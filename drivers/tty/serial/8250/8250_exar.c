// SPDX-License-Identifier: GPL-2.0
/*
 *  Probe module for 8250/16550-type Exar chips PCI serial ports.
 *
 *  Based on drivers/tty/serial/8250/8250_pci.c,
 *
 *  Copyright (C) 2017 Sudip Mukherjee, All Rights Reserved.
 */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/property.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/8250_pci.h>
#include <linux/delay.h>

#include <asm/byteorder.h>

#include "8250.h"

#define PCI_DEVICE_ID_ACCESSIO_COM_2S		0x1052
#define PCI_DEVICE_ID_ACCESSIO_COM_4S		0x105d
#define PCI_DEVICE_ID_ACCESSIO_COM_8S		0x106c
#define PCI_DEVICE_ID_ACCESSIO_COM232_8		0x10a8
#define PCI_DEVICE_ID_ACCESSIO_COM_2SM		0x10d2
#define PCI_DEVICE_ID_ACCESSIO_COM_4SM		0x10db
#define PCI_DEVICE_ID_ACCESSIO_COM_8SM		0x10ea

#define PCI_DEVICE_ID_COMMTECH_4224PCI335	0x0002
#define PCI_DEVICE_ID_COMMTECH_4222PCI335	0x0004
#define PCI_DEVICE_ID_COMMTECH_2324PCI335	0x000a
#define PCI_DEVICE_ID_COMMTECH_2328PCI335	0x000b
#define PCI_DEVICE_ID_COMMTECH_4224PCIE		0x0020
#define PCI_DEVICE_ID_COMMTECH_4228PCIE		0x0021
#define PCI_DEVICE_ID_COMMTECH_4222PCIE		0x0022

#define PCI_DEVICE_ID_EXAR_XR17V4358		0x4358
#define PCI_DEVICE_ID_EXAR_XR17V8358		0x8358

#define PCI_SUBDEVICE_ID_USR_2980		0x0128
#define PCI_SUBDEVICE_ID_USR_2981		0x0129

#define PCI_DEVICE_ID_SEALEVEL_710xC		0x1001
#define PCI_DEVICE_ID_SEALEVEL_720xC		0x1002
#define PCI_DEVICE_ID_SEALEVEL_740xC		0x1004
#define PCI_DEVICE_ID_SEALEVEL_780xC		0x1008
#define PCI_DEVICE_ID_SEALEVEL_716xC		0x1010

#define UART_EXAR_INT0		0x80
#define UART_EXAR_8XMODE	0x88	/* 8X sampling rate select */
#define UART_EXAR_SLEEP		0x8b	/* Sleep mode */
#define UART_EXAR_DVID		0x8d	/* Device identification */

#define UART_EXAR_FCTR		0x08	/* Feature Control Register */
#define UART_FCTR_EXAR_IRDA	0x10	/* IrDa data encode select */
#define UART_FCTR_EXAR_485	0x20	/* Auto 485 half duplex dir ctl */
#define UART_FCTR_EXAR_TRGA	0x00	/* FIFO trigger table A */
#define UART_FCTR_EXAR_TRGB	0x60	/* FIFO trigger table B */
#define UART_FCTR_EXAR_TRGC	0x80	/* FIFO trigger table C */
#define UART_FCTR_EXAR_TRGD	0xc0	/* FIFO trigger table D programmable */

#define UART_EXAR_TXTRG		0x0a	/* Tx FIFO trigger level write-only */
#define UART_EXAR_RXTRG		0x0b	/* Rx FIFO trigger level write-only */

#define UART_EXAR_MPIOINT_7_0	0x8f	/* MPIOINT[7:0] */
#define UART_EXAR_MPIOLVL_7_0	0x90	/* MPIOLVL[7:0] */
#define UART_EXAR_MPIO3T_7_0	0x91	/* MPIO3T[7:0] */
#define UART_EXAR_MPIOINV_7_0	0x92	/* MPIOINV[7:0] */
#define UART_EXAR_MPIOSEL_7_0	0x93	/* MPIOSEL[7:0] */
#define UART_EXAR_MPIOOD_7_0	0x94	/* MPIOOD[7:0] */
#define UART_EXAR_MPIOINT_15_8	0x95	/* MPIOINT[15:8] */
#define UART_EXAR_MPIOLVL_15_8	0x96	/* MPIOLVL[15:8] */
#define UART_EXAR_MPIO3T_15_8	0x97	/* MPIO3T[15:8] */
#define UART_EXAR_MPIOINV_15_8	0x98	/* MPIOINV[15:8] */
#define UART_EXAR_MPIOSEL_15_8	0x99	/* MPIOSEL[15:8] */
#define UART_EXAR_MPIOOD_15_8	0x9a	/* MPIOOD[15:8] */

