/*
 * Copyright (C) 2015 Zodiac Inflight Innovations
 *
 * Author: Martyn Welch <martyn.welch@collabora.co.uk>
 *
 * Based on twl4030_wdt.c by Timo Kokkonen <timo.t.kokkonen at nokia.com>:
 *
 * Copyright (C) Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/ihex.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/watchdog.h>

#define ZIIRAVE_TIMEOUT_MIN	3
#define ZIIRAVE_TIMEOUT_MAX	255

#define ZIIRAVE_PING_VALUE	0x0

#define ZIIRAVE_STATE_INITIAL	0x0
#define ZIIRAVE_STATE_OFF	0x1
#define ZIIRAVE_STATE_ON	0x2

#define ZIIRAVE_FW_NAME		"ziirave_wdt.fw"

static char *ziirave_reasons[] = {"power cycle", "hw watchdog", NULL, NULL,
				  "host request", NULL, "illegal configuration",
				  "illegal instruction", "illegal trap",
				  "unknown"};

#define ZIIRAVE_WDT_FIRM_VER_MAJOR	0x1
#define ZIIRAVE_WDT_BOOT_VER_MAJOR	0x3
#define ZIIRAVE_WDT_RESET_REASON	0x5
#define ZIIRAVE_WDT_STATE		0x6
#define ZIIRAVE_WDT_TIMEOUT		0x7
#define ZIIRAVE_WDT_TIME_LEFT		0x8
#define ZIIRAVE_WDT_PING		0x9
#define ZIIRAVE_WDT_RESET_DURATION	0xa

#define ZIIRAVE_FIRM_PKT_TOTAL_SIZE	20
#define ZIIRAVE_FIRM_PKT_DATA_SIZE	16
#define ZIIRAVE_FIRM_FLASH_MEMORY_START	0x1600
#define ZIIRAVE_FIRM_FLASH_MEMORY_END	0x2bbf

/* Received and ready for next Download packet. */
#define ZIIRAVE_FIRM_DOWNLOAD_ACK	1
/* Currently writing to flash. Retry Download status in a moment! */
#define ZIIRAVE_FIRM_DOWNLOAD_BUSY	2

/* Wait for ACK timeout in ms */
#define ZIIRAVE_FIRM_WAIT_FOR_ACK_TIMEOUT	50

/* Firmware commands */
#define ZIIRAVE_CMD_DOWNLOAD_START		0x10
#define ZIIRAVE_CMD_DOWNLOAD_END		0x11
#define ZIIRAVE_CMD_DOWNLOAD_SET_READ_ADDR	0x12
#define ZIIRAVE_CMD_DOWNLOAD_READ_BYTE		0x13
#define ZIIRAVE_CMD_RESET_PROCESSOR		0x0b
#define ZIIRAVE_CMD_JUMP_TO_BOOTLOADER		0x0c
#define ZIIRAVE_CMD_DOWNLOAD_PACKET		0x0e

struct ziirave_wdt_rev {
	unsigned char major;
	unsigned char minor;
};

struct ziirave_wdt_data {
	struct mutex sysfs_mutex;
	struct watchdog_device wdd;
	struct ziirave_wdt_rev bootloader_rev;
	struct ziirave_wdt_rev firmware_rev;
	int reset_reason;
};

static int wdt_timeout;
module_param(wdt_timeout, int, 0);
MODULE_PARM_DESC(wdt_timeout, "Watchdog timeout in seconds");

static int reset_duration;
module_param(reset_duration, int, 0);
MODULE_PARM_DESC(reset_duration,
		 "Watchdog reset pulse duration in milliseconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int ziirave_wdt_revision(struct i2c_client *client,
				struct ziirave_wdt_rev *rev, u8 command)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, command);
	if (ret < 0)
		return ret;

	rev->major = ret;

	ret = i2c_smbus_read_byte_data(client, command + 1);
	if (ret < 0)
		return ret;

	rev->minor = ret;

	return 0;
}

