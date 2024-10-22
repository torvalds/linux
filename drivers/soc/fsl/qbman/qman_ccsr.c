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

u16 qman_ip_rev;
EXPORT_SYMBOL(qman_ip_rev);
u16 qm_channel_pool1 = QMAN_CHANNEL_POOL1;
EXPORT_SYMBOL(qm_channel_pool1);
u16 qm_channel_caam = QMAN_CHANNEL_CAAM;
EXPORT_SYMBOL(qm_channel_caam);

/* Register offsets */
#define REG_QCSP_LIO_CFG(n)	(0x0000 + ((n) * 0x10))
#define REG_QCSP_IO_CFG(n)	(0x0004 + ((n) * 0x10))
#define REG_QCSP_DD_CFG(n)	(0x000c + ((n) * 0x10))
#define REG_DD_CFG		0x0200
#define REG_DCP_CFG(n)		(0x0300 + ((n) * 0x10))
#define REG_DCP_DD_CFG(n)	(0x0304 + ((n) * 0x10))
#define REG_DCP_DLM_AVG(n)	(0x030c + ((n) * 0x10))
#define REG_PFDR_FPC		0x0400
#define REG_PFDR_FP_HEAD	0x0404
#define REG_PFDR_FP_TAIL	0x0408
#define REG_PFDR_FP_LWIT	0x0410
#define REG_PFDR_CFG		0x0414
#define REG_SFDR_CFG		0x0500
#define REG_SFDR_IN_USE		0x0504
#define REG_WQ_CS_CFG(n)	(0x0600 + ((n) * 0x04))
#define REG_WQ_DEF_ENC_WQID	0x0630
#define REG_WQ_SC_DD_CFG(n)	(0x640 + ((n) * 0x04))
#define REG_WQ_PC_DD_CFG(n)	(0x680 + ((n) * 0x04))
#define REG_WQ_DC0_DD_CFG(n)	(0x6c0 + ((n) * 0x04))
#define REG_WQ_DC1_DD_CFG(n)	(0x700 + ((n) * 0x04))
#define REG_WQ_DCn_DD_CFG(n)	(0x6c0 + ((n) * 0x40)) /* n=2,3 */
#define REG_CM_CFG		0x0800
#define REG_ECSR		0x0a00
#define REG_ECIR		0x0a04
#define REG_EADR		0x0a08
#define REG_ECIR2		0x0a0c
#define REG_EDATA(n)		(0x0a10 + ((n) * 0x04))
#define REG_SBEC(n)		(0x0a80 + ((n) * 0x04))
#define REG_MCR			0x0b00
#define REG_MCP(n)		(0x0b04 + ((n) * 0x04))
#define REG_MISC_CFG		0x0be0
#define REG_HID_CFG		0x0bf0
#define REG_IDLE_STAT		0x0bf4
#define REG_IP_REV_1		0x0bf8
#define REG_IP_REV_2		0x0bfc
#define REG_FQD_BARE		0x0c00
#define REG_PFDR_BARE		0x0c20
#define REG_offset_BAR		0x0004	/* relative to REG_[FQD|PFDR]_BARE */
#define REG_offset_AR		0x0010	/* relative to REG_[FQD|PFDR]_BARE */
#define REG_QCSP_BARE		0x0c80
#define REG_QCSP_BAR		0x0c84
#define REG_CI_SCHED_CFG	0x0d00
#define REG_SRCIDR		0x0d04
#define REG_LIODNR		0x0d08
#define REG_CI_RLM_AVG		0x0d14
#define REG_ERR_ISR		0x0e00
#define REG_ERR_IER		0x0e04
#define REG_REV3_QCSP_LIO_CFG(n)	(0x1000 + ((n) * 0x10))
#define REG_REV3_QCSP_IO_CFG(n)	(0x1004 + ((n) * 0x10))
#define REG_REV3_QCSP_DD_CFG(n)	(0x100c + ((n) * 0x10))

/* Assists for QMAN_MCR */
#define MCR_INIT_PFDR		0x01000000
#define MCR_get_rslt(v)		(u8)((v) >> 24)
#define MCR_rslt_idle(r)	(!(r) || ((r) >= 0xf0))
#define MCR_rslt_ok(r)		((r) == 0xf0)
#define MCR_rslt_eaccess(r)	((r) == 0xf8)
#define MCR_rslt_inval(r)	((r) == 0xff)

/*
 * Corenet initiator settings. Stash request queues are 4-deep to match cores
 * ability to snarf. Stash priority is 3, other priorities are 2.
 */
