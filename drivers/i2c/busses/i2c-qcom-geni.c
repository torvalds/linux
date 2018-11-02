// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/qcom-geni-se.h>
#include <linux/spinlock.h>

#define SE_I2C_TX_TRANS_LEN		0x26c
#define SE_I2C_RX_TRANS_LEN		0x270
#define SE_I2C_SCL_COUNTERS		0x278

#define SE_I2C_ERR  (M_CMD_OVERRUN_EN | M_ILLEGAL_CMD_EN | M_CMD_FAILURE_EN |\
			M_GP_IRQ_1_EN | M_GP_IRQ_3_EN | M_GP_IRQ_4_EN)
#define SE_I2C_ABORT		BIT(1)

/* M_CMD OP codes for I2C */
#define I2C_WRITE		0x1
#define I2C_READ		0x2
#define I2C_WRITE_READ		0x3
#define I2C_ADDR_ONLY		0x4
#define I2C_BUS_CLEAR		0x6
#define I2C_STOP_ON_BUS		0x7
/* M_CMD params for I2C */
#define PRE_CMD_DELAY		BIT(0)
#define TIMESTAMP_BEFORE	BIT(1)
#define STOP_STRETCH		BIT(2)
#define TIMESTAMP_AFTER		BIT(3)
#define POST_COMMAND_DELAY	BIT(4)
#define IGNORE_ADD_NACK		BIT(6)
#define READ_FINISHED_WITH_ACK	BIT(7)
#define BYPASS_ADDR_PHASE	BIT(8)
#define SLV_ADDR_MSK		GENMASK(15, 9)
#define SLV_ADDR_SHFT		9
/* I2C SCL COUNTER fields */
#define HIGH_COUNTER_MSK	GENMASK(29, 20)
#define HIGH_COUNTER_SHFT	20
#define LOW_COUNTER_MSK		GENMASK(19, 10)
#define LOW_COUNTER_SHFT	10
#define CYCLE_COUNTER_MSK	GENMASK(9, 0)

enum geni_i2c_err_code {
	GP_IRQ0,
	NACK,
	GP_IRQ2,
	BUS_PROTO,
	ARB_LOST,
	GP_IRQ5,
	GENI_OVERRUN,
	GENI_ILLEGAL_CMD,
	GENI_ABORT_DONE,
	GENI_TIMEOUT,
};

#define DM_I2C_CB_ERR		((BIT(NACK) | BIT(BUS_PROTO) | BIT(ARB_LOST)) \
									<< 5)

#define I2C_AUTO_SUSPEND_DELAY	250
#define KHZ(freq)		(1000 * freq)
#define PACKING_BYTES_PW	4

#define ABORT_TIMEOUT		HZ
#define XFER_TIMEOUT		HZ
#define RST_TIMEOUT		HZ

struct geni_i2c_dev {
	struct geni_se se;
	u32 tx_wm;
	int irq;
	int err;
	struct i2c_adapter adap;
	struct completion done;
	struct i2c_msg *cur;
	int cur_wr;
	int cur_rd;
	spinlock_t lock;
	u32 clk_freq_out;
	const struct geni_i2c_clk_fld *clk_fld;
	int suspended;
};

struct geni_i2c_err_log {
	int err;
	const char *msg;
};

static const struct geni_i2c_err_log gi2c_log[] = {
	[GP_IRQ0] = {-EIO, "Unknown I2C err GP_IRQ0"},
	[NACK] = {-ENXIO, "NACK: slv unresponsive, check its power/reset-ln"},
	[GP_IRQ2] = {-EIO, "Unknown I2C err GP IRQ2"},
	[BUS_PROTO] = {-EPROTO, "Bus proto err, noisy/unepxected start/stop"},
	[ARB_LOST] = {-EAGAIN, "Bus arbitration lost, clock line undriveable"},
	[GP_IRQ5] = {-EIO, "Unknown I2C err GP IRQ5"},
	[GENI_OVERRUN] = {-EIO, "Cmd overrun, check GENI cmd-state machine"},
	[GENI_ILLEGAL_CMD] = {-EIO, "Illegal cmd, check GENI cmd-state machine"},
	[GENI_ABORT_DONE] = {-ETIMEDOUT, "Abort after timeout successful"},
	[GENI_TIMEOUT] = {-ETIMEDOUT, "I2C TXN timed out"},
};

