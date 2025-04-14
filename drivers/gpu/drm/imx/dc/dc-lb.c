// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/component.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/drm_blend.h>

#include "dc-drv.h"
#include "dc-pe.h"

#define PIXENGCFG_DYNAMIC			0x8
#define  PIXENGCFG_DYNAMIC_PRIM_SEL_MASK	GENMASK(5, 0)
#define  PIXENGCFG_DYNAMIC_PRIM_SEL(x)		\
		FIELD_PREP(PIXENGCFG_DYNAMIC_PRIM_SEL_MASK, (x))
#define  PIXENGCFG_DYNAMIC_SEC_SEL_MASK		GENMASK(13, 8)
#define  PIXENGCFG_DYNAMIC_SEC_SEL(x)		\
		FIELD_PREP(PIXENGCFG_DYNAMIC_SEC_SEL_MASK, (x))

#define STATICCONTROL				0x8
#define  SHDTOKSEL_MASK				GENMASK(4, 3)
#define  SHDTOKSEL(x)				FIELD_PREP(SHDTOKSEL_MASK, (x))
#define  SHDLDSEL_MASK				GENMASK(2, 1)
#define  SHDLDSEL(x)				FIELD_PREP(SHDLDSEL_MASK, (x))

#define CONTROL					0xc
#define  CTRL_MODE_MASK				BIT(0)
#define  CTRL_MODE(x)				FIELD_PREP(CTRL_MODE_MASK, (x))

#define BLENDCONTROL				0x10
#define  ALPHA_MASK				GENMASK(23, 16)
#define  ALPHA(x)				FIELD_PREP(ALPHA_MASK, (x))
#define  PRIM_C_BLD_FUNC_MASK			GENMASK(2, 0)
#define  PRIM_C_BLD_FUNC(x)			\
		FIELD_PREP(PRIM_C_BLD_FUNC_MASK, (x))
#define  SEC_C_BLD_FUNC_MASK			GENMASK(6, 4)
#define  SEC_C_BLD_FUNC(x)			\
		FIELD_PREP(SEC_C_BLD_FUNC_MASK, (x))
#define  PRIM_A_BLD_FUNC_MASK			GENMASK(10, 8)
#define  PRIM_A_BLD_FUNC(x)			\
		FIELD_PREP(PRIM_A_BLD_FUNC_MASK, (x))
#define  SEC_A_BLD_FUNC_MASK			GENMASK(14, 12)
#define  SEC_A_BLD_FUNC(x)			\
		FIELD_PREP(SEC_A_BLD_FUNC_MASK, (x))

#define POSITION				0x14
#define  XPOS_MASK				GENMASK(15, 0)
#define  XPOS(x)				FIELD_PREP(XPOS_MASK, (x))
#define  YPOS_MASK				GENMASK(31, 16)
#define  YPOS(x)				FIELD_PREP(YPOS_MASK, (x))

enum dc_lb_blend_func {
	DC_LAYERBLEND_BLEND_ZERO,
	DC_LAYERBLEND_BLEND_ONE,
	DC_LAYERBLEND_BLEND_PRIM_ALPHA,
	DC_LAYERBLEND_BLEND_ONE_MINUS_PRIM_ALPHA,
	DC_LAYERBLEND_BLEND_SEC_ALPHA,
	DC_LAYERBLEND_BLEND_ONE_MINUS_SEC_ALPHA,
	DC_LAYERBLEND_BLEND_CONST_ALPHA,
	DC_LAYERBLEND_BLEND_ONE_MINUS_CONST_ALPHA,
};

enum dc_lb_shadow_sel {
	BOTH = 0x2,
};

static const struct dc_subdev_info dc_lb_info[] = {
	{ .reg_start = 0x56180ba0, .id = 0, },
	{ .reg_start = 0x56180bc0, .id = 1, },
	{ .reg_start = 0x56180be0, .id = 2, },
	{ .reg_start = 0x56180c00, .id = 3, },
};

static const struct regmap_range dc_lb_pec_regmap_access_ranges[] = {
	regmap_reg_range(PIXENGCFG_DYNAMIC, PIXENGCFG_DYNAMIC),
};

