//==- SystemZInstrVector.td - SystemZ Vector instructions ------*- tblgen-*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  // Register move.
  def VLR : UnaryVRRa<"vlr", 0xE756, null_frag, v128any, v128any>;
  def VLR32 : UnaryAliasVRR<null_frag, v32sb, v32sb>;
  def VLR64 : UnaryAliasVRR<null_frag, v64db, v64db>;

  // Load GR from VR element.
  def VLGV  : BinaryVRScGeneric<"vlgv", 0xE721>;
  def VLGVB : BinaryVRSc<"vlgvb", 0xE721, null_frag, v128b, 0>;
  def VLGVH : BinaryVRSc<"vlgvh", 0xE721, null_frag, v128h, 1>;
  def VLGVF : BinaryVRSc<"vlgvf", 0xE721, null_frag, v128f, 2>;
  def VLGVG : BinaryVRSc<"vlgvg", 0xE721, z_vector_extract, v128g, 3>;

  // Load VR element from GR.
  def VLVG  : TernaryVRSbGeneric<"vlvg", 0xE722>;
  def VLVGB : TernaryVRSb<"vlvgb", 0xE722, z_vector_insert,
                          v128b, v128b, GR32, 0>;
  def VLVGH : TernaryVRSb<"vlvgh", 0xE722, z_vector_insert,
                          v128h, v128h, GR32, 1>;
  def VLVGF : TernaryVRSb<"vlvgf", 0xE722, z_vector_insert,
                          v128f, v128f, GR32, 2>;
  def VLVGG : TernaryVRSb<"vlvgg", 0xE722, z_vector_insert,
                          v128g, v128g, GR64, 3>;

  // Load VR from GRs disjoint.
  def VLVGP : BinaryVRRf<"vlvgp", 0xE762, z_join_dwords, v128g>;
  def VLVGP32 : BinaryAliasVRRf<GR32>;
}

// Extractions always assign to the full GR64, even if the element would
// fit in the lower 32 bits.  Sub-i64 extracts therefore need to take a
// subreg of the result.
class VectorExtractSubreg<ValueType type, Instruction insn>
  : Pat<(i32 (z_vector_extract (type VR128:$vec), shift12only:$index)),
        (EXTRACT_SUBREG (insn VR128:$vec, shift12only:$index), subreg_l32)>;

def : VectorExtractSubreg<v16i8, VLGVB>;
def : VectorExtractSubreg<v8i16, VLGVH>;
def : VectorExtractSubreg<v4i32, VLGVF>;

//===----------------------------------------------------------------------===//
// Immediate instructions
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  let isAsCheapAsAMove = 1, isMoveImm = 1, isReMaterializable = 1 in {

    // Generate byte mask.
    def VZERO : InherentVRIa<"vzero", 0xE744, 0>;
    def VONE  : InherentVRIa<"vone", 0xE744, 0xffff>;
    def VGBM  : UnaryVRIa<"vgbm", 0xE744, z_byte_mask, v128b, imm32zx16>;

    // Generate mask.
    def VGM  : BinaryVRIbGeneric<"vgm", 0xE746>;
    def VGMB : BinaryVRIb<"vgmb", 0xE746, z_rotate_mask, v128b, 0>;
    def VGMH : BinaryVRIb<"vgmh", 0xE746, z_rotate_mask, v128h, 1>;
    def VGMF : BinaryVRIb<"vgmf", 0xE746, z_rotate_mask, v128f, 2>;
    def VGMG : BinaryVRIb<"vgmg", 0xE746, z_rotate_mask, v128g, 3>;

    // Replicate immediate.
    def VREPI  : UnaryVRIaGeneric<"vrepi", 0xE745, imm32sx16>;
    def VREPIB : UnaryVRIa<"vrepib", 0xE745, z_replicate, v128b, imm32sx16, 0>;
    def VREPIH : UnaryVRIa<"vrepih", 0xE745, z_replicate, v128h, imm32sx16, 1>;
    def VREPIF : UnaryVRIa<"vrepif", 0xE745, z_replicate, v128f, imm32sx16, 2>;
    def VREPIG : UnaryVRIa<"vrepig", 0xE745, z_replicate, v128g, imm32sx16, 3>;
  }

  // Load element immediate.
  //
  // We want these instructions to be used ahead of VLVG* where possible.
  // However, VLVG* takes a variable BD-format index whereas VLEI takes
  // a plain immediate index.  This means that VLVG* has an extra "base"
  // register operand and is 3 units more complex.  Bumping the complexity
  // of the VLEI* instructions by 4 means that they are strictly better
  // than VLVG* in cases where both forms match.
  let AddedComplexity = 4 in {
    def VLEIB : TernaryVRIa<"vleib", 0xE740, z_vector_insert,
                            v128b, v128b, imm32sx16trunc, imm32zx4>;
    def VLEIH : TernaryVRIa<"vleih", 0xE741, z_vector_insert,
                            v128h, v128h, imm32sx16trunc, imm32zx3>;
    def VLEIF : TernaryVRIa<"vleif", 0xE743, z_vector_insert,
                            v128f, v128f, imm32sx16, imm32zx2>;
    def VLEIG : TernaryVRIa<"vleig", 0xE742, z_vector_insert,
                            v128g, v128g, imm64sx16, imm32zx1>;
  }
}

//===----------------------------------------------------------------------===//
// Loads
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  // Load.
  def VL : UnaryVRX<"vl", 0xE706, null_frag, v128any, 16>;

  // Load to block boundary.  The number of loaded bytes is only known
  // at run time.  The instruction is really polymorphic, but v128b matches
  // the return type of the associated intrinsic.
  def VLBB : BinaryVRX<"vlbb", 0xE707, int_s390_vlbb, v128b, 0>;

  // Load count to block boundary.
  let Defs = [CC] in
    def LCBB : InstRXE<0xE727, (outs GR32:$R1),
                               (ins bdxaddr12only:$XBD2, imm32zx4:$M3),
                       "lcbb\t$R1, $XBD2, $M3",
                       [(set GR32:$R1, (int_s390_lcbb bdxaddr12only:$XBD2,
                                                      imm32zx4:$M3))]>;

  // Load with length.  The number of loaded bytes is only known at run time.
  def VLL : BinaryVRSb<"vll", 0xE737, int_s390_vll, 0>;

  // Load multiple.
  def VLM : LoadMultipleVRSa<"vlm", 0xE736>;

  // Load and replicate
  def VLREP  : UnaryVRXGeneric<"vlrep", 0xE705>;
  def VLREPB : UnaryVRX<"vlrepb", 0xE705, z_replicate_loadi8,  v128b, 1, 0>;
  def VLREPH : UnaryVRX<"vlreph", 0xE705, z_replicate_loadi16, v128h, 2, 1>;
  def VLREPF : UnaryVRX<"vlrepf", 0xE705, z_replicate_loadi32, v128f, 4, 2>;
  def VLREPG : UnaryVRX<"vlrepg", 0xE705, z_replicate_loadi64, v128g, 8, 3>;
  def : Pat<(v4f32 (z_replicate_loadf32 bdxaddr12only:$addr)),
            (VLREPF bdxaddr12only:$addr)>;
  def : Pat<(v2f64 (z_replicate_loadf64 bdxaddr12only:$addr)),
            (VLREPG bdxaddr12only:$addr)>;

  // Use VLREP to load subvectors.  These patterns use "12pair" because
  // LEY and LDY offer full 20-bit displacement fields.  It's often better
  // to use those instructions rather than force a 20-bit displacement
  // into a GPR temporary.
  let mayLoad = 1 in {
    def VL32 : UnaryAliasVRX<load, v32sb, bdxaddr12pair>;
    def VL64 : UnaryAliasVRX<load, v64db, bdxaddr12pair>;
  }

  // Load logical element and zero.
  def VLLEZ  : UnaryVRXGeneric<"vllez", 0xE704>;
  def VLLEZB : UnaryVRX<"vllezb", 0xE704, z_vllezi8,  v128b, 1, 0>;
  def VLLEZH : UnaryVRX<"vllezh", 0xE704, z_vllezi16, v128h, 2, 1>;
  def VLLEZF : UnaryVRX<"vllezf", 0xE704, z_vllezi32, v128f, 4, 2>;
  def VLLEZG : UnaryVRX<"vllezg", 0xE704, z_vllezi64, v128g, 8, 3>;
  def : Pat<(z_vllezf32 bdxaddr12only:$addr),
            (VLLEZF bdxaddr12only:$addr)>;
  def : Pat<(z_vllezf64 bdxaddr12only:$addr),
            (VLLEZG bdxaddr12only:$addr)>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VLLEZLF : UnaryVRX<"vllezlf", 0xE704, z_vllezli32, v128f, 4, 6>;
    def : Pat<(z_vllezlf32 bdxaddr12only:$addr),
              (VLLEZLF bdxaddr12only:$addr)>;
  }

  // Load element.
  def VLEB : TernaryVRX<"vleb", 0xE700, z_vlei8,  v128b, v128b, 1, imm32zx4>;
  def VLEH : TernaryVRX<"vleh", 0xE701, z_vlei16, v128h, v128h, 2, imm32zx3>;
  def VLEF : TernaryVRX<"vlef", 0xE703, z_vlei32, v128f, v128f, 4, imm32zx2>;
  def VLEG : TernaryVRX<"vleg", 0xE702, z_vlei64, v128g, v128g, 8, imm32zx1>;
  def : Pat<(z_vlef32 (v4f32 VR128:$val), bdxaddr12only:$addr, imm32zx2:$index),
            (VLEF VR128:$val, bdxaddr12only:$addr, imm32zx2:$index)>;
  def : Pat<(z_vlef64 (v2f64 VR128:$val), bdxaddr12only:$addr, imm32zx1:$index),
            (VLEG VR128:$val, bdxaddr12only:$addr, imm32zx1:$index)>;

  // Gather element.
  def VGEF : TernaryVRV<"vgef", 0xE713, 4, imm32zx2>;
  def VGEG : TernaryVRV<"vgeg", 0xE712, 8, imm32zx1>;
}

let Predicates = [FeatureVectorPackedDecimal] in {
  // Load rightmost with length.  The number of loaded bytes is only known
  // at run time.
  def VLRL : BinaryVSI<"vlrl", 0xE635, int_s390_vlrl, 0>;
  def VLRLR : BinaryVRSd<"vlrlr", 0xE637, int_s390_vlrl, 0>;
}

// Use replicating loads if we're inserting a single element into an
// undefined vector.  This avoids a false dependency on the previous
// register contents.
multiclass ReplicatePeephole<Instruction vlrep, ValueType vectype,
                             SDPatternOperator load, ValueType scalartype> {
  def : Pat<(vectype (z_vector_insert
                      (undef), (scalartype (load bdxaddr12only:$addr)), 0)),
            (vlrep bdxaddr12only:$addr)>;
  def : Pat<(vectype (scalar_to_vector
                      (scalartype (load bdxaddr12only:$addr)))),
            (vlrep bdxaddr12only:$addr)>;
}
defm : ReplicatePeephole<VLREPB, v16i8, anyextloadi8, i32>;
defm : ReplicatePeephole<VLREPH, v8i16, anyextloadi16, i32>;
defm : ReplicatePeephole<VLREPF, v4i32, load, i32>;
defm : ReplicatePeephole<VLREPG, v2i64, load, i64>;
defm : ReplicatePeephole<VLREPF, v4f32, load, f32>;
defm : ReplicatePeephole<VLREPG, v2f64, load, f64>;

//===----------------------------------------------------------------------===//
// Stores
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  // Store.
  def VST : StoreVRX<"vst", 0xE70E, null_frag, v128any, 16>;

  // Store with length.  The number of stored bytes is only known at run time.
  def VSTL : StoreLengthVRSb<"vstl", 0xE73F, int_s390_vstl, 0>;

  // Store multiple.
  def VSTM : StoreMultipleVRSa<"vstm", 0xE73E>;

  // Store element.
  def VSTEB : StoreBinaryVRX<"vsteb", 0xE708, z_vstei8,  v128b, 1, imm32zx4>;
  def VSTEH : StoreBinaryVRX<"vsteh", 0xE709, z_vstei16, v128h, 2, imm32zx3>;
  def VSTEF : StoreBinaryVRX<"vstef", 0xE70B, z_vstei32, v128f, 4, imm32zx2>;
  def VSTEG : StoreBinaryVRX<"vsteg", 0xE70A, z_vstei64, v128g, 8, imm32zx1>;
  def : Pat<(z_vstef32 (v4f32 VR128:$val), bdxaddr12only:$addr,
                       imm32zx2:$index),
            (VSTEF VR128:$val, bdxaddr12only:$addr, imm32zx2:$index)>;
  def : Pat<(z_vstef64 (v2f64 VR128:$val), bdxaddr12only:$addr,
                       imm32zx1:$index),
            (VSTEG VR128:$val, bdxaddr12only:$addr, imm32zx1:$index)>;

  // Use VSTE to store subvectors.  These patterns use "12pair" because
  // STEY and STDY offer full 20-bit displacement fields.  It's often better
  // to use those instructions rather than force a 20-bit displacement
  // into a GPR temporary.
  let mayStore = 1 in {
    def VST32 : StoreAliasVRX<store, v32sb, bdxaddr12pair>;
    def VST64 : StoreAliasVRX<store, v64db, bdxaddr12pair>;
  }

  // Scatter element.
  def VSCEF : StoreBinaryVRV<"vscef", 0xE71B, 4, imm32zx2>;
  def VSCEG : StoreBinaryVRV<"vsceg", 0xE71A, 8, imm32zx1>;
}

