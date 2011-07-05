/*
 * Copyright (c) 2010 Broadcom Corporation
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

/**
 * @file bcmsdh_linux.c
 */

#define __UNDEF_NO_VERSION__

#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/completion.h>
#include <linux/sched.h>

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include "sdio_host.h"

#include "dngl_stats.h"
#include "dhd.h"
#include "dhd_bus.h"
#include "bcmsdbus.h"

/**
 * SDIO Host Controller info
 */
struct sdio_hc {
	struct sdio_hc *next;
	struct device *dev;	/* platform device handle */
	void *regs;		/* SDIO Host Controller address */
	struct brcmf_sdio_card *card;
	void *ch;
	unsigned int oob_irq;
	unsigned long oob_flags;	/* OOB Host specifiction
					as edge and etc */
	bool oob_irq_registered;
};
static struct sdio_hc *sdhcinfo;

/* driver info, initialized when brcmf_sdio_register is called */
static struct brcmf_sdioh_driver drvinfo = { NULL, NULL };

/* debugging macros */
#define SDLX_MSG(x)

/**
 * Checks to see if vendor and device IDs match a supported SDIO Host Controller.
 */
bool brcmf_sdio_chipmatch(u16 vendor, u16 device)
{
	/* Add other vendors and devices as required */

#ifdef BCMSDIOH_STD
	/* Check for Arasan host controller */
	if (vendor == VENDOR_SI_IMAGE)
		return true;

	/* Check for BRCM 27XX Standard host controller */
	if (device == BCM27XX_SDIOH_ID && vendor == PCI_VENDOR_ID_BROADCOM)
		return true;

	/* Check for BRCM Standard host controller */
	if (device == SDIOH_FPGA_ID && vendor == PCI_VENDOR_ID_BROADCOM)
		return true;

	/* Check for TI PCIxx21 Standard host controller */
	if (device == PCIXX21_SDIOH_ID && vendor == VENDOR_TI)
		return true;

	if (device == PCIXX21_SDIOH0_ID && vendor == VENDOR_TI)
		return true;

	/* Ricoh R5C822 Standard SDIO Host */
	if (device == R5C822_SDIOH_ID && vendor == VENDOR_RICOH)
		return true;

	/* JMicron Standard SDIO Host */
	if (device == JMICRON_SDIOH_ID && vendor == VENDOR_JMICRON)
		return true;
#endif				/* BCMSDIOH_STD */
#ifdef BCMSDIOH_SPI
	/* This is the PciSpiHost. */
	if (device == SPIH_FPGA_ID && vendor == PCI_VENDOR_ID_BROADCOM) {
		return true;
	}
#endif				/* BCMSDIOH_SPI */

	return false;
}

/* forward declarations */
int brcmf_sdio_probe(struct device *dev);
EXPORT_SYMBOL(brcmf_sdio_probe);

int brcmf_sdio_remove(struct device *dev);
EXPORT_SYMBOL(brcmf_sdio_remove);

int brcmf_sdio_probe(struct device *dev)
{
	struct sdio_hc *sdhc = NULL;
	unsigned long regs = 0;
	struct brcmf_sdio_card *card = NULL;
	int irq = 0;
	u32 vendevid;
	unsigned long irq_flags = 0;

	/* allocate SDIO Host Controller state info */
	sdhc = kzalloc(sizeof(struct sdio_hc), GFP_ATOMIC);
	if (!sdhc) {
		SDLX_MSG(("%s: out of memory\n", __func__));
		goto err;
	}
	sdhc->dev = (void *)dev;

	card = brcmf_sdcard_attach((void *)0, (void **)&regs, irq);
	if (!card) {
		SDLX_MSG(("%s: attach failed\n", __func__));
		goto err;
	}

	sdhc->card = card;
	sdhc->oob_irq = irq;
	sdhc->oob_flags = irq_flags;
	sdhc->oob_irq_registered = false;	/* to make sure.. */

	/* chain SDIO Host Controller info together */
	sdhc->next = sdhcinfo;
	sdhcinfo = sdhc;
	/* Read the vendor/device ID from the CIS */
	vendevid = brcmf_sdcard_query_device(card);

	/* try to attach to the target device */
	sdhc->ch = drvinfo.attach((vendevid >> 16), (vendevid & 0xFFFF),
				  0, 0, 0, 0, (void *)regs, card);
	if (!sdhc->ch) {
		SDLX_MSG(("%s: device attach failed\n", __func__));
		goto err;
	}

	return 0;

	/* error handling */
err:
	if (sdhc) {
		if (sdhc->card)
			brcmf_sdcard_detach(sdhc->card);
		kfree(sdhc);
	}

	return -ENODEV;
}

int brcmf_sdio_remove(struct device *dev)
{
	struct sdio_hc *sdhc, *prev;

	sdhc = sdhcinfo;
	drvinfo.detach(sdhc->ch);
	brcmf_sdcard_detach(sdhc->card);
	/* find the SDIO Host Controller state for this pdev
		 and take it out from the list */
	for (sdhc = sdhcinfo, prev = NULL; sdhc; sdhc = sdhc->next) {
		if (sdhc->dev == (void *)dev) {
			if (prev)
				prev->next = sdhc->next;
			else
				sdhcinfo = NULL;
			break;
		}
		prev = sdhc;
	}
	if (!sdhc) {
		SDLX_MSG(("%s: failed\n", __func__));
		return 0;
	}

	/* release SDIO Host Controller info */
	kfree(sdhc);
	return 0;
}

int brcmf_sdio_register(struct brcmf_sdioh_driver *driver)
{
	drvinfo = *driver;

	SDLX_MSG(("Linux Kernel SDIO/MMC Driver\n"));
	return brcmf_sdio_function_init();
}

void brcmf_sdio_unregister(void)
{
	brcmf_sdio_function_cleanup();
}

/* Module parameters specific to each host-controller driver */

module_param(sd_msglevel, uint, 0);

extern uint sd_f2_blocksize;
module_param(sd_f2_blocksize, int, 0);

void brcmf_sdio_wdtmr_enable(bool enable)
{
	if (enable)
		brcmf_sdbrcm_wd_timer(sdhcinfo->ch, brcmf_watchdog_ms);
	else
		brcmf_sdbrcm_wd_timer(sdhcinfo->ch, 0);
}
