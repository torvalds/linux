/* Copyright 2008 - 2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qman_priv.h"

#define DQRR_MAXFILL	15
#define EQCR_ITHRESH	4	/* if EQCR congests, interrupt threshold */
#define IRQNAME		"QMan portal %d"
#define MAX_IRQNAME	16	/* big enough for "QMan portal %d" */
#define QMAN_POLL_LIMIT 32
#define QMAN_PIRQ_DQRR_ITHRESH 12
#define QMAN_PIRQ_MR_ITHRESH 4
#define QMAN_PIRQ_IPERIOD 100

/* Portal register assists */

/* Cache-inhibited register offsets */
#define QM_REG_EQCR_PI_CINH	0x0000
#define QM_REG_EQCR_CI_CINH	0x0004
#define QM_REG_EQCR_ITR		0x0008
#define QM_REG_DQRR_PI_CINH	0x0040
#define QM_REG_DQRR_CI_CINH	0x0044
#define QM_REG_DQRR_ITR		0x0048
#define QM_REG_DQRR_DCAP	0x0050
#define QM_REG_DQRR_SDQCR	0x0054
#define QM_REG_DQRR_VDQCR	0x0058
#define QM_REG_DQRR_PDQCR	0x005c
#define QM_REG_MR_PI_CINH	0x0080
#define QM_REG_MR_CI_CINH	0x0084
#define QM_REG_MR_ITR		0x0088
#define QM_REG_CFG		0x0100
#define QM_REG_ISR		0x0e00
#define QM_REG_IER		0x0e04
#define QM_REG_ISDR		0x0e08
#define QM_REG_IIR		0x0e0c
#define QM_REG_ITPR		0x0e14

/* Cache-enabled register offsets */
#define QM_CL_EQCR		0x0000
#define QM_CL_DQRR		0x1000
#define QM_CL_MR		0x2000
#define QM_CL_EQCR_PI_CENA	0x3000
#define QM_CL_EQCR_CI_CENA	0x3100
#define QM_CL_DQRR_PI_CENA	0x3200
#define QM_CL_DQRR_CI_CENA	0x3300
#define QM_CL_MR_PI_CENA	0x3400
#define QM_CL_MR_CI_CENA	0x3500
#define QM_CL_CR		0x3800
#define QM_CL_RR0		0x3900
#define QM_CL_RR1		0x3940

/*
 * BTW, the drivers (and h/w programming model) already obtain the required
 * synchronisation for portal accesses and data-dependencies. Use of barrier()s
 * or other order-preserving primitives simply degrade performance. Hence the
 * use of the __raw_*() interfaces, which simply ensure that the compiler treats
 * the portal registers as volatile
 */

/* Cache-enabled ring access */
#define qm_cl(base, idx)	((void *)base + ((idx) << 6))

/*
 * Portal modes.
 *   Enum types;
 *     pmode == production mode
 *     cmode == consumption mode,
 *     dmode == h/w dequeue mode.
 *   Enum values use 3 letter codes. First letter matches the portal mode,
 *   remaining two letters indicate;
 *     ci == cache-inhibited portal register
 *     ce == cache-enabled portal register
 *     vb == in-band valid-bit (cache-enabled)
 *     dc == DCA (Discrete Consumption Acknowledgment), DQRR-only
 *   As for "enum qm_dqrr_dmode", it should be self-explanatory.
 */
enum qm_eqcr_pmode {		/* matches QCSP_CFG::EPM */
	qm_eqcr_pci = 0,	/* PI index, cache-inhibited */
	qm_eqcr_pce = 1,	/* PI index, cache-enabled */
	qm_eqcr_pvb = 2		/* valid-bit */
};
enum qm_dqrr_dmode {		/* matches QCSP_CFG::DP */
	qm_dqrr_dpush = 0,	/* SDQCR  + VDQCR */
	qm_dqrr_dpull = 1	/* PDQCR */
};
enum qm_dqrr_pmode {		/* s/w-only */
	qm_dqrr_pci,		/* reads DQRR_PI_CINH */
	qm_dqrr_pce,		/* reads DQRR_PI_CENA */
	qm_dqrr_pvb		/* reads valid-bit */
};
enum qm_dqrr_cmode {		/* matches QCSP_CFG::DCM */
	qm_dqrr_cci = 0,	/* CI index, cache-inhibited */
	qm_dqrr_cce = 1,	/* CI index, cache-enabled */
	qm_dqrr_cdc = 2		/* Discrete Consumption Acknowledgment */
};
enum qm_mr_pmode {		/* s/w-only */
	qm_mr_pci,		/* reads MR_PI_CINH */
	qm_mr_pce,		/* reads MR_PI_CENA */
	qm_mr_pvb		/* reads valid-bit */
};
enum qm_mr_cmode {		/* matches QCSP_CFG::MM */
	qm_mr_cci = 0,		/* CI index, cache-inhibited */
	qm_mr_cce = 1		/* CI index, cache-enabled */
};

/* --- Portal structures --- */

#define QM_EQCR_SIZE		8
#define QM_DQRR_SIZE		16
#define QM_MR_SIZE		8

/* "Enqueue Command" */
struct qm_eqcr_entry {
	u8 _ncw_verb; /* writes to this are non-coherent */
	u8 dca;
	__be16 seqnum;
	u8 __reserved[4];
	__be32 fqid;	/* 24-bit */
	__be32 tag;
	struct qm_fd fd;
	u8 __reserved3[32];
} __packed;
#define QM_EQCR_VERB_VBIT		0x80
#define QM_EQCR_VERB_CMD_MASK		0x61	/* but only one value; */
#define QM_EQCR_VERB_CMD_ENQUEUE	0x01
#define QM_EQCR_SEQNUM_NESN		0x8000	/* Advance NESN */
#define QM_EQCR_SEQNUM_NLIS		0x4000	/* More fragments to come */
#define QM_EQCR_SEQNUM_SEQMASK		0x3fff	/* sequence number goes here */

struct qm_eqcr {
	struct qm_eqcr_entry *ring, *cursor;
	u8 ci, available, ithresh, vbit;
#ifdef CONFIG_FSL_DPAA_CHECKING
	u32 busy;
	enum qm_eqcr_pmode pmode;
#endif
};

struct qm_dqrr {
	const struct qm_dqrr_entry *ring, *cursor;
	u8 pi, ci, fill, ithresh, vbit;
#ifdef CONFIG_FSL_DPAA_CHECKING
	enum qm_dqrr_dmode dmode;
	enum qm_dqrr_pmode pmode;
	enum qm_dqrr_cmode cmode;
#endif
};

struct qm_mr {
	union qm_mr_entry *ring, *cursor;
	u8 pi, ci, fill, ithresh, vbit;
#ifdef CONFIG_FSL_DPAA_CHECKING
	enum qm_mr_pmode pmode;
	enum qm_mr_cmode cmode;
#endif
};

/* MC (Management Command) command */
/* "FQ" command layout */
struct qm_mcc_fq {
	u8 _ncw_verb;
	u8 __reserved1[3];
	__be32 fqid;	/* 24-bit */
	u8 __reserved2[56];
} __packed;

/* "CGR" command layout */
struct qm_mcc_cgr {
	u8 _ncw_verb;
	u8 __reserved1[30];
	u8 cgid;
	u8 __reserved2[32];
};

#define QM_MCC_VERB_VBIT		0x80
#define QM_MCC_VERB_MASK		0x7f	/* where the verb contains; */
#define QM_MCC_VERB_INITFQ_PARKED	0x40
#define QM_MCC_VERB_INITFQ_SCHED	0x41
#define QM_MCC_VERB_QUERYFQ		0x44
#define QM_MCC_VERB_QUERYFQ_NP		0x45	/* "non-programmable" fields */
#define QM_MCC_VERB_QUERYWQ		0x46
#define QM_MCC_VERB_QUERYWQ_DEDICATED	0x47
#define QM_MCC_VERB_ALTER_SCHED		0x48	/* Schedule FQ */
#define QM_MCC_VERB_ALTER_FE		0x49	/* Force Eligible FQ */
#define QM_MCC_VERB_ALTER_RETIRE	0x4a	/* Retire FQ */
#define QM_MCC_VERB_ALTER_OOS		0x4b	/* Take FQ out of service */
#define QM_MCC_VERB_ALTER_FQXON		0x4d	/* FQ XON */
#define QM_MCC_VERB_ALTER_FQXOFF	0x4e	/* FQ XOFF */
#define QM_MCC_VERB_INITCGR		0x50
#define QM_MCC_VERB_MODIFYCGR		0x51
#define QM_MCC_VERB_CGRTESTWRITE	0x52
#define QM_MCC_VERB_QUERYCGR		0x58
#define QM_MCC_VERB_QUERYCONGESTION	0x59
union qm_mc_command {
	struct {
		u8 _ncw_verb; /* writes to this are non-coherent */
		u8 __reserved[63];
	};
	struct qm_mcc_initfq initfq;
	struct qm_mcc_initcgr initcgr;
	struct qm_mcc_fq fq;
	struct qm_mcc_cgr cgr;
};

/* MC (Management Command) result */
/* "Query FQ" */
struct qm_mcr_queryfq {
	u8 verb;
	u8 result;
	u8 __reserved1[8];
	struct qm_fqd fqd;	/* the FQD fields are here */
	u8 __reserved2[30];
} __packed;

/* "Alter FQ State Commands" */
struct qm_mcr_alterfq {
	u8 verb;
	u8 result;
	u8 fqs;		/* Frame Queue Status */
	u8 __reserved1[61];
};
#define QM_MCR_VERB_RRID		0x80
#define QM_MCR_VERB_MASK		QM_MCC_VERB_MASK
#define QM_MCR_VERB_INITFQ_PARKED	QM_MCC_VERB_INITFQ_PARKED
#define QM_MCR_VERB_INITFQ_SCHED	QM_MCC_VERB_INITFQ_SCHED
#define QM_MCR_VERB_QUERYFQ		QM_MCC_VERB_QUERYFQ
#define QM_MCR_VERB_QUERYFQ_NP		QM_MCC_VERB_QUERYFQ_NP
#define QM_MCR_VERB_QUERYWQ		QM_MCC_VERB_QUERYWQ
#define QM_MCR_VERB_QUERYWQ_DEDICATED	QM_MCC_VERB_QUERYWQ_DEDICATED
#define QM_MCR_VERB_ALTER_SCHED		QM_MCC_VERB_ALTER_SCHED
#define QM_MCR_VERB_ALTER_FE		QM_MCC_VERB_ALTER_FE
#define QM_MCR_VERB_ALTER_RETIRE	QM_MCC_VERB_ALTER_RETIRE
#define QM_MCR_VERB_ALTER_OOS		QM_MCC_VERB_ALTER_OOS
#define QM_MCR_RESULT_NULL		0x00
#define QM_MCR_RESULT_OK		0xf0
#define QM_MCR_RESULT_ERR_FQID		0xf1
#define QM_MCR_RESULT_ERR_FQSTATE	0xf2
#define QM_MCR_RESULT_ERR_NOTEMPTY	0xf3	/* OOS fails if FQ is !empty */
#define QM_MCR_RESULT_ERR_BADCHANNEL	0xf4
#define QM_MCR_RESULT_PENDING		0xf8
#define QM_MCR_RESULT_ERR_BADCOMMAND	0xff
#define QM_MCR_FQS_ORLPRESENT		0x02	/* ORL fragments to come */
#define QM_MCR_FQS_NOTEMPTY		0x01	/* FQ has enqueued frames */
#define QM_MCR_TIMEOUT			10000	/* us */
union qm_mc_result {
	struct {
		u8 verb;
		u8 result;
		u8 __reserved1[62];
	};
	struct qm_mcr_queryfq queryfq;
	struct qm_mcr_alterfq alterfq;
	struct qm_mcr_querycgr querycgr;
	struct qm_mcr_querycongestion querycongestion;
	struct qm_mcr_querywq querywq;
	struct qm_mcr_queryfq_np queryfq_np;
};

struct qm_mc {
	union qm_mc_command *cr;
	union qm_mc_result *rr;
	u8 rridx, vbit;
#ifdef CONFIG_FSL_DPAA_CHECKING
	enum {
		/* Can be _mc_start()ed */
		qman_mc_idle,
		/* Can be _mc_commit()ed or _mc_abort()ed */
		qman_mc_user,
		/* Can only be _mc_retry()ed */
		qman_mc_hw
	} state;
#endif
};

struct qm_addr {
	void __iomem *ce;	/* cache-enabled */
	void __iomem *ci;	/* cache-inhibited */
};

struct qm_portal {
	/*
	 * In the non-CONFIG_FSL_DPAA_CHECKING case, the following stuff up to
	 * and including 'mc' fits within a cacheline (yay!). The 'config' part
	 * is setup-only, so isn't a cause for a concern. In other words, don't
	 * rearrange this structure on a whim, there be dragons ...
	 */
	struct qm_addr addr;
	struct qm_eqcr eqcr;
	struct qm_dqrr dqrr;
	struct qm_mr mr;
	struct qm_mc mc;
} ____cacheline_aligned;

