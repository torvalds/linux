/*
 * at24.c - handle most I2C EEPROMs
 *
 * Copyright (C) 2005-2007 David Brownell
 * Copyright (C) 2008 Wolfram Sang, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/mod_devicetable.h>
#include <linux/log2.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/property.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_data/at24.h>

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

struct at24_data {
	struct at24_platform_data chip;
	int use_smbus;
	int use_smbus_write;

	ssize_t (*read_func)(struct at24_data *, char *, unsigned int, size_t);
	ssize_t (*write_func)(struct at24_data *,
			      const char *, unsigned int, size_t);

	/*
	 * Lock protects against activities from other Linux tasks,
	 * but not from changes by other I2C masters.
	 */
	struct mutex lock;

	u8 *writebuf;
	unsigned write_max;
	unsigned num_addresses;

	struct nvmem_config nvmem_config;
	struct nvmem_device *nvmem;

	/*
	 * Some chips tie up multiple I2C addresses; dummy devices reserve
	 * them for us, and we'll use them with SMBus calls.
	 */
	struct i2c_client *client[];
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
static unsigned io_limit = 128;
module_param(io_limit, uint, 0);
MODULE_PARM_DESC(io_limit, "Maximum bytes per I/O (default 128)");

/*
 * Specs often allow 5 msec for a page write, sometimes 20 msec;
 * it's important to recover from write timeouts.
 */
static unsigned write_timeout = 25;
module_param(write_timeout, uint, 0);
MODULE_PARM_DESC(write_timeout, "Time (in ms) to try writes (default 25)");

#define AT24_SIZE_BYTELEN 5
#define AT24_SIZE_FLAGS 8

#define AT24_BITMASK(x) (BIT(x) - 1)

/* create non-zero magic value for given eeprom parameters */
#define AT24_DEVICE_MAGIC(_len, _flags) 		\
	((1 << AT24_SIZE_FLAGS | (_flags)) 		\
	    << AT24_SIZE_BYTELEN | ilog2(_len))

/*
 * Both reads and writes fail if the previous write didn't complete yet. This
 * macro loops a few times waiting at least long enough for one entire page
 * write to work while making sure that at least one iteration is run before
 * checking the break condition.
 *
 * It takes two parameters: a variable in which the future timeout in jiffies
 * will be stored and a temporary variable holding the time of the last
 * iteration of processing the request. Both should be unsigned integers
 * holding at least 32 bits.
 */
#define loop_until_timeout(tout, op_time)				\
	for (tout = jiffies + msecs_to_jiffies(write_timeout), op_time = 0; \
	     op_time ? time_before(op_time, tout) : true;		\
	     usleep_range(1000, 1500), op_time = jiffies)

