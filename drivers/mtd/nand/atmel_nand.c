/*
 *  Copyright © 2003 Rick Bronson
 *
 *  Derived from drivers/mtd/nand/autcpu12.c
 *	 Copyright © 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 *  Derived from drivers/mtd/spia.c
 *	 Copyright © 2000 Steven J. Hill (sjhill@cotw.com)
 *
 *
 *  Add Hardware ECC support for AT91SAM9260 / AT91SAM9263
 *     Richard Genoud (richard.genoud@gmail.com), Adeneo Copyright © 2007
 *
 *     Derived from Das U-Boot source code
 *     		(u-boot-1.1.5/board/atmel/at91sam9263ek/nand.c)
 *     © Copyright 2006 ATMEL Rousset, Lacressonniere Nicolas
 *
 *  Add Programmable Multibit ECC support for various AT91 SoC
 *     © Copyright 2012 ATMEL, Hong Xu
 *
 *  Add Nand Flash Controller support for SAMA5 SoC
 *     © Copyright 2013 ATMEL, Josh Wu (josh.wu@atmel.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_data/atmel.h>

static int use_dma = 1;
module_param(use_dma, int, 0);

static int on_flash_bbt = 0;
module_param(on_flash_bbt, int, 0);

/* Register access macros */
#define ecc_readl(add, reg)				\
	__raw_readl(add + ATMEL_ECC_##reg)
#define ecc_writel(add, reg, value)			\
	__raw_writel((value), add + ATMEL_ECC_##reg)

#include "atmel_nand_ecc.h"	/* Hardware ECC registers */
#include "atmel_nand_nfc.h"	/* Nand Flash Controller definition */

struct atmel_nand_caps {
	bool pmecc_correct_erase_page;
	uint8_t pmecc_max_correction;
};

/*
 * oob layout for large page size
 * bad block info is on bytes 0 and 1
 * the bytes have to be consecutives to avoid
 * several NAND_CMD_RNDOUT during read
 *
 * oob layout for small page size
 * bad block info is on bytes 4 and 5
 * the bytes have to be consecutives to avoid
 * several NAND_CMD_RNDOUT during read
 */
static int atmel_ooblayout_ecc_sp(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->length = 4;
	oobregion->offset = 0;

	return 0;
}

static int atmel_ooblayout_free_sp(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->offset = 6;
	oobregion->length = mtd->oobsize - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops atmel_ooblayout_sp_ops = {
	.ecc = atmel_ooblayout_ecc_sp,
	.free = atmel_ooblayout_free_sp,
};

struct atmel_nfc {
	void __iomem		*base_cmd_regs;
	void __iomem		*hsmc_regs;
	void			*sram_bank0;
	dma_addr_t		sram_bank0_phys;
	bool			use_nfc_sram;
	bool			write_by_sram;

	struct clk		*clk;

	bool			is_initialized;
	struct completion	comp_ready;
	struct completion	comp_cmd_done;
	struct completion	comp_xfer_done;

	/* Point to the sram bank which include readed data via NFC */
	void			*data_in_sram;
	bool			will_write_sram;
};
static struct atmel_nfc	nand_nfc;

struct atmel_nand_host {
	struct nand_chip	nand_chip;
	void __iomem		*io_base;
	dma_addr_t		io_phys;
	struct atmel_nand_data	board;
	struct device		*dev;
	void __iomem		*ecc;

	struct completion	comp;
	struct dma_chan		*dma_chan;

	struct atmel_nfc	*nfc;

	const struct atmel_nand_caps	*caps;
	bool			has_pmecc;
	u8			pmecc_corr_cap;
	u16			pmecc_sector_size;
	bool			has_no_lookup_table;
	u32			pmecc_lookup_table_offset;
	u32			pmecc_lookup_table_offset_512;
	u32			pmecc_lookup_table_offset_1024;

	int			pmecc_degree;	/* Degree of remainders */
	int			pmecc_cw_len;	/* Length of codeword */

	void __iomem		*pmerrloc_base;
	void __iomem		*pmerrloc_el_base;
	void __iomem		*pmecc_rom_base;

	/* lookup table for alpha_to and index_of */
	void __iomem		*pmecc_alpha_to;
	void __iomem		*pmecc_index_of;

	/* data for pmecc computation */
	int16_t			*pmecc_partial_syn;
	int16_t			*pmecc_si;
	int16_t			*pmecc_smu;	/* Sigma table */
	int16_t			*pmecc_lmu;	/* polynomal order */
	int			*pmecc_mu;
	int			*pmecc_dmu;
	int			*pmecc_delta;
};

/*
 * Enable NAND.
 */
static void atmel_nand_enable(struct atmel_nand_host *host)
{
	if (gpio_is_valid(host->board.enable_pin))
		gpio_set_value(host->board.enable_pin, 0);
}

/*
 * Disable NAND.
 */
static void atmel_nand_disable(struct atmel_nand_host *host)
{
	if (gpio_is_valid(host->board.enable_pin))
		gpio_set_value(host->board.enable_pin, 1);
}

/*
 * Hardware specific access to control-lines
 */
static void atmel_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_NCE)
			atmel_nand_enable(host);
		else
			atmel_nand_disable(host);
	}
	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, host->io_base + (1 << host->board.cle));
	else
		writeb(cmd, host->io_base + (1 << host->board.ale));
}

/*
 * Read the Device Ready pin.
 */
static int atmel_nand_device_ready(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);

	return gpio_get_value(host->board.rdy_pin) ^
                !!host->board.rdy_pin_active_low;
}

/* Set up for hardware ready pin and enable pin. */
static int atmel_nand_set_enable_ready_pins(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(chip);
	int res = 0;

	if (gpio_is_valid(host->board.rdy_pin)) {
		res = devm_gpio_request(host->dev,
				host->board.rdy_pin, "nand_rdy");
		if (res < 0) {
			dev_err(host->dev,
				"can't request rdy gpio %d\n",
				host->board.rdy_pin);
			return res;
		}

		res = gpio_direction_input(host->board.rdy_pin);
		if (res < 0) {
			dev_err(host->dev,
				"can't request input direction rdy gpio %d\n",
				host->board.rdy_pin);
			return res;
		}

		chip->dev_ready = atmel_nand_device_ready;
	}

	if (gpio_is_valid(host->board.enable_pin)) {
		res = devm_gpio_request(host->dev,
				host->board.enable_pin, "nand_enable");
		if (res < 0) {
			dev_err(host->dev,
				"can't request enable gpio %d\n",
				host->board.enable_pin);
			return res;
		}

		res = gpio_direction_output(host->board.enable_pin, 1);
		if (res < 0) {
			dev_err(host->dev,
				"can't request output direction enable gpio %d\n",
				host->board.enable_pin);
			return res;
		}
	}

	return res;
}

/*
 * Minimal-overhead PIO for data access.
 */
static void atmel_read_buf8(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);

	if (host->nfc && host->nfc->use_nfc_sram && host->nfc->data_in_sram) {
		memcpy(buf, host->nfc->data_in_sram, len);
		host->nfc->data_in_sram += len;
	} else {
		__raw_readsb(nand_chip->IO_ADDR_R, buf, len);
	}
}

static void atmel_read_buf16(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);

	if (host->nfc && host->nfc->use_nfc_sram && host->nfc->data_in_sram) {
		memcpy(buf, host->nfc->data_in_sram, len);
		host->nfc->data_in_sram += len;
	} else {
		__raw_readsw(nand_chip->IO_ADDR_R, buf, len / 2);
	}
}

static void atmel_write_buf8(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd_to_nand(mtd);

	__raw_writesb(nand_chip->IO_ADDR_W, buf, len);
}

static void atmel_write_buf16(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd_to_nand(mtd);

	__raw_writesw(nand_chip->IO_ADDR_W, buf, len / 2);
}

static void dma_complete_func(void *completion)
{
	complete(completion);
}

static int nfc_set_sram_bank(struct atmel_nand_host *host, unsigned int bank)
{
	/* NFC only has two banks. Must be 0 or 1 */
	if (bank > 1)
		return -EINVAL;

	if (bank) {
		struct mtd_info *mtd = nand_to_mtd(&host->nand_chip);

		/* Only for a 2k-page or lower flash, NFC can handle 2 banks */
		if (mtd->writesize > 2048)
			return -EINVAL;
		nfc_writel(host->nfc->hsmc_regs, BANK, ATMEL_HSMC_NFC_BANK1);
	} else {
		nfc_writel(host->nfc->hsmc_regs, BANK, ATMEL_HSMC_NFC_BANK0);
	}

	return 0;
}

static uint nfc_get_sram_off(struct atmel_nand_host *host)
{
	if (nfc_readl(host->nfc->hsmc_regs, BANK) & ATMEL_HSMC_NFC_BANK1)
		return NFC_SRAM_BANK1_OFFSET;
	else
		return 0;
}

static dma_addr_t nfc_sram_phys(struct atmel_nand_host *host)
{
	if (nfc_readl(host->nfc->hsmc_regs, BANK) & ATMEL_HSMC_NFC_BANK1)
		return host->nfc->sram_bank0_phys + NFC_SRAM_BANK1_OFFSET;
	else
		return host->nfc->sram_bank0_phys;
}

