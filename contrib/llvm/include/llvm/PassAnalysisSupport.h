//===- llvm/PassAnalysisSupport.h - Analysis Pass Support code --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines stuff that is used to define and "use" Analysis Passes.
// This file is automatically #included by Pass.h, so:
//
//           NO .CPP FILES SHOULD INCLUDE THIS FILE DIRECTLY
//
// Instead, #include Pass.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSANALYSISSUPPORT_H
#define LLVM_PASSANALYSISSUPPORT_H

#include "Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <utility>
#include <vector>

namespace llvm {

class Function;
class Pass;
class PMDataManager;

//===----------------------------------------------------------------------===//
/// Represent the analysis usage information of a pass.  This tracks analyses
/// that the pass REQUIRES (must be available when the pass runs), REQUIRES
/// TRANSITIVE (must be available throughout the lifetime of the pass), and
/// analyses that the pass PRESERVES (the pass does not invalidate the results
/// of these analyses).  This information is provided by a pass to the Pass
/// infrastructure through the getAnalysisUsage virtual function.
///
class AnalysisUsage {
public:
  using VectorType = SmallVectorImpl<AnalysisID>;

private:
  /// Sets of analyses required and preserved by a pass
  // TODO: It's not clear that SmallVector is an appropriate data structure for
  // this usecase.  The sizes were picked to minimize wasted space, but are
  // otherwise fairly meaningless.
  SmallVector<AnalysisID, 8> Required;
  SmallVector<AnalysisID, 2> RequiredTransitive;
  SmallVector<AnalysisID, 2> Preserved;
  SmallVector<AnalysisID, 0> Used;
  bool PreservesAll = false;

public:
  AnalysisUsage() = default;

  ///@{
  /// Add the specified ID to the required set of the usage info for a pass.
  AnalysisUsage &addRequiredID(const void *ID);
  AnalysisUsage &addRequiredID(char &ID);
  template<class PassClass>
  AnalysisUsage &addRequired() {
    return addRequiredID(PassClass::ID);
  }

  AnalysisUsage &addRequiredTransitiveID(char &ID);
  template<class PassClass>
  AnalysisUsage &addRequiredTransitive() {
    return addRequiredTransitiveID(PassClass::ID);
  }
  ///@}

  ///@{
  /// Add the specified ID to the set of analyses preserved by this pass.
  AnalysisUsage &addPreservedID(const void *ID) {
    Preserved.push_back(ID);
    return *this;
  }
  AnalysisUsage &addPreservedID(char &ID) {
    Preserved.push_back(&ID);
    return *this;
  }
  /// Add the specified Pass class to the set of analyses preserved by this pass.
  template<class PassClass>
  AnalysisUsage &addPreserved() {
    Preserved.push_back(&PassClass::ID);
    return *this;
  }
  ///@}

  ///@{
  /// Add the specified ID to the set of analyses used by this pass if they are
  /// available..
  AnalysisUsage &addUsedIfAvailableID(const void *ID) {
    Used.push_back(ID);
    return *this;
  }
  AnalysisUsage &addUsedIfAvailableID(char &ID) {
    Used.push_back(&ID);
    return *this;
  }
  /// Add the specified Pass class to the set of analyses used by this pass.
  template<class PassClass>
  AnalysisUsage &addUsedIfAvailable() {
    Used.push_back(&PassClass::ID);
    return *this;
  }
  ///@}

  /// Add the Pass with the specified argument string to the set of analyses
  /// preserved by this pass. If no such Pass exists, do nothing. This can be
  /// useful when a pass is trivially preserved, but may not be linked in. Be
  /// careful about spelling!
  AnalysisUsage &addPreserved(StringRef Arg);

  /// Set by analyses that do not transform their input at all
  void setPreservesAll() { PreservesAll = true; }

  /// Determine whether a pass said it does not transform its input at all
  bool getPreservesAll() const { return PreservesAll; }

  /// This function should be called by the pass, iff they do not:
  ///
  ///  1. Add or remove basic blocks from the function
  ///  2. Modify terminator instructions in any way.
  ///
  /// This function annotates the AnalysisUsage info object to say that analyses
  /// that only depend on the CFG are preserved by this pass.
  void setPreservesCFG();

  const VectorType &getRequiredSet() const { return Required; }
  const VectorType &getRequiredTransitiveSet() const {
    return RequiredTransitive;
  }
  const VectorType &getPreservedSet() const { return Preserved; }
  const VectorType &getUsedSet() const { return Used; }
};

//===----------------------------------------------------------------------===//
/// AnalysisResolver - Simple interface used by Pass objects to pull all
/// analysis information out of pass manager that is responsible to manage
/// the pass.
///
class AnalysisResolver {
public:
  AnalysisResolver() = delete;
  explicit AnalysisResolver(PMDataManager &P) : PM(P) {}

  PMDataManager &getPMDataManager() { return PM; }

