/*
 * Driver for the Renesas RCar I2C unit
 *
 * Copyright (C) 2014 Wolfram Sang <wsa@sang-engineering.com>
 *
 * Copyright (C) 2012-14 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This file is based on the drivers/i2c/busses/i2c-sh7760.c
 * (c) 2005-2008 MSC Vertriebsges.m.b.H, Manuel Lauss <mlau@msc-ge.com>
 *
 * This file used out-of-tree driver i2c-rcar.c
 * Copyright (C) 2011-2012 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c/i2c-rcar.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/* register offsets */
#define ICSCR	0x00	/* slave ctrl */
#define ICMCR	0x04	/* master ctrl */
#define ICSSR	0x08	/* slave status */
#define ICMSR	0x0C	/* master status */
#define ICSIER	0x10	/* slave irq enable */
#define ICMIER	0x14	/* master irq enable */
#define ICCCR	0x18	/* clock dividers */
#define ICSAR	0x1C	/* slave address */
#define ICMAR	0x20	/* master address */
#define ICRXTX	0x24	/* data port */

/* ICMCR */
#define MDBS	(1 << 7)	/* non-fifo mode switch */
#define FSCL	(1 << 6)	/* override SCL pin */
#define FSDA	(1 << 5)	/* override SDA pin */
#define OBPC	(1 << 4)	/* override pins */
#define MIE	(1 << 3)	/* master if enable */
#define TSBE	(1 << 2)
#define FSB	(1 << 1)	/* force stop bit */
#define ESG	(1 << 0)	/* en startbit gen */

/* ICMSR (also for ICMIE) */
#define MNR	(1 << 6)	/* nack received */
#define MAL	(1 << 5)	/* arbitration lost */
#define MST	(1 << 4)	/* sent a stop */
#define MDE	(1 << 3)
#define MDT	(1 << 2)
#define MDR	(1 << 1)
#define MAT	(1 << 0)	/* slave addr xfer done */


#define RCAR_BUS_PHASE_START	(MDBS | MIE | ESG)
#define RCAR_BUS_PHASE_DATA	(MDBS | MIE)
#define RCAR_BUS_PHASE_STOP	(MDBS | MIE | FSB)

#define RCAR_IRQ_SEND	(MNR | MAL | MST | MAT | MDE)
#define RCAR_IRQ_RECV	(MNR | MAL | MST | MAT | MDR)
#define RCAR_IRQ_STOP	(MST)

#define RCAR_IRQ_ACK_SEND	(~(MAT | MDE) & 0xFF)
#define RCAR_IRQ_ACK_RECV	(~(MAT | MDR) & 0xFF)

#define ID_LAST_MSG	(1 << 0)
#define ID_IOERROR	(1 << 1)
#define ID_DONE		(1 << 2)
#define ID_ARBLOST	(1 << 3)
#define ID_NACK		(1 << 4)

enum rcar_i2c_type {
	I2C_RCAR_GEN1,
	I2C_RCAR_GEN2,
};

struct rcar_i2c_priv {
	void __iomem *io;
	struct i2c_adapter adap;
	struct i2c_msg	*msg;
	struct clk *clk;

	spinlock_t lock;
	wait_queue_head_t wait;

	int pos;
	u32 icccr;
	u32 flags;
	enum rcar_i2c_type devtype;
};

#define rcar_i2c_priv_to_dev(p)		((p)->adap.dev.parent)
#define rcar_i2c_is_recv(p)		((p)->msg->flags & I2C_M_RD)

#define rcar_i2c_flags_set(p, f)	((p)->flags |= (f))
#define rcar_i2c_flags_has(p, f)	((p)->flags & (f))

#define LOOP_TIMEOUT	1024


static void rcar_i2c_write(struct rcar_i2c_priv *priv, int reg, u32 val)
{
	writel(val, priv->io + reg);
}

static u32 rcar_i2c_read(struct rcar_i2c_priv *priv, int reg)
{
	return readl(priv->io + reg);
}

static void rcar_i2c_init(struct rcar_i2c_priv *priv)
{
	/*
	 * reset slave mode.
	 * slave mode is not used on this driver
	 */
	rcar_i2c_write(priv, ICSIER, 0);
	rcar_i2c_write(priv, ICSAR, 0);
	rcar_i2c_write(priv, ICSCR, 0);
	rcar_i2c_write(priv, ICSSR, 0);

	/* reset master mode */
	rcar_i2c_write(priv, ICMIER, 0);
	rcar_i2c_write(priv, ICMCR, 0);
	rcar_i2c_write(priv, ICMSR, 0);
	rcar_i2c_write(priv, ICMAR, 0);
}