static int atmel_nand_dma_op(struct mtd_info *mtd, void *buf, int len,
			       int is_read)
{
	struct dma_device *dma_dev;
	enum dma_ctrl_flags flags;
	dma_addr_t dma_src_addr, dma_dst_addr, phys_addr;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_cookie_t cookie;
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(chip);
	void *p = buf;
	int err = -EIO;
	enum dma_data_direction dir = is_read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	struct atmel_nfc *nfc = host->nfc;

	if (buf >= high_memory)
		goto err_buf;

	dma_dev = host->dma_chan->device;

	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

	phys_addr = dma_map_single(dma_dev->dev, p, len, dir);
	if (dma_mapping_error(dma_dev->dev, phys_addr)) {
		dev_err(host->dev, "Failed to dma_map_single\n");
		goto err_buf;
	}

	if (is_read) {
		if (nfc && nfc->data_in_sram)
			dma_src_addr = nfc_sram_phys(host) + (nfc->data_in_sram
				- (nfc->sram_bank0 + nfc_get_sram_off(host)));
		else
			dma_src_addr = host->io_phys;

		dma_dst_addr = phys_addr;
	} else {
		dma_src_addr = phys_addr;

		if (nfc && nfc->write_by_sram)
			dma_dst_addr = nfc_sram_phys(host);
		else
			dma_dst_addr = host->io_phys;
	}

	tx = dma_dev->device_prep_dma_memcpy(host->dma_chan, dma_dst_addr,
					     dma_src_addr, len, flags);
	if (!tx) {
		dev_err(host->dev, "Failed to prepare DMA memcpy\n");
		goto err_dma;
	}

	init_completion(&host->comp);
	tx->callback = dma_complete_func;
	tx->callback_param = &host->comp;

	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		dev_err(host->dev, "Failed to do DMA tx_submit\n");
		goto err_dma;
	}

	dma_async_issue_pending(host->dma_chan);
	wait_for_completion(&host->comp);

	if (is_read && nfc && nfc->data_in_sram)
		/* After read data from SRAM, need to increase the position */
		nfc->data_in_sram += len;

	err = 0;

err_dma:
	dma_unmap_single(dma_dev->dev, phys_addr, len, dir);
err_buf:
	if (err != 0)
		dev_dbg(host->dev, "Fall back to CPU I/O\n");
	return err;
}

static void atmel_read_buf(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (use_dma && len > mtd->oobsize)
		/* only use DMA for bigger than oob size: better performances */
		if (atmel_nand_dma_op(mtd, buf, len, 1) == 0)
			return;

	if (chip->options & NAND_BUSWIDTH_16)
		atmel_read_buf16(mtd, buf, len);
	else
		atmel_read_buf8(mtd, buf, len);
}

static void atmel_write_buf(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (use_dma && len > mtd->oobsize)
		/* only use DMA for bigger than oob size: better performances */
		if (atmel_nand_dma_op(mtd, (void *)buf, len, 0) == 0)
			return;

	if (chip->options & NAND_BUSWIDTH_16)
		atmel_write_buf16(mtd, buf, len);
	else
		atmel_write_buf8(mtd, buf, len);
}

/*
 * Return number of ecc bytes per sector according to sector size and
 * correction capability
 *
 * Following table shows what at91 PMECC supported:
 * Correction Capability	Sector_512_bytes	Sector_1024_bytes
 * =====================	================	=================
 *                2-bits                 4-bytes                  4-bytes
 *                4-bits                 7-bytes                  7-bytes
 *                8-bits                13-bytes                 14-bytes
 *               12-bits                20-bytes                 21-bytes
 *               24-bits                39-bytes                 42-bytes
 *               32-bits                52-bytes                 56-bytes
 */
static int pmecc_get_ecc_bytes(int cap, int sector_size)
{
	int m = 12 + sector_size / 512;
	return (m * cap + 7) / 8;
}

static void __iomem *pmecc_get_alpha_to(struct atmel_nand_host *host)
{
	int table_size;

	table_size = host->pmecc_sector_size == 512 ?
		PMECC_LOOKUP_TABLE_SIZE_512 : PMECC_LOOKUP_TABLE_SIZE_1024;

	return host->pmecc_rom_base + host->pmecc_lookup_table_offset +
			table_size * sizeof(int16_t);
}

static int pmecc_data_alloc(struct atmel_nand_host *host)
{
	const int cap = host->pmecc_corr_cap;
	int size;

	size = (2 * cap + 1) * sizeof(int16_t);
	host->pmecc_partial_syn = devm_kzalloc(host->dev, size, GFP_KERNEL);
	host->pmecc_si = devm_kzalloc(host->dev, size, GFP_KERNEL);
	host->pmecc_lmu = devm_kzalloc(host->dev,
			(cap + 1) * sizeof(int16_t), GFP_KERNEL);
	host->pmecc_smu = devm_kzalloc(host->dev,
			(cap + 2) * size, GFP_KERNEL);

	size = (cap + 1) * sizeof(int);
	host->pmecc_mu = devm_kzalloc(host->dev, size, GFP_KERNEL);
	host->pmecc_dmu = devm_kzalloc(host->dev, size, GFP_KERNEL);
	host->pmecc_delta = devm_kzalloc(host->dev, size, GFP_KERNEL);

	if (!host->pmecc_partial_syn ||
		!host->pmecc_si ||
		!host->pmecc_lmu ||
		!host->pmecc_smu ||
		!host->pmecc_mu ||
		!host->pmecc_dmu ||
		!host->pmecc_delta)
		return -ENOMEM;

	return 0;
}

static void pmecc_gen_syndrome(struct mtd_info *mtd, int sector)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);
	int i;
	uint32_t value;

	/* Fill odd syndromes */
	for (i = 0; i < host->pmecc_corr_cap; i++) {
		value = pmecc_readl_rem_relaxed(host->ecc, sector, i / 2);
		if (i & 1)
			value >>= 16;
		value &= 0xffff;
		host->pmecc_partial_syn[(2 * i) + 1] = (int16_t)value;
	}
}

static void pmecc_substitute(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);
	int16_t __iomem *alpha_to = host->pmecc_alpha_to;
	int16_t __iomem *index_of = host->pmecc_index_of;
	int16_t *partial_syn = host->pmecc_partial_syn;
	const int cap = host->pmecc_corr_cap;
	int16_t *si;
	int i, j;

	/* si[] is a table that holds the current syndrome value,
	 * an element of that table belongs to the field
	 */
	si = host->pmecc_si;

	memset(&si[1], 0, sizeof(int16_t) * (2 * cap - 1));

	/* Computation 2t syndromes based on S(x) */
	/* Odd syndromes */
	for (i = 1; i < 2 * cap; i += 2) {
		for (j = 0; j < host->pmecc_degree; j++) {
			if (partial_syn[i] & ((unsigned short)0x1 << j))
				si[i] = readw_relaxed(alpha_to + i * j) ^ si[i];
		}
	}
	/* Even syndrome = (Odd syndrome) ** 2 */
	for (i = 2, j = 1; j <= cap; i = ++j << 1) {
		if (si[j] == 0) {
			si[i] = 0;
		} else {
			int16_t tmp;

			tmp = readw_relaxed(index_of + si[j]);
			tmp = (tmp * 2) % host->pmecc_cw_len;
			si[i] = readw_relaxed(alpha_to + tmp);
		}
	}

	return;
}

static void pmecc_get_sigma(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);

	int16_t *lmu = host->pmecc_lmu;
	int16_t *si = host->pmecc_si;
	int *mu = host->pmecc_mu;
	int *dmu = host->pmecc_dmu;	/* Discrepancy */
	int *delta = host->pmecc_delta; /* Delta order */
	int cw_len = host->pmecc_cw_len;
	const int16_t cap = host->pmecc_corr_cap;
	const int num = 2 * cap + 1;
	int16_t __iomem	*index_of = host->pmecc_index_of;
	int16_t __iomem	*alpha_to = host->pmecc_alpha_to;
	int i, j, k;
	uint32_t dmu_0_count, tmp;
	int16_t *smu = host->pmecc_smu;

	/* index of largest delta */
	int ro;
	int largest;
	int diff;

	dmu_0_count = 0;

	/* First Row */

	/* Mu */
	mu[0] = -1;

	memset(smu, 0, sizeof(int16_t) * num);
	smu[0] = 1;

	/* discrepancy set to 1 */
	dmu[0] = 1;
	/* polynom order set to 0 */
	lmu[0] = 0;
	delta[0] = (mu[0] * 2 - lmu[0]) >> 1;

	/* Second Row */

	/* Mu */
	mu[1] = 0;
	/* Sigma(x) set to 1 */
	memset(&smu[num], 0, sizeof(int16_t) * num);
	smu[num] = 1;

	/* discrepancy set to S1 */
	dmu[1] = si[1];

	/* polynom order set to 0 */
	lmu[1] = 0;

	delta[1] = (mu[1] * 2 - lmu[1]) >> 1;

	/* Init the Sigma(x) last row */
	memset(&smu[(cap + 1) * num], 0, sizeof(int16_t) * num);

	for (i = 1; i <= cap; i++) {
		mu[i + 1] = i << 1;
		/* Begin Computing Sigma (Mu+1) and L(mu) */
		/* check if discrepancy is set to 0 */
		if (dmu[i] == 0) {
			dmu_0_count++;

			tmp = ((cap - (lmu[i] >> 1) - 1) / 2);
			if ((cap - (lmu[i] >> 1) - 1) & 0x1)
				tmp += 2;
			else
				tmp += 1;

			if (dmu_0_count == tmp) {
				for (j = 0; j <= (lmu[i] >> 1) + 1; j++)
					smu[(cap + 1) * num + j] =
							smu[i * num + j];

				lmu[cap + 1] = lmu[i];
				return;
			}

			/* copy polynom */
			for (j = 0; j <= lmu[i] >> 1; j++)
				smu[(i + 1) * num + j] = smu[i * num + j];

			/* copy previous polynom order to the next */
			lmu[i + 1] = lmu[i];
		} else {
			ro = 0;
			largest = -1;
			/* find largest delta with dmu != 0 */
			for (j = 0; j < i; j++) {
				if ((dmu[j]) && (delta[j] > largest)) {
					largest = delta[j];
					ro = j;
				}
			}

			/* compute difference */
			diff = (mu[i] - mu[ro]);

			/* Compute degree of the new smu polynomial */
			if ((lmu[i] >> 1) > ((lmu[ro] >> 1) + diff))
				lmu[i + 1] = lmu[i];
			else
				lmu[i + 1] = ((lmu[ro] >> 1) + diff) * 2;

			/* Init smu[i+1] with 0 */
			for (k = 0; k < num; k++)
				smu[(i + 1) * num + k] = 0;

			/* Compute smu[i+1] */
			for (k = 0; k <= lmu[ro] >> 1; k++) {
				int16_t a, b, c;

				if (!(smu[ro * num + k] && dmu[i]))
					continue;
				a = readw_relaxed(index_of + dmu[i]);
				b = readw_relaxed(index_of + dmu[ro]);
				c = readw_relaxed(index_of + smu[ro * num + k]);
				tmp = a + (cw_len - b) + c;
				a = readw_relaxed(alpha_to + tmp % cw_len);
				smu[(i + 1) * num + (k + diff)] = a;
			}

			for (k = 0; k <= lmu[i] >> 1; k++)
				smu[(i + 1) * num + k] ^= smu[i * num + k];
		}

		/* End Computing Sigma (Mu+1) and L(mu) */
		/* In either case compute delta */
		delta[i + 1] = (mu[i + 1] * 2 - lmu[i + 1]) >> 1;

		/* Do not compute discrepancy for the last iteration */
		if (i >= cap)
			continue;

		for (k = 0; k <= (lmu[i + 1] >> 1); k++) {
			tmp = 2 * (i - 1);
			if (k == 0) {
				dmu[i + 1] = si[tmp + 3];
			} else if (smu[(i + 1) * num + k] && si[tmp + 3 - k]) {
				int16_t a, b, c;
				a = readw_relaxed(index_of +
						smu[(i + 1) * num + k]);
				b = si[2 * (i - 1) + 3 - k];
				c = readw_relaxed(index_of + b);
				tmp = a + c;
				tmp %= cw_len;
				dmu[i + 1] = readw_relaxed(alpha_to + tmp) ^
					dmu[i + 1];
			}
		}
	}

	return;
}

