// SPDX-License-Identifier: GPL-2.0+
/*
 * SC16IS7xx tty serial driver - Copyright (C) 2014 GridPoint
 * Author: Jon Ringle <jringle@gridpoint.com>
 *
 *  Based on max310x.c, by Alexander Shiyan <shc_work@mail.ru>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <uapi/linux/sched/types.h>

#define SC16IS7XX_NAME			"sc16is7xx"
#define SC16IS7XX_MAX_DEVS		8

/* SC16IS7XX register definitions */
#define SC16IS7XX_RHR_REG		(0x00) /* RX FIFO */
#define SC16IS7XX_THR_REG		(0x00) /* TX FIFO */
#define SC16IS7XX_IER_REG		(0x01) /* Interrupt enable */
#define SC16IS7XX_IIR_REG		(0x02) /* Interrupt Identification */
#define SC16IS7XX_FCR_REG		(0x02) /* FIFO control */
#define SC16IS7XX_LCR_REG		(0x03) /* Line Control */
#define SC16IS7XX_MCR_REG		(0x04) /* Modem Control */
#define SC16IS7XX_LSR_REG		(0x05) /* Line Status */
#define SC16IS7XX_MSR_REG		(0x06) /* Modem Status */
#define SC16IS7XX_SPR_REG		(0x07) /* Scratch Pad */
#define SC16IS7XX_TXLVL_REG		(0x08) /* TX FIFO level */
#define SC16IS7XX_RXLVL_REG		(0x09) /* RX FIFO level */
#define SC16IS7XX_IODIR_REG		(0x0a) /* I/O Direction
						* - only on 75x/76x
						*/
#define SC16IS7XX_IOSTATE_REG		(0x0b) /* I/O State
						* - only on 75x/76x
						*/
#define SC16IS7XX_IOINTENA_REG		(0x0c) /* I/O Interrupt Enable
						* - only on 75x/76x
						*/
#define SC16IS7XX_IOCONTROL_REG		(0x0e) /* I/O Control
						* - only on 75x/76x
						*/
#define SC16IS7XX_EFCR_REG		(0x0f) /* Extra Features Control */

/* TCR/TLR Register set: Only if ((MCR[2] == 1) && (EFR[4] == 1)) */
#define SC16IS7XX_TCR_REG		(0x06) /* Transmit control */
#define SC16IS7XX_TLR_REG		(0x07) /* Trigger level */

/* Special Register set: Only if ((LCR[7] == 1) && (LCR != 0xBF)) */
#define SC16IS7XX_DLL_REG		(0x00) /* Divisor Latch Low */
#define SC16IS7XX_DLH_REG		(0x01) /* Divisor Latch High */

/* Enhanced Register set: Only if (LCR == 0xBF) */
#define SC16IS7XX_EFR_REG		(0x02) /* Enhanced Features */
#define SC16IS7XX_XON1_REG		(0x04) /* Xon1 word */
#define SC16IS7XX_XON2_REG		(0x05) /* Xon2 word */
#define SC16IS7XX_XOFF1_REG		(0x06) /* Xoff1 word */
#define SC16IS7XX_XOFF2_REG		(0x07) /* Xoff2 word */

/* IER register bits */
#define SC16IS7XX_IER_RDI_BIT		(1 << 0) /* Enable RX data interrupt */
#define SC16IS7XX_IER_THRI_BIT		(1 << 1) /* Enable TX holding register
						  * interrupt */
#define SC16IS7XX_IER_RLSI_BIT		(1 << 2) /* Enable RX line status
						  * interrupt */
#define SC16IS7XX_IER_MSI_BIT		(1 << 3) /* Enable Modem status
						  * interrupt */

/* IER register bits - write only if (EFR[4] == 1) */
#define SC16IS7XX_IER_SLEEP_BIT		(1 << 4) /* Enable Sleep mode */
#define SC16IS7XX_IER_XOFFI_BIT		(1 << 5) /* Enable Xoff interrupt */
#define SC16IS7XX_IER_RTSI_BIT		(1 << 6) /* Enable nRTS interrupt */
#define SC16IS7XX_IER_CTSI_BIT		(1 << 7) /* Enable nCTS interrupt */

/* FCR register bits */
#define SC16IS7XX_FCR_FIFO_BIT		(1 << 0) /* Enable FIFO */
#define SC16IS7XX_FCR_RXRESET_BIT	(1 << 1) /* Reset RX FIFO */
#define SC16IS7XX_FCR_TXRESET_BIT	(1 << 2) /* Reset TX FIFO */
#define SC16IS7XX_FCR_RXLVLL_BIT	(1 << 6) /* RX Trigger level LSB */
#define SC16IS7XX_FCR_RXLVLH_BIT	(1 << 7) /* RX Trigger level MSB */

/* FCR register bits - write only if (EFR[4] == 1) */
#define SC16IS7XX_FCR_TXLVLL_BIT	(1 << 4) /* TX Trigger level LSB */
#define SC16IS7XX_FCR_TXLVLH_BIT	(1 << 5) /* TX Trigger level MSB */

/* IIR register bits */
#define SC16IS7XX_IIR_NO_INT_BIT	(1 << 0) /* No interrupts pending */
#define SC16IS7XX_IIR_ID_MASK		0x3e     /* Mask for the interrupt ID */
#define SC16IS7XX_IIR_THRI_SRC		0x02     /* TX holding register empty */
#define SC16IS7XX_IIR_RDI_SRC		0x04     /* RX data interrupt */
#define SC16IS7XX_IIR_RLSE_SRC		0x06     /* RX line status error */
#define SC16IS7XX_IIR_RTOI_SRC		0x0c     /* RX time-out interrupt */
#define SC16IS7XX_IIR_MSI_SRC		0x00     /* Modem status interrupt
						  * - only on 75x/76x
						  */
#define SC16IS7XX_IIR_INPIN_SRC		0x30     /* Input pin change of state
						  * - only on 75x/76x
						  */
#define SC16IS7XX_IIR_XOFFI_SRC		0x10     /* Received Xoff */
#define SC16IS7XX_IIR_CTSRTS_SRC	0x20     /* nCTS,nRTS change of state
						  * from active (LOW)
						  * to inactive (HIGH)
						  */
/* LCR register bits */
#define SC16IS7XX_LCR_LENGTH0_BIT	(1 << 0) /* Word length bit 0 */
#define SC16IS7XX_LCR_LENGTH1_BIT	(1 << 1) /* Word length bit 1
						  *
						  * Word length bits table:
						  * 00 -> 5 bit words
						  * 01 -> 6 bit words
						  * 10 -> 7 bit words
						  * 11 -> 8 bit words
						  */
#define SC16IS7XX_LCR_STOPLEN_BIT	(1 << 2) /* STOP length bit
						  *
						  * STOP length bit table:
						  * 0 -> 1 stop bit
						  * 1 -> 1-1.5 stop bits if
						  *      word length is 5,
						  *      2 stop bits otherwise
						  */
#define SC16IS7XX_LCR_PARITY_BIT	(1 << 3) /* Parity bit enable */
#define SC16IS7XX_LCR_EVENPARITY_BIT	(1 << 4) /* Even parity bit enable */
#define SC16IS7XX_LCR_FORCEPARITY_BIT	(1 << 5) /* 9-bit multidrop parity */
#define SC16IS7XX_LCR_TXBREAK_BIT	(1 << 6) /* TX break enable */
#define SC16IS7XX_LCR_DLAB_BIT		(1 << 7) /* Divisor Latch enable */
#define SC16IS7XX_LCR_WORD_LEN_5	(0x00)
#define SC16IS7XX_LCR_WORD_LEN_6	(0x01)
#define SC16IS7XX_LCR_WORD_LEN_7	(0x02)
#define SC16IS7XX_LCR_WORD_LEN_8	(0x03)
#define SC16IS7XX_LCR_CONF_MODE_A	SC16IS7XX_LCR_DLAB_BIT /* Special
								* reg set */
