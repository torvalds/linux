/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* ***** SDIO interface chip backplane handle functions ***** */

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/ssb/ssb_regs.h>
#include <linux/bcma/bcma.h>

#include <chipcommon.h>
#include <brcm_hw_ids.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include <soc.h>
#include "dhd_dbg.h"
#include "sdio_host.h"
#include "sdio_chip.h"

/* chip core base & ramsize */
/* bcm4329 */
/* SDIO device core, ID 0x829 */
#define BCM4329_CORE_BUS_BASE		0x18011000
/* internal memory core, ID 0x80e */
#define BCM4329_CORE_SOCRAM_BASE	0x18003000
/* ARM Cortex M3 core, ID 0x82a */
#define BCM4329_CORE_ARM_BASE		0x18002000
#define BCM4329_RAMSIZE			0x48000

/* bcm43143 */
/* SDIO device core */
#define BCM43143_CORE_BUS_BASE		0x18002000
/* internal memory core */
#define BCM43143_CORE_SOCRAM_BASE	0x18004000
/* ARM Cortex M3 core, ID 0x82a */
#define BCM43143_CORE_ARM_BASE		0x18003000
#define BCM43143_RAMSIZE		0x70000

/* All D11 cores, ID 0x812 */
#define BCM43xx_CORE_D11_BASE		0x18001000

#define	SBCOREREV(sbidh) \
	((((sbidh) & SSB_IDHIGH_RCHI) >> SSB_IDHIGH_RCHI_SHIFT) | \
	  ((sbidh) & SSB_IDHIGH_RCLO))

/* SOC Interconnect types (aka chip types) */
#define SOCI_SB		0
#define SOCI_AI		1

/* EROM CompIdentB */
#define CIB_REV_MASK		0xff000000
#define CIB_REV_SHIFT		24

/* ARM CR4 core specific control flag bits */
#define ARMCR4_BCMA_IOCTL_CPUHALT	0x0020

/* D11 core specific control flag bits */
#define D11_BCMA_IOCTL_PHYCLOCKEN	0x0004
#define D11_BCMA_IOCTL_PHYRESET		0x0008

#define SDIOD_DRVSTR_KEY(chip, pmu)     (((chip) << 16) | (pmu))
/* SDIO Pad drive strength to select value mappings */
struct sdiod_drive_str {
	u8 strength;	/* Pad Drive Strength in mA */
	u8 sel;		/* Chip-specific select value */
};
/* SDIO Drive Strength to sel value table for PMU Rev 11 (1.8V) */
static const struct sdiod_drive_str sdiod_drvstr_tab1_1v8[] = {
	{32, 0x6},
	{26, 0x7},
	{22, 0x4},
	{16, 0x5},
	{12, 0x2},
	{8, 0x3},
	{4, 0x0},
	{0, 0x1}
};

/* SDIO Drive Strength to sel value table for PMU Rev 13 (1.8v) */
static const struct sdiod_drive_str sdiod_drive_strength_tab5_1v8[] = {
        {6, 0x7},
        {5, 0x6},
        {4, 0x5},
        {3, 0x4},
        {2, 0x2},
        {1, 0x1},
        {0, 0x0}
};

/* SDIO Drive Strength to sel value table for PMU Rev 17 (1.8v) */
static const struct sdiod_drive_str sdiod_drvstr_tab6_1v8[] = {
	{3, 0x3},
	{2, 0x2},
	{1, 0x1},
	{0, 0x0} };

/* SDIO Drive Strength to sel value table for 43143 PMU Rev 17 (3.3V) */
static const struct sdiod_drive_str sdiod_drvstr_tab2_3v3[] = {
	{16, 0x7},
	{12, 0x5},
	{8,  0x3},
	{4,  0x1}
};

u8
brcmf_sdio_chip_getinfidx(struct brcmf_chip *ci, u16 coreid)
{
	u8 idx;

	for (idx = 0; idx < BRCMF_MAX_CORENUM; idx++)
		if (coreid == ci->c_inf[idx].id)
			return idx;

	return BRCMF_MAX_CORENUM;
}

static u32
brcmf_sdio_sb_corerev(struct brcmf_sdio_dev *sdiodev,
		      struct brcmf_chip *ci, u16 coreid)
{
	u32 regdata;
	u8 idx;

	idx = brcmf_sdio_chip_getinfidx(ci, coreid);

	regdata = brcmf_sdiod_regrl(sdiodev,
				    CORE_SB(ci->c_inf[idx].base, sbidhigh),
				    NULL);
	return SBCOREREV(regdata);
}

static u32
brcmf_sdio_ai_corerev(struct brcmf_sdio_dev *sdiodev,
		      struct brcmf_chip *ci, u16 coreid)
{
	u8 idx;

	idx = brcmf_sdio_chip_getinfidx(ci, coreid);

	return (ci->c_inf[idx].cib & CIB_REV_MASK) >> CIB_REV_SHIFT;
}

static bool
brcmf_sdio_sb_iscoreup(struct brcmf_sdio_dev *sdiodev,
		       struct brcmf_chip *ci, u16 coreid)
{
	u32 regdata;
	u8 idx;

	idx = brcmf_sdio_chip_getinfidx(ci, coreid);
	if (idx == BRCMF_MAX_CORENUM)
		return false;

