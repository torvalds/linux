/*
 *  drivers/i2c/busses/i2c-rcar.c
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
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
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c/i2c-rcar.h>
#include <linux/kernel.h>
#include <linux/module.h>
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

/* ICMSR */
#define MNR	(1 << 6)	/* nack received */
#define MAL	(1 << 5)	/* arbitration lost */
#define MST	(1 << 4)	/* sent a stop */
#define MDE	(1 << 3)
#define MDT	(1 << 2)
#define MDR	(1 << 1)
#define MAT	(1 << 0)	/* slave addr xfer done */

/* ICMIE */
#define MNRE	(1 << 6)	/* nack irq en */
#define MALE	(1 << 5)	/* arblos irq en */
#define MSTE	(1 << 4)	/* stop irq en */
#define MDEE	(1 << 3)
#define MDTE	(1 << 2)
#define MDRE	(1 << 1)
#define MATE	(1 << 0)	/* address sent irq en */


enum {
	RCAR_BUS_PHASE_ADDR,
	RCAR_BUS_PHASE_DATA,
	RCAR_BUS_PHASE_STOP,
};

enum {
	RCAR_IRQ_CLOSE,
	RCAR_IRQ_OPEN_FOR_SEND,
	RCAR_IRQ_OPEN_FOR_RECV,
	RCAR_IRQ_OPEN_FOR_STOP,
};

/*
 * flags
 */
#define ID_LAST_MSG	(1 << 0)
#define ID_IOERROR	(1 << 1)
#define ID_DONE		(1 << 2)
#define ID_ARBLOST	(1 << 3)
#define ID_NACK		(1 << 4)

struct rcar_i2c_priv {
	void __iomem *io;
	struct i2c_adapter adap;
	struct i2c_msg	*msg;

	spinlock_t lock;
	wait_queue_head_t wait;

	int pos;
	int irq;
	u32 icccr;
	u32 flags;
};

#define rcar_i2c_priv_to_dev(p)		((p)->adap.dev.parent)
#define rcar_i2c_is_recv(p)		((p)->msg->flags & I2C_M_RD)

#define rcar_i2c_flags_set(p, f)	((p)->flags |= (f))
#define rcar_i2c_flags_has(p, f)	((p)->flags & (f))

#define LOOP_TIMEOUT	1024

/*
 *		basic functions
 */
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

static void rcar_i2c_irq_mask(struct rcar_i2c_priv *priv, int open)
{
	u32 val = MNRE | MALE | MSTE | MATE; /* default */

	switch (open) {
	case RCAR_IRQ_OPEN_FOR_SEND:
		val |= MDEE; /* default + send */
		break;
	case RCAR_IRQ_OPEN_FOR_RECV:
		val |= MDRE; /* default + read */
		break;
	case RCAR_IRQ_OPEN_FOR_STOP:
		val = MSTE; /* stop irq only */
		break;
	case RCAR_IRQ_CLOSE:
	default:
		val = 0; /* all close */
		break;
	}
	rcar_i2c_write(priv, ICMIER, val);
}

static void rcar_i2c_set_addr(struct rcar_i2c_priv *priv, u32 recv)
{
	rcar_i2c_write(priv, ICMAR, (priv->msg->addr << 1) | recv);
}

/*
 *		bus control functions
 */
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

static void rcar_i2c_bus_phase(struct rcar_i2c_priv *priv, int phase)
{
	switch (phase) {
	case RCAR_BUS_PHASE_ADDR:
		rcar_i2c_write(priv, ICMCR, MDBS | MIE | ESG);
		break;
	case RCAR_BUS_PHASE_DATA:
		rcar_i2c_write(priv, ICMCR, MDBS | MIE);
		break;
	case RCAR_BUS_PHASE_STOP:
		rcar_i2c_write(priv, ICMCR, MDBS | MIE | FSB);
		break;
	}
}

/*
 *		clock function
 */
static int rcar_i2c_clock_calculate(struct rcar_i2c_priv *priv,
				    u32 bus_speed,
				    struct device *dev)
{
	struct clk *clkp = clk_get(NULL, "peripheral_clk");
	u32 scgd, cdf;
	u32 round, ick;
	u32 scl;

