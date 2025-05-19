// SPDX-License-Identifier: GPL-2.0-only
/*
 * AXI clkgen driver
 *
 * Copyright 2012-2013 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/err.h>

#define AXI_CLKGEN_V2_REG_RESET		0x40
#define AXI_CLKGEN_V2_REG_CLKSEL	0x44
#define AXI_CLKGEN_V2_REG_DRP_CNTRL	0x70
#define AXI_CLKGEN_V2_REG_DRP_STATUS	0x74

#define AXI_CLKGEN_V2_RESET_MMCM_ENABLE	BIT(1)
#define AXI_CLKGEN_V2_RESET_ENABLE	BIT(0)

#define AXI_CLKGEN_V2_DRP_CNTRL_SEL	BIT(29)
#define AXI_CLKGEN_V2_DRP_CNTRL_READ	BIT(28)

#define AXI_CLKGEN_V2_DRP_STATUS_BUSY	BIT(16)

#define MMCM_REG_CLKOUT5_2	0x07
#define MMCM_REG_CLKOUT0_1	0x08
#define MMCM_REG_CLKOUT0_2	0x09
#define MMCM_REG_CLKOUT6_2	0x13
#define MMCM_REG_CLK_FB1	0x14
#define MMCM_REG_CLK_FB2	0x15
#define MMCM_REG_CLK_DIV	0x16
#define MMCM_REG_LOCK1		0x18
#define MMCM_REG_LOCK2		0x19
#define MMCM_REG_LOCK3		0x1a
#define MMCM_REG_POWER		0x28
#define MMCM_REG_FILTER1	0x4e
#define MMCM_REG_FILTER2	0x4f

#define MMCM_CLKOUT_NOCOUNT	BIT(6)

#define MMCM_CLK_DIV_DIVIDE	BIT(11)
#define MMCM_CLK_DIV_NOCOUNT	BIT(12)

struct axi_clkgen_limits {
	unsigned int fpfd_min;
	unsigned int fpfd_max;
	unsigned int fvco_min;
	unsigned int fvco_max;
};

struct axi_clkgen {
	void __iomem *base;
	struct clk_hw clk_hw;
	struct axi_clkgen_limits limits;
};

static uint32_t axi_clkgen_lookup_filter(unsigned int m)
{
	switch (m) {
	case 0:
		return 0x01001990;
	case 1:
		return 0x01001190;
	case 2:
		return 0x01009890;
	case 3:
		return 0x01001890;
	case 4:
		return 0x01008890;
	case 5 ... 8:
		return 0x01009090;
	case 9 ... 11:
		return 0x01000890;
	case 12:
		return 0x08009090;
	case 13 ... 22:
		return 0x01001090;
	case 23 ... 36:
		return 0x01008090;
	case 37 ... 46:
		return 0x08001090;
	default:
		return 0x08008090;
	}
}

static const uint32_t axi_clkgen_lock_table[] = {
	0x060603e8, 0x060603e8, 0x080803e8, 0x0b0b03e8,
	0x0e0e03e8, 0x111103e8, 0x131303e8, 0x161603e8,
	0x191903e8, 0x1c1c03e8, 0x1f1f0384, 0x1f1f0339,
	0x1f1f02ee, 0x1f1f02bc, 0x1f1f028a, 0x1f1f0271,
	0x1f1f023f, 0x1f1f0226, 0x1f1f020d, 0x1f1f01f4,
	0x1f1f01db, 0x1f1f01c2, 0x1f1f01a9, 0x1f1f0190,
	0x1f1f0190, 0x1f1f0177, 0x1f1f015e, 0x1f1f015e,
	0x1f1f0145, 0x1f1f0145, 0x1f1f012c, 0x1f1f012c,
	0x1f1f012c, 0x1f1f0113, 0x1f1f0113, 0x1f1f0113,
};

static uint32_t axi_clkgen_lookup_lock(unsigned int m)
{
	if (m < ARRAY_SIZE(axi_clkgen_lock_table))
		return axi_clkgen_lock_table[m];
	return 0x1f1f00fa;
}

static const struct axi_clkgen_limits axi_clkgen_zynqmp_default_limits = {
	.fpfd_min = 10000,
	.fpfd_max = 450000,
	.fvco_min = 800000,
	.fvco_max = 1600000,
};

static const struct axi_clkgen_limits axi_clkgen_zynq_default_limits = {
	.fpfd_min = 10000,
	.fpfd_max = 450000,
	.fvco_min = 600000,
	.fvco_max = 1200000,
};

static void axi_clkgen_calc_params(const struct axi_clkgen_limits *limits,
	unsigned long fin, unsigned long fout,
	unsigned int *best_d, unsigned int *best_m, unsigned int *best_dout)
{
	unsigned long d, d_min, d_max, _d_min, _d_max;
	unsigned long m, m_min, m_max;
	unsigned long f, dout, best_f, fvco;
	unsigned long fract_shift = 0;
	unsigned long fvco_min_fract, fvco_max_fract;

	fin /= 1000;
	fout /= 1000;

	best_f = ULONG_MAX;
	*best_d = 0;
	*best_m = 0;
	*best_dout = 0;

	d_min = max_t(unsigned long, DIV_ROUND_UP(fin, limits->fpfd_max), 1);
	d_max = min_t(unsigned long, fin / limits->fpfd_min, 80);

again:
	fvco_min_fract = limits->fvco_min << fract_shift;
	fvco_max_fract = limits->fvco_max << fract_shift;

	m_min = max_t(unsigned long, DIV_ROUND_UP(fvco_min_fract, fin) * d_min, 1);
	m_max = min_t(unsigned long, fvco_max_fract * d_max / fin, 64 << fract_shift);

	for (m = m_min; m <= m_max; m++) {
		_d_min = max(d_min, DIV_ROUND_UP(fin * m, fvco_max_fract));
		_d_max = min(d_max, fin * m / fvco_min_fract);

		for (d = _d_min; d <= _d_max; d++) {
			fvco = fin * m / d;

			dout = DIV_ROUND_CLOSEST(fvco, fout);
			dout = clamp_t(unsigned long, dout, 1, 128 << fract_shift);
			f = fvco / dout;
			if (abs(f - fout) < abs(best_f - fout)) {
				best_f = f;
				*best_d = d;
				*best_m = m << (3 - fract_shift);
				*best_dout = dout << (3 - fract_shift);
				if (best_f == fout)
					return;
			}
		}
	}

	/* Lets see if we find a better setting in fractional mode */
	if (fract_shift == 0) {
		fract_shift = 3;
		goto again;
	}
}

