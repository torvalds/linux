/*
 * SuperH Mobile I2C Controller
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * Portions of the code based on out-of-tree driver i2c-sh7343.c
 * Copyright (c) 2006 Carlos Munoz <carlos@kenati.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/of_i2c.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/i2c/i2c-sh_mobile.h>

/* Transmit operation:                                                      */
/*                                                                          */
/* 0 byte transmit                                                          */
/* BUS:     S     A8     ACK   P                                            */
/* IRQ:       DTE   WAIT                                                    */
/* ICIC:                                                                    */
/* ICCR: 0x94 0x90                                                          */
/* ICDR:      A8                                                            */
/*                                                                          */
/* 1 byte transmit                                                          */
/* BUS:     S     A8     ACK   D8(1)   ACK   P                              */
/* IRQ:       DTE   WAIT         WAIT                                       */
/* ICIC:      -DTE                                                          */
/* ICCR: 0x94       0x90                                                    */
/* ICDR:      A8    D8(1)                                                   */
/*                                                                          */
/* 2 byte transmit                                                          */
/* BUS:     S     A8     ACK   D8(1)   ACK   D8(2)   ACK   P                */
/* IRQ:       DTE   WAIT         WAIT          WAIT                         */
/* ICIC:      -DTE                                                          */
/* ICCR: 0x94                    0x90                                       */
/* ICDR:      A8    D8(1)        D8(2)                                      */
/*                                                                          */
/* 3 bytes or more, +---------+ gets repeated                               */
/*                                                                          */
/*                                                                          */
/* Receive operation:                                                       */
/*                                                                          */
/* 0 byte receive - not supported since slave may hold SDA low              */
/*                                                                          */
/* 1 byte receive       [TX] | [RX]                                         */
/* BUS:     S     A8     ACK | D8(1)   ACK   P                              */
/* IRQ:       DTE   WAIT     |   WAIT     DTE                               */
/* ICIC:      -DTE           |   +DTE                                       */
/* ICCR: 0x94       0x81     |   0xc0                                       */
/* ICDR:      A8             |            D8(1)                             */
/*                                                                          */
/* 2 byte receive        [TX]| [RX]                                         */
/* BUS:     S     A8     ACK | D8(1)   ACK   D8(2)   ACK   P                */
/* IRQ:       DTE   WAIT     |   WAIT          WAIT     DTE                 */
/* ICIC:      -DTE           |                 +DTE                         */
/* ICCR: 0x94       0x81     |                 0xc0                         */
/* ICDR:      A8             |                 D8(1)    D8(2)               */
/*                                                                          */
/* 3 byte receive       [TX] | [RX]                                         */
/* BUS:     S     A8     ACK | D8(1)   ACK   D8(2)   ACK   D8(3)   ACK    P */
/* IRQ:       DTE   WAIT     |   WAIT          WAIT         WAIT      DTE   */
/* ICIC:      -DTE           |                              +DTE            */
/* ICCR: 0x94       0x81     |                              0xc0            */
/* ICDR:      A8             |                 D8(1)        D8(2)     D8(3) */
/*                                                                          */
/* 4 bytes or more, this part is repeated    +---------+                    */
/*                                                                          */
/*                                                                          */
/* Interrupt order and BUSY flag                                            */
/*     ___                                                 _                */
/* SDA ___\___XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXAAAAAAAAA___/                 */
/* SCL      \_/1\_/2\_/3\_/4\_/5\_/6\_/7\_/8\___/9\_____/                   */
/*                                                                          */
/*        S   D7  D6  D5  D4  D3  D2  D1  D0              P                 */
/*                                           ___                            */
/* WAIT IRQ ________________________________/   \___________                */
/* TACK IRQ ____________________________________/   \_______                */
/* DTE  IRQ __________________________________________/   \_                */
/* AL   IRQ XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX                */
/*         _______________________________________________                  */
/* BUSY __/                                               \_                */
/*                                                                          */

enum sh_mobile_i2c_op {
	OP_START = 0,
	OP_TX_FIRST,
	OP_TX,
	OP_TX_STOP,
	OP_TX_TO_RX,
	OP_RX,
	OP_RX_STOP,
	OP_RX_STOP_DATA,
};

