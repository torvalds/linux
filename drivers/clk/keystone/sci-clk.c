/*
 * SCI Clock driver for keystone based devices
 *
 * Copyright (C) 2015-2016 Texas Instruments Incorporated - http://www.ti.com/
 *	Tero Kristo <t-kristo@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <linux/bsearch.h>

#define SCI_CLK_SSC_ENABLE		BIT(0)
#define SCI_CLK_ALLOW_FREQ_CHANGE	BIT(1)
#define SCI_CLK_INPUT_TERMINATION	BIT(2)

/**
 * struct sci_clk_data - TI SCI clock data
 * @dev: device index
 * @num_clks: number of clocks for this device
 */
struct sci_clk_data {
	u16 dev;
	u16 num_clks;
};

/**
 * struct sci_clk_provider - TI SCI clock provider representation
 * @sci: Handle to the System Control Interface protocol handler
 * @ops: Pointer to the SCI ops to be used by the clocks
 * @dev: Device pointer for the clock provider
 * @clk_data: Clock data
 * @clocks: Clocks array for this device
 * @num_clocks: Total number of clocks for this provider
 */
struct sci_clk_provider {
	const struct ti_sci_handle *sci;
	const struct ti_sci_clk_ops *ops;
	struct device *dev;
	const struct sci_clk_data *clk_data;
	struct clk_hw **clocks;
	int num_clocks;
};

/**
 * struct sci_clk - TI SCI clock representation
 * @hw:		 Hardware clock cookie for common clock framework
 * @dev_id:	 Device index
 * @clk_id:	 Clock index
 * @provider:	 Master clock provider
 * @flags:	 Flags for the clock
 */
struct sci_clk {
	struct clk_hw hw;
	u16 dev_id;
	u8 clk_id;
	struct sci_clk_provider *provider;
	u8 flags;
};

#define to_sci_clk(_hw) container_of(_hw, struct sci_clk, hw)

/**
 * sci_clk_prepare - Prepare (enable) a TI SCI clock
 * @hw: clock to prepare
 *
 * Prepares a clock to be actively used. Returns the SCI protocol status.
 */
static int sci_clk_prepare(struct clk_hw *hw)
{
	struct sci_clk *clk = to_sci_clk(hw);
	bool enable_ssc = clk->flags & SCI_CLK_SSC_ENABLE;
	bool allow_freq_change = clk->flags & SCI_CLK_ALLOW_FREQ_CHANGE;
	bool input_termination = clk->flags & SCI_CLK_INPUT_TERMINATION;

	return clk->provider->ops->get_clock(clk->provider->sci, clk->dev_id,
					     clk->clk_id, enable_ssc,
					     allow_freq_change,
					     input_termination);
}

/**
 * sci_clk_unprepare - Un-prepares (disables) a TI SCI clock
 * @hw: clock to unprepare
 *
 * Un-prepares a clock from active state.
 */
static void sci_clk_unprepare(struct clk_hw *hw)
{
	struct sci_clk *clk = to_sci_clk(hw);
	int ret;

	ret = clk->provider->ops->put_clock(clk->provider->sci, clk->dev_id,
					    clk->clk_id);
	if (ret)
		dev_err(clk->provider->dev,
			"unprepare failed for dev=%d, clk=%d, ret=%d\n",
			clk->dev_id, clk->clk_id, ret);
}

/**
 * sci_clk_is_prepared - Check if a TI SCI clock is prepared or not
 * @hw: clock to check status for
 *
 * Checks if a clock is prepared (enabled) in hardware. Returns non-zero
 * value if clock is enabled, zero otherwise.
 */
static int sci_clk_is_prepared(struct clk_hw *hw)
{
	struct sci_clk *clk = to_sci_clk(hw);
	bool req_state, current_state;
	int ret;

	ret = clk->provider->ops->is_on(clk->provider->sci, clk->dev_id,
					clk->clk_id, &req_state,
					&current_state);
	if (ret) {
		dev_err(clk->provider->dev,
			"is_prepared failed for dev=%d, clk=%d, ret=%d\n",
			clk->dev_id, clk->clk_id, ret);
		return 0;
	}

	return req_state;
}

/**
 * sci_clk_recalc_rate - Get clock rate for a TI SCI clock
 * @hw: clock to get rate for
 * @parent_rate: parent rate provided by common clock framework, not used
 *
 * Gets the current clock rate of a TI SCI clock. Returns the current
 * clock rate, or zero in failure.
 */
