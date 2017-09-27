/*
 * Driver for the ICST307 VCO clock found in the ARM Reference designs.
 * We wrap the custom interface from <asm/hardware/icst.h> into the generic
 * clock framework.
 *
 * Copyright (C) 2012-2015 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO: when all ARM reference designs are migrated to generic clocks, the
 * ICST clock code from the ARM tree should probably be merged into this
 * file.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "icst.h"
#include "clk-icst.h"

/* Magic unlocking token used on all Versatile boards */
#define VERSATILE_LOCK_VAL	0xA05F

#define VERSATILE_AUX_OSC_BITS 0x7FFFF
#define INTEGRATOR_AP_CM_BITS 0xFF
#define INTEGRATOR_AP_SYS_BITS 0xFF
#define INTEGRATOR_CP_CM_CORE_BITS 0x7FF
#define INTEGRATOR_CP_CM_MEM_BITS 0x7FF000

#define INTEGRATOR_AP_PCI_25_33_MHZ BIT(8)

/**
 * enum icst_control_type - the type of ICST control register
 */
enum icst_control_type {
	ICST_VERSATILE, /* The standard type, all control bits available */
	ICST_INTEGRATOR_AP_CM, /* Only 8 bits of VDW available */
	ICST_INTEGRATOR_AP_SYS, /* Only 8 bits of VDW available */
	ICST_INTEGRATOR_AP_PCI, /* Odd bit pattern storage */
	ICST_INTEGRATOR_CP_CM_CORE, /* Only 8 bits of VDW and 3 bits of OD */
	ICST_INTEGRATOR_CP_CM_MEM, /* Only 8 bits of VDW and 3 bits of OD */
};

/**
 * struct clk_icst - ICST VCO clock wrapper
 * @hw: corresponding clock hardware entry
 * @vcoreg: VCO register address
 * @lockreg: VCO lock register address
 * @params: parameters for this ICST instance
 * @rate: current rate
 * @ctype: the type of control register for the ICST
 */
struct clk_icst {
	struct clk_hw hw;
	struct regmap *map;
	u32 vcoreg_off;
	u32 lockreg_off;
	struct icst_params *params;
	unsigned long rate;
	enum icst_control_type ctype;
};

#define to_icst(_hw) container_of(_hw, struct clk_icst, hw)

/**
 * vco_get() - get ICST VCO settings from a certain ICST
 * @icst: the ICST clock to get
 * @vco: the VCO struct to return the value in
 */