#define QM_CI_SCHED_CFG_SRCCIV		4
#define QM_CI_SCHED_CFG_SRQ_W		3
#define QM_CI_SCHED_CFG_RW_W		2
#define QM_CI_SCHED_CFG_BMAN_W		2
/* write SRCCIV enable */
#define QM_CI_SCHED_CFG_SRCCIV_EN	BIT(31)

/* Follows WQ_CS_CFG0-5 */
enum qm_wq_class {
	qm_wq_portal = 0,
	qm_wq_pool = 1,
	qm_wq_fman0 = 2,
	qm_wq_fman1 = 3,
	qm_wq_caam = 4,
	qm_wq_pme = 5,
	qm_wq_first = qm_wq_portal,
	qm_wq_last = qm_wq_pme
};

/* Follows FQD_[BARE|BAR|AR] and PFDR_[BARE|BAR|AR] */
enum qm_memory {
	qm_memory_fqd,
	qm_memory_pfdr
};

/* Used by all error interrupt registers except 'inhibit' */
#define QM_EIRQ_CIDE	0x20000000	/* Corenet Initiator Data Error */
#define QM_EIRQ_CTDE	0x10000000	/* Corenet Target Data Error */
#define QM_EIRQ_CITT	0x08000000	/* Corenet Invalid Target Transaction */
#define QM_EIRQ_PLWI	0x04000000	/* PFDR Low Watermark */
#define QM_EIRQ_MBEI	0x02000000	/* Multi-bit ECC Error */
#define QM_EIRQ_SBEI	0x01000000	/* Single-bit ECC Error */
#define QM_EIRQ_PEBI	0x00800000	/* PFDR Enqueues Blocked Interrupt */
#define QM_EIRQ_IFSI	0x00020000	/* Invalid FQ Flow Control State */
#define QM_EIRQ_ICVI	0x00010000	/* Invalid Command Verb */
#define QM_EIRQ_IDDI	0x00000800	/* Invalid Dequeue (Direct-connect) */
#define QM_EIRQ_IDFI	0x00000400	/* Invalid Dequeue FQ */
#define QM_EIRQ_IDSI	0x00000200	/* Invalid Dequeue Source */
#define QM_EIRQ_IDQI	0x00000100	/* Invalid Dequeue Queue */
#define QM_EIRQ_IECE	0x00000010	/* Invalid Enqueue Configuration */
#define QM_EIRQ_IEOI	0x00000008	/* Invalid Enqueue Overflow */
#define QM_EIRQ_IESI	0x00000004	/* Invalid Enqueue State */
#define QM_EIRQ_IECI	0x00000002	/* Invalid Enqueue Channel */
#define QM_EIRQ_IEQI	0x00000001	/* Invalid Enqueue Queue */

/* QMAN_ECIR valid error bit */
#define PORTAL_ECSR_ERR	(QM_EIRQ_IEQI | QM_EIRQ_IESI | QM_EIRQ_IEOI | \
			 QM_EIRQ_IDQI | QM_EIRQ_IDSI | QM_EIRQ_IDFI | \
			 QM_EIRQ_IDDI | QM_EIRQ_ICVI | QM_EIRQ_IFSI)
#define FQID_ECSR_ERR	(QM_EIRQ_IEQI | QM_EIRQ_IECI | QM_EIRQ_IESI | \
			 QM_EIRQ_IEOI | QM_EIRQ_IDQI | QM_EIRQ_IDFI | \
			 QM_EIRQ_IFSI)

struct qm_ecir {
	u32 info; /* res[30-31], ptyp[29], pnum[24-28], fqid[0-23] */
};

static bool qm_ecir_is_dcp(const struct qm_ecir *p)
{
	return p->info & BIT(29);
}

static int qm_ecir_get_pnum(const struct qm_ecir *p)
{
	return (p->info >> 24) & 0x1f;
}

static int qm_ecir_get_fqid(const struct qm_ecir *p)
{
	return p->info & (BIT(24) - 1);
}

struct qm_ecir2 {
	u32 info; /* ptyp[31], res[10-30], pnum[0-9] */
};

static bool qm_ecir2_is_dcp(const struct qm_ecir2 *p)
{
	return p->info & BIT(31);
}

static int qm_ecir2_get_pnum(const struct qm_ecir2 *p)
{
	return p->info & (BIT(10) - 1);
}

struct qm_eadr {
	u32 info; /* memid[24-27], eadr[0-11] */
		  /* v3: memid[24-28], eadr[0-15] */
};

