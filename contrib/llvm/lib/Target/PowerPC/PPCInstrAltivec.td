//===-- PPCInstrAltivec.td - The PowerPC Altivec Extension -*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the Altivec extension to the PowerPC instruction set.
//
//===----------------------------------------------------------------------===//

// *********************************** NOTE ***********************************
// ** For POWER8 Little Endian, the VSX swap optimization relies on knowing  **
// ** which VMX and VSX instructions are lane-sensitive and which are not.   **
// ** A lane-sensitive instruction relies, implicitly or explicitly, on      **
// ** whether lanes are numbered from left to right.  An instruction like    **
// ** VADDFP is not lane-sensitive, because each lane of the result vector   **
// ** relies only on the corresponding lane of the source vectors.  However, **
// ** an instruction like VMULESB is lane-sensitive, because "even" and      **
// ** "odd" lanes are different for big-endian and little-endian numbering.  **
// **                                                                        **
// ** When adding new VMX and VSX instructions, please consider whether they **
// ** are lane-sensitive.  If so, they must be added to a switch statement   **
// ** in PPCVSXSwapRemoval::gatherVectorInstructions().                      **
// ****************************************************************************


//===----------------------------------------------------------------------===//
// Altivec transformation functions and pattern fragments.
//

// Since we canonicalize buildvectors to v16i8, all vnots "-1" operands will be
// of that type.
def vnot_ppc : PatFrag<(ops node:$in),
                       (xor node:$in, (bitconvert (v16i8 immAllOnesV)))>;

def vpkuhum_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                              (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUHUMShuffleMask(cast<ShuffleVectorSDNode>(N), 0, *CurDAG);
}]>;
def vpkuwum_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                              (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUWUMShuffleMask(cast<ShuffleVectorSDNode>(N), 0, *CurDAG);
}]>;
def vpkudum_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                              (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUDUMShuffleMask(cast<ShuffleVectorSDNode>(N), 0, *CurDAG);
}]>;
def vpkuhum_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                    (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUHUMShuffleMask(cast<ShuffleVectorSDNode>(N), 1, *CurDAG);
}]>;
def vpkuwum_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                    (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUWUMShuffleMask(cast<ShuffleVectorSDNode>(N), 1, *CurDAG);
}]>;
def vpkudum_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                    (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUDUMShuffleMask(cast<ShuffleVectorSDNode>(N), 1, *CurDAG);
}]>;

// These fragments are provided for little-endian, where the inputs must be
// swapped for correct semantics.
def vpkuhum_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                      (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUHUMShuffleMask(cast<ShuffleVectorSDNode>(N), 2, *CurDAG);
}]>;
def vpkuwum_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                      (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUWUMShuffleMask(cast<ShuffleVectorSDNode>(N), 2, *CurDAG);
}]>;
def vpkudum_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                      (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVPKUDUMShuffleMask(cast<ShuffleVectorSDNode>(N), 2, *CurDAG);
}]>;

def vmrglb_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle (v16i8 node:$lhs), node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 1, 0, *CurDAG);
}]>;
def vmrglh_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle (v16i8 node:$lhs), node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 2, 0, *CurDAG);
}]>;
def vmrglw_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle (v16i8 node:$lhs), node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 4, 0, *CurDAG);
}]>;
def vmrghb_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle (v16i8 node:$lhs), node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 1, 0, *CurDAG);
}]>;
def vmrghh_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle (v16i8 node:$lhs), node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 2, 0, *CurDAG);
}]>;
def vmrghw_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle (v16i8 node:$lhs), node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 4, 0, *CurDAG);
}]>;


def vmrglb_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                               (vector_shuffle (v16i8 node:$lhs), node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 1, 1, *CurDAG);
}]>;
def vmrglh_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 2, 1, *CurDAG);
}]>;
def vmrglw_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 4, 1, *CurDAG);
}]>;
def vmrghb_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 1, 1, *CurDAG);
}]>;
def vmrghh_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 2, 1, *CurDAG);
}]>;
def vmrghw_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 4, 1, *CurDAG);
}]>;


// These fragments are provided for little-endian, where the inputs must be
// swapped for correct semantics.
def vmrglb_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                               (vector_shuffle (v16i8 node:$lhs), node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 1, 2, *CurDAG);
}]>;
def vmrglh_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 2, 2, *CurDAG);
}]>;
def vmrglw_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGLShuffleMask(cast<ShuffleVectorSDNode>(N), 4, 2, *CurDAG);
}]>;
def vmrghb_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 1, 2, *CurDAG);
}]>;
def vmrghh_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 2, 2, *CurDAG);
}]>;
def vmrghw_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGHShuffleMask(cast<ShuffleVectorSDNode>(N), 4, 2, *CurDAG);
}]>;


def vmrgew_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGEOShuffleMask(cast<ShuffleVectorSDNode>(N), true, 0, *CurDAG);
}]>;
def vmrgow_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGEOShuffleMask(cast<ShuffleVectorSDNode>(N), false, 0, *CurDAG);
}]>;
def vmrgew_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGEOShuffleMask(cast<ShuffleVectorSDNode>(N), true, 1, *CurDAG);
}]>;
def vmrgow_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGEOShuffleMask(cast<ShuffleVectorSDNode>(N), false, 1, *CurDAG);
}]>;
def vmrgew_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                     (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGEOShuffleMask(cast<ShuffleVectorSDNode>(N), true, 2, *CurDAG);
}]>;
def vmrgow_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                     (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVMRGEOShuffleMask(cast<ShuffleVectorSDNode>(N), false, 2, *CurDAG);
}]>;



def VSLDOI_get_imm : SDNodeXForm<vector_shuffle, [{
  return getI32Imm(PPC::isVSLDOIShuffleMask(N, 0, *CurDAG), SDLoc(N));
}]>;
def vsldoi_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVSLDOIShuffleMask(N, 0, *CurDAG) != -1;
}], VSLDOI_get_imm>;


/// VSLDOI_unary* - These are used to match vsldoi(X,X), which is turned into
/// vector_shuffle(X,undef,mask) by the dag combiner.
def VSLDOI_unary_get_imm : SDNodeXForm<vector_shuffle, [{
  return getI32Imm(PPC::isVSLDOIShuffleMask(N, 1, *CurDAG), SDLoc(N));
}]>;
def vsldoi_unary_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                   (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVSLDOIShuffleMask(N, 1, *CurDAG) != -1;
}], VSLDOI_unary_get_imm>;


/// VSLDOI_swapped* - These fragments are provided for little-endian, where
/// the inputs must be swapped for correct semantics.
def VSLDOI_swapped_get_imm : SDNodeXForm<vector_shuffle, [{
  return getI32Imm(PPC::isVSLDOIShuffleMask(N, 2, *CurDAG), SDLoc(N));
}]>;
def vsldoi_swapped_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                                     (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isVSLDOIShuffleMask(N, 2, *CurDAG) != -1;
}], VSLDOI_get_imm>;


// VSPLT*_get_imm xform function: convert vector_shuffle mask to VSPLT* imm.
def VSPLTB_get_imm : SDNodeXForm<vector_shuffle, [{
  return getI32Imm(PPC::getVSPLTImmediate(N, 1, *CurDAG), SDLoc(N));
}]>;
def vspltb_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isSplatShuffleMask(cast<ShuffleVectorSDNode>(N), 1);
}], VSPLTB_get_imm>;
def VSPLTH_get_imm : SDNodeXForm<vector_shuffle, [{
  return getI32Imm(PPC::getVSPLTImmediate(N, 2, *CurDAG), SDLoc(N));
}]>;
def vsplth_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isSplatShuffleMask(cast<ShuffleVectorSDNode>(N), 2);
}], VSPLTH_get_imm>;
def VSPLTW_get_imm : SDNodeXForm<vector_shuffle, [{
  return getI32Imm(PPC::getVSPLTImmediate(N, 4, *CurDAG), SDLoc(N));
}]>;
def vspltw_shuffle : PatFrag<(ops node:$lhs, node:$rhs),
                             (vector_shuffle node:$lhs, node:$rhs), [{
  return PPC::isSplatShuffleMask(cast<ShuffleVectorSDNode>(N), 4);
}], VSPLTW_get_imm>;


// VSPLTISB_get_imm xform function: convert build_vector to VSPLTISB imm.
def VSPLTISB_get_imm : SDNodeXForm<build_vector, [{
  return PPC::get_VSPLTI_elt(N, 1, *CurDAG);
}]>;
def vecspltisb : PatLeaf<(build_vector), [{
  return PPC::get_VSPLTI_elt(N, 1, *CurDAG).getNode() != nullptr;
}], VSPLTISB_get_imm>;

// VSPLTISH_get_imm xform function: convert build_vector to VSPLTISH imm.
def VSPLTISH_get_imm : SDNodeXForm<build_vector, [{
  return PPC::get_VSPLTI_elt(N, 2, *CurDAG);
}]>;
def vecspltish : PatLeaf<(build_vector), [{
  return PPC::get_VSPLTI_elt(N, 2, *CurDAG).getNode() != nullptr;
}], VSPLTISH_get_imm>;

// VSPLTISW_get_imm xform function: convert build_vector to VSPLTISW imm.
def VSPLTISW_get_imm : SDNodeXForm<build_vector, [{
  return PPC::get_VSPLTI_elt(N, 4, *CurDAG);
}]>;
def vecspltisw : PatLeaf<(build_vector), [{
  return PPC::get_VSPLTI_elt(N, 4, *CurDAG).getNode() != nullptr;
}], VSPLTISW_get_imm>;

//===----------------------------------------------------------------------===//
// Helpers for defining instructions that directly correspond to intrinsics.

// VA1a_Int_Ty - A VAForm_1a intrinsic definition of specific type.
class VA1a_Int_Ty<bits<6> xo, string opc, Intrinsic IntID, ValueType Ty>
  : VAForm_1a<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB, vrrc:$vC),
              !strconcat(opc, " $vD, $vA, $vB, $vC"), IIC_VecFP,
                       [(set Ty:$vD, (IntID Ty:$vA, Ty:$vB, Ty:$vC))]>;

// VA1a_Int_Ty2 - A VAForm_1a intrinsic definition where the type of the
// inputs doesn't match the type of the output.
class VA1a_Int_Ty2<bits<6> xo, string opc, Intrinsic IntID, ValueType OutTy,
                   ValueType InTy>
  : VAForm_1a<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB, vrrc:$vC),
              !strconcat(opc, " $vD, $vA, $vB, $vC"), IIC_VecFP,
                       [(set OutTy:$vD, (IntID InTy:$vA, InTy:$vB, InTy:$vC))]>;

// VA1a_Int_Ty3 - A VAForm_1a intrinsic definition where there are two
// input types and an output type.
class VA1a_Int_Ty3<bits<6> xo, string opc, Intrinsic IntID, ValueType OutTy,
                   ValueType In1Ty, ValueType In2Ty>
  : VAForm_1a<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB, vrrc:$vC),
              !strconcat(opc, " $vD, $vA, $vB, $vC"), IIC_VecFP,
                       [(set OutTy:$vD,
                         (IntID In1Ty:$vA, In1Ty:$vB, In2Ty:$vC))]>;

// VX1_Int_Ty - A VXForm_1 intrinsic definition of specific type.
class VX1_Int_Ty<bits<11> xo, string opc, Intrinsic IntID, ValueType Ty>
  : VXForm_1<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
             !strconcat(opc, " $vD, $vA, $vB"), IIC_VecFP,
             [(set Ty:$vD, (IntID Ty:$vA, Ty:$vB))]>;

// VX1_Int_Ty2 - A VXForm_1 intrinsic definition where the type of the
// inputs doesn't match the type of the output.
class VX1_Int_Ty2<bits<11> xo, string opc, Intrinsic IntID, ValueType OutTy,
                  ValueType InTy>
  : VXForm_1<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
             !strconcat(opc, " $vD, $vA, $vB"), IIC_VecFP,
             [(set OutTy:$vD, (IntID InTy:$vA, InTy:$vB))]>;

