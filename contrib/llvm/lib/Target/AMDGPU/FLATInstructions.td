//===-- FLATInstructions.td - FLAT Instruction Defintions -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def FLATAtomic : ComplexPattern<i64, 3, "SelectFlatAtomic", [], [], -10>;
def FLATOffset : ComplexPattern<i64, 3, "SelectFlatOffset<false>", [], [], -10>;

def FLATOffsetSigned : ComplexPattern<i64, 3, "SelectFlatOffset<true>", [], [], -10>;
def FLATSignedAtomic : ComplexPattern<i64, 3, "SelectFlatAtomicSigned", [], [], -10>;

//===----------------------------------------------------------------------===//
// FLAT classes
//===----------------------------------------------------------------------===//

class FLAT_Pseudo<string opName, dag outs, dag ins,
                  string asmOps, list<dag> pattern=[]> :
  InstSI<outs, ins, "", pattern>,
  SIMCInstr<opName, SIEncodingFamily.NONE> {

  let isPseudo = 1;
  let isCodeGenOnly = 1;

  let FLAT = 1;

  let UseNamedOperandTable = 1;
  let hasSideEffects = 0;
  let SchedRW = [WriteVMEM];

  string Mnemonic = opName;
  string AsmOperands = asmOps;

  bits<1> is_flat_global = 0;
  bits<1> is_flat_scratch = 0;

  bits<1> has_vdst = 1;

  // We need to distinguish having saddr and enabling saddr because
  // saddr is only valid for scratch and global instructions. Pre-gfx9
  // these bits were reserved, so we also don't necessarily want to
  // set these bits to the disabled value for the original flat
  // segment instructions.
  bits<1> has_saddr = 0;
  bits<1> enabled_saddr = 0;
  bits<7> saddr_value = 0;
  bits<1> has_vaddr = 1;

  bits<1> has_data = 1;
  bits<1> has_glc  = 1;
  bits<1> glcValue = 0;

  let SubtargetPredicate = !if(is_flat_global, HasFlatGlobalInsts,
    !if(is_flat_scratch, HasFlatScratchInsts, HasFlatAddressSpace));

  // TODO: M0 if it could possibly access LDS (before gfx9? only)?
  let Uses = !if(is_flat_global, [EXEC], [EXEC, FLAT_SCR]);

  // Internally, FLAT instruction are executed as both an LDS and a
  // Buffer instruction; so, they increment both VM_CNT and LGKM_CNT
  // and are not considered done until both have been decremented.
  let VM_CNT = 1;
  let LGKM_CNT = !if(!or(is_flat_global, is_flat_scratch), 0, 1);
}

class FLAT_Real <bits<7> op, FLAT_Pseudo ps> :
  InstSI <ps.OutOperandList, ps.InOperandList, ps.Mnemonic # ps.AsmOperands, []>,
  Enc64 {

  let isPseudo = 0;
  let isCodeGenOnly = 0;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;
  let TSFlags = ps.TSFlags;
  let UseNamedOperandTable = ps.UseNamedOperandTable;

  // encoding fields
  bits<8> vaddr;
  bits<8> vdata;
  bits<7> saddr;
  bits<8> vdst;

  bits<1> slc;
  bits<1> glc;

  // Only valid on gfx9
  bits<1> lds = 0; // XXX - What does this actually do?

  // Segment, 00=flat, 01=scratch, 10=global, 11=reserved
  bits<2> seg = !if(ps.is_flat_global, 0b10,
                  !if(ps.is_flat_scratch, 0b01, 0));

  // Signed offset. Highest bit ignored for flat and treated as 12-bit
  // unsigned for flat acceses.
  bits<13> offset;
  bits<1> nv = 0; // XXX - What does this actually do?

  // We don't use tfe right now, and it was removed in gfx9.
  bits<1> tfe = 0;

  // Only valid on GFX9+
  let Inst{12-0} = offset;
  let Inst{13} = lds;
  let Inst{15-14} = seg;

  let Inst{16}    = !if(ps.has_glc, glc, ps.glcValue);
  let Inst{17}    = slc;
  let Inst{24-18} = op;
  let Inst{31-26} = 0x37; // Encoding.
  let Inst{39-32} = !if(ps.has_vaddr, vaddr, ?);
  let Inst{47-40} = !if(ps.has_data, vdata, ?);
  let Inst{54-48} = !if(ps.has_saddr, !if(ps.enabled_saddr, saddr, 0x7f), 0);

  // 54-48 is reserved.
  let Inst{55}    = nv; // nv on GFX9+, TFE before.
  let Inst{63-56} = !if(ps.has_vdst, vdst, ?);
}

class GlobalSaddrTable <bit is_saddr, string Name = ""> {
  bit IsSaddr = is_saddr;
  string SaddrOp = Name;
}

// TODO: Is exec allowed for saddr? The disabled value 0x7f is the
// same encoding value as exec_hi, so it isn't possible to use that if
// saddr is 32-bit (which isn't handled here yet).
class FLAT_Load_Pseudo <string opName, RegisterClass regClass,
  bit HasTiedOutput = 0,
  bit HasSignedOffset = 0, bit HasSaddr = 0, bit EnableSaddr = 0> : FLAT_Pseudo<
  opName,
  (outs regClass:$vdst),
  !con(
    !con(
      !con(
        !con((ins VReg_64:$vaddr),
          !if(EnableSaddr, (ins SReg_64:$saddr), (ins))),
            (ins !if(HasSignedOffset,offset_s13,offset_u12):$offset)),
            (ins GLC:$glc, SLC:$slc)),
            !if(HasTiedOutput, (ins regClass:$vdst_in), (ins))),
  " $vdst, $vaddr"#!if(HasSaddr, !if(EnableSaddr, ", $saddr", ", off"), "")#"$offset$glc$slc"> {
  let has_data = 0;
  let mayLoad = 1;
  let has_saddr = HasSaddr;
  let enabled_saddr = EnableSaddr;
  let PseudoInstr = opName#!if(!and(HasSaddr, EnableSaddr), "_SADDR", "");
  let maybeAtomic = 1;

  let Constraints = !if(HasTiedOutput, "$vdst = $vdst_in", "");
  let DisableEncoding = !if(HasTiedOutput, "$vdst_in", "");
}

class FLAT_Store_Pseudo <string opName, RegisterClass vdataClass,
  bit HasSignedOffset = 0, bit HasSaddr = 0, bit EnableSaddr = 0> : FLAT_Pseudo<
  opName,
  (outs),
  !con(
    !con(
      !con((ins VReg_64:$vaddr, vdataClass:$vdata),
        !if(EnableSaddr, (ins SReg_64:$saddr), (ins))),
          (ins !if(HasSignedOffset,offset_s13,offset_u12):$offset)),
          (ins GLC:$glc, SLC:$slc)),
  " $vaddr, $vdata"#!if(HasSaddr, !if(EnableSaddr, ", $saddr", ", off"), "")#"$offset$glc$slc"> {
  let mayLoad  = 0;
  let mayStore = 1;
  let has_vdst = 0;
  let has_saddr = HasSaddr;
  let enabled_saddr = EnableSaddr;
  let PseudoInstr = opName#!if(!and(HasSaddr, EnableSaddr), "_SADDR", "");
  let maybeAtomic = 1;
}

multiclass FLAT_Global_Load_Pseudo<string opName, RegisterClass regClass, bit HasTiedInput = 0> {
  let is_flat_global = 1 in {
    def "" : FLAT_Load_Pseudo<opName, regClass, HasTiedInput, 1, 1>,
      GlobalSaddrTable<0, opName>;
    def _SADDR : FLAT_Load_Pseudo<opName, regClass, HasTiedInput, 1, 1, 1>,
      GlobalSaddrTable<1, opName>;
  }
}

multiclass FLAT_Global_Store_Pseudo<string opName, RegisterClass regClass> {
  let is_flat_global = 1 in {
    def "" : FLAT_Store_Pseudo<opName, regClass, 1, 1>,
      GlobalSaddrTable<0, opName>;
    def _SADDR : FLAT_Store_Pseudo<opName, regClass, 1, 1, 1>,
      GlobalSaddrTable<1, opName>;
  }
}

class FLAT_Scratch_Load_Pseudo <string opName, RegisterClass regClass,
  bit EnableSaddr = 0>: FLAT_Pseudo<
  opName,
  (outs regClass:$vdst),
  !if(EnableSaddr,
      (ins SReg_32_XEXEC_HI:$saddr, offset_s13:$offset, GLC:$glc, SLC:$slc),
      (ins VGPR_32:$vaddr, offset_s13:$offset, GLC:$glc, SLC:$slc)),
  " $vdst, "#!if(EnableSaddr, "off", "$vaddr")#!if(EnableSaddr, ", $saddr", ", off")#"$offset$glc$slc"> {
  let has_data = 0;
  let mayLoad = 1;
  let has_saddr = 1;
  let enabled_saddr = EnableSaddr;
  let has_vaddr = !if(EnableSaddr, 0, 1);
  let PseudoInstr = opName#!if(EnableSaddr, "_SADDR", "");
  let maybeAtomic = 1;
}

class FLAT_Scratch_Store_Pseudo <string opName, RegisterClass vdataClass, bit EnableSaddr = 0> : FLAT_Pseudo<
  opName,
  (outs),
  !if(EnableSaddr,
    (ins vdataClass:$vdata, SReg_32_XEXEC_HI:$saddr, offset_s13:$offset, GLC:$glc, SLC:$slc),
    (ins vdataClass:$vdata, VGPR_32:$vaddr, offset_s13:$offset, GLC:$glc, SLC:$slc)),
  " "#!if(EnableSaddr, "off", "$vaddr")#", $vdata, "#!if(EnableSaddr, "$saddr", "off")#"$offset$glc$slc"> {
  let mayLoad  = 0;
  let mayStore = 1;
  let has_vdst = 0;
  let has_saddr = 1;
  let enabled_saddr = EnableSaddr;
  let has_vaddr = !if(EnableSaddr, 0, 1);
  let PseudoInstr = opName#!if(EnableSaddr, "_SADDR", "");
  let maybeAtomic = 1;
}

