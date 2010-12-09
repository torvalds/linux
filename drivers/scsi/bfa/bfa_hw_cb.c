/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "bfa_modules.h"
#include "bfi_cbreg.h"

void
bfa_hwcb_reginit(struct bfa_s *bfa)
{
	struct bfa_iocfc_regs_s	*bfa_regs = &bfa->iocfc.bfa_regs;
	void __iomem *kva = bfa_ioc_bar0(&bfa->ioc);
	int			i, q, fn = bfa_ioc_pcifn(&bfa->ioc);

	if (fn == 0) {
		bfa_regs->intr_status = (kva + HOSTFN0_INT_STATUS);
		bfa_regs->intr_mask   = (kva + HOSTFN0_INT_MSK);
	} else {
		bfa_regs->intr_status = (kva + HOSTFN1_INT_STATUS);
		bfa_regs->intr_mask   = (kva + HOSTFN1_INT_MSK);
	}

	for (i = 0; i < BFI_IOC_MAX_CQS; i++) {
		/*
		 * CPE registers
		 */
		q = CPE_Q_NUM(fn, i);
		bfa_regs->cpe_q_pi[i] = (kva + CPE_Q_PI(q));
		bfa_regs->cpe_q_ci[i] = (kva + CPE_Q_CI(q));
		bfa_regs->cpe_q_depth[i] = (kva + CPE_Q_DEPTH(q));

		/*
		 * RME registers
		 */
		q = CPE_Q_NUM(fn, i);
		bfa_regs->rme_q_pi[i] = (kva + RME_Q_PI(q));
		bfa_regs->rme_q_ci[i] = (kva + RME_Q_CI(q));
		bfa_regs->rme_q_depth[i] = (kva + RME_Q_DEPTH(q));
	}
}

void
bfa_hwcb_reqq_ack(struct bfa_s *bfa, int reqq)
{
}

static void
bfa_hwcb_reqq_ack_msix(struct bfa_s *bfa, int reqq)
{
	writel(__HFN_INT_CPE_Q0 << CPE_Q_NUM(bfa_ioc_pcifn(&bfa->ioc), reqq),
			bfa->iocfc.bfa_regs.intr_status);
}

void
bfa_hwcb_rspq_ack(struct bfa_s *bfa, int rspq)
{
}

static void
bfa_hwcb_rspq_ack_msix(struct bfa_s *bfa, int rspq)
{
	writel(__HFN_INT_RME_Q0 << RME_Q_NUM(bfa_ioc_pcifn(&bfa->ioc), rspq),
			bfa->iocfc.bfa_regs.intr_status);
}

void
bfa_hwcb_msix_getvecs(struct bfa_s *bfa, u32 *msix_vecs_bmap,
		 u32 *num_vecs, u32 *max_vec_bit)
{
#define __HFN_NUMINTS	13
	if (bfa_ioc_pcifn(&bfa->ioc) == 0) {
		*msix_vecs_bmap = (__HFN_INT_CPE_Q0 | __HFN_INT_CPE_Q1 |
				   __HFN_INT_CPE_Q2 | __HFN_INT_CPE_Q3 |
				   __HFN_INT_RME_Q0 | __HFN_INT_RME_Q1 |
				   __HFN_INT_RME_Q2 | __HFN_INT_RME_Q3 |
				   __HFN_INT_MBOX_LPU0);
		*max_vec_bit = __HFN_INT_MBOX_LPU0;
	} else {
		*msix_vecs_bmap = (__HFN_INT_CPE_Q4 | __HFN_INT_CPE_Q5 |
				   __HFN_INT_CPE_Q6 | __HFN_INT_CPE_Q7 |
				   __HFN_INT_RME_Q4 | __HFN_INT_RME_Q5 |
				   __HFN_INT_RME_Q6 | __HFN_INT_RME_Q7 |
				   __HFN_INT_MBOX_LPU1);
		*max_vec_bit = __HFN_INT_MBOX_LPU1;
	}

	*msix_vecs_bmap |= (__HFN_INT_ERR_EMC | __HFN_INT_ERR_LPU0 |
			    __HFN_INT_ERR_LPU1 | __HFN_INT_ERR_PSS);
	*num_vecs = __HFN_NUMINTS;
}

/*
 * No special setup required for crossbow -- vector assignments are implicit.
 */
void
bfa_hwcb_msix_init(struct bfa_s *bfa, int nvecs)
{
	int i;

	bfa_assert((nvecs == 1) || (nvecs == __HFN_NUMINTS));

	bfa->msix.nvecs = nvecs;
	if (nvecs == 1) {
		for (i = 0; i < BFA_MSIX_CB_MAX; i++)
			bfa->msix.handler[i] = bfa_msix_all;
		return;
	}

	for (i = BFA_MSIX_CPE_Q0; i <= BFA_MSIX_CPE_Q7; i++)
		bfa->msix.handler[i] = bfa_msix_reqq;

	for (i = BFA_MSIX_RME_Q0; i <= BFA_MSIX_RME_Q7; i++)
		bfa->msix.handler[i] = bfa_msix_rspq;

	for (; i < BFA_MSIX_CB_MAX; i++)
		bfa->msix.handler[i] = bfa_msix_lpu_err;
}

/*
 * Crossbow -- dummy, interrupts are masked
 */
void
bfa_hwcb_msix_install(struct bfa_s *bfa)
{
}

void
bfa_hwcb_msix_uninstall(struct bfa_s *bfa)
{
}

/*
 * No special enable/disable -- vector assignments are implicit.
 */
void
bfa_hwcb_isr_mode_set(struct bfa_s *bfa, bfa_boolean_t msix)
{
	bfa->iocfc.hwif.hw_reqq_ack = bfa_hwcb_reqq_ack_msix;
	bfa->iocfc.hwif.hw_rspq_ack = bfa_hwcb_rspq_ack_msix;
}

void
bfa_hwcb_msix_get_rme_range(struct bfa_s *bfa, u32 *start, u32 *end)
{
	*start = BFA_MSIX_RME_Q0;
	*end = BFA_MSIX_RME_Q7;
}
