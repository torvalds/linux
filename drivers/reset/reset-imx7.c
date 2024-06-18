// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX7 System Reset Controller (SRC) driver
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/regmap.h>
#include <dt-bindings/reset/imx7-reset.h>
#include <dt-bindings/reset/imx8mq-reset.h>
#include <dt-bindings/reset/imx8mp-reset.h>

struct imx7_src_signal {
	unsigned int offset, bit;
};

struct imx7_src_variant {
	const struct imx7_src_signal *signals;
	unsigned int signals_num;
	struct reset_control_ops ops;
};

struct imx7_src {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
	const struct imx7_src_signal *signals;
};

enum imx7_src_registers {
	SRC_A7RCR0		= 0x0004,
	SRC_M4RCR		= 0x000c,
	SRC_ERCR		= 0x0014,
	SRC_HSICPHY_RCR		= 0x001c,
	SRC_USBOPHY1_RCR	= 0x0020,
	SRC_USBOPHY2_RCR	= 0x0024,
	SRC_MIPIPHY_RCR		= 0x0028,
	SRC_PCIEPHY_RCR		= 0x002c,
	SRC_DDRC_RCR		= 0x1000,
};

static int imx7_reset_update(struct imx7_src *imx7src,
			     unsigned long id, unsigned int value)
{
	const struct imx7_src_signal *signal = &imx7src->signals[id];

	return regmap_update_bits(imx7src->regmap,
				  signal->offset, signal->bit, value);
}

static const struct imx7_src_signal imx7_src_signals[IMX7_RESET_NUM] = {
	[IMX7_RESET_A7_CORE_POR_RESET0] = { SRC_A7RCR0, BIT(0) },
	[IMX7_RESET_A7_CORE_POR_RESET1] = { SRC_A7RCR0, BIT(1) },
	[IMX7_RESET_A7_CORE_RESET0]     = { SRC_A7RCR0, BIT(4) },
	[IMX7_RESET_A7_CORE_RESET1]	= { SRC_A7RCR0, BIT(5) },
	[IMX7_RESET_A7_DBG_RESET0]	= { SRC_A7RCR0, BIT(8) },
	[IMX7_RESET_A7_DBG_RESET1]	= { SRC_A7RCR0, BIT(9) },
	[IMX7_RESET_A7_ETM_RESET0]	= { SRC_A7RCR0, BIT(12) },
	[IMX7_RESET_A7_ETM_RESET1]	= { SRC_A7RCR0, BIT(13) },
	[IMX7_RESET_A7_SOC_DBG_RESET]	= { SRC_A7RCR0, BIT(20) },
	[IMX7_RESET_A7_L2RESET]		= { SRC_A7RCR0, BIT(21) },
	[IMX7_RESET_SW_M4C_RST]		= { SRC_M4RCR, BIT(1) },
	[IMX7_RESET_SW_M4P_RST]		= { SRC_M4RCR, BIT(2) },
	[IMX7_RESET_EIM_RST]		= { SRC_ERCR, BIT(0) },
	[IMX7_RESET_HSICPHY_PORT_RST]	= { SRC_HSICPHY_RCR, BIT(1) },
	[IMX7_RESET_USBPHY1_POR]	= { SRC_USBOPHY1_RCR, BIT(0) },
	[IMX7_RESET_USBPHY1_PORT_RST]	= { SRC_USBOPHY1_RCR, BIT(1) },
	[IMX7_RESET_USBPHY2_POR]	= { SRC_USBOPHY2_RCR, BIT(0) },
	[IMX7_RESET_USBPHY2_PORT_RST]	= { SRC_USBOPHY2_RCR, BIT(1) },
	[IMX7_RESET_MIPI_PHY_MRST]	= { SRC_MIPIPHY_RCR, BIT(1) },
	[IMX7_RESET_MIPI_PHY_SRST]	= { SRC_MIPIPHY_RCR, BIT(2) },
	[IMX7_RESET_PCIEPHY]		= { SRC_PCIEPHY_RCR, BIT(2) | BIT(1) },
	[IMX7_RESET_PCIEPHY_PERST]	= { SRC_PCIEPHY_RCR, BIT(3) },
	[IMX7_RESET_PCIE_CTRL_APPS_EN]	= { SRC_PCIEPHY_RCR, BIT(6) },
	[IMX7_RESET_PCIE_CTRL_APPS_TURNOFF] = { SRC_PCIEPHY_RCR, BIT(11) },
	[IMX7_RESET_DDRC_PRST]		= { SRC_DDRC_RCR, BIT(0) },
	[IMX7_RESET_DDRC_CORE_RST]	= { SRC_DDRC_RCR, BIT(1) },
};

