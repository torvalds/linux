// SPDX-License-Identifier: GPL-2.0+
/*
 * This is i.MX low power i2c controller driver.
 *
 * Copyright 2016 Freescale Semiconductor, Inc.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/slab.h>

#define DRIVER_NAME "imx-lpi2c"

#define LPI2C_PARAM	0x04	/* i2c RX/TX FIFO size */
#define LPI2C_MCR	0x10	/* i2c contrl register */
#define LPI2C_MSR	0x14	/* i2c status register */
#define LPI2C_MIER	0x18	/* i2c interrupt enable */
#define LPI2C_MDER	0x1C	/* i2c DMA enable */
#define LPI2C_MCFGR0	0x20	/* i2c master configuration */
#define LPI2C_MCFGR1	0x24	/* i2c master configuration */
#define LPI2C_MCFGR2	0x28	/* i2c master configuration */
#define LPI2C_MCFGR3	0x2C	/* i2c master configuration */
#define LPI2C_MCCR0	0x48	/* i2c master clk configuration */
#define LPI2C_MCCR1	0x50	/* i2c master clk configuration */
#define LPI2C_MFCR	0x58	/* i2c master FIFO control */
#define LPI2C_MFSR	0x5C	/* i2c master FIFO status */
#define LPI2C_MTDR	0x60	/* i2c master TX data register */
#define LPI2C_MRDR	0x70	/* i2c master RX data register */

#define LPI2C_SCR	0x110	/* i2c target control register */
#define LPI2C_SSR	0x114	/* i2c target status register */
#define LPI2C_SIER	0x118	/* i2c target interrupt enable */
#define LPI2C_SDER	0x11C	/* i2c target DMA enable */
#define LPI2C_SCFGR0	0x120	/* i2c target configuration */
#define LPI2C_SCFGR1	0x124	/* i2c target configuration */
#define LPI2C_SCFGR2	0x128	/* i2c target configuration */
#define LPI2C_SAMR	0x140	/* i2c target address match */
#define LPI2C_SASR	0x150	/* i2c target address status */
#define LPI2C_STAR	0x154	/* i2c target transmit ACK */
#define LPI2C_STDR	0x160	/* i2c target transmit data */
#define LPI2C_SRDR	0x170	/* i2c target receive data */
#define LPI2C_SRDROR	0x178	/* i2c target receive data read only */

/* i2c command */
#define TRAN_DATA	0X00
#define RECV_DATA	0X01
#define GEN_STOP	0X02
#define RECV_DISCARD	0X03
#define GEN_START	0X04
#define START_NACK	0X05
#define START_HIGH	0X06
#define START_HIGH_NACK	0X07

#define MCR_MEN		BIT(0)
#define MCR_RST		BIT(1)
#define MCR_DOZEN	BIT(2)
#define MCR_DBGEN	BIT(3)
#define MCR_RTF		BIT(8)
#define MCR_RRF		BIT(9)
#define MSR_TDF		BIT(0)
#define MSR_RDF		BIT(1)
#define MSR_SDF		BIT(9)
#define MSR_NDF		BIT(10)
#define MSR_ALF		BIT(11)
#define MSR_MBF		BIT(24)
#define MSR_BBF		BIT(25)
#define MIER_TDIE	BIT(0)
#define MIER_RDIE	BIT(1)
#define MIER_SDIE	BIT(9)
#define MIER_NDIE	BIT(10)
#define MCFGR1_AUTOSTOP	BIT(8)
#define MCFGR1_IGNACK	BIT(9)
#define MRDR_RXEMPTY	BIT(14)
#define MDER_TDDE	BIT(0)
#define MDER_RDDE	BIT(1)

#define SCR_SEN		BIT(0)
#define SCR_RST		BIT(1)
#define SCR_FILTEN	BIT(4)
#define SCR_RTF		BIT(8)
#define SCR_RRF		BIT(9)
#define SSR_TDF		BIT(0)
#define SSR_RDF		BIT(1)
#define SSR_AVF		BIT(2)
#define SSR_TAF		BIT(3)
#define SSR_RSF		BIT(8)
#define SSR_SDF		BIT(9)
#define SSR_BEF		BIT(10)
#define SSR_FEF		BIT(11)
#define SSR_SBF		BIT(24)
#define SSR_BBF		BIT(25)
#define SSR_CLEAR_BITS	(SSR_RSF | SSR_SDF | SSR_BEF | SSR_FEF)
#define SIER_TDIE	BIT(0)
#define SIER_RDIE	BIT(1)
#define SIER_AVIE	BIT(2)
#define SIER_TAIE	BIT(3)
#define SIER_RSIE	BIT(8)
#define SIER_SDIE	BIT(9)
#define SIER_BEIE	BIT(10)
#define SIER_FEIE	BIT(11)
#define SIER_AM0F	BIT(12)
#define SCFGR1_RXSTALL	BIT(1)
#define SCFGR1_TXDSTALL	BIT(2)
#define SCFGR2_FILTSDA_SHIFT	24
#define SCFGR2_FILTSCL_SHIFT	16
#define SCFGR2_CLKHOLD(x)	(x)
#define SCFGR2_FILTSDA(x)	((x) << SCFGR2_FILTSDA_SHIFT)
#define SCFGR2_FILTSCL(x)	((x) << SCFGR2_FILTSCL_SHIFT)
#define SASR_READ_REQ	0x1
#define SLAVE_INT_FLAG	(SIER_TDIE | SIER_RDIE | SIER_AVIE | \
			 SIER_SDIE | SIER_BEIE)

#define I2C_CLK_RATIO	2
#define CHUNK_DATA	256

#define I2C_PM_TIMEOUT		10 /* ms */
#define I2C_DMA_THRESHOLD	8 /* bytes */

enum lpi2c_imx_mode {
	STANDARD,	/* 100+Kbps */
	FAST,		/* 400+Kbps */
	FAST_PLUS,	/* 1.0+Mbps */
	HS,		/* 3.4+Mbps */
	ULTRA_FAST,	/* 5.0+Mbps */
};

enum lpi2c_imx_pincfg {
	TWO_PIN_OD,
	TWO_PIN_OO,
	TWO_PIN_PP,
	FOUR_PIN_PP,
};

struct lpi2c_imx_dma {
	bool		using_pio_mode;
	u8		rx_cmd_buf_len;
	u8		*dma_buf;
	u16		*rx_cmd_buf;
	unsigned int	dma_len;
	unsigned int	tx_burst_num;
	unsigned int	rx_burst_num;
	unsigned long	dma_msg_flag;
	resource_size_t	phy_addr;
	dma_addr_t	dma_tx_addr;
	dma_addr_t	dma_addr;
	enum dma_data_direction dma_data_dir;
	enum dma_transfer_direction dma_transfer_dir;
	struct dma_chan	*chan_tx;
	struct dma_chan	*chan_rx;
};

