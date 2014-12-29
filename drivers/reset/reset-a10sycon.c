/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Reset control for Altera MAX5 Arria10 System Control
 * Adapted from reset-socfpga.c.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/a10sycon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/types.h>

/* Number of A10 System Controller Resets */
#define A10_RESETS		1

struct a10sycon_reset {
	struct reset_controller_dev     rcdev;
	struct work_struct              work;
	struct a10sycon                 *a10sc;
};

static inline struct a10sycon_reset *to_a10sc_rst(struct reset_controller_dev
						  *rc)
{
	return container_of(rc, struct a10sycon_reset, rcdev);
}

static int a10sycon_reset_assert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct a10sycon_reset *a10r = to_a10sc_rst(rcdev);

	int reg_offset = A10SYCON_REG_OFFSET(id);
	u8 value = A10SYCON_REG_BIT_MASK(id);
	int error;

	error = a10sycon_reg_update(a10r->a10sc,
				    A10SYCON_HPS_RST_WR_REG + reg_offset,
				    value, value);
	if (error < 0)
		dev_err(a10r->a10sc->dev, "Failed reset update, %d\n",
			error);
	return error;
}

static int a10sycon_reset_deassert(struct reset_controller_dev *rcdev,
				   unsigned long id)
{
	struct a10sycon_reset *a10r = to_a10sc_rst(rcdev);

	int reg_offset = A10SYCON_REG_OFFSET(id);
	u8 mask = A10SYCON_REG_BIT_MASK(id);
	int error;

	error = a10sycon_reg_update(a10r->a10sc,
				    A10SYCON_HPS_RST_WR_REG + reg_offset,
				    mask, 0);
	if (error < 0)
		dev_err(a10r->a10sc->dev, "Failed reset update, %d\n",
			error);
	return error;
}

static struct reset_control_ops a10sycon_reset_ops = {
	.assert		= a10sycon_reset_assert,
	.deassert	= a10sycon_reset_deassert,
};

static const struct of_device_id a10sycon_reset_of_match[];
static int a10sycon_reset_probe(struct platform_device *pdev)
{
	struct a10sycon_reset *a10r;
	struct device_node *np;

	/* Ensure we have a valid DT entry. */
	np = of_find_matching_node(NULL, a10sycon_reset_of_match);
	if (!np) {
		dev_err(&pdev->dev, "A10 Reset DT Entry not found\n");
		return -EINVAL;
	}

	if (!of_find_property(np, "#reset-cells", NULL)) {
		dev_err(&pdev->dev, "%s missing #reset-cells property\n",
			np->full_name);
		return -EINVAL;
	}

	a10r = devm_kzalloc(&pdev->dev, sizeof(struct a10sycon_reset),
			    GFP_KERNEL);
	if (!a10r)
		return -ENOMEM;

	a10r->rcdev.owner = THIS_MODULE;
	a10r->rcdev.nr_resets = A10_RESETS;
	a10r->rcdev.ops = &a10sycon_reset_ops;
	a10r->rcdev.of_node = np;
	reset_controller_register(&a10r->rcdev);

	platform_set_drvdata(pdev, a10r);

	return 0;
}

static int a10sycon_reset_remove(struct platform_device *pdev)
{
	struct a10sycon_reset *a10r = platform_get_drvdata(pdev);

	reset_controller_unregister(&a10r->rcdev);

	return 0;
}

static const struct of_device_id a10sycon_reset_of_match[] = {
	{ .compatible = "altr,a10sycon-reset" },
	{ },
};
MODULE_DEVICE_TABLE(of, a10sycon_reset_of_match);

static struct platform_driver a10sycon_reset_driver = {
	.probe	= a10sycon_reset_probe,
	.remove	= a10sycon_reset_remove,
	.driver = {
		.name		= "a10sycon-reset",
		.owner		= THIS_MODULE,
	},
};
module_platform_driver(a10sycon_reset_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer");
MODULE_DESCRIPTION("Altera Arria10 System Control Chip Reset");
