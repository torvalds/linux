// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale SAI BCLK as a generic clock driver
 *
 * Copyright 2020 Michael Walle <michael@walle.cc>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define I2S_CSR		0x00
#define I2S_CR2		0x08
#define I2S_MCR		0x100
#define CSR_BCE_BIT	28
#define CSR_TE_BIT	31
#define CR2_BCD		BIT(24)
#define CR2_DIV_SHIFT	0
#define CR2_DIV_WIDTH	8
#define MCR_MOE		BIT(30)

struct fsl_sai_data {
	unsigned int	offset;	/* Register offset */
	bool		have_mclk; /* Have MCLK control */
};

struct fsl_sai_clk {
	const struct fsl_sai_data *data;
	struct clk_divider bclk_div;
	struct clk_divider mclk_div;
	struct clk_gate bclk_gate;
	struct clk_gate mclk_gate;
	struct clk_hw *bclk_hw;
	struct clk_hw *mclk_hw;
	spinlock_t lock;
};

static struct clk_hw *
fsl_sai_of_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct fsl_sai_clk *sai_clk = data;

	if (clkspec->args_count == 0)
		return sai_clk->bclk_hw;

	if (clkspec->args_count == 1) {
		if (clkspec->args[0] == 0)
			return sai_clk->bclk_hw;
		if (sai_clk->data->have_mclk && clkspec->args[0] == 1)
			return sai_clk->mclk_hw;
	}

	return ERR_PTR(-EINVAL);
}

static int fsl_sai_clk_register(struct device *dev, void __iomem *base,
				spinlock_t *lock, struct clk_divider *div,
				struct clk_gate *gate, struct clk_hw **hw,
				const int gate_bit, const int dir_bit,
				const int div_reg, char *name)
{
	const struct fsl_sai_data *data = device_get_match_data(dev);
	struct clk_parent_data pdata = { .index = 0 };
	struct clk_hw *chw;
	char *cname;

	gate->reg = base + data->offset + I2S_CSR;
	gate->bit_idx = gate_bit;
	gate->lock = lock;

	div->reg = base + div_reg;
	div->shift = CR2_DIV_SHIFT;
	div->width = CR2_DIV_WIDTH;
	div->lock = lock;

	cname = devm_kasprintf(dev, GFP_KERNEL, "%s.%s",
			       of_node_full_name(dev->of_node), name);
	if (!cname)
		return -ENOMEM;

	/* Set clock direction */
	writel(dir_bit, base + div_reg);

	chw = devm_clk_hw_register_composite_pdata(dev, cname,
						   &pdata, 1, NULL, NULL,
						   &div->hw,
						   &clk_divider_ops,
						   &gate->hw,
						   &clk_gate_ops,
						   CLK_SET_RATE_GATE);
	if (IS_ERR(chw))
		return PTR_ERR(chw);

	*hw = chw;

	return 0;
}

static int fsl_sai_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct fsl_sai_data *data = device_get_match_data(dev);
	struct fsl_sai_clk *sai_clk;
	struct clk *clk_bus;
	void __iomem *base;
	int ret;

	sai_clk = devm_kzalloc_obj(dev, *sai_clk);
	if (!sai_clk)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_bus = devm_clk_get_optional_enabled(dev, "bus");
	if (IS_ERR(clk_bus))
		return PTR_ERR(clk_bus);

	sai_clk->data = data;
	spin_lock_init(&sai_clk->lock);

	ret = fsl_sai_clk_register(dev, base, &sai_clk->lock,
				   &sai_clk->bclk_div, &sai_clk->bclk_gate,
				   &sai_clk->bclk_hw, CSR_BCE_BIT, CR2_BCD,
				   data->offset + I2S_CR2, "BCLK");
	if (ret)
		return ret;

	if (data->have_mclk) {
		ret = fsl_sai_clk_register(dev, base, &sai_clk->lock,
					   &sai_clk->mclk_div,
					   &sai_clk->mclk_gate,
					   &sai_clk->mclk_hw,
					   CSR_TE_BIT, MCR_MOE, I2S_MCR,
					   "MCLK");
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(dev, fsl_sai_of_clk_get, sai_clk);
}

static const struct fsl_sai_data fsl_sai_vf610_data = {
	.offset	= 0,
	.have_mclk = false,
};

static const struct fsl_sai_data fsl_sai_imx8mq_data = {
	.offset	= 8,
	.have_mclk = true,
};

static const struct of_device_id of_fsl_sai_clk_ids[] = {
	{ .compatible = "fsl,vf610-sai-clock", .data = &fsl_sai_vf610_data },
	{ .compatible = "fsl,imx8mq-sai-clock", .data = &fsl_sai_imx8mq_data },
	{ }
};
MODULE_DEVICE_TABLE(of, of_fsl_sai_clk_ids);

static struct platform_driver fsl_sai_clk_driver = {
	.probe = fsl_sai_clk_probe,
	.driver		= {
		.name	= "fsl-sai-clk",
		.of_match_table = of_fsl_sai_clk_ids,
	},
};
module_platform_driver(fsl_sai_clk_driver);

MODULE_DESCRIPTION("Freescale SAI bitclock-as-a-clock driver");
MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_ALIAS("platform:fsl-sai-clk");
