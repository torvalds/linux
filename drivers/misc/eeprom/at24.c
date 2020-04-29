// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * at24.c - handle most I2C EEPROMs
 *
 * Copyright (C) 2005-2007 David Brownell
 * Copyright (C) 2008 Wolfram Sang, Pengutronix
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

/* Address pointer is 16 bit. */
#define AT24_FLAG_ADDR16	BIT(7)
/* sysfs-entry will be read-only. */
#define AT24_FLAG_READONLY	BIT(6)
/* sysfs-entry will be world-readable. */
#define AT24_FLAG_IRUGO		BIT(5)
/* Take always 8 addresses (24c00). */
#define AT24_FLAG_TAKE8ADDR	BIT(4)
/* Factory-programmed serial number. */
#define AT24_FLAG_SERIAL	BIT(3)
/* Factory-programmed mac address. */
#define AT24_FLAG_MAC		BIT(2)
/* Does not auto-rollover reads to the next slave address. */
#define AT24_FLAG_NO_RDROL	BIT(1)

/*
 * I2C EEPROMs from most vendors are inexpensive and mostly interchangeable.
 * Differences between different vendor product lines (like Atmel AT24C or
 * MicroChip 24LC, etc) won't much matter for typical read/write access.
 * There are also I2C RAM chips, likewise interchangeable. One example
 * would be the PCF8570, which acts like a 24c02 EEPROM (256 bytes).
 *
 * However, misconfiguration can lose data. "Set 16-bit memory address"
 * to a part with 8-bit addressing will overwrite data. Writing with too
 * big a page size also loses data. And it's not safe to assume that the
 * conventional addresses 0x50..0x57 only hold eeproms; a PCF8563 RTC
 * uses 0x51, for just one example.
 *
 * Accordingly, explicit board-specific configuration data should be used
 * in almost all cases. (One partial exception is an SMBus used to access
 * "SPD" data for DRAM sticks. Those only use 24c02 EEPROMs.)
 *
 * So this driver uses "new style" I2C driver binding, expecting to be
 * told what devices exist. That may be in arch/X/mach-Y/board-Z.c or
 * similar kernel-resident tables; or, configuration data coming from
 * a bootloader.
 *
 * Other than binding model, current differences from "eeprom" driver are
 * that this one handles write access and isn't restricted to 24c02 devices.
 * It also handles larger devices (32 kbit and up) with two-byte addresses,
 * which won't work on pure SMBus systems.
 */

struct at24_client {
	struct i2c_client *client;
	struct regmap *regmap;
};

struct at24_data {
	/*
	 * Lock protects against activities from other Linux tasks,
	 * but not from changes by other I2C masters.
	 */
	struct mutex lock;

	unsigned int write_max;
	unsigned int num_addresses;
	unsigned int offset_adj;

	u32 byte_len;
	u16 page_size;
	u8 flags;

	struct nvmem_device *nvmem;
	struct regulator *vcc_reg;

	/*
	 * Some chips tie up multiple I2C addresses; dummy devices reserve
	 * them for us, and we'll use them with SMBus calls.
	 */
	struct at24_client client[];
};

/*
 * This parameter is to help this driver avoid blocking other drivers out
 * of I2C for potentially troublesome amounts of time. With a 100 kHz I2C
 * clock, one 256 byte read takes about 1/43 second which is excessive;
 * but the 1/170 second it takes at 400 kHz may be quite reasonable; and
 * at 1 MHz (Fm+) a 1/430 second delay could easily be invisible.
 *
 * This value is forced to be a power of two so that writes align on pages.
 */
static unsigned int at24_io_limit = 128;
module_param_named(io_limit, at24_io_limit, uint, 0);
MODULE_PARM_DESC(at24_io_limit, "Maximum bytes per I/O (default 128)");

/*
 * Specs often allow 5 msec for a page write, sometimes 20 msec;
 * it's important to recover from write timeouts.
 */
static unsigned int at24_write_timeout = 25;
module_param_named(write_timeout, at24_write_timeout, uint, 0);
MODULE_PARM_DESC(at24_write_timeout, "Time (in ms) to try writes (default 25)");

