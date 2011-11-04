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

	regdata = brcmf_sdcard_reg_read(sdiodev,
		CORE_SB(ci->cccorebase, sbidhigh), 4);
	ci->ccrev = SBCOREREV(regdata);

	regdata = brcmf_sdcard_reg_read(sdiodev,
		CORE_CC_REG(ci->cccorebase, pmucapabilities), 4);
	ci->pmurev = regdata & PCAP_REV_MASK;

	regdata = brcmf_sdcard_reg_read(sdiodev,
					CORE_SB(ci->buscorebase, sbidhigh), 4);
	ci->buscorerev = SBCOREREV(regdata);
	ci->buscoretype = (regdata & SBIDH_CC_MASK) >> SBIDH_CC_SHIFT;

	brcmf_dbg(INFO, "ccrev=%d, pmurev=%d, buscore rev/type=%d/0x%x\n",
		  ci->ccrev, ci->pmurev, ci->buscorerev, ci->buscoretype);

	/* get chipcommon capabilites */
	ci->cccaps = brcmf_sdcard_reg_read(sdiodev,
		CORE_CC_REG(ci->cccorebase, capabilities), 4);

	return 0;
}

int brcmf_sdio_chip_attach(struct brcmf_sdio_dev *sdiodev,
			   struct chip_info *ci, u32 regs)
{
	int ret = 0;

	ret = brcmf_sdio_chip_recognition(sdiodev, ci, regs);
	if (ret != 0)
		return ret;

	return ret;
}
