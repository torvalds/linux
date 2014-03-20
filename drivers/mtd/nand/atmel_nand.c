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

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_mtd.h>
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

/* oob layout for large page size
 * bad block info is on bytes 0 and 1
 * the bytes have to be consecutives to avoid
 * several NAND_CMD_RNDOUT during read
 */
static struct nand_ecclayout atmel_oobinfo_large = {
	.eccbytes = 4,
	.eccpos = {60, 61, 62, 63},
	.oobfree = {
		{2, 58}
	},
};

/* oob layout for small page size
 * bad block info is on bytes 4 and 5
 * the bytes have to be consecutives to avoid
 * several NAND_CMD_RNDOUT during read
 */
static struct nand_ecclayout atmel_oobinfo_small = {
	.eccbytes = 4,
	.eccpos = {0, 1, 2, 3},
	.oobfree = {
		{6, 10}
	},
};

struct atmel_nfc {
	void __iomem		*base_cmd_regs;
	void __iomem		*hsmc_regs;
	void __iomem		*sram_bank0;
	dma_addr_t		sram_bank0_phys;
	bool			use_nfc_sram;
	bool			write_by_sram;

	bool			is_initialized;
	struct completion	comp_nfc;

	/* Point to the sram bank which include readed data via NFC */
	void __iomem		*data_in_sram;
	bool			will_write_sram;
};
static struct atmel_nfc	nand_nfc;

struct atmel_nand_host {
	struct nand_chip	nand_chip;
	struct mtd_info		mtd;
	void __iomem		*io_base;
	dma_addr_t		io_phys;
	struct atmel_nand_data	board;
	struct device		*dev;
	void __iomem		*ecc;

	struct completion	comp;
	struct dma_chan		*dma_chan;

	struct atmel_nfc	*nfc;

	bool			has_pmecc;
	u8			pmecc_corr_cap;
	u16			pmecc_sector_size;
	u32			pmecc_lookup_table_offset;
	u32			pmecc_lookup_table_offset_512;
	u32			pmecc_lookup_table_offset_1024;

	int			pmecc_bytes_per_sector;
	int			pmecc_sector_number;
	int			pmecc_degree;	/* Degree of remainders */
	int			pmecc_cw_len;	/* Length of codeword */

	void __iomem		*pmerrloc_base;
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

static struct nand_ecclayout atmel_pmecc_oobinfo;

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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

	return gpio_get_value(host->board.rdy_pin) ^
                !!host->board.rdy_pin_active_low;
}

