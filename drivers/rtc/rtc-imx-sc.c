// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP.
 */

#include <linux/firmware/imx/sci.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define IMX_SC_TIMER_FUNC_GET_RTC_SEC1970	9
#define IMX_SC_TIMER_FUNC_SET_RTC_TIME		6

static struct imx_sc_ipc *rtc_ipc_handle;
static struct rtc_device *imx_sc_rtc;

struct imx_sc_msg_timer_get_rtc_time {
	struct imx_sc_rpc_msg hdr;
	u32 time;
} __packed;

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

	rtc_time_to_tm(msg.time, tm);

	return 0;
}

static const struct rtc_class_ops imx_sc_rtc_ops = {
	.read_time = imx_sc_rtc_read_time,
};

static int imx_sc_rtc_probe(struct platform_device *pdev)
{
	int ret;

	ret = imx_scu_get_handle(&rtc_ipc_handle);
	if (ret)
		return ret;

	imx_sc_rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(imx_sc_rtc))
		return PTR_ERR(imx_sc_rtc);

	imx_sc_rtc->ops = &imx_sc_rtc_ops;
	imx_sc_rtc->range_min = 0;
	imx_sc_rtc->range_max = U32_MAX;

	ret = rtc_register_device(imx_sc_rtc);
	if (ret) {
		dev_err(&pdev->dev, "failed to register rtc: %d\n", ret);
		return ret;
	}

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
