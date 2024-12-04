// SPDX-License-Identifier: GPL-2.0
/*
 *  Probe module for 8250/16550-type Exar chips PCI serial ports.
 *
 *  Based on drivers/tty/serial/8250/8250_pci.c,
 *
 *  Copyright (C) 2017 Sudip Mukherjee, All Rights Reserved.
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/eeprom_93cx6.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>

#include <asm/byteorder.h>

#include "8250.h"
#include "8250_pcilib.h"

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

#define PCI_VENDOR_ID_CONNECT_TECH				0x12c4
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_SP_OPTO        0x0340
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_SP_OPTO_A      0x0341
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_SP_OPTO_B      0x0342
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XPRS           0x0350
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_A         0x0351
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_B         0x0352
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS           0x0353
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_16_XPRS_A        0x0354
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_16_XPRS_B        0x0355
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XPRS_OPTO      0x0360
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_OPTO_A    0x0361
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_OPTO_B    0x0362
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP             0x0370
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP_232         0x0371
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP_485         0x0372
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4_SP           0x0373
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_6_2_SP           0x0374
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_6_SP           0x0375
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP_232_NS      0x0376
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XP_OPTO_LEFT   0x0380
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XP_OPTO_RIGHT  0x0381
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XP_OPTO        0x0382
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4_XPRS_OPTO    0x0392
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP        0x03A0
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP_232    0x03A1
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP_485    0x03A2
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP_232_NS 0x03A3
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCIE_XEG001               0x0602
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCIE_XR35X_BASE           0x1000
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCIE_XR35X_2              0x1002
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCIE_XR35X_4              0x1004
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCIE_XR35X_8              0x1008
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCIE_XR35X_12             0x100C
#define PCI_SUBDEVICE_ID_CONNECT_TECH_PCIE_XR35X_16             0x1010
#define PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_12_XIG00X          0x110c
#define PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_12_XIG01X          0x110d
#define PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_16                 0x1110

#define PCI_DEVICE_ID_EXAR_XR17V4358		0x4358
#define PCI_DEVICE_ID_EXAR_XR17V8358		0x8358
#define PCI_DEVICE_ID_EXAR_XR17V252		0x0252
#define PCI_DEVICE_ID_EXAR_XR17V254		0x0254
#define PCI_DEVICE_ID_EXAR_XR17V258		0x0258

#define PCI_SUBDEVICE_ID_USR_2980		0x0128
#define PCI_SUBDEVICE_ID_USR_2981		0x0129

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

#define UART_EXAR_DLD			0x02 /* Divisor Fractional */
#define UART_EXAR_DLD_485_POLARITY	0x80 /* RS-485 Enable Signal Polarity */

/* EEPROM registers */
#define UART_EXAR_REGB			0x8e
#define UART_EXAR_REGB_EECK		BIT(4)
#define UART_EXAR_REGB_EECS		BIT(5)
#define UART_EXAR_REGB_EEDI		BIT(6)
#define UART_EXAR_REGB_EEDO		BIT(7)

#define UART_EXAR_XR17C15X_PORT_OFFSET	0x200
#define UART_EXAR_XR17V25X_PORT_OFFSET	0x200
#define UART_EXAR_XR17V35X_PORT_OFFSET	0x400

/*
 * IOT2040 MPIO wiring semantics:
 *
 * MPIO		Port	Function
 * ----		----	--------
 * 0		2	Mode bit 0
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

/* CTI EEPROM offsets */
#define CTI_EE_OFF_XR17C15X_OSC_FREQ	0x04  /* 2 words */
#define CTI_EE_OFF_XR17C15X_PART_NUM	0x0A  /* 4 words */
#define CTI_EE_OFF_XR17C15X_SERIAL_NUM	0x0E  /* 1 word */

#define CTI_EE_OFF_XR17V25X_OSC_FREQ	0x08  /* 2 words */
#define CTI_EE_OFF_XR17V25X_PART_NUM	0x0E  /* 4 words */
#define CTI_EE_OFF_XR17V25X_SERIAL_NUM	0x12  /* 1 word */

#define CTI_EE_OFF_XR17V35X_SERIAL_NUM	0x11  /* 2 word */
#define CTI_EE_OFF_XR17V35X_BRD_FLAGS	0x13  /* 1 word */
#define CTI_EE_OFF_XR17V35X_PORT_FLAGS	0x14  /* 1 word */

#define CTI_EE_MASK_PORT_FLAGS_TYPE	GENMASK(7, 0)
#define CTI_EE_MASK_OSC_FREQ		GENMASK(31, 0)

#define CTI_FPGA_RS485_IO_REG		0x2008
#define CTI_FPGA_CFG_INT_EN_REG		0x48
#define CTI_FPGA_CFG_INT_EN_EXT_BIT	BIT(15) /* External int enable bit */

#define CTI_DEFAULT_PCI_OSC_FREQ	29491200
#define CTI_DEFAULT_PCIE_OSC_FREQ	125000000
#define CTI_DEFAULT_FPGA_OSC_FREQ	33333333

