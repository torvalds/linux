/*
 * Xilinx Zynq SMC Driver
 *
 * Copyright (C) 2012 - 2013 Xilinx, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Currently only a single SMC instance is supported.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/memory/zynq-smc.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Register definitions */
#define XSMCPS_MEMC_STATUS_OFFS		0	/* Controller status reg, RO */
#define XSMCPS_CFG_CLR_OFFS		0xC	/* Clear config reg, WO */
#define XSMCPS_DIRECT_CMD_OFFS		0x10	/* Direct command reg, WO */
#define XSMCPS_SET_CYCLES_OFFS		0x14	/* Set cycles register, WO */
#define XSMCPS_SET_OPMODE_OFFS		0x18	/* Set opmode register, WO */
#define XSMCPS_ECC_STATUS_OFFS		0x400	/* ECC status register */
#define XSMCPS_ECC_MEMCFG_OFFS		0x404	/* ECC mem config reg */
#define XSMCPS_ECC_MEMCMD1_OFFS		0x408	/* ECC mem cmd1 reg */
#define XSMCPS_ECC_MEMCMD2_OFFS		0x40C	/* ECC mem cmd2 reg */
#define XSMCPS_ECC_VALUE0_OFFS		0x418	/* ECC value 0 reg */

#define XSMCPS_CFG_CLR_INT_1	0x10
#define XSMCPS_ECC_STATUS_BUSY	(1 << 6)
#define XSMCPS_DC_UPT_NAND_REGS	((4 << 23) |	/* CS: NAND chip */ \
				 (2 << 21))	/* UpdateRegs operation */

#define XNANDPS_ECC_CMD1	((0x80)       |	/* Write command */ \
				 (0 << 8)     |	/* Read command */ \
				 (0x30 << 16) |	/* Read End command */ \
				 (1 << 24))	/* Read End command calid */

#define XNANDPS_ECC_CMD2	((0x85)	      |	/* Write col change cmd */ \
				 (5 << 8)     |	/* Read col change cmd */ \
				 (0xE0 << 16) |	/* Read col change end cmd */ \
				 (1 << 24)) /* Read col change end cmd valid */
/**
 * struct xsmcps_data
 * @devclk		Pointer to the peripheral clock
 * @aperclk		Pointer to the APER clock
 * @clk_rate_change_nb	Notifier block for clock frequency change callback
 */
struct xsmcps_data {
	struct clk		*devclk;
	struct clk		*aperclk;
	struct notifier_block	clk_rate_change_nb;
	struct resource		*res;
};

/* SMC virtual register base */
static void __iomem *xsmcps_base;
static DEFINE_SPINLOCK(xsmcps_lock);

/**
 * xsmcps_set_buswidth - Set memory buswidth
 * @bw	Memory buswidth (8 | 16)
 * Returns 0 on success or negative errno.
 *
 * Must be called with xsmcps_lock held.
 */
static int xsmcps_set_buswidth(unsigned int bw)
{
	u32 reg;

	if (bw != 8 && bw != 16)
		return -EINVAL;

	reg = readl(xsmcps_base + XSMCPS_SET_OPMODE_OFFS);
	reg &= ~3;
	if (bw == 16)
		reg |= 1;
	writel(reg, xsmcps_base + XSMCPS_SET_OPMODE_OFFS);

	return 0;
}

/**
 * xsmcps_set_cycles - Set memory timing parameters
 * @t0	t_rc		read cycle time
 * @t1	t_wc		write cycle time
 * @t2	t_rea/t_ceoe	output enable assertion delay
 * @t3	t_wp		write enable deassertion delay
 * @t4	t_clr/t_pc	page cycle time
 * @t5	t_ar/t_ta	ID read time/turnaround time
 * @t6	t_rr		busy to RE timing
 *
 * Sets NAND chip specific timing parameters.
 *
 * Must be called with xsmcps_lock held.
 */
static void xsmcps_set_cycles(u32 t0, u32 t1, u32 t2, u32 t3, u32
		t4, u32 t5, u32 t6)
{
	t0 &= 0xf;
	t1 = (t1 & 0xf) << 4;
	t2 = (t2 & 7) << 8;
	t3 = (t3 & 7) << 11;
	t4 = (t4 & 7) << 14;
	t5 = (t5 & 7) << 17;
	t6 = (t6 & 0xf) << 20;

	t0 |= t1 | t2 | t3 | t4 | t5 | t6;

	writel(t0, xsmcps_base + XSMCPS_SET_CYCLES_OFFS);
}

/**
 * xsmcps_ecc_is_busy_noirq - Read ecc busy flag
 * Returns the ecc_status bit from the ecc_status register. 1 = busy, 0 = idle
 *
 * Must be called with xsmcps_lock held.
 */
