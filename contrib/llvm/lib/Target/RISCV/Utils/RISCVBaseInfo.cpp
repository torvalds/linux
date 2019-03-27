#include "RISCVBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"

namespace llvm {
namespace RISCVSysReg {
#define GET_SysRegsList_IMPL
#include "RISCVGenSystemOperands.inc"
} // namespace RISCVSysReg
} // namespace llvm
