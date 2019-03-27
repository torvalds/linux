//===- CVDebugRecord.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_CVDEBUGRECORD_H
#define LLVM_OBJECT_CVDEBUGRECORD_H

#include "llvm/Support/Endian.h"

namespace llvm {
namespace OMF {
struct Signature {
  enum ID : uint32_t {
    PDB70 = 0x53445352, // RSDS
    PDB20 = 0x3031424e, // NB10
    CV50 = 0x3131424e,  // NB11
    CV41 = 0x3930424e,  // NB09
  };

  support::ulittle32_t CVSignature;
  support::ulittle32_t Offset;
};
}

namespace codeview {
struct PDB70DebugInfo {
  support::ulittle32_t CVSignature;
  uint8_t Signature[16];
  support::ulittle32_t Age;
  // char PDBFileName[];
};

struct PDB20DebugInfo {
  support::ulittle32_t CVSignature;
  support::ulittle32_t Offset;
  support::ulittle32_t Signature;
  support::ulittle32_t Age;
  // char PDBFileName[];
};

union DebugInfo {
  struct OMF::Signature Signature;
  struct PDB20DebugInfo PDB20;
  struct PDB70DebugInfo PDB70;
};
}
}

#endif