// VX1_Int_Ty3 - A VXForm_1 intrinsic definition where there are two
// input types and an output type.
class VX1_Int_Ty3<bits<11> xo, string opc, Intrinsic IntID, ValueType OutTy,
                  ValueType In1Ty, ValueType In2Ty>
  : VXForm_1<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
             !strconcat(opc, " $vD, $vA, $vB"), IIC_VecFP,
             [(set OutTy:$vD, (IntID In1Ty:$vA, In2Ty:$vB))]>;

// VX2_Int_SP - A VXForm_2 intrinsic definition of vector single-precision type.
class VX2_Int_SP<bits<11> xo, string opc, Intrinsic IntID>
  : VXForm_2<xo, (outs vrrc:$vD), (ins vrrc:$vB),
             !strconcat(opc, " $vD, $vB"), IIC_VecFP,
             [(set v4f32:$vD, (IntID v4f32:$vB))]>;

// VX2_Int_Ty2 - A VXForm_2 intrinsic definition where the type of the
// inputs doesn't match the type of the output.
class VX2_Int_Ty2<bits<11> xo, string opc, Intrinsic IntID, ValueType OutTy,
                  ValueType InTy>
  : VXForm_2<xo, (outs vrrc:$vD), (ins vrrc:$vB),
             !strconcat(opc, " $vD, $vB"), IIC_VecFP,
             [(set OutTy:$vD, (IntID InTy:$vB))]>;

class VXBX_Int_Ty<bits<11> xo, string opc, Intrinsic IntID, ValueType Ty>
  : VXForm_BX<xo, (outs vrrc:$vD), (ins vrrc:$vA),
             !strconcat(opc, " $vD, $vA"), IIC_VecFP,
             [(set Ty:$vD, (IntID Ty:$vA))]>;

class VXCR_Int_Ty<bits<11> xo, string opc, Intrinsic IntID, ValueType Ty>
  : VXForm_CR<xo, (outs vrrc:$vD), (ins vrrc:$vA, u1imm:$ST, u4imm:$SIX),
              !strconcat(opc, " $vD, $vA, $ST, $SIX"), IIC_VecFP,
              [(set Ty:$vD, (IntID Ty:$vA, imm:$ST, imm:$SIX))]>;

//===----------------------------------------------------------------------===//
// Instruction Definitions.

def HasAltivec : Predicate<"PPCSubTarget->hasAltivec()">;
let Predicates = [HasAltivec] in {

def DSS      : DSS_Form<0, 822, (outs), (ins u5imm:$STRM),
                        "dss $STRM", IIC_LdStLoad /*FIXME*/, [(int_ppc_altivec_dss imm:$STRM)]>,
                        Deprecated<DeprecatedDST> {
  let A = 0;
  let B = 0;
}

def DSSALL   : DSS_Form<1, 822, (outs), (ins),
                        "dssall", IIC_LdStLoad /*FIXME*/, [(int_ppc_altivec_dssall)]>,
                        Deprecated<DeprecatedDST> {
  let STRM = 0;
  let A = 0;
  let B = 0;
}

def DST      : DSS_Form<0, 342, (outs), (ins u5imm:$STRM, gprc:$rA, gprc:$rB),
                        "dst $rA, $rB, $STRM", IIC_LdStLoad /*FIXME*/,
                        [(int_ppc_altivec_dst i32:$rA, i32:$rB, imm:$STRM)]>,
                        Deprecated<DeprecatedDST>;

def DSTT     : DSS_Form<1, 342, (outs), (ins u5imm:$STRM, gprc:$rA, gprc:$rB),
                        "dstt $rA, $rB, $STRM", IIC_LdStLoad /*FIXME*/,
                        [(int_ppc_altivec_dstt i32:$rA, i32:$rB, imm:$STRM)]>,
                        Deprecated<DeprecatedDST>;

def DSTST    : DSS_Form<0, 374, (outs), (ins u5imm:$STRM, gprc:$rA, gprc:$rB),
                        "dstst $rA, $rB, $STRM", IIC_LdStLoad /*FIXME*/,
                        [(int_ppc_altivec_dstst i32:$rA, i32:$rB, imm:$STRM)]>,
                        Deprecated<DeprecatedDST>;

def DSTSTT   : DSS_Form<1, 374, (outs), (ins u5imm:$STRM, gprc:$rA, gprc:$rB),
                        "dststt $rA, $rB, $STRM", IIC_LdStLoad /*FIXME*/,
                        [(int_ppc_altivec_dststt i32:$rA, i32:$rB, imm:$STRM)]>,
                        Deprecated<DeprecatedDST>;

let isCodeGenOnly = 1 in {
  // The very same instructions as above, but formally matching 64bit registers.
  def DST64    : DSS_Form<0, 342, (outs), (ins u5imm:$STRM, g8rc:$rA, gprc:$rB),
                          "dst $rA, $rB, $STRM", IIC_LdStLoad /*FIXME*/,
                          [(int_ppc_altivec_dst i64:$rA, i32:$rB, imm:$STRM)]>,
                          Deprecated<DeprecatedDST>;

  def DSTT64   : DSS_Form<1, 342, (outs), (ins u5imm:$STRM, g8rc:$rA, gprc:$rB),
                          "dstt $rA, $rB, $STRM", IIC_LdStLoad /*FIXME*/,
                          [(int_ppc_altivec_dstt i64:$rA, i32:$rB, imm:$STRM)]>,
                          Deprecated<DeprecatedDST>;

  def DSTST64  : DSS_Form<0, 374, (outs), (ins u5imm:$STRM, g8rc:$rA, gprc:$rB),
                          "dstst $rA, $rB, $STRM", IIC_LdStLoad /*FIXME*/,
                          [(int_ppc_altivec_dstst i64:$rA, i32:$rB,
                                                  imm:$STRM)]>,
                          Deprecated<DeprecatedDST>;

  def DSTSTT64 : DSS_Form<1, 374, (outs), (ins u5imm:$STRM, g8rc:$rA, gprc:$rB),
                          "dststt $rA, $rB, $STRM", IIC_LdStLoad /*FIXME*/,
                          [(int_ppc_altivec_dststt i64:$rA, i32:$rB,
                                                   imm:$STRM)]>,
                          Deprecated<DeprecatedDST>;
}

def MFVSCR : VXForm_4<1540, (outs vrrc:$vD), (ins),
                      "mfvscr $vD", IIC_LdStStore,
                      [(set v8i16:$vD, (int_ppc_altivec_mfvscr))]>; 
def MTVSCR : VXForm_5<1604, (outs), (ins vrrc:$vB),
                      "mtvscr $vB", IIC_LdStLoad,
                      [(int_ppc_altivec_mtvscr v4i32:$vB)]>; 

let PPC970_Unit = 2, mayLoad = 1, mayStore = 0 in {  // Loads.
def LVEBX: XForm_1_memOp<31,   7, (outs vrrc:$vD), (ins memrr:$src),
                   "lvebx $vD, $src", IIC_LdStLoad,
                   [(set v16i8:$vD, (int_ppc_altivec_lvebx xoaddr:$src))]>;
def LVEHX: XForm_1_memOp<31,  39, (outs vrrc:$vD), (ins memrr:$src),
                   "lvehx $vD, $src", IIC_LdStLoad,
                   [(set v8i16:$vD, (int_ppc_altivec_lvehx xoaddr:$src))]>;
def LVEWX: XForm_1_memOp<31,  71, (outs vrrc:$vD), (ins memrr:$src),
                   "lvewx $vD, $src", IIC_LdStLoad,
                   [(set v4i32:$vD, (int_ppc_altivec_lvewx xoaddr:$src))]>;
def LVX  : XForm_1_memOp<31, 103, (outs vrrc:$vD), (ins memrr:$src),
                   "lvx $vD, $src", IIC_LdStLoad,
                   [(set v4i32:$vD, (int_ppc_altivec_lvx xoaddr:$src))]>;
def LVXL : XForm_1_memOp<31, 359, (outs vrrc:$vD), (ins memrr:$src),
                   "lvxl $vD, $src", IIC_LdStLoad,
                   [(set v4i32:$vD, (int_ppc_altivec_lvxl xoaddr:$src))]>;
}

def LVSL : XForm_1_memOp<31,   6, (outs vrrc:$vD), (ins memrr:$src),
                   "lvsl $vD, $src", IIC_LdStLoad,
                   [(set v16i8:$vD, (int_ppc_altivec_lvsl xoaddr:$src))]>,
                   PPC970_Unit_LSU;
def LVSR : XForm_1_memOp<31,  38, (outs vrrc:$vD), (ins memrr:$src),
                   "lvsr $vD, $src", IIC_LdStLoad,
                   [(set v16i8:$vD, (int_ppc_altivec_lvsr xoaddr:$src))]>,
                   PPC970_Unit_LSU;

let PPC970_Unit = 2, mayStore = 1, mayLoad = 0 in {   // Stores.
def STVEBX: XForm_8_memOp<31, 135, (outs), (ins vrrc:$rS, memrr:$dst),
                   "stvebx $rS, $dst", IIC_LdStStore,
                   [(int_ppc_altivec_stvebx v16i8:$rS, xoaddr:$dst)]>;
def STVEHX: XForm_8_memOp<31, 167, (outs), (ins vrrc:$rS, memrr:$dst),
                   "stvehx $rS, $dst", IIC_LdStStore,
                   [(int_ppc_altivec_stvehx v8i16:$rS, xoaddr:$dst)]>;
def STVEWX: XForm_8_memOp<31, 199, (outs), (ins vrrc:$rS, memrr:$dst),
                   "stvewx $rS, $dst", IIC_LdStStore,
                   [(int_ppc_altivec_stvewx v4i32:$rS, xoaddr:$dst)]>;
def STVX  : XForm_8_memOp<31, 231, (outs), (ins vrrc:$rS, memrr:$dst),
                   "stvx $rS, $dst", IIC_LdStStore,
                   [(int_ppc_altivec_stvx v4i32:$rS, xoaddr:$dst)]>;
def STVXL : XForm_8_memOp<31, 487, (outs), (ins vrrc:$rS, memrr:$dst),
                   "stvxl $rS, $dst", IIC_LdStStore,
                   [(int_ppc_altivec_stvxl v4i32:$rS, xoaddr:$dst)]>;
}

let PPC970_Unit = 5 in {  // VALU Operations.
// VA-Form instructions.  3-input AltiVec ops.
let isCommutable = 1 in {
def VMADDFP : VAForm_1<46, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vC, vrrc:$vB),
                       "vmaddfp $vD, $vA, $vC, $vB", IIC_VecFP,
                       [(set v4f32:$vD,
                        (fma v4f32:$vA, v4f32:$vC, v4f32:$vB))]>;

// FIXME: The fma+fneg pattern won't match because fneg is not legal.
def VNMSUBFP: VAForm_1<47, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vC, vrrc:$vB),
                       "vnmsubfp $vD, $vA, $vC, $vB", IIC_VecFP,
                       [(set v4f32:$vD, (fneg (fma v4f32:$vA, v4f32:$vC,
                                                  (fneg v4f32:$vB))))]>;

def VMHADDSHS  : VA1a_Int_Ty<32, "vmhaddshs", int_ppc_altivec_vmhaddshs, v8i16>;
def VMHRADDSHS : VA1a_Int_Ty<33, "vmhraddshs", int_ppc_altivec_vmhraddshs,
                             v8i16>;
def VMLADDUHM  : VA1a_Int_Ty<34, "vmladduhm", int_ppc_altivec_vmladduhm, v8i16>;
} // isCommutable

def VPERM      : VA1a_Int_Ty3<43, "vperm", int_ppc_altivec_vperm,
                              v4i32, v4i32, v16i8>;
def VSEL       : VA1a_Int_Ty<42, "vsel",  int_ppc_altivec_vsel, v4i32>;

// Shuffles.
def VSLDOI  : VAForm_2<44, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB, u4imm:$SH),
                       "vsldoi $vD, $vA, $vB, $SH", IIC_VecFP,
                       [(set v16i8:$vD,
                         (PPCvecshl v16i8:$vA, v16i8:$vB, imm32SExt16:$SH))]>;

