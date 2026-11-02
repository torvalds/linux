// SPDX-License-Identifier: GPL-2.0+
/*
 *  Maxim (Dallas) MAX3107/8/9, MAX14830 serial driver
 *
 *  Copyright (C) 2012-2016 Alexander Shiyan <shc_work@mail.ru>
 *
 *  Based on max3100.c, by Christian Pellegrin <chripell@evolware.org>
 *  Based on max3110.c, by Feng Tang <feng.tang@intel.com>
 *  Based on max3107.c, by Aavamobile
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#define MAX310X_NAME			"max310x"
#define MAX310X_MAJOR			204
#define MAX310X_MINOR			209
#define MAX310X_UART_NRMAX		16
#define MAX310X_MAX_PORTS		4 /* Maximum number of UART ports per IC. */

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
#define MAX310X_EXTREG_START		(0x20) /* Only relevant in SPI mode. */

#define MAX310X_REVID_REG		MAX310X_REG_1F /* Revision ID */

#define MAX310X_GLOBALIRQ_REG		MAX310X_REG_1F /* Global IRQ (RO) */
#define MAX310X_GLOBALCMD_REG		MAX310X_REG_1F /* Global Command (WO) */

/* Extended registers */
#define MAX310X_REVID_EXTREG		(0x25) /* Revision ID
						* (extended addressing space)
						*/
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
/* LCR bits owned by termios; TX break and RTS are driven elsewhere */
#define MAX310X_LCR_TERMIOS_MASK	GENMASK(5, 0)

/* IRDA register bits */
#define MAX310X_IRDA_IRDAEN_BIT		(1 << 0) /* IRDA mode enable */
#define MAX310X_IRDA_SIR_BIT		(1 << 1) /* SIR mode enable */
#define MAX310X_IRDA_RTSINVERT_BIT	(1 << 2) /* Invert RTS output */

/* HDPIXDELAY accessor macros */
#define MAX310X_HDPIXDELAY_SETUP(val)	(((val) & 0x0f) << 4)
#define MAX310X_HDPIXDELAY_HOLD(val)	((val) & 0x0f)

/* Flow control trigger level register masks */
#define MAX310X_FLOWLVL_HALT_MASK	GENMASK(3, 0) /* Flow control halt level */
#define MAX310X_FLOWLVL_RES_MASK	GENMASK(7, 4) /* Flow control resume level */
#define MAX310X_FLOWLVL_HALT(words)	((words / 8) & 0x0f)
#define MAX310X_FLOWLVL_RES(words)	(((words / 8) & 0x0f) << 4)

/* FIFO interrupt trigger level register masks */
#define MAX310X_FIFOTRIGLVL_TX_MASK	GENMASK(3, 0) /* TX FIFO trigger level */
#define MAX310X_FIFOTRIGLVL_RX_MASK	GENMASK(7, 4) /* RX FIFO trigger level */
#define MAX310X_FIFOTRIGLVL_TX(words)	((words / 8) & 0x0f)
#define MAX310X_FIFOTRIGLVL_RX(words)	(((words / 8) & 0x0f) << 4)

/* Flow control register bits */
#define MAX310X_FLOWCTRL_AUTORTS_BIT	(1 << 0) /* Auto RTS flow ctrl enable */
#define MAX310X_FLOWCTRL_AUTOCTS_BIT	(1 << 1) /* Auto CTS flow ctrl enable */
#define MAX310X_FLOWCTRL_GPIADDR_BIT	(1 << 2) /* Enables that GPIO inputs
						  * are used in conjunction with
						  * XOFF2 for definition of
						  * special character
						  */
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

/* PLL configuration register masks */
#define MAX310X_PLLCFG_PREDIV_MASK	GENMASK(5, 0) /* PLL predivision value */
#define MAX310X_PLLCFG_PLLFACTOR_MASK	GENMASK(7, 6) /* PLL multiplication factor */

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
#define MAX310x_REV_MASK		GENMASK(7, 3)
#define MAX310X_WRITE_BIT		0x80

/* Port startup definitions */
#define MAX310X_PORT_STARTUP_SLEEP_US	10000  /* Delay between retries */
#define MAX310X_PORT_STARTUP_TIMEOUT_US	(20 * MAX310X_PORT_STARTUP_SLEEP_US) /* Total timeout */

/* Crystal-related definitions */
#define MAX310X_XTAL_SLEEP_US		10000  /* Delay between retries */
#define MAX310X_XTAL_TIMEOUT_US		(20 * MAX310X_XTAL_SLEEP_US) /* Total timeout */

/* MAX3107 specific */
#define MAX3107_REV_ID			(0xa0)

/* MAX3109 specific */
#define MAX3109_REV_ID			(0xc0)

/* MAX14830 specific */
#define MAX14830_BRGCFG_CLKDIS_BIT	(1 << 6) /* Clock Disable */
#define MAX14830_REV_ID			(0xb0)

struct max310x_clk_config_t {
	u8 prediv; /* Predivider */
	u8 pll_mult; /* PLL multiplier */
	unsigned int fref; /*
			    * Reference clock for fractional baud rate generator:
			    *   PLL enabled:  (freq / prediv) x pll_mult
			    *   PLL disabled: freq
			    */
	unsigned int err; /* Computed error for selected parameters */
};

struct max310x_if_cfg {
	int (*extended_reg_enable)(struct device *dev, bool enable);
	u8 rev_id_offset;
};

struct max310x_devtype {
	struct {
		unsigned short min;
		unsigned short max;
	} slave_addr; /* Relevant only in I2C mode. */
	int	nr;
	char	name[9];
	u8	mode1;
	u8	rev_id_val;
	u8	rev_id_reg; /* Relevant only if rev_id_val is defined. */
	u8	power_reg; /* Register address for power/sleep control. */
	u8	power_bit; /* Bit for sleep or power-off mode (active high). */
};

/* Software-timed RS485 RTS envelope phase */
enum max310x_tx_state {
	MAX310X_TX_OFF,			/* idle, RTS released */
	MAX310X_TX_WAIT_BEFORE_SEND,	/* RTS asserted, before-send delay */
	MAX310X_TX_SEND,		/* data in flight, awaiting TX-empty */
	MAX310X_TX_WAIT_AFTER_SEND,	/* data drained, after-send hold */
};

struct max310x_one {
	struct uart_port	port;
	struct work_struct	tx_work;
	struct work_struct	md_work;
	struct work_struct	rs_work;
	struct work_struct	rts_work;
	struct hrtimer		tx_delay_tmr;
	struct regmap		*regmap;
	unsigned int		char_time_us;
	unsigned int		baud;
	bool			sw_rts_during_tx;
	bool			cancel_tx_delay_tmr;
	bool			tx_teardown;	/* envelope being torn down */
	bool			rs485_pending;	/* rs_work not yet applied */
	bool			tx_break;	/* break_ctl() owns the transceiver */
	enum max310x_tx_state	tx_state;

	u8 rx_buf[MAX310X_FIFO_SIZE];
};
#define to_max310x_port(_port) \
	container_of(_port, struct max310x_one, port)

struct max310x_port {
	const struct max310x_devtype *devtype;
	const struct max310x_if_cfg *if_cfg;
	struct regmap		*regmap;
	struct clk		*clk;
#ifdef CONFIG_GPIOLIB
	struct gpio_chip	gpio;
#endif
	struct max310x_one	p[];
};

static struct uart_driver max310x_uart = {
	.owner		= THIS_MODULE,
	.driver_name	= MAX310X_NAME,
	.dev_name	= "ttyMAX",
	.major		= MAX310X_MAJOR,
	.minor		= MAX310X_MINOR,
	.nr		= MAX310X_UART_NRMAX,
};

static DECLARE_BITMAP(max310x_lines, MAX310X_UART_NRMAX);

static u8 max310x_port_read(struct uart_port *port, u8 reg)
{
	struct max310x_one *one = to_max310x_port(port);
	unsigned int val = 0;

	regmap_read(one->regmap, reg, &val);

	return val;
}

static void max310x_port_write(struct uart_port *port, u8 reg, u8 val)
{
	struct max310x_one *one = to_max310x_port(port);

	regmap_write(one->regmap, reg, val);
}

static void max310x_port_update(struct uart_port *port, u8 reg, u8 mask, u8 val)
{
	struct max310x_one *one = to_max310x_port(port);

	regmap_update_bits(one->regmap, reg, mask, val);
}

static int max310x_detect(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	unsigned int val = 0;
	int ret;

	/* Check if variant supports REV ID register: */
	if (s->devtype->rev_id_val) {
		u8 rev_id_reg = s->devtype->rev_id_reg;

		/* Check if REV ID is in extended addressing space: */
		if (s->devtype->rev_id_reg >= MAX310X_EXTREG_START) {
			ret = s->if_cfg->extended_reg_enable(dev, true);
			if (ret)
				return ret;

			/* Adjust REV ID extended addressing space address: */
			if (s->if_cfg->rev_id_offset)
				rev_id_reg -= s->if_cfg->rev_id_offset;
		}

		regmap_read(s->regmap, rev_id_reg, &val);

		if (s->devtype->rev_id_reg >= MAX310X_EXTREG_START) {
			ret = s->if_cfg->extended_reg_enable(dev, false);
			if (ret)
				return ret;
		}

		if (((val & MAX310x_REV_MASK) != s->devtype->rev_id_val))
			return dev_err_probe(dev, -ENODEV,
					     "%s ID 0x%02x does not match\n",
					     s->devtype->name, val);
	} else {
		/*
		 * For variant without REV ID register, just check default value
		 * from clocksource register to make sure everything works.
		 */
		ret = regmap_read(s->regmap, MAX310X_CLKSRC_REG, &val);
		if (ret)
			return ret;

		if (val != (MAX310X_CLKSRC_EXTCLK_BIT | MAX310X_CLKSRC_PLLBYP_BIT))
			return dev_err_probe(dev, -ENODEV,
					     "%s not present\n",
					     s->devtype->name);
	}

	return 0;
}