	regdata = brcmf_sdiod_regrl(sdiodev,
				    CORE_SB(ci->c_inf[idx].base, sbtmstatelow),
				    NULL);
	regdata &= (SSB_TMSLOW_RESET | SSB_TMSLOW_REJECT |
		    SSB_IMSTATE_REJECT | SSB_TMSLOW_CLOCK);
	return (SSB_TMSLOW_CLOCK == regdata);
}

static bool
brcmf_sdio_ai_iscoreup(struct brcmf_sdio_dev *sdiodev,
		       struct brcmf_chip *ci, u16 coreid)
{
	u32 regdata;
	u8 idx;
	bool ret;

	idx = brcmf_sdio_chip_getinfidx(ci, coreid);
	if (idx == BRCMF_MAX_CORENUM)
		return false;

	regdata = brcmf_sdiod_regrl(sdiodev, ci->c_inf[idx].wrapbase+BCMA_IOCTL,
				    NULL);
	ret = (regdata & (BCMA_IOCTL_FGC | BCMA_IOCTL_CLK)) == BCMA_IOCTL_CLK;

	regdata = brcmf_sdiod_regrl(sdiodev,
				    ci->c_inf[idx].wrapbase+BCMA_RESET_CTL,
				    NULL);
	ret = ret && ((regdata & BCMA_RESET_CTL_RESET) == 0);

	return ret;
}

static void
brcmf_sdio_sb_coredisable(struct brcmf_sdio_dev *sdiodev,
			  struct brcmf_chip *ci, u16 coreid, u32 pre_resetbits,
			  u32 in_resetbits)
{
	u32 regdata, base;
	u8 idx;

	idx = brcmf_sdio_chip_getinfidx(ci, coreid);
	base = ci->c_inf[idx].base;

	regdata = brcmf_sdiod_regrl(sdiodev, CORE_SB(base, sbtmstatelow), NULL);
	if (regdata & SSB_TMSLOW_RESET)
		return;

	regdata = brcmf_sdiod_regrl(sdiodev, CORE_SB(base, sbtmstatelow), NULL);
	if ((regdata & SSB_TMSLOW_CLOCK) != 0) {
		/*
		 * set target reject and spin until busy is clear
		 * (preserve core-specific bits)
		 */
		regdata = brcmf_sdiod_regrl(sdiodev,
					    CORE_SB(base, sbtmstatelow), NULL);
		brcmf_sdiod_regwl(sdiodev, CORE_SB(base, sbtmstatelow),
				  regdata | SSB_TMSLOW_REJECT, NULL);

		regdata = brcmf_sdiod_regrl(sdiodev,
					    CORE_SB(base, sbtmstatelow), NULL);
		udelay(1);
		SPINWAIT((brcmf_sdiod_regrl(sdiodev,
					    CORE_SB(base, sbtmstatehigh),
					    NULL) &
			  SSB_TMSHIGH_BUSY), 100000);

		regdata = brcmf_sdiod_regrl(sdiodev,
					    CORE_SB(base, sbtmstatehigh),
					    NULL);
		if (regdata & SSB_TMSHIGH_BUSY)
			brcmf_err("core state still busy\n");

		regdata = brcmf_sdiod_regrl(sdiodev, CORE_SB(base, sbidlow),
					    NULL);
		if (regdata & SSB_IDLOW_INITIATOR) {
			regdata = brcmf_sdiod_regrl(sdiodev,
						    CORE_SB(base, sbimstate),
						    NULL);
			regdata |= SSB_IMSTATE_REJECT;
			brcmf_sdiod_regwl(sdiodev, CORE_SB(base, sbimstate),
					  regdata, NULL);
			regdata = brcmf_sdiod_regrl(sdiodev,
						    CORE_SB(base, sbimstate),
						    NULL);
			udelay(1);
			SPINWAIT((brcmf_sdiod_regrl(sdiodev,
						    CORE_SB(base, sbimstate),
						    NULL) &
				  SSB_IMSTATE_BUSY), 100000);
		}

		/* set reset and reject while enabling the clocks */
		regdata = SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK |
			  SSB_TMSLOW_REJECT | SSB_TMSLOW_RESET;
		brcmf_sdiod_regwl(sdiodev, CORE_SB(base, sbtmstatelow),
				  regdata, NULL);
		regdata = brcmf_sdiod_regrl(sdiodev,
					    CORE_SB(base, sbtmstatelow), NULL);
		udelay(10);

		/* clear the initiator reject bit */
		regdata = brcmf_sdiod_regrl(sdiodev, CORE_SB(base, sbidlow),
					    NULL);
		if (regdata & SSB_IDLOW_INITIATOR) {
			regdata = brcmf_sdiod_regrl(sdiodev,
						    CORE_SB(base, sbimstate),
						    NULL);
			regdata &= ~SSB_IMSTATE_REJECT;
			brcmf_sdiod_regwl(sdiodev, CORE_SB(base, sbimstate),
					  regdata, NULL);
		}
	}

	/* leave reset and reject asserted */
	brcmf_sdiod_regwl(sdiodev, CORE_SB(base, sbtmstatelow),
			  (SSB_TMSLOW_REJECT | SSB_TMSLOW_RESET), NULL);
	udelay(1);
}

