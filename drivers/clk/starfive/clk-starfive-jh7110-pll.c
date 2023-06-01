// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 PLL Clock Generator Driver
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/debugfs.h>
#include <linux/init.h>

#include "clk-starfive-jh7110-pll.h"

static struct jh7110_clk_pll_data * __init
		jh7110_pll_data_from(struct clk_hw *hw)
{
	return container_of(hw, struct jh7110_clk_pll_data, hw);
}

static unsigned long pll_calculate_freq(struct jh7110_clk_pll_data *data)
{
	u32 dacpd;
	u32 dsmpd;
	u32 fbdiv;
	u32 prediv;
	u32 postdiv1;
	u32 frac;
	unsigned long refclk;
	u32 reg_value;
	unsigned long frac_cal;
	unsigned long freq;
	struct pll_syscon_offset *offset = &data->offset;
	struct pll_syscon_mask *mask = &data->mask;
	struct pll_syscon_shift *shift = &data->shift;

	if (regmap_read(data->sys_syscon_regmap,
			offset->dacpd_offset, &reg_value))
		goto read_register_error;
	dacpd = (reg_value & mask->dacpd_mask) >> shift->dacpd_shift;
	dev_dbg(data->dev, "pll%d read register dacpd:%d\n", data->idx, dacpd);

	if (regmap_read(data->sys_syscon_regmap,
			offset->dsmpd_offset, &reg_value))
		goto read_register_error;
	dsmpd = (reg_value & mask->dsmpd_mask) >> shift->dsmpd_shift;
	dev_dbg(data->dev, "pll%d read register dsmpd:%d\n", data->idx, dsmpd);

	if (regmap_read(data->sys_syscon_regmap,
			offset->fbdiv_offset, &reg_value))
		goto read_register_error;
	fbdiv = (reg_value & mask->fbdiv_mask) >> shift->fbdiv_shift;
	/* fbdiv value should be 8 to 4095 */
	if (fbdiv < 8)
		goto read_register_error;
	dev_dbg(data->dev, "pll%d read register fbdiv:%d\n", data->idx, fbdiv);

	if (regmap_read(data->sys_syscon_regmap,
			offset->prediv_offset, &reg_value))
		goto read_register_error;
	prediv = (reg_value & mask->prediv_mask) >> shift->prediv_shift;
	dev_dbg(data->dev, "pll%d read register prediv:%d\n", data->idx, prediv);

	if (regmap_read(data->sys_syscon_regmap,
			offset->postdiv1_offset, &reg_value))
		goto read_register_error;
	/* postdiv1 = 2^reg */
	postdiv1 = 1 << ((reg_value & mask->postdiv1_mask) >>
			shift->postdiv1_shift);
	dev_dbg(data->dev, "pll%d read register postdiv1:%d\n",
				data->idx, postdiv1);

	if (regmap_read(data->sys_syscon_regmap,
			offset->frac_offset, &reg_value))
		goto read_register_error;
	frac = (reg_value & mask->frac_mask) >> shift->frac_shift;
	dev_dbg(data->dev, "pll%d read register frac:0x%x\n", data->idx, frac);

	refclk = data->refclk_freq;
	/* Integer Mode or Fraction Mode */
	if ((dacpd == 1) && (dsmpd == 1))
		frac_cal = 0;
	else
		frac_cal = (unsigned long) frac * FRAC_PATR_SIZE / (1 << 24);

	freq = (unsigned long) refclk / FRAC_PATR_SIZE *
		(fbdiv * FRAC_PATR_SIZE + frac_cal) / prediv / postdiv1;

	dev_dbg(data->dev, "pll%d calculate freq:%ld\n", data->idx, freq);
	return freq;

read_register_error:
	return 0;
}

static unsigned long pll_get_freq(struct jh7110_clk_pll_data *data)
{
	unsigned long freq;

	freq = pll_calculate_freq(data);
	if (freq == 0) {
		dev_err(data->dev, "PLL calculate error or read syscon error.\n");
		return 0;
	}

	return freq;
}

