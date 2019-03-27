//===--- CodeGenTypeCache.h - Commonly used LLVM types and info -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This structure provides a set of common types useful during IR emission.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CODEGENTYPECACHE_H
#define LLVM_CLANG_LIB_CODEGEN_CODEGENTYPECACHE_H

#include "clang/AST/CharUnits.h"
#include "clang/Basic/AddressSpaces.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {
  class Type;
  class IntegerType;
  class PointerType;
}

namespace clang {
namespace CodeGen {

/// This structure provides a set of types that are commonly used
/// during IR emission.  It's initialized once in CodeGenModule's
/// constructor and then copied around into new CodeGenFunctions.
struct CodeGenTypeCache {
  /// void
  llvm::Type *VoidTy;

  /// i8, i16, i32, and i64
  llvm::IntegerType *Int8Ty, *Int16Ty, *Int32Ty, *Int64Ty;
  /// float, double
  llvm::Type *HalfTy, *FloatTy, *DoubleTy;

  /// int
  llvm::IntegerType *IntTy;

  /// intptr_t, size_t, and ptrdiff_t, which we assume are the same size.
  union {
    llvm::IntegerType *IntPtrTy;
    llvm::IntegerType *SizeTy;
    llvm::IntegerType *PtrDiffTy;
  };

  /// void* in address space 0
  union {
    llvm::PointerType *VoidPtrTy;
    llvm::PointerType *Int8PtrTy;
  };

  /// void** in address space 0
  union {
    llvm::PointerType *VoidPtrPtrTy;
    llvm::PointerType *Int8PtrPtrTy;
  };

  /// void* in alloca address space
  union {
    llvm::PointerType *AllocaVoidPtrTy;
    llvm::PointerType *AllocaInt8PtrTy;
  };

  /// The size and alignment of the builtin C type 'int'.  This comes
  /// up enough in various ABI lowering tasks to be worth pre-computing.
  union {
    unsigned char IntSizeInBytes;
    unsigned char IntAlignInBytes;
  };
  CharUnits getIntSize() const {
    return CharUnits::fromQuantity(IntSizeInBytes);
  }
  CharUnits getIntAlign() const {
    return CharUnits::fromQuantity(IntAlignInBytes);
  }

  /// The width of a pointer into the generic address space.
  unsigned char PointerWidthInBits;

  /// The size and alignment of a pointer into the generic address space.
  union {
    unsigned char PointerAlignInBytes;
    unsigned char PointerSizeInBytes;
  };

  /// The size and alignment of size_t.
  union {
    unsigned char SizeSizeInBytes; // sizeof(size_t)
    unsigned char SizeAlignInBytes;
  };

  LangAS ASTAllocaAddressSpace;

  CharUnits getSizeSize() const {
    return CharUnits::fromQuantity(SizeSizeInBytes);
  }
  CharUnits getSizeAlign() const {
    return CharUnits::fromQuantity(SizeAlignInBytes);
  }
  CharUnits getPointerSize() const {
    return CharUnits::fromQuantity(PointerSizeInBytes);
  }
  CharUnits getPointerAlign() const {
    return CharUnits::fromQuantity(PointerAlignInBytes);
  }

  llvm::CallingConv::ID RuntimeCC;
  llvm::CallingConv::ID getRuntimeCC() const { return RuntimeCC; }

  LangAS getASTAllocaAddressSpace() const { return ASTAllocaAddressSpace; }
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
