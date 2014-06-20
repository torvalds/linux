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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <linux/spi/sh_msiof.h>
#include <linux/spi/spi.h>

#include <asm/unaligned.h>


struct sh_msiof_chipdata {
	u16 tx_fifo_size;
	u16 rx_fifo_size;
	u16 master_flags;
};

struct sh_msiof_spi_priv {
	void __iomem *mapbase;
	struct clk *clk;
	struct platform_device *pdev;
	const struct sh_msiof_chipdata *chipdata;
	struct sh_msiof_spi_info *info;
	struct completion done;
	int tx_fifo_size;
	int rx_fifo_size;
};

#define TMDR1	0x00	/* Transmit Mode Register 1 */
#define TMDR2	0x04	/* Transmit Mode Register 2 */
#define TMDR3	0x08	/* Transmit Mode Register 3 */
#define RMDR1	0x10	/* Receive Mode Register 1 */
#define RMDR2	0x14	/* Receive Mode Register 2 */
#define RMDR3	0x18	/* Receive Mode Register 3 */
#define TSCR	0x20	/* Transmit Clock Select Register */
#define RSCR	0x22	/* Receive Clock Select Register (SH, A1, APE6) */
#define CTR	0x28	/* Control Register */
#define FCTR	0x30	/* FIFO Control Register */
#define STR	0x40	/* Status Register */
#define IER	0x44	/* Interrupt Enable Register */
#define TDR1	0x48	/* Transmit Control Data Register 1 (SH, A1) */
#define TDR2	0x4c	/* Transmit Control Data Register 2 (SH, A1) */
#define TFDR	0x50	/* Transmit FIFO Data Register */
#define RDR1	0x58	/* Receive Control Data Register 1 (SH, A1) */
#define RDR2	0x5c	/* Receive Control Data Register 2 (SH, A1) */
#define RFDR	0x60	/* Receive FIFO Data Register */

/* TMDR1 and RMDR1 */
#define MDR1_TRMD	 0x80000000 /* Transfer Mode (1 = Master mode) */
#define MDR1_SYNCMD_MASK 0x30000000 /* SYNC Mode */
#define MDR1_SYNCMD_SPI	 0x20000000 /*   Level mode/SPI */
#define MDR1_SYNCMD_LR	 0x30000000 /*   L/R mode */
#define MDR1_SYNCAC_SHIFT	 25 /* Sync Polarity (1 = Active-low) */
#define MDR1_BITLSB_SHIFT	 24 /* MSB/LSB First (1 = LSB first) */
#define MDR1_FLD_MASK	 0x000000c0 /* Frame Sync Signal Interval (0-3) */
#define MDR1_FLD_SHIFT		  2
#define MDR1_XXSTP	 0x00000001 /* Transmission/Reception Stop on FIFO */
/* TMDR1 */
#define TMDR1_PCON	 0x40000000 /* Transfer Signal Connection */

/* TMDR2 and RMDR2 */
#define MDR2_BITLEN1(i)	(((i) - 1) << 24) /* Data Size (8-32 bits) */
#define MDR2_WDLEN1(i)	(((i) - 1) << 16) /* Word Count (1-64/256 (SH, A1))) */
#define MDR2_GRPMASK1	0x00000001 /* Group Output Mask 1 (SH, A1) */

/* TSCR and RSCR */
#define SCR_BRPS_MASK	    0x1f00 /* Prescaler Setting (1-32) */
#define SCR_BRPS(i)	(((i) - 1) << 8)
#define SCR_BRDV_MASK	    0x0007 /* Baud Rate Generator's Division Ratio */
#define SCR_BRDV_DIV_2	    0x0000
#define SCR_BRDV_DIV_4	    0x0001
#define SCR_BRDV_DIV_8	    0x0002
#define SCR_BRDV_DIV_16	    0x0003
#define SCR_BRDV_DIV_32	    0x0004
#define SCR_BRDV_DIV_1	    0x0007

