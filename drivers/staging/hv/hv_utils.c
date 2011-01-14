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
#include <linux/reboot.h>
#include <linux/dmi.h>
#include <linux/pci.h>

#include "logging.h"
#include "osd.h"
#include "vmbus.h"
#include "vmbus_packet_format.h"
#include "vmbus_channel_interface.h"
#include "version_info.h"
#include "channel.h"
#include "vmbus_private.h"
#include "vmbus_api.h"
#include "utils.h"


static void shutdown_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u8 *buf;
	u32 buflen, recvlen;
	u64 requestid;
	u8  execute_shutdown = false;

	struct shutdown_msg_data *shutdown_msg;

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop = NULL;

	buflen = PAGE_SIZE;
	buf = kmalloc(buflen, GFP_ATOMIC);

	vmbus_recvpacket(channel, buf, buflen, &recvlen, &requestid);

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

		vmbus_sendpacket(channel, buf,
				       recvlen, requestid,
				       VmbusPacketTypeDataInBand, 0);
	}

	kfree(buf);

	if (execute_shutdown == true)
		orderly_poweroff(false);
}

/*
 * Set guest time to host UTC time.
 */
static inline void do_adj_guesttime(u64 hosttime)
{
	s64 host_tns;
	struct timespec host_ts;

	host_tns = (hosttime - WLTIMEDELTA) * 100;
	host_ts = ns_to_timespec(host_tns);

	do_settimeofday(&host_ts);
}

/*
 * Synchronize time with host after reboot, restore, etc.
 *
 * ICTIMESYNCFLAG_SYNC flag bit indicates reboot, restore events of the VM.
 * After reboot the flag ICTIMESYNCFLAG_SYNC is included in the first time
 * message after the timesync channel is opened. Since the hv_utils module is
 * loaded after hv_vmbus, the first message is usually missed. The other
 * thing is, systime is automatically set to emulated hardware clock which may
 * not be UTC time or in the same time zone. So, to override these effects, we
 * use the first 50 time samples for initial system time setting.
 */
static inline void adj_guesttime(u64 hosttime, u8 flags)
{
	static s32 scnt = 50;

	if ((flags & ICTIMESYNCFLAG_SYNC) != 0) {
		do_adj_guesttime(hosttime);
		return;
	}

	if ((flags & ICTIMESYNCFLAG_SAMPLE) != 0 && scnt > 0) {
		scnt--;
		do_adj_guesttime(hosttime);
	}
}

/*
 * Time Sync Channel message handler.
 */
static void timesync_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u8 *buf;
	u32 buflen, recvlen;
	u64 requestid;
	struct icmsg_hdr *icmsghdrp;
	struct ictimesync_data *timedatap;

	buflen = PAGE_SIZE;
	buf = kmalloc(buflen, GFP_ATOMIC);

	vmbus_recvpacket(channel, buf, buflen, &recvlen, &requestid);

	if (recvlen > 0) {
		DPRINT_DBG(VMBUS, "timesync packet: recvlen=%d, requestid=%lld",
			recvlen, requestid);

		icmsghdrp = (struct icmsg_hdr *)&buf[
				sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			prep_negotiate_resp(icmsghdrp, NULL, buf);
		} else {
			timedatap = (struct ictimesync_data *)&buf[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];
			adj_guesttime(timedatap->parenttime, timedatap->flags);
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, buf,
				recvlen, requestid,
				VmbusPacketTypeDataInBand, 0);
	}

	kfree(buf);
}

/*
 * Heartbeat functionality.
 * Every two seconds, Hyper-V send us a heartbeat request message.
 * we respond to this message, and Hyper-V knows we are alive.
 */
static void heartbeat_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u8 *buf;
	u32 buflen, recvlen;
	u64 requestid;
	struct icmsg_hdr *icmsghdrp;
	struct heartbeat_msg_data *heartbeat_msg;

	buflen = PAGE_SIZE;
	buf = kmalloc(buflen, GFP_ATOMIC);

	vmbus_recvpacket(channel, buf, buflen, &recvlen, &requestid);

	if (recvlen > 0) {
		DPRINT_DBG(VMBUS, "heartbeat packet: len=%d, requestid=%lld",
			   recvlen, requestid);

		icmsghdrp = (struct icmsg_hdr *)&buf[
				sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			prep_negotiate_resp(icmsghdrp, NULL, buf);
		} else {
			heartbeat_msg = (struct heartbeat_msg_data *)&buf[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

			DPRINT_DBG(VMBUS, "heartbeat seq = %lld",
				   heartbeat_msg->seq_num);

			heartbeat_msg->seq_num += 1;
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, buf,
				       recvlen, requestid,
				       VmbusPacketTypeDataInBand, 0);
	}

	kfree(buf);
}

static const struct pci_device_id __initconst
hv_utils_pci_table[] __maybe_unused = {
	{ PCI_DEVICE(0x1414, 0x5353) }, /* Hyper-V emulated VGA controller */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, hv_utils_pci_table);


static const struct dmi_system_id __initconst
hv_utils_dmi_table[] __maybe_unused  = {
	{
		.ident = "Hyper-V",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Virtual Machine"),
			DMI_MATCH(DMI_BOARD_NAME, "Virtual Machine"),
		},
	},
	{ },
};
MODULE_DEVICE_TABLE(dmi, hv_utils_dmi_table);


static int __init init_hyperv_utils(void)
{
	printk(KERN_INFO "Registering HyperV Utility Driver\n");

	if (!dmi_check_system(hv_utils_dmi_table))
		return -ENODEV;

	hv_cb_utils[HV_SHUTDOWN_MSG].channel->OnChannelCallback =
		&shutdown_onchannelcallback;
	hv_cb_utils[HV_SHUTDOWN_MSG].callback = &shutdown_onchannelcallback;

	hv_cb_utils[HV_TIMESYNC_MSG].channel->OnChannelCallback =
		&timesync_onchannelcallback;
	hv_cb_utils[HV_TIMESYNC_MSG].callback = &timesync_onchannelcallback;

	hv_cb_utils[HV_HEARTBEAT_MSG].channel->OnChannelCallback =
		&heartbeat_onchannelcallback;
	hv_cb_utils[HV_HEARTBEAT_MSG].callback = &heartbeat_onchannelcallback;

	return 0;
}

static void exit_hyperv_utils(void)
{
	printk(KERN_INFO "De-Registered HyperV Utility Driver\n");

	hv_cb_utils[HV_SHUTDOWN_MSG].channel->OnChannelCallback =
		&chn_cb_negotiate;
	hv_cb_utils[HV_SHUTDOWN_MSG].callback = &chn_cb_negotiate;

	hv_cb_utils[HV_TIMESYNC_MSG].channel->OnChannelCallback =
		&chn_cb_negotiate;
	hv_cb_utils[HV_TIMESYNC_MSG].callback = &chn_cb_negotiate;

	hv_cb_utils[HV_HEARTBEAT_MSG].channel->OnChannelCallback =
		&chn_cb_negotiate;
	hv_cb_utils[HV_HEARTBEAT_MSG].callback = &chn_cb_negotiate;
}

module_init(init_hyperv_utils);
module_exit(exit_hyperv_utils);

MODULE_DESCRIPTION("Hyper-V Utilities");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_LICENSE("GPL");
