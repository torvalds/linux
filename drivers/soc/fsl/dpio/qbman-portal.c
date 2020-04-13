// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright (C) 2014-2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2019 NXP
 *
 */

#include <asm/cacheflush.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <soc/fsl/dpaa2-global.h>

#include "qbman-portal.h"

/* All QBMan command and result structures use this "valid bit" encoding */
#define QB_VALID_BIT ((u32)0x80)

/* QBMan portal management command codes */
#define QBMAN_MC_ACQUIRE       0x30
#define QBMAN_WQCHAN_CONFIGURE 0x46

/* CINH register offsets */
#define QBMAN_CINH_SWP_EQCR_PI      0x800
#define QBMAN_CINH_SWP_EQCR_CI	    0x840
#define QBMAN_CINH_SWP_EQAR    0x8c0
#define QBMAN_CINH_SWP_CR_RT        0x900
#define QBMAN_CINH_SWP_VDQCR_RT     0x940
#define QBMAN_CINH_SWP_EQCR_AM_RT   0x980
#define QBMAN_CINH_SWP_RCR_AM_RT    0x9c0
#define QBMAN_CINH_SWP_DQPI    0xa00
#define QBMAN_CINH_SWP_DCAP    0xac0
#define QBMAN_CINH_SWP_SDQCR   0xb00
#define QBMAN_CINH_SWP_EQCR_AM_RT2  0xb40
#define QBMAN_CINH_SWP_RCR_PI       0xc00
#define QBMAN_CINH_SWP_RAR     0xcc0
#define QBMAN_CINH_SWP_ISR     0xe00
#define QBMAN_CINH_SWP_IER     0xe40
#define QBMAN_CINH_SWP_ISDR    0xe80
#define QBMAN_CINH_SWP_IIR     0xec0

/* CENA register offsets */
#define QBMAN_CENA_SWP_EQCR(n) (0x000 + ((u32)(n) << 6))
#define QBMAN_CENA_SWP_DQRR(n) (0x200 + ((u32)(n) << 6))
#define QBMAN_CENA_SWP_RCR(n)  (0x400 + ((u32)(n) << 6))
#define QBMAN_CENA_SWP_CR      0x600
#define QBMAN_CENA_SWP_RR(vb)  (0x700 + ((u32)(vb) >> 1))
#define QBMAN_CENA_SWP_VDQCR   0x780
#define QBMAN_CENA_SWP_EQCR_CI 0x840
#define QBMAN_CENA_SWP_EQCR_CI_MEMBACK 0x1840

/* CENA register offsets in memory-backed mode */
#define QBMAN_CENA_SWP_DQRR_MEM(n)  (0x800 + ((u32)(n) << 6))
#define QBMAN_CENA_SWP_RCR_MEM(n)   (0x1400 + ((u32)(n) << 6))
#define QBMAN_CENA_SWP_CR_MEM       0x1600
#define QBMAN_CENA_SWP_RR_MEM       0x1680
#define QBMAN_CENA_SWP_VDQCR_MEM    0x1780

/* Reverse mapping of QBMAN_CENA_SWP_DQRR() */
#define QBMAN_IDX_FROM_DQRR(p) (((unsigned long)(p) & 0x1ff) >> 6)

/* Define token used to determine if response written to memory is valid */
#define QMAN_DQ_TOKEN_VALID 1

/* SDQCR attribute codes */
#define QB_SDQCR_FC_SHIFT   29
#define QB_SDQCR_FC_MASK    0x1
#define QB_SDQCR_DCT_SHIFT  24
#define QB_SDQCR_DCT_MASK   0x3
#define QB_SDQCR_TOK_SHIFT  16
#define QB_SDQCR_TOK_MASK   0xff
#define QB_SDQCR_SRC_SHIFT  0
#define QB_SDQCR_SRC_MASK   0xffff

/* opaque token for static dequeues */
#define QMAN_SDQCR_TOKEN    0xbb

#define QBMAN_EQCR_DCA_IDXMASK          0x0f
#define QBMAN_ENQUEUE_FLAG_DCA          (1ULL << 31)

#define EQ_DESC_SIZE_WITHOUT_FD 29
#define EQ_DESC_SIZE_FD_START 32

enum qbman_sdqcr_dct {
	qbman_sdqcr_dct_null = 0,
	qbman_sdqcr_dct_prio_ics,
	qbman_sdqcr_dct_active_ics,
	qbman_sdqcr_dct_active
};

enum qbman_sdqcr_fc {
	qbman_sdqcr_fc_one = 0,
	qbman_sdqcr_fc_up_to_3 = 1
};

/* Internal Function declaration */
static int qbman_swp_enqueue_direct(struct qbman_swp *s,
				    const struct qbman_eq_desc *d,
				    const struct dpaa2_fd *fd);
static int qbman_swp_enqueue_mem_back(struct qbman_swp *s,
				      const struct qbman_eq_desc *d,
				      const struct dpaa2_fd *fd);
static int qbman_swp_enqueue_multiple_direct(struct qbman_swp *s,
					     const struct qbman_eq_desc *d,
					     const struct dpaa2_fd *fd,
					     uint32_t *flags,
					     int num_frames);
static int qbman_swp_enqueue_multiple_mem_back(struct qbman_swp *s,
					       const struct qbman_eq_desc *d,
					       const struct dpaa2_fd *fd,
					       uint32_t *flags,
					       int num_frames);
static int
qbman_swp_enqueue_multiple_desc_direct(struct qbman_swp *s,
				       const struct qbman_eq_desc *d,
				       const struct dpaa2_fd *fd,
				       int num_frames);
static
int qbman_swp_enqueue_multiple_desc_mem_back(struct qbman_swp *s,
					     const struct qbman_eq_desc *d,
					     const struct dpaa2_fd *fd,
					     int num_frames);
static int qbman_swp_pull_direct(struct qbman_swp *s,
				 struct qbman_pull_desc *d);
static int qbman_swp_pull_mem_back(struct qbman_swp *s,
				   struct qbman_pull_desc *d);

const struct dpaa2_dq *qbman_swp_dqrr_next_direct(struct qbman_swp *s);
const struct dpaa2_dq *qbman_swp_dqrr_next_mem_back(struct qbman_swp *s);

static int qbman_swp_release_direct(struct qbman_swp *s,
				    const struct qbman_release_desc *d,
				    const u64 *buffers,
				    unsigned int num_buffers);
static int qbman_swp_release_mem_back(struct qbman_swp *s,
				      const struct qbman_release_desc *d,
				      const u64 *buffers,
				      unsigned int num_buffers);

/* Function pointers */
int (*qbman_swp_enqueue_ptr)(struct qbman_swp *s,
			     const struct qbman_eq_desc *d,
			     const struct dpaa2_fd *fd)
	= qbman_swp_enqueue_direct;

int (*qbman_swp_enqueue_multiple_ptr)(struct qbman_swp *s,
				      const struct qbman_eq_desc *d,
				      const struct dpaa2_fd *fd,
				      uint32_t *flags,
					     int num_frames)
	= qbman_swp_enqueue_multiple_direct;

int
(*qbman_swp_enqueue_multiple_desc_ptr)(struct qbman_swp *s,
				       const struct qbman_eq_desc *d,
				       const struct dpaa2_fd *fd,
				       int num_frames)
	= qbman_swp_enqueue_multiple_desc_direct;

int (*qbman_swp_pull_ptr)(struct qbman_swp *s, struct qbman_pull_desc *d)
			= qbman_swp_pull_direct;

const struct dpaa2_dq *(*qbman_swp_dqrr_next_ptr)(struct qbman_swp *s)
			= qbman_swp_dqrr_next_direct;

int (*qbman_swp_release_ptr)(struct qbman_swp *s,
			     const struct qbman_release_desc *d,
			     const u64 *buffers,
			     unsigned int num_buffers)
			= qbman_swp_release_direct;

/* Portal Access */

static inline u32 qbman_read_register(struct qbman_swp *p, u32 offset)
{
	return readl_relaxed(p->addr_cinh + offset);
}

static inline void qbman_write_register(struct qbman_swp *p, u32 offset,
					u32 value)
{
	writel_relaxed(value, p->addr_cinh + offset);
}

static inline void *qbman_get_cmd(struct qbman_swp *p, u32 offset)
{
	return p->addr_cena + offset;
}

#define QBMAN_CINH_SWP_CFG   0xd00

#define SWP_CFG_DQRR_MF_SHIFT 20
#define SWP_CFG_EST_SHIFT     16
#define SWP_CFG_CPBS_SHIFT    15
#define SWP_CFG_WN_SHIFT      14
#define SWP_CFG_RPM_SHIFT     12
#define SWP_CFG_DCM_SHIFT     10
#define SWP_CFG_EPM_SHIFT     8
#define SWP_CFG_VPM_SHIFT     7
#define SWP_CFG_CPM_SHIFT     6
#define SWP_CFG_SD_SHIFT      5
#define SWP_CFG_SP_SHIFT      4
#define SWP_CFG_SE_SHIFT      3
#define SWP_CFG_DP_SHIFT      2
#define SWP_CFG_DE_SHIFT      1
#define SWP_CFG_EP_SHIFT      0

