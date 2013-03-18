/*
 * SuperH MSIOF SPI Master Interface
 *
 * Copyright (c) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <linux/spi/sh_msiof.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

#include <asm/unaligned.h>

struct sh_msiof_spi_priv {
	struct spi_bitbang bitbang; /* must be first for spi_bitbang.c */
	void __iomem *mapbase;
	struct clk *clk;
	struct platform_device *pdev;
	struct sh_msiof_spi_info *info;
	struct completion done;
	unsigned long flags;
	int tx_fifo_size;
	int rx_fifo_size;
};

#define TMDR1	0x00
#define TMDR2	0x04
#define TMDR3	0x08
#define RMDR1	0x10
#define RMDR2	0x14
#define RMDR3	0x18
#define TSCR	0x20
#define RSCR	0x22
#define CTR	0x28
#define FCTR	0x30
#define STR	0x40
#define IER	0x44
#define TDR1	0x48
#define TDR2	0x4c
#define TFDR	0x50
#define RDR1	0x58
#define RDR2	0x5c
#define RFDR	0x60

#define CTR_TSCKE (1 << 15)
#define CTR_TFSE  (1 << 14)
#define CTR_TXE   (1 << 9)
#define CTR_RXE   (1 << 8)

#define STR_TEOF  (1 << 23)
#define STR_REOF  (1 << 7)

static u32 sh_msiof_read(struct sh_msiof_spi_priv *p, int reg_offs)
{
	switch (reg_offs) {
	case TSCR:
	case RSCR:
		return ioread16(p->mapbase + reg_offs);
	default:
		return ioread32(p->mapbase + reg_offs);
	}
}

static void sh_msiof_write(struct sh_msiof_spi_priv *p, int reg_offs,
			   u32 value)
{
	switch (reg_offs) {
	case TSCR:
	case RSCR:
		iowrite16(value, p->mapbase + reg_offs);
		break;
	default:
		iowrite32(value, p->mapbase + reg_offs);
		break;
	}
}

static int sh_msiof_modify_ctr_wait(struct sh_msiof_spi_priv *p,
				    u32 clr, u32 set)
{
	u32 mask = clr | set;
	u32 data;
	int k;

	data = sh_msiof_read(p, CTR);
	data &= ~clr;
	data |= set;
	sh_msiof_write(p, CTR, data);

	for (k = 100; k > 0; k--) {
		if ((sh_msiof_read(p, CTR) & mask) == set)
			break;

		udelay(10);
	}

	return k > 0 ? 0 : -ETIMEDOUT;
}

static irqreturn_t sh_msiof_spi_irq(int irq, void *data)
{
	struct sh_msiof_spi_priv *p = data;

	/* just disable the interrupt and wake up */
	sh_msiof_write(p, IER, 0);
	complete(&p->done);

	return IRQ_HANDLED;
}

static struct {
	unsigned short div;
	unsigned short scr;
} const sh_msiof_spi_clk_table[] = {
	{ 1, 0x0007 },
	{ 2, 0x0000 },
	{ 4, 0x0001 },
	{ 8, 0x0002 },
	{ 16, 0x0003 },
	{ 32, 0x0004 },
	{ 64, 0x1f00 },
	{ 128, 0x1f01 },
	{ 256, 0x1f02 },
	{ 512, 0x1f03 },
	{ 1024, 0x1f04 },
};

static void sh_msiof_spi_set_clk_regs(struct sh_msiof_spi_priv *p,
				      unsigned long parent_rate,
				      unsigned long spi_hz)
{
	unsigned long div = 1024;
	size_t k;

	if (!WARN_ON(!spi_hz || !parent_rate))
		div = parent_rate / spi_hz;

	/* TODO: make more fine grained */

	for (k = 0; k < ARRAY_SIZE(sh_msiof_spi_clk_table); k++) {
		if (sh_msiof_spi_clk_table[k].div >= div)
			break;
	}

	k = min_t(int, k, ARRAY_SIZE(sh_msiof_spi_clk_table) - 1);

	sh_msiof_write(p, TSCR, sh_msiof_spi_clk_table[k].scr);
	sh_msiof_write(p, RSCR, sh_msiof_spi_clk_table[k].scr);
}

