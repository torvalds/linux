/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/delay.h>
#include "hfi.h"
#include "qp.h"
#include "trace.h"

#define SC_CTXT_PACKET_EGRESS_TIMEOUT 350 /* in chip cycles */

#define SC(name) SEND_CTXT_##name
/*
 * Send Context functions
 */
static void sc_wait_for_packet_egress(struct send_context *sc, int pause);

/*
 * Set the CM reset bit and wait for it to clear.  Use the provided
 * sendctrl register.  This routine has no locking.
 */
void __cm_reset(struct hfi1_devdata *dd, u64 sendctrl)
{
	write_csr(dd, SEND_CTRL, sendctrl | SEND_CTRL_CM_RESET_SMASK);
	while (1) {
		udelay(1);
		sendctrl = read_csr(dd, SEND_CTRL);
		if ((sendctrl & SEND_CTRL_CM_RESET_SMASK) == 0)
			break;
	}
}

/* defined in header release 48 and higher */
#ifndef SEND_CTRL_UNSUPPORTED_VL_SHIFT
#define SEND_CTRL_UNSUPPORTED_VL_SHIFT 3
#define SEND_CTRL_UNSUPPORTED_VL_MASK 0xffull
#define SEND_CTRL_UNSUPPORTED_VL_SMASK (SEND_CTRL_UNSUPPORTED_VL_MASK \
		<< SEND_CTRL_UNSUPPORTED_VL_SHIFT)
#endif

/* global control of PIO send */
void pio_send_control(struct hfi1_devdata *dd, int op)
{
	u64 reg, mask;
	unsigned long flags;
	int write = 1;	/* write sendctrl back */
	int flush = 0;	/* re-read sendctrl to make sure it is flushed */

	spin_lock_irqsave(&dd->sendctrl_lock, flags);

	reg = read_csr(dd, SEND_CTRL);
	switch (op) {
	case PSC_GLOBAL_ENABLE:
		reg |= SEND_CTRL_SEND_ENABLE_SMASK;
	/* Fall through */
	case PSC_DATA_VL_ENABLE:
		/* Disallow sending on VLs not enabled */
		mask = (((~0ull)<<num_vls) & SEND_CTRL_UNSUPPORTED_VL_MASK)<<
				SEND_CTRL_UNSUPPORTED_VL_SHIFT;
		reg = (reg & ~SEND_CTRL_UNSUPPORTED_VL_SMASK) | mask;
		break;
	case PSC_GLOBAL_DISABLE:
		reg &= ~SEND_CTRL_SEND_ENABLE_SMASK;
		break;
	case PSC_GLOBAL_VLARB_ENABLE:
		reg |= SEND_CTRL_VL_ARBITER_ENABLE_SMASK;
		break;
	case PSC_GLOBAL_VLARB_DISABLE:
		reg &= ~SEND_CTRL_VL_ARBITER_ENABLE_SMASK;
		break;
	case PSC_CM_RESET:
		__cm_reset(dd, reg);
		write = 0; /* CSR already written (and flushed) */
		break;
	case PSC_DATA_VL_DISABLE:
		reg |= SEND_CTRL_UNSUPPORTED_VL_SMASK;
		flush = 1;
		break;
	default:
		dd_dev_err(dd, "%s: invalid control %d\n", __func__, op);
		break;
	}

	if (write) {
		write_csr(dd, SEND_CTRL, reg);
		if (flush)
			(void) read_csr(dd, SEND_CTRL); /* flush write */
	}

	spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
}

/* number of send context memory pools */
#define NUM_SC_POOLS 2

/* Send Context Size (SCS) wildcards */
#define SCS_POOL_0 -1
#define SCS_POOL_1 -2
/* Send Context Count (SCC) wildcards */
#define SCC_PER_VL -1
#define SCC_PER_CPU  -2

#define SCC_PER_KRCVQ  -3
#define SCC_ACK_CREDITS  32

#define PIO_WAIT_BATCH_SIZE 5

/* default send context sizes */
static struct sc_config_sizes sc_config_sizes[SC_MAX] = {
	[SC_KERNEL] = { .size  = SCS_POOL_0,	/* even divide, pool 0 */
			.count = SCC_PER_VL },/* one per NUMA */
	[SC_ACK]    = { .size  = SCC_ACK_CREDITS,
			.count = SCC_PER_KRCVQ },
	[SC_USER]   = { .size  = SCS_POOL_0,	/* even divide, pool 0 */
			.count = SCC_PER_CPU },	/* one per CPU */

};

/* send context memory pool configuration */
struct mem_pool_config {
	int centipercent;	/* % of memory, in 100ths of 1% */
	int absolute_blocks;	/* absolute block count */
};

/* default memory pool configuration: 100% in pool 0 */
static struct mem_pool_config sc_mem_pool_config[NUM_SC_POOLS] = {
	/* centi%, abs blocks */
	{  10000,     -1 },		/* pool 0 */
	{      0,     -1 },		/* pool 1 */
};

/* memory pool information, used when calculating final sizes */
struct mem_pool_info {
	int centipercent;	/* 100th of 1% of memory to use, -1 if blocks
				   already set */
	int count;		/* count of contexts in the pool */
	int blocks;		/* block size of the pool */
	int size;		/* context size, in blocks */
};

/*
 * Convert a pool wildcard to a valid pool index.  The wildcards
 * start at -1 and increase negatively.  Map them as:
 *	-1 => 0
 *	-2 => 1
 *	etc.
 *
 * Return -1 on non-wildcard input, otherwise convert to a pool number.
 */
static int wildcard_to_pool(int wc)
{
	if (wc >= 0)
		return -1;	/* non-wildcard */
	return -wc - 1;
}

static const char *sc_type_names[SC_MAX] = {
	"kernel",
	"ack",
	"user"
};

static const char *sc_type_name(int index)
{
	if (index < 0 || index >= SC_MAX)
		return "unknown";
	return sc_type_names[index];
}

/*
 * Read the send context memory pool configuration and send context
 * size configuration.  Replace any wildcards and come up with final
 * counts and sizes for the send context types.
 */
