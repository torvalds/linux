/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, 2017-2018, 2022, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_GDSC_H__
#define __QCOM_GDSC_H__

#include <linux/err.h>
#include <linux/pm_domain.h>

struct regmap;
struct regulator;
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
 * @en_rest_wait_val: transition delay value for receiving enr ack signal
 * @en_few_wait_val: transition delay value for receiving enf ack signal
 * @clk_dis_wait_val: transition delay value for halting clock
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
	unsigned int			en_rest_wait_val;
	unsigned int			en_few_wait_val;
	unsigned int			clk_dis_wait_val;
	const u8			pwrsts;
/* Powerdomain allowable state bitfields */
#define PWRSTS_OFF		BIT(0)
#define PWRSTS_RET		BIT(1)
#define PWRSTS_ON		BIT(2)
#define PWRSTS_OFF_ON		(PWRSTS_OFF | PWRSTS_ON)
#define PWRSTS_RET_ON		(PWRSTS_RET | PWRSTS_ON)
	const u16			flags;
#define VOTABLE		BIT(0)
#define CLAMP_IO	BIT(1)
#define HW_CTRL		BIT(2)
#define SW_RESET	BIT(3)
#define AON_RESET	BIT(4)
#define POLL_CFG_GDSCR	BIT(5)
#define ALWAYS_ON	BIT(6)
#define RETAIN_FF_ENABLE	BIT(7)
#define NO_RET_PERIPH	BIT(8)
	struct reset_controller_dev	*rcdev;
	unsigned int			*resets;
	unsigned int			reset_count;

	const char 			*supply;
	struct regulator		*rsupply;
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
int gdsc_gx_do_nothing_enable(struct generic_pm_domain *domain);
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