static int qm_eadr_get_memid(const struct qm_eadr *p)
{
	return (p->info >> 24) & 0xf;
}

static int qm_eadr_get_eadr(const struct qm_eadr *p)
{
	return p->info & (BIT(12) - 1);
}

static int qm_eadr_v3_get_memid(const struct qm_eadr *p)
{
	return (p->info >> 24) & 0x1f;
}

static int qm_eadr_v3_get_eadr(const struct qm_eadr *p)
{
	return p->info & (BIT(16) - 1);
}

struct qman_hwerr_txt {
	u32 mask;
	const char *txt;
};


static const struct qman_hwerr_txt qman_hwerr_txts[] = {
	{ QM_EIRQ_CIDE, "Corenet Initiator Data Error" },
	{ QM_EIRQ_CTDE, "Corenet Target Data Error" },
	{ QM_EIRQ_CITT, "Corenet Invalid Target Transaction" },
	{ QM_EIRQ_PLWI, "PFDR Low Watermark" },
	{ QM_EIRQ_MBEI, "Multi-bit ECC Error" },
	{ QM_EIRQ_SBEI, "Single-bit ECC Error" },
	{ QM_EIRQ_PEBI, "PFDR Enqueues Blocked Interrupt" },
	{ QM_EIRQ_ICVI, "Invalid Command Verb" },
	{ QM_EIRQ_IFSI, "Invalid Flow Control State" },
	{ QM_EIRQ_IDDI, "Invalid Dequeue (Direct-connect)" },
	{ QM_EIRQ_IDFI, "Invalid Dequeue FQ" },
	{ QM_EIRQ_IDSI, "Invalid Dequeue Source" },
	{ QM_EIRQ_IDQI, "Invalid Dequeue Queue" },
	{ QM_EIRQ_IECE, "Invalid Enqueue Configuration" },
	{ QM_EIRQ_IEOI, "Invalid Enqueue Overflow" },
	{ QM_EIRQ_IESI, "Invalid Enqueue State" },
	{ QM_EIRQ_IECI, "Invalid Enqueue Channel" },
	{ QM_EIRQ_IEQI, "Invalid Enqueue Queue" },
};

struct qman_error_info_mdata {
	u16 addr_mask;
	u16 bits;
	const char *txt;
};

static const struct qman_error_info_mdata error_mdata[] = {
	{ 0x01FF, 24, "FQD cache tag memory 0" },
	{ 0x01FF, 24, "FQD cache tag memory 1" },
	{ 0x01FF, 24, "FQD cache tag memory 2" },
	{ 0x01FF, 24, "FQD cache tag memory 3" },
	{ 0x0FFF, 512, "FQD cache memory" },
	{ 0x07FF, 128, "SFDR memory" },
	{ 0x01FF, 72, "WQ context memory" },
	{ 0x00FF, 240, "CGR memory" },
	{ 0x00FF, 302, "Internal Order Restoration List memory" },
	{ 0x01FF, 256, "SW portal ring memory" },
};

#define QMAN_ERRS_TO_DISABLE (QM_EIRQ_PLWI | QM_EIRQ_PEBI)

/*
 * TODO: unimplemented registers
 *
 * Keeping a list here of QMan registers I have not yet covered;
 * QCSP_DD_IHRSR, QCSP_DD_IHRFR, QCSP_DD_HASR,
 * DCP_DD_IHRSR, DCP_DD_IHRFR, DCP_DD_HASR, CM_CFG,
 * QMAN_EECC, QMAN_SBET, QMAN_EINJ, QMAN_SBEC0-12
 */

/* Pointer to the start of the QMan's CCSR space */
static u32 __iomem *qm_ccsr_start;
/* A SDQCR mask comprising all the available/visible pool channels */
static u32 qm_pools_sdqcr;
static int __qman_probed;
static int  __qman_requires_cleanup;

static inline u32 qm_ccsr_in(u32 offset)
{
	return ioread32be(qm_ccsr_start + offset/4);
}

static inline void qm_ccsr_out(u32 offset, u32 val)
{
	iowrite32be(val, qm_ccsr_start + offset/4);
}

u32 qm_get_pools_sdqcr(void)
{
	return qm_pools_sdqcr;
}

enum qm_dc_portal {
	qm_dc_portal_fman0 = 0,
	qm_dc_portal_fman1 = 1
};

