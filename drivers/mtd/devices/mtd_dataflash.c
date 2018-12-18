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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/of_device.h>

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

#define CFI_MFR_ATMEL		0x1F

#define DATAFLASH_SHIFT_EXTID	24
#define DATAFLASH_SHIFT_ID	40

struct dataflash {
	u8			command[4];
	char			name[24];

	unsigned short		page_offset;	/* offset in flash address */
	unsigned int		page_size;	/* of bytes per page */

	struct mutex		lock;
	struct spi_device	*spi;

	struct mtd_info		mtd;
};

#ifdef CONFIG_OF
static const struct of_device_id dataflash_dt_ids[] = {
	{ .compatible = "atmel,at45", },
	{ .compatible = "atmel,dataflash", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dataflash_dt_ids);
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
			dev_dbg(&spi->dev, "status %d?\n", status);
			status = 0;
		}

		if (status & (1 << 7))	/* RDY/nBSY */
			return status;

		usleep_range(3000, 4000);
	}
}

/* ......................................................................... */

/*
 * Erase pages of flash.
 */
static int dataflash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct dataflash	*priv = mtd->priv;
	struct spi_device	*spi = priv->spi;
	struct spi_transfer	x = { };
	struct spi_message	msg;
	unsigned		blocksize = priv->page_size << 3;
	u8			*command;
	u32			rem;

	dev_dbg(&spi->dev, "erase addr=0x%llx len 0x%llx\n",
		(long long)instr->addr, (long long)instr->len);

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
		command[1] = (u8)(pageaddr >> 16);
		command[2] = (u8)(pageaddr >> 8);
		command[3] = 0;

		dev_dbg(&spi->dev, "ERASE %s: (%x) %x %x %x [%i]\n",
			do_block ? "block" : "page",
			command[0], command[1], command[2], command[3],
			pageaddr);

		status = spi_sync(spi, &msg);
		(void) dataflash_waitready(spi);

		if (status < 0) {
			dev_err(&spi->dev, "erase %x, err %d\n",
				pageaddr, status);
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
	struct dataflash	*priv = mtd->priv;
	struct spi_transfer	x[2] = { };
	struct spi_message	msg;
	unsigned int		addr;
	u8			*command;
	int			status;

	dev_dbg(&priv->spi->dev, "read 0x%x..0x%x\n",
		  (unsigned int)from, (unsigned int)(from + len));

	/* Calculate flash page/byte address */
	addr = (((unsigned)from / priv->page_size) << priv->page_offset)
		+ ((unsigned)from % priv->page_size);

	command = priv->command;

	dev_dbg(&priv->spi->dev, "READ: (%x) %x %x %x\n",
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
	command[1] = (u8)(addr >> 16);
	command[2] = (u8)(addr >> 8);
	command[3] = (u8)(addr >> 0);
	/* plus 4 "don't care" bytes */

	status = spi_sync(priv->spi, &msg);
	mutex_unlock(&priv->lock);

	if (status >= 0) {
		*retlen = msg.actual_length - 8;
		status = 0;
	} else
		dev_dbg(&priv->spi->dev, "read %x..%x --> %d\n",
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
	struct dataflash	*priv = mtd->priv;
	struct spi_device	*spi = priv->spi;
	struct spi_transfer	x[2] = { };
	struct spi_message	msg;
	unsigned int		pageaddr, addr, offset, writelen;
	size_t			remaining = len;
	u_char			*writebuf = (u_char *) buf;
	int			status = -EINVAL;
	u8			*command;

	dev_dbg(&spi->dev, "write 0x%x..0x%x\n",
		(unsigned int)to, (unsigned int)(to + len));

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
		dev_dbg(&spi->dev, "write @ %i:%i len=%i\n",
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

			dev_dbg(&spi->dev, "TRANSFER: (%x) %x %x %x\n",
				command[0], command[1], command[2], command[3]);

			status = spi_sync(spi, &msg);
			if (status < 0)
				dev_dbg(&spi->dev, "xfer %u -> %d\n",
					addr, status);

			(void) dataflash_waitready(priv->spi);
		}

		/* (2) Program full page via Buffer1 */
		addr += offset;
		command[0] = OP_PROGRAM_VIA_BUF1;
		command[1] = (addr & 0x00FF0000) >> 16;
		command[2] = (addr & 0x0000FF00) >> 8;
		command[3] = (addr & 0x000000FF);

		dev_dbg(&spi->dev, "PROGRAM: (%x) %x %x %x\n",
			command[0], command[1], command[2], command[3]);

		x[1].tx_buf = writebuf;
		x[1].len = writelen;
		spi_message_add_tail(x + 1, &msg);
		status = spi_sync(spi, &msg);
		spi_transfer_del(x + 1);
		if (status < 0)
			dev_dbg(&spi->dev, "pgm %u/%u -> %d\n",
				addr, writelen, status);

		(void) dataflash_waitready(priv->spi);


#ifdef CONFIG_MTD_DATAFLASH_WRITE_VERIFY

		/* (3) Compare to Buffer1 */
		addr = pageaddr << priv->page_offset;
		command[0] = OP_COMPARE_BUF1;
		command[1] = (addr & 0x00FF0000) >> 16;
		command[2] = (addr & 0x0000FF00) >> 8;
		command[3] = 0;

		dev_dbg(&spi->dev, "COMPARE: (%x) %x %x %x\n",
			command[0], command[1], command[2], command[3]);

		status = spi_sync(spi, &msg);
		if (status < 0)
			dev_dbg(&spi->dev, "compare %u -> %d\n",
				addr, status);

		status = dataflash_waitready(priv->spi);

		/* Check result of the compare operation */
		if (status & (1 << 6)) {
			dev_err(&spi->dev, "compare page %u, err %d\n",
				pageaddr, status);
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

static int dataflash_get_otp_info(struct mtd_info *mtd, size_t len,
				  size_t *retlen, struct otp_info *info)
{
	/* Report both blocks as identical:  bytes 0..64, locked.
	 * Unless the user block changed from all-ones, we can't
	 * tell whether it's still writable; so we assume it isn't.
	 */
	info->start = 0;
	info->length = 64;
	info->locked = 1;
	*retlen = sizeof(*info);
	return 0;
}

static ssize_t otp_read(struct spi_device *spi, unsigned base,
		u8 *buf, loff_t off, size_t len)
{
	struct spi_message	m;
	size_t			l;
	u8			*scratch;
	struct spi_transfer	t;
	int			status;

	if (off > 64)
		return -EINVAL;

	if ((off + len) > 64)
		len = 64 - off;

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
	struct dataflash	*priv = mtd->priv;
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
	struct dataflash	*priv = mtd->priv;
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
	u8			*scratch;
	struct spi_transfer	t;
	struct dataflash	*priv = mtd->priv;
	int			status;

	if (from >= 64) {
		/*
		 * Attempting to write beyond the end of OTP memory,
		 * no data can be written.
		 */
		*retlen = 0;
		return 0;
	}

	/* Truncate the write to fit into OTP memory. */
	if ((from + len) > 64)
		len = 64 - from;

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
	device->_get_fact_prot_info = dataflash_get_otp_info;
	device->_read_fact_prot_reg = dataflash_read_fact_otp;
	device->_get_user_prot_info = dataflash_get_otp_info;
	device->_read_user_prot_reg = dataflash_read_user_otp;

	/* rev c parts (at45db321c and at45db1281 only!) use a
	 * different write procedure; not (yet?) implemented.
	 */
	if (revision > 'c')
		device->_write_user_prot_reg = dataflash_write_user_otp;

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
static int add_dataflash_otp(struct spi_device *spi, char *name, int nr_pages,
			     int pagesize, int pageoffset, char revision)
{
	struct dataflash		*priv;
	struct mtd_info			*device;
	struct flash_platform_data	*pdata = dev_get_platdata(&spi->dev);
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
	device->type = MTD_DATAFLASH;
	device->flags = MTD_WRITEABLE;
	device->_erase = dataflash_erase;
	device->_read = dataflash_read;
	device->_write = dataflash_write;
	device->priv = priv;

	device->dev.parent = &spi->dev;
	mtd_set_of_node(device, spi->dev.of_node);

	if (revision >= 'c')
		otp_tag = otp_setup(device, revision);

	dev_info(&spi->dev, "%s (%lld KBytes) pagesize %d bytes%s\n",
			name, (long long)((device->size + 1023) >> 10),
			pagesize, otp_tag);
	spi_set_drvdata(spi, priv);

	err = mtd_device_register(device,
			pdata ? pdata->parts : NULL,
			pdata ? pdata->nr_parts : 0);

	if (!err)
		return 0;

	kfree(priv);
	return err;
}

static inline int add_dataflash(struct spi_device *spi, char *name,
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
	u64		jedec_id;

	/* The size listed here is what works with OP_ERASE_PAGE. */
	unsigned	nr_pages;
	u16		pagesize;
	u16		pageoffset;

	u16		flags;
#define SUP_EXTID	0x0004		/* supports extended ID data */
#define SUP_POW2PS	0x0002		/* supports 2^N byte pages */
#define IS_POW2PS	0x0001		/* uses 2^N byte pages */
};

static struct flash_info dataflash_data[] = {

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

	{ "AT45DB641E",  0x1f28000100ULL, 32768, 264, 9, SUP_EXTID | SUP_POW2PS},
	{ "at45db641e",  0x1f28000100ULL, 32768, 256, 8, SUP_EXTID | SUP_POW2PS | IS_POW2PS},
};

static struct flash_info *jedec_lookup(struct spi_device *spi,
				       u64 jedec, bool use_extid)
{
	struct flash_info *info;
	int status;

	for (info = dataflash_data;
	     info < dataflash_data + ARRAY_SIZE(dataflash_data);
	     info++) {
		if (use_extid && !(info->flags & SUP_EXTID))
			continue;

		if (info->jedec_id == jedec) {
			dev_dbg(&spi->dev, "OTP, sector protect%s\n",
				(info->flags & SUP_POW2PS) ?
				", binary pagesize" : "");
			if (info->flags & SUP_POW2PS) {
				status = dataflash_status(spi);
				if (status < 0) {
					dev_dbg(&spi->dev, "status error %d\n",
						status);
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

	return ERR_PTR(-ENODEV);
}

static struct flash_info *jedec_probe(struct spi_device *spi)
{
	int ret;
	u8 code = OP_READ_ID;
	u64 jedec;
	u8 id[sizeof(jedec)] = {0};
	const unsigned int id_size = 5;
	struct flash_info *info;

	/*
	 * JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here.  Supporting some chips might require using it.
	 *
	 * If the vendor ID isn't Atmel's (0x1f), assume this call failed.
	 * That's not an error; only rev C and newer chips handle it, and
	 * only Atmel sells these chips.
	 */
	ret = spi_write_then_read(spi, &code, 1, id, id_size);
	if (ret < 0) {
		dev_dbg(&spi->dev, "error %d reading JEDEC ID\n", ret);
		return ERR_PTR(ret);
	}

	if (id[0] != CFI_MFR_ATMEL)
		return NULL;

	jedec = be64_to_cpup((__be64 *)id);

	/*
	 * First, try to match device using extended device
	 * information
	 */
	info = jedec_lookup(spi, jedec >> DATAFLASH_SHIFT_EXTID, true);
	if (!IS_ERR(info))
		return info;
	/*
	 * If that fails, make another pass using regular ID
	 * information
	 */
	info = jedec_lookup(spi, jedec >> DATAFLASH_SHIFT_ID, false);
	if (!IS_ERR(info))
		return info;
	/*
	 * Treat other chips as errors ... we won't know the right page
	 * size (it might be binary) even when we can tell which density
	 * class is involved (legacy chip id scheme).
	 */
	dev_warn(&spi->dev, "JEDEC id %016llx not handled\n", jedec);
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
static int dataflash_probe(struct spi_device *spi)
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
		dev_dbg(&spi->dev, "status error %d\n", status);
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
		dev_info(&spi->dev, "unsupported device (%x)\n",
				status & 0x3c);
		status = -ENODEV;
	}

	if (status < 0)
		dev_dbg(&spi->dev, "add_dataflash --> %d\n", status);

	return status;
}

static int dataflash_remove(struct spi_device *spi)
{
	struct dataflash	*flash = spi_get_drvdata(spi);
	int			status;

	dev_dbg(&spi->dev, "remove\n");

	status = mtd_device_unregister(&flash->mtd);
	if (status == 0)
		kfree(flash);
	return status;
}

static struct spi_driver dataflash_driver = {
	.driver = {
		.name		= "mtd_dataflash",
		.of_match_table = of_match_ptr(dataflash_dt_ids),
	},

	.probe		= dataflash_probe,
	.remove		= dataflash_remove,

	/* FIXME:  investigate suspend and resume... */
};

module_spi_driver(dataflash_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrew Victor, David Brownell");
MODULE_DESCRIPTION("MTD DataFlash driver");
MODULE_ALIAS("spi:mtd_dataflash");