let Predicates = [FeatureVectorPackedDecimal] in {
  // Store rightmost with length.  The number of stored bytes is only known
  // at run time.
  def VSTRL : StoreLengthVSI<"vstrl", 0xE63D, int_s390_vstrl, 0>;
  def VSTRLR : StoreLengthVRSd<"vstrlr", 0xE63F, int_s390_vstrl, 0>;
}

//===----------------------------------------------------------------------===//
// Selects and permutes
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  // Merge high.
  def VMRH:   BinaryVRRcGeneric<"vmrh", 0xE761>;
  def VMRHB : BinaryVRRc<"vmrhb", 0xE761, z_merge_high, v128b, v128b, 0>;
  def VMRHH : BinaryVRRc<"vmrhh", 0xE761, z_merge_high, v128h, v128h, 1>;
  def VMRHF : BinaryVRRc<"vmrhf", 0xE761, z_merge_high, v128f, v128f, 2>;
  def VMRHG : BinaryVRRc<"vmrhg", 0xE761, z_merge_high, v128g, v128g, 3>;
  def : BinaryRRWithType<VMRHF, VR128, z_merge_high, v4f32>;
  def : BinaryRRWithType<VMRHG, VR128, z_merge_high, v2f64>;

  // Merge low.
  def VMRL:   BinaryVRRcGeneric<"vmrl", 0xE760>;
  def VMRLB : BinaryVRRc<"vmrlb", 0xE760, z_merge_low, v128b, v128b, 0>;
  def VMRLH : BinaryVRRc<"vmrlh", 0xE760, z_merge_low, v128h, v128h, 1>;
  def VMRLF : BinaryVRRc<"vmrlf", 0xE760, z_merge_low, v128f, v128f, 2>;
  def VMRLG : BinaryVRRc<"vmrlg", 0xE760, z_merge_low, v128g, v128g, 3>;
  def : BinaryRRWithType<VMRLF, VR128, z_merge_low, v4f32>;
  def : BinaryRRWithType<VMRLG, VR128, z_merge_low, v2f64>;

  // Permute.
  def VPERM : TernaryVRRe<"vperm", 0xE78C, z_permute, v128b, v128b>;

  // Permute doubleword immediate.
  def VPDI : TernaryVRRc<"vpdi", 0xE784, z_permute_dwords, v128g, v128g>;

  // Bit Permute.
  let Predicates = [FeatureVectorEnhancements1] in
    def VBPERM : BinaryVRRc<"vbperm", 0xE785, int_s390_vbperm, v128g, v128b>;

  // Replicate.
  def VREP:   BinaryVRIcGeneric<"vrep", 0xE74D>;
  def VREPB : BinaryVRIc<"vrepb", 0xE74D, z_splat, v128b, v128b, 0>;
  def VREPH : BinaryVRIc<"vreph", 0xE74D, z_splat, v128h, v128h, 1>;
  def VREPF : BinaryVRIc<"vrepf", 0xE74D, z_splat, v128f, v128f, 2>;
  def VREPG : BinaryVRIc<"vrepg", 0xE74D, z_splat, v128g, v128g, 3>;
  def : Pat<(v4f32 (z_splat VR128:$vec, imm32zx16:$index)),
            (VREPF VR128:$vec, imm32zx16:$index)>;
  def : Pat<(v2f64 (z_splat VR128:$vec, imm32zx16:$index)),
            (VREPG VR128:$vec, imm32zx16:$index)>;

  // Select.
  def VSEL : TernaryVRRe<"vsel", 0xE78D, null_frag, v128any, v128any>;
}

//===----------------------------------------------------------------------===//
// Widening and narrowing
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  // Pack
  def VPK  : BinaryVRRcGeneric<"vpk", 0xE794>;
  def VPKH : BinaryVRRc<"vpkh", 0xE794, z_pack, v128b, v128h, 1>;
  def VPKF : BinaryVRRc<"vpkf", 0xE794, z_pack, v128h, v128f, 2>;
  def VPKG : BinaryVRRc<"vpkg", 0xE794, z_pack, v128f, v128g, 3>;

  // Pack saturate.
  def  VPKS  : BinaryVRRbSPairGeneric<"vpks", 0xE797>;
  defm VPKSH : BinaryVRRbSPair<"vpksh", 0xE797, int_s390_vpksh, z_packs_cc,
                               v128b, v128h, 1>;
  defm VPKSF : BinaryVRRbSPair<"vpksf", 0xE797, int_s390_vpksf, z_packs_cc,
                               v128h, v128f, 2>;
  defm VPKSG : BinaryVRRbSPair<"vpksg", 0xE797, int_s390_vpksg, z_packs_cc,
                               v128f, v128g, 3>;

  // Pack saturate logical.
  def  VPKLS  : BinaryVRRbSPairGeneric<"vpkls", 0xE795>;
  defm VPKLSH : BinaryVRRbSPair<"vpklsh", 0xE795, int_s390_vpklsh, z_packls_cc,
                                v128b, v128h, 1>;
  defm VPKLSF : BinaryVRRbSPair<"vpklsf", 0xE795, int_s390_vpklsf, z_packls_cc,
                                v128h, v128f, 2>;
  defm VPKLSG : BinaryVRRbSPair<"vpklsg", 0xE795, int_s390_vpklsg, z_packls_cc,
                                v128f, v128g, 3>;

  // Sign-extend to doubleword.
  def VSEG  : UnaryVRRaGeneric<"vseg", 0xE75F>;
  def VSEGB : UnaryVRRa<"vsegb", 0xE75F, z_vsei8,  v128g, v128g, 0>;
  def VSEGH : UnaryVRRa<"vsegh", 0xE75F, z_vsei16, v128g, v128g, 1>;
  def VSEGF : UnaryVRRa<"vsegf", 0xE75F, z_vsei32, v128g, v128g, 2>;
  def : Pat<(z_vsei8_by_parts  (v16i8 VR128:$src)), (VSEGB VR128:$src)>;
  def : Pat<(z_vsei16_by_parts (v8i16 VR128:$src)), (VSEGH VR128:$src)>;
  def : Pat<(z_vsei32_by_parts (v4i32 VR128:$src)), (VSEGF VR128:$src)>;

  // Unpack high.
  def VUPH  : UnaryVRRaGeneric<"vuph", 0xE7D7>;
  def VUPHB : UnaryVRRa<"vuphb", 0xE7D7, z_unpack_high, v128h, v128b, 0>;
  def VUPHH : UnaryVRRa<"vuphh", 0xE7D7, z_unpack_high, v128f, v128h, 1>;
  def VUPHF : UnaryVRRa<"vuphf", 0xE7D7, z_unpack_high, v128g, v128f, 2>;

  // Unpack logical high.
  def VUPLH  : UnaryVRRaGeneric<"vuplh", 0xE7D5>;
  def VUPLHB : UnaryVRRa<"vuplhb", 0xE7D5, z_unpackl_high, v128h, v128b, 0>;
  def VUPLHH : UnaryVRRa<"vuplhh", 0xE7D5, z_unpackl_high, v128f, v128h, 1>;
  def VUPLHF : UnaryVRRa<"vuplhf", 0xE7D5, z_unpackl_high, v128g, v128f, 2>;

  // Unpack low.
  def VUPL   : UnaryVRRaGeneric<"vupl", 0xE7D6>;
  def VUPLB  : UnaryVRRa<"vuplb",  0xE7D6, z_unpack_low, v128h, v128b, 0>;
  def VUPLHW : UnaryVRRa<"vuplhw", 0xE7D6, z_unpack_low, v128f, v128h, 1>;
  def VUPLF  : UnaryVRRa<"vuplf",  0xE7D6, z_unpack_low, v128g, v128f, 2>;

  // Unpack logical low.
  def VUPLL  : UnaryVRRaGeneric<"vupll", 0xE7D4>;
  def VUPLLB : UnaryVRRa<"vupllb", 0xE7D4, z_unpackl_low, v128h, v128b, 0>;
  def VUPLLH : UnaryVRRa<"vupllh", 0xE7D4, z_unpackl_low, v128f, v128h, 1>;
  def VUPLLF : UnaryVRRa<"vupllf", 0xE7D4, z_unpackl_low, v128g, v128f, 2>;
}

//===----------------------------------------------------------------------===//
// Instantiating generic operations for specific types.
//===----------------------------------------------------------------------===//

multiclass GenericVectorOps<ValueType type, ValueType inttype> {
  let Predicates = [FeatureVector] in {
    def : Pat<(type (load bdxaddr12only:$addr)),
              (VL bdxaddr12only:$addr)>;
    def : Pat<(store (type VR128:$src), bdxaddr12only:$addr),
              (VST VR128:$src, bdxaddr12only:$addr)>;
    def : Pat<(type (vselect (inttype VR128:$x), VR128:$y, VR128:$z)),
              (VSEL VR128:$y, VR128:$z, VR128:$x)>;
    def : Pat<(type (vselect (inttype (z_vnot VR128:$x)), VR128:$y, VR128:$z)),
              (VSEL VR128:$z, VR128:$y, VR128:$x)>;
  }
}

defm : GenericVectorOps<v16i8, v16i8>;
defm : GenericVectorOps<v8i16, v8i16>;
defm : GenericVectorOps<v4i32, v4i32>;
defm : GenericVectorOps<v2i64, v2i64>;
defm : GenericVectorOps<v4f32, v4i32>;
defm : GenericVectorOps<v2f64, v2i64>;