multiclass FLAT_Scratch_Load_Pseudo<string opName, RegisterClass regClass> {
  let is_flat_scratch = 1 in {
    def "" : FLAT_Scratch_Load_Pseudo<opName, regClass>;
    def _SADDR : FLAT_Scratch_Load_Pseudo<opName, regClass, 1>;
  }
}

multiclass FLAT_Scratch_Store_Pseudo<string opName, RegisterClass regClass> {
  let is_flat_scratch = 1 in {
    def "" : FLAT_Scratch_Store_Pseudo<opName, regClass>;
    def _SADDR : FLAT_Scratch_Store_Pseudo<opName, regClass, 1>;
  }
}

class FLAT_AtomicNoRet_Pseudo<string opName, dag outs, dag ins,
                               string asm, list<dag> pattern = []> :
  FLAT_Pseudo<opName, outs, ins, asm, pattern> {
    let mayLoad = 1;
    let mayStore = 1;
    let has_glc  = 0;
    let glcValue = 0;
    let has_vdst = 0;
    let maybeAtomic = 1;
}

class FLAT_AtomicRet_Pseudo<string opName, dag outs, dag ins,
                            string asm, list<dag> pattern = []>
  : FLAT_AtomicNoRet_Pseudo<opName, outs, ins, asm, pattern> {
  let hasPostISelHook = 1;
  let has_vdst = 1;
  let glcValue = 1;
  let PseudoInstr = NAME # "_RTN";
}

multiclass FLAT_Atomic_Pseudo<
  string opName,
  RegisterClass vdst_rc,
  ValueType vt,
  SDPatternOperator atomic = null_frag,
  ValueType data_vt = vt,
  RegisterClass data_rc = vdst_rc> {
  def "" : FLAT_AtomicNoRet_Pseudo <opName,
    (outs),
    (ins VReg_64:$vaddr, data_rc:$vdata, offset_u12:$offset, SLC:$slc),
    " $vaddr, $vdata$offset$slc">,
    GlobalSaddrTable<0, opName>,
    AtomicNoRet <opName, 0> {
    let PseudoInstr = NAME;
  }

  def _RTN : FLAT_AtomicRet_Pseudo <opName,
    (outs vdst_rc:$vdst),
    (ins VReg_64:$vaddr, data_rc:$vdata, offset_u12:$offset, SLC:$slc),
    " $vdst, $vaddr, $vdata$offset glc$slc",
    [(set vt:$vdst,
      (atomic (FLATAtomic i64:$vaddr, i16:$offset, i1:$slc), data_vt:$vdata))]>,
       GlobalSaddrTable<0, opName#"_rtn">,
       AtomicNoRet <opName, 1>;
}

multiclass FLAT_Global_Atomic_Pseudo_NO_RTN<
  string opName,
  RegisterClass vdst_rc,
  ValueType vt,
  SDPatternOperator atomic = null_frag,
  ValueType data_vt = vt,
  RegisterClass data_rc = vdst_rc> {

  def "" : FLAT_AtomicNoRet_Pseudo <opName,
    (outs),
    (ins VReg_64:$vaddr, data_rc:$vdata, offset_s13:$offset, SLC:$slc),
    " $vaddr, $vdata, off$offset$slc">,
    GlobalSaddrTable<0, opName>,
    AtomicNoRet <opName, 0> {
    let has_saddr = 1;
    let PseudoInstr = NAME;
  }

  def _SADDR : FLAT_AtomicNoRet_Pseudo <opName,
    (outs),
    (ins VReg_64:$vaddr, data_rc:$vdata, SReg_64:$saddr, offset_s13:$offset, SLC:$slc),
    " $vaddr, $vdata, $saddr$offset$slc">,
    GlobalSaddrTable<1, opName>,
    AtomicNoRet <opName#"_saddr", 0> {
    let has_saddr = 1;
    let enabled_saddr = 1;
    let PseudoInstr = NAME#"_SADDR";
  }
}

multiclass FLAT_Global_Atomic_Pseudo_RTN<
  string opName,
  RegisterClass vdst_rc,
  ValueType vt,
  SDPatternOperator atomic = null_frag,
  ValueType data_vt = vt,
  RegisterClass data_rc = vdst_rc> {

  def _RTN : FLAT_AtomicRet_Pseudo <opName,
    (outs vdst_rc:$vdst),
      (ins VReg_64:$vaddr, data_rc:$vdata, offset_s13:$offset, SLC:$slc),
    " $vdst, $vaddr, $vdata, off$offset glc$slc",
    [(set vt:$vdst,
      (atomic (FLATSignedAtomic i64:$vaddr, i16:$offset, i1:$slc), data_vt:$vdata))]>,
      GlobalSaddrTable<0, opName#"_rtn">,
      AtomicNoRet <opName, 1> {
    let has_saddr = 1;
  }

  def _SADDR_RTN : FLAT_AtomicRet_Pseudo <opName,
    (outs vdst_rc:$vdst),
      (ins VReg_64:$vaddr, data_rc:$vdata, SReg_64:$saddr, offset_s13:$offset, SLC:$slc),
    " $vdst, $vaddr, $vdata, $saddr$offset glc$slc">,
    GlobalSaddrTable<1, opName#"_rtn">,
    AtomicNoRet <opName#"_saddr", 1> {
     let has_saddr = 1;
     let enabled_saddr = 1;
     let PseudoInstr = NAME#"_SADDR_RTN";
  }
}

multiclass FLAT_Global_Atomic_Pseudo<
  string opName,
  RegisterClass vdst_rc,
  ValueType vt,
  SDPatternOperator atomic = null_frag,
  ValueType data_vt = vt,
  RegisterClass data_rc = vdst_rc> :
    FLAT_Global_Atomic_Pseudo_NO_RTN<opName, vdst_rc, vt, atomic, data_vt, data_rc>,
    FLAT_Global_Atomic_Pseudo_RTN<opName, vdst_rc, vt, atomic, data_vt, data_rc>;

class flat_binary_atomic_op<SDNode atomic_op> : PatFrag<
  (ops node:$ptr, node:$value),
  (atomic_op node:$ptr, node:$value),
  [{return cast<MemSDNode>(N)->getAddressSpace() == AMDGPUAS::FLAT_ADDRESS;}]
>;

def atomic_cmp_swap_flat : flat_binary_atomic_op<AMDGPUatomic_cmp_swap>;
def atomic_swap_flat     : flat_binary_atomic_op<atomic_swap>;
def atomic_add_flat      : flat_binary_atomic_op<atomic_load_add>;
def atomic_and_flat      : flat_binary_atomic_op<atomic_load_and>;
def atomic_max_flat      : flat_binary_atomic_op<atomic_load_max>;
def atomic_min_flat      : flat_binary_atomic_op<atomic_load_min>;
def atomic_or_flat       : flat_binary_atomic_op<atomic_load_or>;
def atomic_sub_flat      : flat_binary_atomic_op<atomic_load_sub>;
def atomic_umax_flat     : flat_binary_atomic_op<atomic_load_umax>;
def atomic_umin_flat     : flat_binary_atomic_op<atomic_load_umin>;
def atomic_xor_flat      : flat_binary_atomic_op<atomic_load_xor>;
def atomic_inc_flat      : flat_binary_atomic_op<SIatomic_inc>;
def atomic_dec_flat      : flat_binary_atomic_op<SIatomic_dec>;



//===----------------------------------------------------------------------===//
// Flat Instructions
//===----------------------------------------------------------------------===//

def FLAT_LOAD_UBYTE    : FLAT_Load_Pseudo <"flat_load_ubyte", VGPR_32>;
def FLAT_LOAD_SBYTE    : FLAT_Load_Pseudo <"flat_load_sbyte", VGPR_32>;
def FLAT_LOAD_USHORT   : FLAT_Load_Pseudo <"flat_load_ushort", VGPR_32>;
def FLAT_LOAD_SSHORT   : FLAT_Load_Pseudo <"flat_load_sshort", VGPR_32>;
def FLAT_LOAD_DWORD    : FLAT_Load_Pseudo <"flat_load_dword", VGPR_32>;
def FLAT_LOAD_DWORDX2  : FLAT_Load_Pseudo <"flat_load_dwordx2", VReg_64>;
def FLAT_LOAD_DWORDX4  : FLAT_Load_Pseudo <"flat_load_dwordx4", VReg_128>;
def FLAT_LOAD_DWORDX3  : FLAT_Load_Pseudo <"flat_load_dwordx3", VReg_96>;

def FLAT_STORE_BYTE    : FLAT_Store_Pseudo <"flat_store_byte", VGPR_32>;
def FLAT_STORE_SHORT   : FLAT_Store_Pseudo <"flat_store_short", VGPR_32>;
def FLAT_STORE_DWORD   : FLAT_Store_Pseudo <"flat_store_dword", VGPR_32>;
def FLAT_STORE_DWORDX2 : FLAT_Store_Pseudo <"flat_store_dwordx2", VReg_64>;
def FLAT_STORE_DWORDX4 : FLAT_Store_Pseudo <"flat_store_dwordx4", VReg_128>;
def FLAT_STORE_DWORDX3 : FLAT_Store_Pseudo <"flat_store_dwordx3", VReg_96>;

