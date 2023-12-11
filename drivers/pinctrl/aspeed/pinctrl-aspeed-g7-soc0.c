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
#include "pinctrl-aspeed.h"
#include "../pinctrl-utils.h"

#define SCU400 0x400 /* Multi-function Pin Control #1  */
#define SCU404 0x404 /* Multi-function Pin Control #2  */
#define SCU408 0x408 /* Multi-function Pin Control #3  */
#define SCU40C 0x40C /* Multi-function Pin Control #3  */
#define SCU410 0x410 /* USB Multi-function Control Register  */
#define SCU414 0x414 /* VGA Function Control Register  */

#define SCU480 0x480 /* GPIO18D0 IO Control Register */
#define SCU484 0x484 /* GPIO18D1 IO Control Register */
#define SCU488 0x488 /* GPIO18D2 IO Control Register */
#define SCU48C 0x48c /* GPIO18D3 IO Control Register */
#define SCU490 0x490 /* GPIO18D4 IO Control Register */
#define SCU494 0x494 /* GPIO18D5 IO Control Register */
#define SCU498 0x498 /* GPIO18D6 IO Control Register */
#define SCU49C 0x49c /* GPIO18D7 IO Control Register */

enum {
	AC14,
	AE15,
	AD14,
	AE14,
	AF14,
	AB13,
	AB14,
	AF15,
	AF13,
	AC13,
	AD13,
	AE13,
	PORTA_U3, // SCU410[1:0]
	PORTA_U2, // SCU410[3:2]
	PORTB_U3, // SCU410[5:4]
	PORTB_U2, // SCU410[7:6]
	PORTA_XHCI, // SCU410[9]
	PORTB_XHCI, // SCU410[10]
	PORTA_MODE, // SCU410[25:24]
	PORTB_MODE, // SCU410[29:28]
};

GROUP_DECL(EMMCG1, AC14, AE15, AD14);
GROUP_DECL(EMMCG4, AC14, AE15, AD14, AE14, AF14, AB13);
GROUP_DECL(EMMCG8, AC14, AE15, AD14, AE14, AF14, AB13, AF13, AC13, AD13, AE13);
GROUP_DECL(EMMCWPN, AF15);
GROUP_DECL(EMMCCDN, AB14);
GROUP_DECL(VGADDC, AD13, AE13);
GROUP_DECL(VB1, AC14, AE15, AD14, AE14);
GROUP_DECL(VB0, AF15, AB14, AF13, AC13);
//USB3A
//xhci: BMC/PCIE, vHub/PHY/EXT port
GROUP_DECL(USB3AXHD, PORTA_U3, PORTA_XHCI);
GROUP_DECL(USB3AXHPD, PORTA_U3, PORTA_XHCI);
GROUP_DECL(USB3AXH, PORTA_U3, PORTA_XHCI);
GROUP_DECL(USB3AXHP, PORTA_U3, PORTA_XHCI);
GROUP_DECL(USB3AXH2B, PORTA_U3, PORTA_XHCI);
GROUP_DECL(USB3AXHP2B, PORTA_U3, PORTA_XHCI);
// vhub to phy
GROUP_DECL(USB3AD, PORTA_U3);

//USB2A
//xhci: BMC/PCIE, vHub/PHY/EXT port
GROUP_DECL(USB2AXHD1, PORTA_U2, PORTA_XHCI);
GROUP_DECL(USB2AXHPD1, PORTA_U2, PORTA_XHCI);
GROUP_DECL(USB2AXH, PORTA_U2, PORTA_XHCI, PORTA_MODE);
GROUP_DECL(USB2AXHP, PORTA_U2, PORTA_XHCI, PORTA_MODE);
GROUP_DECL(USB2AXH2B, PORTA_U2, PORTA_XHCI);
GROUP_DECL(USB2AXHP2B, PORTA_U2, PORTA_XHCI);
// vhub to phy
GROUP_DECL(USB2AD1, PORTA_U2, PORTA_MODE);
//ehci
GROUP_DECL(USB2AHPD0, PORTA_U2, PORTA_MODE);
GROUP_DECL(USB2AH, PORTA_U2, PORTA_MODE);
GROUP_DECL(USB2AHP, PORTA_U2, PORTA_MODE);
GROUP_DECL(USB2AD0, PORTA_U2, PORTA_MODE);
//USB3B
//xhci: BMC/PCIE, vHub/PHY/EXT port
GROUP_DECL(USB3BXHD, PORTB_U3, PORTB_XHCI);
GROUP_DECL(USB3BXHPD, PORTB_U3, PORTB_XHCI);
GROUP_DECL(USB3BXH, PORTB_U3, PORTB_XHCI);
GROUP_DECL(USB3BXHP, PORTB_U3, PORTB_XHCI);
GROUP_DECL(USB3BXH2A, PORTB_U3, PORTB_XHCI);
GROUP_DECL(USB3BXHP2A, PORTB_U3, PORTB_XHCI);
// vhub to phy
GROUP_DECL(USB3BD, PORTB_U3);

