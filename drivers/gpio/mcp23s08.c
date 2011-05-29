/*
 * mcp23s08.c - SPI gpio expander driver
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/mcp23s08.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

/**
 * MCP types supported by driver
 */
#define MCP_TYPE_S08	0
#define MCP_TYPE_S17	1

/* Registers are all 8 bits wide.
 *
 * The mcp23s17 has twice as many bits, and can be configured to work
 * with either 16 bit registers or with two adjacent 8 bit banks.
 *
 * Also, there are I2C versions of both chips.
 */
#define MCP_IODIR	0x00		/* init/reset:  all ones */
#define MCP_IPOL	0x01
#define MCP_GPINTEN	0x02
#define MCP_DEFVAL	0x03
#define MCP_INTCON	0x04
#define MCP_IOCON	0x05
#	define IOCON_SEQOP	(1 << 5)
#	define IOCON_HAEN	(1 << 3)
#	define IOCON_ODR	(1 << 2)
#	define IOCON_INTPOL	(1 << 1)
#define MCP_GPPU	0x06
#define MCP_INTF	0x07
#define MCP_INTCAP	0x08
#define MCP_GPIO	0x09
#define MCP_OLAT	0x0a

struct mcp23s08;

struct mcp23s08_ops {
	int	(*read)(struct mcp23s08 *mcp, unsigned reg);
	int	(*write)(struct mcp23s08 *mcp, unsigned reg, unsigned val);
	int	(*read_regs)(struct mcp23s08 *mcp, unsigned reg,
			     u16 *vals, unsigned n);
};

struct mcp23s08 {
	struct spi_device	*spi;
	u8			addr;

	u16			cache[11];
	/* lock protects the cached values */
	struct mutex		lock;

	struct gpio_chip	chip;

	struct work_struct	work;

	const struct mcp23s08_ops	*ops;
};

/* A given spi_device can represent up to eight mcp23sxx chips
 * sharing the same chipselect but using different addresses
 * (e.g. chips #0 and #3 might be populated, but not #1 or $2).
 * Driver data holds all the per-chip data.
 */
struct mcp23s08_driver_data {
	unsigned		ngpio;
	struct mcp23s08		*mcp[8];
	struct mcp23s08		chip[];
};

static int mcp23s08_read(struct mcp23s08 *mcp, unsigned reg)
{
	u8	tx[2], rx[1];
	int	status;

	tx[0] = mcp->addr | 0x01;
	tx[1] = reg;
	status = spi_write_then_read(mcp->spi, tx, sizeof tx, rx, sizeof rx);
	return (status < 0) ? status : rx[0];
}

static int mcp23s08_write(struct mcp23s08 *mcp, unsigned reg, unsigned val)
{
	u8	tx[3];

	tx[0] = mcp->addr;
	tx[1] = reg;
	tx[2] = val;
	return spi_write_then_read(mcp->spi, tx, sizeof tx, NULL, 0);
}

static int
mcp23s08_read_regs(struct mcp23s08 *mcp, unsigned reg, u16 *vals, unsigned n)
{
	u8	tx[2], *tmp;
	int	status;

	if ((n + reg) > sizeof mcp->cache)
		return -EINVAL;
	tx[0] = mcp->addr | 0x01;
	tx[1] = reg;

	tmp = (u8 *)vals;
	status = spi_write_then_read(mcp->spi, tx, sizeof tx, tmp, n);
	if (status >= 0) {
		while (n--)
			vals[n] = tmp[n]; /* expand to 16bit */
	}
	return status;
}

static int mcp23s17_read(struct mcp23s08 *mcp, unsigned reg)
{
	u8	tx[2], rx[2];
	int	status;

	tx[0] = mcp->addr | 0x01;
	tx[1] = reg << 1;
	status = spi_write_then_read(mcp->spi, tx, sizeof tx, rx, sizeof rx);
	return (status < 0) ? status : (rx[0] | (rx[1] << 8));
}

static int mcp23s17_write(struct mcp23s08 *mcp, unsigned reg, unsigned val)
{
	u8	tx[4];

	tx[0] = mcp->addr;
	tx[1] = reg << 1;
	tx[2] = val;
	tx[3] = val >> 8;
	return spi_write_then_read(mcp->spi, tx, sizeof tx, NULL, 0);
}

