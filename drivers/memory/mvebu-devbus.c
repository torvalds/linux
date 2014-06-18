/*
 * Marvell EBU SoC Device Bus Controller
 * (memory controller for NOR/NAND/SRAM/FPGA devices)
 *
 * Copyright (C) 2013-2014 Marvell
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/mbus.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

/* Register definitions */
#define ARMADA_DEV_WIDTH_SHIFT		30
#define ARMADA_BADR_SKEW_SHIFT		28
#define ARMADA_RD_HOLD_SHIFT		23
#define ARMADA_ACC_NEXT_SHIFT		17
#define ARMADA_RD_SETUP_SHIFT		12
#define ARMADA_ACC_FIRST_SHIFT		6

#define ARMADA_SYNC_ENABLE_SHIFT	24
#define ARMADA_WR_HIGH_SHIFT		16
#define ARMADA_WR_LOW_SHIFT		8

#define ARMADA_READ_PARAM_OFFSET	0x0
#define ARMADA_WRITE_PARAM_OFFSET	0x4

#define ORION_RESERVED			(0x2 << 30)
#define ORION_BADR_SKEW_SHIFT		28
#define ORION_WR_HIGH_EXT_BIT		BIT(27)
#define ORION_WR_HIGH_EXT_MASK		0x8
#define ORION_WR_LOW_EXT_BIT		BIT(26)
#define ORION_WR_LOW_EXT_MASK		0x8
#define ORION_ALE_WR_EXT_BIT		BIT(25)
#define ORION_ALE_WR_EXT_MASK		0x8
#define ORION_ACC_NEXT_EXT_BIT		BIT(24)
#define ORION_ACC_NEXT_EXT_MASK		0x10
#define ORION_ACC_FIRST_EXT_BIT		BIT(23)
#define ORION_ACC_FIRST_EXT_MASK	0x10
#define ORION_TURN_OFF_EXT_BIT		BIT(22)
#define ORION_TURN_OFF_EXT_MASK		0x8
#define ORION_DEV_WIDTH_SHIFT		20
#define ORION_WR_HIGH_SHIFT		17
#define ORION_WR_HIGH_MASK		0x7
#define ORION_WR_LOW_SHIFT		14
#define ORION_WR_LOW_MASK		0x7
#define ORION_ALE_WR_SHIFT		11
#define ORION_ALE_WR_MASK		0x7
#define ORION_ACC_NEXT_SHIFT		7
#define ORION_ACC_NEXT_MASK		0xF
#define ORION_ACC_FIRST_SHIFT		3
#define ORION_ACC_FIRST_MASK		0xF
#define ORION_TURN_OFF_SHIFT		0
#define ORION_TURN_OFF_MASK		0x7

struct devbus_read_params {
	u32 bus_width;
	u32 badr_skew;
	u32 turn_off;
	u32 acc_first;
	u32 acc_next;
	u32 rd_setup;
	u32 rd_hold;
};

struct devbus_write_params {
	u32 sync_enable;
	u32 wr_high;
	u32 wr_low;
	u32 ale_wr;
};

struct devbus {
	struct device *dev;
	void __iomem *base;
	unsigned long tick_ps;
};

static int get_timing_param_ps(struct devbus *devbus,
			       struct device_node *node,
			       const char *name,
			       u32 *ticks)
{
	u32 time_ps;
	int err;

	err = of_property_read_u32(node, name, &time_ps);
	if (err < 0) {
		dev_err(devbus->dev, "%s has no '%s' property\n",
			name, node->full_name);
		return err;
	}

	*ticks = (time_ps + devbus->tick_ps - 1) / devbus->tick_ps;

	dev_dbg(devbus->dev, "%s: %u ps -> 0x%x\n",
		name, time_ps, *ticks);
	return 0;
}

static int devbus_get_timing_params(struct devbus *devbus,
				    struct device_node *node,
				    struct devbus_read_params *r,
				    struct devbus_write_params *w)
{
	int err;