//USB2B
//xhci: BMC/PCIE, vHub/PHY/EXT port
GROUP_DECL(USB2BXHD1, PORTB_U2, PORTB_XHCI);
GROUP_DECL(USB2BXHPD1, PORTB_U2, PORTB_XHCI);
GROUP_DECL(USB2BXH, PORTB_U2, PORTB_XHCI, PORTB_MODE);
GROUP_DECL(USB2BXHP, PORTB_U2, PORTB_XHCI, PORTB_MODE);
GROUP_DECL(USB2BXH2A, PORTB_U2, PORTB_XHCI);
GROUP_DECL(USB2BXHP2A, PORTB_U2, PORTB_XHCI);
// vhub to phy
GROUP_DECL(USB2BD1, PORTB_U2, PORTB_MODE);
//ehci
GROUP_DECL(USB2BHPD0, PORTB_U2, PORTB_MODE);
GROUP_DECL(USB2BH, PORTB_U2, PORTB_MODE);
GROUP_DECL(USB2BHP, PORTB_U2, PORTB_MODE);
// vhub to phy
GROUP_DECL(USB2BD0, PORTB_U2, PORTB_MODE);

static struct aspeed_pin_group aspeed_g7_soc0_pingroups[] = {
	ASPEED_PINCTRL_GROUP(EMMCG1),
	ASPEED_PINCTRL_GROUP(EMMCG4),
	ASPEED_PINCTRL_GROUP(EMMCG8),
	ASPEED_PINCTRL_GROUP(EMMCWPN),
	ASPEED_PINCTRL_GROUP(EMMCCDN),
	ASPEED_PINCTRL_GROUP(VGADDC),
	ASPEED_PINCTRL_GROUP(VB1),
	ASPEED_PINCTRL_GROUP(VB0),
	ASPEED_PINCTRL_GROUP(USB3AXHD),
	ASPEED_PINCTRL_GROUP(USB3AXHPD),
	ASPEED_PINCTRL_GROUP(USB3AXH),
	ASPEED_PINCTRL_GROUP(USB3AXHP),
	ASPEED_PINCTRL_GROUP(USB3AXH2B),
	ASPEED_PINCTRL_GROUP(USB3AXHP2B),
	ASPEED_PINCTRL_GROUP(USB3AD),
	ASPEED_PINCTRL_GROUP(USB2AXHD1),
	ASPEED_PINCTRL_GROUP(USB2AXHPD1),
	ASPEED_PINCTRL_GROUP(USB2AXH),
	ASPEED_PINCTRL_GROUP(USB2AXHP),
	ASPEED_PINCTRL_GROUP(USB2AXH2B),
	ASPEED_PINCTRL_GROUP(USB2AXHP2B),
	ASPEED_PINCTRL_GROUP(USB2AD1),
	ASPEED_PINCTRL_GROUP(USB2AHPD0),
	ASPEED_PINCTRL_GROUP(USB2AH),
	ASPEED_PINCTRL_GROUP(USB2AHP),
	ASPEED_PINCTRL_GROUP(USB2AD0),
	ASPEED_PINCTRL_GROUP(USB3BXHD),
	ASPEED_PINCTRL_GROUP(USB3BXHPD),
	ASPEED_PINCTRL_GROUP(USB3BXH),
	ASPEED_PINCTRL_GROUP(USB3BXHP),
	ASPEED_PINCTRL_GROUP(USB3BXH2A),
	ASPEED_PINCTRL_GROUP(USB3BXHP2A),
	ASPEED_PINCTRL_GROUP(USB3BD),
	ASPEED_PINCTRL_GROUP(USB2BXHD1),
	ASPEED_PINCTRL_GROUP(USB2BXHPD1),
	ASPEED_PINCTRL_GROUP(USB2BXH),
	ASPEED_PINCTRL_GROUP(USB2BXHP),
	ASPEED_PINCTRL_GROUP(USB2BXH2A),
	ASPEED_PINCTRL_GROUP(USB2BXHP2A),
	ASPEED_PINCTRL_GROUP(USB2BD1),
	ASPEED_PINCTRL_GROUP(USB2BHPD0),
	ASPEED_PINCTRL_GROUP(USB2BH),
	ASPEED_PINCTRL_GROUP(USB2BHP),
	ASPEED_PINCTRL_GROUP(USB2BD0),
};

