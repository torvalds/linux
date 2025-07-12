// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/ssb/ssb_regs.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>

#include <defs.h>
#include <soc.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <chipcommon.h>
#include "debug.h"
#include "chip.h"

/* SOC Interconnect types (aka chip types) */
#define SOCI_SB		0
#define SOCI_AI		1

/* PL-368 DMP definitions */
#define DMP_DESC_TYPE_MSK	0x0000000F
#define  DMP_DESC_EMPTY		0x00000000
#define  DMP_DESC_VALID		0x00000001
#define  DMP_DESC_COMPONENT	0x00000001
#define  DMP_DESC_MASTER_PORT	0x00000003
#define  DMP_DESC_ADDRESS	0x00000005
#define  DMP_DESC_ADDRSIZE_GT32	0x00000008
#define  DMP_DESC_EOT		0x0000000F

#define DMP_COMP_DESIGNER	0xFFF00000
#define DMP_COMP_DESIGNER_S	20
#define DMP_COMP_PARTNUM	0x000FFF00
#define DMP_COMP_PARTNUM_S	8
#define DMP_COMP_CLASS		0x000000F0
#define DMP_COMP_CLASS_S	4
#define DMP_COMP_REVISION	0xFF000000
#define DMP_COMP_REVISION_S	24
#define DMP_COMP_NUM_SWRAP	0x00F80000
#define DMP_COMP_NUM_SWRAP_S	19
#define DMP_COMP_NUM_MWRAP	0x0007C000
#define DMP_COMP_NUM_MWRAP_S	14
#define DMP_COMP_NUM_SPORT	0x00003E00
#define DMP_COMP_NUM_SPORT_S	9
#define DMP_COMP_NUM_MPORT	0x000001F0
#define DMP_COMP_NUM_MPORT_S	4

#define DMP_MASTER_PORT_UID	0x0000FF00
#define DMP_MASTER_PORT_UID_S	8
#define DMP_MASTER_PORT_NUM	0x000000F0
#define DMP_MASTER_PORT_NUM_S	4

#define DMP_SLAVE_ADDR_BASE	0xFFFFF000
#define DMP_SLAVE_ADDR_BASE_S	12
#define DMP_SLAVE_PORT_NUM	0x00000F00
#define DMP_SLAVE_PORT_NUM_S	8
#define DMP_SLAVE_TYPE		0x000000C0
#define DMP_SLAVE_TYPE_S	6
#define  DMP_SLAVE_TYPE_SLAVE	0
#define  DMP_SLAVE_TYPE_BRIDGE	1
#define  DMP_SLAVE_TYPE_SWRAP	2
#define  DMP_SLAVE_TYPE_MWRAP	3
#define DMP_SLAVE_SIZE_TYPE	0x00000030
#define DMP_SLAVE_SIZE_TYPE_S	4
#define  DMP_SLAVE_SIZE_4K	0
#define  DMP_SLAVE_SIZE_8K	1
#define  DMP_SLAVE_SIZE_16K	2
#define  DMP_SLAVE_SIZE_DESC	3

/* EROM CompIdentB */
#define CIB_REV_MASK		0xff000000
#define CIB_REV_SHIFT		24

/* ARM CR4 core specific control flag bits */
#define ARMCR4_BCMA_IOCTL_CPUHALT	0x0020

/* D11 core specific control flag bits */
#define D11_BCMA_IOCTL_PHYCLOCKEN	0x0004
#define D11_BCMA_IOCTL_PHYRESET		0x0008

/* chip core base & ramsize */
/* bcm4329 */
/* SDIO device core, ID 0x829 */
#define BCM4329_CORE_BUS_BASE		0x18011000
/* internal memory core, ID 0x80e */
#define BCM4329_CORE_SOCRAM_BASE	0x18003000
/* ARM Cortex M3 core, ID 0x82a */
#define BCM4329_CORE_ARM_BASE		0x18002000

/* Max possibly supported memory size (limited by IO mapped memory) */
#define BRCMF_CHIP_MAX_MEMSIZE		(4 * 1024 * 1024)

#define CORE_SB(base, field) \
		(base + SBCONFIGOFF + offsetof(struct sbconfig, field))
#define	SBCOREREV(sbidh) \
	((((sbidh) & SSB_IDHIGH_RCHI) >> SSB_IDHIGH_RCHI_SHIFT) | \
	  ((sbidh) & SSB_IDHIGH_RCLO))

struct sbconfig {
	u32 PAD[2];
	u32 sbipsflag;	/* initiator port ocp slave flag */
	u32 PAD[3];
	u32 sbtpsflag;	/* target port ocp slave flag */
	u32 PAD[11];
	u32 sbtmerrloga;	/* (sonics >= 2.3) */
	u32 PAD;
	u32 sbtmerrlog;	/* (sonics >= 2.3) */
	u32 PAD[3];
	u32 sbadmatch3;	/* address match3 */
	u32 PAD;
	u32 sbadmatch2;	/* address match2 */
	u32 PAD;
	u32 sbadmatch1;	/* address match1 */
	u32 PAD[7];
	u32 sbimstate;	/* initiator agent state */
	u32 sbintvec;	/* interrupt mask */
	u32 sbtmstatelow;	/* target state */
	u32 sbtmstatehigh;	/* target state */
	u32 sbbwa0;		/* bandwidth allocation table0 */
	u32 PAD;
	u32 sbimconfiglow;	/* initiator configuration */
	u32 sbimconfighigh;	/* initiator configuration */
	u32 sbadmatch0;	/* address match0 */
	u32 PAD;
	u32 sbtmconfiglow;	/* target configuration */
	u32 sbtmconfighigh;	/* target configuration */
	u32 sbbconfig;	/* broadcast configuration */
	u32 PAD;
	u32 sbbstate;	/* broadcast state */
	u32 PAD[3];
	u32 sbactcnfg;	/* activate configuration */
	u32 PAD[3];
	u32 sbflagst;	/* current sbflags */
	u32 PAD[3];
	u32 sbidlow;		/* identification */
	u32 sbidhigh;	/* identification */
};

#define INVALID_RAMBASE			((u32)(~0))

/* bankidx and bankinfo reg defines corerev >= 8 */
#define SOCRAM_BANKINFO_RETNTRAM_MASK	0x00010000
#define SOCRAM_BANKINFO_SZMASK		0x0000007f
#define SOCRAM_BANKIDX_ROM_MASK		0x00000100

#define SOCRAM_BANKIDX_MEMTYPE_SHIFT	8
/* socram bankinfo memtype */
#define SOCRAM_MEMTYPE_RAM		0
#define SOCRAM_MEMTYPE_R0M		1
#define SOCRAM_MEMTYPE_DEVRAM		2

#define SOCRAM_BANKINFO_SZBASE		8192
#define SRCI_LSS_MASK		0x00f00000
#define SRCI_LSS_SHIFT		20
#define	SRCI_SRNB_MASK		0xf0
#define	SRCI_SRNB_MASK_EXT	0x100
#define	SRCI_SRNB_SHIFT		4
#define	SRCI_SRBSZ_MASK		0xf
#define	SRCI_SRBSZ_SHIFT	0
#define SR_BSZ_BASE		14

