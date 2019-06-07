/*
 * Copyright (c) 2015, 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __QCOM_GDSC_H__
#define __QCOM_GDSC_H__

#include <linux/err.h>
#include <linux/pm_domain.h>

struct regmap;
struct reset_controller_dev;

/**
 * struct gdsc - Globally Distributed Switch Controller
 * @pd: generic power domain
 * @regmap: regmap for MMIO accesses
 * @gdscr: gsdc control register
 * @gds_hw_ctrl: gds_hw_ctrl register
 * @cxcs: offsets of branch registers to toggle mem/periph bits in
 * @cxc_count: number of @cxcs
 * @pwrsts: Possible powerdomain power states
 * @resets: ids of resets associated with this gdsc
 * @reset_count: number of @resets
 * @rcdev: reset controller
 */
struct gdsc {
	struct generic_pm_domain	pd;
	struct generic_pm_domain	*parent;
	struct regmap			*regmap;
	unsigned int			gdscr;
	unsigned int			gds_hw_ctrl;
	unsigned int			clamp_io_ctrl;
	unsigned int			*cxcs;
	unsigned int			cxc_count;
	const u8			pwrsts;
/* Powerdomain allowable state bitfields */
#define PWRSTS_OFF		BIT(0)
#define PWRSTS_RET		BIT(1)
#define PWRSTS_ON		BIT(2)
#define PWRSTS_OFF_ON		(PWRSTS_OFF | PWRSTS_ON)
#define PWRSTS_RET_ON		(PWRSTS_RET | PWRSTS_ON)
	const u8			flags;
#define VOTABLE		BIT(0)
#define CLAMP_IO	BIT(1)
#define HW_CTRL		BIT(2)
#define SW_RESET	BIT(3)
#define AON_RESET	BIT(4)
#define POLL_CFG_GDSCR	BIT(5)
#define ALWAYS_ON	BIT(6)
	struct reset_controller_dev	*rcdev;
	unsigned int			*resets;
	unsigned int			reset_count;
};

struct gdsc_desc {
	struct device *dev;
	struct gdsc **scs;
	size_t num;
};

#ifdef CONFIG_QCOM_GDSC
int gdsc_register(struct gdsc_desc *desc, struct reset_controller_dev *,
		  struct regmap *);
void gdsc_unregister(struct gdsc_desc *desc);
#else
static inline int gdsc_register(struct gdsc_desc *desc,
				struct reset_controller_dev *rcdev,
				struct regmap *r)
{
	return -ENOSYS;
}

static inline void gdsc_unregister(struct gdsc_desc *desc) {};
#endif /* CONFIG_QCOM_GDSC */
#endif /* __QCOM_GDSC_H__ */
