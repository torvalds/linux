/*
 * Copyright (C) 2018 Olliver Schinagl
 *
 * Olliver Schinagl <oliver@schinagl.nl>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "sun4i_hdmi_i2c_drv.h"

#define SUN4I_HDMI_I2C_DRIVER_NAME "sun4i-hdmi-i2c"

static const struct sun4i_hdmi_i2c_variant sun4i_variant = {
	.ddc_clk_reg		= REG_FIELD(SUN4I_HDMI_DDC_CLK_REG, 0, 6),
	.ddc_clk_pre_divider	= 2,
	.ddc_clk_m_offset	= 1,

	.field_ddc_en		= REG_FIELD(SUN4I_HDMI_DDC_CTRL_REG, 31, 31),
	.field_ddc_start	= REG_FIELD(SUN4I_HDMI_DDC_CTRL_REG, 30, 30),
	.field_ddc_reset	= REG_FIELD(SUN4I_HDMI_DDC_CTRL_REG, 0, 0),
	.field_ddc_addr_reg	= REG_FIELD(SUN4I_HDMI_DDC_ADDR_REG, 0, 31),
	.field_ddc_slave_addr	= REG_FIELD(SUN4I_HDMI_DDC_ADDR_REG, 0, 6),
	.field_ddc_int_status	= REG_FIELD(SUN4I_HDMI_DDC_INT_STATUS_REG, 0, 8),
	.field_ddc_fifo_clear	= REG_FIELD(SUN4I_HDMI_DDC_FIFO_CTRL_REG, 31, 31),
	.field_ddc_fifo_rx_thres = REG_FIELD(SUN4I_HDMI_DDC_FIFO_CTRL_REG, 4, 7),
	.field_ddc_fifo_tx_thres = REG_FIELD(SUN4I_HDMI_DDC_FIFO_CTRL_REG, 0, 3),
	.field_ddc_byte_count	= REG_FIELD(SUN4I_HDMI_DDC_BYTE_COUNT_REG, 0, 9),
	.field_ddc_cmd		= REG_FIELD(SUN4I_HDMI_DDC_CMD_REG, 0, 2),
	.field_ddc_sda_en	= REG_FIELD(SUN4I_HDMI_DDC_LINE_CTRL_REG, 9, 9),
	.field_ddc_sck_en	= REG_FIELD(SUN4I_HDMI_DDC_LINE_CTRL_REG, 8, 8),
	.field_ddc_bus_busy	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG, 10, 10),
	.field_ddc_sda_state	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG, 9, 9),
	.field_ddc_sck_state	= REG_FIELD(SUN4I_HDMI_DDC_EXT_REG, 8, 8),
	.field_ddc_sck_line_ctrl = REG_FIELD(SUN4I_HDMI_DDC_EXT_REG, 3, 3),
	.field_ddc_sck_line_ctrl_en = REG_FIELD(SUN4I_HDMI_DDC_EXT_REG, 2, 2),
	.field_ddc_sda_line_ctrl = REG_FIELD(SUN4I_HDMI_DDC_EXT_REG, 1, 1),
	.field_ddc_sda_line_ctrl_en = REG_FIELD(SUN4I_HDMI_DDC_EXT_REG, 0, 0),

	.ddc_fifo_reg		= SUN4I_HDMI_DDC_FIFO_DATA_REG,
	.ddc_fifo_has_dir	= true,
};

static const struct sun4i_hdmi_i2c_variant sun6i_variant = {
	.ddc_clk_reg		= REG_FIELD(SUN6I_HDMI_DDC_CLK_REG, 0, 6),
	.ddc_clk_pre_divider	= 1,
	.ddc_clk_m_offset	= 2,

	.field_ddc_en		= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG, 0, 0),
	.field_ddc_start	= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG, 27, 27),
	.field_ddc_reset	= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG, 31, 31),
	.field_ddc_addr_reg	= REG_FIELD(SUN6I_HDMI_DDC_ADDR_REG, 1, 31),
	.field_ddc_slave_addr	= REG_FIELD(SUN6I_HDMI_DDC_ADDR_REG, 1, 7),
	.field_ddc_int_status	= REG_FIELD(SUN6I_HDMI_DDC_INT_STATUS_REG, 0, 8),
	.field_ddc_fifo_clear	= REG_FIELD(SUN6I_HDMI_DDC_FIFO_CTRL_REG, 18, 18),
	.field_ddc_fifo_rx_thres = REG_FIELD(SUN6I_HDMI_DDC_FIFO_CTRL_REG, 4, 7),
	.field_ddc_fifo_tx_thres = REG_FIELD(SUN6I_HDMI_DDC_FIFO_CTRL_REG, 0, 3),
	.field_ddc_byte_count	= REG_FIELD(SUN6I_HDMI_DDC_CMD_REG, 16, 25),
	.field_ddc_cmd		= REG_FIELD(SUN6I_HDMI_DDC_CMD_REG, 0, 2),
	.field_ddc_sda_en	= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG, 6, 6),
	.field_ddc_sck_en	= REG_FIELD(SUN6I_HDMI_DDC_CTRL_REG, 4, 4),
	.field_ddc_bus_busy	= REG_FIELD(SUN6I_HDMI_DDC_EXT_REG, 10, 10),
	.field_ddc_sda_state	= REG_FIELD(SUN6I_HDMI_DDC_EXT_REG, 9, 9),
	.field_ddc_sck_state	= REG_FIELD(SUN6I_HDMI_DDC_EXT_REG, 8, 8),
	.field_ddc_sck_line_ctrl = REG_FIELD(SUN6I_HDMI_DDC_EXT_REG, 3, 3),
	.field_ddc_sck_line_ctrl_en = REG_FIELD(SUN6I_HDMI_DDC_EXT_REG, 2, 2),
	.field_ddc_sda_line_ctrl = REG_FIELD(SUN6I_HDMI_DDC_EXT_REG, 1, 1),
	.field_ddc_sda_line_ctrl_en = REG_FIELD(SUN6I_HDMI_DDC_EXT_REG, 0, 0),

	.ddc_fifo_reg		= SUN6I_HDMI_DDC_FIFO_DATA_REG,
	.ddc_fifo_thres_incl	= true,
};

static const struct regmap_config sun4i_hdmi_i2c_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x80,
};

static const struct of_device_id sun4i_hdmi_i2c_of_table[] = {
	{ .compatible = "allwinner,sun4i-a10-hdmi-i2c", .data = &sun4i_variant },
	{ .compatible = "allwinner,sun6i-a31-hdmi-i2c", .data = &sun6i_variant },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun4i_hdmi_i2c_of_table);

static int sun4i_hdmi_i2c_probe(struct platform_device *pdev)
{
	struct sun4i_hdmi_i2c_drv *drv;
	struct resource *res;
	void __iomem *base;

	if (!pdev)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "couldn't map the HDMI-I2C registers\n");
		return PTR_ERR(base);
	}

	drv = sun4i_hdmi_i2c_init(&pdev->dev, base, sun4i_hdmi_i2c_of_table,
				  &sun4i_hdmi_i2c_regmap_config, NULL);
	if (IS_ERR(drv)) {
		if (PTR_ERR(drv) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "couldn't setup HDMI-I2C driver\n");
		return PTR_ERR(drv);
	}

	platform_set_drvdata(pdev, drv);

	return 0;
}


static int sun4i_hdmi_i2c_remove(struct platform_device *pdev)
{
	struct sun4i_hdmi_i2c_drv *drv = platform_get_drvdata(pdev);

	sun4i_hdmi_i2c_fini(drv);

	return 0;
}

static struct platform_driver sun4i_hdmi_i2c_driver = {
	.probe	= sun4i_hdmi_i2c_probe,
	.remove	= sun4i_hdmi_i2c_remove,
	.driver	= {
		.name 		= SUN4I_HDMI_I2C_DRIVER_NAME,
		.of_match_table	= sun4i_hdmi_i2c_of_table,
	},
};
module_platform_driver(sun4i_hdmi_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Olliver Schinagl <oliver@schinagl.nl>");
MODULE_DESCRIPTION("I2C adapter driver for Allwinner sunxi HDMI I2C bus");
MODULE_ALIAS("platform:" SUN4I_HDMI_I2C_DRIVER_NAME);