//===----------------------------------------------------------------------===//
// Integer arithmetic
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  // Add.
  def VA  : BinaryVRRcGeneric<"va", 0xE7F3>;
  def VAB : BinaryVRRc<"vab", 0xE7F3, add, v128b, v128b, 0>;
  def VAH : BinaryVRRc<"vah", 0xE7F3, add, v128h, v128h, 1>;
  def VAF : BinaryVRRc<"vaf", 0xE7F3, add, v128f, v128f, 2>;
  def VAG : BinaryVRRc<"vag", 0xE7F3, add, v128g, v128g, 3>;
  def VAQ : BinaryVRRc<"vaq", 0xE7F3, int_s390_vaq, v128q, v128q, 4>;

  // Add compute carry.
  def VACC  : BinaryVRRcGeneric<"vacc", 0xE7F1>;
  def VACCB : BinaryVRRc<"vaccb", 0xE7F1, int_s390_vaccb, v128b, v128b, 0>;
  def VACCH : BinaryVRRc<"vacch", 0xE7F1, int_s390_vacch, v128h, v128h, 1>;
  def VACCF : BinaryVRRc<"vaccf", 0xE7F1, int_s390_vaccf, v128f, v128f, 2>;
  def VACCG : BinaryVRRc<"vaccg", 0xE7F1, int_s390_vaccg, v128g, v128g, 3>;
  def VACCQ : BinaryVRRc<"vaccq", 0xE7F1, int_s390_vaccq, v128q, v128q, 4>;

  // Add with carry.
  def VAC  : TernaryVRRdGeneric<"vac", 0xE7BB>;
  def VACQ : TernaryVRRd<"vacq", 0xE7BB, int_s390_vacq, v128q, v128q, 4>;

  // Add with carry compute carry.
  def VACCC  : TernaryVRRdGeneric<"vaccc", 0xE7B9>;
  def VACCCQ : TernaryVRRd<"vacccq", 0xE7B9, int_s390_vacccq, v128q, v128q, 4>;

  // And.
  def VN : BinaryVRRc<"vn", 0xE768, null_frag, v128any, v128any>;

  // And with complement.
  def VNC : BinaryVRRc<"vnc", 0xE769, null_frag, v128any, v128any>;

  // Average.
  def VAVG  : BinaryVRRcGeneric<"vavg", 0xE7F2>;
  def VAVGB : BinaryVRRc<"vavgb", 0xE7F2, int_s390_vavgb, v128b, v128b, 0>;
  def VAVGH : BinaryVRRc<"vavgh", 0xE7F2, int_s390_vavgh, v128h, v128h, 1>;
  def VAVGF : BinaryVRRc<"vavgf", 0xE7F2, int_s390_vavgf, v128f, v128f, 2>;
  def VAVGG : BinaryVRRc<"vavgg", 0xE7F2, int_s390_vavgg, v128g, v128g, 3>;

  // Average logical.
  def VAVGL  : BinaryVRRcGeneric<"vavgl", 0xE7F0>;
  def VAVGLB : BinaryVRRc<"vavglb", 0xE7F0, int_s390_vavglb, v128b, v128b, 0>;
  def VAVGLH : BinaryVRRc<"vavglh", 0xE7F0, int_s390_vavglh, v128h, v128h, 1>;
  def VAVGLF : BinaryVRRc<"vavglf", 0xE7F0, int_s390_vavglf, v128f, v128f, 2>;
  def VAVGLG : BinaryVRRc<"vavglg", 0xE7F0, int_s390_vavglg, v128g, v128g, 3>;

  // Checksum.
  def VCKSM : BinaryVRRc<"vcksm", 0xE766, int_s390_vcksm, v128f, v128f>;

  // Count leading zeros.
  def VCLZ  : UnaryVRRaGeneric<"vclz", 0xE753>;
  def VCLZB : UnaryVRRa<"vclzb", 0xE753, ctlz, v128b, v128b, 0>;
  def VCLZH : UnaryVRRa<"vclzh", 0xE753, ctlz, v128h, v128h, 1>;
  def VCLZF : UnaryVRRa<"vclzf", 0xE753, ctlz, v128f, v128f, 2>;
  def VCLZG : UnaryVRRa<"vclzg", 0xE753, ctlz, v128g, v128g, 3>;

  // Count trailing zeros.
  def VCTZ  : UnaryVRRaGeneric<"vctz", 0xE752>;
  def VCTZB : UnaryVRRa<"vctzb", 0xE752, cttz, v128b, v128b, 0>;
  def VCTZH : UnaryVRRa<"vctzh", 0xE752, cttz, v128h, v128h, 1>;
  def VCTZF : UnaryVRRa<"vctzf", 0xE752, cttz, v128f, v128f, 2>;
  def VCTZG : UnaryVRRa<"vctzg", 0xE752, cttz, v128g, v128g, 3>;

  // Not exclusive or.
  let Predicates = [FeatureVectorEnhancements1] in
    def VNX : BinaryVRRc<"vnx", 0xE76C, null_frag, v128any, v128any>;

  // Exclusive or.
  def VX : BinaryVRRc<"vx", 0xE76D, null_frag, v128any, v128any>;

  // Galois field multiply sum.
  def VGFM  : BinaryVRRcGeneric<"vgfm", 0xE7B4>;
  def VGFMB : BinaryVRRc<"vgfmb", 0xE7B4, int_s390_vgfmb, v128h, v128b, 0>;
  def VGFMH : BinaryVRRc<"vgfmh", 0xE7B4, int_s390_vgfmh, v128f, v128h, 1>;
  def VGFMF : BinaryVRRc<"vgfmf", 0xE7B4, int_s390_vgfmf, v128g, v128f, 2>;
  def VGFMG : BinaryVRRc<"vgfmg", 0xE7B4, int_s390_vgfmg, v128q, v128g, 3>;

  // Galois field multiply sum and accumulate.
  def VGFMA  : TernaryVRRdGeneric<"vgfma", 0xE7BC>;
  def VGFMAB : TernaryVRRd<"vgfmab", 0xE7BC, int_s390_vgfmab, v128h, v128b, 0>;
  def VGFMAH : TernaryVRRd<"vgfmah", 0xE7BC, int_s390_vgfmah, v128f, v128h, 1>;
  def VGFMAF : TernaryVRRd<"vgfmaf", 0xE7BC, int_s390_vgfmaf, v128g, v128f, 2>;
  def VGFMAG : TernaryVRRd<"vgfmag", 0xE7BC, int_s390_vgfmag, v128q, v128g, 3>;

  // Load complement.
  def VLC  : UnaryVRRaGeneric<"vlc", 0xE7DE>;
  def VLCB : UnaryVRRa<"vlcb", 0xE7DE, z_vneg, v128b, v128b, 0>;
  def VLCH : UnaryVRRa<"vlch", 0xE7DE, z_vneg, v128h, v128h, 1>;
  def VLCF : UnaryVRRa<"vlcf", 0xE7DE, z_vneg, v128f, v128f, 2>;
  def VLCG : UnaryVRRa<"vlcg", 0xE7DE, z_vneg, v128g, v128g, 3>;

  // Load positive.
  def VLP  : UnaryVRRaGeneric<"vlp", 0xE7DF>;
  def VLPB : UnaryVRRa<"vlpb", 0xE7DF, z_viabs8,  v128b, v128b, 0>;
  def VLPH : UnaryVRRa<"vlph", 0xE7DF, z_viabs16, v128h, v128h, 1>;
  def VLPF : UnaryVRRa<"vlpf", 0xE7DF, z_viabs32, v128f, v128f, 2>;
  def VLPG : UnaryVRRa<"vlpg", 0xE7DF, z_viabs64, v128g, v128g, 3>;

  // Maximum.
  def VMX  : BinaryVRRcGeneric<"vmx", 0xE7FF>;
  def VMXB : BinaryVRRc<"vmxb", 0xE7FF, null_frag, v128b, v128b, 0>;
  def VMXH : BinaryVRRc<"vmxh", 0xE7FF, null_frag, v128h, v128h, 1>;
  def VMXF : BinaryVRRc<"vmxf", 0xE7FF, null_frag, v128f, v128f, 2>;
  def VMXG : BinaryVRRc<"vmxg", 0xE7FF, null_frag, v128g, v128g, 3>;

  // Maximum logical.
  def VMXL  : BinaryVRRcGeneric<"vmxl", 0xE7FD>;
  def VMXLB : BinaryVRRc<"vmxlb", 0xE7FD, null_frag, v128b, v128b, 0>;
  def VMXLH : BinaryVRRc<"vmxlh", 0xE7FD, null_frag, v128h, v128h, 1>;
  def VMXLF : BinaryVRRc<"vmxlf", 0xE7FD, null_frag, v128f, v128f, 2>;
  def VMXLG : BinaryVRRc<"vmxlg", 0xE7FD, null_frag, v128g, v128g, 3>;

  // Minimum.
  def VMN  : BinaryVRRcGeneric<"vmn", 0xE7FE>;
  def VMNB : BinaryVRRc<"vmnb", 0xE7FE, null_frag, v128b, v128b, 0>;
  def VMNH : BinaryVRRc<"vmnh", 0xE7FE, null_frag, v128h, v128h, 1>;
  def VMNF : BinaryVRRc<"vmnf", 0xE7FE, null_frag, v128f, v128f, 2>;
  def VMNG : BinaryVRRc<"vmng", 0xE7FE, null_frag, v128g, v128g, 3>;

  // Minimum logical.
  def VMNL  : BinaryVRRcGeneric<"vmnl", 0xE7FC>;
  def VMNLB : BinaryVRRc<"vmnlb", 0xE7FC, null_frag, v128b, v128b, 0>;
  def VMNLH : BinaryVRRc<"vmnlh", 0xE7FC, null_frag, v128h, v128h, 1>;
  def VMNLF : BinaryVRRc<"vmnlf", 0xE7FC, null_frag, v128f, v128f, 2>;
  def VMNLG : BinaryVRRc<"vmnlg", 0xE7FC, null_frag, v128g, v128g, 3>;

  // Multiply and add low.
  def VMAL   : TernaryVRRdGeneric<"vmal", 0xE7AA>;
  def VMALB  : TernaryVRRd<"vmalb",  0xE7AA, z_muladd, v128b, v128b, 0>;
  def VMALHW : TernaryVRRd<"vmalhw", 0xE7AA, z_muladd, v128h, v128h, 1>;
  def VMALF  : TernaryVRRd<"vmalf",  0xE7AA, z_muladd, v128f, v128f, 2>;

  // Multiply and add high.
  def VMAH  : TernaryVRRdGeneric<"vmah", 0xE7AB>;
  def VMAHB : TernaryVRRd<"vmahb", 0xE7AB, int_s390_vmahb, v128b, v128b, 0>;
  def VMAHH : TernaryVRRd<"vmahh", 0xE7AB, int_s390_vmahh, v128h, v128h, 1>;
  def VMAHF : TernaryVRRd<"vmahf", 0xE7AB, int_s390_vmahf, v128f, v128f, 2>;

  // Multiply and add logical high.
  def VMALH  : TernaryVRRdGeneric<"vmalh", 0xE7A9>;
  def VMALHB : TernaryVRRd<"vmalhb", 0xE7A9, int_s390_vmalhb, v128b, v128b, 0>;
  def VMALHH : TernaryVRRd<"vmalhh", 0xE7A9, int_s390_vmalhh, v128h, v128h, 1>;
  def VMALHF : TernaryVRRd<"vmalhf", 0xE7A9, int_s390_vmalhf, v128f, v128f, 2>;

  // Multiply and add even.
  def VMAE  : TernaryVRRdGeneric<"vmae", 0xE7AE>;
  def VMAEB : TernaryVRRd<"vmaeb", 0xE7AE, int_s390_vmaeb, v128h, v128b, 0>;
  def VMAEH : TernaryVRRd<"vmaeh", 0xE7AE, int_s390_vmaeh, v128f, v128h, 1>;
  def VMAEF : TernaryVRRd<"vmaef", 0xE7AE, int_s390_vmaef, v128g, v128f, 2>;

  // Multiply and add logical even.
  def VMALE  : TernaryVRRdGeneric<"vmale", 0xE7AC>;
  def VMALEB : TernaryVRRd<"vmaleb", 0xE7AC, int_s390_vmaleb, v128h, v128b, 0>;
  def VMALEH : TernaryVRRd<"vmaleh", 0xE7AC, int_s390_vmaleh, v128f, v128h, 1>;
  def VMALEF : TernaryVRRd<"vmalef", 0xE7AC, int_s390_vmalef, v128g, v128f, 2>;

  // Multiply and add odd.
  def VMAO  : TernaryVRRdGeneric<"vmao", 0xE7AF>;
  def VMAOB : TernaryVRRd<"vmaob", 0xE7AF, int_s390_vmaob, v128h, v128b, 0>;
  def VMAOH : TernaryVRRd<"vmaoh", 0xE7AF, int_s390_vmaoh, v128f, v128h, 1>;
  def VMAOF : TernaryVRRd<"vmaof", 0xE7AF, int_s390_vmaof, v128g, v128f, 2>;

  // Multiply and add logical odd.
  def VMALO  : TernaryVRRdGeneric<"vmalo", 0xE7AD>;
  def VMALOB : TernaryVRRd<"vmalob", 0xE7AD, int_s390_vmalob, v128h, v128b, 0>;
  def VMALOH : TernaryVRRd<"vmaloh", 0xE7AD, int_s390_vmaloh, v128f, v128h, 1>;
  def VMALOF : TernaryVRRd<"vmalof", 0xE7AD, int_s390_vmalof, v128g, v128f, 2>;

  // Multiply high.
  def VMH  : BinaryVRRcGeneric<"vmh", 0xE7A3>;
  def VMHB : BinaryVRRc<"vmhb", 0xE7A3, int_s390_vmhb, v128b, v128b, 0>;
  def VMHH : BinaryVRRc<"vmhh", 0xE7A3, int_s390_vmhh, v128h, v128h, 1>;
  def VMHF : BinaryVRRc<"vmhf", 0xE7A3, int_s390_vmhf, v128f, v128f, 2>;

  // Multiply logical high.
  def VMLH  : BinaryVRRcGeneric<"vmlh", 0xE7A1>;
  def VMLHB : BinaryVRRc<"vmlhb", 0xE7A1, int_s390_vmlhb, v128b, v128b, 0>;
  def VMLHH : BinaryVRRc<"vmlhh", 0xE7A1, int_s390_vmlhh, v128h, v128h, 1>;
  def VMLHF : BinaryVRRc<"vmlhf", 0xE7A1, int_s390_vmlhf, v128f, v128f, 2>;

  // Multiply low.
  def VML   : BinaryVRRcGeneric<"vml", 0xE7A2>;
  def VMLB  : BinaryVRRc<"vmlb",  0xE7A2, mul, v128b, v128b, 0>;
  def VMLHW : BinaryVRRc<"vmlhw", 0xE7A2, mul, v128h, v128h, 1>;
  def VMLF  : BinaryVRRc<"vmlf",  0xE7A2, mul, v128f, v128f, 2>;

  // Multiply even.
  def VME  : BinaryVRRcGeneric<"vme", 0xE7A6>;
  def VMEB : BinaryVRRc<"vmeb", 0xE7A6, int_s390_vmeb, v128h, v128b, 0>;
  def VMEH : BinaryVRRc<"vmeh", 0xE7A6, int_s390_vmeh, v128f, v128h, 1>;
  def VMEF : BinaryVRRc<"vmef", 0xE7A6, int_s390_vmef, v128g, v128f, 2>;

  // Multiply logical even.
  def VMLE  : BinaryVRRcGeneric<"vmle", 0xE7A4>;
  def VMLEB : BinaryVRRc<"vmleb", 0xE7A4, int_s390_vmleb, v128h, v128b, 0>;
  def VMLEH : BinaryVRRc<"vmleh", 0xE7A4, int_s390_vmleh, v128f, v128h, 1>;
  def VMLEF : BinaryVRRc<"vmlef", 0xE7A4, int_s390_vmlef, v128g, v128f, 2>;

  // Multiply odd.
  def VMO  : BinaryVRRcGeneric<"vmo", 0xE7A7>;
  def VMOB : BinaryVRRc<"vmob", 0xE7A7, int_s390_vmob, v128h, v128b, 0>;
  def VMOH : BinaryVRRc<"vmoh", 0xE7A7, int_s390_vmoh, v128f, v128h, 1>;
  def VMOF : BinaryVRRc<"vmof", 0xE7A7, int_s390_vmof, v128g, v128f, 2>;

  // Multiply logical odd.
  def VMLO  : BinaryVRRcGeneric<"vmlo", 0xE7A5>;
  def VMLOB : BinaryVRRc<"vmlob", 0xE7A5, int_s390_vmlob, v128h, v128b, 0>;
  def VMLOH : BinaryVRRc<"vmloh", 0xE7A5, int_s390_vmloh, v128f, v128h, 1>;
  def VMLOF : BinaryVRRc<"vmlof", 0xE7A5, int_s390_vmlof, v128g, v128f, 2>;

  // Multiply sum logical.
  let Predicates = [FeatureVectorEnhancements1] in {
    def VMSL  : QuaternaryVRRdGeneric<"vmsl", 0xE7B8>;
    def VMSLG : QuaternaryVRRd<"vmslg", 0xE7B8, int_s390_vmslg,
                               v128q, v128g, v128g, v128q, 3>;
  }

  // Nand.
  let Predicates = [FeatureVectorEnhancements1] in
    def VNN : BinaryVRRc<"vnn", 0xE76E, null_frag, v128any, v128any>;

  // Nor.
  def VNO : BinaryVRRc<"vno", 0xE76B, null_frag, v128any, v128any>;
  def : InstAlias<"vnot\t$V1, $V2", (VNO VR128:$V1, VR128:$V2, VR128:$V2), 0>;

  // Or.
  def VO : BinaryVRRc<"vo", 0xE76A, null_frag, v128any, v128any>;

  // Or with complement.
  let Predicates = [FeatureVectorEnhancements1] in
    def VOC : BinaryVRRc<"voc", 0xE76F, null_frag, v128any, v128any>;

  // Population count.
  def VPOPCT : UnaryVRRaGeneric<"vpopct", 0xE750>;
  def : Pat<(v16i8 (z_popcnt VR128:$x)), (VPOPCT VR128:$x, 0)>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VPOPCTB : UnaryVRRa<"vpopctb", 0xE750, ctpop, v128b, v128b, 0>;
    def VPOPCTH : UnaryVRRa<"vpopcth", 0xE750, ctpop, v128h, v128h, 1>;
    def VPOPCTF : UnaryVRRa<"vpopctf", 0xE750, ctpop, v128f, v128f, 2>;
    def VPOPCTG : UnaryVRRa<"vpopctg", 0xE750, ctpop, v128g, v128g, 3>;
  }

  // Element rotate left logical (with vector shift amount).
  def VERLLV  : BinaryVRRcGeneric<"verllv", 0xE773>;
  def VERLLVB : BinaryVRRc<"verllvb", 0xE773, int_s390_verllvb,
                           v128b, v128b, 0>;
  def VERLLVH : BinaryVRRc<"verllvh", 0xE773, int_s390_verllvh,
                           v128h, v128h, 1>;
  def VERLLVF : BinaryVRRc<"verllvf", 0xE773, int_s390_verllvf,
                           v128f, v128f, 2>;
  def VERLLVG : BinaryVRRc<"verllvg", 0xE773, int_s390_verllvg,
                           v128g, v128g, 3>;

  // Element rotate left logical (with scalar shift amount).
  def VERLL  : BinaryVRSaGeneric<"verll", 0xE733>;
  def VERLLB : BinaryVRSa<"verllb", 0xE733, int_s390_verllb, v128b, v128b, 0>;
  def VERLLH : BinaryVRSa<"verllh", 0xE733, int_s390_verllh, v128h, v128h, 1>;
  def VERLLF : BinaryVRSa<"verllf", 0xE733, int_s390_verllf, v128f, v128f, 2>;
  def VERLLG : BinaryVRSa<"verllg", 0xE733, int_s390_verllg, v128g, v128g, 3>;

  // Element rotate and insert under mask.
  def VERIM  : QuaternaryVRIdGeneric<"verim", 0xE772>;
  def VERIMB : QuaternaryVRId<"verimb", 0xE772, int_s390_verimb, v128b, v128b, 0>;
  def VERIMH : QuaternaryVRId<"verimh", 0xE772, int_s390_verimh, v128h, v128h, 1>;
  def VERIMF : QuaternaryVRId<"verimf", 0xE772, int_s390_verimf, v128f, v128f, 2>;
  def VERIMG : QuaternaryVRId<"verimg", 0xE772, int_s390_verimg, v128g, v128g, 3>;

  // Element shift left (with vector shift amount).
  def VESLV  : BinaryVRRcGeneric<"veslv", 0xE770>;
  def VESLVB : BinaryVRRc<"veslvb", 0xE770, z_vshl, v128b, v128b, 0>;
  def VESLVH : BinaryVRRc<"veslvh", 0xE770, z_vshl, v128h, v128h, 1>;
  def VESLVF : BinaryVRRc<"veslvf", 0xE770, z_vshl, v128f, v128f, 2>;
  def VESLVG : BinaryVRRc<"veslvg", 0xE770, z_vshl, v128g, v128g, 3>;

  // Element shift left (with scalar shift amount).
  def VESL  : BinaryVRSaGeneric<"vesl", 0xE730>;
  def VESLB : BinaryVRSa<"veslb", 0xE730, z_vshl_by_scalar, v128b, v128b, 0>;
  def VESLH : BinaryVRSa<"veslh", 0xE730, z_vshl_by_scalar, v128h, v128h, 1>;
  def VESLF : BinaryVRSa<"veslf", 0xE730, z_vshl_by_scalar, v128f, v128f, 2>;
  def VESLG : BinaryVRSa<"veslg", 0xE730, z_vshl_by_scalar, v128g, v128g, 3>;

  // Element shift right arithmetic (with vector shift amount).
  def VESRAV  : BinaryVRRcGeneric<"vesrav", 0xE77A>;
  def VESRAVB : BinaryVRRc<"vesravb", 0xE77A, z_vsra, v128b, v128b, 0>;
  def VESRAVH : BinaryVRRc<"vesravh", 0xE77A, z_vsra, v128h, v128h, 1>;
  def VESRAVF : BinaryVRRc<"vesravf", 0xE77A, z_vsra, v128f, v128f, 2>;
  def VESRAVG : BinaryVRRc<"vesravg", 0xE77A, z_vsra, v128g, v128g, 3>;

  // Element shift right arithmetic (with scalar shift amount).
  def VESRA  : BinaryVRSaGeneric<"vesra", 0xE73A>;
  def VESRAB : BinaryVRSa<"vesrab", 0xE73A, z_vsra_by_scalar, v128b, v128b, 0>;
  def VESRAH : BinaryVRSa<"vesrah", 0xE73A, z_vsra_by_scalar, v128h, v128h, 1>;
  def VESRAF : BinaryVRSa<"vesraf", 0xE73A, z_vsra_by_scalar, v128f, v128f, 2>;
  def VESRAG : BinaryVRSa<"vesrag", 0xE73A, z_vsra_by_scalar, v128g, v128g, 3>;

  // Element shift right logical (with vector shift amount).
  def VESRLV  : BinaryVRRcGeneric<"vesrlv", 0xE778>;
  def VESRLVB : BinaryVRRc<"vesrlvb", 0xE778, z_vsrl, v128b, v128b, 0>;
  def VESRLVH : BinaryVRRc<"vesrlvh", 0xE778, z_vsrl, v128h, v128h, 1>;
  def VESRLVF : BinaryVRRc<"vesrlvf", 0xE778, z_vsrl, v128f, v128f, 2>;
  def VESRLVG : BinaryVRRc<"vesrlvg", 0xE778, z_vsrl, v128g, v128g, 3>;

  // Element shift right logical (with scalar shift amount).
  def VESRL  : BinaryVRSaGeneric<"vesrl", 0xE738>;
  def VESRLB : BinaryVRSa<"vesrlb", 0xE738, z_vsrl_by_scalar, v128b, v128b, 0>;
  def VESRLH : BinaryVRSa<"vesrlh", 0xE738, z_vsrl_by_scalar, v128h, v128h, 1>;
  def VESRLF : BinaryVRSa<"vesrlf", 0xE738, z_vsrl_by_scalar, v128f, v128f, 2>;
  def VESRLG : BinaryVRSa<"vesrlg", 0xE738, z_vsrl_by_scalar, v128g, v128g, 3>;

  // Shift left.
  def VSL : BinaryVRRc<"vsl", 0xE774, int_s390_vsl, v128b, v128b>;

  // Shift left by byte.
  def VSLB : BinaryVRRc<"vslb", 0xE775, int_s390_vslb, v128b, v128b>;

  // Shift left double by byte.
  def VSLDB : TernaryVRId<"vsldb", 0xE777, z_shl_double, v128b, v128b, 0>;
  def : Pat<(int_s390_vsldb VR128:$x, VR128:$y, imm32zx8:$z),
            (VSLDB VR128:$x, VR128:$y, imm32zx8:$z)>;

  // Shift right arithmetic.
  def VSRA : BinaryVRRc<"vsra", 0xE77E, int_s390_vsra, v128b, v128b>;

  // Shift right arithmetic by byte.
  def VSRAB : BinaryVRRc<"vsrab", 0xE77F, int_s390_vsrab, v128b, v128b>;

  // Shift right logical.
  def VSRL : BinaryVRRc<"vsrl", 0xE77C, int_s390_vsrl, v128b, v128b>;

  // Shift right logical by byte.
  def VSRLB : BinaryVRRc<"vsrlb", 0xE77D, int_s390_vsrlb, v128b, v128b>;

  // Subtract.
  def VS  : BinaryVRRcGeneric<"vs", 0xE7F7>;
  def VSB : BinaryVRRc<"vsb", 0xE7F7, sub, v128b, v128b, 0>;
  def VSH : BinaryVRRc<"vsh", 0xE7F7, sub, v128h, v128h, 1>;
  def VSF : BinaryVRRc<"vsf", 0xE7F7, sub, v128f, v128f, 2>;
  def VSG : BinaryVRRc<"vsg", 0xE7F7, sub, v128g, v128g, 3>;
  def VSQ : BinaryVRRc<"vsq", 0xE7F7, int_s390_vsq, v128q, v128q, 4>;

  // Subtract compute borrow indication.
  def VSCBI  : BinaryVRRcGeneric<"vscbi", 0xE7F5>;
  def VSCBIB : BinaryVRRc<"vscbib", 0xE7F5, int_s390_vscbib, v128b, v128b, 0>;
  def VSCBIH : BinaryVRRc<"vscbih", 0xE7F5, int_s390_vscbih, v128h, v128h, 1>;
  def VSCBIF : BinaryVRRc<"vscbif", 0xE7F5, int_s390_vscbif, v128f, v128f, 2>;
  def VSCBIG : BinaryVRRc<"vscbig", 0xE7F5, int_s390_vscbig, v128g, v128g, 3>;
  def VSCBIQ : BinaryVRRc<"vscbiq", 0xE7F5, int_s390_vscbiq, v128q, v128q, 4>;

  // Subtract with borrow indication.
  def VSBI  : TernaryVRRdGeneric<"vsbi", 0xE7BF>;
  def VSBIQ : TernaryVRRd<"vsbiq", 0xE7BF, int_s390_vsbiq, v128q, v128q, 4>;

  // Subtract with borrow compute borrow indication.
  def VSBCBI  : TernaryVRRdGeneric<"vsbcbi", 0xE7BD>;
  def VSBCBIQ : TernaryVRRd<"vsbcbiq", 0xE7BD, int_s390_vsbcbiq,
                            v128q, v128q, 4>;

  // Sum across doubleword.
  def VSUMG  : BinaryVRRcGeneric<"vsumg", 0xE765>;
  def VSUMGH : BinaryVRRc<"vsumgh", 0xE765, z_vsum, v128g, v128h, 1>;
  def VSUMGF : BinaryVRRc<"vsumgf", 0xE765, z_vsum, v128g, v128f, 2>;

  // Sum across quadword.
  def VSUMQ  : BinaryVRRcGeneric<"vsumq", 0xE767>;
  def VSUMQF : BinaryVRRc<"vsumqf", 0xE767, z_vsum, v128q, v128f, 2>;
  def VSUMQG : BinaryVRRc<"vsumqg", 0xE767, z_vsum, v128q, v128g, 3>;

  // Sum across word.
  def VSUM  : BinaryVRRcGeneric<"vsum", 0xE764>;
  def VSUMB : BinaryVRRc<"vsumb", 0xE764, z_vsum, v128f, v128b, 0>;
  def VSUMH : BinaryVRRc<"vsumh", 0xE764, z_vsum, v128f, v128h, 1>;
}

