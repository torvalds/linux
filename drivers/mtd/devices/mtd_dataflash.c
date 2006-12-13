/*
 * Atmel AT45xxx DataFlash MTD driver for lightweight SPI framework
 *
 * Largely derived from at91_dataflash.c:
 *  Copyright (C) 2003-2005 SAN People (Pty) Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>


/*
 * DataFlash is a kind of SPI flash.  Most AT45 chips have two buffers in
 * each chip, which may be used for double buffered I/O; but this driver
 * doesn't (yet) use these for any kind of i/o overlap or prefetching.
 *
 * Sometimes DataFlash is packaged in MMC-format cards, although the
 * MMC stack can't use SPI (yet), or distinguish between MMC and DataFlash
 * protocols during enumeration.
 */

#define CONFIG_DATAFLASH_WRITE_VERIFY

/* reads can bypass the buffers */
#define OP_READ_CONTINUOUS	0xE8
#define OP_READ_PAGE		0xD2

/* group B requests can run even while status reports "busy" */
#define OP_READ_STATUS		0xD7	/* group B */

/* move data between host and buffer */
#define OP_READ_BUFFER1		0xD4	/* group B */
#define OP_READ_BUFFER2		0xD6	/* group B */
#define OP_WRITE_BUFFER1	0x84	/* group B */
#define OP_WRITE_BUFFER2	0x87	/* group B */

/* erasing flash */
#define OP_ERASE_PAGE		0x81
#define OP_ERASE_BLOCK		0x50

/* move data between buffer and flash */
#define OP_TRANSFER_BUF1	0x53
#define OP_TRANSFER_BUF2	0x55
#define OP_MREAD_BUFFER1	0xD4
#define OP_MREAD_BUFFER2	0xD6
#define OP_MWERASE_BUFFER1	0x83
#define OP_MWERASE_BUFFER2	0x86
#define OP_MWRITE_BUFFER1	0x88	/* sector must be pre-erased */
#define OP_MWRITE_BUFFER2	0x89	/* sector must be pre-erased */

/* write to buffer, then write-erase to flash */
#define OP_PROGRAM_VIA_BUF1	0x82
#define OP_PROGRAM_VIA_BUF2	0x85

/* compare buffer to flash */
#define OP_COMPARE_BUF1		0x60
#define OP_COMPARE_BUF2		0x61

/* read flash to buffer, then write-erase to flash */
#define OP_REWRITE_VIA_BUF1	0x58
#define OP_REWRITE_VIA_BUF2	0x59

/* newer chips report JEDEC manufacturer and device IDs; chip
 * serial number and OTP bits; and per-sector writeprotect.
 */
#define OP_READ_ID		0x9F
#define OP_READ_SECURITY	0x77
#define OP_WRITE_SECURITY	0x9A	/* OTP bits */


struct dataflash {
	u8			command[4];
	char			name[24];

	unsigned		partitioned:1;

	unsigned short		page_offset;	/* offset in flash address */
	unsigned int		page_size;	/* of bytes per page */

	struct semaphore	lock;
	struct spi_device	*spi;

	struct mtd_info		mtd;
};

#ifdef CONFIG_MTD_PARTITIONS
#define	mtd_has_partitions()	(1)
#else
#define	mtd_has_partitions()	(0)
#endif

/* ......................................................................... */

/*
 * Return the status of the DataFlash device.
 */
static inline int dataflash_status(struct spi_device *spi)
{
	/* NOTE:  at45db321c over 25 MHz wants to write
	 * a dummy byte after the opcode...
	 */
	return spi_w8r8(spi, OP_READ_STATUS);
}

/*
 * Poll the DataFlash device until it is READY.
 * This usually takes 5-20 msec or so; more for sector erase.
 */
static int dataflash_waitready(struct spi_device *spi)
{
	int	status;

	for (;;) {
		status = dataflash_status(spi);
		if (status < 0) {
			DEBUG(MTD_DEBUG_LEVEL1, "%s: status %d?\n",
					spi->dev.bus_id, status);
			status = 0;
		}

		if (status & (1 << 7))	/* RDY/nBSY */
			return status;

		msleep(3);
	}
}