/* Cache-inhibited register access. */
static inline u32 qm_in(struct qm_portal *p, u32 offset)
{
	return be32_to_cpu(__raw_readl(p->addr.ci + offset));
}

static inline void qm_out(struct qm_portal *p, u32 offset, u32 val)
{
	__raw_writel(cpu_to_be32(val), p->addr.ci + offset);
}

/* Cache Enabled Portal Access */
static inline void qm_cl_invalidate(struct qm_portal *p, u32 offset)
{
	dpaa_invalidate(p->addr.ce + offset);
}

static inline void qm_cl_touch_ro(struct qm_portal *p, u32 offset)
{
	dpaa_touch_ro(p->addr.ce + offset);
}

static inline u32 qm_ce_in(struct qm_portal *p, u32 offset)
{
	return be32_to_cpu(__raw_readl(p->addr.ce + offset));
}

/* --- EQCR API --- */

#define EQCR_SHIFT	ilog2(sizeof(struct qm_eqcr_entry))
#define EQCR_CARRY	(uintptr_t)(QM_EQCR_SIZE << EQCR_SHIFT)

/* Bit-wise logic to wrap a ring pointer by clearing the "carry bit" */
static struct qm_eqcr_entry *eqcr_carryclear(struct qm_eqcr_entry *p)
{
	uintptr_t addr = (uintptr_t)p;

	addr &= ~EQCR_CARRY;

	return (struct qm_eqcr_entry *)addr;
}

/* Bit-wise logic to convert a ring pointer to a ring index */
static int eqcr_ptr2idx(struct qm_eqcr_entry *e)
{
	return ((uintptr_t)e >> EQCR_SHIFT) & (QM_EQCR_SIZE - 1);
}

/* Increment the 'cursor' ring pointer, taking 'vbit' into account */
static inline void eqcr_inc(struct qm_eqcr *eqcr)
{
	/* increment to the next EQCR pointer and handle overflow and 'vbit' */
	struct qm_eqcr_entry *partial = eqcr->cursor + 1;

	eqcr->cursor = eqcr_carryclear(partial);
	if (partial != eqcr->cursor)
		eqcr->vbit ^= QM_EQCR_VERB_VBIT;
}

static inline int qm_eqcr_init(struct qm_portal *portal,
				enum qm_eqcr_pmode pmode,
				unsigned int eq_stash_thresh,
				int eq_stash_prio)
{
	struct qm_eqcr *eqcr = &portal->eqcr;
	u32 cfg;
	u8 pi;

	eqcr->ring = portal->addr.ce + QM_CL_EQCR;
	eqcr->ci = qm_in(portal, QM_REG_EQCR_CI_CINH) & (QM_EQCR_SIZE - 1);
	qm_cl_invalidate(portal, QM_CL_EQCR_CI_CENA);
	pi = qm_in(portal, QM_REG_EQCR_PI_CINH) & (QM_EQCR_SIZE - 1);
	eqcr->cursor = eqcr->ring + pi;
	eqcr->vbit = (qm_in(portal, QM_REG_EQCR_PI_CINH) & QM_EQCR_SIZE) ?
		     QM_EQCR_VERB_VBIT : 0;
	eqcr->available = QM_EQCR_SIZE - 1 -
			  dpaa_cyc_diff(QM_EQCR_SIZE, eqcr->ci, pi);
	eqcr->ithresh = qm_in(portal, QM_REG_EQCR_ITR);
#ifdef CONFIG_FSL_DPAA_CHECKING
	eqcr->busy = 0;
	eqcr->pmode = pmode;
#endif
	cfg = (qm_in(portal, QM_REG_CFG) & 0x00ffffff) |
	      (eq_stash_thresh << 28) | /* QCSP_CFG: EST */
	      (eq_stash_prio << 26) | /* QCSP_CFG: EP */
	      ((pmode & 0x3) << 24); /* QCSP_CFG::EPM */
	qm_out(portal, QM_REG_CFG, cfg);
	return 0;
}

static inline unsigned int qm_eqcr_get_ci_stashing(struct qm_portal *portal)
{
	return (qm_in(portal, QM_REG_CFG) >> 28) & 0x7;
}

static inline void qm_eqcr_finish(struct qm_portal *portal)
{
	struct qm_eqcr *eqcr = &portal->eqcr;
	u8 pi = qm_in(portal, QM_REG_EQCR_PI_CINH) & (QM_EQCR_SIZE - 1);
	u8 ci = qm_in(portal, QM_REG_EQCR_CI_CINH) & (QM_EQCR_SIZE - 1);

	DPAA_ASSERT(!eqcr->busy);
	if (pi != eqcr_ptr2idx(eqcr->cursor))
		pr_crit("losing uncommitted EQCR entries\n");
	if (ci != eqcr->ci)
		pr_crit("missing existing EQCR completions\n");
	if (eqcr->ci != eqcr_ptr2idx(eqcr->cursor))
		pr_crit("EQCR destroyed unquiesced\n");
}

static inline struct qm_eqcr_entry *qm_eqcr_start_no_stash(struct qm_portal
								 *portal)
{
	struct qm_eqcr *eqcr = &portal->eqcr;

	DPAA_ASSERT(!eqcr->busy);
	if (!eqcr->available)
		return NULL;

#ifdef CONFIG_FSL_DPAA_CHECKING
	eqcr->busy = 1;
#endif
	dpaa_zero(eqcr->cursor);
	return eqcr->cursor;
}

static inline struct qm_eqcr_entry *qm_eqcr_start_stash(struct qm_portal
								*portal)
{
	struct qm_eqcr *eqcr = &portal->eqcr;
	u8 diff, old_ci;

	DPAA_ASSERT(!eqcr->busy);
	if (!eqcr->available) {
		old_ci = eqcr->ci;
		eqcr->ci = qm_ce_in(portal, QM_CL_EQCR_CI_CENA) &
			   (QM_EQCR_SIZE - 1);
		diff = dpaa_cyc_diff(QM_EQCR_SIZE, old_ci, eqcr->ci);
		eqcr->available += diff;
		if (!diff)
			return NULL;
	}
#ifdef CONFIG_FSL_DPAA_CHECKING
	eqcr->busy = 1;
#endif
	dpaa_zero(eqcr->cursor);
	return eqcr->cursor;
}

static inline void eqcr_commit_checks(struct qm_eqcr *eqcr)
{
	DPAA_ASSERT(eqcr->busy);
	DPAA_ASSERT(!(be32_to_cpu(eqcr->cursor->fqid) & ~QM_FQID_MASK));
	DPAA_ASSERT(eqcr->available >= 1);
}

static inline void qm_eqcr_pvb_commit(struct qm_portal *portal, u8 myverb)
{
	struct qm_eqcr *eqcr = &portal->eqcr;
	struct qm_eqcr_entry *eqcursor;

	eqcr_commit_checks(eqcr);
	DPAA_ASSERT(eqcr->pmode == qm_eqcr_pvb);
	dma_wmb();
	eqcursor = eqcr->cursor;
	eqcursor->_ncw_verb = myverb | eqcr->vbit;
	dpaa_flush(eqcursor);
	eqcr_inc(eqcr);
	eqcr->available--;
#ifdef CONFIG_FSL_DPAA_CHECKING
	eqcr->busy = 0;
#endif
}

static inline void qm_eqcr_cce_prefetch(struct qm_portal *portal)
{
	qm_cl_touch_ro(portal, QM_CL_EQCR_CI_CENA);
}

static inline u8 qm_eqcr_cce_update(struct qm_portal *portal)
{
	struct qm_eqcr *eqcr = &portal->eqcr;
	u8 diff, old_ci = eqcr->ci;

	eqcr->ci = qm_ce_in(portal, QM_CL_EQCR_CI_CENA) & (QM_EQCR_SIZE - 1);
	qm_cl_invalidate(portal, QM_CL_EQCR_CI_CENA);
	diff = dpaa_cyc_diff(QM_EQCR_SIZE, old_ci, eqcr->ci);
	eqcr->available += diff;
	return diff;
}

static inline void qm_eqcr_set_ithresh(struct qm_portal *portal, u8 ithresh)
{
	struct qm_eqcr *eqcr = &portal->eqcr;

	eqcr->ithresh = ithresh;
	qm_out(portal, QM_REG_EQCR_ITR, ithresh);
}

static inline u8 qm_eqcr_get_avail(struct qm_portal *portal)
{
	struct qm_eqcr *eqcr = &portal->eqcr;

	return eqcr->available;
}

static inline u8 qm_eqcr_get_fill(struct qm_portal *portal)
{
	struct qm_eqcr *eqcr = &portal->eqcr;

	return QM_EQCR_SIZE - 1 - eqcr->available;
}

/* --- DQRR API --- */

#define DQRR_SHIFT	ilog2(sizeof(struct qm_dqrr_entry))
#define DQRR_CARRY	(uintptr_t)(QM_DQRR_SIZE << DQRR_SHIFT)

static const struct qm_dqrr_entry *dqrr_carryclear(
					const struct qm_dqrr_entry *p)
{
	uintptr_t addr = (uintptr_t)p;

	addr &= ~DQRR_CARRY;

	return (const struct qm_dqrr_entry *)addr;
}

static inline int dqrr_ptr2idx(const struct qm_dqrr_entry *e)
{
	return ((uintptr_t)e >> DQRR_SHIFT) & (QM_DQRR_SIZE - 1);
}

static const struct qm_dqrr_entry *dqrr_inc(const struct qm_dqrr_entry *e)
{
	return dqrr_carryclear(e + 1);
}

static inline void qm_dqrr_set_maxfill(struct qm_portal *portal, u8 mf)
{
	qm_out(portal, QM_REG_CFG, (qm_in(portal, QM_REG_CFG) & 0xff0fffff) |
				   ((mf & (QM_DQRR_SIZE - 1)) << 20));
}

static inline int qm_dqrr_init(struct qm_portal *portal,
			       const struct qm_portal_config *config,
			       enum qm_dqrr_dmode dmode,
			       enum qm_dqrr_pmode pmode,
			       enum qm_dqrr_cmode cmode, u8 max_fill)
{
	struct qm_dqrr *dqrr = &portal->dqrr;
	u32 cfg;

	/* Make sure the DQRR will be idle when we enable */
	qm_out(portal, QM_REG_DQRR_SDQCR, 0);
	qm_out(portal, QM_REG_DQRR_VDQCR, 0);
	qm_out(portal, QM_REG_DQRR_PDQCR, 0);
	dqrr->ring = portal->addr.ce + QM_CL_DQRR;
	dqrr->pi = qm_in(portal, QM_REG_DQRR_PI_CINH) & (QM_DQRR_SIZE - 1);
	dqrr->ci = qm_in(portal, QM_REG_DQRR_CI_CINH) & (QM_DQRR_SIZE - 1);
	dqrr->cursor = dqrr->ring + dqrr->ci;
	dqrr->fill = dpaa_cyc_diff(QM_DQRR_SIZE, dqrr->ci, dqrr->pi);
	dqrr->vbit = (qm_in(portal, QM_REG_DQRR_PI_CINH) & QM_DQRR_SIZE) ?
			QM_DQRR_VERB_VBIT : 0;
	dqrr->ithresh = qm_in(portal, QM_REG_DQRR_ITR);
#ifdef CONFIG_FSL_DPAA_CHECKING
	dqrr->dmode = dmode;
	dqrr->pmode = pmode;
	dqrr->cmode = cmode;
#endif
	/* Invalidate every ring entry before beginning */
	for (cfg = 0; cfg < QM_DQRR_SIZE; cfg++)
		dpaa_invalidate(qm_cl(dqrr->ring, cfg));
	cfg = (qm_in(portal, QM_REG_CFG) & 0xff000f00) |
		((max_fill & (QM_DQRR_SIZE - 1)) << 20) | /* DQRR_MF */
		((dmode & 1) << 18) |			/* DP */
		((cmode & 3) << 16) |			/* DCM */
		0xa0 |					/* RE+SE */
		(0 ? 0x40 : 0) |			/* Ignore RP */
		(0 ? 0x10 : 0);				/* Ignore SP */
	qm_out(portal, QM_REG_CFG, cfg);
	qm_dqrr_set_maxfill(portal, max_fill);
	return 0;
}

static inline void qm_dqrr_finish(struct qm_portal *portal)
{
#ifdef CONFIG_FSL_DPAA_CHECKING
	struct qm_dqrr *dqrr = &portal->dqrr;

	if (dqrr->cmode != qm_dqrr_cdc &&
	    dqrr->ci != dqrr_ptr2idx(dqrr->cursor))
		pr_crit("Ignoring completed DQRR entries\n");
#endif
}

static inline const struct qm_dqrr_entry *qm_dqrr_current(
						struct qm_portal *portal)
{
	struct qm_dqrr *dqrr = &portal->dqrr;

	if (!dqrr->fill)
		return NULL;
	return dqrr->cursor;
}