#define UART_EXAR_RS485_DLY(x)	((x) << 4)

/*
 * IOT2040 MPIO wiring semantics:
 *
 * MPIO		Port	Function
 * ----		----	--------
 * 0		2 	Mode bit 0
 * 1		2	Mode bit 1
 * 2		2	Terminate bus
 * 3		-	<reserved>
 * 4		3	Mode bit 0
 * 5		3	Mode bit 1
 * 6		3	Terminate bus
 * 7		-	<reserved>
 * 8		2	Enable
 * 9		3	Enable
 * 10		-	Red LED
 * 11..15	-	<unused>
 */

/* IOT2040 MPIOs 0..7 */
#define IOT2040_UART_MODE_RS232		0x01
#define IOT2040_UART_MODE_RS485		0x02
#define IOT2040_UART_MODE_RS422		0x03
#define IOT2040_UART_TERMINATE_BUS	0x04

#define IOT2040_UART1_MASK		0x0f
#define IOT2040_UART2_SHIFT		4

#define IOT2040_UARTS_DEFAULT_MODE	0x11	/* both RS232 */
#define IOT2040_UARTS_GPIO_LO_MODE	0x88	/* reserved pins as input */

/* IOT2040 MPIOs 8..15 */
#define IOT2040_UARTS_ENABLE		0x03
#define IOT2040_UARTS_GPIO_HI_MODE	0xF8	/* enable & LED as outputs */

struct exar8250;

struct exar8250_platform {
	int (*rs485_config)(struct uart_port *port, struct ktermios *termios,
			    struct serial_rs485 *rs485);
	const struct serial_rs485 *rs485_supported;
	int (*register_gpio)(struct pci_dev *, struct uart_8250_port *);
	void (*unregister_gpio)(struct uart_8250_port *);
};

/**
 * struct exar8250_board - board information
 * @num_ports: number of serial ports
 * @reg_shift: describes UART register mapping in PCI memory
 * @setup: quirk run at ->probe() stage
 * @exit: quirk run at ->remove() stage
 */
struct exar8250_board {
	unsigned int num_ports;
	unsigned int reg_shift;
	int	(*setup)(struct exar8250 *, struct pci_dev *,
			 struct uart_8250_port *, int);
	void	(*exit)(struct pci_dev *pcidev);
};

struct exar8250 {
	unsigned int		nr;
	struct exar8250_board	*board;
	void __iomem		*virt;
	int			line[];
};

static void exar_pm(struct uart_port *port, unsigned int state, unsigned int old)
{
	/*
	 * Exar UARTs have a SLEEP register that enables or disables each UART
	 * to enter sleep mode separately. On the XR17V35x the register
	 * is accessible to each UART at the UART_EXAR_SLEEP offset, but
	 * the UART channel may only write to the corresponding bit.
	 */
	serial_port_out(port, UART_EXAR_SLEEP, state ? 0xff : 0);
}

/*
 * XR17V35x UARTs have an extra fractional divisor register (DLD)
 * Calculate divisor with extra 4-bit fractional portion
 */
static unsigned int xr17v35x_get_divisor(struct uart_port *p, unsigned int baud,
					 unsigned int *frac)
{
	unsigned int quot_16;

	quot_16 = DIV_ROUND_CLOSEST(p->uartclk, baud);
	*frac = quot_16 & 0x0f;

	return quot_16 >> 4;
}

static void xr17v35x_set_divisor(struct uart_port *p, unsigned int baud,
				 unsigned int quot, unsigned int quot_frac)
{
	serial8250_do_set_divisor(p, baud, quot, quot_frac);

	/* Preserve bits not related to baudrate; DLD[7:4]. */
	quot_frac |= serial_port_in(p, 0x2) & 0xf0;
	serial_port_out(p, 0x2, quot_frac);
}

static int xr17v35x_startup(struct uart_port *port)
{
	/*
	 * First enable access to IER [7:5], ISR [5:4], FCR [5:4],
	 * MCR [7:5] and MSR [7:0]
	 */
	serial_port_out(port, UART_XR_EFR, UART_EFR_ECB);

	/*
	 * Make sure all interrups are masked until initialization is
	 * complete and the FIFOs are cleared
	 *
	 * Synchronize UART_IER access against the console.
	 */
	spin_lock_irq(&port->lock);
	serial_port_out(port, UART_IER, 0);
	spin_unlock_irq(&port->lock);

	return serial8250_do_startup(port);
}

