// SPDX-License-Identifier: GPL-2.0
/*
 * SuperH MSIOF SPI Controller Interface
 *
 * Copyright (c) 2009 Magnus Damm
 * Copyright (C) 2014 Renesas Electronics Corporation
 * Copyright (C) 2014-2017 Glider bvba
 */

#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sh_dma.h>

#include <linux/spi/sh_msiof.h>
#include <linux/spi/spi.h>

#include <linux/unaligned.h>

#define SH_MSIOF_FLAG_FIXED_DTDL_200	BIT(0)

struct sh_msiof_chipdata {
	u32 bits_per_word_mask;
	u16 tx_fifo_size;
	u16 rx_fifo_size;
	u16 ctlr_flags;
	u16 min_div_pow;
	u32 flags;
};

struct sh_msiof_spi_priv {
	struct spi_controller *ctlr;
	void __iomem *mapbase;
	struct clk *clk;
	struct platform_device *pdev;
	struct sh_msiof_spi_info *info;
	struct completion done;
	struct completion done_txdma;
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	unsigned int min_div_pow;
	void *tx_dma_page;
	void *rx_dma_page;
	dma_addr_t tx_dma_addr;
	dma_addr_t rx_dma_addr;
	bool native_cs_inited;
	bool native_cs_high;
	bool target_aborted;
};

#define MAX_SS	3	/* Maximum number of native chip selects */

static u32 sh_msiof_read(struct sh_msiof_spi_priv *p, int reg_offs)
{
	switch (reg_offs) {
	case SITSCR:
	case SIRSCR:
		return ioread16(p->mapbase + reg_offs);
	default:
		return ioread32(p->mapbase + reg_offs);
	}
}

