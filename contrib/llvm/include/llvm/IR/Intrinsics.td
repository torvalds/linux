//===- Intrinsics.td - Defines all LLVM intrinsics ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines properties of all LLVM intrinsics.
//
//===----------------------------------------------------------------------===//

include "llvm/CodeGen/ValueTypes.td"
include "llvm/CodeGen/SDNodeProperties.td"

//===----------------------------------------------------------------------===//
//  Properties we keep track of for intrinsics.
//===----------------------------------------------------------------------===//

class IntrinsicProperty;

// Intr*Mem - Memory properties.  If no property is set, the worst case
// is assumed (it may read and write any memory it can get access to and it may
// have other side effects).

// IntrNoMem - The intrinsic does not access memory or have any other side
// effects.  It may be CSE'd deleted if dead, etc.
def IntrNoMem : IntrinsicProperty;

// IntrReadMem - This intrinsic only reads from memory. It does not write to
// memory and has no other side effects. Therefore, it cannot be moved across
// potentially aliasing stores. However, it can be reordered otherwise and can
// be deleted if dead.
def IntrReadMem : IntrinsicProperty;

// IntrWriteMem - This intrinsic only writes to memory, but does not read from
// memory, and has no other side effects. This means dead stores before calls
// to this intrinsics may be removed.
def IntrWriteMem : IntrinsicProperty;

// IntrArgMemOnly - This intrinsic only accesses memory that its pointer-typed
// argument(s) points to, but may access an unspecified amount. Other than
// reads from and (possibly volatile) writes to memory, it has no side effects.
def IntrArgMemOnly : IntrinsicProperty;

// IntrInaccessibleMemOnly -- This intrinsic only accesses memory that is not
// accessible by the module being compiled. This is a weaker form of IntrNoMem.
def IntrInaccessibleMemOnly : IntrinsicProperty;

// IntrInaccessibleMemOrArgMemOnly -- This intrinsic only accesses memory that
// its pointer-typed arguments point to or memory that is not accessible
// by the module being compiled. This is a weaker form of IntrArgMemOnly.
def IntrInaccessibleMemOrArgMemOnly : IntrinsicProperty;

// Commutative - This intrinsic is commutative: X op Y == Y op X.
def Commutative : IntrinsicProperty;

// Throws - This intrinsic can throw.
def Throws : IntrinsicProperty;

// NoCapture - The specified argument pointer is not captured by the intrinsic.
class NoCapture<int argNo> : IntrinsicProperty {
  int ArgNo = argNo;
}

// Returned - The specified argument is always the return value of the
// intrinsic.
class Returned<int argNo> : IntrinsicProperty {
  int ArgNo = argNo;
}

// ReadOnly - The specified argument pointer is not written to through the
// pointer by the intrinsic.
class ReadOnly<int argNo> : IntrinsicProperty {
  int ArgNo = argNo;
}

// WriteOnly - The intrinsic does not read memory through the specified
// argument pointer.
class WriteOnly<int argNo> : IntrinsicProperty {
  int ArgNo = argNo;
}

// ReadNone - The specified argument pointer is not dereferenced by the
// intrinsic.
class ReadNone<int argNo> : IntrinsicProperty {
  int ArgNo = argNo;
}

def IntrNoReturn : IntrinsicProperty;

// IntrCold - Calls to this intrinsic are cold.
// Parallels the cold attribute on LLVM IR functions.
def IntrCold : IntrinsicProperty;

// IntrNoduplicate - Calls to this intrinsic cannot be duplicated.
// Parallels the noduplicate attribute on LLVM IR functions.
def IntrNoDuplicate : IntrinsicProperty;

// IntrConvergent - Calls to this intrinsic are convergent and may not be made
// control-dependent on any additional values.
// Parallels the convergent attribute on LLVM IR functions.
def IntrConvergent : IntrinsicProperty;

// This property indicates that the intrinsic is safe to speculate.
def IntrSpeculatable : IntrinsicProperty;

// This property can be used to override the 'has no other side effects'
// language of the IntrNoMem, IntrReadMem, IntrWriteMem, and IntrArgMemOnly
// intrinsic properties.  By default, intrinsics are assumed to have side
// effects, so this property is only necessary if you have defined one of
// the memory properties listed above.
// For this property, 'side effects' has the same meaning as 'side effects'
// defined by the hasSideEffects property of the TableGen Instruction class.
def IntrHasSideEffects : IntrinsicProperty;

//===----------------------------------------------------------------------===//
// Types used by intrinsics.
//===----------------------------------------------------------------------===//

class LLVMType<ValueType vt> {
  ValueType VT = vt;
  int isAny = 0;
}

class LLVMQualPointerType<LLVMType elty, int addrspace>
  : LLVMType<iPTR>{
  LLVMType ElTy = elty;
  int AddrSpace = addrspace;
}

class LLVMPointerType<LLVMType elty>
  : LLVMQualPointerType<elty, 0>;

class LLVMAnyPointerType<LLVMType elty>
  : LLVMType<iPTRAny>{
  LLVMType ElTy = elty;

  let isAny = 1;
}

// Match the type of another intrinsic parameter.  Number is an index into the
// list of overloaded types for the intrinsic, excluding all the fixed types.
// The Number value must refer to a previously listed type.  For example:
//   Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_anyfloat_ty, LLVMMatchType<0>]>
// has two overloaded types, the 2nd and 3rd arguments.  LLVMMatchType<0>
// refers to the first overloaded type, which is the 2nd argument.
class LLVMMatchType<int num>
  : LLVMType<OtherVT>{
  int Number = num;
}

// Match the type of another intrinsic parameter that is expected to be based on
// an integral type (i.e. either iN or <N x iM>), but change the scalar size to
// be twice as wide or half as wide as the other type.  This is only useful when
// the intrinsic is overloaded, so the matched type should be declared as iAny.
class LLVMExtendedType<int num> : LLVMMatchType<num>;
class LLVMTruncatedType<int num> : LLVMMatchType<num>;
class LLVMVectorSameWidth<int num, LLVMType elty>
  : LLVMMatchType<num> {
  ValueType ElTy = elty.VT;
}
class LLVMPointerTo<int num> : LLVMMatchType<num>;
class LLVMPointerToElt<int num> : LLVMMatchType<num>;
class LLVMVectorOfAnyPointersToElt<int num> : LLVMMatchType<num>;

