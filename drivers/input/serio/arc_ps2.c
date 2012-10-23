/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver is originally developed by Pavel Sokolov <psokolov@synopsys.com>
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#define ARC_PS2_PORTS                   2

#define ARC_ARC_PS2_ID                  0x0001f609

#define STAT_TIMEOUT                    128

#define PS2_STAT_RX_FRM_ERR             (1)
#define PS2_STAT_RX_BUF_OVER            (1 << 1)
#define PS2_STAT_RX_INT_EN              (1 << 2)
#define PS2_STAT_RX_VAL                 (1 << 3)
#define PS2_STAT_TX_ISNOT_FUL           (1 << 4)
#define PS2_STAT_TX_INT_EN              (1 << 5)

struct arc_ps2_port {
	void __iomem *data_addr;
	void __iomem *status_addr;
	struct serio *io;
};

struct arc_ps2_data {
	struct arc_ps2_port port[ARC_PS2_PORTS];
	struct resource *iomem_res;
	int irq;
	void __iomem *addr;
	unsigned int frame_error;
	unsigned int buf_overflow;
	unsigned int total_int;
};

static void arc_ps2_check_rx(struct arc_ps2_data *arc_ps2,
			     struct arc_ps2_port *port)
{
	unsigned int timeout = 1000;
	unsigned int flag, status;
	unsigned char data;

	do {
		status = ioread32(port->status_addr);
		if (!(status & PS2_STAT_RX_VAL))
			return;

		data = ioread32(port->data_addr) & 0xff;

		flag = 0;
		arc_ps2->total_int++;
		if (status & PS2_STAT_RX_FRM_ERR) {
			arc_ps2->frame_error++;
			flag |= SERIO_PARITY;
		} else if (status & PS2_STAT_RX_BUF_OVER) {
			arc_ps2->buf_overflow++;
			flag |= SERIO_FRAME;
		}

		serio_interrupt(port->io, data, flag);
	} while (--timeout);

	dev_err(&port->io->dev, "PS/2 hardware stuck\n");
}

static irqreturn_t arc_ps2_interrupt(int irq, void *dev)
{
	struct arc_ps2_data *arc_ps2 = dev;
	int i;

	for (i = 0; i < ARC_PS2_PORTS; i++)
		arc_ps2_check_rx(arc_ps2, &arc_ps2->port[i]);

	return IRQ_HANDLED;
}

static int arc_ps2_write(struct serio *io, unsigned char val)
{
	unsigned status;
	struct arc_ps2_port *port = io->port_data;
	int timeout = STAT_TIMEOUT;

	do {
		status = ioread32(port->status_addr);
		cpu_relax();

		if (status & PS2_STAT_TX_ISNOT_FUL) {
			iowrite32(val & 0xff, port->data_addr);
			return 0;
		}

	} while (--timeout);

	dev_err(&io->dev, "write timeout\n");
	return -ETIMEDOUT;
}

static int arc_ps2_open(struct serio *io)
{
	struct arc_ps2_port *port = io->port_data;

	iowrite32(PS2_STAT_RX_INT_EN, port->status_addr);

	return 0;
}

static void arc_ps2_close(struct serio *io)
{
	struct arc_ps2_port *port = io->port_data;

	iowrite32(ioread32(port->status_addr) & ~PS2_STAT_RX_INT_EN,
		  port->status_addr);
}

static int __devinit arc_ps2_create_port(struct platform_device *pdev,
					 struct arc_ps2_data *arc_ps2,
					 int index)
{
	struct arc_ps2_port *port = &arc_ps2->port[index];
	struct serio *io;

	io = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!io)
		return -ENOMEM;

	io->id.type = SERIO_8042;
	io->write = arc_ps2_write;
	io->open = arc_ps2_open;
	io->close = arc_ps2_close;
	snprintf(io->name, sizeof(io->name), "ARC PS/2 port%d", index);
	snprintf(io->phys, sizeof(io->phys), "arc/serio%d", index);
	io->port_data = port;

	port->io = io;

	port->data_addr = arc_ps2->addr + 4 + index * 4;
	port->status_addr = arc_ps2->addr + 4 + ARC_PS2_PORTS * 4 + index * 4;

	dev_dbg(&pdev->dev, "port%d is allocated (data = 0x%p, status = 0x%p)\n",
		index, port->data_addr, port->status_addr);

	serio_register_port(port->io);
	return 0;
}