struct geni_i2c_clk_fld {
	u32	clk_freq_out;
	u8	clk_div;
	u8	t_high_cnt;
	u8	t_low_cnt;
	u8	t_cycle_cnt;
};

/*
 * Hardware uses the underlying formula to calculate time periods of
 * SCL clock cycle. Firmware uses some additional cycles excluded from the
 * below formula and it is confirmed that the time periods are within
 * specification limits.
 *
 * time of high period of SCL: t_high = (t_high_cnt * clk_div) / source_clock
 * time of low period of SCL: t_low = (t_low_cnt * clk_div) / source_clock
 * time of full period of SCL: t_cycle = (t_cycle_cnt * clk_div) / source_clock
 * clk_freq_out = t / t_cycle
 * source_clock = 19.2 MHz
 */
static const struct geni_i2c_clk_fld geni_i2c_clk_map[] = {
	{KHZ(100), 7, 10, 11, 26},
	{KHZ(400), 2,  5, 12, 24},
	{KHZ(1000), 1, 3,  9, 18},
};

static int geni_i2c_clk_map_idx(struct geni_i2c_dev *gi2c)
{
	int i;
	const struct geni_i2c_clk_fld *itr = geni_i2c_clk_map;

	for (i = 0; i < ARRAY_SIZE(geni_i2c_clk_map); i++, itr++) {
		if (itr->clk_freq_out == gi2c->clk_freq_out) {
			gi2c->clk_fld = itr;
			return 0;
		}
	}
	return -EINVAL;
}

static void qcom_geni_i2c_conf(struct geni_i2c_dev *gi2c)
{
	const struct geni_i2c_clk_fld *itr = gi2c->clk_fld;
	u32 val;

	writel_relaxed(0, gi2c->se.base + SE_GENI_CLK_SEL);

	val = (itr->clk_div << CLK_DIV_SHFT) | SER_CLK_EN;
	writel_relaxed(val, gi2c->se.base + GENI_SER_M_CLK_CFG);

	val = itr->t_high_cnt << HIGH_COUNTER_SHFT;
	val |= itr->t_low_cnt << LOW_COUNTER_SHFT;
	val |= itr->t_cycle_cnt;
	writel_relaxed(val, gi2c->se.base + SE_I2C_SCL_COUNTERS);
}

static void geni_i2c_err_misc(struct geni_i2c_dev *gi2c)
{
	u32 m_cmd = readl_relaxed(gi2c->se.base + SE_GENI_M_CMD0);
	u32 m_stat = readl_relaxed(gi2c->se.base + SE_GENI_M_IRQ_STATUS);
	u32 geni_s = readl_relaxed(gi2c->se.base + SE_GENI_STATUS);
	u32 geni_ios = readl_relaxed(gi2c->se.base + SE_GENI_IOS);
	u32 dma = readl_relaxed(gi2c->se.base + SE_GENI_DMA_MODE_EN);
	u32 rx_st, tx_st;

	if (dma) {
		rx_st = readl_relaxed(gi2c->se.base + SE_DMA_RX_IRQ_STAT);
		tx_st = readl_relaxed(gi2c->se.base + SE_DMA_TX_IRQ_STAT);
	} else {
		rx_st = readl_relaxed(gi2c->se.base + SE_GENI_RX_FIFO_STATUS);
		tx_st = readl_relaxed(gi2c->se.base + SE_GENI_TX_FIFO_STATUS);
	}
	dev_dbg(gi2c->se.dev, "DMA:%d tx_stat:0x%x, rx_stat:0x%x, irq-stat:0x%x\n",
		dma, tx_st, rx_st, m_stat);
	dev_dbg(gi2c->se.dev, "m_cmd:0x%x, geni_status:0x%x, geni_ios:0x%x\n",
		m_cmd, geni_s, geni_ios);
}