static int ziirave_wdt_set_state(struct watchdog_device *wdd, int state)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);

	return i2c_smbus_write_byte_data(client, ZIIRAVE_WDT_STATE, state);
}

static int ziirave_wdt_start(struct watchdog_device *wdd)
{
	return ziirave_wdt_set_state(wdd, ZIIRAVE_STATE_ON);
}

static int ziirave_wdt_stop(struct watchdog_device *wdd)
{
	return ziirave_wdt_set_state(wdd, ZIIRAVE_STATE_OFF);
}

static int ziirave_wdt_ping(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);

	return i2c_smbus_write_byte_data(client, ZIIRAVE_WDT_PING,
					 ZIIRAVE_PING_VALUE);
}

static int ziirave_wdt_set_timeout(struct watchdog_device *wdd,
				   unsigned int timeout)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret;

	ret = i2c_smbus_write_byte_data(client, ZIIRAVE_WDT_TIMEOUT, timeout);
	if (!ret)
		wdd->timeout = timeout;

	return ret;
}

static unsigned int ziirave_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret;

	ret = i2c_smbus_read_byte_data(client, ZIIRAVE_WDT_TIME_LEFT);
	if (ret < 0)
		ret = 0;

	return ret;
}

static int ziirave_firm_wait_for_ack(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(ZIIRAVE_FIRM_WAIT_FOR_ACK_TIMEOUT);
	do {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

		usleep_range(5000, 10000);

		ret = i2c_smbus_read_byte(client);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to read byte\n");
			return ret;
		}
	} while (ret == ZIIRAVE_FIRM_DOWNLOAD_BUSY);

	return ret == ZIIRAVE_FIRM_DOWNLOAD_ACK ? 0 : -EIO;
}

static int ziirave_firm_set_read_addr(struct watchdog_device *wdd, u16 addr)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	u8 address[2];

	address[0] = addr & 0xff;
	address[1] = (addr >> 8) & 0xff;

	return i2c_smbus_write_block_data(client,
					  ZIIRAVE_CMD_DOWNLOAD_SET_READ_ADDR,
					  ARRAY_SIZE(address), address);
}

static int ziirave_firm_write_block_data(struct watchdog_device *wdd,
					 u8 command, u8 length, const u8 *data,
					 bool wait_for_ack)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret;

	ret = i2c_smbus_write_block_data(client, command, length, data);
	if (ret) {
		dev_err(&client->dev,
			"Failed to send command 0x%02x: %d\n", command, ret);
		return ret;
	}

	if (wait_for_ack)
		ret = ziirave_firm_wait_for_ack(wdd);

	return ret;
}

static int ziirave_firm_write_byte(struct watchdog_device *wdd, u8 command,
				   u8 byte, bool wait_for_ack)
{
	return ziirave_firm_write_block_data(wdd, command, 1, &byte,
					     wait_for_ack);
}

/*
 * ziirave_firm_write_pkt() - Build and write a firmware packet
 *
 * A packet to send to the firmware is composed by following bytes:
 *     Length | Addr0 | Addr1 | Data0 .. Data15 | Checksum |
 * Where,
 *     Length: A data byte containing the length of the data.
 *     Addr0: Low byte of the address.
 *     Addr1: High byte of the address.
 *     Data0 .. Data15: Array of 16 bytes of data.
 *     Checksum: Checksum byte to verify data integrity.
 */
