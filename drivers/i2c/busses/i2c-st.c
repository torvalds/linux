/*
 * Copyright (C) 2013 STMicroelectronics
 *
 * I2C master mode controller driver, used in STMicroelectronics devices.
 *
 * Author: Maxime Coquelin <maxime.coquelin@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

/* SSC registers */
#define SSC_BRG				0x000
#define SSC_TBUF			0x004
#define SSC_RBUF			0x008
#define SSC_CTL				0x00C
#define SSC_IEN				0x010
#define SSC_STA				0x014
#define SSC_I2C				0x018
#define SSC_SLAD			0x01C
#define SSC_REP_START_HOLD		0x020
#define SSC_START_HOLD			0x024
#define SSC_REP_START_SETUP		0x028
#define SSC_DATA_SETUP			0x02C
#define SSC_STOP_SETUP			0x030
#define SSC_BUS_FREE			0x034
#define SSC_TX_FSTAT			0x038
#define SSC_RX_FSTAT			0x03C
#define SSC_PRE_SCALER_BRG		0x040
#define SSC_CLR				0x080
#define SSC_NOISE_SUPP_WIDTH		0x100
#define SSC_PRSCALER			0x104
#define SSC_NOISE_SUPP_WIDTH_DATAOUT	0x108
#define SSC_PRSCALER_DATAOUT		0x10c

/* SSC Control */
#define SSC_CTL_DATA_WIDTH_9		0x8
#define SSC_CTL_DATA_WIDTH_MSK		0xf
#define SSC_CTL_BM			0xf
#define SSC_CTL_HB			BIT(4)
#define SSC_CTL_PH			BIT(5)
#define SSC_CTL_PO			BIT(6)
#define SSC_CTL_SR			BIT(7)
#define SSC_CTL_MS			BIT(8)
#define SSC_CTL_EN			BIT(9)
#define SSC_CTL_LPB			BIT(10)
#define SSC_CTL_EN_TX_FIFO		BIT(11)
#define SSC_CTL_EN_RX_FIFO		BIT(12)
#define SSC_CTL_EN_CLST_RX		BIT(13)

/* SSC Interrupt Enable */
#define SSC_IEN_RIEN			BIT(0)
#define SSC_IEN_TIEN			BIT(1)
#define SSC_IEN_TEEN			BIT(2)
#define SSC_IEN_REEN			BIT(3)
#define SSC_IEN_PEEN			BIT(4)
#define SSC_IEN_AASEN			BIT(6)
#define SSC_IEN_STOPEN			BIT(7)
#define SSC_IEN_ARBLEN			BIT(8)
#define SSC_IEN_NACKEN			BIT(10)
#define SSC_IEN_REPSTRTEN		BIT(11)
#define SSC_IEN_TX_FIFO_HALF		BIT(12)
#define SSC_IEN_RX_FIFO_HALF_FULL	BIT(14)

/* SSC Status */
#define SSC_STA_RIR			BIT(0)
#define SSC_STA_TIR			BIT(1)
#define SSC_STA_TE			BIT(2)
#define SSC_STA_RE			BIT(3)
#define SSC_STA_PE			BIT(4)
#define SSC_STA_CLST			BIT(5)
#define SSC_STA_AAS			BIT(6)
#define SSC_STA_STOP			BIT(7)
#define SSC_STA_ARBL			BIT(8)
#define SSC_STA_BUSY			BIT(9)
#define SSC_STA_NACK			BIT(10)
#define SSC_STA_REPSTRT			BIT(11)
#define SSC_STA_TX_FIFO_HALF		BIT(12)
#define SSC_STA_TX_FIFO_FULL		BIT(13)
#define SSC_STA_RX_FIFO_HALF		BIT(14)

/* SSC I2C Control */
#define SSC_I2C_I2CM			BIT(0)
#define SSC_I2C_STRTG			BIT(1)
#define SSC_I2C_STOPG			BIT(2)
#define SSC_I2C_ACKG			BIT(3)
#define SSC_I2C_AD10			BIT(4)
#define SSC_I2C_TXENB			BIT(5)
#define SSC_I2C_REPSTRTG		BIT(11)
#define SSC_I2C_SLAVE_DISABLE		BIT(12)