static struct imx7_src *to_imx7_src(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct imx7_src, rcdev);
}

static int imx7_reset_set(struct reset_controller_dev *rcdev,
			  unsigned long id, bool assert)
{
	struct imx7_src *imx7src = to_imx7_src(rcdev);
	const unsigned int bit = imx7src->signals[id].bit;
	unsigned int value = assert ? bit : 0;

	switch (id) {
	case IMX7_RESET_PCIEPHY:
		/*
		 * wait for more than 10us to release phy g_rst and
		 * btnrst
		 */
		if (!assert)
			udelay(10);
		break;

	case IMX7_RESET_PCIE_CTRL_APPS_EN:
		value = assert ? 0 : bit;
		break;
	}

	return imx7_reset_update(imx7src, id, value);
}

static int imx7_reset_assert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	return imx7_reset_set(rcdev, id, true);
}

static int imx7_reset_deassert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return imx7_reset_set(rcdev, id, false);
}

static const struct imx7_src_variant variant_imx7 = {
	.signals = imx7_src_signals,
	.signals_num = ARRAY_SIZE(imx7_src_signals),
	.ops = {
		.assert   = imx7_reset_assert,
		.deassert = imx7_reset_deassert,
	},
};

enum imx8mq_src_registers {
	SRC_A53RCR0		= 0x0004,
	SRC_HDMI_RCR		= 0x0030,
	SRC_DISP_RCR		= 0x0034,
	SRC_GPU_RCR		= 0x0040,
	SRC_VPU_RCR		= 0x0044,
	SRC_PCIE2_RCR		= 0x0048,
	SRC_MIPIPHY1_RCR	= 0x004c,
	SRC_MIPIPHY2_RCR	= 0x0050,
	SRC_DDRC2_RCR		= 0x1004,
};

enum imx8mp_src_registers {
	SRC_SUPERMIX_RCR	= 0x0018,
	SRC_AUDIOMIX_RCR	= 0x001c,
	SRC_MLMIX_RCR		= 0x0028,
	SRC_GPU2D_RCR		= 0x0038,
	SRC_GPU3D_RCR		= 0x003c,
	SRC_VPU_G1_RCR		= 0x0048,
	SRC_VPU_G2_RCR		= 0x004c,
	SRC_VPUVC8KE_RCR	= 0x0050,
	SRC_NOC_RCR		= 0x0054,
};