/* ......................................................................... */

/*
 * Erase pages of flash.
 */
static int dataflash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct dataflash	*priv = (struct dataflash *)mtd->priv;
	struct spi_device	*spi = priv->spi;
	struct spi_transfer	x = { .tx_dma = 0, };
	struct spi_message	msg;
	unsigned		blocksize = priv->page_size << 3;
	u8			*command;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: erase addr=0x%x len 0x%x\n",
			spi->dev.bus_id,
			instr->addr, instr->len);

	/* Sanity checks */
	if ((instr->addr + instr->len) > mtd->size
			|| (instr->len % priv->page_size) != 0
			|| (instr->addr % priv->page_size) != 0)
		return -EINVAL;

	spi_message_init(&msg);

	x.tx_buf = command = priv->command;
	x.len = 4;
	spi_message_add_tail(&x, &msg);

	down(&priv->lock);
	while (instr->len > 0) {
		unsigned int	pageaddr;
		int		status;
		int		do_block;

		/* Calculate flash page address; use block erase (for speed) if
		 * we're at a block boundary and need to erase the whole block.
		 */
		pageaddr = instr->addr / priv->page_size;
		do_block = (pageaddr & 0x7) == 0 && instr->len >= blocksize;
		pageaddr = pageaddr << priv->page_offset;

		command[0] = do_block ? OP_ERASE_BLOCK : OP_ERASE_PAGE;
		command[1] = (u8)(pageaddr >> 16);
		command[2] = (u8)(pageaddr >> 8);
		command[3] = 0;

		DEBUG(MTD_DEBUG_LEVEL3, "ERASE %s: (%x) %x %x %x [%i]\n",
			do_block ? "block" : "page",
			command[0], command[1], command[2], command[3],
			pageaddr);

		status = spi_sync(spi, &msg);
		(void) dataflash_waitready(spi);

		if (status < 0) {
			printk(KERN_ERR "%s: erase %x, err %d\n",
				spi->dev.bus_id, pageaddr, status);
			/* REVISIT:  can retry instr->retries times; or
			 * giveup and instr->fail_addr = instr->addr;
			 */
			continue;
		}

		if (do_block) {
			instr->addr += blocksize;
			instr->len -= blocksize;
		} else {
			instr->addr += priv->page_size;
			instr->len -= priv->page_size;
		}
	}
	up(&priv->lock);

	/* Inform MTD subsystem that erase is complete */
	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

/*
 * Read from the DataFlash device.
 *   from   : Start offset in flash device
 *   len    : Amount to read
 *   retlen : About of data actually read
 *   buf    : Buffer containing the data
 */
static int dataflash_read(struct mtd_info *mtd, loff_t from, size_t len,
			       size_t *retlen, u_char *buf)
{
	struct dataflash	*priv = (struct dataflash *)mtd->priv;
	struct spi_transfer	x[2] = { { .tx_dma = 0, }, };
	struct spi_message	msg;
	unsigned int		addr;
	u8			*command;
	int			status;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: read 0x%x..0x%x\n",
		priv->spi->dev.bus_id, (unsigned)from, (unsigned)(from + len));

	*retlen = 0;

	/* Sanity checks */
	if (!len)
		return 0;
	if (from + len > mtd->size)
		return -EINVAL;

	/* Calculate flash page/byte address */
	addr = (((unsigned)from / priv->page_size) << priv->page_offset)
		+ ((unsigned)from % priv->page_size);

	command = priv->command;

	DEBUG(MTD_DEBUG_LEVEL3, "READ: (%x) %x %x %x\n",
		command[0], command[1], command[2], command[3]);

	spi_message_init(&msg);

	x[0].tx_buf = command;
	x[0].len = 8;
	spi_message_add_tail(&x[0], &msg);

	x[1].rx_buf = buf;
	x[1].len = len;
	spi_message_add_tail(&x[1], &msg);

	down(&priv->lock);

	/* Continuous read, max clock = f(car) which may be less than
	 * the peak rate available.  Some chips support commands with
	 * fewer "don't care" bytes.  Both buffers stay unchanged.
	 */
	command[0] = OP_READ_CONTINUOUS;
	command[1] = (u8)(addr >> 16);
	command[2] = (u8)(addr >> 8);
	command[3] = (u8)(addr >> 0);
	/* plus 4 "don't care" bytes */

	status = spi_sync(priv->spi, &msg);
	up(&priv->lock);

	if (status >= 0) {
		*retlen = msg.actual_length - 8;
		status = 0;
	} else
		DEBUG(MTD_DEBUG_LEVEL1, "%s: read %x..%x --> %d\n",
			priv->spi->dev.bus_id,
			(unsigned)from, (unsigned)(from + len),
			status);
	return status;
}

