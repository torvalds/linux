/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */
#ifndef __OTX2_CPTLF_H
#define __OTX2_CPTLF_H

#include <linux/soc/marvell/octeontx2/asm.h>
#include <linux/bitfield.h>
#include <mbox.h>
#include <rvu.h>
#include "otx2_cpt_common.h"
#include "otx2_cpt_reqmgr.h"

/*
 * CPT instruction and pending queues user requested length in CPT_INST_S msgs
 */
#define OTX2_CPT_USER_REQUESTED_QLEN_MSGS 8200

/*
 * CPT instruction queue size passed to HW is in units of 40*CPT_INST_S
 * messages.
 */
#define OTX2_CPT_SIZE_DIV40 (OTX2_CPT_USER_REQUESTED_QLEN_MSGS/40)

/*
 * CPT instruction and pending queues length in CPT_INST_S messages
 */
#define OTX2_CPT_INST_QLEN_MSGS	((OTX2_CPT_SIZE_DIV40 - 1) * 40)

/*
 * LDWB is getting incorrectly used when IQB_LDWB = 1 and CPT instruction
 * queue has less than 320 free entries. So, increase HW instruction queue
 * size by 320 and give 320 entries less for SW/NIX RX as a workaround.
 */
#define OTX2_CPT_INST_QLEN_EXTRA_BYTES  (320 * OTX2_CPT_INST_SIZE)
#define OTX2_CPT_EXTRA_SIZE_DIV40       (320/40)

/* CPT instruction queue length in bytes */
#define OTX2_CPT_INST_QLEN_BYTES                                               \
		((OTX2_CPT_SIZE_DIV40 * 40 * OTX2_CPT_INST_SIZE) +             \
		OTX2_CPT_INST_QLEN_EXTRA_BYTES)

/* CPT instruction group queue length in bytes */
#define OTX2_CPT_INST_GRP_QLEN_BYTES                                           \
		((OTX2_CPT_SIZE_DIV40 + OTX2_CPT_EXTRA_SIZE_DIV40) * 16)

/* CPT FC length in bytes */
#define OTX2_CPT_Q_FC_LEN 128

/* CPT instruction queue alignment */
#define OTX2_CPT_INST_Q_ALIGNMENT  128

/* Mask which selects all engine groups */
#define OTX2_CPT_ALL_ENG_GRPS_MASK 0xFF

/* Maximum LFs supported in OcteonTX2 for CPT */
#define OTX2_CPT_MAX_LFS_NUM    64

/* Queue priority */
#define OTX2_CPT_QUEUE_HI_PRIO  0x1
#define OTX2_CPT_QUEUE_LOW_PRIO 0x0

enum otx2_cptlf_state {
	OTX2_CPTLF_IN_RESET,
	OTX2_CPTLF_STARTED,
};

struct otx2_cpt_inst_queue {
	u8 *vaddr;
	u8 *real_vaddr;
	dma_addr_t dma_addr;
	dma_addr_t real_dma_addr;
	u32 size;
};

struct otx2_cptlfs_info;
struct otx2_cptlf_wqe {
	struct tasklet_struct work;
	struct otx2_cptlfs_info *lfs;
	u8 lf_num;
};

struct otx2_cptlf_info {
	struct otx2_cptlfs_info *lfs;           /* Ptr to cptlfs_info struct */
	void __iomem *lmtline;                  /* Address of LMTLINE */
	void __iomem *ioreg;                    /* LMTLINE send register */
	int msix_offset;                        /* MSI-X interrupts offset */
	cpumask_var_t affinity_mask;            /* IRQs affinity mask */
	u8 irq_name[OTX2_CPT_LF_MSIX_VECTORS][32];/* Interrupts name */
	u8 is_irq_reg[OTX2_CPT_LF_MSIX_VECTORS];  /* Is interrupt registered */
	u8 slot;                                /* Slot number of this LF */