int init_sc_pools_and_sizes(struct hfi1_devdata *dd)
{
	struct mem_pool_info mem_pool_info[NUM_SC_POOLS] = { { 0 } };
	int total_blocks = (dd->chip_pio_mem_size / PIO_BLOCK_SIZE) - 1;
	int total_contexts = 0;
	int fixed_blocks;
	int pool_blocks;
	int used_blocks;
	int cp_total;		/* centipercent total */
	int ab_total;		/* absolute block total */
	int extra;
	int i;

	/*
	 * Step 0:
	 *	- copy the centipercents/absolute sizes from the pool config
	 *	- sanity check these values
	 *	- add up centipercents, then later check for full value
	 *	- add up absolute blocks, then later check for over-commit
	 */
	cp_total = 0;
	ab_total = 0;
	for (i = 0; i < NUM_SC_POOLS; i++) {
		int cp = sc_mem_pool_config[i].centipercent;
		int ab = sc_mem_pool_config[i].absolute_blocks;

		/*
		 * A negative value is "unused" or "invalid".  Both *can*
		 * be valid, but centipercent wins, so check that first
		 */
		if (cp >= 0) {			/* centipercent valid */
			cp_total += cp;
		} else if (ab >= 0) {		/* absolute blocks valid */
			ab_total += ab;
		} else {			/* neither valid */
			dd_dev_err(
				dd,
				"Send context memory pool %d: both the block count and centipercent are invalid\n",
				i);
			return -EINVAL;
		}

		mem_pool_info[i].centipercent = cp;
		mem_pool_info[i].blocks = ab;
	}

	/* do not use both % and absolute blocks for different pools */
	if (cp_total != 0 && ab_total != 0) {
		dd_dev_err(
			dd,
			"All send context memory pools must be described as either centipercent or blocks, no mixing between pools\n");
		return -EINVAL;
	}

	/* if any percentages are present, they must add up to 100% x 100 */
	if (cp_total != 0 && cp_total != 10000) {
		dd_dev_err(
			dd,
			"Send context memory pool centipercent is %d, expecting 10000\n",
			cp_total);
		return -EINVAL;
	}

	/* the absolute pool total cannot be more than the mem total */
	if (ab_total > total_blocks) {
		dd_dev_err(
			dd,
			"Send context memory pool absolute block count %d is larger than the memory size %d\n",
			ab_total, total_blocks);
		return -EINVAL;
	}

	/*
	 * Step 2:
	 *	- copy from the context size config
	 *	- replace context type wildcard counts with real values
	 *	- add up non-memory pool block sizes
	 *	- add up memory pool user counts
	 */
	fixed_blocks = 0;
	for (i = 0; i < SC_MAX; i++) {
		int count = sc_config_sizes[i].count;
		int size = sc_config_sizes[i].size;
		int pool;

		/*
		 * Sanity check count: Either a positive value or
		 * one of the expected wildcards is valid.  The positive
		 * value is checked later when we compare against total
		 * memory available.
		 */
		if (i == SC_ACK) {
			count = dd->n_krcv_queues;
		} else if (i == SC_KERNEL) {
			count = num_vls + 1 /* VL15 */;
		} else if (count == SCC_PER_CPU) {
			count = dd->num_rcv_contexts - dd->n_krcv_queues;
		} else if (count < 0) {
			dd_dev_err(
				dd,
				"%s send context invalid count wildcard %d\n",
				sc_type_name(i), count);
			return -EINVAL;
		}
		if (total_contexts + count > dd->chip_send_contexts)
			count = dd->chip_send_contexts - total_contexts;

		total_contexts += count;

		/*
		 * Sanity check pool: The conversion will return a pool
		 * number or -1 if a fixed (non-negative) value.  The fixed
		 * value is checked later when we compare against
		 * total memory available.
		 */
		pool = wildcard_to_pool(size);
		if (pool == -1) {			/* non-wildcard */
			fixed_blocks += size * count;
		} else if (pool < NUM_SC_POOLS) {	/* valid wildcard */
			mem_pool_info[pool].count += count;
		} else {				/* invalid wildcard */
			dd_dev_err(
				dd,
				"%s send context invalid pool wildcard %d\n",
				sc_type_name(i), size);
			return -EINVAL;
		}

		dd->sc_sizes[i].count = count;
		dd->sc_sizes[i].size = size;
	}
	if (fixed_blocks > total_blocks) {
		dd_dev_err(
			dd,
			"Send context fixed block count, %u, larger than total block count %u\n",
			fixed_blocks, total_blocks);
		return -EINVAL;
	}

	/* step 3: calculate the blocks in the pools, and pool context sizes */
	pool_blocks = total_blocks - fixed_blocks;
	if (ab_total > pool_blocks) {
		dd_dev_err(
			dd,
			"Send context fixed pool sizes, %u, larger than pool block count %u\n",
			ab_total, pool_blocks);
		return -EINVAL;
	}
	/* subtract off the fixed pool blocks */
	pool_blocks -= ab_total;

	for (i = 0; i < NUM_SC_POOLS; i++) {
		struct mem_pool_info *pi = &mem_pool_info[i];

		/* % beats absolute blocks */
		if (pi->centipercent >= 0)
			pi->blocks = (pool_blocks * pi->centipercent) / 10000;

		if (pi->blocks == 0 && pi->count != 0) {
			dd_dev_err(
				dd,
				"Send context memory pool %d has %u contexts, but no blocks\n",
				i, pi->count);
			return -EINVAL;
		}
		if (pi->count == 0) {
			/* warn about wasted blocks */
			if (pi->blocks != 0)
				dd_dev_err(
					dd,
					"Send context memory pool %d has %u blocks, but zero contexts\n",
					i, pi->blocks);
			pi->size = 0;
		} else {
			pi->size = pi->blocks / pi->count;
		}
	}

	/* step 4: fill in the context type sizes from the pool sizes */
	used_blocks = 0;
	for (i = 0; i < SC_MAX; i++) {
		if (dd->sc_sizes[i].size < 0) {
			unsigned pool = wildcard_to_pool(dd->sc_sizes[i].size);

			WARN_ON_ONCE(pool >= NUM_SC_POOLS);
			dd->sc_sizes[i].size = mem_pool_info[pool].size;
		}
		/* make sure we are not larger than what is allowed by the HW */
#define PIO_MAX_BLOCKS 1024
		if (dd->sc_sizes[i].size > PIO_MAX_BLOCKS)
			dd->sc_sizes[i].size = PIO_MAX_BLOCKS;

		/* calculate our total usage */
		used_blocks += dd->sc_sizes[i].size * dd->sc_sizes[i].count;
	}
	extra = total_blocks - used_blocks;
	if (extra != 0)
		dd_dev_info(dd, "unused send context blocks: %d\n", extra);

	return total_contexts;
}

int init_send_contexts(struct hfi1_devdata *dd)
{
	u16 base;
	int ret, i, j, context;

	ret = init_credit_return(dd);
	if (ret)
		return ret;

	dd->hw_to_sw = kmalloc_array(TXE_NUM_CONTEXTS, sizeof(u8),
					GFP_KERNEL);
	dd->send_contexts = kcalloc(dd->num_send_contexts,
					sizeof(struct send_context_info),
					GFP_KERNEL);
	if (!dd->send_contexts || !dd->hw_to_sw) {
		dd_dev_err(dd, "Unable to allocate send context arrays\n");
		kfree(dd->hw_to_sw);
		kfree(dd->send_contexts);
		free_credit_return(dd);
		return -ENOMEM;
	}

	/* hardware context map starts with invalid send context indices */
	for (i = 0; i < TXE_NUM_CONTEXTS; i++)
		dd->hw_to_sw[i] = INVALID_SCI;

	/*
	 * All send contexts have their credit sizes.  Allocate credits
	 * for each context one after another from the global space.
	 */
	context = 0;
	base = 1;
	for (i = 0; i < SC_MAX; i++) {
		struct sc_config_sizes *scs = &dd->sc_sizes[i];

		for (j = 0; j < scs->count; j++) {
			struct send_context_info *sci =
						&dd->send_contexts[context];
			sci->type = i;
			sci->base = base;
			sci->credits = scs->size;

			context++;
			base += scs->size;
		}
	}

	return 0;
}

/*
 * Allocate a software index and hardware context of the given type.
 *
 * Must be called with dd->sc_lock held.
 */
