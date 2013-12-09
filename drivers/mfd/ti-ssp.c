/*
 * Sequencer Serial Port (SSP) driver for Texas Instruments' SoCs
 *
 * Copyright (C) 2010 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ti_ssp.h>

/* Register Offsets */
#define REG_REV		0x00
#define REG_IOSEL_1	0x04
#define REG_IOSEL_2	0x08
#define REG_PREDIV	0x0c
#define REG_INTR_ST	0x10
#define REG_INTR_EN	0x14
#define REG_TEST_CTRL	0x18

/* Per port registers */
#define PORT_CFG_2	0x00
#define PORT_ADDR	0x04
#define PORT_DATA	0x08
#define PORT_CFG_1	0x0c
#define PORT_STATE	0x10

#define SSP_PORT_CONFIG_MASK	(SSP_EARLY_DIN | SSP_DELAY_DOUT)
#define SSP_PORT_CLKRATE_MASK	0x0f

#define SSP_SEQRAM_WR_EN	BIT(4)
#define SSP_SEQRAM_RD_EN	BIT(5)
#define SSP_START		BIT(15)
#define SSP_BUSY		BIT(10)
#define SSP_PORT_ASL		BIT(7)
#define SSP_PORT_CFO1		BIT(6)

#define SSP_PORT_SEQRAM_SIZE	32

static const int ssp_port_base[]   = {0x040, 0x080};
static const int ssp_port_seqram[] = {0x100, 0x180};

struct ti_ssp {
	struct resource		*res;
	struct device		*dev;
	void __iomem		*regs;
	spinlock_t		lock;
	struct clk		*clk;
	int			irq;
	wait_queue_head_t	wqh;

	/*
	 * Some of the iosel2 register bits always read-back as 0, we need to
	 * remember these values so that we don't clobber previously set
	 * values.
	 */
	u32			iosel2;
};

static inline struct ti_ssp *dev_to_ssp(struct device *dev)
{
	return dev_get_drvdata(dev->parent);
}

static inline int dev_to_port(struct device *dev)
{
	return to_platform_device(dev)->id;
}

/* Register Access Helpers, rmw() functions need to run locked */
static inline u32 ssp_read(struct ti_ssp *ssp, int reg)
{
	return __raw_readl(ssp->regs + reg);
}

static inline void ssp_write(struct ti_ssp *ssp, int reg, u32 val)
{
	__raw_writel(val, ssp->regs + reg);
}

static inline void ssp_rmw(struct ti_ssp *ssp, int reg, u32 mask, u32 bits)
{
	ssp_write(ssp, reg, (ssp_read(ssp, reg) & ~mask) | bits);
}

static inline u32 ssp_port_read(struct ti_ssp *ssp, int port, int reg)
{
	return ssp_read(ssp, ssp_port_base[port] + reg);
}

static inline void ssp_port_write(struct ti_ssp *ssp, int port, int reg,
				  u32 val)
{
	ssp_write(ssp, ssp_port_base[port] + reg, val);
}

static inline void ssp_port_rmw(struct ti_ssp *ssp, int port, int reg,
				u32 mask, u32 bits)
{
	ssp_rmw(ssp, ssp_port_base[port] + reg, mask, bits);
}

static inline void ssp_port_clr_bits(struct ti_ssp *ssp, int port, int reg,
				     u32 bits)
{
	ssp_port_rmw(ssp, port, reg, bits, 0);
}

static inline void ssp_port_set_bits(struct ti_ssp *ssp, int port, int reg,
				     u32 bits)
{
	ssp_port_rmw(ssp, port, reg, 0, bits);
}

/* Called to setup port clock mode, caller must hold ssp->lock */
static int __set_mode(struct ti_ssp *ssp, int port, int mode)
{
	mode &= SSP_PORT_CONFIG_MASK;
	ssp_port_rmw(ssp, port, PORT_CFG_1, SSP_PORT_CONFIG_MASK, mode);

	return 0;
}

int ti_ssp_set_mode(struct device *dev, int mode)
{
	struct ti_ssp *ssp = dev_to_ssp(dev);
	int port = dev_to_port(dev);
	int ret;

	spin_lock(&ssp->lock);
	ret = __set_mode(ssp, port, mode);
	spin_unlock(&ssp->lock);

	return ret;
}
EXPORT_SYMBOL(ti_ssp_set_mode);

/* Called to setup iosel2, caller must hold ssp->lock */
static void __set_iosel2(struct ti_ssp *ssp, u32 mask, u32 val)
{
	ssp->iosel2 = (ssp->iosel2 & ~mask) | val;
	ssp_write(ssp, REG_IOSEL_2, ssp->iosel2);
}