static void exar_shutdown(struct uart_port *port)
{
	bool tx_complete = false;
	struct uart_8250_port *up = up_to_u8250p(port);
	struct circ_buf *xmit = &port->state->xmit;
	int i = 0;
	u16 lsr;

	do {
		lsr = serial_in(up, UART_LSR);
		if (lsr & (UART_LSR_TEMT | UART_LSR_THRE))
			tx_complete = true;
		else
			tx_complete = false;
		usleep_range(1000, 1100);
	} while (!uart_circ_empty(xmit) && !tx_complete && i++ < 1000);

	serial8250_do_shutdown(port);
}

static int default_setup(struct exar8250 *priv, struct pci_dev *pcidev,
			 int idx, unsigned int offset,
			 struct uart_8250_port *port)
{
	const struct exar8250_board *board = priv->board;
	unsigned int bar = 0;
	unsigned char status;

	port->port.iotype = UPIO_MEM;
	port->port.mapbase = pci_resource_start(pcidev, bar) + offset;
	port->port.membase = priv->virt + offset;
	port->port.regshift = board->reg_shift;

	/*
	 * XR17V35x UARTs have an extra divisor register, DLD that gets enabled
	 * with when DLAB is set which will cause the device to incorrectly match
	 * and assign port type to PORT_16650. The EFR for this UART is found
	 * at offset 0x09. Instead check the Deice ID (DVID) register
	 * for a 2, 4 or 8 port UART.
	 */
	status = readb(port->port.membase + UART_EXAR_DVID);
	if (status == 0x82 || status == 0x84 || status == 0x88) {
		port->port.type = PORT_XR17V35X;

		port->port.get_divisor = xr17v35x_get_divisor;
		port->port.set_divisor = xr17v35x_set_divisor;

		port->port.startup = xr17v35x_startup;
	} else {
		port->port.type = PORT_XR17D15X;
	}

	port->port.pm = exar_pm;
	port->port.shutdown = exar_shutdown;

	return 0;
}

static int
pci_fastcom335_setup(struct exar8250 *priv, struct pci_dev *pcidev,
		     struct uart_8250_port *port, int idx)
{
	unsigned int offset = idx * 0x200;
	unsigned int baud = 1843200;
	u8 __iomem *p;
	int err;

	port->port.uartclk = baud * 16;

	err = default_setup(priv, pcidev, idx, offset, port);
	if (err)
		return err;

	p = port->port.membase;

	writeb(0x00, p + UART_EXAR_8XMODE);
	writeb(UART_FCTR_EXAR_TRGD, p + UART_EXAR_FCTR);
	writeb(32, p + UART_EXAR_TXTRG);
	writeb(32, p + UART_EXAR_RXTRG);

	/*
	 * Setup Multipurpose Input/Output pins.
	 */
	if (idx == 0) {
		switch (pcidev->device) {
		case PCI_DEVICE_ID_COMMTECH_4222PCI335:
		case PCI_DEVICE_ID_COMMTECH_4224PCI335:
			writeb(0x78, p + UART_EXAR_MPIOLVL_7_0);
			writeb(0x00, p + UART_EXAR_MPIOINV_7_0);
			writeb(0x00, p + UART_EXAR_MPIOSEL_7_0);
			break;
		case PCI_DEVICE_ID_COMMTECH_2324PCI335:
		case PCI_DEVICE_ID_COMMTECH_2328PCI335:
			writeb(0x00, p + UART_EXAR_MPIOLVL_7_0);
			writeb(0xc0, p + UART_EXAR_MPIOINV_7_0);
			writeb(0xc0, p + UART_EXAR_MPIOSEL_7_0);
			break;
		}
		writeb(0x00, p + UART_EXAR_MPIOINT_7_0);
		writeb(0x00, p + UART_EXAR_MPIO3T_7_0);
		writeb(0x00, p + UART_EXAR_MPIOOD_7_0);
	}

	return 0;
}

static int
pci_connect_tech_setup(struct exar8250 *priv, struct pci_dev *pcidev,
		       struct uart_8250_port *port, int idx)
{
	unsigned int offset = idx * 0x200;
	unsigned int baud = 1843200;

	port->port.uartclk = baud * 16;
	return default_setup(priv, pcidev, idx, offset, port);
}

static int
pci_xr17c154_setup(struct exar8250 *priv, struct pci_dev *pcidev,
		   struct uart_8250_port *port, int idx)
{
	unsigned int offset = idx * 0x200;
	unsigned int baud = 921600;