static int ziirave_firm_write_pkt(struct watchdog_device *wdd,
				  const struct ihex_binrec *rec)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	u8 i, checksum = 0, packet[ZIIRAVE_FIRM_PKT_TOTAL_SIZE];
	int ret;
	u16 addr;

	memset(packet, 0, ARRAY_SIZE(packet));

	/* Packet length */
	packet[0] = (u8)be16_to_cpu(rec->len);
	/* Packet address */
	addr = (be32_to_cpu(rec->addr) & 0xffff) >> 1;
	packet[1] = addr & 0xff;
	packet[2] = (addr & 0xff00) >> 8;

	/* Packet data */
	if (be16_to_cpu(rec->len) > ZIIRAVE_FIRM_PKT_DATA_SIZE)
		return -EMSGSIZE;
	memcpy(packet + 3, rec->data, be16_to_cpu(rec->len));

	/* Packet checksum */
	for (i = 0; i < ZIIRAVE_FIRM_PKT_TOTAL_SIZE - 1; i++)
		checksum += packet[i];
	packet[ZIIRAVE_FIRM_PKT_TOTAL_SIZE - 1] = checksum;

	ret = ziirave_firm_write_block_data(wdd, ZIIRAVE_CMD_DOWNLOAD_PACKET,
					    ARRAY_SIZE(packet), packet, true);
	if (ret)
		dev_err(&client->dev,
		      "Failed to write firmware packet at address 0x%04x: %d\n",
		      addr, ret);

	return ret;
}

static int ziirave_firm_verify(struct watchdog_device *wdd,
			       const struct firmware *fw)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	const struct ihex_binrec *rec;
	int i, ret;
	u8 data[ZIIRAVE_FIRM_PKT_DATA_SIZE];
	u16 addr;

	for (rec = (void *)fw->data; rec; rec = ihex_next_binrec(rec)) {
		/* Zero length marks end of records */
		if (!be16_to_cpu(rec->len))
			break;

		addr = (be32_to_cpu(rec->addr) & 0xffff) >> 1;
		if (addr < ZIIRAVE_FIRM_FLASH_MEMORY_START ||
		    addr > ZIIRAVE_FIRM_FLASH_MEMORY_END)
			continue;

		ret = ziirave_firm_set_read_addr(wdd, addr);
		if (ret) {
			dev_err(&client->dev,
				"Failed to send SET_READ_ADDR command: %d\n",
				ret);
			return ret;
		}

		for (i = 0; i < ARRAY_SIZE(data); i++) {
			ret = i2c_smbus_read_byte_data(client,
						ZIIRAVE_CMD_DOWNLOAD_READ_BYTE);
			if (ret < 0) {
				dev_err(&client->dev,
					"Failed to READ DATA: %d\n", ret);
				return ret;
			}
			data[i] = ret;
		}

		if (memcmp(data, rec->data, be16_to_cpu(rec->len))) {
			dev_err(&client->dev,
				"Firmware mismatch at address 0x%04x\n", addr);
			return -EINVAL;
		}
	}

	return 0;
}

