/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 * Authors:
 *	Rahul Sharma <rahul.sharma@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;	either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include "regs-hdmiphy.h"
#include "exynos_hdmiphy.h"
#include "exynos_hdmiphy_priv.h"

/* list of default phy config settings */
static struct hdmiphy_config hdmiphy_4212_configs[] = {
	{
		.pixel_clock = 25200000,
		.conf = {
			0x51, 0x2A, 0x75, 0x40, 0x01, 0x00, 0x08, 0x82,
			0x80, 0xfc, 0xd8, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0xf4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 27000000,
		.conf = {
			0xd1, 0x22, 0x51, 0x40, 0x08, 0xfc, 0x20, 0x98,
			0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80, 0x06,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0xe4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 27027000,
		.conf = {
			0xd1, 0x2d, 0x72, 0x40, 0x64, 0x12, 0x08, 0x43,
			0xa0, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0xe3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		},
	},
	{
		.pixel_clock = 36000000,
		.conf = {
			0x51, 0x2d, 0x55, 0x40, 0x01, 0x00, 0x08, 0x82,
			0x80, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0xab, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 40000000,
		.conf = {
			0x51, 0x32, 0x55, 0x40, 0x01, 0x00, 0x08, 0x82,
			0x80, 0x2c, 0xd9, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0x9a, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 65000000,
		.conf = {
			0xd1, 0x36, 0x34, 0x40, 0x1e, 0x0a, 0x08, 0x82,
			0xa0, 0x45, 0xd9, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0xbd, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 74176000,
		.conf = {
			0xd1, 0x3e, 0x35, 0x40, 0x5b, 0xde, 0x08, 0x82,
			0xa0, 0x73, 0xd9, 0x45, 0xa0, 0xac, 0x80, 0x56,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0xa6, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 74250000,
		.conf = {
			0xd1, 0x1f, 0x10, 0x40, 0x40, 0xf8, 0x08, 0x81,
			0xa0, 0xba, 0xd8, 0x45, 0xa0, 0xac, 0x80, 0x3c,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0xa5, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		},
	},
	{
		.pixel_clock = 83500000,
		.conf = {
			0xd1, 0x23, 0x11, 0x40, 0x0c, 0xfb, 0x08, 0x85,
			0xa0, 0xd1, 0xd8, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0x93, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 106500000,
		.conf = {
			0xd1, 0x2c, 0x12, 0x40, 0x0c, 0x09, 0x08, 0x84,
			0xa0, 0x0a, 0xd9, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0x73, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 108000000,
		.conf = {
			0x51, 0x2d, 0x15, 0x40, 0x01, 0x00, 0x08, 0x82,
			0x80, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0xc7, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 146250000,
		.conf = {
			0xd1, 0x3d, 0x15, 0x40, 0x18, 0xfd, 0x08, 0x83,
			0xa0, 0x6e, 0xd9, 0x45, 0xa0, 0xac, 0x80, 0x08,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0x50, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 148500000,
		.conf = {
			0xd1, 0x1f, 0x00, 0x40, 0x40, 0xf8, 0x08, 0x81,
			0xa0, 0xba, 0xd8, 0x45, 0xa0, 0xac, 0x80, 0x3c,
			0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86, 0x54,
			0x4b, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	},
};

static struct hdmiphy_config hdmiphy_4210_configs[] = {
	{
		.pixel_clock = 27000000,
		.conf = {
			0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40, 0x6B,
			0x10, 0x02, 0x51, 0xDF, 0xF2, 0x54, 0x87, 0x84,
			0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0, 0x22,
			0x40, 0xE3, 0x26, 0x00, 0x00, 0x00, 0x00,
		},
	},
	{
		.pixel_clock = 27027000,
		.conf = {
			0x05, 0x00, 0xD4, 0x10, 0x9C, 0x09, 0x64, 0x6B,
			0x10, 0x02, 0x51, 0xDF, 0xF2, 0x54, 0x87, 0x84,
			0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0, 0x22,
			0x40, 0xE3, 0x26, 0x00, 0x00, 0x00, 0x00,
		},
	},
	{
		.pixel_clock = 74176000,
		.conf = {
			0x05, 0x00, 0xD8, 0x10, 0x9C, 0xef, 0x5B, 0x6D,
			0x10, 0x01, 0x51, 0xef, 0xF3, 0x54, 0xb9, 0x84,
			0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0, 0x22,
			0x40, 0xa5, 0x26, 0x01, 0x00, 0x00, 0x00,
		},
	},
	{
		.pixel_clock = 74250000,
		.conf = {
			0x05, 0x00, 0xd8, 0x10, 0x9c, 0xf8, 0x40, 0x6a,
			0x10, 0x01, 0x51, 0xff, 0xf1, 0x54, 0xba, 0x84,
			0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xe0, 0x22,
			0x40, 0xa4, 0x26, 0x01, 0x00, 0x00, 0x00,
		},
	},
	{
		.pixel_clock = 148500000,
		.conf = {
			0x05, 0x00, 0xD8, 0x10, 0x9C, 0xf8, 0x40, 0x6A,
			0x18, 0x00, 0x51, 0xff, 0xF1, 0x54, 0xba, 0x84,
			0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0, 0x22,
			0x40, 0xa4, 0x26, 0x02, 0x00, 0x00, 0x00,
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

static int hdmiphy_dt_parse_power_control(struct i2c_client *client)
{
	struct device_node *phy_pow_ctrl_node;
	struct hdmiphy_context *hdata = i2c_get_clientdata(client);
	u32 buf[2];
	int ret = 0;

	phy_pow_ctrl_node = of_get_child_by_name(client->dev.of_node,
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

	hdata->phy_pow_ctrl_reg = devm_ioremap(&client->dev, buf[0], buf[1]);
	if (!hdata->phy_pow_ctrl_reg) {
		DRM_ERROR("failed to ioremap phy pmu reg\n");
		ret = -ENOMEM;
	}

out:
	of_node_put(phy_pow_ctrl_node);
	return ret;
}

static inline void hdmiphy_pow_ctrl_reg_writemask(
			struct hdmiphy_context *hdata,
			u32 value, u32 mask)
{
	u32 old = readl(hdata->phy_pow_ctrl_reg);
	value = (value & mask) | (old & ~mask);
	writel(value, hdata->phy_pow_ctrl_reg);
}

static int hdmiphy_reg_writeb(struct device *dev,
			u32 reg_offset, u8 value)
{
	u8 buffer[2];
	int ret;

	if (reg_offset >= HDMIPHY_REG_COUNT)
		return -EINVAL;

	buffer[0] = reg_offset;
	buffer[1] = value;

	ret = i2c_master_send(to_i2c_client(dev),
			buffer, 2);
	if (ret == 2)
		return 0;
	return ret;
}

static int hdmiphy_reg_write_buf(struct device *dev,
			u32 reg_offset, const u8 *buf, u32 len)
{
	int ret;
	u8 buffer[HDMIPHY_REG_COUNT];

	if ((reg_offset + len) > HDMIPHY_REG_COUNT)
		return -EINVAL;

	buffer[0] = reg_offset;
	memcpy(&buffer[1], buf, len);

	ret = i2c_master_send(to_i2c_client(dev),
			buffer, len);
	if (ret == len)
		return 0;
	return ret;
}

static int hdmiphy_check_mode(struct device *dev,
			struct drm_display_mode *mode)
{
	struct hdmiphy_context *hdata = dev_get_drvdata(dev);
	const struct hdmiphy_config *conf;

	DRM_DEBUG("xres=%d, yres=%d, refresh=%d, intl=%d clock=%d\n",
		mode->hdisplay, mode->vdisplay,
		mode->vrefresh, (mode->flags & DRM_MODE_FLAG_INTERLACE)
		? true : false, mode->clock * 1000);

	conf = hdmiphy_find_conf(hdata, (mode->clock * 1000));
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

	ret = hdmiphy_reg_write_buf(dev, 1, hdata->current_conf->conf,
			HDMIPHY_REG_COUNT - 1);
	if (ret) {
		DRM_ERROR("failed to configure hdmiphy.\n");
		return ret;
	}

	/* need this delay before phy can be set to operation. */
	usleep_range(10000, 12000);
	return 0;
}

static void hdmiphy_enable(struct device *dev, int enable)
{
	int ret;

	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	if (enable)
		ret = hdmiphy_reg_writeb(dev, HDMIPHY_MODE_SET_DONE,
				HDMIPHY_MODE_EN);
	else
		ret = hdmiphy_reg_writeb(dev, HDMIPHY_MODE_SET_DONE, 0);

	if (ret < 0) {
		DRM_ERROR("failed to %s hdmiphy. ret %d.\n",
				enable ? "enable" : "disable", ret);
		return;
	}
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

struct exynos_hdmiphy_ops *exynos_hdmiphy_i2c_device_get_ops
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

static struct hdmiphy_drv_data exynos4212_hdmiphy_drv_data = {
	.confs = hdmiphy_4212_configs,
	.count = ARRAY_SIZE(hdmiphy_4212_configs)
};

static struct hdmiphy_drv_data exynos4210_hdmiphy_drv_data = {
	.confs = hdmiphy_4210_configs,
	.count = ARRAY_SIZE(hdmiphy_4210_configs)
};

static struct of_device_id hdmiphy_i2c_device_match_types[] = {
	{
		.compatible = "samsung,exynos5-hdmiphy",
		.data	= &exynos4212_hdmiphy_drv_data,
	}, {
		.compatible = "samsung,exynos4210-hdmiphy",
		.data	= &exynos4210_hdmiphy_drv_data,
	}, {
		.compatible = "samsung,exynos4212-hdmiphy",
		.data	= &exynos4212_hdmiphy_drv_data,
	}, {
		/* end node */
	}
};

static int hdmiphy_i2c_device_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct hdmiphy_context *hdata;
	struct hdmiphy_drv_data *drv;
	const struct of_device_id *match;
	int ret;

	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	hdata = devm_kzalloc(dev, sizeof(*hdata), GFP_KERNEL);
	if (!hdata) {
		DRM_ERROR("failed to allocate hdmiphy context.\n");
		return -ENOMEM;
	}

	match = of_match_node(of_match_ptr(
		hdmiphy_i2c_device_match_types),
		dev->of_node);

	if (!match)
		return -ENODEV;

	drv = (struct hdmiphy_drv_data *)match->data;

	hdata->confs = drv->confs;
	hdata->nr_confs = drv->count;
	hdata->ops = &phy_ops;

	i2c_set_clientdata(client, hdata);
	ret = hdmiphy_dt_parse_power_control(client);
	if (ret) {
		DRM_ERROR("failed to map hdmiphy pow control reg.\n");
		return ret;
	}

	return 0;
}

static const struct i2c_device_id hdmiphy_id[] = {
	{ },
};

struct i2c_driver hdmiphy_i2c_driver = {
	.driver = {
		.name	= "exynos-hdmiphy",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(
				hdmiphy_i2c_device_match_types),
	},
	.id_table		= hdmiphy_id,
	.probe		= hdmiphy_i2c_device_probe,
	.command		= NULL,
};

int exynos_hdmiphy_i2c_driver_register(void)
{
	int ret;

	ret = i2c_add_driver(&hdmiphy_i2c_driver);
	if (ret)
		return ret;

	return 0;
}

void exynos_hdmiphy_i2c_driver_unregister(void)
{
	i2c_del_driver(&hdmiphy_i2c_driver);
}