static int vco_get(struct clk_icst *icst, struct icst_vco *vco)
{
	u32 val;
	int ret;

	ret = regmap_read(icst->map, icst->vcoreg_off, &val);
	if (ret)
		return ret;

	/*
	 * The Integrator/AP core clock can only access the low eight
	 * bits of the v PLL divider. Bit 8 is tied low and always zero,
	 * r is hardwired to 22 and output divider s is hardwired to 1
	 * (divide by 2) according to the document
	 * "Integrator CM926EJ-S, CM946E-S, CM966E-S, CM1026EJ-S and
	 * CM1136JF-S User Guide" ARM DUI 0138E, page 3-13 thru 3-14.
	 */
	if (icst->ctype == ICST_INTEGRATOR_AP_CM) {
		vco->v = val & INTEGRATOR_AP_CM_BITS;
		vco->r = 22;
		vco->s = 1;
		return 0;
	}

	/*
	 * The Integrator/AP system clock on the base board can only
	 * access the low eight bits of the v PLL divider. Bit 8 is tied low
	 * and always zero, r is hardwired to 46, and the output divider is
	 * hardwired to 3 (divide by 4) according to the document
	 * "Integrator AP ASIC Development Motherboard" ARM DUI 0098B,
	 * page 3-16.
	 */
	if (icst->ctype == ICST_INTEGRATOR_AP_SYS) {
		vco->v = val & INTEGRATOR_AP_SYS_BITS;
		vco->r = 46;
		vco->s = 3;
		return 0;
	}

	/*
	 * The Integrator/AP PCI clock is using an odd pattern to create
	 * the child clock, basically a single bit called DIVX/Y is used
	 * to select between two different hardwired values: setting the
	 * bit to 0 yields v = 17, r = 22 and OD = 1, whereas setting the
	 * bit to 1 yields v = 14, r = 14 and OD = 1 giving the frequencies
	 * 33 or 25 MHz respectively.
	 */
	if (icst->ctype == ICST_INTEGRATOR_AP_PCI) {
		bool divxy = !!(val & INTEGRATOR_AP_PCI_25_33_MHZ);

		vco->v = divxy ? 17 : 14;
		vco->r = divxy ? 22 : 14;
		vco->s = 1;
		return 0;
	}

	/*
	 * The Integrator/CP core clock can access the low eight bits
	 * of the v PLL divider. Bit 8 is tied low and always zero,
	 * r is hardwired to 22 and the output divider s is accessible
	 * in bits 8 thru 10 according to the document
	 * "Integrator/CM940T, CM920T, CM740T, and CM720T User Guide"
	 * ARM DUI 0157A, page 3-20 thru 3-23 and 4-10.
	 */
	if (icst->ctype == ICST_INTEGRATOR_CP_CM_CORE) {
		vco->v = val & 0xFF;
		vco->r = 22;
		vco->s = (val >> 8) & 7;
		return 0;
	}

	if (icst->ctype == ICST_INTEGRATOR_CP_CM_MEM) {
		vco->v = (val >> 12) & 0xFF;
		vco->r = 22;
		vco->s = (val >> 20) & 7;
		return 0;
	}

	vco->v = val & 0x1ff;
	vco->r = (val >> 9) & 0x7f;
	vco->s = (val >> 16) & 03;
	return 0;
}

/**
 * vco_set() - commit changes to an ICST VCO
 * @icst: the ICST clock to set
 * @vco: the VCO struct to set the changes from
 */
static int vco_set(struct clk_icst *icst, struct icst_vco vco)
{
	u32 mask;
	u32 val;
	int ret;

	/* Mask the bits used by the VCO */
	switch (icst->ctype) {
	case ICST_INTEGRATOR_AP_CM:
		mask = INTEGRATOR_AP_CM_BITS;
		val = vco.v & 0xFF;
		if (vco.v & 0x100)
			pr_err("ICST error: tried to set bit 8 of VDW\n");
		if (vco.s != 1)
			pr_err("ICST error: tried to use VOD != 1\n");
		if (vco.r != 22)
			pr_err("ICST error: tried to use RDW != 22\n");
		break;
	case ICST_INTEGRATOR_AP_SYS:
		mask = INTEGRATOR_AP_SYS_BITS;
		val = vco.v & 0xFF;
		if (vco.v & 0x100)
			pr_err("ICST error: tried to set bit 8 of VDW\n");
		if (vco.s != 3)
			pr_err("ICST error: tried to use VOD != 1\n");
		if (vco.r != 46)
			pr_err("ICST error: tried to use RDW != 22\n");
		break;
	case ICST_INTEGRATOR_CP_CM_CORE:
		mask = INTEGRATOR_CP_CM_CORE_BITS; /* Uses 12 bits */
		val = (vco.v & 0xFF) | vco.s << 8;
		if (vco.v & 0x100)
			pr_err("ICST error: tried to set bit 8 of VDW\n");
		if (vco.r != 22)
			pr_err("ICST error: tried to use RDW != 22\n");
		break;
	case ICST_INTEGRATOR_CP_CM_MEM:
		mask = INTEGRATOR_CP_CM_MEM_BITS; /* Uses 12 bits */
		val = ((vco.v & 0xFF) << 12) | (vco.s << 20);
		if (vco.v & 0x100)
			pr_err("ICST error: tried to set bit 8 of VDW\n");
		if (vco.r != 22)
			pr_err("ICST error: tried to use RDW != 22\n");
		break;
	default:
		/* Regular auxilary oscillator */
		mask = VERSATILE_AUX_OSC_BITS;
		val = vco.v | (vco.r << 9) | (vco.s << 16);
		break;
	}

	pr_debug("ICST: new val = 0x%08x\n", val);

	/* This magic unlocks the VCO so it can be controlled */
	ret = regmap_write(icst->map, icst->lockreg_off, VERSATILE_LOCK_VAL);
	if (ret)
		return ret;
	ret = regmap_update_bits(icst->map, icst->vcoreg_off, mask, val);
	if (ret)
		return ret;
	/* This locks the VCO again */
	ret = regmap_write(icst->map, icst->lockreg_off, 0);
	if (ret)
		return ret;
	return 0;
}