// VX-Form instructions.  AltiVec arithmetic ops.
let isCommutable = 1 in {
def VADDFP : VXForm_1<10, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vaddfp $vD, $vA, $vB", IIC_VecFP,
                      [(set v4f32:$vD, (fadd v4f32:$vA, v4f32:$vB))]>;
                      
def VADDUBM : VXForm_1<0, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vaddubm $vD, $vA, $vB", IIC_VecGeneral,
                      [(set v16i8:$vD, (add v16i8:$vA, v16i8:$vB))]>;
def VADDUHM : VXForm_1<64, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vadduhm $vD, $vA, $vB", IIC_VecGeneral,
                      [(set v8i16:$vD, (add v8i16:$vA, v8i16:$vB))]>;
def VADDUWM : VXForm_1<128, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vadduwm $vD, $vA, $vB", IIC_VecGeneral,
                      [(set v4i32:$vD, (add v4i32:$vA, v4i32:$vB))]>;
                      
def VADDCUW : VX1_Int_Ty<384, "vaddcuw", int_ppc_altivec_vaddcuw, v4i32>;
def VADDSBS : VX1_Int_Ty<768, "vaddsbs", int_ppc_altivec_vaddsbs, v16i8>;
def VADDSHS : VX1_Int_Ty<832, "vaddshs", int_ppc_altivec_vaddshs, v8i16>;
def VADDSWS : VX1_Int_Ty<896, "vaddsws", int_ppc_altivec_vaddsws, v4i32>;
def VADDUBS : VX1_Int_Ty<512, "vaddubs", int_ppc_altivec_vaddubs, v16i8>;
def VADDUHS : VX1_Int_Ty<576, "vadduhs", int_ppc_altivec_vadduhs, v8i16>;
def VADDUWS : VX1_Int_Ty<640, "vadduws", int_ppc_altivec_vadduws, v4i32>;
} // isCommutable

let isCommutable = 1 in
def VAND : VXForm_1<1028, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                    "vand $vD, $vA, $vB", IIC_VecFP,
                    [(set v4i32:$vD, (and v4i32:$vA, v4i32:$vB))]>;
def VANDC : VXForm_1<1092, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                     "vandc $vD, $vA, $vB", IIC_VecFP,
                     [(set v4i32:$vD, (and v4i32:$vA,
                                           (vnot_ppc v4i32:$vB)))]>;

def VCFSX  : VXForm_1<842, (outs vrrc:$vD), (ins u5imm:$UIMM, vrrc:$vB),
                      "vcfsx $vD, $vB, $UIMM", IIC_VecFP,
                      [(set v4f32:$vD,
                             (int_ppc_altivec_vcfsx v4i32:$vB, imm:$UIMM))]>;
def VCFUX  : VXForm_1<778, (outs vrrc:$vD), (ins u5imm:$UIMM, vrrc:$vB),
                      "vcfux $vD, $vB, $UIMM", IIC_VecFP,
                      [(set v4f32:$vD,
                             (int_ppc_altivec_vcfux v4i32:$vB, imm:$UIMM))]>;
def VCTSXS : VXForm_1<970, (outs vrrc:$vD), (ins u5imm:$UIMM, vrrc:$vB),
                      "vctsxs $vD, $vB, $UIMM", IIC_VecFP,
                      [(set v4i32:$vD,
                             (int_ppc_altivec_vctsxs v4f32:$vB, imm:$UIMM))]>;
def VCTUXS : VXForm_1<906, (outs vrrc:$vD), (ins u5imm:$UIMM, vrrc:$vB),
                      "vctuxs $vD, $vB, $UIMM", IIC_VecFP,
                      [(set v4i32:$vD,
                             (int_ppc_altivec_vctuxs v4f32:$vB, imm:$UIMM))]>;

// Defines with the UIM field set to 0 for floating-point
// to integer (fp_to_sint/fp_to_uint) conversions and integer
// to floating-point (sint_to_fp/uint_to_fp) conversions.
let isCodeGenOnly = 1, VA = 0 in {
def VCFSX_0 : VXForm_1<842, (outs vrrc:$vD), (ins vrrc:$vB),
                       "vcfsx $vD, $vB, 0", IIC_VecFP,
                       [(set v4f32:$vD,
                             (int_ppc_altivec_vcfsx v4i32:$vB, 0))]>;
def VCTUXS_0 : VXForm_1<906, (outs vrrc:$vD), (ins vrrc:$vB),
                        "vctuxs $vD, $vB, 0", IIC_VecFP,
                        [(set v4i32:$vD,
                               (int_ppc_altivec_vctuxs v4f32:$vB, 0))]>;
def VCFUX_0 : VXForm_1<778, (outs vrrc:$vD), (ins vrrc:$vB),
                       "vcfux $vD, $vB, 0", IIC_VecFP,
                       [(set v4f32:$vD,
                               (int_ppc_altivec_vcfux v4i32:$vB, 0))]>;
def VCTSXS_0 : VXForm_1<970, (outs vrrc:$vD), (ins vrrc:$vB),
                      "vctsxs $vD, $vB, 0", IIC_VecFP,
                      [(set v4i32:$vD,
                             (int_ppc_altivec_vctsxs v4f32:$vB, 0))]>;
}
def VEXPTEFP : VX2_Int_SP<394, "vexptefp", int_ppc_altivec_vexptefp>;
def VLOGEFP  : VX2_Int_SP<458, "vlogefp",  int_ppc_altivec_vlogefp>;

let isCommutable = 1 in {
def VAVGSB : VX1_Int_Ty<1282, "vavgsb", int_ppc_altivec_vavgsb, v16i8>;
def VAVGSH : VX1_Int_Ty<1346, "vavgsh", int_ppc_altivec_vavgsh, v8i16>;
def VAVGSW : VX1_Int_Ty<1410, "vavgsw", int_ppc_altivec_vavgsw, v4i32>;
def VAVGUB : VX1_Int_Ty<1026, "vavgub", int_ppc_altivec_vavgub, v16i8>;
def VAVGUH : VX1_Int_Ty<1090, "vavguh", int_ppc_altivec_vavguh, v8i16>;
def VAVGUW : VX1_Int_Ty<1154, "vavguw", int_ppc_altivec_vavguw, v4i32>;

def VMAXFP : VX1_Int_Ty<1034, "vmaxfp", int_ppc_altivec_vmaxfp, v4f32>;
def VMAXSB : VX1_Int_Ty< 258, "vmaxsb", int_ppc_altivec_vmaxsb, v16i8>;
def VMAXSH : VX1_Int_Ty< 322, "vmaxsh", int_ppc_altivec_vmaxsh, v8i16>;
def VMAXSW : VX1_Int_Ty< 386, "vmaxsw", int_ppc_altivec_vmaxsw, v4i32>;
def VMAXUB : VX1_Int_Ty<   2, "vmaxub", int_ppc_altivec_vmaxub, v16i8>;
def VMAXUH : VX1_Int_Ty<  66, "vmaxuh", int_ppc_altivec_vmaxuh, v8i16>;
def VMAXUW : VX1_Int_Ty< 130, "vmaxuw", int_ppc_altivec_vmaxuw, v4i32>;
def VMINFP : VX1_Int_Ty<1098, "vminfp", int_ppc_altivec_vminfp, v4f32>;
def VMINSB : VX1_Int_Ty< 770, "vminsb", int_ppc_altivec_vminsb, v16i8>;
def VMINSH : VX1_Int_Ty< 834, "vminsh", int_ppc_altivec_vminsh, v8i16>;
def VMINSW : VX1_Int_Ty< 898, "vminsw", int_ppc_altivec_vminsw, v4i32>;
def VMINUB : VX1_Int_Ty< 514, "vminub", int_ppc_altivec_vminub, v16i8>;
def VMINUH : VX1_Int_Ty< 578, "vminuh", int_ppc_altivec_vminuh, v8i16>;
def VMINUW : VX1_Int_Ty< 642, "vminuw", int_ppc_altivec_vminuw, v4i32>;
} // isCommutable

def VMRGHB : VXForm_1< 12, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vmrghb $vD, $vA, $vB", IIC_VecFP,
                      [(set v16i8:$vD, (vmrghb_shuffle v16i8:$vA, v16i8:$vB))]>;
def VMRGHH : VXForm_1< 76, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vmrghh $vD, $vA, $vB", IIC_VecFP,
                      [(set v16i8:$vD, (vmrghh_shuffle v16i8:$vA, v16i8:$vB))]>;
def VMRGHW : VXForm_1<140, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vmrghw $vD, $vA, $vB", IIC_VecFP,
                      [(set v16i8:$vD, (vmrghw_shuffle v16i8:$vA, v16i8:$vB))]>;
def VMRGLB : VXForm_1<268, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vmrglb $vD, $vA, $vB", IIC_VecFP,
                      [(set v16i8:$vD, (vmrglb_shuffle v16i8:$vA, v16i8:$vB))]>;
def VMRGLH : VXForm_1<332, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vmrglh $vD, $vA, $vB", IIC_VecFP,
                      [(set v16i8:$vD, (vmrglh_shuffle v16i8:$vA, v16i8:$vB))]>;
def VMRGLW : VXForm_1<396, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vmrglw $vD, $vA, $vB", IIC_VecFP,
                      [(set v16i8:$vD, (vmrglw_shuffle v16i8:$vA, v16i8:$vB))]>;

def VMSUMMBM : VA1a_Int_Ty3<37, "vmsummbm", int_ppc_altivec_vmsummbm,
                            v4i32, v16i8, v4i32>;
def VMSUMSHM : VA1a_Int_Ty3<40, "vmsumshm", int_ppc_altivec_vmsumshm,
                            v4i32, v8i16, v4i32>;
def VMSUMSHS : VA1a_Int_Ty3<41, "vmsumshs", int_ppc_altivec_vmsumshs,
                            v4i32, v8i16, v4i32>;
def VMSUMUBM : VA1a_Int_Ty3<36, "vmsumubm", int_ppc_altivec_vmsumubm,
                            v4i32, v16i8, v4i32>;
def VMSUMUHM : VA1a_Int_Ty3<38, "vmsumuhm", int_ppc_altivec_vmsumuhm,
                            v4i32, v8i16, v4i32>;
def VMSUMUHS : VA1a_Int_Ty3<39, "vmsumuhs", int_ppc_altivec_vmsumuhs,
                            v4i32, v8i16, v4i32>;

let isCommutable = 1 in {
def VMULESB : VX1_Int_Ty2<776, "vmulesb", int_ppc_altivec_vmulesb,
                          v8i16, v16i8>;
def VMULESH : VX1_Int_Ty2<840, "vmulesh", int_ppc_altivec_vmulesh,
                          v4i32, v8i16>;
def VMULEUB : VX1_Int_Ty2<520, "vmuleub", int_ppc_altivec_vmuleub,
                          v8i16, v16i8>;
def VMULEUH : VX1_Int_Ty2<584, "vmuleuh", int_ppc_altivec_vmuleuh,
                          v4i32, v8i16>;
def VMULOSB : VX1_Int_Ty2<264, "vmulosb", int_ppc_altivec_vmulosb,
                          v8i16, v16i8>;
def VMULOSH : VX1_Int_Ty2<328, "vmulosh", int_ppc_altivec_vmulosh,
                          v4i32, v8i16>;
def VMULOUB : VX1_Int_Ty2<  8, "vmuloub", int_ppc_altivec_vmuloub,
                          v8i16, v16i8>;
def VMULOUH : VX1_Int_Ty2< 72, "vmulouh", int_ppc_altivec_vmulouh,
                          v4i32, v8i16>;
} // isCommutable
                       
def VREFP     : VX2_Int_SP<266, "vrefp",     int_ppc_altivec_vrefp>;
def VRFIM     : VX2_Int_SP<714, "vrfim",     int_ppc_altivec_vrfim>;
def VRFIN     : VX2_Int_SP<522, "vrfin",     int_ppc_altivec_vrfin>;
def VRFIP     : VX2_Int_SP<650, "vrfip",     int_ppc_altivec_vrfip>;
def VRFIZ     : VX2_Int_SP<586, "vrfiz",     int_ppc_altivec_vrfiz>;
def VRSQRTEFP : VX2_Int_SP<330, "vrsqrtefp", int_ppc_altivec_vrsqrtefp>;

