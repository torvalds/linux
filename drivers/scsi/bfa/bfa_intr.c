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
#include <bfa.h>
#include <bfi/bfi_ctreg.h>
#include <bfa_port_priv.h>
#include <bfa_intr_priv.h>
#include <cs/bfa_debug.h>

BFA_TRC_FILE(HAL, INTR);

static void
bfa_msix_errint(struct bfa_s *bfa, u32 intr)
{
	bfa_ioc_error_isr(&bfa->ioc);
}

static void
bfa_msix_lpu(struct bfa_s *bfa)
{
	bfa_ioc_mbox_isr(&bfa->ioc);
}

static void
bfa_reqq_resume(struct bfa_s *bfa, int qid)
{
	struct list_head *waitq, *qe, *qen;
	struct bfa_reqq_wait_s *wqe;

	waitq = bfa_reqq(bfa, qid);
	list_for_each_safe(qe, qen, waitq) {
		/**
		 * Callback only as long as there is room in request queue
		 */
		if (bfa_reqq_full(bfa, qid))
			break;

		list_del(qe);
		wqe = (struct bfa_reqq_wait_s *) qe;
		wqe->qresume(wqe->cbarg);
	}
}

void
bfa_msix_all(struct bfa_s *bfa, int vec)
{
	bfa_intx(bfa);
}

/**
 *  hal_intr_api
 */
bfa_boolean_t
bfa_intx(struct bfa_s *bfa)
{
	u32        intr, qintr;
	int             queue;

	intr = bfa_reg_read(bfa->iocfc.bfa_regs.intr_status);
	if (!intr)
		return BFA_FALSE;

	/**
	 * RME completion queue interrupt
	 */
	qintr = intr & __HFN_INT_RME_MASK;
	bfa_reg_write(bfa->iocfc.bfa_regs.intr_status, qintr);

	for (queue = 0; queue < BFI_IOC_MAX_CQS_ASIC; queue++) {
		if (intr & (__HFN_INT_RME_Q0 << queue))
			bfa_msix_rspq(bfa, queue & (BFI_IOC_MAX_CQS - 1));
	}
	intr &= ~qintr;
	if (!intr)
		return BFA_TRUE;

	/**
	 * CPE completion queue interrupt
	 */
	qintr = intr & __HFN_INT_CPE_MASK;
	bfa_reg_write(bfa->iocfc.bfa_regs.intr_status, qintr);

	for (queue = 0; queue < BFI_IOC_MAX_CQS_ASIC; queue++) {
		if (intr & (__HFN_INT_CPE_Q0 << queue))
			bfa_msix_reqq(bfa, queue & (BFI_IOC_MAX_CQS - 1));
	}
	intr &= ~qintr;
	if (!intr)
		return BFA_TRUE;

	bfa_msix_lpu_err(bfa, intr);

	return BFA_TRUE;
}

void
bfa_isr_enable(struct bfa_s *bfa)
{
	u32        intr_unmask;
	int             pci_func = bfa_ioc_pcifn(&bfa->ioc);

	bfa_trc(bfa, pci_func);

	bfa_msix_install(bfa);
	intr_unmask = (__HFN_INT_ERR_EMC | __HFN_INT_ERR_LPU0 |
		       __HFN_INT_ERR_LPU1 | __HFN_INT_ERR_PSS |
		       __HFN_INT_LL_HALT);

	if (pci_func == 0)
		intr_unmask |= (__HFN_INT_CPE_Q0 | __HFN_INT_CPE_Q1 |
				__HFN_INT_CPE_Q2 | __HFN_INT_CPE_Q3 |
				__HFN_INT_RME_Q0 | __HFN_INT_RME_Q1 |
				__HFN_INT_RME_Q2 | __HFN_INT_RME_Q3 |
				__HFN_INT_MBOX_LPU0);
	else
		intr_unmask |= (__HFN_INT_CPE_Q4 | __HFN_INT_CPE_Q5 |
				__HFN_INT_CPE_Q6 | __HFN_INT_CPE_Q7 |
				__HFN_INT_RME_Q4 | __HFN_INT_RME_Q5 |
				__HFN_INT_RME_Q6 | __HFN_INT_RME_Q7 |
				__HFN_INT_MBOX_LPU1);

	bfa_reg_write(bfa->iocfc.bfa_regs.intr_status, intr_unmask);
	bfa_reg_write(bfa->iocfc.bfa_regs.intr_mask, ~intr_unmask);
	bfa_isr_mode_set(bfa, bfa->msix.nvecs != 0);
}