/*
 * Write to the DataFlash device.
 *   to     : Start offset in flash device
 *   len    : Amount to write
 *   retlen : Amount of data actually written
 *   buf    : Buffer containing the data
 */
static int dataflash_write(struct mtd_info *mtd, loff_t to, size_t len,
				size_t * retlen, const u_char * buf)
{
	struct dataflash	*priv = (struct dataflash *)mtd->priv;
	struct spi_device	*spi = priv->spi;
	struct spi_transfer	x[2] = { { .tx_dma = 0, }, };
	struct spi_message	msg;
	unsigned int		pageaddr, addr, offset, writelen;
	size_t			remaining = len;
	u_char			*writebuf = (u_char *) buf;
	int			status = -EINVAL;
	u8			*command;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: write 0x%x..0x%x\n",
		spi->dev.bus_id, (unsigned)to, (unsigned)(to + len));

	*retlen = 0;

	/* Sanity checks */
	if (!len)
		return 0;
	if ((to + len) > mtd->size)
		return -EINVAL;

	spi_message_init(&msg);

	x[0].tx_buf = command = priv->command;
	x[0].len = 4;
	spi_message_add_tail(&x[0], &msg);

	pageaddr = ((unsigned)to / priv->page_size);
	offset = ((unsigned)to % priv->page_size);
	if (offset + len > priv->page_size)
		writelen = priv->page_size - offset;
	else
		writelen = len;

	down(&priv->lock);
	while (remaining > 0) {
		DEBUG(MTD_DEBUG_LEVEL3, "write @ %i:%i len=%i\n",
			pageaddr, offset, writelen);

		/* REVISIT:
		 * (a) each page in a sector must be rewritten at least
		 *     once every 10K sibling erase/program operations.
		 * (b) for pages that are already erased, we could
		 *     use WRITE+MWRITE not PROGRAM for ~30% speedup.
		 * (c) WRITE to buffer could be done while waiting for
		 *     a previous MWRITE/MWERASE to complete ...
		 * (d) error handling here seems to be mostly missing.
		 *
		 * Two persistent bits per page, plus a per-sector counter,
		 * could support (a) and (b) ... we might consider using
		 * the second half of sector zero, which is just one block,
		 * to track that state.  (On AT91, that sector should also
		 * support boot-from-DataFlash.)
		 */

		addr = pageaddr << priv->page_offset;

		/* (1) Maybe transfer partial page to Buffer1 */
		if (writelen != priv->page_size) {
			command[0] = OP_TRANSFER_BUF1;
			command[1] = (addr & 0x00FF0000) >> 16;
			command[2] = (addr & 0x0000FF00) >> 8;
			command[3] = 0;

			DEBUG(MTD_DEBUG_LEVEL3, "TRANSFER: (%x) %x %x %x\n",
				command[0], command[1], command[2], command[3]);

			status = spi_sync(spi, &msg);
			if (status < 0)
				DEBUG(MTD_DEBUG_LEVEL1, "%s: xfer %u -> %d \n",
					spi->dev.bus_id, addr, status);

			(void) dataflash_waitready(priv->spi);
		}

		/* (2) Program full page via Buffer1 */
		addr += offset;
		command[0] = OP_PROGRAM_VIA_BUF1;
		command[1] = (addr & 0x00FF0000) >> 16;
		command[2] = (addr & 0x0000FF00) >> 8;
		command[3] = (addr & 0x000000FF);

		DEBUG(MTD_DEBUG_LEVEL3, "PROGRAM: (%x) %x %x %x\n",
			command[0], command[1], command[2], command[3]);

		x[1].tx_buf = writebuf;
		x[1].len = writelen;
		spi_message_add_tail(x + 1, &msg);
		status = spi_sync(spi, &msg);
		spi_transfer_del(x + 1);
		if (status < 0)
			DEBUG(MTD_DEBUG_LEVEL1, "%s: pgm %u/%u -> %d \n",
				spi->dev.bus_id, addr, writelen, status);

		(void) dataflash_waitready(priv->spi);


#ifdef	CONFIG_DATAFLASH_WRITE_VERIFY

		/* (3) Compare to Buffer1 */
		addr = pageaddr << priv->page_offset;
		command[0] = OP_COMPARE_BUF1;
		command[1] = (addr & 0x00FF0000) >> 16;
		command[2] = (addr & 0x0000FF00) >> 8;
		command[3] = 0;

		DEBUG(MTD_DEBUG_LEVEL3, "COMPARE: (%x) %x %x %x\n",
			command[0], command[1], command[2], command[3]);

		status = spi_sync(spi, &msg);
		if (status < 0)
			DEBUG(MTD_DEBUG_LEVEL1, "%s: compare %u -> %d \n",
				spi->dev.bus_id, addr, status);

		status = dataflash_waitready(priv->spi);

		/* Check result of the compare operation */
		if ((status & (1 << 6)) == 1) {
			printk(KERN_ERR "%s: compare page %u, err %d\n",
				spi->dev.bus_id, pageaddr, status);
			remaining = 0;
			status = -EIO;
			break;
		} else
			status = 0;

#endif	/* CONFIG_DATAFLASH_WRITE_VERIFY */

		remaining = remaining - writelen;
		pageaddr++;
		offset = 0;
		writebuf += writelen;
		*retlen += writelen;

		if (remaining > priv->page_size)
			writelen = priv->page_size;
		else
			writelen = remaining;
	}
	up(&priv->lock);

	return status;
}

