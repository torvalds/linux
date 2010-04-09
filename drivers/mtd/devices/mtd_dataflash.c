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
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/math64.h>

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
 * MMC stack can't (yet?) distinguish between MMC and DataFlash
 * protocols during enumeration.
 */

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
#define OP_WRITE_SECURITY_REVC	0x9A
#define OP_WRITE_SECURITY	0x9B	/* revision D */


struct dataflash {
	uint8_t			command[4];
	char			name[24];

	unsigned		partitioned:1;

	unsigned short		page_offset;	/* offset in flash address */
	unsigned int		page_size;	/* of bytes per page */

	struct mutex		lock;
	struct spi_device	*spi;

	struct mtd_info		mtd;
};

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
					dev_name(&spi->dev), status);
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
	uint8_t			*command;
	uint32_t		rem;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: erase addr=0x%llx len 0x%llx\n",
	      dev_name(&spi->dev), (long long)instr->addr,
	      (long long)instr->len);

	/* Sanity checks */
	if (instr->addr + instr->len > mtd->size)
		return -EINVAL;
	div_u64_rem(instr->len, priv->page_size, &rem);
	if (rem)
		return -EINVAL;
	div_u64_rem(instr->addr, priv->page_size, &rem);
	if (rem)
		return -EINVAL;

	spi_message_init(&msg);

	x.tx_buf = command = priv->command;
	x.len = 4;
	spi_message_add_tail(&x, &msg);

	mutex_lock(&priv->lock);
	while (instr->len > 0) {
		unsigned int	pageaddr;
		int		status;
		int		do_block;

		/* Calculate flash page address; use block erase (for speed) if
		 * we're at a block boundary and need to erase the whole block.
		 */
		pageaddr = div_u64(instr->addr, priv->page_size);
		do_block = (pageaddr & 0x7) == 0 && instr->len >= blocksize;
		pageaddr = pageaddr << priv->page_offset;

		command[0] = do_block ? OP_ERASE_BLOCK : OP_ERASE_PAGE;
		command[1] = (uint8_t)(pageaddr >> 16);
		command[2] = (uint8_t)(pageaddr >> 8);
		command[3] = 0;

		DEBUG(MTD_DEBUG_LEVEL3, "ERASE %s: (%x) %x %x %x [%i]\n",
			do_block ? "block" : "page",
			command[0], command[1], command[2], command[3],
			pageaddr);

		status = spi_sync(spi, &msg);
		(void) dataflash_waitready(spi);

		if (status < 0) {
			printk(KERN_ERR "%s: erase %x, err %d\n",
				dev_name(&spi->dev), pageaddr, status);
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
	mutex_unlock(&priv->lock);

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
	uint8_t			*command;
	int			status;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: read 0x%x..0x%x\n",
		dev_name(&priv->spi->dev), (unsigned)from, (unsigned)(from + len));

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

	mutex_lock(&priv->lock);

	/* Continuous read, max clock = f(car) which may be less than
	 * the peak rate available.  Some chips support commands with
	 * fewer "don't care" bytes.  Both buffers stay unchanged.
	 */
	command[0] = OP_READ_CONTINUOUS;
	command[1] = (uint8_t)(addr >> 16);
	command[2] = (uint8_t)(addr >> 8);
	command[3] = (uint8_t)(addr >> 0);
	/* plus 4 "don't care" bytes */

	status = spi_sync(priv->spi, &msg);
	mutex_unlock(&priv->lock);

	if (status >= 0) {
		*retlen = msg.actual_length - 8;
		status = 0;
	} else
		DEBUG(MTD_DEBUG_LEVEL1, "%s: read %x..%x --> %d\n",
			dev_name(&priv->spi->dev),
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
	uint8_t			*command;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: write 0x%x..0x%x\n",
		dev_name(&spi->dev), (unsigned)to, (unsigned)(to + len));

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

	mutex_lock(&priv->lock);
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
					dev_name(&spi->dev), addr, status);

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
				dev_name(&spi->dev), addr, writelen, status);

		(void) dataflash_waitready(priv->spi);


#ifdef CONFIG_MTD_DATAFLASH_WRITE_VERIFY

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
				dev_name(&spi->dev), addr, status);

		status = dataflash_waitready(priv->spi);

		/* Check result of the compare operation */
		if (status & (1 << 6)) {
			printk(KERN_ERR "%s: compare page %u, err %d\n",
				dev_name(&spi->dev), pageaddr, status);
			remaining = 0;
			status = -EIO;
			break;
		} else
			status = 0;

#endif	/* CONFIG_MTD_DATAFLASH_WRITE_VERIFY */

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
	mutex_unlock(&priv->lock);

	return status;
}

/* ......................................................................... */

#ifdef CONFIG_MTD_DATAFLASH_OTP