struct at24_chip_data {
	u32 byte_len;
	u8 flags;
};

#define AT24_CHIP_DATA(_name, _len, _flags)				\
	static const struct at24_chip_data _name = {			\
		.byte_len = _len, .flags = _flags,			\
	}

/* needs 8 addresses as A0-A2 are ignored */
AT24_CHIP_DATA(at24_data_24c00, 128 / 8, AT24_FLAG_TAKE8ADDR);
/* old variants can't be handled with this generic entry! */
AT24_CHIP_DATA(at24_data_24c01, 1024 / 8, 0);
AT24_CHIP_DATA(at24_data_24cs01, 16,
	AT24_FLAG_SERIAL | AT24_FLAG_READONLY);
AT24_CHIP_DATA(at24_data_24c02, 2048 / 8, 0);
AT24_CHIP_DATA(at24_data_24cs02, 16,
	AT24_FLAG_SERIAL | AT24_FLAG_READONLY);
AT24_CHIP_DATA(at24_data_24mac402, 48 / 8,
	AT24_FLAG_MAC | AT24_FLAG_READONLY);
AT24_CHIP_DATA(at24_data_24mac602, 64 / 8,
	AT24_FLAG_MAC | AT24_FLAG_READONLY);
/* spd is a 24c02 in memory DIMMs */
AT24_CHIP_DATA(at24_data_spd, 2048 / 8,
	AT24_FLAG_READONLY | AT24_FLAG_IRUGO);
AT24_CHIP_DATA(at24_data_24c04, 4096 / 8, 0);
AT24_CHIP_DATA(at24_data_24cs04, 16,
	AT24_FLAG_SERIAL | AT24_FLAG_READONLY);
/* 24rf08 quirk is handled at i2c-core */
AT24_CHIP_DATA(at24_data_24c08, 8192 / 8, 0);
AT24_CHIP_DATA(at24_data_24cs08, 16,
	AT24_FLAG_SERIAL | AT24_FLAG_READONLY);
AT24_CHIP_DATA(at24_data_24c16, 16384 / 8, 0);
AT24_CHIP_DATA(at24_data_24cs16, 16,
	AT24_FLAG_SERIAL | AT24_FLAG_READONLY);
AT24_CHIP_DATA(at24_data_24c32, 32768 / 8, AT24_FLAG_ADDR16);
AT24_CHIP_DATA(at24_data_24cs32, 16,
	AT24_FLAG_ADDR16 | AT24_FLAG_SERIAL | AT24_FLAG_READONLY);
AT24_CHIP_DATA(at24_data_24c64, 65536 / 8, AT24_FLAG_ADDR16);
AT24_CHIP_DATA(at24_data_24cs64, 16,
	AT24_FLAG_ADDR16 | AT24_FLAG_SERIAL | AT24_FLAG_READONLY);
AT24_CHIP_DATA(at24_data_24c128, 131072 / 8, AT24_FLAG_ADDR16);
AT24_CHIP_DATA(at24_data_24c256, 262144 / 8, AT24_FLAG_ADDR16);
AT24_CHIP_DATA(at24_data_24c512, 524288 / 8, AT24_FLAG_ADDR16);
AT24_CHIP_DATA(at24_data_24c1024, 1048576 / 8, AT24_FLAG_ADDR16);
AT24_CHIP_DATA(at24_data_24c2048, 2097152 / 8, AT24_FLAG_ADDR16);
/* identical to 24c08 ? */
AT24_CHIP_DATA(at24_data_INT3499, 8192 / 8, 0);