#define SC16IS7XX_LCR_CONF_MODE_B	0xBF                   /* Enhanced
								* reg set */

/* MCR register bits */
#define SC16IS7XX_MCR_DTR_BIT		(1 << 0) /* DTR complement
						  * - only on 75x/76x
						  */
#define SC16IS7XX_MCR_RTS_BIT		(1 << 1) /* RTS complement */
#define SC16IS7XX_MCR_TCRTLR_BIT	(1 << 2) /* TCR/TLR register enable */
#define SC16IS7XX_MCR_LOOP_BIT		(1 << 4) /* Enable loopback test mode */
#define SC16IS7XX_MCR_XONANY_BIT	(1 << 5) /* Enable Xon Any
						  * - write enabled
						  * if (EFR[4] == 1)
						  */
#define SC16IS7XX_MCR_IRDA_BIT		(1 << 6) /* Enable IrDA mode
						  * - write enabled
						  * if (EFR[4] == 1)
						  */
#define SC16IS7XX_MCR_CLKSEL_BIT	(1 << 7) /* Divide clock by 4
						  * - write enabled
						  * if (EFR[4] == 1)
						  */

/* LSR register bits */
#define SC16IS7XX_LSR_DR_BIT		(1 << 0) /* Receiver data ready */
#define SC16IS7XX_LSR_OE_BIT		(1 << 1) /* Overrun Error */
#define SC16IS7XX_LSR_PE_BIT		(1 << 2) /* Parity Error */
#define SC16IS7XX_LSR_FE_BIT		(1 << 3) /* Frame Error */
#define SC16IS7XX_LSR_BI_BIT		(1 << 4) /* Break Interrupt */
#define SC16IS7XX_LSR_BRK_ERROR_MASK	0x1E     /* BI, FE, PE, OE bits */
#define SC16IS7XX_LSR_THRE_BIT		(1 << 5) /* TX holding register empty */
#define SC16IS7XX_LSR_TEMT_BIT		(1 << 6) /* Transmitter empty */
#define SC16IS7XX_LSR_FIFOE_BIT		(1 << 7) /* Fifo Error */

/* MSR register bits */
#define SC16IS7XX_MSR_DCTS_BIT		(1 << 0) /* Delta CTS Clear To Send */
#define SC16IS7XX_MSR_DDSR_BIT		(1 << 1) /* Delta DSR Data Set Ready
						  * or (IO4)
						  * - only on 75x/76x
						  */
#define SC16IS7XX_MSR_DRI_BIT		(1 << 2) /* Delta RI Ring Indicator
						  * or (IO7)
						  * - only on 75x/76x
						  */
#define SC16IS7XX_MSR_DCD_BIT		(1 << 3) /* Delta CD Carrier Detect
						  * or (IO6)
						  * - only on 75x/76x
						  */
#define SC16IS7XX_MSR_CTS_BIT		(1 << 4) /* CTS */
#define SC16IS7XX_MSR_DSR_BIT		(1 << 5) /* DSR (IO4)
						  * - only on 75x/76x
						  */
#define SC16IS7XX_MSR_RI_BIT		(1 << 6) /* RI (IO7)
						  * - only on 75x/76x
						  */
#define SC16IS7XX_MSR_CD_BIT		(1 << 7) /* CD (IO6)
						  * - only on 75x/76x
						  */
#define SC16IS7XX_MSR_DELTA_MASK	0x0F     /* Any of the delta bits! */

/*
 * TCR register bits
 * TCR trigger levels are available from 0 to 60 characters with a granularity
 * of four.
 * The programmer must program the TCR such that TCR[3:0] > TCR[7:4]. There is
 * no built-in hardware check to make sure this condition is met. Also, the TCR
 * must be programmed with this condition before auto RTS or software flow
 * control is enabled to avoid spurious operation of the device.
 */
#define SC16IS7XX_TCR_RX_HALT(words)	((((words) / 4) & 0x0f) << 0)
#define SC16IS7XX_TCR_RX_RESUME(words)	((((words) / 4) & 0x0f) << 4)

/*
 * TLR register bits
 * If TLR[3:0] or TLR[7:4] are logical 0, the selectable trigger levels via the
 * FIFO Control Register (FCR) are used for the transmit and receive FIFO
 * trigger levels. Trigger levels from 4 characters to 60 characters are
 * available with a granularity of four.
 *
 * When the trigger level setting in TLR is zero, the SC16IS740/750/760 uses the
 * trigger level setting defined in FCR. If TLR has non-zero trigger level value
 * the trigger level defined in FCR is discarded. This applies to both transmit
 * FIFO and receive FIFO trigger level setting.
 *
 * When TLR is used for RX trigger level control, FCR[7:6] should be left at the
 * default state, that is, '00'.
 */
#define SC16IS7XX_TLR_TX_TRIGGER(words)	((((words) / 4) & 0x0f) << 0)
#define SC16IS7XX_TLR_RX_TRIGGER(words)	((((words) / 4) & 0x0f) << 4)

/* IOControl register bits (Only 750/760) */
#define SC16IS7XX_IOCONTROL_LATCH_BIT	(1 << 0) /* Enable input latching */
#define SC16IS7XX_IOCONTROL_MODEM_BIT	(1 << 1) /* Enable GPIO[7:4] as modem pins */
#define SC16IS7XX_IOCONTROL_SRESET_BIT	(1 << 3) /* Software Reset */

/* EFCR register bits */
#define SC16IS7XX_EFCR_9BIT_MODE_BIT	(1 << 0) /* Enable 9-bit or Multidrop
						  * mode (RS485) */
#define SC16IS7XX_EFCR_RXDISABLE_BIT	(1 << 1) /* Disable receiver */
#define SC16IS7XX_EFCR_TXDISABLE_BIT	(1 << 2) /* Disable transmitter */
#define SC16IS7XX_EFCR_AUTO_RS485_BIT	(1 << 4) /* Auto RS485 RTS direction */
#define SC16IS7XX_EFCR_RTS_INVERT_BIT	(1 << 5) /* RTS output inversion */
#define SC16IS7XX_EFCR_IRDA_MODE_BIT	(1 << 7) /* IrDA mode
						  * 0 = rate upto 115.2 kbit/s
						  *   - Only 750/760
						  * 1 = rate upto 1.152 Mbit/s
						  *   - Only 760
						  */

/* EFR register bits */
#define SC16IS7XX_EFR_AUTORTS_BIT	(1 << 6) /* Auto RTS flow ctrl enable */
#define SC16IS7XX_EFR_AUTOCTS_BIT	(1 << 7) /* Auto CTS flow ctrl enable */
#define SC16IS7XX_EFR_XOFF2_DETECT_BIT	(1 << 5) /* Enable Xoff2 detection */
#define SC16IS7XX_EFR_ENABLE_BIT	(1 << 4) /* Enable enhanced functions
						  * and writing to IER[7:4],
						  * FCR[5:4], MCR[7:5]
						  */
