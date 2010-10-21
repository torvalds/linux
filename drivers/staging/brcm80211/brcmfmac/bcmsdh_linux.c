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

#include <linuxver.h>

#include <linux/pci.h>
#include <linux/completion.h>

#include <osl.h>
#include <pcicfg.h>
#include <bcmdefs.h>
#include <bcmdevs.h>

#if defined(OOB_INTR_ONLY)
#include <linux/irq.h>
extern void dhdsdio_isr(void *args);
#include <bcmutils.h>
#include <dngl_stats.h>
#include <dhd.h>
#endif				/* defined(OOB_INTR_ONLY) */
#if defined(CONFIG_MACH_SANDGATE2G) || defined(CONFIG_MACH_LOGICPD_PXA270)
#if !defined(BCMPLATFORM_BUS)
#define BCMPLATFORM_BUS
#endif				/* !defined(BCMPLATFORM_BUS) */

#include <linux/platform_device.h>
#endif				/* CONFIG_MACH_SANDGATE2G */

/**
 * SDIO Host Controller info
 */
typedef struct bcmsdh_hc bcmsdh_hc_t;

struct bcmsdh_hc {
	bcmsdh_hc_t *next;
#ifdef BCMPLATFORM_BUS
	struct device *dev;	/* platform device handle */
#else
	struct pci_dev *dev;	/* pci device handle */
#endif				/* BCMPLATFORM_BUS */
	osl_t *osh;
	void *regs;		/* SDIO Host Controller address */
	bcmsdh_info_t *sdh;	/* SDIO Host Controller handle */
	void *ch;
	unsigned int oob_irq;
	unsigned long oob_flags;	/* OOB Host specifiction
					as edge and etc */
	bool oob_irq_registered;
#if defined(OOB_INTR_ONLY)
	spinlock_t irq_lock;
#endif
};
static bcmsdh_hc_t *sdhcinfo;

/* driver info, initialized when bcmsdh_register is called */
static bcmsdh_driver_t drvinfo = { NULL, NULL };

/* debugging macros */
#define SDLX_MSG(x)

/**
 * Checks to see if vendor and device IDs match a supported SDIO Host Controller.
 */
bool bcmsdh_chipmatch(u16 vendor, u16 device)
{
	/* Add other vendors and devices as required */

#ifdef BCMSDIOH_STD
	/* Check for Arasan host controller */
	if (vendor == VENDOR_SI_IMAGE)
		return true;

	/* Check for BRCM 27XX Standard host controller */
	if (device == BCM27XX_SDIOH_ID && vendor == VENDOR_BROADCOM)
		return true;

	/* Check for BRCM Standard host controller */
	if (device == SDIOH_FPGA_ID && vendor == VENDOR_BROADCOM)
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
	if (device == SPIH_FPGA_ID && vendor == VENDOR_BROADCOM) {
		printf("Found PCI SPI Host Controller\n");
		return true;
	}
#endif				/* BCMSDIOH_SPI */

	return false;
}

#if defined(BCMPLATFORM_BUS)
#if defined(BCMLXSDMMC)
/* forward declarations */
int bcmsdh_probe(struct device *dev);
EXPORT_SYMBOL(bcmsdh_probe);

int bcmsdh_remove(struct device *dev);
EXPORT_SYMBOL(bcmsdh_remove);

#else
/* forward declarations */
static int __devinit bcmsdh_probe(struct device *dev);
static int __devexit bcmsdh_remove(struct device *dev);
#endif				/* BCMLXSDMMC */

#ifndef BCMLXSDMMC
static struct device_driver bcmsdh_driver = {
	.name = "pxa2xx-mci",
	.bus = &platform_bus_type,
	.probe = bcmsdh_probe,
	.remove = bcmsdh_remove,
	.suspend = NULL,
	.resume = NULL,
};
#endif				/* BCMLXSDMMC */

#ifndef BCMLXSDMMC
static
#endif				/* BCMLXSDMMC */
int bcmsdh_probe(struct device *dev)
{
	osl_t *osh = NULL;
	bcmsdh_hc_t *sdhc = NULL;
	unsigned long regs = 0;
	bcmsdh_info_t *sdh = NULL;
#if !defined(BCMLXSDMMC) && defined(BCMPLATFORM_BUS)
	struct platform_device *pdev;
	struct resource *r;
#endif				/* BCMLXSDMMC */
	int irq = 0;
	u32 vendevid;
	unsigned long irq_flags = 0;

#if !defined(BCMLXSDMMC) && defined(BCMPLATFORM_BUS)
	pdev = to_platform_device(dev);
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!r || irq == NO_IRQ)
		return -ENXIO;
#endif				/* BCMLXSDMMC */

#if defined(OOB_INTR_ONLY)
#ifdef HW_OOB
	irq_flags =
	    IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL |
	    IORESOURCE_IRQ_SHAREABLE;
