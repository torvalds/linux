// SPDX-License-Identifier: GPL-2.0-only
/* CAN bus driver for Microchip 251x/25625 CAN Controller with SPI Interface
 *
 * MCP2510 support and bug fixes by Christian Pellegrin
 * <chripell@evolware.org>
 *
 * Copyright 2009 Christian Pellegrin EVOL S.r.l.
 *
 * Copyright 2007 Raymarine UK, Ltd. All Rights Reserved.
 * Written under contract by:
 *   Chris Elston, Katalix Systems, Ltd.
 *
 * Based on Microchip MCP251x CAN controller driver written by
 * David Vrabel, Copyright 2006 Arcom Control Systems Ltd.
 *
 * Based on CAN bus driver for the CCAN controller written by
 * - Sascha Hauer, Marc Kleine-Budde, Pengutronix
 * - Simon Kallweit, intefo AG
 * Copyright 2007
 */

#include <linux/bitfield.h>
#include <linux/can/core.h>
#include <linux/can/dev.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/freezer.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

/* SPI interface instruction set */
#define INSTRUCTION_WRITE	0x02
#define INSTRUCTION_READ	0x03
#define INSTRUCTION_BIT_MODIFY	0x05
#define INSTRUCTION_LOAD_TXB(n)	(0x40 + 2 * (n))
#define INSTRUCTION_READ_RXB(n)	(((n) == 0) ? 0x90 : 0x94)
#define INSTRUCTION_RESET	0xC0
#define RTS_TXB0		0x01
#define RTS_TXB1		0x02
#define RTS_TXB2		0x04
#define INSTRUCTION_RTS(n)	(0x80 | ((n) & 0x07))

/* MPC251x registers */
#define BFPCTRL			0x0c
#  define BFPCTRL_B0BFM		BIT(0)
#  define BFPCTRL_B1BFM		BIT(1)
#  define BFPCTRL_BFM(n)	(BFPCTRL_B0BFM << (n))
#  define BFPCTRL_BFM_MASK	GENMASK(1, 0)
#  define BFPCTRL_B0BFE		BIT(2)
#  define BFPCTRL_B1BFE		BIT(3)
#  define BFPCTRL_BFE(n)	(BFPCTRL_B0BFE << (n))
#  define BFPCTRL_BFE_MASK	GENMASK(3, 2)
#  define BFPCTRL_B0BFS		BIT(4)
#  define BFPCTRL_B1BFS		BIT(5)
#  define BFPCTRL_BFS(n)	(BFPCTRL_B0BFS << (n))
#  define BFPCTRL_BFS_MASK	GENMASK(5, 4)
#define TXRTSCTRL		0x0d
#  define TXRTSCTRL_B0RTSM	BIT(0)
#  define TXRTSCTRL_B1RTSM	BIT(1)
#  define TXRTSCTRL_B2RTSM	BIT(2)
#  define TXRTSCTRL_RTSM(n)	(TXRTSCTRL_B0RTSM << (n))
#  define TXRTSCTRL_RTSM_MASK	GENMASK(2, 0)
#  define TXRTSCTRL_B0RTS	BIT(3)
#  define TXRTSCTRL_B1RTS	BIT(4)
#  define TXRTSCTRL_B2RTS	BIT(5)
#  define TXRTSCTRL_RTS(n)	(TXRTSCTRL_B0RTS << (n))
#  define TXRTSCTRL_RTS_MASK	GENMASK(5, 3)
#define CANSTAT	      0x0e
#define CANCTRL	      0x0f
#  define CANCTRL_REQOP_MASK	    0xe0
#  define CANCTRL_REQOP_CONF	    0x80
#  define CANCTRL_REQOP_LISTEN_ONLY 0x60
#  define CANCTRL_REQOP_LOOPBACK    0x40
#  define CANCTRL_REQOP_SLEEP	    0x20
#  define CANCTRL_REQOP_NORMAL	    0x00
#  define CANCTRL_OSM		    0x08
#  define CANCTRL_ABAT		    0x10
#define TEC	      0x1c
#define REC	      0x1d
#define CNF1	      0x2a
#  define CNF1_SJW_SHIFT   6
#define CNF2	      0x29
#  define CNF2_BTLMODE	   0x80
#  define CNF2_SAM         0x40
#  define CNF2_PS1_SHIFT   3
#define CNF3	      0x28
#  define CNF3_SOF	   0x08
#  define CNF3_WAKFIL	   0x04
#  define CNF3_PHSEG2_MASK 0x07
#define CANINTE	      0x2b
#  define CANINTE_MERRE 0x80
#  define CANINTE_WAKIE 0x40
#  define CANINTE_ERRIE 0x20
#  define CANINTE_TX2IE 0x10
#  define CANINTE_TX1IE 0x08
#  define CANINTE_TX0IE 0x04
#  define CANINTE_RX1IE 0x02
#  define CANINTE_RX0IE 0x01
#define CANINTF	      0x2c
#  define CANINTF_MERRF 0x80
#  define CANINTF_WAKIF 0x40
#  define CANINTF_ERRIF 0x20
#  define CANINTF_TX2IF 0x10
#  define CANINTF_TX1IF 0x08
#  define CANINTF_TX0IF 0x04
#  define CANINTF_RX1IF 0x02
#  define CANINTF_RX0IF 0x01
#  define CANINTF_RX (CANINTF_RX0IF | CANINTF_RX1IF)
#  define CANINTF_TX (CANINTF_TX2IF | CANINTF_TX1IF | CANINTF_TX0IF)
#  define CANINTF_ERR (CANINTF_ERRIF)
#define EFLG	      0x2d
#  define EFLG_EWARN	0x01
#  define EFLG_RXWAR	0x02
#  define EFLG_TXWAR	0x04
#  define EFLG_RXEP	0x08
#  define EFLG_TXEP	0x10
#  define EFLG_TXBO	0x20
#  define EFLG_RX0OVR	0x40
#  define EFLG_RX1OVR	0x80
#define TXBCTRL(n)  (((n) * 0x10) + 0x30 + TXBCTRL_OFF)
#  define TXBCTRL_ABTF	0x40
#  define TXBCTRL_MLOA	0x20
#  define TXBCTRL_TXERR 0x10
#  define TXBCTRL_TXREQ 0x08
#define TXBSIDH(n)  (((n) * 0x10) + 0x30 + TXBSIDH_OFF)
#  define SIDH_SHIFT    3
#define TXBSIDL(n)  (((n) * 0x10) + 0x30 + TXBSIDL_OFF)
#  define SIDL_SID_MASK    7
#  define SIDL_SID_SHIFT   5
#  define SIDL_EXIDE_SHIFT 3
#  define SIDL_EID_SHIFT   16
#  define SIDL_EID_MASK    3
#define TXBEID8(n)  (((n) * 0x10) + 0x30 + TXBEID8_OFF)
#define TXBEID0(n)  (((n) * 0x10) + 0x30 + TXBEID0_OFF)
#define TXBDLC(n)   (((n) * 0x10) + 0x30 + TXBDLC_OFF)
#  define DLC_RTR_SHIFT    6
#define TXBCTRL_OFF 0
#define TXBSIDH_OFF 1
#define TXBSIDL_OFF 2
#define TXBEID8_OFF 3
#define TXBEID0_OFF 4
#define TXBDLC_OFF  5
#define TXBDAT_OFF  6
#define RXBCTRL(n)  (((n) * 0x10) + 0x60 + RXBCTRL_OFF)
#  define RXBCTRL_BUKT	0x04
#  define RXBCTRL_RXM0	0x20
#  define RXBCTRL_RXM1	0x40
#define RXBSIDH(n)  (((n) * 0x10) + 0x60 + RXBSIDH_OFF)
#  define RXBSIDH_SHIFT 3
#define RXBSIDL(n)  (((n) * 0x10) + 0x60 + RXBSIDL_OFF)
#  define RXBSIDL_IDE   0x08
#  define RXBSIDL_SRR   0x10
#  define RXBSIDL_EID   3
#  define RXBSIDL_SHIFT 5
#define RXBEID8(n)  (((n) * 0x10) + 0x60 + RXBEID8_OFF)
#define RXBEID0(n)  (((n) * 0x10) + 0x60 + RXBEID0_OFF)
#define RXBDLC(n)   (((n) * 0x10) + 0x60 + RXBDLC_OFF)
#  define RXBDLC_LEN_MASK  0x0f
#  define RXBDLC_RTR       0x40
#define RXBCTRL_OFF 0
#define RXBSIDH_OFF 1
#define RXBSIDL_OFF 2
#define RXBEID8_OFF 3
#define RXBEID0_OFF 4
#define RXBDLC_OFF  5
#define RXBDAT_OFF  6
#define RXFSID(n) ((n < 3) ? 0 : 4)
#define RXFSIDH(n) ((n) * 4 + RXFSID(n))
#define RXFSIDL(n) ((n) * 4 + 1 + RXFSID(n))
#define RXFEID8(n) ((n) * 4 + 2 + RXFSID(n))
#define RXFEID0(n) ((n) * 4 + 3 + RXFSID(n))
#define RXMSIDH(n) ((n) * 4 + 0x20)
#define RXMSIDL(n) ((n) * 4 + 0x21)
#define RXMEID8(n) ((n) * 4 + 0x22)
#define RXMEID0(n) ((n) * 4 + 0x23)