static void sh_msiof_spi_set_pin_regs(struct sh_msiof_spi_priv *p,
				      u32 cpol, u32 cpha,
				      u32 tx_hi_z, u32 lsb_first)
{
	u32 tmp;
	int edge;

	/*
	 * CPOL CPHA     TSCKIZ RSCKIZ TEDG REDG
	 *    0    0         10     10    1    1
	 *    0    1         10     10    0    0
	 *    1    0         11     11    0    0
	 *    1    1         11     11    1    1
	 */
	sh_msiof_write(p, FCTR, 0);
	sh_msiof_write(p, TMDR1, 0xe2000005 | (lsb_first << 24));
	sh_msiof_write(p, RMDR1, 0x22000005 | (lsb_first << 24));

	tmp = 0xa0000000;
	tmp |= cpol << 30; /* TSCKIZ */
	tmp |= cpol << 28; /* RSCKIZ */

	edge = cpol ^ !cpha;

	tmp |= edge << 27; /* TEDG */
	tmp |= edge << 26; /* REDG */
	tmp |= (tx_hi_z ? 2 : 0) << 22; /* TXDIZ */
	sh_msiof_write(p, CTR, tmp);
}

static void sh_msiof_spi_set_mode_regs(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, void *rx_buf,
				       u32 bits, u32 words)
{
	u32 dr2 = ((bits - 1) << 24) | ((words - 1) << 16);

	if (tx_buf)
		sh_msiof_write(p, TMDR2, dr2);
	else
		sh_msiof_write(p, TMDR2, dr2 | 1);

	if (rx_buf)
		sh_msiof_write(p, RMDR2, dr2);

	sh_msiof_write(p, IER, STR_TEOF | STR_REOF);
}

static void sh_msiof_reset_str(struct sh_msiof_spi_priv *p)
{
	sh_msiof_write(p, STR, sh_msiof_read(p, STR));
}

static void sh_msiof_spi_write_fifo_8(struct sh_msiof_spi_priv *p,
				      const void *tx_buf, int words, int fs)
{
	const u8 *buf_8 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, buf_8[k] << fs);
}

static void sh_msiof_spi_write_fifo_16(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, int words, int fs)
{
	const u16 *buf_16 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, buf_16[k] << fs);
}

static void sh_msiof_spi_write_fifo_16u(struct sh_msiof_spi_priv *p,
					const void *tx_buf, int words, int fs)
{
	const u16 *buf_16 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, get_unaligned(&buf_16[k]) << fs);
}

static void sh_msiof_spi_write_fifo_32(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, int words, int fs)
{
	const u32 *buf_32 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, buf_32[k] << fs);
}

static void sh_msiof_spi_write_fifo_32u(struct sh_msiof_spi_priv *p,
					const void *tx_buf, int words, int fs)
{
	const u32 *buf_32 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, get_unaligned(&buf_32[k]) << fs);
}

static void sh_msiof_spi_write_fifo_s32(struct sh_msiof_spi_priv *p,
					const void *tx_buf, int words, int fs)
{
	const u32 *buf_32 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, swab32(buf_32[k] << fs));
}

static void sh_msiof_spi_write_fifo_s32u(struct sh_msiof_spi_priv *p,
					 const void *tx_buf, int words, int fs)
{
	const u32 *buf_32 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, swab32(get_unaligned(&buf_32[k]) << fs));
}