static int dataflash_get_otp_info(struct mtd_info *mtd,
		struct otp_info *info, size_t len)
{
	/* Report both blocks as identical:  bytes 0..64, locked.
	 * Unless the user block changed from all-ones, we can't
	 * tell whether it's still writable; so we assume it isn't.
	 */
	info->start = 0;
	info->length = 64;
	info->locked = 1;
	return sizeof(*info);
}

static ssize_t otp_read(struct spi_device *spi, unsigned base,
		uint8_t *buf, loff_t off, size_t len)
{
	struct spi_message	m;
	size_t			l;
	uint8_t			*scratch;
	struct spi_transfer	t;
	int			status;

	if (off > 64)
		return -EINVAL;

	if ((off + len) > 64)
		len = 64 - off;
	if (len == 0)
		return len;

	spi_message_init(&m);

	l = 4 + base + off + len;
	scratch = kzalloc(l, GFP_KERNEL);
	if (!scratch)
		return -ENOMEM;

	/* OUT: OP_READ_SECURITY, 3 don't-care bytes, zeroes
	 * IN:  ignore 4 bytes, data bytes 0..N (max 127)
	 */
	scratch[0] = OP_READ_SECURITY;

	memset(&t, 0, sizeof t);
	t.tx_buf = scratch;
	t.rx_buf = scratch;
	t.len = l;
	spi_message_add_tail(&t, &m);

	dataflash_waitready(spi);

	status = spi_sync(spi, &m);
	if (status >= 0) {
		memcpy(buf, scratch + 4 + base + off, len);
		status = len;
	}

	kfree(scratch);
	return status;
}

static int dataflash_read_fact_otp(struct mtd_info *mtd,
		loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct dataflash	*priv = (struct dataflash *)mtd->priv;
	int			status;

	/* 64 bytes, from 0..63 ... start at 64 on-chip */
	mutex_lock(&priv->lock);
	status = otp_read(priv->spi, 64, buf, from, len);
	mutex_unlock(&priv->lock);

	if (status < 0)
		return status;
	*retlen = status;
	return 0;
}

static int dataflash_read_user_otp(struct mtd_info *mtd,
		loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct dataflash	*priv = (struct dataflash *)mtd->priv;
	int			status;

	/* 64 bytes, from 0..63 ... start at 0 on-chip */
	mutex_lock(&priv->lock);
	status = otp_read(priv->spi, 0, buf, from, len);
	mutex_unlock(&priv->lock);

	if (status < 0)
		return status;
	*retlen = status;
	return 0;
}

static int dataflash_write_user_otp(struct mtd_info *mtd,
		loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct spi_message	m;
	const size_t		l = 4 + 64;
	uint8_t			*scratch;
	struct spi_transfer	t;
	struct dataflash	*priv = (struct dataflash *)mtd->priv;
	int			status;

	if (len > 64)
		return -EINVAL;

	/* Strictly speaking, we *could* truncate the write ... but
	 * let's not do that for the only write that's ever possible.
	 */
	if ((from + len) > 64)
		return -EINVAL;

	/* OUT: OP_WRITE_SECURITY, 3 zeroes, 64 data-or-zero bytes
	 * IN:  ignore all
	 */
	scratch = kzalloc(l, GFP_KERNEL);
	if (!scratch)
		return -ENOMEM;
	scratch[0] = OP_WRITE_SECURITY;
	memcpy(scratch + 4 + from, buf, len);

	spi_message_init(&m);

	memset(&t, 0, sizeof t);
	t.tx_buf = scratch;
	t.len = l;
	spi_message_add_tail(&t, &m);

	/* Write the OTP bits, if they've not yet been written.
	 * This modifies SRAM buffer1.
	 */
	mutex_lock(&priv->lock);
	dataflash_waitready(priv->spi);
	status = spi_sync(priv->spi, &m);
	mutex_unlock(&priv->lock);

	kfree(scratch);

	if (status >= 0) {
		status = 0;
		*retlen = len;
	}
	return status;
}

static char *otp_setup(struct mtd_info *device, char revision)
{
	device->get_fact_prot_info = dataflash_get_otp_info;
	device->read_fact_prot_reg = dataflash_read_fact_otp;
	device->get_user_prot_info = dataflash_get_otp_info;
	device->read_user_prot_reg = dataflash_read_user_otp;

	/* rev c parts (at45db321c and at45db1281 only!) use a
	 * different write procedure; not (yet?) implemented.
	 */
	if (revision > 'c')
		device->write_user_prot_reg = dataflash_write_user_otp;

	return ", OTP";
}

#else

static char *otp_setup(struct mtd_info *device, char revision)
{
	return " (OTP)";
}

#endif

/* ......................................................................... */