def VSUBCUW : VX1_Int_Ty<1408, "vsubcuw", int_ppc_altivec_vsubcuw, v4i32>;

def VSUBFP  : VXForm_1<74, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vsubfp $vD, $vA, $vB", IIC_VecGeneral,
                      [(set v4f32:$vD, (fsub v4f32:$vA, v4f32:$vB))]>;
def VSUBUBM : VXForm_1<1024, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vsububm $vD, $vA, $vB", IIC_VecGeneral,
                      [(set v16i8:$vD, (sub v16i8:$vA, v16i8:$vB))]>;
def VSUBUHM : VXForm_1<1088, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vsubuhm $vD, $vA, $vB", IIC_VecGeneral,
                      [(set v8i16:$vD, (sub v8i16:$vA, v8i16:$vB))]>;
def VSUBUWM : VXForm_1<1152, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vsubuwm $vD, $vA, $vB", IIC_VecGeneral,
                      [(set v4i32:$vD, (sub v4i32:$vA, v4i32:$vB))]>;
                      
def VSUBSBS : VX1_Int_Ty<1792, "vsubsbs" , int_ppc_altivec_vsubsbs, v16i8>;
def VSUBSHS : VX1_Int_Ty<1856, "vsubshs" , int_ppc_altivec_vsubshs, v8i16>;
def VSUBSWS : VX1_Int_Ty<1920, "vsubsws" , int_ppc_altivec_vsubsws, v4i32>;
def VSUBUBS : VX1_Int_Ty<1536, "vsububs" , int_ppc_altivec_vsububs, v16i8>;
def VSUBUHS : VX1_Int_Ty<1600, "vsubuhs" , int_ppc_altivec_vsubuhs, v8i16>;
def VSUBUWS : VX1_Int_Ty<1664, "vsubuws" , int_ppc_altivec_vsubuws, v4i32>;

def VSUMSWS : VX1_Int_Ty<1928, "vsumsws" , int_ppc_altivec_vsumsws, v4i32>;
def VSUM2SWS: VX1_Int_Ty<1672, "vsum2sws", int_ppc_altivec_vsum2sws, v4i32>;

def VSUM4SBS: VX1_Int_Ty3<1800, "vsum4sbs", int_ppc_altivec_vsum4sbs,
                          v4i32, v16i8, v4i32>;
def VSUM4SHS: VX1_Int_Ty3<1608, "vsum4shs", int_ppc_altivec_vsum4shs,
                          v4i32, v8i16, v4i32>;
def VSUM4UBS: VX1_Int_Ty3<1544, "vsum4ubs", int_ppc_altivec_vsum4ubs,
                          v4i32, v16i8, v4i32>;

def VNOR : VXForm_1<1284, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                    "vnor $vD, $vA, $vB", IIC_VecFP,
                    [(set v4i32:$vD, (vnot_ppc (or v4i32:$vA,
                                                   v4i32:$vB)))]>;
let isCommutable = 1 in {
def VOR : VXForm_1<1156, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vor $vD, $vA, $vB", IIC_VecFP,
                      [(set v4i32:$vD, (or v4i32:$vA, v4i32:$vB))]>;
def VXOR : VXForm_1<1220, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vxor $vD, $vA, $vB", IIC_VecFP,
                      [(set v4i32:$vD, (xor v4i32:$vA, v4i32:$vB))]>;
} // isCommutable

def VRLB   : VX1_Int_Ty<   4, "vrlb", int_ppc_altivec_vrlb, v16i8>;
def VRLH   : VX1_Int_Ty<  68, "vrlh", int_ppc_altivec_vrlh, v8i16>;
def VRLW   : VX1_Int_Ty< 132, "vrlw", int_ppc_altivec_vrlw, v4i32>;

def VSL    : VX1_Int_Ty< 452, "vsl" , int_ppc_altivec_vsl,  v4i32 >;
def VSLO   : VX1_Int_Ty<1036, "vslo", int_ppc_altivec_vslo, v4i32>;

def VSLB   : VX1_Int_Ty< 260, "vslb", int_ppc_altivec_vslb, v16i8>;
def VSLH   : VX1_Int_Ty< 324, "vslh", int_ppc_altivec_vslh, v8i16>;
def VSLW   : VX1_Int_Ty< 388, "vslw", int_ppc_altivec_vslw, v4i32>;

def VSPLTB : VXForm_1<524, (outs vrrc:$vD), (ins u5imm:$UIMM, vrrc:$vB),
                      "vspltb $vD, $vB, $UIMM", IIC_VecPerm,
                      [(set v16i8:$vD,
                        (vspltb_shuffle:$UIMM v16i8:$vB, (undef)))]>;
def VSPLTH : VXForm_1<588, (outs vrrc:$vD), (ins u5imm:$UIMM, vrrc:$vB),
                      "vsplth $vD, $vB, $UIMM", IIC_VecPerm,
                      [(set v16i8:$vD,
                        (vsplth_shuffle:$UIMM v16i8:$vB, (undef)))]>;
def VSPLTW : VXForm_1<652, (outs vrrc:$vD), (ins u5imm:$UIMM, vrrc:$vB),
                      "vspltw $vD, $vB, $UIMM", IIC_VecPerm,
                      [(set v16i8:$vD,
                        (vspltw_shuffle:$UIMM v16i8:$vB, (undef)))]>;
let isCodeGenOnly = 1 in {
  def VSPLTBs : VXForm_1<524, (outs vrrc:$vD), (ins u5imm:$UIMM, vfrc:$vB),
                         "vspltb $vD, $vB, $UIMM", IIC_VecPerm, []>;
  def VSPLTHs : VXForm_1<588, (outs vrrc:$vD), (ins u5imm:$UIMM, vfrc:$vB),
                         "vsplth $vD, $vB, $UIMM", IIC_VecPerm, []>;
}

def VSR    : VX1_Int_Ty< 708, "vsr"  , int_ppc_altivec_vsr,  v4i32>;
def VSRO   : VX1_Int_Ty<1100, "vsro" , int_ppc_altivec_vsro, v4i32>;

def VSRAB  : VX1_Int_Ty< 772, "vsrab", int_ppc_altivec_vsrab, v16i8>;
def VSRAH  : VX1_Int_Ty< 836, "vsrah", int_ppc_altivec_vsrah, v8i16>;
def VSRAW  : VX1_Int_Ty< 900, "vsraw", int_ppc_altivec_vsraw, v4i32>;
def VSRB   : VX1_Int_Ty< 516, "vsrb" , int_ppc_altivec_vsrb , v16i8>;
def VSRH   : VX1_Int_Ty< 580, "vsrh" , int_ppc_altivec_vsrh , v8i16>;
def VSRW   : VX1_Int_Ty< 644, "vsrw" , int_ppc_altivec_vsrw , v4i32>;


def VSPLTISB : VXForm_3<780, (outs vrrc:$vD), (ins s5imm:$SIMM),
                       "vspltisb $vD, $SIMM", IIC_VecPerm,
                       [(set v16i8:$vD, (v16i8 vecspltisb:$SIMM))]>;
def VSPLTISH : VXForm_3<844, (outs vrrc:$vD), (ins s5imm:$SIMM),
                       "vspltish $vD, $SIMM", IIC_VecPerm,
                       [(set v8i16:$vD, (v8i16 vecspltish:$SIMM))]>;
def VSPLTISW : VXForm_3<908, (outs vrrc:$vD), (ins s5imm:$SIMM),
                       "vspltisw $vD, $SIMM", IIC_VecPerm,
                       [(set v4i32:$vD, (v4i32 vecspltisw:$SIMM))]>;

// Vector Pack.
def VPKPX   : VX1_Int_Ty2<782, "vpkpx", int_ppc_altivec_vpkpx,
                          v8i16, v4i32>;
def VPKSHSS : VX1_Int_Ty2<398, "vpkshss", int_ppc_altivec_vpkshss,
                          v16i8, v8i16>;
def VPKSHUS : VX1_Int_Ty2<270, "vpkshus", int_ppc_altivec_vpkshus,
                          v16i8, v8i16>;
def VPKSWSS : VX1_Int_Ty2<462, "vpkswss", int_ppc_altivec_vpkswss,
                          v8i16, v4i32>;
def VPKSWUS : VX1_Int_Ty2<334, "vpkswus", int_ppc_altivec_vpkswus,
                          v8i16, v4i32>;
def VPKUHUM : VXForm_1<14, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vpkuhum $vD, $vA, $vB", IIC_VecFP,
                       [(set v16i8:$vD,
                         (vpkuhum_shuffle v16i8:$vA, v16i8:$vB))]>;
def VPKUHUS : VX1_Int_Ty2<142, "vpkuhus", int_ppc_altivec_vpkuhus,
                          v16i8, v8i16>;
def VPKUWUM : VXForm_1<78, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vpkuwum $vD, $vA, $vB", IIC_VecFP,
                       [(set v16i8:$vD,
                         (vpkuwum_shuffle v16i8:$vA, v16i8:$vB))]>;
def VPKUWUS : VX1_Int_Ty2<206, "vpkuwus", int_ppc_altivec_vpkuwus,
                          v8i16, v4i32>;

// Vector Unpack.
def VUPKHPX : VX2_Int_Ty2<846, "vupkhpx", int_ppc_altivec_vupkhpx,
                          v4i32, v8i16>;
def VUPKHSB : VX2_Int_Ty2<526, "vupkhsb", int_ppc_altivec_vupkhsb,
                          v8i16, v16i8>;
def VUPKHSH : VX2_Int_Ty2<590, "vupkhsh", int_ppc_altivec_vupkhsh,
                          v4i32, v8i16>;
def VUPKLPX : VX2_Int_Ty2<974, "vupklpx", int_ppc_altivec_vupklpx,
                          v4i32, v8i16>;
def VUPKLSB : VX2_Int_Ty2<654, "vupklsb", int_ppc_altivec_vupklsb,
                          v8i16, v16i8>;
def VUPKLSH : VX2_Int_Ty2<718, "vupklsh", int_ppc_altivec_vupklsh,
                          v4i32, v8i16>;


// Altivec Comparisons.

class VCMP<bits<10> xo, string asmstr, ValueType Ty>
  : VXRForm_1<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB), asmstr,
              IIC_VecFPCompare,
              [(set Ty:$vD, (Ty (PPCvcmp Ty:$vA, Ty:$vB, xo)))]>;
class VCMPo<bits<10> xo, string asmstr, ValueType Ty>
  : VXRForm_1<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB), asmstr,
              IIC_VecFPCompare,
              [(set Ty:$vD, (Ty (PPCvcmp_o Ty:$vA, Ty:$vB, xo)))]> {
  let Defs = [CR6];
  let RC = 1;
}

// f32 element comparisons.0
def VCMPBFP   : VCMP <966, "vcmpbfp $vD, $vA, $vB"  , v4f32>;
def VCMPBFPo  : VCMPo<966, "vcmpbfp. $vD, $vA, $vB" , v4f32>;
def VCMPEQFP  : VCMP <198, "vcmpeqfp $vD, $vA, $vB" , v4f32>;
def VCMPEQFPo : VCMPo<198, "vcmpeqfp. $vD, $vA, $vB", v4f32>;
def VCMPGEFP  : VCMP <454, "vcmpgefp $vD, $vA, $vB" , v4f32>;
def VCMPGEFPo : VCMPo<454, "vcmpgefp. $vD, $vA, $vB", v4f32>;
def VCMPGTFP  : VCMP <710, "vcmpgtfp $vD, $vA, $vB" , v4f32>;
def VCMPGTFPo : VCMPo<710, "vcmpgtfp. $vD, $vA, $vB", v4f32>;

