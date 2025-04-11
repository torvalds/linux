// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Driver for backlight controllers attached via Apple DWI 2-wire interface
 *
 * Copyright (c) 2024 Nick Chan <towinchenmi@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define DWI_BL_CTL			0x0
#define DWI_BL_CTL_SEND1		BIT(0)
#define DWI_BL_CTL_SEND2		BIT(4)
#define DWI_BL_CTL_SEND3		BIT(5)
#define DWI_BL_CTL_LE_DATA		BIT(6)
/* Only used on Apple A9 and later */
#define DWI_BL_CTL_SEND4		BIT(12)

#define DWI_BL_CMD			0x4
#define DWI_BL_CMD_TYPE			GENMASK(31, 28)
#define DWI_BL_CMD_TYPE_SET_BRIGHTNESS	0xa
#define DWI_BL_CMD_DATA			GENMASK(10, 0)

#define DWI_BL_CTL_SEND			(DWI_BL_CTL_SEND1 | \
					 DWI_BL_CTL_SEND2 | \
					 DWI_BL_CTL_SEND3 | \
					 DWI_BL_CTL_LE_DATA | \
					 DWI_BL_CTL_SEND4)

#define DWI_BL_MAX_BRIGHTNESS		2047

struct apple_dwi_bl {
	void __iomem *base;
};

static int dwi_bl_update_status(struct backlight_device *bl)
{
	struct apple_dwi_bl *dwi_bl = bl_get_data(bl);

	int brightness = backlight_get_brightness(bl);

	u32 cmd = 0;

	cmd |= FIELD_PREP(DWI_BL_CMD_DATA, brightness);
	cmd |= FIELD_PREP(DWI_BL_CMD_TYPE, DWI_BL_CMD_TYPE_SET_BRIGHTNESS);

	writel(cmd, dwi_bl->base + DWI_BL_CMD);
	writel(DWI_BL_CTL_SEND, dwi_bl->base + DWI_BL_CTL);

	return 0;
}

static int dwi_bl_get_brightness(struct backlight_device *bl)
{
	struct apple_dwi_bl *dwi_bl = bl_get_data(bl);

	u32 cmd = readl(dwi_bl->base + DWI_BL_CMD);

	return FIELD_GET(DWI_BL_CMD_DATA, cmd);
}

static const struct backlight_ops dwi_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = dwi_bl_get_brightness,
	.update_status	= dwi_bl_update_status
};

static int dwi_bl_probe(struct platform_device *dev)
{
	struct apple_dwi_bl *dwi_bl;
	struct backlight_device *bl;
	struct backlight_properties props;
	struct resource *res;

	dwi_bl = devm_kzalloc(&dev->dev, sizeof(*dwi_bl), GFP_KERNEL);
	if (!dwi_bl)
		return -ENOMEM;

	dwi_bl->base = devm_platform_get_and_ioremap_resource(dev, 0, &res);
	if (IS_ERR(dwi_bl->base))
		return PTR_ERR(dwi_bl->base);

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = DWI_BL_MAX_BRIGHTNESS;
	props.scale = BACKLIGHT_SCALE_LINEAR;

	bl = devm_backlight_device_register(&dev->dev, dev->name, &dev->dev,
					dwi_bl, &dwi_bl_ops, &props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	platform_set_drvdata(dev, dwi_bl);

	bl->props.brightness = dwi_bl_get_brightness(bl);

	return 0;
}

static const struct of_device_id dwi_bl_of_match[] = {
	{ .compatible = "apple,dwi-bl" },
	{},
};

MODULE_DEVICE_TABLE(of, dwi_bl_of_match);

static struct platform_driver dwi_bl_driver = {
	.driver		= {
		.name	= "apple-dwi-bl",
		.of_match_table = dwi_bl_of_match
	},
	.probe		= dwi_bl_probe,
};

module_platform_driver(dwi_bl_driver);

MODULE_DESCRIPTION("Apple DWI Backlight Driver");
MODULE_AUTHOR("Nick Chan <towinchenmi@gmail.com>");
MODULE_LICENSE("Dual MIT/GPL");
