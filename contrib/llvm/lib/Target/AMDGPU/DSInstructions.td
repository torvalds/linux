//===-- DSInstructions.td - DS Instruction Defintions ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

class DS_Pseudo <string opName, dag outs, dag ins, string asmOps, list<dag> pattern=[]> :
  InstSI <outs, ins, "", pattern>,
  SIMCInstr <opName, SIEncodingFamily.NONE> {

  let SubtargetPredicate = isGCN;

  let LGKM_CNT = 1;
  let DS = 1;
  let Size = 8;
  let UseNamedOperandTable = 1;

  // Most instruction load and store data, so set this as the default.
  let mayLoad = 1;
  let mayStore = 1;

  let hasSideEffects = 0;
  let SchedRW = [WriteLDS];

  let isPseudo = 1;
  let isCodeGenOnly = 1;

  let AsmMatchConverter = "cvtDS";

  string Mnemonic = opName;
  string AsmOperands = asmOps;

  // Well these bits a kind of hack because it would be more natural
  // to test "outs" and "ins" dags for the presence of particular operands
  bits<1> has_vdst = 1;
  bits<1> has_addr = 1;
  bits<1> has_data0 = 1;
  bits<1> has_data1 = 1;

  bits<1> has_offset  = 1; // has "offset" that should be split to offset0,1
  bits<1> has_offset0 = 1;
  bits<1> has_offset1 = 1;

  bits<1> has_gds = 1;
  bits<1> gdsValue = 0; // if has_gds == 0 set gds to this value

  bits<1> has_m0_read = 1;

  let Uses = !if(has_m0_read, [M0, EXEC], [EXEC]);
}

class DS_Real <DS_Pseudo ds> :
  InstSI <ds.OutOperandList, ds.InOperandList, ds.Mnemonic # " " # ds.AsmOperands, []>,
  Enc64 {

  let isPseudo = 0;
  let isCodeGenOnly = 0;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ds.SubtargetPredicate;
  let AsmMatchConverter  = ds.AsmMatchConverter;

  // encoding fields
  bits<8> vdst;
  bits<1> gds;
  bits<8> addr;
  bits<8> data0;
  bits<8> data1;
  bits<8> offset0;
  bits<8> offset1;

  bits<16> offset;
  let offset0 = !if(ds.has_offset, offset{7-0}, ?);
  let offset1 = !if(ds.has_offset, offset{15-8}, ?);
}


// DS Pseudo instructions

class DS_1A1D_NORET<string opName, RegisterClass rc = VGPR_32>
: DS_Pseudo<opName,
  (outs),
  (ins VGPR_32:$addr, rc:$data0, offset:$offset, gds:$gds),
  "$addr, $data0$offset$gds"> {

  let has_data1 = 0;
  let has_vdst = 0;
}

multiclass DS_1A1D_NORET_mc<string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A1D_NORET<opName, rc>,
           AtomicNoRet<opName, 0>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A1D_NORET<opName, rc>,
                AtomicNoRet<opName#"_gfx9", 0>;
  }
}

class DS_1A2D_NORET<string opName, RegisterClass rc = VGPR_32>
: DS_Pseudo<opName,
  (outs),
  (ins VGPR_32:$addr, rc:$data0, rc:$data1, offset:$offset, gds:$gds),
  "$addr, $data0, $data1"#"$offset"#"$gds"> {

  let has_vdst = 0;
}

multiclass DS_1A2D_NORET_mc<string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A2D_NORET<opName, rc>,
           AtomicNoRet<opName, 0>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A2D_NORET<opName, rc>,
                AtomicNoRet<opName#"_gfx9", 0>;
  }
}

class DS_1A2D_Off8_NORET <string opName, RegisterClass rc = VGPR_32>
: DS_Pseudo<opName,
  (outs),
  (ins VGPR_32:$addr, rc:$data0, rc:$data1,
       offset0:$offset0, offset1:$offset1, gds:$gds),
  "$addr, $data0, $data1$offset0$offset1$gds"> {

  let has_vdst = 0;
  let has_offset = 0;
  let AsmMatchConverter = "cvtDSOffset01";
}

multiclass DS_1A2D_Off8_NORET_mc <string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A2D_Off8_NORET<opName, rc>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A2D_Off8_NORET<opName, rc>;
  }
}

class DS_1A1D_RET <string opName, RegisterClass rc = VGPR_32>
: DS_Pseudo<opName,
  (outs rc:$vdst),
  (ins VGPR_32:$addr, rc:$data0, offset:$offset, gds:$gds),
  "$vdst, $addr, $data0$offset$gds"> {

  let hasPostISelHook = 1;
  let has_data1 = 0;
}

multiclass DS_1A1D_RET_mc <string opName, RegisterClass rc = VGPR_32,
                           string NoRetOp = ""> {
  def "" : DS_1A1D_RET<opName, rc>,
    AtomicNoRet<NoRetOp, !if(!eq(NoRetOp, ""), 0, 1)>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A1D_RET<opName, rc>,
      AtomicNoRet<!if(!eq(NoRetOp, ""), "", NoRetOp#"_gfx9"),
                  !if(!eq(NoRetOp, ""), 0, 1)>;
  }
}

class DS_1A2D_RET<string opName,
                  RegisterClass rc = VGPR_32,
                  RegisterClass src = rc>
: DS_Pseudo<opName,
  (outs rc:$vdst),
  (ins VGPR_32:$addr, src:$data0, src:$data1, offset:$offset, gds:$gds),
  "$vdst, $addr, $data0, $data1$offset$gds"> {

  let hasPostISelHook = 1;
}

multiclass DS_1A2D_RET_mc<string opName,
                          RegisterClass rc = VGPR_32,
                          string NoRetOp = "",
                          RegisterClass src = rc> {
  def "" : DS_1A2D_RET<opName, rc, src>,
    AtomicNoRet<NoRetOp, !if(!eq(NoRetOp, ""), 0, 1)>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A2D_RET<opName, rc, src>,
      AtomicNoRet<NoRetOp#"_gfx9", !if(!eq(NoRetOp, ""), 0, 1)>;
  }
}

class DS_1A2D_Off8_RET<string opName,
                       RegisterClass rc = VGPR_32,
                       RegisterClass src = rc>
: DS_Pseudo<opName,
  (outs rc:$vdst),
  (ins VGPR_32:$addr, src:$data0, src:$data1, offset0:$offset0, offset1:$offset1, gds:$gds),
  "$vdst, $addr, $data0, $data1$offset0$offset1$gds"> {

  let has_offset = 0;
  let AsmMatchConverter = "cvtDSOffset01";

  let hasPostISelHook = 1;
}

multiclass DS_1A2D_Off8_RET_mc<string opName,
                               RegisterClass rc = VGPR_32,
                               RegisterClass src = rc> {
  def "" : DS_1A2D_Off8_RET<opName, rc, src>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A2D_Off8_RET<opName, rc, src>;
  }
}


class DS_1A_RET<string opName, RegisterClass rc = VGPR_32, bit HasTiedOutput = 0, Operand ofs = offset>
: DS_Pseudo<opName,
  (outs rc:$vdst),
  !if(HasTiedOutput,
    (ins VGPR_32:$addr, ofs:$offset, gds:$gds, rc:$vdst_in),
    (ins VGPR_32:$addr, ofs:$offset, gds:$gds)),
  "$vdst, $addr$offset$gds"> {
  let Constraints = !if(HasTiedOutput, "$vdst = $vdst_in", "");
  let DisableEncoding = !if(HasTiedOutput, "$vdst_in", "");
  let has_data0 = 0;
  let has_data1 = 0;
}

multiclass DS_1A_RET_mc<string opName, RegisterClass rc = VGPR_32, bit HasTiedOutput = 0, Operand ofs = offset> {
  def "" : DS_1A_RET<opName, rc, HasTiedOutput, ofs>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A_RET<opName, rc, HasTiedOutput, ofs>;
  }
}

class DS_1A_RET_Tied<string opName, RegisterClass rc = VGPR_32> :
  DS_1A_RET<opName, rc, 1>;

class DS_1A_Off8_RET <string opName, RegisterClass rc = VGPR_32>
: DS_Pseudo<opName,
  (outs rc:$vdst),
  (ins VGPR_32:$addr, offset0:$offset0, offset1:$offset1, gds:$gds),
  "$vdst, $addr$offset0$offset1$gds"> {

  let has_offset = 0;
  let has_data0 = 0;
  let has_data1 = 0;
  let AsmMatchConverter = "cvtDSOffset01";
}

multiclass DS_1A_Off8_RET_mc <string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A_Off8_RET<opName, rc>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A_Off8_RET<opName, rc>;
  }
}

class DS_1A_RET_GDS <string opName> : DS_Pseudo<opName,
  (outs VGPR_32:$vdst),
  (ins VGPR_32:$addr, offset:$offset),
  "$vdst, $addr$offset gds"> {

  let has_data0 = 0;
  let has_data1 = 0;
  let has_gds = 0;
  let gdsValue = 1;
  let AsmMatchConverter = "cvtDSGds";
}

class DS_0A_RET <string opName> : DS_Pseudo<opName,
  (outs VGPR_32:$vdst),
  (ins offset:$offset, gds:$gds),
  "$vdst$offset$gds"> {

  let mayLoad = 1;
  let mayStore = 1;

  let has_addr = 0;
  let has_data0 = 0;
  let has_data1 = 0;
}

class DS_1A <string opName> : DS_Pseudo<opName,
  (outs),
  (ins VGPR_32:$addr, offset:$offset, gds:$gds),
  "$addr$offset$gds"> {

  let mayLoad = 1;
  let mayStore = 1;

  let has_vdst = 0;
  let has_data0 = 0;
  let has_data1 = 0;
}

