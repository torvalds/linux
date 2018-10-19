/*
 * Implementation of the Xen vTPM device frontend
 *
 * Author:  Daniel De Graaf <dgdegra@tycho.nsa.gov>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/freezer.h>
#include <xen/xen.h>
#include <xen/events.h>
#include <xen/interface/io/tpmif.h>
#include <xen/grant_table.h>
#include <xen/xenbus.h>
#include <xen/page.h>
#include "tpm.h"
#include <xen/platform_pci.h>

struct tpm_private {
	struct tpm_chip *chip;
	struct xenbus_device *dev;

	struct vtpm_shared_page *shr;

	unsigned int evtchn;
	int ring_ref;
	domid_t backend_id;
	int irq;
	wait_queue_head_t read_queue;
};

enum status_bits {
	VTPM_STATUS_RUNNING  = 0x1,
	VTPM_STATUS_IDLE     = 0x2,
	VTPM_STATUS_RESULT   = 0x4,
	VTPM_STATUS_CANCELED = 0x8,
};

static bool wait_for_tpm_stat_cond(struct tpm_chip *chip, u8 mask,
					bool check_cancel, bool *canceled)
{
	u8 status = chip->ops->status(chip);

	*canceled = false;
	if ((status & mask) == mask)
		return true;
	if (check_cancel && chip->ops->req_canceled(chip, status)) {
		*canceled = true;
		return true;
	}
	return false;
}

static int wait_for_tpm_stat(struct tpm_chip *chip, u8 mask,
		unsigned long timeout, wait_queue_head_t *queue,
		bool check_cancel)
{
	unsigned long stop;
	long rc;
	u8 status;
	bool canceled = false;

	/* check current status */
	status = chip->ops->status(chip);
	if ((status & mask) == mask)
		return 0;

	stop = jiffies + timeout;

	if (chip->flags & TPM_CHIP_FLAG_IRQ) {
again:
		timeout = stop - jiffies;
		if ((long)timeout <= 0)
			return -ETIME;
		rc = wait_event_interruptible_timeout(*queue,
			wait_for_tpm_stat_cond(chip, mask, check_cancel,
					       &canceled),
			timeout);
		if (rc > 0) {
			if (canceled)
				return -ECANCELED;
			return 0;
		}
		if (rc == -ERESTARTSYS && freezing(current)) {
			clear_thread_flag(TIF_SIGPENDING);
			goto again;
		}
	} else {
		do {
			tpm_msleep(TPM_TIMEOUT);
			status = chip->ops->status(chip);
			if ((status & mask) == mask)
				return 0;
		} while (time_before(jiffies, stop));
	}
	return -ETIME;
}

static u8 vtpm_status(struct tpm_chip *chip)
{
	struct tpm_private *priv = dev_get_drvdata(&chip->dev);
	switch (priv->shr->state) {
	case VTPM_STATE_IDLE:
		return VTPM_STATUS_IDLE | VTPM_STATUS_CANCELED;
	case VTPM_STATE_FINISH:
		return VTPM_STATUS_IDLE | VTPM_STATUS_RESULT;
	case VTPM_STATE_SUBMIT:
	case VTPM_STATE_CANCEL: /* cancel requested, not yet canceled */
		return VTPM_STATUS_RUNNING;
	default:
		return 0;
	}
}

static bool vtpm_req_canceled(struct tpm_chip *chip, u8 status)
{
	return status & VTPM_STATUS_CANCELED;
}

static void vtpm_cancel(struct tpm_chip *chip)
{
	struct tpm_private *priv = dev_get_drvdata(&chip->dev);
	priv->shr->state = VTPM_STATE_CANCEL;
	wmb();
	notify_remote_via_evtchn(priv->evtchn);
}

static unsigned int shr_data_offset(struct vtpm_shared_page *shr)
{
	return sizeof(*shr) + sizeof(u32) * shr->nr_extra_pages;
}