struct axi_clkgen_div_params {
	unsigned int low;
	unsigned int high;
	unsigned int edge;
	unsigned int nocount;
	unsigned int frac_en;
	unsigned int frac;
	unsigned int frac_wf_f;
	unsigned int frac_wf_r;
	unsigned int frac_phase;
};

static void axi_clkgen_calc_clk_params(unsigned int divider,
	unsigned int frac_divider, struct axi_clkgen_div_params *params)
{

	memset(params, 0x0, sizeof(*params));

	if (divider == 1) {
		params->nocount = 1;
		return;
	}

	if (frac_divider == 0) {
		params->high = divider / 2;
		params->edge = divider % 2;
		params->low = divider - params->high;
	} else {
		params->frac_en = 1;
		params->frac = frac_divider;

		params->high = divider / 2;
		params->edge = divider % 2;
		params->low = params->high;

		if (params->edge == 0) {
			params->high--;
			params->frac_wf_r = 1;
		}

		if (params->edge == 0 || frac_divider == 1)
			params->low--;
		if (((params->edge == 0) ^ (frac_divider == 1)) ||
			(divider == 2 && frac_divider == 1))
			params->frac_wf_f = 1;

		params->frac_phase = params->edge * 4 + frac_divider / 2;
	}
}

static void axi_clkgen_write(struct axi_clkgen *axi_clkgen,
	unsigned int reg, unsigned int val)
{
	writel(val, axi_clkgen->base + reg);
}

static void axi_clkgen_read(struct axi_clkgen *axi_clkgen,
	unsigned int reg, unsigned int *val)
{
	*val = readl(axi_clkgen->base + reg);
}

static int axi_clkgen_wait_non_busy(struct axi_clkgen *axi_clkgen)
{
	unsigned int timeout = 10000;
	unsigned int val;

	do {
		axi_clkgen_read(axi_clkgen, AXI_CLKGEN_V2_REG_DRP_STATUS, &val);
	} while ((val & AXI_CLKGEN_V2_DRP_STATUS_BUSY) && --timeout);

	if (val & AXI_CLKGEN_V2_DRP_STATUS_BUSY)
		return -EIO;

	return val & 0xffff;
}