static inline u8 qm_dqrr_next(struct qm_portal *portal)
{
	struct qm_dqrr *dqrr = &portal->dqrr;

	DPAA_ASSERT(dqrr->fill);
	dqrr->cursor = dqrr_inc(dqrr->cursor);
	return --dqrr->fill;
}

static inline void qm_dqrr_pvb_update(struct qm_portal *portal)
{
	struct qm_dqrr *dqrr = &portal->dqrr;
	struct qm_dqrr_entry *res = qm_cl(dqrr->ring, dqrr->pi);

	DPAA_ASSERT(dqrr->pmode == qm_dqrr_pvb);
#ifndef CONFIG_FSL_PAMU
	/*
	 * If PAMU is not available we need to invalidate the cache.
	 * When PAMU is available the cache is updated by stash
	 */
	dpaa_invalidate_touch_ro(res);
#endif
	/*
	 *  when accessing 'verb', use __raw_readb() to ensure that compiler
	 * inlining doesn't try to optimise out "excess reads".
	 */
	if ((__raw_readb(&res->verb) & QM_DQRR_VERB_VBIT) == dqrr->vbit) {
		dqrr->pi = (dqrr->pi + 1) & (QM_DQRR_SIZE - 1);
		if (!dqrr->pi)
			dqrr->vbit ^= QM_DQRR_VERB_VBIT;
		dqrr->fill++;
	}
}

static inline void qm_dqrr_cdc_consume_1ptr(struct qm_portal *portal,
					const struct qm_dqrr_entry *dq,
					int park)
{
	__maybe_unused struct qm_dqrr *dqrr = &portal->dqrr;
	int idx = dqrr_ptr2idx(dq);

	DPAA_ASSERT(dqrr->cmode == qm_dqrr_cdc);
	DPAA_ASSERT((dqrr->ring + idx) == dq);
	DPAA_ASSERT(idx < QM_DQRR_SIZE);
	qm_out(portal, QM_REG_DQRR_DCAP, (0 << 8) | /* DQRR_DCAP::S */
	       ((park ? 1 : 0) << 6) |		    /* DQRR_DCAP::PK */
	       idx);				    /* DQRR_DCAP::DCAP_CI */
}

static inline void qm_dqrr_cdc_consume_n(struct qm_portal *portal, u32 bitmask)
{
	__maybe_unused struct qm_dqrr *dqrr = &portal->dqrr;

	DPAA_ASSERT(dqrr->cmode == qm_dqrr_cdc);
	qm_out(portal, QM_REG_DQRR_DCAP, (1 << 8) | /* DQRR_DCAP::S */
	       (bitmask << 16));		    /* DQRR_DCAP::DCAP_CI */
}

static inline void qm_dqrr_sdqcr_set(struct qm_portal *portal, u32 sdqcr)
{
	qm_out(portal, QM_REG_DQRR_SDQCR, sdqcr);
}

static inline void qm_dqrr_vdqcr_set(struct qm_portal *portal, u32 vdqcr)
{
	qm_out(portal, QM_REG_DQRR_VDQCR, vdqcr);
}

static inline void qm_dqrr_set_ithresh(struct qm_portal *portal, u8 ithresh)
{
	qm_out(portal, QM_REG_DQRR_ITR, ithresh);
}

/* --- MR API --- */

#define MR_SHIFT	ilog2(sizeof(union qm_mr_entry))
#define MR_CARRY	(uintptr_t)(QM_MR_SIZE << MR_SHIFT)

static union qm_mr_entry *mr_carryclear(union qm_mr_entry *p)
{
	uintptr_t addr = (uintptr_t)p;

	addr &= ~MR_CARRY;

	return (union qm_mr_entry *)addr;
}

static inline int mr_ptr2idx(const union qm_mr_entry *e)
{
	return ((uintptr_t)e >> MR_SHIFT) & (QM_MR_SIZE - 1);
}

static inline union qm_mr_entry *mr_inc(union qm_mr_entry *e)
{
	return mr_carryclear(e + 1);
}

static inline int qm_mr_init(struct qm_portal *portal, enum qm_mr_pmode pmode,
			     enum qm_mr_cmode cmode)
{
	struct qm_mr *mr = &portal->mr;
	u32 cfg;

	mr->ring = portal->addr.ce + QM_CL_MR;
	mr->pi = qm_in(portal, QM_REG_MR_PI_CINH) & (QM_MR_SIZE - 1);
	mr->ci = qm_in(portal, QM_REG_MR_CI_CINH) & (QM_MR_SIZE - 1);
	mr->cursor = mr->ring + mr->ci;
	mr->fill = dpaa_cyc_diff(QM_MR_SIZE, mr->ci, mr->pi);
	mr->vbit = (qm_in(portal, QM_REG_MR_PI_CINH) & QM_MR_SIZE)
		? QM_MR_VERB_VBIT : 0;
	mr->ithresh = qm_in(portal, QM_REG_MR_ITR);
#ifdef CONFIG_FSL_DPAA_CHECKING
	mr->pmode = pmode;
	mr->cmode = cmode;
#endif
	cfg = (qm_in(portal, QM_REG_CFG) & 0xfffff0ff) |
	      ((cmode & 1) << 8);	/* QCSP_CFG:MM */
	qm_out(portal, QM_REG_CFG, cfg);
	return 0;
}

static inline void qm_mr_finish(struct qm_portal *portal)
{
	struct qm_mr *mr = &portal->mr;

	if (mr->ci != mr_ptr2idx(mr->cursor))
		pr_crit("Ignoring completed MR entries\n");
}

static inline const union qm_mr_entry *qm_mr_current(struct qm_portal *portal)
{
	struct qm_mr *mr = &portal->mr;

	if (!mr->fill)
		return NULL;
	return mr->cursor;
}

static inline int qm_mr_next(struct qm_portal *portal)
{
	struct qm_mr *mr = &portal->mr;

	DPAA_ASSERT(mr->fill);
	mr->cursor = mr_inc(mr->cursor);
	return --mr->fill;
}

static inline void qm_mr_pvb_update(struct qm_portal *portal)
{
	struct qm_mr *mr = &portal->mr;
	union qm_mr_entry *res = qm_cl(mr->ring, mr->pi);

	DPAA_ASSERT(mr->pmode == qm_mr_pvb);
	/*
	 *  when accessing 'verb', use __raw_readb() to ensure that compiler
	 * inlining doesn't try to optimise out "excess reads".
	 */
	if ((__raw_readb(&res->verb) & QM_MR_VERB_VBIT) == mr->vbit) {
		mr->pi = (mr->pi + 1) & (QM_MR_SIZE - 1);
		if (!mr->pi)
			mr->vbit ^= QM_MR_VERB_VBIT;
		mr->fill++;
		res = mr_inc(res);
	}
	dpaa_invalidate_touch_ro(res);
}

static inline void qm_mr_cci_consume(struct qm_portal *portal, u8 num)
{
	struct qm_mr *mr = &portal->mr;

	DPAA_ASSERT(mr->cmode == qm_mr_cci);
	mr->ci = (mr->ci + num) & (QM_MR_SIZE - 1);
	qm_out(portal, QM_REG_MR_CI_CINH, mr->ci);
}

static inline void qm_mr_cci_consume_to_current(struct qm_portal *portal)
{
	struct qm_mr *mr = &portal->mr;

	DPAA_ASSERT(mr->cmode == qm_mr_cci);
	mr->ci = mr_ptr2idx(mr->cursor);
	qm_out(portal, QM_REG_MR_CI_CINH, mr->ci);
}

static inline void qm_mr_set_ithresh(struct qm_portal *portal, u8 ithresh)
{
	qm_out(portal, QM_REG_MR_ITR, ithresh);
}

/* --- Management command API --- */

static inline int qm_mc_init(struct qm_portal *portal)
{
	struct qm_mc *mc = &portal->mc;

	mc->cr = portal->addr.ce + QM_CL_CR;
	mc->rr = portal->addr.ce + QM_CL_RR0;
	mc->rridx = (__raw_readb(&mc->cr->_ncw_verb) & QM_MCC_VERB_VBIT)
		    ? 0 : 1;
	mc->vbit = mc->rridx ? QM_MCC_VERB_VBIT : 0;
#ifdef CONFIG_FSL_DPAA_CHECKING
	mc->state = qman_mc_idle;
#endif
	return 0;
}

static inline void qm_mc_finish(struct qm_portal *portal)
{
#ifdef CONFIG_FSL_DPAA_CHECKING
	struct qm_mc *mc = &portal->mc;

	DPAA_ASSERT(mc->state == qman_mc_idle);
	if (mc->state != qman_mc_idle)
		pr_crit("Losing incomplete MC command\n");
#endif
}

static inline union qm_mc_command *qm_mc_start(struct qm_portal *portal)
{
	struct qm_mc *mc = &portal->mc;

	DPAA_ASSERT(mc->state == qman_mc_idle);
#ifdef CONFIG_FSL_DPAA_CHECKING
	mc->state = qman_mc_user;
#endif
	dpaa_zero(mc->cr);
	return mc->cr;
}

static inline void qm_mc_commit(struct qm_portal *portal, u8 myverb)
{
	struct qm_mc *mc = &portal->mc;
	union qm_mc_result *rr = mc->rr + mc->rridx;

	DPAA_ASSERT(mc->state == qman_mc_user);
	dma_wmb();
	mc->cr->_ncw_verb = myverb | mc->vbit;
	dpaa_flush(mc->cr);
	dpaa_invalidate_touch_ro(rr);
#ifdef CONFIG_FSL_DPAA_CHECKING
	mc->state = qman_mc_hw;
#endif
}

static inline union qm_mc_result *qm_mc_result(struct qm_portal *portal)
{
	struct qm_mc *mc = &portal->mc;
	union qm_mc_result *rr = mc->rr + mc->rridx;

	DPAA_ASSERT(mc->state == qman_mc_hw);
	/*
	 *  The inactive response register's verb byte always returns zero until
	 * its command is submitted and completed. This includes the valid-bit,
	 * in case you were wondering...
	 */
	if (!__raw_readb(&rr->verb)) {
		dpaa_invalidate_touch_ro(rr);
		return NULL;
	}
	mc->rridx ^= 1;
	mc->vbit ^= QM_MCC_VERB_VBIT;
#ifdef CONFIG_FSL_DPAA_CHECKING
	mc->state = qman_mc_idle;
#endif
	return rr;
}

static inline int qm_mc_result_timeout(struct qm_portal *portal,
				       union qm_mc_result **mcr)
{
	int timeout = QM_MCR_TIMEOUT;

	do {
		*mcr = qm_mc_result(portal);
		if (*mcr)
			break;
		udelay(1);
	} while (--timeout);

	return timeout;
}

static inline void fq_set(struct qman_fq *fq, u32 mask)
{
	set_bits(mask, &fq->flags);
}

static inline void fq_clear(struct qman_fq *fq, u32 mask)
{
	clear_bits(mask, &fq->flags);
}

static inline int fq_isset(struct qman_fq *fq, u32 mask)
{
	return fq->flags & mask;
}

static inline int fq_isclear(struct qman_fq *fq, u32 mask)
{
	return !(fq->flags & mask);
}

struct qman_portal {
	struct qm_portal p;
	/* PORTAL_BITS_*** - dynamic, strictly internal */
	unsigned long bits;
	/* interrupt sources processed by portal_isr(), configurable */
	unsigned long irq_sources;
	u32 use_eqcr_ci_stashing;
	/* only 1 volatile dequeue at a time */
	struct qman_fq *vdqcr_owned;
	u32 sdqcr;
	/* probing time config params for cpu-affine portals */
	const struct qm_portal_config *config;
	/* 2-element array. cgrs[0] is mask, cgrs[1] is snapshot. */
	struct qman_cgrs *cgrs;
	/* linked-list of CSCN handlers. */
	struct list_head cgr_cbs;
	/* list lock */
	spinlock_t cgr_lock;
	struct work_struct congestion_work;
	struct work_struct mr_work;
	char irqname[MAX_IRQNAME];
};

static cpumask_t affine_mask;
static DEFINE_SPINLOCK(affine_mask_lock);
static u16 affine_channels[NR_CPUS];
static DEFINE_PER_CPU(struct qman_portal, qman_affine_portal);
struct qman_portal *affine_portals[NR_CPUS];

static inline struct qman_portal *get_affine_portal(void)
{
	return &get_cpu_var(qman_affine_portal);
}

static inline void put_affine_portal(void)
{
	put_cpu_var(qman_affine_portal);
}

static struct workqueue_struct *qm_portal_wq;

int qman_wq_alloc(void)
{
	qm_portal_wq = alloc_workqueue("qman_portal_wq", 0, 1);
	if (!qm_portal_wq)
		return -ENOMEM;
	return 0;
}

/*
 * This is what everything can wait on, even if it migrates to a different cpu
 * to the one whose affine portal it is waiting on.
 */
static DECLARE_WAIT_QUEUE_HEAD(affine_queue);

static struct qman_fq **fq_table;
static u32 num_fqids;