static int pll_select_freq_syscon(struct jh7110_clk_pll_data *data,
				unsigned long target_rate)
{
	unsigned int id;
	unsigned int pll_arry_size;
	const struct starfive_pll_syscon_value *syscon_value;

	if (data->idx == PLL0_INDEX)
		pll_arry_size = ARRAY_SIZE(jh7110_pll0_syscon_freq);
	else if (data->idx == PLL1_INDEX)
		pll_arry_size = ARRAY_SIZE(jh7110_pll1_syscon_freq);
	else
		pll_arry_size = ARRAY_SIZE(jh7110_pll2_syscon_freq);

	for (id = 0; id < pll_arry_size; id++) {
		if (data->idx == PLL0_INDEX)
			syscon_value = &jh7110_pll0_syscon_freq[id];
		else if (data->idx == PLL1_INDEX)
			syscon_value = &jh7110_pll1_syscon_freq[id];
		else
			syscon_value = &jh7110_pll2_syscon_freq[id];

		if (target_rate == syscon_value->freq)
			goto select_end;
	}

	dev_err(data->dev, "pll%d frequency:%ld do not match, please check it.\n",
			data->idx, target_rate);
	return -EINVAL;

select_end:
	data->freq_select_idx = id;
	return 0;
}

static int pll_set_freq_syscon(struct jh7110_clk_pll_data *data)
{
	int ret;
	const struct starfive_pll_syscon_value *syscon_value;
	unsigned int freq_idx = data->freq_select_idx;
	struct pll_syscon_offset *offset = &data->offset;
	struct pll_syscon_mask *mask = &data->mask;
	struct pll_syscon_shift *shift = &data->shift;

	if (data->idx == PLL0_INDEX)
		syscon_value = &jh7110_pll0_syscon_freq[freq_idx];
	else if (data->idx == PLL1_INDEX)
		syscon_value = &jh7110_pll1_syscon_freq[freq_idx];
	else
		syscon_value = &jh7110_pll2_syscon_freq[freq_idx];

	dev_dbg(data->dev, "dacpd:offset=0x%x, mask=0x%x, shift=%d, value=%d\n",
			offset->dacpd_offset, mask->dacpd_mask,
			shift->dacpd_shift, syscon_value->dacpd);
	ret = regmap_update_bits(data->sys_syscon_regmap, offset->dacpd_offset,
		mask->dacpd_mask, (syscon_value->dacpd << shift->dacpd_shift));
	if (ret)
		goto set_failed;
	dev_dbg(data->dev, "dsmpd:offset=%x, mask=%x, shift=%d, value=%d\n",
			offset->dsmpd_offset, mask->dsmpd_mask,
			shift->dsmpd_shift, syscon_value->dsmpd);
	ret = regmap_update_bits(data->sys_syscon_regmap, offset->dsmpd_offset,
		mask->dsmpd_mask, (syscon_value->dsmpd << shift->dsmpd_shift));
	if (ret)
		goto set_failed;

	dev_dbg(data->dev, "prediv:offset=%x, mask=%x, shift=%d, value=%d\n",
			offset->prediv_offset, mask->prediv_mask,
			shift->prediv_shift, syscon_value->prediv);
	ret = regmap_update_bits(data->sys_syscon_regmap, offset->prediv_offset,
		mask->prediv_mask, (syscon_value->prediv << shift->prediv_shift));
	if (ret)
		goto set_failed;

	dev_dbg(data->dev, "fbdiv:offset=%x, mask=%x, shift=%d, value=%d\n",
			offset->fbdiv_offset, mask->fbdiv_mask,
			shift->fbdiv_shift, syscon_value->fbdiv);
	ret = regmap_update_bits(data->sys_syscon_regmap, offset->fbdiv_offset,
		mask->fbdiv_mask, (syscon_value->fbdiv << shift->fbdiv_shift));
	if (ret)
		goto set_failed;

	dev_dbg(data->dev, "postdiv:offset=0x%x, mask=0x%x, shift=%d, value=%d\n",
			offset->postdiv1_offset, mask->postdiv1_mask,
			shift->postdiv1_shift, syscon_value->postdiv1);
	ret = regmap_update_bits(data->sys_syscon_regmap,
		offset->postdiv1_offset, mask->postdiv1_mask,
		((syscon_value->postdiv1 >> 1) << shift->postdiv1_shift));
	if (ret)
		goto set_failed;
	/* frac */
	if ((syscon_value->dacpd == 0) && (syscon_value->dsmpd == 0)) {
		dev_dbg(data->dev, "frac:offset=0x%x mask=0x%x shift=%d value=0x%x\n",
			offset->frac_offset, mask->frac_mask,
			shift->frac_shift, syscon_value->frac);
		ret = regmap_update_bits(data->sys_syscon_regmap, offset->frac_offset,
				mask->frac_mask, (syscon_value->frac << shift->frac_shift));
		if (ret)
			goto set_failed;
	}

	dev_dbg(data->dev, "pll%d set syscon register done and rate is %ld\n",
				data->idx, syscon_value->freq);
	return 0;

set_failed:
	dev_err(data->dev, "pll set syscon failed:%d\n", ret);
	return ret;
}