	port->port.uartclk = baud * 16;
	return default_setup(priv, pcidev, idx, offset, port);
}

static void setup_gpio(struct pci_dev *pcidev, u8 __iomem *p)
{
	/*
	 * The Commtech adapters required the MPIOs to be driven low. The Exar
	 * devices will export them as GPIOs, so we pre-configure them safely
	 * as inputs.
	 */

	u8 dir = 0x00;

	if  ((pcidev->vendor == PCI_VENDOR_ID_EXAR) &&
		(pcidev->subsystem_vendor != PCI_VENDOR_ID_SEALEVEL)) {
		// Configure GPIO as inputs for Commtech adapters
		dir = 0xff;
	} else {
		// Configure GPIO as outputs for SeaLevel adapters
		dir = 0x00;
	}

	writeb(0x00, p + UART_EXAR_MPIOINT_7_0);
	writeb(0x00, p + UART_EXAR_MPIOLVL_7_0);
	writeb(0x00, p + UART_EXAR_MPIO3T_7_0);
	writeb(0x00, p + UART_EXAR_MPIOINV_7_0);
	writeb(dir,  p + UART_EXAR_MPIOSEL_7_0);
	writeb(0x00, p + UART_EXAR_MPIOOD_7_0);
	writeb(0x00, p + UART_EXAR_MPIOINT_15_8);
	writeb(0x00, p + UART_EXAR_MPIOLVL_15_8);
	writeb(0x00, p + UART_EXAR_MPIO3T_15_8);
	writeb(0x00, p + UART_EXAR_MPIOINV_15_8);
	writeb(dir,  p + UART_EXAR_MPIOSEL_15_8);
	writeb(0x00, p + UART_EXAR_MPIOOD_15_8);
}

static struct platform_device *__xr17v35x_register_gpio(struct pci_dev *pcidev,
							const struct software_node *node)
{
	struct platform_device *pdev;

	pdev = platform_device_alloc("gpio_exar", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return NULL;

	pdev->dev.parent = &pcidev->dev;
	ACPI_COMPANION_SET(&pdev->dev, ACPI_COMPANION(&pcidev->dev));

	if (device_add_software_node(&pdev->dev, node) < 0 ||
	    platform_device_add(pdev) < 0) {
		platform_device_put(pdev);
		return NULL;
	}

	return pdev;
}

static void __xr17v35x_unregister_gpio(struct platform_device *pdev)
{
	device_remove_software_node(&pdev->dev);
	platform_device_unregister(pdev);
}

static const struct property_entry exar_gpio_properties[] = {
	PROPERTY_ENTRY_U32("exar,first-pin", 0),
	PROPERTY_ENTRY_U32("ngpios", 16),
	{ }
};

static const struct software_node exar_gpio_node = {
	.properties = exar_gpio_properties,
};

static int xr17v35x_register_gpio(struct pci_dev *pcidev, struct uart_8250_port *port)
{
	if (pcidev->vendor == PCI_VENDOR_ID_EXAR)
		port->port.private_data =
			__xr17v35x_register_gpio(pcidev, &exar_gpio_node);

	return 0;
}

static void xr17v35x_unregister_gpio(struct uart_8250_port *port)
{
	if (!port->port.private_data)
		return;

	__xr17v35x_unregister_gpio(port->port.private_data);
	port->port.private_data = NULL;
}

static int generic_rs485_config(struct uart_port *port, struct ktermios *termios,
				struct serial_rs485 *rs485)
{
	bool is_rs485 = !!(rs485->flags & SER_RS485_ENABLED);
	u8 __iomem *p = port->membase;
	u8 value;

	value = readb(p + UART_EXAR_FCTR);
	if (is_rs485)
		value |= UART_FCTR_EXAR_485;
	else
		value &= ~UART_FCTR_EXAR_485;

	writeb(value, p + UART_EXAR_FCTR);

	if (is_rs485)
		writeb(UART_EXAR_RS485_DLY(4), p + UART_MSR);

	return 0;
}

static const struct serial_rs485 generic_rs485_supported = {
	.flags = SER_RS485_ENABLED,
};

static const struct exar8250_platform exar8250_default_platform = {
	.register_gpio = xr17v35x_register_gpio,
	.unregister_gpio = xr17v35x_unregister_gpio,
	.rs485_config = generic_rs485_config,
	.rs485_supported = &generic_rs485_supported,
};

static int iot2040_rs485_config(struct uart_port *port, struct ktermios *termios,
				struct serial_rs485 *rs485)
{
	bool is_rs485 = !!(rs485->flags & SER_RS485_ENABLED);
	u8 __iomem *p = port->membase;
	u8 mask = IOT2040_UART1_MASK;
	u8 mode, value;

	if (is_rs485) {
		if (rs485->flags & SER_RS485_RX_DURING_TX)
			mode = IOT2040_UART_MODE_RS422;
		else
			mode = IOT2040_UART_MODE_RS485;

		if (rs485->flags & SER_RS485_TERMINATE_BUS)
			mode |= IOT2040_UART_TERMINATE_BUS;
	} else {
		mode = IOT2040_UART_MODE_RS232;
	}

	if (port->line == 3) {
		mask <<= IOT2040_UART2_SHIFT;
		mode <<= IOT2040_UART2_SHIFT;
	}

	value = readb(p + UART_EXAR_MPIOLVL_7_0);
	value &= ~mask;
	value |= mode;
	writeb(value, p + UART_EXAR_MPIOLVL_7_0);

	return generic_rs485_config(port, termios, rs485);
}

static const struct serial_rs485 iot2040_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RX_DURING_TX | SER_RS485_TERMINATE_BUS,
};