static int sc_hw_alloc(struct hfi1_devdata *dd, int type, u32 *sw_index,
		       u32 *hw_context)
{
	struct send_context_info *sci;
	u32 index;
	u32 context;

	for (index = 0, sci = &dd->send_contexts[0];
			index < dd->num_send_contexts; index++, sci++) {
		if (sci->type == type && sci->allocated == 0) {
			sci->allocated = 1;
			/* use a 1:1 mapping, but make them non-equal */
			context = dd->chip_send_contexts - index - 1;
			dd->hw_to_sw[context] = index;
			*sw_index = index;
			*hw_context = context;
			return 0; /* success */
		}
	}
	dd_dev_err(dd, "Unable to locate a free type %d send context\n", type);
	return -ENOSPC;
}

/*
 * Free the send context given by its software index.
 *
 * Must be called with dd->sc_lock held.
 */
static void sc_hw_free(struct hfi1_devdata *dd, u32 sw_index, u32 hw_context)
{
	struct send_context_info *sci;

	sci = &dd->send_contexts[sw_index];
	if (!sci->allocated) {
		dd_dev_err(dd, "%s: sw_index %u not allocated? hw_context %u\n",
			__func__, sw_index, hw_context);
	}
	sci->allocated = 0;
	dd->hw_to_sw[hw_context] = INVALID_SCI;
}

/* return the base context of a context in a group */
static inline u32 group_context(u32 context, u32 group)
{
	return (context >> group) << group;
}

/* return the size of a group */
static inline u32 group_size(u32 group)
{
	return 1 << group;
}

/*
 * Obtain the credit return addresses, kernel virtual and physical, for the
 * given sc.
 *
 * To understand this routine:
 * o va and pa are arrays of struct credit_return.  One for each physical
 *   send context, per NUMA.
 * o Each send context always looks in its relative location in a struct
 *   credit_return for its credit return.
 * o Each send context in a group must have its return address CSR programmed
 *   with the same value.  Use the address of the first send context in the
 *   group.
 */
static void cr_group_addresses(struct send_context *sc, dma_addr_t *pa)
{
	u32 gc = group_context(sc->hw_context, sc->group);
	u32 index = sc->hw_context & 0x7;

	sc->hw_free = &sc->dd->cr_base[sc->node].va[gc].cr[index];
	*pa = (unsigned long)
	       &((struct credit_return *)sc->dd->cr_base[sc->node].pa)[gc];
}

/*
 * Work queue function triggered in error interrupt routine for
 * kernel contexts.
 */
static void sc_halted(struct work_struct *work)
{
	struct send_context *sc;

	sc = container_of(work, struct send_context, halt_work);
	sc_restart(sc);
}

/*
 * Calculate PIO block threshold for this send context using the given MTU.
 * Trigger a return when one MTU plus optional header of credits remain.
 *
 * Parameter mtu is in bytes.
 * Parameter hdrqentsize is in DWORDs.
 *
 * Return value is what to write into the CSR: trigger return when
 * unreturned credits pass this count.
 */
u32 sc_mtu_to_threshold(struct send_context *sc, u32 mtu, u32 hdrqentsize)
{
	u32 release_credits;
	u32 threshold;

	/* add in the header size, then divide by the PIO block size */
	mtu += hdrqentsize << 2;
	release_credits = DIV_ROUND_UP(mtu, PIO_BLOCK_SIZE);

	/* check against this context's credits */
	if (sc->credits <= release_credits)
		threshold = 1;
	else
		threshold = sc->credits - release_credits;

	return threshold;
}

/*
 * Calculate credit threshold in terms of percent of the allocated credits.
 * Trigger when unreturned credits equal or exceed the percentage of the whole.
 *
 * Return value is what to write into the CSR: trigger return when
 * unreturned credits pass this count.
 */
static u32 sc_percent_to_threshold(struct send_context *sc, u32 percent)
{
	return (sc->credits * percent) / 100;
}

/*
 * Set the credit return threshold.
 */
void sc_set_cr_threshold(struct send_context *sc, u32 new_threshold)
{
	unsigned long flags;
	u32 old_threshold;
	int force_return = 0;

	spin_lock_irqsave(&sc->credit_ctrl_lock, flags);

	old_threshold = (sc->credit_ctrl >>
				SC(CREDIT_CTRL_THRESHOLD_SHIFT))
			 & SC(CREDIT_CTRL_THRESHOLD_MASK);

	if (new_threshold != old_threshold) {
		sc->credit_ctrl =
			(sc->credit_ctrl
				& ~SC(CREDIT_CTRL_THRESHOLD_SMASK))
			| ((new_threshold
				& SC(CREDIT_CTRL_THRESHOLD_MASK))
			   << SC(CREDIT_CTRL_THRESHOLD_SHIFT));
		write_kctxt_csr(sc->dd, sc->hw_context,
			SC(CREDIT_CTRL), sc->credit_ctrl);

		/* force a credit return on change to avoid a possible stall */
		force_return = 1;
	}

	spin_unlock_irqrestore(&sc->credit_ctrl_lock, flags);

	if (force_return)
		sc_return_credits(sc);
}

/*
 * set_pio_integrity
 *
 * Set the CHECK_ENABLE register for the send context 'sc'.
 */
void set_pio_integrity(struct send_context *sc)
{
	struct hfi1_devdata *dd = sc->dd;
	u64 reg = 0;
	u32 hw_context = sc->hw_context;
	int type = sc->type;

	/*
	 * No integrity checks if HFI1_CAP_NO_INTEGRITY is set, or if
	 * we're snooping.
	 */
	if (likely(!HFI1_CAP_IS_KSET(NO_INTEGRITY)) &&
	    dd->hfi1_snoop.mode_flag != HFI1_PORT_SNOOP_MODE)
		reg = hfi1_pkt_default_send_ctxt_mask(dd, type);

	write_kctxt_csr(dd, hw_context, SC(CHECK_ENABLE), reg);
}

/*
 * Allocate a NUMA relative send context structure of the given type along
 * with a HW context.
 */
