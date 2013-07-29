/*
 *  Maxim (Dallas) MAX3107/8/9, MAX14830 serial driver
 *
 *  Copyright (C) 2012-2013 Alexander Shiyan <shc_work@mail.ru>
 *
 *  Based on max3100.c, by Christian Pellegrin <chripell@evolware.org>
 *  Based on max3110.c, by Feng Tang <feng.tang@intel.com>
 *  Based on max3107.c, by Aavamobile
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/bitops.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>

#include <linux/platform_data/max310x.h>

#define MAX310X_NAME			"max310x"
#define MAX310X_MAJOR			204
#define MAX310X_MINOR			209

/* MAX310X register definitions */
#define MAX310X_RHR_REG			(0x00) /* RX FIFO */
#define MAX310X_THR_REG			(0x00) /* TX FIFO */
#define MAX310X_IRQEN_REG		(0x01) /* IRQ enable */
#define MAX310X_IRQSTS_REG		(0x02) /* IRQ status */
#define MAX310X_LSR_IRQEN_REG		(0x03) /* LSR IRQ enable */
#define MAX310X_LSR_IRQSTS_REG		(0x04) /* LSR IRQ status */
#define MAX310X_REG_05			(0x05)
#define MAX310X_SPCHR_IRQEN_REG		MAX310X_REG_05 /* Special char IRQ en */
#define MAX310X_SPCHR_IRQSTS_REG	(0x06) /* Special char IRQ status */
#define MAX310X_STS_IRQEN_REG		(0x07) /* Status IRQ enable */
#define MAX310X_STS_IRQSTS_REG		(0x08) /* Status IRQ status */
#define MAX310X_MODE1_REG		(0x09) /* MODE1 */
#define MAX310X_MODE2_REG		(0x0a) /* MODE2 */
#define MAX310X_LCR_REG			(0x0b) /* LCR */
#define MAX310X_RXTO_REG		(0x0c) /* RX timeout */
#define MAX310X_HDPIXDELAY_REG		(0x0d) /* Auto transceiver delays */
#define MAX310X_IRDA_REG		(0x0e) /* IRDA settings */
#define MAX310X_FLOWLVL_REG		(0x0f) /* Flow control levels */
#define MAX310X_FIFOTRIGLVL_REG		(0x10) /* FIFO IRQ trigger levels */
#define MAX310X_TXFIFOLVL_REG		(0x11) /* TX FIFO level */
#define MAX310X_RXFIFOLVL_REG		(0x12) /* RX FIFO level */
#define MAX310X_FLOWCTRL_REG		(0x13) /* Flow control */
#define MAX310X_XON1_REG		(0x14) /* XON1 character */
#define MAX310X_XON2_REG		(0x15) /* XON2 character */
#define MAX310X_XOFF1_REG		(0x16) /* XOFF1 character */
#define MAX310X_XOFF2_REG		(0x17) /* XOFF2 character */
#define MAX310X_GPIOCFG_REG		(0x18) /* GPIO config */
#define MAX310X_GPIODATA_REG		(0x19) /* GPIO data */
#define MAX310X_PLLCFG_REG		(0x1a) /* PLL config */
#define MAX310X_BRGCFG_REG		(0x1b) /* Baud rate generator conf */
#define MAX310X_BRGDIVLSB_REG		(0x1c) /* Baud rate divisor LSB */
#define MAX310X_BRGDIVMSB_REG		(0x1d) /* Baud rate divisor MSB */
#define MAX310X_CLKSRC_REG		(0x1e) /* Clock source */
#define MAX310X_REG_1F			(0x1f)

#define MAX310X_REVID_REG		MAX310X_REG_1F /* Revision ID */

#define MAX310X_GLOBALIRQ_REG		MAX310X_REG_1F /* Global IRQ (RO) */
#define MAX310X_GLOBALCMD_REG		MAX310X_REG_1F /* Global Command (WO) */

/* Extended registers */
#define MAX310X_REVID_EXTREG		MAX310X_REG_05 /* Revision ID */

/* IRQ register bits */
#define MAX310X_IRQ_LSR_BIT		(1 << 0) /* LSR interrupt */
#define MAX310X_IRQ_SPCHR_BIT		(1 << 1) /* Special char interrupt */
#define MAX310X_IRQ_STS_BIT		(1 << 2) /* Status interrupt */
#define MAX310X_IRQ_RXFIFO_BIT		(1 << 3) /* RX FIFO interrupt */
#define MAX310X_IRQ_TXFIFO_BIT		(1 << 4) /* TX FIFO interrupt */
#define MAX310X_IRQ_TXEMPTY_BIT		(1 << 5) /* TX FIFO empty interrupt */
#define MAX310X_IRQ_RXEMPTY_BIT		(1 << 6) /* RX FIFO empty interrupt */
#define MAX310X_IRQ_CTS_BIT		(1 << 7) /* CTS interrupt */

/* LSR register bits */
#define MAX310X_LSR_RXTO_BIT		(1 << 0) /* RX timeout */
#define MAX310X_LSR_RXOVR_BIT		(1 << 1) /* RX overrun */
#define MAX310X_LSR_RXPAR_BIT		(1 << 2) /* RX parity error */
#define MAX310X_LSR_FRERR_BIT		(1 << 3) /* Frame error */
#define MAX310X_LSR_RXBRK_BIT		(1 << 4) /* RX break */
#define MAX310X_LSR_RXNOISE_BIT		(1 << 5) /* RX noise */
#define MAX310X_LSR_CTS_BIT		(1 << 7) /* CTS pin state */

/* Special character register bits */
#define MAX310X_SPCHR_XON1_BIT		(1 << 0) /* XON1 character */
#define MAX310X_SPCHR_XON2_BIT		(1 << 1) /* XON2 character */
#define MAX310X_SPCHR_XOFF1_BIT		(1 << 2) /* XOFF1 character */
#define MAX310X_SPCHR_XOFF2_BIT		(1 << 3) /* XOFF2 character */
#define MAX310X_SPCHR_BREAK_BIT		(1 << 4) /* RX break */
#define MAX310X_SPCHR_MULTIDROP_BIT	(1 << 5) /* 9-bit multidrop addr char */

