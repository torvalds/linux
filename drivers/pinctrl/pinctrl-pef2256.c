// SPDX-License-Identifier: GPL-2.0
/*
 * PEF2256 also known as FALC56 driver
 *
 * Copyright 2023 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include <linux/bitfield.h>
#include <linux/framer/pef2256.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* Port Configuration 1..4 */
#define PEF2256_PC1		  0x80
#define PEF2256_PC2		  0x81
#define PEF2256_PC3		  0x82
#define PEF2256_PC4		  0x83
#define PEF2256_12_PC_RPC_MASK	  GENMASK(6, 4)
#define PEF2256_12_PC_RPC_SYPR	  FIELD_PREP_CONST(PEF2256_12_PC_RPC_MASK, 0x0)
#define PEF2256_12_PC_RPC_RFM	  FIELD_PREP_CONST(PEF2256_12_PC_RPC_MASK, 0x1)
#define PEF2256_12_PC_RPC_RFMB	  FIELD_PREP_CONST(PEF2256_12_PC_RPC_MASK, 0x2)
#define PEF2256_12_PC_RPC_RSIGM	  FIELD_PREP_CONST(PEF2256_12_PC_RPC_MASK, 0x3)
#define PEF2256_12_PC_RPC_RSIG	  FIELD_PREP_CONST(PEF2256_12_PC_RPC_MASK, 0x4)
#define PEF2256_12_PC_RPC_DLR	  FIELD_PREP_CONST(PEF2256_12_PC_RPC_MASK, 0x5)
#define PEF2256_12_PC_RPC_FREEZE  FIELD_PREP_CONST(PEF2256_12_PC_RPC_MASK, 0x6)
#define PEF2256_12_PC_RPC_RFSP	  FIELD_PREP_CONST(PEF2256_12_PC_RPC_MASK, 0x7)
#define PEF2256_12_PC_XPC_MASK    GENMASK(4, 0)
#define PEF2256_12_PC_XPC_SYPX	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x0)
#define PEF2256_12_PC_XPC_XFMS	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x1)
#define PEF2256_12_PC_XPC_XSIG	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x2)
#define PEF2256_12_PC_XPC_TCLK	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x3)
#define PEF2256_12_PC_XPC_XMFB	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x4)
#define PEF2256_12_PC_XPC_XSIGM	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x5)
#define PEF2256_12_PC_XPC_DLX	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x6)
#define PEF2256_12_PC_XPC_XCLK	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x7)
#define PEF2256_12_PC_XPC_XLT	  FIELD_PREP_CONST(PEF2256_12_PC_XPC_MASK, 0x8)
#define PEF2256_2X_PC_RPC_MASK	  GENMASK(7, 4)
#define PEF2256_2X_PC_RPC_SYPR	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x0)
#define PEF2256_2X_PC_RPC_RFM	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x1)
#define PEF2256_2X_PC_RPC_RFMB	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x2)
#define PEF2256_2X_PC_RPC_RSIGM	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x3)
#define PEF2256_2X_PC_RPC_RSIG	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x4)
#define PEF2256_2X_PC_RPC_DLR	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x5)
#define PEF2256_2X_PC_RPC_FREEZE  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x6)
#define PEF2256_2X_PC_RPC_RFSP	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x7)
#define PEF2256_2X_PC_RPC_GPI	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0x9)
#define PEF2256_2X_PC_RPC_GPOH	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0xa)
#define PEF2256_2X_PC_RPC_GPOL	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0xb)
#define PEF2256_2X_PC_RPC_LOS	  FIELD_PREP_CONST(PEF2256_2X_PC_RPC_MASK, 0xc)
#define PEF2256_2X_PC_XPC_MASK	  GENMASK(3, 0)
#define PEF2256_2X_PC_XPC_SYPX	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x0)
#define PEF2256_2X_PC_XPC_XFMS	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x1)
#define PEF2256_2X_PC_XPC_XSIG	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x2)
#define PEF2256_2X_PC_XPC_TCLK	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x3)
#define PEF2256_2X_PC_XPC_XMFB	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x4)
#define PEF2256_2X_PC_XPC_XSIGM	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x5)
#define PEF2256_2X_PC_XPC_DLX	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x6)
#define PEF2256_2X_PC_XPC_XCLK	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x7)
#define PEF2256_2X_PC_XPC_XLT	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x8)
#define PEF2256_2X_PC_XPC_GPI	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0x9)
#define PEF2256_2X_PC_XPC_GPOH	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0xa)
#define PEF2256_2X_PC_XPC_GPOL	  FIELD_PREP_CONST(PEF2256_2X_PC_XPC_MASK, 0xb)

