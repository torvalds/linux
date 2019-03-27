//===- RelocVisitor.h - Visitor for object file relocations -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides a wrapper around all the different types of relocations
// in different file formats, such that a client can handle them in a unified
// manner by only implementing a minimal number of functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_RELOCVISITOR_H
#define LLVM_OBJECT_RELOCVISITOR_H

#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <system_error>

namespace llvm {
namespace object {

/// Base class for object file relocation visitors.
class RelocVisitor {
public:
  explicit RelocVisitor(const ObjectFile &Obj) : ObjToVisit(Obj) {}

  // TODO: Should handle multiple applied relocations via either passing in the
  // previously computed value or just count paired relocations as a single
  // visit.
  uint64_t visit(uint32_t Rel, RelocationRef R, uint64_t Value = 0) {
    if (isa<ELFObjectFileBase>(ObjToVisit))
      return visitELF(Rel, R, Value);
    if (isa<COFFObjectFile>(ObjToVisit))
      return visitCOFF(Rel, R, Value);
    if (isa<MachOObjectFile>(ObjToVisit))
      return visitMachO(Rel, R, Value);
    if (isa<WasmObjectFile>(ObjToVisit))
      return visitWasm(Rel, R, Value);

    HasError = true;
    return 0;
  }

  bool error() { return HasError; }

private:
  const ObjectFile &ObjToVisit;
  bool HasError = false;

  uint64_t visitELF(uint32_t Rel, RelocationRef R, uint64_t Value) {
    if (ObjToVisit.getBytesInAddress() == 8) { // 64-bit object file
      switch (ObjToVisit.getArch()) {
      case Triple::x86_64:
        return visitX86_64(Rel, R, Value);
      case Triple::aarch64:
      case Triple::aarch64_be:
        return visitAarch64(Rel, R, Value);
      case Triple::bpfel:
      case Triple::bpfeb:
        return visitBpf(Rel, R, Value);
      case Triple::mips64el:
      case Triple::mips64:
        return visitMips64(Rel, R, Value);
      case Triple::ppc64le:
      case Triple::ppc64:
        return visitPPC64(Rel, R, Value);
      case Triple::systemz:
        return visitSystemz(Rel, R, Value);
      case Triple::sparcv9:
        return visitSparc64(Rel, R, Value);
      case Triple::amdgcn:
        return visitAmdgpu(Rel, R, Value);
      default:
        HasError = true;
        return 0;
      }
    }

    // 32-bit object file
    assert(ObjToVisit.getBytesInAddress() == 4 &&
           "Invalid word size in object file");

    switch (ObjToVisit.getArch()) {
    case Triple::x86:
      return visitX86(Rel, R, Value);
    case Triple::ppc:
      return visitPPC32(Rel, R, Value);
    case Triple::arm:
    case Triple::armeb:
      return visitARM(Rel, R, Value);
    case Triple::lanai:
      return visitLanai(Rel, R, Value);
    case Triple::mipsel:
    case Triple::mips:
      return visitMips32(Rel, R, Value);
    case Triple::sparc:
      return visitSparc32(Rel, R, Value);
    case Triple::hexagon:
      return visitHexagon(Rel, R, Value);
    default:
      HasError = true;
      return 0;
    }
  }

  int64_t getELFAddend(RelocationRef R) {
    Expected<int64_t> AddendOrErr = ELFRelocationRef(R).getAddend();
    handleAllErrors(AddendOrErr.takeError(), [](const ErrorInfoBase &EI) {
      report_fatal_error(EI.message());
    });
    return *AddendOrErr;
  }

  uint64_t visitX86_64(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_X86_64_NONE:
      return 0;
    case ELF::R_X86_64_64:
    case ELF::R_X86_64_DTPOFF32:
    case ELF::R_X86_64_DTPOFF64:
      return Value + getELFAddend(R);
    case ELF::R_X86_64_PC32:
      return Value + getELFAddend(R) - R.getOffset();
    case ELF::R_X86_64_32:
    case ELF::R_X86_64_32S:
      return (Value + getELFAddend(R)) & 0xFFFFFFFF;
    }
    HasError = true;
    return 0;
  }

  uint64_t visitAarch64(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_AARCH64_ABS32: {
      int64_t Res = Value + getELFAddend(R);
      if (Res < INT32_MIN || Res > UINT32_MAX)
        HasError = true;
      return static_cast<uint32_t>(Res);
    }
    case ELF::R_AARCH64_ABS64:
      return Value + getELFAddend(R);
    }
    HasError = true;
    return 0;
  }

