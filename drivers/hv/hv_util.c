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
#include <linux/hyperv.h>

#include "hyperv_vmbus.h"

#define SD_MAJOR	3
#define SD_MINOR	0
#define SD_VERSION	(SD_MAJOR << 16 | SD_MINOR)

#define SD_MAJOR_1	1
#define SD_VERSION_1	(SD_MAJOR_1 << 16 | SD_MINOR)

#define TS_MAJOR	4
#define TS_MINOR	0
#define TS_VERSION	(TS_MAJOR << 16 | TS_MINOR)

#define TS_MAJOR_1	1
#define TS_VERSION_1	(TS_MAJOR_1 << 16 | TS_MINOR)

#define TS_MAJOR_3	3
#define TS_VERSION_3	(TS_MAJOR_3 << 16 | TS_MINOR)

#define HB_MAJOR	3
#define HB_MINOR	0
#define HB_VERSION	(HB_MAJOR << 16 | HB_MINOR)

#define HB_MAJOR_1	1
#define HB_VERSION_1	(HB_MAJOR_1 << 16 | HB_MINOR)

static int sd_srv_version;
static int ts_srv_version;
static int hb_srv_version;
static int util_fw_version;

static void shutdown_onchannelcallback(void *context);
static struct hv_util_service util_shutdown = {
	.util_cb = shutdown_onchannelcallback,
};

static int hv_timesync_init(struct hv_util_service *srv);
static void hv_timesync_deinit(void);

static void timesync_onchannelcallback(void *context);
static struct hv_util_service util_timesynch = {
	.util_cb = timesync_onchannelcallback,
	.util_init = hv_timesync_init,
	.util_deinit = hv_timesync_deinit,
};

static void heartbeat_onchannelcallback(void *context);
static struct hv_util_service util_heartbeat = {
	.util_cb = heartbeat_onchannelcallback,
};

static struct hv_util_service util_kvp = {
	.util_cb = hv_kvp_onchannelcallback,
	.util_init = hv_kvp_init,
	.util_deinit = hv_kvp_deinit,
};

static struct hv_util_service util_vss = {
	.util_cb = hv_vss_onchannelcallback,
	.util_init = hv_vss_init,
	.util_deinit = hv_vss_deinit,
};

static struct hv_util_service util_fcopy = {
	.util_cb = hv_fcopy_onchannelcallback,
	.util_init = hv_fcopy_init,
	.util_deinit = hv_fcopy_deinit,
};

static void perform_shutdown(struct work_struct *dummy)
{
	orderly_poweroff(true);
}

/*
 * Perform the shutdown operation in a thread context.
 */
static DECLARE_WORK(shutdown_work, perform_shutdown);