struct sbsocramregs {
	u32 coreinfo;
	u32 bwalloc;
	u32 extracoreinfo;
	u32 biststat;
	u32 bankidx;
	u32 standbyctrl;

	u32 errlogstatus;	/* rev 6 */
	u32 errlogaddr;	/* rev 6 */
	/* used for patching rev 3 & 5 */
	u32 cambankidx;
	u32 cambankstandbyctrl;
	u32 cambankpatchctrl;
	u32 cambankpatchtblbaseaddr;
	u32 cambankcmdreg;
	u32 cambankdatareg;
	u32 cambankmaskreg;
	u32 PAD[1];
	u32 bankinfo;	/* corev 8 */
	u32 bankpda;
	u32 PAD[14];
	u32 extmemconfig;
	u32 extmemparitycsr;
	u32 extmemparityerrdata;
	u32 extmemparityerrcnt;
	u32 extmemwrctrlandsize;
	u32 PAD[84];
	u32 workaround;
	u32 pwrctl;		/* corerev >= 2 */
	u32 PAD[133];
	u32 sr_control;     /* corerev >= 15 */
	u32 sr_status;      /* corerev >= 15 */
	u32 sr_address;     /* corerev >= 15 */
	u32 sr_data;        /* corerev >= 15 */
};

#define SOCRAMREGOFFS(_f)	offsetof(struct sbsocramregs, _f)
#define SYSMEMREGOFFS(_f)	offsetof(struct sbsocramregs, _f)

#define ARMCR4_CAP		(0x04)
#define ARMCR4_BANKIDX		(0x40)
#define ARMCR4_BANKINFO		(0x44)
#define ARMCR4_BANKPDA		(0x4C)

#define	ARMCR4_TCBBNB_MASK	0xf0
#define	ARMCR4_TCBBNB_SHIFT	4
#define	ARMCR4_TCBANB_MASK	0xf
#define	ARMCR4_TCBANB_SHIFT	0

#define	ARMCR4_BSZ_MASK		0x7f
#define	ARMCR4_BSZ_MULT		8192
#define	ARMCR4_BLK_1K_MASK	0x200

struct brcmf_core_priv {
	struct brcmf_core pub;
	u32 wrapbase;
	struct list_head list;
	struct brcmf_chip_priv *chip;
};

struct brcmf_chip_priv {
	struct brcmf_chip pub;
	const struct brcmf_buscore_ops *ops;
	void *ctx;
	/* assured first core is chipcommon, second core is buscore */
	struct list_head cores;
	u16 num_cores;

	bool (*iscoreup)(struct brcmf_core_priv *core);
	void (*coredisable)(struct brcmf_core_priv *core, u32 prereset,
			    u32 reset);
	void (*resetcore)(struct brcmf_core_priv *core, u32 prereset, u32 reset,
			  u32 postreset);
};

static void brcmf_chip_sb_corerev(struct brcmf_chip_priv *ci,
				  struct brcmf_core *core)
{
	u32 regdata;

	regdata = ci->ops->read32(ci->ctx, CORE_SB(core->base, sbidhigh));
	core->rev = SBCOREREV(regdata);
}

static bool brcmf_chip_sb_iscoreup(struct brcmf_core_priv *core)
{
	struct brcmf_chip_priv *ci;
	u32 regdata;
	u32 address;

	ci = core->chip;
	address = CORE_SB(core->pub.base, sbtmstatelow);
	regdata = ci->ops->read32(ci->ctx, address);
	regdata &= (SSB_TMSLOW_RESET | SSB_TMSLOW_REJECT |
		    SSB_IMSTATE_REJECT | SSB_TMSLOW_CLOCK);
	return SSB_TMSLOW_CLOCK == regdata;
}

static bool brcmf_chip_ai_iscoreup(struct brcmf_core_priv *core)
{
	struct brcmf_chip_priv *ci;
	u32 regdata;
	bool ret;

	ci = core->chip;
	regdata = ci->ops->read32(ci->ctx, core->wrapbase + BCMA_IOCTL);
	ret = (regdata & (BCMA_IOCTL_FGC | BCMA_IOCTL_CLK)) == BCMA_IOCTL_CLK;

	regdata = ci->ops->read32(ci->ctx, core->wrapbase + BCMA_RESET_CTL);
	ret = ret && ((regdata & BCMA_RESET_CTL_RESET) == 0);

	return ret;
}

static void brcmf_chip_sb_coredisable(struct brcmf_core_priv *core,
				      u32 prereset, u32 reset)
{
	struct brcmf_chip_priv *ci;
	u32 val, base;

	ci = core->chip;
	base = core->pub.base;
	val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
	if (val & SSB_TMSLOW_RESET)
		return;

	val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
	if ((val & SSB_TMSLOW_CLOCK) != 0) {
		/*
		 * set target reject and spin until busy is clear
		 * (preserve core-specific bits)
		 */
		val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
		ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow),
					 val | SSB_TMSLOW_REJECT);

		val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
		udelay(1);
		SPINWAIT((ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatehigh))
			  & SSB_TMSHIGH_BUSY), 100000);

		val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatehigh));
		if (val & SSB_TMSHIGH_BUSY)
			brcmf_err("core state still busy\n");

		val = ci->ops->read32(ci->ctx, CORE_SB(base, sbidlow));
		if (val & SSB_IDLOW_INITIATOR) {
			val = ci->ops->read32(ci->ctx,
					      CORE_SB(base, sbimstate));
			val |= SSB_IMSTATE_REJECT;
			ci->ops->write32(ci->ctx,
					 CORE_SB(base, sbimstate), val);
			val = ci->ops->read32(ci->ctx,
					      CORE_SB(base, sbimstate));
			udelay(1);
			SPINWAIT((ci->ops->read32(ci->ctx,
						  CORE_SB(base, sbimstate)) &
				  SSB_IMSTATE_BUSY), 100000);
		}

		/* set reset and reject while enabling the clocks */
		val = SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK |
		      SSB_TMSLOW_REJECT | SSB_TMSLOW_RESET;
		ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow), val);
		val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
		udelay(10);

		/* clear the initiator reject bit */
		val = ci->ops->read32(ci->ctx, CORE_SB(base, sbidlow));
		if (val & SSB_IDLOW_INITIATOR) {
			val = ci->ops->read32(ci->ctx,
					      CORE_SB(base, sbimstate));
			val &= ~SSB_IMSTATE_REJECT;
			ci->ops->write32(ci->ctx,
					 CORE_SB(base, sbimstate), val);
		}
	}

	/* leave reset and reject asserted */
	ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow),
			 (SSB_TMSLOW_REJECT | SSB_TMSLOW_RESET));
	udelay(1);
}

