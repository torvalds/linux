//===- IntrinsicsSystemZ.td - Defines SystemZ intrinsics ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the SystemZ-specific intrinsics.
//
//===----------------------------------------------------------------------===//

class SystemZUnaryConv<string name, LLVMType result, LLVMType arg>
  : GCCBuiltin<"__builtin_s390_" ## name>,
    Intrinsic<[result], [arg], [IntrNoMem]>;

class SystemZUnary<string name, LLVMType type>
  : SystemZUnaryConv<name, type, type>;

class SystemZUnaryConvCC<LLVMType result, LLVMType arg>
  : Intrinsic<[result, llvm_i32_ty], [arg], [IntrNoMem]>;

class SystemZUnaryCC<LLVMType type>
  : SystemZUnaryConvCC<type, type>;

class SystemZBinaryConv<string name, LLVMType result, LLVMType arg>
  : GCCBuiltin<"__builtin_s390_" ## name>,
    Intrinsic<[result], [arg, arg], [IntrNoMem]>;

class SystemZBinary<string name, LLVMType type>
  : SystemZBinaryConv<name, type, type>;

class SystemZBinaryInt<string name, LLVMType type>
  : GCCBuiltin<"__builtin_s390_" ## name>,
    Intrinsic<[type], [type, llvm_i32_ty], [IntrNoMem]>;

class SystemZBinaryConvCC<LLVMType result, LLVMType arg>
  : Intrinsic<[result, llvm_i32_ty], [arg, arg], [IntrNoMem]>;

class SystemZBinaryConvIntCC<LLVMType result, LLVMType arg>
  : Intrinsic<[result, llvm_i32_ty], [arg, llvm_i32_ty], [IntrNoMem]>;

class SystemZBinaryCC<LLVMType type>
  : SystemZBinaryConvCC<type, type>;

class SystemZTernaryConv<string name, LLVMType result, LLVMType arg>
  : GCCBuiltin<"__builtin_s390_" ## name>,
    Intrinsic<[result], [arg, arg, result], [IntrNoMem]>;

class SystemZTernary<string name, LLVMType type>
  : SystemZTernaryConv<name, type, type>;

class SystemZTernaryInt<string name, LLVMType type>
  : GCCBuiltin<"__builtin_s390_" ## name>,
    Intrinsic<[type], [type, type, llvm_i32_ty], [IntrNoMem]>;

class SystemZTernaryIntCC<LLVMType type>
  : Intrinsic<[type, llvm_i32_ty], [type, type, llvm_i32_ty], [IntrNoMem]>;

class SystemZQuaternaryInt<string name, LLVMType type>
  : GCCBuiltin<"__builtin_s390_" ## name>,
    Intrinsic<[type], [type, type, type, llvm_i32_ty], [IntrNoMem]>;

class SystemZQuaternaryIntCC<LLVMType type>
  : Intrinsic<[type, llvm_i32_ty], [type, type, type, llvm_i32_ty],
              [IntrNoMem]>;

multiclass SystemZUnaryExtBHF<string name> {
  def b : SystemZUnaryConv<name##"b", llvm_v8i16_ty, llvm_v16i8_ty>;
  def h : SystemZUnaryConv<name##"h", llvm_v4i32_ty, llvm_v8i16_ty>;
  def f : SystemZUnaryConv<name##"f", llvm_v2i64_ty, llvm_v4i32_ty>;
}

multiclass SystemZUnaryExtBHWF<string name> {
  def b  : SystemZUnaryConv<name##"b",  llvm_v8i16_ty, llvm_v16i8_ty>;
  def hw : SystemZUnaryConv<name##"hw", llvm_v4i32_ty, llvm_v8i16_ty>;
  def f  : SystemZUnaryConv<name##"f",  llvm_v2i64_ty, llvm_v4i32_ty>;
}

