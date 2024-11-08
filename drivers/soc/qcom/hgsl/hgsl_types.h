/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2006-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __HGSL_TYPES_H
#define __HGSL_TYPES_H

#include <linux/stddef.h>

/****************************************************************************/
/* status                                                                   */
/****************************************************************************/
#define GSL_SUCCESS                          0
#define GSL_FAILURE                         -1
#define GSL_FAILURE_SYSTEMERROR             -2
#define GSL_FAILURE_DEVICEERROR             -3
#define GSL_FAILURE_OUTOFMEM                -4
#define GSL_FAILURE_BADPARAM                -5
#define GSL_FAILURE_NOTSUPPORTED            -6
#define GSL_FAILURE_NOMOREAVAILABLE         -7
#define GSL_FAILURE_NOTINITIALIZED          -8
#define GSL_FAILURE_ALREADYINITIALIZED      -9
#define GSL_FAILURE_TIMEOUT                 -10
#define GSL_FAILURE_OFFSETINVALID           -11
#define GSL_FAILURE_CTXT_DEADLOCK           -12
#define GSL_FAILURE_PERFCOUNTER_UNAVAILABLE -13
#define GSL_FAILURE_HANG                    -14
#define GSL_FAILURE_RETRY                   -15

#define GSL_FLAGS_INITIALIZED           0x00000004
#define GSL_MEMFLAGS_VM                 0x80000000

/****************************************************************************/
/* memory allocation flags                                                  */
/****************************************************************************/
#define GSL_MEMFLAGS_ANY                0x00000000 /* dont care */
#define GSL_MEMFLAGS_ALIGNANY           0x00000000
#define GSL_MEMFLAGS_ALIGN32            0x00000000
#define GSL_MEMFLAGS_ALIGN64            0x00060000
#define GSL_MEMFLAGS_ALIGN128           0x00070000
#define GSL_MEMFLAGS_ALIGN256           0x00080000
#define GSL_MEMFLAGS_ALIGN512           0x00090000
#define GSL_MEMFLAGS_ALIGN1K            0x000A0000
#define GSL_MEMFLAGS_ALIGN2K            0x000B0000
#define GSL_MEMFLAGS_ALIGN4K            0x000C0000
#define GSL_MEMFLAGS_ALIGN8K            0x000D0000
#define GSL_MEMFLAGS_ALIGN16K           0x000E0000
#define GSL_MEMFLAGS_ALIGN32K           0x000F0000
#define GSL_MEMFLAGS_ALIGN64K           0x00100000
#define GSL_MEMFLAGS_ALIGN1MB           0x00140000
#define GSL_MEMFLAGS_ALIGNPAGE          GSL_MEMFLAGS_ALIGN4K

#define GSL_MEMFLAGS_GPUREADWRITE       0x00000000
#define GSL_MEMFLAGS_GPUREADONLY        0x01000000
#define GSL_MEMFLAGS_GPUWRITEONLY       0x02000000
#define GSL_MEMFLAGS_GPUNOACCESS        0x03000000

#define GSL_MEMFLAGS_WRITECOMBINE       0x00000000
#define GSL_MEMFLAGS_PROTECTED          0x00000008 /* protected buffer flag*/
#define GSL_MEMFLAGS_UNCACHED           0x04000000
#define GSL_MEMFLAGS_WRITETHROUGH       0x08000000
#define GSL_MEMFLAGS_WRITEBACK          0x0C000000

#define GSL_MEMFLAGS_USE_CPU_MAP        0x10000000
#define GSL_MEMFLAGS_CONTIGUOUS         0x20000000
#define GSL_MEMFLAGS_FORCEPAGESIZE      0x40000000
#define GSL_MEMFLAGS_GPUIOCOHERENT      0x80000000
#define GSL_MEMFLAGS_CACHEMODE_MASK     0x0C000000

/****************************************************************************/
/* cache flags                                                              */
/****************************************************************************/
#define GSL_CACHEFLAGS_CLEAN            0x00000001
#define GSL_CACHEFLAGS_TO_GPU           GSL_CACHEFLAGS_CLEAN
#define GSL_CACHEFLAGS_INVALIDATE       0x00000002
#define GSL_CACHEFLAGS_FROM_GPU         GSL_CACHEFLAGS_INVALIDATE
#define GSL_CACHEFLAGS_FLUSH (GSL_CACHEFLAGS_CLEAN | GSL_CACHEFLAGS_INVALIDATE)

/****************************************************************************/
/*  context                                                                 */
/****************************************************************************/
#define GSL_CONTEXT_FLAG_USER_GENERATED_TS    0x00000080
#define GSL_CONTEXT_FLAG_BIND                 0x00040000
#define GSL_CONTEXT_FLAG_CLIENT_GENERATED_TS  0x80000000

/****************************************************************************/
/* other                                                                    */
/****************************************************************************/
#define GSL_TIMEOUT_NONE                      0
#define GSL_TIMEOUT_INFINITE                  0xFFFFFFFF
#define GSL_TIMEOUT_DEFAULT                   GSL_TIMEOUT_INFINITE
#define GSL_RPC_WAITTIMESTAMP_SLICE           1000

#define GSL_PAGESIZE                          0x1000
#define GSL_PAGESIZE_SHIFT                    12

#define GSL_TRUE                              1
#define GSL_FALSE                             0

#define GSL_EINVAL                            -1

/* ib desc of cmdbatch profiling buffer */
#define GSL_IBDESC_PROFILING_BUFFER           0x00000002

