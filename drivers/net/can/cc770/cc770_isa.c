/*
 * Copyright (C) 2009, 2011 Wolfgang Grandegger <wg@grandegger.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/can.h>
#include <linux/can/dev.h>

#include "cc770.h"

#define DRV_NAME "cc770_isa"

#define MAXDEV 8

MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");
MODULE_DESCRIPTION("Socket-CAN driver for CC770 on the ISA bus");
MODULE_LICENSE("GPL v2");

#define CLK_DEFAULT	16000000	/* 16 MHz */
#define BCR_DEFAULT	0x00
#define COR_DEFAULT	0x00

static unsigned long port[MAXDEV];
static unsigned long mem[MAXDEV];
static int __devinitdata irq[MAXDEV];
static int __devinitdata clk[MAXDEV];
static u8 __devinitdata cir[MAXDEV] = {[0 ... (MAXDEV - 1)] = 0xff};
static u8 __devinitdata bcr[MAXDEV] = {[0 ... (MAXDEV - 1)] = 0xff};
static u8 __devinitdata cor[MAXDEV] = {[0 ... (MAXDEV - 1)] = 0xff};
static int __devinitdata indirect[MAXDEV] = {[0 ... (MAXDEV - 1)] = -1};

module_param_array(port, ulong, NULL, S_IRUGO);
MODULE_PARM_DESC(port, "I/O port number");

module_param_array(mem, ulong, NULL, S_IRUGO);
MODULE_PARM_DESC(mem, "I/O memory address");

module_param_array(indirect, int, NULL, S_IRUGO);
MODULE_PARM_DESC(indirect, "Indirect access via address and data port");

module_param_array(irq, int, NULL, S_IRUGO);
MODULE_PARM_DESC(irq, "IRQ number");

module_param_array(clk, int, NULL, S_IRUGO);
MODULE_PARM_DESC(clk, "External oscillator clock frequency "
		 "(default=16000000 [16 MHz])");

module_param_array(cir, byte, NULL, S_IRUGO);
MODULE_PARM_DESC(cir, "CPU interface register (default=0x40 [CPU_DSC])");

module_param_array(bcr, byte, NULL, S_IRUGO);
MODULE_PARM_DESC(ocr, "Bus configuration register (default=0x00)");

module_param_array(cor, byte, NULL, S_IRUGO);
MODULE_PARM_DESC(cor, "Clockout register (default=0x00)");

#define CC770_IOSIZE          0x20
#define CC770_IOSIZE_INDIRECT 0x02

static struct platform_device *cc770_isa_devs[MAXDEV];

static u8 cc770_isa_mem_read_reg(const struct cc770_priv *priv, int reg)
{
	return readb(priv->reg_base + reg);
}

static void cc770_isa_mem_write_reg(const struct cc770_priv *priv,
				      int reg, u8 val)
{
	writeb(val, priv->reg_base + reg);
}

static u8 cc770_isa_port_read_reg(const struct cc770_priv *priv, int reg)
{
	return inb((unsigned long)priv->reg_base + reg);
}

static void cc770_isa_port_write_reg(const struct cc770_priv *priv,
				       int reg, u8 val)
{
	outb(val, (unsigned long)priv->reg_base + reg);
}

static u8 cc770_isa_port_read_reg_indirect(const struct cc770_priv *priv,
					     int reg)
{
	unsigned long base = (unsigned long)priv->reg_base;

	outb(reg, base);
	return inb(base + 1);
}

static void cc770_isa_port_write_reg_indirect(const struct cc770_priv *priv,
						int reg, u8 val)
{
	unsigned long base = (unsigned long)priv->reg_base;

	outb(reg, base);
	outb(val, base + 1);
}

