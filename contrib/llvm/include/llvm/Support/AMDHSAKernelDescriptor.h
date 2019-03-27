//===--- AMDHSAKernelDescriptor.h -----------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDHSA kernel descriptor definitions. For more information, visit
/// https://llvm.org/docs/AMDGPUUsage.html#kernel-descriptor
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H
#define LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H

#include <cstddef>
#include <cstdint>

// Gets offset of specified member in specified type.
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE*)0)->MEMBER)
#endif // offsetof

// Creates enumeration entries used for packing bits into integers. Enumeration
// entries include bit shift amount, bit width, and bit mask.
#ifndef AMDHSA_BITS_ENUM_ENTRY
#define AMDHSA_BITS_ENUM_ENTRY(NAME, SHIFT, WIDTH) \
  NAME ## _SHIFT = (SHIFT),                        \
  NAME ## _WIDTH = (WIDTH),                        \
  NAME = (((1 << (WIDTH)) - 1) << (SHIFT))
#endif // AMDHSA_BITS_ENUM_ENTRY

// Gets bits for specified bit mask from specified source.
#ifndef AMDHSA_BITS_GET
#define AMDHSA_BITS_GET(SRC, MSK) ((SRC & MSK) >> MSK ## _SHIFT)
#endif // AMDHSA_BITS_GET

// Sets bits for specified bit mask in specified destination.
#ifndef AMDHSA_BITS_SET
#define AMDHSA_BITS_SET(DST, MSK, VAL)  \
  DST &= ~MSK;                          \
  DST |= ((VAL << MSK ## _SHIFT) & MSK)
#endif // AMDHSA_BITS_SET

namespace llvm {
namespace amdhsa {

// Floating point rounding modes. Must match hardware definition.
enum : uint8_t {
  FLOAT_ROUND_MODE_NEAR_EVEN = 0,
  FLOAT_ROUND_MODE_PLUS_INFINITY = 1,
  FLOAT_ROUND_MODE_MINUS_INFINITY = 2,
  FLOAT_ROUND_MODE_ZERO = 3,
};

// Floating point denorm modes. Must match hardware definition.
enum : uint8_t {
  FLOAT_DENORM_MODE_FLUSH_SRC_DST = 0,
  FLOAT_DENORM_MODE_FLUSH_DST = 1,
  FLOAT_DENORM_MODE_FLUSH_SRC = 2,
  FLOAT_DENORM_MODE_FLUSH_NONE = 3,
};

// System VGPR workitem IDs. Must match hardware definition.
enum : uint8_t {
  SYSTEM_VGPR_WORKITEM_ID_X = 0,
  SYSTEM_VGPR_WORKITEM_ID_X_Y = 1,
  SYSTEM_VGPR_WORKITEM_ID_X_Y_Z = 2,
  SYSTEM_VGPR_WORKITEM_ID_UNDEFINED = 3,
};

// Compute program resource register 1. Must match hardware definition.
#define COMPUTE_PGM_RSRC1(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_ ## NAME, SHIFT, WIDTH)
enum : int32_t {
  COMPUTE_PGM_RSRC1(GRANULATED_WORKITEM_VGPR_COUNT, 0, 6),
  COMPUTE_PGM_RSRC1(GRANULATED_WAVEFRONT_SGPR_COUNT, 6, 4),
  COMPUTE_PGM_RSRC1(PRIORITY, 10, 2),
  COMPUTE_PGM_RSRC1(FLOAT_ROUND_MODE_32, 12, 2),
  COMPUTE_PGM_RSRC1(FLOAT_ROUND_MODE_16_64, 14, 2),
  COMPUTE_PGM_RSRC1(FLOAT_DENORM_MODE_32, 16, 2),
  COMPUTE_PGM_RSRC1(FLOAT_DENORM_MODE_16_64, 18, 2),
  COMPUTE_PGM_RSRC1(PRIV, 20, 1),
  COMPUTE_PGM_RSRC1(ENABLE_DX10_CLAMP, 21, 1),
  COMPUTE_PGM_RSRC1(DEBUG_MODE, 22, 1),
  COMPUTE_PGM_RSRC1(ENABLE_IEEE_MODE, 23, 1),
  COMPUTE_PGM_RSRC1(BULKY, 24, 1),
  COMPUTE_PGM_RSRC1(CDBG_USER, 25, 1),
  COMPUTE_PGM_RSRC1(FP16_OVFL, 26, 1), // GFX9+
  COMPUTE_PGM_RSRC1(RESERVED0, 27, 5),
};
#undef COMPUTE_PGM_RSRC1

// Compute program resource register 2. Must match hardware definition.
#define COMPUTE_PGM_RSRC2(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_ ## NAME, SHIFT, WIDTH)
enum : int32_t {
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_PRIVATE_SEGMENT_WAVEFRONT_OFFSET, 0, 1),
  COMPUTE_PGM_RSRC2(USER_SGPR_COUNT, 1, 5),
  COMPUTE_PGM_RSRC2(ENABLE_TRAP_HANDLER, 6, 1),
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_X, 7, 1),
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_Y, 8, 1),
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_Z, 9, 1),
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_INFO, 10, 1),
  COMPUTE_PGM_RSRC2(ENABLE_VGPR_WORKITEM_ID, 11, 2),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_ADDRESS_WATCH, 13, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_MEMORY, 14, 1),
  COMPUTE_PGM_RSRC2(GRANULATED_LDS_SIZE, 15, 9),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_INVALID_OPERATION, 24, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_FP_DENORMAL_SOURCE, 25, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_DIVISION_BY_ZERO, 26, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_OVERFLOW, 27, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_UNDERFLOW, 28, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_INEXACT, 29, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_INT_DIVIDE_BY_ZERO, 30, 1),
  COMPUTE_PGM_RSRC2(RESERVED0, 31, 1),
};
#undef COMPUTE_PGM_RSRC2