struct sh_mobile_i2c_data {
	struct device *dev;
	void __iomem *reg;
	struct i2c_adapter adap;
	unsigned long bus_speed;
	struct clk *clk;
	u_int8_t icic;
	u_int8_t flags;
	u_int16_t iccl;
	u_int16_t icch;

	spinlock_t lock;
	wait_queue_head_t wait;
	struct i2c_msg *msg;
	int pos;
	int sr;
};

#define IIC_FLAG_HAS_ICIC67	(1 << 0)

#define STANDARD_MODE		100000
#define FAST_MODE		400000

/* Register offsets */
#define ICDR			0x00
#define ICCR			0x04
#define ICSR			0x08
#define ICIC			0x0c
#define ICCL			0x10
#define ICCH			0x14

/* Register bits */
#define ICCR_ICE		0x80
#define ICCR_RACK		0x40
#define ICCR_TRS		0x10
#define ICCR_BBSY		0x04
#define ICCR_SCP		0x01

#define ICSR_SCLM		0x80
#define ICSR_SDAM		0x40
#define SW_DONE			0x20
#define ICSR_BUSY		0x10
#define ICSR_AL			0x08
#define ICSR_TACK		0x04
#define ICSR_WAIT		0x02
#define ICSR_DTE		0x01

#define ICIC_ICCLB8		0x80
#define ICIC_ICCHB8		0x40
#define ICIC_ALE		0x08
#define ICIC_TACKE		0x04
#define ICIC_WAITE		0x02
#define ICIC_DTEE		0x01

static void iic_wr(struct sh_mobile_i2c_data *pd, int offs, unsigned char data)
{
	if (offs == ICIC)
		data |= pd->icic;

	iowrite8(data, pd->reg + offs);
}

static unsigned char iic_rd(struct sh_mobile_i2c_data *pd, int offs)
{
	return ioread8(pd->reg + offs);
}

static void iic_set_clr(struct sh_mobile_i2c_data *pd, int offs,
			unsigned char set, unsigned char clr)
{
	iic_wr(pd, offs, (iic_rd(pd, offs) | set) & ~clr);
}

static u32 sh_mobile_i2c_iccl(unsigned long count_khz, u32 tLOW, u32 tf, int offset)
{
	/*
	 * Conditional expression:
	 *   ICCL >= COUNT_CLK * (tLOW + tf)
	 *
	 * SH-Mobile IIC hardware starts counting the LOW period of
	 * the SCL signal (tLOW) as soon as it pulls the SCL line.
	 * In order to meet the tLOW timing spec, we need to take into
	 * account the fall time of SCL signal (tf).  Default tf value
	 * should be 0.3 us, for safety.
	 */
	return (((count_khz * (tLOW + tf)) + 5000) / 10000) + offset;
}

static u32 sh_mobile_i2c_icch(unsigned long count_khz, u32 tHIGH, u32 tf, int offset)
{
	/*
	 * Conditional expression:
	 *   ICCH >= COUNT_CLK * (tHIGH + tf)
	 *
	 * SH-Mobile IIC hardware is aware of SCL transition period 'tr',
	 * and can ignore it.  SH-Mobile IIC controller starts counting
	 * the HIGH period of the SCL signal (tHIGH) after the SCL input
	 * voltage increases at VIH.
	 *
	 * Afterward it turned out calculating ICCH using only tHIGH spec
	 * will result in violation of the tHD;STA timing spec.  We need
	 * to take into account the fall time of SDA signal (tf) at START
	 * condition, in order to meet both tHIGH and tHD;STA specs.
	 */
	return (((count_khz * (tHIGH + tf)) + 5000) / 10000) + offset;
}