#define SC16IS7XX_EFR_SWFLOW3_BIT	(1 << 3) /* SWFLOW bit 3 */
#define SC16IS7XX_EFR_SWFLOW2_BIT	(1 << 2) /* SWFLOW bit 2
						  *
						  * SWFLOW bits 3 & 2 table:
						  * 00 -> no transmitter flow
						  *       control
						  * 01 -> transmitter generates
						  *       XON2 and XOFF2
						  * 10 -> transmitter generates
						  *       XON1 and XOFF1
						  * 11 -> transmitter generates
						  *       XON1, XON2, XOFF1 and
						  *       XOFF2
						  */
#define SC16IS7XX_EFR_SWFLOW1_BIT	(1 << 1) /* SWFLOW bit 2 */
#define SC16IS7XX_EFR_SWFLOW0_BIT	(1 << 0) /* SWFLOW bit 3
						  *
						  * SWFLOW bits 3 & 2 table:
						  * 00 -> no received flow
						  *       control
						  * 01 -> receiver compares
						  *       XON2 and XOFF2
						  * 10 -> receiver compares
						  *       XON1 and XOFF1
						  * 11 -> receiver compares
						  *       XON1, XON2, XOFF1 and
						  *       XOFF2
						  */

/* Misc definitions */
#define SC16IS7XX_FIFO_SIZE		(64)
#define SC16IS7XX_REG_SHIFT		2

struct sc16is7xx_devtype {
	char	name[10];
	int	nr_gpio;
	int	nr_uart;
};

#define SC16IS7XX_RECONF_MD		(1 << 0)
#define SC16IS7XX_RECONF_IER		(1 << 1)
#define SC16IS7XX_RECONF_RS485		(1 << 2)

struct sc16is7xx_one_config {
	unsigned int			flags;
	u8				ier_clear;
};

struct sc16is7xx_one {
	struct uart_port		port;
	u8				line;
	struct kthread_work		tx_work;
	struct kthread_work		reg_work;
	struct sc16is7xx_one_config	config;
};

struct sc16is7xx_port {
	const struct sc16is7xx_devtype	*devtype;
	struct regmap			*regmap;
	struct clk			*clk;
#ifdef CONFIG_GPIOLIB
	struct gpio_chip		gpio;
#endif
	unsigned char			buf[SC16IS7XX_FIFO_SIZE];
	struct kthread_worker		kworker;
	struct task_struct		*kworker_task;
	struct kthread_work		irq_work;
	struct mutex			efr_lock;
	struct sc16is7xx_one		p[0];
};

static unsigned long sc16is7xx_lines;

static struct uart_driver sc16is7xx_uart = {
	.owner		= THIS_MODULE,
	.dev_name	= "ttySC",
	.nr		= SC16IS7XX_MAX_DEVS,
};

#define to_sc16is7xx_port(p,e)	((container_of((p), struct sc16is7xx_port, e)))
#define to_sc16is7xx_one(p,e)	((container_of((p), struct sc16is7xx_one, e)))

static int sc16is7xx_line(struct uart_port *port)
{
	struct sc16is7xx_one *one = to_sc16is7xx_one(port, port);

	return one->line;
}

static u8 sc16is7xx_port_read(struct uart_port *port, u8 reg)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	unsigned int val = 0;
	const u8 line = sc16is7xx_line(port);

	regmap_read(s->regmap, (reg << SC16IS7XX_REG_SHIFT) | line, &val);

	return val;
}

static void sc16is7xx_port_write(struct uart_port *port, u8 reg, u8 val)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	const u8 line = sc16is7xx_line(port);

	regmap_write(s->regmap, (reg << SC16IS7XX_REG_SHIFT) | line, val);
}

static void sc16is7xx_fifo_read(struct uart_port *port, unsigned int rxlen)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	const u8 line = sc16is7xx_line(port);
	u8 addr = (SC16IS7XX_RHR_REG << SC16IS7XX_REG_SHIFT) | line;

	regcache_cache_bypass(s->regmap, true);
	regmap_raw_read(s->regmap, addr, s->buf, rxlen);
	regcache_cache_bypass(s->regmap, false);
}

static void sc16is7xx_fifo_write(struct uart_port *port, u8 to_send)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	const u8 line = sc16is7xx_line(port);
	u8 addr = (SC16IS7XX_THR_REG << SC16IS7XX_REG_SHIFT) | line;

	/*
	 * Don't send zero-length data, at least on SPI it confuses the chip
	 * delivering wrong TXLVL data.
	 */
	if (unlikely(!to_send))
		return;

	regcache_cache_bypass(s->regmap, true);
	regmap_raw_write(s->regmap, addr, s->buf, to_send);
	regcache_cache_bypass(s->regmap, false);
}

static void sc16is7xx_port_update(struct uart_port *port, u8 reg,
				  u8 mask, u8 val)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	const u8 line = sc16is7xx_line(port);

	regmap_update_bits(s->regmap, (reg << SC16IS7XX_REG_SHIFT) | line,
			   mask, val);
}

static int sc16is7xx_alloc_line(void)
{
	int i;

	BUILD_BUG_ON(SC16IS7XX_MAX_DEVS > BITS_PER_LONG);

	for (i = 0; i < SC16IS7XX_MAX_DEVS; i++)
		if (!test_and_set_bit(i, &sc16is7xx_lines))
			break;

	return i;
}

static void sc16is7xx_power(struct uart_port *port, int on)
{
	sc16is7xx_port_update(port, SC16IS7XX_IER_REG,
			      SC16IS7XX_IER_SLEEP_BIT,
			      on ? 0 : SC16IS7XX_IER_SLEEP_BIT);
}

static const struct sc16is7xx_devtype sc16is74x_devtype = {
	.name		= "SC16IS74X",
	.nr_gpio	= 0,
	.nr_uart	= 1,
};

static const struct sc16is7xx_devtype sc16is750_devtype = {
	.name		= "SC16IS750",
	.nr_gpio	= 8,
	.nr_uart	= 1,
};

static const struct sc16is7xx_devtype sc16is752_devtype = {
	.name		= "SC16IS752",
	.nr_gpio	= 8,
	.nr_uart	= 2,
};

static const struct sc16is7xx_devtype sc16is760_devtype = {
	.name		= "SC16IS760",
	.nr_gpio	= 8,
	.nr_uart	= 1,
};

static const struct sc16is7xx_devtype sc16is762_devtype = {
	.name		= "SC16IS762",
	.nr_gpio	= 8,
	.nr_uart	= 2,
};

static bool sc16is7xx_regmap_volatile(struct device *dev, unsigned int reg)
{
	switch (reg >> SC16IS7XX_REG_SHIFT) {
	case SC16IS7XX_RHR_REG:
	case SC16IS7XX_IIR_REG:
	case SC16IS7XX_LSR_REG:
	case SC16IS7XX_MSR_REG:
	case SC16IS7XX_TXLVL_REG:
	case SC16IS7XX_RXLVL_REG:
	case SC16IS7XX_IOSTATE_REG:
		return true;
	default:
		break;
	}

	return false;
}

static bool sc16is7xx_regmap_precious(struct device *dev, unsigned int reg)
{
	switch (reg >> SC16IS7XX_REG_SHIFT) {
	case SC16IS7XX_RHR_REG:
		return true;
	default:
		break;
	}

	return false;
}