/*
 * Register DataFlash device with MTD subsystem.
 */
static int __devinit
add_dataflash_otp(struct spi_device *spi, char *name,
		int nr_pages, int pagesize, int pageoffset, char revision)
{
	struct dataflash		*priv;
	struct mtd_info			*device;
	struct flash_platform_data	*pdata = spi->dev.platform_data;
	char				*otp_tag = "";
	int				err = 0;

	priv = kzalloc(sizeof *priv, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
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
	device->flags = MTD_WRITEABLE;
	device->erase = dataflash_erase;
	device->read = dataflash_read;
	device->write = dataflash_write;
	device->priv = priv;

	device->dev.parent = &spi->dev;

	if (revision >= 'c')
		otp_tag = otp_setup(device, revision);

	dev_info(&spi->dev, "%s (%lld KBytes) pagesize %d bytes%s\n",
			name, (long long)((device->size + 1023) >> 10),
			pagesize, otp_tag);
	dev_set_drvdata(&spi->dev, priv);

	if (mtd_has_partitions()) {
		struct mtd_partition	*parts;
		int			nr_parts = 0;

		if (mtd_has_cmdlinepart()) {
			static const char *part_probes[]
					= { "cmdlinepart", NULL, };

			nr_parts = parse_mtd_partitions(device,
					part_probes, &parts, 0);
		}

		if (nr_parts <= 0 && pdata && pdata->parts) {
			parts = pdata->parts;
			nr_parts = pdata->nr_parts;
		}

		if (nr_parts > 0) {
			priv->partitioned = 1;
			err = add_mtd_partitions(device, parts, nr_parts);
			goto out;
		}
	} else if (pdata && pdata->nr_parts)
		dev_warn(&spi->dev, "ignoring %d default partitions on %s\n",
				pdata->nr_parts, device->name);

	if (add_mtd_device(device) == 1)
		err = -ENODEV;

out:
	if (!err)
		return 0;

	dev_set_drvdata(&spi->dev, NULL);
	kfree(priv);
	return err;
}

static inline int __devinit
add_dataflash(struct spi_device *spi, char *name,
		int nr_pages, int pagesize, int pageoffset)
{
	return add_dataflash_otp(spi, name, nr_pages, pagesize,
			pageoffset, 0);
}

struct flash_info {
	char		*name;

	/* JEDEC id has a high byte of zero plus three data bytes:
	 * the manufacturer id, then a two byte device id.
	 */
	uint32_t	jedec_id;

	/* The size listed here is what works with OP_ERASE_PAGE. */
	unsigned	nr_pages;
	uint16_t	pagesize;
	uint16_t	pageoffset;

	uint16_t	flags;
#define SUP_POW2PS	0x0002		/* supports 2^N byte pages */
#define IS_POW2PS	0x0001		/* uses 2^N byte pages */
};

static struct flash_info __devinitdata dataflash_data [] = {

	/*
	 * NOTE:  chips with SUP_POW2PS (rev D and up) need two entries,
	 * one with IS_POW2PS and the other without.  The entry with the
	 * non-2^N byte page size can't name exact chip revisions without
	 * losing backwards compatibility for cmdlinepart.
	 *
	 * These newer chips also support 128-byte security registers (with
	 * 64 bytes one-time-programmable) and software write-protection.
	 */
	{ "AT45DB011B",  0x1f2200, 512, 264, 9, SUP_POW2PS},
	{ "at45db011d",  0x1f2200, 512, 256, 8, SUP_POW2PS | IS_POW2PS},

	{ "AT45DB021B",  0x1f2300, 1024, 264, 9, SUP_POW2PS},
	{ "at45db021d",  0x1f2300, 1024, 256, 8, SUP_POW2PS | IS_POW2PS},

	{ "AT45DB041x",  0x1f2400, 2048, 264, 9, SUP_POW2PS},
	{ "at45db041d",  0x1f2400, 2048, 256, 8, SUP_POW2PS | IS_POW2PS},

	{ "AT45DB081B",  0x1f2500, 4096, 264, 9, SUP_POW2PS},
	{ "at45db081d",  0x1f2500, 4096, 256, 8, SUP_POW2PS | IS_POW2PS},

	{ "AT45DB161x",  0x1f2600, 4096, 528, 10, SUP_POW2PS},
	{ "at45db161d",  0x1f2600, 4096, 512, 9, SUP_POW2PS | IS_POW2PS},

	{ "AT45DB321x",  0x1f2700, 8192, 528, 10, 0},		/* rev C */

	{ "AT45DB321x",  0x1f2701, 8192, 528, 10, SUP_POW2PS},
	{ "at45db321d",  0x1f2701, 8192, 512, 9, SUP_POW2PS | IS_POW2PS},

	{ "AT45DB642x",  0x1f2800, 8192, 1056, 11, SUP_POW2PS},
	{ "at45db642d",  0x1f2800, 8192, 1024, 10, SUP_POW2PS | IS_POW2PS},
};

static struct flash_info *__devinit jedec_probe(struct spi_device *spi)
{
	int			tmp;
	uint8_t			code = OP_READ_ID;
	uint8_t			id[3];
	uint32_t		jedec;
	struct flash_info	*info;
	int status;

	/* JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here.  Supporting some chips might require using it.
	 *
	 * If the vendor ID isn't Atmel's (0x1f), assume this call failed.
	 * That's not an error; only rev C and newer chips handle it, and
	 * only Atmel sells these chips.
	 */
	tmp = spi_write_then_read(spi, &code, 1, id, 3);
	if (tmp < 0) {
		DEBUG(MTD_DEBUG_LEVEL0, "%s: error %d reading JEDEC ID\n",
			dev_name(&spi->dev), tmp);
		return ERR_PTR(tmp);
	}
	if (id[0] != 0x1f)
		return NULL;

	jedec = id[0];
	jedec = jedec << 8;
	jedec |= id[1];
	jedec = jedec << 8;
	jedec |= id[2];

	for (tmp = 0, info = dataflash_data;
			tmp < ARRAY_SIZE(dataflash_data);
			tmp++, info++) {
		if (info->jedec_id == jedec) {
			DEBUG(MTD_DEBUG_LEVEL1, "%s: OTP, sector protect%s\n",
				dev_name(&spi->dev),
				(info->flags & SUP_POW2PS)
					? ", binary pagesize" : ""
				);
			if (info->flags & SUP_POW2PS) {
				status = dataflash_status(spi);
				if (status < 0) {
					DEBUG(MTD_DEBUG_LEVEL1,
						"%s: status error %d\n",
						dev_name(&spi->dev), status);
					return ERR_PTR(status);
				}
				if (status & 0x1) {
					if (info->flags & IS_POW2PS)
						return info;
				} else {
					if (!(info->flags & IS_POW2PS))
						return info;
				}
			} else
				return info;
		}
	}

	/*
	 * Treat other chips as errors ... we won't know the right page
	 * size (it might be binary) even when we can tell which density
	 * class is involved (legacy chip id scheme).
	 */
	dev_warn(&spi->dev, "JEDEC id %06x not handled\n", jedec);
	return ERR_PTR(-ENODEV);
}

/*
 * Detect and initialize DataFlash device, using JEDEC IDs on newer chips
 * or else the ID code embedded in the status bits:
 *
 *   Device      Density         ID code          #Pages PageSize  Offset
 *   AT45DB011B  1Mbit   (128K)  xx0011xx (0x0c)    512    264      9
 *   AT45DB021B  2Mbit   (256K)  xx0101xx (0x14)   1024    264      9
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
	struct flash_info	*info;

	/*
	 * Try to detect dataflash by JEDEC ID.
	 * If it succeeds we know we have either a C or D part.
	 * D will support power of 2 pagesize option.
	 * Both support the security register, though with different
	 * write procedures.
	 */
	info = jedec_probe(spi);
	if (IS_ERR(info))
		return PTR_ERR(info);
	if (info != NULL)
		return add_dataflash_otp(spi, info->name, info->nr_pages,
				info->pagesize, info->pageoffset,
				(info->flags & SUP_POW2PS) ? 'd' : 'c');

	/*
	 * Older chips support only legacy commands, identifing
	 * capacity using bits in the status byte.
	 */
	status = dataflash_status(spi);
	if (status <= 0 || status == 0xff) {
		DEBUG(MTD_DEBUG_LEVEL1, "%s: status error %d\n",
				dev_name(&spi->dev), status);
		if (status == 0 || status == 0xff)
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
		status = add_dataflash(spi, "AT45DB021B", 1024, 264, 9);
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
				dev_name(&spi->dev), status & 0x3c);
		status = -ENODEV;
	}

	if (status < 0)
		DEBUG(MTD_DEBUG_LEVEL1, "%s: add_dataflash --> %d\n",
				dev_name(&spi->dev), status);

	return status;
}

static int __devexit dataflash_remove(struct spi_device *spi)
{
	struct dataflash	*flash = dev_get_drvdata(&spi->dev);
	int			status;

	DEBUG(MTD_DEBUG_LEVEL1, "%s: remove\n", dev_name(&spi->dev));

	if (mtd_has_partitions() && flash->partitioned)
		status = del_mtd_partitions(&flash->mtd);
	else
		status = del_mtd_device(&flash->mtd);
	if (status == 0) {
		dev_set_drvdata(&spi->dev, NULL);
		kfree(flash);
	}
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
MODULE_ALIAS("spi:mtd_dataflash");
