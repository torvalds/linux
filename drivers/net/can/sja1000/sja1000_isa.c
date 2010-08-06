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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/isa.h>
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
static int __devinitdata irq[MAXDEV];
static int __devinitdata clk[MAXDEV];
static char __devinitdata cdr[MAXDEV] = {[0 ... (MAXDEV - 1)] = -1};
static char __devinitdata ocr[MAXDEV] = {[0 ... (MAXDEV - 1)] = -1};
static char __devinitdata indirect[MAXDEV] = {[0 ... (MAXDEV - 1)] = -1};

module_param_array(port, ulong, NULL, S_IRUGO);
MODULE_PARM_DESC(port, "I/O port number");

module_param_array(mem, ulong, NULL, S_IRUGO);
MODULE_PARM_DESC(mem, "I/O memory address");

module_param_array(indirect, byte, NULL, S_IRUGO);
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
	unsigned long base = (unsigned long)priv->reg_base;

	outb(reg, base);
	return inb(base + 1);
}

static void sja1000_isa_port_write_reg_indirect(const struct sja1000_priv *priv,
						int reg, u8 val)
{
	unsigned long base = (unsigned long)priv->reg_base;

	outb(reg, base);
	outb(val, base + 1);
}

static int __devinit sja1000_isa_match(struct device *pdev, unsigned int idx)
{
	if (port[idx] || mem[idx]) {
		if (irq[idx])
			return 1;
	} else if (idx)
		return 0;

	dev_err(pdev, "insufficient parameters supplied\n");
	return 0;
}

static int __devinit sja1000_isa_probe(struct device *pdev, unsigned int idx)
{
	struct net_device *dev;
	struct sja1000_priv *priv;
	void __iomem *base = NULL;
	int iosize = SJA1000_IOSIZE;
	int err;

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

	if (ocr[idx] != -1)
		priv->ocr = ocr[idx] & 0xff;
	else if (ocr[0] != -1)
		priv->ocr = ocr[0] & 0xff;
	else
		priv->ocr = OCR_DEFAULT;

	if (cdr[idx] != -1)
		priv->cdr = cdr[idx] & 0xff;
	else if (cdr[0] != -1)
		priv->cdr = cdr[0] & 0xff;
	else
		priv->cdr = CDR_DEFAULT;

	dev_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, pdev);

	err = register_sja1000dev(dev);
	if (err) {
		dev_err(pdev, "registering %s failed (err=%d)\n",
			DRV_NAME, err);
		goto exit_unmap;
	}

	dev_info(pdev, "%s device registered (reg_base=0x%p, irq=%d)\n",
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

static int __devexit sja1000_isa_remove(struct device *pdev, unsigned int idx)
{
	struct net_device *dev = dev_get_drvdata(pdev);
	struct sja1000_priv *priv = netdev_priv(dev);

	unregister_sja1000dev(dev);
	dev_set_drvdata(pdev, NULL);

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

static struct isa_driver sja1000_isa_driver = {
	.match = sja1000_isa_match,
	.probe = sja1000_isa_probe,
	.remove = __devexit_p(sja1000_isa_remove),
	.driver = {
		.name = DRV_NAME,
	},
};

static int __init sja1000_isa_init(void)
{
	int err = isa_register_driver(&sja1000_isa_driver, MAXDEV);

	if (!err)
		printk(KERN_INFO
		       "Legacy %s driver for max. %d devices registered\n",
		       DRV_NAME, MAXDEV);
	return err;
}

static void __exit sja1000_isa_exit(void)
{
	isa_unregister_driver(&sja1000_isa_driver);
}

module_init(sja1000_isa_init);
module_exit(sja1000_isa_exit);