multiclass DS_1A_mc <string opName> {
  def "" : DS_1A<opName>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A<opName>;
  }
}


class DS_GWS <string opName, dag ins, string asmOps>
: DS_Pseudo<opName, (outs), ins, asmOps> {

  let has_vdst  = 0;
  let has_addr  = 0;
  let has_data0 = 0;
  let has_data1 = 0;

  let has_gds   = 0;
  let gdsValue  = 1;
  let AsmMatchConverter = "cvtDSGds";
}

class DS_GWS_0D <string opName>
: DS_GWS<opName,
  (ins offset:$offset, gds:$gds), "$offset gds">;

class DS_GWS_1D <string opName>
: DS_GWS<opName,
  (ins VGPR_32:$data0, offset:$offset, gds:$gds), "$data0$offset gds"> {

  let has_data0 = 1;
}

class DS_VOID <string opName> : DS_Pseudo<opName,
  (outs), (ins), ""> {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 1;
  let UseNamedOperandTable = 0;
  let AsmMatchConverter = "";

  let has_vdst = 0;
  let has_addr = 0;
  let has_data0 = 0;
  let has_data1 = 0;
  let has_offset = 0;
  let has_offset0 = 0;
  let has_offset1 = 0;
  let has_gds = 0;
}

class DS_1A1D_PERMUTE <string opName, SDPatternOperator node = null_frag>
: DS_Pseudo<opName,
  (outs VGPR_32:$vdst),
  (ins VGPR_32:$addr, VGPR_32:$data0, offset:$offset),
  "$vdst, $addr, $data0$offset",
  [(set i32:$vdst,
   (node (DS1Addr1Offset i32:$addr, i16:$offset), i32:$data0))] > {

  let mayLoad = 0;
  let mayStore = 0;
  let isConvergent = 1;

  let has_data1 = 0;
  let has_gds = 0;
}

defm DS_ADD_U32       : DS_1A1D_NORET_mc<"ds_add_u32">;
defm DS_SUB_U32       : DS_1A1D_NORET_mc<"ds_sub_u32">;
defm DS_RSUB_U32      : DS_1A1D_NORET_mc<"ds_rsub_u32">;
defm DS_INC_U32       : DS_1A1D_NORET_mc<"ds_inc_u32">;
defm DS_DEC_U32       : DS_1A1D_NORET_mc<"ds_dec_u32">;
defm DS_MIN_I32       : DS_1A1D_NORET_mc<"ds_min_i32">;
defm DS_MAX_I32       : DS_1A1D_NORET_mc<"ds_max_i32">;
defm DS_MIN_U32       : DS_1A1D_NORET_mc<"ds_min_u32">;
defm DS_MAX_U32       : DS_1A1D_NORET_mc<"ds_max_u32">;
defm DS_AND_B32       : DS_1A1D_NORET_mc<"ds_and_b32">;
defm DS_OR_B32        : DS_1A1D_NORET_mc<"ds_or_b32">;
defm DS_XOR_B32       : DS_1A1D_NORET_mc<"ds_xor_b32">;
defm DS_ADD_F32       : DS_1A1D_NORET_mc<"ds_add_f32">;
defm DS_MIN_F32       : DS_1A1D_NORET_mc<"ds_min_f32">;
defm DS_MAX_F32       : DS_1A1D_NORET_mc<"ds_max_f32">;

let mayLoad = 0 in {
defm DS_WRITE_B8      : DS_1A1D_NORET_mc<"ds_write_b8">;
defm DS_WRITE_B16     : DS_1A1D_NORET_mc<"ds_write_b16">;
defm DS_WRITE_B32     : DS_1A1D_NORET_mc<"ds_write_b32">;
defm DS_WRITE2_B32    : DS_1A2D_Off8_NORET_mc<"ds_write2_b32">;
defm DS_WRITE2ST64_B32: DS_1A2D_Off8_NORET_mc<"ds_write2st64_b32">;


let has_m0_read = 0 in {

let SubtargetPredicate = HasD16LoadStore in {
def DS_WRITE_B8_D16_HI  : DS_1A1D_NORET<"ds_write_b8_d16_hi">;
def DS_WRITE_B16_D16_HI : DS_1A1D_NORET<"ds_write_b16_d16_hi">;
}

let SubtargetPredicate = HasDSAddTid in {
def DS_WRITE_ADDTID_B32 : DS_1A1D_NORET<"ds_write_addtid_b32">;
}

} // End has_m0_read = 0
} // End mayLoad = 0

defm DS_MSKOR_B32     : DS_1A2D_NORET_mc<"ds_mskor_b32">;
defm DS_CMPST_B32     : DS_1A2D_NORET_mc<"ds_cmpst_b32">;
defm DS_CMPST_F32     : DS_1A2D_NORET_mc<"ds_cmpst_f32">;

defm DS_ADD_U64       : DS_1A1D_NORET_mc<"ds_add_u64", VReg_64>;
defm DS_SUB_U64       : DS_1A1D_NORET_mc<"ds_sub_u64", VReg_64>;
defm DS_RSUB_U64      : DS_1A1D_NORET_mc<"ds_rsub_u64", VReg_64>;
defm DS_INC_U64       : DS_1A1D_NORET_mc<"ds_inc_u64", VReg_64>;
defm DS_DEC_U64       : DS_1A1D_NORET_mc<"ds_dec_u64", VReg_64>;
defm DS_MIN_I64       : DS_1A1D_NORET_mc<"ds_min_i64", VReg_64>;
defm DS_MAX_I64       : DS_1A1D_NORET_mc<"ds_max_i64", VReg_64>;
defm DS_MIN_U64       : DS_1A1D_NORET_mc<"ds_min_u64", VReg_64>;
defm DS_MAX_U64       : DS_1A1D_NORET_mc<"ds_max_u64", VReg_64>;
defm DS_AND_B64       : DS_1A1D_NORET_mc<"ds_and_b64", VReg_64>;
defm DS_OR_B64        : DS_1A1D_NORET_mc<"ds_or_b64", VReg_64>;
defm DS_XOR_B64       : DS_1A1D_NORET_mc<"ds_xor_b64", VReg_64>;
defm DS_MSKOR_B64     : DS_1A2D_NORET_mc<"ds_mskor_b64", VReg_64>;
let mayLoad = 0 in {
defm DS_WRITE_B64     : DS_1A1D_NORET_mc<"ds_write_b64", VReg_64>;
defm DS_WRITE2_B64    : DS_1A2D_Off8_NORET_mc<"ds_write2_b64", VReg_64>;
defm DS_WRITE2ST64_B64: DS_1A2D_Off8_NORET_mc<"ds_write2st64_b64", VReg_64>;
}
defm DS_CMPST_B64     : DS_1A2D_NORET_mc<"ds_cmpst_b64", VReg_64>;
defm DS_CMPST_F64     : DS_1A2D_NORET_mc<"ds_cmpst_f64", VReg_64>;
defm DS_MIN_F64       : DS_1A1D_NORET_mc<"ds_min_f64", VReg_64>;
defm DS_MAX_F64       : DS_1A1D_NORET_mc<"ds_max_f64", VReg_64>;

defm DS_ADD_RTN_U32   : DS_1A1D_RET_mc<"ds_add_rtn_u32", VGPR_32, "ds_add_u32">;
defm DS_ADD_RTN_F32   : DS_1A1D_RET_mc<"ds_add_rtn_f32", VGPR_32, "ds_add_f32">;
defm DS_SUB_RTN_U32   : DS_1A1D_RET_mc<"ds_sub_rtn_u32", VGPR_32, "ds_sub_u32">;
defm DS_RSUB_RTN_U32  : DS_1A1D_RET_mc<"ds_rsub_rtn_u32", VGPR_32, "ds_rsub_u32">;
defm DS_INC_RTN_U32   : DS_1A1D_RET_mc<"ds_inc_rtn_u32", VGPR_32, "ds_inc_u32">;
defm DS_DEC_RTN_U32   : DS_1A1D_RET_mc<"ds_dec_rtn_u32", VGPR_32, "ds_dec_u32">;
defm DS_MIN_RTN_I32   : DS_1A1D_RET_mc<"ds_min_rtn_i32", VGPR_32, "ds_min_i32">;
defm DS_MAX_RTN_I32   : DS_1A1D_RET_mc<"ds_max_rtn_i32", VGPR_32, "ds_max_i32">;
defm DS_MIN_RTN_U32   : DS_1A1D_RET_mc<"ds_min_rtn_u32", VGPR_32, "ds_min_u32">;
defm DS_MAX_RTN_U32   : DS_1A1D_RET_mc<"ds_max_rtn_u32", VGPR_32, "ds_max_u32">;
defm DS_AND_RTN_B32   : DS_1A1D_RET_mc<"ds_and_rtn_b32", VGPR_32, "ds_and_b32">;
defm DS_OR_RTN_B32    : DS_1A1D_RET_mc<"ds_or_rtn_b32", VGPR_32, "ds_or_b32">;
defm DS_XOR_RTN_B32   : DS_1A1D_RET_mc<"ds_xor_rtn_b32", VGPR_32, "ds_xor_b32">;
defm DS_MSKOR_RTN_B32 : DS_1A2D_RET_mc<"ds_mskor_rtn_b32", VGPR_32, "ds_mskor_b32">;
defm DS_CMPST_RTN_B32 : DS_1A2D_RET_mc<"ds_cmpst_rtn_b32", VGPR_32, "ds_cmpst_b32">;
defm DS_CMPST_RTN_F32 : DS_1A2D_RET_mc<"ds_cmpst_rtn_f32", VGPR_32, "ds_cmpst_f32">;
defm DS_MIN_RTN_F32   : DS_1A1D_RET_mc<"ds_min_rtn_f32", VGPR_32, "ds_min_f32">;
defm DS_MAX_RTN_F32   : DS_1A1D_RET_mc<"ds_max_rtn_f32", VGPR_32, "ds_max_f32">;

