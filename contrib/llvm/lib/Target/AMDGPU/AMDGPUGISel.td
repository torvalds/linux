//===-- AMDGPUGIsel.td - AMDGPU GlobalISel Patterns---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This files contains patterns that should only be used by GlobalISel.  For
// example patterns for V_* instructions that have S_* equivalents.
// SelectionDAG does not support selecting V_* instructions.
//===----------------------------------------------------------------------===//

include "AMDGPU.td"

def sd_vsrc0 : ComplexPattern<i32, 1, "">;
def gi_vsrc0 :
    GIComplexOperandMatcher<s32, "selectVSRC0">,
    GIComplexPatternEquiv<sd_vsrc0>;

def sd_vcsrc : ComplexPattern<i32, 1, "">;
def gi_vcsrc :
    GIComplexOperandMatcher<s32, "selectVCSRC">,
    GIComplexPatternEquiv<sd_vcsrc>;

def gi_vop3mods0 :
    GIComplexOperandMatcher<s32, "selectVOP3Mods0">,
    GIComplexPatternEquiv<VOP3Mods0>;

def gi_vop3mods :
    GIComplexOperandMatcher<s32, "selectVOP3Mods">,
    GIComplexPatternEquiv<VOP3Mods>;

def gi_vop3omods :
    GIComplexOperandMatcher<s32, "selectVOP3OMods">,
    GIComplexPatternEquiv<VOP3OMods>;

class GISelSop2Pat <
  SDPatternOperator node,
  Instruction inst,
  ValueType dst_vt,
  ValueType src0_vt = dst_vt, ValueType src1_vt = src0_vt>   : GCNPat <

  (dst_vt (node (src0_vt SReg_32:$src0), (src1_vt SReg_32:$src1))),
  (inst src0_vt:$src0, src1_vt:$src1)
>;

class GISelVop2Pat <
  SDPatternOperator node,
  Instruction inst,
  ValueType dst_vt,
  ValueType src0_vt = dst_vt, ValueType src1_vt = src0_vt>   : GCNPat <

  (dst_vt (node (src0_vt (sd_vsrc0 src0_vt:$src0)), (src1_vt VGPR_32:$src1))),
  (inst src0_vt:$src0, src1_vt:$src1)
>;

class GISelVop2CommutePat <
  SDPatternOperator node,
  Instruction inst,
  ValueType dst_vt,
  ValueType src0_vt = dst_vt, ValueType src1_vt = src0_vt>   : GCNPat <

  (dst_vt (node (src1_vt VGPR_32:$src1), (src0_vt (sd_vsrc0 src0_vt:$src0)))),
  (inst src0_vt:$src0, src1_vt:$src1)
>;

class GISelVop3Pat2 <
  SDPatternOperator node,
  Instruction inst,
  ValueType dst_vt,
  ValueType src0_vt = dst_vt, ValueType src1_vt = src0_vt>   : GCNPat <

  (dst_vt (node (src0_vt (sd_vcsrc src0_vt:$src0)), (src1_vt (sd_vcsrc src1_vt:$src1)))),
  (inst src0_vt:$src0, src1_vt:$src1)
>;

class GISelVop3Pat2CommutePat <
  SDPatternOperator node,
  Instruction inst,
  ValueType dst_vt,
  ValueType src0_vt = dst_vt, ValueType src1_vt = src0_vt>   : GCNPat <

  (dst_vt (node (src0_vt (sd_vcsrc src0_vt:$src0)), (src1_vt (sd_vcsrc src1_vt:$src1)))),
  (inst src0_vt:$src1, src1_vt:$src0)
>;

class GISelVop3Pat2ModsPat <
  SDPatternOperator node,
  Instruction inst,
  ValueType dst_vt,
  ValueType src0_vt = dst_vt, ValueType src1_vt = src0_vt> : GCNPat <

  (dst_vt (node (src0_vt (VOP3Mods0 src0_vt:$src0, i32:$src0_modifiers, i1:$clamp, i32:$omods)),
                (src1_vt (VOP3Mods src1_vt:$src1, i32:$src1_modifiers)))),
  (inst i32:$src0_modifiers, src0_vt:$src0,
        i32:$src1_modifiers, src1_vt:$src1, $clamp, $omods)
>;

multiclass GISelVop2IntrPat <
  SDPatternOperator node, Instruction inst,
  ValueType dst_vt, ValueType src_vt = dst_vt> {

  def : GISelVop2Pat <node, inst, dst_vt, src_vt>;

  // FIXME: Intrinsics aren't marked as commutable, so we need to add an explcit
  // pattern to handle commuting.  This is another reason why legalizing to a
  // generic machine instruction may be better that matching the intrinsic
  // directly.
  def : GISelVop2CommutePat <node, inst, dst_vt, src_vt>;
}

def : GISelSop2Pat <or, S_OR_B32, i32>;
def : GISelVop2Pat <or, V_OR_B32_e32, i32>;

def : GISelSop2Pat <sra, S_ASHR_I32, i32>;
let AddedComplexity = 100 in {
let SubtargetPredicate = isSICI in {
def : GISelVop2Pat <sra, V_ASHR_I32_e32, i32>;
}
def : GISelVop2CommutePat <sra, V_ASHRREV_I32_e32, i32>;
}
def : GISelVop3Pat2CommutePat <sra, V_ASHRREV_I32_e64, i32>;

// FIXME: We can't re-use SelectionDAG patterns here because they match
// against a custom SDNode and we would need to create a generic machine
// instruction that is equivalent to the custom SDNode.  This would also require
// us to custom legalize the intrinsic to the new generic machine instruction,
// but I can't get custom legalizing of intrinsic to work and I'm not sure if
// this is even supported yet.
def : GISelVop3Pat2ModsPat <
  int_amdgcn_cvt_pkrtz, V_CVT_PKRTZ_F16_F32_e64, v2f16, f32>;

defm : GISelVop2IntrPat <int_maxnum, V_MAX_F32_e32, f32>;
def : GISelVop3Pat2ModsPat <int_maxnum, V_MAX_F64, f64>;
defm : GISelVop2IntrPat <int_minnum, V_MIN_F32_e32, f32>;
def : GISelVop3Pat2ModsPat <int_minnum, V_MIN_F64, f64>;