static void
brcmf_sdio_ai_coredisable(struct brcmf_sdio_dev *sdiodev,
			  struct brcmf_chip *ci, u16 coreid, u32 pre_resetbits,
			  u32 in_resetbits)
{
	u8 idx;
	u32 regdata;
	u32 wrapbase;

	idx = brcmf_sdio_chip_getinfidx(ci, coreid);
	if (idx == BRCMF_MAX_CORENUM)
		return;

	wrapbase = ci->c_inf[idx].wrapbase;

	/* if core is already in reset, just return */
	regdata = brcmf_sdiod_regrl(sdiodev, wrapbase + BCMA_RESET_CTL, NULL);
	if ((regdata & BCMA_RESET_CTL_RESET) != 0)
		return;

	/* configure reset */
	brcmf_sdiod_regwl(sdiodev, wrapbase + BCMA_IOCTL, pre_resetbits |
			  BCMA_IOCTL_FGC | BCMA_IOCTL_CLK, NULL);
	regdata = brcmf_sdiod_regrl(sdiodev, wrapbase + BCMA_IOCTL, NULL);

	/* put in reset */
	brcmf_sdiod_regwl(sdiodev, wrapbase + BCMA_RESET_CTL,
			  BCMA_RESET_CTL_RESET, NULL);
	usleep_range(10, 20);

	/* wait till reset is 1 */
	SPINWAIT(brcmf_sdiod_regrl(sdiodev, wrapbase + BCMA_RESET_CTL, NULL) !=
		 BCMA_RESET_CTL_RESET, 300);

	/* post reset configure */
	brcmf_sdiod_regwl(sdiodev, wrapbase + BCMA_IOCTL, pre_resetbits |
			  BCMA_IOCTL_FGC | BCMA_IOCTL_CLK, NULL);
	regdata = brcmf_sdiod_regrl(sdiodev, wrapbase + BCMA_IOCTL, NULL);
}

static void
brcmf_sdio_sb_resetcore(struct brcmf_sdio_dev *sdiodev,
			struct brcmf_chip *ci, u16 coreid,  u32 pre_resetbits,
			u32 in_resetbits, u32 post_resetbits)
{
	u32 regdata;
	u8 idx;

	idx = brcmf_sdio_chip_getinfidx(ci, coreid);
	if (idx == BRCMF_MAX_CORENUM)
		return;

	/*
	 * Must do the disable sequence first to work for
	 * arbitrary current core state.
	 */
	brcmf_sdio_sb_coredisable(sdiodev, ci, coreid, pre_resetbits,
				  in_resetbits);

	/*
	 * Now do the initialization sequence.
	 * set reset while enabling the clock and
	 * forcing them on throughout the core
	 */
	brcmf_sdiod_regwl(sdiodev,
			  CORE_SB(ci->c_inf[idx].base, sbtmstatelow),
			  SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK | SSB_TMSLOW_RESET,
			  NULL);
	regdata = brcmf_sdiod_regrl(sdiodev,
				    CORE_SB(ci->c_inf[idx].base, sbtmstatelow),
				    NULL);
	udelay(1);

	/* clear any serror */
	regdata = brcmf_sdiod_regrl(sdiodev,
				    CORE_SB(ci->c_inf[idx].base, sbtmstatehigh),
				    NULL);
	if (regdata & SSB_TMSHIGH_SERR)
		brcmf_sdiod_regwl(sdiodev,
				  CORE_SB(ci->c_inf[idx].base, sbtmstatehigh),
				  0, NULL);

	regdata = brcmf_sdiod_regrl(sdiodev,
				    CORE_SB(ci->c_inf[idx].base, sbimstate),
				    NULL);
	if (regdata & (SSB_IMSTATE_IBE | SSB_IMSTATE_TO))
		brcmf_sdiod_regwl(sdiodev,
				  CORE_SB(ci->c_inf[idx].base, sbimstate),
				  regdata & ~(SSB_IMSTATE_IBE | SSB_IMSTATE_TO),
				  NULL);

	/* clear reset and allow it to propagate throughout the core */
	brcmf_sdiod_regwl(sdiodev, CORE_SB(ci->c_inf[idx].base, sbtmstatelow),
			  SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK, NULL);
	regdata = brcmf_sdiod_regrl(sdiodev,
				    CORE_SB(ci->c_inf[idx].base, sbtmstatelow),
				    NULL);
	udelay(1);

	/* leave clock enabled */
	brcmf_sdiod_regwl(sdiodev, CORE_SB(ci->c_inf[idx].base, sbtmstatelow),
			  SSB_TMSLOW_CLOCK, NULL);
	regdata = brcmf_sdiod_regrl(sdiodev,
				    CORE_SB(ci->c_inf[idx].base, sbtmstatelow),
				    NULL);
	udelay(1);
}