#define GET_BYTE(val, byte)			\
	(((val) >> ((byte) * 8)) & 0xff)
#define SET_BYTE(val, byte)			\
	(((val) & 0xff) << ((byte) * 8))

/* Buffer size required for the largest SPI transfer (i.e., reading a
 * frame)
 */
#define CAN_FRAME_MAX_DATA_LEN	8
#define SPI_TRANSFER_BUF_LEN	(6 + CAN_FRAME_MAX_DATA_LEN)
#define CAN_FRAME_MAX_BITS	128

#define TX_ECHO_SKB_MAX	1

#define MCP251X_OST_DELAY_MS	(5)

#define DEVICE_NAME "mcp251x"

static const struct can_bittiming_const mcp251x_bittiming_const = {
	.name = DEVICE_NAME,
	.tseg1_min = 3,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

enum mcp251x_model {
	CAN_MCP251X_MCP2510	= 0x2510,
	CAN_MCP251X_MCP2515	= 0x2515,
	CAN_MCP251X_MCP25625	= 0x25625,
};

struct mcp251x_priv {
	struct can_priv	   can;
	struct net_device *net;
	struct spi_device *spi;
	enum mcp251x_model model;

	struct mutex mcp_lock; /* SPI device lock */

	u8 *spi_tx_buf;
	u8 *spi_rx_buf;

	struct sk_buff *tx_skb;

	struct workqueue_struct *wq;
	struct work_struct tx_work;
	struct work_struct restart_work;

	int force_quit;
	int after_suspend;
#define AFTER_SUSPEND_UP 1
#define AFTER_SUSPEND_DOWN 2
#define AFTER_SUSPEND_POWER 4
#define AFTER_SUSPEND_RESTART 8
	int restart_tx;
	bool tx_busy;

	struct regulator *power;
	struct regulator *transceiver;
	struct clk *clk;
#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio;
	u8 reg_bfpctrl;
#endif
};

#define MCP251X_IS(_model) \
static inline int mcp251x_is_##_model(struct spi_device *spi) \
{ \
	struct mcp251x_priv *priv = spi_get_drvdata(spi); \
	return priv->model == CAN_MCP251X_MCP##_model; \
}

MCP251X_IS(2510);

static void mcp251x_clean(struct net_device *net)
{
	struct mcp251x_priv *priv = netdev_priv(net);

	if (priv->tx_skb || priv->tx_busy)
		net->stats.tx_errors++;
	dev_kfree_skb(priv->tx_skb);
	if (priv->tx_busy)
		can_free_echo_skb(priv->net, 0, NULL);
	priv->tx_skb = NULL;
	priv->tx_busy = false;
}

/* Note about handling of error return of mcp251x_spi_trans: accessing
 * registers via SPI is not really different conceptually than using
 * normal I/O assembler instructions, although it's much more
 * complicated from a practical POV. So it's not advisable to always
 * check the return value of this function. Imagine that every
 * read{b,l}, write{b,l} and friends would be bracketed in "if ( < 0)
 * error();", it would be a great mess (well there are some situation
 * when exception handling C++ like could be useful after all). So we
 * just check that transfers are OK at the beginning of our
 * conversation with the chip and to avoid doing really nasty things
 * (like injecting bogus packets in the network stack).
 */
static int mcp251x_spi_trans(struct spi_device *spi, int len)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	struct spi_transfer t = {
		.tx_buf = priv->spi_tx_buf,
		.rx_buf = priv->spi_rx_buf,
		.len = len,
		.cs_change = 0,
	};
	struct spi_message m;
	int ret;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	ret = spi_sync(spi, &m);
	if (ret)
		dev_err(&spi->dev, "spi transfer failed: ret = %d\n", ret);
	return ret;
}

static int mcp251x_spi_write(struct spi_device *spi, int len)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	int ret;

	ret = spi_write(spi, priv->spi_tx_buf, len);
	if (ret)
		dev_err(&spi->dev, "spi write failed: ret = %d\n", ret);

	return ret;
}