struct pef2256_pinreg_desc {
	int offset;
	u8 mask;
};

struct pef2256_function_desc {
	const char *name;
	const char * const*groups;
	unsigned int ngroups;
	u8 func_val;
};

struct pef2256_pinctrl {
	struct device *dev;
	struct regmap *regmap;
	enum pef2256_version version;
	struct pinctrl_desc pctrl_desc;
	const struct pef2256_function_desc *functions;
	unsigned int nfunctions;
};

static int pef2256_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct pef2256_pinctrl *pef2256 = pinctrl_dev_get_drvdata(pctldev);

	/* We map 1 group <-> 1 pin */
	return pef2256->pctrl_desc.npins;
}

static const char *pef2256_get_group_name(struct pinctrl_dev *pctldev,
					  unsigned int selector)
{
	struct pef2256_pinctrl *pef2256 = pinctrl_dev_get_drvdata(pctldev);

	/* We map 1 group <-> 1 pin */
	return pef2256->pctrl_desc.pins[selector].name;
}

static int pef2256_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
				  const unsigned int **pins,
				  unsigned int *num_pins)
{
	struct pef2256_pinctrl *pef2256 = pinctrl_dev_get_drvdata(pctldev);

	/* We map 1 group <-> 1 pin */
	*pins = &pef2256->pctrl_desc.pins[selector].number;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops pef2256_pctlops = {
	.get_groups_count	= pef2256_get_groups_count,
	.get_group_name		= pef2256_get_group_name,
	.get_group_pins		= pef2256_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_pin,
	.dt_free_map		= pinconf_generic_dt_free_map,
};

static int pef2256_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct pef2256_pinctrl *pef2256 = pinctrl_dev_get_drvdata(pctldev);

	return pef2256->nfunctions;
}

static const char *pef2256_get_function_name(struct pinctrl_dev *pctldev,
					     unsigned int selector)
{
	struct pef2256_pinctrl *pef2256 = pinctrl_dev_get_drvdata(pctldev);

	return pef2256->functions[selector].name;
}

static int pef2256_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
				       const char * const **groups,
				       unsigned * const num_groups)
{
	struct pef2256_pinctrl *pef2256 = pinctrl_dev_get_drvdata(pctldev);

	*groups = pef2256->functions[selector].groups;
	*num_groups = pef2256->functions[selector].ngroups;
	return 0;
}

static int pef2256_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
			   unsigned int group_selector)
{
	struct pef2256_pinctrl *pef2256 = pinctrl_dev_get_drvdata(pctldev);
	const struct pef2256_pinreg_desc *pinreg_desc;
	u8 func_val;

	/* We map 1 group <-> 1 pin */
	pinreg_desc = pef2256->pctrl_desc.pins[group_selector].drv_data;
	func_val = pef2256->functions[func_selector].func_val;

	return regmap_update_bits(pef2256->regmap, pinreg_desc->offset,
				  pinreg_desc->mask, func_val);
}

static const struct pinmux_ops pef2256_pmxops = {
	.get_functions_count	= pef2256_get_functions_count,
	.get_function_name	= pef2256_get_function_name,
	.get_function_groups	= pef2256_get_function_groups,
	.set_mux		= pef2256_set_mux,
};

#define PEF2256_PINCTRL_PIN(_number, _name, _offset, _mask) { \
	.number = _number, \
	.name = _name, \
	.drv_data = &(struct pef2256_pinreg_desc) { \
		.offset = _offset, \
		.mask = _mask, \
	}, \
}

