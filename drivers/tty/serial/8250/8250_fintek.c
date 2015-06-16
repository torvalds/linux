/*
 *  Probe for F81216A LPC to 4 UART
 *
 *  Based on drivers/tty/serial/8250_pnp.c, by Russell King, et al
 *
 *  Copyright (C) 2014 Ricardo Ribalda, Qtechnology A/S
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pnp.h>
#include <linux/kernel.h>
#include <linux/serial_core.h>
#include  "8250.h"

#define ADDR_PORT 0
#define DATA_PORT 1
#define ENTRY_KEY 0x77
#define EXIT_KEY 0xAA
#define CHIP_ID1  0x20
#define CHIP_ID2  0x21
#define CHIP_ID_0 0x1602
#define CHIP_ID_1 0x0501
#define VENDOR_ID1 0x23
#define VENDOR_ID1_VAL 0x19
#define VENDOR_ID2 0x24
#define VENDOR_ID2_VAL 0x34
#define LDN 0x7

#define RS485  0xF0
#define RTS_INVERT BIT(5)
#define RS485_URA BIT(4)
#define RXW4C_IRA BIT(3)
#define TXW4C_IRA BIT(2)

#define DRIVER_NAME "8250_fintek"

struct fintek_8250 {
	u16 base_port;
	u8 index;
	long line;
};

static int fintek_8250_enter_key(u16 base_port)
{

	if (!request_muxed_region(base_port, 2, DRIVER_NAME))
		return -EBUSY;

	outb(ENTRY_KEY, base_port + ADDR_PORT);
	outb(ENTRY_KEY, base_port + ADDR_PORT);
	return 0;
}

static void fintek_8250_exit_key(u16 base_port)
{

	outb(EXIT_KEY, base_port + ADDR_PORT);
	release_region(base_port + ADDR_PORT, 2);
}

static int fintek_8250_get_index(resource_size_t base_addr)
{
	resource_size_t base[] = {0x3f8, 0x2f8, 0x3e8, 0x2e8};
	int i;

	for (i = 0; i < ARRAY_SIZE(base); i++)
		if (base_addr == base[i])
			return i;

	return -ENODEV;
}

static int fintek_8250_check_id(u16 base_port)
{
	u16 chip;

	outb(VENDOR_ID1, base_port + ADDR_PORT);
	if (inb(base_port + DATA_PORT) != VENDOR_ID1_VAL)
		return -ENODEV;

	outb(VENDOR_ID2, base_port + ADDR_PORT);
	if (inb(base_port + DATA_PORT) != VENDOR_ID2_VAL)
		return -ENODEV;

	outb(CHIP_ID1, base_port + ADDR_PORT);
	chip = inb(base_port + DATA_PORT);
	outb(CHIP_ID2, base_port + ADDR_PORT);
	chip |= inb(base_port + DATA_PORT) << 8;

	if (chip != CHIP_ID_0 && chip != CHIP_ID_1)
		return -ENODEV;

	return 0;
}

static int fintek_8250_rs485_config(struct uart_port *port,
			      struct serial_rs485 *rs485)
{
	uint8_t config = 0;
	struct fintek_8250 *pdata = port->private_data;

	if (!pdata)
		return -EINVAL;

	if (rs485->flags & SER_RS485_ENABLED)
		memset(rs485->padding, 0, sizeof(rs485->padding));
	else
		memset(rs485, 0, sizeof(*rs485));

	rs485->flags &= SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND |
			SER_RS485_RTS_AFTER_SEND;

	if (rs485->delay_rts_before_send) {
		rs485->delay_rts_before_send = 1;
		config |= TXW4C_IRA;
	}

	if (rs485->delay_rts_after_send) {
		rs485->delay_rts_after_send = 1;
		config |= RXW4C_IRA;
	}

	if ((!!(rs485->flags & SER_RS485_RTS_ON_SEND)) ==
			(!!(rs485->flags & SER_RS485_RTS_AFTER_SEND)))
		rs485->flags &= SER_RS485_ENABLED;
	else
		config |= RS485_URA;

	if (rs485->flags & SER_RS485_RTS_ON_SEND)
		config |= RTS_INVERT;

	if (fintek_8250_enter_key(pdata->base_port))
		return -EBUSY;

	outb(LDN, pdata->base_port + ADDR_PORT);
	outb(pdata->index, pdata->base_port + DATA_PORT);
	outb(RS485, pdata->base_port + ADDR_PORT);
	outb(config, pdata->base_port + DATA_PORT);
	fintek_8250_exit_key(pdata->base_port);

	port->rs485 = *rs485;

	return 0;
}

static int fintek_8250_base_port(void)
{
	static const u16 addr[] = {0x4e, 0x2e};
	int i;

	for (i = 0; i < ARRAY_SIZE(addr); i++) {
		int ret;

		if (fintek_8250_enter_key(addr[i]))
			continue;
		ret = fintek_8250_check_id(addr[i]);
		fintek_8250_exit_key(addr[i]);
		if (!ret)
			return addr[i];
	}

	return -ENODEV;
}

static int
fintek_8250_probe(struct pnp_dev *dev, const struct pnp_device_id *dev_id)
{
	struct uart_8250_port uart;
	struct fintek_8250 *pdata;
	int index;
	int base_port;

	if (!pnp_port_valid(dev, 0))
		return -ENODEV;

	base_port = fintek_8250_base_port();
	if (base_port < 0)
		return -ENODEV;

	index = fintek_8250_get_index(pnp_port_start(dev, 0));
	if (index < 0)
		return -ENODEV;

	memset(&uart, 0, sizeof(uart));

	pdata = devm_kzalloc(&dev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	uart.port.private_data = pdata;

	if (!pnp_irq_valid(dev, 0))
		return -ENODEV;
	uart.port.irq = pnp_irq(dev, 0);
	uart.port.iobase = pnp_port_start(dev, 0);
	uart.port.iotype = UPIO_PORT;
	uart.port.rs485_config = fintek_8250_rs485_config;

	uart.port.flags |= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF;
	if (pnp_irq_flags(dev, 0) & IORESOURCE_IRQ_SHAREABLE)
		uart.port.flags |= UPF_SHARE_IRQ;
	uart.port.uartclk = 1843200;
	uart.port.dev = &dev->dev;

	pdata->base_port = base_port;
	pdata->index = index;
	pdata->line = serial8250_register_8250_port(&uart);
	if (pdata->line < 0)
		return -ENODEV;

	pnp_set_drvdata(dev, pdata);
	return 0;
}

static void fintek_8250_remove(struct pnp_dev *dev)
{
	struct fintek_8250 *pdata = pnp_get_drvdata(dev);

	if (pdata)
		serial8250_unregister_port(pdata->line);
}

#ifdef CONFIG_PM
static int fintek_8250_suspend(struct pnp_dev *dev, pm_message_t state)
{
	struct fintek_8250 *pdata = pnp_get_drvdata(dev);

	if (!pdata)
		return -ENODEV;
	serial8250_suspend_port(pdata->line);
	return 0;
}

static int fintek_8250_resume(struct pnp_dev *dev)
{
	struct fintek_8250 *pdata = pnp_get_drvdata(dev);

	if (!pdata)
		return -ENODEV;
	serial8250_resume_port(pdata->line);
	return 0;
}
#else
#define fintek_8250_suspend NULL
#define fintek_8250_resume NULL
#endif /* CONFIG_PM */

static const struct pnp_device_id fintek_dev_table[] = {
	/* Qtechnology Panel PC / IO1000 */
	{ "PNP0501"},
	{}
};

MODULE_DEVICE_TABLE(pnp, fintek_dev_table);

static struct pnp_driver fintek_8250_driver = {
	.name		= DRIVER_NAME,
	.probe		= fintek_8250_probe,
	.remove		= fintek_8250_remove,
	.suspend	= fintek_8250_suspend,
	.resume		= fintek_8250_resume,
	.id_table	= fintek_dev_table,
};

module_pnp_driver(fintek_8250_driver);
MODULE_DESCRIPTION("Fintek F812164 module");
MODULE_AUTHOR("Ricardo Ribalda <ricardo.ribalda@gmail.com>");
MODULE_LICENSE("GPL");