static inline u32 qbman_set_swp_cfg(u8 max_fill, u8 wn,	u8 est, u8 rpm, u8 dcm,
				    u8 epm, int sd, int sp, int se,
				    int dp, int de, int ep)
{
	return (max_fill << SWP_CFG_DQRR_MF_SHIFT |
		est << SWP_CFG_EST_SHIFT |
		wn << SWP_CFG_WN_SHIFT |
		rpm << SWP_CFG_RPM_SHIFT |
		dcm << SWP_CFG_DCM_SHIFT |
		epm << SWP_CFG_EPM_SHIFT |
		sd << SWP_CFG_SD_SHIFT |
		sp << SWP_CFG_SP_SHIFT |
		se << SWP_CFG_SE_SHIFT |
		dp << SWP_CFG_DP_SHIFT |
		de << SWP_CFG_DE_SHIFT |
		ep << SWP_CFG_EP_SHIFT);
}

#define QMAN_RT_MODE	   0x00000100

static inline u8 qm_cyc_diff(u8 ringsize, u8 first, u8 last)
{
	/* 'first' is included, 'last' is excluded */
	if (first <= last)
		return last - first;
	else
		return (2 * ringsize) - (first - last);
}

/**
 * qbman_swp_init() - Create a functional object representing the given
 *                    QBMan portal descriptor.
 * @d: the given qbman swp descriptor
 *
 * Return qbman_swp portal for success, NULL if the object cannot
 * be created.
 */
struct qbman_swp *qbman_swp_init(const struct qbman_swp_desc *d)
{
	struct qbman_swp *p = kzalloc(sizeof(*p), GFP_KERNEL);
	u32 reg;
	u32 mask_size;
	u32 eqcr_pi;

	if (!p)
		return NULL;

	spin_lock_init(&p->access_spinlock);

	p->desc = d;
	p->mc.valid_bit = QB_VALID_BIT;
	p->sdq = 0;
	p->sdq |= qbman_sdqcr_dct_prio_ics << QB_SDQCR_DCT_SHIFT;
	p->sdq |= qbman_sdqcr_fc_up_to_3 << QB_SDQCR_FC_SHIFT;
	p->sdq |= QMAN_SDQCR_TOKEN << QB_SDQCR_TOK_SHIFT;
	if ((p->desc->qman_version & QMAN_REV_MASK) >= QMAN_REV_5000)
		p->mr.valid_bit = QB_VALID_BIT;

	atomic_set(&p->vdq.available, 1);
	p->vdq.valid_bit = QB_VALID_BIT;
	p->dqrr.next_idx = 0;
	p->dqrr.valid_bit = QB_VALID_BIT;

	if ((p->desc->qman_version & QMAN_REV_MASK) < QMAN_REV_4100) {
		p->dqrr.dqrr_size = 4;
		p->dqrr.reset_bug = 1;
	} else {
		p->dqrr.dqrr_size = 8;
		p->dqrr.reset_bug = 0;
	}

	p->addr_cena = d->cena_bar;
	p->addr_cinh = d->cinh_bar;

	if ((p->desc->qman_version & QMAN_REV_MASK) < QMAN_REV_5000) {

		reg = qbman_set_swp_cfg(p->dqrr.dqrr_size,
			1, /* Writes Non-cacheable */
			0, /* EQCR_CI stashing threshold */
			3, /* RPM: RCR in array mode */
			2, /* DCM: Discrete consumption ack */
			2, /* EPM: EQCR in ring mode */
			1, /* mem stashing drop enable enable */
			1, /* mem stashing priority enable */
			1, /* mem stashing enable */
			1, /* dequeue stashing priority enable */
			0, /* dequeue stashing enable enable */
			0); /* EQCR_CI stashing priority enable */
	} else {
		memset(p->addr_cena, 0, 64 * 1024);
		reg = qbman_set_swp_cfg(p->dqrr.dqrr_size,
			1, /* Writes Non-cacheable */
			1, /* EQCR_CI stashing threshold */
			3, /* RPM: RCR in array mode */
			2, /* DCM: Discrete consumption ack */
			0, /* EPM: EQCR in ring mode */
			1, /* mem stashing drop enable */
			1, /* mem stashing priority enable */
			1, /* mem stashing enable */
			1, /* dequeue stashing priority enable */
			0, /* dequeue stashing enable */
			0); /* EQCR_CI stashing priority enable */
		reg |= 1 << SWP_CFG_CPBS_SHIFT | /* memory-backed mode */
		       1 << SWP_CFG_VPM_SHIFT |  /* VDQCR read triggered mode */
		       1 << SWP_CFG_CPM_SHIFT;   /* CR read triggered mode */
	}

	qbman_write_register(p, QBMAN_CINH_SWP_CFG, reg);
	reg = qbman_read_register(p, QBMAN_CINH_SWP_CFG);
	if (!reg) {
		pr_err("qbman: the portal is not enabled!\n");
		kfree(p);
		return NULL;
	}

	if ((p->desc->qman_version & QMAN_REV_MASK) >= QMAN_REV_5000) {
		qbman_write_register(p, QBMAN_CINH_SWP_EQCR_PI, QMAN_RT_MODE);
		qbman_write_register(p, QBMAN_CINH_SWP_RCR_PI, QMAN_RT_MODE);
	}
	/*
	 * SDQCR needs to be initialized to 0 when no channels are
	 * being dequeued from or else the QMan HW will indicate an
	 * error.  The values that were calculated above will be
	 * applied when dequeues from a specific channel are enabled.
	 */
	qbman_write_register(p, QBMAN_CINH_SWP_SDQCR, 0);

	p->eqcr.pi_ring_size = 8;
	if ((p->desc->qman_version & QMAN_REV_MASK) >= QMAN_REV_5000) {
		p->eqcr.pi_ring_size = 32;
		qbman_swp_enqueue_ptr =
			qbman_swp_enqueue_mem_back;
		qbman_swp_enqueue_multiple_ptr =
			qbman_swp_enqueue_multiple_mem_back;
		qbman_swp_enqueue_multiple_desc_ptr =
			qbman_swp_enqueue_multiple_desc_mem_back;
		qbman_swp_pull_ptr = qbman_swp_pull_mem_back;
		qbman_swp_dqrr_next_ptr = qbman_swp_dqrr_next_mem_back;
		qbman_swp_release_ptr = qbman_swp_release_mem_back;
	}

	for (mask_size = p->eqcr.pi_ring_size; mask_size > 0; mask_size >>= 1)
		p->eqcr.pi_ci_mask = (p->eqcr.pi_ci_mask << 1) + 1;
	eqcr_pi = qbman_read_register(p, QBMAN_CINH_SWP_EQCR_PI);
	p->eqcr.pi = eqcr_pi & p->eqcr.pi_ci_mask;
	p->eqcr.pi_vb = eqcr_pi & QB_VALID_BIT;
	p->eqcr.ci = qbman_read_register(p, QBMAN_CINH_SWP_EQCR_CI)
			& p->eqcr.pi_ci_mask;
	p->eqcr.available = p->eqcr.pi_ring_size;

	return p;
}

/**
 * qbman_swp_finish() - Create and destroy a functional object representing
 *                      the given QBMan portal descriptor.
 * @p: the qbman_swp object to be destroyed
 */
void qbman_swp_finish(struct qbman_swp *p)
{
	kfree(p);
}

/**
 * qbman_swp_interrupt_read_status()
 * @p: the given software portal
 *
 * Return the value in the SWP_ISR register.
 */
u32 qbman_swp_interrupt_read_status(struct qbman_swp *p)
{
	return qbman_read_register(p, QBMAN_CINH_SWP_ISR);
}

/**
 * qbman_swp_interrupt_clear_status()
 * @p: the given software portal
 * @mask: The mask to clear in SWP_ISR register
 */
void qbman_swp_interrupt_clear_status(struct qbman_swp *p, u32 mask)
{
	qbman_write_register(p, QBMAN_CINH_SWP_ISR, mask);
}

/**
 * qbman_swp_interrupt_get_trigger() - read interrupt enable register
 * @p: the given software portal
 *
 * Return the value in the SWP_IER register.
 */
u32 qbman_swp_interrupt_get_trigger(struct qbman_swp *p)
{
	return qbman_read_register(p, QBMAN_CINH_SWP_IER);
}

/**
 * qbman_swp_interrupt_set_trigger() - enable interrupts for a swp
 * @p: the given software portal
 * @mask: The mask of bits to enable in SWP_IER
 */