static int sc16is7xx_set_baud(struct uart_port *port, int baud)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	u8 lcr;
	u8 prescaler = 0;
	unsigned long clk = port->uartclk, div = clk / 16 / baud;

	if (div > 0xffff) {
		prescaler = SC16IS7XX_MCR_CLKSEL_BIT;
		div /= 4;
	}

	/* In an amazing feat of design, the Enhanced Features Register shares
	 * the address of the Interrupt Identification Register, and is
	 * switched in by writing a magic value (0xbf) to the Line Control
	 * Register. Any interrupt firing during this time will see the EFR
	 * where it expects the IIR to be, leading to "Unexpected interrupt"
	 * messages.
	 *
	 * Prevent this possibility by claiming a mutex while accessing the
	 * EFR, and claiming the same mutex from within the interrupt handler.
	 * This is similar to disabling the interrupt, but that doesn't work
	 * because the bulk of the interrupt processing is run as a workqueue
	 * job in thread context.
	 */
	mutex_lock(&s->efr_lock);

	lcr = sc16is7xx_port_read(port, SC16IS7XX_LCR_REG);

	/* Open the LCR divisors for configuration */
	sc16is7xx_port_write(port, SC16IS7XX_LCR_REG,
			     SC16IS7XX_LCR_CONF_MODE_B);

	/* Enable enhanced features */
	regcache_cache_bypass(s->regmap, true);
	sc16is7xx_port_write(port, SC16IS7XX_EFR_REG,
			     SC16IS7XX_EFR_ENABLE_BIT);
	regcache_cache_bypass(s->regmap, false);

	/* Put LCR back to the normal mode */
	sc16is7xx_port_write(port, SC16IS7XX_LCR_REG, lcr);

	mutex_unlock(&s->efr_lock);

	sc16is7xx_port_update(port, SC16IS7XX_MCR_REG,
			      SC16IS7XX_MCR_CLKSEL_BIT,
			      prescaler);

	/* Open the LCR divisors for configuration */
	sc16is7xx_port_write(port, SC16IS7XX_LCR_REG,
			     SC16IS7XX_LCR_CONF_MODE_A);

	/* Write the new divisor */
	regcache_cache_bypass(s->regmap, true);
	sc16is7xx_port_write(port, SC16IS7XX_DLH_REG, div / 256);
	sc16is7xx_port_write(port, SC16IS7XX_DLL_REG, div % 256);
	regcache_cache_bypass(s->regmap, false);

	/* Put LCR back to the normal mode */
	sc16is7xx_port_write(port, SC16IS7XX_LCR_REG, lcr);

	return DIV_ROUND_CLOSEST(clk / 16, div);
}

static void sc16is7xx_handle_rx(struct uart_port *port, unsigned int rxlen,
				unsigned int iir)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	unsigned int lsr = 0, ch, flag, bytes_read, i;
	bool read_lsr = (iir == SC16IS7XX_IIR_RLSE_SRC) ? true : false;

	if (unlikely(rxlen >= sizeof(s->buf))) {
		dev_warn_ratelimited(port->dev,
				     "ttySC%i: Possible RX FIFO overrun: %d\n",
				     port->line, rxlen);
		port->icount.buf_overrun++;
		/* Ensure sanity of RX level */
		rxlen = sizeof(s->buf);
	}

	while (rxlen) {
		/* Only read lsr if there are possible errors in FIFO */
		if (read_lsr) {
			lsr = sc16is7xx_port_read(port, SC16IS7XX_LSR_REG);
			if (!(lsr & SC16IS7XX_LSR_FIFOE_BIT))
				read_lsr = false; /* No errors left in FIFO */
		} else
			lsr = 0;

		if (read_lsr) {
			s->buf[0] = sc16is7xx_port_read(port, SC16IS7XX_RHR_REG);
			bytes_read = 1;
		} else {
			sc16is7xx_fifo_read(port, rxlen);
			bytes_read = rxlen;
		}

		lsr &= SC16IS7XX_LSR_BRK_ERROR_MASK;

		port->icount.rx++;
		flag = TTY_NORMAL;

		if (unlikely(lsr)) {
			if (lsr & SC16IS7XX_LSR_BI_BIT) {
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if (lsr & SC16IS7XX_LSR_PE_BIT)
				port->icount.parity++;
			else if (lsr & SC16IS7XX_LSR_FE_BIT)
				port->icount.frame++;
			else if (lsr & SC16IS7XX_LSR_OE_BIT)
				port->icount.overrun++;

			lsr &= port->read_status_mask;
			if (lsr & SC16IS7XX_LSR_BI_BIT)
				flag = TTY_BREAK;
			else if (lsr & SC16IS7XX_LSR_PE_BIT)
				flag = TTY_PARITY;
			else if (lsr & SC16IS7XX_LSR_FE_BIT)
				flag = TTY_FRAME;
			else if (lsr & SC16IS7XX_LSR_OE_BIT)
				flag = TTY_OVERRUN;
		}

		for (i = 0; i < bytes_read; ++i) {
			ch = s->buf[i];
			if (uart_handle_sysrq_char(port, ch))
				continue;

			if (lsr & port->ignore_status_mask)
				continue;

			uart_insert_char(port, lsr, SC16IS7XX_LSR_OE_BIT, ch,
					 flag);
		}
		rxlen -= bytes_read;
	}

	tty_flip_buffer_push(&port->state->port);
}