static void brcmf_chip_ai_coredisable(struct brcmf_core_priv *core,
				      u32 prereset, u32 reset)
{
	struct brcmf_chip_priv *ci;
	u32 regdata;

	ci = core->chip;

	/* if core is already in reset, skip reset */
	regdata = ci->ops->read32(ci->ctx, core->wrapbase + BCMA_RESET_CTL);
	if ((regdata & BCMA_RESET_CTL_RESET) != 0)
		goto in_reset_configure;

	/* configure reset */
	ci->ops->write32(ci->ctx, core->wrapbase + BCMA_IOCTL,
			 prereset | BCMA_IOCTL_FGC | BCMA_IOCTL_CLK);
	ci->ops->read32(ci->ctx, core->wrapbase + BCMA_IOCTL);

	/* put in reset */
	ci->ops->write32(ci->ctx, core->wrapbase + BCMA_RESET_CTL,
			 BCMA_RESET_CTL_RESET);
	usleep_range(10, 20);

	/* wait till reset is 1 */
	SPINWAIT(ci->ops->read32(ci->ctx, core->wrapbase + BCMA_RESET_CTL) !=
		 BCMA_RESET_CTL_RESET, 300);

in_reset_configure:
	/* in-reset configure */
	ci->ops->write32(ci->ctx, core->wrapbase + BCMA_IOCTL,
			 reset | BCMA_IOCTL_FGC | BCMA_IOCTL_CLK);
	ci->ops->read32(ci->ctx, core->wrapbase + BCMA_IOCTL);
}

static void brcmf_chip_sb_resetcore(struct brcmf_core_priv *core, u32 prereset,
				    u32 reset, u32 postreset)
{
	struct brcmf_chip_priv *ci;
	u32 regdata;
	u32 base;

	ci = core->chip;
	base = core->pub.base;
	/*
	 * Must do the disable sequence first to work for
	 * arbitrary current core state.
	 */
	brcmf_chip_sb_coredisable(core, 0, 0);

	/*
	 * Now do the initialization sequence.
	 * set reset while enabling the clock and
	 * forcing them on throughout the core
	 */
	ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow),
			 SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK |
			 SSB_TMSLOW_RESET);
	regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
	udelay(1);

	/* clear any serror */
	regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatehigh));
	if (regdata & SSB_TMSHIGH_SERR)
		ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatehigh), 0);

	regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbimstate));
	if (regdata & (SSB_IMSTATE_IBE | SSB_IMSTATE_TO)) {
		regdata &= ~(SSB_IMSTATE_IBE | SSB_IMSTATE_TO);
		ci->ops->write32(ci->ctx, CORE_SB(base, sbimstate), regdata);
	}

	/* clear reset and allow it to propagate throughout the core */
	ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow),
			 SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK);
	regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
	udelay(1);

	/* leave clock enabled */
	ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow),
			 SSB_TMSLOW_CLOCK);
	regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
	udelay(1);
}

static void brcmf_chip_ai_resetcore(struct brcmf_core_priv *core, u32 prereset,
				    u32 reset, u32 postreset)
{
	struct brcmf_chip_priv *ci;
	int count;
	struct brcmf_core *d11core2 = NULL;
	struct brcmf_core_priv *d11priv2 = NULL;

	ci = core->chip;

	/* special handle two D11 cores reset */
	if (core->pub.id == BCMA_CORE_80211) {
		d11core2 = brcmf_chip_get_d11core(&ci->pub, 1);
		if (d11core2) {
			brcmf_dbg(INFO, "found two d11 cores, reset both\n");
			d11priv2 = container_of(d11core2,
						struct brcmf_core_priv, pub);
		}
	}

	/* must disable first to work for arbitrary current core state */
	brcmf_chip_ai_coredisable(core, prereset, reset);
	if (d11priv2)
		brcmf_chip_ai_coredisable(d11priv2, prereset, reset);

	count = 0;
	while (ci->ops->read32(ci->ctx, core->wrapbase + BCMA_RESET_CTL) &
	       BCMA_RESET_CTL_RESET) {
		ci->ops->write32(ci->ctx, core->wrapbase + BCMA_RESET_CTL, 0);
		count++;
		if (count > 50)
			break;
		usleep_range(40, 60);
	}

	if (d11priv2) {
		count = 0;
		while (ci->ops->read32(ci->ctx,
				       d11priv2->wrapbase + BCMA_RESET_CTL) &
				       BCMA_RESET_CTL_RESET) {
			ci->ops->write32(ci->ctx,
					 d11priv2->wrapbase + BCMA_RESET_CTL,
					 0);
			count++;
			if (count > 50)
				break;
			usleep_range(40, 60);
		}
	}

	ci->ops->write32(ci->ctx, core->wrapbase + BCMA_IOCTL,
			 postreset | BCMA_IOCTL_CLK);
	ci->ops->read32(ci->ctx, core->wrapbase + BCMA_IOCTL);

	if (d11priv2) {
		ci->ops->write32(ci->ctx, d11priv2->wrapbase + BCMA_IOCTL,
				 postreset | BCMA_IOCTL_CLK);
		ci->ops->read32(ci->ctx, d11priv2->wrapbase + BCMA_IOCTL);
	}
}

char *brcmf_chip_name(u32 id, u32 rev, char *buf, uint len)
{
	const char *fmt;

	fmt = ((id > 0xa000) || (id < 0x4000)) ? "BCM%d/%u" : "BCM%x/%u";
	snprintf(buf, len, fmt, id, rev);
	return buf;
}

static struct brcmf_core *brcmf_chip_add_core(struct brcmf_chip_priv *ci,
					      u16 coreid, u32 base,
					      u32 wrapbase)
{
	struct brcmf_core_priv *core;

	core = kzalloc(sizeof(*core), GFP_KERNEL);
	if (!core)
		return ERR_PTR(-ENOMEM);

	core->pub.id = coreid;
	core->pub.base = base;
	core->chip = ci;
	core->wrapbase = wrapbase;

	list_add_tail(&core->list, &ci->cores);
	return &core->pub;
}

/* safety check for chipinfo */
static int brcmf_chip_cores_check(struct brcmf_chip_priv *ci)
{
	struct brcmf_core_priv *core;
	bool need_socram = false;
	bool has_socram = false;
	bool cpu_found = false;
	int idx = 1;

	list_for_each_entry(core, &ci->cores, list) {
		brcmf_dbg(INFO, " [%-2d] core 0x%x:%-3d base 0x%08x wrap 0x%08x\n",
			  idx++, core->pub.id, core->pub.rev, core->pub.base,
			  core->wrapbase);

		switch (core->pub.id) {
		case BCMA_CORE_ARM_CM3:
			cpu_found = true;
			need_socram = true;
			break;
		case BCMA_CORE_INTERNAL_MEM:
			has_socram = true;
			break;
		case BCMA_CORE_ARM_CR4:
			cpu_found = true;
			break;
		case BCMA_CORE_ARM_CA7:
			cpu_found = true;
			break;
		default:
			break;
		}
	}

	if (!cpu_found) {
		brcmf_err("CPU core not detected\n");
		return -ENXIO;
	}
	/* check RAM core presence for ARM CM3 core */
	if (need_socram && !has_socram) {
		brcmf_err("RAM core not provided with ARM CM3 core\n");
		return -ENODEV;
	}
	return 0;
}