int qman_alloc_fq_table(u32 _num_fqids)
{
	num_fqids = _num_fqids;

	fq_table = vzalloc(num_fqids * 2 * sizeof(struct qman_fq *));
	if (!fq_table)
		return -ENOMEM;

	pr_debug("Allocated fq lookup table at %p, entry count %u\n",
		 fq_table, num_fqids * 2);
	return 0;
}

static struct qman_fq *idx_to_fq(u32 idx)
{
	struct qman_fq *fq;

#ifdef CONFIG_FSL_DPAA_CHECKING
	if (WARN_ON(idx >= num_fqids * 2))
		return NULL;
#endif
	fq = fq_table[idx];
	DPAA_ASSERT(!fq || idx == fq->idx);

	return fq;
}

/*
 * Only returns full-service fq objects, not enqueue-only
 * references (QMAN_FQ_FLAG_NO_MODIFY).
 */
static struct qman_fq *fqid_to_fq(u32 fqid)
{
	return idx_to_fq(fqid * 2);
}

static struct qman_fq *tag_to_fq(u32 tag)
{
#if BITS_PER_LONG == 64
	return idx_to_fq(tag);
#else
	return (struct qman_fq *)tag;
#endif
}

static u32 fq_to_tag(struct qman_fq *fq)
{
#if BITS_PER_LONG == 64
	return fq->idx;
#else
	return (u32)fq;
#endif
}

static u32 __poll_portal_slow(struct qman_portal *p, u32 is);
static inline unsigned int __poll_portal_fast(struct qman_portal *p,
					unsigned int poll_limit);
static void qm_congestion_task(struct work_struct *work);
static void qm_mr_process_task(struct work_struct *work);

static irqreturn_t portal_isr(int irq, void *ptr)
{
	struct qman_portal *p = ptr;

	u32 clear = QM_DQAVAIL_MASK | p->irq_sources;
	u32 is = qm_in(&p->p, QM_REG_ISR) & p->irq_sources;

	if (unlikely(!is))
		return IRQ_NONE;

	/* DQRR-handling if it's interrupt-driven */
	if (is & QM_PIRQ_DQRI)
		__poll_portal_fast(p, QMAN_POLL_LIMIT);
	/* Handling of anything else that's interrupt-driven */
	clear |= __poll_portal_slow(p, is);
	qm_out(&p->p, QM_REG_ISR, clear);
	return IRQ_HANDLED;
}

static int drain_mr_fqrni(struct qm_portal *p)
{
	const union qm_mr_entry *msg;
loop:
	msg = qm_mr_current(p);
	if (!msg) {
		/*
		 * if MR was full and h/w had other FQRNI entries to produce, we
		 * need to allow it time to produce those entries once the
		 * existing entries are consumed. A worst-case situation
		 * (fully-loaded system) means h/w sequencers may have to do 3-4
		 * other things before servicing the portal's MR pump, each of
		 * which (if slow) may take ~50 qman cycles (which is ~200
		 * processor cycles). So rounding up and then multiplying this
		 * worst-case estimate by a factor of 10, just to be
		 * ultra-paranoid, goes as high as 10,000 cycles. NB, we consume
		 * one entry at a time, so h/w has an opportunity to produce new
		 * entries well before the ring has been fully consumed, so
		 * we're being *really* paranoid here.
		 */
		u64 now, then = jiffies;

		do {
			now = jiffies;
		} while ((then + 10000) > now);
		msg = qm_mr_current(p);
		if (!msg)
			return 0;
	}
	if ((msg->verb & QM_MR_VERB_TYPE_MASK) != QM_MR_VERB_FQRNI) {
		/* We aren't draining anything but FQRNIs */
		pr_err("Found verb 0x%x in MR\n", msg->verb);
		return -1;
	}
	qm_mr_next(p);
	qm_mr_cci_consume(p, 1);
	goto loop;
}

static int qman_create_portal(struct qman_portal *portal,
			      const struct qm_portal_config *c,
			      const struct qman_cgrs *cgrs)
{
	struct qm_portal *p;
	int ret;
	u32 isdr;

	p = &portal->p;

#ifdef CONFIG_FSL_PAMU
	/* PAMU is required for stashing */
	portal->use_eqcr_ci_stashing = ((qman_ip_rev >= QMAN_REV30) ? 1 : 0);
#else
	portal->use_eqcr_ci_stashing = 0;
#endif
	/*
	 * prep the low-level portal struct with the mapped addresses from the
	 * config, everything that follows depends on it and "config" is more
	 * for (de)reference
	 */
	p->addr.ce = c->addr_virt[DPAA_PORTAL_CE];
	p->addr.ci = c->addr_virt[DPAA_PORTAL_CI];
	/*
	 * If CI-stashing is used, the current defaults use a threshold of 3,
	 * and stash with high-than-DQRR priority.
	 */
	if (qm_eqcr_init(p, qm_eqcr_pvb,
			portal->use_eqcr_ci_stashing ? 3 : 0, 1)) {
		dev_err(c->dev, "EQCR initialisation failed\n");
		goto fail_eqcr;
	}
	if (qm_dqrr_init(p, c, qm_dqrr_dpush, qm_dqrr_pvb,
			qm_dqrr_cdc, DQRR_MAXFILL)) {
		dev_err(c->dev, "DQRR initialisation failed\n");
		goto fail_dqrr;
	}
	if (qm_mr_init(p, qm_mr_pvb, qm_mr_cci)) {
		dev_err(c->dev, "MR initialisation failed\n");
		goto fail_mr;
	}
	if (qm_mc_init(p)) {
		dev_err(c->dev, "MC initialisation failed\n");
		goto fail_mc;
	}
	/* static interrupt-gating controls */
	qm_dqrr_set_ithresh(p, QMAN_PIRQ_DQRR_ITHRESH);
	qm_mr_set_ithresh(p, QMAN_PIRQ_MR_ITHRESH);
	qm_out(p, QM_REG_ITPR, QMAN_PIRQ_IPERIOD);
	portal->cgrs = kmalloc(2 * sizeof(*cgrs), GFP_KERNEL);
	if (!portal->cgrs)
		goto fail_cgrs;
	/* initial snapshot is no-depletion */
	qman_cgrs_init(&portal->cgrs[1]);
	if (cgrs)
		portal->cgrs[0] = *cgrs;
	else
		/* if the given mask is NULL, assume all CGRs can be seen */
		qman_cgrs_fill(&portal->cgrs[0]);
	INIT_LIST_HEAD(&portal->cgr_cbs);
	spin_lock_init(&portal->cgr_lock);
	INIT_WORK(&portal->congestion_work, qm_congestion_task);
	INIT_WORK(&portal->mr_work, qm_mr_process_task);
	portal->bits = 0;
	portal->sdqcr = QM_SDQCR_SOURCE_CHANNELS | QM_SDQCR_COUNT_UPTO3 |
			QM_SDQCR_DEDICATED_PRECEDENCE | QM_SDQCR_TYPE_PRIO_QOS |
			QM_SDQCR_TOKEN_SET(0xab) | QM_SDQCR_CHANNELS_DEDICATED;
	isdr = 0xffffffff;
	qm_out(p, QM_REG_ISDR, isdr);
	portal->irq_sources = 0;
	qm_out(p, QM_REG_IER, 0);
	qm_out(p, QM_REG_ISR, 0xffffffff);
	snprintf(portal->irqname, MAX_IRQNAME, IRQNAME, c->cpu);
	if (request_irq(c->irq, portal_isr, 0, portal->irqname,	portal)) {
		dev_err(c->dev, "request_irq() failed\n");
		goto fail_irq;
	}
	if (c->cpu != -1 && irq_can_set_affinity(c->irq) &&
	    irq_set_affinity(c->irq, cpumask_of(c->cpu))) {
		dev_err(c->dev, "irq_set_affinity() failed\n");
		goto fail_affinity;
	}

	/* Need EQCR to be empty before continuing */
	isdr &= ~QM_PIRQ_EQCI;
	qm_out(p, QM_REG_ISDR, isdr);
	ret = qm_eqcr_get_fill(p);
	if (ret) {
		dev_err(c->dev, "EQCR unclean\n");
		goto fail_eqcr_empty;
	}
	isdr &= ~(QM_PIRQ_DQRI | QM_PIRQ_MRI);
	qm_out(p, QM_REG_ISDR, isdr);
	if (qm_dqrr_current(p)) {
		dev_err(c->dev, "DQRR unclean\n");
		qm_dqrr_cdc_consume_n(p, 0xffff);
	}
	if (qm_mr_current(p) && drain_mr_fqrni(p)) {
		/* special handling, drain just in case it's a few FQRNIs */
		const union qm_mr_entry *e = qm_mr_current(p);

		dev_err(c->dev, "MR dirty, VB 0x%x, rc 0x%x, addr 0x%llx\n",
			e->verb, e->ern.rc, qm_fd_addr_get64(&e->ern.fd));
		goto fail_dqrr_mr_empty;
	}
	/* Success */
	portal->config = c;
	qm_out(p, QM_REG_ISDR, 0);
	qm_out(p, QM_REG_IIR, 0);
	/* Write a sane SDQCR */
	qm_dqrr_sdqcr_set(p, portal->sdqcr);
	return 0;

fail_dqrr_mr_empty:
fail_eqcr_empty:
fail_affinity:
	free_irq(c->irq, portal);
fail_irq:
	kfree(portal->cgrs);
fail_cgrs:
	qm_mc_finish(p);
fail_mc:
	qm_mr_finish(p);
fail_mr:
	qm_dqrr_finish(p);
fail_dqrr:
	qm_eqcr_finish(p);
fail_eqcr:
	return -EIO;
}

struct qman_portal *qman_create_affine_portal(const struct qm_portal_config *c,
					      const struct qman_cgrs *cgrs)
{
	struct qman_portal *portal;
	int err;

	portal = &per_cpu(qman_affine_portal, c->cpu);
	err = qman_create_portal(portal, c, cgrs);
	if (err)
		return NULL;

	spin_lock(&affine_mask_lock);
	cpumask_set_cpu(c->cpu, &affine_mask);
	affine_channels[c->cpu] = c->channel;
	affine_portals[c->cpu] = portal;
	spin_unlock(&affine_mask_lock);

	return portal;
}

static void qman_destroy_portal(struct qman_portal *qm)
{
	const struct qm_portal_config *pcfg;

	/* Stop dequeues on the portal */
	qm_dqrr_sdqcr_set(&qm->p, 0);

	/*
	 * NB we do this to "quiesce" EQCR. If we add enqueue-completions or
	 * something related to QM_PIRQ_EQCI, this may need fixing.
	 * Also, due to the prefetching model used for CI updates in the enqueue
	 * path, this update will only invalidate the CI cacheline *after*
	 * working on it, so we need to call this twice to ensure a full update
	 * irrespective of where the enqueue processing was at when the teardown
	 * began.
	 */
	qm_eqcr_cce_update(&qm->p);
	qm_eqcr_cce_update(&qm->p);
	pcfg = qm->config;

	free_irq(pcfg->irq, qm);

	kfree(qm->cgrs);
	qm_mc_finish(&qm->p);
	qm_mr_finish(&qm->p);
	qm_dqrr_finish(&qm->p);
	qm_eqcr_finish(&qm->p);

	qm->config = NULL;
}

const struct qm_portal_config *qman_destroy_affine_portal(void)
{
	struct qman_portal *qm = get_affine_portal();
	const struct qm_portal_config *pcfg;
	int cpu;

	pcfg = qm->config;
	cpu = pcfg->cpu;

	qman_destroy_portal(qm);

	spin_lock(&affine_mask_lock);
	cpumask_clear_cpu(cpu, &affine_mask);
	spin_unlock(&affine_mask_lock);
	put_affine_portal();
	return pcfg;
}

/* Inline helper to reduce nesting in __poll_portal_slow() */
static inline void fq_state_change(struct qman_portal *p, struct qman_fq *fq,
				   const union qm_mr_entry *msg, u8 verb)
{
	switch (verb) {
	case QM_MR_VERB_FQRL:
		DPAA_ASSERT(fq_isset(fq, QMAN_FQ_STATE_ORL));
		fq_clear(fq, QMAN_FQ_STATE_ORL);
		break;
	case QM_MR_VERB_FQRN:
		DPAA_ASSERT(fq->state == qman_fq_state_parked ||
			    fq->state == qman_fq_state_sched);
		DPAA_ASSERT(fq_isset(fq, QMAN_FQ_STATE_CHANGING));
		fq_clear(fq, QMAN_FQ_STATE_CHANGING);
		if (msg->fq.fqs & QM_MR_FQS_NOTEMPTY)
			fq_set(fq, QMAN_FQ_STATE_NE);
		if (msg->fq.fqs & QM_MR_FQS_ORLPRESENT)
			fq_set(fq, QMAN_FQ_STATE_ORL);
		fq->state = qman_fq_state_retired;
		break;
	case QM_MR_VERB_FQPN:
		DPAA_ASSERT(fq->state == qman_fq_state_sched);
		DPAA_ASSERT(fq_isclear(fq, QMAN_FQ_STATE_CHANGING));
		fq->state = qman_fq_state_parked;
	}
}