static void sc16is7xx_handle_tx(struct uart_port *port)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int txlen, to_send, i;

	if (unlikely(port->x_char)) {
		sc16is7xx_port_write(port, SC16IS7XX_THR_REG, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return;

	/* Get length of data pending in circular buffer */
	to_send = uart_circ_chars_pending(xmit);
	if (likely(to_send)) {
		/* Limit to size of TX FIFO */
		txlen = sc16is7xx_port_read(port, SC16IS7XX_TXLVL_REG);
		if (txlen > SC16IS7XX_FIFO_SIZE) {
			dev_err_ratelimited(port->dev,
				"chip reports %d free bytes in TX fifo, but it only has %d",
				txlen, SC16IS7XX_FIFO_SIZE);
			txlen = 0;
		}
		to_send = (to_send > txlen) ? txlen : to_send;

		/* Add data to send */
		port->icount.tx += to_send;

		/* Convert to linear buffer */
		for (i = 0; i < to_send; ++i) {
			s->buf[i] = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		}

		sc16is7xx_fifo_write(port, to_send);
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static bool sc16is7xx_port_irq(struct sc16is7xx_port *s, int portno)
{
	struct uart_port *port = &s->p[portno].port;

	do {
		unsigned int iir, rxlen;

		iir = sc16is7xx_port_read(port, SC16IS7XX_IIR_REG);
		if (iir & SC16IS7XX_IIR_NO_INT_BIT)
			return false;

		iir &= SC16IS7XX_IIR_ID_MASK;

		switch (iir) {
		case SC16IS7XX_IIR_RDI_SRC:
		case SC16IS7XX_IIR_RLSE_SRC:
		case SC16IS7XX_IIR_RTOI_SRC:
		case SC16IS7XX_IIR_XOFFI_SRC:
			rxlen = sc16is7xx_port_read(port, SC16IS7XX_RXLVL_REG);
			if (rxlen)
				sc16is7xx_handle_rx(port, rxlen, iir);
			break;
		case SC16IS7XX_IIR_THRI_SRC:
			sc16is7xx_handle_tx(port);
			break;
		default:
			dev_err_ratelimited(port->dev,
					    "ttySC%i: Unexpected interrupt: %x",
					    port->line, iir);
			break;
		}
	} while (0);
	return true;
}

static void sc16is7xx_ist(struct kthread_work *ws)
{
	struct sc16is7xx_port *s = to_sc16is7xx_port(ws, irq_work);

	mutex_lock(&s->efr_lock);

	while (1) {
		bool keep_polling = false;
		int i;

		for (i = 0; i < s->devtype->nr_uart; ++i)
			keep_polling |= sc16is7xx_port_irq(s, i);
		if (!keep_polling)
			break;
	}

	mutex_unlock(&s->efr_lock);
}

static irqreturn_t sc16is7xx_irq(int irq, void *dev_id)
{
	struct sc16is7xx_port *s = (struct sc16is7xx_port *)dev_id;

	kthread_queue_work(&s->kworker, &s->irq_work);

	return IRQ_HANDLED;
}

static void sc16is7xx_tx_proc(struct kthread_work *ws)
{
	struct uart_port *port = &(to_sc16is7xx_one(ws, tx_work)->port);

	if ((port->rs485.flags & SER_RS485_ENABLED) &&
	    (port->rs485.delay_rts_before_send > 0))
		msleep(port->rs485.delay_rts_before_send);

	sc16is7xx_handle_tx(port);
}

static void sc16is7xx_reconf_rs485(struct uart_port *port)
{
	const u32 mask = SC16IS7XX_EFCR_AUTO_RS485_BIT |
			 SC16IS7XX_EFCR_RTS_INVERT_BIT;
	u32 efcr = 0;
	struct serial_rs485 *rs485 = &port->rs485;
	unsigned long irqflags;

	spin_lock_irqsave(&port->lock, irqflags);
	if (rs485->flags & SER_RS485_ENABLED) {
		efcr |=	SC16IS7XX_EFCR_AUTO_RS485_BIT;

		if (rs485->flags & SER_RS485_RTS_AFTER_SEND)
			efcr |= SC16IS7XX_EFCR_RTS_INVERT_BIT;
	}
	spin_unlock_irqrestore(&port->lock, irqflags);

	sc16is7xx_port_update(port, SC16IS7XX_EFCR_REG, mask, efcr);
}

static void sc16is7xx_reg_proc(struct kthread_work *ws)
{
	struct sc16is7xx_one *one = to_sc16is7xx_one(ws, reg_work);
	struct sc16is7xx_one_config config;
	unsigned long irqflags;

	spin_lock_irqsave(&one->port.lock, irqflags);
	config = one->config;
	memset(&one->config, 0, sizeof(one->config));
	spin_unlock_irqrestore(&one->port.lock, irqflags);

	if (config.flags & SC16IS7XX_RECONF_MD) {
		sc16is7xx_port_update(&one->port, SC16IS7XX_MCR_REG,
				      SC16IS7XX_MCR_LOOP_BIT,
				      (one->port.mctrl & TIOCM_LOOP) ?
				      SC16IS7XX_MCR_LOOP_BIT : 0);
		sc16is7xx_port_update(&one->port, SC16IS7XX_MCR_REG,
				      SC16IS7XX_MCR_RTS_BIT,
				      (one->port.mctrl & TIOCM_RTS) ?
				      SC16IS7XX_MCR_RTS_BIT : 0);
		sc16is7xx_port_update(&one->port, SC16IS7XX_MCR_REG,
				      SC16IS7XX_MCR_DTR_BIT,
				      (one->port.mctrl & TIOCM_DTR) ?
				      SC16IS7XX_MCR_DTR_BIT : 0);
	}
	if (config.flags & SC16IS7XX_RECONF_IER)
		sc16is7xx_port_update(&one->port, SC16IS7XX_IER_REG,
				      config.ier_clear, 0);

	if (config.flags & SC16IS7XX_RECONF_RS485)
		sc16is7xx_reconf_rs485(&one->port);
}

static void sc16is7xx_ier_clear(struct uart_port *port, u8 bit)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	struct sc16is7xx_one *one = to_sc16is7xx_one(port, port);

	one->config.flags |= SC16IS7XX_RECONF_IER;
	one->config.ier_clear |= bit;
	kthread_queue_work(&s->kworker, &one->reg_work);
}

static void sc16is7xx_stop_tx(struct uart_port *port)
{
	sc16is7xx_ier_clear(port, SC16IS7XX_IER_THRI_BIT);
}

static void sc16is7xx_stop_rx(struct uart_port *port)
{
	sc16is7xx_ier_clear(port, SC16IS7XX_IER_RDI_BIT);
}

static void sc16is7xx_start_tx(struct uart_port *port)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	struct sc16is7xx_one *one = to_sc16is7xx_one(port, port);

	kthread_queue_work(&s->kworker, &one->tx_work);
}

static unsigned int sc16is7xx_tx_empty(struct uart_port *port)
{
	unsigned int lsr;

	lsr = sc16is7xx_port_read(port, SC16IS7XX_LSR_REG);

	return (lsr & SC16IS7XX_LSR_TEMT_BIT) ? TIOCSER_TEMT : 0;
}

static unsigned int sc16is7xx_get_mctrl(struct uart_port *port)
{
	/* DCD and DSR are not wired and CTS/RTS is handled automatically
	 * so just indicate DSR and CAR asserted
	 */
	return TIOCM_DSR | TIOCM_CAR;
}

static void sc16is7xx_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	struct sc16is7xx_one *one = to_sc16is7xx_one(port, port);

	one->config.flags |= SC16IS7XX_RECONF_MD;
	kthread_queue_work(&s->kworker, &one->reg_work);
}

static void sc16is7xx_break_ctl(struct uart_port *port, int break_state)
{
	sc16is7xx_port_update(port, SC16IS7XX_LCR_REG,
			      SC16IS7XX_LCR_TXBREAK_BIT,
			      break_state ? SC16IS7XX_LCR_TXBREAK_BIT : 0);
}

static void sc16is7xx_set_termios(struct uart_port *port,
				  struct ktermios *termios,
				  struct ktermios *old)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	unsigned int lcr, flow = 0;
	int baud;

	/* Mask termios capabilities we don't support */
	termios->c_cflag &= ~CMSPAR;

	/* Word size */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr = SC16IS7XX_LCR_WORD_LEN_5;
		break;
	case CS6:
		lcr = SC16IS7XX_LCR_WORD_LEN_6;
		break;
	case CS7:
		lcr = SC16IS7XX_LCR_WORD_LEN_7;
		break;
	case CS8:
		lcr = SC16IS7XX_LCR_WORD_LEN_8;
		break;
	default:
		lcr = SC16IS7XX_LCR_WORD_LEN_8;
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
		break;
	}

	/* Parity */
	if (termios->c_cflag & PARENB) {
		lcr |= SC16IS7XX_LCR_PARITY_BIT;
		if (!(termios->c_cflag & PARODD))
			lcr |= SC16IS7XX_LCR_EVENPARITY_BIT;
	}

	/* Stop bits */
	if (termios->c_cflag & CSTOPB)
		lcr |= SC16IS7XX_LCR_STOPLEN_BIT; /* 2 stops */

	/* Set read status mask */
	port->read_status_mask = SC16IS7XX_LSR_OE_BIT;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= SC16IS7XX_LSR_PE_BIT |
					  SC16IS7XX_LSR_FE_BIT;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= SC16IS7XX_LSR_BI_BIT;

	/* Set status ignore mask */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNBRK)
		port->ignore_status_mask |= SC16IS7XX_LSR_BI_BIT;
	if (!(termios->c_cflag & CREAD))
		port->ignore_status_mask |= SC16IS7XX_LSR_BRK_ERROR_MASK;

	/* As above, claim the mutex while accessing the EFR. */
	mutex_lock(&s->efr_lock);

	sc16is7xx_port_write(port, SC16IS7XX_LCR_REG,
			     SC16IS7XX_LCR_CONF_MODE_B);

	/* Configure flow control */
	regcache_cache_bypass(s->regmap, true);
	sc16is7xx_port_write(port, SC16IS7XX_XON1_REG, termios->c_cc[VSTART]);
	sc16is7xx_port_write(port, SC16IS7XX_XOFF1_REG, termios->c_cc[VSTOP]);
	if (termios->c_cflag & CRTSCTS)
		flow |= SC16IS7XX_EFR_AUTOCTS_BIT |
			SC16IS7XX_EFR_AUTORTS_BIT;
	if (termios->c_iflag & IXON)
		flow |= SC16IS7XX_EFR_SWFLOW3_BIT;
	if (termios->c_iflag & IXOFF)
		flow |= SC16IS7XX_EFR_SWFLOW1_BIT;

	sc16is7xx_port_write(port, SC16IS7XX_EFR_REG, flow);
	regcache_cache_bypass(s->regmap, false);

	/* Update LCR register */
	sc16is7xx_port_write(port, SC16IS7XX_LCR_REG, lcr);

	mutex_unlock(&s->efr_lock);

	/* Get baud rate generator configuration */
	baud = uart_get_baud_rate(port, termios, old,
				  port->uartclk / 16 / 4 / 0xffff,
				  port->uartclk / 16);

	/* Setup baudrate generator */
	baud = sc16is7xx_set_baud(port, baud);

	/* Update timeout according to new baud rate */
	uart_update_timeout(port, termios->c_cflag, baud);
}