	struct otx2_cpt_inst_queue iqueue;/* Instruction queue */
	struct otx2_cpt_pending_queue pqueue; /* Pending queue */
	struct otx2_cptlf_wqe *wqe;       /* Tasklet work info */
};

struct cpt_hw_ops {
	void (*send_cmd)(union otx2_cpt_inst_s *cptinst, u32 insts_num,
			 struct otx2_cptlf_info *lf);
	u8 (*cpt_get_compcode)(union otx2_cpt_res_s *result);
	u8 (*cpt_get_uc_compcode)(union otx2_cpt_res_s *result);
	struct otx2_cpt_inst_info *
	(*cpt_sg_info_create)(struct pci_dev *pdev, struct otx2_cpt_req_info *req,
			      gfp_t gfp);
};

#define LMTLINE_SIZE  128
#define LMTLINE_ALIGN 128
struct otx2_lmt_info {
	void *base;
	dma_addr_t iova;
	u32 size;
	u8 align;
};

struct otx2_cptlfs_info {
	/* Registers start address of VF/PF LFs are attached to */
	void __iomem *reg_base;
	struct otx2_lmt_info lmt_info;
	struct pci_dev *pdev;   /* Device LFs are attached to */
	struct otx2_cptlf_info lf[OTX2_CPT_MAX_LFS_NUM];
	struct otx2_mbox *mbox;
	struct cpt_hw_ops *ops;
	u8 are_lfs_attached;	/* Whether CPT LFs are attached */
	u8 lfs_num;		/* Number of CPT LFs */
	u8 kcrypto_se_eng_grp_num; /* Crypto symmetric engine group number */
	u8 kcrypto_ae_eng_grp_num; /* Crypto asymmetric engine group number */
	u8 kvf_limits;          /* Kernel crypto limits */
	atomic_t state;         /* LF's state. started/reset */
	int blkaddr;            /* CPT blkaddr: BLKADDR_CPT0/BLKADDR_CPT1 */
	int global_slot;        /* Global slot across the blocks */
	u8 ctx_ilen;
	u8 ctx_ilen_ovrd;
};

static inline void otx2_cpt_free_instruction_queues(
					struct otx2_cptlfs_info *lfs)
{
	struct otx2_cpt_inst_queue *iq;
	int i;

	for (i = 0; i < lfs->lfs_num; i++) {
		iq = &lfs->lf[i].iqueue;
		if (iq->real_vaddr)
			dma_free_coherent(&lfs->pdev->dev,
					  iq->size,
					  iq->real_vaddr,
					  iq->real_dma_addr);
		iq->real_vaddr = NULL;
		iq->vaddr = NULL;
	}
}

static inline int otx2_cpt_alloc_instruction_queues(
					struct otx2_cptlfs_info *lfs)
{
	struct otx2_cpt_inst_queue *iq;
	int ret = 0, i;

	if (!lfs->lfs_num)
		return -EINVAL;

	for (i = 0; i < lfs->lfs_num; i++) {
		iq = &lfs->lf[i].iqueue;
		iq->size = OTX2_CPT_INST_QLEN_BYTES +
			   OTX2_CPT_Q_FC_LEN +
			   OTX2_CPT_INST_GRP_QLEN_BYTES +
			   OTX2_CPT_INST_Q_ALIGNMENT;
		iq->real_vaddr = dma_alloc_coherent(&lfs->pdev->dev, iq->size,
					&iq->real_dma_addr, GFP_KERNEL);
		if (!iq->real_vaddr) {
			ret = -ENOMEM;
			goto error;
		}
		iq->vaddr = iq->real_vaddr + OTX2_CPT_INST_GRP_QLEN_BYTES;
		iq->dma_addr = iq->real_dma_addr + OTX2_CPT_INST_GRP_QLEN_BYTES;

		/* Align pointers */
		iq->vaddr = PTR_ALIGN(iq->vaddr, OTX2_CPT_INST_Q_ALIGNMENT);
		iq->dma_addr = PTR_ALIGN(iq->dma_addr,
					 OTX2_CPT_INST_Q_ALIGNMENT);
	}
	return 0;

error:
	otx2_cpt_free_instruction_queues(lfs);
	return ret;
}