void qbman_swp_interrupt_set_trigger(struct qbman_swp *p, u32 mask)
{
	qbman_write_register(p, QBMAN_CINH_SWP_IER, mask);
}

/**
 * qbman_swp_interrupt_get_inhibit() - read interrupt mask register
 * @p: the given software portal object
 *
 * Return the value in the SWP_IIR register.
 */
int qbman_swp_interrupt_get_inhibit(struct qbman_swp *p)
{
	return qbman_read_register(p, QBMAN_CINH_SWP_IIR);
}

/**
 * qbman_swp_interrupt_set_inhibit() - write interrupt mask register
 * @p: the given software portal object
 * @mask: The mask to set in SWP_IIR register
 */
void qbman_swp_interrupt_set_inhibit(struct qbman_swp *p, int inhibit)
{
	qbman_write_register(p, QBMAN_CINH_SWP_IIR, inhibit ? 0xffffffff : 0);
}

/*
 * Different management commands all use this common base layer of code to issue
 * commands and poll for results.
 */

/*
 * Returns a pointer to where the caller should fill in their management command
 * (caller should ignore the verb byte)
 */
void *qbman_swp_mc_start(struct qbman_swp *p)
{
	if ((p->desc->qman_version & QMAN_REV_MASK) < QMAN_REV_5000)
		return qbman_get_cmd(p, QBMAN_CENA_SWP_CR);
	else
		return qbman_get_cmd(p, QBMAN_CENA_SWP_CR_MEM);
}

/*
 * Commits merges in the caller-supplied command verb (which should not include
 * the valid-bit) and submits the command to hardware
 */
void qbman_swp_mc_submit(struct qbman_swp *p, void *cmd, u8 cmd_verb)
{
	u8 *v = cmd;

	if ((p->desc->qman_version & QMAN_REV_MASK) < QMAN_REV_5000) {
		dma_wmb();
		*v = cmd_verb | p->mc.valid_bit;
	} else {
		*v = cmd_verb | p->mc.valid_bit;
		dma_wmb();
		qbman_write_register(p, QBMAN_CINH_SWP_CR_RT, QMAN_RT_MODE);
	}
}

/*
 * Checks for a completed response (returns non-NULL if only if the response
 * is complete).
 */
void *qbman_swp_mc_result(struct qbman_swp *p)
{
	u32 *ret, verb;

	if ((p->desc->qman_version & QMAN_REV_MASK) < QMAN_REV_5000) {
		ret = qbman_get_cmd(p, QBMAN_CENA_SWP_RR(p->mc.valid_bit));
		/* Remove the valid-bit - command completed if the rest
		 * is non-zero.
		 */
		verb = ret[0] & ~QB_VALID_BIT;
		if (!verb)
			return NULL;
		p->mc.valid_bit ^= QB_VALID_BIT;
	} else {
		ret = qbman_get_cmd(p, QBMAN_CENA_SWP_RR_MEM);
		/* Command completed if the valid bit is toggled */
		if (p->mr.valid_bit != (ret[0] & QB_VALID_BIT))
			return NULL;
		/* Command completed if the rest is non-zero */
		verb = ret[0] & ~QB_VALID_BIT;
		if (!verb)
			return NULL;
		p->mr.valid_bit ^= QB_VALID_BIT;
	}

	return ret;
}

#define QB_ENQUEUE_CMD_OPTIONS_SHIFT    0
enum qb_enqueue_commands {
	enqueue_empty = 0,
	enqueue_response_always = 1,
	enqueue_rejects_to_fq = 2
};

#define QB_ENQUEUE_CMD_ORP_ENABLE_SHIFT      2
#define QB_ENQUEUE_CMD_IRQ_ON_DISPATCH_SHIFT 3
#define QB_ENQUEUE_CMD_TARGET_TYPE_SHIFT     4
#define QB_ENQUEUE_CMD_DCA_EN_SHIFT          7

/**
 * qbman_eq_desc_clear() - Clear the contents of a descriptor to
 *                         default/starting state.
 */
void qbman_eq_desc_clear(struct qbman_eq_desc *d)
{
	memset(d, 0, sizeof(*d));
}

/**
 * qbman_eq_desc_set_no_orp() - Set enqueue descriptor without orp
 * @d:                the enqueue descriptor.
 * @response_success: 1 = enqueue with response always; 0 = enqueue with
 *                    rejections returned on a FQ.
 */
void qbman_eq_desc_set_no_orp(struct qbman_eq_desc *d, int respond_success)
{
	d->verb &= ~(1 << QB_ENQUEUE_CMD_ORP_ENABLE_SHIFT);
	if (respond_success)
		d->verb |= enqueue_response_always;
	else
		d->verb |= enqueue_rejects_to_fq;
}

/*
 * Exactly one of the following descriptor "targets" should be set. (Calling any
 * one of these will replace the effect of any prior call to one of these.)
 *   -enqueue to a frame queue
 *   -enqueue to a queuing destination
 */

/**
 * qbman_eq_desc_set_fq() - set the FQ for the enqueue command
 * @d:    the enqueue descriptor
 * @fqid: the id of the frame queue to be enqueued
 */
void qbman_eq_desc_set_fq(struct qbman_eq_desc *d, u32 fqid)
{
	d->verb &= ~(1 << QB_ENQUEUE_CMD_TARGET_TYPE_SHIFT);
	d->tgtid = cpu_to_le32(fqid);
}

/**
 * qbman_eq_desc_set_qd() - Set Queuing Destination for the enqueue command
 * @d:       the enqueue descriptor
 * @qdid:    the id of the queuing destination to be enqueued
 * @qd_bin:  the queuing destination bin
 * @qd_prio: the queuing destination priority
 */
void qbman_eq_desc_set_qd(struct qbman_eq_desc *d, u32 qdid,
			  u32 qd_bin, u32 qd_prio)
{
	d->verb |= 1 << QB_ENQUEUE_CMD_TARGET_TYPE_SHIFT;
	d->tgtid = cpu_to_le32(qdid);
	d->qdbin = cpu_to_le16(qd_bin);
	d->qpri = qd_prio;
}

#define EQAR_IDX(eqar)     ((eqar) & 0x7)
#define EQAR_VB(eqar)      ((eqar) & 0x80)
#define EQAR_SUCCESS(eqar) ((eqar) & 0x100)

static inline void qbman_write_eqcr_am_rt_register(struct qbman_swp *p,
						   u8 idx)
{
	if (idx < 16)
		qbman_write_register(p, QBMAN_CINH_SWP_EQCR_AM_RT + idx * 4,
				     QMAN_RT_MODE);
	else
		qbman_write_register(p, QBMAN_CINH_SWP_EQCR_AM_RT2 +
				     (idx - 16) * 4,
				     QMAN_RT_MODE);
}

#define QB_RT_BIT ((u32)0x100)
/**
 * qbman_swp_enqueue_direct() - Issue an enqueue command
 * @s:  the software portal used for enqueue
 * @d:  the enqueue descriptor
 * @fd: the frame descriptor to be enqueued
 *
 * Please note that 'fd' should only be NULL if the "action" of the
 * descriptor is "orp_hole" or "orp_nesn".
 *
 * Return 0 for successful enqueue, -EBUSY if the EQCR is not ready.
 */
static
int qbman_swp_enqueue_direct(struct qbman_swp *s,
			     const struct qbman_eq_desc *d,
			     const struct dpaa2_fd *fd)
{
	int flags = 0;
	int ret = qbman_swp_enqueue_multiple_direct(s, d, fd, &flags, 1);

	if (ret >= 0)
		ret = 0;
	else
		ret = -EBUSY;
	return  ret;
}

/**
 * qbman_swp_enqueue_mem_back() - Issue an enqueue command
 * @s:  the software portal used for enqueue
 * @d:  the enqueue descriptor
 * @fd: the frame descriptor to be enqueued
 *
 * Please note that 'fd' should only be NULL if the "action" of the
 * descriptor is "orp_hole" or "orp_nesn".
 *
 * Return 0 for successful enqueue, -EBUSY if the EQCR is not ready.
 */
static
int qbman_swp_enqueue_mem_back(struct qbman_swp *s,
			       const struct qbman_eq_desc *d,
			       const struct dpaa2_fd *fd)
{
	int flags = 0;
	int ret = qbman_swp_enqueue_multiple_mem_back(s, d, fd, &flags, 1);

	if (ret >= 0)
		ret = 0;
	else
		ret = -EBUSY;
	return  ret;
}

/**
 * qbman_swp_enqueue_multiple_direct() - Issue a multi enqueue command
 * using one enqueue descriptor
 * @s:  the software portal used for enqueue
 * @d:  the enqueue descriptor
 * @fd: table pointer of frame descriptor table to be enqueued
 * @flags: table pointer of QBMAN_ENQUEUE_FLAG_DCA flags, not used if NULL
 * @num_frames: number of fd to be enqueued
 *
 * Return the number of fd enqueued, or a negative error number.
 */