static u32 brcmf_chip_core_read32(struct brcmf_core_priv *core, u16 reg)
{
	return core->chip->ops->read32(core->chip->ctx, core->pub.base + reg);
}

static void brcmf_chip_core_write32(struct brcmf_core_priv *core,
				    u16 reg, u32 val)
{
	core->chip->ops->write32(core->chip->ctx, core->pub.base + reg, val);
}

static bool brcmf_chip_socram_banksize(struct brcmf_core_priv *core, u8 idx,
				       u32 *banksize)
{
	u32 bankinfo;
	u32 bankidx = (SOCRAM_MEMTYPE_RAM << SOCRAM_BANKIDX_MEMTYPE_SHIFT);

	bankidx |= idx;
	brcmf_chip_core_write32(core, SOCRAMREGOFFS(bankidx), bankidx);
	bankinfo = brcmf_chip_core_read32(core, SOCRAMREGOFFS(bankinfo));
	*banksize = (bankinfo & SOCRAM_BANKINFO_SZMASK) + 1;
	*banksize *= SOCRAM_BANKINFO_SZBASE;
	return !!(bankinfo & SOCRAM_BANKINFO_RETNTRAM_MASK);
}

static void brcmf_chip_socram_ramsize(struct brcmf_core_priv *sr, u32 *ramsize,
				      u32 *srsize)
{
	u32 coreinfo;
	uint nb, banksize, lss;
	bool retent;
	int i;

	*ramsize = 0;
	*srsize = 0;

	if (WARN_ON(sr->pub.rev < 4))
		return;

	if (!brcmf_chip_iscoreup(&sr->pub))
		brcmf_chip_resetcore(&sr->pub, 0, 0, 0);

	/* Get info for determining size */
	coreinfo = brcmf_chip_core_read32(sr, SOCRAMREGOFFS(coreinfo));
	nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;

	if ((sr->pub.rev <= 7) || (sr->pub.rev == 12)) {
		banksize = (coreinfo & SRCI_SRBSZ_MASK);
		lss = (coreinfo & SRCI_LSS_MASK) >> SRCI_LSS_SHIFT;
		if (lss != 0)
			nb--;
		*ramsize = nb * (1 << (banksize + SR_BSZ_BASE));
		if (lss != 0)
			*ramsize += (1 << ((lss - 1) + SR_BSZ_BASE));
	} else {
		/* length of SRAM Banks increased for corerev greater than 23 */
		if (sr->pub.rev >= 23) {
			nb = (coreinfo & (SRCI_SRNB_MASK | SRCI_SRNB_MASK_EXT))
				>> SRCI_SRNB_SHIFT;
		} else {
			nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;
		}
		for (i = 0; i < nb; i++) {
			retent = brcmf_chip_socram_banksize(sr, i, &banksize);
			*ramsize += banksize;
			if (retent)
				*srsize += banksize;
		}
	}

	/* hardcoded save&restore memory sizes */
	switch (sr->chip->pub.chip) {
	case BRCM_CC_4334_CHIP_ID:
		if (sr->chip->pub.chiprev < 2)
			*srsize = (32 * 1024);
		break;
	case BRCM_CC_43430_CHIP_ID:
	case CY_CC_43439_CHIP_ID:
		/* assume sr for now as we can not check
		 * firmware sr capability at this point.
		 */
		*srsize = (64 * 1024);
		break;
	default:
		break;
	}
}

/** Return the SYS MEM size */
static u32 brcmf_chip_sysmem_ramsize(struct brcmf_core_priv *sysmem)
{
	u32 memsize = 0;
	u32 coreinfo;
	u32 idx;
	u32 nb;
	u32 banksize;

	if (!brcmf_chip_iscoreup(&sysmem->pub))
		brcmf_chip_resetcore(&sysmem->pub, 0, 0, 0);

	coreinfo = brcmf_chip_core_read32(sysmem, SYSMEMREGOFFS(coreinfo));
	nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;

	for (idx = 0; idx < nb; idx++) {
		brcmf_chip_socram_banksize(sysmem, idx, &banksize);
		memsize += banksize;
	}

	return memsize;
}

/** Return the TCM-RAM size of the ARMCR4 core. */
static u32 brcmf_chip_tcm_ramsize(struct brcmf_core_priv *cr4)
{
	u32 corecap;
	u32 memsize = 0;
	u32 nab;
	u32 nbb;
	u32 totb;
	u32 bxinfo;
	u32 blksize;
	u32 idx;

	corecap = brcmf_chip_core_read32(cr4, ARMCR4_CAP);

	nab = (corecap & ARMCR4_TCBANB_MASK) >> ARMCR4_TCBANB_SHIFT;
	nbb = (corecap & ARMCR4_TCBBNB_MASK) >> ARMCR4_TCBBNB_SHIFT;
	totb = nab + nbb;

	for (idx = 0; idx < totb; idx++) {
		brcmf_chip_core_write32(cr4, ARMCR4_BANKIDX, idx);
		bxinfo = brcmf_chip_core_read32(cr4, ARMCR4_BANKINFO);
		blksize = ARMCR4_BSZ_MULT;
		if (bxinfo & ARMCR4_BLK_1K_MASK)
			blksize >>= 3;

		memsize += ((bxinfo & ARMCR4_BSZ_MASK) + 1) * blksize;
	}

	return memsize;
}

static u32 brcmf_chip_tcm_rambase(struct brcmf_chip_priv *ci)
{
	switch (ci->pub.chip) {
	case BRCM_CC_4345_CHIP_ID:
	case BRCM_CC_43454_CHIP_ID:
		return 0x198000;
	case BRCM_CC_4335_CHIP_ID:
	case BRCM_CC_4339_CHIP_ID:
	case BRCM_CC_4350_CHIP_ID:
	case BRCM_CC_4354_CHIP_ID:
	case BRCM_CC_4356_CHIP_ID:
	case BRCM_CC_43567_CHIP_ID:
	case BRCM_CC_43569_CHIP_ID:
	case BRCM_CC_43570_CHIP_ID:
	case BRCM_CC_4358_CHIP_ID:
	case BRCM_CC_43602_CHIP_ID:
	case BRCM_CC_4371_CHIP_ID:
		return 0x180000;
	case BRCM_CC_43465_CHIP_ID:
	case BRCM_CC_43525_CHIP_ID:
	case BRCM_CC_4365_CHIP_ID:
	case BRCM_CC_4366_CHIP_ID:
	case BRCM_CC_43664_CHIP_ID:
	case BRCM_CC_43666_CHIP_ID:
		return 0x200000;
	case BRCM_CC_4355_CHIP_ID:
	case BRCM_CC_4359_CHIP_ID:
		return (ci->pub.chiprev < 9) ? 0x180000 : 0x160000;
	case BRCM_CC_4364_CHIP_ID:
	case CY_CC_4373_CHIP_ID:
		return 0x160000;
	case CY_CC_43752_CHIP_ID:
	case BRCM_CC_43751_CHIP_ID:
	case BRCM_CC_4377_CHIP_ID:
		return 0x170000;
	case BRCM_CC_4378_CHIP_ID:
		return 0x352000;
	case BRCM_CC_4387_CHIP_ID:
		return 0x740000;
	default:
		brcmf_err("unknown chip: %s\n", ci->pub.name);
		break;
	}
	return INVALID_RAMBASE;
}