/* Called to setup port iosel, caller must hold ssp->lock */
static void __set_iosel(struct ti_ssp *ssp, int port, u32 iosel)
{
	unsigned val, shift = port ? 16 : 0;

	/* IOSEL1 gets the least significant 16 bits */
	val = ssp_read(ssp, REG_IOSEL_1);
	val &= 0xffff << (port ? 0 : 16);
	val |= (iosel & 0xffff) << (port ? 16 : 0);
	ssp_write(ssp, REG_IOSEL_1, val);

	/* IOSEL2 gets the most significant 16 bits */
	val = (iosel >> 16) & 0x7;
	__set_iosel2(ssp, 0x7 << shift, val << shift);
}

int ti_ssp_set_iosel(struct device *dev, u32 iosel)
{
	struct ti_ssp *ssp = dev_to_ssp(dev);
	int port = dev_to_port(dev);

	spin_lock(&ssp->lock);
	__set_iosel(ssp, port, iosel);
	spin_unlock(&ssp->lock);

	return 0;
}
EXPORT_SYMBOL(ti_ssp_set_iosel);

int ti_ssp_load(struct device *dev, int offs, u32* prog, int len)
{
	struct ti_ssp *ssp = dev_to_ssp(dev);
	int port = dev_to_port(dev);
	int i;

	if (len > SSP_PORT_SEQRAM_SIZE)
		return -ENOSPC;

	spin_lock(&ssp->lock);

	/* Enable SeqRAM access */
	ssp_port_set_bits(ssp, port, PORT_CFG_2, SSP_SEQRAM_WR_EN);

	/* Copy code */
	for (i = 0; i < len; i++) {
		__raw_writel(prog[i], ssp->regs + offs + 4*i +
			     ssp_port_seqram[port]);
	}

	/* Disable SeqRAM access */
	ssp_port_clr_bits(ssp, port, PORT_CFG_2, SSP_SEQRAM_WR_EN);

	spin_unlock(&ssp->lock);

	return 0;
}
EXPORT_SYMBOL(ti_ssp_load);

int ti_ssp_raw_read(struct device *dev)
{
	struct ti_ssp *ssp = dev_to_ssp(dev);
	int port = dev_to_port(dev);
	int shift = port ? 27 : 11;

	return (ssp_read(ssp, REG_IOSEL_2) >> shift) & 0xf;
}
EXPORT_SYMBOL(ti_ssp_raw_read);

int ti_ssp_raw_write(struct device *dev, u32 val)
{
	struct ti_ssp *ssp = dev_to_ssp(dev);
	int port = dev_to_port(dev), shift;

	spin_lock(&ssp->lock);

	shift = port ? 22 : 6;
	val &= 0xf;
	__set_iosel2(ssp, 0xf << shift, val << shift);

	spin_unlock(&ssp->lock);

	return 0;
}
EXPORT_SYMBOL(ti_ssp_raw_write);

static inline int __xfer_done(struct ti_ssp *ssp, int port)
{
	return !(ssp_port_read(ssp, port, PORT_CFG_1) & SSP_BUSY);
}

int ti_ssp_run(struct device *dev, u32 pc, u32 input, u32 *output)
{
	struct ti_ssp *ssp = dev_to_ssp(dev);
	int port = dev_to_port(dev);
	int ret;

	if (pc & ~(0x3f))
		return -EINVAL;

	/* Grab ssp->lock to serialize rmw on ssp registers */
	spin_lock(&ssp->lock);

	ssp_port_write(ssp, port, PORT_ADDR, input >> 16);
	ssp_port_write(ssp, port, PORT_DATA, input & 0xffff);
	ssp_port_rmw(ssp, port, PORT_CFG_1, 0x3f, pc);

	/* grab wait queue head lock to avoid race with the isr */
	spin_lock_irq(&ssp->wqh.lock);

	/* kick off sequence execution in hardware */
	ssp_port_set_bits(ssp, port, PORT_CFG_1, SSP_START);

	/* drop ssp lock; no register writes beyond this */
	spin_unlock(&ssp->lock);

	ret = wait_event_interruptible_locked_irq(ssp->wqh,
						  __xfer_done(ssp, port));
	spin_unlock_irq(&ssp->wqh.lock);

	if (ret < 0)
		return ret;

	if (output) {
		*output = (ssp_port_read(ssp, port, PORT_ADDR) << 16) |
			  (ssp_port_read(ssp, port, PORT_DATA) &  0xffff);
	}

	ret = ssp_port_read(ssp, port, PORT_STATE) & 0x3f; /* stop address */

	return ret;
}
EXPORT_SYMBOL(ti_ssp_run);

static irqreturn_t ti_ssp_interrupt(int irq, void *dev_data)
{
	struct ti_ssp *ssp = dev_data;

	spin_lock(&ssp->wqh.lock);

	ssp_write(ssp, REG_INTR_ST, 0x3);
	wake_up_locked(&ssp->wqh);

	spin_unlock(&ssp->wqh.lock);

	return IRQ_HANDLED;
}