static const struct i2c_device_id at24_ids[] = {
	/* needs 8 addresses as A0-A2 are ignored */
	{ "24c00",	AT24_DEVICE_MAGIC(128 / 8,	AT24_FLAG_TAKE8ADDR) },
	/* old variants can't be handled with this generic entry! */
	{ "24c01",	AT24_DEVICE_MAGIC(1024 / 8,	0) },
	{ "24cs01",	AT24_DEVICE_MAGIC(16,
				AT24_FLAG_SERIAL | AT24_FLAG_READONLY) },
	{ "24c02",	AT24_DEVICE_MAGIC(2048 / 8,	0) },
	{ "24cs02",	AT24_DEVICE_MAGIC(16,
				AT24_FLAG_SERIAL | AT24_FLAG_READONLY) },
	{ "24mac402",	AT24_DEVICE_MAGIC(48 / 8,
				AT24_FLAG_MAC | AT24_FLAG_READONLY) },
	{ "24mac602",	AT24_DEVICE_MAGIC(64 / 8,
				AT24_FLAG_MAC | AT24_FLAG_READONLY) },
	/* spd is a 24c02 in memory DIMMs */
	{ "spd",	AT24_DEVICE_MAGIC(2048 / 8,
				AT24_FLAG_READONLY | AT24_FLAG_IRUGO) },
	{ "24c04",	AT24_DEVICE_MAGIC(4096 / 8,	0) },
	{ "24cs04",	AT24_DEVICE_MAGIC(16,
				AT24_FLAG_SERIAL | AT24_FLAG_READONLY) },
	/* 24rf08 quirk is handled at i2c-core */
	{ "24c08",	AT24_DEVICE_MAGIC(8192 / 8,	0) },
	{ "24cs08",	AT24_DEVICE_MAGIC(16,
				AT24_FLAG_SERIAL | AT24_FLAG_READONLY) },
	{ "24c16",	AT24_DEVICE_MAGIC(16384 / 8,	0) },
	{ "24cs16",	AT24_DEVICE_MAGIC(16,
				AT24_FLAG_SERIAL | AT24_FLAG_READONLY) },
	{ "24c32",	AT24_DEVICE_MAGIC(32768 / 8,	AT24_FLAG_ADDR16) },
	{ "24cs32",	AT24_DEVICE_MAGIC(16,
				AT24_FLAG_ADDR16 |
				AT24_FLAG_SERIAL |
				AT24_FLAG_READONLY) },
	{ "24c64",	AT24_DEVICE_MAGIC(65536 / 8,	AT24_FLAG_ADDR16) },
	{ "24cs64",	AT24_DEVICE_MAGIC(16,
				AT24_FLAG_ADDR16 |
				AT24_FLAG_SERIAL |
				AT24_FLAG_READONLY) },
	{ "24c128",	AT24_DEVICE_MAGIC(131072 / 8,	AT24_FLAG_ADDR16) },
	{ "24c256",	AT24_DEVICE_MAGIC(262144 / 8,	AT24_FLAG_ADDR16) },
	{ "24c512",	AT24_DEVICE_MAGIC(524288 / 8,	AT24_FLAG_ADDR16) },
	{ "24c1024",	AT24_DEVICE_MAGIC(1048576 / 8,	AT24_FLAG_ADDR16) },
	{ "at24", 0 },
	{ /* END OF LIST */ }
};
MODULE_DEVICE_TABLE(i2c, at24_ids);

static const struct acpi_device_id at24_acpi_ids[] = {
	{ "INT3499", AT24_DEVICE_MAGIC(8192 / 8, 0) },
	{ }
};
MODULE_DEVICE_TABLE(acpi, at24_acpi_ids);

/*-------------------------------------------------------------------------*/

/*
 * This routine supports chips which consume multiple I2C addresses. It
 * computes the addressing information to be used for a given r/w request.
 * Assumes that sanity checks for offset happened at sysfs-layer.
 *
 * Slave address and byte offset derive from the offset. Always
 * set the byte address; on a multi-master board, another master
 * may have changed the chip's "current" address pointer.
 *
 * REVISIT some multi-address chips don't rollover page reads to
 * the next slave address, so we may need to truncate the count.
 * Those chips might need another quirk flag.
 *
 * If the real hardware used four adjacent 24c02 chips and that
 * were misconfigured as one 24c08, that would be a similar effect:
 * one "eeprom" file not four, but larger reads would fail when
 * they crossed certain pages.
 */
static struct i2c_client *at24_translate_offset(struct at24_data *at24,
						unsigned int *offset)
{
	unsigned i;

	if (at24->chip.flags & AT24_FLAG_ADDR16) {
		i = *offset >> 16;
		*offset &= 0xffff;
	} else {
		i = *offset >> 8;
		*offset &= 0xff;
	}

	return at24->client[i];
}

static ssize_t at24_eeprom_read_smbus(struct at24_data *at24, char *buf,
				      unsigned int offset, size_t count)
{
	unsigned long timeout, read_time;
	struct i2c_client *client;
	int status;

	client = at24_translate_offset(at24, &offset);

	if (count > io_limit)
		count = io_limit;

	/* Smaller eeproms can work given some SMBus extension calls */
	if (count > I2C_SMBUS_BLOCK_MAX)
		count = I2C_SMBUS_BLOCK_MAX;

	loop_until_timeout(timeout, read_time) {
		status = i2c_smbus_read_i2c_block_data_or_emulated(client,
								   offset,
								   count, buf);

		dev_dbg(&client->dev, "read %zu@%d --> %d (%ld)\n",
				count, offset, status, jiffies);

		if (status == count)
			return count;
	}

	return -ETIMEDOUT;
}