// i8 element comparisons.
def VCMPEQUB  : VCMP <  6, "vcmpequb $vD, $vA, $vB" , v16i8>;
def VCMPEQUBo : VCMPo<  6, "vcmpequb. $vD, $vA, $vB", v16i8>;
def VCMPGTSB  : VCMP <774, "vcmpgtsb $vD, $vA, $vB" , v16i8>;
def VCMPGTSBo : VCMPo<774, "vcmpgtsb. $vD, $vA, $vB", v16i8>;
def VCMPGTUB  : VCMP <518, "vcmpgtub $vD, $vA, $vB" , v16i8>;
def VCMPGTUBo : VCMPo<518, "vcmpgtub. $vD, $vA, $vB", v16i8>;

// i16 element comparisons.
def VCMPEQUH  : VCMP < 70, "vcmpequh $vD, $vA, $vB" , v8i16>;
def VCMPEQUHo : VCMPo< 70, "vcmpequh. $vD, $vA, $vB", v8i16>;
def VCMPGTSH  : VCMP <838, "vcmpgtsh $vD, $vA, $vB" , v8i16>;
def VCMPGTSHo : VCMPo<838, "vcmpgtsh. $vD, $vA, $vB", v8i16>;
def VCMPGTUH  : VCMP <582, "vcmpgtuh $vD, $vA, $vB" , v8i16>;
def VCMPGTUHo : VCMPo<582, "vcmpgtuh. $vD, $vA, $vB", v8i16>;

// i32 element comparisons.
def VCMPEQUW  : VCMP <134, "vcmpequw $vD, $vA, $vB" , v4i32>;
def VCMPEQUWo : VCMPo<134, "vcmpequw. $vD, $vA, $vB", v4i32>;
def VCMPGTSW  : VCMP <902, "vcmpgtsw $vD, $vA, $vB" , v4i32>;
def VCMPGTSWo : VCMPo<902, "vcmpgtsw. $vD, $vA, $vB", v4i32>;
def VCMPGTUW  : VCMP <646, "vcmpgtuw $vD, $vA, $vB" , v4i32>;
def VCMPGTUWo : VCMPo<646, "vcmpgtuw. $vD, $vA, $vB", v4i32>;

let isCodeGenOnly = 1 in {
def V_SET0B : VXForm_setzero<1220, (outs vrrc:$vD), (ins),
                      "vxor $vD, $vD, $vD", IIC_VecFP,
                      [(set v16i8:$vD, (v16i8 immAllZerosV))]>;
def V_SET0H : VXForm_setzero<1220, (outs vrrc:$vD), (ins),
                      "vxor $vD, $vD, $vD", IIC_VecFP,
                      [(set v8i16:$vD, (v8i16 immAllZerosV))]>;
def V_SET0  : VXForm_setzero<1220, (outs vrrc:$vD), (ins),
                      "vxor $vD, $vD, $vD", IIC_VecFP,
                      [(set v4i32:$vD, (v4i32 immAllZerosV))]>;

let IMM=-1 in {
def V_SETALLONESB : VXForm_3<908, (outs vrrc:$vD), (ins),
                      "vspltisw $vD, -1", IIC_VecFP,
                      [(set v16i8:$vD, (v16i8 immAllOnesV))]>;
def V_SETALLONESH : VXForm_3<908, (outs vrrc:$vD), (ins),
                      "vspltisw $vD, -1", IIC_VecFP,
                      [(set v8i16:$vD, (v8i16 immAllOnesV))]>;
def V_SETALLONES  : VXForm_3<908, (outs vrrc:$vD), (ins),
                      "vspltisw $vD, -1", IIC_VecFP,
                      [(set v4i32:$vD, (v4i32 immAllOnesV))]>;
}
}
} // VALU Operations.

//===----------------------------------------------------------------------===//
// Additional Altivec Patterns
//

// Extended mnemonics
def : InstAlias<"vmr $vD, $vA", (VOR vrrc:$vD, vrrc:$vA, vrrc:$vA)>;
def : InstAlias<"vnot $vD, $vA", (VNOR vrrc:$vD, vrrc:$vA, vrrc:$vA)>;

// Loads.
def : Pat<(v4i32 (load xoaddr:$src)), (LVX xoaddr:$src)>;

// Stores.
def : Pat<(store v4i32:$rS, xoaddr:$dst),
          (STVX $rS, xoaddr:$dst)>;

// Bit conversions.
def : Pat<(v16i8 (bitconvert (v8i16 VRRC:$src))), (v16i8 VRRC:$src)>;
def : Pat<(v16i8 (bitconvert (v4i32 VRRC:$src))), (v16i8 VRRC:$src)>;
def : Pat<(v16i8 (bitconvert (v4f32 VRRC:$src))), (v16i8 VRRC:$src)>;
def : Pat<(v16i8 (bitconvert (v2i64 VRRC:$src))), (v16i8 VRRC:$src)>;
def : Pat<(v16i8 (bitconvert (v1i128 VRRC:$src))), (v16i8 VRRC:$src)>;

def : Pat<(v8i16 (bitconvert (v16i8 VRRC:$src))), (v8i16 VRRC:$src)>;
def : Pat<(v8i16 (bitconvert (v4i32 VRRC:$src))), (v8i16 VRRC:$src)>;
def : Pat<(v8i16 (bitconvert (v4f32 VRRC:$src))), (v8i16 VRRC:$src)>;
def : Pat<(v8i16 (bitconvert (v2i64 VRRC:$src))), (v8i16 VRRC:$src)>;
def : Pat<(v8i16 (bitconvert (v1i128 VRRC:$src))), (v8i16 VRRC:$src)>;

def : Pat<(v4i32 (bitconvert (v16i8 VRRC:$src))), (v4i32 VRRC:$src)>;
def : Pat<(v4i32 (bitconvert (v8i16 VRRC:$src))), (v4i32 VRRC:$src)>;
def : Pat<(v4i32 (bitconvert (v4f32 VRRC:$src))), (v4i32 VRRC:$src)>;
def : Pat<(v4i32 (bitconvert (v2i64 VRRC:$src))), (v4i32 VRRC:$src)>;
def : Pat<(v4i32 (bitconvert (v1i128 VRRC:$src))), (v4i32 VRRC:$src)>;

def : Pat<(v4f32 (bitconvert (v16i8 VRRC:$src))), (v4f32 VRRC:$src)>;
def : Pat<(v4f32 (bitconvert (v8i16 VRRC:$src))), (v4f32 VRRC:$src)>;
def : Pat<(v4f32 (bitconvert (v4i32 VRRC:$src))), (v4f32 VRRC:$src)>;
def : Pat<(v4f32 (bitconvert (v2i64 VRRC:$src))), (v4f32 VRRC:$src)>;
def : Pat<(v4f32 (bitconvert (v1i128 VRRC:$src))), (v4f32 VRRC:$src)>;

def : Pat<(v2i64 (bitconvert (v16i8 VRRC:$src))), (v2i64 VRRC:$src)>;
def : Pat<(v2i64 (bitconvert (v8i16 VRRC:$src))), (v2i64 VRRC:$src)>;
def : Pat<(v2i64 (bitconvert (v4i32 VRRC:$src))), (v2i64 VRRC:$src)>;
def : Pat<(v2i64 (bitconvert (v4f32 VRRC:$src))), (v2i64 VRRC:$src)>;
def : Pat<(v2i64 (bitconvert (v1i128 VRRC:$src))), (v2i64 VRRC:$src)>;

def : Pat<(v1i128 (bitconvert (v16i8 VRRC:$src))), (v1i128 VRRC:$src)>;
def : Pat<(v1i128 (bitconvert (v8i16 VRRC:$src))), (v1i128 VRRC:$src)>;
def : Pat<(v1i128 (bitconvert (v4i32 VRRC:$src))), (v1i128 VRRC:$src)>;
def : Pat<(v1i128 (bitconvert (v4f32 VRRC:$src))), (v1i128 VRRC:$src)>;
def : Pat<(v1i128 (bitconvert (v2i64 VRRC:$src))), (v1i128 VRRC:$src)>;

// Shuffles.

// Match vsldoi(x,x), vpkuwum(x,x), vpkuhum(x,x)
def:Pat<(vsldoi_unary_shuffle:$in v16i8:$vA, undef),
        (VSLDOI $vA, $vA, (VSLDOI_unary_get_imm $in))>;
def:Pat<(vpkuwum_unary_shuffle v16i8:$vA, undef),
        (VPKUWUM $vA, $vA)>;
def:Pat<(vpkuhum_unary_shuffle v16i8:$vA, undef),
        (VPKUHUM $vA, $vA)>;
def:Pat<(vsldoi_shuffle:$SH v16i8:$vA, v16i8:$vB),
        (VSLDOI v16i8:$vA, v16i8:$vB, (VSLDOI_get_imm $SH))>;


// Match vsldoi(y,x), vpkuwum(y,x), vpkuhum(y,x), i.e., swapped operands.
// These fragments are matched for little-endian, where the inputs must
// be swapped for correct semantics.
def:Pat<(vsldoi_swapped_shuffle:$in v16i8:$vA, v16i8:$vB),
        (VSLDOI $vB, $vA, (VSLDOI_swapped_get_imm $in))>;
def:Pat<(vpkuwum_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VPKUWUM $vB, $vA)>;
def:Pat<(vpkuhum_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VPKUHUM $vB, $vA)>;

// Match vmrg*(x,x)
def:Pat<(vmrglb_unary_shuffle v16i8:$vA, undef),
        (VMRGLB $vA, $vA)>;
def:Pat<(vmrglh_unary_shuffle v16i8:$vA, undef),
        (VMRGLH $vA, $vA)>;
def:Pat<(vmrglw_unary_shuffle v16i8:$vA, undef),
        (VMRGLW $vA, $vA)>;
def:Pat<(vmrghb_unary_shuffle v16i8:$vA, undef),
        (VMRGHB $vA, $vA)>;
def:Pat<(vmrghh_unary_shuffle v16i8:$vA, undef),
        (VMRGHH $vA, $vA)>;
def:Pat<(vmrghw_unary_shuffle v16i8:$vA, undef),
        (VMRGHW $vA, $vA)>;

// Match vmrg*(y,x), i.e., swapped operands.  These fragments
// are matched for little-endian, where the inputs must be
// swapped for correct semantics.
def:Pat<(vmrglb_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VMRGLB $vB, $vA)>;
def:Pat<(vmrglh_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VMRGLH $vB, $vA)>;
def:Pat<(vmrglw_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VMRGLW $vB, $vA)>;
def:Pat<(vmrghb_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VMRGHB $vB, $vA)>;
def:Pat<(vmrghh_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VMRGHH $vB, $vA)>;
def:Pat<(vmrghw_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VMRGHW $vB, $vA)>;

// Logical Operations
def : Pat<(vnot_ppc v4i32:$vA), (VNOR $vA, $vA)>;

def : Pat<(vnot_ppc (or v4i32:$A, v4i32:$B)),
          (VNOR $A, $B)>;
def : Pat<(and v4i32:$A, (vnot_ppc v4i32:$B)),
          (VANDC $A, $B)>;

def : Pat<(fmul v4f32:$vA, v4f32:$vB),
          (VMADDFP $vA, $vB,
             (v4i32 (VSLW (v4i32 (V_SETALLONES)), (v4i32 (V_SETALLONES)))))>; 

// Fused multiply add and multiply sub for packed float.  These are represented
// separately from the real instructions above, for operations that must have
// the additional precision, such as Newton-Rhapson (used by divide, sqrt)
def : Pat<(PPCvmaddfp v4f32:$A, v4f32:$B, v4f32:$C),
          (VMADDFP $A, $B, $C)>;
def : Pat<(PPCvnmsubfp v4f32:$A, v4f32:$B, v4f32:$C),
          (VNMSUBFP $A, $B, $C)>;

def : Pat<(int_ppc_altivec_vmaddfp v4f32:$A, v4f32:$B, v4f32:$C),
          (VMADDFP $A, $B, $C)>;
def : Pat<(int_ppc_altivec_vnmsubfp v4f32:$A, v4f32:$B, v4f32:$C),
          (VNMSUBFP $A, $B, $C)>;

def : Pat<(PPCvperm v16i8:$vA, v16i8:$vB, v16i8:$vC),
          (VPERM $vA, $vB, $vC)>;

def : Pat<(PPCfre v4f32:$A), (VREFP $A)>;
def : Pat<(PPCfrsqrte v4f32:$A), (VRSQRTEFP $A)>;

