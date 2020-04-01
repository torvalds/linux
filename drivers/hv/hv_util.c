// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010, Microsoft Corporation.
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
#include <linux/clockchips.h>
#include <linux/ptp_clock_kernel.h>
#include <clocksource/hyperv_timer.h>
#include <asm/mshyperv.h>

#include "hyperv_vmbus.h"

#define SD_MAJOR	3
#define SD_MINOR	0
#define SD_MINOR_1	1
#define SD_MINOR_2	2
#define SD_VERSION_3_1	(SD_MAJOR << 16 | SD_MINOR_1)
#define SD_VERSION_3_2	(SD_MAJOR << 16 | SD_MINOR_2)
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

#define SD_VER_COUNT 4
static const int sd_versions[] = {
	SD_VERSION_3_2,
	SD_VERSION_3_1,
	SD_VERSION,
	SD_VERSION_1
};

#define TS_VER_COUNT 3
static const int ts_versions[] = {
	TS_VERSION,
	TS_VERSION_3,
	TS_VERSION_1
};

#define HB_VER_COUNT 2
static const int hb_versions[] = {
	HB_VERSION,
	HB_VERSION_1
};

#define FW_VER_COUNT 2
static const int fw_versions[] = {
	UTIL_FW_VERSION,
	UTIL_WS2K8_FW_VERSION
};

/*
 * Send the "hibernate" udev event in a thread context.
 */
struct hibernate_work_context {
	struct work_struct work;
	struct hv_device *dev;
};

static struct hibernate_work_context hibernate_context;
static bool hibernation_supported;

static void send_hibernate_uevent(struct work_struct *work)
{
	char *uevent_env[2] = { "EVENT=hibernate", NULL };
	struct hibernate_work_context *ctx;

	ctx = container_of(work, struct hibernate_work_context, work);

	kobject_uevent_env(&ctx->dev->device.kobj, KOBJ_CHANGE, uevent_env);

	pr_info("Sent hibernation uevent\n");
}

static int hv_shutdown_init(struct hv_util_service *srv)
{
	struct vmbus_channel *channel = srv->channel;

	INIT_WORK(&hibernate_context.work, send_hibernate_uevent);
	hibernate_context.dev = channel->device_obj;

	hibernation_supported = hv_is_hibernation_supported();

	return 0;
}

static void shutdown_onchannelcallback(void *context);
static struct hv_util_service util_shutdown = {
	.util_cb = shutdown_onchannelcallback,
	.util_init = hv_shutdown_init,
};

static int hv_timesync_init(struct hv_util_service *srv);
static int hv_timesync_pre_suspend(void);
static void hv_timesync_deinit(void);

static void timesync_onchannelcallback(void *context);
static struct hv_util_service util_timesynch = {
	.util_cb = timesync_onchannelcallback,
	.util_init = hv_timesync_init,
	.util_pre_suspend = hv_timesync_pre_suspend,
	.util_deinit = hv_timesync_deinit,
};

static void heartbeat_onchannelcallback(void *context);
static struct hv_util_service util_heartbeat = {
	.util_cb = heartbeat_onchannelcallback,
};

static struct hv_util_service util_kvp = {
	.util_cb = hv_kvp_onchannelcallback,
	.util_init = hv_kvp_init,
	.util_pre_suspend = hv_kvp_pre_suspend,
	.util_pre_resume = hv_kvp_pre_resume,
	.util_deinit = hv_kvp_deinit,
};

static struct hv_util_service util_vss = {
	.util_cb = hv_vss_onchannelcallback,
	.util_init = hv_vss_init,
	.util_pre_suspend = hv_vss_pre_suspend,
	.util_pre_resume = hv_vss_pre_resume,
	.util_deinit = hv_vss_deinit,
};

static struct hv_util_service util_fcopy = {
	.util_cb = hv_fcopy_onchannelcallback,
	.util_init = hv_fcopy_init,
	.util_pre_suspend = hv_fcopy_pre_suspend,
	.util_pre_resume = hv_fcopy_pre_resume,
	.util_deinit = hv_fcopy_deinit,
};

static void perform_shutdown(struct work_struct *dummy)
{
	orderly_poweroff(true);
}

static void perform_restart(struct work_struct *dummy)
{
	orderly_reboot();
}

/*
 * Perform the shutdown operation in a thread context.
 */
static DECLARE_WORK(shutdown_work, perform_shutdown);

/*
 * Perform the restart operation in a thread context.
 */
static DECLARE_WORK(restart_work, perform_restart);

