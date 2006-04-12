/*
 * MTD SPI driver for ST M25Pxx flash chips
 *
 * Author: Mike Lavender, mike@steroidmicros.com
 *
 * Copyright (c) 2005, Intec Automation Inc.
 *
 * Some parts are based on lart.c by Abraham Van Der Merwe
 *
 * Cleaned up and generalized based on mtd_dataflash.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

#include <asm/semaphore.h>


/* NOTE: AT 25F and SST 25LF series are very similar,
 * but commands for sector erase and chip id differ...
 */

#define FLASH_PAGESIZE		256

/* Flash opcodes. */
#define	OPCODE_WREN		6	/* Write enable */
#define	OPCODE_RDSR		5	/* Read status register */
#define	OPCODE_READ		3	/* Read data bytes */
#define	OPCODE_PP		2	/* Page program */
#define	OPCODE_SE		0xd8	/* Sector erase */
#define	OPCODE_RES		0xab	/* Read Electronic Signature */
#define	OPCODE_RDID		0x9f	/* Read JEDEC ID */

/* Status Register bits. */
#define	SR_WIP			1	/* Write in progress */
#define	SR_WEL			2	/* Write enable latch */
#define	SR_BP0			4	/* Block protect 0 */
#define	SR_BP1			8	/* Block protect 1 */
#define	SR_BP2			0x10	/* Block protect 2 */
#define	SR_SRWD			0x80	/* SR write protect */

/* Define max times to check status register before we give up. */
#define	MAX_READY_WAIT_COUNT	100000


#ifdef CONFIG_MTD_PARTITIONS
#define	mtd_has_partitions()	(1)
#else
#define	mtd_has_partitions()	(0)
#endif

/****************************************************************************/

struct m25p {
	struct spi_device	*spi;
	struct semaphore	lock;
	struct mtd_info		mtd;
	unsigned		partitioned;
	u8			command[4];
};

static inline struct m25p *mtd_to_m25p(struct mtd_info *mtd)
{
	return container_of(mtd, struct m25p, mtd);
}

/****************************************************************************/

/*
 * Internal helper functions
 */

/*
 * Read the status register, returning its value in the location
 * Return the status register value.
 * Returns negative if error occurred.
 */
static int read_sr(struct m25p *flash)
{
	ssize_t retval;
	u8 code = OPCODE_RDSR;
	u8 val;

	retval = spi_write_then_read(flash->spi, &code, 1, &val, 1);

	if (retval < 0) {
		dev_err(&flash->spi->dev, "error %d reading SR\n",
				(int) retval);
		return retval;
	}

	return val;
}


/*
 * Set write enable latch with Write Enable command.
 * Returns negative if error occurred.
 */
static inline int write_enable(struct m25p *flash)
{
	u8	code = OPCODE_WREN;

	return spi_write_then_read(flash->spi, &code, 1, NULL, 0);
}


/*
 * Service routine to read status register until ready, or timeout occurs.
 * Returns non-zero if error.
 */
static int wait_till_ready(struct m25p *flash)
{
	int count;
	int sr;

	/* one chip guarantees max 5 msec wait here after page writes,
	 * but potentially three seconds (!) after page erase.
	 */
	for (count = 0; count < MAX_READY_WAIT_COUNT; count++) {
		if ((sr = read_sr(flash)) < 0)
			break;
		else if (!(sr & SR_WIP))
			return 0;

		/* REVISIT sometimes sleeping would be best */
	}

	return 1;
}


/*
 * Erase one sector of flash memory at offset ``offset'' which is any
 * address within the sector which should be erased.
 *
 * Returns 0 if successful, non-zero otherwise.
 */
static int erase_sector(struct m25p *flash, u32 offset)
{
	DEBUG(MTD_DEBUG_LEVEL3, "%s: %s at 0x%08x\n", flash->spi->dev.bus_id,
			__FUNCTION__, offset);

	/* Wait until finished previous write command. */
	if (wait_till_ready(flash))
		return 1;

	/* Send write enable, then erase commands. */
	write_enable(flash);

	/* Set up command buffer. */
	flash->command[0] = OPCODE_SE;
	flash->command[1] = offset >> 16;
	flash->command[2] = offset >> 8;
	flash->command[3] = offset;

	spi_write(flash->spi, flash->command, sizeof(flash->command));

	return 0;
}

/****************************************************************************/

/*
 * MTD implementation
 */

/*
 * Erase an address range on the flash chip.  The address range may extend
 * one or more erase sectors.  Return an error is there is a problem erasing.
 */
