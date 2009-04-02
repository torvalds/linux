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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/sched.h>

#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>


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

	if (unlikely(offset >= at25->bin.size))
		return 0;
	if ((offset + count) > at25->bin.size)
		count = at25->bin.size - offset;
	if (unlikely(!count))
		return count;

	cp = command;
	*cp++ = AT25_READ;

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
at25_bin_read(struct kobject *kobj, struct bin_attribute *bin_attr,
	      char *buf, loff_t off, size_t count)
{
	struct device		*dev;
	struct at25_data	*at25;

	dev = container_of(kobj, struct device, kobj);
	at25 = dev_get_drvdata(dev);

	return at25_ee_read(at25, buf, off, count);
}


static ssize_t
at25_ee_write(struct at25_data *at25, char *buf, loff_t off, size_t count)
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
	bounce[0] = AT25_WRITE;
	mutex_lock(&at25->lock);
	do {
		unsigned long	timeout, retries;
		unsigned	segment;
		unsigned	offset = (unsigned) off;
		u8		*cp = bounce + 1;

		*cp = AT25_WREN;
		status = spi_write(at25->spi, cp, 1);
		if (status < 0) {
			dev_dbg(&at25->spi->dev, "WREN --> %d\n",
					(int) status);
			break;
		}

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
			int	sr;

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

		if (time_after(jiffies, timeout)) {
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
at25_bin_write(struct kobject *kobj, struct bin_attribute *bin_attr,
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

static ssize_t at25_mem_write(struct memory_accessor *mem, char *buf,
			  off_t offset, size_t count)
{
	struct at25_data *at25 = container_of(mem, struct at25_data, mem);

	return at25_ee_write(at25, buf, offset, count);
}

/*-------------------------------------------------------------------------*/

static int at25_probe(struct spi_device *spi)
{
	struct at25_data	*at25 = NULL;
	const struct spi_eeprom *chip;
	int			err;
	int			sr;
	int			addrlen;

	/* Chip description */
	chip = spi->dev.platform_data;
	if (!chip) {
		dev_dbg(&spi->dev, "no chip description\n");
		err = -ENODEV;
		goto fail;
	}

	/* For now we only support 8/16/24 bit addressing */
	if (chip->flags & EE_ADDR1)
		addrlen = 1;
	else if (chip->flags & EE_ADDR2)
		addrlen = 2;
	else if (chip->flags & EE_ADDR3)
		addrlen = 3;
	else {
		dev_dbg(&spi->dev, "unsupported address type\n");
		err = -EINVAL;
		goto fail;
	}

	/* Ping the chip ... the status register is pretty portable,
	 * unlike probing manufacturer IDs.  We do expect that system
	 * firmware didn't write it in the past few milliseconds!
	 */
	sr = spi_w8r8(spi, AT25_RDSR);
	if (sr < 0 || sr & AT25_SR_nRDY) {
		dev_dbg(&spi->dev, "rdsr --> %d (%02x)\n", sr, sr);
		err = -ENXIO;
		goto fail;
	}

	if (!(at25 = kzalloc(sizeof *at25, GFP_KERNEL))) {
		err = -ENOMEM;
		goto fail;
	}

	mutex_init(&at25->lock);
	at25->chip = *chip;
	at25->spi = spi_dev_get(spi);
	dev_set_drvdata(&spi->dev, at25);
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
	at25->bin.attr.name = "eeprom";
	at25->bin.attr.mode = S_IRUSR;
	at25->bin.read = at25_bin_read;
	at25->mem.read = at25_mem_read;

	at25->bin.size = at25->chip.byte_len;
	if (!(chip->flags & EE_READONLY)) {
		at25->bin.write = at25_bin_write;
		at25->bin.attr.mode |= S_IWUSR;
		at25->mem.write = at25_mem_write;
	}

	err = sysfs_create_bin_file(&spi->dev.kobj, &at25->bin);
	if (err)
		goto fail;

	if (chip->setup)
		chip->setup(&at25->mem, chip->context);

	dev_info(&spi->dev, "%Zd %s %s eeprom%s, pagesize %u\n",
		(at25->bin.size < 1024)
			? at25->bin.size
			: (at25->bin.size / 1024),
		(at25->bin.size < 1024) ? "Byte" : "KByte",
		at25->chip.name,
		(chip->flags & EE_READONLY) ? " (readonly)" : "",
		at25->chip.page_size);
	return 0;
fail:
	dev_dbg(&spi->dev, "probe err %d\n", err);
	kfree(at25);
	return err;
}

static int __devexit at25_remove(struct spi_device *spi)
{
	struct at25_data	*at25;

	at25 = dev_get_drvdata(&spi->dev);
	sysfs_remove_bin_file(&spi->dev.kobj, &at25->bin);
	kfree(at25);
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct spi_driver at25_driver = {
	.driver = {
		.name		= "at25",
		.owner		= THIS_MODULE,
	},
	.probe		= at25_probe,
	.remove		= __devexit_p(at25_remove),
};

static int __init at25_init(void)
{
	return spi_register_driver(&at25_driver);
}
module_init(at25_init);

static void __exit at25_exit(void)
{
	spi_unregister_driver(&at25_driver);
}
module_exit(at25_exit);

MODULE_DESCRIPTION("Driver for most SPI EEPROMs");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");

