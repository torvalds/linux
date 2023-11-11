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

#include "dpaa_sys.h"

#include <soc/fsl/qman.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>

#if defined(CONFIG_FSL_PAMU)
#include <asm/fsl_pamu_stash.h>
#endif

struct qm_mcr_querywq {
	u8 verb;
	u8 result;
	u16 channel_wq; /* ignores wq (3 lsbits): _res[0-2] */
	u8 __reserved[28];
	u32 wq_len[8];
} __packed;

static inline u16 qm_mcr_querywq_get_chan(const struct qm_mcr_querywq *wq)
{
	return wq->channel_wq >> 3;
}

struct __qm_mcr_querycongestion {
	u32 state[8];
};

/* "Query Congestion Group State" */
struct qm_mcr_querycongestion {
	u8 verb;
	u8 result;
	u8 __reserved[30];
	/* Access this struct using qman_cgrs_get() */
	struct __qm_mcr_querycongestion state;
} __packed;

/* "Query CGR" */
struct qm_mcr_querycgr {
	u8 verb;
	u8 result;
	u16 __reserved1;
	struct __qm_mc_cgr cgr; /* CGR fields */
	u8 __reserved2[6];
	u8 i_bcnt_hi;	/* high 8-bits of 40-bit "Instant" */
	__be32 i_bcnt_lo;	/* low 32-bits of 40-bit */
	u8 __reserved3[3];
	u8 a_bcnt_hi;	/* high 8-bits of 40-bit "Average" */
	__be32 a_bcnt_lo;	/* low 32-bits of 40-bit */
	__be32 cscn_targ_swp[4];
} __packed;

static inline u64 qm_mcr_querycgr_i_get64(const struct qm_mcr_querycgr *q)
{
	return ((u64)q->i_bcnt_hi << 32) | be32_to_cpu(q->i_bcnt_lo);
}
static inline u64 qm_mcr_querycgr_a_get64(const struct qm_mcr_querycgr *q)
{
	return ((u64)q->a_bcnt_hi << 32) | be32_to_cpu(q->a_bcnt_lo);
}

/* Congestion Groups */

/*
 * This wrapper represents a bit-array for the state of the 256 QMan congestion
 * groups. Is also used as a *mask* for congestion groups, eg. so we ignore
 * those that don't concern us. We harness the structure and accessor details
 * already used in the management command to query congestion groups.
 */
#define CGR_BITS_PER_WORD 5
#define CGR_WORD(x)	((x) >> CGR_BITS_PER_WORD)
#define CGR_BIT(x)	(BIT(31) >> ((x) & 0x1f))
#define CGR_NUM	(sizeof(struct __qm_mcr_querycongestion) << 3)

struct qman_cgrs {
	struct __qm_mcr_querycongestion q;
};

static inline void qman_cgrs_init(struct qman_cgrs *c)
{
	memset(c, 0, sizeof(*c));
}

static inline void qman_cgrs_fill(struct qman_cgrs *c)
{
	memset(c, 0xff, sizeof(*c));
}

static inline int qman_cgrs_get(struct qman_cgrs *c, u8 cgr)
{
	return c->q.state[CGR_WORD(cgr)] & CGR_BIT(cgr);
}

static inline void qman_cgrs_cp(struct qman_cgrs *dest,
				const struct qman_cgrs *src)
{
	*dest = *src;
}

static inline void qman_cgrs_and(struct qman_cgrs *dest,
			const struct qman_cgrs *a, const struct qman_cgrs *b)
{
	int ret;
	u32 *_d = dest->q.state;
	const u32 *_a = a->q.state;
	const u32 *_b = b->q.state;

	for (ret = 0; ret < 8; ret++)
		*_d++ = *_a++ & *_b++;
}

static inline void qman_cgrs_xor(struct qman_cgrs *dest,
			const struct qman_cgrs *a, const struct qman_cgrs *b)
{
	int ret;
	u32 *_d = dest->q.state;
	const u32 *_a = a->q.state;
	const u32 *_b = b->q.state;

	for (ret = 0; ret < 8; ret++)
		*_d++ = *_a++ ^ *_b++;
}

void qman_init_cgr_all(void);

struct qm_portal_config {
	/* Portal addresses */
	void *addr_virt_ce;
	void __iomem *addr_virt_ci;
	struct device *dev;
	struct iommu_domain *iommu_domain;
	/* Allow these to be joined in lists */
	struct list_head list;
	/* User-visible portal configuration settings */
	/* portal is affined to this cpu */
	int cpu;
	/* portal interrupt line */
	int irq;
	/*
	 * the portal's dedicated channel id, used initialising
	 * frame queues to target this portal when scheduled
	 */
	u16 channel;
	/*
	 * mask of pool channels this portal has dequeue access to
	 * (using QM_SDQCR_CHANNELS_POOL(n) for the bitmask)
	 */
	u32 pools;
};

