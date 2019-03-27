//===-- MIMGInstructions.td - MIMG Instruction Defintions -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// MIMG-specific encoding families to distinguish between semantically
// equivalent machine instructions with different encoding.
//
// - MIMGEncGfx6: encoding introduced with gfx6 (obsoleted for atomics in gfx8)
// - MIMGEncGfx8: encoding introduced with gfx8 for atomics
class MIMGEncoding;

def MIMGEncGfx6 : MIMGEncoding;
def MIMGEncGfx8 : MIMGEncoding;

def MIMGEncoding : GenericEnum {
  let FilterClass = "MIMGEncoding";
}

// Represent an ISA-level opcode, independent of the encoding and the
// vdata/vaddr size.
class MIMGBaseOpcode {
  MIMGBaseOpcode BaseOpcode = !cast<MIMGBaseOpcode>(NAME);
  bit Store = 0;
  bit Atomic = 0;
  bit AtomicX2 = 0; // (f)cmpswap
  bit Sampler = 0;
  bit Gather4 = 0;
  bits<8> NumExtraArgs = 0;
  bit Gradients = 0;
  bit Coordinates = 1;
  bit LodOrClampOrMip = 0;
  bit HasD16 = 0;
}

def MIMGBaseOpcode : GenericEnum {
  let FilterClass = "MIMGBaseOpcode";
}

def MIMGBaseOpcodesTable : GenericTable {
  let FilterClass = "MIMGBaseOpcode";
  let CppTypeName = "MIMGBaseOpcodeInfo";
  let Fields = ["BaseOpcode", "Store", "Atomic", "AtomicX2", "Sampler", "Gather4",
                "NumExtraArgs", "Gradients", "Coordinates", "LodOrClampOrMip",
                "HasD16"];
  GenericEnum TypeOf_BaseOpcode = MIMGBaseOpcode;

  let PrimaryKey = ["BaseOpcode"];
  let PrimaryKeyName = "getMIMGBaseOpcodeInfo";
}

def MIMGDim : GenericEnum {
  let FilterClass = "AMDGPUDimProps";
}

def MIMGDimInfoTable : GenericTable {
  let FilterClass = "AMDGPUDimProps";
  let CppTypeName = "MIMGDimInfo";
  let Fields = ["Dim", "NumCoords", "NumGradients", "DA"];
  GenericEnum TypeOf_Dim = MIMGDim;

  let PrimaryKey = ["Dim"];
  let PrimaryKeyName = "getMIMGDimInfo";
}

class MIMGLZMapping<MIMGBaseOpcode l, MIMGBaseOpcode lz> {
  MIMGBaseOpcode L = l;
  MIMGBaseOpcode LZ = lz;
}

def MIMGLZMappingTable : GenericTable {
  let FilterClass = "MIMGLZMapping";
  let CppTypeName = "MIMGLZMappingInfo";
  let Fields = ["L", "LZ"];
  GenericEnum TypeOf_L = MIMGBaseOpcode;
  GenericEnum TypeOf_LZ = MIMGBaseOpcode;

  let PrimaryKey = ["L"];
  let PrimaryKeyName = "getMIMGLZMappingInfo";
}

class mimg <bits<7> si, bits<7> vi = si> {
  field bits<7> SI = si;
  field bits<7> VI = vi;
}

class MIMG <dag outs, string dns = "">
  : InstSI <outs, (ins), "", []> {

  let VM_CNT = 1;
  let EXP_CNT = 1;
  let MIMG = 1;
  let Uses = [EXEC];
  let mayLoad = 1;
  let mayStore = 0;
  let hasPostISelHook = 1;
  let SchedRW = [WriteVMEM];
  let UseNamedOperandTable = 1;
  let hasSideEffects = 0; // XXX ????

  let SubtargetPredicate = isGCN;
  let DecoderNamespace = dns;
  let isAsmParserOnly = !if(!eq(dns,""), 1, 0);
  let AsmMatchConverter = "cvtMIMG";
  let usesCustomInserter = 1;

  Instruction Opcode = !cast<Instruction>(NAME);
  MIMGBaseOpcode BaseOpcode;
  MIMGEncoding MIMGEncoding = MIMGEncGfx6;
  bits<8> VDataDwords;
  bits<8> VAddrDwords;
}

