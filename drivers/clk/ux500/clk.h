/*
 * Clocks for ux500 platforms
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __UX500_CLK_H
#define __UX500_CLK_H

#include <linux/device.h>
#include <linux/types.h>

struct clk;

struct clk *clk_reg_prcc_pclk(const char *name,
			      const char *parent_name,
			      resource_size_t phy_base,
			      u32 cg_sel,
			      unsigned long flags);

struct clk *clk_reg_prcc_kclk(const char *name,
			      const char *parent_name,
			      resource_size_t phy_base,
			      u32 cg_sel,
			      unsigned long flags);

struct clk *clk_reg_prcmu_scalable(const char *name,
				   const char *parent_name,
				   u8 cg_sel,
				   unsigned long rate,
				   unsigned long flags);

struct clk *clk_reg_prcmu_gate(const char *name,
			       const char *parent_name,
			       u8 cg_sel,
			       unsigned long flags);

struct clk *clk_reg_prcmu_scalable_rate(const char *name,
					const char *parent_name,
					u8 cg_sel,
					unsigned long rate,
					unsigned long flags);

struct clk *clk_reg_prcmu_rate(const char *name,
			       const char *parent_name,
			       u8 cg_sel,
			       unsigned long flags);

struct clk *clk_reg_prcmu_opp_gate(const char *name,
				   const char *parent_name,
				   u8 cg_sel,
				   unsigned long flags);

struct clk *clk_reg_prcmu_opp_volt_scalable(const char *name,
					    const char *parent_name,
					    u8 cg_sel,
					    unsigned long rate,
					    unsigned long flags);

struct clk *clk_reg_sysctrl_gate(struct device *dev,
				 const char *name,
				 const char *parent_name,
				 u16 reg_sel,
				 u8 reg_mask,
				 u8 reg_bits,
				 unsigned long enable_delay_us,
				 unsigned long flags);

struct clk *clk_reg_sysctrl_gate_fixed_rate(struct device *dev,
					    const char *name,
					    const char *parent_name,
					    u16 reg_sel,
					    u8 reg_mask,
					    u8 reg_bits,
					    unsigned long rate,
					    unsigned long enable_delay_us,
					    unsigned long flags);

struct clk *clk_reg_sysctrl_set_parent(struct device *dev,
				       const char *name,
				       const char **parent_names,
				       u8 num_parents,
				       u16 *reg_sel,
				       u8 *reg_mask,
				       u8 *reg_bits,
				       unsigned long flags);

#endif /* __UX500_CLK_H */