/* SSC Tx FIFO Status */
#define SSC_TX_FSTAT_STATUS		0x07

/* SSC Rx FIFO Status */
#define SSC_RX_FSTAT_STATUS		0x07

/* SSC Clear bit operation */
#define SSC_CLR_SSCAAS			BIT(6)
#define SSC_CLR_SSCSTOP			BIT(7)
#define SSC_CLR_SSCARBL			BIT(8)
#define SSC_CLR_NACK			BIT(10)
#define SSC_CLR_REPSTRT			BIT(11)

/* SSC Clock Prescaler */
#define SSC_PRSC_VALUE			0x0f


#define SSC_TXFIFO_SIZE			0x8
#define SSC_RXFIFO_SIZE			0x8

enum st_i2c_mode {
	I2C_MODE_STANDARD,
	I2C_MODE_FAST,
	I2C_MODE_END,
};

/**
 * struct st_i2c_timings - per-Mode tuning parameters
 * @rate: I2C bus rate
 * @rep_start_hold: I2C repeated start hold time requirement
 * @rep_start_setup: I2C repeated start set up time requirement
 * @start_hold: I2C start hold time requirement
 * @data_setup_time: I2C data set up time requirement
 * @stop_setup_time: I2C stop set up time requirement
 * @bus_free_time: I2C bus free time requirement
 * @sda_pulse_min_limit: I2C SDA pulse mini width limit
 */
struct st_i2c_timings {
	u32 rate;
	u32 rep_start_hold;
	u32 rep_start_setup;
	u32 start_hold;
	u32 data_setup_time;
	u32 stop_setup_time;
	u32 bus_free_time;
	u32 sda_pulse_min_limit;
};

/**
 * struct st_i2c_client - client specific data
 * @addr: 8-bit slave addr, including r/w bit
 * @count: number of bytes to be transfered
 * @xfered: number of bytes already transferred
 * @buf: data buffer
 * @result: result of the transfer
 * @stop: last I2C msg to be sent, i.e. STOP to be generated
 */
struct st_i2c_client {
	u8	addr;
	u32	count;
	u32	xfered;
	u8	*buf;
	int	result;
	bool	stop;
};

/**
 * struct st_i2c_dev - private data of the controller
 * @adap: I2C adapter for this controller
 * @dev: device for this controller
 * @base: virtual memory area
 * @complete: completion of I2C message
 * @irq: interrupt line for th controller
 * @clk: hw ssc block clock
 * @mode: I2C mode of the controller. Standard or Fast only supported
 * @scl_min_width_us: SCL line minimum pulse width in us
 * @sda_min_width_us: SDA line minimum pulse width in us
 * @client: I2C transfert information
 * @busy: I2C transfer on-going
 */
struct st_i2c_dev {
	struct i2c_adapter	adap;
	struct device		*dev;
	void __iomem		*base;
	struct completion	complete;
	int			irq;
	struct clk		*clk;
	int			mode;
	u32			scl_min_width_us;
	u32			sda_min_width_us;
	struct st_i2c_client	client;
	bool			busy;
};

static inline void st_i2c_set_bits(void __iomem *reg, u32 mask)
{
	writel_relaxed(readl_relaxed(reg) | mask, reg);
}

static inline void st_i2c_clr_bits(void __iomem *reg, u32 mask)
{
	writel_relaxed(readl_relaxed(reg) & ~mask, reg);
}

/*
 * From I2C Specifications v0.5.
 *
 * All the values below have +10% margin added to be
 * compatible with some out-of-spec devices,
 * like HDMI link of the Toshiba 19AV600 TV.
 */
static struct st_i2c_timings i2c_timings[] = {
	[I2C_MODE_STANDARD] = {
		.rate			= 100000,
		.rep_start_hold		= 4400,
		.rep_start_setup	= 5170,
		.start_hold		= 4400,
		.data_setup_time	= 275,
		.stop_setup_time	= 4400,
		.bus_free_time		= 5170,
	},
	[I2C_MODE_FAST] = {
		.rate			= 400000,
		.rep_start_hold		= 660,
		.rep_start_setup	= 660,
		.start_hold		= 660,
		.data_setup_time	= 110,
		.stop_setup_time	= 660,
		.bus_free_time		= 1430,
	},
};