static int __devinit cc770_isa_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct cc770_priv *priv;
	void __iomem *base = NULL;
	int iosize = CC770_IOSIZE;
	int idx = pdev->id;
	int err;
	u32 clktmp;

	dev_dbg(&pdev->dev, "probing idx=%d: port=%#lx, mem=%#lx, irq=%d\n",
		idx, port[idx], mem[idx], irq[idx]);
	if (mem[idx]) {
		if (!request_mem_region(mem[idx], iosize, DRV_NAME)) {
			err = -EBUSY;
			goto exit;
		}
		base = ioremap_nocache(mem[idx], iosize);
		if (!base) {
			err = -ENOMEM;
			goto exit_release;
		}
	} else {
		if (indirect[idx] > 0 ||
		    (indirect[idx] == -1 && indirect[0] > 0))
			iosize = CC770_IOSIZE_INDIRECT;
		if (!request_region(port[idx], iosize, DRV_NAME)) {
			err = -EBUSY;
			goto exit;
		}
	}

	dev = alloc_cc770dev(0);
	if (!dev) {
		err = -ENOMEM;
		goto exit_unmap;
	}
	priv = netdev_priv(dev);

	dev->irq = irq[idx];
	priv->irq_flags = IRQF_SHARED;
	if (mem[idx]) {
		priv->reg_base = base;
		dev->base_addr = mem[idx];
		priv->read_reg = cc770_isa_mem_read_reg;
		priv->write_reg = cc770_isa_mem_write_reg;
	} else {
		priv->reg_base = (void __iomem *)port[idx];
		dev->base_addr = port[idx];

		if (iosize == CC770_IOSIZE_INDIRECT) {
			priv->read_reg = cc770_isa_port_read_reg_indirect;
			priv->write_reg = cc770_isa_port_write_reg_indirect;
		} else {
			priv->read_reg = cc770_isa_port_read_reg;
			priv->write_reg = cc770_isa_port_write_reg;
		}
	}

	if (clk[idx])
		clktmp = clk[idx];
	else if (clk[0])
		clktmp = clk[0];
	else
		clktmp = CLK_DEFAULT;
	priv->can.clock.freq = clktmp;

	if (cir[idx] != 0xff) {
		priv->cpu_interface = cir[idx] & 0xff;
	} else if (cir[0] != 0xff) {
		priv->cpu_interface = cir[0] & 0xff;
	} else {
		/* The system clock may not exceed 10 MHz */
		if (clktmp > 10000000) {
			priv->cpu_interface |= CPUIF_DSC;
			clktmp /= 2;
		}
		/* The memory clock may not exceed 8 MHz */
		if (clktmp > 8000000)
			priv->cpu_interface |= CPUIF_DMC;
	}

	if (priv->cpu_interface & CPUIF_DSC)
		priv->can.clock.freq /= 2;

	if (bcr[idx] != 0xff)
		priv->bus_config = bcr[idx] & 0xff;
	else if (bcr[0] != 0xff)
		priv->bus_config = bcr[0] & 0xff;
	else
		priv->bus_config = BCR_DEFAULT;

	if (cor[idx] != 0xff)
		priv->clkout = cor[idx];
	else if (cor[0] != 0xff)
		priv->clkout = cor[0] & 0xff;
	else
		priv->clkout = COR_DEFAULT;

	dev_set_drvdata(&pdev->dev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_cc770dev(dev);
	if (err) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			DRV_NAME, err);
		goto exit_unmap;
	}

	dev_info(&pdev->dev, "%s device registered (reg_base=0x%p, irq=%d)\n",
		 DRV_NAME, priv->reg_base, dev->irq);
	return 0;

 exit_unmap:
	if (mem[idx])
		iounmap(base);
 exit_release:
	if (mem[idx])
		release_mem_region(mem[idx], iosize);
	else
		release_region(port[idx], iosize);
 exit:
	return err;
}

static int __devexit cc770_isa_remove(struct platform_device *pdev)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct cc770_priv *priv = netdev_priv(dev);
	int idx = pdev->id;

	unregister_cc770dev(dev);
	dev_set_drvdata(&pdev->dev, NULL);

	if (mem[idx]) {
		iounmap(priv->reg_base);
		release_mem_region(mem[idx], CC770_IOSIZE);
	} else {
		if (priv->read_reg == cc770_isa_port_read_reg_indirect)
			release_region(port[idx], CC770_IOSIZE_INDIRECT);
		else
			release_region(port[idx], CC770_IOSIZE);
	}
	free_cc770dev(dev);

	return 0;
}

static struct platform_driver cc770_isa_driver = {
	.probe = cc770_isa_probe,
	.remove = __devexit_p(cc770_isa_remove),
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init cc770_isa_init(void)
{
	int idx, err;

	for (idx = 0; idx < MAXDEV; idx++) {
		if ((port[idx] || mem[idx]) && irq[idx]) {
			cc770_isa_devs[idx] =
				platform_device_alloc(DRV_NAME, idx);
			if (!cc770_isa_devs[idx]) {
				err = -ENOMEM;
				goto exit_free_devices;
			}
			err = platform_device_add(cc770_isa_devs[idx]);
			if (err) {
				platform_device_put(cc770_isa_devs[idx]);
				goto exit_free_devices;
			}
			pr_debug("%s: platform device %d: port=%#lx, mem=%#lx, "
				 "irq=%d\n",
				 DRV_NAME, idx, port[idx], mem[idx], irq[idx]);
		} else if (idx == 0 || port[idx] || mem[idx]) {
				pr_err("%s: insufficient parameters supplied\n",
				       DRV_NAME);
				err = -EINVAL;
				goto exit_free_devices;
		}
	}

	err = platform_driver_register(&cc770_isa_driver);
	if (err)
		goto exit_free_devices;

	pr_info("Legacy %s driver for max. %d devices registered\n",
		DRV_NAME, MAXDEV);

	return 0;

exit_free_devices:
	while (--idx >= 0) {
		if (cc770_isa_devs[idx])
			platform_device_unregister(cc770_isa_devs[idx]);
	}

	return err;
}
module_init(cc770_isa_init);

static void __exit cc770_isa_exit(void)
{
	int idx;

	platform_driver_unregister(&cc770_isa_driver);
	for (idx = 0; idx < MAXDEV; idx++) {
		if (cc770_isa_devs[idx])
			platform_device_unregister(cc770_isa_devs[idx]);
	}
}
module_exit(cc770_isa_exit);
