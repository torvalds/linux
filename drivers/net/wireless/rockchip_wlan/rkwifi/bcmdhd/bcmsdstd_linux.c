/*
 *  'Standard' SDIO HOST CONTROLLER driver - linux portion
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <linux/sched.h>	/* request_irq() */
#include <typedefs.h>
#include <pcicfg.h>
#include <bcmutils.h>
#include <sdio.h>	/* SDIO Device and Protocol Specs */
#include <sdioh.h> /* SDIO Host Controller Spec header file */
#include <bcmsdbus.h>	/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>	/* to get msglevel bit values */
#include <bcmsdstd.h>
#include <bcmdevs.h>

extern void* bcmsdh_probe(osl_t *osh, void *dev, void *sdioh, void *adapter_info, uint bus_type,
	uint bus_num, uint slot_num);
extern int bcmsdh_remove(bcmsdh_info_t *bcmsdh);

/* Extern functions for sdio power save */
extern uint8 sdstd_turn_on_clock(sdioh_info_t *sd);
extern uint8 sdstd_turn_off_clock(sdioh_info_t *sd);
/* Extern variable for sdio power save. This is enabled or disabled using the IOCTL call */
extern uint sd_3_power_save;

struct sdos_info {
	sdioh_info_t *sd;
	spinlock_t lock;
	wait_queue_head_t intr_wait_queue;
	timer_list_compat_t tuning_timer;
	int tuning_timer_exp;
	atomic_t timer_enab;
	struct tasklet_struct tuning_tasklet;
};

#define SDSTD_WAITBITS_TIMEOUT		(5 * HZ)	/* seconds * HZ */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define BLOCKABLE()	(!in_atomic())
#else
#define BLOCKABLE()	(!in_interrupt()) /* XXX Doesn't handle CONFIG_PREEMPT? */
#endif

static void
sdstd_3_ostasklet(ulong data);
static void
sdstd_3_tuning_timer(ulong data);

/* Interrupt handler */
static irqreturn_t
sdstd_isr(int irq, void *dev_id
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
, struct pt_regs *ptregs
#endif
)
{
	sdioh_info_t *sd;
	struct sdos_info *sdos;
	bool ours;

	unsigned long flags;
	sd = (sdioh_info_t *)dev_id;
	sdos = (struct sdos_info *)sd->sdos_info;

	if (!sd->card_init_done) {
		sd_err(("%s: Hey Bogus intr...not even initted: irq %d\n", __FUNCTION__, irq));
		return IRQ_RETVAL(FALSE);
	} else {
		if (sdstd_3_is_retuning_int_set(sd)) {
			/* for 3.0 host, retuning request might come in this path */
			/* * disable ISR's */
			local_irq_save(flags);

			if (sdstd_3_check_and_set_retuning(sd))
				tasklet_schedule(&sdos->tuning_tasklet);

			/* * enable back ISR's */
			local_irq_restore(flags);

			/* * disable tuning isr signaling */
			sdstd_3_disable_retuning_int(sd);
			/* * note: check_client_intr() checks for  intmask also to
				wakeup. so be careful to use sd->intmask to disable
				re-tuning ISR.
				*/
		}
		ours = check_client_intr(sd);

		/* For local interrupts, wake the waiting process */
		if (ours && sd->got_hcint) {
			sd_trace(("INTR->WAKE\n"));
/* 			sdos = (struct sdos_info *)sd->sdos_info; */
			wake_up_interruptible(&sdos->intr_wait_queue);
		}
		return IRQ_RETVAL(ours);
	}
}

/* Register with Linux for interrupts */
int
sdstd_register_irq(sdioh_info_t *sd, uint irq)
{
	sd_trace(("Entering %s: irq == %d\n", __FUNCTION__, irq));
	if (request_irq(irq, sdstd_isr, IRQF_SHARED, "bcmsdstd", sd) < 0) {
		sd_err(("%s: request_irq() failed\n", __FUNCTION__));
		return ERROR;
	}
	return SUCCESS;
}

/* Free Linux irq */
void
sdstd_free_irq(uint irq, sdioh_info_t *sd)
{
	free_irq(irq, sd);
}

/* Map Host controller registers */

uint32 *
sdstd_reg_map(osl_t *osh, dmaaddr_t addr, int size)
{
	return (uint32 *)REG_MAP(addr, size);
}

