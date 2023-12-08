// SPDX-License-Identifier: (GPL-2.0)
/*
 * oxnas SoC reset driver
 * based on:
 * Microsemi MIPS SoC reset driver
 * and ox820_assert_system_reset() written by Ma Hajun <mahaijuns@gmail.com>
 *
 * Copyright (c) 2013 Ma Hajun <mahaijuns@gmail.com>
 * Copyright (c) 2017 Microsemi Corporation
 * Copyright (c) 2020 Daniel Golle <daniel@makrotopia.org>
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

/* bit numbers of reset control register */
#define OX820_SYS_CTRL_RST_SCU                0
#define OX820_SYS_CTRL_RST_COPRO              1
#define OX820_SYS_CTRL_RST_ARM0               2
#define OX820_SYS_CTRL_RST_ARM1               3
#define OX820_SYS_CTRL_RST_USBHS              4
#define OX820_SYS_CTRL_RST_USBHSPHYA          5
#define OX820_SYS_CTRL_RST_MACA               6
#define OX820_SYS_CTRL_RST_MAC                OX820_SYS_CTRL_RST_MACA
#define OX820_SYS_CTRL_RST_PCIEA              7
#define OX820_SYS_CTRL_RST_SGDMA              8
#define OX820_SYS_CTRL_RST_CIPHER             9
#define OX820_SYS_CTRL_RST_DDR                10
#define OX820_SYS_CTRL_RST_SATA               11
#define OX820_SYS_CTRL_RST_SATA_LINK          12
#define OX820_SYS_CTRL_RST_SATA_PHY           13
#define OX820_SYS_CTRL_RST_PCIEPHY            14
#define OX820_SYS_CTRL_RST_STATIC             15
#define OX820_SYS_CTRL_RST_GPIO               16
#define OX820_SYS_CTRL_RST_UART1              17
#define OX820_SYS_CTRL_RST_UART2              18
#define OX820_SYS_CTRL_RST_MISC               19
#define OX820_SYS_CTRL_RST_I2S                20
#define OX820_SYS_CTRL_RST_SD                 21
#define OX820_SYS_CTRL_RST_MACB               22
#define OX820_SYS_CTRL_RST_PCIEB              23
#define OX820_SYS_CTRL_RST_VIDEO              24
#define OX820_SYS_CTRL_RST_DDR_PHY            25
#define OX820_SYS_CTRL_RST_USBHSPHYB          26
#define OX820_SYS_CTRL_RST_USBDEV             27
#define OX820_SYS_CTRL_RST_ARMDBG             29
#define OX820_SYS_CTRL_RST_PLLA               30
#define OX820_SYS_CTRL_RST_PLLB               31

/* bit numbers of clock control register */
#define OX820_SYS_CTRL_CLK_COPRO              0
#define OX820_SYS_CTRL_CLK_DMA                1
#define OX820_SYS_CTRL_CLK_CIPHER             2
#define OX820_SYS_CTRL_CLK_SD                 3
#define OX820_SYS_CTRL_CLK_SATA               4
#define OX820_SYS_CTRL_CLK_I2S                5
#define OX820_SYS_CTRL_CLK_USBHS              6
#define OX820_SYS_CTRL_CLK_MACA               7
#define OX820_SYS_CTRL_CLK_MAC                OX820_SYS_CTRL_CLK_MACA
#define OX820_SYS_CTRL_CLK_PCIEA              8
#define OX820_SYS_CTRL_CLK_STATIC             9
#define OX820_SYS_CTRL_CLK_MACB               10
#define OX820_SYS_CTRL_CLK_PCIEB              11
#define OX820_SYS_CTRL_CLK_REF600             12
#define OX820_SYS_CTRL_CLK_USBDEV             13
#define OX820_SYS_CTRL_CLK_DDR                14
#define OX820_SYS_CTRL_CLK_DDRPHY             15
#define OX820_SYS_CTRL_CLK_DDRCK              16

/* Regmap offsets */
#define OX820_CLK_SET_REGOFFSET               0x2c
#define OX820_CLK_CLR_REGOFFSET               0x30
#define OX820_RST_SET_REGOFFSET               0x34
#define OX820_RST_CLR_REGOFFSET               0x38
#define OX820_SECONDARY_SEL_REGOFFSET         0x14
#define OX820_TERTIARY_SEL_REGOFFSET          0x8c
#define OX820_QUATERNARY_SEL_REGOFFSET        0x94
#define OX820_DEBUG_SEL_REGOFFSET             0x9c
#define OX820_ALTERNATIVE_SEL_REGOFFSET       0xa4
#define OX820_PULLUP_SEL_REGOFFSET            0xac
#define OX820_SEC_SECONDARY_SEL_REGOFFSET     0x100014
#define OX820_SEC_TERTIARY_SEL_REGOFFSET      0x10008c
#define OX820_SEC_QUATERNARY_SEL_REGOFFSET    0x100094
#define OX820_SEC_DEBUG_SEL_REGOFFSET         0x10009c
#define OX820_SEC_ALTERNATIVE_SEL_REGOFFSET   0x1000a4
#define OX820_SEC_PULLUP_SEL_REGOFFSET        0x1000ac

struct oxnas_restart_context {
	struct regmap *sys_ctrl;
	struct notifier_block restart_handler;
};