static unsigned long icst_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;
	int ret;

	if (parent_rate)
		icst->params->ref = parent_rate;
	ret = vco_get(icst, &vco);
	if (ret) {
		pr_err("ICST: could not get VCO setting\n");
		return 0;
	}
	icst->rate = icst_hz(icst->params, vco);
	return icst->rate;
}

static long icst_round_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long *prate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;

	if (icst->ctype == ICST_INTEGRATOR_AP_CM ||
	    icst->ctype == ICST_INTEGRATOR_CP_CM_CORE) {
		if (rate <= 12000000)
			return 12000000;
		if (rate >= 160000000)
			return 160000000;
		/* Slam to closest megahertz */
		return DIV_ROUND_CLOSEST(rate, 1000000) * 1000000;
	}

	if (icst->ctype == ICST_INTEGRATOR_CP_CM_MEM) {
		if (rate <= 6000000)
			return 6000000;
		if (rate >= 66000000)
			return 66000000;
		/* Slam to closest 0.5 megahertz */
		return DIV_ROUND_CLOSEST(rate, 500000) * 500000;
	}

	if (icst->ctype == ICST_INTEGRATOR_AP_SYS) {
		/* Divides between 3 and 50 MHz in steps of 0.25 MHz */
		if (rate <= 3000000)
			return 3000000;
		if (rate >= 50000000)
			return 5000000;
		/* Slam to closest 0.25 MHz */
		return DIV_ROUND_CLOSEST(rate, 250000) * 250000;
	}

	if (icst->ctype == ICST_INTEGRATOR_AP_PCI) {
		/*
		 * If we're below or less than halfway from 25 to 33 MHz
		 * select 25 MHz
		 */
		if (rate <= 25000000 || rate < 29000000)
			return 25000000;
		/* Else just return the default frequency */
		return 33000000;
	}

	vco = icst_hz_to_vco(icst->params, rate);
	return icst_hz(icst->params, vco);
}

static int icst_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;

	if (icst->ctype == ICST_INTEGRATOR_AP_PCI) {
		/* This clock is especially primitive */
		unsigned int val;
		int ret;

		if (rate == 25000000) {
			val = 0;
		} else if (rate == 33000000) {
			val = INTEGRATOR_AP_PCI_25_33_MHZ;
		} else {
			pr_err("ICST: cannot set PCI frequency %lu\n",
			       rate);
			return -EINVAL;
		}
		ret = regmap_write(icst->map, icst->lockreg_off,
				   VERSATILE_LOCK_VAL);
		if (ret)
			return ret;
		ret = regmap_update_bits(icst->map, icst->vcoreg_off,
					 INTEGRATOR_AP_PCI_25_33_MHZ,
					 val);
		if (ret)
			return ret;
		/* This locks the VCO again */
		ret = regmap_write(icst->map, icst->lockreg_off, 0);
		if (ret)
			return ret;
		return 0;
	}

	if (parent_rate)
		icst->params->ref = parent_rate;
	vco = icst_hz_to_vco(icst->params, rate);
	icst->rate = icst_hz(icst->params, vco);
	return vco_set(icst, vco);
}