static int __devinit arc_ps2_probe(struct platform_device *pdev)
{
	struct arc_ps2_data *arc_ps2;
	int error, id, i;

	arc_ps2 = kzalloc(sizeof(struct arc_ps2_data), GFP_KERNEL);
	if (!arc_ps2) {
		dev_err(&pdev->dev, "out of memory\n");
		return -ENOMEM;
	}

	arc_ps2->iomem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!arc_ps2->iomem_res) {
		dev_err(&pdev->dev, "no IO memory defined\n");
		error = -EINVAL;
		goto err_free_mem;
	}

	arc_ps2->irq = platform_get_irq_byname(pdev, "arc_ps2_irq");
	if (arc_ps2->irq < 0) {
		dev_err(&pdev->dev, "no IRQ defined\n");
		error = -EINVAL;
		goto err_free_mem;
	}

	if (!request_mem_region(arc_ps2->iomem_res->start,
	    resource_size(arc_ps2->iomem_res), pdev->name)) {
		dev_err(&pdev->dev, "memory region allocation failed for %pR\n",
			arc_ps2->iomem_res);

		error = -EBUSY;
		goto err_free_mem;
	}

	arc_ps2->addr = ioremap_nocache(arc_ps2->iomem_res->start,
					resource_size(arc_ps2->iomem_res));
	if (!arc_ps2->addr) {
		dev_err(&pdev->dev, "memory mapping failed\n");
		error = -ENOMEM;
		goto err_release_region;
	}

	dev_info(&pdev->dev, "irq = %d, address = 0x%p, ports = %i\n",
		 arc_ps2->irq, arc_ps2->addr, ARC_PS2_PORTS);

	id = ioread32(arc_ps2->addr);
	if (id != ARC_ARC_PS2_ID) {
		dev_err(&pdev->dev, "device id does not match\n");
		error = -ENXIO;
		goto err_unmap;
	}

	for (i = 0; i < ARC_PS2_PORTS; i++) {
		error = arc_ps2_create_port(pdev, arc_ps2, i);
		if (error)
			goto err_unregister_ports;
	}

	error = request_irq(arc_ps2->irq, arc_ps2_interrupt, 0,
			    "arc_ps2", arc_ps2);
	if (error) {
		dev_err(&pdev->dev, "Could not allocate IRQ\n");
		goto err_unregister_ports;
	}

	platform_set_drvdata(pdev, arc_ps2);

	return 0;

err_unregister_ports:
	for (i = 0; i < ARC_PS2_PORTS; i++) {
		if (arc_ps2->port[i].io)
			serio_unregister_port(arc_ps2->port[i].io);
	}
err_unmap:
	iounmap(arc_ps2->addr);
err_release_region:
	release_mem_region(arc_ps2->iomem_res->start,
			   resource_size(arc_ps2->iomem_res));
err_free_mem:
	kfree(arc_ps2);
	return error;
}

static int __devexit arc_ps2_remove(struct platform_device *pdev)
{
	struct arc_ps2_data *arc_ps2 = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARC_PS2_PORTS; i++)
		serio_unregister_port(arc_ps2->port[i].io);

	free_irq(arc_ps2->irq, arc_ps2);
	iounmap(arc_ps2->addr);
	release_mem_region(arc_ps2->iomem_res->start,
			   resource_size(arc_ps2->iomem_res));

	dev_dbg(&pdev->dev, "interrupt count = %i\n", arc_ps2->total_int);
	dev_dbg(&pdev->dev, "frame error count = %i\n", arc_ps2->frame_error);
	dev_dbg(&pdev->dev, "buffer overflow count = %i\n",
		arc_ps2->buf_overflow);

	kfree(arc_ps2);

	return 0;
}

static struct platform_driver arc_ps2_driver = {
	.driver	= {
		.name	= "arc_ps2",
		.owner	= THIS_MODULE,
	},
	.probe	= arc_ps2_probe,
	.remove	= __devexit_p(arc_ps2_remove),
};

module_platform_driver(arc_ps2_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pavel Sokolov <psokolov@synopsys.com>");
MODULE_DESCRIPTION("ARC PS/2 Driver");