int brcmf_chip_get_raminfo(struct brcmf_chip *pub)
{
	struct brcmf_chip_priv *ci = container_of(pub, struct brcmf_chip_priv,
						  pub);
	struct brcmf_core_priv *mem_core;
	struct brcmf_core *mem;

	mem = brcmf_chip_get_core(&ci->pub, BCMA_CORE_ARM_CR4);
	if (mem) {
		mem_core = container_of(mem, struct brcmf_core_priv, pub);
		ci->pub.ramsize = brcmf_chip_tcm_ramsize(mem_core);
		ci->pub.rambase = brcmf_chip_tcm_rambase(ci);
		if (ci->pub.rambase == INVALID_RAMBASE) {
			brcmf_err("RAM base not provided with ARM CR4 core\n");
			return -EINVAL;
		}
	} else {
		mem = brcmf_chip_get_core(&ci->pub, BCMA_CORE_SYS_MEM);
		if (mem) {
			mem_core = container_of(mem, struct brcmf_core_priv,
						pub);
			ci->pub.ramsize = brcmf_chip_sysmem_ramsize(mem_core);
			ci->pub.rambase = brcmf_chip_tcm_rambase(ci);
			if (ci->pub.rambase == INVALID_RAMBASE) {
				brcmf_err("RAM base not provided with ARM CA7 core\n");
				return -EINVAL;
			}
		} else {
			mem = brcmf_chip_get_core(&ci->pub,
						  BCMA_CORE_INTERNAL_MEM);
			if (!mem) {
				brcmf_err("No memory cores found\n");
				return -ENOMEM;
			}
			mem_core = container_of(mem, struct brcmf_core_priv,
						pub);
			brcmf_chip_socram_ramsize(mem_core, &ci->pub.ramsize,
						  &ci->pub.srsize);
		}
	}
	brcmf_dbg(INFO, "RAM: base=0x%x size=%d (0x%x) sr=%d (0x%x)\n",
		  ci->pub.rambase, ci->pub.ramsize, ci->pub.ramsize,
		  ci->pub.srsize, ci->pub.srsize);

	if (!ci->pub.ramsize) {
		brcmf_err("RAM size is undetermined\n");
		return -ENOMEM;
	}

	if (ci->pub.ramsize > BRCMF_CHIP_MAX_MEMSIZE) {
		brcmf_err("RAM size is incorrect\n");
		return -ENOMEM;
	}

	return 0;
}

static u32 brcmf_chip_dmp_get_desc(struct brcmf_chip_priv *ci, u32 *eromaddr,
				   u8 *type)
{
	u32 val;

	/* read next descriptor */
	val = ci->ops->read32(ci->ctx, *eromaddr);
	*eromaddr += 4;

	if (!type)
		return val;

	/* determine descriptor type */
	*type = (val & DMP_DESC_TYPE_MSK);
	if ((*type & ~DMP_DESC_ADDRSIZE_GT32) == DMP_DESC_ADDRESS)
		*type = DMP_DESC_ADDRESS;

	return val;
}

static int brcmf_chip_dmp_get_regaddr(struct brcmf_chip_priv *ci, u32 *eromaddr,
				      u32 *regbase, u32 *wrapbase)
{
	u8 desc;
	u32 val, szdesc;
	u8 stype, sztype, wraptype;

	*regbase = 0;
	*wrapbase = 0;

	val = brcmf_chip_dmp_get_desc(ci, eromaddr, &desc);
	if (desc == DMP_DESC_MASTER_PORT) {
		wraptype = DMP_SLAVE_TYPE_MWRAP;
	} else if (desc == DMP_DESC_ADDRESS) {
		/* revert erom address */
		*eromaddr -= 4;
		wraptype = DMP_SLAVE_TYPE_SWRAP;
	} else {
		*eromaddr -= 4;
		return -EILSEQ;
	}

	do {
		/* locate address descriptor */
		do {
			val = brcmf_chip_dmp_get_desc(ci, eromaddr, &desc);
			/* unexpected table end */
			if (desc == DMP_DESC_EOT) {
				*eromaddr -= 4;
				return -EFAULT;
			}
		} while (desc != DMP_DESC_ADDRESS &&
			 desc != DMP_DESC_COMPONENT);

		/* stop if we crossed current component border */
		if (desc == DMP_DESC_COMPONENT) {
			*eromaddr -= 4;
			return 0;
		}

		/* skip upper 32-bit address descriptor */
		if (val & DMP_DESC_ADDRSIZE_GT32)
			brcmf_chip_dmp_get_desc(ci, eromaddr, NULL);

		sztype = (val & DMP_SLAVE_SIZE_TYPE) >> DMP_SLAVE_SIZE_TYPE_S;

		/* next size descriptor can be skipped */
		if (sztype == DMP_SLAVE_SIZE_DESC) {
			szdesc = brcmf_chip_dmp_get_desc(ci, eromaddr, NULL);
			/* skip upper size descriptor if present */
			if (szdesc & DMP_DESC_ADDRSIZE_GT32)
				brcmf_chip_dmp_get_desc(ci, eromaddr, NULL);
		}

		/* look for 4K or 8K register regions */
		if (sztype != DMP_SLAVE_SIZE_4K &&
		    sztype != DMP_SLAVE_SIZE_8K)
			continue;

		stype = (val & DMP_SLAVE_TYPE) >> DMP_SLAVE_TYPE_S;

		/* only regular slave and wrapper */
		if (*regbase == 0 && stype == DMP_SLAVE_TYPE_SLAVE)
			*regbase = val & DMP_SLAVE_ADDR_BASE;
		if (*wrapbase == 0 && stype == wraptype)
			*wrapbase = val & DMP_SLAVE_ADDR_BASE;
	} while (*regbase == 0 || *wrapbase == 0);

	return 0;
}

