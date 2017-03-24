/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

#define UNIPHIER_FI2C_DEFAULT_SPEED	100000
#define UNIPHIER_FI2C_MAX_SPEED		400000
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

		dev_dbg(&priv->adap.dev, "write data: %02x\n", *priv->buf);
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
		dev_dbg(&priv->adap.dev, "read data: %02x\n", priv->buf[-1]);
		priv->len--;
	}
}

static void uniphier_fi2c_set_irqs(struct uniphier_fi2c_priv *priv)
{
	writel(priv->enabled_irqs, priv->membase + UNIPHIER_FI2C_IE);
}

static void uniphier_fi2c_clear_irqs(struct uniphier_fi2c_priv *priv)
{
	writel(-1, priv->membase + UNIPHIER_FI2C_IC);
}

static void uniphier_fi2c_stop(struct uniphier_fi2c_priv *priv)
{
	dev_dbg(&priv->adap.dev, "stop condition\n");

	priv->enabled_irqs |= UNIPHIER_FI2C_INT_STOP;
	uniphier_fi2c_set_irqs(priv);
	writel(UNIPHIER_FI2C_CR_MST | UNIPHIER_FI2C_CR_STO,
	       priv->membase + UNIPHIER_FI2C_CR);
}

static irqreturn_t uniphier_fi2c_interrupt(int irq, void *dev_id)
{
	struct uniphier_fi2c_priv *priv = dev_id;
	u32 irq_status;

	irq_status = readl(priv->membase + UNIPHIER_FI2C_INT);

	dev_dbg(&priv->adap.dev,
		"interrupt: enabled_irqs=%04x, irq_status=%04x\n",
		priv->enabled_irqs, irq_status);

	if (irq_status & UNIPHIER_FI2C_INT_STOP)
		goto complete;

	if (unlikely(irq_status & UNIPHIER_FI2C_INT_AL)) {
		dev_dbg(&priv->adap.dev, "arbitration lost\n");
		priv->error = -EAGAIN;
		goto complete;
	}

	if (unlikely(irq_status & UNIPHIER_FI2C_INT_NA)) {
		dev_dbg(&priv->adap.dev, "could not get ACK\n");
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
		if (!priv->len)
			goto data_done;

		if (unlikely(priv->flags & UNIPHIER_FI2C_MANUAL_NACK)) {
			if (priv->len <= UNIPHIER_FI2C_FIFO_SIZE &&
			    !(priv->flags & UNIPHIER_FI2C_BYTE_WISE)) {
				dev_dbg(&priv->adap.dev,
					"enable read byte count IRQ\n");
				priv->enabled_irqs |= UNIPHIER_FI2C_INT_RB;
				uniphier_fi2c_set_irqs(priv);
				priv->flags |= UNIPHIER_FI2C_BYTE_WISE;
			}
			if (priv->len <= 1) {
				dev_dbg(&priv->adap.dev, "set NACK\n");
				writel(UNIPHIER_FI2C_CR_MST |
				       UNIPHIER_FI2C_CR_NACK,
				       priv->membase + UNIPHIER_FI2C_CR);
			}
		}

		goto handled;
	}

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
	uniphier_fi2c_clear_irqs(priv);

	return IRQ_HANDLED;
}

static void uniphier_fi2c_tx_init(struct uniphier_fi2c_priv *priv, u16 addr)
{
	priv->enabled_irqs |= UNIPHIER_FI2C_INT_TE;
	/* do not use TX byte counter */
	writel(0, priv->membase + UNIPHIER_FI2C_TBC);
	/* set slave address */
	writel(UNIPHIER_FI2C_DTTX_CMD | addr << 1,
	       priv->membase + UNIPHIER_FI2C_DTTX);
	/* first chunk of data */
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
					 struct i2c_msg *msg, bool stop)
{
	struct uniphier_fi2c_priv *priv = i2c_get_adapdata(adap);
	bool is_read = msg->flags & I2C_M_RD;
	unsigned long time_left;

	dev_dbg(&adap->dev, "%s: addr=0x%02x, len=%d, stop=%d\n",
		is_read ? "receive" : "transmit", msg->addr, msg->len, stop);

	priv->len = msg->len;
	priv->buf = msg->buf;
	priv->enabled_irqs = UNIPHIER_FI2C_INT_FAULTS;
	priv->error = 0;
	priv->flags = 0;

	if (stop)
		priv->flags |= UNIPHIER_FI2C_STOP;

	reinit_completion(&priv->comp);
	uniphier_fi2c_clear_irqs(priv);
	writel(UNIPHIER_FI2C_RST_TBRST | UNIPHIER_FI2C_RST_RBRST,
	       priv->membase + UNIPHIER_FI2C_RST);	/* reset TX/RX FIFO */

	if (is_read)
		uniphier_fi2c_rx_init(priv, msg->addr);
	else
		uniphier_fi2c_tx_init(priv, msg->addr);

