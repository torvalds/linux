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

#if defined(OOB_INTR_ONLY)
#include <linux/irq.h>
extern void brcmf_sdbrcm_isr(void *args);
#endif				/* defined(OOB_INTR_ONLY) */
#if defined(CONFIG_MACH_SANDGATE2G) || defined(CONFIG_MACH_LOGICPD_PXA270)
#if !defined(BCMPLATFORM_BUS)
#define BCMPLATFORM_BUS
#endif				/* !defined(BCMPLATFORM_BUS) */

#include <linux/platform_device.h>
#endif				/* CONFIG_MACH_SANDGATE2G */

#include "dngl_stats.h"
#include "dhd.h"

/**
 * SDIO Host Controller info
 */
struct bcmsdh_hc {
	struct bcmsdh_hc *next;
#ifdef BCMPLATFORM_BUS
	struct device *dev;	/* platform device handle */
#else
	struct pci_dev *dev;	/* pci device handle */
#endif				/* BCMPLATFORM_BUS */
	void *regs;		/* SDIO Host Controller address */
	struct brcmf_sdio *sdh;	/* SDIO Host Controller handle */
	void *ch;
	unsigned int oob_irq;
	unsigned long oob_flags;	/* OOB Host specifiction
					as edge and etc */
	bool oob_irq_registered;
#if defined(OOB_INTR_ONLY)
	spinlock_t irq_lock;
#endif
};
static struct bcmsdh_hc *sdhcinfo;

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

#if defined(BCMPLATFORM_BUS)
/* forward declarations */
int brcmf_sdio_probe(struct device *dev);
EXPORT_SYMBOL(brcmf_sdio_probe);

int brcmf_sdio_remove(struct device *dev);
EXPORT_SYMBOL(brcmf_sdio_remove);

int brcmf_sdio_probe(struct device *dev)
{
	struct bcmsdh_hc *sdhc = NULL;
	unsigned long regs = 0;
	struct brcmf_sdio *sdh = NULL;
	int irq = 0;
	u32 vendevid;
	unsigned long irq_flags = 0;

#if defined(OOB_INTR_ONLY)
#ifdef HW_OOB
	irq_flags =
	    IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL |
	    IORESOURCE_IRQ_SHAREABLE;
#else
	irq_flags = IRQF_TRIGGER_FALLING;
#endif				/* HW_OOB */
	irq = brcmf_customer_oob_irq_map(&irq_flags);
	if (irq < 0) {
		SDLX_MSG(("%s: Host irq is not defined\n", __func__));
		return 1;
	}
#endif				/* defined(OOB_INTR_ONLY) */
	/* allocate SDIO Host Controller state info */
	sdhc = kzalloc(sizeof(struct bcmsdh_hc), GFP_ATOMIC);
	if (!sdhc) {
		SDLX_MSG(("%s: out of memory\n", __func__));
		goto err;
	}
	sdhc->dev = (void *)dev;

	sdh = brcmf_sdcard_attach((void *)0, (void **)&regs, irq);
	if (!sdh) {
		SDLX_MSG(("%s: bcmsdh_attach failed\n", __func__));
		goto err;
	}

	sdhc->sdh = sdh;
	sdhc->oob_irq = irq;
	sdhc->oob_flags = irq_flags;
	sdhc->oob_irq_registered = false;	/* to make sure.. */
#if defined(OOB_INTR_ONLY)
	spin_lock_init(&sdhc->irq_lock);
#endif

	/* chain SDIO Host Controller info together */
	sdhc->next = sdhcinfo;
	sdhcinfo = sdhc;
	/* Read the vendor/device ID from the CIS */
	vendevid = brcmf_sdcard_query_device(sdh);

	/* try to attach to the target device */
	sdhc->ch = drvinfo.attach((vendevid >> 16), (vendevid & 0xFFFF),
				  0, 0, 0, 0, (void *)regs, sdh);
	if (!sdhc->ch) {
		SDLX_MSG(("%s: device attach failed\n", __func__));
		goto err;
	}

	return 0;

	/* error handling */
err:
	if (sdhc) {
		if (sdhc->sdh)
			brcmf_sdcard_detach(sdhc->sdh);
		kfree(sdhc);
	}

	return -ENODEV;
}

