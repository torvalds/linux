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
#include <linux/sysfs.h>
#include <linux/mod_devicetable.h>
#include <linux/log2.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>

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
	struct memory_accessor macc;
	bool use_smbus;

	/*
	 * Lock protects against activities from other Linux tasks,
	 * but not from changes by other I2C masters.
	 */
	struct mutex lock;
	struct bin_attribute bin;

	u8 *writebuf;
	unsigned write_max;
	unsigned num_addresses;

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

static const struct i2c_device_id at24_ids[] = {
	/* needs 8 addresses as A0-A2 are ignored */
	{ "24c00", AT24_DEVICE_MAGIC(128 / 8, AT24_FLAG_TAKE8ADDR) },
	/* old variants can't be handled with this generic entry! */
	{ "24c01", AT24_DEVICE_MAGIC(1024 / 8, 0) },
	{ "24c02", AT24_DEVICE_MAGIC(2048 / 8, 0) },
	/* spd is a 24c02 in memory DIMMs */
	{ "spd", AT24_DEVICE_MAGIC(2048 / 8,
		AT24_FLAG_READONLY | AT24_FLAG_IRUGO) },
	{ "24c04", AT24_DEVICE_MAGIC(4096 / 8, 0) },
	/* 24rf08 quirk is handled at i2c-core */
	{ "24c08", AT24_DEVICE_MAGIC(8192 / 8, 0) },
	{ "24c16", AT24_DEVICE_MAGIC(16384 / 8, 0) },
	{ "24c32", AT24_DEVICE_MAGIC(32768 / 8, AT24_FLAG_ADDR16) },
	{ "24c64", AT24_DEVICE_MAGIC(65536 / 8, AT24_FLAG_ADDR16) },
	{ "24c128", AT24_DEVICE_MAGIC(131072 / 8, AT24_FLAG_ADDR16) },
	{ "24c256", AT24_DEVICE_MAGIC(262144 / 8, AT24_FLAG_ADDR16) },
	{ "24c512", AT24_DEVICE_MAGIC(524288 / 8, AT24_FLAG_ADDR16) },
	{ "24c1024", AT24_DEVICE_MAGIC(1048576 / 8, AT24_FLAG_ADDR16) },
	{ "at24", 0 },
	{ /* END OF LIST */ }
};
MODULE_DEVICE_TABLE(i2c, at24_ids);

/*-------------------------------------------------------------------------*/

/*
 * This routine supports chips which consume multiple I2C addresses. It
 * computes the addressing information to be used for a given r/w request.
 * Assumes that sanity checks for offset happened at sysfs-layer.
 */
static struct i2c_client *at24_translate_offset(struct at24_data *at24,
		unsigned *offset)
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

static ssize_t at24_eeprom_read(struct at24_data *at24, char *buf,
		unsigned offset, size_t count)
{
	struct i2c_msg msg[2];
	u8 msgbuf[2];
	struct i2c_client *client;
	int status, i;

	memset(msg, 0, sizeof(msg));

	/*
	 * REVISIT some multi-address chips don't rollover page reads to
	 * the next slave address, so we may need to truncate the count.
	 * Those chips might need another quirk flag.
	 *
	 * If the real hardware used four adjacent 24c02 chips and that
	 * were misconfigured as one 24c08, that would be a similar effect:
	 * one "eeprom" file not four, but larger reads would fail when
	 * they crossed certain pages.
	 */

	/*
	 * Slave address and byte offset derive from the offset. Always
	 * set the byte address; on a multi-master board, another master
	 * may have changed the chip's "current" address pointer.
	 */
	client = at24_translate_offset(at24, &offset);

	if (count > io_limit)
		count = io_limit;

	/* Smaller eeproms can work given some SMBus extension calls */
	if (at24->use_smbus) {
		if (count > I2C_SMBUS_BLOCK_MAX)
			count = I2C_SMBUS_BLOCK_MAX;
		status = i2c_smbus_read_i2c_block_data(client, offset,
				count, buf);
		dev_dbg(&client->dev, "smbus read %zu@%d --> %d\n",
				count, offset, status);
		return (status < 0) ? -EIO : status;
	}

	/*
	 * When we have a better choice than SMBus calls, use a combined
	 * I2C message. Write address; then read up to io_limit data bytes.
	 * Note that read page rollover helps us here (unlike writes).
	 * msgbuf is u8 and will cast to our needs.
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

	status = i2c_transfer(client->adapter, msg, 2);
	dev_dbg(&client->dev, "i2c read %zu@%d --> %d\n",
			count, offset, status);

	if (status == 2)
		return count;
	else if (status >= 0)
		return -EIO;
	else
		return status;
}

static ssize_t at24_read(struct at24_data *at24,
		char *buf, loff_t off, size_t count)
{
	ssize_t retval = 0;

	if (unlikely(!count))
		return count;

	/*
	 * Read data from chip, protecting against concurrent updates
	 * from this host, but not from other I2C masters.
	 */
	mutex_lock(&at24->lock);

	while (count) {
		ssize_t	status;

		status = at24_eeprom_read(at24, buf, off, count);
		if (status <= 0) {
			if (retval == 0)
				retval = status;
			break;
		}
		buf += status;
		off += status;
		count -= status;
		retval += status;
	}

	mutex_unlock(&at24->lock);

	return retval;
}