static const struct property_entry iot2040_gpio_properties[] = {
	PROPERTY_ENTRY_U32("exar,first-pin", 10),
	PROPERTY_ENTRY_U32("ngpios", 1),
	{ }
};

static const struct software_node iot2040_gpio_node = {
	.properties = iot2040_gpio_properties,
};

static int iot2040_register_gpio(struct pci_dev *pcidev,
			      struct uart_8250_port *port)
{
	u8 __iomem *p = port->port.membase;

	writeb(IOT2040_UARTS_DEFAULT_MODE, p + UART_EXAR_MPIOLVL_7_0);
	writeb(IOT2040_UARTS_GPIO_LO_MODE, p + UART_EXAR_MPIOSEL_7_0);
	writeb(IOT2040_UARTS_ENABLE, p + UART_EXAR_MPIOLVL_15_8);
	writeb(IOT2040_UARTS_GPIO_HI_MODE, p + UART_EXAR_MPIOSEL_15_8);

	port->port.private_data =
		__xr17v35x_register_gpio(pcidev, &iot2040_gpio_node);

	return 0;
}

static const struct exar8250_platform iot2040_platform = {
	.rs485_config = iot2040_rs485_config,
	.rs485_supported = &iot2040_rs485_supported,
	.register_gpio = iot2040_register_gpio,
	.unregister_gpio = xr17v35x_unregister_gpio,
};

/*
 * For SIMATIC IOT2000, only IOT2040 and its variants have the Exar device,
 * IOT2020 doesn't have. Therefore it is sufficient to match on the common
 * board name after the device was found.
 */
static const struct dmi_system_id exar_platforms[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "SIMATIC IOT2000"),
		},
		.driver_data = (void *)&iot2040_platform,
	},
	{}
};

static const struct exar8250_platform *exar_get_platform(void)
{
	const struct dmi_system_id *dmi_match;

	dmi_match = dmi_first_match(exar_platforms);
	if (dmi_match)
		return dmi_match->driver_data;

	return &exar8250_default_platform;
}

static int
pci_xr17v35x_setup(struct exar8250 *priv, struct pci_dev *pcidev,
		   struct uart_8250_port *port, int idx)
{
	const struct exar8250_platform *platform = exar_get_platform();
	unsigned int offset = idx * 0x400;
	unsigned int baud = 7812500;
	u8 __iomem *p;
	int ret;

	port->port.uartclk = baud * 16;
	port->port.rs485_config = platform->rs485_config;
	port->port.rs485_supported = *(platform->rs485_supported);

	/*
	 * Setup the UART clock for the devices on expansion slot to
	 * half the clock speed of the main chip (which is 125MHz)
	 */
	if (idx >= 8)
		port->port.uartclk /= 2;

	ret = default_setup(priv, pcidev, idx, offset, port);
	if (ret)
		return ret;

	p = port->port.membase;

	writeb(0x00, p + UART_EXAR_8XMODE);
	writeb(UART_FCTR_EXAR_TRGD, p + UART_EXAR_FCTR);
	writeb(128, p + UART_EXAR_TXTRG);
	writeb(128, p + UART_EXAR_RXTRG);

	if (idx == 0) {
		/* Setup Multipurpose Input/Output pins. */
		setup_gpio(pcidev, p);

		ret = platform->register_gpio(pcidev, port);
	}

	return ret;
}