static int axi_clkgen_mmcm_read(struct axi_clkgen *axi_clkgen,
	unsigned int reg, unsigned int *val)
{
	unsigned int reg_val;
	int ret;

	ret = axi_clkgen_wait_non_busy(axi_clkgen);
	if (ret < 0)
		return ret;

	reg_val = AXI_CLKGEN_V2_DRP_CNTRL_SEL | AXI_CLKGEN_V2_DRP_CNTRL_READ;
	reg_val |= (reg << 16);

	axi_clkgen_write(axi_clkgen, AXI_CLKGEN_V2_REG_DRP_CNTRL, reg_val);

	ret = axi_clkgen_wait_non_busy(axi_clkgen);
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

static int axi_clkgen_mmcm_write(struct axi_clkgen *axi_clkgen,
	unsigned int reg, unsigned int val, unsigned int mask)
{
	unsigned int reg_val = 0;
	int ret;

	ret = axi_clkgen_wait_non_busy(axi_clkgen);
	if (ret < 0)
		return ret;

	if (mask != 0xffff) {
		axi_clkgen_mmcm_read(axi_clkgen, reg, &reg_val);
		reg_val &= ~mask;
	}

	reg_val |= AXI_CLKGEN_V2_DRP_CNTRL_SEL | (reg << 16) | (val & mask);

	axi_clkgen_write(axi_clkgen, AXI_CLKGEN_V2_REG_DRP_CNTRL, reg_val);

	return 0;
}

static void axi_clkgen_mmcm_enable(struct axi_clkgen *axi_clkgen,
	bool enable)
{
	unsigned int val = AXI_CLKGEN_V2_RESET_ENABLE;

	if (enable)
		val |= AXI_CLKGEN_V2_RESET_MMCM_ENABLE;

	axi_clkgen_write(axi_clkgen, AXI_CLKGEN_V2_REG_RESET, val);
}

static struct axi_clkgen *clk_hw_to_axi_clkgen(struct clk_hw *clk_hw)
{
	return container_of(clk_hw, struct axi_clkgen, clk_hw);
}

static void axi_clkgen_set_div(struct axi_clkgen *axi_clkgen,
	unsigned int reg1, unsigned int reg2, unsigned int reg3,
	struct axi_clkgen_div_params *params)
{
	axi_clkgen_mmcm_write(axi_clkgen, reg1,
		(params->high << 6) | params->low, 0xefff);
	axi_clkgen_mmcm_write(axi_clkgen, reg2,
		(params->frac << 12) | (params->frac_en << 11) |
		(params->frac_wf_r << 10) | (params->edge << 7) |
		(params->nocount << 6), 0x7fff);
	if (reg3 != 0) {
		axi_clkgen_mmcm_write(axi_clkgen, reg3,
			(params->frac_phase << 11) | (params->frac_wf_f << 10), 0x3c00);
	}
}

static int axi_clkgen_set_rate(struct clk_hw *clk_hw,
	unsigned long rate, unsigned long parent_rate)
{
	struct axi_clkgen *axi_clkgen = clk_hw_to_axi_clkgen(clk_hw);
	const struct axi_clkgen_limits *limits = &axi_clkgen->limits;
	unsigned int d, m, dout;
	struct axi_clkgen_div_params params;
	uint32_t power = 0;
	uint32_t filter;
	uint32_t lock;

	if (parent_rate == 0 || rate == 0)
		return -EINVAL;

	axi_clkgen_calc_params(limits, parent_rate, rate, &d, &m, &dout);

	if (d == 0 || dout == 0 || m == 0)
		return -EINVAL;

	if ((dout & 0x7) != 0 || (m & 0x7) != 0)
		power |= 0x9800;

	axi_clkgen_mmcm_write(axi_clkgen, MMCM_REG_POWER, power, 0x9800);

	filter = axi_clkgen_lookup_filter(m - 1);
	lock = axi_clkgen_lookup_lock(m - 1);

	axi_clkgen_calc_clk_params(dout >> 3, dout & 0x7, &params);
	axi_clkgen_set_div(axi_clkgen,  MMCM_REG_CLKOUT0_1, MMCM_REG_CLKOUT0_2,
		MMCM_REG_CLKOUT5_2, &params);

	axi_clkgen_calc_clk_params(d, 0, &params);
	axi_clkgen_mmcm_write(axi_clkgen, MMCM_REG_CLK_DIV,
		(params.edge << 13) | (params.nocount << 12) |
		(params.high << 6) | params.low, 0x3fff);

	axi_clkgen_calc_clk_params(m >> 3, m & 0x7, &params);
	axi_clkgen_set_div(axi_clkgen,  MMCM_REG_CLK_FB1, MMCM_REG_CLK_FB2,
		MMCM_REG_CLKOUT6_2, &params);

	axi_clkgen_mmcm_write(axi_clkgen, MMCM_REG_LOCK1, lock & 0x3ff, 0x3ff);
	axi_clkgen_mmcm_write(axi_clkgen, MMCM_REG_LOCK2,
		(((lock >> 16) & 0x1f) << 10) | 0x1, 0x7fff);
	axi_clkgen_mmcm_write(axi_clkgen, MMCM_REG_LOCK3,
		(((lock >> 24) & 0x1f) << 10) | 0x3e9, 0x7fff);
	axi_clkgen_mmcm_write(axi_clkgen, MMCM_REG_FILTER1, filter >> 16, 0x9900);
	axi_clkgen_mmcm_write(axi_clkgen, MMCM_REG_FILTER2, filter, 0x9900);

	return 0;
}

static int axi_clkgen_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct axi_clkgen *axi_clkgen = clk_hw_to_axi_clkgen(hw);
	const struct axi_clkgen_limits *limits = &axi_clkgen->limits;
	unsigned int d, m, dout;
	unsigned long long tmp;

	axi_clkgen_calc_params(limits, req->best_parent_rate, req->rate,
			       &d, &m, &dout);

	if (d == 0 || dout == 0 || m == 0)
		return -EINVAL;

	tmp = (unsigned long long)req->best_parent_rate * m;
	tmp = DIV_ROUND_CLOSEST_ULL(tmp, dout * d);

	req->rate = min_t(unsigned long long, tmp, LONG_MAX);
	return 0;
}