static void st_i2c_flush_rx_fifo(struct st_i2c_dev *i2c_dev)
{
	int count, i;

	/*
	 * Counter only counts up to 7 but fifo size is 8...
	 * When fifo is full, counter is 0 and RIR bit of status register is
	 * set
	 */
	if (readl_relaxed(i2c_dev->base + SSC_STA) & SSC_STA_RIR)
		count = SSC_RXFIFO_SIZE;
	else
		count = readl_relaxed(i2c_dev->base + SSC_RX_FSTAT) &
			SSC_RX_FSTAT_STATUS;

	for (i = 0; i < count; i++)
		readl_relaxed(i2c_dev->base + SSC_RBUF);
}

static void st_i2c_soft_reset(struct st_i2c_dev *i2c_dev)
{
	/*
	 * FIFO needs to be emptied before reseting the IP,
	 * else the controller raises a BUSY error.
	 */
	st_i2c_flush_rx_fifo(i2c_dev);

	st_i2c_set_bits(i2c_dev->base + SSC_CTL, SSC_CTL_SR);
	st_i2c_clr_bits(i2c_dev->base + SSC_CTL, SSC_CTL_SR);
}

/**
 * st_i2c_hw_config() - Prepare SSC block, calculate and apply tuning timings
 * @i2c_dev: Controller's private data
 */
static void st_i2c_hw_config(struct st_i2c_dev *i2c_dev)
{
	unsigned long rate;
	u32 val, ns_per_clk;
	struct st_i2c_timings *t = &i2c_timings[i2c_dev->mode];

	st_i2c_soft_reset(i2c_dev);

	val = SSC_CLR_REPSTRT | SSC_CLR_NACK | SSC_CLR_SSCARBL |
		SSC_CLR_SSCAAS | SSC_CLR_SSCSTOP;
	writel_relaxed(val, i2c_dev->base + SSC_CLR);

	/* SSC Control register setup */
	val = SSC_CTL_PO | SSC_CTL_PH | SSC_CTL_HB | SSC_CTL_DATA_WIDTH_9;
	writel_relaxed(val, i2c_dev->base + SSC_CTL);

	rate = clk_get_rate(i2c_dev->clk);
	ns_per_clk = 1000000000 / rate;

	/* Baudrate */
	val = rate / (2 * t->rate);
	writel_relaxed(val, i2c_dev->base + SSC_BRG);

	/* Pre-scaler baudrate */
	writel_relaxed(1, i2c_dev->base + SSC_PRE_SCALER_BRG);

	/* Enable I2C mode */
	writel_relaxed(SSC_I2C_I2CM, i2c_dev->base + SSC_I2C);

	/* Repeated start hold time */
	val = t->rep_start_hold / ns_per_clk;
	writel_relaxed(val, i2c_dev->base + SSC_REP_START_HOLD);

	/* Repeated start set up time */
	val = t->rep_start_setup / ns_per_clk;
	writel_relaxed(val, i2c_dev->base + SSC_REP_START_SETUP);

	/* Start hold time */
	val = t->start_hold / ns_per_clk;
	writel_relaxed(val, i2c_dev->base + SSC_START_HOLD);

	/* Data set up time */
	val = t->data_setup_time / ns_per_clk;
	writel_relaxed(val, i2c_dev->base + SSC_DATA_SETUP);

	/* Stop set up time */
	val = t->stop_setup_time / ns_per_clk;
	writel_relaxed(val, i2c_dev->base + SSC_STOP_SETUP);

	/* Bus free time */
	val = t->bus_free_time / ns_per_clk;
	writel_relaxed(val, i2c_dev->base + SSC_BUS_FREE);

	/* Prescalers set up */
	val = rate / 10000000;
	writel_relaxed(val, i2c_dev->base + SSC_PRSCALER);
	writel_relaxed(val, i2c_dev->base + SSC_PRSCALER_DATAOUT);

	/* Noise suppression witdh */
	val = i2c_dev->scl_min_width_us * rate / 100000000;
	writel_relaxed(val, i2c_dev->base + SSC_NOISE_SUPP_WIDTH);

	/* Noise suppression max output data delay width */
	val = i2c_dev->sda_min_width_us * rate / 100000000;
	writel_relaxed(val, i2c_dev->base + SSC_NOISE_SUPP_WIDTH_DATAOUT);
}

