/*
 * Wireless Host Controller: Radio Control Interface (WHCI v0.95[2.3])
 * Radio Control command/event transport to the UWB stack
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Initialize and hook up the Radio Control interface.
 *
 * For each device probed, creates an 'struct whcrc' which contains
 * just the representation of the UWB Radio Controller, and the logic
 * for reading notifications and passing them to the UWB Core.
 *
 * So we initialize all of those, register the UWB Radio Controller
 * and setup the notification/event handle to pipe the notifications
 * to the UWB management Daemon.
 *
 * Once uwb_rc_add() is called, the UWB stack takes control, resets
 * the radio and readies the device to take commands the UWB
 * API/user-space.
 *
 * Note this driver is just a transport driver; the commands are
 * formed at the UWB stack and given to this driver who will deliver
 * them to the hw and transfer the replies/notifications back to the
 * UWB stack through the UWB daemon (UWBD).
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/uwb.h>
#include <linux/uwb/whci.h>
#include <linux/uwb/umc.h>

#include "uwb-internal.h"

/**
 * Descriptor for an instance of the UWB Radio Control Driver that
 * attaches to the URC interface of the WHCI PCI card.
 *
 * Unless there is a lock specific to the 'data members', all access
 * is protected by uwb_rc->mutex.
 */
struct whcrc {
	struct umc_dev *umc_dev;
	struct uwb_rc *uwb_rc;		/* UWB host controller */

	unsigned long area;
	void __iomem *rc_base;
	size_t rc_len;
	spinlock_t irq_lock;

	void *evt_buf, *cmd_buf;
	dma_addr_t evt_dma_buf, cmd_dma_buf;
	wait_queue_head_t cmd_wq;
	struct work_struct event_work;
};

/**
 * Execute an UWB RC command on WHCI/RC
 *
 * @rc:       Instance of a Radio Controller that is a whcrc
 * @cmd:      Buffer containing the RCCB and payload to execute
 * @cmd_size: Size of the command buffer.
 *
 * We copy the command into whcrc->cmd_buf (as it is pretty and
 * aligned`and physically contiguous) and then press the right keys in
 * the controller's URCCMD register to get it to read it. We might
 * have to wait for the cmd_sem to be open to us.
 *
 * NOTE: rc's mutex has to be locked
 */
static int whcrc_cmd(struct uwb_rc *uwb_rc,
	      const struct uwb_rccb *cmd, size_t cmd_size)
{
	int result = 0;
	struct whcrc *whcrc = uwb_rc->priv;
	struct device *dev = &whcrc->umc_dev->dev;
	u32 urccmd;

	if (cmd_size >= 4096)
		return -EINVAL;

	/*
	 * If the URC is halted, then the hardware has reset itself.
	 * Attempt to recover by restarting the device and then return
	 * an error as it's likely that the current command isn't
	 * valid for a newly started RC.
	 */
	if (le_readl(whcrc->rc_base + URCSTS) & URCSTS_HALTED) {
		dev_err(dev, "requesting reset of halted radio controller\n");
		uwb_rc_reset_all(uwb_rc);
		return -EIO;
	}

	result = wait_event_timeout(whcrc->cmd_wq,
		!(le_readl(whcrc->rc_base + URCCMD) & URCCMD_ACTIVE), HZ/2);
	if (result == 0) {
		dev_err(dev, "device is not ready to execute commands\n");
		return -ETIMEDOUT;
	}

	memmove(whcrc->cmd_buf, cmd, cmd_size);
	le_writeq(whcrc->cmd_dma_buf, whcrc->rc_base + URCCMDADDR);

	spin_lock(&whcrc->irq_lock);
	urccmd = le_readl(whcrc->rc_base + URCCMD);
	urccmd &= ~(URCCMD_EARV | URCCMD_SIZE_MASK);
	le_writel(urccmd | URCCMD_ACTIVE | URCCMD_IWR | cmd_size,
		  whcrc->rc_base + URCCMD);
	spin_unlock(&whcrc->irq_lock);

	return 0;
}

static int whcrc_reset(struct uwb_rc *rc)
{
	struct whcrc *whcrc = rc->priv;

	return umc_controller_reset(whcrc->umc_dev);
}