void
sdstd_reg_unmap(osl_t *osh, dmaaddr_t addr, int size)
{
	REG_UNMAP((void*)(uintptr)addr);
}

int
sdstd_osinit(sdioh_info_t *sd)
{
	struct sdos_info *sdos;

	sdos = (struct sdos_info*)MALLOC(sd->osh, sizeof(struct sdos_info));
	sd->sdos_info = (void*)sdos;
	if (sdos == NULL)
		return BCME_NOMEM;

	sdos->sd = sd;
	spin_lock_init(&sdos->lock);
	atomic_set(&sdos->timer_enab, FALSE);
	init_waitqueue_head(&sdos->intr_wait_queue);
	return BCME_OK;
}

/* initilize tuning related OS structures */
void
sdstd_3_osinit_tuning(sdioh_info_t *sd)
{
	struct sdos_info *sdos = (struct sdos_info *)sd->sdos_info;
	uint8 timer_count = sdstd_3_get_tuning_exp(sdos->sd);

	sd_trace(("%s Enter\n", __FUNCTION__));

	init_timer_compat(&sdos->tuning_timer, sdstd_3_tuning_timer, sdos);
	if (timer_count == CAP3_RETUNING_TC_DISABLED || timer_count > CAP3_RETUNING_TC_1024S) {
		sdos->tuning_timer_exp = 0;
	} else {
		sdos->tuning_timer_exp = 1 << (timer_count - 1);
	}
	tasklet_init(&sdos->tuning_tasklet, sdstd_3_ostasklet, (ulong)sdos);
	if (sdos->tuning_timer_exp) {
		timer_expires(&sdos->tuning_timer) = jiffies + sdos->tuning_timer_exp * HZ;
		add_timer(&sdos->tuning_timer);
		atomic_set(&sdos->timer_enab, TRUE);
	}
}

/* finalize tuning related OS structures */
void
sdstd_3_osclean_tuning(sdioh_info_t *sd)
{
	struct sdos_info *sdos = (struct sdos_info *)sd->sdos_info;
	if (atomic_read(&sdos->timer_enab) == TRUE) {
		/* disable timer if it was running */
		del_timer_sync(&sdos->tuning_timer);
		atomic_set(&sdos->timer_enab, FALSE);
	}
	tasklet_kill(&sdos->tuning_tasklet);
}

static void
sdstd_3_ostasklet(ulong data)
{
	struct sdos_info *sdos = (struct sdos_info *)data;
	int tune_state = sdstd_3_get_tune_state(sdos->sd);
	int data_state = sdstd_3_get_data_state(sdos->sd);
	if ((tune_state == TUNING_START) || (tune_state == TUNING_ONGOING) ||
		(tune_state == TUNING_START_AFTER_DAT)) {
		return;
	}
	else if (data_state == DATA_TRANSFER_IDLE)
		sdstd_3_set_tune_state(sdos->sd, TUNING_START);
	else if (data_state == DATA_TRANSFER_ONGOING)
		sdstd_3_set_tune_state(sdos->sd, TUNING_START_AFTER_DAT);
}

static void
sdstd_3_tuning_timer(ulong data)
{
	struct sdos_info *sdos = (struct sdos_info *)data;
/* 	uint8 timeout = 0; */
	unsigned long int_flags;

	sd_trace(("%s: enter\n", __FUNCTION__));
	/* schedule tasklet */
	/* * disable ISR's */
	local_irq_save(int_flags);
	if (sdstd_3_check_and_set_retuning(sdos->sd))
		tasklet_schedule(&sdos->tuning_tasklet);

	/* * enable back ISR's */
	local_irq_restore(int_flags);
}

