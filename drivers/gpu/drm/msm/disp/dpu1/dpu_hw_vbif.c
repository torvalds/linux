// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include <drm/drm_managed.h>

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_vbif.h"

#define VBIF_VERSION			0x0000
#define VBIF_CLK_FORCE_CTRL0		0x0008
#define VBIF_CLK_FORCE_CTRL1		0x000C
#define VBIF_QOS_REMAP_00		0x0020
#define VBIF_QOS_REMAP_01		0x0024
#define VBIF_QOS_REMAP_10		0x0028
#define VBIF_QOS_REMAP_11		0x002C
#define VBIF_WRITE_GATHER_EN		0x00AC
#define VBIF_IN_RD_LIM_CONF0		0x00B0
#define VBIF_IN_RD_LIM_CONF1		0x00B4
#define VBIF_IN_RD_LIM_CONF2		0x00B8
#define VBIF_IN_WR_LIM_CONF0		0x00C0
#define VBIF_IN_WR_LIM_CONF1		0x00C4
#define VBIF_IN_WR_LIM_CONF2		0x00C8
#define VBIF_OUT_RD_LIM_CONF0		0x00D0
#define VBIF_OUT_WR_LIM_CONF0		0x00D4
#define VBIF_OUT_AXI_AMEMTYPE_CONF0	0x0160
#define VBIF_OUT_AXI_AMEMTYPE_CONF1	0x0164
#define VBIF_XIN_PND_ERR		0x0190
#define VBIF_XIN_SRC_ERR		0x0194
#define VBIF_XIN_CLR_ERR		0x019C
#define VBIF_XIN_HALT_CTRL0		0x0200
#define VBIF_XIN_HALT_CTRL1		0x0204
#define VBIF_XINL_QOS_RP_REMAP_000	0x0550
#define VBIF_XINL_QOS_LVL_REMAP_000(vbif)	(VBIF_XINL_QOS_RP_REMAP_000 + (vbif)->cap->qos_rp_remap_size)

static void dpu_hw_clear_errors(struct dpu_hw_vbif *vbif,
		u32 *pnd_errors, u32 *src_errors)
{
	struct dpu_hw_blk_reg_map *c;
	u32 pnd, src;

	if (!vbif)
		return;
	c = &vbif->hw;
	pnd = DPU_REG_READ(c, VBIF_XIN_PND_ERR);
	src = DPU_REG_READ(c, VBIF_XIN_SRC_ERR);

	if (pnd_errors)
		*pnd_errors = pnd;
	if (src_errors)
		*src_errors = src;

	DPU_REG_WRITE(c, VBIF_XIN_CLR_ERR, pnd | src);
}

static void dpu_hw_set_mem_type(struct dpu_hw_vbif *vbif,
		u32 xin_id, u32 value)
{
	struct dpu_hw_blk_reg_map *c;
	u32 reg_off;
	u32 bit_off;
	u32 reg_val;

	/*
	 * Assume 4 bits per bit field, 8 fields per 32-bit register so
	 * 16 bit fields maximum across two registers
	 */
	if (!vbif || xin_id >= MAX_XIN_COUNT || xin_id >= 16)
		return;

	c = &vbif->hw;

	if (xin_id >= 8) {
		xin_id -= 8;
		reg_off = VBIF_OUT_AXI_AMEMTYPE_CONF1;
	} else {
		reg_off = VBIF_OUT_AXI_AMEMTYPE_CONF0;
	}
	bit_off = (xin_id & 0x7) * 4;
	reg_val = DPU_REG_READ(c, reg_off);
	reg_val &= ~(0x7 << bit_off);
	reg_val |= (value & 0x7) << bit_off;
	DPU_REG_WRITE(c, reg_off, reg_val);
}

static void dpu_hw_set_limit_conf(struct dpu_hw_vbif *vbif,
		u32 xin_id, bool rd, u32 limit)
{
	struct dpu_hw_blk_reg_map *c = &vbif->hw;
	u32 reg_val;
	u32 reg_off;
	u32 bit_off;

	if (rd)
		reg_off = VBIF_IN_RD_LIM_CONF0;
	else
		reg_off = VBIF_IN_WR_LIM_CONF0;

	reg_off += (xin_id / 4) * 4;
	bit_off = (xin_id % 4) * 8;
	reg_val = DPU_REG_READ(c, reg_off);
	reg_val &= ~(0xFF << bit_off);
	reg_val |= (limit) << bit_off;
	DPU_REG_WRITE(c, reg_off, reg_val);
}