struct send_context *sc_alloc(struct hfi1_devdata *dd, int type,
			      uint hdrqentsize, int numa)
{
	struct send_context_info *sci;
	struct send_context *sc;
	dma_addr_t pa;
	unsigned long flags;
	u64 reg;
	u32 thresh;
	u32 sw_index;
	u32 hw_context;
	int ret;
	u8 opval, opmask;

	/* do not allocate while frozen */
	if (dd->flags & HFI1_FROZEN)
		return NULL;

	sc = kzalloc_node(sizeof(struct send_context), GFP_KERNEL, numa);
	if (!sc) {
		dd_dev_err(dd, "Cannot allocate send context structure\n");
		return NULL;
	}

	spin_lock_irqsave(&dd->sc_lock, flags);
	ret = sc_hw_alloc(dd, type, &sw_index, &hw_context);
	if (ret) {
		spin_unlock_irqrestore(&dd->sc_lock, flags);
		kfree(sc);
		return NULL;
	}

	sci = &dd->send_contexts[sw_index];
	sci->sc = sc;

	sc->dd = dd;
	sc->node = numa;
	sc->type = type;
	spin_lock_init(&sc->alloc_lock);
	spin_lock_init(&sc->release_lock);
	spin_lock_init(&sc->credit_ctrl_lock);
	INIT_LIST_HEAD(&sc->piowait);
	INIT_WORK(&sc->halt_work, sc_halted);
	atomic_set(&sc->buffers_allocated, 0);
	init_waitqueue_head(&sc->halt_wait);

	/* grouping is always single context for now */
	sc->group = 0;

	sc->sw_index = sw_index;
	sc->hw_context = hw_context;
	cr_group_addresses(sc, &pa);
	sc->credits = sci->credits;

/* PIO Send Memory Address details */
#define PIO_ADDR_CONTEXT_MASK 0xfful
#define PIO_ADDR_CONTEXT_SHIFT 16
	sc->base_addr = dd->piobase + ((hw_context & PIO_ADDR_CONTEXT_MASK)
					<< PIO_ADDR_CONTEXT_SHIFT);

	/* set base and credits */
	reg = ((sci->credits & SC(CTRL_CTXT_DEPTH_MASK))
					<< SC(CTRL_CTXT_DEPTH_SHIFT))
		| ((sci->base & SC(CTRL_CTXT_BASE_MASK))
					<< SC(CTRL_CTXT_BASE_SHIFT));
	write_kctxt_csr(dd, hw_context, SC(CTRL), reg);

	set_pio_integrity(sc);

	/* unmask all errors */
	write_kctxt_csr(dd, hw_context, SC(ERR_MASK), (u64)-1);

	/* set the default partition key */
	write_kctxt_csr(dd, hw_context, SC(CHECK_PARTITION_KEY),
		(DEFAULT_PKEY &
			SC(CHECK_PARTITION_KEY_VALUE_MASK))
		    << SC(CHECK_PARTITION_KEY_VALUE_SHIFT));

	/* per context type checks */
	if (type == SC_USER) {
		opval = USER_OPCODE_CHECK_VAL;
		opmask = USER_OPCODE_CHECK_MASK;
	} else {
		opval = OPCODE_CHECK_VAL_DISABLED;
		opmask = OPCODE_CHECK_MASK_DISABLED;
	}

	/* set the send context check opcode mask and value */
	write_kctxt_csr(dd, hw_context, SC(CHECK_OPCODE),
		((u64)opmask << SC(CHECK_OPCODE_MASK_SHIFT)) |
		((u64)opval << SC(CHECK_OPCODE_VALUE_SHIFT)));

	/* set up credit return */
	reg = pa & SC(CREDIT_RETURN_ADDR_ADDRESS_SMASK);
	write_kctxt_csr(dd, hw_context, SC(CREDIT_RETURN_ADDR), reg);

	/*
	 * Calculate the initial credit return threshold.
	 *
	 * For Ack contexts, set a threshold for half the credits.
	 * For User contexts use the given percentage.  This has been
	 * sanitized on driver start-up.
	 * For Kernel contexts, use the default MTU plus a header.
	 */
	if (type == SC_ACK) {
		thresh = sc_percent_to_threshold(sc, 50);
	} else if (type == SC_USER) {
		thresh = sc_percent_to_threshold(sc,
				user_credit_return_threshold);
	} else { /* kernel */
		thresh = sc_mtu_to_threshold(sc, hfi1_max_mtu, hdrqentsize);
	}
	reg = thresh << SC(CREDIT_CTRL_THRESHOLD_SHIFT);
	/* add in early return */
	if (type == SC_USER && HFI1_CAP_IS_USET(EARLY_CREDIT_RETURN))
		reg |= SC(CREDIT_CTRL_EARLY_RETURN_SMASK);
	else if (HFI1_CAP_IS_KSET(EARLY_CREDIT_RETURN)) /* kernel, ack */
		reg |= SC(CREDIT_CTRL_EARLY_RETURN_SMASK);

	/* set up write-through credit_ctrl */
	sc->credit_ctrl = reg;
	write_kctxt_csr(dd, hw_context, SC(CREDIT_CTRL), reg);

	/* User send contexts should not allow sending on VL15 */
	if (type == SC_USER) {
		reg = 1ULL << 15;
		write_kctxt_csr(dd, hw_context, SC(CHECK_VL), reg);
	}

	spin_unlock_irqrestore(&dd->sc_lock, flags);

	/*
	 * Allocate shadow ring to track outstanding PIO buffers _after_
	 * unlocking.  We don't know the size until the lock is held and
	 * we can't allocate while the lock is held.  No one is using
	 * the context yet, so allocate it now.
	 *
	 * User contexts do not get a shadow ring.
	 */
	if (type != SC_USER) {
		/*
		 * Size the shadow ring 1 larger than the number of credits
		 * so head == tail can mean empty.
		 */
		sc->sr_size = sci->credits + 1;
		sc->sr = kzalloc_node(sizeof(union pio_shadow_ring) *
				sc->sr_size, GFP_KERNEL, numa);
		if (!sc->sr) {
			dd_dev_err(dd,
				"Cannot allocate send context shadow ring structure\n");
			sc_free(sc);
			return NULL;
		}
	}

	dd_dev_info(dd,
		"Send context %u(%u) %s group %u credits %u credit_ctrl 0x%llx threshold %u\n",
		sw_index,
		hw_context,
		sc_type_name(type),
		sc->group,
		sc->credits,
		sc->credit_ctrl,
		thresh);

	return sc;
}

/* free a per-NUMA send context structure */
void sc_free(struct send_context *sc)
{
	struct hfi1_devdata *dd;
	unsigned long flags;
	u32 sw_index;
	u32 hw_context;

	if (!sc)
		return;

	sc->flags |= SCF_IN_FREE;	/* ensure no restarts */
	dd = sc->dd;
	if (!list_empty(&sc->piowait))
		dd_dev_err(dd, "piowait list not empty!\n");
	sw_index = sc->sw_index;
	hw_context = sc->hw_context;
	sc_disable(sc);	/* make sure the HW is disabled */
	flush_work(&sc->halt_work);

	spin_lock_irqsave(&dd->sc_lock, flags);
	dd->send_contexts[sw_index].sc = NULL;

	/* clear/disable all registers set in sc_alloc */
	write_kctxt_csr(dd, hw_context, SC(CTRL), 0);
	write_kctxt_csr(dd, hw_context, SC(CHECK_ENABLE), 0);
	write_kctxt_csr(dd, hw_context, SC(ERR_MASK), 0);
	write_kctxt_csr(dd, hw_context, SC(CHECK_PARTITION_KEY), 0);
	write_kctxt_csr(dd, hw_context, SC(CHECK_OPCODE), 0);
	write_kctxt_csr(dd, hw_context, SC(CREDIT_RETURN_ADDR), 0);
	write_kctxt_csr(dd, hw_context, SC(CREDIT_CTRL), 0);

	/* release the index and context for re-use */
	sc_hw_free(dd, sw_index, hw_context);
	spin_unlock_irqrestore(&dd->sc_lock, flags);

	kfree(sc->sr);
	kfree(sc);
}

/* disable the context */
void sc_disable(struct send_context *sc)
{
	u64 reg;
	unsigned long flags;
	struct pio_buf *pbuf;

	if (!sc)
		return;

	/* do all steps, even if already disabled */
	spin_lock_irqsave(&sc->alloc_lock, flags);
	reg = read_kctxt_csr(sc->dd, sc->hw_context, SC(CTRL));
	reg &= ~SC(CTRL_CTXT_ENABLE_SMASK);
	sc->flags &= ~SCF_ENABLED;
	sc_wait_for_packet_egress(sc, 1);
	write_kctxt_csr(sc->dd, sc->hw_context, SC(CTRL), reg);
	spin_unlock_irqrestore(&sc->alloc_lock, flags);

	/*
	 * Flush any waiters.  Once the context is disabled,
	 * credit return interrupts are stopped (although there
	 * could be one in-process when the context is disabled).
	 * Wait one microsecond for any lingering interrupts, then
	 * proceed with the flush.
	 */
	udelay(1);
	spin_lock_irqsave(&sc->release_lock, flags);
	if (sc->sr) {	/* this context has a shadow ring */
		while (sc->sr_tail != sc->sr_head) {
			pbuf = &sc->sr[sc->sr_tail].pbuf;
			if (pbuf->cb)
				(*pbuf->cb)(pbuf->arg, PRC_SC_DISABLE);
			sc->sr_tail++;
			if (sc->sr_tail >= sc->sr_size)
				sc->sr_tail = 0;
		}
	}
	spin_unlock_irqrestore(&sc->release_lock, flags);
}