	err = of_property_read_u32(node, "devbus,bus-width", &r->bus_width);
	if (err < 0) {
		dev_err(devbus->dev,
			"%s has no 'devbus,bus-width' property\n",
			node->full_name);
		return err;
	}

	/*
	 * The bus width is encoded into the register as 0 for 8 bits,
	 * and 1 for 16 bits, so we do the necessary conversion here.
	 */
	if (r->bus_width == 8)
		r->bus_width = 0;
	else if (r->bus_width == 16)
		r->bus_width = 1;
	else {
		dev_err(devbus->dev, "invalid bus width %d\n", r->bus_width);
		return -EINVAL;
	}

	err = get_timing_param_ps(devbus, node, "devbus,badr-skew-ps",
				 &r->badr_skew);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,turn-off-ps",
				 &r->turn_off);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,acc-first-ps",
				 &r->acc_first);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,acc-next-ps",
				 &r->acc_next);
	if (err < 0)
		return err;

	if (of_device_is_compatible(devbus->dev->of_node, "marvell,mvebu-devbus")) {
		err = get_timing_param_ps(devbus, node, "devbus,rd-setup-ps",
					  &r->rd_setup);
		if (err < 0)
			return err;

		err = get_timing_param_ps(devbus, node, "devbus,rd-hold-ps",
					  &r->rd_hold);
		if (err < 0)
			return err;

		err = of_property_read_u32(node, "devbus,sync-enable",
					   &w->sync_enable);
		if (err < 0) {
			dev_err(devbus->dev,
				"%s has no 'devbus,sync-enable' property\n",
				node->full_name);
			return err;
		}
	}

	err = get_timing_param_ps(devbus, node, "devbus,ale-wr-ps",
				 &w->ale_wr);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,wr-low-ps",
				 &w->wr_low);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,wr-high-ps",
				 &w->wr_high);
	if (err < 0)
		return err;

	return 0;
}

static void devbus_orion_set_timing_params(struct devbus *devbus,
					  struct device_node *node,
					  struct devbus_read_params *r,
					  struct devbus_write_params *w)
{
	u32 value;

	/*
	 * The hardware designers found it would be a good idea to
	 * split most of the values in the register into two fields:
	 * one containing all the low-order bits, and another one
	 * containing just the high-order bit. For all of those
	 * fields, we have to split the value into these two parts.
	 */
	value =	(r->turn_off   & ORION_TURN_OFF_MASK)  << ORION_TURN_OFF_SHIFT  |
		(r->acc_first  & ORION_ACC_FIRST_MASK) << ORION_ACC_FIRST_SHIFT |
		(r->acc_next   & ORION_ACC_NEXT_MASK)  << ORION_ACC_NEXT_SHIFT  |
		(w->ale_wr     & ORION_ALE_WR_MASK)    << ORION_ALE_WR_SHIFT    |
		(w->wr_low     & ORION_WR_LOW_MASK)    << ORION_WR_LOW_SHIFT    |
		(w->wr_high    & ORION_WR_HIGH_MASK)   << ORION_WR_HIGH_SHIFT   |
		r->bus_width                           << ORION_DEV_WIDTH_SHIFT |
		((r->turn_off  & ORION_TURN_OFF_EXT_MASK)  ? ORION_TURN_OFF_EXT_BIT  : 0) |
		((r->acc_first & ORION_ACC_FIRST_EXT_MASK) ? ORION_ACC_FIRST_EXT_BIT : 0) |
		((r->acc_next  & ORION_ACC_NEXT_EXT_MASK)  ? ORION_ACC_NEXT_EXT_BIT  : 0) |
		((w->ale_wr    & ORION_ALE_WR_EXT_MASK)    ? ORION_ALE_WR_EXT_BIT    : 0) |
		((w->wr_low    & ORION_WR_LOW_EXT_MASK)    ? ORION_WR_LOW_EXT_BIT    : 0) |
		((w->wr_high   & ORION_WR_HIGH_EXT_MASK)   ? ORION_WR_HIGH_EXT_BIT   : 0) |
		(r->badr_skew << ORION_BADR_SKEW_SHIFT) |
		ORION_RESERVED;