/* Set up for hardware ready pin and enable pin. */
static int atmel_nand_set_enable_ready_pins(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct atmel_nand_host *host = chip->priv;
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

static void memcpy32_fromio(void *trg, const void __iomem  *src, size_t size)
{
	int i;
	u32 *t = trg;
	const __iomem u32 *s = src;

	for (i = 0; i < (size >> 2); i++)
		*t++ = readl_relaxed(s++);
}

static void memcpy32_toio(void __iomem *trg, const void *src, int size)
{
	int i;
	u32 __iomem *t = trg;
	const u32 *s = src;

	for (i = 0; i < (size >> 2); i++)
		writel_relaxed(*s++, t++);
}

/*
 * Minimal-overhead PIO for data access.
 */
static void atmel_read_buf8(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

	if (host->nfc && host->nfc->use_nfc_sram && host->nfc->data_in_sram) {
		memcpy32_fromio(buf, host->nfc->data_in_sram, len);
		host->nfc->data_in_sram += len;
	} else {
		__raw_readsb(nand_chip->IO_ADDR_R, buf, len);
	}
}

static void atmel_read_buf16(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

	if (host->nfc && host->nfc->use_nfc_sram && host->nfc->data_in_sram) {
		memcpy32_fromio(buf, host->nfc->data_in_sram, len);
		host->nfc->data_in_sram += len;
	} else {
		__raw_readsw(nand_chip->IO_ADDR_R, buf, len / 2);
	}
}

static void atmel_write_buf8(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd->priv;

	__raw_writesb(nand_chip->IO_ADDR_W, buf, len);
}

static void atmel_write_buf16(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd->priv;

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
		/* Only for a 2k-page or lower flash, NFC can handle 2 banks */
		if (host->mtd.writesize > 2048)
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
	struct nand_chip *chip = mtd->priv;
	struct atmel_nand_host *host = chip->priv;
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
		dev_warn(host->dev, "Fall back to CPU I/O\n");
	return err;
}

static void atmel_read_buf(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct atmel_nand_host *host = chip->priv;

	if (use_dma && len > mtd->oobsize)
		/* only use DMA for bigger than oob size: better performances */
		if (atmel_nand_dma_op(mtd, buf, len, 1) == 0)
			return;

	if (host->board.bus_width_16)
		atmel_read_buf16(mtd, buf, len);
	else
		atmel_read_buf8(mtd, buf, len);
}

static void atmel_write_buf(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct atmel_nand_host *host = chip->priv;

	if (use_dma && len > mtd->oobsize)
		/* only use DMA for bigger than oob size: better performances */
		if (atmel_nand_dma_op(mtd, (void *)buf, len, 0) == 0)
			return;

	if (host->board.bus_width_16)
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
 */
static int pmecc_get_ecc_bytes(int cap, int sector_size)
{
	int m = 12 + sector_size / 512;
	return (m * cap + 7) / 8;
}

static void pmecc_config_ecc_layout(struct nand_ecclayout *layout,
				    int oobsize, int ecc_len)
{
	int i;

	layout->eccbytes = ecc_len;

	/* ECC will occupy the last ecc_len bytes continuously */
	for (i = 0; i < ecc_len; i++)
		layout->eccpos[i] = oobsize - ecc_len + i;

	layout->oobfree[0].offset = 2;
	layout->oobfree[0].length =
		oobsize - ecc_len - layout->oobfree[0].offset;
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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
	int i = 0;
	int byte_pos, bit_pos, sector_size, pos;
	uint32_t tmp;
	uint8_t err_byte;

	sector_size = host->pmecc_sector_size;

	while (err_nbr) {
		tmp = pmerrloc_readl_el_relaxed(host->pmerrloc_base, i) - 1;
		byte_pos = tmp / 8;
		bit_pos  = tmp % 8;

		if (byte_pos >= (sector_size + extra_bytes))
			BUG();	/* should never happen */

		if (byte_pos < sector_size) {
			err_byte = *(buf + byte_pos);
			*(buf + byte_pos) ^= (1 << bit_pos);

			pos = sector_num * host->pmecc_sector_size + byte_pos;
			dev_info(host->dev, "Bit flip in data area, byte_pos: %d, bit_pos: %d, 0x%02x -> 0x%02x\n",
				pos, bit_pos, err_byte, *(buf + byte_pos));
		} else {
			/* Bit flip in OOB area */
			tmp = sector_num * host->pmecc_bytes_per_sector
					+ (byte_pos - sector_size);
			err_byte = ecc[tmp];
			ecc[tmp] ^= (1 << bit_pos);

			pos = tmp + nand_chip->ecc.layout->eccpos[0];
			dev_info(host->dev, "Bit flip in OOB, oob_byte_pos: %d, bit_pos: %d, 0x%02x -> 0x%02x\n",
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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
	int i, err_nbr, eccbytes;
	uint8_t *buf_pos;
	int total_err = 0;

	eccbytes = nand_chip->ecc.bytes;
	for (i = 0; i < eccbytes; i++)
		if (ecc[i] != 0xff)
			goto normal_check;
	/* Erased page, return OK */
	return 0;

normal_check:
	for (i = 0; i < host->pmecc_sector_number; i++) {
		err_nbr = 0;
		if (pmecc_stat & 0x1) {
			buf_pos = buf + i * host->pmecc_sector_size;

			pmecc_gen_syndrome(mtd, i);
			pmecc_substitute(mtd);
			pmecc_get_sigma(mtd);

			err_nbr = pmecc_err_location(mtd);
			if (err_nbr == -1) {
				dev_err(host->dev, "PMECC: Too many errors\n");
				mtd->ecc_stats.failed++;
				return -EIO;
			} else {
				pmecc_correct_data(mtd, buf_pos, ecc, i,
					host->pmecc_bytes_per_sector, err_nbr);
				mtd->ecc_stats.corrected += err_nbr;
				total_err += err_nbr;
			}
		}
		pmecc_stat >>= 1;
	}

	return total_err;
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
	struct atmel_nand_host *host = chip->priv;
	int eccsize = chip->ecc.size;
	uint8_t *oob = chip->oob_poi;
	uint32_t *eccpos = chip->ecc.layout->eccpos;
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
		bitflips = pmecc_correction(mtd, stat, buf, &oob[eccpos[0]]);
		if (bitflips < 0)
			/* uncorrectable errors */
			return 0;
	}

	return bitflips;
}

static int atmel_nand_pmecc_write_page(struct mtd_info *mtd,
		struct nand_chip *chip, const uint8_t *buf, int oob_required)
{
	struct atmel_nand_host *host = chip->priv;
	uint32_t *eccpos = chip->ecc.layout->eccpos;
	int i, j;
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

	for (i = 0; i < host->pmecc_sector_number; i++) {
		for (j = 0; j < host->pmecc_bytes_per_sector; j++) {
			int pos;

			pos = i * host->pmecc_bytes_per_sector + j;
			chip->oob_poi[eccpos[pos]] =
				pmecc_readb_ecc_relaxed(host->ecc, i, j);
		}
	}
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

static void atmel_pmecc_core_init(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
	uint32_t val = 0;
	struct nand_ecclayout *ecc_layout;

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
	}

	if (host->pmecc_sector_size == 512)
		val |= PMECC_CFG_SECTOR512;
	else if (host->pmecc_sector_size == 1024)
		val |= PMECC_CFG_SECTOR1024;

	switch (host->pmecc_sector_number) {
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

	ecc_layout = nand_chip->ecc.layout;
	pmecc_writel(host->ecc, SAREA, mtd->oobsize - 1);
	pmecc_writel(host->ecc, SADDR, ecc_layout->eccpos[0]);
	pmecc_writel(host->ecc, EADDR,
			ecc_layout->eccpos[ecc_layout->eccbytes - 1]);
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

static int atmel_pmecc_nand_init_params(struct platform_device *pdev,
					 struct atmel_nand_host *host)
{
	struct mtd_info *mtd = &host->mtd;
	struct nand_chip *nand_chip = &host->nand_chip;
	struct resource *regs, *regs_pmerr, *regs_rom;
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
		return 0;
	}

	host->ecc = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(host->ecc)) {
		dev_err(host->dev, "ioremap failed\n");
		err_no = PTR_ERR(host->ecc);
		goto err;
	}

	regs_pmerr = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	host->pmerrloc_base = devm_ioremap_resource(&pdev->dev, regs_pmerr);
	if (IS_ERR(host->pmerrloc_base)) {
		dev_err(host->dev,
			"Can not get I/O resource for PMECC ERRLOC controller!\n");
		err_no = PTR_ERR(host->pmerrloc_base);
		goto err;
	}

	regs_rom = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	host->pmecc_rom_base = devm_ioremap_resource(&pdev->dev, regs_rom);
	if (IS_ERR(host->pmecc_rom_base)) {
		dev_err(host->dev, "Can not get I/O resource for ROM!\n");
		err_no = PTR_ERR(host->pmecc_rom_base);
		goto err;
	}

	/* ECC is calculated for the whole page (1 step) */
	nand_chip->ecc.size = mtd->writesize;

	/* set ECC page size and oob layout */
	switch (mtd->writesize) {
	case 2048:
		host->pmecc_degree = (sector_size == 512) ?
			PMECC_GF_DIMENSION_13 : PMECC_GF_DIMENSION_14;
		host->pmecc_cw_len = (1 << host->pmecc_degree) - 1;
		host->pmecc_sector_number = mtd->writesize / sector_size;
		host->pmecc_bytes_per_sector = pmecc_get_ecc_bytes(
			cap, sector_size);
		host->pmecc_alpha_to = pmecc_get_alpha_to(host);
		host->pmecc_index_of = host->pmecc_rom_base +
			host->pmecc_lookup_table_offset;

		nand_chip->ecc.steps = 1;
		nand_chip->ecc.strength = cap;
		nand_chip->ecc.bytes = host->pmecc_bytes_per_sector *
				       host->pmecc_sector_number;
		if (nand_chip->ecc.bytes > mtd->oobsize - 2) {
			dev_err(host->dev, "No room for ECC bytes\n");
			err_no = -EINVAL;
			goto err;
		}
		pmecc_config_ecc_layout(&atmel_pmecc_oobinfo,
					mtd->oobsize,
					nand_chip->ecc.bytes);
		nand_chip->ecc.layout = &atmel_pmecc_oobinfo;
		break;
	case 512:
	case 1024:
	case 4096:
		/* TODO */
		dev_warn(host->dev,
			"Unsupported page size for PMECC, use Software ECC\n");
	default:
		/* page size not handled by HW ECC */
		/* switching back to soft ECC */
		nand_chip->ecc.mode = NAND_ECC_SOFT;
		return 0;
	}

	/* Allocate data for PMECC computation */
	err_no = pmecc_data_alloc(host);
	if (err_no) {
		dev_err(host->dev,
				"Cannot allocate memory for PMECC computation!\n");
		goto err;
	}

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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
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
	uint32_t *eccpos = chip->ecc.layout->eccpos;
	uint8_t *p = buf;
	uint8_t *oob = chip->oob_poi;
	uint8_t *ecc_pos;
	int stat;
	unsigned int max_bitflips = 0;

	/*
	 * Errata: ALE is incorrectly wired up to the ECC controller
	 * on the AP7000, so it will include the address cycles in the
	 * ECC calculation.
	 *
	 * Workaround: Reset the parity registers before reading the
	 * actual data.
	 */
	struct atmel_nand_host *host = chip->priv;
	if (host->board.need_reset_workaround)
		ecc_writel(host->ecc, CR, ATMEL_ECC_RST);

	/* read the page */
	chip->read_buf(mtd, p, eccsize);

	/* move to ECC position if needed */
	if (eccpos[0] != 0) {
		/* This only works on large pages
		 * because the ECC controller waits for
		 * NAND_CMD_RNDOUTSTART after the
		 * NAND_CMD_RNDOUT.
		 * anyway, for small pages, the eccpos[0] == 0
		 */
		chip->cmdfunc(mtd, NAND_CMD_RNDOUT,
				mtd->writesize + eccpos[0], -1);
	}

	/* the ECC controller needs to read the ECC just after the data */
	ecc_pos = oob + eccpos[0];
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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
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
		return -EIO;
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
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

	if (host->board.need_reset_workaround)
		ecc_writel(host->ecc, CR, ATMEL_ECC_RST);
}

static int atmel_of_init_port(struct atmel_nand_host *host,
			      struct device_node *np)
{
	u32 val;
	u32 offset[2];
	int ecc_mode;
	struct atmel_nand_data *board = &host->board;
	enum of_gpio_flags flags = 0;

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

	ecc_mode = of_get_nand_ecc_mode(np);

	board->ecc_mode = ecc_mode < 0 ? NAND_ECC_SOFT : ecc_mode;

	board->on_flash_bbt = of_get_nand_on_flash_bbt(np);

	board->has_dma = of_property_read_bool(np, "atmel,nand-has-dma");

	if (of_get_nand_bus_width(np) == 16)
		board->bus_width_16 = 1;

	board->rdy_pin = of_get_gpio_flags(np, 0, &flags);
	board->rdy_pin_active_low = (flags == OF_GPIO_ACTIVE_LOW);

	board->enable_pin = of_get_gpio(np, 1);
	board->det_pin = of_get_gpio(np, 2);

	host->has_pmecc = of_property_read_bool(np, "atmel,has-pmecc");

	/* load the nfc driver if there is */
	of_platform_populate(np, NULL, NULL, host->dev);

	if (!(board->ecc_mode == NAND_ECC_HW) || !host->has_pmecc)
		return 0;	/* Not using PMECC */

	/* use PMECC, get correction capability, sector size and lookup
	 * table offset.
	 * If correction bits and sector size are not specified, then find
	 * them from NAND ONFI parameters.
	 */
	if (of_property_read_u32(np, "atmel,pmecc-cap", &val) == 0) {
		if ((val != 2) && (val != 4) && (val != 8) && (val != 12) &&
				(val != 24)) {
			dev_err(host->dev,
				"Unsupported PMECC correction capability: %d; should be 2, 4, 8, 12 or 24\n",
				val);
			return -EINVAL;
		}
		host->pmecc_corr_cap = (u8)val;
	}

	if (of_property_read_u32(np, "atmel,pmecc-sector-size", &val) == 0) {
		if ((val != 512) && (val != 1024)) {
			dev_err(host->dev,
				"Unsupported PMECC sector size: %d; should be 512 or 1024 bytes\n",
				val);
			return -EINVAL;
		}
		host->pmecc_sector_size = (u16)val;
	}

	if (of_property_read_u32_array(np, "atmel,pmecc-lookup-table-offset",
			offset, 2) != 0) {
		dev_err(host->dev, "Cannot get PMECC lookup table offset\n");
		return -EINVAL;
	}
	if (!offset[0] && !offset[1]) {
		dev_err(host->dev, "Invalid PMECC lookup table offset\n");
		return -EINVAL;
	}
	host->pmecc_lookup_table_offset_512 = offset[0];
	host->pmecc_lookup_table_offset_1024 = offset[1];

	return 0;
}

static int atmel_hw_nand_init_params(struct platform_device *pdev,
					 struct atmel_nand_host *host)
{
	struct mtd_info *mtd = &host->mtd;
	struct nand_chip *nand_chip = &host->nand_chip;
	struct resource		*regs;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!regs) {
		dev_err(host->dev,
			"Can't get I/O resource regs, use software ECC\n");
		nand_chip->ecc.mode = NAND_ECC_SOFT;
		return 0;
	}

	host->ecc = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(host->ecc)) {
		dev_err(host->dev, "ioremap failed\n");
		return PTR_ERR(host->ecc);
	}

	/* ECC is calculated for the whole page (1 step) */
	nand_chip->ecc.size = mtd->writesize;

	/* set ECC page size and oob layout */
	switch (mtd->writesize) {
	case 512:
		nand_chip->ecc.layout = &atmel_oobinfo_small;
		ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_528);
		break;
	case 1024:
		nand_chip->ecc.layout = &atmel_oobinfo_large;
		ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_1056);
		break;
	case 2048:
		nand_chip->ecc.layout = &atmel_oobinfo_large;
		ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_2112);
		break;
	case 4096:
		nand_chip->ecc.layout = &atmel_oobinfo_large;
		ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_4224);
		break;
	default:
		/* page size not handled by HW ECC */
		/* switching back to soft ECC */
		nand_chip->ecc.mode = NAND_ECC_SOFT;
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