static int ziirave_firm_upload(struct watchdog_device *wdd,
			       const struct firmware *fw)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret, words_till_page_break;
	const struct ihex_binrec *rec;
	struct ihex_binrec *rec_new;

	ret = ziirave_firm_write_byte(wdd, ZIIRAVE_CMD_JUMP_TO_BOOTLOADER, 1,
				      false);
	if (ret)
		return ret;

	msleep(500);

	ret = ziirave_firm_write_byte(wdd, ZIIRAVE_CMD_DOWNLOAD_START, 1, true);
	if (ret)
		return ret;

	msleep(500);

	for (rec = (void *)fw->data; rec; rec = ihex_next_binrec(rec)) {
		/* Zero length marks end of records */
		if (!be16_to_cpu(rec->len))
			break;

		/* Check max data size */
		if (be16_to_cpu(rec->len) > ZIIRAVE_FIRM_PKT_DATA_SIZE) {
			dev_err(&client->dev, "Firmware packet too long (%d)\n",
				be16_to_cpu(rec->len));
			return -EMSGSIZE;
		}

		/* Calculate words till page break */
		words_till_page_break = (64 - ((be32_to_cpu(rec->addr) >> 1) &
					 0x3f));
		if ((be16_to_cpu(rec->len) >> 1) > words_till_page_break) {
			/*
			 * Data in passes page boundary, so we need to split in
			 * two blocks of data. Create a packet with the first
			 * block of data.
			 */
			rec_new = kzalloc(sizeof(struct ihex_binrec) +
					  (words_till_page_break << 1),
					  GFP_KERNEL);
			if (!rec_new)
				return -ENOMEM;

			rec_new->len = cpu_to_be16(words_till_page_break << 1);
			rec_new->addr = rec->addr;
			memcpy(rec_new->data, rec->data,
			       be16_to_cpu(rec_new->len));

			ret = ziirave_firm_write_pkt(wdd, rec_new);
			kfree(rec_new);
			if (ret)
				return ret;

			/* Create a packet with the second block of data */
			rec_new = kzalloc(sizeof(struct ihex_binrec) +
					  be16_to_cpu(rec->len) -
					  (words_till_page_break << 1),
					  GFP_KERNEL);
			if (!rec_new)
				return -ENOMEM;

			/* Remaining bytes */
			rec_new->len = rec->len -
				       cpu_to_be16(words_till_page_break << 1);

			rec_new->addr = cpu_to_be32(be32_to_cpu(rec->addr) +
					(words_till_page_break << 1));

			memcpy(rec_new->data,
			       rec->data + (words_till_page_break << 1),
			       be16_to_cpu(rec_new->len));

			ret = ziirave_firm_write_pkt(wdd, rec_new);
			kfree(rec_new);
			if (ret)
				return ret;
		} else {
			ret = ziirave_firm_write_pkt(wdd, rec);
			if (ret)
				return ret;
		}
	}

	/* For end of download, the length field will be set to 0 */
	rec_new = kzalloc(sizeof(struct ihex_binrec) + 1, GFP_KERNEL);
	if (!rec_new)
		return -ENOMEM;

	ret = ziirave_firm_write_pkt(wdd, rec_new);
	kfree(rec_new);
	if (ret) {
		dev_err(&client->dev, "Failed to send EMPTY packet: %d\n", ret);
		return ret;
	}

	/* This sleep seems to be required */
	msleep(20);

	/* Start firmware verification */
	ret = ziirave_firm_verify(wdd, fw);
	if (ret) {
		dev_err(&client->dev,
			"Failed to verify firmware: %d\n", ret);
		return ret;
	}

	/* End download operation */
	ret = ziirave_firm_write_byte(wdd, ZIIRAVE_CMD_DOWNLOAD_END, 1, false);
	if (ret)
		return ret;

	/* Reset the processor */
	ret = ziirave_firm_write_byte(wdd, ZIIRAVE_CMD_RESET_PROCESSOR, 1,
				      false);
	if (ret)
		return ret;

	msleep(500);

	return 0;
}

static const struct watchdog_info ziirave_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "Zodiac RAVE Watchdog",
};

static const struct watchdog_ops ziirave_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= ziirave_wdt_start,
	.stop		= ziirave_wdt_stop,
	.ping		= ziirave_wdt_ping,
	.set_timeout	= ziirave_wdt_set_timeout,
	.get_timeleft	= ziirave_wdt_get_timeleft,
};

static ssize_t ziirave_wdt_sysfs_show_firm(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);
	int ret;

	ret = mutex_lock_interruptible(&w_priv->sysfs_mutex);
	if (ret)
		return ret;

	ret = sprintf(buf, "02.%02u.%02u", w_priv->firmware_rev.major,
		      w_priv->firmware_rev.minor);

	mutex_unlock(&w_priv->sysfs_mutex);

	return ret;
}

static DEVICE_ATTR(firmware_version, S_IRUGO, ziirave_wdt_sysfs_show_firm,
		   NULL);