static void geni_i2c_err(struct geni_i2c_dev *gi2c, int err)
{
	if (!gi2c->err)
		gi2c->err = gi2c_log[err].err;
	if (gi2c->cur)
		dev_dbg(gi2c->se.dev, "len:%d, slv-addr:0x%x, RD/WR:%d\n",
			gi2c->cur->len, gi2c->cur->addr, gi2c->cur->flags);

	if (err != NACK && err != GENI_ABORT_DONE) {
		dev_err(gi2c->se.dev, "%s\n", gi2c_log[err].msg);
		geni_i2c_err_misc(gi2c);
	}
}

static irqreturn_t geni_i2c_irq(int irq, void *dev)
{
	struct geni_i2c_dev *gi2c = dev;
	int j;
	u32 m_stat;
	u32 rx_st;
	u32 dm_tx_st;
	u32 dm_rx_st;
	u32 dma;
	struct i2c_msg *cur;
	unsigned long flags;

	spin_lock_irqsave(&gi2c->lock, flags);
	m_stat = readl_relaxed(gi2c->se.base + SE_GENI_M_IRQ_STATUS);
	rx_st = readl_relaxed(gi2c->se.base + SE_GENI_RX_FIFO_STATUS);
	dm_tx_st = readl_relaxed(gi2c->se.base + SE_DMA_TX_IRQ_STAT);
	dm_rx_st = readl_relaxed(gi2c->se.base + SE_DMA_RX_IRQ_STAT);
	dma = readl_relaxed(gi2c->se.base + SE_GENI_DMA_MODE_EN);
	cur = gi2c->cur;

	if (!cur ||
	    m_stat & (M_CMD_FAILURE_EN | M_CMD_ABORT_EN) ||
	    dm_rx_st & (DM_I2C_CB_ERR)) {
		if (m_stat & M_GP_IRQ_1_EN)
			geni_i2c_err(gi2c, NACK);
		if (m_stat & M_GP_IRQ_3_EN)
			geni_i2c_err(gi2c, BUS_PROTO);
		if (m_stat & M_GP_IRQ_4_EN)
			geni_i2c_err(gi2c, ARB_LOST);
		if (m_stat & M_CMD_OVERRUN_EN)
			geni_i2c_err(gi2c, GENI_OVERRUN);
		if (m_stat & M_ILLEGAL_CMD_EN)
			geni_i2c_err(gi2c, GENI_ILLEGAL_CMD);
		if (m_stat & M_CMD_ABORT_EN)
			geni_i2c_err(gi2c, GENI_ABORT_DONE);
		if (m_stat & M_GP_IRQ_0_EN)
			geni_i2c_err(gi2c, GP_IRQ0);

		/* Disable the TX Watermark interrupt to stop TX */
		if (!dma)
			writel_relaxed(0, gi2c->se.base +
					   SE_GENI_TX_WATERMARK_REG);
		goto irqret;
	}

	if (dma) {
		dev_dbg(gi2c->se.dev, "i2c dma tx:0x%x, dma rx:0x%x\n",
			dm_tx_st, dm_rx_st);
		goto irqret;
	}

	if (cur->flags & I2C_M_RD &&
	    m_stat & (M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN)) {
		u32 rxcnt = rx_st & RX_FIFO_WC_MSK;

		for (j = 0; j < rxcnt; j++) {
			u32 val;
			int p = 0;

			val = readl_relaxed(gi2c->se.base + SE_GENI_RX_FIFOn);
			while (gi2c->cur_rd < cur->len && p < sizeof(val)) {
				cur->buf[gi2c->cur_rd++] = val & 0xff;
				val >>= 8;
				p++;
			}
			if (gi2c->cur_rd == cur->len)
				break;
		}
	} else if (!(cur->flags & I2C_M_RD) &&
		   m_stat & M_TX_FIFO_WATERMARK_EN) {
		for (j = 0; j < gi2c->tx_wm; j++) {
			u32 temp;
			u32 val = 0;
			int p = 0;

			while (gi2c->cur_wr < cur->len && p < sizeof(val)) {
				temp = cur->buf[gi2c->cur_wr++];
				val |= temp << (p * 8);
				p++;
			}
			writel_relaxed(val, gi2c->se.base + SE_GENI_TX_FIFOn);
			/* TX Complete, Disable the TX Watermark interrupt */
			if (gi2c->cur_wr == cur->len) {
				writel_relaxed(0, gi2c->se.base +
						SE_GENI_TX_WATERMARK_REG);
				break;
			}
		}
	}
irqret:
	if (m_stat)
		writel_relaxed(m_stat, gi2c->se.base + SE_GENI_M_IRQ_CLEAR);

	if (dma) {
		if (dm_tx_st)
			writel_relaxed(dm_tx_st, gi2c->se.base +
						SE_DMA_TX_IRQ_CLR);
		if (dm_rx_st)
			writel_relaxed(dm_rx_st, gi2c->se.base +
						SE_DMA_RX_IRQ_CLR);
	}
	/* if this is err with done-bit not set, handle that through timeout. */
	if (m_stat & M_CMD_DONE_EN || m_stat & M_CMD_ABORT_EN)
		complete(&gi2c->done);
	else if (dm_tx_st & TX_DMA_DONE || dm_tx_st & TX_RESET_DONE)
		complete(&gi2c->done);
	else if (dm_rx_st & RX_DMA_DONE || dm_rx_st & RX_RESET_DONE)
		complete(&gi2c->done);

	spin_unlock_irqrestore(&gi2c->lock, flags);
	return IRQ_HANDLED;
}