static int xsmcps_ecc_is_busy_noirq(void)
{
	return !!(readl(xsmcps_base + XSMCPS_ECC_STATUS_OFFS) &
			XSMCPS_ECC_STATUS_BUSY);
}

/**
 * xsmcps_ecc_is_busy - Read ecc busy flag
 * Returns the ecc_status bit from the ecc_status register. 1 = busy, 0 = idle
 */
int xsmcps_ecc_is_busy(void)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&xsmcps_lock, flags);

	ret = xsmcps_ecc_is_busy_noirq();

	spin_unlock_irqrestore(&xsmcps_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(xsmcps_ecc_is_busy);

/**
 * xsmcps_get_ecc_val - Read ecc_valueN registers
 * @ecc_reg	Index of the ecc_value reg (0..3)
 * Returns the content of the requested ecc_value register.
 *
 * There are four valid ecc_value registers. The argument is truncated to stay
 * within this valid boundary.
 */
u32 xsmcps_get_ecc_val(int ecc_reg)
{
	u32 reg;
	u32 addr;
	unsigned long flags;

	ecc_reg &= 3;
	addr = XSMCPS_ECC_VALUE0_OFFS + (ecc_reg << 2);

	spin_lock_irqsave(&xsmcps_lock, flags);

	reg = readl(xsmcps_base + addr);

	spin_unlock_irqrestore(&xsmcps_lock, flags);

	return reg;
}
EXPORT_SYMBOL_GPL(xsmcps_get_ecc_val);

/**
 * xsmcps_get_nand_int_status_raw - Get NAND interrupt status bit
 * Returns the raw_int_status1 bit from the memc_status register
 */
int xsmcps_get_nand_int_status_raw(void)
{
	u32 reg;
	unsigned long flags;

	spin_lock_irqsave(&xsmcps_lock, flags);

	reg = readl(xsmcps_base + XSMCPS_MEMC_STATUS_OFFS);

	spin_unlock_irqrestore(&xsmcps_lock, flags);

	reg >>= 6;
	reg &= 1;

	return reg;
}
EXPORT_SYMBOL_GPL(xsmcps_get_nand_int_status_raw);

/**
 * xsmcps_clr_nand_int - Clear NAND interrupt
 */
void xsmcps_clr_nand_int(void)
{
	unsigned long flags;

	spin_lock_irqsave(&xsmcps_lock, flags);

	writel(XSMCPS_CFG_CLR_INT_1, xsmcps_base + XSMCPS_CFG_CLR_OFFS);

	spin_unlock_irqrestore(&xsmcps_lock, flags);
}
EXPORT_SYMBOL_GPL(xsmcps_clr_nand_int);

/**
 * xsmcps_set_ecc_mode - Set SMC ECC mode
 * @mode	ECC mode (BYPASS, APB, MEM)
 * Returns 0 on success or negative errno.
 */
int xsmcps_set_ecc_mode(enum xsmcps_ecc_mode mode)
{
	u32 reg;
	unsigned long flags;
	int ret = 0;

	switch (mode) {
	case XSMCPS_ECCMODE_BYPASS:
	case XSMCPS_ECCMODE_APB:
	case XSMCPS_ECCMODE_MEM:
		spin_lock_irqsave(&xsmcps_lock, flags);

		reg = readl(xsmcps_base + XSMCPS_ECC_MEMCFG_OFFS);
		reg &= ~0xc;
		reg |= mode << 2;
		writel(reg, xsmcps_base + XSMCPS_ECC_MEMCFG_OFFS);

		spin_unlock_irqrestore(&xsmcps_lock, flags);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(xsmcps_set_ecc_mode);

/**
 * xsmcps_set_ecc_pg_size - Set SMC ECC page size
 * @pg_sz	ECC page size
 * Returns 0 on success or negative errno.
 */
int xsmcps_set_ecc_pg_size(unsigned int pg_sz)
{
	u32 reg;
	u32 sz;
	unsigned long flags;

	switch (pg_sz) {
	case 0:
		sz = 0;
		break;
	case 512:
		sz = 1;
		break;
	case 1024:
		sz = 2;
		break;
	case 2048:
		sz = 3;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&xsmcps_lock, flags);

	reg = readl(xsmcps_base + XSMCPS_ECC_MEMCFG_OFFS);
	reg &= ~3;
	reg |= sz;
	writel(reg, xsmcps_base + XSMCPS_ECC_MEMCFG_OFFS);

	spin_unlock_irqrestore(&xsmcps_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(xsmcps_set_ecc_pg_size);

static int xsmcps_clk_notifier_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{

	switch (event) {
	case PRE_RATE_CHANGE:
		/*
		 * if a rate change is announced we need to check whether we can
		 * run under the changed conditions
		 */
		/* fall through */
	case POST_RATE_CHANGE:
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

#ifdef CONFIG_PM_SLEEP
static int xsmcps_suspend(struct device *dev)
{
	struct xsmcps_data *xsmcps = dev_get_drvdata(dev);

	clk_disable(xsmcps->devclk);
	clk_disable(xsmcps->aperclk);

	return 0;
}

static int xsmcps_resume(struct device *dev)
{
	int ret;
	struct xsmcps_data *xsmcps = dev_get_drvdata(dev);

	ret = clk_enable(xsmcps->aperclk);
	if (ret) {
		dev_err(dev, "Cannot enable APER clock.\n");
		return ret;
	}

	ret = clk_enable(xsmcps->devclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xsmcps->aperclk);
		return ret;
	}
	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(xsmcps_dev_pm_ops, xsmcps_suspend, xsmcps_resume);

/**
 * xsmcps_init_nand_interface - Initialize the NAND interface
 * @pdev	Pointer to the platform_device struct
 * @nand_node	Pointer to the xnandps device_node struct
 */
static void xsmcps_init_nand_interface(struct platform_device *pdev,
		struct device_node *nand_node)
{
	u32 t_rc, t_wc, t_rea, t_wp, t_clr, t_ar, t_rr;
	unsigned int bw;
	int err;
	unsigned long flags;

	err = of_property_read_u32(nand_node, "xlnx,nand-width", &bw);
	if (err) {
		dev_warn(&pdev->dev,
				"xlnx,nand-width not in device tree, using 8");
		bw = 8;
	}
	/* nand-cycle-<X> property is refer to the NAND flash timing
	 * mapping between dts and the NAND flash AC timing
	 *  X  : AC timing name
	 *  t0 : t_rc
	 *  t1 : t_wc
	 *  t2 : t_rea
	 *  t3 : t_wp
	 *  t4 : t_clr
	 *  t5 : t_ar
	 *  t6 : t_rr
	 */
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t0", &t_rc);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t0 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t1", &t_wc);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t1 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t2", &t_rea);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t2 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t3", &t_wp);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t3 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t4", &t_clr);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t4 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t5", &t_ar);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t5 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t6", &t_rr);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t6 not in device tree");
		goto default_nand_timing;
	}

default_nand_timing:
	if (err) {
		/* set default NAND flash timing property */
		dev_warn(&pdev->dev, "Using default timing for");
		dev_warn(&pdev->dev, "2Gb Numonyx MT29F2G08ABAEAWP NAND flash");
		dev_warn(&pdev->dev, "t_wp, t_clr, t_ar are set to 4");
		dev_warn(&pdev->dev, "t_rc, t_wc, t_rr are set to 2");
		dev_warn(&pdev->dev, "t_rea is set to 1");
		t_rc = t_wc = t_rr = 4;
		t_rea = 1;
		t_wp = t_clr = t_ar = 2;
	}

	spin_lock_irqsave(&xsmcps_lock, flags);

	if (xsmcps_set_buswidth(bw)) {
		dev_warn(&pdev->dev, "xlnx,nand-width not valid, using 8");
		xsmcps_set_buswidth(8);
	}

	/*
	 * Default assume 50MHz clock (20ns cycle time) and 3V operation
	 * The SET_CYCLES_REG register value depends on the flash device.
	 * Look in to the device datasheet and change its value, This value
	 * is for 2Gb Numonyx flash.
	 */
	xsmcps_set_cycles(t_rc, t_wc, t_rea, t_wp, t_clr, t_ar, t_rr);
	writel(XSMCPS_CFG_CLR_INT_1, xsmcps_base + XSMCPS_CFG_CLR_OFFS);
	writel(XSMCPS_DC_UPT_NAND_REGS, xsmcps_base + XSMCPS_DIRECT_CMD_OFFS);
	/* Wait till the ECC operation is complete */
	while (xsmcps_ecc_is_busy_noirq())
		cpu_relax();
	/* Set the command1 and command2 register */
	writel(XNANDPS_ECC_CMD1, xsmcps_base + XSMCPS_ECC_MEMCMD1_OFFS);
	writel(XNANDPS_ECC_CMD2, xsmcps_base + XSMCPS_ECC_MEMCMD2_OFFS);

	spin_unlock_irqrestore(&xsmcps_lock, flags);
}

const struct of_device_id matches_nor[] = {
	{.compatible = "cfi-flash"},
	{}
};
const struct of_device_id matches_nand[] = {
	{.compatible = "xlnx,ps7-nand-1.00.a"},
	{}
};

static int xsmcps_probe(struct platform_device *pdev)
{
	struct xsmcps_data *xsmcps;
	struct device_node *child;
	unsigned long flags;
	int err;
	struct device_node *of_node = pdev->dev.of_node;
	const struct of_device_id *matches = NULL;

	xsmcps = kzalloc(sizeof(*xsmcps), GFP_KERNEL);
	if (!xsmcps) {
		dev_err(&pdev->dev, "unable to allocate memory\n");
		return -ENOMEM;
	}

	xsmcps->aperclk = clk_get(&pdev->dev, "aper_clk");
	if (IS_ERR(xsmcps->aperclk)) {
		dev_err(&pdev->dev, "aper_clk clock not found.\n");
		err = PTR_ERR(xsmcps->aperclk);
		goto out_free;
	}

	xsmcps->devclk = clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xsmcps->devclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		err = PTR_ERR(xsmcps->devclk);
		goto out_clk_put_aper;
	}

	err = clk_prepare_enable(xsmcps->aperclk);
	if (err) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		goto out_clk_put;
	}

	err = clk_prepare_enable(xsmcps->devclk);
	if (err) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto out_clk_dis_aper;
	}

	platform_set_drvdata(pdev, xsmcps);

	xsmcps->clk_rate_change_nb.notifier_call = xsmcps_clk_notifier_cb;
	if (clk_notifier_register(xsmcps->devclk, &xsmcps->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");

	/* Get the NAND controller virtual address */
	xsmcps->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!xsmcps->res) {
		err = -ENODEV;
		dev_err(&pdev->dev, "platform_get_resource failed\n");
		goto out_clk_disable;
	}
	xsmcps->res = request_mem_region(xsmcps->res->start,
			resource_size(xsmcps->res), pdev->name);
	if (!xsmcps->res) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "request_mem_region failed\n");
		goto out_clk_disable;
	}

	xsmcps_base = ioremap(xsmcps->res->start, resource_size(xsmcps->res));
	if (!xsmcps_base) {
		err = -EIO;
		dev_err(&pdev->dev, "ioremap failed\n");
		goto out_release_mem_region;
	}

	/* clear interrupts */
	spin_lock_irqsave(&xsmcps_lock, flags);

	writel(0x52, xsmcps_base + XSMCPS_CFG_CLR_OFFS);

	spin_unlock_irqrestore(&xsmcps_lock, flags);

	/* Find compatible children. Only a single child is supported */
	for_each_available_child_of_node(of_node, child) {
		if (of_match_node(matches_nand, child)) {
			xsmcps_init_nand_interface(pdev, child);
			if (!matches) {
				matches = matches_nand;
			} else {
				dev_err(&pdev->dev,
					"incompatible configuration\n");
				goto out_release_mem_region;
			}
		}

		if (of_match_node(matches_nor, child)) {
			static int counts = 0;
			if (!matches) {
				matches = matches_nor;
			} else {
				if (matches != matches_nor || counts > 1) {
					dev_err(&pdev->dev,
						"incompatible configuration\n");
					goto out_release_mem_region;
				}
			}
			counts++;
		}
	}

	if (matches)
		of_platform_populate(of_node, matches, NULL, &pdev->dev);

	return 0;

