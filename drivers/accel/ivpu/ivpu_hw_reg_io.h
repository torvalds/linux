/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_HW_REG_IO_H__
#define __IVPU_HW_REG_IO_H__

#include <linux/bitfield.h>
#include <linux/fault-inject.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "ivpu_drv.h"

#define REG_POLL_SLEEP_US 50
#define REG_IO_ERROR      0xffffffff

#define REGB_RD32(reg)          ivpu_hw_reg_rd32(vdev, vdev->regb, (reg), #reg, __func__)
#define REGB_RD64(reg)          ivpu_hw_reg_rd64(vdev, vdev->regb, (reg), #reg, __func__)
#define REGB_WR32(reg, val)     ivpu_hw_reg_wr32(vdev, vdev->regb, (reg), (val), #reg, __func__)
#define REGB_WR64(reg, val)     ivpu_hw_reg_wr64(vdev, vdev->regb, (reg), (val), #reg, __func__)

#define REGV_RD32(reg)          ivpu_hw_reg_rd32(vdev, vdev->regv, (reg), #reg, __func__)
#define REGV_RD64(reg)          ivpu_hw_reg_rd64(vdev, vdev->regv, (reg), #reg, __func__)
#define REGV_WR32(reg, val)     ivpu_hw_reg_wr32(vdev, vdev->regv, (reg), (val), #reg, __func__)
#define REGV_WR64(reg, val)     ivpu_hw_reg_wr64(vdev, vdev->regv, (reg), (val), #reg, __func__)

#define REGV_WR32I(reg, stride, index, val) \
	ivpu_hw_reg_wr32_index(vdev, vdev->regv, (reg), (stride), (index), (val), #reg, __func__)

#define REG_FLD(REG, FLD) \
	(REG##_##FLD##_MASK)
#define REG_FLD_NUM(REG, FLD, num) \
	FIELD_PREP(REG##_##FLD##_MASK, num)
#define REG_GET_FLD(REG, FLD, val) \
	FIELD_GET(REG##_##FLD##_MASK, val)
#define REG_CLR_FLD(REG, FLD, val) \
	((val) & ~(REG##_##FLD##_MASK))
#define REG_SET_FLD(REG, FLD, val) \
	((val) | (REG##_##FLD##_MASK))
#define REG_SET_FLD_NUM(REG, FLD, num, val) \
	(((val) & ~(REG##_##FLD##_MASK)) | FIELD_PREP(REG##_##FLD##_MASK, num))
#define REG_TEST_FLD(REG, FLD, val) \
	((REG##_##FLD##_MASK) == ((val) & (REG##_##FLD##_MASK)))
#define REG_TEST_FLD_NUM(REG, FLD, num, val) \
	((num) == FIELD_GET(REG##_##FLD##_MASK, val))

#define REGB_POLL_FLD(reg, fld, exp_fld_val, timeout_us) \
	ivpu_hw_reg_poll_fld(vdev, vdev->regb, reg, reg##_##fld##_MASK, \
			     FIELD_PREP(reg##_##fld##_MASK, exp_fld_val), timeout_us, \
			     __func__, #reg, #fld)

#define REGV_POLL_FLD(reg, fld, exp_fld_val, timeout_us) \
	ivpu_hw_reg_poll_fld(vdev, vdev->regv, reg, reg##_##fld##_MASK, \
			     FIELD_PREP(reg##_##fld##_MASK, exp_fld_val), timeout_us, \
			     __func__, #reg, #fld)

extern struct fault_attr ivpu_hw_failure;

static inline int __must_check
ivpu_hw_reg_poll_fld(struct ivpu_device *vdev, void __iomem *base,
		     u32 reg_offset, u32 reg_mask, u32 exp_masked_val, u32 timeout_us,
		     const char *func_name, const char *reg_name, const char *fld_name)
{
	u32 reg_val;
	int ret;

	ivpu_dbg(vdev, REG, "%s : %s (0x%08x) POLL %s started (exp_val 0x%x)\n",
		 func_name, reg_name, reg_offset, fld_name, exp_masked_val);

	ret = read_poll_timeout(readl, reg_val, (reg_val & reg_mask) == exp_masked_val,
				REG_POLL_SLEEP_US, timeout_us, false, base + reg_offset);

#ifdef CONFIG_FAULT_INJECTION
	if (should_fail(&ivpu_hw_failure, 1))
		ret = -ETIMEDOUT;
#endif

	ivpu_dbg(vdev, REG, "%s : %s (0x%08x) POLL %s %s (reg_val 0x%08x)\n",
		 func_name, reg_name, reg_offset, fld_name, ret ? "ETIMEDOUT" : "OK", reg_val);

	return ret;
}

static inline u32
ivpu_hw_reg_rd32(struct ivpu_device *vdev, void __iomem *base, u32 reg,
		 const char *name, const char *func)
{
	u32 val = readl(base + reg);

	ivpu_dbg(vdev, REG, "%s : %s (0x%08x) RD: 0x%08x\n", func, name, reg, val);
	return val;
}

static inline u64
ivpu_hw_reg_rd64(struct ivpu_device *vdev, void __iomem *base, u32 reg,
		 const char *name, const char *func)
{
	u64 val = readq(base + reg);

	ivpu_dbg(vdev, REG, "%s : %s (0x%08x) RD: 0x%016llx\n", func, name, reg, val);
	return val;
}

static inline void
ivpu_hw_reg_wr32(struct ivpu_device *vdev, void __iomem *base, u32 reg, u32 val,
		 const char *name, const char *func)
{
	ivpu_dbg(vdev, REG, "%s : %s (0x%08x) WR: 0x%08x\n", func, name, reg, val);
	writel(val, base + reg);
}

static inline void
ivpu_hw_reg_wr64(struct ivpu_device *vdev, void __iomem *base, u32 reg, u64 val,
		 const char *name, const char *func)
{
	ivpu_dbg(vdev, REG, "%s : %s (0x%08x) WR: 0x%016llx\n", func, name, reg, val);
	writeq(val, base + reg);
}

static inline void
ivpu_hw_reg_wr32_index(struct ivpu_device *vdev, void __iomem *base, u32 reg,
		       u32 stride, u32 index, u32 val, const char *name,
		       const char *func)
{
	reg += index * stride;

	ivpu_dbg(vdev, REG, "%s WR: %s_%d (0x%08x) <= 0x%08x\n", func, name, index, reg, val);
	writel(val, base + reg);
}

#endif /* __IVPU_HW_REG_IO_H__ */
