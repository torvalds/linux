//===--- CGBlocks.cpp - Emit LLVM Code for declarations ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit blocks.
//
//===----------------------------------------------------------------------===//

#include "CGBlocks.h"
#include "CGCXXABI.h"
#include "CGDebugInfo.h"
#include "CGObjCRuntime.h"
#include "CGOpenCLRuntime.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "TargetInfo.h"
#include "clang/AST/DeclObjC.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ScopedPrinter.h"
#include <algorithm>
#include <cstdio>

using namespace clang;
using namespace CodeGen;

CGBlockInfo::CGBlockInfo(const BlockDecl *block, StringRef name)
  : Name(name), CXXThisIndex(0), CanBeGlobal(false), NeedsCopyDispose(false),
    HasCXXObject(false), UsesStret(false), HasCapturedVariableLayout(false),
    CapturesNonExternalType(false), LocalAddress(Address::invalid()),
    StructureType(nullptr), Block(block), DominatingIP(nullptr) {

  // Skip asm prefix, if any.  'name' is usually taken directly from
  // the mangled name of the enclosing function.
  if (!name.empty() && name[0] == '\01')
    name = name.substr(1);
}

// Anchor the vtable to this translation unit.
BlockByrefHelpers::~BlockByrefHelpers() {}

/// Build the given block as a global block.
static llvm::Constant *buildGlobalBlock(CodeGenModule &CGM,
                                        const CGBlockInfo &blockInfo,
                                        llvm::Constant *blockFn);

/// Build the helper function to copy a block.
static llvm::Constant *buildCopyHelper(CodeGenModule &CGM,
                                       const CGBlockInfo &blockInfo) {
  return CodeGenFunction(CGM).GenerateCopyHelperFunction(blockInfo);
}

/// Build the helper function to dispose of a block.
static llvm::Constant *buildDisposeHelper(CodeGenModule &CGM,
                                          const CGBlockInfo &blockInfo) {
  return CodeGenFunction(CGM).GenerateDestroyHelperFunction(blockInfo);
}

namespace {

/// Represents a type of copy/destroy operation that should be performed for an
/// entity that's captured by a block.
enum class BlockCaptureEntityKind {
  CXXRecord, // Copy or destroy
  ARCWeak,
  ARCStrong,
  NonTrivialCStruct,
  BlockObject, // Assign or release
  None
};

/// Represents a captured entity that requires extra operations in order for
/// this entity to be copied or destroyed correctly.
struct BlockCaptureManagedEntity {
  BlockCaptureEntityKind CopyKind, DisposeKind;
  BlockFieldFlags CopyFlags, DisposeFlags;
  const BlockDecl::Capture *CI;
  const CGBlockInfo::Capture *Capture;

  BlockCaptureManagedEntity(BlockCaptureEntityKind CopyType,
                            BlockCaptureEntityKind DisposeType,
                            BlockFieldFlags CopyFlags,
                            BlockFieldFlags DisposeFlags,
                            const BlockDecl::Capture &CI,
                            const CGBlockInfo::Capture &Capture)
      : CopyKind(CopyType), DisposeKind(DisposeType), CopyFlags(CopyFlags),
        DisposeFlags(DisposeFlags), CI(&CI), Capture(&Capture) {}

  bool operator<(const BlockCaptureManagedEntity &Other) const {
    return Capture->getOffset() < Other.Capture->getOffset();
  }
};

enum class CaptureStrKind {
  // String for the copy helper.
  CopyHelper,
  // String for the dispose helper.
  DisposeHelper,
  // Merge the strings for the copy helper and dispose helper.
  Merged
};

} // end anonymous namespace

static void findBlockCapturedManagedEntities(
    const CGBlockInfo &BlockInfo, const LangOptions &LangOpts,
    SmallVectorImpl<BlockCaptureManagedEntity> &ManagedCaptures);

static std::string getBlockCaptureStr(const BlockCaptureManagedEntity &E,
                                      CaptureStrKind StrKind,
                                      CharUnits BlockAlignment,
                                      CodeGenModule &CGM);

static std::string getBlockDescriptorName(const CGBlockInfo &BlockInfo,
                                          CodeGenModule &CGM) {
  std::string Name = "__block_descriptor_";
  Name += llvm::to_string(BlockInfo.BlockSize.getQuantity()) + "_";

  if (BlockInfo.needsCopyDisposeHelpers()) {
    if (CGM.getLangOpts().Exceptions)
      Name += "e";
    if (CGM.getCodeGenOpts().ObjCAutoRefCountExceptions)
      Name += "a";
    Name += llvm::to_string(BlockInfo.BlockAlign.getQuantity()) + "_";

    SmallVector<BlockCaptureManagedEntity, 4> ManagedCaptures;
    findBlockCapturedManagedEntities(BlockInfo, CGM.getContext().getLangOpts(),
                                     ManagedCaptures);

    for (const BlockCaptureManagedEntity &E : ManagedCaptures) {
      Name += llvm::to_string(E.Capture->getOffset().getQuantity());

      if (E.CopyKind == E.DisposeKind) {
        // If CopyKind and DisposeKind are the same, merge the capture
        // information.
        assert(E.CopyKind != BlockCaptureEntityKind::None &&
               "shouldn't see BlockCaptureManagedEntity that is None");
        Name += getBlockCaptureStr(E, CaptureStrKind::Merged,
                                   BlockInfo.BlockAlign, CGM);
      } else {
        // If CopyKind and DisposeKind are not the same, which can happen when
        // either Kind is None or the captured object is a __strong block,
        // concatenate the copy and dispose strings.
        Name += getBlockCaptureStr(E, CaptureStrKind::CopyHelper,
                                   BlockInfo.BlockAlign, CGM);
        Name += getBlockCaptureStr(E, CaptureStrKind::DisposeHelper,
                                   BlockInfo.BlockAlign, CGM);
      }
    }
    Name += "_";
  }

  std::string TypeAtEncoding =
      CGM.getContext().getObjCEncodingForBlock(BlockInfo.getBlockExpr());
  /// Replace occurrences of '@' with '\1'. '@' is reserved on ELF platforms as
  /// a separator between symbol name and symbol version.
  std::replace(TypeAtEncoding.begin(), TypeAtEncoding.end(), '@', '\1');
  Name += "e" + llvm::to_string(TypeAtEncoding.size()) + "_" + TypeAtEncoding;
  Name += "l" + CGM.getObjCRuntime().getRCBlockLayoutStr(CGM, BlockInfo);
  return Name;
}

/// buildBlockDescriptor - Build the block descriptor meta-data for a block.
/// buildBlockDescriptor is accessed from 5th field of the Block_literal
/// meta-data and contains stationary information about the block literal.
/// Its definition will have 4 (or optionally 6) words.
/// \code
/// struct Block_descriptor {
///   unsigned long reserved;
///   unsigned long size;  // size of Block_literal metadata in bytes.
///   void *copy_func_helper_decl;  // optional copy helper.
///   void *destroy_func_decl; // optional destructor helper.
///   void *block_method_encoding_address; // @encode for block literal signature.
///   void *block_layout_info; // encoding of captured block variables.
/// };
/// \endcode
static llvm::Constant *buildBlockDescriptor(CodeGenModule &CGM,
                                            const CGBlockInfo &blockInfo) {
  ASTContext &C = CGM.getContext();

  llvm::IntegerType *ulong =
    cast<llvm::IntegerType>(CGM.getTypes().ConvertType(C.UnsignedLongTy));
  llvm::PointerType *i8p = nullptr;
  if (CGM.getLangOpts().OpenCL)
    i8p =
      llvm::Type::getInt8PtrTy(
           CGM.getLLVMContext(), C.getTargetAddressSpace(LangAS::opencl_constant));
  else
    i8p = CGM.VoidPtrTy;

  std::string descName;

  // If an equivalent block descriptor global variable exists, return it.
  if (C.getLangOpts().ObjC &&
      CGM.getLangOpts().getGC() == LangOptions::NonGC) {
    descName = getBlockDescriptorName(blockInfo, CGM);
    if (llvm::GlobalValue *desc = CGM.getModule().getNamedValue(descName))
      return llvm::ConstantExpr::getBitCast(desc,
                                            CGM.getBlockDescriptorType());
  }

  // If there isn't an equivalent block descriptor global variable, create a new
  // one.
  ConstantInitBuilder builder(CGM);
  auto elements = builder.beginStruct();

  // reserved
  elements.addInt(ulong, 0);

  // Size
  // FIXME: What is the right way to say this doesn't fit?  We should give
  // a user diagnostic in that case.  Better fix would be to change the
  // API to size_t.
  elements.addInt(ulong, blockInfo.BlockSize.getQuantity());

  // Optional copy/dispose helpers.
  bool hasInternalHelper = false;
  if (blockInfo.needsCopyDisposeHelpers()) {
    // copy_func_helper_decl
    llvm::Constant *copyHelper = buildCopyHelper(CGM, blockInfo);
    elements.add(copyHelper);

    // destroy_func_decl
    llvm::Constant *disposeHelper = buildDisposeHelper(CGM, blockInfo);
    elements.add(disposeHelper);

    if (cast<llvm::Function>(copyHelper->getOperand(0))->hasInternalLinkage() ||
        cast<llvm::Function>(disposeHelper->getOperand(0))
            ->hasInternalLinkage())
      hasInternalHelper = true;
  }

  // Signature.  Mandatory ObjC-style method descriptor @encode sequence.
  std::string typeAtEncoding =
    CGM.getContext().getObjCEncodingForBlock(blockInfo.getBlockExpr());
  elements.add(llvm::ConstantExpr::getBitCast(
    CGM.GetAddrOfConstantCString(typeAtEncoding).getPointer(), i8p));

  // GC layout.
  if (C.getLangOpts().ObjC) {
    if (CGM.getLangOpts().getGC() != LangOptions::NonGC)
      elements.add(CGM.getObjCRuntime().BuildGCBlockLayout(CGM, blockInfo));
    else
      elements.add(CGM.getObjCRuntime().BuildRCBlockLayout(CGM, blockInfo));
  }
  else
    elements.addNullPointer(i8p);

  unsigned AddrSpace = 0;
  if (C.getLangOpts().OpenCL)
    AddrSpace = C.getTargetAddressSpace(LangAS::opencl_constant);

  llvm::GlobalValue::LinkageTypes linkage;
  if (descName.empty()) {
    linkage = llvm::GlobalValue::InternalLinkage;
    descName = "__block_descriptor_tmp";
  } else if (hasInternalHelper) {
    // If either the copy helper or the dispose helper has internal linkage,
    // the block descriptor must have internal linkage too.
    linkage = llvm::GlobalValue::InternalLinkage;
  } else {
    linkage = llvm::GlobalValue::LinkOnceODRLinkage;
  }

  llvm::GlobalVariable *global =
      elements.finishAndCreateGlobal(descName, CGM.getPointerAlign(),
                                     /*constant*/ true, linkage, AddrSpace);

  if (linkage == llvm::GlobalValue::LinkOnceODRLinkage) {
    global->setVisibility(llvm::GlobalValue::HiddenVisibility);
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  }

  return llvm::ConstantExpr::getBitCast(global, CGM.getBlockDescriptorType());
}

/*
  Purely notional variadic template describing the layout of a block.

  template <class _ResultType, class... _ParamTypes, class... _CaptureTypes>
  struct Block_literal {
    /// Initialized to one of:
    ///   extern void *_NSConcreteStackBlock[];
    ///   extern void *_NSConcreteGlobalBlock[];
    ///
    /// In theory, we could start one off malloc'ed by setting
    /// BLOCK_NEEDS_FREE, giving it a refcount of 1, and using
    /// this isa:
    ///   extern void *_NSConcreteMallocBlock[];
    struct objc_class *isa;

    /// These are the flags (with corresponding bit number) that the
    /// compiler is actually supposed to know about.
    ///  23. BLOCK_IS_NOESCAPE - indicates that the block is non-escaping
    ///  25. BLOCK_HAS_COPY_DISPOSE - indicates that the block
    ///   descriptor provides copy and dispose helper functions
    ///  26. BLOCK_HAS_CXX_OBJ - indicates that there's a captured
    ///   object with a nontrivial destructor or copy constructor
    ///  28. BLOCK_IS_GLOBAL - indicates that the block is allocated
    ///   as global memory
    ///  29. BLOCK_USE_STRET - indicates that the block function
    ///   uses stret, which objc_msgSend needs to know about
    ///  30. BLOCK_HAS_SIGNATURE - indicates that the block has an
    ///   @encoded signature string
    /// And we're not supposed to manipulate these:
    ///  24. BLOCK_NEEDS_FREE - indicates that the block has been moved
    ///   to malloc'ed memory
    ///  27. BLOCK_IS_GC - indicates that the block has been moved to
    ///   to GC-allocated memory
    /// Additionally, the bottom 16 bits are a reference count which
    /// should be zero on the stack.
    int flags;

    /// Reserved;  should be zero-initialized.
    int reserved;

    /// Function pointer generated from block literal.
    _ResultType (*invoke)(Block_literal *, _ParamTypes...);

    /// Block description metadata generated from block literal.
    struct Block_descriptor *block_descriptor;

    /// Captured values follow.
    _CapturesTypes captures...;
  };
 */

namespace {
  /// A chunk of data that we actually have to capture in the block.
  struct BlockLayoutChunk {
    CharUnits Alignment;
    CharUnits Size;
    Qualifiers::ObjCLifetime Lifetime;
    const BlockDecl::Capture *Capture; // null for 'this'
    llvm::Type *Type;
    QualType FieldType;

    BlockLayoutChunk(CharUnits align, CharUnits size,
                     Qualifiers::ObjCLifetime lifetime,
                     const BlockDecl::Capture *capture,
                     llvm::Type *type, QualType fieldType)
      : Alignment(align), Size(size), Lifetime(lifetime),
        Capture(capture), Type(type), FieldType(fieldType) {}

    /// Tell the block info that this chunk has the given field index.
    void setIndex(CGBlockInfo &info, unsigned index, CharUnits offset) {
      if (!Capture) {
        info.CXXThisIndex = index;
        info.CXXThisOffset = offset;
      } else {
        auto C = CGBlockInfo::Capture::makeIndex(index, offset, FieldType);
        info.Captures.insert({Capture->getVariable(), C});
      }
    }
  };

  /// Order by 1) all __strong together 2) next, all byfref together 3) next,
  /// all __weak together. Preserve descending alignment in all situations.
  bool operator<(const BlockLayoutChunk &left, const BlockLayoutChunk &right) {
    if (left.Alignment != right.Alignment)
      return left.Alignment > right.Alignment;

    auto getPrefOrder = [](const BlockLayoutChunk &chunk) {
      if (chunk.Capture && chunk.Capture->isByRef())
        return 1;
      if (chunk.Lifetime == Qualifiers::OCL_Strong)
        return 0;
      if (chunk.Lifetime == Qualifiers::OCL_Weak)
        return 2;
      return 3;
    };

    return getPrefOrder(left) < getPrefOrder(right);
  }
} // end anonymous namespace

/// Determines if the given type is safe for constant capture in C++.
static bool isSafeForCXXConstantCapture(QualType type) {
  const RecordType *recordType =
    type->getBaseElementTypeUnsafe()->getAs<RecordType>();

  // Only records can be unsafe.
  if (!recordType) return true;

  const auto *record = cast<CXXRecordDecl>(recordType->getDecl());

  // Maintain semantics for classes with non-trivial dtors or copy ctors.
  if (!record->hasTrivialDestructor()) return false;
  if (record->hasNonTrivialCopyConstructor()) return false;

  // Otherwise, we just have to make sure there aren't any mutable
  // fields that might have changed since initialization.
  return !record->hasMutableFields();
}

