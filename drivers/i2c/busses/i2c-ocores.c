// SPDX-License-Identifier: GPL-2.0
/*
 * i2c-ocores.c: I2C bus driver for OpenCores I2C controller
 * (https://opencores.org/project/i2c/overview)
 *
 * Peter Korsgaard <peter@korsgaard.com>
 *
 * Support for the GRLIB port of the controller by
 * Andreas Larsson <andreas@gaisler.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/platform_data/i2c-ocores.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

/*
 * 'process_lock' exists because ocores_process() and ocores_process_timeout()
 * can't run in parallel.
 */
struct ocores_i2c {
	void __iomem *base;
	int iobase;
	u32 reg_shift;
	u32 reg_io_width;
	unsigned long flags;
	wait_queue_head_t wait;
	struct i2c_adapter adap;
	struct i2c_msg *msg;
	int pos;
	int nmsgs;
	int state; /* see STATE_ */
	spinlock_t process_lock;
	struct clk *clk;
	int ip_clock_khz;
	int bus_clock_khz;
	void (*setreg)(struct ocores_i2c *i2c, int reg, u8 value);
	u8 (*getreg)(struct ocores_i2c *i2c, int reg);
};

/* registers */
#define OCI2C_PRELOW		0
#define OCI2C_PREHIGH		1
#define OCI2C_CONTROL		2
#define OCI2C_DATA		3
#define OCI2C_CMD		4 /* write only */
#define OCI2C_STATUS		4 /* read only, same address as OCI2C_CMD */

#define OCI2C_CTRL_IEN		0x40
#define OCI2C_CTRL_EN		0x80

#define OCI2C_CMD_START		0x91
#define OCI2C_CMD_STOP		0x41
#define OCI2C_CMD_READ		0x21
#define OCI2C_CMD_WRITE		0x11
#define OCI2C_CMD_READ_ACK	0x21
#define OCI2C_CMD_READ_NACK	0x29
#define OCI2C_CMD_IACK		0x01

#define OCI2C_STAT_IF		0x01
#define OCI2C_STAT_TIP		0x02
#define OCI2C_STAT_ARBLOST	0x20
#define OCI2C_STAT_BUSY		0x40
#define OCI2C_STAT_NACK		0x80

#define STATE_DONE		0
#define STATE_START		1
#define STATE_WRITE		2
#define STATE_READ		3
#define STATE_ERROR		4

#define TYPE_OCORES		0
#define TYPE_GRLIB		1
#define TYPE_SIFIVE_REV0	2

#define OCORES_FLAG_BROKEN_IRQ BIT(1) /* Broken IRQ for FU540-C000 SoC */