// Instantiate the bitwise ops for type TYPE.
multiclass BitwiseVectorOps<ValueType type> {
  let Predicates = [FeatureVector] in {
    def : Pat<(type (and VR128:$x, VR128:$y)), (VN VR128:$x, VR128:$y)>;
    def : Pat<(type (and VR128:$x, (z_vnot VR128:$y))),
              (VNC VR128:$x, VR128:$y)>;
    def : Pat<(type (or VR128:$x, VR128:$y)), (VO VR128:$x, VR128:$y)>;
    def : Pat<(type (xor VR128:$x, VR128:$y)), (VX VR128:$x, VR128:$y)>;
    def : Pat<(type (or (and VR128:$x, VR128:$z),
                        (and VR128:$y, (z_vnot VR128:$z)))),
              (VSEL VR128:$x, VR128:$y, VR128:$z)>;
    def : Pat<(type (z_vnot (or VR128:$x, VR128:$y))),
              (VNO VR128:$x, VR128:$y)>;
    def : Pat<(type (z_vnot VR128:$x)), (VNO VR128:$x, VR128:$x)>;
  }
  let Predicates = [FeatureVectorEnhancements1] in {
    def : Pat<(type (z_vnot (xor VR128:$x, VR128:$y))),
              (VNX VR128:$x, VR128:$y)>;
    def : Pat<(type (z_vnot (and VR128:$x, VR128:$y))),
              (VNN VR128:$x, VR128:$y)>;
    def : Pat<(type (or VR128:$x, (z_vnot VR128:$y))),
              (VOC VR128:$x, VR128:$y)>;
  }
}