/// It is illegal to modify a const object after initialization.
/// Therefore, if a const object has a constant initializer, we don't
/// actually need to keep storage for it in the block; we'll just
/// rematerialize it at the start of the block function.  This is
/// acceptable because we make no promises about address stability of
/// captured variables.
static llvm::Constant *tryCaptureAsConstant(CodeGenModule &CGM,
                                            CodeGenFunction *CGF,
                                            const VarDecl *var) {
  // Return if this is a function parameter. We shouldn't try to
  // rematerialize default arguments of function parameters.
  if (isa<ParmVarDecl>(var))
    return nullptr;

  QualType type = var->getType();

  // We can only do this if the variable is const.
  if (!type.isConstQualified()) return nullptr;

  // Furthermore, in C++ we have to worry about mutable fields:
  // C++ [dcl.type.cv]p4:
  //   Except that any class member declared mutable can be
  //   modified, any attempt to modify a const object during its
  //   lifetime results in undefined behavior.
  if (CGM.getLangOpts().CPlusPlus && !isSafeForCXXConstantCapture(type))
    return nullptr;

  // If the variable doesn't have any initializer (shouldn't this be
  // invalid?), it's not clear what we should do.  Maybe capture as
  // zero?
  const Expr *init = var->getInit();
  if (!init) return nullptr;

  return ConstantEmitter(CGM, CGF).tryEmitAbstractForInitializer(*var);
}

/// Get the low bit of a nonzero character count.  This is the
/// alignment of the nth byte if the 0th byte is universally aligned.
static CharUnits getLowBit(CharUnits v) {
  return CharUnits::fromQuantity(v.getQuantity() & (~v.getQuantity() + 1));
}

static void initializeForBlockHeader(CodeGenModule &CGM, CGBlockInfo &info,
                             SmallVectorImpl<llvm::Type*> &elementTypes) {

  assert(elementTypes.empty());
  if (CGM.getLangOpts().OpenCL) {
    // The header is basically 'struct { int; int; generic void *;
    // custom_fields; }'. Assert that struct is packed.
    auto GenericAS =
        CGM.getContext().getTargetAddressSpace(LangAS::opencl_generic);
    auto GenPtrAlign =
        CharUnits::fromQuantity(CGM.getTarget().getPointerAlign(GenericAS) / 8);
    auto GenPtrSize =
        CharUnits::fromQuantity(CGM.getTarget().getPointerWidth(GenericAS) / 8);
    assert(CGM.getIntSize() <= GenPtrSize);
    assert(CGM.getIntAlign() <= GenPtrAlign);
    assert((2 * CGM.getIntSize()).isMultipleOf(GenPtrAlign));
    elementTypes.push_back(CGM.IntTy); /* total size */
    elementTypes.push_back(CGM.IntTy); /* align */
    elementTypes.push_back(
        CGM.getOpenCLRuntime()
            .getGenericVoidPointerType()); /* invoke function */
    unsigned Offset =
        2 * CGM.getIntSize().getQuantity() + GenPtrSize.getQuantity();
    unsigned BlockAlign = GenPtrAlign.getQuantity();
    if (auto *Helper =
            CGM.getTargetCodeGenInfo().getTargetOpenCLBlockHelper()) {
      for (auto I : Helper->getCustomFieldTypes()) /* custom fields */ {
        // TargetOpenCLBlockHelp needs to make sure the struct is packed.
        // If necessary, add padding fields to the custom fields.
        unsigned Align = CGM.getDataLayout().getABITypeAlignment(I);
        if (BlockAlign < Align)
          BlockAlign = Align;
        assert(Offset % Align == 0);
        Offset += CGM.getDataLayout().getTypeAllocSize(I);
        elementTypes.push_back(I);
      }
    }
    info.BlockAlign = CharUnits::fromQuantity(BlockAlign);
    info.BlockSize = CharUnits::fromQuantity(Offset);
  } else {
    // The header is basically 'struct { void *; int; int; void *; void *; }'.
    // Assert that the struct is packed.
    assert(CGM.getIntSize() <= CGM.getPointerSize());
    assert(CGM.getIntAlign() <= CGM.getPointerAlign());
    assert((2 * CGM.getIntSize()).isMultipleOf(CGM.getPointerAlign()));
    info.BlockAlign = CGM.getPointerAlign();
    info.BlockSize = 3 * CGM.getPointerSize() + 2 * CGM.getIntSize();
    elementTypes.push_back(CGM.VoidPtrTy);
    elementTypes.push_back(CGM.IntTy);
    elementTypes.push_back(CGM.IntTy);
    elementTypes.push_back(CGM.VoidPtrTy);
    elementTypes.push_back(CGM.getBlockDescriptorType());
  }
}

static QualType getCaptureFieldType(const CodeGenFunction &CGF,
                                    const BlockDecl::Capture &CI) {
  const VarDecl *VD = CI.getVariable();

  // If the variable is captured by an enclosing block or lambda expression,
  // use the type of the capture field.
  if (CGF.BlockInfo && CI.isNested())
    return CGF.BlockInfo->getCapture(VD).fieldType();
  if (auto *FD = CGF.LambdaCaptureFields.lookup(VD))
    return FD->getType();
  // If the captured variable is a non-escaping __block variable, the field
  // type is the reference type. If the variable is a __block variable that
  // already has a reference type, the field type is the variable's type.
  return VD->isNonEscapingByref() ?
         CGF.getContext().getLValueReferenceType(VD->getType()) : VD->getType();
}

/// Compute the layout of the given block.  Attempts to lay the block
/// out with minimal space requirements.
static void computeBlockInfo(CodeGenModule &CGM, CodeGenFunction *CGF,
                             CGBlockInfo &info) {
  ASTContext &C = CGM.getContext();
  const BlockDecl *block = info.getBlockDecl();

  SmallVector<llvm::Type*, 8> elementTypes;
  initializeForBlockHeader(CGM, info, elementTypes);
  bool hasNonConstantCustomFields = false;
  if (auto *OpenCLHelper =
          CGM.getTargetCodeGenInfo().getTargetOpenCLBlockHelper())
    hasNonConstantCustomFields =
        !OpenCLHelper->areAllCustomFieldValuesConstant(info);
  if (!block->hasCaptures() && !hasNonConstantCustomFields) {
    info.StructureType =
      llvm::StructType::get(CGM.getLLVMContext(), elementTypes, true);
    info.CanBeGlobal = true;
    return;
  }
  else if (C.getLangOpts().ObjC &&
           CGM.getLangOpts().getGC() == LangOptions::NonGC)
    info.HasCapturedVariableLayout = true;

  // Collect the layout chunks.
  SmallVector<BlockLayoutChunk, 16> layout;
  layout.reserve(block->capturesCXXThis() +
                 (block->capture_end() - block->capture_begin()));

  CharUnits maxFieldAlign;

  // First, 'this'.
  if (block->capturesCXXThis()) {
    assert(CGF && CGF->CurFuncDecl && isa<CXXMethodDecl>(CGF->CurFuncDecl) &&
           "Can't capture 'this' outside a method");
    QualType thisType = cast<CXXMethodDecl>(CGF->CurFuncDecl)->getThisType();

    // Theoretically, this could be in a different address space, so
    // don't assume standard pointer size/align.
    llvm::Type *llvmType = CGM.getTypes().ConvertType(thisType);
    std::pair<CharUnits,CharUnits> tinfo
      = CGM.getContext().getTypeInfoInChars(thisType);
    maxFieldAlign = std::max(maxFieldAlign, tinfo.second);

    layout.push_back(BlockLayoutChunk(tinfo.second, tinfo.first,
                                      Qualifiers::OCL_None,
                                      nullptr, llvmType, thisType));
  }

  // Next, all the block captures.
  for (const auto &CI : block->captures()) {
    const VarDecl *variable = CI.getVariable();

    if (CI.isEscapingByref()) {
      // We have to copy/dispose of the __block reference.
      info.NeedsCopyDispose = true;

      // Just use void* instead of a pointer to the byref type.
      CharUnits align = CGM.getPointerAlign();
      maxFieldAlign = std::max(maxFieldAlign, align);

      // Since a __block variable cannot be captured by lambdas, its type and
      // the capture field type should always match.
      assert(getCaptureFieldType(*CGF, CI) == variable->getType() &&
             "capture type differs from the variable type");
      layout.push_back(BlockLayoutChunk(align, CGM.getPointerSize(),
                                        Qualifiers::OCL_None, &CI,
                                        CGM.VoidPtrTy, variable->getType()));
      continue;
    }

    // Otherwise, build a layout chunk with the size and alignment of
    // the declaration.
    if (llvm::Constant *constant = tryCaptureAsConstant(CGM, CGF, variable)) {
      info.Captures[variable] = CGBlockInfo::Capture::makeConstant(constant);
      continue;
    }

    QualType VT = getCaptureFieldType(*CGF, CI);

    // If we have a lifetime qualifier, honor it for capture purposes.
    // That includes *not* copying it if it's __unsafe_unretained.
    Qualifiers::ObjCLifetime lifetime = VT.getObjCLifetime();
    if (lifetime) {
      switch (lifetime) {
      case Qualifiers::OCL_None: llvm_unreachable("impossible");
      case Qualifiers::OCL_ExplicitNone:
      case Qualifiers::OCL_Autoreleasing:
        break;

      case Qualifiers::OCL_Strong:
      case Qualifiers::OCL_Weak:
        info.NeedsCopyDispose = true;
      }

    // Block pointers require copy/dispose.  So do Objective-C pointers.
    } else if (VT->isObjCRetainableType()) {
      // But honor the inert __unsafe_unretained qualifier, which doesn't
      // actually make it into the type system.
       if (VT->isObjCInertUnsafeUnretainedType()) {
        lifetime = Qualifiers::OCL_ExplicitNone;
      } else {
        info.NeedsCopyDispose = true;
        // used for mrr below.
        lifetime = Qualifiers::OCL_Strong;
      }

    // So do types that require non-trivial copy construction.
    } else if (CI.hasCopyExpr()) {
      info.NeedsCopyDispose = true;
      info.HasCXXObject = true;
      if (!VT->getAsCXXRecordDecl()->isExternallyVisible())
        info.CapturesNonExternalType = true;

    // So do C structs that require non-trivial copy construction or
    // destruction.
    } else if (VT.isNonTrivialToPrimitiveCopy() == QualType::PCK_Struct ||
               VT.isDestructedType() == QualType::DK_nontrivial_c_struct) {
      info.NeedsCopyDispose = true;

    // And so do types with destructors.
    } else if (CGM.getLangOpts().CPlusPlus) {
      if (const CXXRecordDecl *record = VT->getAsCXXRecordDecl()) {
        if (!record->hasTrivialDestructor()) {
          info.HasCXXObject = true;
          info.NeedsCopyDispose = true;
          if (!record->isExternallyVisible())
            info.CapturesNonExternalType = true;
        }
      }
    }

    CharUnits size = C.getTypeSizeInChars(VT);
    CharUnits align = C.getDeclAlign(variable);

    maxFieldAlign = std::max(maxFieldAlign, align);

    llvm::Type *llvmType =
      CGM.getTypes().ConvertTypeForMem(VT);

    layout.push_back(
        BlockLayoutChunk(align, size, lifetime, &CI, llvmType, VT));
  }

  // If that was everything, we're done here.
  if (layout.empty()) {
    info.StructureType =
      llvm::StructType::get(CGM.getLLVMContext(), elementTypes, true);
    info.CanBeGlobal = true;
    return;
  }

  // Sort the layout by alignment.  We have to use a stable sort here
  // to get reproducible results.  There should probably be an
  // llvm::array_pod_stable_sort.
  std::stable_sort(layout.begin(), layout.end());

  // Needed for blocks layout info.
  info.BlockHeaderForcedGapOffset = info.BlockSize;
  info.BlockHeaderForcedGapSize = CharUnits::Zero();

  CharUnits &blockSize = info.BlockSize;
  info.BlockAlign = std::max(maxFieldAlign, info.BlockAlign);

  // Assuming that the first byte in the header is maximally aligned,
  // get the alignment of the first byte following the header.
  CharUnits endAlign = getLowBit(blockSize);

  // If the end of the header isn't satisfactorily aligned for the
  // maximum thing, look for things that are okay with the header-end
  // alignment, and keep appending them until we get something that's
  // aligned right.  This algorithm is only guaranteed optimal if
  // that condition is satisfied at some point; otherwise we can get
  // things like:
  //   header                 // next byte has alignment 4
  //   something_with_size_5; // next byte has alignment 1
  //   something_with_alignment_8;
  // which has 7 bytes of padding, as opposed to the naive solution
  // which might have less (?).
  if (endAlign < maxFieldAlign) {
    SmallVectorImpl<BlockLayoutChunk>::iterator
      li = layout.begin() + 1, le = layout.end();

    // Look for something that the header end is already
    // satisfactorily aligned for.
    for (; li != le && endAlign < li->Alignment; ++li)
      ;

    // If we found something that's naturally aligned for the end of
    // the header, keep adding things...
    if (li != le) {
      SmallVectorImpl<BlockLayoutChunk>::iterator first = li;
      for (; li != le; ++li) {
        assert(endAlign >= li->Alignment);

        li->setIndex(info, elementTypes.size(), blockSize);
        elementTypes.push_back(li->Type);
        blockSize += li->Size;
        endAlign = getLowBit(blockSize);

        // ...until we get to the alignment of the maximum field.
        if (endAlign >= maxFieldAlign) {
          break;
        }
      }
      // Don't re-append everything we just appended.
      layout.erase(first, li);
    }
  }

  assert(endAlign == getLowBit(blockSize));

  // At this point, we just have to add padding if the end align still
  // isn't aligned right.
  if (endAlign < maxFieldAlign) {
    CharUnits newBlockSize = blockSize.alignTo(maxFieldAlign);
    CharUnits padding = newBlockSize - blockSize;

    // If we haven't yet added any fields, remember that there was an
    // initial gap; this need to go into the block layout bit map.
    if (blockSize == info.BlockHeaderForcedGapOffset) {
      info.BlockHeaderForcedGapSize = padding;
    }

    elementTypes.push_back(llvm::ArrayType::get(CGM.Int8Ty,
                                                padding.getQuantity()));
    blockSize = newBlockSize;
    endAlign = getLowBit(blockSize); // might be > maxFieldAlign
  }

  assert(endAlign >= maxFieldAlign);
  assert(endAlign == getLowBit(blockSize));
  // Slam everything else on now.  This works because they have
  // strictly decreasing alignment and we expect that size is always a
  // multiple of alignment.
  for (SmallVectorImpl<BlockLayoutChunk>::iterator
         li = layout.begin(), le = layout.end(); li != le; ++li) {
    if (endAlign < li->Alignment) {
      // size may not be multiple of alignment. This can only happen with
      // an over-aligned variable. We will be adding a padding field to
      // make the size be multiple of alignment.
      CharUnits padding = li->Alignment - endAlign;
      elementTypes.push_back(llvm::ArrayType::get(CGM.Int8Ty,
                                                  padding.getQuantity()));
      blockSize += padding;
      endAlign = getLowBit(blockSize);
    }
    assert(endAlign >= li->Alignment);
    li->setIndex(info, elementTypes.size(), blockSize);
    elementTypes.push_back(li->Type);
    blockSize += li->Size;
    endAlign = getLowBit(blockSize);
  }

  info.StructureType =
    llvm::StructType::get(CGM.getLLVMContext(), elementTypes, true);
}