static unsigned int axi_clkgen_get_div(struct axi_clkgen *axi_clkgen,
	unsigned int reg1, unsigned int reg2)
{
	unsigned int val1, val2;
	unsigned int div;

	axi_clkgen_mmcm_read(axi_clkgen, reg2, &val2);
	if (val2 & MMCM_CLKOUT_NOCOUNT)
		return 8;

	axi_clkgen_mmcm_read(axi_clkgen, reg1, &val1);

	div = (val1 & 0x3f) + ((val1 >> 6) & 0x3f);
	div <<= 3;

	if (val2 & MMCM_CLK_DIV_DIVIDE) {
		if ((val2 & BIT(7)) && (val2 & 0x7000) != 0x1000)
			div += 8;
		else
			div += 16;

		div += (val2 >> 12) & 0x7;
	}

	return div;
}

static unsigned long axi_clkgen_recalc_rate(struct clk_hw *clk_hw,
	unsigned long parent_rate)
{
	struct axi_clkgen *axi_clkgen = clk_hw_to_axi_clkgen(clk_hw);
	unsigned int d, m, dout;
	unsigned long long tmp;
	unsigned int val;

	dout = axi_clkgen_get_div(axi_clkgen, MMCM_REG_CLKOUT0_1,
		MMCM_REG_CLKOUT0_2);
	m = axi_clkgen_get_div(axi_clkgen, MMCM_REG_CLK_FB1,
		MMCM_REG_CLK_FB2);

	axi_clkgen_mmcm_read(axi_clkgen, MMCM_REG_CLK_DIV, &val);
	if (val & MMCM_CLK_DIV_NOCOUNT)
		d = 1;
	else
		d = (val & 0x3f) + ((val >> 6) & 0x3f);

	if (d == 0 || dout == 0)
		return 0;

	tmp = (unsigned long long)parent_rate * m;
	tmp = DIV_ROUND_CLOSEST_ULL(tmp, dout * d);

	return min_t(unsigned long long, tmp, ULONG_MAX);
}

static int axi_clkgen_enable(struct clk_hw *clk_hw)
{
	struct axi_clkgen *axi_clkgen = clk_hw_to_axi_clkgen(clk_hw);

	axi_clkgen_mmcm_enable(axi_clkgen, true);

	return 0;
}

