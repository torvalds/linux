// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP.
 */

#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/scmi_protocol.h>
#include <linux/scmi_imx_protocol.h>

struct scmi_imx_bbm {
	const struct scmi_imx_bbm_proto_ops *ops;
	struct rtc_device *rtc_dev;
	struct scmi_protocol_handle *ph;
	struct notifier_block nb;
};

static int scmi_imx_bbm_read_time(struct device *dev, struct rtc_time *tm)
{
	struct scmi_imx_bbm *bbnsm = dev_get_drvdata(dev);
	struct scmi_protocol_handle *ph = bbnsm->ph;
	u64 val;
	int ret;

	ret = bbnsm->ops->rtc_time_get(ph, 0, &val);
	if (ret)
		return ret;

	rtc_time64_to_tm(val, tm);

	return 0;
}

static int scmi_imx_bbm_set_time(struct device *dev, struct rtc_time *tm)
{
	struct scmi_imx_bbm *bbnsm = dev_get_drvdata(dev);
	struct scmi_protocol_handle *ph = bbnsm->ph;
	u64 val;

	val = rtc_tm_to_time64(tm);

	return bbnsm->ops->rtc_time_set(ph, 0, val);
}

static int scmi_imx_bbm_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct scmi_imx_bbm *bbnsm = dev_get_drvdata(dev);
	struct scmi_protocol_handle *ph = bbnsm->ph;

	/* scmi_imx_bbm_set_alarm enables the irq, just handle disable here */
	if (!enable)
		return bbnsm->ops->rtc_alarm_set(ph, 0, false, 0);

	return 0;
}

static int scmi_imx_bbm_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct scmi_imx_bbm *bbnsm = dev_get_drvdata(dev);
	struct scmi_protocol_handle *ph = bbnsm->ph;
	struct rtc_time *alrm_tm = &alrm->time;
	u64 val;

	val = rtc_tm_to_time64(alrm_tm);

	return bbnsm->ops->rtc_alarm_set(ph, 0, true, val);
}

static const struct rtc_class_ops smci_imx_bbm_rtc_ops = {
	.read_time = scmi_imx_bbm_read_time,
	.set_time = scmi_imx_bbm_set_time,
	.set_alarm = scmi_imx_bbm_set_alarm,
	.alarm_irq_enable = scmi_imx_bbm_alarm_irq_enable,
};

static int scmi_imx_bbm_rtc_notifier(struct notifier_block *nb, unsigned long event, void *data)
{
	struct scmi_imx_bbm *bbnsm = container_of(nb, struct scmi_imx_bbm, nb);
	struct scmi_imx_bbm_notif_report *r = data;

	if (r->is_rtc)
		rtc_update_irq(bbnsm->rtc_dev, 1, RTC_AF | RTC_IRQF);
	else
		pr_err("Unexpected bbm event: %s\n", __func__);

	return 0;
}

static int scmi_imx_bbm_rtc_init(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;
	struct device *dev = &sdev->dev;
	struct scmi_imx_bbm *bbnsm = dev_get_drvdata(dev);
	int ret;

	bbnsm->rtc_dev = devm_rtc_allocate_device(dev);
	if (IS_ERR(bbnsm->rtc_dev))
		return PTR_ERR(bbnsm->rtc_dev);

	bbnsm->rtc_dev->ops = &smci_imx_bbm_rtc_ops;
	bbnsm->rtc_dev->range_max = U32_MAX;

	bbnsm->nb.notifier_call = &scmi_imx_bbm_rtc_notifier;
	ret = handle->notify_ops->devm_event_notifier_register(sdev, SCMI_PROTOCOL_IMX_BBM,
							       SCMI_EVENT_IMX_BBM_RTC,
							       NULL, &bbnsm->nb);
	if (ret)
		return ret;

	return devm_rtc_register_device(bbnsm->rtc_dev);
}

static int scmi_imx_bbm_rtc_probe(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;
	struct device *dev = &sdev->dev;
	struct scmi_protocol_handle *ph;
	struct scmi_imx_bbm *bbnsm;
	int ret;

	if (!handle)
		return -ENODEV;

	bbnsm = devm_kzalloc(dev, sizeof(*bbnsm), GFP_KERNEL);
	if (!bbnsm)
		return -ENOMEM;

	bbnsm->ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_IMX_BBM, &ph);
	if (IS_ERR(bbnsm->ops))
		return PTR_ERR(bbnsm->ops);

	bbnsm->ph = ph;

	device_init_wakeup(dev, true);

	dev_set_drvdata(dev, bbnsm);

	ret = scmi_imx_bbm_rtc_init(sdev);
	if (ret)
		device_init_wakeup(dev, false);

	return ret;
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_IMX_BBM, "imx-bbm-rtc" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_imx_bbm_rtc_driver = {
	.name = "scmi-imx-bbm-rtc",
	.probe = scmi_imx_bbm_rtc_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_imx_bbm_rtc_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("IMX SM BBM RTC driver");
MODULE_LICENSE("GPL");