void
bfa_isr_disable(struct bfa_s *bfa)
{
	bfa_isr_mode_set(bfa, BFA_FALSE);
	bfa_reg_write(bfa->iocfc.bfa_regs.intr_mask, -1L);
	bfa_msix_uninstall(bfa);
}

void
bfa_msix_reqq(struct bfa_s *bfa, int qid)
{
	struct list_head *waitq;

	qid &= (BFI_IOC_MAX_CQS - 1);

	bfa->iocfc.hwif.hw_reqq_ack(bfa, qid);

	/**
	 * Resume any pending requests in the corresponding reqq.
	 */
	waitq = bfa_reqq(bfa, qid);
	if (!list_empty(waitq))
		bfa_reqq_resume(bfa, qid);
}

void
bfa_isr_unhandled(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	bfa_trc(bfa, m->mhdr.msg_class);
	bfa_trc(bfa, m->mhdr.msg_id);
	bfa_trc(bfa, m->mhdr.mtag.i2htok);
	bfa_assert(0);
	bfa_trc_stop(bfa->trcmod);
}

void
bfa_msix_rspq(struct bfa_s *bfa, int qid)
{
	struct bfi_msg_s *m;
	u32 pi, ci;
	struct list_head *waitq;

	bfa_trc_fp(bfa, qid);

	qid &= (BFI_IOC_MAX_CQS - 1);

	bfa->iocfc.hwif.hw_rspq_ack(bfa, qid);

	ci = bfa_rspq_ci(bfa, qid);
	pi = bfa_rspq_pi(bfa, qid);

	bfa_trc_fp(bfa, ci);
	bfa_trc_fp(bfa, pi);

	if (bfa->rme_process) {
		while (ci != pi) {
			m = bfa_rspq_elem(bfa, qid, ci);
			bfa_assert_fp(m->mhdr.msg_class < BFI_MC_MAX);

			bfa_isrs[m->mhdr.msg_class] (bfa, m);

			CQ_INCR(ci, bfa->iocfc.cfg.drvcfg.num_rspq_elems);
		}
	}

	/**
	 * update CI
	 */
	bfa_rspq_ci(bfa, qid) = pi;
	bfa_reg_write(bfa->iocfc.bfa_regs.rme_q_ci[qid], pi);
	bfa_os_mmiowb();

	/**
	 * Resume any pending requests in the corresponding reqq.
	 */
	waitq = bfa_reqq(bfa, qid);
	if (!list_empty(waitq))
		bfa_reqq_resume(bfa, qid);
}

void
bfa_msix_lpu_err(struct bfa_s *bfa, int vec)
{
	u32 intr, curr_value;

	intr = bfa_reg_read(bfa->iocfc.bfa_regs.intr_status);

	if (intr & (__HFN_INT_MBOX_LPU0 | __HFN_INT_MBOX_LPU1))
		bfa_msix_lpu(bfa);

	intr &= (__HFN_INT_ERR_EMC | __HFN_INT_ERR_LPU0 |
		__HFN_INT_ERR_LPU1 | __HFN_INT_ERR_PSS | __HFN_INT_LL_HALT);

	if (intr) {
		if (intr & __HFN_INT_LL_HALT) {
			/**
			 * If LL_HALT bit is set then FW Init Halt LL Port
			 * Register needs to be cleared as well so Interrupt
			 * Status Register will be cleared.
			 */
			curr_value = bfa_reg_read(bfa->ioc.ioc_regs.ll_halt);
			curr_value &= ~__FW_INIT_HALT_P;
			bfa_reg_write(bfa->ioc.ioc_regs.ll_halt, curr_value);
		}

		if (intr & __HFN_INT_ERR_PSS) {
			/**
			 * ERR_PSS bit needs to be cleared as well in case
			 * interrups are shared so driver's interrupt handler is
			 * still called eventhough it is already masked out.
			 */
			curr_value = bfa_reg_read(
				bfa->ioc.ioc_regs.pss_err_status_reg);
			curr_value &= __PSS_ERR_STATUS_SET;
			bfa_reg_write(bfa->ioc.ioc_regs.pss_err_status_reg,
				curr_value);
		}

		bfa_reg_write(bfa->iocfc.bfa_regs.intr_status, intr);
		bfa_msix_errint(bfa, intr);
	}
}

void
bfa_isr_bind(enum bfi_mclass mc, bfa_isr_func_t isr_func)
{
	bfa_isrs[mc] = isr_func;
}


