//===-- MPIFunctionClassifier.cpp - classifies MPI functions ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines functionality to identify and classify MPI functions.
///
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/MPIFunctionClassifier.h"
#include "llvm/ADT/STLExtras.h"

namespace clang {
namespace ento {
namespace mpi {

void MPIFunctionClassifier::identifierInit(ASTContext &ASTCtx) {
  // Initialize function identifiers.
  initPointToPointIdentifiers(ASTCtx);
  initCollectiveIdentifiers(ASTCtx);
  initAdditionalIdentifiers(ASTCtx);
}

void MPIFunctionClassifier::initPointToPointIdentifiers(ASTContext &ASTCtx) {
  // Copy identifiers into the correct classification containers.
  IdentInfo_MPI_Send = &ASTCtx.Idents.get("MPI_Send");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Send);
  MPIType.push_back(IdentInfo_MPI_Send);
  assert(IdentInfo_MPI_Send);

  IdentInfo_MPI_Isend = &ASTCtx.Idents.get("MPI_Isend");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Isend);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Isend);
  MPIType.push_back(IdentInfo_MPI_Isend);
  assert(IdentInfo_MPI_Isend);

  IdentInfo_MPI_Ssend = &ASTCtx.Idents.get("MPI_Ssend");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Ssend);
  MPIType.push_back(IdentInfo_MPI_Ssend);
  assert(IdentInfo_MPI_Ssend);

  IdentInfo_MPI_Issend = &ASTCtx.Idents.get("MPI_Issend");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Issend);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Issend);
  MPIType.push_back(IdentInfo_MPI_Issend);
  assert(IdentInfo_MPI_Issend);

  IdentInfo_MPI_Bsend = &ASTCtx.Idents.get("MPI_Bsend");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Bsend);
  MPIType.push_back(IdentInfo_MPI_Bsend);
  assert(IdentInfo_MPI_Bsend);

  IdentInfo_MPI_Ibsend = &ASTCtx.Idents.get("MPI_Ibsend");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Ibsend);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Ibsend);
  MPIType.push_back(IdentInfo_MPI_Ibsend);
  assert(IdentInfo_MPI_Ibsend);

  IdentInfo_MPI_Rsend = &ASTCtx.Idents.get("MPI_Rsend");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Rsend);
  MPIType.push_back(IdentInfo_MPI_Rsend);
  assert(IdentInfo_MPI_Rsend);

  IdentInfo_MPI_Irsend = &ASTCtx.Idents.get("MPI_Irsend");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Irsend);
  MPIType.push_back(IdentInfo_MPI_Irsend);
  assert(IdentInfo_MPI_Irsend);

  IdentInfo_MPI_Recv = &ASTCtx.Idents.get("MPI_Recv");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Recv);
  MPIType.push_back(IdentInfo_MPI_Recv);
  assert(IdentInfo_MPI_Recv);

  IdentInfo_MPI_Irecv = &ASTCtx.Idents.get("MPI_Irecv");
  MPIPointToPointTypes.push_back(IdentInfo_MPI_Irecv);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Irecv);
  MPIType.push_back(IdentInfo_MPI_Irecv);
  assert(IdentInfo_MPI_Irecv);
}