/**
 * Reset event reception mechanism and tell hw we are ready to get more
 *
 * We have read all the events in the event buffer, so we are ready to
 * reset it to the beginning.
 *
 * This is only called during initialization or after an event buffer
 * has been retired.  This means we can be sure that event processing
 * is disabled and it's safe to update the URCEVTADDR register.
 *
 * There's no need to wait for the event processing to start as the
 * URC will not clear URCCMD_ACTIVE until (internal) event buffer
 * space is available.
 */
static
void whcrc_enable_events(struct whcrc *whcrc)
{
	u32 urccmd;

	le_writeq(whcrc->evt_dma_buf, whcrc->rc_base + URCEVTADDR);

	spin_lock(&whcrc->irq_lock);
	urccmd = le_readl(whcrc->rc_base + URCCMD) & ~URCCMD_ACTIVE;
	le_writel(urccmd | URCCMD_EARV, whcrc->rc_base + URCCMD);
	spin_unlock(&whcrc->irq_lock);
}

static void whcrc_event_work(struct work_struct *work)
{
	struct whcrc *whcrc = container_of(work, struct whcrc, event_work);
	size_t size;
	u64 urcevtaddr;

	urcevtaddr = le_readq(whcrc->rc_base + URCEVTADDR);
	size = urcevtaddr & URCEVTADDR_OFFSET_MASK;

	uwb_rc_neh_grok(whcrc->uwb_rc, whcrc->evt_buf, size);
	whcrc_enable_events(whcrc);
}

/**
 * Catch interrupts?
 *
 * We ack inmediately (and expect the hw to do the right thing and
 * raise another IRQ if things have changed :)
 */
static
irqreturn_t whcrc_irq_cb(int irq, void *_whcrc)
{
	struct whcrc *whcrc = _whcrc;
	struct device *dev = &whcrc->umc_dev->dev;
	u32 urcsts;

	urcsts = le_readl(whcrc->rc_base + URCSTS);
	if (!(urcsts & URCSTS_INT_MASK))
		return IRQ_NONE;
	le_writel(urcsts & URCSTS_INT_MASK, whcrc->rc_base + URCSTS);

	if (urcsts & URCSTS_HSE) {
		dev_err(dev, "host system error -- hardware halted\n");
		/* FIXME: do something sensible here */
		goto out;
	}
	if (urcsts & URCSTS_ER)
		schedule_work(&whcrc->event_work);
	if (urcsts & URCSTS_RCI)
		wake_up_all(&whcrc->cmd_wq);
out:
	return IRQ_HANDLED;
}


/**
 * Initialize a UMC RC interface: map regions, get (shared) IRQ
 */
static
int whcrc_setup_rc_umc(struct whcrc *whcrc)
{
	int result = 0;
	struct device *dev = &whcrc->umc_dev->dev;
	struct umc_dev *umc_dev = whcrc->umc_dev;

	whcrc->area = umc_dev->resource.start;
	whcrc->rc_len = resource_size(&umc_dev->resource);
	result = -EBUSY;
	if (request_mem_region(whcrc->area, whcrc->rc_len, KBUILD_MODNAME) == NULL) {
		dev_err(dev, "can't request URC region (%zu bytes @ 0x%lx): %d\n",
			whcrc->rc_len, whcrc->area, result);
		goto error_request_region;
	}

	whcrc->rc_base = ioremap_nocache(whcrc->area, whcrc->rc_len);
	if (whcrc->rc_base == NULL) {
		dev_err(dev, "can't ioremap registers (%zu bytes @ 0x%lx): %d\n",
			whcrc->rc_len, whcrc->area, result);
		goto error_ioremap_nocache;
	}

	result = request_irq(umc_dev->irq, whcrc_irq_cb, IRQF_SHARED,
			     KBUILD_MODNAME, whcrc);
	if (result < 0) {
		dev_err(dev, "can't allocate IRQ %d: %d\n",
			umc_dev->irq, result);
		goto error_request_irq;
	}

	result = -ENOMEM;
	whcrc->cmd_buf = dma_alloc_coherent(&umc_dev->dev, PAGE_SIZE,
					    &whcrc->cmd_dma_buf, GFP_KERNEL);
	if (whcrc->cmd_buf == NULL) {
		dev_err(dev, "Can't allocate cmd transfer buffer\n");
		goto error_cmd_buffer;
	}

	whcrc->evt_buf = dma_alloc_coherent(&umc_dev->dev, PAGE_SIZE,
					    &whcrc->evt_dma_buf, GFP_KERNEL);
	if (whcrc->evt_buf == NULL) {
		dev_err(dev, "Can't allocate evt transfer buffer\n");
		goto error_evt_buffer;
	}
	return 0;

error_evt_buffer:
	dma_free_coherent(&umc_dev->dev, PAGE_SIZE, whcrc->cmd_buf,
			  whcrc->cmd_dma_buf);
error_cmd_buffer:
	free_irq(umc_dev->irq, whcrc);
error_request_irq:
	iounmap(whcrc->rc_base);
error_ioremap_nocache:
	release_mem_region(whcrc->area, whcrc->rc_len);
error_request_region:
	return result;
}


