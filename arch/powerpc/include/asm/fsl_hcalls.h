/*
 * Freescale hypervisor call interface
 *
 * Copyright 2008-2010 Freescale Semiconductor, Inc.
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * This file is provided under a dual BSD/GPL license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _FSL_HCALLS_H
#define _FSL_HCALLS_H

#include <linux/types.h>
#include <linux/errno.h>
#include <asm/byteorder.h>
#include <asm/epapr_hcalls.h>

#define FH_API_VERSION			1

#define FH_ERR_GET_INFO			1
#define FH_PARTITION_GET_DTPROP		2
#define FH_PARTITION_SET_DTPROP		3
#define FH_PARTITION_RESTART		4
#define FH_PARTITION_GET_STATUS		5
#define FH_PARTITION_START		6
#define FH_PARTITION_STOP		7
#define FH_PARTITION_MEMCPY		8
#define FH_DMA_ENABLE			9
#define FH_DMA_DISABLE			10
#define FH_SEND_NMI			11
#define FH_VMPIC_GET_MSIR		12
#define FH_SYSTEM_RESET			13
#define FH_GET_CORE_STATE		14
#define FH_ENTER_NAP			15
#define FH_EXIT_NAP			16
#define FH_CLAIM_DEVICE			17
#define FH_PARTITION_STOP_DMA		18

/* vendor ID: Freescale Semiconductor */
#define FH_HCALL_TOKEN(num)		_EV_HCALL_TOKEN(EV_FSL_VENDOR_ID, num)

/*
 * We use "uintptr_t" to define a register because it's guaranteed to be a
 * 32-bit integer on a 32-bit platform, and a 64-bit integer on a 64-bit
 * platform.
 *
 * All registers are either input/output or output only.  Registers that are
 * initialized before making the hypercall are input/output.  All
 * input/output registers are represented with "+r".  Output-only registers
 * are represented with "=r".  Do not specify any unused registers.  The
 * clobber list will tell the compiler that the hypercall modifies those
 * registers, which is good enough.
 */

/**
 * fh_send_nmi - send NMI to virtual cpu(s).
 * @vcpu_mask: send NMI to virtual cpu(s) specified by this mask.
 *
 * Returns 0 for success, or EINVAL for invalid vcpu_mask.
 */
static inline unsigned int fh_send_nmi(unsigned int vcpu_mask)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = FH_HCALL_TOKEN(FH_SEND_NMI);
	r3 = vcpu_mask;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}

/* Arbitrary limits to avoid excessive memory allocation in hypervisor */
#define FH_DTPROP_MAX_PATHLEN 4096
#define FH_DTPROP_MAX_PROPLEN 32768

/**
 * fh_partiton_get_dtprop - get a property from a guest device tree.
 * @handle: handle of partition whose device tree is to be accessed
 * @dtpath_addr: physical address of device tree path to access
 * @propname_addr: physical address of name of property
 * @propvalue_addr: physical address of property value buffer
 * @propvalue_len: length of buffer on entry, length of property on return
 *
 * Returns zero on success, non-zero on error.
 */
static inline unsigned int fh_partition_get_dtprop(int handle,
						   uint64_t dtpath_addr,
						   uint64_t propname_addr,
						   uint64_t propvalue_addr,
						   uint32_t *propvalue_len)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");
	register uintptr_t r6 __asm__("r6");
	register uintptr_t r7 __asm__("r7");
	register uintptr_t r8 __asm__("r8");
	register uintptr_t r9 __asm__("r9");
	register uintptr_t r10 __asm__("r10");

	r11 = FH_HCALL_TOKEN(FH_PARTITION_GET_DTPROP);
	r3 = handle;

#ifdef CONFIG_PHYS_64BIT
	r4 = dtpath_addr >> 32;
	r6 = propname_addr >> 32;
	r8 = propvalue_addr >> 32;
#else
	r4 = 0;
	r6 = 0;
	r8 = 0;
#endif
	r5 = (uint32_t)dtpath_addr;
	r7 = (uint32_t)propname_addr;
	r9 = (uint32_t)propvalue_addr;
	r10 = *propvalue_len;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11),
		  "+r" (r3), "+r" (r4), "+r" (r5), "+r" (r6), "+r" (r7),
		  "+r" (r8), "+r" (r9), "+r" (r10)
		: : EV_HCALL_CLOBBERS8
	);

	*propvalue_len = r4;
	return r3;
}

/**
 * Set a property in a guest device tree.
 * @handle: handle of partition whose device tree is to be accessed
 * @dtpath_addr: physical address of device tree path to access
 * @propname_addr: physical address of name of property
 * @propvalue_addr: physical address of property value
 * @propvalue_len: length of property
 *
 * Returns zero on success, non-zero on error.
 */
