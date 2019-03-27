//===- IntrinsicsAMDGPU.td - Defines AMDGPU intrinsics -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the R600-specific intrinsics.
//
//===----------------------------------------------------------------------===//

class AMDGPUReadPreloadRegisterIntrinsic
  : Intrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable]>;

class AMDGPUReadPreloadRegisterIntrinsicNamed<string name>
  : Intrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable]>, GCCBuiltin<name>;

// Used to tag image and resource intrinsics with information used to generate
// mem operands.
class AMDGPURsrcIntrinsic<int rsrcarg, bit isimage = 0> {
  int RsrcArg = rsrcarg;
  bit IsImage = isimage;
}

let TargetPrefix = "r600" in {

multiclass AMDGPUReadPreloadRegisterIntrinsic_xyz {
  def _x : AMDGPUReadPreloadRegisterIntrinsic;
  def _y : AMDGPUReadPreloadRegisterIntrinsic;
  def _z : AMDGPUReadPreloadRegisterIntrinsic;
}

multiclass AMDGPUReadPreloadRegisterIntrinsic_xyz_named<string prefix> {
  def _x : AMDGPUReadPreloadRegisterIntrinsicNamed<!strconcat(prefix, "_x")>;
  def _y : AMDGPUReadPreloadRegisterIntrinsicNamed<!strconcat(prefix, "_y")>;
  def _z : AMDGPUReadPreloadRegisterIntrinsicNamed<!strconcat(prefix, "_z")>;
}

defm int_r600_read_global_size : AMDGPUReadPreloadRegisterIntrinsic_xyz_named
                                 <"__builtin_r600_read_global_size">;
defm int_r600_read_ngroups : AMDGPUReadPreloadRegisterIntrinsic_xyz_named
                             <"__builtin_r600_read_ngroups">;
defm int_r600_read_tgid : AMDGPUReadPreloadRegisterIntrinsic_xyz_named
                          <"__builtin_r600_read_tgid">;

defm int_r600_read_local_size : AMDGPUReadPreloadRegisterIntrinsic_xyz;
defm int_r600_read_tidig : AMDGPUReadPreloadRegisterIntrinsic_xyz;

def int_r600_group_barrier : GCCBuiltin<"__builtin_r600_group_barrier">,
  Intrinsic<[], [], [IntrConvergent]>;

// AS 7 is PARAM_I_ADDRESS, used for kernel arguments
def int_r600_implicitarg_ptr :
  GCCBuiltin<"__builtin_r600_implicitarg_ptr">,
  Intrinsic<[LLVMQualPointerType<llvm_i8_ty, 7>], [],
  [IntrNoMem, IntrSpeculatable]>;

def int_r600_rat_store_typed :
  // 1st parameter: Data
  // 2nd parameter: Index
  // 3rd parameter: Constant RAT ID
  Intrinsic<[], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty], []>,
  GCCBuiltin<"__builtin_r600_rat_store_typed">;

def int_r600_recipsqrt_ieee :  Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_r600_recipsqrt_clamped : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_r600_cube : Intrinsic<
  [llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem, IntrSpeculatable]
>;

def int_r600_store_stream_output : Intrinsic<
  [], [llvm_v4f32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []
>;

class TextureIntrinsicFloatInput : Intrinsic<[llvm_v4f32_ty], [
  llvm_v4f32_ty, // Coord
  llvm_i32_ty,   // offset_x
  llvm_i32_ty,   // offset_y,
  llvm_i32_ty,   // offset_z,
  llvm_i32_ty,   // resource_id
  llvm_i32_ty,   // samplerid
  llvm_i32_ty,   // coord_type_x
  llvm_i32_ty,   // coord_type_y
  llvm_i32_ty,   // coord_type_z
  llvm_i32_ty],  // coord_type_w
  [IntrNoMem]
>;

class TextureIntrinsicInt32Input : Intrinsic<[llvm_v4i32_ty], [
    llvm_v4i32_ty, // Coord
    llvm_i32_ty,   // offset_x
    llvm_i32_ty,   // offset_y,
    llvm_i32_ty,   // offset_z,
    llvm_i32_ty,   // resource_id
    llvm_i32_ty,   // samplerid
    llvm_i32_ty,   // coord_type_x
    llvm_i32_ty,   // coord_type_y
    llvm_i32_ty,   // coord_type_z
    llvm_i32_ty],  // coord_type_w
    [IntrNoMem]
>;

def int_r600_store_swizzle :
  Intrinsic<[], [llvm_v4f32_ty, llvm_i32_ty, llvm_i32_ty], []
>;

def int_r600_tex : TextureIntrinsicFloatInput;
def int_r600_texc : TextureIntrinsicFloatInput;
def int_r600_txl : TextureIntrinsicFloatInput;
def int_r600_txlc : TextureIntrinsicFloatInput;
def int_r600_txb : TextureIntrinsicFloatInput;
def int_r600_txbc : TextureIntrinsicFloatInput;
def int_r600_txf : TextureIntrinsicInt32Input;
def int_r600_txq : TextureIntrinsicInt32Input;
def int_r600_ddx : TextureIntrinsicFloatInput;
def int_r600_ddy : TextureIntrinsicFloatInput;

def int_r600_dot4 : Intrinsic<[llvm_float_ty],
  [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem, IntrSpeculatable]
>;

def int_r600_kill : Intrinsic<[], [llvm_float_ty], []>;

} // End TargetPrefix = "r600"