static ssize_t at24_eeprom_read_i2c(struct at24_data *at24, char *buf,
				    unsigned int offset, size_t count)
{
	unsigned long timeout, read_time;
	struct i2c_client *client;
	struct i2c_msg msg[2];
	int status, i;
	u8 msgbuf[2];

	memset(msg, 0, sizeof(msg));
	client = at24_translate_offset(at24, &offset);

	if (count > io_limit)
		count = io_limit;

	/*
	 * When we have a better choice than SMBus calls, use a combined I2C
	 * message. Write address; then read up to io_limit data bytes. Note
	 * that read page rollover helps us here (unlike writes). msgbuf is
	 * u8 and will cast to our needs.
	 */
	i = 0;
	if (at24->chip.flags & AT24_FLAG_ADDR16)
		msgbuf[i++] = offset >> 8;
	msgbuf[i++] = offset;

	msg[0].addr = client->addr;
	msg[0].buf = msgbuf;
	msg[0].len = i;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = count;

	loop_until_timeout(timeout, read_time) {
		status = i2c_transfer(client->adapter, msg, 2);
		if (status == 2)
			status = count;

		dev_dbg(&client->dev, "read %zu@%d --> %d (%ld)\n",
				count, offset, status, jiffies);

		if (status == count)
			return count;
	}

	return -ETIMEDOUT;
}

static ssize_t at24_eeprom_read_serial(struct at24_data *at24, char *buf,
				       unsigned int offset, size_t count)
{
	unsigned long timeout, read_time;
	struct i2c_client *client;
	struct i2c_msg msg[2];
	u8 addrbuf[2];
	int status;

	client = at24_translate_offset(at24, &offset);

	memset(msg, 0, sizeof(msg));
	msg[0].addr = client->addr;
	msg[0].buf = addrbuf;

	/*
	 * The address pointer of the device is shared between the regular
	 * EEPROM array and the serial number block. The dummy write (part of
	 * the sequential read protocol) ensures the address pointer is reset
	 * to the desired position.
	 */
	if (at24->chip.flags & AT24_FLAG_ADDR16) {
		/*
		 * For 16 bit address pointers, the word address must contain
		 * a '10' sequence in bits 11 and 10 regardless of the
		 * intended position of the address pointer.
		 */
		addrbuf[0] = 0x08;
		addrbuf[1] = offset;
		msg[0].len = 2;
	} else {
		/*
		 * Otherwise the word address must begin with a '10' sequence,
		 * regardless of the intended address.
		 */
		addrbuf[0] = 0x80 + offset;
		msg[0].len = 1;
	}

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = count;

	loop_until_timeout(timeout, read_time) {
		status = i2c_transfer(client->adapter, msg, 2);
		if (status == 2)
			return count;
	}

	return -ETIMEDOUT;
}

static ssize_t at24_eeprom_read_mac(struct at24_data *at24, char *buf,
				    unsigned int offset, size_t count)
{
	unsigned long timeout, read_time;
	struct i2c_client *client;
	struct i2c_msg msg[2];
	u8 addrbuf[2];
	int status;

	client = at24_translate_offset(at24, &offset);

	memset(msg, 0, sizeof(msg));
	msg[0].addr = client->addr;
	msg[0].buf = addrbuf;
	/* EUI-48 starts from 0x9a, EUI-64 from 0x98 */
	addrbuf[0] = 0xa0 - at24->chip.byte_len + offset;
	msg[0].len = 1;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = count;

	loop_until_timeout(timeout, read_time) {
		status = i2c_transfer(client->adapter, msg, 2);
		if (status == 2)
			return count;
	}

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
	unsigned next_page;

	/* write_max is at most a page */
	if (count > at24->write_max)
		count = at24->write_max;

	/* Never roll over backwards, to the start of this page */
	next_page = roundup(offset + 1, at24->chip.page_size);
	if (offset + count > next_page)
		count = next_page - offset;

	return count;
}

static ssize_t at24_eeprom_write_smbus_block(struct at24_data *at24,
					     const char *buf,
					     unsigned int offset, size_t count)
{
	unsigned long timeout, write_time;
	struct i2c_client *client;
	ssize_t status = 0;

	client = at24_translate_offset(at24, &offset);
	count = at24_adjust_write_count(at24, offset, count);

	loop_until_timeout(timeout, write_time) {
		status = i2c_smbus_write_i2c_block_data(client,
							offset, count, buf);
		if (status == 0)
			status = count;

		dev_dbg(&client->dev, "write %zu@%d --> %zd (%ld)\n",
				count, offset, status, jiffies);

		if (status == count)
			return count;
	}

	return -ETIMEDOUT;
}

