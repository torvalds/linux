/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _BASE_HWCONFIG_H_
#define _BASE_HWCONFIG_H_

/** Clarifications requested to First Vertex Index.
 *  Note this reference needs to be replaced with a proper issue raised against the HW Beta.
 */
#define BASE_HW_ISSUE_999   1

/** Incorrect handling of unorm16 pixel formats. */
#define BASE_HW_ISSUE_4015  1

/** Tiler triggers a fault if the scissor rectangle is empty. */
#define BASE_HW_ISSUE_5699  1

/** Soft-stopped jobs should cause the job slot to stall until the software has cleared the IRQ. */
#define BASE_HW_ISSUE_5713  1

/* The current version of the model doesn't support Soft-Stop */
#define BASE_HW_ISSUE_5736  0

/** Framebuffer output smaller than 6 pixels causes hang. */
#define BASE_HW_ISSUE_5753  0
#define BASE_HW_ISSUE_5753_ext  1

/* Transaction Elimination doesn't work correctly. */
#define BASE_HW_ISSUE_5907  1

/* Multisample write mask must be set to all 1s. */
#define BASE_HW_ISSUE_5936  1

/* Jobs can get stuck after page fault */
#define BASE_HW_ISSUE_6035 1

/* Hierarchical tiling doesn't work properly. */
#define BASE_HW_ISSUE_6097  1

/* Depth texture read of D24S8 hangs the FPGA */
#define BASE_HW_ISSUE_6156  1

/* GPU_COMMAND completion is not visible */
#define BASE_HW_ISSUE_6315  1

/* Readback with negative stride doesn't work properly. */
#define BASE_HW_ISSUE_6325  1

/* Using 8xMSAA surfaces produces incorrect output */
#define BASE_HW_ISSUE_6352  1

/* Need way to guarantee that all previously-translated memory accesses are commited */
#define BASE_HW_ISSUE_6367  1

/* Pixel format 95 doesn't work properly (HW writes to memory) */
#define BASE_HW_ISSUE_6405  1

/* Point size arrays using half-floats may be read out of order. */
#define BASE_HW_ISSUE_6676  1

/* On job complete with non-done the cache is not flushed */
#define BASE_HW_ISSUE_6787  1

/* There is no interrupt when a Performance Counters dump is completed */
#define BASE_HW_ISSUE_7115  1

/* The clamp integer coordinate flag bit of the sampler descriptor is reserved */
#define BASE_HW_ISSUE_7144  1

/* Descriptor Cache usage-counter issue */
#define BASE_HW_ISSUE_7347  1

/* Writing to averaging mode MULTISAMPLE might hang */
#define BASE_HW_ISSUE_7516 1

/* Nested page faults not visible to SW */
#define BASE_HW_ISSUE_7660  1

/* Hang when doing 4x multisampled writeback with transaction elimination enabled */
#define BASE_HW_ISSUE_8142 1

/* Write of PRFCNT_CONFIG_MODE_MANUAL to PRFCNT_CONFIG causes a instrumentation dump if
   PRFCNT_TILER_EN is enabled */
#define BASE_HW_ISSUE_8186  1

/** Hierz doesn't work when stenciling is enabled */
#define BASE_HW_ISSUE_8260  1

/** uTLB deadlock could occur when writing to an invalid page at the same time as
 * access to a valid page in the same uTLB cache line ( == 4 PTEs == 16K block of mapping) */
#define BASE_HW_ISSUE_8316  1

/* Livelock in L0 icache */
#define BASE_HW_ISSUE_8280  1

/* TIB: Reports faults from a vtile which has not yet been allocated */
#define BASE_HW_ISSUE_8245  1

/* HT: TERMINATE for RUN command ignored if previous LOAD_DESCRIPTOR is still executing */
#define BASE_HW_ISSUE_8394  0

/* CSE : Sends a TERMINATED response for a task that should not be terminated */
/* (Note that PRLAM-8379 also uses this workaround) */
#define BASE_HW_ISSUE_8401  1

/* Repeatedly Soft-stopping a job chain consisting of (Vertex Shader, Cache Flush, Tiler)
 * jobs causes 0x58 error on tiler job. */
#define BASE_HW_ISSUE_8408 1

/* Compute job hangs: disable the Pause buffer in the LS pipe.
 * BASE_HW_ISSUE_8443 implemented at run-time for GPUs with GPU ID
 * 0x69560000 and 0x69560001 (beta-eco1 and r0p0-15dev0)
 */

/** Tiler heap issue using FBOs or multiple processes using the tiler simultaneously.
 *  Workaround is disabled on beta since the bug only occurs when hierarchical tiling is enabled.
 */
#define BASE_HW_ISSUE_8564 0

/* Jobs with relaxed dependencies are not supporting soft-stop */
#define BASE_HW_ISSUE_8803 1

/* Boolean occlusion queries don't work properly due to sdc issue. */
#define BASE_HW_ISSUE_8986 0

/* Occlusion query result is not updated if color writes are disabled. */
#define BASE_HW_ISSUE_9010 0

/* Occlusion queries can create false 0 result in boolean and counter modes */
#define BASE_HW_ISSUE_8879 1

/* The whole tiler pointer array must be cleared */
#define BASE_HW_ISSUE_9102 1

/* Blend shader output is wrong for certain formats */
#define BASE_HW_ISSUE_8833 1

/* RSD and DCD structures are incorrectly uncached in GPU L2 */
#define BASE_HW_ISSUE_6494 1

/* Stencil test enable 1->0 sticks */
#define BASE_HW_ISSUE_8456 1

/* YUV image dimensions are specified in chroma samples, not luma samples. */
#define BASE_HW_ISSUE_6996 1

/* BASE_MEM_COHERENT_LOCAL does not work on beta HW */
#define BASE_HW_ISSUE_9235 1

#endif /* _BASE_HWCONFIG_H_ */