#else
	irq_flags = IRQF_TRIGGER_FALLING;
#endif				/* HW_OOB */
	irq = dhd_customer_oob_irq_map(&irq_flags);
	if (irq < 0) {
		SDLX_MSG(("%s: Host irq is not defined\n", __func__));
		return 1;
	}
#endif				/* defined(OOB_INTR_ONLY) */
	/* allocate SDIO Host Controller state info */
	osh = osl_attach(dev, PCI_BUS, false);
	if (!osh) {
		SDLX_MSG(("%s: osl_attach failed\n", __func__));
		goto err;
	}
	sdhc = kzalloc(sizeof(bcmsdh_hc_t), GFP_ATOMIC);
	if (!sdhc) {
		SDLX_MSG(("%s: out of memory\n", __func__));
		goto err;
	}
	sdhc->osh = osh;

	sdhc->dev = (void *)dev;

#ifdef BCMLXSDMMC
	sdh = bcmsdh_attach(osh, (void *)0, (void **)&regs, irq);
	if (!sdh) {
		SDLX_MSG(("%s: bcmsdh_attach failed\n", __func__));
		goto err;
	}
#else
	sdh = bcmsdh_attach(osh, (void *)r->start, (void **)&regs, irq);
	if (!sdh) {
		SDLX_MSG(("%s: bcmsdh_attach failed\n", __func__));
		goto err;
	}
#endif				/* BCMLXSDMMC */
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
	vendevid = bcmsdh_query_device(sdh);

	/* try to attach to the target device */
	sdhc->ch = drvinfo.attach((vendevid >> 16), (vendevid & 0xFFFF),
				0, 0, 0, 0, (void *)regs, NULL, sdh);
	if (!sdhc->ch) {
		SDLX_MSG(("%s: device attach failed\n", __func__));
		goto err;
	}

	return 0;

	/* error handling */
err:
	if (sdhc) {
		if (sdhc->sdh)
			bcmsdh_detach(sdhc->osh, sdhc->sdh);
		kfree(sdhc);
	}
	if (osh)
		osl_detach(osh);
	return -ENODEV;
}

#ifndef BCMLXSDMMC
static
#endif				/* BCMLXSDMMC */
int bcmsdh_remove(struct device *dev)
{
	bcmsdh_hc_t *sdhc, *prev;
	osl_t *osh;

	sdhc = sdhcinfo;
	drvinfo.detach(sdhc->ch);
	bcmsdh_detach(sdhc->osh, sdhc->sdh);
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
	osh = sdhc->osh;
	kfree(sdhc);
	osl_detach(osh);

#if !defined(BCMLXSDMMC)
	dev_set_drvdata(dev, NULL);
#endif				/* !defined(BCMLXSDMMC) */

	return 0;
}

#else				/* BCMPLATFORM_BUS */

#if !defined(BCMLXSDMMC)
/* forward declarations for PCI probe and remove functions. */
static int __devinit bcmsdh_pci_probe(struct pci_dev *pdev,
				      const struct pci_device_id *ent);
static void __devexit bcmsdh_pci_remove(struct pci_dev *pdev);

/**
 * pci id table
 */
static struct pci_device_id bcmsdh_pci_devid[] __devinitdata = {
{
	.vendor = PCI_ANY_ID,
	.device = PCI_ANY_ID,
	.subvendor = PCI_ANY_ID,
	.subdevice = PCI_ANY_ID,
	.class = 0,
	.class_mask = 0,
	.driver_data = 0,
},
{0,}
};

MODULE_DEVICE_TABLE(pci, bcmsdh_pci_devid);

/**
 * SDIO Host Controller pci driver info
 */
static struct pci_driver bcmsdh_pci_driver = {
	.node = {},
	.name = "bcmsdh",
	.id_table = bcmsdh_pci_devid,
	.probe = bcmsdh_pci_probe,
	.remove = bcmsdh_pci_remove,
	.suspend = NULL,
	.resume = NULL,
};

extern uint sd_pci_slot;	/* Force detection to a particular PCI */
				/* slot only . Allows for having multiple */
				/* WL devices at once in a PC */
				/* Only one instance of dhd will be */
				/* usable at a time */
				/* Upper word is bus number, */
				/* lower word is slot number */
				/* Default value of 0xFFFFffff turns this */
				/* off */
module_param(sd_pci_slot, uint, 0);

/**
 * Detect supported SDIO Host Controller and attach if found.
 *
 * Determine if the device described by pdev is a supported SDIO Host
 * Controller.  If so, attach to it and attach to the target device.
 */
