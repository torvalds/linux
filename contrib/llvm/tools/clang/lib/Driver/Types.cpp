//===--- Types.cpp - Driver input & temporary type information ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Types.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include <cassert>
#include <string.h>

using namespace clang::driver;
using namespace clang::driver::types;

struct TypeInfo {
  const char *Name;
  const char *Flags;
  const char *TempSuffix;
  ID PreprocessedType;
};

static const TypeInfo TypeInfos[] = {
#define TYPE(NAME, ID, PP_TYPE, TEMP_SUFFIX, FLAGS) \
  { NAME, FLAGS, TEMP_SUFFIX, TY_##PP_TYPE, },
#include "clang/Driver/Types.def"
#undef TYPE
};
static const unsigned numTypes = llvm::array_lengthof(TypeInfos);

static const TypeInfo &getInfo(unsigned id) {
  assert(id > 0 && id - 1 < numTypes && "Invalid Type ID.");
  return TypeInfos[id - 1];
}

const char *types::getTypeName(ID Id) {
  return getInfo(Id).Name;
}

types::ID types::getPreprocessedType(ID Id) {
  return getInfo(Id).PreprocessedType;
}

types::ID types::getPrecompiledType(ID Id) {
  if (strchr(getInfo(Id).Flags, 'm'))
    return TY_ModuleFile;
  if (onlyPrecompileType(Id))
    return TY_PCH;
  return TY_INVALID;
}

const char *types::getTypeTempSuffix(ID Id, bool CLMode) {
  if (CLMode) {
    switch (Id) {
    case TY_Object:
    case TY_LTO_BC:
      return "obj";
    case TY_Image:
      return "exe";
    case TY_PP_Asm:
      return "asm";
    default:
      break;
    }
  }
  return getInfo(Id).TempSuffix;
}

bool types::onlyAssembleType(ID Id) {
  return strchr(getInfo(Id).Flags, 'a');
}

bool types::onlyPrecompileType(ID Id) {
  return strchr(getInfo(Id).Flags, 'p');
}

bool types::canTypeBeUserSpecified(ID Id) {
  return strchr(getInfo(Id).Flags, 'u');
}

bool types::appendSuffixForType(ID Id) {
  return strchr(getInfo(Id).Flags, 'A');
}

bool types::canLipoType(ID Id) {
  return (Id == TY_Nothing ||
          Id == TY_Image ||
          Id == TY_Object ||
          Id == TY_LTO_BC);
}

bool types::isAcceptedByClang(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_Asm:
  case TY_C: case TY_PP_C:
  case TY_CL:
  case TY_CUDA: case TY_PP_CUDA:
  case TY_CUDA_DEVICE:
  case TY_HIP:
  case TY_PP_HIP:
  case TY_HIP_DEVICE:
  case TY_ObjC: case TY_PP_ObjC: case TY_PP_ObjC_Alias:
  case TY_CXX: case TY_PP_CXX:
  case TY_ObjCXX: case TY_PP_ObjCXX: case TY_PP_ObjCXX_Alias:
  case TY_CHeader: case TY_PP_CHeader:
  case TY_CLHeader:
  case TY_ObjCHeader: case TY_PP_ObjCHeader:
  case TY_CXXHeader: case TY_PP_CXXHeader:
  case TY_ObjCXXHeader: case TY_PP_ObjCXXHeader:
  case TY_CXXModule: case TY_PP_CXXModule:
  case TY_AST: case TY_ModuleFile:
  case TY_LLVM_IR: case TY_LLVM_BC:
    return true;
  }
}

bool types::isObjC(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_ObjC: case TY_PP_ObjC: case TY_PP_ObjC_Alias:
  case TY_ObjCXX: case TY_PP_ObjCXX:
  case TY_ObjCHeader: case TY_PP_ObjCHeader:
  case TY_ObjCXXHeader: case TY_PP_ObjCXXHeader: case TY_PP_ObjCXX_Alias:
    return true;
  }
}

bool types::isCXX(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_CXX: case TY_PP_CXX:
  case TY_ObjCXX: case TY_PP_ObjCXX: case TY_PP_ObjCXX_Alias:
  case TY_CXXHeader: case TY_PP_CXXHeader:
  case TY_ObjCXXHeader: case TY_PP_ObjCXXHeader:
  case TY_CXXModule: case TY_PP_CXXModule:
  case TY_CUDA: case TY_PP_CUDA: case TY_CUDA_DEVICE:
  case TY_HIP:
  case TY_PP_HIP:
  case TY_HIP_DEVICE:
    return true;
  }
}

bool types::isLLVMIR(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_LLVM_IR:
  case TY_LLVM_BC:
  case TY_LTO_IR:
  case TY_LTO_BC:
    return true;
  }
}

bool types::isCuda(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_CUDA:
  case TY_PP_CUDA:
  case TY_CUDA_DEVICE:
    return true;
  }
}

bool types::isHIP(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_HIP:
  case TY_PP_HIP:
  case TY_HIP_DEVICE:
    return true;
  }
}

