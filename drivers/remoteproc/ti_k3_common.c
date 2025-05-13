// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI K3 Remote Processor(s) driver common code
 *
 * Refactored out of ti_k3_r5_remoteproc.c, ti_k3_dsp_remoteproc.c and
 * ti_k3_m4_remoteproc.c.
 *
 * ti_k3_r5_remoteproc.c:
 * Copyright (C) 2017-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 *
 * ti_k3_dsp_remoteproc.c:
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 *
 * ti_k3_m4_remoteproc.c:
 * Copyright (C) 2021-2024 Texas Instruments Incorporated - https://www.ti.com/
 *	Hari Nagalla <hnagalla@ti.com>
 */

#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/omap-mailbox.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "omap_remoteproc.h"
#include "remoteproc_internal.h"
#include "ti_sci_proc.h"
#include "ti_k3_common.h"

/**
 * k3_rproc_mbox_callback() - inbound mailbox message handler
 * @client: mailbox client pointer used for requesting the mailbox channel
 * @data: mailbox payload
 *
 * This handler is invoked by the K3 mailbox driver whenever a mailbox
 * message is received. Usually, the mailbox payload simply contains
 * the index of the virtqueue that is kicked by the remote processor,
 * and we let remoteproc core handle it.
 *
 * In addition to virtqueue indices, we also have some out-of-band values
 * that indicate different events. Those values are deliberately very
 * large so they don't coincide with virtqueue indices.
 */
void k3_rproc_mbox_callback(struct mbox_client *client, void *data)
{
	struct k3_rproc *kproc = container_of(client, struct k3_rproc, client);
	struct device *dev = kproc->rproc->dev.parent;
	struct rproc *rproc = kproc->rproc;
	u32 msg = (u32)(uintptr_t)(data);

	dev_dbg(dev, "mbox msg: 0x%x\n", msg);

	switch (msg) {
	case RP_MBOX_CRASH:
		/*
		 * remoteproc detected an exception, but error recovery is not
		 * supported. So, just log this for now
		 */
		dev_err(dev, "K3 rproc %s crashed\n", rproc->name);
		break;
	case RP_MBOX_ECHO_REPLY:
		dev_info(dev, "received echo reply from %s\n", rproc->name);
		break;
	default:
		/* silently handle all other valid messages */
		if (msg >= RP_MBOX_READY && msg < RP_MBOX_END_MSG)
			return;
		if (msg > rproc->max_notifyid) {
			dev_dbg(dev, "dropping unknown message 0x%x", msg);
			return;
		}
		/* msg contains the index of the triggered vring */
		if (rproc_vq_interrupt(rproc, msg) == IRQ_NONE)
			dev_dbg(dev, "no message was found in vqid %d\n", msg);
	}
}
EXPORT_SYMBOL_GPL(k3_rproc_mbox_callback);

/*
 * Kick the remote processor to notify about pending unprocessed messages.
 * The vqid usage is not used and is inconsequential, as the kick is performed
 * through a simulated GPIO (a bit in an IPC interrupt-triggering register),
 * the remote processor is expected to process both its Tx and Rx virtqueues.
 */
void k3_rproc_kick(struct rproc *rproc, int vqid)
{
	struct k3_rproc *kproc = rproc->priv;
	struct device *dev = kproc->dev;
	u32 msg = (u32)vqid;
	int ret;

	/*
	 * Send the index of the triggered virtqueue in the mailbox payload.
	 * NOTE: msg is cast to uintptr_t to prevent compiler warnings when
	 * void* is 64bit. It is safely cast back to u32 in the mailbox driver.
	 */
	ret = mbox_send_message(kproc->mbox, (void *)(uintptr_t)msg);
	if (ret < 0)
		dev_err(dev, "failed to send mailbox message, status = %d\n",
			ret);
}
EXPORT_SYMBOL_GPL(k3_rproc_kick);

