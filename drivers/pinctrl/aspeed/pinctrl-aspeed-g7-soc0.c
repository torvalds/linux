// SPDX-License-Identifier: GPL-2.0

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define SCU00 0x00 /* Multi-function Pin Control #1  */
#define SCU04 0x04 /* Multi-function Pin Control #2  */
#define SCU08 0x08 /* Multi-function Pin Control #3  */
#define SCU0C 0x0C /* Multi-function Pin Control #3  */
#define SCU10 0x10 /* USB Multi-function Control Register  */
#define SCU14 0x14 /* VGA Function Control Register  */

#define SCU80 0x80 /* GPIO18D0 IO Control Register */
#define SCU84 0x84 /* GPIO18D1 IO Control Register */
#define SCU88 0x88 /* GPIO18D2 IO Control Register */
#define SCU8C 0x8c /* GPIO18D3 IO Control Register */
#define SCU90 0x90 /* GPIO18D4 IO Control Register */
#define SCU94 0x94 /* GPIO18D5 IO Control Register */
#define SCU98 0x98 /* GPIO18D6 IO Control Register */
#define SCU9C 0x9c /* GPIO18D7 IO Control Register */

struct aspeed_g7_soc0_pinctrl {
	struct pinctrl_dev *pctldev;
	struct device *dev;
	struct irq_domain *domain;
	void __iomem *regs;
};

static const int emmcg1_pins[] = { 0, 1, 2, 3, 6, 7 };
static const int emmcg4_pins[] = { 0, 1, 4, 3, 6, 7, 4, 5 };
static const int emmcg8_pins[] = { 0, 1, 4, 3, 6, 7, 4, 5, 8, 9, 10, 11 };
static const int vgdddc_pins[] = { 10, 11 };

struct aspeed_g7_group_funcfg {
	u32 reg;
	u32 mask;
	bool enable;
	u32 val;
};

/*
 * pin:	     name, number
 * group:    name, npins,   pins
 * function: name, ngroups, groups
 */
struct aspeed_g7_soc0_pingroup {
	const char *name;
	const char *fn_name;
	const unsigned int *pins;
	int npins;
	struct aspeed_g7_group_funcfg *groupcfg;
	int groupcfg_nums;
};

/* USB3A */
/* 00: BMC XHCI to vHub2, [9] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb3axhd_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(1, 0), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 1, .val = BIT(9) },
};

/* 00: PCI XHCI to vHub2, [9] 0: PCIe XHCI */
struct aspeed_g7_group_funcfg usb3axhpd_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(1, 0), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 0, .val = 0 },
};

/* 01: vHub2 to PHY */
struct aspeed_g7_group_funcfg usb3ad_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(1, 0), .enable = 1, .val = BIT(0) },
};

/* 10: BMC XHCI to PHY, [9] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb3axh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(1, 0), .enable = 1, .val = BIT(1) },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 1, .val = BIT(9) },
};

/* 10: PCI XHCI to PHY, [9] 0: PCIe XHCI */
struct aspeed_g7_group_funcfg usb3axhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(1, 0), .enable = 1, .val = BIT(1) },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 0, .val = 0 },
};

/* 11: XHCI Ext (port B), [10] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb3a2bxh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(1, 0), .enable = 1, .val = GENMASK(1, 0) },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 1, .val = BIT(10) }
};

/* 11: XHCI Ext (port B), [10] 0: PCI XHCI */
struct aspeed_g7_group_funcfg usb3a2bxhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(1, 0), .enable = 1, .val = GENMASK(1, 0) },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 0, .val = 0 }
};

//USB2A
/* 00: xhci to vHub1, [9] : 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb2axhd1_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 1, .val = BIT(9) },
};

/* 00: pci xhci to vHub1, [9] : 0 PCIe XHCI */
struct aspeed_g7_group_funcfg usb2axhpd1_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 0, .val = 0 },
};

/* 01: vHub1 to PHY */
struct aspeed_g7_group_funcfg usb2ad1_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 1, .val = BIT(2) },
};

/* 10: BMC xhci to PHY, [9] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb2axh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 1, .val = BIT(3) },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 1, .val = BIT(9) },
};

/* 10: PCI xhci to PHY, ,[9] : 0 PCIe XHCI */
struct aspeed_g7_group_funcfg usb2axhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 1, .val = BIT(3) },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 0, .val = 0 },
};