  /// Find pass that is implementing PI.
  Pass *findImplPass(AnalysisID PI) {
    Pass *ResultPass = nullptr;
    for (const auto &AnalysisImpl : AnalysisImpls) {
      if (AnalysisImpl.first == PI) {
        ResultPass = AnalysisImpl.second;
        break;
      }
    }
    return ResultPass;
  }

  /// Find pass that is implementing PI. Initialize pass for Function F.
  Pass *findImplPass(Pass *P, AnalysisID PI, Function &F);

  void addAnalysisImplsPair(AnalysisID PI, Pass *P) {
    if (findImplPass(PI) == P)
      return;
    std::pair<AnalysisID, Pass*> pir = std::make_pair(PI,P);
    AnalysisImpls.push_back(pir);
  }

  /// Clear cache that is used to connect a pass to the analysis (PassInfo).
  void clearAnalysisImpls() {
    AnalysisImpls.clear();
  }

  /// Return analysis result or null if it doesn't exist.
  Pass *getAnalysisIfAvailable(AnalysisID ID, bool Direction) const;

private:
  /// This keeps track of which passes implements the interfaces that are
  /// required by the current pass (to implement getAnalysis()).
  std::vector<std::pair<AnalysisID, Pass *>> AnalysisImpls;

  /// PassManager that is used to resolve analysis info
  PMDataManager &PM;
};

/// getAnalysisIfAvailable<AnalysisType>() - Subclasses use this function to
/// get analysis information that might be around, for example to update it.
/// This is different than getAnalysis in that it can fail (if the analysis
/// results haven't been computed), so should only be used if you can handle
/// the case when the analysis is not available.  This method is often used by
/// transformation APIs to update analysis results for a pass automatically as
/// the transform is performed.
template<typename AnalysisType>
AnalysisType *Pass::getAnalysisIfAvailable() const {
  assert(Resolver && "Pass not resident in a PassManager object!");

  const void *PI = &AnalysisType::ID;

  Pass *ResultPass = Resolver->getAnalysisIfAvailable(PI, true);
  if (!ResultPass) return nullptr;

  // Because the AnalysisType may not be a subclass of pass (for
  // AnalysisGroups), we use getAdjustedAnalysisPointer here to potentially
  // adjust the return pointer (because the class may multiply inherit, once
  // from pass, once from AnalysisType).
  return (AnalysisType*)ResultPass->getAdjustedAnalysisPointer(PI);
}

/// getAnalysis<AnalysisType>() - This function is used by subclasses to get
/// to the analysis information that they claim to use by overriding the
/// getAnalysisUsage function.
template<typename AnalysisType>
AnalysisType &Pass::getAnalysis() const {
  assert(Resolver && "Pass has not been inserted into a PassManager object!");
  return getAnalysisID<AnalysisType>(&AnalysisType::ID);
}

template<typename AnalysisType>
AnalysisType &Pass::getAnalysisID(AnalysisID PI) const {
  assert(PI && "getAnalysis for unregistered pass!");
  assert(Resolver&&"Pass has not been inserted into a PassManager object!");
  // PI *must* appear in AnalysisImpls.  Because the number of passes used
  // should be a small number, we just do a linear search over a (dense)
  // vector.
  Pass *ResultPass = Resolver->findImplPass(PI);
  assert(ResultPass &&
         "getAnalysis*() called on an analysis that was not "
         "'required' by pass!");

  // Because the AnalysisType may not be a subclass of pass (for
  // AnalysisGroups), we use getAdjustedAnalysisPointer here to potentially
  // adjust the return pointer (because the class may multiply inherit, once
  // from pass, once from AnalysisType).
  return *(AnalysisType*)ResultPass->getAdjustedAnalysisPointer(PI);
}

/// getAnalysis<AnalysisType>() - This function is used by subclasses to get
/// to the analysis information that they claim to use by overriding the
/// getAnalysisUsage function.
template<typename AnalysisType>
AnalysisType &Pass::getAnalysis(Function &F) {
  assert(Resolver &&"Pass has not been inserted into a PassManager object!");

  return getAnalysisID<AnalysisType>(&AnalysisType::ID, F);
}

template<typename AnalysisType>
AnalysisType &Pass::getAnalysisID(AnalysisID PI, Function &F) {
  assert(PI && "getAnalysis for unregistered pass!");
  assert(Resolver && "Pass has not been inserted into a PassManager object!");
  // PI *must* appear in AnalysisImpls.  Because the number of passes used
  // should be a small number, we just do a linear search over a (dense)
  // vector.
  Pass *ResultPass = Resolver->findImplPass(this, PI, F);
  assert(ResultPass && "Unable to find requested analysis info");

  // Because the AnalysisType may not be a subclass of pass (for
  // AnalysisGroups), we use getAdjustedAnalysisPointer here to potentially
  // adjust the return pointer (because the class may multiply inherit, once
  // from pass, once from AnalysisType).
  return *(AnalysisType*)ResultPass->getAdjustedAnalysisPointer(PI);
}

} // end namespace llvm

#endif // LLVM_PASSANALYSISSUPPORT_H