static u32 dpu_hw_get_limit_conf(struct dpu_hw_vbif *vbif,
		u32 xin_id, bool rd)
{
	struct dpu_hw_blk_reg_map *c = &vbif->hw;
	u32 reg_val;
	u32 reg_off;
	u32 bit_off;
	u32 limit;

	if (rd)
		reg_off = VBIF_IN_RD_LIM_CONF0;
	else
		reg_off = VBIF_IN_WR_LIM_CONF0;

	reg_off += (xin_id / 4) * 4;
	bit_off = (xin_id % 4) * 8;
	reg_val = DPU_REG_READ(c, reg_off);
	limit = (reg_val >> bit_off) & 0xFF;

	return limit;
}

static void dpu_hw_set_halt_ctrl(struct dpu_hw_vbif *vbif,
		u32 xin_id, bool enable)
{
	struct dpu_hw_blk_reg_map *c = &vbif->hw;
	u32 reg_val;

	reg_val = DPU_REG_READ(c, VBIF_XIN_HALT_CTRL0);

	if (enable)
		reg_val |= BIT(xin_id);
	else
		reg_val &= ~BIT(xin_id);

	DPU_REG_WRITE(c, VBIF_XIN_HALT_CTRL0, reg_val);
}

static bool dpu_hw_get_halt_ctrl(struct dpu_hw_vbif *vbif,
		u32 xin_id)
{
	struct dpu_hw_blk_reg_map *c = &vbif->hw;
	u32 reg_val;

	reg_val = DPU_REG_READ(c, VBIF_XIN_HALT_CTRL1);

	return (reg_val & BIT(xin_id)) ? true : false;
}

static void dpu_hw_set_qos_remap(struct dpu_hw_vbif *vbif,
		u32 xin_id, u32 level, u32 remap_level)
{
	struct dpu_hw_blk_reg_map *c;
	u32 reg_lvl, reg_val, reg_val_lvl, mask, reg_high, reg_shift;

	if (!vbif)
		return;

	c = &vbif->hw;

	reg_lvl = VBIF_XINL_QOS_LVL_REMAP_000(vbif);
	reg_high = ((xin_id & 0x8) >> 3) * 4 + (level * 8);
	reg_shift = (xin_id & 0x7) * 4;

	reg_val = DPU_REG_READ(c, VBIF_XINL_QOS_RP_REMAP_000 + reg_high);
	reg_val_lvl = DPU_REG_READ(c, reg_lvl + reg_high);

	mask = 0x7 << reg_shift;

	reg_val &= ~mask;
	reg_val |= (remap_level << reg_shift) & mask;

	reg_val_lvl &= ~mask;
	reg_val_lvl |= (remap_level << reg_shift) & mask;

	DPU_REG_WRITE(c, VBIF_XINL_QOS_RP_REMAP_000 + reg_high, reg_val);
	DPU_REG_WRITE(c, reg_lvl + reg_high, reg_val_lvl);
}

static void dpu_hw_set_write_gather_en(struct dpu_hw_vbif *vbif, u32 xin_id)
{
	struct dpu_hw_blk_reg_map *c;
	u32 reg_val;

	if (!vbif || xin_id >= MAX_XIN_COUNT)
		return;

	c = &vbif->hw;

	reg_val = DPU_REG_READ(c, VBIF_WRITE_GATHER_EN);
	reg_val |= BIT(xin_id);
	DPU_REG_WRITE(c, VBIF_WRITE_GATHER_EN, reg_val);
}

static void _setup_vbif_ops(struct dpu_hw_vbif_ops *ops,
		unsigned long cap)
{
	ops->set_limit_conf = dpu_hw_set_limit_conf;
	ops->get_limit_conf = dpu_hw_get_limit_conf;
	ops->set_halt_ctrl = dpu_hw_set_halt_ctrl;
	ops->get_halt_ctrl = dpu_hw_get_halt_ctrl;
	if (test_bit(DPU_VBIF_QOS_REMAP, &cap))
		ops->set_qos_remap = dpu_hw_set_qos_remap;
	ops->set_mem_type = dpu_hw_set_mem_type;
	ops->clear_errors = dpu_hw_clear_errors;
	ops->set_write_gather_en = dpu_hw_set_write_gather_en;
}

/**
 * dpu_hw_vbif_init() - Initializes the VBIF driver for the passed
 * VBIF catalog entry.
 * @dev:  Corresponding device for devres management
 * @cfg:  VBIF catalog entry for which driver object is required
 * @addr: Mapped register io address of MDSS
 */
struct dpu_hw_vbif *dpu_hw_vbif_init(struct drm_device *dev,
				     const struct dpu_vbif_cfg *cfg,
				     void __iomem *addr)
{
	struct dpu_hw_vbif *c;

	c = drmm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	c->hw.blk_addr = addr + cfg->base;
	c->hw.log_mask = DPU_DBG_MASK_VBIF;

	/*
	 * Assign ops
	 */
	c->idx = cfg->id;
	c->cap = cfg;
	_setup_vbif_ops(&c->ops, c->cap->features);

	/* no need to register sub-range in dpu dbg, dump entire vbif io base */

	return c;
}
