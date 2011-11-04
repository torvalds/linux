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
#include <linux/ssb/ssb_regs.h>

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

#define	SBCOREREV(sbidh) \
	((((sbidh) & SSB_IDHIGH_RCHI) >> SSB_IDHIGH_RCHI_SHIFT) | \
	  ((sbidh) & SSB_IDHIGH_RCLO))

#define SDIOD_DRVSTR_KEY(chip, pmu)     (((chip) << 16) | (pmu))
/* SDIO Pad drive strength to select value mappings */
struct sdiod_drive_str {
	u8 strength;	/* Pad Drive Strength in mA */
	u8 sel;		/* Chip-specific select value */
};
/* SDIO Drive Strength to sel value table for PMU Rev 1 */
static const struct sdiod_drive_str sdiod_drive_strength_tab1[] = {
	{
	4, 0x2}, {
	2, 0x3}, {
	1, 0x0}, {
	0, 0x0}
	};
/* SDIO Drive Strength to sel value table for PMU Rev 2, 3 */
static const struct sdiod_drive_str sdiod_drive_strength_tab2[] = {
	{
	12, 0x7}, {
	10, 0x6}, {
	8, 0x5}, {
	6, 0x4}, {
	4, 0x2}, {
	2, 0x1}, {
	0, 0x0}
	};
/* SDIO Drive Strength to sel value table for PMU Rev 8 (1.8V) */
static const struct sdiod_drive_str sdiod_drive_strength_tab3[] = {
	{
	32, 0x7}, {
	26, 0x6}, {
	22, 0x5}, {
	16, 0x4}, {
	12, 0x3}, {
	8, 0x2}, {
	4, 0x1}, {
	0, 0x0}
	};

static u32
brcmf_sdio_chip_corerev(struct brcmf_sdio_dev *sdiodev,
			u32 corebase)
{
	u32 regdata;

	regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbidhigh), 4);
	return SBCOREREV(regdata);
}

bool
brcmf_sdio_chip_iscoreup(struct brcmf_sdio_dev *sdiodev,
			 u32 corebase)
{
	u32 regdata;

	regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4);
	regdata &= (SSB_TMSLOW_RESET | SSB_TMSLOW_REJECT |
		    SSB_IMSTATE_REJECT | SSB_TMSLOW_CLOCK);
	return (SSB_TMSLOW_CLOCK == regdata);
}

void
brcmf_sdio_chip_coredisable(struct brcmf_sdio_dev *sdiodev, u32 corebase)
{
	u32 regdata;

	regdata = brcmf_sdcard_reg_read(sdiodev,
		CORE_SB(corebase, sbtmstatelow), 4);
	if (regdata & SSB_TMSLOW_RESET)
		return;

	regdata = brcmf_sdcard_reg_read(sdiodev,
		CORE_SB(corebase, sbtmstatelow), 4);
	if ((regdata & SSB_TMSLOW_CLOCK) != 0) {
		/*
		 * set target reject and spin until busy is clear
		 * (preserve core-specific bits)
		 */
		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4);
		brcmf_sdcard_reg_write(sdiodev, CORE_SB(corebase, sbtmstatelow),
				       4, regdata | SSB_TMSLOW_REJECT);

		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4);
		udelay(1);
		SPINWAIT((brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatehigh), 4) &
			SSB_TMSHIGH_BUSY), 100000);

		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatehigh), 4);
		if (regdata & SSB_TMSHIGH_BUSY)
			brcmf_dbg(ERROR, "core state still busy\n");

		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbidlow), 4);
		if (regdata & SSB_IDLOW_INITIATOR) {
			regdata = brcmf_sdcard_reg_read(sdiodev,
				CORE_SB(corebase, sbimstate), 4) |
				SSB_IMSTATE_REJECT;
			brcmf_sdcard_reg_write(sdiodev,
				CORE_SB(corebase, sbimstate), 4,
				regdata);
			regdata = brcmf_sdcard_reg_read(sdiodev,
				CORE_SB(corebase, sbimstate), 4);
			udelay(1);
			SPINWAIT((brcmf_sdcard_reg_read(sdiodev,
				CORE_SB(corebase, sbimstate), 4) &
				SSB_IMSTATE_BUSY), 100000);
		}

		/* set reset and reject while enabling the clocks */
		brcmf_sdcard_reg_write(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4,
			(SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK |
			SSB_TMSLOW_REJECT | SSB_TMSLOW_RESET));
		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbtmstatelow), 4);
		udelay(10);

		/* clear the initiator reject bit */
		regdata = brcmf_sdcard_reg_read(sdiodev,
			CORE_SB(corebase, sbidlow), 4);
		if (regdata & SSB_IDLOW_INITIATOR) {
			regdata = brcmf_sdcard_reg_read(sdiodev,
				CORE_SB(corebase, sbimstate), 4) &
				~SSB_IMSTATE_REJECT;
			brcmf_sdcard_reg_write(sdiodev,
				CORE_SB(corebase, sbimstate), 4,
				regdata);
		}
	}

	/* leave reset and reject asserted */
	brcmf_sdcard_reg_write(sdiodev, CORE_SB(corebase, sbtmstatelow), 4,
		(SSB_TMSLOW_REJECT | SSB_TMSLOW_RESET));
	udelay(1);
}