static const struct imx7_src_signal imx8mq_src_signals[IMX8MQ_RESET_NUM] = {
	[IMX8MQ_RESET_A53_CORE_POR_RESET0]	= { SRC_A53RCR0, BIT(0) },
	[IMX8MQ_RESET_A53_CORE_POR_RESET1]	= { SRC_A53RCR0, BIT(1) },
	[IMX8MQ_RESET_A53_CORE_POR_RESET2]	= { SRC_A53RCR0, BIT(2) },
	[IMX8MQ_RESET_A53_CORE_POR_RESET3]	= { SRC_A53RCR0, BIT(3) },
	[IMX8MQ_RESET_A53_CORE_RESET0]		= { SRC_A53RCR0, BIT(4) },
	[IMX8MQ_RESET_A53_CORE_RESET1]		= { SRC_A53RCR0, BIT(5) },
	[IMX8MQ_RESET_A53_CORE_RESET2]		= { SRC_A53RCR0, BIT(6) },
	[IMX8MQ_RESET_A53_CORE_RESET3]		= { SRC_A53RCR0, BIT(7) },
	[IMX8MQ_RESET_A53_DBG_RESET0]		= { SRC_A53RCR0, BIT(8) },
	[IMX8MQ_RESET_A53_DBG_RESET1]		= { SRC_A53RCR0, BIT(9) },
	[IMX8MQ_RESET_A53_DBG_RESET2]		= { SRC_A53RCR0, BIT(10) },
	[IMX8MQ_RESET_A53_DBG_RESET3]		= { SRC_A53RCR0, BIT(11) },
	[IMX8MQ_RESET_A53_ETM_RESET0]		= { SRC_A53RCR0, BIT(12) },
	[IMX8MQ_RESET_A53_ETM_RESET1]		= { SRC_A53RCR0, BIT(13) },
	[IMX8MQ_RESET_A53_ETM_RESET2]		= { SRC_A53RCR0, BIT(14) },
	[IMX8MQ_RESET_A53_ETM_RESET3]		= { SRC_A53RCR0, BIT(15) },
	[IMX8MQ_RESET_A53_SOC_DBG_RESET]	= { SRC_A53RCR0, BIT(20) },
	[IMX8MQ_RESET_A53_L2RESET]		= { SRC_A53RCR0, BIT(21) },
	[IMX8MQ_RESET_SW_NON_SCLR_M4C_RST]	= { SRC_M4RCR, BIT(0) },
	[IMX8MQ_RESET_SW_M4C_RST]		= { SRC_M4RCR, BIT(1) },
	[IMX8MQ_RESET_SW_M4P_RST]		= { SRC_M4RCR, BIT(2) },
	[IMX8MQ_RESET_M4_ENABLE]		= { SRC_M4RCR, BIT(3) },
	[IMX8MQ_RESET_OTG1_PHY_RESET]		= { SRC_USBOPHY1_RCR, BIT(0) },
	[IMX8MQ_RESET_OTG2_PHY_RESET]		= { SRC_USBOPHY2_RCR, BIT(0) },
	[IMX8MQ_RESET_MIPI_DSI_RESET_BYTE_N]	= { SRC_MIPIPHY_RCR, BIT(1) },
	[IMX8MQ_RESET_MIPI_DSI_RESET_N]		= { SRC_MIPIPHY_RCR, BIT(2) },
	[IMX8MQ_RESET_MIPI_DSI_DPI_RESET_N]	= { SRC_MIPIPHY_RCR, BIT(3) },
	[IMX8MQ_RESET_MIPI_DSI_ESC_RESET_N]	= { SRC_MIPIPHY_RCR, BIT(4) },
	[IMX8MQ_RESET_MIPI_DSI_PCLK_RESET_N]	= { SRC_MIPIPHY_RCR, BIT(5) },
	[IMX8MQ_RESET_PCIEPHY]			= { SRC_PCIEPHY_RCR,
						    BIT(2) | BIT(1) },
	[IMX8MQ_RESET_PCIEPHY_PERST]		= { SRC_PCIEPHY_RCR, BIT(3) },
	[IMX8MQ_RESET_PCIE_CTRL_APPS_EN]	= { SRC_PCIEPHY_RCR, BIT(6) },
	[IMX8MQ_RESET_PCIE_CTRL_APPS_TURNOFF]	= { SRC_PCIEPHY_RCR, BIT(11) },
	[IMX8MQ_RESET_HDMI_PHY_APB_RESET]	= { SRC_HDMI_RCR, BIT(0) },
	[IMX8MQ_RESET_DISP_RESET]		= { SRC_DISP_RCR, BIT(0) },
	[IMX8MQ_RESET_GPU_RESET]		= { SRC_GPU_RCR, BIT(0) },
	[IMX8MQ_RESET_VPU_RESET]		= { SRC_VPU_RCR, BIT(0) },
	[IMX8MQ_RESET_PCIEPHY2]			= { SRC_PCIE2_RCR,
						    BIT(2) | BIT(1) },
	[IMX8MQ_RESET_PCIEPHY2_PERST]		= { SRC_PCIE2_RCR, BIT(3) },
	[IMX8MQ_RESET_PCIE2_CTRL_APPS_EN]	= { SRC_PCIE2_RCR, BIT(6) },
	[IMX8MQ_RESET_PCIE2_CTRL_APPS_TURNOFF]	= { SRC_PCIE2_RCR, BIT(11) },
	[IMX8MQ_RESET_MIPI_CSI1_CORE_RESET]	= { SRC_MIPIPHY1_RCR, BIT(0) },
	[IMX8MQ_RESET_MIPI_CSI1_PHY_REF_RESET]	= { SRC_MIPIPHY1_RCR, BIT(1) },
	[IMX8MQ_RESET_MIPI_CSI1_ESC_RESET]	= { SRC_MIPIPHY1_RCR, BIT(2) },
	[IMX8MQ_RESET_MIPI_CSI2_CORE_RESET]	= { SRC_MIPIPHY2_RCR, BIT(0) },
	[IMX8MQ_RESET_MIPI_CSI2_PHY_REF_RESET]	= { SRC_MIPIPHY2_RCR, BIT(1) },
	[IMX8MQ_RESET_MIPI_CSI2_ESC_RESET]	= { SRC_MIPIPHY2_RCR, BIT(2) },
	[IMX8MQ_RESET_DDRC1_PRST]		= { SRC_DDRC_RCR, BIT(0) },
	[IMX8MQ_RESET_DDRC1_CORE_RESET]		= { SRC_DDRC_RCR, BIT(1) },
	[IMX8MQ_RESET_DDRC1_PHY_RESET]		= { SRC_DDRC_RCR, BIT(2) },
	[IMX8MQ_RESET_DDRC2_PHY_RESET]		= { SRC_DDRC2_RCR, BIT(0) },
	[IMX8MQ_RESET_DDRC2_CORE_RESET]		= { SRC_DDRC2_RCR, BIT(1) },
	[IMX8MQ_RESET_DDRC2_PRST]		= { SRC_DDRC2_RCR, BIT(2) },
};