void sdstd_3_start_tuning(sdioh_info_t *sd)
{
	int tune_state;
	unsigned long int_flags = 0;
	unsigned int timer_enab;
	struct sdos_info *sdos = (struct sdos_info *)sd->sdos_info;
	sd_trace(("%s: enter\n", __FUNCTION__));
	/* * disable ISR's */
	local_irq_save(int_flags);
	timer_enab = atomic_read(&sdos->timer_enab);

	tune_state = sdstd_3_get_tune_state(sd);

	if (tune_state == TUNING_ONGOING) {
		/* do nothing */
		local_irq_restore(int_flags);
		goto exit;
	}
	/* change state */
	sdstd_3_set_tune_state(sd, TUNING_ONGOING);
	/* * enable ISR's */
	local_irq_restore(int_flags);
	sdstd_3_clk_tuning(sd, sdstd_3_get_uhsi_clkmode(sd));
#ifdef BCMSDIOH_STD_TUNING_WAR
	/*
	 * Observed intermittent SDIO command error after re-tuning done
	 * successfully.  Re-tuning twice is giving much reliable results.
	 */
	sdstd_3_clk_tuning(sd, sdstd_3_get_uhsi_clkmode(sd));
#endif /* BCMSDIOH_STD_TUNING_WAR */
	/* * disable ISR's */
	local_irq_save(int_flags);
	sdstd_3_set_tune_state(sd, TUNING_IDLE);
	/* * enable ISR's */
	local_irq_restore(int_flags);

	/* enable retuning intrrupt */
	sdstd_3_enable_retuning_int(sd);

	/* start retuning timer if enabled */
	if ((sdos->tuning_timer_exp) && (timer_enab)) {
		if (sd->sd3_tuning_reqd) {
			timer_expires(&sdos->tuning_timer) = jiffies + sdos->tuning_timer_exp * HZ;
			mod_timer(&sdos->tuning_timer, timer_expires(&sdos->tuning_timer));
		}
	}
exit:
	return;

}

void
sdstd_osfree(sdioh_info_t *sd)
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

	if (enable && !(sd->intr_handler && sd->intr_handler_arg)) {
		sd_err(("%s: no handler registered, will not enable\n", __FUNCTION__));
		return SDIOH_API_RC_FAIL;
	}

	/* Ensure atomicity for enable/disable calls */
	spin_lock_irqsave(&sdos->lock, flags);

	sd->client_intr_enabled = enable;
	if (enable && !sd->lockcount)
		sdstd_devintr_on(sd);
	else
		sdstd_devintr_off(sd);

	spin_unlock_irqrestore(&sdos->lock, flags);

	return SDIOH_API_RC_SUCCESS;
}

/* Protect against reentrancy (disable device interrupts while executing) */
void
sdstd_lock(sdioh_info_t *sd)
{
	ulong flags;
	struct sdos_info *sdos;
	int    wait_count = 0;

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	sd_trace(("%s: %d\n", __FUNCTION__, sd->lockcount));

	spin_lock_irqsave(&sdos->lock, flags);
	while (sd->lockcount)
	{
	    spin_unlock_irqrestore(&sdos->lock, flags);
	    yield();
		spin_lock_irqsave(&sdos->lock, flags);
		if (++wait_count == 25000) {
		    if (!(sd->lockcount == 0)) {
			sd_err(("%s: ERROR: sd->lockcount == 0\n", __FUNCTION__));
		    }
		}
	}
	/* PR86684: Add temporary debugging print */
	if (wait_count)
		printk("sdstd_lock: wait count = %d\n", wait_count);
	sdstd_devintr_off(sd);
	sd->lockcount++;
	spin_unlock_irqrestore(&sdos->lock, flags);
	if ((sd->controller_type == SDIOH_TYPE_RICOH_R5C822) && (sd->version == HOST_CONTR_VER_3))
		sdstd_turn_on_clock(sd);
}

/* Enable client interrupt */
void
sdstd_unlock(sdioh_info_t *sd)
{
	ulong flags;
	struct sdos_info *sdos;

	sd_trace(("%s: %d, %d\n", __FUNCTION__, sd->lockcount, sd->client_intr_enabled));
	ASSERT(sd->lockcount > 0);

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	spin_lock_irqsave(&sdos->lock, flags);
	if (--sd->lockcount == 0 && sd->client_intr_enabled) {
		sdstd_devintr_on(sd);
	}
	spin_unlock_irqrestore(&sdos->lock, flags);
	if (sd_3_power_save)
	{
		if ((sd->controller_type == SDIOH_TYPE_RICOH_R5C822) &&
			(sd->version == HOST_CONTR_VER_3))
			sdstd_turn_off_clock(sd);
	}
}