/* Status register bits */
#define MAX310X_STS_GPIO0_BIT		(1 << 0) /* GPIO 0 interrupt */
#define MAX310X_STS_GPIO1_BIT		(1 << 1) /* GPIO 1 interrupt */
#define MAX310X_STS_GPIO2_BIT		(1 << 2) /* GPIO 2 interrupt */
#define MAX310X_STS_GPIO3_BIT		(1 << 3) /* GPIO 3 interrupt */
#define MAX310X_STS_CLKREADY_BIT	(1 << 5) /* Clock ready */
#define MAX310X_STS_SLEEP_BIT		(1 << 6) /* Sleep interrupt */

/* MODE1 register bits */
#define MAX310X_MODE1_RXDIS_BIT		(1 << 0) /* RX disable */
#define MAX310X_MODE1_TXDIS_BIT		(1 << 1) /* TX disable */
#define MAX310X_MODE1_TXHIZ_BIT		(1 << 2) /* TX pin three-state */
#define MAX310X_MODE1_RTSHIZ_BIT	(1 << 3) /* RTS pin three-state */
#define MAX310X_MODE1_TRNSCVCTRL_BIT	(1 << 4) /* Transceiver ctrl enable */
#define MAX310X_MODE1_FORCESLEEP_BIT	(1 << 5) /* Force sleep mode */
#define MAX310X_MODE1_AUTOSLEEP_BIT	(1 << 6) /* Auto sleep enable */
#define MAX310X_MODE1_IRQSEL_BIT	(1 << 7) /* IRQ pin enable */

/* MODE2 register bits */
#define MAX310X_MODE2_RST_BIT		(1 << 0) /* Chip reset */
#define MAX310X_MODE2_FIFORST_BIT	(1 << 1) /* FIFO reset */
#define MAX310X_MODE2_RXTRIGINV_BIT	(1 << 2) /* RX FIFO INT invert */
#define MAX310X_MODE2_RXEMPTINV_BIT	(1 << 3) /* RX FIFO empty INT invert */
#define MAX310X_MODE2_SPCHR_BIT		(1 << 4) /* Special chr detect enable */
#define MAX310X_MODE2_LOOPBACK_BIT	(1 << 5) /* Internal loopback enable */
#define MAX310X_MODE2_MULTIDROP_BIT	(1 << 6) /* 9-bit multidrop enable */
#define MAX310X_MODE2_ECHOSUPR_BIT	(1 << 7) /* ECHO suppression enable */

/* LCR register bits */
#define MAX310X_LCR_LENGTH0_BIT		(1 << 0) /* Word length bit 0 */
#define MAX310X_LCR_LENGTH1_BIT		(1 << 1) /* Word length bit 1
						  *
						  * Word length bits table:
						  * 00 -> 5 bit words
						  * 01 -> 6 bit words
						  * 10 -> 7 bit words
						  * 11 -> 8 bit words
						  */
#define MAX310X_LCR_STOPLEN_BIT		(1 << 2) /* STOP length bit
						  *
						  * STOP length bit table:
						  * 0 -> 1 stop bit
						  * 1 -> 1-1.5 stop bits if
						  *      word length is 5,
						  *      2 stop bits otherwise
						  */
#define MAX310X_LCR_PARITY_BIT		(1 << 3) /* Parity bit enable */
#define MAX310X_LCR_EVENPARITY_BIT	(1 << 4) /* Even parity bit enable */
#define MAX310X_LCR_FORCEPARITY_BIT	(1 << 5) /* 9-bit multidrop parity */
#define MAX310X_LCR_TXBREAK_BIT		(1 << 6) /* TX break enable */
#define MAX310X_LCR_RTS_BIT		(1 << 7) /* RTS pin control */
#define MAX310X_LCR_WORD_LEN_5		(0x00)
#define MAX310X_LCR_WORD_LEN_6		(0x01)
#define MAX310X_LCR_WORD_LEN_7		(0x02)
#define MAX310X_LCR_WORD_LEN_8		(0x03)

/* IRDA register bits */
#define MAX310X_IRDA_IRDAEN_BIT		(1 << 0) /* IRDA mode enable */
#define MAX310X_IRDA_SIR_BIT		(1 << 1) /* SIR mode enable */
#define MAX310X_IRDA_SHORTIR_BIT	(1 << 2) /* Short SIR mode enable */
#define MAX310X_IRDA_MIR_BIT		(1 << 3) /* MIR mode enable */
#define MAX310X_IRDA_RXINV_BIT		(1 << 4) /* RX logic inversion enable */
#define MAX310X_IRDA_TXINV_BIT		(1 << 5) /* TX logic inversion enable */

/* Flow control trigger level register masks */
#define MAX310X_FLOWLVL_HALT_MASK	(0x000f) /* Flow control halt level */
#define MAX310X_FLOWLVL_RES_MASK	(0x00f0) /* Flow control resume level */
#define MAX310X_FLOWLVL_HALT(words)	((words / 8) & 0x0f)
#define MAX310X_FLOWLVL_RES(words)	(((words / 8) & 0x0f) << 4)

/* FIFO interrupt trigger level register masks */
#define MAX310X_FIFOTRIGLVL_TX_MASK	(0x0f) /* TX FIFO trigger level */
#define MAX310X_FIFOTRIGLVL_RX_MASK	(0xf0) /* RX FIFO trigger level */
#define MAX310X_FIFOTRIGLVL_TX(words)	((words / 8) & 0x0f)
#define MAX310X_FIFOTRIGLVL_RX(words)	(((words / 8) & 0x0f) << 4)

/* Flow control register bits */
#define MAX310X_FLOWCTRL_AUTORTS_BIT	(1 << 0) /* Auto RTS flow ctrl enable */
#define MAX310X_FLOWCTRL_AUTOCTS_BIT	(1 << 1) /* Auto CTS flow ctrl enable */
#define MAX310X_FLOWCTRL_GPIADDR_BIT	(1 << 2) /* Enables that GPIO inputs
						  * are used in conjunction with
						  * XOFF2 for definition of
						  * special character */
#define MAX310X_FLOWCTRL_SWFLOWEN_BIT	(1 << 3) /* Auto SW flow ctrl enable */
#define MAX310X_FLOWCTRL_SWFLOW0_BIT	(1 << 4) /* SWFLOW bit 0 */
#define MAX310X_FLOWCTRL_SWFLOW1_BIT	(1 << 5) /* SWFLOW bit 1
						  *
						  * SWFLOW bits 1 & 0 table:
						  * 00 -> no transmitter flow
						  *       control
						  * 01 -> receiver compares
						  *       XON2 and XOFF2
						  *       and controls
						  *       transmitter
						  * 10 -> receiver compares
						  *       XON1 and XOFF1
						  *       and controls
						  *       transmitter
						  * 11 -> receiver compares
						  *       XON1, XON2, XOFF1 and
						  *       XOFF2 and controls
						  *       transmitter
						  */
