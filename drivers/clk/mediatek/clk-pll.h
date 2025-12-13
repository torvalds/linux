/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#ifndef __DRV_CLK_MTK_PLL_H
#define __DRV_CLK_MTK_PLL_H

#include <linux/clk-provider.h>
#include <linux/types.h>

struct clk_ops;
struct clk_hw_onecell_data;
struct device_node;

struct mtk_pll_div_table {
	u32 div;
	unsigned long freq;
};

#define HAVE_RST_BAR	BIT(0)
#define PLL_AO		BIT(1)
#define POSTDIV_MASK	GENMASK(2, 0)

struct mtk_pll_data {
	int id;
	const char *name;
	u32 reg;
	u32 pwr_reg;
	u32 en_mask;
	u32 fenc_sta_ofs;
	u32 pd_reg;
	u32 tuner_reg;
	u32 tuner_en_reg;
	u8 tuner_en_bit;
	int pd_shift;
	unsigned int flags;
	const struct clk_ops *ops;
	u32 rst_bar_mask;
	unsigned long fmin;
	unsigned long fmax;
	int pcwbits;
	int pcwibits;
	u32 pcw_reg;
	int pcw_shift;
	u32 pcw_chg_reg;
	const struct mtk_pll_div_table *div_table;
	const char *parent_name;
	u32 en_reg;
	u32 en_set_reg;
	u32 en_clr_reg;
	u8 pll_en_bit; /* Assume 0, indicates BIT(0) by default */
	u8 pcw_chg_bit;
	u8 fenc_sta_bit;
};

/*
 * MediaTek PLLs are configured through their pcw value. The pcw value describes
 * a divider in the PLL feedback loop which consists of 7 bits for the integer
 * part and the remaining bits (if present) for the fractional part. Also they
 * have a 3 bit power-of-two post divider.
 */

struct mtk_clk_pll {
	struct clk_hw	hw;
	void __iomem	*base_addr;
	void __iomem	*pd_addr;
	void __iomem	*pwr_addr;
	void __iomem	*tuner_addr;
	void __iomem	*tuner_en_addr;
	void __iomem	*pcw_addr;
	void __iomem	*pcw_chg_addr;
	void __iomem	*en_addr;
	void __iomem	*en_set_addr;
	void __iomem	*en_clr_addr;
	void __iomem	*fenc_addr;
	const struct mtk_pll_data *data;
};

int mtk_clk_register_plls(struct device_node *node,
			  const struct mtk_pll_data *plls, int num_plls,
			  struct clk_hw_onecell_data *clk_data);
void mtk_clk_unregister_plls(const struct mtk_pll_data *plls, int num_plls,
			     struct clk_hw_onecell_data *clk_data);

extern const struct clk_ops mtk_pll_ops;
extern const struct clk_ops mtk_pll_fenc_clr_set_ops;

static inline struct mtk_clk_pll *to_mtk_clk_pll(struct clk_hw *hw)
{
	return container_of(hw, struct mtk_clk_pll, hw);
}

int mtk_pll_is_prepared(struct clk_hw *hw);

int mtk_pll_prepare(struct clk_hw *hw);

void mtk_pll_unprepare(struct clk_hw *hw);

unsigned long mtk_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate);

void mtk_pll_calc_values(struct mtk_clk_pll *pll, u32 *pcw, u32 *postdiv,
			 u32 freq, u32 fin);
int mtk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		     unsigned long parent_rate);
int mtk_pll_determine_rate(struct clk_hw *hw, struct clk_rate_request *req);

struct clk_hw *mtk_clk_register_pll_ops(struct mtk_clk_pll *pll,
					const struct mtk_pll_data *data,
					void __iomem *base,
					const struct clk_ops *pll_ops);
struct clk_hw *mtk_clk_register_pll(const struct mtk_pll_data *data,
				    void __iomem *base);
void mtk_clk_unregister_pll(struct clk_hw *hw);

__iomem void *mtk_clk_pll_get_base(struct clk_hw *hw,
				   const struct mtk_pll_data *data);

#endif /* __DRV_CLK_MTK_PLL_H */