static void
brcmf_sdio_ai_resetcore(struct brcmf_sdio_dev *sdiodev,
			struct brcmf_chip *ci, u16 coreid, u32 pre_resetbits,
			u32 in_resetbits, u32 post_resetbits)
{
	u8 idx;
	u32 regdata;
	u32 wrapbase;

	idx = brcmf_sdio_chip_getinfidx(ci, coreid);
	if (idx == BRCMF_MAX_CORENUM)
		return;

	wrapbase = ci->c_inf[idx].wrapbase;

	/* must disable first to work for arbitrary current core state */
	brcmf_sdio_ai_coredisable(sdiodev, ci, coreid, pre_resetbits,
				  in_resetbits);

	while (brcmf_sdiod_regrl(sdiodev, wrapbase + BCMA_RESET_CTL, NULL) &
	       BCMA_RESET_CTL_RESET) {
		brcmf_sdiod_regwl(sdiodev, wrapbase + BCMA_RESET_CTL, 0, NULL);
		usleep_range(40, 60);
	}

	brcmf_sdiod_regwl(sdiodev, wrapbase + BCMA_IOCTL, post_resetbits |
			  BCMA_IOCTL_CLK, NULL);
	regdata = brcmf_sdiod_regrl(sdiodev, wrapbase + BCMA_IOCTL, NULL);
}

#ifdef DEBUG
/* safety check for chipinfo */
static int brcmf_sdio_chip_cichk(struct brcmf_chip *ci)
{
	u8 core_idx;

	/* check RAM core presence for ARM CM3 core */
	core_idx = brcmf_sdio_chip_getinfidx(ci, BCMA_CORE_ARM_CM3);
	if (BRCMF_MAX_CORENUM != core_idx) {
		core_idx = brcmf_sdio_chip_getinfidx(ci,
						     BCMA_CORE_INTERNAL_MEM);
		if (BRCMF_MAX_CORENUM == core_idx) {
			brcmf_err("RAM core not provided with ARM CM3 core\n");
			return -ENODEV;
		}
	}

	/* check RAM base for ARM CR4 core */
	core_idx = brcmf_sdio_chip_getinfidx(ci, BCMA_CORE_ARM_CR4);
	if (BRCMF_MAX_CORENUM != core_idx) {
		if (ci->rambase == 0) {
			brcmf_err("RAM base not provided with ARM CR4 core\n");
			return -ENOMEM;
		}
	}

	return 0;
}
#else	/* DEBUG */
static inline int brcmf_sdio_chip_cichk(struct brcmf_chip *ci)
{
	return 0;
}
#endif

static int brcmf_sdio_chip_recognition(struct brcmf_sdio_dev *sdiodev,
				       struct brcmf_chip *ci)
{
	u32 regdata;
	u32 socitype;

	/* Get CC core rev
	 * Chipid is assume to be at offset 0 from SI_ENUM_BASE
	 * For different chiptypes or old sdio hosts w/o chipcommon,
	 * other ways of recognition should be added here.
	 */
	regdata = brcmf_sdiod_regrl(sdiodev,
				    CORE_CC_REG(SI_ENUM_BASE, chipid),
				    NULL);
	ci->chip = regdata & CID_ID_MASK;
	ci->chiprev = (regdata & CID_REV_MASK) >> CID_REV_SHIFT;
	if (sdiodev->func[0]->device == SDIO_DEVICE_ID_BROADCOM_4335_4339 &&
	    ci->chiprev >= 2)
		ci->chip = BCM4339_CHIP_ID;
	socitype = (regdata & CID_TYPE_MASK) >> CID_TYPE_SHIFT;

	brcmf_dbg(INFO, "found %s chip: id=0x%x, rev=%d\n",
		  socitype == SOCI_SB ? "SB" : "AXI", ci->chip, ci->chiprev);