FUNC_DECL_(EMMC, "EMMCG1", "EMMCG4", "EMMCG8", "EMMCWPN", "EMMCCDN");
FUNC_DECL_(VGADDC, "VGADDC");
FUNC_DECL_(VB, "VB0", "VB1");
FUNC_DECL_(USB3A, "USB3AXHD", "USB3AXHPD", "USB3AXH", "USB3AXHP", "USB3AXH2B",
	   "USB3AXHP2B", "USB3AD");
FUNC_DECL_(USB2A, "USB2AXHD1", "USB2AXHPD1", "USB2AXH", "USB2AXHP", "USB2AXH2B",
	   "USB2AXHP2B", "USB2AD1", "USB2AHPD0", "USB2AH", "USB2AHP",
	   "USB2AD0");
FUNC_DECL_(USB3B, "USB3BXHD", "USB3BXHPD", "USB3BXH", "USB3BXHP", "USB3BXH2A",
	   "USB3BXHP2A", "USB3BD");
FUNC_DECL_(USB2B, "USB2BXHD1", "USB2BXHPD1", "USB2BXH", "USB2BXHP", "USB2BXH2A",
	   "USB2BXHP2A", "USB2BD1", "USB2BHPD0", "USB2BH", "USB2BHP",
	   "USB2BD0");

static struct aspeed_pin_function aspeed_g7_soc0_funcs[] = {
	ASPEED_PINCTRL_FUNC(EMMC),
	ASPEED_PINCTRL_FUNC(VGADDC),
	ASPEED_PINCTRL_FUNC(VB),
	ASPEED_PINCTRL_FUNC(USB3A),
	ASPEED_PINCTRL_FUNC(USB2A),
	ASPEED_PINCTRL_FUNC(USB3B),
	ASPEED_PINCTRL_FUNC(USB2B),
};

static const struct pinctrl_pin_desc aspeed_g7_soc0_pins[] = {
	PINCTRL_PIN(AC14, "AC14"),
	PINCTRL_PIN(AE15, "AE15"),
	PINCTRL_PIN(AD14, "AD14"),
	PINCTRL_PIN(AE14, "AE14"),
	PINCTRL_PIN(AF14, "AF14"),
	PINCTRL_PIN(AB13, "AB13"),
	PINCTRL_PIN(AF15, "AF15"),
	PINCTRL_PIN(AB14, "AB14"),
	PINCTRL_PIN(AF13, "AF13"),
	PINCTRL_PIN(AC13, "AC13"),
	PINCTRL_PIN(AD13, "AD13"),
	PINCTRL_PIN(AE13, "AE13"),
	PINCTRL_PIN(PORTA_U3, "PORTA_U3"),
	PINCTRL_PIN(PORTA_U2, "PORTA_U2"),
	PINCTRL_PIN(PORTB_U3, "PORTB_U3"),
	PINCTRL_PIN(PORTB_U2, "PORTB_U2"),
	PINCTRL_PIN(PORTA_XHCI, "PORTA_XHCI"),
	PINCTRL_PIN(PORTB_XHCI, "PORTB_XHCI"),
	PINCTRL_PIN(PORTA_MODE, "PORTA_MODE"),
	PINCTRL_PIN(PORTB_MODE, "PORTB_MODE"),
};