let SubtargetPredicate = HasD16LoadStore in {
def FLAT_LOAD_UBYTE_D16     : FLAT_Load_Pseudo <"flat_load_ubyte_d16", VGPR_32, 1>;
def FLAT_LOAD_UBYTE_D16_HI  : FLAT_Load_Pseudo <"flat_load_ubyte_d16_hi", VGPR_32, 1>;
def FLAT_LOAD_SBYTE_D16     : FLAT_Load_Pseudo <"flat_load_sbyte_d16", VGPR_32, 1>;
def FLAT_LOAD_SBYTE_D16_HI  : FLAT_Load_Pseudo <"flat_load_sbyte_d16_hi", VGPR_32, 1>;
def FLAT_LOAD_SHORT_D16     : FLAT_Load_Pseudo <"flat_load_short_d16", VGPR_32, 1>;
def FLAT_LOAD_SHORT_D16_HI  : FLAT_Load_Pseudo <"flat_load_short_d16_hi", VGPR_32, 1>;

def FLAT_STORE_BYTE_D16_HI  : FLAT_Store_Pseudo <"flat_store_byte_d16_hi", VGPR_32>;
def FLAT_STORE_SHORT_D16_HI : FLAT_Store_Pseudo <"flat_store_short_d16_hi", VGPR_32>;
}

defm FLAT_ATOMIC_CMPSWAP    : FLAT_Atomic_Pseudo <"flat_atomic_cmpswap",
                                VGPR_32, i32, atomic_cmp_swap_flat,
                                v2i32, VReg_64>;

defm FLAT_ATOMIC_CMPSWAP_X2 : FLAT_Atomic_Pseudo <"flat_atomic_cmpswap_x2",
                                VReg_64, i64, atomic_cmp_swap_flat,
                                v2i64, VReg_128>;

defm FLAT_ATOMIC_SWAP       : FLAT_Atomic_Pseudo <"flat_atomic_swap",
                                VGPR_32, i32, atomic_swap_flat>;

defm FLAT_ATOMIC_SWAP_X2    : FLAT_Atomic_Pseudo <"flat_atomic_swap_x2",
                                VReg_64, i64, atomic_swap_flat>;

defm FLAT_ATOMIC_ADD        : FLAT_Atomic_Pseudo <"flat_atomic_add",
                                VGPR_32, i32, atomic_add_flat>;

defm FLAT_ATOMIC_SUB        : FLAT_Atomic_Pseudo <"flat_atomic_sub",
                                VGPR_32, i32, atomic_sub_flat>;

defm FLAT_ATOMIC_SMIN       : FLAT_Atomic_Pseudo <"flat_atomic_smin",
                                VGPR_32, i32, atomic_min_flat>;

defm FLAT_ATOMIC_UMIN       : FLAT_Atomic_Pseudo <"flat_atomic_umin",
                                VGPR_32, i32, atomic_umin_flat>;

defm FLAT_ATOMIC_SMAX       : FLAT_Atomic_Pseudo <"flat_atomic_smax",
                                VGPR_32, i32, atomic_max_flat>;

defm FLAT_ATOMIC_UMAX       : FLAT_Atomic_Pseudo <"flat_atomic_umax",
                                VGPR_32, i32, atomic_umax_flat>;

defm FLAT_ATOMIC_AND        : FLAT_Atomic_Pseudo <"flat_atomic_and",
                                VGPR_32, i32, atomic_and_flat>;

defm FLAT_ATOMIC_OR         : FLAT_Atomic_Pseudo <"flat_atomic_or",
                                VGPR_32, i32, atomic_or_flat>;

defm FLAT_ATOMIC_XOR        : FLAT_Atomic_Pseudo <"flat_atomic_xor",
                                VGPR_32, i32, atomic_xor_flat>;

defm FLAT_ATOMIC_INC        : FLAT_Atomic_Pseudo <"flat_atomic_inc",
                                VGPR_32, i32, atomic_inc_flat>;

defm FLAT_ATOMIC_DEC        : FLAT_Atomic_Pseudo <"flat_atomic_dec",
                                VGPR_32, i32, atomic_dec_flat>;

defm FLAT_ATOMIC_ADD_X2     : FLAT_Atomic_Pseudo <"flat_atomic_add_x2",
                                VReg_64, i64, atomic_add_flat>;

defm FLAT_ATOMIC_SUB_X2     : FLAT_Atomic_Pseudo <"flat_atomic_sub_x2",
                                VReg_64, i64, atomic_sub_flat>;

defm FLAT_ATOMIC_SMIN_X2    : FLAT_Atomic_Pseudo <"flat_atomic_smin_x2",
                                VReg_64, i64, atomic_min_flat>;

defm FLAT_ATOMIC_UMIN_X2    : FLAT_Atomic_Pseudo <"flat_atomic_umin_x2",
                                VReg_64, i64, atomic_umin_flat>;

defm FLAT_ATOMIC_SMAX_X2    : FLAT_Atomic_Pseudo <"flat_atomic_smax_x2",
                                VReg_64, i64, atomic_max_flat>;

defm FLAT_ATOMIC_UMAX_X2    : FLAT_Atomic_Pseudo <"flat_atomic_umax_x2",
                                VReg_64, i64, atomic_umax_flat>;

defm FLAT_ATOMIC_AND_X2     : FLAT_Atomic_Pseudo <"flat_atomic_and_x2",
                                VReg_64, i64, atomic_and_flat>;

defm FLAT_ATOMIC_OR_X2      : FLAT_Atomic_Pseudo <"flat_atomic_or_x2",
                                VReg_64, i64, atomic_or_flat>;

defm FLAT_ATOMIC_XOR_X2     : FLAT_Atomic_Pseudo <"flat_atomic_xor_x2",
                                VReg_64, i64, atomic_xor_flat>;

defm FLAT_ATOMIC_INC_X2     : FLAT_Atomic_Pseudo <"flat_atomic_inc_x2",
                                VReg_64, i64, atomic_inc_flat>;

defm FLAT_ATOMIC_DEC_X2     : FLAT_Atomic_Pseudo <"flat_atomic_dec_x2",
                                VReg_64, i64, atomic_dec_flat>;

let SubtargetPredicate = isCI in { // CI Only flat instructions : FIXME Only?

defm FLAT_ATOMIC_FCMPSWAP    : FLAT_Atomic_Pseudo <"flat_atomic_fcmpswap",
                                VGPR_32, f32, null_frag, v2f32, VReg_64>;

defm FLAT_ATOMIC_FCMPSWAP_X2 : FLAT_Atomic_Pseudo <"flat_atomic_fcmpswap_x2",
                                VReg_64, f64, null_frag, v2f64, VReg_128>;

defm FLAT_ATOMIC_FMIN        : FLAT_Atomic_Pseudo <"flat_atomic_fmin",
                                VGPR_32, f32>;

defm FLAT_ATOMIC_FMAX        : FLAT_Atomic_Pseudo <"flat_atomic_fmax",
                                VGPR_32, f32>;

defm FLAT_ATOMIC_FMIN_X2     : FLAT_Atomic_Pseudo <"flat_atomic_fmin_x2",
                                VReg_64, f64>;

defm FLAT_ATOMIC_FMAX_X2     : FLAT_Atomic_Pseudo <"flat_atomic_fmax_x2",
                                VReg_64, f64>;

} // End SubtargetPredicate = isCI