static const struct clk_ops icst_ops = {
	.recalc_rate = icst_recalc_rate,
	.round_rate = icst_round_rate,
	.set_rate = icst_set_rate,
};

static struct clk *icst_clk_setup(struct device *dev,
				  const struct clk_icst_desc *desc,
				  const char *name,
				  const char *parent_name,
				  struct regmap *map,
				  enum icst_control_type ctype)
{
	struct clk *clk;
	struct clk_icst *icst;
	struct clk_init_data init;
	struct icst_params *pclone;

	icst = kzalloc(sizeof(struct clk_icst), GFP_KERNEL);
	if (!icst)
		return ERR_PTR(-ENOMEM);

	pclone = kmemdup(desc->params, sizeof(*pclone), GFP_KERNEL);
	if (!pclone) {
		kfree(icst);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &icst_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	icst->map = map;
	icst->hw.init = &init;
	icst->params = pclone;
	icst->vcoreg_off = desc->vco_offset;
	icst->lockreg_off = desc->lock_offset;
	icst->ctype = ctype;

	clk = clk_register(dev, &icst->hw);
	if (IS_ERR(clk)) {
		kfree(pclone);
		kfree(icst);
	}

	return clk;
}

struct clk *icst_clk_register(struct device *dev,
			const struct clk_icst_desc *desc,
			const char *name,
			const char *parent_name,
			void __iomem *base)
{
	struct regmap_config icst_regmap_conf = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
	};
	struct regmap *map;

	map = regmap_init_mmio(dev, base, &icst_regmap_conf);
	if (IS_ERR(map)) {
		pr_err("could not initialize ICST regmap\n");
		return ERR_CAST(map);
	}
	return icst_clk_setup(dev, desc, name, parent_name, map,
			      ICST_VERSATILE);
}
EXPORT_SYMBOL_GPL(icst_clk_register);

#ifdef CONFIG_OF
/*
 * In a device tree, an memory-mapped ICST clock appear as a child
 * of a syscon node. Assume this and probe it only as a child of a
 * syscon.
 */