static ssize_t at24_eeprom_write_smbus_byte(struct at24_data *at24,
					    const char *buf,
					    unsigned int offset, size_t count)
{
	unsigned long timeout, write_time;
	struct i2c_client *client;
	ssize_t status = 0;

	client = at24_translate_offset(at24, &offset);

	loop_until_timeout(timeout, write_time) {
		status = i2c_smbus_write_byte_data(client, offset, buf[0]);
		if (status == 0)
			status = count;

		dev_dbg(&client->dev, "write %zu@%d --> %zd (%ld)\n",
				count, offset, status, jiffies);

		if (status == count)
			return count;
	}

	return -ETIMEDOUT;
}

static ssize_t at24_eeprom_write_i2c(struct at24_data *at24, const char *buf,
				     unsigned int offset, size_t count)
{
	unsigned long timeout, write_time;
	struct i2c_client *client;
	struct i2c_msg msg;
	ssize_t status = 0;
	int i = 0;

	client = at24_translate_offset(at24, &offset);
	count = at24_adjust_write_count(at24, offset, count);

	msg.addr = client->addr;
	msg.flags = 0;

	/* msg.buf is u8 and casts will mask the values */
	msg.buf = at24->writebuf;
	if (at24->chip.flags & AT24_FLAG_ADDR16)
		msg.buf[i++] = offset >> 8;

	msg.buf[i++] = offset;
	memcpy(&msg.buf[i], buf, count);
	msg.len = i + count;

	loop_until_timeout(timeout, write_time) {
		status = i2c_transfer(client->adapter, &msg, 1);
		if (status == 1)
			status = count;

		dev_dbg(&client->dev, "write %zu@%d --> %zd (%ld)\n",
				count, offset, status, jiffies);

		if (status == count)
			return count;
	}

	return -ETIMEDOUT;
}

static int at24_read(void *priv, unsigned int off, void *val, size_t count)
{
	struct at24_data *at24 = priv;
	char *buf = val;

	if (unlikely(!count))
		return count;

	if (off + count > at24->chip.byte_len)
		return -EINVAL;

	/*
	 * Read data from chip, protecting against concurrent updates
	 * from this host, but not from other I2C masters.
	 */
	mutex_lock(&at24->lock);

	while (count) {
		int	status;

		status = at24->read_func(at24, buf, off, count);
		if (status < 0) {
			mutex_unlock(&at24->lock);
			return status;
		}
		buf += status;
		off += status;
		count -= status;
	}

	mutex_unlock(&at24->lock);

	return 0;
}

static int at24_write(void *priv, unsigned int off, void *val, size_t count)
{
	struct at24_data *at24 = priv;
	char *buf = val;

	if (unlikely(!count))
		return -EINVAL;

	if (off + count > at24->chip.byte_len)
		return -EINVAL;

	/*
	 * Write data to chip, protecting against concurrent updates
	 * from this host, but not from other I2C masters.
	 */
	mutex_lock(&at24->lock);

	while (count) {
		int status;

		status = at24->write_func(at24, buf, off, count);
		if (status < 0) {
			mutex_unlock(&at24->lock);
			return status;
		}
		buf += status;
		off += status;
		count -= status;
	}

	mutex_unlock(&at24->lock);

	return 0;
}

static void at24_get_pdata(struct device *dev, struct at24_platform_data *chip)
{
	int err;
	u32 val;

	if (device_property_present(dev, "read-only"))
		chip->flags |= AT24_FLAG_READONLY;

	err = device_property_read_u32(dev, "address-width", &val);
	if (!err) {
		switch (val) {
		case 8:
			if (chip->flags & AT24_FLAG_ADDR16)
				dev_warn(dev, "Override address width to be 8, while default is 16\n");
			chip->flags &= ~AT24_FLAG_ADDR16;
			break;
		case 16:
			chip->flags |= AT24_FLAG_ADDR16;
			break;
		default:
			dev_warn(dev, "Bad \"address-width\" property: %u\n",
				 val);
		}
	}

	err = device_property_read_u32(dev, "pagesize", &val);
	if (!err) {
		chip->page_size = val;
	} else {
		/*
		 * This is slow, but we can't know all eeproms, so we better
		 * play safe. Specifying custom eeprom-types via platform_data
		 * is recommended anyhow.
		 */
		chip->page_size = 1;
	}
}

