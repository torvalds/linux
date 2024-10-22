// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) NXP BBM Protocol
 *
 * Copyright 2024 NXP
 */

#define pr_fmt(fmt) "SCMI Notifications BBM - " fmt

#include <linux/bits.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scmi_protocol.h>
#include <linux/scmi_imx_protocol.h>

#include "../../protocols.h"
#include "../../notify.h"

#define SCMI_PROTOCOL_SUPPORTED_VERSION		0x10000

enum scmi_imx_bbm_protocol_cmd {
	IMX_BBM_GPR_SET = 0x3,
	IMX_BBM_GPR_GET = 0x4,
	IMX_BBM_RTC_ATTRIBUTES = 0x5,
	IMX_BBM_RTC_TIME_SET = 0x6,
	IMX_BBM_RTC_TIME_GET = 0x7,
	IMX_BBM_RTC_ALARM_SET = 0x8,
	IMX_BBM_BUTTON_GET = 0x9,
	IMX_BBM_RTC_NOTIFY = 0xA,
	IMX_BBM_BUTTON_NOTIFY = 0xB,
};

#define GET_RTCS_NR(x)	le32_get_bits((x), GENMASK(23, 16))
#define GET_GPRS_NR(x)	le32_get_bits((x), GENMASK(15, 0))

#define SCMI_IMX_BBM_NOTIFY_RTC_UPDATED		BIT(2)
#define SCMI_IMX_BBM_NOTIFY_RTC_ROLLOVER	BIT(1)
#define SCMI_IMX_BBM_NOTIFY_RTC_ALARM		BIT(0)

#define SCMI_IMX_BBM_RTC_ALARM_ENABLE_FLAG	BIT(0)

#define SCMI_IMX_BBM_NOTIFY_RTC_FLAG	\
	(SCMI_IMX_BBM_NOTIFY_RTC_UPDATED | SCMI_IMX_BBM_NOTIFY_RTC_ROLLOVER | \
	 SCMI_IMX_BBM_NOTIFY_RTC_ALARM)

#define SCMI_IMX_BBM_EVENT_RTC_MASK		GENMASK(31, 24)

struct scmi_imx_bbm_info {
	u32 version;
	int nr_rtc;
	int nr_gpr;
};

struct scmi_msg_imx_bbm_protocol_attributes {
	__le32 attributes;
};

struct scmi_imx_bbm_set_time {
	__le32 id;
	__le32 flags;
	__le32 value_low;
	__le32 value_high;
};

struct scmi_imx_bbm_get_time {
	__le32 id;
	__le32 flags;
};

struct scmi_imx_bbm_alarm_time {
	__le32 id;
	__le32 flags;
	__le32 value_low;
	__le32 value_high;
};

struct scmi_msg_imx_bbm_rtc_notify {
	__le32 rtc_id;
	__le32 flags;
};

struct scmi_msg_imx_bbm_button_notify {
	__le32 flags;
};

struct scmi_imx_bbm_notify_payld {
	__le32 flags;
};