static int imx8mq_reset_set(struct reset_controller_dev *rcdev,
			    unsigned long id, bool assert)
{
	struct imx7_src *imx7src = to_imx7_src(rcdev);
	const unsigned int bit = imx7src->signals[id].bit;
	unsigned int value = assert ? bit : 0;

	switch (id) {
	case IMX8MQ_RESET_PCIEPHY:
	case IMX8MQ_RESET_PCIEPHY2:
		/*
		 * wait for more than 10us to release phy g_rst and
		 * btnrst
		 */
		if (!assert)
			udelay(10);
		break;

	case IMX8MQ_RESET_PCIE_CTRL_APPS_EN:
	case IMX8MQ_RESET_PCIE2_CTRL_APPS_EN:
	case IMX8MQ_RESET_MIPI_DSI_PCLK_RESET_N:
	case IMX8MQ_RESET_MIPI_DSI_ESC_RESET_N:
	case IMX8MQ_RESET_MIPI_DSI_DPI_RESET_N:
	case IMX8MQ_RESET_MIPI_DSI_RESET_N:
	case IMX8MQ_RESET_MIPI_DSI_RESET_BYTE_N:
	case IMX8MQ_RESET_M4_ENABLE:
		value = assert ? 0 : bit;
		break;
	}

	return imx7_reset_update(imx7src, id, value);
}

static int imx8mq_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return imx8mq_reset_set(rcdev, id, true);
}

static int imx8mq_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return imx8mq_reset_set(rcdev, id, false);
}