static void axi_clkgen_disable(struct clk_hw *clk_hw)
{
	struct axi_clkgen *axi_clkgen = clk_hw_to_axi_clkgen(clk_hw);

	axi_clkgen_mmcm_enable(axi_clkgen, false);
}

static int axi_clkgen_set_parent(struct clk_hw *clk_hw, u8 index)
{
	struct axi_clkgen *axi_clkgen = clk_hw_to_axi_clkgen(clk_hw);

	axi_clkgen_write(axi_clkgen, AXI_CLKGEN_V2_REG_CLKSEL, index);

	return 0;
}

static u8 axi_clkgen_get_parent(struct clk_hw *clk_hw)
{
	struct axi_clkgen *axi_clkgen = clk_hw_to_axi_clkgen(clk_hw);
	unsigned int parent;

	axi_clkgen_read(axi_clkgen, AXI_CLKGEN_V2_REG_CLKSEL, &parent);

	return parent;
}

static const struct clk_ops axi_clkgen_ops = {
	.recalc_rate = axi_clkgen_recalc_rate,
	.determine_rate = axi_clkgen_determine_rate,
	.set_rate = axi_clkgen_set_rate,
	.enable = axi_clkgen_enable,
	.disable = axi_clkgen_disable,
	.set_parent = axi_clkgen_set_parent,
	.get_parent = axi_clkgen_get_parent,
};

static int axi_clkgen_probe(struct platform_device *pdev)
{
	const struct axi_clkgen_limits *dflt_limits;
	struct axi_clkgen *axi_clkgen;
	struct clk_init_data init;
	const char *parent_names[2];
	const char *clk_name;
	struct clk *axi_clk;
	unsigned int i;
	int ret;

	dflt_limits = device_get_match_data(&pdev->dev);
	if (!dflt_limits)
		return -ENODEV;

	axi_clkgen = devm_kzalloc(&pdev->dev, sizeof(*axi_clkgen), GFP_KERNEL);
	if (!axi_clkgen)
		return -ENOMEM;

	axi_clkgen->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(axi_clkgen->base))
		return PTR_ERR(axi_clkgen->base);

	init.num_parents = of_clk_get_parent_count(pdev->dev.of_node);

	axi_clk = devm_clk_get_enabled(&pdev->dev, "s_axi_aclk");
	if (!IS_ERR(axi_clk)) {
		if (init.num_parents < 2 || init.num_parents > 3)
			return -EINVAL;

		init.num_parents -= 1;
	} else {
		/*
		 * Legacy... So that old DTs which do not have clock-names still
		 * work. In this case we don't explicitly enable the AXI bus
		 * clock.
		 */
		if (PTR_ERR(axi_clk) != -ENOENT)
			return PTR_ERR(axi_clk);
		if (init.num_parents < 1 || init.num_parents > 2)
			return -EINVAL;
	}

	for (i = 0; i < init.num_parents; i++) {
		parent_names[i] = of_clk_get_parent_name(pdev->dev.of_node, i);
		if (!parent_names[i])
			return -EINVAL;
	}

	memcpy(&axi_clkgen->limits, dflt_limits, sizeof(axi_clkgen->limits));

	clk_name = pdev->dev.of_node->name;
	of_property_read_string(pdev->dev.of_node, "clock-output-names",
		&clk_name);

	init.name = clk_name;
	init.ops = &axi_clkgen_ops;
	init.flags = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE;
	init.parent_names = parent_names;

	axi_clkgen_mmcm_enable(axi_clkgen, false);

	axi_clkgen->clk_hw.init = &init;
	ret = devm_clk_hw_register(&pdev->dev, &axi_clkgen->clk_hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_simple_get,
					   &axi_clkgen->clk_hw);
}

static const struct of_device_id axi_clkgen_ids[] = {
	{
		.compatible = "adi,zynqmp-axi-clkgen-2.00.a",
		.data = &axi_clkgen_zynqmp_default_limits,
	},
	{
		.compatible = "adi,axi-clkgen-2.00.a",
		.data = &axi_clkgen_zynq_default_limits,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, axi_clkgen_ids);

static struct platform_driver axi_clkgen_driver = {
	.driver = {
		.name = "adi-axi-clkgen",
		.of_match_table = axi_clkgen_ids,
	},
	.probe = axi_clkgen_probe,
};
module_platform_driver(axi_clkgen_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Driver for the Analog Devices' AXI clkgen pcore clock generator");