/* 11: XHCI Ext (port B), [10] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb2a2bxh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 1, .val = BIT(10) }
};

/* 11: XHCI Ext (port B), [10] 0: PCI XHCI */
struct aspeed_g7_group_funcfg usb2a2bxhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 0, .val = 0 }
};

//USB2A
/* 00: PCIE EHCI to vHub0 */
struct aspeed_g7_group_funcfg usb2ahpd0_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 0, .val = 0 },
};

/* 01: vHub0 to PHY */
struct aspeed_g7_group_funcfg usb2ad0_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 1, .val = BIT(24) },
};

/* 10: BMC EHCI to PHY */
struct aspeed_g7_group_funcfg usb2ah_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 1, .val = BIT(25) },
};

/* 11: PCI EHCI to PHY */
struct aspeed_g7_group_funcfg usb2ahp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(3, 2), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(25, 24), .enable = 1, .val = GENMASK(25, 24) },
};

/* USB3B */
/* 00: BMC XHCI to vHub2, [9] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb3bxhd_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(5, 4), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 1, .val = BIT(10) },
};

/* 00: PCI XHCI to vHub2, [9] 0: PCIe XHCI */
struct aspeed_g7_group_funcfg usb3bxhpd_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(5, 4), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 0, .val = 0 },
};

/* 01: vHub2 to PHY */
struct aspeed_g7_group_funcfg usb3bd_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(5, 4), .enable = 1, .val = BIT(4) },
};

/* 10: BMC XHCI to PHY, [9] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb3bxh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(5, 4), .enable = 1, .val = BIT(5) },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 1, .val = BIT(10) },
};

/* 10: PCI XHCI to PHY, [9] 0: PCIe XHCI */
struct aspeed_g7_group_funcfg usb3bxhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(5, 4), .enable = 1, .val = BIT(5) },
	{ .reg = SCU10, .mask = BIT_MASK(9), .enable = 0, .val = 0 },
};

/* 11: XHCI Ext (port B), [10] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb3b2axh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(5, 4), .enable = 1, .val = GENMASK(5, 4) },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 1, .val = BIT(10) }
};

/* 11: XHCI Ext (port B), [10] 0: PCI XHCI */
struct aspeed_g7_group_funcfg usb3b2axhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(5, 4), .enable = 1, .val = GENMASK(5, 4) },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 0, .val = 0 }
};

//USB2B
/* 00: xhci to vHub1, [10] : 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb2bxhd1_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 1, .val = BIT(10) },
};

/* 00: pci xhci to vHub1, [10] : 0 PCIe XHCI */
struct aspeed_g7_group_funcfg usb2bxhpd1_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 0, .val = 0 },
};

/* 01: vHub1 to PHY */
struct aspeed_g7_group_funcfg usb2bd1_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(29, 28), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 1, .val = BIT(6) },
};

/* 10: BMC xhci to PHY, [10] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb2bxh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(29, 28), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 1, .val = BIT(7) },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 1, .val = BIT(10) },
};

/* 10: PCI xhci to PHY, ,[10] : 0 PCIe XHCI */
struct aspeed_g7_group_funcfg usb2bxhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(29, 28), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 1, .val = BIT(7) },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 0, .val = 0 },
};

/* 11: XHCI Ext (port B), [10] 1: BMC XHCI */
struct aspeed_g7_group_funcfg usb2b2axh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(26, 26), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 1, .val = BIT(10) }
};

/* 11: XHCI Ext (port B), [10] 0: PCI XHCI */
struct aspeed_g7_group_funcfg usb2b2axhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(29, 28), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = BIT_MASK(10), .enable = 0, .val = 0 }
};

//USB2B
/* 00: PCIE EHCI to vHub0 */
struct aspeed_g7_group_funcfg usb2bhpd0_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(29, 28), .enable = 0, .val = 0 },
};

/* 01: vHub0 to PHY */
struct aspeed_g7_group_funcfg usb2bd0_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(29, 28), .enable = 1, .val = BIT(28) },
};

/* 10: BMC EHCI to PHY */
struct aspeed_g7_group_funcfg usb2bh_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(29, 28), .enable = 1, .val = BIT(29) },
};