defm DS_WRXCHG_RTN_B32 : DS_1A1D_RET_mc<"ds_wrxchg_rtn_b32">;
defm DS_WRXCHG2_RTN_B32 : DS_1A2D_Off8_RET_mc<"ds_wrxchg2_rtn_b32", VReg_64, VGPR_32>;
defm DS_WRXCHG2ST64_RTN_B32 : DS_1A2D_Off8_RET_mc<"ds_wrxchg2st64_rtn_b32", VReg_64, VGPR_32>;

defm DS_ADD_RTN_U64  : DS_1A1D_RET_mc<"ds_add_rtn_u64", VReg_64, "ds_add_u64">;
defm DS_SUB_RTN_U64  : DS_1A1D_RET_mc<"ds_sub_rtn_u64", VReg_64, "ds_sub_u64">;
defm DS_RSUB_RTN_U64  : DS_1A1D_RET_mc<"ds_rsub_rtn_u64", VReg_64, "ds_rsub_u64">;
defm DS_INC_RTN_U64   : DS_1A1D_RET_mc<"ds_inc_rtn_u64", VReg_64, "ds_inc_u64">;
defm DS_DEC_RTN_U64   : DS_1A1D_RET_mc<"ds_dec_rtn_u64", VReg_64, "ds_dec_u64">;
defm DS_MIN_RTN_I64    : DS_1A1D_RET_mc<"ds_min_rtn_i64", VReg_64, "ds_min_i64">;
defm DS_MAX_RTN_I64    : DS_1A1D_RET_mc<"ds_max_rtn_i64", VReg_64, "ds_max_i64">;
defm DS_MIN_RTN_U64   : DS_1A1D_RET_mc<"ds_min_rtn_u64", VReg_64, "ds_min_u64">;
defm DS_MAX_RTN_U64   : DS_1A1D_RET_mc<"ds_max_rtn_u64", VReg_64, "ds_max_u64">;
defm DS_AND_RTN_B64    : DS_1A1D_RET_mc<"ds_and_rtn_b64", VReg_64, "ds_and_b64">;
defm DS_OR_RTN_B64     : DS_1A1D_RET_mc<"ds_or_rtn_b64", VReg_64, "ds_or_b64">;
defm DS_XOR_RTN_B64    : DS_1A1D_RET_mc<"ds_xor_rtn_b64", VReg_64, "ds_xor_b64">;
defm DS_MSKOR_RTN_B64  : DS_1A2D_RET_mc<"ds_mskor_rtn_b64", VReg_64, "ds_mskor_b64">;
defm DS_CMPST_RTN_B64  : DS_1A2D_RET_mc<"ds_cmpst_rtn_b64", VReg_64, "ds_cmpst_b64">;
defm DS_CMPST_RTN_F64  : DS_1A2D_RET_mc<"ds_cmpst_rtn_f64", VReg_64, "ds_cmpst_f64">;
defm DS_MIN_RTN_F64    : DS_1A1D_RET_mc<"ds_min_rtn_f64", VReg_64, "ds_min_f64">;
defm DS_MAX_RTN_F64    : DS_1A1D_RET_mc<"ds_max_rtn_f64", VReg_64, "ds_max_f64">;

defm DS_WRXCHG_RTN_B64 : DS_1A1D_RET_mc<"ds_wrxchg_rtn_b64", VReg_64>;
defm DS_WRXCHG2_RTN_B64 : DS_1A2D_Off8_RET_mc<"ds_wrxchg2_rtn_b64", VReg_128, VReg_64>;
defm DS_WRXCHG2ST64_RTN_B64 : DS_1A2D_Off8_RET_mc<"ds_wrxchg2st64_rtn_b64", VReg_128, VReg_64>;

def DS_GWS_INIT       : DS_GWS_1D<"ds_gws_init">;
def DS_GWS_SEMA_V     : DS_GWS_0D<"ds_gws_sema_v">;
def DS_GWS_SEMA_BR    : DS_GWS_1D<"ds_gws_sema_br">;
def DS_GWS_SEMA_P     : DS_GWS_0D<"ds_gws_sema_p">;
def DS_GWS_BARRIER    : DS_GWS_1D<"ds_gws_barrier">;

def DS_ADD_SRC2_U32   : DS_1A<"ds_add_src2_u32">;
def DS_SUB_SRC2_U32   : DS_1A<"ds_sub_src2_u32">;
def DS_RSUB_SRC2_U32  : DS_1A<"ds_rsub_src2_u32">;
def DS_INC_SRC2_U32   : DS_1A<"ds_inc_src2_u32">;
def DS_DEC_SRC2_U32   : DS_1A<"ds_dec_src2_u32">;
def DS_MIN_SRC2_I32   : DS_1A<"ds_min_src2_i32">;
def DS_MAX_SRC2_I32   : DS_1A<"ds_max_src2_i32">;
def DS_MIN_SRC2_U32   : DS_1A<"ds_min_src2_u32">;
def DS_MAX_SRC2_U32   : DS_1A<"ds_max_src2_u32">;
def DS_AND_SRC2_B32   : DS_1A<"ds_and_src2_b32">;
def DS_OR_SRC2_B32    : DS_1A<"ds_or_src2_b32">;
def DS_XOR_SRC2_B32   : DS_1A<"ds_xor_src2_b32">;
def DS_MIN_SRC2_F32   : DS_1A<"ds_min_src2_f32">;
def DS_MAX_SRC2_F32   : DS_1A<"ds_max_src2_f32">;

def DS_ADD_SRC2_U64   : DS_1A<"ds_add_src2_u64">;
def DS_SUB_SRC2_U64   : DS_1A<"ds_sub_src2_u64">;
def DS_RSUB_SRC2_U64  : DS_1A<"ds_rsub_src2_u64">;
def DS_INC_SRC2_U64   : DS_1A<"ds_inc_src2_u64">;
def DS_DEC_SRC2_U64   : DS_1A<"ds_dec_src2_u64">;
def DS_MIN_SRC2_I64   : DS_1A<"ds_min_src2_i64">;
def DS_MAX_SRC2_I64   : DS_1A<"ds_max_src2_i64">;
def DS_MIN_SRC2_U64   : DS_1A<"ds_min_src2_u64">;
def DS_MAX_SRC2_U64   : DS_1A<"ds_max_src2_u64">;
def DS_AND_SRC2_B64   : DS_1A<"ds_and_src2_b64">;
def DS_OR_SRC2_B64    : DS_1A<"ds_or_src2_b64">;
def DS_XOR_SRC2_B64   : DS_1A<"ds_xor_src2_b64">;
def DS_MIN_SRC2_F64   : DS_1A<"ds_min_src2_f64">;
def DS_MAX_SRC2_F64   : DS_1A<"ds_max_src2_f64">;

def DS_WRITE_SRC2_B32 : DS_1A<"ds_write_src2_b32">;
def DS_WRITE_SRC2_B64 : DS_1A<"ds_write_src2_b64">;

let Uses = [EXEC], mayLoad = 0, mayStore = 0, isConvergent = 1 in {
def DS_SWIZZLE_B32 : DS_1A_RET <"ds_swizzle_b32", VGPR_32, 0, SwizzleImm>;
}

let mayStore = 0 in {
defm DS_READ_I8      : DS_1A_RET_mc<"ds_read_i8">;
defm DS_READ_U8      : DS_1A_RET_mc<"ds_read_u8">;
defm DS_READ_I16     : DS_1A_RET_mc<"ds_read_i16">;
defm DS_READ_U16     : DS_1A_RET_mc<"ds_read_u16">;
defm DS_READ_B32     : DS_1A_RET_mc<"ds_read_b32">;
defm DS_READ_B64     : DS_1A_RET_mc<"ds_read_b64", VReg_64>;

defm DS_READ2_B32    : DS_1A_Off8_RET_mc<"ds_read2_b32", VReg_64>;
defm DS_READ2ST64_B32: DS_1A_Off8_RET_mc<"ds_read2st64_b32", VReg_64>;

defm DS_READ2_B64    : DS_1A_Off8_RET_mc<"ds_read2_b64", VReg_128>;
defm DS_READ2ST64_B64: DS_1A_Off8_RET_mc<"ds_read2st64_b64", VReg_128>;

let has_m0_read = 0 in {
let SubtargetPredicate = HasD16LoadStore in {
def DS_READ_U8_D16     : DS_1A_RET_Tied<"ds_read_u8_d16">;
def DS_READ_U8_D16_HI  : DS_1A_RET_Tied<"ds_read_u8_d16_hi">;
def DS_READ_I8_D16     : DS_1A_RET_Tied<"ds_read_i8_d16">;
def DS_READ_I8_D16_HI  : DS_1A_RET_Tied<"ds_read_i8_d16_hi">;
def DS_READ_U16_D16    : DS_1A_RET_Tied<"ds_read_u16_d16">;
def DS_READ_U16_D16_HI : DS_1A_RET_Tied<"ds_read_u16_d16_hi">;
}

let SubtargetPredicate = HasDSAddTid in {
def DS_READ_ADDTID_B32 : DS_1A_RET<"ds_read_addtid_b32">;
}
} // End has_m0_read = 0
}

def DS_CONSUME       : DS_0A_RET<"ds_consume">;
def DS_APPEND        : DS_0A_RET<"ds_append">;
def DS_ORDERED_COUNT : DS_1A_RET_GDS<"ds_ordered_count">;