defm : BitwiseVectorOps<v16i8>;
defm : BitwiseVectorOps<v8i16>;
defm : BitwiseVectorOps<v4i32>;
defm : BitwiseVectorOps<v2i64>;

// Instantiate additional patterns for absolute-related expressions on
// type TYPE.  LC is the negate instruction for TYPE and LP is the absolute
// instruction.
multiclass IntegerAbsoluteVectorOps<ValueType type, Instruction lc,
                                    Instruction lp, int shift> {
  let Predicates = [FeatureVector] in {
    def : Pat<(type (vselect (type (z_vicmph_zero VR128:$x)),
                             (z_vneg VR128:$x), VR128:$x)),
              (lc (lp VR128:$x))>;
    def : Pat<(type (vselect (type (z_vnot (z_vicmph_zero VR128:$x))),
                             VR128:$x, (z_vneg VR128:$x))),
              (lc (lp VR128:$x))>;
    def : Pat<(type (vselect (type (z_vicmpl_zero VR128:$x)),
                             VR128:$x, (z_vneg VR128:$x))),
              (lc (lp VR128:$x))>;
    def : Pat<(type (vselect (type (z_vnot (z_vicmpl_zero VR128:$x))),
                             (z_vneg VR128:$x), VR128:$x)),
              (lc (lp VR128:$x))>;
    def : Pat<(type (or (and (z_vsra_by_scalar VR128:$x, (i32 shift)),
                             (z_vneg VR128:$x)),
                        (and (z_vnot (z_vsra_by_scalar VR128:$x, (i32 shift))),
                             VR128:$x))),
              (lp VR128:$x)>;
    def : Pat<(type (or (and (z_vsra_by_scalar VR128:$x, (i32 shift)),
                             VR128:$x),
                        (and (z_vnot (z_vsra_by_scalar VR128:$x, (i32 shift))),
                             (z_vneg VR128:$x)))),
              (lc (lp VR128:$x))>;
  }
}

defm : IntegerAbsoluteVectorOps<v16i8, VLCB, VLPB, 7>;
defm : IntegerAbsoluteVectorOps<v8i16, VLCH, VLPH, 15>;
defm : IntegerAbsoluteVectorOps<v4i32, VLCF, VLPF, 31>;
defm : IntegerAbsoluteVectorOps<v2i64, VLCG, VLPG, 63>;

// Instantiate minimum- and maximum-related patterns for TYPE.  CMPH is the
// signed or unsigned "set if greater than" comparison instruction and
// MIN and MAX are the associated minimum and maximum instructions.
multiclass IntegerMinMaxVectorOps<ValueType type, SDPatternOperator cmph,
                                  Instruction min, Instruction max> {
  let Predicates = [FeatureVector] in {
    def : Pat<(type (vselect (cmph VR128:$x, VR128:$y), VR128:$x, VR128:$y)),
              (max VR128:$x, VR128:$y)>;
    def : Pat<(type (vselect (cmph VR128:$x, VR128:$y), VR128:$y, VR128:$x)),
              (min VR128:$x, VR128:$y)>;
    def : Pat<(type (vselect (z_vnot (cmph VR128:$x, VR128:$y)),
                             VR128:$x, VR128:$y)),
              (min VR128:$x, VR128:$y)>;
    def : Pat<(type (vselect (z_vnot (cmph VR128:$x, VR128:$y)),
                             VR128:$y, VR128:$x)),
              (max VR128:$x, VR128:$y)>;
  }
}

// Signed min/max.
defm : IntegerMinMaxVectorOps<v16i8, z_vicmph, VMNB, VMXB>;
defm : IntegerMinMaxVectorOps<v8i16, z_vicmph, VMNH, VMXH>;
defm : IntegerMinMaxVectorOps<v4i32, z_vicmph, VMNF, VMXF>;
defm : IntegerMinMaxVectorOps<v2i64, z_vicmph, VMNG, VMXG>;

// Unsigned min/max.
defm : IntegerMinMaxVectorOps<v16i8, z_vicmphl, VMNLB, VMXLB>;
defm : IntegerMinMaxVectorOps<v8i16, z_vicmphl, VMNLH, VMXLH>;
defm : IntegerMinMaxVectorOps<v4i32, z_vicmphl, VMNLF, VMXLF>;
defm : IntegerMinMaxVectorOps<v2i64, z_vicmphl, VMNLG, VMXLG>;

//===----------------------------------------------------------------------===//
// Integer comparison
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  // Element compare.
  let Defs = [CC] in {
    def VEC  : CompareVRRaGeneric<"vec", 0xE7DB>;
    def VECB : CompareVRRa<"vecb", 0xE7DB, null_frag, v128b, 0>;
    def VECH : CompareVRRa<"vech", 0xE7DB, null_frag, v128h, 1>;
    def VECF : CompareVRRa<"vecf", 0xE7DB, null_frag, v128f, 2>;
    def VECG : CompareVRRa<"vecg", 0xE7DB, null_frag, v128g, 3>;
  }

  // Element compare logical.
  let Defs = [CC] in {
    def VECL  : CompareVRRaGeneric<"vecl", 0xE7D9>;
    def VECLB : CompareVRRa<"veclb", 0xE7D9, null_frag, v128b, 0>;
    def VECLH : CompareVRRa<"veclh", 0xE7D9, null_frag, v128h, 1>;
    def VECLF : CompareVRRa<"veclf", 0xE7D9, null_frag, v128f, 2>;
    def VECLG : CompareVRRa<"veclg", 0xE7D9, null_frag, v128g, 3>;
  }

  // Compare equal.
  def  VCEQ  : BinaryVRRbSPairGeneric<"vceq", 0xE7F8>;
  defm VCEQB : BinaryVRRbSPair<"vceqb", 0xE7F8, z_vicmpe, z_vicmpes,
                               v128b, v128b, 0>;
  defm VCEQH : BinaryVRRbSPair<"vceqh", 0xE7F8, z_vicmpe, z_vicmpes,
                               v128h, v128h, 1>;
  defm VCEQF : BinaryVRRbSPair<"vceqf", 0xE7F8, z_vicmpe, z_vicmpes,
                               v128f, v128f, 2>;
  defm VCEQG : BinaryVRRbSPair<"vceqg", 0xE7F8, z_vicmpe, z_vicmpes,
                               v128g, v128g, 3>;

  // Compare high.
  def  VCH  : BinaryVRRbSPairGeneric<"vch", 0xE7FB>;
  defm VCHB : BinaryVRRbSPair<"vchb", 0xE7FB, z_vicmph, z_vicmphs,
                              v128b, v128b, 0>;
  defm VCHH : BinaryVRRbSPair<"vchh", 0xE7FB, z_vicmph, z_vicmphs,
                              v128h, v128h, 1>;
  defm VCHF : BinaryVRRbSPair<"vchf", 0xE7FB, z_vicmph, z_vicmphs,
                              v128f, v128f, 2>;
  defm VCHG : BinaryVRRbSPair<"vchg", 0xE7FB, z_vicmph, z_vicmphs,
                              v128g, v128g, 3>;

  // Compare high logical.
  def  VCHL  : BinaryVRRbSPairGeneric<"vchl", 0xE7F9>;
  defm VCHLB : BinaryVRRbSPair<"vchlb", 0xE7F9, z_vicmphl, z_vicmphls,
                               v128b, v128b, 0>;
  defm VCHLH : BinaryVRRbSPair<"vchlh", 0xE7F9, z_vicmphl, z_vicmphls,
                               v128h, v128h, 1>;
  defm VCHLF : BinaryVRRbSPair<"vchlf", 0xE7F9, z_vicmphl, z_vicmphls,
                               v128f, v128f, 2>;
  defm VCHLG : BinaryVRRbSPair<"vchlg", 0xE7F9, z_vicmphl, z_vicmphls,
                               v128g, v128g, 3>;

  // Test under mask.
  let Defs = [CC] in
    def VTM : CompareVRRa<"vtm", 0xE7D8, z_vtm, v128b, 0>;
}

//===----------------------------------------------------------------------===//
// Floating-point arithmetic
//===----------------------------------------------------------------------===//

// See comments in SystemZInstrFP.td for the suppression flags and
// rounding modes.
multiclass VectorRounding<Instruction insn, TypedReg tr> {
  def : FPConversion<insn, frint,      tr, tr, 0, 0>;
  def : FPConversion<insn, fnearbyint, tr, tr, 4, 0>;
  def : FPConversion<insn, ffloor,     tr, tr, 4, 7>;
  def : FPConversion<insn, fceil,      tr, tr, 4, 6>;
  def : FPConversion<insn, ftrunc,     tr, tr, 4, 5>;
  def : FPConversion<insn, fround,     tr, tr, 4, 1>;
}