static int ti_ssp_probe(struct platform_device *pdev)
{
	static struct ti_ssp *ssp;
	const struct ti_ssp_data *pdata = dev_get_platdata(&pdev->dev);
	int error = 0, prediv = 0xff, id;
	unsigned long sysclk;
	struct device *dev = &pdev->dev;
	struct mfd_cell cells[2];

	ssp = kzalloc(sizeof(*ssp), GFP_KERNEL);
	if (!ssp) {
		dev_err(dev, "cannot allocate device info\n");
		return -ENOMEM;
	}

	ssp->dev = dev;
	dev_set_drvdata(dev, ssp);

	ssp->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ssp->res) {
		error = -ENODEV;
		dev_err(dev, "cannot determine register area\n");
		goto error_res;
	}

	if (!request_mem_region(ssp->res->start, resource_size(ssp->res),
				pdev->name)) {
		error = -ENOMEM;
		dev_err(dev, "cannot claim register memory\n");
		goto error_res;
	}

	ssp->regs = ioremap(ssp->res->start, resource_size(ssp->res));
	if (!ssp->regs) {
		error = -ENOMEM;
		dev_err(dev, "cannot map register memory\n");
		goto error_map;
	}

	ssp->clk = clk_get(dev, NULL);
	if (IS_ERR(ssp->clk)) {
		error = PTR_ERR(ssp->clk);
		dev_err(dev, "cannot claim device clock\n");
		goto error_clk;
	}

	ssp->irq = platform_get_irq(pdev, 0);
	if (ssp->irq < 0) {
		error = -ENODEV;
		dev_err(dev, "unknown irq\n");
		goto error_irq;
	}

	error = request_threaded_irq(ssp->irq, NULL, ti_ssp_interrupt, 0,
				     dev_name(dev), ssp);
	if (error < 0) {
		dev_err(dev, "cannot acquire irq\n");
		goto error_irq;
	}

	spin_lock_init(&ssp->lock);
	init_waitqueue_head(&ssp->wqh);

	/* Power on and initialize SSP */
	error = clk_enable(ssp->clk);
	if (error) {
		dev_err(dev, "cannot enable device clock\n");
		goto error_enable;
	}

	/* Reset registers to a sensible known state */
	ssp_write(ssp, REG_IOSEL_1, 0);
	ssp_write(ssp, REG_IOSEL_2, 0);
	ssp_write(ssp, REG_INTR_EN, 0x3);
	ssp_write(ssp, REG_INTR_ST, 0x3);
	ssp_write(ssp, REG_TEST_CTRL, 0);
	ssp_port_write(ssp, 0, PORT_CFG_1, SSP_PORT_ASL);
	ssp_port_write(ssp, 1, PORT_CFG_1, SSP_PORT_ASL);
	ssp_port_write(ssp, 0, PORT_CFG_2, SSP_PORT_CFO1);
	ssp_port_write(ssp, 1, PORT_CFG_2, SSP_PORT_CFO1);

	sysclk = clk_get_rate(ssp->clk);
	if (pdata && pdata->out_clock)
		prediv = (sysclk / pdata->out_clock) - 1;
	prediv = clamp(prediv, 0, 0xff);
	ssp_rmw(ssp, REG_PREDIV, 0xff, prediv);

	memset(cells, 0, sizeof(cells));
	for (id = 0; id < 2; id++) {
		const struct ti_ssp_dev_data *data = &pdata->dev_data[id];

		cells[id].id		= id;
		cells[id].name		= data->dev_name;
		cells[id].platform_data	= data->pdata;
		cells[id].data_size	= data->pdata_size;
	}

	error = mfd_add_devices(dev, 0, cells, 2, NULL, 0, NULL);
	if (error < 0) {
		dev_err(dev, "cannot add mfd cells\n");
		goto error_enable;
	}

	return 0;

error_enable:
	free_irq(ssp->irq, ssp);
error_irq:
	clk_put(ssp->clk);
error_clk:
	iounmap(ssp->regs);
error_map:
	release_mem_region(ssp->res->start, resource_size(ssp->res));
error_res:
	kfree(ssp);
	return error;
}

static int ti_ssp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ti_ssp *ssp = dev_get_drvdata(dev);

	mfd_remove_devices(dev);
	clk_disable(ssp->clk);
	free_irq(ssp->irq, ssp);
	clk_put(ssp->clk);
	iounmap(ssp->regs);
	release_mem_region(ssp->res->start, resource_size(ssp->res));
	kfree(ssp);
	return 0;
}

static struct platform_driver ti_ssp_driver = {
	.probe		= ti_ssp_probe,
	.remove		= ti_ssp_remove,
	.driver		= {
		.name	= "ti-ssp",
		.owner	= THIS_MODULE,
	}
};

module_platform_driver(ti_ssp_driver);

MODULE_DESCRIPTION("Sequencer Serial Port (SSP) Driver");
MODULE_AUTHOR("Cyril Chemparathy");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ti-ssp");