/* Put the remote processor into reset */
int k3_rproc_reset(struct k3_rproc *kproc)
{
	struct device *dev = kproc->dev;
	int ret;

	if (kproc->data->uses_lreset) {
		ret = reset_control_assert(kproc->reset);
		if (ret)
			dev_err(dev, "local-reset assert failed (%pe)\n", ERR_PTR(ret));
	} else {
		ret = kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
							    kproc->ti_sci_id);
		if (ret)
			dev_err(dev, "module-reset assert failed (%pe)\n", ERR_PTR(ret));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(k3_rproc_reset);

/* Release the remote processor from reset */
int k3_rproc_release(struct k3_rproc *kproc)
{
	struct device *dev = kproc->dev;
	int ret;

	if (kproc->data->uses_lreset) {
		ret = reset_control_deassert(kproc->reset);
		if (ret) {
			dev_err(dev, "local-reset deassert failed, (%pe)\n", ERR_PTR(ret));
			if (kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
								  kproc->ti_sci_id))
				dev_warn(dev, "module-reset assert back failed\n");
		}
	} else {
		ret = kproc->ti_sci->ops.dev_ops.get_device(kproc->ti_sci,
							    kproc->ti_sci_id);
		if (ret)
			dev_err(dev, "module-reset deassert failed (%pe)\n", ERR_PTR(ret));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(k3_rproc_release);

int k3_rproc_request_mbox(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct mbox_client *client = &kproc->client;
	struct device *dev = kproc->dev;
	int ret;

	client->dev = dev;
	client->tx_done = NULL;
	client->rx_callback = k3_rproc_mbox_callback;
	client->tx_block = false;
	client->knows_txdone = false;

	kproc->mbox = mbox_request_channel(client, 0);
	if (IS_ERR(kproc->mbox))
		return dev_err_probe(dev, PTR_ERR(kproc->mbox),
				     "mbox_request_channel failed\n");

	/*
	 * Ping the remote processor, this is only for sanity-sake for now;
	 * there is no functional effect whatsoever.
	 *
	 * Note that the reply will _not_ arrive immediately: this message
	 * will wait in the mailbox fifo until the remote processor is booted.
	 */
	ret = mbox_send_message(kproc->mbox, (void *)RP_MBOX_ECHO_REQUEST);
	if (ret < 0) {
		dev_err(dev, "mbox_send_message failed (%pe)\n", ERR_PTR(ret));
		mbox_free_channel(kproc->mbox);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(k3_rproc_request_mbox);

/*
 * The K3 DSP and M4 cores have a local reset that affects only the CPU, and a
 * generic module reset that powers on the device and allows the internal
 * memories to be accessed while the local reset is asserted. This function is
 * used to release the global reset on remote cores to allow loading into the
 * internal RAMs. The .prepare() ops is invoked by remoteproc core before any
 * firmware loading, and is followed by the .start() ops after loading to
 * actually let the remote cores to run.
 */
int k3_rproc_prepare(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct device *dev = kproc->dev;
	int ret;

	/* If the core is running already no need to deassert the module reset */
	if (rproc->state == RPROC_DETACHED)
		return 0;

	/*
	 * Ensure the local reset is asserted so the core doesn't
	 * execute bogus code when the module reset is released.
	 */
	if (kproc->data->uses_lreset) {
		ret = k3_rproc_reset(kproc);
		if (ret)
			return ret;

		ret = reset_control_status(kproc->reset);
		if (ret <= 0) {
			dev_err(dev, "local reset still not asserted\n");
			return ret;
		}
	}

	ret = kproc->ti_sci->ops.dev_ops.get_device(kproc->ti_sci,
						    kproc->ti_sci_id);
	if (ret) {
		dev_err(dev, "could not deassert module-reset for internal RAM loading\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(k3_rproc_prepare);

/*
 * This function implements the .unprepare() ops and performs the complimentary
 * operations to that of the .prepare() ops. The function is used to assert the
 * global reset on applicable K3 DSP and M4 cores. This completes the second
 * portion of powering down the remote core. The cores themselves are only
 * halted in the .stop() callback through the local reset, and the .unprepare()
 * ops is invoked by the remoteproc core after the remoteproc is stopped to
 * balance the global reset.
 */
int k3_rproc_unprepare(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct device *dev = kproc->dev;
	int ret;

	/* If the core is going to be detached do not assert the module reset */
	if (rproc->state == RPROC_DETACHED)
		return 0;

	ret = kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
						    kproc->ti_sci_id);
	if (ret) {
		dev_err(dev, "module-reset assert failed\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(k3_rproc_unprepare);

/*
 * Power up the remote processor.
 *
 * This function will be invoked only after the firmware for this rproc
 * was loaded, parsed successfully, and all of its resource requirements
 * were met. This callback is invoked only in remoteproc mode.
 */
int k3_rproc_start(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;

	return k3_rproc_release(kproc);
}
EXPORT_SYMBOL_GPL(k3_rproc_start);

/*
 * Stop the remote processor.
 *
 * This function puts the remote processor into reset, and finishes processing
 * of any pending messages. This callback is invoked only in remoteproc mode.
 */
int k3_rproc_stop(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;

	return k3_rproc_reset(kproc);
}
EXPORT_SYMBOL_GPL(k3_rproc_stop);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TI K3 common Remoteproc code");
