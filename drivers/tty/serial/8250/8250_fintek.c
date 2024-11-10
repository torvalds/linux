// SPDX-License-Identifier: GPL-2.0
/*
 *  Probe for F81216A LPC to 4 UART
 *
 *  Copyright (C) 2014-2016 Ricardo Ribalda, Qtechnology A/S
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
#define CHIP_ID_F81865 0x0407
#define CHIP_ID_F81866 0x1010
#define CHIP_ID_F81966 0x0215
#define CHIP_ID_F81216AD 0x1602
#define CHIP_ID_F81216E 0x1617
#define CHIP_ID_F81216H 0x0501
#define CHIP_ID_F81216 0x0802
#define VENDOR_ID1 0x23
#define VENDOR_ID1_VAL 0x19
#define VENDOR_ID2 0x24
#define VENDOR_ID2_VAL 0x34
#define IO_ADDR1 0x61
#define IO_ADDR2 0x60
#define LDN 0x7

#define FINTEK_IRQ_MODE	0x70
#define IRQ_SHARE	BIT(4)
#define IRQ_MODE_MASK	(BIT(6) | BIT(5))
#define IRQ_LEVEL_LOW	0
#define IRQ_EDGE_HIGH	BIT(5)

/*
 * F81216H clock source register, the value and mask is the same with F81866,
 * but it's on F0h.
 *
 * Clock speeds for UART (register F0h)
 * 00: 1.8432MHz.
 * 01: 18.432MHz.
 * 10: 24MHz.
 * 11: 14.769MHz.
 */
#define RS485  0xF0
#define RTS_INVERT BIT(5)
#define RS485_URA BIT(4)
#define RXW4C_IRA BIT(3)
#define TXW4C_IRA BIT(2)

#define FIFO_CTRL		0xF6
#define FIFO_MODE_MASK		(BIT(1) | BIT(0))
#define FIFO_MODE_128		(BIT(1) | BIT(0))
#define RXFTHR_MODE_MASK	(BIT(5) | BIT(4))
#define RXFTHR_MODE_4X		BIT(5)

#define F81216_LDN_LOW	0x0
#define F81216_LDN_HIGH	0x4

/*
 * F81866/966 registers
 *
 * The IRQ setting mode of F81866/966 is not the same with F81216 series.
 *	Level/Low: IRQ_MODE0:0, IRQ_MODE1:0
 *	Edge/High: IRQ_MODE0:1, IRQ_MODE1:0
 *
 * Clock speeds for UART (register F2h)
 * 00: 1.8432MHz.
 * 01: 18.432MHz.
 * 10: 24MHz.
 * 11: 14.769MHz.
 */
#define F81866_IRQ_MODE		0xf0
#define F81866_IRQ_SHARE	BIT(0)
#define F81866_IRQ_MODE0	BIT(1)

#define F81866_FIFO_CTRL	FIFO_CTRL
#define F81866_IRQ_MODE1	BIT(3)

#define F81866_LDN_LOW		0x10
#define F81866_LDN_HIGH		0x16

#define F81866_UART_CLK 0xF2
#define F81866_UART_CLK_MASK (BIT(1) | BIT(0))
#define F81866_UART_CLK_1_8432MHZ 0
#define F81866_UART_CLK_14_769MHZ (BIT(1) | BIT(0))
#define F81866_UART_CLK_18_432MHZ BIT(0)
#define F81866_UART_CLK_24MHZ BIT(1)

struct fintek_8250 {
	u16 pid;
	u16 base_port;
	u8 index;
	u8 key;
};

static u8 sio_read_reg(struct fintek_8250 *pdata, u8 reg)
{
	outb(reg, pdata->base_port + ADDR_PORT);
	return inb(pdata->base_port + DATA_PORT);
}

static void sio_write_reg(struct fintek_8250 *pdata, u8 reg, u8 data)
{
	outb(reg, pdata->base_port + ADDR_PORT);
	outb(data, pdata->base_port + DATA_PORT);
}