struct aspeed_g7_soc0_funcfg {
	char *name;
	u32 reg;
	u32 mask;
	int val;
};

struct aspeed_g7_soc0_pincfg {
	struct aspeed_g7_soc0_funcfg *funcfg;
};

#define PIN_CFG(cfg_name, cfg_reg, cfg_mask, cfg_val) \
	{ .name = #cfg_name, .reg = cfg_reg, .mask = cfg_mask, .val = cfg_val }

static const struct aspeed_g7_soc0_pincfg pin_cfg[] = {
//GPIO18A0
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(0), BIT(0)),
				PIN_CFG(VB1, SCU404, BIT_MASK(0), BIT(0)),
			},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(1), BIT(1)),
				PIN_CFG(VB1, SCU404, BIT_MASK(1), BIT(1)),
			},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(2), BIT(2)),
				PIN_CFG(VB1, SCU404, BIT_MASK(2), BIT(2)),
			},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(3), BIT(3)),
				PIN_CFG(VB1, SCU404, BIT_MASK(3), BIT(3)),
			},
	},
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]) {
				PIN_CFG(EMMC, SCU400, BIT_MASK(4), BIT(4)),
		},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(5), BIT(5)),
			},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(6), BIT(6)),
				PIN_CFG(VB0, SCU404, BIT_MASK(6), BIT(6)),
			},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(7), BIT(7)),
				PIN_CFG(VB0, SCU404, BIT_MASK(7), BIT(7)),
			},
	},
//GPIO18B0
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(8), BIT(8)),
				PIN_CFG(VB0, SCU404, BIT_MASK(8), BIT(8)),
			},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(9), BIT(9)),
				PIN_CFG(VB0, SCU404, BIT_MASK(9), BIT(9)),
			},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(10), BIT(10)),
				PIN_CFG(VGADDC, SCU404, BIT_MASK(10), BIT(10)),
			},
	},
	{
		.funcfg =
			(struct aspeed_g7_soc0_funcfg[]){
				PIN_CFG(EMMC, SCU400, BIT_MASK(11), BIT(11)),
				PIN_CFG(VGADDC, SCU404, BIT_MASK(11), BIT(11)),
			},
	},
//PORTA_U3
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]){
			PIN_CFG(USB3AXHD, SCU410, GENMASK(1, 0), 0),
			PIN_CFG(USB3AXHPD, SCU410, GENMASK(1, 0), 0),
			PIN_CFG(USB3AXH, SCU410, GENMASK(1, 0), 2),
			PIN_CFG(USB3AXHP, SCU410, GENMASK(1, 0), 2),
			PIN_CFG(USB3AXH2B, SCU410, GENMASK(1, 0), 3),
			PIN_CFG(USB3AXHP2B, SCU410, GENMASK(1, 0), 3),
			PIN_CFG(USB3AD, SCU410, GENMASK(1, 0), 1),
		},
	},
//PORTA_U2
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]){
			PIN_CFG(USB2AXHD1, SCU410, GENMASK(3, 2), 0),
			PIN_CFG(USB2AXHPD1, SCU410, GENMASK(3, 2), 0),
			PIN_CFG(USB2AXH, SCU410, GENMASK(3, 2), 2 << 2),
			PIN_CFG(USB2AXHP, SCU410, GENMASK(3, 2), 2 << 2),
			PIN_CFG(USB2AXH2B, SCU410, GENMASK(3, 2), 3 << 2),
			PIN_CFG(USB2AXHP2B, SCU410, GENMASK(3, 2), 3 << 2),
			PIN_CFG(USB2AD1, SCU410, GENMASK(3, 2), 1 << 2),
		},
	},
