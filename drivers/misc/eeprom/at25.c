/*
 * at25.c -- support most SPI EEPROMs, such as Atmel AT25 models
 *
 * Copyright (C) 2006 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/sched.h>

#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/of.h>

/*
 * NOTE: this is an *EEPROM* driver.  The vagaries of product naming
 * mean that some AT25 products are EEPROMs, and others are FLASH.
 * Handle FLASH chips with the drivers/mtd/devices/m25p80.c driver,
 * not this one!
 */

struct at25_data {
	struct spi_device	*spi;
	struct memory_accessor	mem;
	struct mutex		lock;
	struct spi_eeprom	chip;
	struct bin_attribute	bin;
	unsigned		addrlen;
};

#define	AT25_WREN	0x06		/* latch the write enable */
#define	AT25_WRDI	0x04		/* reset the write enable */
#define	AT25_RDSR	0x05		/* read status register */
#define	AT25_WRSR	0x01		/* write status register */
#define	AT25_READ	0x03		/* read byte(s) */
#define	AT25_WRITE	0x02		/* write byte(s)/sector */

#define	AT25_SR_nRDY	0x01		/* nRDY = write-in-progress */
#define	AT25_SR_WEN	0x02		/* write enable (latched) */
#define	AT25_SR_BP0	0x04		/* BP for software writeprotect */
#define	AT25_SR_BP1	0x08
#define	AT25_SR_WPEN	0x80		/* writeprotect enable */

#define	AT25_INSTR_BIT3	0x08		/* Additional address bit in instr */

#define EE_MAXADDRLEN	3		/* 24 bit addresses, up to 2 MBytes */

/* Specs often allow 5 msec for a page write, sometimes 20 msec;
 * it's important to recover from write timeouts.
 */
#define	EE_TIMEOUT	25

/*-------------------------------------------------------------------------*/

#define	io_limit	PAGE_SIZE	/* bytes */

static ssize_t
at25_ee_read(
	struct at25_data	*at25,
	char			*buf,
	unsigned		offset,
	size_t			count
)
{
	u8			command[EE_MAXADDRLEN + 1];
	u8			*cp;
	ssize_t			status;
	struct spi_transfer	t[2];
	struct spi_message	m;
	u8			instr;

	if (unlikely(offset >= at25->bin.size))
		return 0;
	if ((offset + count) > at25->bin.size)
		count = at25->bin.size - offset;
	if (unlikely(!count))
		return count;

	cp = command;

	instr = AT25_READ;
	if (at25->chip.flags & EE_INSTR_BIT3_IS_ADDR)
		if (offset >= (1U << (at25->addrlen * 8)))
			instr |= AT25_INSTR_BIT3;
	*cp++ = instr;

	/* 8/16/24-bit address is written MSB first */
	switch (at25->addrlen) {
	default:	/* case 3 */
		*cp++ = offset >> 16;
	case 2:
		*cp++ = offset >> 8;
	case 1:
	case 0:	/* can't happen: for better codegen */
		*cp++ = offset >> 0;
	}

	spi_message_init(&m);
	memset(t, 0, sizeof t);

	t[0].tx_buf = command;
	t[0].len = at25->addrlen + 1;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = count;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&at25->lock);

	/* Read it all at once.
	 *
	 * REVISIT that's potentially a problem with large chips, if
	 * other devices on the bus need to be accessed regularly or
	 * this chip is clocked very slowly
	 */
	status = spi_sync(at25->spi, &m);
	dev_dbg(&at25->spi->dev,
		"read %Zd bytes at %d --> %d\n",
		count, offset, (int) status);

	mutex_unlock(&at25->lock);
	return status ? status : count;
}

static ssize_t
at25_bin_read(struct file *filp, struct kobject *kobj,
	      struct bin_attribute *bin_attr,
	      char *buf, loff_t off, size_t count)
{
	struct device		*dev;
	struct at25_data	*at25;

	dev = container_of(kobj, struct device, kobj);
	at25 = dev_get_drvdata(dev);

	return at25_ee_read(at25, buf, off, count);
}