static void sio_write_mask_reg(struct fintek_8250 *pdata, u8 reg, u8 mask,
			       u8 data)
{
	u8 tmp;

	tmp = (sio_read_reg(pdata, reg) & ~mask) | (mask & data);
	sio_write_reg(pdata, reg, tmp);
}

static int fintek_8250_enter_key(u16 base_port, u8 key)
{
	if (!request_muxed_region(base_port, 2, "8250_fintek"))
		return -EBUSY;

	/* Force to deactivate all SuperIO in this base_port */
	outb(EXIT_KEY, base_port + ADDR_PORT);

	outb(key, base_port + ADDR_PORT);
	outb(key, base_port + ADDR_PORT);
	return 0;
}

static void fintek_8250_exit_key(u16 base_port)
{

	outb(EXIT_KEY, base_port + ADDR_PORT);
	release_region(base_port + ADDR_PORT, 2);
}

static int fintek_8250_check_id(struct fintek_8250 *pdata)
{
	u16 chip;

	if (sio_read_reg(pdata, VENDOR_ID1) != VENDOR_ID1_VAL)
		return -ENODEV;

	if (sio_read_reg(pdata, VENDOR_ID2) != VENDOR_ID2_VAL)
		return -ENODEV;

	chip = sio_read_reg(pdata, CHIP_ID1);
	chip |= sio_read_reg(pdata, CHIP_ID2) << 8;

	switch (chip) {
	case CHIP_ID_F81865:
	case CHIP_ID_F81866:
	case CHIP_ID_F81966:
	case CHIP_ID_F81216AD:
	case CHIP_ID_F81216E:
	case CHIP_ID_F81216H:
	case CHIP_ID_F81216:
		break;
	default:
		return -ENODEV;
	}

	pdata->pid = chip;
	return 0;
}

static int fintek_8250_get_ldn_range(struct fintek_8250 *pdata, int *min,
				     int *max)
{
	switch (pdata->pid) {
	case CHIP_ID_F81966:
	case CHIP_ID_F81865:
	case CHIP_ID_F81866:
		*min = F81866_LDN_LOW;
		*max = F81866_LDN_HIGH;
		return 0;

	case CHIP_ID_F81216AD:
	case CHIP_ID_F81216E:
	case CHIP_ID_F81216H:
	case CHIP_ID_F81216:
		*min = F81216_LDN_LOW;
		*max = F81216_LDN_HIGH;
		return 0;
	}

	return -ENODEV;
}

static int fintek_8250_rs485_config(struct uart_port *port, struct ktermios *termios,
			      struct serial_rs485 *rs485)
{
	uint8_t config = 0;
	struct fintek_8250 *pdata = port->private_data;

	if (!pdata)
		return -EINVAL;


	if (rs485->flags & SER_RS485_ENABLED) {
		/* Hardware do not support same RTS level on send and receive */
		if (!(rs485->flags & SER_RS485_RTS_ON_SEND) ==
		    !(rs485->flags & SER_RS485_RTS_AFTER_SEND))
			return -EINVAL;
		config |= RS485_URA;
	}

	if (rs485->delay_rts_before_send) {
		rs485->delay_rts_before_send = 1;
		config |= TXW4C_IRA;
	}

	if (rs485->delay_rts_after_send) {
		rs485->delay_rts_after_send = 1;
		config |= RXW4C_IRA;
	}

	if (rs485->flags & SER_RS485_RTS_ON_SEND)
		config |= RTS_INVERT;

	if (fintek_8250_enter_key(pdata->base_port, pdata->key))
		return -EBUSY;

	sio_write_reg(pdata, LDN, pdata->index);
	sio_write_reg(pdata, RS485, config);
	fintek_8250_exit_key(pdata->base_port);

	return 0;
}

