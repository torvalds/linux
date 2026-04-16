// SPDX-License-Identifier: GPL-2.0
/*
 * QEMU Virt Machine System Controller Driver
 *
 * Copyright (C) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

/* Registers */
#define VIRT_CTRL_REG_FEATURES	0x00
#define VIRT_CTRL_REG_CMD	0x04

/* Commands */
#define CMD_NOOP	0
#define CMD_RESET	1
#define CMD_HALT	2
#define CMD_PANIC	3

struct qemu_virt_ctrl {
	void __iomem *base;
	struct notifier_block reboot_nb;
};

static inline void virt_ctrl_write32(u32 val, void __iomem *addr)
{
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		iowrite32be(val, addr);
	else
		iowrite32(val, addr);
}

static int qemu_virt_ctrl_power_off(struct sys_off_data *data)
{
	struct qemu_virt_ctrl *ctrl = data->cb_data;

	virt_ctrl_write32(CMD_HALT, ctrl->base + VIRT_CTRL_REG_CMD);

	return NOTIFY_DONE;
}

static int qemu_virt_ctrl_restart(struct sys_off_data *data)
{
	struct qemu_virt_ctrl *ctrl = data->cb_data;

	virt_ctrl_write32(CMD_RESET, ctrl->base + VIRT_CTRL_REG_CMD);

	return NOTIFY_DONE;
}

static int qemu_virt_ctrl_reboot_notify(struct notifier_block *nb,
					unsigned long action, void *data)
{
	struct qemu_virt_ctrl *ctrl = container_of(nb, struct qemu_virt_ctrl, reboot_nb);

	if (action == SYS_HALT)
		virt_ctrl_write32(CMD_HALT, ctrl->base + VIRT_CTRL_REG_CMD);

	return NOTIFY_DONE;
}

static int qemu_virt_ctrl_probe(struct platform_device *pdev)
{
	struct qemu_virt_ctrl *ctrl;
	int ret;

	ctrl = devm_kzalloc(&pdev->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	ret = devm_register_sys_off_handler(&pdev->dev,
					    SYS_OFF_MODE_RESTART,
					    SYS_OFF_PRIO_DEFAULT,
					    qemu_virt_ctrl_restart,
					    ctrl);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "cannot register restart handler\n");

	ret = devm_register_sys_off_handler(&pdev->dev,
					    SYS_OFF_MODE_POWER_OFF,
					    SYS_OFF_PRIO_DEFAULT,
					    qemu_virt_ctrl_power_off,
					    ctrl);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "cannot register power-off handler\n");

	ctrl->reboot_nb.notifier_call = qemu_virt_ctrl_reboot_notify;
	ret = devm_register_reboot_notifier(&pdev->dev, &ctrl->reboot_nb);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "cannot register reboot notifier\n");

	return 0;
}

static const struct platform_device_id qemu_virt_ctrl_id[] = {
	{ "qemu-virt-ctrl", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, qemu_virt_ctrl_id);

static struct platform_driver qemu_virt_ctrl_driver = {
	.probe = qemu_virt_ctrl_probe,
	.driver = {
		.name = "qemu-virt-ctrl",
	},
	.id_table = qemu_virt_ctrl_id,
};
module_platform_driver(qemu_virt_ctrl_driver);

MODULE_AUTHOR("Kuan-Wei Chiu <visitorckw@gmail.com>");
MODULE_DESCRIPTION("QEMU Virt Machine System Controller Driver");
MODULE_LICENSE("GPL");
