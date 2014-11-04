/*
 * Copyright (C) 2009 Wolfgang Grandegger <wg@grandegger.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/can/dev.h>
#include <linux/can/platform/sja1000.h>

#include "sja1000.h"

#define DRV_NAME "sja1000_isa"

#define MAXDEV 8

MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");
MODULE_DESCRIPTION("Socket-CAN driver for SJA1000 on the ISA bus");
MODULE_LICENSE("GPL v2");

#define CLK_DEFAULT	16000000	/* 16 MHz */
#define CDR_DEFAULT	(CDR_CBP | CDR_CLK_OFF)
#define OCR_DEFAULT	OCR_TX0_PUSHPULL

static unsigned long port[MAXDEV];
static unsigned long mem[MAXDEV];
static int irq[MAXDEV];
static int clk[MAXDEV];
static unsigned char cdr[MAXDEV] = {[0 ... (MAXDEV - 1)] = 0xff};
static unsigned char ocr[MAXDEV] = {[0 ... (MAXDEV - 1)] = 0xff};
static int indirect[MAXDEV] = {[0 ... (MAXDEV - 1)] = -1};
static spinlock_t indirect_lock[MAXDEV];  /* lock for indirect access mode */

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

module_param_array(cdr, byte, NULL, S_IRUGO);
MODULE_PARM_DESC(cdr, "Clock divider register "
		 "(default=0x48 [CDR_CBP | CDR_CLK_OFF])");

module_param_array(ocr, byte, NULL, S_IRUGO);
MODULE_PARM_DESC(ocr, "Output control register "
		 "(default=0x18 [OCR_TX0_PUSHPULL])");

#define SJA1000_IOSIZE          0x20
#define SJA1000_IOSIZE_INDIRECT 0x02

static struct platform_device *sja1000_isa_devs[MAXDEV];

static u8 sja1000_isa_mem_read_reg(const struct sja1000_priv *priv, int reg)
{
	return readb(priv->reg_base + reg);
}

static void sja1000_isa_mem_write_reg(const struct sja1000_priv *priv,
				      int reg, u8 val)
{
	writeb(val, priv->reg_base + reg);
}

static u8 sja1000_isa_port_read_reg(const struct sja1000_priv *priv, int reg)
{
	return inb((unsigned long)priv->reg_base + reg);
}

static void sja1000_isa_port_write_reg(const struct sja1000_priv *priv,
				       int reg, u8 val)
{
	outb(val, (unsigned long)priv->reg_base + reg);
}

static u8 sja1000_isa_port_read_reg_indirect(const struct sja1000_priv *priv,
					     int reg)
{
	unsigned long flags, base = (unsigned long)priv->reg_base;
	u8 readval;

	spin_lock_irqsave(&indirect_lock[priv->dev->dev_id], flags);
	outb(reg, base);
	readval = inb(base + 1);
	spin_unlock_irqrestore(&indirect_lock[priv->dev->dev_id], flags);

	return readval;
}

static void sja1000_isa_port_write_reg_indirect(const struct sja1000_priv *priv,
						int reg, u8 val)
{
	unsigned long flags, base = (unsigned long)priv->reg_base;

	spin_lock_irqsave(&indirect_lock[priv->dev->dev_id], flags);
	outb(reg, base);
	outb(val, base + 1);
	spin_unlock_irqrestore(&indirect_lock[priv->dev->dev_id], flags);
}

static int sja1000_isa_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct sja1000_priv *priv;
	void __iomem *base = NULL;
	int iosize = SJA1000_IOSIZE;
	int idx = pdev->id;
	int err;

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
			iosize = SJA1000_IOSIZE_INDIRECT;
		if (!request_region(port[idx], iosize, DRV_NAME)) {
			err = -EBUSY;
			goto exit;
		}
	}

	dev = alloc_sja1000dev(0);
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
		priv->read_reg = sja1000_isa_mem_read_reg;
		priv->write_reg = sja1000_isa_mem_write_reg;
	} else {
		priv->reg_base = (void __iomem *)port[idx];
		dev->base_addr = port[idx];

		if (iosize == SJA1000_IOSIZE_INDIRECT) {
			priv->read_reg = sja1000_isa_port_read_reg_indirect;
			priv->write_reg = sja1000_isa_port_write_reg_indirect;
			spin_lock_init(&indirect_lock[idx]);
		} else {
			priv->read_reg = sja1000_isa_port_read_reg;
			priv->write_reg = sja1000_isa_port_write_reg;
		}
	}

	if (clk[idx])
		priv->can.clock.freq = clk[idx] / 2;
	else if (clk[0])
		priv->can.clock.freq = clk[0] / 2;
	else
		priv->can.clock.freq = CLK_DEFAULT / 2;

	if (ocr[idx] != 0xff)
		priv->ocr = ocr[idx];
	else if (ocr[0] != 0xff)
		priv->ocr = ocr[0];
	else
		priv->ocr = OCR_DEFAULT;

	if (cdr[idx] != 0xff)
		priv->cdr = cdr[idx];
	else if (cdr[0] != 0xff)
		priv->cdr = cdr[0];
	else
		priv->cdr = CDR_DEFAULT;

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	dev->dev_id = idx;

	err = register_sja1000dev(dev);
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

static int sja1000_isa_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct sja1000_priv *priv = netdev_priv(dev);
	int idx = pdev->id;

	unregister_sja1000dev(dev);

	if (mem[idx]) {
		iounmap(priv->reg_base);
		release_mem_region(mem[idx], SJA1000_IOSIZE);
	} else {
		if (priv->read_reg == sja1000_isa_port_read_reg_indirect)
			release_region(port[idx], SJA1000_IOSIZE_INDIRECT);
		else
			release_region(port[idx], SJA1000_IOSIZE);
	}
	free_sja1000dev(dev);

	return 0;
}

static struct platform_driver sja1000_isa_driver = {
	.probe = sja1000_isa_probe,
	.remove = sja1000_isa_remove,
	.driver = {
		.name = DRV_NAME,
	},
};

static int __init sja1000_isa_init(void)
{
	int idx, err;

	for (idx = 0; idx < MAXDEV; idx++) {
		if ((port[idx] || mem[idx]) && irq[idx]) {
			sja1000_isa_devs[idx] =
				platform_device_alloc(DRV_NAME, idx);
			if (!sja1000_isa_devs[idx]) {
				err = -ENOMEM;
				goto exit_free_devices;
			}
			err = platform_device_add(sja1000_isa_devs[idx]);
			if (err) {
				platform_device_put(sja1000_isa_devs[idx]);
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

	err = platform_driver_register(&sja1000_isa_driver);
	if (err)
		goto exit_free_devices;

	pr_info("Legacy %s driver for max. %d devices registered\n",
		DRV_NAME, MAXDEV);

	return 0;

exit_free_devices:
	while (--idx >= 0) {
		if (sja1000_isa_devs[idx])
			platform_device_unregister(sja1000_isa_devs[idx]);
	}

	return err;
}

static void __exit sja1000_isa_exit(void)
{
	int idx;

	platform_driver_unregister(&sja1000_isa_driver);
	for (idx = 0; idx < MAXDEV; idx++) {
		if (sja1000_isa_devs[idx])
			platform_device_unregister(sja1000_isa_devs[idx]);
	}
}

module_init(sja1000_isa_init);
module_exit(sja1000_isa_exit);