static unsigned long sci_clk_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct sci_clk *clk = to_sci_clk(hw);
	u64 freq;
	int ret;

	ret = clk->provider->ops->get_freq(clk->provider->sci, clk->dev_id,
					   clk->clk_id, &freq);
	if (ret) {
		dev_err(clk->provider->dev,
			"recalc-rate failed for dev=%d, clk=%d, ret=%d\n",
			clk->dev_id, clk->clk_id, ret);
		return 0;
	}

	return freq;
}

/**
 * sci_clk_determine_rate - Determines a clock rate a clock can be set to
 * @hw: clock to change rate for
 * @req: requested rate configuration for the clock
 *
 * Determines a suitable clock rate and parent for a TI SCI clock.
 * The parent handling is un-used, as generally the parent clock rates
 * are not known by the kernel; instead these are internally handled
 * by the firmware. Returns 0 on success, negative error value on failure.
 */
static int sci_clk_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct sci_clk *clk = to_sci_clk(hw);
	int ret;
	u64 new_rate;

	ret = clk->provider->ops->get_best_match_freq(clk->provider->sci,
						      clk->dev_id,
						      clk->clk_id,
						      req->min_rate,
						      req->rate,
						      req->max_rate,
						      &new_rate);
	if (ret) {
		dev_err(clk->provider->dev,
			"determine-rate failed for dev=%d, clk=%d, ret=%d\n",
			clk->dev_id, clk->clk_id, ret);
		return ret;
	}

	req->rate = new_rate;

	return 0;
}

/**
 * sci_clk_set_rate - Set rate for a TI SCI clock
 * @hw: clock to change rate for
 * @rate: target rate for the clock
 * @parent_rate: rate of the clock parent, not used for TI SCI clocks
 *
 * Sets a clock frequency for a TI SCI clock. Returns the TI SCI
 * protocol status.
 */
static int sci_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct sci_clk *clk = to_sci_clk(hw);

	return clk->provider->ops->set_freq(clk->provider->sci, clk->dev_id,
					    clk->clk_id, rate, rate, rate);
}

/**
 * sci_clk_get_parent - Get the current parent of a TI SCI clock
 * @hw: clock to get parent for
 *
 * Returns the index of the currently selected parent for a TI SCI clock.
 */
static u8 sci_clk_get_parent(struct clk_hw *hw)
{
	struct sci_clk *clk = to_sci_clk(hw);
	u8 parent_id;
	int ret;

	ret = clk->provider->ops->get_parent(clk->provider->sci, clk->dev_id,
					     clk->clk_id, &parent_id);
	if (ret) {
		dev_err(clk->provider->dev,
			"get-parent failed for dev=%d, clk=%d, ret=%d\n",
			clk->dev_id, clk->clk_id, ret);
		return 0;
	}

	return parent_id - clk->clk_id - 1;
}

/**
 * sci_clk_set_parent - Set the parent of a TI SCI clock
 * @hw: clock to set parent for
 * @index: new parent index for the clock
 *
 * Sets the parent of a TI SCI clock. Return TI SCI protocol status.
 */
static int sci_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct sci_clk *clk = to_sci_clk(hw);

	return clk->provider->ops->set_parent(clk->provider->sci, clk->dev_id,
					      clk->clk_id,
					      index + 1 + clk->clk_id);
}

static const struct clk_ops sci_clk_ops = {
	.prepare = sci_clk_prepare,
	.unprepare = sci_clk_unprepare,
	.is_prepared = sci_clk_is_prepared,
	.recalc_rate = sci_clk_recalc_rate,
	.determine_rate = sci_clk_determine_rate,
	.set_rate = sci_clk_set_rate,
	.get_parent = sci_clk_get_parent,
	.set_parent = sci_clk_set_parent,
};

/**
 * _sci_clk_get - Gets a handle for an SCI clock
 * @provider: Handle to SCI clock provider
 * @dev_id: device ID for the clock to register
 * @clk_id: clock ID for the clock to register
 *
 * Gets a handle to an existing TI SCI hw clock, or builds a new clock
 * entry and registers it with the common clock framework. Called from
 * the common clock framework, when a corresponding of_clk_get call is
 * executed, or recursively from itself when parsing parent clocks.
 * Returns a pointer to the hw clock struct, or ERR_PTR value in failure.
 */
