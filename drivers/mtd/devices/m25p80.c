// SPDX-License-Identifier: GPL-2.0-only
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
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/spi/flash.h>
#include <linux/mtd/spi-nor.h>

struct m25p {
	struct spi_mem		*spimem;
	struct spi_nor		spi_nor;
};

static int m25p80_read_reg(struct spi_nor *nor, u8 code, u8 *val, int len)
{
	struct m25p *flash = nor->priv;
	struct spi_mem_op op = SPI_MEM_OP(SPI_MEM_OP_CMD(code, 1),
					  SPI_MEM_OP_NO_ADDR,
					  SPI_MEM_OP_NO_DUMMY,
					  SPI_MEM_OP_DATA_IN(len, NULL, 1));
	void *scratchbuf;
	int ret;

	scratchbuf = kmalloc(len, GFP_KERNEL);
	if (!scratchbuf)
		return -ENOMEM;

	op.data.buf.in = scratchbuf;
	ret = spi_mem_exec_op(flash->spimem, &op);
	if (ret < 0)
		dev_err(&flash->spimem->spi->dev, "error %d reading %x\n", ret,
			code);
	else
		memcpy(val, scratchbuf, len);

	kfree(scratchbuf);

	return ret;
}

static int m25p80_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct m25p *flash = nor->priv;
	struct spi_mem_op op = SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 1),
					  SPI_MEM_OP_NO_ADDR,
					  SPI_MEM_OP_NO_DUMMY,
					  SPI_MEM_OP_DATA_OUT(len, NULL, 1));
	void *scratchbuf;
	int ret;

	scratchbuf = kmemdup(buf, len, GFP_KERNEL);
	if (!scratchbuf)
		return -ENOMEM;

	op.data.buf.out = scratchbuf;
	ret = spi_mem_exec_op(flash->spimem, &op);
	kfree(scratchbuf);

	return ret;
}

static ssize_t m25p80_write(struct spi_nor *nor, loff_t to, size_t len,
			    const u_char *buf)
{
	struct m25p *flash = nor->priv;
	struct spi_mem_op op =
			SPI_MEM_OP(SPI_MEM_OP_CMD(nor->program_opcode, 1),
				   SPI_MEM_OP_ADDR(nor->addr_width, to, 1),
				   SPI_MEM_OP_NO_DUMMY,
				   SPI_MEM_OP_DATA_OUT(len, buf, 1));
	int ret;

	/* get transfer protocols. */
	op.cmd.buswidth = spi_nor_get_protocol_inst_nbits(nor->write_proto);
	op.addr.buswidth = spi_nor_get_protocol_addr_nbits(nor->write_proto);
	op.data.buswidth = spi_nor_get_protocol_data_nbits(nor->write_proto);

	if (nor->program_opcode == SPINOR_OP_AAI_WP && nor->sst_write_second)
		op.addr.nbytes = 0;

	ret = spi_mem_adjust_op_size(flash->spimem, &op);
	if (ret)
		return ret;
	op.data.nbytes = len < op.data.nbytes ? len : op.data.nbytes;

	ret = spi_mem_exec_op(flash->spimem, &op);
	if (ret)
		return ret;

	return op.data.nbytes;
}

/*
 * Read an address range from the nor chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static ssize_t m25p80_read(struct spi_nor *nor, loff_t from, size_t len,
			   u_char *buf)
{
	struct m25p *flash = nor->priv;
	struct spi_mem_op op =
			SPI_MEM_OP(SPI_MEM_OP_CMD(nor->read_opcode, 1),
				   SPI_MEM_OP_ADDR(nor->addr_width, from, 1),
				   SPI_MEM_OP_DUMMY(nor->read_dummy, 1),
				   SPI_MEM_OP_DATA_IN(len, buf, 1));
	size_t remaining = len;
	int ret;

	/* get transfer protocols. */
	op.cmd.buswidth = spi_nor_get_protocol_inst_nbits(nor->read_proto);
	op.addr.buswidth = spi_nor_get_protocol_addr_nbits(nor->read_proto);
	op.dummy.buswidth = op.addr.buswidth;
	op.data.buswidth = spi_nor_get_protocol_data_nbits(nor->read_proto);

	/* convert the dummy cycles to the number of bytes */
	op.dummy.nbytes = (nor->read_dummy * op.dummy.buswidth) / 8;

	while (remaining) {
		op.data.nbytes = remaining < UINT_MAX ? remaining : UINT_MAX;
		ret = spi_mem_adjust_op_size(flash->spimem, &op);
		if (ret)
			return ret;

		ret = spi_mem_exec_op(flash->spimem, &op);
		if (ret)
			return ret;

		op.addr.val += op.data.nbytes;
		remaining -= op.data.nbytes;
		op.data.buf.in += op.data.nbytes;
	}

	return len;
}