//PORTB_U3
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]){
			PIN_CFG(USB3BXHD, SCU410, GENMASK(5, 4), 0),
			PIN_CFG(USB3BXHPD, SCU410, GENMASK(5, 4), 0),
			PIN_CFG(USB3BXH, SCU410, GENMASK(5, 4), 2 << 4),
			PIN_CFG(USB3BXHP, SCU410, GENMASK(5, 4), 2 << 4),
			PIN_CFG(USB3BXH2A, SCU410, GENMASK(5, 4), 3 << 4),
			PIN_CFG(USB3BXHP2A, SCU410, GENMASK(5, 4), 3 << 4),
			PIN_CFG(USB3BD, SCU410, GENMASK(5, 4), 1 << 4),
		},
	},
//PORTB_U2
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]){
			PIN_CFG(USB2BXHD1, SCU410, GENMASK(7, 6), 0),
			PIN_CFG(USB2BXHPD1, SCU410, GENMASK(7, 6), 0),
			PIN_CFG(USB2BXH, SCU410, GENMASK(7, 6), 2 << 6),
			PIN_CFG(USB2BXHP, SCU410, GENMASK(7, 6), 2 << 6),
			PIN_CFG(USB2BXH2A, SCU410, GENMASK(7, 6), 3 << 6),
			PIN_CFG(USB2BXHP2A, SCU410, GENMASK(7, 6), 3 << 6),
			PIN_CFG(USB2BD1, SCU410, GENMASK(7, 6), 1 << 6),
		},
	},
//PORTA_XHCI
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]){
			PIN_CFG(USB3AXHD, SCU410, BIT_MASK(9), 1 << 9),
			PIN_CFG(USB3AXHPD, SCU410, BIT_MASK(9), 0),
			PIN_CFG(USB3AXH, SCU410, BIT_MASK(9), 1 << 9),
			PIN_CFG(USB3AXHP, SCU410, BIT_MASK(9), 0),
			PIN_CFG(USB3AXH2B, SCU410, BIT_MASK(9), 1 << 9),
			PIN_CFG(USB3AXHP2B, SCU410, BIT_MASK(9), 0),
			PIN_CFG(USB2AXHD1, SCU410, BIT_MASK(9), 1 << 9),
			PIN_CFG(USB2AXHPD1, SCU410, BIT_MASK(9), 0),
			PIN_CFG(USB2AXH, SCU410, BIT_MASK(9), 1 << 9),
			PIN_CFG(USB2AXHP, SCU410, BIT_MASK(9), 0),
			PIN_CFG(USB2AXH2B, SCU410, BIT_MASK(9), 1 << 9),
			PIN_CFG(USB2AXHP2B, SCU410, BIT_MASK(9), 0),
		},
	},
//PORTB_XHCI
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]){
			PIN_CFG(USB3BXHD, SCU410, BIT_MASK(10), 1 << 10),
			PIN_CFG(USB3BXHPD, SCU410, BIT_MASK(10), 0),
			PIN_CFG(USB3BXH, SCU410, BIT_MASK(10), 1 << 10),
			PIN_CFG(USB3BXHP, SCU410, BIT_MASK(10), 0),
			PIN_CFG(USB3BXH2A, SCU410, BIT_MASK(10), 1 << 10),
			PIN_CFG(USB3BXHP2A, SCU410, BIT_MASK(10), 0),
			PIN_CFG(USB2BXHD1, SCU410, BIT_MASK(10), 1 << 10),
			PIN_CFG(USB2BXHPD1, SCU410, BIT_MASK(10), 0),
			PIN_CFG(USB2BXH, SCU410, BIT_MASK(10), 1 << 10),
			PIN_CFG(USB2BXHP, SCU410, BIT_MASK(10), 0),
			PIN_CFG(USB2BXH2A, SCU410, BIT_MASK(10), 1 << 10),
			PIN_CFG(USB2BXHP2A, SCU410, BIT_MASK(10), 0),
		},
	},
//PORTA_MODE
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]){
			PIN_CFG(USB2AHPD0, SCU410, GENMASK(25, 24), 0),
			PIN_CFG(USB2AH, SCU410, GENMASK(25, 24), 2 << 24),
			PIN_CFG(USB2AHP, SCU410, GENMASK(25, 24), 3 << 24),
			PIN_CFG(USB2AD0, SCU410, GENMASK(25, 24), 1 << 24),
		},
	},