static struct clk_hw *_sci_clk_build(struct sci_clk_provider *provider,
				     u16 dev_id, u8 clk_id)
{
	struct clk_init_data init = { NULL };
	struct sci_clk *sci_clk = NULL;
	char *name = NULL;
	char **parent_names = NULL;
	int i;
	int ret;

	sci_clk = devm_kzalloc(provider->dev, sizeof(*sci_clk), GFP_KERNEL);
	if (!sci_clk)
		return ERR_PTR(-ENOMEM);

	sci_clk->dev_id = dev_id;
	sci_clk->clk_id = clk_id;
	sci_clk->provider = provider;

	ret = provider->ops->get_num_parents(provider->sci, dev_id,
					     clk_id,
					     &init.num_parents);
	if (ret)
		goto err;

	name = kasprintf(GFP_KERNEL, "%s:%d:%d", dev_name(provider->dev),
			 sci_clk->dev_id, sci_clk->clk_id);

	init.name = name;

	/*
	 * From kernel point of view, we only care about a clocks parents,
	 * if it has more than 1 possible parent. In this case, it is going
	 * to have mux functionality. Otherwise it is going to act as a root
	 * clock.
	 */
	if (init.num_parents < 2)
		init.num_parents = 0;

	if (init.num_parents) {
		parent_names = kcalloc(init.num_parents, sizeof(char *),
				       GFP_KERNEL);

		if (!parent_names) {
			ret = -ENOMEM;
			goto err;
		}

		for (i = 0; i < init.num_parents; i++) {
			char *parent_name;

			parent_name = kasprintf(GFP_KERNEL, "%s:%d:%d",
						dev_name(provider->dev),
						sci_clk->dev_id,
						sci_clk->clk_id + 1 + i);
			if (!parent_name) {
				ret = -ENOMEM;
				goto err;
			}
			parent_names[i] = parent_name;
		}
		init.parent_names = (void *)parent_names;
	}

	init.ops = &sci_clk_ops;
	sci_clk->hw.init = &init;

	ret = devm_clk_hw_register(provider->dev, &sci_clk->hw);
	if (ret)
		dev_err(provider->dev, "failed clk register with %d\n", ret);

err:
	if (parent_names) {
		for (i = 0; i < init.num_parents; i++)
			kfree(parent_names[i]);

		kfree(parent_names);
	}

	kfree(name);

	if (ret)
		return ERR_PTR(ret);

	return &sci_clk->hw;
}

static int _cmp_sci_clk(const void *a, const void *b)
{
	const struct sci_clk *ca = a;
	const struct sci_clk *cb = *(struct sci_clk **)b;

	if (ca->dev_id == cb->dev_id && ca->clk_id == cb->clk_id)
		return 0;
	if (ca->dev_id > cb->dev_id ||
	    (ca->dev_id == cb->dev_id && ca->clk_id > cb->clk_id))
		return 1;
	return -1;
}

/**
 * sci_clk_get - Xlate function for getting clock handles
 * @clkspec: device tree clock specifier
 * @data: pointer to the clock provider
 *
 * Xlate function for retrieving clock TI SCI hw clock handles based on
 * device tree clock specifier. Called from the common clock framework,
 * when a corresponding of_clk_get call is executed. Returns a pointer
 * to the TI SCI hw clock struct, or ERR_PTR value in failure.
 */
static struct clk_hw *sci_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct sci_clk_provider *provider = data;
	struct sci_clk **clk;
	struct sci_clk key;

	if (clkspec->args_count != 2)
		return ERR_PTR(-EINVAL);

	key.dev_id = clkspec->args[0];
	key.clk_id = clkspec->args[1];

	clk = bsearch(&key, provider->clocks, provider->num_clocks,
		      sizeof(clk), _cmp_sci_clk);

	if (!clk)
		return ERR_PTR(-ENODEV);

	return &(*clk)->hw;
}