static
int brcmf_chip_dmp_erom_scan(struct brcmf_chip_priv *ci)
{
	struct brcmf_core *core;
	u32 eromaddr;
	u8 desc_type = 0;
	u32 val;
	u16 id;
	u8 nmw, nsw, rev;
	u32 base, wrap;
	int err;

	eromaddr = ci->ops->read32(ci->ctx,
				   CORE_CC_REG(ci->pub.enum_base, eromptr));

	while (desc_type != DMP_DESC_EOT) {
		val = brcmf_chip_dmp_get_desc(ci, &eromaddr, &desc_type);
		if (!(val & DMP_DESC_VALID))
			continue;

		if (desc_type == DMP_DESC_EMPTY)
			continue;

		/* need a component descriptor */
		if (desc_type != DMP_DESC_COMPONENT)
			continue;

		id = (val & DMP_COMP_PARTNUM) >> DMP_COMP_PARTNUM_S;

		/* next descriptor must be component as well */
		val = brcmf_chip_dmp_get_desc(ci, &eromaddr, &desc_type);
		if (WARN_ON((val & DMP_DESC_TYPE_MSK) != DMP_DESC_COMPONENT))
			return -EFAULT;

		/* only look at cores with master port(s) */
		nmw = (val & DMP_COMP_NUM_MWRAP) >> DMP_COMP_NUM_MWRAP_S;
		nsw = (val & DMP_COMP_NUM_SWRAP) >> DMP_COMP_NUM_SWRAP_S;
		rev = (val & DMP_COMP_REVISION) >> DMP_COMP_REVISION_S;

		/* need core with ports */
		if (nmw + nsw == 0 &&
		    id != BCMA_CORE_PMU &&
		    id != BCMA_CORE_GCI)
			continue;

		/* try to obtain register address info */
		err = brcmf_chip_dmp_get_regaddr(ci, &eromaddr, &base, &wrap);
		if (err)
			continue;

		/* finally a core to be added */
		core = brcmf_chip_add_core(ci, id, base, wrap);
		if (IS_ERR(core))
			return PTR_ERR(core);

		core->rev = rev;
	}

	return 0;
}

u32 brcmf_chip_enum_base(u16 devid)
{
	return SI_ENUM_BASE_DEFAULT;
}

static int brcmf_chip_recognition(struct brcmf_chip_priv *ci)
{
	struct brcmf_core *core;
	u32 regdata;
	u32 socitype;
	int ret;
	const u32 READ_FAILED = 0xFFFFFFFF;

	/* Get CC core rev
	 * Chipid is assume to be at offset 0 from SI_ENUM_BASE
	 * For different chiptypes or old sdio hosts w/o chipcommon,
	 * other ways of recognition should be added here.
	 */
	regdata = ci->ops->read32(ci->ctx,
				  CORE_CC_REG(ci->pub.enum_base, chipid));
	if (regdata == READ_FAILED) {
		brcmf_err("MMIO read failed: 0x%08x\n", regdata);
		return -ENODEV;
	}

	ci->pub.chip = regdata & CID_ID_MASK;
	ci->pub.chiprev = (regdata & CID_REV_MASK) >> CID_REV_SHIFT;
	socitype = (regdata & CID_TYPE_MASK) >> CID_TYPE_SHIFT;

	brcmf_chip_name(ci->pub.chip, ci->pub.chiprev,
			ci->pub.name, sizeof(ci->pub.name));
	brcmf_dbg(INFO, "found %s chip: %s\n",
		  socitype == SOCI_SB ? "SB" : "AXI", ci->pub.name);

	if (socitype == SOCI_SB) {
		if (ci->pub.chip != BRCM_CC_4329_CHIP_ID) {
			brcmf_err("SB chip is not supported\n");
			return -ENODEV;
		}
		ci->iscoreup = brcmf_chip_sb_iscoreup;
		ci->coredisable = brcmf_chip_sb_coredisable;
		ci->resetcore = brcmf_chip_sb_resetcore;

		core = brcmf_chip_add_core(ci, BCMA_CORE_CHIPCOMMON,
					   SI_ENUM_BASE_DEFAULT, 0);
		brcmf_chip_sb_corerev(ci, core);
		core = brcmf_chip_add_core(ci, BCMA_CORE_SDIO_DEV,
					   BCM4329_CORE_BUS_BASE, 0);
		brcmf_chip_sb_corerev(ci, core);
		core = brcmf_chip_add_core(ci, BCMA_CORE_INTERNAL_MEM,
					   BCM4329_CORE_SOCRAM_BASE, 0);
		brcmf_chip_sb_corerev(ci, core);
		core = brcmf_chip_add_core(ci, BCMA_CORE_ARM_CM3,
					   BCM4329_CORE_ARM_BASE, 0);
		brcmf_chip_sb_corerev(ci, core);

		core = brcmf_chip_add_core(ci, BCMA_CORE_80211, 0x18001000, 0);
		brcmf_chip_sb_corerev(ci, core);
	} else if (socitype == SOCI_AI) {
		ci->iscoreup = brcmf_chip_ai_iscoreup;
		ci->coredisable = brcmf_chip_ai_coredisable;
		ci->resetcore = brcmf_chip_ai_resetcore;

		brcmf_chip_dmp_erom_scan(ci);
	} else {
		brcmf_err("chip backplane type %u is not supported\n",
			  socitype);
		return -ENODEV;
	}

	ret = brcmf_chip_cores_check(ci);
	if (ret)
		return ret;

	/* assure chip is passive for core access */
	brcmf_chip_set_passive(&ci->pub);

	/* Call bus specific reset function now. Cores have been determined
	 * but further access may require a chip specific reset at this point.
	 */
	if (ci->ops->reset) {
		ci->ops->reset(ci->ctx, &ci->pub);
		brcmf_chip_set_passive(&ci->pub);
	}

	return brcmf_chip_get_raminfo(&ci->pub);
}

static void brcmf_chip_disable_arm(struct brcmf_chip_priv *chip, u16 id)
{
	struct brcmf_core *core;
	struct brcmf_core_priv *cpu;
	u32 val;


	core = brcmf_chip_get_core(&chip->pub, id);
	if (!core)
		return;

	switch (id) {
	case BCMA_CORE_ARM_CM3:
		brcmf_chip_coredisable(core, 0, 0);
		break;
	case BCMA_CORE_ARM_CR4:
	case BCMA_CORE_ARM_CA7:
		cpu = container_of(core, struct brcmf_core_priv, pub);

		/* clear all IOCTL bits except HALT bit */
		val = chip->ops->read32(chip->ctx, cpu->wrapbase + BCMA_IOCTL);
		val &= ARMCR4_BCMA_IOCTL_CPUHALT;
		brcmf_chip_resetcore(core, val, ARMCR4_BCMA_IOCTL_CPUHALT,
				     ARMCR4_BCMA_IOCTL_CPUHALT);
		break;
	default:
		brcmf_err("unknown id: %u\n", id);
		break;
	}
}