static u8 mcp251x_read_reg(struct spi_device *spi, u8 reg)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	u8 val = 0;

	priv->spi_tx_buf[0] = INSTRUCTION_READ;
	priv->spi_tx_buf[1] = reg;

	if (spi->controller->flags & SPI_CONTROLLER_HALF_DUPLEX) {
		spi_write_then_read(spi, priv->spi_tx_buf, 2, &val, 1);
	} else {
		mcp251x_spi_trans(spi, 3);
		val = priv->spi_rx_buf[2];
	}

	return val;
}

static void mcp251x_read_2regs(struct spi_device *spi, u8 reg, u8 *v1, u8 *v2)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);

	priv->spi_tx_buf[0] = INSTRUCTION_READ;
	priv->spi_tx_buf[1] = reg;

	if (spi->controller->flags & SPI_CONTROLLER_HALF_DUPLEX) {
		u8 val[2] = { 0 };

		spi_write_then_read(spi, priv->spi_tx_buf, 2, val, 2);
		*v1 = val[0];
		*v2 = val[1];
	} else {
		mcp251x_spi_trans(spi, 4);

		*v1 = priv->spi_rx_buf[2];
		*v2 = priv->spi_rx_buf[3];
	}
}

static void mcp251x_write_reg(struct spi_device *spi, u8 reg, u8 val)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);

	priv->spi_tx_buf[0] = INSTRUCTION_WRITE;
	priv->spi_tx_buf[1] = reg;
	priv->spi_tx_buf[2] = val;

	mcp251x_spi_write(spi, 3);
}

static void mcp251x_write_2regs(struct spi_device *spi, u8 reg, u8 v1, u8 v2)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);

	priv->spi_tx_buf[0] = INSTRUCTION_WRITE;
	priv->spi_tx_buf[1] = reg;
	priv->spi_tx_buf[2] = v1;
	priv->spi_tx_buf[3] = v2;

	mcp251x_spi_write(spi, 4);
}

static void mcp251x_write_bits(struct spi_device *spi, u8 reg,
			       u8 mask, u8 val)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);

	priv->spi_tx_buf[0] = INSTRUCTION_BIT_MODIFY;
	priv->spi_tx_buf[1] = reg;
	priv->spi_tx_buf[2] = mask;
	priv->spi_tx_buf[3] = val;

	mcp251x_spi_write(spi, 4);
}

static u8 mcp251x_read_stat(struct spi_device *spi)
{
	return mcp251x_read_reg(spi, CANSTAT) & CANCTRL_REQOP_MASK;
}

#define mcp251x_read_stat_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(mcp251x_read_stat, addr, val, cond, \
			   delay_us, timeout_us)

#ifdef CONFIG_GPIOLIB
enum {
	MCP251X_GPIO_TX0RTS = 0,		/* inputs */
	MCP251X_GPIO_TX1RTS,
	MCP251X_GPIO_TX2RTS,
	MCP251X_GPIO_RX0BF,			/* outputs */
	MCP251X_GPIO_RX1BF,
};

#define MCP251X_GPIO_INPUT_MASK \
	GENMASK(MCP251X_GPIO_TX2RTS, MCP251X_GPIO_TX0RTS)
#define MCP251X_GPIO_OUTPUT_MASK \
	GENMASK(MCP251X_GPIO_RX1BF, MCP251X_GPIO_RX0BF)

static const char * const mcp251x_gpio_names[] = {
	[MCP251X_GPIO_TX0RTS] = "TX0RTS",	/* inputs */
	[MCP251X_GPIO_TX1RTS] = "TX1RTS",
	[MCP251X_GPIO_TX2RTS] = "TX2RTS",
	[MCP251X_GPIO_RX0BF] = "RX0BF",		/* outputs */
	[MCP251X_GPIO_RX1BF] = "RX1BF",
};

static inline bool mcp251x_gpio_is_input(unsigned int offset)
{
	return offset <= MCP251X_GPIO_TX2RTS;
}

static int mcp251x_gpio_request(struct gpio_chip *chip,
				unsigned int offset)
{
	struct mcp251x_priv *priv = gpiochip_get_data(chip);
	u8 val;

	/* nothing to be done for inputs */
	if (mcp251x_gpio_is_input(offset))
		return 0;

	val = BFPCTRL_BFE(offset - MCP251X_GPIO_RX0BF);

	mutex_lock(&priv->mcp_lock);
	mcp251x_write_bits(priv->spi, BFPCTRL, val, val);
	mutex_unlock(&priv->mcp_lock);

	priv->reg_bfpctrl |= val;

	return 0;
}

static void mcp251x_gpio_free(struct gpio_chip *chip,
			      unsigned int offset)
{
	struct mcp251x_priv *priv = gpiochip_get_data(chip);
	u8 val;

	/* nothing to be done for inputs */
	if (mcp251x_gpio_is_input(offset))
		return;

	val = BFPCTRL_BFE(offset - MCP251X_GPIO_RX0BF);

	mutex_lock(&priv->mcp_lock);
	mcp251x_write_bits(priv->spi, BFPCTRL, val, 0);
	mutex_unlock(&priv->mcp_lock);

	priv->reg_bfpctrl &= ~val;
}

static int mcp251x_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	if (mcp251x_gpio_is_input(offset))
		return GPIOF_DIR_IN;

	return GPIOF_DIR_OUT;
}

static int mcp251x_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct mcp251x_priv *priv = gpiochip_get_data(chip);
	u8 reg, mask, val;

	if (mcp251x_gpio_is_input(offset)) {
		reg = TXRTSCTRL;
		mask = TXRTSCTRL_RTS(offset);
	} else {
		reg = BFPCTRL;
		mask = BFPCTRL_BFS(offset - MCP251X_GPIO_RX0BF);
	}

	mutex_lock(&priv->mcp_lock);
	val = mcp251x_read_reg(priv->spi, reg);
	mutex_unlock(&priv->mcp_lock);

	return !!(val & mask);
}

static int mcp251x_gpio_get_multiple(struct gpio_chip *chip,
				     unsigned long *maskp, unsigned long *bitsp)
{
	struct mcp251x_priv *priv = gpiochip_get_data(chip);
	unsigned long bits = 0;
	u8 val;

	mutex_lock(&priv->mcp_lock);
	if (maskp[0] & MCP251X_GPIO_INPUT_MASK) {
		val = mcp251x_read_reg(priv->spi, TXRTSCTRL);
		val = FIELD_GET(TXRTSCTRL_RTS_MASK, val);
		bits |= FIELD_PREP(MCP251X_GPIO_INPUT_MASK, val);
	}
	if (maskp[0] & MCP251X_GPIO_OUTPUT_MASK) {
		val = mcp251x_read_reg(priv->spi, BFPCTRL);
		val = FIELD_GET(BFPCTRL_BFS_MASK, val);
		bits |= FIELD_PREP(MCP251X_GPIO_OUTPUT_MASK, val);
	}
	mutex_unlock(&priv->mcp_lock);

	bitsp[0] = bits;
	return 0;
}

