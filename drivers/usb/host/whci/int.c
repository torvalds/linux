// SPDX-License-Identifier: GPL-2.0
/*
 * Wireless Host Controller (WHC) interrupt handling.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/uwb/umc.h>

#include "../../wusbcore/wusbhc.h"

#include "whcd.h"

static void transfer_done(struct whc *whc)
{
	queue_work(whc->workqueue, &whc->async_work);
	queue_work(whc->workqueue, &whc->periodic_work);
}

irqreturn_t whc_int_handler(struct usb_hcd *hcd)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(hcd);
	struct whc *whc = wusbhc_to_whc(wusbhc);
	u32 sts;

	sts = le_readl(whc->base + WUSBSTS);
	if (!(sts & WUSBSTS_INT_MASK))
		return IRQ_NONE;
	le_writel(sts & WUSBSTS_INT_MASK, whc->base + WUSBSTS);

	if (sts & WUSBSTS_GEN_CMD_DONE)
		wake_up(&whc->cmd_wq);

	if (sts & WUSBSTS_HOST_ERR)
		dev_err(&whc->umc->dev, "FIXME: host system error\n");

	if (sts & WUSBSTS_ASYNC_SCHED_SYNCED)
		wake_up(&whc->async_list_wq);

	if (sts & WUSBSTS_PERIODIC_SCHED_SYNCED)
		wake_up(&whc->periodic_list_wq);

	if (sts & WUSBSTS_DNTS_INT)
		queue_work(whc->workqueue, &whc->dn_work);

	/*
	 * A transfer completed (see [WHCI] section 4.7.1.2 for when
	 * this occurs).
	 */
	if (sts & (WUSBSTS_INT | WUSBSTS_ERR_INT))
		transfer_done(whc);

	return IRQ_HANDLED;
}

static int process_dn_buf(struct whc *whc)
{
	struct wusbhc *wusbhc = &whc->wusbhc;
	struct dn_buf_entry *dn;
	int processed = 0;

	for (dn = whc->dn_buf; dn < whc->dn_buf + WHC_N_DN_ENTRIES; dn++) {
		if (dn->status & WHC_DN_STATUS_VALID) {
			wusbhc_handle_dn(wusbhc, dn->src_addr,
					 (struct wusb_dn_hdr *)dn->dn_data,
					 dn->msg_size);
			dn->status &= ~WHC_DN_STATUS_VALID;
			processed++;
		}
	}
	return processed;
}

void whc_dn_work(struct work_struct *work)
{
	struct whc *whc = container_of(work, struct whc, dn_work);
	int processed;

	do {
		processed = process_dn_buf(whc);
	} while (processed);
}
