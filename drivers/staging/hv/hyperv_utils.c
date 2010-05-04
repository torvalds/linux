/*
 * Copyright (c) 2010, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/version.h>

#include "logging.h"
#include "osd.h"
#include "vmbus.h"
#include "VmbusPacketFormat.h"
#include "VmbusChannelInterface.h"
#include "VersionInfo.h"
#include "Channel.h"
#include "VmbusPrivate.h"
#include "VmbusApi.h"
#include "utils.h"


void shutdown_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u8 *buf;
	u32 buflen, recvlen;
	u64 requestid;
	u8  execute_shutdown = false;

	struct shutdown_msg_data *shutdown_msg;

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop = NULL;

	DPRINT_ENTER(VMBUS);

	buflen = PAGE_SIZE;
	buf = kmalloc(buflen, GFP_ATOMIC);

	VmbusChannelRecvPacket(channel, buf, buflen, &recvlen, &requestid);

	if (recvlen > 0) {
		DPRINT_DBG(VMBUS, "shutdown packet: len=%d, requestid=%lld",
			   recvlen, requestid);

		icmsghdrp = (struct icmsg_hdr *)&buf[
			sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			prep_negotiate_resp(icmsghdrp, negop, buf);
		} else {
			shutdown_msg = (struct shutdown_msg_data *)&buf[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

			switch (shutdown_msg->flags) {
			case 0:
			case 1:
				icmsghdrp->status = HV_S_OK;
				execute_shutdown = true;

				DPRINT_INFO(VMBUS, "Shutdown request received -"
					    " gracefull shutdown initiated");
				break;
			default:
				icmsghdrp->status = HV_E_FAIL;
				execute_shutdown = false;

				DPRINT_INFO(VMBUS, "Shutdown request received -"
					    " Invalid request");
				break;
			};
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		VmbusChannelSendPacket(channel, buf,
				       recvlen, requestid,
				       VmbusPacketTypeDataInBand, 0);
	}

	kfree(buf);

	DPRINT_EXIT(VMBUS);

	if (execute_shutdown == true)
		shutdown_linux_system();
}

static int __init init_hyperv_utils(void)
{
	printk(KERN_INFO "Registering HyperV Utility Driver\n");

	hv_cb_utils[HV_SHUTDOWN_MSG].channel->OnChannelCallback =
		&shutdown_onchannelcallback;
	hv_cb_utils[HV_SHUTDOWN_MSG].callback = &shutdown_onchannelcallback;

	return 0;
}

static void exit_hyperv_utils(void)
{
	printk(KERN_INFO "De-Registered HyperV Utility Driver\n");

	hv_cb_utils[HV_SHUTDOWN_MSG].channel->OnChannelCallback =
		&chn_cb_negotiate;
	hv_cb_utils[HV_SHUTDOWN_MSG].callback = &chn_cb_negotiate;
}

module_init(init_hyperv_utils);
module_exit(exit_hyperv_utils);

MODULE_DESCRIPTION("Hyper-V Utilities");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_LICENSE("GPL");