static void shutdown_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	struct work_struct *work = NULL;
	u32 recvlen;
	u64 requestid;
	u8  *shut_txf_buf = util_shutdown.recv_buffer;

	struct shutdown_msg_data *shutdown_msg;

	struct icmsg_hdr *icmsghdrp;

	vmbus_recvpacket(channel, shut_txf_buf,
			 HV_HYP_PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&shut_txf_buf[
			sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			if (vmbus_prep_negotiate_resp(icmsghdrp, shut_txf_buf,
					fw_versions, FW_VER_COUNT,
					sd_versions, SD_VER_COUNT,
					NULL, &sd_srv_version)) {
				pr_info("Shutdown IC version %d.%d\n",
					sd_srv_version >> 16,
					sd_srv_version & 0xFFFF);
			}
		} else {
			shutdown_msg =
				(struct shutdown_msg_data *)&shut_txf_buf[
					sizeof(struct vmbuspipe_hdr) +
					sizeof(struct icmsg_hdr)];

			/*
			 * shutdown_msg->flags can be 0(shut down), 2(reboot),
			 * or 4(hibernate). It may bitwise-OR 1, which means
			 * performing the request by force. Linux always tries
			 * to perform the request by force.
			 */
			switch (shutdown_msg->flags) {
			case 0:
			case 1:
				icmsghdrp->status = HV_S_OK;
				work = &shutdown_work;
				pr_info("Shutdown request received -"
					    " graceful shutdown initiated\n");
				break;
			case 2:
			case 3:
				icmsghdrp->status = HV_S_OK;
				work = &restart_work;
				pr_info("Restart request received -"
					    " graceful restart initiated\n");
				break;
			case 4:
			case 5:
				pr_info("Hibernation request received\n");
				icmsghdrp->status = hibernation_supported ?
					HV_S_OK : HV_E_FAIL;
				if (hibernation_supported)
					work = &hibernate_context.work;
				break;
			default:
				icmsghdrp->status = HV_E_FAIL;
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

	if (work)
		schedule_work(work);
}

/*
 * Set the host time in a process context.
 */
static struct work_struct adj_time_work;

/*
 * The last time sample, received from the host. PTP device responds to
 * requests by using this data and the current partition-wide time reference
 * count.
 */
static struct {
	u64				host_time;
	u64				ref_time;
	spinlock_t			lock;
} host_ts;

static struct timespec64 hv_get_adj_host_time(void)
{
	struct timespec64 ts;
	u64 newtime, reftime;
	unsigned long flags;

	spin_lock_irqsave(&host_ts.lock, flags);
	reftime = hv_read_reference_counter();
	newtime = host_ts.host_time + (reftime - host_ts.ref_time);
	ts = ns_to_timespec64((newtime - WLTIMEDELTA) * 100);
	spin_unlock_irqrestore(&host_ts.lock, flags);

	return ts;
}

static void hv_set_host_time(struct work_struct *work)
{
	struct timespec64 ts = hv_get_adj_host_time();

	do_settimeofday64(&ts);
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
static inline void adj_guesttime(u64 hosttime, u64 reftime, u8 adj_flags)
{
	unsigned long flags;
	u64 cur_reftime;

	/*
	 * Save the adjusted time sample from the host and the snapshot
	 * of the current system time.
	 */
	spin_lock_irqsave(&host_ts.lock, flags);

	cur_reftime = hv_read_reference_counter();
	host_ts.host_time = hosttime;
	host_ts.ref_time = cur_reftime;

	/*
	 * TimeSync v4 messages contain reference time (guest's Hyper-V
	 * clocksource read when the time sample was generated), we can
	 * improve the precision by adding the delta between now and the
	 * time of generation. For older protocols we set
	 * reftime == cur_reftime on call.
	 */
	host_ts.host_time += (cur_reftime - reftime);

	spin_unlock_irqrestore(&host_ts.lock, flags);

	/* Schedule work to do do_settimeofday64() */
	if (adj_flags & ICTIMESYNCFLAG_SYNC)
		schedule_work(&adj_time_work);
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

	vmbus_recvpacket(channel, time_txf_buf,
			 HV_HYP_PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&time_txf_buf[
				sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			if (vmbus_prep_negotiate_resp(icmsghdrp, time_txf_buf,
						fw_versions, FW_VER_COUNT,
						ts_versions, TS_VER_COUNT,
						NULL, &ts_srv_version)) {
				pr_info("TimeSync IC version %d.%d\n",
					ts_srv_version >> 16,
					ts_srv_version & 0xFFFF);
			}
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
					      hv_read_reference_counter(),
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

	while (1) {

		vmbus_recvpacket(channel, hbeat_txf_buf,
				 HV_HYP_PAGE_SIZE, &recvlen, &requestid);

		if (!recvlen)
			break;

		icmsghdrp = (struct icmsg_hdr *)&hbeat_txf_buf[
				sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			if (vmbus_prep_negotiate_resp(icmsghdrp,
					hbeat_txf_buf,
					fw_versions, FW_VER_COUNT,
					hb_versions, HB_VER_COUNT,
					NULL, &hb_srv_version)) {

				pr_info("Heartbeat IC version %d.%d\n",
					hb_srv_version >> 16,
					hb_srv_version & 0xFFFF);
			}
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

	srv->recv_buffer = kmalloc(HV_HYP_PAGE_SIZE * 4, GFP_KERNEL);
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
	set_channel_read_mode(dev->channel, HV_CALL_DIRECT);

	hv_set_drvdata(dev, srv);

	ret = vmbus_open(dev->channel, 4 * HV_HYP_PAGE_SIZE,
			 4 * HV_HYP_PAGE_SIZE, NULL, 0, srv->util_cb,
			 dev->channel);
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

/*
 * When we're in util_suspend(), all the userspace processes have been frozen
 * (refer to hibernate() -> freeze_processes()). The userspace is thawed only
 * after the whole resume procedure, including util_resume(), finishes.
 */
static int util_suspend(struct hv_device *dev)
{
	struct hv_util_service *srv = hv_get_drvdata(dev);
	int ret = 0;

	if (srv->util_pre_suspend) {
		ret = srv->util_pre_suspend();
		if (ret)
			return ret;
	}

	vmbus_close(dev->channel);

	return 0;
}

static int util_resume(struct hv_device *dev)
{
	struct hv_util_service *srv = hv_get_drvdata(dev);
	int ret = 0;

	if (srv->util_pre_resume) {
		ret = srv->util_pre_resume();
		if (ret)
			return ret;
	}

	ret = vmbus_open(dev->channel, 4 * HV_HYP_PAGE_SIZE,
			 4 * HV_HYP_PAGE_SIZE, NULL, 0, srv->util_cb,
			 dev->channel);
	return ret;
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
	.name = "hv_utils",
	.id_table = id_table,
	.probe =  util_probe,
	.remove =  util_remove,
	.suspend = util_suspend,
	.resume =  util_resume,
	.driver = {
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int hv_ptp_enable(struct ptp_clock_info *info,
			 struct ptp_clock_request *request, int on)
{
	return -EOPNOTSUPP;
}

static int hv_ptp_settime(struct ptp_clock_info *p, const struct timespec64 *ts)
{
	return -EOPNOTSUPP;
}

static int hv_ptp_adjfreq(struct ptp_clock_info *ptp, s32 delta)
{
	return -EOPNOTSUPP;
}
static int hv_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	return -EOPNOTSUPP;
}

static int hv_ptp_gettime(struct ptp_clock_info *info, struct timespec64 *ts)
{
	*ts = hv_get_adj_host_time();

	return 0;
}

static struct ptp_clock_info ptp_hyperv_info = {
	.name		= "hyperv",
	.enable         = hv_ptp_enable,
	.adjtime        = hv_ptp_adjtime,
	.adjfreq        = hv_ptp_adjfreq,
	.gettime64      = hv_ptp_gettime,
	.settime64      = hv_ptp_settime,
	.owner		= THIS_MODULE,
};

static struct ptp_clock *hv_ptp_clock;

static int hv_timesync_init(struct hv_util_service *srv)
{
	/* TimeSync requires Hyper-V clocksource. */
	if (!hv_read_reference_counter)
		return -ENODEV;

	spin_lock_init(&host_ts.lock);

	INIT_WORK(&adj_time_work, hv_set_host_time);

	/*
	 * ptp_clock_register() returns NULL when CONFIG_PTP_1588_CLOCK is
	 * disabled but the driver is still useful without the PTP device
	 * as it still handles the ICTIMESYNCFLAG_SYNC case.
	 */
	hv_ptp_clock = ptp_clock_register(&ptp_hyperv_info, NULL);
	if (IS_ERR_OR_NULL(hv_ptp_clock)) {
		pr_err("cannot register PTP clock: %ld\n",
		       PTR_ERR(hv_ptp_clock));
		hv_ptp_clock = NULL;
	}

	return 0;
}

static void hv_timesync_cancel_work(void)
{
	cancel_work_sync(&adj_time_work);
}

static int hv_timesync_pre_suspend(void)
{
	hv_timesync_cancel_work();
	return 0;
}

static void hv_timesync_deinit(void)
{
	if (hv_ptp_clock)
		ptp_clock_unregister(hv_ptp_clock);

	hv_timesync_cancel_work();
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
