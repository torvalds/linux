/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
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

#include <bfa_priv.h>
#include <bfi/bfi_ctreg.h>
#include <bfa_ioc.h>

BFA_TRC_FILE(HAL, IOCFC_CT);

static u32 __ct_msix_err_vec_reg[] = {
	HOST_MSIX_ERR_INDEX_FN0,
	HOST_MSIX_ERR_INDEX_FN1,
	HOST_MSIX_ERR_INDEX_FN2,
	HOST_MSIX_ERR_INDEX_FN3,
};

static void
bfa_hwct_msix_lpu_err_set(struct bfa_s *bfa, bfa_boolean_t msix, int vec)
{
	int fn = bfa_ioc_pcifn(&bfa->ioc);
	bfa_os_addr_t kva = bfa_ioc_bar0(&bfa->ioc);

	if (msix)
		bfa_reg_write(kva + __ct_msix_err_vec_reg[fn], vec);
	else
		bfa_reg_write(kva + __ct_msix_err_vec_reg[fn], 0);
}

/**
 * Dummy interrupt handler for handling spurious interrupt during chip-reinit.
 */
static void
bfa_hwct_msix_dummy(struct bfa_s *bfa, int vec)
{
}

void
bfa_hwct_reginit(struct bfa_s *bfa)
{
	struct bfa_iocfc_regs_s	*bfa_regs = &bfa->iocfc.bfa_regs;
	bfa_os_addr_t		kva = bfa_ioc_bar0(&bfa->ioc);
	int             	i, q, fn = bfa_ioc_pcifn(&bfa->ioc);

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
		bfa_regs->cpe_q_pi[i] = (kva + CPE_PI_PTR_Q(q << 5));
		bfa_regs->cpe_q_ci[i] = (kva + CPE_CI_PTR_Q(q << 5));
		bfa_regs->cpe_q_depth[i] = (kva + CPE_DEPTH_Q(q << 5));
		bfa_regs->cpe_q_ctrl[i] = (kva + CPE_QCTRL_Q(q << 5));

		/*
		 * RME registers
		 */
		q = CPE_Q_NUM(fn, i);
		bfa_regs->rme_q_pi[i] = (kva + RME_PI_PTR_Q(q << 5));
		bfa_regs->rme_q_ci[i] = (kva + RME_CI_PTR_Q(q << 5));
		bfa_regs->rme_q_depth[i] = (kva + RME_DEPTH_Q(q << 5));
		bfa_regs->rme_q_ctrl[i] = (kva + RME_QCTRL_Q(q << 5));
	}
}

void
bfa_hwct_reqq_ack(struct bfa_s *bfa, int reqq)
{
	u32 r32;

	r32 = bfa_reg_read(bfa->iocfc.bfa_regs.cpe_q_ctrl[reqq]);
	bfa_reg_write(bfa->iocfc.bfa_regs.cpe_q_ctrl[reqq], r32);
}

void
bfa_hwct_rspq_ack(struct bfa_s *bfa, int rspq)
{
	u32	r32;

	r32 = bfa_reg_read(bfa->iocfc.bfa_regs.rme_q_ctrl[rspq]);
	bfa_reg_write(bfa->iocfc.bfa_regs.rme_q_ctrl[rspq], r32);
}

void
bfa_hwct_msix_getvecs(struct bfa_s *bfa, u32 *msix_vecs_bmap,
		 u32 *num_vecs, u32 *max_vec_bit)
{
	*msix_vecs_bmap = (1 << BFA_MSIX_CT_MAX) - 1;
	*max_vec_bit = (1 << (BFA_MSIX_CT_MAX - 1));
	*num_vecs = BFA_MSIX_CT_MAX;
}

/**
 * Setup MSI-X vector for catapult
 */
void
bfa_hwct_msix_init(struct bfa_s *bfa, int nvecs)
{
	bfa_assert((nvecs == 1) || (nvecs == BFA_MSIX_CT_MAX));
	bfa_trc(bfa, nvecs);

	bfa->msix.nvecs = nvecs;
	bfa_hwct_msix_uninstall(bfa);
}

void
bfa_hwct_msix_install(struct bfa_s *bfa)
{
	int i;

	if (bfa->msix.nvecs == 0)
		return;

	if (bfa->msix.nvecs == 1) {
		for (i = 0; i < BFA_MSIX_CT_MAX; i++)
			bfa->msix.handler[i] = bfa_msix_all;
		return;
	}

	for (i = BFA_MSIX_CPE_Q0; i <= BFA_MSIX_CPE_Q3; i++)
		bfa->msix.handler[i] = bfa_msix_reqq;

	for (; i <= BFA_MSIX_RME_Q3; i++)
		bfa->msix.handler[i] = bfa_msix_rspq;

	bfa_assert(i == BFA_MSIX_LPU_ERR);
	bfa->msix.handler[BFA_MSIX_LPU_ERR] = bfa_msix_lpu_err;
}

void
bfa_hwct_msix_uninstall(struct bfa_s *bfa)
{
	int i;

	for (i = 0; i < BFA_MSIX_CT_MAX; i++)
		bfa->msix.handler[i] = bfa_hwct_msix_dummy;
}

/**
 * Enable MSI-X vectors
 */
void
bfa_hwct_isr_mode_set(struct bfa_s *bfa, bfa_boolean_t msix)
{
	bfa_trc(bfa, 0);
	bfa_hwct_msix_lpu_err_set(bfa, msix, BFA_MSIX_LPU_ERR);
	bfa_ioc_isr_mode_set(&bfa->ioc, msix);
}

void
bfa_hwct_msix_get_rme_range(struct bfa_s *bfa, u32 *start, u32 *end)
{
	*start = BFA_MSIX_RME_Q0;
	*end = BFA_MSIX_RME_Q3;
}
