/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 * Authors:
 *	Rahul Sharma <rahul.sharma@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/module.h>

#include "regs-hdmiphy.h"
#include "exynos_hdmiphy.h"
#include "exynos_hdmiphy_priv.h"

/* default phy config settings for exynos5420 */
static struct hdmiphy_config hdmiphy_5420_configs[] = {
	{
		.pixel_clock = 25200000,
		.conf = {
			0x52, 0x3F, 0x55, 0x40, 0x01, 0x00, 0xC8, 0x82,
			0xC8, 0xBD, 0xD8, 0x45, 0xA0, 0xAC, 0x80, 0x06,
			0x80, 0x01, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0xF4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 27000000,
		.conf = {
			0xD1, 0x22, 0x51, 0x40, 0x08, 0xFC, 0xE0, 0x98,
			0xE8, 0xCB, 0xD8, 0x45, 0xA0, 0xAC, 0x80, 0x06,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0xE4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 27027000,
		.conf = {
			0xD1, 0x2D, 0x72, 0x40, 0x64, 0x12, 0xC8, 0x43,
			0xE8, 0x0E, 0xD9, 0x45, 0xA0, 0xAC, 0x80, 0x06,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0xE3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 36000000,
		.conf = {
			0x51, 0x2D, 0x55, 0x40, 0x40, 0x00, 0xC8, 0x02,
			0xC8, 0x0E, 0xD9, 0x45, 0xA0, 0xAC, 0x80, 0x08,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0xAB, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 40000000,
		.conf = {
			0xD1, 0x21, 0x31, 0x40, 0x3C, 0x28, 0xC8, 0x87,
			0xE8, 0xC8, 0xD8, 0x45, 0xA0, 0xAC, 0x80, 0x08,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0x9A, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 65000000,
		.conf = {
			0xD1, 0x36, 0x34, 0x40, 0x0C, 0x04, 0xC8, 0x82,
			0xE8, 0x45, 0xD9, 0x45, 0xA0, 0xAC, 0x80, 0x08,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0xBD, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 71000000,
		.conf = {
			0xD1, 0x3B, 0x35, 0x40, 0x0C, 0x04, 0xC8, 0x85,
			0xE8, 0x63, 0xD9, 0x45, 0xA0, 0xAC, 0x80, 0x08,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0x57, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 74176000,
		.conf = {
			0xD1, 0x1F, 0x10, 0x40, 0x5B, 0xEF, 0xC8, 0x81,
			0xE8, 0xB9, 0xD8, 0x45, 0xA0, 0xAC, 0x80, 0x56,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0xA6, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 74250000,
		.conf = {
			0xD1, 0x1F, 0x10, 0x40, 0x40, 0xF8, 0xC8, 0x81,
			0xE8, 0xBA, 0xD8, 0x45, 0xA0, 0xAC, 0x80, 0x56,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0xA5, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 83500000,
		.conf = {
			0xD1, 0x23, 0x11, 0x40, 0x0C, 0xFB, 0xC8, 0x85,
			0xE8, 0xD1, 0xD8, 0x45, 0xA0, 0xAC, 0x80, 0x08,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0x4A, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 106500000,
		.conf = {
			0xD1, 0x2C, 0x12, 0x40, 0x0C, 0x09, 0xC8, 0x84,
			0xE8, 0x0A, 0xD9, 0x45, 0xA0, 0xAC, 0x80, 0x08,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0x73, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 108000000,
		.conf = {
			0x51, 0x2D, 0x15, 0x40, 0x01, 0x00, 0xC8, 0x82,
			0xC8, 0x0E, 0xD9, 0x45, 0xA0, 0xAC, 0x80, 0x08,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0xC7, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 146250000,
		.conf = {
			0xD1, 0x3D, 0x15, 0x40, 0x18, 0xFD, 0xC8, 0x83,
			0xE8, 0x6E, 0xD9, 0x45, 0xA0, 0xAC, 0x80, 0x08,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0x54, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 148500000,
		.conf = {
			0xD1, 0x1F, 0x00, 0x40, 0x40, 0xF8, 0xC8, 0x81,
			0xE8, 0xBA, 0xD8, 0x45, 0xA0, 0xAC, 0x80, 0x66,
			0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66, 0x54,
			0x4B, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
		},
	},
};

static struct hdmiphy_config *hdmiphy_find_conf(struct hdmiphy_context *hdata,
			unsigned int pixel_clk)
{
	int i;

	for (i = 0; i < hdata->nr_confs; i++)
		if (hdata->confs[i].pixel_clock == pixel_clk)
			return &hdata->confs[i];

	return NULL;
}

static int hdmiphy_dt_parse_power_control(struct platform_device *pdev)
{
	struct device_node *phy_pow_ctrl_node;
	struct hdmiphy_context *hdata = platform_get_drvdata(pdev);
	u32 buf[2];
	int ret = 0;

	phy_pow_ctrl_node = of_get_child_by_name(pdev->dev.of_node,
			"phy-power-control");
	if (!phy_pow_ctrl_node) {
		DRM_ERROR("Failed to find phy power control node\n");
		return -ENODEV;
	}

	/* reg property holds two informations: addr of pmu register, size */
	if (of_property_read_u32_array(phy_pow_ctrl_node, "reg", buf, 2)) {
		DRM_ERROR("faild to get phy power control reg\n");
		ret = -EINVAL;
		goto out;
	}

	hdata->phy_pow_ctrl_reg = devm_ioremap(&pdev->dev, buf[0], buf[1]);
	if (!hdata->phy_pow_ctrl_reg) {
		DRM_ERROR("failed to ioremap phy pmu reg\n");
		ret = -ENOMEM;
	}

out:
	of_node_put(phy_pow_ctrl_node);
	return ret;
}

static void hdmiphy_pow_ctrl_reg_writemask(
			struct hdmiphy_context *hdata,
			u32 value, u32 mask)
{
	u32 old = readl(hdata->phy_pow_ctrl_reg);
	value = (value & mask) | (old & ~mask);
	writel(value, hdata->phy_pow_ctrl_reg);
}

static int hdmiphy_reg_writeb(struct hdmiphy_context *hdata,
			u32 reg_offset, u8 value)
{
	if (reg_offset >= HDMIPHY_REG_COUNT)
		return -EINVAL;

	writeb(value, hdata->regs + (reg_offset<<2));
	return 0;
}

static int hdmiphy_reg_write_buf(struct hdmiphy_context *hdata,
			u32 reg_offset, const u8 *buf, u32 len)
{
	int i;

	if ((reg_offset + len) > HDMIPHY_REG_COUNT)
		return -EINVAL;

	for (i = 0; i < len; i++)
		writeb(buf[i], hdata->regs +
			((reg_offset + i)<<2));
	return 0;
}

static int hdmiphy_check_mode(struct device *dev,
			struct drm_display_mode *mode)
{
	struct hdmiphy_context *hdata = dev_get_drvdata(dev);
	const struct hdmiphy_config *conf;

	DRM_DEBUG("%s xres=%d, yres=%d, refresh=%d, intl=%d clock=%d\n",
		__func__, mode->hdisplay, mode->vdisplay,
		mode->vrefresh, (mode->flags & DRM_MODE_FLAG_INTERLACE)
		? true : false, mode->clock * 1000);

	conf = hdmiphy_find_conf(hdata, mode->clock * 1000);
	if (!conf) {
		DRM_DEBUG("Display Mode is not supported.\n");
		return -EINVAL;
	}
	return 0;
}

static int hdmiphy_mode_set(struct device *dev,
			struct drm_display_mode *mode)
{
	struct hdmiphy_context *hdata = dev_get_drvdata(dev);

	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	hdata->current_conf = hdmiphy_find_conf(hdata, mode->clock * 1000);
	if (!hdata->current_conf) {
		DRM_ERROR("Display Mode is not supported.\n");
		return -EINVAL;
	}
	return 0;
}

static int hdmiphy_commit(struct device *dev)
{
	struct hdmiphy_context *hdata = dev_get_drvdata(dev);
	int ret;

	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	ret = hdmiphy_reg_write_buf(hdata, 1, hdata->current_conf->conf,
			HDMIPHY_REG_COUNT - 1);
	if (ret) {
		DRM_ERROR("failed to configure hdmiphy. ret %d.\n", ret);
		return ret;
	}

	/* need this delay before phy can be set to operation. */
	usleep_range(10000, 12000);
	return 0;
}

static void hdmiphy_enable(struct device *dev, int enable)
{
	struct hdmiphy_context *hdata = dev_get_drvdata(dev);

	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	if (enable)
		hdmiphy_reg_writeb(hdata, HDMIPHY_MODE_SET_DONE,
				HDMIPHY_MODE_EN);
	else
		hdmiphy_reg_writeb(hdata, HDMIPHY_MODE_SET_DONE, 0);
}

static void hdmiphy_poweron(struct device *dev, int mode)
{
	struct hdmiphy_context *hdata = dev_get_drvdata(dev);

	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	if (mode)
		hdmiphy_pow_ctrl_reg_writemask(hdata, PMU_HDMI_PHY_ENABLE,
			PMU_HDMI_PHY_CONTROL_MASK);
	else
		hdmiphy_pow_ctrl_reg_writemask(hdata, PMU_HDMI_PHY_DISABLE,
			PMU_HDMI_PHY_CONTROL_MASK);
}

struct exynos_hdmiphy_ops *exynos_hdmiphy_platform_device_get_ops
			(struct device *dev)
{
	struct hdmiphy_context *hdata = dev_get_drvdata(dev);
	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	if (hdata)
		return hdata->ops;

	return NULL;
}

static struct exynos_hdmiphy_ops phy_ops = {
	.check_mode = hdmiphy_check_mode,
	.mode_set = hdmiphy_mode_set,
	.commit = hdmiphy_commit,
	.enable = hdmiphy_enable,
	.poweron = hdmiphy_poweron,
};

static struct hdmiphy_drv_data exynos5420_hdmiphy_drv_data = {
	.confs = hdmiphy_5420_configs,
	.count = ARRAY_SIZE(hdmiphy_5420_configs)
};

static struct of_device_id hdmiphy_platform_device_match_types[] = {
	{
		.compatible = "samsung,exynos5420-hdmiphy",
		.data	= &exynos5420_hdmiphy_drv_data,
	}, {
		/* end node */
	}
};

static int hdmiphy_platform_device_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdmiphy_context *hdata;
	struct hdmiphy_drv_data *drv;
	struct resource *res;
	const struct of_device_id *match;
	int ret;

	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	hdata = devm_kzalloc(dev, sizeof(*hdata), GFP_KERNEL);
	if (!hdata) {
		DRM_ERROR("failed to allocate hdmiphy context.\n");
		return -ENOMEM;
	}

	match = of_match_node(of_match_ptr(
		hdmiphy_platform_device_match_types),
		dev->of_node);

	if (!match)
		return -ENODEV;

	drv = (struct hdmiphy_drv_data *)match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		DRM_ERROR("failed to find phy registers\n");
		return -ENOENT;
	}

	hdata->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (!hdata->regs) {
		DRM_ERROR("failed to map registers\n");
		return -ENXIO;
	}

	hdata->confs = drv->confs;
	hdata->nr_confs = drv->count;
	hdata->ops = &phy_ops;

	platform_set_drvdata(pdev, hdata);
	ret = hdmiphy_dt_parse_power_control(pdev);
	if (ret) {
		DRM_ERROR("failed to map hdmiphy pow control reg.\n");
		return ret;
	}

	return 0;
}

struct platform_driver hdmiphy_platform_driver = {
	.driver = {
		.name	= "exynos-hdmiphy",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(
				hdmiphy_platform_device_match_types),
	},
	.probe		= hdmiphy_platform_device_probe,
};

int exynos_hdmiphy_platform_driver_register(void)
{
	int ret;

	ret = platform_driver_register(&hdmiphy_platform_driver);
	if (ret)
		return ret;

	return 0;
}

void exynos_hdmiphy_platform_driver_unregister(void)
{
	platform_driver_unregister(&hdmiphy_platform_driver);
}
