//===-- MPIFunctionClassifier.h - classifies MPI functions ----*- C++ -*-===//
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

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MPICHECKER_MPIFUNCTIONCLASSIFIER_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MPICHECKER_MPIFUNCTIONCLASSIFIER_H

#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

namespace clang {
namespace ento {
namespace mpi {

class MPIFunctionClassifier {
public:
  MPIFunctionClassifier(ASTContext &ASTCtx) { identifierInit(ASTCtx); }

  // general identifiers
  bool isMPIType(const IdentifierInfo *const IdentInfo) const;
  bool isNonBlockingType(const IdentifierInfo *const IdentInfo) const;

  // point-to-point identifiers
  bool isPointToPointType(const IdentifierInfo *const IdentInfo) const;

  // collective identifiers
  bool isCollectiveType(const IdentifierInfo *const IdentInfo) const;
  bool isCollToColl(const IdentifierInfo *const IdentInfo) const;
  bool isScatterType(const IdentifierInfo *const IdentInfo) const;
  bool isGatherType(const IdentifierInfo *const IdentInfo) const;
  bool isAllgatherType(const IdentifierInfo *const IdentInfo) const;
  bool isAlltoallType(const IdentifierInfo *const IdentInfo) const;
  bool isReduceType(const IdentifierInfo *const IdentInfo) const;
  bool isBcastType(const IdentifierInfo *const IdentInfo) const;

  // additional identifiers
  bool isMPI_Wait(const IdentifierInfo *const IdentInfo) const;
  bool isMPI_Waitall(const IdentifierInfo *const IdentInfo) const;
  bool isWaitType(const IdentifierInfo *const IdentInfo) const;

private:
  // Initializes function identifiers, to recognize them during analysis.
  void identifierInit(ASTContext &ASTCtx);
  void initPointToPointIdentifiers(ASTContext &ASTCtx);
  void initCollectiveIdentifiers(ASTContext &ASTCtx);
  void initAdditionalIdentifiers(ASTContext &ASTCtx);

  // The containers are used, to enable classification of MPI-functions during
  // analysis.
  llvm::SmallVector<IdentifierInfo *, 12> MPINonBlockingTypes;

  llvm::SmallVector<IdentifierInfo *, 10> MPIPointToPointTypes;
  llvm::SmallVector<IdentifierInfo *, 16> MPICollectiveTypes;

  llvm::SmallVector<IdentifierInfo *, 4> MPIPointToCollTypes;
  llvm::SmallVector<IdentifierInfo *, 4> MPICollToPointTypes;
  llvm::SmallVector<IdentifierInfo *, 6> MPICollToCollTypes;

  llvm::SmallVector<IdentifierInfo *, 32> MPIType;

  // point-to-point functions
  IdentifierInfo *IdentInfo_MPI_Send = nullptr, *IdentInfo_MPI_Isend = nullptr,
      *IdentInfo_MPI_Ssend = nullptr, *IdentInfo_MPI_Issend = nullptr,
      *IdentInfo_MPI_Bsend = nullptr, *IdentInfo_MPI_Ibsend = nullptr,
      *IdentInfo_MPI_Rsend = nullptr, *IdentInfo_MPI_Irsend = nullptr,
      *IdentInfo_MPI_Recv = nullptr, *IdentInfo_MPI_Irecv = nullptr;

  // collective functions
  IdentifierInfo *IdentInfo_MPI_Scatter = nullptr,
      *IdentInfo_MPI_Iscatter = nullptr, *IdentInfo_MPI_Gather = nullptr,
      *IdentInfo_MPI_Igather = nullptr, *IdentInfo_MPI_Allgather = nullptr,
      *IdentInfo_MPI_Iallgather = nullptr, *IdentInfo_MPI_Bcast = nullptr,
      *IdentInfo_MPI_Ibcast = nullptr, *IdentInfo_MPI_Reduce = nullptr,
      *IdentInfo_MPI_Ireduce = nullptr, *IdentInfo_MPI_Allreduce = nullptr,
      *IdentInfo_MPI_Iallreduce = nullptr, *IdentInfo_MPI_Alltoall = nullptr,
      *IdentInfo_MPI_Ialltoall = nullptr, *IdentInfo_MPI_Barrier = nullptr;

  // additional functions
  IdentifierInfo *IdentInfo_MPI_Comm_rank = nullptr,
      *IdentInfo_MPI_Comm_size = nullptr, *IdentInfo_MPI_Wait = nullptr,
      *IdentInfo_MPI_Waitall = nullptr;
};

} // end of namespace: mpi
} // end of namespace: ento
} // end of namespace: clang

#endif