static void qm_congestion_task(struct work_struct *work)
{
	struct qman_portal *p = container_of(work, struct qman_portal,
					     congestion_work);
	struct qman_cgrs rr, c;
	union qm_mc_result *mcr;
	struct qman_cgr *cgr;

	spin_lock(&p->cgr_lock);
	qm_mc_start(&p->p);
	qm_mc_commit(&p->p, QM_MCC_VERB_QUERYCONGESTION);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		spin_unlock(&p->cgr_lock);
		dev_crit(p->config->dev, "QUERYCONGESTION timeout\n");
		qman_p_irqsource_add(p, QM_PIRQ_CSCI);
		return;
	}
	/* mask out the ones I'm not interested in */
	qman_cgrs_and(&rr, (struct qman_cgrs *)&mcr->querycongestion.state,
		      &p->cgrs[0]);
	/* check previous snapshot for delta, enter/exit congestion */
	qman_cgrs_xor(&c, &rr, &p->cgrs[1]);
	/* update snapshot */
	qman_cgrs_cp(&p->cgrs[1], &rr);
	/* Invoke callback */
	list_for_each_entry(cgr, &p->cgr_cbs, node)
		if (cgr->cb && qman_cgrs_get(&c, cgr->cgrid))
			cgr->cb(p, cgr, qman_cgrs_get(&rr, cgr->cgrid));
	spin_unlock(&p->cgr_lock);
	qman_p_irqsource_add(p, QM_PIRQ_CSCI);
}

static void qm_mr_process_task(struct work_struct *work)
{
	struct qman_portal *p = container_of(work, struct qman_portal,
					     mr_work);
	const union qm_mr_entry *msg;
	struct qman_fq *fq;
	u8 verb, num = 0;

	preempt_disable();

	while (1) {
		qm_mr_pvb_update(&p->p);
		msg = qm_mr_current(&p->p);
		if (!msg)
			break;

		verb = msg->verb & QM_MR_VERB_TYPE_MASK;
		/* The message is a software ERN iff the 0x20 bit is clear */
		if (verb & 0x20) {
			switch (verb) {
			case QM_MR_VERB_FQRNI:
				/* nada, we drop FQRNIs on the floor */
				break;
			case QM_MR_VERB_FQRN:
			case QM_MR_VERB_FQRL:
				/* Lookup in the retirement table */
				fq = fqid_to_fq(qm_fqid_get(&msg->fq));
				if (WARN_ON(!fq))
					break;
				fq_state_change(p, fq, msg, verb);
				if (fq->cb.fqs)
					fq->cb.fqs(p, fq, msg);
				break;
			case QM_MR_VERB_FQPN:
				/* Parked */
				fq = tag_to_fq(be32_to_cpu(msg->fq.context_b));
				fq_state_change(p, fq, msg, verb);
				if (fq->cb.fqs)
					fq->cb.fqs(p, fq, msg);
				break;
			case QM_MR_VERB_DC_ERN:
				/* DCP ERN */
				pr_crit_once("Leaking DCP ERNs!\n");
				break;
			default:
				pr_crit("Invalid MR verb 0x%02x\n", verb);
			}
		} else {
			/* Its a software ERN */
			fq = tag_to_fq(be32_to_cpu(msg->ern.tag));
			fq->cb.ern(p, fq, msg);
		}
		num++;
		qm_mr_next(&p->p);
	}

	qm_mr_cci_consume(&p->p, num);
	qman_p_irqsource_add(p, QM_PIRQ_MRI);
	preempt_enable();
}

static u32 __poll_portal_slow(struct qman_portal *p, u32 is)
{
	if (is & QM_PIRQ_CSCI) {
		qman_p_irqsource_remove(p, QM_PIRQ_CSCI);
		queue_work_on(smp_processor_id(), qm_portal_wq,
			      &p->congestion_work);
	}

	if (is & QM_PIRQ_EQRI) {
		qm_eqcr_cce_update(&p->p);
		qm_eqcr_set_ithresh(&p->p, 0);
		wake_up(&affine_queue);
	}

	if (is & QM_PIRQ_MRI) {
		qman_p_irqsource_remove(p, QM_PIRQ_MRI);
		queue_work_on(smp_processor_id(), qm_portal_wq,
			      &p->mr_work);
	}

	return is;
}

/*
 * remove some slowish-path stuff from the "fast path" and make sure it isn't
 * inlined.
 */
static noinline void clear_vdqcr(struct qman_portal *p, struct qman_fq *fq)
{
	p->vdqcr_owned = NULL;
	fq_clear(fq, QMAN_FQ_STATE_VDQCR);
	wake_up(&affine_queue);
}

/*
 * The only states that would conflict with other things if they ran at the
 * same time on the same cpu are:
 *
 *   (i) setting/clearing vdqcr_owned, and
 *  (ii) clearing the NE (Not Empty) flag.
 *
 * Both are safe. Because;
 *
 *   (i) this clearing can only occur after qman_volatile_dequeue() has set the
 *	 vdqcr_owned field (which it does before setting VDQCR), and
 *	 qman_volatile_dequeue() blocks interrupts and preemption while this is
 *	 done so that we can't interfere.
 *  (ii) the NE flag is only cleared after qman_retire_fq() has set it, and as
 *	 with (i) that API prevents us from interfering until it's safe.
 *
 * The good thing is that qman_volatile_dequeue() and qman_retire_fq() run far
 * less frequently (ie. per-FQ) than __poll_portal_fast() does, so the nett
 * advantage comes from this function not having to "lock" anything at all.
 *
 * Note also that the callbacks are invoked at points which are safe against the
 * above potential conflicts, but that this function itself is not re-entrant
 * (this is because the function tracks one end of each FIFO in the portal and
 * we do *not* want to lock that). So the consequence is that it is safe for
 * user callbacks to call into any QMan API.
 */
static inline unsigned int __poll_portal_fast(struct qman_portal *p,
					unsigned int poll_limit)
{
	const struct qm_dqrr_entry *dq;
	struct qman_fq *fq;
	enum qman_cb_dqrr_result res;
	unsigned int limit = 0;

	do {
		qm_dqrr_pvb_update(&p->p);
		dq = qm_dqrr_current(&p->p);
		if (!dq)
			break;

		if (dq->stat & QM_DQRR_STAT_UNSCHEDULED) {
			/*
			 * VDQCR: don't trust context_b as the FQ may have
			 * been configured for h/w consumption and we're
			 * draining it post-retirement.
			 */
			fq = p->vdqcr_owned;
			/*
			 * We only set QMAN_FQ_STATE_NE when retiring, so we
			 * only need to check for clearing it when doing
			 * volatile dequeues.  It's one less thing to check
			 * in the critical path (SDQCR).
			 */
			if (dq->stat & QM_DQRR_STAT_FQ_EMPTY)
				fq_clear(fq, QMAN_FQ_STATE_NE);
			/*
			 * This is duplicated from the SDQCR code, but we
			 * have stuff to do before *and* after this callback,
			 * and we don't want multiple if()s in the critical
			 * path (SDQCR).
			 */
			res = fq->cb.dqrr(p, fq, dq);
			if (res == qman_cb_dqrr_stop)
				break;
			/* Check for VDQCR completion */
			if (dq->stat & QM_DQRR_STAT_DQCR_EXPIRED)
				clear_vdqcr(p, fq);
		} else {
			/* SDQCR: context_b points to the FQ */
			fq = tag_to_fq(be32_to_cpu(dq->context_b));
			/* Now let the callback do its stuff */
			res = fq->cb.dqrr(p, fq, dq);
			/*
			 * The callback can request that we exit without
			 * consuming this entry nor advancing;
			 */
			if (res == qman_cb_dqrr_stop)
				break;
		}
		/* Interpret 'dq' from a driver perspective. */
		/*
		 * Parking isn't possible unless HELDACTIVE was set. NB,
		 * FORCEELIGIBLE implies HELDACTIVE, so we only need to
		 * check for HELDACTIVE to cover both.
		 */
		DPAA_ASSERT((dq->stat & QM_DQRR_STAT_FQ_HELDACTIVE) ||
			    (res != qman_cb_dqrr_park));
		/* just means "skip it, I'll consume it myself later on" */
		if (res != qman_cb_dqrr_defer)
			qm_dqrr_cdc_consume_1ptr(&p->p, dq,
						 res == qman_cb_dqrr_park);
		/* Move forward */
		qm_dqrr_next(&p->p);
		/*
		 * Entry processed and consumed, increment our counter.  The
		 * callback can request that we exit after consuming the
		 * entry, and we also exit if we reach our processing limit,
		 * so loop back only if neither of these conditions is met.
		 */
	} while (++limit < poll_limit && res != qman_cb_dqrr_consume_stop);

	return limit;
}

void qman_p_irqsource_add(struct qman_portal *p, u32 bits)
{
	unsigned long irqflags;

	local_irq_save(irqflags);
	set_bits(bits & QM_PIRQ_VISIBLE, &p->irq_sources);
	qm_out(&p->p, QM_REG_IER, p->irq_sources);
	local_irq_restore(irqflags);
}
EXPORT_SYMBOL(qman_p_irqsource_add);

void qman_p_irqsource_remove(struct qman_portal *p, u32 bits)
{
	unsigned long irqflags;
	u32 ier;

	/*
	 * Our interrupt handler only processes+clears status register bits that
	 * are in p->irq_sources. As we're trimming that mask, if one of them
	 * were to assert in the status register just before we remove it from
	 * the enable register, there would be an interrupt-storm when we
	 * release the IRQ lock. So we wait for the enable register update to
	 * take effect in h/w (by reading it back) and then clear all other bits
	 * in the status register. Ie. we clear them from ISR once it's certain
	 * IER won't allow them to reassert.
	 */
	local_irq_save(irqflags);
	bits &= QM_PIRQ_VISIBLE;
	clear_bits(bits, &p->irq_sources);
	qm_out(&p->p, QM_REG_IER, p->irq_sources);
	ier = qm_in(&p->p, QM_REG_IER);
	/*
	 * Using "~ier" (rather than "bits" or "~p->irq_sources") creates a
	 * data-dependency, ie. to protect against re-ordering.
	 */
	qm_out(&p->p, QM_REG_ISR, ~ier);
	local_irq_restore(irqflags);
}
EXPORT_SYMBOL(qman_p_irqsource_remove);

const cpumask_t *qman_affine_cpus(void)
{
	return &affine_mask;
}
EXPORT_SYMBOL(qman_affine_cpus);

u16 qman_affine_channel(int cpu)
{
	if (cpu < 0) {
		struct qman_portal *portal = get_affine_portal();

		cpu = portal->config->cpu;
		put_affine_portal();
	}
	WARN_ON(!cpumask_test_cpu(cpu, &affine_mask));
	return affine_channels[cpu];
}
EXPORT_SYMBOL(qman_affine_channel);

struct qman_portal *qman_get_affine_portal(int cpu)
{
	return affine_portals[cpu];
}
EXPORT_SYMBOL(qman_get_affine_portal);

int qman_p_poll_dqrr(struct qman_portal *p, unsigned int limit)
{
	return __poll_portal_fast(p, limit);
}
EXPORT_SYMBOL(qman_p_poll_dqrr);

void qman_p_static_dequeue_add(struct qman_portal *p, u32 pools)
{
	unsigned long irqflags;

	local_irq_save(irqflags);
	pools &= p->config->pools;
	p->sdqcr |= pools;
	qm_dqrr_sdqcr_set(&p->p, p->sdqcr);
	local_irq_restore(irqflags);
}
EXPORT_SYMBOL(qman_p_static_dequeue_add);

/* Frame queue API */

static const char *mcr_result_str(u8 result)
{
	switch (result) {
	case QM_MCR_RESULT_NULL:
		return "QM_MCR_RESULT_NULL";
	case QM_MCR_RESULT_OK:
		return "QM_MCR_RESULT_OK";
	case QM_MCR_RESULT_ERR_FQID:
		return "QM_MCR_RESULT_ERR_FQID";
	case QM_MCR_RESULT_ERR_FQSTATE:
		return "QM_MCR_RESULT_ERR_FQSTATE";
	case QM_MCR_RESULT_ERR_NOTEMPTY:
		return "QM_MCR_RESULT_ERR_NOTEMPTY";
	case QM_MCR_RESULT_PENDING:
		return "QM_MCR_RESULT_PENDING";
	case QM_MCR_RESULT_ERR_BADCOMMAND:
		return "QM_MCR_RESULT_ERR_BADCOMMAND";
	}
	return "<unknown MCR result>";
}

int qman_create_fq(u32 fqid, u32 flags, struct qman_fq *fq)
{
	if (flags & QMAN_FQ_FLAG_DYNAMIC_FQID) {
		int ret = qman_alloc_fqid(&fqid);

		if (ret)
			return ret;
	}
	fq->fqid = fqid;
	fq->flags = flags;
	fq->state = qman_fq_state_oos;
	fq->cgr_groupid = 0;

	/* A context_b of 0 is allegedly special, so don't use that fqid */
	if (fqid == 0 || fqid >= num_fqids) {
		WARN(1, "bad fqid %d\n", fqid);
		return -EINVAL;
	}

	fq->idx = fqid * 2;
	if (flags & QMAN_FQ_FLAG_NO_MODIFY)
		fq->idx++;

	WARN_ON(fq_table[fq->idx]);
	fq_table[fq->idx] = fq;

	return 0;
}
EXPORT_SYMBOL(qman_create_fq);