static int pmecc_err_location(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);
	unsigned long end_time;
	const int cap = host->pmecc_corr_cap;
	const int num = 2 * cap + 1;
	int sector_size = host->pmecc_sector_size;
	int err_nbr = 0;	/* number of error */
	int roots_nbr;		/* number of roots */
	int i;
	uint32_t val;
	int16_t *smu = host->pmecc_smu;

	pmerrloc_writel(host->pmerrloc_base, ELDIS, PMERRLOC_DISABLE);

	for (i = 0; i <= host->pmecc_lmu[cap + 1] >> 1; i++) {
		pmerrloc_writel_sigma_relaxed(host->pmerrloc_base, i,
				      smu[(cap + 1) * num + i]);
		err_nbr++;
	}

	val = (err_nbr - 1) << 16;
	if (sector_size == 1024)
		val |= 1;

	pmerrloc_writel(host->pmerrloc_base, ELCFG, val);
	pmerrloc_writel(host->pmerrloc_base, ELEN,
			sector_size * 8 + host->pmecc_degree * cap);

	end_time = jiffies + msecs_to_jiffies(PMECC_MAX_TIMEOUT_MS);
	while (!(pmerrloc_readl_relaxed(host->pmerrloc_base, ELISR)
		 & PMERRLOC_CALC_DONE)) {
		if (unlikely(time_after(jiffies, end_time))) {
			dev_err(host->dev, "PMECC: Timeout to calculate error location.\n");
			return -1;
		}
		cpu_relax();
	}

	roots_nbr = (pmerrloc_readl_relaxed(host->pmerrloc_base, ELISR)
		& PMERRLOC_ERR_NUM_MASK) >> 8;
	/* Number of roots == degree of smu hence <= cap */
	if (roots_nbr == host->pmecc_lmu[cap + 1] >> 1)
		return err_nbr - 1;

	/* Number of roots does not match the degree of smu
	 * unable to correct error */
	return -1;
}

static void pmecc_correct_data(struct mtd_info *mtd, uint8_t *buf, uint8_t *ecc,
		int sector_num, int extra_bytes, int err_nbr)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);
	int i = 0;
	int byte_pos, bit_pos, sector_size, pos;
	uint32_t tmp;
	uint8_t err_byte;

	sector_size = host->pmecc_sector_size;

	while (err_nbr) {
		tmp = pmerrloc_readl_el_relaxed(host->pmerrloc_el_base, i) - 1;
		byte_pos = tmp / 8;
		bit_pos  = tmp % 8;

		if (byte_pos >= (sector_size + extra_bytes))
			BUG();	/* should never happen */

		if (byte_pos < sector_size) {
			err_byte = *(buf + byte_pos);
			*(buf + byte_pos) ^= (1 << bit_pos);

			pos = sector_num * host->pmecc_sector_size + byte_pos;
			dev_dbg(host->dev, "Bit flip in data area, byte_pos: %d, bit_pos: %d, 0x%02x -> 0x%02x\n",
				pos, bit_pos, err_byte, *(buf + byte_pos));
		} else {
			struct mtd_oob_region oobregion;

			/* Bit flip in OOB area */
			tmp = sector_num * nand_chip->ecc.bytes
					+ (byte_pos - sector_size);
			err_byte = ecc[tmp];
			ecc[tmp] ^= (1 << bit_pos);

			mtd_ooblayout_ecc(mtd, 0, &oobregion);
			pos = tmp + oobregion.offset;
			dev_dbg(host->dev, "Bit flip in OOB, oob_byte_pos: %d, bit_pos: %d, 0x%02x -> 0x%02x\n",
				pos, bit_pos, err_byte, ecc[tmp]);
		}

		i++;
		err_nbr--;
	}

	return;
}

static int pmecc_correction(struct mtd_info *mtd, u32 pmecc_stat, uint8_t *buf,
	u8 *ecc)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);
	int i, err_nbr;
	uint8_t *buf_pos;
	int max_bitflips = 0;

	for (i = 0; i < nand_chip->ecc.steps; i++) {
		err_nbr = 0;
		if (pmecc_stat & 0x1) {
			buf_pos = buf + i * host->pmecc_sector_size;

			pmecc_gen_syndrome(mtd, i);
			pmecc_substitute(mtd);
			pmecc_get_sigma(mtd);

			err_nbr = pmecc_err_location(mtd);
			if (err_nbr >= 0) {
				pmecc_correct_data(mtd, buf_pos, ecc, i,
						   nand_chip->ecc.bytes,
						   err_nbr);
			} else if (!host->caps->pmecc_correct_erase_page) {
				u8 *ecc_pos = ecc + (i * nand_chip->ecc.bytes);

				/* Try to detect erased pages */
				err_nbr = nand_check_erased_ecc_chunk(buf_pos,
							host->pmecc_sector_size,
							ecc_pos,
							nand_chip->ecc.bytes,
							NULL, 0,
							nand_chip->ecc.strength);
			}

			if (err_nbr < 0) {
				dev_err(host->dev, "PMECC: Too many errors\n");
				mtd->ecc_stats.failed++;
				return -EIO;
			}

			mtd->ecc_stats.corrected += err_nbr;
			max_bitflips = max_t(int, max_bitflips, err_nbr);
		}
		pmecc_stat >>= 1;
	}

	return max_bitflips;
}

static void pmecc_enable(struct atmel_nand_host *host, int ecc_op)
{
	u32 val;

	if (ecc_op != NAND_ECC_READ && ecc_op != NAND_ECC_WRITE) {
		dev_err(host->dev, "atmel_nand: wrong pmecc operation type!");
		return;
	}

	pmecc_writel(host->ecc, CTRL, PMECC_CTRL_RST);
	pmecc_writel(host->ecc, CTRL, PMECC_CTRL_DISABLE);
	val = pmecc_readl_relaxed(host->ecc, CFG);

	if (ecc_op == NAND_ECC_READ)
		pmecc_writel(host->ecc, CFG, (val & ~PMECC_CFG_WRITE_OP)
			| PMECC_CFG_AUTO_ENABLE);
	else
		pmecc_writel(host->ecc, CFG, (val | PMECC_CFG_WRITE_OP)
			& ~PMECC_CFG_AUTO_ENABLE);

	pmecc_writel(host->ecc, CTRL, PMECC_CTRL_ENABLE);
	pmecc_writel(host->ecc, CTRL, PMECC_CTRL_DATA);
}

static int atmel_nand_pmecc_read_page(struct mtd_info *mtd,
	struct nand_chip *chip, uint8_t *buf, int oob_required, int page)
{
	struct atmel_nand_host *host = nand_get_controller_data(chip);
	int eccsize = chip->ecc.size * chip->ecc.steps;
	uint8_t *oob = chip->oob_poi;
	uint32_t stat;
	unsigned long end_time;
	int bitflips = 0;

	if (!host->nfc || !host->nfc->use_nfc_sram)
		pmecc_enable(host, NAND_ECC_READ);

	chip->read_buf(mtd, buf, eccsize);
	chip->read_buf(mtd, oob, mtd->oobsize);

	end_time = jiffies + msecs_to_jiffies(PMECC_MAX_TIMEOUT_MS);
	while ((pmecc_readl_relaxed(host->ecc, SR) & PMECC_SR_BUSY)) {
		if (unlikely(time_after(jiffies, end_time))) {
			dev_err(host->dev, "PMECC: Timeout to get error status.\n");
			return -EIO;
		}
		cpu_relax();
	}

	stat = pmecc_readl_relaxed(host->ecc, ISR);
	if (stat != 0) {
		struct mtd_oob_region oobregion;

		mtd_ooblayout_ecc(mtd, 0, &oobregion);
		bitflips = pmecc_correction(mtd, stat, buf,
					    &oob[oobregion.offset]);
		if (bitflips < 0)
			/* uncorrectable errors */
			return 0;
	}

	return bitflips;
}