// Vector shifts
def : Pat<(v16i8 (shl v16i8:$vA, v16i8:$vB)),
          (v16i8 (VSLB $vA, $vB))>;
def : Pat<(v8i16 (shl v8i16:$vA, v8i16:$vB)),
          (v8i16 (VSLH $vA, $vB))>;
def : Pat<(v4i32 (shl v4i32:$vA, v4i32:$vB)),
          (v4i32 (VSLW $vA, $vB))>;
def : Pat<(v1i128 (shl v1i128:$vA, v1i128:$vB)),
          (v1i128 (VSL (v16i8 (VSLO $vA, $vB)), (v16i8 (VSPLTB 15, $vB))))>;
def : Pat<(v16i8 (PPCshl v16i8:$vA, v16i8:$vB)),
          (v16i8 (VSLB $vA, $vB))>;
def : Pat<(v8i16 (PPCshl v8i16:$vA, v8i16:$vB)),
          (v8i16 (VSLH $vA, $vB))>;
def : Pat<(v4i32 (PPCshl v4i32:$vA, v4i32:$vB)),
          (v4i32 (VSLW $vA, $vB))>;
def : Pat<(v1i128 (PPCshl v1i128:$vA, v1i128:$vB)),
          (v1i128 (VSL (v16i8 (VSLO $vA, $vB)), (v16i8 (VSPLTB 15, $vB))))>;

def : Pat<(v16i8 (srl v16i8:$vA, v16i8:$vB)),
          (v16i8 (VSRB $vA, $vB))>;
def : Pat<(v8i16 (srl v8i16:$vA, v8i16:$vB)),
          (v8i16 (VSRH $vA, $vB))>;
def : Pat<(v4i32 (srl v4i32:$vA, v4i32:$vB)),
          (v4i32 (VSRW $vA, $vB))>;
def : Pat<(v1i128 (srl v1i128:$vA, v1i128:$vB)),
          (v1i128 (VSR (v16i8 (VSRO $vA, $vB)), (v16i8 (VSPLTB 15, $vB))))>;
def : Pat<(v16i8 (PPCsrl v16i8:$vA, v16i8:$vB)),
          (v16i8 (VSRB $vA, $vB))>;
def : Pat<(v8i16 (PPCsrl v8i16:$vA, v8i16:$vB)),
          (v8i16 (VSRH $vA, $vB))>;
def : Pat<(v4i32 (PPCsrl v4i32:$vA, v4i32:$vB)),
          (v4i32 (VSRW $vA, $vB))>;
def : Pat<(v1i128 (PPCsrl v1i128:$vA, v1i128:$vB)),
          (v1i128 (VSR (v16i8 (VSRO $vA, $vB)), (v16i8 (VSPLTB 15, $vB))))>;

def : Pat<(v16i8 (sra v16i8:$vA, v16i8:$vB)),
          (v16i8 (VSRAB $vA, $vB))>;
def : Pat<(v8i16 (sra v8i16:$vA, v8i16:$vB)),
          (v8i16 (VSRAH $vA, $vB))>;
def : Pat<(v4i32 (sra v4i32:$vA, v4i32:$vB)),
          (v4i32 (VSRAW $vA, $vB))>;
def : Pat<(v16i8 (PPCsra v16i8:$vA, v16i8:$vB)),
          (v16i8 (VSRAB $vA, $vB))>;
def : Pat<(v8i16 (PPCsra v8i16:$vA, v8i16:$vB)),
          (v8i16 (VSRAH $vA, $vB))>;
def : Pat<(v4i32 (PPCsra v4i32:$vA, v4i32:$vB)),
          (v4i32 (VSRAW $vA, $vB))>;

// Float to integer and integer to float conversions
def : Pat<(v4i32 (fp_to_sint v4f32:$vA)),
           (VCTSXS_0 $vA)>;
def : Pat<(v4i32 (fp_to_uint v4f32:$vA)),
           (VCTUXS_0 $vA)>;
def : Pat<(v4f32 (sint_to_fp v4i32:$vA)),
           (VCFSX_0 $vA)>;
def : Pat<(v4f32 (uint_to_fp v4i32:$vA)),
           (VCFUX_0 $vA)>;

// Floating-point rounding
def : Pat<(v4f32 (ffloor v4f32:$vA)),
          (VRFIM $vA)>;
def : Pat<(v4f32 (fceil v4f32:$vA)),
          (VRFIP $vA)>;
def : Pat<(v4f32 (ftrunc v4f32:$vA)),
          (VRFIZ $vA)>;
def : Pat<(v4f32 (fnearbyint v4f32:$vA)),
          (VRFIN $vA)>;

// Vector selection
def : Pat<(v16i8 (vselect v16i8:$vA, v16i8:$vB, v16i8:$vC)),
          (VSEL $vC, $vB, $vA)>;
def : Pat<(v8i16 (vselect v8i16:$vA, v8i16:$vB, v8i16:$vC)),
          (VSEL $vC, $vB, $vA)>;
def : Pat<(v4i32 (vselect v4i32:$vA, v4i32:$vB, v4i32:$vC)),
          (VSEL $vC, $vB, $vA)>;
def : Pat<(v2i64 (vselect v2i64:$vA, v2i64:$vB, v2i64:$vC)),
          (VSEL $vC, $vB, $vA)>;
def : Pat<(v4f32 (vselect v4i32:$vA, v4f32:$vB, v4f32:$vC)),
          (VSEL $vC, $vB, $vA)>;
def : Pat<(v2f64 (vselect v2i64:$vA, v2f64:$vB, v2f64:$vC)),
          (VSEL $vC, $vB, $vA)>;

} // end HasAltivec