static void oc_setreg_8(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite8(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_16(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite16(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_32(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite32(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_16be(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite16be(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_32be(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite32be(value, i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_8(struct ocores_i2c *i2c, int reg)
{
	return ioread8(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_16(struct ocores_i2c *i2c, int reg)
{
	return ioread16(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_32(struct ocores_i2c *i2c, int reg)
{
	return ioread32(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_16be(struct ocores_i2c *i2c, int reg)
{
	return ioread16be(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_32be(struct ocores_i2c *i2c, int reg)
{
	return ioread32be(i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_io_8(struct ocores_i2c *i2c, int reg, u8 value)
{
	outb(value, i2c->iobase + reg);
}

static inline u8 oc_getreg_io_8(struct ocores_i2c *i2c, int reg)
{
	return inb(i2c->iobase + reg);
}

static inline void oc_setreg(struct ocores_i2c *i2c, int reg, u8 value)
{
	i2c->setreg(i2c, reg, value);
}

static inline u8 oc_getreg(struct ocores_i2c *i2c, int reg)
{
	return i2c->getreg(i2c, reg);
}

static void ocores_process(struct ocores_i2c *i2c, u8 stat)
{
	struct i2c_msg *msg = i2c->msg;
	unsigned long flags;

	/*
	 * If we spin here is because we are in timeout, so we are going
	 * to be in STATE_ERROR. See ocores_process_timeout()
	 */
	spin_lock_irqsave(&i2c->process_lock, flags);

	if ((i2c->state == STATE_DONE) || (i2c->state == STATE_ERROR)) {
		/* stop has been sent */
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
		wake_up(&i2c->wait);
		goto out;
	}

	/* error? */
	if (stat & OCI2C_STAT_ARBLOST) {
		i2c->state = STATE_ERROR;
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
		goto out;
	}

	if ((i2c->state == STATE_START) || (i2c->state == STATE_WRITE)) {
		i2c->state =
			(msg->flags & I2C_M_RD) ? STATE_READ : STATE_WRITE;

		if (stat & OCI2C_STAT_NACK) {
			i2c->state = STATE_ERROR;
			oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
			goto out;
		}
	} else {
		msg->buf[i2c->pos++] = oc_getreg(i2c, OCI2C_DATA);
	}

	/* end of msg? */
	if (i2c->pos == msg->len) {
		i2c->nmsgs--;
		i2c->msg++;
		i2c->pos = 0;
		msg = i2c->msg;

		if (i2c->nmsgs) {	/* end? */
			/* send start? */
			if (!(msg->flags & I2C_M_NOSTART)) {
				u8 addr = i2c_8bit_addr_from_msg(msg);

				i2c->state = STATE_START;

				oc_setreg(i2c, OCI2C_DATA, addr);
				oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_START);
				goto out;
			}
			i2c->state = (msg->flags & I2C_M_RD)
				? STATE_READ : STATE_WRITE;
		} else {
			i2c->state = STATE_DONE;
			oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
			goto out;
		}
	}

	if (i2c->state == STATE_READ) {
		oc_setreg(i2c, OCI2C_CMD, i2c->pos == (msg->len-1) ?
			  OCI2C_CMD_READ_NACK : OCI2C_CMD_READ_ACK);
	} else {
		oc_setreg(i2c, OCI2C_DATA, msg->buf[i2c->pos++]);
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_WRITE);
	}

out:
	spin_unlock_irqrestore(&i2c->process_lock, flags);
}

static irqreturn_t ocores_isr(int irq, void *dev_id)
{
	struct ocores_i2c *i2c = dev_id;
	u8 stat = oc_getreg(i2c, OCI2C_STATUS);

	if (i2c->flags & OCORES_FLAG_BROKEN_IRQ) {
		if ((stat & OCI2C_STAT_IF) && !(stat & OCI2C_STAT_BUSY))
			return IRQ_NONE;
	} else if (!(stat & OCI2C_STAT_IF)) {
		return IRQ_NONE;
	}
	ocores_process(i2c, stat);

	return IRQ_HANDLED;
}

/**
 * Process timeout event
 * @i2c: ocores I2C device instance
 */
static void ocores_process_timeout(struct ocores_i2c *i2c)
{
	unsigned long flags;

	spin_lock_irqsave(&i2c->process_lock, flags);
	i2c->state = STATE_ERROR;
	oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
	spin_unlock_irqrestore(&i2c->process_lock, flags);
}

/**
 * Wait until something change in a given register
 * @i2c: ocores I2C device instance
 * @reg: register to query
 * @mask: bitmask to apply on register value
 * @val: expected result
 * @timeout: timeout in jiffies
 *
 * Timeout is necessary to avoid to stay here forever when the chip
 * does not answer correctly.
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
static int ocores_wait(struct ocores_i2c *i2c,
		       int reg, u8 mask, u8 val,
		       const unsigned long timeout)
{
	unsigned long j;

	j = jiffies + timeout;
	while (1) {
		u8 status = oc_getreg(i2c, reg);

		if ((status & mask) == val)
			break;

		if (time_after(jiffies, j))
			return -ETIMEDOUT;
	}
	return 0;
}

/**
 * Wait until is possible to process some data
 * @i2c: ocores I2C device instance
 *
 * Used when the device is in polling mode (interrupts disabled).
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
static int ocores_poll_wait(struct ocores_i2c *i2c)
{
	u8 mask;
	int err;

	if (i2c->state == STATE_DONE || i2c->state == STATE_ERROR) {
		/* transfer is over */
		mask = OCI2C_STAT_BUSY;
	} else {
		/* on going transfer */
		mask = OCI2C_STAT_TIP;
		/*
		 * We wait for the data to be transferred (8bit),
		 * then we start polling on the ACK/NACK bit
		 */
		udelay((8 * 1000) / i2c->bus_clock_khz);
	}

	/*
	 * once we are here we expect to get the expected result immediately
	 * so if after 1ms we timeout then something is broken.
	 */
	err = ocores_wait(i2c, OCI2C_STATUS, mask, 0, msecs_to_jiffies(1));
	if (err)
		dev_warn(i2c->adap.dev.parent,
			 "%s: STATUS timeout, bit 0x%x did not clear in 1ms\n",
			 __func__, mask);
	return err;
}

/**
 * It handles an IRQ-less transfer
 * @i2c: ocores I2C device instance
 *
 * Even if IRQ are disabled, the I2C OpenCore IP behavior is exactly the same
 * (only that IRQ are not produced). This means that we can re-use entirely
 * ocores_isr(), we just add our polling code around it.
 *
 * It can run in atomic context
 */
static void ocores_process_polling(struct ocores_i2c *i2c)
{
	while (1) {
		irqreturn_t ret;
		int err;

		err = ocores_poll_wait(i2c);
		if (err) {
			i2c->state = STATE_ERROR;
			break; /* timeout */
		}

		ret = ocores_isr(-1, i2c);
		if (ret == IRQ_NONE)
			break; /* all messages have been transferred */
		else {
			if (i2c->flags & OCORES_FLAG_BROKEN_IRQ)
				if (i2c->state == STATE_DONE)
					break;
		}
	}
}

static int ocores_xfer_core(struct ocores_i2c *i2c,
			    struct i2c_msg *msgs, int num,
			    bool polling)
{
	int ret;
	u8 ctrl;

	ctrl = oc_getreg(i2c, OCI2C_CONTROL);
	if (polling)
		oc_setreg(i2c, OCI2C_CONTROL, ctrl & ~OCI2C_CTRL_IEN);
	else
		oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_IEN);

	i2c->msg = msgs;
	i2c->pos = 0;
	i2c->nmsgs = num;
	i2c->state = STATE_START;

	oc_setreg(i2c, OCI2C_DATA, i2c_8bit_addr_from_msg(i2c->msg));
	oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_START);

	if (polling) {
		ocores_process_polling(i2c);
	} else {
		ret = wait_event_timeout(i2c->wait,
					 (i2c->state == STATE_ERROR) ||
					 (i2c->state == STATE_DONE), HZ);
		if (ret == 0) {
			ocores_process_timeout(i2c);
			return -ETIMEDOUT;
		}
	}

	return (i2c->state == STATE_DONE) ? num : -EIO;
}

static int ocores_xfer_polling(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	return ocores_xfer_core(i2c_get_adapdata(adap), msgs, num, true);
}

static int ocores_xfer(struct i2c_adapter *adap,
		       struct i2c_msg *msgs, int num)
{
	return ocores_xfer_core(i2c_get_adapdata(adap), msgs, num, false);
}

static int ocores_init(struct device *dev, struct ocores_i2c *i2c)
{
	int prescale;
	int diff;
	u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

	/* make sure the device is disabled */
	ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl);

	prescale = (i2c->ip_clock_khz / (5 * i2c->bus_clock_khz)) - 1;
	prescale = clamp(prescale, 0, 0xffff);

	diff = i2c->ip_clock_khz / (5 * (prescale + 1)) - i2c->bus_clock_khz;
	if (abs(diff) > i2c->bus_clock_khz / 10) {
		dev_err(dev,
			"Unsupported clock settings: core: %d KHz, bus: %d KHz\n",
			i2c->ip_clock_khz, i2c->bus_clock_khz);
		return -EINVAL;
	}

	oc_setreg(i2c, OCI2C_PRELOW, prescale & 0xff);
	oc_setreg(i2c, OCI2C_PREHIGH, prescale >> 8);

	/* Init the device */
	oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_EN);

	return 0;
}


static u32 ocores_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm ocores_algorithm = {
	.master_xfer = ocores_xfer,
	.master_xfer_atomic = ocores_xfer_polling,
	.functionality = ocores_func,
};

static const struct i2c_adapter ocores_adapter = {
	.owner = THIS_MODULE,
	.name = "i2c-ocores",
	.class = I2C_CLASS_DEPRECATED,
	.algo = &ocores_algorithm,
};

static const struct of_device_id ocores_i2c_match[] = {
	{
		.compatible = "opencores,i2c-ocores",
		.data = (void *)TYPE_OCORES,
	},
	{
		.compatible = "aeroflexgaisler,i2cmst",
		.data = (void *)TYPE_GRLIB,
	},
	{
		.compatible = "sifive,fu540-c000-i2c",
		.data = (void *)TYPE_SIFIVE_REV0,
	},
	{
		.compatible = "sifive,i2c0",
		.data = (void *)TYPE_SIFIVE_REV0,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ocores_i2c_match);

#ifdef CONFIG_OF
/*
 * Read and write functions for the GRLIB port of the controller. Registers are
 * 32-bit big endian and the PRELOW and PREHIGH registers are merged into one
 * register. The subsequent registers have their offsets decreased accordingly.
 */
static u8 oc_getreg_grlib(struct ocores_i2c *i2c, int reg)
{
	u32 rd;
	int rreg = reg;

	if (reg != OCI2C_PRELOW)
		rreg--;
	rd = ioread32be(i2c->base + (rreg << i2c->reg_shift));
	if (reg == OCI2C_PREHIGH)
		return (u8)(rd >> 8);
	else
		return (u8)rd;
}

static void oc_setreg_grlib(struct ocores_i2c *i2c, int reg, u8 value)
{
	u32 curr, wr;
	int rreg = reg;

	if (reg != OCI2C_PRELOW)
		rreg--;
	if (reg == OCI2C_PRELOW || reg == OCI2C_PREHIGH) {
		curr = ioread32be(i2c->base + (rreg << i2c->reg_shift));
		if (reg == OCI2C_PRELOW)
			wr = (curr & 0xff00) | value;
		else
			wr = (((u32)value) << 8) | (curr & 0xff);
	} else {
		wr = value;
	}
	iowrite32be(wr, i2c->base + (rreg << i2c->reg_shift));
}

static int ocores_i2c_of_probe(struct platform_device *pdev,
				struct ocores_i2c *i2c)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	u32 val;
	u32 clock_frequency;
	bool clock_frequency_present;

	if (of_property_read_u32(np, "reg-shift", &i2c->reg_shift)) {
		/* no 'reg-shift', check for deprecated 'regstep' */
		if (!of_property_read_u32(np, "regstep", &val)) {
			if (!is_power_of_2(val)) {
				dev_err(&pdev->dev, "invalid regstep %d\n",
					val);
				return -EINVAL;
			}
			i2c->reg_shift = ilog2(val);
			dev_warn(&pdev->dev,
				"regstep property deprecated, use reg-shift\n");
		}
	}

	clock_frequency_present = !of_property_read_u32(np, "clock-frequency",
							&clock_frequency);
	i2c->bus_clock_khz = 100;

	i2c->clk = devm_clk_get(&pdev->dev, NULL);

	if (!IS_ERR(i2c->clk)) {
		int ret = clk_prepare_enable(i2c->clk);

		if (ret) {
			dev_err(&pdev->dev,
				"clk_prepare_enable failed: %d\n", ret);
			return ret;
		}
		i2c->ip_clock_khz = clk_get_rate(i2c->clk) / 1000;
		if (clock_frequency_present)
			i2c->bus_clock_khz = clock_frequency / 1000;
	}

	if (i2c->ip_clock_khz == 0) {
		if (of_property_read_u32(np, "opencores,ip-clock-frequency",
						&val)) {
			if (!clock_frequency_present) {
				dev_err(&pdev->dev,
					"Missing required parameter 'opencores,ip-clock-frequency'\n");
				clk_disable_unprepare(i2c->clk);
				return -ENODEV;
			}
			i2c->ip_clock_khz = clock_frequency / 1000;
			dev_warn(&pdev->dev,
				 "Deprecated usage of the 'clock-frequency' property, please update to 'opencores,ip-clock-frequency'\n");
		} else {
			i2c->ip_clock_khz = val / 1000;
			if (clock_frequency_present)
				i2c->bus_clock_khz = clock_frequency / 1000;
		}
	}

	of_property_read_u32(pdev->dev.of_node, "reg-io-width",
				&i2c->reg_io_width);

	match = of_match_node(ocores_i2c_match, pdev->dev.of_node);
	if (match && (long)match->data == TYPE_GRLIB) {
		dev_dbg(&pdev->dev, "GRLIB variant of i2c-ocores\n");
		i2c->setreg = oc_setreg_grlib;
		i2c->getreg = oc_getreg_grlib;
	}

	return 0;
}
#else
#define ocores_i2c_of_probe(pdev, i2c) -ENODEV
#endif

static int ocores_i2c_probe(struct platform_device *pdev)
{
	struct ocores_i2c *i2c;
	struct ocores_i2c_platform_data *pdata;
	const struct of_device_id *match;
	struct resource *res;
	int irq;
	int ret;
	int i;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	spin_lock_init(&i2c->process_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		i2c->base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(i2c->base))
			return PTR_ERR(i2c->base);
	} else {
		res = platform_get_resource(pdev, IORESOURCE_IO, 0);
		if (!res)
			return -EINVAL;
		i2c->iobase = res->start;
		if (!devm_request_region(&pdev->dev, res->start,
					 resource_size(res),
					 pdev->name)) {
			dev_err(&pdev->dev, "Can't get I/O resource.\n");
			return -EBUSY;
		}
		i2c->setreg = oc_setreg_io_8;
		i2c->getreg = oc_getreg_io_8;
	}

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata) {
		i2c->reg_shift = pdata->reg_shift;
		i2c->reg_io_width = pdata->reg_io_width;
		i2c->ip_clock_khz = pdata->clock_khz;
		if (pdata->bus_khz)
			i2c->bus_clock_khz = pdata->bus_khz;
		else
			i2c->bus_clock_khz = 100;
	} else {
		ret = ocores_i2c_of_probe(pdev, i2c);
		if (ret)
			return ret;
	}

	if (i2c->reg_io_width == 0)
		i2c->reg_io_width = 1; /* Set to default value */

	if (!i2c->setreg || !i2c->getreg) {
		bool be = pdata ? pdata->big_endian :
			of_device_is_big_endian(pdev->dev.of_node);

		switch (i2c->reg_io_width) {
		case 1:
			i2c->setreg = oc_setreg_8;
			i2c->getreg = oc_getreg_8;
			break;

		case 2:
			i2c->setreg = be ? oc_setreg_16be : oc_setreg_16;
			i2c->getreg = be ? oc_getreg_16be : oc_getreg_16;
			break;

		case 4:
			i2c->setreg = be ? oc_setreg_32be : oc_setreg_32;
			i2c->getreg = be ? oc_getreg_32be : oc_getreg_32;
			break;

		default:
			dev_err(&pdev->dev, "Unsupported I/O width (%d)\n",
				i2c->reg_io_width);
			ret = -EINVAL;
			goto err_clk;
		}
	}

	init_waitqueue_head(&i2c->wait);

	irq = platform_get_irq(pdev, 0);
	if (irq == -ENXIO) {
		ocores_algorithm.master_xfer = ocores_xfer_polling;

		/*
		 * Set in OCORES_FLAG_BROKEN_IRQ to enable workaround for
		 * FU540-C000 SoC in polling mode.
		 */
		match = of_match_node(ocores_i2c_match, pdev->dev.of_node);
		if (match && (long)match->data == TYPE_SIFIVE_REV0)
			i2c->flags |= OCORES_FLAG_BROKEN_IRQ;
	} else {
		if (irq < 0)
			return irq;
	}

	if (ocores_algorithm.master_xfer != ocores_xfer_polling) {
		ret = devm_request_irq(&pdev->dev, irq, ocores_isr, 0,
				       pdev->name, i2c);
		if (ret) {
			dev_err(&pdev->dev, "Cannot claim IRQ\n");
			goto err_clk;
		}
	}

	ret = ocores_init(&pdev->dev, i2c);
	if (ret)
		goto err_clk;

	/* hook up driver to tree */
	platform_set_drvdata(pdev, i2c);
	i2c->adap = ocores_adapter;
	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = pdev->dev.of_node;

	/* add i2c adapter to i2c tree */
	ret = i2c_add_adapter(&i2c->adap);
	if (ret)
		goto err_clk;

	/* add in known devices to the bus */
	if (pdata) {
		for (i = 0; i < pdata->num_devices; i++)
			i2c_new_device(&i2c->adap, pdata->devices + i);
	}

	return 0;

err_clk:
	clk_disable_unprepare(i2c->clk);
	return ret;
}

static int ocores_i2c_remove(struct platform_device *pdev)
{
	struct ocores_i2c *i2c = platform_get_drvdata(pdev);
	u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

	/* disable i2c logic */
	ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl);

	/* remove adapter & data */
	i2c_del_adapter(&i2c->adap);

	if (!IS_ERR(i2c->clk))
		clk_disable_unprepare(i2c->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ocores_i2c_suspend(struct device *dev)
{
	struct ocores_i2c *i2c = dev_get_drvdata(dev);
	u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

	/* make sure the device is disabled */
	ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl);

	if (!IS_ERR(i2c->clk))
		clk_disable_unprepare(i2c->clk);
	return 0;
}

static int ocores_i2c_resume(struct device *dev)
{
	struct ocores_i2c *i2c = dev_get_drvdata(dev);

	if (!IS_ERR(i2c->clk)) {
		unsigned long rate;
		int ret = clk_prepare_enable(i2c->clk);

		if (ret) {
			dev_err(dev,
				"clk_prepare_enable failed: %d\n", ret);
			return ret;
		}
		rate = clk_get_rate(i2c->clk) / 1000;
		if (rate)
			i2c->ip_clock_khz = rate;
	}
	return ocores_init(dev, i2c);
}

static SIMPLE_DEV_PM_OPS(ocores_i2c_pm, ocores_i2c_suspend, ocores_i2c_resume);
#define OCORES_I2C_PM	(&ocores_i2c_pm)
#else
#define OCORES_I2C_PM	NULL
#endif

static struct platform_driver ocores_i2c_driver = {
	.probe   = ocores_i2c_probe,
	.remove  = ocores_i2c_remove,
	.driver  = {
		.name = "ocores-i2c",
		.of_match_table = ocores_i2c_match,
		.pm = OCORES_I2C_PM,
	},
};

module_platform_driver(ocores_i2c_driver);

MODULE_AUTHOR("Peter Korsgaard <peter@korsgaard.com>");
MODULE_DESCRIPTION("OpenCores I2C bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ocores-i2c");
