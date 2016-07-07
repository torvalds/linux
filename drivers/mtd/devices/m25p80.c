/*
 * MTD SPI driver for ST M25Pxx (and similar) serial flash chips
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

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/mtd/spi-nor.h>

#define	MAX_CMD_SIZE		6
struct m25p {
	struct spi_device	*spi;
	struct spi_nor		spi_nor;
	u8			command[MAX_CMD_SIZE];
};

static int m25p80_read_reg(struct spi_nor *nor, u8 code, u8 *val, int len)
{
	struct m25p *flash = nor->priv;
	struct spi_device *spi = flash->spi;
	int ret;

	ret = spi_write_then_read(spi, &code, 1, val, len);
	if (ret < 0)
		dev_err(&spi->dev, "error %d reading %x\n", ret, code);

	return ret;
}

static void m25p_addr2cmd(struct spi_nor *nor, unsigned int addr, u8 *cmd)
{
	/* opcode is in cmd[0] */
	cmd[1] = addr >> (nor->addr_width * 8 -  8);
	cmd[2] = addr >> (nor->addr_width * 8 - 16);
	cmd[3] = addr >> (nor->addr_width * 8 - 24);
	cmd[4] = addr >> (nor->addr_width * 8 - 32);
}

static int m25p_cmdsz(struct spi_nor *nor)
{
	return 1 + nor->addr_width;
}

static int m25p80_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct m25p *flash = nor->priv;
	struct spi_device *spi = flash->spi;

	flash->command[0] = opcode;
	if (buf)
		memcpy(&flash->command[1], buf, len);

	return spi_write(spi, flash->command, len + 1);
}

static void m25p80_write(struct spi_nor *nor, loff_t to, size_t len,
			size_t *retlen, const u_char *buf)
{
	struct m25p *flash = nor->priv;
	struct spi_device *spi = flash->spi;
	struct spi_transfer t[2] = {};
	struct spi_message m;
	int cmd_sz = m25p_cmdsz(nor);

	spi_message_init(&m);

	if (nor->program_opcode == SPINOR_OP_AAI_WP && nor->sst_write_second)
		cmd_sz = 1;

	flash->command[0] = nor->program_opcode;
	m25p_addr2cmd(nor, to, flash->command);

	t[0].tx_buf = flash->command;
	t[0].len = cmd_sz;
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	spi_sync(spi, &m);

	*retlen += m.actual_length - cmd_sz;
}

static inline unsigned int m25p80_rx_nbits(struct spi_nor *nor)
{
	switch (nor->flash_read) {
	case SPI_NOR_DUAL:
		return 2;
	case SPI_NOR_QUAD:
		return 4;
	default:
		return 0;
	}
}

/*
 * Read an address range from the nor chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int m25p80_read(struct spi_nor *nor, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	struct m25p *flash = nor->priv;
	struct spi_device *spi = flash->spi;
	struct spi_transfer t[2];
	struct spi_message m;
	unsigned int dummy = nor->read_dummy;

	/* convert the dummy cycles to the number of bytes */
	dummy /= 8;

	if (spi_flash_read_supported(spi)) {
		struct spi_flash_read_message msg;
		int ret;

		memset(&msg, 0, sizeof(msg));

		msg.buf = buf;
		msg.from = from;
		msg.len = len;
		msg.read_opcode = nor->read_opcode;
		msg.addr_width = nor->addr_width;
		msg.dummy_bytes = dummy;
		/* TODO: Support other combinations */
		msg.opcode_nbits = SPI_NBITS_SINGLE;
		msg.addr_nbits = SPI_NBITS_SINGLE;
		msg.data_nbits = m25p80_rx_nbits(nor);

		ret = spi_flash_read(spi, &msg);
		*retlen = msg.retlen;
		return ret;
	}

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	flash->command[0] = nor->read_opcode;
	m25p_addr2cmd(nor, from, flash->command);

	t[0].tx_buf = flash->command;
	t[0].len = m25p_cmdsz(nor) + dummy;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].rx_nbits = m25p80_rx_nbits(nor);
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	spi_sync(spi, &m);

	*retlen = m.actual_length - m25p_cmdsz(nor) - dummy;
	return 0;
}

/*
 * board specific setup should have ensured the SPI clock used here
 * matches what the READ command supports, at least until this driver
 * understands FAST_READ (for clocks over 25 MHz).
 */