void qman_destroy_fq(struct qman_fq *fq)
{
	/*
	 * We don't need to lock the FQ as it is a pre-condition that the FQ be
	 * quiesced. Instead, run some checks.
	 */
	switch (fq->state) {
	case qman_fq_state_parked:
	case qman_fq_state_oos:
		if (fq_isset(fq, QMAN_FQ_FLAG_DYNAMIC_FQID))
			qman_release_fqid(fq->fqid);

		DPAA_ASSERT(fq_table[fq->idx]);
		fq_table[fq->idx] = NULL;
		return;
	default:
		break;
	}
	DPAA_ASSERT(NULL == "qman_free_fq() on unquiesced FQ!");
}
EXPORT_SYMBOL(qman_destroy_fq);

u32 qman_fq_fqid(struct qman_fq *fq)
{
	return fq->fqid;
}
EXPORT_SYMBOL(qman_fq_fqid);

int qman_init_fq(struct qman_fq *fq, u32 flags, struct qm_mcc_initfq *opts)
{
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	struct qman_portal *p;
	u8 res, myverb;
	int ret = 0;

	myverb = (flags & QMAN_INITFQ_FLAG_SCHED)
		? QM_MCC_VERB_INITFQ_SCHED : QM_MCC_VERB_INITFQ_PARKED;

	if (fq->state != qman_fq_state_oos &&
	    fq->state != qman_fq_state_parked)
		return -EINVAL;
#ifdef CONFIG_FSL_DPAA_CHECKING
	if (fq_isset(fq, QMAN_FQ_FLAG_NO_MODIFY))
		return -EINVAL;
#endif
	if (opts && (be16_to_cpu(opts->we_mask) & QM_INITFQ_WE_OAC)) {
		/* And can't be set at the same time as TDTHRESH */
		if (be16_to_cpu(opts->we_mask) & QM_INITFQ_WE_TDTHRESH)
			return -EINVAL;
	}
	/* Issue an INITFQ_[PARKED|SCHED] management command */
	p = get_affine_portal();
	if (fq_isset(fq, QMAN_FQ_STATE_CHANGING) ||
	    (fq->state != qman_fq_state_oos &&
	     fq->state != qman_fq_state_parked)) {
		ret = -EBUSY;
		goto out;
	}
	mcc = qm_mc_start(&p->p);
	if (opts)
		mcc->initfq = *opts;
	qm_fqid_set(&mcc->fq, fq->fqid);
	mcc->initfq.count = 0;
	/*
	 * If the FQ does *not* have the TO_DCPORTAL flag, context_b is set as a
	 * demux pointer. Otherwise, the caller-provided value is allowed to
	 * stand, don't overwrite it.
	 */
	if (fq_isclear(fq, QMAN_FQ_FLAG_TO_DCPORTAL)) {
		dma_addr_t phys_fq;

		mcc->initfq.we_mask |= cpu_to_be16(QM_INITFQ_WE_CONTEXTB);
		mcc->initfq.fqd.context_b = cpu_to_be32(fq_to_tag(fq));
		/*
		 *  and the physical address - NB, if the user wasn't trying to
		 * set CONTEXTA, clear the stashing settings.
		 */
		if (!(be16_to_cpu(mcc->initfq.we_mask) &
				  QM_INITFQ_WE_CONTEXTA)) {
			mcc->initfq.we_mask |=
				cpu_to_be16(QM_INITFQ_WE_CONTEXTA);
			memset(&mcc->initfq.fqd.context_a, 0,
				sizeof(mcc->initfq.fqd.context_a));
		} else {
			struct qman_portal *p = qman_dma_portal;

			phys_fq = dma_map_single(p->config->dev, fq,
						 sizeof(*fq), DMA_TO_DEVICE);
			if (dma_mapping_error(p->config->dev, phys_fq)) {
				dev_err(p->config->dev, "dma_mapping failed\n");
				ret = -EIO;
				goto out;
			}

			qm_fqd_stashing_set64(&mcc->initfq.fqd, phys_fq);
		}
	}
	if (flags & QMAN_INITFQ_FLAG_LOCAL) {
		int wq = 0;

		if (!(be16_to_cpu(mcc->initfq.we_mask) &
				  QM_INITFQ_WE_DESTWQ)) {
			mcc->initfq.we_mask |=
				cpu_to_be16(QM_INITFQ_WE_DESTWQ);
			wq = 4;
		}
		qm_fqd_set_destwq(&mcc->initfq.fqd, p->config->channel, wq);
	}
	qm_mc_commit(&p->p, myverb);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		dev_err(p->config->dev, "MCR timeout\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == myverb);
	res = mcr->result;
	if (res != QM_MCR_RESULT_OK) {
		ret = -EIO;
		goto out;
	}
	if (opts) {
		if (be16_to_cpu(opts->we_mask) & QM_INITFQ_WE_FQCTRL) {
			if (be16_to_cpu(opts->fqd.fq_ctrl) & QM_FQCTRL_CGE)
				fq_set(fq, QMAN_FQ_STATE_CGR_EN);
			else
				fq_clear(fq, QMAN_FQ_STATE_CGR_EN);
		}
		if (be16_to_cpu(opts->we_mask) & QM_INITFQ_WE_CGID)
			fq->cgr_groupid = opts->fqd.cgid;
	}
	fq->state = (flags & QMAN_INITFQ_FLAG_SCHED) ?
		qman_fq_state_sched : qman_fq_state_parked;

out:
	put_affine_portal();
	return ret;
}
EXPORT_SYMBOL(qman_init_fq);

int qman_schedule_fq(struct qman_fq *fq)
{
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	struct qman_portal *p;
	int ret = 0;

	if (fq->state != qman_fq_state_parked)
		return -EINVAL;
#ifdef CONFIG_FSL_DPAA_CHECKING
	if (fq_isset(fq, QMAN_FQ_FLAG_NO_MODIFY))
		return -EINVAL;
#endif
	/* Issue a ALTERFQ_SCHED management command */
	p = get_affine_portal();
	if (fq_isset(fq, QMAN_FQ_STATE_CHANGING) ||
	    fq->state != qman_fq_state_parked) {
		ret = -EBUSY;
		goto out;
	}
	mcc = qm_mc_start(&p->p);
	qm_fqid_set(&mcc->fq, fq->fqid);
	qm_mc_commit(&p->p, QM_MCC_VERB_ALTER_SCHED);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		dev_err(p->config->dev, "ALTER_SCHED timeout\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_ALTER_SCHED);
	if (mcr->result != QM_MCR_RESULT_OK) {
		ret = -EIO;
		goto out;
	}
	fq->state = qman_fq_state_sched;
out:
	put_affine_portal();
	return ret;
}
EXPORT_SYMBOL(qman_schedule_fq);

int qman_retire_fq(struct qman_fq *fq, u32 *flags)
{
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	struct qman_portal *p;
	int ret;
	u8 res;

	if (fq->state != qman_fq_state_parked &&
	    fq->state != qman_fq_state_sched)
		return -EINVAL;
#ifdef CONFIG_FSL_DPAA_CHECKING
	if (fq_isset(fq, QMAN_FQ_FLAG_NO_MODIFY))
		return -EINVAL;
#endif
	p = get_affine_portal();
	if (fq_isset(fq, QMAN_FQ_STATE_CHANGING) ||
	    fq->state == qman_fq_state_retired ||
	    fq->state == qman_fq_state_oos) {
		ret = -EBUSY;
		goto out;
	}
	mcc = qm_mc_start(&p->p);
	qm_fqid_set(&mcc->fq, fq->fqid);
	qm_mc_commit(&p->p, QM_MCC_VERB_ALTER_RETIRE);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		dev_crit(p->config->dev, "ALTER_RETIRE timeout\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_ALTER_RETIRE);
	res = mcr->result;
	/*
	 * "Elegant" would be to treat OK/PENDING the same way; set CHANGING,
	 * and defer the flags until FQRNI or FQRN (respectively) show up. But
	 * "Friendly" is to process OK immediately, and not set CHANGING. We do
	 * friendly, otherwise the caller doesn't necessarily have a fully
	 * "retired" FQ on return even if the retirement was immediate. However
	 * this does mean some code duplication between here and
	 * fq_state_change().
	 */
	if (res == QM_MCR_RESULT_OK) {
		ret = 0;
		/* Process 'fq' right away, we'll ignore FQRNI */
		if (mcr->alterfq.fqs & QM_MCR_FQS_NOTEMPTY)
			fq_set(fq, QMAN_FQ_STATE_NE);
		if (mcr->alterfq.fqs & QM_MCR_FQS_ORLPRESENT)
			fq_set(fq, QMAN_FQ_STATE_ORL);
		if (flags)
			*flags = fq->flags;
		fq->state = qman_fq_state_retired;
		if (fq->cb.fqs) {
			/*
			 * Another issue with supporting "immediate" retirement
			 * is that we're forced to drop FQRNIs, because by the
			 * time they're seen it may already be "too late" (the
			 * fq may have been OOS'd and free()'d already). But if
			 * the upper layer wants a callback whether it's
			 * immediate or not, we have to fake a "MR" entry to
			 * look like an FQRNI...
			 */
			union qm_mr_entry msg;

			msg.verb = QM_MR_VERB_FQRNI;
			msg.fq.fqs = mcr->alterfq.fqs;
			qm_fqid_set(&msg.fq, fq->fqid);
			msg.fq.context_b = cpu_to_be32(fq_to_tag(fq));
			fq->cb.fqs(p, fq, &msg);
		}
	} else if (res == QM_MCR_RESULT_PENDING) {
		ret = 1;
		fq_set(fq, QMAN_FQ_STATE_CHANGING);
	} else {
		ret = -EIO;
	}
out:
	put_affine_portal();
	return ret;
}
EXPORT_SYMBOL(qman_retire_fq);

int qman_oos_fq(struct qman_fq *fq)
{
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	struct qman_portal *p;
	int ret = 0;

	if (fq->state != qman_fq_state_retired)
		return -EINVAL;
#ifdef CONFIG_FSL_DPAA_CHECKING
	if (fq_isset(fq, QMAN_FQ_FLAG_NO_MODIFY))
		return -EINVAL;
#endif
	p = get_affine_portal();
	if (fq_isset(fq, QMAN_FQ_STATE_BLOCKOOS) ||
	    fq->state != qman_fq_state_retired) {
		ret = -EBUSY;
		goto out;
	}
	mcc = qm_mc_start(&p->p);
	qm_fqid_set(&mcc->fq, fq->fqid);
	qm_mc_commit(&p->p, QM_MCC_VERB_ALTER_OOS);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		ret = -ETIMEDOUT;
		goto out;
	}
	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_ALTER_OOS);
	if (mcr->result != QM_MCR_RESULT_OK) {
		ret = -EIO;
		goto out;
	}
	fq->state = qman_fq_state_oos;
out:
	put_affine_portal();
	return ret;
}
EXPORT_SYMBOL(qman_oos_fq);

int qman_query_fq(struct qman_fq *fq, struct qm_fqd *fqd)
{
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	struct qman_portal *p = get_affine_portal();
	int ret = 0;

	mcc = qm_mc_start(&p->p);
	qm_fqid_set(&mcc->fq, fq->fqid);
	qm_mc_commit(&p->p, QM_MCC_VERB_QUERYFQ);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		ret = -ETIMEDOUT;
		goto out;
	}

	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_QUERYFQ);
	if (mcr->result == QM_MCR_RESULT_OK)
		*fqd = mcr->queryfq.fqd;
	else
		ret = -EIO;
out:
	put_affine_portal();
	return ret;
}

int qman_query_fq_np(struct qman_fq *fq, struct qm_mcr_queryfq_np *np)
{
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	struct qman_portal *p = get_affine_portal();
	int ret = 0;

	mcc = qm_mc_start(&p->p);
	qm_fqid_set(&mcc->fq, fq->fqid);
	qm_mc_commit(&p->p, QM_MCC_VERB_QUERYFQ_NP);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		ret = -ETIMEDOUT;
		goto out;
	}

	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_QUERYFQ_NP);
	if (mcr->result == QM_MCR_RESULT_OK)
		*np = mcr->queryfq_np;
	else if (mcr->result == QM_MCR_RESULT_ERR_FQID)
		ret = -ERANGE;
	else
		ret = -EIO;
out:
	put_affine_portal();
	return ret;
}
EXPORT_SYMBOL(qman_query_fq_np);

static int qman_query_cgr(struct qman_cgr *cgr,
			  struct qm_mcr_querycgr *cgrd)
{
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	struct qman_portal *p = get_affine_portal();
	int ret = 0;