static int st_i2c_recover_bus(struct i2c_adapter *i2c_adap)
{
	struct st_i2c_dev *i2c_dev = i2c_get_adapdata(i2c_adap);
	u32 ctl;

	dev_dbg(i2c_dev->dev, "Trying to recover bus\n");

	/*
	 * SSP IP is dual role SPI/I2C to generate 9 clock pulses
	 * we switch to SPI node, 9 bit words and write a 0. This
	 * has been validate with a oscilloscope and is easier
	 * than switching to GPIO mode.
	 */

	/* Disable interrupts */
	writel_relaxed(0, i2c_dev->base + SSC_IEN);

	st_i2c_hw_config(i2c_dev);

	ctl = SSC_CTL_EN | SSC_CTL_MS |	SSC_CTL_EN_RX_FIFO | SSC_CTL_EN_TX_FIFO;
	st_i2c_set_bits(i2c_dev->base + SSC_CTL, ctl);

	st_i2c_clr_bits(i2c_dev->base + SSC_I2C, SSC_I2C_I2CM);
	usleep_range(8000, 10000);

	writel_relaxed(0, i2c_dev->base + SSC_TBUF);
	usleep_range(2000, 4000);
	st_i2c_set_bits(i2c_dev->base + SSC_I2C, SSC_I2C_I2CM);

	return 0;
}

static int st_i2c_wait_free_bus(struct st_i2c_dev *i2c_dev)
{
	u32 sta;
	int i, ret;

	for (i = 0; i < 10; i++) {
		sta = readl_relaxed(i2c_dev->base + SSC_STA);
		if (!(sta & SSC_STA_BUSY))
			return 0;

		usleep_range(2000, 4000);
	}

	dev_err(i2c_dev->dev, "bus not free (status = 0x%08x)\n", sta);

	ret = i2c_recover_bus(&i2c_dev->adap);
	if (ret) {
		dev_err(i2c_dev->dev, "Failed to recover the bus (%d)\n", ret);
		return ret;
	}

	return -EBUSY;
}

/**
 * st_i2c_write_tx_fifo() - Write a byte in the Tx FIFO
 * @i2c_dev: Controller's private data
 * @byte: Data to write in the Tx FIFO
 */
static inline void st_i2c_write_tx_fifo(struct st_i2c_dev *i2c_dev, u8 byte)
{
	u16 tbuf = byte << 1;

	writel_relaxed(tbuf | 1, i2c_dev->base + SSC_TBUF);
}

/**
 * st_i2c_wr_fill_tx_fifo() - Fill the Tx FIFO in write mode
 * @i2c_dev: Controller's private data
 *
 * This functions fills the Tx FIFO with I2C transfert buffer when
 * in write mode.
 */
static void st_i2c_wr_fill_tx_fifo(struct st_i2c_dev *i2c_dev)
{
	struct st_i2c_client *c = &i2c_dev->client;
	u32 tx_fstat, sta;
	int i;

	sta = readl_relaxed(i2c_dev->base + SSC_STA);
	if (sta & SSC_STA_TX_FIFO_FULL)
		return;

	tx_fstat = readl_relaxed(i2c_dev->base + SSC_TX_FSTAT);
	tx_fstat &= SSC_TX_FSTAT_STATUS;

	if (c->count < (SSC_TXFIFO_SIZE - tx_fstat))
		i = c->count;
	else
		i = SSC_TXFIFO_SIZE - tx_fstat;

	for (; i > 0; i--, c->count--, c->buf++)
		st_i2c_write_tx_fifo(i2c_dev, *c->buf);
}