//===----------------------------------------------------------------------===//
// Instruction definitions for CI and newer.
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isCIVI in {

defm DS_WRAP_RTN_B32 : DS_1A2D_RET_mc<"ds_wrap_rtn_b32", VGPR_32>;
defm DS_CONDXCHG32_RTN_B64 : DS_1A1D_RET_mc<"ds_condxchg32_rtn_b64", VReg_64>;

def DS_GWS_SEMA_RELEASE_ALL : DS_GWS_0D<"ds_gws_sema_release_all">;

let mayStore = 0 in {
defm DS_READ_B96 : DS_1A_RET_mc<"ds_read_b96", VReg_96>;
defm DS_READ_B128: DS_1A_RET_mc<"ds_read_b128", VReg_128>;
} // End mayStore = 0

let mayLoad = 0 in {
defm DS_WRITE_B96 : DS_1A1D_NORET_mc<"ds_write_b96", VReg_96>;
defm DS_WRITE_B128 : DS_1A1D_NORET_mc<"ds_write_b128", VReg_128>;
} // End mayLoad = 0

def DS_NOP : DS_VOID<"ds_nop">;

} // let SubtargetPredicate = isCIVI

//===----------------------------------------------------------------------===//
// Instruction definitions for VI and newer.
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isVI in {

let Uses = [EXEC] in {
def DS_PERMUTE_B32  : DS_1A1D_PERMUTE <"ds_permute_b32",
                                       int_amdgcn_ds_permute>;
def DS_BPERMUTE_B32 : DS_1A1D_PERMUTE <"ds_bpermute_b32",
                                       int_amdgcn_ds_bpermute>;
}

def DS_ADD_SRC2_F32 : DS_1A<"ds_add_src2_f32">;

} // let SubtargetPredicate = isVI

//===----------------------------------------------------------------------===//
// DS Patterns
//===----------------------------------------------------------------------===//

def : GCNPat <
  (int_amdgcn_ds_swizzle i32:$src, imm:$offset16),
  (DS_SWIZZLE_B32 $src, (as_i16imm $offset16), (i1 0))
>;

class DSReadPat <DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat <
  (vt (frag (DS1Addr1Offset i32:$ptr, i32:$offset))),
  (inst $ptr, (as_i16imm $offset), (i1 0))
>;

multiclass DSReadPat_mc<DS_Pseudo inst, ValueType vt, string frag> {

  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSReadPat<inst, vt, !cast<PatFrag>(frag#"_m0")>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSReadPat<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt, !cast<PatFrag>(frag)>;
  }
}


multiclass DSReadPat_Hi16 <DS_Pseudo inst, PatFrag frag, ValueType vt = i16> {
  def : GCNPat <
    (build_vector vt:$lo, (vt (frag (DS1Addr1Offset i32:$ptr, i32:$offset)))),
    (v2i16 (inst $ptr, (as_i16imm $offset), (i1 0), $lo))
  >;

  def : GCNPat <
    (build_vector f16:$lo, (f16 (bitconvert (vt (frag (DS1Addr1Offset i32:$ptr, i32:$offset)))))),
    (v2f16 (inst $ptr, (as_i16imm $offset), (i1 0), $lo))
  >;
}

multiclass DSReadPat_Lo16 <DS_Pseudo inst, PatFrag frag, ValueType vt = i16> {
  def : GCNPat <
    (build_vector (vt (frag (DS1Addr1Offset i32:$ptr, i32:$offset))), (vt (Hi16Elt vt:$hi))),
    (v2i16 (inst $ptr, (as_i16imm $offset), 0, $hi))
  >;

  def : GCNPat <
    (build_vector (f16 (bitconvert (vt (frag (DS1Addr1Offset i32:$ptr, i32:$offset))))), (f16 (Hi16Elt f16:$hi))),
    (v2f16 (inst $ptr, (as_i16imm $offset), 0, $hi))
  >;
}

defm : DSReadPat_mc <DS_READ_I8, i32, "sextloadi8_local">;
defm : DSReadPat_mc <DS_READ_U8,  i32, "az_extloadi8_local">;
defm : DSReadPat_mc <DS_READ_I8,  i16, "sextloadi8_local">;
defm : DSReadPat_mc <DS_READ_U8,  i16, "az_extloadi8_local">;
defm : DSReadPat_mc <DS_READ_I16, i32, "sextloadi16_local">;
defm : DSReadPat_mc <DS_READ_I16, i32, "sextloadi16_local">;
defm : DSReadPat_mc <DS_READ_U16, i32, "az_extloadi16_local">;
defm : DSReadPat_mc <DS_READ_U16, i16, "load_local">;
defm : DSReadPat_mc <DS_READ_B32, i32, "load_local">;
defm : DSReadPat_mc <DS_READ_B32, i32, "atomic_load_32_local">;
defm : DSReadPat_mc <DS_READ_B64, i64, "atomic_load_64_local">;

let AddedComplexity = 100 in {

defm : DSReadPat_mc <DS_READ_B64, v2i32, "load_align8_local">;
defm : DSReadPat_mc <DS_READ_B128, v4i32, "load_align16_local">;

} // End AddedComplexity = 100

let OtherPredicates = [D16PreservesUnusedBits] in {
let AddedComplexity = 100 in {
defm : DSReadPat_Hi16<DS_READ_U16_D16_HI, load_local>;
defm : DSReadPat_Hi16<DS_READ_U8_D16_HI, az_extloadi8_local>;
defm : DSReadPat_Hi16<DS_READ_I8_D16_HI, sextloadi8_local>;

defm : DSReadPat_Lo16<DS_READ_U16_D16, load_local>;
defm : DSReadPat_Lo16<DS_READ_U8_D16, az_extloadi8_local>;
defm : DSReadPat_Lo16<DS_READ_I8_D16, sextloadi8_local>;

}
}

class DSWritePat <DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat <
  (frag vt:$value, (DS1Addr1Offset i32:$ptr, i32:$offset)),
  (inst $ptr, $value, (as_i16imm $offset), (i1 0))
>;

multiclass DSWritePat_mc <DS_Pseudo inst, ValueType vt, string frag> {
  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSWritePat<inst, vt, !cast<PatFrag>(frag#"_m0")>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSWritePat<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt, !cast<PatFrag>(frag)>;
  }
}

// Irritatingly, atomic_store reverses the order of operands from a
// normal store.
class DSAtomicWritePat <DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat <
  (frag (DS1Addr1Offset i32:$ptr, i32:$offset), vt:$value),
  (inst $ptr, $value, (as_i16imm $offset), (i1 0))
>;

multiclass DSAtomicWritePat_mc <DS_Pseudo inst, ValueType vt, string frag> {
  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSAtomicWritePat<inst, vt, !cast<PatFrag>(frag#"_m0")>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSAtomicWritePat<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt, !cast<PatFrag>(frag)>;
  }
}

defm : DSWritePat_mc <DS_WRITE_B8, i32, "truncstorei8_local">;
defm : DSWritePat_mc <DS_WRITE_B16, i32, "truncstorei16_local">;
defm : DSWritePat_mc <DS_WRITE_B8, i16, "truncstorei8_local">;
defm : DSWritePat_mc <DS_WRITE_B16, i16, "store_local">;
defm : DSWritePat_mc <DS_WRITE_B32, i32, "store_local">;
defm : DSAtomicWritePat_mc <DS_WRITE_B32, i32, "atomic_store_local">;
defm : DSAtomicWritePat_mc <DS_WRITE_B64, i64, "atomic_store_local">;

let OtherPredicates = [D16PreservesUnusedBits] in {
def : DSWritePat <DS_WRITE_B16_D16_HI, i32, store_local_hi16>;
def : DSWritePat <DS_WRITE_B8_D16_HI, i32, truncstorei8_local_hi16>;
}


class DS64Bit4ByteAlignedReadPat<DS_Pseudo inst, PatFrag frag> : GCNPat <
  (v2i32 (frag (DS64Bit4ByteAligned i32:$ptr, i8:$offset0, i8:$offset1))),
  (inst $ptr, $offset0, $offset1, (i1 0))
>;

class DS64Bit4ByteAlignedWritePat<DS_Pseudo inst, PatFrag frag> : GCNPat<
  (frag v2i32:$value, (DS64Bit4ByteAligned i32:$ptr, i8:$offset0, i8:$offset1)),
  (inst $ptr, (i32 (EXTRACT_SUBREG $value, sub0)),
              (i32 (EXTRACT_SUBREG $value, sub1)), $offset0, $offset1,
              (i1 0))
>;

// v2i32 loads are split into i32 loads on SI during lowering, due to a bug
// related to bounds checking.
let OtherPredicates = [LDSRequiresM0Init, isCIVI] in {
def : DS64Bit4ByteAlignedReadPat<DS_READ2_B32, load_local_m0>;
def : DS64Bit4ByteAlignedWritePat<DS_WRITE2_B32, store_local_m0>;
}

let OtherPredicates = [NotLDSRequiresM0Init] in {
def : DS64Bit4ByteAlignedReadPat<DS_READ2_B32_gfx9, load_local>;
def : DS64Bit4ByteAlignedWritePat<DS_WRITE2_B32_gfx9, store_local>;
}


let AddedComplexity = 100 in {

defm : DSWritePat_mc <DS_WRITE_B64, v2i32, "store_align8_local">;
defm : DSWritePat_mc <DS_WRITE_B128, v4i32, "store_align16_local">;

} // End AddedComplexity = 100
class DSAtomicRetPat<DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat <
  (frag (DS1Addr1Offset i32:$ptr, i32:$offset), vt:$value),
  (inst $ptr, $value, (as_i16imm $offset), (i1 0))
>;

multiclass DSAtomicRetPat_mc<DS_Pseudo inst, ValueType vt, string frag> {
  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSAtomicRetPat<inst, vt, !cast<PatFrag>(frag#"_m0")>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSAtomicRetPat<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt,
                         !cast<PatFrag>(frag)>;
  }
}



class DSAtomicCmpXChg<DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat <
  (frag (DS1Addr1Offset i32:$ptr, i32:$offset), vt:$cmp, vt:$swap),
  (inst $ptr, $cmp, $swap, (as_i16imm $offset), (i1 0))