/* CTR */
#define CTR_TSCKIZ_MASK	0xc0000000 /* Transmit Clock I/O Polarity Select */
#define CTR_TSCKIZ_SCK	0x80000000 /*   Disable SCK when TX disabled */
#define CTR_TSCKIZ_POL_SHIFT	30 /*   Transmit Clock Polarity */
#define CTR_RSCKIZ_MASK	0x30000000 /* Receive Clock Polarity Select */
#define CTR_RSCKIZ_SCK	0x20000000 /*   Must match CTR_TSCKIZ_SCK */
#define CTR_RSCKIZ_POL_SHIFT	28 /*   Receive Clock Polarity */
#define CTR_TEDG_SHIFT		27 /* Transmit Timing (1 = falling edge) */
#define CTR_REDG_SHIFT		26 /* Receive Timing (1 = falling edge) */
#define CTR_TXDIZ_MASK	0x00c00000 /* Pin Output When TX is Disabled */
#define CTR_TXDIZ_LOW	0x00000000 /*   0 */
#define CTR_TXDIZ_HIGH	0x00400000 /*   1 */
#define CTR_TXDIZ_HIZ	0x00800000 /*   High-impedance */
#define CTR_TSCKE	0x00008000 /* Transmit Serial Clock Output Enable */
#define CTR_TFSE	0x00004000 /* Transmit Frame Sync Signal Output Enable */
#define CTR_TXE		0x00000200 /* Transmit Enable */
#define CTR_RXE		0x00000100 /* Receive Enable */

/* FCTR */
#define FCTR_TFWM_MASK	0xe0000000 /* Transmit FIFO Watermark */
#define FCTR_TFWM_64	0x00000000 /*  Transfer Request when 64 empty stages */
#define FCTR_TFWM_32	0x20000000 /*  Transfer Request when 32 empty stages */
#define FCTR_TFWM_24	0x40000000 /*  Transfer Request when 24 empty stages */
#define FCTR_TFWM_16	0x60000000 /*  Transfer Request when 16 empty stages */
#define FCTR_TFWM_12	0x80000000 /*  Transfer Request when 12 empty stages */
#define FCTR_TFWM_8	0xa0000000 /*  Transfer Request when 8 empty stages */
#define FCTR_TFWM_4	0xc0000000 /*  Transfer Request when 4 empty stages */
#define FCTR_TFWM_1	0xe0000000 /*  Transfer Request when 1 empty stage */
#define FCTR_TFUA_MASK	0x07f00000 /* Transmit FIFO Usable Area */
#define FCTR_TFUA_SHIFT		20
#define FCTR_TFUA(i)	((i) << FCTR_TFUA_SHIFT)
#define FCTR_RFWM_MASK	0x0000e000 /* Receive FIFO Watermark */
#define FCTR_RFWM_1	0x00000000 /*  Transfer Request when 1 valid stages */
#define FCTR_RFWM_4	0x00002000 /*  Transfer Request when 4 valid stages */
#define FCTR_RFWM_8	0x00004000 /*  Transfer Request when 8 valid stages */
#define FCTR_RFWM_16	0x00006000 /*  Transfer Request when 16 valid stages */
#define FCTR_RFWM_32	0x00008000 /*  Transfer Request when 32 valid stages */
#define FCTR_RFWM_64	0x0000a000 /*  Transfer Request when 64 valid stages */
#define FCTR_RFWM_128	0x0000c000 /*  Transfer Request when 128 valid stages */
#define FCTR_RFWM_256	0x0000e000 /*  Transfer Request when 256 valid stages */
#define FCTR_RFUA_MASK	0x00001ff0 /* Receive FIFO Usable Area (0x40 = full) */
#define FCTR_RFUA_SHIFT		 4
#define FCTR_RFUA(i)	((i) << FCTR_RFUA_SHIFT)

/* STR */
#define STR_TFEMP	0x20000000 /* Transmit FIFO Empty */
#define STR_TDREQ	0x10000000 /* Transmit Data Transfer Request */
#define STR_TEOF	0x00800000 /* Frame Transmission End */
#define STR_TFSERR	0x00200000 /* Transmit Frame Synchronization Error */
#define STR_TFOVF	0x00100000 /* Transmit FIFO Overflow */
#define STR_TFUDF	0x00080000 /* Transmit FIFO Underflow */
#define STR_RFFUL	0x00002000 /* Receive FIFO Full */
#define STR_RDREQ	0x00001000 /* Receive Data Transfer Request */
#define STR_REOF	0x00000080 /* Frame Reception End */
#define STR_RFSERR	0x00000020 /* Receive Frame Synchronization Error */
#define STR_RFUDF	0x00000010 /* Receive FIFO Underflow */
#define STR_RFOVF	0x00000008 /* Receive FIFO Overflow */