/**
 * st_i2c_rd_fill_tx_fifo() - Fill the Tx FIFO in read mode
 * @i2c_dev: Controller's private data
 *
 * This functions fills the Tx FIFO with fixed pattern when
 * in read mode to trigger clock.
 */
static void st_i2c_rd_fill_tx_fifo(struct st_i2c_dev *i2c_dev, int max)
{
	struct st_i2c_client *c = &i2c_dev->client;
	u32 tx_fstat, sta;
	int i;

	sta = readl_relaxed(i2c_dev->base + SSC_STA);
	if (sta & SSC_STA_TX_FIFO_FULL)
		return;

	tx_fstat = readl_relaxed(i2c_dev->base + SSC_TX_FSTAT);
	tx_fstat &= SSC_TX_FSTAT_STATUS;

	if (max < (SSC_TXFIFO_SIZE - tx_fstat))
		i = max;
	else
		i = SSC_TXFIFO_SIZE - tx_fstat;

	for (; i > 0; i--, c->xfered++)
		st_i2c_write_tx_fifo(i2c_dev, 0xff);
}

static void st_i2c_read_rx_fifo(struct st_i2c_dev *i2c_dev)
{
	struct st_i2c_client *c = &i2c_dev->client;
	u32 i, sta;
	u16 rbuf;

	sta = readl_relaxed(i2c_dev->base + SSC_STA);
	if (sta & SSC_STA_RIR) {
		i = SSC_RXFIFO_SIZE;
	} else {
		i = readl_relaxed(i2c_dev->base + SSC_RX_FSTAT);
		i &= SSC_RX_FSTAT_STATUS;
	}

	for (; (i > 0) && (c->count > 0); i--, c->count--) {
		rbuf = readl_relaxed(i2c_dev->base + SSC_RBUF) >> 1;
		*c->buf++ = (u8)rbuf & 0xff;
	}

	if (i) {
		dev_err(i2c_dev->dev, "Unexpected %d bytes in rx fifo\n", i);
		st_i2c_flush_rx_fifo(i2c_dev);
	}
}

/**
 * st_i2c_terminate_xfer() - Send either STOP or REPSTART condition
 * @i2c_dev: Controller's private data
 */
static void st_i2c_terminate_xfer(struct st_i2c_dev *i2c_dev)
{
	struct st_i2c_client *c = &i2c_dev->client;

	st_i2c_clr_bits(i2c_dev->base + SSC_IEN, SSC_IEN_TEEN);
	st_i2c_clr_bits(i2c_dev->base + SSC_I2C, SSC_I2C_STRTG);

	if (c->stop) {
		st_i2c_set_bits(i2c_dev->base + SSC_IEN, SSC_IEN_STOPEN);
		st_i2c_set_bits(i2c_dev->base + SSC_I2C, SSC_I2C_STOPG);
	} else {
		st_i2c_set_bits(i2c_dev->base + SSC_IEN, SSC_IEN_REPSTRTEN);
		st_i2c_set_bits(i2c_dev->base + SSC_I2C, SSC_I2C_REPSTRTG);
	}
}

/**
 * st_i2c_handle_write() - Handle FIFO empty interrupt in case of write
 * @i2c_dev: Controller's private data
 */
static void st_i2c_handle_write(struct st_i2c_dev *i2c_dev)
{
	struct st_i2c_client *c = &i2c_dev->client;

	st_i2c_flush_rx_fifo(i2c_dev);

	if (!c->count)
		/* End of xfer, send stop or repstart */
		st_i2c_terminate_xfer(i2c_dev);
	else
		st_i2c_wr_fill_tx_fifo(i2c_dev);
}

/**
 * st_i2c_handle_write() - Handle FIFO enmpty interrupt in case of read
 * @i2c_dev: Controller's private data
 */