static const struct imx7_src_variant variant_imx8mq = {
	.signals = imx8mq_src_signals,
	.signals_num = ARRAY_SIZE(imx8mq_src_signals),
	.ops = {
		.assert   = imx8mq_reset_assert,
		.deassert = imx8mq_reset_deassert,
	},
};

static const struct imx7_src_signal imx8mp_src_signals[IMX8MP_RESET_NUM] = {
	[IMX8MP_RESET_A53_CORE_POR_RESET0]	= { SRC_A53RCR0, BIT(0) },
	[IMX8MP_RESET_A53_CORE_POR_RESET1]	= { SRC_A53RCR0, BIT(1) },
	[IMX8MP_RESET_A53_CORE_POR_RESET2]	= { SRC_A53RCR0, BIT(2) },
	[IMX8MP_RESET_A53_CORE_POR_RESET3]	= { SRC_A53RCR0, BIT(3) },
	[IMX8MP_RESET_A53_CORE_RESET0]		= { SRC_A53RCR0, BIT(4) },
	[IMX8MP_RESET_A53_CORE_RESET1]		= { SRC_A53RCR0, BIT(5) },
	[IMX8MP_RESET_A53_CORE_RESET2]		= { SRC_A53RCR0, BIT(6) },
	[IMX8MP_RESET_A53_CORE_RESET3]		= { SRC_A53RCR0, BIT(7) },
	[IMX8MP_RESET_A53_DBG_RESET0]		= { SRC_A53RCR0, BIT(8) },
	[IMX8MP_RESET_A53_DBG_RESET1]		= { SRC_A53RCR0, BIT(9) },
	[IMX8MP_RESET_A53_DBG_RESET2]		= { SRC_A53RCR0, BIT(10) },
	[IMX8MP_RESET_A53_DBG_RESET3]		= { SRC_A53RCR0, BIT(11) },
	[IMX8MP_RESET_A53_ETM_RESET0]		= { SRC_A53RCR0, BIT(12) },
	[IMX8MP_RESET_A53_ETM_RESET1]		= { SRC_A53RCR0, BIT(13) },
	[IMX8MP_RESET_A53_ETM_RESET2]		= { SRC_A53RCR0, BIT(14) },
	[IMX8MP_RESET_A53_ETM_RESET3]		= { SRC_A53RCR0, BIT(15) },
	[IMX8MP_RESET_A53_SOC_DBG_RESET]	= { SRC_A53RCR0, BIT(20) },
	[IMX8MP_RESET_A53_L2RESET]		= { SRC_A53RCR0, BIT(21) },
	[IMX8MP_RESET_SW_NON_SCLR_M7C_RST]	= { SRC_M4RCR, BIT(0) },
	[IMX8MP_RESET_OTG1_PHY_RESET]		= { SRC_USBOPHY1_RCR, BIT(0) },
	[IMX8MP_RESET_OTG2_PHY_RESET]		= { SRC_USBOPHY2_RCR, BIT(0) },
	[IMX8MP_RESET_SUPERMIX_RESET]		= { SRC_SUPERMIX_RCR, BIT(0) },
	[IMX8MP_RESET_AUDIOMIX_RESET]		= { SRC_AUDIOMIX_RCR, BIT(0) },
	[IMX8MP_RESET_MLMIX_RESET]		= { SRC_MLMIX_RCR, BIT(0) },
	[IMX8MP_RESET_PCIEPHY]			= { SRC_PCIEPHY_RCR, BIT(2) },
	[IMX8MP_RESET_PCIEPHY_PERST]		= { SRC_PCIEPHY_RCR, BIT(3) },
	[IMX8MP_RESET_PCIE_CTRL_APPS_EN]	= { SRC_PCIEPHY_RCR, BIT(6) },
	[IMX8MP_RESET_PCIE_CTRL_APPS_TURNOFF]	= { SRC_PCIEPHY_RCR, BIT(11) },
	[IMX8MP_RESET_HDMI_PHY_APB_RESET]	= { SRC_HDMI_RCR, BIT(0) },
	[IMX8MP_RESET_MEDIA_RESET]		= { SRC_DISP_RCR, BIT(0) },
	[IMX8MP_RESET_GPU2D_RESET]		= { SRC_GPU2D_RCR, BIT(0) },
	[IMX8MP_RESET_GPU3D_RESET]		= { SRC_GPU3D_RCR, BIT(0) },
	[IMX8MP_RESET_GPU_RESET]		= { SRC_GPU_RCR, BIT(0) },
	[IMX8MP_RESET_VPU_RESET]		= { SRC_VPU_RCR, BIT(0) },
	[IMX8MP_RESET_VPU_G1_RESET]		= { SRC_VPU_G1_RCR, BIT(0) },
	[IMX8MP_RESET_VPU_G2_RESET]		= { SRC_VPU_G2_RCR, BIT(0) },
	[IMX8MP_RESET_VPUVC8KE_RESET]		= { SRC_VPUVC8KE_RCR, BIT(0) },
	[IMX8MP_RESET_NOC_RESET]		= { SRC_NOC_RCR, BIT(0) },
};