	if (socitype == SOCI_SB) {
		if (ci->chip != BCM4329_CHIP_ID) {
			brcmf_err("SB chip is not supported\n");
			return -ENODEV;
		}
		ci->iscoreup = brcmf_sdio_sb_iscoreup;
		ci->corerev = brcmf_sdio_sb_corerev;
		ci->coredisable = brcmf_sdio_sb_coredisable;
		ci->resetcore = brcmf_sdio_sb_resetcore;

		ci->c_inf[0].id = BCMA_CORE_CHIPCOMMON;
		ci->c_inf[0].base = SI_ENUM_BASE;
		ci->c_inf[1].id = BCMA_CORE_SDIO_DEV;
		ci->c_inf[1].base = BCM4329_CORE_BUS_BASE;
		ci->c_inf[2].id = BCMA_CORE_INTERNAL_MEM;
		ci->c_inf[2].base = BCM4329_CORE_SOCRAM_BASE;
		ci->c_inf[3].id = BCMA_CORE_ARM_CM3;
		ci->c_inf[3].base = BCM4329_CORE_ARM_BASE;
		ci->c_inf[4].id = BCMA_CORE_80211;
		ci->c_inf[4].base = BCM43xx_CORE_D11_BASE;
		ci->ramsize = BCM4329_RAMSIZE;
	} else if (socitype == SOCI_AI) {
		ci->iscoreup = brcmf_sdio_ai_iscoreup;
		ci->corerev = brcmf_sdio_ai_corerev;
		ci->coredisable = brcmf_sdio_ai_coredisable;
		ci->resetcore = brcmf_sdio_ai_resetcore;

		ci->c_inf[0].id = BCMA_CORE_CHIPCOMMON;
		ci->c_inf[0].base = SI_ENUM_BASE;

		/* Address of cores for new chips should be added here */
		switch (ci->chip) {
		case BCM43143_CHIP_ID:
			ci->c_inf[0].wrapbase = ci->c_inf[0].base + 0x00100000;
			ci->c_inf[0].cib = 0x2b000000;
			ci->c_inf[1].id = BCMA_CORE_SDIO_DEV;
			ci->c_inf[1].base = BCM43143_CORE_BUS_BASE;
			ci->c_inf[1].wrapbase = ci->c_inf[1].base + 0x00100000;
			ci->c_inf[1].cib = 0x18000000;
			ci->c_inf[2].id = BCMA_CORE_INTERNAL_MEM;
			ci->c_inf[2].base = BCM43143_CORE_SOCRAM_BASE;
			ci->c_inf[2].wrapbase = ci->c_inf[2].base + 0x00100000;
			ci->c_inf[2].cib = 0x14000000;
			ci->c_inf[3].id = BCMA_CORE_ARM_CM3;
			ci->c_inf[3].base = BCM43143_CORE_ARM_BASE;
			ci->c_inf[3].wrapbase = ci->c_inf[3].base + 0x00100000;
			ci->c_inf[3].cib = 0x07000000;
			ci->c_inf[4].id = BCMA_CORE_80211;
			ci->c_inf[4].base = BCM43xx_CORE_D11_BASE;
			ci->c_inf[4].wrapbase = ci->c_inf[4].base + 0x00100000;
			ci->ramsize = BCM43143_RAMSIZE;
			break;
		case BCM43241_CHIP_ID:
			ci->c_inf[0].wrapbase = 0x18100000;
			ci->c_inf[0].cib = 0x2a084411;
			ci->c_inf[1].id = BCMA_CORE_SDIO_DEV;
			ci->c_inf[1].base = 0x18002000;
			ci->c_inf[1].wrapbase = 0x18102000;
			ci->c_inf[1].cib = 0x0e004211;
			ci->c_inf[2].id = BCMA_CORE_INTERNAL_MEM;
			ci->c_inf[2].base = 0x18004000;
			ci->c_inf[2].wrapbase = 0x18104000;
			ci->c_inf[2].cib = 0x14080401;
			ci->c_inf[3].id = BCMA_CORE_ARM_CM3;
			ci->c_inf[3].base = 0x18003000;
			ci->c_inf[3].wrapbase = 0x18103000;
			ci->c_inf[3].cib = 0x07004211;
			ci->c_inf[4].id = BCMA_CORE_80211;
			ci->c_inf[4].base = BCM43xx_CORE_D11_BASE;
			ci->c_inf[4].wrapbase = ci->c_inf[4].base + 0x00100000;
			ci->ramsize = 0x90000;
			break;
		case BCM4330_CHIP_ID:
			ci->c_inf[0].wrapbase = 0x18100000;
			ci->c_inf[0].cib = 0x27004211;
			ci->c_inf[1].id = BCMA_CORE_SDIO_DEV;
			ci->c_inf[1].base = 0x18002000;
			ci->c_inf[1].wrapbase = 0x18102000;
			ci->c_inf[1].cib = 0x07004211;
			ci->c_inf[2].id = BCMA_CORE_INTERNAL_MEM;
			ci->c_inf[2].base = 0x18004000;
			ci->c_inf[2].wrapbase = 0x18104000;
			ci->c_inf[2].cib = 0x0d080401;
			ci->c_inf[3].id = BCMA_CORE_ARM_CM3;
			ci->c_inf[3].base = 0x18003000;
			ci->c_inf[3].wrapbase = 0x18103000;
			ci->c_inf[3].cib = 0x03004211;
			ci->c_inf[4].id = BCMA_CORE_80211;
			ci->c_inf[4].base = BCM43xx_CORE_D11_BASE;
			ci->c_inf[4].wrapbase = ci->c_inf[4].base + 0x00100000;
			ci->ramsize = 0x48000;
			break;
		case BCM4334_CHIP_ID:
			ci->c_inf[0].wrapbase = 0x18100000;
			ci->c_inf[0].cib = 0x29004211;
			ci->c_inf[1].id = BCMA_CORE_SDIO_DEV;
			ci->c_inf[1].base = 0x18002000;
			ci->c_inf[1].wrapbase = 0x18102000;
			ci->c_inf[1].cib = 0x0d004211;
			ci->c_inf[2].id = BCMA_CORE_INTERNAL_MEM;
			ci->c_inf[2].base = 0x18004000;
			ci->c_inf[2].wrapbase = 0x18104000;
			ci->c_inf[2].cib = 0x13080401;
			ci->c_inf[3].id = BCMA_CORE_ARM_CM3;
			ci->c_inf[3].base = 0x18003000;
			ci->c_inf[3].wrapbase = 0x18103000;
			ci->c_inf[3].cib = 0x07004211;
			ci->c_inf[4].id = BCMA_CORE_80211;
			ci->c_inf[4].base = BCM43xx_CORE_D11_BASE;
			ci->c_inf[4].wrapbase = ci->c_inf[4].base + 0x00100000;
			ci->ramsize = 0x80000;
			break;
		case BCM4335_CHIP_ID:
			ci->c_inf[0].wrapbase = 0x18100000;
			ci->c_inf[0].cib = 0x2b084411;
			ci->c_inf[1].id = BCMA_CORE_SDIO_DEV;
			ci->c_inf[1].base = 0x18005000;
			ci->c_inf[1].wrapbase = 0x18105000;
			ci->c_inf[1].cib = 0x0f004211;
			ci->c_inf[2].id = BCMA_CORE_ARM_CR4;
			ci->c_inf[2].base = 0x18002000;
			ci->c_inf[2].wrapbase = 0x18102000;
			ci->c_inf[2].cib = 0x01084411;
			ci->c_inf[3].id = BCMA_CORE_80211;
			ci->c_inf[3].base = BCM43xx_CORE_D11_BASE;
			ci->c_inf[3].wrapbase = ci->c_inf[3].base + 0x00100000;
			ci->ramsize = 0xc0000;
			ci->rambase = 0x180000;
			break;
		case BCM43362_CHIP_ID:
			ci->c_inf[0].wrapbase = 0x18100000;
			ci->c_inf[0].cib = 0x27004211;
			ci->c_inf[1].id = BCMA_CORE_SDIO_DEV;
			ci->c_inf[1].base = 0x18002000;
			ci->c_inf[1].wrapbase = 0x18102000;
			ci->c_inf[1].cib = 0x0a004211;
			ci->c_inf[2].id = BCMA_CORE_INTERNAL_MEM;
			ci->c_inf[2].base = 0x18004000;
			ci->c_inf[2].wrapbase = 0x18104000;
			ci->c_inf[2].cib = 0x08080401;
			ci->c_inf[3].id = BCMA_CORE_ARM_CM3;
			ci->c_inf[3].base = 0x18003000;
			ci->c_inf[3].wrapbase = 0x18103000;
			ci->c_inf[3].cib = 0x03004211;
			ci->c_inf[4].id = BCMA_CORE_80211;
			ci->c_inf[4].base = BCM43xx_CORE_D11_BASE;
			ci->c_inf[4].wrapbase = ci->c_inf[4].base + 0x00100000;
			ci->ramsize = 0x3C000;
			break;
		case BCM4339_CHIP_ID:
			ci->c_inf[0].wrapbase = 0x18100000;
			ci->c_inf[0].cib = 0x2e084411;
			ci->c_inf[1].id = BCMA_CORE_SDIO_DEV;
			ci->c_inf[1].base = 0x18005000;
			ci->c_inf[1].wrapbase = 0x18105000;
			ci->c_inf[1].cib = 0x15004211;
			ci->c_inf[2].id = BCMA_CORE_ARM_CR4;
			ci->c_inf[2].base = 0x18002000;
			ci->c_inf[2].wrapbase = 0x18102000;
			ci->c_inf[2].cib = 0x04084411;
			ci->c_inf[3].id = BCMA_CORE_80211;
			ci->c_inf[3].base = BCM43xx_CORE_D11_BASE;
			ci->c_inf[3].wrapbase = ci->c_inf[3].base + 0x00100000;
			ci->ramsize = 0xc0000;
			ci->rambase = 0x180000;
			break;
		default:
			brcmf_err("AXI chip is not supported\n");
			return -ENODEV;
		}
	} else {
		brcmf_err("chip backplane type %u is not supported\n",
			  socitype);
		return -ENODEV;
	}