static const struct pinctrl_pin_desc pef2256_v12_pins[] = {
	PEF2256_PINCTRL_PIN(0, "RPA", PEF2256_PC1, PEF2256_12_PC_RPC_MASK),
	PEF2256_PINCTRL_PIN(1, "RPB", PEF2256_PC2, PEF2256_12_PC_RPC_MASK),
	PEF2256_PINCTRL_PIN(2, "RPC", PEF2256_PC3, PEF2256_12_PC_RPC_MASK),
	PEF2256_PINCTRL_PIN(3, "RPD", PEF2256_PC4, PEF2256_12_PC_RPC_MASK),
	PEF2256_PINCTRL_PIN(4, "XPA", PEF2256_PC1, PEF2256_12_PC_XPC_MASK),
	PEF2256_PINCTRL_PIN(5, "XPB", PEF2256_PC2, PEF2256_12_PC_XPC_MASK),
	PEF2256_PINCTRL_PIN(6, "XPC", PEF2256_PC3, PEF2256_12_PC_XPC_MASK),
	PEF2256_PINCTRL_PIN(7, "XPD", PEF2256_PC4, PEF2256_12_PC_XPC_MASK),
};

static const struct pinctrl_pin_desc pef2256_v2x_pins[] = {
	PEF2256_PINCTRL_PIN(0, "RPA", PEF2256_PC1, PEF2256_2X_PC_RPC_MASK),
	PEF2256_PINCTRL_PIN(1, "RPB", PEF2256_PC2, PEF2256_2X_PC_RPC_MASK),
	PEF2256_PINCTRL_PIN(2, "RPC", PEF2256_PC3, PEF2256_2X_PC_RPC_MASK),
	PEF2256_PINCTRL_PIN(3, "RPD", PEF2256_PC4, PEF2256_2X_PC_RPC_MASK),
	PEF2256_PINCTRL_PIN(4, "XPA", PEF2256_PC1, PEF2256_2X_PC_XPC_MASK),
	PEF2256_PINCTRL_PIN(5, "XPB", PEF2256_PC2, PEF2256_2X_PC_XPC_MASK),
	PEF2256_PINCTRL_PIN(6, "XPC", PEF2256_PC3, PEF2256_2X_PC_XPC_MASK),
	PEF2256_PINCTRL_PIN(7, "XPD", PEF2256_PC4, PEF2256_2X_PC_XPC_MASK),
};

static const char *const pef2256_rp_groups[] = { "RPA", "RPB", "RPC", "RPD" };
static const char *const pef2256_xp_groups[] = { "XPA", "XPB", "XPC", "XPD" };
static const char *const pef2256_all_groups[] = { "RPA", "RPB", "RPC", "RPD",
						  "XPA", "XPB", "XPC", "XPD" };

#define PEF2256_FUNCTION(_name, _func_val, _groups) { \
	.name = _name, \
	.groups = _groups, \
	.ngroups = ARRAY_SIZE(_groups), \
	.func_val = _func_val, \
}