static void sh_msiof_write(struct sh_msiof_spi_priv *p, int reg_offs,
			   u32 value)
{
	switch (reg_offs) {
	case SITSCR:
	case SIRSCR:
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

	data = sh_msiof_read(p, SICTR);
	data &= ~clr;
	data |= set;
	sh_msiof_write(p, SICTR, data);

	return readl_poll_timeout_atomic(p->mapbase + SICTR, data,
					 (data & mask) == set, 1, 100);
}

static irqreturn_t sh_msiof_spi_irq(int irq, void *data)
{
	struct sh_msiof_spi_priv *p = data;

	/* just disable the interrupt and wake up */
	sh_msiof_write(p, SIIER, 0);
	complete(&p->done);

	return IRQ_HANDLED;
}

static void sh_msiof_spi_reset_regs(struct sh_msiof_spi_priv *p)
{
	u32 mask = SICTR_TXRST | SICTR_RXRST;
	u32 data;

	data = sh_msiof_read(p, SICTR);
	data |= mask;
	sh_msiof_write(p, SICTR, data);

	readl_poll_timeout_atomic(p->mapbase + SICTR, data, !(data & mask), 1,
				  100);
}

static void sh_msiof_spi_set_clk_regs(struct sh_msiof_spi_priv *p,
				      struct spi_transfer *t)
{
	unsigned long parent_rate = clk_get_rate(p->clk);
	unsigned int div_pow = p->min_div_pow;
	u32 spi_hz = t->speed_hz;
	unsigned long div;
	u32 brps, scr;

	if (!spi_hz || !parent_rate) {
		WARN(1, "Invalid clock rate parameters %lu and %u\n",
		     parent_rate, spi_hz);
		return;
	}

	div = DIV_ROUND_UP(parent_rate, spi_hz);
	if (div <= 1024) {
		/* SISCR_BRDV_DIV_1 is valid only if BRPS is x 1/1 or x 1/2 */
		if (!div_pow && div <= 32 && div > 2)
			div_pow = 1;

		if (div_pow)
			brps = (div + 1) >> div_pow;
		else
			brps = div;

		for (; brps > 32; div_pow++)
			brps = (brps + 1) >> 1;
	} else {
		/* Set transfer rate composite divisor to 2^5 * 32 = 1024 */
		dev_err(&p->pdev->dev,
			"Requested SPI transfer rate %d is too low\n", spi_hz);
		div_pow = 5;
		brps = 32;
	}

	t->effective_speed_hz = parent_rate / (brps << div_pow);

	/* div_pow == 0 maps to SISCR_BRDV_DIV_1 == all ones */
	scr = FIELD_PREP(SISCR_BRDV, div_pow - 1) |
	      FIELD_PREP(SISCR_BRPS, brps - 1);
	sh_msiof_write(p, SITSCR, scr);
	if (!(p->ctlr->flags & SPI_CONTROLLER_MUST_TX))
		sh_msiof_write(p, SIRSCR, scr);
}

static u32 sh_msiof_get_delay_bit(u32 dtdl_or_syncdl)
{
	/*
	 * DTDL/SYNCDL bit	: p->info->dtdl or p->info->syncdl
	 * b'000		: 0
	 * b'001		: 100
	 * b'010		: 200
	 * b'011 (SYNCDL only)	: 300
	 * b'101		: 50
	 * b'110		: 150
	 */
	if (dtdl_or_syncdl % 100)
		return dtdl_or_syncdl / 100 + 5;
	else
		return dtdl_or_syncdl / 100;
}

static u32 sh_msiof_spi_get_dtdl_and_syncdl(struct sh_msiof_spi_priv *p)
{
	u32 val;

	if (!p->info)
		return 0;

	/* check if DTDL and SYNCDL is allowed value */
	if (p->info->dtdl > 200 || p->info->syncdl > 300) {
		dev_warn(&p->pdev->dev, "DTDL or SYNCDL is too large\n");
		return 0;
	}

	/* check if the sum of DTDL and SYNCDL becomes an integer value  */
	if ((p->info->dtdl + p->info->syncdl) % 100) {
		dev_warn(&p->pdev->dev, "the sum of DTDL/SYNCDL is not good\n");
		return 0;
	}

	val = FIELD_PREP(SIMDR1_DTDL, sh_msiof_get_delay_bit(p->info->dtdl)) |
	      FIELD_PREP(SIMDR1_SYNCDL,
			 sh_msiof_get_delay_bit(p->info->syncdl));

	return val;
}

static void sh_msiof_spi_set_pin_regs(struct sh_msiof_spi_priv *p, u32 ss,
				      bool cpol, bool cpha, bool tx_hi_z,
				      bool lsb_first, bool cs_high)
{
	bool edge;
	u32 tmp;

	/*
	 * CPOL CPHA     TSCKIZ RSCKIZ TEDG REDG
	 *    0    0         10     10    1    1
	 *    0    1         10     10    0    0
	 *    1    0         11     11    0    0
	 *    1    1         11     11    1    1
	 */
	tmp = FIELD_PREP(SIMDR1_SYNCMD, SIMDR1_SYNCMD_SPI) |
	      FIELD_PREP(SIMDR1_FLD, 1) | SIMDR1_XXSTP |
	      FIELD_PREP(SIMDR1_SYNCAC, !cs_high) |
	      FIELD_PREP(SIMDR1_BITLSB, lsb_first);
	tmp |= sh_msiof_spi_get_dtdl_and_syncdl(p);
	if (spi_controller_is_target(p->ctlr)) {
		sh_msiof_write(p, SITMDR1, tmp | SITMDR1_PCON);
	} else {
		sh_msiof_write(p, SITMDR1,
			       tmp | SIMDR1_TRMD | SITMDR1_PCON |
			       FIELD_PREP(SITMDR1_SYNCCH,
					  ss < MAX_SS ? ss : 0));
	}
	if (p->ctlr->flags & SPI_CONTROLLER_MUST_TX) {
		/* These bits are reserved if RX needs TX */
		tmp &= ~0x0000ffff;
	}
	sh_msiof_write(p, SIRMDR1, tmp);

	tmp = 0;
	tmp |= SICTR_TSCKIZ_SCK | FIELD_PREP(SICTR_TSCKIZ_POL, cpol);
	tmp |= SICTR_RSCKIZ_SCK | FIELD_PREP(SICTR_RSCKIZ_POL, cpol);

	edge = cpol ^ !cpha;

	tmp |= FIELD_PREP(SICTR_TEDG, edge);
	tmp |= FIELD_PREP(SICTR_REDG, edge);
	tmp |= FIELD_PREP(SICTR_TXDIZ,
			  tx_hi_z ? SICTR_TXDIZ_HIZ : SICTR_TXDIZ_LOW);
	sh_msiof_write(p, SICTR, tmp);
}

static void sh_msiof_spi_set_mode_regs(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, void *rx_buf,
				       u32 bits, u32 words1, u32 words2)
{
	u32 dr2 = FIELD_PREP(SIMDR2_GRP, words2 ? 1 : 0) |
		  FIELD_PREP(SIMDR2_BITLEN1, bits - 1) |
		  FIELD_PREP(SIMDR2_WDLEN1, words1 - 1);

	if (tx_buf || (p->ctlr->flags & SPI_CONTROLLER_MUST_TX))
		sh_msiof_write(p, SITMDR2, dr2);
	else
		sh_msiof_write(p, SITMDR2, dr2 | SIMDR2_GRPMASK);

	if (rx_buf)
		sh_msiof_write(p, SIRMDR2, dr2);

	if (words2) {
		u32 dr3 = FIELD_PREP(SIMDR3_BITLEN2, bits - 1) |
			  FIELD_PREP(SIMDR3_WDLEN2, words2 - 1);

		sh_msiof_write(p, SITMDR3, dr3);
		if (rx_buf)
			sh_msiof_write(p, SIRMDR3, dr3);
	}
}

static void sh_msiof_reset_str(struct sh_msiof_spi_priv *p)
{
	sh_msiof_write(p, SISTR,
		       sh_msiof_read(p, SISTR) & ~(SISTR_TDREQ | SISTR_RDREQ));
}

static void sh_msiof_spi_write_fifo_8(struct sh_msiof_spi_priv *p,
				      const void *tx_buf, unsigned int words,
				      unsigned int fs)
{
	const u8 *buf_8 = tx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, SITFDR, buf_8[k] << fs);
}

static void sh_msiof_spi_write_fifo_16(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, unsigned int words,
				       unsigned int fs)
{
	const u16 *buf_16 = tx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, SITFDR, buf_16[k] << fs);
}

static void sh_msiof_spi_write_fifo_16u(struct sh_msiof_spi_priv *p,
					const void *tx_buf, unsigned int words,
					unsigned int fs)
{
	const u16 *buf_16 = tx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, SITFDR, get_unaligned(&buf_16[k]) << fs);
}

static void sh_msiof_spi_write_fifo_32(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, unsigned int words,
				       unsigned int fs)
{
	const u32 *buf_32 = tx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, SITFDR, buf_32[k] << fs);
}