struct lpi2c_imx_struct {
	struct i2c_adapter	adapter;
	int			num_clks;
	struct clk_bulk_data	*clks;
	void __iomem		*base;
	__u8			*rx_buf;
	__u8			*tx_buf;
	struct completion	complete;
	unsigned long		rate_per;
	unsigned int		msglen;
	unsigned int		delivered;
	unsigned int		block_data;
	unsigned int		bitrate;
	unsigned int		txfifosize;
	unsigned int		rxfifosize;
	enum lpi2c_imx_mode	mode;
	struct i2c_bus_recovery_info rinfo;
	bool			can_use_dma;
	struct lpi2c_imx_dma	*dma;
	struct i2c_client	*target;
};

static void lpi2c_imx_intctrl(struct lpi2c_imx_struct *lpi2c_imx,
			      unsigned int enable)
{
	writel(enable, lpi2c_imx->base + LPI2C_MIER);
}

static int lpi2c_imx_bus_busy(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned long orig_jiffies = jiffies;
	unsigned int temp;

	while (1) {
		temp = readl(lpi2c_imx->base + LPI2C_MSR);

		/* check for arbitration lost, clear if set */
		if (temp & MSR_ALF) {
			writel(temp, lpi2c_imx->base + LPI2C_MSR);
			return -EAGAIN;
		}

		if (temp & (MSR_BBF | MSR_MBF))
			break;

		if (time_after(jiffies, orig_jiffies + msecs_to_jiffies(500))) {
			dev_dbg(&lpi2c_imx->adapter.dev, "bus not work\n");
			if (lpi2c_imx->adapter.bus_recovery_info)
				i2c_recover_bus(&lpi2c_imx->adapter);
			return -ETIMEDOUT;
		}
		schedule();
	}

	return 0;
}

static void lpi2c_imx_set_mode(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned int bitrate = lpi2c_imx->bitrate;
	enum lpi2c_imx_mode mode;

	if (bitrate < I2C_MAX_FAST_MODE_FREQ)
		mode = STANDARD;
	else if (bitrate < I2C_MAX_FAST_MODE_PLUS_FREQ)
		mode = FAST;
	else if (bitrate < I2C_MAX_HIGH_SPEED_MODE_FREQ)
		mode = FAST_PLUS;
	else if (bitrate < I2C_MAX_ULTRA_FAST_MODE_FREQ)
		mode = HS;
	else
		mode = ULTRA_FAST;

	lpi2c_imx->mode = mode;
}

static int lpi2c_imx_start(struct lpi2c_imx_struct *lpi2c_imx,
			   struct i2c_msg *msgs)
{
	unsigned int temp;

	temp = readl(lpi2c_imx->base + LPI2C_MCR);
	temp |= MCR_RRF | MCR_RTF;
	writel(temp, lpi2c_imx->base + LPI2C_MCR);
	writel(0x7f00, lpi2c_imx->base + LPI2C_MSR);

	temp = i2c_8bit_addr_from_msg(msgs) | (GEN_START << 8);
	writel(temp, lpi2c_imx->base + LPI2C_MTDR);

	return lpi2c_imx_bus_busy(lpi2c_imx);
}

static void lpi2c_imx_stop(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned long orig_jiffies = jiffies;
	unsigned int temp;

	writel(GEN_STOP << 8, lpi2c_imx->base + LPI2C_MTDR);

	do {
		temp = readl(lpi2c_imx->base + LPI2C_MSR);
		if (temp & MSR_SDF)
			break;

		if (time_after(jiffies, orig_jiffies + msecs_to_jiffies(500))) {
			dev_dbg(&lpi2c_imx->adapter.dev, "stop timeout\n");
			if (lpi2c_imx->adapter.bus_recovery_info)
				i2c_recover_bus(&lpi2c_imx->adapter);
			break;
		}
		schedule();

	} while (1);
}

/* CLKLO = I2C_CLK_RATIO * CLKHI, SETHOLD = CLKHI, DATAVD = CLKHI/2 */
static int lpi2c_imx_config(struct lpi2c_imx_struct *lpi2c_imx)
{
	u8 prescale, filt, sethold, datavd;
	unsigned int clk_rate, clk_cycle, clkhi, clklo;
	enum lpi2c_imx_pincfg pincfg;
	unsigned int temp;

	lpi2c_imx_set_mode(lpi2c_imx);

	clk_rate = lpi2c_imx->rate_per;

	if (lpi2c_imx->mode == HS || lpi2c_imx->mode == ULTRA_FAST)
		filt = 0;
	else
		filt = 2;

	for (prescale = 0; prescale <= 7; prescale++) {
		clk_cycle = clk_rate / ((1 << prescale) * lpi2c_imx->bitrate)
			    - 3 - (filt >> 1);
		clkhi = DIV_ROUND_UP(clk_cycle, I2C_CLK_RATIO + 1);
		clklo = clk_cycle - clkhi;
		if (clklo < 64)
			break;
	}

	if (prescale > 7)
		return -EINVAL;

	/* set MCFGR1: PINCFG, PRESCALE, IGNACK */
	if (lpi2c_imx->mode == ULTRA_FAST)
		pincfg = TWO_PIN_OO;
	else
		pincfg = TWO_PIN_OD;
	temp = prescale | pincfg << 24;

	if (lpi2c_imx->mode == ULTRA_FAST)
		temp |= MCFGR1_IGNACK;

	writel(temp, lpi2c_imx->base + LPI2C_MCFGR1);

	/* set MCFGR2: FILTSDA, FILTSCL */
	temp = (filt << 16) | (filt << 24);
	writel(temp, lpi2c_imx->base + LPI2C_MCFGR2);

	/* set MCCR: DATAVD, SETHOLD, CLKHI, CLKLO */
	sethold = clkhi;
	datavd = clkhi >> 1;
	temp = datavd << 24 | sethold << 16 | clkhi << 8 | clklo;

	if (lpi2c_imx->mode == HS)
		writel(temp, lpi2c_imx->base + LPI2C_MCCR1);
	else
		writel(temp, lpi2c_imx->base + LPI2C_MCCR0);

	return 0;
}

static int lpi2c_imx_master_enable(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned int temp;
	int ret;

	ret = pm_runtime_resume_and_get(lpi2c_imx->adapter.dev.parent);
	if (ret < 0)
		return ret;

	temp = MCR_RST;
	writel(temp, lpi2c_imx->base + LPI2C_MCR);
	writel(0, lpi2c_imx->base + LPI2C_MCR);

	ret = lpi2c_imx_config(lpi2c_imx);
	if (ret)
		goto rpm_put;

	temp = readl(lpi2c_imx->base + LPI2C_MCR);
	temp |= MCR_MEN;
	writel(temp, lpi2c_imx->base + LPI2C_MCR);

	return 0;

rpm_put:
	pm_runtime_mark_last_busy(lpi2c_imx->adapter.dev.parent);
	pm_runtime_put_autosuspend(lpi2c_imx->adapter.dev.parent);

	return ret;
}