static void sh_msiof_spi_read_fifo_8(struct sh_msiof_spi_priv *p,
				     void *rx_buf, int words, int fs)
{
	u8 *buf_8 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		buf_8[k] = sh_msiof_read(p, RFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_16(struct sh_msiof_spi_priv *p,
				      void *rx_buf, int words, int fs)
{
	u16 *buf_16 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		buf_16[k] = sh_msiof_read(p, RFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_16u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, int words, int fs)
{
	u16 *buf_16 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		put_unaligned(sh_msiof_read(p, RFDR) >> fs, &buf_16[k]);
}

static void sh_msiof_spi_read_fifo_32(struct sh_msiof_spi_priv *p,
				      void *rx_buf, int words, int fs)
{
	u32 *buf_32 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		buf_32[k] = sh_msiof_read(p, RFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_32u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, int words, int fs)
{
	u32 *buf_32 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		put_unaligned(sh_msiof_read(p, RFDR) >> fs, &buf_32[k]);
}

static void sh_msiof_spi_read_fifo_s32(struct sh_msiof_spi_priv *p,
				       void *rx_buf, int words, int fs)
{
	u32 *buf_32 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		buf_32[k] = swab32(sh_msiof_read(p, RFDR) >> fs);
}

static void sh_msiof_spi_read_fifo_s32u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, int words, int fs)
{
	u32 *buf_32 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		put_unaligned(swab32(sh_msiof_read(p, RFDR) >> fs), &buf_32[k]);
}

static int sh_msiof_spi_bits(struct spi_device *spi, struct spi_transfer *t)
{
	int bits;

	bits = t ? t->bits_per_word : 0;
	if (!bits)
		bits = spi->bits_per_word;
	return bits;
}

static unsigned long sh_msiof_spi_hz(struct spi_device *spi,
				     struct spi_transfer *t)
{
	unsigned long hz;

	hz = t ? t->speed_hz : 0;
	if (!hz)
		hz = spi->max_speed_hz;
	return hz;
}

static int sh_msiof_spi_setup_transfer(struct spi_device *spi,
				       struct spi_transfer *t)
{
	int bits;

	/* noting to check hz values against since parent clock is disabled */

	bits = sh_msiof_spi_bits(spi, t);
	if (bits < 8)
		return -EINVAL;
	if (bits > 32)
		return -EINVAL;

	return spi_bitbang_setup_transfer(spi, t);
}

static void sh_msiof_spi_chipselect(struct spi_device *spi, int is_on)
{
	struct sh_msiof_spi_priv *p = spi_master_get_devdata(spi->master);
	int value;

	/* chip select is active low unless SPI_CS_HIGH is set */
	if (spi->mode & SPI_CS_HIGH)
		value = (is_on == BITBANG_CS_ACTIVE) ? 1 : 0;
	else
		value = (is_on == BITBANG_CS_ACTIVE) ? 0 : 1;

	if (is_on == BITBANG_CS_ACTIVE) {
		if (!test_and_set_bit(0, &p->flags)) {
			pm_runtime_get_sync(&p->pdev->dev);
			clk_enable(p->clk);
		}

		/* Configure pins before asserting CS */
		sh_msiof_spi_set_pin_regs(p, !!(spi->mode & SPI_CPOL),
					  !!(spi->mode & SPI_CPHA),
					  !!(spi->mode & SPI_3WIRE),
					  !!(spi->mode & SPI_LSB_FIRST));
	}

	/* use spi->controller data for CS (same strategy as spi_gpio) */
	gpio_set_value((unsigned)spi->controller_data, value);

	if (is_on == BITBANG_CS_INACTIVE) {
		if (test_and_clear_bit(0, &p->flags)) {
			clk_disable(p->clk);
			pm_runtime_put(&p->pdev->dev);
		}
	}
}

static int sh_msiof_spi_txrx_once(struct sh_msiof_spi_priv *p,
				  void (*tx_fifo)(struct sh_msiof_spi_priv *,
						  const void *, int, int),
				  void (*rx_fifo)(struct sh_msiof_spi_priv *,
						  void *, int, int),
				  const void *tx_buf, void *rx_buf,
				  int words, int bits)
{
	int fifo_shift;
	int ret;

	/* limit maximum word transfer to rx/tx fifo size */
	if (tx_buf)
		words = min_t(int, words, p->tx_fifo_size);
	if (rx_buf)
		words = min_t(int, words, p->rx_fifo_size);

	/* the fifo contents need shifting */
	fifo_shift = 32 - bits;

	/* setup msiof transfer mode registers */
	sh_msiof_spi_set_mode_regs(p, tx_buf, rx_buf, bits, words);

	/* write tx fifo */
	if (tx_buf)
		tx_fifo(p, tx_buf, words, fifo_shift);

	/* setup clock and rx/tx signals */
	ret = sh_msiof_modify_ctr_wait(p, 0, CTR_TSCKE);
	if (rx_buf)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_RXE);
	ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_TXE);

	/* start by setting frame bit */
	INIT_COMPLETION(p->done);
	ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_TFSE);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to start hardware\n");
		goto err;
	}

	/* wait for tx fifo to be emptied / rx fifo to be filled */
	wait_for_completion(&p->done);

	/* read rx fifo */
	if (rx_buf)
		rx_fifo(p, rx_buf, words, fifo_shift);

	/* clear status bits */
	sh_msiof_reset_str(p);

	/* shut down frame, tx/tx and clock signals */
	ret = sh_msiof_modify_ctr_wait(p, CTR_TFSE, 0);
	ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_TXE, 0);
	if (rx_buf)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_RXE, 0);
	ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_TSCKE, 0);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to shut down hardware\n");
		goto err;
	}

	return words;

 err:
	sh_msiof_write(p, IER, 0);
	return ret;
}