static void sh_msiof_spi_write_fifo_32u(struct sh_msiof_spi_priv *p,
					const void *tx_buf, unsigned int words,
					unsigned int fs)
{
	const u32 *buf_32 = tx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, SITFDR, get_unaligned(&buf_32[k]) << fs);
}

static void sh_msiof_spi_write_fifo_s32(struct sh_msiof_spi_priv *p,
					const void *tx_buf, unsigned int words,
					unsigned int fs)
{
	const u32 *buf_32 = tx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, SITFDR, swab32(buf_32[k] << fs));
}

static void sh_msiof_spi_write_fifo_s32u(struct sh_msiof_spi_priv *p,
					 const void *tx_buf,
					 unsigned int words, unsigned int fs)
{
	const u32 *buf_32 = tx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, SITFDR, swab32(get_unaligned(&buf_32[k]) << fs));
}

static void sh_msiof_spi_read_fifo_8(struct sh_msiof_spi_priv *p,
				     void *rx_buf, unsigned int words,
				     unsigned int fs)
{
	u8 *buf_8 = rx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		buf_8[k] = sh_msiof_read(p, SIRFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_16(struct sh_msiof_spi_priv *p,
				      void *rx_buf, unsigned int words,
				      unsigned int fs)
{
	u16 *buf_16 = rx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		buf_16[k] = sh_msiof_read(p, SIRFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_16u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, unsigned int words,
				       unsigned int fs)
{
	u16 *buf_16 = rx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		put_unaligned(sh_msiof_read(p, SIRFDR) >> fs, &buf_16[k]);
}

static void sh_msiof_spi_read_fifo_32(struct sh_msiof_spi_priv *p,
				      void *rx_buf, unsigned int words,
				      unsigned int fs)
{
	u32 *buf_32 = rx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		buf_32[k] = sh_msiof_read(p, SIRFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_32u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, unsigned int words,
				       unsigned int fs)
{
	u32 *buf_32 = rx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		put_unaligned(sh_msiof_read(p, SIRFDR) >> fs, &buf_32[k]);
}

static void sh_msiof_spi_read_fifo_s32(struct sh_msiof_spi_priv *p,
				       void *rx_buf, unsigned int words,
				       unsigned int fs)
{
	u32 *buf_32 = rx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		buf_32[k] = swab32(sh_msiof_read(p, SIRFDR) >> fs);
}

static void sh_msiof_spi_read_fifo_s32u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, unsigned int words,
				       unsigned int fs)
{
	u32 *buf_32 = rx_buf;
	unsigned int k;

	for (k = 0; k < words; k++)
		put_unaligned(swab32(sh_msiof_read(p, SIRFDR) >> fs), &buf_32[k]);
}

static int sh_msiof_spi_setup(struct spi_device *spi)
{
	struct sh_msiof_spi_priv *p =
		spi_controller_get_devdata(spi->controller);
	u32 clr, set, tmp;

	if (spi_get_csgpiod(spi, 0) || spi_controller_is_target(p->ctlr))
		return 0;

	if (p->native_cs_inited &&
	    (p->native_cs_high == !!(spi->mode & SPI_CS_HIGH)))
		return 0;

	/* Configure native chip select mode/polarity early */
	clr = SIMDR1_SYNCMD;
	set = FIELD_PREP(SIMDR1_SYNCMD, SIMDR1_SYNCMD_SPI);
	if (spi->mode & SPI_CS_HIGH)
		clr |= SIMDR1_SYNCAC;
	else
		set |= SIMDR1_SYNCAC;
	pm_runtime_get_sync(&p->pdev->dev);
	tmp = sh_msiof_read(p, SITMDR1) & ~clr;
	sh_msiof_write(p, SITMDR1, tmp | set | SIMDR1_TRMD | SITMDR1_PCON);
	tmp = sh_msiof_read(p, SIRMDR1) & ~clr;
	sh_msiof_write(p, SIRMDR1, tmp | set);
	pm_runtime_put(&p->pdev->dev);
	p->native_cs_high = spi->mode & SPI_CS_HIGH;
	p->native_cs_inited = true;
	return 0;
}

static int sh_msiof_prepare_message(struct spi_controller *ctlr,
				    struct spi_message *msg)
{
	struct sh_msiof_spi_priv *p = spi_controller_get_devdata(ctlr);
	const struct spi_device *spi = msg->spi;
	bool cs_high;
	u32 ss;

	/* Configure pins before asserting CS */
	if (spi_get_csgpiod(spi, 0)) {
		ss = ctlr->unused_native_cs;
		cs_high = p->native_cs_high;
	} else {
		ss = spi_get_chipselect(spi, 0);
		cs_high = spi->mode & SPI_CS_HIGH;
	}
	sh_msiof_spi_set_pin_regs(p, ss, spi->mode & SPI_CPOL,
				  spi->mode & SPI_CPHA, spi->mode & SPI_3WIRE,
				  spi->mode & SPI_LSB_FIRST, cs_high);
	return 0;
}

static int sh_msiof_spi_start(struct sh_msiof_spi_priv *p, void *rx_buf)
{
	bool target = spi_controller_is_target(p->ctlr);
	int ret = 0;

	/* setup clock and rx/tx signals */
	if (!target)
		ret = sh_msiof_modify_ctr_wait(p, 0, SICTR_TSCKE);
	if (rx_buf && !ret)
		ret = sh_msiof_modify_ctr_wait(p, 0, SICTR_RXE);
	if (!ret)
		ret = sh_msiof_modify_ctr_wait(p, 0, SICTR_TXE);

	/* start by setting frame bit */
	if (!ret && !target)
		ret = sh_msiof_modify_ctr_wait(p, 0, SICTR_TFSE);

	return ret;
}

static int sh_msiof_spi_stop(struct sh_msiof_spi_priv *p, void *rx_buf)
{
	bool target = spi_controller_is_target(p->ctlr);
	int ret = 0;

	/* shut down frame, rx/tx and clock signals */
	if (!target)
		ret = sh_msiof_modify_ctr_wait(p, SICTR_TFSE, 0);
	if (!ret)
		ret = sh_msiof_modify_ctr_wait(p, SICTR_TXE, 0);
	if (rx_buf && !ret)
		ret = sh_msiof_modify_ctr_wait(p, SICTR_RXE, 0);
	if (!ret && !target)
		ret = sh_msiof_modify_ctr_wait(p, SICTR_TSCKE, 0);

	return ret;
}

static int sh_msiof_target_abort(struct spi_controller *ctlr)
{
	struct sh_msiof_spi_priv *p = spi_controller_get_devdata(ctlr);

	p->target_aborted = true;
	complete(&p->done);
	complete(&p->done_txdma);
	return 0;
}

static int sh_msiof_wait_for_completion(struct sh_msiof_spi_priv *p,
					struct completion *x)
{
	if (spi_controller_is_target(p->ctlr)) {
		if (wait_for_completion_interruptible(x) ||
		    p->target_aborted) {
			dev_dbg(&p->pdev->dev, "interrupted\n");
			return -EINTR;
		}
	} else {
		if (!wait_for_completion_timeout(x, HZ)) {
			dev_err(&p->pdev->dev, "timeout\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int sh_msiof_spi_txrx_once(struct sh_msiof_spi_priv *p,
				  void (*tx_fifo)(struct sh_msiof_spi_priv *,
						  const void *, unsigned int,
						  unsigned int),
				  void (*rx_fifo)(struct sh_msiof_spi_priv *,
						  void *, unsigned int,
						  unsigned int),
				  const void *tx_buf, void *rx_buf,
				  unsigned int words, unsigned int bits)
{
	unsigned int fifo_shift;
	int ret;

	/* limit maximum word transfer to rx/tx fifo size */
	if (tx_buf)
		words = min(words, p->tx_fifo_size);
	if (rx_buf)
		words = min(words, p->rx_fifo_size);

	/* the fifo contents need shifting */
	fifo_shift = 32 - bits;

	/* default FIFO watermarks for PIO */
	sh_msiof_write(p, SIFCTR, 0);

	/* setup msiof transfer mode registers */
	sh_msiof_spi_set_mode_regs(p, tx_buf, rx_buf, bits, words, 0);
	sh_msiof_write(p, SIIER, SIIER_TEOFE | SIIER_REOFE);

	/* write tx fifo */
	if (tx_buf)
		tx_fifo(p, tx_buf, words, fifo_shift);

	reinit_completion(&p->done);
	p->target_aborted = false;

	ret = sh_msiof_spi_start(p, rx_buf);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to start hardware\n");
		goto stop_ier;
	}

	/* wait for tx fifo to be emptied / rx fifo to be filled */
	ret = sh_msiof_wait_for_completion(p, &p->done);
	if (ret)
		goto stop_reset;

	/* read rx fifo */
	if (rx_buf)
		rx_fifo(p, rx_buf, words, fifo_shift);

	/* clear status bits */
	sh_msiof_reset_str(p);

	ret = sh_msiof_spi_stop(p, rx_buf);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to shut down hardware\n");
		return ret;
	}

	return words;

stop_reset:
	sh_msiof_reset_str(p);
	sh_msiof_spi_stop(p, rx_buf);
stop_ier:
	sh_msiof_write(p, SIIER, 0);
	return ret;
}

static void sh_msiof_dma_complete(void *arg)
{
	complete(arg);
}

static int sh_msiof_dma_once(struct sh_msiof_spi_priv *p, const void *tx,
			     void *rx, unsigned int len,
			     unsigned int max_wdlen)
{
	u32 ier_bits = 0;
	struct dma_async_tx_descriptor *desc_tx = NULL, *desc_rx = NULL;
	unsigned int words1, words2;
	dma_cookie_t cookie;
	int ret;

	/* First prepare and submit the DMA request(s), as this may fail */
	if (rx) {
		ier_bits |= SIIER_RDREQE | SIIER_RDMAE;
		desc_rx = dmaengine_prep_slave_single(p->ctlr->dma_rx,
					p->rx_dma_addr, len, DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!desc_rx)
			return -EAGAIN;

		desc_rx->callback = sh_msiof_dma_complete;
		desc_rx->callback_param = &p->done;
		cookie = dmaengine_submit(desc_rx);
		if (dma_submit_error(cookie))
			return cookie;
	}

	if (tx) {
		ier_bits |= SIIER_TDREQE | SIIER_TDMAE;
		dma_sync_single_for_device(p->ctlr->dma_tx->device->dev,
					   p->tx_dma_addr, len, DMA_TO_DEVICE);
		desc_tx = dmaengine_prep_slave_single(p->ctlr->dma_tx,
					p->tx_dma_addr, len, DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!desc_tx) {
			ret = -EAGAIN;
			goto no_dma_tx;
		}

		desc_tx->callback = sh_msiof_dma_complete;
		desc_tx->callback_param = &p->done_txdma;
		cookie = dmaengine_submit(desc_tx);
		if (dma_submit_error(cookie)) {
			ret = cookie;
			goto no_dma_tx;
		}
	}

	/* 1 stage FIFO watermarks for DMA */
	sh_msiof_write(p, SIFCTR,
		       FIELD_PREP(SIFCTR_TFWM, SIFCTR_TFWM_1) |
		       FIELD_PREP(SIFCTR_RFWM, SIFCTR_RFWM_1));

	/* setup msiof transfer mode registers (32-bit words) */
	words1 = min(len / 4, max_wdlen);
	words2 = len / 4 - words1;
	sh_msiof_spi_set_mode_regs(p, tx, rx, 32, words1, words2);

	sh_msiof_write(p, SIIER, ier_bits);

	reinit_completion(&p->done);
	if (tx)
		reinit_completion(&p->done_txdma);
	p->target_aborted = false;

	/* Now start DMA */
	if (rx)
		dma_async_issue_pending(p->ctlr->dma_rx);
	if (tx)
		dma_async_issue_pending(p->ctlr->dma_tx);

	ret = sh_msiof_spi_start(p, rx);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to start hardware\n");
		goto stop_dma;
	}

	if (tx) {
		/* wait for tx DMA completion */
		ret = sh_msiof_wait_for_completion(p, &p->done_txdma);
		if (ret)
			goto stop_reset;
	}

	if (rx) {
		/* wait for rx DMA completion */
		ret = sh_msiof_wait_for_completion(p, &p->done);
		if (ret)
			goto stop_reset;

		sh_msiof_write(p, SIIER, 0);
	} else {
		/* wait for tx fifo to be emptied */
		sh_msiof_write(p, SIIER, SIIER_TEOFE);
		ret = sh_msiof_wait_for_completion(p, &p->done);
		if (ret)
			goto stop_reset;
	}

	/* clear status bits */
	sh_msiof_reset_str(p);

	ret = sh_msiof_spi_stop(p, rx);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to shut down hardware\n");
		return ret;
	}

	if (rx)
		dma_sync_single_for_cpu(p->ctlr->dma_rx->device->dev,
					p->rx_dma_addr, len, DMA_FROM_DEVICE);

	return 0;

stop_reset:
	sh_msiof_reset_str(p);
	sh_msiof_spi_stop(p, rx);
stop_dma:
	if (tx)
		dmaengine_terminate_sync(p->ctlr->dma_tx);
no_dma_tx:
	if (rx)
		dmaengine_terminate_sync(p->ctlr->dma_rx);
	sh_msiof_write(p, SIIER, 0);
	return ret;
}

static void copy_bswap32(u32 *dst, const u32 *src, unsigned int words)
{
	/* src or dst can be unaligned, but not both */
	if ((unsigned long)src & 3) {
		while (words--) {
			*dst++ = swab32(get_unaligned(src));
			src++;
		}
	} else if ((unsigned long)dst & 3) {
		while (words--) {
			put_unaligned(swab32(*src++), dst);
			dst++;
		}
	} else {
		while (words--)
			*dst++ = swab32(*src++);
	}
}

static void copy_wswap32(u32 *dst, const u32 *src, unsigned int words)
{
	/* src or dst can be unaligned, but not both */
	if ((unsigned long)src & 3) {
		while (words--) {
			*dst++ = swahw32(get_unaligned(src));
			src++;
		}
	} else if ((unsigned long)dst & 3) {
		while (words--) {
			put_unaligned(swahw32(*src++), dst);
			dst++;
		}
	} else {
		while (words--)
			*dst++ = swahw32(*src++);
	}
}

static void copy_plain32(u32 *dst, const u32 *src, unsigned int words)
{
	memcpy(dst, src, words * 4);
}

static int sh_msiof_transfer_one(struct spi_controller *ctlr,
				 struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct sh_msiof_spi_priv *p = spi_controller_get_devdata(ctlr);
	unsigned int max_wdlen = FIELD_MAX(SIMDR2_WDLEN1) + 1;
	void (*copy32)(u32 *, const u32 *, unsigned int);
	void (*tx_fifo)(struct sh_msiof_spi_priv *, const void *, unsigned int,
			unsigned int);
	void (*rx_fifo)(struct sh_msiof_spi_priv *, void *, unsigned int,
			unsigned int);
	const void *tx_buf = t->tx_buf;
	void *rx_buf = t->rx_buf;
	unsigned int len = t->len;
	unsigned int bits = t->bits_per_word;
	unsigned int bytes_per_word;
	unsigned int words;
	int n;
	bool swab;
	int ret;

	/* reset registers */
	sh_msiof_spi_reset_regs(p);

	/* setup clocks (clock already enabled in chipselect()) */
	if (!spi_controller_is_target(p->ctlr))
		sh_msiof_spi_set_clk_regs(p, t);

	if (tx_buf)
		max_wdlen = min(max_wdlen, p->tx_fifo_size);
	if (rx_buf)
		max_wdlen = min(max_wdlen, p->rx_fifo_size);

	while (ctlr->dma_tx && len > 15) {
		/*
		 *  DMA supports 32-bit words only, hence pack 8-bit and 16-bit
		 *  words, with byte resp. word swapping.
		 */
		unsigned int l = min(round_down(len, 4), 2 * max_wdlen * 4);

		if (bits <= 8) {
			copy32 = copy_bswap32;
		} else if (bits <= 16) {
			copy32 = copy_wswap32;
		} else {
			copy32 = copy_plain32;
		}

		if (tx_buf)
			copy32(p->tx_dma_page, tx_buf, l / 4);

		ret = sh_msiof_dma_once(p, tx_buf, rx_buf, l, max_wdlen);
		if (ret == -EAGAIN) {
			dev_warn_once(&p->pdev->dev,
				"DMA not available, falling back to PIO\n");
			break;
		}
		if (ret)
			return ret;

		if (rx_buf) {
			copy32(rx_buf, p->rx_dma_page, l / 4);
			rx_buf += l;
		}
		if (tx_buf)
			tx_buf += l;

		len -= l;
		if (!len)
			return 0;
	}

	if (bits <= 8 && len > 15) {
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
		if ((unsigned long)tx_buf & 0x01)
			tx_fifo = sh_msiof_spi_write_fifo_16u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_16;

		if ((unsigned long)rx_buf & 0x01)
			rx_fifo = sh_msiof_spi_read_fifo_16u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_16;
	} else if (swab) {
		bytes_per_word = 4;
		if ((unsigned long)tx_buf & 0x03)
			tx_fifo = sh_msiof_spi_write_fifo_s32u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_s32;

		if ((unsigned long)rx_buf & 0x03)
			rx_fifo = sh_msiof_spi_read_fifo_s32u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_s32;
	} else {
		bytes_per_word = 4;
		if ((unsigned long)tx_buf & 0x03)
			tx_fifo = sh_msiof_spi_write_fifo_32u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_32;

		if ((unsigned long)rx_buf & 0x03)
			rx_fifo = sh_msiof_spi_read_fifo_32u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_32;
	}

	/* transfer in fifo sized chunks */
	words = len / bytes_per_word;

	while (words > 0) {
		n = sh_msiof_spi_txrx_once(p, tx_fifo, rx_fifo, tx_buf, rx_buf,
					   words, bits);
		if (n < 0)
			return n;

		if (tx_buf)
			tx_buf += n * bytes_per_word;
		if (rx_buf)
			rx_buf += n * bytes_per_word;
		words -= n;

		if (words == 0 && (len % bytes_per_word)) {
			words = len % bytes_per_word;
			bits = t->bits_per_word;
			bytes_per_word = 1;
			tx_fifo = sh_msiof_spi_write_fifo_8;
			rx_fifo = sh_msiof_spi_read_fifo_8;
		}
	}

	return 0;
}

static const struct sh_msiof_chipdata sh_data = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(8, 32),
	.tx_fifo_size = 64,
	.rx_fifo_size = 64,
	.ctlr_flags = 0,
	.min_div_pow = 0,
};

static const struct sh_msiof_chipdata rcar_gen2_data = {
	.bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16) |
			      SPI_BPW_MASK(24) | SPI_BPW_MASK(32),
	.tx_fifo_size = 64,
	.rx_fifo_size = 128,
	.ctlr_flags = SPI_CONTROLLER_MUST_TX,
	.min_div_pow = 0,
};