static void geni_i2c_abort_xfer(struct geni_i2c_dev *gi2c)
{
	u32 val;
	unsigned long time_left = ABORT_TIMEOUT;
	unsigned long flags;

	spin_lock_irqsave(&gi2c->lock, flags);
	geni_i2c_err(gi2c, GENI_TIMEOUT);
	gi2c->cur = NULL;
	geni_se_abort_m_cmd(&gi2c->se);
	spin_unlock_irqrestore(&gi2c->lock, flags);
	do {
		time_left = wait_for_completion_timeout(&gi2c->done, time_left);
		val = readl_relaxed(gi2c->se.base + SE_GENI_M_IRQ_STATUS);
	} while (!(val & M_CMD_ABORT_EN) && time_left);

	if (!(val & M_CMD_ABORT_EN))
		dev_err(gi2c->se.dev, "Timeout abort_m_cmd\n");
}

static void geni_i2c_rx_fsm_rst(struct geni_i2c_dev *gi2c)
{
	u32 val;
	unsigned long time_left = RST_TIMEOUT;

	writel_relaxed(1, gi2c->se.base + SE_DMA_RX_FSM_RST);
	do {
		time_left = wait_for_completion_timeout(&gi2c->done, time_left);
		val = readl_relaxed(gi2c->se.base + SE_DMA_RX_IRQ_STAT);
	} while (!(val & RX_RESET_DONE) && time_left);

	if (!(val & RX_RESET_DONE))
		dev_err(gi2c->se.dev, "Timeout resetting RX_FSM\n");
}

static void geni_i2c_tx_fsm_rst(struct geni_i2c_dev *gi2c)
{
	u32 val;
	unsigned long time_left = RST_TIMEOUT;

	writel_relaxed(1, gi2c->se.base + SE_DMA_TX_FSM_RST);
	do {
		time_left = wait_for_completion_timeout(&gi2c->done, time_left);
		val = readl_relaxed(gi2c->se.base + SE_DMA_TX_IRQ_STAT);
	} while (!(val & TX_RESET_DONE) && time_left);

	if (!(val & TX_RESET_DONE))
		dev_err(gi2c->se.dev, "Timeout resetting TX_FSM\n");
}