/*
 * board specific setup should have ensured the SPI clock used here
 * matches what the READ command supports, at least until this driver
 * understands FAST_READ (for clocks over 25 MHz).
 */
static int m25p_probe(struct spi_mem *spimem)
{
	struct spi_device *spi = spimem->spi;
	struct flash_platform_data	*data;
	struct m25p *flash;
	struct spi_nor *nor;
	struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_READ_FAST |
			SNOR_HWCAPS_PP,
	};
	char *flash_name;
	int ret;

	data = dev_get_platdata(&spimem->spi->dev);

	flash = devm_kzalloc(&spimem->spi->dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;

	nor = &flash->spi_nor;

	/* install the hooks */
	nor->read = m25p80_read;
	nor->write = m25p80_write;
	nor->write_reg = m25p80_write_reg;
	nor->read_reg = m25p80_read_reg;

	nor->dev = &spimem->spi->dev;
	spi_nor_set_flash_node(nor, spi->dev.of_node);
	nor->priv = flash;

	spi_mem_set_drvdata(spimem, flash);
	flash->spimem = spimem;

	if (spi->mode & SPI_RX_OCTAL) {
		hwcaps.mask |= SNOR_HWCAPS_READ_1_1_8;

		if (spi->mode & SPI_TX_OCTAL)
			hwcaps.mask |= (SNOR_HWCAPS_READ_1_8_8 |
					SNOR_HWCAPS_PP_1_1_8 |
					SNOR_HWCAPS_PP_1_8_8);
	} else if (spi->mode & SPI_RX_QUAD) {
		hwcaps.mask |= SNOR_HWCAPS_READ_1_1_4;

		if (spi->mode & SPI_TX_QUAD)
			hwcaps.mask |= (SNOR_HWCAPS_READ_1_4_4 |
					SNOR_HWCAPS_PP_1_1_4 |
					SNOR_HWCAPS_PP_1_4_4);
	} else if (spi->mode & SPI_RX_DUAL) {
		hwcaps.mask |= SNOR_HWCAPS_READ_1_1_2;

		if (spi->mode & SPI_TX_DUAL)
			hwcaps.mask |= SNOR_HWCAPS_READ_1_2_2;
	}

	if (data && data->name)
		nor->mtd.name = data->name;

	if (!nor->mtd.name)
		nor->mtd.name = spi_mem_get_name(spimem);

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

	ret = spi_nor_scan(nor, flash_name, &hwcaps);
	if (ret)
		return ret;

	return mtd_device_register(&nor->mtd, data ? data->parts : NULL,
				   data ? data->nr_parts : 0);
}


static int m25p_remove(struct spi_mem *spimem)
{
	struct m25p	*flash = spi_mem_get_drvdata(spimem);

	spi_nor_restore(&flash->spi_nor);

	/* Clean up MTD stuff. */
	return mtd_device_unregister(&flash->spi_nor.mtd);
}

static void m25p_shutdown(struct spi_mem *spimem)
{
	struct m25p *flash = spi_mem_get_drvdata(spimem);

	spi_nor_restore(&flash->spi_nor);
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

	/* Everspin MRAMs (non-JEDEC) */
	{ "mr25h128" }, /* 128 Kib, 40 MHz */
	{ "mr25h256" }, /* 256 Kib, 40 MHz */
	{ "mr25h10" },  /*   1 Mib, 40 MHz */
	{ "mr25h40" },  /*   4 Mib, 40 MHz */

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

static struct spi_mem_driver m25p80_driver = {
	.spidrv = {
		.driver = {
			.name	= "m25p80",
			.of_match_table = m25p_of_table,
		},
		.id_table	= m25p_ids,
	},
	.probe	= m25p_probe,
	.remove	= m25p_remove,
	.shutdown	= m25p_shutdown,

	/* REVISIT: many of these chips have deep power-down modes, which
	 * should clearly be entered on suspend() to minimize power use.
	 * And also when they're otherwise idle...
	 */
};

module_spi_mem_driver(m25p80_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("MTD SPI driver for ST M25Pxx flash chips");
