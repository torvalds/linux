/*
 * Atmel DataFlash driver for Atmel AT91RM9200 (Thunder)
 * This is a largely modified version of at91_dataflash.c that
 * supports AT26xxx dataflash chips. The original driver supports
 * AT45xxx chips.
 *
 * Note: This driver was only tested with an AT26F004. It should be
 * easy to make it work with other AT26xxx dataflash devices, though.
 *
 * Copyright (C) 2007 Hans J. Koch <hjk@linutronix.de>
 * original Copyright (C) SAN People (Pty) Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>

#include <asm/arch/at91_spi.h>

#define DATAFLASH_MAX_DEVICES	4	/* max number of dataflash devices */

#define MANUFACTURER_ID_ATMEL		0x1F

/* command codes */

#define AT26_OP_READ_STATUS		0x05
#define AT26_OP_READ_DEV_ID		0x9F
#define AT26_OP_ERASE_PAGE_4K		0x20
#define AT26_OP_READ_ARRAY_FAST		0x0B
#define AT26_OP_SEQUENTIAL_WRITE	0xAF
#define AT26_OP_WRITE_ENABLE		0x06
#define AT26_OP_WRITE_DISABLE		0x04
#define AT26_OP_SECTOR_PROTECT		0x36
#define AT26_OP_SECTOR_UNPROTECT	0x39

/* status register bits */

#define AT26_STATUS_BUSY		0x01
#define AT26_STATUS_WRITE_ENABLE	0x02

struct dataflash_local
{
	int spi;			/* SPI chip-select number */
	unsigned int page_size;		/* number of bytes per page */
};


/* Detected DataFlash devices */
static struct mtd_info* mtd_devices[DATAFLASH_MAX_DEVICES];
static int nr_devices = 0;

/* Allocate a single SPI transfer descriptor.  We're assuming that if multiple
   SPI transfers occur at the same time, spi_access_bus() will serialize them.
   If this is not valid, then either (i) each dataflash 'priv' structure
   needs it's own transfer descriptor, (ii) we lock this one, or (iii) use
   another mechanism.   */
static struct spi_transfer_list* spi_transfer_desc;

/*
 * Perform a SPI transfer to access the DataFlash device.
 */
static int do_spi_transfer(int nr, char* tx, int tx_len, char* rx, int rx_len,
		char* txnext, int txnext_len, char* rxnext, int rxnext_len)
{
	struct spi_transfer_list* list = spi_transfer_desc;

	list->tx[0] = tx;	list->txlen[0] = tx_len;
	list->rx[0] = rx;	list->rxlen[0] = rx_len;

	list->tx[1] = txnext; 	list->txlen[1] = txnext_len;
	list->rx[1] = rxnext;	list->rxlen[1] = rxnext_len;

	list->nr_transfers = nr;
	/* Note: spi_transfer() always returns 0, there are no error checks */
	return spi_transfer(list);
}

/*
 * Return the status of the DataFlash device.
 */
static unsigned char at91_dataflash26_status(void)
{
	unsigned char command[2];

	command[0] = AT26_OP_READ_STATUS;
	command[1] = 0;

	do_spi_transfer(1, command, 2, command, 2, NULL, 0, NULL, 0);

	return command[1];
}

/*
 * Poll the DataFlash device until it is READY.
 */
static unsigned char at91_dataflash26_waitready(void)
{
	unsigned char status;

	while (1) {
		status = at91_dataflash26_status();
		if (!(status & AT26_STATUS_BUSY))
			return status;
	}
}

/*
 * Enable/disable write access
 */
 static void at91_dataflash26_write_enable(int enable)
{
	unsigned char cmd[2];

	DEBUG(MTD_DEBUG_LEVEL3, "write_enable: enable=%i\n", enable);

	if (enable)
		cmd[0] = AT26_OP_WRITE_ENABLE;
	else
		cmd[0] = AT26_OP_WRITE_DISABLE;
	cmd[1] = 0;

	do_spi_transfer(1, cmd, 2, cmd, 2, NULL, 0, NULL, 0);
}