/*
 * CTI Serial port line types. These match the values stored in the first
 * nibble of the CTI EEPROM port_flags word.
 */
enum cti_port_type {
	CTI_PORT_TYPE_NONE = 0,
	CTI_PORT_TYPE_RS232,            // RS232 ONLY
	CTI_PORT_TYPE_RS422_485,        // RS422/RS485 ONLY
	CTI_PORT_TYPE_RS232_422_485_HW, // RS232/422/485 HW ONLY Switchable
	CTI_PORT_TYPE_RS232_422_485_SW, // RS232/422/485 SW ONLY Switchable
	CTI_PORT_TYPE_RS232_422_485_4B, // RS232/422/485 HW/SW (4bit ex. BCG004)
	CTI_PORT_TYPE_RS232_422_485_2B, // RS232/422/485 HW/SW (2bit ex. BBG008)
	CTI_PORT_TYPE_MAX,
};

#define CTI_PORT_TYPE_VALID(_port_type) \
	(((_port_type) > CTI_PORT_TYPE_NONE) && \
	((_port_type) < CTI_PORT_TYPE_MAX))

#define CTI_PORT_TYPE_RS485(_port_type) \
	(((_port_type) > CTI_PORT_TYPE_RS232) && \
	((_port_type) < CTI_PORT_TYPE_MAX))

struct exar8250;

struct exar8250_platform {
	int (*rs485_config)(struct uart_port *port, struct ktermios *termios,
			    struct serial_rs485 *rs485);
	const struct serial_rs485 *rs485_supported;
	int (*register_gpio)(struct pci_dev *pcidev, struct uart_8250_port *port);
	void (*unregister_gpio)(struct uart_8250_port *port);
};

/**
 * struct exar8250_board - board information
 * @num_ports: number of serial ports
 * @reg_shift: describes UART register mapping in PCI memory
 * @setup: quirk run at ->probe() stage for each port
 * @exit: quirk run at ->remove() stage
 */
struct exar8250_board {
	unsigned int num_ports;
	unsigned int reg_shift;
	int	(*setup)(struct exar8250 *priv, struct pci_dev *pcidev,
			 struct uart_8250_port *port, int idx);
	void	(*exit)(struct pci_dev *pcidev);
};

struct exar8250 {
	unsigned int		nr;
	unsigned int		osc_freq;
	struct exar8250_board	*board;
	struct eeprom_93cx6	eeprom;
	void __iomem		*virt;
	int			line[];
};

static inline void exar_write_reg(struct exar8250 *priv,
				unsigned int reg, u8 value)
{
	writeb(value, priv->virt + reg);
}

static inline u8 exar_read_reg(struct exar8250 *priv, unsigned int reg)
{
	return readb(priv->virt + reg);
}

static void exar_eeprom_93cx6_reg_read(struct eeprom_93cx6 *eeprom)
{
	struct exar8250 *priv = eeprom->data;
	u8 regb = exar_read_reg(priv, UART_EXAR_REGB);

	/* EECK and EECS always read 0 from REGB so only set EEDO */
	eeprom->reg_data_out = regb & UART_EXAR_REGB_EEDO;
}

static void exar_eeprom_93cx6_reg_write(struct eeprom_93cx6 *eeprom)
{
	struct exar8250 *priv = eeprom->data;
	u8 regb = 0;

	if (eeprom->reg_data_in)
		regb |= UART_EXAR_REGB_EEDI;
	if (eeprom->reg_data_clock)
		regb |= UART_EXAR_REGB_EECK;
	if (eeprom->reg_chip_select)
		regb |= UART_EXAR_REGB_EECS;

	exar_write_reg(priv, UART_EXAR_REGB, regb);
}

static void exar_eeprom_init(struct exar8250 *priv)
{
	priv->eeprom.data = priv;
	priv->eeprom.register_read = exar_eeprom_93cx6_reg_read;
	priv->eeprom.register_write = exar_eeprom_93cx6_reg_write;
	priv->eeprom.width = PCI_EEPROM_WIDTH_93C46;
	priv->eeprom.quirks |= PCI_EEPROM_QUIRK_EXTRA_READ_CYCLE;
}

/**
 * exar_mpio_config_output() - Configure an Exar MPIO as an output
 * @priv: Device's private structure
 * @mpio_num: MPIO number/offset to configure
 *
 * Configure a single MPIO as an output and disable tristate. It is recommended
 * to set the level with exar_mpio_set_high()/exar_mpio_set_low() prior to
 * calling this function to ensure default MPIO pin state.
 *
 * Return: 0 on success, negative error code on failure
 */