#define MAX310X_FLOWCTRL_SWFLOW2_BIT	(1 << 6) /* SWFLOW bit 2 */
#define MAX310X_FLOWCTRL_SWFLOW3_BIT	(1 << 7) /* SWFLOW bit 3
						  *
						  * SWFLOW bits 3 & 2 table:
						  * 00 -> no received flow
						  *       control
						  * 01 -> transmitter generates
						  *       XON2 and XOFF2
						  * 10 -> transmitter generates
						  *       XON1 and XOFF1
						  * 11 -> transmitter generates
						  *       XON1, XON2, XOFF1 and
						  *       XOFF2
						  */

/* GPIO configuration register bits */
#define MAX310X_GPIOCFG_GP0OUT_BIT	(1 << 0) /* GPIO 0 output enable */
#define MAX310X_GPIOCFG_GP1OUT_BIT	(1 << 1) /* GPIO 1 output enable */
#define MAX310X_GPIOCFG_GP2OUT_BIT	(1 << 2) /* GPIO 2 output enable */
#define MAX310X_GPIOCFG_GP3OUT_BIT	(1 << 3) /* GPIO 3 output enable */
#define MAX310X_GPIOCFG_GP0OD_BIT	(1 << 4) /* GPIO 0 open-drain enable */
#define MAX310X_GPIOCFG_GP1OD_BIT	(1 << 5) /* GPIO 1 open-drain enable */
#define MAX310X_GPIOCFG_GP2OD_BIT	(1 << 6) /* GPIO 2 open-drain enable */
#define MAX310X_GPIOCFG_GP3OD_BIT	(1 << 7) /* GPIO 3 open-drain enable */

/* GPIO DATA register bits */
#define MAX310X_GPIODATA_GP0OUT_BIT	(1 << 0) /* GPIO 0 output value */
#define MAX310X_GPIODATA_GP1OUT_BIT	(1 << 1) /* GPIO 1 output value */
#define MAX310X_GPIODATA_GP2OUT_BIT	(1 << 2) /* GPIO 2 output value */
#define MAX310X_GPIODATA_GP3OUT_BIT	(1 << 3) /* GPIO 3 output value */
#define MAX310X_GPIODATA_GP0IN_BIT	(1 << 4) /* GPIO 0 input value */
#define MAX310X_GPIODATA_GP1IN_BIT	(1 << 5) /* GPIO 1 input value */
#define MAX310X_GPIODATA_GP2IN_BIT	(1 << 6) /* GPIO 2 input value */
#define MAX310X_GPIODATA_GP3IN_BIT	(1 << 7) /* GPIO 3 input value */

/* PLL configuration register masks */
#define MAX310X_PLLCFG_PREDIV_MASK	(0x3f) /* PLL predivision value */
#define MAX310X_PLLCFG_PLLFACTOR_MASK	(0xc0) /* PLL multiplication factor */

/* Baud rate generator configuration register bits */
#define MAX310X_BRGCFG_2XMODE_BIT	(1 << 4) /* Double baud rate */
#define MAX310X_BRGCFG_4XMODE_BIT	(1 << 5) /* Quadruple baud rate */

/* Clock source register bits */
#define MAX310X_CLKSRC_CRYST_BIT	(1 << 1) /* Crystal osc enable */
#define MAX310X_CLKSRC_PLL_BIT		(1 << 2) /* PLL enable */
#define MAX310X_CLKSRC_PLLBYP_BIT	(1 << 3) /* PLL bypass */
#define MAX310X_CLKSRC_EXTCLK_BIT	(1 << 4) /* External clock enable */
#define MAX310X_CLKSRC_CLK2RTS_BIT	(1 << 7) /* Baud clk to RTS pin */

/* Global commands */
#define MAX310X_EXTREG_ENBL		(0xce)
#define MAX310X_EXTREG_DSBL		(0xcd)

/* Misc definitions */
#define MAX310X_FIFO_SIZE		(128)
#define MAX310x_REV_MASK		(0xfc)

/* MAX3107 specific */
#define MAX3107_REV_ID			(0xa0)

/* MAX3109 specific */
#define MAX3109_REV_ID			(0xc0)

/* MAX14830 specific */
#define MAX14830_BRGCFG_CLKDIS_BIT	(1 << 6) /* Clock Disable */
#define MAX14830_REV_ID			(0xb0)

struct max310x_devtype {
	char	name[9];
	int	nr;
	int	(*detect)(struct device *);
	void	(*power)(struct uart_port *, int);
};

struct max310x_one {
	struct uart_port	port;
	struct work_struct	tx_work;
};

struct max310x_port {
	struct uart_driver	uart;
	struct max310x_devtype	*devtype;
	struct regmap		*regmap;
	struct regmap_config	regcfg;
	struct mutex		mutex;
	struct max310x_pdata	*pdata;
	int			gpio_used;
#ifdef CONFIG_GPIOLIB
	struct gpio_chip	gpio;
#endif
	struct max310x_one	p[0];
};

static u8 max310x_port_read(struct uart_port *port, u8 reg)
{
	struct max310x_port *s = dev_get_drvdata(port->dev);
	unsigned int val = 0;

	regmap_read(s->regmap, port->iobase + reg, &val);

	return val;
}

static void max310x_port_write(struct uart_port *port, u8 reg, u8 val)
{
	struct max310x_port *s = dev_get_drvdata(port->dev);

	regmap_write(s->regmap, port->iobase + reg, val);
}

static void max310x_port_update(struct uart_port *port, u8 reg, u8 mask, u8 val)
{
	struct max310x_port *s = dev_get_drvdata(port->dev);

	regmap_update_bits(s->regmap, port->iobase + reg, mask, val);
}

static int max3107_detect(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	unsigned int val = 0;
	int ret;

	ret = regmap_read(s->regmap, MAX310X_REVID_REG, &val);
	if (ret)
		return ret;

	if (((val & MAX310x_REV_MASK) != MAX3107_REV_ID)) {
		dev_err(dev,
			"%s ID 0x%02x does not match\n", s->devtype->name, val);
		return -ENODEV;
	}

	return 0;
}

