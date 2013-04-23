/*
 * Marvell EBU SoC Device Bus Controller
 * (memory controller for NOR/NAND/SRAM/FPGA devices)
 *
 * Copyright (C) 2013 Marvell
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
#define DEV_WIDTH_BIT		30
#define BADR_SKEW_BIT		28
#define RD_HOLD_BIT		23
#define ACC_NEXT_BIT		17
#define RD_SETUP_BIT		12
#define ACC_FIRST_BIT		6

#define SYNC_ENABLE_BIT		24
#define WR_HIGH_BIT		16
#define WR_LOW_BIT		8

#define READ_PARAM_OFFSET	0x0
#define WRITE_PARAM_OFFSET	0x4

static const char * const devbus_wins[] = {
	"devbus-boot",
	"devbus-cs0",
	"devbus-cs1",
	"devbus-cs2",
	"devbus-cs3",
};

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

static int devbus_set_timing_params(struct devbus *devbus,
				    struct device_node *node)
{
	struct devbus_read_params r;
	struct devbus_write_params w;
	u32 value;
	int err;

	dev_dbg(devbus->dev, "Setting timing parameter, tick is %lu ps\n",
		devbus->tick_ps);

	/* Get read timings */
	err = of_property_read_u32(node, "devbus,bus-width", &r.bus_width);
	if (err < 0) {
		dev_err(devbus->dev,
			"%s has no 'devbus,bus-width' property\n",
			node->full_name);
		return err;
	}
	/* Convert bit width to byte width */
	r.bus_width /= 8;

	err = get_timing_param_ps(devbus, node, "devbus,badr-skew-ps",
				 &r.badr_skew);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,turn-off-ps",
				 &r.turn_off);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,acc-first-ps",
				 &r.acc_first);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,acc-next-ps",
				 &r.acc_next);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,rd-setup-ps",
				 &r.rd_setup);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,rd-hold-ps",
				 &r.rd_hold);
	if (err < 0)
		return err;

	/* Get write timings */
	err = of_property_read_u32(node, "devbus,sync-enable",
				  &w.sync_enable);
	if (err < 0) {
		dev_err(devbus->dev,
			"%s has no 'devbus,sync-enable' property\n",
			node->full_name);
		return err;
	}

	err = get_timing_param_ps(devbus, node, "devbus,ale-wr-ps",
				 &w.ale_wr);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,wr-low-ps",
				 &w.wr_low);
	if (err < 0)
		return err;

	err = get_timing_param_ps(devbus, node, "devbus,wr-high-ps",
				 &w.wr_high);
	if (err < 0)
		return err;

	/* Set read timings */
	value = r.bus_width << DEV_WIDTH_BIT |
		r.badr_skew << BADR_SKEW_BIT |
		r.rd_hold   << RD_HOLD_BIT   |
		r.acc_next  << ACC_NEXT_BIT  |
		r.rd_setup  << RD_SETUP_BIT  |
		r.acc_first << ACC_FIRST_BIT |
		r.turn_off;

	dev_dbg(devbus->dev, "read parameters register 0x%p = 0x%x\n",
		devbus->base + READ_PARAM_OFFSET,
		value);

	writel(value, devbus->base + READ_PARAM_OFFSET);

	/* Set write timings */
	value = w.sync_enable  << SYNC_ENABLE_BIT |
		w.wr_low       << WR_LOW_BIT      |
		w.wr_high      << WR_HIGH_BIT     |
		w.ale_wr;

	dev_dbg(devbus->dev, "write parameters register: 0x%p = 0x%x\n",
		devbus->base + WRITE_PARAM_OFFSET,
		value);

	writel(value, devbus->base + WRITE_PARAM_OFFSET);

	return 0;
}

static int mvebu_devbus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *parent;
	struct devbus *devbus;
	struct resource *res;
	struct clk *clk;
	unsigned long rate;
	const __be32 *ranges;
	int err, cs;
	int addr_cells, p_addr_cells, size_cells;
	int ranges_len, tuple_len;
	u32 base, size;

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

	/* Read the device tree node and set the new timing parameters */
	err = devbus_set_timing_params(devbus, node);
	if (err < 0)
		return err;

	/*
	 * Allocate an address window for this device.
	 * If the device probing fails, then we won't be able to
	 * remove the allocated address decoding window.
	 *
	 * FIXME: This is only a temporary hack! We need to do this here
	 * because we still don't have device tree bindings for mbus.
	 * Once that support is added, we will declare these address windows
	 * statically in the device tree, and remove the window configuration
	 * from here.
	 */

	/*
	 * Get the CS to choose the window string.
	 * This is a bit hacky, but it will be removed once the
	 * address windows are declared in the device tree.
	 */
	cs = (((unsigned long)devbus->base) % 0x400) / 8;

	/*
	 * Parse 'ranges' property to obtain a (base,size) window tuple.
	 * This will be removed once the address windows
	 * are declared in the device tree.
	 */
	parent = of_get_parent(node);
	if (!parent)
		return -EINVAL;

	p_addr_cells = of_n_addr_cells(parent);
	of_node_put(parent);

	addr_cells = of_n_addr_cells(node);
	size_cells = of_n_size_cells(node);
	tuple_len = (p_addr_cells + addr_cells + size_cells) * sizeof(__be32);

	ranges = of_get_property(node, "ranges", &ranges_len);
	if (ranges == NULL || ranges_len != tuple_len)
		return -EINVAL;

	base = of_translate_address(node, ranges + addr_cells);
	if (base == OF_BAD_ADDR)
		return -EINVAL;
	size = of_read_number(ranges + addr_cells + p_addr_cells, size_cells);

	/*
	 * Create an mbus address windows.
	 * FIXME: Remove this, together with the above code, once the
	 * address windows are declared in the device tree.
	 */
	err = mvebu_mbus_add_window(devbus_wins[cs], base, size);
	if (err < 0)
		return err;

	/*
	 * We need to create a child device explicitly from here to
	 * guarantee that the child will be probed after the timing
	 * parameters for the bus are written.
	 */
	err = of_platform_populate(node, NULL, NULL, dev);
	if (err < 0) {
		mvebu_mbus_del_window(base, size);
		return err;
	}

	return 0;
}

static const struct of_device_id mvebu_devbus_of_match[] = {
	{ .compatible = "marvell,mvebu-devbus" },
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