let SubtargetPredicate = HasFlatGlobalInsts in {
defm GLOBAL_LOAD_UBYTE    : FLAT_Global_Load_Pseudo <"global_load_ubyte", VGPR_32>;
defm GLOBAL_LOAD_SBYTE    : FLAT_Global_Load_Pseudo <"global_load_sbyte", VGPR_32>;
defm GLOBAL_LOAD_USHORT   : FLAT_Global_Load_Pseudo <"global_load_ushort", VGPR_32>;
defm GLOBAL_LOAD_SSHORT   : FLAT_Global_Load_Pseudo <"global_load_sshort", VGPR_32>;
defm GLOBAL_LOAD_DWORD    : FLAT_Global_Load_Pseudo <"global_load_dword", VGPR_32>;
defm GLOBAL_LOAD_DWORDX2  : FLAT_Global_Load_Pseudo <"global_load_dwordx2", VReg_64>;
defm GLOBAL_LOAD_DWORDX3  : FLAT_Global_Load_Pseudo <"global_load_dwordx3", VReg_96>;
defm GLOBAL_LOAD_DWORDX4  : FLAT_Global_Load_Pseudo <"global_load_dwordx4", VReg_128>;

defm GLOBAL_LOAD_UBYTE_D16    : FLAT_Global_Load_Pseudo <"global_load_ubyte_d16", VGPR_32, 1>;
defm GLOBAL_LOAD_UBYTE_D16_HI : FLAT_Global_Load_Pseudo <"global_load_ubyte_d16_hi", VGPR_32, 1>;
defm GLOBAL_LOAD_SBYTE_D16    : FLAT_Global_Load_Pseudo <"global_load_sbyte_d16", VGPR_32, 1>;
defm GLOBAL_LOAD_SBYTE_D16_HI : FLAT_Global_Load_Pseudo <"global_load_sbyte_d16_hi", VGPR_32, 1>;
defm GLOBAL_LOAD_SHORT_D16    : FLAT_Global_Load_Pseudo <"global_load_short_d16", VGPR_32, 1>;
defm GLOBAL_LOAD_SHORT_D16_HI : FLAT_Global_Load_Pseudo <"global_load_short_d16_hi", VGPR_32, 1>;

defm GLOBAL_STORE_BYTE    : FLAT_Global_Store_Pseudo <"global_store_byte", VGPR_32>;
defm GLOBAL_STORE_SHORT   : FLAT_Global_Store_Pseudo <"global_store_short", VGPR_32>;
defm GLOBAL_STORE_DWORD   : FLAT_Global_Store_Pseudo <"global_store_dword", VGPR_32>;
defm GLOBAL_STORE_DWORDX2 : FLAT_Global_Store_Pseudo <"global_store_dwordx2", VReg_64>;
defm GLOBAL_STORE_DWORDX3 : FLAT_Global_Store_Pseudo <"global_store_dwordx3", VReg_96>;
defm GLOBAL_STORE_DWORDX4 : FLAT_Global_Store_Pseudo <"global_store_dwordx4", VReg_128>;

defm GLOBAL_STORE_BYTE_D16_HI  : FLAT_Global_Store_Pseudo <"global_store_byte_d16_hi", VGPR_32>;
defm GLOBAL_STORE_SHORT_D16_HI : FLAT_Global_Store_Pseudo <"global_store_short_d16_hi", VGPR_32>;

let is_flat_global = 1 in {
defm GLOBAL_ATOMIC_CMPSWAP : FLAT_Global_Atomic_Pseudo <"global_atomic_cmpswap",
                               VGPR_32, i32, AMDGPUatomic_cmp_swap_global,
                               v2i32, VReg_64>;

defm GLOBAL_ATOMIC_CMPSWAP_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_cmpswap_x2",
                                  VReg_64, i64, AMDGPUatomic_cmp_swap_global,
                                  v2i64, VReg_128>;

defm GLOBAL_ATOMIC_SWAP : FLAT_Global_Atomic_Pseudo <"global_atomic_swap",
                             VGPR_32, i32, atomic_swap_global>;

defm GLOBAL_ATOMIC_SWAP_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_swap_x2",
                                VReg_64, i64, atomic_swap_global>;

defm GLOBAL_ATOMIC_ADD : FLAT_Global_Atomic_Pseudo <"global_atomic_add",
                           VGPR_32, i32, atomic_add_global>;

defm GLOBAL_ATOMIC_SUB : FLAT_Global_Atomic_Pseudo <"global_atomic_sub",
                           VGPR_32, i32, atomic_sub_global>;

defm GLOBAL_ATOMIC_SMIN : FLAT_Global_Atomic_Pseudo <"global_atomic_smin",
                            VGPR_32, i32, atomic_min_global>;

defm GLOBAL_ATOMIC_UMIN : FLAT_Global_Atomic_Pseudo <"global_atomic_umin",
                            VGPR_32, i32, atomic_umin_global>;

defm GLOBAL_ATOMIC_SMAX : FLAT_Global_Atomic_Pseudo <"global_atomic_smax",
                            VGPR_32, i32, atomic_max_global>;

defm GLOBAL_ATOMIC_UMAX : FLAT_Global_Atomic_Pseudo <"global_atomic_umax",
                            VGPR_32, i32, atomic_umax_global>;

defm GLOBAL_ATOMIC_AND : FLAT_Global_Atomic_Pseudo <"global_atomic_and",
                           VGPR_32, i32, atomic_and_global>;

defm GLOBAL_ATOMIC_OR : FLAT_Global_Atomic_Pseudo <"global_atomic_or",
                          VGPR_32, i32, atomic_or_global>;

defm GLOBAL_ATOMIC_XOR : FLAT_Global_Atomic_Pseudo <"global_atomic_xor",
                           VGPR_32, i32, atomic_xor_global>;

defm GLOBAL_ATOMIC_INC : FLAT_Global_Atomic_Pseudo <"global_atomic_inc",
                           VGPR_32, i32, atomic_inc_global>;

defm GLOBAL_ATOMIC_DEC : FLAT_Global_Atomic_Pseudo <"global_atomic_dec",
                           VGPR_32, i32, atomic_dec_global>;

defm GLOBAL_ATOMIC_ADD_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_add_x2",
                              VReg_64, i64, atomic_add_global>;

defm GLOBAL_ATOMIC_SUB_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_sub_x2",
                              VReg_64, i64, atomic_sub_global>;

defm GLOBAL_ATOMIC_SMIN_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_smin_x2",
                               VReg_64, i64, atomic_min_global>;

defm GLOBAL_ATOMIC_UMIN_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_umin_x2",
                               VReg_64, i64, atomic_umin_global>;

defm GLOBAL_ATOMIC_SMAX_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_smax_x2",
                               VReg_64, i64, atomic_max_global>;

defm GLOBAL_ATOMIC_UMAX_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_umax_x2",
                               VReg_64, i64, atomic_umax_global>;

defm GLOBAL_ATOMIC_AND_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_and_x2",
                              VReg_64, i64, atomic_and_global>;

defm GLOBAL_ATOMIC_OR_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_or_x2",
                             VReg_64, i64, atomic_or_global>;

defm GLOBAL_ATOMIC_XOR_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_xor_x2",
                              VReg_64, i64, atomic_xor_global>;

defm GLOBAL_ATOMIC_INC_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_inc_x2",
                              VReg_64, i64, atomic_inc_global>;

defm GLOBAL_ATOMIC_DEC_X2 : FLAT_Global_Atomic_Pseudo <"global_atomic_dec_x2",
                              VReg_64, i64, atomic_dec_global>;
} // End is_flat_global = 1

} // End SubtargetPredicate = HasFlatGlobalInsts


let SubtargetPredicate = HasFlatScratchInsts in {
defm SCRATCH_LOAD_UBYTE    : FLAT_Scratch_Load_Pseudo <"scratch_load_ubyte", VGPR_32>;
defm SCRATCH_LOAD_SBYTE    : FLAT_Scratch_Load_Pseudo <"scratch_load_sbyte", VGPR_32>;
defm SCRATCH_LOAD_USHORT   : FLAT_Scratch_Load_Pseudo <"scratch_load_ushort", VGPR_32>;
defm SCRATCH_LOAD_SSHORT   : FLAT_Scratch_Load_Pseudo <"scratch_load_sshort", VGPR_32>;
defm SCRATCH_LOAD_DWORD    : FLAT_Scratch_Load_Pseudo <"scratch_load_dword", VGPR_32>;
defm SCRATCH_LOAD_DWORDX2  : FLAT_Scratch_Load_Pseudo <"scratch_load_dwordx2", VReg_64>;
defm SCRATCH_LOAD_DWORDX3  : FLAT_Scratch_Load_Pseudo <"scratch_load_dwordx3", VReg_96>;
defm SCRATCH_LOAD_DWORDX4  : FLAT_Scratch_Load_Pseudo <"scratch_load_dwordx4", VReg_128>;

defm SCRATCH_LOAD_UBYTE_D16    : FLAT_Scratch_Load_Pseudo <"scratch_load_ubyte_d16", VGPR_32>;
defm SCRATCH_LOAD_UBYTE_D16_HI : FLAT_Scratch_Load_Pseudo <"scratch_load_ubyte_d16_hi", VGPR_32>;
defm SCRATCH_LOAD_SBYTE_D16    : FLAT_Scratch_Load_Pseudo <"scratch_load_sbyte_d16", VGPR_32>;
defm SCRATCH_LOAD_SBYTE_D16_HI : FLAT_Scratch_Load_Pseudo <"scratch_load_sbyte_d16_hi", VGPR_32>;
defm SCRATCH_LOAD_SHORT_D16    : FLAT_Scratch_Load_Pseudo <"scratch_load_short_d16", VGPR_32>;
defm SCRATCH_LOAD_SHORT_D16_HI : FLAT_Scratch_Load_Pseudo <"scratch_load_short_d16_hi", VGPR_32>;

defm SCRATCH_STORE_BYTE    : FLAT_Scratch_Store_Pseudo <"scratch_store_byte", VGPR_32>;
defm SCRATCH_STORE_SHORT   : FLAT_Scratch_Store_Pseudo <"scratch_store_short", VGPR_32>;
defm SCRATCH_STORE_DWORD   : FLAT_Scratch_Store_Pseudo <"scratch_store_dword", VGPR_32>;
defm SCRATCH_STORE_DWORDX2 : FLAT_Scratch_Store_Pseudo <"scratch_store_dwordx2", VReg_64>;
defm SCRATCH_STORE_DWORDX3 : FLAT_Scratch_Store_Pseudo <"scratch_store_dwordx3", VReg_96>;
defm SCRATCH_STORE_DWORDX4 : FLAT_Scratch_Store_Pseudo <"scratch_store_dwordx4", VReg_128>;

defm SCRATCH_STORE_BYTE_D16_HI : FLAT_Scratch_Store_Pseudo <"scratch_store_byte_d16_hi", VGPR_32>;
defm SCRATCH_STORE_SHORT_D16_HI : FLAT_Scratch_Store_Pseudo <"scratch_store_short_d16_hi", VGPR_32>;

} // End SubtargetPredicate = HasFlatScratchInsts

//===----------------------------------------------------------------------===//
// Flat Patterns
//===----------------------------------------------------------------------===//

// Patterns for global loads with no offset.
class FlatLoadPat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt> : GCNPat <
  (vt (node (FLATOffset i64:$vaddr, i16:$offset, i1:$slc))),
  (inst $vaddr, $offset, 0, $slc)
>;

multiclass FlatLoadPat_Hi16 <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt = i16> {
  def : GCNPat <
    (build_vector vt:$elt0, (vt (node (FLATOffset i64:$vaddr, i16:$offset, i1:$slc)))),
    (v2i16 (inst $vaddr, $offset, 0, $slc, $elt0))
  >;

 def : GCNPat <
    (build_vector f16:$elt0, (f16 (bitconvert (vt (node (FLATOffset i64:$vaddr, i16:$offset, i1:$slc)))))),
    (v2f16 (inst $vaddr, $offset, 0, $slc, $elt0))
  >;
}