static int sc16is7xx_config_rs485(struct uart_port *port,
				  struct serial_rs485 *rs485)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	struct sc16is7xx_one *one = to_sc16is7xx_one(port, port);

	if (rs485->flags & SER_RS485_ENABLED) {
		bool rts_during_rx, rts_during_tx;

		rts_during_rx = rs485->flags & SER_RS485_RTS_AFTER_SEND;
		rts_during_tx = rs485->flags & SER_RS485_RTS_ON_SEND;

		if (rts_during_rx == rts_during_tx)
			dev_err(port->dev,
				"unsupported RTS signalling on_send:%d after_send:%d - exactly one of RS485 RTS flags should be set\n",
				rts_during_tx, rts_during_rx);

		/*
		 * RTS signal is handled by HW, it's timing can't be influenced.
		 * However, it's sometimes useful to delay TX even without RTS
		 * control therefore we try to handle .delay_rts_before_send.
		 */
		if (rs485->delay_rts_after_send)
			return -EINVAL;
	}

	port->rs485 = *rs485;
	one->config.flags |= SC16IS7XX_RECONF_RS485;
	kthread_queue_work(&s->kworker, &one->reg_work);

	return 0;
}

static int sc16is7xx_startup(struct uart_port *port)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);
	unsigned int val;

	sc16is7xx_power(port, 1);

	/* Reset FIFOs*/
	val = SC16IS7XX_FCR_RXRESET_BIT | SC16IS7XX_FCR_TXRESET_BIT;
	sc16is7xx_port_write(port, SC16IS7XX_FCR_REG, val);
	udelay(5);
	sc16is7xx_port_write(port, SC16IS7XX_FCR_REG,
			     SC16IS7XX_FCR_FIFO_BIT);

	/* Enable EFR */
	sc16is7xx_port_write(port, SC16IS7XX_LCR_REG,
			     SC16IS7XX_LCR_CONF_MODE_B);

	regcache_cache_bypass(s->regmap, true);

	/* Enable write access to enhanced features and internal clock div */
	sc16is7xx_port_write(port, SC16IS7XX_EFR_REG,
			     SC16IS7XX_EFR_ENABLE_BIT);

	/* Enable TCR/TLR */
	sc16is7xx_port_update(port, SC16IS7XX_MCR_REG,
			      SC16IS7XX_MCR_TCRTLR_BIT,
			      SC16IS7XX_MCR_TCRTLR_BIT);

	/* Configure flow control levels */
	/* Flow control halt level 48, resume level 24 */
	sc16is7xx_port_write(port, SC16IS7XX_TCR_REG,
			     SC16IS7XX_TCR_RX_RESUME(24) |
			     SC16IS7XX_TCR_RX_HALT(48));

	regcache_cache_bypass(s->regmap, false);

	/* Now, initialize the UART */
	sc16is7xx_port_write(port, SC16IS7XX_LCR_REG, SC16IS7XX_LCR_WORD_LEN_8);

	/* Enable the Rx and Tx FIFO */
	sc16is7xx_port_update(port, SC16IS7XX_EFCR_REG,
			      SC16IS7XX_EFCR_RXDISABLE_BIT |
			      SC16IS7XX_EFCR_TXDISABLE_BIT,
			      0);

	/* Enable RX, TX interrupts */
	val = SC16IS7XX_IER_RDI_BIT | SC16IS7XX_IER_THRI_BIT;
	sc16is7xx_port_write(port, SC16IS7XX_IER_REG, val);

	return 0;
}

static void sc16is7xx_shutdown(struct uart_port *port)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);

	/* Disable all interrupts */
	sc16is7xx_port_write(port, SC16IS7XX_IER_REG, 0);
	/* Disable TX/RX */
	sc16is7xx_port_update(port, SC16IS7XX_EFCR_REG,
			      SC16IS7XX_EFCR_RXDISABLE_BIT |
			      SC16IS7XX_EFCR_TXDISABLE_BIT,
			      SC16IS7XX_EFCR_RXDISABLE_BIT |
			      SC16IS7XX_EFCR_TXDISABLE_BIT);

	sc16is7xx_power(port, 0);

	kthread_flush_worker(&s->kworker);
}

static const char *sc16is7xx_type(struct uart_port *port)
{
	struct sc16is7xx_port *s = dev_get_drvdata(port->dev);

	return (port->type == PORT_SC16IS7XX) ? s->devtype->name : NULL;
}

static int sc16is7xx_request_port(struct uart_port *port)
{
	/* Do nothing */
	return 0;
}

static void sc16is7xx_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_SC16IS7XX;
}

static int sc16is7xx_verify_port(struct uart_port *port,
				 struct serial_struct *s)
{
	if ((s->type != PORT_UNKNOWN) && (s->type != PORT_SC16IS7XX))
		return -EINVAL;
	if (s->irq != port->irq)
		return -EINVAL;

	return 0;
}

static void sc16is7xx_pm(struct uart_port *port, unsigned int state,
			 unsigned int oldstate)
{
	sc16is7xx_power(port, (state == UART_PM_STATE_ON) ? 1 : 0);
}

static void sc16is7xx_null_void(struct uart_port *port)
{
	/* Do nothing */
}

static const struct uart_ops sc16is7xx_ops = {
	.tx_empty	= sc16is7xx_tx_empty,
	.set_mctrl	= sc16is7xx_set_mctrl,
	.get_mctrl	= sc16is7xx_get_mctrl,
	.stop_tx	= sc16is7xx_stop_tx,
	.start_tx	= sc16is7xx_start_tx,
	.stop_rx	= sc16is7xx_stop_rx,
	.break_ctl	= sc16is7xx_break_ctl,
	.startup	= sc16is7xx_startup,
	.shutdown	= sc16is7xx_shutdown,
	.set_termios	= sc16is7xx_set_termios,
	.type		= sc16is7xx_type,
	.request_port	= sc16is7xx_request_port,
	.release_port	= sc16is7xx_null_void,
	.config_port	= sc16is7xx_config_port,
	.verify_port	= sc16is7xx_verify_port,
	.pm		= sc16is7xx_pm,
};