void
sdstd_os_lock_irqsave(sdioh_info_t *sd, ulong* flags)
{
	struct sdos_info *sdos = (struct sdos_info *)sd->sdos_info;
	spin_lock_irqsave(&sdos->lock, *flags);
}
void
sdstd_os_unlock_irqrestore(sdioh_info_t *sd, ulong* flags)
{
	struct sdos_info *sdos = (struct sdos_info *)sd->sdos_info;
	spin_unlock_irqrestore(&sdos->lock, *flags);
}

void
sdstd_waitlockfree(sdioh_info_t *sd)
{
	if (sd->lockcount) {
		printk("wait lock free\n");
		while (sd->lockcount)
		{
		    yield();
		}
	}
}

#ifdef BCMQT
void
sdstd_os_yield(sdioh_info_t *sd)
{
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 29))
/*
 * FC4/11 issue on QT if driver hogs > 10s of CPU causing:
 *    BUG: soft lockup detected on CPU#0!
 *
 * XXX Hack: For now, interleave yielding of CPU when we're spinning waiting for
 * XXX register status
 */
	yield();
#endif
}
#endif /* BCMQT */

/* Returns 0 for success, -1 for interrupted, -2 for timeout */
int
sdstd_waitbits(sdioh_info_t *sd, uint16 norm, uint16 err, bool local_yield, uint16 *bits)
{
	struct sdos_info *sdos;
	int rc = 0;

	sdos = (struct sdos_info *)sd->sdos_info;

#ifndef BCMSDYIELD
	ASSERT(!local_yield);
#endif
	sd_trace(("%s: int 0x%02x err 0x%02x yield %d canblock %d\n",
	          __FUNCTION__, norm, err, local_yield, BLOCKABLE()));

	/* Clear the "interrupt happened" flag and last intrstatus */
	sd->got_hcint = FALSE;
	sd->last_intrstatus = 0;

#ifdef BCMSDYIELD
	if (local_yield && BLOCKABLE()) {
		/* Enable interrupts, wait for the indication, then disable */
		sdstd_intrs_on(sd, norm, err);
		rc = wait_event_interruptible_timeout(sdos->intr_wait_queue,
		                                      (sd->got_hcint),
		                                      SDSTD_WAITBITS_TIMEOUT);
		if (rc < 0)
			rc = -1;	/* interrupted */
		else if (rc == 0)
			rc = -2;	/* timeout */
		else
			rc = 0;		/* success */
		sdstd_intrs_off(sd, norm, err);
	} else
#endif /* BCMSDYIELD */
	{
		sdstd_spinbits(sd, norm, err);
	}

	sd_trace(("%s: last_intrstatus 0x%04x\n", __FUNCTION__, sd->last_intrstatus));

	*bits = sd->last_intrstatus;

	return rc;
}

#ifdef DHD_DEBUG
void sdstd_enable_disable_periodic_timer(sdioh_info_t *sd, uint val)
{
	struct sdos_info *sdos = (struct sdos_info *)sd->sdos_info;

	    if (val == SD_DHD_ENABLE_PERIODIC_TUNING) {
			/* start of tuning timer */
			timer_expires(&sdos->tuning_timer) = jiffies +  sdos->tuning_timer_exp * HZ;
			mod_timer(&sdos->tuning_timer, timer_expires(&sdos->tuning_timer));
		}
	    if (val == SD_DHD_DISABLE_PERIODIC_TUNING) {
			/* stop periodic timer */
		   del_timer_sync(&sdos->tuning_timer);
		}
}
#endif /* debugging purpose */

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
	{ 0, 0, 0, 0, 0, 0, 0}
};
MODULE_DEVICE_TABLE(pci, bcmsdh_pci_devid);

/**
 * SDIO Host Controller pci driver info
 */
static struct pci_driver bcmsdh_pci_driver = {
	node:		{&(bcmsdh_pci_driver.node), &(bcmsdh_pci_driver.node)},
	name:		"bcmsdh",
	id_table:	bcmsdh_pci_devid,
	probe:		bcmsdh_pci_probe,
	remove:		bcmsdh_pci_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	save_state:	NULL,
#endif
	suspend:	NULL,
	resume:		NULL,
	};