>;

multiclass DSAtomicCmpXChg_mc<DS_Pseudo inst, ValueType vt, string frag> {
  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSAtomicCmpXChg<inst, vt, !cast<PatFrag>(frag#"_m0")>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSAtomicCmpXChg<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt,
                          !cast<PatFrag>(frag)>;
  }
}



// 32-bit atomics.
defm : DSAtomicRetPat_mc<DS_WRXCHG_RTN_B32, i32, "atomic_swap_local">;
defm : DSAtomicRetPat_mc<DS_ADD_RTN_U32, i32, "atomic_load_add_local">;
defm : DSAtomicRetPat_mc<DS_SUB_RTN_U32, i32, "atomic_load_sub_local">;
defm : DSAtomicRetPat_mc<DS_INC_RTN_U32, i32, "atomic_inc_local">;
defm : DSAtomicRetPat_mc<DS_DEC_RTN_U32, i32, "atomic_dec_local">;
defm : DSAtomicRetPat_mc<DS_AND_RTN_B32, i32, "atomic_load_and_local">;
defm : DSAtomicRetPat_mc<DS_OR_RTN_B32, i32, "atomic_load_or_local">;
defm : DSAtomicRetPat_mc<DS_XOR_RTN_B32, i32, "atomic_load_xor_local">;
defm : DSAtomicRetPat_mc<DS_MIN_RTN_I32, i32, "atomic_load_min_local">;
defm : DSAtomicRetPat_mc<DS_MAX_RTN_I32, i32, "atomic_load_max_local">;
defm : DSAtomicRetPat_mc<DS_MIN_RTN_U32, i32, "atomic_load_umin_local">;
defm : DSAtomicRetPat_mc<DS_MAX_RTN_U32, i32, "atomic_load_umax_local">;
defm : DSAtomicCmpXChg_mc<DS_CMPST_RTN_B32, i32, "atomic_cmp_swap_local">;
defm : DSAtomicRetPat_mc<DS_MIN_RTN_F32, f32, "atomic_load_fmin_local">;
defm : DSAtomicRetPat_mc<DS_MAX_RTN_F32, f32, "atomic_load_fmax_local">;
defm : DSAtomicRetPat_mc<DS_ADD_RTN_F32, f32, "atomic_load_fadd_local">;

// 64-bit atomics.
defm : DSAtomicRetPat_mc<DS_WRXCHG_RTN_B64, i64, "atomic_swap_local">;
defm : DSAtomicRetPat_mc<DS_ADD_RTN_U64, i64, "atomic_load_add_local">;
defm : DSAtomicRetPat_mc<DS_SUB_RTN_U64, i64, "atomic_load_sub_local">;
defm : DSAtomicRetPat_mc<DS_INC_RTN_U64, i64, "atomic_inc_local">;
defm : DSAtomicRetPat_mc<DS_DEC_RTN_U64, i64, "atomic_dec_local">;
defm : DSAtomicRetPat_mc<DS_AND_RTN_B64, i64, "atomic_load_and_local">;
defm : DSAtomicRetPat_mc<DS_OR_RTN_B64, i64, "atomic_load_or_local">;
defm : DSAtomicRetPat_mc<DS_XOR_RTN_B64, i64, "atomic_load_xor_local">;
defm : DSAtomicRetPat_mc<DS_MIN_RTN_I64, i64, "atomic_load_min_local">;
defm : DSAtomicRetPat_mc<DS_MAX_RTN_I64, i64, "atomic_load_max_local">;
defm : DSAtomicRetPat_mc<DS_MIN_RTN_U64, i64, "atomic_load_umin_local">;
defm : DSAtomicRetPat_mc<DS_MAX_RTN_U64, i64, "atomic_load_umax_local">;

defm : DSAtomicCmpXChg_mc<DS_CMPST_RTN_B64, i64, "atomic_cmp_swap_local">;

def : Pat <
  (SIds_ordered_count i32:$value, i16:$offset),
  (DS_ORDERED_COUNT $value, (as_i16imm $offset))
>;

//===----------------------------------------------------------------------===//
// Real instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SIInstructions.td
//===----------------------------------------------------------------------===//

class DS_Real_si <bits<8> op, DS_Pseudo ds> :
  DS_Real <ds>,
  SIMCInstr <ds.Mnemonic, SIEncodingFamily.SI> {
  let AssemblerPredicates=[isSICI];
  let DecoderNamespace="SICI";

  // encoding
  let Inst{7-0}   = !if(ds.has_offset0, offset0, 0);
  let Inst{15-8}  = !if(ds.has_offset1, offset1, 0);
  let Inst{17}    = !if(ds.has_gds, gds, ds.gdsValue);
  let Inst{25-18} = op;
  let Inst{31-26} = 0x36; // ds prefix
  let Inst{39-32} = !if(ds.has_addr, addr, 0);
  let Inst{47-40} = !if(ds.has_data0, data0, 0);
  let Inst{55-48} = !if(ds.has_data1, data1, 0);
  let Inst{63-56} = !if(ds.has_vdst, vdst, 0);
}

def DS_ADD_U32_si         : DS_Real_si<0x0,  DS_ADD_U32>;
def DS_SUB_U32_si         : DS_Real_si<0x1,  DS_SUB_U32>;
def DS_RSUB_U32_si        : DS_Real_si<0x2,  DS_RSUB_U32>;
def DS_INC_U32_si         : DS_Real_si<0x3,  DS_INC_U32>;
def DS_DEC_U32_si         : DS_Real_si<0x4,  DS_DEC_U32>;
def DS_MIN_I32_si         : DS_Real_si<0x5,  DS_MIN_I32>;
def DS_MAX_I32_si         : DS_Real_si<0x6,  DS_MAX_I32>;
def DS_MIN_U32_si         : DS_Real_si<0x7,  DS_MIN_U32>;
def DS_MAX_U32_si         : DS_Real_si<0x8,  DS_MAX_U32>;
def DS_AND_B32_si         : DS_Real_si<0x9,  DS_AND_B32>;
def DS_OR_B32_si          : DS_Real_si<0xa,  DS_OR_B32>;
def DS_XOR_B32_si         : DS_Real_si<0xb,  DS_XOR_B32>;
def DS_MSKOR_B32_si       : DS_Real_si<0xc,  DS_MSKOR_B32>;
def DS_WRITE_B32_si       : DS_Real_si<0xd,  DS_WRITE_B32>;
def DS_WRITE2_B32_si      : DS_Real_si<0xe,  DS_WRITE2_B32>;
def DS_WRITE2ST64_B32_si  : DS_Real_si<0xf,  DS_WRITE2ST64_B32>;
def DS_CMPST_B32_si       : DS_Real_si<0x10, DS_CMPST_B32>;
def DS_CMPST_F32_si       : DS_Real_si<0x11, DS_CMPST_F32>;
def DS_MIN_F32_si         : DS_Real_si<0x12, DS_MIN_F32>;
def DS_MAX_F32_si         : DS_Real_si<0x13, DS_MAX_F32>;
def DS_NOP_si             : DS_Real_si<0x14, DS_NOP>;
def DS_GWS_INIT_si        : DS_Real_si<0x19, DS_GWS_INIT>;
def DS_GWS_SEMA_V_si      : DS_Real_si<0x1a, DS_GWS_SEMA_V>;
def DS_GWS_SEMA_BR_si     : DS_Real_si<0x1b, DS_GWS_SEMA_BR>;
def DS_GWS_SEMA_P_si      : DS_Real_si<0x1c, DS_GWS_SEMA_P>;
def DS_GWS_BARRIER_si     : DS_Real_si<0x1d, DS_GWS_BARRIER>;
def DS_WRITE_B8_si        : DS_Real_si<0x1e, DS_WRITE_B8>;
def DS_WRITE_B16_si       : DS_Real_si<0x1f, DS_WRITE_B16>;
def DS_ADD_RTN_U32_si     : DS_Real_si<0x20, DS_ADD_RTN_U32>;
def DS_SUB_RTN_U32_si     : DS_Real_si<0x21, DS_SUB_RTN_U32>;
def DS_RSUB_RTN_U32_si    : DS_Real_si<0x22, DS_RSUB_RTN_U32>;
def DS_INC_RTN_U32_si     : DS_Real_si<0x23, DS_INC_RTN_U32>;
def DS_DEC_RTN_U32_si     : DS_Real_si<0x24, DS_DEC_RTN_U32>;
def DS_MIN_RTN_I32_si     : DS_Real_si<0x25, DS_MIN_RTN_I32>;
def DS_MAX_RTN_I32_si     : DS_Real_si<0x26, DS_MAX_RTN_I32>;
def DS_MIN_RTN_U32_si     : DS_Real_si<0x27, DS_MIN_RTN_U32>;
def DS_MAX_RTN_U32_si     : DS_Real_si<0x28, DS_MAX_RTN_U32>;
def DS_AND_RTN_B32_si     : DS_Real_si<0x29, DS_AND_RTN_B32>;
def DS_OR_RTN_B32_si      : DS_Real_si<0x2a, DS_OR_RTN_B32>;
def DS_XOR_RTN_B32_si     : DS_Real_si<0x2b, DS_XOR_RTN_B32>;
def DS_MSKOR_RTN_B32_si   : DS_Real_si<0x2c, DS_MSKOR_RTN_B32>;
def DS_WRXCHG_RTN_B32_si  : DS_Real_si<0x2d, DS_WRXCHG_RTN_B32>;
def DS_WRXCHG2_RTN_B32_si : DS_Real_si<0x2e, DS_WRXCHG2_RTN_B32>;
def DS_WRXCHG2ST64_RTN_B32_si : DS_Real_si<0x2f, DS_WRXCHG2ST64_RTN_B32>;
def DS_CMPST_RTN_B32_si   : DS_Real_si<0x30, DS_CMPST_RTN_B32>;
def DS_CMPST_RTN_F32_si   : DS_Real_si<0x31, DS_CMPST_RTN_F32>;
def DS_MIN_RTN_F32_si     : DS_Real_si<0x32, DS_MIN_RTN_F32>;
def DS_MAX_RTN_F32_si     : DS_Real_si<0x33, DS_MAX_RTN_F32>;