static const struct sh_msiof_chipdata rcar_gen3_data = {
	.bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16) |
			      SPI_BPW_MASK(24) | SPI_BPW_MASK(32),
	.tx_fifo_size = 64,
	.rx_fifo_size = 256,
	.ctlr_flags = SPI_CONTROLLER_MUST_TX,
	.min_div_pow = 1,
};

static const struct sh_msiof_chipdata rcar_gen4_data = {
	.bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16) |
			      SPI_BPW_MASK(24) | SPI_BPW_MASK(32),
	.tx_fifo_size = 256,
	.rx_fifo_size = 256,
	.ctlr_flags = SPI_CONTROLLER_MUST_TX,
	.min_div_pow = 1,
};

static const struct sh_msiof_chipdata rcar_r8a7795_data = {
	.bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16) |
			      SPI_BPW_MASK(24) | SPI_BPW_MASK(32),
	.tx_fifo_size = 64,
	.rx_fifo_size = 256,
	.ctlr_flags = SPI_CONTROLLER_MUST_TX,
	.min_div_pow = 1,
	.flags = SH_MSIOF_FLAG_FIXED_DTDL_200,
};

static const struct of_device_id sh_msiof_match[] __maybe_unused = {
	{ .compatible = "renesas,sh-mobile-msiof", .data = &sh_data },
	{ .compatible = "renesas,rcar-gen2-msiof", .data = &rcar_gen2_data },
	{ .compatible = "renesas,msiof-r8a7795",   .data = &rcar_r8a7795_data },
	{ .compatible = "renesas,rcar-gen3-msiof", .data = &rcar_gen3_data },
	{ .compatible = "renesas,msiof-r8a779a0",  .data = &rcar_gen3_data },
	{ .compatible = "renesas,msiof-r8a779f0",  .data = &rcar_gen3_data },
	{ .compatible = "renesas,rcar-gen4-msiof", .data = &rcar_gen4_data },
	{ .compatible = "renesas,sh-msiof",        .data = &sh_data }, /* Deprecated */
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sh_msiof_match);

