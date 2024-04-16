// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define UNIPHIER_I2C_DTRM	0x00	/* TX register */
#define     UNIPHIER_I2C_DTRM_IRQEN	BIT(11)	/* enable interrupt */
#define     UNIPHIER_I2C_DTRM_STA	BIT(10)	/* start condition */
#define     UNIPHIER_I2C_DTRM_STO	BIT(9)	/* stop condition */
#define     UNIPHIER_I2C_DTRM_NACK	BIT(8)	/* do not return ACK */
#define     UNIPHIER_I2C_DTRM_RD	BIT(0)	/* read transaction */
#define UNIPHIER_I2C_DREC	0x04	/* RX register */
#define     UNIPHIER_I2C_DREC_MST	BIT(14)	/* 1 = master, 0 = slave */
#define     UNIPHIER_I2C_DREC_TX	BIT(13)	/* 1 = transmit, 0 = receive */
#define     UNIPHIER_I2C_DREC_STS	BIT(12)	/* stop condition detected */
#define     UNIPHIER_I2C_DREC_LRB	BIT(11)	/* no ACK */
#define     UNIPHIER_I2C_DREC_LAB	BIT(9)	/* arbitration lost */
#define     UNIPHIER_I2C_DREC_BBN	BIT(8)	/* bus not busy */
#define UNIPHIER_I2C_MYAD	0x08	/* slave address */
#define UNIPHIER_I2C_CLK	0x0c	/* clock frequency control */
#define UNIPHIER_I2C_BRST	0x10	/* bus reset */
#define     UNIPHIER_I2C_BRST_FOEN	BIT(1)	/* normal operation */
#define     UNIPHIER_I2C_BRST_RSCL	BIT(0)	/* release SCL */
#define UNIPHIER_I2C_HOLD	0x14	/* hold time control */
#define UNIPHIER_I2C_BSTS	0x18	/* bus status monitor */
#define     UNIPHIER_I2C_BSTS_SDA	BIT(1)	/* readback of SDA line */
#define     UNIPHIER_I2C_BSTS_SCL	BIT(0)	/* readback of SCL line */
#define UNIPHIER_I2C_NOISE	0x1c	/* noise filter control */
#define UNIPHIER_I2C_SETUP	0x20	/* setup time control */

struct uniphier_i2c_priv {
	struct completion comp;
	struct i2c_adapter adap;
	void __iomem *membase;
	struct clk *clk;
	unsigned int busy_cnt;
	unsigned int clk_cycle;
};

static irqreturn_t uniphier_i2c_interrupt(int irq, void *dev_id)
{
	struct uniphier_i2c_priv *priv = dev_id;

	/*
	 * This hardware uses edge triggered interrupt.  Do not touch the
	 * hardware registers in this handler to make sure to catch the next
	 * interrupt edge.  Just send a complete signal and return.
	 */
	complete(&priv->comp);

	return IRQ_HANDLED;
}

static int uniphier_i2c_xfer_byte(struct i2c_adapter *adap, u32 txdata,
				  u32 *rxdatap)
{
	struct uniphier_i2c_priv *priv = i2c_get_adapdata(adap);
	unsigned long time_left;
	u32 rxdata;

	reinit_completion(&priv->comp);

	txdata |= UNIPHIER_I2C_DTRM_IRQEN;
	writel(txdata, priv->membase + UNIPHIER_I2C_DTRM);

	time_left = wait_for_completion_timeout(&priv->comp, adap->timeout);
	if (unlikely(!time_left)) {
		dev_err(&adap->dev, "transaction timeout\n");
		return -ETIMEDOUT;
	}

	rxdata = readl(priv->membase + UNIPHIER_I2C_DREC);
	if (rxdatap)
		*rxdatap = rxdata;

	return 0;
}

static int uniphier_i2c_send_byte(struct i2c_adapter *adap, u32 txdata)
{
	u32 rxdata;
	int ret;

	ret = uniphier_i2c_xfer_byte(adap, txdata, &rxdata);
	if (ret)
		return ret;

	if (unlikely(rxdata & UNIPHIER_I2C_DREC_LAB))
		return -EAGAIN;

	if (unlikely(rxdata & UNIPHIER_I2C_DREC_LRB))
		return -ENXIO;

	return 0;
}

