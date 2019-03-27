#include "clang/Basic/Cuda.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"

namespace clang {

const char *CudaVersionToString(CudaVersion V) {
  switch (V) {
  case CudaVersion::UNKNOWN:
    return "unknown";
  case CudaVersion::CUDA_70:
    return "7.0";
  case CudaVersion::CUDA_75:
    return "7.5";
  case CudaVersion::CUDA_80:
    return "8.0";
  case CudaVersion::CUDA_90:
    return "9.0";
  case CudaVersion::CUDA_91:
    return "9.1";
  case CudaVersion::CUDA_92:
    return "9.2";
  case CudaVersion::CUDA_100:
    return "10.0";
  }
  llvm_unreachable("invalid enum");
}

const char *CudaArchToString(CudaArch A) {
  switch (A) {
  case CudaArch::LAST:
    break;
  case CudaArch::UNKNOWN:
    return "unknown";
  case CudaArch::SM_20:
    return "sm_20";
  case CudaArch::SM_21:
    return "sm_21";
  case CudaArch::SM_30:
    return "sm_30";
  case CudaArch::SM_32:
    return "sm_32";
  case CudaArch::SM_35:
    return "sm_35";
  case CudaArch::SM_37:
    return "sm_37";
  case CudaArch::SM_50:
    return "sm_50";
  case CudaArch::SM_52:
    return "sm_52";
  case CudaArch::SM_53:
    return "sm_53";
  case CudaArch::SM_60:
    return "sm_60";
  case CudaArch::SM_61:
    return "sm_61";
  case CudaArch::SM_62:
    return "sm_62";
  case CudaArch::SM_70:
    return "sm_70";
  case CudaArch::SM_72:
    return "sm_72";
  case CudaArch::SM_75:
    return "sm_75";
  case CudaArch::GFX600: // tahiti
    return "gfx600";
  case CudaArch::GFX601: // pitcairn, verde, oland,hainan
    return "gfx601";
  case CudaArch::GFX700: // kaveri
    return "gfx700";
  case CudaArch::GFX701: // hawaii
    return "gfx701";
  case CudaArch::GFX702: // 290,290x,R390,R390x
    return "gfx702";
  case CudaArch::GFX703: // kabini mullins
    return "gfx703";
  case CudaArch::GFX704: // bonaire
    return "gfx704";
  case CudaArch::GFX801: // carrizo
    return "gfx801";
  case CudaArch::GFX802: // tonga,iceland
    return "gfx802";
  case CudaArch::GFX803: // fiji,polaris10
    return "gfx803";
  case CudaArch::GFX810: // stoney
    return "gfx810";
  case CudaArch::GFX900: // vega, instinct
    return "gfx900";
  case CudaArch::GFX902: // TBA
    return "gfx902";
  case CudaArch::GFX904: // TBA
    return "gfx904";
  case CudaArch::GFX906: // TBA
    return "gfx906";
  case CudaArch::GFX909: // TBA
    return "gfx909";
  }
  llvm_unreachable("invalid enum");
}

CudaArch StringToCudaArch(llvm::StringRef S) {
  return llvm::StringSwitch<CudaArch>(S)
      .Case("sm_20", CudaArch::SM_20)
      .Case("sm_21", CudaArch::SM_21)
      .Case("sm_30", CudaArch::SM_30)
      .Case("sm_32", CudaArch::SM_32)
      .Case("sm_35", CudaArch::SM_35)
      .Case("sm_37", CudaArch::SM_37)
      .Case("sm_50", CudaArch::SM_50)
      .Case("sm_52", CudaArch::SM_52)
      .Case("sm_53", CudaArch::SM_53)
      .Case("sm_60", CudaArch::SM_60)
      .Case("sm_61", CudaArch::SM_61)
      .Case("sm_62", CudaArch::SM_62)
      .Case("sm_70", CudaArch::SM_70)
      .Case("sm_72", CudaArch::SM_72)
      .Case("sm_75", CudaArch::SM_75)
      .Case("gfx600", CudaArch::GFX600)
      .Case("gfx601", CudaArch::GFX601)
      .Case("gfx700", CudaArch::GFX700)
      .Case("gfx701", CudaArch::GFX701)
      .Case("gfx702", CudaArch::GFX702)
      .Case("gfx703", CudaArch::GFX703)
      .Case("gfx704", CudaArch::GFX704)
      .Case("gfx801", CudaArch::GFX801)
      .Case("gfx802", CudaArch::GFX802)
      .Case("gfx803", CudaArch::GFX803)
      .Case("gfx810", CudaArch::GFX810)
      .Case("gfx900", CudaArch::GFX900)
      .Case("gfx902", CudaArch::GFX902)
      .Case("gfx904", CudaArch::GFX904)
      .Case("gfx906", CudaArch::GFX906)
      .Case("gfx909", CudaArch::GFX909)
      .Default(CudaArch::UNKNOWN);
}

const char *CudaVirtualArchToString(CudaVirtualArch A) {
  switch (A) {
  case CudaVirtualArch::UNKNOWN:
    return "unknown";
  case CudaVirtualArch::COMPUTE_20:
    return "compute_20";
  case CudaVirtualArch::COMPUTE_30:
    return "compute_30";
  case CudaVirtualArch::COMPUTE_32:
    return "compute_32";
  case CudaVirtualArch::COMPUTE_35:
    return "compute_35";
  case CudaVirtualArch::COMPUTE_37:
    return "compute_37";
  case CudaVirtualArch::COMPUTE_50:
    return "compute_50";
  case CudaVirtualArch::COMPUTE_52:
    return "compute_52";
  case CudaVirtualArch::COMPUTE_53:
    return "compute_53";
  case CudaVirtualArch::COMPUTE_60:
    return "compute_60";
  case CudaVirtualArch::COMPUTE_61:
    return "compute_61";
  case CudaVirtualArch::COMPUTE_62:
    return "compute_62";
  case CudaVirtualArch::COMPUTE_70:
    return "compute_70";
  case CudaVirtualArch::COMPUTE_72:
    return "compute_72";
  case CudaVirtualArch::COMPUTE_75:
    return "compute_75";
  case CudaVirtualArch::COMPUTE_AMDGCN:
    return "compute_amdgcn";
  }
  llvm_unreachable("invalid enum");
}

CudaVirtualArch StringToCudaVirtualArch(llvm::StringRef S) {
  return llvm::StringSwitch<CudaVirtualArch>(S)
      .Case("compute_20", CudaVirtualArch::COMPUTE_20)
      .Case("compute_30", CudaVirtualArch::COMPUTE_30)
      .Case("compute_32", CudaVirtualArch::COMPUTE_32)
      .Case("compute_35", CudaVirtualArch::COMPUTE_35)
      .Case("compute_37", CudaVirtualArch::COMPUTE_37)
      .Case("compute_50", CudaVirtualArch::COMPUTE_50)
      .Case("compute_52", CudaVirtualArch::COMPUTE_52)
      .Case("compute_53", CudaVirtualArch::COMPUTE_53)
      .Case("compute_60", CudaVirtualArch::COMPUTE_60)
      .Case("compute_61", CudaVirtualArch::COMPUTE_61)
      .Case("compute_62", CudaVirtualArch::COMPUTE_62)
      .Case("compute_70", CudaVirtualArch::COMPUTE_70)
      .Case("compute_72", CudaVirtualArch::COMPUTE_72)
      .Case("compute_75", CudaVirtualArch::COMPUTE_75)
      .Case("compute_amdgcn", CudaVirtualArch::COMPUTE_AMDGCN)
      .Default(CudaVirtualArch::UNKNOWN);
}

CudaVirtualArch VirtualArchForCudaArch(CudaArch A) {
  switch (A) {
  case CudaArch::LAST:
    break;
  case CudaArch::UNKNOWN:
    return CudaVirtualArch::UNKNOWN;
  case CudaArch::SM_20:
  case CudaArch::SM_21:
    return CudaVirtualArch::COMPUTE_20;
  case CudaArch::SM_30:
    return CudaVirtualArch::COMPUTE_30;
  case CudaArch::SM_32:
    return CudaVirtualArch::COMPUTE_32;
  case CudaArch::SM_35:
    return CudaVirtualArch::COMPUTE_35;
  case CudaArch::SM_37:
    return CudaVirtualArch::COMPUTE_37;
  case CudaArch::SM_50:
    return CudaVirtualArch::COMPUTE_50;
  case CudaArch::SM_52:
    return CudaVirtualArch::COMPUTE_52;
  case CudaArch::SM_53:
    return CudaVirtualArch::COMPUTE_53;
  case CudaArch::SM_60:
    return CudaVirtualArch::COMPUTE_60;
  case CudaArch::SM_61:
    return CudaVirtualArch::COMPUTE_61;
  case CudaArch::SM_62:
    return CudaVirtualArch::COMPUTE_62;
  case CudaArch::SM_70:
    return CudaVirtualArch::COMPUTE_70;
  case CudaArch::SM_72:
    return CudaVirtualArch::COMPUTE_72;
  case CudaArch::SM_75:
    return CudaVirtualArch::COMPUTE_75;
  case CudaArch::GFX600:
  case CudaArch::GFX601:
  case CudaArch::GFX700:
  case CudaArch::GFX701:
  case CudaArch::GFX702:
  case CudaArch::GFX703:
  case CudaArch::GFX704:
  case CudaArch::GFX801:
  case CudaArch::GFX802:
  case CudaArch::GFX803:
  case CudaArch::GFX810:
  case CudaArch::GFX900:
  case CudaArch::GFX902:
  case CudaArch::GFX904:
  case CudaArch::GFX906:
  case CudaArch::GFX909:
    return CudaVirtualArch::COMPUTE_AMDGCN;
  }
  llvm_unreachable("invalid enum");
}

CudaVersion MinVersionForCudaArch(CudaArch A) {
  switch (A) {
  case CudaArch::LAST:
    break;
  case CudaArch::UNKNOWN:
    return CudaVersion::UNKNOWN;
  case CudaArch::SM_20:
  case CudaArch::SM_21:
  case CudaArch::SM_30:
  case CudaArch::SM_32:
  case CudaArch::SM_35:
  case CudaArch::SM_37:
  case CudaArch::SM_50:
  case CudaArch::SM_52:
  case CudaArch::SM_53:
    return CudaVersion::CUDA_70;
  case CudaArch::SM_60:
  case CudaArch::SM_61:
  case CudaArch::SM_62:
    return CudaVersion::CUDA_80;
  case CudaArch::SM_70:
    return CudaVersion::CUDA_90;
  case CudaArch::SM_72:
    return CudaVersion::CUDA_91;
  case CudaArch::SM_75:
    return CudaVersion::CUDA_100;
  case CudaArch::GFX600:
  case CudaArch::GFX601:
  case CudaArch::GFX700:
  case CudaArch::GFX701:
  case CudaArch::GFX702:
  case CudaArch::GFX703:
  case CudaArch::GFX704:
  case CudaArch::GFX801:
  case CudaArch::GFX802:
  case CudaArch::GFX803:
  case CudaArch::GFX810:
  case CudaArch::GFX900:
  case CudaArch::GFX902:
  case CudaArch::GFX904:
  case CudaArch::GFX906:
  case CudaArch::GFX909:
    return CudaVersion::CUDA_70;
  }
  llvm_unreachable("invalid enum");
}

CudaVersion MaxVersionForCudaArch(CudaArch A) {
  switch (A) {
  case CudaArch::UNKNOWN:
    return CudaVersion::UNKNOWN;
  case CudaArch::SM_20:
  case CudaArch::SM_21:
  case CudaArch::GFX600:
  case CudaArch::GFX601:
  case CudaArch::GFX700:
  case CudaArch::GFX701:
  case CudaArch::GFX702:
  case CudaArch::GFX703:
  case CudaArch::GFX704:
  case CudaArch::GFX801:
  case CudaArch::GFX802:
  case CudaArch::GFX803:
  case CudaArch::GFX810:
  case CudaArch::GFX900:
  case CudaArch::GFX902:
    return CudaVersion::CUDA_80;
  default:
    return CudaVersion::LATEST;
  }
}

} // namespace clang
