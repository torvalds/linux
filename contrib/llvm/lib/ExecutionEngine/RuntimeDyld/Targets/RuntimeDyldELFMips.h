//===-- RuntimeDyldELFMips.h ---- ELF/Mips specific code. -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDELFMIPS_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDELFMIPS_H

#include "../RuntimeDyldELF.h"
#include <string>

#define DEBUG_TYPE "dyld"

namespace llvm {

class RuntimeDyldELFMips : public RuntimeDyldELF {
public:

  typedef uint64_t TargetPtrT;

  RuntimeDyldELFMips(RuntimeDyld::MemoryManager &MM,
                     JITSymbolResolver &Resolver)
      : RuntimeDyldELF(MM, Resolver) {}

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override;

protected:
  void resolveMIPSO32Relocation(const SectionEntry &Section, uint64_t Offset,
                                uint32_t Value, uint32_t Type, int32_t Addend);
  void resolveMIPSN32Relocation(const SectionEntry &Section, uint64_t Offset,
                                uint64_t Value, uint32_t Type, int64_t Addend,
                                uint64_t SymOffset, SID SectionID);
  void resolveMIPSN64Relocation(const SectionEntry &Section, uint64_t Offset,
                                uint64_t Value, uint32_t Type, int64_t Addend,
                                uint64_t SymOffset, SID SectionID);

private:
  /// A object file specific relocation resolver
  /// \param RE The relocation to be resolved
  /// \param Value Target symbol address to apply the relocation action
  uint64_t evaluateRelocation(const RelocationEntry &RE, uint64_t Value,
                              uint64_t Addend);

  /// A object file specific relocation resolver
  /// \param RE The relocation to be resolved
  /// \param Value Target symbol address to apply the relocation action
  void applyRelocation(const RelocationEntry &RE, uint64_t Value);

  int64_t evaluateMIPS32Relocation(const SectionEntry &Section, uint64_t Offset,
                                   uint64_t Value, uint32_t Type);
  int64_t evaluateMIPS64Relocation(const SectionEntry &Section,
                                   uint64_t Offset, uint64_t Value,
                                   uint32_t Type,  int64_t Addend,
                                   uint64_t SymOffset, SID SectionID);

  void applyMIPSRelocation(uint8_t *TargetPtr, int64_t CalculatedValue,
                           uint32_t Type);

};
}

#undef DEBUG_TYPE

#endif