/* SMC interrupt service routine */
static irqreturn_t hsmc_interrupt(int irq, void *dev_id)
{
	struct atmel_nand_host *host = dev_id;
	u32 status, mask, pending;
	irqreturn_t ret = IRQ_HANDLED;

	status = nfc_readl(host->nfc->hsmc_regs, SR);
	mask = nfc_readl(host->nfc->hsmc_regs, IMR);
	pending = status & mask;

	if (pending & NFC_SR_XFR_DONE) {
		complete(&host->nfc->comp_nfc);
		nfc_writel(host->nfc->hsmc_regs, IDR, NFC_SR_XFR_DONE);
	} else if (pending & NFC_SR_RB_EDGE) {
		complete(&host->nfc->comp_nfc);
		nfc_writel(host->nfc->hsmc_regs, IDR, NFC_SR_RB_EDGE);
	} else if (pending & NFC_SR_CMD_DONE) {
		complete(&host->nfc->comp_nfc);
		nfc_writel(host->nfc->hsmc_regs, IDR, NFC_SR_CMD_DONE);
	} else {
		ret = IRQ_NONE;
	}

	return ret;
}

/* NFC(Nand Flash Controller) related functions */
static int nfc_wait_interrupt(struct atmel_nand_host *host, u32 flag)
{
	unsigned long timeout;
	init_completion(&host->nfc->comp_nfc);

	/* Enable interrupt that need to wait for */
	nfc_writel(host->nfc->hsmc_regs, IER, flag);

	timeout = wait_for_completion_timeout(&host->nfc->comp_nfc,
			msecs_to_jiffies(NFC_TIME_OUT_MS));
	if (timeout)
		return 0;

	/* Time out to wait for the interrupt */
	dev_err(host->dev, "Time out to wait for interrupt: 0x%08x\n", flag);
	return -ETIMEDOUT;
}