static int ti_sci_init_clocks(struct sci_clk_provider *p)
{
	const struct sci_clk_data *data = p->clk_data;
	struct clk_hw *hw;
	int i;
	int num_clks = 0;

	while (data->num_clks) {
		num_clks += data->num_clks;
		data++;
	}

	p->num_clocks = num_clks;

	p->clocks = devm_kcalloc(p->dev, num_clks, sizeof(struct sci_clk),
				 GFP_KERNEL);
	if (!p->clocks)
		return -ENOMEM;

	num_clks = 0;

	data = p->clk_data;

	while (data->num_clks) {
		for (i = 0; i < data->num_clks; i++) {
			hw = _sci_clk_build(p, data->dev, i);
			if (!IS_ERR(hw)) {
				p->clocks[num_clks++] = hw;
				continue;
			}

			/* Skip any holes in the clock lists */
			if (PTR_ERR(hw) == -ENODEV)
				continue;

			return PTR_ERR(hw);
		}
		data++;
	}

	return 0;
}

static const struct sci_clk_data k2g_clk_data[] = {
	/* pmmc */
	{ .dev = 0x0, .num_clks = 4 },

	/* mlb0 */
	{ .dev = 0x1, .num_clks = 5 },

	/* dss0 */
	{ .dev = 0x2, .num_clks = 2 },

	/* mcbsp0 */
	{ .dev = 0x3, .num_clks = 8 },

	/* mcasp0 */
	{ .dev = 0x4, .num_clks = 8 },

	/* mcasp1 */
	{ .dev = 0x5, .num_clks = 8 },

	/* mcasp2 */
	{ .dev = 0x6, .num_clks = 8 },

	/* dcan0 */
	{ .dev = 0x8, .num_clks = 2 },

	/* dcan1 */
	{ .dev = 0x9, .num_clks = 2 },

	/* emif0 */
	{ .dev = 0xa, .num_clks = 6 },

	/* mmchs0 */
	{ .dev = 0xb, .num_clks = 3 },

	/* mmchs1 */
	{ .dev = 0xc, .num_clks = 3 },

	/* gpmc0 */
	{ .dev = 0xd, .num_clks = 1 },

	/* elm0 */
	{ .dev = 0xe, .num_clks = 1 },

	/* spi0 */
	{ .dev = 0x10, .num_clks = 1 },

	/* spi1 */
	{ .dev = 0x11, .num_clks = 1 },

	/* spi2 */
	{ .dev = 0x12, .num_clks = 1 },

	/* spi3 */
	{ .dev = 0x13, .num_clks = 1 },

	/* icss0 */
	{ .dev = 0x14, .num_clks = 6 },

	/* icss1 */
	{ .dev = 0x15, .num_clks = 6 },

	/* usb0 */
	{ .dev = 0x16, .num_clks = 7 },

	/* usb1 */
	{ .dev = 0x17, .num_clks = 7 },

	/* nss0 */
	{ .dev = 0x18, .num_clks = 14 },

	/* pcie0 */
	{ .dev = 0x19, .num_clks = 1 },

	/* gpio0 */
	{ .dev = 0x1b, .num_clks = 1 },

	/* gpio1 */
	{ .dev = 0x1c, .num_clks = 1 },

	/* timer64_0 */
	{ .dev = 0x1d, .num_clks = 9 },

	/* timer64_1 */
	{ .dev = 0x1e, .num_clks = 9 },

	/* timer64_2 */
	{ .dev = 0x1f, .num_clks = 9 },

	/* timer64_3 */
	{ .dev = 0x20, .num_clks = 9 },

	/* timer64_4 */
	{ .dev = 0x21, .num_clks = 9 },

	/* timer64_5 */
	{ .dev = 0x22, .num_clks = 9 },

	/* timer64_6 */
	{ .dev = 0x23, .num_clks = 9 },

	/* msgmgr0 */
	{ .dev = 0x25, .num_clks = 1 },

	/* bootcfg0 */
	{ .dev = 0x26, .num_clks = 1 },

	/* arm_bootrom0 */
	{ .dev = 0x27, .num_clks = 1 },

	/* dsp_bootrom0 */
	{ .dev = 0x29, .num_clks = 1 },

	/* debugss0 */
	{ .dev = 0x2b, .num_clks = 8 },

	/* uart0 */
	{ .dev = 0x2c, .num_clks = 1 },

	/* uart1 */
	{ .dev = 0x2d, .num_clks = 1 },

	/* uart2 */
	{ .dev = 0x2e, .num_clks = 1 },

	/* ehrpwm0 */
	{ .dev = 0x2f, .num_clks = 1 },

	/* ehrpwm1 */
	{ .dev = 0x30, .num_clks = 1 },

	/* ehrpwm2 */
	{ .dev = 0x31, .num_clks = 1 },

	/* ehrpwm3 */
	{ .dev = 0x32, .num_clks = 1 },

	/* ehrpwm4 */
	{ .dev = 0x33, .num_clks = 1 },

	/* ehrpwm5 */
	{ .dev = 0x34, .num_clks = 1 },

	/* eqep0 */
	{ .dev = 0x35, .num_clks = 1 },

	/* eqep1 */
	{ .dev = 0x36, .num_clks = 1 },

	/* eqep2 */
	{ .dev = 0x37, .num_clks = 1 },

	/* ecap0 */
	{ .dev = 0x38, .num_clks = 1 },

	/* ecap1 */
	{ .dev = 0x39, .num_clks = 1 },

	/* i2c0 */
	{ .dev = 0x3a, .num_clks = 1 },

	/* i2c1 */
	{ .dev = 0x3b, .num_clks = 1 },

	/* i2c2 */
	{ .dev = 0x3c, .num_clks = 1 },

	/* edma0 */
	{ .dev = 0x3f, .num_clks = 2 },

	/* semaphore0 */
	{ .dev = 0x40, .num_clks = 1 },

	/* intc0 */
	{ .dev = 0x41, .num_clks = 1 },

	/* gic0 */
	{ .dev = 0x42, .num_clks = 1 },

	/* qspi0 */
	{ .dev = 0x43, .num_clks = 5 },

	/* arm_64b_counter0 */
	{ .dev = 0x44, .num_clks = 2 },

	/* tetris0 */
	{ .dev = 0x45, .num_clks = 2 },

	/* cgem0 */
	{ .dev = 0x46, .num_clks = 2 },

	/* msmc0 */
	{ .dev = 0x47, .num_clks = 1 },

	/* cbass0 */
	{ .dev = 0x49, .num_clks = 1 },

	/* board0 */
	{ .dev = 0x4c, .num_clks = 36 },

	/* edma1 */
	{ .dev = 0x4f, .num_clks = 2 },
	{ .num_clks = 0 },
};

