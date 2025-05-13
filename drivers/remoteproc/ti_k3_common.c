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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TI K3 common Remoteproc code");