static void mcp251x_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	struct mcp251x_priv *priv = gpiochip_get_data(chip);
	u8 mask, val;

	mask = BFPCTRL_BFS(offset - MCP251X_GPIO_RX0BF);
	val = value ? mask : 0;

	mutex_lock(&priv->mcp_lock);
	mcp251x_write_bits(priv->spi, BFPCTRL, mask, val);
	mutex_unlock(&priv->mcp_lock);

	priv->reg_bfpctrl &= ~mask;
	priv->reg_bfpctrl |= val;
}

static void
mcp251x_gpio_set_multiple(struct gpio_chip *chip,
			  unsigned long *maskp, unsigned long *bitsp)
{
	struct mcp251x_priv *priv = gpiochip_get_data(chip);
	u8 mask, val;

	mask = FIELD_GET(MCP251X_GPIO_OUTPUT_MASK, maskp[0]);
	mask = FIELD_PREP(BFPCTRL_BFS_MASK, mask);

	val = FIELD_GET(MCP251X_GPIO_OUTPUT_MASK, bitsp[0]);
	val = FIELD_PREP(BFPCTRL_BFS_MASK, val);

	if (!mask)
		return;

	mutex_lock(&priv->mcp_lock);
	mcp251x_write_bits(priv->spi, BFPCTRL, mask, val);
	mutex_unlock(&priv->mcp_lock);

	priv->reg_bfpctrl &= ~mask;
	priv->reg_bfpctrl |= val;
}

static void mcp251x_gpio_restore(struct spi_device *spi)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);

	mcp251x_write_reg(spi, BFPCTRL, priv->reg_bfpctrl);
}

static int mcp251x_gpio_setup(struct mcp251x_priv *priv)
{
	struct gpio_chip *gpio = &priv->gpio;

	if (!device_property_present(&priv->spi->dev, "gpio-controller"))
		return 0;

	/* gpiochip handles TX[0..2]RTS and RX[0..1]BF */
	gpio->label = priv->spi->modalias;
	gpio->parent = &priv->spi->dev;
	gpio->owner = THIS_MODULE;
	gpio->request = mcp251x_gpio_request;
	gpio->free = mcp251x_gpio_free;
	gpio->get_direction = mcp251x_gpio_get_direction;
	gpio->get = mcp251x_gpio_get;
	gpio->get_multiple = mcp251x_gpio_get_multiple;
	gpio->set = mcp251x_gpio_set;
	gpio->set_multiple = mcp251x_gpio_set_multiple;
	gpio->base = -1;
	gpio->ngpio = ARRAY_SIZE(mcp251x_gpio_names);
	gpio->names = mcp251x_gpio_names;
	gpio->can_sleep = true;

	return devm_gpiochip_add_data(&priv->spi->dev, gpio, priv);
}
#else
static inline void mcp251x_gpio_restore(struct spi_device *spi)
{
}

static inline int mcp251x_gpio_setup(struct mcp251x_priv *priv)
{
	return 0;
}
#endif

static void mcp251x_hw_tx_frame(struct spi_device *spi, u8 *buf,
				int len, int tx_buf_idx)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);

	if (mcp251x_is_2510(spi)) {
		int i;

		for (i = 1; i < TXBDAT_OFF + len; i++)
			mcp251x_write_reg(spi, TXBCTRL(tx_buf_idx) + i,
					  buf[i]);
	} else {
		memcpy(priv->spi_tx_buf, buf, TXBDAT_OFF + len);
		mcp251x_spi_write(spi, TXBDAT_OFF + len);
	}
}

static void mcp251x_hw_tx(struct spi_device *spi, struct can_frame *frame,
			  int tx_buf_idx)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	u32 sid, eid, exide, rtr;
	u8 buf[SPI_TRANSFER_BUF_LEN];

	exide = (frame->can_id & CAN_EFF_FLAG) ? 1 : 0; /* Extended ID Enable */
	if (exide)
		sid = (frame->can_id & CAN_EFF_MASK) >> 18;
	else
		sid = frame->can_id & CAN_SFF_MASK; /* Standard ID */
	eid = frame->can_id & CAN_EFF_MASK; /* Extended ID */
	rtr = (frame->can_id & CAN_RTR_FLAG) ? 1 : 0; /* Remote transmission */

	buf[TXBCTRL_OFF] = INSTRUCTION_LOAD_TXB(tx_buf_idx);
	buf[TXBSIDH_OFF] = sid >> SIDH_SHIFT;
	buf[TXBSIDL_OFF] = ((sid & SIDL_SID_MASK) << SIDL_SID_SHIFT) |
		(exide << SIDL_EXIDE_SHIFT) |
		((eid >> SIDL_EID_SHIFT) & SIDL_EID_MASK);
	buf[TXBEID8_OFF] = GET_BYTE(eid, 1);
	buf[TXBEID0_OFF] = GET_BYTE(eid, 0);
	buf[TXBDLC_OFF] = (rtr << DLC_RTR_SHIFT) | frame->len;
	memcpy(buf + TXBDAT_OFF, frame->data, frame->len);
	mcp251x_hw_tx_frame(spi, buf, frame->len, tx_buf_idx);

	/* use INSTRUCTION_RTS, to avoid "repeated frame problem" */
	priv->spi_tx_buf[0] = INSTRUCTION_RTS(1 << tx_buf_idx);
	mcp251x_spi_write(priv->spi, 1);
}

static void mcp251x_hw_rx_frame(struct spi_device *spi, u8 *buf,
				int buf_idx)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);

	if (mcp251x_is_2510(spi)) {
		int i, len;

		for (i = 1; i < RXBDAT_OFF; i++)
			buf[i] = mcp251x_read_reg(spi, RXBCTRL(buf_idx) + i);

		len = can_cc_dlc2len(buf[RXBDLC_OFF] & RXBDLC_LEN_MASK);
		for (; i < (RXBDAT_OFF + len); i++)
			buf[i] = mcp251x_read_reg(spi, RXBCTRL(buf_idx) + i);
	} else {
		priv->spi_tx_buf[RXBCTRL_OFF] = INSTRUCTION_READ_RXB(buf_idx);
		if (spi->controller->flags & SPI_CONTROLLER_HALF_DUPLEX) {
			spi_write_then_read(spi, priv->spi_tx_buf, 1,
					    priv->spi_rx_buf,
					    SPI_TRANSFER_BUF_LEN);
			memcpy(buf + 1, priv->spi_rx_buf,
			       SPI_TRANSFER_BUF_LEN - 1);
		} else {
			mcp251x_spi_trans(spi, SPI_TRANSFER_BUF_LEN);
			memcpy(buf, priv->spi_rx_buf, SPI_TRANSFER_BUF_LEN);
		}
	}
}

