// Copyright (c) 2016 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SPIRV_TOOLS_OPTIMIZER_HPP_
#define SPIRV_TOOLS_OPTIMIZER_HPP_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "libspirv.hpp"

namespace spvtools {

// C++ interface for SPIR-V optimization functionalities. It wraps the context
// (including target environment and the corresponding SPIR-V grammar) and
// provides methods for registering optimization passes and optimizing.
//
// Instances of this class provides basic thread-safety guarantee.
class Optimizer {
 public:
  // The token for an optimization pass. It is returned via one of the
  // Create*Pass() standalone functions at the end of this header file and
  // consumed by the RegisterPass() method. Tokens are one-time objects that
  // only support move; copying is not allowed.
  struct PassToken {
    struct Impl;  // Opaque struct for holding inernal data.

    PassToken(std::unique_ptr<Impl>);

    // Tokens can only be moved. Copying is disabled.
    PassToken(const PassToken&) = delete;
    PassToken(PassToken&&);
    PassToken& operator=(const PassToken&) = delete;
    PassToken& operator=(PassToken&&);

    ~PassToken();

    std::unique_ptr<Impl> impl_;  // Unique pointer to internal data.
  };

  // Constructs an instance with the given target |env|, which is used to decode
  // the binaries to be optimized later.
  //
  // The constructed instance will have an empty message consumer, which just
  // ignores all messages from the library. Use SetMessageConsumer() to supply
  // one if messages are of concern.
  explicit Optimizer(spv_target_env env);

  // Disables copy/move constructor/assignment operations.
  Optimizer(const Optimizer&) = delete;
  Optimizer(Optimizer&&) = delete;
  Optimizer& operator=(const Optimizer&) = delete;
  Optimizer& operator=(Optimizer&&) = delete;

  // Destructs this instance.
  ~Optimizer();

  // Sets the message consumer to the given |consumer|. The |consumer| will be
  // invoked once for each message communicated from the library.
  void SetMessageConsumer(MessageConsumer consumer);

  // Registers the given |pass| to this optimizer. Passes will be run in the
  // exact order of registration. The token passed in will be consumed by this
  // method.
  Optimizer& RegisterPass(PassToken&& pass);

  // Optimizes the given SPIR-V module |original_binary| and writes the
  // optimized binary into |optimized_binary|.
  // Returns true on successful optimization, whether or not the module is
  // modified. Returns false if errors occur when processing |original_binary|
  // using any of the registered passes. In that case, no further passes are
  // excuted and the contents in |optimized_binary| may be invalid.
  //
  // It's allowed to alias |original_binary| to the start of |optimized_binary|.
  bool Run(const uint32_t* original_binary, size_t original_binary_size,
           std::vector<uint32_t>* optimized_binary) const;