static int m25p80_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	u32 addr,len;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: %s %s 0x%08x, len %d\n",
			flash->spi->dev.bus_id, __FUNCTION__, "at",
			(u32)instr->addr, instr->len);

	/* sanity checks */
	if (instr->addr + instr->len > flash->mtd.size)
		return -EINVAL;
	if ((instr->addr % mtd->erasesize) != 0
			|| (instr->len % mtd->erasesize) != 0) {
		return -EINVAL;
	}

	addr = instr->addr;
	len = instr->len;

  	down(&flash->lock);

	/* now erase those sectors */
	while (len) {
		if (erase_sector(flash, addr)) {
			instr->state = MTD_ERASE_FAILED;
			up(&flash->lock);
			return -EIO;
		}

		addr += mtd->erasesize;
		len -= mtd->erasesize;
	}

  	up(&flash->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

/*
 * Read an address range from the flash chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int m25p80_read(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	struct spi_transfer t[2];
	struct spi_message m;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: %s %s 0x%08x, len %zd\n",
			flash->spi->dev.bus_id, __FUNCTION__, "from",
			(u32)from, len);

	/* sanity checks */
	if (!len)
		return 0;

	if (from + len > flash->mtd.size)
		return -EINVAL;

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	t[0].tx_buf = flash->command;
	t[0].len = sizeof(flash->command);
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	/* Byte count starts at zero. */
	if (retlen)
		*retlen = 0;

	down(&flash->lock);

	/* Wait till previous write/erase is done. */
	if (wait_till_ready(flash)) {
		/* REVISIT status return?? */
		up(&flash->lock);
		return 1;
	}

	/* NOTE:  OPCODE_FAST_READ (if available) is faster... */

	/* Set up the write data buffer. */
	flash->command[0] = OPCODE_READ;
	flash->command[1] = from >> 16;
	flash->command[2] = from >> 8;
	flash->command[3] = from;

	spi_sync(flash->spi, &m);

	*retlen = m.actual_length - sizeof(flash->command);

  	up(&flash->lock);

	return 0;
}

/*
 * Write an address range to the flash chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int m25p80_write(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	u32 page_offset, page_size;
	struct spi_transfer t[2];
	struct spi_message m;

	DEBUG(MTD_DEBUG_LEVEL2, "%s: %s %s 0x%08x, len %zd\n",
			flash->spi->dev.bus_id, __FUNCTION__, "to",
			(u32)to, len);

	if (retlen)
		*retlen = 0;

	/* sanity checks */
	if (!len)
		return(0);

	if (to + len > flash->mtd.size)
		return -EINVAL;

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	t[0].tx_buf = flash->command;
	t[0].len = sizeof(flash->command);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	spi_message_add_tail(&t[1], &m);

  	down(&flash->lock);

	/* Wait until finished previous write command. */
	if (wait_till_ready(flash))
		return 1;

	write_enable(flash);

	/* Set up the opcode in the write buffer. */
	flash->command[0] = OPCODE_PP;
	flash->command[1] = to >> 16;
	flash->command[2] = to >> 8;
	flash->command[3] = to;

	/* what page do we start with? */
	page_offset = to % FLASH_PAGESIZE;

	/* do all the bytes fit onto one page? */
	if (page_offset + len <= FLASH_PAGESIZE) {
		t[1].len = len;

		spi_sync(flash->spi, &m);

		*retlen = m.actual_length - sizeof(flash->command);
	} else {
		u32 i;

		/* the size of data remaining on the first page */
		page_size = FLASH_PAGESIZE - page_offset;

		t[1].len = page_size;
		spi_sync(flash->spi, &m);

		*retlen = m.actual_length - sizeof(flash->command);

		/* write everything in PAGESIZE chunks */
		for (i = page_size; i < len; i += page_size) {
			page_size = len - i;
			if (page_size > FLASH_PAGESIZE)
				page_size = FLASH_PAGESIZE;

			/* write the next page to flash */
			flash->command[1] = (to + i) >> 16;
			flash->command[2] = (to + i) >> 8;
			flash->command[3] = (to + i);

			t[1].tx_buf = buf + i;
			t[1].len = page_size;

			wait_till_ready(flash);

			write_enable(flash);

			spi_sync(flash->spi, &m);

			if (retlen)
				*retlen += m.actual_length
					- sizeof(flash->command);
	        }
 	}

	up(&flash->lock);

	return 0;
}


/****************************************************************************/

/*
 * SPI device driver setup and teardown
 */

struct flash_info {
	char		*name;
	u8		id;
	u16		jedec_id;
	unsigned	sector_size;
	unsigned	n_sectors;
};

static struct flash_info __devinitdata m25p_data [] = {
	/* REVISIT: fill in JEDEC ids, for parts that have them */
	{ "m25p05", 0x05, 0x0000, 32 * 1024, 2 },
	{ "m25p10", 0x10, 0x0000, 32 * 1024, 4 },
	{ "m25p20", 0x11, 0x0000, 64 * 1024, 4 },
	{ "m25p40", 0x12, 0x0000, 64 * 1024, 8 },
	{ "m25p80", 0x13, 0x0000, 64 * 1024, 16 },
	{ "m25p16", 0x14, 0x0000, 64 * 1024, 32 },
	{ "m25p32", 0x15, 0x0000, 64 * 1024, 64 },
	{ "m25p64", 0x16, 0x2017, 64 * 1024, 128 },
};

/*
 * board specific setup should have ensured the SPI clock used here
 * matches what the READ command supports, at least until this driver
 * understands FAST_READ (for clocks over 25 MHz).
 */