// Match the type of another intrinsic parameter that is expected to be a
// vector type, but change the element count to be half as many
class LLVMHalfElementsVectorType<int num> : LLVMMatchType<num>;

def llvm_void_ty       : LLVMType<isVoid>;
let isAny = 1 in {
  def llvm_any_ty        : LLVMType<Any>;
  def llvm_anyint_ty     : LLVMType<iAny>;
  def llvm_anyfloat_ty   : LLVMType<fAny>;
  def llvm_anyvector_ty  : LLVMType<vAny>;
}
def llvm_i1_ty         : LLVMType<i1>;
def llvm_i8_ty         : LLVMType<i8>;
def llvm_i16_ty        : LLVMType<i16>;
def llvm_i32_ty        : LLVMType<i32>;
def llvm_i64_ty        : LLVMType<i64>;
def llvm_half_ty       : LLVMType<f16>;
def llvm_float_ty      : LLVMType<f32>;
def llvm_double_ty     : LLVMType<f64>;
def llvm_f80_ty        : LLVMType<f80>;
def llvm_f128_ty       : LLVMType<f128>;
def llvm_ppcf128_ty    : LLVMType<ppcf128>;
def llvm_ptr_ty        : LLVMPointerType<llvm_i8_ty>;             // i8*
def llvm_ptrptr_ty     : LLVMPointerType<llvm_ptr_ty>;            // i8**
def llvm_anyptr_ty     : LLVMAnyPointerType<llvm_i8_ty>;          // (space)i8*
def llvm_empty_ty      : LLVMType<OtherVT>;                       // { }
def llvm_descriptor_ty : LLVMPointerType<llvm_empty_ty>;          // { }*
def llvm_metadata_ty   : LLVMType<MetadataVT>;                    // !{...}
def llvm_token_ty      : LLVMType<token>;                         // token

def llvm_x86mmx_ty     : LLVMType<x86mmx>;
def llvm_ptrx86mmx_ty  : LLVMPointerType<llvm_x86mmx_ty>;         // <1 x i64>*

def llvm_v2i1_ty       : LLVMType<v2i1>;     //   2 x i1
def llvm_v4i1_ty       : LLVMType<v4i1>;     //   4 x i1
def llvm_v8i1_ty       : LLVMType<v8i1>;     //   8 x i1
def llvm_v16i1_ty      : LLVMType<v16i1>;    //  16 x i1
def llvm_v32i1_ty      : LLVMType<v32i1>;    //  32 x i1
def llvm_v64i1_ty      : LLVMType<v64i1>;    //  64 x i1
def llvm_v512i1_ty     : LLVMType<v512i1>;   // 512 x i1
def llvm_v1024i1_ty    : LLVMType<v1024i1>;  //1024 x i1

def llvm_v1i8_ty       : LLVMType<v1i8>;     //  1 x i8
def llvm_v2i8_ty       : LLVMType<v2i8>;     //  2 x i8
def llvm_v4i8_ty       : LLVMType<v4i8>;     //  4 x i8
def llvm_v8i8_ty       : LLVMType<v8i8>;     //  8 x i8
def llvm_v16i8_ty      : LLVMType<v16i8>;    // 16 x i8
def llvm_v32i8_ty      : LLVMType<v32i8>;    // 32 x i8
def llvm_v64i8_ty      : LLVMType<v64i8>;    // 64 x i8
def llvm_v128i8_ty     : LLVMType<v128i8>;   //128 x i8
def llvm_v256i8_ty     : LLVMType<v256i8>;   //256 x i8

def llvm_v1i16_ty      : LLVMType<v1i16>;    //  1 x i16
def llvm_v2i16_ty      : LLVMType<v2i16>;    //  2 x i16
def llvm_v4i16_ty      : LLVMType<v4i16>;    //  4 x i16
def llvm_v8i16_ty      : LLVMType<v8i16>;    //  8 x i16
def llvm_v16i16_ty     : LLVMType<v16i16>;   // 16 x i16
def llvm_v32i16_ty     : LLVMType<v32i16>;   // 32 x i16
def llvm_v64i16_ty     : LLVMType<v64i16>;   // 64 x i16
def llvm_v128i16_ty    : LLVMType<v128i16>;  //128 x i16

def llvm_v1i32_ty      : LLVMType<v1i32>;    //  1 x i32
def llvm_v2i32_ty      : LLVMType<v2i32>;    //  2 x i32
def llvm_v4i32_ty      : LLVMType<v4i32>;    //  4 x i32
def llvm_v8i32_ty      : LLVMType<v8i32>;    //  8 x i32
def llvm_v16i32_ty     : LLVMType<v16i32>;   // 16 x i32
def llvm_v32i32_ty     : LLVMType<v32i32>;   // 32 x i32
def llvm_v64i32_ty     : LLVMType<v64i32>;   // 64 x i32

def llvm_v1i64_ty      : LLVMType<v1i64>;    //  1 x i64
def llvm_v2i64_ty      : LLVMType<v2i64>;    //  2 x i64
def llvm_v4i64_ty      : LLVMType<v4i64>;    //  4 x i64
def llvm_v8i64_ty      : LLVMType<v8i64>;    //  8 x i64
def llvm_v16i64_ty     : LLVMType<v16i64>;   // 16 x i64
def llvm_v32i64_ty     : LLVMType<v32i64>;   // 32 x i64

def llvm_v1i128_ty     : LLVMType<v1i128>;   //  1 x i128

def llvm_v2f16_ty      : LLVMType<v2f16>;    //  2 x half (__fp16)
def llvm_v4f16_ty      : LLVMType<v4f16>;    //  4 x half (__fp16)
def llvm_v8f16_ty      : LLVMType<v8f16>;    //  8 x half (__fp16)
def llvm_v1f32_ty      : LLVMType<v1f32>;    //  1 x float
def llvm_v2f32_ty      : LLVMType<v2f32>;    //  2 x float
def llvm_v4f32_ty      : LLVMType<v4f32>;    //  4 x float
def llvm_v8f32_ty      : LLVMType<v8f32>;    //  8 x float
def llvm_v16f32_ty     : LLVMType<v16f32>;   // 16 x float
def llvm_v1f64_ty      : LLVMType<v1f64>;    //  1 x double
def llvm_v2f64_ty      : LLVMType<v2f64>;    //  2 x double
def llvm_v4f64_ty      : LLVMType<v4f64>;    //  4 x double
def llvm_v8f64_ty      : LLVMType<v8f64>;    //  8 x double