/* IER */
#define IER_TDMAE	0x80000000 /* Transmit Data DMA Transfer Req. Enable */
#define IER_TFEMPE	0x20000000 /* Transmit FIFO Empty Enable */
#define IER_TDREQE	0x10000000 /* Transmit Data Transfer Request Enable */
#define IER_TEOFE	0x00800000 /* Frame Transmission End Enable */
#define IER_TFSERRE	0x00200000 /* Transmit Frame Sync Error Enable */
#define IER_TFOVFE	0x00100000 /* Transmit FIFO Overflow Enable */
#define IER_TFUDFE	0x00080000 /* Transmit FIFO Underflow Enable */
#define IER_RDMAE	0x00008000 /* Receive Data DMA Transfer Req. Enable */
#define IER_RFFULE	0x00002000 /* Receive FIFO Full Enable */
#define IER_RDREQE	0x00001000 /* Receive Data Transfer Request Enable */
#define IER_REOFE	0x00000080 /* Frame Reception End Enable */
#define IER_RFSERRE	0x00000020 /* Receive Frame Sync Error Enable */
#define IER_RFUDFE	0x00000010 /* Receive FIFO Underflow Enable */
#define IER_RFOVFE	0x00000008 /* Receive FIFO Overflow Enable */


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
	{ 1,	SCR_BRPS( 1) | SCR_BRDV_DIV_1 },
	{ 2,	SCR_BRPS( 1) | SCR_BRDV_DIV_2 },
	{ 4,	SCR_BRPS( 1) | SCR_BRDV_DIV_4 },
	{ 8,	SCR_BRPS( 1) | SCR_BRDV_DIV_8 },
	{ 16,	SCR_BRPS( 1) | SCR_BRDV_DIV_16 },
	{ 32,	SCR_BRPS( 1) | SCR_BRDV_DIV_32 },
	{ 64,	SCR_BRPS(32) | SCR_BRDV_DIV_2 },
	{ 128,	SCR_BRPS(32) | SCR_BRDV_DIV_4 },
	{ 256,	SCR_BRPS(32) | SCR_BRDV_DIV_8 },
	{ 512,	SCR_BRPS(32) | SCR_BRDV_DIV_16 },
	{ 1024,	SCR_BRPS(32) | SCR_BRDV_DIV_32 },
};

static void sh_msiof_spi_set_clk_regs(struct sh_msiof_spi_priv *p,
				      unsigned long parent_rate, u32 spi_hz)
{
	unsigned long div = 1024;
	size_t k;

	if (!WARN_ON(!spi_hz || !parent_rate))
		div = DIV_ROUND_UP(parent_rate, spi_hz);

	/* TODO: make more fine grained */

	for (k = 0; k < ARRAY_SIZE(sh_msiof_spi_clk_table); k++) {
		if (sh_msiof_spi_clk_table[k].div >= div)
			break;
	}

	k = min_t(int, k, ARRAY_SIZE(sh_msiof_spi_clk_table) - 1);

	sh_msiof_write(p, TSCR, sh_msiof_spi_clk_table[k].scr);
	if (!(p->chipdata->master_flags & SPI_MASTER_MUST_TX))
		sh_msiof_write(p, RSCR, sh_msiof_spi_clk_table[k].scr);
}

static void sh_msiof_spi_set_pin_regs(struct sh_msiof_spi_priv *p,
				      u32 cpol, u32 cpha,
				      u32 tx_hi_z, u32 lsb_first, u32 cs_high)
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

	tmp = MDR1_SYNCMD_SPI | 1 << MDR1_FLD_SHIFT | MDR1_XXSTP;
	tmp |= !cs_high << MDR1_SYNCAC_SHIFT;
	tmp |= lsb_first << MDR1_BITLSB_SHIFT;
	sh_msiof_write(p, TMDR1, tmp | MDR1_TRMD | TMDR1_PCON);
	if (p->chipdata->master_flags & SPI_MASTER_MUST_TX) {
		/* These bits are reserved if RX needs TX */
		tmp &= ~0x0000ffff;
	}
	sh_msiof_write(p, RMDR1, tmp);

	tmp = 0;
	tmp |= CTR_TSCKIZ_SCK | cpol << CTR_TSCKIZ_POL_SHIFT;
	tmp |= CTR_RSCKIZ_SCK | cpol << CTR_RSCKIZ_POL_SHIFT;

	edge = cpol ^ !cpha;

	tmp |= edge << CTR_TEDG_SHIFT;
	tmp |= edge << CTR_REDG_SHIFT;
	tmp |= tx_hi_z ? CTR_TXDIZ_HIZ : CTR_TXDIZ_LOW;
	sh_msiof_write(p, CTR, tmp);
}