static int __devinit
bcmsdh_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	osl_t *osh = NULL;
	bcmsdh_hc_t *sdhc = NULL;
	unsigned long regs;
	bcmsdh_info_t *sdh = NULL;
	int rc;

	if (sd_pci_slot != 0xFFFFffff) {
		if (pdev->bus->number != (sd_pci_slot >> 16) ||
		    PCI_SLOT(pdev->devfn) != (sd_pci_slot & 0xffff)) {
			SDLX_MSG(("%s: %s: bus %X, slot %X, vend %X, dev %X\n",
				  __func__,
				  bcmsdh_chipmatch(pdev->vendor, pdev->device) ?
				  "Found compatible SDIOHC" :
				  "Probing unknown device",
				  pdev->bus->number, PCI_SLOT(pdev->devfn),
				  pdev->vendor, pdev->device));
			return -ENODEV;
		}
		SDLX_MSG(("%s: %s: bus %X, slot %X, vendor %X, device %X "
			"(good PCI location)\n", __func__,
			bcmsdh_chipmatch(pdev->vendor, pdev->device) ?
			"Using compatible SDIOHC" : "WARNING, forced use "
			"of unkown device",
		pdev->bus->number, PCI_SLOT(pdev->devfn), pdev->vendor,
		pdev->device));
	}

	if ((pdev->vendor == VENDOR_TI)
	    && ((pdev->device == PCIXX21_FLASHMEDIA_ID)
		|| (pdev->device == PCIXX21_FLASHMEDIA0_ID))) {
		u32 config_reg;

		SDLX_MSG(("%s: Disabling TI FlashMedia Controller.\n",
			  __func__));
		osh = osl_attach(pdev, PCI_BUS, false);
		if (!osh) {
			SDLX_MSG(("%s: osl_attach failed\n", __func__));
			goto err;
		}

		config_reg = OSL_PCI_READ_CONFIG(osh, 0x4c, 4);

		/*
		 * Set MMC_SD_DIS bit in FlashMedia Controller.
		 * Disbling the SD/MMC Controller in the FlashMedia Controller
		 * allows the Standard SD Host Controller to take over control
		 * of the SD Slot.
		 */
		config_reg |= 0x02;
		OSL_PCI_WRITE_CONFIG(osh, 0x4c, 4, config_reg);
		osl_detach(osh);
	}
	/* match this pci device with what we support */
	/* we can't solely rely on this to believe it is
		our SDIO Host Controller! */
	if (!bcmsdh_chipmatch(pdev->vendor, pdev->device))
		return -ENODEV;

	/* this is a pci device we might support */
	SDLX_MSG(("%s: Found possible SDIO Host Controller: "
		"bus %d slot %d func %d irq %d\n", __func__,
		pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn), pdev->irq));

	/* use bcmsdh_query_device() to get the vendor ID of the target device
	 * so it will eventually appear in the Broadcom string on the console
	 */

	/* allocate SDIO Host Controller state info */
	osh = osl_attach(pdev, PCI_BUS, false);
	if (!osh) {
		SDLX_MSG(("%s: osl_attach failed\n", __func__));
		goto err;
	}
	sdhc = kzalloc(sizeof(bcmsdh_hc_t), GFP_ATOMIC);
	if (!sdhc) {
		SDLX_MSG(("%s: out of memory\n", __func__));
		goto err;
	}
	sdhc->osh = osh;

	sdhc->dev = pdev;

	/* map to address where host can access */
	pci_set_master(pdev);
	rc = pci_enable_device(pdev);
	if (rc) {
		SDLX_MSG(("%s: Cannot enable PCI device\n", __func__));
		goto err;
	}
	sdh = bcmsdh_attach(osh, (void *)(unsigned long)pci_resource_start(pdev, 0),
			(void **)&regs, pdev->irq);
	if (!sdh) {
		SDLX_MSG(("%s: bcmsdh_attach failed\n", __func__));
		goto err;
	}

	sdhc->sdh = sdh;

	/* try to attach to the target device */
	sdhc->ch = drvinfo.attach(VENDOR_BROADCOM, /* pdev->vendor, */
				bcmsdh_query_device(sdh) & 0xFFFF, 0, 0, 0, 0,
				(void *)regs, NULL, sdh);
	if (!sdhc->ch) {
		SDLX_MSG(("%s: device attach failed\n", __func__));
		goto err;
	}

	/* chain SDIO Host Controller info together */
	sdhc->next = sdhcinfo;
	sdhcinfo = sdhc;

	return 0;

	/* error handling */
err:
	if (sdhc->sdh)
		bcmsdh_detach(sdhc->osh, sdhc->sdh);
	if (sdhc)
		kfree(sdhc);
	if (osh)
		osl_detach(osh);
	return -ENODEV;
}

/**
 * Detach from target devices and SDIO Host Controller
 */