static int max3108_detect(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	unsigned int val = 0;
	int ret;

	/* MAX3108 have not REV ID register, we just check default value
	 * from clocksource register to make sure everything works.
	 */
	ret = regmap_read(s->regmap, MAX310X_CLKSRC_REG, &val);
	if (ret)
		return ret;

	if (val != (MAX310X_CLKSRC_EXTCLK_BIT | MAX310X_CLKSRC_PLLBYP_BIT)) {
		dev_err(dev, "%s not present\n", s->devtype->name);
		return -ENODEV;
	}

	return 0;
}

static int max3109_detect(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	unsigned int val = 0;
	int ret;

	ret = regmap_read(s->regmap, MAX310X_REVID_REG, &val);
	if (ret)
		return ret;

	if (((val & MAX310x_REV_MASK) != MAX3109_REV_ID)) {
		dev_err(dev,
			"%s ID 0x%02x does not match\n", s->devtype->name, val);
		return -ENODEV;
	}

	return 0;
}

static void max310x_power(struct uart_port *port, int on)
{
	max310x_port_update(port, MAX310X_MODE1_REG,
			    MAX310X_MODE1_FORCESLEEP_BIT,
			    on ? 0 : MAX310X_MODE1_FORCESLEEP_BIT);
	if (on)
		msleep(50);
}

static int max14830_detect(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	unsigned int val = 0;
	int ret;

	ret = regmap_write(s->regmap, MAX310X_GLOBALCMD_REG,
			   MAX310X_EXTREG_ENBL);
	if (ret)
		return ret;
	
	regmap_read(s->regmap, MAX310X_REVID_EXTREG, &val);
	regmap_write(s->regmap, MAX310X_GLOBALCMD_REG, MAX310X_EXTREG_DSBL);
	if (((val & MAX310x_REV_MASK) != MAX14830_REV_ID)) {
		dev_err(dev,
			"%s ID 0x%02x does not match\n", s->devtype->name, val);
		return -ENODEV;
	}

	return 0;
}

static void max14830_power(struct uart_port *port, int on)
{
	max310x_port_update(port, MAX310X_BRGCFG_REG,
			    MAX14830_BRGCFG_CLKDIS_BIT,
			    on ? 0 : MAX14830_BRGCFG_CLKDIS_BIT);
	if (on)
		msleep(50);
}

static const struct max310x_devtype max3107_devtype = {
	.name	= "MAX3107",
	.nr	= 1,
	.detect	= max3107_detect,
	.power	= max310x_power,
};

static const struct max310x_devtype max3108_devtype = {
	.name	= "MAX3108",
	.nr	= 1,
	.detect	= max3108_detect,
	.power	= max310x_power,
};

static const struct max310x_devtype max3109_devtype = {
	.name	= "MAX3109",
	.nr	= 2,
	.detect	= max3109_detect,
	.power	= max310x_power,
};

static const struct max310x_devtype max14830_devtype = {
	.name	= "MAX14830",
	.nr	= 4,
	.detect	= max14830_detect,
	.power	= max14830_power,
};

static bool max310x_reg_writeable(struct device *dev, unsigned int reg)
{
	switch (reg & 0x1f) {
	case MAX310X_IRQSTS_REG:
	case MAX310X_LSR_IRQSTS_REG:
	case MAX310X_SPCHR_IRQSTS_REG:
	case MAX310X_STS_IRQSTS_REG:
	case MAX310X_TXFIFOLVL_REG:
	case MAX310X_RXFIFOLVL_REG:
		return false;
	default:
		break;
	}

	return true;
}

static bool max310x_reg_volatile(struct device *dev, unsigned int reg)
{
	switch (reg & 0x1f) {
	case MAX310X_RHR_REG:
	case MAX310X_IRQSTS_REG:
	case MAX310X_LSR_IRQSTS_REG:
	case MAX310X_SPCHR_IRQSTS_REG:
	case MAX310X_STS_IRQSTS_REG:
	case MAX310X_TXFIFOLVL_REG:
	case MAX310X_RXFIFOLVL_REG:
	case MAX310X_GPIODATA_REG:
	case MAX310X_BRGDIVLSB_REG:
	case MAX310X_REG_05:
	case MAX310X_REG_1F:
		return true;
	default:
		break;
	}

	return false;
}

static bool max310x_reg_precious(struct device *dev, unsigned int reg)
{
	switch (reg & 0x1f) {
	case MAX310X_RHR_REG:
	case MAX310X_IRQSTS_REG:
	case MAX310X_SPCHR_IRQSTS_REG:
	case MAX310X_STS_IRQSTS_REG:
		return true;
	default:
		break;
	}

	return false;
}

static void max310x_set_baud(struct uart_port *port, int baud)
{
	unsigned int mode = 0, div = port->uartclk / baud;

	if (!(div / 16)) {
		/* Mode x2 */
		mode = MAX310X_BRGCFG_2XMODE_BIT;
		div = (port->uartclk * 2) / baud;
	}

	if (!(div / 16)) {
		/* Mode x4 */
		mode = MAX310X_BRGCFG_4XMODE_BIT;
		div = (port->uartclk * 4) / baud;
	}

	max310x_port_write(port, MAX310X_BRGDIVMSB_REG, (div / 16) >> 8);
	max310x_port_write(port, MAX310X_BRGDIVLSB_REG, div / 16);
	max310x_port_write(port, MAX310X_BRGCFG_REG, (div % 16) | mode);
}

static int max310x_update_best_err(unsigned long f, long *besterr)
{
	/* Use baudrate 115200 for calculate error */
	long err = f % (115200 * 16);

	if ((*besterr < 0) || (*besterr > err)) {
		*besterr = err;
		return 0;
	}

	return 1;
}