static void max310x_power(struct uart_port *port, int on)
{
	struct max310x_port *s = dev_get_drvdata(port->dev);

	max310x_port_update(port, s->devtype->power_reg, s->devtype->power_bit,
			    on ? 0 : s->devtype->power_bit);
	if (on)
		msleep(50);
}

static const struct max310x_devtype max3107_devtype = {
	.name	= "MAX3107",
	.nr	= 1,
	.mode1	= MAX310X_MODE1_AUTOSLEEP_BIT | MAX310X_MODE1_IRQSEL_BIT,
	.rev_id_val = MAX3107_REV_ID,
	.rev_id_reg = MAX310X_REVID_REG,
	.power_reg = MAX310X_MODE1_REG,
	.power_bit = MAX310X_MODE1_FORCESLEEP_BIT,
	.slave_addr	= {
		.min = 0x2c,
		.max = 0x2f,
	},
};

static const struct max310x_devtype max3108_devtype = {
	.name	= "MAX3108",
	.nr	= 1,
	.mode1	= MAX310X_MODE1_AUTOSLEEP_BIT,
	.rev_id_val = 0, /* Unsupported. */
	.rev_id_reg = 0, /* Irrelevant when rev_id_val is not defined. */
	.power_reg = MAX310X_MODE1_REG,
	.power_bit = MAX310X_MODE1_FORCESLEEP_BIT,
	.slave_addr	= {
		.min = 0x60,
		.max = 0x6f,
	},
};

static const struct max310x_devtype max3109_devtype = {
	.name	= "MAX3109",
	.nr	= 2,
	.mode1	= MAX310X_MODE1_AUTOSLEEP_BIT,
	.rev_id_val = MAX3109_REV_ID,
	.rev_id_reg = MAX310X_REVID_EXTREG,
	.power_reg = MAX310X_MODE1_REG,
	.power_bit = MAX310X_MODE1_FORCESLEEP_BIT,
	.slave_addr	= {
		.min = 0x60,
		.max = 0x6f,
	},
};

static const struct max310x_devtype max14830_devtype = {
	.name	= "MAX14830",
	.nr	= 4,
	.mode1	= MAX310X_MODE1_IRQSEL_BIT,
	.rev_id_val = MAX14830_REV_ID,
	.rev_id_reg = MAX310X_REVID_EXTREG,
	.power_reg = MAX310X_BRGCFG_REG,
	.power_bit = MAX14830_BRGCFG_CLKDIS_BIT,
	.slave_addr	= {
		.min = 0x60,
		.max = 0x6f,
	},
};

static bool max310x_reg_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX310X_IRQSTS_REG:
	case MAX310X_LSR_IRQSTS_REG:
	case MAX310X_SPCHR_IRQSTS_REG:
	case MAX310X_STS_IRQSTS_REG:
	case MAX310X_TXFIFOLVL_REG:
	case MAX310X_RXFIFOLVL_REG:
		return false;
	default:
		return true;
	}
}

static bool max310x_reg_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
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
		return false;
	}
}

static bool max310x_reg_precious(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX310X_RHR_REG:
	case MAX310X_IRQSTS_REG:
	case MAX310X_SPCHR_IRQSTS_REG:
	case MAX310X_STS_IRQSTS_REG:
		return true;
	default:
		return false;
	}
}

static bool max310x_reg_noinc(struct device *dev, unsigned int reg)
{
	return reg == MAX310X_RHR_REG;
}

static int max310x_set_baud(struct uart_port *port, int baud)
{
	unsigned int mode = 0, div = 0, frac = 0, c = 0, F = 0;

	/*
	 * Calculate the integer divisor first. Select a proper mode
	 * in case if the requested baud is too high for the pre-defined
	 * clocks frequency.
	 */
	div = port->uartclk / baud;
	if (div < 8) {
		/* Mode x4 */
		c = 4;
		mode = MAX310X_BRGCFG_4XMODE_BIT;
	} else if (div < 16) {
		/* Mode x2 */
		c = 8;
		mode = MAX310X_BRGCFG_2XMODE_BIT;
	} else {
		c = 16;
	}

	/* Calculate the divisor in accordance with the fraction coefficient */
	div /= c;
	F = c*baud;

	/* Calculate the baud rate fraction */
	if (div > 0)
		frac = (16*(port->uartclk % F)) / F;
	else
		div = 1;

	max310x_port_write(port, MAX310X_BRGDIVMSB_REG, div >> 8);
	max310x_port_write(port, MAX310X_BRGDIVLSB_REG, div);
	max310x_port_write(port, MAX310X_BRGCFG_REG, frac | mode);

	/* Return the actual baud rate we just programmed */
	return (16*port->uartclk) / (c*(16*div + frac));
}

static void max310x_try_cfg(unsigned int fdiv, u8 div, u8 pll_mult,
			    struct max310x_clk_config_t *cfg)
{
	unsigned int fmul = fdiv * pll_mult;
	unsigned int err;

	/* Use high-enough baudrate to calculate error */
	err = fmul % (460800 * 16);

	if (cfg->err > err) {
		cfg->err = err;
		cfg->pll_mult = pll_mult;
		cfg->prediv = div;
		cfg->fref = fmul;
	}
}

static u8 max310x_pll_mult_to_id(u8 pll_mult)
{
	switch (pll_mult) {
	case 144:	return 3;
	case 96:	return 2;
	case 48:	return 1;
	case 6:
	default:	return 0;
	}
}

/*
 * From table 7 in datasheet: PLLFactor Selector Guide
 *
 * +-----------+----------------+-------------------+-------------------+
 * | PLLFactor | MULTIPLICATION |      fPLLIN       |       fREF        |
 * |  (1 & 0)  |     FACTOR     +---------+---------+---------+---------+
 * |           |                |   MIN   |   MAX   |   MIN   |   MAX   |
 * +-----------+----------------+---------+---------+---------+---------+
 * |     0     |        6       |  500kHz |  800kHz |   3MHz  |  4.8MHz |
 * +-----------+----------------+---------+---------+---------+---------+
 * |     1     |       48       |  850kHz |  1.2MHz | 40.8MHz |  56MHz  |
 * +-----------+----------------+---------+---------+---------+---------+
 * |     2     |       96       |  425kHz |   1MHz  | 40.8MHz |  96MHz  |
 * +-----------+----------------+---------+---------+---------+---------+
 * |     3     |      144       |  390kHz |  667kHz |  56MHz  |  96MHz  |
 * +-----------+----------------+---------+---------+---------+---------+
 */
static int max310x_set_ref_clk(struct device *dev, struct max310x_port *s,
			       unsigned int freq, unsigned int *fref, bool xtal)
{
	unsigned int div, fdiv, clksrc, val;
	struct max310x_clk_config_t cfg;

	cfg.err = UINT_MAX;
	cfg.prediv = 0;
	cfg.fref = freq;

	/* First, update error without PLL */
	max310x_try_cfg(freq, 1, 1, &cfg);

	/* Try all possible PLL dividers */
	for (div = 1; (div <= 63) && cfg.err; div++) {
		fdiv = DIV_ROUND_CLOSEST(freq, div);

		if ((fdiv >= 500000) && (fdiv <= 800000))
			max310x_try_cfg(fdiv, div, 6, &cfg);   /* PLL x6 */
		else if ((fdiv >= 850000) && (fdiv <= 1200000))
			max310x_try_cfg(fdiv, div, 48, &cfg);  /* PLL x48 */

		if ((fdiv >= 425000) && (fdiv <= 1000000))
			max310x_try_cfg(fdiv, div, 96, &cfg);  /* PLL x96 */

		if ((fdiv >= 390000) && (fdiv <= 667000))
			max310x_try_cfg(fdiv, div, 144, &cfg); /* PLL x144 */
	}

	/* Configure clock source */
	clksrc = MAX310X_CLKSRC_EXTCLK_BIT | (xtal ? MAX310X_CLKSRC_CRYST_BIT : 0);

	/* Configure PLL */
	if (cfg.prediv) {
		u8 pll_id = max310x_pll_mult_to_id(cfg.pll_mult);

		clksrc |= MAX310X_CLKSRC_PLL_BIT;
		val = FIELD_PREP(MAX310X_PLLCFG_PLLFACTOR_MASK, pll_id) |
			FIELD_PREP(MAX310X_PLLCFG_PREDIV_MASK, cfg.prediv);
		regmap_write(s->regmap, MAX310X_PLLCFG_REG, val);
	} else
		clksrc |= MAX310X_CLKSRC_PLLBYP_BIT;

	regmap_write(s->regmap, MAX310X_CLKSRC_REG, clksrc);

	/* Wait for crystal */
	if (xtal) {
		int ret;

		ret = regmap_read_poll_timeout(s->regmap, MAX310X_STS_IRQSTS_REG,
					       val, val & MAX310X_STS_CLKREADY_BIT,
					       MAX310X_XTAL_SLEEP_US,
					       MAX310X_XTAL_TIMEOUT_US);
		if (ret)
			return dev_err_probe(dev, -EAGAIN,
					     "clock is not stable\n");
	}

	*fref = cfg.fref;

	return 0;
}