static void __devexit bcmsdh_pci_remove(struct pci_dev *pdev)
{
	bcmsdh_hc_t *sdhc, *prev;
	osl_t *osh;

	/* find the SDIO Host Controller state for this
		 pdev and take it out from the list */
	for (sdhc = sdhcinfo, prev = NULL; sdhc; sdhc = sdhc->next) {
		if (sdhc->dev == pdev) {
			if (prev)
				prev->next = sdhc->next;
			else
				sdhcinfo = NULL;
			break;
		}
		prev = sdhc;
	}
	if (!sdhc)
		return;

	drvinfo.detach(sdhc->ch);

	bcmsdh_detach(sdhc->osh, sdhc->sdh);

	/* release SDIO Host Controller info */
	osh = sdhc->osh;
	kfree(sdhc);
	osl_detach(osh);
}
#endif				/* BCMLXSDMMC */
#endif				/* BCMPLATFORM_BUS */

extern int sdio_function_init(void);

int bcmsdh_register(bcmsdh_driver_t *driver)
{
	int error = 0;

	drvinfo = *driver;

#if defined(BCMPLATFORM_BUS)
#if defined(BCMLXSDMMC)
	SDLX_MSG(("Linux Kernel SDIO/MMC Driver\n"));
	error = sdio_function_init();
#else
	SDLX_MSG(("Intel PXA270 SDIO Driver\n"));
	error = driver_register(&bcmsdh_driver);
#endif				/* defined(BCMLXSDMMC) */
	return error;
#endif				/* defined(BCMPLATFORM_BUS) */

#if !defined(BCMPLATFORM_BUS) && !defined(BCMLXSDMMC)
	error = pci_register_driver(&bcmsdh_pci_driver);
	if (!error)
		return 0;

	SDLX_MSG(("%s: pci_register_driver failed 0x%x\n", __func__, error));
#endif				/* BCMPLATFORM_BUS */

	return error;
}

extern void sdio_function_cleanup(void);

void bcmsdh_unregister(void)
{
#if defined(BCMPLATFORM_BUS) && !defined(BCMLXSDMMC)
		driver_unregister(&bcmsdh_driver);
#endif
#if defined(BCMLXSDMMC)
	sdio_function_cleanup();
#endif				/* BCMLXSDMMC */
#if !defined(BCMPLATFORM_BUS) && !defined(BCMLXSDMMC)
	pci_unregister_driver(&bcmsdh_pci_driver);
#endif				/* BCMPLATFORM_BUS */
}

#if defined(OOB_INTR_ONLY)
void bcmsdh_oob_intr_set(bool enable)
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

static irqreturn_t wlan_oob_irq(int irq, void *dev_id)
{
	dhd_pub_t *dhdp;

	dhdp = (dhd_pub_t *) dev_get_drvdata(sdhcinfo->dev);

	bcmsdh_oob_intr_set(0);

	if (dhdp == NULL) {
		SDLX_MSG(("Out of band GPIO interrupt fired way too early\n"));
		return IRQ_HANDLED;
	}

	WAKE_LOCK_TIMEOUT(dhdp, WAKE_LOCK_TMOUT, 25);

	dhdsdio_isr((void *)dhdp->bus);

	return IRQ_HANDLED;
}

int bcmsdh_register_oob_intr(void *dhdp)
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
		    request_irq(sdhcinfo->oob_irq, wlan_oob_irq,
				sdhcinfo->oob_flags, "bcmsdh_sdmmc", NULL);
		if (error)
			return -ENODEV;

		set_irq_wake(sdhcinfo->oob_irq, 1);
		sdhcinfo->oob_irq_registered = true;
	}

	return 0;
}

void bcmsdh_unregister_oob_intr(void)
{
	SDLX_MSG(("%s: Enter\n", __func__));

	set_irq_wake(sdhcinfo->oob_irq, 0);
	disable_irq(sdhcinfo->oob_irq);	/* just in case.. */
	free_irq(sdhcinfo->oob_irq, NULL);
	sdhcinfo->oob_irq_registered = false;
}
#endif				/* defined(OOB_INTR_ONLY) */
/* Module parameters specific to each host-controller driver */

extern uint sd_msglevel;	/* Debug message level */
module_param(sd_msglevel, uint, 0);

extern uint sd_power;		/* 0 = SD Power OFF,
					 1 = SD Power ON. */
module_param(sd_power, uint, 0);

extern uint sd_clock;		/* SD Clock Control, 0 = SD Clock OFF,
				 1 = SD Clock ON */
module_param(sd_clock, uint, 0);

extern uint sd_divisor;		/* Divisor (-1 means external clock) */
module_param(sd_divisor, uint, 0);

extern uint sd_sdmode;		/* Default is SD4, 0=SPI, 1=SD1, 2=SD4 */
module_param(sd_sdmode, uint, 0);

extern uint sd_hiok;		/* Ok to use hi-speed mode */
module_param(sd_hiok, uint, 0);

extern uint sd_f2_blocksize;
module_param(sd_f2_blocksize, int, 0);