/// Enter the scope of a block.  This should be run at the entrance to
/// a full-expression so that the block's cleanups are pushed at the
/// right place in the stack.
static void enterBlockScope(CodeGenFunction &CGF, BlockDecl *block) {
  assert(CGF.HaveInsertPoint());

  // Allocate the block info and place it at the head of the list.
  CGBlockInfo &blockInfo =
    *new CGBlockInfo(block, CGF.CurFn->getName());
  blockInfo.NextBlockInfo = CGF.FirstBlockInfo;
  CGF.FirstBlockInfo = &blockInfo;

  // Compute information about the layout, etc., of this block,
  // pushing cleanups as necessary.
  computeBlockInfo(CGF.CGM, &CGF, blockInfo);

  // Nothing else to do if it can be global.
  if (blockInfo.CanBeGlobal) return;

  // Make the allocation for the block.
  blockInfo.LocalAddress = CGF.CreateTempAlloca(blockInfo.StructureType,
                                                blockInfo.BlockAlign, "block");

  // If there are cleanups to emit, enter them (but inactive).
  if (!blockInfo.NeedsCopyDispose) return;

  // Walk through the captures (in order) and find the ones not
  // captured by constant.
  for (const auto &CI : block->captures()) {
    // Ignore __block captures; there's nothing special in the
    // on-stack block that we need to do for them.
    if (CI.isByRef()) continue;

    // Ignore variables that are constant-captured.
    const VarDecl *variable = CI.getVariable();
    CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
    if (capture.isConstant()) continue;

    // Ignore objects that aren't destructed.
    QualType VT = getCaptureFieldType(CGF, CI);
    QualType::DestructionKind dtorKind = VT.isDestructedType();
    if (dtorKind == QualType::DK_none) continue;

    CodeGenFunction::Destroyer *destroyer;

    // Block captures count as local values and have imprecise semantics.
    // They also can't be arrays, so need to worry about that.
    //
    // For const-qualified captures, emit clang.arc.use to ensure the captured
    // object doesn't get released while we are still depending on its validity
    // within the block.
    if (VT.isConstQualified() &&
        VT.getObjCLifetime() == Qualifiers::OCL_Strong &&
        CGF.CGM.getCodeGenOpts().OptimizationLevel != 0) {
      assert(CGF.CGM.getLangOpts().ObjCAutoRefCount &&
             "expected ObjC ARC to be enabled");
      destroyer = CodeGenFunction::emitARCIntrinsicUse;
    } else if (dtorKind == QualType::DK_objc_strong_lifetime) {
      destroyer = CodeGenFunction::destroyARCStrongImprecise;
    } else {
      destroyer = CGF.getDestroyer(dtorKind);
    }

    // GEP down to the address.
    Address addr = CGF.Builder.CreateStructGEP(blockInfo.LocalAddress,
                                               capture.getIndex(),
                                               capture.getOffset());

    // We can use that GEP as the dominating IP.
    if (!blockInfo.DominatingIP)
      blockInfo.DominatingIP = cast<llvm::Instruction>(addr.getPointer());

    CleanupKind cleanupKind = InactiveNormalCleanup;
    bool useArrayEHCleanup = CGF.needsEHCleanup(dtorKind);
    if (useArrayEHCleanup)
      cleanupKind = InactiveNormalAndEHCleanup;

    CGF.pushDestroy(cleanupKind, addr, VT,
                    destroyer, useArrayEHCleanup);

    // Remember where that cleanup was.
    capture.setCleanup(CGF.EHStack.stable_begin());
  }
}

/// Enter a full-expression with a non-trivial number of objects to
/// clean up.  This is in this file because, at the moment, the only
/// kind of cleanup object is a BlockDecl*.
void CodeGenFunction::enterNonTrivialFullExpression(const FullExpr *E) {
  if (const auto EWC = dyn_cast<ExprWithCleanups>(E)) {
    assert(EWC->getNumObjects() != 0);
    for (const ExprWithCleanups::CleanupObject &C : EWC->getObjects())
      enterBlockScope(*this, C);
  }
}

/// Find the layout for the given block in a linked list and remove it.
static CGBlockInfo *findAndRemoveBlockInfo(CGBlockInfo **head,
                                           const BlockDecl *block) {
  while (true) {
    assert(head && *head);
    CGBlockInfo *cur = *head;

    // If this is the block we're looking for, splice it out of the list.
    if (cur->getBlockDecl() == block) {
      *head = cur->NextBlockInfo;
      return cur;
    }

    head = &cur->NextBlockInfo;
  }
}

/// Destroy a chain of block layouts.
void CodeGenFunction::destroyBlockInfos(CGBlockInfo *head) {
  assert(head && "destroying an empty chain");
  do {
    CGBlockInfo *cur = head;
    head = cur->NextBlockInfo;
    delete cur;
  } while (head != nullptr);
}

/// Emit a block literal expression in the current function.
llvm::Value *CodeGenFunction::EmitBlockLiteral(const BlockExpr *blockExpr) {
  // If the block has no captures, we won't have a pre-computed
  // layout for it.
  if (!blockExpr->getBlockDecl()->hasCaptures()) {
    // The block literal is emitted as a global variable, and the block invoke
    // function has to be extracted from its initializer.
    if (llvm::Constant *Block = CGM.getAddrOfGlobalBlockIfEmitted(blockExpr)) {
      return Block;
    }
    CGBlockInfo blockInfo(blockExpr->getBlockDecl(), CurFn->getName());
    computeBlockInfo(CGM, this, blockInfo);
    blockInfo.BlockExpression = blockExpr;
    return EmitBlockLiteral(blockInfo);
  }

  // Find the block info for this block and take ownership of it.
  std::unique_ptr<CGBlockInfo> blockInfo;
  blockInfo.reset(findAndRemoveBlockInfo(&FirstBlockInfo,
                                         blockExpr->getBlockDecl()));

  blockInfo->BlockExpression = blockExpr;
  return EmitBlockLiteral(*blockInfo);
}

llvm::Value *CodeGenFunction::EmitBlockLiteral(const CGBlockInfo &blockInfo) {
  bool IsOpenCL = CGM.getContext().getLangOpts().OpenCL;
  auto GenVoidPtrTy =
      IsOpenCL ? CGM.getOpenCLRuntime().getGenericVoidPointerType() : VoidPtrTy;
  LangAS GenVoidPtrAddr = IsOpenCL ? LangAS::opencl_generic : LangAS::Default;
  auto GenVoidPtrSize = CharUnits::fromQuantity(
      CGM.getTarget().getPointerWidth(
          CGM.getContext().getTargetAddressSpace(GenVoidPtrAddr)) /
      8);
  // Using the computed layout, generate the actual block function.
  bool isLambdaConv = blockInfo.getBlockDecl()->isConversionFromLambda();
  CodeGenFunction BlockCGF{CGM, true};
  BlockCGF.SanOpts = SanOpts;
  auto *InvokeFn = BlockCGF.GenerateBlockFunction(
      CurGD, blockInfo, LocalDeclMap, isLambdaConv, blockInfo.CanBeGlobal);
  auto *blockFn = llvm::ConstantExpr::getPointerCast(InvokeFn, GenVoidPtrTy);

  // If there is nothing to capture, we can emit this as a global block.
  if (blockInfo.CanBeGlobal)
    return CGM.getAddrOfGlobalBlockIfEmitted(blockInfo.BlockExpression);

  // Otherwise, we have to emit this as a local block.

  Address blockAddr = blockInfo.LocalAddress;
  assert(blockAddr.isValid() && "block has no address!");

  llvm::Constant *isa;
  llvm::Constant *descriptor;
  BlockFlags flags;
  if (!IsOpenCL) {
    // If the block is non-escaping, set field 'isa 'to NSConcreteGlobalBlock
    // and set the BLOCK_IS_GLOBAL bit of field 'flags'. Copying a non-escaping
    // block just returns the original block and releasing it is a no-op.
    llvm::Constant *blockISA = blockInfo.getBlockDecl()->doesNotEscape()
                                   ? CGM.getNSConcreteGlobalBlock()
                                   : CGM.getNSConcreteStackBlock();
    isa = llvm::ConstantExpr::getBitCast(blockISA, VoidPtrTy);

    // Build the block descriptor.
    descriptor = buildBlockDescriptor(CGM, blockInfo);

    // Compute the initial on-stack block flags.
    flags = BLOCK_HAS_SIGNATURE;
    if (blockInfo.HasCapturedVariableLayout)
      flags |= BLOCK_HAS_EXTENDED_LAYOUT;
    if (blockInfo.needsCopyDisposeHelpers())
      flags |= BLOCK_HAS_COPY_DISPOSE;
    if (blockInfo.HasCXXObject)
      flags |= BLOCK_HAS_CXX_OBJ;
    if (blockInfo.UsesStret)
      flags |= BLOCK_USE_STRET;
    if (blockInfo.getBlockDecl()->doesNotEscape())
      flags |= BLOCK_IS_NOESCAPE | BLOCK_IS_GLOBAL;
  }

  auto projectField =
    [&](unsigned index, CharUnits offset, const Twine &name) -> Address {
      return Builder.CreateStructGEP(blockAddr, index, offset, name);
    };
  auto storeField =
    [&](llvm::Value *value, unsigned index, CharUnits offset,
        const Twine &name) {
      Builder.CreateStore(value, projectField(index, offset, name));
    };

  // Initialize the block header.
  {
    // We assume all the header fields are densely packed.
    unsigned index = 0;
    CharUnits offset;
    auto addHeaderField =
      [&](llvm::Value *value, CharUnits size, const Twine &name) {
        storeField(value, index, offset, name);
        offset += size;
        index++;
      };

    if (!IsOpenCL) {
      addHeaderField(isa, getPointerSize(), "block.isa");
      addHeaderField(llvm::ConstantInt::get(IntTy, flags.getBitMask()),
                     getIntSize(), "block.flags");
      addHeaderField(llvm::ConstantInt::get(IntTy, 0), getIntSize(),
                     "block.reserved");
    } else {
      addHeaderField(
          llvm::ConstantInt::get(IntTy, blockInfo.BlockSize.getQuantity()),
          getIntSize(), "block.size");
      addHeaderField(
          llvm::ConstantInt::get(IntTy, blockInfo.BlockAlign.getQuantity()),
          getIntSize(), "block.align");
    }
    addHeaderField(blockFn, GenVoidPtrSize, "block.invoke");
    if (!IsOpenCL)
      addHeaderField(descriptor, getPointerSize(), "block.descriptor");
    else if (auto *Helper =
                 CGM.getTargetCodeGenInfo().getTargetOpenCLBlockHelper()) {
      for (auto I : Helper->getCustomFieldValues(*this, blockInfo)) {
        addHeaderField(
            I.first,
            CharUnits::fromQuantity(
                CGM.getDataLayout().getTypeAllocSize(I.first->getType())),
            I.second);
      }
    }
  }

  // Finally, capture all the values into the block.
  const BlockDecl *blockDecl = blockInfo.getBlockDecl();

  // First, 'this'.
  if (blockDecl->capturesCXXThis()) {
    Address addr = projectField(blockInfo.CXXThisIndex, blockInfo.CXXThisOffset,
                                "block.captured-this.addr");
    Builder.CreateStore(LoadCXXThis(), addr);
  }

  // Next, captured variables.
  for (const auto &CI : blockDecl->captures()) {
    const VarDecl *variable = CI.getVariable();
    const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);

    // Ignore constant captures.
    if (capture.isConstant()) continue;

    QualType type = capture.fieldType();

    // This will be a [[type]]*, except that a byref entry will just be
    // an i8**.
    Address blockField =
      projectField(capture.getIndex(), capture.getOffset(), "block.captured");

    // Compute the address of the thing we're going to move into the
    // block literal.
    Address src = Address::invalid();

    if (blockDecl->isConversionFromLambda()) {
      // The lambda capture in a lambda's conversion-to-block-pointer is
      // special; we'll simply emit it directly.
      src = Address::invalid();
    } else if (CI.isEscapingByref()) {
      if (BlockInfo && CI.isNested()) {
        // We need to use the capture from the enclosing block.
        const CGBlockInfo::Capture &enclosingCapture =
            BlockInfo->getCapture(variable);

        // This is a [[type]]*, except that a byref entry will just be an i8**.
        src = Builder.CreateStructGEP(LoadBlockStruct(),
                                      enclosingCapture.getIndex(),
                                      enclosingCapture.getOffset(),
                                      "block.capture.addr");
      } else {
        auto I = LocalDeclMap.find(variable);
        assert(I != LocalDeclMap.end());
        src = I->second;
      }
    } else {
      DeclRefExpr declRef(getContext(), const_cast<VarDecl *>(variable),
                          /*RefersToEnclosingVariableOrCapture*/ CI.isNested(),
                          type.getNonReferenceType(), VK_LValue,
                          SourceLocation());
      src = EmitDeclRefLValue(&declRef).getAddress();
    };

    // For byrefs, we just write the pointer to the byref struct into
    // the block field.  There's no need to chase the forwarding
    // pointer at this point, since we're building something that will
    // live a shorter life than the stack byref anyway.
    if (CI.isEscapingByref()) {
      // Get a void* that points to the byref struct.
      llvm::Value *byrefPointer;
      if (CI.isNested())
        byrefPointer = Builder.CreateLoad(src, "byref.capture");
      else
        byrefPointer = Builder.CreateBitCast(src.getPointer(), VoidPtrTy);

      // Write that void* into the capture field.
      Builder.CreateStore(byrefPointer, blockField);

    // If we have a copy constructor, evaluate that into the block field.
    } else if (const Expr *copyExpr = CI.getCopyExpr()) {
      if (blockDecl->isConversionFromLambda()) {
        // If we have a lambda conversion, emit the expression
        // directly into the block instead.
        AggValueSlot Slot =
            AggValueSlot::forAddr(blockField, Qualifiers(),
                                  AggValueSlot::IsDestructed,
                                  AggValueSlot::DoesNotNeedGCBarriers,
                                  AggValueSlot::IsNotAliased,
                                  AggValueSlot::DoesNotOverlap);
        EmitAggExpr(copyExpr, Slot);
      } else {
        EmitSynthesizedCXXCopyCtor(blockField, src, copyExpr);
      }

    // If it's a reference variable, copy the reference into the block field.
    } else if (type->isReferenceType()) {
      Builder.CreateStore(src.getPointer(), blockField);

    // If type is const-qualified, copy the value into the block field.
    } else if (type.isConstQualified() &&
               type.getObjCLifetime() == Qualifiers::OCL_Strong &&
               CGM.getCodeGenOpts().OptimizationLevel != 0) {
      llvm::Value *value = Builder.CreateLoad(src, "captured");
      Builder.CreateStore(value, blockField);

    // If this is an ARC __strong block-pointer variable, don't do a
    // block copy.
    //
    // TODO: this can be generalized into the normal initialization logic:
    // we should never need to do a block-copy when initializing a local
    // variable, because the local variable's lifetime should be strictly
    // contained within the stack block's.
    } else if (type.getObjCLifetime() == Qualifiers::OCL_Strong &&
               type->isBlockPointerType()) {
      // Load the block and do a simple retain.
      llvm::Value *value = Builder.CreateLoad(src, "block.captured_block");
      value = EmitARCRetainNonBlock(value);

      // Do a primitive store to the block field.
      Builder.CreateStore(value, blockField);

    // Otherwise, fake up a POD copy into the block field.
    } else {
      // Fake up a new variable so that EmitScalarInit doesn't think
      // we're referring to the variable in its own initializer.
      ImplicitParamDecl BlockFieldPseudoVar(getContext(), type,
                                            ImplicitParamDecl::Other);

      // We use one of these or the other depending on whether the
      // reference is nested.
      DeclRefExpr declRef(getContext(), const_cast<VarDecl *>(variable),
                          /*RefersToEnclosingVariableOrCapture*/ CI.isNested(),
                          type, VK_LValue, SourceLocation());

      ImplicitCastExpr l2r(ImplicitCastExpr::OnStack, type, CK_LValueToRValue,
                           &declRef, VK_RValue);
      // FIXME: Pass a specific location for the expr init so that the store is
      // attributed to a reasonable location - otherwise it may be attributed to
      // locations of subexpressions in the initialization.
      EmitExprAsInit(&l2r, &BlockFieldPseudoVar,
                     MakeAddrLValue(blockField, type, AlignmentSource::Decl),
                     /*captured by init*/ false);
    }

    // Activate the cleanup if layout pushed one.
    if (!CI.isByRef()) {
      EHScopeStack::stable_iterator cleanup = capture.getCleanup();
      if (cleanup.isValid())
        ActivateCleanupBlock(cleanup, blockInfo.DominatingIP);
    }
  }

  // Cast to the converted block-pointer type, which happens (somewhat
  // unfortunately) to be a pointer to function type.
  llvm::Value *result = Builder.CreatePointerCast(
      blockAddr.getPointer(), ConvertType(blockInfo.getBlockExpr()->getType()));

  if (IsOpenCL) {
    CGM.getOpenCLRuntime().recordBlockInfo(blockInfo.BlockExpression, InvokeFn,
                                           result);
  }

  return result;
}


