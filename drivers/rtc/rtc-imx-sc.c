// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP.
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/arm-smccc.h>
#include <linux/firmware/imx/sci.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define IMX_SC_TIMER_FUNC_GET_RTC_SEC1970	9
#define IMX_SC_TIMER_FUNC_SET_RTC_ALARM		8
#define IMX_SC_TIMER_FUNC_SET_RTC_TIME		6

#define IMX_SIP_SRTC			0xC2000002
#define IMX_SIP_SRTC_SET_TIME		0x0

#define SC_IRQ_GROUP_RTC    2
#define SC_IRQ_RTC          1

static struct imx_sc_ipc *rtc_ipc_handle;
static struct rtc_device *imx_sc_rtc;

struct imx_sc_msg_timer_get_rtc_time {
	struct imx_sc_rpc_msg hdr;
	u32 time;
} __packed;

struct imx_sc_msg_timer_rtc_set_alarm {
	struct imx_sc_rpc_msg hdr;
	u16 year;
	u8 mon;
	u8 day;
	u8 hour;
	u8 min;
	u8 sec;
} __packed __aligned(4);

static int imx_sc_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct imx_sc_msg_timer_get_rtc_time msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_TIMER;
	hdr->func = IMX_SC_TIMER_FUNC_GET_RTC_SEC1970;
	hdr->size = 1;

	ret = imx_scu_call_rpc(rtc_ipc_handle, &msg, true);
	if (ret) {
		dev_err(dev, "read rtc time failed, ret %d\n", ret);
		return ret;
	}

	rtc_time64_to_tm(msg.time, tm);

	return 0;
}

static int imx_sc_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct arm_smccc_res res;

	/* pack 2 time parameters into 1 register, 16 bits for each */
	arm_smccc_smc(IMX_SIP_SRTC, IMX_SIP_SRTC_SET_TIME,
		      ((tm->tm_year + 1900) << 16) | (tm->tm_mon + 1),
		      (tm->tm_mday << 16) | tm->tm_hour,
		      (tm->tm_min << 16) | tm->tm_sec,
		      0, 0, 0, &res);

	return res.a0;
}

static int imx_sc_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	return imx_scu_irq_group_enable(SC_IRQ_GROUP_RTC, SC_IRQ_RTC, enable);
}

static int imx_sc_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	/*
	 * SCU firmware does NOT provide read alarm API, but .read_alarm
	 * callback is required by RTC framework to support alarm function,
	 * so just return here.
	 */
	return 0;
}

static int imx_sc_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct imx_sc_msg_timer_rtc_set_alarm msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;
	struct rtc_time *alrm_tm = &alrm->time;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_TIMER;
	hdr->func = IMX_SC_TIMER_FUNC_SET_RTC_ALARM;
	hdr->size = 3;

	msg.year = alrm_tm->tm_year + 1900;
	msg.mon = alrm_tm->tm_mon + 1;
	msg.day = alrm_tm->tm_mday;
	msg.hour = alrm_tm->tm_hour;
	msg.min = alrm_tm->tm_min;
	msg.sec = alrm_tm->tm_sec;

	ret = imx_scu_call_rpc(rtc_ipc_handle, &msg, true);
	if (ret) {
		dev_err(dev, "set rtc alarm failed, ret %d\n", ret);
		return ret;
	}

	ret = imx_sc_rtc_alarm_irq_enable(dev, alrm->enabled);
	if (ret) {
		dev_err(dev, "enable rtc alarm failed, ret %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct rtc_class_ops imx_sc_rtc_ops = {
	.read_time = imx_sc_rtc_read_time,
	.set_time = imx_sc_rtc_set_time,
	.read_alarm = imx_sc_rtc_read_alarm,
	.set_alarm = imx_sc_rtc_set_alarm,
	.alarm_irq_enable = imx_sc_rtc_alarm_irq_enable,
};

static int imx_sc_rtc_alarm_notify(struct notifier_block *nb,
					unsigned long event, void *group)
{
	/* ignore non-rtc irq */
	if (!((event & SC_IRQ_RTC) && (*(u8 *)group == SC_IRQ_GROUP_RTC)))
		return 0;

	rtc_update_irq(imx_sc_rtc, 1, RTC_IRQF | RTC_AF);

	return 0;
}

static struct notifier_block imx_sc_rtc_alarm_sc_notifier = {
	.notifier_call = imx_sc_rtc_alarm_notify,
};

static int imx_sc_rtc_probe(struct platform_device *pdev)
{
	int ret;

	ret = imx_scu_get_handle(&rtc_ipc_handle);
	if (ret)
		return ret;

	device_init_wakeup(&pdev->dev, true);

	imx_sc_rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(imx_sc_rtc))
		return PTR_ERR(imx_sc_rtc);

	imx_sc_rtc->ops = &imx_sc_rtc_ops;
	imx_sc_rtc->range_min = 0;
	imx_sc_rtc->range_max = U32_MAX;

	ret = rtc_register_device(imx_sc_rtc);
	if (ret)
		return ret;

	imx_scu_irq_register_notifier(&imx_sc_rtc_alarm_sc_notifier);

	return 0;
}

static const struct of_device_id imx_sc_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-sc-rtc", },
	{}
};
MODULE_DEVICE_TABLE(of, imx_sc_dt_ids);

static struct platform_driver imx_sc_rtc_driver = {
	.driver = {
		.name	= "imx-sc-rtc",
		.of_match_table = imx_sc_dt_ids,
	},
	.probe		= imx_sc_rtc_probe,
};
module_platform_driver(imx_sc_rtc_driver);

MODULE_AUTHOR("Anson Huang <Anson.Huang@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX System Controller RTC Driver");
MODULE_LICENSE("GPL");
