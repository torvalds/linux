// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 KEBA Industrial Automation GmbH
 *
 * Driver for KEBA UART FPGA IP core
 */

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/misc/keba.h>
#include <linux/module.h>

#include "8250.h"

#define KUART "kuart"

/* flags */
#define KUART_RS485		BIT(0)
#define KUART_USE_CAPABILITY	BIT(1)

/* registers */
#define KUART_VERSION		0x0000
#define KUART_REVISION		0x0001
#define KUART_CAPABILITY	0x0002
#define KUART_CONTROL		0x0004
#define KUART_BASE		0x000C
#define KUART_REGSHIFT		2
#define KUART_CLK		1843200

/* mode flags */
enum kuart_mode {
	KUART_MODE_NONE = 0,
	KUART_MODE_RS485,
	KUART_MODE_RS422,
	KUART_MODE_RS232
};

/* capability flags */
#define KUART_CAPABILITY_NONE	BIT(KUART_MODE_NONE)
#define KUART_CAPABILITY_RS485	BIT(KUART_MODE_RS485)
#define KUART_CAPABILITY_RS422	BIT(KUART_MODE_RS422)
#define KUART_CAPABILITY_RS232	BIT(KUART_MODE_RS232)
#define KUART_CAPABILITY_MASK	GENMASK(3, 0)

/* Additional Control Register DTR line configuration */
#define UART_ACR_DTRLC_MASK		0x18
#define UART_ACR_DTRLC_COMPAT		0x00
#define UART_ACR_DTRLC_ENABLE_LOW	0x10

struct kuart {
	struct keba_uart_auxdev *auxdev;
	void __iomem *base;
	unsigned int line;

	unsigned int flags;
	u8 capability;
	enum kuart_mode mode;
};

static void kuart_set_phy_mode(struct kuart *kuart, enum kuart_mode mode)
{
	iowrite8(mode, kuart->base + KUART_CONTROL);
}

static void kuart_enhanced_mode(struct uart_8250_port *up, bool enable)
{
	u8 lcr, efr;

	/* backup LCR register */
	lcr = serial_in(up, UART_LCR);

	/* enable 650 compatible register set (EFR, ...) */
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);

	/* enable/disable enhanced mode with indexed control registers */
	efr = serial_in(up, UART_EFR);
	if (enable)
		efr |= UART_EFR_ECB;
	else
		efr &= ~UART_EFR_ECB;
	serial_out(up, UART_EFR, efr);

	/* disable 650 compatible register set, restore LCR */
	serial_out(up, UART_LCR, lcr);
}

static void kuart_dtr_line_config(struct uart_8250_port *up, u8 dtrlc)
{
	u8 acr;

	/* set index register to 0 to access ACR register */
	serial_out(up, UART_SCR, UART_ACR);

	/* set value register to 0x10 writing DTR mode (1,0) */
	acr = serial_in(up, UART_LSR);
	acr &= ~UART_ACR_DTRLC_MASK;
	acr |= dtrlc;
	serial_out(up, UART_LSR, acr);
}

static int kuart_rs485_config(struct uart_port *port, struct ktermios *termios,
			      struct serial_rs485 *rs485)
{
	struct uart_8250_port *up = up_to_u8250p(port);
	struct kuart *kuart = port->private_data;
	enum kuart_mode mode;
	u8 dtrlc;

	if (rs485->flags & SER_RS485_ENABLED) {
		if (rs485->flags & SER_RS485_MODE_RS422)
			mode = KUART_MODE_RS422;
		else
			mode = KUART_MODE_RS485;
	} else {
		mode = KUART_MODE_RS232;
	}

	if (mode == kuart->mode)
		return 0;

	if (kuart->flags & KUART_USE_CAPABILITY) {
		/* deactivate physical interface, break before make */
		kuart_set_phy_mode(kuart, KUART_MODE_NONE);
	}

	if (mode == KUART_MODE_RS485) {
		/*
		 * Set DTR line configuration of 95x UART to DTR mode (1,0).
		 * In this mode the DTR pin drives the active-low enable pin of
		 * an external RS485 buffer. The DTR pin will be forced low
		 * whenever the transmitter is not empty, otherwise DTR pin is
		 * high.
		 */
		dtrlc = UART_ACR_DTRLC_ENABLE_LOW;
	} else {
		/*
		 * Set DTR line configuration of 95x UART to DTR mode (0,0).
		 * In this mode the DTR pin is compatible with 16C450, 16C550,
		 * 16C650 and 16c670 (i.e. normal).
		 */
		dtrlc = UART_ACR_DTRLC_COMPAT;
	}

	kuart_enhanced_mode(up, true);
	kuart_dtr_line_config(up, dtrlc);
	kuart_enhanced_mode(up, false);

