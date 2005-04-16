/* kernel/perfmon_fsl_booke.c
 * Freescale Book-E Performance Monitor code
 *
 * Author: Andy Fleming
 * Copyright (c) 2004 Freescale Semiconductor, Inc
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/prctl.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/reg.h>
#include <asm/xmon.h>
#include <asm/perfmon.h>

static inline u32 get_pmlca(int ctr);
static inline void set_pmlca(int ctr, u32 pmlca);

static inline u32 get_pmlca(int ctr)
{
	u32 pmlca;

	switch (ctr) {
		case 0:
			pmlca = mfpmr(PMRN_PMLCA0);
			break;
		case 1:
			pmlca = mfpmr(PMRN_PMLCA1);
			break;
		case 2:
			pmlca = mfpmr(PMRN_PMLCA2);
			break;
		case 3:
			pmlca = mfpmr(PMRN_PMLCA3);
			break;
		default:
			panic("Bad ctr number\n");
	}

	return pmlca;
}

static inline void set_pmlca(int ctr, u32 pmlca)
{
	switch (ctr) {
		case 0:
			mtpmr(PMRN_PMLCA0, pmlca);
			break;
		case 1:
			mtpmr(PMRN_PMLCA1, pmlca);
			break;
		case 2:
			mtpmr(PMRN_PMLCA2, pmlca);
			break;
		case 3:
			mtpmr(PMRN_PMLCA3, pmlca);
			break;
		default:
			panic("Bad ctr number\n");
	}
}

void init_pmc_stop(int ctr)
{
	u32 pmlca = (PMLCA_FC | PMLCA_FCS | PMLCA_FCU |
			PMLCA_FCM1 | PMLCA_FCM0);
	u32 pmlcb = 0;

	switch (ctr) {
		case 0:
			mtpmr(PMRN_PMLCA0, pmlca);
			mtpmr(PMRN_PMLCB0, pmlcb);
			break;
		case 1:
			mtpmr(PMRN_PMLCA1, pmlca);
			mtpmr(PMRN_PMLCB1, pmlcb);
			break;
		case 2:
			mtpmr(PMRN_PMLCA2, pmlca);
			mtpmr(PMRN_PMLCB2, pmlcb);
			break;
		case 3:
			mtpmr(PMRN_PMLCA3, pmlca);
			mtpmr(PMRN_PMLCB3, pmlcb);
			break;
		default:
			panic("Bad ctr number!\n");
	}
}

void set_pmc_event(int ctr, int event)
{
	u32 pmlca;

	pmlca = get_pmlca(ctr);

	pmlca = (pmlca & ~PMLCA_EVENT_MASK) |
		((event << PMLCA_EVENT_SHIFT) &
		 PMLCA_EVENT_MASK);

	set_pmlca(ctr, pmlca);
}

void set_pmc_user_kernel(int ctr, int user, int kernel)
{
	u32 pmlca;

	pmlca = get_pmlca(ctr);

	if(user)
		pmlca &= ~PMLCA_FCU;
	else
		pmlca |= PMLCA_FCU;

	if(kernel)
		pmlca &= ~PMLCA_FCS;
	else
		pmlca |= PMLCA_FCS;

	set_pmlca(ctr, pmlca);
}

void set_pmc_marked(int ctr, int mark0, int mark1)
{
	u32 pmlca = get_pmlca(ctr);

	if(mark0)
		pmlca &= ~PMLCA_FCM0;
	else
		pmlca |= PMLCA_FCM0;

	if(mark1)
		pmlca &= ~PMLCA_FCM1;
	else
		pmlca |= PMLCA_FCM1;

	set_pmlca(ctr, pmlca);
}

void pmc_start_ctr(int ctr, int enable)
{
	u32 pmlca = get_pmlca(ctr);

	pmlca &= ~PMLCA_FC;

	if (enable)
		pmlca |= PMLCA_CE;
	else
		pmlca &= ~PMLCA_CE;

	set_pmlca(ctr, pmlca);
}

void pmc_start_ctrs(int enable)
{
	u32 pmgc0 = mfpmr(PMRN_PMGC0);

	pmgc0 &= ~PMGC0_FAC;
	pmgc0 |= PMGC0_FCECE;

	if (enable)
		pmgc0 |= PMGC0_PMIE;
	else
		pmgc0 &= ~PMGC0_PMIE;

	mtpmr(PMRN_PMGC0, pmgc0);
}

void pmc_stop_ctrs(void)
{
	u32 pmgc0 = mfpmr(PMRN_PMGC0);

	pmgc0 |= PMGC0_FAC;

	pmgc0 &= ~(PMGC0_PMIE | PMGC0_FCECE);

	mtpmr(PMRN_PMGC0, pmgc0);
}

void dump_pmcs(void)
{
	printk("pmgc0: %x\n", mfpmr(PMRN_PMGC0));
	printk("pmc\t\tpmlca\t\tpmlcb\n");
	printk("%8x\t%8x\t%8x\n", mfpmr(PMRN_PMC0),
			mfpmr(PMRN_PMLCA0), mfpmr(PMRN_PMLCB0));
	printk("%8x\t%8x\t%8x\n", mfpmr(PMRN_PMC1),
			mfpmr(PMRN_PMLCA1), mfpmr(PMRN_PMLCB1));
	printk("%8x\t%8x\t%8x\n", mfpmr(PMRN_PMC2),
			mfpmr(PMRN_PMLCA2), mfpmr(PMRN_PMLCB2));
	printk("%8x\t%8x\t%8x\n", mfpmr(PMRN_PMC3),
			mfpmr(PMRN_PMLCA3), mfpmr(PMRN_PMLCB3));
}

EXPORT_SYMBOL(init_pmc_stop);
EXPORT_SYMBOL(set_pmc_event);
EXPORT_SYMBOL(set_pmc_user_kernel);
EXPORT_SYMBOL(set_pmc_marked);
EXPORT_SYMBOL(pmc_start_ctr);
EXPORT_SYMBOL(pmc_start_ctrs);
EXPORT_SYMBOL(pmc_stop_ctrs);
EXPORT_SYMBOL(dump_pmcs);