def llvm_vararg_ty     : LLVMType<isVoid>;   // this means vararg here

//===----------------------------------------------------------------------===//
// Intrinsic Definitions.
//===----------------------------------------------------------------------===//

// Intrinsic class - This is used to define one LLVM intrinsic.  The name of the
// intrinsic definition should start with "int_", then match the LLVM intrinsic
// name with the "llvm." prefix removed, and all "."s turned into "_"s.  For
// example, llvm.bswap.i16 -> int_bswap_i16.
//
//  * RetTypes is a list containing the return types expected for the
//    intrinsic.
//  * ParamTypes is a list containing the parameter types expected for the
//    intrinsic.
//  * Properties can be set to describe the behavior of the intrinsic.
//
class Intrinsic<list<LLVMType> ret_types,
                list<LLVMType> param_types = [],
                list<IntrinsicProperty> intr_properties = [],
                string name = "",
                list<SDNodeProperty> sd_properties = []> : SDPatternOperator {
  string LLVMName = name;
  string TargetPrefix = "";   // Set to a prefix for target-specific intrinsics.
  list<LLVMType> RetTypes = ret_types;
  list<LLVMType> ParamTypes = param_types;
  list<IntrinsicProperty> IntrProperties = intr_properties;
  let Properties = sd_properties;

  bit isTarget = 0;
}

/// GCCBuiltin - If this intrinsic exactly corresponds to a GCC builtin, this
/// specifies the name of the builtin.  This provides automatic CBE and CFE
/// support.
class GCCBuiltin<string name> {
  string GCCBuiltinName = name;
}

class MSBuiltin<string name> {
  string MSBuiltinName = name;
}


//===--------------- Variable Argument Handling Intrinsics ----------------===//
//

def int_vastart : Intrinsic<[], [llvm_ptr_ty], [], "llvm.va_start">;
def int_vacopy  : Intrinsic<[], [llvm_ptr_ty, llvm_ptr_ty], [],
                            "llvm.va_copy">;
def int_vaend   : Intrinsic<[], [llvm_ptr_ty], [], "llvm.va_end">;

//===------------------- Garbage Collection Intrinsics --------------------===//
//
def int_gcroot  : Intrinsic<[],
                            [llvm_ptrptr_ty, llvm_ptr_ty]>;
def int_gcread  : Intrinsic<[llvm_ptr_ty],
                            [llvm_ptr_ty, llvm_ptrptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_gcwrite : Intrinsic<[],
                            [llvm_ptr_ty, llvm_ptr_ty, llvm_ptrptr_ty],
                            [IntrArgMemOnly, NoCapture<1>, NoCapture<2>]>;

//===------------------- ObjC ARC runtime Intrinsics --------------------===//
//
// Note these are to support the Objective-C ARC optimizer which wants to
// eliminate retain and releases where possible.

def int_objc_autorelease                    : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_autoreleasePoolPop             : Intrinsic<[], [llvm_ptr_ty]>;
def int_objc_autoreleasePoolPush            : Intrinsic<[llvm_ptr_ty], []>;
def int_objc_autoreleaseReturnValue         : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_copyWeak                       : Intrinsic<[],
                                                        [llvm_ptrptr_ty,
                                                         llvm_ptrptr_ty]>;
def int_objc_destroyWeak                    : Intrinsic<[], [llvm_ptrptr_ty]>;
def int_objc_initWeak                       : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptrptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_loadWeak                       : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptrptr_ty]>;
def int_objc_loadWeakRetained               : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptrptr_ty]>;
def int_objc_moveWeak                       : Intrinsic<[],
                                                        [llvm_ptrptr_ty,
                                                         llvm_ptrptr_ty]>;