/*
 * Protect/unprotect sector
 */
 static void at91_dataflash26_sector_protect(loff_t addr, int protect)
{
	unsigned char cmd[4];

	DEBUG(MTD_DEBUG_LEVEL3, "sector_protect: addr=0x%06x prot=%d\n",
	       addr, protect);

	if (protect)
		cmd[0] = AT26_OP_SECTOR_PROTECT;
	else
		cmd[0] = AT26_OP_SECTOR_UNPROTECT;
	cmd[1] = (addr & 0x00FF0000) >> 16;
	cmd[2] = (addr & 0x0000FF00) >> 8;
	cmd[3] = (addr & 0x000000FF);

	do_spi_transfer(1, cmd, 4, cmd, 4, NULL, 0, NULL, 0);
}

/*
 * Erase blocks of flash.
 */
static int at91_dataflash26_erase(struct mtd_info *mtd,
				  struct erase_info *instr)
{
	struct dataflash_local *priv = (struct dataflash_local *) mtd->priv;
	unsigned char cmd[4];

	DEBUG(MTD_DEBUG_LEVEL1, "dataflash_erase: addr=0x%06x len=%i\n",
	       instr->addr, instr->len);

	/* Sanity checks */
	if (priv->page_size != 4096)
		return -EINVAL; /* Can't handle other sizes at the moment */

	if (   ((instr->len % mtd->erasesize) != 0)
	    || ((instr->len % priv->page_size) != 0)
	    || ((instr->addr % priv->page_size) != 0)
	    || ((instr->addr + instr->len) > mtd->size))
		return -EINVAL;

	spi_access_bus(priv->spi);

	while (instr->len > 0) {
		at91_dataflash26_write_enable(1);
		at91_dataflash26_sector_protect(instr->addr, 0);
		at91_dataflash26_write_enable(1);
		cmd[0] = AT26_OP_ERASE_PAGE_4K;
		cmd[1] = (instr->addr & 0x00FF0000) >> 16;
		cmd[2] = (instr->addr & 0x0000FF00) >> 8;
		cmd[3] = (instr->addr & 0x000000FF);

		DEBUG(MTD_DEBUG_LEVEL3, "ERASE: (0x%02x) 0x%02x 0x%02x"
					"0x%02x\n",
			cmd[0], cmd[1], cmd[2], cmd[3]);

		do_spi_transfer(1, cmd, 4, cmd, 4, NULL, 0, NULL, 0);
		at91_dataflash26_waitready();

		instr->addr += priv->page_size;	 /* next page */
		instr->len -= priv->page_size;
	}

	at91_dataflash26_write_enable(0);
	spi_release_bus(priv->spi);

	/* Inform MTD subsystem that erase is complete */
	instr->state = MTD_ERASE_DONE;
	if (instr->callback)
		instr->callback(instr);

	return 0;
}

/*
 * Read from the DataFlash device.
 *   from   : Start offset in flash device
 *   len    : Number of bytes to read
 *   retlen : Number of bytes actually read
 *   buf    : Buffer that will receive data
 */
static int at91_dataflash26_read(struct mtd_info *mtd, loff_t from, size_t len,
				 size_t *retlen, u_char *buf)
{
	struct dataflash_local *priv = (struct dataflash_local *) mtd->priv;
	unsigned char cmd[5];

	DEBUG(MTD_DEBUG_LEVEL1, "dataflash_read: %lli .. %lli\n",
	      from, from+len);

	*retlen = 0;

	/* Sanity checks */
	if (!len)
		return 0;
	if (from + len > mtd->size)
		return -EINVAL;

	cmd[0] = AT26_OP_READ_ARRAY_FAST;
	cmd[1] = (from & 0x00FF0000) >> 16;
	cmd[2] = (from & 0x0000FF00) >> 8;
	cmd[3] = (from & 0x000000FF);
	/* cmd[4] is a "Don't care" byte  */

	DEBUG(MTD_DEBUG_LEVEL3, "READ: (0x%02x) 0x%02x 0x%02x 0x%02x\n",
	       cmd[0], cmd[1], cmd[2], cmd[3]);

	spi_access_bus(priv->spi);
	do_spi_transfer(2, cmd, 5, cmd, 5, buf, len, buf, len);
	spi_release_bus(priv->spi);

	*retlen = len;
	return 0;
}

