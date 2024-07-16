/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2018-2021 NXP
 *   Dong Aisheng <aisheng.dong@nxp.com>
 */

#ifndef __IMX_CLK_SCU_H
#define __IMX_CLK_SCU_H

#include <linux/firmware/imx/sci.h>
#include <linux/of.h>

#define IMX_SCU_GPR_CLK_GATE	BIT(0)
#define IMX_SCU_GPR_CLK_DIV	BIT(1)
#define IMX_SCU_GPR_CLK_MUX	BIT(2)

struct imx_clk_scu_rsrc_table {
	const u32 *rsrc;
	u8 num;
};

extern struct list_head imx_scu_clks[];
extern const struct dev_pm_ops imx_clk_lpcg_scu_pm_ops;
extern const struct imx_clk_scu_rsrc_table imx_clk_scu_rsrc_imx8dxl;
extern const struct imx_clk_scu_rsrc_table imx_clk_scu_rsrc_imx8qxp;
extern const struct imx_clk_scu_rsrc_table imx_clk_scu_rsrc_imx8qm;

int imx_clk_scu_init(struct device_node *np,
		     const struct imx_clk_scu_rsrc_table *data);
struct clk_hw *imx_scu_of_clk_src_get(struct of_phandle_args *clkspec,
				      void *data);
struct clk_hw *imx_clk_scu_alloc_dev(const char *name,
				     const char * const *parents,
				     int num_parents, u32 rsrc_id, u8 clk_type);

struct clk_hw *__imx_clk_scu(struct device *dev, const char *name,
			     const char * const *parents, int num_parents,
			     u32 rsrc_id, u8 clk_type);

void imx_clk_scu_unregister(void);

struct clk_hw *__imx_clk_lpcg_scu(struct device *dev, const char *name,
				  const char *parent_name, unsigned long flags,
				  void __iomem *reg, u8 bit_idx, bool hw_gate);
void imx_clk_lpcg_scu_unregister(struct clk_hw *hw);

struct clk_hw *__imx_clk_gpr_scu(const char *name, const char * const *parent_name,
				 int num_parents, u32 rsrc_id, u8 gpr_id, u8 flags,
				 bool invert);

static inline struct clk_hw *imx_clk_scu(const char *name, u32 rsrc_id,
					 u8 clk_type)
{
	return imx_clk_scu_alloc_dev(name, NULL, 0, rsrc_id, clk_type);
}

static inline struct clk_hw *imx_clk_scu2(const char *name, const char * const *parents,
					  int num_parents, u32 rsrc_id, u8 clk_type)
{
	return imx_clk_scu_alloc_dev(name, parents, num_parents, rsrc_id, clk_type);
}

static inline struct clk_hw *imx_clk_lpcg_scu_dev(struct device *dev, const char *name,
						  const char *parent_name, unsigned long flags,
						  void __iomem *reg, u8 bit_idx, bool hw_gate)
{
	return __imx_clk_lpcg_scu(dev, name, parent_name, flags, reg,
				  bit_idx, hw_gate);
}

static inline struct clk_hw *imx_clk_lpcg_scu(const char *name, const char *parent_name,
					      unsigned long flags, void __iomem *reg,
					      u8 bit_idx, bool hw_gate)
{
	return __imx_clk_lpcg_scu(NULL, name, parent_name, flags, reg,
				  bit_idx, hw_gate);
}

static inline struct clk_hw *imx_clk_gate_gpr_scu(const char *name, const char *parent_name,
						  u32 rsrc_id, u8 gpr_id, bool invert)
{
	return __imx_clk_gpr_scu(name, &parent_name, 1, rsrc_id, gpr_id,
				 IMX_SCU_GPR_CLK_GATE, invert);
}

static inline struct clk_hw *imx_clk_divider_gpr_scu(const char *name, const char *parent_name,
						     u32 rsrc_id, u8 gpr_id)
{
	return __imx_clk_gpr_scu(name, &parent_name, 1, rsrc_id, gpr_id,
				 IMX_SCU_GPR_CLK_DIV, 0);
}

static inline struct clk_hw *imx_clk_mux_gpr_scu(const char *name, const char * const *parent_names,
						 int num_parents, u32 rsrc_id, u8 gpr_id)
{
	return __imx_clk_gpr_scu(name, parent_names, num_parents, rsrc_id,
				 gpr_id, IMX_SCU_GPR_CLK_MUX, 0);
}
#endif