static int atmel_nand_pmecc_write_page(struct mtd_info *mtd,
		struct nand_chip *chip, const uint8_t *buf, int oob_required,
		int page)
{
	struct atmel_nand_host *host = nand_get_controller_data(chip);
	struct mtd_oob_region oobregion = { };
	int i, j, section = 0;
	unsigned long end_time;

	if (!host->nfc || !host->nfc->write_by_sram) {
		pmecc_enable(host, NAND_ECC_WRITE);
		chip->write_buf(mtd, (u8 *)buf, mtd->writesize);
	}

	end_time = jiffies + msecs_to_jiffies(PMECC_MAX_TIMEOUT_MS);
	while ((pmecc_readl_relaxed(host->ecc, SR) & PMECC_SR_BUSY)) {
		if (unlikely(time_after(jiffies, end_time))) {
			dev_err(host->dev, "PMECC: Timeout to get ECC value.\n");
			return -EIO;
		}
		cpu_relax();
	}

	for (i = 0; i < chip->ecc.steps; i++) {
		for (j = 0; j < chip->ecc.bytes; j++) {
			if (!oobregion.length)
				mtd_ooblayout_ecc(mtd, section, &oobregion);

			chip->oob_poi[oobregion.offset] =
				pmecc_readb_ecc_relaxed(host->ecc, i, j);
			oobregion.length--;
			oobregion.offset++;
			section++;
		}
	}
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

static void atmel_pmecc_core_init(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);
	int eccbytes = mtd_ooblayout_count_eccbytes(mtd);
	uint32_t val = 0;
	struct mtd_oob_region oobregion;

	pmecc_writel(host->ecc, CTRL, PMECC_CTRL_RST);
	pmecc_writel(host->ecc, CTRL, PMECC_CTRL_DISABLE);

	switch (host->pmecc_corr_cap) {
	case 2:
		val = PMECC_CFG_BCH_ERR2;
		break;
	case 4:
		val = PMECC_CFG_BCH_ERR4;
		break;
	case 8:
		val = PMECC_CFG_BCH_ERR8;
		break;
	case 12:
		val = PMECC_CFG_BCH_ERR12;
		break;
	case 24:
		val = PMECC_CFG_BCH_ERR24;
		break;
	case 32:
		val = PMECC_CFG_BCH_ERR32;
		break;
	}

	if (host->pmecc_sector_size == 512)
		val |= PMECC_CFG_SECTOR512;
	else if (host->pmecc_sector_size == 1024)
		val |= PMECC_CFG_SECTOR1024;

	switch (nand_chip->ecc.steps) {
	case 1:
		val |= PMECC_CFG_PAGE_1SECTOR;
		break;
	case 2:
		val |= PMECC_CFG_PAGE_2SECTORS;
		break;
	case 4:
		val |= PMECC_CFG_PAGE_4SECTORS;
		break;
	case 8:
		val |= PMECC_CFG_PAGE_8SECTORS;
		break;
	}

	val |= (PMECC_CFG_READ_OP | PMECC_CFG_SPARE_DISABLE
		| PMECC_CFG_AUTO_DISABLE);
	pmecc_writel(host->ecc, CFG, val);

	pmecc_writel(host->ecc, SAREA, mtd->oobsize - 1);
	mtd_ooblayout_ecc(mtd, 0, &oobregion);
	pmecc_writel(host->ecc, SADDR, oobregion.offset);
	pmecc_writel(host->ecc, EADDR,
		     oobregion.offset + eccbytes - 1);
	/* See datasheet about PMECC Clock Control Register */
	pmecc_writel(host->ecc, CLK, 2);
	pmecc_writel(host->ecc, IDR, 0xff);
	pmecc_writel(host->ecc, CTRL, PMECC_CTRL_ENABLE);
}

/*
 * Get minimum ecc requirements from NAND.
 * If pmecc-cap, pmecc-sector-size in DTS are not specified, this function
 * will set them according to minimum ecc requirement. Otherwise, use the
 * value in DTS file.
 * return 0 if success. otherwise return error code.
 */
static int pmecc_choose_ecc(struct atmel_nand_host *host,
		int *cap, int *sector_size)
{
	/* Get minimum ECC requirements */
	if (host->nand_chip.ecc_strength_ds) {
		*cap = host->nand_chip.ecc_strength_ds;
		*sector_size = host->nand_chip.ecc_step_ds;
		dev_info(host->dev, "minimum ECC: %d bits in %d bytes\n",
				*cap, *sector_size);
	} else {
		*cap = 2;
		*sector_size = 512;
		dev_info(host->dev, "can't detect min. ECC, assume 2 bits in 512 bytes\n");
	}

	/* If device tree doesn't specify, use NAND's minimum ECC parameters */
	if (host->pmecc_corr_cap == 0) {
		if (*cap > host->caps->pmecc_max_correction)
			return -EINVAL;

		/* use the most fitable ecc bits (the near bigger one ) */
		if (*cap <= 2)
			host->pmecc_corr_cap = 2;
		else if (*cap <= 4)
			host->pmecc_corr_cap = 4;
		else if (*cap <= 8)
			host->pmecc_corr_cap = 8;
		else if (*cap <= 12)
			host->pmecc_corr_cap = 12;
		else if (*cap <= 24)
			host->pmecc_corr_cap = 24;
		else if (*cap <= 32)
			host->pmecc_corr_cap = 32;
		else
			return -EINVAL;
	}
	if (host->pmecc_sector_size == 0) {
		/* use the most fitable sector size (the near smaller one ) */
		if (*sector_size >= 1024)
			host->pmecc_sector_size = 1024;
		else if (*sector_size >= 512)
			host->pmecc_sector_size = 512;
		else
			return -EINVAL;
	}
	return 0;
}

static inline int deg(unsigned int poly)
{
	/* polynomial degree is the most-significant bit index */
	return fls(poly) - 1;
}

static int build_gf_tables(int mm, unsigned int poly,
		int16_t *index_of, int16_t *alpha_to)
{
	unsigned int i, x = 1;
	const unsigned int k = 1 << deg(poly);
	unsigned int nn = (1 << mm) - 1;

	/* primitive polynomial must be of degree m */
	if (k != (1u << mm))
		return -EINVAL;

	for (i = 0; i < nn; i++) {
		alpha_to[i] = x;
		index_of[x] = i;
		if (i && (x == 1))
			/* polynomial is not primitive (a^i=1 with 0<i<2^m-1) */
			return -EINVAL;
		x <<= 1;
		if (x & k)
			x ^= poly;
	}
	alpha_to[nn] = 1;
	index_of[0] = 0;

	return 0;
}

static uint16_t *create_lookup_table(struct device *dev, int sector_size)
{
	int degree = (sector_size == 512) ?
			PMECC_GF_DIMENSION_13 :
			PMECC_GF_DIMENSION_14;
	unsigned int poly = (sector_size == 512) ?
			PMECC_GF_13_PRIMITIVE_POLY :
			PMECC_GF_14_PRIMITIVE_POLY;
	int table_size = (sector_size == 512) ?
			PMECC_LOOKUP_TABLE_SIZE_512 :
			PMECC_LOOKUP_TABLE_SIZE_1024;

	int16_t *addr = devm_kzalloc(dev, 2 * table_size * sizeof(uint16_t),
			GFP_KERNEL);
	if (addr && build_gf_tables(degree, poly, addr, addr + table_size))
		return NULL;

	return addr;
}

static int atmel_pmecc_nand_init_params(struct platform_device *pdev,
					 struct atmel_nand_host *host)
{
	struct nand_chip *nand_chip = &host->nand_chip;
	struct mtd_info *mtd = nand_to_mtd(nand_chip);
	struct resource *regs, *regs_pmerr, *regs_rom;
	uint16_t *galois_table;
	int cap, sector_size, err_no;

	err_no = pmecc_choose_ecc(host, &cap, &sector_size);
	if (err_no) {
		dev_err(host->dev, "The NAND flash's ECC requirement are not support!");
		return err_no;
	}

	if (cap > host->pmecc_corr_cap ||
			sector_size != host->pmecc_sector_size)
		dev_info(host->dev, "WARNING: Be Caution! Using different PMECC parameters from Nand ONFI ECC reqirement.\n");

	cap = host->pmecc_corr_cap;
	sector_size = host->pmecc_sector_size;
	host->pmecc_lookup_table_offset = (sector_size == 512) ?
			host->pmecc_lookup_table_offset_512 :
			host->pmecc_lookup_table_offset_1024;

	dev_info(host->dev, "Initialize PMECC params, cap: %d, sector: %d\n",
		 cap, sector_size);

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!regs) {
		dev_warn(host->dev,
			"Can't get I/O resource regs for PMECC controller, rolling back on software ECC\n");
		nand_chip->ecc.mode = NAND_ECC_SOFT;
		nand_chip->ecc.algo = NAND_ECC_HAMMING;
		return 0;
	}

	host->ecc = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(host->ecc)) {
		err_no = PTR_ERR(host->ecc);
		goto err;
	}

	regs_pmerr = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	host->pmerrloc_base = devm_ioremap_resource(&pdev->dev, regs_pmerr);
	if (IS_ERR(host->pmerrloc_base)) {
		err_no = PTR_ERR(host->pmerrloc_base);
		goto err;
	}
	host->pmerrloc_el_base = host->pmerrloc_base + ATMEL_PMERRLOC_SIGMAx +
		(host->caps->pmecc_max_correction + 1) * 4;

	if (!host->has_no_lookup_table) {
		regs_rom = platform_get_resource(pdev, IORESOURCE_MEM, 3);
		host->pmecc_rom_base = devm_ioremap_resource(&pdev->dev,
								regs_rom);
		if (IS_ERR(host->pmecc_rom_base)) {
			dev_err(host->dev, "Can not get I/O resource for ROM, will build a lookup table in runtime!\n");
			host->has_no_lookup_table = true;
		}
	}

	if (host->has_no_lookup_table) {
		/* Build the look-up table in runtime */
		galois_table = create_lookup_table(host->dev, sector_size);
		if (!galois_table) {
			dev_err(host->dev, "Failed to build a lookup table in runtime!\n");
			err_no = -EINVAL;
			goto err;
		}

		host->pmecc_rom_base = (void __iomem *)galois_table;
		host->pmecc_lookup_table_offset = 0;
	}

	nand_chip->ecc.size = sector_size;

	/* set ECC page size and oob layout */
	switch (mtd->writesize) {
	case 512:
	case 1024:
	case 2048:
	case 4096:
	case 8192:
		if (sector_size > mtd->writesize) {
			dev_err(host->dev, "pmecc sector size is bigger than the page size!\n");
			err_no = -EINVAL;
			goto err;
		}

		host->pmecc_degree = (sector_size == 512) ?
			PMECC_GF_DIMENSION_13 : PMECC_GF_DIMENSION_14;
		host->pmecc_cw_len = (1 << host->pmecc_degree) - 1;
		host->pmecc_alpha_to = pmecc_get_alpha_to(host);
		host->pmecc_index_of = host->pmecc_rom_base +
			host->pmecc_lookup_table_offset;

		nand_chip->ecc.strength = cap;
		nand_chip->ecc.bytes = pmecc_get_ecc_bytes(cap, sector_size);
		nand_chip->ecc.steps = mtd->writesize / sector_size;
		nand_chip->ecc.total = nand_chip->ecc.bytes *
			nand_chip->ecc.steps;
		if (nand_chip->ecc.total >
				mtd->oobsize - PMECC_OOB_RESERVED_BYTES) {
			dev_err(host->dev, "No room for ECC bytes\n");
			err_no = -EINVAL;
			goto err;
		}

		mtd_set_ooblayout(mtd, &nand_ooblayout_lp_ops);
		break;
	default:
		dev_warn(host->dev,
			"Unsupported page size for PMECC, use Software ECC\n");
		/* page size not handled by HW ECC */
		/* switching back to soft ECC */
		nand_chip->ecc.mode = NAND_ECC_SOFT;
		nand_chip->ecc.algo = NAND_ECC_HAMMING;
		return 0;
	}

	/* Allocate data for PMECC computation */
	err_no = pmecc_data_alloc(host);
	if (err_no) {
		dev_err(host->dev,
				"Cannot allocate memory for PMECC computation!\n");
		goto err;
	}

	nand_chip->options |= NAND_NO_SUBPAGE_WRITE;
	nand_chip->ecc.read_page = atmel_nand_pmecc_read_page;
	nand_chip->ecc.write_page = atmel_nand_pmecc_write_page;

	atmel_pmecc_core_init(mtd);

	return 0;