static inline void otx2_cptlf_set_iqueues_base_addr(
					struct otx2_cptlfs_info *lfs)
{
	union otx2_cptx_lf_q_base lf_q_base;
	int slot;

	for (slot = 0; slot < lfs->lfs_num; slot++) {
		lf_q_base.u = lfs->lf[slot].iqueue.dma_addr;
		otx2_cpt_write64(lfs->reg_base, lfs->blkaddr, slot,
				 OTX2_CPT_LF_Q_BASE, lf_q_base.u);
	}
}

static inline void otx2_cptlf_do_set_iqueue_size(struct otx2_cptlf_info *lf)
{
	union otx2_cptx_lf_q_size lf_q_size = { .u = 0x0 };

	lf_q_size.s.size_div40 = OTX2_CPT_SIZE_DIV40 +
				 OTX2_CPT_EXTRA_SIZE_DIV40;
	otx2_cpt_write64(lf->lfs->reg_base, lf->lfs->blkaddr, lf->slot,
			 OTX2_CPT_LF_Q_SIZE, lf_q_size.u);
}

static inline void otx2_cptlf_set_iqueues_size(struct otx2_cptlfs_info *lfs)
{
	int slot;

	for (slot = 0; slot < lfs->lfs_num; slot++)
		otx2_cptlf_do_set_iqueue_size(&lfs->lf[slot]);
}

#define INFLIGHT   GENMASK_ULL(8, 0)
#define GRB_CNT    GENMASK_ULL(39, 32)
#define GWB_CNT    GENMASK_ULL(47, 40)
#define XQ_XOR     GENMASK_ULL(63, 63)
#define DQPTR      GENMASK_ULL(19, 0)
#define NQPTR      GENMASK_ULL(51, 32)

static inline void otx2_cptlf_do_disable_iqueue(struct otx2_cptlf_info *lf)
{
	void __iomem *reg_base = lf->lfs->reg_base;
	struct pci_dev *pdev = lf->lfs->pdev;
	u8 blkaddr = lf->lfs->blkaddr;
	int timeout = 1000000;
	u64 inprog, inst_ptr;
	u64 slot = lf->slot;
	u64 qsize, pending;
	int i = 0;

	/* Disable instructions enqueuing */
	otx2_cpt_write64(reg_base, blkaddr, slot, OTX2_CPT_LF_CTL, 0x0);

	inprog = otx2_cpt_read64(reg_base, blkaddr, slot, OTX2_CPT_LF_INPROG);
	inprog |= BIT_ULL(16);
	otx2_cpt_write64(reg_base, blkaddr, slot, OTX2_CPT_LF_INPROG, inprog);

	qsize = otx2_cpt_read64(reg_base, blkaddr, slot, OTX2_CPT_LF_Q_SIZE) & 0x7FFF;
	do {
		inst_ptr = otx2_cpt_read64(reg_base, blkaddr, slot, OTX2_CPT_LF_Q_INST_PTR);
		pending = (FIELD_GET(XQ_XOR, inst_ptr) * qsize * 40) +
			  FIELD_GET(NQPTR, inst_ptr) - FIELD_GET(DQPTR, inst_ptr);
		udelay(1);
		timeout--;
	} while ((pending != 0) && (timeout != 0));

	if (timeout == 0)
		dev_warn(&pdev->dev, "TIMEOUT: CPT poll on pending instructions\n");

	timeout = 1000000;
	/* Wait for CPT queue to become execution-quiescent */
	do {
		inprog = otx2_cpt_read64(reg_base, blkaddr, slot, OTX2_CPT_LF_INPROG);

		if ((FIELD_GET(INFLIGHT, inprog) == 0) &&
		    (FIELD_GET(GRB_CNT, inprog) == 0)) {
			i++;
		} else {
			i = 0;
			timeout--;
		}
	} while ((timeout != 0) && (i < 10));

	if (timeout == 0)
		dev_warn(&pdev->dev, "TIMEOUT: CPT poll on inflight count\n");
	/* Wait for 2 us to flush all queue writes to memory */
	udelay(2);
}