/**
 * Release RC's UMC resources
 */
static
void whcrc_release_rc_umc(struct whcrc *whcrc)
{
	struct umc_dev *umc_dev = whcrc->umc_dev;

	dma_free_coherent(&umc_dev->dev, PAGE_SIZE, whcrc->evt_buf,
			  whcrc->evt_dma_buf);
	dma_free_coherent(&umc_dev->dev, PAGE_SIZE, whcrc->cmd_buf,
			  whcrc->cmd_dma_buf);
	free_irq(umc_dev->irq, whcrc);
	iounmap(whcrc->rc_base);
	release_mem_region(whcrc->area, whcrc->rc_len);
}


/**
 * whcrc_start_rc - start a WHCI radio controller
 * @whcrc: the radio controller to start
 *
 * Reset the UMC device, start the radio controller, enable events and
 * finally enable interrupts.
 */
static int whcrc_start_rc(struct uwb_rc *rc)
{
	struct whcrc *whcrc = rc->priv;
	struct device *dev = &whcrc->umc_dev->dev;

	/* Reset the thing */
	le_writel(URCCMD_RESET, whcrc->rc_base + URCCMD);
	if (whci_wait_for(dev, whcrc->rc_base + URCCMD, URCCMD_RESET, 0,
			  5000, "hardware reset") < 0)
		return -EBUSY;

	/* Set the event buffer, start the controller (enable IRQs later) */
	le_writel(0, whcrc->rc_base + URCINTR);
	le_writel(URCCMD_RS, whcrc->rc_base + URCCMD);
	if (whci_wait_for(dev, whcrc->rc_base + URCSTS, URCSTS_HALTED, 0,
			  5000, "radio controller start") < 0)
		return -ETIMEDOUT;
	whcrc_enable_events(whcrc);
	le_writel(URCINTR_EN_ALL, whcrc->rc_base + URCINTR);
	return 0;
}


/**
 * whcrc_stop_rc - stop a WHCI radio controller
 * @whcrc: the radio controller to stop
 *
 * Disable interrupts and cancel any pending event processing work
 * before clearing the Run/Stop bit.
 */
static
void whcrc_stop_rc(struct uwb_rc *rc)
{
	struct whcrc *whcrc = rc->priv;
	struct umc_dev *umc_dev = whcrc->umc_dev;

	le_writel(0, whcrc->rc_base + URCINTR);
	cancel_work_sync(&whcrc->event_work);

	le_writel(0, whcrc->rc_base + URCCMD);
	whci_wait_for(&umc_dev->dev, whcrc->rc_base + URCSTS,
		      URCSTS_HALTED, URCSTS_HALTED, 100, "radio controller stop");
}

static void whcrc_init(struct whcrc *whcrc)
{
	spin_lock_init(&whcrc->irq_lock);
	init_waitqueue_head(&whcrc->cmd_wq);
	INIT_WORK(&whcrc->event_work, whcrc_event_work);
}

/**
 * Initialize the radio controller.
 *
 * NOTE: we setup whcrc->uwb_rc before calling uwb_rc_add(); in the
 *       IRQ handler we use that to determine if the hw is ready to
 *       handle events. Looks like a race condition, but it really is
 *       not.
 */