let Predicates = [FeatureVector] in {
  // Add.
  def VFA   : BinaryVRRcFloatGeneric<"vfa", 0xE7E3>;
  def VFADB : BinaryVRRc<"vfadb", 0xE7E3, fadd, v128db, v128db, 3, 0>;
  def WFADB : BinaryVRRc<"wfadb", 0xE7E3, fadd, v64db, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFASB : BinaryVRRc<"vfasb", 0xE7E3, fadd, v128sb, v128sb, 2, 0>;
    def WFASB : BinaryVRRc<"wfasb", 0xE7E3, fadd, v32sb, v32sb, 2, 8>;
    def WFAXB : BinaryVRRc<"wfaxb", 0xE7E3, fadd, v128xb, v128xb, 4, 8>;
  }

  // Convert from fixed 64-bit.
  def VCDG  : TernaryVRRaFloatGeneric<"vcdg", 0xE7C3>;
  def VCDGB : TernaryVRRa<"vcdgb", 0xE7C3, null_frag, v128db, v128g, 3, 0>;
  def WCDGB : TernaryVRRa<"wcdgb", 0xE7C3, null_frag, v64db, v64g, 3, 8>;
  def : FPConversion<VCDGB, sint_to_fp, v128db, v128g, 0, 0>;

  // Convert from logical 64-bit.
  def VCDLG  : TernaryVRRaFloatGeneric<"vcdlg", 0xE7C1>;
  def VCDLGB : TernaryVRRa<"vcdlgb", 0xE7C1, null_frag, v128db, v128g, 3, 0>;
  def WCDLGB : TernaryVRRa<"wcdlgb", 0xE7C1, null_frag, v64db, v64g, 3, 8>;
  def : FPConversion<VCDLGB, uint_to_fp, v128db, v128g, 0, 0>;

  // Convert to fixed 64-bit.
  def VCGD  : TernaryVRRaFloatGeneric<"vcgd", 0xE7C2>;
  def VCGDB : TernaryVRRa<"vcgdb", 0xE7C2, null_frag, v128g, v128db, 3, 0>;
  def WCGDB : TernaryVRRa<"wcgdb", 0xE7C2, null_frag, v64g, v64db, 3, 8>;
  // Rounding mode should agree with SystemZInstrFP.td.
  def : FPConversion<VCGDB, fp_to_sint, v128g, v128db, 0, 5>;

  // Convert to logical 64-bit.
  def VCLGD  : TernaryVRRaFloatGeneric<"vclgd", 0xE7C0>;
  def VCLGDB : TernaryVRRa<"vclgdb", 0xE7C0, null_frag, v128g, v128db, 3, 0>;
  def WCLGDB : TernaryVRRa<"wclgdb", 0xE7C0, null_frag, v64g, v64db, 3, 8>;
  // Rounding mode should agree with SystemZInstrFP.td.
  def : FPConversion<VCLGDB, fp_to_uint, v128g, v128db, 0, 5>;

  // Divide.
  def VFD   : BinaryVRRcFloatGeneric<"vfd", 0xE7E5>;
  def VFDDB : BinaryVRRc<"vfddb", 0xE7E5, fdiv, v128db, v128db, 3, 0>;
  def WFDDB : BinaryVRRc<"wfddb", 0xE7E5, fdiv, v64db, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFDSB : BinaryVRRc<"vfdsb", 0xE7E5, fdiv, v128sb, v128sb, 2, 0>;
    def WFDSB : BinaryVRRc<"wfdsb", 0xE7E5, fdiv, v32sb, v32sb, 2, 8>;
    def WFDXB : BinaryVRRc<"wfdxb", 0xE7E5, fdiv, v128xb, v128xb, 4, 8>;
  }

  // Load FP integer.
  def VFI   : TernaryVRRaFloatGeneric<"vfi", 0xE7C7>;
  def VFIDB : TernaryVRRa<"vfidb", 0xE7C7, int_s390_vfidb, v128db, v128db, 3, 0>;
  def WFIDB : TernaryVRRa<"wfidb", 0xE7C7, null_frag, v64db, v64db, 3, 8>;
  defm : VectorRounding<VFIDB, v128db>;
  defm : VectorRounding<WFIDB, v64db>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFISB : TernaryVRRa<"vfisb", 0xE7C7, int_s390_vfisb, v128sb, v128sb, 2, 0>;
    def WFISB : TernaryVRRa<"wfisb", 0xE7C7, null_frag, v32sb, v32sb, 2, 8>;
    def WFIXB : TernaryVRRa<"wfixb", 0xE7C7, null_frag, v128xb, v128xb, 4, 8>;
    defm : VectorRounding<VFISB, v128sb>;
    defm : VectorRounding<WFISB, v32sb>;
    defm : VectorRounding<WFIXB, v128xb>;
  }

  // Load lengthened.
  def VLDE  : UnaryVRRaFloatGeneric<"vlde", 0xE7C4>;
  def VLDEB : UnaryVRRa<"vldeb", 0xE7C4, z_vextend, v128db, v128sb, 2, 0>;
  def WLDEB : UnaryVRRa<"wldeb", 0xE7C4, fpextend, v64db, v32sb, 2, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    let isAsmParserOnly = 1 in {
      def VFLL  : UnaryVRRaFloatGeneric<"vfll", 0xE7C4>;
      def VFLLS : UnaryVRRa<"vflls", 0xE7C4, null_frag, v128db, v128sb, 2, 0>;
      def WFLLS : UnaryVRRa<"wflls", 0xE7C4, null_frag, v64db, v32sb, 2, 8>;
    }
    def WFLLD : UnaryVRRa<"wflld", 0xE7C4, fpextend, v128xb, v64db, 3, 8>;
    def : Pat<(f128 (fpextend (f32 VR32:$src))),
              (WFLLD (WLDEB VR32:$src))>;
  }

  // Load rounded.
  def VLED  : TernaryVRRaFloatGeneric<"vled", 0xE7C5>;
  def VLEDB : TernaryVRRa<"vledb", 0xE7C5, null_frag, v128sb, v128db, 3, 0>;
  def WLEDB : TernaryVRRa<"wledb", 0xE7C5, null_frag, v32sb, v64db, 3, 8>;
  def : Pat<(v4f32 (z_vround (v2f64 VR128:$src))), (VLEDB VR128:$src, 0, 0)>;
  def : FPConversion<WLEDB, fpround, v32sb, v64db, 0, 0>;
  let Predicates = [FeatureVectorEnhancements1] in {
    let isAsmParserOnly = 1 in {
      def VFLR  : TernaryVRRaFloatGeneric<"vflr", 0xE7C5>;
      def VFLRD : TernaryVRRa<"vflrd", 0xE7C5, null_frag, v128sb, v128db, 3, 0>;
      def WFLRD : TernaryVRRa<"wflrd", 0xE7C5, null_frag, v32sb, v64db, 3, 8>;
    }
    def WFLRX : TernaryVRRa<"wflrx", 0xE7C5, null_frag, v64db, v128xb, 4, 8>;
    def : FPConversion<WFLRX, fpround, v64db, v128xb, 0, 0>;
    def : Pat<(f32 (fpround (f128 VR128:$src))),
              (WLEDB (WFLRX VR128:$src, 0, 3), 0, 0)>;
  }

  // Maximum.
  multiclass VectorMax<Instruction insn, TypedReg tr> {
    def : FPMinMax<insn, fmaxnum, tr, 4>;
    def : FPMinMax<insn, fmaximum, tr, 1>;
  }
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFMAX   : TernaryVRRcFloatGeneric<"vfmax", 0xE7EF>;
    def VFMAXDB : TernaryVRRcFloat<"vfmaxdb", 0xE7EF, int_s390_vfmaxdb,
                                   v128db, v128db, 3, 0>;
    def WFMAXDB : TernaryVRRcFloat<"wfmaxdb", 0xE7EF, null_frag,
                                   v64db, v64db, 3, 8>;
    def VFMAXSB : TernaryVRRcFloat<"vfmaxsb", 0xE7EF, int_s390_vfmaxsb,
                                   v128sb, v128sb, 2, 0>;
    def WFMAXSB : TernaryVRRcFloat<"wfmaxsb", 0xE7EF, null_frag,
                                   v32sb, v32sb, 2, 8>;
    def WFMAXXB : TernaryVRRcFloat<"wfmaxxb", 0xE7EF, null_frag,
                                   v128xb, v128xb, 4, 8>;
    defm : VectorMax<VFMAXDB, v128db>;
    defm : VectorMax<WFMAXDB, v64db>;
    defm : VectorMax<VFMAXSB, v128sb>;
    defm : VectorMax<WFMAXSB, v32sb>;
    defm : VectorMax<WFMAXXB, v128xb>;
  }

  // Minimum.
  multiclass VectorMin<Instruction insn, TypedReg tr> {
    def : FPMinMax<insn, fminnum, tr, 4>;
    def : FPMinMax<insn, fminimum, tr, 1>;
  }
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFMIN   : TernaryVRRcFloatGeneric<"vfmin", 0xE7EE>;
    def VFMINDB : TernaryVRRcFloat<"vfmindb", 0xE7EE, int_s390_vfmindb,
                                   v128db, v128db, 3, 0>;
    def WFMINDB : TernaryVRRcFloat<"wfmindb", 0xE7EE, null_frag,
                                   v64db, v64db, 3, 8>;
    def VFMINSB : TernaryVRRcFloat<"vfminsb", 0xE7EE, int_s390_vfminsb,
                                   v128sb, v128sb, 2, 0>;
    def WFMINSB : TernaryVRRcFloat<"wfminsb", 0xE7EE, null_frag,
                                   v32sb, v32sb, 2, 8>;
    def WFMINXB : TernaryVRRcFloat<"wfminxb", 0xE7EE, null_frag,
                                   v128xb, v128xb, 4, 8>;
    defm : VectorMin<VFMINDB, v128db>;
    defm : VectorMin<WFMINDB, v64db>;
    defm : VectorMin<VFMINSB, v128sb>;
    defm : VectorMin<WFMINSB, v32sb>;
    defm : VectorMin<WFMINXB, v128xb>;
  }

  // Multiply.
  def VFM   : BinaryVRRcFloatGeneric<"vfm", 0xE7E7>;
  def VFMDB : BinaryVRRc<"vfmdb", 0xE7E7, fmul, v128db, v128db, 3, 0>;
  def WFMDB : BinaryVRRc<"wfmdb", 0xE7E7, fmul, v64db, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFMSB : BinaryVRRc<"vfmsb", 0xE7E7, fmul, v128sb, v128sb, 2, 0>;
    def WFMSB : BinaryVRRc<"wfmsb", 0xE7E7, fmul, v32sb, v32sb, 2, 8>;
    def WFMXB : BinaryVRRc<"wfmxb", 0xE7E7, fmul, v128xb, v128xb, 4, 8>;
  }

  // Multiply and add.
  def VFMA   : TernaryVRReFloatGeneric<"vfma", 0xE78F>;
  def VFMADB : TernaryVRRe<"vfmadb", 0xE78F, fma, v128db, v128db, 0, 3>;
  def WFMADB : TernaryVRRe<"wfmadb", 0xE78F, fma, v64db, v64db, 8, 3>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFMASB : TernaryVRRe<"vfmasb", 0xE78F, fma, v128sb, v128sb, 0, 2>;
    def WFMASB : TernaryVRRe<"wfmasb", 0xE78F, fma, v32sb, v32sb, 8, 2>;
    def WFMAXB : TernaryVRRe<"wfmaxb", 0xE78F, fma, v128xb, v128xb, 8, 4>;
  }

  // Multiply and subtract.
  def VFMS   : TernaryVRReFloatGeneric<"vfms", 0xE78E>;
  def VFMSDB : TernaryVRRe<"vfmsdb", 0xE78E, fms, v128db, v128db, 0, 3>;
  def WFMSDB : TernaryVRRe<"wfmsdb", 0xE78E, fms, v64db, v64db, 8, 3>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFMSSB : TernaryVRRe<"vfmssb", 0xE78E, fms, v128sb, v128sb, 0, 2>;
    def WFMSSB : TernaryVRRe<"wfmssb", 0xE78E, fms, v32sb, v32sb, 8, 2>;
    def WFMSXB : TernaryVRRe<"wfmsxb", 0xE78E, fms, v128xb, v128xb, 8, 4>;
  }

  // Negative multiply and add.
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFNMA   : TernaryVRReFloatGeneric<"vfnma", 0xE79F>;
    def VFNMADB : TernaryVRRe<"vfnmadb", 0xE79F, fnma, v128db, v128db, 0, 3>;
    def WFNMADB : TernaryVRRe<"wfnmadb", 0xE79F, fnma, v64db, v64db, 8, 3>;
    def VFNMASB : TernaryVRRe<"vfnmasb", 0xE79F, fnma, v128sb, v128sb, 0, 2>;
    def WFNMASB : TernaryVRRe<"wfnmasb", 0xE79F, fnma, v32sb, v32sb, 8, 2>;
    def WFNMAXB : TernaryVRRe<"wfnmaxb", 0xE79F, fnma, v128xb, v128xb, 8, 4>;
  }

  // Negative multiply and subtract.
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFNMS   : TernaryVRReFloatGeneric<"vfnms", 0xE79E>;
    def VFNMSDB : TernaryVRRe<"vfnmsdb", 0xE79E, fnms, v128db, v128db, 0, 3>;
    def WFNMSDB : TernaryVRRe<"wfnmsdb", 0xE79E, fnms, v64db, v64db, 8, 3>;
    def VFNMSSB : TernaryVRRe<"vfnmssb", 0xE79E, fnms, v128sb, v128sb, 0, 2>;
    def WFNMSSB : TernaryVRRe<"wfnmssb", 0xE79E, fnms, v32sb, v32sb, 8, 2>;
    def WFNMSXB : TernaryVRRe<"wfnmsxb", 0xE79E, fnms, v128xb, v128xb, 8, 4>;
  }

  // Perform sign operation.
  def VFPSO   : BinaryVRRaFloatGeneric<"vfpso", 0xE7CC>;
  def VFPSODB : BinaryVRRa<"vfpsodb", 0xE7CC, null_frag, v128db, v128db, 3, 0>;
  def WFPSODB : BinaryVRRa<"wfpsodb", 0xE7CC, null_frag, v64db, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFPSOSB : BinaryVRRa<"vfpsosb", 0xE7CC, null_frag, v128sb, v128sb, 2, 0>;
    def WFPSOSB : BinaryVRRa<"wfpsosb", 0xE7CC, null_frag, v32sb, v32sb, 2, 8>;
    def WFPSOXB : BinaryVRRa<"wfpsoxb", 0xE7CC, null_frag, v128xb, v128xb, 4, 8>;
  }

  // Load complement.
  def VFLCDB : UnaryVRRa<"vflcdb", 0xE7CC, fneg, v128db, v128db, 3, 0, 0>;
  def WFLCDB : UnaryVRRa<"wflcdb", 0xE7CC, fneg, v64db, v64db, 3, 8, 0>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFLCSB : UnaryVRRa<"vflcsb", 0xE7CC, fneg, v128sb, v128sb, 2, 0, 0>;
    def WFLCSB : UnaryVRRa<"wflcsb", 0xE7CC, fneg, v32sb, v32sb, 2, 8, 0>;
    def WFLCXB : UnaryVRRa<"wflcxb", 0xE7CC, fneg, v128xb, v128xb, 4, 8, 0>;
  }

  // Load negative.
  def VFLNDB : UnaryVRRa<"vflndb", 0xE7CC, fnabs, v128db, v128db, 3, 0, 1>;
  def WFLNDB : UnaryVRRa<"wflndb", 0xE7CC, fnabs, v64db, v64db, 3, 8, 1>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFLNSB : UnaryVRRa<"vflnsb", 0xE7CC, fnabs, v128sb, v128sb, 2, 0, 1>;
    def WFLNSB : UnaryVRRa<"wflnsb", 0xE7CC, fnabs, v32sb, v32sb, 2, 8, 1>;
    def WFLNXB : UnaryVRRa<"wflnxb", 0xE7CC, fnabs, v128xb, v128xb, 4, 8, 1>;
  }

  // Load positive.
  def VFLPDB : UnaryVRRa<"vflpdb", 0xE7CC, fabs, v128db, v128db, 3, 0, 2>;
  def WFLPDB : UnaryVRRa<"wflpdb", 0xE7CC, fabs, v64db, v64db, 3, 8, 2>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFLPSB : UnaryVRRa<"vflpsb", 0xE7CC, fabs, v128sb, v128sb, 2, 0, 2>;
    def WFLPSB : UnaryVRRa<"wflpsb", 0xE7CC, fabs, v32sb, v32sb, 2, 8, 2>;
    def WFLPXB : UnaryVRRa<"wflpxb", 0xE7CC, fabs, v128xb, v128xb, 4, 8, 2>;
  }

  // Square root.
  def VFSQ   : UnaryVRRaFloatGeneric<"vfsq", 0xE7CE>;
  def VFSQDB : UnaryVRRa<"vfsqdb", 0xE7CE, fsqrt, v128db, v128db, 3, 0>;
  def WFSQDB : UnaryVRRa<"wfsqdb", 0xE7CE, fsqrt, v64db, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFSQSB : UnaryVRRa<"vfsqsb", 0xE7CE, fsqrt, v128sb, v128sb, 2, 0>;
    def WFSQSB : UnaryVRRa<"wfsqsb", 0xE7CE, fsqrt, v32sb, v32sb, 2, 8>;
    def WFSQXB : UnaryVRRa<"wfsqxb", 0xE7CE, fsqrt, v128xb, v128xb, 4, 8>;
  }

  // Subtract.
  def VFS   : BinaryVRRcFloatGeneric<"vfs", 0xE7E2>;
  def VFSDB : BinaryVRRc<"vfsdb", 0xE7E2, fsub, v128db, v128db, 3, 0>;
  def WFSDB : BinaryVRRc<"wfsdb", 0xE7E2, fsub, v64db, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    def VFSSB : BinaryVRRc<"vfssb", 0xE7E2, fsub, v128sb, v128sb, 2, 0>;
    def WFSSB : BinaryVRRc<"wfssb", 0xE7E2, fsub, v32sb, v32sb, 2, 8>;
    def WFSXB : BinaryVRRc<"wfsxb", 0xE7E2, fsub, v128xb, v128xb, 4, 8>;
  }

  // Test data class immediate.
  let Defs = [CC] in {
    def VFTCI   : BinaryVRIeFloatGeneric<"vftci", 0xE74A>;
    def VFTCIDB : BinaryVRIe<"vftcidb", 0xE74A, z_vftci, v128g, v128db, 3, 0>;
    def WFTCIDB : BinaryVRIe<"wftcidb", 0xE74A, null_frag, v64g, v64db, 3, 8>;
    let Predicates = [FeatureVectorEnhancements1] in {
      def VFTCISB : BinaryVRIe<"vftcisb", 0xE74A, z_vftci, v128f, v128sb, 2, 0>;
      def WFTCISB : BinaryVRIe<"wftcisb", 0xE74A, null_frag, v32f, v32sb, 2, 8>;
      def WFTCIXB : BinaryVRIe<"wftcixb", 0xE74A, null_frag, v128q, v128xb, 4, 8>;
    }
  }
}