static void pci_xr17v35x_exit(struct pci_dev *pcidev)
{
	const struct exar8250_platform *platform = exar_get_platform();
	struct exar8250 *priv = pci_get_drvdata(pcidev);
	struct uart_8250_port *port = serial8250_get_port(priv->line[0]);

	platform->unregister_gpio(port);
}

static inline void exar_misc_clear(struct exar8250 *priv)
{
	/* Clear all PCI interrupts by reading INT0. No effect on IIR */
	readb(priv->virt + UART_EXAR_INT0);

	/* Clear INT0 for Expansion Interface slave ports, too */
	if (priv->board->num_ports > 8)
		readb(priv->virt + 0x2000 + UART_EXAR_INT0);
}

/*
 * These Exar UARTs have an extra interrupt indicator that could fire for a
 * few interrupts that are not presented/cleared through IIR.  One of which is
 * a wakeup interrupt when coming out of sleep.  These interrupts are only
 * cleared by reading global INT0 or INT1 registers as interrupts are
 * associated with channel 0. The INT[3:0] registers _are_ accessible from each
 * channel's address space, but for the sake of bus efficiency we register a
 * dedicated handler at the PCI device level to handle them.
 */
static irqreturn_t exar_misc_handler(int irq, void *data)
{
	exar_misc_clear(data);

	return IRQ_HANDLED;
}