static void sh_msiof_spi_set_mode_regs(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, void *rx_buf,
				       u32 bits, u32 words)
{
	u32 dr2 = MDR2_BITLEN1(bits) | MDR2_WDLEN1(words);

	if (tx_buf || (p->chipdata->master_flags & SPI_MASTER_MUST_TX))
		sh_msiof_write(p, TMDR2, dr2);
	else
		sh_msiof_write(p, TMDR2, dr2 | MDR2_GRPMASK1);

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

static int sh_msiof_spi_setup(struct spi_device *spi)
{
	struct device_node	*np = spi->master->dev.of_node;
	struct sh_msiof_spi_priv *p = spi_master_get_devdata(spi->master);

	if (!np) {
		/*
		 * Use spi->controller_data for CS (same strategy as spi_gpio),
		 * if any. otherwise let HW control CS
		 */
		spi->cs_gpio = (uintptr_t)spi->controller_data;
	}

	/* Configure pins before deasserting CS */
	sh_msiof_spi_set_pin_regs(p, !!(spi->mode & SPI_CPOL),
				  !!(spi->mode & SPI_CPHA),
				  !!(spi->mode & SPI_3WIRE),
				  !!(spi->mode & SPI_LSB_FIRST),
				  !!(spi->mode & SPI_CS_HIGH));

	if (spi->cs_gpio >= 0)
		gpio_set_value(spi->cs_gpio, !(spi->mode & SPI_CS_HIGH));

	return 0;
}

static int sh_msiof_prepare_message(struct spi_master *master,
				    struct spi_message *msg)
{
	struct sh_msiof_spi_priv *p = spi_master_get_devdata(master);
	const struct spi_device *spi = msg->spi;

	/* Configure pins before asserting CS */
	sh_msiof_spi_set_pin_regs(p, !!(spi->mode & SPI_CPOL),
				  !!(spi->mode & SPI_CPHA),
				  !!(spi->mode & SPI_3WIRE),
				  !!(spi->mode & SPI_LSB_FIRST),
				  !!(spi->mode & SPI_CS_HIGH));
	return 0;
}

static int sh_msiof_spi_start(struct sh_msiof_spi_priv *p, void *rx_buf)
{
	int ret;

	/* setup clock and rx/tx signals */
	ret = sh_msiof_modify_ctr_wait(p, 0, CTR_TSCKE);
	if (rx_buf && !ret)
		ret = sh_msiof_modify_ctr_wait(p, 0, CTR_RXE);
	if (!ret)
		ret = sh_msiof_modify_ctr_wait(p, 0, CTR_TXE);

	/* start by setting frame bit */
	if (!ret)
		ret = sh_msiof_modify_ctr_wait(p, 0, CTR_TFSE);

	return ret;
}

static int sh_msiof_spi_stop(struct sh_msiof_spi_priv *p, void *rx_buf)
{
	int ret;

	/* shut down frame, rx/tx and clock signals */
	ret = sh_msiof_modify_ctr_wait(p, CTR_TFSE, 0);
	if (!ret)
		ret = sh_msiof_modify_ctr_wait(p, CTR_TXE, 0);
	if (rx_buf && !ret)
		ret = sh_msiof_modify_ctr_wait(p, CTR_RXE, 0);
	if (!ret)
		ret = sh_msiof_modify_ctr_wait(p, CTR_TSCKE, 0);

	return ret;
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

	reinit_completion(&p->done);

	ret = sh_msiof_spi_start(p, rx_buf);
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

	ret = sh_msiof_spi_stop(p, rx_buf);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to shut down hardware\n");
		goto err;
	}

	return words;

 err:
	sh_msiof_write(p, IER, 0);
	return ret;
}

static int sh_msiof_transfer_one(struct spi_master *master,
				 struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct sh_msiof_spi_priv *p = spi_master_get_devdata(master);
	void (*tx_fifo)(struct sh_msiof_spi_priv *, const void *, int, int);
	void (*rx_fifo)(struct sh_msiof_spi_priv *, void *, int, int);
	int bits;
	int bytes_per_word;
	int bytes_done;
	int words;
	int n;
	bool swab;

	bits = t->bits_per_word;

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
	sh_msiof_spi_set_clk_regs(p, clk_get_rate(p->clk), t->speed_hz);

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

	return 0;
}