llvm::Type *CodeGenModule::getBlockDescriptorType() {
  if (BlockDescriptorType)
    return BlockDescriptorType;

  llvm::Type *UnsignedLongTy =
    getTypes().ConvertType(getContext().UnsignedLongTy);

  // struct __block_descriptor {
  //   unsigned long reserved;
  //   unsigned long block_size;
  //
  //   // later, the following will be added
  //
  //   struct {
  //     void (*copyHelper)();
  //     void (*copyHelper)();
  //   } helpers;                // !!! optional
  //
  //   const char *signature;   // the block signature
  //   const char *layout;      // reserved
  // };
  BlockDescriptorType = llvm::StructType::create(
      "struct.__block_descriptor", UnsignedLongTy, UnsignedLongTy);

  // Now form a pointer to that.
  unsigned AddrSpace = 0;
  if (getLangOpts().OpenCL)
    AddrSpace = getContext().getTargetAddressSpace(LangAS::opencl_constant);
  BlockDescriptorType = llvm::PointerType::get(BlockDescriptorType, AddrSpace);
  return BlockDescriptorType;
}

llvm::Type *CodeGenModule::getGenericBlockLiteralType() {
  if (GenericBlockLiteralType)
    return GenericBlockLiteralType;

  llvm::Type *BlockDescPtrTy = getBlockDescriptorType();

  if (getLangOpts().OpenCL) {
    // struct __opencl_block_literal_generic {
    //   int __size;
    //   int __align;
    //   __generic void *__invoke;
    //   /* custom fields */
    // };
    SmallVector<llvm::Type *, 8> StructFields(
        {IntTy, IntTy, getOpenCLRuntime().getGenericVoidPointerType()});
    if (auto *Helper = getTargetCodeGenInfo().getTargetOpenCLBlockHelper()) {
      for (auto I : Helper->getCustomFieldTypes())
        StructFields.push_back(I);
    }
    GenericBlockLiteralType = llvm::StructType::create(
        StructFields, "struct.__opencl_block_literal_generic");
  } else {
    // struct __block_literal_generic {
    //   void *__isa;
    //   int __flags;
    //   int __reserved;
    //   void (*__invoke)(void *);
    //   struct __block_descriptor *__descriptor;
    // };
    GenericBlockLiteralType =
        llvm::StructType::create("struct.__block_literal_generic", VoidPtrTy,
                                 IntTy, IntTy, VoidPtrTy, BlockDescPtrTy);
  }

  return GenericBlockLiteralType;
}

RValue CodeGenFunction::EmitBlockCallExpr(const CallExpr *E,
                                          ReturnValueSlot ReturnValue) {
  const BlockPointerType *BPT =
    E->getCallee()->getType()->getAs<BlockPointerType>();

  llvm::Value *BlockPtr = EmitScalarExpr(E->getCallee());

  // Get a pointer to the generic block literal.
  // For OpenCL we generate generic AS void ptr to be able to reuse the same
  // block definition for blocks with captures generated as private AS local
  // variables and without captures generated as global AS program scope
  // variables.
  unsigned AddrSpace = 0;
  if (getLangOpts().OpenCL)
    AddrSpace = getContext().getTargetAddressSpace(LangAS::opencl_generic);

  llvm::Type *BlockLiteralTy =
      llvm::PointerType::get(CGM.getGenericBlockLiteralType(), AddrSpace);

  // Bitcast the callee to a block literal.
  BlockPtr =
      Builder.CreatePointerCast(BlockPtr, BlockLiteralTy, "block.literal");

  // Get the function pointer from the literal.
  llvm::Value *FuncPtr =
      Builder.CreateStructGEP(CGM.getGenericBlockLiteralType(), BlockPtr,
                              CGM.getLangOpts().OpenCL ? 2 : 3);

  // Add the block literal.
  CallArgList Args;

  QualType VoidPtrQualTy = getContext().VoidPtrTy;
  llvm::Type *GenericVoidPtrTy = VoidPtrTy;
  if (getLangOpts().OpenCL) {
    GenericVoidPtrTy = CGM.getOpenCLRuntime().getGenericVoidPointerType();
    VoidPtrQualTy =
        getContext().getPointerType(getContext().getAddrSpaceQualType(
            getContext().VoidTy, LangAS::opencl_generic));
  }

  BlockPtr = Builder.CreatePointerCast(BlockPtr, GenericVoidPtrTy);
  Args.add(RValue::get(BlockPtr), VoidPtrQualTy);

  QualType FnType = BPT->getPointeeType();

  // And the rest of the arguments.
  EmitCallArgs(Args, FnType->getAs<FunctionProtoType>(), E->arguments());

  // Load the function.
  llvm::Value *Func = Builder.CreateAlignedLoad(FuncPtr, getPointerAlign());

  const FunctionType *FuncTy = FnType->castAs<FunctionType>();
  const CGFunctionInfo &FnInfo =
    CGM.getTypes().arrangeBlockFunctionCall(Args, FuncTy);

  // Cast the function pointer to the right type.
  llvm::Type *BlockFTy = CGM.getTypes().GetFunctionType(FnInfo);

  llvm::Type *BlockFTyPtr = llvm::PointerType::getUnqual(BlockFTy);
  Func = Builder.CreatePointerCast(Func, BlockFTyPtr);

  // Prepare the callee.
  CGCallee Callee(CGCalleeInfo(), Func);

  // And call the block.
  return EmitCall(FnInfo, Callee, ReturnValue, Args);
}

Address CodeGenFunction::GetAddrOfBlockDecl(const VarDecl *variable) {
  assert(BlockInfo && "evaluating block ref without block information?");
  const CGBlockInfo::Capture &capture = BlockInfo->getCapture(variable);

  // Handle constant captures.
  if (capture.isConstant()) return LocalDeclMap.find(variable)->second;

  Address addr =
    Builder.CreateStructGEP(LoadBlockStruct(), capture.getIndex(),
                            capture.getOffset(), "block.capture.addr");

  if (variable->isEscapingByref()) {
    // addr should be a void** right now.  Load, then cast the result
    // to byref*.

    auto &byrefInfo = getBlockByrefInfo(variable);
    addr = Address(Builder.CreateLoad(addr), byrefInfo.ByrefAlignment);

    auto byrefPointerType = llvm::PointerType::get(byrefInfo.Type, 0);
    addr = Builder.CreateBitCast(addr, byrefPointerType, "byref.addr");

    addr = emitBlockByrefAddress(addr, byrefInfo, /*follow*/ true,
                                 variable->getName());
  }

  assert((!variable->isNonEscapingByref() ||
          capture.fieldType()->isReferenceType()) &&
         "the capture field of a non-escaping variable should have a "
         "reference type");
  if (capture.fieldType()->isReferenceType())
    addr = EmitLoadOfReference(MakeAddrLValue(addr, capture.fieldType()));

  return addr;
}

void CodeGenModule::setAddrOfGlobalBlock(const BlockExpr *BE,
                                         llvm::Constant *Addr) {
  bool Ok = EmittedGlobalBlocks.insert(std::make_pair(BE, Addr)).second;
  (void)Ok;
  assert(Ok && "Trying to replace an already-existing global block!");
}

llvm::Constant *
CodeGenModule::GetAddrOfGlobalBlock(const BlockExpr *BE,
                                    StringRef Name) {
  if (llvm::Constant *Block = getAddrOfGlobalBlockIfEmitted(BE))
    return Block;

  CGBlockInfo blockInfo(BE->getBlockDecl(), Name);
  blockInfo.BlockExpression = BE;

  // Compute information about the layout, etc., of this block.
  computeBlockInfo(*this, nullptr, blockInfo);

  // Using that metadata, generate the actual block function.
  {
    CodeGenFunction::DeclMapTy LocalDeclMap;
    CodeGenFunction(*this).GenerateBlockFunction(
        GlobalDecl(), blockInfo, LocalDeclMap,
        /*IsLambdaConversionToBlock*/ false, /*BuildGlobalBlock*/ true);
  }

  return getAddrOfGlobalBlockIfEmitted(BE);
}

static llvm::Constant *buildGlobalBlock(CodeGenModule &CGM,
                                        const CGBlockInfo &blockInfo,
                                        llvm::Constant *blockFn) {
  assert(blockInfo.CanBeGlobal);
  // Callers should detect this case on their own: calling this function
  // generally requires computing layout information, which is a waste of time
  // if we've already emitted this block.
  assert(!CGM.getAddrOfGlobalBlockIfEmitted(blockInfo.BlockExpression) &&
         "Refusing to re-emit a global block.");

  // Generate the constants for the block literal initializer.
  ConstantInitBuilder builder(CGM);
  auto fields = builder.beginStruct();

  bool IsOpenCL = CGM.getLangOpts().OpenCL;
  bool IsWindows = CGM.getTarget().getTriple().isOSWindows();
  if (!IsOpenCL) {
    // isa
    if (IsWindows)
      fields.addNullPointer(CGM.Int8PtrPtrTy);
    else
      fields.add(CGM.getNSConcreteGlobalBlock());

    // __flags
    BlockFlags flags = BLOCK_IS_GLOBAL | BLOCK_HAS_SIGNATURE;
    if (blockInfo.UsesStret)
      flags |= BLOCK_USE_STRET;

    fields.addInt(CGM.IntTy, flags.getBitMask());

    // Reserved
    fields.addInt(CGM.IntTy, 0);
  } else {
    fields.addInt(CGM.IntTy, blockInfo.BlockSize.getQuantity());
    fields.addInt(CGM.IntTy, blockInfo.BlockAlign.getQuantity());
  }

  // Function
  fields.add(blockFn);

  if (!IsOpenCL) {
    // Descriptor
    fields.add(buildBlockDescriptor(CGM, blockInfo));
  } else if (auto *Helper =
                 CGM.getTargetCodeGenInfo().getTargetOpenCLBlockHelper()) {
    for (auto I : Helper->getCustomFieldValues(CGM, blockInfo)) {
      fields.add(I);
    }
  }

  unsigned AddrSpace = 0;
  if (CGM.getContext().getLangOpts().OpenCL)
    AddrSpace = CGM.getContext().getTargetAddressSpace(LangAS::opencl_global);

  llvm::Constant *literal = fields.finishAndCreateGlobal(
      "__block_literal_global", blockInfo.BlockAlign,
      /*constant*/ !IsWindows, llvm::GlobalVariable::InternalLinkage, AddrSpace);

  // Windows does not allow globals to be initialised to point to globals in
  // different DLLs.  Any such variables must run code to initialise them.
  if (IsWindows) {
    auto *Init = llvm::Function::Create(llvm::FunctionType::get(CGM.VoidTy,
          {}), llvm::GlobalValue::InternalLinkage, ".block_isa_init",
        &CGM.getModule());
    llvm::IRBuilder<> b(llvm::BasicBlock::Create(CGM.getLLVMContext(), "entry",
          Init));
    b.CreateAlignedStore(CGM.getNSConcreteGlobalBlock(),
        b.CreateStructGEP(literal, 0), CGM.getPointerAlign().getQuantity());
    b.CreateRetVoid();
    // We can't use the normal LLVM global initialisation array, because we
    // need to specify that this runs early in library initialisation.
    auto *InitVar = new llvm::GlobalVariable(CGM.getModule(), Init->getType(),
        /*isConstant*/true, llvm::GlobalValue::InternalLinkage,
        Init, ".block_isa_init_ptr");
    InitVar->setSection(".CRT$XCLa");
    CGM.addUsedGlobal(InitVar);
  }

  // Return a constant of the appropriately-casted type.
  llvm::Type *RequiredType =
    CGM.getTypes().ConvertType(blockInfo.getBlockExpr()->getType());
  llvm::Constant *Result =
      llvm::ConstantExpr::getPointerCast(literal, RequiredType);
  CGM.setAddrOfGlobalBlock(blockInfo.BlockExpression, Result);
  if (CGM.getContext().getLangOpts().OpenCL)
    CGM.getOpenCLRuntime().recordBlockInfo(
        blockInfo.BlockExpression,
        cast<llvm::Function>(blockFn->stripPointerCasts()), Result);
  return Result;
}

void CodeGenFunction::setBlockContextParameter(const ImplicitParamDecl *D,
                                               unsigned argNum,
                                               llvm::Value *arg) {
  assert(BlockInfo && "not emitting prologue of block invocation function?!");

  // Allocate a stack slot like for any local variable to guarantee optimal
  // debug info at -O0. The mem2reg pass will eliminate it when optimizing.
  Address alloc = CreateMemTemp(D->getType(), D->getName() + ".addr");
  Builder.CreateStore(arg, alloc);
  if (CGDebugInfo *DI = getDebugInfo()) {
    if (CGM.getCodeGenOpts().getDebugInfo() >=
        codegenoptions::LimitedDebugInfo) {
      DI->setLocation(D->getLocation());
      DI->EmitDeclareOfBlockLiteralArgVariable(
          *BlockInfo, D->getName(), argNum,
          cast<llvm::AllocaInst>(alloc.getPointer()), Builder);
    }
  }

  SourceLocation StartLoc = BlockInfo->getBlockExpr()->getBody()->getBeginLoc();
  ApplyDebugLocation Scope(*this, StartLoc);

  // Instead of messing around with LocalDeclMap, just set the value
  // directly as BlockPointer.
  BlockPointer = Builder.CreatePointerCast(
      arg,
      BlockInfo->StructureType->getPointerTo(
          getContext().getLangOpts().OpenCL
              ? getContext().getTargetAddressSpace(LangAS::opencl_generic)
              : 0),
      "block");
}