/* Revision info (for errata and feature handling) */
#define QMAN_REV11 0x0101
#define QMAN_REV12 0x0102
#define QMAN_REV20 0x0200
#define QMAN_REV30 0x0300
#define QMAN_REV31 0x0301
#define QMAN_REV32 0x0302
extern u16 qman_ip_rev; /* 0 if uninitialised, otherwise QMAN_REVx */

#define QM_FQID_RANGE_START 1 /* FQID 0 reserved for internal use */
extern struct gen_pool *qm_fqalloc; /* FQID allocator */
extern struct gen_pool *qm_qpalloc; /* pool-channel allocator */
extern struct gen_pool *qm_cgralloc; /* CGR ID allocator */
u32 qm_get_pools_sdqcr(void);

int qman_wq_alloc(void);
#ifdef CONFIG_FSL_PAMU
#define qman_liodn_fixup __qman_liodn_fixup
#else
static inline void qman_liodn_fixup(u16 channel)
{
}
#endif
void __qman_liodn_fixup(u16 channel);
void qman_set_sdest(u16 channel, unsigned int cpu_idx);

struct qman_portal *qman_create_affine_portal(
			const struct qm_portal_config *config,
			const struct qman_cgrs *cgrs);
const struct qm_portal_config *qman_destroy_affine_portal(void);

/*
 * qman_query_fq - Queries FQD fields (via h/w query command)
 * @fq: the frame queue object to be queried
 * @fqd: storage for the queried FQD fields
 */
int qman_query_fq(struct qman_fq *fq, struct qm_fqd *fqd);

int qman_alloc_fq_table(u32 num_fqids);

/*   QMan s/w corenet portal, low-level i/face	 */

/*
 * For qm_dqrr_sdqcr_set(); Choose one SOURCE. Choose one COUNT. Choose one
 * dequeue TYPE. Choose TOKEN (8-bit).
 * If SOURCE == CHANNELS,
 *   Choose CHANNELS_DEDICATED and/or CHANNELS_POOL(n).
 *   You can choose DEDICATED_PRECEDENCE if the portal channel should have
 *   priority.
 * If SOURCE == SPECIFICWQ,
 *     Either select the work-queue ID with SPECIFICWQ_WQ(), or select the
 *     channel (SPECIFICWQ_DEDICATED or SPECIFICWQ_POOL()) and specify the
 *     work-queue priority (0-7) with SPECIFICWQ_WQ() - either way, you get the
 *     same value.
 */
#define QM_SDQCR_SOURCE_CHANNELS	0x0
#define QM_SDQCR_SOURCE_SPECIFICWQ	0x40000000
#define QM_SDQCR_COUNT_EXACT1		0x0
#define QM_SDQCR_COUNT_UPTO3		0x20000000
#define QM_SDQCR_DEDICATED_PRECEDENCE	0x10000000
#define QM_SDQCR_TYPE_MASK		0x03000000
#define QM_SDQCR_TYPE_NULL		0x0
#define QM_SDQCR_TYPE_PRIO_QOS		0x01000000
#define QM_SDQCR_TYPE_ACTIVE_QOS	0x02000000
#define QM_SDQCR_TYPE_ACTIVE		0x03000000
#define QM_SDQCR_TOKEN_MASK		0x00ff0000
#define QM_SDQCR_TOKEN_SET(v)		(((v) & 0xff) << 16)
#define QM_SDQCR_TOKEN_GET(v)		(((v) >> 16) & 0xff)
#define QM_SDQCR_CHANNELS_DEDICATED	0x00008000
#define QM_SDQCR_SPECIFICWQ_MASK	0x000000f7
#define QM_SDQCR_SPECIFICWQ_DEDICATED	0x00000000
#define QM_SDQCR_SPECIFICWQ_POOL(n)	((n) << 4)
#define QM_SDQCR_SPECIFICWQ_WQ(n)	(n)

/* For qm_dqrr_vdqcr_set(): use FQID(n) to fill in the frame queue ID */
#define QM_VDQCR_FQID_MASK		0x00ffffff
#define QM_VDQCR_FQID(n)		((n) & QM_VDQCR_FQID_MASK)

/*
 * Used by all portal interrupt registers except 'inhibit'
 * Channels with frame availability
 */
#define QM_PIRQ_DQAVAIL	0x0000ffff

/* The DQAVAIL interrupt fields break down into these bits; */
#define QM_DQAVAIL_PORTAL	0x8000		/* Portal channel */
#define QM_DQAVAIL_POOL(n)	(0x8000 >> (n))	/* Pool channel, n==[1..15] */
#define QM_DQAVAIL_MASK		0xffff
/* This mask contains all the "irqsource" bits visible to API users */
#define QM_PIRQ_VISIBLE	(QM_PIRQ_SLOW | QM_PIRQ_DQRI)

extern struct qman_portal *affine_portals[NR_CPUS];
extern struct qman_portal *qman_dma_portal;
const struct qm_portal_config *qman_get_qm_portal_config(
						struct qman_portal *portal);

unsigned int qm_get_fqid_maxcnt(void);

int qman_shutdown_fq(u32 fqid);

int qman_requires_cleanup(void);
void qman_done_cleanup(void);
void qman_enable_irqs(void);