static int exar_mpio_config_output(struct exar8250 *priv,
				unsigned int mpio_num)
{
	unsigned int mpio_offset;
	u8 sel_reg; // MPIO Select register (input/output)
	u8 tri_reg; // MPIO Tristate register
	u8 value;

	if (mpio_num < 8) {
		sel_reg = UART_EXAR_MPIOSEL_7_0;
		tri_reg = UART_EXAR_MPIO3T_7_0;
		mpio_offset = mpio_num;
	} else if (mpio_num >= 8 && mpio_num < 16) {
		sel_reg = UART_EXAR_MPIOSEL_15_8;
		tri_reg = UART_EXAR_MPIO3T_15_8;
		mpio_offset = mpio_num - 8;
	} else {
		return -EINVAL;
	}

	// Disable MPIO pin tri-state
	value = exar_read_reg(priv, tri_reg);
	value &= ~BIT(mpio_offset);
	exar_write_reg(priv, tri_reg, value);

	value = exar_read_reg(priv, sel_reg);
	value &= ~BIT(mpio_offset);
	exar_write_reg(priv, sel_reg, value);

	return 0;
}

/**
 * _exar_mpio_set() - Set an Exar MPIO output high or low
 * @priv: Device's private structure
 * @mpio_num: MPIO number/offset to set
 * @high: Set MPIO high if true, low if false
 *
 * Set a single MPIO high or low. exar_mpio_config_output() must also be called
 * to configure the pin as an output.
 *
 * Return: 0 on success, negative error code on failure
 */
static int _exar_mpio_set(struct exar8250 *priv,
			unsigned int mpio_num, bool high)
{
	unsigned int mpio_offset;
	u8 lvl_reg;
	u8 value;

	if (mpio_num < 8) {
		lvl_reg = UART_EXAR_MPIOLVL_7_0;
		mpio_offset = mpio_num;
	} else if (mpio_num >= 8 && mpio_num < 16) {
		lvl_reg = UART_EXAR_MPIOLVL_15_8;
		mpio_offset = mpio_num - 8;
	} else {
		return -EINVAL;
	}

	value = exar_read_reg(priv, lvl_reg);
	if (high)
		value |= BIT(mpio_offset);
	else
		value &= ~BIT(mpio_offset);
	exar_write_reg(priv, lvl_reg, value);

	return 0;
}

static int exar_mpio_set_low(struct exar8250 *priv, unsigned int mpio_num)
{
	return _exar_mpio_set(priv, mpio_num, false);
}

static int exar_mpio_set_high(struct exar8250 *priv, unsigned int mpio_num)
{
	return _exar_mpio_set(priv, mpio_num, true);
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
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND,
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
	serial8250_do_set_divisor(p, baud, quot);

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
	 * Make sure all interrupts are masked until initialization is
	 * complete and the FIFOs are cleared
	 *
	 * Synchronize UART_IER access against the console.
	 */
	uart_port_lock_irq(port);
	serial_port_out(port, UART_IER, 0);
	uart_port_unlock_irq(port);

	return serial8250_do_startup(port);
}

static void exar_shutdown(struct uart_port *port)
{
	bool tx_complete = false;
	struct uart_8250_port *up = up_to_u8250p(port);
	struct tty_port *tport = &port->state->port;
	int i = 0;
	u16 lsr;

	do {
		lsr = serial_in(up, UART_LSR);
		if (lsr & (UART_LSR_TEMT | UART_LSR_THRE))
			tx_complete = true;
		else
			tx_complete = false;
		usleep_range(1000, 1100);
	} while (!kfifo_is_empty(&tport->xmit_fifo) &&
			!tx_complete && i++ < 1000);

	serial8250_do_shutdown(port);
}

static int default_setup(struct exar8250 *priv, struct pci_dev *pcidev,
			 int idx, unsigned int offset,
			 struct uart_8250_port *port)
{
	const struct exar8250_board *board = priv->board;
	unsigned char status;
	int err;

	err = serial8250_pci_setup_port(pcidev, port, 0, offset, board->reg_shift);
	if (err)
		return err;

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

	/* Skip the initial (per device) setup */
	if (idx)
		return 0;

	/*
	 * Setup Multipurpose Input/Output pins.
	 */
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
	default:
		break;
	}
	writeb(0x00, p + UART_EXAR_MPIOINT_7_0);
	writeb(0x00, p + UART_EXAR_MPIO3T_7_0);
	writeb(0x00, p + UART_EXAR_MPIOOD_7_0);

	return 0;
}

/**
 * cti_tristate_disable() - Disable RS485 transciever tristate
 * @priv: Device's private structure
 * @port_num: Port number to set tristate off
 *
 * Most RS485 capable cards have a power on tristate jumper/switch that ensures
 * the RS422/RS485 transceiver does not drive a multi-drop RS485 bus when it is
 * not the master. When this jumper is installed the user must set the RS485
 * mode to Full or Half duplex to disable tristate prior to using the port.
 *
 * Some Exar UARTs have an auto-tristate feature while others require setting
 * an MPIO to disable the tristate.
 *
 * Return: 0 on success, negative error code on failure
 */
