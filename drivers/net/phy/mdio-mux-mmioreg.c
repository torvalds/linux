/*
 * Simple memory-mapped device MDIO MUX driver
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/mdio-mux.h>

struct mdio_mux_mmioreg_state {
	void *mux_handle;
	phys_addr_t phys;
	unsigned int iosize;
	unsigned int mask;
};

/*
 * MDIO multiplexing switch function
 *
 * This function is called by the mdio-mux layer when it thinks the mdio bus
 * multiplexer needs to switch.
 *
 * 'current_child' is the current value of the mux register (masked via
 * s->mask).
 *
 * 'desired_child' is the value of the 'reg' property of the target child MDIO
 * node.
 *
 * The first time this function is called, current_child == -1.
 *
 * If current_child == desired_child, then the mux is already set to the
 * correct bus.
 */
static int mdio_mux_mmioreg_switch_fn(int current_child, int desired_child,
				      void *data)
{
	struct mdio_mux_mmioreg_state *s = data;

	if (current_child ^ desired_child) {
		void __iomem *p = ioremap(s->phys, s->iosize);
		if (!p)
			return -ENOMEM;

		switch (s->iosize) {
		case sizeof(uint8_t): {
			uint8_t x, y;

			x = ioread8(p);
			y = (x & ~s->mask) | desired_child;
			if (x != y) {
				iowrite8((x & ~s->mask) | desired_child, p);
				pr_debug("%s: %02x -> %02x\n", __func__, x, y);
			}

			break;
		}
		case sizeof(uint16_t): {
			uint16_t x, y;

			x = ioread16(p);
			y = (x & ~s->mask) | desired_child;
			if (x != y) {
				iowrite16((x & ~s->mask) | desired_child, p);
				pr_debug("%s: %04x -> %04x\n", __func__, x, y);
			}

			break;
		}
		case sizeof(uint32_t): {
			uint32_t x, y;

			x = ioread32(p);
			y = (x & ~s->mask) | desired_child;
			if (x != y) {
				iowrite32((x & ~s->mask) | desired_child, p);
				pr_debug("%s: %08x -> %08x\n", __func__, x, y);
			}

			break;
		}
		}

		iounmap(p);
	}

	return 0;
}

static int mdio_mux_mmioreg_probe(struct platform_device *pdev)
{
	struct device_node *np2, *np = pdev->dev.of_node;
	struct mdio_mux_mmioreg_state *s;
	struct resource res;
	const __be32 *iprop;
	int len, ret;

	dev_dbg(&pdev->dev, "probing node %s\n", np->full_name);

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "could not obtain memory map for node %s\n",
			np->full_name);
		return ret;
	}
	s->phys = res.start;

	s->iosize = resource_size(&res);
	if (s->iosize != sizeof(uint8_t) &&
	    s->iosize != sizeof(uint16_t) &&
	    s->iosize != sizeof(uint32_t)) {
		dev_err(&pdev->dev, "only 8/16/32-bit registers are supported\n");
		return -EINVAL;
	}

	iprop = of_get_property(np, "mux-mask", &len);
	if (!iprop || len != sizeof(uint32_t)) {
		dev_err(&pdev->dev, "missing or invalid mux-mask property\n");
		return -ENODEV;
	}
	if (be32_to_cpup(iprop) >= BIT(s->iosize * 8)) {
		dev_err(&pdev->dev, "only 8/16/32-bit registers are supported\n");
		return -EINVAL;
	}
	s->mask = be32_to_cpup(iprop);

	/*
	 * Verify that the 'reg' property of each child MDIO bus does not
	 * set any bits outside of the 'mask'.
	 */
	for_each_available_child_of_node(np, np2) {
		iprop = of_get_property(np2, "reg", &len);
		if (!iprop || len != sizeof(uint32_t)) {
			dev_err(&pdev->dev, "mdio-mux child node %s is "
				"missing a 'reg' property\n", np2->full_name);
			of_node_put(np2);
			return -ENODEV;
		}
		if (be32_to_cpup(iprop) & ~s->mask) {
			dev_err(&pdev->dev, "mdio-mux child node %s has "
				"a 'reg' value with unmasked bits\n",
				np2->full_name);
			of_node_put(np2);
			return -ENODEV;
		}
	}

	ret = mdio_mux_init(&pdev->dev, mdio_mux_mmioreg_switch_fn,
			    &s->mux_handle, s, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to register mdio-mux bus %s\n",
			np->full_name);
		return ret;
	}

	pdev->dev.platform_data = s;

	return 0;
}

static int mdio_mux_mmioreg_remove(struct platform_device *pdev)
{
	struct mdio_mux_mmioreg_state *s = dev_get_platdata(&pdev->dev);

	mdio_mux_uninit(s->mux_handle);

	return 0;
}

static const struct of_device_id mdio_mux_mmioreg_match[] = {
	{
		.compatible = "mdio-mux-mmioreg",
	},
	{},
};
MODULE_DEVICE_TABLE(of, mdio_mux_mmioreg_match);

static struct platform_driver mdio_mux_mmioreg_driver = {
	.driver = {
		.name		= "mdio-mux-mmioreg",
		.of_match_table = mdio_mux_mmioreg_match,
	},
	.probe		= mdio_mux_mmioreg_probe,
	.remove		= mdio_mux_mmioreg_remove,
};

module_platform_driver(mdio_mux_mmioreg_driver);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Memory-mapped device MDIO MUX driver");
MODULE_LICENSE("GPL v2");