static void fintek_8250_set_irq_mode(struct fintek_8250 *pdata, bool is_level)
{
	sio_write_reg(pdata, LDN, pdata->index);

	switch (pdata->pid) {
	case CHIP_ID_F81966:
	case CHIP_ID_F81866:
		sio_write_mask_reg(pdata, F81866_FIFO_CTRL, F81866_IRQ_MODE1,
				   0);
		fallthrough;
	case CHIP_ID_F81865:
		sio_write_mask_reg(pdata, F81866_IRQ_MODE, F81866_IRQ_SHARE,
				   F81866_IRQ_SHARE);
		sio_write_mask_reg(pdata, F81866_IRQ_MODE, F81866_IRQ_MODE0,
				   is_level ? 0 : F81866_IRQ_MODE0);
		break;

	case CHIP_ID_F81216AD:
	case CHIP_ID_F81216E:
	case CHIP_ID_F81216H:
	case CHIP_ID_F81216:
		sio_write_mask_reg(pdata, FINTEK_IRQ_MODE, IRQ_SHARE,
				   IRQ_SHARE);
		sio_write_mask_reg(pdata, FINTEK_IRQ_MODE, IRQ_MODE_MASK,
				   is_level ? IRQ_LEVEL_LOW : IRQ_EDGE_HIGH);
		break;
	}
}

static void fintek_8250_set_max_fifo(struct fintek_8250 *pdata)
{
	switch (pdata->pid) {
	case CHIP_ID_F81216E: /* 128Bytes FIFO */
	case CHIP_ID_F81216H:
	case CHIP_ID_F81966:
	case CHIP_ID_F81866:
		sio_write_mask_reg(pdata, FIFO_CTRL,
				   FIFO_MODE_MASK | RXFTHR_MODE_MASK,
				   FIFO_MODE_128 | RXFTHR_MODE_4X);
		break;

	default: /* Default 16Bytes FIFO */
		break;
	}
}

static void fintek_8250_set_termios(struct uart_port *port,
				    struct ktermios *termios,
				    const struct ktermios *old)
{
	struct fintek_8250 *pdata = port->private_data;
	unsigned int baud = tty_termios_baud_rate(termios);
	int i;
	u8 reg;
	static u32 baudrate_table[] = {115200, 921600, 1152000, 1500000};
	static u8 clock_table[] = { F81866_UART_CLK_1_8432MHZ,
			F81866_UART_CLK_14_769MHZ, F81866_UART_CLK_18_432MHZ,
			F81866_UART_CLK_24MHZ };

	/*
	 * We'll use serial8250_do_set_termios() for baud = 0, otherwise It'll
	 * crash on baudrate_table[i] % baud with "division by zero".
	 */
	if (!baud)
		goto exit;

	switch (pdata->pid) {
	case CHIP_ID_F81216E:
	case CHIP_ID_F81216H:
		reg = RS485;
		break;
	case CHIP_ID_F81966:
	case CHIP_ID_F81866:
		reg = F81866_UART_CLK;
		break;
	default:
		/* Don't change clocksource with unknown PID */
		dev_warn(port->dev,
			"%s: pid: %x Not support. use default set_termios.\n",
			__func__, pdata->pid);
		goto exit;
	}

	for (i = 0; i < ARRAY_SIZE(baudrate_table); ++i) {
		if (baud > baudrate_table[i] || baudrate_table[i] % baud != 0)
			continue;

		if (port->uartclk == baudrate_table[i] * 16)
			break;

		if (fintek_8250_enter_key(pdata->base_port, pdata->key))
			continue;

		port->uartclk = baudrate_table[i] * 16;

		sio_write_reg(pdata, LDN, pdata->index);
		sio_write_mask_reg(pdata, reg, F81866_UART_CLK_MASK,
				clock_table[i]);

		fintek_8250_exit_key(pdata->base_port);
		break;
	}

	if (i == ARRAY_SIZE(baudrate_table)) {
		baud = tty_termios_baud_rate(old);
		tty_termios_encode_baud_rate(termios, baud, baud);
	}

exit:
	serial8250_do_set_termios(port, termios, old);
}