Address CodeGenFunction::LoadBlockStruct() {
  assert(BlockInfo && "not in a block invocation function!");
  assert(BlockPointer && "no block pointer set!");
  return Address(BlockPointer, BlockInfo->BlockAlign);
}

llvm::Function *
CodeGenFunction::GenerateBlockFunction(GlobalDecl GD,
                                       const CGBlockInfo &blockInfo,
                                       const DeclMapTy &ldm,
                                       bool IsLambdaConversionToBlock,
                                       bool BuildGlobalBlock) {
  const BlockDecl *blockDecl = blockInfo.getBlockDecl();

  CurGD = GD;

  CurEHLocation = blockInfo.getBlockExpr()->getEndLoc();

  BlockInfo = &blockInfo;

  // Arrange for local static and local extern declarations to appear
  // to be local to this function as well, in case they're directly
  // referenced in a block.
  for (DeclMapTy::const_iterator i = ldm.begin(), e = ldm.end(); i != e; ++i) {
    const auto *var = dyn_cast<VarDecl>(i->first);
    if (var && !var->hasLocalStorage())
      setAddrOfLocalVar(var, i->second);
  }

  // Begin building the function declaration.

  // Build the argument list.
  FunctionArgList args;

  // The first argument is the block pointer.  Just take it as a void*
  // and cast it later.
  QualType selfTy = getContext().VoidPtrTy;

  // For OpenCL passed block pointer can be private AS local variable or
  // global AS program scope variable (for the case with and without captures).
  // Generic AS is used therefore to be able to accommodate both private and
  // generic AS in one implementation.
  if (getLangOpts().OpenCL)
    selfTy = getContext().getPointerType(getContext().getAddrSpaceQualType(
        getContext().VoidTy, LangAS::opencl_generic));

  IdentifierInfo *II = &CGM.getContext().Idents.get(".block_descriptor");

  ImplicitParamDecl SelfDecl(getContext(), const_cast<BlockDecl *>(blockDecl),
                             SourceLocation(), II, selfTy,
                             ImplicitParamDecl::ObjCSelf);
  args.push_back(&SelfDecl);

  // Now add the rest of the parameters.
  args.append(blockDecl->param_begin(), blockDecl->param_end());

  // Create the function declaration.
  const FunctionProtoType *fnType = blockInfo.getBlockExpr()->getFunctionType();
  const CGFunctionInfo &fnInfo =
    CGM.getTypes().arrangeBlockFunctionDeclaration(fnType, args);
  if (CGM.ReturnSlotInterferesWithArgs(fnInfo))
    blockInfo.UsesStret = true;

  llvm::FunctionType *fnLLVMType = CGM.getTypes().GetFunctionType(fnInfo);

  StringRef name = CGM.getBlockMangledName(GD, blockDecl);
  llvm::Function *fn = llvm::Function::Create(
      fnLLVMType, llvm::GlobalValue::InternalLinkage, name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(blockDecl, fn, fnInfo);

  if (BuildGlobalBlock) {
    auto GenVoidPtrTy = getContext().getLangOpts().OpenCL
                            ? CGM.getOpenCLRuntime().getGenericVoidPointerType()
                            : VoidPtrTy;
    buildGlobalBlock(CGM, blockInfo,
                     llvm::ConstantExpr::getPointerCast(fn, GenVoidPtrTy));
  }

  // Begin generating the function.
  StartFunction(blockDecl, fnType->getReturnType(), fn, fnInfo, args,
                blockDecl->getLocation(),
                blockInfo.getBlockExpr()->getBody()->getBeginLoc());

  // Okay.  Undo some of what StartFunction did.

  // At -O0 we generate an explicit alloca for the BlockPointer, so the RA
  // won't delete the dbg.declare intrinsics for captured variables.
  llvm::Value *BlockPointerDbgLoc = BlockPointer;
  if (CGM.getCodeGenOpts().OptimizationLevel == 0) {
    // Allocate a stack slot for it, so we can point the debugger to it
    Address Alloca = CreateTempAlloca(BlockPointer->getType(),
                                      getPointerAlign(),
                                      "block.addr");
    // Set the DebugLocation to empty, so the store is recognized as a
    // frame setup instruction by llvm::DwarfDebug::beginFunction().
    auto NL = ApplyDebugLocation::CreateEmpty(*this);
    Builder.CreateStore(BlockPointer, Alloca);
    BlockPointerDbgLoc = Alloca.getPointer();
  }

  // If we have a C++ 'this' reference, go ahead and force it into
  // existence now.
  if (blockDecl->capturesCXXThis()) {
    Address addr =
      Builder.CreateStructGEP(LoadBlockStruct(), blockInfo.CXXThisIndex,
                              blockInfo.CXXThisOffset, "block.captured-this");
    CXXThisValue = Builder.CreateLoad(addr, "this");
  }

  // Also force all the constant captures.
  for (const auto &CI : blockDecl->captures()) {
    const VarDecl *variable = CI.getVariable();
    const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
    if (!capture.isConstant()) continue;

    CharUnits align = getContext().getDeclAlign(variable);
    Address alloca =
      CreateMemTemp(variable->getType(), align, "block.captured-const");

    Builder.CreateStore(capture.getConstant(), alloca);

    setAddrOfLocalVar(variable, alloca);
  }

  // Save a spot to insert the debug information for all the DeclRefExprs.
  llvm::BasicBlock *entry = Builder.GetInsertBlock();
  llvm::BasicBlock::iterator entry_ptr = Builder.GetInsertPoint();
  --entry_ptr;

  if (IsLambdaConversionToBlock)
    EmitLambdaBlockInvokeBody();
  else {
    PGO.assignRegionCounters(GlobalDecl(blockDecl), fn);
    incrementProfileCounter(blockDecl->getBody());
    EmitStmt(blockDecl->getBody());
  }

  // Remember where we were...
  llvm::BasicBlock *resume = Builder.GetInsertBlock();

  // Go back to the entry.
  ++entry_ptr;
  Builder.SetInsertPoint(entry, entry_ptr);

  // Emit debug information for all the DeclRefExprs.
  // FIXME: also for 'this'
  if (CGDebugInfo *DI = getDebugInfo()) {
    for (const auto &CI : blockDecl->captures()) {
      const VarDecl *variable = CI.getVariable();
      DI->EmitLocation(Builder, variable->getLocation());

      if (CGM.getCodeGenOpts().getDebugInfo() >=
          codegenoptions::LimitedDebugInfo) {
        const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
        if (capture.isConstant()) {
          auto addr = LocalDeclMap.find(variable)->second;
          (void)DI->EmitDeclareOfAutoVariable(variable, addr.getPointer(),
                                              Builder);
          continue;
        }

        DI->EmitDeclareOfBlockDeclRefVariable(
            variable, BlockPointerDbgLoc, Builder, blockInfo,
            entry_ptr == entry->end() ? nullptr : &*entry_ptr);
      }
    }
    // Recover location if it was changed in the above loop.
    DI->EmitLocation(Builder,
                     cast<CompoundStmt>(blockDecl->getBody())->getRBracLoc());
  }

  // And resume where we left off.
  if (resume == nullptr)
    Builder.ClearInsertionPoint();
  else
    Builder.SetInsertPoint(resume);

  FinishFunction(cast<CompoundStmt>(blockDecl->getBody())->getRBracLoc());

  return fn;
}

static std::pair<BlockCaptureEntityKind, BlockFieldFlags>
computeCopyInfoForBlockCapture(const BlockDecl::Capture &CI, QualType T,
                               const LangOptions &LangOpts) {
  if (CI.getCopyExpr()) {
    assert(!CI.isByRef());
    // don't bother computing flags
    return std::make_pair(BlockCaptureEntityKind::CXXRecord, BlockFieldFlags());
  }
  BlockFieldFlags Flags;
  if (CI.isEscapingByref()) {
    Flags = BLOCK_FIELD_IS_BYREF;
    if (T.isObjCGCWeak())
      Flags |= BLOCK_FIELD_IS_WEAK;
    return std::make_pair(BlockCaptureEntityKind::BlockObject, Flags);
  }

  Flags = BLOCK_FIELD_IS_OBJECT;
  bool isBlockPointer = T->isBlockPointerType();
  if (isBlockPointer)
    Flags = BLOCK_FIELD_IS_BLOCK;

  switch (T.isNonTrivialToPrimitiveCopy()) {
  case QualType::PCK_Struct:
    return std::make_pair(BlockCaptureEntityKind::NonTrivialCStruct,
                          BlockFieldFlags());
  case QualType::PCK_ARCWeak:
    // We need to register __weak direct captures with the runtime.
    return std::make_pair(BlockCaptureEntityKind::ARCWeak, Flags);
  case QualType::PCK_ARCStrong:
    // We need to retain the copied value for __strong direct captures.
    // If it's a block pointer, we have to copy the block and assign that to
    // the destination pointer, so we might as well use _Block_object_assign.
    // Otherwise we can avoid that.
    return std::make_pair(!isBlockPointer ? BlockCaptureEntityKind::ARCStrong
                                          : BlockCaptureEntityKind::BlockObject,
                          Flags);
  case QualType::PCK_Trivial:
  case QualType::PCK_VolatileTrivial: {
    if (!T->isObjCRetainableType())
      // For all other types, the memcpy is fine.
      return std::make_pair(BlockCaptureEntityKind::None, BlockFieldFlags());

    // Special rules for ARC captures:
    Qualifiers QS = T.getQualifiers();

    // Non-ARC captures of retainable pointers are strong and
    // therefore require a call to _Block_object_assign.
    if (!QS.getObjCLifetime() && !LangOpts.ObjCAutoRefCount)
      return std::make_pair(BlockCaptureEntityKind::BlockObject, Flags);

    // Otherwise the memcpy is fine.
    return std::make_pair(BlockCaptureEntityKind::None, BlockFieldFlags());
  }
  }
  llvm_unreachable("after exhaustive PrimitiveCopyKind switch");
}

static std::pair<BlockCaptureEntityKind, BlockFieldFlags>
computeDestroyInfoForBlockCapture(const BlockDecl::Capture &CI, QualType T,
                                  const LangOptions &LangOpts);

/// Find the set of block captures that need to be explicitly copied or destroy.
static void findBlockCapturedManagedEntities(
    const CGBlockInfo &BlockInfo, const LangOptions &LangOpts,
    SmallVectorImpl<BlockCaptureManagedEntity> &ManagedCaptures) {
  for (const auto &CI : BlockInfo.getBlockDecl()->captures()) {
    const VarDecl *Variable = CI.getVariable();
    const CGBlockInfo::Capture &Capture = BlockInfo.getCapture(Variable);
    if (Capture.isConstant())
      continue;

    QualType VT = Capture.fieldType();
    auto CopyInfo = computeCopyInfoForBlockCapture(CI, VT, LangOpts);
    auto DisposeInfo = computeDestroyInfoForBlockCapture(CI, VT, LangOpts);
    if (CopyInfo.first != BlockCaptureEntityKind::None ||
        DisposeInfo.first != BlockCaptureEntityKind::None)
      ManagedCaptures.emplace_back(CopyInfo.first, DisposeInfo.first,
                                   CopyInfo.second, DisposeInfo.second, CI,
                                   Capture);
  }

  // Sort the captures by offset.
  llvm::sort(ManagedCaptures);
}

namespace {
/// Release a __block variable.
struct CallBlockRelease final : EHScopeStack::Cleanup {
  Address Addr;
  BlockFieldFlags FieldFlags;
  bool LoadBlockVarAddr, CanThrow;

  CallBlockRelease(Address Addr, BlockFieldFlags Flags, bool LoadValue,
                   bool CT)
      : Addr(Addr), FieldFlags(Flags), LoadBlockVarAddr(LoadValue),
        CanThrow(CT) {}

  void Emit(CodeGenFunction &CGF, Flags flags) override {
    llvm::Value *BlockVarAddr;
    if (LoadBlockVarAddr) {
      BlockVarAddr = CGF.Builder.CreateLoad(Addr);
      BlockVarAddr = CGF.Builder.CreateBitCast(BlockVarAddr, CGF.VoidPtrTy);
    } else {
      BlockVarAddr = Addr.getPointer();
    }

    CGF.BuildBlockRelease(BlockVarAddr, FieldFlags, CanThrow);
  }
};
} // end anonymous namespace

/// Check if \p T is a C++ class that has a destructor that can throw.
bool CodeGenFunction::cxxDestructorCanThrow(QualType T) {
  if (const auto *RD = T->getAsCXXRecordDecl())
    if (const CXXDestructorDecl *DD = RD->getDestructor())
      return DD->getType()->getAs<FunctionProtoType>()->canThrow();
  return false;
}

// Return a string that has the information about a capture.
static std::string getBlockCaptureStr(const BlockCaptureManagedEntity &E,
                                      CaptureStrKind StrKind,
                                      CharUnits BlockAlignment,
                                      CodeGenModule &CGM) {
  std::string Str;
  ASTContext &Ctx = CGM.getContext();
  const BlockDecl::Capture &CI = *E.CI;
  QualType CaptureTy = CI.getVariable()->getType();

  BlockCaptureEntityKind Kind;
  BlockFieldFlags Flags;

  // CaptureStrKind::Merged should be passed only when the operations and the
  // flags are the same for copy and dispose.
  assert((StrKind != CaptureStrKind::Merged ||
          (E.CopyKind == E.DisposeKind && E.CopyFlags == E.DisposeFlags)) &&
         "different operations and flags");

  if (StrKind == CaptureStrKind::DisposeHelper) {
    Kind = E.DisposeKind;
    Flags = E.DisposeFlags;
  } else {
    Kind = E.CopyKind;
    Flags = E.CopyFlags;
  }

  switch (Kind) {
  case BlockCaptureEntityKind::CXXRecord: {
    Str += "c";
    SmallString<256> TyStr;
    llvm::raw_svector_ostream Out(TyStr);
    CGM.getCXXABI().getMangleContext().mangleTypeName(CaptureTy, Out);
    Str += llvm::to_string(TyStr.size()) + TyStr.c_str();
    break;
  }
  case BlockCaptureEntityKind::ARCWeak:
    Str += "w";
    break;
  case BlockCaptureEntityKind::ARCStrong:
    Str += "s";
    break;
  case BlockCaptureEntityKind::BlockObject: {
    const VarDecl *Var = CI.getVariable();
    unsigned F = Flags.getBitMask();
    if (F & BLOCK_FIELD_IS_BYREF) {
      Str += "r";
      if (F & BLOCK_FIELD_IS_WEAK)
        Str += "w";
      else {
        // If CaptureStrKind::Merged is passed, check both the copy expression
        // and the destructor.
        if (StrKind != CaptureStrKind::DisposeHelper) {
          if (Ctx.getBlockVarCopyInit(Var).canThrow())
            Str += "c";
        }
        if (StrKind != CaptureStrKind::CopyHelper) {
          if (CodeGenFunction::cxxDestructorCanThrow(CaptureTy))
            Str += "d";
        }
      }
    } else {
      assert((F & BLOCK_FIELD_IS_OBJECT) && "unexpected flag value");
      if (F == BLOCK_FIELD_IS_BLOCK)
        Str += "b";
      else
        Str += "o";
    }
    break;
  }
  case BlockCaptureEntityKind::NonTrivialCStruct: {
    bool IsVolatile = CaptureTy.isVolatileQualified();
    CharUnits Alignment =
        BlockAlignment.alignmentAtOffset(E.Capture->getOffset());

    Str += "n";
    std::string FuncStr;
    if (StrKind == CaptureStrKind::DisposeHelper)
      FuncStr = CodeGenFunction::getNonTrivialDestructorStr(
          CaptureTy, Alignment, IsVolatile, Ctx);
    else
      // If CaptureStrKind::Merged is passed, use the copy constructor string.
      // It has all the information that the destructor string has.
      FuncStr = CodeGenFunction::getNonTrivialCopyConstructorStr(
          CaptureTy, Alignment, IsVolatile, Ctx);
    // The underscore is necessary here because non-trivial copy constructor
    // and destructor strings can start with a number.
    Str += llvm::to_string(FuncStr.size()) + "_" + FuncStr;
    break;
  }
  case BlockCaptureEntityKind::None:
    break;
  }

  return Str;
}

static std::string getCopyDestroyHelperFuncName(
    const SmallVectorImpl<BlockCaptureManagedEntity> &Captures,
    CharUnits BlockAlignment, CaptureStrKind StrKind, CodeGenModule &CGM) {
  assert((StrKind == CaptureStrKind::CopyHelper ||
          StrKind == CaptureStrKind::DisposeHelper) &&
         "unexpected CaptureStrKind");
  std::string Name = StrKind == CaptureStrKind::CopyHelper
                         ? "__copy_helper_block_"
                         : "__destroy_helper_block_";
  if (CGM.getLangOpts().Exceptions)
    Name += "e";
  if (CGM.getCodeGenOpts().ObjCAutoRefCountExceptions)
    Name += "a";
  Name += llvm::to_string(BlockAlignment.getQuantity()) + "_";

  for (const BlockCaptureManagedEntity &E : Captures) {
    Name += llvm::to_string(E.Capture->getOffset().getQuantity());
    Name += getBlockCaptureStr(E, StrKind, BlockAlignment, CGM);
  }

  return Name;
}

static void pushCaptureCleanup(BlockCaptureEntityKind CaptureKind,
                               Address Field, QualType CaptureType,
                               BlockFieldFlags Flags, bool ForCopyHelper,
                               VarDecl *Var, CodeGenFunction &CGF) {
  bool EHOnly = ForCopyHelper;

  switch (CaptureKind) {
  case BlockCaptureEntityKind::CXXRecord:
  case BlockCaptureEntityKind::ARCWeak:
  case BlockCaptureEntityKind::NonTrivialCStruct:
  case BlockCaptureEntityKind::ARCStrong: {
    if (CaptureType.isDestructedType() &&
        (!EHOnly || CGF.needsEHCleanup(CaptureType.isDestructedType()))) {
      CodeGenFunction::Destroyer *Destroyer =
          CaptureKind == BlockCaptureEntityKind::ARCStrong
              ? CodeGenFunction::destroyARCStrongImprecise
              : CGF.getDestroyer(CaptureType.isDestructedType());
      CleanupKind Kind =
          EHOnly ? EHCleanup
                 : CGF.getCleanupKind(CaptureType.isDestructedType());
      CGF.pushDestroy(Kind, Field, CaptureType, Destroyer, Kind & EHCleanup);
    }
    break;
  }
  case BlockCaptureEntityKind::BlockObject: {
    if (!EHOnly || CGF.getLangOpts().Exceptions) {
      CleanupKind Kind = EHOnly ? EHCleanup : NormalAndEHCleanup;
      // Calls to _Block_object_dispose along the EH path in the copy helper
      // function don't throw as newly-copied __block variables always have a
      // reference count of 2.
      bool CanThrow =
          !ForCopyHelper && CGF.cxxDestructorCanThrow(CaptureType);
      CGF.enterByrefCleanup(Kind, Field, Flags, /*LoadBlockVarAddr*/ true,
                            CanThrow);
    }
    break;
  }
  case BlockCaptureEntityKind::None:
    break;
  }
}

static void setBlockHelperAttributesVisibility(bool CapturesNonExternalType,
                                               llvm::Function *Fn,
                                               const CGFunctionInfo &FI,
                                               CodeGenModule &CGM) {
  if (CapturesNonExternalType) {
    CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FI);
  } else {
    Fn->setVisibility(llvm::GlobalValue::HiddenVisibility);
    Fn->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI, Fn);
    CGM.SetLLVMFunctionAttributesForDefinition(nullptr, Fn);
  }
}
/// Generate the copy-helper function for a block closure object:
///   static void block_copy_helper(block_t *dst, block_t *src);
/// The runtime will have previously initialized 'dst' by doing a
/// bit-copy of 'src'.
///
/// Note that this copies an entire block closure object to the heap;
/// it should not be confused with a 'byref copy helper', which moves
/// the contents of an individual __block variable to the heap.
llvm::Constant *
CodeGenFunction::GenerateCopyHelperFunction(const CGBlockInfo &blockInfo) {
  SmallVector<BlockCaptureManagedEntity, 4> CopiedCaptures;
  findBlockCapturedManagedEntities(blockInfo, getLangOpts(), CopiedCaptures);
  std::string FuncName =
      getCopyDestroyHelperFuncName(CopiedCaptures, blockInfo.BlockAlign,
                                   CaptureStrKind::CopyHelper, CGM);

  if (llvm::GlobalValue *Func = CGM.getModule().getNamedValue(FuncName))
    return llvm::ConstantExpr::getBitCast(Func, VoidPtrTy);

  ASTContext &C = getContext();

  QualType ReturnTy = C.VoidTy;

  FunctionArgList args;
  ImplicitParamDecl DstDecl(C, C.VoidPtrTy, ImplicitParamDecl::Other);
  args.push_back(&DstDecl);
  ImplicitParamDecl SrcDecl(C, C.VoidPtrTy, ImplicitParamDecl::Other);
  args.push_back(&SrcDecl);

  const CGFunctionInfo &FI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(ReturnTy, args);

  // FIXME: it would be nice if these were mergeable with things with
  // identical semantics.
  llvm::FunctionType *LTy = CGM.getTypes().GetFunctionType(FI);

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::LinkOnceODRLinkage,
                           FuncName, &CGM.getModule());

  IdentifierInfo *II = &C.Idents.get(FuncName);

  SmallVector<QualType, 2> ArgTys;
  ArgTys.push_back(C.VoidPtrTy);
  ArgTys.push_back(C.VoidPtrTy);
  QualType FunctionTy = C.getFunctionType(ReturnTy, ArgTys, {});

  FunctionDecl *FD = FunctionDecl::Create(
      C, C.getTranslationUnitDecl(), SourceLocation(), SourceLocation(), II,
      FunctionTy, nullptr, SC_Static, false, false);

  setBlockHelperAttributesVisibility(blockInfo.CapturesNonExternalType, Fn, FI,
                                     CGM);
  StartFunction(FD, ReturnTy, Fn, FI, args);
  ApplyDebugLocation NL{*this, blockInfo.getBlockExpr()->getBeginLoc()};
  llvm::Type *structPtrTy = blockInfo.StructureType->getPointerTo();

  Address src = GetAddrOfLocalVar(&SrcDecl);
  src = Address(Builder.CreateLoad(src), blockInfo.BlockAlign);
  src = Builder.CreateBitCast(src, structPtrTy, "block.source");

  Address dst = GetAddrOfLocalVar(&DstDecl);
  dst = Address(Builder.CreateLoad(dst), blockInfo.BlockAlign);
  dst = Builder.CreateBitCast(dst, structPtrTy, "block.dest");

  for (const auto &CopiedCapture : CopiedCaptures) {
    const BlockDecl::Capture &CI = *CopiedCapture.CI;
    const CGBlockInfo::Capture &capture = *CopiedCapture.Capture;
    QualType captureType = CI.getVariable()->getType();
    BlockFieldFlags flags = CopiedCapture.CopyFlags;

    unsigned index = capture.getIndex();
    Address srcField = Builder.CreateStructGEP(src, index, capture.getOffset());
    Address dstField = Builder.CreateStructGEP(dst, index, capture.getOffset());

    switch (CopiedCapture.CopyKind) {
    case BlockCaptureEntityKind::CXXRecord:
      // If there's an explicit copy expression, we do that.
      assert(CI.getCopyExpr() && "copy expression for variable is missing");
      EmitSynthesizedCXXCopyCtor(dstField, srcField, CI.getCopyExpr());
      break;
    case BlockCaptureEntityKind::ARCWeak:
      EmitARCCopyWeak(dstField, srcField);
      break;
    case BlockCaptureEntityKind::NonTrivialCStruct: {
      // If this is a C struct that requires non-trivial copy construction,
      // emit a call to its copy constructor.
      QualType varType = CI.getVariable()->getType();
      callCStructCopyConstructor(MakeAddrLValue(dstField, varType),
                                 MakeAddrLValue(srcField, varType));
      break;
    }
    case BlockCaptureEntityKind::ARCStrong: {
      llvm::Value *srcValue = Builder.CreateLoad(srcField, "blockcopy.src");
      // At -O0, store null into the destination field (so that the
      // storeStrong doesn't over-release) and then call storeStrong.
      // This is a workaround to not having an initStrong call.
      if (CGM.getCodeGenOpts().OptimizationLevel == 0) {
        auto *ty = cast<llvm::PointerType>(srcValue->getType());
        llvm::Value *null = llvm::ConstantPointerNull::get(ty);
        Builder.CreateStore(null, dstField);
        EmitARCStoreStrongCall(dstField, srcValue, true);

      // With optimization enabled, take advantage of the fact that
      // the blocks runtime guarantees a memcpy of the block data, and
      // just emit a retain of the src field.
      } else {
        EmitARCRetainNonBlock(srcValue);

        // Unless EH cleanup is required, we don't need this anymore, so kill
        // it. It's not quite worth the annoyance to avoid creating it in the
        // first place.
        if (!needsEHCleanup(captureType.isDestructedType()))
          cast<llvm::Instruction>(dstField.getPointer())->eraseFromParent();
      }
      break;
    }
    case BlockCaptureEntityKind::BlockObject: {
      llvm::Value *srcValue = Builder.CreateLoad(srcField, "blockcopy.src");
      srcValue = Builder.CreateBitCast(srcValue, VoidPtrTy);
      llvm::Value *dstAddr =
          Builder.CreateBitCast(dstField.getPointer(), VoidPtrTy);
      llvm::Value *args[] = {
        dstAddr, srcValue, llvm::ConstantInt::get(Int32Ty, flags.getBitMask())
      };

      if (CI.isByRef() && C.getBlockVarCopyInit(CI.getVariable()).canThrow())
        EmitRuntimeCallOrInvoke(CGM.getBlockObjectAssign(), args);
      else
        EmitNounwindRuntimeCall(CGM.getBlockObjectAssign(), args);
      break;
    }
    case BlockCaptureEntityKind::None:
      continue;
    }

    // Ensure that we destroy the copied object if an exception is thrown later
    // in the helper function.
    pushCaptureCleanup(CopiedCapture.CopyKind, dstField, captureType, flags,
                       /*ForCopyHelper*/ true, CI.getVariable(), *this);
  }

  FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, VoidPtrTy);
}