def int_objc_release                        : Intrinsic<[], [llvm_ptr_ty]>;
def int_objc_retain                         : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_retainAutorelease              : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_retainAutoreleaseReturnValue   : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_retainAutoreleasedReturnValue  : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_retainBlock                    : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_storeStrong                    : Intrinsic<[],
                                                        [llvm_ptrptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_storeWeak                      : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptrptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_clang_arc_use                  : Intrinsic<[],
                                                        [llvm_vararg_ty]>;
def int_objc_unsafeClaimAutoreleasedReturnValue : Intrinsic<[llvm_ptr_ty],
                                                            [llvm_ptr_ty]>;
def int_objc_retainedObject                 : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_unretainedObject               : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_unretainedPointer              : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_retain_autorelease             : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_sync_enter                     : Intrinsic<[llvm_i32_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_sync_exit                      : Intrinsic<[llvm_i32_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_arc_annotation_topdown_bbstart : Intrinsic<[],
                                                        [llvm_ptrptr_ty,
                                                         llvm_ptrptr_ty]>;
def int_objc_arc_annotation_topdown_bbend   : Intrinsic<[],
                                                        [llvm_ptrptr_ty,
                                                         llvm_ptrptr_ty]>;
def int_objc_arc_annotation_bottomup_bbstart  : Intrinsic<[],
                                                          [llvm_ptrptr_ty,
                                                           llvm_ptrptr_ty]>;
def int_objc_arc_annotation_bottomup_bbend  : Intrinsic<[],
                                                        [llvm_ptrptr_ty,
                                                         llvm_ptrptr_ty]>;


//===--------------------- Code Generator Intrinsics ----------------------===//
//
def int_returnaddress : Intrinsic<[llvm_ptr_ty], [llvm_i32_ty], [IntrNoMem]>;
def int_addressofreturnaddress : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;
def int_frameaddress  : Intrinsic<[llvm_ptr_ty], [llvm_i32_ty], [IntrNoMem]>;
def int_sponentry  : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;
def int_read_register  : Intrinsic<[llvm_anyint_ty], [llvm_metadata_ty],
                                   [IntrReadMem], "llvm.read_register">;
def int_write_register : Intrinsic<[], [llvm_metadata_ty, llvm_anyint_ty],
                                   [], "llvm.write_register">;

// Gets the address of the local variable area. This is typically a copy of the
// stack, frame, or base pointer depending on the type of prologue.
def int_localaddress : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;

// Escapes local variables to allow access from other functions.
def int_localescape : Intrinsic<[], [llvm_vararg_ty]>;

// Given a function and the localaddress of a parent frame, returns a pointer
// to an escaped allocation indicated by the index.
def int_localrecover : Intrinsic<[llvm_ptr_ty],
                                 [llvm_ptr_ty, llvm_ptr_ty, llvm_i32_ty],
                                 [IntrNoMem]>;

// Given the frame pointer passed into an SEH filter function, returns a
// pointer to the local variable area suitable for use with llvm.localrecover.
def int_eh_recoverfp : Intrinsic<[llvm_ptr_ty],
                                 [llvm_ptr_ty, llvm_ptr_ty],
                                 [IntrNoMem]>;

// Note: we treat stacksave/stackrestore as writemem because we don't otherwise
// model their dependencies on allocas.
def int_stacksave     : Intrinsic<[llvm_ptr_ty]>,
                        GCCBuiltin<"__builtin_stack_save">;
def int_stackrestore  : Intrinsic<[], [llvm_ptr_ty]>,
                        GCCBuiltin<"__builtin_stack_restore">;

def int_get_dynamic_area_offset : Intrinsic<[llvm_anyint_ty]>;

def int_thread_pointer : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>,
                         GCCBuiltin<"__builtin_thread_pointer">;

// IntrInaccessibleMemOrArgMemOnly is a little more pessimistic than strictly
// necessary for prefetch, however it does conveniently prevent the prefetch
// from being reordered overly much with respect to nearby access to the same
// memory while not impeding optimization.
def int_prefetch
    : Intrinsic<[], [ llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty ],
                [ IntrInaccessibleMemOrArgMemOnly, ReadOnly<0>, NoCapture<0> ]>;
def int_pcmarker      : Intrinsic<[], [llvm_i32_ty]>;

def int_readcyclecounter : Intrinsic<[llvm_i64_ty]>;

// The assume intrinsic is marked as arbitrarily writing so that proper
// control dependencies will be maintained.
def int_assume        : Intrinsic<[], [llvm_i1_ty], []>;

// Stack Protector Intrinsic - The stackprotector intrinsic writes the stack
// guard to the correct place on the stack frame.
def int_stackprotector : Intrinsic<[], [llvm_ptr_ty, llvm_ptrptr_ty], []>;
def int_stackguard : Intrinsic<[llvm_ptr_ty], [], []>;

// A counter increment for instrumentation based profiling.
def int_instrprof_increment : Intrinsic<[],
                                        [llvm_ptr_ty, llvm_i64_ty,
                                         llvm_i32_ty, llvm_i32_ty],
                                        []>;

// A counter increment with step for instrumentation based profiling.
def int_instrprof_increment_step : Intrinsic<[],
                                        [llvm_ptr_ty, llvm_i64_ty,
                                         llvm_i32_ty, llvm_i32_ty, llvm_i64_ty],
                                        []>;

// A call to profile runtime for value profiling of target expressions
// through instrumentation based profiling.
def int_instrprof_value_profile : Intrinsic<[],
                                            [llvm_ptr_ty, llvm_i64_ty,
                                             llvm_i64_ty, llvm_i32_ty,
                                             llvm_i32_ty],
                                            []>;

//===------------------- Standard C Library Intrinsics --------------------===//
//

def int_memcpy  : Intrinsic<[],
                             [llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty,
                              llvm_i1_ty],
                            [IntrArgMemOnly, NoCapture<0>, NoCapture<1>,
                             WriteOnly<0>, ReadOnly<1>]>;
def int_memmove : Intrinsic<[],
                            [llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty,
                             llvm_i1_ty],
                            [IntrArgMemOnly, NoCapture<0>, NoCapture<1>,
                             ReadOnly<1>]>;
def int_memset  : Intrinsic<[],
                            [llvm_anyptr_ty, llvm_i8_ty, llvm_anyint_ty,
                             llvm_i1_ty],
                            [IntrArgMemOnly, NoCapture<0>, WriteOnly<0>]>;

// FIXME: Add version of these floating point intrinsics which allow non-default
// rounding modes and FP exception handling.

let IntrProperties = [IntrNoMem, IntrSpeculatable] in {
  def int_fma  : Intrinsic<[llvm_anyfloat_ty],
                           [LLVMMatchType<0>, LLVMMatchType<0>,
                            LLVMMatchType<0>]>;
  def int_fmuladd : Intrinsic<[llvm_anyfloat_ty],
                              [LLVMMatchType<0>, LLVMMatchType<0>,
                               LLVMMatchType<0>]>;

  // These functions do not read memory, but are sensitive to the
  // rounding mode. LLVM purposely does not model changes to the FP
  // environment so they can be treated as readnone.
  def int_sqrt : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_powi : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>, llvm_i32_ty]>;
  def int_sin  : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_cos  : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_pow  : Intrinsic<[llvm_anyfloat_ty],
                           [LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_log  : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_log10: Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_log2 : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_exp  : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_exp2 : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_fabs : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_copysign : Intrinsic<[llvm_anyfloat_ty],
                               [LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_floor : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_ceil  : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_trunc : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_rint  : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_nearbyint : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_round : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_canonicalize : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>],
                                   [IntrNoMem]>;
}

def int_minnum : Intrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable, Commutative]
>;
def int_maxnum : Intrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable, Commutative]
>;
def int_minimum : Intrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable, Commutative]
>;
def int_maximum : Intrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable, Commutative]
>;

// NOTE: these are internal interfaces.
def int_setjmp     : Intrinsic<[llvm_i32_ty],  [llvm_ptr_ty]>;
def int_longjmp    : Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty], [IntrNoReturn]>;
def int_sigsetjmp  : Intrinsic<[llvm_i32_ty] , [llvm_ptr_ty, llvm_i32_ty]>;
def int_siglongjmp : Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty], [IntrNoReturn]>;

// Internal interface for object size checking
def int_objectsize : Intrinsic<[llvm_anyint_ty],
                               [llvm_anyptr_ty, llvm_i1_ty, llvm_i1_ty],
                               [IntrNoMem, IntrSpeculatable]>,
                               GCCBuiltin<"__builtin_object_size">;

//===--------------- Constrained Floating Point Intrinsics ----------------===//
//