static unsigned long jh7110_clk_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct jh7110_clk_pll_data *data = jh7110_pll_data_from(hw);

	return pll_get_freq(data);
}

static int jh7110_clk_pll_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	int ret;
	struct jh7110_clk_pll_data *data = jh7110_pll_data_from(hw);

	ret = pll_select_freq_syscon(data, req->rate);
	if (ret)
		return ret;

	if (data->idx == PLL0_INDEX)
		req->rate = jh7110_pll0_syscon_freq[data->freq_select_idx].freq;
	else if (data->idx == PLL1_INDEX)
		req->rate = jh7110_pll1_syscon_freq[data->freq_select_idx].freq;
	else
		req->rate = jh7110_pll2_syscon_freq[data->freq_select_idx].freq;

	return 0;
}

static int jh7110_clk_pll_set_rate(struct clk_hw *hw,
				unsigned long rate,
				unsigned long parent_rate)
{
	struct jh7110_clk_pll_data *data = jh7110_pll_data_from(hw);

	return pll_set_freq_syscon(data);

}

#ifdef CONFIG_DEBUG_FS
static void jh7110_clk_pll_debug_init(struct clk_hw *hw,
				struct dentry *dentry)
{
	static const struct debugfs_reg32 jh7110_clk_pll_reg = {
		.name = "CTRL",
		.offset = 0,
	};
	struct jh7110_clk_pll_data *data = jh7110_pll_data_from(hw);
	struct debugfs_regset32 *regset;

	regset = devm_kzalloc(data->dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	regset->regs = &jh7110_clk_pll_reg;
	regset->nregs = 1;

	debugfs_create_regset32("registers", 0400, dentry, regset);
}
#else
#define jh7110_clk_debug_init NULL
#endif

static const struct clk_ops jh7110_clk_pll_ops = {
	.recalc_rate = jh7110_clk_pll_recalc_rate,
	.determine_rate = jh7110_clk_pll_determine_rate,
	.set_rate = jh7110_clk_pll_set_rate,
	.debug_init = jh7110_clk_pll_debug_init,
};

static int pll_data_offset_get(struct jh7110_clk_pll_data *data,
			struct of_phandle_args *args, int index)
{
	struct pll_syscon_offset *offset = &data->offset;
	struct pll_syscon_mask *mask = &data->mask;
	struct pll_syscon_shift *shift = &data->shift;