static int uniphier_i2c_tx(struct i2c_adapter *adap, u16 addr, u16 len,
			   const u8 *buf)
{
	int ret;

	ret = uniphier_i2c_send_byte(adap, addr << 1 |
				     UNIPHIER_I2C_DTRM_STA |
				     UNIPHIER_I2C_DTRM_NACK);
	if (ret)
		return ret;

	while (len--) {
		ret = uniphier_i2c_send_byte(adap,
					     UNIPHIER_I2C_DTRM_NACK | *buf++);
		if (ret)
			return ret;
	}

	return 0;
}

static int uniphier_i2c_rx(struct i2c_adapter *adap, u16 addr, u16 len,
			   u8 *buf)
{
	int ret;

	ret = uniphier_i2c_send_byte(adap, addr << 1 |
				     UNIPHIER_I2C_DTRM_STA |
				     UNIPHIER_I2C_DTRM_NACK |
				     UNIPHIER_I2C_DTRM_RD);
	if (ret)
		return ret;

	while (len--) {
		u32 rxdata;

		ret = uniphier_i2c_xfer_byte(adap,
					     len ? 0 : UNIPHIER_I2C_DTRM_NACK,
					     &rxdata);
		if (ret)
			return ret;
		*buf++ = rxdata;
	}

	return 0;
}

static int uniphier_i2c_stop(struct i2c_adapter *adap)
{
	return uniphier_i2c_send_byte(adap, UNIPHIER_I2C_DTRM_STO |
				      UNIPHIER_I2C_DTRM_NACK);
}

static int uniphier_i2c_master_xfer_one(struct i2c_adapter *adap,
					struct i2c_msg *msg, bool stop)
{
	bool is_read = msg->flags & I2C_M_RD;
	bool recovery = false;
	int ret;

	if (is_read)
		ret = uniphier_i2c_rx(adap, msg->addr, msg->len, msg->buf);
	else
		ret = uniphier_i2c_tx(adap, msg->addr, msg->len, msg->buf);

	if (ret == -EAGAIN) /* could not acquire bus. bail out without STOP */
		return ret;

	if (ret == -ETIMEDOUT) {
		/* This error is fatal.  Needs recovery. */
		stop = false;
		recovery = true;
	}

	if (stop) {
		int ret2 = uniphier_i2c_stop(adap);

		if (ret2) {
			/* Failed to issue STOP.  The bus needs recovery. */
			recovery = true;
			ret = ret ?: ret2;
		}
	}

	if (recovery)
		i2c_recover_bus(adap);

	return ret;
}

static int uniphier_i2c_check_bus_busy(struct i2c_adapter *adap)
{
	struct uniphier_i2c_priv *priv = i2c_get_adapdata(adap);

	if (!(readl(priv->membase + UNIPHIER_I2C_DREC) &
						UNIPHIER_I2C_DREC_BBN)) {
		if (priv->busy_cnt++ > 3) {
			/*
			 * If bus busy continues too long, it is probably
			 * in a wrong state.  Try bus recovery.
			 */
			i2c_recover_bus(adap);
			priv->busy_cnt = 0;
		}

		return -EAGAIN;
	}

	priv->busy_cnt = 0;
	return 0;
}

static int uniphier_i2c_master_xfer(struct i2c_adapter *adap,
				    struct i2c_msg *msgs, int num)
{
	struct i2c_msg *msg, *emsg = msgs + num;
	int ret;

	ret = uniphier_i2c_check_bus_busy(adap);
	if (ret)
		return ret;

	for (msg = msgs; msg < emsg; msg++) {
		/* Emit STOP if it is the last message or I2C_M_STOP is set. */
		bool stop = (msg + 1 == emsg) || (msg->flags & I2C_M_STOP);

		ret = uniphier_i2c_master_xfer_one(adap, msg, stop);
		if (ret)
			return ret;
	}

	return num;
}

static u32 uniphier_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm uniphier_i2c_algo = {
	.master_xfer = uniphier_i2c_master_xfer,
	.functionality = uniphier_i2c_functionality,
};

static void uniphier_i2c_reset(struct uniphier_i2c_priv *priv, bool reset_on)
{
	u32 val = UNIPHIER_I2C_BRST_RSCL;

	val |= reset_on ? 0 : UNIPHIER_I2C_BRST_FOEN;
	writel(val, priv->membase + UNIPHIER_I2C_BRST);
}

