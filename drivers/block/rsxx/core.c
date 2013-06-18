/*
* Filename: core.c
*
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software Foundation,
* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#include <linux/genhd.h>
#include <linux/idr.h>

#include "rsxx_priv.h"
#include "rsxx_cfg.h"

#define NO_LEGACY 0

MODULE_DESCRIPTION("IBM FlashSystem 70/80 PCIe SSD Device Driver");
MODULE_AUTHOR("Joshua Morris/Philip Kelleher, IBM");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

static unsigned int force_legacy = NO_LEGACY;
module_param(force_legacy, uint, 0444);
MODULE_PARM_DESC(force_legacy, "Force the use of legacy type PCI interrupts");

static DEFINE_IDA(rsxx_disk_ida);
static DEFINE_SPINLOCK(rsxx_ida_lock);

/*----------------- Interrupt Control & Handling -------------------*/

static void rsxx_mask_interrupts(struct rsxx_cardinfo *card)
{
	card->isr_mask = 0;
	card->ier_mask = 0;
}

static void __enable_intr(unsigned int *mask, unsigned int intr)
{
	*mask |= intr;
}

static void __disable_intr(unsigned int *mask, unsigned int intr)
{
	*mask &= ~intr;
}

/*
 * NOTE: Disabling the IER will disable the hardware interrupt.
 * Disabling the ISR will disable the software handling of the ISR bit.
 *
 * Enable/Disable interrupt functions assume the card->irq_lock
 * is held by the caller.
 */
void rsxx_enable_ier(struct rsxx_cardinfo *card, unsigned int intr)
{
	if (unlikely(card->halt) ||
	    unlikely(card->eeh_state))
		return;

	__enable_intr(&card->ier_mask, intr);
	iowrite32(card->ier_mask, card->regmap + IER);
}

void rsxx_disable_ier(struct rsxx_cardinfo *card, unsigned int intr)
{
	if (unlikely(card->eeh_state))
		return;

	__disable_intr(&card->ier_mask, intr);
	iowrite32(card->ier_mask, card->regmap + IER);
}

void rsxx_enable_ier_and_isr(struct rsxx_cardinfo *card,
				 unsigned int intr)
{
	if (unlikely(card->halt) ||
	    unlikely(card->eeh_state))
		return;

	__enable_intr(&card->isr_mask, intr);
	__enable_intr(&card->ier_mask, intr);
	iowrite32(card->ier_mask, card->regmap + IER);
}
void rsxx_disable_ier_and_isr(struct rsxx_cardinfo *card,
				  unsigned int intr)
{
	if (unlikely(card->eeh_state))
		return;

	__disable_intr(&card->isr_mask, intr);
	__disable_intr(&card->ier_mask, intr);
	iowrite32(card->ier_mask, card->regmap + IER);
}