static
int qbman_swp_enqueue_multiple_direct(struct qbman_swp *s,
				      const struct qbman_eq_desc *d,
				      const struct dpaa2_fd *fd,
				      uint32_t *flags,
				      int num_frames)
{
	uint32_t *p = NULL;
	const uint32_t *cl = (uint32_t *)d;
	uint32_t eqcr_ci, eqcr_pi, half_mask, full_mask;
	int i, num_enqueued = 0;
	uint64_t addr_cena;

	spin_lock(&s->access_spinlock);
	half_mask = (s->eqcr.pi_ci_mask>>1);
	full_mask = s->eqcr.pi_ci_mask;

	if (!s->eqcr.available) {
		eqcr_ci = s->eqcr.ci;
		p = s->addr_cena + QBMAN_CENA_SWP_EQCR_CI;
		s->eqcr.ci = qbman_read_register(s, QBMAN_CINH_SWP_EQCR_CI);

		s->eqcr.available = qm_cyc_diff(s->eqcr.pi_ring_size,
					eqcr_ci, s->eqcr.ci);
		if (!s->eqcr.available) {
			spin_unlock(&s->access_spinlock);
			return 0;
		}
	}

	eqcr_pi = s->eqcr.pi;
	num_enqueued = (s->eqcr.available < num_frames) ?
			s->eqcr.available : num_frames;
	s->eqcr.available -= num_enqueued;
	/* Fill in the EQCR ring */
	for (i = 0; i < num_enqueued; i++) {
		p = (s->addr_cena + QBMAN_CENA_SWP_EQCR(eqcr_pi & half_mask));
		/* Skip copying the verb */
		memcpy(&p[1], &cl[1], EQ_DESC_SIZE_WITHOUT_FD - 1);
		memcpy(&p[EQ_DESC_SIZE_FD_START/sizeof(uint32_t)],
		       &fd[i], sizeof(*fd));
		eqcr_pi++;
	}

	dma_wmb();

	/* Set the verb byte, have to substitute in the valid-bit */
	eqcr_pi = s->eqcr.pi;
	for (i = 0; i < num_enqueued; i++) {
		p = (s->addr_cena + QBMAN_CENA_SWP_EQCR(eqcr_pi & half_mask));
		p[0] = cl[0] | s->eqcr.pi_vb;
		if (flags && (flags[i] & QBMAN_ENQUEUE_FLAG_DCA)) {
			struct qbman_eq_desc *d = (struct qbman_eq_desc *)p;

			d->dca = (1 << QB_ENQUEUE_CMD_DCA_EN_SHIFT) |
				((flags[i]) & QBMAN_EQCR_DCA_IDXMASK);
		}
		eqcr_pi++;
		if (!(eqcr_pi & half_mask))
			s->eqcr.pi_vb ^= QB_VALID_BIT;
	}

	/* Flush all the cacheline without load/store in between */
	eqcr_pi = s->eqcr.pi;
	addr_cena = (size_t)s->addr_cena;
	for (i = 0; i < num_enqueued; i++)
		eqcr_pi++;
	s->eqcr.pi = eqcr_pi & full_mask;
	spin_unlock(&s->access_spinlock);

	return num_enqueued;
}

/**
 * qbman_swp_enqueue_multiple_mem_back() - Issue a multi enqueue command
 * using one enqueue descriptor
 * @s:  the software portal used for enqueue
 * @d:  the enqueue descriptor
 * @fd: table pointer of frame descriptor table to be enqueued
 * @flags: table pointer of QBMAN_ENQUEUE_FLAG_DCA flags, not used if NULL
 * @num_frames: number of fd to be enqueued
 *
 * Return the number of fd enqueued, or a negative error number.
 */
static
int qbman_swp_enqueue_multiple_mem_back(struct qbman_swp *s,
					const struct qbman_eq_desc *d,
					const struct dpaa2_fd *fd,
					uint32_t *flags,
					int num_frames)
{
	uint32_t *p = NULL;
	const uint32_t *cl = (uint32_t *)(d);
	uint32_t eqcr_ci, eqcr_pi, half_mask, full_mask;
	int i, num_enqueued = 0;
	unsigned long irq_flags;

	spin_lock(&s->access_spinlock);
	local_irq_save(irq_flags);

	half_mask = (s->eqcr.pi_ci_mask>>1);
	full_mask = s->eqcr.pi_ci_mask;
	if (!s->eqcr.available) {
		eqcr_ci = s->eqcr.ci;
		p = s->addr_cena + QBMAN_CENA_SWP_EQCR_CI_MEMBACK;
		s->eqcr.ci = __raw_readl(p) & full_mask;
		s->eqcr.available = qm_cyc_diff(s->eqcr.pi_ring_size,
					eqcr_ci, s->eqcr.ci);
		if (!s->eqcr.available) {
			local_irq_restore(irq_flags);
			spin_unlock(&s->access_spinlock);
			return 0;
		}
	}

	eqcr_pi = s->eqcr.pi;
	num_enqueued = (s->eqcr.available < num_frames) ?
			s->eqcr.available : num_frames;
	s->eqcr.available -= num_enqueued;
	/* Fill in the EQCR ring */
	for (i = 0; i < num_enqueued; i++) {
		p = (s->addr_cena + QBMAN_CENA_SWP_EQCR(eqcr_pi & half_mask));
		/* Skip copying the verb */
		memcpy(&p[1], &cl[1], EQ_DESC_SIZE_WITHOUT_FD - 1);
		memcpy(&p[EQ_DESC_SIZE_FD_START/sizeof(uint32_t)],
		       &fd[i], sizeof(*fd));
		eqcr_pi++;
	}

	/* Set the verb byte, have to substitute in the valid-bit */
	eqcr_pi = s->eqcr.pi;
	for (i = 0; i < num_enqueued; i++) {
		p = (s->addr_cena + QBMAN_CENA_SWP_EQCR(eqcr_pi & half_mask));
		p[0] = cl[0] | s->eqcr.pi_vb;
		if (flags && (flags[i] & QBMAN_ENQUEUE_FLAG_DCA)) {
			struct qbman_eq_desc *d = (struct qbman_eq_desc *)p;

			d->dca = (1 << QB_ENQUEUE_CMD_DCA_EN_SHIFT) |
				((flags[i]) & QBMAN_EQCR_DCA_IDXMASK);
		}
		eqcr_pi++;
		if (!(eqcr_pi & half_mask))
			s->eqcr.pi_vb ^= QB_VALID_BIT;
	}
	s->eqcr.pi = eqcr_pi & full_mask;

	dma_wmb();
	qbman_write_register(s, QBMAN_CINH_SWP_EQCR_PI,
				(QB_RT_BIT)|(s->eqcr.pi)|s->eqcr.pi_vb);
	local_irq_restore(irq_flags);
	spin_unlock(&s->access_spinlock);

	return num_enqueued;
}

/**
 * qbman_swp_enqueue_multiple_desc_direct() - Issue a multi enqueue command
 * using multiple enqueue descriptor
 * @s:  the software portal used for enqueue
 * @d:  table of minimal enqueue descriptor
 * @fd: table pointer of frame descriptor table to be enqueued
 * @num_frames: number of fd to be enqueued
 *
 * Return the number of fd enqueued, or a negative error number.
 */
static
int qbman_swp_enqueue_multiple_desc_direct(struct qbman_swp *s,
					   const struct qbman_eq_desc *d,
					   const struct dpaa2_fd *fd,
					   int num_frames)
{
	uint32_t *p;
	const uint32_t *cl;
	uint32_t eqcr_ci, eqcr_pi, half_mask, full_mask;
	int i, num_enqueued = 0;
	uint64_t addr_cena;

	half_mask = (s->eqcr.pi_ci_mask>>1);
	full_mask = s->eqcr.pi_ci_mask;
	if (!s->eqcr.available) {
		eqcr_ci = s->eqcr.ci;
		p = s->addr_cena + QBMAN_CENA_SWP_EQCR_CI;
		s->eqcr.ci = qbman_read_register(s, QBMAN_CINH_SWP_EQCR_CI);
		s->eqcr.available = qm_cyc_diff(s->eqcr.pi_ring_size,
					eqcr_ci, s->eqcr.ci);
		if (!s->eqcr.available)
			return 0;
	}

	eqcr_pi = s->eqcr.pi;
	num_enqueued = (s->eqcr.available < num_frames) ?
			s->eqcr.available : num_frames;
	s->eqcr.available -= num_enqueued;
	/* Fill in the EQCR ring */
	for (i = 0; i < num_enqueued; i++) {
		p = (s->addr_cena + QBMAN_CENA_SWP_EQCR(eqcr_pi & half_mask));
		cl = (uint32_t *)(&d[i]);
		/* Skip copying the verb */
		memcpy(&p[1], &cl[1], EQ_DESC_SIZE_WITHOUT_FD - 1);
		memcpy(&p[EQ_DESC_SIZE_FD_START/sizeof(uint32_t)],
		       &fd[i], sizeof(*fd));
		eqcr_pi++;
	}

	dma_wmb();

	/* Set the verb byte, have to substitute in the valid-bit */
	eqcr_pi = s->eqcr.pi;
	for (i = 0; i < num_enqueued; i++) {
		p = (s->addr_cena + QBMAN_CENA_SWP_EQCR(eqcr_pi & half_mask));
		cl = (uint32_t *)(&d[i]);
		p[0] = cl[0] | s->eqcr.pi_vb;
		eqcr_pi++;
		if (!(eqcr_pi & half_mask))
			s->eqcr.pi_vb ^= QB_VALID_BIT;
	}

	/* Flush all the cacheline without load/store in between */
	eqcr_pi = s->eqcr.pi;
	addr_cena = (uint64_t)s->addr_cena;
	for (i = 0; i < num_enqueued; i++)
		eqcr_pi++;
	s->eqcr.pi = eqcr_pi & full_mask;

	return num_enqueued;
}