bool types::isSrcFile(ID Id) {
  return Id != TY_Object && getPreprocessedType(Id) != TY_INVALID;
}

types::ID types::lookupTypeForExtension(llvm::StringRef Ext) {
  return llvm::StringSwitch<types::ID>(Ext)
           .Case("c", TY_C)
           .Case("C", TY_CXX)
           .Case("F", TY_Fortran)
           .Case("f", TY_PP_Fortran)
           .Case("h", TY_CHeader)
           .Case("H", TY_CXXHeader)
           .Case("i", TY_PP_C)
           .Case("m", TY_ObjC)
           .Case("M", TY_ObjCXX)
           .Case("o", TY_Object)
           .Case("S", TY_Asm)
           .Case("s", TY_PP_Asm)
           .Case("bc", TY_LLVM_BC)
           .Case("cc", TY_CXX)
           .Case("CC", TY_CXX)
           .Case("cl", TY_CL)
           .Case("cp", TY_CXX)
           .Case("cu", TY_CUDA)
           .Case("hh", TY_CXXHeader)
           .Case("ii", TY_PP_CXX)
           .Case("ll", TY_LLVM_IR)
           .Case("mi", TY_PP_ObjC)
           .Case("mm", TY_ObjCXX)
           .Case("rs", TY_RenderScript)
           .Case("adb", TY_Ada)
           .Case("ads", TY_Ada)
           .Case("asm", TY_PP_Asm)
           .Case("ast", TY_AST)
           .Case("ccm", TY_CXXModule)
           .Case("cpp", TY_CXX)
           .Case("CPP", TY_CXX)
           .Case("c++", TY_CXX)
           .Case("C++", TY_CXX)
           .Case("cui", TY_PP_CUDA)
           .Case("cxx", TY_CXX)
           .Case("CXX", TY_CXX)
           .Case("F90", TY_Fortran)
           .Case("f90", TY_PP_Fortran)
           .Case("F95", TY_Fortran)
           .Case("f95", TY_PP_Fortran)
           .Case("for", TY_PP_Fortran)
           .Case("FOR", TY_PP_Fortran)
           .Case("fpp", TY_Fortran)
           .Case("FPP", TY_Fortran)
           .Case("gch", TY_PCH)
           .Case("hip", TY_HIP)
           .Case("hpp", TY_CXXHeader)
           .Case("iim", TY_PP_CXXModule)
           .Case("lib", TY_Object)
           .Case("mii", TY_PP_ObjCXX)
           .Case("obj", TY_Object)
           .Case("pch", TY_PCH)
           .Case("pcm", TY_ModuleFile)
           .Case("c++m", TY_CXXModule)
           .Case("cppm", TY_CXXModule)
           .Case("cxxm", TY_CXXModule)
           .Default(TY_INVALID);
}

types::ID types::lookupTypeForTypeSpecifier(const char *Name) {
  for (unsigned i=0; i<numTypes; ++i) {
    types::ID Id = (types::ID) (i + 1);
    if (canTypeBeUserSpecified(Id) &&
        strcmp(Name, getInfo(Id).Name) == 0)
      return Id;
  }

  return TY_INVALID;
}

// FIXME: Why don't we just put this list in the defs file, eh.
void types::getCompilationPhases(ID Id, llvm::SmallVectorImpl<phases::ID> &P) {
  if (Id != TY_Object) {
    if (getPreprocessedType(Id) != TY_INVALID) {
      P.push_back(phases::Preprocess);
    }

    if (getPrecompiledType(Id) != TY_INVALID) {
      P.push_back(phases::Precompile);
    }

    if (!onlyPrecompileType(Id)) {
      if (!onlyAssembleType(Id)) {
        P.push_back(phases::Compile);
        P.push_back(phases::Backend);
      }
      P.push_back(phases::Assemble);
    }
  }

  if (!onlyPrecompileType(Id)) {
    P.push_back(phases::Link);
  }
  assert(0 < P.size() && "Not enough phases in list");
  assert(P.size() <= phases::MaxNumberOfPhases && "Too many phases in list");
}

ID types::lookupCXXTypeForCType(ID Id) {
  switch (Id) {
  default:
    return Id;

  case types::TY_C:
    return types::TY_CXX;
  case types::TY_PP_C:
    return types::TY_PP_CXX;
  case types::TY_CHeader:
    return types::TY_CXXHeader;
  case types::TY_PP_CHeader:
    return types::TY_PP_CXXHeader;
  }
}

ID types::lookupHeaderTypeForSourceType(ID Id) {
  switch (Id) {
  default:
    return Id;

  // FIXME: Handle preprocessed input types.
  case types::TY_C:
    return types::TY_CHeader;
  case types::TY_CXX:
  case types::TY_CXXModule:
    return types::TY_CXXHeader;
  case types::TY_ObjC:
    return types::TY_ObjCHeader;
  case types::TY_ObjCXX:
    return types::TY_ObjCXXHeader;
  case types::TY_CL:
    return types::TY_CLHeader;
  }
}