multiclass FlatSignedLoadPat_Hi16 <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt = i16> {
  def : GCNPat <
    (build_vector vt:$elt0, (vt (node (FLATOffsetSigned i64:$vaddr, i16:$offset, i1:$slc)))),
    (v2i16 (inst $vaddr, $offset, 0, $slc, $elt0))
  >;

 def : GCNPat <
    (build_vector f16:$elt0, (f16 (bitconvert (vt (node (FLATOffsetSigned i64:$vaddr, i16:$offset, i1:$slc)))))),
    (v2f16 (inst $vaddr, $offset, 0, $slc, $elt0))
  >;
}

multiclass FlatLoadPat_Lo16 <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt = i16> {
  def : GCNPat <
    (build_vector (vt (node (FLATOffset i64:$vaddr, i16:$offset, i1:$slc))), (vt (Hi16Elt vt:$hi))),
    (v2i16 (inst $vaddr, $offset, 0, $slc, $hi))
  >;

 def : GCNPat <
    (build_vector (f16 (bitconvert (vt (node (FLATOffset i64:$vaddr, i16:$offset, i1:$slc))))), (f16 (Hi16Elt f16:$hi))),
    (v2f16 (inst $vaddr, $offset, 0, $slc, $hi))
  >;
}

multiclass FlatSignedLoadPat_Lo16 <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt = i16> {
  def : GCNPat <
    (build_vector (vt (node (FLATOffsetSigned i64:$vaddr, i16:$offset, i1:$slc))), (vt (Hi16Elt vt:$hi))),
    (v2i16 (inst $vaddr, $offset, 0, $slc, $hi))
  >;

 def : GCNPat <
    (build_vector (f16 (bitconvert (vt (node (FLATOffsetSigned i64:$vaddr, i16:$offset, i1:$slc))))), (f16 (Hi16Elt f16:$hi))),
    (v2f16 (inst $vaddr, $offset, 0, $slc, $hi))
  >;
}

class FlatLoadAtomicPat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt> : GCNPat <
  (vt (node (FLATAtomic i64:$vaddr, i16:$offset, i1:$slc))),
  (inst $vaddr, $offset, 0, $slc)
>;

class FlatLoadSignedPat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt> : GCNPat <
  (vt (node (FLATOffsetSigned i64:$vaddr, i16:$offset, i1:$slc))),
  (inst $vaddr, $offset, 0, $slc)
>;

class FlatStorePat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt> : GCNPat <
  (node vt:$data, (FLATOffset i64:$vaddr, i16:$offset, i1:$slc)),
  (inst $vaddr, $data, $offset, 0, $slc)
>;

class FlatStoreSignedPat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt> : GCNPat <
  (node vt:$data, (FLATOffsetSigned i64:$vaddr, i16:$offset, i1:$slc)),
  (inst $vaddr, $data, $offset, 0, $slc)
>;

class FlatStoreAtomicPat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt> : GCNPat <
  // atomic store follows atomic binop convention so the address comes
  // first.
  (node (FLATAtomic i64:$vaddr, i16:$offset, i1:$slc), vt:$data),
  (inst $vaddr, $data, $offset, 0, $slc)
>;

class FlatStoreSignedAtomicPat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt> : GCNPat <
  // atomic store follows atomic binop convention so the address comes
  // first.
  (node (FLATSignedAtomic i64:$vaddr, i16:$offset, i1:$slc), vt:$data),
  (inst $vaddr, $data, $offset, 0, $slc)
>;

class FlatAtomicPat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt,
                     ValueType data_vt = vt> : GCNPat <
  (vt (node (FLATAtomic i64:$vaddr, i16:$offset, i1:$slc), data_vt:$data)),
  (inst $vaddr, $data, $offset, $slc)
>;

class FlatSignedAtomicPat <FLAT_Pseudo inst, SDPatternOperator node, ValueType vt,
                     ValueType data_vt = vt> : GCNPat <
  (vt (node (FLATSignedAtomic i64:$vaddr, i16:$offset, i1:$slc), data_vt:$data)),
  (inst $vaddr, $data, $offset, $slc)
>;

let OtherPredicates = [HasFlatAddressSpace] in {

def : FlatLoadPat <FLAT_LOAD_UBYTE, az_extloadi8_flat, i32>;
def : FlatLoadPat <FLAT_LOAD_SBYTE, sextloadi8_flat, i32>;
def : FlatLoadPat <FLAT_LOAD_UBYTE, az_extloadi8_flat, i16>;
def : FlatLoadPat <FLAT_LOAD_SBYTE, sextloadi8_flat, i16>;
def : FlatLoadPat <FLAT_LOAD_USHORT, az_extloadi16_flat, i32>;
def : FlatLoadPat <FLAT_LOAD_USHORT, load_flat, i16>;
def : FlatLoadPat <FLAT_LOAD_SSHORT, sextloadi16_flat, i32>;
def : FlatLoadPat <FLAT_LOAD_DWORD, load_flat, i32>;
def : FlatLoadPat <FLAT_LOAD_DWORDX2, load_flat, v2i32>;
def : FlatLoadPat <FLAT_LOAD_DWORDX4, load_flat, v4i32>;

def : FlatLoadAtomicPat <FLAT_LOAD_DWORD, atomic_load_flat, i32>;
def : FlatLoadAtomicPat <FLAT_LOAD_DWORDX2, atomic_load_flat, i64>;

def : FlatStorePat <FLAT_STORE_BYTE, truncstorei8_flat, i32>;
def : FlatStorePat <FLAT_STORE_SHORT, truncstorei16_flat, i32>;
def : FlatStorePat <FLAT_STORE_DWORD, store_flat, i32>;
def : FlatStorePat <FLAT_STORE_DWORDX2, store_flat, v2i32>;
def : FlatStorePat <FLAT_STORE_DWORDX4, store_flat, v4i32>;

def : FlatStoreAtomicPat <FLAT_STORE_DWORD, atomic_store_flat, i32>;
def : FlatStoreAtomicPat <FLAT_STORE_DWORDX2, atomic_store_flat, i64>;

def : FlatAtomicPat <FLAT_ATOMIC_ADD_RTN, atomic_add_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_SUB_RTN, atomic_sub_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_INC_RTN, atomic_inc_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_DEC_RTN, atomic_dec_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_AND_RTN, atomic_and_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_SMAX_RTN, atomic_max_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_UMAX_RTN, atomic_umax_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_SMIN_RTN, atomic_min_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_UMIN_RTN, atomic_umin_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_OR_RTN, atomic_or_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_SWAP_RTN, atomic_swap_global, i32>;
def : FlatAtomicPat <FLAT_ATOMIC_CMPSWAP_RTN, AMDGPUatomic_cmp_swap_global, i32, v2i32>;
def : FlatAtomicPat <FLAT_ATOMIC_XOR_RTN, atomic_xor_global, i32>;

def : FlatAtomicPat <FLAT_ATOMIC_ADD_X2_RTN, atomic_add_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_SUB_X2_RTN, atomic_sub_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_INC_X2_RTN, atomic_inc_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_DEC_X2_RTN, atomic_dec_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_AND_X2_RTN, atomic_and_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_SMAX_X2_RTN, atomic_max_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_UMAX_X2_RTN, atomic_umax_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_SMIN_X2_RTN, atomic_min_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_UMIN_X2_RTN, atomic_umin_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_OR_X2_RTN, atomic_or_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_SWAP_X2_RTN, atomic_swap_global, i64>;
def : FlatAtomicPat <FLAT_ATOMIC_CMPSWAP_X2_RTN, AMDGPUatomic_cmp_swap_global, i64, v2i64>;
def : FlatAtomicPat <FLAT_ATOMIC_XOR_X2_RTN, atomic_xor_global, i64>;

def : FlatStorePat <FLAT_STORE_BYTE, truncstorei8_flat, i16>;
def : FlatStorePat <FLAT_STORE_SHORT, store_flat, i16>;

let OtherPredicates = [D16PreservesUnusedBits] in {
def : FlatStorePat <FLAT_STORE_SHORT_D16_HI, truncstorei16_hi16_flat, i32>;
def : FlatStorePat <FLAT_STORE_BYTE_D16_HI, truncstorei8_hi16_flat, i32>;

let AddedComplexity = 3 in {
defm : FlatLoadPat_Hi16 <FLAT_LOAD_UBYTE_D16_HI, az_extloadi8_flat>;
defm : FlatLoadPat_Hi16 <FLAT_LOAD_SBYTE_D16_HI, sextloadi8_flat>;
defm : FlatLoadPat_Hi16 <FLAT_LOAD_SHORT_D16_HI, load_flat>;
}

let AddedComplexity = 9 in {
defm : FlatLoadPat_Lo16 <FLAT_LOAD_UBYTE_D16, az_extloadi8_flat>;
defm : FlatLoadPat_Lo16 <FLAT_LOAD_SBYTE_D16, sextloadi8_flat>;
defm : FlatLoadPat_Lo16 <FLAT_LOAD_SHORT_D16, load_flat>;
}
}

} // End OtherPredicates = [HasFlatAddressSpace]