 private:
  struct Impl;                  // Opaque struct for holding internal data.
  std::unique_ptr<Impl> impl_;  // Unique pointer to internal data.
};

// Creates a null pass.
// A null pass does nothing to the SPIR-V module to be optimized.
Optimizer::PassToken CreateNullPass();

// Creates a strip-debug-info pass.
// A strip-debug-info pass removes all debug instructions (as documented in
// Section 3.32.2 of the SPIR-V spec) of the SPIR-V module to be optimized.
Optimizer::PassToken CreateStripDebugInfoPass();

// Creates a set-spec-constant-default-value pass from a mapping from spec-ids
// to the default values in the form of string.
// A set-spec-constant-default-value pass sets the default values for the
// spec constants that have SpecId decorations (i.e., those defined by
// OpSpecConstant{|True|False} instructions).
Optimizer::PassToken CreateSetSpecConstantDefaultValuePass(
    const std::unordered_map<uint32_t, std::string>& id_value_map);

// Creates a set-spec-constant-default-value pass from a mapping from spec-ids
// to the default values in the form of bit pattern.
// A set-spec-constant-default-value pass sets the default values for the
// spec constants that have SpecId decorations (i.e., those defined by
// OpSpecConstant{|True|False} instructions).
Optimizer::PassToken CreateSetSpecConstantDefaultValuePass(
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& id_value_map);

// Creates a flatten-decoration pass.
// A flatten-decoration pass replaces grouped decorations with equivalent
// ungrouped decorations.  That is, it replaces each OpDecorationGroup
// instruction and associated OpGroupDecorate and OpGroupMemberDecorate
// instructions with equivalent OpDecorate and OpMemberDecorate instructions.
// The pass does not attempt to preserve debug information for instructions
// it removes.
Optimizer::PassToken CreateFlattenDecorationPass();

// Creates a freeze-spec-constant-value pass.
// A freeze-spec-constant pass specializes the value of spec constants to
// their default values. This pass only processes the spec constants that have
// SpecId decorations (defined by OpSpecConstant, OpSpecConstantTrue, or
// OpSpecConstantFalse instructions) and replaces them with their normal
// counterparts (OpConstant, OpConstantTrue, or OpConstantFalse). The
// corresponding SpecId annotation instructions will also be removed. This
// pass does not fold the newly added normal constants and does not process
// other spec constants defined by OpSpecConstantComposite or
// OpSpecConstantOp.
Optimizer::PassToken CreateFreezeSpecConstantValuePass();

// Creates a fold-spec-constant-op-and-composite pass.
// A fold-spec-constant-op-and-composite pass folds spec constants defined by
// OpSpecConstantOp or OpSpecConstantComposite instruction, to normal Constants
// defined by OpConstantTrue, OpConstantFalse, OpConstant, OpConstantNull, or
// OpConstantComposite instructions. Note that spec constants defined with
// OpSpecConstant, OpSpecConstantTrue, or OpSpecConstantFalse instructions are
// not handled, as these instructions indicate their value are not determined
// and can be changed in future. A spec constant is foldable if all of its
// value(s) can be determined from the module. E.g., an integer spec constant
// defined with OpSpecConstantOp instruction can be folded if its value won't
// change later. This pass will replace the original OpSpecContantOp instruction
// with an OpConstant instruction. When folding composite spec constants,
// new instructions may be inserted to define the components of the composite
// constant first, then the original spec constants will be replaced by
// OpConstantComposite instructions.
//
// There are some operations not supported yet:
//   OpSConvert, OpFConvert, OpQuantizeToF16 and
//   all the operations under Kernel capability.
// TODO(qining): Add support for the operations listed above.
Optimizer::PassToken CreateFoldSpecConstantOpAndCompositePass();

// Creates a unify-constant pass.
// A unify-constant pass de-duplicates the constants. Constants with the exact
// same value and identical form will be unified and only one constant will
// be kept for each unique pair of type and value.
// There are several cases not handled by this pass:
//  1) Constants defined by OpConstantNull instructions (null constants) and
//  constants defined by OpConstantFalse, OpConstant or OpConstantComposite
//  with value 0 (zero-valued normal constants) are not considered equivalent.
//  So null constants won't be used to replace zero-valued normal constants,
//  vice versa.
//  2) Whenever there are decorations to the constant's result id id, the
//  constant won't be handled, which means, it won't be used to replace any
//  other constants, neither can other constants replace it.
//  3) NaN in float point format with different bit patterns are not unified.
Optimizer::PassToken CreateUnifyConstantPass();

// Creates a eliminate-dead-constant pass.
// A eliminate-dead-constant pass removes dead constants, including normal
// contants defined by OpConstant, OpConstantComposite, OpConstantTrue, or
// OpConstantFalse and spec constants defined by OpSpecConstant,
// OpSpecConstantComposite, OpSpecConstantTrue, OpSpecConstantFalse or
// OpSpecConstantOp.
Optimizer::PassToken CreateEliminateDeadConstantPass();

// Creates a block merge pass.
// This pass searches for blocks with a single Branch to a block with no
// other predecessors and merges the blocks into a single block. Continue
// blocks and Merge blocks are not candidates for the second block.
//
// The pass is most useful after Dead Branch Elimination, which can leave
// such sequences of blocks. Merging them makes subsequent passes more
// effective, such as single block local store-load elimination.
//
// While this pass reduces the number of occurrences of this sequence, at
// this time it does not guarantee all such sequences are eliminated.
//
// Presence of phi instructions can inhibit this optimization. Handling
// these is left for future improvements. 
Optimizer::PassToken CreateBlockMergePass();

// Creates an inline pass.
// An inline pass exhaustively inlines all function calls in all functions
// designated as an entry point. The intent is to enable, albeit through
// brute force, analysis and optimization across function calls by subsequent
// passes. As the inlining is exhaustive, there is no attempt to optimize for
// size or runtime performance. Functions that are not designated as entry
// points are not changed.
Optimizer::PassToken CreateInlinePass();
  
// Creates a single-block local variable load/store elimination pass.
// For every entry point function, do single block memory optimization of 
// function variables referenced only with non-access-chain loads and stores.
// For each targeted variable load, if previous store to that variable in the
// block, replace the load's result id with the value id of the store.
// If previous load within the block, replace the current load's result id
// with the previous load's result id. In either case, delete the current
// load. Finally, check if any remaining stores are useless, and delete store
// and variable if possible.
//
// The presence of access chain references and function calls can inhibit
// the above optimization.
//
// Only modules with logical addressing are currently processed. 
//
// This pass is most effective if preceeded by Inlining and 
// LocalAccessChainConvert. This pass will reduce the work needed to be done
// by LocalSingleStoreElim and LocalSSARewrite.
Optimizer::PassToken CreateLocalSingleBlockLoadStoreElimPass();

// Creates a local access chain conversion pass.
// A local access chain conversion pass identifies all function scope
// variables which are accessed only with loads, stores and access chains
// with constant indices. It then converts all loads and stores of such
// variables into equivalent sequences of loads, stores, extracts and inserts.
//
// This pass only processes entry point functions. It currently only converts
// non-nested, non-ptr access chains. It does not process modules with
// non-32-bit integer types present. Optional memory access options on loads
// and stores are ignored as we are only processing function scope variables.
//
// This pass unifies access to these variables to a single mode and simplifies
// subsequent analysis and elimination of these variables along with their
// loads and stores allowing values to propagate to their points of use where
// possible.
Optimizer::PassToken CreateLocalAccessChainConvertPass();

// Creates a local single store elimination pass.
// For each entry point function, this pass eliminates loads and stores for 
// function scope variable that are stored to only once, where possible. Only
// whole variable loads and stores are eliminated; access-chain references are
// not optimized. Replace all loads of such variables with the value that is
// stored and eliminate any resulting dead code.
//
// Currently, the presence of access chains and function calls can inhibit this
// pass, however the Inlining and LocalAccessChainConvert passes can make it
// more effective. In additional, many non-load/store memory operations are
// not supported and will prohibit optimization of a function. Support of
// these operations are future work.
//
// This pass will reduce the work needed to be done by LocalSingleBlockElim
// and LocalSSARewrite and can improve the effectiveness of other passes such
// as DeadBranchElimination which depend on values for their analysis.
Optimizer::PassToken CreateLocalSingleStoreElimPass();

// Creates an insert/extract elimination pass.
// This pass processes each entry point function in the module, searching for
// extracts on a sequence of inserts. It further searches the sequence for an
// insert with indices identical to the extract. If such an insert can be
// found before hitting a conflicting insert, the extract's result id is
// replaced with the id of the values from the insert.
//
// Besides removing extracts this pass enables subsequent dead code elimination
// passes to delete the inserts. This pass performs best after access chains are
// converted to inserts and extracts and local loads and stores are eliminated.
Optimizer::PassToken CreateInsertExtractElimPass();

// Creates a compact ids pass.
// The pass remaps result ids to a compact and gapless range starting from %1.
Optimizer::PassToken CreateCompactIdsPass();

}  // namespace spvtools

#endif  // SPIRV_TOOLS_OPTIMIZER_HPP_