static int max310x_set_ref_clk(struct max310x_port *s)
{
	unsigned int div, clksrc, pllcfg = 0;
	long besterr = -1;
	unsigned long fdiv, fmul, bestfreq = s->pdata->frequency;

	/* First, update error without PLL */
	max310x_update_best_err(s->pdata->frequency, &besterr);

	/* Try all possible PLL dividers */
	for (div = 1; (div <= 63) && besterr; div++) {
		fdiv = DIV_ROUND_CLOSEST(s->pdata->frequency, div);

		/* Try multiplier 6 */
		fmul = fdiv * 6;
		if ((fdiv >= 500000) && (fdiv <= 800000))
			if (!max310x_update_best_err(fmul, &besterr)) {
				pllcfg = (0 << 6) | div;
				bestfreq = fmul;
			}
		/* Try multiplier 48 */
		fmul = fdiv * 48;
		if ((fdiv >= 850000) && (fdiv <= 1200000))
			if (!max310x_update_best_err(fmul, &besterr)) {
				pllcfg = (1 << 6) | div;
				bestfreq = fmul;
			}
		/* Try multiplier 96 */
		fmul = fdiv * 96;
		if ((fdiv >= 425000) && (fdiv <= 1000000))
			if (!max310x_update_best_err(fmul, &besterr)) {
				pllcfg = (2 << 6) | div;
				bestfreq = fmul;
			}
		/* Try multiplier 144 */
		fmul = fdiv * 144;
		if ((fdiv >= 390000) && (fdiv <= 667000))
			if (!max310x_update_best_err(fmul, &besterr)) {
				pllcfg = (3 << 6) | div;
				bestfreq = fmul;
			}
	}

	/* Configure clock source */
	if (s->pdata->driver_flags & MAX310X_EXT_CLK)
		clksrc = MAX310X_CLKSRC_EXTCLK_BIT;
	else
		clksrc = MAX310X_CLKSRC_CRYST_BIT;

	/* Configure PLL */
	if (pllcfg) {
		clksrc |= MAX310X_CLKSRC_PLL_BIT;
		regmap_write(s->regmap, MAX310X_PLLCFG_REG, pllcfg);
	} else
		clksrc |= MAX310X_CLKSRC_PLLBYP_BIT;

	regmap_write(s->regmap, MAX310X_CLKSRC_REG, clksrc);

	/* Wait for crystal */
	if (pllcfg && !(s->pdata->driver_flags & MAX310X_EXT_CLK))
		msleep(10);

	return (int)bestfreq;
}

static void max310x_handle_rx(struct uart_port *port, unsigned int rxlen)
{
	unsigned int sts, ch, flag;

	if (unlikely(rxlen >= port->fifosize)) {
		dev_warn_ratelimited(port->dev,
				     "Port %i: Possible RX FIFO overrun\n",
				     port->line);
		port->icount.buf_overrun++;
		/* Ensure sanity of RX level */
		rxlen = port->fifosize;
	}

	while (rxlen--) {
		ch = max310x_port_read(port, MAX310X_RHR_REG);
		sts = max310x_port_read(port, MAX310X_LSR_IRQSTS_REG);

		sts &= MAX310X_LSR_RXPAR_BIT | MAX310X_LSR_FRERR_BIT |
		       MAX310X_LSR_RXOVR_BIT | MAX310X_LSR_RXBRK_BIT;

		port->icount.rx++;
		flag = TTY_NORMAL;

		if (unlikely(sts)) {
			if (sts & MAX310X_LSR_RXBRK_BIT) {
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if (sts & MAX310X_LSR_RXPAR_BIT)
				port->icount.parity++;
			else if (sts & MAX310X_LSR_FRERR_BIT)
				port->icount.frame++;
			else if (sts & MAX310X_LSR_RXOVR_BIT)
				port->icount.overrun++;

			sts &= port->read_status_mask;
			if (sts & MAX310X_LSR_RXBRK_BIT)
				flag = TTY_BREAK;
			else if (sts & MAX310X_LSR_RXPAR_BIT)
				flag = TTY_PARITY;
			else if (sts & MAX310X_LSR_FRERR_BIT)
				flag = TTY_FRAME;
			else if (sts & MAX310X_LSR_RXOVR_BIT)
				flag = TTY_OVERRUN;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;

		if (sts & port->ignore_status_mask)
			continue;

		uart_insert_char(port, sts, MAX310X_LSR_RXOVR_BIT, ch, flag);
	}

	tty_flip_buffer_push(&port->state->port);
}

static void max310x_handle_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int txlen, to_send;

	if (unlikely(port->x_char)) {
		max310x_port_write(port, MAX310X_THR_REG, port->x_char);
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
		txlen = max310x_port_read(port, MAX310X_TXFIFOLVL_REG);
		txlen = port->fifosize - txlen;
		to_send = (to_send > txlen) ? txlen : to_send;

		/* Add data to send */
		port->icount.tx += to_send;
		while (to_send--) {
			max310x_port_write(port, MAX310X_THR_REG,
					   xmit->buf[xmit->tail]);
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		};
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static void max310x_port_irq(struct max310x_port *s, int portno)
{
	struct uart_port *port = &s->p[portno].port;

	do {
		unsigned int ists, lsr, rxlen;

		/* Read IRQ status & RX FIFO level */
		ists = max310x_port_read(port, MAX310X_IRQSTS_REG);
		rxlen = max310x_port_read(port, MAX310X_RXFIFOLVL_REG);
		if (!ists && !rxlen)
			break;

		if (ists & MAX310X_IRQ_CTS_BIT) {
			lsr = max310x_port_read(port, MAX310X_LSR_IRQSTS_REG);
			uart_handle_cts_change(port,
					       !!(lsr & MAX310X_LSR_CTS_BIT));
		}
		if (rxlen)
			max310x_handle_rx(port, rxlen);
		if (ists & MAX310X_IRQ_TXEMPTY_BIT) {
			mutex_lock(&s->mutex);
			max310x_handle_tx(port);
			mutex_unlock(&s->mutex);
		}
	} while (1);
}

static irqreturn_t max310x_ist(int irq, void *dev_id)
{
	struct max310x_port *s = (struct max310x_port *)dev_id;

	if (s->uart.nr > 1) {
		do {
			unsigned int val = ~0;

			WARN_ON_ONCE(regmap_read(s->regmap,
						 MAX310X_GLOBALIRQ_REG, &val));
			val = ((1 << s->uart.nr) - 1) & ~val;
			if (!val)
				break;
			max310x_port_irq(s, fls(val) - 1);
		} while (1);
	} else
		max310x_port_irq(s, 0);

	return IRQ_HANDLED;
}

static void max310x_wq_proc(struct work_struct *ws)
{
	struct max310x_one *one = container_of(ws, struct max310x_one, tx_work);
	struct max310x_port *s = dev_get_drvdata(one->port.dev);

	mutex_lock(&s->mutex);
	max310x_handle_tx(&one->port);
	mutex_unlock(&s->mutex);
}

static void max310x_start_tx(struct uart_port *port)
{
	struct max310x_one *one = container_of(port, struct max310x_one, port);

	if (!work_pending(&one->tx_work))
		schedule_work(&one->tx_work);
}

static unsigned int max310x_tx_empty(struct uart_port *port)
{
	unsigned int lvl, sts;

	lvl = max310x_port_read(port, MAX310X_TXFIFOLVL_REG);
	sts = max310x_port_read(port, MAX310X_IRQSTS_REG);

	return ((sts & MAX310X_IRQ_TXEMPTY_BIT) && !lvl) ? TIOCSER_TEMT : 0;
}

static unsigned int max310x_get_mctrl(struct uart_port *port)
{
	/* DCD and DSR are not wired and CTS/RTS is handled automatically
	 * so just indicate DSR and CAR asserted
	 */
	return TIOCM_DSR | TIOCM_CAR;
}

static void max310x_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* DCD and DSR are not wired and CTS/RTS is hadnled automatically
	 * so do nothing
	 */
}

static void max310x_break_ctl(struct uart_port *port, int break_state)
{
	max310x_port_update(port, MAX310X_LCR_REG,
			    MAX310X_LCR_TXBREAK_BIT,
			    break_state ? MAX310X_LCR_TXBREAK_BIT : 0);
}

static void max310x_set_termios(struct uart_port *port,
				struct ktermios *termios,
				struct ktermios *old)
{
	unsigned int lcr, flow = 0;
	int baud;

	/* Mask termios capabilities we don't support */
	termios->c_cflag &= ~CMSPAR;

	/* Word size */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr = MAX310X_LCR_WORD_LEN_5;
		break;
	case CS6:
		lcr = MAX310X_LCR_WORD_LEN_6;
		break;
	case CS7:
		lcr = MAX310X_LCR_WORD_LEN_7;
		break;
	case CS8:
	default:
		lcr = MAX310X_LCR_WORD_LEN_8;
		break;
	}

	/* Parity */
	if (termios->c_cflag & PARENB) {
		lcr |= MAX310X_LCR_PARITY_BIT;
		if (!(termios->c_cflag & PARODD))
			lcr |= MAX310X_LCR_EVENPARITY_BIT;
	}

	/* Stop bits */
	if (termios->c_cflag & CSTOPB)
		lcr |= MAX310X_LCR_STOPLEN_BIT; /* 2 stops */

	/* Update LCR register */
	max310x_port_write(port, MAX310X_LCR_REG, lcr);

	/* Set read status mask */
	port->read_status_mask = MAX310X_LSR_RXOVR_BIT;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= MAX310X_LSR_RXPAR_BIT |
					  MAX310X_LSR_FRERR_BIT;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= MAX310X_LSR_RXBRK_BIT;

	/* Set status ignore mask */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNBRK)
		port->ignore_status_mask |= MAX310X_LSR_RXBRK_BIT;
	if (!(termios->c_cflag & CREAD))
		port->ignore_status_mask |= MAX310X_LSR_RXPAR_BIT |
					    MAX310X_LSR_RXOVR_BIT |
					    MAX310X_LSR_FRERR_BIT |
					    MAX310X_LSR_RXBRK_BIT;

	/* Configure flow control */
	max310x_port_write(port, MAX310X_XON1_REG, termios->c_cc[VSTART]);
	max310x_port_write(port, MAX310X_XOFF1_REG, termios->c_cc[VSTOP]);
	if (termios->c_cflag & CRTSCTS)
		flow |= MAX310X_FLOWCTRL_AUTOCTS_BIT |
			MAX310X_FLOWCTRL_AUTORTS_BIT;
	if (termios->c_iflag & IXON)
		flow |= MAX310X_FLOWCTRL_SWFLOW3_BIT |
			MAX310X_FLOWCTRL_SWFLOWEN_BIT;
	if (termios->c_iflag & IXOFF)
		flow |= MAX310X_FLOWCTRL_SWFLOW1_BIT |
			MAX310X_FLOWCTRL_SWFLOWEN_BIT;
	max310x_port_write(port, MAX310X_FLOWCTRL_REG, flow);

	/* Get baud rate generator configuration */
	baud = uart_get_baud_rate(port, termios, old,
				  port->uartclk / 16 / 0xffff,
				  port->uartclk / 4);

	/* Setup baudrate generator */
	max310x_set_baud(port, baud);

	/* Update timeout according to new baud rate */
	uart_update_timeout(port, termios->c_cflag, baud);
}