static ssize_t ziirave_wdt_sysfs_show_boot(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);
	int ret;

	ret = mutex_lock_interruptible(&w_priv->sysfs_mutex);
	if (ret)
		return ret;

	ret = sprintf(buf, "01.%02u.%02u", w_priv->bootloader_rev.major,
		      w_priv->bootloader_rev.minor);

	mutex_unlock(&w_priv->sysfs_mutex);

	return ret;
}

static DEVICE_ATTR(bootloader_version, S_IRUGO, ziirave_wdt_sysfs_show_boot,
		   NULL);

static ssize_t ziirave_wdt_sysfs_show_reason(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);
	int ret;

	ret = mutex_lock_interruptible(&w_priv->sysfs_mutex);
	if (ret)
		return ret;

	ret = sprintf(buf, "%s", ziirave_reasons[w_priv->reset_reason]);

	mutex_unlock(&w_priv->sysfs_mutex);

	return ret;
}

static DEVICE_ATTR(reset_reason, S_IRUGO, ziirave_wdt_sysfs_show_reason,
		   NULL);

static ssize_t ziirave_wdt_sysfs_store_firm(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);
	const struct firmware *fw;
	int err;

	err = request_ihex_firmware(&fw, ZIIRAVE_FW_NAME, dev);
	if (err) {
		dev_err(&client->dev, "Failed to request ihex firmware\n");
		return err;
	}

	err = mutex_lock_interruptible(&w_priv->sysfs_mutex);
	if (err)
		goto release_firmware;

	err = ziirave_firm_upload(&w_priv->wdd, fw);
	if (err) {
		dev_err(&client->dev, "The firmware update failed: %d\n", err);
		goto unlock_mutex;
	}

	/* Update firmware version */
	err = ziirave_wdt_revision(client, &w_priv->firmware_rev,
				   ZIIRAVE_WDT_FIRM_VER_MAJOR);
	if (err) {
		dev_err(&client->dev, "Failed to read firmware version: %d\n",
			err);
		goto unlock_mutex;
	}

	dev_info(&client->dev, "Firmware updated to version 02.%02u.%02u\n",
		 w_priv->firmware_rev.major, w_priv->firmware_rev.minor);

	/* Restore the watchdog timeout */
	err = ziirave_wdt_set_timeout(&w_priv->wdd, w_priv->wdd.timeout);
	if (err)
		dev_err(&client->dev, "Failed to set timeout: %d\n", err);

unlock_mutex:
	mutex_unlock(&w_priv->sysfs_mutex);

release_firmware:
	release_firmware(fw);

	return err ? err : count;
}

static DEVICE_ATTR(update_firmware, S_IWUSR, NULL,
		   ziirave_wdt_sysfs_store_firm);

static struct attribute *ziirave_wdt_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_bootloader_version.attr,
	&dev_attr_reset_reason.attr,
	&dev_attr_update_firmware.attr,
	NULL
};
ATTRIBUTE_GROUPS(ziirave_wdt);

static int ziirave_wdt_init_duration(struct i2c_client *client)
{
	int ret;

	if (!reset_duration) {
		/* See if the reset pulse duration is provided in an of_node */
		if (!client->dev.of_node)
			ret = -ENODEV;
		else
			ret = of_property_read_u32(client->dev.of_node,
						   "reset-duration-ms",
						   &reset_duration);
		if (ret) {
			dev_info(&client->dev,
				 "Unable to set reset pulse duration, using default\n");
			return 0;
		}
	}

	if (reset_duration < 1 || reset_duration > 255)
		return -EINVAL;

	dev_info(&client->dev, "Setting reset duration to %dms",
		 reset_duration);

	return i2c_smbus_write_byte_data(client, ZIIRAVE_WDT_RESET_DURATION,
					 reset_duration);
}