err:
	return err_no;
}

/*
 * Calculate HW ECC
 *
 * function called after a write
 *
 * mtd:        MTD block structure
 * dat:        raw data (unused)
 * ecc_code:   buffer for ECC
 */
static int atmel_nand_calculate(struct mtd_info *mtd,
		const u_char *dat, unsigned char *ecc_code)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);
	unsigned int ecc_value;

	/* get the first 2 ECC bytes */
	ecc_value = ecc_readl(host->ecc, PR);

	ecc_code[0] = ecc_value & 0xFF;
	ecc_code[1] = (ecc_value >> 8) & 0xFF;

	/* get the last 2 ECC bytes */
	ecc_value = ecc_readl(host->ecc, NPR) & ATMEL_ECC_NPARITY;

	ecc_code[2] = ecc_value & 0xFF;
	ecc_code[3] = (ecc_value >> 8) & 0xFF;

	return 0;
}

/*
 * HW ECC read page function
 *
 * mtd:        mtd info structure
 * chip:       nand chip info structure
 * buf:        buffer to store read data
 * oob_required:    caller expects OOB data read to chip->oob_poi
 */
static int atmel_nand_read_page(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf, int oob_required, int page)
{
	int eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	uint8_t *p = buf;
	uint8_t *oob = chip->oob_poi;
	uint8_t *ecc_pos;
	int stat;
	unsigned int max_bitflips = 0;
	struct mtd_oob_region oobregion = {};

	/*
	 * Errata: ALE is incorrectly wired up to the ECC controller
	 * on the AP7000, so it will include the address cycles in the
	 * ECC calculation.
	 *
	 * Workaround: Reset the parity registers before reading the
	 * actual data.
	 */
	struct atmel_nand_host *host = nand_get_controller_data(chip);
	if (host->board.need_reset_workaround)
		ecc_writel(host->ecc, CR, ATMEL_ECC_RST);

	/* read the page */
	chip->read_buf(mtd, p, eccsize);

	/* move to ECC position if needed */
	mtd_ooblayout_ecc(mtd, 0, &oobregion);
	if (oobregion.offset != 0) {
		/*
		 * This only works on large pages because the ECC controller
		 * waits for NAND_CMD_RNDOUTSTART after the NAND_CMD_RNDOUT.
		 * Anyway, for small pages, the first ECC byte is at offset
		 * 0 in the OOB area.
		 */
		chip->cmdfunc(mtd, NAND_CMD_RNDOUT,
			      mtd->writesize + oobregion.offset, -1);
	}

	/* the ECC controller needs to read the ECC just after the data */
	ecc_pos = oob + oobregion.offset;
	chip->read_buf(mtd, ecc_pos, eccbytes);

	/* check if there's an error */
	stat = chip->ecc.correct(mtd, p, oob, NULL);

	if (stat < 0) {
		mtd->ecc_stats.failed++;
	} else {
		mtd->ecc_stats.corrected += stat;
		max_bitflips = max_t(unsigned int, max_bitflips, stat);
	}

	/* get back to oob start (end of page) */
	chip->cmdfunc(mtd, NAND_CMD_RNDOUT, mtd->writesize, -1);

	/* read the oob */
	chip->read_buf(mtd, oob, mtd->oobsize);

	return max_bitflips;
}

/*
 * HW ECC Correction
 *
 * function called after a read
 *
 * mtd:        MTD block structure
 * dat:        raw data read from the chip
 * read_ecc:   ECC from the chip (unused)
 * isnull:     unused
 *
 * Detect and correct a 1 bit error for a page
 */
static int atmel_nand_correct(struct mtd_info *mtd, u_char *dat,
		u_char *read_ecc, u_char *isnull)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);
	unsigned int ecc_status;
	unsigned int ecc_word, ecc_bit;

	/* get the status from the Status Register */
	ecc_status = ecc_readl(host->ecc, SR);

	/* if there's no error */
	if (likely(!(ecc_status & ATMEL_ECC_RECERR)))
		return 0;

	/* get error bit offset (4 bits) */
	ecc_bit = ecc_readl(host->ecc, PR) & ATMEL_ECC_BITADDR;
	/* get word address (12 bits) */
	ecc_word = ecc_readl(host->ecc, PR) & ATMEL_ECC_WORDADDR;
	ecc_word >>= 4;

	/* if there are multiple errors */
	if (ecc_status & ATMEL_ECC_MULERR) {
		/* check if it is a freshly erased block
		 * (filled with 0xff) */
		if ((ecc_bit == ATMEL_ECC_BITADDR)
				&& (ecc_word == (ATMEL_ECC_WORDADDR >> 4))) {
			/* the block has just been erased, return OK */
			return 0;
		}
		/* it doesn't seems to be a freshly
		 * erased block.
		 * We can't correct so many errors */
		dev_dbg(host->dev, "atmel_nand : multiple errors detected."
				" Unable to correct.\n");
		return -EBADMSG;
	}

	/* if there's a single bit error : we can correct it */
	if (ecc_status & ATMEL_ECC_ECCERR) {
		/* there's nothing much to do here.
		 * the bit error is on the ECC itself.
		 */
		dev_dbg(host->dev, "atmel_nand : one bit error on ECC code."
				" Nothing to correct\n");
		return 0;
	}

	dev_dbg(host->dev, "atmel_nand : one bit error on data."
			" (word offset in the page :"
			" 0x%x bit offset : 0x%x)\n",
			ecc_word, ecc_bit);
	/* correct the error */
	if (nand_chip->options & NAND_BUSWIDTH_16) {
		/* 16 bits words */
		((unsigned short *) dat)[ecc_word] ^= (1 << ecc_bit);
	} else {
		/* 8 bits words */
		dat[ecc_word] ^= (1 << ecc_bit);
	}
	dev_dbg(host->dev, "atmel_nand : error corrected\n");
	return 1;
}

/*
 * Enable HW ECC : unused on most chips
 */
static void atmel_nand_hwctl(struct mtd_info *mtd, int mode)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);

	if (host->board.need_reset_workaround)
		ecc_writel(host->ecc, CR, ATMEL_ECC_RST);
}

static int atmel_of_init_ecc(struct atmel_nand_host *host,
			     struct device_node *np)
{
	u32 offset[2];
	u32 val;

	host->has_pmecc = of_property_read_bool(np, "atmel,has-pmecc");

	/* Not using PMECC */
	if (!(host->nand_chip.ecc.mode == NAND_ECC_HW) || !host->has_pmecc)
		return 0;

	/* use PMECC, get correction capability, sector size and lookup
	 * table offset.
	 * If correction bits and sector size are not specified, then find
	 * them from NAND ONFI parameters.
	 */
	if (of_property_read_u32(np, "atmel,pmecc-cap", &val) == 0) {
		if (val > host->caps->pmecc_max_correction) {
			dev_err(host->dev,
				"Required ECC strength too high: %u max %u\n",
				val, host->caps->pmecc_max_correction);
			return -EINVAL;
		}
		if ((val != 2)  && (val != 4)  && (val != 8) &&
		    (val != 12) && (val != 24) && (val != 32)) {
			dev_err(host->dev,
				"Required ECC strength not supported: %u\n",
				val);
			return -EINVAL;
		}
		host->pmecc_corr_cap = (u8)val;
	}

	if (of_property_read_u32(np, "atmel,pmecc-sector-size", &val) == 0) {
		if ((val != 512) && (val != 1024)) {
			dev_err(host->dev,
				"Required ECC sector size not supported: %u\n",
				val);
			return -EINVAL;
		}
		host->pmecc_sector_size = (u16)val;
	}

	if (of_property_read_u32_array(np, "atmel,pmecc-lookup-table-offset",
			offset, 2) != 0) {
		dev_err(host->dev, "Cannot get PMECC lookup table offset, will build a lookup table in runtime.\n");
		host->has_no_lookup_table = true;
		/* Will build a lookup table and initialize the offset later */
		return 0;
	}

	if (!offset[0] && !offset[1]) {
		dev_err(host->dev, "Invalid PMECC lookup table offset\n");
		return -EINVAL;
	}

	host->pmecc_lookup_table_offset_512 = offset[0];
	host->pmecc_lookup_table_offset_1024 = offset[1];

	return 0;
}

static int atmel_of_init_port(struct atmel_nand_host *host,
			      struct device_node *np)
{
	u32 val;
	struct atmel_nand_data *board = &host->board;
	enum of_gpio_flags flags = 0;

	host->caps = (struct atmel_nand_caps *)
		of_device_get_match_data(host->dev);

	if (of_property_read_u32(np, "atmel,nand-addr-offset", &val) == 0) {
		if (val >= 32) {
			dev_err(host->dev, "invalid addr-offset %u\n", val);
			return -EINVAL;
		}
		board->ale = val;
	}

	if (of_property_read_u32(np, "atmel,nand-cmd-offset", &val) == 0) {
		if (val >= 32) {
			dev_err(host->dev, "invalid cmd-offset %u\n", val);
			return -EINVAL;
		}
		board->cle = val;
	}

	board->has_dma = of_property_read_bool(np, "atmel,nand-has-dma");