out_release_mem_region:
	release_mem_region(xsmcps->res->start, resource_size(xsmcps->res));
	kfree(xsmcps->res);
out_clk_disable:
	clk_disable_unprepare(xsmcps->devclk);
out_clk_dis_aper:
	clk_disable_unprepare(xsmcps->aperclk);
out_clk_put:
	clk_put(xsmcps->devclk);
out_clk_put_aper:
	clk_put(xsmcps->aperclk);
out_free:
	kfree(xsmcps);

	return err;
}

static int xsmcps_remove(struct platform_device *pdev)
{
	struct xsmcps_data *xsmcps = platform_get_drvdata(pdev);

	clk_notifier_unregister(xsmcps->devclk, &xsmcps->clk_rate_change_nb);
	release_mem_region(xsmcps->res->start, resource_size(xsmcps->res));
	kfree(xsmcps->res);
	iounmap(xsmcps_base);
	clk_disable_unprepare(xsmcps->devclk);
	clk_disable_unprepare(xsmcps->aperclk);
	clk_put(xsmcps->devclk);
	clk_put(xsmcps->aperclk);
	kfree(xsmcps);

	return 0;
}

/* Match table for device tree binding */
static const struct of_device_id xsmcps_of_match[] = {
	{.compatible = "xlnx,ps7-smc"},
	{ },
};
MODULE_DEVICE_TABLE(of, xsmcps_of_match);

static struct platform_driver xsmcps_driver = {
	.probe		= xsmcps_probe,
	.remove		= xsmcps_remove,
	.driver		= {
		.name	= "xsmcps",
		.owner	= THIS_MODULE,
		.pm	= &xsmcps_dev_pm_ops,
		.of_match_table = xsmcps_of_match,
	},
};

module_platform_driver(xsmcps_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx PS SMC Driver");
MODULE_LICENSE("GPL");