static void qm_set_dc(enum qm_dc_portal portal, int ed, u8 sernd)
{
	DPAA_ASSERT(!ed || portal == qm_dc_portal_fman0 ||
		    portal == qm_dc_portal_fman1);
	if ((qman_ip_rev & 0xFF00) >= QMAN_REV30)
		qm_ccsr_out(REG_DCP_CFG(portal),
			    (ed ? 0x1000 : 0) | (sernd & 0x3ff));
	else
		qm_ccsr_out(REG_DCP_CFG(portal),
			    (ed ? 0x100 : 0) | (sernd & 0x1f));
}

static void qm_set_wq_scheduling(enum qm_wq_class wq_class,
				 u8 cs_elev, u8 csw2, u8 csw3, u8 csw4,
				 u8 csw5, u8 csw6, u8 csw7)
{
	qm_ccsr_out(REG_WQ_CS_CFG(wq_class), ((cs_elev & 0xff) << 24) |
		    ((csw2 & 0x7) << 20) | ((csw3 & 0x7) << 16) |
		    ((csw4 & 0x7) << 12) | ((csw5 & 0x7) << 8) |
		    ((csw6 & 0x7) << 4) | (csw7 & 0x7));
}

static void qm_set_hid(void)
{
	qm_ccsr_out(REG_HID_CFG, 0);
}

static void qm_set_corenet_initiator(void)
{
	qm_ccsr_out(REG_CI_SCHED_CFG, QM_CI_SCHED_CFG_SRCCIV_EN |
		    (QM_CI_SCHED_CFG_SRCCIV << 24) |
		    (QM_CI_SCHED_CFG_SRQ_W << 8) |
		    (QM_CI_SCHED_CFG_RW_W << 4) |
		    QM_CI_SCHED_CFG_BMAN_W);
}

static void qm_get_version(u16 *id, u8 *major, u8 *minor)
{
	u32 v = qm_ccsr_in(REG_IP_REV_1);
	*id = (v >> 16);
	*major = (v >> 8) & 0xff;
	*minor = v & 0xff;
}

#define PFDR_AR_EN		BIT(31)
static int qm_set_memory(enum qm_memory memory, u64 ba, u32 size)
{
	void *ptr;
	u32 offset = (memory == qm_memory_fqd) ? REG_FQD_BARE : REG_PFDR_BARE;
	u32 exp = ilog2(size);
	u32 bar, bare;

	/* choke if size isn't within range */
	DPAA_ASSERT((size >= 4096) && (size <= 1024*1024*1024) &&
		    is_power_of_2(size));
	/* choke if 'ba' has lower-alignment than 'size' */
	DPAA_ASSERT(!(ba & (size - 1)));

	/* Check to see if QMan has already been initialized */
	bar = qm_ccsr_in(offset + REG_offset_BAR);
	if (bar) {
		/* Maker sure ba == what was programmed) */
		bare = qm_ccsr_in(offset);
		if (bare != upper_32_bits(ba) || bar != lower_32_bits(ba)) {
			pr_err("Attempted to reinitialize QMan with different BAR, got 0x%llx read BARE=0x%x BAR=0x%x\n",
			       ba, bare, bar);
			return -ENOMEM;
		}
		__qman_requires_cleanup = 1;
		/* Return 1 to indicate memory was previously programmed */
		return 1;
	}
	/* Need to temporarily map the area to make sure it is zeroed */
	ptr = memremap(ba, size, MEMREMAP_WB);
	if (!ptr) {
		pr_crit("memremap() of QMan private memory failed\n");
		return -ENOMEM;
	}
	memset(ptr, 0, size);

#ifdef CONFIG_PPC
	/*
	 * PPC doesn't appear to flush the cache on memunmap() but the
	 * cache must be flushed since QMan does non coherent accesses
	 * to this memory
	 */
	flush_dcache_range((unsigned long) ptr, (unsigned long) ptr+size);
#endif
	memunmap(ptr);

	qm_ccsr_out(offset, upper_32_bits(ba));
	qm_ccsr_out(offset + REG_offset_BAR, lower_32_bits(ba));
	qm_ccsr_out(offset + REG_offset_AR, PFDR_AR_EN | (exp - 1));
	return 0;
}

static void qm_set_pfdr_threshold(u32 th, u8 k)
{
	qm_ccsr_out(REG_PFDR_FP_LWIT, th & 0xffffff);
	qm_ccsr_out(REG_PFDR_CFG, k);
}

static void qm_set_sfdr_threshold(u16 th)
{
	qm_ccsr_out(REG_SFDR_CFG, th & 0x3ff);
}