static int rcar_i2c_bus_barrier(struct rcar_i2c_priv *priv)
{
	int i;

	for (i = 0; i < LOOP_TIMEOUT; i++) {
		/* make sure that bus is not busy */
		if (!(rcar_i2c_read(priv, ICMCR) & FSDA))
			return 0;
		udelay(1);
	}

	return -EBUSY;
}

static int rcar_i2c_clock_calculate(struct rcar_i2c_priv *priv,
				    u32 bus_speed,
				    struct device *dev)
{
	u32 scgd, cdf;
	u32 round, ick;
	u32 scl;
	u32 cdf_width;
	unsigned long rate;

	switch (priv->devtype) {
	case I2C_RCAR_GEN1:
		cdf_width = 2;
		break;
	case I2C_RCAR_GEN2:
		cdf_width = 3;
		break;
	default:
		dev_err(dev, "device type error\n");
		return -EIO;
	}

	/*
	 * calculate SCL clock
	 * see
	 *	ICCCR
	 *
	 * ick	= clkp / (1 + CDF)
	 * SCL	= ick / (20 + SCGD * 8 + F[(ticf + tr + intd) * ick])
	 *
	 * ick  : I2C internal clock < 20 MHz
	 * ticf : I2C SCL falling time  =  35 ns here
	 * tr   : I2C SCL rising  time  = 200 ns here
	 * intd : LSI internal delay    =  50 ns here
	 * clkp : peripheral_clk
	 * F[]  : integer up-valuation
	 */
	rate = clk_get_rate(priv->clk);
	cdf = rate / 20000000;
	if (cdf >= 1 << cdf_width) {
		dev_err(dev, "Input clock %lu too high\n", rate);
		return -EIO;
	}
	ick = rate / (cdf + 1);

	/*
	 * it is impossible to calculate large scale
	 * number on u32. separate it
	 *
	 * F[(ticf + tr + intd) * ick]
	 *  = F[(35 + 200 + 50)ns * ick]
	 *  = F[285 * ick / 1000000000]
	 *  = F[(ick / 1000000) * 285 / 1000]
	 */
	round = (ick + 500000) / 1000000 * 285;
	round = (round + 500) / 1000;

	/*
	 * SCL	= ick / (20 + SCGD * 8 + F[(ticf + tr + intd) * ick])
	 *
	 * Calculation result (= SCL) should be less than
	 * bus_speed for hardware safety
	 *
	 * We could use something along the lines of
	 *	div = ick / (bus_speed + 1) + 1;
	 *	scgd = (div - 20 - round + 7) / 8;
	 *	scl = ick / (20 + (scgd * 8) + round);
	 * (not fully verified) but that would get pretty involved
	 */
	for (scgd = 0; scgd < 0x40; scgd++) {
		scl = ick / (20 + (scgd * 8) + round);
		if (scl <= bus_speed)
			goto scgd_find;
	}
	dev_err(dev, "it is impossible to calculate best SCL\n");
	return -EIO;

scgd_find:
	dev_dbg(dev, "clk %d/%d(%lu), round %u, CDF:0x%x, SCGD: 0x%x\n",
		scl, bus_speed, clk_get_rate(priv->clk), round, cdf, scgd);

	/*
	 * keep icccr value
	 */
	priv->icccr = scgd << cdf_width | cdf;

	return 0;
}

static void rcar_i2c_prepare_msg(struct rcar_i2c_priv *priv)
{
	int read = !!rcar_i2c_is_recv(priv);

	rcar_i2c_write(priv, ICMAR, (priv->msg->addr << 1) | read);
	rcar_i2c_write(priv, ICMSR, 0);
	rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_START);
	rcar_i2c_write(priv, ICMIER, read ? RCAR_IRQ_RECV : RCAR_IRQ_SEND);
}

/*
 *		interrupt functions
 */
static int rcar_i2c_irq_send(struct rcar_i2c_priv *priv, u32 msr)
{
	struct i2c_msg *msg = priv->msg;

	/*
	 * FIXME
	 * sometimes, unknown interrupt happened.
	 * Do nothing
	 */
	if (!(msr & MDE))
		return 0;

	/*
	 * If address transfer phase finished,
	 * goto data phase.
	 */
	if (msr & MAT)
		rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_DATA);

	if (priv->pos < msg->len) {
		/*
		 * Prepare next data to ICRXTX register.
		 * This data will go to _SHIFT_ register.
		 *
		 *    *
		 * [ICRXTX] -> [SHIFT] -> [I2C bus]
		 */
		rcar_i2c_write(priv, ICRXTX, msg->buf[priv->pos]);
		priv->pos++;

	} else {
		/*
		 * The last data was pushed to ICRXTX on _PREV_ empty irq.
		 * It is on _SHIFT_ register, and will sent to I2C bus.
		 *
		 *		  *
		 * [ICRXTX] -> [SHIFT] -> [I2C bus]
		 */

		if (priv->flags & ID_LAST_MSG)
			/*
			 * If current msg is the _LAST_ msg,
			 * prepare stop condition here.
			 * ID_DONE will be set on STOP irq.
			 */
			rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_STOP);
		else
			/*
			 * If current msg is _NOT_ last msg,
			 * it doesn't call stop phase.
			 * thus, there is no STOP irq.
			 * return ID_DONE here.
			 */
			return ID_DONE;
	}

	rcar_i2c_write(priv, ICMSR, RCAR_IRQ_ACK_SEND);

	return 0;
}