static ssize_t at24_bin_read(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct at24_data *at24;

	at24 = dev_get_drvdata(container_of(kobj, struct device, kobj));
	return at24_read(at24, buf, off, count);
}


/*
 * Note that if the hardware write-protect pin is pulled high, the whole
 * chip is normally write protected. But there are plenty of product
 * variants here, including OTP fuses and partial chip protect.
 *
 * We only use page mode writes; the alternative is sloooow. This routine
 * writes at most one page.
 */
static ssize_t at24_eeprom_write(struct at24_data *at24, const char *buf,
		unsigned offset, size_t count)
{
	struct i2c_client *client;
	struct i2c_msg msg;
	ssize_t status;
	unsigned long timeout, write_time;
	unsigned next_page;

	/* Get corresponding I2C address and adjust offset */
	client = at24_translate_offset(at24, &offset);

	/* write_max is at most a page */
	if (count > at24->write_max)
		count = at24->write_max;

	/* Never roll over backwards, to the start of this page */
	next_page = roundup(offset + 1, at24->chip.page_size);
	if (offset + count > next_page)
		count = next_page - offset;

	/* If we'll use I2C calls for I/O, set up the message */
	if (!at24->use_smbus) {
		int i = 0;

		msg.addr = client->addr;
		msg.flags = 0;

		/* msg.buf is u8 and casts will mask the values */
		msg.buf = at24->writebuf;
		if (at24->chip.flags & AT24_FLAG_ADDR16)
			msg.buf[i++] = offset >> 8;

		msg.buf[i++] = offset;
		memcpy(&msg.buf[i], buf, count);
		msg.len = i + count;
	}

	/*
	 * Writes fail if the previous one didn't complete yet. We may
	 * loop a few times until this one succeeds, waiting at least
	 * long enough for one entire page write to work.
	 */
	timeout = jiffies + msecs_to_jiffies(write_timeout);
	do {
		write_time = jiffies;
		if (at24->use_smbus) {
			status = i2c_smbus_write_i2c_block_data(client,
					offset, count, buf);
			if (status == 0)
				status = count;
		} else {
			status = i2c_transfer(client->adapter, &msg, 1);
			if (status == 1)
				status = count;
		}
		dev_dbg(&client->dev, "write %zu@%d --> %zd (%ld)\n",
				count, offset, status, jiffies);

		if (status == count)
			return count;

		/* REVISIT: at HZ=100, this is sloooow */
		msleep(1);
	} while (time_before(write_time, timeout));

	return -ETIMEDOUT;
}

static ssize_t at24_write(struct at24_data *at24, const char *buf, loff_t off,
			  size_t count)
{
	ssize_t retval = 0;

	if (unlikely(!count))
		return count;

	/*
	 * Write data to chip, protecting against concurrent updates
	 * from this host, but not from other I2C masters.
	 */
	mutex_lock(&at24->lock);

	while (count) {
		ssize_t	status;

		status = at24_eeprom_write(at24, buf, off, count);
		if (status <= 0) {
			if (retval == 0)
				retval = status;
			break;
		}
		buf += status;
		off += status;
		count -= status;
		retval += status;
	}

	mutex_unlock(&at24->lock);

	return retval;
}

static ssize_t at24_bin_write(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct at24_data *at24;

	at24 = dev_get_drvdata(container_of(kobj, struct device, kobj));
	return at24_write(at24, buf, off, count);
}

/*-------------------------------------------------------------------------*/

/*
 * This lets other kernel code access the eeprom data. For example, it
 * might hold a board's Ethernet address, or board-specific calibration
 * data generated on the manufacturing floor.
 */

static ssize_t at24_macc_read(struct memory_accessor *macc, char *buf,
			 off_t offset, size_t count)
{
	struct at24_data *at24 = container_of(macc, struct at24_data, macc);

	return at24_read(at24, buf, offset, count);
}

static ssize_t at24_macc_write(struct memory_accessor *macc, const char *buf,
			  off_t offset, size_t count)
{
	struct at24_data *at24 = container_of(macc, struct at24_data, macc);

	return at24_write(at24, buf, offset, count);
}

/*-------------------------------------------------------------------------*/