static int brcmf_chip_setup(struct brcmf_chip_priv *chip)
{
	struct brcmf_chip *pub;
	struct brcmf_core_priv *cc;
	struct brcmf_core *pmu;
	u32 base;
	u32 val;
	int ret = 0;

	pub = &chip->pub;
	cc = list_first_entry(&chip->cores, struct brcmf_core_priv, list);
	base = cc->pub.base;

	/* get chipcommon capabilites */
	pub->cc_caps = chip->ops->read32(chip->ctx,
					 CORE_CC_REG(base, capabilities));
	pub->cc_caps_ext = chip->ops->read32(chip->ctx,
					     CORE_CC_REG(base,
							 capabilities_ext));

	/* get pmu caps & rev */
	pmu = brcmf_chip_get_pmu(pub); /* after reading cc_caps_ext */
	if (pub->cc_caps & CC_CAP_PMU) {
		val = chip->ops->read32(chip->ctx,
					CORE_CC_REG(pmu->base, pmucapabilities));
		pub->pmurev = val & PCAP_REV_MASK;
		pub->pmucaps = val;
	}

	brcmf_dbg(INFO, "ccrev=%d, pmurev=%d, pmucaps=0x%x\n",
		  cc->pub.rev, pub->pmurev, pub->pmucaps);

	/* execute bus core specific setup */
	if (chip->ops->setup)
		ret = chip->ops->setup(chip->ctx, pub);

	return ret;
}

struct brcmf_chip *brcmf_chip_attach(void *ctx, u16 devid,
				     const struct brcmf_buscore_ops *ops)
{
	struct brcmf_chip_priv *chip;
	int err = 0;

	if (WARN_ON(!ops->read32))
		err = -EINVAL;
	if (WARN_ON(!ops->write32))
		err = -EINVAL;
	if (WARN_ON(!ops->prepare))
		err = -EINVAL;
	if (WARN_ON(!ops->activate))
		err = -EINVAL;
	if (err < 0)
		return ERR_PTR(-EINVAL);

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&chip->cores);
	chip->num_cores = 0;
	chip->ops = ops;
	chip->ctx = ctx;
	chip->pub.enum_base = brcmf_chip_enum_base(devid);

	err = ops->prepare(ctx);
	if (err < 0)
		goto fail;

	err = brcmf_chip_recognition(chip);
	if (err < 0)
		goto fail;

	err = brcmf_chip_setup(chip);
	if (err < 0)
		goto fail;

	return &chip->pub;

fail:
	brcmf_chip_detach(&chip->pub);
	return ERR_PTR(err);
}

void brcmf_chip_detach(struct brcmf_chip *pub)
{
	struct brcmf_chip_priv *chip;
	struct brcmf_core_priv *core;
	struct brcmf_core_priv *tmp;

	chip = container_of(pub, struct brcmf_chip_priv, pub);
	list_for_each_entry_safe(core, tmp, &chip->cores, list) {
		list_del(&core->list);
		kfree(core);
	}
	kfree(chip);
}

struct brcmf_core *brcmf_chip_get_d11core(struct brcmf_chip *pub, u8 unit)
{
	struct brcmf_chip_priv *chip;
	struct brcmf_core_priv *core;

	chip = container_of(pub, struct brcmf_chip_priv, pub);
	list_for_each_entry(core, &chip->cores, list) {
		if (core->pub.id == BCMA_CORE_80211) {
			if (unit-- == 0)
				return &core->pub;
		}
	}
	return NULL;
}

struct brcmf_core *brcmf_chip_get_core(struct brcmf_chip *pub, u16 coreid)
{
	struct brcmf_chip_priv *chip;
	struct brcmf_core_priv *core;

	chip = container_of(pub, struct brcmf_chip_priv, pub);
	list_for_each_entry(core, &chip->cores, list)
		if (core->pub.id == coreid)
			return &core->pub;

	return NULL;
}

struct brcmf_core *brcmf_chip_get_chipcommon(struct brcmf_chip *pub)
{
	struct brcmf_chip_priv *chip;
	struct brcmf_core_priv *cc;

	chip = container_of(pub, struct brcmf_chip_priv, pub);
	cc = list_first_entry(&chip->cores, struct brcmf_core_priv, list);
	if (WARN_ON(!cc || cc->pub.id != BCMA_CORE_CHIPCOMMON))
		return brcmf_chip_get_core(pub, BCMA_CORE_CHIPCOMMON);
	return &cc->pub;
}

struct brcmf_core *brcmf_chip_get_pmu(struct brcmf_chip *pub)
{
	struct brcmf_core *cc = brcmf_chip_get_chipcommon(pub);
	struct brcmf_core *pmu;

	/* See if there is separated PMU core available */
	if (cc->rev >= 35 &&
	    pub->cc_caps_ext & BCMA_CC_CAP_EXT_AOB_PRESENT) {
		pmu = brcmf_chip_get_core(pub, BCMA_CORE_PMU);
		if (pmu)
			return pmu;
	}

	/* Fallback to ChipCommon core for older hardware */
	return cc;
}

bool brcmf_chip_iscoreup(struct brcmf_core *pub)
{
	struct brcmf_core_priv *core;

	core = container_of(pub, struct brcmf_core_priv, pub);
	return core->chip->iscoreup(core);
}

void brcmf_chip_coredisable(struct brcmf_core *pub, u32 prereset, u32 reset)
{
	struct brcmf_core_priv *core;

	core = container_of(pub, struct brcmf_core_priv, pub);
	core->chip->coredisable(core, prereset, reset);
}

void brcmf_chip_resetcore(struct brcmf_core *pub, u32 prereset, u32 reset,
			  u32 postreset)
{
	struct brcmf_core_priv *core;

	core = container_of(pub, struct brcmf_core_priv, pub);
	core->chip->resetcore(core, prereset, reset, postreset);
}

static void
brcmf_chip_cm3_set_passive(struct brcmf_chip_priv *chip)
{
	struct brcmf_core *core;
	struct brcmf_core_priv *sr;

	brcmf_chip_disable_arm(chip, BCMA_CORE_ARM_CM3);
	core = brcmf_chip_get_core(&chip->pub, BCMA_CORE_80211);
	brcmf_chip_resetcore(core, D11_BCMA_IOCTL_PHYRESET |
				   D11_BCMA_IOCTL_PHYCLOCKEN,
			     D11_BCMA_IOCTL_PHYCLOCKEN,
			     D11_BCMA_IOCTL_PHYCLOCKEN);
	core = brcmf_chip_get_core(&chip->pub, BCMA_CORE_INTERNAL_MEM);
	brcmf_chip_resetcore(core, 0, 0, 0);

	/* disable bank #3 remap for this device */
	if (chip->pub.chip == BRCM_CC_43430_CHIP_ID ||
	    chip->pub.chip == CY_CC_43439_CHIP_ID) {
		sr = container_of(core, struct brcmf_core_priv, pub);
		brcmf_chip_core_write32(sr, SOCRAMREGOFFS(bankidx), 3);
		brcmf_chip_core_write32(sr, SOCRAMREGOFFS(bankpda), 0);
	}
}

static bool brcmf_chip_cm3_set_active(struct brcmf_chip_priv *chip)
{
	struct brcmf_core *core;

	core = brcmf_chip_get_core(&chip->pub, BCMA_CORE_INTERNAL_MEM);
	if (!brcmf_chip_iscoreup(core)) {
		brcmf_err("SOCRAM core is down after reset?\n");
		return false;
	}

	chip->ops->activate(chip->ctx, &chip->pub, 0);

	core = brcmf_chip_get_core(&chip->pub, BCMA_CORE_ARM_CM3);
	brcmf_chip_resetcore(core, 0, 0, 0);

	return true;
}