static int rcar_i2c_irq_recv(struct rcar_i2c_priv *priv, u32 msr)
{
	struct i2c_msg *msg = priv->msg;

	/*
	 * FIXME
	 * sometimes, unknown interrupt happened.
	 * Do nothing
	 */
	if (!(msr & MDR))
		return 0;

	if (msr & MAT) {
		/*
		 * Address transfer phase finished,
		 * but, there is no data at this point.
		 * Do nothing.
		 */
	} else if (priv->pos < msg->len) {
		/*
		 * get received data
		 */
		msg->buf[priv->pos] = rcar_i2c_read(priv, ICRXTX);
		priv->pos++;
	}

	/*
	 * If next received data is the _LAST_,
	 * go to STOP phase,
	 * otherwise, go to DATA phase.
	 */
	if (priv->pos + 1 >= msg->len)
		rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_STOP);
	else
		rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_DATA);

	rcar_i2c_write(priv, ICMSR, RCAR_IRQ_ACK_RECV);

	return 0;
}

static irqreturn_t rcar_i2c_irq(int irq, void *ptr)
{
	struct rcar_i2c_priv *priv = ptr;
	irqreturn_t result = IRQ_HANDLED;
	u32 msr;

	/*-------------- spin lock -----------------*/
	spin_lock(&priv->lock);

	msr = rcar_i2c_read(priv, ICMSR);

	/* Only handle interrupts that are currently enabled */
	msr &= rcar_i2c_read(priv, ICMIER);
	if (!msr) {
		result = IRQ_NONE;
		goto exit;
	}

	/* Arbitration lost */
	if (msr & MAL) {
		rcar_i2c_flags_set(priv, (ID_DONE | ID_ARBLOST));
		goto out;
	}

	/* Nack */
	if (msr & MNR) {
		/* go to stop phase */
		rcar_i2c_write(priv, ICMCR, RCAR_BUS_PHASE_STOP);
		rcar_i2c_write(priv, ICMIER, RCAR_IRQ_STOP);
		rcar_i2c_flags_set(priv, ID_NACK);
		goto out;
	}

	/* Stop */
	if (msr & MST) {
		rcar_i2c_flags_set(priv, ID_DONE);
		goto out;
	}

	if (rcar_i2c_is_recv(priv))
		rcar_i2c_flags_set(priv, rcar_i2c_irq_recv(priv, msr));
	else
		rcar_i2c_flags_set(priv, rcar_i2c_irq_send(priv, msr));

out:
	if (rcar_i2c_flags_has(priv, ID_DONE)) {
		rcar_i2c_write(priv, ICMIER, 0);
		rcar_i2c_write(priv, ICMSR, 0);
		wake_up(&priv->wait);
	}

exit:
	spin_unlock(&priv->lock);
	/*-------------- spin unlock -----------------*/

	return result;
}

static int rcar_i2c_master_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msgs,
				int num)
{
	struct rcar_i2c_priv *priv = i2c_get_adapdata(adap);
	struct device *dev = rcar_i2c_priv_to_dev(priv);
	unsigned long flags;
	int i, ret, timeout;

	pm_runtime_get_sync(dev);

	/*-------------- spin lock -----------------*/
	spin_lock_irqsave(&priv->lock, flags);

	rcar_i2c_init(priv);
	/* start clock */
	rcar_i2c_write(priv, ICCCR, priv->icccr);

	spin_unlock_irqrestore(&priv->lock, flags);
	/*-------------- spin unlock -----------------*/

	ret = rcar_i2c_bus_barrier(priv);
	if (ret < 0)
		goto out;

	for (i = 0; i < num; i++) {
		/* This HW can't send STOP after address phase */
		if (msgs[i].len == 0) {
			ret = -EOPNOTSUPP;
			break;
		}

		/*-------------- spin lock -----------------*/
		spin_lock_irqsave(&priv->lock, flags);

		/* init each data */
		priv->msg	= &msgs[i];
		priv->pos	= 0;
		priv->flags	= 0;
		if (i == num - 1)
			rcar_i2c_flags_set(priv, ID_LAST_MSG);

		rcar_i2c_prepare_msg(priv);

		spin_unlock_irqrestore(&priv->lock, flags);
		/*-------------- spin unlock -----------------*/

		timeout = wait_event_timeout(priv->wait,
					     rcar_i2c_flags_has(priv, ID_DONE),
					     5 * HZ);
		if (!timeout) {
			ret = -ETIMEDOUT;
			break;
		}

		if (rcar_i2c_flags_has(priv, ID_NACK)) {
			ret = -ENXIO;
			break;
		}

		if (rcar_i2c_flags_has(priv, ID_ARBLOST)) {
			ret = -EAGAIN;
			break;
		}

		if (rcar_i2c_flags_has(priv, ID_IOERROR)) {
			ret = -EIO;
			break;
		}

		ret = i + 1; /* The number of transfer */
	}
out:
	pm_runtime_put(dev);

	if (ret < 0 && ret != -ENXIO)
		dev_err(dev, "error %d : %x\n", ret, priv->flags);

	return ret;
}