let IntrProperties = [IntrInaccessibleMemOnly] in {
  def int_experimental_constrained_fadd : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_fsub : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_fmul : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_fdiv : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_frem : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;

  def int_experimental_constrained_fma : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;

  // These intrinsics are sensitive to the rounding mode so we need constrained
  // versions of each of them.  When strict rounding and exception control are
  // not required the non-constrained versions of these intrinsics should be
  // used.
  def int_experimental_constrained_sqrt : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_powi : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_i32_ty,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_sin  : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_cos  : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_pow  : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_log  : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_log10: Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_log2 : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_exp  : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_exp2 : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_rint  : Intrinsic<[ llvm_anyfloat_ty ],
                                                     [ LLVMMatchType<0>,
                                                       llvm_metadata_ty,
                                                       llvm_metadata_ty ]>;
  def int_experimental_constrained_nearbyint : Intrinsic<[ llvm_anyfloat_ty ],
                                                         [ LLVMMatchType<0>,
                                                           llvm_metadata_ty,
                                                           llvm_metadata_ty ]>;
  def int_experimental_constrained_maxnum : Intrinsic<[ llvm_anyfloat_ty ],
                                                      [ LLVMMatchType<0>,
                                                        LLVMMatchType<0>,
                                                        llvm_metadata_ty,
                                                        llvm_metadata_ty ]>;
  def int_experimental_constrained_minnum : Intrinsic<[ llvm_anyfloat_ty ],
                                                      [ LLVMMatchType<0>,
                                                        LLVMMatchType<0>,
                                                        llvm_metadata_ty,
                                                        llvm_metadata_ty ]>;
  def int_experimental_constrained_ceil : Intrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_floor : Intrinsic<[ llvm_anyfloat_ty ],
                                                     [ LLVMMatchType<0>,
                                                       llvm_metadata_ty,
                                                       llvm_metadata_ty ]>;
  def int_experimental_constrained_round : Intrinsic<[ llvm_anyfloat_ty ],
                                                     [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_trunc : Intrinsic<[ llvm_anyfloat_ty ],
                                                     [ LLVMMatchType<0>,
                                                       llvm_metadata_ty,
                                                       llvm_metadata_ty ]>;
}
// FIXME: Add intrinsics for fcmp, fptrunc, fpext, fptoui and fptosi.
// FIXME: Add intrinsics for fabs and copysign? 


//===------------------------- Expect Intrinsics --------------------------===//
//
def int_expect : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>,
                                              LLVMMatchType<0>], [IntrNoMem]>;

//===-------------------- Bit Manipulation Intrinsics ---------------------===//
//

// None of these intrinsics accesses memory at all.
let IntrProperties = [IntrNoMem, IntrSpeculatable] in {
  def int_bswap: Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>]>;
  def int_ctpop: Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>]>;
  def int_ctlz : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, llvm_i1_ty]>;
  def int_cttz : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, llvm_i1_ty]>;
  def int_bitreverse : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>]>;
  def int_fshl : Intrinsic<[llvm_anyint_ty],
      [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_fshr : Intrinsic<[llvm_anyint_ty],
      [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>]>;
}

//===------------------------ Debugger Intrinsics -------------------------===//
//

// None of these intrinsics accesses memory at all...but that doesn't
// mean the optimizers can change them aggressively.  Special handling
// needed in a few places. These synthetic intrinsics have no
// side-effects and just mark information about their operands.
let IntrProperties = [IntrNoMem, IntrSpeculatable] in {
  def int_dbg_declare      : Intrinsic<[],
                                       [llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty]>;
  def int_dbg_value        : Intrinsic<[],
                                       [llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty]>;
  def int_dbg_addr         : Intrinsic<[],
                                       [llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty]>;
  def int_dbg_label        : Intrinsic<[],
                                       [llvm_metadata_ty]>;
}

//===------------------ Exception Handling Intrinsics----------------------===//
//

// The result of eh.typeid.for depends on the enclosing function, but inside a
// given function it is 'const' and may be CSE'd etc.
def int_eh_typeid_for : Intrinsic<[llvm_i32_ty], [llvm_ptr_ty], [IntrNoMem]>;

def int_eh_return_i32 : Intrinsic<[], [llvm_i32_ty, llvm_ptr_ty]>;
def int_eh_return_i64 : Intrinsic<[], [llvm_i64_ty, llvm_ptr_ty]>;

// eh.exceptionpointer returns the pointer to the exception caught by
// the given `catchpad`.
def int_eh_exceptionpointer : Intrinsic<[llvm_anyptr_ty], [llvm_token_ty],
                                        [IntrNoMem]>;

// Gets the exception code from a catchpad token. Only used on some platforms.
def int_eh_exceptioncode : Intrinsic<[llvm_i32_ty], [llvm_token_ty], [IntrNoMem]>;

// __builtin_unwind_init is an undocumented GCC intrinsic that causes all
// callee-saved registers to be saved and restored (regardless of whether they
// are used) in the calling function. It is used by libgcc_eh.
def int_eh_unwind_init: Intrinsic<[]>,
                        GCCBuiltin<"__builtin_unwind_init">;

def int_eh_dwarf_cfa  : Intrinsic<[llvm_ptr_ty], [llvm_i32_ty]>;

let IntrProperties = [IntrNoMem] in {
  def int_eh_sjlj_lsda             : Intrinsic<[llvm_ptr_ty]>;
  def int_eh_sjlj_callsite         : Intrinsic<[], [llvm_i32_ty]>;
}
def int_eh_sjlj_functioncontext : Intrinsic<[], [llvm_ptr_ty]>;
def int_eh_sjlj_setjmp          : Intrinsic<[llvm_i32_ty], [llvm_ptr_ty]>;
def int_eh_sjlj_longjmp         : Intrinsic<[], [llvm_ptr_ty], [IntrNoReturn]>;
def int_eh_sjlj_setup_dispatch  : Intrinsic<[], []>;

//===---------------- Generic Variable Attribute Intrinsics----------------===//
//
def int_var_annotation : Intrinsic<[],
                                   [llvm_ptr_ty, llvm_ptr_ty,
                                    llvm_ptr_ty, llvm_i32_ty],
                                   [], "llvm.var.annotation">;
def int_ptr_annotation : Intrinsic<[LLVMAnyPointerType<llvm_anyint_ty>],
                                   [LLVMMatchType<0>, llvm_ptr_ty, llvm_ptr_ty,
                                    llvm_i32_ty],
                                   [], "llvm.ptr.annotation">;
def int_annotation : Intrinsic<[llvm_anyint_ty],
                               [LLVMMatchType<0>, llvm_ptr_ty,
                                llvm_ptr_ty, llvm_i32_ty],
                               [], "llvm.annotation">;