static void sh_mobile_i2c_init(struct sh_mobile_i2c_data *pd)
{
	unsigned long i2c_clk_khz;
	u32 tHIGH, tLOW, tf;
	int offset;

	/* Get clock rate after clock is enabled */
	clk_enable(pd->clk);
	i2c_clk_khz = clk_get_rate(pd->clk) / 1000;

	if (pd->bus_speed == STANDARD_MODE) {
		tLOW	= 47;	/* tLOW = 4.7 us */
		tHIGH	= 40;	/* tHD;STA = tHIGH = 4.0 us */
		tf	= 3;	/* tf = 0.3 us */
		offset	= 0;	/* No offset */
	} else if (pd->bus_speed == FAST_MODE) {
		tLOW	= 13;	/* tLOW = 1.3 us */
		tHIGH	= 6;	/* tHD;STA = tHIGH = 0.6 us */
		tf	= 3;	/* tf = 0.3 us */
		offset	= 0;	/* No offset */
	} else {
		dev_err(pd->dev, "unrecognized bus speed %lu Hz\n",
			pd->bus_speed);
		goto out;
	}

	pd->iccl = sh_mobile_i2c_iccl(i2c_clk_khz, tLOW, tf, offset);
	/* one more bit of ICCL in ICIC */
	if ((pd->iccl > 0xff) && (pd->flags & IIC_FLAG_HAS_ICIC67))
		pd->icic |= ICIC_ICCLB8;
	else
		pd->icic &= ~ICIC_ICCLB8;

	pd->icch = sh_mobile_i2c_icch(i2c_clk_khz, tHIGH, tf, offset);
	/* one more bit of ICCH in ICIC */
	if ((pd->icch > 0xff) && (pd->flags & IIC_FLAG_HAS_ICIC67))
		pd->icic |= ICIC_ICCHB8;
	else
		pd->icic &= ~ICIC_ICCHB8;

out:
	clk_disable(pd->clk);
}

static void activate_ch(struct sh_mobile_i2c_data *pd)
{
	/* Wake up device and enable clock */
	pm_runtime_get_sync(pd->dev);
	clk_enable(pd->clk);

	/* Enable channel and configure rx ack */
	iic_set_clr(pd, ICCR, ICCR_ICE, 0);

	/* Mask all interrupts */
	iic_wr(pd, ICIC, 0);

	/* Set the clock */
	iic_wr(pd, ICCL, pd->iccl & 0xff);
	iic_wr(pd, ICCH, pd->icch & 0xff);
}

static void deactivate_ch(struct sh_mobile_i2c_data *pd)
{
	/* Clear/disable interrupts */
	iic_wr(pd, ICSR, 0);
	iic_wr(pd, ICIC, 0);

	/* Disable channel */
	iic_set_clr(pd, ICCR, 0, ICCR_ICE);

	/* Disable clock and mark device as idle */
	clk_disable(pd->clk);
	pm_runtime_put_sync(pd->dev);
}

static unsigned char i2c_op(struct sh_mobile_i2c_data *pd,
			    enum sh_mobile_i2c_op op, unsigned char data)
{
	unsigned char ret = 0;
	unsigned long flags;

	dev_dbg(pd->dev, "op %d, data in 0x%02x\n", op, data);

	spin_lock_irqsave(&pd->lock, flags);

	switch (op) {
	case OP_START: /* issue start and trigger DTE interrupt */
		iic_wr(pd, ICCR, 0x94);
		break;
	case OP_TX_FIRST: /* disable DTE interrupt and write data */
		iic_wr(pd, ICIC, ICIC_WAITE | ICIC_ALE | ICIC_TACKE);
		iic_wr(pd, ICDR, data);
		break;
	case OP_TX: /* write data */
		iic_wr(pd, ICDR, data);
		break;
	case OP_TX_STOP: /* write data and issue a stop afterwards */
		iic_wr(pd, ICDR, data);
		iic_wr(pd, ICCR, 0x90);
		break;
	case OP_TX_TO_RX: /* select read mode */
		iic_wr(pd, ICCR, 0x81);
		break;
	case OP_RX: /* just read data */
		ret = iic_rd(pd, ICDR);
		break;
	case OP_RX_STOP: /* enable DTE interrupt, issue stop */
		iic_wr(pd, ICIC,
		       ICIC_DTEE | ICIC_WAITE | ICIC_ALE | ICIC_TACKE);
		iic_wr(pd, ICCR, 0xc0);
		break;
	case OP_RX_STOP_DATA: /* enable DTE interrupt, read data, issue stop */
		iic_wr(pd, ICIC,
		       ICIC_DTEE | ICIC_WAITE | ICIC_ALE | ICIC_TACKE);
		ret = iic_rd(pd, ICDR);
		iic_wr(pd, ICCR, 0xc0);
		break;
	}

	spin_unlock_irqrestore(&pd->lock, flags);

	dev_dbg(pd->dev, "op %d, data out 0x%02x\n", op, ret);
	return ret;
}