static void mcp251x_hw_rx(struct spi_device *spi, int buf_idx)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	struct sk_buff *skb;
	struct can_frame *frame;
	u8 buf[SPI_TRANSFER_BUF_LEN];

	skb = alloc_can_skb(priv->net, &frame);
	if (!skb) {
		dev_err(&spi->dev, "cannot allocate RX skb\n");
		priv->net->stats.rx_dropped++;
		return;
	}

	mcp251x_hw_rx_frame(spi, buf, buf_idx);
	if (buf[RXBSIDL_OFF] & RXBSIDL_IDE) {
		/* Extended ID format */
		frame->can_id = CAN_EFF_FLAG;
		frame->can_id |=
			/* Extended ID part */
			SET_BYTE(buf[RXBSIDL_OFF] & RXBSIDL_EID, 2) |
			SET_BYTE(buf[RXBEID8_OFF], 1) |
			SET_BYTE(buf[RXBEID0_OFF], 0) |
			/* Standard ID part */
			(((buf[RXBSIDH_OFF] << RXBSIDH_SHIFT) |
			  (buf[RXBSIDL_OFF] >> RXBSIDL_SHIFT)) << 18);
		/* Remote transmission request */
		if (buf[RXBDLC_OFF] & RXBDLC_RTR)
			frame->can_id |= CAN_RTR_FLAG;
	} else {
		/* Standard ID format */
		frame->can_id =
			(buf[RXBSIDH_OFF] << RXBSIDH_SHIFT) |
			(buf[RXBSIDL_OFF] >> RXBSIDL_SHIFT);
		if (buf[RXBSIDL_OFF] & RXBSIDL_SRR)
			frame->can_id |= CAN_RTR_FLAG;
	}
	/* Data length */
	frame->len = can_cc_dlc2len(buf[RXBDLC_OFF] & RXBDLC_LEN_MASK);
	if (!(frame->can_id & CAN_RTR_FLAG)) {
		memcpy(frame->data, buf + RXBDAT_OFF, frame->len);

		priv->net->stats.rx_bytes += frame->len;
	}
	priv->net->stats.rx_packets++;

	netif_rx(skb);
}

static void mcp251x_hw_sleep(struct spi_device *spi)
{
	mcp251x_write_reg(spi, CANCTRL, CANCTRL_REQOP_SLEEP);
}

/* May only be called when device is sleeping! */
static int mcp251x_hw_wake(struct spi_device *spi)
{
	u8 value;
	int ret;

	/* Force wakeup interrupt to wake device, but don't execute IST */
	disable_irq(spi->irq);
	mcp251x_write_2regs(spi, CANINTE, CANINTE_WAKIE, CANINTF_WAKIF);

	/* Wait for oscillator startup timer after wake up */
	mdelay(MCP251X_OST_DELAY_MS);

	/* Put device into config mode */
	mcp251x_write_reg(spi, CANCTRL, CANCTRL_REQOP_CONF);

	/* Wait for the device to enter config mode */
	ret = mcp251x_read_stat_poll_timeout(spi, value, value == CANCTRL_REQOP_CONF,
					     MCP251X_OST_DELAY_MS * 1000,
					     USEC_PER_SEC);
	if (ret) {
		dev_err(&spi->dev, "MCP251x didn't enter in config mode\n");
		return ret;
	}

	/* Disable and clear pending interrupts */
	mcp251x_write_2regs(spi, CANINTE, 0x00, 0x00);
	enable_irq(spi->irq);

	return 0;
}

static netdev_tx_t mcp251x_hard_start_xmit(struct sk_buff *skb,
					   struct net_device *net)
{
	struct mcp251x_priv *priv = netdev_priv(net);
	struct spi_device *spi = priv->spi;

	if (priv->tx_skb || priv->tx_busy) {
		dev_warn(&spi->dev, "hard_xmit called while tx busy\n");
		return NETDEV_TX_BUSY;
	}

	if (can_dropped_invalid_skb(net, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(net);
	priv->tx_skb = skb;
	queue_work(priv->wq, &priv->tx_work);

	return NETDEV_TX_OK;
}

static int mcp251x_do_set_mode(struct net_device *net, enum can_mode mode)
{
	struct mcp251x_priv *priv = netdev_priv(net);

	switch (mode) {
	case CAN_MODE_START:
		mcp251x_clean(net);
		/* We have to delay work since SPI I/O may sleep */
		priv->can.state = CAN_STATE_ERROR_ACTIVE;
		priv->restart_tx = 1;
		if (priv->can.restart_ms == 0)
			priv->after_suspend = AFTER_SUSPEND_RESTART;
		queue_work(priv->wq, &priv->restart_work);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int mcp251x_set_normal_mode(struct spi_device *spi)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	u8 value;
	int ret;

	/* Enable interrupts */
	mcp251x_write_reg(spi, CANINTE,
			  CANINTE_ERRIE | CANINTE_TX2IE | CANINTE_TX1IE |
			  CANINTE_TX0IE | CANINTE_RX1IE | CANINTE_RX0IE);

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) {
		/* Put device into loopback mode */
		mcp251x_write_reg(spi, CANCTRL, CANCTRL_REQOP_LOOPBACK);
	} else if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY) {
		/* Put device into listen-only mode */
		mcp251x_write_reg(spi, CANCTRL, CANCTRL_REQOP_LISTEN_ONLY);
	} else {
		/* Put device into normal mode */
		mcp251x_write_reg(spi, CANCTRL, CANCTRL_REQOP_NORMAL);

		/* Wait for the device to enter normal mode */
		ret = mcp251x_read_stat_poll_timeout(spi, value, value == 0,
						     MCP251X_OST_DELAY_MS * 1000,
						     USEC_PER_SEC);
		if (ret) {
			dev_err(&spi->dev, "MCP251x didn't enter in normal mode\n");
			return ret;
		}
	}
	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	return 0;
}

static int mcp251x_do_set_bittiming(struct net_device *net)
{
	struct mcp251x_priv *priv = netdev_priv(net);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct spi_device *spi = priv->spi;

	mcp251x_write_reg(spi, CNF1, ((bt->sjw - 1) << CNF1_SJW_SHIFT) |
			  (bt->brp - 1));
	mcp251x_write_reg(spi, CNF2, CNF2_BTLMODE |
			  (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES ?
			   CNF2_SAM : 0) |
			  ((bt->phase_seg1 - 1) << CNF2_PS1_SHIFT) |
			  (bt->prop_seg - 1));
	mcp251x_write_bits(spi, CNF3, CNF3_PHSEG2_MASK,
			   (bt->phase_seg2 - 1));
	dev_dbg(&spi->dev, "CNF: 0x%02x 0x%02x 0x%02x\n",
		mcp251x_read_reg(spi, CNF1),
		mcp251x_read_reg(spi, CNF2),
		mcp251x_read_reg(spi, CNF3));

	return 0;
}

static int mcp251x_setup(struct net_device *net, struct spi_device *spi)
{
	mcp251x_do_set_bittiming(net);

	mcp251x_write_reg(spi, RXBCTRL(0),
			  RXBCTRL_BUKT | RXBCTRL_RXM0 | RXBCTRL_RXM1);
	mcp251x_write_reg(spi, RXBCTRL(1),
			  RXBCTRL_RXM0 | RXBCTRL_RXM1);
	return 0;
}

static int mcp251x_hw_reset(struct spi_device *spi)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	u8 value;
	int ret;

