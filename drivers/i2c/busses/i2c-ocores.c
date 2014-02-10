/*
 * i2c-ocores.c: I2C bus driver for OpenCores I2C controller
 * (http://www.opencores.org/projects.cgi/web/i2c/overview).
 *
 * Peter Korsgaard <jacmet@sunsite.dk>
 *
 * Support for the GRLIB port of the controller by
 * Andreas Larsson <andreas@gaisler.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/i2c-ocores.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/log2.h>

struct ocores_i2c {
	void __iomem *base;
	u32 reg_shift;
	u32 reg_io_width;
	wait_queue_head_t wait;
	struct i2c_adapter adap;
	struct i2c_msg *msg;
	int pos;
	int nmsgs;
	int state; /* see STATE_ */
	int clock_khz;
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

static inline void oc_setreg(struct ocores_i2c *i2c, int reg, u8 value)
{
	i2c->setreg(i2c, reg, value);
}

static inline u8 oc_getreg(struct ocores_i2c *i2c, int reg)
{
	return i2c->getreg(i2c, reg);
}

static void ocores_process(struct ocores_i2c *i2c)
{
	struct i2c_msg *msg = i2c->msg;
	u8 stat = oc_getreg(i2c, OCI2C_STATUS);

	if ((i2c->state == STATE_DONE) || (i2c->state == STATE_ERROR)) {
		/* stop has been sent */
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
		wake_up(&i2c->wait);
		return;
	}

	/* error? */
	if (stat & OCI2C_STAT_ARBLOST) {
		i2c->state = STATE_ERROR;
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
		return;
	}

	if ((i2c->state == STATE_START) || (i2c->state == STATE_WRITE)) {
		i2c->state =
			(msg->flags & I2C_M_RD) ? STATE_READ : STATE_WRITE;

		if (stat & OCI2C_STAT_NACK) {
			i2c->state = STATE_ERROR;
			oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
			return;
		}
	} else
		msg->buf[i2c->pos++] = oc_getreg(i2c, OCI2C_DATA);

	/* end of msg? */
	if (i2c->pos == msg->len) {
		i2c->nmsgs--;
		i2c->msg++;
		i2c->pos = 0;
		msg = i2c->msg;

		if (i2c->nmsgs) {	/* end? */
			/* send start? */
			if (!(msg->flags & I2C_M_NOSTART)) {
				u8 addr = (msg->addr << 1);

				if (msg->flags & I2C_M_RD)
					addr |= 1;

				i2c->state = STATE_START;

				oc_setreg(i2c, OCI2C_DATA, addr);
				oc_setreg(i2c, OCI2C_CMD,  OCI2C_CMD_START);
				return;
			} else
				i2c->state = (msg->flags & I2C_M_RD)
					? STATE_READ : STATE_WRITE;
		} else {
			i2c->state = STATE_DONE;
			oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
			return;
		}
	}

	if (i2c->state == STATE_READ) {
		oc_setreg(i2c, OCI2C_CMD, i2c->pos == (msg->len-1) ?
			  OCI2C_CMD_READ_NACK : OCI2C_CMD_READ_ACK);
	} else {
		oc_setreg(i2c, OCI2C_DATA, msg->buf[i2c->pos++]);
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_WRITE);
	}
}

static irqreturn_t ocores_isr(int irq, void *dev_id)
{
	struct ocores_i2c *i2c = dev_id;

	ocores_process(i2c);

	return IRQ_HANDLED;
}

static int ocores_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct ocores_i2c *i2c = i2c_get_adapdata(adap);

	i2c->msg = msgs;
	i2c->pos = 0;
	i2c->nmsgs = num;
	i2c->state = STATE_START;

	oc_setreg(i2c, OCI2C_DATA,
			(i2c->msg->addr << 1) |
			((i2c->msg->flags & I2C_M_RD) ? 1:0));

	oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_START);

	if (wait_event_timeout(i2c->wait, (i2c->state == STATE_ERROR) ||
			       (i2c->state == STATE_DONE), HZ))
		return (i2c->state == STATE_DONE) ? num : -EIO;
	else
		return -ETIMEDOUT;
}

static void ocores_init(struct ocores_i2c *i2c)
{
	int prescale;
	u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

	/* make sure the device is disabled */
	oc_setreg(i2c, OCI2C_CONTROL, ctrl & ~(OCI2C_CTRL_EN|OCI2C_CTRL_IEN));

	prescale = (i2c->clock_khz / (5*100)) - 1;
	oc_setreg(i2c, OCI2C_PRELOW, prescale & 0xff);
	oc_setreg(i2c, OCI2C_PREHIGH, prescale >> 8);

	/* Init the device */
	oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_IEN | OCI2C_CTRL_EN);
}