static int sh_mobile_i2c_is_first_byte(struct sh_mobile_i2c_data *pd)
{
	if (pd->pos == -1)
		return 1;

	return 0;
}

static int sh_mobile_i2c_is_last_byte(struct sh_mobile_i2c_data *pd)
{
	if (pd->pos == (pd->msg->len - 1))
		return 1;

	return 0;
}

static void sh_mobile_i2c_get_data(struct sh_mobile_i2c_data *pd,
				   unsigned char *buf)
{
	switch (pd->pos) {
	case -1:
		*buf = (pd->msg->addr & 0x7f) << 1;
		*buf |= (pd->msg->flags & I2C_M_RD) ? 1 : 0;
		break;
	default:
		*buf = pd->msg->buf[pd->pos];
	}
}

static int sh_mobile_i2c_isr_tx(struct sh_mobile_i2c_data *pd)
{
	unsigned char data;

	if (pd->pos == pd->msg->len)
		return 1;

	sh_mobile_i2c_get_data(pd, &data);

	if (sh_mobile_i2c_is_last_byte(pd))
		i2c_op(pd, OP_TX_STOP, data);
	else if (sh_mobile_i2c_is_first_byte(pd))
		i2c_op(pd, OP_TX_FIRST, data);
	else
		i2c_op(pd, OP_TX, data);

	pd->pos++;
	return 0;
}

static int sh_mobile_i2c_isr_rx(struct sh_mobile_i2c_data *pd)
{
	unsigned char data;
	int real_pos;

	do {
		if (pd->pos <= -1) {
			sh_mobile_i2c_get_data(pd, &data);

			if (sh_mobile_i2c_is_first_byte(pd))
				i2c_op(pd, OP_TX_FIRST, data);
			else
				i2c_op(pd, OP_TX, data);
			break;
		}

		if (pd->pos == 0) {
			i2c_op(pd, OP_TX_TO_RX, 0);
			break;
		}

		real_pos = pd->pos - 2;

		if (pd->pos == pd->msg->len) {
			if (real_pos < 0) {
				i2c_op(pd, OP_RX_STOP, 0);
				break;
			}
			data = i2c_op(pd, OP_RX_STOP_DATA, 0);
		} else
			data = i2c_op(pd, OP_RX, 0);

		if (real_pos >= 0)
			pd->msg->buf[real_pos] = data;
	} while (0);

	pd->pos++;
	return pd->pos == (pd->msg->len + 2);
}

static irqreturn_t sh_mobile_i2c_isr(int irq, void *dev_id)
{
	struct platform_device *dev = dev_id;
	struct sh_mobile_i2c_data *pd = platform_get_drvdata(dev);
	unsigned char sr;
	int wakeup;

	sr = iic_rd(pd, ICSR);
	pd->sr |= sr; /* remember state */

	dev_dbg(pd->dev, "i2c_isr 0x%02x 0x%02x %s %d %d!\n", sr, pd->sr,
	       (pd->msg->flags & I2C_M_RD) ? "read" : "write",
	       pd->pos, pd->msg->len);

	if (sr & (ICSR_AL | ICSR_TACK)) {
		/* don't interrupt transaction - continue to issue stop */
		iic_wr(pd, ICSR, sr & ~(ICSR_AL | ICSR_TACK));
		wakeup = 0;
	} else if (pd->msg->flags & I2C_M_RD)
		wakeup = sh_mobile_i2c_isr_rx(pd);
	else
		wakeup = sh_mobile_i2c_isr_tx(pd);

	if (sr & ICSR_WAIT) /* TODO: add delay here to support slow acks */
		iic_wr(pd, ICSR, sr & ~ICSR_WAIT);

	if (wakeup) {
		pd->sr |= SW_DONE;
		wake_up(&pd->wait);
	}

	return IRQ_HANDLED;
}