// Annotates the current program point with metadata strings which are emitted
// as CodeView debug info records. This is expensive, as it disables inlining
// and is modelled as having side effects.
def int_codeview_annotation : Intrinsic<[], [llvm_metadata_ty],
                                        [IntrInaccessibleMemOnly, IntrNoDuplicate],
                                        "llvm.codeview.annotation">;

//===------------------------ Trampoline Intrinsics -----------------------===//
//
def int_init_trampoline : Intrinsic<[],
                                    [llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
                                    [IntrArgMemOnly, NoCapture<0>]>,
                                   GCCBuiltin<"__builtin_init_trampoline">;

def int_adjust_trampoline : Intrinsic<[llvm_ptr_ty], [llvm_ptr_ty],
                                      [IntrReadMem, IntrArgMemOnly]>,
                                     GCCBuiltin<"__builtin_adjust_trampoline">;

//===------------------------ Overflow Intrinsics -------------------------===//
//

// Expose the carry flag from add operations on two integrals.
def int_sadd_with_overflow : Intrinsic<[llvm_anyint_ty, llvm_i1_ty],
                                       [LLVMMatchType<0>, LLVMMatchType<0>],
                                       [IntrNoMem, IntrSpeculatable]>;
def int_uadd_with_overflow : Intrinsic<[llvm_anyint_ty, llvm_i1_ty],
                                       [LLVMMatchType<0>, LLVMMatchType<0>],
                                       [IntrNoMem, IntrSpeculatable]>;

def int_ssub_with_overflow : Intrinsic<[llvm_anyint_ty, llvm_i1_ty],
                                       [LLVMMatchType<0>, LLVMMatchType<0>],
                                       [IntrNoMem, IntrSpeculatable]>;
def int_usub_with_overflow : Intrinsic<[llvm_anyint_ty, llvm_i1_ty],
                                       [LLVMMatchType<0>, LLVMMatchType<0>],
                                       [IntrNoMem, IntrSpeculatable]>;

def int_smul_with_overflow : Intrinsic<[llvm_anyint_ty, llvm_i1_ty],
                                       [LLVMMatchType<0>, LLVMMatchType<0>],
                                       [IntrNoMem, IntrSpeculatable]>;
def int_umul_with_overflow : Intrinsic<[llvm_anyint_ty, llvm_i1_ty],
                                       [LLVMMatchType<0>, LLVMMatchType<0>],
                                       [IntrNoMem, IntrSpeculatable]>;

//===------------------------- Saturation Arithmetic Intrinsics ---------------------===//
//
def int_sadd_sat : Intrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable, Commutative]>;
def int_uadd_sat : Intrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable, Commutative]>;
def int_ssub_sat : Intrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable]>;
def int_usub_sat : Intrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable]>;

//===------------------------- Fixed Point Arithmetic Intrinsics ---------------------===//
//
def int_smul_fix : Intrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                             [IntrNoMem, IntrSpeculatable, Commutative]>;

//===------------------------- Memory Use Markers -------------------------===//
//
def int_lifetime_start  : Intrinsic<[],
                                    [llvm_i64_ty, llvm_anyptr_ty],
                                    [IntrArgMemOnly, NoCapture<1>]>;
def int_lifetime_end    : Intrinsic<[],
                                    [llvm_i64_ty, llvm_anyptr_ty],
                                    [IntrArgMemOnly, NoCapture<1>]>;
def int_invariant_start : Intrinsic<[llvm_descriptor_ty],
                                    [llvm_i64_ty, llvm_anyptr_ty],
                                    [IntrArgMemOnly, NoCapture<1>]>;
def int_invariant_end   : Intrinsic<[],
                                    [llvm_descriptor_ty, llvm_i64_ty,
                                     llvm_anyptr_ty],
                                    [IntrArgMemOnly, NoCapture<2>]>;

// launder.invariant.group can't be marked with 'readnone' (IntrNoMem),
// because it would cause CSE of two barriers with the same argument.
// Inaccessiblememonly says that the barrier doesn't read the argument,
// but it changes state not accessible to this module. This way
// we can DSE through the barrier because it doesn't read the value
// after store. Although the barrier doesn't modify any memory it
// can't be marked as readonly, because it would be possible to
// CSE 2 barriers with store in between.
// The argument also can't be marked with 'returned' attribute, because
// it would remove barrier.
// Note that it is still experimental, which means that its semantics
// might change in the future.
def int_launder_invariant_group : Intrinsic<[llvm_anyptr_ty],
                                            [LLVMMatchType<0>],
                                            [IntrInaccessibleMemOnly, IntrSpeculatable]>;


def int_strip_invariant_group : Intrinsic<[llvm_anyptr_ty],
                                          [LLVMMatchType<0>],
                                          [IntrSpeculatable, IntrNoMem]>;

//===------------------------ Stackmap Intrinsics -------------------------===//
//
def int_experimental_stackmap : Intrinsic<[],
                                  [llvm_i64_ty, llvm_i32_ty, llvm_vararg_ty],
                                  [Throws]>;
def int_experimental_patchpoint_void : Intrinsic<[],
                                                 [llvm_i64_ty, llvm_i32_ty,
                                                  llvm_ptr_ty, llvm_i32_ty,
                                                  llvm_vararg_ty],
                                                  [Throws]>;
def int_experimental_patchpoint_i64 : Intrinsic<[llvm_i64_ty],
                                                [llvm_i64_ty, llvm_i32_ty,
                                                 llvm_ptr_ty, llvm_i32_ty,
                                                 llvm_vararg_ty],
                                                 [Throws]>;


//===------------------------ Garbage Collection Intrinsics ---------------===//
// These are documented in docs/Statepoint.rst

def int_experimental_gc_statepoint : Intrinsic<[llvm_token_ty],
                               [llvm_i64_ty, llvm_i32_ty,
                                llvm_anyptr_ty, llvm_i32_ty,
                                llvm_i32_ty, llvm_vararg_ty],
                                [Throws]>;

def int_experimental_gc_result   : Intrinsic<[llvm_any_ty], [llvm_token_ty],
                                             [IntrReadMem]>;
def int_experimental_gc_relocate : Intrinsic<[llvm_any_ty],
                                [llvm_token_ty, llvm_i32_ty, llvm_i32_ty],
                                [IntrReadMem]>;

//===------------------------ Coroutine Intrinsics ---------------===//
// These are documented in docs/Coroutines.rst

// Coroutine Structure Intrinsics.