/* 11: PCI EHCI to PHY */
struct aspeed_g7_group_funcfg usb2bhp_cfg[] = {
	{ .reg = SCU10, .mask = GENMASK(7, 6), .enable = 0, .val = 0 },
	{ .reg = SCU10, .mask = GENMASK(29, 28), .enable = 1, .val = GENMASK(29, 28) },
};

#define PINGROUP(pingrp_name, grp_fn_name, n) \
		{ .name = #pingrp_name, .fn_name = #grp_fn_name, \
		  .pins = n##_pins, .npins = ARRAY_SIZE(n##_pins)}

#define GROUPCFG(pingrp_name, grp_fn_name, n) \
		{ .name = #pingrp_name, .fn_name = #grp_fn_name, \
		  .groupcfg = n##_cfg, .groupcfg_nums = ARRAY_SIZE(n##_cfg)}

static struct aspeed_g7_soc0_pingroup aspeed_g7_soc0_pingroups[] = {
	PINGROUP(EMMCG1, EMMC, emmcg1),
	PINGROUP(EMMCG4, EMMC, emmcg4),
	PINGROUP(EMMCG8, EMMC, emmcg8),
	PINGROUP(VGADDC, VGADDC, vgdddc),
//USBA
	GROUPCFG(USB3AXHD, USB3A, usb3axhd),
	GROUPCFG(USB3AXHPD, USB3A, usb3axhpd),
	GROUPCFG(USB3AD, USB3A, usb3ad),
	GROUPCFG(USB3AXH, USB3A, usb3axh),
	GROUPCFG(USB3AXHP, USB3A, usb3axhp),
	GROUPCFG(USB3A2BH, USB3A, usb3a2bxh),
	GROUPCFG(USB3A2BHP, USB3A, usb3a2bxhp),
	GROUPCFG(USB2AXD1, USB3A, usb2axhd1),
	GROUPCFG(USB2AXHPD1, USB3A, usb2axhpd1),
	GROUPCFG(USB2AD1, USB3A, usb2ad1),
	GROUPCFG(USB2AXH, USB3A, usb2axh),
	GROUPCFG(USB2AXHP, USB3A, usb2axhp),
	GROUPCFG(USB2A2BXH, USB3A, usb2a2bxh),
	GROUPCFG(USB2A2BXHP, USB3A, usb2a2bxhp),
	GROUPCFG(USB2AHPD0, USB3A, usb2ahpd0),
	GROUPCFG(USB2AD0, USB3A, usb2ad0),
	GROUPCFG(USB2AH, USB3A, usb2ah),
	GROUPCFG(USB2AHP, USB3A, usb2ahp),
//USBB
	GROUPCFG(USB3BXHD, USB3A, usb3bxhd),
	GROUPCFG(USB3BXHPD, USB3A, usb3bxhpd),
	GROUPCFG(USB3BD, USB3A, usb3bd),
	GROUPCFG(USB3BXH, USB3A, usb3bxh),
	GROUPCFG(USB3BXHP, USB3A, usb3bxhp),
	GROUPCFG(USB3B2AH, USB3A, usb3b2axh),
	GROUPCFG(USB3B2AHP, USB3A, usb3b2axhp),
	GROUPCFG(USB2BXD1, USB3A, usb2bxhd1),
	GROUPCFG(USB2BXHPD1, USB3A, usb2bxhpd1),
	GROUPCFG(USB2BD1, USB3A, usb2bd1),
	GROUPCFG(USB2BXH, USB3A, usb2bxh),
	GROUPCFG(USB2BXHP, USB3A, usb2bxhp),
	GROUPCFG(USB2B2AXH, USB3A, usb2b2axh),
	GROUPCFG(USB2B2AXHP, USB3A, usb2b2axhp),
	GROUPCFG(USB2BHPD0, USB3A, usb2bhpd0),
	GROUPCFG(USB2BD0, USB3A, usb2bd0),
	GROUPCFG(USB2BH, USB3A, usb2bh),
	GROUPCFG(USB2BHP, USB3A, usb2bhp),
};

static const char *const emmc_grp[] = { "EMMCG1", "EMMCG4", "EMMCG8" };
static const char *const vgaddc_grp[] = { "VGADDC" };
static const char *const usb3a_grp[] = { "USB3AXHD", "USB3AXHPD", "USB3AD", "USB3AXH",
					 "USB3AXHP", "USB3A2BH", "USB3A2BHP" };
static const char *const usb2a_grp[] = { "USB2AXHD1", "USB2AXHPD1", "USB2AD1", "USB2AXH",
					 "USB2AXHP", "USB2A2BH", "USB2A2BHP",
					 "USB2AHPD0", "USB2AD0", "USB2AH", "USB2AHP" };
static const char *const usb3b_grp[] = { "USB3BXHD", "USB3BXHPD", "USB3BD", "USB3BXH",
					 "USB3BXHP", "USB3B2AH", "USB3B2AHP" };
static const char *const usb2b_grp[] = { "USB2BXHD1", "USB2BXHPD1", "USB2BD1", "USB2BXH",
					 "USB2BXHP", "USB2B2AH", "USB2B2AHP",
					 "USB2BHPD0", "USB2BD0", "USB2BH", "USB2BHP" };

struct aspeed_g7_soc0_func {
	const char *name;
	const unsigned int ngroups;
	const char *const *groups;
};

static struct aspeed_g7_soc0_func aspeed_g7_soc0_funcs[] = {
	{ .name = "EMMC",	.ngroups = ARRAY_SIZE(emmc_grp),	.groups = emmc_grp	},
	{ .name = "VGADDC",	.ngroups = ARRAY_SIZE(vgaddc_grp),	.groups = vgaddc_grp	},
	{ .name = "USB3A",	.ngroups = ARRAY_SIZE(usb3a_grp),	.groups = usb3a_grp	},
	{ .name = "USB2A",	.ngroups = ARRAY_SIZE(usb2a_grp),	.groups = usb2a_grp	},
	{ .name = "USB3B",	.ngroups = ARRAY_SIZE(usb3b_grp),	.groups = usb3b_grp	},
	{ .name = "USB2B",	.ngroups = ARRAY_SIZE(usb2b_grp),	.groups = usb2b_grp	},
};

/* number, name, drv_data */
static const struct pinctrl_pin_desc aspeed_g7_soc0_pins[] = {
	PINCTRL_PIN(0, "EMMCCLK_VB1CSN_GPIO18A0"),
	PINCTRL_PIN(1, "EMMCCMD_VB1CK_GPIO18A1"),
	PINCTRL_PIN(2, "EMMCDAT0_VB1MOSI_GPIO18A2"),
	PINCTRL_PIN(3, "EMMCDAT1_VB1MISO_GPIO18A3"),
	PINCTRL_PIN(4, "EMMCDAT2_GPIO18A4"),
	PINCTRL_PIN(5, "EMMCDAT3_GPIO18A5"),
	PINCTRL_PIN(6, "EMMCCDN_VB0CSN_GPIO18A6"),
	PINCTRL_PIN(7, "EMMCWPN_VB0CK_GPIO18A7"),
	PINCTRL_PIN(8, "EMMCDAT4_VB0MOSI_GPIO18B0"),
	PINCTRL_PIN(9, "EMMCDAT5_VB0MISO_GPIO18B1"),
	PINCTRL_PIN(10, "EMMCDAT6_DDCCLK_GPIO18B2"),
	PINCTRL_PIN(11, "EMMCDAT7_DDCDAT_GPIO18B3"),
};

struct aspeed_g7_soc0_funcfg {
	char *fn_name;
	u32 reg;
	u32 mask;
	int bit;
};

struct aspeed_g7_soc0_pincfg {
	struct aspeed_g7_soc0_funcfg *funcfg;
};

static const struct aspeed_g7_soc0_pincfg pin_cfg[] = {
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(0), .bit = 0, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(1), .bit = 1, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(2), .bit = 2, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(3), .bit = 3, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(4), .bit = 4, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(5), .bit = 5, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(6), .bit = 6, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(7), .bit = 7, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(8), .bit = 8, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{	.fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(9), .bit = 9, },
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{ .fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(10), .bit = 10,	},
			{ .fn_name = "VGADDC", .reg = SCU04, .mask = BIT_MASK(10), .bit = 10,	},
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
			{ .fn_name = "EMMC", .reg = SCU00, .mask = BIT_MASK(11), .bit = 11,	},
			{ .fn_name = "VGADDC", .reg = SCU04, .mask = BIT_MASK(11), .bit = 11,	},
			},
	}
};