static const struct pef2256_function_desc pef2256_v2x_functions[] = {
	PEF2256_FUNCTION("SYPR",   PEF2256_2X_PC_RPC_SYPR,   pef2256_rp_groups),
	PEF2256_FUNCTION("RFM",    PEF2256_2X_PC_RPC_RFM,    pef2256_rp_groups),
	PEF2256_FUNCTION("RFMB",   PEF2256_2X_PC_RPC_RFMB,   pef2256_rp_groups),
	PEF2256_FUNCTION("RSIGM",  PEF2256_2X_PC_RPC_RSIGM,  pef2256_rp_groups),
	PEF2256_FUNCTION("RSIG",   PEF2256_2X_PC_RPC_RSIG,   pef2256_rp_groups),
	PEF2256_FUNCTION("DLR",    PEF2256_2X_PC_RPC_DLR,    pef2256_rp_groups),
	PEF2256_FUNCTION("FREEZE", PEF2256_2X_PC_RPC_FREEZE, pef2256_rp_groups),
	PEF2256_FUNCTION("RFSP",   PEF2256_2X_PC_RPC_RFSP,   pef2256_rp_groups),
	PEF2256_FUNCTION("LOS",    PEF2256_2X_PC_RPC_LOS,    pef2256_rp_groups),

	PEF2256_FUNCTION("SYPX",  PEF2256_2X_PC_XPC_SYPX,  pef2256_xp_groups),
	PEF2256_FUNCTION("XFMS",  PEF2256_2X_PC_XPC_XFMS,  pef2256_xp_groups),
	PEF2256_FUNCTION("XSIG",  PEF2256_2X_PC_XPC_XSIG,  pef2256_xp_groups),
	PEF2256_FUNCTION("TCLK",  PEF2256_2X_PC_XPC_TCLK,  pef2256_xp_groups),
	PEF2256_FUNCTION("XMFB",  PEF2256_2X_PC_XPC_XMFB,  pef2256_xp_groups),
	PEF2256_FUNCTION("XSIGM", PEF2256_2X_PC_XPC_XSIGM, pef2256_xp_groups),
	PEF2256_FUNCTION("DLX",   PEF2256_2X_PC_XPC_DLX,   pef2256_xp_groups),
	PEF2256_FUNCTION("XCLK",  PEF2256_2X_PC_XPC_XCLK,  pef2256_xp_groups),
	PEF2256_FUNCTION("XLT",   PEF2256_2X_PC_XPC_XLT,   pef2256_xp_groups),

	PEF2256_FUNCTION("GPI",  PEF2256_2X_PC_RPC_GPI | PEF2256_2X_PC_XPC_GPI,
			 pef2256_all_groups),
	PEF2256_FUNCTION("GPOH", PEF2256_2X_PC_RPC_GPOH | PEF2256_2X_PC_XPC_GPOH,
			 pef2256_all_groups),
	PEF2256_FUNCTION("GPOL", PEF2256_2X_PC_RPC_GPOL | PEF2256_2X_PC_XPC_GPOL,
			 pef2256_all_groups),
};

static const struct pef2256_function_desc pef2256_v12_functions[] = {
	PEF2256_FUNCTION("SYPR",   PEF2256_12_PC_RPC_SYPR,   pef2256_rp_groups),
	PEF2256_FUNCTION("RFM",    PEF2256_12_PC_RPC_RFM,    pef2256_rp_groups),
	PEF2256_FUNCTION("RFMB",   PEF2256_12_PC_RPC_RFMB,   pef2256_rp_groups),
	PEF2256_FUNCTION("RSIGM",  PEF2256_12_PC_RPC_RSIGM,  pef2256_rp_groups),
	PEF2256_FUNCTION("RSIG",   PEF2256_12_PC_RPC_RSIG,   pef2256_rp_groups),
	PEF2256_FUNCTION("DLR",    PEF2256_12_PC_RPC_DLR,    pef2256_rp_groups),
	PEF2256_FUNCTION("FREEZE", PEF2256_12_PC_RPC_FREEZE, pef2256_rp_groups),
	PEF2256_FUNCTION("RFSP",   PEF2256_12_PC_RPC_RFSP,   pef2256_rp_groups),

	PEF2256_FUNCTION("SYPX",  PEF2256_12_PC_XPC_SYPX,  pef2256_xp_groups),
	PEF2256_FUNCTION("XFMS",  PEF2256_12_PC_XPC_XFMS,  pef2256_xp_groups),
	PEF2256_FUNCTION("XSIG",  PEF2256_12_PC_XPC_XSIG,  pef2256_xp_groups),
	PEF2256_FUNCTION("TCLK",  PEF2256_12_PC_XPC_TCLK,  pef2256_xp_groups),
	PEF2256_FUNCTION("XMFB",  PEF2256_12_PC_XPC_XMFB,  pef2256_xp_groups),
	PEF2256_FUNCTION("XSIGM", PEF2256_12_PC_XPC_XSIGM, pef2256_xp_groups),
	PEF2256_FUNCTION("DLX",   PEF2256_12_PC_XPC_DLX,   pef2256_xp_groups),
	PEF2256_FUNCTION("XCLK",  PEF2256_12_PC_XPC_XCLK,  pef2256_xp_groups),
	PEF2256_FUNCTION("XLT",   PEF2256_12_PC_XPC_XLT,   pef2256_xp_groups),
};