static BlockFieldFlags
getBlockFieldFlagsForObjCObjectPointer(const BlockDecl::Capture &CI,
                                       QualType T) {
  BlockFieldFlags Flags = BLOCK_FIELD_IS_OBJECT;
  if (T->isBlockPointerType())
    Flags = BLOCK_FIELD_IS_BLOCK;
  return Flags;
}

static std::pair<BlockCaptureEntityKind, BlockFieldFlags>
computeDestroyInfoForBlockCapture(const BlockDecl::Capture &CI, QualType T,
                                  const LangOptions &LangOpts) {
  if (CI.isEscapingByref()) {
    BlockFieldFlags Flags = BLOCK_FIELD_IS_BYREF;
    if (T.isObjCGCWeak())
      Flags |= BLOCK_FIELD_IS_WEAK;
    return std::make_pair(BlockCaptureEntityKind::BlockObject, Flags);
  }

  switch (T.isDestructedType()) {
  case QualType::DK_cxx_destructor:
    return std::make_pair(BlockCaptureEntityKind::CXXRecord, BlockFieldFlags());
  case QualType::DK_objc_strong_lifetime:
    // Use objc_storeStrong for __strong direct captures; the
    // dynamic tools really like it when we do this.
    return std::make_pair(BlockCaptureEntityKind::ARCStrong,
                          getBlockFieldFlagsForObjCObjectPointer(CI, T));
  case QualType::DK_objc_weak_lifetime:
    // Support __weak direct captures.
    return std::make_pair(BlockCaptureEntityKind::ARCWeak,
                          getBlockFieldFlagsForObjCObjectPointer(CI, T));
  case QualType::DK_nontrivial_c_struct:
    return std::make_pair(BlockCaptureEntityKind::NonTrivialCStruct,
                          BlockFieldFlags());
  case QualType::DK_none: {
    // Non-ARC captures are strong, and we need to use _Block_object_dispose.
    if (T->isObjCRetainableType() && !T.getQualifiers().hasObjCLifetime() &&
        !LangOpts.ObjCAutoRefCount)
      return std::make_pair(BlockCaptureEntityKind::BlockObject,
                            getBlockFieldFlagsForObjCObjectPointer(CI, T));
    // Otherwise, we have nothing to do.
    return std::make_pair(BlockCaptureEntityKind::None, BlockFieldFlags());
  }
  }
  llvm_unreachable("after exhaustive DestructionKind switch");
}

/// Generate the destroy-helper function for a block closure object:
///   static void block_destroy_helper(block_t *theBlock);
///
/// Note that this destroys a heap-allocated block closure object;
/// it should not be confused with a 'byref destroy helper', which
/// destroys the heap-allocated contents of an individual __block
/// variable.
llvm::Constant *
CodeGenFunction::GenerateDestroyHelperFunction(const CGBlockInfo &blockInfo) {
  SmallVector<BlockCaptureManagedEntity, 4> DestroyedCaptures;
  findBlockCapturedManagedEntities(blockInfo, getLangOpts(), DestroyedCaptures);
  std::string FuncName =
      getCopyDestroyHelperFuncName(DestroyedCaptures, blockInfo.BlockAlign,
                                   CaptureStrKind::DisposeHelper, CGM);

  if (llvm::GlobalValue *Func = CGM.getModule().getNamedValue(FuncName))
    return llvm::ConstantExpr::getBitCast(Func, VoidPtrTy);

  ASTContext &C = getContext();

  QualType ReturnTy = C.VoidTy;

  FunctionArgList args;
  ImplicitParamDecl SrcDecl(C, C.VoidPtrTy, ImplicitParamDecl::Other);
  args.push_back(&SrcDecl);

  const CGFunctionInfo &FI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(ReturnTy, args);

  // FIXME: We'd like to put these into a mergable by content, with
  // internal linkage.
  llvm::FunctionType *LTy = CGM.getTypes().GetFunctionType(FI);

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::LinkOnceODRLinkage,
                           FuncName, &CGM.getModule());

  IdentifierInfo *II = &C.Idents.get(FuncName);

  SmallVector<QualType, 1> ArgTys;
  ArgTys.push_back(C.VoidPtrTy);
  QualType FunctionTy = C.getFunctionType(ReturnTy, ArgTys, {});

  FunctionDecl *FD = FunctionDecl::Create(
      C, C.getTranslationUnitDecl(), SourceLocation(), SourceLocation(), II,
      FunctionTy, nullptr, SC_Static, false, false);

  setBlockHelperAttributesVisibility(blockInfo.CapturesNonExternalType, Fn, FI,
                                     CGM);
  StartFunction(FD, ReturnTy, Fn, FI, args);
  markAsIgnoreThreadCheckingAtRuntime(Fn);

  ApplyDebugLocation NL{*this, blockInfo.getBlockExpr()->getBeginLoc()};

  llvm::Type *structPtrTy = blockInfo.StructureType->getPointerTo();

  Address src = GetAddrOfLocalVar(&SrcDecl);
  src = Address(Builder.CreateLoad(src), blockInfo.BlockAlign);
  src = Builder.CreateBitCast(src, structPtrTy, "block");

  CodeGenFunction::RunCleanupsScope cleanups(*this);

  for (const auto &DestroyedCapture : DestroyedCaptures) {
    const BlockDecl::Capture &CI = *DestroyedCapture.CI;
    const CGBlockInfo::Capture &capture = *DestroyedCapture.Capture;
    BlockFieldFlags flags = DestroyedCapture.DisposeFlags;

    Address srcField =
      Builder.CreateStructGEP(src, capture.getIndex(), capture.getOffset());

    pushCaptureCleanup(DestroyedCapture.DisposeKind, srcField,
                       CI.getVariable()->getType(), flags,
                       /*ForCopyHelper*/ false, CI.getVariable(), *this);
  }

  cleanups.ForceCleanup();

  FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, VoidPtrTy);
}