static int nfc_send_command(struct atmel_nand_host *host,
	unsigned int cmd, unsigned int addr, unsigned char cycle0)
{
	unsigned long timeout;
	dev_dbg(host->dev,
		"nfc_cmd: 0x%08x, addr1234: 0x%08x, cycle0: 0x%02x\n",
		cmd, addr, cycle0);

	timeout = jiffies + msecs_to_jiffies(NFC_TIME_OUT_MS);
	while (nfc_cmd_readl(NFCADDR_CMD_NFCBUSY, host->nfc->base_cmd_regs)
			& NFCADDR_CMD_NFCBUSY) {
		if (time_after(jiffies, timeout)) {
			dev_err(host->dev,
				"Time out to wait CMD_NFCBUSY ready!\n");
			return -ETIMEDOUT;
		}
	}
	nfc_writel(host->nfc->hsmc_regs, CYCLE0, cycle0);
	nfc_cmd_addr1234_writel(cmd, addr, host->nfc->base_cmd_regs);
	return nfc_wait_interrupt(host, NFC_SR_CMD_DONE);
}

static int nfc_device_ready(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
	if (!nfc_wait_interrupt(host, NFC_SR_RB_EDGE))
		return 1;
	return 0;
}

static void nfc_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

	if (chip == -1)
		nfc_writel(host->nfc->hsmc_regs, CTRL, NFC_CTRL_DISABLE);
	else
		nfc_writel(host->nfc->hsmc_regs, CTRL, NFC_CTRL_ENABLE);
}