static int imx8mp_reset_set(struct reset_controller_dev *rcdev,
			    unsigned long id, bool assert)
{
	struct imx7_src *imx7src = to_imx7_src(rcdev);
	const unsigned int bit = imx7src->signals[id].bit;
	unsigned int value = assert ? bit : 0;

	switch (id) {
	case IMX8MP_RESET_PCIEPHY:
		/*
		 * wait for more than 10us to release phy g_rst and
		 * btnrst
		 */
		if (!assert)
			udelay(10);
		break;

	case IMX8MP_RESET_PCIE_CTRL_APPS_EN:
	case IMX8MP_RESET_PCIEPHY_PERST:
		value = assert ? 0 : bit;
		break;
	}

	return imx7_reset_update(imx7src, id, value);
}

static int imx8mp_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return imx8mp_reset_set(rcdev, id, true);
}

static int imx8mp_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return imx8mp_reset_set(rcdev, id, false);
}

static const struct imx7_src_variant variant_imx8mp = {
	.signals = imx8mp_src_signals,
	.signals_num = ARRAY_SIZE(imx8mp_src_signals),
	.ops = {
		.assert   = imx8mp_reset_assert,
		.deassert = imx8mp_reset_deassert,
	},
};

static int imx7_reset_probe(struct platform_device *pdev)
{
	struct imx7_src *imx7src;
	struct device *dev = &pdev->dev;
	struct regmap_config config = { .name = "src" };
	const struct imx7_src_variant *variant = of_device_get_match_data(dev);

	imx7src = devm_kzalloc(dev, sizeof(*imx7src), GFP_KERNEL);
	if (!imx7src)
		return -ENOMEM;

	imx7src->signals = variant->signals;
	imx7src->regmap = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(imx7src->regmap)) {
		dev_err(dev, "Unable to get imx7-src regmap");
		return PTR_ERR(imx7src->regmap);
	}
	regmap_attach_dev(dev, imx7src->regmap, &config);

	imx7src->rcdev.owner     = THIS_MODULE;
	imx7src->rcdev.nr_resets = variant->signals_num;
	imx7src->rcdev.ops       = &variant->ops;
	imx7src->rcdev.of_node   = dev->of_node;

	return devm_reset_controller_register(dev, &imx7src->rcdev);
}

static const struct of_device_id imx7_reset_dt_ids[] = {
	{ .compatible = "fsl,imx7d-src", .data = &variant_imx7 },
	{ .compatible = "fsl,imx8mq-src", .data = &variant_imx8mq },
	{ .compatible = "fsl,imx8mp-src", .data = &variant_imx8mp },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx7_reset_dt_ids);

static struct platform_driver imx7_reset_driver = {
	.probe	= imx7_reset_probe,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= imx7_reset_dt_ids,
	},
};
module_platform_driver(imx7_reset_driver);

MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("NXP i.MX7 reset driver");
MODULE_LICENSE("GPL v2");
