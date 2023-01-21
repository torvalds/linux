/*
 * Copyright(c) 2015-2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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

/* global control of PIO send */
void pio_send_control(struct hfi1_devdata *dd, int op)
{
	u64 reg, mask;
	unsigned long flags;
	int write = 1;	/* write sendctrl back */
	int flush = 0;	/* re-read sendctrl to make sure it is flushed */
	int i;

	spin_lock_irqsave(&dd->sendctrl_lock, flags);

	reg = read_csr(dd, SEND_CTRL);
	switch (op) {
	case PSC_GLOBAL_ENABLE:
		reg |= SEND_CTRL_SEND_ENABLE_SMASK;
		fallthrough;
	case PSC_DATA_VL_ENABLE:
		mask = 0;
		for (i = 0; i < ARRAY_SIZE(dd->vld); i++)
			if (!dd->vld[i].mtu)
				mask |= BIT_ULL(i);
		/* Disallow sending on VLs not enabled */
		mask = (mask & SEND_CTRL_UNSUPPORTED_VL_MASK) <<
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
			(void)read_csr(dd, SEND_CTRL); /* flush write */
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

/* Send Context Size (SCS) constants */
#define SCS_ACK_CREDITS  32
#define SCS_VL15_CREDITS 102	/* 3 pkts of 2048B data + 128B header */

#define PIO_THRESHOLD_CEILING 4096

#define PIO_WAIT_BATCH_SIZE 5

/* default send context sizes */
static struct sc_config_sizes sc_config_sizes[SC_MAX] = {
	[SC_KERNEL] = { .size  = SCS_POOL_0,	/* even divide, pool 0 */
			.count = SCC_PER_VL },	/* one per NUMA */
	[SC_ACK]    = { .size  = SCS_ACK_CREDITS,
			.count = SCC_PER_KRCVQ },
	[SC_USER]   = { .size  = SCS_POOL_0,	/* even divide, pool 0 */
			.count = SCC_PER_CPU },	/* one per CPU */
	[SC_VL15]   = { .size  = SCS_VL15_CREDITS,
			.count = 1 },

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
	int centipercent;	/*
				 * 100th of 1% of memory to use, -1 if blocks
				 * already set
				 */
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
	"user",
	"vl15"
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
	int total_blocks = (chip_pio_mem_size(dd) / PIO_BLOCK_SIZE) - 1;
	int total_contexts = 0;
	int fixed_blocks;
	int pool_blocks;
	int used_blocks;
	int cp_total;		/* centipercent total */
	int ab_total;		/* absolute block total */
	int extra;
	int i;

	/*
	 * When SDMA is enabled, kernel context pio packet size is capped by
	 * "piothreshold". Reduce pio buffer allocation for kernel context by
	 * setting it to a fixed size. The allocation allows 3-deep buffering
	 * of the largest pio packets plus up to 128 bytes header, sufficient
	 * to maintain verbs performance.
	 *
	 * When SDMA is disabled, keep the default pooling allocation.
	 */
	if (HFI1_CAP_IS_KSET(SDMA)) {
		u16 max_pkt_size = (piothreshold < PIO_THRESHOLD_CEILING) ?
					 piothreshold : PIO_THRESHOLD_CEILING;
		sc_config_sizes[SC_KERNEL].size =
			3 * (max_pkt_size + 128) / PIO_BLOCK_SIZE;
	}

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
			count = INIT_SC_PER_VL * num_vls;
		} else if (count == SCC_PER_CPU) {
			count = dd->num_rcv_contexts - dd->n_krcv_queues;
		} else if (count < 0) {
			dd_dev_err(
				dd,
				"%s send context invalid count wildcard %d\n",
				sc_type_name(i), count);
			return -EINVAL;
		}
		if (total_contexts + count > chip_send_contexts(dd))
			count = chip_send_contexts(dd) - total_contexts;

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
			context = chip_send_contexts(dd) - index - 1;
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
 * Obtain the credit return addresses, kernel virtual and bus, for the
 * given sc.
 *
 * To understand this routine:
 * o va and dma are arrays of struct credit_return.  One for each physical
 *   send context, per NUMA.
 * o Each send context always looks in its relative location in a struct
 *   credit_return for its credit return.
 * o Each send context in a group must have its return address CSR programmed
 *   with the same value.  Use the address of the first send context in the
 *   group.
 */
static void cr_group_addresses(struct send_context *sc, dma_addr_t *dma)
{
	u32 gc = group_context(sc->hw_context, sc->group);
	u32 index = sc->hw_context & 0x7;

	sc->hw_free = &sc->dd->cr_base[sc->node].va[gc].cr[index];
	*dma = (unsigned long)
	       &((struct credit_return *)sc->dd->cr_base[sc->node].dma)[gc];
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
u32 sc_percent_to_threshold(struct send_context *sc, u32 percent)
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
	u32 hw_context = sc->hw_context;
	int type = sc->type;

	write_kctxt_csr(dd, hw_context,
			SC(CHECK_ENABLE),
			hfi1_pkt_default_send_ctxt_mask(dd, type));
}

static u32 get_buffers_allocated(struct send_context *sc)
{
	int cpu;
	u32 ret = 0;

	for_each_possible_cpu(cpu)
		ret += *per_cpu_ptr(sc->buffers_allocated, cpu);
	return ret;
}

static void reset_buffers_allocated(struct send_context *sc)
{
	int cpu;

	for_each_possible_cpu(cpu)
		(*per_cpu_ptr(sc->buffers_allocated, cpu)) = 0;
}

/*
 * Allocate a NUMA relative send context structure of the given type along
 * with a HW context.
 */
struct send_context *sc_alloc(struct hfi1_devdata *dd, int type,
			      uint hdrqentsize, int numa)
{
	struct send_context_info *sci;
	struct send_context *sc = NULL;
	dma_addr_t dma;
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

	sc = kzalloc_node(sizeof(*sc), GFP_KERNEL, numa);
	if (!sc)
		return NULL;

	sc->buffers_allocated = alloc_percpu(u32);
	if (!sc->buffers_allocated) {
		kfree(sc);
		dd_dev_err(dd,
			   "Cannot allocate buffers_allocated per cpu counters\n"
			  );
		return NULL;
	}

	spin_lock_irqsave(&dd->sc_lock, flags);
	ret = sc_hw_alloc(dd, type, &sw_index, &hw_context);
	if (ret) {
		spin_unlock_irqrestore(&dd->sc_lock, flags);
		free_percpu(sc->buffers_allocated);
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
	seqlock_init(&sc->waitlock);
	INIT_LIST_HEAD(&sc->piowait);
	INIT_WORK(&sc->halt_work, sc_halted);
	init_waitqueue_head(&sc->halt_wait);

	/* grouping is always single context for now */
	sc->group = 0;

	sc->sw_index = sw_index;
	sc->hw_context = hw_context;
	cr_group_addresses(sc, &dma);
	sc->credits = sci->credits;
	sc->size = sc->credits * PIO_BLOCK_SIZE;

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
			(SC(CHECK_PARTITION_KEY_VALUE_MASK) &
			 DEFAULT_PKEY) <<
			SC(CHECK_PARTITION_KEY_VALUE_SHIFT));

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
	reg = dma & SC(CREDIT_RETURN_ADDR_ADDRESS_SMASK);
	write_kctxt_csr(dd, hw_context, SC(CREDIT_RETURN_ADDR), reg);

	/*
	 * Calculate the initial credit return threshold.
	 *
	 * For Ack contexts, set a threshold for half the credits.
	 * For User contexts use the given percentage.  This has been
	 * sanitized on driver start-up.
	 * For Kernel contexts, use the default MTU plus a header
	 * or half the credits, whichever is smaller. This should
	 * work for both the 3-deep buffering allocation and the
	 * pooling allocation.
	 */
	if (type == SC_ACK) {
		thresh = sc_percent_to_threshold(sc, 50);
	} else if (type == SC_USER) {
		thresh = sc_percent_to_threshold(sc,
						 user_credit_return_threshold);
	} else { /* kernel */
		thresh = min(sc_percent_to_threshold(sc, 50),
			     sc_mtu_to_threshold(sc, hfi1_max_mtu,
						 hdrqentsize));
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
		sc->sr = kcalloc_node(sc->sr_size,
				      sizeof(union pio_shadow_ring),
				      GFP_KERNEL, numa);
		if (!sc->sr) {
			sc_free(sc);
			return NULL;
		}
	}

	hfi1_cdbg(PIO,
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
	free_percpu(sc->buffers_allocated);
	kfree(sc);
}

/* disable the context */
void sc_disable(struct send_context *sc)
{
	u64 reg;
	struct pio_buf *pbuf;
	LIST_HEAD(wake_list);

	if (!sc)
		return;

	/* do all steps, even if already disabled */
	spin_lock_irq(&sc->alloc_lock);
	reg = read_kctxt_csr(sc->dd, sc->hw_context, SC(CTRL));
	reg &= ~SC(CTRL_CTXT_ENABLE_SMASK);
	sc->flags &= ~SCF_ENABLED;
	sc_wait_for_packet_egress(sc, 1);
	write_kctxt_csr(sc->dd, sc->hw_context, SC(CTRL), reg);

	/*
	 * Flush any waiters.  Once the context is disabled,
	 * credit return interrupts are stopped (although there
	 * could be one in-process when the context is disabled).
	 * Wait one microsecond for any lingering interrupts, then
	 * proceed with the flush.
	 */
	udelay(1);
	spin_lock(&sc->release_lock);
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
	spin_unlock(&sc->release_lock);

	write_seqlock(&sc->waitlock);
	list_splice_init(&sc->piowait, &wake_list);
	write_sequnlock(&sc->waitlock);
	while (!list_empty(&wake_list)) {
		struct iowait *wait;
		struct rvt_qp *qp;
		struct hfi1_qp_priv *priv;

		wait = list_first_entry(&wake_list, struct iowait, list);
		qp = iowait_to_qp(wait);
		priv = qp->priv;
		list_del_init(&priv->s_iowait.list);
		priv->s_iowait.lock = NULL;
		hfi1_qp_wakeup(qp, RVT_S_WAIT_PIO | HFI1_S_WAIT_PIO_DRAIN);
	}

	spin_unlock_irq(&sc->alloc_lock);
}

/* return SendEgressCtxtStatus.PacketOccupancy */
static u64 packet_occupancy(u64 reg)
{
	return (reg &
		SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_PACKET_OCCUPANCY_SMASK)
		>> SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_PACKET_OCCUPANCY_SHIFT;
}

/* is egress halted on the context? */
static bool egress_halted(u64 reg)
{
	return !!(reg & SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_HALT_STATUS_SMASK);
}

/* is the send context halted? */
static bool is_sc_halted(struct hfi1_devdata *dd, u32 hw_context)
{
	return !!(read_kctxt_csr(dd, hw_context, SC(STATUS)) &
		  SC(STATUS_CTXT_HALTED_SMASK));
}

/**
 * sc_wait_for_packet_egress
 * @sc: valid send context
 * @pause: wait for credit return
 *
 * Wait for packet egress, optionally pause for credit return
 *
 * Egress halt and Context halt are not necessarily the same thing, so
 * check for both.
 *
 * NOTE: The context halt bit may not be set immediately.  Because of this,
 * it is necessary to check the SW SFC_HALTED bit (set in the IRQ) and the HW
 * context bit to determine if the context is halted.
 */
static void sc_wait_for_packet_egress(struct send_context *sc, int pause)
{
	struct hfi1_devdata *dd = sc->dd;
	u64 reg = 0;
	u64 reg_prev;
	u32 loop = 0;

	while (1) {
		reg_prev = reg;
		reg = read_csr(dd, sc->hw_context * 8 +
			       SEND_EGRESS_CTXT_STATUS);
		/* done if any halt bits, SW or HW are set */
		if (sc->flags & SCF_HALTED ||
		    is_sc_halted(dd, sc->hw_context) || egress_halted(reg))
			break;
		reg = packet_occupancy(reg);
		if (reg == 0)
			break;
		/* counter is reset if occupancy count changes */
		if (reg != reg_prev)
			loop = 0;
		if (loop > 50000) {
			/* timed out - bounce the link */
			dd_dev_err(dd,
				   "%s: context %u(%u) timeout waiting for packets to egress, remaining count %u, bouncing link\n",
				   __func__, sc->sw_index,
				   sc->hw_context, (u32)reg);
			queue_work(dd->pport->link_wq,
				   &dd->pport->link_bounce_work);
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
			count = get_buffers_allocated(sc);
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
		if (sc->flags & SCF_LINK_DOWN)
			continue;

		sc_enable(sc);	/* will clear the sc frozen flag */
	}
}

/**
 * pio_kernel_linkup() - Re-enable send contexts after linkup event
 * @dd: valid devive data
 *
 * When the link goes down, the freeze path is taken.  However, a link down
 * event is different from a freeze because if the send context is re-enabled
 * whowever is sending data will start sending data again, which will hang
 * any QP that is sending data.
 *
 * The freeze path now looks at the type of event that occurs and takes this
 * path for link down event.
 */
void pio_kernel_linkup(struct hfi1_devdata *dd)
{
	struct send_context *sc;
	int i;

	for (i = 0; i < dd->num_send_contexts; i++) {
		sc = dd->send_contexts[i].sc;
		if (!sc || !(sc->flags & SCF_LINK_DOWN) || sc->type == SC_USER)
			continue;

		sc_enable(sc);	/* will clear the sc link down flag */
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
	sc->fill_wrap = 0;
	sc->sr_head = 0;
	sc->sr_tail = 0;
	sc->flags = 0;
	/* the alloc lock insures no fast path allocation */
	reset_buffers_allocated(sc);

	/*
	 * Clear all per-context errors.  Some of these will be set when
	 * we are re-enabling after a context halt.  Now that the context
	 * is disabled, the halt will not clear until after the PIO init
	 * engine runs below.
	 */
	reg = read_kctxt_csr(dd, sc->hw_context, SC(ERR_STATUS));
	if (reg)
		write_kctxt_csr(dd, sc->hw_context, SC(ERR_CLEAR), reg);

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

	/* stop buffer allocations */
	spin_lock_irqsave(&sc->alloc_lock, flags);
	/* mark the context */
	sc->flags |= flag;
	sc->flags &= ~SCF_ENABLED;
	spin_unlock_irqrestore(&sc->alloc_lock, flags);
	wake_up(&sc->halt_wait);
}

#define BLOCK_DWORDS (PIO_BLOCK_SIZE / sizeof(u32))
#define dwords_to_blocks(x) DIV_ROUND_UP(x, BLOCK_DWORDS)

/*
 * The send context buffer "allocator".
 *
 * @sc: the PIO send context we are allocating from
 * @len: length of whole packet - including PBC - in dwords
 * @cb: optional callback to call when the buffer is finished sending
 * @arg: argument for cb
 *
 * Return a pointer to a PIO buffer, NULL if not enough room, -ECOMM
 * when link is down.
 */
struct pio_buf *sc_buffer_alloc(struct send_context *sc, u32 dw_len,
				pio_release_cb cb, void *arg)
{
	struct pio_buf *pbuf = NULL;
	unsigned long flags;
	unsigned long avail;
	unsigned long blocks = dwords_to_blocks(dw_len);
	u32 fill_wrap;
	int trycount = 0;
	u32 head, next;

	spin_lock_irqsave(&sc->alloc_lock, flags);
	if (!(sc->flags & SCF_ENABLED)) {
		spin_unlock_irqrestore(&sc->alloc_lock, flags);
		return ERR_PTR(-ECOMM);
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
		sc->alloc_free = READ_ONCE(sc->free);
		avail =
			(unsigned long)sc->credits -
			(sc->fill - sc->alloc_free);
		if (blocks > avail) {
			/* still no room, actively update */
			sc_release_update(sc);
			sc->alloc_free = READ_ONCE(sc->free);
			trycount++;
			goto retry;
		}
	}

	/* there is enough room */

	preempt_disable();
	this_cpu_inc(*sc->buffers_allocated);

	/* read this once */
	head = sc->sr_head;

	/* "allocate" the buffer */
	sc->fill += blocks;
	fill_wrap = sc->fill_wrap;
	sc->fill_wrap += blocks;
	if (sc->fill_wrap >= sc->credits)
		sc->fill_wrap = sc->fill_wrap - sc->credits;

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
	/*
	 * update the head - must be last! - the releaser can look at fields
	 * in pbuf once we move the head
	 */
	smp_wmb();
	sc->sr_head = next;
	spin_unlock_irqrestore(&sc->alloc_lock, flags);

	/* finish filling in the buffer outside the lock */
	pbuf->start = sc->base_addr + fill_wrap * PIO_BLOCK_SIZE;
	pbuf->end = sc->base_addr + sc->size;
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
	if (needint)
		sc_return_credits(sc);
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
	struct list_head *list;
	struct rvt_qp *qps[PIO_WAIT_BATCH_SIZE];
	struct rvt_qp *qp;
	struct hfi1_qp_priv *priv;
	unsigned long flags;
	uint i, n = 0, top_idx = 0;

	if (dd->send_contexts[sc->sw_index].type != SC_KERNEL &&
	    dd->send_contexts[sc->sw_index].type != SC_VL15)
		return;
	list = &sc->piowait;
	/*
	 * Note: checking that the piowait list is empty and clearing
	 * the buffer available interrupt needs to be atomic or we
	 * could end up with QPs on the wait list with the interrupt
	 * disabled.
	 */
	write_seqlock_irqsave(&sc->waitlock, flags);
	while (!list_empty(list)) {
		struct iowait *wait;

		if (n == ARRAY_SIZE(qps))
			break;
		wait = list_first_entry(list, struct iowait, list);
		iowait_get_priority(wait);
		qp = iowait_to_qp(wait);
		priv = qp->priv;
		list_del_init(&priv->s_iowait.list);
		priv->s_iowait.lock = NULL;
		if (n) {
			priv = qps[top_idx]->priv;
			top_idx = iowait_priority_update_top(wait,
							     &priv->s_iowait,
							     n, top_idx);
		}

		/* refcount held until actual wake up */
		qps[n++] = qp;
	}
	/*
	 * If there had been waiters and there are more
	 * insure that we redo the force to avoid a potential hang.
	 */
	if (n) {
		hfi1_sc_wantpiobuf_intr(sc, 0);
		if (!list_empty(list))
			hfi1_sc_wantpiobuf_intr(sc, 1);
	}
	write_sequnlock_irqrestore(&sc->waitlock, flags);

	/* Wake up the top-priority one first */
	if (n)
		hfi1_qp_wakeup(qps[top_idx],
			       RVT_S_WAIT_PIO | HFI1_S_WAIT_PIO_DRAIN);
	for (i = 0; i < n; i++)
		if (i != top_idx)
			hfi1_qp_wakeup(qps[i],
				       RVT_S_WAIT_PIO | HFI1_S_WAIT_PIO_DRAIN);
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
	unsigned long free;
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
	free = old_free + extra;
	trace_hfi1_piofree(sc, extra);

	/* call sent buffer callbacks */
	code = -1;				/* code not yet set */
	head = READ_ONCE(sc->sr_head);	/* snapshot the head */
	tail = sc->sr_tail;
	while (head != tail) {
		pbuf = &sc->sr[tail].pbuf;

		if (sent_before(free, pbuf->sent_at)) {
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
	sc->sr_tail = tail;
	/* make sure tail is updated before free */
	smp_wmb();
	sc->free = free;
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

/*
 * pio_select_send_context_vl() - select send context
 * @dd: devdata
 * @selector: a spreading factor
 * @vl: this vl
 *
 * This function returns a send context based on the selector and a vl.
 * The mapping fields are protected by RCU
 */
struct send_context *pio_select_send_context_vl(struct hfi1_devdata *dd,
						u32 selector, u8 vl)
{
	struct pio_vl_map *m;
	struct pio_map_elem *e;
	struct send_context *rval;

	/*
	 * NOTE This should only happen if SC->VL changed after the initial
	 * checks on the QP/AH
	 * Default will return VL0's send context below
	 */
	if (unlikely(vl >= num_vls)) {
		rval = NULL;
		goto done;
	}

	rcu_read_lock();
	m = rcu_dereference(dd->pio_map);
	if (unlikely(!m)) {
		rcu_read_unlock();
		return dd->vld[0].sc;
	}
	e = m->map[vl & m->mask];
	rval = e->ksc[selector & e->mask];
	rcu_read_unlock();

done:
	rval = !rval ? dd->vld[0].sc : rval;
	return rval;
}

/*
 * pio_select_send_context_sc() - select send context
 * @dd: devdata
 * @selector: a spreading factor
 * @sc5: the 5 bit sc
 *
 * This function returns an send context based on the selector and an sc
 */
struct send_context *pio_select_send_context_sc(struct hfi1_devdata *dd,
						u32 selector, u8 sc5)
{
	u8 vl = sc_to_vlt(dd, sc5);

	return pio_select_send_context_vl(dd, selector, vl);
}

/*
 * Free the indicated map struct
 */
static void pio_map_free(struct pio_vl_map *m)
{
	int i;

	for (i = 0; m && i < m->actual_vls; i++)
		kfree(m->map[i]);
	kfree(m);
}

/*
 * Handle RCU callback
 */
static void pio_map_rcu_callback(struct rcu_head *list)
{
	struct pio_vl_map *m = container_of(list, struct pio_vl_map, list);

	pio_map_free(m);
}

/*
 * Set credit return threshold for the kernel send context
 */
static void set_threshold(struct hfi1_devdata *dd, int scontext, int i)
{
	u32 thres;

	thres = min(sc_percent_to_threshold(dd->kernel_send_context[scontext],
					    50),
		    sc_mtu_to_threshold(dd->kernel_send_context[scontext],
					dd->vld[i].mtu,
					dd->rcd[0]->rcvhdrqentsize));
	sc_set_cr_threshold(dd->kernel_send_context[scontext], thres);
}

/*
 * pio_map_init - called when #vls change
 * @dd: hfi1_devdata
 * @port: port number
 * @num_vls: number of vls
 * @vl_scontexts: per vl send context mapping (optional)
 *
 * This routine changes the mapping based on the number of vls.
 *
 * vl_scontexts is used to specify a non-uniform vl/send context
 * loading. NULL implies auto computing the loading and giving each
 * VL an uniform distribution of send contexts per VL.
 *
 * The auto algorithm computers the sc_per_vl and the number of extra
 * send contexts. Any extra send contexts are added from the last VL
 * on down
 *
 * rcu locking is used here to control access to the mapping fields.
 *
 * If either the num_vls or num_send_contexts are non-power of 2, the
 * array sizes in the struct pio_vl_map and the struct pio_map_elem are
 * rounded up to the next highest power of 2 and the first entry is
 * reused in a round robin fashion.
 *
 * If an error occurs the map change is not done and the mapping is not
 * chaged.
 *
 */
int pio_map_init(struct hfi1_devdata *dd, u8 port, u8 num_vls, u8 *vl_scontexts)
{
	int i, j;
	int extra, sc_per_vl;
	int scontext = 1;
	int num_kernel_send_contexts = 0;
	u8 lvl_scontexts[OPA_MAX_VLS];
	struct pio_vl_map *oldmap, *newmap;

	if (!vl_scontexts) {
		for (i = 0; i < dd->num_send_contexts; i++)
			if (dd->send_contexts[i].type == SC_KERNEL)
				num_kernel_send_contexts++;
		/* truncate divide */
		sc_per_vl = num_kernel_send_contexts / num_vls;
		/* extras */
		extra = num_kernel_send_contexts % num_vls;
		vl_scontexts = lvl_scontexts;
		/* add extras from last vl down */
		for (i = num_vls - 1; i >= 0; i--, extra--)
			vl_scontexts[i] = sc_per_vl + (extra > 0 ? 1 : 0);
	}
	/* build new map */
	newmap = kzalloc(sizeof(*newmap) +
			 roundup_pow_of_two(num_vls) *
			 sizeof(struct pio_map_elem *),
			 GFP_KERNEL);
	if (!newmap)
		goto bail;
	newmap->actual_vls = num_vls;
	newmap->vls = roundup_pow_of_two(num_vls);
	newmap->mask = (1 << ilog2(newmap->vls)) - 1;
	for (i = 0; i < newmap->vls; i++) {
		/* save for wrap around */
		int first_scontext = scontext;

		if (i < newmap->actual_vls) {
			int sz = roundup_pow_of_two(vl_scontexts[i]);

			/* only allocate once */
			newmap->map[i] = kzalloc(sizeof(*newmap->map[i]) +
						 sz * sizeof(struct
							     send_context *),
						 GFP_KERNEL);
			if (!newmap->map[i])
				goto bail;
			newmap->map[i]->mask = (1 << ilog2(sz)) - 1;
			/*
			 * assign send contexts and
			 * adjust credit return threshold
			 */
			for (j = 0; j < sz; j++) {
				if (dd->kernel_send_context[scontext]) {
					newmap->map[i]->ksc[j] =
					dd->kernel_send_context[scontext];
					set_threshold(dd, scontext, i);
				}
				if (++scontext >= first_scontext +
						  vl_scontexts[i])
					/* wrap back to first send context */
					scontext = first_scontext;
			}
		} else {
			/* just re-use entry without allocating */
			newmap->map[i] = newmap->map[i % num_vls];
		}
		scontext = first_scontext + vl_scontexts[i];
	}
	/* newmap in hand, save old map */
	spin_lock_irq(&dd->pio_map_lock);
	oldmap = rcu_dereference_protected(dd->pio_map,
					   lockdep_is_held(&dd->pio_map_lock));

	/* publish newmap */
	rcu_assign_pointer(dd->pio_map, newmap);

	spin_unlock_irq(&dd->pio_map_lock);
	/* success, free any old map after grace period */
	if (oldmap)
		call_rcu(&oldmap->list, pio_map_rcu_callback);
	return 0;
bail:
	/* free any partial allocation */
	pio_map_free(newmap);
	return -ENOMEM;
}

void free_pio_map(struct hfi1_devdata *dd)
{
	/* Free PIO map if allocated */
	if (rcu_access_pointer(dd->pio_map)) {
		spin_lock_irq(&dd->pio_map_lock);
		pio_map_free(rcu_access_pointer(dd->pio_map));
		RCU_INIT_POINTER(dd->pio_map, NULL);
		spin_unlock_irq(&dd->pio_map_lock);
		synchronize_rcu();
	}
	kfree(dd->kernel_send_context);
	dd->kernel_send_context = NULL;
}

int init_pervl_scs(struct hfi1_devdata *dd)
{
	int i;
	u64 mask, all_vl_mask = (u64)0x80ff; /* VLs 0-7, 15 */
	u64 data_vls_mask = (u64)0x00ff; /* VLs 0-7 */
	u32 ctxt;
	struct hfi1_pportdata *ppd = dd->pport;

	dd->vld[15].sc = sc_alloc(dd, SC_VL15,
				  dd->rcd[0]->rcvhdrqentsize, dd->node);
	if (!dd->vld[15].sc)
		return -ENOMEM;

	hfi1_init_ctxt(dd->vld[15].sc);
	dd->vld[15].mtu = enum_to_mtu(OPA_MTU_2048);

	dd->kernel_send_context = kcalloc_node(dd->num_send_contexts,
					       sizeof(struct send_context *),
					       GFP_KERNEL, dd->node);
	if (!dd->kernel_send_context)
		goto freesc15;

	dd->kernel_send_context[0] = dd->vld[15].sc;

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
		dd->kernel_send_context[i + 1] = dd->vld[i].sc;
		hfi1_init_ctxt(dd->vld[i].sc);
		/* non VL15 start with the max MTU */
		dd->vld[i].mtu = hfi1_max_mtu;
	}
	for (i = num_vls; i < INIT_SC_PER_VL * num_vls; i++) {
		dd->kernel_send_context[i + 1] =
		sc_alloc(dd, SC_KERNEL, dd->rcd[0]->rcvhdrqentsize, dd->node);
		if (!dd->kernel_send_context[i + 1])
			goto nomem;
		hfi1_init_ctxt(dd->kernel_send_context[i + 1]);
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
		mask = all_vl_mask & ~(data_vls_mask);
		write_kctxt_csr(dd, ctxt, SC(CHECK_VL), mask);
	}
	for (i = num_vls; i < INIT_SC_PER_VL * num_vls; i++) {
		sc_enable(dd->kernel_send_context[i + 1]);
		ctxt = dd->kernel_send_context[i + 1]->hw_context;
		mask = all_vl_mask & ~(data_vls_mask);
		write_kctxt_csr(dd, ctxt, SC(CHECK_VL), mask);
	}

	if (pio_map_init(dd, ppd->port - 1, num_vls, NULL))
		goto nomem;
	return 0;

nomem:
	for (i = 0; i < num_vls; i++) {
		sc_free(dd->vld[i].sc);
		dd->vld[i].sc = NULL;
	}

	for (i = num_vls; i < INIT_SC_PER_VL * num_vls; i++)
		sc_free(dd->kernel_send_context[i + 1]);

	kfree(dd->kernel_send_context);
	dd->kernel_send_context = NULL;

freesc15:
	sc_free(dd->vld[15].sc);
	return -ENOMEM;
}

int init_credit_return(struct hfi1_devdata *dd)
{
	int ret;
	int i;

	dd->cr_base = kcalloc(
		node_affinity.num_possible_nodes,
		sizeof(struct credit_return_base),
		GFP_KERNEL);
	if (!dd->cr_base) {
		ret = -ENOMEM;
		goto done;
	}
	for_each_node_with_cpus(i) {
		int bytes = TXE_NUM_CONTEXTS * sizeof(struct credit_return);

		set_dev_node(&dd->pcidev->dev, i);
		dd->cr_base[i].va = dma_alloc_coherent(&dd->pcidev->dev,
						       bytes,
						       &dd->cr_base[i].dma,
						       GFP_KERNEL);
		if (!dd->cr_base[i].va) {
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
	int i;

	if (!dd->cr_base)
		return;
	for (i = 0; i < node_affinity.num_possible_nodes; i++) {
		if (dd->cr_base[i].va) {
			dma_free_coherent(&dd->pcidev->dev,
					  TXE_NUM_CONTEXTS *
					  sizeof(struct credit_return),
					  dd->cr_base[i].va,
					  dd->cr_base[i].dma);
		}
	}
	kfree(dd->cr_base);
	dd->cr_base = NULL;
}

void seqfile_dump_sci(struct seq_file *s, u32 i,
		      struct send_context_info *sci)
{
	struct send_context *sc = sci->sc;
	u64 reg;

	seq_printf(s, "SCI %u: type %u base %u credits %u\n",
		   i, sci->type, sci->base, sci->credits);
	seq_printf(s, "  flags 0x%x sw_inx %u hw_ctxt %u grp %u\n",
		   sc->flags,  sc->sw_index, sc->hw_context, sc->group);
	seq_printf(s, "  sr_size %u credits %u sr_head %u sr_tail %u\n",
		   sc->sr_size, sc->credits, sc->sr_head, sc->sr_tail);
	seq_printf(s, "  fill %lu free %lu fill_wrap %u alloc_free %lu\n",
		   sc->fill, sc->free, sc->fill_wrap, sc->alloc_free);
	seq_printf(s, "  credit_intr_count %u credit_ctrl 0x%llx\n",
		   sc->credit_intr_count, sc->credit_ctrl);
	reg = read_kctxt_csr(sc->dd, sc->hw_context, SC(CREDIT_STATUS));
	seq_printf(s, "  *hw_free %llu CurrentFree %llu LastReturned %llu\n",
		   (le64_to_cpu(*sc->hw_free) & CR_COUNTER_SMASK) >>
		    CR_COUNTER_SHIFT,
		   (reg >> SC(CREDIT_STATUS_CURRENT_FREE_COUNTER_SHIFT)) &
		    SC(CREDIT_STATUS_CURRENT_FREE_COUNTER_MASK),
		   reg & SC(CREDIT_STATUS_LAST_RETURNED_COUNTER_SMASK));
}