void MPIFunctionClassifier::initCollectiveIdentifiers(ASTContext &ASTCtx) {
  // Copy identifiers into the correct classification containers.
  IdentInfo_MPI_Scatter = &ASTCtx.Idents.get("MPI_Scatter");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Scatter);
  MPIPointToCollTypes.push_back(IdentInfo_MPI_Scatter);
  MPIType.push_back(IdentInfo_MPI_Scatter);
  assert(IdentInfo_MPI_Scatter);

  IdentInfo_MPI_Iscatter = &ASTCtx.Idents.get("MPI_Iscatter");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Iscatter);
  MPIPointToCollTypes.push_back(IdentInfo_MPI_Iscatter);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Iscatter);
  MPIType.push_back(IdentInfo_MPI_Iscatter);
  assert(IdentInfo_MPI_Iscatter);

  IdentInfo_MPI_Gather = &ASTCtx.Idents.get("MPI_Gather");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Gather);
  MPICollToPointTypes.push_back(IdentInfo_MPI_Gather);
  MPIType.push_back(IdentInfo_MPI_Gather);
  assert(IdentInfo_MPI_Gather);

  IdentInfo_MPI_Igather = &ASTCtx.Idents.get("MPI_Igather");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Igather);
  MPICollToPointTypes.push_back(IdentInfo_MPI_Igather);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Igather);
  MPIType.push_back(IdentInfo_MPI_Igather);
  assert(IdentInfo_MPI_Igather);

  IdentInfo_MPI_Allgather = &ASTCtx.Idents.get("MPI_Allgather");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Allgather);
  MPICollToCollTypes.push_back(IdentInfo_MPI_Allgather);
  MPIType.push_back(IdentInfo_MPI_Allgather);
  assert(IdentInfo_MPI_Allgather);

  IdentInfo_MPI_Iallgather = &ASTCtx.Idents.get("MPI_Iallgather");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Iallgather);
  MPICollToCollTypes.push_back(IdentInfo_MPI_Iallgather);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Iallgather);
  MPIType.push_back(IdentInfo_MPI_Iallgather);
  assert(IdentInfo_MPI_Iallgather);

  IdentInfo_MPI_Bcast = &ASTCtx.Idents.get("MPI_Bcast");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Bcast);
  MPIPointToCollTypes.push_back(IdentInfo_MPI_Bcast);
  MPIType.push_back(IdentInfo_MPI_Bcast);
  assert(IdentInfo_MPI_Bcast);

  IdentInfo_MPI_Ibcast = &ASTCtx.Idents.get("MPI_Ibcast");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Ibcast);
  MPIPointToCollTypes.push_back(IdentInfo_MPI_Ibcast);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Ibcast);
  MPIType.push_back(IdentInfo_MPI_Ibcast);
  assert(IdentInfo_MPI_Ibcast);

  IdentInfo_MPI_Reduce = &ASTCtx.Idents.get("MPI_Reduce");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Reduce);
  MPICollToPointTypes.push_back(IdentInfo_MPI_Reduce);
  MPIType.push_back(IdentInfo_MPI_Reduce);
  assert(IdentInfo_MPI_Reduce);

  IdentInfo_MPI_Ireduce = &ASTCtx.Idents.get("MPI_Ireduce");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Ireduce);
  MPICollToPointTypes.push_back(IdentInfo_MPI_Ireduce);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Ireduce);
  MPIType.push_back(IdentInfo_MPI_Ireduce);
  assert(IdentInfo_MPI_Ireduce);

  IdentInfo_MPI_Allreduce = &ASTCtx.Idents.get("MPI_Allreduce");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Allreduce);
  MPICollToCollTypes.push_back(IdentInfo_MPI_Allreduce);
  MPIType.push_back(IdentInfo_MPI_Allreduce);
  assert(IdentInfo_MPI_Allreduce);

  IdentInfo_MPI_Iallreduce = &ASTCtx.Idents.get("MPI_Iallreduce");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Iallreduce);
  MPICollToCollTypes.push_back(IdentInfo_MPI_Iallreduce);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Iallreduce);
  MPIType.push_back(IdentInfo_MPI_Iallreduce);
  assert(IdentInfo_MPI_Iallreduce);

  IdentInfo_MPI_Alltoall = &ASTCtx.Idents.get("MPI_Alltoall");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Alltoall);
  MPICollToCollTypes.push_back(IdentInfo_MPI_Alltoall);
  MPIType.push_back(IdentInfo_MPI_Alltoall);
  assert(IdentInfo_MPI_Alltoall);

  IdentInfo_MPI_Ialltoall = &ASTCtx.Idents.get("MPI_Ialltoall");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Ialltoall);
  MPICollToCollTypes.push_back(IdentInfo_MPI_Ialltoall);
  MPINonBlockingTypes.push_back(IdentInfo_MPI_Ialltoall);
  MPIType.push_back(IdentInfo_MPI_Ialltoall);
  assert(IdentInfo_MPI_Ialltoall);
}