def MIMGInfoTable : GenericTable {
  let FilterClass = "MIMG";
  let CppTypeName = "MIMGInfo";
  let Fields = ["Opcode", "BaseOpcode", "MIMGEncoding", "VDataDwords", "VAddrDwords"];
  GenericEnum TypeOf_BaseOpcode = MIMGBaseOpcode;
  GenericEnum TypeOf_MIMGEncoding = MIMGEncoding;

  let PrimaryKey = ["BaseOpcode", "MIMGEncoding", "VDataDwords", "VAddrDwords"];
  let PrimaryKeyName = "getMIMGOpcodeHelper";
}

def getMIMGInfo : SearchIndex {
  let Table = MIMGInfoTable;
  let Key = ["Opcode"];
}

class MIMG_NoSampler_Helper <bits<7> op, string asm,
                             RegisterClass dst_rc,
                             RegisterClass addr_rc,
                             string dns="">
  : MIMG <(outs dst_rc:$vdata), dns>,
    MIMGe<op> {
  let ssamp = 0;
  let d16 = !if(BaseOpcode.HasD16, ?, 0);

  let InOperandList = !con((ins addr_rc:$vaddr, SReg_256:$srsrc,
                                DMask:$dmask, UNorm:$unorm, GLC:$glc, SLC:$slc,
                                R128A16:$r128, TFE:$tfe, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc$dmask$unorm$glc$slc$r128$tfe$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

multiclass MIMG_NoSampler_Src_Helper <bits<7> op, string asm,
                                             RegisterClass dst_rc,
                                             bit enableDisasm> {
  let VAddrDwords = 1 in
  def NAME # _V1 : MIMG_NoSampler_Helper <op, asm, dst_rc, VGPR_32,
                                         !if(enableDisasm, "AMDGPU", "")>;
  let VAddrDwords = 2 in
  def NAME # _V2 : MIMG_NoSampler_Helper <op, asm, dst_rc, VReg_64>;
  let VAddrDwords = 3 in
  def NAME # _V3 : MIMG_NoSampler_Helper <op, asm, dst_rc, VReg_96>;
  let VAddrDwords = 4 in
  def NAME # _V4 : MIMG_NoSampler_Helper <op, asm, dst_rc, VReg_128>;
}

multiclass MIMG_NoSampler <bits<7> op, string asm, bit has_d16, bit mip = 0,
                           bit isResInfo = 0> {
  def "" : MIMGBaseOpcode {
    let Coordinates = !if(isResInfo, 0, 1);
    let LodOrClampOrMip = mip;
    let HasD16 = has_d16;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME),
      mayLoad = !if(isResInfo, 0, 1) in {
    let VDataDwords = 1 in
    defm _V1 : MIMG_NoSampler_Src_Helper <op, asm, VGPR_32, 1>;
    let VDataDwords = 2 in
    defm _V2 : MIMG_NoSampler_Src_Helper <op, asm, VReg_64, 0>;
    let VDataDwords = 3 in
    defm _V3 : MIMG_NoSampler_Src_Helper <op, asm, VReg_96, 0>;
    let VDataDwords = 4 in
    defm _V4 : MIMG_NoSampler_Src_Helper <op, asm, VReg_128, 0>;
    let VDataDwords = 8 in
    defm _V8 : MIMG_NoSampler_Src_Helper <op, asm, VReg_256, 0>;
  }
}

class MIMG_Store_Helper <bits<7> op, string asm,
                         RegisterClass data_rc,
                         RegisterClass addr_rc,
                         string dns = "">
  : MIMG <(outs), dns>,
    MIMGe<op> {
  let ssamp = 0;
  let d16 = !if(BaseOpcode.HasD16, ?, 0);

  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 0;
  let hasPostISelHook = 0;
  let DisableWQM = 1;

  let InOperandList = !con((ins data_rc:$vdata, addr_rc:$vaddr, SReg_256:$srsrc,
                                DMask:$dmask, UNorm:$unorm, GLC:$glc, SLC:$slc,
                                R128A16:$r128, TFE:$tfe, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc$dmask$unorm$glc$slc$r128$tfe$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

multiclass MIMG_Store_Addr_Helper <bits<7> op, string asm,
                                  RegisterClass data_rc,
                                  bit enableDisasm> {
  let VAddrDwords = 1 in
  def NAME # _V1 : MIMG_Store_Helper <op, asm, data_rc, VGPR_32,
                                      !if(enableDisasm, "AMDGPU", "")>;
  let VAddrDwords = 2 in
  def NAME # _V2 : MIMG_Store_Helper <op, asm, data_rc, VReg_64>;
  let VAddrDwords = 3 in
  def NAME # _V3 : MIMG_Store_Helper <op, asm, data_rc, VReg_96>;
  let VAddrDwords = 4 in
  def NAME # _V4 : MIMG_Store_Helper <op, asm, data_rc, VReg_128>;
}

multiclass MIMG_Store <bits<7> op, string asm, bit has_d16, bit mip = 0> {
  def "" : MIMGBaseOpcode {
    let Store = 1;
    let LodOrClampOrMip = mip;
    let HasD16 = has_d16;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME) in {
    let VDataDwords = 1 in
    defm _V1 : MIMG_Store_Addr_Helper <op, asm, VGPR_32, 1>;
    let VDataDwords = 2 in
    defm _V2 : MIMG_Store_Addr_Helper <op, asm, VReg_64, 0>;
    let VDataDwords = 3 in
    defm _V3 : MIMG_Store_Addr_Helper <op, asm, VReg_96, 0>;
    let VDataDwords = 4 in
    defm _V4 : MIMG_Store_Addr_Helper <op, asm, VReg_128, 0>;
  }
}

class MIMG_Atomic_Helper <string asm, RegisterClass data_rc,
                          RegisterClass addr_rc, string dns="",
                          bit enableDasm = 0>
  : MIMG <(outs data_rc:$vdst), !if(enableDasm, dns, "")> {
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 1; // FIXME: Remove this
  let hasPostISelHook = 0;
  let DisableWQM = 1;
  let Constraints = "$vdst = $vdata";
  let AsmMatchConverter = "cvtMIMGAtomic";

  let InOperandList = (ins data_rc:$vdata, addr_rc:$vaddr, SReg_256:$srsrc,
                           DMask:$dmask, UNorm:$unorm, GLC:$glc, SLC:$slc,
                           R128A16:$r128, TFE:$tfe, LWE:$lwe, DA:$da);
  let AsmString = asm#" $vdst, $vaddr, $srsrc$dmask$unorm$glc$slc$r128$tfe$lwe$da";
}

multiclass MIMG_Atomic_Helper_m <mimg op, string asm, RegisterClass data_rc,
                                 RegisterClass addr_rc, bit enableDasm = 0> {
  let ssamp = 0, d16 = 0 in {
    def _si : MIMG_Atomic_Helper<asm, data_rc, addr_rc, "SICI", enableDasm>,
              SIMCInstr<NAME, SIEncodingFamily.SI>,
              MIMGe<op.SI> {
      let AssemblerPredicates = [isSICI];
      let DisableDecoder = DisableSIDecoder;
    }

    def _vi : MIMG_Atomic_Helper<asm, data_rc, addr_rc, "VI", enableDasm>,
              SIMCInstr<NAME, SIEncodingFamily.VI>,
              MIMGe<op.VI> {
      let AssemblerPredicates = [isVI];
      let DisableDecoder = DisableVIDecoder;
      let MIMGEncoding = MIMGEncGfx8;
    }
  }
}

multiclass MIMG_Atomic_Addr_Helper_m <mimg op, string asm,
                                      RegisterClass data_rc,
                                      bit enableDasm = 0> {
  // _V* variants have different address size, but the size is not encoded.
  // So only one variant can be disassembled. V1 looks the safest to decode.
  let VAddrDwords = 1 in
  defm _V1 : MIMG_Atomic_Helper_m <op, asm, data_rc, VGPR_32, enableDasm>;
  let VAddrDwords = 2 in
  defm _V2 : MIMG_Atomic_Helper_m <op, asm, data_rc, VReg_64>;
  let VAddrDwords = 3 in
  defm _V3 : MIMG_Atomic_Helper_m <op, asm, data_rc, VReg_96>;
  let VAddrDwords = 4 in
  defm _V4 : MIMG_Atomic_Helper_m <op, asm, data_rc, VReg_128>;
}

multiclass MIMG_Atomic <mimg op, string asm, bit isCmpSwap = 0> { // 64-bit atomics
  def "" : MIMGBaseOpcode {
    let Atomic = 1;
    let AtomicX2 = isCmpSwap;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME) in {
    // _V* variants have different dst size, but the size is encoded implicitly,
    // using dmask and tfe. Only 32-bit variant is registered with disassembler.
    // Other variants are reconstructed by disassembler using dmask and tfe.
    let VDataDwords = !if(isCmpSwap, 2, 1) in
    defm _V1 : MIMG_Atomic_Addr_Helper_m <op, asm, !if(isCmpSwap, VReg_64, VGPR_32), 1>;
    let VDataDwords = !if(isCmpSwap, 4, 2) in
    defm _V2 : MIMG_Atomic_Addr_Helper_m <op, asm, !if(isCmpSwap, VReg_128, VReg_64)>;
  }
}

class MIMG_Sampler_Helper <bits<7> op, string asm, RegisterClass dst_rc,
                           RegisterClass src_rc, string dns="">
  : MIMG <(outs dst_rc:$vdata), dns>,
    MIMGe<op> {
  let d16 = !if(BaseOpcode.HasD16, ?, 0);

  let InOperandList = !con((ins src_rc:$vaddr, SReg_256:$srsrc, SReg_128:$ssamp,
                                DMask:$dmask, UNorm:$unorm, GLC:$glc, SLC:$slc,
                                R128A16:$r128, TFE:$tfe, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc, $ssamp$dmask$unorm$glc$slc$r128$tfe$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMGAddrSize<int dw, bit enable_disasm> {
  int NumWords = dw;

  RegisterClass RegClass = !if(!le(NumWords, 0), ?,
                           !if(!eq(NumWords, 1), VGPR_32,
                           !if(!eq(NumWords, 2), VReg_64,
                           !if(!eq(NumWords, 3), VReg_96,
                           !if(!eq(NumWords, 4), VReg_128,
                           !if(!le(NumWords, 8), VReg_256,
                           !if(!le(NumWords, 16), VReg_512, ?)))))));

  // Whether the instruction variant with this vaddr size should be enabled for
  // the auto-generated disassembler.
  bit Disassemble = enable_disasm;
}

// Return whether a value inside the range [min, max] (endpoints inclusive)
// is in the given list.
class isRangeInList<int min, int max, list<int> lst> {
  bit ret = !foldl(0, lst, lhs, y, !or(lhs, !and(!le(min, y), !le(y, max))));
}

class MIMGAddrSizes_tmp<list<MIMGAddrSize> lst, int min> {
  list<MIMGAddrSize> List = lst;
  int Min = min;
}

class MIMG_Sampler_AddrSizes<AMDGPUSampleVariant sample> {
  // List of all possible numbers of address words, taking all combinations of
  // A16 and image dimension into account (note: no MSAA, since this is for
  // sample/gather ops).
  list<int> AllNumAddrWords =
    !foreach(dw, !if(sample.Gradients,
                     !if(!eq(sample.LodOrClamp, ""),
                         [2, 3, 4, 5, 6, 7, 9],
                         [2, 3, 4, 5, 7, 8, 10]),
                     !if(!eq(sample.LodOrClamp, ""),
                         [1, 2, 3],
                         [1, 2, 3, 4])),
             !add(dw, !size(sample.ExtraAddrArgs)));

  // Generate machine instructions based on possible register classes for the
  // required numbers of address words. The disassembler defaults to the
  // smallest register class.
  list<MIMGAddrSize> MachineInstrs =
    !foldl(MIMGAddrSizes_tmp<[], 0>, [1, 2, 3, 4, 8, 16], lhs, dw,
           !if(isRangeInList<lhs.Min, dw, AllNumAddrWords>.ret,
               MIMGAddrSizes_tmp<
                  !listconcat(lhs.List, [MIMGAddrSize<dw, !empty(lhs.List)>]),
                  !if(!eq(dw, 3), 3, !add(dw, 1))>, // we still need _V4 for codegen w/ 3 dwords
               lhs)).List;
}

multiclass MIMG_Sampler_Src_Helper <bits<7> op, string asm,
                                    AMDGPUSampleVariant sample, RegisterClass dst_rc,
                                    bit enableDisasm = 0> {
  foreach addr = MIMG_Sampler_AddrSizes<sample>.MachineInstrs in {
    let VAddrDwords = addr.NumWords in
    def _V # addr.NumWords
      : MIMG_Sampler_Helper <op, asm, dst_rc, addr.RegClass,
                             !if(!and(enableDisasm, addr.Disassemble), "AMDGPU", "")>;
  }
}

class MIMG_Sampler_BaseOpcode<AMDGPUSampleVariant sample>
  : MIMGBaseOpcode {
  let Sampler = 1;
  let NumExtraArgs = !size(sample.ExtraAddrArgs);
  let Gradients = sample.Gradients;
  let LodOrClampOrMip = !ne(sample.LodOrClamp, "");
}

multiclass MIMG_Sampler <bits<7> op, AMDGPUSampleVariant sample, bit wqm = 0,
                         bit isGetLod = 0,
                         string asm = "image_sample"#sample.LowerCaseMod> {
  def "" : MIMG_Sampler_BaseOpcode<sample> {
    let HasD16 = !if(isGetLod, 0, 1);
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME), WQM = wqm,
      mayLoad = !if(isGetLod, 0, 1) in {
    let VDataDwords = 1 in
    defm _V1 : MIMG_Sampler_Src_Helper<op, asm, sample, VGPR_32, 1>;
    let VDataDwords = 2 in
    defm _V2 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_64>;
    let VDataDwords = 3 in
    defm _V3 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_96>;
    let VDataDwords = 4 in
    defm _V4 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_128>;
    let VDataDwords = 8 in
    defm _V8 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_256>;
  }
}

multiclass MIMG_Sampler_WQM <bits<7> op, AMDGPUSampleVariant sample>
    : MIMG_Sampler<op, sample, 1>;

multiclass MIMG_Gather <bits<7> op, AMDGPUSampleVariant sample, bit wqm = 0,
                        string asm = "image_gather4"#sample.LowerCaseMod> {
  def "" : MIMG_Sampler_BaseOpcode<sample> {
    let HasD16 = 1;
    let Gather4 = 1;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME), WQM = wqm,
      Gather4 = 1, hasPostISelHook = 0 in {
    let VDataDwords = 2 in
    defm _V2 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_64>; /* for packed D16 only */
    let VDataDwords = 4 in
    defm _V4 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_128, 1>;
    let VDataDwords = 8 in
    defm _V8 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_256>;
  }
}

multiclass MIMG_Gather_WQM <bits<7> op, AMDGPUSampleVariant sample>
    : MIMG_Gather<op, sample, 1>;

//===----------------------------------------------------------------------===//
// MIMG Instructions
//===----------------------------------------------------------------------===//
defm IMAGE_LOAD : MIMG_NoSampler <0x00000000, "image_load", 1>;
defm IMAGE_LOAD_MIP : MIMG_NoSampler <0x00000001, "image_load_mip", 1, 1>;
defm IMAGE_LOAD_PCK : MIMG_NoSampler <0x00000002, "image_load_pck", 0>;
defm IMAGE_LOAD_PCK_SGN : MIMG_NoSampler <0x00000003, "image_load_pck_sgn", 0>;
defm IMAGE_LOAD_MIP_PCK : MIMG_NoSampler <0x00000004, "image_load_mip_pck", 0, 1>;
defm IMAGE_LOAD_MIP_PCK_SGN : MIMG_NoSampler <0x00000005, "image_load_mip_pck_sgn", 0, 1>;
defm IMAGE_STORE : MIMG_Store <0x00000008, "image_store", 1>;
defm IMAGE_STORE_MIP : MIMG_Store <0x00000009, "image_store_mip", 1, 1>;
defm IMAGE_STORE_PCK : MIMG_Store <0x0000000a, "image_store_pck", 0>;
defm IMAGE_STORE_MIP_PCK : MIMG_Store <0x0000000b, "image_store_mip_pck", 0, 1>;

defm IMAGE_GET_RESINFO : MIMG_NoSampler <0x0000000e, "image_get_resinfo", 0, 1, 1>;

defm IMAGE_ATOMIC_SWAP : MIMG_Atomic <mimg<0x0f, 0x10>, "image_atomic_swap">;
defm IMAGE_ATOMIC_CMPSWAP : MIMG_Atomic <mimg<0x10, 0x11>, "image_atomic_cmpswap", 1>;
defm IMAGE_ATOMIC_ADD : MIMG_Atomic <mimg<0x11, 0x12>, "image_atomic_add">;
defm IMAGE_ATOMIC_SUB : MIMG_Atomic <mimg<0x12, 0x13>, "image_atomic_sub">;
//def IMAGE_ATOMIC_RSUB : MIMG_NoPattern_ <"image_atomic_rsub", 0x00000013>; -- not on VI
defm IMAGE_ATOMIC_SMIN : MIMG_Atomic <mimg<0x14>, "image_atomic_smin">;
defm IMAGE_ATOMIC_UMIN : MIMG_Atomic <mimg<0x15>, "image_atomic_umin">;
defm IMAGE_ATOMIC_SMAX : MIMG_Atomic <mimg<0x16>, "image_atomic_smax">;
defm IMAGE_ATOMIC_UMAX : MIMG_Atomic <mimg<0x17>, "image_atomic_umax">;
defm IMAGE_ATOMIC_AND : MIMG_Atomic <mimg<0x18>, "image_atomic_and">;
defm IMAGE_ATOMIC_OR : MIMG_Atomic <mimg<0x19>, "image_atomic_or">;
defm IMAGE_ATOMIC_XOR : MIMG_Atomic <mimg<0x1a>, "image_atomic_xor">;
defm IMAGE_ATOMIC_INC : MIMG_Atomic <mimg<0x1b>, "image_atomic_inc">;
defm IMAGE_ATOMIC_DEC : MIMG_Atomic <mimg<0x1c>, "image_atomic_dec">;
//def IMAGE_ATOMIC_FCMPSWAP : MIMG_NoPattern_ <"image_atomic_fcmpswap", 0x0000001d, 1>; -- not on VI
//def IMAGE_ATOMIC_FMIN : MIMG_NoPattern_ <"image_atomic_fmin", 0x0000001e>; -- not on VI
//def IMAGE_ATOMIC_FMAX : MIMG_NoPattern_ <"image_atomic_fmax", 0x0000001f>; -- not on VI
defm IMAGE_SAMPLE           : MIMG_Sampler_WQM <0x00000020, AMDGPUSample>;
defm IMAGE_SAMPLE_CL        : MIMG_Sampler_WQM <0x00000021, AMDGPUSample_cl>;
defm IMAGE_SAMPLE_D         : MIMG_Sampler <0x00000022, AMDGPUSample_d>;
defm IMAGE_SAMPLE_D_CL      : MIMG_Sampler <0x00000023, AMDGPUSample_d_cl>;
defm IMAGE_SAMPLE_L         : MIMG_Sampler <0x00000024, AMDGPUSample_l>;
defm IMAGE_SAMPLE_B         : MIMG_Sampler_WQM <0x00000025, AMDGPUSample_b>;
defm IMAGE_SAMPLE_B_CL      : MIMG_Sampler_WQM <0x00000026, AMDGPUSample_b_cl>;
defm IMAGE_SAMPLE_LZ        : MIMG_Sampler <0x00000027, AMDGPUSample_lz>;
defm IMAGE_SAMPLE_C         : MIMG_Sampler_WQM <0x00000028, AMDGPUSample_c>;
defm IMAGE_SAMPLE_C_CL      : MIMG_Sampler_WQM <0x00000029, AMDGPUSample_c_cl>;
defm IMAGE_SAMPLE_C_D       : MIMG_Sampler <0x0000002a, AMDGPUSample_c_d>;
defm IMAGE_SAMPLE_C_D_CL    : MIMG_Sampler <0x0000002b, AMDGPUSample_c_d_cl>;
defm IMAGE_SAMPLE_C_L       : MIMG_Sampler <0x0000002c, AMDGPUSample_c_l>;
defm IMAGE_SAMPLE_C_B       : MIMG_Sampler_WQM <0x0000002d, AMDGPUSample_c_b>;
defm IMAGE_SAMPLE_C_B_CL    : MIMG_Sampler_WQM <0x0000002e, AMDGPUSample_c_b_cl>;
defm IMAGE_SAMPLE_C_LZ      : MIMG_Sampler <0x0000002f, AMDGPUSample_c_lz>;
defm IMAGE_SAMPLE_O         : MIMG_Sampler_WQM <0x00000030, AMDGPUSample_o>;
defm IMAGE_SAMPLE_CL_O      : MIMG_Sampler_WQM <0x00000031, AMDGPUSample_cl_o>;
defm IMAGE_SAMPLE_D_O       : MIMG_Sampler <0x00000032, AMDGPUSample_d_o>;
defm IMAGE_SAMPLE_D_CL_O    : MIMG_Sampler <0x00000033, AMDGPUSample_d_cl_o>;
defm IMAGE_SAMPLE_L_O       : MIMG_Sampler <0x00000034, AMDGPUSample_l_o>;
defm IMAGE_SAMPLE_B_O       : MIMG_Sampler_WQM <0x00000035, AMDGPUSample_b_o>;
defm IMAGE_SAMPLE_B_CL_O    : MIMG_Sampler_WQM <0x00000036, AMDGPUSample_b_cl_o>;
defm IMAGE_SAMPLE_LZ_O      : MIMG_Sampler <0x00000037, AMDGPUSample_lz_o>;
defm IMAGE_SAMPLE_C_O       : MIMG_Sampler_WQM <0x00000038, AMDGPUSample_c_o>;
defm IMAGE_SAMPLE_C_CL_O    : MIMG_Sampler_WQM <0x00000039, AMDGPUSample_c_cl_o>;
defm IMAGE_SAMPLE_C_D_O     : MIMG_Sampler <0x0000003a, AMDGPUSample_c_d_o>;
defm IMAGE_SAMPLE_C_D_CL_O  : MIMG_Sampler <0x0000003b, AMDGPUSample_c_d_cl_o>;
defm IMAGE_SAMPLE_C_L_O     : MIMG_Sampler <0x0000003c, AMDGPUSample_c_l_o>;
defm IMAGE_SAMPLE_C_B_CL_O  : MIMG_Sampler_WQM <0x0000003e, AMDGPUSample_c_b_cl_o>;
defm IMAGE_SAMPLE_C_B_O     : MIMG_Sampler_WQM <0x0000003d, AMDGPUSample_c_b_o>;
defm IMAGE_SAMPLE_C_LZ_O    : MIMG_Sampler <0x0000003f, AMDGPUSample_c_lz_o>;
defm IMAGE_GATHER4          : MIMG_Gather_WQM <0x00000040, AMDGPUSample>;
defm IMAGE_GATHER4_CL       : MIMG_Gather_WQM <0x00000041, AMDGPUSample_cl>;
defm IMAGE_GATHER4_L        : MIMG_Gather <0x00000044, AMDGPUSample_l>;
defm IMAGE_GATHER4_B        : MIMG_Gather_WQM <0x00000045, AMDGPUSample_b>;
defm IMAGE_GATHER4_B_CL     : MIMG_Gather_WQM <0x00000046, AMDGPUSample_b_cl>;
defm IMAGE_GATHER4_LZ       : MIMG_Gather <0x00000047, AMDGPUSample_lz>;
defm IMAGE_GATHER4_C        : MIMG_Gather_WQM <0x00000048, AMDGPUSample_c>;
defm IMAGE_GATHER4_C_CL     : MIMG_Gather_WQM <0x00000049, AMDGPUSample_c_cl>;
defm IMAGE_GATHER4_C_L      : MIMG_Gather <0x0000004c, AMDGPUSample_c_l>;
defm IMAGE_GATHER4_C_B      : MIMG_Gather_WQM <0x0000004d, AMDGPUSample_c_b>;
defm IMAGE_GATHER4_C_B_CL   : MIMG_Gather_WQM <0x0000004e, AMDGPUSample_c_b_cl>;
defm IMAGE_GATHER4_C_LZ     : MIMG_Gather <0x0000004f, AMDGPUSample_c_lz>;
defm IMAGE_GATHER4_O        : MIMG_Gather_WQM <0x00000050, AMDGPUSample_o>;
defm IMAGE_GATHER4_CL_O     : MIMG_Gather_WQM <0x00000051, AMDGPUSample_cl_o>;
defm IMAGE_GATHER4_L_O      : MIMG_Gather <0x00000054, AMDGPUSample_l_o>;
defm IMAGE_GATHER4_B_O      : MIMG_Gather_WQM <0x00000055, AMDGPUSample_b_o>;
defm IMAGE_GATHER4_B_CL_O   : MIMG_Gather <0x00000056, AMDGPUSample_b_cl_o>;
defm IMAGE_GATHER4_LZ_O     : MIMG_Gather <0x00000057, AMDGPUSample_lz_o>;
defm IMAGE_GATHER4_C_O      : MIMG_Gather_WQM <0x00000058, AMDGPUSample_c_o>;
defm IMAGE_GATHER4_C_CL_O   : MIMG_Gather_WQM <0x00000059, AMDGPUSample_c_cl_o>;
defm IMAGE_GATHER4_C_L_O    : MIMG_Gather <0x0000005c, AMDGPUSample_c_l_o>;
defm IMAGE_GATHER4_C_B_O    : MIMG_Gather_WQM <0x0000005d, AMDGPUSample_c_b_o>;
defm IMAGE_GATHER4_C_B_CL_O : MIMG_Gather_WQM <0x0000005e, AMDGPUSample_c_b_cl_o>;
defm IMAGE_GATHER4_C_LZ_O   : MIMG_Gather <0x0000005f, AMDGPUSample_c_lz_o>;

defm IMAGE_GET_LOD          : MIMG_Sampler <0x00000060, AMDGPUSample, 1, 1, "image_get_lod">;

defm IMAGE_SAMPLE_CD        : MIMG_Sampler <0x00000068, AMDGPUSample_cd>;
defm IMAGE_SAMPLE_CD_CL     : MIMG_Sampler <0x00000069, AMDGPUSample_cd_cl>;
defm IMAGE_SAMPLE_C_CD      : MIMG_Sampler <0x0000006a, AMDGPUSample_c_cd>;
defm IMAGE_SAMPLE_C_CD_CL   : MIMG_Sampler <0x0000006b, AMDGPUSample_c_cd_cl>;
defm IMAGE_SAMPLE_CD_O      : MIMG_Sampler <0x0000006c, AMDGPUSample_cd_o>;
defm IMAGE_SAMPLE_CD_CL_O   : MIMG_Sampler <0x0000006d, AMDGPUSample_cd_cl_o>;
defm IMAGE_SAMPLE_C_CD_O    : MIMG_Sampler <0x0000006e, AMDGPUSample_c_cd_o>;
defm IMAGE_SAMPLE_C_CD_CL_O : MIMG_Sampler <0x0000006f, AMDGPUSample_c_cd_cl_o>;
//def IMAGE_RSRC256 : MIMG_NoPattern_RSRC256 <"image_rsrc256", 0x0000007e>;
//def IMAGE_SAMPLER : MIMG_NoPattern_ <"image_sampler", 0x0000007f>;

/********** ========================================= **********/
/********** Table of dimension-aware image intrinsics **********/
/********** ========================================= **********/

class ImageDimIntrinsicInfo<AMDGPUImageDimIntrinsic I> {
  Intrinsic Intr = I;
  MIMGBaseOpcode BaseOpcode = !cast<MIMGBaseOpcode>(!strconcat("IMAGE_", I.P.OpMod));
  AMDGPUDimProps Dim = I.P.Dim;
}

def ImageDimIntrinsicTable : GenericTable {
  let FilterClass = "ImageDimIntrinsicInfo";
  let Fields = ["Intr", "BaseOpcode", "Dim"];
  GenericEnum TypeOf_BaseOpcode = MIMGBaseOpcode;
  GenericEnum TypeOf_Dim = MIMGDim;

  let PrimaryKey = ["Intr"];
  let PrimaryKeyName = "getImageDimIntrinsicInfo";
  let PrimaryKeyEarlyOut = 1;
}

foreach intr = !listconcat(AMDGPUImageDimIntrinsics,
                           AMDGPUImageDimAtomicIntrinsics) in {
  def : ImageDimIntrinsicInfo<intr>;
}

// L to LZ Optimization Mapping
def : MIMGLZMapping<IMAGE_SAMPLE_L, IMAGE_SAMPLE_LZ>;
def : MIMGLZMapping<IMAGE_SAMPLE_C_L, IMAGE_SAMPLE_C_LZ>;
def : MIMGLZMapping<IMAGE_SAMPLE_L_O, IMAGE_SAMPLE_LZ_O>;
def : MIMGLZMapping<IMAGE_SAMPLE_C_L_O, IMAGE_SAMPLE_C_LZ_O>;
def : MIMGLZMapping<IMAGE_GATHER4_L, IMAGE_GATHER4_LZ>;
def : MIMGLZMapping<IMAGE_GATHER4_C_L, IMAGE_GATHER4_C_LZ>;
def : MIMGLZMapping<IMAGE_GATHER4_L_O, IMAGE_GATHER4_LZ_O>;
def : MIMGLZMapping<IMAGE_GATHER4_C_L_O, IMAGE_GATHER4_C_LZ_O>;