static int uniphier_i2c_get_scl(struct i2c_adapter *adap)
{
	struct uniphier_i2c_priv *priv = i2c_get_adapdata(adap);

	return !!(readl(priv->membase + UNIPHIER_I2C_BSTS) &
							UNIPHIER_I2C_BSTS_SCL);
}

static void uniphier_i2c_set_scl(struct i2c_adapter *adap, int val)
{
	struct uniphier_i2c_priv *priv = i2c_get_adapdata(adap);

	writel(val ? UNIPHIER_I2C_BRST_RSCL : 0,
	       priv->membase + UNIPHIER_I2C_BRST);
}

static int uniphier_i2c_get_sda(struct i2c_adapter *adap)
{
	struct uniphier_i2c_priv *priv = i2c_get_adapdata(adap);

	return !!(readl(priv->membase + UNIPHIER_I2C_BSTS) &
							UNIPHIER_I2C_BSTS_SDA);
}

static void uniphier_i2c_unprepare_recovery(struct i2c_adapter *adap)
{
	uniphier_i2c_reset(i2c_get_adapdata(adap), false);
}

static struct i2c_bus_recovery_info uniphier_i2c_bus_recovery_info = {
	.recover_bus = i2c_generic_scl_recovery,
	.get_scl = uniphier_i2c_get_scl,
	.set_scl = uniphier_i2c_set_scl,
	.get_sda = uniphier_i2c_get_sda,
	.unprepare_recovery = uniphier_i2c_unprepare_recovery,
};

static void uniphier_i2c_hw_init(struct uniphier_i2c_priv *priv)
{
	unsigned int cyc = priv->clk_cycle;

	uniphier_i2c_reset(priv, true);

	/*
	 * Bit30-16: clock cycles of tLOW.
	 *  Standard-mode: tLOW = 4.7 us, tHIGH = 4.0 us
	 *  Fast-mode:     tLOW = 1.3 us, tHIGH = 0.6 us
	 * "tLow/tHIGH = 5/4" meets both.
	 */
	writel((cyc * 5 / 9 << 16) | cyc, priv->membase + UNIPHIER_I2C_CLK);

	uniphier_i2c_reset(priv, false);
}

static int uniphier_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_i2c_priv *priv;
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

	priv->clk_cycle = clk_rate / bus_speed;
	init_completion(&priv->comp);
	priv->adap.owner = THIS_MODULE;
	priv->adap.algo = &uniphier_i2c_algo;
	priv->adap.dev.parent = dev;
	priv->adap.dev.of_node = dev->of_node;
	strscpy(priv->adap.name, "UniPhier I2C", sizeof(priv->adap.name));
	priv->adap.bus_recovery_info = &uniphier_i2c_bus_recovery_info;
	i2c_set_adapdata(&priv->adap, priv);
	platform_set_drvdata(pdev, priv);

	uniphier_i2c_hw_init(priv);

	ret = devm_request_irq(dev, irq, uniphier_i2c_interrupt, 0, pdev->name,
			       priv);
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

static int uniphier_i2c_remove(struct platform_device *pdev)
{
	struct uniphier_i2c_priv *priv = platform_get_drvdata(pdev);

	i2c_del_adapter(&priv->adap);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused uniphier_i2c_suspend(struct device *dev)
{
	struct uniphier_i2c_priv *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused uniphier_i2c_resume(struct device *dev)
{
	struct uniphier_i2c_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	uniphier_i2c_hw_init(priv);

	return 0;
}

static const struct dev_pm_ops uniphier_i2c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(uniphier_i2c_suspend, uniphier_i2c_resume)
};

static const struct of_device_id uniphier_i2c_match[] = {
	{ .compatible = "socionext,uniphier-i2c" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_i2c_match);

static struct platform_driver uniphier_i2c_drv = {
	.probe  = uniphier_i2c_probe,
	.remove = uniphier_i2c_remove,
	.driver = {
		.name  = "uniphier-i2c",
		.of_match_table = uniphier_i2c_match,
		.pm = &uniphier_i2c_pm_ops,
	},
};
module_platform_driver(uniphier_i2c_drv);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier I2C bus driver");
MODULE_LICENSE("GPL");
