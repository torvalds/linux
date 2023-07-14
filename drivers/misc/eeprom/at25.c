// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for most of the SPI EEPROMs, such as Atmel AT25 models
 * and Cypress FRAMs FM25 models.
 *
 * Copyright (C) 2006 David Brownell
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/spi/eeprom.h>
#include <linux/spi/spi.h>

#include <linux/nvmem-provider.h>

/*
 * NOTE: this is an *EEPROM* driver. The vagaries of product naming
 * mean that some AT25 products are EEPROMs, and others are FLASH.
 * Handle FLASH chips with the drivers/mtd/devices/m25p80.c driver,
 * not this one!
 *
 * EEPROMs that can be used with this driver include, for example:
 *   AT25M02, AT25128B
 */

#define	FM25_SN_LEN	8		/* serial number length */
#define EE_MAXADDRLEN	3		/* 24 bit addresses, up to 2 MBytes */

struct at25_data {
	struct spi_eeprom	chip;
	struct spi_device	*spi;
	struct mutex		lock;
	unsigned		addrlen;
	struct nvmem_config	nvmem_config;
	struct nvmem_device	*nvmem;
	u8 sernum[FM25_SN_LEN];
	u8 command[EE_MAXADDRLEN + 1];
};

#define	AT25_WREN	0x06		/* latch the write enable */
#define	AT25_WRDI	0x04		/* reset the write enable */
#define	AT25_RDSR	0x05		/* read status register */
#define	AT25_WRSR	0x01		/* write status register */
#define	AT25_READ	0x03		/* read byte(s) */
#define	AT25_WRITE	0x02		/* write byte(s)/sector */
#define	FM25_SLEEP	0xb9		/* enter sleep mode */
#define	FM25_RDID	0x9f		/* read device ID */
#define	FM25_RDSN	0xc3		/* read S/N */

#define	AT25_SR_nRDY	0x01		/* nRDY = write-in-progress */
#define	AT25_SR_WEN	0x02		/* write enable (latched) */
#define	AT25_SR_BP0	0x04		/* BP for software writeprotect */
#define	AT25_SR_BP1	0x08
#define	AT25_SR_WPEN	0x80		/* writeprotect enable */

#define	AT25_INSTR_BIT3	0x08		/* additional address bit in instr */

#define	FM25_ID_LEN	9		/* ID length */

/*
 * Specs often allow 5ms for a page write, sometimes 20ms;
 * it's important to recover from write timeouts.
 */
#define	EE_TIMEOUT	25

/*-------------------------------------------------------------------------*/

#define	io_limit	PAGE_SIZE	/* bytes */

static int at25_ee_read(void *priv, unsigned int offset,
			void *val, size_t count)
{
	struct at25_data *at25 = priv;
	char *buf = val;
	size_t max_chunk = spi_max_transfer_size(at25->spi);
	unsigned int msg_offset = offset;
	size_t bytes_left = count;
	size_t segment;
	u8			*cp;
	ssize_t			status;
	struct spi_transfer	t[2];
	struct spi_message	m;
	u8			instr;

	if (unlikely(offset >= at25->chip.byte_len))
		return -EINVAL;
	if ((offset + count) > at25->chip.byte_len)
		count = at25->chip.byte_len - offset;
	if (unlikely(!count))
		return -EINVAL;

	do {
		segment = min(bytes_left, max_chunk);
		cp = at25->command;

		instr = AT25_READ;
		if (at25->chip.flags & EE_INSTR_BIT3_IS_ADDR)
			if (msg_offset >= BIT(at25->addrlen * 8))
				instr |= AT25_INSTR_BIT3;

		mutex_lock(&at25->lock);

		*cp++ = instr;

		/* 8/16/24-bit address is written MSB first */
		switch (at25->addrlen) {
		default:	/* case 3 */
			*cp++ = msg_offset >> 16;
			fallthrough;
		case 2:
			*cp++ = msg_offset >> 8;
			fallthrough;
		case 1:
		case 0:	/* can't happen: for better code generation */
			*cp++ = msg_offset >> 0;
		}

		spi_message_init(&m);
		memset(t, 0, sizeof(t));

		t[0].tx_buf = at25->command;
		t[0].len = at25->addrlen + 1;
		spi_message_add_tail(&t[0], &m);

		t[1].rx_buf = buf;
		t[1].len = segment;
		spi_message_add_tail(&t[1], &m);

		status = spi_sync(at25->spi, &m);

		mutex_unlock(&at25->lock);

		if (status)
			return status;

		msg_offset += segment;
		buf += segment;
		bytes_left -= segment;
	} while (bytes_left > 0);

	dev_dbg(&at25->spi->dev, "read %zu bytes at %d\n",
		count, offset);
	return 0;
}