def HasP8Altivec : Predicate<"PPCSubTarget->hasP8Altivec()">;
def HasP8Crypto : Predicate<"PPCSubTarget->hasP8Crypto()">;
let Predicates = [HasP8Altivec] in {

let isCommutable = 1 in {
def VMULESW : VX1_Int_Ty2<904, "vmulesw", int_ppc_altivec_vmulesw,
                          v2i64, v4i32>;
def VMULEUW : VX1_Int_Ty2<648, "vmuleuw", int_ppc_altivec_vmuleuw,
                          v2i64, v4i32>;
def VMULOSW : VX1_Int_Ty2<392, "vmulosw", int_ppc_altivec_vmulosw,
                          v2i64, v4i32>;
def VMULOUW : VX1_Int_Ty2<136, "vmulouw", int_ppc_altivec_vmulouw,
                          v2i64, v4i32>;
def VMULUWM : VXForm_1<137, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vmuluwm $vD, $vA, $vB", IIC_VecGeneral,
                       [(set v4i32:$vD, (mul v4i32:$vA, v4i32:$vB))]>;
def VMAXSD : VX1_Int_Ty<450, "vmaxsd", int_ppc_altivec_vmaxsd, v2i64>;
def VMAXUD : VX1_Int_Ty<194, "vmaxud", int_ppc_altivec_vmaxud, v2i64>;
def VMINSD : VX1_Int_Ty<962, "vminsd", int_ppc_altivec_vminsd, v2i64>;
def VMINUD : VX1_Int_Ty<706, "vminud", int_ppc_altivec_vminud, v2i64>;
} // isCommutable

// Vector merge 
def VMRGEW : VXForm_1<1932, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vmrgew $vD, $vA, $vB", IIC_VecFP,
                      [(set v16i8:$vD,
                            (v16i8 (vmrgew_shuffle v16i8:$vA, v16i8:$vB)))]>;
def VMRGOW : VXForm_1<1676, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vmrgow $vD, $vA, $vB", IIC_VecFP,
                      [(set v16i8:$vD,
                            (v16i8 (vmrgow_shuffle v16i8:$vA, v16i8:$vB)))]>;

// Match vmrgew(x,x) and vmrgow(x,x)
def:Pat<(vmrgew_unary_shuffle v16i8:$vA, undef),
        (VMRGEW $vA, $vA)>;
def:Pat<(vmrgow_unary_shuffle v16i8:$vA, undef),
        (VMRGOW $vA, $vA)>;

// Match vmrgew(y,x) and vmrgow(y,x), i.e., swapped operands.  These fragments
// are matched for little-endian, where the inputs must be swapped for correct
// semantics.w
def:Pat<(vmrgew_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VMRGEW $vB, $vA)>;
def:Pat<(vmrgow_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VMRGOW $vB, $vA)>;


// Vector shifts
def VRLD : VX1_Int_Ty<196, "vrld", int_ppc_altivec_vrld, v2i64>;
def VSLD : VXForm_1<1476, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                    "vsld $vD, $vA, $vB", IIC_VecGeneral, []>;
def VSRD : VXForm_1<1732, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                   "vsrd $vD, $vA, $vB", IIC_VecGeneral, []>;
def VSRAD : VXForm_1<964, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                    "vsrad $vD, $vA, $vB", IIC_VecGeneral, []>;

def : Pat<(v2i64 (shl v2i64:$vA, v2i64:$vB)),
          (v2i64 (VSLD $vA, $vB))>;
def : Pat<(v2i64 (PPCshl v2i64:$vA, v2i64:$vB)),
          (v2i64 (VSLD $vA, $vB))>;
def : Pat<(v2i64 (srl v2i64:$vA, v2i64:$vB)),
          (v2i64 (VSRD $vA, $vB))>;
def : Pat<(v2i64 (PPCsrl v2i64:$vA, v2i64:$vB)),
          (v2i64 (VSRD $vA, $vB))>;
def : Pat<(v2i64 (sra v2i64:$vA, v2i64:$vB)),
          (v2i64 (VSRAD $vA, $vB))>;
def : Pat<(v2i64 (PPCsra v2i64:$vA, v2i64:$vB)),
          (v2i64 (VSRAD $vA, $vB))>;

// Vector Integer Arithmetic Instructions
let isCommutable = 1 in {
def VADDUDM : VXForm_1<192, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vaddudm $vD, $vA, $vB", IIC_VecGeneral,
                       [(set v2i64:$vD, (add v2i64:$vA, v2i64:$vB))]>;
def VADDUQM : VXForm_1<256, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vadduqm $vD, $vA, $vB", IIC_VecGeneral,
                       [(set v1i128:$vD, (add v1i128:$vA, v1i128:$vB))]>;
} // isCommutable

// Vector Quadword Add
def VADDEUQM : VA1a_Int_Ty<60, "vaddeuqm", int_ppc_altivec_vaddeuqm, v1i128>;
def VADDCUQ  : VX1_Int_Ty<320, "vaddcuq", int_ppc_altivec_vaddcuq, v1i128>;
def VADDECUQ : VA1a_Int_Ty<61, "vaddecuq", int_ppc_altivec_vaddecuq, v1i128>;

// Vector Doubleword Subtract
def VSUBUDM : VXForm_1<1216, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vsubudm $vD, $vA, $vB", IIC_VecGeneral,
                       [(set v2i64:$vD, (sub v2i64:$vA, v2i64:$vB))]>;

// Vector Quadword Subtract
def VSUBUQM : VXForm_1<1280, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vsubuqm $vD, $vA, $vB", IIC_VecGeneral,
                       [(set v1i128:$vD, (sub v1i128:$vA, v1i128:$vB))]>;
def VSUBEUQM : VA1a_Int_Ty<62, "vsubeuqm", int_ppc_altivec_vsubeuqm, v1i128>;
def VSUBCUQ  : VX1_Int_Ty<1344, "vsubcuq", int_ppc_altivec_vsubcuq, v1i128>;
def VSUBECUQ : VA1a_Int_Ty<63, "vsubecuq", int_ppc_altivec_vsubecuq, v1i128>;

// Count Leading Zeros
def VCLZB : VXForm_2<1794, (outs vrrc:$vD), (ins vrrc:$vB),
                     "vclzb $vD, $vB", IIC_VecGeneral,
                     [(set v16i8:$vD, (ctlz v16i8:$vB))]>;
def VCLZH : VXForm_2<1858, (outs vrrc:$vD), (ins vrrc:$vB),
                     "vclzh $vD, $vB", IIC_VecGeneral,
                     [(set v8i16:$vD, (ctlz v8i16:$vB))]>;
def VCLZW : VXForm_2<1922, (outs vrrc:$vD), (ins vrrc:$vB),
                     "vclzw $vD, $vB", IIC_VecGeneral,
                     [(set v4i32:$vD, (ctlz v4i32:$vB))]>;
def VCLZD : VXForm_2<1986, (outs vrrc:$vD), (ins vrrc:$vB),
                     "vclzd $vD, $vB", IIC_VecGeneral,
                     [(set v2i64:$vD, (ctlz v2i64:$vB))]>;

// Population Count
def VPOPCNTB : VXForm_2<1795, (outs vrrc:$vD), (ins vrrc:$vB),
                        "vpopcntb $vD, $vB", IIC_VecGeneral,
                        [(set v16i8:$vD, (ctpop v16i8:$vB))]>;
def VPOPCNTH : VXForm_2<1859, (outs vrrc:$vD), (ins vrrc:$vB),
                        "vpopcnth $vD, $vB", IIC_VecGeneral,
                        [(set v8i16:$vD, (ctpop v8i16:$vB))]>;
def VPOPCNTW : VXForm_2<1923, (outs vrrc:$vD), (ins vrrc:$vB),
                        "vpopcntw $vD, $vB", IIC_VecGeneral,
                        [(set v4i32:$vD, (ctpop v4i32:$vB))]>;
def VPOPCNTD : VXForm_2<1987, (outs vrrc:$vD), (ins vrrc:$vB),
                        "vpopcntd $vD, $vB", IIC_VecGeneral,
                        [(set v2i64:$vD, (ctpop v2i64:$vB))]>;

let isCommutable = 1 in {
// FIXME: Use AddedComplexity > 400 to ensure these patterns match before the 
//        VSX equivalents. We need to fix this up at some point. Two possible
//        solutions for this problem:
//        1. Disable Altivec patterns that compete with VSX patterns using the
//           !HasVSX predicate. This essentially favours VSX over Altivec, in 
//           hopes of reducing register pressure (larger register set using VSX 
//           instructions than VMX instructions)
//        2. Employ a more disciplined use of AddedComplexity, which would provide
//           more fine-grained control than option 1. This would be beneficial
//           if we find situations where Altivec is really preferred over VSX. 
def VEQV  : VXForm_1<1668, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                     "veqv $vD, $vA, $vB", IIC_VecGeneral,
                     [(set v4i32:$vD, (vnot_ppc (xor v4i32:$vA, v4i32:$vB)))]>;
def VNAND : VXForm_1<1412, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                     "vnand $vD, $vA, $vB", IIC_VecGeneral,
                     [(set v4i32:$vD, (vnot_ppc (and v4i32:$vA, v4i32:$vB)))]>;
} // isCommutable

def VORC : VXForm_1<1348, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                      "vorc $vD, $vA, $vB", IIC_VecGeneral,
                      [(set v4i32:$vD, (or v4i32:$vA,
                                           (vnot_ppc v4i32:$vB)))]>;

// i64 element comparisons.
def VCMPEQUD  : VCMP <199, "vcmpequd $vD, $vA, $vB" , v2i64>;
def VCMPEQUDo : VCMPo<199, "vcmpequd. $vD, $vA, $vB", v2i64>;
def VCMPGTSD  : VCMP <967, "vcmpgtsd $vD, $vA, $vB" , v2i64>;
def VCMPGTSDo : VCMPo<967, "vcmpgtsd. $vD, $vA, $vB", v2i64>;
def VCMPGTUD  : VCMP <711, "vcmpgtud $vD, $vA, $vB" , v2i64>;
def VCMPGTUDo : VCMPo<711, "vcmpgtud. $vD, $vA, $vB", v2i64>;

// The cryptography instructions that do not require Category:Vector.Crypto
def VPMSUMB : VX1_Int_Ty<1032, "vpmsumb",
                         int_ppc_altivec_crypto_vpmsumb, v16i8>;
def VPMSUMH : VX1_Int_Ty<1096, "vpmsumh",
                         int_ppc_altivec_crypto_vpmsumh, v8i16>;
def VPMSUMW : VX1_Int_Ty<1160, "vpmsumw",
                         int_ppc_altivec_crypto_vpmsumw, v4i32>;
def VPMSUMD : VX1_Int_Ty<1224, "vpmsumd",
                         int_ppc_altivec_crypto_vpmsumd, v2i64>;
def VPERMXOR : VA1a_Int_Ty<45, "vpermxor",
                         int_ppc_altivec_crypto_vpermxor, v16i8>;

// Vector doubleword integer pack and unpack.
def VPKSDSS : VX1_Int_Ty2<1486, "vpksdss", int_ppc_altivec_vpksdss,
                          v4i32, v2i64>;
def VPKSDUS : VX1_Int_Ty2<1358, "vpksdus", int_ppc_altivec_vpksdus,
                          v4i32, v2i64>;
def VPKUDUM : VXForm_1<1102, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vpkudum $vD, $vA, $vB", IIC_VecFP,
                       [(set v16i8:$vD,
                         (vpkudum_shuffle v16i8:$vA, v16i8:$vB))]>;
def VPKUDUS : VX1_Int_Ty2<1230, "vpkudus", int_ppc_altivec_vpkudus,
                          v4i32, v2i64>;
def VUPKHSW : VX2_Int_Ty2<1614, "vupkhsw", int_ppc_altivec_vupkhsw,
                          v2i64, v4i32>;
def VUPKLSW : VX2_Int_Ty2<1742, "vupklsw", int_ppc_altivec_vupklsw,
                          v2i64, v4i32>;

// Shuffle patterns for unary and swapped (LE) vector pack modulo.
def:Pat<(vpkudum_unary_shuffle v16i8:$vA, undef),
        (VPKUDUM $vA, $vA)>;
def:Pat<(vpkudum_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (VPKUDUM $vB, $vA)>;

def VGBBD : VX2_Int_Ty2<1292, "vgbbd", int_ppc_altivec_vgbbd, v16i8, v16i8>;
def VBPERMQ : VX1_Int_Ty2<1356, "vbpermq", int_ppc_altivec_vbpermq,
                          v2i64, v16i8>;
} // end HasP8Altivec

// Crypto instructions (from builtins)
let Predicates = [HasP8Crypto] in {
def VSHASIGMAW : VXCR_Int_Ty<1666, "vshasigmaw",
                              int_ppc_altivec_crypto_vshasigmaw, v4i32>;
def VSHASIGMAD : VXCR_Int_Ty<1730, "vshasigmad",
                              int_ppc_altivec_crypto_vshasigmad, v2i64>;
def VCIPHER : VX1_Int_Ty<1288, "vcipher", int_ppc_altivec_crypto_vcipher,
                         v2i64>;
def VCIPHERLAST : VX1_Int_Ty<1289, "vcipherlast",
                              int_ppc_altivec_crypto_vcipherlast, v2i64>;
def VNCIPHER : VX1_Int_Ty<1352, "vncipher",
                          int_ppc_altivec_crypto_vncipher, v2i64>;
def VNCIPHERLAST : VX1_Int_Ty<1353, "vncipherlast",
                              int_ppc_altivec_crypto_vncipherlast, v2i64>;
def VSBOX : VXBX_Int_Ty<1480, "vsbox", int_ppc_altivec_crypto_vsbox, v2i64>;
} // HasP8Crypto

// The following altivec instructions were introduced in Power ISA 3.0
def HasP9Altivec : Predicate<"PPCSubTarget->hasP9Altivec()">;
let Predicates = [HasP9Altivec] in {

// i8 element comparisons.
def VCMPNEB   : VCMP   <  7, "vcmpneb $vD, $vA, $vB"  , v16i8>;
def VCMPNEBo  : VCMPo  <  7, "vcmpneb. $vD, $vA, $vB" , v16i8>;
def VCMPNEZB  : VCMP <263, "vcmpnezb $vD, $vA, $vB" , v16i8>;
def VCMPNEZBo : VCMPo<263, "vcmpnezb. $vD, $vA, $vB", v16i8>;

// i16 element comparisons.
def VCMPNEH   : VCMP < 71, "vcmpneh $vD, $vA, $vB"  , v8i16>;
def VCMPNEHo  : VCMPo< 71, "vcmpneh. $vD, $vA, $vB" , v8i16>;
def VCMPNEZH  : VCMP <327, "vcmpnezh $vD, $vA, $vB" , v8i16>;
def VCMPNEZHo : VCMPo<327, "vcmpnezh. $vD, $vA, $vB", v8i16>;

// i32 element comparisons.
def VCMPNEW   : VCMP <135, "vcmpnew $vD, $vA, $vB"  , v4i32>;
def VCMPNEWo  : VCMPo<135, "vcmpnew. $vD, $vA, $vB" , v4i32>;
def VCMPNEZW  : VCMP <391, "vcmpnezw $vD, $vA, $vB" , v4i32>;
def VCMPNEZWo : VCMPo<391, "vcmpnezw. $vD, $vA, $vB", v4i32>;

// VX-Form: [PO VRT / UIM VRB XO].
// We use VXForm_1 to implement it, that is, we use "VRA" (5 bit) to represent
// "/ UIM" (1 + 4 bit)
class VX1_VT5_UIM5_VB5<bits<11> xo, string opc, list<dag> pattern>
  : VXForm_1<xo, (outs vrrc:$vD), (ins u4imm:$UIMM, vrrc:$vB),
             !strconcat(opc, " $vD, $vB, $UIMM"), IIC_VecGeneral, pattern>;

class VX1_RT5_RA5_VB5<bits<11> xo, string opc, list<dag> pattern>
  : VXForm_1<xo, (outs g8rc:$rD), (ins g8rc:$rA, vrrc:$vB),
             !strconcat(opc, " $rD, $rA, $vB"), IIC_VecGeneral, pattern>;

// Vector Extract Unsigned
def VEXTRACTUB : VX1_VT5_UIM5_VB5<525, "vextractub", []>;
def VEXTRACTUH : VX1_VT5_UIM5_VB5<589, "vextractuh", []>;
def VEXTRACTUW : VX1_VT5_UIM5_VB5<653, "vextractuw", []>;
def VEXTRACTD  : VX1_VT5_UIM5_VB5<717, "vextractd" , []>;

// Vector Extract Unsigned Byte/Halfword/Word Left/Right-Indexed
def VEXTUBLX : VX1_RT5_RA5_VB5<1549, "vextublx", []>;
def VEXTUBRX : VX1_RT5_RA5_VB5<1805, "vextubrx", []>;
def VEXTUHLX : VX1_RT5_RA5_VB5<1613, "vextuhlx", []>;
def VEXTUHRX : VX1_RT5_RA5_VB5<1869, "vextuhrx", []>;
def VEXTUWLX : VX1_RT5_RA5_VB5<1677, "vextuwlx", []>;
def VEXTUWRX : VX1_RT5_RA5_VB5<1933, "vextuwrx", []>;

// Vector Insert Element Instructions
def VINSERTB : VXForm_1<781, (outs vrrc:$vD),
                        (ins vrrc:$vDi, u4imm:$UIM, vrrc:$vB),
                        "vinsertb $vD, $vB, $UIM", IIC_VecGeneral,
                        [(set v16i8:$vD, (PPCvecinsert v16i8:$vDi, v16i8:$vB,
                                                      imm32SExt16:$UIM))]>,
                        RegConstraint<"$vDi = $vD">, NoEncode<"$vDi">;
def VINSERTH : VXForm_1<845, (outs vrrc:$vD),
                        (ins vrrc:$vDi, u4imm:$UIM, vrrc:$vB),
                        "vinserth $vD, $vB, $UIM", IIC_VecGeneral,
                        [(set v8i16:$vD, (PPCvecinsert v8i16:$vDi, v8i16:$vB,
                                                      imm32SExt16:$UIM))]>,
                        RegConstraint<"$vDi = $vD">, NoEncode<"$vDi">;
def VINSERTW : VX1_VT5_UIM5_VB5<909, "vinsertw", []>;
def VINSERTD : VX1_VT5_UIM5_VB5<973, "vinsertd", []>;

class VX_VT5_EO5_VB5<bits<11> xo, bits<5> eo, string opc, list<dag> pattern>
  : VXForm_RD5_XO5_RS5<xo, eo, (outs vrrc:$vD), (ins vrrc:$vB),
                       !strconcat(opc, " $vD, $vB"), IIC_VecGeneral, pattern>;
class VX_VT5_EO5_VB5s<bits<11> xo, bits<5> eo, string opc, list<dag> pattern>
  : VXForm_RD5_XO5_RS5<xo, eo, (outs vfrc:$vD), (ins vfrc:$vB),
                       !strconcat(opc, " $vD, $vB"), IIC_VecGeneral, pattern>;

// Vector Count Leading/Trailing Zero LSB. Result is placed into GPR[rD]
def VCLZLSBB : VXForm_RD5_XO5_RS5<1538, 0, (outs gprc:$rD), (ins vrrc:$vB),
                                  "vclzlsbb $rD, $vB", IIC_VecGeneral,
                                  [(set i32:$rD, (int_ppc_altivec_vclzlsbb
                                     v16i8:$vB))]>;
def VCTZLSBB : VXForm_RD5_XO5_RS5<1538, 1, (outs gprc:$rD), (ins vrrc:$vB),
                                  "vctzlsbb $rD, $vB", IIC_VecGeneral,
                                  [(set i32:$rD, (int_ppc_altivec_vctzlsbb
                                     v16i8:$vB))]>;
// Vector Count Trailing Zeros
def VCTZB : VX_VT5_EO5_VB5<1538, 28, "vctzb",
                           [(set v16i8:$vD, (cttz v16i8:$vB))]>;
def VCTZH : VX_VT5_EO5_VB5<1538, 29, "vctzh",
                           [(set v8i16:$vD, (cttz v8i16:$vB))]>;
def VCTZW : VX_VT5_EO5_VB5<1538, 30, "vctzw",
                           [(set v4i32:$vD, (cttz v4i32:$vB))]>;
def VCTZD : VX_VT5_EO5_VB5<1538, 31, "vctzd",
                           [(set v2i64:$vD, (cttz v2i64:$vB))]>;

// Vector Extend Sign
def VEXTSB2W : VX_VT5_EO5_VB5<1538, 16, "vextsb2w", []>;
def VEXTSH2W : VX_VT5_EO5_VB5<1538, 17, "vextsh2w", []>;
def VEXTSB2D : VX_VT5_EO5_VB5<1538, 24, "vextsb2d", []>;
def VEXTSH2D : VX_VT5_EO5_VB5<1538, 25, "vextsh2d", []>;
def VEXTSW2D : VX_VT5_EO5_VB5<1538, 26, "vextsw2d", []>;
let isCodeGenOnly = 1 in {
  def VEXTSB2Ws : VX_VT5_EO5_VB5s<1538, 16, "vextsb2w", []>;
  def VEXTSH2Ws : VX_VT5_EO5_VB5s<1538, 17, "vextsh2w", []>;
  def VEXTSB2Ds : VX_VT5_EO5_VB5s<1538, 24, "vextsb2d", []>;
  def VEXTSH2Ds : VX_VT5_EO5_VB5s<1538, 25, "vextsh2d", []>;
  def VEXTSW2Ds : VX_VT5_EO5_VB5s<1538, 26, "vextsw2d", []>;
}

// Vector Integer Negate
def VNEGW : VX_VT5_EO5_VB5<1538, 6, "vnegw",
                           [(set v4i32:$vD,
                            (sub (v4i32 immAllZerosV), v4i32:$vB))]>;

def VNEGD : VX_VT5_EO5_VB5<1538, 7, "vnegd",
                           [(set v2i64:$vD,
                            (sub (v2i64 (bitconvert (v4i32 immAllZerosV))),
                                  v2i64:$vB))]>;

// Vector Parity Byte
def VPRTYBW : VX_VT5_EO5_VB5<1538, 8, "vprtybw", [(set v4i32:$vD,
                            (int_ppc_altivec_vprtybw v4i32:$vB))]>;
def VPRTYBD : VX_VT5_EO5_VB5<1538,  9, "vprtybd", [(set v2i64:$vD,
                            (int_ppc_altivec_vprtybd v2i64:$vB))]>;
def VPRTYBQ : VX_VT5_EO5_VB5<1538, 10, "vprtybq", [(set v1i128:$vD,
                            (int_ppc_altivec_vprtybq v1i128:$vB))]>;

// Vector (Bit) Permute (Right-indexed)
def VBPERMD : VXForm_1<1484, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vbpermd $vD, $vA, $vB", IIC_VecFP, []>;
def VPERMR : VAForm_1a<59, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB, vrrc:$vC),
                       "vpermr $vD, $vA, $vB, $vC", IIC_VecFP, []>;

class VX1_VT5_VA5_VB5<bits<11> xo, string opc, list<dag> pattern>
  : VXForm_1<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
             !strconcat(opc, " $vD, $vA, $vB"), IIC_VecFP, pattern>;

// Vector Rotate Left Mask/Mask-Insert
def VRLWNM : VX1_VT5_VA5_VB5<389, "vrlwnm",
                             [(set v4i32:$vD,
                                 (int_ppc_altivec_vrlwnm v4i32:$vA,
                                                         v4i32:$vB))]>;
def VRLWMI : VXForm_1<133, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB, vrrc:$vDi),
                      "vrlwmi $vD, $vA, $vB", IIC_VecFP,
                      [(set v4i32:$vD,
                         (int_ppc_altivec_vrlwmi v4i32:$vA, v4i32:$vB,
                                                 v4i32:$vDi))]>,
                      RegConstraint<"$vDi = $vD">, NoEncode<"$vDi">;