static void st_i2c_handle_read(struct st_i2c_dev *i2c_dev)
{
	struct st_i2c_client *c = &i2c_dev->client;
	u32 ien;

	/* Trash the address read back */
	if (!c->xfered) {
		readl_relaxed(i2c_dev->base + SSC_RBUF);
		st_i2c_clr_bits(i2c_dev->base + SSC_I2C, SSC_I2C_TXENB);
	} else {
		st_i2c_read_rx_fifo(i2c_dev);
	}

	if (!c->count) {
		/* End of xfer, send stop or repstart */
		st_i2c_terminate_xfer(i2c_dev);
	} else if (c->count == 1) {
		/* Penultimate byte to xfer, disable ACK gen. */
		st_i2c_clr_bits(i2c_dev->base + SSC_I2C, SSC_I2C_ACKG);

		/* Last received byte is to be handled by NACK interrupt */
		ien = SSC_IEN_NACKEN | SSC_IEN_ARBLEN;
		writel_relaxed(ien, i2c_dev->base + SSC_IEN);

		st_i2c_rd_fill_tx_fifo(i2c_dev, c->count);
	} else {
		st_i2c_rd_fill_tx_fifo(i2c_dev, c->count - 1);
	}
}

/**
 * st_i2c_isr() - Interrupt routine
 * @irq: interrupt number
 * @data: Controller's private data
 */
static irqreturn_t st_i2c_isr_thread(int irq, void *data)
{
	struct st_i2c_dev *i2c_dev = data;
	struct st_i2c_client *c = &i2c_dev->client;
	u32 sta, ien;
	int it;

	ien = readl_relaxed(i2c_dev->base + SSC_IEN);
	sta = readl_relaxed(i2c_dev->base + SSC_STA);

	/* Use __fls() to check error bits first */
	it = __fls(sta & ien);
	if (it < 0) {
		dev_dbg(i2c_dev->dev, "spurious it (sta=0x%04x, ien=0x%04x)\n",
				sta, ien);
		return IRQ_NONE;
	}

	switch (1 << it) {
	case SSC_STA_TE:
		if (c->addr & I2C_M_RD)
			st_i2c_handle_read(i2c_dev);
		else
			st_i2c_handle_write(i2c_dev);
		break;

	case SSC_STA_STOP:
	case SSC_STA_REPSTRT:
		writel_relaxed(0, i2c_dev->base + SSC_IEN);
		complete(&i2c_dev->complete);
		break;

	case SSC_STA_NACK:
		writel_relaxed(SSC_CLR_NACK, i2c_dev->base + SSC_CLR);

		/* Last received byte handled by NACK interrupt */
		if ((c->addr & I2C_M_RD) && (c->count == 1) && (c->xfered)) {
			st_i2c_handle_read(i2c_dev);
			break;
		}

		it = SSC_IEN_STOPEN | SSC_IEN_ARBLEN;
		writel_relaxed(it, i2c_dev->base + SSC_IEN);

		st_i2c_set_bits(i2c_dev->base + SSC_I2C, SSC_I2C_STOPG);
		c->result = -EIO;
		break;

	case SSC_STA_ARBL:
		writel_relaxed(SSC_CLR_SSCARBL, i2c_dev->base + SSC_CLR);

		it = SSC_IEN_STOPEN | SSC_IEN_ARBLEN;
		writel_relaxed(it, i2c_dev->base + SSC_IEN);

		st_i2c_set_bits(i2c_dev->base + SSC_I2C, SSC_I2C_STOPG);
		c->result = -EAGAIN;
		break;

	default:
		dev_err(i2c_dev->dev,
				"it %d unhandled (sta=0x%04x)\n", it, sta);
	}

	/*
	 * Read IEN register to ensure interrupt mask write is effective
	 * before re-enabling interrupt at GIC level, and thus avoid spurious
	 * interrupts.
	 */
	readl(i2c_dev->base + SSC_IEN);

	return IRQ_HANDLED;
}

/**
 * st_i2c_xfer_msg() - Transfer a single I2C message
 * @i2c_dev: Controller's private data
 * @msg: I2C message to transfer
 * @is_first: first message of the sequence
 * @is_last: last message of the sequence
 */