/* Read extra registers as ID or serial number */
static int fm25_aux_read(struct at25_data *at25, u8 *buf, uint8_t command,
			 int len)
{
	int status;
	struct spi_transfer t[2];
	struct spi_message m;

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = at25->command;
	t[0].len = 1;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&at25->lock);

	at25->command[0] = command;

	status = spi_sync(at25->spi, &m);
	dev_dbg(&at25->spi->dev, "read %d aux bytes --> %d\n", len, status);

	mutex_unlock(&at25->lock);
	return status;
}

static ssize_t sernum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct at25_data *at25;

	at25 = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%*ph\n", (int)sizeof(at25->sernum), at25->sernum);
}
static DEVICE_ATTR_RO(sernum);

static struct attribute *sernum_attrs[] = {
	&dev_attr_sernum.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sernum);

static int at25_ee_write(void *priv, unsigned int off, void *val, size_t count)
{
	struct at25_data *at25 = priv;
	size_t maxsz = spi_max_transfer_size(at25->spi);
	const char *buf = val;
	int			status = 0;
	unsigned		buf_size;
	u8			*bounce;

	if (unlikely(off >= at25->chip.byte_len))
		return -EFBIG;
	if ((off + count) > at25->chip.byte_len)
		count = at25->chip.byte_len - off;
	if (unlikely(!count))
		return -EINVAL;

	/* Temp buffer starts with command and address */
	buf_size = at25->chip.page_size;
	if (buf_size > io_limit)
		buf_size = io_limit;
	bounce = kmalloc(buf_size + at25->addrlen + 1, GFP_KERNEL);
	if (!bounce)
		return -ENOMEM;

	/*
	 * For write, rollover is within the page ... so we write at
	 * most one page, then manually roll over to the next page.
	 */
	mutex_lock(&at25->lock);
	do {
		unsigned long	timeout, retries;
		unsigned	segment;
		unsigned	offset = off;
		u8		*cp = bounce;
		int		sr;
		u8		instr;

		*cp = AT25_WREN;
		status = spi_write(at25->spi, cp, 1);
		if (status < 0) {
			dev_dbg(&at25->spi->dev, "WREN --> %d\n", status);
			break;
		}

		instr = AT25_WRITE;
		if (at25->chip.flags & EE_INSTR_BIT3_IS_ADDR)
			if (offset >= BIT(at25->addrlen * 8))
				instr |= AT25_INSTR_BIT3;
		*cp++ = instr;

		/* 8/16/24-bit address is written MSB first */
		switch (at25->addrlen) {
		default:	/* case 3 */
			*cp++ = offset >> 16;
			fallthrough;
		case 2:
			*cp++ = offset >> 8;
			fallthrough;
		case 1:
		case 0:	/* can't happen: for better code generation */
			*cp++ = offset >> 0;
		}

		/* Write as much of a page as we can */
		segment = buf_size - (offset % buf_size);
		if (segment > count)
			segment = count;
		if (segment > maxsz)
			segment = maxsz;
		memcpy(cp, buf, segment);
		status = spi_write(at25->spi, bounce,
				segment + at25->addrlen + 1);
		dev_dbg(&at25->spi->dev, "write %u bytes at %u --> %d\n",
			segment, offset, status);
		if (status < 0)
			break;

		/*
		 * REVISIT this should detect (or prevent) failed writes
		 * to read-only sections of the EEPROM...
		 */

		/* Wait for non-busy status */
		timeout = jiffies + msecs_to_jiffies(EE_TIMEOUT);
		retries = 0;
		do {

			sr = spi_w8r8(at25->spi, AT25_RDSR);
			if (sr < 0 || (sr & AT25_SR_nRDY)) {
				dev_dbg(&at25->spi->dev,
					"rdsr --> %d (%02x)\n", sr, sr);
				/* at HZ=100, this is sloooow */
				msleep(1);
				continue;
			}
			if (!(sr & AT25_SR_nRDY))
				break;
		} while (retries++ < 3 || time_before_eq(jiffies, timeout));

		if ((sr < 0) || (sr & AT25_SR_nRDY)) {
			dev_err(&at25->spi->dev,
				"write %u bytes offset %u, timeout after %u msecs\n",
				segment, offset,
				jiffies_to_msecs(jiffies -
					(timeout - EE_TIMEOUT)));
			status = -ETIMEDOUT;
			break;
		}

		off += segment;
		buf += segment;
		count -= segment;

	} while (count > 0);

	mutex_unlock(&at25->lock);

	kfree(bounce);
	return status;
}