/* pinctrl_ops */
static int aspeed_g7_soc0_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(aspeed_g7_soc0_pingroups);
}

static const char *aspeed_g7_soc0_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	return aspeed_g7_soc0_pingroups[selector].name;
}

static int aspeed_g7_soc0_get_group_pins(struct pinctrl_dev *pctldev,
					 unsigned int selector,
					 const unsigned int **pins,
					 unsigned int *npins)
{
	*npins = aspeed_g7_soc0_pingroups[selector].npins;
	*pins = aspeed_g7_soc0_pingroups[selector].pins;

	return 0;
}

static int aspeed_g7_soc0_dt_node_to_map(struct pinctrl_dev *pctldev,
					 struct device_node *np_config,
					 struct pinctrl_map **map, u32 *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np_config, map, num_maps,
					      PIN_MAP_TYPE_INVALID);
}

static void aspeed_g7_soc0_dt_free_map(struct pinctrl_dev *pctldev,
				       struct pinctrl_map *map, u32 num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops aspeed_g7_soc0_pinctrl_ops = {
	.get_groups_count = aspeed_g7_soc0_get_groups_count,
	.get_group_name = aspeed_g7_soc0_get_group_name,
	.get_group_pins = aspeed_g7_soc0_get_group_pins,
	.dt_node_to_map = aspeed_g7_soc0_dt_node_to_map,
	.dt_free_map = aspeed_g7_soc0_dt_free_map,
};

static int aspeed_g7_soc0_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(aspeed_g7_soc0_funcs);
}

