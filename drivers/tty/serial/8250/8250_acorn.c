// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/drivers/serial/acorn.c
 *
 *  Copyright (C) 1996-2003 Russell King.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/ecard.h>
#include <asm/string.h>

#include "8250.h"

#define MAX_PORTS	3

struct serial_card_type {
	unsigned int	num_ports;
	unsigned int	uartclk;
	unsigned int	type;
	unsigned int	offset[MAX_PORTS];
};

struct serial_card_info {
	unsigned int	num_ports;
	int		ports[MAX_PORTS];
	void __iomem *vaddr;
};

static int
serial_card_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct serial_card_info *info;
	struct serial_card_type *type = id->data;
	struct uart_8250_port uart;
	unsigned long bus_addr;
	unsigned int i;

	info = kzalloc(sizeof(struct serial_card_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->num_ports = type->num_ports;

	bus_addr = ecard_resource_start(ec, type->type);
	info->vaddr = ecardm_iomap(ec, type->type, 0, 0);
	if (!info->vaddr) {
		kfree(info);
		return -ENOMEM;
	}

	ecard_set_drvdata(ec, info);

	memset(&uart, 0, sizeof(struct uart_8250_port));
	uart.port.irq	= ec->irq;
	uart.port.flags	= UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ;
	uart.port.uartclk	= type->uartclk;
	uart.port.iotype	= UPIO_MEM;
	uart.port.regshift	= 2;
	uart.port.dev	= &ec->dev;

	for (i = 0; i < info->num_ports; i++) {
		uart.port.membase = info->vaddr + type->offset[i];
		uart.port.mapbase = bus_addr + type->offset[i];

		info->ports[i] = serial8250_register_8250_port(&uart);
	}

	return 0;
}

static void serial_card_remove(struct expansion_card *ec)
{
	struct serial_card_info *info = ecard_get_drvdata(ec);
	int i;

	ecard_set_drvdata(ec, NULL);

	for (i = 0; i < info->num_ports; i++)
		if (info->ports[i] > 0)
			serial8250_unregister_port(info->ports[i]);

	kfree(info);
}

static struct serial_card_type atomwide_type = {
	.num_ports	= 3,
	.uartclk	= 7372800,
	.type		= ECARD_RES_IOCSLOW,
	.offset		= { 0x2800, 0x2400, 0x2000 },
};

static struct serial_card_type serport_type = {
	.num_ports	= 2,
	.uartclk	= 3686400,
	.type		= ECARD_RES_IOCSLOW,
	.offset		= { 0x2000, 0x2020 },
};

static const struct ecard_id serial_cids[] = {
	{ MANU_ATOMWIDE,	PROD_ATOMWIDE_3PSERIAL,	&atomwide_type	},
	{ MANU_SERPORT,		PROD_SERPORT_DSPORT,	&serport_type	},
	{ 0xffff, 0xffff }
};

static struct ecard_driver serial_card_driver = {
	.probe		= serial_card_probe,
	.remove		= serial_card_remove,
	.id_table	= serial_cids,
	.drv = {
		.name	= "8250_acorn",
	},
};

static int __init serial_card_init(void)
{
	return ecard_register_driver(&serial_card_driver);
}

static void __exit serial_card_exit(void)
{
	ecard_remove_driver(&serial_card_driver);
}

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Acorn 8250-compatible serial port expansion card driver");
MODULE_LICENSE("GPL");

module_init(serial_card_init);
module_exit(serial_card_exit);