static int at24_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct at24_platform_data chip;
	bool writable;
	bool use_smbus = false;
	struct at24_data *at24;
	int err;
	unsigned i, num_addresses;
	kernel_ulong_t magic;

	if (client->dev.platform_data) {
		chip = *(struct at24_platform_data *)client->dev.platform_data;
	} else {
		if (!id->driver_data) {
			err = -ENODEV;
			goto err_out;
		}
		magic = id->driver_data;
		chip.byte_len = BIT(magic & AT24_BITMASK(AT24_SIZE_BYTELEN));
		magic >>= AT24_SIZE_BYTELEN;
		chip.flags = magic & AT24_BITMASK(AT24_SIZE_FLAGS);
		/*
		 * This is slow, but we can't know all eeproms, so we better
		 * play safe. Specifying custom eeprom-types via platform_data
		 * is recommended anyhow.
		 */
		chip.page_size = 1;

		chip.setup = NULL;
		chip.context = NULL;
	}

	if (!is_power_of_2(chip.byte_len))
		dev_warn(&client->dev,
			"byte_len looks suspicious (no power of 2)!\n");
	if (!is_power_of_2(chip.page_size))
		dev_warn(&client->dev,
			"page_size looks suspicious (no power of 2)!\n");

	/* Use I2C operations unless we're stuck with SMBus extensions. */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		if (chip.flags & AT24_FLAG_ADDR16) {
			err = -EPFNOSUPPORT;
			goto err_out;
		}
		if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
			err = -EPFNOSUPPORT;
			goto err_out;
		}
		use_smbus = true;
	}

	if (chip.flags & AT24_FLAG_TAKE8ADDR)
		num_addresses = 8;
	else
		num_addresses =	DIV_ROUND_UP(chip.byte_len,
			(chip.flags & AT24_FLAG_ADDR16) ? 65536 : 256);

	at24 = kzalloc(sizeof(struct at24_data) +
		num_addresses * sizeof(struct i2c_client *), GFP_KERNEL);
	if (!at24) {
		err = -ENOMEM;
		goto err_out;
	}

	mutex_init(&at24->lock);
	at24->use_smbus = use_smbus;
	at24->chip = chip;
	at24->num_addresses = num_addresses;

	/*
	 * Export the EEPROM bytes through sysfs, since that's convenient.
	 * By default, only root should see the data (maybe passwords etc)
	 */
	at24->bin.attr.name = "eeprom";
	at24->bin.attr.mode = chip.flags & AT24_FLAG_IRUGO ? S_IRUGO : S_IRUSR;
	at24->bin.read = at24_bin_read;
	at24->bin.size = chip.byte_len;

	at24->macc.read = at24_macc_read;

	writable = !(chip.flags & AT24_FLAG_READONLY);
	if (writable) {
		if (!use_smbus || i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_WRITE_I2C_BLOCK)) {

			unsigned write_max = chip.page_size;

			at24->macc.write = at24_macc_write;

			at24->bin.write = at24_bin_write;
			at24->bin.attr.mode |= S_IWUSR;

			if (write_max > io_limit)
				write_max = io_limit;
			if (use_smbus && write_max > I2C_SMBUS_BLOCK_MAX)
				write_max = I2C_SMBUS_BLOCK_MAX;
			at24->write_max = write_max;

			/* buffer (data + address at the beginning) */
			at24->writebuf = kmalloc(write_max + 2, GFP_KERNEL);
			if (!at24->writebuf) {
				err = -ENOMEM;
				goto err_struct;
			}
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

	err = sysfs_create_bin_file(&client->dev.kobj, &at24->bin);
	if (err)
		goto err_clients;

	i2c_set_clientdata(client, at24);

	dev_info(&client->dev, "%zu byte %s EEPROM %s\n",
		at24->bin.size, client->name,
		writable ? "(writable)" : "(read-only)");
	dev_dbg(&client->dev,
		"page_size %d, num_addresses %d, write_max %d%s\n",
		chip.page_size, num_addresses,
		at24->write_max,
		use_smbus ? ", use_smbus" : "");

	/* export data to kernel code */
	if (chip.setup)
		chip.setup(&at24->macc, chip.context);

	return 0;

err_clients:
	for (i = 1; i < num_addresses; i++)
		if (at24->client[i])
			i2c_unregister_device(at24->client[i]);

	kfree(at24->writebuf);
err_struct:
	kfree(at24);
err_out:
	dev_dbg(&client->dev, "probe error %d\n", err);
	return err;
}

static int __devexit at24_remove(struct i2c_client *client)
{
	struct at24_data *at24;
	int i;

	at24 = i2c_get_clientdata(client);
	sysfs_remove_bin_file(&client->dev.kobj, &at24->bin);

	for (i = 1; i < at24->num_addresses; i++)
		i2c_unregister_device(at24->client[i]);

	kfree(at24->writebuf);
	kfree(at24);
	i2c_set_clientdata(client, NULL);
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct i2c_driver at24_driver = {
	.driver = {
		.name = "at24",
		.owner = THIS_MODULE,
	},
	.probe = at24_probe,
	.remove = __devexit_p(at24_remove),
	.id_table = at24_ids,
};

static int __init at24_init(void)
{
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