/**
 * qbman_swp_enqueue_multiple_desc_mem_back() - Issue a multi enqueue command
 * using multiple enqueue descriptor
 * @s:  the software portal used for enqueue
 * @d:  table of minimal enqueue descriptor
 * @fd: table pointer of frame descriptor table to be enqueued
 * @num_frames: number of fd to be enqueued
 *
 * Return the number of fd enqueued, or a negative error number.
 */
static
int qbman_swp_enqueue_multiple_desc_mem_back(struct qbman_swp *s,
					   const struct qbman_eq_desc *d,
					   const struct dpaa2_fd *fd,
					   int num_frames)
{
	uint32_t *p;
	const uint32_t *cl;
	uint32_t eqcr_ci, eqcr_pi, half_mask, full_mask;
	int i, num_enqueued = 0;

	half_mask = (s->eqcr.pi_ci_mask>>1);
	full_mask = s->eqcr.pi_ci_mask;
	if (!s->eqcr.available) {
		eqcr_ci = s->eqcr.ci;
		p = s->addr_cena + QBMAN_CENA_SWP_EQCR_CI_MEMBACK;
		s->eqcr.ci = __raw_readl(p) & full_mask;
		s->eqcr.available = qm_cyc_diff(s->eqcr.pi_ring_size,
					eqcr_ci, s->eqcr.ci);
		if (!s->eqcr.available)
			return 0;
	}

	eqcr_pi = s->eqcr.pi;
	num_enqueued = (s->eqcr.available < num_frames) ?
			s->eqcr.available : num_frames;
	s->eqcr.available -= num_enqueued;
	/* Fill in the EQCR ring */
	for (i = 0; i < num_enqueued; i++) {
		p = (s->addr_cena + QBMAN_CENA_SWP_EQCR(eqcr_pi & half_mask));
		cl = (uint32_t *)(&d[i]);
		/* Skip copying the verb */
		memcpy(&p[1], &cl[1], EQ_DESC_SIZE_WITHOUT_FD - 1);
		memcpy(&p[EQ_DESC_SIZE_FD_START/sizeof(uint32_t)],
		       &fd[i], sizeof(*fd));
		eqcr_pi++;
	}

	/* Set the verb byte, have to substitute in the valid-bit */
	eqcr_pi = s->eqcr.pi;
	for (i = 0; i < num_enqueued; i++) {
		p = (s->addr_cena + QBMAN_CENA_SWP_EQCR(eqcr_pi & half_mask));
		cl = (uint32_t *)(&d[i]);
		p[0] = cl[0] | s->eqcr.pi_vb;
		eqcr_pi++;
		if (!(eqcr_pi & half_mask))
			s->eqcr.pi_vb ^= QB_VALID_BIT;
	}

	s->eqcr.pi = eqcr_pi & full_mask;

	dma_wmb();
	qbman_write_register(s, QBMAN_CINH_SWP_EQCR_PI,
				(QB_RT_BIT)|(s->eqcr.pi)|s->eqcr.pi_vb);

	return num_enqueued;
}

/* Static (push) dequeue */

/**
 * qbman_swp_push_get() - Get the push dequeue setup
 * @p:           the software portal object
 * @channel_idx: the channel index to query
 * @enabled:     returned boolean to show whether the push dequeue is enabled
 *               for the given channel
 */
void qbman_swp_push_get(struct qbman_swp *s, u8 channel_idx, int *enabled)
{
	u16 src = (s->sdq >> QB_SDQCR_SRC_SHIFT) & QB_SDQCR_SRC_MASK;

	WARN_ON(channel_idx > 15);
	*enabled = src | (1 << channel_idx);
}

/**
 * qbman_swp_push_set() - Enable or disable push dequeue
 * @p:           the software portal object
 * @channel_idx: the channel index (0 to 15)
 * @enable:      enable or disable push dequeue
 */
void qbman_swp_push_set(struct qbman_swp *s, u8 channel_idx, int enable)
{
	u16 dqsrc;

	WARN_ON(channel_idx > 15);
	if (enable)
		s->sdq |= 1 << channel_idx;
	else
		s->sdq &= ~(1 << channel_idx);

	/* Read make the complete src map.  If no channels are enabled
	 * the SDQCR must be 0 or else QMan will assert errors
	 */
	dqsrc = (s->sdq >> QB_SDQCR_SRC_SHIFT) & QB_SDQCR_SRC_MASK;
	if (dqsrc != 0)
		qbman_write_register(s, QBMAN_CINH_SWP_SDQCR, s->sdq);
	else
		qbman_write_register(s, QBMAN_CINH_SWP_SDQCR, 0);
}

#define QB_VDQCR_VERB_DCT_SHIFT    0
#define QB_VDQCR_VERB_DT_SHIFT     2
#define QB_VDQCR_VERB_RLS_SHIFT    4
#define QB_VDQCR_VERB_WAE_SHIFT    5

enum qb_pull_dt_e {
	qb_pull_dt_channel,
	qb_pull_dt_workqueue,
	qb_pull_dt_framequeue
};

/**
 * qbman_pull_desc_clear() - Clear the contents of a descriptor to
 *                           default/starting state
 * @d: the pull dequeue descriptor to be cleared
 */
void qbman_pull_desc_clear(struct qbman_pull_desc *d)
{
	memset(d, 0, sizeof(*d));
}

/**
 * qbman_pull_desc_set_storage()- Set the pull dequeue storage
 * @d:            the pull dequeue descriptor to be set
 * @storage:      the pointer of the memory to store the dequeue result
 * @storage_phys: the physical address of the storage memory
 * @stash:        to indicate whether write allocate is enabled
 *
 * If not called, or if called with 'storage' as NULL, the result pull dequeues
 * will produce results to DQRR. If 'storage' is non-NULL, then results are
 * produced to the given memory location (using the DMA address which
 * the caller provides in 'storage_phys'), and 'stash' controls whether or not
 * those writes to main-memory express a cache-warming attribute.
 */
void qbman_pull_desc_set_storage(struct qbman_pull_desc *d,
				 struct dpaa2_dq *storage,
				 dma_addr_t storage_phys,
				 int stash)
{
	/* save the virtual address */
	d->rsp_addr_virt = (u64)(uintptr_t)storage;

	if (!storage) {
		d->verb &= ~(1 << QB_VDQCR_VERB_RLS_SHIFT);
		return;
	}
	d->verb |= 1 << QB_VDQCR_VERB_RLS_SHIFT;
	if (stash)
		d->verb |= 1 << QB_VDQCR_VERB_WAE_SHIFT;
	else
		d->verb &= ~(1 << QB_VDQCR_VERB_WAE_SHIFT);

	d->rsp_addr = cpu_to_le64(storage_phys);
}

/**
 * qbman_pull_desc_set_numframes() - Set the number of frames to be dequeued
 * @d:         the pull dequeue descriptor to be set
 * @numframes: number of frames to be set, must be between 1 and 16, inclusive
 */
void qbman_pull_desc_set_numframes(struct qbman_pull_desc *d, u8 numframes)
{
	d->numf = numframes - 1;
}

/*
 * Exactly one of the following descriptor "actions" should be set. (Calling any
 * one of these will replace the effect of any prior call to one of these.)
 * - pull dequeue from the given frame queue (FQ)
 * - pull dequeue from any FQ in the given work queue (WQ)
 * - pull dequeue from any FQ in any WQ in the given channel
 */

/**
 * qbman_pull_desc_set_fq() - Set fqid from which the dequeue command dequeues
 * @fqid: the frame queue index of the given FQ
 */