#ifdef CONFIG_OF
static struct sh_msiof_spi_info *sh_msiof_spi_parse_dt(struct device *dev)
{
	struct sh_msiof_spi_info *info;
	struct device_node *np = dev->of_node;
	u32 num_cs = 1;

	info = devm_kzalloc(dev, sizeof(struct sh_msiof_spi_info), GFP_KERNEL);
	if (!info)
		return NULL;

	info->mode = of_property_read_bool(np, "spi-slave") ? MSIOF_SPI_TARGET
							    : MSIOF_SPI_HOST;

	/* Parse the MSIOF properties */
	if (info->mode == MSIOF_SPI_HOST)
		of_property_read_u32(np, "num-cs", &num_cs);
	of_property_read_u32(np, "renesas,tx-fifo-size",
					&info->tx_fifo_override);
	of_property_read_u32(np, "renesas,rx-fifo-size",
					&info->rx_fifo_override);
	of_property_read_u32(np, "renesas,dtdl", &info->dtdl);
	of_property_read_u32(np, "renesas,syncdl", &info->syncdl);

	info->num_chipselect = num_cs;

	return info;
}
#else
static struct sh_msiof_spi_info *sh_msiof_spi_parse_dt(struct device *dev)
{
	return NULL;
}
#endif

static struct dma_chan *sh_msiof_request_dma_chan(struct device *dev,
	enum dma_transfer_direction dir, unsigned int id, dma_addr_t port_addr)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	struct dma_slave_config cfg;
	int ret;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	chan = dma_request_slave_channel_compat(mask, shdma_chan_filter,
				(void *)(unsigned long)id, dev,
				dir == DMA_MEM_TO_DEV ? "tx" : "rx");
	if (!chan) {
		dev_warn(dev, "dma_request_slave_channel_compat failed\n");
		return NULL;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.direction = dir;
	if (dir == DMA_MEM_TO_DEV) {
		cfg.dst_addr = port_addr;
		cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	} else {
		cfg.src_addr = port_addr;
		cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	}

	ret = dmaengine_slave_config(chan, &cfg);
	if (ret) {
		dev_warn(dev, "dmaengine_slave_config failed %d\n", ret);
		dma_release_channel(chan);
		return NULL;
	}

	return chan;
}