	/* Wait for oscillator startup timer after power up */
	mdelay(MCP251X_OST_DELAY_MS);

	priv->spi_tx_buf[0] = INSTRUCTION_RESET;
	ret = mcp251x_spi_write(spi, 1);
	if (ret)
		return ret;

	/* Wait for oscillator startup timer after reset */
	mdelay(MCP251X_OST_DELAY_MS);

	/* Wait for reset to finish */
	ret = mcp251x_read_stat_poll_timeout(spi, value, value == CANCTRL_REQOP_CONF,
					     MCP251X_OST_DELAY_MS * 1000,
					     USEC_PER_SEC);
	if (ret)
		dev_err(&spi->dev, "MCP251x didn't enter in conf mode after reset\n");
	return ret;
}

static int mcp251x_hw_probe(struct spi_device *spi)
{
	u8 ctrl;
	int ret;

	ret = mcp251x_hw_reset(spi);
	if (ret)
		return ret;

	ctrl = mcp251x_read_reg(spi, CANCTRL);

	dev_dbg(&spi->dev, "CANCTRL 0x%02x\n", ctrl);

	/* Check for power up default value */
	if ((ctrl & 0x17) != 0x07)
		return -ENODEV;

	return 0;
}

static int mcp251x_power_enable(struct regulator *reg, int enable)
{
	if (IS_ERR_OR_NULL(reg))
		return 0;

	if (enable)
		return regulator_enable(reg);
	else
		return regulator_disable(reg);
}

static int mcp251x_stop(struct net_device *net)
{
	struct mcp251x_priv *priv = netdev_priv(net);
	struct spi_device *spi = priv->spi;

	close_candev(net);

	priv->force_quit = 1;
	free_irq(spi->irq, priv);

	mutex_lock(&priv->mcp_lock);

	/* Disable and clear pending interrupts */
	mcp251x_write_2regs(spi, CANINTE, 0x00, 0x00);

	mcp251x_write_reg(spi, TXBCTRL(0), 0);
	mcp251x_clean(net);

	mcp251x_hw_sleep(spi);

	mcp251x_power_enable(priv->transceiver, 0);

	priv->can.state = CAN_STATE_STOPPED;

	mutex_unlock(&priv->mcp_lock);

	return 0;
}

static void mcp251x_error_skb(struct net_device *net, int can_id, int data1)
{
	struct sk_buff *skb;
	struct can_frame *frame;

	skb = alloc_can_err_skb(net, &frame);
	if (skb) {
		frame->can_id |= can_id;
		frame->data[1] = data1;
		netif_rx(skb);
	} else {
		netdev_err(net, "cannot allocate error skb\n");
	}
}

static void mcp251x_tx_work_handler(struct work_struct *ws)
{
	struct mcp251x_priv *priv = container_of(ws, struct mcp251x_priv,
						 tx_work);
	struct spi_device *spi = priv->spi;
	struct net_device *net = priv->net;
	struct can_frame *frame;

	mutex_lock(&priv->mcp_lock);
	if (priv->tx_skb) {
		if (priv->can.state == CAN_STATE_BUS_OFF) {
			mcp251x_clean(net);
		} else {
			frame = (struct can_frame *)priv->tx_skb->data;

			if (frame->len > CAN_FRAME_MAX_DATA_LEN)
				frame->len = CAN_FRAME_MAX_DATA_LEN;
			mcp251x_hw_tx(spi, frame, 0);
			priv->tx_busy = true;
			can_put_echo_skb(priv->tx_skb, net, 0, 0);
			priv->tx_skb = NULL;
		}
	}
	mutex_unlock(&priv->mcp_lock);
}

static void mcp251x_restart_work_handler(struct work_struct *ws)
{
	struct mcp251x_priv *priv = container_of(ws, struct mcp251x_priv,
						 restart_work);
	struct spi_device *spi = priv->spi;
	struct net_device *net = priv->net;

	mutex_lock(&priv->mcp_lock);
	if (priv->after_suspend) {
		if (priv->after_suspend & AFTER_SUSPEND_POWER) {
			mcp251x_hw_reset(spi);
			mcp251x_setup(net, spi);
			mcp251x_gpio_restore(spi);
		} else {
			mcp251x_hw_wake(spi);
		}
		priv->force_quit = 0;
		if (priv->after_suspend & AFTER_SUSPEND_RESTART) {
			mcp251x_set_normal_mode(spi);
		} else if (priv->after_suspend & AFTER_SUSPEND_UP) {
			netif_device_attach(net);
			mcp251x_clean(net);
			mcp251x_set_normal_mode(spi);
			netif_wake_queue(net);
		} else {
			mcp251x_hw_sleep(spi);
		}
		priv->after_suspend = 0;
	}

	if (priv->restart_tx) {
		priv->restart_tx = 0;
		mcp251x_write_reg(spi, TXBCTRL(0), 0);
		mcp251x_clean(net);
		netif_wake_queue(net);
		mcp251x_error_skb(net, CAN_ERR_RESTARTED, 0);
	}
	mutex_unlock(&priv->mcp_lock);
}

