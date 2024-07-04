// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define UNIPHIER_FI2C_CR	0x00	/* control register */
#define     UNIPHIER_FI2C_CR_MST	BIT(3)	/* master mode */
#define     UNIPHIER_FI2C_CR_STA	BIT(2)	/* start condition */
#define     UNIPHIER_FI2C_CR_STO	BIT(1)	/* stop condition */
#define     UNIPHIER_FI2C_CR_NACK	BIT(0)	/* do not return ACK */
#define UNIPHIER_FI2C_DTTX	0x04	/* TX FIFO */
#define     UNIPHIER_FI2C_DTTX_CMD	BIT(8)	/* send command (slave addr) */
#define     UNIPHIER_FI2C_DTTX_RD	BIT(0)	/* read transaction */
#define UNIPHIER_FI2C_DTRX	0x04	/* RX FIFO */
#define UNIPHIER_FI2C_SLAD	0x0c	/* slave address */
#define UNIPHIER_FI2C_CYC	0x10	/* clock cycle control */
#define UNIPHIER_FI2C_LCTL	0x14	/* clock low period control */
#define UNIPHIER_FI2C_SSUT	0x18	/* restart/stop setup time control */
#define UNIPHIER_FI2C_DSUT	0x1c	/* data setup time control */
#define UNIPHIER_FI2C_INT	0x20	/* interrupt status */
#define UNIPHIER_FI2C_IE	0x24	/* interrupt enable */
#define UNIPHIER_FI2C_IC	0x28	/* interrupt clear */
#define     UNIPHIER_FI2C_INT_TE	BIT(9)	/* TX FIFO empty */
#define     UNIPHIER_FI2C_INT_RF	BIT(8)	/* RX FIFO full */
#define     UNIPHIER_FI2C_INT_TC	BIT(7)	/* send complete (STOP) */
#define     UNIPHIER_FI2C_INT_RC	BIT(6)	/* receive complete (STOP) */
#define     UNIPHIER_FI2C_INT_TB	BIT(5)	/* sent specified bytes */
#define     UNIPHIER_FI2C_INT_RB	BIT(4)	/* received specified bytes */
#define     UNIPHIER_FI2C_INT_NA	BIT(2)	/* no ACK */
#define     UNIPHIER_FI2C_INT_AL	BIT(1)	/* arbitration lost */
#define UNIPHIER_FI2C_SR	0x2c	/* status register */
#define     UNIPHIER_FI2C_SR_DB		BIT(12)	/* device busy */
#define     UNIPHIER_FI2C_SR_STS	BIT(11)	/* stop condition detected */
#define     UNIPHIER_FI2C_SR_BB		BIT(8)	/* bus busy */
#define     UNIPHIER_FI2C_SR_RFF	BIT(3)	/* RX FIFO full */
#define     UNIPHIER_FI2C_SR_RNE	BIT(2)	/* RX FIFO not empty */
#define     UNIPHIER_FI2C_SR_TNF	BIT(1)	/* TX FIFO not full */
#define     UNIPHIER_FI2C_SR_TFE	BIT(0)	/* TX FIFO empty */
#define UNIPHIER_FI2C_RST	0x34	/* reset control */
#define     UNIPHIER_FI2C_RST_TBRST	BIT(2)	/* clear TX FIFO */
#define     UNIPHIER_FI2C_RST_RBRST	BIT(1)	/* clear RX FIFO */
#define     UNIPHIER_FI2C_RST_RST	BIT(0)	/* forcible bus reset */
#define UNIPHIER_FI2C_BM	0x38	/* bus monitor */
#define     UNIPHIER_FI2C_BM_SDAO	BIT(3)	/* output for SDA line */
#define     UNIPHIER_FI2C_BM_SDAS	BIT(2)	/* readback of SDA line */
#define     UNIPHIER_FI2C_BM_SCLO	BIT(1)	/* output for SCL line */
#define     UNIPHIER_FI2C_BM_SCLS	BIT(0)	/* readback of SCL line */
#define UNIPHIER_FI2C_NOISE	0x3c	/* noise filter control */
#define UNIPHIER_FI2C_TBC	0x40	/* TX byte count setting */
#define UNIPHIER_FI2C_RBC	0x44	/* RX byte count setting */
#define UNIPHIER_FI2C_TBCM	0x48	/* TX byte count monitor */
#define UNIPHIER_FI2C_RBCM	0x4c	/* RX byte count monitor */
#define UNIPHIER_FI2C_BRST	0x50	/* bus reset */
#define     UNIPHIER_FI2C_BRST_FOEN	BIT(1)	/* normal operation */
#define     UNIPHIER_FI2C_BRST_RSCL	BIT(0)	/* release SCL */