static int cti_tristate_disable(struct exar8250 *priv, unsigned int port_num)
{
	int ret;

	ret = exar_mpio_set_high(priv, port_num);
	if (ret)
		return ret;

	return exar_mpio_config_output(priv, port_num);
}

/**
 * cti_plx_int_enable() - Enable UART interrupts to PLX bridge
 * @priv: Device's private structure
 *
 * Some older CTI cards require MPIO_0 to be set low to enable the
 * interrupts from the UART to the PLX PCI->PCIe bridge.
 *
 * Return: 0 on success, negative error code on failure
 */
static int cti_plx_int_enable(struct exar8250 *priv)
{
	int ret;

	ret = exar_mpio_set_low(priv, 0);
	if (ret)
		return ret;

	return exar_mpio_config_output(priv, 0);
}

/**
 * cti_read_osc_freq() - Read the UART oscillator frequency from EEPROM
 * @priv: Device's private structure
 * @eeprom_offset: Offset where the oscillator frequency is stored
 *
 * CTI XR17x15X and XR17V25X cards have the serial boards oscillator frequency
 * stored in the EEPROM. FPGA and XR17V35X based cards use the PCI/PCIe clock.
 *
 * Return: frequency on success, negative error code on failure
 */
static int cti_read_osc_freq(struct exar8250 *priv, u8 eeprom_offset)
{
	__le16 ee_words[2];
	u32 osc_freq;

	eeprom_93cx6_multiread(&priv->eeprom, eeprom_offset, ee_words, ARRAY_SIZE(ee_words));

	osc_freq = le16_to_cpu(ee_words[0]) | (le16_to_cpu(ee_words[1]) << 16);
	if (osc_freq == CTI_EE_MASK_OSC_FREQ)
		return -EIO;

	return osc_freq;
}

/**
 * cti_get_port_type_xr17c15x_xr17v25x() - Get port type of xr17c15x/xr17v25x
 * @priv: Device's private structure
 * @pcidev: Pointer to the PCI device for this port
 * @port_num: Port to get type of
 *
 * CTI xr17c15x and xr17v25x based cards port types are based on PCI IDs.
 *
 * Return: port type on success, CTI_PORT_TYPE_NONE on failure
 */
static enum cti_port_type cti_get_port_type_xr17c15x_xr17v25x(struct exar8250 *priv,
							struct pci_dev *pcidev,
							unsigned int port_num)
{
	switch (pcidev->subsystem_device) {
	// RS232 only cards
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_232:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_232:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_232:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP_232:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP_232_NS:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP_232:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP_232_NS:
		return CTI_PORT_TYPE_RS232;
	// 1x RS232, 1x RS422/RS485
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_1_1:
		return (port_num == 0) ? CTI_PORT_TYPE_RS232 : CTI_PORT_TYPE_RS422_485;
	// 2x RS232, 2x RS422/RS485
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_2:
		return (port_num < 2) ? CTI_PORT_TYPE_RS232 : CTI_PORT_TYPE_RS422_485;
	// 4x RS232, 4x RS422/RS485
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4_SP:
		return (port_num < 4) ? CTI_PORT_TYPE_RS232 : CTI_PORT_TYPE_RS422_485;
	// RS232/RS422/RS485 HW (jumper) selectable
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_SP_OPTO:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_SP_OPTO_A:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_SP_OPTO_B:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XPRS:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_A:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_B:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_16_XPRS_A:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_16_XPRS_B:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XPRS_OPTO:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_OPTO_A:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_OPTO_B:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XP_OPTO_LEFT:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XP_OPTO_RIGHT:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XP_OPTO:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4_XPRS_OPTO:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP:
		return CTI_PORT_TYPE_RS232_422_485_HW;
	// RS422/RS485 HW (jumper) selectable
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_485:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_485:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_485:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP_485:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP_485:
		return CTI_PORT_TYPE_RS422_485;
	// 6x RS232, 2x RS422/RS485
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_6_2_SP:
		return (port_num < 6) ? CTI_PORT_TYPE_RS232 : CTI_PORT_TYPE_RS422_485;
	// 2x RS232, 6x RS422/RS485
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_6_SP:
		return (port_num < 2) ? CTI_PORT_TYPE_RS232 : CTI_PORT_TYPE_RS422_485;
	default:
		dev_err(&pcidev->dev, "unknown/unsupported device\n");
		return CTI_PORT_TYPE_NONE;
	}
}

/**
 * cti_get_port_type_fpga() - Get the port type of a CTI FPGA card
 * @priv: Device's private structure
 * @pcidev: Pointer to the PCI device for this port
 * @port_num: Port to get type of
 *
 * FPGA based cards port types are based on PCI IDs.
 *
 * Return: port type on success, CTI_PORT_TYPE_NONE on failure
 */