static int sh_msiof_request_dma(struct sh_msiof_spi_priv *p)
{
	struct platform_device *pdev = p->pdev;
	struct device *dev = &pdev->dev;
	const struct sh_msiof_spi_info *info = p->info;
	unsigned int dma_tx_id, dma_rx_id;
	const struct resource *res;
	struct spi_controller *ctlr;
	struct device *tx_dev, *rx_dev;

	if (dev->of_node) {
		/* In the OF case we will get the slave IDs from the DT */
		dma_tx_id = 0;
		dma_rx_id = 0;
	} else if (info && info->dma_tx_id && info->dma_rx_id) {
		dma_tx_id = info->dma_tx_id;
		dma_rx_id = info->dma_rx_id;
	} else {
		/* The driver assumes no error */
		return 0;
	}

	/* The DMA engine uses the second register set, if present */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ctlr = p->ctlr;
	ctlr->dma_tx = sh_msiof_request_dma_chan(dev, DMA_MEM_TO_DEV,
						 dma_tx_id, res->start + SITFDR);
	if (!ctlr->dma_tx)
		return -ENODEV;

	ctlr->dma_rx = sh_msiof_request_dma_chan(dev, DMA_DEV_TO_MEM,
						 dma_rx_id, res->start + SIRFDR);
	if (!ctlr->dma_rx)
		goto free_tx_chan;

	p->tx_dma_page = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
	if (!p->tx_dma_page)
		goto free_rx_chan;

	p->rx_dma_page = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
	if (!p->rx_dma_page)
		goto free_tx_page;

	tx_dev = ctlr->dma_tx->device->dev;
	p->tx_dma_addr = dma_map_single(tx_dev, p->tx_dma_page, PAGE_SIZE,
					DMA_TO_DEVICE);
	if (dma_mapping_error(tx_dev, p->tx_dma_addr))
		goto free_rx_page;

	rx_dev = ctlr->dma_rx->device->dev;
	p->rx_dma_addr = dma_map_single(rx_dev, p->rx_dma_page, PAGE_SIZE,
					DMA_FROM_DEVICE);
	if (dma_mapping_error(rx_dev, p->rx_dma_addr))
		goto unmap_tx_page;

	dev_info(dev, "DMA available");
	return 0;

unmap_tx_page:
	dma_unmap_single(tx_dev, p->tx_dma_addr, PAGE_SIZE, DMA_TO_DEVICE);
free_rx_page:
	free_page((unsigned long)p->rx_dma_page);
free_tx_page:
	free_page((unsigned long)p->tx_dma_page);
free_rx_chan:
	dma_release_channel(ctlr->dma_rx);
free_tx_chan:
	dma_release_channel(ctlr->dma_tx);
	ctlr->dma_tx = NULL;
	return -ENODEV;
}