static int ziirave_wdt_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int ret;
	struct ziirave_wdt_data *w_priv;
	int val;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	w_priv = devm_kzalloc(&client->dev, sizeof(*w_priv), GFP_KERNEL);
	if (!w_priv)
		return -ENOMEM;

	mutex_init(&w_priv->sysfs_mutex);

	w_priv->wdd.info = &ziirave_wdt_info;
	w_priv->wdd.ops = &ziirave_wdt_ops;
	w_priv->wdd.min_timeout = ZIIRAVE_TIMEOUT_MIN;
	w_priv->wdd.max_timeout = ZIIRAVE_TIMEOUT_MAX;
	w_priv->wdd.parent = &client->dev;
	w_priv->wdd.groups = ziirave_wdt_groups;

	watchdog_init_timeout(&w_priv->wdd, wdt_timeout, &client->dev);

	/*
	 * The default value set in the watchdog should be perfectly valid, so
	 * pass that in if we haven't provided one via the module parameter or
	 * of property.
	 */
	if (w_priv->wdd.timeout == 0) {
		val = i2c_smbus_read_byte_data(client, ZIIRAVE_WDT_TIMEOUT);
		if (val < 0)
			return val;

		if (val < ZIIRAVE_TIMEOUT_MIN)
			return -ENODEV;

		w_priv->wdd.timeout = val;
	} else {
		ret = ziirave_wdt_set_timeout(&w_priv->wdd,
					      w_priv->wdd.timeout);
		if (ret)
			return ret;

		dev_info(&client->dev, "Timeout set to %ds.",
			 w_priv->wdd.timeout);
	}

	watchdog_set_nowayout(&w_priv->wdd, nowayout);

	i2c_set_clientdata(client, w_priv);

	/* If in unconfigured state, set to stopped */
	val = i2c_smbus_read_byte_data(client, ZIIRAVE_WDT_STATE);
	if (val < 0)
		return val;

	if (val == ZIIRAVE_STATE_INITIAL)
		ziirave_wdt_stop(&w_priv->wdd);

	ret = ziirave_wdt_init_duration(client);
	if (ret)
		return ret;

	ret = ziirave_wdt_revision(client, &w_priv->firmware_rev,
				   ZIIRAVE_WDT_FIRM_VER_MAJOR);
	if (ret)
		return ret;

	ret = ziirave_wdt_revision(client, &w_priv->bootloader_rev,
				   ZIIRAVE_WDT_BOOT_VER_MAJOR);
	if (ret)
		return ret;

	w_priv->reset_reason = i2c_smbus_read_byte_data(client,
						ZIIRAVE_WDT_RESET_REASON);
	if (w_priv->reset_reason < 0)
		return w_priv->reset_reason;

	if (w_priv->reset_reason >= ARRAY_SIZE(ziirave_reasons) ||
	    !ziirave_reasons[w_priv->reset_reason])
		return -ENODEV;

	ret = watchdog_register_device(&w_priv->wdd);

	return ret;
}

static int ziirave_wdt_remove(struct i2c_client *client)
{
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);

	watchdog_unregister_device(&w_priv->wdd);

	return 0;
}

static const struct i2c_device_id ziirave_wdt_id[] = {
	{ "rave-wdt", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ziirave_wdt_id);

static const struct of_device_id zrv_wdt_of_match[] = {
	{ .compatible = "zii,rave-wdt", },
	{ },
};
MODULE_DEVICE_TABLE(of, zrv_wdt_of_match);

static struct i2c_driver ziirave_wdt_driver = {
	.driver = {
		.name = "ziirave_wdt",
		.of_match_table = zrv_wdt_of_match,
	},
	.probe = ziirave_wdt_probe,
	.remove = ziirave_wdt_remove,
	.id_table = ziirave_wdt_id,
};

module_i2c_driver(ziirave_wdt_driver);

MODULE_AUTHOR("Martyn Welch <martyn.welch@collabora.co.uk");
MODULE_DESCRIPTION("Zodiac Aerospace RAVE Switch Watchdog Processor Driver");
MODULE_LICENSE("GPL");