static void max310x_batch_write(struct uart_port *port, u8 *txbuf, unsigned int len)
{
	struct max310x_one *one = to_max310x_port(port);

	regmap_noinc_write(one->regmap, MAX310X_THR_REG, txbuf, len);
}

static void max310x_batch_read(struct uart_port *port, u8 *rxbuf, unsigned int len)
{
	struct max310x_one *one = to_max310x_port(port);

	regmap_noinc_read(one->regmap, MAX310X_RHR_REG, rxbuf, len);
}

static void max310x_rts_ctl(struct uart_port *port, bool rts_state)
{
	max310x_port_update(port, MAX310X_LCR_REG, MAX310X_LCR_RTS_BIT,
			    rts_state ? MAX310X_LCR_RTS_BIT : 0);
}

/* RTS level for the transmitting or the idle phase of an RS485 envelope */
static bool max310x_rts_level(struct uart_port *port, bool active)
{
	return active ? (port->rs485.flags & SER_RS485_RTS_ON_SEND) :
			(port->rs485.flags & SER_RS485_RTS_AFTER_SEND);
}

/*
 * Drive the RS485 RTS line to match the current tx_state and break
 * ownership. This is the only place that touches RTS, and it reads the
 * state rather than a fixed assert/deassert intent, so a newer assert is
 * never clobbered by a stale release and an expiring after-send hold never
 * releases a break in progress. It also arms the before-send timer once the
 * RTS edge is on the wire, so data is never shifted before RTS is asserted.
 */
static void max310x_rts_work_proc(struct work_struct *ws)
{
	struct max310x_one *one = container_of(ws, struct max310x_one, rts_work);
	struct uart_port *port = &one->port;
	bool rts_on = READ_ONCE(one->tx_state) != MAX310X_TX_OFF ||
		      READ_ONCE(one->tx_break);

	max310x_rts_ctl(port, max310x_rts_level(port, rts_on));

	guard(spinlock_irqsave)(&port->lock);
	if (READ_ONCE(one->tx_state) == MAX310X_TX_WAIT_BEFORE_SEND &&
	    !one->cancel_tx_delay_tmr && !hrtimer_active(&one->tx_delay_tmr))
		hrtimer_start(&one->tx_delay_tmr,
			      ms_to_ktime(port->rs485.delay_rts_before_send),
			      HRTIMER_MODE_REL);
}