// These instruction are CI/VI only
def DS_WRAP_RTN_B32_si    : DS_Real_si<0x34, DS_WRAP_RTN_B32>;
def DS_CONDXCHG32_RTN_B64_si   : DS_Real_si<0x7e, DS_CONDXCHG32_RTN_B64>;
def DS_GWS_SEMA_RELEASE_ALL_si : DS_Real_si<0x18, DS_GWS_SEMA_RELEASE_ALL>;

def DS_SWIZZLE_B32_si     : DS_Real_si<0x35, DS_SWIZZLE_B32>;
def DS_READ_B32_si        : DS_Real_si<0x36, DS_READ_B32>;
def DS_READ2_B32_si       : DS_Real_si<0x37, DS_READ2_B32>;
def DS_READ2ST64_B32_si   : DS_Real_si<0x38, DS_READ2ST64_B32>;
def DS_READ_I8_si         : DS_Real_si<0x39, DS_READ_I8>;
def DS_READ_U8_si         : DS_Real_si<0x3a, DS_READ_U8>;
def DS_READ_I16_si        : DS_Real_si<0x3b, DS_READ_I16>;
def DS_READ_U16_si        : DS_Real_si<0x3c, DS_READ_U16>;
def DS_CONSUME_si         : DS_Real_si<0x3d, DS_CONSUME>;
def DS_APPEND_si          : DS_Real_si<0x3e, DS_APPEND>;
def DS_ORDERED_COUNT_si   : DS_Real_si<0x3f, DS_ORDERED_COUNT>;
def DS_ADD_U64_si         : DS_Real_si<0x40, DS_ADD_U64>;
def DS_SUB_U64_si         : DS_Real_si<0x41, DS_SUB_U64>;
def DS_RSUB_U64_si        : DS_Real_si<0x42, DS_RSUB_U64>;
def DS_INC_U64_si         : DS_Real_si<0x43, DS_INC_U64>;
def DS_DEC_U64_si         : DS_Real_si<0x44, DS_DEC_U64>;
def DS_MIN_I64_si         : DS_Real_si<0x45, DS_MIN_I64>;
def DS_MAX_I64_si         : DS_Real_si<0x46, DS_MAX_I64>;
def DS_MIN_U64_si         : DS_Real_si<0x47, DS_MIN_U64>;
def DS_MAX_U64_si         : DS_Real_si<0x48, DS_MAX_U64>;
def DS_AND_B64_si         : DS_Real_si<0x49, DS_AND_B64>;
def DS_OR_B64_si          : DS_Real_si<0x4a, DS_OR_B64>;
def DS_XOR_B64_si         : DS_Real_si<0x4b, DS_XOR_B64>;
def DS_MSKOR_B64_si       : DS_Real_si<0x4c, DS_MSKOR_B64>;
def DS_WRITE_B64_si       : DS_Real_si<0x4d, DS_WRITE_B64>;
def DS_WRITE2_B64_si      : DS_Real_si<0x4E, DS_WRITE2_B64>;
def DS_WRITE2ST64_B64_si  : DS_Real_si<0x4f, DS_WRITE2ST64_B64>;
def DS_CMPST_B64_si       : DS_Real_si<0x50, DS_CMPST_B64>;
def DS_CMPST_F64_si       : DS_Real_si<0x51, DS_CMPST_F64>;
def DS_MIN_F64_si         : DS_Real_si<0x52, DS_MIN_F64>;
def DS_MAX_F64_si         : DS_Real_si<0x53, DS_MAX_F64>;

def DS_ADD_RTN_U64_si     : DS_Real_si<0x60, DS_ADD_RTN_U64>;
def DS_SUB_RTN_U64_si     : DS_Real_si<0x61, DS_SUB_RTN_U64>;
def DS_RSUB_RTN_U64_si    : DS_Real_si<0x62, DS_RSUB_RTN_U64>;
def DS_INC_RTN_U64_si     : DS_Real_si<0x63, DS_INC_RTN_U64>;
def DS_DEC_RTN_U64_si     : DS_Real_si<0x64, DS_DEC_RTN_U64>;
def DS_MIN_RTN_I64_si     : DS_Real_si<0x65, DS_MIN_RTN_I64>;
def DS_MAX_RTN_I64_si     : DS_Real_si<0x66, DS_MAX_RTN_I64>;
def DS_MIN_RTN_U64_si     : DS_Real_si<0x67, DS_MIN_RTN_U64>;
def DS_MAX_RTN_U64_si     : DS_Real_si<0x68, DS_MAX_RTN_U64>;
def DS_AND_RTN_B64_si     : DS_Real_si<0x69, DS_AND_RTN_B64>;
def DS_OR_RTN_B64_si      : DS_Real_si<0x6a, DS_OR_RTN_B64>;
def DS_XOR_RTN_B64_si     : DS_Real_si<0x6b, DS_XOR_RTN_B64>;
def DS_MSKOR_RTN_B64_si   : DS_Real_si<0x6c, DS_MSKOR_RTN_B64>;
def DS_WRXCHG_RTN_B64_si  : DS_Real_si<0x6d, DS_WRXCHG_RTN_B64>;
def DS_WRXCHG2_RTN_B64_si : DS_Real_si<0x6e, DS_WRXCHG2_RTN_B64>;
def DS_WRXCHG2ST64_RTN_B64_si : DS_Real_si<0x6f, DS_WRXCHG2ST64_RTN_B64>;
def DS_CMPST_RTN_B64_si   : DS_Real_si<0x70, DS_CMPST_RTN_B64>;
def DS_CMPST_RTN_F64_si   : DS_Real_si<0x71, DS_CMPST_RTN_F64>;
def DS_MIN_RTN_F64_si     : DS_Real_si<0x72, DS_MIN_RTN_F64>;
def DS_MAX_RTN_F64_si     : DS_Real_si<0x73, DS_MAX_RTN_F64>;

def DS_READ_B64_si        : DS_Real_si<0x76, DS_READ_B64>;
def DS_READ2_B64_si       : DS_Real_si<0x77, DS_READ2_B64>;
def DS_READ2ST64_B64_si   : DS_Real_si<0x78, DS_READ2ST64_B64>;

def DS_ADD_SRC2_U32_si    : DS_Real_si<0x80, DS_ADD_SRC2_U32>;
def DS_SUB_SRC2_U32_si    : DS_Real_si<0x81, DS_SUB_SRC2_U32>;
def DS_RSUB_SRC2_U32_si   : DS_Real_si<0x82, DS_RSUB_SRC2_U32>;
def DS_INC_SRC2_U32_si    : DS_Real_si<0x83, DS_INC_SRC2_U32>;
def DS_DEC_SRC2_U32_si    : DS_Real_si<0x84, DS_DEC_SRC2_U32>;
def DS_MIN_SRC2_I32_si    : DS_Real_si<0x85, DS_MIN_SRC2_I32>;
def DS_MAX_SRC2_I32_si    : DS_Real_si<0x86, DS_MAX_SRC2_I32>;
def DS_MIN_SRC2_U32_si    : DS_Real_si<0x87, DS_MIN_SRC2_U32>;
def DS_MAX_SRC2_U32_si    : DS_Real_si<0x88, DS_MAX_SRC2_U32>;
def DS_AND_SRC2_B32_si    : DS_Real_si<0x89, DS_AND_SRC2_B32>;
def DS_OR_SRC2_B32_si     : DS_Real_si<0x8a, DS_OR_SRC2_B32>;
def DS_XOR_SRC2_B32_si    : DS_Real_si<0x8b, DS_XOR_SRC2_B32>;
def DS_WRITE_SRC2_B32_si  : DS_Real_si<0x8d, DS_WRITE_SRC2_B32>;

def DS_MIN_SRC2_F32_si    : DS_Real_si<0x92, DS_MIN_SRC2_F32>;
def DS_MAX_SRC2_F32_si    : DS_Real_si<0x93, DS_MAX_SRC2_F32>;

def DS_ADD_SRC2_U64_si    : DS_Real_si<0xc0, DS_ADD_SRC2_U64>;
def DS_SUB_SRC2_U64_si    : DS_Real_si<0xc1, DS_SUB_SRC2_U64>;
def DS_RSUB_SRC2_U64_si   : DS_Real_si<0xc2, DS_RSUB_SRC2_U64>;
def DS_INC_SRC2_U64_si    : DS_Real_si<0xc3, DS_INC_SRC2_U64>;
def DS_DEC_SRC2_U64_si    : DS_Real_si<0xc4, DS_DEC_SRC2_U64>;
def DS_MIN_SRC2_I64_si    : DS_Real_si<0xc5, DS_MIN_SRC2_I64>;
def DS_MAX_SRC2_I64_si    : DS_Real_si<0xc6, DS_MAX_SRC2_I64>;
def DS_MIN_SRC2_U64_si    : DS_Real_si<0xc7, DS_MIN_SRC2_U64>;
def DS_MAX_SRC2_U64_si    : DS_Real_si<0xc8, DS_MAX_SRC2_U64>;
def DS_AND_SRC2_B64_si    : DS_Real_si<0xc9, DS_AND_SRC2_B64>;
def DS_OR_SRC2_B64_si     : DS_Real_si<0xca, DS_OR_SRC2_B64>;
def DS_XOR_SRC2_B64_si    : DS_Real_si<0xcb, DS_XOR_SRC2_B64>;
def DS_WRITE_SRC2_B64_si  : DS_Real_si<0xcd, DS_WRITE_SRC2_B64>;

