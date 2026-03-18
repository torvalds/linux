// SPDX-License-Identifier: GPL-2.0-only
/*
 * Largely copied from rzn1_irqmux.c
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MPFS_IRQMUX_CR			0x54
#define MPFS_IRQMUX_NUM_CHILDREN	96
#define MPFS_IRQMUX_NUM_DIRECT		38
#define MPFS_IRQMUX_DIRECT_START	13
#define MPFS_IRQMUX_DIRECT_END		50
#define MPFS_IRQMUX_NONDIRECT_END	53

static int mpfs_irqmux_is_direct_mode(struct device *dev,
				      const struct of_phandle_args *parent_args)
{
	if (parent_args->args_count != 1) {
		dev_err(dev, "Invalid interrupt-map item\n");
		return -EINVAL;
	}

	if (parent_args->args[0] < MPFS_IRQMUX_DIRECT_START ||
			parent_args->args[0] > MPFS_IRQMUX_NONDIRECT_END) {
		dev_err(dev, "Invalid interrupt %u\n", parent_args->args[0]);
		return -EINVAL;
	}

	if (parent_args->args[0] > MPFS_IRQMUX_DIRECT_END)
		return 0;

	return 1;
}

static int mpfs_irqmux_probe(struct platform_device *pdev)
{
	DECLARE_BITMAP(child_done, MPFS_IRQMUX_NUM_CHILDREN) = {};
	DECLARE_BITMAP(parent_done, MPFS_IRQMUX_NUM_DIRECT) = {};
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct of_imap_parser imap_parser;
	struct of_imap_item imap_item;
	struct regmap *regmap;
	int ret, direct_mode, line, controller, gpio, parent_line;
	u32 tmp, val = 0, old;

	regmap = device_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to find syscon regmap\n");

	/* We support only #interrupt-cells = <1> and #address-cells = <0> */
	ret = of_property_read_u32(np, "#interrupt-cells", &tmp);
	if (ret)
		return ret;
	if (tmp != 1)
		return -EINVAL;

	ret = of_property_read_u32(np, "#address-cells", &tmp);
	if (ret)
		return ret;
	if (tmp != 0)
		return -EINVAL;

	ret = of_imap_parser_init(&imap_parser, np, &imap_item);
	if (ret)
		return ret;

	for_each_of_imap_item(&imap_parser, &imap_item) {

		direct_mode = mpfs_irqmux_is_direct_mode(dev, &imap_item.parent_args);
		if (direct_mode < 0) {
			of_node_put(imap_item.parent_args.np);
			return direct_mode;
		}

		line = imap_item.child_imap[0];
		gpio = line % 32;
		controller = line / 32;

		if (controller > 2) {
			of_node_put(imap_item.parent_args.np);
			dev_err(dev, "child interrupt number too large: %d\n", line);
			return -EINVAL;
		}

		if (test_and_set_bit(line, child_done)) {
			of_node_put(imap_item.parent_args.np);
			dev_err(dev, "mux child line %d already defined in interrupt-map\n",
				line);
			return -EINVAL;
		}

		parent_line = imap_item.parent_args.args[0] - MPFS_IRQMUX_DIRECT_START;
		if (direct_mode && test_and_set_bit(parent_line, parent_done)) {
			of_node_put(imap_item.parent_args.np);
			dev_err(dev, "mux parent line %d already defined in interrupt-map\n",
				line);
			return -EINVAL;
		}

		/*
		 * There are 41 interrupts assigned to GPIOs, of which 38 are "direct". Since the
		 * mux has 32 bits only, 6 of these exclusive/"direct" interrupts remain. These
		 * are used by GPIO controller 1's lines 18 to 23. Nothing needs to be done
		 * for these interrupts.
		 */
		if (controller == 1 && gpio >= 18)
			continue;

		/*
		 * The mux has a single register, where bits 0 to 13 mux between GPIO controller
		 * 1's 14 GPIOs and GPIO controller 2's first 14 GPIOs. The remaining bits mux
		 * between the first 18 GPIOs of controller 1 and the last 18 GPIOS of
		 * controller 2. If a bit in the mux's control register is set, the
		 * corresponding interrupt line for GPIO controller 0 or 1 will be put in
		 * "non-direct" mode. If cleared, the "fabric" controller's will.
		 *
		 * Register layout:
		 *    GPIO 1 interrupt line 17 | mux bit 31 | GPIO 2 interrupt line 31
		 *    ...                      | ...        | ...
		 *    ...                      | ...        | ...
		 *    GPIO 1 interrupt line  0 | mux bit 14 | GPIO 2 interrupt line 14
		 *    GPIO 0 interrupt line 13 | mux bit 13 | GPIO 2 interrupt line 13
		 *    ...                      | ...        | ...
		 *    ...                      | ...        | ...
		 *    GPIO 0 interrupt line  0 | mux bit  0 | GPIO 2 interrupt line  0
		 *
		 * As the binding mandates 70 items, one for each GPIO line, there's no need to
		 * handle anything for GPIO controller 2, since the bit will be set for the
		 * corresponding line in GPIO controller 0 or 1.
		 */
		if (controller == 2)
			continue;

		/*
		 * If in direct mode, the bit is cleared, nothing needs to be done as val is zero
		 * initialised and that's the direct mode setting for GPIO controller 0 and 1.
		 */
		if (direct_mode)
			continue;

		if (controller == 0)
			val |= 1U << gpio;
		else
			val |= 1U << (gpio + 14);
	}

	regmap_read(regmap, MPFS_IRQMUX_CR, &old);
	regmap_write(regmap, MPFS_IRQMUX_CR, val);

	if (val != old)
		dev_info(dev, "firmware mux setting of 0x%x overwritten to 0x%x\n", old, val);

	return 0;
}

static const struct of_device_id mpfs_irqmux_of_match[] = {
	{ .compatible = "microchip,mpfs-irqmux", },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_irqmux_of_match);

static struct platform_driver mpfs_irqmux_driver = {
	.probe = mpfs_irqmux_probe,
	.driver = {
		.name = "mpfs_irqmux",
		.of_match_table = mpfs_irqmux_of_match,
	},
};
module_platform_driver(mpfs_irqmux_driver);

MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("Polarfire SoC interrupt mux driver");
MODULE_LICENSE("GPL");