static int max310x_startup(struct uart_port *port)
{
	unsigned int val, line = port->line;
	struct max310x_port *s = dev_get_drvdata(port->dev);

	s->devtype->power(port, 1);

	/* Configure baud rate, 9600 as default */
	max310x_set_baud(port, 9600);

	/* Configure LCR register, 8N1 mode by default */
	max310x_port_write(port, MAX310X_LCR_REG, MAX310X_LCR_WORD_LEN_8);

	/* Configure MODE1 register */
	max310x_port_update(port, MAX310X_MODE1_REG,
			    MAX310X_MODE1_TRNSCVCTRL_BIT,
			    (s->pdata->uart_flags[line] & MAX310X_AUTO_DIR_CTRL)
			    ? MAX310X_MODE1_TRNSCVCTRL_BIT : 0);

	/* Configure MODE2 register */
	val = MAX310X_MODE2_RXEMPTINV_BIT;
	if (s->pdata->uart_flags[line] & MAX310X_LOOPBACK)
		val |= MAX310X_MODE2_LOOPBACK_BIT;
	if (s->pdata->uart_flags[line] & MAX310X_ECHO_SUPRESS)
		val |= MAX310X_MODE2_ECHOSUPR_BIT;

	/* Reset FIFOs */
	val |= MAX310X_MODE2_FIFORST_BIT;
	max310x_port_write(port, MAX310X_MODE2_REG, val);
	max310x_port_update(port, MAX310X_MODE2_REG,
			    MAX310X_MODE2_FIFORST_BIT, 0);

	/* Configure flow control levels */
	/* Flow control halt level 96, resume level 48 */
	max310x_port_write(port, MAX310X_FLOWLVL_REG,
			   MAX310X_FLOWLVL_RES(48) | MAX310X_FLOWLVL_HALT(96));

	/* Clear IRQ status register */
	max310x_port_read(port, MAX310X_IRQSTS_REG);

	/* Enable RX, TX, CTS change interrupts */
	val = MAX310X_IRQ_RXEMPTY_BIT | MAX310X_IRQ_TXEMPTY_BIT;
	max310x_port_write(port, MAX310X_IRQEN_REG, val | MAX310X_IRQ_CTS_BIT);

	return 0;
}