	if (index == PLL0_INDEX) {
		offset->dacpd_offset = args->args[0];
		offset->dsmpd_offset = args->args[0];
		offset->fbdiv_offset = args->args[1];
		offset->frac_offset = args->args[2];
		offset->prediv_offset = args->args[3];
		offset->postdiv1_offset = args->args[2];

		mask->dacpd_mask = PLL0_DACPD_MASK;
		mask->dsmpd_mask = PLL0_DSMPD_MASK;
		mask->fbdiv_mask = PLL0_FBDIV_MASK;
		mask->frac_mask = PLL0_FRAC_MASK;
		mask->prediv_mask = PLL0_PREDIV_MASK;
		mask->postdiv1_mask = PLL0_POSTDIV1_MASK;

		shift->dacpd_shift = PLL0_DACPD_SHIFT;
		shift->dsmpd_shift = PLL0_DSMPD_SHIFT;
		shift->fbdiv_shift = PLL0_FBDIV_SHIFT;
		shift->frac_shift = PLL0_FRAC_SHIFT;
		shift->prediv_shift = PLL0_PREDIV_SHIFT;
		shift->postdiv1_shift = PLL0_POSTDIV1_SHIFT;
	} else if (index == PLL1_INDEX) {
		offset->dacpd_offset = args->args[3];
		offset->dsmpd_offset = args->args[3];
		offset->fbdiv_offset = args->args[3];
		offset->frac_offset = args->args[4];
		offset->prediv_offset = args->args[5];
		offset->postdiv1_offset = args->args[4];

		mask->dacpd_mask = PLL1_DACPD_MASK;
		mask->dsmpd_mask = PLL1_DSMPD_MASK;
		mask->fbdiv_mask = PLL1_FBDIV_MASK;
		mask->frac_mask = PLL1_FRAC_MASK;
		mask->prediv_mask = PLL1_PREDIV_MASK;
		mask->postdiv1_mask = PLL1_POSTDIV1_MASK;

		shift->dacpd_shift = PLL1_DACPD_SHIFT;
		shift->dsmpd_shift = PLL1_DSMPD_SHIFT;
		shift->fbdiv_shift = PLL1_FBDIV_SHIFT;
		shift->frac_shift = PLL1_FRAC_SHIFT;
		shift->prediv_shift = PLL1_PREDIV_SHIFT;
		shift->postdiv1_shift = PLL1_POSTDIV1_SHIFT;
	} else if (index == PLL2_INDEX) {
		offset->dacpd_offset = args->args[5];
		offset->dsmpd_offset = args->args[5];
		offset->fbdiv_offset = args->args[5];
		offset->frac_offset = args->args[6];
		offset->prediv_offset = args->args[7];
		offset->postdiv1_offset = args->args[6];

		mask->dacpd_mask = PLL2_DACPD_MASK;
		mask->dsmpd_mask = PLL2_DSMPD_MASK;
		mask->fbdiv_mask = PLL2_FBDIV_MASK;
		mask->frac_mask = PLL2_FRAC_MASK;
		mask->prediv_mask = PLL2_PREDIV_MASK;
		mask->postdiv1_mask = PLL2_POSTDIV1_MASK;

		shift->dacpd_shift = PLL2_DACPD_SHIFT;
		shift->dsmpd_shift = PLL2_DSMPD_SHIFT;
		shift->fbdiv_shift = PLL2_FBDIV_SHIFT;
		shift->frac_shift = PLL2_FRAC_SHIFT;
		shift->prediv_shift = PLL2_PREDIV_SHIFT;
		shift->postdiv1_shift = PLL2_POSTDIV1_SHIFT;
	} else
		return -ENOENT;

	return 0;
}

int __init clk_starfive_jh7110_pll_init(struct platform_device *pdev,
			struct jh7110_clk_pll_data *pll_priv)
{
	int ret;
	struct of_phandle_args args;
	struct regmap *pll_syscon_regmap;
	unsigned int idx;
	struct clk *osc_clk;
	unsigned long refclk_freq;
	struct jh7110_clk_pll_data *data;
	char *pll_name[3] = {
		"pll0_out",
		"pll1_out",
		"pll2_out"
	};

	ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
				"starfive,sys-syscon", 8, 0, &args);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to parse starfive,sys-syscon\n");
		goto pll_init_failed;
	}

	pll_syscon_regmap = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	if (IS_ERR(pll_syscon_regmap)) {
		ret = PTR_ERR(pll_syscon_regmap);
		goto pll_init_failed;
	}

	osc_clk = clk_get(&pdev->dev, "osc");
	if (!IS_ERR(osc_clk)) {
		refclk_freq = clk_get_rate(osc_clk);
		clk_put(osc_clk);
	} else {
		ret = PTR_ERR(osc_clk);
		dev_err(&pdev->dev, "get osc clk failed :%d.\n", ret);
		goto pll_init_failed;
	}

	for (idx = 0; idx < PLL_INDEX_MAX; idx++) {
		struct clk_parent_data parents = {
			.fw_name = "osc",
		};
		struct clk_init_data init = {
			.name = pll_name[idx],
			.ops = &jh7110_clk_pll_ops,
			.parent_data = &parents,
			.num_parents = 1,
			.flags = 0,
		};

		data = &pll_priv[idx];
		data->dev = &pdev->dev;
		data->sys_syscon_regmap = pll_syscon_regmap;

		ret = pll_data_offset_get(data, &args, idx);
		if (ret)
			goto pll_init_failed;

		data->hw.init = &init;
		data->idx = idx;
		data->refclk_freq = refclk_freq;

		ret = devm_clk_hw_register(&pdev->dev, &data->hw);
		if (ret)
			return ret;
	}

	dev_dbg(&pdev->dev, "PLL0, PLL1 and PLL2 clock registered done\n");

/* Change PLL2 rate before other driver up */
	if (PLL2_DEFAULT_FREQ) {
		struct clk *pll2_clk = pll_priv[PLL2_INDEX].hw.clk;

		if (clk_set_rate(pll2_clk, PLL2_DEFAULT_FREQ))
			dev_info(&pdev->dev, "set pll2 failed\n");
	}

	return 0;

pll_init_failed:
	return ret;
}