void qbman_pull_desc_set_fq(struct qbman_pull_desc *d, u32 fqid)
{
	d->verb |= 1 << QB_VDQCR_VERB_DCT_SHIFT;
	d->verb |= qb_pull_dt_framequeue << QB_VDQCR_VERB_DT_SHIFT;
	d->dq_src = cpu_to_le32(fqid);
}

/**
 * qbman_pull_desc_set_wq() - Set wqid from which the dequeue command dequeues
 * @wqid: composed of channel id and wqid within the channel
 * @dct:  the dequeue command type
 */
void qbman_pull_desc_set_wq(struct qbman_pull_desc *d, u32 wqid,
			    enum qbman_pull_type_e dct)
{
	d->verb |= dct << QB_VDQCR_VERB_DCT_SHIFT;
	d->verb |= qb_pull_dt_workqueue << QB_VDQCR_VERB_DT_SHIFT;
	d->dq_src = cpu_to_le32(wqid);
}

/**
 * qbman_pull_desc_set_channel() - Set channelid from which the dequeue command
 *                                 dequeues
 * @chid: the channel id to be dequeued
 * @dct:  the dequeue command type
 */
void qbman_pull_desc_set_channel(struct qbman_pull_desc *d, u32 chid,
				 enum qbman_pull_type_e dct)
{
	d->verb |= dct << QB_VDQCR_VERB_DCT_SHIFT;
	d->verb |= qb_pull_dt_channel << QB_VDQCR_VERB_DT_SHIFT;
	d->dq_src = cpu_to_le32(chid);
}

/**
 * qbman_swp_pull_direct() - Issue the pull dequeue command
 * @s: the software portal object
 * @d: the software portal descriptor which has been configured with
 *     the set of qbman_pull_desc_set_*() calls
 *
 * Return 0 for success, and -EBUSY if the software portal is not ready
 * to do pull dequeue.
 */
static
int qbman_swp_pull_direct(struct qbman_swp *s, struct qbman_pull_desc *d)
{
	struct qbman_pull_desc *p;

	if (!atomic_dec_and_test(&s->vdq.available)) {
		atomic_inc(&s->vdq.available);
		return -EBUSY;
	}
	s->vdq.storage = (void *)(uintptr_t)d->rsp_addr_virt;
	if ((s->desc->qman_version & QMAN_REV_MASK) < QMAN_REV_5000)
		p = qbman_get_cmd(s, QBMAN_CENA_SWP_VDQCR);
	else
		p = qbman_get_cmd(s, QBMAN_CENA_SWP_VDQCR_MEM);
	p->numf = d->numf;
	p->tok = QMAN_DQ_TOKEN_VALID;
	p->dq_src = d->dq_src;
	p->rsp_addr = d->rsp_addr;
	p->rsp_addr_virt = d->rsp_addr_virt;
	dma_wmb();
	/* Set the verb byte, have to substitute in the valid-bit */
	p->verb = d->verb | s->vdq.valid_bit;
	s->vdq.valid_bit ^= QB_VALID_BIT;

	return 0;
}

/**
 * qbman_swp_pull_mem_back() - Issue the pull dequeue command
 * @s: the software portal object
 * @d: the software portal descriptor which has been configured with
 *     the set of qbman_pull_desc_set_*() calls
 *
 * Return 0 for success, and -EBUSY if the software portal is not ready
 * to do pull dequeue.
 */
static
int qbman_swp_pull_mem_back(struct qbman_swp *s, struct qbman_pull_desc *d)
{
	struct qbman_pull_desc *p;

	if (!atomic_dec_and_test(&s->vdq.available)) {
		atomic_inc(&s->vdq.available);
		return -EBUSY;
	}
	s->vdq.storage = (void *)(uintptr_t)d->rsp_addr_virt;
	if ((s->desc->qman_version & QMAN_REV_MASK) < QMAN_REV_5000)
		p = qbman_get_cmd(s, QBMAN_CENA_SWP_VDQCR);
	else
		p = qbman_get_cmd(s, QBMAN_CENA_SWP_VDQCR_MEM);
	p->numf = d->numf;
	p->tok = QMAN_DQ_TOKEN_VALID;
	p->dq_src = d->dq_src;
	p->rsp_addr = d->rsp_addr;
	p->rsp_addr_virt = d->rsp_addr_virt;

	/* Set the verb byte, have to substitute in the valid-bit */
	p->verb = d->verb | s->vdq.valid_bit;
	s->vdq.valid_bit ^= QB_VALID_BIT;
	dma_wmb();
	qbman_write_register(s, QBMAN_CINH_SWP_VDQCR_RT, QMAN_RT_MODE);

	return 0;
}

#define QMAN_DQRR_PI_MASK   0xf

/**
 * qbman_swp_dqrr_next_direct() - Get an valid DQRR entry
 * @s: the software portal object
 *
 * Return NULL if there are no unconsumed DQRR entries. Return a DQRR entry
 * only once, so repeated calls can return a sequence of DQRR entries, without
 * requiring they be consumed immediately or in any particular order.
 */
const struct dpaa2_dq *qbman_swp_dqrr_next_direct(struct qbman_swp *s)
{
	u32 verb;
	u32 response_verb;
	u32 flags;
	struct dpaa2_dq *p;

	/* Before using valid-bit to detect if something is there, we have to
	 * handle the case of the DQRR reset bug...
	 */
	if (unlikely(s->dqrr.reset_bug)) {
		/*
		 * We pick up new entries by cache-inhibited producer index,
		 * which means that a non-coherent mapping would require us to
		 * invalidate and read *only* once that PI has indicated that
		 * there's an entry here. The first trip around the DQRR ring
		 * will be much less efficient than all subsequent trips around
		 * it...
		 */
		u8 pi = qbman_read_register(s, QBMAN_CINH_SWP_DQPI) &
			QMAN_DQRR_PI_MASK;

		/* there are new entries if pi != next_idx */
		if (pi == s->dqrr.next_idx)
			return NULL;

		/*
		 * if next_idx is/was the last ring index, and 'pi' is
		 * different, we can disable the workaround as all the ring
		 * entries have now been DMA'd to so valid-bit checking is
		 * repaired. Note: this logic needs to be based on next_idx
		 * (which increments one at a time), rather than on pi (which
		 * can burst and wrap-around between our snapshots of it).
		 */
		if (s->dqrr.next_idx == (s->dqrr.dqrr_size - 1)) {
			pr_debug("next_idx=%d, pi=%d, clear reset bug\n",
				 s->dqrr.next_idx, pi);
			s->dqrr.reset_bug = 0;
		}
		prefetch(qbman_get_cmd(s,
				       QBMAN_CENA_SWP_DQRR(s->dqrr.next_idx)));
	}

	p = qbman_get_cmd(s, QBMAN_CENA_SWP_DQRR(s->dqrr.next_idx));
	verb = p->dq.verb;

	/*
	 * If the valid-bit isn't of the expected polarity, nothing there. Note,
	 * in the DQRR reset bug workaround, we shouldn't need to skip these
	 * check, because we've already determined that a new entry is available
	 * and we've invalidated the cacheline before reading it, so the
	 * valid-bit behaviour is repaired and should tell us what we already
	 * knew from reading PI.
	 */
	if ((verb & QB_VALID_BIT) != s->dqrr.valid_bit) {
		prefetch(qbman_get_cmd(s,
				       QBMAN_CENA_SWP_DQRR(s->dqrr.next_idx)));
		return NULL;
	}
	/*
	 * There's something there. Move "next_idx" attention to the next ring
	 * entry (and prefetch it) before returning what we found.
	 */
	s->dqrr.next_idx++;
	s->dqrr.next_idx &= s->dqrr.dqrr_size - 1; /* Wrap around */
	if (!s->dqrr.next_idx)
		s->dqrr.valid_bit ^= QB_VALID_BIT;

	/*
	 * If this is the final response to a volatile dequeue command
	 * indicate that the vdq is available
	 */
	flags = p->dq.stat;
	response_verb = verb & QBMAN_RESULT_MASK;
	if ((response_verb == QBMAN_RESULT_DQ) &&
	    (flags & DPAA2_DQ_STAT_VOLATILE) &&
	    (flags & DPAA2_DQ_STAT_EXPIRED))
		atomic_inc(&s->vdq.available);

	prefetch(qbman_get_cmd(s, QBMAN_CENA_SWP_DQRR(s->dqrr.next_idx)));

	return p;
}

/**
 * qbman_swp_dqrr_next_mem_back() - Get an valid DQRR entry
 * @s: the software portal object
 *
 * Return NULL if there are no unconsumed DQRR entries. Return a DQRR entry
 * only once, so repeated calls can return a sequence of DQRR entries, without
 * requiring they be consumed immediately or in any particular order.
 */