#define UNIPHIER_FI2C_INT_FAULTS	\
				(UNIPHIER_FI2C_INT_NA | UNIPHIER_FI2C_INT_AL)
#define UNIPHIER_FI2C_INT_STOP		\
				(UNIPHIER_FI2C_INT_TC | UNIPHIER_FI2C_INT_RC)

#define UNIPHIER_FI2C_RD		BIT(0)
#define UNIPHIER_FI2C_STOP		BIT(1)
#define UNIPHIER_FI2C_MANUAL_NACK	BIT(2)
#define UNIPHIER_FI2C_BYTE_WISE		BIT(3)
#define UNIPHIER_FI2C_DEFER_STOP_COMP	BIT(4)

#define UNIPHIER_FI2C_FIFO_SIZE		8

struct uniphier_fi2c_priv {
	struct completion comp;
	struct i2c_adapter adap;
	void __iomem *membase;
	struct clk *clk;
	unsigned int len;
	u8 *buf;
	u32 enabled_irqs;
	int error;
	unsigned int flags;
	unsigned int busy_cnt;
	unsigned int clk_cycle;
	spinlock_t lock;	/* IRQ synchronization */
};

static void uniphier_fi2c_fill_txfifo(struct uniphier_fi2c_priv *priv,
				      bool first)
{
	int fifo_space = UNIPHIER_FI2C_FIFO_SIZE;

	/*
	 * TX-FIFO stores slave address in it for the first access.
	 * Decrement the counter.
	 */
	if (first)
		fifo_space--;

	while (priv->len) {
		if (fifo_space-- <= 0)
			break;

		writel(*priv->buf++, priv->membase + UNIPHIER_FI2C_DTTX);
		priv->len--;
	}
}

static void uniphier_fi2c_drain_rxfifo(struct uniphier_fi2c_priv *priv)
{
	int fifo_left = priv->flags & UNIPHIER_FI2C_BYTE_WISE ?
						1 : UNIPHIER_FI2C_FIFO_SIZE;

	while (priv->len) {
		if (fifo_left-- <= 0)
			break;

		*priv->buf++ = readl(priv->membase + UNIPHIER_FI2C_DTRX);
		priv->len--;
	}
}

static void uniphier_fi2c_set_irqs(struct uniphier_fi2c_priv *priv)
{
	writel(priv->enabled_irqs, priv->membase + UNIPHIER_FI2C_IE);
}

static void uniphier_fi2c_clear_irqs(struct uniphier_fi2c_priv *priv,
				     u32 mask)
{
	writel(mask, priv->membase + UNIPHIER_FI2C_IC);
}

static void uniphier_fi2c_stop(struct uniphier_fi2c_priv *priv)
{
	priv->enabled_irqs |= UNIPHIER_FI2C_INT_STOP;
	uniphier_fi2c_set_irqs(priv);
	writel(UNIPHIER_FI2C_CR_MST | UNIPHIER_FI2C_CR_STO,
	       priv->membase + UNIPHIER_FI2C_CR);
}