	board->rdy_pin = of_get_gpio_flags(np, 0, &flags);
	board->rdy_pin_active_low = (flags == OF_GPIO_ACTIVE_LOW);

	board->enable_pin = of_get_gpio(np, 1);
	board->det_pin = of_get_gpio(np, 2);

	/* load the nfc driver if there is */
	of_platform_populate(np, NULL, NULL, host->dev);

	/*
	 * Initialize ECC mode to NAND_ECC_SOFT so that we have a correct value
	 * even if the nand-ecc-mode property is not defined.
	 */
	host->nand_chip.ecc.mode = NAND_ECC_SOFT;
	host->nand_chip.ecc.algo = NAND_ECC_HAMMING;

	return 0;
}

static int atmel_hw_nand_init_params(struct platform_device *pdev,
					 struct atmel_nand_host *host)
{
	struct nand_chip *nand_chip = &host->nand_chip;
	struct mtd_info *mtd = nand_to_mtd(nand_chip);
	struct resource		*regs;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!regs) {
		dev_err(host->dev,
			"Can't get I/O resource regs, use software ECC\n");
		nand_chip->ecc.mode = NAND_ECC_SOFT;
		nand_chip->ecc.algo = NAND_ECC_HAMMING;
		return 0;
	}

	host->ecc = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(host->ecc))
		return PTR_ERR(host->ecc);

	/* ECC is calculated for the whole page (1 step) */
	nand_chip->ecc.size = mtd->writesize;

	/* set ECC page size and oob layout */
	switch (mtd->writesize) {
	case 512:
		mtd_set_ooblayout(mtd, &atmel_ooblayout_sp_ops);
		ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_528);
		break;
	case 1024:
		mtd_set_ooblayout(mtd, &nand_ooblayout_lp_ops);
		ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_1056);
		break;
	case 2048:
		mtd_set_ooblayout(mtd, &nand_ooblayout_lp_ops);
		ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_2112);
		break;
	case 4096:
		mtd_set_ooblayout(mtd, &nand_ooblayout_lp_ops);
		ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_4224);
		break;
	default:
		/* page size not handled by HW ECC */
		/* switching back to soft ECC */
		nand_chip->ecc.mode = NAND_ECC_SOFT;
		nand_chip->ecc.algo = NAND_ECC_HAMMING;
		return 0;
	}

	/* set up for HW ECC */
	nand_chip->ecc.calculate = atmel_nand_calculate;
	nand_chip->ecc.correct = atmel_nand_correct;
	nand_chip->ecc.hwctl = atmel_nand_hwctl;
	nand_chip->ecc.read_page = atmel_nand_read_page;
	nand_chip->ecc.bytes = 4;
	nand_chip->ecc.strength = 1;

	return 0;
}

static inline u32 nfc_read_status(struct atmel_nand_host *host)
{
	u32 err_flags = NFC_SR_DTOE | NFC_SR_UNDEF | NFC_SR_AWB | NFC_SR_ASE;
	u32 nfc_status = nfc_readl(host->nfc->hsmc_regs, SR);

	if (unlikely(nfc_status & err_flags)) {
		if (nfc_status & NFC_SR_DTOE)
			dev_err(host->dev, "NFC: Waiting Nand R/B Timeout Error\n");
		else if (nfc_status & NFC_SR_UNDEF)
			dev_err(host->dev, "NFC: Access Undefined Area Error\n");
		else if (nfc_status & NFC_SR_AWB)
			dev_err(host->dev, "NFC: Access memory While NFC is busy\n");
		else if (nfc_status & NFC_SR_ASE)
			dev_err(host->dev, "NFC: Access memory Size Error\n");
	}

	return nfc_status;
}

/* SMC interrupt service routine */
static irqreturn_t hsmc_interrupt(int irq, void *dev_id)
{
	struct atmel_nand_host *host = dev_id;
	u32 status, mask, pending;
	irqreturn_t ret = IRQ_NONE;

	status = nfc_read_status(host);
	mask = nfc_readl(host->nfc->hsmc_regs, IMR);
	pending = status & mask;

	if (pending & NFC_SR_XFR_DONE) {
		complete(&host->nfc->comp_xfer_done);
		nfc_writel(host->nfc->hsmc_regs, IDR, NFC_SR_XFR_DONE);
		ret = IRQ_HANDLED;
	}
	if (pending & NFC_SR_RB_EDGE) {
		complete(&host->nfc->comp_ready);
		nfc_writel(host->nfc->hsmc_regs, IDR, NFC_SR_RB_EDGE);
		ret = IRQ_HANDLED;
	}
	if (pending & NFC_SR_CMD_DONE) {
		complete(&host->nfc->comp_cmd_done);
		nfc_writel(host->nfc->hsmc_regs, IDR, NFC_SR_CMD_DONE);
		ret = IRQ_HANDLED;
	}

	return ret;
}

/* NFC(Nand Flash Controller) related functions */
static void nfc_prepare_interrupt(struct atmel_nand_host *host, u32 flag)
{
	if (flag & NFC_SR_XFR_DONE)
		init_completion(&host->nfc->comp_xfer_done);

	if (flag & NFC_SR_RB_EDGE)
		init_completion(&host->nfc->comp_ready);

	if (flag & NFC_SR_CMD_DONE)
		init_completion(&host->nfc->comp_cmd_done);

	/* Enable interrupt that need to wait for */
	nfc_writel(host->nfc->hsmc_regs, IER, flag);
}

static int nfc_wait_interrupt(struct atmel_nand_host *host, u32 flag)
{
	int i, index = 0;
	struct completion *comp[3];	/* Support 3 interrupt completion */

	if (flag & NFC_SR_XFR_DONE)
		comp[index++] = &host->nfc->comp_xfer_done;

	if (flag & NFC_SR_RB_EDGE)
		comp[index++] = &host->nfc->comp_ready;

	if (flag & NFC_SR_CMD_DONE)
		comp[index++] = &host->nfc->comp_cmd_done;

	if (index == 0) {
		dev_err(host->dev, "Unknown interrupt flag: 0x%08x\n", flag);
		return -EINVAL;
	}

	for (i = 0; i < index; i++) {
		if (wait_for_completion_timeout(comp[i],
				msecs_to_jiffies(NFC_TIME_OUT_MS)))
			continue;	/* wait for next completion */
		else
			goto err_timeout;
	}

	return 0;

err_timeout:
	dev_err(host->dev, "Time out to wait for interrupt: 0x%08x\n", flag);
	/* Disable the interrupt as it is not handled by interrupt handler */
	nfc_writel(host->nfc->hsmc_regs, IDR, flag);
	return -ETIMEDOUT;
}

static int nfc_send_command(struct atmel_nand_host *host,
	unsigned int cmd, unsigned int addr, unsigned char cycle0)
{
	unsigned long timeout;
	u32 flag = NFC_SR_CMD_DONE;
	flag |= cmd & NFCADDR_CMD_DATAEN ? NFC_SR_XFR_DONE : 0;

	dev_dbg(host->dev,
		"nfc_cmd: 0x%08x, addr1234: 0x%08x, cycle0: 0x%02x\n",
		cmd, addr, cycle0);

	timeout = jiffies + msecs_to_jiffies(NFC_TIME_OUT_MS);
	while (nfc_readl(host->nfc->hsmc_regs, SR) & NFC_SR_BUSY) {
		if (time_after(jiffies, timeout)) {
			dev_err(host->dev,
				"Time out to wait for NFC ready!\n");
			return -ETIMEDOUT;
		}
	}

	nfc_prepare_interrupt(host, flag);
	nfc_writel(host->nfc->hsmc_regs, CYCLE0, cycle0);
	nfc_cmd_addr1234_writel(cmd, addr, host->nfc->base_cmd_regs);
	return nfc_wait_interrupt(host, flag);
}

static int nfc_device_ready(struct mtd_info *mtd)
{
	u32 status, mask;
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);

	status = nfc_read_status(host);
	mask = nfc_readl(host->nfc->hsmc_regs, IMR);

	/* The mask should be 0. If not we may lost interrupts */
	if (unlikely(mask & status))
		dev_err(host->dev, "Lost the interrupt flags: 0x%08x\n",
				mask & status);

	return status & NFC_SR_RB_EDGE;
}

static void nfc_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(nand_chip);

	if (chip == -1)
		nfc_writel(host->nfc->hsmc_regs, CTRL, NFC_CTRL_DISABLE);
	else
		nfc_writel(host->nfc->hsmc_regs, CTRL, NFC_CTRL_ENABLE);
}

static int nfc_make_addr(struct mtd_info *mtd, int command, int column,
		int page_addr, unsigned int *addr1234, unsigned int *cycle0)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	int acycle = 0;
	unsigned char addr_bytes[8];
	int index = 0, bit_shift;

	BUG_ON(addr1234 == NULL || cycle0 == NULL);

	*cycle0 = 0;
	*addr1234 = 0;

	if (column != -1) {
		if (chip->options & NAND_BUSWIDTH_16 &&
				!nand_opcode_8bits(command))
			column >>= 1;
		addr_bytes[acycle++] = column & 0xff;
		if (mtd->writesize > 512)
			addr_bytes[acycle++] = (column >> 8) & 0xff;
	}

	if (page_addr != -1) {
		addr_bytes[acycle++] = page_addr & 0xff;
		addr_bytes[acycle++] = (page_addr >> 8) & 0xff;
		if (chip->chipsize > (128 << 20))
			addr_bytes[acycle++] = (page_addr >> 16) & 0xff;
	}

	if (acycle > 4)
		*cycle0 = addr_bytes[index++];

	for (bit_shift = 0; index < acycle; bit_shift += 8)
		*addr1234 += addr_bytes[index++] << bit_shift;

	/* return acycle in cmd register */
	return acycle << NFCADDR_CMD_ACYCLE_BIT_POS;
}