multiclass SystemZUnaryBHF<string name> {
  def b : SystemZUnary<name##"b", llvm_v16i8_ty>;
  def h : SystemZUnary<name##"h", llvm_v8i16_ty>;
  def f : SystemZUnary<name##"f", llvm_v4i32_ty>;
}

multiclass SystemZUnaryBHFG<string name> : SystemZUnaryBHF<name> {
  def g : SystemZUnary<name##"g", llvm_v2i64_ty>;
}

multiclass SystemZUnaryCCBHF {
  def bs : SystemZUnaryCC<llvm_v16i8_ty>;
  def hs : SystemZUnaryCC<llvm_v8i16_ty>;
  def fs : SystemZUnaryCC<llvm_v4i32_ty>;
}

multiclass SystemZBinaryTruncHFG<string name> {
  def h : SystemZBinaryConv<name##"h", llvm_v16i8_ty, llvm_v8i16_ty>;
  def f : SystemZBinaryConv<name##"f", llvm_v8i16_ty, llvm_v4i32_ty>;
  def g : SystemZBinaryConv<name##"g", llvm_v4i32_ty, llvm_v2i64_ty>;
}

multiclass SystemZBinaryTruncCCHFG {
  def hs : SystemZBinaryConvCC<llvm_v16i8_ty, llvm_v8i16_ty>;
  def fs : SystemZBinaryConvCC<llvm_v8i16_ty, llvm_v4i32_ty>;
  def gs : SystemZBinaryConvCC<llvm_v4i32_ty, llvm_v2i64_ty>;
}

multiclass SystemZBinaryExtBHF<string name> {
  def b : SystemZBinaryConv<name##"b", llvm_v8i16_ty, llvm_v16i8_ty>;
  def h : SystemZBinaryConv<name##"h", llvm_v4i32_ty, llvm_v8i16_ty>;
  def f : SystemZBinaryConv<name##"f", llvm_v2i64_ty, llvm_v4i32_ty>;
}

multiclass SystemZBinaryExtBHFG<string name> : SystemZBinaryExtBHF<name> {
  def g : SystemZBinaryConv<name##"g", llvm_v16i8_ty, llvm_v2i64_ty>;
}

multiclass SystemZBinaryBHF<string name> {
  def b : SystemZBinary<name##"b", llvm_v16i8_ty>;
  def h : SystemZBinary<name##"h", llvm_v8i16_ty>;
  def f : SystemZBinary<name##"f", llvm_v4i32_ty>;
}

multiclass SystemZBinaryBHFG<string name> : SystemZBinaryBHF<name> {
  def g : SystemZBinary<name##"g", llvm_v2i64_ty>;
}

multiclass SystemZBinaryIntBHFG<string name> {
  def b : SystemZBinaryInt<name##"b", llvm_v16i8_ty>;
  def h : SystemZBinaryInt<name##"h", llvm_v8i16_ty>;
  def f : SystemZBinaryInt<name##"f", llvm_v4i32_ty>;
  def g : SystemZBinaryInt<name##"g", llvm_v2i64_ty>;
}

multiclass SystemZBinaryCCBHF {
  def bs : SystemZBinaryCC<llvm_v16i8_ty>;
  def hs : SystemZBinaryCC<llvm_v8i16_ty>;
  def fs : SystemZBinaryCC<llvm_v4i32_ty>;
}

multiclass SystemZCompareBHFG<string name> {
  def bs : SystemZBinaryCC<llvm_v16i8_ty>;
  def hs : SystemZBinaryCC<llvm_v8i16_ty>;
  def fs : SystemZBinaryCC<llvm_v4i32_ty>;
  def gs : SystemZBinaryCC<llvm_v2i64_ty>;
}

multiclass SystemZTernaryExtBHF<string name> {
  def b : SystemZTernaryConv<name##"b", llvm_v8i16_ty, llvm_v16i8_ty>;
  def h : SystemZTernaryConv<name##"h", llvm_v4i32_ty, llvm_v8i16_ty>;
  def f : SystemZTernaryConv<name##"f", llvm_v2i64_ty, llvm_v4i32_ty>;
}