#ifdef CONFIG_GPIOLIB
static int sc16is7xx_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	unsigned int val;
	struct sc16is7xx_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[0].port;

	val = sc16is7xx_port_read(port, SC16IS7XX_IOSTATE_REG);

	return !!(val & BIT(offset));
}

static void sc16is7xx_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct sc16is7xx_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[0].port;

	sc16is7xx_port_update(port, SC16IS7XX_IOSTATE_REG, BIT(offset),
			      val ? BIT(offset) : 0);
}

static int sc16is7xx_gpio_direction_input(struct gpio_chip *chip,
					  unsigned offset)
{
	struct sc16is7xx_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[0].port;

	sc16is7xx_port_update(port, SC16IS7XX_IODIR_REG, BIT(offset), 0);

	return 0;
}

static int sc16is7xx_gpio_direction_output(struct gpio_chip *chip,
					   unsigned offset, int val)
{
	struct sc16is7xx_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[0].port;
	u8 state = sc16is7xx_port_read(port, SC16IS7XX_IOSTATE_REG);

	if (val)
		state |= BIT(offset);
	else
		state &= ~BIT(offset);
	sc16is7xx_port_write(port, SC16IS7XX_IOSTATE_REG, state);
	sc16is7xx_port_update(port, SC16IS7XX_IODIR_REG, BIT(offset),
			      BIT(offset));

	return 0;
}
#endif

static int sc16is7xx_probe(struct device *dev,
			   const struct sc16is7xx_devtype *devtype,
			   struct regmap *regmap, int irq, unsigned long flags)
{
	struct sched_param sched_param = { .sched_priority = MAX_RT_PRIO / 2 };
	unsigned long freq, *pfreq = dev_get_platdata(dev);
	int i, ret;
	struct sc16is7xx_port *s;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Alloc port structure */
	s = devm_kzalloc(dev, sizeof(*s) +
			 sizeof(struct sc16is7xx_one) * devtype->nr_uart,
			 GFP_KERNEL);
	if (!s) {
		dev_err(dev, "Error allocating port structure\n");
		return -ENOMEM;
	}

	s->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(s->clk)) {
		if (pfreq)
			freq = *pfreq;
		else
			return PTR_ERR(s->clk);
	} else {
		ret = clk_prepare_enable(s->clk);
		if (ret)
			return ret;

		freq = clk_get_rate(s->clk);
	}

	s->regmap = regmap;
	s->devtype = devtype;
	dev_set_drvdata(dev, s);
	mutex_init(&s->efr_lock);

	kthread_init_worker(&s->kworker);
	kthread_init_work(&s->irq_work, sc16is7xx_ist);
	s->kworker_task = kthread_run(kthread_worker_fn, &s->kworker,
				      "sc16is7xx");
	if (IS_ERR(s->kworker_task)) {
		ret = PTR_ERR(s->kworker_task);
		goto out_clk;
	}
	sched_setscheduler(s->kworker_task, SCHED_FIFO, &sched_param);

#ifdef CONFIG_GPIOLIB
	if (devtype->nr_gpio) {
		/* Setup GPIO cotroller */
		s->gpio.owner		 = THIS_MODULE;
		s->gpio.parent		 = dev;
		s->gpio.label		 = dev_name(dev);
		s->gpio.direction_input	 = sc16is7xx_gpio_direction_input;
		s->gpio.get		 = sc16is7xx_gpio_get;
		s->gpio.direction_output = sc16is7xx_gpio_direction_output;
		s->gpio.set		 = sc16is7xx_gpio_set;
		s->gpio.base		 = -1;
		s->gpio.ngpio		 = devtype->nr_gpio;
		s->gpio.can_sleep	 = 1;
		ret = gpiochip_add_data(&s->gpio, s);
		if (ret)
			goto out_thread;
	}
#endif

	/* reset device, purging any pending irq / data */
	regmap_write(s->regmap, SC16IS7XX_IOCONTROL_REG << SC16IS7XX_REG_SHIFT,
			SC16IS7XX_IOCONTROL_SRESET_BIT);

	for (i = 0; i < devtype->nr_uart; ++i) {
		s->p[i].line		= i;
		/* Initialize port data */
		s->p[i].port.dev	= dev;
		s->p[i].port.irq	= irq;
		s->p[i].port.type	= PORT_SC16IS7XX;
		s->p[i].port.fifosize	= SC16IS7XX_FIFO_SIZE;
		s->p[i].port.flags	= UPF_FIXED_TYPE | UPF_LOW_LATENCY;
		s->p[i].port.iotype	= UPIO_PORT;
		s->p[i].port.uartclk	= freq;
		s->p[i].port.rs485_config = sc16is7xx_config_rs485;
		s->p[i].port.ops	= &sc16is7xx_ops;
		s->p[i].port.line	= sc16is7xx_alloc_line();
		if (s->p[i].port.line >= SC16IS7XX_MAX_DEVS) {
			ret = -ENOMEM;
			goto out_ports;
		}

		/* Disable all interrupts */
		sc16is7xx_port_write(&s->p[i].port, SC16IS7XX_IER_REG, 0);
		/* Disable TX/RX */
		sc16is7xx_port_write(&s->p[i].port, SC16IS7XX_EFCR_REG,
				     SC16IS7XX_EFCR_RXDISABLE_BIT |
				     SC16IS7XX_EFCR_TXDISABLE_BIT);
		/* Initialize kthread work structs */
		kthread_init_work(&s->p[i].tx_work, sc16is7xx_tx_proc);
		kthread_init_work(&s->p[i].reg_work, sc16is7xx_reg_proc);
		/* Register port */
		uart_add_one_port(&sc16is7xx_uart, &s->p[i].port);

		/* Enable EFR */
		sc16is7xx_port_write(&s->p[i].port, SC16IS7XX_LCR_REG,
				     SC16IS7XX_LCR_CONF_MODE_B);

		regcache_cache_bypass(s->regmap, true);

		/* Enable write access to enhanced features */
		sc16is7xx_port_write(&s->p[i].port, SC16IS7XX_EFR_REG,
				     SC16IS7XX_EFR_ENABLE_BIT);

		regcache_cache_bypass(s->regmap, false);

		/* Restore access to general registers */
		sc16is7xx_port_write(&s->p[i].port, SC16IS7XX_LCR_REG, 0x00);

		/* Go to suspend mode */
		sc16is7xx_power(&s->p[i].port, 0);
	}

	/* Setup interrupt */
	ret = devm_request_irq(dev, irq, sc16is7xx_irq,
			       flags, dev_name(dev), s);
	if (!ret)
		return 0;

out_ports:
	for (i--; i >= 0; i--) {
		uart_remove_one_port(&sc16is7xx_uart, &s->p[i].port);
		clear_bit(s->p[i].port.line, &sc16is7xx_lines);
	}

#ifdef CONFIG_GPIOLIB
	if (devtype->nr_gpio)
		gpiochip_remove(&s->gpio);

out_thread:
#endif
	kthread_stop(s->kworker_task);

out_clk:
	if (!IS_ERR(s->clk))
		clk_disable_unprepare(s->clk);

	return ret;
}

static int sc16is7xx_remove(struct device *dev)
{
	struct sc16is7xx_port *s = dev_get_drvdata(dev);
	int i;

#ifdef CONFIG_GPIOLIB
	if (s->devtype->nr_gpio)
		gpiochip_remove(&s->gpio);
#endif

	for (i = 0; i < s->devtype->nr_uart; i++) {
		uart_remove_one_port(&sc16is7xx_uart, &s->p[i].port);
		clear_bit(s->p[i].port.line, &sc16is7xx_lines);
		sc16is7xx_power(&s->p[i].port, 0);
	}

	kthread_flush_worker(&s->kworker);
	kthread_stop(s->kworker_task);

	if (!IS_ERR(s->clk))
		clk_disable_unprepare(s->clk);

	return 0;
}