static irqreturn_t rsxx_isr(int irq, void *pdata)
{
	struct rsxx_cardinfo *card = pdata;
	unsigned int isr;
	int handled = 0;
	int reread_isr;
	int i;

	spin_lock(&card->irq_lock);

	do {
		reread_isr = 0;

		if (unlikely(card->eeh_state))
			break;

		isr = ioread32(card->regmap + ISR);
		if (isr == 0xffffffff) {
			/*
			 * A few systems seem to have an intermittent issue
			 * where PCI reads return all Fs, but retrying the read
			 * a little later will return as expected.
			 */
			dev_info(CARD_TO_DEV(card),
				"ISR = 0xFFFFFFFF, retrying later\n");
			break;
		}

		isr &= card->isr_mask;
		if (!isr)
			break;

		for (i = 0; i < card->n_targets; i++) {
			if (isr & CR_INTR_DMA(i)) {
				if (card->ier_mask & CR_INTR_DMA(i)) {
					rsxx_disable_ier(card, CR_INTR_DMA(i));
					reread_isr = 1;
				}
				queue_work(card->ctrl[i].done_wq,
					   &card->ctrl[i].dma_done_work);
				handled++;
			}
		}

		if (isr & CR_INTR_CREG) {
			queue_work(card->creg_ctrl.creg_wq,
				   &card->creg_ctrl.done_work);
			handled++;
		}

		if (isr & CR_INTR_EVENT) {
			queue_work(card->event_wq, &card->event_work);
			rsxx_disable_ier_and_isr(card, CR_INTR_EVENT);
			handled++;
		}
	} while (reread_isr);

	spin_unlock(&card->irq_lock);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

/*----------------- Card Event Handler -------------------*/
static const char * const rsxx_card_state_to_str(unsigned int state)
{
	static const char * const state_strings[] = {
		"Unknown", "Shutdown", "Starting", "Formatting",
		"Uninitialized", "Good", "Shutting Down",
		"Fault", "Read Only Fault", "dStroying"
	};

	return state_strings[ffs(state)];
}

static void card_state_change(struct rsxx_cardinfo *card,
			      unsigned int new_state)
{
	int st;

	dev_info(CARD_TO_DEV(card),
		"card state change detected.(%s -> %s)\n",
		rsxx_card_state_to_str(card->state),
		rsxx_card_state_to_str(new_state));

	card->state = new_state;

	/* Don't attach DMA interfaces if the card has an invalid config */
	if (!card->config_valid)
		return;

	switch (new_state) {
	case CARD_STATE_RD_ONLY_FAULT:
		dev_crit(CARD_TO_DEV(card),
			"Hardware has entered read-only mode!\n");
		/*
		 * Fall through so the DMA devices can be attached and
		 * the user can attempt to pull off their data.
		 */
	case CARD_STATE_GOOD:
		st = rsxx_get_card_size8(card, &card->size8);
		if (st)
			dev_err(CARD_TO_DEV(card),
				"Failed attaching DMA devices\n");

		if (card->config_valid)
			set_capacity(card->gendisk, card->size8 >> 9);
		break;

	case CARD_STATE_FAULT:
		dev_crit(CARD_TO_DEV(card),
			"Hardware Fault reported!\n");
		/* Fall through. */

	/* Everything else, detach DMA interface if it's attached. */
	case CARD_STATE_SHUTDOWN:
	case CARD_STATE_STARTING:
	case CARD_STATE_FORMATTING:
	case CARD_STATE_UNINITIALIZED:
	case CARD_STATE_SHUTTING_DOWN:
	/*
	 * dStroy is a term coined by marketing to represent the low level
	 * secure erase.
	 */
	case CARD_STATE_DSTROYING:
		set_capacity(card->gendisk, 0);
		break;
	}
}

static void card_event_handler(struct work_struct *work)
{
	struct rsxx_cardinfo *card;
	unsigned int state;
	unsigned long flags;
	int st;

	card = container_of(work, struct rsxx_cardinfo, event_work);

	if (unlikely(card->halt))
		return;

	/*
	 * Enable the interrupt now to avoid any weird race conditions where a
	 * state change might occur while rsxx_get_card_state() is
	 * processing a returned creg cmd.
	 */
	spin_lock_irqsave(&card->irq_lock, flags);
	rsxx_enable_ier_and_isr(card, CR_INTR_EVENT);
	spin_unlock_irqrestore(&card->irq_lock, flags);

	st = rsxx_get_card_state(card, &state);
	if (st) {
		dev_info(CARD_TO_DEV(card),
			"Failed reading state after event.\n");
		return;
	}

	if (card->state != state)
		card_state_change(card, state);

	if (card->creg_ctrl.creg_stats.stat & CREG_STAT_LOG_PENDING)
		rsxx_read_hw_log(card);
}

/*----------------- Card Operations -------------------*/
static int card_shutdown(struct rsxx_cardinfo *card)
{
	unsigned int state;
	signed long start;
	const int timeout = msecs_to_jiffies(120000);
	int st;

	/* We can't issue a shutdown if the card is in a transition state */
	start = jiffies;
	do {
		st = rsxx_get_card_state(card, &state);
		if (st)
			return st;
	} while (state == CARD_STATE_STARTING &&
		 (jiffies - start < timeout));

	if (state == CARD_STATE_STARTING)
		return -ETIMEDOUT;

	/* Only issue a shutdown if we need to */
	if ((state != CARD_STATE_SHUTTING_DOWN) &&
	    (state != CARD_STATE_SHUTDOWN)) {
		st = rsxx_issue_card_cmd(card, CARD_CMD_SHUTDOWN);
		if (st)
			return st;
	}

	start = jiffies;
	do {
		st = rsxx_get_card_state(card, &state);
		if (st)
			return st;
	} while (state != CARD_STATE_SHUTDOWN &&
		 (jiffies - start < timeout));

	if (state != CARD_STATE_SHUTDOWN)
		return -ETIMEDOUT;

	return 0;
}

static int rsxx_eeh_frozen(struct pci_dev *dev)
{
	struct rsxx_cardinfo *card = pci_get_drvdata(dev);
	int i;
	int st;

	dev_warn(&dev->dev, "IBM FlashSystem PCI: preparing for slot reset.\n");

	card->eeh_state = 1;
	rsxx_mask_interrupts(card);

	/*
	 * We need to guarantee that the write for eeh_state and masking
	 * interrupts does not become reordered. This will prevent a possible
	 * race condition with the EEH code.
	 */
	wmb();

	pci_disable_device(dev);

	st = rsxx_eeh_save_issued_dmas(card);
	if (st)
		return st;

	rsxx_eeh_save_issued_creg(card);

	for (i = 0; i < card->n_targets; i++) {
		if (card->ctrl[i].status.buf)
			pci_free_consistent(card->dev, STATUS_BUFFER_SIZE8,
					    card->ctrl[i].status.buf,
					    card->ctrl[i].status.dma_addr);
		if (card->ctrl[i].cmd.buf)
			pci_free_consistent(card->dev, COMMAND_BUFFER_SIZE8,
					    card->ctrl[i].cmd.buf,
					    card->ctrl[i].cmd.dma_addr);
	}

	return 0;
}

static void rsxx_eeh_failure(struct pci_dev *dev)
{
	struct rsxx_cardinfo *card = pci_get_drvdata(dev);
	int i;
	int cnt = 0;

	dev_err(&dev->dev, "IBM FlashSystem PCI: disabling failed card.\n");

	card->eeh_state = 1;
	card->halt = 1;

	for (i = 0; i < card->n_targets; i++) {
		spin_lock_bh(&card->ctrl[i].queue_lock);
		cnt = rsxx_cleanup_dma_queue(&card->ctrl[i],
					     &card->ctrl[i].queue);
		spin_unlock_bh(&card->ctrl[i].queue_lock);

		cnt += rsxx_dma_cancel(&card->ctrl[i]);

		if (cnt)
			dev_info(CARD_TO_DEV(card),
				"Freed %d queued DMAs on channel %d\n",
				cnt, card->ctrl[i].id);
	}
}

static int rsxx_eeh_fifo_flush_poll(struct rsxx_cardinfo *card)
{
	unsigned int status;
	int iter = 0;

	/* We need to wait for the hardware to reset */
	while (iter++ < 10) {
		status = ioread32(card->regmap + PCI_RECONFIG);

		if (status & RSXX_FLUSH_BUSY) {
			ssleep(1);
			continue;
		}

		if (status & RSXX_FLUSH_TIMEOUT)
			dev_warn(CARD_TO_DEV(card), "HW: flash controller timeout\n");
		return 0;
	}

	/* Hardware failed resetting itself. */
	return -1;
}

static pci_ers_result_t rsxx_error_detected(struct pci_dev *dev,
					    enum pci_channel_state error)
{
	int st;

	if (dev->revision < RSXX_EEH_SUPPORT)
		return PCI_ERS_RESULT_NONE;

	if (error == pci_channel_io_perm_failure) {
		rsxx_eeh_failure(dev);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	st = rsxx_eeh_frozen(dev);
	if (st) {
		dev_err(&dev->dev, "Slot reset setup failed\n");
		rsxx_eeh_failure(dev);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t rsxx_slot_reset(struct pci_dev *dev)
{
	struct rsxx_cardinfo *card = pci_get_drvdata(dev);
	unsigned long flags;
	int i;
	int st;

	dev_warn(&dev->dev,
		"IBM FlashSystem PCI: recovering from slot reset.\n");

	st = pci_enable_device(dev);
	if (st)
		goto failed_hw_setup;

	pci_set_master(dev);

	st = rsxx_eeh_fifo_flush_poll(card);
	if (st)
		goto failed_hw_setup;

	rsxx_dma_queue_reset(card);

	for (i = 0; i < card->n_targets; i++) {
		st = rsxx_hw_buffers_init(dev, &card->ctrl[i]);
		if (st)
			goto failed_hw_buffers_init;
	}

	if (card->config_valid)
		rsxx_dma_configure(card);

	/* Clears the ISR register from spurious interrupts */
	st = ioread32(card->regmap + ISR);

	card->eeh_state = 0;

	st = rsxx_eeh_remap_dmas(card);
	if (st)
		goto failed_remap_dmas;

	spin_lock_irqsave(&card->irq_lock, flags);
	if (card->n_targets & RSXX_MAX_TARGETS)
		rsxx_enable_ier_and_isr(card, CR_INTR_ALL_G);
	else
		rsxx_enable_ier_and_isr(card, CR_INTR_ALL_C);
	spin_unlock_irqrestore(&card->irq_lock, flags);

	rsxx_kick_creg_queue(card);

	for (i = 0; i < card->n_targets; i++) {
		spin_lock(&card->ctrl[i].queue_lock);
		if (list_empty(&card->ctrl[i].queue)) {
			spin_unlock(&card->ctrl[i].queue_lock);
			continue;
		}
		spin_unlock(&card->ctrl[i].queue_lock);

		queue_work(card->ctrl[i].issue_wq,
				&card->ctrl[i].issue_dma_work);
	}

	dev_info(&dev->dev, "IBM FlashSystem PCI: recovery complete.\n");

	return PCI_ERS_RESULT_RECOVERED;

failed_hw_buffers_init:
failed_remap_dmas:
	for (i = 0; i < card->n_targets; i++) {
		if (card->ctrl[i].status.buf)
			pci_free_consistent(card->dev,
					STATUS_BUFFER_SIZE8,
					card->ctrl[i].status.buf,
					card->ctrl[i].status.dma_addr);
		if (card->ctrl[i].cmd.buf)
			pci_free_consistent(card->dev,
					COMMAND_BUFFER_SIZE8,
					card->ctrl[i].cmd.buf,
					card->ctrl[i].cmd.dma_addr);
	}
failed_hw_setup:
	rsxx_eeh_failure(dev);
	return PCI_ERS_RESULT_DISCONNECT;

}

/*----------------- Driver Initialization & Setup -------------------*/
/* Returns:   0 if the driver is compatible with the device
	     -1 if the driver is NOT compatible with the device */
static int rsxx_compatibility_check(struct rsxx_cardinfo *card)
{
	unsigned char pci_rev;

	pci_read_config_byte(card->dev, PCI_REVISION_ID, &pci_rev);

	if (pci_rev > RS70_PCI_REV_SUPPORTED)
		return -1;
	return 0;
}

static int rsxx_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *id)
{
	struct rsxx_cardinfo *card;
	int st;

	dev_info(&dev->dev, "PCI-Flash SSD discovered\n");

	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = dev;
	pci_set_drvdata(dev, card);

	do {
		if (!ida_pre_get(&rsxx_disk_ida, GFP_KERNEL)) {
			st = -ENOMEM;
			goto failed_ida_get;
		}

		spin_lock(&rsxx_ida_lock);
		st = ida_get_new(&rsxx_disk_ida, &card->disk_id);
		spin_unlock(&rsxx_ida_lock);
	} while (st == -EAGAIN);

	if (st)
		goto failed_ida_get;

	st = pci_enable_device(dev);
	if (st)
		goto failed_enable;

	pci_set_master(dev);
	pci_set_dma_max_seg_size(dev, RSXX_HW_BLK_SIZE);

	st = pci_set_dma_mask(dev, DMA_BIT_MASK(64));
	if (st) {
		dev_err(CARD_TO_DEV(card),
			"No usable DMA configuration,aborting\n");
		goto failed_dma_mask;
	}

	st = pci_request_regions(dev, DRIVER_NAME);
	if (st) {
		dev_err(CARD_TO_DEV(card),
			"Failed to request memory region\n");
		goto failed_request_regions;
	}

	if (pci_resource_len(dev, 0) == 0) {
		dev_err(CARD_TO_DEV(card), "BAR0 has length 0!\n");
		st = -ENOMEM;
		goto failed_iomap;
	}

	card->regmap = pci_iomap(dev, 0, 0);
	if (!card->regmap) {
		dev_err(CARD_TO_DEV(card), "Failed to map BAR0\n");
		st = -ENOMEM;
		goto failed_iomap;
	}

	spin_lock_init(&card->irq_lock);
	card->halt = 0;
	card->eeh_state = 0;

	spin_lock_irq(&card->irq_lock);
	rsxx_disable_ier_and_isr(card, CR_INTR_ALL);
	spin_unlock_irq(&card->irq_lock);

	if (!force_legacy) {
		st = pci_enable_msi(dev);
		if (st)
			dev_warn(CARD_TO_DEV(card),
				"Failed to enable MSI\n");
	}

	st = request_irq(dev->irq, rsxx_isr, IRQF_DISABLED | IRQF_SHARED,
			 DRIVER_NAME, card);
	if (st) {
		dev_err(CARD_TO_DEV(card),
			"Failed requesting IRQ%d\n", dev->irq);
		goto failed_irq;
	}

	/************* Setup Processor Command Interface *************/
	st = rsxx_creg_setup(card);
	if (st) {
		dev_err(CARD_TO_DEV(card), "Failed to setup creg interface.\n");
		goto failed_creg_setup;
	}

	spin_lock_irq(&card->irq_lock);
	rsxx_enable_ier_and_isr(card, CR_INTR_CREG);
	spin_unlock_irq(&card->irq_lock);

	st = rsxx_compatibility_check(card);
	if (st) {
		dev_warn(CARD_TO_DEV(card),
			"Incompatible driver detected. Please update the driver.\n");
		st = -EINVAL;
		goto failed_compatiblity_check;
	}

	/************* Load Card Config *************/
	st = rsxx_load_config(card);
	if (st)
		dev_err(CARD_TO_DEV(card),
			"Failed loading card config\n");

	/************* Setup DMA Engine *************/
	st = rsxx_get_num_targets(card, &card->n_targets);
	if (st)
		dev_info(CARD_TO_DEV(card),
			"Failed reading the number of DMA targets\n");

	card->ctrl = kzalloc(card->n_targets * sizeof(*card->ctrl), GFP_KERNEL);
	if (!card->ctrl) {
		st = -ENOMEM;
		goto failed_dma_setup;
	}

	st = rsxx_dma_setup(card);
	if (st) {
		dev_info(CARD_TO_DEV(card),
			"Failed to setup DMA engine\n");
		goto failed_dma_setup;
	}

	/************* Setup Card Event Handler *************/
	card->event_wq = create_singlethread_workqueue(DRIVER_NAME"_event");
	if (!card->event_wq) {
		dev_err(CARD_TO_DEV(card), "Failed card event setup.\n");
		goto failed_event_handler;
	}

	INIT_WORK(&card->event_work, card_event_handler);

	st = rsxx_setup_dev(card);
	if (st)
		goto failed_create_dev;

	rsxx_get_card_state(card, &card->state);

	dev_info(CARD_TO_DEV(card),
		"card state: %s\n",
		rsxx_card_state_to_str(card->state));

	/*
	 * Now that the DMA Engine and devices have been setup,
	 * we can enable the event interrupt(it kicks off actions in
	 * those layers so we couldn't enable it right away.)
	 */
	spin_lock_irq(&card->irq_lock);
	rsxx_enable_ier_and_isr(card, CR_INTR_EVENT);
	spin_unlock_irq(&card->irq_lock);

	if (card->state == CARD_STATE_SHUTDOWN) {
		st = rsxx_issue_card_cmd(card, CARD_CMD_STARTUP);
		if (st)
			dev_crit(CARD_TO_DEV(card),
				"Failed issuing card startup\n");
	} else if (card->state == CARD_STATE_GOOD ||
		   card->state == CARD_STATE_RD_ONLY_FAULT) {
		st = rsxx_get_card_size8(card, &card->size8);
		if (st)
			card->size8 = 0;
	}

	rsxx_attach_dev(card);

	return 0;

failed_create_dev:
	destroy_workqueue(card->event_wq);
	card->event_wq = NULL;
failed_event_handler:
	rsxx_dma_destroy(card);
failed_dma_setup:
failed_compatiblity_check:
	destroy_workqueue(card->creg_ctrl.creg_wq);
	card->creg_ctrl.creg_wq = NULL;
failed_creg_setup:
	spin_lock_irq(&card->irq_lock);
	rsxx_disable_ier_and_isr(card, CR_INTR_ALL);
	spin_unlock_irq(&card->irq_lock);
	free_irq(dev->irq, card);
	if (!force_legacy)
		pci_disable_msi(dev);
failed_irq:
	pci_iounmap(dev, card->regmap);
failed_iomap:
	pci_release_regions(dev);
failed_request_regions:
failed_dma_mask:
	pci_disable_device(dev);
failed_enable:
	spin_lock(&rsxx_ida_lock);
	ida_remove(&rsxx_disk_ida, card->disk_id);
	spin_unlock(&rsxx_ida_lock);
failed_ida_get:
	kfree(card);

	return st;
}

static void rsxx_pci_remove(struct pci_dev *dev)
{
	struct rsxx_cardinfo *card = pci_get_drvdata(dev);
	unsigned long flags;
	int st;
	int i;

	if (!card)
		return;

	dev_info(CARD_TO_DEV(card),
		"Removing PCI-Flash SSD.\n");

	rsxx_detach_dev(card);

	for (i = 0; i < card->n_targets; i++) {
		spin_lock_irqsave(&card->irq_lock, flags);
		rsxx_disable_ier_and_isr(card, CR_INTR_DMA(i));
		spin_unlock_irqrestore(&card->irq_lock, flags);
	}

	st = card_shutdown(card);
	if (st)
		dev_crit(CARD_TO_DEV(card), "Shutdown failed!\n");

	/* Sync outstanding event handlers. */
	spin_lock_irqsave(&card->irq_lock, flags);
	rsxx_disable_ier_and_isr(card, CR_INTR_EVENT);
	spin_unlock_irqrestore(&card->irq_lock, flags);

	cancel_work_sync(&card->event_work);

	rsxx_destroy_dev(card);
	rsxx_dma_destroy(card);

	spin_lock_irqsave(&card->irq_lock, flags);
	rsxx_disable_ier_and_isr(card, CR_INTR_ALL);
	spin_unlock_irqrestore(&card->irq_lock, flags);

	/* Prevent work_structs from re-queuing themselves. */
	card->halt = 1;

	free_irq(dev->irq, card);

	if (!force_legacy)
		pci_disable_msi(dev);

	rsxx_creg_destroy(card);

	pci_iounmap(dev, card->regmap);

	pci_disable_device(dev);
	pci_release_regions(dev);

	kfree(card);
}

static int rsxx_pci_suspend(struct pci_dev *dev, pm_message_t state)
{
	/* We don't support suspend at this time. */
	return -ENOSYS;
}

static void rsxx_pci_shutdown(struct pci_dev *dev)
{
	struct rsxx_cardinfo *card = pci_get_drvdata(dev);
	unsigned long flags;
	int i;

	if (!card)
		return;

	dev_info(CARD_TO_DEV(card), "Shutting down PCI-Flash SSD.\n");

	rsxx_detach_dev(card);

	for (i = 0; i < card->n_targets; i++) {
		spin_lock_irqsave(&card->irq_lock, flags);
		rsxx_disable_ier_and_isr(card, CR_INTR_DMA(i));
		spin_unlock_irqrestore(&card->irq_lock, flags);
	}

	card_shutdown(card);
}

static const struct pci_error_handlers rsxx_err_handler = {
	.error_detected = rsxx_error_detected,
	.slot_reset     = rsxx_slot_reset,
};

static DEFINE_PCI_DEVICE_TABLE(rsxx_pci_ids) = {
	{PCI_DEVICE(PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_FS70_FLASH)},
	{PCI_DEVICE(PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_FS80_FLASH)},
	{0,},
};

MODULE_DEVICE_TABLE(pci, rsxx_pci_ids);

static struct pci_driver rsxx_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= rsxx_pci_ids,
	.probe		= rsxx_pci_probe,
	.remove		= rsxx_pci_remove,
	.suspend	= rsxx_pci_suspend,
	.shutdown	= rsxx_pci_shutdown,
	.err_handler    = &rsxx_err_handler,
};

static int __init rsxx_core_init(void)
{
	int st;

	st = rsxx_dev_init();
	if (st)
		return st;

	st = rsxx_dma_init();
	if (st)
		goto dma_init_failed;

	st = rsxx_creg_init();
	if (st)
		goto creg_init_failed;

	return pci_register_driver(&rsxx_pci_driver);

creg_init_failed:
	rsxx_dma_cleanup();
dma_init_failed:
	rsxx_dev_cleanup();

	return st;
}

static void __exit rsxx_core_cleanup(void)
{
	pci_unregister_driver(&rsxx_pci_driver);
	rsxx_creg_cleanup();
	rsxx_dma_cleanup();
	rsxx_dev_cleanup();
}

module_init(rsxx_core_init);
module_exit(rsxx_core_cleanup);