static int qm_init_pfdr(struct device *dev, u32 pfdr_start, u32 num)
{
	u8 rslt = MCR_get_rslt(qm_ccsr_in(REG_MCR));

	DPAA_ASSERT(pfdr_start && !(pfdr_start & 7) && !(num & 7) && num);
	/* Make sure the command interface is 'idle' */
	if (!MCR_rslt_idle(rslt)) {
		dev_crit(dev, "QMAN_MCR isn't idle");
		WARN_ON(1);
	}

	/* Write the MCR command params then the verb */
	qm_ccsr_out(REG_MCP(0), pfdr_start);
	/*
	 * TODO: remove this - it's a workaround for a model bug that is
	 * corrected in more recent versions. We use the workaround until
	 * everyone has upgraded.
	 */
	qm_ccsr_out(REG_MCP(1), pfdr_start + num - 16);
	dma_wmb();
	qm_ccsr_out(REG_MCR, MCR_INIT_PFDR);
	/* Poll for the result */
	do {
		rslt = MCR_get_rslt(qm_ccsr_in(REG_MCR));
	} while (!MCR_rslt_idle(rslt));
	if (MCR_rslt_ok(rslt))
		return 0;
	if (MCR_rslt_eaccess(rslt))
		return -EACCES;
	if (MCR_rslt_inval(rslt))
		return -EINVAL;
	dev_crit(dev, "Unexpected result from MCR_INIT_PFDR: %02x\n", rslt);
	return -ENODEV;
}

/*
 * QMan needs two global memory areas initialized at boot time:
 *  1) FQD: Frame Queue Descriptors used to manage frame queues
 *  2) PFDR: Packed Frame Queue Descriptor Records used to store frames
 * Both areas are reserved using the device tree reserved memory framework
 * and the addresses and sizes are initialized when the QMan device is probed
 */
static dma_addr_t fqd_a, pfdr_a;
static size_t fqd_sz, pfdr_sz;

#ifdef CONFIG_PPC
/*
 * Support for PPC Device Tree backward compatibility when compatible
 * string is set to fsl-qman-fqd and fsl-qman-pfdr
 */
static int zero_priv_mem(phys_addr_t addr, size_t sz)
{
	/* map as cacheable, non-guarded */
	void __iomem *tmpp = ioremap_cache(addr, sz);

	if (!tmpp)
		return -ENOMEM;

	memset_io(tmpp, 0, sz);
	flush_dcache_range((unsigned long)tmpp,
			   (unsigned long)tmpp + sz);
	iounmap(tmpp);

	return 0;
}
#endif

unsigned int qm_get_fqid_maxcnt(void)
{
	return fqd_sz / 64;
}

static void log_edata_bits(struct device *dev, u32 bit_count)
{
	u32 i, j, mask = 0xffffffff;

	dev_warn(dev, "ErrInt, EDATA:\n");
	i = bit_count / 32;
	if (bit_count % 32) {
		i++;
		mask = ~(mask << bit_count % 32);
	}
	j = 16 - i;
	dev_warn(dev, "  0x%08x\n", qm_ccsr_in(REG_EDATA(j)) & mask);
	j++;
	for (; j < 16; j++)
		dev_warn(dev, "  0x%08x\n", qm_ccsr_in(REG_EDATA(j)));
}

