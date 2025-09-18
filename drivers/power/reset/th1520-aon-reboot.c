// SPDX-License-Identifier: GPL-2.0
/*
 * T-HEAD TH1520 AON Firmware Reboot Driver
 *
 * Copyright (c) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/auxiliary_bus.h>
#include <linux/firmware/thead/thead,th1520-aon.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/slab.h>

#define TH1520_AON_REBOOT_PRIORITY 200

struct th1520_aon_msg_empty_body {
	struct th1520_aon_rpc_msg_hdr hdr;
	u16 reserved[12];
} __packed __aligned(1);

static int th1520_aon_pwroff_handler(struct sys_off_data *data)
{
	struct th1520_aon_chan *aon_chan = data->cb_data;
	struct th1520_aon_msg_empty_body msg = {};

	msg.hdr.svc = TH1520_AON_RPC_SVC_WDG;
	msg.hdr.func = TH1520_AON_WDG_FUNC_POWER_OFF;
	msg.hdr.size = TH1520_AON_RPC_MSG_NUM;

	th1520_aon_call_rpc(aon_chan, &msg);

	return NOTIFY_DONE;
}

static int th1520_aon_restart_handler(struct sys_off_data *data)
{
	struct th1520_aon_chan *aon_chan = data->cb_data;
	struct th1520_aon_msg_empty_body msg = {};

	msg.hdr.svc = TH1520_AON_RPC_SVC_WDG;
	msg.hdr.func = TH1520_AON_WDG_FUNC_RESTART;
	msg.hdr.size = TH1520_AON_RPC_MSG_NUM;

	th1520_aon_call_rpc(aon_chan, &msg);

	return NOTIFY_DONE;
}

static int th1520_aon_reboot_probe(struct auxiliary_device *adev,
				  const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	int ret;

	/* Expect struct th1520_aon_chan to be passed via platform_data */
	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_POWER_OFF,
					    TH1520_AON_REBOOT_PRIORITY,
					    th1520_aon_pwroff_handler,
					    adev->dev.platform_data);

	if (ret) {
		dev_err(dev, "Failed to register power off handler\n");
		return ret;
	}

	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_RESTART,
					    TH1520_AON_REBOOT_PRIORITY,
					    th1520_aon_restart_handler,
					    adev->dev.platform_data);

	if (ret) {
		dev_err(dev, "Failed to register restart handler\n");
		return ret;
	}

	return 0;
}

static const struct auxiliary_device_id th1520_aon_reboot_id_table[] = {
	{ .name = "th1520_pm_domains.reboot" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, th1520_aon_reboot_id_table);

static struct auxiliary_driver th1520_aon_reboot_driver = {
	.driver = {
		.name = "th1520-aon-reboot",
	},
	.probe = th1520_aon_reboot_probe,
	.id_table = th1520_aon_reboot_id_table,
};
module_auxiliary_driver(th1520_aon_reboot_driver);

MODULE_AUTHOR("Icenowy Zheng <uwu@icenowy.me>");
MODULE_DESCRIPTION("T-HEAD TH1520 AON-firmware-based reboot driver");
MODULE_LICENSE("GPL");