extern uint sd_pci_slot;	/* Force detection to a particular PCI */
							/* slot only . Allows for having multiple */
							/* WL devices at once in a PC */
							/* Only one instance of dhd will be */
							/* usable at a time */
							/* Upper word is bus number, */
							/* lower word is slot number */
							/* Default value of 0xffffffff turns this */
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
	sdioh_info_t *sdioh = NULL;
	int rc;

	if (sd_pci_slot != 0xFFFFffff) {
		if (pdev->bus->number != (sd_pci_slot>>16) ||
			PCI_SLOT(pdev->devfn) != (sd_pci_slot&0xffff)) {
			sd_err(("%s: %s: bus %X, slot %X, vend %X, dev %X\n",
				__FUNCTION__,
				bcmsdh_chipmatch(pdev->vendor, pdev->device)
				?"Found compatible SDIOHC"
				:"Probing unknown device",
				pdev->bus->number, PCI_SLOT(pdev->devfn), pdev->vendor,
				pdev->device));
			return -ENODEV;
		}
		sd_err(("%s: %s: bus %X, slot %X, vendor %X, device %X (good PCI location)\n",
			__FUNCTION__,
			bcmsdh_chipmatch(pdev->vendor, pdev->device)
			?"Using compatible SDIOHC"
			:"WARNING, forced use of unkown device",
			pdev->bus->number, PCI_SLOT(pdev->devfn), pdev->vendor, pdev->device));
	}

	if ((pdev->vendor == VENDOR_TI) && ((pdev->device == PCIXX21_FLASHMEDIA_ID) ||
	    (pdev->device == PCIXX21_FLASHMEDIA0_ID))) {
		uint32 config_reg;

		sd_err(("%s: Disabling TI FlashMedia Controller.\n", __FUNCTION__));
		if (!(osh = osl_attach(pdev, SDIO_BUS, TRUE))) {
			sd_err(("%s: osl_attach failed\n", __FUNCTION__));
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
			sd_err(("%s: Unknown Broadcom device (vendor: %#x, device: %#x).\n",
				__FUNCTION__, pdev->vendor, pdev->device));
		}
		return -ENODEV;
	}

	/* this is a pci device we might support */
	sd_err(("%s: Found possible SDIO Host Controller: bus %d slot %d func %d irq %d\n",
		__FUNCTION__,
		pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn), pdev->irq));

	/* use bcmsdh_query_device() to get the vendor ID of the target device so
	 * it will eventually appear in the Broadcom string on the console
	 */

	/* allocate SDIO Host Controller state info */
	if (!(osh = osl_attach(pdev, SDIO_BUS, TRUE))) {
		sd_err(("%s: osl_attach failed\n", __FUNCTION__));
		goto err;
	}

	/* map to address where host can access */
	pci_set_master(pdev);
	rc = pci_enable_device(pdev);
	if (rc) {
		sd_err(("%s: Cannot enable PCI device\n", __FUNCTION__));
		goto err;
	}

	sdioh = sdioh_attach(osh, (void *)(ulong)pci_resource_start(pdev, 0), pdev->irq);
	if (sdioh == NULL) {
		sd_err(("%s: sdioh_attach failed\n", __FUNCTION__));
		goto err;
	}
	sdioh->bcmsdh = bcmsdh_probe(osh, &pdev->dev, sdioh, NULL, PCI_BUS, -1, -1);
	if (sdioh->bcmsdh == NULL) {
		sd_err(("%s: bcmsdh_probe failed\n", __FUNCTION__));
		goto err;
	}

	pci_set_drvdata(pdev, sdioh);
	return 0;

err:
	if (sdioh != NULL)
		sdioh_detach(osh, sdioh);
	if (osh != NULL)
		osl_detach(osh);
	return -ENOMEM;
}

/**
 * Detach from target devices and SDIO Host Controller
 */
static void __devexit
bcmsdh_pci_remove(struct pci_dev *pdev)
{
	sdioh_info_t *sdioh;
	osl_t *osh;

	sdioh = pci_get_drvdata(pdev);
	if (sdioh == NULL) {
		sd_err(("%s: error, no sdioh handler found\n", __FUNCTION__));
		return;
	}

	osh = sdioh->osh;
	bcmsdh_remove(sdioh->bcmsdh);
	sdioh_detach(osh, sdioh);
	osl_detach(osh);
}

int bcmsdh_register_client_driver(void)
{
	return pci_module_init(&bcmsdh_pci_driver);
}

void bcmsdh_unregister_client_driver(void)
{
	pci_unregister_driver(&bcmsdh_pci_driver);
}
