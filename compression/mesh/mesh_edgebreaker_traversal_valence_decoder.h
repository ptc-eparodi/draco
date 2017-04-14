// Copyright 2016 The Draco Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#ifndef DRACO_COMPRESSION_MESH_MESH_EDGEBREAKER_TRAVERSAL_VALENCE_DECODER_H_
#define DRACO_COMPRESSION_MESH_MESH_EDGEBREAKER_TRAVERSAL_VALENCE_DECODER_H_

#include "compression/mesh/mesh_edgebreaker_traversal_decoder.h"
#include "core/symbol_decoding.h"
#include "core/varint_decoding.h"

namespace draco {

// Decoder for traversal encoded with MeshEdgeBreakerTraversalValenceEncoder.
// The decoder maintains valences of the decoded portion of the traversed mesh
// and it uses them to select entropy context used for decoding of the actual
// symbols.
class MeshEdgeBreakerTraversalValenceDecoder
    : public MeshEdgeBreakerTraversalDecoder {
 public:
  MeshEdgeBreakerTraversalValenceDecoder()
      : corner_table_(nullptr),
        num_vertices_(0),
        last_symbol_(-1),
        active_context_(-1),
        min_valence_(2),
        max_valence_(7) {}
  void Init(MeshEdgeBreakerDecoderImplInterface *decoder) {
    MeshEdgeBreakerTraversalDecoder::Init(decoder);
    corner_table_ = decoder->GetCornerTable();
  }
  void SetNumEncodedVertices(int num_vertices) { num_vertices_ = num_vertices; }

  bool Start(DecoderBuffer *out_buffer) {
    if (!MeshEdgeBreakerTraversalDecoder::Start(out_buffer))
      return false;
    int32_t num_split_symbols;
    if (!out_buffer->Decode(&num_split_symbols))
      return false;
    // Add one extra vertex for each split symbol.
    num_vertices_ += num_split_symbols;
    // Set the valences of all initial vertices to 0.
    vertex_valences_.resize(num_vertices_, 0);

    int8_t mode;
    if (!out_buffer->Decode(&mode))
      return false;
    if (mode == EDGEBREAKER_VALENCE_MODE_2_7) {
      min_valence_ = 2;
      max_valence_ = 7;
    } else {
      // Unsupported mode.
      return false;
    }

    const int num_unique_valences = max_valence_ - min_valence_ + 1;

    // Decode all symbols for all contexts.
    context_symbols_.resize(num_unique_valences);
    context_counters_.resize(context_symbols_.size());
    for (int i = 0; i < context_symbols_.size(); ++i) {
      uint32_t num_symbols;
      DecodeVarint<uint32_t>(&num_symbols, out_buffer);
      if (num_symbols > 0) {
        context_symbols_[i].resize(num_symbols);
        DecodeSymbols(num_symbols, 1, out_buffer, context_symbols_[i].data());
        // All symbols are going to be processed from the back.
        context_counters_[i] = num_symbols;
      }
    }
    return true;
  }

  inline uint32_t DecodeSymbol() {
    // First check if we have a valid context.
    if (active_context_ != -1) {
      const int symbol_id =
          context_symbols_[active_context_]
                          [--context_counters_[active_context_]];
      last_symbol_ = edge_breaker_symbol_to_topology_id[symbol_id];
    } else {
      // We don't have a predicted symbol or the symbol was mis-predicted.
      // Decode it directly.
      last_symbol_ = MeshEdgeBreakerTraversalDecoder::DecodeSymbol();
    }
    return last_symbol_;
  }

  inline void NewActiveCornerReached(CornerIndex corner) {
    const CornerIndex next = corner_table_->Next(corner);
    const CornerIndex prev = corner_table_->Previous(corner);
    // Update valences.
    switch (last_symbol_) {
      case TOPOLOGY_C:
      case TOPOLOGY_S:
        vertex_valences_[corner_table_->Vertex(next)] += 1;
        vertex_valences_[corner_table_->Vertex(prev)] += 1;
        break;
      case TOPOLOGY_R:
        vertex_valences_[corner_table_->Vertex(corner)] += 1;
        vertex_valences_[corner_table_->Vertex(next)] += 1;
        vertex_valences_[corner_table_->Vertex(prev)] += 2;
        break;
      case TOPOLOGY_L:
        vertex_valences_[corner_table_->Vertex(corner)] += 1;
        vertex_valences_[corner_table_->Vertex(next)] += 2;
        vertex_valences_[corner_table_->Vertex(prev)] += 1;
        break;
      case TOPOLOGY_E:
        vertex_valences_[corner_table_->Vertex(corner)] += 2;
        vertex_valences_[corner_table_->Vertex(next)] += 2;
        vertex_valences_[corner_table_->Vertex(prev)] += 2;
        break;
      default:
        break;
    }
    // Compute the new context that is going to be used to decode the next
    // symbol.
    const int active_valence = vertex_valences_[corner_table_->Vertex(next)];
    int clamped_valence;
    if (active_valence < min_valence_) {
      clamped_valence = min_valence_;
    } else if (active_valence > max_valence_) {
      clamped_valence = max_valence_;
    } else {
      clamped_valence = active_valence;
    }

    active_context_ = (clamped_valence - min_valence_);
  }

  inline void MergeVertices(VertexIndex dest, VertexIndex source) {
    // Update valences on the merged vertices.
    vertex_valences_[dest] += vertex_valences_[source];
  }

 private:
  const CornerTable *corner_table_;
  int num_vertices_;
  IndexTypeVector<VertexIndex, int> vertex_valences_;
  int last_symbol_;
  int active_context_;

  int min_valence_;
  int max_valence_;
  std::vector<std::vector<uint32_t>> context_symbols_;
  // Points to the active symbol in each context.
  std::vector<int> context_counters_;
};

}  // namespace draco

#endif  // DRACO_COMPRESSION_MESH_MESH_EDGEBREAKER_TRAVERSAL_VALENCE_DECODER_H_