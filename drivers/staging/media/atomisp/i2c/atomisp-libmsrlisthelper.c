/*
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#include <linux/i2c.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/export.h>
#include "../include/linux/libmsrlisthelper.h"
#include <linux/module.h>
#include <linux/slab.h>

/* Tagged binary data container structure definitions. */
struct tbd_header {
	uint32_t tag;          /*!< Tag identifier, also checks endianness */
	uint32_t size;         /*!< Container size including this header */
	uint32_t version;      /*!< Version, format 0xYYMMDDVV */
	uint32_t revision;     /*!< Revision, format 0xYYMMDDVV */
	uint32_t config_bits;  /*!< Configuration flag bits set */
	uint32_t checksum;     /*!< Global checksum, header included */
} __packed;

struct tbd_record_header {
	uint32_t size;        /*!< Size of record including header */
	uint8_t format_id;    /*!< tbd_format_t enumeration values used */
	uint8_t packing_key;  /*!< Packing method; 0 = no packing */
	uint16_t class_id;    /*!< tbd_class_t enumeration values used */
} __packed;

struct tbd_data_record_header {
	uint16_t next_offset;
	uint16_t flags;
	uint16_t data_offset;
	uint16_t data_size;
} __packed;

#define TBD_CLASS_DRV_ID 2

static int set_msr_configuration(struct i2c_client *client, uint8_t *bufptr,
		unsigned int size)
{
	/* The configuration data contains any number of sequences where
	 * the first byte (that is, uint8_t) that marks the number of bytes
	 * in the sequence to follow, is indeed followed by the indicated
	 * number of bytes of actual data to be written to sensor.
	 * By convention, the first two bytes of actual data should be
	 * understood as an address in the sensor address space (hibyte
	 * followed by lobyte) where the remaining data in the sequence
	 * will be written. */

	uint8_t *ptr = bufptr;
	while (ptr < bufptr + size) {
		struct i2c_msg msg = {
			.addr = client->addr,
			.flags = 0,
		};
		int ret;

		/* How many bytes */
		msg.len = *ptr++;
		/* Where the bytes are located */
		msg.buf = ptr;
		ptr += msg.len;

		if (ptr > bufptr + size)
			/* Accessing data beyond bounds is not tolerated */
			return -EINVAL;

		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			dev_err(&client->dev, "i2c write error: %d", ret);
			return ret;
		}
	}
	return 0;
}

static int parse_and_apply(struct i2c_client *client, uint8_t *buffer,
		unsigned int size)
{
	uint8_t *endptr8 = buffer + size;
	struct tbd_data_record_header *header =
		(struct tbd_data_record_header *)buffer;

	/* There may be any number of datasets present */
	unsigned int dataset = 0;

	do {
		/* In below, four variables are read from buffer */
		if ((uint8_t *)header + sizeof(*header) > endptr8)
			return -EINVAL;

		/* All data should be located within given buffer */
		if ((uint8_t *)header + header->data_offset +
				header->data_size > endptr8)
			return -EINVAL;

		/* We have a new valid dataset */
		dataset++;
		/* See whether there is MSR data */
		/* If yes, update the reg info */
		if (header->data_size && (header->flags & 1)) {
			int ret;

			dev_info(&client->dev,
				"New MSR data for sensor driver (dataset %02d) size:%d\n",
				dataset, header->data_size);
			ret = set_msr_configuration(client,
						buffer + header->data_offset,
						header->data_size);
			if (ret)
				return ret;
		}
		header = (struct tbd_data_record_header *)(buffer +
			header->next_offset);
	} while (header->next_offset);

	return 0;
}

int apply_msr_data(struct i2c_client *client, const struct firmware *fw)
{
	struct tbd_header *header;
	struct tbd_record_header *record;

	if (!fw) {
		dev_warn(&client->dev, "Drv data is not loaded.\n");
		return -EINVAL;
	}

	if (sizeof(*header) > fw->size)
		return -EINVAL;

	header = (struct tbd_header *)fw->data;
	/* Check that we have drvb block. */
	if (memcmp(&header->tag, "DRVB", 4))
		return -EINVAL;

	/* Check the size */
	if (header->size != fw->size)
		return -EINVAL;

	if (sizeof(*header) + sizeof(*record) > fw->size)
		return -EINVAL;

	record = (struct tbd_record_header *)(header + 1);
	/* Check that class id mathes tbd's drv id. */
	if (record->class_id != TBD_CLASS_DRV_ID)
		return -EINVAL;

	/* Size 0 shall not be treated as an error */
	if (!record->size)
		return 0;

	return parse_and_apply(client, (uint8_t *)(record + 1), record->size);
}
EXPORT_SYMBOL_GPL(apply_msr_data);

int load_msr_list(struct i2c_client *client, char *name,
		const struct firmware **fw)
{
	int ret = request_firmware(fw, name, &client->dev);
	if (ret) {
		dev_err(&client->dev,
			"Error %d while requesting firmware %s\n",
			ret, name);
		return ret;
	}
	dev_info(&client->dev, "Received %lu bytes drv data\n",
			(unsigned long)(*fw)->size);

	return 0;
}
EXPORT_SYMBOL_GPL(load_msr_list);

void release_msr_list(struct i2c_client *client, const struct firmware *fw)
{
	release_firmware(fw);
}
EXPORT_SYMBOL_GPL(release_msr_list);

static int init_msrlisthelper(void)
{
	return 0;
}

static void exit_msrlisthelper(void)
{
}

module_init(init_msrlisthelper);
module_exit(exit_msrlisthelper);

MODULE_AUTHOR("Jukka Kaartinen <jukka.o.kaartinen@intel.com>");
MODULE_LICENSE("GPL");