/* return SendEgressCtxtStatus.PacketOccupancy */
#define packet_occupancy(r) \
	(((r) & SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_PACKET_OCCUPANCY_SMASK)\
	>> SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_PACKET_OCCUPANCY_SHIFT)

/* is egress halted on the context? */
#define egress_halted(r) \
	((r) & SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_HALT_STATUS_SMASK)

/* wait for packet egress, optionally pause for credit return  */
static void sc_wait_for_packet_egress(struct send_context *sc, int pause)
{
	struct hfi1_devdata *dd = sc->dd;
	u64 reg;
	u32 loop = 0;

	while (1) {
		reg = read_csr(dd, sc->hw_context * 8 +
			       SEND_EGRESS_CTXT_STATUS);
		/* done if egress is stopped */
		if (egress_halted(reg))
			break;
		reg = packet_occupancy(reg);
		if (reg == 0)
			break;
		if (loop > 100) {
			dd_dev_err(dd,
				"%s: context %u(%u) timeout waiting for packets to egress, remaining count %u\n",
				__func__, sc->sw_index,
				sc->hw_context, (u32)reg);
			break;
		}
		loop++;
		udelay(1);
	}

	if (pause)
		/* Add additional delay to ensure chip returns all credits */
		pause_for_credit_return(dd);
}

void sc_wait(struct hfi1_devdata *dd)
{
	int i;

	for (i = 0; i < dd->num_send_contexts; i++) {
		struct send_context *sc = dd->send_contexts[i].sc;

		if (!sc)
			continue;
		sc_wait_for_packet_egress(sc, 0);
	}
}

/*
 * Restart a context after it has been halted due to error.
 *
 * If the first step fails - wait for the halt to be asserted, return early.
 * Otherwise complain about timeouts but keep going.
 *
 * It is expected that allocations (enabled flag bit) have been shut off
 * already (only applies to kernel contexts).
 */
int sc_restart(struct send_context *sc)
{
	struct hfi1_devdata *dd = sc->dd;
	u64 reg;
	u32 loop;
	int count;

	/* bounce off if not halted, or being free'd */
	if (!(sc->flags & SCF_HALTED) || (sc->flags & SCF_IN_FREE))
		return -EINVAL;

	dd_dev_info(dd, "restarting send context %u(%u)\n", sc->sw_index,
		sc->hw_context);

	/*
	 * Step 1: Wait for the context to actually halt.
	 *
	 * The error interrupt is asynchronous to actually setting halt
	 * on the context.
	 */
	loop = 0;
	while (1) {
		reg = read_kctxt_csr(dd, sc->hw_context, SC(STATUS));
		if (reg & SC(STATUS_CTXT_HALTED_SMASK))
			break;
		if (loop > 100) {
			dd_dev_err(dd, "%s: context %u(%u) not halting, skipping\n",
				__func__, sc->sw_index, sc->hw_context);
			return -ETIME;
		}
		loop++;
		udelay(1);
	}

	/*
	 * Step 2: Ensure no users are still trying to write to PIO.
	 *
	 * For kernel contexts, we have already turned off buffer allocation.
	 * Now wait for the buffer count to go to zero.
	 *
	 * For user contexts, the user handling code has cut off write access
	 * to the context's PIO pages before calling this routine and will
	 * restore write access after this routine returns.
	 */
	if (sc->type != SC_USER) {
		/* kernel context */
		loop = 0;
		while (1) {
			count = atomic_read(&sc->buffers_allocated);
			if (count == 0)
				break;
			if (loop > 100) {
				dd_dev_err(dd,
					"%s: context %u(%u) timeout waiting for PIO buffers to zero, remaining %d\n",
					__func__, sc->sw_index,
					sc->hw_context, count);
			}
			loop++;
			udelay(1);
		}
	}

	/*
	 * Step 3: Wait for all packets to egress.
	 * This is done while disabling the send context
	 *
	 * Step 4: Disable the context
	 *
	 * This is a superset of the halt.  After the disable, the
	 * errors can be cleared.
	 */
	sc_disable(sc);

	/*
	 * Step 5: Enable the context
	 *
	 * This enable will clear the halted flag and per-send context
	 * error flags.
	 */
	return sc_enable(sc);
}

/*
 * PIO freeze processing.  To be called after the TXE block is fully frozen.
 * Go through all frozen send contexts and disable them.  The contexts are
 * already stopped by the freeze.
 */
void pio_freeze(struct hfi1_devdata *dd)
{
	struct send_context *sc;
	int i;

	for (i = 0; i < dd->num_send_contexts; i++) {
		sc = dd->send_contexts[i].sc;
		/*
		 * Don't disable unallocated, unfrozen, or user send contexts.
		 * User send contexts will be disabled when the process
		 * calls into the driver to reset its context.
		 */
		if (!sc || !(sc->flags & SCF_FROZEN) || sc->type == SC_USER)
			continue;

		/* only need to disable, the context is already stopped */
		sc_disable(sc);
	}
}

/*
 * Unfreeze PIO for kernel send contexts.  The precondition for calling this
 * is that all PIO send contexts have been disabled and the SPC freeze has
 * been cleared.  Now perform the last step and re-enable each kernel context.
 * User (PSM) processing will occur when PSM calls into the kernel to
 * acknowledge the freeze.
 */
void pio_kernel_unfreeze(struct hfi1_devdata *dd)
{
	struct send_context *sc;
	int i;

	for (i = 0; i < dd->num_send_contexts; i++) {
		sc = dd->send_contexts[i].sc;
		if (!sc || !(sc->flags & SCF_FROZEN) || sc->type == SC_USER)
			continue;

		sc_enable(sc);	/* will clear the sc frozen flag */
	}
}

/*
 * Wait for the SendPioInitCtxt.PioInitInProgress bit to clear.
 * Returns:
 *	-ETIMEDOUT - if we wait too long
 *	-EIO	   - if there was an error
 */
static int pio_init_wait_progress(struct hfi1_devdata *dd)
{
	u64 reg;
	int max, count = 0;

	/* max is the longest possible HW init time / delay */
	max = (dd->icode == ICODE_FPGA_EMULATION) ? 120 : 5;
	while (1) {
		reg = read_csr(dd, SEND_PIO_INIT_CTXT);
		if (!(reg & SEND_PIO_INIT_CTXT_PIO_INIT_IN_PROGRESS_SMASK))
			break;
		if (count >= max)
			return -ETIMEDOUT;
		udelay(5);
		count++;
	}

	return reg & SEND_PIO_INIT_CTXT_PIO_INIT_ERR_SMASK ? -EIO : 0;
}

/*
 * Reset all of the send contexts to their power-on state.  Used
 * only during manual init - no lock against sc_enable needed.
 */