multiclass SystemZTernaryExtBHFG<string name> : SystemZTernaryExtBHF<name> {
  def g : SystemZTernaryConv<name##"g", llvm_v16i8_ty, llvm_v2i64_ty>;
}

multiclass SystemZTernaryBHF<string name> {
  def b : SystemZTernary<name##"b", llvm_v16i8_ty>;
  def h : SystemZTernary<name##"h", llvm_v8i16_ty>;
  def f : SystemZTernary<name##"f", llvm_v4i32_ty>;
}

multiclass SystemZTernaryIntBHF<string name> {
  def b : SystemZTernaryInt<name##"b", llvm_v16i8_ty>;
  def h : SystemZTernaryInt<name##"h", llvm_v8i16_ty>;
  def f : SystemZTernaryInt<name##"f", llvm_v4i32_ty>;
}

multiclass SystemZTernaryIntCCBHF {
  def bs : SystemZTernaryIntCC<llvm_v16i8_ty>;
  def hs : SystemZTernaryIntCC<llvm_v8i16_ty>;
  def fs : SystemZTernaryIntCC<llvm_v4i32_ty>;
}

multiclass SystemZQuaternaryIntBHF<string name> {
  def b : SystemZQuaternaryInt<name##"b", llvm_v16i8_ty>;
  def h : SystemZQuaternaryInt<name##"h", llvm_v8i16_ty>;
  def f : SystemZQuaternaryInt<name##"f", llvm_v4i32_ty>;
}

multiclass SystemZQuaternaryIntBHFG<string name> : SystemZQuaternaryIntBHF<name> {
  def g : SystemZQuaternaryInt<name##"g", llvm_v2i64_ty>;
}

multiclass SystemZQuaternaryIntCCBHF {
  def bs : SystemZQuaternaryIntCC<llvm_v16i8_ty>;
  def hs : SystemZQuaternaryIntCC<llvm_v8i16_ty>;
  def fs : SystemZQuaternaryIntCC<llvm_v4i32_ty>;
}

//===----------------------------------------------------------------------===//
//
// Transactional-execution intrinsics
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "s390" in {
  def int_s390_tbegin : Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
                                  [IntrNoDuplicate, IntrWriteMem]>;

  def int_s390_tbegin_nofloat : Intrinsic<[llvm_i32_ty],
                                          [llvm_ptr_ty, llvm_i32_ty],
                                          [IntrNoDuplicate, IntrWriteMem]>;

  def int_s390_tbeginc : Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty],
                                   [IntrNoDuplicate, IntrWriteMem]>;

  def int_s390_tabort : Intrinsic<[], [llvm_i64_ty],
                                  [IntrNoReturn, Throws, IntrWriteMem]>;

  def int_s390_tend : GCCBuiltin<"__builtin_tend">,
                      Intrinsic<[llvm_i32_ty], []>;

  def int_s390_etnd : GCCBuiltin<"__builtin_tx_nesting_depth">,
                      Intrinsic<[llvm_i32_ty], [], [IntrNoMem]>;

  def int_s390_ntstg : Intrinsic<[], [llvm_i64_ty, llvm_ptr64_ty],
                                 [IntrArgMemOnly, IntrWriteMem]>;

  def int_s390_ppa_txassist : GCCBuiltin<"__builtin_tx_assist">,
                              Intrinsic<[], [llvm_i32_ty]>;
}

