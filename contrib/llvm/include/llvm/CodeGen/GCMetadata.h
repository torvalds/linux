//===- GCMetadata.h - Garbage collector metadata ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the GCFunctionInfo and GCModuleInfo classes, which are
// used as a communication channel from the target code generator to the target
// garbage collectors. This interface allows code generators and garbage
// collectors to be developed independently.
//
// The GCFunctionInfo class logs the data necessary to build a type accurate
// stack map. The code generator outputs:
//
//   - Safe points as specified by the GCStrategy's NeededSafePoints.
//   - Stack offsets for GC roots, as specified by calls to llvm.gcroot
//
// As a refinement, liveness analysis calculates the set of live roots at each
// safe point. Liveness analysis is not presently performed by the code
// generator, so all roots are assumed live.
//
// GCModuleInfo simply collects GCFunctionInfo instances for each Function as
// they are compiled. This accretion is necessary for collectors which must emit
// a stack map for the compilation unit as a whole. Therefore, GCFunctionInfo
// outlives the MachineFunction from which it is derived and must not refer to
// any code generator data structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GCMETADATA_H
#define LLVM_CODEGEN_GCMETADATA_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/GCStrategy.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {

class Constant;
class Function;
class MCSymbol;

/// GCPoint - Metadata for a collector-safe point in machine code.
///
struct GCPoint {
  MCSymbol *Label;    ///< A label.
  DebugLoc Loc;

  GCPoint(MCSymbol *L, DebugLoc DL)
      : Label(L), Loc(std::move(DL)) {}
};

/// GCRoot - Metadata for a pointer to an object managed by the garbage
/// collector.
struct GCRoot {
  int Num;                  ///< Usually a frame index.
  int StackOffset = -1;     ///< Offset from the stack pointer.
  const Constant *Metadata; ///< Metadata straight from the call
                            ///< to llvm.gcroot.

  GCRoot(int N, const Constant *MD) : Num(N), Metadata(MD) {}
};

/// Garbage collection metadata for a single function.  Currently, this
/// information only applies to GCStrategies which use GCRoot.
class GCFunctionInfo {
public:
  using iterator = std::vector<GCPoint>::iterator;
  using roots_iterator = std::vector<GCRoot>::iterator;
  using live_iterator = std::vector<GCRoot>::const_iterator;

private:
  const Function &F;
  GCStrategy &S;
  uint64_t FrameSize;
  std::vector<GCRoot> Roots;
  std::vector<GCPoint> SafePoints;

  // FIXME: Liveness. A 2D BitVector, perhaps?
  //
  //   BitVector Liveness;
  //
  //   bool islive(int point, int root) =
  //     Liveness[point * SafePoints.size() + root]
  //
  // The bit vector is the more compact representation where >3.2% of roots
  // are live per safe point (1.5% on 64-bit hosts).

public:
  GCFunctionInfo(const Function &F, GCStrategy &S);
  ~GCFunctionInfo();

  /// getFunction - Return the function to which this metadata applies.
  const Function &getFunction() const { return F; }

  /// getStrategy - Return the GC strategy for the function.
  GCStrategy &getStrategy() { return S; }

  /// addStackRoot - Registers a root that lives on the stack. Num is the
  ///                stack object ID for the alloca (if the code generator is
  //                 using  MachineFrameInfo).
  void addStackRoot(int Num, const Constant *Metadata) {
    Roots.push_back(GCRoot(Num, Metadata));
  }

  /// removeStackRoot - Removes a root.
  roots_iterator removeStackRoot(roots_iterator position) {
    return Roots.erase(position);
  }

  /// addSafePoint - Notes the existence of a safe point. Num is the ID of the
  /// label just prior to the safe point (if the code generator is using
  /// MachineModuleInfo).
  void addSafePoint(MCSymbol *Label, const DebugLoc &DL) {
    SafePoints.emplace_back(Label, DL);
  }

  /// getFrameSize/setFrameSize - Records the function's frame size.
  uint64_t getFrameSize() const { return FrameSize; }
  void setFrameSize(uint64_t S) { FrameSize = S; }

  /// begin/end - Iterators for safe points.
  iterator begin() { return SafePoints.begin(); }
  iterator end() { return SafePoints.end(); }
  size_t size() const { return SafePoints.size(); }

  /// roots_begin/roots_end - Iterators for all roots in the function.
  roots_iterator roots_begin() { return Roots.begin(); }
  roots_iterator roots_end() { return Roots.end(); }
  size_t roots_size() const { return Roots.size(); }

  /// live_begin/live_end - Iterators for live roots at a given safe point.
  live_iterator live_begin(const iterator &p) { return roots_begin(); }
  live_iterator live_end(const iterator &p) { return roots_end(); }
  size_t live_size(const iterator &p) const { return roots_size(); }
};

/// An analysis pass which caches information about the entire Module.
/// Records both the function level information used by GCRoots and a
/// cache of the 'active' gc strategy objects for the current Module.
class GCModuleInfo : public ImmutablePass {
  /// An owning list of all GCStrategies which have been created
  SmallVector<std::unique_ptr<GCStrategy>, 1> GCStrategyList;
  /// A helper map to speedup lookups into the above list
  StringMap<GCStrategy*> GCStrategyMap;

public:
  /// Lookup the GCStrategy object associated with the given gc name.
  /// Objects are owned internally; No caller should attempt to delete the
  /// returned objects.
  GCStrategy *getGCStrategy(const StringRef Name);

  /// List of per function info objects.  In theory, Each of these
  /// may be associated with a different GC.
  using FuncInfoVec = std::vector<std::unique_ptr<GCFunctionInfo>>;

  FuncInfoVec::iterator funcinfo_begin() { return Functions.begin(); }
  FuncInfoVec::iterator funcinfo_end() { return Functions.end(); }

private:
  /// Owning list of all GCFunctionInfos associated with this Module
  FuncInfoVec Functions;

  /// Non-owning map to bypass linear search when finding the GCFunctionInfo
  /// associated with a particular Function.
  using finfo_map_type = DenseMap<const Function *, GCFunctionInfo *>;
  finfo_map_type FInfoMap;

public:
  using iterator = SmallVector<std::unique_ptr<GCStrategy>, 1>::const_iterator;

  static char ID;

  GCModuleInfo();

  /// clear - Resets the pass. Any pass, which uses GCModuleInfo, should
  /// call it in doFinalization().
  ///
  void clear();

  /// begin/end - Iterators for used strategies.
  ///
  iterator begin() const { return GCStrategyList.begin(); }
  iterator end() const { return GCStrategyList.end(); }

  /// get - Look up function metadata.  This is currently assumed
  /// have the side effect of initializing the associated GCStrategy.  That
  /// will soon change.
  GCFunctionInfo &getFunctionInfo(const Function &F);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_GCMETADATA_H