static void log_additional_error_info(struct device *dev, u32 isr_val,
				      u32 ecsr_val)
{
	struct qm_ecir ecir_val;
	struct qm_eadr eadr_val;
	int memid;

	ecir_val.info = qm_ccsr_in(REG_ECIR);
	/* Is portal info valid */
	if ((qman_ip_rev & 0xFF00) >= QMAN_REV30) {
		struct qm_ecir2 ecir2_val;

		ecir2_val.info = qm_ccsr_in(REG_ECIR2);
		if (ecsr_val & PORTAL_ECSR_ERR) {
			dev_warn(dev, "ErrInt: %s id %d\n",
				 qm_ecir2_is_dcp(&ecir2_val) ? "DCP" : "SWP",
				 qm_ecir2_get_pnum(&ecir2_val));
		}
		if (ecsr_val & (FQID_ECSR_ERR | QM_EIRQ_IECE))
			dev_warn(dev, "ErrInt: ecir.fqid 0x%x\n",
				 qm_ecir_get_fqid(&ecir_val));

		if (ecsr_val & (QM_EIRQ_SBEI|QM_EIRQ_MBEI)) {
			eadr_val.info = qm_ccsr_in(REG_EADR);
			memid = qm_eadr_v3_get_memid(&eadr_val);
			dev_warn(dev, "ErrInt: EADR Memory: %s, 0x%x\n",
				 error_mdata[memid].txt,
				 error_mdata[memid].addr_mask
					& qm_eadr_v3_get_eadr(&eadr_val));
			log_edata_bits(dev, error_mdata[memid].bits);
		}
	} else {
		if (ecsr_val & PORTAL_ECSR_ERR) {
			dev_warn(dev, "ErrInt: %s id %d\n",
				 qm_ecir_is_dcp(&ecir_val) ? "DCP" : "SWP",
				 qm_ecir_get_pnum(&ecir_val));
		}
		if (ecsr_val & FQID_ECSR_ERR)
			dev_warn(dev, "ErrInt: ecir.fqid 0x%x\n",
				 qm_ecir_get_fqid(&ecir_val));

		if (ecsr_val & (QM_EIRQ_SBEI|QM_EIRQ_MBEI)) {
			eadr_val.info = qm_ccsr_in(REG_EADR);
			memid = qm_eadr_get_memid(&eadr_val);
			dev_warn(dev, "ErrInt: EADR Memory: %s, 0x%x\n",
				 error_mdata[memid].txt,
				 error_mdata[memid].addr_mask
					& qm_eadr_get_eadr(&eadr_val));
			log_edata_bits(dev, error_mdata[memid].bits);
		}
	}
}

static irqreturn_t qman_isr(int irq, void *ptr)
{
	u32 isr_val, ier_val, ecsr_val, isr_mask, i;
	struct device *dev = ptr;

	ier_val = qm_ccsr_in(REG_ERR_IER);
	isr_val = qm_ccsr_in(REG_ERR_ISR);
	ecsr_val = qm_ccsr_in(REG_ECSR);
	isr_mask = isr_val & ier_val;

	if (!isr_mask)
		return IRQ_NONE;

	for (i = 0; i < ARRAY_SIZE(qman_hwerr_txts); i++) {
		if (qman_hwerr_txts[i].mask & isr_mask) {
			dev_err_ratelimited(dev, "ErrInt: %s\n",
					    qman_hwerr_txts[i].txt);
			if (qman_hwerr_txts[i].mask & ecsr_val) {
				log_additional_error_info(dev, isr_mask,
							  ecsr_val);
				/* Re-arm error capture registers */
				qm_ccsr_out(REG_ECSR, ecsr_val);
			}
			if (qman_hwerr_txts[i].mask & QMAN_ERRS_TO_DISABLE) {
				dev_dbg(dev, "Disabling error 0x%x\n",
					qman_hwerr_txts[i].mask);
				ier_val &= ~qman_hwerr_txts[i].mask;
				qm_ccsr_out(REG_ERR_IER, ier_val);
			}
		}
	}
	qm_ccsr_out(REG_ERR_ISR, isr_val);

	return IRQ_HANDLED;
}

static int qman_init_ccsr(struct device *dev)
{
	int i, err;

	/* FQD memory */
	err = qm_set_memory(qm_memory_fqd, fqd_a, fqd_sz);
	if (err < 0)
		return err;
	/* PFDR memory */
	err = qm_set_memory(qm_memory_pfdr, pfdr_a, pfdr_sz);
	if (err < 0)
		return err;
	/* Only initialize PFDRs if the QMan was not initialized before */
	if (err == 0) {
		err = qm_init_pfdr(dev, 8, pfdr_sz / 64 - 8);
		if (err)
			return err;
	}
	/* thresholds */
	qm_set_pfdr_threshold(512, 64);
	qm_set_sfdr_threshold(128);
	/* clear stale PEBI bit from interrupt status register */
	qm_ccsr_out(REG_ERR_ISR, QM_EIRQ_PEBI);
	/* corenet initiator settings */
	qm_set_corenet_initiator();
	/* HID settings */
	qm_set_hid();
	/* Set scheduling weights to defaults */
	for (i = qm_wq_first; i <= qm_wq_last; i++)
		qm_set_wq_scheduling(i, 0, 0, 0, 0, 0, 0, 0);
	/* We are not prepared to accept ERNs for hardware enqueues */
	qm_set_dc(qm_dc_portal_fman0, 1, 0);
	qm_set_dc(qm_dc_portal_fman1, 1, 0);
	return 0;
}