int brcmf_sdio_remove(struct device *dev)
{
	struct bcmsdh_hc *sdhc, *prev;

	sdhc = sdhcinfo;
	drvinfo.detach(sdhc->ch);
	brcmf_sdcard_detach(sdhc->sdh);
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
#endif				/* BCMPLATFORM_BUS */

extern int brcmf_sdio_function_init(void);

int brcmf_sdio_register(struct brcmf_sdioh_driver *driver)
{
	drvinfo = *driver;

	SDLX_MSG(("Linux Kernel SDIO/MMC Driver\n"));
	return brcmf_sdio_function_init();
}

extern void brcmf_sdio_function_cleanup(void);

void brcmf_sdio_unregister(void)
{
	brcmf_sdio_function_cleanup();
}

#if defined(OOB_INTR_ONLY)
void brcmf_sdio_oob_intr_set(bool enable)
{
	static bool curstate = 1;
	unsigned long flags;

	spin_lock_irqsave(&sdhcinfo->irq_lock, flags);
	if (curstate != enable) {
		if (enable)
			enable_irq(sdhcinfo->oob_irq);
		else
			disable_irq_nosync(sdhcinfo->oob_irq);
		curstate = enable;
	}
	spin_unlock_irqrestore(&sdhcinfo->irq_lock, flags);
}

static irqreturn_t brcmf_sdio_oob_irq(int irq, void *dev_id)
{
	dhd_pub_t *dhdp;

	dhdp = (dhd_pub_t *) dev_get_drvdata(sdhcinfo->dev);

	brcmf_sdio_oob_intr_set(0);

	if (dhdp == NULL) {
		SDLX_MSG(("Out of band GPIO interrupt fired way too early\n"));
		return IRQ_HANDLED;
	}

	brcmf_sdbrcm_isr((void *)dhdp->bus);

	return IRQ_HANDLED;
}

int brcmf_sdio_register_oob_intr(void *dhdp)
{
	int error = 0;

	SDLX_MSG(("%s Enter\n", __func__));

	sdhcinfo->oob_flags =
	    IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL |
	    IORESOURCE_IRQ_SHAREABLE;
	dev_set_drvdata(sdhcinfo->dev, dhdp);

	if (!sdhcinfo->oob_irq_registered) {
		SDLX_MSG(("%s IRQ=%d Type=%X\n", __func__,
			  (int)sdhcinfo->oob_irq, (int)sdhcinfo->oob_flags));
		/* Refer to customer Host IRQ docs about
			 proper irqflags definition */
		error =
		    request_irq(sdhcinfo->oob_irq, brcmf_sdio_oob_irq,
				sdhcinfo->oob_flags, "bcmsdh_sdmmc", NULL);
		if (error)
			return -ENODEV;

		irq_set_irq_wake(sdhcinfo->oob_irq, 1);
		sdhcinfo->oob_irq_registered = true;
	}

	return 0;
}

void brcmf_sdio_unregister_oob_intr(void)
{
	SDLX_MSG(("%s: Enter\n", __func__));

	irq_set_irq_wake(sdhcinfo->oob_irq, 0);
	disable_irq(sdhcinfo->oob_irq);	/* just in case.. */
	free_irq(sdhcinfo->oob_irq, NULL);
	sdhcinfo->oob_irq_registered = false;
}
#endif				/* defined(OOB_INTR_ONLY) */
/* Module parameters specific to each host-controller driver */

extern uint sd_msglevel;	/* Debug message level */
module_param(sd_msglevel, uint, 0);

extern uint sd_f2_blocksize;
module_param(sd_f2_blocksize, int, 0);