static int nfc_make_addr(struct mtd_info *mtd, int column, int page_addr,
		unsigned int *addr1234, unsigned int *cycle0)
{
	struct nand_chip *chip = mtd->priv;

	int acycle = 0;
	unsigned char addr_bytes[8];
	int index = 0, bit_shift;

	BUG_ON(addr1234 == NULL || cycle0 == NULL);

	*cycle0 = 0;
	*addr1234 = 0;

	if (column != -1) {
		if (chip->options & NAND_BUSWIDTH_16)
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
	struct nand_chip *chip = mtd->priv;
	struct atmel_nand_host *host = chip->priv;
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
		acycle = nfc_make_addr(mtd, column, page_addr, &addr1234,
				&cycle0);

	nfc_addr_cmd = cmd1 | cmd2 | vcmd2 | acycle | csid | dataen | nfcwr;
	nfc_send_command(host, nfc_addr_cmd, addr1234, cycle0);

	if (dataen == NFCADDR_CMD_DATAEN)
		if (nfc_wait_interrupt(host, NFC_SR_XFR_DONE))
			dev_err(host->dev, "something wrong, No XFR_DONE interrupt comes.\n");

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
		nfc_wait_interrupt(host, NFC_SR_RB_EDGE);
	}
}

static int nfc_sram_write_page(struct mtd_info *mtd, struct nand_chip *chip,
			uint32_t offset, int data_len, const uint8_t *buf,
			int oob_required, int page, int cached, int raw)
{
	int cfg, len;
	int status = 0;
	struct atmel_nand_host *host = chip->priv;
	void __iomem *sram = host->nfc->sram_bank0 + nfc_get_sram_off(host);

	/* Subpage write is not supported */
	if (offset || (data_len < mtd->writesize))
		return -EINVAL;

	cfg = nfc_readl(host->nfc->hsmc_regs, CFG);
	len = mtd->writesize;

	if (unlikely(raw)) {
		len += mtd->oobsize;
		nfc_writel(host->nfc->hsmc_regs, CFG, cfg | NFC_CFG_WSPARE);
	} else
		nfc_writel(host->nfc->hsmc_regs, CFG, cfg & ~NFC_CFG_WSPARE);

	/* Copy page data to sram that will write to nand via NFC */
	if (use_dma) {
		if (atmel_nand_dma_op(mtd, (void *)buf, len, 0) != 0)
			/* Fall back to use cpu copy */
			memcpy32_toio(sram, buf, len);
	} else {
		memcpy32_toio(sram, buf, len);
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
		status = chip->ecc.write_page(mtd, chip, buf, oob_required);

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
	struct nand_chip *chip = mtd->priv;
	struct atmel_nand_host *host = chip->priv;
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
	struct mtd_part_parser_data ppdata = {};
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
		dev_err(&pdev->dev, "atmel_nand: ioremap resource failed\n");
		res = PTR_ERR(host->io_base);
		goto err_nand_ioremap;
	}
	host->io_phys = (dma_addr_t)mem->start;

	mtd = &host->mtd;
	nand_chip = &host->nand_chip;
	host->dev = &pdev->dev;
	if (IS_ENABLED(CONFIG_OF) && pdev->dev.of_node) {
		/* Only when CONFIG_OF is enabled of_node can be parsed */
		res = atmel_of_init_port(host, pdev->dev.of_node);
		if (res)
			goto err_nand_ioremap;
	} else {
		memcpy(&host->board, dev_get_platdata(&pdev->dev),
		       sizeof(struct atmel_nand_data));
	}

	nand_chip->priv = host;		/* link the private data structures */
	mtd->priv = nand_chip;
	mtd->owner = THIS_MODULE;

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

	nand_chip->ecc.mode = host->board.ecc_mode;
	nand_chip->chip_delay = 20;		/* 20us command delay time */

	if (host->board.bus_width_16)	/* 16-bit bus width */
		nand_chip->options |= NAND_BUSWIDTH_16;

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

	if (host->board.on_flash_bbt || on_flash_bbt) {
		dev_info(&pdev->dev, "Use On Flash BBT\n");
		nand_chip->bbt_options |= NAND_BBT_USE_FLASH;
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
	ppdata.of_node = pdev->dev.of_node;
	res = mtd_device_parse_register(mtd, NULL, &ppdata,
			host->board.parts, host->board.num_parts);
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
	struct mtd_info *mtd = &host->mtd;

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

static const struct of_device_id atmel_nand_dt_ids[] = {
	{ .compatible = "atmel,at91rm9200-nand" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmel_nand_dt_ids);

static int atmel_nand_nfc_probe(struct platform_device *pdev)
{
	struct atmel_nfc *nfc = &nand_nfc;
	struct resource *nfc_cmd_regs, *nfc_hsmc_regs, *nfc_sram;

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
		nfc->sram_bank0 = devm_ioremap_resource(&pdev->dev, nfc_sram);
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

	nfc->is_initialized = true;
	dev_info(&pdev->dev, "NFC is probed.\n");
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
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(atmel_nand_nfc_match),
	},
	.probe = atmel_nand_nfc_probe,
};

static struct platform_driver atmel_nand_driver = {
	.probe		= atmel_nand_probe,
	.remove		= atmel_nand_remove,
	.driver		= {
		.name	= "atmel_nand",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(atmel_nand_dt_ids),
	},
};

module_platform_driver(atmel_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rick Bronson");
MODULE_DESCRIPTION("NAND/SmartMedia driver for AT91 / AVR32");
MODULE_ALIAS("platform:atmel_nand");