/*
 * Write to the DataFlash device.
 *   to     : Start offset in flash device
 *   len    : Number of bytes to write
 *   retlen : Number of bytes actually written
 *   buf    : Buffer containing the data
 */
static int at91_dataflash26_write(struct mtd_info *mtd, loff_t to, size_t len,
				  size_t *retlen, const u_char *buf)
{
	struct dataflash_local *priv = (struct dataflash_local *) mtd->priv;
	unsigned int addr, buf_index = 0;
	int ret = -EIO, sector, last_sector;
	unsigned char status, cmd[5];

	DEBUG(MTD_DEBUG_LEVEL1, "dataflash_write: %lli .. %lli\n", to, to+len);

	*retlen = 0;

	/* Sanity checks */
	if (!len)
		return 0;
	if (to + len > mtd->size)
		return -EINVAL;

	spi_access_bus(priv->spi);

	addr = to;
	last_sector = -1;

	while (buf_index < len) {
		sector = addr / priv->page_size;
		/* Write first byte if a new sector begins */
		if (sector != last_sector) {
			at91_dataflash26_write_enable(1);
			at91_dataflash26_sector_protect(addr, 0);
			at91_dataflash26_write_enable(1);

			/* Program first byte of a new sector */
			cmd[0] = AT26_OP_SEQUENTIAL_WRITE;
			cmd[1] = (addr & 0x00FF0000) >> 16;
			cmd[2] = (addr & 0x0000FF00) >> 8;
			cmd[3] = (addr & 0x000000FF);
			cmd[4] = buf[buf_index++];
			do_spi_transfer(1, cmd, 5, cmd, 5, NULL, 0, NULL, 0);
			status = at91_dataflash26_waitready();
			addr++;
			/* On write errors, the chip resets the write enable
			   flag. This also happens after the last byte of a
			   sector is successfully programmed. */
			if (   ( !(status & AT26_STATUS_WRITE_ENABLE))
			    && ((addr % priv->page_size) != 0) ) {
				DEBUG(MTD_DEBUG_LEVEL1,
					"write error1: addr=0x%06x, "
					"status=0x%02x\n", addr, status);
				goto write_err;
			}
			(*retlen)++;
			last_sector = sector;
		}

		/* Write subsequent bytes in the same sector */
		cmd[0] = AT26_OP_SEQUENTIAL_WRITE;
		cmd[1] = buf[buf_index++];
		do_spi_transfer(1, cmd, 2, cmd, 2, NULL, 0, NULL, 0);
		status = at91_dataflash26_waitready();
		addr++;

		if (   ( !(status & AT26_STATUS_WRITE_ENABLE))
		    && ((addr % priv->page_size) != 0) ) {
			DEBUG(MTD_DEBUG_LEVEL1, "write error2: addr=0x%06x, "
				"status=0x%02x\n", addr, status);
			goto write_err;
		}

		(*retlen)++;
	}

	ret = 0;
	at91_dataflash26_write_enable(0);
write_err:
	spi_release_bus(priv->spi);
	return ret;
}

/*
 * Initialize and register DataFlash device with MTD subsystem.
 */