static inline unsigned int fh_partition_set_dtprop(int handle,
						   uint64_t dtpath_addr,
						   uint64_t propname_addr,
						   uint64_t propvalue_addr,
						   uint32_t propvalue_len)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r6 __asm__("r6");
	register uintptr_t r8 __asm__("r8");
	register uintptr_t r5 __asm__("r5");
	register uintptr_t r7 __asm__("r7");
	register uintptr_t r9 __asm__("r9");
	register uintptr_t r10 __asm__("r10");

	r11 = FH_HCALL_TOKEN(FH_PARTITION_SET_DTPROP);
	r3 = handle;

#ifdef CONFIG_PHYS_64BIT
	r4 = dtpath_addr >> 32;
	r6 = propname_addr >> 32;
	r8 = propvalue_addr >> 32;
#else
	r4 = 0;
	r6 = 0;
	r8 = 0;
#endif
	r5 = (uint32_t)dtpath_addr;
	r7 = (uint32_t)propname_addr;
	r9 = (uint32_t)propvalue_addr;
	r10 = propvalue_len;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11),
		  "+r" (r3), "+r" (r4), "+r" (r5), "+r" (r6), "+r" (r7),
		  "+r" (r8), "+r" (r9), "+r" (r10)
		: : EV_HCALL_CLOBBERS8
	);

	return r3;
}

/**
 * fh_partition_restart - reboot the current partition
 * @partition: partition ID
 *
 * Returns an error code if reboot failed.  Does not return if it succeeds.
 */
static inline unsigned int fh_partition_restart(unsigned int partition)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = FH_HCALL_TOKEN(FH_PARTITION_RESTART);
	r3 = partition;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}

#define FH_PARTITION_STOPPED	0
#define FH_PARTITION_RUNNING	1
#define FH_PARTITION_STARTING	2
#define FH_PARTITION_STOPPING	3
#define FH_PARTITION_PAUSING	4
#define FH_PARTITION_PAUSED	5
#define FH_PARTITION_RESUMING	6

/**
 * fh_partition_get_status - gets the status of a partition
 * @partition: partition ID
 * @status: returned status code
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_partition_get_status(unsigned int partition,
	unsigned int *status)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");

	r11 = FH_HCALL_TOKEN(FH_PARTITION_GET_STATUS);
	r3 = partition;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "=r" (r4)
		: : EV_HCALL_CLOBBERS2
	);

	*status = r4;

	return r3;
}

/**
 * fh_partition_start - boots and starts execution of the specified partition
 * @partition: partition ID
 * @entry_point: guest physical address to start execution
 *
 * The hypervisor creates a 1-to-1 virtual/physical IMA mapping, so at boot
 * time, guest physical address are the same as guest virtual addresses.
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_partition_start(unsigned int partition,
	uint32_t entry_point, int load)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");

	r11 = FH_HCALL_TOKEN(FH_PARTITION_START);
	r3 = partition;
	r4 = entry_point;
	r5 = load;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "+r" (r4), "+r" (r5)
		: : EV_HCALL_CLOBBERS3
	);

	return r3;
}

/**
 * fh_partition_stop - stops another partition
 * @partition: partition ID
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_partition_stop(unsigned int partition)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = FH_HCALL_TOKEN(FH_PARTITION_STOP);
	r3 = partition;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}

/**
 * struct fh_sg_list: definition of the fh_partition_memcpy S/G list
 * @source: guest physical address to copy from
 * @target: guest physical address to copy to
 * @size: number of bytes to copy
 * @reserved: reserved, must be zero
 *
 * The scatter/gather list for fh_partition_memcpy() is an array of these
 * structures.  The array must be guest physically contiguous.
 *
 * This structure must be aligned on 32-byte boundary, so that no single
 * strucuture can span two pages.
 */
struct fh_sg_list {
	uint64_t source;   /**< guest physical address to copy from */
	uint64_t target;   /**< guest physical address to copy to */
	uint64_t size;     /**< number of bytes to copy */
	uint64_t reserved; /**< reserved, must be zero */
} __attribute__ ((aligned(32)));