namespace {

/// Emits the copy/dispose helper functions for a __block object of id type.
class ObjectByrefHelpers final : public BlockByrefHelpers {
  BlockFieldFlags Flags;

public:
  ObjectByrefHelpers(CharUnits alignment, BlockFieldFlags flags)
    : BlockByrefHelpers(alignment), Flags(flags) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    destField = CGF.Builder.CreateBitCast(destField, CGF.VoidPtrTy);

    srcField = CGF.Builder.CreateBitCast(srcField, CGF.VoidPtrPtrTy);
    llvm::Value *srcValue = CGF.Builder.CreateLoad(srcField);

    unsigned flags = (Flags | BLOCK_BYREF_CALLER).getBitMask();

    llvm::Value *flagsVal = llvm::ConstantInt::get(CGF.Int32Ty, flags);
    llvm::Value *fn = CGF.CGM.getBlockObjectAssign();

    llvm::Value *args[] = { destField.getPointer(), srcValue, flagsVal };
    CGF.EmitNounwindRuntimeCall(fn, args);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    field = CGF.Builder.CreateBitCast(field, CGF.Int8PtrTy->getPointerTo(0));
    llvm::Value *value = CGF.Builder.CreateLoad(field);

    CGF.BuildBlockRelease(value, Flags | BLOCK_BYREF_CALLER, false);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    id.AddInteger(Flags.getBitMask());
  }
};

/// Emits the copy/dispose helpers for an ARC __block __weak variable.
class ARCWeakByrefHelpers final : public BlockByrefHelpers {
public:
  ARCWeakByrefHelpers(CharUnits alignment) : BlockByrefHelpers(alignment) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    CGF.EmitARCMoveWeak(destField, srcField);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    CGF.EmitARCDestroyWeak(field);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    // 0 is distinguishable from all pointers and byref flags
    id.AddInteger(0);
  }
};

/// Emits the copy/dispose helpers for an ARC __block __strong variable
/// that's not of block-pointer type.
class ARCStrongByrefHelpers final : public BlockByrefHelpers {
public:
  ARCStrongByrefHelpers(CharUnits alignment) : BlockByrefHelpers(alignment) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    // Do a "move" by copying the value and then zeroing out the old
    // variable.

    llvm::Value *value = CGF.Builder.CreateLoad(srcField);

    llvm::Value *null =
      llvm::ConstantPointerNull::get(cast<llvm::PointerType>(value->getType()));

    if (CGF.CGM.getCodeGenOpts().OptimizationLevel == 0) {
      CGF.Builder.CreateStore(null, destField);
      CGF.EmitARCStoreStrongCall(destField, value, /*ignored*/ true);
      CGF.EmitARCStoreStrongCall(srcField, null, /*ignored*/ true);
      return;
    }
    CGF.Builder.CreateStore(value, destField);
    CGF.Builder.CreateStore(null, srcField);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    CGF.EmitARCDestroyStrong(field, ARCImpreciseLifetime);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    // 1 is distinguishable from all pointers and byref flags
    id.AddInteger(1);
  }
};

/// Emits the copy/dispose helpers for an ARC __block __strong
/// variable that's of block-pointer type.
class ARCStrongBlockByrefHelpers final : public BlockByrefHelpers {
public:
  ARCStrongBlockByrefHelpers(CharUnits alignment)
    : BlockByrefHelpers(alignment) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    // Do the copy with objc_retainBlock; that's all that
    // _Block_object_assign would do anyway, and we'd have to pass the
    // right arguments to make sure it doesn't get no-op'ed.
    llvm::Value *oldValue = CGF.Builder.CreateLoad(srcField);
    llvm::Value *copy = CGF.EmitARCRetainBlock(oldValue, /*mandatory*/ true);
    CGF.Builder.CreateStore(copy, destField);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    CGF.EmitARCDestroyStrong(field, ARCImpreciseLifetime);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    // 2 is distinguishable from all pointers and byref flags
    id.AddInteger(2);
  }
};

/// Emits the copy/dispose helpers for a __block variable with a
/// nontrivial copy constructor or destructor.
class CXXByrefHelpers final : public BlockByrefHelpers {
  QualType VarType;
  const Expr *CopyExpr;

public:
  CXXByrefHelpers(CharUnits alignment, QualType type,
                  const Expr *copyExpr)
    : BlockByrefHelpers(alignment), VarType(type), CopyExpr(copyExpr) {}

  bool needsCopy() const override { return CopyExpr != nullptr; }
  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    if (!CopyExpr) return;
    CGF.EmitSynthesizedCXXCopyCtor(destField, srcField, CopyExpr);
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    EHScopeStack::stable_iterator cleanupDepth = CGF.EHStack.stable_begin();
    CGF.PushDestructorCleanup(VarType, field);
    CGF.PopCleanupBlocks(cleanupDepth);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    id.AddPointer(VarType.getCanonicalType().getAsOpaquePtr());
  }
};

/// Emits the copy/dispose helpers for a __block variable that is a non-trivial
/// C struct.
class NonTrivialCStructByrefHelpers final : public BlockByrefHelpers {
  QualType VarType;

public:
  NonTrivialCStructByrefHelpers(CharUnits alignment, QualType type)
    : BlockByrefHelpers(alignment), VarType(type) {}

  void emitCopy(CodeGenFunction &CGF, Address destField,
                Address srcField) override {
    CGF.callCStructMoveConstructor(CGF.MakeAddrLValue(destField, VarType),
                                   CGF.MakeAddrLValue(srcField, VarType));
  }

  bool needsDispose() const override {
    return VarType.isDestructedType();
  }

  void emitDispose(CodeGenFunction &CGF, Address field) override {
    EHScopeStack::stable_iterator cleanupDepth = CGF.EHStack.stable_begin();
    CGF.pushDestroy(VarType.isDestructedType(), field, VarType);
    CGF.PopCleanupBlocks(cleanupDepth);
  }

  void profileImpl(llvm::FoldingSetNodeID &id) const override {
    id.AddPointer(VarType.getCanonicalType().getAsOpaquePtr());
  }
};
} // end anonymous namespace

static llvm::Constant *
generateByrefCopyHelper(CodeGenFunction &CGF, const BlockByrefInfo &byrefInfo,
                        BlockByrefHelpers &generator) {
  ASTContext &Context = CGF.getContext();

  QualType ReturnTy = Context.VoidTy;

  FunctionArgList args;
  ImplicitParamDecl Dst(Context, Context.VoidPtrTy, ImplicitParamDecl::Other);
  args.push_back(&Dst);

  ImplicitParamDecl Src(Context, Context.VoidPtrTy, ImplicitParamDecl::Other);
  args.push_back(&Src);

  const CGFunctionInfo &FI =
      CGF.CGM.getTypes().arrangeBuiltinFunctionDeclaration(ReturnTy, args);

  llvm::FunctionType *LTy = CGF.CGM.getTypes().GetFunctionType(FI);

  // FIXME: We'd like to put these into a mergable by content, with
  // internal linkage.
  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           "__Block_byref_object_copy_", &CGF.CGM.getModule());

  IdentifierInfo *II
    = &Context.Idents.get("__Block_byref_object_copy_");

  SmallVector<QualType, 2> ArgTys;
  ArgTys.push_back(Context.VoidPtrTy);
  ArgTys.push_back(Context.VoidPtrTy);
  QualType FunctionTy = Context.getFunctionType(ReturnTy, ArgTys, {});

  FunctionDecl *FD = FunctionDecl::Create(
      Context, Context.getTranslationUnitDecl(), SourceLocation(),
      SourceLocation(), II, FunctionTy, nullptr, SC_Static, false, false);

  CGF.CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FI);

  CGF.StartFunction(FD, ReturnTy, Fn, FI, args);

  if (generator.needsCopy()) {
    llvm::Type *byrefPtrType = byrefInfo.Type->getPointerTo(0);

    // dst->x
    Address destField = CGF.GetAddrOfLocalVar(&Dst);
    destField = Address(CGF.Builder.CreateLoad(destField),
                        byrefInfo.ByrefAlignment);
    destField = CGF.Builder.CreateBitCast(destField, byrefPtrType);
    destField = CGF.emitBlockByrefAddress(destField, byrefInfo, false,
                                          "dest-object");

    // src->x
    Address srcField = CGF.GetAddrOfLocalVar(&Src);
    srcField = Address(CGF.Builder.CreateLoad(srcField),
                       byrefInfo.ByrefAlignment);
    srcField = CGF.Builder.CreateBitCast(srcField, byrefPtrType);
    srcField = CGF.emitBlockByrefAddress(srcField, byrefInfo, false,
                                         "src-object");

    generator.emitCopy(CGF, destField, srcField);
  }

  CGF.FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, CGF.Int8PtrTy);
}

/// Build the copy helper for a __block variable.
static llvm::Constant *buildByrefCopyHelper(CodeGenModule &CGM,
                                            const BlockByrefInfo &byrefInfo,
                                            BlockByrefHelpers &generator) {
  CodeGenFunction CGF(CGM);
  return generateByrefCopyHelper(CGF, byrefInfo, generator);
}

/// Generate code for a __block variable's dispose helper.
static llvm::Constant *
generateByrefDisposeHelper(CodeGenFunction &CGF,
                           const BlockByrefInfo &byrefInfo,
                           BlockByrefHelpers &generator) {
  ASTContext &Context = CGF.getContext();
  QualType R = Context.VoidTy;

  FunctionArgList args;
  ImplicitParamDecl Src(CGF.getContext(), Context.VoidPtrTy,
                        ImplicitParamDecl::Other);
  args.push_back(&Src);

  const CGFunctionInfo &FI =
    CGF.CGM.getTypes().arrangeBuiltinFunctionDeclaration(R, args);

  llvm::FunctionType *LTy = CGF.CGM.getTypes().GetFunctionType(FI);

  // FIXME: We'd like to put these into a mergable by content, with
  // internal linkage.
  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           "__Block_byref_object_dispose_",
                           &CGF.CGM.getModule());

  IdentifierInfo *II
    = &Context.Idents.get("__Block_byref_object_dispose_");

  SmallVector<QualType, 1> ArgTys;
  ArgTys.push_back(Context.VoidPtrTy);
  QualType FunctionTy = Context.getFunctionType(R, ArgTys, {});

  FunctionDecl *FD = FunctionDecl::Create(
      Context, Context.getTranslationUnitDecl(), SourceLocation(),
      SourceLocation(), II, FunctionTy, nullptr, SC_Static, false, false);

  CGF.CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FI);

  CGF.StartFunction(FD, R, Fn, FI, args);

  if (generator.needsDispose()) {
    Address addr = CGF.GetAddrOfLocalVar(&Src);
    addr = Address(CGF.Builder.CreateLoad(addr), byrefInfo.ByrefAlignment);
    auto byrefPtrType = byrefInfo.Type->getPointerTo(0);
    addr = CGF.Builder.CreateBitCast(addr, byrefPtrType);
    addr = CGF.emitBlockByrefAddress(addr, byrefInfo, false, "object");

    generator.emitDispose(CGF, addr);
  }

  CGF.FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, CGF.Int8PtrTy);
}

/// Build the dispose helper for a __block variable.
static llvm::Constant *buildByrefDisposeHelper(CodeGenModule &CGM,
                                               const BlockByrefInfo &byrefInfo,
                                               BlockByrefHelpers &generator) {
  CodeGenFunction CGF(CGM);
  return generateByrefDisposeHelper(CGF, byrefInfo, generator);
}

/// Lazily build the copy and dispose helpers for a __block variable
/// with the given information.
template <class T>
static T *buildByrefHelpers(CodeGenModule &CGM, const BlockByrefInfo &byrefInfo,
                            T &&generator) {
  llvm::FoldingSetNodeID id;
  generator.Profile(id);

  void *insertPos;
  BlockByrefHelpers *node
    = CGM.ByrefHelpersCache.FindNodeOrInsertPos(id, insertPos);
  if (node) return static_cast<T*>(node);

  generator.CopyHelper = buildByrefCopyHelper(CGM, byrefInfo, generator);
  generator.DisposeHelper = buildByrefDisposeHelper(CGM, byrefInfo, generator);

  T *copy = new (CGM.getContext()) T(std::forward<T>(generator));
  CGM.ByrefHelpersCache.InsertNode(copy, insertPos);
  return copy;
}