	uniphier_fi2c_set_irqs(priv);

	dev_dbg(&adap->dev, "start condition\n");
	writel(UNIPHIER_FI2C_CR_MST | UNIPHIER_FI2C_CR_STA,
	       priv->membase + UNIPHIER_FI2C_CR);

	time_left = wait_for_completion_timeout(&priv->comp, adap->timeout);
	if (!time_left) {
		dev_err(&adap->dev, "transaction timeout.\n");
		uniphier_fi2c_recover(priv);
		return -ETIMEDOUT;
	}
	dev_dbg(&adap->dev, "complete\n");

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
	int ret;

	ret = uniphier_fi2c_check_bus_busy(adap);
	if (ret)
		return ret;

	for (msg = msgs; msg < emsg; msg++) {
		/* If next message is read, skip the stop condition */
		bool stop = !(msg + 1 < emsg && msg[1].flags & I2C_M_RD);
		/* but, force it if I2C_M_STOP is set */
		if (msg->flags & I2C_M_STOP)
			stop = true;

		ret = uniphier_fi2c_master_xfer_one(adap, msg, stop);
		if (ret)
			return ret;
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

static void uniphier_fi2c_hw_init(struct uniphier_fi2c_priv *priv,
				  u32 bus_speed, unsigned long clk_rate)
{
	u32 tmp;

	tmp = readl(priv->membase + UNIPHIER_FI2C_CR);
	tmp |= UNIPHIER_FI2C_CR_MST;
	writel(tmp, priv->membase + UNIPHIER_FI2C_CR);

	uniphier_fi2c_reset(priv);

	tmp = clk_rate / bus_speed;

	writel(tmp, priv->membase + UNIPHIER_FI2C_CYC);
	writel(tmp / 2, priv->membase + UNIPHIER_FI2C_LCTL);
	writel(tmp / 2, priv->membase + UNIPHIER_FI2C_SSUT);
	writel(tmp / 16, priv->membase + UNIPHIER_FI2C_DSUT);

	uniphier_fi2c_prepare_operation(priv);
}

static int uniphier_fi2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_fi2c_priv *priv;
	struct resource *regs;
	u32 bus_speed;
	unsigned long clk_rate;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->membase = devm_ioremap_resource(dev, regs);
	if (IS_ERR(priv->membase))
		return PTR_ERR(priv->membase);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get IRQ number\n");
		return irq;
	}

	if (of_property_read_u32(dev->of_node, "clock-frequency", &bus_speed))
		bus_speed = UNIPHIER_FI2C_DEFAULT_SPEED;

	if (!bus_speed || bus_speed > UNIPHIER_FI2C_MAX_SPEED) {
		dev_err(dev, "invalid clock-frequency %d\n", bus_speed);
		return -EINVAL;
	}

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	clk_rate = clk_get_rate(priv->clk);
	if (!clk_rate) {
		dev_err(dev, "input clock rate should not be zero\n");
		ret = -EINVAL;
		goto disable_clk;
	}

	init_completion(&priv->comp);
	priv->adap.owner = THIS_MODULE;
	priv->adap.algo = &uniphier_fi2c_algo;
	priv->adap.dev.parent = dev;
	priv->adap.dev.of_node = dev->of_node;
	strlcpy(priv->adap.name, "UniPhier FI2C", sizeof(priv->adap.name));
	priv->adap.bus_recovery_info = &uniphier_fi2c_bus_recovery_info;
	i2c_set_adapdata(&priv->adap, priv);
	platform_set_drvdata(pdev, priv);

	uniphier_fi2c_hw_init(priv, bus_speed, clk_rate);

	ret = devm_request_irq(dev, irq, uniphier_fi2c_interrupt, 0,
			       pdev->name, priv);
	if (ret) {
		dev_err(dev, "failed to request irq %d\n", irq);
		goto disable_clk;
	}

	ret = i2c_add_adapter(&priv->adap);
disable_clk:
	if (ret)
		clk_disable_unprepare(priv->clk);

	return ret;
}

static int uniphier_fi2c_remove(struct platform_device *pdev)
{
	struct uniphier_fi2c_priv *priv = platform_get_drvdata(pdev);

	i2c_del_adapter(&priv->adap);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id uniphier_fi2c_match[] = {
	{ .compatible = "socionext,uniphier-fi2c" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_fi2c_match);

static struct platform_driver uniphier_fi2c_drv = {
	.probe  = uniphier_fi2c_probe,
	.remove = uniphier_fi2c_remove,
	.driver = {
		.name  = "uniphier-fi2c",
		.of_match_table = uniphier_fi2c_match,
	},
};
module_platform_driver(uniphier_fi2c_drv);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier FIFO-builtin I2C bus driver");
MODULE_LICENSE("GPL");