void
brcmf_sdio_chip_resetcore(struct brcmf_sdio_dev *sdiodev, u32 corebase)
{
	u32 regdata;

	/*
	 * Must do the disable sequence first to work for
	 * arbitrary current core state.
	 */
	brcmf_sdio_chip_coredisable(sdiodev, corebase);

	/*
	 * Now do the initialization sequence.
	 * set reset while enabling the clock and
	 * forcing them on throughout the core
	 */
	brcmf_sdcard_reg_write(sdiodev, CORE_SB(corebase, sbtmstatelow), 4,
		SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK | SSB_TMSLOW_RESET);
	udelay(1);

	regdata = brcmf_sdcard_reg_read(sdiodev,
					CORE_SB(corebase, sbtmstatehigh), 4);
	if (regdata & SSB_TMSHIGH_SERR)
		brcmf_sdcard_reg_write(sdiodev,
				       CORE_SB(corebase, sbtmstatehigh), 4, 0);

	regdata = brcmf_sdcard_reg_read(sdiodev,
					CORE_SB(corebase, sbimstate), 4);
	if (regdata & (SSB_IMSTATE_IBE | SSB_IMSTATE_TO))
		brcmf_sdcard_reg_write(sdiodev, CORE_SB(corebase, sbimstate), 4,
			regdata & ~(SSB_IMSTATE_IBE | SSB_IMSTATE_TO));

	/* clear reset and allow it to propagate throughout the core */
	brcmf_sdcard_reg_write(sdiodev, CORE_SB(corebase, sbtmstatelow), 4,
		SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK);
	udelay(1);

	/* leave clock enabled */
	brcmf_sdcard_reg_write(sdiodev, CORE_SB(corebase, sbtmstatelow),
			       4, SSB_TMSLOW_CLOCK);
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
	ci->ccrev = brcmf_sdio_chip_corerev(sdiodev, ci->cccorebase);

	/* get chipcommon capabilites */
	ci->cccaps = brcmf_sdcard_reg_read(sdiodev,
		CORE_CC_REG(ci->cccorebase, capabilities), 4);

	/* get pmu caps & rev */
	if (ci->cccaps & CC_CAP_PMU) {
		ci->pmucaps = brcmf_sdcard_reg_read(sdiodev,
			CORE_CC_REG(ci->cccorebase, pmucapabilities), 4);
		ci->pmurev = ci->pmucaps & PCAP_REV_MASK;
	}


	ci->buscorerev = brcmf_sdio_chip_corerev(sdiodev, ci->buscorebase);
	regdata = brcmf_sdcard_reg_read(sdiodev,
					CORE_SB(ci->buscorebase, sbidhigh), 4);
	ci->buscoretype = (regdata & SSB_IDHIGH_CC) >> SSB_IDHIGH_CC_SHIFT;

	brcmf_dbg(INFO, "ccrev=%d, pmurev=%d, buscore rev/type=%d/0x%x\n",
		  ci->ccrev, ci->pmurev, ci->buscorerev, ci->buscoretype);

	/*
	 * Make sure any on-chip ARM is off (in case strapping is wrong),
	 * or downloaded code was already running.
	 */
	brcmf_sdio_chip_coredisable(sdiodev, ci->armcorebase);
}