static const char *aspeed_g7_soc0_get_function_name(struct pinctrl_dev *pctldev,
						    unsigned int function)
{
	return aspeed_g7_soc0_funcs[function].name;
}

static int aspeed_g7_soc0_get_function_groups(struct pinctrl_dev *pctldev,
					      unsigned int function,
					      const char *const **groups,
					      unsigned int *const ngroups)
{
	*ngroups = aspeed_g7_soc0_funcs[function].ngroups;
	*groups = aspeed_g7_soc0_funcs[function].groups;

	return 0;
}

static int aspeed_g7_soc0_pinmux_set_mux(struct pinctrl_dev *pctldev,
					 unsigned int function,
					 unsigned int group)
{
	int i;
	int pin;
	const struct aspeed_g7_soc0_pincfg *cfg;
	const struct aspeed_g7_soc0_funcfg *funcfg;
	struct aspeed_g7_soc0_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct aspeed_g7_soc0_pingroup *pingroup = &aspeed_g7_soc0_pingroups[group];

	if (pingroup->groupcfg) {
		for (i = 0; i < pingroup->groupcfg_nums; i++) {
			struct aspeed_g7_group_funcfg *groupcfg = &pingroup->groupcfg[i];
			u32 val = readl(pinctrl->regs + groupcfg->reg) & ~groupcfg->mask;

			if (groupcfg->enable)
				val |= groupcfg->val;
			writel(val, pinctrl->regs + groupcfg->reg);
			groupcfg++;
		}
	} else {
		for (i = 0; i < pingroup->npins; i++) {
			pin = pingroup->pins[i];
			cfg = &pin_cfg[pin];

			funcfg = &cfg->funcfg[0];
			while (funcfg->fn_name) {
				if (strcmp(funcfg->fn_name, pingroup->name) == 0) {
					writel((readl(pinctrl->regs + funcfg->reg) &
						funcfg->mask) |
							   BIT(funcfg->bit),
						   pinctrl->regs + funcfg->reg);
					break;
				}
				funcfg++;
			}

			if (!funcfg->fn_name)
				return 0;
		}
	}

	return 0;
}

static int aspeed_g7_soc0_gpio_request_enable(struct pinctrl_dev *pctldev,
					      struct pinctrl_gpio_range *range,
					      unsigned int offset)
{
	struct aspeed_g7_soc0_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct aspeed_g7_soc0_pincfg *cfg = &pin_cfg[offset];
	const struct aspeed_g7_soc0_funcfg *funcfg;

	if (!cfg) {
		funcfg = &cfg->funcfg[0];
		while (funcfg->fn_name) {
			writel((readl(pinctrl->regs + funcfg->reg) & ~funcfg->mask),
			       pinctrl->regs + funcfg->reg);
			funcfg++;
		}
	}
	return 0;
}