static int start_ch(struct sh_mobile_i2c_data *pd, struct i2c_msg *usr_msg)
{
	if (usr_msg->len == 0 && (usr_msg->flags & I2C_M_RD)) {
		dev_err(pd->dev, "Unsupported zero length i2c read\n");
		return -EIO;
	}

	/* Initialize channel registers */
	iic_set_clr(pd, ICCR, 0, ICCR_ICE);

	/* Enable channel and configure rx ack */
	iic_set_clr(pd, ICCR, ICCR_ICE, 0);

	/* Set the clock */
	iic_wr(pd, ICCL, pd->iccl & 0xff);
	iic_wr(pd, ICCH, pd->icch & 0xff);

	pd->msg = usr_msg;
	pd->pos = -1;
	pd->sr = 0;

	/* Enable all interrupts to begin with */
	iic_wr(pd, ICIC, ICIC_DTEE | ICIC_WAITE | ICIC_ALE | ICIC_TACKE);
	return 0;
}

static int sh_mobile_i2c_xfer(struct i2c_adapter *adapter,
			      struct i2c_msg *msgs,
			      int num)
{
	struct sh_mobile_i2c_data *pd = i2c_get_adapdata(adapter);
	struct i2c_msg	*msg;
	int err = 0;
	u_int8_t val;
	int i, k, retry_count;

	activate_ch(pd);

	/* Process all messages */
	for (i = 0; i < num; i++) {
		msg = &msgs[i];

		err = start_ch(pd, msg);
		if (err)
			break;

		i2c_op(pd, OP_START, 0);

		/* The interrupt handler takes care of the rest... */
		k = wait_event_timeout(pd->wait,
				       pd->sr & (ICSR_TACK | SW_DONE),
				       5 * HZ);
		if (!k)
			dev_err(pd->dev, "Transfer request timed out\n");

		retry_count = 1000;
again:
		val = iic_rd(pd, ICSR);

		dev_dbg(pd->dev, "val 0x%02x pd->sr 0x%02x\n", val, pd->sr);

		/* the interrupt handler may wake us up before the
		 * transfer is finished, so poll the hardware
		 * until we're done.
		 */
		if (val & ICSR_BUSY) {
			udelay(10);
			if (retry_count--)
				goto again;

			err = -EIO;
			dev_err(pd->dev, "Polling timed out\n");
			break;
		}

		/* handle missing acknowledge and arbitration lost */
		if ((val | pd->sr) & (ICSR_TACK | ICSR_AL)) {
			err = -EIO;
			break;
		}
	}

	deactivate_ch(pd);

	if (!err)
		err = num;
	return err;
}

static u32 sh_mobile_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm sh_mobile_i2c_algorithm = {
	.functionality	= sh_mobile_i2c_func,
	.master_xfer	= sh_mobile_i2c_xfer,
};

static int sh_mobile_i2c_hook_irqs(struct platform_device *dev, int hook)
{
	struct resource *res;
	int ret = -ENXIO;
	int n, k = 0;

	while ((res = platform_get_resource(dev, IORESOURCE_IRQ, k))) {
		for (n = res->start; hook && n <= res->end; n++) {
			if (request_irq(n, sh_mobile_i2c_isr, 0,
					dev_name(&dev->dev), dev)) {
				for (n--; n >= res->start; n--)
					free_irq(n, dev);

				goto rollback;
			}
		}
		k++;
	}

	if (hook)
		return k > 0 ? 0 : -ENOENT;

	ret = 0;

 rollback:
	k--;

	while (k >= 0) {
		res = platform_get_resource(dev, IORESOURCE_IRQ, k);
		for (n = res->start; n <= res->end; n++)
			free_irq(n, dev);

		k--;
	}

	return ret;
}