static enum cti_port_type cti_get_port_type_fpga(struct exar8250 *priv,
						struct pci_dev *pcidev,
						unsigned int port_num)
{
	switch (pcidev->device) {
	case PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_12_XIG00X:
	case PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_12_XIG01X:
	case PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_16:
		return CTI_PORT_TYPE_RS232_422_485_HW;
	default:
		dev_err(&pcidev->dev, "unknown/unsupported device\n");
		return CTI_PORT_TYPE_NONE;
	}
}

/**
 * cti_get_port_type_xr17v35x() - Read port type from the EEPROM
 * @priv: Device's private structure
 * @pcidev: Pointer to the PCI device for this port
 * @port_num: port offset
 *
 * CTI XR17V35X based cards have the port types stored in the EEPROM.
 * This function reads the port type for a single port.
 *
 * Return: port type on success, CTI_PORT_TYPE_NONE on failure
 */
static enum cti_port_type cti_get_port_type_xr17v35x(struct exar8250 *priv,
						struct pci_dev *pcidev,
						unsigned int port_num)
{
	enum cti_port_type port_type;
	u16 port_flags;
	u8 offset;

	offset = CTI_EE_OFF_XR17V35X_PORT_FLAGS + port_num;
	eeprom_93cx6_read(&priv->eeprom, offset, &port_flags);

	port_type = FIELD_GET(CTI_EE_MASK_PORT_FLAGS_TYPE, port_flags);
	if (CTI_PORT_TYPE_VALID(port_type))
		return port_type;

	/*
	 * If the port type is missing the card assume it is a
	 * RS232/RS422/RS485 card to be safe.
	 *
	 * There is one known board (BEG013) that only has 3 of 4 port types
	 * written to the EEPROM so this acts as a work around.
	 */
	dev_warn(&pcidev->dev, "failed to get port %d type from EEPROM\n", port_num);

	return CTI_PORT_TYPE_RS232_422_485_HW;
}

static int cti_rs485_config_mpio_tristate(struct uart_port *port,
					struct ktermios *termios,
					struct serial_rs485 *rs485)
{
	struct exar8250 *priv = (struct exar8250 *)port->private_data;
	int ret;

	ret = generic_rs485_config(port, termios, rs485);
	if (ret)
		return ret;

	// Disable power-on RS485 tri-state via MPIO
	return cti_tristate_disable(priv, port->port_id);
}

static void cti_board_init_osc_freq(struct exar8250 *priv, struct pci_dev *pcidev, u8 eeprom_offset)
{
	int osc_freq;

	osc_freq = cti_read_osc_freq(priv, eeprom_offset);
	if (osc_freq <= 0) {
		dev_warn(&pcidev->dev, "failed to read OSC freq from EEPROM, using default\n");
		osc_freq = CTI_DEFAULT_PCI_OSC_FREQ;
	}

	priv->osc_freq = osc_freq;
}

static int cti_port_setup_common(struct exar8250 *priv,
				struct pci_dev *pcidev,
				int idx, unsigned int offset,
				struct uart_8250_port *port)
{
	int ret;

	port->port.port_id = idx;
	port->port.uartclk = priv->osc_freq;

	ret = serial8250_pci_setup_port(pcidev, port, 0, offset, 0);
	if (ret)
		return ret;

	port->port.private_data = (void *)priv;
	port->port.pm = exar_pm;
	port->port.shutdown = exar_shutdown;

	return 0;
}

static int cti_board_init_fpga(struct exar8250 *priv, struct pci_dev *pcidev)
{
	int ret;
	u16 cfg_val;

	// FPGA OSC is fixed to the 33MHz PCI clock
	priv->osc_freq = CTI_DEFAULT_FPGA_OSC_FREQ;

	// Enable external interrupts in special cfg space register
	ret = pci_read_config_word(pcidev, CTI_FPGA_CFG_INT_EN_REG, &cfg_val);
	if (ret)
		return pcibios_err_to_errno(ret);

	cfg_val |= CTI_FPGA_CFG_INT_EN_EXT_BIT;
	ret = pci_write_config_word(pcidev, CTI_FPGA_CFG_INT_EN_REG, cfg_val);
	if (ret)
		return pcibios_err_to_errno(ret);

	// RS485 gate needs to be enabled; otherwise RTS/CTS will not work
	exar_write_reg(priv, CTI_FPGA_RS485_IO_REG, 0x01);

	return 0;
}

static int cti_port_setup_fpga(struct exar8250 *priv,
				struct pci_dev *pcidev,
				struct uart_8250_port *port,
				int idx)
{
	enum cti_port_type port_type;
	unsigned int offset;
	int ret;

	if (idx == 0) {
		ret = cti_board_init_fpga(priv, pcidev);
		if (ret)
			return ret;
	}

	port_type = cti_get_port_type_fpga(priv, pcidev, idx);

	// FPGA shares port offsets with XR17C15X
	offset = idx * UART_EXAR_XR17C15X_PORT_OFFSET;
	port->port.type = PORT_XR17D15X;

	port->port.get_divisor = xr17v35x_get_divisor;
	port->port.set_divisor = xr17v35x_set_divisor;
	port->port.startup = xr17v35x_startup;

	if (CTI_PORT_TYPE_RS485(port_type)) {
		port->port.rs485_config = generic_rs485_config;
		port->port.rs485_supported = generic_rs485_supported;
	}

	return cti_port_setup_common(priv, pcidev, idx, offset, port);
}