static void nfc_nand_command(struct mtd_info *mtd, unsigned int command,
				int column, int page_addr)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(chip);
	unsigned long timeout;
	unsigned int nfc_addr_cmd = 0;

	unsigned int cmd1 = command << NFCADDR_CMD_CMD1_BIT_POS;

	/* Set default settings: no cmd2, no addr cycle. read from nand */
	unsigned int cmd2 = 0;
	unsigned int vcmd2 = 0;
	int acycle = NFCADDR_CMD_ACYCLE_NONE;
	int csid = NFCADDR_CMD_CSID_3;
	int dataen = NFCADDR_CMD_DATADIS;
	int nfcwr = NFCADDR_CMD_NFCRD;
	unsigned int addr1234 = 0;
	unsigned int cycle0 = 0;
	bool do_addr = true;
	host->nfc->data_in_sram = NULL;

	dev_dbg(host->dev, "%s: cmd = 0x%02x, col = 0x%08x, page = 0x%08x\n",
	     __func__, command, column, page_addr);

	switch (command) {
	case NAND_CMD_RESET:
		nfc_addr_cmd = cmd1 | acycle | csid | dataen | nfcwr;
		nfc_send_command(host, nfc_addr_cmd, addr1234, cycle0);
		udelay(chip->chip_delay);

		nfc_nand_command(mtd, NAND_CMD_STATUS, -1, -1);
		timeout = jiffies + msecs_to_jiffies(NFC_TIME_OUT_MS);
		while (!(chip->read_byte(mtd) & NAND_STATUS_READY)) {
			if (time_after(jiffies, timeout)) {
				dev_err(host->dev,
					"Time out to wait status ready!\n");
				break;
			}
		}
		return;
	case NAND_CMD_STATUS:
		do_addr = false;
		break;
	case NAND_CMD_PARAM:
	case NAND_CMD_READID:
		do_addr = false;
		acycle = NFCADDR_CMD_ACYCLE_1;
		if (column != -1)
			addr1234 = column;
		break;
	case NAND_CMD_RNDOUT:
		cmd2 = NAND_CMD_RNDOUTSTART << NFCADDR_CMD_CMD2_BIT_POS;
		vcmd2 = NFCADDR_CMD_VCMD2;
		break;
	case NAND_CMD_READ0:
	case NAND_CMD_READOOB:
		if (command == NAND_CMD_READOOB) {
			column += mtd->writesize;
			command = NAND_CMD_READ0; /* only READ0 is valid */
			cmd1 = command << NFCADDR_CMD_CMD1_BIT_POS;
		}
		if (host->nfc->use_nfc_sram) {
			/* Enable Data transfer to sram */
			dataen = NFCADDR_CMD_DATAEN;

			/* Need enable PMECC now, since NFC will transfer
			 * data in bus after sending nfc read command.
			 */
			if (chip->ecc.mode == NAND_ECC_HW && host->has_pmecc)
				pmecc_enable(host, NAND_ECC_READ);
		}

		cmd2 = NAND_CMD_READSTART << NFCADDR_CMD_CMD2_BIT_POS;
		vcmd2 = NFCADDR_CMD_VCMD2;
		break;
	/* For prgramming command, the cmd need set to write enable */
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_SEQIN:
	case NAND_CMD_RNDIN:
		nfcwr = NFCADDR_CMD_NFCWR;
		if (host->nfc->will_write_sram && command == NAND_CMD_SEQIN)
			dataen = NFCADDR_CMD_DATAEN;
		break;
	default:
		break;
	}

	if (do_addr)
		acycle = nfc_make_addr(mtd, command, column, page_addr,
				&addr1234, &cycle0);

	nfc_addr_cmd = cmd1 | cmd2 | vcmd2 | acycle | csid | dataen | nfcwr;
	nfc_send_command(host, nfc_addr_cmd, addr1234, cycle0);

	/*
	 * Program and erase have their own busy handlers status, sequential
	 * in, and deplete1 need no delay.
	 */
	switch (command) {
	case NAND_CMD_CACHEDPROG:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_RNDIN:
	case NAND_CMD_STATUS:
	case NAND_CMD_RNDOUT:
	case NAND_CMD_SEQIN:
	case NAND_CMD_READID:
		return;

	case NAND_CMD_READ0:
		if (dataen == NFCADDR_CMD_DATAEN) {
			host->nfc->data_in_sram = host->nfc->sram_bank0 +
				nfc_get_sram_off(host);
			return;
		}
		/* fall through */
	default:
		nfc_prepare_interrupt(host, NFC_SR_RB_EDGE);
		nfc_wait_interrupt(host, NFC_SR_RB_EDGE);
	}
}

static int nfc_sram_write_page(struct mtd_info *mtd, struct nand_chip *chip,
			uint32_t offset, int data_len, const uint8_t *buf,
			int oob_required, int page, int cached, int raw)
{
	int cfg, len;
	int status = 0;
	struct atmel_nand_host *host = nand_get_controller_data(chip);
	void *sram = host->nfc->sram_bank0 + nfc_get_sram_off(host);

	/* Subpage write is not supported */
	if (offset || (data_len < mtd->writesize))
		return -EINVAL;

	len = mtd->writesize;
	/* Copy page data to sram that will write to nand via NFC */
	if (use_dma) {
		if (atmel_nand_dma_op(mtd, (void *)buf, len, 0) != 0)
			/* Fall back to use cpu copy */
			memcpy(sram, buf, len);
	} else {
		memcpy(sram, buf, len);
	}

	cfg = nfc_readl(host->nfc->hsmc_regs, CFG);
	if (unlikely(raw) && oob_required) {
		memcpy(sram + len, chip->oob_poi, mtd->oobsize);
		len += mtd->oobsize;
		nfc_writel(host->nfc->hsmc_regs, CFG, cfg | NFC_CFG_WSPARE);
	} else {
		nfc_writel(host->nfc->hsmc_regs, CFG, cfg & ~NFC_CFG_WSPARE);
	}

	if (chip->ecc.mode == NAND_ECC_HW && host->has_pmecc)
		/*
		 * When use NFC sram, need set up PMECC before send
		 * NAND_CMD_SEQIN command. Since when the nand command
		 * is sent, nfc will do transfer from sram and nand.
		 */
		pmecc_enable(host, NAND_ECC_WRITE);

	host->nfc->will_write_sram = true;
	chip->cmdfunc(mtd, NAND_CMD_SEQIN, 0x00, page);
	host->nfc->will_write_sram = false;

	if (likely(!raw))
		/* Need to write ecc into oob */
		status = chip->ecc.write_page(mtd, chip, buf, oob_required,
					      page);

	if (status < 0)
		return status;

	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);

	if ((status & NAND_STATUS_FAIL) && (chip->errstat))
		status = chip->errstat(mtd, chip, FL_WRITING, status, page);

	if (status & NAND_STATUS_FAIL)
		return -EIO;

	return 0;
}

static int nfc_sram_init(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct atmel_nand_host *host = nand_get_controller_data(chip);
	int res = 0;

	/* Initialize the NFC CFG register */
	unsigned int cfg_nfc = 0;

	/* set page size and oob layout */
	switch (mtd->writesize) {
	case 512:
		cfg_nfc = NFC_CFG_PAGESIZE_512;
		break;
	case 1024:
		cfg_nfc = NFC_CFG_PAGESIZE_1024;
		break;
	case 2048:
		cfg_nfc = NFC_CFG_PAGESIZE_2048;
		break;
	case 4096:
		cfg_nfc = NFC_CFG_PAGESIZE_4096;
		break;
	case 8192:
		cfg_nfc = NFC_CFG_PAGESIZE_8192;
		break;
	default:
		dev_err(host->dev, "Unsupported page size for NFC.\n");
		res = -ENXIO;
		return res;
	}

	/* oob bytes size = (NFCSPARESIZE + 1) * 4
	 * Max support spare size is 512 bytes. */
	cfg_nfc |= (((mtd->oobsize / 4) - 1) << NFC_CFG_NFC_SPARESIZE_BIT_POS
		& NFC_CFG_NFC_SPARESIZE);
	/* default set a max timeout */
	cfg_nfc |= NFC_CFG_RSPARE |
			NFC_CFG_NFC_DTOCYC | NFC_CFG_NFC_DTOMUL;

	nfc_writel(host->nfc->hsmc_regs, CFG, cfg_nfc);

	host->nfc->will_write_sram = false;
	nfc_set_sram_bank(host, 0);

	/* Use Write page with NFC SRAM only for PMECC or ECC NONE. */
	if (host->nfc->write_by_sram) {
		if ((chip->ecc.mode == NAND_ECC_HW && host->has_pmecc) ||
				chip->ecc.mode == NAND_ECC_NONE)
			chip->write_page = nfc_sram_write_page;
		else
			host->nfc->write_by_sram = false;
	}

	dev_info(host->dev, "Using NFC Sram read %s\n",
			host->nfc->write_by_sram ? "and write" : "");
	return 0;
}

static struct platform_driver atmel_nand_nfc_driver;
/*
 * Probe for the NAND device.
 */
