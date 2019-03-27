//===-- AMDGPUKernelCodeT.h - Print AMDGPU assembly code ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file AMDKernelCodeT.h
//===----------------------------------------------------------------------===//

#ifndef AMDKERNELCODET_H
#define AMDKERNELCODET_H

#include "llvm/MC/SubtargetFeature.h"

#include <cstddef>
#include <cstdint>

#include "llvm/Support/Debug.h"
//---------------------------------------------------------------------------//
// AMD Kernel Code, and its dependencies                                     //
//---------------------------------------------------------------------------//

typedef uint8_t hsa_powertwo8_t;
typedef uint32_t hsa_ext_code_kind_t;
typedef uint8_t hsa_ext_brig_profile8_t;
typedef uint8_t hsa_ext_brig_machine_model8_t;
typedef uint64_t hsa_ext_control_directive_present64_t;
typedef uint16_t hsa_ext_exception_kind16_t;
typedef uint32_t hsa_ext_code_kind32_t;

typedef struct hsa_dim3_s {
  uint32_t x;
  uint32_t y;
  uint32_t z;
} hsa_dim3_t;

/// The version of the amd_*_code_t struct. Minor versions must be
/// backward compatible.
typedef uint32_t amd_code_version32_t;
enum amd_code_version_t {
  AMD_CODE_VERSION_MAJOR = 0,
  AMD_CODE_VERSION_MINOR = 1
};