let OtherPredicates = [HasFlatGlobalInsts], AddedComplexity = 10 in {

def : FlatLoadSignedPat <GLOBAL_LOAD_UBYTE, az_extloadi8_global, i32>;
def : FlatLoadSignedPat <GLOBAL_LOAD_SBYTE, sextloadi8_global, i32>;
def : FlatLoadSignedPat <GLOBAL_LOAD_UBYTE, az_extloadi8_global, i16>;
def : FlatLoadSignedPat <GLOBAL_LOAD_SBYTE, sextloadi8_global, i16>;
def : FlatLoadSignedPat <GLOBAL_LOAD_USHORT, az_extloadi16_global, i32>;
def : FlatLoadSignedPat <GLOBAL_LOAD_SSHORT, sextloadi16_global, i32>;
def : FlatLoadSignedPat <GLOBAL_LOAD_USHORT, load_global, i16>;

def : FlatLoadSignedPat <GLOBAL_LOAD_DWORD, load_global, i32>;
def : FlatLoadSignedPat <GLOBAL_LOAD_DWORDX2, load_global, v2i32>;
def : FlatLoadSignedPat <GLOBAL_LOAD_DWORDX4, load_global, v4i32>;

def : FlatLoadAtomicPat <GLOBAL_LOAD_DWORD, atomic_load_global, i32>;
def : FlatLoadAtomicPat <GLOBAL_LOAD_DWORDX2, atomic_load_global, i64>;

def : FlatStoreSignedPat <GLOBAL_STORE_BYTE, truncstorei8_global, i32>;
def : FlatStoreSignedPat <GLOBAL_STORE_BYTE, truncstorei8_global, i16>;
def : FlatStoreSignedPat <GLOBAL_STORE_SHORT, truncstorei16_global, i32>;
def : FlatStoreSignedPat <GLOBAL_STORE_SHORT, store_global, i16>;
def : FlatStoreSignedPat <GLOBAL_STORE_DWORD, store_global, i32>;
def : FlatStoreSignedPat <GLOBAL_STORE_DWORDX2, store_global, v2i32>;
def : FlatStoreSignedPat <GLOBAL_STORE_DWORDX4, store_global, v4i32>;

let OtherPredicates = [D16PreservesUnusedBits] in {
def : FlatStoreSignedPat <GLOBAL_STORE_SHORT_D16_HI, truncstorei16_hi16_global, i32>;
def : FlatStoreSignedPat <GLOBAL_STORE_BYTE_D16_HI, truncstorei8_hi16_global, i32>;

defm : FlatSignedLoadPat_Hi16 <GLOBAL_LOAD_UBYTE_D16_HI, az_extloadi8_global>;
defm : FlatSignedLoadPat_Hi16 <GLOBAL_LOAD_SBYTE_D16_HI, sextloadi8_global>;
defm : FlatSignedLoadPat_Hi16 <GLOBAL_LOAD_SHORT_D16_HI, load_global>;

defm : FlatSignedLoadPat_Lo16 <GLOBAL_LOAD_UBYTE_D16, az_extloadi8_global>;
defm : FlatSignedLoadPat_Lo16 <GLOBAL_LOAD_SBYTE_D16, sextloadi8_global>;
defm : FlatSignedLoadPat_Lo16 <GLOBAL_LOAD_SHORT_D16, load_global>;

}

def : FlatStoreSignedAtomicPat <GLOBAL_STORE_DWORD, store_atomic_global, i32>;
def : FlatStoreSignedAtomicPat <GLOBAL_STORE_DWORDX2, store_atomic_global, i64>;

def : FlatSignedAtomicPat <GLOBAL_ATOMIC_ADD_RTN, atomic_add_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_SUB_RTN, atomic_sub_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_INC_RTN, atomic_inc_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_DEC_RTN, atomic_dec_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_AND_RTN, atomic_and_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_SMAX_RTN, atomic_max_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_UMAX_RTN, atomic_umax_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_SMIN_RTN, atomic_min_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_UMIN_RTN, atomic_umin_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_OR_RTN, atomic_or_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_SWAP_RTN, atomic_swap_global, i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_CMPSWAP_RTN, AMDGPUatomic_cmp_swap_global, i32, v2i32>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_XOR_RTN, atomic_xor_global, i32>;

def : FlatSignedAtomicPat <GLOBAL_ATOMIC_ADD_X2_RTN, atomic_add_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_SUB_X2_RTN, atomic_sub_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_INC_X2_RTN, atomic_inc_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_DEC_X2_RTN, atomic_dec_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_AND_X2_RTN, atomic_and_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_SMAX_X2_RTN, atomic_max_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_UMAX_X2_RTN, atomic_umax_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_SMIN_X2_RTN, atomic_min_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_UMIN_X2_RTN, atomic_umin_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_OR_X2_RTN, atomic_or_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_SWAP_X2_RTN, atomic_swap_global, i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_CMPSWAP_X2_RTN, AMDGPUatomic_cmp_swap_global, i64, v2i64>;
def : FlatSignedAtomicPat <GLOBAL_ATOMIC_XOR_X2_RTN, atomic_xor_global, i64>;

} // End OtherPredicates = [HasFlatGlobalInsts]


//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// CI
//===----------------------------------------------------------------------===//

class FLAT_Real_ci <bits<7> op, FLAT_Pseudo ps> :
  FLAT_Real <op, ps>,
  SIMCInstr <ps.PseudoInstr, SIEncodingFamily.SI> {
  let AssemblerPredicate = isCIOnly;
  let DecoderNamespace="CI";
}

def FLAT_LOAD_UBYTE_ci         : FLAT_Real_ci <0x8,  FLAT_LOAD_UBYTE>;
def FLAT_LOAD_SBYTE_ci         : FLAT_Real_ci <0x9,  FLAT_LOAD_SBYTE>;
def FLAT_LOAD_USHORT_ci        : FLAT_Real_ci <0xa,  FLAT_LOAD_USHORT>;
def FLAT_LOAD_SSHORT_ci        : FLAT_Real_ci <0xb,  FLAT_LOAD_SSHORT>;
def FLAT_LOAD_DWORD_ci         : FLAT_Real_ci <0xc,  FLAT_LOAD_DWORD>;
def FLAT_LOAD_DWORDX2_ci       : FLAT_Real_ci <0xd,  FLAT_LOAD_DWORDX2>;
def FLAT_LOAD_DWORDX4_ci       : FLAT_Real_ci <0xe,  FLAT_LOAD_DWORDX4>;
def FLAT_LOAD_DWORDX3_ci       : FLAT_Real_ci <0xf,  FLAT_LOAD_DWORDX3>;

def FLAT_STORE_BYTE_ci         : FLAT_Real_ci <0x18, FLAT_STORE_BYTE>;
def FLAT_STORE_SHORT_ci        : FLAT_Real_ci <0x1a, FLAT_STORE_SHORT>;
def FLAT_STORE_DWORD_ci        : FLAT_Real_ci <0x1c, FLAT_STORE_DWORD>;
def FLAT_STORE_DWORDX2_ci      : FLAT_Real_ci <0x1d, FLAT_STORE_DWORDX2>;
def FLAT_STORE_DWORDX4_ci      : FLAT_Real_ci <0x1e, FLAT_STORE_DWORDX4>;
def FLAT_STORE_DWORDX3_ci      : FLAT_Real_ci <0x1f, FLAT_STORE_DWORDX3>;

