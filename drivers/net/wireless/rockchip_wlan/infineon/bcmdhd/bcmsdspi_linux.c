/*
 * Broadcom SPI Host Controller Driver - Linux Per-port
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmsdspi_linux.c 514727 2014-11-12 03:02:48Z $
 */

#include <typedefs.h>
#include <bcmutils.h>

#include <bcmsdbus.h>		/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>		/* to get msglevel bit values */

#ifdef BCMSPI_ANDROID
#include <bcmsdh.h>
#include <bcmspibrcm.h>
#include <linux/spi/spi.h>
#else
#include <pcicfg.h>
#include <sdio.h>		/* SDIO Device and Protocol Specs */
#include <linux/sched.h>	/* request_irq(), free_irq() */
#include <bcmsdspi.h>
#include <bcmdevs.h>
#include <bcmspi.h>
#endif /* BCMSPI_ANDROID */

#ifndef GSPIBCM
#ifndef BCMSPI_ANDROID
extern uint sd_crc;
module_param(sd_crc, uint, 0);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define KERNEL26
#endif // endif
#endif /* !BCMSPI_ANDROID */

struct sdos_info {
	sdioh_info_t *sd;
	spinlock_t lock;
#ifndef BCMSPI_ANDROID
	wait_queue_head_t intr_wait_queue;
#endif /* !BCMSPI_ANDROID */
};
#endif /* !GSPIBCM */

#ifndef BCMSPI_ANDROID
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define BLOCKABLE()	(!in_atomic())
#else
#define BLOCKABLE()	(!in_interrupt())
#endif // endif

/* For Broadcom PCI-SPI Host controller (Raggedstone) */
#if defined(BCMSPI) && (defined(BCMPCISPIHOST) || defined(GSPIBCM))
#ifndef SDLX_MSG
#define SDLX_MSG(x) printf x
#endif // endif
extern void* bcmsdh_probe(osl_t *osh, void *dev, void *sdioh, void *adapter_info,
        uint bus_type, uint bus_num, uint slot_num);
extern int bcmsdh_remove(bcmsdh_info_t *bcmsdh);
extern sdioh_info_t * sdioh_attach(osl_t *osh, void *bar0, uint irq);
extern SDIOH_API_RC sdioh_detach(osl_t *osh, sdioh_info_t *sd);

/* forward declarations for PCI probe and remove functions. */
static int __devinit bcmsdh_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void __devexit bcmsdh_pci_remove(struct pci_dev *pdev);

/* forward declarations for PCI probe and remove functions. */
static int __devinit bcmsdh_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void __devexit bcmsdh_pci_remove(struct pci_dev *pdev);

/**
 * pci id table
 */
static struct pci_device_id bcmsdh_pci_devid[] __devinitdata = {
	{ vendor: PCI_ANY_ID,
	device: PCI_ANY_ID,
	subvendor: PCI_ANY_ID,
	subdevice: PCI_ANY_ID,
	class: 0,
	class_mask: 0,
	driver_data: 0,
	},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, bcmsdh_pci_devid);

/**
 * PCI-SPI Host Controller: pci driver info
 */
static struct pci_driver bcmsdh_pci_driver = {
	node:       {},
	name:       "bcmsdh",
	id_table:   bcmsdh_pci_devid,
	probe:      bcmsdh_pci_probe,
	remove:     bcmsdh_pci_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	save_state: NULL,
#endif // endif
	suspend:    NULL,
	resume:     NULL,
};

/* Force detection to a particular PCI */
/* slot only . Allows for having multiple */
/* WL devices at once in a PC */
/* Only one instance of dhd will be */
/* usable at a time */
/* Upper word is bus number, */
/* lower word is slot number */
/* Default value of 0xffffffff turns this off */
extern uint sd_pci_slot;
module_param(sd_pci_slot, uint, 0);

/**
 * Detect supported Host Controller and attach if found.
 *
 * Determine if the device described by pdev is a supported PCI Host
 * Controller.  If so, attach to it and attach to the target device.
 */