static const struct of_device_id ti_sci_clk_of_match[] = {
	{ .compatible = "ti,k2g-sci-clk", .data = &k2g_clk_data },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, ti_sci_clk_of_match);

/**
 * ti_sci_clk_probe - Probe function for the TI SCI clock driver
 * @pdev: platform device pointer to be probed
 *
 * Probes the TI SCI clock device. Allocates a new clock provider
 * and registers this to the common clock framework. Also applies
 * any required flags to the identified clocks via clock lists
 * supplied from DT. Returns 0 for success, negative error value
 * for failure.
 */
static int ti_sci_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct sci_clk_provider *provider;
	const struct ti_sci_handle *handle;
	const struct sci_clk_data *data;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	handle = devm_ti_sci_get_handle(dev);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	provider = devm_kzalloc(dev, sizeof(*provider), GFP_KERNEL);
	if (!provider)
		return -ENOMEM;

	provider->clk_data = data;

	provider->sci = handle;
	provider->ops = &handle->ops.clk_ops;
	provider->dev = dev;

	ret = ti_sci_init_clocks(provider);
	if (ret) {
		pr_err("ti-sci-init-clocks failed.\n");
		return ret;
	}

	return of_clk_add_hw_provider(np, sci_clk_get, provider);
}

/**
 * ti_sci_clk_remove - Remove TI SCI clock device
 * @pdev: platform device pointer for the device to be removed
 *
 * Removes the TI SCI device. Unregisters the clock provider registered
 * via common clock framework. Any memory allocated for the device will
 * be free'd silently via the devm framework. Returns 0 always.
 */
static int ti_sci_clk_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);

	return 0;
}

static struct platform_driver ti_sci_clk_driver = {
	.probe = ti_sci_clk_probe,
	.remove = ti_sci_clk_remove,
	.driver = {
		.name = "ti-sci-clk",
		.of_match_table = of_match_ptr(ti_sci_clk_of_match),
	},
};
module_platform_driver(ti_sci_clk_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI System Control Interface(SCI) Clock driver");
MODULE_AUTHOR("Tero Kristo");
MODULE_ALIAS("platform:ti-sci-clk");