static void max310x_handle_rx(struct uart_port *port, unsigned int rxlen)
{
	struct max310x_one *one = to_max310x_port(port);
	unsigned int sts, i;
	u8 ch, flag;

	if (port->read_status_mask == MAX310X_LSR_RXOVR_BIT) {
		/*
		 * We are just reading, happily ignoring any error conditions.
		 * Break condition, parity checking, framing errors -- they
		 * are all ignored. That means that we can do a batch-read.
		 *
		 * There is a small opportunity for race if the RX FIFO
		 * overruns while we're reading the buffer; the datasheets says
		 * that the LSR register applies to the "current" character.
		 * That's also the reason why we cannot do batched reads when
		 * asked to check the individual statuses.
		 */

		sts = max310x_port_read(port, MAX310X_LSR_IRQSTS_REG);
		max310x_batch_read(port, one->rx_buf, rxlen);

		port->icount.rx += rxlen;
		flag = TTY_NORMAL;
		sts &= port->read_status_mask;

		if (sts & MAX310X_LSR_RXOVR_BIT) {
			dev_warn_ratelimited(port->dev, "Hardware RX FIFO overrun\n");
			port->icount.overrun++;
		}

		for (i = 0; i < (rxlen - 1); ++i)
			uart_insert_char(port, sts, 0, one->rx_buf[i], flag);

		/*
		 * Handle the overrun case for the last character only, since
		 * the RxFIFO overflow happens after it is pushed to the FIFO
		 * tail.
		 */
		uart_insert_char(port, sts, MAX310X_LSR_RXOVR_BIT,
				 one->rx_buf[rxlen-1], flag);

	} else {
		if (unlikely(rxlen >= port->fifosize)) {
			dev_warn_ratelimited(port->dev, "Possible RX FIFO overrun\n");
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
	}

	tty_flip_buffer_push(&port->state->port);
}

static enum hrtimer_restart max310x_tmr_tx(struct hrtimer *timer)
{
	struct max310x_one *one = container_of(timer, struct max310x_one,
					       tx_delay_tmr);

	guard(spinlock_irqsave)(&one->port.lock);
	if (!one->cancel_tx_delay_tmr) {
		if (READ_ONCE(one->tx_state) == MAX310X_TX_WAIT_AFTER_SEND) {
			/* After-send hold elapsed: drop RTS via the rts worker. */
			WRITE_ONCE(one->tx_state, MAX310X_TX_OFF);
			schedule_work(&one->rts_work);
		} else {
			WRITE_ONCE(one->tx_state, MAX310X_TX_SEND);
			schedule_work(&one->tx_work);
		}
	}

	return HRTIMER_NORESTART;
}

static void max310x_delayed_stop_tx(struct uart_port *port)
{
	struct max310x_one *one = to_max310x_port(port);
	unsigned int txlvl;

	if (READ_ONCE(one->tx_state) == MAX310X_TX_OFF)
		return;

	/*
	 * Data still queued for transmission defers the hold: a refill is
	 * coming and the envelope is not over. Stopped output does not
	 * count - it drains nowhere, and the envelope must end.
	 */
	if (!kfifo_is_empty(&port->state->port.xmit_fifo) &&
	    !uart_tx_stopped(port))
		return;

	/*
	 * The kfifo can be empty while the chip TX FIFO is still draining, so arm
	 * the after-send hold only once the chip FIFO is empty too - the TX-empty
	 * interrupt re-invokes us then. Otherwise the hold starts early and RTS
	 * drops mid-character, clipping the last byte(s).
	 */
	txlvl = max310x_port_read(port, MAX310X_TXFIFOLVL_REG);
	if (txlvl)
		return;

	/*
	 * Runs from tx_work without port->lock, so re-check the state under it:
	 * shutdown() may have cancelled the envelope meanwhile. Only
	 * MAX310X_TX_SEND may arm the hold.
	 */
	guard(spinlock_irqsave)(&one->port.lock);
	if (one->cancel_tx_delay_tmr ||
	    READ_ONCE(one->tx_state) != MAX310X_TX_SEND)
		return;

	if (!hrtimer_active(&one->tx_delay_tmr)) {
		/*
		 * Add one character for the byte still in the shift register -
		 * TX-empty fires as it enters, not as it leaves.
		 */
		ktime_t delay = us_to_ktime(one->char_time_us +
					    port->rs485.delay_rts_after_send *
					    USEC_PER_MSEC);

		WRITE_ONCE(one->tx_state, MAX310X_TX_WAIT_AFTER_SEND);
		hrtimer_start(&one->tx_delay_tmr, delay, HRTIMER_MODE_REL);
	}
}

static void max310x_handle_tx(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;

	if (unlikely(port->x_char)) {
		max310x_port_write(port, MAX310X_THR_REG, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (kfifo_is_empty(&tport->xmit_fifo) || uart_tx_stopped(port)) {
		/*
		 * Unwind the envelope even if a reconfigure has moved the
		 * port to the hardware path mid-envelope: a stranded
		 * tx_state would make a later reconfigure believe a
		 * software envelope still owns RTS.
		 */
		max310x_delayed_stop_tx(port);
		return;
	}

	/*
	 * It's a circ buffer -- wrap around.
	 * We could do that in one SPI transaction, but meh.
	 */
	while (!kfifo_is_empty(&tport->xmit_fifo)) {
		unsigned int txlen, to_send;
		unsigned char *tail;

		/* Limit to space available in TX FIFO */
		txlen = max310x_port_read(port, MAX310X_TXFIFOLVL_REG);
		txlen = port->fifosize - txlen;
		if (!txlen)
			break;

		to_send = kfifo_out_linear_ptr(&tport->xmit_fifo, &tail, txlen);
		max310x_batch_write(port, tail, to_send);
		uart_xmit_advance(port, to_send);
	}

	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

/*
 * Begin a software-timed RTS envelope: set the before-send phase and queue the
 * rts worker to assert RTS. tx_state is set synchronously here (start_tx() holds
 * port.lock) so close()/shutdown can see an envelope is in flight; rts_work then
 * asserts RTS and arms the before-send timer (see there).
 */
static void max310x_delayed_start_tx(struct uart_port *port)
{
	struct max310x_one *one = to_max310x_port(port);

	WRITE_ONCE(one->tx_state, MAX310X_TX_WAIT_BEFORE_SEND);
	one->cancel_tx_delay_tmr = false;
	schedule_work(&one->rts_work);
}

/* called with port.lock taken and irqs off */
static void max310x_start_tx(struct uart_port *port)
{
	struct max310x_one *one = to_max310x_port(port);

	/* A teardown is in progress; nothing may start an envelope or TX. */
	if (one->tx_teardown)
		return;

	/*
	 * An RS485 reconfigure is scheduled but not applied yet: transmitting
	 * now would use the old path against half-programmed registers - a
	 * TIOCSRS485 switching paths followed immediately by a write puts the
	 * data on the wire with the transceiver released. Leave the data in
	 * the kfifo; rs_work restarts TX once the configuration is applied.
	 */
	if (one->rs485_pending)
		return;

	if (READ_ONCE(one->sw_rts_during_tx)) {
		/*
		 * The before- and after-send phases share one delay timer. If an
		 * after-send release is pending, cancel it before starting a new
		 * TX so the just-asserted RTS is not yanked; re-arming the timer
		 * for the before-send phase then supersedes the release.
		 */
		int res = 0;

		if (READ_ONCE(one->tx_state) == MAX310X_TX_WAIT_AFTER_SEND)
			res = hrtimer_try_to_cancel(&one->tx_delay_tmr);
		if (unlikely(res == -1)) {
			one->cancel_tx_delay_tmr = true;
			uart_port_unlock(port);
			hrtimer_cancel(&one->tx_delay_tmr);
			uart_port_lock(port);
			/*
			 * The lock was dropped: a teardown may have run to
			 * completion or a reconfigure may have been posted
			 * meanwhile. Re-check before starting.
			 */
			if (one->tx_teardown || one->rs485_pending)
				return;
		}

		/* Already sending: pump the new data, don't rewind. */
		if (READ_ONCE(one->tx_state) == MAX310X_TX_SEND) {
			schedule_work(&one->tx_work);
			return;
		}

		max310x_delayed_start_tx(port);
	} else {
		schedule_work(&one->tx_work);
	}
}

static irqreturn_t max310x_port_irq(struct max310x_port *s, int portno)
{
	struct uart_port *port = &s->p[portno].port;
	irqreturn_t res = IRQ_NONE;

	do {
		unsigned int ists, lsr, rxlen;

		/* Read IRQ status & RX FIFO level */
		ists = max310x_port_read(port, MAX310X_IRQSTS_REG);
		rxlen = max310x_port_read(port, MAX310X_RXFIFOLVL_REG);
		if (!ists && !rxlen)
			break;

		res = IRQ_HANDLED;

		if (ists & MAX310X_IRQ_CTS_BIT) {
			lsr = max310x_port_read(port, MAX310X_LSR_IRQSTS_REG);
			uart_handle_cts_change(port, lsr & MAX310X_LSR_CTS_BIT);
		}
		if (rxlen)
			max310x_handle_rx(port, rxlen);
		/*
		 * TXEMPTY latches on the FIFO becoming empty, so a stale
		 * interrupt cannot pump data during an RTS setup delay.
		 */
		if (ists & MAX310X_IRQ_TXEMPTY_BIT)
			schedule_work(&s->p[portno].tx_work);
	} while (1);

	return res;
}

static irqreturn_t max310x_ist(int irq, void *dev_id)
{
	struct max310x_port *s = (struct max310x_port *)dev_id;
	bool handled = false;

	if (s->devtype->nr > 1) {
		bool done;

		do {
			unsigned int val = ~0;
			unsigned long irq;
			unsigned int port;

			done = true;

			WARN_ON_ONCE(regmap_read(s->regmap,
						 MAX310X_GLOBALIRQ_REG, &val));

			irq = val;

			for_each_clear_bit(port, &irq, s->devtype->nr) {
				done = false;

				if (max310x_port_irq(s, port) == IRQ_HANDLED)
					handled = true;
			}

		} while (!done);
	} else {
		if (max310x_port_irq(s, 0) == IRQ_HANDLED)
			handled = true;
	}

	return IRQ_RETVAL(handled);
}

static void max310x_tx_proc(struct work_struct *ws)
{
	struct max310x_one *one = container_of(ws, struct max310x_one, tx_work);

	max310x_handle_tx(&one->port);
}

static unsigned int max310x_tx_empty(struct uart_port *port)
{
	u8 lvl = max310x_port_read(port, MAX310X_TXFIFOLVL_REG);

	return lvl ? 0 : TIOCSER_TEMT;
}

static unsigned int max310x_get_mctrl(struct uart_port *port)
{
	/*
	 * DCD and DSR are not wired and CTS/RTS is handled automatically
	 * so just indicate DSR and CAR asserted
	 */
	return TIOCM_DSR | TIOCM_CAR;
}

static void max310x_md_proc(struct work_struct *ws)
{
	struct max310x_one *one = container_of(ws, struct max310x_one, md_work);

	max310x_port_update(&one->port, MAX310X_MODE2_REG,
			    MAX310X_MODE2_LOOPBACK_BIT,
			    (one->port.mctrl & TIOCM_LOOP) ?
			    MAX310X_MODE2_LOOPBACK_BIT : 0);
}

static void max310x_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct max310x_one *one = to_max310x_port(port);

	schedule_work(&one->md_work);
}

/*
 * A reconfigure can move the port off the hardware RTS path while the chip's
 * auto-RTS still owns a transmission in flight; tx_state does not track
 * those. Take such a transfer over before auto-RTS is disabled: enter
 * MAX310X_TX_SEND, so the caller keeps RTS driven across the handover and
 * the normal drain path arms the after-send hold. Returns true when a
 * transmission owns RTS - adopted here or started concurrently - and false
 * when the line is really idle.
 */
static bool max310x_adopt_hw_tx(struct max310x_one *one)
{
	struct uart_port *port = &one->port;

	if (!max310x_port_read(port, MAX310X_TXFIFOLVL_REG) &&
	    kfifo_is_empty(&port->state->port.xmit_fifo))
		return false;

	scoped_guard(spinlock_irqsave, &port->lock) {
		if (one->tx_teardown)
			return false;
		if (READ_ONCE(one->tx_state) != MAX310X_TX_OFF)
			return true;
		WRITE_ONCE(one->tx_state, MAX310X_TX_SEND);
		one->cancel_tx_delay_tmr = false;
	}

	return true;
}

/*
 * Pick hardware or software RTS timing for the current port. The chip can
 * deliver up to 15 bit-times of setup/hold delay via HDPIXDELAY; anything
 * longer (or any RTS polarity the chip cannot produce automatically) must
 * be driven by software via tx_delay_tmr and rts_work.
 */
static void max310x_set_rts_ctl_params(struct max310x_one *one)
{
	const unsigned int max_bit_dly = 15;
	struct uart_port *port = &one->port;
	unsigned int setup = 0, hold = 0;
	u8 mode1 = 0, irda = 0;
	bool sw_rts = false;

	if (!(port->rs485.flags & SER_RS485_ENABLED))
		goto out;

	/* Convert milliseconds to bit-times, rounding up. */
	setup = DIV_ROUND_UP(one->baud * port->rs485.delay_rts_before_send,
			     MSEC_PER_SEC);
	hold  = DIV_ROUND_UP(one->baud * port->rs485.delay_rts_after_send,
			     MSEC_PER_SEC);

	/*
	 * Compare in bit-times: a delay of exactly max_bit_dly bit-times
	 * must stay on the hardware path, which a nanosecond ceiling
	 * computed from the truncated per-bit time would reject. Without
	 * a baud rate the conversion is meaningless - use software.
	 */
	if (!one->baud || setup > max_bit_dly || hold > max_bit_dly ||
	    !!(port->rs485.flags & SER_RS485_RTS_ON_SEND) ==
	    !!(port->rs485.flags & SER_RS485_RTS_AFTER_SEND)) {
		sw_rts = true;
		setup = 0;
		hold  = 0;
	}

out:
	/* Assign once; a transient false would be seen by other readers. */
	WRITE_ONCE(one->sw_rts_during_tx, sw_rts);

	max310x_port_write(port, MAX310X_HDPIXDELAY_REG,
			   MAX310X_HDPIXDELAY_SETUP(setup) |
			   MAX310X_HDPIXDELAY_HOLD(hold));

	/*
	 * A break owns the transceiver: break_ctl() disabled auto-RTS and
	 * drives RTS manually, and restores it from the current
	 * configuration when the break ends. Touching them here would
	 * release the transceiver mid-break.
	 */
	if (one->tx_break)
		return;

	if (port->rs485.flags & SER_RS485_ENABLED) {
		if (sw_rts) {
			/*
			 * Only settle RTS at idle when no transmission owns it.
			 * A reconfigure while one is in flight - rs_work runs
			 * on every TIOCSRS485 - would otherwise release the
			 * transceiver mid-character. A transfer the hardware
			 * path owns is invisible to tx_state - adopt it
			 * instead of settling. The reverse strand also
			 * exists: an envelope the hardware path inherited
			 * mid-envelope leaves tx_state set with nothing
			 * driving the pin.
			 */
			if (READ_ONCE(one->tx_state) != MAX310X_TX_OFF ||
			    max310x_adopt_hw_tx(one)) {
				/*
				 * The transfer may be running under auto-RTS:
				 * assert before auto-RTS is disabled below so
				 * the pin is never released under it, and arm
				 * the after-send hold - with the chip FIFO
				 * already drained no TX-empty interrupt
				 * arrives to arm it.
				 */
				max310x_rts_ctl(port,
						max310x_rts_level(port, true));
				max310x_delayed_stop_tx(port);
			} else {
				max310x_rts_ctl(port,
						max310x_rts_level(port, false));
				/*
				 * serial_core calls set_termios() without
				 * port->lock, so an envelope may have started
				 * while the idle level was written and the
				 * settle can land after its RTS assert.
				 * rts_work re-derives the level from
				 * tx_state; requeue it to converge.
				 */
				if (READ_ONCE(one->tx_state) != MAX310X_TX_OFF)
					schedule_work(&one->rts_work);
			}
		} else {
			mode1 = MAX310X_MODE1_TRNSCVCTRL_BIT;
			/*
			 * The auto-RTS engine asserts RTS high on send; for an
			 * active-low RTS let IRDA.RTSINVERT invert the output
			 * stage.
			 */
			if (!(port->rs485.flags & SER_RS485_RTS_ON_SEND))
				irda = MAX310X_IRDA_RTSINVERT_BIT;
		}
	} else {
		max310x_rts_ctl(port, 0);
	}

	max310x_port_update(port, MAX310X_MODE1_REG,
			    MAX310X_MODE1_TRNSCVCTRL_BIT, mode1);
	max310x_port_update(port, MAX310X_IRDA_REG,
			    MAX310X_IRDA_RTSINVERT_BIT, irda);
}

static void max310x_break_ctl(struct uart_port *port, int break_state)
{
	struct max310x_one *one = to_max310x_port(port);

	one->tx_break = break_state;

	max310x_port_update(port, MAX310X_LCR_REG,
			    MAX310X_LCR_TXBREAK_BIT,
			    break_state ? MAX310X_LCR_TXBREAK_BIT : 0);

	/*
	 * The chip's auto-RTS asserts the transceiver only while FIFO data is
	 * shifting out, and a break is not FIFO data. Disable auto-RTS for the
	 * break duration and drive RTS manually so the break reaches the wire;
	 * restore auto-RTS when the break ends. For an active-low RTS,
	 * IRDA.RTSINVERT already inverts the RTS_ output stage, so break_state
	 * is driven as it is.
	 */
	if (break_state) {
		if (!(port->rs485.flags & SER_RS485_ENABLED))
			return;
		if (READ_ONCE(one->sw_rts_during_tx)) {
			max310x_rts_ctl(port, max310x_rts_level(port, 1));
		} else {
			max310x_port_update(port, MAX310X_MODE1_REG,
					    MAX310X_MODE1_TRNSCVCTRL_BIT, 0);
			max310x_rts_ctl(port, 1);
		}
	} else {
		/*
		 * Reapply the current configuration: a reconfigure that
		 * arrived during the break was deferred by the tx_break
		 * guard, and RS485 may have been disabled outright - the
		 * manually driven RTS must not leak past the break. On the
		 * software path the helper also settles the idle level; on
		 * the hardware path release the manual RTS - auto-RTS owns
		 * the pin again.
		 */
		max310x_set_rts_ctl_params(one);
		if (!READ_ONCE(one->sw_rts_during_tx))
			max310x_rts_ctl(port, 0);
	}
}

static void max310x_set_termios(struct uart_port *port,
				struct ktermios *termios,
				const struct ktermios *old)
{
	unsigned int frame_bits = tty_get_frame_size(termios->c_cflag);
	unsigned int lcr = 0, flow = 0;
	int baud;

	/* Mask termios capabilities we don't support */
	termios->c_cflag &= ~CMSPAR;

	/* Word size */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		break;
	case CS6:
		lcr = MAX310X_LCR_LENGTH0_BIT;
		break;
	case CS7:
		lcr = MAX310X_LCR_LENGTH1_BIT;
		break;
	case CS8:
	default:
		lcr = MAX310X_LCR_LENGTH1_BIT | MAX310X_LCR_LENGTH0_BIT;
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

	/*
	 * Update LCR register. Leave the TX break and RTS bits alone: they are
	 * driven by break_ctl() and by the software-timed RS485 RTS, and a
	 * whole-register write here would end a break in progress or release
	 * the transceiver mid-character.
	 */
	max310x_port_update(port, MAX310X_LCR_REG, MAX310X_LCR_TERMIOS_MASK, lcr);

	/* Set read status mask */
	port->read_status_mask = MAX310X_LSR_RXOVR_BIT;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= MAX310X_LSR_RXPAR_BIT |
					  MAX310X_LSR_FRERR_BIT;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
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

	/*
	 * Disable transmitter before enabling AutoCTS or auto transmitter
	 * flow control
	 */
	if (termios->c_cflag & CRTSCTS || termios->c_iflag & IXOFF) {
		max310x_port_update(port, MAX310X_MODE1_REG,
				    MAX310X_MODE1_TXDIS_BIT,
				    MAX310X_MODE1_TXDIS_BIT);
	}

	port->status &= ~(UPSTAT_AUTOCTS | UPSTAT_AUTORTS | UPSTAT_AUTOXOFF);

	if (termios->c_cflag & CRTSCTS) {
		/* Enable AUTORTS and AUTOCTS */
		port->status |= UPSTAT_AUTOCTS | UPSTAT_AUTORTS;
		flow |= MAX310X_FLOWCTRL_AUTOCTS_BIT |
			MAX310X_FLOWCTRL_AUTORTS_BIT;
	}
	if (termios->c_iflag & IXON)
		flow |= MAX310X_FLOWCTRL_SWFLOW3_BIT |
			MAX310X_FLOWCTRL_SWFLOWEN_BIT;
	if (termios->c_iflag & IXOFF) {
		port->status |= UPSTAT_AUTOXOFF;
		flow |= MAX310X_FLOWCTRL_SWFLOW1_BIT |
			MAX310X_FLOWCTRL_SWFLOWEN_BIT;
	}
	max310x_port_write(port, MAX310X_FLOWCTRL_REG, flow);

	/*
	 * Enable transmitter after disabling AutoCTS and auto transmitter
	 * flow control
	 */
	if (!(termios->c_cflag & CRTSCTS) && !(termios->c_iflag & IXOFF)) {
		max310x_port_update(port, MAX310X_MODE1_REG,
				    MAX310X_MODE1_TXDIS_BIT,
				    0);
	}

	/* Get baud rate generator configuration */
	baud = uart_get_baud_rate(port, termios, old,
				  port->uartclk / 16 / 0xffff,
				  port->uartclk / 4);

	/* Setup baudrate generator */
	baud = max310x_set_baud(port, baud);

	/* Update timeout according to new baud rate */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * Cache the new baud rate and the time it takes to clock out one
	 * character before reprogramming the RS485 RTS delays: the
	 * millisecond-to-bit-time conversion divides by the baud rate, and
	 * taking over an in-flight transfer arms the after-send hold from
	 * the character time.
	 */
	to_max310x_port(port)->baud = baud;
	to_max310x_port(port)->char_time_us =
		DIV_ROUND_UP(USEC_PER_SEC * frame_bits, baud);
	max310x_set_rts_ctl_params(to_max310x_port(port));
}

static void max310x_rs_proc(struct work_struct *ws)
{
	struct max310x_one *one = container_of(ws, struct max310x_one, rs_work);
	unsigned int mode2 = 0;
	unsigned long flags;
	bool chip_tx_empty;

	/*
	 * Serialize against break_ctl() and set_termios(), which run under
	 * port->mutex: the tx_break-guarded register writes must not
	 * straddle a break starting or ending.
	 */
	guard(mutex)(&one->port.state->port.mutex);

	/* Flush an rts_work that read tx_state before the disable cleared it. */
	if (!(one->port.rs485.flags & SER_RS485_ENABLED))
		cancel_work_sync(&one->rts_work);

	max310x_set_rts_ctl_params(one);

	if (one->port.rs485.flags & SER_RS485_ENABLED &&
	    !(one->port.rs485.flags & SER_RS485_RX_DURING_TX))
		mode2 = MAX310X_MODE2_ECHOSUPR_BIT;

	max310x_port_update(&one->port, MAX310X_MODE2_REG,
			    MAX310X_MODE2_ECHOSUPR_BIT, mode2);

	/*
	 * The configuration is applied: release any TX that start_tx()
	 * deferred while the reconfigure was pending, now on the right
	 * path. start_tx() can drop and retake the lock through the
	 * uart_port API - a raw spinlock guard here would unbalance it.
	 */
	chip_tx_empty = !max310x_port_read(&one->port,
					   MAX310X_TXFIFOLVL_REG);

	uart_port_lock_irqsave(&one->port, &flags);
	one->rs485_pending = false;
	if (one->port.x_char ||
	    !kfifo_is_empty(&one->port.state->port.xmit_fifo)) {
		/*
		 * A send state adopted without chip data is the deferred
		 * write itself: restart it as a fresh envelope so the
		 * before-send delay is honoured.
		 */
		if (chip_tx_empty &&
		    READ_ONCE(one->tx_state) == MAX310X_TX_SEND)
			WRITE_ONCE(one->tx_state, MAX310X_TX_OFF);
		max310x_start_tx(&one->port);
	}
	uart_port_unlock_irqrestore(&one->port, flags);
}

/* called with port.lock taken and irqs off */
static int max310x_rs485_config(struct uart_port *port, struct ktermios *termios,
				struct serial_rs485 *rs485)
{
	struct max310x_one *one = to_max310x_port(port);

	rs485->delay_rts_before_send = min(rs485->delay_rts_before_send, 100U);
	rs485->delay_rts_after_send  = min(rs485->delay_rts_after_send,  100U);

	/*
	 * Make sure no SW-timed RTS toggle survives an RS485 disable, even
	 * if the delay timer happens to be running right now.
	 */
	if (!(rs485->flags & SER_RS485_ENABLED)) {
		one->tx_teardown = true;
		one->cancel_tx_delay_tmr = true;
		if (hrtimer_try_to_cancel(&one->tx_delay_tmr) == -1) {
			uart_port_unlock(port);
			hrtimer_cancel(&one->tx_delay_tmr);
			uart_port_lock(port);
		}
		WRITE_ONCE(one->tx_state, MAX310X_TX_OFF);
		one->tx_teardown = false;
	}

	port->rs485 = *rs485;

	one->rs485_pending = true;
	schedule_work(&one->rs_work);

	return 0;
}

static int max310x_startup(struct uart_port *port)
{
	struct max310x_one *one = to_max310x_port(port);
	unsigned int val;

	max310x_power(port, 1);

	/*
	 * Clear a latched break: nothing clears TXBREAK when a port is
	 * closed with a break still asserted, and set_termios() no longer
	 * rewrites it.
	 */
	one->tx_break = false;
	max310x_port_update(port, MAX310X_LCR_REG, MAX310X_LCR_TXBREAK_BIT, 0);

	/* Configure MODE1: re-enable the transmitter shutdown() stopped */
	max310x_port_update(port, MAX310X_MODE1_REG,
			    MAX310X_MODE1_TRNSCVCTRL_BIT |
			    MAX310X_MODE1_TXDIS_BIT, 0);

	/* Configure MODE2 register & Reset FIFOs*/
	val = MAX310X_MODE2_RXEMPTINV_BIT | MAX310X_MODE2_FIFORST_BIT;
	max310x_port_write(port, MAX310X_MODE2_REG, val);
	max310x_port_update(port, MAX310X_MODE2_REG,
			    MAX310X_MODE2_FIFORST_BIT, 0);

	one->tx_teardown = false;

	/*
	 * Configure the RS485 RTS timing (HW auto-RTS vs software-driven) and
	 * the RS485/RS232 mode bits. Don't hardcode HW auto-RTS here - let
	 * max310x_set_rts_ctl_params() pick HW or SW per the configured
	 * delays, otherwise the chip's auto-RTS would override the software
	 * RTS hold and the after-send delay is lost.
	 */
	max310x_set_rts_ctl_params(one);

	if (port->rs485.flags & SER_RS485_ENABLED &&
	    !(port->rs485.flags & SER_RS485_RX_DURING_TX))
		max310x_port_update(port, MAX310X_MODE2_REG,
				    MAX310X_MODE2_ECHOSUPR_BIT,
				    MAX310X_MODE2_ECHOSUPR_BIT);

	/*
	 * Configure flow control levels:
	 *   resume: 48
	 *   halt:   96
	 */
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
	struct max310x_one *one = to_max310x_port(port);

	/*
	 * Gate new transmissions and interrupts first: a concurrent
	 * start_tx() either sees the interlock or happens-before it, and
	 * nothing below may start a new envelope. The timer and the works
	 * stay live for now - they are what completes a running after-send
	 * hold.
	 */
	scoped_guard(spinlock_irqsave, &port->lock)
		one->tx_teardown = true;
	max310x_port_write(port, MAX310X_IRQEN_REG, 0);

	/*
	 * Stop the transmitter: the character in flight completes, data
	 * still queued is abandoned (the next startup() resets the FIFOs).
	 */
	max310x_port_update(port, MAX310X_MODE1_REG,
			    MAX310X_MODE1_TXDIS_BIT, MAX310X_MODE1_TXDIS_BIT);

	if (READ_ONCE(one->sw_rts_during_tx)) {
		/*
		 * Honour a running after-send hold before the port is
		 * powered down. The bound covers the final character -
		 * tx_empty() cannot see the transmit shift register - plus
		 * the hold itself. Data beyond that is abandoned: it is
		 * only still queued when the tty layer was told not to
		 * wait, and the release below drives RTS to idle
		 * regardless.
		 */
		unsigned int tries = port->rs485.delay_rts_after_send +
			2 * DIV_ROUND_UP(one->char_time_us, USEC_PER_MSEC);

		while (READ_ONCE(one->tx_state) != MAX310X_TX_OFF && tries-- > 0)
			fsleep(USEC_PER_MSEC);
	} else {
		/*
		 * Let the character in flight finish (the chip has no
		 * transmitter-idle status), then empty the FIFO: auto-RTS
		 * releases only once the transmitter is empty, and the
		 * power-off below would freeze an asserted pin until the
		 * next open. The hold is at most 15 bit-times.
		 */
		fsleep(one->char_time_us);
		max310x_port_update(port, MAX310X_MODE2_REG,
				    MAX310X_MODE2_FIFORST_BIT,
				    MAX310X_MODE2_FIFORST_BIT);
		max310x_port_update(port, MAX310X_MODE2_REG,
				    MAX310X_MODE2_FIFORST_BIT, 0);
		if (port->rs485.flags & SER_RS485_ENABLED && one->baud) {
			unsigned int hold = DIV_ROUND_UP(one->baud *
					port->rs485.delay_rts_after_send,
					MSEC_PER_SEC);

			fsleep(max(DIV_ROUND_UP((hold + 1) * USEC_PER_SEC,
						one->baud), 100U));
		}
	}

	/*
	 * The wind-down is over: cancel unconditionally. The SW/HW decision
	 * is recomputed on every reconfigure, so a TIOCSRS485 can clear
	 * sw_rts_during_tx while an envelope is still in flight, and
	 * neither the timer nor the works may outlive the port.
	 */
	scoped_guard(spinlock_irqsave, &port->lock)
		one->cancel_tx_delay_tmr = true;
	cancel_work_sync(&one->tx_work);
	hrtimer_cancel(&one->tx_delay_tmr);
	cancel_work_sync(&one->rts_work);
	WRITE_ONCE(one->tx_state, MAX310X_TX_OFF);

	if (READ_ONCE(one->sw_rts_during_tx)) {
		max310x_rts_ctl(port, max310x_rts_level(port, false));
		/*
		 * The power-off below stops the UART clock that moves the
		 * RTS pin: released too late, the write reaches the register
		 * but the pin stays at its old level until the next startup
		 * restarts the clock. Give the release one bit time (at
		 * least 100 us) to reach the pin - the measured propagation
		 * is about one tick of the 16x oversampling clock.
		 */
		fsleep(max(one->char_time_us / 10, 100U));
	}

	max310x_power(port, 0);
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

	for (i = 0; i < s->devtype->nr; i++) {
		uart_suspend_port(&max310x_uart, &s->p[i].port);
		max310x_power(&s->p[i].port, 0);
	}

	return 0;
}

static int __maybe_unused max310x_resume(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < s->devtype->nr; i++) {
		max310x_power(&s->p[i].port, 1);
		uart_resume_port(&max310x_uart, &s->p[i].port);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(max310x_pm_ops, max310x_suspend, max310x_resume);

#ifdef CONFIG_GPIOLIB
static int max310x_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	unsigned int val;
	struct max310x_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[offset / 4].port;

	val = max310x_port_read(port, MAX310X_GPIODATA_REG);

	return !!((val >> 4) & (1 << (offset % 4)));
}

static int max310x_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	struct max310x_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[offset / 4].port;

	max310x_port_update(port, MAX310X_GPIODATA_REG, 1 << (offset % 4),
			    value ? 1 << (offset % 4) : 0);

	return 0;
}

static int max310x_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct max310x_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[offset / 4].port;
	unsigned int val;

	val = max310x_port_read(port, MAX310X_GPIOCFG_REG);

	return val & BIT(offset % 4) ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int max310x_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct max310x_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[offset / 4].port;

	max310x_port_update(port, MAX310X_GPIOCFG_REG, 1 << (offset % 4), 0);

	return 0;
}

static int max310x_gpio_direction_output(struct gpio_chip *chip,
					 unsigned int offset, int value)
{
	struct max310x_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[offset / 4].port;

	max310x_port_update(port, MAX310X_GPIODATA_REG, 1 << (offset % 4),
			    value ? 1 << (offset % 4) : 0);
	max310x_port_update(port, MAX310X_GPIOCFG_REG, 1 << (offset % 4),
			    1 << (offset % 4));

	return 0;
}

static int max310x_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				   unsigned long config)
{
	struct max310x_port *s = gpiochip_get_data(chip);
	struct uart_port *port = &s->p[offset / 4].port;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		max310x_port_update(port, MAX310X_GPIOCFG_REG,
				1 << ((offset % 4) + 4),
				1 << ((offset % 4) + 4));
		return 0;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		max310x_port_update(port, MAX310X_GPIOCFG_REG,
				1 << ((offset % 4) + 4), 0);
		return 0;
	default:
		return -ENOTSUPP;
	}
}
#endif