static const struct i2c_device_id at24_ids[] = {
	{ "24c00",	(kernel_ulong_t)&at24_data_24c00 },
	{ "24c01",	(kernel_ulong_t)&at24_data_24c01 },
	{ "24cs01",	(kernel_ulong_t)&at24_data_24cs01 },
	{ "24c02",	(kernel_ulong_t)&at24_data_24c02 },
	{ "24cs02",	(kernel_ulong_t)&at24_data_24cs02 },
	{ "24mac402",	(kernel_ulong_t)&at24_data_24mac402 },
	{ "24mac602",	(kernel_ulong_t)&at24_data_24mac602 },
	{ "spd",	(kernel_ulong_t)&at24_data_spd },
	{ "24c04",	(kernel_ulong_t)&at24_data_24c04 },
	{ "24cs04",	(kernel_ulong_t)&at24_data_24cs04 },
	{ "24c08",	(kernel_ulong_t)&at24_data_24c08 },
	{ "24cs08",	(kernel_ulong_t)&at24_data_24cs08 },
	{ "24c16",	(kernel_ulong_t)&at24_data_24c16 },
	{ "24cs16",	(kernel_ulong_t)&at24_data_24cs16 },
	{ "24c32",	(kernel_ulong_t)&at24_data_24c32 },
	{ "24cs32",	(kernel_ulong_t)&at24_data_24cs32 },
	{ "24c64",	(kernel_ulong_t)&at24_data_24c64 },
	{ "24cs64",	(kernel_ulong_t)&at24_data_24cs64 },
	{ "24c128",	(kernel_ulong_t)&at24_data_24c128 },
	{ "24c256",	(kernel_ulong_t)&at24_data_24c256 },
	{ "24c512",	(kernel_ulong_t)&at24_data_24c512 },
	{ "24c1024",	(kernel_ulong_t)&at24_data_24c1024 },
	{ "24c2048",    (kernel_ulong_t)&at24_data_24c2048 },
	{ "at24",	0 },
	{ /* END OF LIST */ }
};
MODULE_DEVICE_TABLE(i2c, at24_ids);