/*-------------------------------------------------------------------------*/

static int at25_fw_to_chip(struct device *dev, struct spi_eeprom *chip)
{
	u32 val;
	int err;

	strscpy(chip->name, "at25", sizeof(chip->name));

	err = device_property_read_u32(dev, "size", &val);
	if (err)
		err = device_property_read_u32(dev, "at25,byte-len", &val);
	if (err) {
		dev_err(dev, "Error: missing \"size\" property\n");
		return err;
	}
	chip->byte_len = val;

	err = device_property_read_u32(dev, "pagesize", &val);
	if (err)
		err = device_property_read_u32(dev, "at25,page-size", &val);
	if (err) {
		dev_err(dev, "Error: missing \"pagesize\" property\n");
		return err;
	}
	chip->page_size = val;

	err = device_property_read_u32(dev, "address-width", &val);
	if (err) {
		err = device_property_read_u32(dev, "at25,addr-mode", &val);
		if (err) {
			dev_err(dev, "Error: missing \"address-width\" property\n");
			return err;
		}
		chip->flags = (u16)val;
	} else {
		switch (val) {
		case 9:
			chip->flags |= EE_INSTR_BIT3_IS_ADDR;
			fallthrough;
		case 8:
			chip->flags |= EE_ADDR1;
			break;
		case 16:
			chip->flags |= EE_ADDR2;
			break;
		case 24:
			chip->flags |= EE_ADDR3;
			break;
		default:
			dev_err(dev,
				"Error: bad \"address-width\" property: %u\n",
				val);
			return -ENODEV;
		}
		if (device_property_present(dev, "read-only"))
			chip->flags |= EE_READONLY;
	}
	return 0;
}

static int at25_fram_to_chip(struct device *dev, struct spi_eeprom *chip)
{
	struct at25_data *at25 = container_of(chip, struct at25_data, chip);
	u8 sernum[FM25_SN_LEN];
	u8 id[FM25_ID_LEN];
	int i;

	strscpy(chip->name, "fm25", sizeof(chip->name));

	/* Get ID of chip */
	fm25_aux_read(at25, id, FM25_RDID, FM25_ID_LEN);
	if (id[6] != 0xc2) {
		dev_err(dev, "Error: no Cypress FRAM (id %02x)\n", id[6]);
		return -ENODEV;
	}
	/* Set size found in ID */
	if (id[7] < 0x21 || id[7] > 0x26) {
		dev_err(dev, "Error: unsupported size (id %02x)\n", id[7]);
		return -ENODEV;
	}

	chip->byte_len = BIT(id[7] - 0x21 + 4) * 1024;
	if (chip->byte_len > 64 * 1024)
		chip->flags |= EE_ADDR3;
	else
		chip->flags |= EE_ADDR2;

	if (id[8]) {
		fm25_aux_read(at25, sernum, FM25_RDSN, FM25_SN_LEN);
		/* Swap byte order */
		for (i = 0; i < FM25_SN_LEN; i++)
			at25->sernum[i] = sernum[FM25_SN_LEN - 1 - i];
	}

	chip->page_size = PAGE_SIZE;
	return 0;
}