static int vtpm_send(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct tpm_private *priv = dev_get_drvdata(&chip->dev);
	struct vtpm_shared_page *shr = priv->shr;
	unsigned int offset = shr_data_offset(shr);

	u32 ordinal;
	unsigned long duration;

	if (offset > PAGE_SIZE)
		return -EINVAL;

	if (offset + count > PAGE_SIZE)
		return -EINVAL;

	/* Wait for completion of any existing command or cancellation */
	if (wait_for_tpm_stat(chip, VTPM_STATUS_IDLE, chip->timeout_c,
			&priv->read_queue, true) < 0) {
		vtpm_cancel(chip);
		return -ETIME;
	}

	memcpy(offset + (u8 *)shr, buf, count);
	shr->length = count;
	barrier();
	shr->state = VTPM_STATE_SUBMIT;
	wmb();
	notify_remote_via_evtchn(priv->evtchn);

	ordinal = be32_to_cpu(((struct tpm_input_header*)buf)->ordinal);
	duration = tpm1_calc_ordinal_duration(chip, ordinal);

	if (wait_for_tpm_stat(chip, VTPM_STATUS_IDLE, duration,
			&priv->read_queue, true) < 0) {
		/* got a signal or timeout, try to cancel */
		vtpm_cancel(chip);
		return -ETIME;
	}

	return count;
}

static int vtpm_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct tpm_private *priv = dev_get_drvdata(&chip->dev);
	struct vtpm_shared_page *shr = priv->shr;
	unsigned int offset = shr_data_offset(shr);
	size_t length = shr->length;

	if (shr->state == VTPM_STATE_IDLE)
		return -ECANCELED;

	/* In theory the wait at the end of _send makes this one unnecessary */
	if (wait_for_tpm_stat(chip, VTPM_STATUS_RESULT, chip->timeout_c,
			&priv->read_queue, true) < 0) {
		vtpm_cancel(chip);
		return -ETIME;
	}

	if (offset > PAGE_SIZE)
		return -EIO;

	if (offset + length > PAGE_SIZE)
		length = PAGE_SIZE - offset;

	if (length > count)
		length = count;

	memcpy(buf, offset + (u8 *)shr, length);

	return length;
}

static const struct tpm_class_ops tpm_vtpm = {
	.status = vtpm_status,
	.recv = vtpm_recv,
	.send = vtpm_send,
	.cancel = vtpm_cancel,
	.req_complete_mask = VTPM_STATUS_IDLE | VTPM_STATUS_RESULT,
	.req_complete_val  = VTPM_STATUS_IDLE | VTPM_STATUS_RESULT,
	.req_canceled      = vtpm_req_canceled,
};

static irqreturn_t tpmif_interrupt(int dummy, void *dev_id)
{
	struct tpm_private *priv = dev_id;

	switch (priv->shr->state) {
	case VTPM_STATE_IDLE:
	case VTPM_STATE_FINISH:
		wake_up_interruptible(&priv->read_queue);
		break;
	case VTPM_STATE_SUBMIT:
	case VTPM_STATE_CANCEL:
	default:
		break;
	}
	return IRQ_HANDLED;
}

static int setup_chip(struct device *dev, struct tpm_private *priv)
{
	struct tpm_chip *chip;

	chip = tpmm_chip_alloc(dev, &tpm_vtpm);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	init_waitqueue_head(&priv->read_queue);

	priv->chip = chip;
	dev_set_drvdata(&chip->dev, priv);

	return 0;
}