static int __devinit m25p_probe(struct spi_device *spi)
{
	struct flash_platform_data	*data;
	struct m25p			*flash;
	struct flash_info		*info;
	unsigned			i;

	/* Platform data helps sort out which chip type we have, as
	 * well as how this board partitions it.
	 */
	data = spi->dev.platform_data;
	if (!data || !data->type) {
		/* FIXME some chips can identify themselves with RES
		 * or JEDEC get-id commands.  Try them ...
		 */
		DEBUG(MTD_DEBUG_LEVEL1, "%s: no chip id\n",
				flash->spi->dev.bus_id);
		return -ENODEV;
	}

	for (i = 0, info = m25p_data; i < ARRAY_SIZE(m25p_data); i++, info++) {
		if (strcmp(data->type, info->name) == 0)
			break;
	}
	if (i == ARRAY_SIZE(m25p_data)) {
		DEBUG(MTD_DEBUG_LEVEL1, "%s: unrecognized id %s\n",
				flash->spi->dev.bus_id, data->type);
		return -ENODEV;
	}

	flash = kzalloc(sizeof *flash, SLAB_KERNEL);
	if (!flash)
		return -ENOMEM;

	flash->spi = spi;
	init_MUTEX(&flash->lock);
	dev_set_drvdata(&spi->dev, flash);

	if (data->name)
		flash->mtd.name = data->name;
	else
		flash->mtd.name = spi->dev.bus_id;

	flash->mtd.type = MTD_NORFLASH;
	flash->mtd.flags = MTD_CAP_NORFLASH;
	flash->mtd.size = info->sector_size * info->n_sectors;
	flash->mtd.erasesize = info->sector_size;
	flash->mtd.erase = m25p80_erase;
	flash->mtd.read = m25p80_read;
	flash->mtd.write = m25p80_write;

	dev_info(&spi->dev, "%s (%d Kbytes)\n", info->name,
			flash->mtd.size / 1024);

	DEBUG(MTD_DEBUG_LEVEL2,
		"mtd .name = %s, .size = 0x%.8x (%uM) "
			".erasesize = 0x%.8x (%uK) .numeraseregions = %d\n",
		flash->mtd.name,
		flash->mtd.size, flash->mtd.size / (1024*1024),
		flash->mtd.erasesize, flash->mtd.erasesize / 1024,
		flash->mtd.numeraseregions);

	if (flash->mtd.numeraseregions)
		for (i = 0; i < flash->mtd.numeraseregions; i++)
			DEBUG(MTD_DEBUG_LEVEL2,
				"mtd.eraseregions[%d] = { .offset = 0x%.8x, "
				".erasesize = 0x%.8x (%uK), "
				".numblocks = %d }\n",
				i, flash->mtd.eraseregions[i].offset,
				flash->mtd.eraseregions[i].erasesize,
				flash->mtd.eraseregions[i].erasesize / 1024,
				flash->mtd.eraseregions[i].numblocks);


	/* partitions should match sector boundaries; and it may be good to
	 * use readonly partitions for writeprotected sectors (BP2..BP0).
	 */
	if (mtd_has_partitions()) {
		struct mtd_partition	*parts = NULL;
		int			nr_parts = 0;

#ifdef CONFIG_MTD_CMDLINE_PARTS
		static const char *part_probes[] = { "cmdlinepart", NULL, };

		nr_parts = parse_mtd_partitions(&flash->mtd,
				part_probes, &parts, 0);
#endif

		if (nr_parts <= 0 && data && data->parts) {
			parts = data->parts;
			nr_parts = data->nr_parts;
		}

		if (nr_parts > 0) {
			for (i = 0; i < data->nr_parts; i++) {
				DEBUG(MTD_DEBUG_LEVEL2, "partitions[%d] = "
					"{.name = %s, .offset = 0x%.8x, "
						".size = 0x%.8x (%uK) }\n",
					i, data->parts[i].name,
					data->parts[i].offset,
					data->parts[i].size,
					data->parts[i].size / 1024);
			}
			flash->partitioned = 1;
			return add_mtd_partitions(&flash->mtd, parts, nr_parts);
		}
	} else if (data->nr_parts)
		dev_warn(&spi->dev, "ignoring %d default partitions on %s\n",
				data->nr_parts, data->name);

	return add_mtd_device(&flash->mtd) == 1 ? -ENODEV : 0;
}


static int __devexit m25p_remove(struct spi_device *spi)
{
	struct m25p	*flash = dev_get_drvdata(&spi->dev);
	int		status;

	/* Clean up MTD stuff. */
	if (mtd_has_partitions() && flash->partitioned)
		status = del_mtd_partitions(&flash->mtd);
	else
		status = del_mtd_device(&flash->mtd);
	if (status == 0)
		kfree(flash);
	return 0;
}


static struct spi_driver m25p80_driver = {
	.driver = {
		.name	= "m25p80",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= m25p_probe,
	.remove	= __devexit_p(m25p_remove),
};


static int m25p80_init(void)
{
	return spi_register_driver(&m25p80_driver);
}


static void m25p80_exit(void)
{
	spi_unregister_driver(&m25p80_driver);
}


module_init(m25p80_init);
module_exit(m25p80_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("MTD SPI driver for ST M25Pxx flash chips");