/* ......................................................................... */

/*
 * Register DataFlash device with MTD subsystem.
 */
static int __devinit
add_dataflash(struct spi_device *spi, char *name,
		int nr_pages, int pagesize, int pageoffset)
{
	struct dataflash		*priv;
	struct mtd_info			*device;
	struct flash_platform_data	*pdata = spi->dev.platform_data;

	priv = kzalloc(sizeof *priv, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	init_MUTEX(&priv->lock);
	priv->spi = spi;
	priv->page_size = pagesize;
	priv->page_offset = pageoffset;

	/* name must be usable with cmdlinepart */
	sprintf(priv->name, "spi%d.%d-%s",
			spi->master->bus_num, spi->chip_select,
			name);

	device = &priv->mtd;
	device->name = (pdata && pdata->name) ? pdata->name : priv->name;
	device->size = nr_pages * pagesize;
	device->erasesize = pagesize;
	device->writesize = pagesize;
	device->owner = THIS_MODULE;
	device->type = MTD_DATAFLASH;
	device->flags = MTD_CAP_NORFLASH;
	device->erase = dataflash_erase;
	device->read = dataflash_read;
	device->write = dataflash_write;
	device->priv = priv;

	dev_info(&spi->dev, "%s (%d KBytes)\n", name, device->size/1024);
	dev_set_drvdata(&spi->dev, priv);

	if (mtd_has_partitions()) {
		struct mtd_partition	*parts;
		int			nr_parts = 0;

#ifdef CONFIG_MTD_CMDLINE_PARTS
		static const char *part_probes[] = { "cmdlinepart", NULL, };

		nr_parts = parse_mtd_partitions(device, part_probes, &parts, 0);
#endif

		if (nr_parts <= 0 && pdata && pdata->parts) {
			parts = pdata->parts;
			nr_parts = pdata->nr_parts;
		}

		if (nr_parts > 0) {
			priv->partitioned = 1;
			return add_mtd_partitions(device, parts, nr_parts);
		}
	} else if (pdata && pdata->nr_parts)
		dev_warn(&spi->dev, "ignoring %d default partitions on %s\n",
				pdata->nr_parts, device->name);

	return add_mtd_device(device) == 1 ? -ENODEV : 0;
}

/*
 * Detect and initialize DataFlash device:
 *
 *   Device      Density         ID code          #Pages PageSize  Offset
 *   AT45DB011B  1Mbit   (128K)  xx0011xx (0x0c)    512    264      9
 *   AT45DB021B  2Mbit   (256K)  xx0101xx (0x14)   1025    264      9
 *   AT45DB041B  4Mbit   (512K)  xx0111xx (0x1c)   2048    264      9
 *   AT45DB081B  8Mbit   (1M)    xx1001xx (0x24)   4096    264      9
 *   AT45DB0161B 16Mbit  (2M)    xx1011xx (0x2c)   4096    528     10
 *   AT45DB0321B 32Mbit  (4M)    xx1101xx (0x34)   8192    528     10
 *   AT45DB0642  64Mbit  (8M)    xx111xxx (0x3c)   8192   1056     11
 *   AT45DB1282  128Mbit (16M)   xx0100xx (0x10)  16384   1056     11
 */
static int __devinit dataflash_probe(struct spi_device *spi)
{
	int status;

	status = dataflash_status(spi);
	if (status <= 0 || status == 0xff) {
		DEBUG(MTD_DEBUG_LEVEL1, "%s: status error %d\n",
				spi->dev.bus_id, status);
		if (status == 0xff)
			status = -ENODEV;
		return status;
	}

	/* if there's a device there, assume it's dataflash.
	 * board setup should have set spi->max_speed_max to
	 * match f(car) for continuous reads, mode 0 or 3.
	 */
	switch (status & 0x3c) {
	case 0x0c:	/* 0 0 1 1 x x */
		status = add_dataflash(spi, "AT45DB011B", 512, 264, 9);
		break;
	case 0x14:	/* 0 1 0 1 x x */
		status = add_dataflash(spi, "AT45DB021B", 1025, 264, 9);
		break;
	case 0x1c:	/* 0 1 1 1 x x */
		status = add_dataflash(spi, "AT45DB041x", 2048, 264, 9);
		break;
	case 0x24:	/* 1 0 0 1 x x */
		status = add_dataflash(spi, "AT45DB081B", 4096, 264, 9);
		break;
	case 0x2c:	/* 1 0 1 1 x x */
		status = add_dataflash(spi, "AT45DB161x", 4096, 528, 10);
		break;
	case 0x34:	/* 1 1 0 1 x x */
		status = add_dataflash(spi, "AT45DB321x", 8192, 528, 10);
		break;
	case 0x38:	/* 1 1 1 x x x */
	case 0x3c:
		status = add_dataflash(spi, "AT45DB642x", 8192, 1056, 11);
		break;
	/* obsolete AT45DB1282 not (yet?) supported */
	default:
		DEBUG(MTD_DEBUG_LEVEL1, "%s: unsupported device (%x)\n",
				spi->dev.bus_id, status & 0x3c);
		status = -ENODEV;
	}

	if (status < 0)
		DEBUG(MTD_DEBUG_LEVEL1, "%s: add_dataflash --> %d\n",
				spi->dev.bus_id, status);

	return status;
}

static int __devexit dataflash_remove(struct spi_device *spi)
{
	struct dataflash	*flash = dev_get_drvdata(&spi->dev);
	int			status;

	DEBUG(MTD_DEBUG_LEVEL1, "%s: remove\n", spi->dev.bus_id);

	if (mtd_has_partitions() && flash->partitioned)
		status = del_mtd_partitions(&flash->mtd);
	else
		status = del_mtd_device(&flash->mtd);
	if (status == 0)
		kfree(flash);
	return status;
}

static struct spi_driver dataflash_driver = {
	.driver = {
		.name		= "mtd_dataflash",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= dataflash_probe,
	.remove		= __devexit_p(dataflash_remove),

	/* FIXME:  investigate suspend and resume... */
};

static int __init dataflash_init(void)
{
	return spi_register_driver(&dataflash_driver);
}
module_init(dataflash_init);

static void __exit dataflash_exit(void)
{
	spi_unregister_driver(&dataflash_driver);
}
module_exit(dataflash_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrew Victor, David Brownell");
MODULE_DESCRIPTION("MTD DataFlash driver");
