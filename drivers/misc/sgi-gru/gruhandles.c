/*
 *              GRU KERNEL MCS INSTRUCTIONS
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include "gru.h"
#include "grulib.h"
#include "grutables.h"

/* 10 sec */
#ifdef CONFIG_IA64
#include <asm/processor.h>
#define GRU_OPERATION_TIMEOUT	(((cycles_t) local_cpu_data->itc_freq)*10)
#else
#include <asm/tsc.h>
#define GRU_OPERATION_TIMEOUT	((cycles_t) tsc_khz*10*1000)
#endif

/* Extract the status field from a kernel handle */
#define GET_MSEG_HANDLE_STATUS(h)	(((*(unsigned long *)(h)) >> 16) & 3)

struct mcs_op_statistic mcs_op_statistics[mcsop_last];

static void update_mcs_stats(enum mcs_op op, unsigned long clks)
{
	atomic_long_inc(&mcs_op_statistics[op].count);
	atomic_long_add(clks, &mcs_op_statistics[op].total);
	if (mcs_op_statistics[op].max < clks)
		mcs_op_statistics[op].max = clks;
}

static void start_instruction(void *h)
{
	unsigned long *w0 = h;

	wmb();		/* setting CMD bit must be last */
	*w0 = *w0 | 1;
	gru_flush_cache(h);
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
		if (GRU_OPERATION_TIMEOUT < (get_cycles() - start_time))
			panic("GRU %p is malfunctioning: start %ld, end %ld\n",
			      h, start_time, (unsigned long)get_cycles());
	}
	if (gru_options & OPT_STATS)
		update_mcs_stats(opc, get_cycles() - start_time);
	return status;
}

int cch_allocate(struct gru_context_configuration_handle *cch)
{
	cch->opc = CCHOP_ALLOCATE;
	start_instruction(cch);
	return wait_instruction_complete(cch, cchop_allocate);
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
	cch->opc = CCHOP_DEALLOCATE;
	start_instruction(cch);
	return wait_instruction_complete(cch, cchop_deallocate);
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

void tfh_write_only(struct gru_tlb_fault_handle *tfh,
				  unsigned long pfn, unsigned long vaddr,
				  int asid, int dirty, int pagesize)
{
	tfh->fillasid = asid;
	tfh->fillvaddr = vaddr;
	tfh->pfn = pfn;
	tfh->dirty = dirty;
	tfh->pagesize = pagesize;
	tfh->opc = TFHOP_WRITE_ONLY;
	start_instruction(tfh);
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

void tfh_restart(struct gru_tlb_fault_handle *tfh)
{
	tfh->opc = TFHOP_RESTART;
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

