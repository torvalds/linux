//===-- llvm/Instrinsics.h - LLVM Intrinsic Function Handling ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a set of enums which allow processing of intrinsic
// functions.  Values of these enum types are returned by
// Function::getIntrinsicID.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INTRINSICS_H
#define LLVM_IR_INTRINSICS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include <string>

namespace llvm {

class Type;
class FunctionType;
class Function;
class LLVMContext;
class Module;
class AttributeList;

/// This namespace contains an enum with a value for every intrinsic/builtin
/// function known by LLVM. The enum values are returned by
/// Function::getIntrinsicID().
namespace Intrinsic {
  enum ID : unsigned {
    not_intrinsic = 0,   // Must be zero

    // Get the intrinsic enums generated from Intrinsics.td
#define GET_INTRINSIC_ENUM_VALUES
#include "llvm/IR/IntrinsicEnums.inc"
#undef GET_INTRINSIC_ENUM_VALUES
    , num_intrinsics
  };

  /// Return the LLVM name for an intrinsic, such as "llvm.ppc.altivec.lvx".
  /// Note, this version is for intrinsics with no overloads.  Use the other
  /// version of getName if overloads are required.
  StringRef getName(ID id);

  /// Return the LLVM name for an intrinsic, such as "llvm.ppc.altivec.lvx".
  /// Note, this version of getName supports overloads, but is less efficient
  /// than the StringRef version of this function.  If no overloads are
  /// requried, it is safe to use this version, but better to use the StringRef
  /// version.
  std::string getName(ID id, ArrayRef<Type*> Tys);

  /// Return the function type for an intrinsic.
  FunctionType *getType(LLVMContext &Context, ID id,
                        ArrayRef<Type*> Tys = None);

  /// Returns true if the intrinsic can be overloaded.
  bool isOverloaded(ID id);

  /// Returns true if the intrinsic is a leaf, i.e. it does not make any calls
  /// itself.  Most intrinsics are leafs, the exceptions being the patchpoint
  /// and statepoint intrinsics. These call (or invoke) their "target" argument.
  bool isLeaf(ID id);

  /// Return the attributes for an intrinsic.
  AttributeList getAttributes(LLVMContext &C, ID id);

  /// Create or insert an LLVM Function declaration for an intrinsic, and return
  /// it.
  ///
  /// The Tys parameter is for intrinsics with overloaded types (e.g., those
  /// using iAny, fAny, vAny, or iPTRAny).  For a declaration of an overloaded
  /// intrinsic, Tys must provide exactly one type for each overloaded type in
  /// the intrinsic.
  Function *getDeclaration(Module *M, ID id, ArrayRef<Type*> Tys = None);

  /// Looks up Name in NameTable via binary search. NameTable must be sorted
  /// and all entries must start with "llvm.".  If NameTable contains an exact
  /// match for Name or a prefix of Name followed by a dot, its index in
  /// NameTable is returned. Otherwise, -1 is returned.
  int lookupLLVMIntrinsicByName(ArrayRef<const char *> NameTable,
                                StringRef Name);

  /// Map a GCC builtin name to an intrinsic ID.
  ID getIntrinsicForGCCBuiltin(const char *Prefix, StringRef BuiltinName);

  /// Map a MS builtin name to an intrinsic ID.
  ID getIntrinsicForMSBuiltin(const char *Prefix, StringRef BuiltinName);

  /// This is a type descriptor which explains the type requirements of an
  /// intrinsic. This is returned by getIntrinsicInfoTableEntries.
  struct IITDescriptor {
    enum IITDescriptorKind {
      Void, VarArg, MMX, Token, Metadata, Half, Float, Double, Quad,
      Integer, Vector, Pointer, Struct,
      Argument, ExtendArgument, TruncArgument, HalfVecArgument,
      SameVecWidthArgument, PtrToArgument, PtrToElt, VecOfAnyPtrsToElt
    } Kind;

    union {
      unsigned Integer_Width;
      unsigned Float_Width;
      unsigned Vector_Width;
      unsigned Pointer_AddressSpace;
      unsigned Struct_NumElements;
      unsigned Argument_Info;
    };

    enum ArgKind {
      AK_Any,
      AK_AnyInteger,
      AK_AnyFloat,
      AK_AnyVector,
      AK_AnyPointer
    };

    unsigned getArgumentNumber() const {
      assert(Kind == Argument || Kind == ExtendArgument ||
             Kind == TruncArgument || Kind == HalfVecArgument ||
             Kind == SameVecWidthArgument || Kind == PtrToArgument ||
             Kind == PtrToElt);
      return Argument_Info >> 3;
    }
    ArgKind getArgumentKind() const {
      assert(Kind == Argument || Kind == ExtendArgument ||
             Kind == TruncArgument || Kind == HalfVecArgument ||
             Kind == SameVecWidthArgument || Kind == PtrToArgument);
      return (ArgKind)(Argument_Info & 7);
    }

    // VecOfAnyPtrsToElt uses both an overloaded argument (for address space)
    // and a reference argument (for matching vector width and element types)
    unsigned getOverloadArgNumber() const {
      assert(Kind == VecOfAnyPtrsToElt);
      return Argument_Info >> 16;
    }
    unsigned getRefArgNumber() const {
      assert(Kind == VecOfAnyPtrsToElt);
      return Argument_Info & 0xFFFF;
    }

    static IITDescriptor get(IITDescriptorKind K, unsigned Field) {
      IITDescriptor Result = { K, { Field } };
      return Result;
    }

    static IITDescriptor get(IITDescriptorKind K, unsigned short Hi,
                             unsigned short Lo) {
      unsigned Field = Hi << 16 | Lo;
      IITDescriptor Result = {K, {Field}};
      return Result;
    }
  };

  /// Return the IIT table descriptor for the specified intrinsic into an array
  /// of IITDescriptors.
  void getIntrinsicInfoTableEntries(ID id, SmallVectorImpl<IITDescriptor> &T);

  /// Match the specified type (which comes from an intrinsic argument or return
  /// value) with the type constraints specified by the .td file. If the given
  /// type is an overloaded type it is pushed to the ArgTys vector.
  ///
  /// Returns false if the given type matches with the constraints, true
  /// otherwise.
  bool matchIntrinsicType(Type *Ty, ArrayRef<IITDescriptor> &Infos,
                          SmallVectorImpl<Type*> &ArgTys);

  /// Verify if the intrinsic has variable arguments. This method is intended to
  /// be called after all the fixed arguments have been matched first.
  ///
  /// This method returns true on error.
  bool matchIntrinsicVarArg(bool isVarArg, ArrayRef<IITDescriptor> &Infos);

  // Checks if the intrinsic name matches with its signature and if not
  // returns the declaration with the same signature and remangled name.
  llvm::Optional<Function*> remangleIntrinsicFunction(Function *F);

} // End Intrinsic namespace

} // End llvm namespace

#endif