static inline void otx2_cptlf_disable_iqueues(struct otx2_cptlfs_info *lfs)
{
	int slot;

	for (slot = 0; slot < lfs->lfs_num; slot++) {
		otx2_cptlf_do_disable_iqueue(&lfs->lf[slot]);
		otx2_cpt_lf_reset_msg(lfs, lfs->global_slot + slot);
	}
}

static inline void otx2_cptlf_set_iqueue_enq(struct otx2_cptlf_info *lf,
					     bool enable)
{
	u8 blkaddr = lf->lfs->blkaddr;
	union otx2_cptx_lf_ctl lf_ctl;

	lf_ctl.u = otx2_cpt_read64(lf->lfs->reg_base, blkaddr, lf->slot,
				   OTX2_CPT_LF_CTL);

	/* Set iqueue's enqueuing */
	lf_ctl.s.ena = enable ? 0x1 : 0x0;
	otx2_cpt_write64(lf->lfs->reg_base, blkaddr, lf->slot,
			 OTX2_CPT_LF_CTL, lf_ctl.u);
}

static inline void otx2_cptlf_enable_iqueue_enq(struct otx2_cptlf_info *lf)
{
	otx2_cptlf_set_iqueue_enq(lf, true);
}

static inline void otx2_cptlf_set_iqueue_exec(struct otx2_cptlf_info *lf,
					      bool enable)
{
	union otx2_cptx_lf_inprog lf_inprog;
	u8 blkaddr = lf->lfs->blkaddr;

	lf_inprog.u = otx2_cpt_read64(lf->lfs->reg_base, blkaddr, lf->slot,
				      OTX2_CPT_LF_INPROG);

	/* Set iqueue's execution */
	lf_inprog.s.eena = enable ? 0x1 : 0x0;
	otx2_cpt_write64(lf->lfs->reg_base, blkaddr, lf->slot,
			 OTX2_CPT_LF_INPROG, lf_inprog.u);
}

static inline void otx2_cptlf_set_ctx_flr_flush(struct otx2_cptlf_info *lf)
{
	u8 blkaddr = lf->lfs->blkaddr;
	u64 val;

	val = otx2_cpt_read64(lf->lfs->reg_base, blkaddr, lf->slot,
			      OTX2_CPT_LF_CTX_CTL);
	val |= BIT_ULL(0);

	otx2_cpt_write64(lf->lfs->reg_base, blkaddr, lf->slot,
			 OTX2_CPT_LF_CTX_CTL, val);
}

static inline void otx2_cptlf_enable_iqueue_exec(struct otx2_cptlf_info *lf)
{
	otx2_cptlf_set_iqueue_exec(lf, true);
}

static inline void otx2_cptlf_disable_iqueue_exec(struct otx2_cptlf_info *lf)
{
	otx2_cptlf_set_iqueue_exec(lf, false);
}

static inline void otx2_cptlf_enable_iqueues(struct otx2_cptlfs_info *lfs)
{
	int slot;

	for (slot = 0; slot < lfs->lfs_num; slot++) {
		/* Enable flush on FLR for Errata */
		if (is_dev_cn10kb(lfs->pdev))
			otx2_cptlf_set_ctx_flr_flush(&lfs->lf[slot]);

		otx2_cptlf_enable_iqueue_exec(&lfs->lf[slot]);
		otx2_cptlf_enable_iqueue_enq(&lfs->lf[slot]);
	}
}