static int
mcp23s17_read_regs(struct mcp23s08 *mcp, unsigned reg, u16 *vals, unsigned n)
{
	u8	tx[2];
	int	status;

	if ((n + reg) > sizeof mcp->cache)
		return -EINVAL;
	tx[0] = mcp->addr | 0x01;
	tx[1] = reg << 1;

	status = spi_write_then_read(mcp->spi, tx, sizeof tx,
				     (u8 *)vals, n * 2);
	if (status >= 0) {
		while (n--)
			vals[n] = __le16_to_cpu((__le16)vals[n]);
	}

	return status;
}

static const struct mcp23s08_ops mcp23s08_ops = {
	.read		= mcp23s08_read,
	.write		= mcp23s08_write,
	.read_regs	= mcp23s08_read_regs,
};

static const struct mcp23s08_ops mcp23s17_ops = {
	.read		= mcp23s17_read,
	.write		= mcp23s17_write,
	.read_regs	= mcp23s17_read_regs,
};


/*----------------------------------------------------------------------*/

static int mcp23s08_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct mcp23s08	*mcp = container_of(chip, struct mcp23s08, chip);
	int status;

	mutex_lock(&mcp->lock);
	mcp->cache[MCP_IODIR] |= (1 << offset);
	status = mcp->ops->write(mcp, MCP_IODIR, mcp->cache[MCP_IODIR]);
	mutex_unlock(&mcp->lock);
	return status;
}

static int mcp23s08_get(struct gpio_chip *chip, unsigned offset)
{
	struct mcp23s08	*mcp = container_of(chip, struct mcp23s08, chip);
	int status;

	mutex_lock(&mcp->lock);

	/* REVISIT reading this clears any IRQ ... */
	status = mcp->ops->read(mcp, MCP_GPIO);
	if (status < 0)
		status = 0;
	else {
		mcp->cache[MCP_GPIO] = status;
		status = !!(status & (1 << offset));
	}
	mutex_unlock(&mcp->lock);
	return status;
}

static int __mcp23s08_set(struct mcp23s08 *mcp, unsigned mask, int value)
{
	unsigned olat = mcp->cache[MCP_OLAT];

	if (value)
		olat |= mask;
	else
		olat &= ~mask;
	mcp->cache[MCP_OLAT] = olat;
	return mcp->ops->write(mcp, MCP_OLAT, olat);
}

static void mcp23s08_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcp23s08	*mcp = container_of(chip, struct mcp23s08, chip);
	unsigned mask = 1 << offset;

	mutex_lock(&mcp->lock);
	__mcp23s08_set(mcp, mask, value);
	mutex_unlock(&mcp->lock);
}

static int
mcp23s08_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcp23s08	*mcp = container_of(chip, struct mcp23s08, chip);
	unsigned mask = 1 << offset;
	int status;

	mutex_lock(&mcp->lock);
	status = __mcp23s08_set(mcp, mask, value);
	if (status == 0) {
		mcp->cache[MCP_IODIR] &= ~mask;
		status = mcp->ops->write(mcp, MCP_IODIR, mcp->cache[MCP_IODIR]);
	}
	mutex_unlock(&mcp->lock);
	return status;
}

/*----------------------------------------------------------------------*/

#ifdef CONFIG_DEBUG_FS

#include <linux/seq_file.h>

/*
 * This shows more info than the generic gpio dump code:
 * pullups, deglitching, open drain drive.
 */
