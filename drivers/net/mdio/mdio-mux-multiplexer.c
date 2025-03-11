// SPDX-License-Identifier: GPL-2.0+
/* MDIO bus multiplexer using kernel multiplexer subsystem
 *
 * Copyright 2019 NXP
 */

#include <linux/mdio-mux.h>
#include <linux/module.h>
#include <linux/mux/consumer.h>
#include <linux/platform_device.h>

struct mdio_mux_multiplexer_state {
	struct mux_control *muxc;
	bool do_deselect;
	void *mux_handle;
};

/**
 * mdio_mux_multiplexer_switch_fn - This function is called by the mdio-mux
 *                                  layer when it thinks the mdio bus
 *                                  multiplexer needs to switch.
 * @current_child:  current value of the mux register.
 * @desired_child: value of the 'reg' property of the target child MDIO node.
 * @data: Private data used by this switch_fn passed to mdio_mux_init function
 *        via mdio_mux_init(.., .., .., .., data, ..).
 *
 * The first time this function is called, current_child == -1.
 * If current_child == desired_child, then the mux is already set to the
 * correct bus.
 */
static int mdio_mux_multiplexer_switch_fn(int current_child, int desired_child,
					  void *data)
{
	struct platform_device *pdev;
	struct mdio_mux_multiplexer_state *s;
	int ret = 0;

	pdev = (struct platform_device *)data;
	s = platform_get_drvdata(pdev);

	if (!(current_child ^ desired_child))
		return 0;

	if (s->do_deselect)
		ret = mux_control_deselect(s->muxc);
	if (ret) {
		dev_err(&pdev->dev, "mux_control_deselect failed in %s: %d\n",
			__func__, ret);
		return ret;
	}

	ret =  mux_control_select(s->muxc, desired_child);
	if (!ret) {
		dev_dbg(&pdev->dev, "%s %d -> %d\n", __func__, current_child,
			desired_child);
		s->do_deselect = true;
	} else {
		s->do_deselect = false;
	}

	return ret;
}

static int mdio_mux_multiplexer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mdio_mux_multiplexer_state *s;
	int ret = 0;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->muxc = devm_mux_control_get(dev, NULL);
	if (IS_ERR(s->muxc))
		return dev_err_probe(&pdev->dev, PTR_ERR(s->muxc),
				     "Failed to get mux\n");

	platform_set_drvdata(pdev, s);

	ret = mdio_mux_init(&pdev->dev, pdev->dev.of_node,
			    mdio_mux_multiplexer_switch_fn, &s->mux_handle,
			    pdev, NULL);

	return ret;
}

static void mdio_mux_multiplexer_remove(struct platform_device *pdev)
{
	struct mdio_mux_multiplexer_state *s = platform_get_drvdata(pdev);

	mdio_mux_uninit(s->mux_handle);

	if (s->do_deselect)
		mux_control_deselect(s->muxc);
}

static const struct of_device_id mdio_mux_multiplexer_match[] = {
	{ .compatible = "mdio-mux-multiplexer", },
	{},
};
MODULE_DEVICE_TABLE(of, mdio_mux_multiplexer_match);

static struct platform_driver mdio_mux_multiplexer_driver = {
	.driver = {
		.name		= "mdio-mux-multiplexer",
		.of_match_table	= mdio_mux_multiplexer_match,
	},
	.probe		= mdio_mux_multiplexer_probe,
	.remove		= mdio_mux_multiplexer_remove,
};

module_platform_driver(mdio_mux_multiplexer_driver);

MODULE_DESCRIPTION("MDIO bus multiplexer using kernel multiplexer subsystem");
MODULE_AUTHOR("Pankaj Bansal <pankaj.bansal@nxp.com>");
MODULE_LICENSE("GPL");