  uint64_t visitBpf(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_BPF_64_32:
      return Value & 0xFFFFFFFF;
    case ELF::R_BPF_64_64:
      return Value;
    }
    HasError = true;
    return 0;
  }

  uint64_t visitMips64(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_MIPS_32:
      return (Value + getELFAddend(R)) & 0xFFFFFFFF;
    case ELF::R_MIPS_64:
      return Value + getELFAddend(R);
    case ELF::R_MIPS_TLS_DTPREL64:
      return Value + getELFAddend(R) - 0x8000;
    }
    HasError = true;
    return 0;
  }

  uint64_t visitPPC64(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_PPC64_ADDR32:
      return (Value + getELFAddend(R)) & 0xFFFFFFFF;
    case ELF::R_PPC64_ADDR64:
      return Value + getELFAddend(R);
    }
    HasError = true;
    return 0;
  }

  uint64_t visitSystemz(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_390_32: {
      int64_t Res = Value + getELFAddend(R);
      if (Res < INT32_MIN || Res > UINT32_MAX)
        HasError = true;
      return static_cast<uint32_t>(Res);
    }
    case ELF::R_390_64:
      return Value + getELFAddend(R);
    }
    HasError = true;
    return 0;
  }

  uint64_t visitSparc64(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_SPARC_32:
    case ELF::R_SPARC_64:
    case ELF::R_SPARC_UA32:
    case ELF::R_SPARC_UA64:
      return Value + getELFAddend(R);
    }
    HasError = true;
    return 0;
  }

  uint64_t visitAmdgpu(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_AMDGPU_ABS32:
    case ELF::R_AMDGPU_ABS64:
      return Value + getELFAddend(R);
    }
    HasError = true;
    return 0;
  }

  uint64_t visitX86(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (Rel) {
    case ELF::R_386_NONE:
      return 0;
    case ELF::R_386_32:
      return Value;
    case ELF::R_386_PC32:
      return Value - R.getOffset();
    }
    HasError = true;
    return 0;
  }

  uint64_t visitPPC32(uint32_t Rel, RelocationRef R, uint64_t Value) {
    if (Rel == ELF::R_PPC_ADDR32)
      return (Value + getELFAddend(R)) & 0xFFFFFFFF;
    HasError = true;
    return 0;
  }

  uint64_t visitARM(uint32_t Rel, RelocationRef R, uint64_t Value) {
    if (Rel == ELF::R_ARM_ABS32) {
      if ((int64_t)Value < INT32_MIN || (int64_t)Value > UINT32_MAX)
        HasError = true;
      return static_cast<uint32_t>(Value);
    }
    HasError = true;
    return 0;
  }

  uint64_t visitLanai(uint32_t Rel, RelocationRef R, uint64_t Value) {
    if (Rel == ELF::R_LANAI_32)
      return (Value + getELFAddend(R)) & 0xFFFFFFFF;
    HasError = true;
    return 0;
  }

  uint64_t visitMips32(uint32_t Rel, RelocationRef R, uint64_t Value) {
    // FIXME: Take in account implicit addends to get correct results.
    if (Rel == ELF::R_MIPS_32)
      return Value & 0xFFFFFFFF;
    if (Rel == ELF::R_MIPS_TLS_DTPREL32)
      return Value & 0xFFFFFFFF;
    HasError = true;
    return 0;
  }

  uint64_t visitSparc32(uint32_t Rel, RelocationRef R, uint64_t Value) {
    if (Rel == ELF::R_SPARC_32 || Rel == ELF::R_SPARC_UA32)
      return Value + getELFAddend(R);
    HasError = true;
    return 0;
  }

  uint64_t visitHexagon(uint32_t Rel, RelocationRef R, uint64_t Value) {
    if (Rel == ELF::R_HEX_32)
      return Value + getELFAddend(R);
    HasError = true;
    return 0;
  }

  uint64_t visitCOFF(uint32_t Rel, RelocationRef R, uint64_t Value) {
    switch (ObjToVisit.getArch()) {
    case Triple::x86:
      switch (Rel) {
      case COFF::IMAGE_REL_I386_SECREL:
      case COFF::IMAGE_REL_I386_DIR32:
        return static_cast<uint32_t>(Value);
      }
      break;
    case Triple::x86_64:
      switch (Rel) {
      case COFF::IMAGE_REL_AMD64_SECREL:
        return static_cast<uint32_t>(Value);
      case COFF::IMAGE_REL_AMD64_ADDR64:
        return Value;
      }
      break;
    default:
      break;
    }
    HasError = true;
    return 0;
  }

  uint64_t visitMachO(uint32_t Rel, RelocationRef R, uint64_t Value) {
    if (ObjToVisit.getArch() == Triple::x86_64 &&
        Rel == MachO::X86_64_RELOC_UNSIGNED)
      return Value;
    HasError = true;
    return 0;
  }

  uint64_t visitWasm(uint32_t Rel, RelocationRef R, uint64_t Value) {
    if (ObjToVisit.getArch() == Triple::wasm32) {
      switch (Rel) {
      case wasm::R_WEBASSEMBLY_FUNCTION_INDEX_LEB:
      case wasm::R_WEBASSEMBLY_TABLE_INDEX_SLEB:
      case wasm::R_WEBASSEMBLY_TABLE_INDEX_I32:
      case wasm::R_WEBASSEMBLY_MEMORY_ADDR_LEB:
      case wasm::R_WEBASSEMBLY_MEMORY_ADDR_SLEB:
      case wasm::R_WEBASSEMBLY_MEMORY_ADDR_I32:
      case wasm::R_WEBASSEMBLY_TYPE_INDEX_LEB:
      case wasm::R_WEBASSEMBLY_GLOBAL_INDEX_LEB:
      case wasm::R_WEBASSEMBLY_FUNCTION_OFFSET_I32:
      case wasm::R_WEBASSEMBLY_SECTION_OFFSET_I32:
      case wasm::R_WEBASSEMBLY_EVENT_INDEX_LEB:
        // For wasm section, its offset at 0 -- ignoring Value
        return 0;
      }
    }
    HasError = true;
    return 0;
  }
};

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_RELOCVISITOR_H