static void max310x_shutdown(struct uart_port *port)
{
	struct max310x_port *s = dev_get_drvdata(port->dev);

	/* Disable all interrupts */
	max310x_port_write(port, MAX310X_IRQEN_REG, 0);

	s->devtype->power(port, 0);
}

static const char *max310x_type(struct uart_port *port)
{
	struct max310x_port *s = dev_get_drvdata(port->dev);

	return (port->type == PORT_MAX310X) ? s->devtype->name : NULL;
}

static int max310x_request_port(struct uart_port *port)
{
	/* Do nothing */
	return 0;
}

static void max310x_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_MAX310X;
}

static int max310x_verify_port(struct uart_port *port, struct serial_struct *s)
{
	if ((s->type != PORT_UNKNOWN) && (s->type != PORT_MAX310X))
		return -EINVAL;
	if (s->irq != port->irq)
		return -EINVAL;

	return 0;
}

static void max310x_null_void(struct uart_port *port)
{
	/* Do nothing */
}

static const struct uart_ops max310x_ops = {
	.tx_empty	= max310x_tx_empty,
	.set_mctrl	= max310x_set_mctrl,
	.get_mctrl	= max310x_get_mctrl,
	.stop_tx	= max310x_null_void,
	.start_tx	= max310x_start_tx,
	.stop_rx	= max310x_null_void,
	.enable_ms	= max310x_null_void,
	.break_ctl	= max310x_break_ctl,
	.startup	= max310x_startup,
	.shutdown	= max310x_shutdown,
	.set_termios	= max310x_set_termios,
	.type		= max310x_type,
	.request_port	= max310x_request_port,
	.release_port	= max310x_null_void,
	.config_port	= max310x_config_port,
	.verify_port	= max310x_verify_port,
};

static int __maybe_unused max310x_suspend(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < s->uart.nr; i++) {
		uart_suspend_port(&s->uart, &s->p[i].port);
		s->devtype->power(&s->p[i].port, 0);
	}

	return 0;
}

static int __maybe_unused max310x_resume(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < s->uart.nr; i++) {
		s->devtype->power(&s->p[i].port, 1);
		uart_resume_port(&s->uart, &s->p[i].port);
	}

	return 0;
}

#ifdef CONFIG_GPIOLIB
static int max310x_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	unsigned int val;
	struct max310x_port *s = container_of(chip, struct max310x_port, gpio);
	struct uart_port *port = &s->p[offset / 4].port;

	val = max310x_port_read(port, MAX310X_GPIODATA_REG);

	return !!((val >> 4) & (1 << (offset % 4)));
}

static void max310x_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct max310x_port *s = container_of(chip, struct max310x_port, gpio);
	struct uart_port *port = &s->p[offset / 4].port;

	max310x_port_update(port, MAX310X_GPIODATA_REG, 1 << (offset % 4),
			    value ? 1 << (offset % 4) : 0);
}

static int max310x_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct max310x_port *s = container_of(chip, struct max310x_port, gpio);
	struct uart_port *port = &s->p[offset / 4].port;

	max310x_port_update(port, MAX310X_GPIOCFG_REG, 1 << (offset % 4), 0);

	return 0;
}

static int max310x_gpio_direction_output(struct gpio_chip *chip,
					 unsigned offset, int value)
{
	struct max310x_port *s = container_of(chip, struct max310x_port, gpio);
	struct uart_port *port = &s->p[offset / 4].port;

	max310x_port_update(port, MAX310X_GPIODATA_REG, 1 << (offset % 4),
			    value ? 1 << (offset % 4) : 0);
	max310x_port_update(port, MAX310X_GPIOCFG_REG, 1 << (offset % 4),
			    1 << (offset % 4));

	return 0;
}
#endif

static int max310x_probe(struct device *dev, int is_spi,
			 struct max310x_devtype *devtype, int irq)
{
	struct max310x_port *s;
	struct max310x_pdata *pdata = dev_get_platdata(dev);
	int i, ret, uartclk;

	/* Check for IRQ */
	if (irq <= 0) {
		dev_err(dev, "No IRQ specified\n");
		return -ENOTSUPP;
	}

	if (!pdata) {
		dev_err(dev, "No platform data supplied\n");
		return -EINVAL;
	}

	/* Alloc port structure */
	s = devm_kzalloc(dev, sizeof(*s) +
			 sizeof(struct max310x_one) * devtype->nr, GFP_KERNEL);
	if (!s) {
		dev_err(dev, "Error allocating port structure\n");
		return -ENOMEM;
	}

	/* Check input frequency */
	if ((pdata->driver_flags & MAX310X_EXT_CLK) &&
	   ((pdata->frequency < 500000) || (pdata->frequency > 35000000)))
		goto err_freq;
	/* Check frequency for quartz */
	if (!(pdata->driver_flags & MAX310X_EXT_CLK) &&
	   ((pdata->frequency < 1000000) || (pdata->frequency > 4000000)))
		goto err_freq;

	s->pdata = pdata;
	s->devtype = devtype;
	dev_set_drvdata(dev, s);

	mutex_init(&s->mutex);

	/* Setup regmap */
	s->regcfg.reg_bits		= 8;
	s->regcfg.val_bits		= 8;
	s->regcfg.read_flag_mask	= 0x00;
	s->regcfg.write_flag_mask	= 0x80;
	s->regcfg.cache_type		= REGCACHE_RBTREE;
	s->regcfg.writeable_reg		= max310x_reg_writeable;
	s->regcfg.volatile_reg		= max310x_reg_volatile;
	s->regcfg.precious_reg		= max310x_reg_precious;
	s->regcfg.max_register		= devtype->nr * 0x20 - 1;

	if (IS_ENABLED(CONFIG_SPI_MASTER) && is_spi) {
		struct spi_device *spi = to_spi_device(dev);

		s->regmap = devm_regmap_init_spi(spi, &s->regcfg);
	} else
		return -ENOTSUPP;

	if (IS_ERR(s->regmap)) {
		dev_err(dev, "Failed to initialize register map\n");
		return PTR_ERR(s->regmap);
	}

	/* Board specific configure */
	if (s->pdata->init)
		s->pdata->init();

	/* Check device to ensure we are talking to what we expect */
	ret = devtype->detect(dev);
	if (ret)
		return ret;