static void sh_msiof_release_dma(struct sh_msiof_spi_priv *p)
{
	struct spi_controller *ctlr = p->ctlr;

	if (!ctlr->dma_tx)
		return;

	dma_unmap_single(ctlr->dma_rx->device->dev, p->rx_dma_addr, PAGE_SIZE,
			 DMA_FROM_DEVICE);
	dma_unmap_single(ctlr->dma_tx->device->dev, p->tx_dma_addr, PAGE_SIZE,
			 DMA_TO_DEVICE);
	free_page((unsigned long)p->rx_dma_page);
	free_page((unsigned long)p->tx_dma_page);
	dma_release_channel(ctlr->dma_rx);
	dma_release_channel(ctlr->dma_tx);
}

static int sh_msiof_spi_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	const struct sh_msiof_chipdata *chipdata;
	struct sh_msiof_spi_info *info;
	struct sh_msiof_spi_priv *p;
	struct device *dev = &pdev->dev;
	unsigned long clksrc;
	int i;
	int ret;

	/* Check whether MSIOF is used as I2S mode or SPI mode by checking "port" node */
	struct device_node *port __free(device_node) = of_graph_get_next_port(dev->of_node, NULL);
	if (port) /* It was MSIOF-I2S */
		return -ENODEV;

	chipdata = of_device_get_match_data(dev);
	if (chipdata) {
		info = sh_msiof_spi_parse_dt(dev);
	} else {
		chipdata = (const void *)pdev->id_entry->driver_data;
		info = dev_get_platdata(dev);
	}

	if (!info) {
		dev_err(dev, "failed to obtain device info\n");
		return -ENXIO;
	}

	if (chipdata->flags & SH_MSIOF_FLAG_FIXED_DTDL_200)
		info->dtdl = 200;

	if (info->mode == MSIOF_SPI_TARGET)
		ctlr = spi_alloc_target(dev, sizeof(struct sh_msiof_spi_priv));
	else
		ctlr = spi_alloc_host(dev, sizeof(struct sh_msiof_spi_priv));
	if (ctlr == NULL)
		return -ENOMEM;

	p = spi_controller_get_devdata(ctlr);

	platform_set_drvdata(pdev, p);
	p->ctlr = ctlr;
	p->info = info;
	p->min_div_pow = chipdata->min_div_pow;

	init_completion(&p->done);
	init_completion(&p->done_txdma);

	p->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(p->clk)) {
		dev_err(dev, "cannot get clock\n");
		ret = PTR_ERR(p->clk);
		goto err1;
	}

	i = platform_get_irq(pdev, 0);
	if (i < 0) {
		ret = i;
		goto err1;
	}

	p->mapbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(p->mapbase)) {
		ret = PTR_ERR(p->mapbase);
		goto err1;
	}

	ret = devm_request_irq(dev, i, sh_msiof_spi_irq, 0, dev_name(dev), p);
	if (ret) {
		dev_err(dev, "unable to request irq\n");
		goto err1;
	}

	p->pdev = pdev;
	pm_runtime_enable(dev);

	/* Platform data may override FIFO sizes */
	p->tx_fifo_size = chipdata->tx_fifo_size;
	p->rx_fifo_size = chipdata->rx_fifo_size;
	if (p->info->tx_fifo_override)
		p->tx_fifo_size = p->info->tx_fifo_override;
	if (p->info->rx_fifo_override)
		p->rx_fifo_size = p->info->rx_fifo_override;

	/* init controller code */
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	ctlr->mode_bits |= SPI_LSB_FIRST | SPI_3WIRE;
	clksrc = clk_get_rate(p->clk);
	ctlr->min_speed_hz = DIV_ROUND_UP(clksrc, 1024);
	ctlr->max_speed_hz = DIV_ROUND_UP(clksrc, 1 << p->min_div_pow);
	ctlr->flags = chipdata->ctlr_flags;
	ctlr->bus_num = pdev->id;
	ctlr->num_chipselect = p->info->num_chipselect;
	ctlr->dev.of_node = dev->of_node;
	ctlr->setup = sh_msiof_spi_setup;
	ctlr->prepare_message = sh_msiof_prepare_message;
	ctlr->target_abort = sh_msiof_target_abort;
	ctlr->bits_per_word_mask = chipdata->bits_per_word_mask;
	ctlr->auto_runtime_pm = true;
	ctlr->transfer_one = sh_msiof_transfer_one;
	ctlr->use_gpio_descriptors = true;
	ctlr->max_native_cs = MAX_SS;

	ret = sh_msiof_request_dma(p);
	if (ret < 0)
		dev_warn(dev, "DMA not available, using PIO\n");

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret < 0) {
		dev_err(dev, "devm_spi_register_controller error.\n");
		goto err2;
	}

	return 0;

 err2:
	sh_msiof_release_dma(p);
	pm_runtime_disable(dev);
 err1:
	spi_controller_put(ctlr);
	return ret;
}