static void shutdown_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u32 recvlen;
	u64 requestid;
	bool execute_shutdown = false;
	u8  *shut_txf_buf = util_shutdown.recv_buffer;

	struct shutdown_msg_data *shutdown_msg;

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop = NULL;

	vmbus_recvpacket(channel, shut_txf_buf,
			 PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&shut_txf_buf[
			sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			vmbus_prep_negotiate_resp(icmsghdrp, negop,
					shut_txf_buf, util_fw_version,
					sd_srv_version);
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
		schedule_work(&shutdown_work);
}

/*
 * Set the host time in a process context.
 */

struct adj_time_work {
	struct work_struct work;
	u64	host_time;
	u64	ref_time;
	u8	flags;
};

static void hv_set_host_time(struct work_struct *work)
{
	struct adj_time_work	*wrk;
	s64 host_tns;
	u64 newtime;
	struct timespec host_ts;

	wrk = container_of(work, struct adj_time_work, work);

	newtime = wrk->host_time;
	if (ts_srv_version > TS_VERSION_3) {
		/*
		 * Some latency has been introduced since Hyper-V generated
		 * its time sample. Take that latency into account before
		 * using TSC reference time sample from Hyper-V.
		 *
		 * This sample is given by TimeSync v4 and above hosts.
		 */
		u64 current_tick;

		rdmsrl(HV_X64_MSR_TIME_REF_COUNT, current_tick);
		newtime += (current_tick - wrk->ref_time);
	}
	host_tns = (newtime - WLTIMEDELTA) * 100;
	host_ts = ns_to_timespec(host_tns);

	do_settimeofday(&host_ts);
}

/*
 * Synchronize time with host after reboot, restore, etc.
 *
 * ICTIMESYNCFLAG_SYNC flag bit indicates reboot, restore events of the VM.
 * After reboot the flag ICTIMESYNCFLAG_SYNC is included in the first time
 * message after the timesync channel is opened. Since the hv_utils module is
 * loaded after hv_vmbus, the first message is usually missed. This bit is
 * considered a hard request to discipline the clock.
 *
 * ICTIMESYNCFLAG_SAMPLE bit indicates a time sample from host. This is
 * typically used as a hint to the guest. The guest is under no obligation
 * to discipline the clock.
 */
static struct adj_time_work  wrk;
static inline void adj_guesttime(u64 hosttime, u64 reftime, u8 flags)
{

	/*
	 * This check is safe since we are executing in the
	 * interrupt context and time synch messages arre always
	 * delivered on the same CPU.
	 */
	if (work_pending(&wrk.work))
		return;

	wrk.host_time = hosttime;
	wrk.ref_time = reftime;
	wrk.flags = flags;
	if ((flags & (ICTIMESYNCFLAG_SYNC | ICTIMESYNCFLAG_SAMPLE)) != 0) {
		schedule_work(&wrk.work);
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
	struct ictimesync_ref_data *refdata;
	u8 *time_txf_buf = util_timesynch.recv_buffer;
	struct icmsg_negotiate *negop = NULL;

	vmbus_recvpacket(channel, time_txf_buf,
			 PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&time_txf_buf[
				sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			vmbus_prep_negotiate_resp(icmsghdrp, negop,
						time_txf_buf,
						util_fw_version,
						ts_srv_version);
			pr_info("Using TimeSync version %d.%d\n",
				ts_srv_version >> 16, ts_srv_version & 0xFFFF);
		} else {
			if (ts_srv_version > TS_VERSION_3) {
				refdata = (struct ictimesync_ref_data *)
					&time_txf_buf[
					sizeof(struct vmbuspipe_hdr) +
					sizeof(struct icmsg_hdr)];

				adj_guesttime(refdata->parenttime,
						refdata->vmreferencetime,
						refdata->flags);
			} else {
				timedatap = (struct ictimesync_data *)
					&time_txf_buf[
					sizeof(struct vmbuspipe_hdr) +
					sizeof(struct icmsg_hdr)];
				adj_guesttime(timedatap->parenttime,
						0,
						timedatap->flags);
			}
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
	u8 *hbeat_txf_buf = util_heartbeat.recv_buffer;
	struct icmsg_negotiate *negop = NULL;

	while (1) {

		vmbus_recvpacket(channel, hbeat_txf_buf,
				 PAGE_SIZE, &recvlen, &requestid);

		if (!recvlen)
			break;

		icmsghdrp = (struct icmsg_hdr *)&hbeat_txf_buf[
				sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			vmbus_prep_negotiate_resp(icmsghdrp, negop,
				hbeat_txf_buf, util_fw_version,
				hb_srv_version);
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

static int util_probe(struct hv_device *dev,
			const struct hv_vmbus_device_id *dev_id)
{
	struct hv_util_service *srv =
		(struct hv_util_service *)dev_id->driver_data;
	int ret;

	srv->recv_buffer = kmalloc(PAGE_SIZE * 4, GFP_KERNEL);
	if (!srv->recv_buffer)
		return -ENOMEM;
	srv->channel = dev->channel;
	if (srv->util_init) {
		ret = srv->util_init(srv);
		if (ret) {
			ret = -ENODEV;
			goto error1;
		}
	}

	/*
	 * The set of services managed by the util driver are not performance
	 * critical and do not need batched reading. Furthermore, some services
	 * such as KVP can only handle one message from the host at a time.
	 * Turn off batched reading for all util drivers before we open the
	 * channel.
	 */

	set_channel_read_state(dev->channel, false);

	hv_set_drvdata(dev, srv);

	/*
	 * Based on the host; initialize the framework and
	 * service version numbers we will negotiate.
	 */
	switch (vmbus_proto_version) {
	case (VERSION_WS2008):
		util_fw_version = UTIL_WS2K8_FW_VERSION;
		sd_srv_version = SD_VERSION_1;
		ts_srv_version = TS_VERSION_1;
		hb_srv_version = HB_VERSION_1;
		break;
	case VERSION_WIN7:
	case VERSION_WIN8:
	case VERSION_WIN8_1:
		util_fw_version = UTIL_FW_VERSION;
		sd_srv_version = SD_VERSION;
		ts_srv_version = TS_VERSION_3;
		hb_srv_version = HB_VERSION;
		break;
	case VERSION_WIN10:
	default:
		util_fw_version = UTIL_FW_VERSION;
		sd_srv_version = SD_VERSION;
		ts_srv_version = TS_VERSION;
		hb_srv_version = HB_VERSION;
	}

	ret = vmbus_open(dev->channel, 4 * PAGE_SIZE, 4 * PAGE_SIZE, NULL, 0,
			srv->util_cb, dev->channel);
	if (ret)
		goto error;

	return 0;

error:
	if (srv->util_deinit)
		srv->util_deinit();
error1:
	kfree(srv->recv_buffer);
	return ret;
}

static int util_remove(struct hv_device *dev)
{
	struct hv_util_service *srv = hv_get_drvdata(dev);

	if (srv->util_deinit)
		srv->util_deinit();
	vmbus_close(dev->channel);
	kfree(srv->recv_buffer);

	return 0;
}

static const struct hv_vmbus_device_id id_table[] = {
	/* Shutdown guid */
	{ HV_SHUTDOWN_GUID,
	  .driver_data = (unsigned long)&util_shutdown
	},
	/* Time synch guid */
	{ HV_TS_GUID,
	  .driver_data = (unsigned long)&util_timesynch
	},
	/* Heartbeat guid */
	{ HV_HEART_BEAT_GUID,
	  .driver_data = (unsigned long)&util_heartbeat
	},
	/* KVP guid */
	{ HV_KVP_GUID,
	  .driver_data = (unsigned long)&util_kvp
	},
	/* VSS GUID */
	{ HV_VSS_GUID,
	  .driver_data = (unsigned long)&util_vss
	},
	/* File copy GUID */
	{ HV_FCOPY_GUID,
	  .driver_data = (unsigned long)&util_fcopy
	},
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

static int hv_timesync_init(struct hv_util_service *srv)
{
	INIT_WORK(&wrk.work, hv_set_host_time);
	return 0;
}

static void hv_timesync_deinit(void)
{
	cancel_work_sync(&wrk.work);
}

static int __init init_hyperv_utils(void)
{
	pr_info("Registering HyperV Utility Driver\n");

	return vmbus_driver_register(&util_drv);
}

static void exit_hyperv_utils(void)
{
	pr_info("De-Registered HyperV Utility Driver\n");

	vmbus_driver_unregister(&util_drv);
}

module_init(init_hyperv_utils);
module_exit(exit_hyperv_utils);

MODULE_DESCRIPTION("Hyper-V Utilities");
MODULE_LICENSE("GPL");