	for (i = 0; i < devtype->nr; i++) {
		unsigned int offs = i << 5;

		/* Reset port */
		regmap_write(s->regmap, MAX310X_MODE2_REG + offs,
			     MAX310X_MODE2_RST_BIT);
		/* Clear port reset */
		regmap_write(s->regmap, MAX310X_MODE2_REG + offs, 0);

		/* Wait for port startup */
		do {
			regmap_read(s->regmap,
				    MAX310X_BRGDIVLSB_REG + offs, &ret);
		} while (ret != 0x01);

		regmap_update_bits(s->regmap, MAX310X_MODE1_REG + offs,
				   MAX310X_MODE1_AUTOSLEEP_BIT,
				   MAX310X_MODE1_AUTOSLEEP_BIT);
	}

	uartclk = max310x_set_ref_clk(s);
	dev_dbg(dev, "Reference clock set to %i Hz\n", uartclk);

	/* Register UART driver */
	s->uart.owner		= THIS_MODULE;
	s->uart.dev_name	= "ttyMAX";
	s->uart.major		= MAX310X_MAJOR;
	s->uart.minor		= MAX310X_MINOR;
	s->uart.nr		= devtype->nr;
	ret = uart_register_driver(&s->uart);
	if (ret) {
		dev_err(dev, "Registering UART driver failed\n");
		return ret;
	}

	for (i = 0; i < devtype->nr; i++) {
		/* Initialize port data */
		s->p[i].port.line	= i;
		s->p[i].port.dev	= dev;
		s->p[i].port.irq	= irq;
		s->p[i].port.type	= PORT_MAX310X;
		s->p[i].port.fifosize	= MAX310X_FIFO_SIZE;
		s->p[i].port.flags	= UPF_SKIP_TEST | UPF_FIXED_TYPE |
					  UPF_LOW_LATENCY;
		s->p[i].port.iotype	= UPIO_PORT;
		s->p[i].port.iobase	= i * 0x20;
		s->p[i].port.membase	= (void __iomem *)~0;
		s->p[i].port.uartclk	= uartclk;
		s->p[i].port.ops	= &max310x_ops;
		/* Disable all interrupts */
		max310x_port_write(&s->p[i].port, MAX310X_IRQEN_REG, 0);
		/* Clear IRQ status register */
		max310x_port_read(&s->p[i].port, MAX310X_IRQSTS_REG);
		/* Enable IRQ pin */
		max310x_port_update(&s->p[i].port, MAX310X_MODE1_REG,
				    MAX310X_MODE1_IRQSEL_BIT,
				    MAX310X_MODE1_IRQSEL_BIT);
		/* Initialize queue for start TX */
		INIT_WORK(&s->p[i].tx_work, max310x_wq_proc);
		/* Register port */
		uart_add_one_port(&s->uart, &s->p[i].port);
		/* Go to suspend mode */
		devtype->power(&s->p[i].port, 0);
	}

#ifdef CONFIG_GPIOLIB
	/* Setup GPIO cotroller */
	if (s->pdata->gpio_base) {
		s->gpio.owner		= THIS_MODULE;
		s->gpio.dev		= dev;
		s->gpio.label		= dev_name(dev);
		s->gpio.direction_input	= max310x_gpio_direction_input;
		s->gpio.get		= max310x_gpio_get;
		s->gpio.direction_output= max310x_gpio_direction_output;
		s->gpio.set		= max310x_gpio_set;
		s->gpio.base		= s->pdata->gpio_base;
		s->gpio.ngpio		= devtype->nr * 4;
		s->gpio.can_sleep	= 1;
		if (!gpiochip_add(&s->gpio))
			s->gpio_used = 1;
	} else
		dev_info(dev, "GPIO support not enabled\n");
#endif

	/* Setup interrupt */
	ret = devm_request_threaded_irq(dev, irq, NULL, max310x_ist,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name(dev), s);
	if (ret) {
		dev_err(dev, "Unable to reguest IRQ %i\n", irq);
#ifdef CONFIG_GPIOLIB
		if (s->gpio_used)
			WARN_ON(gpiochip_remove(&s->gpio));
#endif
	}

	return ret;

err_freq:
	dev_err(dev, "Frequency parameter incorrect\n");
	return -EINVAL;
}

static int max310x_remove(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	int i, ret = 0;

	for (i = 0; i < s->uart.nr; i++) {
		cancel_work_sync(&s->p[i].tx_work);
		uart_remove_one_port(&s->uart, &s->p[i].port);
		s->devtype->power(&s->p[i].port, 0);
	}

	uart_unregister_driver(&s->uart);

#ifdef CONFIG_GPIOLIB
	if (s->gpio_used)
		ret = gpiochip_remove(&s->gpio);
#endif

	if (s->pdata->exit)
		s->pdata->exit();

	return ret;
}

#ifdef CONFIG_SPI_MASTER
static int max310x_spi_probe(struct spi_device *spi)
{
	struct max310x_devtype *devtype =
		(struct max310x_devtype *)spi_get_device_id(spi)->driver_data;
	int ret;

	/* Setup SPI bus */
	spi->bits_per_word	= 8;
	spi->mode		= spi->mode ? : SPI_MODE_0;
	spi->max_speed_hz	= spi->max_speed_hz ? : 26000000;
	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "SPI setup failed\n");
		return ret;
	}

	return max310x_probe(&spi->dev, 1, devtype, spi->irq);
}

static int max310x_spi_remove(struct spi_device *spi)
{
	return max310x_remove(&spi->dev);
}

static SIMPLE_DEV_PM_OPS(max310x_pm_ops, max310x_suspend, max310x_resume);

static const struct spi_device_id max310x_id_table[] = {
	{ "max3107",	(kernel_ulong_t)&max3107_devtype, },
	{ "max3108",	(kernel_ulong_t)&max3108_devtype, },
	{ "max3109",	(kernel_ulong_t)&max3109_devtype, },
	{ "max14830",	(kernel_ulong_t)&max14830_devtype, },
	{ }
};
MODULE_DEVICE_TABLE(spi, max310x_id_table);

static struct spi_driver max310x_uart_driver = {
	.driver = {
		.name	= MAX310X_NAME,
		.owner	= THIS_MODULE,
		.pm	= &max310x_pm_ops,
	},
	.probe		= max310x_spi_probe,
	.remove		= max310x_spi_remove,
	.id_table	= max310x_id_table,
};
module_spi_driver(max310x_uart_driver);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("MAX310X serial driver");
