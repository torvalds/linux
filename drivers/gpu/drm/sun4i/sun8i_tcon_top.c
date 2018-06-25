// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net> */

#include <drm/drmP.h>

#include <dt-bindings/clock/sun8i-tcon-top.h>

#include <linux/bitfield.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include "sun8i_tcon_top.h"

static int sun8i_tcon_top_get_connected_ep_id(struct device_node *node,
					      int port_id)
{
	struct device_node *ep, *remote, *port;
	struct of_endpoint endpoint;

	port = of_graph_get_port_by_id(node, port_id);
	if (!port)
		return -ENOENT;

	for_each_available_child_of_node(port, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (!remote)
			continue;

		if (of_device_is_available(remote)) {
			of_graph_parse_endpoint(ep, &endpoint);

			of_node_put(remote);

			return endpoint.id;
		}

		of_node_put(remote);
	}

	return -ENOENT;
}

static struct clk_hw *sun8i_tcon_top_register_gate(struct device *dev,
						   struct clk *parent,
						   void __iomem *regs,
						   spinlock_t *lock,
						   u8 bit, int name_index)
{
	const char *clk_name, *parent_name;
	int ret;

	parent_name = __clk_get_name(parent);
	ret = of_property_read_string_index(dev->of_node,
					    "clock-output-names", name_index,
					    &clk_name);
	if (ret)
		return ERR_PTR(ret);

	return clk_hw_register_gate(dev, clk_name, parent_name,
				    CLK_SET_RATE_PARENT,
				    regs + TCON_TOP_GATE_SRC_REG,
				    bit, 0, lock);
};

static int sun8i_tcon_top_bind(struct device *dev, struct device *master,
			       void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct clk *dsi, *tcon_tv0, *tcon_tv1, *tve0, *tve1;
	struct clk_hw_onecell_data *clk_data;
	struct sun8i_tcon_top *tcon_top;
	bool mixer0_unused = false;
	struct resource *res;
	void __iomem *regs;
	int ret, i, id;
	u32 val;

	tcon_top = devm_kzalloc(dev, sizeof(*tcon_top), GFP_KERNEL);
	if (!tcon_top)
		return -ENOMEM;

	clk_data = devm_kzalloc(dev, sizeof(*clk_data) +
				sizeof(*clk_data->hws) * CLK_NUM,
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;
	tcon_top->clk_data = clk_data;

	spin_lock_init(&tcon_top->reg_lock);

	tcon_top->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(tcon_top->rst)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(tcon_top->rst);
	}

	tcon_top->bus = devm_clk_get(dev, "bus");
	if (IS_ERR(tcon_top->bus)) {
		dev_err(dev, "Couldn't get the bus clock\n");
		return PTR_ERR(tcon_top->bus);
	}

	dsi = devm_clk_get(dev, "dsi");
	if (IS_ERR(dsi)) {
		dev_err(dev, "Couldn't get the dsi clock\n");
		return PTR_ERR(dsi);
	}

	tcon_tv0 = devm_clk_get(dev, "tcon-tv0");
	if (IS_ERR(tcon_tv0)) {
		dev_err(dev, "Couldn't get the tcon-tv0 clock\n");
		return PTR_ERR(tcon_tv0);
	}

	tcon_tv1 = devm_clk_get(dev, "tcon-tv1");
	if (IS_ERR(tcon_tv1)) {
		dev_err(dev, "Couldn't get the tcon-tv1 clock\n");
		return PTR_ERR(tcon_tv1);
	}

	tve0 = devm_clk_get(dev, "tve0");
	if (IS_ERR(tve0)) {
		dev_err(dev, "Couldn't get the tve0 clock\n");
		return PTR_ERR(tve0);
	}

	tve1 = devm_clk_get(dev, "tve1");
	if (IS_ERR(tve1)) {
		dev_err(dev, "Couldn't get the tve1 clock\n");
		return PTR_ERR(tve1);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ret = reset_control_deassert(tcon_top->rst);
	if (ret) {
		dev_err(dev, "Could not deassert ctrl reset control\n");
		return ret;
	}

	ret = clk_prepare_enable(tcon_top->bus);
	if (ret) {
		dev_err(dev, "Could not enable bus clock\n");
		goto err_assert_reset;
	}

	val = 0;

	/* check if HDMI mux output is connected */
	if (sun8i_tcon_top_get_connected_ep_id(dev->of_node, 5) >= 0) {
		/* find HDMI input endpoint id, if it is connected at all*/
		id = sun8i_tcon_top_get_connected_ep_id(dev->of_node, 4);
		if (id >= 0)
			val = FIELD_PREP(TCON_TOP_HDMI_SRC_MSK, id + 1);
		else
			DRM_DEBUG_DRIVER("TCON TOP HDMI input is not connected\n");
	} else {
		DRM_DEBUG_DRIVER("TCON TOP HDMI output is not connected\n");
	}

	writel(val, regs + TCON_TOP_GATE_SRC_REG);

	val = 0;

	/* process mixer0 mux output */
	id = sun8i_tcon_top_get_connected_ep_id(dev->of_node, 1);
	if (id >= 0) {
		val = FIELD_PREP(TCON_TOP_PORT_DE0_MSK, id);
	} else {
		DRM_DEBUG_DRIVER("TCON TOP mixer0 output is not connected\n");
		mixer0_unused = true;
	}

	/* process mixer1 mux output */
	id = sun8i_tcon_top_get_connected_ep_id(dev->of_node, 3);
	if (id >= 0) {
		val |= FIELD_PREP(TCON_TOP_PORT_DE1_MSK, id);

		/*
		 * mixer0 mux has priority over mixer1 mux. We have to
		 * make sure mixer0 doesn't overtake TCON from mixer1.
		 */
		if (mixer0_unused && id == 0)
			val |= FIELD_PREP(TCON_TOP_PORT_DE0_MSK, 1);
	} else {
		DRM_DEBUG_DRIVER("TCON TOP mixer1 output is not connected\n");
	}

	writel(val, regs + TCON_TOP_PORT_SEL_REG);

	/*
	 * TCON TOP has two muxes, which select parent clock for each TCON TV
	 * channel clock. Parent could be either TCON TV or TVE clock. For now
	 * we leave this fixed to TCON TV, since TVE driver for R40 is not yet
	 * implemented. Once it is, graph needs to be traversed to determine
	 * if TVE is active on each TCON TV. If it is, mux should be switched
	 * to TVE clock parent.
	 */
	clk_data->hws[CLK_TCON_TOP_TV0] =
		sun8i_tcon_top_register_gate(dev, tcon_tv0, regs,
					     &tcon_top->reg_lock,
					     TCON_TOP_TCON_TV0_GATE, 0);

	clk_data->hws[CLK_TCON_TOP_TV1] =
		sun8i_tcon_top_register_gate(dev, tcon_tv1, regs,
					     &tcon_top->reg_lock,
					     TCON_TOP_TCON_TV1_GATE, 1);

	clk_data->hws[CLK_TCON_TOP_DSI] =
		sun8i_tcon_top_register_gate(dev, dsi, regs,
					     &tcon_top->reg_lock,
					     TCON_TOP_TCON_DSI_GATE, 2);

	for (i = 0; i < CLK_NUM; i++)
		if (IS_ERR(clk_data->hws[i])) {
			ret = PTR_ERR(clk_data->hws[i]);
			goto err_unregister_gates;
		}

	clk_data->num = CLK_NUM;

	ret = of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
				     clk_data);
	if (ret)
		goto err_unregister_gates;

	dev_set_drvdata(dev, tcon_top);

	return 0;

err_unregister_gates:
	for (i = 0; i < CLK_NUM; i++)
		if (clk_data->hws[i])
			clk_hw_unregister_gate(clk_data->hws[i]);
	clk_disable_unprepare(tcon_top->bus);
err_assert_reset:
	reset_control_assert(tcon_top->rst);

	return ret;
}