def VRLDNM : VX1_VT5_VA5_VB5<453, "vrldnm",
                             [(set v2i64:$vD,
                                 (int_ppc_altivec_vrldnm v2i64:$vA,
                                                         v2i64:$vB))]>;
def VRLDMI : VXForm_1<197, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB, vrrc:$vDi),
                      "vrldmi $vD, $vA, $vB", IIC_VecFP,
                      [(set v2i64:$vD,
                         (int_ppc_altivec_vrldmi v2i64:$vA, v2i64:$vB,
                                                 v2i64:$vDi))]>,
                      RegConstraint<"$vDi = $vD">, NoEncode<"$vDi">;

// Vector Shift Left/Right
def VSLV : VX1_VT5_VA5_VB5<1860, "vslv",
                           [(set v16i8 : $vD, (int_ppc_altivec_vslv v16i8 : $vA, v16i8 : $vB))]>;
def VSRV : VX1_VT5_VA5_VB5<1796, "vsrv",
                           [(set v16i8 : $vD, (int_ppc_altivec_vsrv v16i8 : $vA, v16i8 : $vB))]>;

// Vector Multiply-by-10 (& Write Carry) Unsigned Quadword
def VMUL10UQ   : VXForm_BX<513, (outs vrrc:$vD), (ins vrrc:$vA),
                           "vmul10uq $vD, $vA", IIC_VecFP, []>;
def VMUL10CUQ  : VXForm_BX<  1, (outs vrrc:$vD), (ins vrrc:$vA),
                           "vmul10cuq $vD, $vA", IIC_VecFP, []>;

// Vector Multiply-by-10 Extended (& Write Carry) Unsigned Quadword
def VMUL10EUQ  : VX1_VT5_VA5_VB5<577, "vmul10euq" , []>;
def VMUL10ECUQ : VX1_VT5_VA5_VB5< 65, "vmul10ecuq", []>;

// Decimal Integer Format Conversion Instructions

// [PO VRT EO VRB 1 PS XO], "_o" means CR6 is set.
class VX_VT5_EO5_VB5_PS1_XO9_o<bits<5> eo, bits<9> xo, string opc,
                               list<dag> pattern>
  : VX_RD5_EO5_RS5_PS1_XO9<eo, xo, (outs vrrc:$vD), (ins vrrc:$vB, u1imm:$PS),
                        !strconcat(opc, " $vD, $vB, $PS"), IIC_VecFP, pattern> {
  let Defs = [CR6];
}

// [PO VRT EO VRB 1 / XO]
class VX_VT5_EO5_VB5_XO9_o<bits<5> eo, bits<9> xo, string opc,
                           list<dag> pattern>
  : VX_RD5_EO5_RS5_PS1_XO9<eo, xo, (outs vrrc:$vD), (ins vrrc:$vB),
                           !strconcat(opc, " $vD, $vB"), IIC_VecFP, pattern> {
  let Defs = [CR6];
  let PS = 0;
}

// Decimal Convert From/to National/Zoned/Signed-QWord
def BCDCFNo  : VX_VT5_EO5_VB5_PS1_XO9_o<7, 385, "bcdcfn." , []>;
def BCDCFZo  : VX_VT5_EO5_VB5_PS1_XO9_o<6, 385, "bcdcfz." , []>;
def BCDCTNo  : VX_VT5_EO5_VB5_XO9_o    <5, 385, "bcdctn." , []>;
def BCDCTZo  : VX_VT5_EO5_VB5_PS1_XO9_o<4, 385, "bcdctz." , []>;
def BCDCFSQo : VX_VT5_EO5_VB5_PS1_XO9_o<2, 385, "bcdcfsq.", []>;
def BCDCTSQo : VX_VT5_EO5_VB5_XO9_o    <0, 385, "bcdctsq.", []>;

// Decimal Copy-Sign/Set-Sign
let Defs = [CR6] in
def BCDCPSGNo : VX1_VT5_VA5_VB5<833, "bcdcpsgn.", []>;

def BCDSETSGNo : VX_VT5_EO5_VB5_PS1_XO9_o<31, 385, "bcdsetsgn.", []>;

// [PO VRT VRA VRB 1 PS XO], "_o" means CR6 is set.
class VX_VT5_VA5_VB5_PS1_XO9_o<bits<9> xo, string opc, list<dag> pattern>
  : VX_RD5_RSp5_PS1_XO9<xo,
                   (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB, u1imm:$PS),
                   !strconcat(opc, " $vD, $vA, $vB, $PS"), IIC_VecFP, pattern> {
  let Defs = [CR6];
}

// [PO VRT VRA VRB 1 / XO]
class VX_VT5_VA5_VB5_XO9_o<bits<9> xo, string opc, list<dag> pattern>
  : VX_RD5_RSp5_PS1_XO9<xo, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                        !strconcat(opc, " $vD, $vA, $vB"), IIC_VecFP, pattern> {
  let Defs = [CR6];
  let PS = 0;
}

// Decimal Shift/Unsigned-Shift/Shift-and-Round
def BCDSo :  VX_VT5_VA5_VB5_PS1_XO9_o<193, "bcds." , []>;
def BCDUSo : VX_VT5_VA5_VB5_XO9_o    <129, "bcdus.", []>;
def BCDSRo : VX_VT5_VA5_VB5_PS1_XO9_o<449, "bcdsr.", []>;

// Decimal (Unsigned) Truncate
def BCDTRUNCo :  VX_VT5_VA5_VB5_PS1_XO9_o<257, "bcdtrunc." , []>;
def BCDUTRUNCo : VX_VT5_VA5_VB5_XO9_o    <321, "bcdutrunc.", []>;

// Absolute Difference
def VABSDUB : VXForm_1<1027, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vabsdub $vD, $vA, $vB", IIC_VecGeneral,
                       [(set v16i8:$vD, (int_ppc_altivec_vabsdub v16i8:$vA, v16i8:$vB))]>;
def VABSDUH : VXForm_1<1091, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vabsduh $vD, $vA, $vB", IIC_VecGeneral,
                       [(set v8i16:$vD, (int_ppc_altivec_vabsduh v8i16:$vA, v8i16:$vB))]>;
def VABSDUW : VXForm_1<1155, (outs vrrc:$vD), (ins vrrc:$vA, vrrc:$vB),
                       "vabsduw $vD, $vA, $vB", IIC_VecGeneral,
                       [(set v4i32:$vD, (int_ppc_altivec_vabsduw v4i32:$vA, v4i32:$vB))]>;

} // end HasP9Altivec