static int sh_msiof_spi_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	struct sh_msiof_spi_priv *p = spi_master_get_devdata(spi->master);
	void (*tx_fifo)(struct sh_msiof_spi_priv *, const void *, int, int);
	void (*rx_fifo)(struct sh_msiof_spi_priv *, void *, int, int);
	int bits;
	int bytes_per_word;
	int bytes_done;
	int words;
	int n;
	bool swab;

	bits = sh_msiof_spi_bits(spi, t);

	if (bits <= 8 && t->len > 15 && !(t->len & 3)) {
		bits = 32;
		swab = true;
	} else {
		swab = false;
	}

	/* setup bytes per word and fifo read/write functions */
	if (bits <= 8) {
		bytes_per_word = 1;
		tx_fifo = sh_msiof_spi_write_fifo_8;
		rx_fifo = sh_msiof_spi_read_fifo_8;
	} else if (bits <= 16) {
		bytes_per_word = 2;
		if ((unsigned long)t->tx_buf & 0x01)
			tx_fifo = sh_msiof_spi_write_fifo_16u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_16;

		if ((unsigned long)t->rx_buf & 0x01)
			rx_fifo = sh_msiof_spi_read_fifo_16u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_16;
	} else if (swab) {
		bytes_per_word = 4;
		if ((unsigned long)t->tx_buf & 0x03)
			tx_fifo = sh_msiof_spi_write_fifo_s32u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_s32;

		if ((unsigned long)t->rx_buf & 0x03)
			rx_fifo = sh_msiof_spi_read_fifo_s32u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_s32;
	} else {
		bytes_per_word = 4;
		if ((unsigned long)t->tx_buf & 0x03)
			tx_fifo = sh_msiof_spi_write_fifo_32u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_32;

		if ((unsigned long)t->rx_buf & 0x03)
			rx_fifo = sh_msiof_spi_read_fifo_32u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_32;
	}

	/* setup clocks (clock already enabled in chipselect()) */
	sh_msiof_spi_set_clk_regs(p, clk_get_rate(p->clk),
				  sh_msiof_spi_hz(spi, t));

	/* transfer in fifo sized chunks */
	words = t->len / bytes_per_word;
	bytes_done = 0;

	while (bytes_done < t->len) {
		void *rx_buf = t->rx_buf ? t->rx_buf + bytes_done : NULL;
		const void *tx_buf = t->tx_buf ? t->tx_buf + bytes_done : NULL;
		n = sh_msiof_spi_txrx_once(p, tx_fifo, rx_fifo,
					   tx_buf,
					   rx_buf,
					   words, bits);
		if (n < 0)
			break;

		bytes_done += n * bytes_per_word;
		words -= n;
	}

	return bytes_done;
}

static u32 sh_msiof_spi_txrx_word(struct spi_device *spi, unsigned nsecs,
				  u32 word, u8 bits)
{
	BUG(); /* unused but needed by bitbang code */
	return 0;
}

#ifdef CONFIG_OF
static struct sh_msiof_spi_info *sh_msiof_spi_parse_dt(struct device *dev)
{
	struct sh_msiof_spi_info *info;
	struct device_node *np = dev->of_node;
	u32 num_cs = 0;

	info = devm_kzalloc(dev, sizeof(struct sh_msiof_spi_info), GFP_KERNEL);
	if (!info) {
		dev_err(dev, "failed to allocate setup data\n");
		return NULL;
	}

	/* Parse the MSIOF properties */
	of_property_read_u32(np, "num-cs", &num_cs);
	of_property_read_u32(np, "renesas,tx-fifo-size",
					&info->tx_fifo_override);
	of_property_read_u32(np, "renesas,rx-fifo-size",
					&info->rx_fifo_override);

	info->num_chipselect = num_cs;

	return info;
}
#else
static struct sh_msiof_spi_info *sh_msiof_spi_parse_dt(struct device *dev)
{
	return NULL;
}
#endif