//===----------------------------------------------------------------------===//
//
// Vector intrinsics
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "s390" in {
  def int_s390_lcbb : GCCBuiltin<"__builtin_s390_lcbb">,
                      Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
                                [IntrNoMem]>;

  def int_s390_vlbb : GCCBuiltin<"__builtin_s390_vlbb">,
                      Intrinsic<[llvm_v16i8_ty], [llvm_ptr_ty, llvm_i32_ty],
                                [IntrReadMem, IntrArgMemOnly]>;

  def int_s390_vll : GCCBuiltin<"__builtin_s390_vll">,
                     Intrinsic<[llvm_v16i8_ty], [llvm_i32_ty, llvm_ptr_ty],
                               [IntrReadMem, IntrArgMemOnly]>;

  def int_s390_vpdi : GCCBuiltin<"__builtin_s390_vpdi">,
                      Intrinsic<[llvm_v2i64_ty],
                                [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
                                [IntrNoMem]>;

  def int_s390_vperm : GCCBuiltin<"__builtin_s390_vperm">,
                       Intrinsic<[llvm_v16i8_ty],
                                 [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
                                 [IntrNoMem]>;

  defm int_s390_vpks : SystemZBinaryTruncHFG<"vpks">;
  defm int_s390_vpks : SystemZBinaryTruncCCHFG;

  defm int_s390_vpkls : SystemZBinaryTruncHFG<"vpkls">;
  defm int_s390_vpkls : SystemZBinaryTruncCCHFG;

  def int_s390_vstl : GCCBuiltin<"__builtin_s390_vstl">,
                      Intrinsic<[], [llvm_v16i8_ty, llvm_i32_ty, llvm_ptr_ty],
                                [IntrArgMemOnly, IntrWriteMem]>;

  defm int_s390_vupl  : SystemZUnaryExtBHWF<"vupl">;
  defm int_s390_vupll : SystemZUnaryExtBHF<"vupll">;

  defm int_s390_vuph  : SystemZUnaryExtBHF<"vuph">;
  defm int_s390_vuplh : SystemZUnaryExtBHF<"vuplh">;

  defm int_s390_vacc : SystemZBinaryBHFG<"vacc">;

  def int_s390_vaq    : SystemZBinary<"vaq",     llvm_v16i8_ty>;
  def int_s390_vacq   : SystemZTernary<"vacq",   llvm_v16i8_ty>;
  def int_s390_vaccq  : SystemZBinary<"vaccq",   llvm_v16i8_ty>;
  def int_s390_vacccq : SystemZTernary<"vacccq", llvm_v16i8_ty>;

  defm int_s390_vavg  : SystemZBinaryBHFG<"vavg">;
  defm int_s390_vavgl : SystemZBinaryBHFG<"vavgl">;

  def int_s390_vcksm : SystemZBinary<"vcksm", llvm_v4i32_ty>;

  defm int_s390_vgfm  : SystemZBinaryExtBHFG<"vgfm">;
  defm int_s390_vgfma : SystemZTernaryExtBHFG<"vgfma">;

  defm int_s390_vmah  : SystemZTernaryBHF<"vmah">;
  defm int_s390_vmalh : SystemZTernaryBHF<"vmalh">;
  defm int_s390_vmae  : SystemZTernaryExtBHF<"vmae">;
  defm int_s390_vmale : SystemZTernaryExtBHF<"vmale">;
  defm int_s390_vmao  : SystemZTernaryExtBHF<"vmao">;
  defm int_s390_vmalo : SystemZTernaryExtBHF<"vmalo">;

  defm int_s390_vmh  : SystemZBinaryBHF<"vmh">;
  defm int_s390_vmlh : SystemZBinaryBHF<"vmlh">;
  defm int_s390_vme  : SystemZBinaryExtBHF<"vme">;
  defm int_s390_vmle : SystemZBinaryExtBHF<"vmle">;
  defm int_s390_vmo  : SystemZBinaryExtBHF<"vmo">;
  defm int_s390_vmlo : SystemZBinaryExtBHF<"vmlo">;

  defm int_s390_verllv : SystemZBinaryBHFG<"verllv">;
  defm int_s390_verll  : SystemZBinaryIntBHFG<"verll">;
  defm int_s390_verim  : SystemZQuaternaryIntBHFG<"verim">;

  def int_s390_vsl   : SystemZBinary<"vsl",   llvm_v16i8_ty>;
  def int_s390_vslb  : SystemZBinary<"vslb",  llvm_v16i8_ty>;
  def int_s390_vsra  : SystemZBinary<"vsra",  llvm_v16i8_ty>;
  def int_s390_vsrab : SystemZBinary<"vsrab", llvm_v16i8_ty>;
  def int_s390_vsrl  : SystemZBinary<"vsrl",  llvm_v16i8_ty>;
  def int_s390_vsrlb : SystemZBinary<"vsrlb", llvm_v16i8_ty>;

  def int_s390_vsldb : GCCBuiltin<"__builtin_s390_vsldb">,
                       Intrinsic<[llvm_v16i8_ty],
                                 [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
                                 [IntrNoMem]>;

  defm int_s390_vscbi : SystemZBinaryBHFG<"vscbi">;

  def int_s390_vsq     : SystemZBinary<"vsq",      llvm_v16i8_ty>;
  def int_s390_vsbiq   : SystemZTernary<"vsbiq",   llvm_v16i8_ty>;
  def int_s390_vscbiq  : SystemZBinary<"vscbiq",   llvm_v16i8_ty>;
  def int_s390_vsbcbiq : SystemZTernary<"vsbcbiq", llvm_v16i8_ty>;

  def int_s390_vsumb : SystemZBinaryConv<"vsumb", llvm_v4i32_ty, llvm_v16i8_ty>;
  def int_s390_vsumh : SystemZBinaryConv<"vsumh", llvm_v4i32_ty, llvm_v8i16_ty>;

  def int_s390_vsumgh : SystemZBinaryConv<"vsumgh", llvm_v2i64_ty,
                                          llvm_v8i16_ty>;
  def int_s390_vsumgf : SystemZBinaryConv<"vsumgf", llvm_v2i64_ty,
                                          llvm_v4i32_ty>;

  def int_s390_vsumqf : SystemZBinaryConv<"vsumqf", llvm_v16i8_ty,
                                          llvm_v4i32_ty>;
  def int_s390_vsumqg : SystemZBinaryConv<"vsumqg", llvm_v16i8_ty,
                                          llvm_v2i64_ty>;

  def int_s390_vtm : SystemZBinaryConv<"vtm", llvm_i32_ty, llvm_v16i8_ty>;

  defm int_s390_vceq : SystemZCompareBHFG<"vceq">;
  defm int_s390_vch  : SystemZCompareBHFG<"vch">;
  defm int_s390_vchl : SystemZCompareBHFG<"vchl">;

  defm int_s390_vfae  : SystemZTernaryIntBHF<"vfae">;
  defm int_s390_vfae  : SystemZTernaryIntCCBHF;
  defm int_s390_vfaez : SystemZTernaryIntBHF<"vfaez">;
  defm int_s390_vfaez : SystemZTernaryIntCCBHF;

  defm int_s390_vfee  : SystemZBinaryBHF<"vfee">;
  defm int_s390_vfee  : SystemZBinaryCCBHF;
  defm int_s390_vfeez : SystemZBinaryBHF<"vfeez">;
  defm int_s390_vfeez : SystemZBinaryCCBHF;

  defm int_s390_vfene  : SystemZBinaryBHF<"vfene">;
  defm int_s390_vfene  : SystemZBinaryCCBHF;
  defm int_s390_vfenez : SystemZBinaryBHF<"vfenez">;
  defm int_s390_vfenez : SystemZBinaryCCBHF;

  defm int_s390_vistr : SystemZUnaryBHF<"vistr">;
  defm int_s390_vistr : SystemZUnaryCCBHF;

  defm int_s390_vstrc  : SystemZQuaternaryIntBHF<"vstrc">;
  defm int_s390_vstrc  : SystemZQuaternaryIntCCBHF;
  defm int_s390_vstrcz : SystemZQuaternaryIntBHF<"vstrcz">;
  defm int_s390_vstrcz : SystemZQuaternaryIntCCBHF;

  def int_s390_vfcedbs  : SystemZBinaryConvCC<llvm_v2i64_ty, llvm_v2f64_ty>;
  def int_s390_vfchdbs  : SystemZBinaryConvCC<llvm_v2i64_ty, llvm_v2f64_ty>;
  def int_s390_vfchedbs : SystemZBinaryConvCC<llvm_v2i64_ty, llvm_v2f64_ty>;

  def int_s390_vftcidb : SystemZBinaryConvIntCC<llvm_v2i64_ty, llvm_v2f64_ty>;

  def int_s390_vfidb : Intrinsic<[llvm_v2f64_ty],
                                 [llvm_v2f64_ty, llvm_i32_ty, llvm_i32_ty],
                                 [IntrNoMem]>;

  // Instructions from the Vector Enhancements Facility 1
  def int_s390_vbperm : SystemZBinaryConv<"vbperm", llvm_v2i64_ty,
                                          llvm_v16i8_ty>;

  def int_s390_vmslg  : GCCBuiltin<"__builtin_s390_vmslg">,
                        Intrinsic<[llvm_v16i8_ty],
                                  [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v16i8_ty,
                                   llvm_i32_ty], [IntrNoMem]>;

  def int_s390_vfmaxdb : Intrinsic<[llvm_v2f64_ty],
                                   [llvm_v2f64_ty, llvm_v2f64_ty, llvm_i32_ty],
                                   [IntrNoMem]>;
  def int_s390_vfmindb : Intrinsic<[llvm_v2f64_ty],
                                   [llvm_v2f64_ty, llvm_v2f64_ty, llvm_i32_ty],
                                   [IntrNoMem]>;
  def int_s390_vfmaxsb : Intrinsic<[llvm_v4f32_ty],
                                   [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i32_ty],
                                   [IntrNoMem]>;
  def int_s390_vfminsb : Intrinsic<[llvm_v4f32_ty],
                                   [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i32_ty],
                                   [IntrNoMem]>;

  def int_s390_vfcesbs  : SystemZBinaryConvCC<llvm_v4i32_ty, llvm_v4f32_ty>;
  def int_s390_vfchsbs  : SystemZBinaryConvCC<llvm_v4i32_ty, llvm_v4f32_ty>;
  def int_s390_vfchesbs : SystemZBinaryConvCC<llvm_v4i32_ty, llvm_v4f32_ty>;

  def int_s390_vftcisb : SystemZBinaryConvIntCC<llvm_v4i32_ty, llvm_v4f32_ty>;

  def int_s390_vfisb : Intrinsic<[llvm_v4f32_ty],
                                 [llvm_v4f32_ty, llvm_i32_ty, llvm_i32_ty],
                                 [IntrNoMem]>;

  // Instructions from the Vector Packed Decimal Facility
  def int_s390_vlrl : GCCBuiltin<"__builtin_s390_vlrl">,
                      Intrinsic<[llvm_v16i8_ty], [llvm_i32_ty, llvm_ptr_ty],
                                [IntrReadMem, IntrArgMemOnly]>;

  def int_s390_vstrl : GCCBuiltin<"__builtin_s390_vstrl">,
                       Intrinsic<[], [llvm_v16i8_ty, llvm_i32_ty, llvm_ptr_ty],
                                 [IntrArgMemOnly, IntrWriteMem]>;
}

//===----------------------------------------------------------------------===//
//
// Misc intrinsics
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "s390" in {
  def int_s390_sfpc : GCCBuiltin<"__builtin_s390_sfpc">,
                      Intrinsic<[], [llvm_i32_ty], []>;
  def int_s390_efpc : GCCBuiltin<"__builtin_s390_efpc">,
                      Intrinsic<[llvm_i32_ty], [], []>;

  def int_s390_tdc : Intrinsic<[llvm_i32_ty], [llvm_anyfloat_ty, llvm_i64_ty],
                               [IntrNoMem]>;
}