const struct dpaa2_dq *qbman_swp_dqrr_next_mem_back(struct qbman_swp *s)
{
	u32 verb;
	u32 response_verb;
	u32 flags;
	struct dpaa2_dq *p;

	/* Before using valid-bit to detect if something is there, we have to
	 * handle the case of the DQRR reset bug...
	 */
	if (unlikely(s->dqrr.reset_bug)) {
		/*
		 * We pick up new entries by cache-inhibited producer index,
		 * which means that a non-coherent mapping would require us to
		 * invalidate and read *only* once that PI has indicated that
		 * there's an entry here. The first trip around the DQRR ring
		 * will be much less efficient than all subsequent trips around
		 * it...
		 */
		u8 pi = qbman_read_register(s, QBMAN_CINH_SWP_DQPI) &
			QMAN_DQRR_PI_MASK;

		/* there are new entries if pi != next_idx */
		if (pi == s->dqrr.next_idx)
			return NULL;

		/*
		 * if next_idx is/was the last ring index, and 'pi' is
		 * different, we can disable the workaround as all the ring
		 * entries have now been DMA'd to so valid-bit checking is
		 * repaired. Note: this logic needs to be based on next_idx
		 * (which increments one at a time), rather than on pi (which
		 * can burst and wrap-around between our snapshots of it).
		 */
		if (s->dqrr.next_idx == (s->dqrr.dqrr_size - 1)) {
			pr_debug("next_idx=%d, pi=%d, clear reset bug\n",
				 s->dqrr.next_idx, pi);
			s->dqrr.reset_bug = 0;
		}
		prefetch(qbman_get_cmd(s,
				       QBMAN_CENA_SWP_DQRR(s->dqrr.next_idx)));
	}

	p = qbman_get_cmd(s, QBMAN_CENA_SWP_DQRR_MEM(s->dqrr.next_idx));
	verb = p->dq.verb;

	/*
	 * If the valid-bit isn't of the expected polarity, nothing there. Note,
	 * in the DQRR reset bug workaround, we shouldn't need to skip these
	 * check, because we've already determined that a new entry is available
	 * and we've invalidated the cacheline before reading it, so the
	 * valid-bit behaviour is repaired and should tell us what we already
	 * knew from reading PI.
	 */
	if ((verb & QB_VALID_BIT) != s->dqrr.valid_bit) {
		prefetch(qbman_get_cmd(s,
				       QBMAN_CENA_SWP_DQRR(s->dqrr.next_idx)));
		return NULL;
	}
	/*
	 * There's something there. Move "next_idx" attention to the next ring
	 * entry (and prefetch it) before returning what we found.
	 */
	s->dqrr.next_idx++;
	s->dqrr.next_idx &= s->dqrr.dqrr_size - 1; /* Wrap around */
	if (!s->dqrr.next_idx)
		s->dqrr.valid_bit ^= QB_VALID_BIT;

	/*
	 * If this is the final response to a volatile dequeue command
	 * indicate that the vdq is available
	 */
	flags = p->dq.stat;
	response_verb = verb & QBMAN_RESULT_MASK;
	if ((response_verb == QBMAN_RESULT_DQ) &&
	    (flags & DPAA2_DQ_STAT_VOLATILE) &&
	    (flags & DPAA2_DQ_STAT_EXPIRED))
		atomic_inc(&s->vdq.available);

	prefetch(qbman_get_cmd(s, QBMAN_CENA_SWP_DQRR(s->dqrr.next_idx)));

	return p;
}

/**
 * qbman_swp_dqrr_consume() -  Consume DQRR entries previously returned from
 *                             qbman_swp_dqrr_next().
 * @s: the software portal object
 * @dq: the DQRR entry to be consumed
 */
void qbman_swp_dqrr_consume(struct qbman_swp *s, const struct dpaa2_dq *dq)
{
	qbman_write_register(s, QBMAN_CINH_SWP_DCAP, QBMAN_IDX_FROM_DQRR(dq));
}

/**
 * qbman_result_has_new_result() - Check and get the dequeue response from the
 *                                 dq storage memory set in pull dequeue command
 * @s: the software portal object
 * @dq: the dequeue result read from the memory
 *
 * Return 1 for getting a valid dequeue result, or 0 for not getting a valid
 * dequeue result.
 *
 * Only used for user-provided storage of dequeue results, not DQRR. For
 * efficiency purposes, the driver will perform any required endianness
 * conversion to ensure that the user's dequeue result storage is in host-endian
 * format. As such, once the user has called qbman_result_has_new_result() and
 * been returned a valid dequeue result, they should not call it again on
 * the same memory location (except of course if another dequeue command has
 * been executed to produce a new result to that location).
 */
int qbman_result_has_new_result(struct qbman_swp *s, const struct dpaa2_dq *dq)
{
	if (dq->dq.tok != QMAN_DQ_TOKEN_VALID)
		return 0;

	/*
	 * Set token to be 0 so we will detect change back to 1
	 * next time the looping is traversed. Const is cast away here
	 * as we want users to treat the dequeue responses as read only.
	 */
	((struct dpaa2_dq *)dq)->dq.tok = 0;

	/*
	 * Determine whether VDQCR is available based on whether the
	 * current result is sitting in the first storage location of
	 * the busy command.
	 */
	if (s->vdq.storage == dq) {
		s->vdq.storage = NULL;
		atomic_inc(&s->vdq.available);
	}

	return 1;
}

/**
 * qbman_release_desc_clear() - Clear the contents of a descriptor to
 *                              default/starting state.
 */
void qbman_release_desc_clear(struct qbman_release_desc *d)
{
	memset(d, 0, sizeof(*d));
	d->verb = 1 << 5; /* Release Command Valid */
}

/**
 * qbman_release_desc_set_bpid() - Set the ID of the buffer pool to release to
 */
void qbman_release_desc_set_bpid(struct qbman_release_desc *d, u16 bpid)
{
	d->bpid = cpu_to_le16(bpid);
}

/**
 * qbman_release_desc_set_rcdi() - Determines whether or not the portal's RCDI
 * interrupt source should be asserted after the release command is completed.
 */
void qbman_release_desc_set_rcdi(struct qbman_release_desc *d, int enable)
{
	if (enable)
		d->verb |= 1 << 6;
	else
		d->verb &= ~(1 << 6);
}

#define RAR_IDX(rar)     ((rar) & 0x7)
#define RAR_VB(rar)      ((rar) & 0x80)
#define RAR_SUCCESS(rar) ((rar) & 0x100)

/**
 * qbman_swp_release_direct() - Issue a buffer release command
 * @s:           the software portal object
 * @d:           the release descriptor
 * @buffers:     a pointer pointing to the buffer address to be released
 * @num_buffers: number of buffers to be released,  must be less than 8
 *
 * Return 0 for success, -EBUSY if the release command ring is not ready.
 */
int qbman_swp_release_direct(struct qbman_swp *s,
			     const struct qbman_release_desc *d,
			     const u64 *buffers, unsigned int num_buffers)
{
	int i;
	struct qbman_release_desc *p;
	u32 rar;

	if (!num_buffers || (num_buffers > 7))
		return -EINVAL;

	rar = qbman_read_register(s, QBMAN_CINH_SWP_RAR);
	if (!RAR_SUCCESS(rar))
		return -EBUSY;

	/* Start the release command */
	p = qbman_get_cmd(s, QBMAN_CENA_SWP_RCR(RAR_IDX(rar)));

	/* Copy the caller's buffer pointers to the command */
	for (i = 0; i < num_buffers; i++)
		p->buf[i] = cpu_to_le64(buffers[i]);
	p->bpid = d->bpid;

	/*
	 * Set the verb byte, have to substitute in the valid-bit
	 * and the number of buffers.
	 */
	dma_wmb();
	p->verb = d->verb | RAR_VB(rar) | num_buffers;

	return 0;
}

/**
 * qbman_swp_release_mem_back() - Issue a buffer release command
 * @s:           the software portal object
 * @d:           the release descriptor
 * @buffers:     a pointer pointing to the buffer address to be released
 * @num_buffers: number of buffers to be released,  must be less than 8
 *
 * Return 0 for success, -EBUSY if the release command ring is not ready.
 */
int qbman_swp_release_mem_back(struct qbman_swp *s,
			       const struct qbman_release_desc *d,
			       const u64 *buffers, unsigned int num_buffers)
{
	int i;
	struct qbman_release_desc *p;
	u32 rar;

	if (!num_buffers || (num_buffers > 7))
		return -EINVAL;

	rar = qbman_read_register(s, QBMAN_CINH_SWP_RAR);
	if (!RAR_SUCCESS(rar))
		return -EBUSY;

	/* Start the release command */
	p = qbman_get_cmd(s, QBMAN_CENA_SWP_RCR_MEM(RAR_IDX(rar)));

	/* Copy the caller's buffer pointers to the command */
	for (i = 0; i < num_buffers; i++)
		p->buf[i] = cpu_to_le64(buffers[i]);
	p->bpid = d->bpid;

	p->verb = d->verb | RAR_VB(rar) | num_buffers;
	dma_wmb();
	qbman_write_register(s, QBMAN_CINH_SWP_RCR_AM_RT +
			     RAR_IDX(rar)  * 4, QMAN_RT_MODE);

	return 0;
}

