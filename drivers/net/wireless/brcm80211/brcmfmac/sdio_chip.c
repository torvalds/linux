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
#include <chipcommon.h>
#include <brcm_hw_ids.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include <soc.h>
#include "dhd.h"
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


/* SB regs */
/* sbidhigh */
#define	SBIDH_RC_MASK		0x000f	/* revision code */
#define	SBIDH_RCE_MASK		0x7000	/* revision code extension field */
#define	SBIDH_RCE_SHIFT		8
#define	SBCOREREV(sbidh) \
	((((sbidh) & SBIDH_RCE_MASK) >> SBIDH_RCE_SHIFT) | \
	  ((sbidh) & SBIDH_RC_MASK))
#define	SBIDH_CC_MASK		0x8ff0	/* core code */
#define	SBIDH_CC_SHIFT		4
#define	SBIDH_VC_MASK		0xffff0000	/* vendor code */
#define	SBIDH_VC_SHIFT		16

void
brcmf_sdio_chip_coredisable(struct brcmf_sdio_dev *sdiodev, u32 corebase)
{
	u32 regdata;

	regdata = brcmf_sdcard_reg_read(sdiodev,
		CORE_SB(corebase, sbtmstatelow), 4);
	if (regdata & SBTML_RESET)
		return;

	regdata = brcmf_sdcard_reg_read(sdiodev,
		CORE_SB(corebase, sbtmstatelow), 4);
	if ((regdata & (SICF_CLOCK_EN << SBTML_SICF_SHIFT)) != 0) {
		/*
		 * set target reject and spin until busy is clear
		 * (preserve core-specific bits)
		 */
		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4);
		brcmf_sdcard_reg_write(sdiodev, CORE_SB(corebase, sbtmstatelow),
				       4, regdata | SBTML_REJ);

		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4);
		udelay(1);
		SPINWAIT((brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatehigh), 4) &
			SBTMH_BUSY), 100000);

		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatehigh), 4);
		if (regdata & SBTMH_BUSY)
			brcmf_dbg(ERROR, "core state still busy\n");

		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbidlow), 4);
		if (regdata & SBIDL_INIT) {
			regdata = brcmf_sdcard_reg_read(sdiodev,
				CORE_SB(corebase, sbimstate), 4) |
				SBIM_RJ;
			brcmf_sdcard_reg_write(sdiodev,
				CORE_SB(corebase, sbimstate), 4,
				regdata);
			regdata = brcmf_sdcard_reg_read(sdiodev,
				CORE_SB(corebase, sbimstate), 4);
			udelay(1);
			SPINWAIT((brcmf_sdcard_reg_read(sdiodev,
				CORE_SB(corebase, sbimstate), 4) &
				SBIM_BY), 100000);
		}

		/* set reset and reject while enabling the clocks */
		brcmf_sdcard_reg_write(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4,
			(((SICF_FGC | SICF_CLOCK_EN) << SBTML_SICF_SHIFT) |
			SBTML_REJ | SBTML_RESET));
		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4);
		udelay(10);

		/* clear the initiator reject bit */
		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbidlow), 4);
		if (regdata & SBIDL_INIT) {
			regdata = brcmf_sdcard_reg_read(sdiodev,
				CORE_SB(corebase, sbimstate), 4) &
				~SBIM_RJ;
			brcmf_sdcard_reg_write(sdiodev,
				CORE_SB(corebase, sbimstate), 4,
				regdata);
		}
	}

	/* leave reset and reject asserted */
	brcmf_sdcard_reg_write(sdiodev, CORE_SB(corebase, sbtmstatelow), 4,
		(SBTML_REJ | SBTML_RESET));
	udelay(1);
}

static int brcmf_sdio_chip_recognition(struct brcmf_sdio_dev *sdiodev,
				       struct chip_info *ci, u32 regs)
{
	u32 regdata;

	/*
	 * Get CC core rev
	 * Chipid is assume to be at offset 0 from regs arg
	 * For different chiptypes or old sdio hosts w/o chipcommon,
	 * other ways of recognition should be added here.
	 */
	ci->cccorebase = regs;
	regdata = brcmf_sdcard_reg_read(sdiodev,
				CORE_CC_REG(ci->cccorebase, chipid), 4);
	ci->chip = regdata & CID_ID_MASK;
	ci->chiprev = (regdata & CID_REV_MASK) >> CID_REV_SHIFT;

	brcmf_dbg(INFO, "chipid=0x%x chiprev=%d\n", ci->chip, ci->chiprev);