multiclass FLAT_Real_Atomics_ci <bits<7> op, FLAT_Pseudo ps> {
  def _ci     : FLAT_Real_ci<op, !cast<FLAT_Pseudo>(ps.PseudoInstr)>;
  def _RTN_ci : FLAT_Real_ci<op, !cast<FLAT_Pseudo>(ps.PseudoInstr # "_RTN")>;
}

defm FLAT_ATOMIC_SWAP          : FLAT_Real_Atomics_ci <0x30, FLAT_ATOMIC_SWAP>;
defm FLAT_ATOMIC_CMPSWAP       : FLAT_Real_Atomics_ci <0x31, FLAT_ATOMIC_CMPSWAP>;
defm FLAT_ATOMIC_ADD           : FLAT_Real_Atomics_ci <0x32, FLAT_ATOMIC_ADD>;
defm FLAT_ATOMIC_SUB           : FLAT_Real_Atomics_ci <0x33, FLAT_ATOMIC_SUB>;
defm FLAT_ATOMIC_SMIN          : FLAT_Real_Atomics_ci <0x35, FLAT_ATOMIC_SMIN>;
defm FLAT_ATOMIC_UMIN          : FLAT_Real_Atomics_ci <0x36, FLAT_ATOMIC_UMIN>;
defm FLAT_ATOMIC_SMAX          : FLAT_Real_Atomics_ci <0x37, FLAT_ATOMIC_SMAX>;
defm FLAT_ATOMIC_UMAX          : FLAT_Real_Atomics_ci <0x38, FLAT_ATOMIC_UMAX>;
defm FLAT_ATOMIC_AND           : FLAT_Real_Atomics_ci <0x39, FLAT_ATOMIC_AND>;
defm FLAT_ATOMIC_OR            : FLAT_Real_Atomics_ci <0x3a, FLAT_ATOMIC_OR>;
defm FLAT_ATOMIC_XOR           : FLAT_Real_Atomics_ci <0x3b, FLAT_ATOMIC_XOR>;
defm FLAT_ATOMIC_INC           : FLAT_Real_Atomics_ci <0x3c, FLAT_ATOMIC_INC>;
defm FLAT_ATOMIC_DEC           : FLAT_Real_Atomics_ci <0x3d, FLAT_ATOMIC_DEC>;
defm FLAT_ATOMIC_SWAP_X2       : FLAT_Real_Atomics_ci <0x50, FLAT_ATOMIC_SWAP_X2>;
defm FLAT_ATOMIC_CMPSWAP_X2    : FLAT_Real_Atomics_ci <0x51, FLAT_ATOMIC_CMPSWAP_X2>;
defm FLAT_ATOMIC_ADD_X2        : FLAT_Real_Atomics_ci <0x52, FLAT_ATOMIC_ADD_X2>;
defm FLAT_ATOMIC_SUB_X2        : FLAT_Real_Atomics_ci <0x53, FLAT_ATOMIC_SUB_X2>;
defm FLAT_ATOMIC_SMIN_X2       : FLAT_Real_Atomics_ci <0x55, FLAT_ATOMIC_SMIN_X2>;
defm FLAT_ATOMIC_UMIN_X2       : FLAT_Real_Atomics_ci <0x56, FLAT_ATOMIC_UMIN_X2>;
defm FLAT_ATOMIC_SMAX_X2       : FLAT_Real_Atomics_ci <0x57, FLAT_ATOMIC_SMAX_X2>;
defm FLAT_ATOMIC_UMAX_X2       : FLAT_Real_Atomics_ci <0x58, FLAT_ATOMIC_UMAX_X2>;
defm FLAT_ATOMIC_AND_X2        : FLAT_Real_Atomics_ci <0x59, FLAT_ATOMIC_AND_X2>;
defm FLAT_ATOMIC_OR_X2         : FLAT_Real_Atomics_ci <0x5a, FLAT_ATOMIC_OR_X2>;
defm FLAT_ATOMIC_XOR_X2        : FLAT_Real_Atomics_ci <0x5b, FLAT_ATOMIC_XOR_X2>;
defm FLAT_ATOMIC_INC_X2        : FLAT_Real_Atomics_ci <0x5c, FLAT_ATOMIC_INC_X2>;
defm FLAT_ATOMIC_DEC_X2        : FLAT_Real_Atomics_ci <0x5d, FLAT_ATOMIC_DEC_X2>;

// CI Only flat instructions
defm FLAT_ATOMIC_FCMPSWAP      : FLAT_Real_Atomics_ci <0x3e, FLAT_ATOMIC_FCMPSWAP>;
defm FLAT_ATOMIC_FMIN          : FLAT_Real_Atomics_ci <0x3f, FLAT_ATOMIC_FMIN>;
defm FLAT_ATOMIC_FMAX          : FLAT_Real_Atomics_ci <0x40, FLAT_ATOMIC_FMAX>;
defm FLAT_ATOMIC_FCMPSWAP_X2   : FLAT_Real_Atomics_ci <0x5e, FLAT_ATOMIC_FCMPSWAP_X2>;
defm FLAT_ATOMIC_FMIN_X2       : FLAT_Real_Atomics_ci <0x5f, FLAT_ATOMIC_FMIN_X2>;
defm FLAT_ATOMIC_FMAX_X2       : FLAT_Real_Atomics_ci <0x60, FLAT_ATOMIC_FMAX_X2>;


//===----------------------------------------------------------------------===//
// VI
//===----------------------------------------------------------------------===//

class FLAT_Real_vi <bits<7> op, FLAT_Pseudo ps> :
  FLAT_Real <op, ps>,
  SIMCInstr <ps.PseudoInstr, SIEncodingFamily.VI> {
  let AssemblerPredicate = isVI;
  let DecoderNamespace="VI";
}

multiclass FLAT_Real_AllAddr_vi<bits<7> op> {
  def _vi : FLAT_Real_vi<op, !cast<FLAT_Pseudo>(NAME)>;
  def _SADDR_vi : FLAT_Real_vi<op, !cast<FLAT_Pseudo>(NAME#"_SADDR")>;
}

def FLAT_LOAD_UBYTE_vi         : FLAT_Real_vi <0x10, FLAT_LOAD_UBYTE>;
def FLAT_LOAD_SBYTE_vi         : FLAT_Real_vi <0x11, FLAT_LOAD_SBYTE>;
def FLAT_LOAD_USHORT_vi        : FLAT_Real_vi <0x12, FLAT_LOAD_USHORT>;
def FLAT_LOAD_SSHORT_vi        : FLAT_Real_vi <0x13, FLAT_LOAD_SSHORT>;
def FLAT_LOAD_DWORD_vi         : FLAT_Real_vi <0x14, FLAT_LOAD_DWORD>;
def FLAT_LOAD_DWORDX2_vi       : FLAT_Real_vi <0x15, FLAT_LOAD_DWORDX2>;
def FLAT_LOAD_DWORDX4_vi       : FLAT_Real_vi <0x17, FLAT_LOAD_DWORDX4>;
def FLAT_LOAD_DWORDX3_vi       : FLAT_Real_vi <0x16, FLAT_LOAD_DWORDX3>;

def FLAT_STORE_BYTE_vi         : FLAT_Real_vi <0x18, FLAT_STORE_BYTE>;
def FLAT_STORE_BYTE_D16_HI_vi  : FLAT_Real_vi <0x19, FLAT_STORE_BYTE_D16_HI>;
def FLAT_STORE_SHORT_vi        : FLAT_Real_vi <0x1a, FLAT_STORE_SHORT>;
def FLAT_STORE_SHORT_D16_HI_vi : FLAT_Real_vi <0x1b, FLAT_STORE_SHORT_D16_HI>;
def FLAT_STORE_DWORD_vi        : FLAT_Real_vi <0x1c, FLAT_STORE_DWORD>;
def FLAT_STORE_DWORDX2_vi      : FLAT_Real_vi <0x1d, FLAT_STORE_DWORDX2>;
def FLAT_STORE_DWORDX4_vi      : FLAT_Real_vi <0x1f, FLAT_STORE_DWORDX4>;
def FLAT_STORE_DWORDX3_vi      : FLAT_Real_vi <0x1e, FLAT_STORE_DWORDX3>;

def FLAT_LOAD_UBYTE_D16_vi    : FLAT_Real_vi <0x20, FLAT_LOAD_UBYTE_D16>;
def FLAT_LOAD_UBYTE_D16_HI_vi : FLAT_Real_vi <0x21, FLAT_LOAD_UBYTE_D16_HI>;
def FLAT_LOAD_SBYTE_D16_vi    : FLAT_Real_vi <0x22, FLAT_LOAD_SBYTE_D16>;
def FLAT_LOAD_SBYTE_D16_HI_vi : FLAT_Real_vi <0x23, FLAT_LOAD_SBYTE_D16_HI>;
def FLAT_LOAD_SHORT_D16_vi    : FLAT_Real_vi <0x24, FLAT_LOAD_SHORT_D16>;
def FLAT_LOAD_SHORT_D16_HI_vi : FLAT_Real_vi <0x25, FLAT_LOAD_SHORT_D16_HI>;

multiclass FLAT_Real_Atomics_vi <bits<7> op, FLAT_Pseudo ps> {
  def _vi     : FLAT_Real_vi<op, !cast<FLAT_Pseudo>(ps.PseudoInstr)>;
  def _RTN_vi : FLAT_Real_vi<op, !cast<FLAT_Pseudo>(ps.PseudoInstr # "_RTN")>;
}

multiclass FLAT_Global_Real_Atomics_vi<bits<7> op> :
  FLAT_Real_AllAddr_vi<op> {
  def _RTN_vi  : FLAT_Real_vi <op, !cast<FLAT_Pseudo>(NAME#"_RTN")>;
  def _SADDR_RTN_vi : FLAT_Real_vi <op, !cast<FLAT_Pseudo>(NAME#"_SADDR_RTN")>;
}


defm FLAT_ATOMIC_SWAP       : FLAT_Real_Atomics_vi <0x40, FLAT_ATOMIC_SWAP>;
defm FLAT_ATOMIC_CMPSWAP    : FLAT_Real_Atomics_vi <0x41, FLAT_ATOMIC_CMPSWAP>;
defm FLAT_ATOMIC_ADD        : FLAT_Real_Atomics_vi <0x42, FLAT_ATOMIC_ADD>;
defm FLAT_ATOMIC_SUB        : FLAT_Real_Atomics_vi <0x43, FLAT_ATOMIC_SUB>;
defm FLAT_ATOMIC_SMIN       : FLAT_Real_Atomics_vi <0x44, FLAT_ATOMIC_SMIN>;
defm FLAT_ATOMIC_UMIN       : FLAT_Real_Atomics_vi <0x45, FLAT_ATOMIC_UMIN>;
defm FLAT_ATOMIC_SMAX       : FLAT_Real_Atomics_vi <0x46, FLAT_ATOMIC_SMAX>;
defm FLAT_ATOMIC_UMAX       : FLAT_Real_Atomics_vi <0x47, FLAT_ATOMIC_UMAX>;
defm FLAT_ATOMIC_AND        : FLAT_Real_Atomics_vi <0x48, FLAT_ATOMIC_AND>;
defm FLAT_ATOMIC_OR         : FLAT_Real_Atomics_vi <0x49, FLAT_ATOMIC_OR>;
defm FLAT_ATOMIC_XOR        : FLAT_Real_Atomics_vi <0x4a, FLAT_ATOMIC_XOR>;
defm FLAT_ATOMIC_INC        : FLAT_Real_Atomics_vi <0x4b, FLAT_ATOMIC_INC>;
defm FLAT_ATOMIC_DEC        : FLAT_Real_Atomics_vi <0x4c, FLAT_ATOMIC_DEC>;
defm FLAT_ATOMIC_SWAP_X2    : FLAT_Real_Atomics_vi <0x60, FLAT_ATOMIC_SWAP_X2>;
defm FLAT_ATOMIC_CMPSWAP_X2 : FLAT_Real_Atomics_vi <0x61, FLAT_ATOMIC_CMPSWAP_X2>;
defm FLAT_ATOMIC_ADD_X2     : FLAT_Real_Atomics_vi <0x62, FLAT_ATOMIC_ADD_X2>;
defm FLAT_ATOMIC_SUB_X2     : FLAT_Real_Atomics_vi <0x63, FLAT_ATOMIC_SUB_X2>;
defm FLAT_ATOMIC_SMIN_X2    : FLAT_Real_Atomics_vi <0x64, FLAT_ATOMIC_SMIN_X2>;
defm FLAT_ATOMIC_UMIN_X2    : FLAT_Real_Atomics_vi <0x65, FLAT_ATOMIC_UMIN_X2>;
defm FLAT_ATOMIC_SMAX_X2    : FLAT_Real_Atomics_vi <0x66, FLAT_ATOMIC_SMAX_X2>;
defm FLAT_ATOMIC_UMAX_X2    : FLAT_Real_Atomics_vi <0x67, FLAT_ATOMIC_UMAX_X2>;
defm FLAT_ATOMIC_AND_X2     : FLAT_Real_Atomics_vi <0x68, FLAT_ATOMIC_AND_X2>;
defm FLAT_ATOMIC_OR_X2      : FLAT_Real_Atomics_vi <0x69, FLAT_ATOMIC_OR_X2>;
defm FLAT_ATOMIC_XOR_X2     : FLAT_Real_Atomics_vi <0x6a, FLAT_ATOMIC_XOR_X2>;
defm FLAT_ATOMIC_INC_X2     : FLAT_Real_Atomics_vi <0x6b, FLAT_ATOMIC_INC_X2>;
defm FLAT_ATOMIC_DEC_X2     : FLAT_Real_Atomics_vi <0x6c, FLAT_ATOMIC_DEC_X2>;

defm GLOBAL_LOAD_UBYTE : FLAT_Real_AllAddr_vi <0x10>;
defm GLOBAL_LOAD_SBYTE : FLAT_Real_AllAddr_vi <0x11>;
defm GLOBAL_LOAD_USHORT : FLAT_Real_AllAddr_vi <0x12>;
defm GLOBAL_LOAD_SSHORT : FLAT_Real_AllAddr_vi <0x13>;
defm GLOBAL_LOAD_DWORD : FLAT_Real_AllAddr_vi <0x14>;
defm GLOBAL_LOAD_DWORDX2 : FLAT_Real_AllAddr_vi <0x15>;
defm GLOBAL_LOAD_DWORDX3 : FLAT_Real_AllAddr_vi <0x16>;
defm GLOBAL_LOAD_DWORDX4 : FLAT_Real_AllAddr_vi <0x17>;

defm GLOBAL_LOAD_UBYTE_D16    : FLAT_Real_AllAddr_vi <0x20>;
defm GLOBAL_LOAD_UBYTE_D16_HI : FLAT_Real_AllAddr_vi <0x21>;
defm GLOBAL_LOAD_SBYTE_D16    : FLAT_Real_AllAddr_vi <0x22>;
defm GLOBAL_LOAD_SBYTE_D16_HI : FLAT_Real_AllAddr_vi <0x23>;
defm GLOBAL_LOAD_SHORT_D16    : FLAT_Real_AllAddr_vi <0x24>;
defm GLOBAL_LOAD_SHORT_D16_HI : FLAT_Real_AllAddr_vi <0x25>;

defm GLOBAL_STORE_BYTE : FLAT_Real_AllAddr_vi <0x18>;
defm GLOBAL_STORE_BYTE_D16_HI : FLAT_Real_AllAddr_vi <0x19>;
defm GLOBAL_STORE_SHORT : FLAT_Real_AllAddr_vi <0x1a>;
defm GLOBAL_STORE_SHORT_D16_HI : FLAT_Real_AllAddr_vi <0x1b>;
defm GLOBAL_STORE_DWORD : FLAT_Real_AllAddr_vi <0x1c>;
defm GLOBAL_STORE_DWORDX2 : FLAT_Real_AllAddr_vi <0x1d>;
defm GLOBAL_STORE_DWORDX3 : FLAT_Real_AllAddr_vi <0x1e>;
defm GLOBAL_STORE_DWORDX4 : FLAT_Real_AllAddr_vi <0x1f>;


defm GLOBAL_ATOMIC_SWAP       : FLAT_Global_Real_Atomics_vi <0x40>;
defm GLOBAL_ATOMIC_CMPSWAP    : FLAT_Global_Real_Atomics_vi <0x41>;
defm GLOBAL_ATOMIC_ADD        : FLAT_Global_Real_Atomics_vi <0x42>;
defm GLOBAL_ATOMIC_SUB        : FLAT_Global_Real_Atomics_vi <0x43>;
defm GLOBAL_ATOMIC_SMIN       : FLAT_Global_Real_Atomics_vi <0x44>;
defm GLOBAL_ATOMIC_UMIN       : FLAT_Global_Real_Atomics_vi <0x45>;
defm GLOBAL_ATOMIC_SMAX       : FLAT_Global_Real_Atomics_vi <0x46>;
defm GLOBAL_ATOMIC_UMAX       : FLAT_Global_Real_Atomics_vi <0x47>;
defm GLOBAL_ATOMIC_AND        : FLAT_Global_Real_Atomics_vi <0x48>;
defm GLOBAL_ATOMIC_OR         : FLAT_Global_Real_Atomics_vi <0x49>;
defm GLOBAL_ATOMIC_XOR        : FLAT_Global_Real_Atomics_vi <0x4a>;
defm GLOBAL_ATOMIC_INC        : FLAT_Global_Real_Atomics_vi <0x4b>;
defm GLOBAL_ATOMIC_DEC        : FLAT_Global_Real_Atomics_vi <0x4c>;
defm GLOBAL_ATOMIC_SWAP_X2    : FLAT_Global_Real_Atomics_vi <0x60>;
defm GLOBAL_ATOMIC_CMPSWAP_X2 : FLAT_Global_Real_Atomics_vi <0x61>;
defm GLOBAL_ATOMIC_ADD_X2     : FLAT_Global_Real_Atomics_vi <0x62>;
defm GLOBAL_ATOMIC_SUB_X2     : FLAT_Global_Real_Atomics_vi <0x63>;
defm GLOBAL_ATOMIC_SMIN_X2    : FLAT_Global_Real_Atomics_vi <0x64>;
defm GLOBAL_ATOMIC_UMIN_X2    : FLAT_Global_Real_Atomics_vi <0x65>;
defm GLOBAL_ATOMIC_SMAX_X2    : FLAT_Global_Real_Atomics_vi <0x66>;
defm GLOBAL_ATOMIC_UMAX_X2    : FLAT_Global_Real_Atomics_vi <0x67>;
defm GLOBAL_ATOMIC_AND_X2     : FLAT_Global_Real_Atomics_vi <0x68>;
defm GLOBAL_ATOMIC_OR_X2      : FLAT_Global_Real_Atomics_vi <0x69>;
defm GLOBAL_ATOMIC_XOR_X2     : FLAT_Global_Real_Atomics_vi <0x6a>;
defm GLOBAL_ATOMIC_INC_X2     : FLAT_Global_Real_Atomics_vi <0x6b>;
defm GLOBAL_ATOMIC_DEC_X2     : FLAT_Global_Real_Atomics_vi <0x6c>;

defm SCRATCH_LOAD_UBYTE         : FLAT_Real_AllAddr_vi <0x10>;
defm SCRATCH_LOAD_SBYTE         : FLAT_Real_AllAddr_vi <0x11>;
defm SCRATCH_LOAD_USHORT        : FLAT_Real_AllAddr_vi <0x12>;
defm SCRATCH_LOAD_SSHORT        : FLAT_Real_AllAddr_vi <0x13>;
defm SCRATCH_LOAD_DWORD         : FLAT_Real_AllAddr_vi <0x14>;
defm SCRATCH_LOAD_DWORDX2       : FLAT_Real_AllAddr_vi <0x15>;
defm SCRATCH_LOAD_DWORDX3       : FLAT_Real_AllAddr_vi <0x16>;
defm SCRATCH_LOAD_DWORDX4       : FLAT_Real_AllAddr_vi <0x17>;
defm SCRATCH_STORE_BYTE         : FLAT_Real_AllAddr_vi <0x18>;
defm SCRATCH_STORE_BYTE_D16_HI  : FLAT_Real_AllAddr_vi <0x19>;
defm SCRATCH_LOAD_UBYTE_D16     : FLAT_Real_AllAddr_vi <0x20>;
defm SCRATCH_LOAD_UBYTE_D16_HI  : FLAT_Real_AllAddr_vi <0x21>;
defm SCRATCH_LOAD_SBYTE_D16     : FLAT_Real_AllAddr_vi <0x22>;
defm SCRATCH_LOAD_SBYTE_D16_HI  : FLAT_Real_AllAddr_vi <0x23>;
defm SCRATCH_LOAD_SHORT_D16     : FLAT_Real_AllAddr_vi <0x24>;
defm SCRATCH_LOAD_SHORT_D16_HI  : FLAT_Real_AllAddr_vi <0x25>;
defm SCRATCH_STORE_SHORT        : FLAT_Real_AllAddr_vi <0x1a>;
defm SCRATCH_STORE_SHORT_D16_HI : FLAT_Real_AllAddr_vi <0x1b>;
defm SCRATCH_STORE_DWORD        : FLAT_Real_AllAddr_vi <0x1c>;
defm SCRATCH_STORE_DWORDX2      : FLAT_Real_AllAddr_vi <0x1d>;
defm SCRATCH_STORE_DWORDX3      : FLAT_Real_AllAddr_vi <0x1e>;
defm SCRATCH_STORE_DWORDX4      : FLAT_Real_AllAddr_vi <0x1f>;