static const struct of_device_id at24_of_match[] = {
	{ .compatible = "atmel,24c00",		.data = &at24_data_24c00 },
	{ .compatible = "atmel,24c01",		.data = &at24_data_24c01 },
	{ .compatible = "atmel,24cs01",		.data = &at24_data_24cs01 },
	{ .compatible = "atmel,24c02",		.data = &at24_data_24c02 },
	{ .compatible = "atmel,24cs02",		.data = &at24_data_24cs02 },
	{ .compatible = "atmel,24mac402",	.data = &at24_data_24mac402 },
	{ .compatible = "atmel,24mac602",	.data = &at24_data_24mac602 },
	{ .compatible = "atmel,spd",		.data = &at24_data_spd },
	{ .compatible = "atmel,24c04",		.data = &at24_data_24c04 },
	{ .compatible = "atmel,24cs04",		.data = &at24_data_24cs04 },
	{ .compatible = "atmel,24c08",		.data = &at24_data_24c08 },
	{ .compatible = "atmel,24cs08",		.data = &at24_data_24cs08 },
	{ .compatible = "atmel,24c16",		.data = &at24_data_24c16 },
	{ .compatible = "atmel,24cs16",		.data = &at24_data_24cs16 },
	{ .compatible = "atmel,24c32",		.data = &at24_data_24c32 },
	{ .compatible = "atmel,24cs32",		.data = &at24_data_24cs32 },
	{ .compatible = "atmel,24c64",		.data = &at24_data_24c64 },
	{ .compatible = "atmel,24cs64",		.data = &at24_data_24cs64 },
	{ .compatible = "atmel,24c128",		.data = &at24_data_24c128 },
	{ .compatible = "atmel,24c256",		.data = &at24_data_24c256 },
	{ .compatible = "atmel,24c512",		.data = &at24_data_24c512 },
	{ .compatible = "atmel,24c1024",	.data = &at24_data_24c1024 },
	{ .compatible = "atmel,24c2048",	.data = &at24_data_24c2048 },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(of, at24_of_match);

static const struct acpi_device_id at24_acpi_ids[] = {
	{ "INT3499",	(kernel_ulong_t)&at24_data_INT3499 },
	{ "TPF0001",	(kernel_ulong_t)&at24_data_24c1024 },
	{ /* END OF LIST */ }
};
MODULE_DEVICE_TABLE(acpi, at24_acpi_ids);

/*
 * This routine supports chips which consume multiple I2C addresses. It
 * computes the addressing information to be used for a given r/w request.
 * Assumes that sanity checks for offset happened at sysfs-layer.
 *
 * Slave address and byte offset derive from the offset. Always
 * set the byte address; on a multi-master board, another master
 * may have changed the chip's "current" address pointer.
 */
static struct at24_client *at24_translate_offset(struct at24_data *at24,
						 unsigned int *offset)
{
	unsigned int i;

	if (at24->flags & AT24_FLAG_ADDR16) {
		i = *offset >> 16;
		*offset &= 0xffff;
	} else {
		i = *offset >> 8;
		*offset &= 0xff;
	}

	return &at24->client[i];
}

static struct device *at24_base_client_dev(struct at24_data *at24)
{
	return &at24->client[0].client->dev;
}

static size_t at24_adjust_read_count(struct at24_data *at24,
				      unsigned int offset, size_t count)
{
	unsigned int bits;
	size_t remainder;

	/*
	 * In case of multi-address chips that don't rollover reads to
	 * the next slave address: truncate the count to the slave boundary,
	 * so that the read never straddles slaves.
	 */
	if (at24->flags & AT24_FLAG_NO_RDROL) {
		bits = (at24->flags & AT24_FLAG_ADDR16) ? 16 : 8;
		remainder = BIT(bits) - offset;
		if (count > remainder)
			count = remainder;
	}

	if (count > at24_io_limit)
		count = at24_io_limit;

	return count;
}

static ssize_t at24_regmap_read(struct at24_data *at24, char *buf,
				unsigned int offset, size_t count)
{
	unsigned long timeout, read_time;
	struct at24_client *at24_client;
	struct i2c_client *client;
	struct regmap *regmap;
	int ret;

	at24_client = at24_translate_offset(at24, &offset);
	regmap = at24_client->regmap;
	client = at24_client->client;
	count = at24_adjust_read_count(at24, offset, count);

	/* adjust offset for mac and serial read ops */
	offset += at24->offset_adj;

	timeout = jiffies + msecs_to_jiffies(at24_write_timeout);
	do {
		/*
		 * The timestamp shall be taken before the actual operation
		 * to avoid a premature timeout in case of high CPU load.
		 */
		read_time = jiffies;

		ret = regmap_bulk_read(regmap, offset, buf, count);
		dev_dbg(&client->dev, "read %zu@%d --> %d (%ld)\n",
			count, offset, ret, jiffies);
		if (!ret)
			return count;

		usleep_range(1000, 1500);
	} while (time_before(read_time, timeout));

	return -ETIMEDOUT;
}

/*
 * Note that if the hardware write-protect pin is pulled high, the whole
 * chip is normally write protected. But there are plenty of product
 * variants here, including OTP fuses and partial chip protect.
 *
 * We only use page mode writes; the alternative is sloooow. These routines
 * write at most one page.
 */

static size_t at24_adjust_write_count(struct at24_data *at24,
				      unsigned int offset, size_t count)
{
	unsigned int next_page;

	/* write_max is at most a page */
	if (count > at24->write_max)
		count = at24->write_max;

	/* Never roll over backwards, to the start of this page */
	next_page = roundup(offset + 1, at24->page_size);
	if (offset + count > next_page)
		count = next_page - offset;

	return count;
}

static ssize_t at24_regmap_write(struct at24_data *at24, const char *buf,
				 unsigned int offset, size_t count)
{
	unsigned long timeout, write_time;
	struct at24_client *at24_client;
	struct i2c_client *client;
	struct regmap *regmap;
	int ret;

	at24_client = at24_translate_offset(at24, &offset);
	regmap = at24_client->regmap;
	client = at24_client->client;
	count = at24_adjust_write_count(at24, offset, count);
	timeout = jiffies + msecs_to_jiffies(at24_write_timeout);

	do {
		/*
		 * The timestamp shall be taken before the actual operation
		 * to avoid a premature timeout in case of high CPU load.
		 */
		write_time = jiffies;

		ret = regmap_bulk_write(regmap, offset, buf, count);
		dev_dbg(&client->dev, "write %zu@%d --> %d (%ld)\n",
			count, offset, ret, jiffies);
		if (!ret)
			return count;

		usleep_range(1000, 1500);
	} while (time_before(write_time, timeout));

	return -ETIMEDOUT;
}

static int at24_read(void *priv, unsigned int off, void *val, size_t count)
{
	struct at24_data *at24;
	struct device *dev;
	char *buf = val;
	int ret;

	at24 = priv;
	dev = at24_base_client_dev(at24);

	if (unlikely(!count))
		return count;

	if (off + count > at24->byte_len)
		return -EINVAL;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	/*
	 * Read data from chip, protecting against concurrent updates
	 * from this host, but not from other I2C masters.
	 */
	mutex_lock(&at24->lock);

	while (count) {
		ret = at24_regmap_read(at24, buf, off, count);
		if (ret < 0) {
			mutex_unlock(&at24->lock);
			pm_runtime_put(dev);
			return ret;
		}
		buf += ret;
		off += ret;
		count -= ret;
	}

	mutex_unlock(&at24->lock);

	pm_runtime_put(dev);

	return 0;
}

static int at24_write(void *priv, unsigned int off, void *val, size_t count)
{
	struct at24_data *at24;
	struct device *dev;
	char *buf = val;
	int ret;

	at24 = priv;
	dev = at24_base_client_dev(at24);

	if (unlikely(!count))
		return -EINVAL;

	if (off + count > at24->byte_len)
		return -EINVAL;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	/*
	 * Write data to chip, protecting against concurrent updates
	 * from this host, but not from other I2C masters.
	 */
	mutex_lock(&at24->lock);

	while (count) {
		ret = at24_regmap_write(at24, buf, off, count);
		if (ret < 0) {
			mutex_unlock(&at24->lock);
			pm_runtime_put(dev);
			return ret;
		}
		buf += ret;
		off += ret;
		count -= ret;
	}

	mutex_unlock(&at24->lock);

	pm_runtime_put(dev);

	return 0;
}

static const struct at24_chip_data *at24_get_chip_data(struct device *dev)
{
	struct device_node *of_node = dev->of_node;
	const struct at24_chip_data *cdata;
	const struct i2c_device_id *id;

	id = i2c_match_id(at24_ids, to_i2c_client(dev));

	/*
	 * The I2C core allows OF nodes compatibles to match against the
	 * I2C device ID table as a fallback, so check not only if an OF
	 * node is present but also if it matches an OF device ID entry.
	 */
	if (of_node && of_match_device(at24_of_match, dev))
		cdata = of_device_get_match_data(dev);
	else if (id)
		cdata = (void *)id->driver_data;
	else
		cdata = acpi_device_get_match_data(dev);

	if (!cdata)
		return ERR_PTR(-ENODEV);

	return cdata;
}

static int at24_make_dummy_client(struct at24_data *at24, unsigned int index,
				  struct regmap_config *regmap_config)
{
	struct i2c_client *base_client, *dummy_client;
	struct regmap *regmap;
	struct device *dev;

	base_client = at24->client[0].client;
	dev = &base_client->dev;

	dummy_client = devm_i2c_new_dummy_device(dev, base_client->adapter,
						 base_client->addr + index);
	if (IS_ERR(dummy_client))
		return PTR_ERR(dummy_client);

	regmap = devm_regmap_init_i2c(dummy_client, regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	at24->client[index].client = dummy_client;
	at24->client[index].regmap = regmap;

	return 0;
}

static unsigned int at24_get_offset_adj(u8 flags, unsigned int byte_len)
{
	if (flags & AT24_FLAG_MAC) {
		/* EUI-48 starts from 0x9a, EUI-64 from 0x98 */
		return 0xa0 - byte_len;
	} else if (flags & AT24_FLAG_SERIAL && flags & AT24_FLAG_ADDR16) {
		/*
		 * For 16 bit address pointers, the word address must contain
		 * a '10' sequence in bits 11 and 10 regardless of the
		 * intended position of the address pointer.
		 */
		return 0x0800;
	} else if (flags & AT24_FLAG_SERIAL) {
		/*
		 * Otherwise the word address must begin with a '10' sequence,
		 * regardless of the intended address.
		 */
		return 0x0080;
	} else {
		return 0;
	}
}

static int at24_probe(struct i2c_client *client)
{
	struct regmap_config regmap_config = { };
	struct nvmem_config nvmem_config = { };
	u32 byte_len, page_size, flags, addrw;
	const struct at24_chip_data *cdata;
	struct device *dev = &client->dev;
	bool i2c_fn_i2c, i2c_fn_block;
	unsigned int i, num_addresses;
	struct at24_data *at24;
	struct regmap *regmap;
	bool writable;
	u8 test_byte;
	int err;

	i2c_fn_i2c = i2c_check_functionality(client->adapter, I2C_FUNC_I2C);
	i2c_fn_block = i2c_check_functionality(client->adapter,
					       I2C_FUNC_SMBUS_WRITE_I2C_BLOCK);

	cdata = at24_get_chip_data(dev);
	if (IS_ERR(cdata))
		return PTR_ERR(cdata);

	err = device_property_read_u32(dev, "pagesize", &page_size);
	if (err)
		/*
		 * This is slow, but we can't know all eeproms, so we better
		 * play safe. Specifying custom eeprom-types via device tree
		 * or properties is recommended anyhow.
		 */
		page_size = 1;

	flags = cdata->flags;
	if (device_property_present(dev, "read-only"))
		flags |= AT24_FLAG_READONLY;
	if (device_property_present(dev, "no-read-rollover"))
		flags |= AT24_FLAG_NO_RDROL;

	err = device_property_read_u32(dev, "address-width", &addrw);
	if (!err) {
		switch (addrw) {
		case 8:
			if (flags & AT24_FLAG_ADDR16)
				dev_warn(dev,
					 "Override address width to be 8, while default is 16\n");
			flags &= ~AT24_FLAG_ADDR16;
			break;
		case 16:
			flags |= AT24_FLAG_ADDR16;
			break;
		default:
			dev_warn(dev, "Bad \"address-width\" property: %u\n",
				 addrw);
		}
	}

	err = device_property_read_u32(dev, "size", &byte_len);
	if (err)
		byte_len = cdata->byte_len;

	if (!i2c_fn_i2c && !i2c_fn_block)
		page_size = 1;

	if (!page_size) {
		dev_err(dev, "page_size must not be 0!\n");
		return -EINVAL;
	}

	if (!is_power_of_2(page_size))
		dev_warn(dev, "page_size looks suspicious (no power of 2)!\n");

	err = device_property_read_u32(dev, "num-addresses", &num_addresses);
	if (err) {
		if (flags & AT24_FLAG_TAKE8ADDR)
			num_addresses = 8;
		else
			num_addresses =	DIV_ROUND_UP(byte_len,
				(flags & AT24_FLAG_ADDR16) ? 65536 : 256);
	}

	if ((flags & AT24_FLAG_SERIAL) && (flags & AT24_FLAG_MAC)) {
		dev_err(dev,
			"invalid device data - cannot have both AT24_FLAG_SERIAL & AT24_FLAG_MAC.");
		return -EINVAL;
	}

	regmap_config.val_bits = 8;
	regmap_config.reg_bits = (flags & AT24_FLAG_ADDR16) ? 16 : 8;
	regmap_config.disable_locking = true;

	regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	at24 = devm_kzalloc(dev, struct_size(at24, client, num_addresses),
			    GFP_KERNEL);
	if (!at24)
		return -ENOMEM;

	mutex_init(&at24->lock);
	at24->byte_len = byte_len;
	at24->page_size = page_size;
	at24->flags = flags;
	at24->num_addresses = num_addresses;
	at24->offset_adj = at24_get_offset_adj(flags, byte_len);
	at24->client[0].client = client;
	at24->client[0].regmap = regmap;

	at24->vcc_reg = devm_regulator_get(dev, "vcc");
	if (IS_ERR(at24->vcc_reg))
		return PTR_ERR(at24->vcc_reg);

	writable = !(flags & AT24_FLAG_READONLY);
	if (writable) {
		at24->write_max = min_t(unsigned int,
					page_size, at24_io_limit);
		if (!i2c_fn_i2c && at24->write_max > I2C_SMBUS_BLOCK_MAX)
			at24->write_max = I2C_SMBUS_BLOCK_MAX;
	}

	/* use dummy devices for multiple-address chips */
	for (i = 1; i < num_addresses; i++) {
		err = at24_make_dummy_client(at24, i, &regmap_config);
		if (err)
			return err;
	}

	nvmem_config.name = dev_name(dev);
	nvmem_config.dev = dev;
	nvmem_config.read_only = !writable;
	nvmem_config.root_only = !(flags & AT24_FLAG_IRUGO);
	nvmem_config.owner = THIS_MODULE;
	nvmem_config.compat = true;
	nvmem_config.base_dev = dev;
	nvmem_config.reg_read = at24_read;
	nvmem_config.reg_write = at24_write;
	nvmem_config.priv = at24;
	nvmem_config.stride = 1;
	nvmem_config.word_size = 1;
	nvmem_config.size = byte_len;

	at24->nvmem = devm_nvmem_register(dev, &nvmem_config);
	if (IS_ERR(at24->nvmem))
		return PTR_ERR(at24->nvmem);

	i2c_set_clientdata(client, at24);

	err = regulator_enable(at24->vcc_reg);
	if (err) {
		dev_err(dev, "Failed to enable vcc regulator\n");
		return err;
	}

	/* enable runtime pm */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	/*
	 * Perform a one-byte test read to verify that the
	 * chip is functional.
	 */
	err = at24_read(at24, 0, &test_byte, 1);
	if (err) {
		pm_runtime_disable(dev);
		regulator_disable(at24->vcc_reg);
		return -ENODEV;
	}

	pm_runtime_idle(dev);

	if (writable)
		dev_info(dev, "%u byte %s EEPROM, writable, %u bytes/write\n",
			 byte_len, client->name, at24->write_max);
	else
		dev_info(dev, "%u byte %s EEPROM, read-only\n",
			 byte_len, client->name);

	return 0;
}

static int at24_remove(struct i2c_client *client)
{
	struct at24_data *at24 = i2c_get_clientdata(client);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		regulator_disable(at24->vcc_reg);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static int __maybe_unused at24_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct at24_data *at24 = i2c_get_clientdata(client);

	return regulator_disable(at24->vcc_reg);
}

static int __maybe_unused at24_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct at24_data *at24 = i2c_get_clientdata(client);

	return regulator_enable(at24->vcc_reg);
}

static const struct dev_pm_ops at24_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(at24_suspend, at24_resume, NULL)
};

static struct i2c_driver at24_driver = {
	.driver = {
		.name = "at24",
		.pm = &at24_pm_ops,
		.of_match_table = at24_of_match,
		.acpi_match_table = ACPI_PTR(at24_acpi_ids),
	},
	.probe_new = at24_probe,
	.remove = at24_remove,
	.id_table = at24_ids,
};

static int __init at24_init(void)
{
	if (!at24_io_limit) {
		pr_err("at24: at24_io_limit must not be 0!\n");
		return -EINVAL;
	}

	at24_io_limit = rounddown_pow_of_two(at24_io_limit);
	return i2c_add_driver(&at24_driver);
}
module_init(at24_init);

static void __exit at24_exit(void)
{
	i2c_del_driver(&at24_driver);
}
module_exit(at24_exit);

MODULE_DESCRIPTION("Driver for most I2C EEPROMs");
MODULE_AUTHOR("David Brownell and Wolfram Sang");
MODULE_LICENSE("GPL");