static void aspeed_g7_soc0_gpio_request_free(struct pinctrl_dev *pctldev,
					     struct pinctrl_gpio_range *range,
					     unsigned int offset)
{
	struct aspeed_g7_soc0_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	int virq;

	virq = irq_find_mapping(pinctrl->domain, offset);
	if (virq)
		irq_dispose_mapping(virq);
}

static const struct pinmux_ops aspeed_g7_soc0_pinmux_ops = {
	.get_functions_count = aspeed_g7_soc0_get_functions_count,
	.get_function_name = aspeed_g7_soc0_get_function_name,
	.get_function_groups = aspeed_g7_soc0_get_function_groups,
	.set_mux = aspeed_g7_soc0_pinmux_set_mux,
	.gpio_request_enable = aspeed_g7_soc0_gpio_request_enable,
	.gpio_disable_free = aspeed_g7_soc0_gpio_request_free,
};

static int aspeed_g7_soc0_config_get(struct pinctrl_dev *pctldev,
				     unsigned int pin, unsigned long *config)
{
//	struct aspeed_g7_soc0_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	int rc = 0;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		break;
	case PIN_CONFIG_OUTPUT:
	case PIN_CONFIG_INPUT_ENABLE:
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		break;
	case PIN_CONFIG_SLEW_RATE:
		break;
	default:
		return -ENOTSUPP;
	}

	if (!rc)
		return -EINVAL;

	return 0;
}

static int aspeed_g7_soc0_config_set_one(struct aspeed_g7_soc0_pinctrl *pinctrl,
					 unsigned int pin, unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		break;
	case PIN_CONFIG_OUTPUT:
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:

		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:

		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		break;
	case PIN_CONFIG_SLEW_RATE:

		break;
	case PIN_CONFIG_DRIVE_STRENGTH:

		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int aspeed_g7_soc0_config_set(struct pinctrl_dev *pctldev,
				     unsigned int pin, unsigned long *configs,
				     unsigned int num_configs)
{
	struct aspeed_g7_soc0_pinctrl *pinctrl =
		pinctrl_dev_get_drvdata(pctldev);
	int rc;

	while (num_configs--) {
		rc = aspeed_g7_soc0_config_set_one(pinctrl, pin, *configs++);
		if (rc)
			return rc;
	}

	return 0;
}

static const struct pinconf_ops aspeed_g7_soc0_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = aspeed_g7_soc0_config_get,
	.pin_config_set = aspeed_g7_soc0_config_set,
};

/* pinctrl_desc */
static struct pinctrl_desc aspeed_g7_soc0_pinctrl_desc = {
	.name = "aspeed-g7-soc0-pinctrl",
	.pins = aspeed_g7_soc0_pins,
	.npins = ARRAY_SIZE(aspeed_g7_soc0_pins),
	.pctlops = &aspeed_g7_soc0_pinctrl_ops,
	.pmxops = &aspeed_g7_soc0_pinmux_ops,
	.confops = &aspeed_g7_soc0_pinconf_ops,
	.owner = THIS_MODULE,
};

static int aspeed_g7_soc0_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_g7_soc0_pinctrl *pctrl;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = dev;
	platform_set_drvdata(pdev, pctrl);

	pctrl->regs = devm_platform_ioremap_resource(pdev, 0);
	pctrl->pctldev =
		devm_pinctrl_register(dev, &aspeed_g7_soc0_pinctrl_desc, pctrl);
	if (IS_ERR(pctrl->pctldev))
		return dev_err_probe(dev, PTR_ERR(pctrl->pctldev),
				     "Failed to register pinctrl device\n");

	return 0;
}

static const struct of_device_id aspeed_g7_soc0_pinctrl_match[] = {
	{ .compatible = "aspeed,ast2700-soc0-pinctrl" },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_g7_soc0_pinctrl_match);

static struct platform_driver aspeed_g7_soc0_pinctrl_driver = {
	.probe = aspeed_g7_soc0_pinctrl_probe,
	.driver = {
		.name = "aspeed-g7-soc0-pinctrl",
		.of_match_table = aspeed_g7_soc0_pinctrl_match,
		.suppress_bind_attrs = true,
	},
};

static int __init aspeed_g7_soc0_pinctrl_register(void)
{
	return platform_driver_register(&aspeed_g7_soc0_pinctrl_driver);
}
arch_initcall(aspeed_g7_soc0_pinctrl_register);