static const struct serial_rs485 max310x_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND |
		 SER_RS485_RTS_AFTER_SEND | SER_RS485_RX_DURING_TX,
	.delay_rts_before_send = 1,
	.delay_rts_after_send = 1,
};

static int max310x_probe(struct device *dev, const struct max310x_devtype *devtype,
			 const struct max310x_if_cfg *if_cfg,
			 struct regmap *regmaps[], int irq)
{
	unsigned int fmin, fmax, freq;
	int i, ret;
	struct max310x_port *s;
	unsigned int uartclk = 0;
	bool xtal;

	for (i = 0; i < devtype->nr; i++)
		if (IS_ERR(regmaps[i]))
			return PTR_ERR(regmaps[i]);

	/* Alloc port structure */
	s = devm_kzalloc(dev, struct_size(s, p, devtype->nr), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	/* Always ask for fixed clock rate from a property. */
	device_property_read_u32(dev, "clock-frequency", &uartclk);

	xtal = device_property_match_string(dev, "clock-names", "osc") < 0;
	if (xtal)
		s->clk = devm_clk_get_optional(dev, "xtal");
	else
		s->clk = devm_clk_get_optional(dev, "osc");
	if (IS_ERR(s->clk))
		return PTR_ERR(s->clk);

	ret = clk_prepare_enable(s->clk);
	if (ret)
		return ret;

	freq = clk_get_rate(s->clk);
	if (freq == 0)
		freq = uartclk;
	if (freq == 0) {
		ret = dev_err_probe(dev, -EINVAL, "Cannot get clock rate\n");
		goto out_clk;
	}

	if (xtal) {
		fmin = 1000000;
		fmax = 4000000;
	} else {
		fmin = 500000;
		fmax = 35000000;
	}

	/* Check frequency limits */
	if (freq < fmin || freq > fmax) {
		ret = -ERANGE;
		goto out_clk;
	}

	s->regmap = regmaps[0];
	s->devtype = devtype;
	s->if_cfg = if_cfg;
	dev_set_drvdata(dev, s);

	/* Check device to ensure we are talking to what we expect */
	ret = max310x_detect(dev);
	if (ret)
		goto out_clk;

	for (i = 0; i < devtype->nr; i++) {
		unsigned int val;

		/* Reset port */
		regmap_write(regmaps[i], MAX310X_MODE2_REG,
			     MAX310X_MODE2_RST_BIT);
		/* Clear port reset */
		regmap_write(regmaps[i], MAX310X_MODE2_REG, 0);

		/* Wait for port startup */
		ret = regmap_read_poll_timeout(regmaps[i], MAX310X_BRGDIVLSB_REG,
					       val, val == 0x01,
					       MAX310X_PORT_STARTUP_SLEEP_US,
					       MAX310X_PORT_STARTUP_TIMEOUT_US);
		if (ret) {
			ret = dev_err_probe(dev, -EAGAIN, "port reset failed\n");
			goto out_uart;
		}

		regmap_write(regmaps[i], MAX310X_MODE1_REG, devtype->mode1);
	}

	ret = max310x_set_ref_clk(dev, s, freq, &uartclk, xtal);
	if (ret < 0)
		goto out_uart;

	dev_dbg(dev, "Reference clock set to %i Hz\n", uartclk);

	/*
	 * Set up each port's state before registering the gpiochip,
	 * since the gpiochip callbacks will read s->p[i].regmap as
	 * soon as gpiolib exposes the controller.
	 */
	for (i = 0; i < devtype->nr; i++) {
		s->p[i].port.dev	= dev;
		s->p[i].port.irq	= irq;
		s->p[i].port.type	= PORT_MAX310X;
		s->p[i].port.fifosize	= MAX310X_FIFO_SIZE;
		s->p[i].port.flags	= UPF_FIXED_TYPE | UPF_LOW_LATENCY;
		s->p[i].port.iotype	= UPIO_BUS;
		s->p[i].port.uartclk	= uartclk;
		s->p[i].port.rs485_config = max310x_rs485_config;
		s->p[i].port.rs485_supported = max310x_rs485_supported;
		s->p[i].port.ops	= &max310x_ops;
		s->p[i].regmap		= regmaps[i];

		/* Disable all interrupts */
		max310x_port_write(&s->p[i].port, MAX310X_IRQEN_REG, 0);
		/* Clear IRQ status register */
		max310x_port_read(&s->p[i].port, MAX310X_IRQSTS_REG);
		/* Initialize queue for start TX */
		INIT_WORK(&s->p[i].tx_work, max310x_tx_proc);
		/* Initialize queue for changing LOOPBACK mode */
		INIT_WORK(&s->p[i].md_work, max310x_md_proc);
		/* Initialize queue for changing RS485 mode */
		INIT_WORK(&s->p[i].rs_work, max310x_rs_proc);
		/* Initialize queue for software-driven RTS toggling */
		INIT_WORK(&s->p[i].rts_work, max310x_rts_work_proc);
		hrtimer_setup(&s->p[i].tx_delay_tmr, max310x_tmr_tx,
			      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		s->p[i].tx_state = MAX310X_TX_OFF;
	}

#ifdef CONFIG_GPIOLIB
	/*
	 * Register the GPIO controller before adding the UART ports so
	 * that consumers referencing the chip's own GPIOs from device
	 * tree (for example rs485-term-gpios = <&max310x ...>) can
	 * resolve them at uart_add_one_port() time instead of receiving
	 * -EPROBE_DEFER from their own provider.
	 */
	s->gpio.owner		= THIS_MODULE;
	s->gpio.parent		= dev;
	s->gpio.label		= devtype->name;
	s->gpio.get_direction	= max310x_gpio_get_direction;
	s->gpio.direction_input	= max310x_gpio_direction_input;
	s->gpio.get		= max310x_gpio_get;
	s->gpio.direction_output= max310x_gpio_direction_output;
	s->gpio.set		= max310x_gpio_set;
	s->gpio.set_config	= max310x_gpio_set_config;
	s->gpio.base		= -1;
	s->gpio.ngpio		= devtype->nr * 4;
	s->gpio.can_sleep	= 1;
	ret = devm_gpiochip_add_data(dev, &s->gpio, s);
	if (ret)
		goto out_uart;
#endif

	for (i = 0; i < devtype->nr; i++) {
		struct fwnode_handle *saved_fwnode = dev_fwnode(dev);
		struct device_node *port_np = NULL;
		struct device_node *child;
		unsigned int line;

		line = find_first_zero_bit(max310x_lines, MAX310X_UART_NRMAX);
		if (line == MAX310X_UART_NRMAX) {
			ret = -ERANGE;
			goto out_uart;
		}
		s->p[i].port.line = line;

		/* Locate the matching "serial@i" DT subnode, if any. */
		for_each_available_child_of_node(dev->of_node, child) {
			u32 reg;

			if (!of_node_name_eq(child, "serial"))
				continue;
			if (of_property_read_u32(child, "reg", &reg))
				continue;
			if (reg == i) {
				port_np = child;
				break;
			}
		}

		/*
		 * Temporarily retarget dev's fwnode to the per-port subnode
		 * so uart_get_rs485_mode() picks up the per-port properties.
		 * For single-port variants, fall back to the chip's own
		 * fwnode so legacy DTs that declare rs485 properties at the
		 * top level keep working.
		 */
		if (port_np) {
			device_set_node(dev, of_fwnode_handle(port_np));
			ret = uart_get_rs485_mode(&s->p[i].port);
			device_set_node(dev, saved_fwnode);
			of_node_put(port_np);
			if (ret)
				goto out_uart;
		} else if (devtype->nr == 1) {
			ret = uart_get_rs485_mode(&s->p[i].port);
			if (ret)
				goto out_uart;
		}

		/* Register port */
		ret = uart_add_one_port(&max310x_uart, &s->p[i].port);
		if (ret)
			goto out_uart;

		set_bit(line, max310x_lines);

		/* Go to suspend mode */
		max310x_power(&s->p[i].port, 0);
	}

	/* Setup interrupt */
	ret = devm_request_threaded_irq(dev, irq, NULL, max310x_ist,
					IRQF_ONESHOT | IRQF_SHARED, dev_name(dev), s);
	if (!ret)
		return 0;

	dev_err(dev, "Unable to request IRQ %i\n", irq);

out_uart:
	for (i = 0; i < devtype->nr; i++) {
		if (test_and_clear_bit(s->p[i].port.line, max310x_lines))
			uart_remove_one_port(&max310x_uart, &s->p[i].port);
	}

out_clk:
	clk_disable_unprepare(s->clk);

	return ret;
}

static void max310x_remove(struct device *dev)
{
	struct max310x_port *s = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < s->devtype->nr; i++) {
		hrtimer_cancel(&s->p[i].tx_delay_tmr);
		cancel_work_sync(&s->p[i].rts_work);
		cancel_work_sync(&s->p[i].tx_work);
		cancel_work_sync(&s->p[i].md_work);
		cancel_work_sync(&s->p[i].rs_work);

		if (test_and_clear_bit(s->p[i].port.line, max310x_lines))
			uart_remove_one_port(&max310x_uart, &s->p[i].port);

		max310x_power(&s->p[i].port, 0);
	}

	clk_disable_unprepare(s->clk);
}

static const struct of_device_id __maybe_unused max310x_dt_ids[] = {
	{ .compatible = "maxim,max3107",	.data = &max3107_devtype, },
	{ .compatible = "maxim,max3108",	.data = &max3108_devtype, },
	{ .compatible = "maxim,max3109",	.data = &max3109_devtype, },
	{ .compatible = "maxim,max14830",	.data = &max14830_devtype },
	{ }
};
MODULE_DEVICE_TABLE(of, max310x_dt_ids);

static const char *max310x_regmap_name(u8 port_id)
{
	switch (port_id) {
	case 0:	return "port0";
	case 1:	return "port1";
	case 2:	return "port2";
	case 3:	return "port3";
	default:
		WARN_ON(true);
		return NULL;
	}
}

#if IS_ENABLED(CONFIG_SPI_MASTER)

static struct regmap_config regcfg = {
	.reg_bits = 8,
	.val_bits = 8,
	.write_flag_mask = MAX310X_WRITE_BIT,
	.cache_type = REGCACHE_MAPLE,
	.max_register = MAX310X_REG_1F,
	.writeable_reg = max310x_reg_writeable,
	.volatile_reg = max310x_reg_volatile,
	.precious_reg = max310x_reg_precious,
	.writeable_noinc_reg = max310x_reg_noinc,
	.readable_noinc_reg = max310x_reg_noinc,
	.max_raw_read = MAX310X_FIFO_SIZE,
	.max_raw_write = MAX310X_FIFO_SIZE,
};

static int max310x_spi_extended_reg_enable(struct device *dev, bool enable)
{
	struct max310x_port *s = dev_get_drvdata(dev);

	return regmap_write(s->regmap, MAX310X_GLOBALCMD_REG,
			    enable ? MAX310X_EXTREG_ENBL : MAX310X_EXTREG_DSBL);
}

static const struct max310x_if_cfg __maybe_unused max310x_spi_if_cfg = {
	.extended_reg_enable = max310x_spi_extended_reg_enable,
	.rev_id_offset = MAX310X_EXTREG_START,
};

static int max310x_spi_probe(struct spi_device *spi)
{
	const struct max310x_devtype *devtype;
	struct regmap *regmaps[MAX310X_MAX_PORTS];
	unsigned int i;
	int ret;

	/* Setup SPI bus */
	spi->bits_per_word	= 8;
	spi->mode		= spi->mode ? : SPI_MODE_0;
	spi->max_speed_hz	= spi->max_speed_hz ? : 26000000;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	devtype = spi_get_device_match_data(spi);
	if (!devtype)
		return dev_err_probe(&spi->dev, -ENODEV, "Failed to match device\n");

	for (i = 0; i < devtype->nr; i++) {
		u8 port_mask = i * 0x20;

		regcfg.name = max310x_regmap_name(i);
		regcfg.read_flag_mask = port_mask;
		regcfg.write_flag_mask = port_mask | MAX310X_WRITE_BIT;
		regmaps[i] = devm_regmap_init_spi(spi, &regcfg);
	}

	return max310x_probe(&spi->dev, devtype, &max310x_spi_if_cfg, regmaps, spi->irq);
}

static void max310x_spi_remove(struct spi_device *spi)
{
	max310x_remove(&spi->dev);
}

static const struct spi_device_id max310x_id_table[] = {
	{ "max3107",	(kernel_ulong_t)&max3107_devtype, },
	{ "max3108",	(kernel_ulong_t)&max3108_devtype, },
	{ "max3109",	(kernel_ulong_t)&max3109_devtype, },
	{ "max14830",	(kernel_ulong_t)&max14830_devtype, },
	{ }
};
MODULE_DEVICE_TABLE(spi, max310x_id_table);

static struct spi_driver max310x_spi_driver = {
	.driver = {
		.name		= MAX310X_NAME,
		.of_match_table	= max310x_dt_ids,
		.pm		= &max310x_pm_ops,
	},
	.probe		= max310x_spi_probe,
	.remove		= max310x_spi_remove,
	.id_table	= max310x_id_table,
};
#endif

#if IS_ENABLED(CONFIG_I2C)

static int max310x_i2c_extended_reg_enable(struct device *dev, bool enable)
{
	return 0;
}

static struct regmap_config regcfg_i2c = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_MAPLE,
	.writeable_reg = max310x_reg_writeable,
	.volatile_reg = max310x_reg_volatile,
	.precious_reg = max310x_reg_precious,
	.max_register = MAX310X_REVID_EXTREG,
	.writeable_noinc_reg = max310x_reg_noinc,
	.readable_noinc_reg = max310x_reg_noinc,
	.max_raw_read = MAX310X_FIFO_SIZE,
	.max_raw_write = MAX310X_FIFO_SIZE,
};