static void sun8i_tcon_top_unbind(struct device *dev, struct device *master,
				  void *data)
{
	struct sun8i_tcon_top *tcon_top = dev_get_drvdata(dev);
	struct clk_hw_onecell_data *clk_data = tcon_top->clk_data;
	int i;

	of_clk_del_provider(dev->of_node);
	for (i = 0; i < CLK_NUM; i++)
		clk_hw_unregister_gate(clk_data->hws[i]);

	clk_disable_unprepare(tcon_top->bus);
	reset_control_assert(tcon_top->rst);
}

static const struct component_ops sun8i_tcon_top_ops = {
	.bind	= sun8i_tcon_top_bind,
	.unbind	= sun8i_tcon_top_unbind,
};

static int sun8i_tcon_top_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun8i_tcon_top_ops);
}

static int sun8i_tcon_top_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun8i_tcon_top_ops);

	return 0;
}

/* sun4i_drv uses this list to check if a device node is a TCON TOP */
const struct of_device_id sun8i_tcon_top_of_table[] = {
	{ .compatible = "allwinner,sun8i-r40-tcon-top" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun8i_tcon_top_of_table);
EXPORT_SYMBOL(sun8i_tcon_top_of_table);

static struct platform_driver sun8i_tcon_top_platform_driver = {
	.probe		= sun8i_tcon_top_probe,
	.remove		= sun8i_tcon_top_remove,
	.driver		= {
		.name		= "sun8i-tcon-top",
		.of_match_table	= sun8i_tcon_top_of_table,
	},
};
module_platform_driver(sun8i_tcon_top_platform_driver);

MODULE_AUTHOR("Jernej Skrabec <jernej.skrabec@siol.net>");
MODULE_DESCRIPTION("Allwinner R40 TCON TOP driver");
MODULE_LICENSE("GPL");