/// Build the copy and dispose helpers for the given __block variable
/// emission.  Places the helpers in the global cache.  Returns null
/// if no helpers are required.
BlockByrefHelpers *
CodeGenFunction::buildByrefHelpers(llvm::StructType &byrefType,
                                   const AutoVarEmission &emission) {
  const VarDecl &var = *emission.Variable;
  assert(var.isEscapingByref() &&
         "only escaping __block variables need byref helpers");

  QualType type = var.getType();

  auto &byrefInfo = getBlockByrefInfo(&var);

  // The alignment we care about for the purposes of uniquing byref
  // helpers is the alignment of the actual byref value field.
  CharUnits valueAlignment =
    byrefInfo.ByrefAlignment.alignmentAtOffset(byrefInfo.FieldOffset);

  if (const CXXRecordDecl *record = type->getAsCXXRecordDecl()) {
    const Expr *copyExpr =
        CGM.getContext().getBlockVarCopyInit(&var).getCopyExpr();
    if (!copyExpr && record->hasTrivialDestructor()) return nullptr;

    return ::buildByrefHelpers(
        CGM, byrefInfo, CXXByrefHelpers(valueAlignment, type, copyExpr));
  }

  // If type is a non-trivial C struct type that is non-trivial to
  // destructly move or destroy, build the copy and dispose helpers.
  if (type.isNonTrivialToPrimitiveDestructiveMove() == QualType::PCK_Struct ||
      type.isDestructedType() == QualType::DK_nontrivial_c_struct)
    return ::buildByrefHelpers(
        CGM, byrefInfo, NonTrivialCStructByrefHelpers(valueAlignment, type));

  // Otherwise, if we don't have a retainable type, there's nothing to do.
  // that the runtime does extra copies.
  if (!type->isObjCRetainableType()) return nullptr;

  Qualifiers qs = type.getQualifiers();

  // If we have lifetime, that dominates.
  if (Qualifiers::ObjCLifetime lifetime = qs.getObjCLifetime()) {
    switch (lifetime) {
    case Qualifiers::OCL_None: llvm_unreachable("impossible");

    // These are just bits as far as the runtime is concerned.
    case Qualifiers::OCL_ExplicitNone:
    case Qualifiers::OCL_Autoreleasing:
      return nullptr;

    // Tell the runtime that this is ARC __weak, called by the
    // byref routines.
    case Qualifiers::OCL_Weak:
      return ::buildByrefHelpers(CGM, byrefInfo,
                                 ARCWeakByrefHelpers(valueAlignment));

    // ARC __strong __block variables need to be retained.
    case Qualifiers::OCL_Strong:
      // Block pointers need to be copied, and there's no direct
      // transfer possible.
      if (type->isBlockPointerType()) {
        return ::buildByrefHelpers(CGM, byrefInfo,
                                   ARCStrongBlockByrefHelpers(valueAlignment));

      // Otherwise, we transfer ownership of the retain from the stack
      // to the heap.
      } else {
        return ::buildByrefHelpers(CGM, byrefInfo,
                                   ARCStrongByrefHelpers(valueAlignment));
      }
    }
    llvm_unreachable("fell out of lifetime switch!");
  }

  BlockFieldFlags flags;
  if (type->isBlockPointerType()) {
    flags |= BLOCK_FIELD_IS_BLOCK;
  } else if (CGM.getContext().isObjCNSObjectType(type) ||
             type->isObjCObjectPointerType()) {
    flags |= BLOCK_FIELD_IS_OBJECT;
  } else {
    return nullptr;
  }

  if (type.isObjCGCWeak())
    flags |= BLOCK_FIELD_IS_WEAK;

  return ::buildByrefHelpers(CGM, byrefInfo,
                             ObjectByrefHelpers(valueAlignment, flags));
}

Address CodeGenFunction::emitBlockByrefAddress(Address baseAddr,
                                               const VarDecl *var,
                                               bool followForward) {
  auto &info = getBlockByrefInfo(var);
  return emitBlockByrefAddress(baseAddr, info, followForward, var->getName());
}

Address CodeGenFunction::emitBlockByrefAddress(Address baseAddr,
                                               const BlockByrefInfo &info,
                                               bool followForward,
                                               const llvm::Twine &name) {
  // Chase the forwarding address if requested.
  if (followForward) {
    Address forwardingAddr =
      Builder.CreateStructGEP(baseAddr, 1, getPointerSize(), "forwarding");
    baseAddr = Address(Builder.CreateLoad(forwardingAddr), info.ByrefAlignment);
  }

  return Builder.CreateStructGEP(baseAddr, info.FieldIndex,
                                 info.FieldOffset, name);
}

/// BuildByrefInfo - This routine changes a __block variable declared as T x
///   into:
///
///      struct {
///        void *__isa;
///        void *__forwarding;
///        int32_t __flags;
///        int32_t __size;
///        void *__copy_helper;       // only if needed
///        void *__destroy_helper;    // only if needed
///        void *__byref_variable_layout;// only if needed
///        char padding[X];           // only if needed
///        T x;
///      } x
///
const BlockByrefInfo &CodeGenFunction::getBlockByrefInfo(const VarDecl *D) {
  auto it = BlockByrefInfos.find(D);
  if (it != BlockByrefInfos.end())
    return it->second;

  llvm::StructType *byrefType =
    llvm::StructType::create(getLLVMContext(),
                             "struct.__block_byref_" + D->getNameAsString());

  QualType Ty = D->getType();

  CharUnits size;
  SmallVector<llvm::Type *, 8> types;

  // void *__isa;
  types.push_back(Int8PtrTy);
  size += getPointerSize();

  // void *__forwarding;
  types.push_back(llvm::PointerType::getUnqual(byrefType));
  size += getPointerSize();

  // int32_t __flags;
  types.push_back(Int32Ty);
  size += CharUnits::fromQuantity(4);

  // int32_t __size;
  types.push_back(Int32Ty);
  size += CharUnits::fromQuantity(4);

  // Note that this must match *exactly* the logic in buildByrefHelpers.
  bool hasCopyAndDispose = getContext().BlockRequiresCopying(Ty, D);
  if (hasCopyAndDispose) {
    /// void *__copy_helper;
    types.push_back(Int8PtrTy);
    size += getPointerSize();

    /// void *__destroy_helper;
    types.push_back(Int8PtrTy);
    size += getPointerSize();
  }

  bool HasByrefExtendedLayout = false;
  Qualifiers::ObjCLifetime Lifetime;
  if (getContext().getByrefLifetime(Ty, Lifetime, HasByrefExtendedLayout) &&
      HasByrefExtendedLayout) {
    /// void *__byref_variable_layout;
    types.push_back(Int8PtrTy);
    size += CharUnits::fromQuantity(PointerSizeInBytes);
  }

  // T x;
  llvm::Type *varTy = ConvertTypeForMem(Ty);

  bool packed = false;
  CharUnits varAlign = getContext().getDeclAlign(D);
  CharUnits varOffset = size.alignTo(varAlign);

  // We may have to insert padding.
  if (varOffset != size) {
    llvm::Type *paddingTy =
      llvm::ArrayType::get(Int8Ty, (varOffset - size).getQuantity());

    types.push_back(paddingTy);
    size = varOffset;

  // Conversely, we might have to prevent LLVM from inserting padding.
  } else if (CGM.getDataLayout().getABITypeAlignment(varTy)
               > varAlign.getQuantity()) {
    packed = true;
  }
  types.push_back(varTy);

  byrefType->setBody(types, packed);

  BlockByrefInfo info;
  info.Type = byrefType;
  info.FieldIndex = types.size() - 1;
  info.FieldOffset = varOffset;
  info.ByrefAlignment = std::max(varAlign, getPointerAlign());

  auto pair = BlockByrefInfos.insert({D, info});
  assert(pair.second && "info was inserted recursively?");
  return pair.first->second;
}

/// Initialize the structural components of a __block variable, i.e.
/// everything but the actual object.
void CodeGenFunction::emitByrefStructureInit(const AutoVarEmission &emission) {
  // Find the address of the local.
  Address addr = emission.Addr;

  // That's an alloca of the byref structure type.
  llvm::StructType *byrefType = cast<llvm::StructType>(
    cast<llvm::PointerType>(addr.getPointer()->getType())->getElementType());

  unsigned nextHeaderIndex = 0;
  CharUnits nextHeaderOffset;
  auto storeHeaderField = [&](llvm::Value *value, CharUnits fieldSize,
                              const Twine &name) {
    auto fieldAddr = Builder.CreateStructGEP(addr, nextHeaderIndex,
                                             nextHeaderOffset, name);
    Builder.CreateStore(value, fieldAddr);

    nextHeaderIndex++;
    nextHeaderOffset += fieldSize;
  };

  // Build the byref helpers if necessary.  This is null if we don't need any.
  BlockByrefHelpers *helpers = buildByrefHelpers(*byrefType, emission);

  const VarDecl &D = *emission.Variable;
  QualType type = D.getType();

  bool HasByrefExtendedLayout;
  Qualifiers::ObjCLifetime ByrefLifetime;
  bool ByRefHasLifetime =
    getContext().getByrefLifetime(type, ByrefLifetime, HasByrefExtendedLayout);

  llvm::Value *V;

  // Initialize the 'isa', which is just 0 or 1.
  int isa = 0;
  if (type.isObjCGCWeak())
    isa = 1;
  V = Builder.CreateIntToPtr(Builder.getInt32(isa), Int8PtrTy, "isa");
  storeHeaderField(V, getPointerSize(), "byref.isa");

  // Store the address of the variable into its own forwarding pointer.
  storeHeaderField(addr.getPointer(), getPointerSize(), "byref.forwarding");

  // Blocks ABI:
  //   c) the flags field is set to either 0 if no helper functions are
  //      needed or BLOCK_BYREF_HAS_COPY_DISPOSE if they are,
  BlockFlags flags;
  if (helpers) flags |= BLOCK_BYREF_HAS_COPY_DISPOSE;
  if (ByRefHasLifetime) {
    if (HasByrefExtendedLayout) flags |= BLOCK_BYREF_LAYOUT_EXTENDED;
      else switch (ByrefLifetime) {
        case Qualifiers::OCL_Strong:
          flags |= BLOCK_BYREF_LAYOUT_STRONG;
          break;
        case Qualifiers::OCL_Weak:
          flags |= BLOCK_BYREF_LAYOUT_WEAK;
          break;
        case Qualifiers::OCL_ExplicitNone:
          flags |= BLOCK_BYREF_LAYOUT_UNRETAINED;
          break;
        case Qualifiers::OCL_None:
          if (!type->isObjCObjectPointerType() && !type->isBlockPointerType())
            flags |= BLOCK_BYREF_LAYOUT_NON_OBJECT;
          break;
        default:
          break;
      }
    if (CGM.getLangOpts().ObjCGCBitmapPrint) {
      printf("\n Inline flag for BYREF variable layout (%d):", flags.getBitMask());
      if (flags & BLOCK_BYREF_HAS_COPY_DISPOSE)
        printf(" BLOCK_BYREF_HAS_COPY_DISPOSE");
      if (flags & BLOCK_BYREF_LAYOUT_MASK) {
        BlockFlags ThisFlag(flags.getBitMask() & BLOCK_BYREF_LAYOUT_MASK);
        if (ThisFlag ==  BLOCK_BYREF_LAYOUT_EXTENDED)
          printf(" BLOCK_BYREF_LAYOUT_EXTENDED");
        if (ThisFlag ==  BLOCK_BYREF_LAYOUT_STRONG)
          printf(" BLOCK_BYREF_LAYOUT_STRONG");
        if (ThisFlag == BLOCK_BYREF_LAYOUT_WEAK)
          printf(" BLOCK_BYREF_LAYOUT_WEAK");
        if (ThisFlag == BLOCK_BYREF_LAYOUT_UNRETAINED)
          printf(" BLOCK_BYREF_LAYOUT_UNRETAINED");
        if (ThisFlag == BLOCK_BYREF_LAYOUT_NON_OBJECT)
          printf(" BLOCK_BYREF_LAYOUT_NON_OBJECT");
      }
      printf("\n");
    }
  }
  storeHeaderField(llvm::ConstantInt::get(IntTy, flags.getBitMask()),
                   getIntSize(), "byref.flags");

  CharUnits byrefSize = CGM.GetTargetTypeStoreSize(byrefType);
  V = llvm::ConstantInt::get(IntTy, byrefSize.getQuantity());
  storeHeaderField(V, getIntSize(), "byref.size");

  if (helpers) {
    storeHeaderField(helpers->CopyHelper, getPointerSize(),
                     "byref.copyHelper");
    storeHeaderField(helpers->DisposeHelper, getPointerSize(),
                     "byref.disposeHelper");
  }

  if (ByRefHasLifetime && HasByrefExtendedLayout) {
    auto layoutInfo = CGM.getObjCRuntime().BuildByrefLayout(CGM, type);
    storeHeaderField(layoutInfo, getPointerSize(), "byref.layout");
  }
}

void CodeGenFunction::BuildBlockRelease(llvm::Value *V, BlockFieldFlags flags,
                                        bool CanThrow) {
  llvm::Value *F = CGM.getBlockObjectDispose();
  llvm::Value *args[] = {
    Builder.CreateBitCast(V, Int8PtrTy),
    llvm::ConstantInt::get(Int32Ty, flags.getBitMask())
  };

  if (CanThrow)
    EmitRuntimeCallOrInvoke(F, args);
  else
    EmitNounwindRuntimeCall(F, args);
}

void CodeGenFunction::enterByrefCleanup(CleanupKind Kind, Address Addr,
                                        BlockFieldFlags Flags,
                                        bool LoadBlockVarAddr, bool CanThrow) {
  EHStack.pushCleanup<CallBlockRelease>(Kind, Addr, Flags, LoadBlockVarAddr,
                                        CanThrow);
}

/// Adjust the declaration of something from the blocks API.
static void configureBlocksRuntimeObject(CodeGenModule &CGM,
                                         llvm::Constant *C) {
  auto *GV = cast<llvm::GlobalValue>(C->stripPointerCasts());

  if (CGM.getTarget().getTriple().isOSBinFormatCOFF()) {
    IdentifierInfo &II = CGM.getContext().Idents.get(C->getName());
    TranslationUnitDecl *TUDecl = CGM.getContext().getTranslationUnitDecl();
    DeclContext *DC = TranslationUnitDecl::castToDeclContext(TUDecl);

    assert((isa<llvm::Function>(C->stripPointerCasts()) ||
            isa<llvm::GlobalVariable>(C->stripPointerCasts())) &&
           "expected Function or GlobalVariable");

    const NamedDecl *ND = nullptr;
    for (const auto &Result : DC->lookup(&II))
      if ((ND = dyn_cast<FunctionDecl>(Result)) ||
          (ND = dyn_cast<VarDecl>(Result)))
        break;

    // TODO: support static blocks runtime
    if (GV->isDeclaration() && (!ND || !ND->hasAttr<DLLExportAttr>())) {
      GV->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
      GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
    } else {
      GV->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
      GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
    }
  }

  if (CGM.getLangOpts().BlocksRuntimeOptional && GV->isDeclaration() &&
      GV->hasExternalLinkage())
    GV->setLinkage(llvm::GlobalValue::ExternalWeakLinkage);

  CGM.setDSOLocal(GV);
}

llvm::Constant *CodeGenModule::getBlockObjectDispose() {
  if (BlockObjectDispose)
    return BlockObjectDispose;

  llvm::Type *args[] = { Int8PtrTy, Int32Ty };
  llvm::FunctionType *fty
    = llvm::FunctionType::get(VoidTy, args, false);
  BlockObjectDispose = CreateRuntimeFunction(fty, "_Block_object_dispose");
  configureBlocksRuntimeObject(*this, BlockObjectDispose);
  return BlockObjectDispose;
}

llvm::Constant *CodeGenModule::getBlockObjectAssign() {
  if (BlockObjectAssign)
    return BlockObjectAssign;

  llvm::Type *args[] = { Int8PtrTy, Int8PtrTy, Int32Ty };
  llvm::FunctionType *fty
    = llvm::FunctionType::get(VoidTy, args, false);
  BlockObjectAssign = CreateRuntimeFunction(fty, "_Block_object_assign");
  configureBlocksRuntimeObject(*this, BlockObjectAssign);
  return BlockObjectAssign;
}

llvm::Constant *CodeGenModule::getNSConcreteGlobalBlock() {
  if (NSConcreteGlobalBlock)
    return NSConcreteGlobalBlock;

  NSConcreteGlobalBlock = GetOrCreateLLVMGlobal("_NSConcreteGlobalBlock",
                                                Int8PtrTy->getPointerTo(),
                                                nullptr);
  configureBlocksRuntimeObject(*this, NSConcreteGlobalBlock);
  return NSConcreteGlobalBlock;
}

llvm::Constant *CodeGenModule::getNSConcreteStackBlock() {
  if (NSConcreteStackBlock)
    return NSConcreteStackBlock;

  NSConcreteStackBlock = GetOrCreateLLVMGlobal("_NSConcreteStackBlock",
                                               Int8PtrTy->getPointerTo(),
                                               nullptr);
  configureBlocksRuntimeObject(*this, NSConcreteStackBlock);
  return NSConcreteStackBlock;
}