//PORTB_MODE
	{
		.funcfg = (struct aspeed_g7_soc0_funcfg[]){
			PIN_CFG(USB2BHPD0, SCU410, GENMASK(29, 28), 0),
			PIN_CFG(USB2BH, SCU410, GENMASK(29, 28), 2 << 28),
			PIN_CFG(USB2BHP, SCU410, GENMASK(29, 28), 3 << 28),
			PIN_CFG(USB2BD0, SCU410, GENMASK(29, 28), 1 << 28),
		},
	},
};

static const struct pinctrl_ops aspeed_g7_soc0_pinctrl_ops = {
	.get_groups_count = aspeed_pinctrl_get_groups_count,
	.get_group_name = aspeed_pinctrl_get_group_name,
	.get_group_pins = aspeed_pinctrl_get_group_pins,
	.pin_dbg_show = aspeed_pinctrl_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

static int aspeed_g7_soc0_pinmux_set_mux(struct pinctrl_dev *pctldev,
					 unsigned int function,
					 unsigned int group)
{
	int i;
	int pin;
	const struct aspeed_g7_soc0_pincfg *cfg;
	const struct aspeed_g7_soc0_funcfg *funcfg;
	struct aspeed_pinctrl_data *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct aspeed_pin_group *pingroup = &aspeed_g7_soc0_pingroups[group];

	for (i = 0; i < pingroup->npins; i++) {
		pin = pingroup->pins[i];
		cfg = &pin_cfg[pin];

		funcfg = &cfg->funcfg[0];
		while (funcfg->name) {
			if (strcmp(funcfg->name, pingroup->name) == 0) {
				regmap_update_bits(pinctrl->scu, funcfg->reg,
						   funcfg->mask, funcfg->val);
				break;
			}
			funcfg++;
		}

		if (!funcfg->name)
			return 0;
	}

	return 0;
}

static int aspeed_g7_soc0_gpio_request_enable(struct pinctrl_dev *pctldev,
					      struct pinctrl_gpio_range *range,
					      unsigned int offset)
{
	struct aspeed_pinctrl_data *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct aspeed_g7_soc0_pincfg *cfg = &pin_cfg[offset];
	const struct aspeed_g7_soc0_funcfg *funcfg;

	if (cfg) {
		funcfg = &cfg->funcfg[0];
		if (funcfg->name)
			regmap_update_bits(pinctrl->scu, funcfg->reg,
					   funcfg->mask, 0);
	}
	return 0;
}


static const struct pinmux_ops aspeed_g7_soc0_pinmux_ops = {
	.get_functions_count = aspeed_pinmux_get_fn_count,
	.get_function_name = aspeed_pinmux_get_fn_name,
	.get_function_groups = aspeed_pinmux_get_fn_groups,
	.set_mux = aspeed_g7_soc0_pinmux_set_mux,
	.gpio_request_enable = aspeed_g7_soc0_gpio_request_enable,
	.strict = true,
};

static const struct pinconf_ops aspeed_g7_soc0_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = aspeed_pin_config_get,
	.pin_config_set = aspeed_pin_config_set,
	.pin_config_group_get = aspeed_pin_config_group_get,
	.pin_config_group_set = aspeed_pin_config_group_set,
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

static struct aspeed_pinctrl_data aspeed_g7_pinctrl_data = {
	.pins = aspeed_g7_soc0_pins,
	.npins = ARRAY_SIZE(aspeed_g7_soc0_pins),
	.pinmux = {
		.groups = aspeed_g7_soc0_pingroups,
		.ngroups = ARRAY_SIZE(aspeed_g7_soc0_pingroups),
		.functions = aspeed_g7_soc0_funcs,
		.nfunctions = ARRAY_SIZE(aspeed_g7_soc0_funcs),
	},
};

static int aspeed_g7_soc0_pinctrl_probe(struct platform_device *pdev)
{
	return aspeed_pinctrl_probe(pdev, &aspeed_g7_soc0_pinctrl_desc,
				    &aspeed_g7_pinctrl_data);
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