static irqreturn_t mcp251x_can_ist(int irq, void *dev_id)
{
	struct mcp251x_priv *priv = dev_id;
	struct spi_device *spi = priv->spi;
	struct net_device *net = priv->net;

	mutex_lock(&priv->mcp_lock);
	while (!priv->force_quit) {
		enum can_state new_state;
		u8 intf, eflag;
		u8 clear_intf = 0;
		int can_id = 0, data1 = 0;

		mcp251x_read_2regs(spi, CANINTF, &intf, &eflag);

		/* mask out flags we don't care about */
		intf &= CANINTF_RX | CANINTF_TX | CANINTF_ERR;

		/* receive buffer 0 */
		if (intf & CANINTF_RX0IF) {
			mcp251x_hw_rx(spi, 0);
			/* Free one buffer ASAP
			 * (The MCP2515/25625 does this automatically.)
			 */
			if (mcp251x_is_2510(spi))
				mcp251x_write_bits(spi, CANINTF,
						   CANINTF_RX0IF, 0x00);
		}

		/* receive buffer 1 */
		if (intf & CANINTF_RX1IF) {
			mcp251x_hw_rx(spi, 1);
			/* The MCP2515/25625 does this automatically. */
			if (mcp251x_is_2510(spi))
				clear_intf |= CANINTF_RX1IF;
		}

		/* any error or tx interrupt we need to clear? */
		if (intf & (CANINTF_ERR | CANINTF_TX))
			clear_intf |= intf & (CANINTF_ERR | CANINTF_TX);
		if (clear_intf)
			mcp251x_write_bits(spi, CANINTF, clear_intf, 0x00);

		if (eflag & (EFLG_RX0OVR | EFLG_RX1OVR))
			mcp251x_write_bits(spi, EFLG, eflag, 0x00);

		/* Update can state */
		if (eflag & EFLG_TXBO) {
			new_state = CAN_STATE_BUS_OFF;
			can_id |= CAN_ERR_BUSOFF;
		} else if (eflag & EFLG_TXEP) {
			new_state = CAN_STATE_ERROR_PASSIVE;
			can_id |= CAN_ERR_CRTL;
			data1 |= CAN_ERR_CRTL_TX_PASSIVE;
		} else if (eflag & EFLG_RXEP) {
			new_state = CAN_STATE_ERROR_PASSIVE;
			can_id |= CAN_ERR_CRTL;
			data1 |= CAN_ERR_CRTL_RX_PASSIVE;
		} else if (eflag & EFLG_TXWAR) {
			new_state = CAN_STATE_ERROR_WARNING;
			can_id |= CAN_ERR_CRTL;
			data1 |= CAN_ERR_CRTL_TX_WARNING;
		} else if (eflag & EFLG_RXWAR) {
			new_state = CAN_STATE_ERROR_WARNING;
			can_id |= CAN_ERR_CRTL;
			data1 |= CAN_ERR_CRTL_RX_WARNING;
		} else {
			new_state = CAN_STATE_ERROR_ACTIVE;
		}

		/* Update can state statistics */
		switch (priv->can.state) {
		case CAN_STATE_ERROR_ACTIVE:
			if (new_state >= CAN_STATE_ERROR_WARNING &&
			    new_state <= CAN_STATE_BUS_OFF)
				priv->can.can_stats.error_warning++;
			fallthrough;
		case CAN_STATE_ERROR_WARNING:
			if (new_state >= CAN_STATE_ERROR_PASSIVE &&
			    new_state <= CAN_STATE_BUS_OFF)
				priv->can.can_stats.error_passive++;
			break;
		default:
			break;
		}
		priv->can.state = new_state;

		if (intf & CANINTF_ERRIF) {
			/* Handle overflow counters */
			if (eflag & (EFLG_RX0OVR | EFLG_RX1OVR)) {
				if (eflag & EFLG_RX0OVR) {
					net->stats.rx_over_errors++;
					net->stats.rx_errors++;
				}
				if (eflag & EFLG_RX1OVR) {
					net->stats.rx_over_errors++;
					net->stats.rx_errors++;
				}
				can_id |= CAN_ERR_CRTL;
				data1 |= CAN_ERR_CRTL_RX_OVERFLOW;
			}
			mcp251x_error_skb(net, can_id, data1);
		}

		if (priv->can.state == CAN_STATE_BUS_OFF) {
			if (priv->can.restart_ms == 0) {
				priv->force_quit = 1;
				priv->can.can_stats.bus_off++;
				can_bus_off(net);
				mcp251x_hw_sleep(spi);
				break;
			}
		}

		if (intf == 0)
			break;

		if (intf & CANINTF_TX) {
			if (priv->tx_busy) {
				net->stats.tx_packets++;
				net->stats.tx_bytes += can_get_echo_skb(net, 0,
									NULL);
				priv->tx_busy = false;
			}
			netif_wake_queue(net);
		}
	}
	mutex_unlock(&priv->mcp_lock);
	return IRQ_HANDLED;
}

static int mcp251x_open(struct net_device *net)
{
	struct mcp251x_priv *priv = netdev_priv(net);
	struct spi_device *spi = priv->spi;
	unsigned long flags = 0;
	int ret;

	ret = open_candev(net);
	if (ret) {
		dev_err(&spi->dev, "unable to set initial baudrate!\n");
		return ret;
	}

	mutex_lock(&priv->mcp_lock);
	mcp251x_power_enable(priv->transceiver, 1);

	priv->force_quit = 0;
	priv->tx_skb = NULL;
	priv->tx_busy = false;

	if (!dev_fwnode(&spi->dev))
		flags = IRQF_TRIGGER_FALLING;

	ret = request_threaded_irq(spi->irq, NULL, mcp251x_can_ist,
				   flags | IRQF_ONESHOT, dev_name(&spi->dev),
				   priv);
	if (ret) {
		dev_err(&spi->dev, "failed to acquire irq %d\n", spi->irq);
		goto out_close;
	}

	ret = mcp251x_hw_wake(spi);
	if (ret)
		goto out_free_irq;
	ret = mcp251x_setup(net, spi);
	if (ret)
		goto out_free_irq;
	ret = mcp251x_set_normal_mode(spi);
	if (ret)
		goto out_free_irq;

	netif_wake_queue(net);
	mutex_unlock(&priv->mcp_lock);

	return 0;

out_free_irq:
	free_irq(spi->irq, priv);
	mcp251x_hw_sleep(spi);
out_close:
	mcp251x_power_enable(priv->transceiver, 0);
	close_candev(net);
	mutex_unlock(&priv->mcp_lock);
	return ret;
}

