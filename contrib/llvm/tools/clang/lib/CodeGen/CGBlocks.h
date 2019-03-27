//===-- CGBlocks.h - state for LLVM CodeGen for blocks ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the internal state used for llvm translation for block literals.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGBLOCKS_H
#define LLVM_CLANG_LIB_CODEGEN_CGBLOCKS_H

#include "CGBuilder.h"
#include "CGCall.h"
#include "CGValue.h"
#include "CodeGenFunction.h"
#include "CodeGenTypes.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Type.h"
#include "clang/Basic/TargetInfo.h"

namespace llvm {
class Constant;
class Function;
class GlobalValue;
class DataLayout;
class FunctionType;
class PointerType;
class Value;
class LLVMContext;
}

namespace clang {
namespace CodeGen {

class CGBlockInfo;

// Flags stored in __block variables.
enum BlockByrefFlags {
  BLOCK_BYREF_HAS_COPY_DISPOSE         = (1   << 25), // compiler
  BLOCK_BYREF_LAYOUT_MASK              = (0xF << 28), // compiler
  BLOCK_BYREF_LAYOUT_EXTENDED          = (1   << 28),
  BLOCK_BYREF_LAYOUT_NON_OBJECT        = (2   << 28),
  BLOCK_BYREF_LAYOUT_STRONG            = (3   << 28),
  BLOCK_BYREF_LAYOUT_WEAK              = (4   << 28),
  BLOCK_BYREF_LAYOUT_UNRETAINED        = (5   << 28)
};

enum BlockLiteralFlags {
  BLOCK_IS_NOESCAPE      =  (1 << 23),
  BLOCK_HAS_COPY_DISPOSE =  (1 << 25),
  BLOCK_HAS_CXX_OBJ =       (1 << 26),
  BLOCK_IS_GLOBAL =         (1 << 28),
  BLOCK_USE_STRET =         (1 << 29),
  BLOCK_HAS_SIGNATURE  =    (1 << 30),
  BLOCK_HAS_EXTENDED_LAYOUT = (1u << 31)
};
class BlockFlags {
  uint32_t flags;

public:
  BlockFlags(uint32_t flags) : flags(flags) {}
  BlockFlags() : flags(0) {}
  BlockFlags(BlockLiteralFlags flag) : flags(flag) {}
  BlockFlags(BlockByrefFlags flag) : flags(flag) {}

  uint32_t getBitMask() const { return flags; }
  bool empty() const { return flags == 0; }

  friend BlockFlags operator|(BlockFlags l, BlockFlags r) {
    return BlockFlags(l.flags | r.flags);
  }
  friend BlockFlags &operator|=(BlockFlags &l, BlockFlags r) {
    l.flags |= r.flags;
    return l;
  }
  friend bool operator&(BlockFlags l, BlockFlags r) {
    return (l.flags & r.flags);
  }
  bool operator==(BlockFlags r) {
    return (flags == r.flags);
  }
};
inline BlockFlags operator|(BlockLiteralFlags l, BlockLiteralFlags r) {
  return BlockFlags(l) | BlockFlags(r);
}

enum BlockFieldFlag_t {
  BLOCK_FIELD_IS_OBJECT   = 0x03,  /* id, NSObject, __attribute__((NSObject)),
                                    block, ... */
  BLOCK_FIELD_IS_BLOCK    = 0x07,  /* a block variable */

  BLOCK_FIELD_IS_BYREF    = 0x08,  /* the on stack structure holding the __block
                                    variable */
  BLOCK_FIELD_IS_WEAK     = 0x10,  /* declared __weak, only used in byref copy
                                    helpers */
  BLOCK_FIELD_IS_ARC      = 0x40,  /* field has ARC-specific semantics */
  BLOCK_BYREF_CALLER      = 128,   /* called from __block (byref) copy/dispose
                                      support routines */
  BLOCK_BYREF_CURRENT_MAX = 256
};

class BlockFieldFlags {
  uint32_t flags;

  BlockFieldFlags(uint32_t flags) : flags(flags) {}
public:
  BlockFieldFlags() : flags(0) {}
  BlockFieldFlags(BlockFieldFlag_t flag) : flags(flag) {}

  uint32_t getBitMask() const { return flags; }
  bool empty() const { return flags == 0; }

  /// Answers whether the flags indicate that this field is an object
  /// or block pointer that requires _Block_object_assign/dispose.
  bool isSpecialPointer() const { return flags & BLOCK_FIELD_IS_OBJECT; }

  friend BlockFieldFlags operator|(BlockFieldFlags l, BlockFieldFlags r) {
    return BlockFieldFlags(l.flags | r.flags);
  }
  friend BlockFieldFlags &operator|=(BlockFieldFlags &l, BlockFieldFlags r) {
    l.flags |= r.flags;
    return l;
  }
  friend bool operator&(BlockFieldFlags l, BlockFieldFlags r) {
    return (l.flags & r.flags);
  }
  bool operator==(BlockFieldFlags Other) const {
    return flags == Other.flags;
  }
};
inline BlockFieldFlags operator|(BlockFieldFlag_t l, BlockFieldFlag_t r) {
  return BlockFieldFlags(l) | BlockFieldFlags(r);
}

/// Information about the layout of a __block variable.
class BlockByrefInfo {
public:
  llvm::StructType *Type;
  unsigned FieldIndex;
  CharUnits ByrefAlignment;
  CharUnits FieldOffset;
};

/// CGBlockInfo - Information to generate a block literal.
class CGBlockInfo {
public:
  /// Name - The name of the block, kindof.
  StringRef Name;