static const struct regmap_access_table dc_lb_pec_regmap_access_table = {
	.yes_ranges = dc_lb_pec_regmap_access_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_lb_pec_regmap_access_ranges),
};

static const struct regmap_config dc_lb_pec_regmap_config = {
	.name = "pec",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_lb_pec_regmap_access_table,
	.rd_table = &dc_lb_pec_regmap_access_table,
	.max_register = PIXENGCFG_DYNAMIC,
};

static const struct regmap_range dc_lb_regmap_ranges[] = {
	regmap_reg_range(STATICCONTROL, POSITION),
};

static const struct regmap_access_table dc_lb_regmap_access_table = {
	.yes_ranges = dc_lb_regmap_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_lb_regmap_ranges),
};

static const struct regmap_config dc_lb_cfg_regmap_config = {
	.name = "cfg",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_lb_regmap_access_table,
	.rd_table = &dc_lb_regmap_access_table,
	.max_register = POSITION,
};

static const enum dc_link_id prim_sels[] = {
	/* common options */
	LINK_ID_NONE,
	LINK_ID_CONSTFRAME0,
	LINK_ID_CONSTFRAME1,
	LINK_ID_CONSTFRAME4,
	LINK_ID_CONSTFRAME5,
	/*
	 * special options:
	 * layerblend(n) has n special options,
	 * from layerblend0 to layerblend(n - 1), e.g.,
	 * layerblend3 has 3 special options -
	 * layerblend0/1/2.
	 */
	LINK_ID_LAYERBLEND0,
	LINK_ID_LAYERBLEND1,
	LINK_ID_LAYERBLEND2,
	LINK_ID_LAYERBLEND3,
};

static const enum dc_link_id sec_sels[] = {
	LINK_ID_NONE,
	LINK_ID_FETCHWARP2,
	LINK_ID_FETCHLAYER0,
};

enum dc_link_id dc_lb_get_link_id(struct dc_lb *lb)
{
	return lb->link;
}

void dc_lb_pec_dynamic_prim_sel(struct dc_lb *lb, enum dc_link_id prim)
{
	int fixed_sels_num = ARRAY_SIZE(prim_sels) - 4;
	int i;

	for (i = 0; i < fixed_sels_num + lb->id; i++) {
		if (prim_sels[i] == prim) {
			regmap_write_bits(lb->reg_pec, PIXENGCFG_DYNAMIC,
					  PIXENGCFG_DYNAMIC_PRIM_SEL_MASK,
					  PIXENGCFG_DYNAMIC_PRIM_SEL(prim));
			return;
		}
	}

	dev_warn(lb->dev, "invalid primary input selection:%d\n", prim);
}

void dc_lb_pec_dynamic_sec_sel(struct dc_lb *lb, enum dc_link_id sec)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sec_sels); i++) {
		if (sec_sels[i] == sec) {
			regmap_write_bits(lb->reg_pec, PIXENGCFG_DYNAMIC,
					  PIXENGCFG_DYNAMIC_SEC_SEL_MASK,
					  PIXENGCFG_DYNAMIC_SEC_SEL(sec));
			return;
		}
	}

	dev_warn(lb->dev, "invalid secondary input selection:%d\n", sec);
}

void dc_lb_pec_clken(struct dc_lb *lb, enum dc_pec_clken clken)
{
	regmap_write_bits(lb->reg_pec, PIXENGCFG_DYNAMIC, CLKEN_MASK,
			  CLKEN(clken));
}

static inline void dc_lb_enable_shden(struct dc_lb *lb)
{
	regmap_write_bits(lb->reg_cfg, STATICCONTROL, SHDEN, SHDEN);
}

static inline void dc_lb_shdtoksel(struct dc_lb *lb, enum dc_lb_shadow_sel sel)
{
	regmap_write_bits(lb->reg_cfg, STATICCONTROL, SHDTOKSEL_MASK,
			  SHDTOKSEL(sel));
}