static int m25p_probe(struct spi_device *spi)
{
	struct flash_platform_data	*data;
	struct m25p *flash;
	struct spi_nor *nor;
	enum read_mode mode = SPI_NOR_NORMAL;
	char *flash_name;
	int ret;

	data = dev_get_platdata(&spi->dev);

	flash = devm_kzalloc(&spi->dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;

	nor = &flash->spi_nor;

	/* install the hooks */
	nor->read = m25p80_read;
	nor->write = m25p80_write;
	nor->write_reg = m25p80_write_reg;
	nor->read_reg = m25p80_read_reg;

	nor->dev = &spi->dev;
	spi_nor_set_flash_node(nor, spi->dev.of_node);
	nor->priv = flash;

	spi_set_drvdata(spi, flash);
	flash->spi = spi;

	if (spi->mode & SPI_RX_QUAD)
		mode = SPI_NOR_QUAD;
	else if (spi->mode & SPI_RX_DUAL)
		mode = SPI_NOR_DUAL;

	if (data && data->name)
		nor->mtd.name = data->name;

	/* For some (historical?) reason many platforms provide two different
	 * names in flash_platform_data: "name" and "type". Quite often name is
	 * set to "m25p80" and then "type" provides a real chip name.
	 * If that's the case, respect "type" and ignore a "name".
	 */
	if (data && data->type)
		flash_name = data->type;
	else if (!strcmp(spi->modalias, "spi-nor"))
		flash_name = NULL; /* auto-detect */
	else
		flash_name = spi->modalias;

	ret = spi_nor_scan(nor, flash_name, mode);
	if (ret)
		return ret;

	return mtd_device_register(&nor->mtd, data ? data->parts : NULL,
				   data ? data->nr_parts : 0);
}


static int m25p_remove(struct spi_device *spi)
{
	struct m25p	*flash = spi_get_drvdata(spi);

	/* Clean up MTD stuff. */
	return mtd_device_unregister(&flash->spi_nor.mtd);
}

/*
 * Do NOT add to this array without reading the following:
 *
 * Historically, many flash devices are bound to this driver by their name. But
 * since most of these flash are compatible to some extent, and their
 * differences can often be differentiated by the JEDEC read-ID command, we
 * encourage new users to add support to the spi-nor library, and simply bind
 * against a generic string here (e.g., "jedec,spi-nor").
 *
 * Many flash names are kept here in this list (as well as in spi-nor.c) to
 * keep them available as module aliases for existing platforms.
 */
static const struct spi_device_id m25p_ids[] = {
	/*
	 * Allow non-DT platform devices to bind to the "spi-nor" modalias, and
	 * hack around the fact that the SPI core does not provide uevent
	 * matching for .of_match_table
	 */
	{"spi-nor"},

	/*
	 * Entries not used in DTs that should be safe to drop after replacing
	 * them with "spi-nor" in platform data.
	 */
	{"s25sl064a"},	{"w25x16"},	{"m25p10"},	{"m25px64"},

	/*
	 * Entries that were used in DTs without "jedec,spi-nor" fallback and
	 * should be kept for backward compatibility.
	 */
	{"at25df321a"},	{"at25df641"},	{"at26df081a"},
	{"mr25h256"},
	{"mx25l4005a"},	{"mx25l1606e"},	{"mx25l6405d"},	{"mx25l12805d"},
	{"mx25l25635e"},{"mx66l51235l"},
	{"n25q064"},	{"n25q128a11"},	{"n25q128a13"},	{"n25q512a"},
	{"s25fl256s1"},	{"s25fl512s"},	{"s25sl12801"},	{"s25fl008k"},
	{"s25fl064k"},
	{"sst25vf040b"},{"sst25vf016b"},{"sst25vf032b"},{"sst25wf040"},
	{"m25p40"},	{"m25p80"},	{"m25p16"},	{"m25p32"},
	{"m25p64"},	{"m25p128"},
	{"w25x80"},	{"w25x32"},	{"w25q32"},	{"w25q32dw"},
	{"w25q80bl"},	{"w25q128"},	{"w25q256"},

	/* Flashes that can't be detected using JEDEC */
	{"m25p05-nonjedec"},	{"m25p10-nonjedec"},	{"m25p20-nonjedec"},
	{"m25p40-nonjedec"},	{"m25p80-nonjedec"},	{"m25p16-nonjedec"},
	{"m25p32-nonjedec"},	{"m25p64-nonjedec"},	{"m25p128-nonjedec"},

	{ },
};
MODULE_DEVICE_TABLE(spi, m25p_ids);

static const struct of_device_id m25p_of_table[] = {
	/*
	 * Generic compatibility for SPI NOR that can be identified by the
	 * JEDEC READ ID opcode (0x9F). Use this, if possible.
	 */
	{ .compatible = "jedec,spi-nor" },
	{}
};
MODULE_DEVICE_TABLE(of, m25p_of_table);

static struct spi_driver m25p80_driver = {
	.driver = {
		.name	= "m25p80",
		.of_match_table = m25p_of_table,
	},
	.id_table	= m25p_ids,
	.probe	= m25p_probe,
	.remove	= m25p_remove,

	/* REVISIT: many of these chips have deep power-down modes, which
	 * should clearly be entered on suspend() to minimize power use.
	 * And also when they're otherwise idle...
	 */
};

module_spi_driver(m25p80_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("MTD SPI driver for ST M25Pxx flash chips");