def int_coro_id : Intrinsic<[llvm_token_ty], [llvm_i32_ty, llvm_ptr_ty,
                             llvm_ptr_ty, llvm_ptr_ty],
                            [IntrArgMemOnly, IntrReadMem,
                             ReadNone<1>, ReadOnly<2>, NoCapture<2>]>;
def int_coro_alloc : Intrinsic<[llvm_i1_ty], [llvm_token_ty], []>;
def int_coro_begin : Intrinsic<[llvm_ptr_ty], [llvm_token_ty, llvm_ptr_ty],
                               [WriteOnly<1>]>;

def int_coro_free : Intrinsic<[llvm_ptr_ty], [llvm_token_ty, llvm_ptr_ty],
                              [IntrReadMem, IntrArgMemOnly, ReadOnly<1>,
                               NoCapture<1>]>;
def int_coro_end : Intrinsic<[llvm_i1_ty], [llvm_ptr_ty, llvm_i1_ty], []>;

def int_coro_frame : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;
def int_coro_noop : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;
def int_coro_size : Intrinsic<[llvm_anyint_ty], [], [IntrNoMem]>;

def int_coro_save : Intrinsic<[llvm_token_ty], [llvm_ptr_ty], []>;
def int_coro_suspend : Intrinsic<[llvm_i8_ty], [llvm_token_ty, llvm_i1_ty], []>;

def int_coro_param : Intrinsic<[llvm_i1_ty], [llvm_ptr_ty, llvm_ptr_ty],
                               [IntrNoMem, ReadNone<0>, ReadNone<1>]>;

// Coroutine Manipulation Intrinsics.

def int_coro_resume : Intrinsic<[], [llvm_ptr_ty], [Throws]>;
def int_coro_destroy : Intrinsic<[], [llvm_ptr_ty], [Throws]>;
def int_coro_done : Intrinsic<[llvm_i1_ty], [llvm_ptr_ty],
                              [IntrArgMemOnly, ReadOnly<0>, NoCapture<0>]>;
def int_coro_promise : Intrinsic<[llvm_ptr_ty],
                                 [llvm_ptr_ty, llvm_i32_ty, llvm_i1_ty],
                                 [IntrNoMem, NoCapture<0>]>;

// Coroutine Lowering Intrinsics. Used internally by coroutine passes.

def int_coro_subfn_addr : Intrinsic<[llvm_ptr_ty], [llvm_ptr_ty, llvm_i8_ty],
                                    [IntrReadMem, IntrArgMemOnly, ReadOnly<0>,
                                     NoCapture<0>]>;

///===-------------------------- Other Intrinsics --------------------------===//
//
def int_flt_rounds : Intrinsic<[llvm_i32_ty]>,
                     GCCBuiltin<"__builtin_flt_rounds">;
def int_trap : Intrinsic<[], [], [IntrNoReturn, IntrCold]>,
               GCCBuiltin<"__builtin_trap">;
def int_debugtrap : Intrinsic<[]>,
                    GCCBuiltin<"__builtin_debugtrap">;

// Support for dynamic deoptimization (or de-specialization)
def int_experimental_deoptimize : Intrinsic<[llvm_any_ty], [llvm_vararg_ty],
                                            [Throws]>;

// Support for speculative runtime guards
def int_experimental_guard : Intrinsic<[], [llvm_i1_ty, llvm_vararg_ty],
                                       [Throws]>;

// Supports widenable conditions for guards represented as explicit branches.
def int_experimental_widenable_condition : Intrinsic<[llvm_i1_ty], [],
                                           [IntrInaccessibleMemOnly]>;

// NOP: calls/invokes to this intrinsic are removed by codegen
def int_donothing : Intrinsic<[], [], [IntrNoMem]>;

// This instruction has no actual effect, though it is treated by the optimizer
// has having opaque side effects. This may be inserted into loops to ensure
// that they are not removed even if they turn out to be empty, for languages
// which specify that infinite loops must be preserved.
def int_sideeffect : Intrinsic<[], [], [IntrInaccessibleMemOnly]>;

// Intrisics to support half precision floating point format
let IntrProperties = [IntrNoMem] in {
def int_convert_to_fp16   : Intrinsic<[llvm_i16_ty], [llvm_anyfloat_ty]>;
def int_convert_from_fp16 : Intrinsic<[llvm_anyfloat_ty], [llvm_i16_ty]>;
}

// Clear cache intrinsic, default to ignore (ie. emit nothing)
// maps to void __clear_cache() on supporting platforms
def int_clear_cache : Intrinsic<[], [llvm_ptr_ty, llvm_ptr_ty],
                                [], "llvm.clear_cache">;

// Intrinsic to detect whether its argument is a constant.
def int_is_constant : Intrinsic<[llvm_i1_ty], [llvm_any_ty], [IntrNoMem], "llvm.is.constant">;


//===-------------------------- Masked Intrinsics -------------------------===//
//
def int_masked_store : Intrinsic<[], [llvm_anyvector_ty,
                                      LLVMAnyPointerType<LLVMMatchType<0>>,
                                      llvm_i32_ty,
                                      LLVMVectorSameWidth<0, llvm_i1_ty>],
                                 [IntrArgMemOnly]>;

def int_masked_load  : Intrinsic<[llvm_anyvector_ty],
                                 [LLVMAnyPointerType<LLVMMatchType<0>>, llvm_i32_ty,
                                  LLVMVectorSameWidth<0, llvm_i1_ty>, LLVMMatchType<0>],
                                 [IntrReadMem, IntrArgMemOnly]>;

def int_masked_gather: Intrinsic<[llvm_anyvector_ty],
                                 [LLVMVectorOfAnyPointersToElt<0>, llvm_i32_ty,
                                  LLVMVectorSameWidth<0, llvm_i1_ty>,
                                  LLVMMatchType<0>],
                                 [IntrReadMem]>;

def int_masked_scatter: Intrinsic<[],
                                  [llvm_anyvector_ty,
                                   LLVMVectorOfAnyPointersToElt<0>, llvm_i32_ty,
                                   LLVMVectorSameWidth<0, llvm_i1_ty>]>;

def int_masked_expandload: Intrinsic<[llvm_anyvector_ty],
                                     [LLVMPointerToElt<0>,
                                      LLVMVectorSameWidth<0, llvm_i1_ty>,
                                      LLVMMatchType<0>],
                                     [IntrReadMem]>;

def int_masked_compressstore: Intrinsic<[],
                                     [llvm_anyvector_ty,
                                      LLVMPointerToElt<0>,
                                      LLVMVectorSameWidth<0, llvm_i1_ty>],
                                     [IntrArgMemOnly]>;