static void sh_msiof_spi_remove(struct platform_device *pdev)
{
	struct sh_msiof_spi_priv *p = platform_get_drvdata(pdev);

	sh_msiof_release_dma(p);
	pm_runtime_disable(&pdev->dev);
}

static const struct platform_device_id spi_driver_ids[] = {
	{ "spi_sh_msiof",	(kernel_ulong_t)&sh_data },
	{},
};
MODULE_DEVICE_TABLE(platform, spi_driver_ids);

static int sh_msiof_spi_suspend(struct device *dev)
{
	struct sh_msiof_spi_priv *p = dev_get_drvdata(dev);

	return spi_controller_suspend(p->ctlr);
}

static int sh_msiof_spi_resume(struct device *dev)
{
	struct sh_msiof_spi_priv *p = dev_get_drvdata(dev);

	return spi_controller_resume(p->ctlr);
}

static DEFINE_SIMPLE_DEV_PM_OPS(sh_msiof_spi_pm_ops, sh_msiof_spi_suspend,
				sh_msiof_spi_resume);

static struct platform_driver sh_msiof_spi_drv = {
	.probe		= sh_msiof_spi_probe,
	.remove		= sh_msiof_spi_remove,
	.id_table	= spi_driver_ids,
	.driver		= {
		.name		= "spi_sh_msiof",
		.pm		= pm_sleep_ptr(&sh_msiof_spi_pm_ops),
		.of_match_table = of_match_ptr(sh_msiof_match),
	},
};
module_platform_driver(sh_msiof_spi_drv);

MODULE_DESCRIPTION("SuperH MSIOF SPI Controller Interface Driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