static inline void otx2_cpt_fill_inst(union otx2_cpt_inst_s *cptinst,
				      struct otx2_cpt_iq_command *iq_cmd,
				      u64 comp_baddr)
{
	cptinst->u[0] = 0x0;
	cptinst->s.doneint = true;
	cptinst->s.res_addr = comp_baddr;
	cptinst->u[2] = 0x0;
	cptinst->u[3] = 0x0;
	cptinst->s.ei0 = iq_cmd->cmd.u;
	cptinst->s.ei1 = iq_cmd->dptr;
	cptinst->s.ei2 = iq_cmd->rptr;
	cptinst->s.ei3 = iq_cmd->cptr.u;
}

/*
 * On OcteonTX2 platform the parameter insts_num is used as a count of
 * instructions to be enqueued. The valid values for insts_num are:
 * 1 - 1 CPT instruction will be enqueued during LMTST operation
 * 2 - 2 CPT instructions will be enqueued during LMTST operation
 */
static inline void otx2_cpt_send_cmd(union otx2_cpt_inst_s *cptinst,
				     u32 insts_num, struct otx2_cptlf_info *lf)
{
	void __iomem *lmtline = lf->lmtline;
	long ret;

	/*
	 * Make sure memory areas pointed in CPT_INST_S
	 * are flushed before the instruction is sent to CPT
	 */
	dma_wmb();

	do {
		/* Copy CPT command to LMTLINE */
		memcpy_toio(lmtline, cptinst, insts_num * OTX2_CPT_INST_SIZE);

		/*
		 * LDEOR initiates atomic transfer to I/O device
		 * The following will cause the LMTST to fail (the LDEOR
		 * returns zero):
		 * - No stores have been performed to the LMTLINE since it was
		 * last invalidated.
		 * - The bytes which have been stored to LMTLINE since it was
		 * last invalidated form a pattern that is non-contiguous, does
		 * not start at byte 0, or does not end on a 8-byte boundary.
		 * (i.e.comprises a formation of other than 1–16 8-byte
		 * words.)
		 *
		 * These rules are designed such that an operating system
		 * context switch or hypervisor guest switch need have no
		 * knowledge of the LMTST operations; the switch code does not
		 * need to store to LMTCANCEL. Also note as LMTLINE data cannot
		 * be read, there is no information leakage between processes.
		 */
		ret = otx2_lmt_flush(lf->ioreg);

	} while (!ret);
}

static inline bool otx2_cptlf_started(struct otx2_cptlfs_info *lfs)
{
	return atomic_read(&lfs->state) == OTX2_CPTLF_STARTED;
}

static inline void otx2_cptlf_set_dev_info(struct otx2_cptlfs_info *lfs,
					   struct pci_dev *pdev,
					   void __iomem *reg_base,
					   struct otx2_mbox *mbox,
					   int blkaddr)
{
	lfs->pdev = pdev;
	lfs->reg_base = reg_base;
	lfs->mbox = mbox;
	lfs->blkaddr = blkaddr;
}

int otx2_cptlf_init(struct otx2_cptlfs_info *lfs, u8 eng_grp_msk, int pri,
		    int lfs_num);
void otx2_cptlf_shutdown(struct otx2_cptlfs_info *lfs);
int otx2_cptlf_register_misc_interrupts(struct otx2_cptlfs_info *lfs);
int otx2_cptlf_register_done_interrupts(struct otx2_cptlfs_info *lfs);
void otx2_cptlf_unregister_misc_interrupts(struct otx2_cptlfs_info *lfs);
void otx2_cptlf_unregister_done_interrupts(struct otx2_cptlfs_info *lfs);
void otx2_cptlf_free_irqs_affinity(struct otx2_cptlfs_info *lfs);
int otx2_cptlf_set_irqs_affinity(struct otx2_cptlfs_info *lfs);

#endif /* __OTX2_CPTLF_H */