	mcc = qm_mc_start(&p->p);
	mcc->cgr.cgid = cgr->cgrid;
	qm_mc_commit(&p->p, QM_MCC_VERB_QUERYCGR);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		ret = -ETIMEDOUT;
		goto out;
	}
	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_QUERYCGR);
	if (mcr->result == QM_MCR_RESULT_OK)
		*cgrd = mcr->querycgr;
	else {
		dev_err(p->config->dev, "QUERY_CGR failed: %s\n",
			mcr_result_str(mcr->result));
		ret = -EIO;
	}
out:
	put_affine_portal();
	return ret;
}

int qman_query_cgr_congested(struct qman_cgr *cgr, bool *result)
{
	struct qm_mcr_querycgr query_cgr;
	int err;

	err = qman_query_cgr(cgr, &query_cgr);
	if (err)
		return err;

	*result = !!query_cgr.cgr.cs;
	return 0;
}
EXPORT_SYMBOL(qman_query_cgr_congested);

/* internal function used as a wait_event() expression */
static int set_p_vdqcr(struct qman_portal *p, struct qman_fq *fq, u32 vdqcr)
{
	unsigned long irqflags;
	int ret = -EBUSY;

	local_irq_save(irqflags);
	if (p->vdqcr_owned)
		goto out;
	if (fq_isset(fq, QMAN_FQ_STATE_VDQCR))
		goto out;

	fq_set(fq, QMAN_FQ_STATE_VDQCR);
	p->vdqcr_owned = fq;
	qm_dqrr_vdqcr_set(&p->p, vdqcr);
	ret = 0;
out:
	local_irq_restore(irqflags);
	return ret;
}

static int set_vdqcr(struct qman_portal **p, struct qman_fq *fq, u32 vdqcr)
{
	int ret;

	*p = get_affine_portal();
	ret = set_p_vdqcr(*p, fq, vdqcr);
	put_affine_portal();
	return ret;
}

static int wait_vdqcr_start(struct qman_portal **p, struct qman_fq *fq,
				u32 vdqcr, u32 flags)
{
	int ret = 0;

	if (flags & QMAN_VOLATILE_FLAG_WAIT_INT)
		ret = wait_event_interruptible(affine_queue,
				!set_vdqcr(p, fq, vdqcr));
	else
		wait_event(affine_queue, !set_vdqcr(p, fq, vdqcr));
	return ret;
}

int qman_volatile_dequeue(struct qman_fq *fq, u32 flags, u32 vdqcr)
{
	struct qman_portal *p;
	int ret;

	if (fq->state != qman_fq_state_parked &&
	    fq->state != qman_fq_state_retired)
		return -EINVAL;
	if (vdqcr & QM_VDQCR_FQID_MASK)
		return -EINVAL;
	if (fq_isset(fq, QMAN_FQ_STATE_VDQCR))
		return -EBUSY;
	vdqcr = (vdqcr & ~QM_VDQCR_FQID_MASK) | fq->fqid;
	if (flags & QMAN_VOLATILE_FLAG_WAIT)
		ret = wait_vdqcr_start(&p, fq, vdqcr, flags);
	else
		ret = set_vdqcr(&p, fq, vdqcr);
	if (ret)
		return ret;
	/* VDQCR is set */
	if (flags & QMAN_VOLATILE_FLAG_FINISH) {
		if (flags & QMAN_VOLATILE_FLAG_WAIT_INT)
			/*
			 * NB: don't propagate any error - the caller wouldn't
			 * know whether the VDQCR was issued or not. A signal
			 * could arrive after returning anyway, so the caller
			 * can check signal_pending() if that's an issue.
			 */
			wait_event_interruptible(affine_queue,
				!fq_isset(fq, QMAN_FQ_STATE_VDQCR));
		else
			wait_event(affine_queue,
				!fq_isset(fq, QMAN_FQ_STATE_VDQCR));
	}
	return 0;
}
EXPORT_SYMBOL(qman_volatile_dequeue);

static void update_eqcr_ci(struct qman_portal *p, u8 avail)
{
	if (avail)
		qm_eqcr_cce_prefetch(&p->p);
	else
		qm_eqcr_cce_update(&p->p);
}

int qman_enqueue(struct qman_fq *fq, const struct qm_fd *fd)
{
	struct qman_portal *p;
	struct qm_eqcr_entry *eq;
	unsigned long irqflags;
	u8 avail;

	p = get_affine_portal();
	local_irq_save(irqflags);

	if (p->use_eqcr_ci_stashing) {
		/*
		 * The stashing case is easy, only update if we need to in
		 * order to try and liberate ring entries.
		 */
		eq = qm_eqcr_start_stash(&p->p);
	} else {
		/*
		 * The non-stashing case is harder, need to prefetch ahead of
		 * time.
		 */
		avail = qm_eqcr_get_avail(&p->p);
		if (avail < 2)
			update_eqcr_ci(p, avail);
		eq = qm_eqcr_start_no_stash(&p->p);
	}

	if (unlikely(!eq))
		goto out;

	qm_fqid_set(eq, fq->fqid);
	eq->tag = cpu_to_be32(fq_to_tag(fq));
	eq->fd = *fd;

	qm_eqcr_pvb_commit(&p->p, QM_EQCR_VERB_CMD_ENQUEUE);
out:
	local_irq_restore(irqflags);
	put_affine_portal();
	return 0;
}
EXPORT_SYMBOL(qman_enqueue);

static int qm_modify_cgr(struct qman_cgr *cgr, u32 flags,
			 struct qm_mcc_initcgr *opts)
{
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	struct qman_portal *p = get_affine_portal();
	u8 verb = QM_MCC_VERB_MODIFYCGR;
	int ret = 0;

	mcc = qm_mc_start(&p->p);
	if (opts)
		mcc->initcgr = *opts;
	mcc->initcgr.cgid = cgr->cgrid;
	if (flags & QMAN_CGR_FLAG_USE_INIT)
		verb = QM_MCC_VERB_INITCGR;
	qm_mc_commit(&p->p, verb);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		ret = -ETIMEDOUT;
		goto out;
	}

	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == verb);
	if (mcr->result != QM_MCR_RESULT_OK)
		ret = -EIO;

out:
	put_affine_portal();
	return ret;
}

#define PORTAL_IDX(n)	(n->config->channel - QM_CHANNEL_SWPORTAL0)

/* congestion state change notification target update control */
static void qm_cgr_cscn_targ_set(struct __qm_mc_cgr *cgr, int pi, u32 val)
{
	if (qman_ip_rev >= QMAN_REV30)
		cgr->cscn_targ_upd_ctrl = cpu_to_be16(pi |
					QM_CGR_TARG_UDP_CTRL_WRITE_BIT);
	else
		cgr->cscn_targ = cpu_to_be32(val | QM_CGR_TARG_PORTAL(pi));
}

static void qm_cgr_cscn_targ_clear(struct __qm_mc_cgr *cgr, int pi, u32 val)
{
	if (qman_ip_rev >= QMAN_REV30)
		cgr->cscn_targ_upd_ctrl = cpu_to_be16(pi);
	else
		cgr->cscn_targ = cpu_to_be32(val & ~QM_CGR_TARG_PORTAL(pi));
}

static u8 qman_cgr_cpus[CGR_NUM];

void qman_init_cgr_all(void)
{
	struct qman_cgr cgr;
	int err_cnt = 0;

	for (cgr.cgrid = 0; cgr.cgrid < CGR_NUM; cgr.cgrid++) {
		if (qm_modify_cgr(&cgr, QMAN_CGR_FLAG_USE_INIT, NULL))
			err_cnt++;
	}

	if (err_cnt)
		pr_err("Warning: %d error%s while initialising CGR h/w\n",
		       err_cnt, (err_cnt > 1) ? "s" : "");
}

int qman_create_cgr(struct qman_cgr *cgr, u32 flags,
		    struct qm_mcc_initcgr *opts)
{
	struct qm_mcr_querycgr cgr_state;
	int ret;
	struct qman_portal *p;

	/*
	 * We have to check that the provided CGRID is within the limits of the
	 * data-structures, for obvious reasons. However we'll let h/w take
	 * care of determining whether it's within the limits of what exists on
	 * the SoC.
	 */
	if (cgr->cgrid >= CGR_NUM)
		return -EINVAL;

	preempt_disable();
	p = get_affine_portal();
	qman_cgr_cpus[cgr->cgrid] = smp_processor_id();
	preempt_enable();

	cgr->chan = p->config->channel;
	spin_lock(&p->cgr_lock);

	if (opts) {
		struct qm_mcc_initcgr local_opts = *opts;

		ret = qman_query_cgr(cgr, &cgr_state);
		if (ret)
			goto out;

		qm_cgr_cscn_targ_set(&local_opts.cgr, PORTAL_IDX(p),
				     be32_to_cpu(cgr_state.cgr.cscn_targ));
		local_opts.we_mask |= cpu_to_be16(QM_CGR_WE_CSCN_TARG);

		/* send init if flags indicate so */
		if (flags & QMAN_CGR_FLAG_USE_INIT)
			ret = qm_modify_cgr(cgr, QMAN_CGR_FLAG_USE_INIT,
					    &local_opts);
		else
			ret = qm_modify_cgr(cgr, 0, &local_opts);
		if (ret)
			goto out;
	}

	list_add(&cgr->node, &p->cgr_cbs);

	/* Determine if newly added object requires its callback to be called */
	ret = qman_query_cgr(cgr, &cgr_state);
	if (ret) {
		/* we can't go back, so proceed and return success */
		dev_err(p->config->dev, "CGR HW state partially modified\n");
		ret = 0;
		goto out;
	}
	if (cgr->cb && cgr_state.cgr.cscn_en &&
	    qman_cgrs_get(&p->cgrs[1], cgr->cgrid))
		cgr->cb(p, cgr, 1);
out:
	spin_unlock(&p->cgr_lock);
	put_affine_portal();
	return ret;
}
EXPORT_SYMBOL(qman_create_cgr);

int qman_delete_cgr(struct qman_cgr *cgr)
{
	unsigned long irqflags;
	struct qm_mcr_querycgr cgr_state;
	struct qm_mcc_initcgr local_opts;
	int ret = 0;
	struct qman_cgr *i;
	struct qman_portal *p = get_affine_portal();

	if (cgr->chan != p->config->channel) {
		/* attempt to delete from other portal than creator */
		dev_err(p->config->dev, "CGR not owned by current portal");
		dev_dbg(p->config->dev, " create 0x%x, delete 0x%x\n",
			cgr->chan, p->config->channel);

		ret = -EINVAL;
		goto put_portal;
	}
	memset(&local_opts, 0, sizeof(struct qm_mcc_initcgr));
	spin_lock_irqsave(&p->cgr_lock, irqflags);
	list_del(&cgr->node);
	/*
	 * If there are no other CGR objects for this CGRID in the list,
	 * update CSCN_TARG accordingly
	 */
	list_for_each_entry(i, &p->cgr_cbs, node)
		if (i->cgrid == cgr->cgrid && i->cb)
			goto release_lock;
	ret = qman_query_cgr(cgr, &cgr_state);
	if (ret)  {
		/* add back to the list */
		list_add(&cgr->node, &p->cgr_cbs);
		goto release_lock;
	}

	local_opts.we_mask = cpu_to_be16(QM_CGR_WE_CSCN_TARG);
	qm_cgr_cscn_targ_clear(&local_opts.cgr, PORTAL_IDX(p),
			       be32_to_cpu(cgr_state.cgr.cscn_targ));

	ret = qm_modify_cgr(cgr, 0, &local_opts);
	if (ret)
		/* add back to the list */
		list_add(&cgr->node, &p->cgr_cbs);
release_lock:
	spin_unlock_irqrestore(&p->cgr_lock, irqflags);
put_portal:
	put_affine_portal();
	return ret;
}
EXPORT_SYMBOL(qman_delete_cgr);

struct cgr_comp {
	struct qman_cgr *cgr;
	struct completion completion;
};

static void qman_delete_cgr_smp_call(void *p)
{
	qman_delete_cgr((struct qman_cgr *)p);
}

void qman_delete_cgr_safe(struct qman_cgr *cgr)
{
	preempt_disable();
	if (qman_cgr_cpus[cgr->cgrid] != smp_processor_id()) {
		smp_call_function_single(qman_cgr_cpus[cgr->cgrid],
					 qman_delete_cgr_smp_call, cgr, true);
		preempt_enable();
		return;
	}

	qman_delete_cgr(cgr);
	preempt_enable();
}
EXPORT_SYMBOL(qman_delete_cgr_safe);

/* Cleanup FQs */

static int _qm_mr_consume_and_match_verb(struct qm_portal *p, int v)
{
	const union qm_mr_entry *msg;
	int found = 0;

	qm_mr_pvb_update(p);
	msg = qm_mr_current(p);
	while (msg) {
		if ((msg->verb & QM_MR_VERB_TYPE_MASK) == v)
			found = 1;
		qm_mr_next(p);
		qm_mr_cci_consume_to_current(p);
		qm_mr_pvb_update(p);
		msg = qm_mr_current(p);
	}
	return found;
}

