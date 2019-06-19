// SPDX-License-Identifier: GPL-2.0-only
/*
 * Marvell PXA25x family pin control
 *
 * Copyright (C) 2016 Robert Jarzmik
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-pxa2xx.h"

static const struct pxa_desc_pin pxa25x_pins[] = {
	PXA_GPIO_ONLY_PIN(PXA_PINCTRL_PIN(0)),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(1),
		     PXA_FUNCTION(0, 1, "GP_RST")),
	PXA_GPIO_ONLY_PIN(PXA_PINCTRL_PIN(2)),
	PXA_GPIO_ONLY_PIN(PXA_PINCTRL_PIN(3)),
	PXA_GPIO_ONLY_PIN(PXA_PINCTRL_PIN(4)),
	PXA_GPIO_ONLY_PIN(PXA_PINCTRL_PIN(5)),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(6),
		     PXA_FUNCTION(1, 1, "MMCCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(7),
		     PXA_FUNCTION(1, 1, "48_MHz")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(8),
		     PXA_FUNCTION(1, 1, "MMCCS0")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(9),
		     PXA_FUNCTION(1, 1, "MMCCS1")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(10),
		     PXA_FUNCTION(1, 1, "RTCCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(11),
		     PXA_FUNCTION(1, 1, "3_6_MHz")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(12),
		     PXA_FUNCTION(1, 1, "32_kHz")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(13),
		     PXA_FUNCTION(1, 2, "MBGNT")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(14),
		     PXA_FUNCTION(0, 1, "MBREQ")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(15),
		     PXA_FUNCTION(1, 2, "nCS_1")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(16),
		     PXA_FUNCTION(1, 2, "PWM0")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(17),
		     PXA_FUNCTION(1, 2, "PWM1")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(18),
		     PXA_FUNCTION(0, 1, "RDY")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(19),
		     PXA_FUNCTION(0, 1, "DREQ[1]")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(20),
		     PXA_FUNCTION(0, 1, "DREQ[0]")),
	PXA_GPIO_ONLY_PIN(PXA_PINCTRL_PIN(21)),
	PXA_GPIO_ONLY_PIN(PXA_PINCTRL_PIN(22)),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(23),
		     PXA_FUNCTION(1, 2, "SCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(24),
		     PXA_FUNCTION(1, 2, "SFRM")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(25),
		     PXA_FUNCTION(1, 2, "TXD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(26),
		     PXA_FUNCTION(0, 1, "RXD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(27),
		     PXA_FUNCTION(0, 1, "EXTCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(28),
		     PXA_FUNCTION(0, 1, "BITCLK"),
		     PXA_FUNCTION(0, 2, "BITCLK"),
		     PXA_FUNCTION(1, 1, "BITCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(29),
		     PXA_FUNCTION(0, 1, "SDATA_IN0"),
		     PXA_FUNCTION(0, 2, "SDATA_IN")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(30),
		     PXA_FUNCTION(1, 1, "SDATA_OUT"),
		     PXA_FUNCTION(1, 2, "SDATA_OUT")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(31),
		     PXA_FUNCTION(1, 1, "SYNC"),
		     PXA_FUNCTION(1, 2, "SYNC")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(32),
		     PXA_FUNCTION(0, 1, "SDATA_IN1"),
		     PXA_FUNCTION(1, 1, "SYSCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(33),
		     PXA_FUNCTION(1, 2, "nCS[5]")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(34),
		     PXA_FUNCTION(0, 1, "FFRXD"),
		     PXA_FUNCTION(1, 2, "MMCCS0")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(35),
		     PXA_FUNCTION(0, 1, "CTS")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(36),
		     PXA_FUNCTION(0, 1, "DCD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(37),
		     PXA_FUNCTION(0, 1, "DSR")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(38),
		     PXA_FUNCTION(0, 1, "RI")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(39),
		     PXA_FUNCTION(1, 1, "MMCC1"),
		     PXA_FUNCTION(1, 2, "FFTXD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(40),
		     PXA_FUNCTION(1, 2, "DTR")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(41),
		     PXA_FUNCTION(1, 2, "RTS")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(42),
		     PXA_FUNCTION(0, 1, "BTRXD"),
		     PXA_FUNCTION(0, 3, "HWRXD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(43),
		     PXA_FUNCTION(1, 2, "BTTXD"),
		     PXA_FUNCTION(1, 3, "HWTXD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(44),
		     PXA_FUNCTION(0, 1, "BTCTS"),
		     PXA_FUNCTION(0, 3, "HWCTS")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(45),
		     PXA_FUNCTION(1, 2, "BTRTS"),
		     PXA_FUNCTION(1, 3, "HWRTS")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(46),
		     PXA_FUNCTION(0, 1, "ICP_RXD"),
		     PXA_FUNCTION(0, 2, "RXD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(47),
		     PXA_FUNCTION(1, 1, "TXD"),
		     PXA_FUNCTION(1, 2, "ICP_TXD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(48),
		     PXA_FUNCTION(1, 1, "HWTXD"),
		     PXA_FUNCTION(1, 2, "nPOE")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(49),
		     PXA_FUNCTION(0, 1, "HWRXD"),
		     PXA_FUNCTION(1, 2, "nPWE")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(50),
		     PXA_FUNCTION(0, 1, "HWCTS"),
		     PXA_FUNCTION(1, 2, "nPIOR")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(51),
		     PXA_FUNCTION(1, 1, "HWRTS"),
		     PXA_FUNCTION(1, 2, "nPIOW")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(52),
		     PXA_FUNCTION(1, 2, "nPCE[1]")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(53),
		     PXA_FUNCTION(1, 1, "MMCCLK"),
		     PXA_FUNCTION(1, 2, "nPCE[2]")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(54),
		     PXA_FUNCTION(1, 1, "MMCCLK"),
		     PXA_FUNCTION(1, 2, "nPSKTSEL")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(55),
		     PXA_FUNCTION(1, 2, "nPREG")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(56),
		     PXA_FUNCTION(0, 1, "nPWAIT")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(57),
		     PXA_FUNCTION(0, 1, "nIOIS16")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(58),
		     PXA_FUNCTION(1, 2, "LDD<0>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(59),
		     PXA_FUNCTION(1, 2, "LDD<1>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(60),
		     PXA_FUNCTION(1, 2, "LDD<2>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(61),
		     PXA_FUNCTION(1, 2, "LDD<3>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(62),
		     PXA_FUNCTION(1, 2, "LDD<4>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(63),
		     PXA_FUNCTION(1, 2, "LDD<5>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(64),
		     PXA_FUNCTION(1, 2, "LDD<6>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(65),
		     PXA_FUNCTION(1, 2, "LDD<7>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(66),
		     PXA_FUNCTION(0, 1, "MBREQ"),
		     PXA_FUNCTION(1, 2, "LDD<8>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(67),
		     PXA_FUNCTION(1, 1, "MMCCS0"),
		     PXA_FUNCTION(1, 2, "LDD<9>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(68),
		     PXA_FUNCTION(1, 1, "MMCCS1"),
		     PXA_FUNCTION(1, 2, "LDD<10>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(69),
		     PXA_FUNCTION(1, 1, "MMCCLK"),
		     PXA_FUNCTION(1, 2, "LDD<11>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(70),
		     PXA_FUNCTION(1, 1, "RTCCLK"),
		     PXA_FUNCTION(1, 2, "LDD<12>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(71),
		     PXA_FUNCTION(1, 1, "3_6_MHz"),
		     PXA_FUNCTION(1, 2, "LDD<13>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(72),
		     PXA_FUNCTION(1, 1, "32_kHz"),
		     PXA_FUNCTION(1, 2, "LDD<14>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(73),
		     PXA_FUNCTION(1, 1, "MBGNT"),
		     PXA_FUNCTION(1, 2, "LDD<15>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(74),
		     PXA_FUNCTION(1, 2, "LCD_FCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(75),
		     PXA_FUNCTION(1, 2, "LCD_LCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(76),
		     PXA_FUNCTION(1, 2, "LCD_PCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(77),
		     PXA_FUNCTION(1, 2, "LCD_ACBIAS")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(78),
		     PXA_FUNCTION(1, 2, "nCS<2>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(79),
		     PXA_FUNCTION(1, 2, "nCS<3>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(80),
		     PXA_FUNCTION(1, 2, "nCS<4>")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(81),
		     PXA_FUNCTION(0, 1, "NSSPSCLK"),
		     PXA_FUNCTION(1, 1, "NSSPSCLK")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(82),
		     PXA_FUNCTION(0, 1, "NSSPSFRM"),
		     PXA_FUNCTION(1, 1, "NSSPSFRM")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(83),
		     PXA_FUNCTION(0, 2, "NSSPRXD"),
		     PXA_FUNCTION(1, 1, "NSSPTXD")),
	PXA_GPIO_PIN(PXA_PINCTRL_PIN(84),
		     PXA_FUNCTION(0, 2, "NSSPRXD"),
		     PXA_FUNCTION(1, 1, "NSSPTXD")),
};

static int pxa25x_pinctrl_probe(struct platform_device *pdev)
{
	int ret, i;
	void __iomem *base_af[8];
	void __iomem *base_dir[4];
	void __iomem *base_sleep[4];
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base_af[0] = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base_af[0]))
		return PTR_ERR(base_af[0]);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	base_dir[0] = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base_dir[0]))
		return PTR_ERR(base_dir[0]);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	base_dir[3] = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base_dir[3]))
		return PTR_ERR(base_dir[3]);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	base_sleep[0] = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base_sleep[0]))
		return PTR_ERR(base_sleep[0]);

	for (i = 0; i < ARRAY_SIZE(base_af); i++)
		base_af[i] = base_af[0] + sizeof(base_af[0]) * i;
	for (i = 0; i < 3; i++)
		base_dir[i] = base_dir[0] + sizeof(base_dir[0]) * i;
	for (i = 0; i < ARRAY_SIZE(base_sleep); i++)
		base_sleep[i] = base_sleep[0] + sizeof(base_af[0]) * i;

	ret = pxa2xx_pinctrl_init(pdev, pxa25x_pins, ARRAY_SIZE(pxa25x_pins),
				  base_af, base_dir, base_sleep);
	return ret;
}

static const struct of_device_id pxa25x_pinctrl_match[] = {
	{ .compatible = "marvell,pxa25x-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, pxa25x_pinctrl_match);

static struct platform_driver pxa25x_pinctrl_driver = {
	.probe	= pxa25x_pinctrl_probe,
	.driver	= {
		.name		= "pxa25x-pinctrl",
		.of_match_table	= pxa25x_pinctrl_match,
	},
};
module_platform_driver(pxa25x_pinctrl_driver);

MODULE_AUTHOR("Robert Jarzmik <robert.jarzmik@free.fr>");
MODULE_DESCRIPTION("Marvell PXA25x pinctrl driver");
MODULE_LICENSE("GPL v2");