static int ox820_restart_handle(struct notifier_block *this,
				 unsigned long mode, void *cmd)
{
	struct oxnas_restart_context *ctx = container_of(this, struct
							oxnas_restart_context,
							restart_handler);
	u32 value;

	/*
	 * Assert reset to cores as per power on defaults
	 * Don't touch the DDR interface as things will come to an impromptu
	 * stop NB Possibly should be asserting reset for PLLB, but there are
	 * timing concerns here according to the docs
	 */
	value = BIT(OX820_SYS_CTRL_RST_COPRO)		|
		BIT(OX820_SYS_CTRL_RST_USBHS)		|
		BIT(OX820_SYS_CTRL_RST_USBHSPHYA)	|
		BIT(OX820_SYS_CTRL_RST_MACA)		|
		BIT(OX820_SYS_CTRL_RST_PCIEA)		|
		BIT(OX820_SYS_CTRL_RST_SGDMA)		|
		BIT(OX820_SYS_CTRL_RST_CIPHER)		|
		BIT(OX820_SYS_CTRL_RST_SATA)		|
		BIT(OX820_SYS_CTRL_RST_SATA_LINK)	|
		BIT(OX820_SYS_CTRL_RST_SATA_PHY)	|
		BIT(OX820_SYS_CTRL_RST_PCIEPHY)		|
		BIT(OX820_SYS_CTRL_RST_STATIC)		|
		BIT(OX820_SYS_CTRL_RST_UART1)		|
		BIT(OX820_SYS_CTRL_RST_UART2)		|
		BIT(OX820_SYS_CTRL_RST_MISC)		|
		BIT(OX820_SYS_CTRL_RST_I2S)		|
		BIT(OX820_SYS_CTRL_RST_SD)		|
		BIT(OX820_SYS_CTRL_RST_MACB)		|
		BIT(OX820_SYS_CTRL_RST_PCIEB)		|
		BIT(OX820_SYS_CTRL_RST_VIDEO)		|
		BIT(OX820_SYS_CTRL_RST_USBHSPHYB)	|
		BIT(OX820_SYS_CTRL_RST_USBDEV);

	regmap_write(ctx->sys_ctrl, OX820_RST_SET_REGOFFSET, value);

	/* Release reset to cores as per power on defaults */
	regmap_write(ctx->sys_ctrl, OX820_RST_CLR_REGOFFSET,
			BIT(OX820_SYS_CTRL_RST_GPIO));

	/*
	 * Disable clocks to cores as per power-on defaults - must leave DDR
	 * related clocks enabled otherwise we'll stop rather abruptly.
	 */
	value = BIT(OX820_SYS_CTRL_CLK_COPRO)		|
		BIT(OX820_SYS_CTRL_CLK_DMA)		|
		BIT(OX820_SYS_CTRL_CLK_CIPHER)		|
		BIT(OX820_SYS_CTRL_CLK_SD)		|
		BIT(OX820_SYS_CTRL_CLK_SATA)		|
		BIT(OX820_SYS_CTRL_CLK_I2S)		|
		BIT(OX820_SYS_CTRL_CLK_USBHS)		|
		BIT(OX820_SYS_CTRL_CLK_MAC)		|
		BIT(OX820_SYS_CTRL_CLK_PCIEA)		|
		BIT(OX820_SYS_CTRL_CLK_STATIC)		|
		BIT(OX820_SYS_CTRL_CLK_MACB)		|
		BIT(OX820_SYS_CTRL_CLK_PCIEB)		|
		BIT(OX820_SYS_CTRL_CLK_REF600)		|
		BIT(OX820_SYS_CTRL_CLK_USBDEV);

	regmap_write(ctx->sys_ctrl, OX820_CLK_CLR_REGOFFSET, value);

	/* Enable clocks to cores as per power-on defaults */

	/* Set sys-control pin mux'ing as per power-on defaults */
	regmap_write(ctx->sys_ctrl, OX820_SECONDARY_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_TERTIARY_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_QUATERNARY_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_DEBUG_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_ALTERNATIVE_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_PULLUP_SEL_REGOFFSET, 0);

	regmap_write(ctx->sys_ctrl, OX820_SEC_SECONDARY_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_SEC_TERTIARY_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_SEC_QUATERNARY_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_SEC_DEBUG_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_SEC_ALTERNATIVE_SEL_REGOFFSET, 0);
	regmap_write(ctx->sys_ctrl, OX820_SEC_PULLUP_SEL_REGOFFSET, 0);

	/*
	 * No need to save any state, as the ROM loader can determine whether
	 * reset is due to power cycling or programatic action, just hit the
	 * (self-clearing) CPU reset bit of the block reset register
	 */
	value =
		BIT(OX820_SYS_CTRL_RST_SCU) |
		BIT(OX820_SYS_CTRL_RST_ARM0) |
		BIT(OX820_SYS_CTRL_RST_ARM1);

	regmap_write(ctx->sys_ctrl, OX820_RST_SET_REGOFFSET, value);

	pr_emerg("Unable to restart system\n");
	return NOTIFY_DONE;
}

static int ox820_restart_probe(struct platform_device *pdev)
{
	struct oxnas_restart_context *ctx;
	struct regmap *sys_ctrl;
	struct device *dev = &pdev->dev;
	int err = 0;

	sys_ctrl = syscon_node_to_regmap(pdev->dev.of_node);
	if (IS_ERR(sys_ctrl))
		return PTR_ERR(sys_ctrl);

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->sys_ctrl = sys_ctrl;
	ctx->restart_handler.notifier_call = ox820_restart_handle;
	ctx->restart_handler.priority = 192;
	err = register_restart_handler(&ctx->restart_handler);
	if (err)
		dev_err(dev, "can't register restart notifier (err=%d)\n", err);

	return err;
}

static const struct of_device_id ox820_restart_of_match[] = {
	{ .compatible = "oxsemi,ox820-sys-ctrl" },
	{}
};

static struct platform_driver ox820_restart_driver = {
	.probe = ox820_restart_probe,
	.driver = {
		.name = "ox820-chip-reset",
		.of_match_table = ox820_restart_of_match,
	},
};
builtin_platform_driver(ox820_restart_driver);