static int sh_msiof_spi_probe(struct platform_device *pdev)
{
	struct resource	*r;
	struct spi_master *master;
	struct sh_msiof_spi_priv *p;
	int i;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof(struct sh_msiof_spi_priv));
	if (master == NULL) {
		dev_err(&pdev->dev, "failed to allocate spi master\n");
		ret = -ENOMEM;
		goto err0;
	}

	p = spi_master_get_devdata(master);

	platform_set_drvdata(pdev, p);
	if (pdev->dev.of_node)
		p->info = sh_msiof_spi_parse_dt(&pdev->dev);
	else
		p->info = pdev->dev.platform_data;

	if (!p->info) {
		dev_err(&pdev->dev, "failed to obtain device info\n");
		ret = -ENXIO;
		goto err1;
	}

	init_completion(&p->done);

	p->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(p->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(p->clk);
		goto err1;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i = platform_get_irq(pdev, 0);
	if (!r || i < 0) {
		dev_err(&pdev->dev, "cannot get platform resources\n");
		ret = -ENOENT;
		goto err2;
	}
	p->mapbase = ioremap_nocache(r->start, resource_size(r));
	if (!p->mapbase) {
		dev_err(&pdev->dev, "unable to ioremap\n");
		ret = -ENXIO;
		goto err2;
	}

	ret = request_irq(i, sh_msiof_spi_irq, 0,
			  dev_name(&pdev->dev), p);
	if (ret) {
		dev_err(&pdev->dev, "unable to request irq\n");
		goto err3;
	}

	p->pdev = pdev;
	pm_runtime_enable(&pdev->dev);

	/* The standard version of MSIOF use 64 word FIFOs */
	p->tx_fifo_size = 64;
	p->rx_fifo_size = 64;

	/* Platform data may override FIFO sizes */
	if (p->info->tx_fifo_override)
		p->tx_fifo_size = p->info->tx_fifo_override;
	if (p->info->rx_fifo_override)
		p->rx_fifo_size = p->info->rx_fifo_override;

	/* init master and bitbang code */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->mode_bits |= SPI_LSB_FIRST | SPI_3WIRE;
	master->flags = 0;
	master->bus_num = pdev->id;
	master->num_chipselect = p->info->num_chipselect;
	master->setup = spi_bitbang_setup;
	master->cleanup = spi_bitbang_cleanup;

	p->bitbang.master = master;
	p->bitbang.chipselect = sh_msiof_spi_chipselect;
	p->bitbang.setup_transfer = sh_msiof_spi_setup_transfer;
	p->bitbang.txrx_bufs = sh_msiof_spi_txrx;
	p->bitbang.txrx_word[SPI_MODE_0] = sh_msiof_spi_txrx_word;
	p->bitbang.txrx_word[SPI_MODE_1] = sh_msiof_spi_txrx_word;
	p->bitbang.txrx_word[SPI_MODE_2] = sh_msiof_spi_txrx_word;
	p->bitbang.txrx_word[SPI_MODE_3] = sh_msiof_spi_txrx_word;

	ret = spi_bitbang_start(&p->bitbang);
	if (ret == 0)
		return 0;

	pm_runtime_disable(&pdev->dev);
 err3:
	iounmap(p->mapbase);
 err2:
	clk_put(p->clk);
 err1:
	spi_master_put(master);
 err0:
	return ret;
}

static int sh_msiof_spi_remove(struct platform_device *pdev)
{
	struct sh_msiof_spi_priv *p = platform_get_drvdata(pdev);
	int ret;

	ret = spi_bitbang_stop(&p->bitbang);
	if (!ret) {
		pm_runtime_disable(&pdev->dev);
		free_irq(platform_get_irq(pdev, 0), p);
		iounmap(p->mapbase);
		clk_put(p->clk);
		spi_master_put(p->bitbang.master);
	}
	return ret;
}

static int sh_msiof_spi_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id sh_msiof_match[] = {
	{ .compatible = "renesas,sh-msiof", },
	{ .compatible = "renesas,sh-mobile-msiof", },
	{},
};
MODULE_DEVICE_TABLE(of, sh_msiof_match);
#else
#define sh_msiof_match NULL
#endif

static struct dev_pm_ops sh_msiof_spi_dev_pm_ops = {
	.runtime_suspend = sh_msiof_spi_runtime_nop,
	.runtime_resume = sh_msiof_spi_runtime_nop,
};

static struct platform_driver sh_msiof_spi_drv = {
	.probe		= sh_msiof_spi_probe,
	.remove		= sh_msiof_spi_remove,
	.driver		= {
		.name		= "spi_sh_msiof",
		.owner		= THIS_MODULE,
		.pm		= &sh_msiof_spi_dev_pm_ops,
		.of_match_table = sh_msiof_match,
	},
};
module_platform_driver(sh_msiof_spi_drv);

MODULE_DESCRIPTION("SuperH MSIOF SPI Master Interface Driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:spi_sh_msiof");