def DS_MIN_SRC2_F64_si    : DS_Real_si<0xd2, DS_MIN_SRC2_F64>;
def DS_MAX_SRC2_F64_si    : DS_Real_si<0xd3, DS_MAX_SRC2_F64>;
def DS_WRITE_B96_si       : DS_Real_si<0xde, DS_WRITE_B96>;
def DS_WRITE_B128_si      : DS_Real_si<0xdf, DS_WRITE_B128>;
def DS_READ_B96_si        : DS_Real_si<0xfe, DS_READ_B96>;
def DS_READ_B128_si       : DS_Real_si<0xff, DS_READ_B128>;

//===----------------------------------------------------------------------===//
// VIInstructions.td
//===----------------------------------------------------------------------===//

class DS_Real_vi <bits<8> op, DS_Pseudo ds> :
  DS_Real <ds>,
  SIMCInstr <ds.Mnemonic, SIEncodingFamily.VI> {
  let AssemblerPredicates = [isVI];
  let DecoderNamespace="VI";

  // encoding
  let Inst{7-0}   = !if(ds.has_offset0, offset0, 0);
  let Inst{15-8}  = !if(ds.has_offset1, offset1, 0);
  let Inst{16}    = !if(ds.has_gds, gds, ds.gdsValue);
  let Inst{24-17} = op;
  let Inst{31-26} = 0x36; // ds prefix
  let Inst{39-32} = !if(ds.has_addr, addr, 0);
  let Inst{47-40} = !if(ds.has_data0, data0, 0);
  let Inst{55-48} = !if(ds.has_data1, data1, 0);
  let Inst{63-56} = !if(ds.has_vdst, vdst, 0);
}

def DS_ADD_U32_vi         : DS_Real_vi<0x0,  DS_ADD_U32>;
def DS_SUB_U32_vi         : DS_Real_vi<0x1,  DS_SUB_U32>;
def DS_RSUB_U32_vi        : DS_Real_vi<0x2,  DS_RSUB_U32>;
def DS_INC_U32_vi         : DS_Real_vi<0x3,  DS_INC_U32>;
def DS_DEC_U32_vi         : DS_Real_vi<0x4,  DS_DEC_U32>;
def DS_MIN_I32_vi         : DS_Real_vi<0x5,  DS_MIN_I32>;
def DS_MAX_I32_vi         : DS_Real_vi<0x6,  DS_MAX_I32>;
def DS_MIN_U32_vi         : DS_Real_vi<0x7,  DS_MIN_U32>;
def DS_MAX_U32_vi         : DS_Real_vi<0x8,  DS_MAX_U32>;
def DS_AND_B32_vi         : DS_Real_vi<0x9,  DS_AND_B32>;
def DS_OR_B32_vi          : DS_Real_vi<0xa,  DS_OR_B32>;
def DS_XOR_B32_vi         : DS_Real_vi<0xb,  DS_XOR_B32>;
def DS_MSKOR_B32_vi       : DS_Real_vi<0xc,  DS_MSKOR_B32>;
def DS_WRITE_B32_vi       : DS_Real_vi<0xd,  DS_WRITE_B32>;
def DS_WRITE2_B32_vi      : DS_Real_vi<0xe,  DS_WRITE2_B32>;
def DS_WRITE2ST64_B32_vi  : DS_Real_vi<0xf,  DS_WRITE2ST64_B32>;
def DS_CMPST_B32_vi       : DS_Real_vi<0x10, DS_CMPST_B32>;
def DS_CMPST_F32_vi       : DS_Real_vi<0x11, DS_CMPST_F32>;
def DS_MIN_F32_vi         : DS_Real_vi<0x12, DS_MIN_F32>;
def DS_MAX_F32_vi         : DS_Real_vi<0x13, DS_MAX_F32>;
def DS_NOP_vi             : DS_Real_vi<0x14, DS_NOP>;
def DS_ADD_F32_vi         : DS_Real_vi<0x15, DS_ADD_F32>;
def DS_GWS_INIT_vi        : DS_Real_vi<0x99, DS_GWS_INIT>;
def DS_GWS_SEMA_V_vi      : DS_Real_vi<0x9a, DS_GWS_SEMA_V>;
def DS_GWS_SEMA_BR_vi     : DS_Real_vi<0x9b, DS_GWS_SEMA_BR>;
def DS_GWS_SEMA_P_vi      : DS_Real_vi<0x9c, DS_GWS_SEMA_P>;
def DS_GWS_BARRIER_vi     : DS_Real_vi<0x9d, DS_GWS_BARRIER>;
def DS_WRITE_ADDTID_B32_vi : DS_Real_vi<0x1d, DS_WRITE_ADDTID_B32>;
def DS_WRITE_B8_vi        : DS_Real_vi<0x1e, DS_WRITE_B8>;
def DS_WRITE_B16_vi       : DS_Real_vi<0x1f, DS_WRITE_B16>;
def DS_ADD_RTN_U32_vi     : DS_Real_vi<0x20, DS_ADD_RTN_U32>;
def DS_SUB_RTN_U32_vi     : DS_Real_vi<0x21, DS_SUB_RTN_U32>;
def DS_RSUB_RTN_U32_vi    : DS_Real_vi<0x22, DS_RSUB_RTN_U32>;
def DS_INC_RTN_U32_vi     : DS_Real_vi<0x23, DS_INC_RTN_U32>;
def DS_DEC_RTN_U32_vi     : DS_Real_vi<0x24, DS_DEC_RTN_U32>;
def DS_MIN_RTN_I32_vi     : DS_Real_vi<0x25, DS_MIN_RTN_I32>;
def DS_MAX_RTN_I32_vi     : DS_Real_vi<0x26, DS_MAX_RTN_I32>;
def DS_MIN_RTN_U32_vi     : DS_Real_vi<0x27, DS_MIN_RTN_U32>;
def DS_MAX_RTN_U32_vi     : DS_Real_vi<0x28, DS_MAX_RTN_U32>;
def DS_AND_RTN_B32_vi     : DS_Real_vi<0x29, DS_AND_RTN_B32>;
def DS_OR_RTN_B32_vi      : DS_Real_vi<0x2a, DS_OR_RTN_B32>;
def DS_XOR_RTN_B32_vi     : DS_Real_vi<0x2b, DS_XOR_RTN_B32>;
def DS_MSKOR_RTN_B32_vi   : DS_Real_vi<0x2c, DS_MSKOR_RTN_B32>;
def DS_WRXCHG_RTN_B32_vi  : DS_Real_vi<0x2d, DS_WRXCHG_RTN_B32>;
def DS_WRXCHG2_RTN_B32_vi : DS_Real_vi<0x2e, DS_WRXCHG2_RTN_B32>;
def DS_WRXCHG2ST64_RTN_B32_vi : DS_Real_vi<0x2f, DS_WRXCHG2ST64_RTN_B32>;
def DS_CMPST_RTN_B32_vi   : DS_Real_vi<0x30, DS_CMPST_RTN_B32>;
def DS_CMPST_RTN_F32_vi   : DS_Real_vi<0x31, DS_CMPST_RTN_F32>;
def DS_MIN_RTN_F32_vi     : DS_Real_vi<0x32, DS_MIN_RTN_F32>;
def DS_MAX_RTN_F32_vi     : DS_Real_vi<0x33, DS_MAX_RTN_F32>;
def DS_WRAP_RTN_B32_vi    : DS_Real_vi<0x34, DS_WRAP_RTN_B32>;
def DS_ADD_RTN_F32_vi     : DS_Real_vi<0x35, DS_ADD_RTN_F32>;
def DS_READ_B32_vi        : DS_Real_vi<0x36, DS_READ_B32>;
def DS_READ2_B32_vi       : DS_Real_vi<0x37, DS_READ2_B32>;
def DS_READ2ST64_B32_vi   : DS_Real_vi<0x38, DS_READ2ST64_B32>;
def DS_READ_I8_vi         : DS_Real_vi<0x39, DS_READ_I8>;
def DS_READ_U8_vi         : DS_Real_vi<0x3a, DS_READ_U8>;
def DS_READ_I16_vi        : DS_Real_vi<0x3b, DS_READ_I16>;
def DS_READ_U16_vi        : DS_Real_vi<0x3c, DS_READ_U16>;
def DS_READ_ADDTID_B32_vi : DS_Real_vi<0xb6, DS_READ_ADDTID_B32>;
def DS_CONSUME_vi         : DS_Real_vi<0xbd, DS_CONSUME>;
def DS_APPEND_vi          : DS_Real_vi<0xbe, DS_APPEND>;
def DS_ORDERED_COUNT_vi   : DS_Real_vi<0xbf, DS_ORDERED_COUNT>;
def DS_SWIZZLE_B32_vi     : DS_Real_vi<0x3d, DS_SWIZZLE_B32>;
def DS_PERMUTE_B32_vi     : DS_Real_vi<0x3e, DS_PERMUTE_B32>;
def DS_BPERMUTE_B32_vi    : DS_Real_vi<0x3f, DS_BPERMUTE_B32>;