	/* Address of cores for new chips should be added here */
	switch (ci->chip) {
	case BCM4329_CHIP_ID:
		ci->buscorebase = BCM4329_CORE_BUS_BASE;
		ci->ramcorebase = BCM4329_CORE_SOCRAM_BASE;
		ci->armcorebase	= BCM4329_CORE_ARM_BASE;
		ci->ramsize = BCM4329_RAMSIZE;
		break;
	default:
		brcmf_dbg(ERROR, "chipid 0x%x is not supported\n", ci->chip);
		return -ENODEV;
	}

	return 0;
}

static int
brcmf_sdio_chip_buscoreprep(struct brcmf_sdio_dev *sdiodev)
{
	int err = 0;
	u8 clkval, clkset;

	/* Try forcing SDIO core to do ALPAvail request only */
	clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ;
	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1,
			       SBSDIO_FUNC1_CHIPCLKCSR,	clkset, &err);
	if (err) {
		brcmf_dbg(ERROR, "error writing for HT off\n");
		return err;
	}

	/* If register supported, wait for ALPAvail and then force ALP */
	/* This may take up to 15 milliseconds */
	clkval = brcmf_sdcard_cfg_read(sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_CHIPCLKCSR, NULL);

	if ((clkval & ~SBSDIO_AVBITS) != clkset) {
		brcmf_dbg(ERROR, "ChipClkCSR access: wrote 0x%02x read 0x%02x\n",
			  clkset, clkval);
		return -EACCES;
	}

	SPINWAIT(((clkval = brcmf_sdcard_cfg_read(sdiodev, SDIO_FUNC_1,
				SBSDIO_FUNC1_CHIPCLKCSR, NULL)),
			!SBSDIO_ALPAV(clkval)),
			PMU_MAX_TRANSITION_DLY);
	if (!SBSDIO_ALPAV(clkval)) {
		brcmf_dbg(ERROR, "timeout on ALPAV wait, clkval 0x%02x\n",
			  clkval);
		return -EBUSY;
	}

	clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP;
	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1,
			       SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
	udelay(65);

	/* Also, disable the extra SDIO pull-ups */
	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1,
			       SBSDIO_FUNC1_SDIOPULLUP, 0, NULL);

	return 0;
}

static void
brcmf_sdio_chip_buscoresetup(struct brcmf_sdio_dev *sdiodev,
			     struct chip_info *ci)
{
	u32 regdata;

	/* get chipcommon rev */
	regdata = brcmf_sdcard_reg_read(sdiodev,
		CORE_SB(ci->cccorebase, sbidhigh), 4);
	ci->ccrev = SBCOREREV(regdata);

	/* get chipcommon capabilites */
	ci->cccaps = brcmf_sdcard_reg_read(sdiodev,
		CORE_CC_REG(ci->cccorebase, capabilities), 4);

	/* get pmu caps & rev */
	if (ci->cccaps & CC_CAP_PMU) {
		ci->pmucaps = brcmf_sdcard_reg_read(sdiodev,
			CORE_CC_REG(ci->cccorebase, pmucapabilities), 4);
		ci->pmurev = ci->pmucaps & PCAP_REV_MASK;
	}

	regdata = brcmf_sdcard_reg_read(sdiodev,
					CORE_SB(ci->buscorebase, sbidhigh), 4);
	ci->buscorerev = SBCOREREV(regdata);
	ci->buscoretype = (regdata & SBIDH_CC_MASK) >> SBIDH_CC_SHIFT;

	brcmf_dbg(INFO, "ccrev=%d, pmurev=%d, buscore rev/type=%d/0x%x\n",
		  ci->ccrev, ci->pmurev, ci->buscorerev, ci->buscoretype);

	/*
	 * Make sure any on-chip ARM is off (in case strapping is wrong),
	 * or downloaded code was already running.
	 */
	brcmf_sdio_chip_coredisable(sdiodev, ci->armcorebase);
}

int brcmf_sdio_chip_attach(struct brcmf_sdio_dev *sdiodev,
			   struct chip_info *ci, u32 regs)
{
	int ret = 0;

	ret = brcmf_sdio_chip_buscoreprep(sdiodev);
	if (ret != 0)
		return ret;

	ret = brcmf_sdio_chip_recognition(sdiodev, ci, regs);
	if (ret != 0)
		return ret;

	brcmf_sdio_chip_buscoresetup(sdiodev, ci);

	brcmf_sdcard_reg_write(sdiodev,
		CORE_CC_REG(ci->cccorebase, gpiopullup), 4, 0);
	brcmf_sdcard_reg_write(sdiodev,
		CORE_CC_REG(ci->cccorebase, gpiopulldown), 4, 0);

	return ret;
}