static int atmel_nand_probe(struct platform_device *pdev)
{
	struct atmel_nand_host *host;
	struct mtd_info *mtd;
	struct nand_chip *nand_chip;
	struct resource *mem;
	int res, irq;

	/* Allocate memory for the device structure (and zero it) */
	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	res = platform_driver_register(&atmel_nand_nfc_driver);
	if (res)
		dev_err(&pdev->dev, "atmel_nand: can't register NFC driver\n");

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->io_base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(host->io_base)) {
		res = PTR_ERR(host->io_base);
		goto err_nand_ioremap;
	}
	host->io_phys = (dma_addr_t)mem->start;

	nand_chip = &host->nand_chip;
	mtd = nand_to_mtd(nand_chip);
	host->dev = &pdev->dev;
	if (IS_ENABLED(CONFIG_OF) && pdev->dev.of_node) {
		nand_set_flash_node(nand_chip, pdev->dev.of_node);
		/* Only when CONFIG_OF is enabled of_node can be parsed */
		res = atmel_of_init_port(host, pdev->dev.of_node);
		if (res)
			goto err_nand_ioremap;
	} else {
		memcpy(&host->board, dev_get_platdata(&pdev->dev),
		       sizeof(struct atmel_nand_data));
		nand_chip->ecc.mode = host->board.ecc_mode;

		/*
		 * When using software ECC every supported avr32 board means
		 * Hamming algorithm. If that ever changes we'll need to add
		 * ecc_algo field to the struct atmel_nand_data.
		 */
		if (nand_chip->ecc.mode == NAND_ECC_SOFT)
			nand_chip->ecc.algo = NAND_ECC_HAMMING;

		/* 16-bit bus width */
		if (host->board.bus_width_16)
			nand_chip->options |= NAND_BUSWIDTH_16;
	}

	 /* link the private data structures */
	nand_set_controller_data(nand_chip, host);
	mtd->dev.parent = &pdev->dev;

	/* Set address of NAND IO lines */
	nand_chip->IO_ADDR_R = host->io_base;
	nand_chip->IO_ADDR_W = host->io_base;

	if (nand_nfc.is_initialized) {
		/* NFC driver is probed and initialized */
		host->nfc = &nand_nfc;

		nand_chip->select_chip = nfc_select_chip;
		nand_chip->dev_ready = nfc_device_ready;
		nand_chip->cmdfunc = nfc_nand_command;

		/* Initialize the interrupt for NFC */
		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			dev_err(host->dev, "Cannot get HSMC irq!\n");
			res = irq;
			goto err_nand_ioremap;
		}

		res = devm_request_irq(&pdev->dev, irq, hsmc_interrupt,
				0, "hsmc", host);
		if (res) {
			dev_err(&pdev->dev, "Unable to request HSMC irq %d\n",
				irq);
			goto err_nand_ioremap;
		}
	} else {
		res = atmel_nand_set_enable_ready_pins(mtd);
		if (res)
			goto err_nand_ioremap;

		nand_chip->cmd_ctrl = atmel_nand_cmd_ctrl;
	}

	nand_chip->chip_delay = 40;		/* 40us command delay time */


	nand_chip->read_buf = atmel_read_buf;
	nand_chip->write_buf = atmel_write_buf;

	platform_set_drvdata(pdev, host);
	atmel_nand_enable(host);

	if (gpio_is_valid(host->board.det_pin)) {
		res = devm_gpio_request(&pdev->dev,
				host->board.det_pin, "nand_det");
		if (res < 0) {
			dev_err(&pdev->dev,
				"can't request det gpio %d\n",
				host->board.det_pin);
			goto err_no_card;
		}

		res = gpio_direction_input(host->board.det_pin);
		if (res < 0) {
			dev_err(&pdev->dev,
				"can't request input direction det gpio %d\n",
				host->board.det_pin);
			goto err_no_card;
		}

		if (gpio_get_value(host->board.det_pin)) {
			dev_info(&pdev->dev, "No SmartMedia card inserted.\n");
			res = -ENXIO;
			goto err_no_card;
		}
	}

	if (!host->board.has_dma)
		use_dma = 0;

	if (use_dma) {
		dma_cap_mask_t mask;

		dma_cap_zero(mask);
		dma_cap_set(DMA_MEMCPY, mask);
		host->dma_chan = dma_request_channel(mask, NULL, NULL);
		if (!host->dma_chan) {
			dev_err(host->dev, "Failed to request DMA channel\n");
			use_dma = 0;
		}
	}
	if (use_dma)
		dev_info(host->dev, "Using %s for DMA transfers.\n",
					dma_chan_name(host->dma_chan));
	else
		dev_info(host->dev, "No DMA support for NAND access.\n");

	/* first scan to find the device and get the page size */
	if (nand_scan_ident(mtd, 1, NULL)) {
		res = -ENXIO;
		goto err_scan_ident;
	}

	if (host->board.on_flash_bbt || on_flash_bbt)
		nand_chip->bbt_options |= NAND_BBT_USE_FLASH;

	if (nand_chip->bbt_options & NAND_BBT_USE_FLASH)
		dev_info(&pdev->dev, "Use On Flash BBT\n");

	if (IS_ENABLED(CONFIG_OF) && pdev->dev.of_node) {
		res = atmel_of_init_ecc(host, pdev->dev.of_node);
		if (res)
			goto err_hw_ecc;
	}

	if (nand_chip->ecc.mode == NAND_ECC_HW) {
		if (host->has_pmecc)
			res = atmel_pmecc_nand_init_params(pdev, host);
		else
			res = atmel_hw_nand_init_params(pdev, host);

		if (res != 0)
			goto err_hw_ecc;
	}

	/* initialize the nfc configuration register */
	if (host->nfc && host->nfc->use_nfc_sram) {
		res = nfc_sram_init(mtd);
		if (res) {
			host->nfc->use_nfc_sram = false;
			dev_err(host->dev, "Disable use nfc sram for data transfer.\n");
		}
	}

	/* second phase scan */
	if (nand_scan_tail(mtd)) {
		res = -ENXIO;
		goto err_scan_tail;
	}

	mtd->name = "atmel_nand";
	res = mtd_device_register(mtd, host->board.parts,
				  host->board.num_parts);
	if (!res)
		return res;

err_scan_tail:
	if (host->has_pmecc && host->nand_chip.ecc.mode == NAND_ECC_HW)
		pmecc_writel(host->ecc, CTRL, PMECC_CTRL_DISABLE);
err_hw_ecc:
err_scan_ident:
err_no_card:
	atmel_nand_disable(host);
	if (host->dma_chan)
		dma_release_channel(host->dma_chan);
err_nand_ioremap:
	return res;
}

/*
 * Remove a NAND device.
 */
static int atmel_nand_remove(struct platform_device *pdev)
{
	struct atmel_nand_host *host = platform_get_drvdata(pdev);
	struct mtd_info *mtd = nand_to_mtd(&host->nand_chip);

	nand_release(mtd);

	atmel_nand_disable(host);

	if (host->has_pmecc && host->nand_chip.ecc.mode == NAND_ECC_HW) {
		pmecc_writel(host->ecc, CTRL, PMECC_CTRL_DISABLE);
		pmerrloc_writel(host->pmerrloc_base, ELDIS,
				PMERRLOC_DISABLE);
	}

	if (host->dma_chan)
		dma_release_channel(host->dma_chan);

	platform_driver_unregister(&atmel_nand_nfc_driver);

	return 0;
}

/*
 * AT91RM9200 does not have PMECC or PMECC Errloc peripherals for
 * BCH ECC. Combined with the "atmel,has-pmecc", it is used to describe
 * devices from the SAM9 family that have those.
 */
static const struct atmel_nand_caps at91rm9200_caps = {
	.pmecc_correct_erase_page = false,
	.pmecc_max_correction = 24,
};

static const struct atmel_nand_caps sama5d4_caps = {
	.pmecc_correct_erase_page = true,
	.pmecc_max_correction = 24,
};

/*
 * The PMECC Errloc controller starting in SAMA5D2 is not compatible,
 * as the increased correction strength requires more registers.
 */
static const struct atmel_nand_caps sama5d2_caps = {
	.pmecc_correct_erase_page = true,
	.pmecc_max_correction = 32,
};

static const struct of_device_id atmel_nand_dt_ids[] = {
	{ .compatible = "atmel,at91rm9200-nand", .data = &at91rm9200_caps },
	{ .compatible = "atmel,sama5d4-nand", .data = &sama5d4_caps },
	{ .compatible = "atmel,sama5d2-nand", .data = &sama5d2_caps },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmel_nand_dt_ids);

static int atmel_nand_nfc_probe(struct platform_device *pdev)
{
	struct atmel_nfc *nfc = &nand_nfc;
	struct resource *nfc_cmd_regs, *nfc_hsmc_regs, *nfc_sram;
	int ret;

	nfc_cmd_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nfc->base_cmd_regs = devm_ioremap_resource(&pdev->dev, nfc_cmd_regs);
	if (IS_ERR(nfc->base_cmd_regs))
		return PTR_ERR(nfc->base_cmd_regs);

	nfc_hsmc_regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	nfc->hsmc_regs = devm_ioremap_resource(&pdev->dev, nfc_hsmc_regs);
	if (IS_ERR(nfc->hsmc_regs))
		return PTR_ERR(nfc->hsmc_regs);

	nfc_sram = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (nfc_sram) {
		nfc->sram_bank0 = (void * __force)
				devm_ioremap_resource(&pdev->dev, nfc_sram);
		if (IS_ERR(nfc->sram_bank0)) {
			dev_warn(&pdev->dev, "Fail to ioremap the NFC sram with error: %ld. So disable NFC sram.\n",
					PTR_ERR(nfc->sram_bank0));
		} else {
			nfc->use_nfc_sram = true;
			nfc->sram_bank0_phys = (dma_addr_t)nfc_sram->start;

			if (pdev->dev.of_node)
				nfc->write_by_sram = of_property_read_bool(
						pdev->dev.of_node,
						"atmel,write-by-sram");
		}
	}

	nfc_writel(nfc->hsmc_regs, IDR, 0xffffffff);
	nfc_readl(nfc->hsmc_regs, SR);	/* clear the NFC_SR */

	nfc->clk = devm_clk_get(&pdev->dev, NULL);
	if (!IS_ERR(nfc->clk)) {
		ret = clk_prepare_enable(nfc->clk);
		if (ret)
			return ret;
	} else {
		dev_warn(&pdev->dev, "NFC clock missing, update your Device Tree");
	}

	nfc->is_initialized = true;
	dev_info(&pdev->dev, "NFC is probed.\n");

	return 0;
}

static int atmel_nand_nfc_remove(struct platform_device *pdev)
{
	struct atmel_nfc *nfc = &nand_nfc;

	if (!IS_ERR(nfc->clk))
		clk_disable_unprepare(nfc->clk);

	return 0;
}

static const struct of_device_id atmel_nand_nfc_match[] = {
	{ .compatible = "atmel,sama5d3-nfc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_nand_nfc_match);

static struct platform_driver atmel_nand_nfc_driver = {
	.driver = {
		.name = "atmel_nand_nfc",
		.of_match_table = of_match_ptr(atmel_nand_nfc_match),
	},
	.probe = atmel_nand_nfc_probe,
	.remove = atmel_nand_nfc_remove,
};

static struct platform_driver atmel_nand_driver = {
	.probe		= atmel_nand_probe,
	.remove		= atmel_nand_remove,
	.driver		= {
		.name	= "atmel_nand",
		.of_match_table	= of_match_ptr(atmel_nand_dt_ids),
	},
};

module_platform_driver(atmel_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rick Bronson");
MODULE_DESCRIPTION("NAND/SmartMedia driver for AT91 / AVR32");
MODULE_ALIAS("platform:atmel_nand");