static int geni_i2c_rx_one_msg(struct geni_i2c_dev *gi2c, struct i2c_msg *msg,
				u32 m_param)
{
	dma_addr_t rx_dma;
	enum geni_se_xfer_mode mode;
	unsigned long time_left = XFER_TIMEOUT;
	void *dma_buf;

	gi2c->cur = msg;
	mode = GENI_SE_FIFO;
	dma_buf = i2c_get_dma_safe_msg_buf(msg, 32);
	if (dma_buf)
		mode = GENI_SE_DMA;

	geni_se_select_mode(&gi2c->se, mode);
	writel_relaxed(msg->len, gi2c->se.base + SE_I2C_RX_TRANS_LEN);
	geni_se_setup_m_cmd(&gi2c->se, I2C_READ, m_param);
	if (mode == GENI_SE_DMA) {
		int ret;

		ret = geni_se_rx_dma_prep(&gi2c->se, dma_buf, msg->len,
								&rx_dma);
		if (ret) {
			mode = GENI_SE_FIFO;
			geni_se_select_mode(&gi2c->se, mode);
			i2c_put_dma_safe_msg_buf(dma_buf, msg, false);
		}
	}

	time_left = wait_for_completion_timeout(&gi2c->done, XFER_TIMEOUT);
	if (!time_left)
		geni_i2c_abort_xfer(gi2c);

	gi2c->cur_rd = 0;
	if (mode == GENI_SE_DMA) {
		if (gi2c->err)
			geni_i2c_rx_fsm_rst(gi2c);
		geni_se_rx_dma_unprep(&gi2c->se, rx_dma, msg->len);
		i2c_put_dma_safe_msg_buf(dma_buf, msg, !gi2c->err);
	}
	return gi2c->err;
}

static int geni_i2c_tx_one_msg(struct geni_i2c_dev *gi2c, struct i2c_msg *msg,
				u32 m_param)
{
	dma_addr_t tx_dma;
	enum geni_se_xfer_mode mode;
	unsigned long time_left;
	void *dma_buf;

	gi2c->cur = msg;
	mode = GENI_SE_FIFO;
	dma_buf = i2c_get_dma_safe_msg_buf(msg, 32);
	if (dma_buf)
		mode = GENI_SE_DMA;

	geni_se_select_mode(&gi2c->se, mode);
	writel_relaxed(msg->len, gi2c->se.base + SE_I2C_TX_TRANS_LEN);
	geni_se_setup_m_cmd(&gi2c->se, I2C_WRITE, m_param);
	if (mode == GENI_SE_DMA) {
		int ret;

		ret = geni_se_tx_dma_prep(&gi2c->se, dma_buf, msg->len,
								&tx_dma);
		if (ret) {
			mode = GENI_SE_FIFO;
			geni_se_select_mode(&gi2c->se, mode);
			i2c_put_dma_safe_msg_buf(dma_buf, msg, false);
		}
	}

	if (mode == GENI_SE_FIFO) /* Get FIFO IRQ */
		writel_relaxed(1, gi2c->se.base + SE_GENI_TX_WATERMARK_REG);

	time_left = wait_for_completion_timeout(&gi2c->done, XFER_TIMEOUT);
	if (!time_left)
		geni_i2c_abort_xfer(gi2c);

	gi2c->cur_wr = 0;
	if (mode == GENI_SE_DMA) {
		if (gi2c->err)
			geni_i2c_tx_fsm_rst(gi2c);
		geni_se_tx_dma_unprep(&gi2c->se, tx_dma, msg->len);
		i2c_put_dma_safe_msg_buf(dma_buf, msg, !gi2c->err);
	}
	return gi2c->err;
}