	if (!clkp) {
		dev_err(dev, "there is no peripheral_clk\n");
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
	for (cdf = 0; cdf < 4; cdf++) {
		ick = clk_get_rate(clkp) / (1 + cdf);
		if (ick < 20000000)
			goto ick_find;
	}
	dev_err(dev, "there is no best CDF\n");
	return -EIO;

ick_find:
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
		scl, bus_speed, clk_get_rate(clkp), round, cdf, scgd);

	/*
	 * keep icccr value
	 */
	priv->icccr = (scgd << 2 | cdf);

	return 0;
}

static void rcar_i2c_clock_start(struct rcar_i2c_priv *priv)
{
	rcar_i2c_write(priv, ICCCR, priv->icccr);
}

/*
 *		status functions
 */
static u32 rcar_i2c_status_get(struct rcar_i2c_priv *priv)
{
	return rcar_i2c_read(priv, ICMSR);
}

#define rcar_i2c_status_clear(priv) rcar_i2c_status_bit_clear(priv, 0xffffffff)
static void rcar_i2c_status_bit_clear(struct rcar_i2c_priv *priv, u32 bit)
{
	rcar_i2c_write(priv, ICMSR, ~bit);
}

/*
 *		recv/send functions
 */
static int rcar_i2c_recv(struct rcar_i2c_priv *priv)
{
	rcar_i2c_set_addr(priv, 1);
	rcar_i2c_status_clear(priv);
	rcar_i2c_bus_phase(priv, RCAR_BUS_PHASE_ADDR);
	rcar_i2c_irq_mask(priv, RCAR_IRQ_OPEN_FOR_RECV);

	return 0;
}

static int rcar_i2c_send(struct rcar_i2c_priv *priv)
{
	int ret;

	/*
	 * It should check bus status when send case
	 */
	ret = rcar_i2c_bus_barrier(priv);
	if (ret < 0)
		return ret;

	rcar_i2c_set_addr(priv, 0);
	rcar_i2c_status_clear(priv);
	rcar_i2c_bus_phase(priv, RCAR_BUS_PHASE_ADDR);
	rcar_i2c_irq_mask(priv, RCAR_IRQ_OPEN_FOR_SEND);

	return 0;
}

#define rcar_i2c_send_restart(priv) rcar_i2c_status_bit_clear(priv, (MAT | MDE))
#define rcar_i2c_recv_restart(priv) rcar_i2c_status_bit_clear(priv, (MAT | MDR))

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
		rcar_i2c_bus_phase(priv, RCAR_BUS_PHASE_DATA);

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
			rcar_i2c_bus_phase(priv, RCAR_BUS_PHASE_STOP);
		else
			/*
			 * If current msg is _NOT_ last msg,
			 * it doesn't call stop phase.
			 * thus, there is no STOP irq.
			 * return ID_DONE here.
			 */
			return ID_DONE;
	}

	rcar_i2c_send_restart(priv);

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
		rcar_i2c_bus_phase(priv, RCAR_BUS_PHASE_STOP);
	else
		rcar_i2c_bus_phase(priv, RCAR_BUS_PHASE_DATA);

	rcar_i2c_recv_restart(priv);

	return 0;
}