static int scmi_imx_bbm_attributes_get(const struct scmi_protocol_handle *ph,
				       struct scmi_imx_bbm_info *pi)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_imx_bbm_protocol_attributes *attr;

	ret = ph->xops->xfer_get_init(ph, PROTOCOL_ATTRIBUTES, 0, sizeof(*attr), &t);
	if (ret)
		return ret;

	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		pi->nr_rtc = GET_RTCS_NR(attr->attributes);
		pi->nr_gpr = GET_GPRS_NR(attr->attributes);
	}

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_imx_bbm_notify(const struct scmi_protocol_handle *ph,
			       u32 src_id, int message_id, bool enable)
{
	int ret;
	struct scmi_xfer *t;

	if (message_id == IMX_BBM_RTC_NOTIFY) {
		struct scmi_msg_imx_bbm_rtc_notify *rtc_notify;

		ret = ph->xops->xfer_get_init(ph, message_id,
					      sizeof(*rtc_notify), 0, &t);
		if (ret)
			return ret;

		rtc_notify = t->tx.buf;
		rtc_notify->rtc_id = cpu_to_le32(0);
		rtc_notify->flags =
			cpu_to_le32(enable ? SCMI_IMX_BBM_NOTIFY_RTC_FLAG : 0);
	} else if (message_id == IMX_BBM_BUTTON_NOTIFY) {
		struct scmi_msg_imx_bbm_button_notify *button_notify;

		ret = ph->xops->xfer_get_init(ph, message_id,
					      sizeof(*button_notify), 0, &t);
		if (ret)
			return ret;

		button_notify = t->tx.buf;
		button_notify->flags = cpu_to_le32(enable ? 1 : 0);
	} else {
		return -EINVAL;
	}

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static enum scmi_imx_bbm_protocol_cmd evt_2_cmd[] = {
	IMX_BBM_RTC_NOTIFY,
	IMX_BBM_BUTTON_NOTIFY
};

static int scmi_imx_bbm_set_notify_enabled(const struct scmi_protocol_handle *ph,
					   u8 evt_id, u32 src_id, bool enable)
{
	int ret, cmd_id;

	if (evt_id >= ARRAY_SIZE(evt_2_cmd))
		return -EINVAL;

	cmd_id = evt_2_cmd[evt_id];
	ret = scmi_imx_bbm_notify(ph, src_id, cmd_id, enable);
	if (ret)
		pr_debug("FAIL_ENABLED - evt[%X] dom[%d] - ret:%d\n",
			 evt_id, src_id, ret);

	return ret;
}

static void *scmi_imx_bbm_fill_custom_report(const struct scmi_protocol_handle *ph,
					     u8 evt_id, ktime_t timestamp,
					     const void *payld, size_t payld_sz,
					     void *report, u32 *src_id)
{
	const struct scmi_imx_bbm_notify_payld *p = payld;
	struct scmi_imx_bbm_notif_report *r = report;

	if (sizeof(*p) != payld_sz)
		return NULL;

	if (evt_id == SCMI_EVENT_IMX_BBM_RTC) {
		r->is_rtc = true;
		r->is_button = false;
		r->timestamp = timestamp;
		r->rtc_id = le32_get_bits(p->flags, SCMI_IMX_BBM_EVENT_RTC_MASK);
		r->rtc_evt = le32_get_bits(p->flags, SCMI_IMX_BBM_NOTIFY_RTC_FLAG);
		dev_dbg(ph->dev, "RTC: %d evt: %x\n", r->rtc_id, r->rtc_evt);
		*src_id = r->rtc_evt;
	} else if (evt_id == SCMI_EVENT_IMX_BBM_BUTTON) {
		r->is_rtc = false;
		r->is_button = true;
		r->timestamp = timestamp;
		dev_dbg(ph->dev, "BBM Button\n");
		*src_id = 0;
	} else {
		WARN_ON_ONCE(1);
		return NULL;
	}

	return r;
}

static const struct scmi_event scmi_imx_bbm_events[] = {
	{
		.id = SCMI_EVENT_IMX_BBM_RTC,
		.max_payld_sz = sizeof(struct scmi_imx_bbm_notify_payld),
		.max_report_sz = sizeof(struct scmi_imx_bbm_notif_report),
	},
	{
		.id = SCMI_EVENT_IMX_BBM_BUTTON,
		.max_payld_sz = sizeof(struct scmi_imx_bbm_notify_payld),
		.max_report_sz = sizeof(struct scmi_imx_bbm_notif_report),
	},
};

static const struct scmi_event_ops scmi_imx_bbm_event_ops = {
	.set_notify_enabled = scmi_imx_bbm_set_notify_enabled,
	.fill_custom_report = scmi_imx_bbm_fill_custom_report,
};

static const struct scmi_protocol_events scmi_imx_bbm_protocol_events = {
	.queue_sz = SCMI_PROTO_QUEUE_SZ,
	.ops = &scmi_imx_bbm_event_ops,
	.evts = scmi_imx_bbm_events,
	.num_events = ARRAY_SIZE(scmi_imx_bbm_events),
	.num_sources = 1,
};

static int scmi_imx_bbm_rtc_time_set(const struct scmi_protocol_handle *ph,
				     u32 rtc_id, u64 sec)
{
	struct scmi_imx_bbm_info *pi = ph->get_priv(ph);
	struct scmi_imx_bbm_set_time *cfg;
	struct scmi_xfer *t;
	int ret;

	if (rtc_id >= pi->nr_rtc)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, IMX_BBM_RTC_TIME_SET, sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(rtc_id);
	cfg->flags = 0;
	cfg->value_low = cpu_to_le32(lower_32_bits(sec));
	cfg->value_high = cpu_to_le32(upper_32_bits(sec));

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_imx_bbm_rtc_time_get(const struct scmi_protocol_handle *ph,
				     u32 rtc_id, u64 *value)
{
	struct scmi_imx_bbm_info *pi = ph->get_priv(ph);
	struct scmi_imx_bbm_get_time *cfg;
	struct scmi_xfer *t;
	int ret;

	if (rtc_id >= pi->nr_rtc)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, IMX_BBM_RTC_TIME_GET, sizeof(*cfg),
				      sizeof(u64), &t);
	if (ret)
		return ret;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(rtc_id);
	cfg->flags = 0;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*value = get_unaligned_le64(t->rx.buf);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_imx_bbm_rtc_alarm_set(const struct scmi_protocol_handle *ph,
				      u32 rtc_id, bool enable, u64 sec)
{
	struct scmi_imx_bbm_info *pi = ph->get_priv(ph);
	struct scmi_imx_bbm_alarm_time *cfg;
	struct scmi_xfer *t;
	int ret;

	if (rtc_id >= pi->nr_rtc)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, IMX_BBM_RTC_ALARM_SET, sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(rtc_id);
	cfg->flags = enable ?
		     cpu_to_le32(SCMI_IMX_BBM_RTC_ALARM_ENABLE_FLAG) : 0;
	cfg->value_low = cpu_to_le32(lower_32_bits(sec));
	cfg->value_high = cpu_to_le32(upper_32_bits(sec));

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_imx_bbm_button_get(const struct scmi_protocol_handle *ph, u32 *state)
{
	struct scmi_xfer *t;
	int ret;

	ret = ph->xops->xfer_get_init(ph, IMX_BBM_BUTTON_GET, 0, sizeof(u32), &t);
	if (ret)
		return ret;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*state = get_unaligned_le32(t->rx.buf);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static const struct scmi_imx_bbm_proto_ops scmi_imx_bbm_proto_ops = {
	.rtc_time_get = scmi_imx_bbm_rtc_time_get,
	.rtc_time_set = scmi_imx_bbm_rtc_time_set,
	.rtc_alarm_set = scmi_imx_bbm_rtc_alarm_set,
	.button_get = scmi_imx_bbm_button_get,
};

static int scmi_imx_bbm_protocol_init(const struct scmi_protocol_handle *ph)
{
	u32 version;
	int ret;
	struct scmi_imx_bbm_info *binfo;

	ret = ph->xops->version_get(ph, &version);
	if (ret)
		return ret;

	dev_info(ph->dev, "NXP SM BBM Version %d.%d\n",
		 PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	binfo = devm_kzalloc(ph->dev, sizeof(*binfo), GFP_KERNEL);
	if (!binfo)
		return -ENOMEM;

	ret = scmi_imx_bbm_attributes_get(ph, binfo);
	if (ret)
		return ret;

	return ph->set_priv(ph, binfo, version);
}

static const struct scmi_protocol scmi_imx_bbm = {
	.id = SCMI_PROTOCOL_IMX_BBM,
	.owner = THIS_MODULE,
	.instance_init = &scmi_imx_bbm_protocol_init,
	.ops = &scmi_imx_bbm_proto_ops,
	.events = &scmi_imx_bbm_protocol_events,
	.supported_version = SCMI_PROTOCOL_SUPPORTED_VERSION,
	.vendor_id = "NXP",
	.sub_vendor_id = "IMX",
};
module_scmi_protocol(scmi_imx_bbm);

MODULE_DESCRIPTION("i.MX SCMI BBM driver");
MODULE_LICENSE("GPL");