  /// The field index of 'this' within the block, if there is one.
  unsigned CXXThisIndex;

  class Capture {
    uintptr_t Data;
    EHScopeStack::stable_iterator Cleanup;
    CharUnits::QuantityType Offset;

    /// Type of the capture field. Normally, this is identical to the type of
    /// the capture's VarDecl, but can be different if there is an enclosing
    /// lambda.
    QualType FieldType;

  public:
    bool isIndex() const { return (Data & 1) != 0; }
    bool isConstant() const { return !isIndex(); }

    unsigned getIndex() const {
      assert(isIndex());
      return Data >> 1;
    }
    CharUnits getOffset() const {
      assert(isIndex());
      return CharUnits::fromQuantity(Offset);
    }
    EHScopeStack::stable_iterator getCleanup() const {
      assert(isIndex());
      return Cleanup;
    }
    void setCleanup(EHScopeStack::stable_iterator cleanup) {
      assert(isIndex());
      Cleanup = cleanup;
    }

    llvm::Value *getConstant() const {
      assert(isConstant());
      return reinterpret_cast<llvm::Value*>(Data);
    }

    QualType fieldType() const {
      return FieldType;
    }

    static Capture makeIndex(unsigned index, CharUnits offset,
                             QualType FieldType) {
      Capture v;
      v.Data = (index << 1) | 1;
      v.Offset = offset.getQuantity();
      v.FieldType = FieldType;
      return v;
    }

    static Capture makeConstant(llvm::Value *value) {
      Capture v;
      v.Data = reinterpret_cast<uintptr_t>(value);
      return v;
    }
  };

  /// CanBeGlobal - True if the block can be global, i.e. it has
  /// no non-constant captures.
  bool CanBeGlobal : 1;

  /// True if the block has captures that would necessitate custom copy or
  /// dispose helper functions if the block were escaping.
  bool NeedsCopyDispose : 1;

  /// HasCXXObject - True if the block's custom copy/dispose functions
  /// need to be run even in GC mode.
  bool HasCXXObject : 1;

  /// UsesStret : True if the block uses an stret return.  Mutable
  /// because it gets set later in the block-creation process.
  mutable bool UsesStret : 1;

  /// HasCapturedVariableLayout : True if block has captured variables
  /// and their layout meta-data has been generated.
  bool HasCapturedVariableLayout : 1;

  /// Indicates whether an object of a non-external C++ class is captured. This
  /// bit is used to determine the linkage of the block copy/destroy helper
  /// functions.
  bool CapturesNonExternalType : 1;

  /// The mapping of allocated indexes within the block.
  llvm::DenseMap<const VarDecl*, Capture> Captures;

  Address LocalAddress;
  llvm::StructType *StructureType;
  const BlockDecl *Block;
  const BlockExpr *BlockExpression;
  CharUnits BlockSize;
  CharUnits BlockAlign;
  CharUnits CXXThisOffset;

  // Offset of the gap caused by block header having a smaller
  // alignment than the alignment of the block descriptor. This
  // is the gap offset before the first capturued field.
  CharUnits BlockHeaderForcedGapOffset;
  // Gap size caused by aligning first field after block header.
  // This could be zero if no forced alignment is required.
  CharUnits BlockHeaderForcedGapSize;

  /// An instruction which dominates the full-expression that the
  /// block is inside.
  llvm::Instruction *DominatingIP;

  /// The next block in the block-info chain.  Invalid if this block
  /// info is not part of the CGF's block-info chain, which is true
  /// if it corresponds to a global block or a block whose expression
  /// has been encountered.
  CGBlockInfo *NextBlockInfo;

  const Capture &getCapture(const VarDecl *var) const {
    return const_cast<CGBlockInfo*>(this)->getCapture(var);
  }
  Capture &getCapture(const VarDecl *var) {
    llvm::DenseMap<const VarDecl*, Capture>::iterator
      it = Captures.find(var);
    assert(it != Captures.end() && "no entry for variable!");
    return it->second;
  }

  const BlockDecl *getBlockDecl() const { return Block; }
  const BlockExpr *getBlockExpr() const {
    assert(BlockExpression);
    assert(BlockExpression->getBlockDecl() == Block);
    return BlockExpression;
  }

  CGBlockInfo(const BlockDecl *blockDecl, StringRef Name);

  // Indicates whether the block needs a custom copy or dispose function.
  bool needsCopyDisposeHelpers() const {
    return NeedsCopyDispose && !Block->doesNotEscape();
  }
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
