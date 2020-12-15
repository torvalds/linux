// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *              GRU KERNEL MCS INSTRUCTIONS
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/kernel.h>
#include "gru.h"
#include "grulib.h"
#include "grutables.h"

/* 10 sec */
#ifdef CONFIG_IA64
#include <asm/processor.h>
#define GRU_OPERATION_TIMEOUT	(((cycles_t) local_cpu_data->itc_freq)*10)
#define CLKS2NSEC(c)		((c) *1000000000 / local_cpu_data->itc_freq)
#else
#include <linux/sync_core.h>
#include <asm/tsc.h>
#define GRU_OPERATION_TIMEOUT	((cycles_t) tsc_khz*10*1000)
#define CLKS2NSEC(c)		((c) * 1000000 / tsc_khz)
#endif

/* Extract the status field from a kernel handle */
#define GET_MSEG_HANDLE_STATUS(h)	(((*(unsigned long *)(h)) >> 16) & 3)

struct mcs_op_statistic mcs_op_statistics[mcsop_last];

static void update_mcs_stats(enum mcs_op op, unsigned long clks)
{
	unsigned long nsec;

	nsec = CLKS2NSEC(clks);
	atomic_long_inc(&mcs_op_statistics[op].count);
	atomic_long_add(nsec, &mcs_op_statistics[op].total);
	if (mcs_op_statistics[op].max < nsec)
		mcs_op_statistics[op].max = nsec;
}

static void start_instruction(void *h)
{
	unsigned long *w0 = h;

	wmb();		/* setting CMD/STATUS bits must be last */
	*w0 = *w0 | 0x20001;
	gru_flush_cache(h);
}

static void report_instruction_timeout(void *h)
{
	unsigned long goff = GSEGPOFF((unsigned long)h);
	char *id = "???";

	if (TYPE_IS(CCH, goff))
		id = "CCH";
	else if (TYPE_IS(TGH, goff))
		id = "TGH";
	else if (TYPE_IS(TFH, goff))
		id = "TFH";

	panic(KERN_ALERT "GRU %p (%s) is malfunctioning\n", h, id);
}

static int wait_instruction_complete(void *h, enum mcs_op opc)
{
	int status;
	unsigned long start_time = get_cycles();

	while (1) {
		cpu_relax();
		status = GET_MSEG_HANDLE_STATUS(h);
		if (status != CCHSTATUS_ACTIVE)
			break;
		if (GRU_OPERATION_TIMEOUT < (get_cycles() - start_time)) {
			report_instruction_timeout(h);
			start_time = get_cycles();
		}
	}
	if (gru_options & OPT_STATS)
		update_mcs_stats(opc, get_cycles() - start_time);
	return status;
}

int cch_allocate(struct gru_context_configuration_handle *cch)
{
	int ret;

	cch->opc = CCHOP_ALLOCATE;
	start_instruction(cch);
	ret = wait_instruction_complete(cch, cchop_allocate);

	/*
	 * Stop speculation into the GSEG being mapped by the previous ALLOCATE.
	 * The GSEG memory does not exist until the ALLOCATE completes.
	 */
	sync_core();
	return ret;
}

int cch_start(struct gru_context_configuration_handle *cch)
{
	cch->opc = CCHOP_START;
	start_instruction(cch);
	return wait_instruction_complete(cch, cchop_start);
}

int cch_interrupt(struct gru_context_configuration_handle *cch)
{
	cch->opc = CCHOP_INTERRUPT;
	start_instruction(cch);
	return wait_instruction_complete(cch, cchop_interrupt);
}

int cch_deallocate(struct gru_context_configuration_handle *cch)
{
	int ret;

	cch->opc = CCHOP_DEALLOCATE;
	start_instruction(cch);
	ret = wait_instruction_complete(cch, cchop_deallocate);

	/*
	 * Stop speculation into the GSEG being unmapped by the previous
	 * DEALLOCATE.
	 */
	sync_core();
	return ret;
}

int cch_interrupt_sync(struct gru_context_configuration_handle
				     *cch)
{
	cch->opc = CCHOP_INTERRUPT_SYNC;
	start_instruction(cch);
	return wait_instruction_complete(cch, cchop_interrupt_sync);
}

int tgh_invalidate(struct gru_tlb_global_handle *tgh,
				 unsigned long vaddr, unsigned long vaddrmask,
				 int asid, int pagesize, int global, int n,
				 unsigned short ctxbitmap)
{
	tgh->vaddr = vaddr;
	tgh->asid = asid;
	tgh->pagesize = pagesize;
	tgh->n = n;
	tgh->global = global;
	tgh->vaddrmask = vaddrmask;
	tgh->ctxbitmap = ctxbitmap;
	tgh->opc = TGHOP_TLBINV;
	start_instruction(tgh);
	return wait_instruction_complete(tgh, tghop_invalidate);
}

int tfh_write_only(struct gru_tlb_fault_handle *tfh,
				  unsigned long paddr, int gaa,
				  unsigned long vaddr, int asid, int dirty,
				  int pagesize)
{
	tfh->fillasid = asid;
	tfh->fillvaddr = vaddr;
	tfh->pfn = paddr >> GRU_PADDR_SHIFT;
	tfh->gaa = gaa;
	tfh->dirty = dirty;
	tfh->pagesize = pagesize;
	tfh->opc = TFHOP_WRITE_ONLY;
	start_instruction(tfh);
	return wait_instruction_complete(tfh, tfhop_write_only);
}

void tfh_write_restart(struct gru_tlb_fault_handle *tfh,
				     unsigned long paddr, int gaa,
				     unsigned long vaddr, int asid, int dirty,
				     int pagesize)
{
	tfh->fillasid = asid;
	tfh->fillvaddr = vaddr;
	tfh->pfn = paddr >> GRU_PADDR_SHIFT;
	tfh->gaa = gaa;
	tfh->dirty = dirty;
	tfh->pagesize = pagesize;
	tfh->opc = TFHOP_WRITE_RESTART;
	start_instruction(tfh);
}

void tfh_user_polling_mode(struct gru_tlb_fault_handle *tfh)
{
	tfh->opc = TFHOP_USER_POLLING_MODE;
	start_instruction(tfh);
}

void tfh_exception(struct gru_tlb_fault_handle *tfh)
{
	tfh->opc = TFHOP_EXCEPTION;
	start_instruction(tfh);
}