struct qbman_acquire_desc {
	u8 verb;
	u8 reserved;
	__le16 bpid;
	u8 num;
	u8 reserved2[59];
};

struct qbman_acquire_rslt {
	u8 verb;
	u8 rslt;
	__le16 reserved;
	u8 num;
	u8 reserved2[3];
	__le64 buf[7];
};

/**
 * qbman_swp_acquire() - Issue a buffer acquire command
 * @s:           the software portal object
 * @bpid:        the buffer pool index
 * @buffers:     a pointer pointing to the acquired buffer addresses
 * @num_buffers: number of buffers to be acquired, must be less than 8
 *
 * Return 0 for success, or negative error code if the acquire command
 * fails.
 */
int qbman_swp_acquire(struct qbman_swp *s, u16 bpid, u64 *buffers,
		      unsigned int num_buffers)
{
	struct qbman_acquire_desc *p;
	struct qbman_acquire_rslt *r;
	int i;

	if (!num_buffers || (num_buffers > 7))
		return -EINVAL;

	/* Start the management command */
	p = qbman_swp_mc_start(s);

	if (!p)
		return -EBUSY;

	/* Encode the caller-provided attributes */
	p->bpid = cpu_to_le16(bpid);
	p->num = num_buffers;

	/* Complete the management command */
	r = qbman_swp_mc_complete(s, p, QBMAN_MC_ACQUIRE);
	if (unlikely(!r)) {
		pr_err("qbman: acquire from BPID %d failed, no response\n",
		       bpid);
		return -EIO;
	}

	/* Decode the outcome */
	WARN_ON((r->verb & 0x7f) != QBMAN_MC_ACQUIRE);

	/* Determine success or failure */
	if (unlikely(r->rslt != QBMAN_MC_RSLT_OK)) {
		pr_err("qbman: acquire from BPID 0x%x failed, code=0x%02x\n",
		       bpid, r->rslt);
		return -EIO;
	}

	WARN_ON(r->num > num_buffers);

	/* Copy the acquired buffers to the caller's array */
	for (i = 0; i < r->num; i++)
		buffers[i] = le64_to_cpu(r->buf[i]);

	return (int)r->num;
}

struct qbman_alt_fq_state_desc {
	u8 verb;
	u8 reserved[3];
	__le32 fqid;
	u8 reserved2[56];
};

struct qbman_alt_fq_state_rslt {
	u8 verb;
	u8 rslt;
	u8 reserved[62];
};

#define ALT_FQ_FQID_MASK 0x00FFFFFF

int qbman_swp_alt_fq_state(struct qbman_swp *s, u32 fqid,
			   u8 alt_fq_verb)
{
	struct qbman_alt_fq_state_desc *p;
	struct qbman_alt_fq_state_rslt *r;

	/* Start the management command */
	p = qbman_swp_mc_start(s);
	if (!p)
		return -EBUSY;

	p->fqid = cpu_to_le32(fqid & ALT_FQ_FQID_MASK);

	/* Complete the management command */
	r = qbman_swp_mc_complete(s, p, alt_fq_verb);
	if (unlikely(!r)) {
		pr_err("qbman: mgmt cmd failed, no response (verb=0x%x)\n",
		       alt_fq_verb);
		return -EIO;
	}

	/* Decode the outcome */
	WARN_ON((r->verb & QBMAN_RESULT_MASK) != alt_fq_verb);

	/* Determine success or failure */
	if (unlikely(r->rslt != QBMAN_MC_RSLT_OK)) {
		pr_err("qbman: ALT FQID %d failed: verb = 0x%08x code = 0x%02x\n",
		       fqid, r->verb, r->rslt);
		return -EIO;
	}

	return 0;
}

struct qbman_cdan_ctrl_desc {
	u8 verb;
	u8 reserved;
	__le16 ch;
	u8 we;
	u8 ctrl;
	__le16 reserved2;
	__le64 cdan_ctx;
	u8 reserved3[48];

};

struct qbman_cdan_ctrl_rslt {
	u8 verb;
	u8 rslt;
	__le16 ch;
	u8 reserved[60];
};

int qbman_swp_CDAN_set(struct qbman_swp *s, u16 channelid,
		       u8 we_mask, u8 cdan_en,
		       u64 ctx)
{
	struct qbman_cdan_ctrl_desc *p = NULL;
	struct qbman_cdan_ctrl_rslt *r = NULL;

	/* Start the management command */
	p = qbman_swp_mc_start(s);
	if (!p)
		return -EBUSY;

	/* Encode the caller-provided attributes */
	p->ch = cpu_to_le16(channelid);
	p->we = we_mask;
	if (cdan_en)
		p->ctrl = 1;
	else
		p->ctrl = 0;
	p->cdan_ctx = cpu_to_le64(ctx);

	/* Complete the management command */
	r = qbman_swp_mc_complete(s, p, QBMAN_WQCHAN_CONFIGURE);
	if (unlikely(!r)) {
		pr_err("qbman: wqchan config failed, no response\n");
		return -EIO;
	}

	WARN_ON((r->verb & 0x7f) != QBMAN_WQCHAN_CONFIGURE);

	/* Determine success or failure */
	if (unlikely(r->rslt != QBMAN_MC_RSLT_OK)) {
		pr_err("qbman: CDAN cQID %d failed: code = 0x%02x\n",
		       channelid, r->rslt);
		return -EIO;
	}

	return 0;
}

#define QBMAN_RESPONSE_VERB_MASK	0x7f
#define QBMAN_FQ_QUERY_NP		0x45
#define QBMAN_BP_QUERY			0x32

struct qbman_fq_query_desc {
	u8 verb;
	u8 reserved[3];
	__le32 fqid;
	u8 reserved2[56];
};

int qbman_fq_query_state(struct qbman_swp *s, u32 fqid,
			 struct qbman_fq_query_np_rslt *r)
{
	struct qbman_fq_query_desc *p;
	void *resp;

	p = (struct qbman_fq_query_desc *)qbman_swp_mc_start(s);
	if (!p)
		return -EBUSY;

	/* FQID is a 24 bit value */
	p->fqid = cpu_to_le32(fqid & 0x00FFFFFF);
	resp = qbman_swp_mc_complete(s, p, QBMAN_FQ_QUERY_NP);
	if (!resp) {
		pr_err("qbman: Query FQID %d NP fields failed, no response\n",
		       fqid);
		return -EIO;
	}
	*r = *(struct qbman_fq_query_np_rslt *)resp;
	/* Decode the outcome */
	WARN_ON((r->verb & QBMAN_RESPONSE_VERB_MASK) != QBMAN_FQ_QUERY_NP);

	/* Determine success or failure */
	if (r->rslt != QBMAN_MC_RSLT_OK) {
		pr_err("Query NP fields of FQID 0x%x failed, code=0x%02x\n",
		       p->fqid, r->rslt);
		return -EIO;
	}

	return 0;
}

u32 qbman_fq_state_frame_count(const struct qbman_fq_query_np_rslt *r)
{
	return (le32_to_cpu(r->frm_cnt) & 0x00FFFFFF);
}

u32 qbman_fq_state_byte_count(const struct qbman_fq_query_np_rslt *r)
{
	return le32_to_cpu(r->byte_cnt);
}

struct qbman_bp_query_desc {
	u8 verb;
	u8 reserved;
	__le16 bpid;
	u8 reserved2[60];
};

int qbman_bp_query(struct qbman_swp *s, u16 bpid,
		   struct qbman_bp_query_rslt *r)
{
	struct qbman_bp_query_desc *p;
	void *resp;

	p = (struct qbman_bp_query_desc *)qbman_swp_mc_start(s);
	if (!p)
		return -EBUSY;

	p->bpid = cpu_to_le16(bpid);
	resp = qbman_swp_mc_complete(s, p, QBMAN_BP_QUERY);
	if (!resp) {
		pr_err("qbman: Query BPID %d fields failed, no response\n",
		       bpid);
		return -EIO;
	}
	*r = *(struct qbman_bp_query_rslt *)resp;
	/* Decode the outcome */
	WARN_ON((r->verb & QBMAN_RESPONSE_VERB_MASK) != QBMAN_BP_QUERY);

	/* Determine success or failure */
	if (r->rslt != QBMAN_MC_RSLT_OK) {
		pr_err("Query fields of BPID 0x%x failed, code=0x%02x\n",
		       bpid, r->rslt);
		return -EIO;
	}

	return 0;
}

u32 qbman_bp_info_num_free_bufs(struct qbman_bp_query_rslt *a)
{
	return le32_to_cpu(a->fill);
}
