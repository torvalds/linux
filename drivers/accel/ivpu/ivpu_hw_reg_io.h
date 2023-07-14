/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_HW_REG_IO_H__
#define __IVPU_HW_REG_IO_H__

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "ivpu_drv.h"

#define REG_POLL_SLEEP_US 50
#define REG_IO_ERROR      0xffffffff

#define REGB_RD32(reg)          ivpu_hw_reg_rd32(vdev, vdev->regb, (reg), #reg, __func__)
#define REGB_RD32_SILENT(reg)   readl(vdev->regb + (reg))
#define REGB_RD64(reg)          ivpu_hw_reg_rd64(vdev, vdev->regb, (reg), #reg, __func__)
#define REGB_WR32(reg, val)     ivpu_hw_reg_wr32(vdev, vdev->regb, (reg), (val), #reg, __func__)
#define REGB_WR64(reg, val)     ivpu_hw_reg_wr64(vdev, vdev->regb, (reg), (val), #reg, __func__)

#define REGV_RD32(reg)          ivpu_hw_reg_rd32(vdev, vdev->regv, (reg), #reg, __func__)
#define REGV_RD32_SILENT(reg)   readl(vdev->regv + (reg))
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

#define REGB_POLL(reg, var, cond, timeout_us) \
	read_poll_timeout(REGB_RD32_SILENT, var, cond, REG_POLL_SLEEP_US, timeout_us, false, reg)

#define REGV_POLL(reg, var, cond, timeout_us) \
	read_poll_timeout(REGV_RD32_SILENT, var, cond, REG_POLL_SLEEP_US, timeout_us, false, reg)

#define REGB_POLL_FLD(reg, fld, val, timeout_us) \
({ \
	u32 var; \
	REGB_POLL(reg, var, (FIELD_GET(reg##_##fld##_MASK, var) == (val)), timeout_us); \
})

#define REGV_POLL_FLD(reg, fld, val, timeout_us) \
({ \
	u32 var; \
	REGV_POLL(reg, var, (FIELD_GET(reg##_##fld##_MASK, var) == (val)), timeout_us); \
})

static inline u32
ivpu_hw_reg_rd32(struct ivpu_device *vdev, void __iomem *base, u32 reg,
		 const char *name, const char *func)
{
	u32 val = readl(base + reg);

	ivpu_dbg(vdev, REG, "%s RD: %s (0x%08x) => 0x%08x\n", func, name, reg, val);
	return val;
}

static inline u64
ivpu_hw_reg_rd64(struct ivpu_device *vdev, void __iomem *base, u32 reg,
		 const char *name, const char *func)
{
	u64 val = readq(base + reg);

	ivpu_dbg(vdev, REG, "%s RD: %s (0x%08x) => 0x%016llx\n", func, name, reg, val);
	return val;
}

static inline void
ivpu_hw_reg_wr32(struct ivpu_device *vdev, void __iomem *base, u32 reg, u32 val,
		 const char *name, const char *func)
{
	ivpu_dbg(vdev, REG, "%s WR: %s (0x%08x) <= 0x%08x\n", func, name, reg, val);
	writel(val, base + reg);
}

static inline void
ivpu_hw_reg_wr64(struct ivpu_device *vdev, void __iomem *base, u32 reg, u64 val,
		 const char *name, const char *func)
{
	ivpu_dbg(vdev, REG, "%s WR: %s (0x%08x) <= 0x%016llx\n", func, name, reg, val);
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