static int
exar_pci_probe(struct pci_dev *pcidev, const struct pci_device_id *ent)
{
	unsigned int nr_ports, i, bar = 0, maxnr;
	struct exar8250_board *board;
	struct uart_8250_port uart;
	struct exar8250 *priv;
	int rc;

	board = (struct exar8250_board *)ent->driver_data;
	if (!board)
		return -EINVAL;

	rc = pcim_enable_device(pcidev);
	if (rc)
		return rc;

	maxnr = pci_resource_len(pcidev, bar) >> (board->reg_shift + 3);

	if (pcidev->vendor == PCI_VENDOR_ID_ACCESSIO)
		nr_ports = BIT(((pcidev->device & 0x38) >> 3) - 1);
	else if (board->num_ports)
		nr_ports = board->num_ports;
	else if (pcidev->vendor == PCI_VENDOR_ID_SEALEVEL)
		nr_ports = pcidev->device & 0xff;
	else
		nr_ports = pcidev->device & 0x0f;

	priv = devm_kzalloc(&pcidev->dev, struct_size(priv, line, nr_ports), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->board = board;
	priv->virt = pcim_iomap(pcidev, bar, 0);
	if (!priv->virt)
		return -ENOMEM;

	pci_set_master(pcidev);

	rc = pci_alloc_irq_vectors(pcidev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (rc < 0)
		return rc;

	memset(&uart, 0, sizeof(uart));
	uart.port.flags = UPF_SHARE_IRQ | UPF_EXAR_EFR | UPF_FIXED_TYPE | UPF_FIXED_PORT;
	uart.port.irq = pci_irq_vector(pcidev, 0);
	uart.port.dev = &pcidev->dev;

	rc = devm_request_irq(&pcidev->dev, uart.port.irq, exar_misc_handler,
			 IRQF_SHARED, "exar_uart", priv);
	if (rc)
		return rc;

	/* Clear interrupts */
	exar_misc_clear(priv);

	for (i = 0; i < nr_ports && i < maxnr; i++) {
		rc = board->setup(priv, pcidev, &uart, i);
		if (rc) {
			dev_err(&pcidev->dev, "Failed to setup port %u\n", i);
			break;
		}

		dev_dbg(&pcidev->dev, "Setup PCI port: port %lx, irq %d, type %d\n",
			uart.port.iobase, uart.port.irq, uart.port.iotype);

		priv->line[i] = serial8250_register_8250_port(&uart);
		if (priv->line[i] < 0) {
			dev_err(&pcidev->dev,
				"Couldn't register serial port %lx, irq %d, type %d, error %d\n",
				uart.port.iobase, uart.port.irq,
				uart.port.iotype, priv->line[i]);
			break;
		}
	}
	priv->nr = i;
	pci_set_drvdata(pcidev, priv);
	return 0;
}

static void exar_pci_remove(struct pci_dev *pcidev)
{
	struct exar8250 *priv = pci_get_drvdata(pcidev);
	unsigned int i;

	for (i = 0; i < priv->nr; i++)
		serial8250_unregister_port(priv->line[i]);

	if (priv->board->exit)
		priv->board->exit(pcidev);
}

static int __maybe_unused exar_suspend(struct device *dev)
{
	struct pci_dev *pcidev = to_pci_dev(dev);
	struct exar8250 *priv = pci_get_drvdata(pcidev);
	unsigned int i;

	for (i = 0; i < priv->nr; i++)
		if (priv->line[i] >= 0)
			serial8250_suspend_port(priv->line[i]);

	/* Ensure that every init quirk is properly torn down */
	if (priv->board->exit)
		priv->board->exit(pcidev);

	return 0;
}

static int __maybe_unused exar_resume(struct device *dev)
{
	struct exar8250 *priv = dev_get_drvdata(dev);
	unsigned int i;

	exar_misc_clear(priv);

	for (i = 0; i < priv->nr; i++)
		if (priv->line[i] >= 0)
			serial8250_resume_port(priv->line[i]);

	return 0;
}

static SIMPLE_DEV_PM_OPS(exar_pci_pm, exar_suspend, exar_resume);

static const struct exar8250_board pbn_fastcom335_2 = {
	.num_ports	= 2,
	.setup		= pci_fastcom335_setup,
};

static const struct exar8250_board pbn_fastcom335_4 = {
	.num_ports	= 4,
	.setup		= pci_fastcom335_setup,
};

static const struct exar8250_board pbn_fastcom335_8 = {
	.num_ports	= 8,
	.setup		= pci_fastcom335_setup,
};

static const struct exar8250_board pbn_connect = {
	.setup		= pci_connect_tech_setup,
};

static const struct exar8250_board pbn_exar_ibm_saturn = {
	.num_ports	= 1,
	.setup		= pci_xr17c154_setup,
};

static const struct exar8250_board pbn_exar_XR17C15x = {
	.setup		= pci_xr17c154_setup,
};

static const struct exar8250_board pbn_exar_XR17V35x = {
	.setup		= pci_xr17v35x_setup,
	.exit		= pci_xr17v35x_exit,
};

static const struct exar8250_board pbn_fastcom35x_2 = {
	.num_ports	= 2,
	.setup		= pci_xr17v35x_setup,
	.exit		= pci_xr17v35x_exit,
};

static const struct exar8250_board pbn_fastcom35x_4 = {
	.num_ports	= 4,
	.setup		= pci_xr17v35x_setup,
	.exit		= pci_xr17v35x_exit,
};

static const struct exar8250_board pbn_fastcom35x_8 = {
	.num_ports	= 8,
	.setup		= pci_xr17v35x_setup,
	.exit		= pci_xr17v35x_exit,
};

static const struct exar8250_board pbn_exar_XR17V4358 = {
	.num_ports	= 12,
	.setup		= pci_xr17v35x_setup,
	.exit		= pci_xr17v35x_exit,
};

static const struct exar8250_board pbn_exar_XR17V8358 = {
	.num_ports	= 16,
	.setup		= pci_xr17v35x_setup,
	.exit		= pci_xr17v35x_exit,
};

#define CONNECT_DEVICE(devid, sdevid, bd) {				\
	PCI_DEVICE_SUB(							\
		PCI_VENDOR_ID_EXAR,					\
		PCI_DEVICE_ID_EXAR_##devid,				\
		PCI_SUBVENDOR_ID_CONNECT_TECH,				\
		PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_##sdevid), 0, 0,	\
		(kernel_ulong_t)&bd					\
	}

#define EXAR_DEVICE(vend, devid, bd) { PCI_DEVICE_DATA(vend, devid, &bd) }

#define IBM_DEVICE(devid, sdevid, bd) {			\
	PCI_DEVICE_SUB(					\
		PCI_VENDOR_ID_EXAR,			\
		PCI_DEVICE_ID_EXAR_##devid,		\
		PCI_VENDOR_ID_IBM,			\
		PCI_SUBDEVICE_ID_IBM_##sdevid), 0, 0,	\
		(kernel_ulong_t)&bd			\
	}

#define USR_DEVICE(devid, sdevid, bd) {			\
	PCI_DEVICE_SUB(					\
		PCI_VENDOR_ID_USR,			\
		PCI_DEVICE_ID_EXAR_##devid,		\
		PCI_VENDOR_ID_EXAR,			\
		PCI_SUBDEVICE_ID_USR_##sdevid), 0, 0,	\
		(kernel_ulong_t)&bd			\
	}

static const struct pci_device_id exar_pci_tbl[] = {
	EXAR_DEVICE(ACCESSIO, COM_2S, pbn_exar_XR17C15x),
	EXAR_DEVICE(ACCESSIO, COM_4S, pbn_exar_XR17C15x),
	EXAR_DEVICE(ACCESSIO, COM_8S, pbn_exar_XR17C15x),
	EXAR_DEVICE(ACCESSIO, COM232_8, pbn_exar_XR17C15x),
	EXAR_DEVICE(ACCESSIO, COM_2SM, pbn_exar_XR17C15x),
	EXAR_DEVICE(ACCESSIO, COM_4SM, pbn_exar_XR17C15x),
	EXAR_DEVICE(ACCESSIO, COM_8SM, pbn_exar_XR17C15x),

	CONNECT_DEVICE(XR17C152, UART_2_232, pbn_connect),
	CONNECT_DEVICE(XR17C154, UART_4_232, pbn_connect),
	CONNECT_DEVICE(XR17C158, UART_8_232, pbn_connect),
	CONNECT_DEVICE(XR17C152, UART_1_1, pbn_connect),
	CONNECT_DEVICE(XR17C154, UART_2_2, pbn_connect),
	CONNECT_DEVICE(XR17C158, UART_4_4, pbn_connect),
	CONNECT_DEVICE(XR17C152, UART_2, pbn_connect),
	CONNECT_DEVICE(XR17C154, UART_4, pbn_connect),
	CONNECT_DEVICE(XR17C158, UART_8, pbn_connect),
	CONNECT_DEVICE(XR17C152, UART_2_485, pbn_connect),
	CONNECT_DEVICE(XR17C154, UART_4_485, pbn_connect),
	CONNECT_DEVICE(XR17C158, UART_8_485, pbn_connect),

	IBM_DEVICE(XR17C152, SATURN_SERIAL_ONE_PORT, pbn_exar_ibm_saturn),

	/* USRobotics USR298x-OEM PCI Modems */
	USR_DEVICE(XR17C152, 2980, pbn_exar_XR17C15x),
	USR_DEVICE(XR17C152, 2981, pbn_exar_XR17C15x),

	/* Exar Corp. XR17C15[248] Dual/Quad/Octal UART */
	EXAR_DEVICE(EXAR, XR17C152, pbn_exar_XR17C15x),
	EXAR_DEVICE(EXAR, XR17C154, pbn_exar_XR17C15x),
	EXAR_DEVICE(EXAR, XR17C158, pbn_exar_XR17C15x),

	/* Exar Corp. XR17V[48]35[248] Dual/Quad/Octal/Hexa PCIe UARTs */
	EXAR_DEVICE(EXAR, XR17V352, pbn_exar_XR17V35x),
	EXAR_DEVICE(EXAR, XR17V354, pbn_exar_XR17V35x),
	EXAR_DEVICE(EXAR, XR17V358, pbn_exar_XR17V35x),
	EXAR_DEVICE(EXAR, XR17V4358, pbn_exar_XR17V4358),
	EXAR_DEVICE(EXAR, XR17V8358, pbn_exar_XR17V8358),
	EXAR_DEVICE(COMMTECH, 4222PCIE, pbn_fastcom35x_2),
	EXAR_DEVICE(COMMTECH, 4224PCIE, pbn_fastcom35x_4),
	EXAR_DEVICE(COMMTECH, 4228PCIE, pbn_fastcom35x_8),

	EXAR_DEVICE(COMMTECH, 4222PCI335, pbn_fastcom335_2),
	EXAR_DEVICE(COMMTECH, 4224PCI335, pbn_fastcom335_4),
	EXAR_DEVICE(COMMTECH, 2324PCI335, pbn_fastcom335_4),
	EXAR_DEVICE(COMMTECH, 2328PCI335, pbn_fastcom335_8),

	EXAR_DEVICE(SEALEVEL, 710xC, pbn_exar_XR17V35x),
	EXAR_DEVICE(SEALEVEL, 720xC, pbn_exar_XR17V35x),
	EXAR_DEVICE(SEALEVEL, 740xC, pbn_exar_XR17V35x),
	EXAR_DEVICE(SEALEVEL, 780xC, pbn_exar_XR17V35x),
	EXAR_DEVICE(SEALEVEL, 716xC, pbn_exar_XR17V35x),
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, exar_pci_tbl);

static struct pci_driver exar_pci_driver = {
	.name		= "exar_serial",
	.probe		= exar_pci_probe,
	.remove		= exar_pci_remove,
	.driver         = {
		.pm     = &exar_pci_pm,
	},
	.id_table	= exar_pci_tbl,
};
module_pci_driver(exar_pci_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Exar Serial Driver");
MODULE_AUTHOR("Sudip Mukherjee <sudip.mukherjee@codethink.co.uk>");