static void cti_board_init_xr17v35x(struct exar8250 *priv, struct pci_dev *pcidev)
{
	// XR17V35X uses the PCIe clock rather than an oscillator
	priv->osc_freq = CTI_DEFAULT_PCIE_OSC_FREQ;
}

static int cti_port_setup_xr17v35x(struct exar8250 *priv,
				struct pci_dev *pcidev,
				struct uart_8250_port *port,
				int idx)
{
	enum cti_port_type port_type;
	unsigned int offset;
	int ret;

	if (idx == 0)
		cti_board_init_xr17v35x(priv, pcidev);

	port_type = cti_get_port_type_xr17v35x(priv, pcidev, idx);

	offset = idx * UART_EXAR_XR17V35X_PORT_OFFSET;
	port->port.type = PORT_XR17V35X;

	port->port.get_divisor = xr17v35x_get_divisor;
	port->port.set_divisor = xr17v35x_set_divisor;
	port->port.startup = xr17v35x_startup;

	switch (port_type) {
	case CTI_PORT_TYPE_RS422_485:
	case CTI_PORT_TYPE_RS232_422_485_HW:
		port->port.rs485_config = cti_rs485_config_mpio_tristate;
		port->port.rs485_supported = generic_rs485_supported;
		break;
	case CTI_PORT_TYPE_RS232_422_485_SW:
	case CTI_PORT_TYPE_RS232_422_485_4B:
	case CTI_PORT_TYPE_RS232_422_485_2B:
		port->port.rs485_config = generic_rs485_config;
		port->port.rs485_supported = generic_rs485_supported;
		break;
	default:
		break;
	}

	ret = cti_port_setup_common(priv, pcidev, idx, offset, port);
	if (ret)
		return ret;

	exar_write_reg(priv, (offset + UART_EXAR_8XMODE), 0x00);
	exar_write_reg(priv, (offset + UART_EXAR_FCTR), UART_FCTR_EXAR_TRGD);
	exar_write_reg(priv, (offset + UART_EXAR_TXTRG), 128);
	exar_write_reg(priv, (offset + UART_EXAR_RXTRG), 128);

	return 0;
}

static void cti_board_init_xr17v25x(struct exar8250 *priv, struct pci_dev *pcidev)
{
	cti_board_init_osc_freq(priv, pcidev, CTI_EE_OFF_XR17V25X_OSC_FREQ);

	/* enable interrupts on cards that need the "PLX fix" */
	switch (pcidev->subsystem_device) {
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_16_XPRS_A:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_16_XPRS_B:
		cti_plx_int_enable(priv);
		break;
	default:
		break;
	}
}

static int cti_port_setup_xr17v25x(struct exar8250 *priv,
				struct pci_dev *pcidev,
				struct uart_8250_port *port,
				int idx)
{
	enum cti_port_type port_type;
	unsigned int offset;
	int ret;

	if (idx == 0)
		cti_board_init_xr17v25x(priv, pcidev);

	port_type = cti_get_port_type_xr17c15x_xr17v25x(priv, pcidev, idx);

	offset = idx * UART_EXAR_XR17V25X_PORT_OFFSET;
	port->port.type = PORT_XR17D15X;

	// XR17V25X supports fractional baudrates
	port->port.get_divisor = xr17v35x_get_divisor;
	port->port.set_divisor = xr17v35x_set_divisor;
	port->port.startup = xr17v35x_startup;

	if (CTI_PORT_TYPE_RS485(port_type)) {
		switch (pcidev->subsystem_device) {
		// These cards support power on 485 tri-state via MPIO
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP_485:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4_SP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_6_2_SP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_6_SP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XP_OPTO_LEFT:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XP_OPTO_RIGHT:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XP_OPTO:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4_XPRS_OPTO:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP_485:
			port->port.rs485_config = cti_rs485_config_mpio_tristate;
			break;
		// Otherwise auto or no power on 485 tri-state support
		default:
			port->port.rs485_config = generic_rs485_config;
			break;
		}

		port->port.rs485_supported = generic_rs485_supported;
	}

	ret = cti_port_setup_common(priv, pcidev, idx, offset, port);
	if (ret)
		return ret;

	exar_write_reg(priv, (offset + UART_EXAR_8XMODE), 0x00);
	exar_write_reg(priv, (offset + UART_EXAR_FCTR), UART_FCTR_EXAR_TRGD);
	exar_write_reg(priv, (offset + UART_EXAR_TXTRG), 32);
	exar_write_reg(priv, (offset + UART_EXAR_RXTRG), 32);

	return 0;
}