//===----------------------------------------------------------------------===//
// Floating-point comparison
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  // Compare scalar.
  let Defs = [CC] in {
    def WFC   : CompareVRRaFloatGeneric<"wfc", 0xE7CB>;
    def WFCDB : CompareVRRa<"wfcdb", 0xE7CB, z_fcmp, v64db, 3>;
    let Predicates = [FeatureVectorEnhancements1] in {
      def WFCSB : CompareVRRa<"wfcsb", 0xE7CB, z_fcmp, v32sb, 2>;
      def WFCXB : CompareVRRa<"wfcxb", 0xE7CB, z_fcmp, v128xb, 4>;
    }
  }

  // Compare and signal scalar.
  let Defs = [CC] in {
    def WFK   : CompareVRRaFloatGeneric<"wfk", 0xE7CA>;
    def WFKDB : CompareVRRa<"wfkdb", 0xE7CA, null_frag, v64db, 3>;
    let Predicates = [FeatureVectorEnhancements1] in {
      def WFKSB : CompareVRRa<"wfksb", 0xE7CA, null_frag, v32sb, 2>;
      def WFKXB : CompareVRRa<"wfkxb", 0xE7CA, null_frag, v128xb, 4>;
    }
  }

  // Compare equal.
  def  VFCE   : BinaryVRRcSPairFloatGeneric<"vfce", 0xE7E8>;
  defm VFCEDB : BinaryVRRcSPair<"vfcedb", 0xE7E8, z_vfcmpe, z_vfcmpes,
                                v128g, v128db, 3, 0>;
  defm WFCEDB : BinaryVRRcSPair<"wfcedb", 0xE7E8, null_frag, null_frag,
                                v64g, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    defm VFCESB : BinaryVRRcSPair<"vfcesb", 0xE7E8, z_vfcmpe, z_vfcmpes,
                                  v128f, v128sb, 2, 0>;
    defm WFCESB : BinaryVRRcSPair<"wfcesb", 0xE7E8, null_frag, null_frag,
                                  v32f, v32sb, 2, 8>;
    defm WFCEXB : BinaryVRRcSPair<"wfcexb", 0xE7E8, null_frag, null_frag,
                                  v128q, v128xb, 4, 8>;
  }

  // Compare and signal equal.
  let Predicates = [FeatureVectorEnhancements1] in {
    defm VFKEDB : BinaryVRRcSPair<"vfkedb", 0xE7E8, null_frag, null_frag,
                                  v128g, v128db, 3, 4>;
    defm WFKEDB : BinaryVRRcSPair<"wfkedb", 0xE7E8, null_frag, null_frag,
                                  v64g, v64db, 3, 12>;
    defm VFKESB : BinaryVRRcSPair<"vfkesb", 0xE7E8, null_frag, null_frag,
                                  v128f, v128sb, 2, 4>;
    defm WFKESB : BinaryVRRcSPair<"wfkesb", 0xE7E8, null_frag, null_frag,
                                  v32f, v32sb, 2, 12>;
    defm WFKEXB : BinaryVRRcSPair<"wfkexb", 0xE7E8, null_frag, null_frag,
                                  v128q, v128xb, 4, 12>;
  }

  // Compare high.
  def  VFCH   : BinaryVRRcSPairFloatGeneric<"vfch", 0xE7EB>;
  defm VFCHDB : BinaryVRRcSPair<"vfchdb", 0xE7EB, z_vfcmph, z_vfcmphs,
                                v128g, v128db, 3, 0>;
  defm WFCHDB : BinaryVRRcSPair<"wfchdb", 0xE7EB, null_frag, null_frag,
                                v64g, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    defm VFCHSB : BinaryVRRcSPair<"vfchsb", 0xE7EB, z_vfcmph, z_vfcmphs,
                                  v128f, v128sb, 2, 0>;
    defm WFCHSB : BinaryVRRcSPair<"wfchsb", 0xE7EB, null_frag, null_frag,
                                  v32f, v32sb, 2, 8>;
    defm WFCHXB : BinaryVRRcSPair<"wfchxb", 0xE7EB, null_frag, null_frag,
                                  v128q, v128xb, 4, 8>;
  }

  // Compare and signal high.
  let Predicates = [FeatureVectorEnhancements1] in {
    defm VFKHDB : BinaryVRRcSPair<"vfkhdb", 0xE7EB, null_frag, null_frag,
                                  v128g, v128db, 3, 4>;
    defm WFKHDB : BinaryVRRcSPair<"wfkhdb", 0xE7EB, null_frag, null_frag,
                                  v64g, v64db, 3, 12>;
    defm VFKHSB : BinaryVRRcSPair<"vfkhsb", 0xE7EB, null_frag, null_frag,
                                  v128f, v128sb, 2, 4>;
    defm WFKHSB : BinaryVRRcSPair<"wfkhsb", 0xE7EB, null_frag, null_frag,
                                  v32f, v32sb, 2, 12>;
    defm WFKHXB : BinaryVRRcSPair<"wfkhxb", 0xE7EB, null_frag, null_frag,
                                  v128q, v128xb, 4, 12>;
  }

  // Compare high or equal.
  def  VFCHE   : BinaryVRRcSPairFloatGeneric<"vfche", 0xE7EA>;
  defm VFCHEDB : BinaryVRRcSPair<"vfchedb", 0xE7EA, z_vfcmphe, z_vfcmphes,
                                 v128g, v128db, 3, 0>;
  defm WFCHEDB : BinaryVRRcSPair<"wfchedb", 0xE7EA, null_frag, null_frag,
                                 v64g, v64db, 3, 8>;
  let Predicates = [FeatureVectorEnhancements1] in {
    defm VFCHESB : BinaryVRRcSPair<"vfchesb", 0xE7EA, z_vfcmphe, z_vfcmphes,
                                   v128f, v128sb, 2, 0>;
    defm WFCHESB : BinaryVRRcSPair<"wfchesb", 0xE7EA, null_frag, null_frag,
                                   v32f, v32sb, 2, 8>;
    defm WFCHEXB : BinaryVRRcSPair<"wfchexb", 0xE7EA, null_frag, null_frag,
                                   v128q, v128xb, 4, 8>;
  }

  // Compare and signal high or equal.
  let Predicates = [FeatureVectorEnhancements1] in {
    defm VFKHEDB : BinaryVRRcSPair<"vfkhedb", 0xE7EA, null_frag, null_frag,
                                   v128g, v128db, 3, 4>;
    defm WFKHEDB : BinaryVRRcSPair<"wfkhedb", 0xE7EA, null_frag, null_frag,
                                   v64g, v64db, 3, 12>;
    defm VFKHESB : BinaryVRRcSPair<"vfkhesb", 0xE7EA, null_frag, null_frag,
                                   v128f, v128sb, 2, 4>;
    defm WFKHESB : BinaryVRRcSPair<"wfkhesb", 0xE7EA, null_frag, null_frag,
                                   v32f, v32sb, 2, 12>;
    defm WFKHEXB : BinaryVRRcSPair<"wfkhexb", 0xE7EA, null_frag, null_frag,
                                   v128q, v128xb, 4, 12>;
  }
}

//===----------------------------------------------------------------------===//
// Conversions
//===----------------------------------------------------------------------===//

def : Pat<(v16i8 (bitconvert (v8i16 VR128:$src))), (v16i8 VR128:$src)>;
def : Pat<(v16i8 (bitconvert (v4i32 VR128:$src))), (v16i8 VR128:$src)>;
def : Pat<(v16i8 (bitconvert (v2i64 VR128:$src))), (v16i8 VR128:$src)>;
def : Pat<(v16i8 (bitconvert (v4f32 VR128:$src))), (v16i8 VR128:$src)>;
def : Pat<(v16i8 (bitconvert (v2f64 VR128:$src))), (v16i8 VR128:$src)>;
def : Pat<(v16i8 (bitconvert (f128  VR128:$src))), (v16i8 VR128:$src)>;

def : Pat<(v8i16 (bitconvert (v16i8 VR128:$src))), (v8i16 VR128:$src)>;
def : Pat<(v8i16 (bitconvert (v4i32 VR128:$src))), (v8i16 VR128:$src)>;
def : Pat<(v8i16 (bitconvert (v2i64 VR128:$src))), (v8i16 VR128:$src)>;
def : Pat<(v8i16 (bitconvert (v4f32 VR128:$src))), (v8i16 VR128:$src)>;
def : Pat<(v8i16 (bitconvert (v2f64 VR128:$src))), (v8i16 VR128:$src)>;
def : Pat<(v8i16 (bitconvert (f128  VR128:$src))), (v8i16 VR128:$src)>;

def : Pat<(v4i32 (bitconvert (v16i8 VR128:$src))), (v4i32 VR128:$src)>;
def : Pat<(v4i32 (bitconvert (v8i16 VR128:$src))), (v4i32 VR128:$src)>;
def : Pat<(v4i32 (bitconvert (v2i64 VR128:$src))), (v4i32 VR128:$src)>;
def : Pat<(v4i32 (bitconvert (v4f32 VR128:$src))), (v4i32 VR128:$src)>;
def : Pat<(v4i32 (bitconvert (v2f64 VR128:$src))), (v4i32 VR128:$src)>;
def : Pat<(v4i32 (bitconvert (f128  VR128:$src))), (v4i32 VR128:$src)>;

def : Pat<(v2i64 (bitconvert (v16i8 VR128:$src))), (v2i64 VR128:$src)>;
def : Pat<(v2i64 (bitconvert (v8i16 VR128:$src))), (v2i64 VR128:$src)>;
def : Pat<(v2i64 (bitconvert (v4i32 VR128:$src))), (v2i64 VR128:$src)>;
def : Pat<(v2i64 (bitconvert (v4f32 VR128:$src))), (v2i64 VR128:$src)>;
def : Pat<(v2i64 (bitconvert (v2f64 VR128:$src))), (v2i64 VR128:$src)>;
def : Pat<(v2i64 (bitconvert (f128  VR128:$src))), (v2i64 VR128:$src)>;

def : Pat<(v4f32 (bitconvert (v16i8 VR128:$src))), (v4f32 VR128:$src)>;
def : Pat<(v4f32 (bitconvert (v8i16 VR128:$src))), (v4f32 VR128:$src)>;
def : Pat<(v4f32 (bitconvert (v4i32 VR128:$src))), (v4f32 VR128:$src)>;
def : Pat<(v4f32 (bitconvert (v2i64 VR128:$src))), (v4f32 VR128:$src)>;
def : Pat<(v4f32 (bitconvert (v2f64 VR128:$src))), (v4f32 VR128:$src)>;
def : Pat<(v4f32 (bitconvert (f128  VR128:$src))), (v4f32 VR128:$src)>;