void MPIFunctionClassifier::initAdditionalIdentifiers(ASTContext &ASTCtx) {
  IdentInfo_MPI_Comm_rank = &ASTCtx.Idents.get("MPI_Comm_rank");
  MPIType.push_back(IdentInfo_MPI_Comm_rank);
  assert(IdentInfo_MPI_Comm_rank);

  IdentInfo_MPI_Comm_size = &ASTCtx.Idents.get("MPI_Comm_size");
  MPIType.push_back(IdentInfo_MPI_Comm_size);
  assert(IdentInfo_MPI_Comm_size);

  IdentInfo_MPI_Wait = &ASTCtx.Idents.get("MPI_Wait");
  MPIType.push_back(IdentInfo_MPI_Wait);
  assert(IdentInfo_MPI_Wait);

  IdentInfo_MPI_Waitall = &ASTCtx.Idents.get("MPI_Waitall");
  MPIType.push_back(IdentInfo_MPI_Waitall);
  assert(IdentInfo_MPI_Waitall);

  IdentInfo_MPI_Barrier = &ASTCtx.Idents.get("MPI_Barrier");
  MPICollectiveTypes.push_back(IdentInfo_MPI_Barrier);
  MPIType.push_back(IdentInfo_MPI_Barrier);
  assert(IdentInfo_MPI_Barrier);
}

// general identifiers
bool MPIFunctionClassifier::isMPIType(const IdentifierInfo *IdentInfo) const {
  return llvm::is_contained(MPIType, IdentInfo);
}

bool MPIFunctionClassifier::isNonBlockingType(
    const IdentifierInfo *IdentInfo) const {
  return llvm::is_contained(MPINonBlockingTypes, IdentInfo);
}

// point-to-point identifiers
bool MPIFunctionClassifier::isPointToPointType(
    const IdentifierInfo *IdentInfo) const {
  return llvm::is_contained(MPIPointToPointTypes, IdentInfo);
}

// collective identifiers
bool MPIFunctionClassifier::isCollectiveType(
    const IdentifierInfo *IdentInfo) const {
  return llvm::is_contained(MPICollectiveTypes, IdentInfo);
}

bool MPIFunctionClassifier::isCollToColl(
    const IdentifierInfo *IdentInfo) const {
  return llvm::is_contained(MPICollToCollTypes, IdentInfo);
}

bool MPIFunctionClassifier::isScatterType(
    const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Scatter ||
         IdentInfo == IdentInfo_MPI_Iscatter;
}

bool MPIFunctionClassifier::isGatherType(
    const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Gather ||
         IdentInfo == IdentInfo_MPI_Igather ||
         IdentInfo == IdentInfo_MPI_Allgather ||
         IdentInfo == IdentInfo_MPI_Iallgather;
}

bool MPIFunctionClassifier::isAllgatherType(
    const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Allgather ||
         IdentInfo == IdentInfo_MPI_Iallgather;
}

bool MPIFunctionClassifier::isAlltoallType(
    const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Alltoall ||
         IdentInfo == IdentInfo_MPI_Ialltoall;
}

bool MPIFunctionClassifier::isBcastType(const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Bcast || IdentInfo == IdentInfo_MPI_Ibcast;
}

bool MPIFunctionClassifier::isReduceType(
    const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Reduce ||
         IdentInfo == IdentInfo_MPI_Ireduce ||
         IdentInfo == IdentInfo_MPI_Allreduce ||
         IdentInfo == IdentInfo_MPI_Iallreduce;
}

// additional identifiers
bool MPIFunctionClassifier::isMPI_Wait(const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Wait;
}

bool MPIFunctionClassifier::isMPI_Waitall(
    const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Waitall;
}

bool MPIFunctionClassifier::isWaitType(const IdentifierInfo *IdentInfo) const {
  return IdentInfo == IdentInfo_MPI_Wait || IdentInfo == IdentInfo_MPI_Waitall;
}

} // end of namespace: mpi
} // end of namespace: ento
} // end of namespace: clang
