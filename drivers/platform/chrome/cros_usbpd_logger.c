// SPDX-License-Identifier: GPL-2.0
/*
 * Logging driver for ChromeOS EC based USBPD Charger.
 *
 * Copyright 2018 Google LLC.
 */

#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define DRV_NAME "cros-usbpd-logger"

#define CROS_USBPD_MAX_LOG_ENTRIES	30
#define CROS_USBPD_LOG_UPDATE_DELAY	msecs_to_jiffies(60000)
#define CROS_USBPD_DATA_SIZE		16
#define CROS_USBPD_LOG_RESP_SIZE	(sizeof(struct ec_response_pd_log) + \
					 CROS_USBPD_DATA_SIZE)
#define CROS_USBPD_BUFFER_SIZE		(sizeof(struct cros_ec_command) + \
					 CROS_USBPD_LOG_RESP_SIZE)
/* Buffer for building the PDLOG string */
#define BUF_SIZE	80

struct logger_data {
	struct device *dev;
	struct cros_ec_dev *ec_dev;
	u8 ec_buffer[CROS_USBPD_BUFFER_SIZE];
	struct delayed_work log_work;
	struct workqueue_struct *log_workqueue;
};

static const char * const chg_type_names[] = {
	"None", "PD", "Type-C", "Proprietary", "DCP", "CDP", "SDP",
	"Other", "VBUS"
};

static const char * const role_names[] = {
	"Disconnected", "SRC", "SNK", "SNK (not charging)"
};

static const char * const fault_names[] = {
	"---", "OCP", "fast OCP", "OVP", "Discharge"
};

__printf(3, 4)
static int append_str(char *buf, int pos, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf + pos, BUF_SIZE - pos, fmt, args);
	va_end(args);

	return i;
}

static struct ec_response_pd_log *ec_get_log_entry(struct logger_data *logger)
{
	struct cros_ec_dev *ec_dev = logger->ec_dev;
	struct cros_ec_command *msg;
	int ret;

	msg = (struct cros_ec_command *)logger->ec_buffer;

	msg->command = ec_dev->cmd_offset + EC_CMD_PD_GET_LOG_ENTRY;
	msg->insize = CROS_USBPD_LOG_RESP_SIZE;

	ret = cros_ec_cmd_xfer_status(ec_dev->ec_dev, msg);
	if (ret < 0)
		return ERR_PTR(ret);

	return (struct ec_response_pd_log *)msg->data;
}

static void cros_usbpd_print_log_entry(struct ec_response_pd_log *r,
				       ktime_t tstamp)
{
	const char *fault, *role, *chg_type;
	struct usb_chg_measures *meas;
	struct mcdp_info *minfo;
	int role_idx, type_idx;
	char buf[BUF_SIZE + 1];
	struct rtc_time rt;
	int len = 0;
	s32 rem;
	int i;

	/* The timestamp is the number of 1024th of seconds in the past */
	tstamp = ktime_sub_us(tstamp, r->timestamp << PD_LOG_TIMESTAMP_SHIFT);
	rt = rtc_ktime_to_tm(tstamp);

	switch (r->type) {
	case PD_EVENT_MCU_CHARGE:
		if (r->data & CHARGE_FLAGS_OVERRIDE)
			len += append_str(buf, len, "override ");

		if (r->data & CHARGE_FLAGS_DELAYED_OVERRIDE)
			len += append_str(buf, len, "pending_override ");

		role_idx = r->data & CHARGE_FLAGS_ROLE_MASK;
		role = role_idx < ARRAY_SIZE(role_names) ?
			role_names[role_idx] : "Unknown";

		type_idx = (r->data & CHARGE_FLAGS_TYPE_MASK)
			 >> CHARGE_FLAGS_TYPE_SHIFT;

		chg_type = type_idx < ARRAY_SIZE(chg_type_names) ?
			chg_type_names[type_idx] : "???";

		if (role_idx == USB_PD_PORT_POWER_DISCONNECTED ||
		    role_idx == USB_PD_PORT_POWER_SOURCE) {
			len += append_str(buf, len, "%s", role);
			break;
		}

		meas = (struct usb_chg_measures *)r->payload;
		len += append_str(buf, len, "%s %s %s %dmV max %dmV / %dmA",
				  role,	r->data & CHARGE_FLAGS_DUAL_ROLE ?
				  "DRP" : "Charger",
				  chg_type, meas->voltage_now,
				  meas->voltage_max, meas->current_max);
		break;
	case PD_EVENT_ACC_RW_FAIL:
		len += append_str(buf, len, "RW signature check failed");
		break;
	case PD_EVENT_PS_FAULT:
		fault = r->data < ARRAY_SIZE(fault_names) ? fault_names[r->data]
							  : "???";
		len += append_str(buf, len, "Power supply fault: %s", fault);
		break;
	case PD_EVENT_VIDEO_DP_MODE:
		len += append_str(buf, len, "DP mode %sabled", r->data == 1 ?
				  "en" : "dis");
		break;
	case PD_EVENT_VIDEO_CODEC:
		minfo = (struct mcdp_info *)r->payload;
		len += append_str(buf, len, "HDMI info: family:%04x chipid:%04x ",
				  MCDP_FAMILY(minfo->family),
				  MCDP_CHIPID(minfo->chipid));
		len += append_str(buf, len, "irom:%d.%d.%d fw:%d.%d.%d",
				  minfo->irom.major, minfo->irom.minor,
				  minfo->irom.build, minfo->fw.major,
				  minfo->fw.minor, minfo->fw.build);
		break;
	default:
		len += append_str(buf, len, "Event %02x (%04x) [", r->type,
				  r->data);

		for (i = 0; i < PD_LOG_SIZE(r->size_port); i++)
			len += append_str(buf, len, "%02x ", r->payload[i]);

		len += append_str(buf, len, "]");
		break;
	}

	div_s64_rem(ktime_to_ms(tstamp), MSEC_PER_SEC, &rem);
	pr_info("PDLOG %d/%02d/%02d %02d:%02d:%02d.%03d P%d %s\n",
		rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday,
		rt.tm_hour, rt.tm_min, rt.tm_sec, rem,
		PD_LOG_PORT(r->size_port), buf);
}