static int __devinit
bcmsdh_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	osl_t *osh = NULL;
	sdioh_info_t *sdioh = NULL;
	void *bcmsdh;

	int rc;

	if (sd_pci_slot != 0xFFFFFFFF) {
		if (pdev->bus->number != (sd_pci_slot>>16) ||
				PCI_SLOT(pdev->devfn) != (sd_pci_slot&0xffff)) {
			SDLX_MSG(("%s: %s: bus %X, slot %X, vend %X, dev %X\n",
				__FUNCTION__,
				bcmsdh_chipmatch(pdev->vendor, pdev->device)
				?"Found compatible SDIOHC"
				:"Probing unknown device",
				pdev->bus->number, PCI_SLOT(pdev->devfn), pdev->vendor,
				pdev->device));
			return -ENODEV;
		}
		SDLX_MSG(("%s: %s: bus %X, slot %X, vendor %X, device %X (good PCI location)\n",
			__FUNCTION__,
			bcmsdh_chipmatch(pdev->vendor, pdev->device)
			?"Using compatible SDIOHC"
			:"WARNING, forced use of unkown device",
			pdev->bus->number, PCI_SLOT(pdev->devfn), pdev->vendor, pdev->device));
	}

	if ((pdev->vendor == VENDOR_TI) && ((pdev->device == PCIXX21_FLASHMEDIA_ID) ||
		(pdev->device == PCIXX21_FLASHMEDIA0_ID))) {
		uint32 config_reg;

		SDLX_MSG(("%s: Disabling TI FlashMedia Controller.\n", __FUNCTION__));
		if (!(osh = osl_attach(pdev, PCI_BUS, FALSE))) {
			SDLX_MSG(("%s: osl_attach failed\n", __FUNCTION__));
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
	/* we can't solely rely on this to believe it is our SDIO Host Controller! */
	if (!bcmsdh_chipmatch(pdev->vendor, pdev->device)) {
		if (pdev->vendor == VENDOR_BROADCOM) {
			SDLX_MSG(("%s: Unknown Broadcom device (vendor: %#x, device: %#x).\n",
				__FUNCTION__, pdev->vendor, pdev->device));
		}
		return -ENODEV;
	}

	/* this is a pci device we might support */
	SDLX_MSG(("%s: Found possible SDIO Host Controller: bus %d slot %d func %d irq %d\n",
		__FUNCTION__,
		pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn), pdev->irq));

	/* use bcmsdh_query_device() to get the vendor ID of the target device so
	 * it will eventually appear in the Broadcom string on the console
	 */

	/* allocate SDIO Host Controller state info */
	if (!(osh = osl_attach(pdev, PCI_BUS, FALSE))) {
		SDLX_MSG(("%s: osl_attach failed\n", __FUNCTION__));
		goto err;
	}

	/* map to address where host can access */
	pci_set_master(pdev);
	rc = pci_enable_device(pdev);
	if (rc) {
		SDLX_MSG(("%s: Cannot enable PCI device\n", __FUNCTION__));
		goto err;
	}

	if (!(sdioh = sdioh_attach(osh, (void *)(ulong)pci_resource_start(pdev, 0), pdev->irq))) {
		SDLX_MSG(("%s: bcmsdh_attach failed\n", __FUNCTION__));
		goto err;
	}

#ifdef GSPIBCM
	bcmsdh = bcmsdh_probe(osh, &pdev->dev, sdioh, NULL, PCI_BUS, -1, -1);
	if (bcmsdh == NULL) {
#else
	sdioh->bcmsdh = bcmsdh_probe(osh, &pdev->dev, sdioh, NULL, PCI_BUS, -1, -1);
	if (sdioh->bcmsdh == NULL) {
#endif /* GSPIBCM */
		sd_err(("%s: bcmsdh_probe failed\n", __FUNCTION__));
		goto err;
	}

	pci_set_drvdata(pdev, sdioh);
	return 0;

	/* error handling */
err:
	if (sdioh != NULL)
		sdioh_detach(osh, sdioh);
	if (osh != NULL)
		osl_detach(osh);
	return -ENOMEM;

}

/**
 * Detach from target devices and PCI-SPI Host Controller
 */
static void __devexit
bcmsdh_pci_remove(struct pci_dev *pdev)
{

	sdioh_info_t *sdioh = NULL;
	osl_t *osh;

	sdioh = pci_get_drvdata(pdev);
	if (sdioh == NULL) {
		sd_err(("%s: error, no sdh handler found\n", __FUNCTION__));
		return;
	}

	osh = sdioh->osh;
	bcmsdh_remove(sdioh->bcmsdh);
	sdioh_detach(osh, sdioh);
	osl_detach(osh);

}
#endif /* BCMSPI && BCMPCISPIHOST */

#ifndef GSPIBCM
/* Interrupt handler */
static irqreturn_t
sdspi_isr(int irq, void *dev_id
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
, struct pt_regs *ptregs
#endif // endif
)
{
	sdioh_info_t *sd;
	struct sdos_info *sdos;
	bool ours;

	sd = (sdioh_info_t *)dev_id;
	sd->local_intrcount++;

	if (!sd->card_init_done) {
		sd_err(("%s: Hey Bogus intr...not even initted: irq %d\n", __FUNCTION__, irq));
		return IRQ_RETVAL(FALSE);
	} else {
		ours = spi_check_client_intr(sd, NULL);

		/* For local interrupts, wake the waiting process */
		if (ours && sd->got_hcint) {
			sdos = (struct sdos_info *)sd->sdos_info;
			wake_up_interruptible(&sdos->intr_wait_queue);
		}

		return IRQ_RETVAL(ours);
	}
}
#endif /* !GSPIBCM */
#endif /* !BCMSPI_ANDROID */

#ifdef BCMSPI_ANDROID
static struct spi_device *gBCMSPI = NULL;

extern int bcmsdh_probe(struct device *dev);
extern int bcmsdh_remove(struct device *dev);

static int bcmsdh_spi_probe(struct spi_device *spi_dev)
{
	int ret = 0;

	gBCMSPI = spi_dev;

#ifdef SPI_PIO_32BIT_RW
	spi_dev->bits_per_word = 32;
#else
	spi_dev->bits_per_word = 8;
#endif /* SPI_PIO_32BIT_RW */
	ret = spi_setup(spi_dev);

	if (ret) {
		sd_err(("bcmsdh_spi_probe: spi_setup fail with %d\n", ret));
	}
	sd_err(("bcmsdh_spi_probe: spi_setup with %d, bits_per_word=%d\n",
		ret, spi_dev->bits_per_word));
	ret = bcmsdh_probe(&spi_dev->dev);

	return ret;
}

static int  bcmsdh_spi_remove(struct spi_device *spi_dev)
{
	int ret = 0;

	ret = bcmsdh_remove(&spi_dev->dev);
	gBCMSPI = NULL;

	return ret;
}

static struct spi_driver bcmsdh_spi_driver = {
	.probe		= bcmsdh_spi_probe,
	.remove		= bcmsdh_spi_remove,
	.driver		= {
		.name = "wlan_spi",
		.bus    = &spi_bus_type,
		.owner  = THIS_MODULE,
		},
};
#endif /* BCMSPI_ANDROID */

/*
 * module init
*/
int bcmsdh_register_client_driver(void)
{
	int error = 0;
	sd_trace(("bcmsdh_gspi: %s Enter\n", __FUNCTION__));
#if defined(BCMSPI) && (defined(BCMPCISPIHOST) || defined(GSPIBCM))
	error = pci_module_init(&bcmsdh_pci_driver);
#else
	error = spi_register_driver(&bcmsdh_spi_driver);
#endif /* BCMSPI && BCMPCISPIHOST */

	return error;
}

/*
 * module cleanup
*/
void bcmsdh_unregister_client_driver(void)
{
	sd_trace(("%s Enter\n", __FUNCTION__));
#if defined(BCMSPI) && (defined(BCMPCISPIHOST) || defined(GSPIBCM))
	pci_unregister_driver(&bcmsdh_pci_driver);
#else
	spi_unregister_driver(&bcmsdh_spi_driver);
#endif /* BCMSPI && BCMPCISPIHOST */
}

#ifndef GSPIBCM
/* Register with Linux for interrupts */
int
spi_register_irq(sdioh_info_t *sd, uint irq)
{
#ifndef BCMSPI_ANDROID
	sd_trace(("Entering %s: irq == %d\n", __FUNCTION__, irq));
	if (request_irq(irq, sdspi_isr, IRQF_SHARED, "bcmsdspi", sd) < 0) {
		sd_err(("%s: request_irq() failed\n", __FUNCTION__));
		return ERROR;
	}
#endif /* !BCMSPI_ANDROID */
	return SUCCESS;
}

/* Free Linux irq */
void
spi_free_irq(uint irq, sdioh_info_t *sd)
{
#ifndef BCMSPI_ANDROID
	free_irq(irq, sd);
#endif /* !BCMSPI_ANDROID */
}

/* Map Host controller registers */
#ifndef BCMSPI_ANDROID
uint32 *
spi_reg_map(osl_t *osh, uintptr addr, int size)
{
	return (uint32 *)REG_MAP(addr, size);
}

void
spi_reg_unmap(osl_t *osh, uintptr addr, int size)
{
	REG_UNMAP((void*)(uintptr)addr);
}
#endif /* !BCMSPI_ANDROID */

int
spi_osinit(sdioh_info_t *sd)
{
	struct sdos_info *sdos;

	sdos = (struct sdos_info*)MALLOC(sd->osh, sizeof(struct sdos_info));
	sd->sdos_info = (void*)sdos;
	if (sdos == NULL)
		return BCME_NOMEM;

	sdos->sd = sd;
	spin_lock_init(&sdos->lock);
#ifndef BCMSPI_ANDROID
	init_waitqueue_head(&sdos->intr_wait_queue);
#endif /* !BCMSPI_ANDROID */
	return BCME_OK;
}

void
spi_osfree(sdioh_info_t *sd)
{
	struct sdos_info *sdos;
	ASSERT(sd && sd->sdos_info);

	sdos = (struct sdos_info *)sd->sdos_info;
	MFREE(sd->osh, sdos, sizeof(struct sdos_info));
}

/* Interrupt enable/disable */
SDIOH_API_RC
sdioh_interrupt_set(sdioh_info_t *sd, bool enable)
{
	ulong flags;
	struct sdos_info *sdos;

	sd_trace(("%s: %s\n", __FUNCTION__, enable ? "Enabling" : "Disabling"));

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	if (!(sd->host_init_done && sd->card_init_done)) {
		sd_err(("%s: Card & Host are not initted - bailing\n", __FUNCTION__));
		return SDIOH_API_RC_FAIL;
	}

#ifndef BCMSPI_ANDROID
	if (enable && !(sd->intr_handler && sd->intr_handler_arg)) {
		sd_err(("%s: no handler registered, will not enable\n", __FUNCTION__));
		return SDIOH_API_RC_FAIL;
	}
#endif /* !BCMSPI_ANDROID */

	/* Ensure atomicity for enable/disable calls */
	spin_lock_irqsave(&sdos->lock, flags);

	sd->client_intr_enabled = enable;
#ifndef BCMSPI_ANDROID
	if (enable && !sd->lockcount)
		spi_devintr_on(sd);
	else
		spi_devintr_off(sd);
#endif /* !BCMSPI_ANDROID */

	spin_unlock_irqrestore(&sdos->lock, flags);

	return SDIOH_API_RC_SUCCESS;
}

/* Protect against reentrancy (disable device interrupts while executing) */
void
spi_lock(sdioh_info_t *sd)
{
	ulong flags;
	struct sdos_info *sdos;

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	sd_trace(("%s: %d\n", __FUNCTION__, sd->lockcount));

	spin_lock_irqsave(&sdos->lock, flags);
	if (sd->lockcount) {
		sd_err(("%s: Already locked!\n", __FUNCTION__));
		ASSERT(sd->lockcount == 0);
	}
#ifdef BCMSPI_ANDROID
	if (sd->client_intr_enabled)
		bcmsdh_oob_intr_set(0);
#else
	spi_devintr_off(sd);
#endif /* BCMSPI_ANDROID */
	sd->lockcount++;
	spin_unlock_irqrestore(&sdos->lock, flags);
}

/* Enable client interrupt */
void
spi_unlock(sdioh_info_t *sd)
{
	ulong flags;
	struct sdos_info *sdos;

	sd_trace(("%s: %d, %d\n", __FUNCTION__, sd->lockcount, sd->client_intr_enabled));
	ASSERT(sd->lockcount > 0);

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	spin_lock_irqsave(&sdos->lock, flags);
	if (--sd->lockcount == 0 && sd->client_intr_enabled) {
#ifdef BCMSPI_ANDROID
		bcmsdh_oob_intr_set(1);
#else
		spi_devintr_on(sd);
#endif /* BCMSPI_ANDROID */
	}
	spin_unlock_irqrestore(&sdos->lock, flags);
}

#ifndef BCMSPI_ANDROID
void spi_waitbits(sdioh_info_t *sd, bool yield)
{
#ifndef BCMSDYIELD
	ASSERT(!yield);
#endif // endif
	sd_trace(("%s: yield %d canblock %d\n",
	          __FUNCTION__, yield, BLOCKABLE()));

	/* Clear the "interrupt happened" flag and last intrstatus */
	sd->got_hcint = FALSE;

#ifdef BCMSDYIELD
	if (yield && BLOCKABLE()) {
		struct sdos_info *sdos;
		sdos = (struct sdos_info *)sd->sdos_info;
		/* Wait for the indication, the interrupt will be masked when the ISR fires. */
		wait_event_interruptible(sdos->intr_wait_queue, (sd->got_hcint));
	} else
#endif /* BCMSDYIELD */
	{
		spi_spinbits(sd);
	}

}
#else /* !BCMSPI_ANDROID */
int bcmgspi_dump = 0;		/* Set to dump complete trace of all SPI bus transactions */

static void
hexdump(char *pfx, unsigned char *msg, int msglen)
{
	int i, col;
	char buf[80];

	ASSERT(strlen(pfx) + 49 <= sizeof(buf));

	col = 0;

	for (i = 0; i < msglen; i++, col++) {
		if (col % 16 == 0)
			strcpy(buf, pfx);
		sprintf(buf + strlen(buf), "%02x", msg[i]);
		if ((col + 1) % 16 == 0)
			printf("%s\n", buf);
		else
			sprintf(buf + strlen(buf), " ");
	}

	if (col % 16 != 0)
		printf("%s\n", buf);
}

/* Send/Receive an SPI Packet */
void
spi_sendrecv(sdioh_info_t *sd, uint8 *msg_out, uint8 *msg_in, int msglen)
{
	int write = 0;
	int tx_len = 0;
	struct spi_message msg;
	struct spi_transfer t[2];

	spi_message_init(&msg);
	memset(t, 0, 2*sizeof(struct spi_transfer));

	if (sd->wordlen == 2)
#if !(defined(SPI_PIO_RW_BIGENDIAN) && defined(SPI_PIO_32BIT_RW))
		write = msg_out[2] & 0x80;
#else
		write = msg_out[1] & 0x80;
#endif /* !(defined(SPI_PIO_RW_BIGENDIAN) && defined(SPI_PIO_32BIT_RW)) */
	if (sd->wordlen == 4)
#if !(defined(SPI_PIO_RW_BIGENDIAN) && defined(SPI_PIO_32BIT_RW))
		write = msg_out[0] & 0x80;
#else
		write = msg_out[3] & 0x80;
#endif /* !(defined(SPI_PIO_RW_BIGENDIAN) && defined(SPI_PIO_32BIT_RW)) */

	if (bcmgspi_dump) {
		hexdump(" OUT: ", msg_out, msglen);
	}

	tx_len = write ? msglen-4 : 4;

	sd_trace(("spi_sendrecv: %s, wordlen %d, cmd : 0x%02x 0x%02x 0x%02x 0x%02x\n",
		write ? "WR" : "RD", sd->wordlen,
		msg_out[0], msg_out[1], msg_out[2], msg_out[3]));

	t[0].tx_buf = (char *)&msg_out[0];
	t[0].rx_buf = 0;
	t[0].len = tx_len;

	spi_message_add_tail(&t[0], &msg);

	t[1].rx_buf = (char *)&msg_in[tx_len];
	t[1].tx_buf = 0;
	t[1].len = msglen-tx_len;

	spi_message_add_tail(&t[1], &msg);
	spi_sync(gBCMSPI, &msg);

	if (bcmgspi_dump) {
		hexdump(" IN  : ", msg_in, msglen);
	}
}
#endif /* !BCMSPI_ANDROID */
#endif /* !GSPIBCM */