#define LIO_CFG_LIODN_MASK 0x0fff0000
void __qman_liodn_fixup(u16 channel)
{
	static int done;
	static u32 liodn_offset;
	u32 before, after;
	int idx = channel - QM_CHANNEL_SWPORTAL0;

	if ((qman_ip_rev & 0xFF00) >= QMAN_REV30)
		before = qm_ccsr_in(REG_REV3_QCSP_LIO_CFG(idx));
	else
		before = qm_ccsr_in(REG_QCSP_LIO_CFG(idx));
	if (!done) {
		liodn_offset = before & LIO_CFG_LIODN_MASK;
		done = 1;
		return;
	}
	after = (before & (~LIO_CFG_LIODN_MASK)) | liodn_offset;
	if ((qman_ip_rev & 0xFF00) >= QMAN_REV30)
		qm_ccsr_out(REG_REV3_QCSP_LIO_CFG(idx), after);
	else
		qm_ccsr_out(REG_QCSP_LIO_CFG(idx), after);
}

#define IO_CFG_SDEST_MASK 0x00ff0000
void qman_set_sdest(u16 channel, unsigned int cpu_idx)
{
	int idx = channel - QM_CHANNEL_SWPORTAL0;
	u32 before, after;

	if ((qman_ip_rev & 0xFF00) >= QMAN_REV30) {
		before = qm_ccsr_in(REG_REV3_QCSP_IO_CFG(idx));
		/* Each pair of vcpu share the same SRQ(SDEST) */
		cpu_idx /= 2;
		after = (before & (~IO_CFG_SDEST_MASK)) | (cpu_idx << 16);
		qm_ccsr_out(REG_REV3_QCSP_IO_CFG(idx), after);
	} else {
		before = qm_ccsr_in(REG_QCSP_IO_CFG(idx));
		after = (before & (~IO_CFG_SDEST_MASK)) | (cpu_idx << 16);
		qm_ccsr_out(REG_QCSP_IO_CFG(idx), after);
	}
}

static int qman_resource_init(struct device *dev)
{
	int pool_chan_num, cgrid_num;
	int ret, i;

	switch (qman_ip_rev >> 8) {
	case 1:
		pool_chan_num = 15;
		cgrid_num = 256;
		break;
	case 2:
		pool_chan_num = 3;
		cgrid_num = 64;
		break;
	case 3:
		pool_chan_num = 15;
		cgrid_num = 256;
		break;
	default:
		return -ENODEV;
	}

	ret = gen_pool_add(qm_qpalloc, qm_channel_pool1 | DPAA_GENALLOC_OFF,
			   pool_chan_num, -1);
	if (ret) {
		dev_err(dev, "Failed to seed pool channels (%d)\n", ret);
		return ret;
	}

	ret = gen_pool_add(qm_cgralloc, DPAA_GENALLOC_OFF, cgrid_num, -1);
	if (ret) {
		dev_err(dev, "Failed to seed CGRID range (%d)\n", ret);
		return ret;
	}

	/* parse pool channels into the SDQCR mask */
	for (i = 0; i < cgrid_num; i++)
		qm_pools_sdqcr |= QM_SDQCR_CHANNELS_POOL_CONV(i);

	ret = gen_pool_add(qm_fqalloc, QM_FQID_RANGE_START | DPAA_GENALLOC_OFF,
			   qm_get_fqid_maxcnt() - QM_FQID_RANGE_START, -1);
	if (ret) {
		dev_err(dev, "Failed to seed FQID range (%d)\n", ret);
		return ret;
	}

	return 0;
}

int qman_is_probed(void)
{
	return __qman_probed;
}
EXPORT_SYMBOL_GPL(qman_is_probed);

int qman_requires_cleanup(void)
{
	return __qman_requires_cleanup;
}

void qman_done_cleanup(void)
{
	qman_enable_irqs();
	__qman_requires_cleanup = 0;
}