static int _qm_dqrr_consume_and_match(struct qm_portal *p, u32 fqid, int s,
				      bool wait)
{
	const struct qm_dqrr_entry *dqrr;
	int found = 0;

	do {
		qm_dqrr_pvb_update(p);
		dqrr = qm_dqrr_current(p);
		if (!dqrr)
			cpu_relax();
	} while (wait && !dqrr);

	while (dqrr) {
		if (qm_fqid_get(dqrr) == fqid && (dqrr->stat & s))
			found = 1;
		qm_dqrr_cdc_consume_1ptr(p, dqrr, 0);
		qm_dqrr_pvb_update(p);
		qm_dqrr_next(p);
		dqrr = qm_dqrr_current(p);
	}
	return found;
}

#define qm_mr_drain(p, V) \
	_qm_mr_consume_and_match_verb(p, QM_MR_VERB_##V)

#define qm_dqrr_drain(p, f, S) \
	_qm_dqrr_consume_and_match(p, f, QM_DQRR_STAT_##S, false)

#define qm_dqrr_drain_wait(p, f, S) \
	_qm_dqrr_consume_and_match(p, f, QM_DQRR_STAT_##S, true)

#define qm_dqrr_drain_nomatch(p) \
	_qm_dqrr_consume_and_match(p, 0, 0, false)

static int qman_shutdown_fq(u32 fqid)
{
	struct qman_portal *p;
	struct device *dev;
	union qm_mc_command *mcc;
	union qm_mc_result *mcr;
	int orl_empty, drain = 0, ret = 0;
	u32 channel, wq, res;
	u8 state;

	p = get_affine_portal();
	dev = p->config->dev;
	/* Determine the state of the FQID */
	mcc = qm_mc_start(&p->p);
	qm_fqid_set(&mcc->fq, fqid);
	qm_mc_commit(&p->p, QM_MCC_VERB_QUERYFQ_NP);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		dev_err(dev, "QUERYFQ_NP timeout\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_QUERYFQ_NP);
	state = mcr->queryfq_np.state & QM_MCR_NP_STATE_MASK;
	if (state == QM_MCR_NP_STATE_OOS)
		goto out; /* Already OOS, no need to do anymore checks */

	/* Query which channel the FQ is using */
	mcc = qm_mc_start(&p->p);
	qm_fqid_set(&mcc->fq, fqid);
	qm_mc_commit(&p->p, QM_MCC_VERB_QUERYFQ);
	if (!qm_mc_result_timeout(&p->p, &mcr)) {
		dev_err(dev, "QUERYFQ timeout\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_QUERYFQ);
	/* Need to store these since the MCR gets reused */
	channel = qm_fqd_get_chan(&mcr->queryfq.fqd);
	wq = qm_fqd_get_wq(&mcr->queryfq.fqd);

	switch (state) {
	case QM_MCR_NP_STATE_TEN_SCHED:
	case QM_MCR_NP_STATE_TRU_SCHED:
	case QM_MCR_NP_STATE_ACTIVE:
	case QM_MCR_NP_STATE_PARKED:
		orl_empty = 0;
		mcc = qm_mc_start(&p->p);
		qm_fqid_set(&mcc->fq, fqid);
		qm_mc_commit(&p->p, QM_MCC_VERB_ALTER_RETIRE);
		if (!qm_mc_result_timeout(&p->p, &mcr)) {
			dev_err(dev, "QUERYFQ_NP timeout\n");
			ret = -ETIMEDOUT;
			goto out;
		}
		DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) ==
			    QM_MCR_VERB_ALTER_RETIRE);
		res = mcr->result; /* Make a copy as we reuse MCR below */

		if (res == QM_MCR_RESULT_PENDING) {
			/*
			 * Need to wait for the FQRN in the message ring, which
			 * will only occur once the FQ has been drained.  In
			 * order for the FQ to drain the portal needs to be set
			 * to dequeue from the channel the FQ is scheduled on
			 */
			int found_fqrn = 0;
			u16 dequeue_wq = 0;

			/* Flag that we need to drain FQ */
			drain = 1;

			if (channel >= qm_channel_pool1 &&
			    channel < qm_channel_pool1 + 15) {
				/* Pool channel, enable the bit in the portal */
				dequeue_wq = (channel -
					      qm_channel_pool1 + 1)<<4 | wq;
			} else if (channel < qm_channel_pool1) {
				/* Dedicated channel */
				dequeue_wq = wq;
			} else {
				dev_err(dev, "Can't recover FQ 0x%x, ch: 0x%x",
					fqid, channel);
				ret = -EBUSY;
				goto out;
			}
			/* Set the sdqcr to drain this channel */
			if (channel < qm_channel_pool1)
				qm_dqrr_sdqcr_set(&p->p,
						  QM_SDQCR_TYPE_ACTIVE |
						  QM_SDQCR_CHANNELS_DEDICATED);
			else
				qm_dqrr_sdqcr_set(&p->p,
						  QM_SDQCR_TYPE_ACTIVE |
						  QM_SDQCR_CHANNELS_POOL_CONV
						  (channel));
			do {
				/* Keep draining DQRR while checking the MR*/
				qm_dqrr_drain_nomatch(&p->p);
				/* Process message ring too */
				found_fqrn = qm_mr_drain(&p->p, FQRN);
				cpu_relax();
			} while (!found_fqrn);

		}
		if (res != QM_MCR_RESULT_OK &&
		    res != QM_MCR_RESULT_PENDING) {
			dev_err(dev, "retire_fq failed: FQ 0x%x, res=0x%x\n",
				fqid, res);
			ret = -EIO;
			goto out;
		}
		if (!(mcr->alterfq.fqs & QM_MCR_FQS_ORLPRESENT)) {
			/*
			 * ORL had no entries, no need to wait until the
			 * ERNs come in
			 */
			orl_empty = 1;
		}
		/*
		 * Retirement succeeded, check to see if FQ needs
		 * to be drained
		 */
		if (drain || mcr->alterfq.fqs & QM_MCR_FQS_NOTEMPTY) {
			/* FQ is Not Empty, drain using volatile DQ commands */
			do {
				u32 vdqcr = fqid | QM_VDQCR_NUMFRAMES_SET(3);

				qm_dqrr_vdqcr_set(&p->p, vdqcr);
				/*
				 * Wait for a dequeue and process the dequeues,
				 * making sure to empty the ring completely
				 */
			} while (qm_dqrr_drain_wait(&p->p, fqid, FQ_EMPTY));
		}
		qm_dqrr_sdqcr_set(&p->p, 0);

		while (!orl_empty) {
			/* Wait for the ORL to have been completely drained */
			orl_empty = qm_mr_drain(&p->p, FQRL);
			cpu_relax();
		}
		mcc = qm_mc_start(&p->p);
		qm_fqid_set(&mcc->fq, fqid);
		qm_mc_commit(&p->p, QM_MCC_VERB_ALTER_OOS);
		if (!qm_mc_result_timeout(&p->p, &mcr)) {
			ret = -ETIMEDOUT;
			goto out;
		}

		DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) ==
			    QM_MCR_VERB_ALTER_OOS);
		if (mcr->result != QM_MCR_RESULT_OK) {
			dev_err(dev, "OOS after drain fail: FQ 0x%x (0x%x)\n",
				fqid, mcr->result);
			ret = -EIO;
			goto out;
		}
		break;

	case QM_MCR_NP_STATE_RETIRED:
		/* Send OOS Command */
		mcc = qm_mc_start(&p->p);
		qm_fqid_set(&mcc->fq, fqid);
		qm_mc_commit(&p->p, QM_MCC_VERB_ALTER_OOS);
		if (!qm_mc_result_timeout(&p->p, &mcr)) {
			ret = -ETIMEDOUT;
			goto out;
		}

		DPAA_ASSERT((mcr->verb & QM_MCR_VERB_MASK) ==
			    QM_MCR_VERB_ALTER_OOS);
		if (mcr->result) {
			dev_err(dev, "OOS fail: FQ 0x%x (0x%x)\n",
				fqid, mcr->result);
			ret = -EIO;
			goto out;
		}
		break;

	case QM_MCR_NP_STATE_OOS:
		/*  Done */
		break;

	default:
		ret = -EIO;
	}

out:
	put_affine_portal();
	return ret;
}

const struct qm_portal_config *qman_get_qm_portal_config(
						struct qman_portal *portal)
{
	return portal->config;
}
EXPORT_SYMBOL(qman_get_qm_portal_config);

struct gen_pool *qm_fqalloc; /* FQID allocator */
struct gen_pool *qm_qpalloc; /* pool-channel allocator */
struct gen_pool *qm_cgralloc; /* CGR ID allocator */

static int qman_alloc_range(struct gen_pool *p, u32 *result, u32 cnt)
{
	unsigned long addr;

	addr = gen_pool_alloc(p, cnt);
	if (!addr)
		return -ENOMEM;

	*result = addr & ~DPAA_GENALLOC_OFF;

	return 0;
}

int qman_alloc_fqid_range(u32 *result, u32 count)
{
	return qman_alloc_range(qm_fqalloc, result, count);
}
EXPORT_SYMBOL(qman_alloc_fqid_range);

int qman_alloc_pool_range(u32 *result, u32 count)
{
	return qman_alloc_range(qm_qpalloc, result, count);
}
EXPORT_SYMBOL(qman_alloc_pool_range);

int qman_alloc_cgrid_range(u32 *result, u32 count)
{
	return qman_alloc_range(qm_cgralloc, result, count);
}
EXPORT_SYMBOL(qman_alloc_cgrid_range);

int qman_release_fqid(u32 fqid)
{
	int ret = qman_shutdown_fq(fqid);

	if (ret) {
		pr_debug("FQID %d leaked\n", fqid);
		return ret;
	}

	gen_pool_free(qm_fqalloc, fqid | DPAA_GENALLOC_OFF, 1);
	return 0;
}
EXPORT_SYMBOL(qman_release_fqid);

static int qpool_cleanup(u32 qp)
{
	/*
	 * We query all FQDs starting from
	 * FQID 1 until we get an "invalid FQID" error, looking for non-OOS FQDs
	 * whose destination channel is the pool-channel being released.
	 * When a non-OOS FQD is found we attempt to clean it up
	 */
	struct qman_fq fq = {
		.fqid = QM_FQID_RANGE_START
	};
	int err;

	do {
		struct qm_mcr_queryfq_np np;

		err = qman_query_fq_np(&fq, &np);
		if (err == -ERANGE)
			/* FQID range exceeded, found no problems */
			return 0;
		else if (WARN_ON(err))
			return err;

		if ((np.state & QM_MCR_NP_STATE_MASK) != QM_MCR_NP_STATE_OOS) {
			struct qm_fqd fqd;

			err = qman_query_fq(&fq, &fqd);
			if (WARN_ON(err))
				return err;
			if (qm_fqd_get_chan(&fqd) == qp) {
				/* The channel is the FQ's target, clean it */
				err = qman_shutdown_fq(fq.fqid);
				if (err)
					/*
					 * Couldn't shut down the FQ
					 * so the pool must be leaked
					 */
					return err;
			}
		}
		/* Move to the next FQID */
		fq.fqid++;
	} while (1);
}

int qman_release_pool(u32 qp)
{
	int ret;

	ret = qpool_cleanup(qp);
	if (ret) {
		pr_debug("CHID %d leaked\n", qp);
		return ret;
	}

	gen_pool_free(qm_qpalloc, qp | DPAA_GENALLOC_OFF, 1);
	return 0;
}
EXPORT_SYMBOL(qman_release_pool);

static int cgr_cleanup(u32 cgrid)
{
	/*
	 * query all FQDs starting from FQID 1 until we get an "invalid FQID"
	 * error, looking for non-OOS FQDs whose CGR is the CGR being released
	 */
	struct qman_fq fq = {
		.fqid = QM_FQID_RANGE_START
	};
	int err;

	do {
		struct qm_mcr_queryfq_np np;

		err = qman_query_fq_np(&fq, &np);
		if (err == -ERANGE)
			/* FQID range exceeded, found no problems */
			return 0;
		else if (WARN_ON(err))
			return err;

		if ((np.state & QM_MCR_NP_STATE_MASK) != QM_MCR_NP_STATE_OOS) {
			struct qm_fqd fqd;

			err = qman_query_fq(&fq, &fqd);
			if (WARN_ON(err))
				return err;
			if (be16_to_cpu(fqd.fq_ctrl) & QM_FQCTRL_CGE &&
			    fqd.cgid == cgrid) {
				pr_err("CRGID 0x%x is being used by FQID 0x%x, CGR will be leaked\n",
				       cgrid, fq.fqid);
				return -EIO;
			}
		}
		/* Move to the next FQID */
		fq.fqid++;
	} while (1);
}

int qman_release_cgrid(u32 cgrid)
{
	int ret;

	ret = cgr_cleanup(cgrid);
	if (ret) {
		pr_debug("CGRID %d leaked\n", cgrid);
		return ret;
	}

	gen_pool_free(qm_cgralloc, cgrid | DPAA_GENALLOC_OFF, 1);
	return 0;
}
EXPORT_SYMBOL(qman_release_cgrid);