static int lpi2c_imx_master_disable(struct lpi2c_imx_struct *lpi2c_imx)
{
	u32 temp;

	temp = readl(lpi2c_imx->base + LPI2C_MCR);
	temp &= ~MCR_MEN;
	writel(temp, lpi2c_imx->base + LPI2C_MCR);

	pm_runtime_mark_last_busy(lpi2c_imx->adapter.dev.parent);
	pm_runtime_put_autosuspend(lpi2c_imx->adapter.dev.parent);

	return 0;
}

static int lpi2c_imx_pio_msg_complete(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned long time_left;

	time_left = wait_for_completion_timeout(&lpi2c_imx->complete, HZ);

	return time_left ? 0 : -ETIMEDOUT;
}

static int lpi2c_imx_txfifo_empty(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned long orig_jiffies = jiffies;
	u32 txcnt;

	do {
		txcnt = readl(lpi2c_imx->base + LPI2C_MFSR) & 0xff;

		if (readl(lpi2c_imx->base + LPI2C_MSR) & MSR_NDF) {
			dev_dbg(&lpi2c_imx->adapter.dev, "NDF detected\n");
			return -EIO;
		}

		if (time_after(jiffies, orig_jiffies + msecs_to_jiffies(500))) {
			dev_dbg(&lpi2c_imx->adapter.dev, "txfifo empty timeout\n");
			if (lpi2c_imx->adapter.bus_recovery_info)
				i2c_recover_bus(&lpi2c_imx->adapter);
			return -ETIMEDOUT;
		}
		schedule();

	} while (txcnt);

	return 0;
}

static void lpi2c_imx_set_tx_watermark(struct lpi2c_imx_struct *lpi2c_imx)
{
	writel(lpi2c_imx->txfifosize >> 1, lpi2c_imx->base + LPI2C_MFCR);
}

static void lpi2c_imx_set_rx_watermark(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned int temp, remaining;

	remaining = lpi2c_imx->msglen - lpi2c_imx->delivered;

	if (remaining > (lpi2c_imx->rxfifosize >> 1))
		temp = lpi2c_imx->rxfifosize >> 1;
	else
		temp = 0;

	writel(temp << 16, lpi2c_imx->base + LPI2C_MFCR);
}

static void lpi2c_imx_write_txfifo(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned int data, txcnt;

	txcnt = readl(lpi2c_imx->base + LPI2C_MFSR) & 0xff;

	while (txcnt < lpi2c_imx->txfifosize) {
		if (lpi2c_imx->delivered == lpi2c_imx->msglen)
			break;

		data = lpi2c_imx->tx_buf[lpi2c_imx->delivered++];
		writel(data, lpi2c_imx->base + LPI2C_MTDR);
		txcnt++;
	}

	if (lpi2c_imx->delivered < lpi2c_imx->msglen)
		lpi2c_imx_intctrl(lpi2c_imx, MIER_TDIE | MIER_NDIE);
	else
		complete(&lpi2c_imx->complete);
}

static void lpi2c_imx_read_rxfifo(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned int blocklen, remaining;
	unsigned int temp, data;

	do {
		data = readl(lpi2c_imx->base + LPI2C_MRDR);
		if (data & MRDR_RXEMPTY)
			break;

		lpi2c_imx->rx_buf[lpi2c_imx->delivered++] = data & 0xff;
	} while (1);

	/*
	 * First byte is the length of remaining packet in the SMBus block
	 * data read. Add it to msgs->len.
	 */
	if (lpi2c_imx->block_data) {
		blocklen = lpi2c_imx->rx_buf[0];
		lpi2c_imx->msglen += blocklen;
	}

	remaining = lpi2c_imx->msglen - lpi2c_imx->delivered;

	if (!remaining) {
		complete(&lpi2c_imx->complete);
		return;
	}

	/* not finished, still waiting for rx data */
	lpi2c_imx_set_rx_watermark(lpi2c_imx);

	/* multiple receive commands */
	if (lpi2c_imx->block_data) {
		lpi2c_imx->block_data = 0;
		temp = remaining;
		temp |= (RECV_DATA << 8);
		writel(temp, lpi2c_imx->base + LPI2C_MTDR);
	} else if (!(lpi2c_imx->delivered & 0xff)) {
		temp = (remaining > CHUNK_DATA ? CHUNK_DATA : remaining) - 1;
		temp |= (RECV_DATA << 8);
		writel(temp, lpi2c_imx->base + LPI2C_MTDR);
	}

	lpi2c_imx_intctrl(lpi2c_imx, MIER_RDIE);
}

static void lpi2c_imx_write(struct lpi2c_imx_struct *lpi2c_imx,
			    struct i2c_msg *msgs)
{
	lpi2c_imx->tx_buf = msgs->buf;
	lpi2c_imx_set_tx_watermark(lpi2c_imx);
	lpi2c_imx_write_txfifo(lpi2c_imx);
}

static void lpi2c_imx_read(struct lpi2c_imx_struct *lpi2c_imx,
			   struct i2c_msg *msgs)
{
	unsigned int temp;

	lpi2c_imx->rx_buf = msgs->buf;
	lpi2c_imx->block_data = msgs->flags & I2C_M_RECV_LEN;

	lpi2c_imx_set_rx_watermark(lpi2c_imx);
	temp = msgs->len > CHUNK_DATA ? CHUNK_DATA - 1 : msgs->len - 1;
	temp |= (RECV_DATA << 8);
	writel(temp, lpi2c_imx->base + LPI2C_MTDR);

	lpi2c_imx_intctrl(lpi2c_imx, MIER_RDIE | MIER_NDIE);
}

static bool is_use_dma(struct lpi2c_imx_struct *lpi2c_imx, struct i2c_msg *msg)
{
	if (!lpi2c_imx->can_use_dma)
		return false;

	/*
	 * When the length of data is less than I2C_DMA_THRESHOLD,
	 * cpu mode is used directly to avoid low performance.
	 */
	return !(msg->len < I2C_DMA_THRESHOLD);
}

static int lpi2c_imx_pio_xfer(struct lpi2c_imx_struct *lpi2c_imx,
			      struct i2c_msg *msg)
{
	reinit_completion(&lpi2c_imx->complete);

	if (msg->flags & I2C_M_RD)
		lpi2c_imx_read(lpi2c_imx, msg);
	else
		lpi2c_imx_write(lpi2c_imx, msg);

	return lpi2c_imx_pio_msg_complete(lpi2c_imx);
}