// Test whether a pointer is associated with a type metadata identifier.
def int_type_test : Intrinsic<[llvm_i1_ty], [llvm_ptr_ty, llvm_metadata_ty],
                              [IntrNoMem]>;

// Safely loads a function pointer from a virtual table pointer using type metadata.
def int_type_checked_load : Intrinsic<[llvm_ptr_ty, llvm_i1_ty],
                                      [llvm_ptr_ty, llvm_i32_ty, llvm_metadata_ty],
                                      [IntrNoMem]>;

// Create a branch funnel that implements an indirect call to a limited set of
// callees. This needs to be a musttail call.
def int_icall_branch_funnel : Intrinsic<[], [llvm_vararg_ty], []>;

def int_load_relative: Intrinsic<[llvm_ptr_ty], [llvm_ptr_ty, llvm_anyint_ty],
                                 [IntrReadMem, IntrArgMemOnly]>;

// Xray intrinsics
//===----------------------------------------------------------------------===//
// Custom event logging for x-ray.
// Takes a pointer to a string and the length of the string.
def int_xray_customevent : Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty],
                                     [NoCapture<0>, ReadOnly<0>, IntrWriteMem]>;
// Typed event logging for x-ray.
// Takes a numeric type tag, a pointer to a string and the length of the string.
def int_xray_typedevent : Intrinsic<[], [llvm_i16_ty, llvm_ptr_ty, llvm_i32_ty],
                                        [NoCapture<1>, ReadOnly<1>, IntrWriteMem]>;
//===----------------------------------------------------------------------===//

//===------ Memory intrinsics with element-wise atomicity guarantees ------===//
//

// @llvm.memcpy.element.unordered.atomic.*(dest, src, length, elementsize)
def int_memcpy_element_unordered_atomic
    : Intrinsic<[],
                [
                  llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty, llvm_i32_ty
                ],
                [
                  IntrArgMemOnly, NoCapture<0>, NoCapture<1>, WriteOnly<0>,
                  ReadOnly<1>
                ]>;

// @llvm.memmove.element.unordered.atomic.*(dest, src, length, elementsize)
def int_memmove_element_unordered_atomic
    : Intrinsic<[],
                [
                  llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty, llvm_i32_ty
                ],
                [
                  IntrArgMemOnly, NoCapture<0>, NoCapture<1>, WriteOnly<0>,
                  ReadOnly<1>
                ]>;

// @llvm.memset.element.unordered.atomic.*(dest, value, length, elementsize)
def int_memset_element_unordered_atomic
    : Intrinsic<[], [ llvm_anyptr_ty, llvm_i8_ty, llvm_anyint_ty, llvm_i32_ty ],
                [ IntrArgMemOnly, NoCapture<0>, WriteOnly<0> ]>;

//===------------------------ Reduction Intrinsics ------------------------===//
//
def int_experimental_vector_reduce_fadd : Intrinsic<[llvm_anyfloat_ty],
                                                    [llvm_anyfloat_ty,
                                                     llvm_anyvector_ty],
                                                    [IntrNoMem]>;
def int_experimental_vector_reduce_fmul : Intrinsic<[llvm_anyfloat_ty],
                                                    [llvm_anyfloat_ty,
                                                     llvm_anyvector_ty],
                                                    [IntrNoMem]>;
def int_experimental_vector_reduce_add : Intrinsic<[llvm_anyint_ty],
                                                   [llvm_anyvector_ty],
                                                   [IntrNoMem]>;
def int_experimental_vector_reduce_mul : Intrinsic<[llvm_anyint_ty],
                                                   [llvm_anyvector_ty],
                                                   [IntrNoMem]>;
def int_experimental_vector_reduce_and : Intrinsic<[llvm_anyint_ty],
                                                   [llvm_anyvector_ty],
                                                   [IntrNoMem]>;
def int_experimental_vector_reduce_or : Intrinsic<[llvm_anyint_ty],
                                                  [llvm_anyvector_ty],
                                                  [IntrNoMem]>;
def int_experimental_vector_reduce_xor : Intrinsic<[llvm_anyint_ty],
                                                   [llvm_anyvector_ty],
                                                   [IntrNoMem]>;
def int_experimental_vector_reduce_smax : Intrinsic<[llvm_anyint_ty],
                                                    [llvm_anyvector_ty],
                                                    [IntrNoMem]>;
def int_experimental_vector_reduce_smin : Intrinsic<[llvm_anyint_ty],
                                                    [llvm_anyvector_ty],
                                                    [IntrNoMem]>;
def int_experimental_vector_reduce_umax : Intrinsic<[llvm_anyint_ty],
                                                    [llvm_anyvector_ty],
                                                    [IntrNoMem]>;
def int_experimental_vector_reduce_umin : Intrinsic<[llvm_anyint_ty],
                                                    [llvm_anyvector_ty],
                                                    [IntrNoMem]>;
def int_experimental_vector_reduce_fmax : Intrinsic<[llvm_anyfloat_ty],
                                                    [llvm_anyvector_ty],
                                                    [IntrNoMem]>;
def int_experimental_vector_reduce_fmin : Intrinsic<[llvm_anyfloat_ty],
                                                    [llvm_anyvector_ty],
                                                    [IntrNoMem]>;

//===----- Intrinsics that are used to provide predicate information -----===//

def int_ssa_copy : Intrinsic<[llvm_any_ty], [LLVMMatchType<0>],
                             [IntrNoMem, Returned<0>]>;
//===----------------------------------------------------------------------===//
// Target-specific intrinsics
//===----------------------------------------------------------------------===//

include "llvm/IR/IntrinsicsPowerPC.td"
include "llvm/IR/IntrinsicsX86.td"
include "llvm/IR/IntrinsicsARM.td"
include "llvm/IR/IntrinsicsAArch64.td"
include "llvm/IR/IntrinsicsXCore.td"
include "llvm/IR/IntrinsicsHexagon.td"
include "llvm/IR/IntrinsicsNVVM.td"
include "llvm/IR/IntrinsicsMips.td"
include "llvm/IR/IntrinsicsAMDGPU.td"
include "llvm/IR/IntrinsicsBPF.td"
include "llvm/IR/IntrinsicsSystemZ.td"
include "llvm/IR/IntrinsicsWebAssembly.td"
include "llvm/IR/IntrinsicsRISCV.td"