/**
 * fh_partition_memcpy - copies data from one guest to another
 * @source: the ID of the partition to copy from
 * @target: the ID of the partition to copy to
 * @sg_list: guest physical address of an array of &fh_sg_list structures
 * @count: the number of entries in @sg_list
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_partition_memcpy(unsigned int source,
	unsigned int target, phys_addr_t sg_list, unsigned int count)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");
	register uintptr_t r6 __asm__("r6");
	register uintptr_t r7 __asm__("r7");

	r11 = FH_HCALL_TOKEN(FH_PARTITION_MEMCPY);
	r3 = source;
	r4 = target;
	r5 = (uint32_t) sg_list;

#ifdef CONFIG_PHYS_64BIT
	r6 = sg_list >> 32;
#else
	r6 = 0;
#endif
	r7 = count;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11),
		  "+r" (r3), "+r" (r4), "+r" (r5), "+r" (r6), "+r" (r7)
		: : EV_HCALL_CLOBBERS5
	);

	return r3;
}

/**
 * fh_dma_enable - enable DMA for the specified device
 * @liodn: the LIODN of the I/O device for which to enable DMA
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_dma_enable(unsigned int liodn)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = FH_HCALL_TOKEN(FH_DMA_ENABLE);
	r3 = liodn;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}

/**
 * fh_dma_disable - disable DMA for the specified device
 * @liodn: the LIODN of the I/O device for which to disable DMA
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_dma_disable(unsigned int liodn)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = FH_HCALL_TOKEN(FH_DMA_DISABLE);
	r3 = liodn;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}


/**
 * fh_vmpic_get_msir - returns the MPIC-MSI register value
 * @interrupt: the interrupt number
 * @msir_val: returned MPIC-MSI register value
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_vmpic_get_msir(unsigned int interrupt,
	unsigned int *msir_val)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");

	r11 = FH_HCALL_TOKEN(FH_VMPIC_GET_MSIR);
	r3 = interrupt;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "=r" (r4)
		: : EV_HCALL_CLOBBERS2
	);

	*msir_val = r4;

	return r3;
}

/**
 * fh_system_reset - reset the system
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_system_reset(void)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = FH_HCALL_TOKEN(FH_SYSTEM_RESET);

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "=r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}


/**
 * fh_err_get_info - get platform error information
 * @queue id:
 * 0 for guest error event queue
 * 1 for global error event queue
 *
 * @pointer to store the platform error data:
 * platform error data is returned in registers r4 - r11
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_err_get_info(int queue, uint32_t *bufsize,
	uint32_t addr_hi, uint32_t addr_lo, int peek)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");
	register uintptr_t r6 __asm__("r6");
	register uintptr_t r7 __asm__("r7");

	r11 = FH_HCALL_TOKEN(FH_ERR_GET_INFO);
	r3 = queue;
	r4 = *bufsize;
	r5 = addr_hi;
	r6 = addr_lo;
	r7 = peek;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "+r" (r4), "+r" (r5), "+r" (r6),
		  "+r" (r7)
		: : EV_HCALL_CLOBBERS5
	);

	*bufsize = r4;

	return r3;
}


#define FH_VCPU_RUN	0
#define FH_VCPU_IDLE	1
#define FH_VCPU_NAP	2

/**
 * fh_get_core_state - get the state of a vcpu
 *
 * @handle: handle of partition containing the vcpu
 * @vcpu: vcpu number within the partition
 * @state:the current state of the vcpu, see FH_VCPU_*
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_get_core_state(unsigned int handle,
	unsigned int vcpu, unsigned int *state)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");

	r11 = FH_HCALL_TOKEN(FH_GET_CORE_STATE);
	r3 = handle;
	r4 = vcpu;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "+r" (r4)
		: : EV_HCALL_CLOBBERS2
	);

	*state = r4;
	return r3;
}

/**
 * fh_enter_nap - enter nap on a vcpu
 *
 * Note that though the API supports entering nap on a vcpu other
 * than the caller, this may not be implmented and may return EINVAL.
 *
 * @handle: handle of partition containing the vcpu
 * @vcpu: vcpu number within the partition
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_enter_nap(unsigned int handle, unsigned int vcpu)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");

	r11 = FH_HCALL_TOKEN(FH_ENTER_NAP);
	r3 = handle;
	r4 = vcpu;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "+r" (r4)
		: : EV_HCALL_CLOBBERS2
	);

	return r3;
}

/**
 * fh_exit_nap - exit nap on a vcpu
 * @handle: handle of partition containing the vcpu
 * @vcpu: vcpu number within the partition
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_exit_nap(unsigned int handle, unsigned int vcpu)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");

	r11 = FH_HCALL_TOKEN(FH_EXIT_NAP);
	r3 = handle;
	r4 = vcpu;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "+r" (r4)
		: : EV_HCALL_CLOBBERS2
	);

	return r3;
}
/**
 * fh_claim_device - claim a "claimable" shared device
 * @handle: fsl,hv-device-handle of node to claim
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_claim_device(unsigned int handle)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = FH_HCALL_TOKEN(FH_CLAIM_DEVICE);
	r3 = handle;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}

/**
 * Run deferred DMA disabling on a partition's private devices
 *
 * This applies to devices which a partition owns either privately,
 * or which are claimable and still actively owned by that partition,
 * and which do not have the no-dma-disable property.
 *
 * @handle: partition (must be stopped) whose DMA is to be disabled
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int fh_partition_stop_dma(unsigned int handle)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = FH_HCALL_TOKEN(FH_PARTITION_STOP_DMA);
	r3 = handle;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}
#endif