static void fintek_8250_set_termios_handler(struct uart_8250_port *uart)
{
	struct fintek_8250 *pdata = uart->port.private_data;

	switch (pdata->pid) {
	case CHIP_ID_F81216E:
	case CHIP_ID_F81216H:
	case CHIP_ID_F81966:
	case CHIP_ID_F81866:
		uart->port.set_termios = fintek_8250_set_termios;
		break;

	default:
		break;
	}
}

static int probe_setup_port(struct fintek_8250 *pdata,
					struct uart_8250_port *uart)
{
	static const u16 addr[] = {0x4e, 0x2e};
	static const u8 keys[] = {0x77, 0xa0, 0x87, 0x67};
	struct irq_data *irq_data;
	bool level_mode = false;
	int i, j, k, min, max;

	for (i = 0; i < ARRAY_SIZE(addr); i++) {
		for (j = 0; j < ARRAY_SIZE(keys); j++) {
			pdata->base_port = addr[i];
			pdata->key = keys[j];

			if (fintek_8250_enter_key(addr[i], keys[j]))
				continue;
			if (fintek_8250_check_id(pdata) ||
			    fintek_8250_get_ldn_range(pdata, &min, &max)) {
				fintek_8250_exit_key(addr[i]);
				continue;
			}

			for (k = min; k < max; k++) {
				u16 aux;

				sio_write_reg(pdata, LDN, k);
				aux = sio_read_reg(pdata, IO_ADDR1);
				aux |= sio_read_reg(pdata, IO_ADDR2) << 8;
				if (aux != uart->port.iobase)
					continue;

				pdata->index = k;

				irq_data = irq_get_irq_data(uart->port.irq);
				if (irq_data)
					level_mode =
						irqd_is_level_type(irq_data);

				fintek_8250_set_irq_mode(pdata, level_mode);
				fintek_8250_set_max_fifo(pdata);

				fintek_8250_exit_key(addr[i]);

				return 0;
			}

			fintek_8250_exit_key(addr[i]);
		}
	}

	return -ENODEV;
}

/* Only the first port supports delays */
static const struct serial_rs485 fintek_8250_rs485_supported_port0 = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND,
	.delay_rts_before_send = 1,
	.delay_rts_after_send = 1,
};

static const struct serial_rs485 fintek_8250_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND,
};

static void fintek_8250_set_rs485_handler(struct uart_8250_port *uart)
{
	struct fintek_8250 *pdata = uart->port.private_data;

	switch (pdata->pid) {
	case CHIP_ID_F81216AD:
	case CHIP_ID_F81216H:
	case CHIP_ID_F81966:
	case CHIP_ID_F81866:
	case CHIP_ID_F81865:
		uart->port.rs485_config = fintek_8250_rs485_config;
		if (!pdata->index)
			uart->port.rs485_supported = fintek_8250_rs485_supported_port0;
		else
			uart->port.rs485_supported = fintek_8250_rs485_supported;
		break;

	case CHIP_ID_F81216E: /* F81216E does not support RS485 delays */
		uart->port.rs485_config = fintek_8250_rs485_config;
		uart->port.rs485_supported = fintek_8250_rs485_supported;
		break;

	default: /* No RS485 Auto direction functional */
		break;
	}
}

int fintek_8250_probe(struct uart_8250_port *uart)
{
	struct fintek_8250 *pdata;
	struct fintek_8250 probe_data;

	if (probe_setup_port(&probe_data, uart))
		return -ENODEV;

	pdata = devm_kzalloc(uart->port.dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	memcpy(pdata, &probe_data, sizeof(probe_data));
	uart->port.private_data = pdata;
	fintek_8250_set_rs485_handler(uart);
	fintek_8250_set_termios_handler(uart);

	return 0;
}