static const struct net_device_ops mcp251x_netdev_ops = {
	.ndo_open = mcp251x_open,
	.ndo_stop = mcp251x_stop,
	.ndo_start_xmit = mcp251x_hard_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct of_device_id mcp251x_of_match[] = {
	{
		.compatible	= "microchip,mcp2510",
		.data		= (void *)CAN_MCP251X_MCP2510,
	},
	{
		.compatible	= "microchip,mcp2515",
		.data		= (void *)CAN_MCP251X_MCP2515,
	},
	{
		.compatible	= "microchip,mcp25625",
		.data		= (void *)CAN_MCP251X_MCP25625,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mcp251x_of_match);

static const struct spi_device_id mcp251x_id_table[] = {
	{
		.name		= "mcp2510",
		.driver_data	= (kernel_ulong_t)CAN_MCP251X_MCP2510,
	},
	{
		.name		= "mcp2515",
		.driver_data	= (kernel_ulong_t)CAN_MCP251X_MCP2515,
	},
	{
		.name		= "mcp25625",
		.driver_data	= (kernel_ulong_t)CAN_MCP251X_MCP25625,
	},
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp251x_id_table);

static int mcp251x_can_probe(struct spi_device *spi)
{
	const void *match = device_get_match_data(&spi->dev);
	struct net_device *net;
	struct mcp251x_priv *priv;
	struct clk *clk;
	u32 freq;
	int ret;

	clk = devm_clk_get_optional(&spi->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	freq = clk_get_rate(clk);
	if (freq == 0)
		device_property_read_u32(&spi->dev, "clock-frequency", &freq);

	/* Sanity check */
	if (freq < 1000000 || freq > 25000000)
		return -ERANGE;

	/* Allocate can/net device */
	net = alloc_candev(sizeof(struct mcp251x_priv), TX_ECHO_SKB_MAX);
	if (!net)
		return -ENOMEM;

	ret = clk_prepare_enable(clk);
	if (ret)
		goto out_free;

	net->netdev_ops = &mcp251x_netdev_ops;
	net->flags |= IFF_ECHO;

	priv = netdev_priv(net);
	priv->can.bittiming_const = &mcp251x_bittiming_const;
	priv->can.do_set_mode = mcp251x_do_set_mode;
	priv->can.clock.freq = freq / 2;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES |
		CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_LISTENONLY;
	if (match)
		priv->model = (enum mcp251x_model)(uintptr_t)match;
	else
		priv->model = spi_get_device_id(spi)->driver_data;
	priv->net = net;
	priv->clk = clk;

	spi_set_drvdata(spi, priv);

	/* Configure the SPI bus */
	spi->bits_per_word = 8;
	if (mcp251x_is_2510(spi))
		spi->max_speed_hz = spi->max_speed_hz ? : 5 * 1000 * 1000;
	else
		spi->max_speed_hz = spi->max_speed_hz ? : 10 * 1000 * 1000;
	ret = spi_setup(spi);
	if (ret)
		goto out_clk;

	priv->power = devm_regulator_get_optional(&spi->dev, "vdd");
	priv->transceiver = devm_regulator_get_optional(&spi->dev, "xceiver");
	if ((PTR_ERR(priv->power) == -EPROBE_DEFER) ||
	    (PTR_ERR(priv->transceiver) == -EPROBE_DEFER)) {
		ret = -EPROBE_DEFER;
		goto out_clk;
	}

	ret = mcp251x_power_enable(priv->power, 1);
	if (ret)
		goto out_clk;

	priv->wq = alloc_workqueue("mcp251x_wq", WQ_FREEZABLE | WQ_MEM_RECLAIM,
				   0);
	if (!priv->wq) {
		ret = -ENOMEM;
		goto out_clk;
	}
	INIT_WORK(&priv->tx_work, mcp251x_tx_work_handler);
	INIT_WORK(&priv->restart_work, mcp251x_restart_work_handler);

	priv->spi = spi;
	mutex_init(&priv->mcp_lock);

	priv->spi_tx_buf = devm_kzalloc(&spi->dev, SPI_TRANSFER_BUF_LEN,
					GFP_KERNEL);
	if (!priv->spi_tx_buf) {
		ret = -ENOMEM;
		goto error_probe;
	}

	priv->spi_rx_buf = devm_kzalloc(&spi->dev, SPI_TRANSFER_BUF_LEN,
					GFP_KERNEL);
	if (!priv->spi_rx_buf) {
		ret = -ENOMEM;
		goto error_probe;
	}

	SET_NETDEV_DEV(net, &spi->dev);

	/* Here is OK to not lock the MCP, no one knows about it yet */
	ret = mcp251x_hw_probe(spi);
	if (ret) {
		if (ret == -ENODEV)
			dev_err(&spi->dev, "Cannot initialize MCP%x. Wrong wiring?\n",
				priv->model);
		goto error_probe;
	}

	mcp251x_hw_sleep(spi);

	ret = register_candev(net);
	if (ret)
		goto error_probe;

	ret = mcp251x_gpio_setup(priv);
	if (ret)
		goto error_probe;

	netdev_info(net, "MCP%x successfully initialized.\n", priv->model);
	return 0;

error_probe:
	destroy_workqueue(priv->wq);
	priv->wq = NULL;
	mcp251x_power_enable(priv->power, 0);

out_clk:
	clk_disable_unprepare(clk);

out_free:
	free_candev(net);

	dev_err(&spi->dev, "Probe failed, err=%d\n", -ret);
	return ret;
}

static void mcp251x_can_remove(struct spi_device *spi)
{
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	struct net_device *net = priv->net;

	unregister_candev(net);

	mcp251x_power_enable(priv->power, 0);

	destroy_workqueue(priv->wq);
	priv->wq = NULL;

	clk_disable_unprepare(priv->clk);

	free_candev(net);
}

static int __maybe_unused mcp251x_can_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct mcp251x_priv *priv = spi_get_drvdata(spi);
	struct net_device *net = priv->net;

	priv->force_quit = 1;
	disable_irq(spi->irq);
	/* Note: at this point neither IST nor workqueues are running.
	 * open/stop cannot be called anyway so locking is not needed
	 */
	if (netif_running(net)) {
		netif_device_detach(net);

		mcp251x_hw_sleep(spi);
		mcp251x_power_enable(priv->transceiver, 0);
		priv->after_suspend = AFTER_SUSPEND_UP;
	} else {
		priv->after_suspend = AFTER_SUSPEND_DOWN;
	}

	mcp251x_power_enable(priv->power, 0);
	priv->after_suspend |= AFTER_SUSPEND_POWER;

	return 0;
}

static int __maybe_unused mcp251x_can_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct mcp251x_priv *priv = spi_get_drvdata(spi);

	if (priv->after_suspend & AFTER_SUSPEND_POWER)
		mcp251x_power_enable(priv->power, 1);
	if (priv->after_suspend & AFTER_SUSPEND_UP)
		mcp251x_power_enable(priv->transceiver, 1);

	if (priv->after_suspend & (AFTER_SUSPEND_POWER | AFTER_SUSPEND_UP))
		queue_work(priv->wq, &priv->restart_work);
	else
		priv->after_suspend = 0;

	priv->force_quit = 0;
	enable_irq(spi->irq);
	return 0;
}

static SIMPLE_DEV_PM_OPS(mcp251x_can_pm_ops, mcp251x_can_suspend,
	mcp251x_can_resume);

static struct spi_driver mcp251x_can_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = mcp251x_of_match,
		.pm = &mcp251x_can_pm_ops,
	},
	.id_table = mcp251x_id_table,
	.probe = mcp251x_can_probe,
	.remove = mcp251x_can_remove,
};
module_spi_driver(mcp251x_can_driver);

MODULE_AUTHOR("Chris Elston <celston@katalix.com>, "
	      "Christian Pellegrin <chripell@evolware.org>");
MODULE_DESCRIPTION("Microchip 251x/25625 CAN driver");
MODULE_LICENSE("GPL v2");