int brcmf_sdio_chip_attach(struct brcmf_sdio_dev *sdiodev,
			   struct chip_info **ci_ptr, u32 regs)
{
	int ret;
	struct chip_info *ci;

	brcmf_dbg(TRACE, "Enter\n");

	/* alloc chip_info_t */
	ci = kzalloc(sizeof(struct chip_info), GFP_ATOMIC);
	if (!ci)
		return -ENOMEM;

	ret = brcmf_sdio_chip_buscoreprep(sdiodev);
	if (ret != 0)
		goto err;

	ret = brcmf_sdio_chip_recognition(sdiodev, ci, regs);
	if (ret != 0)
		goto err;

	brcmf_sdio_chip_buscoresetup(sdiodev, ci);

	brcmf_sdcard_reg_write(sdiodev,
		CORE_CC_REG(ci->cccorebase, gpiopullup), 4, 0);
	brcmf_sdcard_reg_write(sdiodev,
		CORE_CC_REG(ci->cccorebase, gpiopulldown), 4, 0);

	*ci_ptr = ci;
	return 0;

err:
	kfree(ci);
	return ret;
}

void
brcmf_sdio_chip_detach(struct chip_info **ci_ptr)
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
				  struct chip_info *ci, u32 drivestrength)
{
	struct sdiod_drive_str *str_tab = NULL;
	u32 str_mask = 0;
	u32 str_shift = 0;
	char chn[8];

	if (!(ci->cccaps & CC_CAP_PMU))
		return;

	switch (SDIOD_DRVSTR_KEY(ci->chip, ci->pmurev)) {
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 1):
		str_tab = (struct sdiod_drive_str *)&sdiod_drive_strength_tab1;
		str_mask = 0x30000000;
		str_shift = 28;
		break;
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 2):
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 3):
		str_tab = (struct sdiod_drive_str *)&sdiod_drive_strength_tab2;
		str_mask = 0x00003800;
		str_shift = 11;
		break;
	case SDIOD_DRVSTR_KEY(BCM4336_CHIP_ID, 8):
		str_tab = (struct sdiod_drive_str *)&sdiod_drive_strength_tab3;
		str_mask = 0x00003800;
		str_shift = 11;
		break;
	default:
		brcmf_dbg(ERROR, "No SDIO Drive strength init done for chip %s rev %d pmurev %d\n",
			  brcmf_sdio_chip_name(ci->chip, chn, 8),
			  ci->chiprev, ci->pmurev);
		break;
	}

	if (str_tab != NULL) {
		u32 drivestrength_sel = 0;
		u32 cc_data_temp;
		int i;

		for (i = 0; str_tab[i].strength != 0; i++) {
			if (drivestrength >= str_tab[i].strength) {
				drivestrength_sel = str_tab[i].sel;
				break;
			}
		}

		brcmf_sdcard_reg_write(sdiodev,
			CORE_CC_REG(ci->cccorebase, chipcontrol_addr),
			4, 1);
		cc_data_temp = brcmf_sdcard_reg_read(sdiodev,
			CORE_CC_REG(ci->cccorebase, chipcontrol_addr), 4);
		cc_data_temp &= ~str_mask;
		drivestrength_sel <<= str_shift;
		cc_data_temp |= drivestrength_sel;
		brcmf_sdcard_reg_write(sdiodev,
			CORE_CC_REG(ci->cccorebase, chipcontrol_addr),
			4, cc_data_temp);

		brcmf_dbg(INFO, "SDIO: %dmA drive strength selected, set to 0x%08x\n",
			  drivestrength, cc_data_temp);
	}
}