static int lpi2c_imx_dma_timeout_calculate(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned long time = 0;

	time = 8 * lpi2c_imx->dma->dma_len * 1000 / lpi2c_imx->bitrate;

	/* Add extra second for scheduler related activities */
	time += 1;

	/* Double calculated time */
	return msecs_to_jiffies(time * MSEC_PER_SEC);
}

static int lpi2c_imx_alloc_rx_cmd_buf(struct lpi2c_imx_struct *lpi2c_imx)
{
	struct lpi2c_imx_dma *dma = lpi2c_imx->dma;
	u16 rx_remain = dma->dma_len;
	int cmd_num;
	u16 temp;

	/*
	 * Calculate the number of rx command words via the DMA TX channel
	 * writing into command register based on the i2c msg len, and build
	 * the rx command words buffer.
	 */
	cmd_num = DIV_ROUND_UP(rx_remain, CHUNK_DATA);
	dma->rx_cmd_buf = kcalloc(cmd_num, sizeof(u16), GFP_KERNEL);
	dma->rx_cmd_buf_len = cmd_num * sizeof(u16);

	if (!dma->rx_cmd_buf) {
		dev_err(&lpi2c_imx->adapter.dev, "Alloc RX cmd buffer failed\n");
		return -ENOMEM;
	}

	for (int i = 0; i < cmd_num ; i++) {
		temp = rx_remain > CHUNK_DATA ? CHUNK_DATA - 1 : rx_remain - 1;
		temp |= (RECV_DATA << 8);
		rx_remain -= CHUNK_DATA;
		dma->rx_cmd_buf[i] = temp;
	}

	return 0;
}