	return brcmf_sdio_chip_cichk(ci);
}

static int
brcmf_sdio_chip_buscoreprep(struct brcmf_sdio_dev *sdiodev)
{
	int err = 0;
	u8 clkval, clkset;

	/* Try forcing SDIO core to do ALPAvail request only */
	clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ;
	brcmf_sdiod_regwb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
	if (err) {
		brcmf_err("error writing for HT off\n");
		return err;
	}

	/* If register supported, wait for ALPAvail and then force ALP */
	/* This may take up to 15 milliseconds */
	clkval = brcmf_sdiod_regrb(sdiodev,
				   SBSDIO_FUNC1_CHIPCLKCSR, NULL);

	if ((clkval & ~SBSDIO_AVBITS) != clkset) {
		brcmf_err("ChipClkCSR access: wrote 0x%02x read 0x%02x\n",
			  clkset, clkval);
		return -EACCES;
	}

	SPINWAIT(((clkval = brcmf_sdiod_regrb(sdiodev,
					      SBSDIO_FUNC1_CHIPCLKCSR, NULL)),
			!SBSDIO_ALPAV(clkval)),
			PMU_MAX_TRANSITION_DLY);
	if (!SBSDIO_ALPAV(clkval)) {
		brcmf_err("timeout on ALPAV wait, clkval 0x%02x\n",
			  clkval);
		return -EBUSY;
	}

	clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP;
	brcmf_sdiod_regwb(sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
	udelay(65);

	/* Also, disable the extra SDIO pull-ups */
	brcmf_sdiod_regwb(sdiodev, SBSDIO_FUNC1_SDIOPULLUP, 0, NULL);

	return 0;
}

static void
brcmf_sdio_chip_buscoresetup(struct brcmf_sdio_dev *sdiodev,
			     struct brcmf_chip *ci)
{
	u32 base = ci->c_inf[0].base;

	/* get chipcommon rev */
	ci->c_inf[0].rev = ci->corerev(sdiodev, ci, ci->c_inf[0].id);

	/* get chipcommon capabilites */
	ci->c_inf[0].caps = brcmf_sdiod_regrl(sdiodev,
					      CORE_CC_REG(base, capabilities),
					      NULL);

	/* get pmu caps & rev */
	if (ci->c_inf[0].caps & CC_CAP_PMU) {
		ci->pmucaps =
			brcmf_sdiod_regrl(sdiodev,
					  CORE_CC_REG(base, pmucapabilities),
					  NULL);
		ci->pmurev = ci->pmucaps & PCAP_REV_MASK;
	}

	ci->c_inf[1].rev = ci->corerev(sdiodev, ci, ci->c_inf[1].id);

	brcmf_dbg(INFO, "ccrev=%d, pmurev=%d, buscore rev/type=%d/0x%x\n",
		  ci->c_inf[0].rev, ci->pmurev,
		  ci->c_inf[1].rev, ci->c_inf[1].id);

	/*
	 * Make sure any on-chip ARM is off (in case strapping is wrong),
	 * or downloaded code was already running.
	 */
	ci->coredisable(sdiodev, ci, BCMA_CORE_ARM_CM3, 0, 0);
}

int brcmf_sdio_chip_attach(struct brcmf_sdio_dev *sdiodev,
			   struct brcmf_chip **ci_ptr)
{
	int ret;
	struct brcmf_chip *ci;

	brcmf_dbg(TRACE, "Enter\n");

	ci = kzalloc(sizeof(*ci), GFP_ATOMIC);
	if (!ci)
		return -ENOMEM;

	ret = brcmf_sdio_chip_buscoreprep(sdiodev);
	if (ret != 0)
		goto err;

	ret = brcmf_sdio_chip_recognition(sdiodev, ci);
	if (ret != 0)
		goto err;

	brcmf_sdio_chip_buscoresetup(sdiodev, ci);

	brcmf_sdiod_regwl(sdiodev, CORE_CC_REG(ci->c_inf[0].base, gpiopullup),
			  0, NULL);
	brcmf_sdiod_regwl(sdiodev, CORE_CC_REG(ci->c_inf[0].base, gpiopulldown),
			  0, NULL);

	*ci_ptr = ci;
	return 0;

err:
	kfree(ci);
	return ret;
}

void
brcmf_sdio_chip_detach(struct brcmf_chip **ci_ptr)
{
	brcmf_dbg(TRACE, "Enter\n");

	kfree(*ci_ptr);
	*ci_ptr = NULL;
}

static char *brcmf_sdio_chip_name(uint chipid, char *buf, uint len)
{
	const char *fmt;

	fmt = ((chipid > 0xa000) || (chipid < 0x4000)) ? "%d" : "%x";
	snprintf(buf, len, fmt, chipid);
	return buf;
}

void
brcmf_sdio_chip_drivestrengthinit(struct brcmf_sdio_dev *sdiodev,
				  struct brcmf_chip *ci, u32 drivestrength)
{
	const struct sdiod_drive_str *str_tab = NULL;
	u32 str_mask;
	u32 str_shift;
	char chn[8];
	u32 base = ci->c_inf[0].base;
	u32 i;
	u32 drivestrength_sel = 0;
	u32 cc_data_temp;
	u32 addr;

	if (!(ci->c_inf[0].caps & CC_CAP_PMU))
		return;

	switch (SDIOD_DRVSTR_KEY(ci->chip, ci->pmurev)) {
	case SDIOD_DRVSTR_KEY(BCM4330_CHIP_ID, 12):
		str_tab = sdiod_drvstr_tab1_1v8;
		str_mask = 0x00003800;
		str_shift = 11;
		break;
	case SDIOD_DRVSTR_KEY(BCM4334_CHIP_ID, 17):
		str_tab = sdiod_drvstr_tab6_1v8;
		str_mask = 0x00001800;
		str_shift = 11;
		break;
	case SDIOD_DRVSTR_KEY(BCM43143_CHIP_ID, 17):
		/* note: 43143 does not support tristate */
		i = ARRAY_SIZE(sdiod_drvstr_tab2_3v3) - 1;
		if (drivestrength >= sdiod_drvstr_tab2_3v3[i].strength) {
			str_tab = sdiod_drvstr_tab2_3v3;
			str_mask = 0x00000007;
			str_shift = 0;
		} else
			brcmf_err("Invalid SDIO Drive strength for chip %s, strength=%d\n",
				  brcmf_sdio_chip_name(ci->chip, chn, 8),
				  drivestrength);
		break;
	case SDIOD_DRVSTR_KEY(BCM43362_CHIP_ID, 13):
		str_tab = sdiod_drive_strength_tab5_1v8;
		str_mask = 0x00003800;
		str_shift = 11;
		break;
	default:
		brcmf_err("No SDIO Drive strength init done for chip %s rev %d pmurev %d\n",
			  brcmf_sdio_chip_name(ci->chip, chn, 8),
			  ci->chiprev, ci->pmurev);
		break;
	}

	if (str_tab != NULL) {
		for (i = 0; str_tab[i].strength != 0; i++) {
			if (drivestrength >= str_tab[i].strength) {
				drivestrength_sel = str_tab[i].sel;
				break;
			}
		}
		addr = CORE_CC_REG(base, chipcontrol_addr);
		brcmf_sdiod_regwl(sdiodev, addr, 1, NULL);
		cc_data_temp = brcmf_sdiod_regrl(sdiodev, addr, NULL);
		cc_data_temp &= ~str_mask;
		drivestrength_sel <<= str_shift;
		cc_data_temp |= drivestrength_sel;
		brcmf_sdiod_regwl(sdiodev, addr, cc_data_temp, NULL);

		brcmf_dbg(INFO, "SDIO: %d mA (req=%d mA) drive strength selected, set to 0x%08x\n",
			  str_tab[i].strength, drivestrength, cc_data_temp);
	}
}

static void
brcmf_sdio_chip_cm3_enterdl(struct brcmf_sdio_dev *sdiodev,
			    struct brcmf_chip *ci)
{
	ci->coredisable(sdiodev, ci, BCMA_CORE_ARM_CM3, 0, 0);
	ci->resetcore(sdiodev, ci, BCMA_CORE_80211,
		      D11_BCMA_IOCTL_PHYRESET | D11_BCMA_IOCTL_PHYCLOCKEN,
		      D11_BCMA_IOCTL_PHYCLOCKEN, D11_BCMA_IOCTL_PHYCLOCKEN);
	ci->resetcore(sdiodev, ci, BCMA_CORE_INTERNAL_MEM, 0, 0, 0);
}

static bool brcmf_sdio_chip_cm3_exitdl(struct brcmf_sdio_dev *sdiodev,
				       struct brcmf_chip *ci)
{
	u8 core_idx;
	u32 reg_addr;

	if (!ci->iscoreup(sdiodev, ci, BCMA_CORE_INTERNAL_MEM)) {
		brcmf_err("SOCRAM core is down after reset?\n");
		return false;
	}

	/* clear all interrupts */
	core_idx = brcmf_sdio_chip_getinfidx(ci, BCMA_CORE_SDIO_DEV);
	reg_addr = ci->c_inf[core_idx].base;
	reg_addr += offsetof(struct sdpcmd_regs, intstatus);
	brcmf_sdiod_regwl(sdiodev, reg_addr, 0xFFFFFFFF, NULL);

	ci->resetcore(sdiodev, ci, BCMA_CORE_ARM_CM3, 0, 0, 0);

	return true;
}

static inline void
brcmf_sdio_chip_cr4_enterdl(struct brcmf_sdio_dev *sdiodev,
			    struct brcmf_chip *ci)
{
	u8 idx;
	u32 regdata;
	u32 wrapbase;
	idx = brcmf_sdio_chip_getinfidx(ci, BCMA_CORE_ARM_CR4);

	if (idx == BRCMF_MAX_CORENUM)
		return;

	wrapbase = ci->c_inf[idx].wrapbase;
	regdata = brcmf_sdiod_regrl(sdiodev, wrapbase + BCMA_IOCTL, NULL);
	regdata &= ARMCR4_BCMA_IOCTL_CPUHALT;
	ci->resetcore(sdiodev, ci, BCMA_CORE_ARM_CR4, regdata,
		      ARMCR4_BCMA_IOCTL_CPUHALT, ARMCR4_BCMA_IOCTL_CPUHALT);
	ci->resetcore(sdiodev, ci, BCMA_CORE_80211,
		      D11_BCMA_IOCTL_PHYRESET | D11_BCMA_IOCTL_PHYCLOCKEN,
		      D11_BCMA_IOCTL_PHYCLOCKEN, D11_BCMA_IOCTL_PHYCLOCKEN);
}

static bool brcmf_sdio_chip_cr4_exitdl(struct brcmf_sdio_dev *sdiodev,
				       struct brcmf_chip *ci, u32 rstvec)
{
	u8 core_idx;
	u32 reg_addr;

	/* clear all interrupts */
	core_idx = brcmf_sdio_chip_getinfidx(ci, BCMA_CORE_SDIO_DEV);
	reg_addr = ci->c_inf[core_idx].base;
	reg_addr += offsetof(struct sdpcmd_regs, intstatus);
	brcmf_sdiod_regwl(sdiodev, reg_addr, 0xFFFFFFFF, NULL);

	/* Write reset vector to address 0 */
	brcmf_sdiod_ramrw(sdiodev, true, 0, (void *)&rstvec,
			  sizeof(rstvec));

	/* restore ARM */
	ci->resetcore(sdiodev, ci, BCMA_CORE_ARM_CR4, ARMCR4_BCMA_IOCTL_CPUHALT,
		      0, 0);

	return true;
}

void brcmf_sdio_chip_enter_download(struct brcmf_sdio_dev *sdiodev,
				    struct brcmf_chip *ci)
{
	u8 arm_core_idx;

	arm_core_idx = brcmf_sdio_chip_getinfidx(ci, BCMA_CORE_ARM_CM3);
	if (BRCMF_MAX_CORENUM != arm_core_idx) {
		brcmf_sdio_chip_cm3_enterdl(sdiodev, ci);
		return;
	}

	brcmf_sdio_chip_cr4_enterdl(sdiodev, ci);
}

bool brcmf_sdio_chip_exit_download(struct brcmf_sdio_dev *sdiodev,
				   struct brcmf_chip *ci, u32 rstvec)
{
	u8 arm_core_idx;

	arm_core_idx = brcmf_sdio_chip_getinfidx(ci, BCMA_CORE_ARM_CM3);
	if (BRCMF_MAX_CORENUM != arm_core_idx)
		return brcmf_sdio_chip_cm3_exitdl(sdiodev, ci);

	return brcmf_sdio_chip_cr4_exitdl(sdiodev, ci, rstvec);
}
