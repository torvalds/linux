#ifndef LLVM_TRANSFORMS_IPO_SYNTHETIC_COUNTS_PROPAGATION_H
#define LLVM_TRANSFORMS_IPO_SYNTHETIC_COUNTS_PROPAGATION_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/ScaledNumber.h"

namespace llvm {
class Function;
class Module;

class SyntheticCountsPropagation
    : public PassInfoMixin<SyntheticCountsPropagation> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};
} // namespace llvm
#endif
