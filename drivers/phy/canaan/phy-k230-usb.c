// SPDX-License-Identifier: GPL-2.0-only
/*
 * Canaan usb PHY driver
 *
 * Copyright (C) 2026 Jiayu Du <jiayu.riscv@isrc.iscas.ac.cn>
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define MAX_PHYS		2

/* Register offsets within the HiSysConfig system controller */
#define K230_USB0_TEST_REG_BASE     0x70
#define K230_USB0_CTL_REG_BASE      0xb0
#define K230_USB1_TEST_REG_BASE     0x90
#define K230_USB1_CTL_REG_BASE      0xb8

/* Relative offsets within each PHY's control/test block */
#define CTL0_OFFSET		0x00
#define CTL1_OFFSET		0x04
#define TEST_CTL3_OFFSET	0x0c

/* Bit definitions for TEST_CTL3 */
#define USB_IDPULLUP0		BIT(4)
#define USB_DMPULLDOWN0		BIT(8)
#define USB_DPPULLDOWN0		BIT(9)

/* USB control register 0 in HiSysConfig system controller */
/* PLL Integral Path Tune */
#define USB_CTL0_PLLITUNE_MASK		GENMASK(23, 22)

/* PLL Proportional Path Tune */
#define USB_CTL0_PLLPTUNE_MASK		GENMASK(21, 18)

/* PLL Bandwidth Adjustment */
#define USB_CTL0_PLLBTUNE_MASK		GENMASK(17, 17)

/* VReg18 Bypass Control */
#define USB_CTL0_VREGBYPASS_MASK	GENMASK(16, 16)

/* Retention Mode Enable */
#define USB_CTL0_RETENABLEN_MASK	GENMASK(15, 15)

/* Reserved Request Input */
#define USB_CTL0_RESREQIN_MASK		GENMASK(14, 14)

/* External VBUS Valid Select */
#define USB_CTL0_VBUSVLDEXTSEL0_MASK	GENMASK(13, 13)

/* OTG Block Disable Control */
#define USB_CTL0_OTGDISABLE0_MASK	GENMASK(12, 12)

/* Drive VBUS Enable */
#define USB_CTL0_DRVVBUS0_MASK		GENMASK(11, 11)

/* Autoresume Mode Enable */
#define USB_CTL0_AUTORSMENB0_MASK	GENMASK(10, 10)

/* HS Transceiver Asynchronous Control */
#define USB_CTL0_HSXCVREXTCTL0_MASK	GENMASK(9, 9)

/* USB 1.1 Transmit Data */
#define USB_CTL0_FSDATAEXT0_MASK	GENMASK(8, 8)

/* USB 1.1 SE0 Generation */
#define USB_CTL0_FSSE0EXT0_MASK		GENMASK(7, 7)

/* USB 1.1 Data Enable */
#define USB_CTL0_TXENABLEN0_MASK	GENMASK(6, 6)

/* Disconnect Threshold */
#define USB_CTL0_COMPDISTUNE0_MASK	GENMASK(5, 3)

/* Squelch Threshold */
#define USB_CTL0_SQRXTUNE0_MASK		GENMASK(2, 0)

/* USB control register 1 in HiSysConfig system controller */
/* Data Detect Voltage */
#define USB_CTL1_VDATREFTUNE0_MASK	GENMASK(23, 22)

/* VBUS Valid Threshold */
#define USB_CTL1_OTGTUNE0_MASK		GENMASK(21, 19)

/* Transmitter High-Speed Crossover */
#define USB_CTL1_TXHSXVTUNE0_MASK	GENMASK(18, 17)

/* FS/LS Source Impedance */
#define USB_CTL1_TXFSLSTUNE0_MASK	GENMASK(16, 13)

/* HS DC Voltage Level */
#define USB_CTL1_TXVREFTUNE0_MASK	GENMASK(12, 9)

/* HS Transmitter Rise/Fall Time */
#define USB_CTL1_TXRISETUNE0_MASK	GENMASK(8, 7)

/* USB Source Impedance */
#define USB_CTL1_TXRESTUNE0_MASK	GENMASK(6, 5)

/* HS Transmitter Pre-Emphasis Current Control */
#define USB_CTL1_TXPREEMPAMPTUNE0_MASK	GENMASK(4, 3)

/* HS Transmitter Pre-Emphasis Duration Control */
#define USB_CTL1_TXPREEMPPULSETUNE0_MASK	GENMASK(2, 2)

/* charging detection */
#define USB_CTL1_CHRGSRCPUENB0_MASK	GENMASK(1, 0)

#define K230_PHY_CTL0_VAL \
( \
	FIELD_PREP(USB_CTL0_PLLITUNE_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_PLLPTUNE_MASK, 0xc) | \
	FIELD_PREP(USB_CTL0_PLLBTUNE_MASK, 0x1) | \
	FIELD_PREP(USB_CTL0_VREGBYPASS_MASK, 0x1) | \
	FIELD_PREP(USB_CTL0_RETENABLEN_MASK, 0x1) | \
	FIELD_PREP(USB_CTL0_RESREQIN_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_VBUSVLDEXTSEL0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_OTGDISABLE0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_DRVVBUS0_MASK, 0x1) | \
	FIELD_PREP(USB_CTL0_AUTORSMENB0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_HSXCVREXTCTL0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_FSDATAEXT0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_FSSE0EXT0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_TXENABLEN0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL0_COMPDISTUNE0_MASK, 0x3) | \
	FIELD_PREP(USB_CTL0_SQRXTUNE0_MASK, 0x3) \
)

