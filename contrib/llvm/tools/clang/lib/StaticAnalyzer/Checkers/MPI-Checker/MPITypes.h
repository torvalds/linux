//===-- MPITypes.h - Functionality to model MPI concepts --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides definitions to model concepts of MPI. The mpi::Request
/// class defines a wrapper class, in order to make MPI requests trackable for
/// path-sensitive analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MPICHECKER_MPITYPES_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MPICHECKER_MPITYPES_H

#include "clang/StaticAnalyzer/Checkers/MPIFunctionClassifier.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "llvm/ADT/SmallSet.h"

namespace clang {
namespace ento {
namespace mpi {

class Request {
public:
  enum State : unsigned char { Nonblocking, Wait };

  Request(State S) : CurrentState{S} {}

  void Profile(llvm::FoldingSetNodeID &Id) const {
    Id.AddInteger(CurrentState);
  }

  bool operator==(const Request &ToCompare) const {
    return CurrentState == ToCompare.CurrentState;
  }

  const State CurrentState;
};

// The RequestMap stores MPI requests which are identified by their memory
// region. Requests are used in MPI to complete nonblocking operations with wait
// operations. A custom map implementation is used, in order to make it
// available in an arbitrary amount of translation units.
struct RequestMap {};
typedef llvm::ImmutableMap<const clang::ento::MemRegion *,
                           clang::ento::mpi::Request>
    RequestMapImpl;

} // end of namespace: mpi

template <>
struct ProgramStateTrait<mpi::RequestMap>
    : public ProgramStatePartialTrait<mpi::RequestMapImpl> {
  static void *GDMIndex() {
    static int index = 0;
    return &index;
  }
};

} // end of namespace: ento
} // end of namespace: clang
#endif