static inline void dc_lb_shdldsel(struct dc_lb *lb, enum dc_lb_shadow_sel sel)
{
	regmap_write_bits(lb->reg_cfg, STATICCONTROL, SHDLDSEL_MASK,
			  SHDLDSEL(sel));
}

void dc_lb_mode(struct dc_lb *lb, enum dc_lb_mode mode)
{
	regmap_write_bits(lb->reg_cfg, CONTROL, CTRL_MODE_MASK, mode);
}

static inline void dc_lb_blendcontrol(struct dc_lb *lb)
{
	u32 val = PRIM_A_BLD_FUNC(DC_LAYERBLEND_BLEND_ZERO) |
		  SEC_A_BLD_FUNC(DC_LAYERBLEND_BLEND_ZERO) |
		  PRIM_C_BLD_FUNC(DC_LAYERBLEND_BLEND_ZERO) |
		  SEC_C_BLD_FUNC(DC_LAYERBLEND_BLEND_CONST_ALPHA) |
		  ALPHA(DRM_BLEND_ALPHA_OPAQUE >> 8);

	regmap_write(lb->reg_cfg, BLENDCONTROL, val);
}

void dc_lb_position(struct dc_lb *lb, int x, int y)
{
	regmap_write(lb->reg_cfg, POSITION, XPOS(x) | YPOS(y));
}

int dc_lb_get_id(struct dc_lb *lb)
{
	return lb->id;
}

void dc_lb_init(struct dc_lb *lb)
{
	dc_lb_pec_dynamic_prim_sel(lb, LINK_ID_NONE);
	dc_lb_pec_dynamic_sec_sel(lb, LINK_ID_NONE);
	dc_lb_pec_clken(lb, CLKEN_DISABLE);
	dc_lb_shdldsel(lb, BOTH);
	dc_lb_shdtoksel(lb, BOTH);
	dc_lb_blendcontrol(lb);
	dc_lb_enable_shden(lb);
}

static int dc_lb_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dc_drm_device *dc_drm = data;
	struct resource *res_pec;
	void __iomem *base_pec;
	void __iomem *base_cfg;
	struct dc_lb *lb;

	lb = devm_kzalloc(dev, sizeof(*lb), GFP_KERNEL);
	if (!lb)
		return -ENOMEM;

	base_pec = devm_platform_get_and_ioremap_resource(pdev, 0, &res_pec);
	if (IS_ERR(base_pec))
		return PTR_ERR(base_pec);

	base_cfg = devm_platform_ioremap_resource_byname(pdev, "cfg");
	if (IS_ERR(base_cfg))
		return PTR_ERR(base_cfg);

	lb->reg_pec = devm_regmap_init_mmio(dev, base_pec,
					    &dc_lb_pec_regmap_config);
	if (IS_ERR(lb->reg_pec))
		return PTR_ERR(lb->reg_pec);

	lb->reg_cfg = devm_regmap_init_mmio(dev, base_cfg,
					    &dc_lb_cfg_regmap_config);
	if (IS_ERR(lb->reg_cfg))
		return PTR_ERR(lb->reg_cfg);

	lb->id = dc_subdev_get_id(dc_lb_info, ARRAY_SIZE(dc_lb_info), res_pec);
	if (lb->id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", lb->id);
		return lb->id;
	}

	lb->dev = dev;
	lb->link = LINK_ID_LAYERBLEND0 + lb->id;

	dc_drm->lb[lb->id] = lb;

	return 0;
}

static const struct component_ops dc_lb_ops = {
	.bind = dc_lb_bind,
};

static int dc_lb_probe(struct platform_device *pdev)
{
	int ret;

	ret = component_add(&pdev->dev, &dc_lb_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component\n");

	return 0;
}

static void dc_lb_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_lb_ops);
}

static const struct of_device_id dc_lb_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-layerblend" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_lb_dt_ids);

struct platform_driver dc_lb_driver = {
	.probe = dc_lb_probe,
	.remove = dc_lb_remove,
	.driver = {
		.name = "imx8-dc-layerblend",
		.suppress_bind_attrs = true,
		.of_match_table = dc_lb_dt_ids,
	},
};