void pio_reset_all(struct hfi1_devdata *dd)
{
	int ret;

	/* make sure the init engine is not busy */
	ret = pio_init_wait_progress(dd);
	/* ignore any timeout */
	if (ret == -EIO) {
		/* clear the error */
		write_csr(dd, SEND_PIO_ERR_CLEAR,
			SEND_PIO_ERR_CLEAR_PIO_INIT_SM_IN_ERR_SMASK);
	}

	/* reset init all */
	write_csr(dd, SEND_PIO_INIT_CTXT,
			SEND_PIO_INIT_CTXT_PIO_ALL_CTXT_INIT_SMASK);
	udelay(2);
	ret = pio_init_wait_progress(dd);
	if (ret < 0) {
		dd_dev_err(dd,
			"PIO send context init %s while initializing all PIO blocks\n",
			ret == -ETIMEDOUT ? "is stuck" : "had an error");
	}
}

/* enable the context */
int sc_enable(struct send_context *sc)
{
	u64 sc_ctrl, reg, pio;
	struct hfi1_devdata *dd;
	unsigned long flags;
	int ret = 0;

	if (!sc)
		return -EINVAL;
	dd = sc->dd;

	/*
	 * Obtain the allocator lock to guard against any allocation
	 * attempts (which should not happen prior to context being
	 * enabled). On the release/disable side we don't need to
	 * worry about locking since the releaser will not do anything
	 * if the context accounting values have not changed.
	 */
	spin_lock_irqsave(&sc->alloc_lock, flags);
	sc_ctrl = read_kctxt_csr(dd, sc->hw_context, SC(CTRL));
	if ((sc_ctrl & SC(CTRL_CTXT_ENABLE_SMASK)))
		goto unlock; /* already enabled */

	/* IMPORTANT: only clear free and fill if transitioning 0 -> 1 */

	*sc->hw_free = 0;
	sc->free = 0;
	sc->alloc_free = 0;
	sc->fill = 0;
	sc->sr_head = 0;
	sc->sr_tail = 0;
	sc->flags = 0;
	atomic_set(&sc->buffers_allocated, 0);

	/*
	 * Clear all per-context errors.  Some of these will be set when
	 * we are re-enabling after a context halt.  Now that the context
	 * is disabled, the halt will not clear until after the PIO init
	 * engine runs below.
	 */
	reg = read_kctxt_csr(dd, sc->hw_context, SC(ERR_STATUS));
	if (reg)
		write_kctxt_csr(dd, sc->hw_context, SC(ERR_CLEAR),
			reg);

	/*
	 * The HW PIO initialization engine can handle only one init
	 * request at a time. Serialize access to each device's engine.
	 */
	spin_lock(&dd->sc_init_lock);
	/*
	 * Since access to this code block is serialized and
	 * each access waits for the initialization to complete
	 * before releasing the lock, the PIO initialization engine
	 * should not be in use, so we don't have to wait for the
	 * InProgress bit to go down.
	 */
	pio = ((sc->hw_context & SEND_PIO_INIT_CTXT_PIO_CTXT_NUM_MASK) <<
	       SEND_PIO_INIT_CTXT_PIO_CTXT_NUM_SHIFT) |
		SEND_PIO_INIT_CTXT_PIO_SINGLE_CTXT_INIT_SMASK;
	write_csr(dd, SEND_PIO_INIT_CTXT, pio);
	/*
	 * Wait until the engine is done.  Give the chip the required time
	 * so, hopefully, we read the register just once.
	 */
	udelay(2);
	ret = pio_init_wait_progress(dd);
	spin_unlock(&dd->sc_init_lock);
	if (ret) {
		dd_dev_err(dd,
			   "sctxt%u(%u): Context not enabled due to init failure %d\n",
			   sc->sw_index, sc->hw_context, ret);
		goto unlock;
	}

	/*
	 * All is well. Enable the context.
	 */
	sc_ctrl |= SC(CTRL_CTXT_ENABLE_SMASK);
	write_kctxt_csr(dd, sc->hw_context, SC(CTRL), sc_ctrl);
	/*
	 * Read SendCtxtCtrl to force the write out and prevent a timing
	 * hazard where a PIO write may reach the context before the enable.
	 */
	read_kctxt_csr(dd, sc->hw_context, SC(CTRL));
	sc->flags |= SCF_ENABLED;

unlock:
	spin_unlock_irqrestore(&sc->alloc_lock, flags);

	return ret;
}

/* force a credit return on the context */
void sc_return_credits(struct send_context *sc)
{
	if (!sc)
		return;

	/* a 0->1 transition schedules a credit return */
	write_kctxt_csr(sc->dd, sc->hw_context, SC(CREDIT_FORCE),
		SC(CREDIT_FORCE_FORCE_RETURN_SMASK));
	/*
	 * Ensure that the write is flushed and the credit return is
	 * scheduled. We care more about the 0 -> 1 transition.
	 */
	read_kctxt_csr(sc->dd, sc->hw_context, SC(CREDIT_FORCE));
	/* set back to 0 for next time */
	write_kctxt_csr(sc->dd, sc->hw_context, SC(CREDIT_FORCE), 0);
}

/* allow all in-flight packets to drain on the context */
void sc_flush(struct send_context *sc)
{
	if (!sc)
		return;

	sc_wait_for_packet_egress(sc, 1);
}

/* drop all packets on the context, no waiting until they are sent */
void sc_drop(struct send_context *sc)
{
	if (!sc)
		return;

	dd_dev_info(sc->dd, "%s: context %u(%u) - not implemented\n",
			__func__, sc->sw_index, sc->hw_context);
}

/*
 * Start the software reaction to a context halt or SPC freeze:
 *	- mark the context as halted or frozen
 *	- stop buffer allocations
 *
 * Called from the error interrupt.  Other work is deferred until
 * out of the interrupt.
 */
void sc_stop(struct send_context *sc, int flag)
{
	unsigned long flags;

	/* mark the context */
	sc->flags |= flag;

	/* stop buffer allocations */
	spin_lock_irqsave(&sc->alloc_lock, flags);
	sc->flags &= ~SCF_ENABLED;
	spin_unlock_irqrestore(&sc->alloc_lock, flags);
	wake_up(&sc->halt_wait);
}

#define BLOCK_DWORDS (PIO_BLOCK_SIZE/sizeof(u32))
#define dwords_to_blocks(x) DIV_ROUND_UP(x, BLOCK_DWORDS)

/*
 * The send context buffer "allocator".
 *
 * @sc: the PIO send context we are allocating from
 * @len: length of whole packet - including PBC - in dwords
 * @cb: optional callback to call when the buffer is finished sending
 * @arg: argument for cb
 *
 * Return a pointer to a PIO buffer if successful, NULL if not enough room.
 */