static int pef2256_register_pinctrl(struct pef2256_pinctrl *pef2256)
{
	struct pinctrl_dev	*pctrl;

	pef2256->pctrl_desc.name    = dev_name(pef2256->dev);
	pef2256->pctrl_desc.owner   = THIS_MODULE;
	pef2256->pctrl_desc.pctlops = &pef2256_pctlops;
	pef2256->pctrl_desc.pmxops  = &pef2256_pmxops;
	if (pef2256->version == PEF2256_VERSION_1_2) {
		pef2256->pctrl_desc.pins  = pef2256_v12_pins;
		pef2256->pctrl_desc.npins = ARRAY_SIZE(pef2256_v12_pins);
		pef2256->functions  = pef2256_v12_functions;
		pef2256->nfunctions = ARRAY_SIZE(pef2256_v12_functions);
	} else {
		pef2256->pctrl_desc.pins  = pef2256_v2x_pins;
		pef2256->pctrl_desc.npins = ARRAY_SIZE(pef2256_v2x_pins);
		pef2256->functions  = pef2256_v2x_functions;
		pef2256->nfunctions = ARRAY_SIZE(pef2256_v2x_functions);
	}

	pctrl = devm_pinctrl_register(pef2256->dev, &pef2256->pctrl_desc, pef2256);
	if (IS_ERR(pctrl))
		return dev_err_probe(pef2256->dev, PTR_ERR(pctrl),
				     "pinctrl driver registration failed\n");

	return 0;
}

static void pef2256_reset_pinmux(struct pef2256_pinctrl *pef2256)
{
	u8 val;
	/*
	 * Reset values cannot be used.
	 * They define the SYPR/SYPX pin mux for all the RPx and XPx pins and
	 * Only one pin can be muxed to SYPR and one pin can be muxed to SYPX.
	 * Choose here an other reset value.
	 */
	if (pef2256->version == PEF2256_VERSION_1_2)
		val = PEF2256_12_PC_XPC_XCLK | PEF2256_12_PC_RPC_RFSP;
	else
		val = PEF2256_2X_PC_XPC_GPI | PEF2256_2X_PC_RPC_GPI;

	regmap_write(pef2256->regmap, PEF2256_PC1, val);
	regmap_write(pef2256->regmap, PEF2256_PC2, val);
	regmap_write(pef2256->regmap, PEF2256_PC3, val);
	regmap_write(pef2256->regmap, PEF2256_PC4, val);
}

static int pef2256_pinctrl_probe(struct platform_device *pdev)
{
	struct pef2256_pinctrl *pef2256_pinctrl;
	struct pef2256 *pef2256;
	int ret;

	pef2256_pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pef2256_pinctrl), GFP_KERNEL);
	if (!pef2256_pinctrl)
		return -ENOMEM;

	device_set_node(&pdev->dev, dev_fwnode(pdev->dev.parent));

	pef2256 = dev_get_drvdata(pdev->dev.parent);

	pef2256_pinctrl->dev = &pdev->dev;
	pef2256_pinctrl->regmap = pef2256_get_regmap(pef2256);
	pef2256_pinctrl->version = pef2256_get_version(pef2256);

	platform_set_drvdata(pdev, pef2256_pinctrl);

	pef2256_reset_pinmux(pef2256_pinctrl);
	ret = pef2256_register_pinctrl(pef2256_pinctrl);
	if (ret)
		return ret;

	return 0;
}

static struct platform_driver pef2256_pinctrl_driver = {
	.driver = {
		.name = "lantiq-pef2256-pinctrl",
	},
	.probe = pef2256_pinctrl_probe,
};
module_platform_driver(pef2256_pinctrl_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("PEF2256 pin controller driver");
MODULE_LICENSE("GPL");