static int st_i2c_xfer_msg(struct st_i2c_dev *i2c_dev, struct i2c_msg *msg,
			    bool is_first, bool is_last)
{
	struct st_i2c_client *c = &i2c_dev->client;
	u32 ctl, i2c, it;
	unsigned long timeout;
	int ret;

	c->addr		= i2c_8bit_addr_from_msg(msg);
	c->buf		= msg->buf;
	c->count	= msg->len;
	c->xfered	= 0;
	c->result	= 0;
	c->stop		= is_last;

	reinit_completion(&i2c_dev->complete);

	ctl = SSC_CTL_EN | SSC_CTL_MS |	SSC_CTL_EN_RX_FIFO | SSC_CTL_EN_TX_FIFO;
	st_i2c_set_bits(i2c_dev->base + SSC_CTL, ctl);

	i2c = SSC_I2C_TXENB;
	if (c->addr & I2C_M_RD)
		i2c |= SSC_I2C_ACKG;
	st_i2c_set_bits(i2c_dev->base + SSC_I2C, i2c);

	/* Write slave address */
	st_i2c_write_tx_fifo(i2c_dev, c->addr);

	/* Pre-fill Tx fifo with data in case of write */
	if (!(c->addr & I2C_M_RD))
		st_i2c_wr_fill_tx_fifo(i2c_dev);

	it = SSC_IEN_NACKEN | SSC_IEN_TEEN | SSC_IEN_ARBLEN;
	writel_relaxed(it, i2c_dev->base + SSC_IEN);

	if (is_first) {
		ret = st_i2c_wait_free_bus(i2c_dev);
		if (ret)
			return ret;

		st_i2c_set_bits(i2c_dev->base + SSC_I2C, SSC_I2C_STRTG);
	}

	timeout = wait_for_completion_timeout(&i2c_dev->complete,
			i2c_dev->adap.timeout);
	ret = c->result;

	if (!timeout) {
		dev_err(i2c_dev->dev, "Write to slave 0x%x timed out\n",
				c->addr);
		ret = -ETIMEDOUT;
	}

	i2c = SSC_I2C_STOPG | SSC_I2C_REPSTRTG;
	st_i2c_clr_bits(i2c_dev->base + SSC_I2C, i2c);

	writel_relaxed(SSC_CLR_SSCSTOP | SSC_CLR_REPSTRT,
			i2c_dev->base + SSC_CLR);

	return ret;
}

/**
 * st_i2c_xfer() - Transfer a single I2C message
 * @i2c_adap: Adapter pointer to the controller
 * @msgs: Pointer to data to be written.
 * @num: Number of messages to be executed
 */
static int st_i2c_xfer(struct i2c_adapter *i2c_adap,
			struct i2c_msg msgs[], int num)
{
	struct st_i2c_dev *i2c_dev = i2c_get_adapdata(i2c_adap);
	int ret, i;

	i2c_dev->busy = true;

	ret = clk_prepare_enable(i2c_dev->clk);
	if (ret) {
		dev_err(i2c_dev->dev, "Failed to prepare_enable clock\n");
		return ret;
	}

	pinctrl_pm_select_default_state(i2c_dev->dev);

	st_i2c_hw_config(i2c_dev);

	for (i = 0; (i < num) && !ret; i++)
		ret = st_i2c_xfer_msg(i2c_dev, &msgs[i], i == 0, i == num - 1);

	pinctrl_pm_select_idle_state(i2c_dev->dev);

	clk_disable_unprepare(i2c_dev->clk);

	i2c_dev->busy = false;

	return (ret < 0) ? ret : i;
}