static void cros_usbpd_log_check(struct work_struct *work)
{
	struct logger_data *logger = container_of(to_delayed_work(work),
						  struct logger_data,
						  log_work);
	struct device *dev = logger->dev;
	struct ec_response_pd_log *r;
	int entries = 0;
	ktime_t now;

	while (entries++ < CROS_USBPD_MAX_LOG_ENTRIES) {
		r = ec_get_log_entry(logger);
		now = ktime_get_real();
		if (IS_ERR(r)) {
			dev_dbg(dev, "Cannot get PD log %ld\n", PTR_ERR(r));
			break;
		}
		if (r->type == PD_EVENT_NO_ENTRY)
			break;

		cros_usbpd_print_log_entry(r, now);
	}

	queue_delayed_work(logger->log_workqueue, &logger->log_work,
			   CROS_USBPD_LOG_UPDATE_DELAY);
}

static int cros_usbpd_logger_probe(struct platform_device *pd)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pd->dev.parent);
	struct device *dev = &pd->dev;
	struct logger_data *logger;

	logger = devm_kzalloc(dev, sizeof(*logger), GFP_KERNEL);
	if (!logger)
		return -ENOMEM;

	logger->dev = dev;
	logger->ec_dev = ec_dev;

	platform_set_drvdata(pd, logger);

	/* Retrieve PD event logs periodically */
	INIT_DELAYED_WORK(&logger->log_work, cros_usbpd_log_check);
	logger->log_workqueue =	create_singlethread_workqueue("cros_usbpd_log");
	if (!logger->log_workqueue)
		return -ENOMEM;

	queue_delayed_work(logger->log_workqueue, &logger->log_work,
			   CROS_USBPD_LOG_UPDATE_DELAY);

	return 0;
}

static void cros_usbpd_logger_remove(struct platform_device *pd)
{
	struct logger_data *logger = platform_get_drvdata(pd);

	cancel_delayed_work_sync(&logger->log_work);
	destroy_workqueue(logger->log_workqueue);
}

static int __maybe_unused cros_usbpd_logger_resume(struct device *dev)
{
	struct logger_data *logger = dev_get_drvdata(dev);

	queue_delayed_work(logger->log_workqueue, &logger->log_work,
			   CROS_USBPD_LOG_UPDATE_DELAY);

	return 0;
}

static int __maybe_unused cros_usbpd_logger_suspend(struct device *dev)
{
	struct logger_data *logger = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&logger->log_work);

	return 0;
}

static SIMPLE_DEV_PM_OPS(cros_usbpd_logger_pm_ops, cros_usbpd_logger_suspend,
			 cros_usbpd_logger_resume);

static struct platform_driver cros_usbpd_logger_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &cros_usbpd_logger_pm_ops,
	},
	.probe = cros_usbpd_logger_probe,
	.remove_new = cros_usbpd_logger_remove,
};

module_platform_driver(cros_usbpd_logger_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Logging driver for ChromeOS EC USBPD Charger.");
MODULE_ALIAS("platform:" DRV_NAME);