static int fsl_qman_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct resource *res;
	int ret, err_irq;
	u16 id;
	u8 major, minor;

	__qman_probed = -1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Can't get %pOF property 'IORESOURCE_MEM'\n",
			node);
		return -ENXIO;
	}
	qm_ccsr_start = devm_ioremap(dev, res->start, resource_size(res));
	if (!qm_ccsr_start)
		return -ENXIO;

	qm_get_version(&id, &major, &minor);
	if (major == 1 && minor == 0) {
		dev_err(dev, "Rev1.0 on P4080 rev1 is not supported!\n");
			return -ENODEV;
	} else if (major == 1 && minor == 1)
		qman_ip_rev = QMAN_REV11;
	else if	(major == 1 && minor == 2)
		qman_ip_rev = QMAN_REV12;
	else if (major == 2 && minor == 0)
		qman_ip_rev = QMAN_REV20;
	else if (major == 3 && minor == 0)
		qman_ip_rev = QMAN_REV30;
	else if (major == 3 && minor == 1)
		qman_ip_rev = QMAN_REV31;
	else if (major == 3 && minor == 2)
		qman_ip_rev = QMAN_REV32;
	else {
		dev_err(dev, "Unknown QMan version\n");
		return -ENODEV;
	}

	if ((qman_ip_rev & 0xff00) >= QMAN_REV30) {
		qm_channel_pool1 = QMAN_CHANNEL_POOL1_REV3;
		qm_channel_caam = QMAN_CHANNEL_CAAM_REV3;
	}

	/*
	* Order of memory regions is assumed as FQD followed by PFDR
	* in order to ensure allocations from the correct regions the
	* driver initializes then allocates each piece in order
	*/
	ret = qbman_init_private_mem(dev, 0, "fsl,qman-fqd", &fqd_a, &fqd_sz);
	if (ret) {
		dev_err(dev, "qbman_init_private_mem() for FQD failed 0x%x\n",
			ret);
		return -ENODEV;
	}
#ifdef CONFIG_PPC
	/*
	 * For PPC backward DT compatibility
	 * FQD memory MUST be zero'd by software
	 */
	zero_priv_mem(fqd_a, fqd_sz);
#endif
	dev_dbg(dev, "Allocated FQD 0x%llx 0x%zx\n", fqd_a, fqd_sz);

	/* Setup PFDR memory */
	ret = qbman_init_private_mem(dev, 1, "fsl,qman-pfdr", &pfdr_a, &pfdr_sz);
	if (ret) {
		dev_err(dev, "qbman_init_private_mem() for PFDR failed 0x%x\n",
			ret);
		return -ENODEV;
	}
	dev_dbg(dev, "Allocated PFDR 0x%llx 0x%zx\n", pfdr_a, pfdr_sz);

	ret = qman_init_ccsr(dev);
	if (ret) {
		dev_err(dev, "CCSR setup failed\n");
		return ret;
	}

	err_irq = platform_get_irq(pdev, 0);
	if (err_irq <= 0) {
		dev_info(dev, "Can't get %pOF property 'interrupts'\n",
			 node);
		return -ENODEV;
	}
	ret = devm_request_irq(dev, err_irq, qman_isr, IRQF_SHARED, "qman-err",
			       dev);
	if (ret)  {
		dev_err(dev, "devm_request_irq() failed %d for '%pOF'\n",
			ret, node);
		return ret;
	}

	/*
	 * Write-to-clear any stale bits, (eg. starvation being asserted prior
	 * to resource allocation during driver init).
	 */
	qm_ccsr_out(REG_ERR_ISR, 0xffffffff);
	/* Enable Error Interrupts */
	qm_ccsr_out(REG_ERR_IER, 0xffffffff);

	qm_fqalloc = devm_gen_pool_create(dev, 0, -1, "qman-fqalloc");
	if (IS_ERR(qm_fqalloc)) {
		ret = PTR_ERR(qm_fqalloc);
		dev_err(dev, "qman-fqalloc pool init failed (%d)\n", ret);
		return ret;
	}

	qm_qpalloc = devm_gen_pool_create(dev, 0, -1, "qman-qpalloc");
	if (IS_ERR(qm_qpalloc)) {
		ret = PTR_ERR(qm_qpalloc);
		dev_err(dev, "qman-qpalloc pool init failed (%d)\n", ret);
		return ret;
	}

	qm_cgralloc = devm_gen_pool_create(dev, 0, -1, "qman-cgralloc");
	if (IS_ERR(qm_cgralloc)) {
		ret = PTR_ERR(qm_cgralloc);
		dev_err(dev, "qman-cgralloc pool init failed (%d)\n", ret);
		return ret;
	}

	ret = qman_resource_init(dev);
	if (ret)
		return ret;

	ret = qman_alloc_fq_table(qm_get_fqid_maxcnt());
	if (ret)
		return ret;

	ret = qman_wq_alloc();
	if (ret)
		return ret;

	__qman_probed = 1;

	return 0;
}

static const struct of_device_id fsl_qman_ids[] = {
	{
		.compatible = "fsl,qman",
	},
	{}
};

static struct platform_driver fsl_qman_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = fsl_qman_ids,
		.suppress_bind_attrs = true,
	},
	.probe = fsl_qman_probe,
};

builtin_platform_driver(fsl_qman_driver);