static const struct max310x_if_cfg max310x_i2c_if_cfg = {
	.extended_reg_enable = max310x_i2c_extended_reg_enable,
	.rev_id_offset = 0, /* No offset in I2C mode. */
};

static unsigned short max310x_i2c_slave_addr(unsigned short addr,
					     unsigned int nr)
{
	/*
	 * For MAX14830 and MAX3109, the slave address depends on what the
	 * A0 and A1 pins are tied to.
	 * See Table I2C Address Map of the datasheet.
	 * Based on that table, the following formulas were determined:
	 *   UART1 - UART0 = 0x10
	 *   UART2 - UART1 = 0x20 + 0x10
	 *   UART3 - UART2 = 0x10
	 */

	addr -= nr * 0x10;

	if (nr >= 2)
		addr -= 0x20;

	return addr;
}

static int max310x_i2c_probe(struct i2c_client *client)
{
	const struct max310x_devtype *devtype;
	struct i2c_client *port_client;
	struct regmap *regmaps[MAX310X_MAX_PORTS];
	unsigned int i;
	u8 port_addr;

	devtype = i2c_get_match_data(client);
	if (!devtype)
		return dev_err_probe(&client->dev, -ENODEV, "Failed to match device\n");

	if (client->addr < devtype->slave_addr.min ||
	    client->addr > devtype->slave_addr.max)
		return dev_err_probe(&client->dev, -EINVAL,
				     "Slave addr 0x%x outside of range [0x%x, 0x%x]\n",
				     client->addr, devtype->slave_addr.min,
				     devtype->slave_addr.max);

	regcfg_i2c.name = max310x_regmap_name(0);
	regmaps[0] = devm_regmap_init_i2c(client, &regcfg_i2c);

	for (i = 1; i < devtype->nr; i++) {
		port_addr = max310x_i2c_slave_addr(client->addr, i);
		port_client = devm_i2c_new_dummy_device(&client->dev,
							client->adapter,
							port_addr);
		if (IS_ERR(port_client))
			return PTR_ERR(port_client);

		regcfg_i2c.name = max310x_regmap_name(i);
		regmaps[i] = devm_regmap_init_i2c(port_client, &regcfg_i2c);
	}

	return max310x_probe(&client->dev, devtype, &max310x_i2c_if_cfg,
			     regmaps, client->irq);
}

