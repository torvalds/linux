/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2013, The Linux Foundation. All rights reserved. */

#ifndef __QCOM_CLK_BRANCH_H__
#define __QCOM_CLK_BRANCH_H__

#include <linux/bitfield.h>
#include <linux/clk-provider.h>

#include "clk-regmap.h"

/**
 * struct clk_branch - gating clock with status bit and dynamic hardware gating
 *
 * @hwcg_reg: dynamic hardware clock gating register
 * @hwcg_bit: ORed with @hwcg_reg to enable dynamic hardware clock gating
 * @halt_reg: halt register
 * @halt_bit: ANDed with @halt_reg to test for clock halted
 * @halt_check: type of halt checking to perform
 * @clkr: handle between common and hardware-specific interfaces
 *
 * Clock which can gate its output.
 */
struct clk_branch {
	u32	hwcg_reg;
	u32	halt_reg;
	u8	hwcg_bit;
	u8	halt_bit;
	u8	halt_check;
#define BRANCH_VOTED			BIT(7) /* Delay on disable */
#define BRANCH_HALT			0 /* pol: 1 = halt */
#define BRANCH_HALT_VOTED		(BRANCH_HALT | BRANCH_VOTED)
#define BRANCH_HALT_ENABLE		1 /* pol: 0 = halt */
#define BRANCH_HALT_ENABLE_VOTED	(BRANCH_HALT_ENABLE | BRANCH_VOTED)
#define BRANCH_HALT_DELAY		2 /* No bit to check; just delay */
#define BRANCH_HALT_SKIP		3 /* Don't check halt bit */

	struct clk_regmap clkr;
};

/**
 * struct clk_mem_branch - gating clock which are associated with memories
 *
 * @mem_enable_reg: branch clock memory gating register
 * @mem_ack_reg: branch clock memory ack register
 * @mem_enable_ack_mask: branch clock memory enable and ack field in @mem_ack_reg
 * @branch: branch clock gating handle
 *
 * Clock which can gate its memories.
 */
struct clk_mem_branch {
	u32	mem_enable_reg;
	u32	mem_ack_reg;
	u32	mem_enable_ack_mask;
	struct clk_branch branch;
};

/* Branch clock common bits for HLOS-owned clocks */
#define CBCR_CLK_OFF			BIT(31)
#define CBCR_NOC_FSM_STATUS		GENMASK(30, 28)
 #define FSM_STATUS_ON			BIT(1)
#define CBCR_FORCE_MEM_CORE_ON		BIT(14)
#define CBCR_FORCE_MEM_PERIPH_ON	BIT(13)
#define CBCR_FORCE_MEM_PERIPH_OFF	BIT(12)
#define CBCR_WAKEUP			GENMASK(11, 8)
#define CBCR_SLEEP			GENMASK(7, 4)

static inline void qcom_branch_set_force_mem_core(struct regmap *regmap,
						  struct clk_branch clk, bool on)
{
	regmap_update_bits(regmap, clk.halt_reg, CBCR_FORCE_MEM_CORE_ON,
			   on ? CBCR_FORCE_MEM_CORE_ON : 0);
}

static inline void qcom_branch_set_force_periph_on(struct regmap *regmap,
						   struct clk_branch clk, bool on)
{
	regmap_update_bits(regmap, clk.halt_reg, CBCR_FORCE_MEM_PERIPH_ON,
			   on ? CBCR_FORCE_MEM_PERIPH_ON : 0);
}

static inline void qcom_branch_set_force_periph_off(struct regmap *regmap,
						    struct clk_branch clk, bool on)
{
	regmap_update_bits(regmap, clk.halt_reg, CBCR_FORCE_MEM_PERIPH_OFF,
			   on ? CBCR_FORCE_MEM_PERIPH_OFF : 0);
}

static inline void qcom_branch_set_wakeup(struct regmap *regmap, struct clk_branch clk, u32 val)
{
	regmap_update_bits(regmap, clk.halt_reg, CBCR_WAKEUP,
			   FIELD_PREP(CBCR_WAKEUP, val));
}

static inline void qcom_branch_set_sleep(struct regmap *regmap, struct clk_branch clk, u32 val)
{
	regmap_update_bits(regmap, clk.halt_reg, CBCR_SLEEP,
			   FIELD_PREP(CBCR_SLEEP, val));
}

extern const struct clk_ops clk_branch_ops;
extern const struct clk_ops clk_branch2_ops;
extern const struct clk_ops clk_branch_simple_ops;
extern const struct clk_ops clk_branch2_aon_ops;
extern const struct clk_ops clk_branch2_mem_ops;

#define to_clk_branch(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_branch, clkr)

#define to_clk_mem_branch(_hw) \
	container_of(to_clk_branch(_hw), struct clk_mem_branch, branch)

#endif