static const struct icst_params icst525_params = {
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min		= 8,
	.vd_max		= 263,
	.rd_min		= 3,
	.rd_max		= 65,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct icst_params icst307_params = {
	.vco_max	= ICST307_VCO_MAX,
	.vco_min	= ICST307_VCO_MIN,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
	.s2div		= icst307_s2div,
	.idx2s		= icst307_idx2s,
};

/**
 * The core modules on the Integrator/AP and Integrator/CP have
 * especially crippled ICST525 control.
 */
static const struct icst_params icst525_apcp_cm_params = {
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	/* Minimum 12 MHz, VDW = 4 */
	.vd_min		= 12,
	/*
	 * Maximum 160 MHz, VDW = 152 for all core modules, but
	 * CM926EJ-S, CM1026EJ-S and CM1136JF-S can actually
	 * go to 200 MHz (max VDW = 192).
	 */
	.vd_max		= 192,
	/* r is hardcoded to 22 and this is the actual divisor, +2 */
	.rd_min		= 24,
	.rd_max		= 24,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct icst_params icst525_ap_sys_params = {
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	/* Minimum 3 MHz, VDW = 4 */
	.vd_min		= 3,
	/* Maximum 50 MHz, VDW = 192 */
	.vd_max		= 50,
	/* r is hardcoded to 46 and this is the actual divisor, +2 */
	.rd_min		= 48,
	.rd_max		= 48,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct icst_params icst525_ap_pci_params = {
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	/* Minimum 25 MHz */
	.vd_min		= 25,
	/* Maximum 33 MHz */
	.vd_max		= 33,
	/* r is hardcoded to 14 or 22 and this is the actual divisors +2 */
	.rd_min		= 16,
	.rd_max		= 24,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static void __init of_syscon_icst_setup(struct device_node *np)
{
	struct device_node *parent;
	struct regmap *map;
	struct clk_icst_desc icst_desc;
	const char *name = np->name;
	const char *parent_name;
	struct clk *regclk;
	enum icst_control_type ctype;

	/* We do not release this reference, we are using it perpetually */
	parent = of_get_parent(np);
	if (!parent) {
		pr_err("no parent node for syscon ICST clock\n");
		return;
	}
	map = syscon_node_to_regmap(parent);
	if (IS_ERR(map)) {
		pr_err("no regmap for syscon ICST clock parent\n");
		return;
	}

	if (of_property_read_u32(np, "vco-offset", &icst_desc.vco_offset)) {
		pr_err("no VCO register offset for ICST clock\n");
		return;
	}
	if (of_property_read_u32(np, "lock-offset", &icst_desc.lock_offset)) {
		pr_err("no lock register offset for ICST clock\n");
		return;
	}

	if (of_device_is_compatible(np, "arm,syscon-icst525")) {
		icst_desc.params = &icst525_params;
		ctype = ICST_VERSATILE;
	} else if (of_device_is_compatible(np, "arm,syscon-icst307")) {
		icst_desc.params = &icst307_params;
		ctype = ICST_VERSATILE;
	} else if (of_device_is_compatible(np, "arm,syscon-icst525-integratorap-cm")) {
		icst_desc.params = &icst525_apcp_cm_params;
		ctype = ICST_INTEGRATOR_AP_CM;
	} else if (of_device_is_compatible(np, "arm,syscon-icst525-integratorap-sys")) {
		icst_desc.params = &icst525_ap_sys_params;
		ctype = ICST_INTEGRATOR_AP_SYS;
	} else if (of_device_is_compatible(np, "arm,syscon-icst525-integratorap-pci")) {
		icst_desc.params = &icst525_ap_pci_params;
		ctype = ICST_INTEGRATOR_AP_PCI;
	} else if (of_device_is_compatible(np, "arm,syscon-icst525-integratorcp-cm-core")) {
		icst_desc.params = &icst525_apcp_cm_params;
		ctype = ICST_INTEGRATOR_CP_CM_CORE;
	} else if (of_device_is_compatible(np, "arm,syscon-icst525-integratorcp-cm-mem")) {
		icst_desc.params = &icst525_apcp_cm_params;
		ctype = ICST_INTEGRATOR_CP_CM_MEM;
	} else {
		pr_err("unknown ICST clock %s\n", name);
		return;
	}

	/* Parent clock name is not the same as node parent */
	parent_name = of_clk_get_parent_name(np, 0);

	regclk = icst_clk_setup(NULL, &icst_desc, name, parent_name, map, ctype);
	if (IS_ERR(regclk)) {
		pr_err("error setting up syscon ICST clock %s\n", name);
		return;
	}
	of_clk_add_provider(np, of_clk_src_simple_get, regclk);
	pr_debug("registered syscon ICST clock %s\n", name);
}

CLK_OF_DECLARE(arm_syscon_icst525_clk,
	       "arm,syscon-icst525", of_syscon_icst_setup);
CLK_OF_DECLARE(arm_syscon_icst307_clk,
	       "arm,syscon-icst307", of_syscon_icst_setup);
CLK_OF_DECLARE(arm_syscon_integratorap_cm_clk,
	       "arm,syscon-icst525-integratorap-cm", of_syscon_icst_setup);
CLK_OF_DECLARE(arm_syscon_integratorap_sys_clk,
	       "arm,syscon-icst525-integratorap-sys", of_syscon_icst_setup);
CLK_OF_DECLARE(arm_syscon_integratorap_pci_clk,
	       "arm,syscon-icst525-integratorap-pci", of_syscon_icst_setup);
CLK_OF_DECLARE(arm_syscon_integratorcp_cm_core_clk,
	       "arm,syscon-icst525-integratorcp-cm-core", of_syscon_icst_setup);
CLK_OF_DECLARE(arm_syscon_integratorcp_cm_mem_clk,
	       "arm,syscon-icst525-integratorcp-cm-mem", of_syscon_icst_setup);
#endif