#ifdef CONFIG_PM_SLEEP
static int st_i2c_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct st_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	if (i2c_dev->busy)
		return -EBUSY;

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int st_i2c_resume(struct device *dev)
{
	pinctrl_pm_select_default_state(dev);
	/* Go in idle state if available */
	pinctrl_pm_select_idle_state(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(st_i2c_pm, st_i2c_suspend, st_i2c_resume);
#define ST_I2C_PM	(&st_i2c_pm)
#else
#define ST_I2C_PM	NULL
#endif

static u32 st_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm st_i2c_algo = {
	.master_xfer = st_i2c_xfer,
	.functionality = st_i2c_func,
};

static struct i2c_bus_recovery_info st_i2c_recovery_info = {
	.recover_bus = st_i2c_recover_bus,
};

static int st_i2c_of_get_deglitch(struct device_node *np,
		struct st_i2c_dev *i2c_dev)
{
	int ret;

	ret = of_property_read_u32(np, "st,i2c-min-scl-pulse-width-us",
			&i2c_dev->scl_min_width_us);
	if ((ret == -ENODATA) || (ret == -EOVERFLOW)) {
		dev_err(i2c_dev->dev, "st,i2c-min-scl-pulse-width-us invalid\n");
		return ret;
	}

	ret = of_property_read_u32(np, "st,i2c-min-sda-pulse-width-us",
			&i2c_dev->sda_min_width_us);
	if ((ret == -ENODATA) || (ret == -EOVERFLOW)) {
		dev_err(i2c_dev->dev, "st,i2c-min-sda-pulse-width-us invalid\n");
		return ret;
	}

	return 0;
}

static int st_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct st_i2c_dev *i2c_dev;
	struct resource *res;
	u32 clk_rate;
	struct i2c_adapter *adap;
	int ret;

	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c_dev->base))
		return PTR_ERR(i2c_dev->base);

	i2c_dev->irq = irq_of_parse_and_map(np, 0);
	if (!i2c_dev->irq) {
		dev_err(&pdev->dev, "IRQ missing or invalid\n");
		return -EINVAL;
	}

	i2c_dev->clk = of_clk_get_by_name(np, "ssc");
	if (IS_ERR(i2c_dev->clk)) {
		dev_err(&pdev->dev, "Unable to request clock\n");
		return PTR_ERR(i2c_dev->clk);
	}

	i2c_dev->mode = I2C_MODE_STANDARD;
	ret = of_property_read_u32(np, "clock-frequency", &clk_rate);
	if ((!ret) && (clk_rate == 400000))
		i2c_dev->mode = I2C_MODE_FAST;

	i2c_dev->dev = &pdev->dev;

	ret = devm_request_threaded_irq(&pdev->dev, i2c_dev->irq,
			NULL, st_i2c_isr_thread,
			IRQF_ONESHOT, pdev->name, i2c_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq %i\n", i2c_dev->irq);
		return ret;
	}

	pinctrl_pm_select_default_state(i2c_dev->dev);
	/* In case idle state available, select it */
	pinctrl_pm_select_idle_state(i2c_dev->dev);

	ret = st_i2c_of_get_deglitch(np, i2c_dev);
	if (ret)
		return ret;

	adap = &i2c_dev->adap;
	i2c_set_adapdata(adap, i2c_dev);
	snprintf(adap->name, sizeof(adap->name), "ST I2C(%pa)", &res->start);
	adap->owner = THIS_MODULE;
	adap->timeout = 2 * HZ;
	adap->retries = 0;
	adap->algo = &st_i2c_algo;
	adap->bus_recovery_info = &st_i2c_recovery_info;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	init_completion(&i2c_dev->complete);

	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add adapter\n");
		return ret;
	}

	platform_set_drvdata(pdev, i2c_dev);

	dev_info(i2c_dev->dev, "%s initialized\n", adap->name);

	return 0;
}

static int st_i2c_remove(struct platform_device *pdev)
{
	struct st_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c_dev->adap);

	return 0;
}

static const struct of_device_id st_i2c_match[] = {
	{ .compatible = "st,comms-ssc-i2c", },
	{ .compatible = "st,comms-ssc4-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, st_i2c_match);

static struct platform_driver st_i2c_driver = {
	.driver = {
		.name = "st-i2c",
		.of_match_table = st_i2c_match,
		.pm = ST_I2C_PM,
	},
	.probe = st_i2c_probe,
	.remove = st_i2c_remove,
};

module_platform_driver(st_i2c_driver);

MODULE_AUTHOR("Maxime Coquelin <maxime.coquelin@st.com>");
MODULE_DESCRIPTION("STMicroelectronics I2C driver");
MODULE_LICENSE("GPL v2");