#define K230_PHY_CTL1_VAL \
( \
	FIELD_PREP(USB_CTL1_VDATREFTUNE0_MASK, 0x1) | \
	FIELD_PREP(USB_CTL1_OTGTUNE0_MASK, 0x3) | \
	FIELD_PREP(USB_CTL1_TXHSXVTUNE0_MASK, 0x3) | \
	FIELD_PREP(USB_CTL1_TXFSLSTUNE0_MASK, 0x3) | \
	FIELD_PREP(USB_CTL1_TXVREFTUNE0_MASK, 0x3) | \
	FIELD_PREP(USB_CTL1_TXRISETUNE0_MASK, 0x1) | \
	FIELD_PREP(USB_CTL1_TXRESTUNE0_MASK, 0x1) | \
	FIELD_PREP(USB_CTL1_TXPREEMPAMPTUNE0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL1_TXPREEMPPULSETUNE0_MASK, 0x0) | \
	FIELD_PREP(USB_CTL1_CHRGSRCPUENB0_MASK, 0x0) \
)

struct k230_usb_phy_instance {
	struct k230_usb_phy_global *global;
	struct phy *phy;
	u32 test_offset;
	u32 ctl_offset;
	int index;
};

struct k230_usb_phy_global {
	struct k230_usb_phy_instance phys[MAX_PHYS];
	void __iomem *base;
};

static int k230_usb_phy_power_on(struct phy *phy)
{
	struct k230_usb_phy_instance *inst = phy_get_drvdata(phy);
	struct k230_usb_phy_global *global = inst->global;
	void __iomem *base = global->base;
	u32 val;

	/* Apply recommended settings */
	writel(K230_PHY_CTL0_VAL, base + inst->ctl_offset + CTL0_OFFSET);
	writel(K230_PHY_CTL1_VAL, base + inst->ctl_offset + CTL1_OFFSET);

	/* Configure test register (pull-ups/pull-downs) */
	val = readl(base + inst->test_offset + TEST_CTL3_OFFSET);
	val |= USB_IDPULLUP0;

	if (inst->index == 1)
		val |= (USB_DMPULLDOWN0 | USB_DPPULLDOWN0);
	else
		val &= ~(USB_DMPULLDOWN0 | USB_DPPULLDOWN0);

	writel(val, base + inst->test_offset + TEST_CTL3_OFFSET);

	return 0;
}

static int k230_usb_phy_power_off(struct phy *phy)
{
	struct k230_usb_phy_instance *inst = phy_get_drvdata(phy);
	struct k230_usb_phy_global *global = inst->global;
	void __iomem *base = global->base;
	u32 val;

	val = readl(base + inst->test_offset + TEST_CTL3_OFFSET);
	val &= ~(USB_DMPULLDOWN0 | USB_DPPULLDOWN0);
	writel(val, base + inst->test_offset + TEST_CTL3_OFFSET);

	return 0;
}

static const struct phy_ops k230_usb_phy_ops = {
	.power_on = k230_usb_phy_power_on,
	.power_off = k230_usb_phy_power_off,
	.owner = THIS_MODULE,
};

static struct phy *k230_usb_phy_xlate(struct device *dev,
				      const struct of_phandle_args *args)
{
	struct k230_usb_phy_global *global = dev_get_drvdata(dev);
	unsigned int idx = args->args[0];

	if (idx >= MAX_PHYS)
		return ERR_PTR(-EINVAL);

	return global->phys[idx].phy;
}

static int k230_usb_phy_probe(struct platform_device *pdev)
{
	struct k230_usb_phy_global *global;
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	int i;

	global = devm_kzalloc(dev, sizeof(*global), GFP_KERNEL);
	if (!global)
		return -ENOMEM;
	dev_set_drvdata(dev, global);

	global->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(global->base))
		return dev_err_probe(dev, PTR_ERR(global->base),
				     "failed to map registers\n");

	static const struct {
		u32 test_offset;
		u32 ctl_offset;
	} phy_reg_info[MAX_PHYS] = {
		[0] = { K230_USB0_TEST_REG_BASE, K230_USB0_CTL_REG_BASE },
		[1] = { K230_USB1_TEST_REG_BASE, K230_USB1_CTL_REG_BASE },
	};

	for (i = 0; i < MAX_PHYS; i++) {
		struct k230_usb_phy_instance *inst = &global->phys[i];
		struct phy *phy;

		inst->global = global;
		inst->index = i;
		inst->test_offset = phy_reg_info[i].test_offset;
		inst->ctl_offset  = phy_reg_info[i].ctl_offset;

		phy = devm_phy_create(dev, NULL, &k230_usb_phy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create phy%d\n", i);
			return PTR_ERR(phy);
		}

		phy_set_drvdata(phy, inst);
		inst->phy = phy;
	}

	provider = devm_of_phy_provider_register(dev, k230_usb_phy_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	return 0;
}

static const struct of_device_id k230_usb_phy_of_match[] = {
	{ .compatible = "canaan,k230-usb-phy" },
	{}
};
MODULE_DEVICE_TABLE(of, k230_usb_phy_of_match);

static struct platform_driver k230_usb_phy_driver = {
	.probe = k230_usb_phy_probe,
	.driver = {
		.name = "k230-usb-phy",
		.of_match_table = k230_usb_phy_of_match,
	},
};
module_platform_driver(k230_usb_phy_driver);

MODULE_DESCRIPTION("Canaan Kendryte K230 USB 2.0 PHY driver");
MODULE_AUTHOR("Jiayu Du <jiayu.riscv@isrc.iscas.ac.cn>");
MODULE_LICENSE("GPL");