static ssize_t
at25_ee_write(struct at25_data *at25, const char *buf, loff_t off,
	      size_t count)
{
	ssize_t			status = 0;
	unsigned		written = 0;
	unsigned		buf_size;
	u8			*bounce;

	if (unlikely(off >= at25->bin.size))
		return -EFBIG;
	if ((off + count) > at25->bin.size)
		count = at25->bin.size - off;
	if (unlikely(!count))
		return count;

	/* Temp buffer starts with command and address */
	buf_size = at25->chip.page_size;
	if (buf_size > io_limit)
		buf_size = io_limit;
	bounce = kmalloc(buf_size + at25->addrlen + 1, GFP_KERNEL);
	if (!bounce)
		return -ENOMEM;

	/* For write, rollover is within the page ... so we write at
	 * most one page, then manually roll over to the next page.
	 */
	mutex_lock(&at25->lock);
	do {
		unsigned long	timeout, retries;
		unsigned	segment;
		unsigned	offset = (unsigned) off;
		u8		*cp = bounce;
		int		sr;
		u8		instr;

		*cp = AT25_WREN;
		status = spi_write(at25->spi, cp, 1);
		if (status < 0) {
			dev_dbg(&at25->spi->dev, "WREN --> %d\n",
					(int) status);
			break;
		}

		instr = AT25_WRITE;
		if (at25->chip.flags & EE_INSTR_BIT3_IS_ADDR)
			if (offset >= (1U << (at25->addrlen * 8)))
				instr |= AT25_INSTR_BIT3;
		*cp++ = instr;

		/* 8/16/24-bit address is written MSB first */
		switch (at25->addrlen) {
		default:	/* case 3 */
			*cp++ = offset >> 16;
		case 2:
			*cp++ = offset >> 8;
		case 1:
		case 0:	/* can't happen: for better codegen */
			*cp++ = offset >> 0;
		}

		/* Write as much of a page as we can */
		segment = buf_size - (offset % buf_size);
		if (segment > count)
			segment = count;
		memcpy(cp, buf, segment);
		status = spi_write(at25->spi, bounce,
				segment + at25->addrlen + 1);
		dev_dbg(&at25->spi->dev,
				"write %u bytes at %u --> %d\n",
				segment, offset, (int) status);
		if (status < 0)
			break;

		/* REVISIT this should detect (or prevent) failed writes
		 * to readonly sections of the EEPROM...
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
				"write %d bytes offset %d, "
				"timeout after %u msecs\n",
				segment, offset,
				jiffies_to_msecs(jiffies -
					(timeout - EE_TIMEOUT)));
			status = -ETIMEDOUT;
			break;
		}

		off += segment;
		buf += segment;
		count -= segment;
		written += segment;

	} while (count > 0);

	mutex_unlock(&at25->lock);

	kfree(bounce);
	return written ? written : status;
}

static ssize_t
at25_bin_write(struct file *filp, struct kobject *kobj,
	       struct bin_attribute *bin_attr,
	       char *buf, loff_t off, size_t count)
{
	struct device		*dev;
	struct at25_data	*at25;

	dev = container_of(kobj, struct device, kobj);
	at25 = dev_get_drvdata(dev);

	return at25_ee_write(at25, buf, off, count);
}

/*-------------------------------------------------------------------------*/

/* Let in-kernel code access the eeprom data. */

static ssize_t at25_mem_read(struct memory_accessor *mem, char *buf,
			 off_t offset, size_t count)
{
	struct at25_data *at25 = container_of(mem, struct at25_data, mem);

	return at25_ee_read(at25, buf, offset, count);
}

static ssize_t at25_mem_write(struct memory_accessor *mem, const char *buf,
			  off_t offset, size_t count)
{
	struct at25_data *at25 = container_of(mem, struct at25_data, mem);

	return at25_ee_write(at25, buf, offset, count);
}

/*-------------------------------------------------------------------------*/

static int at25_np_to_chip(struct device *dev,
			   struct device_node *np,
			   struct spi_eeprom *chip)
{
	u32 val;

	memset(chip, 0, sizeof(*chip));
	strncpy(chip->name, np->name, sizeof(chip->name));

	if (of_property_read_u32(np, "size", &val) == 0 ||
	    of_property_read_u32(np, "at25,byte-len", &val) == 0) {
		chip->byte_len = val;
	} else {
		dev_err(dev, "Error: missing \"size\" property\n");
		return -ENODEV;
	}

	if (of_property_read_u32(np, "pagesize", &val) == 0 ||
	    of_property_read_u32(np, "at25,page-size", &val) == 0) {
		chip->page_size = (u16)val;
	} else {
		dev_err(dev, "Error: missing \"pagesize\" property\n");
		return -ENODEV;
	}

	if (of_property_read_u32(np, "at25,addr-mode", &val) == 0) {
		chip->flags = (u16)val;
	} else {
		if (of_property_read_u32(np, "address-width", &val)) {
			dev_err(dev,
				"Error: missing \"address-width\" property\n");
			return -ENODEV;
		}
		switch (val) {
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
		if (of_find_property(np, "read-only", NULL))
			chip->flags |= EE_READONLY;
	}
	return 0;
}

static int at25_probe(struct spi_device *spi)
{
	struct at25_data	*at25 = NULL;
	struct spi_eeprom	chip;
	struct device_node	*np = spi->dev.of_node;
	int			err;
	int			sr;
	int			addrlen;

	/* Chip description */
	if (!spi->dev.platform_data) {
		if (np) {
			err = at25_np_to_chip(&spi->dev, np, &chip);
			if (err)
				return err;
		} else {
			dev_err(&spi->dev, "Error: no chip description\n");
			return -ENODEV;
		}
	} else
		chip = *(struct spi_eeprom *)spi->dev.platform_data;

	/* For now we only support 8/16/24 bit addressing */
	if (chip.flags & EE_ADDR1)
		addrlen = 1;
	else if (chip.flags & EE_ADDR2)
		addrlen = 2;
	else if (chip.flags & EE_ADDR3)
		addrlen = 3;
	else {
		dev_dbg(&spi->dev, "unsupported address type\n");
		return -EINVAL;
	}

	/* Ping the chip ... the status register is pretty portable,
	 * unlike probing manufacturer IDs.  We do expect that system
	 * firmware didn't write it in the past few milliseconds!
	 */
	sr = spi_w8r8(spi, AT25_RDSR);
	if (sr < 0 || sr & AT25_SR_nRDY) {
		dev_dbg(&spi->dev, "rdsr --> %d (%02x)\n", sr, sr);
		return -ENXIO;
	}

	at25 = devm_kzalloc(&spi->dev, sizeof(struct at25_data), GFP_KERNEL);
	if (!at25)
		return -ENOMEM;

	mutex_init(&at25->lock);
	at25->chip = chip;
	at25->spi = spi_dev_get(spi);
	spi_set_drvdata(spi, at25);
	at25->addrlen = addrlen;

	/* Export the EEPROM bytes through sysfs, since that's convenient.
	 * And maybe to other kernel code; it might hold a board's Ethernet
	 * address, or board-specific calibration data generated on the
	 * manufacturing floor.
	 *
	 * Default to root-only access to the data; EEPROMs often hold data
	 * that's sensitive for read and/or write, like ethernet addresses,
	 * security codes, board-specific manufacturing calibrations, etc.
	 */
	sysfs_bin_attr_init(&at25->bin);
	at25->bin.attr.name = "eeprom";
	at25->bin.attr.mode = S_IRUSR;
	at25->bin.read = at25_bin_read;
	at25->mem.read = at25_mem_read;

	at25->bin.size = at25->chip.byte_len;
	if (!(chip.flags & EE_READONLY)) {
		at25->bin.write = at25_bin_write;
		at25->bin.attr.mode |= S_IWUSR;
		at25->mem.write = at25_mem_write;
	}

	err = sysfs_create_bin_file(&spi->dev.kobj, &at25->bin);
	if (err)
		return err;

	if (chip.setup)
		chip.setup(&at25->mem, chip.context);

	dev_info(&spi->dev, "%Zd %s %s eeprom%s, pagesize %u\n",
		(at25->bin.size < 1024)
			? at25->bin.size
			: (at25->bin.size / 1024),
		(at25->bin.size < 1024) ? "Byte" : "KByte",
		at25->chip.name,
		(chip.flags & EE_READONLY) ? " (readonly)" : "",
		at25->chip.page_size);
	return 0;
}

static int at25_remove(struct spi_device *spi)
{
	struct at25_data	*at25;

	at25 = spi_get_drvdata(spi);
	sysfs_remove_bin_file(&spi->dev.kobj, &at25->bin);
	return 0;
}

/*-------------------------------------------------------------------------*/

static const struct of_device_id at25_of_match[] = {
	{ .compatible = "atmel,at25", },
	{ }
};
MODULE_DEVICE_TABLE(of, at25_of_match);

static struct spi_driver at25_driver = {
	.driver = {
		.name		= "at25",
		.owner		= THIS_MODULE,
		.of_match_table = at25_of_match,
	},
	.probe		= at25_probe,
	.remove		= at25_remove,
};

module_spi_driver(at25_driver);

MODULE_DESCRIPTION("Driver for most SPI EEPROMs");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:at25");