static const struct of_device_id at25_of_match[] = {
	{ .compatible = "atmel,at25" },
	{ .compatible = "cypress,fm25" },
	{ }
};
MODULE_DEVICE_TABLE(of, at25_of_match);

static const struct spi_device_id at25_spi_ids[] = {
	{ .name = "at25" },
	{ .name = "fm25" },
	{ }
};
MODULE_DEVICE_TABLE(spi, at25_spi_ids);

static int at25_probe(struct spi_device *spi)
{
	struct at25_data	*at25 = NULL;
	int			err;
	int			sr;
	struct spi_eeprom *pdata;
	bool is_fram;

	/*
	 * Ping the chip ... the status register is pretty portable,
	 * unlike probing manufacturer IDs. We do expect that system
	 * firmware didn't write it in the past few milliseconds!
	 */
	sr = spi_w8r8(spi, AT25_RDSR);
	if (sr < 0 || sr & AT25_SR_nRDY) {
		dev_dbg(&spi->dev, "rdsr --> %d (%02x)\n", sr, sr);
		return -ENXIO;
	}

	at25 = devm_kzalloc(&spi->dev, sizeof(*at25), GFP_KERNEL);
	if (!at25)
		return -ENOMEM;

	mutex_init(&at25->lock);
	at25->spi = spi;
	spi_set_drvdata(spi, at25);

	is_fram = fwnode_device_is_compatible(dev_fwnode(&spi->dev), "cypress,fm25");

	/* Chip description */
	pdata = dev_get_platdata(&spi->dev);
	if (pdata) {
		at25->chip = *pdata;
	} else {
		if (is_fram)
			err = at25_fram_to_chip(&spi->dev, &at25->chip);
		else
			err = at25_fw_to_chip(&spi->dev, &at25->chip);
		if (err)
			return err;
	}

	/* For now we only support 8/16/24 bit addressing */
	if (at25->chip.flags & EE_ADDR1)
		at25->addrlen = 1;
	else if (at25->chip.flags & EE_ADDR2)
		at25->addrlen = 2;
	else if (at25->chip.flags & EE_ADDR3)
		at25->addrlen = 3;
	else {
		dev_dbg(&spi->dev, "unsupported address type\n");
		return -EINVAL;
	}

	at25->nvmem_config.type = is_fram ? NVMEM_TYPE_FRAM : NVMEM_TYPE_EEPROM;
	at25->nvmem_config.name = dev_name(&spi->dev);
	at25->nvmem_config.dev = &spi->dev;
	at25->nvmem_config.read_only = at25->chip.flags & EE_READONLY;
	at25->nvmem_config.root_only = true;
	at25->nvmem_config.owner = THIS_MODULE;
	at25->nvmem_config.compat = true;
	at25->nvmem_config.base_dev = &spi->dev;
	at25->nvmem_config.reg_read = at25_ee_read;
	at25->nvmem_config.reg_write = at25_ee_write;
	at25->nvmem_config.priv = at25;
	at25->nvmem_config.stride = 1;
	at25->nvmem_config.word_size = 1;
	at25->nvmem_config.size = at25->chip.byte_len;

	at25->nvmem = devm_nvmem_register(&spi->dev, &at25->nvmem_config);
	if (IS_ERR(at25->nvmem))
		return PTR_ERR(at25->nvmem);

	dev_info(&spi->dev, "%d %s %s %s%s, pagesize %u\n",
		 (at25->chip.byte_len < 1024) ?
			at25->chip.byte_len : (at25->chip.byte_len / 1024),
		 (at25->chip.byte_len < 1024) ? "Byte" : "KByte",
		 at25->chip.name, is_fram ? "fram" : "eeprom",
		 (at25->chip.flags & EE_READONLY) ? " (readonly)" : "",
		 at25->chip.page_size);
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct spi_driver at25_driver = {
	.driver = {
		.name		= "at25",
		.of_match_table = at25_of_match,
		.dev_groups	= sernum_groups,
	},
	.probe		= at25_probe,
	.id_table	= at25_spi_ids,
};

module_spi_driver(at25_driver);

MODULE_DESCRIPTION("Driver for most SPI EEPROMs");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:at25");
