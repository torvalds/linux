/* SPDX-License-Identifier: MIT */
#ifndef __NVRM_NVTYPES_H__
#define __NVRM_NVTYPES_H__

#define NV_ALIGN_BYTES(a) __attribute__ ((__aligned__(a)))
#define NV_DECLARE_ALIGNED(f,a) f __attribute__ ((__aligned__(a)))

typedef u32 NvV32;

typedef u8 NvU8;
typedef u16 NvU16;
typedef u32 NvU32;
typedef u64 NvU64;

typedef void* NvP64;

typedef NvU8 NvBool;
typedef NvU32 NvHandle;
typedef NvU64 NvLength;

typedef NvU64 RmPhysAddr;

typedef NvU32 NV_STATUS;

typedef union {} rpc_generic_union;
#endif