static int __init add_dataflash(int channel, char *name, int nr_pages,
				int pagesize)
{
	struct mtd_info *device;
	struct dataflash_local *priv;

	if (nr_devices >= DATAFLASH_MAX_DEVICES) {
		printk(KERN_ERR "at91_dataflash26: Too many devices "
				"detected\n");
		return 0;
	}

	device = kzalloc(sizeof(struct mtd_info) + strlen(name) + 8,
			 GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	device->name = (char *)&device[1];
	sprintf(device->name, "%s.spi%d", name, channel);
	device->size = nr_pages * pagesize;
	device->erasesize = pagesize;
	device->owner = THIS_MODULE;
	device->type = MTD_DATAFLASH;
	device->flags = MTD_CAP_NORFLASH;
	device->erase = at91_dataflash26_erase;
	device->read = at91_dataflash26_read;
	device->write = at91_dataflash26_write;

	priv = (struct dataflash_local *)kzalloc(sizeof(struct dataflash_local),
		GFP_KERNEL);
	if (!priv) {
		kfree(device);
		return -ENOMEM;
	}

	priv->spi = channel;
	priv->page_size = pagesize;
	device->priv = priv;

	mtd_devices[nr_devices] = device;
	nr_devices++;
	printk(KERN_INFO "at91_dataflash26: %s detected [spi%i] (%i bytes)\n",
	       name, channel, device->size);

	return add_mtd_device(device);
}

/*
 * Detect and initialize DataFlash device connected to specified SPI channel.
 *
 */

struct dataflash26_types {
	unsigned char id0;
	unsigned char id1;
	char *name;
	int pagesize;
	int nr_pages;
};

struct dataflash26_types df26_types[] = {
	{
		.id0 = 0x04,
		.id1 = 0x00,
		.name = "AT26F004",
		.pagesize = 4096,
		.nr_pages = 128,
	},
	{
		.id0 = 0x45,
		.id1 = 0x01,
		.name = "AT26DF081A", /* Not tested ! */
		.pagesize = 4096,
		.nr_pages = 256,
	},
};

static int __init at91_dataflash26_detect(int channel)
{
	unsigned char status, cmd[5];
	int i;

	spi_access_bus(channel);
	status = at91_dataflash26_status();

	if (status == 0 || status == 0xff) {
		printk(KERN_ERR "at91_dataflash26_detect: status error %d\n",
			status);
		spi_release_bus(channel);
		return -ENODEV;
	}

	cmd[0] = AT26_OP_READ_DEV_ID;
	do_spi_transfer(1, cmd, 5, cmd, 5, NULL, 0, NULL, 0);
	spi_release_bus(channel);

	if (cmd[1] != MANUFACTURER_ID_ATMEL)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(df26_types); i++) {
		if (   cmd[2] == df26_types[i].id0
		    && cmd[3] == df26_types[i].id1)
			return add_dataflash(channel,
						df26_types[i].name,
						df26_types[i].nr_pages,
						df26_types[i].pagesize);
	}

	printk(KERN_ERR "at91_dataflash26_detect: Unsupported device "
			"(0x%02x/0x%02x)\n", cmd[2], cmd[3]);
	return -ENODEV;
}

static int __init at91_dataflash26_init(void)
{
	spi_transfer_desc = kmalloc(sizeof(struct spi_transfer_list),
				    GFP_KERNEL);
	if (!spi_transfer_desc)
		return -ENOMEM;

	/* DataFlash (SPI chip select 0) */
	at91_dataflash26_detect(0);

#ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
	/* DataFlash card (SPI chip select 3) */
	at91_dataflash26_detect(3);
#endif
	return 0;
}

static void __exit at91_dataflash26_exit(void)
{
	int i;

	for (i = 0; i < DATAFLASH_MAX_DEVICES; i++) {
		if (mtd_devices[i]) {
			del_mtd_device(mtd_devices[i]);
			kfree(mtd_devices[i]->priv);
			kfree(mtd_devices[i]);
		}
	}
	nr_devices = 0;
	kfree(spi_transfer_desc);
}

module_init(at91_dataflash26_init);
module_exit(at91_dataflash26_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hans J. Koch");
MODULE_DESCRIPTION("DataFlash AT26xxx driver for Atmel AT91RM9200");