struct pio_buf *sc_buffer_alloc(struct send_context *sc, u32 dw_len,
				pio_release_cb cb, void *arg)
{
	struct pio_buf *pbuf = NULL;
	unsigned long flags;
	unsigned long avail;
	unsigned long blocks = dwords_to_blocks(dw_len);
	unsigned long start_fill;
	int trycount = 0;
	u32 head, next;

	spin_lock_irqsave(&sc->alloc_lock, flags);
	if (!(sc->flags & SCF_ENABLED)) {
		spin_unlock_irqrestore(&sc->alloc_lock, flags);
		goto done;
	}

retry:
	avail = (unsigned long)sc->credits - (sc->fill - sc->alloc_free);
	if (blocks > avail) {
		/* not enough room */
		if (unlikely(trycount))	{ /* already tried to get more room */
			spin_unlock_irqrestore(&sc->alloc_lock, flags);
			goto done;
		}
		/* copy from receiver cache line and recalculate */
		sc->alloc_free = ACCESS_ONCE(sc->free);
		avail =
			(unsigned long)sc->credits -
			(sc->fill - sc->alloc_free);
		if (blocks > avail) {
			/* still no room, actively update */
			spin_unlock_irqrestore(&sc->alloc_lock, flags);
			sc_release_update(sc);
			spin_lock_irqsave(&sc->alloc_lock, flags);
			sc->alloc_free = ACCESS_ONCE(sc->free);
			trycount++;
			goto retry;
		}
	}

	/* there is enough room */

	atomic_inc(&sc->buffers_allocated);

	/* read this once */
	head = sc->sr_head;

	/* "allocate" the buffer */
	start_fill = sc->fill;
	sc->fill += blocks;

	/*
	 * Fill the parts that the releaser looks at before moving the head.
	 * The only necessary piece is the sent_at field.  The credits
	 * we have just allocated cannot have been returned yet, so the
	 * cb and arg will not be looked at for a "while".  Put them
	 * on this side of the memory barrier anyway.
	 */
	pbuf = &sc->sr[head].pbuf;
	pbuf->sent_at = sc->fill;
	pbuf->cb = cb;
	pbuf->arg = arg;
	pbuf->sc = sc;	/* could be filled in at sc->sr init time */
	/* make sure this is in memory before updating the head */

	/* calculate next head index, do not store */
	next = head + 1;
	if (next >= sc->sr_size)
		next = 0;
	/* update the head - must be last! - the releaser can look at fields
	   in pbuf once we move the head */
	smp_wmb();
	sc->sr_head = next;
	spin_unlock_irqrestore(&sc->alloc_lock, flags);

	/* finish filling in the buffer outside the lock */
	pbuf->start = sc->base_addr + ((start_fill % sc->credits)
							* PIO_BLOCK_SIZE);
	pbuf->size = sc->credits * PIO_BLOCK_SIZE;
	pbuf->end = sc->base_addr + pbuf->size;
	pbuf->block_count = blocks;
	pbuf->qw_written = 0;
	pbuf->carry_bytes = 0;
	pbuf->carry.val64 = 0;
done:
	return pbuf;
}

/*
 * There are at least two entities that can turn on credit return
 * interrupts and they can overlap.  Avoid problems by implementing
 * a count scheme that is enforced by a lock.  The lock is needed because
 * the count and CSR write must be paired.
 */

/*
 * Start credit return interrupts.  This is managed by a count.  If already
 * on, just increment the count.
 */
void sc_add_credit_return_intr(struct send_context *sc)
{
	unsigned long flags;

	/* lock must surround both the count change and the CSR update */
	spin_lock_irqsave(&sc->credit_ctrl_lock, flags);
	if (sc->credit_intr_count == 0) {
		sc->credit_ctrl |= SC(CREDIT_CTRL_CREDIT_INTR_SMASK);
		write_kctxt_csr(sc->dd, sc->hw_context,
			SC(CREDIT_CTRL), sc->credit_ctrl);
	}
	sc->credit_intr_count++;
	spin_unlock_irqrestore(&sc->credit_ctrl_lock, flags);
}

/*
 * Stop credit return interrupts.  This is managed by a count.  Decrement the
 * count, if the last user, then turn the credit interrupts off.
 */
void sc_del_credit_return_intr(struct send_context *sc)
{
	unsigned long flags;

	WARN_ON(sc->credit_intr_count == 0);

	/* lock must surround both the count change and the CSR update */
	spin_lock_irqsave(&sc->credit_ctrl_lock, flags);
	sc->credit_intr_count--;
	if (sc->credit_intr_count == 0) {
		sc->credit_ctrl &= ~SC(CREDIT_CTRL_CREDIT_INTR_SMASK);
		write_kctxt_csr(sc->dd, sc->hw_context,
			SC(CREDIT_CTRL), sc->credit_ctrl);
	}
	spin_unlock_irqrestore(&sc->credit_ctrl_lock, flags);
}

/*
 * The caller must be careful when calling this.  All needint calls
 * must be paired with !needint.
 */
void hfi1_sc_wantpiobuf_intr(struct send_context *sc, u32 needint)
{
	if (needint)
		sc_add_credit_return_intr(sc);
	else
		sc_del_credit_return_intr(sc);
	trace_hfi1_wantpiointr(sc, needint, sc->credit_ctrl);
	if (needint) {
		mmiowb();
		sc_return_credits(sc);
	}
}

/**
 * sc_piobufavail - callback when a PIO buffer is available
 * @sc: the send context
 *
 * This is called from the interrupt handler when a PIO buffer is
 * available after hfi1_verbs_send() returned an error that no buffers were
 * available. Disable the interrupt if there are no more QPs waiting.
 */
static void sc_piobufavail(struct send_context *sc)
{
	struct hfi1_devdata *dd = sc->dd;
	struct hfi1_ibdev *dev = &dd->verbs_dev;
	struct list_head *list;
	struct hfi1_qp *qps[PIO_WAIT_BATCH_SIZE];
	struct hfi1_qp *qp;
	unsigned long flags;
	unsigned i, n = 0;

	if (dd->send_contexts[sc->sw_index].type != SC_KERNEL)
		return;
	list = &sc->piowait;
	/*
	 * Note: checking that the piowait list is empty and clearing
	 * the buffer available interrupt needs to be atomic or we
	 * could end up with QPs on the wait list with the interrupt
	 * disabled.
	 */
	write_seqlock_irqsave(&dev->iowait_lock, flags);
	while (!list_empty(list)) {
		struct iowait *wait;

		if (n == ARRAY_SIZE(qps))
			goto full;
		wait = list_first_entry(list, struct iowait, list);
		qp = container_of(wait, struct hfi1_qp, s_iowait);
		list_del_init(&qp->s_iowait.list);
		/* refcount held until actual wake up */
		qps[n++] = qp;
	}
	/*
	 * Counting: only call wantpiobuf_intr() if there were waiters and they
	 * are now all gone.
	 */
	if (n)
		hfi1_sc_wantpiobuf_intr(sc, 0);
full:
	write_sequnlock_irqrestore(&dev->iowait_lock, flags);

	for (i = 0; i < n; i++)
		hfi1_qp_wakeup(qps[i], HFI1_S_WAIT_PIO);
}

/* translate a send credit update to a bit code of reasons */
static inline int fill_code(u64 hw_free)
{
	int code = 0;

	if (hw_free & CR_STATUS_SMASK)
		code |= PRC_STATUS_ERR;
	if (hw_free & CR_CREDIT_RETURN_DUE_TO_PBC_SMASK)
		code |= PRC_PBC;
	if (hw_free & CR_CREDIT_RETURN_DUE_TO_THRESHOLD_SMASK)
		code |= PRC_THRESHOLD;
	if (hw_free & CR_CREDIT_RETURN_DUE_TO_ERR_SMASK)
		code |= PRC_FILL_ERR;
	if (hw_free & CR_CREDIT_RETURN_DUE_TO_FORCE_SMASK)
		code |= PRC_SC_DISABLE;
	return code;
}