// Sets val bits for specified mask in specified dst packed instance.
#define AMD_HSA_BITS_SET(dst, mask, val)                                       \
  dst &= (~(1 << mask ## _SHIFT) & ~mask);                                     \
  dst |= (((val) << mask ## _SHIFT) & mask)

// Gets bits for specified mask from specified src packed instance.
#define AMD_HSA_BITS_GET(src, mask)                                            \
  ((src & mask) >> mask ## _SHIFT)                                             \

/// The values used to define the number of bytes to use for the
/// swizzle element size.
enum amd_element_byte_size_t {
  AMD_ELEMENT_2_BYTES = 0,
  AMD_ELEMENT_4_BYTES = 1,
  AMD_ELEMENT_8_BYTES = 2,
  AMD_ELEMENT_16_BYTES = 3
};

/// Shader program settings for CS. Contains COMPUTE_PGM_RSRC1 and
/// COMPUTE_PGM_RSRC2 registers.
typedef uint64_t amd_compute_pgm_resource_register64_t;

/// Every amd_*_code_t has the following properties, which are composed of
/// a number of bit fields. Every bit field has a mask (AMD_CODE_PROPERTY_*),
/// bit width (AMD_CODE_PROPERTY_*_WIDTH, and bit shift amount
/// (AMD_CODE_PROPERTY_*_SHIFT) for convenient access. Unused bits must be 0.
///
/// (Note that bit fields cannot be used as their layout is
/// implementation defined in the C standard and so cannot be used to
/// specify an ABI)
typedef uint32_t amd_code_property32_t;
enum amd_code_property_mask_t {

  /// Enable the setup of the SGPR user data registers
  /// (AMD_CODE_PROPERTY_ENABLE_SGPR_*), see documentation of amd_kernel_code_t
  /// for initial register state.
  ///
  /// The total number of SGPRuser data registers requested must not
  /// exceed 16. Any requests beyond 16 will be ignored.
  ///
  /// Used to set COMPUTE_PGM_RSRC2.USER_SGPR (set to total count of
  /// SGPR user data registers enabled up to 16).

  AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER_SHIFT = 0,
  AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR_SHIFT = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_PTR_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_QUEUE_PTR_SHIFT = 2,
  AMD_CODE_PROPERTY_ENABLE_SGPR_QUEUE_PTR_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_QUEUE_PTR = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_QUEUE_PTR_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_QUEUE_PTR_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_KERNARG_SEGMENT_PTR_SHIFT = 3,
  AMD_CODE_PROPERTY_ENABLE_SGPR_KERNARG_SEGMENT_PTR_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_KERNARG_SEGMENT_PTR = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_KERNARG_SEGMENT_PTR_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_KERNARG_SEGMENT_PTR_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_ID_SHIFT = 4,
  AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_ID_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_ID = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_ID_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_DISPATCH_ID_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_FLAT_SCRATCH_INIT_SHIFT = 5,
  AMD_CODE_PROPERTY_ENABLE_SGPR_FLAT_SCRATCH_INIT_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_FLAT_SCRATCH_INIT = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_FLAT_SCRATCH_INIT_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_FLAT_SCRATCH_INIT_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_SIZE_SHIFT = 6,
  AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_SIZE_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_SIZE = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_SIZE_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_PRIVATE_SEGMENT_SIZE_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_X_SHIFT = 7,
  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_X_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_X = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_X_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_X_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Y_SHIFT = 8,
  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Y_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Y = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Y_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Y_SHIFT,

  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Z_SHIFT = 9,
  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Z_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Z = ((1 << AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Z_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_SGPR_GRID_WORKGROUP_COUNT_Z_SHIFT,

  AMD_CODE_PROPERTY_RESERVED1_SHIFT = 10,
  AMD_CODE_PROPERTY_RESERVED1_WIDTH = 6,
  AMD_CODE_PROPERTY_RESERVED1 = ((1 << AMD_CODE_PROPERTY_RESERVED1_WIDTH) - 1) << AMD_CODE_PROPERTY_RESERVED1_SHIFT,

  /// Control wave ID base counter for GDS ordered-append. Used to set
  /// COMPUTE_DISPATCH_INITIATOR.ORDERED_APPEND_ENBL. (Not sure if
  /// ORDERED_APPEND_MODE also needs to be settable)
  AMD_CODE_PROPERTY_ENABLE_ORDERED_APPEND_GDS_SHIFT = 16,
  AMD_CODE_PROPERTY_ENABLE_ORDERED_APPEND_GDS_WIDTH = 1,
  AMD_CODE_PROPERTY_ENABLE_ORDERED_APPEND_GDS = ((1 << AMD_CODE_PROPERTY_ENABLE_ORDERED_APPEND_GDS_WIDTH) - 1) << AMD_CODE_PROPERTY_ENABLE_ORDERED_APPEND_GDS_SHIFT,

  /// The interleave (swizzle) element size in bytes required by the
  /// code for private memory. This must be 2, 4, 8 or 16. This value
  /// is provided to the finalizer when it is invoked and is recorded
  /// here. The hardware will interleave the memory requests of each
  /// lane of a wavefront by this element size to ensure each
  /// work-item gets a distinct memory memory location. Therefore, the
  /// finalizer ensures that all load and store operations done to
  /// private memory do not exceed this size. For example, if the
  /// element size is 4 (32-bits or dword) and a 64-bit value must be
  /// loaded, the finalizer will generate two 32-bit loads. This
  /// ensures that the interleaving will get the work-item
  /// specific dword for both halves of the 64-bit value. If it just
  /// did a 64-bit load then it would get one dword which belonged to
  /// its own work-item, but the second dword would belong to the
  /// adjacent lane work-item since the interleaving is in dwords.
  ///
  /// The value used must match the value that the runtime configures
  /// the GPU flat scratch (SH_STATIC_MEM_CONFIG.ELEMENT_SIZE). This
  /// is generally DWORD.
  ///
  /// uSE VALUES FROM THE AMD_ELEMENT_BYTE_SIZE_T ENUM.
  AMD_CODE_PROPERTY_PRIVATE_ELEMENT_SIZE_SHIFT = 17,
  AMD_CODE_PROPERTY_PRIVATE_ELEMENT_SIZE_WIDTH = 2,
  AMD_CODE_PROPERTY_PRIVATE_ELEMENT_SIZE = ((1 << AMD_CODE_PROPERTY_PRIVATE_ELEMENT_SIZE_WIDTH) - 1) << AMD_CODE_PROPERTY_PRIVATE_ELEMENT_SIZE_SHIFT,

  /// Are global memory addresses 64 bits. Must match
  /// amd_kernel_code_t.hsail_machine_model ==
  /// HSA_MACHINE_LARGE. Must also match
  /// SH_MEM_CONFIG.PTR32 (GFX6 (SI)/GFX7 (CI)),
  /// SH_MEM_CONFIG.ADDRESS_MODE (GFX8 (VI)+).
  AMD_CODE_PROPERTY_IS_PTR64_SHIFT = 19,
  AMD_CODE_PROPERTY_IS_PTR64_WIDTH = 1,
  AMD_CODE_PROPERTY_IS_PTR64 = ((1 << AMD_CODE_PROPERTY_IS_PTR64_WIDTH) - 1) << AMD_CODE_PROPERTY_IS_PTR64_SHIFT,

  /// Indicate if the generated ISA is using a dynamically sized call
  /// stack. This can happen if calls are implemented using a call
  /// stack and recursion, alloca or calls to indirect functions are
  /// present. In these cases the Finalizer cannot compute the total
  /// private segment size at compile time. In this case the
  /// workitem_private_segment_byte_size only specifies the statically
  /// know private segment size, and additional space must be added
  /// for the call stack.
  AMD_CODE_PROPERTY_IS_DYNAMIC_CALLSTACK_SHIFT = 20,
  AMD_CODE_PROPERTY_IS_DYNAMIC_CALLSTACK_WIDTH = 1,
  AMD_CODE_PROPERTY_IS_DYNAMIC_CALLSTACK = ((1 << AMD_CODE_PROPERTY_IS_DYNAMIC_CALLSTACK_WIDTH) - 1) << AMD_CODE_PROPERTY_IS_DYNAMIC_CALLSTACK_SHIFT,

  /// Indicate if code generated has support for debugging.
  AMD_CODE_PROPERTY_IS_DEBUG_SUPPORTED_SHIFT = 21,
  AMD_CODE_PROPERTY_IS_DEBUG_SUPPORTED_WIDTH = 1,
  AMD_CODE_PROPERTY_IS_DEBUG_SUPPORTED = ((1 << AMD_CODE_PROPERTY_IS_DEBUG_SUPPORTED_WIDTH) - 1) << AMD_CODE_PROPERTY_IS_DEBUG_SUPPORTED_SHIFT,

  AMD_CODE_PROPERTY_IS_XNACK_SUPPORTED_SHIFT = 22,
  AMD_CODE_PROPERTY_IS_XNACK_SUPPORTED_WIDTH = 1,
  AMD_CODE_PROPERTY_IS_XNACK_SUPPORTED = ((1 << AMD_CODE_PROPERTY_IS_XNACK_SUPPORTED_WIDTH) - 1) << AMD_CODE_PROPERTY_IS_XNACK_SUPPORTED_SHIFT,

  AMD_CODE_PROPERTY_RESERVED2_SHIFT = 23,
  AMD_CODE_PROPERTY_RESERVED2_WIDTH = 9,
  AMD_CODE_PROPERTY_RESERVED2 = ((1 << AMD_CODE_PROPERTY_RESERVED2_WIDTH) - 1) << AMD_CODE_PROPERTY_RESERVED2_SHIFT
};

/// The hsa_ext_control_directives_t specifies the values for the HSAIL
/// control directives. These control how the finalizer generates code. This
/// struct is used both as an argument to hsaFinalizeKernel to specify values for
/// the control directives, and is used in HsaKernelCode to record the values of
/// the control directives that the finalize used when generating the code which
/// either came from the finalizer argument or explicit HSAIL control
/// directives. See the definition of the control directives in HSA Programmer's
/// Reference Manual which also defines how the values specified as finalizer
/// arguments have to agree with the control directives in the HSAIL code.
typedef struct hsa_ext_control_directives_s {
  /// This is a bit set indicating which control directives have been
  /// specified. If the value is 0 then there are no control directives specified
  /// and the rest of the fields can be ignored. The bits are accessed using the
  /// hsa_ext_control_directives_present_mask_t. Any control directive that is not
  /// enabled in this bit set must have the value of all 0s.
  hsa_ext_control_directive_present64_t enabled_control_directives;

  /// If enableBreakExceptions is not enabled then must be 0, otherwise must be
  /// non-0 and specifies the set of HSAIL exceptions that must have the BREAK
  /// policy enabled. If this set is not empty then the generated code may have
  /// lower performance than if the set is empty. If the kernel being finalized
  /// has any enablebreakexceptions control directives, then the values specified
  /// by this argument are unioned with the values in these control
  /// directives. If any of the functions the kernel calls have an
  /// enablebreakexceptions control directive, then they must be equal or a
  /// subset of, this union.
  hsa_ext_exception_kind16_t enable_break_exceptions;

  /// If enableDetectExceptions is not enabled then must be 0, otherwise must be
  /// non-0 and specifies the set of HSAIL exceptions that must have the DETECT
  /// policy enabled. If this set is not empty then the generated code may have
  /// lower performance than if the set is empty. However, an implementation
  /// should endeavour to make the performance impact small. If the kernel being
  /// finalized has any enabledetectexceptions control directives, then the
  /// values specified by this argument are unioned with the values in these
  /// control directives. If any of the functions the kernel calls have an
  /// enabledetectexceptions control directive, then they must be equal or a
  /// subset of, this union.
  hsa_ext_exception_kind16_t enable_detect_exceptions;

  /// If maxDynamicGroupSize is not enabled then must be 0, and any amount of
  /// dynamic group segment can be allocated for a dispatch, otherwise the value
  /// specifies the maximum number of bytes of dynamic group segment that can be
  /// allocated for a dispatch. If the kernel being finalized has any
  /// maxdynamicsize control directives, then the values must be the same, and
  /// must be the same as this argument if it is enabled. This value can be used
  /// by the finalizer to determine the maximum number of bytes of group memory
  /// used by each work-group by adding this value to the group memory required
  /// for all group segment variables used by the kernel and all functions it
  /// calls, and group memory used to implement other HSAIL features such as
  /// fbarriers and the detect exception operations. This can allow the finalizer
  /// to determine the expected number of work-groups that can be executed by a
  /// compute unit and allow more resources to be allocated to the work-items if
  /// it is known that fewer work-groups can be executed due to group memory
  /// limitations.
  uint32_t max_dynamic_group_size;

  /// If maxFlatGridSize is not enabled then must be 0, otherwise must be greater
  /// than 0. See HSA Programmer's Reference Manual description of
  /// maxflatgridsize control directive.
  uint32_t max_flat_grid_size;

  /// If maxFlatWorkgroupSize is not enabled then must be 0, otherwise must be
  /// greater than 0. See HSA Programmer's Reference Manual description of
  /// maxflatworkgroupsize control directive.
  uint32_t max_flat_workgroup_size;

  /// If requestedWorkgroupsPerCu is not enabled then must be 0, and the
  /// finalizer is free to generate ISA that may result in any number of
  /// work-groups executing on a single compute unit. Otherwise, the finalizer
  /// should attempt to generate ISA that will allow the specified number of
  /// work-groups to execute on a single compute unit. This is only a hint and
  /// can be ignored by the finalizer. If the kernel being finalized, or any of
  /// the functions it calls, has a requested control directive, then the values
  /// must be the same. This can be used to determine the number of resources
  /// that should be allocated to a single work-group and work-item. For example,
  /// a low value may allow more resources to be allocated, resulting in higher
  /// per work-item performance, as it is known there will never be more than the
  /// specified number of work-groups actually executing on the compute
  /// unit. Conversely, a high value may allocate fewer resources, resulting in
  /// lower per work-item performance, which is offset by the fact it allows more
  /// work-groups to actually execute on the compute unit.
  uint32_t requested_workgroups_per_cu;

  /// If not enabled then all elements for Dim3 must be 0, otherwise every
  /// element must be greater than 0. See HSA Programmer's Reference Manual
  /// description of requiredgridsize control directive.
  hsa_dim3_t required_grid_size;

  /// If requiredWorkgroupSize is not enabled then all elements for Dim3 must be
  /// 0, and the produced code can be dispatched with any legal work-group range
  /// consistent with the dispatch dimensions. Otherwise, the code produced must
  /// always be dispatched with the specified work-group range. No element of the
  /// specified range must be 0. It must be consistent with required_dimensions
  /// and max_flat_workgroup_size. If the kernel being finalized, or any of the
  /// functions it calls, has a requiredworkgroupsize control directive, then the
  /// values must be the same. Specifying a value can allow the finalizer to
  /// optimize work-group id operations, and if the number of work-items in the
  /// work-group is less than the WAVESIZE then barrier operations can be
  /// optimized to just a memory fence.
  hsa_dim3_t required_workgroup_size;

  /// If requiredDim is not enabled then must be 0 and the produced kernel code
  /// can be dispatched with 1, 2 or 3 dimensions. If enabled then the value is
  /// 1..3 and the code produced must only be dispatched with a dimension that
  /// matches. Other values are illegal. If the kernel being finalized, or any of
  /// the functions it calls, has a requireddimsize control directive, then the
  /// values must be the same. This can be used to optimize the code generated to
  /// compute the absolute and flat work-group and work-item id, and the dim
  /// HSAIL operations.
  uint8_t required_dim;

  /// Reserved. Must be 0.
  uint8_t reserved[75];
} hsa_ext_control_directives_t;

/// AMD Kernel Code Object (amd_kernel_code_t). GPU CP uses the AMD Kernel
/// Code Object to set up the hardware to execute the kernel dispatch.
///
/// Initial Kernel Register State.
///
/// Initial kernel register state will be set up by CP/SPI prior to the start
/// of execution of every wavefront. This is limited by the constraints of the
/// current hardware.
///
/// The order of the SGPR registers is defined, but the Finalizer can specify
/// which ones are actually setup in the amd_kernel_code_t object using the
/// enable_sgpr_* bit fields. The register numbers used for enabled registers
/// are dense starting at SGPR0: the first enabled register is SGPR0, the next
/// enabled register is SGPR1 etc.; disabled registers do not have an SGPR
/// number.
///
/// The initial SGPRs comprise up to 16 User SRGPs that are set up by CP and
/// apply to all waves of the grid. It is possible to specify more than 16 User
/// SGPRs using the enable_sgpr_* bit fields, in which case only the first 16
/// are actually initialized. These are then immediately followed by the System
/// SGPRs that are set up by ADC/SPI and can have different values for each wave
/// of the grid dispatch.
///
/// SGPR register initial state is defined as follows:
///
/// Private Segment Buffer (enable_sgpr_private_segment_buffer):
///   Number of User SGPR registers: 4. V# that can be used, together with
///   Scratch Wave Offset as an offset, to access the Private/Spill/Arg
///   segments using a segment address. It must be set as follows:
///     - Base address: of the scratch memory area used by the dispatch. It
///       does not include the scratch wave offset. It will be the per process
///       SH_HIDDEN_PRIVATE_BASE_VMID plus any offset from this dispatch (for
///       example there may be a per pipe offset, or per AQL Queue offset).
///     - Stride + data_format: Element Size * Index Stride (???)
///     - Cache swizzle: ???
///     - Swizzle enable: SH_STATIC_MEM_CONFIG.SWIZZLE_ENABLE (must be 1 for
///       scratch)
///     - Num records: Flat Scratch Work Item Size / Element Size (???)
///     - Dst_sel_*: ???
///     - Num_format: ???
///     - Element_size: SH_STATIC_MEM_CONFIG.ELEMENT_SIZE (will be DWORD, must
///       agree with amd_kernel_code_t.privateElementSize)
///     - Index_stride: SH_STATIC_MEM_CONFIG.INDEX_STRIDE (will be 64 as must
///       be number of wavefront lanes for scratch, must agree with
///       amd_kernel_code_t.wavefrontSize)
///     - Add tid enable: 1
///     - ATC: from SH_MEM_CONFIG.PRIVATE_ATC,
///     - Hash_enable: ???
///     - Heap: ???
///     - Mtype: from SH_STATIC_MEM_CONFIG.PRIVATE_MTYPE
///     - Type: 0 (a buffer) (???)
///
/// Dispatch Ptr (enable_sgpr_dispatch_ptr):
///   Number of User SGPR registers: 2. 64 bit address of AQL dispatch packet
///   for kernel actually executing.
///
/// Queue Ptr (enable_sgpr_queue_ptr):
///   Number of User SGPR registers: 2. 64 bit address of AmdQueue object for
///   AQL queue on which the dispatch packet was queued.
///
/// Kernarg Segment Ptr (enable_sgpr_kernarg_segment_ptr):
///   Number of User SGPR registers: 2. 64 bit address of Kernarg segment. This
///   is directly copied from the kernargPtr in the dispatch packet. Having CP
///   load it once avoids loading it at the beginning of every wavefront.
///
/// Dispatch Id (enable_sgpr_dispatch_id):
///   Number of User SGPR registers: 2. 64 bit Dispatch ID of the dispatch
///   packet being executed.
///
/// Flat Scratch Init (enable_sgpr_flat_scratch_init):
///   Number of User SGPR registers: 2. This is 2 SGPRs.
///
///   For CI/VI:
///     The first SGPR is a 32 bit byte offset from SH_MEM_HIDDEN_PRIVATE_BASE
///     to base of memory for scratch for this dispatch. This is the same offset
///     used in computing the Scratch Segment Buffer base address. The value of
///     Scratch Wave Offset must be added by the kernel code and moved to
///     SGPRn-4 for use as the FLAT SCRATCH BASE in flat memory instructions.
///
///     The second SGPR is 32 bit byte size of a single work-item's scratch
///     memory usage. This is directly loaded from the dispatch packet Private
///     Segment Byte Size and rounded up to a multiple of DWORD.
///
///     \todo [Does CP need to round this to >4 byte alignment?]
///
///     The kernel code must move to SGPRn-3 for use as the FLAT SCRATCH SIZE in
///     flat memory instructions. Having CP load it once avoids loading it at
///     the beginning of every wavefront.
///
///   For PI:
///     This is the 64 bit base address of the scratch backing memory for
///     allocated by CP for this dispatch.
///
/// Private Segment Size (enable_sgpr_private_segment_size):
///   Number of User SGPR registers: 1. The 32 bit byte size of a single
///   work-item's scratch memory allocation. This is the value from the dispatch
///   packet. Private Segment Byte Size rounded up by CP to a multiple of DWORD.
///
///   \todo [Does CP need to round this to >4 byte alignment?]
///
///   Having CP load it once avoids loading it at the beginning of every
///   wavefront.
///
///   \todo [This will not be used for CI/VI since it is the same value as
///   the second SGPR of Flat Scratch Init. However, it is need for PI which
///   changes meaning of Flat Scratchg Init..]
///
/// Grid Work-Group Count X (enable_sgpr_grid_workgroup_count_x):
///   Number of User SGPR registers: 1. 32 bit count of the number of
///   work-groups in the X dimension for the grid being executed. Computed from
///   the fields in the HsaDispatchPacket as
///   ((gridSize.x+workgroupSize.x-1)/workgroupSize.x).
///
/// Grid Work-Group Count Y (enable_sgpr_grid_workgroup_count_y):
///   Number of User SGPR registers: 1. 32 bit count of the number of
///   work-groups in the Y dimension for the grid being executed. Computed from
///   the fields in the HsaDispatchPacket as
///   ((gridSize.y+workgroupSize.y-1)/workgroupSize.y).
///
///   Only initialized if <16 previous SGPRs initialized.
///
/// Grid Work-Group Count Z (enable_sgpr_grid_workgroup_count_z):
///   Number of User SGPR registers: 1. 32 bit count of the number of
///   work-groups in the Z dimension for the grid being executed. Computed
///   from the fields in the HsaDispatchPacket as
///   ((gridSize.z+workgroupSize.z-1)/workgroupSize.z).
///
///   Only initialized if <16 previous SGPRs initialized.
///
/// Work-Group Id X (enable_sgpr_workgroup_id_x):
///   Number of System SGPR registers: 1. 32 bit work group id in X dimension
///   of grid for wavefront. Always present.
///
/// Work-Group Id Y (enable_sgpr_workgroup_id_y):
///   Number of System SGPR registers: 1. 32 bit work group id in Y dimension
///   of grid for wavefront.
///
/// Work-Group Id Z (enable_sgpr_workgroup_id_z):
///   Number of System SGPR registers: 1. 32 bit work group id in Z dimension
///   of grid for wavefront. If present then Work-group Id Y will also be
///   present
///
/// Work-Group Info (enable_sgpr_workgroup_info):
///   Number of System SGPR registers: 1. {first_wave, 14'b0000,
///   ordered_append_term[10:0], threadgroup_size_in_waves[5:0]}
///
/// Private Segment Wave Byte Offset
/// (enable_sgpr_private_segment_wave_byte_offset):
///   Number of System SGPR registers: 1. 32 bit byte offset from base of
///   dispatch scratch base. Must be used as an offset with Private/Spill/Arg
///   segment address when using Scratch Segment Buffer. It must be added to
///   Flat Scratch Offset if setting up FLAT SCRATCH for flat addressing.
///
///
/// The order of the VGPR registers is defined, but the Finalizer can specify
/// which ones are actually setup in the amd_kernel_code_t object using the
/// enableVgpr*  bit fields. The register numbers used for enabled registers
/// are dense starting at VGPR0: the first enabled register is VGPR0, the next
/// enabled register is VGPR1 etc.; disabled registers do not have an VGPR
/// number.
///
/// VGPR register initial state is defined as follows:
///
/// Work-Item Id X (always initialized):
///   Number of registers: 1. 32 bit work item id in X dimension of work-group
///   for wavefront lane.
///
/// Work-Item Id X (enable_vgpr_workitem_id > 0):
///   Number of registers: 1. 32 bit work item id in Y dimension of work-group
///   for wavefront lane.
///
/// Work-Item Id X (enable_vgpr_workitem_id > 0):
///   Number of registers: 1. 32 bit work item id in Z dimension of work-group
///   for wavefront lane.
///
///
/// The setting of registers is being done by existing GPU hardware as follows:
///   1) SGPRs before the Work-Group Ids are set by CP using the 16 User Data
///      registers.
///   2) Work-group Id registers X, Y, Z are set by SPI which supports any
///      combination including none.
///   3) Scratch Wave Offset is also set by SPI which is why its value cannot
///      be added into the value Flat Scratch Offset which would avoid the
///      Finalizer generated prolog having to do the add.
///   4) The VGPRs are set by SPI which only supports specifying either (X),
///      (X, Y) or (X, Y, Z).
///
/// Flat Scratch Dispatch Offset and Flat Scratch Size are adjacent SGRRs so
/// they can be moved as a 64 bit value to the hardware required SGPRn-3 and
/// SGPRn-4 respectively using the Finalizer ?FLAT_SCRATCH? Register.
///
/// The global segment can be accessed either using flat operations or buffer
/// operations. If buffer operations are used then the Global Buffer used to
/// access HSAIL Global/Readonly/Kernarg (which are combine) segments using a
/// segment address is not passed into the kernel code by CP since its base
/// address is always 0. Instead the Finalizer generates prolog code to
/// initialize 4 SGPRs with a V# that has the following properties, and then
/// uses that in the buffer instructions:
///   - base address of 0
///   - no swizzle
///   - ATC=1
///   - MTYPE set to support memory coherence specified in
///     amd_kernel_code_t.globalMemoryCoherence
///
/// When the Global Buffer is used to access the Kernarg segment, must add the
/// dispatch packet kernArgPtr to a kernarg segment address before using this V#.
/// Alternatively scalar loads can be used if the kernarg offset is uniform, as
/// the kernarg segment is constant for the duration of the kernel execution.
///

typedef struct amd_kernel_code_s {
  uint32_t amd_kernel_code_version_major;
  uint32_t amd_kernel_code_version_minor;
  uint16_t amd_machine_kind;
  uint16_t amd_machine_version_major;
  uint16_t amd_machine_version_minor;
  uint16_t amd_machine_version_stepping;

  /// Byte offset (possibly negative) from start of amd_kernel_code_t
  /// object to kernel's entry point instruction. The actual code for
  /// the kernel is required to be 256 byte aligned to match hardware
  /// requirements (SQ cache line is 16). The code must be position
  /// independent code (PIC) for AMD devices to give runtime the
  /// option of copying code to discrete GPU memory or APU L2
  /// cache. The Finalizer should endeavour to allocate all kernel
  /// machine code in contiguous memory pages so that a device
  /// pre-fetcher will tend to only pre-fetch Kernel Code objects,
  /// improving cache performance.
  int64_t kernel_code_entry_byte_offset;

  /// Range of bytes to consider prefetching expressed as an offset
  /// and size. The offset is from the start (possibly negative) of
  /// amd_kernel_code_t object. Set both to 0 if no prefetch
  /// information is available.
  int64_t kernel_code_prefetch_byte_offset;
  uint64_t kernel_code_prefetch_byte_size;

  /// Reserved. Must be 0.
  uint64_t reserved0;

  /// Shader program settings for CS. Contains COMPUTE_PGM_RSRC1 and
  /// COMPUTE_PGM_RSRC2 registers.
  uint64_t compute_pgm_resource_registers;

  /// Code properties. See amd_code_property_mask_t for a full list of
  /// properties.
  uint32_t code_properties;

  /// The amount of memory required for the combined private, spill
  /// and arg segments for a work-item in bytes. If
  /// is_dynamic_callstack is 1 then additional space must be added to
  /// this value for the call stack.
  uint32_t workitem_private_segment_byte_size;

  /// The amount of group segment memory required by a work-group in
  /// bytes. This does not include any dynamically allocated group
  /// segment memory that may be added when the kernel is
  /// dispatched.
  uint32_t workgroup_group_segment_byte_size;

  /// Number of byte of GDS required by kernel dispatch. Must be 0 if
  /// not using GDS.
  uint32_t gds_segment_byte_size;

  /// The size in bytes of the kernarg segment that holds the values
  /// of the arguments to the kernel. This could be used by CP to
  /// prefetch the kernarg segment pointed to by the dispatch packet.
  uint64_t kernarg_segment_byte_size;

  /// Number of fbarrier's used in the kernel and all functions it
  /// calls. If the implementation uses group memory to allocate the
  /// fbarriers then that amount must already be included in the
  /// workgroup_group_segment_byte_size total.
  uint32_t workgroup_fbarrier_count;

  /// Number of scalar registers used by a wavefront. This includes
  /// the special SGPRs for VCC, Flat Scratch Base, Flat Scratch Size
  /// and XNACK (for GFX8 (VI)). It does not include the 16 SGPR added if a
  /// trap handler is enabled. Used to set COMPUTE_PGM_RSRC1.SGPRS.
  uint16_t wavefront_sgpr_count;

  /// Number of vector registers used by each work-item. Used to set
  /// COMPUTE_PGM_RSRC1.VGPRS.
  uint16_t workitem_vgpr_count;

  /// If reserved_vgpr_count is 0 then must be 0. Otherwise, this is the
  /// first fixed VGPR number reserved.
  uint16_t reserved_vgpr_first;

  /// The number of consecutive VGPRs reserved by the client. If
  /// is_debug_supported then this count includes VGPRs reserved
  /// for debugger use.
  uint16_t reserved_vgpr_count;

  /// If reserved_sgpr_count is 0 then must be 0. Otherwise, this is the
  /// first fixed SGPR number reserved.
  uint16_t reserved_sgpr_first;

  /// The number of consecutive SGPRs reserved by the client. If
  /// is_debug_supported then this count includes SGPRs reserved
  /// for debugger use.
  uint16_t reserved_sgpr_count;

  /// If is_debug_supported is 0 then must be 0. Otherwise, this is the
  /// fixed SGPR number used to hold the wave scratch offset for the
  /// entire kernel execution, or uint16_t(-1) if the register is not
  /// used or not known.
  uint16_t debug_wavefront_private_segment_offset_sgpr;

  /// If is_debug_supported is 0 then must be 0. Otherwise, this is the
  /// fixed SGPR number of the first of 4 SGPRs used to hold the
  /// scratch V# used for the entire kernel execution, or uint16_t(-1)
  /// if the registers are not used or not known.
  uint16_t debug_private_segment_buffer_sgpr;

  /// The maximum byte alignment of variables used by the kernel in
  /// the specified memory segment. Expressed as a power of two. Must
  /// be at least HSA_POWERTWO_16.
  uint8_t kernarg_segment_alignment;
  uint8_t group_segment_alignment;
  uint8_t private_segment_alignment;

  /// Wavefront size expressed as a power of two. Must be a power of 2
  /// in range 1..64 inclusive. Used to support runtime query that
  /// obtains wavefront size, which may be used by application to
  /// allocated dynamic group memory and set the dispatch work-group
  /// size.
  uint8_t wavefront_size;

  int32_t call_convention;
  uint8_t reserved3[12];
  uint64_t runtime_loader_kernel_symbol;
  uint64_t control_directives[16];
} amd_kernel_code_t;

#endif // AMDKERNELCODET_H