/* caller must clean up in case of errors */
static int setup_ring(struct xenbus_device *dev, struct tpm_private *priv)
{
	struct xenbus_transaction xbt;
	const char *message = NULL;
	int rv;
	grant_ref_t gref;

	priv->shr = (void *)__get_free_page(GFP_KERNEL|__GFP_ZERO);
	if (!priv->shr) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating shared ring");
		return -ENOMEM;
	}

	rv = xenbus_grant_ring(dev, priv->shr, 1, &gref);
	if (rv < 0)
		return rv;

	priv->ring_ref = gref;

	rv = xenbus_alloc_evtchn(dev, &priv->evtchn);
	if (rv)
		return rv;

	rv = bind_evtchn_to_irqhandler(priv->evtchn, tpmif_interrupt, 0,
				       "tpmif", priv);
	if (rv <= 0) {
		xenbus_dev_fatal(dev, rv, "allocating TPM irq");
		return rv;
	}
	priv->irq = rv;

 again:
	rv = xenbus_transaction_start(&xbt);
	if (rv) {
		xenbus_dev_fatal(dev, rv, "starting transaction");
		return rv;
	}

	rv = xenbus_printf(xbt, dev->nodename,
			"ring-ref", "%u", priv->ring_ref);
	if (rv) {
		message = "writing ring-ref";
		goto abort_transaction;
	}

	rv = xenbus_printf(xbt, dev->nodename, "event-channel", "%u",
			priv->evtchn);
	if (rv) {
		message = "writing event-channel";
		goto abort_transaction;
	}

	rv = xenbus_printf(xbt, dev->nodename, "feature-protocol-v2", "1");
	if (rv) {
		message = "writing feature-protocol-v2";
		goto abort_transaction;
	}

	rv = xenbus_transaction_end(xbt, 0);
	if (rv == -EAGAIN)
		goto again;
	if (rv) {
		xenbus_dev_fatal(dev, rv, "completing transaction");
		return rv;
	}

	xenbus_switch_state(dev, XenbusStateInitialised);

	return 0;

 abort_transaction:
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_error(dev, rv, "%s", message);

	return rv;
}

static void ring_free(struct tpm_private *priv)
{
	if (!priv)
		return;

	if (priv->ring_ref)
		gnttab_end_foreign_access(priv->ring_ref, 0,
				(unsigned long)priv->shr);
	else
		free_page((unsigned long)priv->shr);

	if (priv->irq)
		unbind_from_irqhandler(priv->irq, priv);

	kfree(priv);
}

static int tpmfront_probe(struct xenbus_device *dev,
		const struct xenbus_device_id *id)
{
	struct tpm_private *priv;
	int rv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating priv structure");
		return -ENOMEM;
	}

	rv = setup_chip(&dev->dev, priv);
	if (rv) {
		kfree(priv);
		return rv;
	}

	rv = setup_ring(dev, priv);
	if (rv) {
		ring_free(priv);
		return rv;
	}

	tpm_get_timeouts(priv->chip);

	return tpm_chip_register(priv->chip);
}

static int tpmfront_remove(struct xenbus_device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(&dev->dev);
	struct tpm_private *priv = dev_get_drvdata(&chip->dev);
	tpm_chip_unregister(chip);
	ring_free(priv);
	dev_set_drvdata(&chip->dev, NULL);
	return 0;
}

static int tpmfront_resume(struct xenbus_device *dev)
{
	/* A suspend/resume/migrate will interrupt a vTPM anyway */
	tpmfront_remove(dev);
	return tpmfront_probe(dev, NULL);
}

static void backend_changed(struct xenbus_device *dev,
		enum xenbus_state backend_state)
{
	switch (backend_state) {
	case XenbusStateInitialised:
	case XenbusStateConnected:
		if (dev->state == XenbusStateConnected)
			break;

		if (!xenbus_read_unsigned(dev->otherend, "feature-protocol-v2",
					  0)) {
			xenbus_dev_fatal(dev, -EINVAL,
					"vTPM protocol 2 required");
			return;
		}
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
	case XenbusStateClosed:
		device_unregister(&dev->dev);
		xenbus_frontend_closed(dev);
		break;
	default:
		break;
	}
}

static const struct xenbus_device_id tpmfront_ids[] = {
	{ "vtpm" },
	{ "" }
};
MODULE_ALIAS("xen:vtpm");

static struct xenbus_driver tpmfront_driver = {
	.ids = tpmfront_ids,
	.probe = tpmfront_probe,
	.remove = tpmfront_remove,
	.resume = tpmfront_resume,
	.otherend_changed = backend_changed,
};

static int __init xen_tpmfront_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	if (!xen_has_pv_devices())
		return -ENODEV;

	return xenbus_register_frontend(&tpmfront_driver);
}
module_init(xen_tpmfront_init);

static void __exit xen_tpmfront_exit(void)
{
	xenbus_unregister_driver(&tpmfront_driver);
}
module_exit(xen_tpmfront_exit);

MODULE_AUTHOR("Daniel De Graaf <dgdegra@tycho.nsa.gov>");
MODULE_DESCRIPTION("Xen vTPM Driver");
MODULE_LICENSE("GPL");