static int geni_i2c_xfer(struct i2c_adapter *adap,
			 struct i2c_msg msgs[],
			 int num)
{
	struct geni_i2c_dev *gi2c = i2c_get_adapdata(adap);
	int i, ret;

	gi2c->err = 0;
	reinit_completion(&gi2c->done);
	ret = pm_runtime_get_sync(gi2c->se.dev);
	if (ret < 0) {
		dev_err(gi2c->se.dev, "error turning SE resources:%d\n", ret);
		pm_runtime_put_noidle(gi2c->se.dev);
		/* Set device in suspended since resume failed */
		pm_runtime_set_suspended(gi2c->se.dev);
		return ret;
	}

	qcom_geni_i2c_conf(gi2c);
	for (i = 0; i < num; i++) {
		u32 m_param = i < (num - 1) ? STOP_STRETCH : 0;

		m_param |= ((msgs[i].addr << SLV_ADDR_SHFT) & SLV_ADDR_MSK);

		if (msgs[i].flags & I2C_M_RD)
			ret = geni_i2c_rx_one_msg(gi2c, &msgs[i], m_param);
		else
			ret = geni_i2c_tx_one_msg(gi2c, &msgs[i], m_param);

		if (ret)
			break;
	}
	if (ret == 0)
		ret = num;

	pm_runtime_mark_last_busy(gi2c->se.dev);
	pm_runtime_put_autosuspend(gi2c->se.dev);
	gi2c->cur = NULL;
	gi2c->err = 0;
	return ret;
}

static u32 geni_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static const struct i2c_algorithm geni_i2c_algo = {
	.master_xfer	= geni_i2c_xfer,
	.functionality	= geni_i2c_func,
};

static int geni_i2c_probe(struct platform_device *pdev)
{
	struct geni_i2c_dev *gi2c;
	struct resource *res;
	u32 proto, tx_depth;
	int ret;

	gi2c = devm_kzalloc(&pdev->dev, sizeof(*gi2c), GFP_KERNEL);
	if (!gi2c)
		return -ENOMEM;

	gi2c->se.dev = &pdev->dev;
	gi2c->se.wrapper = dev_get_drvdata(pdev->dev.parent);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gi2c->se.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gi2c->se.base))
		return PTR_ERR(gi2c->se.base);

	gi2c->se.clk = devm_clk_get(&pdev->dev, "se");
	if (IS_ERR(gi2c->se.clk)) {
		ret = PTR_ERR(gi2c->se.clk);
		dev_err(&pdev->dev, "Err getting SE Core clk %d\n", ret);
		return ret;
	}

	ret = device_property_read_u32(&pdev->dev, "clock-frequency",
							&gi2c->clk_freq_out);
	if (ret) {
		dev_info(&pdev->dev,
			"Bus frequency not specified, default to 100kHz.\n");
		gi2c->clk_freq_out = KHZ(100);
	}

	gi2c->irq = platform_get_irq(pdev, 0);
	if (gi2c->irq < 0) {
		dev_err(&pdev->dev, "IRQ error for i2c-geni\n");
		return gi2c->irq;
	}

	ret = geni_i2c_clk_map_idx(gi2c);
	if (ret) {
		dev_err(&pdev->dev, "Invalid clk frequency %d Hz: %d\n",
			gi2c->clk_freq_out, ret);
		return ret;
	}

	gi2c->adap.algo = &geni_i2c_algo;
	init_completion(&gi2c->done);
	spin_lock_init(&gi2c->lock);
	platform_set_drvdata(pdev, gi2c);
	ret = devm_request_irq(&pdev->dev, gi2c->irq, geni_i2c_irq,
			       IRQF_TRIGGER_HIGH, "i2c_geni", gi2c);
	if (ret) {
		dev_err(&pdev->dev, "Request_irq failed:%d: err:%d\n",
			gi2c->irq, ret);
		return ret;
	}
	/* Disable the interrupt so that the system can enter low-power mode */
	disable_irq(gi2c->irq);
	i2c_set_adapdata(&gi2c->adap, gi2c);
	gi2c->adap.dev.parent = &pdev->dev;
	gi2c->adap.dev.of_node = pdev->dev.of_node;
	strlcpy(gi2c->adap.name, "Geni-I2C", sizeof(gi2c->adap.name));

	ret = geni_se_resources_on(&gi2c->se);
	if (ret) {
		dev_err(&pdev->dev, "Error turning on resources %d\n", ret);
		return ret;
	}
	proto = geni_se_read_proto(&gi2c->se);
	tx_depth = geni_se_get_tx_fifo_depth(&gi2c->se);
	if (proto != GENI_SE_I2C) {
		dev_err(&pdev->dev, "Invalid proto %d\n", proto);
		geni_se_resources_off(&gi2c->se);
		return -ENXIO;
	}
	gi2c->tx_wm = tx_depth - 1;
	geni_se_init(&gi2c->se, gi2c->tx_wm, tx_depth);
	geni_se_config_packing(&gi2c->se, BITS_PER_BYTE, PACKING_BYTES_PW,
							true, true, true);
	ret = geni_se_resources_off(&gi2c->se);
	if (ret) {
		dev_err(&pdev->dev, "Error turning off resources %d\n", ret);
		return ret;
	}

	dev_dbg(&pdev->dev, "i2c fifo/se-dma mode. fifo depth:%d\n", tx_depth);

	gi2c->suspended = 1;
	pm_runtime_set_suspended(gi2c->se.dev);
	pm_runtime_set_autosuspend_delay(gi2c->se.dev, I2C_AUTO_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(gi2c->se.dev);
	pm_runtime_enable(gi2c->se.dev);

	ret = i2c_add_adapter(&gi2c->adap);
	if (ret) {
		dev_err(&pdev->dev, "Error adding i2c adapter %d\n", ret);
		pm_runtime_disable(gi2c->se.dev);
		return ret;
	}

	return 0;
}