static u32 ocores_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm ocores_algorithm = {
	.master_xfer	= ocores_xfer,
	.functionality	= ocores_func,
};

static struct i2c_adapter ocores_adapter = {
	.owner		= THIS_MODULE,
	.name		= "i2c-ocores",
	.class		= I2C_CLASS_HWMON | I2C_CLASS_SPD | I2C_CLASS_DEPRECATED,
	.algo		= &ocores_algorithm,
};

static struct of_device_id ocores_i2c_match[] = {
	{
		.compatible = "opencores,i2c-ocores",
		.data = (void *)TYPE_OCORES,
	},
	{
		.compatible = "aeroflexgaisler,i2cmst",
		.data = (void *)TYPE_GRLIB,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ocores_i2c_match);

#ifdef CONFIG_OF
/* Read and write functions for the GRLIB port of the controller. Registers are
 * 32-bit big endian and the PRELOW and PREHIGH registers are merged into one
 * register. The subsequent registers has their offset decreased accordingly. */
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

	if (of_property_read_u32(np, "clock-frequency", &val)) {
		dev_err(&pdev->dev,
			"Missing required parameter 'clock-frequency'\n");
		return -ENODEV;
	}
	i2c->clock_khz = val / 1000;

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
#define ocores_i2c_of_probe(pdev,i2c) -ENODEV
#endif

static int ocores_i2c_probe(struct platform_device *pdev)
{
	struct ocores_i2c *i2c;
	struct ocores_i2c_platform_data *pdata;
	struct resource *res;
	int irq;
	int ret;
	int i;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata) {
		i2c->reg_shift = pdata->reg_shift;
		i2c->reg_io_width = pdata->reg_io_width;
		i2c->clock_khz = pdata->clock_khz;
	} else {
		ret = ocores_i2c_of_probe(pdev, i2c);
		if (ret)
			return ret;
	}

	if (i2c->reg_io_width == 0)
		i2c->reg_io_width = 1; /* Set to default value */

	if (!i2c->setreg || !i2c->getreg) {
		switch (i2c->reg_io_width) {
		case 1:
			i2c->setreg = oc_setreg_8;
			i2c->getreg = oc_getreg_8;
			break;

		case 2:
			i2c->setreg = oc_setreg_16;
			i2c->getreg = oc_getreg_16;
			break;

		case 4:
			i2c->setreg = oc_setreg_32;
			i2c->getreg = oc_getreg_32;
			break;

		default:
			dev_err(&pdev->dev, "Unsupported I/O width (%d)\n",
				i2c->reg_io_width);
			return -EINVAL;
		}
	}

	ocores_init(i2c);

	init_waitqueue_head(&i2c->wait);
	ret = devm_request_irq(&pdev->dev, irq, ocores_isr, 0,
			       pdev->name, i2c);
	if (ret) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		return ret;
	}

	/* hook up driver to tree */
	platform_set_drvdata(pdev, i2c);
	i2c->adap = ocores_adapter;
	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = pdev->dev.of_node;

	/* add i2c adapter to i2c tree */
	ret = i2c_add_adapter(&i2c->adap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add adapter\n");
		return ret;
	}

	/* add in known devices to the bus */
	if (pdata) {
		for (i = 0; i < pdata->num_devices; i++)
			i2c_new_device(&i2c->adap, pdata->devices + i);
	}

	return 0;
}

static int ocores_i2c_remove(struct platform_device *pdev)
{
	struct ocores_i2c *i2c = platform_get_drvdata(pdev);

	/* disable i2c logic */
	oc_setreg(i2c, OCI2C_CONTROL, oc_getreg(i2c, OCI2C_CONTROL)
		  & ~(OCI2C_CTRL_EN|OCI2C_CTRL_IEN));

	/* remove adapter & data */
	i2c_del_adapter(&i2c->adap);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ocores_i2c_suspend(struct device *dev)
{
	struct ocores_i2c *i2c = dev_get_drvdata(dev);
	u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

	/* make sure the device is disabled */
	oc_setreg(i2c, OCI2C_CONTROL, ctrl & ~(OCI2C_CTRL_EN|OCI2C_CTRL_IEN));

	return 0;
}

static int ocores_i2c_resume(struct device *dev)
{
	struct ocores_i2c *i2c = dev_get_drvdata(dev);

	ocores_init(i2c);

	return 0;
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
		.owner = THIS_MODULE,
		.name = "ocores-i2c",
		.of_match_table = ocores_i2c_match,
		.pm = OCORES_I2C_PM,
	},
};

module_platform_driver(ocores_i2c_driver);

MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("OpenCores I2C bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ocores-i2c");