static void mcp23s08_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct mcp23s08	*mcp;
	char		bank;
	int		t;
	unsigned	mask;

	mcp = container_of(chip, struct mcp23s08, chip);

	/* NOTE: we only handle one bank for now ... */
	bank = '0' + ((mcp->addr >> 1) & 0x7);

	mutex_lock(&mcp->lock);
	t = mcp->ops->read_regs(mcp, 0, mcp->cache, ARRAY_SIZE(mcp->cache));
	if (t < 0) {
		seq_printf(s, " I/O ERROR %d\n", t);
		goto done;
	}

	for (t = 0, mask = 1; t < chip->ngpio; t++, mask <<= 1) {
		const char	*label;

		label = gpiochip_is_requested(chip, t);
		if (!label)
			continue;

		seq_printf(s, " gpio-%-3d P%c.%d (%-12s) %s %s %s",
			chip->base + t, bank, t, label,
			(mcp->cache[MCP_IODIR] & mask) ? "in " : "out",
			(mcp->cache[MCP_GPIO] & mask) ? "hi" : "lo",
			(mcp->cache[MCP_GPPU] & mask) ? "  " : "up");
		/* NOTE:  ignoring the irq-related registers */
		seq_printf(s, "\n");
	}
done:
	mutex_unlock(&mcp->lock);
}

#else
#define mcp23s08_dbg_show	NULL
#endif

/*----------------------------------------------------------------------*/

static int mcp23s08_probe_one(struct spi_device *spi, unsigned addr,
			      unsigned type, unsigned base, unsigned pullups)
{
	struct mcp23s08_driver_data	*data = spi_get_drvdata(spi);
	struct mcp23s08			*mcp = data->mcp[addr];
	int				status;

	mutex_init(&mcp->lock);

	mcp->spi = spi;
	mcp->addr = 0x40 | (addr << 1);

	mcp->chip.direction_input = mcp23s08_direction_input;
	mcp->chip.get = mcp23s08_get;
	mcp->chip.direction_output = mcp23s08_direction_output;
	mcp->chip.set = mcp23s08_set;
	mcp->chip.dbg_show = mcp23s08_dbg_show;

	if (type == MCP_TYPE_S17) {
		mcp->ops = &mcp23s17_ops;
		mcp->chip.ngpio = 16;
		mcp->chip.label = "mcp23s17";
	} else {
		mcp->ops = &mcp23s08_ops;
		mcp->chip.ngpio = 8;
		mcp->chip.label = "mcp23s08";
	}
	mcp->chip.base = base;
	mcp->chip.can_sleep = 1;
	mcp->chip.dev = &spi->dev;
	mcp->chip.owner = THIS_MODULE;

	/* verify MCP_IOCON.SEQOP = 0, so sequential reads work,
	 * and MCP_IOCON.HAEN = 1, so we work with all chips.
	 */
	status = mcp->ops->read(mcp, MCP_IOCON);
	if (status < 0)
		goto fail;
	if ((status & IOCON_SEQOP) || !(status & IOCON_HAEN)) {
		/* mcp23s17 has IOCON twice, make sure they are in sync */
		status &= ~(IOCON_SEQOP | (IOCON_SEQOP << 8));
		status |= IOCON_HAEN | (IOCON_HAEN << 8);
		status = mcp->ops->write(mcp, MCP_IOCON, status);
		if (status < 0)
			goto fail;
	}

	/* configure ~100K pullups */
	status = mcp->ops->write(mcp, MCP_GPPU, pullups);
	if (status < 0)
		goto fail;

	status = mcp->ops->read_regs(mcp, 0, mcp->cache, ARRAY_SIZE(mcp->cache));
	if (status < 0)
		goto fail;

	/* disable inverter on input */
	if (mcp->cache[MCP_IPOL] != 0) {
		mcp->cache[MCP_IPOL] = 0;
		status = mcp->ops->write(mcp, MCP_IPOL, 0);
		if (status < 0)
			goto fail;
	}

	/* disable irqs */
	if (mcp->cache[MCP_GPINTEN] != 0) {
		mcp->cache[MCP_GPINTEN] = 0;
		status = mcp->ops->write(mcp, MCP_GPINTEN, 0);
		if (status < 0)
			goto fail;
	}

	status = gpiochip_add(&mcp->chip);
fail:
	if (status < 0)
		dev_dbg(&spi->dev, "can't setup chip %d, --> %d\n",
				addr, status);
	return status;
}