static int lpi2c_imx_dma_msg_complete(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned long time_left, time;

	time = lpi2c_imx_dma_timeout_calculate(lpi2c_imx);
	time_left = wait_for_completion_timeout(&lpi2c_imx->complete, time);
	if (time_left == 0) {
		dev_err(&lpi2c_imx->adapter.dev, "I/O Error in DMA Data Transfer\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void lpi2c_dma_unmap(struct lpi2c_imx_dma *dma)
{
	struct dma_chan *chan = dma->dma_data_dir == DMA_FROM_DEVICE
				? dma->chan_rx : dma->chan_tx;

	dma_unmap_single(chan->device->dev, dma->dma_addr,
			 dma->dma_len, dma->dma_data_dir);

	dma->dma_data_dir = DMA_NONE;
}

static void lpi2c_cleanup_rx_cmd_dma(struct lpi2c_imx_dma *dma)
{
	dmaengine_terminate_sync(dma->chan_tx);
	dma_unmap_single(dma->chan_tx->device->dev, dma->dma_tx_addr,
			 dma->rx_cmd_buf_len, DMA_TO_DEVICE);
}

static void lpi2c_cleanup_dma(struct lpi2c_imx_dma *dma)
{
	if (dma->dma_data_dir == DMA_FROM_DEVICE)
		dmaengine_terminate_sync(dma->chan_rx);
	else if (dma->dma_data_dir == DMA_TO_DEVICE)
		dmaengine_terminate_sync(dma->chan_tx);

	lpi2c_dma_unmap(dma);
}

static void lpi2c_dma_callback(void *data)
{
	struct lpi2c_imx_struct *lpi2c_imx = (struct lpi2c_imx_struct *)data;

	complete(&lpi2c_imx->complete);
}

static int lpi2c_dma_rx_cmd_submit(struct lpi2c_imx_struct *lpi2c_imx)
{
	struct dma_async_tx_descriptor *rx_cmd_desc;
	struct lpi2c_imx_dma *dma = lpi2c_imx->dma;
	struct dma_chan *txchan = dma->chan_tx;
	dma_cookie_t cookie;

	dma->dma_tx_addr = dma_map_single(txchan->device->dev,
					  dma->rx_cmd_buf, dma->rx_cmd_buf_len,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(txchan->device->dev, dma->dma_tx_addr)) {
		dev_err(&lpi2c_imx->adapter.dev, "DMA map failed, use pio\n");
		return -EINVAL;
	}

	rx_cmd_desc = dmaengine_prep_slave_single(txchan, dma->dma_tx_addr,
						  dma->rx_cmd_buf_len, DMA_MEM_TO_DEV,
						  DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rx_cmd_desc) {
		dev_err(&lpi2c_imx->adapter.dev, "DMA prep slave sg failed, use pio\n");
		goto desc_prepare_err_exit;
	}

	cookie = dmaengine_submit(rx_cmd_desc);
	if (dma_submit_error(cookie)) {
		dev_err(&lpi2c_imx->adapter.dev, "submitting DMA failed, use pio\n");
		goto submit_err_exit;
	}

	dma_async_issue_pending(txchan);

	return 0;

desc_prepare_err_exit:
	dma_unmap_single(txchan->device->dev, dma->dma_tx_addr,
			 dma->rx_cmd_buf_len, DMA_TO_DEVICE);
	return -EINVAL;

submit_err_exit:
	dma_unmap_single(txchan->device->dev, dma->dma_tx_addr,
			 dma->rx_cmd_buf_len, DMA_TO_DEVICE);
	dmaengine_desc_free(rx_cmd_desc);
	return -EINVAL;
}

static int lpi2c_dma_submit(struct lpi2c_imx_struct *lpi2c_imx)
{
	struct lpi2c_imx_dma *dma = lpi2c_imx->dma;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *chan;
	dma_cookie_t cookie;

	if (dma->dma_msg_flag & I2C_M_RD) {
		chan = dma->chan_rx;
		dma->dma_data_dir = DMA_FROM_DEVICE;
		dma->dma_transfer_dir = DMA_DEV_TO_MEM;
	} else {
		chan = dma->chan_tx;
		dma->dma_data_dir = DMA_TO_DEVICE;
		dma->dma_transfer_dir = DMA_MEM_TO_DEV;
	}

	dma->dma_addr = dma_map_single(chan->device->dev,
				       dma->dma_buf, dma->dma_len, dma->dma_data_dir);
	if (dma_mapping_error(chan->device->dev, dma->dma_addr)) {
		dev_err(&lpi2c_imx->adapter.dev, "DMA map failed, use pio\n");
		return -EINVAL;
	}

	desc = dmaengine_prep_slave_single(chan, dma->dma_addr,
					   dma->dma_len, dma->dma_transfer_dir,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(&lpi2c_imx->adapter.dev, "DMA prep slave sg failed, use pio\n");
		goto desc_prepare_err_exit;
	}

	reinit_completion(&lpi2c_imx->complete);
	desc->callback = lpi2c_dma_callback;
	desc->callback_param = lpi2c_imx;

	cookie = dmaengine_submit(desc);
	if (dma_submit_error(cookie)) {
		dev_err(&lpi2c_imx->adapter.dev, "submitting DMA failed, use pio\n");
		goto submit_err_exit;
	}

	/* Can't switch to PIO mode when DMA have started transfer */
	dma->using_pio_mode = false;

	dma_async_issue_pending(chan);

	return 0;

desc_prepare_err_exit:
	lpi2c_dma_unmap(dma);
	return -EINVAL;

submit_err_exit:
	lpi2c_dma_unmap(dma);
	dmaengine_desc_free(desc);
	return -EINVAL;
}

static int lpi2c_imx_find_max_burst_num(unsigned int fifosize, unsigned int len)
{
	unsigned int i;

	for (i = fifosize / 2; i > 0; i--)
		if (!(len % i))
			break;

	return i;
}

/*
 * For a highest DMA efficiency, tx/rx burst number should be calculated according
 * to the FIFO depth.
 */
static void lpi2c_imx_dma_burst_num_calculate(struct lpi2c_imx_struct *lpi2c_imx)
{
	struct lpi2c_imx_dma *dma = lpi2c_imx->dma;
	unsigned int cmd_num;

	if (dma->dma_msg_flag & I2C_M_RD) {
		/*
		 * One RX cmd word can trigger DMA receive no more than 256 bytes.
		 * The number of RX cmd words should be calculated based on the data
		 * length.
		 */
		cmd_num = DIV_ROUND_UP(dma->dma_len, CHUNK_DATA);
		dma->tx_burst_num = lpi2c_imx_find_max_burst_num(lpi2c_imx->txfifosize,
								 cmd_num);
		dma->rx_burst_num = lpi2c_imx_find_max_burst_num(lpi2c_imx->rxfifosize,
								 dma->dma_len);
	} else {
		dma->tx_burst_num = lpi2c_imx_find_max_burst_num(lpi2c_imx->txfifosize,
								 dma->dma_len);
	}
}

static int lpi2c_dma_config(struct lpi2c_imx_struct *lpi2c_imx)
{
	struct lpi2c_imx_dma *dma = lpi2c_imx->dma;
	struct dma_slave_config rx = {}, tx = {};
	int ret;

	lpi2c_imx_dma_burst_num_calculate(lpi2c_imx);

	if (dma->dma_msg_flag & I2C_M_RD) {
		tx.dst_addr = dma->phy_addr + LPI2C_MTDR;
		tx.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		tx.dst_maxburst = dma->tx_burst_num;
		tx.direction = DMA_MEM_TO_DEV;
		ret = dmaengine_slave_config(dma->chan_tx, &tx);
		if (ret < 0)
			return ret;

		rx.src_addr = dma->phy_addr + LPI2C_MRDR;
		rx.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		rx.src_maxburst = dma->rx_burst_num;
		rx.direction = DMA_DEV_TO_MEM;
		ret = dmaengine_slave_config(dma->chan_rx, &rx);
		if (ret < 0)
			return ret;
	} else {
		tx.dst_addr = dma->phy_addr + LPI2C_MTDR;
		tx.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		tx.dst_maxburst = dma->tx_burst_num;
		tx.direction = DMA_MEM_TO_DEV;
		ret = dmaengine_slave_config(dma->chan_tx, &tx);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void lpi2c_dma_enable(struct lpi2c_imx_struct *lpi2c_imx)
{
	struct lpi2c_imx_dma *dma = lpi2c_imx->dma;
	/*
	 * TX interrupt will be triggered when the number of words in
	 * the transmit FIFO is equal or less than TX watermark.
	 * RX interrupt will be triggered when the number of words in
	 * the receive FIFO is greater than RX watermark.
	 * In order to trigger the DMA interrupt, TX watermark should be
	 * set equal to the DMA TX burst number but RX watermark should
	 * be set less than the DMA RX burst number.
	 */
	if (dma->dma_msg_flag & I2C_M_RD) {
		/* Set I2C TX/RX watermark */
		writel(dma->tx_burst_num | (dma->rx_burst_num - 1) << 16,
		       lpi2c_imx->base + LPI2C_MFCR);
		/* Enable I2C DMA TX/RX function */
		writel(MDER_TDDE | MDER_RDDE, lpi2c_imx->base + LPI2C_MDER);
	} else {
		/* Set I2C TX watermark */
		writel(dma->tx_burst_num, lpi2c_imx->base + LPI2C_MFCR);
		/* Enable I2C DMA TX function */
		writel(MDER_TDDE, lpi2c_imx->base + LPI2C_MDER);
	}

	/* Enable NACK detected */
	lpi2c_imx_intctrl(lpi2c_imx, MIER_NDIE);
};

/*
 * When lpi2c is in TX DMA mode we can use one DMA TX channel to write
 * data word into TXFIFO, but in RX DMA mode it is different.
 *
 * The LPI2C MTDR register is a command data and transmit data register.
 * Bits 8-10 are the command data field and Bits 0-7 are the transmit
 * data field. When the LPI2C master needs to read data, the number of
 * bytes to read should be set in the command field and RECV_DATA should
 * be set into the command data field to receive (DATA[7:0] + 1) bytes.
 * The recv data command word is made of RECV_DATA in the command data
 * field and the number of bytes to read in transmit data field. When the
 * length of data to be read exceeds 256 bytes, recv data command word
 * needs to be written to TXFIFO multiple times.
 *
 * So when in RX DMA mode, the TX channel also must to be configured to
 * send RX command words and the RX command word must be set in advance
 * before transmitting.
 */
static int lpi2c_imx_dma_xfer(struct lpi2c_imx_struct *lpi2c_imx,
			      struct i2c_msg *msg)
{
	struct lpi2c_imx_dma *dma = lpi2c_imx->dma;
	int ret;

	/* When DMA mode fails before transferring, CPU mode can be used. */
	dma->using_pio_mode = true;

	dma->dma_len = msg->len;
	dma->dma_msg_flag = msg->flags;
	dma->dma_buf = i2c_get_dma_safe_msg_buf(msg, I2C_DMA_THRESHOLD);
	if (!dma->dma_buf)
		return -ENOMEM;

	ret = lpi2c_dma_config(lpi2c_imx);
	if (ret) {
		dev_err(&lpi2c_imx->adapter.dev, "Failed to configure DMA (%d)\n", ret);
		goto disable_dma;
	}

	lpi2c_dma_enable(lpi2c_imx);

	ret = lpi2c_dma_submit(lpi2c_imx);
	if (ret) {
		dev_err(&lpi2c_imx->adapter.dev, "DMA submission failed (%d)\n", ret);
		goto disable_dma;
	}

	if (dma->dma_msg_flag & I2C_M_RD) {
		ret = lpi2c_imx_alloc_rx_cmd_buf(lpi2c_imx);
		if (ret)
			goto disable_cleanup_data_dma;

		ret = lpi2c_dma_rx_cmd_submit(lpi2c_imx);
		if (ret)
			goto disable_cleanup_data_dma;
	}

	ret = lpi2c_imx_dma_msg_complete(lpi2c_imx);
	if (ret)
		goto disable_cleanup_all_dma;

	/* When encountering NACK in transfer, clean up all DMA transfers */
	if ((readl(lpi2c_imx->base + LPI2C_MSR) & MSR_NDF) && !ret) {
		ret = -EIO;
		goto disable_cleanup_all_dma;
	}

	if (dma->dma_msg_flag & I2C_M_RD)
		dma_unmap_single(dma->chan_tx->device->dev, dma->dma_tx_addr,
				 dma->rx_cmd_buf_len, DMA_TO_DEVICE);
	lpi2c_dma_unmap(dma);

	goto disable_dma;

disable_cleanup_all_dma:
	if (dma->dma_msg_flag & I2C_M_RD)
		lpi2c_cleanup_rx_cmd_dma(dma);
disable_cleanup_data_dma:
	lpi2c_cleanup_dma(dma);
disable_dma:
	/* Disable I2C DMA function */
	writel(0, lpi2c_imx->base + LPI2C_MDER);

	if (dma->dma_msg_flag & I2C_M_RD)
		kfree(dma->rx_cmd_buf);

	if (ret)
		i2c_put_dma_safe_msg_buf(dma->dma_buf, msg, false);
	else
		i2c_put_dma_safe_msg_buf(dma->dma_buf, msg, true);

	return ret;
}

static int lpi2c_imx_xfer(struct i2c_adapter *adapter,
			  struct i2c_msg *msgs, int num)
{
	struct lpi2c_imx_struct *lpi2c_imx = i2c_get_adapdata(adapter);
	unsigned int temp;
	int i, result;

	result = lpi2c_imx_master_enable(lpi2c_imx);
	if (result)
		return result;

	for (i = 0; i < num; i++) {
		result = lpi2c_imx_start(lpi2c_imx, &msgs[i]);
		if (result)
			goto disable;

		/* quick smbus */
		if (num == 1 && msgs[0].len == 0)
			goto stop;

		lpi2c_imx->rx_buf = NULL;
		lpi2c_imx->tx_buf = NULL;
		lpi2c_imx->delivered = 0;
		lpi2c_imx->msglen = msgs[i].len;
		init_completion(&lpi2c_imx->complete);

		if (is_use_dma(lpi2c_imx, &msgs[i])) {
			result = lpi2c_imx_dma_xfer(lpi2c_imx, &msgs[i]);
			if (result && lpi2c_imx->dma->using_pio_mode)
				result = lpi2c_imx_pio_xfer(lpi2c_imx, &msgs[i]);
		} else {
			result = lpi2c_imx_pio_xfer(lpi2c_imx, &msgs[i]);
		}

		if (result)
			goto stop;

		if (!(msgs[i].flags & I2C_M_RD)) {
			result = lpi2c_imx_txfifo_empty(lpi2c_imx);
			if (result)
				goto stop;
		}
	}

stop:
	lpi2c_imx_stop(lpi2c_imx);

	temp = readl(lpi2c_imx->base + LPI2C_MSR);
	if ((temp & MSR_NDF) && !result)
		result = -EIO;

disable:
	lpi2c_imx_master_disable(lpi2c_imx);

	dev_dbg(&lpi2c_imx->adapter.dev, "<%s> exit with: %s: %d\n", __func__,
		(result < 0) ? "error" : "success msg",
		(result < 0) ? result : num);

	return (result < 0) ? result : num;
}

static irqreturn_t lpi2c_imx_target_isr(struct lpi2c_imx_struct *lpi2c_imx,
					u32 ssr, u32 sier_filter)
{
	u8 value;
	u32 sasr;

	/* Arbitration lost */
	if (sier_filter & SSR_BEF) {
		writel(0, lpi2c_imx->base + LPI2C_SIER);
		return IRQ_HANDLED;
	}

	/* Address detected */
	if (sier_filter & SSR_AVF) {
		sasr = readl(lpi2c_imx->base + LPI2C_SASR);
		if (SASR_READ_REQ & sasr) {
			/* Read request */
			i2c_slave_event(lpi2c_imx->target, I2C_SLAVE_READ_REQUESTED, &value);
			writel(value, lpi2c_imx->base + LPI2C_STDR);
			goto ret;
		} else {
			/* Write request */
			i2c_slave_event(lpi2c_imx->target, I2C_SLAVE_WRITE_REQUESTED, &value);
		}
	}

	if (sier_filter & SSR_SDF)
		/* STOP */
		i2c_slave_event(lpi2c_imx->target, I2C_SLAVE_STOP, &value);

	if (sier_filter & SSR_TDF) {
		/* Target send data */
		i2c_slave_event(lpi2c_imx->target, I2C_SLAVE_READ_PROCESSED, &value);
		writel(value, lpi2c_imx->base + LPI2C_STDR);
	}

	if (sier_filter & SSR_RDF) {
		/* Target receive data */
		value = readl(lpi2c_imx->base + LPI2C_SRDR);
		i2c_slave_event(lpi2c_imx->target, I2C_SLAVE_WRITE_RECEIVED, &value);
	}

ret:
	/* Clear SSR */
	writel(ssr & SSR_CLEAR_BITS, lpi2c_imx->base + LPI2C_SSR);
	return IRQ_HANDLED;
}

static irqreturn_t lpi2c_imx_master_isr(struct lpi2c_imx_struct *lpi2c_imx)
{
	unsigned int enabled;
	unsigned int temp;

	enabled = readl(lpi2c_imx->base + LPI2C_MIER);

	lpi2c_imx_intctrl(lpi2c_imx, 0);
	temp = readl(lpi2c_imx->base + LPI2C_MSR);
	temp &= enabled;

	if (temp & MSR_NDF)
		complete(&lpi2c_imx->complete);
	else if (temp & MSR_RDF)
		lpi2c_imx_read_rxfifo(lpi2c_imx);
	else if (temp & MSR_TDF)
		lpi2c_imx_write_txfifo(lpi2c_imx);

	return IRQ_HANDLED;
}

static irqreturn_t lpi2c_imx_isr(int irq, void *dev_id)
{
	struct lpi2c_imx_struct *lpi2c_imx = dev_id;

	if (lpi2c_imx->target) {
		u32 scr = readl(lpi2c_imx->base + LPI2C_SCR);
		u32 ssr = readl(lpi2c_imx->base + LPI2C_SSR);
		u32 sier_filter = ssr & readl(lpi2c_imx->base + LPI2C_SIER);

		/*
		 * The target is enabled and an interrupt has been triggered.
		 * Enter the target's irq handler.
		 */
		if ((scr & SCR_SEN) && sier_filter)
			return lpi2c_imx_target_isr(lpi2c_imx, ssr, sier_filter);
	}

	/*
	 * Otherwise the interrupt has been triggered by the master.
	 * Enter the master's irq handler.
	 */
	return lpi2c_imx_master_isr(lpi2c_imx);
}

static void lpi2c_imx_target_init(struct lpi2c_imx_struct *lpi2c_imx)
{
	u32 temp;

	/* reset target module */
	writel(SCR_RST, lpi2c_imx->base + LPI2C_SCR);
	writel(0, lpi2c_imx->base + LPI2C_SCR);

	/* Set target address */
	writel((lpi2c_imx->target->addr << 1), lpi2c_imx->base + LPI2C_SAMR);

	writel(SCFGR1_RXSTALL | SCFGR1_TXDSTALL, lpi2c_imx->base + LPI2C_SCFGR1);

	/*
	 * set SCFGR2: FILTSDA, FILTSCL and CLKHOLD
	 *
	 * FILTSCL/FILTSDA can eliminate signal skew. It should generally be
	 * set to the same value and should be set >= 50ns.
	 *
	 * CLKHOLD is only used when clock stretching is enabled, but it will
	 * extend the clock stretching to ensure there is an additional delay
	 * between the target driving SDA and the target releasing the SCL pin.
	 *
	 * CLKHOLD setting is crucial for lpi2c target. When master read data
	 * from target, if there is a delay caused by cpu idle, excessive load,
	 * or other delays between two bytes in one message transmission, it
	 * will cause a short interval time between the driving SDA signal and
	 * releasing SCL signal. The lpi2c master will mistakenly think it is a stop
	 * signal resulting in an arbitration failure. This issue can be avoided
	 * by setting CLKHOLD.
	 *
	 * In order to ensure lpi2c function normally when the lpi2c speed is as
	 * low as 100kHz, CLKHOLD should be set to 3 and it is also compatible with
	 * higher clock frequency like 400kHz and 1MHz.
	 */
	temp = SCFGR2_FILTSDA(2) | SCFGR2_FILTSCL(2) | SCFGR2_CLKHOLD(3);
	writel(temp, lpi2c_imx->base + LPI2C_SCFGR2);

	/*
	 * Enable module:
	 * SCR_FILTEN can enable digital filter and output delay counter for LPI2C
	 * target mode. So SCR_FILTEN need be asserted when enable SDA/SCL FILTER
	 * and CLKHOLD.
	 */
	writel(SCR_SEN | SCR_FILTEN, lpi2c_imx->base + LPI2C_SCR);

	/* Enable interrupt from i2c module */
	writel(SLAVE_INT_FLAG, lpi2c_imx->base + LPI2C_SIER);
}

static int lpi2c_imx_register_target(struct i2c_client *client)
{
	struct lpi2c_imx_struct *lpi2c_imx = i2c_get_adapdata(client->adapter);
	int ret;

	if (lpi2c_imx->target)
		return -EBUSY;

	lpi2c_imx->target = client;

	ret = pm_runtime_resume_and_get(lpi2c_imx->adapter.dev.parent);
	if (ret < 0) {
		dev_err(&lpi2c_imx->adapter.dev, "failed to resume i2c controller");
		return ret;
	}

	lpi2c_imx_target_init(lpi2c_imx);

	return 0;
}

static int lpi2c_imx_unregister_target(struct i2c_client *client)
{
	struct lpi2c_imx_struct *lpi2c_imx = i2c_get_adapdata(client->adapter);
	int ret;

	if (!lpi2c_imx->target)
		return -EINVAL;

	/* Reset target address. */
	writel(0, lpi2c_imx->base + LPI2C_SAMR);

	writel(SCR_RST, lpi2c_imx->base + LPI2C_SCR);
	writel(0, lpi2c_imx->base + LPI2C_SCR);

	lpi2c_imx->target = NULL;

	ret = pm_runtime_put_sync(lpi2c_imx->adapter.dev.parent);
	if (ret < 0)
		dev_err(&lpi2c_imx->adapter.dev, "failed to suspend i2c controller");

	return ret;
}

static int lpi2c_imx_init_recovery_info(struct lpi2c_imx_struct *lpi2c_imx,
				  struct platform_device *pdev)
{
	struct i2c_bus_recovery_info *bri = &lpi2c_imx->rinfo;

	bri->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(bri->pinctrl))
		return PTR_ERR(bri->pinctrl);

	lpi2c_imx->adapter.bus_recovery_info = bri;

	return 0;
}

static void dma_exit(struct device *dev, struct lpi2c_imx_dma *dma)
{
	if (dma->chan_rx)
		dma_release_channel(dma->chan_rx);

	if (dma->chan_tx)
		dma_release_channel(dma->chan_tx);

	devm_kfree(dev, dma);
}

static int lpi2c_dma_init(struct device *dev, dma_addr_t phy_addr)
{
	struct lpi2c_imx_struct *lpi2c_imx = dev_get_drvdata(dev);
	struct lpi2c_imx_dma *dma;
	int ret;

	dma = devm_kzalloc(dev, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	dma->phy_addr = phy_addr;

	/* Prepare for TX DMA: */
	dma->chan_tx = dma_request_chan(dev, "tx");
	if (IS_ERR(dma->chan_tx)) {
		ret = PTR_ERR(dma->chan_tx);
		if (ret != -ENODEV && ret != -EPROBE_DEFER)
			dev_err(dev, "can't request DMA tx channel (%d)\n", ret);
		dma->chan_tx = NULL;
		goto dma_exit;
	}

	/* Prepare for RX DMA: */
	dma->chan_rx = dma_request_chan(dev, "rx");
	if (IS_ERR(dma->chan_rx)) {
		ret = PTR_ERR(dma->chan_rx);
		if (ret != -ENODEV && ret != -EPROBE_DEFER)
			dev_err(dev, "can't request DMA rx channel (%d)\n", ret);
		dma->chan_rx = NULL;
		goto dma_exit;
	}

	lpi2c_imx->can_use_dma = true;
	lpi2c_imx->dma = dma;
	return 0;

dma_exit:
	dma_exit(dev, dma);
	return ret;
}

static u32 lpi2c_imx_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
		I2C_FUNC_SMBUS_READ_BLOCK_DATA;
}

static const struct i2c_algorithm lpi2c_imx_algo = {
	.master_xfer	= lpi2c_imx_xfer,
	.functionality	= lpi2c_imx_func,
	.reg_target	= lpi2c_imx_register_target,
	.unreg_target	= lpi2c_imx_unregister_target,
};

static const struct of_device_id lpi2c_imx_of_match[] = {
	{ .compatible = "fsl,imx7ulp-lpi2c" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpi2c_imx_of_match);

static int lpi2c_imx_probe(struct platform_device *pdev)
{
	struct lpi2c_imx_struct *lpi2c_imx;
	struct resource *res;
	dma_addr_t phy_addr;
	unsigned int temp;
	int irq, ret;

	lpi2c_imx = devm_kzalloc(&pdev->dev, sizeof(*lpi2c_imx), GFP_KERNEL);
	if (!lpi2c_imx)
		return -ENOMEM;

	lpi2c_imx->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(lpi2c_imx->base))
		return PTR_ERR(lpi2c_imx->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	lpi2c_imx->adapter.owner	= THIS_MODULE;
	lpi2c_imx->adapter.algo		= &lpi2c_imx_algo;
	lpi2c_imx->adapter.dev.parent	= &pdev->dev;
	lpi2c_imx->adapter.dev.of_node	= pdev->dev.of_node;
	strscpy(lpi2c_imx->adapter.name, pdev->name,
		sizeof(lpi2c_imx->adapter.name));
	phy_addr = (dma_addr_t)res->start;

	ret = devm_clk_bulk_get_all(&pdev->dev, &lpi2c_imx->clks);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "can't get I2C peripheral clock\n");
	lpi2c_imx->num_clks = ret;

	ret = of_property_read_u32(pdev->dev.of_node,
				   "clock-frequency", &lpi2c_imx->bitrate);
	if (ret)
		lpi2c_imx->bitrate = I2C_MAX_STANDARD_MODE_FREQ;

	ret = devm_request_irq(&pdev->dev, irq, lpi2c_imx_isr, IRQF_NO_SUSPEND,
			       pdev->name, lpi2c_imx);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "can't claim irq %d\n", irq);

	i2c_set_adapdata(&lpi2c_imx->adapter, lpi2c_imx);
	platform_set_drvdata(pdev, lpi2c_imx);

	ret = clk_bulk_prepare_enable(lpi2c_imx->num_clks, lpi2c_imx->clks);
	if (ret)
		return ret;

	/*
	 * Lock the parent clock rate to avoid getting parent clock upon
	 * each transfer
	 */
	ret = devm_clk_rate_exclusive_get(&pdev->dev, lpi2c_imx->clks[0].clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "can't lock I2C peripheral clock rate\n");

	lpi2c_imx->rate_per = clk_get_rate(lpi2c_imx->clks[0].clk);
	if (!lpi2c_imx->rate_per)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "can't get I2C peripheral clock rate\n");

	pm_runtime_set_autosuspend_delay(&pdev->dev, I2C_PM_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	temp = readl(lpi2c_imx->base + LPI2C_PARAM);
	lpi2c_imx->txfifosize = 1 << (temp & 0x0f);
	lpi2c_imx->rxfifosize = 1 << ((temp >> 8) & 0x0f);

	/* Init optional bus recovery function */
	ret = lpi2c_imx_init_recovery_info(lpi2c_imx, pdev);
	/* Give it another chance if pinctrl used is not ready yet */
	if (ret == -EPROBE_DEFER)
		goto rpm_disable;

	/* Init DMA */
	ret = lpi2c_dma_init(&pdev->dev, phy_addr);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			goto rpm_disable;
		dev_info(&pdev->dev, "use pio mode\n");
	}

	ret = i2c_add_adapter(&lpi2c_imx->adapter);
	if (ret)
		goto rpm_disable;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	dev_info(&lpi2c_imx->adapter.dev, "LPI2C adapter registered\n");

	return 0;

rpm_disable:
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void lpi2c_imx_remove(struct platform_device *pdev)
{
	struct lpi2c_imx_struct *lpi2c_imx = platform_get_drvdata(pdev);

	i2c_del_adapter(&lpi2c_imx->adapter);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
}

static int __maybe_unused lpi2c_runtime_suspend(struct device *dev)
{
	struct lpi2c_imx_struct *lpi2c_imx = dev_get_drvdata(dev);

	clk_bulk_disable(lpi2c_imx->num_clks, lpi2c_imx->clks);
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused lpi2c_runtime_resume(struct device *dev)
{
	struct lpi2c_imx_struct *lpi2c_imx = dev_get_drvdata(dev);
	int ret;

	pinctrl_pm_select_default_state(dev);
	ret = clk_bulk_enable(lpi2c_imx->num_clks, lpi2c_imx->clks);
	if (ret) {
		dev_err(dev, "failed to enable I2C clock, ret=%d\n", ret);
		return ret;
	}

	return 0;
}

static int __maybe_unused lpi2c_suspend_noirq(struct device *dev)
{
	return pm_runtime_force_suspend(dev);
}

static int __maybe_unused lpi2c_resume_noirq(struct device *dev)
{
	struct lpi2c_imx_struct *lpi2c_imx = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	/*
	 * If the I2C module powers down during system suspend,
	 * the register values will be lost. Therefore, reinitialize
	 * the target when the system resumes.
	 */
	if (lpi2c_imx->target)
		lpi2c_imx_target_init(lpi2c_imx);

	return 0;
}

static int lpi2c_suspend(struct device *dev)
{
	/*
	 * Some I2C devices may need the I2C controller to remain active
	 * during resume_noirq() or suspend_noirq(). If the controller is
	 * autosuspended, there is no way to wake it up once runtime PM is
	 * disabled (in suspend_late()).
	 *
	 * During system resume, the I2C controller will be available only
	 * after runtime PM is re-enabled (in resume_early()). However, this
	 * may be too late for some devices.
	 *
	 * Wake up the controller in the suspend() callback while runtime PM
	 * is still enabled. The I2C controller will remain available until
	 * the suspend_noirq() callback (pm_runtime_force_suspend()) is
	 * called. During resume, the I2C controller can be restored by the
	 * resume_noirq() callback (pm_runtime_force_resume()).
	 *
	 * Finally, the resume() callback re-enables autosuspend, ensuring
	 * the I2C controller remains available until the system enters
	 * suspend_noirq() and from resume_noirq().
	 */
	return pm_runtime_resume_and_get(dev);
}

static int lpi2c_resume(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static const struct dev_pm_ops lpi2c_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(lpi2c_suspend_noirq,
				      lpi2c_resume_noirq)
	SYSTEM_SLEEP_PM_OPS(lpi2c_suspend, lpi2c_resume)
	SET_RUNTIME_PM_OPS(lpi2c_runtime_suspend,
			   lpi2c_runtime_resume, NULL)
};

static struct platform_driver lpi2c_imx_driver = {
	.probe = lpi2c_imx_probe,
	.remove = lpi2c_imx_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = lpi2c_imx_of_match,
		.pm = &lpi2c_pm_ops,
	},
};

module_platform_driver(lpi2c_imx_driver);

MODULE_AUTHOR("Gao Pan <pandy.gao@nxp.com>");
MODULE_DESCRIPTION("I2C adapter driver for LPI2C bus");
MODULE_LICENSE("GPL");