static
int whcrc_probe(struct umc_dev *umc_dev)
{
	int result;
	struct uwb_rc *uwb_rc;
	struct whcrc *whcrc;
	struct device *dev = &umc_dev->dev;

	result = -ENOMEM;
	uwb_rc = uwb_rc_alloc();
	if (uwb_rc == NULL) {
		dev_err(dev, "unable to allocate RC instance\n");
		goto error_rc_alloc;
	}
	whcrc = kzalloc(sizeof(*whcrc), GFP_KERNEL);
	if (whcrc == NULL) {
		dev_err(dev, "unable to allocate WHC-RC instance\n");
		goto error_alloc;
	}
	whcrc_init(whcrc);
	whcrc->umc_dev = umc_dev;

	result = whcrc_setup_rc_umc(whcrc);
	if (result < 0) {
		dev_err(dev, "Can't setup RC UMC interface: %d\n", result);
		goto error_setup_rc_umc;
	}
	whcrc->uwb_rc = uwb_rc;

	uwb_rc->owner = THIS_MODULE;
	uwb_rc->cmd   = whcrc_cmd;
	uwb_rc->reset = whcrc_reset;
	uwb_rc->start = whcrc_start_rc;
	uwb_rc->stop  = whcrc_stop_rc;

	result = uwb_rc_add(uwb_rc, dev, whcrc);
	if (result < 0)
		goto error_rc_add;
	umc_set_drvdata(umc_dev, whcrc);
	return 0;

error_rc_add:
	whcrc_release_rc_umc(whcrc);
error_setup_rc_umc:
	kfree(whcrc);
error_alloc:
	uwb_rc_put(uwb_rc);
error_rc_alloc:
	return result;
}

/**
 * Clean up the radio control resources
 *
 * When we up the command semaphore, everybody possibly held trying to
 * execute a command should be granted entry and then they'll see the
 * host is quiescing and up it (so it will chain to the next waiter).
 * This should not happen (in any case), as we can only remove when
 * there are no handles open...
 */
static void whcrc_remove(struct umc_dev *umc_dev)
{
	struct whcrc *whcrc = umc_get_drvdata(umc_dev);
	struct uwb_rc *uwb_rc = whcrc->uwb_rc;

	umc_set_drvdata(umc_dev, NULL);
	uwb_rc_rm(uwb_rc);
	whcrc_release_rc_umc(whcrc);
	kfree(whcrc);
	uwb_rc_put(uwb_rc);
}

static int whcrc_pre_reset(struct umc_dev *umc)
{
	struct whcrc *whcrc = umc_get_drvdata(umc);
	struct uwb_rc *uwb_rc = whcrc->uwb_rc;

	uwb_rc_pre_reset(uwb_rc);
	return 0;
}

static int whcrc_post_reset(struct umc_dev *umc)
{
	struct whcrc *whcrc = umc_get_drvdata(umc);
	struct uwb_rc *uwb_rc = whcrc->uwb_rc;

	return uwb_rc_post_reset(uwb_rc);
}

/* PCI device ID's that we handle [so it gets loaded] */
static struct pci_device_id __used whcrc_id_table[] = {
	{ PCI_DEVICE_CLASS(PCI_CLASS_WIRELESS_WHCI, ~0) },
	{ /* empty last entry */ }
};
MODULE_DEVICE_TABLE(pci, whcrc_id_table);

static struct umc_driver whcrc_driver = {
	.name       = "whc-rc",
	.cap_id     = UMC_CAP_ID_WHCI_RC,
	.probe      = whcrc_probe,
	.remove     = whcrc_remove,
	.pre_reset  = whcrc_pre_reset,
	.post_reset = whcrc_post_reset,
};

static int __init whcrc_driver_init(void)
{
	return umc_driver_register(&whcrc_driver);
}
module_init(whcrc_driver_init);

static void __exit whcrc_driver_exit(void)
{
	umc_driver_unregister(&whcrc_driver);
}
module_exit(whcrc_driver_exit);

MODULE_AUTHOR("Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>");
MODULE_DESCRIPTION("Wireless Host Controller Radio Control Driver");
MODULE_LICENSE("GPL");