/*************/
/* device id */
/*************/
enum gsl_deviceid_t {
	GSL_DEVICE_UNUSED = -1,	/* gcc compiler warning fix, unsigned->signed */
	GSL_DEVICE_ANY    = 0,
	GSL_DEVICE_3D     = 1,
	GSL_DEVICE_0      = 2,
	GSL_DEVICE_1      = 3,

	GSL_DEVICE_FOOBAR = 0x7FFFFFFF
};

enum gsl_devhandle_t {
	GSL_HANDLE_NULL   = 0,
	GSL_HANDLE_DEV0   = 1,
	GSL_HANDLE_DEV1   = 2
};

/****************************/
/* shared memory allocation */
/****************************/
struct gsl_memdesc_t {
	union {
		void         *hostptr;
		uint64_t     hostptr64;
	};
	uint64_t             gpuaddr;
	union {
		unsigned int size;
		uint64_t     size64;
	};
	uint64_t             flags;
	union {
		uintptr_t    priv;
		uint64_t     priv64;
	};
};

struct gsl_command_buffer_object_t {
	struct gsl_memdesc_t		*memdesc;
	uint64_t			sizedwords;
	uint64_t			offset;
	uint64_t			flags;
};

struct gsl_memory_object_t {
	struct gsl_memdesc_t		*memdesc;
	uint64_t			sizedwords;
	uint64_t			offset;
	uint64_t			flags;
};

/****************/
/* timestamp id */
/****************/
enum gsl_timestamp_type_t {
	GSL_TIMESTAMP_CONSUMED = 1, /* start-of-pipeline timestamp */
	GSL_TIMESTAMP_RETIRED  = 2, /* end-of-pipeline timestamp */
	GSL_TIMESTAMP_QUEUED   = 3, /* Timestamp of last submitted IB */
	GSL_TIMESTAMP_MAX      = 3,

	GSL_TIMESTAMP_FOOBAR   = 0x7FFFFFFF
};

enum gsl_context_type_t {
	GSL_CONTEXT_TYPE_GENERIC = 1,
	GSL_CONTEXT_TYPE_OPENGL	 = 2,
	GSL_CONTEXT_TYPE_OPENVG	 = 3,
	GSL_CONTEXT_TYPE_OPENCL	 = 4,
	GSL_CONTEXT_TYPE_C2D     = 5,
	GSL_CONTEXT_TYPE_RS      = 6,
	GSL_CONTEXT_TYPE_DX      = 7,
	GSL_CONTEXT_TYPE_VK      = 8,

	GSL_CONTEXT_TYPE_FOOBAR  = 0x7FFFFFFF
};


/*****************************/
/* Performance Counter Group */
/*****************************/
enum gsl_perfcountergroupid_t {
	GSL_PERF_COUNTER_GROUP_INVALID  = -1,
	GSL_PERF_COUNTER_GROUP_CP       = 0x0,
	GSL_PERF_COUNTER_GROUP_RBBM     = 0x1,
	GSL_PERF_COUNTER_GROUP_PC       = 0x2,
	GSL_PERF_COUNTER_GROUP_VFD      = 0x3,
	GSL_PERF_COUNTER_GROUP_HLSQ     = 0x4,
	GSL_PERF_COUNTER_GROUP_VPC      = 0x5,
	GSL_PERF_COUNTER_GROUP_TSE      = 0x6,
	GSL_PERF_COUNTER_GROUP_RAS      = 0x7,
	GSL_PERF_COUNTER_GROUP_UCHE     = 0x8,
	GSL_PERF_COUNTER_GROUP_TP       = 0x9,
	GSL_PERF_COUNTER_GROUP_SP       = 0xA,
	GSL_PERF_COUNTER_GROUP_RB       = 0xB,
	GSL_PERF_COUNTER_GROUP_PWR      = 0xC,
	GSL_PERF_COUNTER_GROUP_VBIF     = 0xD,
	GSL_PERF_COUNTER_GROUP_VBIF_PWR = 0xE,
	GSL_PERF_COUNTER_GROUP_MH       = 0xF,
	GSL_PERF_COUNTER_GROUP_PA_SU    = 0x10,
	GSL_PERF_COUNTER_GROUP_SQ       = 0x11,
	GSL_PERF_COUNTER_GROUP_SX       = 0x12,
	GSL_PERF_COUNTER_GROUP_TCF      = 0x13,
	GSL_PERF_COUNTER_GROUP_TCM      = 0x14,
	GSL_PERF_COUNTER_GROUP_TCR      = 0x15,
	GSL_PERF_COUNTER_GROUP_L2       = 0x16,
	GSL_PERF_COUNTER_GROUP_VSC      = 0x17,
	GSL_PERF_COUNTER_GROUP_CCU      = 0x18,
	GSL_PERF_COUNTER_GROUP_LRZ      = 0x19,
	GSL_PERF_COUNTER_GROUP_CMP      = 0x1A,
	GSL_PERF_COUNTER_GROUP_ALWAYSON = 0x1B,
	GSL_PERF_COUNTER_GROUP_SW       = 0x1C,
	GSL_PERF_COUNTER_GROUP_GMU_PWC  = 0x1D,
	GSL_PERF_COUNTER_GROUP_GLC      = 0x1E,
	GSL_PERF_COUNTER_GROUP_FCHE     = 0x1F,
	GSL_PERF_COUNTER_GROUP_MHUB     = 0x20,
	GSL_PERF_COUNTER_GROUP_MAX
};

/****************************************************************************/
/* system time usage                                                        */
/****************************************************************************/
enum gsl_systemtime_usage_t {
	GSL_SYSTEMTIME_GENERIC		= 0x0,
	GSL_SYSTEMTIME_CL_PROFILING	= 0x1,
};

#endif	/* __HGSL_TYPES_H */
