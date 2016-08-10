/*
 *  Probe for F81216A LPC to 4 UART
 *
 *  Copyright (C) 2014-2016 Ricardo Ribalda, Qtechnology A/S
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
#include <linux/irq.h>
#include  "8250.h"

#define ADDR_PORT 0
#define DATA_PORT 1
#define EXIT_KEY 0xAA
#define CHIP_ID1  0x20
#define CHIP_ID2  0x21
#define CHIP_ID_0 0x1602
#define CHIP_ID_1 0x0501
#define VENDOR_ID1 0x23
#define VENDOR_ID1_VAL 0x19
#define VENDOR_ID2 0x24
#define VENDOR_ID2_VAL 0x34
#define IO_ADDR1 0x61
#define IO_ADDR2 0x60
#define LDN 0x7

#define IRQ_MODE	0x70
#define IRQ_SHARE	BIT(4)
#define IRQ_MODE_MASK	(BIT(6) | BIT(5))
#define IRQ_LEVEL_LOW	0
#define IRQ_EDGE_HIGH	BIT(5)

#define RS485  0xF0
#define RTS_INVERT BIT(5)
#define RS485_URA BIT(4)
#define RXW4C_IRA BIT(3)
#define TXW4C_IRA BIT(2)

struct fintek_8250 {
	u16 base_port;
	u8 index;
	u8 key;
};

static int fintek_8250_enter_key(u16 base_port, u8 key)
{
	if (!request_muxed_region(base_port, 2, "8250_fintek"))
		return -EBUSY;

	outb(key, base_port + ADDR_PORT);
	outb(key, base_port + ADDR_PORT);
	return 0;
}

static void fintek_8250_exit_key(u16 base_port)
{

	outb(EXIT_KEY, base_port + ADDR_PORT);
	release_region(base_port + ADDR_PORT, 2);
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

	if (fintek_8250_enter_key(pdata->base_port, pdata->key))
		return -EBUSY;

	outb(LDN, pdata->base_port + ADDR_PORT);
	outb(pdata->index, pdata->base_port + DATA_PORT);
	outb(RS485, pdata->base_port + ADDR_PORT);
	outb(config, pdata->base_port + DATA_PORT);
	fintek_8250_exit_key(pdata->base_port);

	port->rs485 = *rs485;

	return 0;
}

static int find_base_port(struct fintek_8250 *pdata, u16 io_address)
{
	static const u16 addr[] = {0x4e, 0x2e};
	static const u8 keys[] = {0x77, 0xa0, 0x87, 0x67};
	int i, j, k;

	for (i = 0; i < ARRAY_SIZE(addr); i++) {
		for (j = 0; j < ARRAY_SIZE(keys); j++) {

			if (fintek_8250_enter_key(addr[i], keys[j]))
				continue;
			if (fintek_8250_check_id(addr[i])) {
				fintek_8250_exit_key(addr[i]);
				continue;
			}

			for (k = 0; k < 4; k++) {
				u16 aux;

				outb(LDN, addr[i] + ADDR_PORT);
				outb(k, addr[i] + DATA_PORT);

				outb(IO_ADDR1, addr[i] + ADDR_PORT);
				aux = inb(addr[i] + DATA_PORT);
				outb(IO_ADDR2, addr[i] + ADDR_PORT);
				aux |= inb(addr[i] + DATA_PORT) << 8;
				if (aux != io_address)
					continue;

				fintek_8250_exit_key(addr[i]);
				pdata->key = keys[j];
				pdata->base_port = addr[i];
				pdata->index = k;

				return 0;
			}

			fintek_8250_exit_key(addr[i]);
		}
	}

	return -ENODEV;
}

static int fintek_8250_set_irq_mode(struct fintek_8250 *pdata, bool level_mode)
{
	int status;
	u8 tmp;

	status = fintek_8250_enter_key(pdata->base_port, pdata->key);
	if (status)
		return status;

	outb(LDN, pdata->base_port + ADDR_PORT);
	outb(pdata->index, pdata->base_port + DATA_PORT);

	outb(IRQ_MODE, pdata->base_port + ADDR_PORT);
	tmp = inb(pdata->base_port + DATA_PORT);

	tmp &= ~IRQ_MODE_MASK;
	tmp |= IRQ_SHARE;
	if (!level_mode)
		tmp |= IRQ_EDGE_HIGH;

	outb(tmp, pdata->base_port + DATA_PORT);
	fintek_8250_exit_key(pdata->base_port);
	return 0;
}

int fintek_8250_probe(struct uart_8250_port *uart)
{
	struct fintek_8250 *pdata;
	struct fintek_8250 probe_data;
	struct irq_data *irq_data = irq_get_irq_data(uart->port.irq);
	bool level_mode = irqd_is_level_type(irq_data);

	if (find_base_port(&probe_data, uart->port.iobase))
		return -ENODEV;

	pdata = devm_kzalloc(uart->port.dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	memcpy(pdata, &probe_data, sizeof(probe_data));
	uart->port.rs485_config = fintek_8250_rs485_config;
	uart->port.private_data = pdata;

	return fintek_8250_set_irq_mode(pdata, level_mode);
}