static int geni_i2c_remove(struct platform_device *pdev)
{
	struct geni_i2c_dev *gi2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&gi2c->adap);
	pm_runtime_disable(gi2c->se.dev);
	return 0;
}

static int __maybe_unused geni_i2c_runtime_suspend(struct device *dev)
{
	int ret;
	struct geni_i2c_dev *gi2c = dev_get_drvdata(dev);

	disable_irq(gi2c->irq);
	ret = geni_se_resources_off(&gi2c->se);
	if (ret) {
		enable_irq(gi2c->irq);
		return ret;

	} else {
		gi2c->suspended = 1;
	}

	return 0;
}

static int __maybe_unused geni_i2c_runtime_resume(struct device *dev)
{
	int ret;
	struct geni_i2c_dev *gi2c = dev_get_drvdata(dev);

	ret = geni_se_resources_on(&gi2c->se);
	if (ret)
		return ret;

	enable_irq(gi2c->irq);
	gi2c->suspended = 0;
	return 0;
}

static int __maybe_unused geni_i2c_suspend_noirq(struct device *dev)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(dev);

	if (!gi2c->suspended) {
		geni_i2c_runtime_suspend(dev);
		pm_runtime_disable(dev);
		pm_runtime_set_suspended(dev);
		pm_runtime_enable(dev);
	}
	return 0;
}

static const struct dev_pm_ops geni_i2c_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(geni_i2c_suspend_noirq, NULL)
	SET_RUNTIME_PM_OPS(geni_i2c_runtime_suspend, geni_i2c_runtime_resume,
									NULL)
};

static const struct of_device_id geni_i2c_dt_match[] = {
	{ .compatible = "qcom,geni-i2c" },
	{}
};
MODULE_DEVICE_TABLE(of, geni_i2c_dt_match);

static struct platform_driver geni_i2c_driver = {
	.probe  = geni_i2c_probe,
	.remove = geni_i2c_remove,
	.driver = {
		.name = "geni_i2c",
		.pm = &geni_i2c_pm_ops,
		.of_match_table = geni_i2c_dt_match,
	},
};

module_platform_driver(geni_i2c_driver);

MODULE_DESCRIPTION("I2C Controller Driver for GENI based QUP cores");
MODULE_LICENSE("GPL v2");
