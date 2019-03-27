// The encoding used for conditional codes used in BR instructions

#ifndef LLVM_LIB_TARGET_LANAI_LANAICONDCODE_H
#define LLVM_LIB_TARGET_LANAI_LANAICONDCODE_H

#include "llvm/ADT/StringSwitch.h"

namespace llvm {
namespace LPCC {
enum CondCode {
  ICC_T = 0,   //  true
  ICC_F = 1,   //  false
  ICC_HI = 2,  //  high
  ICC_UGT = 2, //  unsigned greater than
  ICC_LS = 3,  //  low or same
  ICC_ULE = 3, //  unsigned less than or equal
  ICC_CC = 4,  //  carry cleared
  ICC_ULT = 4, //  unsigned less than
  ICC_CS = 5,  //  carry set
  ICC_UGE = 5, //  unsigned greater than or equal
  ICC_NE = 6,  //  not equal
  ICC_EQ = 7,  //  equal
  ICC_VC = 8,  //  oVerflow cleared
  ICC_VS = 9,  //  oVerflow set
  ICC_PL = 10, //  plus
  ICC_MI = 11, //  minus
  ICC_GE = 12, //  greater than or equal
  ICC_LT = 13, //  less than
  ICC_GT = 14, //  greater than
  ICC_LE = 15, //  less than or equal
  UNKNOWN
};

inline static StringRef lanaiCondCodeToString(LPCC::CondCode CC) {
  switch (CC) {
  case LPCC::ICC_T:
    return "t"; // true
  case LPCC::ICC_F:
    return "f"; // false
  case LPCC::ICC_NE:
    return "ne"; // not equal
  case LPCC::ICC_EQ:
    return "eq"; // equal
  case LPCC::ICC_VC:
    return "vc"; // oVerflow cleared
  case LPCC::ICC_VS:
    return "vs"; // oVerflow set
  case LPCC::ICC_PL:
    return "pl"; // plus
  case LPCC::ICC_MI:
    return "mi"; // minus
  case LPCC::ICC_GE:
    return "ge"; // greater than or equal
  case LPCC::ICC_LT:
    return "lt"; // less than
  case LPCC::ICC_GT:
    return "gt"; // greater than
  case LPCC::ICC_LE:
    return "le"; // less than or equal
  case LPCC::ICC_UGT:
    return "ugt"; // high | unsigned greater than
  case LPCC::ICC_ULE:
    return "ule"; // low or same | unsigned less or equal
  case LPCC::ICC_ULT:
    return "ult"; // carry cleared | unsigned less than
  case LPCC::ICC_UGE:
    return "uge"; // carry set | unsigned than or equal
  default:
    llvm_unreachable("Invalid cond code");
  }
}

inline static CondCode suffixToLanaiCondCode(StringRef S) {
  return StringSwitch<CondCode>(S)
      .EndsWith("f", LPCC::ICC_F)
      .EndsWith("hi", LPCC::ICC_HI)
      .EndsWith("ugt", LPCC::ICC_UGT)
      .EndsWith("ls", LPCC::ICC_LS)
      .EndsWith("ule", LPCC::ICC_ULE)
      .EndsWith("cc", LPCC::ICC_CC)
      .EndsWith("ult", LPCC::ICC_ULT)
      .EndsWith("cs", LPCC::ICC_CS)
      .EndsWith("uge", LPCC::ICC_UGE)
      .EndsWith("ne", LPCC::ICC_NE)
      .EndsWith("eq", LPCC::ICC_EQ)
      .EndsWith("vc", LPCC::ICC_VC)
      .EndsWith("vs", LPCC::ICC_VS)
      .EndsWith("pl", LPCC::ICC_PL)
      .EndsWith("mi", LPCC::ICC_MI)
      .EndsWith("ge", LPCC::ICC_GE)
      .EndsWith("lt", LPCC::ICC_LT)
      .EndsWith("gt", LPCC::ICC_GT)
      .EndsWith("le", LPCC::ICC_LE)
      .EndsWith("t", LPCC::ICC_T) // Has to be after others with suffix t
      .Default(LPCC::UNKNOWN);
}
} // namespace LPCC
} // namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_LANAICONDCODE_H