static irqreturn_t rcar_i2c_irq(int irq, void *ptr)
{
	struct rcar_i2c_priv *priv = ptr;
	struct device *dev = rcar_i2c_priv_to_dev(priv);
	u32 msr;

	/*-------------- spin lock -----------------*/
	spin_lock(&priv->lock);

	msr = rcar_i2c_status_get(priv);

	/*
	 * Arbitration lost
	 */
	if (msr & MAL) {
		/*
		 * CAUTION
		 *
		 * When arbitration lost, device become _slave_ mode.
		 */
		dev_dbg(dev, "Arbitration Lost\n");
		rcar_i2c_flags_set(priv, (ID_DONE | ID_ARBLOST));
		goto out;
	}

	/*
	 * Stop
	 */
	if (msr & MST) {
		dev_dbg(dev, "Stop\n");
		rcar_i2c_flags_set(priv, ID_DONE);
		goto out;
	}

	/*
	 * Nack
	 */
	if (msr & MNR) {
		dev_dbg(dev, "Nack\n");

		/* go to stop phase */
		rcar_i2c_bus_phase(priv, RCAR_BUS_PHASE_STOP);
		rcar_i2c_irq_mask(priv, RCAR_IRQ_OPEN_FOR_STOP);
		rcar_i2c_flags_set(priv, ID_NACK);
		goto out;
	}

	/*
	 * recv/send
	 */
	if (rcar_i2c_is_recv(priv))
		rcar_i2c_flags_set(priv, rcar_i2c_irq_recv(priv, msr));
	else
		rcar_i2c_flags_set(priv, rcar_i2c_irq_send(priv, msr));

out:
	if (rcar_i2c_flags_has(priv, ID_DONE)) {
		rcar_i2c_irq_mask(priv, RCAR_IRQ_CLOSE);
		rcar_i2c_status_clear(priv);
		wake_up(&priv->wait);
	}

	spin_unlock(&priv->lock);
	/*-------------- spin unlock -----------------*/

	return IRQ_HANDLED;
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
	rcar_i2c_clock_start(priv);

	spin_unlock_irqrestore(&priv->lock, flags);
	/*-------------- spin unlock -----------------*/

	ret = -EINVAL;
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
		if (priv->msg == &msgs[num - 1])
			rcar_i2c_flags_set(priv, ID_LAST_MSG);

		/* start send/recv */
		if (rcar_i2c_is_recv(priv))
			ret = rcar_i2c_recv(priv);
		else
			ret = rcar_i2c_send(priv);

		spin_unlock_irqrestore(&priv->lock, flags);
		/*-------------- spin unlock -----------------*/

		if (ret < 0)
			break;

		/*
		 * wait result
		 */
		timeout = wait_event_timeout(priv->wait,
					     rcar_i2c_flags_has(priv, ID_DONE),
					     5 * HZ);
		if (!timeout) {
			ret = -ETIMEDOUT;
			break;
		}

		/*
		 * error handling
		 */
		if (rcar_i2c_flags_has(priv, ID_NACK)) {
			ret = -EREMOTEIO;
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

	pm_runtime_put(dev);

	if (ret < 0)
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

static int rcar_i2c_probe(struct platform_device *pdev)
{
	struct i2c_rcar_platform_data *pdata = pdev->dev.platform_data;
	struct rcar_i2c_priv *priv;
	struct i2c_adapter *adap;
	struct resource *res;
	struct device *dev = &pdev->dev;
	u32 bus_speed;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "no mmio resources\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(struct rcar_i2c_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "no mem for private data\n");
		return -ENOMEM;
	}

	bus_speed = 100000; /* default 100 kHz */
	if (pdata && pdata->bus_speed)
		bus_speed = pdata->bus_speed;
	ret = rcar_i2c_clock_calculate(priv, bus_speed, dev);
	if (ret < 0)
		return ret;

	priv->io = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->io))
		return PTR_ERR(priv->io);

	priv->irq = platform_get_irq(pdev, 0);
	init_waitqueue_head(&priv->wait);
	spin_lock_init(&priv->lock);

	adap			= &priv->adap;
	adap->nr		= pdev->id;
	adap->algo		= &rcar_i2c_algo;
	adap->class		= I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adap->retries		= 3;
	adap->dev.parent	= dev;
	i2c_set_adapdata(adap, priv);
	strlcpy(adap->name, pdev->name, sizeof(adap->name));

	ret = devm_request_irq(dev, priv->irq, rcar_i2c_irq, 0,
			       dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "cannot get irq %d\n", priv->irq);
		return ret;
	}

	pm_runtime_enable(dev);
	platform_set_drvdata(pdev, priv);

	ret = i2c_add_numbered_adapter(adap);
	if (ret < 0) {
		dev_err(dev, "reg adap failed: %d\n", ret);
		pm_runtime_disable(dev);
		return ret;
	}

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

static struct platform_driver rcar_i2c_driver = {
	.driver	= {
		.name	= "i2c-rcar",
		.owner	= THIS_MODULE,
	},
	.probe		= rcar_i2c_probe,
	.remove		= rcar_i2c_remove,
};

module_platform_driver(rcar_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas R-Car I2C bus driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