static inline void
brcmf_chip_cr4_set_passive(struct brcmf_chip_priv *chip)
{
	int i;
	struct brcmf_core *core;

	brcmf_chip_disable_arm(chip, BCMA_CORE_ARM_CR4);

	/* Disable the cores only and let the firmware enable them.
	 * Releasing reset ourselves breaks BCM4387 in weird ways.
	 */
	for (i = 0; (core = brcmf_chip_get_d11core(&chip->pub, i)); i++)
		brcmf_chip_coredisable(core, D11_BCMA_IOCTL_PHYRESET |
				       D11_BCMA_IOCTL_PHYCLOCKEN,
				       D11_BCMA_IOCTL_PHYCLOCKEN);
}

static bool brcmf_chip_cr4_set_active(struct brcmf_chip_priv *chip, u32 rstvec)
{
	struct brcmf_core *core;

	chip->ops->activate(chip->ctx, &chip->pub, rstvec);

	/* restore ARM */
	core = brcmf_chip_get_core(&chip->pub, BCMA_CORE_ARM_CR4);
	brcmf_chip_resetcore(core, ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);

	return true;
}

static inline void
brcmf_chip_ca7_set_passive(struct brcmf_chip_priv *chip)
{
	struct brcmf_core *core;

	brcmf_chip_disable_arm(chip, BCMA_CORE_ARM_CA7);

	core = brcmf_chip_get_core(&chip->pub, BCMA_CORE_80211);
	brcmf_chip_resetcore(core, D11_BCMA_IOCTL_PHYRESET |
				   D11_BCMA_IOCTL_PHYCLOCKEN,
			     D11_BCMA_IOCTL_PHYCLOCKEN,
			     D11_BCMA_IOCTL_PHYCLOCKEN);
}

static bool brcmf_chip_ca7_set_active(struct brcmf_chip_priv *chip, u32 rstvec)
{
	struct brcmf_core *core;

	chip->ops->activate(chip->ctx, &chip->pub, rstvec);

	/* restore ARM */
	core = brcmf_chip_get_core(&chip->pub, BCMA_CORE_ARM_CA7);
	brcmf_chip_resetcore(core, ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);

	return true;
}

void brcmf_chip_set_passive(struct brcmf_chip *pub)
{
	struct brcmf_chip_priv *chip;
	struct brcmf_core *arm;

	brcmf_dbg(TRACE, "Enter\n");

	chip = container_of(pub, struct brcmf_chip_priv, pub);
	arm = brcmf_chip_get_core(pub, BCMA_CORE_ARM_CR4);
	if (arm) {
		brcmf_chip_cr4_set_passive(chip);
		return;
	}
	arm = brcmf_chip_get_core(pub, BCMA_CORE_ARM_CA7);
	if (arm) {
		brcmf_chip_ca7_set_passive(chip);
		return;
	}
	arm = brcmf_chip_get_core(pub, BCMA_CORE_ARM_CM3);
	if (arm) {
		brcmf_chip_cm3_set_passive(chip);
		return;
	}
}

bool brcmf_chip_set_active(struct brcmf_chip *pub, u32 rstvec)
{
	struct brcmf_chip_priv *chip;
	struct brcmf_core *arm;

	brcmf_dbg(TRACE, "Enter\n");

	chip = container_of(pub, struct brcmf_chip_priv, pub);
	arm = brcmf_chip_get_core(pub, BCMA_CORE_ARM_CR4);
	if (arm)
		return brcmf_chip_cr4_set_active(chip, rstvec);
	arm = brcmf_chip_get_core(pub, BCMA_CORE_ARM_CA7);
	if (arm)
		return brcmf_chip_ca7_set_active(chip, rstvec);
	arm = brcmf_chip_get_core(pub, BCMA_CORE_ARM_CM3);
	if (arm)
		return brcmf_chip_cm3_set_active(chip);

	return false;
}

bool brcmf_chip_sr_capable(struct brcmf_chip *pub)
{
	u32 base, addr, reg, pmu_cc3_mask = ~0;
	struct brcmf_chip_priv *chip;
	struct brcmf_core *pmu = brcmf_chip_get_pmu(pub);

	brcmf_dbg(TRACE, "Enter\n");

	/* old chips with PMU version less than 17 don't support save restore */
	if (pub->pmurev < 17)
		return false;

	base = brcmf_chip_get_chipcommon(pub)->base;
	chip = container_of(pub, struct brcmf_chip_priv, pub);

	switch (pub->chip) {
	case BRCM_CC_4354_CHIP_ID:
	case BRCM_CC_4356_CHIP_ID:
	case BRCM_CC_4345_CHIP_ID:
	case BRCM_CC_43454_CHIP_ID:
		/* explicitly check SR engine enable bit */
		pmu_cc3_mask = BIT(2);
		fallthrough;
	case BRCM_CC_43241_CHIP_ID:
	case BRCM_CC_4335_CHIP_ID:
	case BRCM_CC_4339_CHIP_ID:
		/* read PMU chipcontrol register 3 */
		addr = CORE_CC_REG(pmu->base, chipcontrol_addr);
		chip->ops->write32(chip->ctx, addr, 3);
		addr = CORE_CC_REG(pmu->base, chipcontrol_data);
		reg = chip->ops->read32(chip->ctx, addr);
		return (reg & pmu_cc3_mask) != 0;
	case BRCM_CC_43430_CHIP_ID:
	case CY_CC_43439_CHIP_ID:
		addr = CORE_CC_REG(base, sr_control1);
		reg = chip->ops->read32(chip->ctx, addr);
		return reg != 0;
	case BRCM_CC_4355_CHIP_ID:
	case CY_CC_4373_CHIP_ID:
		/* explicitly check SR engine enable bit */
		addr = CORE_CC_REG(base, sr_control0);
		reg = chip->ops->read32(chip->ctx, addr);
		return (reg & CC_SR_CTL0_ENABLE_MASK) != 0;
	case BRCM_CC_4359_CHIP_ID:
	case BRCM_CC_43751_CHIP_ID:
	case CY_CC_43752_CHIP_ID:
	case CY_CC_43012_CHIP_ID:
		addr = CORE_CC_REG(pmu->base, retention_ctl);
		reg = chip->ops->read32(chip->ctx, addr);
		return (reg & (PMU_RCTL_MACPHY_DISABLE_MASK |
			       PMU_RCTL_LOGIC_DISABLE_MASK)) == 0;
	default:
		addr = CORE_CC_REG(pmu->base, pmucapabilities_ext);
		reg = chip->ops->read32(chip->ctx, addr);
		if ((reg & PCAPEXT_SR_SUPPORTED_MASK) == 0)
			return false;

		addr = CORE_CC_REG(pmu->base, retention_ctl);
		reg = chip->ops->read32(chip->ctx, addr);
		return (reg & (PMU_RCTL_MACPHY_DISABLE_MASK |
			       PMU_RCTL_LOGIC_DISABLE_MASK)) == 0;
	}
}