/* use the jiffies compare to get the wrap right */
#define sent_before(a, b) time_before(a, b)	/* a < b */

/*
 * The send context buffer "releaser".
 */
void sc_release_update(struct send_context *sc)
{
	struct pio_buf *pbuf;
	u64 hw_free;
	u32 head, tail;
	unsigned long old_free;
	unsigned long extra;
	unsigned long flags;
	int code;

	if (!sc)
		return;

	spin_lock_irqsave(&sc->release_lock, flags);
	/* update free */
	hw_free = le64_to_cpu(*sc->hw_free);		/* volatile read */
	old_free = sc->free;
	extra = (((hw_free & CR_COUNTER_SMASK) >> CR_COUNTER_SHIFT)
			- (old_free & CR_COUNTER_MASK))
				& CR_COUNTER_MASK;
	sc->free = old_free + extra;
	trace_hfi1_piofree(sc, extra);

	/* call sent buffer callbacks */
	code = -1;				/* code not yet set */
	head = ACCESS_ONCE(sc->sr_head);	/* snapshot the head */
	tail = sc->sr_tail;
	while (head != tail) {
		pbuf = &sc->sr[tail].pbuf;

		if (sent_before(sc->free, pbuf->sent_at)) {
			/* not sent yet */
			break;
		}
		if (pbuf->cb) {
			if (code < 0) /* fill in code on first user */
				code = fill_code(hw_free);
			(*pbuf->cb)(pbuf->arg, code);
		}

		tail++;
		if (tail >= sc->sr_size)
			tail = 0;
	}
	/* update tail, in case we moved it */
	sc->sr_tail = tail;
	spin_unlock_irqrestore(&sc->release_lock, flags);
	sc_piobufavail(sc);
}

/*
 * Send context group releaser.  Argument is the send context that caused
 * the interrupt.  Called from the send context interrupt handler.
 *
 * Call release on all contexts in the group.
 *
 * This routine takes the sc_lock without an irqsave because it is only
 * called from an interrupt handler.  Adjust if that changes.
 */
void sc_group_release_update(struct hfi1_devdata *dd, u32 hw_context)
{
	struct send_context *sc;
	u32 sw_index;
	u32 gc, gc_end;

	spin_lock(&dd->sc_lock);
	sw_index = dd->hw_to_sw[hw_context];
	if (unlikely(sw_index >= dd->num_send_contexts)) {
		dd_dev_err(dd, "%s: invalid hw (%u) to sw (%u) mapping\n",
			__func__, hw_context, sw_index);
		goto done;
	}
	sc = dd->send_contexts[sw_index].sc;
	if (unlikely(!sc))
		goto done;

	gc = group_context(hw_context, sc->group);
	gc_end = gc + group_size(sc->group);
	for (; gc < gc_end; gc++) {
		sw_index = dd->hw_to_sw[gc];
		if (unlikely(sw_index >= dd->num_send_contexts)) {
			dd_dev_err(dd,
				"%s: invalid hw (%u) to sw (%u) mapping\n",
				__func__, hw_context, sw_index);
			continue;
		}
		sc_release_update(dd->send_contexts[sw_index].sc);
	}
done:
	spin_unlock(&dd->sc_lock);
}

int init_pervl_scs(struct hfi1_devdata *dd)
{
	int i;
	u64 mask, all_vl_mask = (u64) 0x80ff; /* VLs 0-7, 15 */
	u32 ctxt;

	dd->vld[15].sc = sc_alloc(dd, SC_KERNEL,
				  dd->rcd[0]->rcvhdrqentsize, dd->node);
	if (!dd->vld[15].sc)
		goto nomem;
	hfi1_init_ctxt(dd->vld[15].sc);
	dd->vld[15].mtu = enum_to_mtu(OPA_MTU_2048);
	for (i = 0; i < num_vls; i++) {
		/*
		 * Since this function does not deal with a specific
		 * receive context but we need the RcvHdrQ entry size,
		 * use the size from rcd[0]. It is guaranteed to be
		 * valid at this point and will remain the same for all
		 * receive contexts.
		 */
		dd->vld[i].sc = sc_alloc(dd, SC_KERNEL,
					 dd->rcd[0]->rcvhdrqentsize, dd->node);
		if (!dd->vld[i].sc)
			goto nomem;

		hfi1_init_ctxt(dd->vld[i].sc);

		/* non VL15 start with the max MTU */
		dd->vld[i].mtu = hfi1_max_mtu;
	}
	sc_enable(dd->vld[15].sc);
	ctxt = dd->vld[15].sc->hw_context;
	mask = all_vl_mask & ~(1LL << 15);
	write_kctxt_csr(dd, ctxt, SC(CHECK_VL), mask);
	dd_dev_info(dd,
		    "Using send context %u(%u) for VL15\n",
		    dd->vld[15].sc->sw_index, ctxt);
	for (i = 0; i < num_vls; i++) {
		sc_enable(dd->vld[i].sc);
		ctxt = dd->vld[i].sc->hw_context;
		mask = all_vl_mask & ~(1LL << i);
		write_kctxt_csr(dd, ctxt, SC(CHECK_VL), mask);
	}
	return 0;
nomem:
	sc_free(dd->vld[15].sc);
	for (i = 0; i < num_vls; i++)
		sc_free(dd->vld[i].sc);
	return -ENOMEM;
}

int init_credit_return(struct hfi1_devdata *dd)
{
	int ret;
	int num_numa;
	int i;

	num_numa = num_online_nodes();
	/* enforce the expectation that the numas are compact */
	for (i = 0; i < num_numa; i++) {
		if (!node_online(i)) {
			dd_dev_err(dd, "NUMA nodes are not compact\n");
			ret = -EINVAL;
			goto done;
		}
	}

	dd->cr_base = kcalloc(
		num_numa,
		sizeof(struct credit_return_base),
		GFP_KERNEL);
	if (!dd->cr_base) {
		dd_dev_err(dd, "Unable to allocate credit return base\n");
		ret = -ENOMEM;
		goto done;
	}
	for (i = 0; i < num_numa; i++) {
		int bytes = TXE_NUM_CONTEXTS * sizeof(struct credit_return);

		set_dev_node(&dd->pcidev->dev, i);
		dd->cr_base[i].va = dma_zalloc_coherent(
					&dd->pcidev->dev,
					bytes,
					&dd->cr_base[i].pa,
					GFP_KERNEL);
		if (dd->cr_base[i].va == NULL) {
			set_dev_node(&dd->pcidev->dev, dd->node);
			dd_dev_err(dd,
				"Unable to allocate credit return DMA range for NUMA %d\n",
				i);
			ret = -ENOMEM;
			goto done;
		}
	}
	set_dev_node(&dd->pcidev->dev, dd->node);

	ret = 0;
done:
	return ret;
}

void free_credit_return(struct hfi1_devdata *dd)
{
	int num_numa;
	int i;

	if (!dd->cr_base)
		return;

	num_numa = num_online_nodes();
	for (i = 0; i < num_numa; i++) {
		if (dd->cr_base[i].va) {
			dma_free_coherent(&dd->pcidev->dev,
				TXE_NUM_CONTEXTS
					* sizeof(struct credit_return),
				dd->cr_base[i].va,
				dd->cr_base[i].pa);
		}
	}
	kfree(dd->cr_base);
	dd->cr_base = NULL;
}