	if (kuart->flags & KUART_USE_CAPABILITY) {
		/* activate selected physical interface */
		kuart_set_phy_mode(kuart, mode);
	}

	kuart->mode = mode;

	return 0;
}

static int kuart_probe(struct auxiliary_device *auxdev,
		       const struct auxiliary_device_id *id)
{
	struct device *dev = &auxdev->dev;
	struct uart_8250_port uart = {};
	struct resource res;
	struct kuart *kuart;
	int retval;

	kuart = devm_kzalloc(dev, sizeof(*kuart), GFP_KERNEL);
	if (!kuart)
		return -ENOMEM;
	kuart->auxdev = container_of(auxdev, struct keba_uart_auxdev, auxdev);
	kuart->flags = id->driver_data;
	auxiliary_set_drvdata(auxdev, kuart);

	/*
	 * map only memory in front of UART registers, UART registers will be
	 * mapped by serial port
	 */
	res = kuart->auxdev->io;
	res.end = res.start + KUART_BASE - 1;
	kuart->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(kuart->base))
		return PTR_ERR(kuart->base);

	if (kuart->flags & KUART_USE_CAPABILITY) {
		/*
		 * supported modes are read from capability register, at least
		 * one mode other than none must be supported
		 */
		kuart->capability = ioread8(kuart->base + KUART_CAPABILITY) &
				    KUART_CAPABILITY_MASK;
		if ((kuart->capability & ~KUART_CAPABILITY_NONE) == 0)
			return -EIO;
	}

	spin_lock_init(&uart.port.lock);
	uart.port.dev = dev;
	uart.port.mapbase = kuart->auxdev->io.start + KUART_BASE;
	uart.port.irq = kuart->auxdev->irq;
	uart.port.uartclk = KUART_CLK;
	uart.port.private_data = kuart;

	/* 8 bit registers are 32 bit aligned => shift register offset */
	uart.port.iotype = UPIO_MEM32;
	uart.port.regshift = KUART_REGSHIFT;

	/*
	 * UART mixes 16550, 16750 and 16C950 (for RS485) standard => auto
	 * configuration works best
	 */
	uart.port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF | UPF_IOREMAP;

	/*
	 * UART supports RS485, RS422 and RS232 with switching of physical
	 * interface
	 */
	uart.port.rs485_config = kuart_rs485_config;
	if (kuart->flags & KUART_RS485) {
		uart.port.rs485_supported.flags = SER_RS485_ENABLED |
						  SER_RS485_RTS_ON_SEND;
		uart.port.rs485.flags = SER_RS485_ENABLED |
					SER_RS485_RTS_ON_SEND;
	}
	if (kuart->flags & KUART_USE_CAPABILITY) {
		/* default mode priority is RS485 > RS422 > RS232 */
		if (kuart->capability & KUART_CAPABILITY_RS422) {
			uart.port.rs485_supported.flags |= SER_RS485_ENABLED |
							   SER_RS485_RTS_ON_SEND |
							   SER_RS485_MODE_RS422;
			uart.port.rs485.flags = SER_RS485_ENABLED |
						SER_RS485_RTS_ON_SEND |
						SER_RS485_MODE_RS422;
		}
		if (kuart->capability & KUART_CAPABILITY_RS485) {
			uart.port.rs485_supported.flags |= SER_RS485_ENABLED |
							   SER_RS485_RTS_ON_SEND;
			uart.port.rs485.flags = SER_RS485_ENABLED |
						SER_RS485_RTS_ON_SEND;
		}
	}

	retval = serial8250_register_8250_port(&uart);
	if (retval < 0) {
		dev_err(&auxdev->dev, "UART registration failed!\n");
		return retval;
	}
	kuart->line = retval;

	return 0;
}

static void kuart_remove(struct auxiliary_device *auxdev)
{
	struct kuart *kuart = auxiliary_get_drvdata(auxdev);

	if (kuart->flags & KUART_USE_CAPABILITY)
		kuart_set_phy_mode(kuart, KUART_MODE_NONE);

	serial8250_unregister_port(kuart->line);
}

static const struct auxiliary_device_id kuart_devtype_aux[] = {
	{ .name = "keba.rs485-uart", .driver_data = KUART_RS485 },
	{ .name = "keba.rs232-uart", .driver_data = 0 },
	{ .name = "keba.uart", .driver_data = KUART_USE_CAPABILITY },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, kuart_devtype_aux);

static struct auxiliary_driver kuart_driver_aux = {
	.name = KUART,
	.id_table = kuart_devtype_aux,
	.probe  = kuart_probe,
	.remove = kuart_remove,
};
module_auxiliary_driver(kuart_driver_aux);

MODULE_AUTHOR("Gerhard Engleder <eg@keba.com>");
MODULE_DESCRIPTION("KEBA 8250 serial port driver");
MODULE_LICENSE("GPL");
