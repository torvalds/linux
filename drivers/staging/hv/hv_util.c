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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/reboot.h>

#include "hyperv.h"
#include "hv_kvp.h"

static u8 *shut_txf_buf;
static u8 *time_txf_buf;
static u8 *hbeat_txf_buf;

static void shutdown_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u32 recvlen;
	u64 requestid;
	u8  execute_shutdown = false;

	struct shutdown_msg_data *shutdown_msg;

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop = NULL;

	vmbus_recvpacket(channel, shut_txf_buf,
			 PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&shut_txf_buf[
			sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			prep_negotiate_resp(icmsghdrp, negop, shut_txf_buf);
		} else {
			shutdown_msg =
				(struct shutdown_msg_data *)&shut_txf_buf[
					sizeof(struct vmbuspipe_hdr) +
					sizeof(struct icmsg_hdr)];

			switch (shutdown_msg->flags) {
			case 0:
			case 1:
				icmsghdrp->status = HV_S_OK;
				execute_shutdown = true;

				pr_info("Shutdown request received -"
					    " graceful shutdown initiated\n");
				break;
			default:
				icmsghdrp->status = HV_E_FAIL;
				execute_shutdown = false;

				pr_info("Shutdown request received -"
					    " Invalid request\n");
				break;
			}
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, shut_txf_buf,
				       recvlen, requestid,
				       VM_PKT_DATA_INBAND, 0);
	}

	if (execute_shutdown == true)
		orderly_poweroff(true);
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
	u32 recvlen;
	u64 requestid;
	struct icmsg_hdr *icmsghdrp;
	struct ictimesync_data *timedatap;

	vmbus_recvpacket(channel, time_txf_buf,
			 PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&time_txf_buf[
				sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			prep_negotiate_resp(icmsghdrp, NULL, time_txf_buf);
		} else {
			timedatap = (struct ictimesync_data *)&time_txf_buf[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];
			adj_guesttime(timedatap->parenttime, timedatap->flags);
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, time_txf_buf,
				recvlen, requestid,
				VM_PKT_DATA_INBAND, 0);
	}
}

/*
 * Heartbeat functionality.
 * Every two seconds, Hyper-V send us a heartbeat request message.
 * we respond to this message, and Hyper-V knows we are alive.
 */
static void heartbeat_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u32 recvlen;
	u64 requestid;
	struct icmsg_hdr *icmsghdrp;
	struct heartbeat_msg_data *heartbeat_msg;

	vmbus_recvpacket(channel, hbeat_txf_buf,
			 PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&hbeat_txf_buf[
				sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			prep_negotiate_resp(icmsghdrp, NULL, hbeat_txf_buf);
		} else {
			heartbeat_msg =
				(struct heartbeat_msg_data *)&hbeat_txf_buf[
					sizeof(struct vmbuspipe_hdr) +
					sizeof(struct icmsg_hdr)];

			heartbeat_msg->seq_num += 1;
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, hbeat_txf_buf,
				       recvlen, requestid,
				       VM_PKT_DATA_INBAND, 0);
	}
}

/*
 * The devices managed by the util driver don't need any additional
 * setup.
 */
static int util_probe(struct hv_device *dev)
{
	return 0;
}

static int util_remove(struct hv_device *dev)
{
	return 0;
}

static const struct hv_vmbus_device_id id_table[] = {
	/* Shutdown guid */
	{ VMBUS_DEVICE(0x31, 0x60, 0x0B, 0X0E, 0x13, 0x52, 0x34, 0x49,
		       0x81, 0x8B, 0x38, 0XD9, 0x0C, 0xED, 0x39, 0xDB) },
	/* Time synch guid */
	{ VMBUS_DEVICE(0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49,
		       0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf) },
	/* Heartbeat guid */
	{ VMBUS_DEVICE(0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e,
		       0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d) },
	/* KVP guid */
	{ VMBUS_DEVICE(0xe7, 0xf4, 0xa0, 0xa9, 0x45, 0x5a, 0x96, 0x4d,
		       0xb8, 0x27, 0x8a, 0x84, 0x1e, 0x8c, 0x3,  0xe6) },
	{ },
};

MODULE_DEVICE_TABLE(vmbus, id_table);

/* The one and only one */
static  struct hv_driver util_drv = {
	.name = "hv_util",
	.id_table = id_table,
	.probe =  util_probe,
	.remove =  util_remove,
};

static int __init init_hyperv_utils(void)
{
	pr_info("Registering HyperV Utility Driver\n");

	if (hv_kvp_init())
		return -ENODEV;


	shut_txf_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	time_txf_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	hbeat_txf_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);

	if (!shut_txf_buf || !time_txf_buf || !hbeat_txf_buf) {
		pr_info("Unable to allocate memory for receive buffer\n");
		kfree(shut_txf_buf);
		kfree(time_txf_buf);
		kfree(hbeat_txf_buf);
		return -ENOMEM;
	}

	hv_cb_utils[HV_SHUTDOWN_MSG].callback = &shutdown_onchannelcallback;

	hv_cb_utils[HV_TIMESYNC_MSG].callback = &timesync_onchannelcallback;

	hv_cb_utils[HV_HEARTBEAT_MSG].callback = &heartbeat_onchannelcallback;

	hv_cb_utils[HV_KVP_MSG].callback = &hv_kvp_onchannelcallback;

	return vmbus_driver_register(&util_drv);
}

static void exit_hyperv_utils(void)
{
	pr_info("De-Registered HyperV Utility Driver\n");

	if (hv_cb_utils[HV_SHUTDOWN_MSG].channel != NULL)
		hv_cb_utils[HV_SHUTDOWN_MSG].channel->onchannel_callback =
			&chn_cb_negotiate;
	hv_cb_utils[HV_SHUTDOWN_MSG].callback = NULL;

	if (hv_cb_utils[HV_TIMESYNC_MSG].channel != NULL)
		hv_cb_utils[HV_TIMESYNC_MSG].channel->onchannel_callback =
			&chn_cb_negotiate;
	hv_cb_utils[HV_TIMESYNC_MSG].callback = NULL;

	if (hv_cb_utils[HV_HEARTBEAT_MSG].channel != NULL)
		hv_cb_utils[HV_HEARTBEAT_MSG].channel->onchannel_callback =
			&chn_cb_negotiate;
	hv_cb_utils[HV_HEARTBEAT_MSG].callback = NULL;

	if (hv_cb_utils[HV_KVP_MSG].channel != NULL)
		hv_cb_utils[HV_KVP_MSG].channel->onchannel_callback =
			&chn_cb_negotiate;
	hv_cb_utils[HV_KVP_MSG].callback = NULL;

	hv_kvp_deinit();

	kfree(shut_txf_buf);
	kfree(time_txf_buf);
	kfree(hbeat_txf_buf);
	vmbus_driver_unregister(&util_drv);
}

module_init(init_hyperv_utils);
module_exit(exit_hyperv_utils);

MODULE_DESCRIPTION("Hyper-V Utilities");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_LICENSE("GPL");