let TargetPrefix = "amdgcn" in {

//===----------------------------------------------------------------------===//
// ABI Special Intrinsics
//===----------------------------------------------------------------------===//

defm int_amdgcn_workitem_id : AMDGPUReadPreloadRegisterIntrinsic_xyz;
defm int_amdgcn_workgroup_id : AMDGPUReadPreloadRegisterIntrinsic_xyz_named
                               <"__builtin_amdgcn_workgroup_id">;

def int_amdgcn_dispatch_ptr :
  GCCBuiltin<"__builtin_amdgcn_dispatch_ptr">,
  Intrinsic<[LLVMQualPointerType<llvm_i8_ty, 4>], [],
  [IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_queue_ptr :
  GCCBuiltin<"__builtin_amdgcn_queue_ptr">,
  Intrinsic<[LLVMQualPointerType<llvm_i8_ty, 4>], [],
  [IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_kernarg_segment_ptr :
  GCCBuiltin<"__builtin_amdgcn_kernarg_segment_ptr">,
  Intrinsic<[LLVMQualPointerType<llvm_i8_ty, 4>], [],
  [IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_implicitarg_ptr :
  GCCBuiltin<"__builtin_amdgcn_implicitarg_ptr">,
  Intrinsic<[LLVMQualPointerType<llvm_i8_ty, 4>], [],
  [IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_groupstaticsize :
  GCCBuiltin<"__builtin_amdgcn_groupstaticsize">,
  Intrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_dispatch_id :
  GCCBuiltin<"__builtin_amdgcn_dispatch_id">,
  Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_implicit_buffer_ptr :
  GCCBuiltin<"__builtin_amdgcn_implicit_buffer_ptr">,
  Intrinsic<[LLVMQualPointerType<llvm_i8_ty, 4>], [],
  [IntrNoMem, IntrSpeculatable]>;

// Set EXEC to the 64-bit value given.
// This is always moved to the beginning of the basic block.
def int_amdgcn_init_exec : Intrinsic<[],
  [llvm_i64_ty],      // 64-bit literal constant
  [IntrConvergent]>;

// Set EXEC according to a thread count packed in an SGPR input:
//    thread_count = (input >> bitoffset) & 0x7f;
// This is always moved to the beginning of the basic block.
def int_amdgcn_init_exec_from_input : Intrinsic<[],
  [llvm_i32_ty,       // 32-bit SGPR input
   llvm_i32_ty],      // bit offset of the thread count
  [IntrConvergent]>;


//===----------------------------------------------------------------------===//
// Instruction Intrinsics
//===----------------------------------------------------------------------===//

// The first parameter is s_sendmsg immediate (i16),
// the second one is copied to m0
def int_amdgcn_s_sendmsg : GCCBuiltin<"__builtin_amdgcn_s_sendmsg">,
  Intrinsic <[], [llvm_i32_ty, llvm_i32_ty], []>;
def int_amdgcn_s_sendmsghalt : GCCBuiltin<"__builtin_amdgcn_s_sendmsghalt">,
  Intrinsic <[], [llvm_i32_ty, llvm_i32_ty], []>;

def int_amdgcn_s_barrier : GCCBuiltin<"__builtin_amdgcn_s_barrier">,
  Intrinsic<[], [], [IntrConvergent]>;

def int_amdgcn_wave_barrier : GCCBuiltin<"__builtin_amdgcn_wave_barrier">,
  Intrinsic<[], [], [IntrConvergent]>;

def int_amdgcn_s_waitcnt : GCCBuiltin<"__builtin_amdgcn_s_waitcnt">,
  Intrinsic<[], [llvm_i32_ty], []>;

def int_amdgcn_div_scale : Intrinsic<
  // 1st parameter: Numerator
  // 2nd parameter: Denominator
  // 3rd parameter: Constant to select select between first and
  //                second. (0 = first, 1 = second).
  [llvm_anyfloat_ty, llvm_i1_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i1_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_div_fmas : Intrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>, llvm_i1_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_div_fixup : Intrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_trig_preop : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sin : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cos : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_log_clamp : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_fmul_legacy : GCCBuiltin<"__builtin_amdgcn_fmul_legacy">,
  Intrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_rcp : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_rcp_legacy : GCCBuiltin<"__builtin_amdgcn_rcp_legacy">,
  Intrinsic<[llvm_float_ty], [llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_rsq :  Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_rsq_legacy :  GCCBuiltin<"__builtin_amdgcn_rsq_legacy">,
  Intrinsic<
  [llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_rsq_clamp : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_ldexp : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_frexp_mant : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_frexp_exp : Intrinsic<
  [llvm_anyint_ty], [llvm_anyfloat_ty], [IntrNoMem, IntrSpeculatable]
>;

// v_fract is buggy on SI/CI. It mishandles infinities, may return 1.0
// and always uses rtz, so is not suitable for implementing the OpenCL
// fract function. It should be ok on VI.
def int_amdgcn_fract : Intrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pkrtz : Intrinsic<
  [llvm_v2f16_ty], [llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pknorm_i16 : Intrinsic<
  [llvm_v2i16_ty], [llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pknorm_u16 : Intrinsic<
  [llvm_v2i16_ty], [llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pk_i16 : Intrinsic<
  [llvm_v2i16_ty], [llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pk_u16 : Intrinsic<
  [llvm_v2i16_ty], [llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_class : Intrinsic<
  [llvm_i1_ty], [llvm_anyfloat_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_fmed3 : GCCBuiltin<"__builtin_amdgcn_fmed3">,
  Intrinsic<[llvm_anyfloat_ty],
    [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cubeid : GCCBuiltin<"__builtin_amdgcn_cubeid">,
  Intrinsic<[llvm_float_ty],
    [llvm_float_ty, llvm_float_ty, llvm_float_ty],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cubema : GCCBuiltin<"__builtin_amdgcn_cubema">,
  Intrinsic<[llvm_float_ty],
  [llvm_float_ty, llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cubesc : GCCBuiltin<"__builtin_amdgcn_cubesc">,
  Intrinsic<[llvm_float_ty],
    [llvm_float_ty, llvm_float_ty, llvm_float_ty],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cubetc : GCCBuiltin<"__builtin_amdgcn_cubetc">,
  Intrinsic<[llvm_float_ty],
    [llvm_float_ty, llvm_float_ty, llvm_float_ty],
    [IntrNoMem, IntrSpeculatable]
>;

// v_ffbh_i32, as opposed to v_ffbh_u32. For v_ffbh_u32, llvm.ctlz
// should be used.
def int_amdgcn_sffbh :
  Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable]
>;

// v_mad_f32|f16/v_mac_f32|f16, selected regardless of denorm support.
def int_amdgcn_fmad_ftz :
  Intrinsic<[llvm_anyfloat_ty],
            [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
            [IntrNoMem, IntrSpeculatable]
>;

// Fields should mirror atomicrmw
class AMDGPUAtomicIncIntrin : Intrinsic<[llvm_anyint_ty],
  [llvm_anyptr_ty,
  LLVMMatchType<0>,
  llvm_i32_ty, // ordering
  llvm_i32_ty, // scope
  llvm_i1_ty], // isVolatile
  [IntrArgMemOnly, NoCapture<0>], "",
  [SDNPMemOperand]
>;

def int_amdgcn_atomic_inc : AMDGPUAtomicIncIntrin;
def int_amdgcn_atomic_dec : AMDGPUAtomicIncIntrin;

class AMDGPULDSF32Intrin<string clang_builtin> :
  GCCBuiltin<clang_builtin>,
  Intrinsic<[llvm_float_ty],
    [LLVMQualPointerType<llvm_float_ty, 3>,
    llvm_float_ty,
    llvm_i32_ty, // ordering
    llvm_i32_ty, // scope
    llvm_i1_ty], // isVolatile
    [IntrArgMemOnly, NoCapture<0>]
>;

class AMDGPUDSOrderedIntrinsic : Intrinsic<
  [llvm_i32_ty],
  // M0 = {hi16:address, lo16:waveID}. Allow passing M0 as a pointer, so that
  // the bit packing can be optimized at the IR level.
  [LLVMQualPointerType<llvm_i32_ty, 2>, // IntToPtr(M0)
   llvm_i32_ty, // value to add or swap
   llvm_i32_ty, // ordering
   llvm_i32_ty, // scope
   llvm_i1_ty,  // isVolatile
   llvm_i32_ty, // ordered count index (OA index), also added to the address
   llvm_i1_ty,  // wave release, usually set to 1
   llvm_i1_ty], // wave done, set to 1 for the last ordered instruction
  [NoCapture<0>]
>;

def int_amdgcn_ds_ordered_add : AMDGPUDSOrderedIntrinsic;
def int_amdgcn_ds_ordered_swap : AMDGPUDSOrderedIntrinsic;

def int_amdgcn_ds_fadd : AMDGPULDSF32Intrin<"__builtin_amdgcn_ds_faddf">;
def int_amdgcn_ds_fmin : AMDGPULDSF32Intrin<"__builtin_amdgcn_ds_fminf">;
def int_amdgcn_ds_fmax : AMDGPULDSF32Intrin<"__builtin_amdgcn_ds_fmaxf">;

} // TargetPrefix = "amdgcn"

// New-style image intrinsics

//////////////////////////////////////////////////////////////////////////
// Dimension-aware image intrinsics framework
//////////////////////////////////////////////////////////////////////////

// Helper class to represent (type, name) combinations of arguments. The
// argument names are explanatory and used as DAG operand names for codegen
// pattern matching.
class AMDGPUArg<LLVMType ty, string name> {
  LLVMType Type = ty;
  string Name = name;
}

// Return [AMDGPUArg<basety, names[0]>, AMDGPUArg<LLVMMatchType<0>, names[1]>, ...]
class makeArgList<list<string> names, LLVMType basety> {
  list<AMDGPUArg> ret =
    !listconcat([AMDGPUArg<basety, names[0]>],
                !foreach(name, !tail(names), AMDGPUArg<LLVMMatchType<0>, name>));
}

// Return arglist, with LLVMMatchType's references shifted by 'shift'.
class arglistmatchshift<list<AMDGPUArg> arglist, int shift> {
  list<AMDGPUArg> ret =
    !foreach(arg, arglist,
             !if(!isa<LLVMMatchType>(arg.Type),
                 AMDGPUArg<LLVMMatchType<!add(!cast<LLVMMatchType>(arg.Type).Number, shift)>,
                           arg.Name>,
                 arg));
}

// Return the concatenation of the given arglists. LLVMMatchType's are adjusted
// accordingly, and shifted by an additional 'shift'.
class arglistconcat<list<list<AMDGPUArg>> arglists, int shift = 0> {
  list<AMDGPUArg> ret =
    !foldl([]<AMDGPUArg>, arglists, lhs, rhs,
           !listconcat(
             lhs,
             arglistmatchshift<rhs,
                               !add(shift, !foldl(0, lhs, a, b,
                                                  !add(a, b.Type.isAny)))>.ret));
}

// Represent texture/image types / dimensionality.
class AMDGPUDimProps<string name, list<string> coord_names, list<string> slice_names> {
  AMDGPUDimProps Dim = !cast<AMDGPUDimProps>(NAME);
  string Name = name; // e.g. "2darraymsaa"
  bit DA = 0; // DA bit in MIMG encoding

  list<AMDGPUArg> CoordSliceArgs =
    makeArgList<!listconcat(coord_names, slice_names), llvm_anyfloat_ty>.ret;
  list<AMDGPUArg> CoordSliceIntArgs =
    makeArgList<!listconcat(coord_names, slice_names), llvm_anyint_ty>.ret;
  list<AMDGPUArg> GradientArgs =
    makeArgList<!listconcat(!foreach(name, coord_names, "d" # name # "dh"),
                            !foreach(name, coord_names, "d" # name # "dv")),
                llvm_anyfloat_ty>.ret;

  bits<8> NumCoords = !size(CoordSliceArgs);
  bits<8> NumGradients = !size(GradientArgs);
}

def AMDGPUDim1D : AMDGPUDimProps<"1d", ["s"], []>;
def AMDGPUDim2D : AMDGPUDimProps<"2d", ["s", "t"], []>;
def AMDGPUDim3D : AMDGPUDimProps<"3d", ["s", "t", "r"], []>;
let DA = 1 in {
  def AMDGPUDimCube : AMDGPUDimProps<"cube", ["s", "t"], ["face"]>;
  def AMDGPUDim1DArray : AMDGPUDimProps<"1darray", ["s"], ["slice"]>;
  def AMDGPUDim2DArray : AMDGPUDimProps<"2darray", ["s", "t"], ["slice"]>;
}
def AMDGPUDim2DMsaa : AMDGPUDimProps<"2dmsaa", ["s", "t"], ["fragid"]>;
let DA = 1 in {
  def AMDGPUDim2DArrayMsaa : AMDGPUDimProps<"2darraymsaa", ["s", "t"], ["slice", "fragid"]>;
}

def AMDGPUDims {
  list<AMDGPUDimProps> NoMsaa = [AMDGPUDim1D, AMDGPUDim2D, AMDGPUDim3D,
                                 AMDGPUDimCube, AMDGPUDim1DArray,
                                 AMDGPUDim2DArray];
  list<AMDGPUDimProps> Msaa = [AMDGPUDim2DMsaa, AMDGPUDim2DArrayMsaa];
  list<AMDGPUDimProps> All = !listconcat(NoMsaa, Msaa);
}

// Represent sample variants, i.e. _C, _O, _B, ... and combinations thereof.
class AMDGPUSampleVariant<string ucmod, string lcmod, list<AMDGPUArg> extra_addr> {
  string UpperCaseMod = ucmod;
  string LowerCaseMod = lcmod;

  // {offset} {bias} {z-compare}
  list<AMDGPUArg> ExtraAddrArgs = extra_addr;
  bit Gradients = 0;

  // Name of the {lod} or {clamp} argument that is appended to the coordinates,
  // if any.
  string LodOrClamp = "";
}

// AMDGPUSampleVariants: all variants supported by IMAGE_SAMPLE
// AMDGPUSampleVariantsNoGradients: variants supported by IMAGE_GATHER4
defset list<AMDGPUSampleVariant> AMDGPUSampleVariants = {
  multiclass AMDGPUSampleHelper_Offset<string ucmod, string lcmod,
                                       list<AMDGPUArg> extra_addr> {
    def NAME#lcmod : AMDGPUSampleVariant<ucmod, lcmod, extra_addr>;
    def NAME#lcmod#_o : AMDGPUSampleVariant<
        ucmod#"_O", lcmod#"_o", !listconcat([AMDGPUArg<llvm_i32_ty, "offset">], extra_addr)>;
  }

  multiclass AMDGPUSampleHelper_Compare<string ucmod, string lcmod,
                                        list<AMDGPUArg> extra_addr> {
    defm NAME : AMDGPUSampleHelper_Offset<ucmod, lcmod, extra_addr>;
    defm NAME : AMDGPUSampleHelper_Offset<
        "_C"#ucmod, "_c"#lcmod, !listconcat(extra_addr, [AMDGPUArg<llvm_float_ty, "zcompare">])>;
  }

  multiclass AMDGPUSampleHelper_Clamp<string ucmod, string lcmod,
                                      list<AMDGPUArg> extra_addr> {
    defm NAME : AMDGPUSampleHelper_Compare<ucmod, lcmod, extra_addr>;
    let LodOrClamp = "clamp" in
    defm NAME : AMDGPUSampleHelper_Compare<ucmod#"_CL", lcmod#"_cl", extra_addr>;
  }

  defset list<AMDGPUSampleVariant> AMDGPUSampleVariantsNoGradients = {
    defm AMDGPUSample : AMDGPUSampleHelper_Clamp<"", "", []>;
    defm AMDGPUSample : AMDGPUSampleHelper_Clamp<
        "_B", "_b", [AMDGPUArg<llvm_anyfloat_ty, "bias">]>;
    let LodOrClamp = "lod" in
    defm AMDGPUSample : AMDGPUSampleHelper_Compare<"_L", "_l", []>;
    defm AMDGPUSample : AMDGPUSampleHelper_Compare<"_LZ", "_lz", []>;
  }

  let Gradients = 1 in {
    defm AMDGPUSample : AMDGPUSampleHelper_Clamp<"_D", "_d", []>;
    defm AMDGPUSample : AMDGPUSampleHelper_Clamp<"_CD", "_cd", []>;
  }
}

// Helper class to capture the profile of a dimension-aware image intrinsic.
// This information is used to generate the intrinsic's type and to inform
// codegen pattern matching.
class AMDGPUDimProfile<string opmod,
                       AMDGPUDimProps dim> {
  AMDGPUDimProps Dim = dim;
  string OpMod = opmod; // the corresponding instruction is named IMAGE_OpMod

  // These are entended to be overwritten by subclasses
  bit IsSample = 0;
  bit IsAtomic = 0;
  list<LLVMType> RetTypes = [];
  list<AMDGPUArg> DataArgs = [];
  list<AMDGPUArg> ExtraAddrArgs = [];
  bit Gradients = 0;
  string LodClampMip = "";

  int NumRetAndDataAnyTypes =
    !foldl(0, !listconcat(RetTypes, !foreach(arg, DataArgs, arg.Type)), a, b,
           !add(a, b.isAny));

  list<AMDGPUArg> AddrArgs =
    arglistconcat<[ExtraAddrArgs,
                   !if(Gradients, dim.GradientArgs, []),
                   !listconcat(!if(IsSample, dim.CoordSliceArgs, dim.CoordSliceIntArgs),
                               !if(!eq(LodClampMip, ""),
                                   []<AMDGPUArg>,
                                   [AMDGPUArg<LLVMMatchType<0>, LodClampMip>]))],
                  NumRetAndDataAnyTypes>.ret;
  list<LLVMType> AddrTypes = !foreach(arg, AddrArgs, arg.Type);
  list<AMDGPUArg> AddrDefaultArgs =
    !foreach(arg, AddrArgs,
             AMDGPUArg<!if(!or(arg.Type.isAny, !isa<LLVMMatchType>(arg.Type)),
                           !if(IsSample, llvm_float_ty, llvm_i32_ty), arg.Type),
                       arg.Name>);
  list<AMDGPUArg> AddrA16Args =
    !foreach(arg, AddrArgs,
             AMDGPUArg<!if(!or(arg.Type.isAny, !isa<LLVMMatchType>(arg.Type)),
                           !if(IsSample, llvm_half_ty, llvm_i16_ty), arg.Type),
                       arg.Name>);
}

class AMDGPUDimProfileCopy<AMDGPUDimProfile base> : AMDGPUDimProfile<base.OpMod, base.Dim> {
  let IsSample = base.IsSample;
  let IsAtomic = base.IsAtomic;
  let RetTypes = base.RetTypes;
  let DataArgs = base.DataArgs;
  let ExtraAddrArgs = base.ExtraAddrArgs;
  let Gradients = base.Gradients;
  let LodClampMip = base.LodClampMip;
}

class AMDGPUDimSampleProfile<string opmod,
                             AMDGPUDimProps dim,
                             AMDGPUSampleVariant sample> : AMDGPUDimProfile<opmod, dim> {
  let IsSample = 1;
  let RetTypes = [llvm_any_ty];
  let ExtraAddrArgs = sample.ExtraAddrArgs;
  let Gradients = sample.Gradients;
  let LodClampMip = sample.LodOrClamp;
}

class AMDGPUDimNoSampleProfile<string opmod,
                               AMDGPUDimProps dim,
                               list<LLVMType> retty,
                               list<AMDGPUArg> dataargs,
                               bit Mip = 0> : AMDGPUDimProfile<opmod, dim> {
  let RetTypes = retty;
  let DataArgs = dataargs;
  let LodClampMip = !if(Mip, "mip", "");
}

class AMDGPUDimAtomicProfile<string opmod,
                             AMDGPUDimProps dim,
                             list<AMDGPUArg> dataargs> : AMDGPUDimProfile<opmod, dim> {
  let RetTypes = [llvm_anyint_ty];
  let DataArgs = dataargs;
  let IsAtomic = 1;
}

class AMDGPUDimGetResInfoProfile<AMDGPUDimProps dim> : AMDGPUDimProfile<"GET_RESINFO", dim> {
  let RetTypes = [llvm_anyfloat_ty];
  let DataArgs = [];
  let AddrArgs = [AMDGPUArg<llvm_anyint_ty, "mip">];
  let LodClampMip = "mip";
}

// All dimension-aware intrinsics are derived from this class.
class AMDGPUImageDimIntrinsic<AMDGPUDimProfile P_,
                              list<IntrinsicProperty> props,
                              list<SDNodeProperty> sdnodeprops> : Intrinsic<
    P_.RetTypes,        // vdata(VGPR) -- for load/atomic-with-return
    !listconcat(
      !foreach(arg, P_.DataArgs, arg.Type),      // vdata(VGPR) -- for store/atomic
      !if(P_.IsAtomic, [], [llvm_i32_ty]),       // dmask(imm)
      P_.AddrTypes,                              // vaddr(VGPR)
      [llvm_v8i32_ty],                           // rsrc(SGPR)
      !if(P_.IsSample, [llvm_v4i32_ty,           // samp(SGPR)
                        llvm_i1_ty], []),        // unorm(imm)
      [llvm_i32_ty,                              // texfailctrl(imm; bit 0 = tfe, bit 1 = lwe)
       llvm_i32_ty]),                            // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
      props, "", sdnodeprops>,
  AMDGPURsrcIntrinsic<!add(!size(P_.DataArgs), !size(P_.AddrTypes),
                           !if(P_.IsAtomic, 0, 1)), 1> {
  AMDGPUDimProfile P = P_;

  AMDGPUImageDimIntrinsic Intr = !cast<AMDGPUImageDimIntrinsic>(NAME);

  let TargetPrefix = "amdgcn";
}

// Marker class for intrinsics with a DMask that determines the returned
// channels.
class AMDGPUImageDMaskIntrinsic;

defset list<AMDGPUImageDimIntrinsic> AMDGPUImageDimIntrinsics = {

  //////////////////////////////////////////////////////////////////////////
  // Load and store intrinsics
  //////////////////////////////////////////////////////////////////////////
  multiclass AMDGPUImageDimIntrinsicsNoMsaa<string opmod,
                                            list<LLVMType> retty,
                                            list<AMDGPUArg> dataargs,
                                            list<IntrinsicProperty> props,
                                            list<SDNodeProperty> sdnodeprops,
                                            bit Mip = 0> {
    foreach dim = AMDGPUDims.NoMsaa in {
      def !strconcat(NAME, "_", dim.Name)
        : AMDGPUImageDimIntrinsic<
            AMDGPUDimNoSampleProfile<opmod, dim, retty, dataargs, Mip>,
            props, sdnodeprops>;
    }
  }

  multiclass AMDGPUImageDimIntrinsicsAll<string opmod,
                                         list<LLVMType> retty,
                                         list<AMDGPUArg> dataargs,
                                         list<IntrinsicProperty> props,
                                         list<SDNodeProperty> sdnodeprops,
                                         bit Mip = 0> {
    foreach dim = AMDGPUDims.All in {
      def !strconcat(NAME, "_", dim.Name)
        : AMDGPUImageDimIntrinsic<
            AMDGPUDimNoSampleProfile<opmod, dim, retty, dataargs, Mip>,
            props, sdnodeprops>;
    }
  }

  defm int_amdgcn_image_load
    : AMDGPUImageDimIntrinsicsAll<"LOAD", [llvm_any_ty], [], [IntrReadMem],
                                  [SDNPMemOperand]>,
      AMDGPUImageDMaskIntrinsic;
  defm int_amdgcn_image_load_mip
    : AMDGPUImageDimIntrinsicsNoMsaa<"LOAD_MIP", [llvm_any_ty], [],
                                     [IntrReadMem], [SDNPMemOperand], 1>,
      AMDGPUImageDMaskIntrinsic;

  defm int_amdgcn_image_store : AMDGPUImageDimIntrinsicsAll<
              "STORE", [], [AMDGPUArg<llvm_anyfloat_ty, "vdata">],
              [IntrWriteMem], [SDNPMemOperand]>;
  defm int_amdgcn_image_store_mip : AMDGPUImageDimIntrinsicsNoMsaa<
              "STORE_MIP", [], [AMDGPUArg<llvm_anyfloat_ty, "vdata">],
              [IntrWriteMem], [SDNPMemOperand], 1>;

  //////////////////////////////////////////////////////////////////////////
  // sample and getlod intrinsics
  //////////////////////////////////////////////////////////////////////////
  multiclass AMDGPUImageDimSampleDims<string opmod,
                                      AMDGPUSampleVariant sample,
                                      bit NoMem = 0> {
    foreach dim = AMDGPUDims.NoMsaa in {
      def !strconcat(NAME, "_", dim.Name) : AMDGPUImageDimIntrinsic<
          AMDGPUDimSampleProfile<opmod, dim, sample>,
          !if(NoMem, [IntrNoMem], [IntrReadMem]),
          !if(NoMem, [], [SDNPMemOperand])>;
    }
  }

  foreach sample = AMDGPUSampleVariants in {
    defm int_amdgcn_image_sample # sample.LowerCaseMod
      : AMDGPUImageDimSampleDims<"SAMPLE" # sample.UpperCaseMod, sample>,
        AMDGPUImageDMaskIntrinsic;
  }

  defm int_amdgcn_image_getlod
    : AMDGPUImageDimSampleDims<"GET_LOD", AMDGPUSample, 1>,
      AMDGPUImageDMaskIntrinsic;

  //////////////////////////////////////////////////////////////////////////
  // getresinfo intrinsics
  //////////////////////////////////////////////////////////////////////////
  foreach dim = AMDGPUDims.All in {
    def !strconcat("int_amdgcn_image_getresinfo_", dim.Name)
      : AMDGPUImageDimIntrinsic<AMDGPUDimGetResInfoProfile<dim>, [IntrNoMem], []>,
        AMDGPUImageDMaskIntrinsic;
  }

  //////////////////////////////////////////////////////////////////////////
  // gather4 intrinsics
  //////////////////////////////////////////////////////////////////////////
  foreach sample = AMDGPUSampleVariantsNoGradients in {
    foreach dim = [AMDGPUDim2D, AMDGPUDimCube, AMDGPUDim2DArray] in {
      def int_amdgcn_image_gather4 # sample.LowerCaseMod # _ # dim.Name:
          AMDGPUImageDimIntrinsic<
              AMDGPUDimSampleProfile<"GATHER4" # sample.UpperCaseMod, dim, sample>,
              [IntrReadMem], [SDNPMemOperand]>;
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// atomic intrinsics
//////////////////////////////////////////////////////////////////////////
defset list<AMDGPUImageDimIntrinsic> AMDGPUImageDimAtomicIntrinsics = {
  multiclass AMDGPUImageDimAtomicX<string opmod, list<AMDGPUArg> dataargs> {
    foreach dim = AMDGPUDims.All in {
      def !strconcat(NAME, "_", dim.Name)
        : AMDGPUImageDimIntrinsic<
            AMDGPUDimAtomicProfile<opmod, dim, dataargs>,
            [], [SDNPMemOperand]>;
    }
  }

  multiclass AMDGPUImageDimAtomic<string opmod> {
    defm "" : AMDGPUImageDimAtomicX<opmod, [AMDGPUArg<LLVMMatchType<0>, "vdata">]>;
  }

  defm int_amdgcn_image_atomic_swap : AMDGPUImageDimAtomic<"ATOMIC_SWAP">;
  defm int_amdgcn_image_atomic_add : AMDGPUImageDimAtomic<"ATOMIC_ADD">;
  defm int_amdgcn_image_atomic_sub : AMDGPUImageDimAtomic<"ATOMIC_SUB">;
  defm int_amdgcn_image_atomic_smin : AMDGPUImageDimAtomic<"ATOMIC_SMIN">;
  defm int_amdgcn_image_atomic_umin : AMDGPUImageDimAtomic<"ATOMIC_UMIN">;
  defm int_amdgcn_image_atomic_smax : AMDGPUImageDimAtomic<"ATOMIC_SMAX">;
  defm int_amdgcn_image_atomic_umax : AMDGPUImageDimAtomic<"ATOMIC_UMAX">;
  defm int_amdgcn_image_atomic_and : AMDGPUImageDimAtomic<"ATOMIC_AND">;
  defm int_amdgcn_image_atomic_or : AMDGPUImageDimAtomic<"ATOMIC_OR">;
  defm int_amdgcn_image_atomic_xor : AMDGPUImageDimAtomic<"ATOMIC_XOR">;

  // TODO: INC/DEC are weird: they seem to have a vdata argument in hardware,
  //       even though it clearly shouldn't be needed
  defm int_amdgcn_image_atomic_inc : AMDGPUImageDimAtomic<"ATOMIC_INC">;
  defm int_amdgcn_image_atomic_dec : AMDGPUImageDimAtomic<"ATOMIC_DEC">;

  defm int_amdgcn_image_atomic_cmpswap :
      AMDGPUImageDimAtomicX<"ATOMIC_CMPSWAP", [AMDGPUArg<LLVMMatchType<0>, "src">,
                                               AMDGPUArg<LLVMMatchType<0>, "cmp">]>;
}

//////////////////////////////////////////////////////////////////////////
// Buffer intrinsics
//////////////////////////////////////////////////////////////////////////

let TargetPrefix = "amdgcn" in {

defset list<AMDGPURsrcIntrinsic> AMDGPUBufferIntrinsics = {

class AMDGPUBufferLoad : Intrinsic <
  [llvm_anyfloat_ty],
  [llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(SGPR/VGPR/imm)
   llvm_i1_ty,        // glc(imm)
   llvm_i1_ty],       // slc(imm)
  [IntrReadMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_buffer_load_format : AMDGPUBufferLoad;
def int_amdgcn_buffer_load : AMDGPUBufferLoad;

def int_amdgcn_s_buffer_load : Intrinsic <
  [llvm_any_ty],
  [llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // byte offset(SGPR/VGPR/imm)
   llvm_i32_ty],      // cachepolicy(imm; bit 0 = glc)
  [IntrNoMem]>,
  AMDGPURsrcIntrinsic<0>;

class AMDGPUBufferStore : Intrinsic <
  [],
  [llvm_anyfloat_ty,  // vdata(VGPR) -- can currently only select f32, v2f32, v4f32
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(SGPR/VGPR/imm)
   llvm_i1_ty,        // glc(imm)
   llvm_i1_ty],       // slc(imm)
  [IntrWriteMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;
def int_amdgcn_buffer_store_format : AMDGPUBufferStore;
def int_amdgcn_buffer_store : AMDGPUBufferStore;

// New buffer intrinsics with separate raw and struct variants.  The raw
// variant never has an index. The struct variant always has an index, even if
// it is const 0. A struct intrinsic with constant 0 index is different to the
// corresponding raw intrinsic on gfx9+ because the behavior of bound checking
// and swizzling changes depending on whether idxen is set in the instruction.
// These new instrinsics also keep the offset and soffset arguments separate as
// they behave differently in bounds checking and swizzling.
class AMDGPURawBufferLoad : Intrinsic <
  [llvm_any_ty],
  [llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
  [IntrReadMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_raw_buffer_load_format : AMDGPURawBufferLoad;
def int_amdgcn_raw_buffer_load : AMDGPURawBufferLoad;

class AMDGPUStructBufferLoad : Intrinsic <
  [llvm_any_ty],
  [llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
  [IntrReadMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_struct_buffer_load_format : AMDGPUStructBufferLoad;
def int_amdgcn_struct_buffer_load : AMDGPUStructBufferLoad;

class AMDGPURawBufferStore : Intrinsic <
  [],
  [llvm_any_ty,       // vdata(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
  [IntrWriteMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;
def int_amdgcn_raw_buffer_store_format : AMDGPURawBufferStore;
def int_amdgcn_raw_buffer_store : AMDGPURawBufferStore;

class AMDGPUStructBufferStore : Intrinsic <
  [],
  [llvm_any_ty,       // vdata(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
  [IntrWriteMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;
def int_amdgcn_struct_buffer_store_format : AMDGPUStructBufferStore;
def int_amdgcn_struct_buffer_store : AMDGPUStructBufferStore;

class AMDGPURawBufferAtomic : Intrinsic <
  [llvm_anyint_ty],
  [LLVMMatchType<0>,  // vdata(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc)
  [], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1, 0>;
def int_amdgcn_raw_buffer_atomic_swap : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_add : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_sub : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_smin : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_umin : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_smax : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_umax : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_and : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_or : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_xor : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_cmpswap : Intrinsic<
  [llvm_anyint_ty],
  [LLVMMatchType<0>,  // src(VGPR)
   LLVMMatchType<0>,  // cmp(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc)
  [], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<2, 0>;

class AMDGPUStructBufferAtomic : Intrinsic <
  [llvm_anyint_ty],
  [LLVMMatchType<0>,  // vdata(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc)
  [], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1, 0>;
def int_amdgcn_struct_buffer_atomic_swap : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_add : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_sub : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_smin : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_umin : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_smax : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_umax : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_and : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_or : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_xor : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_cmpswap : Intrinsic<
  [llvm_anyint_ty],
  [LLVMMatchType<0>,  // src(VGPR)
   LLVMMatchType<0>,  // cmp(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc)
  [], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<2, 0>;

// Obsolescent tbuffer intrinsics.
def int_amdgcn_tbuffer_load : Intrinsic <
    [llvm_any_ty],    // overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
    [llvm_v4i32_ty,   // rsrc(SGPR)
     llvm_i32_ty,     // vindex(VGPR)
     llvm_i32_ty,     // voffset(VGPR)
     llvm_i32_ty,     // soffset(SGPR)
     llvm_i32_ty,     // offset(imm)
     llvm_i32_ty,     // dfmt(imm)
     llvm_i32_ty,     // nfmt(imm)
     llvm_i1_ty,     // glc(imm)
     llvm_i1_ty],    // slc(imm)
    [IntrReadMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;

def int_amdgcn_tbuffer_store : Intrinsic <
    [],
    [llvm_any_ty,    // vdata(VGPR), overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
     llvm_v4i32_ty,  // rsrc(SGPR)
     llvm_i32_ty,    // vindex(VGPR)
     llvm_i32_ty,    // voffset(VGPR)
     llvm_i32_ty,    // soffset(SGPR)
     llvm_i32_ty,    // offset(imm)
     llvm_i32_ty,    // dfmt(imm)
     llvm_i32_ty,    // nfmt(imm)
     llvm_i1_ty,     // glc(imm)
     llvm_i1_ty],    // slc(imm)
    [IntrWriteMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;

// New tbuffer intrinsics, with:
// - raw and struct variants
// - joint format field
// - joint cachepolicy field
def int_amdgcn_raw_tbuffer_load : Intrinsic <
    [llvm_any_ty],    // overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
    [llvm_v4i32_ty,   // rsrc(SGPR)
     llvm_i32_ty,     // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,     // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,     // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],    // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
    [IntrReadMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;

def int_amdgcn_raw_tbuffer_store : Intrinsic <
    [],
    [llvm_any_ty,    // vdata(VGPR), overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
     llvm_v4i32_ty,  // rsrc(SGPR)
     llvm_i32_ty,    // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,    // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,    // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],   // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
    [IntrWriteMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;

def int_amdgcn_struct_tbuffer_load : Intrinsic <
    [llvm_any_ty],    // overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
    [llvm_v4i32_ty,   // rsrc(SGPR)
     llvm_i32_ty,     // vindex(VGPR)
     llvm_i32_ty,     // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,     // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,     // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],    // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
    [IntrReadMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;

def int_amdgcn_struct_tbuffer_store : Intrinsic <
    [],
    [llvm_any_ty,    // vdata(VGPR), overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
     llvm_v4i32_ty,  // rsrc(SGPR)
     llvm_i32_ty,    // vindex(VGPR)
     llvm_i32_ty,    // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,    // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,    // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],   // cachepolicy(imm; bit 0 = glc, bit 1 = slc)
    [IntrWriteMem], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;

class AMDGPUBufferAtomic : Intrinsic <
  [llvm_i32_ty],
  [llvm_i32_ty,       // vdata(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(SGPR/VGPR/imm)
   llvm_i1_ty],       // slc(imm)
  [], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1, 0>;
def int_amdgcn_buffer_atomic_swap : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_add : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_sub : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_smin : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_umin : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_smax : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_umax : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_and : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_or : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_xor : AMDGPUBufferAtomic;
def int_amdgcn_buffer_atomic_cmpswap : Intrinsic<
  [llvm_i32_ty],
  [llvm_i32_ty,       // src(VGPR)
   llvm_i32_ty,       // cmp(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(SGPR/VGPR/imm)
   llvm_i1_ty],       // slc(imm)
  [], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<2, 0>;

} // defset AMDGPUBufferIntrinsics

// Uses that do not set the done bit should set IntrWriteMem on the
// call site.
def int_amdgcn_exp : Intrinsic <[], [
  llvm_i32_ty,       // tgt,
  llvm_i32_ty,       // en
  llvm_any_ty,       // src0 (f32 or i32)
  LLVMMatchType<0>,  // src1
  LLVMMatchType<0>,  // src2
  LLVMMatchType<0>,  // src3
  llvm_i1_ty,        // done
  llvm_i1_ty         // vm
  ],
  []
>;

// exp with compr bit set.
def int_amdgcn_exp_compr : Intrinsic <[], [
  llvm_i32_ty,       // tgt,
  llvm_i32_ty,       // en
  llvm_anyvector_ty, // src0 (v2f16 or v2i16)
  LLVMMatchType<0>,  // src1
  llvm_i1_ty,        // done
  llvm_i1_ty],       // vm
  []
>;

def int_amdgcn_buffer_wbinvl1_sc :
  GCCBuiltin<"__builtin_amdgcn_buffer_wbinvl1_sc">,
  Intrinsic<[], [], []>;

def int_amdgcn_buffer_wbinvl1 :
  GCCBuiltin<"__builtin_amdgcn_buffer_wbinvl1">,
  Intrinsic<[], [], []>;

def int_amdgcn_s_dcache_inv :
  GCCBuiltin<"__builtin_amdgcn_s_dcache_inv">,
  Intrinsic<[], [], []>;

def int_amdgcn_s_memtime :
  GCCBuiltin<"__builtin_amdgcn_s_memtime">,
  Intrinsic<[llvm_i64_ty], [], [IntrReadMem]>;

def int_amdgcn_s_sleep :
  GCCBuiltin<"__builtin_amdgcn_s_sleep">,
  Intrinsic<[], [llvm_i32_ty], []> {
}

def int_amdgcn_s_incperflevel :
  GCCBuiltin<"__builtin_amdgcn_s_incperflevel">,
  Intrinsic<[], [llvm_i32_ty], []> {
}

def int_amdgcn_s_decperflevel :
  GCCBuiltin<"__builtin_amdgcn_s_decperflevel">,
  Intrinsic<[], [llvm_i32_ty], []> {
}

def int_amdgcn_s_getreg :
  GCCBuiltin<"__builtin_amdgcn_s_getreg">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty],
  [IntrReadMem, IntrSpeculatable]
>;

// int_amdgcn_s_getpc is provided to allow a specific style of position
// independent code to determine the high part of its address when it is
// known (through convention) that the code and any data of interest does
// not cross a 4Gb address boundary. Use for any other purpose may not
// produce the desired results as optimizations may cause code movement,
// especially as we explicitly use IntrNoMem to allow optimizations.
def int_amdgcn_s_getpc :
  GCCBuiltin<"__builtin_amdgcn_s_getpc">,
  Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrSpeculatable]>;

// __builtin_amdgcn_interp_mov <param>, <attr_chan>, <attr>, <m0>
// param values: 0 = P10, 1 = P20, 2 = P0
def int_amdgcn_interp_mov :
  GCCBuiltin<"__builtin_amdgcn_interp_mov">,
  Intrinsic<[llvm_float_ty],
            [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable]>;

// __builtin_amdgcn_interp_p1 <i>, <attr_chan>, <attr>, <m0>
// This intrinsic reads from lds, but the memory values are constant,
// so it behaves like IntrNoMem.
def int_amdgcn_interp_p1 :
  GCCBuiltin<"__builtin_amdgcn_interp_p1">,
  Intrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable]>;

// __builtin_amdgcn_interp_p2 <p1>, <j>, <attr_chan>, <attr>, <m0>
def int_amdgcn_interp_p2 :
  GCCBuiltin<"__builtin_amdgcn_interp_p2">,
  Intrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_float_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable]>;
          // See int_amdgcn_v_interp_p1 for why this is IntrNoMem.

// Pixel shaders only: whether the current pixel is live (i.e. not a helper
// invocation for derivative computation).
def int_amdgcn_ps_live : Intrinsic <
  [llvm_i1_ty],
  [],
  [IntrNoMem]>;

def int_amdgcn_mbcnt_lo :
  GCCBuiltin<"__builtin_amdgcn_mbcnt_lo">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;

def int_amdgcn_mbcnt_hi :
  GCCBuiltin<"__builtin_amdgcn_mbcnt_hi">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;

// llvm.amdgcn.ds.swizzle src offset
def int_amdgcn_ds_swizzle :
  GCCBuiltin<"__builtin_amdgcn_ds_swizzle">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem, IntrConvergent]>;

def int_amdgcn_ubfe : Intrinsic<[llvm_anyint_ty],
  [LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sbfe : Intrinsic<[llvm_anyint_ty],
  [LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_lerp :
  GCCBuiltin<"__builtin_amdgcn_lerp">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sad_u8 :
  GCCBuiltin<"__builtin_amdgcn_sad_u8">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_msad_u8 :
  GCCBuiltin<"__builtin_amdgcn_msad_u8">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sad_hi_u8 :
  GCCBuiltin<"__builtin_amdgcn_sad_hi_u8">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sad_u16 :
  GCCBuiltin<"__builtin_amdgcn_sad_u16">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_qsad_pk_u16_u8 :
  GCCBuiltin<"__builtin_amdgcn_qsad_pk_u16_u8">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_mqsad_pk_u16_u8 :
  GCCBuiltin<"__builtin_amdgcn_mqsad_pk_u16_u8">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_mqsad_u32_u8 :
  GCCBuiltin<"__builtin_amdgcn_mqsad_u32_u8">,
  Intrinsic<[llvm_v4i32_ty], [llvm_i64_ty, llvm_i32_ty, llvm_v4i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pk_u8_f32 :
  GCCBuiltin<"__builtin_amdgcn_cvt_pk_u8_f32">,
  Intrinsic<[llvm_i32_ty], [llvm_float_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_icmp :
  Intrinsic<[llvm_i64_ty], [llvm_anyint_ty, LLVMMatchType<0>, llvm_i32_ty],
            [IntrNoMem, IntrConvergent]>;

def int_amdgcn_fcmp :
  Intrinsic<[llvm_i64_ty], [llvm_anyfloat_ty, LLVMMatchType<0>, llvm_i32_ty],
            [IntrNoMem, IntrConvergent]>;

def int_amdgcn_readfirstlane :
  GCCBuiltin<"__builtin_amdgcn_readfirstlane">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem, IntrConvergent]>;

// The lane argument must be uniform across the currently active threads of the
// current wave. Otherwise, the result is undefined.
def int_amdgcn_readlane :
  GCCBuiltin<"__builtin_amdgcn_readlane">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem, IntrConvergent]>;

// The value to write and lane select arguments must be uniform across the
// currently active threads of the current wave. Otherwise, the result is
// undefined.
def int_amdgcn_writelane :
  GCCBuiltin<"__builtin_amdgcn_writelane">,
  Intrinsic<[llvm_i32_ty], [
    llvm_i32_ty,    // uniform value to write: returned by the selected lane
    llvm_i32_ty,    // uniform lane select
    llvm_i32_ty     // returned by all lanes other than the selected one
  ],
  [IntrNoMem, IntrConvergent]
>;

def int_amdgcn_alignbit : Intrinsic<[llvm_i32_ty],
  [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_alignbyte : Intrinsic<[llvm_i32_ty],
  [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;


// Copies the source value to the destination value, with the guarantee that
// the source value is computed as if the entire program were executed in WQM.
def int_amdgcn_wqm : Intrinsic<[llvm_any_ty],
  [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

// Return true if at least one thread within the pixel quad passes true into
// the function.
def int_amdgcn_wqm_vote : Intrinsic<[llvm_i1_ty],
  [llvm_i1_ty], [IntrNoMem, IntrConvergent]
>;

// If false, set EXEC=0 for the current thread until the end of program.
def int_amdgcn_kill : Intrinsic<[], [llvm_i1_ty], []>;

// Copies the active channels of the source value to the destination value,
// with the guarantee that the source value is computed as if the entire
// program were executed in Whole Wavefront Mode, i.e. with all channels
// enabled, with a few exceptions: - Phi nodes with require WWM return an
// undefined value.
def int_amdgcn_wwm : Intrinsic<[llvm_any_ty],
  [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

// Given a value, copies it while setting all the inactive lanes to a given
// value. Note that OpenGL helper lanes are considered active, so if the
// program ever uses WQM, then the instruction and the first source will be
// computed in WQM.
def int_amdgcn_set_inactive :
  Intrinsic<[llvm_anyint_ty],
            [LLVMMatchType<0>, // value to be copied
             LLVMMatchType<0>], // value for the inactive lanes to take
            [IntrNoMem, IntrConvergent]>;

//===----------------------------------------------------------------------===//
// CI+ Intrinsics
//===----------------------------------------------------------------------===//

def int_amdgcn_s_dcache_inv_vol :
  GCCBuiltin<"__builtin_amdgcn_s_dcache_inv_vol">,
  Intrinsic<[], [], []>;

def int_amdgcn_buffer_wbinvl1_vol :
  GCCBuiltin<"__builtin_amdgcn_buffer_wbinvl1_vol">,
  Intrinsic<[], [], []>;

//===----------------------------------------------------------------------===//
// VI Intrinsics
//===----------------------------------------------------------------------===//

// llvm.amdgcn.mov.dpp.i32 <src> <dpp_ctrl> <row_mask> <bank_mask> <bound_ctrl>
def int_amdgcn_mov_dpp :
  Intrinsic<[llvm_anyint_ty],
            [LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
             llvm_i1_ty], [IntrNoMem, IntrConvergent]>;

// llvm.amdgcn.update.dpp.i32 <old> <src> <dpp_ctrl> <row_mask> <bank_mask> <bound_ctrl>
// Should be equivalent to:
// v_mov_b32 <dest> <old>
// v_mov_b32 <dest> <src> <dpp_ctrl> <row_mask> <bank_mask> <bound_ctrl>
def int_amdgcn_update_dpp :
  Intrinsic<[llvm_anyint_ty],
            [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty,
             llvm_i32_ty, llvm_i1_ty], [IntrNoMem, IntrConvergent]>;

def int_amdgcn_s_dcache_wb :
  GCCBuiltin<"__builtin_amdgcn_s_dcache_wb">,
  Intrinsic<[], [], []>;

def int_amdgcn_s_dcache_wb_vol :
  GCCBuiltin<"__builtin_amdgcn_s_dcache_wb_vol">,
  Intrinsic<[], [], []>;

def int_amdgcn_s_memrealtime :
  GCCBuiltin<"__builtin_amdgcn_s_memrealtime">,
  Intrinsic<[llvm_i64_ty], [], [IntrReadMem]>;

// llvm.amdgcn.ds.permute <index> <src>
def int_amdgcn_ds_permute :
  GCCBuiltin<"__builtin_amdgcn_ds_permute">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem, IntrConvergent]>;

// llvm.amdgcn.ds.bpermute <index> <src>
def int_amdgcn_ds_bpermute :
  GCCBuiltin<"__builtin_amdgcn_ds_bpermute">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem, IntrConvergent]>;

//===----------------------------------------------------------------------===//
// Deep learning intrinsics.
//===----------------------------------------------------------------------===//

// f32 %r = llvm.amdgcn.fdot2(v2f16 %a, v2f16 %b, f32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_fdot2 :
  GCCBuiltin<"__builtin_amdgcn_fdot2">,
  Intrinsic<
    [llvm_float_ty], // %r
    [
      llvm_v2f16_ty, // %a
      llvm_v2f16_ty, // %b
      llvm_float_ty, // %c
      llvm_i1_ty     // %clamp
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

// i32 %r = llvm.amdgcn.sdot2(v2i16 %a, v2i16 %b, i32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_sdot2 :
  GCCBuiltin<"__builtin_amdgcn_sdot2">,
  Intrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_v2i16_ty, // %a
      llvm_v2i16_ty, // %b
      llvm_i32_ty,   // %c
      llvm_i1_ty     // %clamp
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

// u32 %r = llvm.amdgcn.udot2(v2u16 %a, v2u16 %b, u32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_udot2 :
  GCCBuiltin<"__builtin_amdgcn_udot2">,
  Intrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_v2i16_ty, // %a
      llvm_v2i16_ty, // %b
      llvm_i32_ty,   // %c
      llvm_i1_ty     // %clamp
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

// i32 %r = llvm.amdgcn.sdot4(v4i8 (as i32) %a, v4i8 (as i32) %b, i32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] + %c
def int_amdgcn_sdot4 :
  GCCBuiltin<"__builtin_amdgcn_sdot4">,
  Intrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i32_ty, // %a
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

// u32 %r = llvm.amdgcn.udot4(v4u8 (as u32) %a, v4u8 (as u32) %b, u32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] + %c
def int_amdgcn_udot4 :
  GCCBuiltin<"__builtin_amdgcn_udot4">,
  Intrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i32_ty, // %a
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

// i32 %r = llvm.amdgcn.sdot8(v8i4 (as i32) %a, v8i4 (as i32) %b, i32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] +
//        %a[4] * %b[4] + %a[5] * %b[5] + %a[6] * %b[6] + %a[7] * %b[7] + %c
def int_amdgcn_sdot8 :
  GCCBuiltin<"__builtin_amdgcn_sdot8">,
  Intrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i32_ty, // %a
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

// u32 %r = llvm.amdgcn.udot8(v8u4 (as u32) %a, v8u4 (as u32) %b, u32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] +
//        %a[4] * %b[4] + %a[5] * %b[5] + %a[6] * %b[6] + %a[7] * %b[7] + %c
def int_amdgcn_udot8 :
  GCCBuiltin<"__builtin_amdgcn_udot8">,
  Intrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i32_ty, // %a
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

//===----------------------------------------------------------------------===//
// Special Intrinsics for backend internal use only. No frontend
// should emit calls to these.
// ===----------------------------------------------------------------------===//
def int_amdgcn_if : Intrinsic<[llvm_i1_ty, llvm_i64_ty],
  [llvm_i1_ty], [IntrConvergent]
>;

def int_amdgcn_else : Intrinsic<[llvm_i1_ty, llvm_i64_ty],
  [llvm_i64_ty], [IntrConvergent]
>;

def int_amdgcn_if_break : Intrinsic<[llvm_i64_ty],
  [llvm_i1_ty, llvm_i64_ty], [IntrNoMem, IntrConvergent]
>;

def int_amdgcn_loop : Intrinsic<[llvm_i1_ty],
  [llvm_i64_ty], [IntrConvergent]
>;

def int_amdgcn_end_cf : Intrinsic<[], [llvm_i64_ty], [IntrConvergent]>;

// Represent unreachable in a divergent region.
def int_amdgcn_unreachable : Intrinsic<[], [], [IntrConvergent]>;

// Emit 2.5 ulp, no denormal division. Should only be inserted by
// pass based on !fpmath metadata.
def int_amdgcn_fdiv_fast : Intrinsic<
  [llvm_float_ty], [llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;
}
