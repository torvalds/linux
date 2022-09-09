/* SPDX-License-Identifier: GPL-2.0 */
struct regmap;

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
	ICST_INTEGRATOR_IM_PD1, /* Like the Versatile, all control bits */
};

/**
 * struct clk_icst_desc - descriptor for the ICST VCO
 * @params: ICST parameters
 * @vco_offset: offset to the ICST VCO from the provided memory base
 * @lock_offset: offset to the ICST VCO locking register from the provided
 *	memory base
 */
struct clk_icst_desc {
	const struct icst_params *params;
	u32 vco_offset;
	u32 lock_offset;
};

struct clk *icst_clk_register(struct device *dev,
			      const struct clk_icst_desc *desc,
			      const char *name,
			      const char *parent_name,
			      void __iomem *base);

struct clk *icst_clk_setup(struct device *dev,
			   const struct clk_icst_desc *desc,
			   const char *name,
			   const char *parent_name,
			   struct regmap *map,
			   enum icst_control_type ctype);