static int at24_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct at24_platform_data chip;
	kernel_ulong_t magic = 0;
	bool writable;
	int use_smbus = 0;
	int use_smbus_write = 0;
	struct at24_data *at24;
	int err;
	unsigned i, num_addresses;
	u8 test_byte;

	if (client->dev.platform_data) {
		chip = *(struct at24_platform_data *)client->dev.platform_data;
	} else {
		if (id) {
			magic = id->driver_data;
		} else {
			const struct acpi_device_id *aid;

			aid = acpi_match_device(at24_acpi_ids, &client->dev);
			if (aid)
				magic = aid->driver_data;
		}
		if (!magic)
			return -ENODEV;

		chip.byte_len = BIT(magic & AT24_BITMASK(AT24_SIZE_BYTELEN));
		magic >>= AT24_SIZE_BYTELEN;
		chip.flags = magic & AT24_BITMASK(AT24_SIZE_FLAGS);

		at24_get_pdata(&client->dev, &chip);

		chip.setup = NULL;
		chip.context = NULL;
	}

	if (!is_power_of_2(chip.byte_len))
		dev_warn(&client->dev,
			"byte_len looks suspicious (no power of 2)!\n");
	if (!chip.page_size) {
		dev_err(&client->dev, "page_size must not be 0!\n");
		return -EINVAL;
	}
	if (!is_power_of_2(chip.page_size))
		dev_warn(&client->dev,
			"page_size looks suspicious (no power of 2)!\n");

	/*
	 * REVISIT: the size of the EUI-48 byte array is 6 in at24mac402, while
	 * the call to ilog2() in AT24_DEVICE_MAGIC() rounds it down to 4.
	 *
	 * Eventually we'll get rid of the magic values altoghether in favor of
	 * real structs, but for now just manually set the right size.
	 */
	if (chip.flags & AT24_FLAG_MAC && chip.byte_len == 4)
		chip.byte_len = 6;

	/* Use I2C operations unless we're stuck with SMBus extensions. */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		if (chip.flags & AT24_FLAG_ADDR16)
			return -EPFNOSUPPORT;

		if (i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
			use_smbus = I2C_SMBUS_I2C_BLOCK_DATA;
		} else if (i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_READ_WORD_DATA)) {
			use_smbus = I2C_SMBUS_WORD_DATA;
		} else if (i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_READ_BYTE_DATA)) {
			use_smbus = I2C_SMBUS_BYTE_DATA;
		} else {
			return -EPFNOSUPPORT;
		}

		if (i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_WRITE_I2C_BLOCK)) {
			use_smbus_write = I2C_SMBUS_I2C_BLOCK_DATA;
		} else if (i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_WRITE_BYTE_DATA)) {
			use_smbus_write = I2C_SMBUS_BYTE_DATA;
			chip.page_size = 1;
		}
	}

	if (chip.flags & AT24_FLAG_TAKE8ADDR)
		num_addresses = 8;
	else
		num_addresses =	DIV_ROUND_UP(chip.byte_len,
			(chip.flags & AT24_FLAG_ADDR16) ? 65536 : 256);

	at24 = devm_kzalloc(&client->dev, sizeof(struct at24_data) +
		num_addresses * sizeof(struct i2c_client *), GFP_KERNEL);
	if (!at24)
		return -ENOMEM;

	mutex_init(&at24->lock);
	at24->use_smbus = use_smbus;
	at24->use_smbus_write = use_smbus_write;
	at24->chip = chip;
	at24->num_addresses = num_addresses;

	if ((chip.flags & AT24_FLAG_SERIAL) && (chip.flags & AT24_FLAG_MAC)) {
		dev_err(&client->dev,
			"invalid device data - cannot have both AT24_FLAG_SERIAL & AT24_FLAG_MAC.");
		return -EINVAL;
	}

	if (chip.flags & AT24_FLAG_SERIAL) {
		at24->read_func = at24_eeprom_read_serial;
	} else if (chip.flags & AT24_FLAG_MAC) {
		at24->read_func = at24_eeprom_read_mac;
	} else {
		at24->read_func = at24->use_smbus ? at24_eeprom_read_smbus
						  : at24_eeprom_read_i2c;
	}

	if (at24->use_smbus) {
		if (at24->use_smbus_write == I2C_SMBUS_I2C_BLOCK_DATA)
			at24->write_func = at24_eeprom_write_smbus_block;
		else
			at24->write_func = at24_eeprom_write_smbus_byte;
	} else {
		at24->write_func = at24_eeprom_write_i2c;
	}

	writable = !(chip.flags & AT24_FLAG_READONLY);
	if (writable) {
		if (!use_smbus || use_smbus_write) {

			unsigned write_max = chip.page_size;

			if (write_max > io_limit)
				write_max = io_limit;
			if (use_smbus && write_max > I2C_SMBUS_BLOCK_MAX)
				write_max = I2C_SMBUS_BLOCK_MAX;
			at24->write_max = write_max;

			/* buffer (data + address at the beginning) */
			at24->writebuf = devm_kzalloc(&client->dev,
				write_max + 2, GFP_KERNEL);
			if (!at24->writebuf)
				return -ENOMEM;
		} else {
			dev_warn(&client->dev,
				"cannot write due to controller restrictions.");
		}
	}

	at24->client[0] = client;

	/* use dummy devices for multiple-address chips */
	for (i = 1; i < num_addresses; i++) {
		at24->client[i] = i2c_new_dummy(client->adapter,
					client->addr + i);
		if (!at24->client[i]) {
			dev_err(&client->dev, "address 0x%02x unavailable\n",
					client->addr + i);
			err = -EADDRINUSE;
			goto err_clients;
		}
	}

	i2c_set_clientdata(client, at24);

	/*
	 * Perform a one-byte test read to verify that the
	 * chip is functional.
	 */
	err = at24_read(at24, 0, &test_byte, 1);
	if (err) {
		err = -ENODEV;
		goto err_clients;
	}

	at24->nvmem_config.name = dev_name(&client->dev);
	at24->nvmem_config.dev = &client->dev;
	at24->nvmem_config.read_only = !writable;
	at24->nvmem_config.root_only = true;
	at24->nvmem_config.owner = THIS_MODULE;
	at24->nvmem_config.compat = true;
	at24->nvmem_config.base_dev = &client->dev;
	at24->nvmem_config.reg_read = at24_read;
	at24->nvmem_config.reg_write = at24_write;
	at24->nvmem_config.priv = at24;
	at24->nvmem_config.stride = 1;
	at24->nvmem_config.word_size = 1;
	at24->nvmem_config.size = chip.byte_len;

	at24->nvmem = nvmem_register(&at24->nvmem_config);

	if (IS_ERR(at24->nvmem)) {
		err = PTR_ERR(at24->nvmem);
		goto err_clients;
	}

	dev_info(&client->dev, "%u byte %s EEPROM, %s, %u bytes/write\n",
		chip.byte_len, client->name,
		writable ? "writable" : "read-only", at24->write_max);
	if (use_smbus == I2C_SMBUS_WORD_DATA ||
	    use_smbus == I2C_SMBUS_BYTE_DATA) {
		dev_notice(&client->dev, "Falling back to %s reads, "
			   "performance will suffer\n", use_smbus ==
			   I2C_SMBUS_WORD_DATA ? "word" : "byte");
	}

	/* export data to kernel code */
	if (chip.setup)
		chip.setup(at24->nvmem, chip.context);

	return 0;

err_clients:
	for (i = 1; i < num_addresses; i++)
		if (at24->client[i])
			i2c_unregister_device(at24->client[i]);

	return err;
}

static int at24_remove(struct i2c_client *client)
{
	struct at24_data *at24;
	int i;

	at24 = i2c_get_clientdata(client);

	nvmem_unregister(at24->nvmem);

	for (i = 1; i < at24->num_addresses; i++)
		i2c_unregister_device(at24->client[i]);

	return 0;
}

/*-------------------------------------------------------------------------*/

static struct i2c_driver at24_driver = {
	.driver = {
		.name = "at24",
		.acpi_match_table = ACPI_PTR(at24_acpi_ids),
	},
	.probe = at24_probe,
	.remove = at24_remove,
	.id_table = at24_ids,
};

static int __init at24_init(void)
{
	if (!io_limit) {
		pr_err("at24: io_limit must not be 0!\n");
		return -EINVAL;
	}

	io_limit = rounddown_pow_of_two(io_limit);
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