	writel(value, devbus->base);
}

static void devbus_armada_set_timing_params(struct devbus *devbus,
					   struct device_node *node,
					   struct devbus_read_params *r,
					   struct devbus_write_params *w)
{
	u32 value;

	/* Set read timings */
	value = r->bus_width << ARMADA_DEV_WIDTH_SHIFT |
		r->badr_skew << ARMADA_BADR_SKEW_SHIFT |
		r->rd_hold   << ARMADA_RD_HOLD_SHIFT   |
		r->acc_next  << ARMADA_ACC_NEXT_SHIFT  |
		r->rd_setup  << ARMADA_RD_SETUP_SHIFT  |
		r->acc_first << ARMADA_ACC_FIRST_SHIFT |
		r->turn_off;

	dev_dbg(devbus->dev, "read parameters register 0x%p = 0x%x\n",
		devbus->base + ARMADA_READ_PARAM_OFFSET,
		value);

	writel(value, devbus->base + ARMADA_READ_PARAM_OFFSET);

	/* Set write timings */
	value = w->sync_enable  << ARMADA_SYNC_ENABLE_SHIFT |
		w->wr_low       << ARMADA_WR_LOW_SHIFT      |
		w->wr_high      << ARMADA_WR_HIGH_SHIFT     |
		w->ale_wr;

	dev_dbg(devbus->dev, "write parameters register: 0x%p = 0x%x\n",
		devbus->base + ARMADA_WRITE_PARAM_OFFSET,
		value);

	writel(value, devbus->base + ARMADA_WRITE_PARAM_OFFSET);
}

static int mvebu_devbus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct devbus_read_params r;
	struct devbus_write_params w;
	struct devbus *devbus;
	struct resource *res;
	struct clk *clk;
	unsigned long rate;
	int err;

	devbus = devm_kzalloc(&pdev->dev, sizeof(struct devbus), GFP_KERNEL);
	if (!devbus)
		return -ENOMEM;

	devbus->dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	devbus->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(devbus->base))
		return PTR_ERR(devbus->base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	clk_prepare_enable(clk);

	/*
	 * Obtain clock period in picoseconds,
	 * we need this in order to convert timing
	 * parameters from cycles to picoseconds.
	 */
	rate = clk_get_rate(clk) / 1000;
	devbus->tick_ps = 1000000000 / rate;

	dev_dbg(devbus->dev, "Setting timing parameter, tick is %lu ps\n",
		devbus->tick_ps);

	if (!of_property_read_bool(node, "devbus,keep-config")) {
		/* Read the Device Tree node */
		err = devbus_get_timing_params(devbus, node, &r, &w);
		if (err < 0)
			return err;

		/* Set the new timing parameters */
		if (of_device_is_compatible(node, "marvell,orion-devbus"))
			devbus_orion_set_timing_params(devbus, node, &r, &w);
		else
			devbus_armada_set_timing_params(devbus, node, &r, &w);
	}

	/*
	 * We need to create a child device explicitly from here to
	 * guarantee that the child will be probed after the timing
	 * parameters for the bus are written.
	 */
	err = of_platform_populate(node, NULL, NULL, dev);
	if (err < 0)
		return err;

	return 0;
}

static const struct of_device_id mvebu_devbus_of_match[] = {
	{ .compatible = "marvell,mvebu-devbus" },
	{ .compatible = "marvell,orion-devbus" },
	{},
};
MODULE_DEVICE_TABLE(of, mvebu_devbus_of_match);

static struct platform_driver mvebu_devbus_driver = {
	.probe		= mvebu_devbus_probe,
	.driver		= {
		.name	= "mvebu-devbus",
		.owner	= THIS_MODULE,
		.of_match_table = mvebu_devbus_of_match,
	},
};

static int __init mvebu_devbus_init(void)
{
	return platform_driver_register(&mvebu_devbus_driver);
}
module_init(mvebu_devbus_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ezequiel Garcia <ezequiel.garcia@free-electrons.com>");
MODULE_DESCRIPTION("Marvell EBU SoC Device Bus controller");