static void cti_board_init_xr17c15x(struct exar8250 *priv, struct pci_dev *pcidev)
{
	cti_board_init_osc_freq(priv, pcidev, CTI_EE_OFF_XR17C15X_OSC_FREQ);

	/* enable interrupts on cards that need the "PLX fix" */
	switch (pcidev->subsystem_device) {
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XPRS:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_A:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_B:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XPRS_OPTO:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_OPTO_A:
	case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XPRS_OPTO_B:
		cti_plx_int_enable(priv);
		break;
	default:
		break;
	}
}

static int cti_port_setup_xr17c15x(struct exar8250 *priv,
				struct pci_dev *pcidev,
				struct uart_8250_port *port,
				int idx)
{
	enum cti_port_type port_type;
	unsigned int offset;

	if (idx == 0)
		cti_board_init_xr17c15x(priv, pcidev);

	port_type = cti_get_port_type_xr17c15x_xr17v25x(priv, pcidev, idx);

	offset = idx * UART_EXAR_XR17C15X_PORT_OFFSET;
	port->port.type = PORT_XR17D15X;

	if (CTI_PORT_TYPE_RS485(port_type)) {
		switch (pcidev->subsystem_device) {
		// These cards support power on 485 tri-state via MPIO
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_SP_485:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4_SP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_6_2_SP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_6_SP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XP_OPTO_LEFT:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_2_XP_OPTO_RIGHT:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_XP_OPTO:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_4_4_XPRS_OPTO:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP:
		case PCI_SUBDEVICE_ID_CONNECT_TECH_PCI_UART_8_XPRS_LP_485:
			port->port.rs485_config = cti_rs485_config_mpio_tristate;
			break;
		// Otherwise auto or no power on 485 tri-state support
		default:
			port->port.rs485_config = generic_rs485_config;
			break;
		}

		port->port.rs485_supported = generic_rs485_supported;
	}

	return cti_port_setup_common(priv, pcidev, idx, offset, port);
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
	device_set_node(&pdev->dev, dev_fwnode(&pcidev->dev));

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

static int sealevel_rs485_config(struct uart_port *port, struct ktermios *termios,
				  struct serial_rs485 *rs485)
{
	u8 __iomem *p = port->membase;
	u8 old_lcr;
	u8 efr;
	u8 dld;
	int ret;

	ret = generic_rs485_config(port, termios, rs485);
	if (ret)
		return ret;

	if (!(rs485->flags & SER_RS485_ENABLED))
		return 0;

	old_lcr = readb(p + UART_LCR);

	/* Set EFR[4]=1 to enable enhanced feature registers */
	efr = readb(p + UART_XR_EFR);
	efr |= UART_EFR_ECB;
	writeb(efr, p + UART_XR_EFR);

	/* Set MCR to use DTR as Auto-RS485 Enable signal */
	writeb(UART_MCR_OUT1, p + UART_MCR);

	/* Set LCR[7]=1 to enable access to DLD register */
	writeb(old_lcr | UART_LCR_DLAB, p + UART_LCR);

	/* Set DLD[7]=1 for inverted RS485 Enable logic */
	dld = readb(p + UART_EXAR_DLD);
	dld |= UART_EXAR_DLD_485_POLARITY;
	writeb(dld, p + UART_EXAR_DLD);

	writeb(old_lcr, p + UART_LCR);

	return 0;
}

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
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND |
		 SER_RS485_RX_DURING_TX | SER_RS485_TERMINATE_BUS,
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

	if (pcidev->subsystem_vendor == PCI_VENDOR_ID_SEALEVEL)
		port->port.rs485_config = sealevel_rs485_config;

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

static unsigned int exar_get_nr_ports(struct exar8250_board *board, struct pci_dev *pcidev)
{
	if (pcidev->vendor == PCI_VENDOR_ID_ACCESSIO)
		return BIT(((pcidev->device & 0x38) >> 3) - 1);

	// Check if board struct overrides number of ports
	if (board->num_ports > 0)
		return board->num_ports;

	// Exar encodes # ports in last nibble of PCI Device ID ex. 0358
	if (pcidev->vendor == PCI_VENDOR_ID_EXAR)
		return pcidev->device & 0x0f;

	// Handle CTI FPGA cards
	if (pcidev->vendor == PCI_VENDOR_ID_CONNECT_TECH) {
		switch (pcidev->device) {
		case PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_12_XIG00X:
		case PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_12_XIG01X:
			return 12;
		case PCI_DEVICE_ID_CONNECT_TECH_PCI_XR79X_16:
			return 16;
		default:
			return 0;
		}
	}

	return 0;
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

	nr_ports = exar_get_nr_ports(board, pcidev);
	if (nr_ports == 0)
		return dev_err_probe(&pcidev->dev, -ENODEV, "failed to get number of ports\n");

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

	/* Clear interrupts */
	exar_misc_clear(priv);

	rc = devm_request_irq(&pcidev->dev, uart.port.irq, exar_misc_handler,
			 IRQF_SHARED, "exar_uart", priv);
	if (rc)
		return rc;

	exar_eeprom_init(priv);

	for (i = 0; i < nr_ports && i < maxnr; i++) {
		rc = board->setup(priv, pcidev, &uart, i);
		if (rc) {
			dev_err_probe(&pcidev->dev, rc, "Failed to setup port %u\n", i);
			break;
		}

		dev_dbg(&pcidev->dev, "Setup PCI port: port %lx, irq %d, type %d\n",
			uart.port.iobase, uart.port.irq, uart.port.iotype);

		priv->line[i] = serial8250_register_8250_port(&uart);
		if (priv->line[i] < 0) {
			dev_err_probe(&pcidev->dev, priv->line[i],
				"Couldn't register serial port %lx, type %d, irq %d\n",
				uart.port.iobase, uart.port.iotype, uart.port.irq);
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

	/* Ensure that every init quirk is properly torn down */
	if (priv->board->exit)
		priv->board->exit(pcidev);
}

static int exar_suspend(struct device *dev)
{
	struct exar8250 *priv = dev_get_drvdata(dev);
	unsigned int i;

	for (i = 0; i < priv->nr; i++)
		if (priv->line[i] >= 0)
			serial8250_suspend_port(priv->line[i]);

	return 0;
}

static int exar_resume(struct device *dev)
{
	struct exar8250 *priv = dev_get_drvdata(dev);
	unsigned int i;

	exar_misc_clear(priv);

	for (i = 0; i < priv->nr; i++)
		if (priv->line[i] >= 0)
			serial8250_resume_port(priv->line[i]);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(exar_pci_pm, exar_suspend, exar_resume);

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

static const struct exar8250_board pbn_cti_xr17c15x = {
	.setup		= cti_port_setup_xr17c15x,
};

static const struct exar8250_board pbn_cti_xr17v25x = {
	.setup		= cti_port_setup_xr17v25x,
};

static const struct exar8250_board pbn_cti_xr17v35x = {
	.setup		= cti_port_setup_xr17v35x,
};

static const struct exar8250_board pbn_cti_fpga = {
	.setup		= cti_port_setup_fpga,
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

#define CTI_EXAR_DEVICE(devid, bd) {                    \
	PCI_DEVICE_SUB(                                 \
		PCI_VENDOR_ID_EXAR,                     \
		PCI_DEVICE_ID_EXAR_##devid,             \
		PCI_SUBVENDOR_ID_CONNECT_TECH,          \
		PCI_ANY_ID), 0, 0,                      \
		(kernel_ulong_t)&bd                     \
	}

#define EXAR_DEVICE(vend, devid, bd) { PCI_DEVICE_DATA(vend, devid, &bd) }

#define IBM_DEVICE(devid, sdevid, bd) {			\
	PCI_DEVICE_SUB(					\
		PCI_VENDOR_ID_EXAR,			\
		PCI_DEVICE_ID_EXAR_##devid,		\
		PCI_SUBVENDOR_ID_IBM,			\
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

	/* Connect Tech cards with Exar vendor/device PCI IDs */
	CTI_EXAR_DEVICE(XR17C152,       pbn_cti_xr17c15x),
	CTI_EXAR_DEVICE(XR17C154,       pbn_cti_xr17c15x),
	CTI_EXAR_DEVICE(XR17C158,       pbn_cti_xr17c15x),

	CTI_EXAR_DEVICE(XR17V252,       pbn_cti_xr17v25x),
	CTI_EXAR_DEVICE(XR17V254,       pbn_cti_xr17v25x),
	CTI_EXAR_DEVICE(XR17V258,       pbn_cti_xr17v25x),

	CTI_EXAR_DEVICE(XR17V352,       pbn_cti_xr17v35x),
	CTI_EXAR_DEVICE(XR17V354,       pbn_cti_xr17v35x),
	CTI_EXAR_DEVICE(XR17V358,       pbn_cti_xr17v35x),

	/* Connect Tech cards with Connect Tech vendor/device PCI IDs (FPGA based) */
	EXAR_DEVICE(CONNECT_TECH, PCI_XR79X_12_XIG00X, pbn_cti_fpga),
	EXAR_DEVICE(CONNECT_TECH, PCI_XR79X_12_XIG01X, pbn_cti_fpga),
	EXAR_DEVICE(CONNECT_TECH, PCI_XR79X_16,        pbn_cti_fpga),

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
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, exar_pci_tbl);

static struct pci_driver exar_pci_driver = {
	.name		= "exar_serial",
	.probe		= exar_pci_probe,
	.remove		= exar_pci_remove,
	.driver         = {
		.pm     = pm_sleep_ptr(&exar_pci_pm),
	},
	.id_table	= exar_pci_tbl,
};
module_pci_driver(exar_pci_driver);

MODULE_IMPORT_NS("SERIAL_8250_PCI");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Exar Serial Driver");
MODULE_AUTHOR("Sudip Mukherjee <sudip.mukherjee@codethink.co.uk>");