static const struct sh_msiof_chipdata sh_data = {
	.tx_fifo_size = 64,
	.rx_fifo_size = 64,
	.master_flags = 0,
};

static const struct sh_msiof_chipdata r8a779x_data = {
	.tx_fifo_size = 64,
	.rx_fifo_size = 256,
	.master_flags = SPI_MASTER_MUST_TX,
};

static const struct of_device_id sh_msiof_match[] = {
	{ .compatible = "renesas,sh-msiof",        .data = &sh_data },
	{ .compatible = "renesas,sh-mobile-msiof", .data = &sh_data },
	{ .compatible = "renesas,msiof-r8a7790",   .data = &r8a779x_data },
	{ .compatible = "renesas,msiof-r8a7791",   .data = &r8a779x_data },
	{},
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
	const struct of_device_id *of_id;
	struct sh_msiof_spi_priv *p;
	int i;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof(struct sh_msiof_spi_priv));
	if (master == NULL) {
		dev_err(&pdev->dev, "failed to allocate spi master\n");
		return -ENOMEM;
	}

	p = spi_master_get_devdata(master);

	platform_set_drvdata(pdev, p);

	of_id = of_match_device(sh_msiof_match, &pdev->dev);
	if (of_id) {
		p->chipdata = of_id->data;
		p->info = sh_msiof_spi_parse_dt(&pdev->dev);
	} else {
		p->chipdata = (const void *)pdev->id_entry->driver_data;
		p->info = dev_get_platdata(&pdev->dev);
	}

	if (!p->info) {
		dev_err(&pdev->dev, "failed to obtain device info\n");
		ret = -ENXIO;
		goto err1;
	}

	init_completion(&p->done);

	p->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(p->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(p->clk);
		goto err1;
	}

	i = platform_get_irq(pdev, 0);
	if (i < 0) {
		dev_err(&pdev->dev, "cannot get platform IRQ\n");
		ret = -ENOENT;
		goto err1;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	p->mapbase = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(p->mapbase)) {
		ret = PTR_ERR(p->mapbase);
		goto err1;
	}

	ret = devm_request_irq(&pdev->dev, i, sh_msiof_spi_irq, 0,
			       dev_name(&pdev->dev), p);
	if (ret) {
		dev_err(&pdev->dev, "unable to request irq\n");
		goto err1;
	}

	p->pdev = pdev;
	pm_runtime_enable(&pdev->dev);

	/* Platform data may override FIFO sizes */
	p->tx_fifo_size = p->chipdata->tx_fifo_size;
	p->rx_fifo_size = p->chipdata->rx_fifo_size;
	if (p->info->tx_fifo_override)
		p->tx_fifo_size = p->info->tx_fifo_override;
	if (p->info->rx_fifo_override)
		p->rx_fifo_size = p->info->rx_fifo_override;

	/* init master code */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->mode_bits |= SPI_LSB_FIRST | SPI_3WIRE;
	master->flags = p->chipdata->master_flags;
	master->bus_num = pdev->id;
	master->dev.of_node = pdev->dev.of_node;
	master->num_chipselect = p->info->num_chipselect;
	master->setup = sh_msiof_spi_setup;
	master->prepare_message = sh_msiof_prepare_message;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(8, 32);
	master->auto_runtime_pm = true;
	master->transfer_one = sh_msiof_transfer_one;

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_register_master error.\n");
		goto err2;
	}

	return 0;

 err2:
	pm_runtime_disable(&pdev->dev);
 err1:
	spi_master_put(master);
	return ret;
}

static int sh_msiof_spi_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static struct platform_device_id spi_driver_ids[] = {
	{ "spi_sh_msiof",	(kernel_ulong_t)&sh_data },
	{ "spi_r8a7790_msiof",	(kernel_ulong_t)&r8a779x_data },
	{ "spi_r8a7791_msiof",	(kernel_ulong_t)&r8a779x_data },
	{},
};
MODULE_DEVICE_TABLE(platform, spi_driver_ids);

static struct platform_driver sh_msiof_spi_drv = {
	.probe		= sh_msiof_spi_probe,
	.remove		= sh_msiof_spi_remove,
	.id_table	= spi_driver_ids,
	.driver		= {
		.name		= "spi_sh_msiof",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(sh_msiof_match),
	},
};
module_platform_driver(sh_msiof_spi_drv);

MODULE_DESCRIPTION("SuperH MSIOF SPI Master Interface Driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:spi_sh_msiof");