static u32 rcar_i2c_func(struct i2c_adapter *adap)
{
	/* This HW can't do SMBUS_QUICK and NOSTART */
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static const struct i2c_algorithm rcar_i2c_algo = {
	.master_xfer	= rcar_i2c_master_xfer,
	.functionality	= rcar_i2c_func,
};

static const struct of_device_id rcar_i2c_dt_ids[] = {
	{ .compatible = "renesas,i2c-rcar", .data = (void *)I2C_RCAR_GEN1 },
	{ .compatible = "renesas,i2c-r8a7778", .data = (void *)I2C_RCAR_GEN1 },
	{ .compatible = "renesas,i2c-r8a7779", .data = (void *)I2C_RCAR_GEN1 },
	{ .compatible = "renesas,i2c-r8a7790", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7791", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7792", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7793", .data = (void *)I2C_RCAR_GEN2 },
	{ .compatible = "renesas,i2c-r8a7794", .data = (void *)I2C_RCAR_GEN2 },
	{},
};
MODULE_DEVICE_TABLE(of, rcar_i2c_dt_ids);

static int rcar_i2c_probe(struct platform_device *pdev)
{
	struct i2c_rcar_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct rcar_i2c_priv *priv;
	struct i2c_adapter *adap;
	struct resource *res;
	struct device *dev = &pdev->dev;
	u32 bus_speed;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(struct rcar_i2c_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "cannot get clock\n");
		return PTR_ERR(priv->clk);
	}

	bus_speed = 100000; /* default 100 kHz */
	ret = of_property_read_u32(dev->of_node, "clock-frequency", &bus_speed);
	if (ret < 0 && pdata && pdata->bus_speed)
		bus_speed = pdata->bus_speed;

	if (pdev->dev.of_node)
		priv->devtype = (long)of_match_device(rcar_i2c_dt_ids,
						      dev)->data;
	else
		priv->devtype = platform_get_device_id(pdev)->driver_data;

	ret = rcar_i2c_clock_calculate(priv, bus_speed, dev);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->io = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->io))
		return PTR_ERR(priv->io);

	irq = platform_get_irq(pdev, 0);
	init_waitqueue_head(&priv->wait);
	spin_lock_init(&priv->lock);

	adap = &priv->adap;
	adap->nr = pdev->id;
	adap->algo = &rcar_i2c_algo;
	adap->class = I2C_CLASS_DEPRECATED;
	adap->retries = 3;
	adap->dev.parent = dev;
	adap->dev.of_node = dev->of_node;
	i2c_set_adapdata(adap, priv);
	strlcpy(adap->name, pdev->name, sizeof(adap->name));

	ret = devm_request_irq(dev, irq, rcar_i2c_irq, 0,
			       dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "cannot get irq %d\n", irq);
		return ret;
	}

	ret = i2c_add_numbered_adapter(adap);
	if (ret < 0) {
		dev_err(dev, "reg adap failed: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(dev);
	platform_set_drvdata(pdev, priv);

	dev_info(dev, "probed\n");

	return 0;
}

static int rcar_i2c_remove(struct platform_device *pdev)
{
	struct rcar_i2c_priv *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	i2c_del_adapter(&priv->adap);
	pm_runtime_disable(dev);

	return 0;
}

static struct platform_device_id rcar_i2c_id_table[] = {
	{ "i2c-rcar",		I2C_RCAR_GEN1 },
	{ "i2c-rcar_gen1",	I2C_RCAR_GEN1 },
	{ "i2c-rcar_gen2",	I2C_RCAR_GEN2 },
	{},
};
MODULE_DEVICE_TABLE(platform, rcar_i2c_id_table);

static struct platform_driver rcar_i2c_driver = {
	.driver	= {
		.name	= "i2c-rcar",
		.owner	= THIS_MODULE,
		.of_match_table = rcar_i2c_dt_ids,
	},
	.probe		= rcar_i2c_probe,
	.remove		= rcar_i2c_remove,
	.id_table	= rcar_i2c_id_table,
};

module_platform_driver(rcar_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car I2C bus driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