static int mcp23s08_probe(struct spi_device *spi)
{
	struct mcp23s08_platform_data	*pdata;
	unsigned			addr;
	unsigned			chips = 0;
	struct mcp23s08_driver_data	*data;
	int				status, type;
	unsigned			base;

	type = spi_get_device_id(spi)->driver_data;

	pdata = spi->dev.platform_data;
	if (!pdata || !gpio_is_valid(pdata->base)) {
		dev_dbg(&spi->dev, "invalid or missing platform data\n");
		return -EINVAL;
	}

	for (addr = 0; addr < ARRAY_SIZE(pdata->chip); addr++) {
		if (!pdata->chip[addr].is_present)
			continue;
		chips++;
		if ((type == MCP_TYPE_S08) && (addr > 3)) {
			dev_err(&spi->dev,
				"mcp23s08 only supports address 0..3\n");
			return -EINVAL;
		}
	}
	if (!chips)
		return -ENODEV;

	data = kzalloc(sizeof *data + chips * sizeof(struct mcp23s08),
			GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	spi_set_drvdata(spi, data);

	base = pdata->base;
	for (addr = 0; addr < ARRAY_SIZE(pdata->chip); addr++) {
		if (!pdata->chip[addr].is_present)
			continue;
		chips--;
		data->mcp[addr] = &data->chip[chips];
		status = mcp23s08_probe_one(spi, addr, type, base,
					    pdata->chip[addr].pullups);
		if (status < 0)
			goto fail;

		base += (type == MCP_TYPE_S17) ? 16 : 8;
	}
	data->ngpio = base - pdata->base;

	/* NOTE:  these chips have a relatively sane IRQ framework, with
	 * per-signal masking and level/edge triggering.  It's not yet
	 * handled here...
	 */

	if (pdata->setup) {
		status = pdata->setup(spi,
				pdata->base, data->ngpio,
				pdata->context);
		if (status < 0)
			dev_dbg(&spi->dev, "setup --> %d\n", status);
	}

	return 0;

fail:
	for (addr = 0; addr < ARRAY_SIZE(data->mcp); addr++) {
		int tmp;

		if (!data->mcp[addr])
			continue;
		tmp = gpiochip_remove(&data->mcp[addr]->chip);
		if (tmp < 0)
			dev_err(&spi->dev, "%s --> %d\n", "remove", tmp);
	}
	kfree(data);
	return status;
}

static int mcp23s08_remove(struct spi_device *spi)
{
	struct mcp23s08_driver_data	*data = spi_get_drvdata(spi);
	struct mcp23s08_platform_data	*pdata = spi->dev.platform_data;
	unsigned			addr;
	int				status = 0;

	if (pdata->teardown) {
		status = pdata->teardown(spi,
				pdata->base, data->ngpio,
				pdata->context);
		if (status < 0) {
			dev_err(&spi->dev, "%s --> %d\n", "teardown", status);
			return status;
		}
	}

	for (addr = 0; addr < ARRAY_SIZE(data->mcp); addr++) {
		int tmp;

		if (!data->mcp[addr])
			continue;

		tmp = gpiochip_remove(&data->mcp[addr]->chip);
		if (tmp < 0) {
			dev_err(&spi->dev, "%s --> %d\n", "remove", tmp);
			status = tmp;
		}
	}
	if (status == 0)
		kfree(data);
	return status;
}

static const struct spi_device_id mcp23s08_ids[] = {
	{ "mcp23s08", MCP_TYPE_S08 },
	{ "mcp23s17", MCP_TYPE_S17 },
	{ },
};
MODULE_DEVICE_TABLE(spi, mcp23s08_ids);

static struct spi_driver mcp23s08_driver = {
	.probe		= mcp23s08_probe,
	.remove		= mcp23s08_remove,
	.id_table	= mcp23s08_ids,
	.driver = {
		.name	= "mcp23s08",
		.owner	= THIS_MODULE,
	},
};

/*----------------------------------------------------------------------*/

static int __init mcp23s08_init(void)
{
	return spi_register_driver(&mcp23s08_driver);
}
/* register after spi postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(mcp23s08_init);

static void __exit mcp23s08_exit(void)
{
	spi_unregister_driver(&mcp23s08_driver);
}
module_exit(mcp23s08_exit);

MODULE_LICENSE("GPL");