static void max310x_i2c_remove(struct i2c_client *client)
{
	max310x_remove(&client->dev);
}

static const struct i2c_device_id max310x_i2c_id_table[] = {
	{ .name = "max3107", .driver_data = (kernel_ulong_t)&max3107_devtype },
	{ .name = "max3108", .driver_data = (kernel_ulong_t)&max3108_devtype },
	{ .name = "max3109", .driver_data = (kernel_ulong_t)&max3109_devtype },
	{ .name = "max14830", .driver_data = (kernel_ulong_t)&max14830_devtype },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max310x_i2c_id_table);

static struct i2c_driver max310x_i2c_driver = {
	.driver = {
		.name		= MAX310X_NAME,
		.of_match_table	= max310x_dt_ids,
		.pm		= &max310x_pm_ops,
	},
	.probe		= max310x_i2c_probe,
	.remove		= max310x_i2c_remove,
	.id_table	= max310x_i2c_id_table,
};
#endif

static int __init max310x_uart_init(void)
{
	int ret;

	bitmap_zero(max310x_lines, MAX310X_UART_NRMAX);

	ret = uart_register_driver(&max310x_uart);
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&max310x_spi_driver);
	if (ret)
		goto err_spi_register;
#endif

#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&max310x_i2c_driver);
	if (ret)
		goto err_i2c_register;
#endif

	return 0;

#if IS_ENABLED(CONFIG_I2C)
err_i2c_register:
#endif
#if IS_ENABLED(CONFIG_SPI_MASTER)
	spi_unregister_driver(&max310x_spi_driver);
err_spi_register:
#endif
	uart_unregister_driver(&max310x_uart);

	return ret;
}
module_init(max310x_uart_init);

static void __exit max310x_uart_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&max310x_i2c_driver);
#endif

#if IS_ENABLED(CONFIG_SPI_MASTER)
	spi_unregister_driver(&max310x_spi_driver);
#endif

	uart_unregister_driver(&max310x_uart);
}
module_exit(max310x_uart_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("MAX310X serial driver");