// Kernel code properties. Must be kept backwards compatible.
#define KERNEL_CODE_PROPERTY(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(KERNEL_CODE_PROPERTY_ ## NAME, SHIFT, WIDTH)
enum : int32_t {
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER, 0, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_DISPATCH_PTR, 1, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_QUEUE_PTR, 2, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_KERNARG_SEGMENT_PTR, 3, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_DISPATCH_ID, 4, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_FLAT_SCRATCH_INIT, 5, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_PRIVATE_SEGMENT_SIZE, 6, 1),
  KERNEL_CODE_PROPERTY(RESERVED0, 7, 9),
};
#undef KERNEL_CODE_PROPERTY

// Kernel descriptor. Must be kept backwards compatible.
struct kernel_descriptor_t {
  uint32_t group_segment_fixed_size;
  uint32_t private_segment_fixed_size;
  uint8_t reserved0[8];
  int64_t kernel_code_entry_byte_offset;
  uint8_t reserved1[24];
  uint32_t compute_pgm_rsrc1;
  uint32_t compute_pgm_rsrc2;
  uint16_t kernel_code_properties;
  uint8_t reserved2[6];
};

static_assert(
    sizeof(kernel_descriptor_t) == 64,
    "invalid size for kernel_descriptor_t");
static_assert(
    offsetof(kernel_descriptor_t, group_segment_fixed_size) == 0,
    "invalid offset for group_segment_fixed_size");
static_assert(
    offsetof(kernel_descriptor_t, private_segment_fixed_size) == 4,
    "invalid offset for private_segment_fixed_size");
static_assert(
    offsetof(kernel_descriptor_t, reserved0) == 8,
    "invalid offset for reserved0");
static_assert(
    offsetof(kernel_descriptor_t, kernel_code_entry_byte_offset) == 16,
    "invalid offset for kernel_code_entry_byte_offset");
static_assert(
    offsetof(kernel_descriptor_t, reserved1) == 24,
    "invalid offset for reserved1");
static_assert(
    offsetof(kernel_descriptor_t, compute_pgm_rsrc1) == 48,
    "invalid offset for compute_pgm_rsrc1");
static_assert(
    offsetof(kernel_descriptor_t, compute_pgm_rsrc2) == 52,
    "invalid offset for compute_pgm_rsrc2");
static_assert(
    offsetof(kernel_descriptor_t, kernel_code_properties) == 56,
    "invalid offset for kernel_code_properties");
static_assert(
    offsetof(kernel_descriptor_t, reserved2) == 58,
    "invalid offset for reserved2");

} // end namespace amdhsa
} // end namespace llvm

#endif // LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H