static int sh_mobile_i2c_probe(struct platform_device *dev)
{
	struct i2c_sh_mobile_platform_data *pdata = dev->dev.platform_data;
	struct sh_mobile_i2c_data *pd;
	struct i2c_adapter *adap;
	struct resource *res;
	int size;
	int ret;

	pd = kzalloc(sizeof(struct sh_mobile_i2c_data), GFP_KERNEL);
	if (pd == NULL) {
		dev_err(&dev->dev, "cannot allocate private data\n");
		return -ENOMEM;
	}

	pd->clk = clk_get(&dev->dev, NULL);
	if (IS_ERR(pd->clk)) {
		dev_err(&dev->dev, "cannot get clock\n");
		ret = PTR_ERR(pd->clk);
		goto err;
	}

	ret = sh_mobile_i2c_hook_irqs(dev, 1);
	if (ret) {
		dev_err(&dev->dev, "cannot request IRQ\n");
		goto err_clk;
	}

	pd->dev = &dev->dev;
	platform_set_drvdata(dev, pd);

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&dev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		goto err_irq;
	}

	size = resource_size(res);

	pd->reg = ioremap(res->start, size);
	if (pd->reg == NULL) {
		dev_err(&dev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err_irq;
	}

	/* Use platform data bus speed or STANDARD_MODE */
	pd->bus_speed = STANDARD_MODE;
	if (pdata && pdata->bus_speed)
		pd->bus_speed = pdata->bus_speed;

	/* The IIC blocks on SH-Mobile ARM processors
	 * come with two new bits in ICIC.
	 */
	if (size > 0x17)
		pd->flags |= IIC_FLAG_HAS_ICIC67;

	sh_mobile_i2c_init(pd);

	/* Enable Runtime PM for this device.
	 *
	 * Also tell the Runtime PM core to ignore children
	 * for this device since it is valid for us to suspend
	 * this I2C master driver even though the slave devices
	 * on the I2C bus may not be suspended.
	 *
	 * The state of the I2C hardware bus is unaffected by
	 * the Runtime PM state.
	 */
	pm_suspend_ignore_children(&dev->dev, true);
	pm_runtime_enable(&dev->dev);

	/* setup the private data */
	adap = &pd->adap;
	i2c_set_adapdata(adap, pd);

	adap->owner = THIS_MODULE;
	adap->algo = &sh_mobile_i2c_algorithm;
	adap->dev.parent = &dev->dev;
	adap->retries = 5;
	adap->nr = dev->id;
	adap->dev.of_node = dev->dev.of_node;

	strlcpy(adap->name, dev->name, sizeof(adap->name));

	spin_lock_init(&pd->lock);
	init_waitqueue_head(&pd->wait);

	ret = i2c_add_numbered_adapter(adap);
	if (ret < 0) {
		dev_err(&dev->dev, "cannot add numbered adapter\n");
		goto err_all;
	}

	dev_info(&dev->dev,
		 "I2C adapter %d with bus speed %lu Hz (L/H=%x/%x)\n",
		 adap->nr, pd->bus_speed, pd->iccl, pd->icch);

	of_i2c_register_devices(adap);
	return 0;

 err_all:
	iounmap(pd->reg);
 err_irq:
	sh_mobile_i2c_hook_irqs(dev, 0);
 err_clk:
	clk_put(pd->clk);
 err:
	kfree(pd);
	return ret;
}

static int sh_mobile_i2c_remove(struct platform_device *dev)
{
	struct sh_mobile_i2c_data *pd = platform_get_drvdata(dev);

	i2c_del_adapter(&pd->adap);
	iounmap(pd->reg);
	sh_mobile_i2c_hook_irqs(dev, 0);
	clk_put(pd->clk);
	pm_runtime_disable(&dev->dev);
	kfree(pd);
	return 0;
}

static int sh_mobile_i2c_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static const struct dev_pm_ops sh_mobile_i2c_dev_pm_ops = {
	.runtime_suspend = sh_mobile_i2c_runtime_nop,
	.runtime_resume = sh_mobile_i2c_runtime_nop,
};

static const struct of_device_id sh_mobile_i2c_dt_ids[] __devinitconst = {
	{ .compatible = "renesas,rmobile-iic", },
	{},
};
MODULE_DEVICE_TABLE(of, sh_mobile_i2c_dt_ids);

static struct platform_driver sh_mobile_i2c_driver = {
	.driver		= {
		.name		= "i2c-sh_mobile",
		.owner		= THIS_MODULE,
		.pm		= &sh_mobile_i2c_dev_pm_ops,
		.of_match_table = sh_mobile_i2c_dt_ids,
	},
	.probe		= sh_mobile_i2c_probe,
	.remove		= sh_mobile_i2c_remove,
};

static int __init sh_mobile_i2c_adap_init(void)
{
	return platform_driver_register(&sh_mobile_i2c_driver);
}

static void __exit sh_mobile_i2c_adap_exit(void)
{
	platform_driver_unregister(&sh_mobile_i2c_driver);
}

subsys_initcall(sh_mobile_i2c_adap_init);
module_exit(sh_mobile_i2c_adap_exit);

MODULE_DESCRIPTION("SuperH Mobile I2C Bus Controller driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c-sh_mobile");