static const struct of_device_id __maybe_unused sc16is7xx_dt_ids[] = {
	{ .compatible = "nxp,sc16is740",	.data = &sc16is74x_devtype, },
	{ .compatible = "nxp,sc16is741",	.data = &sc16is74x_devtype, },
	{ .compatible = "nxp,sc16is750",	.data = &sc16is750_devtype, },
	{ .compatible = "nxp,sc16is752",	.data = &sc16is752_devtype, },
	{ .compatible = "nxp,sc16is760",	.data = &sc16is760_devtype, },
	{ .compatible = "nxp,sc16is762",	.data = &sc16is762_devtype, },
	{ }
};
MODULE_DEVICE_TABLE(of, sc16is7xx_dt_ids);

static struct regmap_config regcfg = {
	.reg_bits = 7,
	.pad_bits = 1,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = sc16is7xx_regmap_volatile,
	.precious_reg = sc16is7xx_regmap_precious,
};

#ifdef CONFIG_SERIAL_SC16IS7XX_SPI
static int sc16is7xx_spi_probe(struct spi_device *spi)
{
	const struct sc16is7xx_devtype *devtype;
	unsigned long flags = 0;
	struct regmap *regmap;
	int ret;

	/* Setup SPI bus */
	spi->bits_per_word	= 8;
	/* only supports mode 0 on SC16IS762 */
	spi->mode		= spi->mode ? : SPI_MODE_0;
	spi->max_speed_hz	= spi->max_speed_hz ? : 15000000;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	if (spi->dev.of_node) {
		const struct of_device_id *of_id =
			of_match_device(sc16is7xx_dt_ids, &spi->dev);

		if (!of_id)
			return -ENODEV;

		devtype = (struct sc16is7xx_devtype *)of_id->data;
	} else {
		const struct spi_device_id *id_entry = spi_get_device_id(spi);

		devtype = (struct sc16is7xx_devtype *)id_entry->driver_data;
		flags = IRQF_TRIGGER_FALLING;
	}

	regcfg.max_register = (0xf << SC16IS7XX_REG_SHIFT) |
			      (devtype->nr_uart - 1);
	regmap = devm_regmap_init_spi(spi, &regcfg);

	return sc16is7xx_probe(&spi->dev, devtype, regmap, spi->irq, flags);
}

static int sc16is7xx_spi_remove(struct spi_device *spi)
{
	return sc16is7xx_remove(&spi->dev);
}

static const struct spi_device_id sc16is7xx_spi_id_table[] = {
	{ "sc16is74x",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is740",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is741",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is750",	(kernel_ulong_t)&sc16is750_devtype, },
	{ "sc16is752",	(kernel_ulong_t)&sc16is752_devtype, },
	{ "sc16is760",	(kernel_ulong_t)&sc16is760_devtype, },
	{ "sc16is762",	(kernel_ulong_t)&sc16is762_devtype, },
	{ }
};

MODULE_DEVICE_TABLE(spi, sc16is7xx_spi_id_table);

static struct spi_driver sc16is7xx_spi_uart_driver = {
	.driver = {
		.name		= SC16IS7XX_NAME,
		.of_match_table	= of_match_ptr(sc16is7xx_dt_ids),
	},
	.probe		= sc16is7xx_spi_probe,
	.remove		= sc16is7xx_spi_remove,
	.id_table	= sc16is7xx_spi_id_table,
};

MODULE_ALIAS("spi:sc16is7xx");
#endif

#ifdef CONFIG_SERIAL_SC16IS7XX_I2C
static int sc16is7xx_i2c_probe(struct i2c_client *i2c,
			       const struct i2c_device_id *id)
{
	const struct sc16is7xx_devtype *devtype;
	unsigned long flags = 0;
	struct regmap *regmap;

	if (i2c->dev.of_node) {
		const struct of_device_id *of_id =
				of_match_device(sc16is7xx_dt_ids, &i2c->dev);

		if (!of_id)
			return -ENODEV;

		devtype = (struct sc16is7xx_devtype *)of_id->data;
	} else {
		devtype = (struct sc16is7xx_devtype *)id->driver_data;
		flags = IRQF_TRIGGER_FALLING;
	}

	regcfg.max_register = (0xf << SC16IS7XX_REG_SHIFT) |
			      (devtype->nr_uart - 1);
	regmap = devm_regmap_init_i2c(i2c, &regcfg);

	return sc16is7xx_probe(&i2c->dev, devtype, regmap, i2c->irq, flags);
}

static int sc16is7xx_i2c_remove(struct i2c_client *client)
{
	return sc16is7xx_remove(&client->dev);
}

static const struct i2c_device_id sc16is7xx_i2c_id_table[] = {
	{ "sc16is74x",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is740",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is741",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is750",	(kernel_ulong_t)&sc16is750_devtype, },
	{ "sc16is752",	(kernel_ulong_t)&sc16is752_devtype, },
	{ "sc16is760",	(kernel_ulong_t)&sc16is760_devtype, },
	{ "sc16is762",	(kernel_ulong_t)&sc16is762_devtype, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc16is7xx_i2c_id_table);

static struct i2c_driver sc16is7xx_i2c_uart_driver = {
	.driver = {
		.name		= SC16IS7XX_NAME,
		.of_match_table	= of_match_ptr(sc16is7xx_dt_ids),
	},
	.probe		= sc16is7xx_i2c_probe,
	.remove		= sc16is7xx_i2c_remove,
	.id_table	= sc16is7xx_i2c_id_table,
};

#endif

static int __init sc16is7xx_init(void)
{
	int ret;

	ret = uart_register_driver(&sc16is7xx_uart);
	if (ret) {
		pr_err("Registering UART driver failed\n");
		return ret;
	}

#ifdef CONFIG_SERIAL_SC16IS7XX_I2C
	ret = i2c_add_driver(&sc16is7xx_i2c_uart_driver);
	if (ret < 0) {
		pr_err("failed to init sc16is7xx i2c --> %d\n", ret);
		goto err_i2c;
	}
#endif

#ifdef CONFIG_SERIAL_SC16IS7XX_SPI
	ret = spi_register_driver(&sc16is7xx_spi_uart_driver);
	if (ret < 0) {
		pr_err("failed to init sc16is7xx spi --> %d\n", ret);
		goto err_spi;
	}
#endif
	return ret;

#ifdef CONFIG_SERIAL_SC16IS7XX_SPI
err_spi:
#ifdef CONFIG_SERIAL_SC16IS7XX_I2C
	i2c_del_driver(&sc16is7xx_i2c_uart_driver);
#endif
#endif
err_i2c:
	uart_unregister_driver(&sc16is7xx_uart);
	return ret;
}
module_init(sc16is7xx_init);

static void __exit sc16is7xx_exit(void)
{
#ifdef CONFIG_SERIAL_SC16IS7XX_I2C
	i2c_del_driver(&sc16is7xx_i2c_uart_driver);
#endif

#ifdef CONFIG_SERIAL_SC16IS7XX_SPI
	spi_unregister_driver(&sc16is7xx_spi_uart_driver);
#endif
	uart_unregister_driver(&sc16is7xx_uart);
}
module_exit(sc16is7xx_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Ringle <jringle@gridpoint.com>");
MODULE_DESCRIPTION("SC16IS7XX serial driver");