def DS_ADD_U64_vi         : DS_Real_vi<0x40, DS_ADD_U64>;
def DS_SUB_U64_vi         : DS_Real_vi<0x41, DS_SUB_U64>;
def DS_RSUB_U64_vi        : DS_Real_vi<0x42, DS_RSUB_U64>;
def DS_INC_U64_vi         : DS_Real_vi<0x43, DS_INC_U64>;
def DS_DEC_U64_vi         : DS_Real_vi<0x44, DS_DEC_U64>;
def DS_MIN_I64_vi         : DS_Real_vi<0x45, DS_MIN_I64>;
def DS_MAX_I64_vi         : DS_Real_vi<0x46, DS_MAX_I64>;
def DS_MIN_U64_vi         : DS_Real_vi<0x47, DS_MIN_U64>;
def DS_MAX_U64_vi         : DS_Real_vi<0x48, DS_MAX_U64>;
def DS_AND_B64_vi         : DS_Real_vi<0x49, DS_AND_B64>;
def DS_OR_B64_vi          : DS_Real_vi<0x4a, DS_OR_B64>;
def DS_XOR_B64_vi         : DS_Real_vi<0x4b, DS_XOR_B64>;
def DS_MSKOR_B64_vi       : DS_Real_vi<0x4c, DS_MSKOR_B64>;
def DS_WRITE_B64_vi       : DS_Real_vi<0x4d, DS_WRITE_B64>;
def DS_WRITE2_B64_vi      : DS_Real_vi<0x4E, DS_WRITE2_B64>;
def DS_WRITE2ST64_B64_vi  : DS_Real_vi<0x4f, DS_WRITE2ST64_B64>;
def DS_CMPST_B64_vi       : DS_Real_vi<0x50, DS_CMPST_B64>;
def DS_CMPST_F64_vi       : DS_Real_vi<0x51, DS_CMPST_F64>;
def DS_MIN_F64_vi         : DS_Real_vi<0x52, DS_MIN_F64>;
def DS_MAX_F64_vi         : DS_Real_vi<0x53, DS_MAX_F64>;

def DS_WRITE_B8_D16_HI_vi  : DS_Real_vi<0x54, DS_WRITE_B8_D16_HI>;
def DS_WRITE_B16_D16_HI_vi : DS_Real_vi<0x55, DS_WRITE_B16_D16_HI>;

def DS_READ_U8_D16_vi     : DS_Real_vi<0x56, DS_READ_U8_D16>;
def DS_READ_U8_D16_HI_vi  : DS_Real_vi<0x57, DS_READ_U8_D16_HI>;
def DS_READ_I8_D16_vi     : DS_Real_vi<0x58, DS_READ_I8_D16>;
def DS_READ_I8_D16_HI_vi  : DS_Real_vi<0x59, DS_READ_I8_D16_HI>;
def DS_READ_U16_D16_vi    : DS_Real_vi<0x5a, DS_READ_U16_D16>;
def DS_READ_U16_D16_HI_vi : DS_Real_vi<0x5b, DS_READ_U16_D16_HI>;

def DS_ADD_RTN_U64_vi     : DS_Real_vi<0x60, DS_ADD_RTN_U64>;
def DS_SUB_RTN_U64_vi     : DS_Real_vi<0x61, DS_SUB_RTN_U64>;
def DS_RSUB_RTN_U64_vi    : DS_Real_vi<0x62, DS_RSUB_RTN_U64>;
def DS_INC_RTN_U64_vi     : DS_Real_vi<0x63, DS_INC_RTN_U64>;
def DS_DEC_RTN_U64_vi     : DS_Real_vi<0x64, DS_DEC_RTN_U64>;
def DS_MIN_RTN_I64_vi     : DS_Real_vi<0x65, DS_MIN_RTN_I64>;
def DS_MAX_RTN_I64_vi     : DS_Real_vi<0x66, DS_MAX_RTN_I64>;
def DS_MIN_RTN_U64_vi     : DS_Real_vi<0x67, DS_MIN_RTN_U64>;
def DS_MAX_RTN_U64_vi     : DS_Real_vi<0x68, DS_MAX_RTN_U64>;
def DS_AND_RTN_B64_vi     : DS_Real_vi<0x69, DS_AND_RTN_B64>;
def DS_OR_RTN_B64_vi      : DS_Real_vi<0x6a, DS_OR_RTN_B64>;
def DS_XOR_RTN_B64_vi     : DS_Real_vi<0x6b, DS_XOR_RTN_B64>;
def DS_MSKOR_RTN_B64_vi   : DS_Real_vi<0x6c, DS_MSKOR_RTN_B64>;
def DS_WRXCHG_RTN_B64_vi  : DS_Real_vi<0x6d, DS_WRXCHG_RTN_B64>;
def DS_WRXCHG2_RTN_B64_vi : DS_Real_vi<0x6e, DS_WRXCHG2_RTN_B64>;
def DS_WRXCHG2ST64_RTN_B64_vi : DS_Real_vi<0x6f, DS_WRXCHG2ST64_RTN_B64>;
def DS_CONDXCHG32_RTN_B64_vi   : DS_Real_vi<0x7e, DS_CONDXCHG32_RTN_B64>;
def DS_GWS_SEMA_RELEASE_ALL_vi : DS_Real_vi<0x98, DS_GWS_SEMA_RELEASE_ALL>;
def DS_CMPST_RTN_B64_vi   : DS_Real_vi<0x70, DS_CMPST_RTN_B64>;
def DS_CMPST_RTN_F64_vi   : DS_Real_vi<0x71, DS_CMPST_RTN_F64>;
def DS_MIN_RTN_F64_vi     : DS_Real_vi<0x72, DS_MIN_RTN_F64>;
def DS_MAX_RTN_F64_vi     : DS_Real_vi<0x73, DS_MAX_RTN_F64>;

def DS_READ_B64_vi        : DS_Real_vi<0x76, DS_READ_B64>;
def DS_READ2_B64_vi       : DS_Real_vi<0x77, DS_READ2_B64>;
def DS_READ2ST64_B64_vi   : DS_Real_vi<0x78, DS_READ2ST64_B64>;

def DS_ADD_SRC2_U32_vi    : DS_Real_vi<0x80, DS_ADD_SRC2_U32>;
def DS_SUB_SRC2_U32_vi    : DS_Real_vi<0x81, DS_SUB_SRC2_U32>;
def DS_RSUB_SRC2_U32_vi   : DS_Real_vi<0x82, DS_RSUB_SRC2_U32>;
def DS_INC_SRC2_U32_vi    : DS_Real_vi<0x83, DS_INC_SRC2_U32>;
def DS_DEC_SRC2_U32_vi    : DS_Real_vi<0x84, DS_DEC_SRC2_U32>;
def DS_MIN_SRC2_I32_vi    : DS_Real_vi<0x85, DS_MIN_SRC2_I32>;
def DS_MAX_SRC2_I32_vi    : DS_Real_vi<0x86, DS_MAX_SRC2_I32>;
def DS_MIN_SRC2_U32_vi    : DS_Real_vi<0x87, DS_MIN_SRC2_U32>;
def DS_MAX_SRC2_U32_vi    : DS_Real_vi<0x88, DS_MAX_SRC2_U32>;
def DS_AND_SRC2_B32_vi    : DS_Real_vi<0x89, DS_AND_SRC2_B32>;
def DS_OR_SRC2_B32_vi     : DS_Real_vi<0x8a, DS_OR_SRC2_B32>;
def DS_XOR_SRC2_B32_vi    : DS_Real_vi<0x8b, DS_XOR_SRC2_B32>;
def DS_WRITE_SRC2_B32_vi  : DS_Real_vi<0x8d, DS_WRITE_SRC2_B32>;
def DS_MIN_SRC2_F32_vi    : DS_Real_vi<0x92, DS_MIN_SRC2_F32>;
def DS_MAX_SRC2_F32_vi    : DS_Real_vi<0x93, DS_MAX_SRC2_F32>;
def DS_ADD_SRC2_F32_vi    : DS_Real_vi<0x95, DS_ADD_SRC2_F32>;
def DS_ADD_SRC2_U64_vi    : DS_Real_vi<0xc0, DS_ADD_SRC2_U64>;
def DS_SUB_SRC2_U64_vi    : DS_Real_vi<0xc1, DS_SUB_SRC2_U64>;
def DS_RSUB_SRC2_U64_vi   : DS_Real_vi<0xc2, DS_RSUB_SRC2_U64>;
def DS_INC_SRC2_U64_vi    : DS_Real_vi<0xc3, DS_INC_SRC2_U64>;
def DS_DEC_SRC2_U64_vi    : DS_Real_vi<0xc4, DS_DEC_SRC2_U64>;
def DS_MIN_SRC2_I64_vi    : DS_Real_vi<0xc5, DS_MIN_SRC2_I64>;
def DS_MAX_SRC2_I64_vi    : DS_Real_vi<0xc6, DS_MAX_SRC2_I64>;
def DS_MIN_SRC2_U64_vi    : DS_Real_vi<0xc7, DS_MIN_SRC2_U64>;
def DS_MAX_SRC2_U64_vi    : DS_Real_vi<0xc8, DS_MAX_SRC2_U64>;
def DS_AND_SRC2_B64_vi    : DS_Real_vi<0xc9, DS_AND_SRC2_B64>;
def DS_OR_SRC2_B64_vi     : DS_Real_vi<0xca, DS_OR_SRC2_B64>;
def DS_XOR_SRC2_B64_vi    : DS_Real_vi<0xcb, DS_XOR_SRC2_B64>;
def DS_WRITE_SRC2_B64_vi  : DS_Real_vi<0xcd, DS_WRITE_SRC2_B64>;
def DS_MIN_SRC2_F64_vi    : DS_Real_vi<0xd2, DS_MIN_SRC2_F64>;
def DS_MAX_SRC2_F64_vi    : DS_Real_vi<0xd3, DS_MAX_SRC2_F64>;
def DS_WRITE_B96_vi       : DS_Real_vi<0xde, DS_WRITE_B96>;
def DS_WRITE_B128_vi      : DS_Real_vi<0xdf, DS_WRITE_B128>;
def DS_READ_B96_vi        : DS_Real_vi<0xfe, DS_READ_B96>;
def DS_READ_B128_vi       : DS_Real_vi<0xff, DS_READ_B128>;
