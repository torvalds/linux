//===-- AArch64BaseInfo.cpp - AArch64 Base encoding information------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides basic encoding and assembly information for AArch64.
//
//===----------------------------------------------------------------------===//
#include "AArch64BaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Regex.h"

using namespace llvm;

namespace llvm {
  namespace AArch64AT {
#define GET_AT_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}


namespace llvm {
  namespace AArch64DB {
#define GET_DB_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64DC {
#define GET_DC_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64IC {
#define GET_IC_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64ISB {
#define GET_ISB_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64TSB {
#define GET_TSB_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64PRCTX {
#define GET_PRCTX_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64PRFM {
#define GET_PRFM_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64SVEPRFM {
#define GET_SVEPRFM_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64SVEPredPattern {
#define GET_SVEPREDPAT_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64ExactFPImm {
#define GET_EXACTFPIMM_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64PState {
#define GET_PSTATE_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64PSBHint {
#define GET_PSB_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64BTIHint {
#define GET_BTI_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

namespace llvm {
  namespace AArch64SysReg {
#define GET_SYSREG_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}

uint32_t AArch64SysReg::parseGenericRegister(StringRef Name) {
  // Try to parse an S<op0>_<op1>_<Cn>_<Cm>_<op2> register name
  Regex GenericRegPattern("^S([0-3])_([0-7])_C([0-9]|1[0-5])_C([0-9]|1[0-5])_([0-7])$");

  std::string UpperName = Name.upper();
  SmallVector<StringRef, 5> Ops;
  if (!GenericRegPattern.match(UpperName, &Ops))
    return -1;

  uint32_t Op0 = 0, Op1 = 0, CRn = 0, CRm = 0, Op2 = 0;
  uint32_t Bits;
  Ops[1].getAsInteger(10, Op0);
  Ops[2].getAsInteger(10, Op1);
  Ops[3].getAsInteger(10, CRn);
  Ops[4].getAsInteger(10, CRm);
  Ops[5].getAsInteger(10, Op2);
  Bits = (Op0 << 14) | (Op1 << 11) | (CRn << 7) | (CRm << 3) | Op2;

  return Bits;
}

std::string AArch64SysReg::genericRegisterString(uint32_t Bits) {
  assert(Bits < 0x10000);
  uint32_t Op0 = (Bits >> 14) & 0x3;
  uint32_t Op1 = (Bits >> 11) & 0x7;
  uint32_t CRn = (Bits >> 7) & 0xf;
  uint32_t CRm = (Bits >> 3) & 0xf;
  uint32_t Op2 = Bits & 0x7;

  return "S" + utostr(Op0) + "_" + utostr(Op1) + "_C" + utostr(CRn) + "_C" +
         utostr(CRm) + "_" + utostr(Op2);
}

namespace llvm {
  namespace AArch64TLBI {
#define GET_TLBI_IMPL
#include "AArch64GenSystemOperands.inc"
  }
}