def : Pat<(v2f64 (bitconvert (v16i8 VR128:$src))), (v2f64 VR128:$src)>;
def : Pat<(v2f64 (bitconvert (v8i16 VR128:$src))), (v2f64 VR128:$src)>;
def : Pat<(v2f64 (bitconvert (v4i32 VR128:$src))), (v2f64 VR128:$src)>;
def : Pat<(v2f64 (bitconvert (v2i64 VR128:$src))), (v2f64 VR128:$src)>;
def : Pat<(v2f64 (bitconvert (v4f32 VR128:$src))), (v2f64 VR128:$src)>;
def : Pat<(v2f64 (bitconvert (f128  VR128:$src))), (v2f64 VR128:$src)>;

def : Pat<(f128  (bitconvert (v16i8 VR128:$src))), (f128  VR128:$src)>;
def : Pat<(f128  (bitconvert (v8i16 VR128:$src))), (f128  VR128:$src)>;
def : Pat<(f128  (bitconvert (v4i32 VR128:$src))), (f128  VR128:$src)>;
def : Pat<(f128  (bitconvert (v2i64 VR128:$src))), (f128  VR128:$src)>;
def : Pat<(f128  (bitconvert (v4f32 VR128:$src))), (f128  VR128:$src)>;
def : Pat<(f128  (bitconvert (v2f64 VR128:$src))), (f128  VR128:$src)>;

//===----------------------------------------------------------------------===//
// Replicating scalars
//===----------------------------------------------------------------------===//

// Define patterns for replicating a scalar GR32 into a vector of type TYPE.
// INDEX is 8 minus the element size in bytes.
class VectorReplicateScalar<ValueType type, Instruction insn, bits<16> index>
  : Pat<(type (z_replicate GR32:$scalar)),
        (insn (VLVGP32 GR32:$scalar, GR32:$scalar), index)>;

def : VectorReplicateScalar<v16i8, VREPB, 7>;
def : VectorReplicateScalar<v8i16, VREPH, 3>;
def : VectorReplicateScalar<v4i32, VREPF, 1>;

// i64 replications are just a single isntruction.
def : Pat<(v2i64 (z_replicate GR64:$scalar)),
          (VLVGP GR64:$scalar, GR64:$scalar)>;

//===----------------------------------------------------------------------===//
// Floating-point insertion and extraction
//===----------------------------------------------------------------------===//

// Moving 32-bit values between GPRs and FPRs can be done using VLVGF
// and VLGVF.
let Predicates = [FeatureVector] in {
  def LEFR : UnaryAliasVRS<VR32, GR32>;
  def LFER : UnaryAliasVRS<GR64, VR32>;
  def : Pat<(f32 (bitconvert (i32 GR32:$src))), (LEFR GR32:$src)>;
  def : Pat<(i32 (bitconvert (f32 VR32:$src))),
            (EXTRACT_SUBREG (LFER VR32:$src), subreg_l32)>;
}

// Floating-point values are stored in element 0 of the corresponding
// vector register.  Scalar to vector conversion is just a subreg and
// scalar replication can just replicate element 0 of the vector register.
multiclass ScalarToVectorFP<Instruction vrep, ValueType vt, RegisterOperand cls,
                            SubRegIndex subreg> {
  def : Pat<(vt (scalar_to_vector cls:$scalar)),
            (INSERT_SUBREG (vt (IMPLICIT_DEF)), cls:$scalar, subreg)>;
  def : Pat<(vt (z_replicate cls:$scalar)),
            (vrep (INSERT_SUBREG (vt (IMPLICIT_DEF)), cls:$scalar,
                                 subreg), 0)>;
}
defm : ScalarToVectorFP<VREPF, v4f32, FP32, subreg_h32>;
defm : ScalarToVectorFP<VREPG, v2f64, FP64, subreg_h64>;

// Match v2f64 insertions.  The AddedComplexity counters the 3 added by
// TableGen for the base register operand in VLVG-based integer insertions
// and ensures that this version is strictly better.
let AddedComplexity = 4 in {
  def : Pat<(z_vector_insert (v2f64 VR128:$vec), FP64:$elt, 0),
            (VPDI (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), FP64:$elt,
                                 subreg_h64), VR128:$vec, 1)>;
  def : Pat<(z_vector_insert (v2f64 VR128:$vec), FP64:$elt, 1),
            (VPDI VR128:$vec, (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), FP64:$elt,
                                             subreg_h64), 0)>;
}

// We extract floating-point element X by replicating (for elements other
// than 0) and then taking a high subreg.  The AddedComplexity counters the
// 3 added by TableGen for the base register operand in VLGV-based integer
// extractions and ensures that this version is strictly better.
let AddedComplexity = 4 in {
  def : Pat<(f32 (z_vector_extract (v4f32 VR128:$vec), 0)),
            (EXTRACT_SUBREG VR128:$vec, subreg_h32)>;
  def : Pat<(f32 (z_vector_extract (v4f32 VR128:$vec), imm32zx2:$index)),
            (EXTRACT_SUBREG (VREPF VR128:$vec, imm32zx2:$index), subreg_h32)>;

  def : Pat<(f64 (z_vector_extract (v2f64 VR128:$vec), 0)),
            (EXTRACT_SUBREG VR128:$vec, subreg_h64)>;
  def : Pat<(f64 (z_vector_extract (v2f64 VR128:$vec), imm32zx1:$index)),
            (EXTRACT_SUBREG (VREPG VR128:$vec, imm32zx1:$index), subreg_h64)>;
}

//===----------------------------------------------------------------------===//
// Support for 128-bit floating-point values in vector registers
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVectorEnhancements1] in {
  def : Pat<(f128 (load bdxaddr12only:$addr)),
            (VL bdxaddr12only:$addr)>;
  def : Pat<(store (f128 VR128:$src), bdxaddr12only:$addr),
            (VST VR128:$src, bdxaddr12only:$addr)>;

  def : Pat<(f128 fpimm0), (VZERO)>;
  def : Pat<(f128 fpimmneg0), (WFLNXB (VZERO))>;
}

//===----------------------------------------------------------------------===//
// String instructions
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVector] in {
  defm VFAE  : TernaryOptVRRbSPairGeneric<"vfae", 0xE782>;
  defm VFAEB : TernaryOptVRRbSPair<"vfaeb", 0xE782, int_s390_vfaeb,
                                   z_vfae_cc, v128b, v128b, 0>;
  defm VFAEH : TernaryOptVRRbSPair<"vfaeh", 0xE782, int_s390_vfaeh,
                                   z_vfae_cc, v128h, v128h, 1>;
  defm VFAEF : TernaryOptVRRbSPair<"vfaef", 0xE782, int_s390_vfaef,
                                   z_vfae_cc, v128f, v128f, 2>;
  defm VFAEZB : TernaryOptVRRbSPair<"vfaezb", 0xE782, int_s390_vfaezb,
                                    z_vfaez_cc, v128b, v128b, 0, 2>;
  defm VFAEZH : TernaryOptVRRbSPair<"vfaezh", 0xE782, int_s390_vfaezh,
                                    z_vfaez_cc, v128h, v128h, 1, 2>;
  defm VFAEZF : TernaryOptVRRbSPair<"vfaezf", 0xE782, int_s390_vfaezf,
                                    z_vfaez_cc, v128f, v128f, 2, 2>;

  defm VFEE  : BinaryExtraVRRbSPairGeneric<"vfee", 0xE780>;
  defm VFEEB : BinaryExtraVRRbSPair<"vfeeb", 0xE780, int_s390_vfeeb,
                                    z_vfee_cc, v128b, v128b, 0>;
  defm VFEEH : BinaryExtraVRRbSPair<"vfeeh", 0xE780, int_s390_vfeeh,
                                    z_vfee_cc, v128h, v128h, 1>;
  defm VFEEF : BinaryExtraVRRbSPair<"vfeef", 0xE780, int_s390_vfeef,
                                    z_vfee_cc, v128f, v128f, 2>;
  defm VFEEZB : BinaryVRRbSPair<"vfeezb", 0xE780, int_s390_vfeezb,
                                z_vfeez_cc, v128b, v128b, 0, 2>;
  defm VFEEZH : BinaryVRRbSPair<"vfeezh", 0xE780, int_s390_vfeezh,
                                z_vfeez_cc, v128h, v128h, 1, 2>;
  defm VFEEZF : BinaryVRRbSPair<"vfeezf", 0xE780, int_s390_vfeezf,
                                z_vfeez_cc, v128f, v128f, 2, 2>;

  defm VFENE  : BinaryExtraVRRbSPairGeneric<"vfene", 0xE781>;
  defm VFENEB : BinaryExtraVRRbSPair<"vfeneb", 0xE781, int_s390_vfeneb,
                                     z_vfene_cc, v128b, v128b, 0>;
  defm VFENEH : BinaryExtraVRRbSPair<"vfeneh", 0xE781, int_s390_vfeneh,
                                     z_vfene_cc, v128h, v128h, 1>;
  defm VFENEF : BinaryExtraVRRbSPair<"vfenef", 0xE781, int_s390_vfenef,
                                     z_vfene_cc, v128f, v128f, 2>;
  defm VFENEZB : BinaryVRRbSPair<"vfenezb", 0xE781, int_s390_vfenezb,
                                 z_vfenez_cc, v128b, v128b, 0, 2>;
  defm VFENEZH : BinaryVRRbSPair<"vfenezh", 0xE781, int_s390_vfenezh,
                                 z_vfenez_cc, v128h, v128h, 1, 2>;
  defm VFENEZF : BinaryVRRbSPair<"vfenezf", 0xE781, int_s390_vfenezf,
                                 z_vfenez_cc, v128f, v128f, 2, 2>;

  defm VISTR  : UnaryExtraVRRaSPairGeneric<"vistr", 0xE75C>;
  defm VISTRB : UnaryExtraVRRaSPair<"vistrb", 0xE75C, int_s390_vistrb,
                                    z_vistr_cc, v128b, v128b, 0>;
  defm VISTRH : UnaryExtraVRRaSPair<"vistrh", 0xE75C, int_s390_vistrh,
                                    z_vistr_cc, v128h, v128h, 1>;
  defm VISTRF : UnaryExtraVRRaSPair<"vistrf", 0xE75C, int_s390_vistrf,
                                    z_vistr_cc, v128f, v128f, 2>;

  defm VSTRC  : QuaternaryOptVRRdSPairGeneric<"vstrc", 0xE78A>;
  defm VSTRCB : QuaternaryOptVRRdSPair<"vstrcb", 0xE78A, int_s390_vstrcb,
                                       z_vstrc_cc, v128b, v128b, 0>;
  defm VSTRCH : QuaternaryOptVRRdSPair<"vstrch", 0xE78A, int_s390_vstrch,
                                       z_vstrc_cc, v128h, v128h, 1>;
  defm VSTRCF : QuaternaryOptVRRdSPair<"vstrcf", 0xE78A, int_s390_vstrcf,
                                       z_vstrc_cc, v128f, v128f, 2>;
  defm VSTRCZB : QuaternaryOptVRRdSPair<"vstrczb", 0xE78A, int_s390_vstrczb,
                                        z_vstrcz_cc, v128b, v128b, 0, 2>;
  defm VSTRCZH : QuaternaryOptVRRdSPair<"vstrczh", 0xE78A, int_s390_vstrczh,
                                        z_vstrcz_cc, v128h, v128h, 1, 2>;
  defm VSTRCZF : QuaternaryOptVRRdSPair<"vstrczf", 0xE78A, int_s390_vstrczf,
                                        z_vstrcz_cc, v128f, v128f, 2, 2>;
}

//===----------------------------------------------------------------------===//
// Packed-decimal instructions
//===----------------------------------------------------------------------===//

let Predicates = [FeatureVectorPackedDecimal] in {
  def VLIP : BinaryVRIh<"vlip", 0xE649>;

  def VPKZ : BinaryVSI<"vpkz", 0xE634, null_frag, 0>;
  def VUPKZ : StoreLengthVSI<"vupkz", 0xE63C, null_frag, 0>;

  let Defs = [CC] in {
    def VCVB : BinaryVRRi<"vcvb", 0xE650, GR32>;
    def VCVBG : BinaryVRRi<"vcvbg", 0xE652, GR64>;
    def VCVD : TernaryVRIi<"vcvd", 0xE658, GR32>;
    def VCVDG : TernaryVRIi<"vcvdg", 0xE65A, GR64>;

    def VAP : QuaternaryVRIf<"vap", 0xE671>;
    def VSP : QuaternaryVRIf<"vsp", 0xE673>;

    def VMP : QuaternaryVRIf<"vmp", 0xE678>;
    def VMSP : QuaternaryVRIf<"vmsp", 0xE679>;

    def VDP : QuaternaryVRIf<"vdp", 0xE67A>;
    def VRP : QuaternaryVRIf<"vrp", 0xE67B>;
    def VSDP : QuaternaryVRIf<"vsdp", 0xE67E>;

    def VSRP : QuaternaryVRIg<"vsrp", 0xE659>;
    def VPSOP : QuaternaryVRIg<"vpsop", 0xE65B>;

    def VTP : TestVRRg<"vtp", 0xE65F>;
    def VCP : CompareVRRh<"vcp", 0xE677>;
  }
}