static irqreturn_t uniphier_fi2c_interrupt(int irq, void *dev_id)
{
	struct uniphier_fi2c_priv *priv = dev_id;
	u32 irq_status;

	spin_lock(&priv->lock);

	irq_status = readl(priv->membase + UNIPHIER_FI2C_INT);
	irq_status &= priv->enabled_irqs;

	if (irq_status & UNIPHIER_FI2C_INT_STOP)
		goto complete;

	if (unlikely(irq_status & UNIPHIER_FI2C_INT_AL)) {
		priv->error = -EAGAIN;
		goto complete;
	}

	if (unlikely(irq_status & UNIPHIER_FI2C_INT_NA)) {
		priv->error = -ENXIO;
		if (priv->flags & UNIPHIER_FI2C_RD) {
			/*
			 * work around a hardware bug:
			 * The receive-completed interrupt is never set even if
			 * STOP condition is detected after the address phase
			 * of read transaction fails to get ACK.
			 * To avoid time-out error, we issue STOP here,
			 * but do not wait for its completion.
			 * It should be checked after exiting this handler.
			 */
			uniphier_fi2c_stop(priv);
			priv->flags |= UNIPHIER_FI2C_DEFER_STOP_COMP;
			goto complete;
		}
		goto stop;
	}

	if (irq_status & UNIPHIER_FI2C_INT_TE) {
		if (!priv->len)
			goto data_done;

		uniphier_fi2c_fill_txfifo(priv, false);
		goto handled;
	}

	if (irq_status & (UNIPHIER_FI2C_INT_RF | UNIPHIER_FI2C_INT_RB)) {
		uniphier_fi2c_drain_rxfifo(priv);
		/*
		 * If the number of bytes to read is multiple of the FIFO size
		 * (msg->len == 8, 16, 24, ...), the INT_RF bit is set a little
		 * earlier than INT_RB. We wait for INT_RB to confirm the
		 * completion of the current message.
		 */
		if (!priv->len && (irq_status & UNIPHIER_FI2C_INT_RB))
			goto data_done;

		if (unlikely(priv->flags & UNIPHIER_FI2C_MANUAL_NACK)) {
			if (priv->len <= UNIPHIER_FI2C_FIFO_SIZE &&
			    !(priv->flags & UNIPHIER_FI2C_BYTE_WISE)) {
				priv->enabled_irqs |= UNIPHIER_FI2C_INT_RB;
				uniphier_fi2c_set_irqs(priv);
				priv->flags |= UNIPHIER_FI2C_BYTE_WISE;
			}
			if (priv->len <= 1)
				writel(UNIPHIER_FI2C_CR_MST |
				       UNIPHIER_FI2C_CR_NACK,
				       priv->membase + UNIPHIER_FI2C_CR);
		}

		goto handled;
	}

	spin_unlock(&priv->lock);

	return IRQ_NONE;

data_done:
	if (priv->flags & UNIPHIER_FI2C_STOP) {
stop:
		uniphier_fi2c_stop(priv);
	} else {
complete:
		priv->enabled_irqs = 0;
		uniphier_fi2c_set_irqs(priv);
		complete(&priv->comp);
	}

handled:
	/*
	 * This controller makes a pause while any bit of the IRQ status is
	 * asserted. Clear the asserted bit to kick the controller just before
	 * exiting the handler.
	 */
	uniphier_fi2c_clear_irqs(priv, irq_status);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static void uniphier_fi2c_tx_init(struct uniphier_fi2c_priv *priv, u16 addr,
				  bool repeat)
{
	priv->enabled_irqs |= UNIPHIER_FI2C_INT_TE;
	uniphier_fi2c_set_irqs(priv);

	/* do not use TX byte counter */
	writel(0, priv->membase + UNIPHIER_FI2C_TBC);
	/* set slave address */
	writel(UNIPHIER_FI2C_DTTX_CMD | addr << 1,
	       priv->membase + UNIPHIER_FI2C_DTTX);
	/*
	 * First chunk of data. For a repeated START condition, do not write
	 * data to the TX fifo here to avoid the timing issue.
	 */
	if (!repeat)
		uniphier_fi2c_fill_txfifo(priv, true);
}

static void uniphier_fi2c_rx_init(struct uniphier_fi2c_priv *priv, u16 addr)
{
	priv->flags |= UNIPHIER_FI2C_RD;

	if (likely(priv->len < 256)) {
		/*
		 * If possible, use RX byte counter.
		 * It can automatically handle NACK for the last byte.
		 */
		writel(priv->len, priv->membase + UNIPHIER_FI2C_RBC);
		priv->enabled_irqs |= UNIPHIER_FI2C_INT_RF |
				      UNIPHIER_FI2C_INT_RB;
	} else {
		/*
		 * The byte counter can not count over 256.  In this case,
		 * do not use it at all.  Drain data when FIFO gets full,
		 * but treat the last portion as a special case.
		 */
		writel(0, priv->membase + UNIPHIER_FI2C_RBC);
		priv->flags |= UNIPHIER_FI2C_MANUAL_NACK;
		priv->enabled_irqs |= UNIPHIER_FI2C_INT_RF;
	}

	uniphier_fi2c_set_irqs(priv);

	/* set slave address with RD bit */
	writel(UNIPHIER_FI2C_DTTX_CMD | UNIPHIER_FI2C_DTTX_RD | addr << 1,
	       priv->membase + UNIPHIER_FI2C_DTTX);
}

static void uniphier_fi2c_reset(struct uniphier_fi2c_priv *priv)
{
	writel(UNIPHIER_FI2C_RST_RST, priv->membase + UNIPHIER_FI2C_RST);
}

static void uniphier_fi2c_prepare_operation(struct uniphier_fi2c_priv *priv)
{
	writel(UNIPHIER_FI2C_BRST_FOEN | UNIPHIER_FI2C_BRST_RSCL,
	       priv->membase + UNIPHIER_FI2C_BRST);
}

static void uniphier_fi2c_recover(struct uniphier_fi2c_priv *priv)
{
	uniphier_fi2c_reset(priv);
	i2c_recover_bus(&priv->adap);
}

static int uniphier_fi2c_master_xfer_one(struct i2c_adapter *adap,
					 struct i2c_msg *msg, bool repeat,
					 bool stop)
{
	struct uniphier_fi2c_priv *priv = i2c_get_adapdata(adap);
	bool is_read = msg->flags & I2C_M_RD;
	unsigned long time_left, flags;

	priv->len = msg->len;
	priv->buf = msg->buf;
	priv->enabled_irqs = UNIPHIER_FI2C_INT_FAULTS;
	priv->error = 0;
	priv->flags = 0;

	if (stop)
		priv->flags |= UNIPHIER_FI2C_STOP;

	reinit_completion(&priv->comp);
	uniphier_fi2c_clear_irqs(priv, U32_MAX);
	writel(UNIPHIER_FI2C_RST_TBRST | UNIPHIER_FI2C_RST_RBRST,
	       priv->membase + UNIPHIER_FI2C_RST);	/* reset TX/RX FIFO */

	spin_lock_irqsave(&priv->lock, flags);

	if (is_read)
		uniphier_fi2c_rx_init(priv, msg->addr);
	else
		uniphier_fi2c_tx_init(priv, msg->addr, repeat);

	/*
	 * For a repeated START condition, writing a slave address to the FIFO
	 * kicks the controller. So, the UNIPHIER_FI2C_CR register should be
	 * written only for a non-repeated START condition.
	 */
	if (!repeat)
		writel(UNIPHIER_FI2C_CR_MST | UNIPHIER_FI2C_CR_STA,
		       priv->membase + UNIPHIER_FI2C_CR);

	spin_unlock_irqrestore(&priv->lock, flags);

	time_left = wait_for_completion_timeout(&priv->comp, adap->timeout);

	spin_lock_irqsave(&priv->lock, flags);
	priv->enabled_irqs = 0;
	uniphier_fi2c_set_irqs(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (!time_left) {
		uniphier_fi2c_recover(priv);
		return -ETIMEDOUT;
	}

	if (unlikely(priv->flags & UNIPHIER_FI2C_DEFER_STOP_COMP)) {
		u32 status;
		int ret;

		ret = readl_poll_timeout(priv->membase + UNIPHIER_FI2C_SR,
					 status,
					 (status & UNIPHIER_FI2C_SR_STS) &&
					 !(status & UNIPHIER_FI2C_SR_BB),
					 1, 20);
		if (ret) {
			dev_err(&adap->dev,
				"stop condition was not completed.\n");
			uniphier_fi2c_recover(priv);
			return ret;
		}
	}

	return priv->error;
}

static int uniphier_fi2c_check_bus_busy(struct i2c_adapter *adap)
{
	struct uniphier_fi2c_priv *priv = i2c_get_adapdata(adap);

	if (readl(priv->membase + UNIPHIER_FI2C_SR) & UNIPHIER_FI2C_SR_DB) {
		if (priv->busy_cnt++ > 3) {
			/*
			 * If bus busy continues too long, it is probably
			 * in a wrong state.  Try bus recovery.
			 */
			uniphier_fi2c_recover(priv);
			priv->busy_cnt = 0;
		}

		return -EAGAIN;
	}

	priv->busy_cnt = 0;
	return 0;
}

static int uniphier_fi2c_master_xfer(struct i2c_adapter *adap,
				     struct i2c_msg *msgs, int num)
{
	struct i2c_msg *msg, *emsg = msgs + num;
	bool repeat = false;
	int ret;

	ret = uniphier_fi2c_check_bus_busy(adap);
	if (ret)
		return ret;

	for (msg = msgs; msg < emsg; msg++) {
		/* Emit STOP if it is the last message or I2C_M_STOP is set. */
		bool stop = (msg + 1 == emsg) || (msg->flags & I2C_M_STOP);

		ret = uniphier_fi2c_master_xfer_one(adap, msg, repeat, stop);
		if (ret)
			return ret;

		repeat = !stop;
	}

	return num;
}

static u32 uniphier_fi2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm uniphier_fi2c_algo = {
	.master_xfer = uniphier_fi2c_master_xfer,
	.functionality = uniphier_fi2c_functionality,
};

static int uniphier_fi2c_get_scl(struct i2c_adapter *adap)
{
	struct uniphier_fi2c_priv *priv = i2c_get_adapdata(adap);

	return !!(readl(priv->membase + UNIPHIER_FI2C_BM) &
							UNIPHIER_FI2C_BM_SCLS);
}

static void uniphier_fi2c_set_scl(struct i2c_adapter *adap, int val)
{
	struct uniphier_fi2c_priv *priv = i2c_get_adapdata(adap);

	writel(val ? UNIPHIER_FI2C_BRST_RSCL : 0,
	       priv->membase + UNIPHIER_FI2C_BRST);
}

static int uniphier_fi2c_get_sda(struct i2c_adapter *adap)
{
	struct uniphier_fi2c_priv *priv = i2c_get_adapdata(adap);

	return !!(readl(priv->membase + UNIPHIER_FI2C_BM) &
							UNIPHIER_FI2C_BM_SDAS);
}

static void uniphier_fi2c_unprepare_recovery(struct i2c_adapter *adap)
{
	uniphier_fi2c_prepare_operation(i2c_get_adapdata(adap));
}

static struct i2c_bus_recovery_info uniphier_fi2c_bus_recovery_info = {
	.recover_bus = i2c_generic_scl_recovery,
	.get_scl = uniphier_fi2c_get_scl,
	.set_scl = uniphier_fi2c_set_scl,
	.get_sda = uniphier_fi2c_get_sda,
	.unprepare_recovery = uniphier_fi2c_unprepare_recovery,
};

static void uniphier_fi2c_hw_init(struct uniphier_fi2c_priv *priv)
{
	unsigned int cyc = priv->clk_cycle;
	u32 tmp;

	tmp = readl(priv->membase + UNIPHIER_FI2C_CR);
	tmp |= UNIPHIER_FI2C_CR_MST;
	writel(tmp, priv->membase + UNIPHIER_FI2C_CR);

	uniphier_fi2c_reset(priv);

	/*
	 *  Standard-mode: tLOW + tHIGH = 10 us
	 *  Fast-mode:     tLOW + tHIGH = 2.5 us
	 */
	writel(cyc, priv->membase + UNIPHIER_FI2C_CYC);
	/*
	 *  Standard-mode: tLOW = 4.7 us, tHIGH = 4.0 us, tBUF = 4.7 us
	 *  Fast-mode:     tLOW = 1.3 us, tHIGH = 0.6 us, tBUF = 1.3 us
	 * "tLow/tHIGH = 5/4" meets both.
	 */
	writel(cyc * 5 / 9, priv->membase + UNIPHIER_FI2C_LCTL);
	/*
	 *  Standard-mode: tHD;STA = 4.0 us, tSU;STA = 4.7 us, tSU;STO = 4.0 us
	 *  Fast-mode:     tHD;STA = 0.6 us, tSU;STA = 0.6 us, tSU;STO = 0.6 us
	 */
	writel(cyc / 2, priv->membase + UNIPHIER_FI2C_SSUT);
	/*
	 *  Standard-mode: tSU;DAT = 250 ns
	 *  Fast-mode:     tSU;DAT = 100 ns
	 */
	writel(cyc / 16, priv->membase + UNIPHIER_FI2C_DSUT);

	uniphier_fi2c_prepare_operation(priv);
}

static int uniphier_fi2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_fi2c_priv *priv;
	u32 bus_speed;
	unsigned long clk_rate;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->membase))
		return PTR_ERR(priv->membase);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (of_property_read_u32(dev->of_node, "clock-frequency", &bus_speed))
		bus_speed = I2C_MAX_STANDARD_MODE_FREQ;

	if (!bus_speed || bus_speed > I2C_MAX_FAST_MODE_FREQ) {
		dev_err(dev, "invalid clock-frequency %d\n", bus_speed);
		return -EINVAL;
	}

	priv->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to enable clock\n");
		return PTR_ERR(priv->clk);
	}

	clk_rate = clk_get_rate(priv->clk);
	if (!clk_rate) {
		dev_err(dev, "input clock rate should not be zero\n");
		return -EINVAL;
	}

	priv->clk_cycle = clk_rate / bus_speed;
	init_completion(&priv->comp);
	spin_lock_init(&priv->lock);
	priv->adap.owner = THIS_MODULE;
	priv->adap.algo = &uniphier_fi2c_algo;
	priv->adap.dev.parent = dev;
	priv->adap.dev.of_node = dev->of_node;
	strscpy(priv->adap.name, "UniPhier FI2C", sizeof(priv->adap.name));
	priv->adap.bus_recovery_info = &uniphier_fi2c_bus_recovery_info;
	i2c_set_adapdata(&priv->adap, priv);
	platform_set_drvdata(pdev, priv);

	uniphier_fi2c_hw_init(priv);

	ret = devm_request_irq(dev, irq, uniphier_fi2c_interrupt, 0,
			       pdev->name, priv);
	if (ret) {
		dev_err(dev, "failed to request irq %d\n", irq);
		return ret;
	}

	return i2c_add_adapter(&priv->adap);
}

static void uniphier_fi2c_remove(struct platform_device *pdev)
{
	struct uniphier_fi2c_priv *priv = platform_get_drvdata(pdev);

	i2c_del_adapter(&priv->adap);
}

static int __maybe_unused uniphier_fi2c_suspend(struct device *dev)
{
	struct uniphier_fi2c_priv *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused uniphier_fi2c_resume(struct device *dev)
{
	struct uniphier_fi2c_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	uniphier_fi2c_hw_init(priv);

	return 0;
}

static const struct dev_pm_ops uniphier_fi2c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(uniphier_fi2c_suspend, uniphier_fi2c_resume)
};

static const struct of_device_id uniphier_fi2c_match[] = {
	{ .compatible = "socionext,uniphier-fi2c" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_fi2c_match);

static struct platform_driver uniphier_fi2c_drv = {
	.probe  = uniphier_fi2c_probe,
	.remove_new = uniphier_fi2c_remove,
	.driver = {
		.name  = "uniphier-fi2c",
		.of_match_table = uniphier_fi2c_match,
		.pm = &uniphier_fi2c_pm_ops,
	},
};
module_platform_driver(uniphier_fi2c_drv);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier FIFO-builtin I2C bus driver");
MODULE_LICENSE("GPL");
