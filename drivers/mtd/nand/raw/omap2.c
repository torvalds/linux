// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2004 Texas Instruments, Jian Zhang <jzhang@ti.com>
 * Copyright © 2004 Micron Technology Inc.
 * Copyright © 2004 David Brownell
 */

#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand-ecc-sw-bch.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/omap-dma.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/platform_data/elm.h>

#include <linux/omap-gpmc.h>
#include <linux/platform_data/mtd-nand-omap2.h>

#define	DRIVER_NAME	"omap2-nand"
#define	OMAP_NAND_TIMEOUT_MS	5000

#define NAND_Ecc_P1e		(1 << 0)
#define NAND_Ecc_P2e		(1 << 1)
#define NAND_Ecc_P4e		(1 << 2)
#define NAND_Ecc_P8e		(1 << 3)
#define NAND_Ecc_P16e		(1 << 4)
#define NAND_Ecc_P32e		(1 << 5)
#define NAND_Ecc_P64e		(1 << 6)
#define NAND_Ecc_P128e		(1 << 7)
#define NAND_Ecc_P256e		(1 << 8)
#define NAND_Ecc_P512e		(1 << 9)
#define NAND_Ecc_P1024e		(1 << 10)
#define NAND_Ecc_P2048e		(1 << 11)

#define NAND_Ecc_P1o		(1 << 16)
#define NAND_Ecc_P2o		(1 << 17)
#define NAND_Ecc_P4o		(1 << 18)
#define NAND_Ecc_P8o		(1 << 19)
#define NAND_Ecc_P16o		(1 << 20)
#define NAND_Ecc_P32o		(1 << 21)
#define NAND_Ecc_P64o		(1 << 22)
#define NAND_Ecc_P128o		(1 << 23)
#define NAND_Ecc_P256o		(1 << 24)
#define NAND_Ecc_P512o		(1 << 25)
#define NAND_Ecc_P1024o		(1 << 26)
#define NAND_Ecc_P2048o		(1 << 27)

#define TF(value)	(value ? 1 : 0)

#define P2048e(a)	(TF(a & NAND_Ecc_P2048e)	<< 0)
#define P2048o(a)	(TF(a & NAND_Ecc_P2048o)	<< 1)
#define P1e(a)		(TF(a & NAND_Ecc_P1e)		<< 2)
#define P1o(a)		(TF(a & NAND_Ecc_P1o)		<< 3)
#define P2e(a)		(TF(a & NAND_Ecc_P2e)		<< 4)
#define P2o(a)		(TF(a & NAND_Ecc_P2o)		<< 5)
#define P4e(a)		(TF(a & NAND_Ecc_P4e)		<< 6)
#define P4o(a)		(TF(a & NAND_Ecc_P4o)		<< 7)

#define P8e(a)		(TF(a & NAND_Ecc_P8e)		<< 0)
#define P8o(a)		(TF(a & NAND_Ecc_P8o)		<< 1)
#define P16e(a)		(TF(a & NAND_Ecc_P16e)		<< 2)
#define P16o(a)		(TF(a & NAND_Ecc_P16o)		<< 3)
#define P32e(a)		(TF(a & NAND_Ecc_P32e)		<< 4)
#define P32o(a)		(TF(a & NAND_Ecc_P32o)		<< 5)
#define P64e(a)		(TF(a & NAND_Ecc_P64e)		<< 6)
#define P64o(a)		(TF(a & NAND_Ecc_P64o)		<< 7)

#define P128e(a)	(TF(a & NAND_Ecc_P128e)		<< 0)
#define P128o(a)	(TF(a & NAND_Ecc_P128o)		<< 1)
#define P256e(a)	(TF(a & NAND_Ecc_P256e)		<< 2)
#define P256o(a)	(TF(a & NAND_Ecc_P256o)		<< 3)
#define P512e(a)	(TF(a & NAND_Ecc_P512e)		<< 4)
#define P512o(a)	(TF(a & NAND_Ecc_P512o)		<< 5)
#define P1024e(a)	(TF(a & NAND_Ecc_P1024e)	<< 6)
#define P1024o(a)	(TF(a & NAND_Ecc_P1024o)	<< 7)

#define P8e_s(a)	(TF(a & NAND_Ecc_P8e)		<< 0)
#define P8o_s(a)	(TF(a & NAND_Ecc_P8o)		<< 1)
#define P16e_s(a)	(TF(a & NAND_Ecc_P16e)		<< 2)
#define P16o_s(a)	(TF(a & NAND_Ecc_P16o)		<< 3)
#define P1e_s(a)	(TF(a & NAND_Ecc_P1e)		<< 4)
#define P1o_s(a)	(TF(a & NAND_Ecc_P1o)		<< 5)
#define P2e_s(a)	(TF(a & NAND_Ecc_P2e)		<< 6)
#define P2o_s(a)	(TF(a & NAND_Ecc_P2o)		<< 7)

#define P4e_s(a)	(TF(a & NAND_Ecc_P4e)		<< 0)
#define P4o_s(a)	(TF(a & NAND_Ecc_P4o)		<< 1)

#define	PREFETCH_CONFIG1_CS_SHIFT	24
#define	ECC_CONFIG_CS_SHIFT		1
#define	CS_MASK				0x7
#define	ENABLE_PREFETCH			(0x1 << 7)
#define	DMA_MPU_MODE_SHIFT		2
#define	ECCSIZE0_SHIFT			12
#define	ECCSIZE1_SHIFT			22
#define	ECC1RESULTSIZE			0x1
#define	ECCCLEAR			0x100
#define	ECC1				0x1
#define	PREFETCH_FIFOTHRESHOLD_MAX	0x40
#define	PREFETCH_FIFOTHRESHOLD(val)	((val) << 8)
#define	PREFETCH_STATUS_COUNT(val)	(val & 0x00003fff)
#define	PREFETCH_STATUS_FIFO_CNT(val)	((val >> 24) & 0x7F)
#define	STATUS_BUFF_EMPTY		0x00000001

#define SECTOR_BYTES		512
/* 4 bit padding to make byte aligned, 56 = 52 + 4 */
#define BCH4_BIT_PAD		4

/* GPMC ecc engine settings for read */
#define BCH_WRAPMODE_1		1	/* BCH wrap mode 1 */
#define BCH8R_ECC_SIZE0		0x1a	/* ecc_size0 = 26 */
#define BCH8R_ECC_SIZE1		0x2	/* ecc_size1 = 2 */
#define BCH4R_ECC_SIZE0		0xd	/* ecc_size0 = 13 */
#define BCH4R_ECC_SIZE1		0x3	/* ecc_size1 = 3 */

/* GPMC ecc engine settings for write */
#define BCH_WRAPMODE_6		6	/* BCH wrap mode 6 */
#define BCH_ECC_SIZE0		0x0	/* ecc_size0 = 0, no oob protection */
#define BCH_ECC_SIZE1		0x20	/* ecc_size1 = 32 */

#define BBM_LEN			2

static u_char bch16_vector[] = {0xf5, 0x24, 0x1c, 0xd0, 0x61, 0xb3, 0xf1, 0x55,
				0x2e, 0x2c, 0x86, 0xa3, 0xed, 0x36, 0x1b, 0x78,
				0x48, 0x76, 0xa9, 0x3b, 0x97, 0xd1, 0x7a, 0x93,
				0x07, 0x0e};
static u_char bch8_vector[] = {0xf3, 0xdb, 0x14, 0x16, 0x8b, 0xd2, 0xbe, 0xcc,
	0xac, 0x6b, 0xff, 0x99, 0x7b};
static u_char bch4_vector[] = {0x00, 0x6b, 0x31, 0xdd, 0x41, 0xbc, 0x10};

struct omap_nand_info {
	struct nand_chip		nand;
	struct platform_device		*pdev;

	int				gpmc_cs;
	bool				dev_ready;
	enum nand_io			xfer_type;
	int				devsize;
	enum omap_ecc			ecc_opt;
	struct device_node		*elm_of_node;

	unsigned long			phys_base;
	struct completion		comp;
	struct dma_chan			*dma;
	int				gpmc_irq_fifo;
	int				gpmc_irq_count;
	enum {
		OMAP_NAND_IO_READ = 0,	/* read */
		OMAP_NAND_IO_WRITE,	/* write */
	} iomode;
	u_char				*buf;
	int					buf_len;
	/* Interface to GPMC */
	struct gpmc_nand_regs		reg;
	struct gpmc_nand_ops		*ops;
	bool				flash_bbt;
	/* fields specific for BCHx_HW ECC scheme */
	struct device			*elm_dev;
	/* NAND ready gpio */
	struct gpio_desc		*ready_gpiod;
	unsigned int			neccpg;
	unsigned int			nsteps_per_eccpg;
	unsigned int			eccpg_size;
	unsigned int			eccpg_bytes;
};

static inline struct omap_nand_info *mtd_to_omap(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct omap_nand_info, nand);
}

/**
 * omap_prefetch_enable - configures and starts prefetch transfer
 * @cs: cs (chip select) number
 * @fifo_th: fifo threshold to be used for read/ write
 * @dma_mode: dma mode enable (1) or disable (0)
 * @u32_count: number of bytes to be transferred
 * @is_write: prefetch read(0) or write post(1) mode
 * @info: NAND device structure containing platform data
 */
static int omap_prefetch_enable(int cs, int fifo_th, int dma_mode,
	unsigned int u32_count, int is_write, struct omap_nand_info *info)
{
	u32 val;

	if (fifo_th > PREFETCH_FIFOTHRESHOLD_MAX)
		return -1;

	if (readl(info->reg.gpmc_prefetch_control))
		return -EBUSY;

	/* Set the amount of bytes to be prefetched */
	writel(u32_count, info->reg.gpmc_prefetch_config2);

	/* Set dma/mpu mode, the prefetch read / post write and
	 * enable the engine. Set which cs is has requested for.
	 */
	val = ((cs << PREFETCH_CONFIG1_CS_SHIFT) |
		PREFETCH_FIFOTHRESHOLD(fifo_th) | ENABLE_PREFETCH |
		(dma_mode << DMA_MPU_MODE_SHIFT) | (is_write & 0x1));
	writel(val, info->reg.gpmc_prefetch_config1);

	/*  Start the prefetch engine */
	writel(0x1, info->reg.gpmc_prefetch_control);

	return 0;
}

/*
 * omap_prefetch_reset - disables and stops the prefetch engine
 */
static int omap_prefetch_reset(int cs, struct omap_nand_info *info)
{
	u32 config1;

	/* check if the same module/cs is trying to reset */
	config1 = readl(info->reg.gpmc_prefetch_config1);
	if (((config1 >> PREFETCH_CONFIG1_CS_SHIFT) & CS_MASK) != cs)
		return -EINVAL;

	/* Stop the PFPW engine */
	writel(0x0, info->reg.gpmc_prefetch_control);

	/* Reset/disable the PFPW engine */
	writel(0x0, info->reg.gpmc_prefetch_config1);

	return 0;
}

/**
 * omap_hwcontrol - hardware specific access to control-lines
 * @chip: NAND chip object
 * @cmd: command to device
 * @ctrl:
 * NAND_NCE: bit 0 -> don't care
 * NAND_CLE: bit 1 -> Command Latch
 * NAND_ALE: bit 2 -> Address Latch
 *
 * NOTE: boards may use different bits for these!!
 */
static void omap_hwcontrol(struct nand_chip *chip, int cmd, unsigned int ctrl)
{
	struct omap_nand_info *info = mtd_to_omap(nand_to_mtd(chip));

	if (cmd != NAND_CMD_NONE) {
		if (ctrl & NAND_CLE)
			writeb(cmd, info->reg.gpmc_nand_command);

		else if (ctrl & NAND_ALE)
			writeb(cmd, info->reg.gpmc_nand_address);

		else /* NAND_NCE */
			writeb(cmd, info->reg.gpmc_nand_data);
	}
}

/**
 * omap_read_buf8 - read data from NAND controller into buffer
 * @mtd: MTD device structure
 * @buf: buffer to store date
 * @len: number of bytes to read
 */
static void omap_read_buf8(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *nand = mtd_to_nand(mtd);

	ioread8_rep(nand->legacy.IO_ADDR_R, buf, len);
}

/**
 * omap_write_buf8 - write buffer to NAND controller
 * @mtd: MTD device structure
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf8(struct mtd_info *mtd, const u_char *buf, int len)
{
	struct omap_nand_info *info = mtd_to_omap(mtd);
	u_char *p = (u_char *)buf;
	bool status;

	while (len--) {
		iowrite8(*p++, info->nand.legacy.IO_ADDR_W);
		/* wait until buffer is available for write */
		do {
			status = info->ops->nand_writebuffer_empty();
		} while (!status);
	}
}

/**
 * omap_read_buf16 - read data from NAND controller into buffer
 * @mtd: MTD device structure
 * @buf: buffer to store date
 * @len: number of bytes to read
 */
static void omap_read_buf16(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *nand = mtd_to_nand(mtd);

	ioread16_rep(nand->legacy.IO_ADDR_R, buf, len / 2);
}

/**
 * omap_write_buf16 - write buffer to NAND controller
 * @mtd: MTD device structure
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf16(struct mtd_info *mtd, const u_char * buf, int len)
{
	struct omap_nand_info *info = mtd_to_omap(mtd);
	u16 *p = (u16 *) buf;
	bool status;
	/* FIXME try bursts of writesw() or DMA ... */
	len >>= 1;

	while (len--) {
		iowrite16(*p++, info->nand.legacy.IO_ADDR_W);
		/* wait until buffer is available for write */
		do {
			status = info->ops->nand_writebuffer_empty();
		} while (!status);
	}
}

/**
 * omap_read_buf_pref - read data from NAND controller into buffer
 * @chip: NAND chip object
 * @buf: buffer to store date
 * @len: number of bytes to read
 */
static void omap_read_buf_pref(struct nand_chip *chip, u_char *buf, int len)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	uint32_t r_count = 0;
	int ret = 0;
	u32 *p = (u32 *)buf;

	/* take care of subpage reads */
	if (len % 4) {
		if (info->nand.options & NAND_BUSWIDTH_16)
			omap_read_buf16(mtd, buf, len % 4);
		else
			omap_read_buf8(mtd, buf, len % 4);
		p = (u32 *) (buf + len % 4);
		len -= len % 4;
	}

	/* configure and start prefetch transfer */
	ret = omap_prefetch_enable(info->gpmc_cs,
			PREFETCH_FIFOTHRESHOLD_MAX, 0x0, len, 0x0, info);
	if (ret) {
		/* PFPW engine is busy, use cpu copy method */
		if (info->nand.options & NAND_BUSWIDTH_16)
			omap_read_buf16(mtd, (u_char *)p, len);
		else
			omap_read_buf8(mtd, (u_char *)p, len);
	} else {
		do {
			r_count = readl(info->reg.gpmc_prefetch_status);
			r_count = PREFETCH_STATUS_FIFO_CNT(r_count);
			r_count = r_count >> 2;
			ioread32_rep(info->nand.legacy.IO_ADDR_R, p, r_count);
			p += r_count;
			len -= r_count << 2;
		} while (len);
		/* disable and stop the PFPW engine */
		omap_prefetch_reset(info->gpmc_cs, info);
	}
}

/**
 * omap_write_buf_pref - write buffer to NAND controller
 * @chip: NAND chip object
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf_pref(struct nand_chip *chip, const u_char *buf,
				int len)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	uint32_t w_count = 0;
	int i = 0, ret = 0;
	u16 *p = (u16 *)buf;
	unsigned long tim, limit;
	u32 val;

	/* take care of subpage writes */
	if (len % 2 != 0) {
		writeb(*buf, info->nand.legacy.IO_ADDR_W);
		p = (u16 *)(buf + 1);
		len--;
	}

	/*  configure and start prefetch transfer */
	ret = omap_prefetch_enable(info->gpmc_cs,
			PREFETCH_FIFOTHRESHOLD_MAX, 0x0, len, 0x1, info);
	if (ret) {
		/* PFPW engine is busy, use cpu copy method */
		if (info->nand.options & NAND_BUSWIDTH_16)
			omap_write_buf16(mtd, (u_char *)p, len);
		else
			omap_write_buf8(mtd, (u_char *)p, len);
	} else {
		while (len) {
			w_count = readl(info->reg.gpmc_prefetch_status);
			w_count = PREFETCH_STATUS_FIFO_CNT(w_count);
			w_count = w_count >> 1;
			for (i = 0; (i < w_count) && len; i++, len -= 2)
				iowrite16(*p++, info->nand.legacy.IO_ADDR_W);
		}
		/* wait for data to flushed-out before reset the prefetch */
		tim = 0;
		limit = (loops_per_jiffy *
					msecs_to_jiffies(OMAP_NAND_TIMEOUT_MS));
		do {
			cpu_relax();
			val = readl(info->reg.gpmc_prefetch_status);
			val = PREFETCH_STATUS_COUNT(val);
		} while (val && (tim++ < limit));

		/* disable and stop the PFPW engine */
		omap_prefetch_reset(info->gpmc_cs, info);
	}
}

/*
 * omap_nand_dma_callback: callback on the completion of dma transfer
 * @data: pointer to completion data structure
 */
static void omap_nand_dma_callback(void *data)
{
	complete((struct completion *) data);
}

/*
 * omap_nand_dma_transfer: configure and start dma transfer
 * @mtd: MTD device structure
 * @addr: virtual address in RAM of source/destination
 * @len: number of data bytes to be transferred
 * @is_write: flag for read/write operation
 */
static inline int omap_nand_dma_transfer(struct mtd_info *mtd, void *addr,
					unsigned int len, int is_write)
{
	struct omap_nand_info *info = mtd_to_omap(mtd);
	struct dma_async_tx_descriptor *tx;
	enum dma_data_direction dir = is_write ? DMA_TO_DEVICE :
							DMA_FROM_DEVICE;
	struct scatterlist sg;
	unsigned long tim, limit;
	unsigned n;
	int ret;
	u32 val;

	if (!virt_addr_valid(addr))
		goto out_copy;

	sg_init_one(&sg, addr, len);
	n = dma_map_sg(info->dma->device->dev, &sg, 1, dir);
	if (n == 0) {
		dev_err(&info->pdev->dev,
			"Couldn't DMA map a %d byte buffer\n", len);
		goto out_copy;
	}

	tx = dmaengine_prep_slave_sg(info->dma, &sg, n,
		is_write ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM,
		DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx)
		goto out_copy_unmap;

	tx->callback = omap_nand_dma_callback;
	tx->callback_param = &info->comp;
	dmaengine_submit(tx);

	init_completion(&info->comp);

	/* setup and start DMA using dma_addr */
	dma_async_issue_pending(info->dma);

	/*  configure and start prefetch transfer */
	ret = omap_prefetch_enable(info->gpmc_cs,
		PREFETCH_FIFOTHRESHOLD_MAX, 0x1, len, is_write, info);
	if (ret)
		/* PFPW engine is busy, use cpu copy method */
		goto out_copy_unmap;

	wait_for_completion(&info->comp);
	tim = 0;
	limit = (loops_per_jiffy * msecs_to_jiffies(OMAP_NAND_TIMEOUT_MS));

	do {
		cpu_relax();
		val = readl(info->reg.gpmc_prefetch_status);
		val = PREFETCH_STATUS_COUNT(val);
	} while (val && (tim++ < limit));

	/* disable and stop the PFPW engine */
	omap_prefetch_reset(info->gpmc_cs, info);

	dma_unmap_sg(info->dma->device->dev, &sg, 1, dir);
	return 0;

out_copy_unmap:
	dma_unmap_sg(info->dma->device->dev, &sg, 1, dir);
out_copy:
	if (info->nand.options & NAND_BUSWIDTH_16)
		is_write == 0 ? omap_read_buf16(mtd, (u_char *) addr, len)
			: omap_write_buf16(mtd, (u_char *) addr, len);
	else
		is_write == 0 ? omap_read_buf8(mtd, (u_char *) addr, len)
			: omap_write_buf8(mtd, (u_char *) addr, len);
	return 0;
}

/**
 * omap_read_buf_dma_pref - read data from NAND controller into buffer
 * @chip: NAND chip object
 * @buf: buffer to store date
 * @len: number of bytes to read
 */
static void omap_read_buf_dma_pref(struct nand_chip *chip, u_char *buf,
				   int len)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	if (len <= mtd->oobsize)
		omap_read_buf_pref(chip, buf, len);
	else
		/* start transfer in DMA mode */
		omap_nand_dma_transfer(mtd, buf, len, 0x0);
}

/**
 * omap_write_buf_dma_pref - write buffer to NAND controller
 * @chip: NAND chip object
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf_dma_pref(struct nand_chip *chip, const u_char *buf,
				    int len)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	if (len <= mtd->oobsize)
		omap_write_buf_pref(chip, buf, len);
	else
		/* start transfer in DMA mode */
		omap_nand_dma_transfer(mtd, (u_char *)buf, len, 0x1);
}

/*
 * omap_nand_irq - GPMC irq handler
 * @this_irq: gpmc irq number
 * @dev: omap_nand_info structure pointer is passed here
 */
static irqreturn_t omap_nand_irq(int this_irq, void *dev)
{
	struct omap_nand_info *info = (struct omap_nand_info *) dev;
	u32 bytes;

	bytes = readl(info->reg.gpmc_prefetch_status);
	bytes = PREFETCH_STATUS_FIFO_CNT(bytes);
	bytes = bytes  & 0xFFFC; /* io in multiple of 4 bytes */
	if (info->iomode == OMAP_NAND_IO_WRITE) { /* checks for write io */
		if (this_irq == info->gpmc_irq_count)
			goto done;

		if (info->buf_len && (info->buf_len < bytes))
			bytes = info->buf_len;
		else if (!info->buf_len)
			bytes = 0;
		iowrite32_rep(info->nand.legacy.IO_ADDR_W, (u32 *)info->buf,
			      bytes >> 2);
		info->buf = info->buf + bytes;
		info->buf_len -= bytes;

	} else {
		ioread32_rep(info->nand.legacy.IO_ADDR_R, (u32 *)info->buf,
			     bytes >> 2);
		info->buf = info->buf + bytes;

		if (this_irq == info->gpmc_irq_count)
			goto done;
	}

	return IRQ_HANDLED;

done:
	complete(&info->comp);

	disable_irq_nosync(info->gpmc_irq_fifo);
	disable_irq_nosync(info->gpmc_irq_count);

	return IRQ_HANDLED;
}

/*
 * omap_read_buf_irq_pref - read data from NAND controller into buffer
 * @chip: NAND chip object
 * @buf: buffer to store date
 * @len: number of bytes to read
 */
static void omap_read_buf_irq_pref(struct nand_chip *chip, u_char *buf,
				   int len)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	int ret = 0;

	if (len <= mtd->oobsize) {
		omap_read_buf_pref(chip, buf, len);
		return;
	}

	info->iomode = OMAP_NAND_IO_READ;
	info->buf = buf;
	init_completion(&info->comp);

	/*  configure and start prefetch transfer */
	ret = omap_prefetch_enable(info->gpmc_cs,
			PREFETCH_FIFOTHRESHOLD_MAX/2, 0x0, len, 0x0, info);
	if (ret)
		/* PFPW engine is busy, use cpu copy method */
		goto out_copy;

	info->buf_len = len;

	enable_irq(info->gpmc_irq_count);
	enable_irq(info->gpmc_irq_fifo);

	/* waiting for read to complete */
	wait_for_completion(&info->comp);

	/* disable and stop the PFPW engine */
	omap_prefetch_reset(info->gpmc_cs, info);
	return;

out_copy:
	if (info->nand.options & NAND_BUSWIDTH_16)
		omap_read_buf16(mtd, buf, len);
	else
		omap_read_buf8(mtd, buf, len);
}

/*
 * omap_write_buf_irq_pref - write buffer to NAND controller
 * @chip: NAND chip object
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf_irq_pref(struct nand_chip *chip, const u_char *buf,
				    int len)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	int ret = 0;
	unsigned long tim, limit;
	u32 val;

	if (len <= mtd->oobsize) {
		omap_write_buf_pref(chip, buf, len);
		return;
	}

	info->iomode = OMAP_NAND_IO_WRITE;
	info->buf = (u_char *) buf;
	init_completion(&info->comp);

	/* configure and start prefetch transfer : size=24 */
	ret = omap_prefetch_enable(info->gpmc_cs,
		(PREFETCH_FIFOTHRESHOLD_MAX * 3) / 8, 0x0, len, 0x1, info);
	if (ret)
		/* PFPW engine is busy, use cpu copy method */
		goto out_copy;

	info->buf_len = len;

	enable_irq(info->gpmc_irq_count);
	enable_irq(info->gpmc_irq_fifo);

	/* waiting for write to complete */
	wait_for_completion(&info->comp);

	/* wait for data to flushed-out before reset the prefetch */
	tim = 0;
	limit = (loops_per_jiffy *  msecs_to_jiffies(OMAP_NAND_TIMEOUT_MS));
	do {
		val = readl(info->reg.gpmc_prefetch_status);
		val = PREFETCH_STATUS_COUNT(val);
		cpu_relax();
	} while (val && (tim++ < limit));

	/* disable and stop the PFPW engine */
	omap_prefetch_reset(info->gpmc_cs, info);
	return;

out_copy:
	if (info->nand.options & NAND_BUSWIDTH_16)
		omap_write_buf16(mtd, buf, len);
	else
		omap_write_buf8(mtd, buf, len);
}

/**
 * gen_true_ecc - This function will generate true ECC value
 * @ecc_buf: buffer to store ecc code
 *
 * This generated true ECC value can be used when correcting
 * data read from NAND flash memory core
 */
static void gen_true_ecc(u8 *ecc_buf)
{
	u32 tmp = ecc_buf[0] | (ecc_buf[1] << 16) |
		((ecc_buf[2] & 0xF0) << 20) | ((ecc_buf[2] & 0x0F) << 8);

	ecc_buf[0] = ~(P64o(tmp) | P64e(tmp) | P32o(tmp) | P32e(tmp) |
			P16o(tmp) | P16e(tmp) | P8o(tmp) | P8e(tmp));
	ecc_buf[1] = ~(P1024o(tmp) | P1024e(tmp) | P512o(tmp) | P512e(tmp) |
			P256o(tmp) | P256e(tmp) | P128o(tmp) | P128e(tmp));
	ecc_buf[2] = ~(P4o(tmp) | P4e(tmp) | P2o(tmp) | P2e(tmp) | P1o(tmp) |
			P1e(tmp) | P2048o(tmp) | P2048e(tmp));
}

/**
 * omap_compare_ecc - Detect (2 bits) and correct (1 bit) error in data
 * @ecc_data1:  ecc code from nand spare area
 * @ecc_data2:  ecc code from hardware register obtained from hardware ecc
 * @page_data:  page data
 *
 * This function compares two ECC's and indicates if there is an error.
 * If the error can be corrected it will be corrected to the buffer.
 * If there is no error, %0 is returned. If there is an error but it
 * was corrected, %1 is returned. Otherwise, %-1 is returned.
 */
static int omap_compare_ecc(u8 *ecc_data1,	/* read from NAND memory */
			    u8 *ecc_data2,	/* read from register */
			    u8 *page_data)
{
	uint	i;
	u8	tmp0_bit[8], tmp1_bit[8], tmp2_bit[8];
	u8	comp0_bit[8], comp1_bit[8], comp2_bit[8];
	u8	ecc_bit[24];
	u8	ecc_sum = 0;
	u8	find_bit = 0;
	uint	find_byte = 0;
	int	isEccFF;

	isEccFF = ((*(u32 *)ecc_data1 & 0xFFFFFF) == 0xFFFFFF);

	gen_true_ecc(ecc_data1);
	gen_true_ecc(ecc_data2);

	for (i = 0; i <= 2; i++) {
		*(ecc_data1 + i) = ~(*(ecc_data1 + i));
		*(ecc_data2 + i) = ~(*(ecc_data2 + i));
	}

	for (i = 0; i < 8; i++) {
		tmp0_bit[i]     = *ecc_data1 % 2;
		*ecc_data1	= *ecc_data1 / 2;
	}

	for (i = 0; i < 8; i++) {
		tmp1_bit[i]	 = *(ecc_data1 + 1) % 2;
		*(ecc_data1 + 1) = *(ecc_data1 + 1) / 2;
	}

	for (i = 0; i < 8; i++) {
		tmp2_bit[i]	 = *(ecc_data1 + 2) % 2;
		*(ecc_data1 + 2) = *(ecc_data1 + 2) / 2;
	}

	for (i = 0; i < 8; i++) {
		comp0_bit[i]     = *ecc_data2 % 2;
		*ecc_data2       = *ecc_data2 / 2;
	}

	for (i = 0; i < 8; i++) {
		comp1_bit[i]     = *(ecc_data2 + 1) % 2;
		*(ecc_data2 + 1) = *(ecc_data2 + 1) / 2;
	}

	for (i = 0; i < 8; i++) {
		comp2_bit[i]     = *(ecc_data2 + 2) % 2;
		*(ecc_data2 + 2) = *(ecc_data2 + 2) / 2;
	}

	for (i = 0; i < 6; i++)
		ecc_bit[i] = tmp2_bit[i + 2] ^ comp2_bit[i + 2];

	for (i = 0; i < 8; i++)
		ecc_bit[i + 6] = tmp0_bit[i] ^ comp0_bit[i];

	for (i = 0; i < 8; i++)
		ecc_bit[i + 14] = tmp1_bit[i] ^ comp1_bit[i];

	ecc_bit[22] = tmp2_bit[0] ^ comp2_bit[0];
	ecc_bit[23] = tmp2_bit[1] ^ comp2_bit[1];

	for (i = 0; i < 24; i++)
		ecc_sum += ecc_bit[i];

	switch (ecc_sum) {
	case 0:
		/* Not reached because this function is not called if
		 *  ECC values are equal
		 */
		return 0;

	case 1:
		/* Uncorrectable error */
		pr_debug("ECC UNCORRECTED_ERROR 1\n");
		return -EBADMSG;

	case 11:
		/* UN-Correctable error */
		pr_debug("ECC UNCORRECTED_ERROR B\n");
		return -EBADMSG;

	case 12:
		/* Correctable error */
		find_byte = (ecc_bit[23] << 8) +
			    (ecc_bit[21] << 7) +
			    (ecc_bit[19] << 6) +
			    (ecc_bit[17] << 5) +
			    (ecc_bit[15] << 4) +
			    (ecc_bit[13] << 3) +
			    (ecc_bit[11] << 2) +
			    (ecc_bit[9]  << 1) +
			    ecc_bit[7];

		find_bit = (ecc_bit[5] << 2) + (ecc_bit[3] << 1) + ecc_bit[1];

		pr_debug("Correcting single bit ECC error at offset: "
				"%d, bit: %d\n", find_byte, find_bit);

		page_data[find_byte] ^= (1 << find_bit);

		return 1;
	default:
		if (isEccFF) {
			if (ecc_data2[0] == 0 &&
			    ecc_data2[1] == 0 &&
			    ecc_data2[2] == 0)
				return 0;
		}
		pr_debug("UNCORRECTED_ERROR default\n");
		return -EBADMSG;
	}
}

/**
 * omap_correct_data - Compares the ECC read with HW generated ECC
 * @chip: NAND chip object
 * @dat: page data
 * @read_ecc: ecc read from nand flash
 * @calc_ecc: ecc read from HW ECC registers
 *
 * Compares the ecc read from nand spare area with ECC registers values
 * and if ECC's mismatched, it will call 'omap_compare_ecc' for error
 * detection and correction. If there are no errors, %0 is returned. If
 * there were errors and all of the errors were corrected, the number of
 * corrected errors is returned. If uncorrectable errors exist, %-1 is
 * returned.
 */
static int omap_correct_data(struct nand_chip *chip, u_char *dat,
			     u_char *read_ecc, u_char *calc_ecc)
{
	struct omap_nand_info *info = mtd_to_omap(nand_to_mtd(chip));
	int blockCnt = 0, i = 0, ret = 0;
	int stat = 0;

	/* Ex NAND_ECC_HW12_2048 */
	if (info->nand.ecc.engine_type == NAND_ECC_ENGINE_TYPE_ON_HOST &&
	    info->nand.ecc.size == 2048)
		blockCnt = 4;
	else
		blockCnt = 1;

	for (i = 0; i < blockCnt; i++) {
		if (memcmp(read_ecc, calc_ecc, 3) != 0) {
			ret = omap_compare_ecc(read_ecc, calc_ecc, dat);
			if (ret < 0)
				return ret;
			/* keep track of the number of corrected errors */
			stat += ret;
		}
		read_ecc += 3;
		calc_ecc += 3;
		dat      += 512;
	}
	return stat;
}

/**
 * omap_calcuate_ecc - Generate non-inverted ECC bytes.
 * @chip: NAND chip object
 * @dat: The pointer to data on which ecc is computed
 * @ecc_code: The ecc_code buffer
 *
 * Using noninverted ECC can be considered ugly since writing a blank
 * page ie. padding will clear the ECC bytes. This is no problem as long
 * nobody is trying to write data on the seemingly unused page. Reading
 * an erased page will produce an ECC mismatch between generated and read
 * ECC bytes that has to be dealt with separately.
 */
static int omap_calculate_ecc(struct nand_chip *chip, const u_char *dat,
			      u_char *ecc_code)
{
	struct omap_nand_info *info = mtd_to_omap(nand_to_mtd(chip));
	u32 val;

	val = readl(info->reg.gpmc_ecc_config);
	if (((val >> ECC_CONFIG_CS_SHIFT) & CS_MASK) != info->gpmc_cs)
		return -EINVAL;

	/* read ecc result */
	val = readl(info->reg.gpmc_ecc1_result);
	*ecc_code++ = val;          /* P128e, ..., P1e */
	*ecc_code++ = val >> 16;    /* P128o, ..., P1o */
	/* P2048o, P1024o, P512o, P256o, P2048e, P1024e, P512e, P256e */
	*ecc_code++ = ((val >> 8) & 0x0f) | ((val >> 20) & 0xf0);

	return 0;
}

/**
 * omap_enable_hwecc - This function enables the hardware ecc functionality
 * @chip: NAND chip object
 * @mode: Read/Write mode
 */
static void omap_enable_hwecc(struct nand_chip *chip, int mode)
{
	struct omap_nand_info *info = mtd_to_omap(nand_to_mtd(chip));
	unsigned int dev_width = (chip->options & NAND_BUSWIDTH_16) ? 1 : 0;
	u32 val;

	/* clear ecc and enable bits */
	val = ECCCLEAR | ECC1;
	writel(val, info->reg.gpmc_ecc_control);

	/* program ecc and result sizes */
	val = ((((info->nand.ecc.size >> 1) - 1) << ECCSIZE1_SHIFT) |
			 ECC1RESULTSIZE);
	writel(val, info->reg.gpmc_ecc_size_config);

	switch (mode) {
	case NAND_ECC_READ:
	case NAND_ECC_WRITE:
		writel(ECCCLEAR | ECC1, info->reg.gpmc_ecc_control);
		break;
	case NAND_ECC_READSYN:
		writel(ECCCLEAR, info->reg.gpmc_ecc_control);
		break;
	default:
		dev_info(&info->pdev->dev,
			"error: unrecognized Mode[%d]!\n", mode);
		break;
	}

	/* (ECC 16 or 8 bit col) | ( CS  )  | ECC Enable */
	val = (dev_width << 7) | (info->gpmc_cs << 1) | (0x1);
	writel(val, info->reg.gpmc_ecc_config);
}

/**
 * omap_wait - wait until the command is done
 * @this: NAND Chip structure
 *
 * Wait function is called during Program and erase operations and
 * the way it is called from MTD layer, we should wait till the NAND
 * chip is ready after the programming/erase operation has completed.
 *
 * Erase can take up to 400ms and program up to 20ms according to
 * general NAND and SmartMedia specs
 */
static int omap_wait(struct nand_chip *this)
{
	struct omap_nand_info *info = mtd_to_omap(nand_to_mtd(this));
	unsigned long timeo = jiffies;
	int status;

	timeo += msecs_to_jiffies(400);

	writeb(NAND_CMD_STATUS & 0xFF, info->reg.gpmc_nand_command);
	while (time_before(jiffies, timeo)) {
		status = readb(info->reg.gpmc_nand_data);
		if (status & NAND_STATUS_READY)
			break;
		cond_resched();
	}

	status = readb(info->reg.gpmc_nand_data);
	return status;
}

/**
 * omap_dev_ready - checks the NAND Ready GPIO line
 * @chip: NAND chip object
 *
 * Returns true if ready and false if busy.
 */
static int omap_dev_ready(struct nand_chip *chip)
{
	struct omap_nand_info *info = mtd_to_omap(nand_to_mtd(chip));

	return gpiod_get_value(info->ready_gpiod);
}

/**
 * omap_enable_hwecc_bch - Program GPMC to perform BCH ECC calculation
 * @chip: NAND chip object
 * @mode: Read/Write mode
 *
 * When using BCH with SW correction (i.e. no ELM), sector size is set
 * to 512 bytes and we use BCH_WRAPMODE_6 wrapping mode
 * for both reading and writing with:
 * eccsize0 = 0  (no additional protected byte in spare area)
 * eccsize1 = 32 (skip 32 nibbles = 16 bytes per sector in spare area)
 */
static void __maybe_unused omap_enable_hwecc_bch(struct nand_chip *chip,
						 int mode)
{
	unsigned int bch_type;
	unsigned int dev_width, nsectors;
	struct omap_nand_info *info = mtd_to_omap(nand_to_mtd(chip));
	enum omap_ecc ecc_opt = info->ecc_opt;
	u32 val, wr_mode;
	unsigned int ecc_size1, ecc_size0;

	/* GPMC configurations for calculating ECC */
	switch (ecc_opt) {
	case OMAP_ECC_BCH4_CODE_HW_DETECTION_SW:
		bch_type = 0;
		nsectors = 1;
		wr_mode	  = BCH_WRAPMODE_6;
		ecc_size0 = BCH_ECC_SIZE0;
		ecc_size1 = BCH_ECC_SIZE1;
		break;
	case OMAP_ECC_BCH4_CODE_HW:
		bch_type = 0;
		nsectors = chip->ecc.steps;
		if (mode == NAND_ECC_READ) {
			wr_mode	  = BCH_WRAPMODE_1;
			ecc_size0 = BCH4R_ECC_SIZE0;
			ecc_size1 = BCH4R_ECC_SIZE1;
		} else {
			wr_mode   = BCH_WRAPMODE_6;
			ecc_size0 = BCH_ECC_SIZE0;
			ecc_size1 = BCH_ECC_SIZE1;
		}
		break;
	case OMAP_ECC_BCH8_CODE_HW_DETECTION_SW:
		bch_type = 1;
		nsectors = 1;
		wr_mode	  = BCH_WRAPMODE_6;
		ecc_size0 = BCH_ECC_SIZE0;
		ecc_size1 = BCH_ECC_SIZE1;
		break;
	case OMAP_ECC_BCH8_CODE_HW:
		bch_type = 1;
		nsectors = chip->ecc.steps;
		if (mode == NAND_ECC_READ) {
			wr_mode	  = BCH_WRAPMODE_1;
			ecc_size0 = BCH8R_ECC_SIZE0;
			ecc_size1 = BCH8R_ECC_SIZE1;
		} else {
			wr_mode   = BCH_WRAPMODE_6;
			ecc_size0 = BCH_ECC_SIZE0;
			ecc_size1 = BCH_ECC_SIZE1;
		}
		break;
	case OMAP_ECC_BCH16_CODE_HW:
		bch_type = 0x2;
		nsectors = chip->ecc.steps;
		if (mode == NAND_ECC_READ) {
			wr_mode	  = 0x01;
			ecc_size0 = 52; /* ECC bits in nibbles per sector */
			ecc_size1 = 0;  /* non-ECC bits in nibbles per sector */
		} else {
			wr_mode	  = 0x01;
			ecc_size0 = 0;  /* extra bits in nibbles per sector */
			ecc_size1 = 52; /* OOB bits in nibbles per sector */
		}
		break;
	default:
		return;
	}

	writel(ECC1, info->reg.gpmc_ecc_control);

	/* Configure ecc size for BCH */
	val = (ecc_size1 << ECCSIZE1_SHIFT) | (ecc_size0 << ECCSIZE0_SHIFT);
	writel(val, info->reg.gpmc_ecc_size_config);

	dev_width = (chip->options & NAND_BUSWIDTH_16) ? 1 : 0;

	/* BCH configuration */
	val = ((1                        << 16) | /* enable BCH */
	       (bch_type		 << 12) | /* BCH4/BCH8/BCH16 */
	       (wr_mode                  <<  8) | /* wrap mode */
	       (dev_width                <<  7) | /* bus width */
	       (((nsectors-1) & 0x7)     <<  4) | /* number of sectors */
	       (info->gpmc_cs            <<  1) | /* ECC CS */
	       (0x1));                            /* enable ECC */

	writel(val, info->reg.gpmc_ecc_config);

	/* Clear ecc and enable bits */
	writel(ECCCLEAR | ECC1, info->reg.gpmc_ecc_control);
}

static u8  bch4_polynomial[] = {0x28, 0x13, 0xcc, 0x39, 0x96, 0xac, 0x7f};
static u8  bch8_polynomial[] = {0xef, 0x51, 0x2e, 0x09, 0xed, 0x93, 0x9a, 0xc2,
				0x97, 0x79, 0xe5, 0x24, 0xb5};

/**
 * _omap_calculate_ecc_bch - Generate ECC bytes for one sector
 * @mtd:	MTD device structure
 * @dat:	The pointer to data on which ecc is computed
 * @ecc_calc:	The ecc_code buffer
 * @i:		The sector number (for a multi sector page)
 *
 * Support calculating of BCH4/8/16 ECC vectors for one sector
 * within a page. Sector number is in @i.
 */
static int _omap_calculate_ecc_bch(struct mtd_info *mtd,
				   const u_char *dat, u_char *ecc_calc, int i)
{
	struct omap_nand_info *info = mtd_to_omap(mtd);
	int eccbytes	= info->nand.ecc.bytes;
	struct gpmc_nand_regs	*gpmc_regs = &info->reg;
	u8 *ecc_code;
	unsigned long bch_val1, bch_val2, bch_val3, bch_val4;
	u32 val;
	int j;

	ecc_code = ecc_calc;
	switch (info->ecc_opt) {
	case OMAP_ECC_BCH8_CODE_HW_DETECTION_SW:
	case OMAP_ECC_BCH8_CODE_HW:
		bch_val1 = readl(gpmc_regs->gpmc_bch_result0[i]);
		bch_val2 = readl(gpmc_regs->gpmc_bch_result1[i]);
		bch_val3 = readl(gpmc_regs->gpmc_bch_result2[i]);
		bch_val4 = readl(gpmc_regs->gpmc_bch_result3[i]);
		*ecc_code++ = (bch_val4 & 0xFF);
		*ecc_code++ = ((bch_val3 >> 24) & 0xFF);
		*ecc_code++ = ((bch_val3 >> 16) & 0xFF);
		*ecc_code++ = ((bch_val3 >> 8) & 0xFF);
		*ecc_code++ = (bch_val3 & 0xFF);
		*ecc_code++ = ((bch_val2 >> 24) & 0xFF);
		*ecc_code++ = ((bch_val2 >> 16) & 0xFF);
		*ecc_code++ = ((bch_val2 >> 8) & 0xFF);
		*ecc_code++ = (bch_val2 & 0xFF);
		*ecc_code++ = ((bch_val1 >> 24) & 0xFF);
		*ecc_code++ = ((bch_val1 >> 16) & 0xFF);
		*ecc_code++ = ((bch_val1 >> 8) & 0xFF);
		*ecc_code++ = (bch_val1 & 0xFF);
		break;
	case OMAP_ECC_BCH4_CODE_HW_DETECTION_SW:
	case OMAP_ECC_BCH4_CODE_HW:
		bch_val1 = readl(gpmc_regs->gpmc_bch_result0[i]);
		bch_val2 = readl(gpmc_regs->gpmc_bch_result1[i]);
		*ecc_code++ = ((bch_val2 >> 12) & 0xFF);
		*ecc_code++ = ((bch_val2 >> 4) & 0xFF);
		*ecc_code++ = ((bch_val2 & 0xF) << 4) |
			((bch_val1 >> 28) & 0xF);
		*ecc_code++ = ((bch_val1 >> 20) & 0xFF);
		*ecc_code++ = ((bch_val1 >> 12) & 0xFF);
		*ecc_code++ = ((bch_val1 >> 4) & 0xFF);
		*ecc_code++ = ((bch_val1 & 0xF) << 4);
		break;
	case OMAP_ECC_BCH16_CODE_HW:
		val = readl(gpmc_regs->gpmc_bch_result6[i]);
		ecc_code[0]  = ((val >>  8) & 0xFF);
		ecc_code[1]  = ((val >>  0) & 0xFF);
		val = readl(gpmc_regs->gpmc_bch_result5[i]);
		ecc_code[2]  = ((val >> 24) & 0xFF);
		ecc_code[3]  = ((val >> 16) & 0xFF);
		ecc_code[4]  = ((val >>  8) & 0xFF);
		ecc_code[5]  = ((val >>  0) & 0xFF);
		val = readl(gpmc_regs->gpmc_bch_result4[i]);
		ecc_code[6]  = ((val >> 24) & 0xFF);
		ecc_code[7]  = ((val >> 16) & 0xFF);
		ecc_code[8]  = ((val >>  8) & 0xFF);
		ecc_code[9]  = ((val >>  0) & 0xFF);
		val = readl(gpmc_regs->gpmc_bch_result3[i]);
		ecc_code[10] = ((val >> 24) & 0xFF);
		ecc_code[11] = ((val >> 16) & 0xFF);
		ecc_code[12] = ((val >>  8) & 0xFF);
		ecc_code[13] = ((val >>  0) & 0xFF);
		val = readl(gpmc_regs->gpmc_bch_result2[i]);
		ecc_code[14] = ((val >> 24) & 0xFF);
		ecc_code[15] = ((val >> 16) & 0xFF);
		ecc_code[16] = ((val >>  8) & 0xFF);
		ecc_code[17] = ((val >>  0) & 0xFF);
		val = readl(gpmc_regs->gpmc_bch_result1[i]);
		ecc_code[18] = ((val >> 24) & 0xFF);
		ecc_code[19] = ((val >> 16) & 0xFF);
		ecc_code[20] = ((val >>  8) & 0xFF);
		ecc_code[21] = ((val >>  0) & 0xFF);
		val = readl(gpmc_regs->gpmc_bch_result0[i]);
		ecc_code[22] = ((val >> 24) & 0xFF);
		ecc_code[23] = ((val >> 16) & 0xFF);
		ecc_code[24] = ((val >>  8) & 0xFF);
		ecc_code[25] = ((val >>  0) & 0xFF);
		break;
	default:
		return -EINVAL;
	}

	/* ECC scheme specific syndrome customizations */
	switch (info->ecc_opt) {
	case OMAP_ECC_BCH4_CODE_HW_DETECTION_SW:
		/* Add constant polynomial to remainder, so that
		 * ECC of blank pages results in 0x0 on reading back
		 */
		for (j = 0; j < eccbytes; j++)
			ecc_calc[j] ^= bch4_polynomial[j];
		break;
	case OMAP_ECC_BCH4_CODE_HW:
		/* Set  8th ECC byte as 0x0 for ROM compatibility */
		ecc_calc[eccbytes - 1] = 0x0;
		break;
	case OMAP_ECC_BCH8_CODE_HW_DETECTION_SW:
		/* Add constant polynomial to remainder, so that
		 * ECC of blank pages results in 0x0 on reading back
		 */
		for (j = 0; j < eccbytes; j++)
			ecc_calc[j] ^= bch8_polynomial[j];
		break;
	case OMAP_ECC_BCH8_CODE_HW:
		/* Set 14th ECC byte as 0x0 for ROM compatibility */
		ecc_calc[eccbytes - 1] = 0x0;
		break;
	case OMAP_ECC_BCH16_CODE_HW:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * omap_calculate_ecc_bch_sw - ECC generator for sector for SW based correction
 * @chip:	NAND chip object
 * @dat:	The pointer to data on which ecc is computed
 * @ecc_calc:	Buffer storing the calculated ECC bytes
 *
 * Support calculating of BCH4/8/16 ECC vectors for one sector. This is used
 * when SW based correction is required as ECC is required for one sector
 * at a time.
 */
static int omap_calculate_ecc_bch_sw(struct nand_chip *chip,
				     const u_char *dat, u_char *ecc_calc)
{
	return _omap_calculate_ecc_bch(nand_to_mtd(chip), dat, ecc_calc, 0);
}

/**
 * omap_calculate_ecc_bch_multi - Generate ECC for multiple sectors
 * @mtd:	MTD device structure
 * @dat:	The pointer to data on which ecc is computed
 * @ecc_calc:	Buffer storing the calculated ECC bytes
 *
 * Support calculating of BCH4/8/16 ecc vectors for the entire page in one go.
 */
static int omap_calculate_ecc_bch_multi(struct mtd_info *mtd,
					const u_char *dat, u_char *ecc_calc)
{
	struct omap_nand_info *info = mtd_to_omap(mtd);
	int eccbytes = info->nand.ecc.bytes;
	unsigned long nsectors;
	int i, ret;

	nsectors = ((readl(info->reg.gpmc_ecc_config) >> 4) & 0x7) + 1;
	for (i = 0; i < nsectors; i++) {
		ret = _omap_calculate_ecc_bch(mtd, dat, ecc_calc, i);
		if (ret)
			return ret;

		ecc_calc += eccbytes;
	}

	return 0;
}

/**
 * erased_sector_bitflips - count bit flips
 * @data:	data sector buffer
 * @oob:	oob buffer
 * @info:	omap_nand_info
 *
 * Check the bit flips in erased page falls below correctable level.
 * If falls below, report the page as erased with correctable bit
 * flip, else report as uncorrectable page.
 */
static int erased_sector_bitflips(u_char *data, u_char *oob,
		struct omap_nand_info *info)
{
	int flip_bits = 0, i;

	for (i = 0; i < info->nand.ecc.size; i++) {
		flip_bits += hweight8(~data[i]);
		if (flip_bits > info->nand.ecc.strength)
			return 0;
	}

	for (i = 0; i < info->nand.ecc.bytes - 1; i++) {
		flip_bits += hweight8(~oob[i]);
		if (flip_bits > info->nand.ecc.strength)
			return 0;
	}

	/*
	 * Bit flips falls in correctable level.
	 * Fill data area with 0xFF
	 */
	if (flip_bits) {
		memset(data, 0xFF, info->nand.ecc.size);
		memset(oob, 0xFF, info->nand.ecc.bytes);
	}

	return flip_bits;
}

/**
 * omap_elm_correct_data - corrects page data area in case error reported
 * @chip:	NAND chip object
 * @data:	page data
 * @read_ecc:	ecc read from nand flash
 * @calc_ecc:	ecc read from HW ECC registers
 *
 * Calculated ecc vector reported as zero in case of non-error pages.
 * In case of non-zero ecc vector, first filter out erased-pages, and
 * then process data via ELM to detect bit-flips.
 */
static int omap_elm_correct_data(struct nand_chip *chip, u_char *data,
				 u_char *read_ecc, u_char *calc_ecc)
{
	struct omap_nand_info *info = mtd_to_omap(nand_to_mtd(chip));
	struct nand_ecc_ctrl *ecc = &info->nand.ecc;
	int eccsteps = info->nsteps_per_eccpg;
	int i , j, stat = 0;
	int eccflag, actual_eccbytes;
	struct elm_errorvec err_vec[ERROR_VECTOR_MAX];
	u_char *ecc_vec = calc_ecc;
	u_char *spare_ecc = read_ecc;
	u_char *erased_ecc_vec;
	u_char *buf;
	int bitflip_count;
	bool is_error_reported = false;
	u32 bit_pos, byte_pos, error_max, pos;
	int err;

	switch (info->ecc_opt) {
	case OMAP_ECC_BCH4_CODE_HW:
		/* omit  7th ECC byte reserved for ROM code compatibility */
		actual_eccbytes = ecc->bytes - 1;
		erased_ecc_vec = bch4_vector;
		break;
	case OMAP_ECC_BCH8_CODE_HW:
		/* omit 14th ECC byte reserved for ROM code compatibility */
		actual_eccbytes = ecc->bytes - 1;
		erased_ecc_vec = bch8_vector;
		break;
	case OMAP_ECC_BCH16_CODE_HW:
		actual_eccbytes = ecc->bytes;
		erased_ecc_vec = bch16_vector;
		break;
	default:
		dev_err(&info->pdev->dev, "invalid driver configuration\n");
		return -EINVAL;
	}

	/* Initialize elm error vector to zero */
	memset(err_vec, 0, sizeof(err_vec));

	for (i = 0; i < eccsteps ; i++) {
		eccflag = 0;	/* initialize eccflag */

		/*
		 * Check any error reported,
		 * In case of error, non zero ecc reported.
		 */
		for (j = 0; j < actual_eccbytes; j++) {
			if (calc_ecc[j] != 0) {
				eccflag = 1; /* non zero ecc, error present */
				break;
			}
		}

		if (eccflag == 1) {
			if (memcmp(calc_ecc, erased_ecc_vec,
						actual_eccbytes) == 0) {
				/*
				 * calc_ecc[] matches pattern for ECC(all 0xff)
				 * so this is definitely an erased-page
				 */
			} else {
				buf = &data[info->nand.ecc.size * i];
				/*
				 * count number of 0-bits in read_buf.
				 * This check can be removed once a similar
				 * check is introduced in generic NAND driver
				 */
				bitflip_count = erased_sector_bitflips(
						buf, read_ecc, info);
				if (bitflip_count) {
					/*
					 * number of 0-bits within ECC limits
					 * So this may be an erased-page
					 */
					stat += bitflip_count;
				} else {
					/*
					 * Too many 0-bits. It may be a
					 * - programmed-page, OR
					 * - erased-page with many bit-flips
					 * So this page requires check by ELM
					 */
					err_vec[i].error_reported = true;
					is_error_reported = true;
				}
			}
		}

		/* Update the ecc vector */
		calc_ecc += ecc->bytes;
		read_ecc += ecc->bytes;
	}

	/* Check if any error reported */
	if (!is_error_reported)
		return stat;

	/* Decode BCH error using ELM module */
	elm_decode_bch_error_page(info->elm_dev, ecc_vec, err_vec);

	err = 0;
	for (i = 0; i < eccsteps; i++) {
		if (err_vec[i].error_uncorrectable) {
			dev_err(&info->pdev->dev,
				"uncorrectable bit-flips found\n");
			err = -EBADMSG;
		} else if (err_vec[i].error_reported) {
			for (j = 0; j < err_vec[i].error_count; j++) {
				switch (info->ecc_opt) {
				case OMAP_ECC_BCH4_CODE_HW:
					/* Add 4 bits to take care of padding */
					pos = err_vec[i].error_loc[j] +
						BCH4_BIT_PAD;
					break;
				case OMAP_ECC_BCH8_CODE_HW:
				case OMAP_ECC_BCH16_CODE_HW:
					pos = err_vec[i].error_loc[j];
					break;
				default:
					return -EINVAL;
				}
				error_max = (ecc->size + actual_eccbytes) * 8;
				/* Calculate bit position of error */
				bit_pos = pos % 8;

				/* Calculate byte position of error */
				byte_pos = (error_max - pos - 1) / 8;

				if (pos < error_max) {
					if (byte_pos < 512) {
						pr_debug("bitflip@dat[%d]=%x\n",
						     byte_pos, data[byte_pos]);
						data[byte_pos] ^= 1 << bit_pos;
					} else {
						pr_debug("bitflip@oob[%d]=%x\n",
							(byte_pos - 512),
						     spare_ecc[byte_pos - 512]);
						spare_ecc[byte_pos - 512] ^=
							1 << bit_pos;
					}
				} else {
					dev_err(&info->pdev->dev,
						"invalid bit-flip @ %d:%d\n",
						byte_pos, bit_pos);
					err = -EBADMSG;
				}
			}
		}

		/* Update number of correctable errors */
		stat = max_t(unsigned int, stat, err_vec[i].error_count);

		/* Update page data with sector size */
		data += ecc->size;
		spare_ecc += ecc->bytes;
	}

	return (err) ? err : stat;
}

/**
 * omap_write_page_bch - BCH ecc based write page function for entire page
 * @chip:		nand chip info structure
 * @buf:		data buffer
 * @oob_required:	must write chip->oob_poi to OOB
 * @page:		page
 *
 * Custom write page method evolved to support multi sector writing in one shot
 */
static int omap_write_page_bch(struct nand_chip *chip, const uint8_t *buf,
			       int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	uint8_t *ecc_calc = chip->ecc.calc_buf;
	unsigned int eccpg;
	int ret;

	ret = nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	if (ret)
		return ret;

	for (eccpg = 0; eccpg < info->neccpg; eccpg++) {
		/* Enable GPMC ecc engine */
		chip->ecc.hwctl(chip, NAND_ECC_WRITE);

		/* Write data */
		chip->legacy.write_buf(chip, buf + (eccpg * info->eccpg_size),
				       info->eccpg_size);

		/* Update ecc vector from GPMC result registers */
		ret = omap_calculate_ecc_bch_multi(mtd,
						   buf + (eccpg * info->eccpg_size),
						   ecc_calc);
		if (ret)
			return ret;

		ret = mtd_ooblayout_set_eccbytes(mtd, ecc_calc,
						 chip->oob_poi,
						 eccpg * info->eccpg_bytes,
						 info->eccpg_bytes);
		if (ret)
			return ret;
	}

	/* Write ecc vector to OOB area */
	chip->legacy.write_buf(chip, chip->oob_poi, mtd->oobsize);

	return nand_prog_page_end_op(chip);
}

/**
 * omap_write_subpage_bch - BCH hardware ECC based subpage write
 * @chip:	nand chip info structure
 * @offset:	column address of subpage within the page
 * @data_len:	data length
 * @buf:	data buffer
 * @oob_required: must write chip->oob_poi to OOB
 * @page: page number to write
 *
 * OMAP optimized subpage write method.
 */
static int omap_write_subpage_bch(struct nand_chip *chip, u32 offset,
				  u32 data_len, const u8 *buf,
				  int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	u8 *ecc_calc = chip->ecc.calc_buf;
	int ecc_size      = chip->ecc.size;
	int ecc_bytes     = chip->ecc.bytes;
	u32 start_step = offset / ecc_size;
	u32 end_step   = (offset + data_len - 1) / ecc_size;
	unsigned int eccpg;
	int step, ret = 0;

	/*
	 * Write entire page at one go as it would be optimal
	 * as ECC is calculated by hardware.
	 * ECC is calculated for all subpages but we choose
	 * only what we want.
	 */
	ret = nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	if (ret)
		return ret;

	for (eccpg = 0; eccpg < info->neccpg; eccpg++) {
		/* Enable GPMC ECC engine */
		chip->ecc.hwctl(chip, NAND_ECC_WRITE);

		/* Write data */
		chip->legacy.write_buf(chip, buf + (eccpg * info->eccpg_size),
				       info->eccpg_size);

		for (step = 0; step < info->nsteps_per_eccpg; step++) {
			unsigned int base_step = eccpg * info->nsteps_per_eccpg;
			const u8 *bufoffs = buf + (eccpg * info->eccpg_size);

			/* Mask ECC of un-touched subpages with 0xFFs */
			if ((step + base_step) < start_step ||
			    (step + base_step) > end_step)
				memset(ecc_calc + (step * ecc_bytes), 0xff,
				       ecc_bytes);
			else
				ret = _omap_calculate_ecc_bch(mtd,
							      bufoffs + (step * ecc_size),
							      ecc_calc + (step * ecc_bytes),
							      step);

			if (ret)
				return ret;
		}

		/*
		 * Copy the calculated ECC for the whole page including the
		 * masked values (0xFF) corresponding to unwritten subpages.
		 */
		ret = mtd_ooblayout_set_eccbytes(mtd, ecc_calc, chip->oob_poi,
						 eccpg * info->eccpg_bytes,
						 info->eccpg_bytes);
		if (ret)
			return ret;
	}

	/* write OOB buffer to NAND device */
	chip->legacy.write_buf(chip, chip->oob_poi, mtd->oobsize);

	return nand_prog_page_end_op(chip);
}

/**
 * omap_read_page_bch - BCH ecc based page read function for entire page
 * @chip:		nand chip info structure
 * @buf:		buffer to store read data
 * @oob_required:	caller requires OOB data read to chip->oob_poi
 * @page:		page number to read
 *
 * For BCH ecc scheme, GPMC used for syndrome calculation and ELM module
 * used for error correction.
 * Custom method evolved to support ELM error correction & multi sector
 * reading. On reading page data area is read along with OOB data with
 * ecc engine enabled. ecc vector updated after read of OOB data.
 * For non error pages ecc vector reported as zero.
 */
static int omap_read_page_bch(struct nand_chip *chip, uint8_t *buf,
			      int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	uint8_t *ecc_calc = chip->ecc.calc_buf;
	uint8_t *ecc_code = chip->ecc.code_buf;
	unsigned int max_bitflips = 0, eccpg;
	int stat, ret;

	ret = nand_read_page_op(chip, page, 0, NULL, 0);
	if (ret)
		return ret;

	for (eccpg = 0; eccpg < info->neccpg; eccpg++) {
		/* Enable GPMC ecc engine */
		chip->ecc.hwctl(chip, NAND_ECC_READ);

		/* Read data */
		ret = nand_change_read_column_op(chip, eccpg * info->eccpg_size,
						 buf + (eccpg * info->eccpg_size),
						 info->eccpg_size, false);
		if (ret)
			return ret;

		/* Read oob bytes */
		ret = nand_change_read_column_op(chip,
						 mtd->writesize + BBM_LEN +
						 (eccpg * info->eccpg_bytes),
						 chip->oob_poi + BBM_LEN +
						 (eccpg * info->eccpg_bytes),
						 info->eccpg_bytes, false);
		if (ret)
			return ret;

		/* Calculate ecc bytes */
		ret = omap_calculate_ecc_bch_multi(mtd,
						   buf + (eccpg * info->eccpg_size),
						   ecc_calc);
		if (ret)
			return ret;

		ret = mtd_ooblayout_get_eccbytes(mtd, ecc_code,
						 chip->oob_poi,
						 eccpg * info->eccpg_bytes,
						 info->eccpg_bytes);
		if (ret)
			return ret;

		stat = chip->ecc.correct(chip,
					 buf + (eccpg * info->eccpg_size),
					 ecc_code, ecc_calc);
		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}

	return max_bitflips;
}

/**
 * is_elm_present - checks for presence of ELM module by scanning DT nodes
 * @info: NAND device structure containing platform data
 * @elm_node: ELM's DT node
 */
static bool is_elm_present(struct omap_nand_info *info,
			   struct device_node *elm_node)
{
	struct platform_device *pdev;

	/* check whether elm-id is passed via DT */
	if (!elm_node) {
		dev_err(&info->pdev->dev, "ELM devicetree node not found\n");
		return false;
	}
	pdev = of_find_device_by_node(elm_node);
	/* check whether ELM device is registered */
	if (!pdev) {
		dev_err(&info->pdev->dev, "ELM device not found\n");
		return false;
	}
	/* ELM module available, now configure it */
	info->elm_dev = &pdev->dev;
	return true;
}

static bool omap2_nand_ecc_check(struct omap_nand_info *info)
{
	bool ecc_needs_bch, ecc_needs_omap_bch, ecc_needs_elm;

	switch (info->ecc_opt) {
	case OMAP_ECC_BCH4_CODE_HW_DETECTION_SW:
	case OMAP_ECC_BCH8_CODE_HW_DETECTION_SW:
		ecc_needs_omap_bch = false;
		ecc_needs_bch = true;
		ecc_needs_elm = false;
		break;
	case OMAP_ECC_BCH4_CODE_HW:
	case OMAP_ECC_BCH8_CODE_HW:
	case OMAP_ECC_BCH16_CODE_HW:
		ecc_needs_omap_bch = true;
		ecc_needs_bch = false;
		ecc_needs_elm = true;
		break;
	default:
		ecc_needs_omap_bch = false;
		ecc_needs_bch = false;
		ecc_needs_elm = false;
		break;
	}

	if (ecc_needs_bch && !IS_ENABLED(CONFIG_MTD_NAND_ECC_SW_BCH)) {
		dev_err(&info->pdev->dev,
			"CONFIG_MTD_NAND_ECC_SW_BCH not enabled\n");
		return false;
	}
	if (ecc_needs_omap_bch && !IS_ENABLED(CONFIG_MTD_NAND_OMAP_BCH)) {
		dev_err(&info->pdev->dev,
			"CONFIG_MTD_NAND_OMAP_BCH not enabled\n");
		return false;
	}
	if (ecc_needs_elm && !is_elm_present(info, info->elm_of_node)) {
		dev_err(&info->pdev->dev, "ELM not available\n");
		return false;
	}

	return true;
}

static const char * const nand_xfer_types[] = {
	[NAND_OMAP_PREFETCH_POLLED] = "prefetch-polled",
	[NAND_OMAP_POLLED] = "polled",
	[NAND_OMAP_PREFETCH_DMA] = "prefetch-dma",
	[NAND_OMAP_PREFETCH_IRQ] = "prefetch-irq",
};

static int omap_get_dt_info(struct device *dev, struct omap_nand_info *info)
{
	struct device_node *child = dev->of_node;
	int i;
	const char *s;
	u32 cs;

	if (of_property_read_u32(child, "reg", &cs) < 0) {
		dev_err(dev, "reg not found in DT\n");
		return -EINVAL;
	}

	info->gpmc_cs = cs;

	/* detect availability of ELM module. Won't be present pre-OMAP4 */
	info->elm_of_node = of_parse_phandle(child, "ti,elm-id", 0);
	if (!info->elm_of_node) {
		info->elm_of_node = of_parse_phandle(child, "elm_id", 0);
		if (!info->elm_of_node)
			dev_dbg(dev, "ti,elm-id not in DT\n");
	}

	/* select ecc-scheme for NAND */
	if (of_property_read_string(child, "ti,nand-ecc-opt", &s)) {
		dev_err(dev, "ti,nand-ecc-opt not found\n");
		return -EINVAL;
	}

	if (!strcmp(s, "sw")) {
		info->ecc_opt = OMAP_ECC_HAM1_CODE_SW;
	} else if (!strcmp(s, "ham1") ||
		   !strcmp(s, "hw") || !strcmp(s, "hw-romcode")) {
		info->ecc_opt =	OMAP_ECC_HAM1_CODE_HW;
	} else if (!strcmp(s, "bch4")) {
		if (info->elm_of_node)
			info->ecc_opt = OMAP_ECC_BCH4_CODE_HW;
		else
			info->ecc_opt = OMAP_ECC_BCH4_CODE_HW_DETECTION_SW;
	} else if (!strcmp(s, "bch8")) {
		if (info->elm_of_node)
			info->ecc_opt = OMAP_ECC_BCH8_CODE_HW;
		else
			info->ecc_opt = OMAP_ECC_BCH8_CODE_HW_DETECTION_SW;
	} else if (!strcmp(s, "bch16")) {
		info->ecc_opt =	OMAP_ECC_BCH16_CODE_HW;
	} else {
		dev_err(dev, "unrecognized value for ti,nand-ecc-opt\n");
		return -EINVAL;
	}

	/* select data transfer mode */
	if (!of_property_read_string(child, "ti,nand-xfer-type", &s)) {
		for (i = 0; i < ARRAY_SIZE(nand_xfer_types); i++) {
			if (!strcasecmp(s, nand_xfer_types[i])) {
				info->xfer_type = i;
				return 0;
			}
		}

		dev_err(dev, "unrecognized value for ti,nand-xfer-type\n");
		return -EINVAL;
	}

	return 0;
}

static int omap_ooblayout_ecc(struct mtd_info *mtd, int section,
			      struct mtd_oob_region *oobregion)
{
	struct omap_nand_info *info = mtd_to_omap(mtd);
	struct nand_chip *chip = &info->nand;
	int off = BBM_LEN;

	if (info->ecc_opt == OMAP_ECC_HAM1_CODE_HW &&
	    !(chip->options & NAND_BUSWIDTH_16))
		off = 1;

	if (section)
		return -ERANGE;

	oobregion->offset = off;
	oobregion->length = chip->ecc.total;

	return 0;
}

static int omap_ooblayout_free(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	struct omap_nand_info *info = mtd_to_omap(mtd);
	struct nand_chip *chip = &info->nand;
	int off = BBM_LEN;

	if (info->ecc_opt == OMAP_ECC_HAM1_CODE_HW &&
	    !(chip->options & NAND_BUSWIDTH_16))
		off = 1;

	if (section)
		return -ERANGE;

	off += chip->ecc.total;
	if (off >= mtd->oobsize)
		return -ERANGE;

	oobregion->offset = off;
	oobregion->length = mtd->oobsize - off;

	return 0;
}

static const struct mtd_ooblayout_ops omap_ooblayout_ops = {
	.ecc = omap_ooblayout_ecc,
	.free = omap_ooblayout_free,
};

static int omap_sw_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	unsigned int nsteps = nanddev_get_ecc_nsteps(nand);
	unsigned int ecc_bytes = nanddev_get_ecc_bytes_per_step(nand);
	int off = BBM_LEN;

	if (section >= nsteps)
		return -ERANGE;

	/*
	 * When SW correction is employed, one OMAP specific marker byte is
	 * reserved after each ECC step.
	 */
	oobregion->offset = off + (section * (ecc_bytes + 1));
	oobregion->length = ecc_bytes;

	return 0;
}

static int omap_sw_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	unsigned int nsteps = nanddev_get_ecc_nsteps(nand);
	unsigned int ecc_bytes = nanddev_get_ecc_bytes_per_step(nand);
	int off = BBM_LEN;

	if (section)
		return -ERANGE;

	/*
	 * When SW correction is employed, one OMAP specific marker byte is
	 * reserved after each ECC step.
	 */
	off += ((ecc_bytes + 1) * nsteps);
	if (off >= mtd->oobsize)
		return -ERANGE;

	oobregion->offset = off;
	oobregion->length = mtd->oobsize - off;

	return 0;
}

static const struct mtd_ooblayout_ops omap_sw_ooblayout_ops = {
	.ecc = omap_sw_ooblayout_ecc,
	.free = omap_sw_ooblayout_free,
};

static int omap_nand_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	struct device *dev = &info->pdev->dev;
	int min_oobbytes = BBM_LEN;
	int elm_bch_strength = -1;
	int oobbytes_per_step;
	dma_cap_mask_t mask;
	int err;

	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;
	else
		chip->options |= NAND_SKIP_BBTSCAN;

	/* Re-populate low-level callbacks based on xfer modes */
	switch (info->xfer_type) {
	case NAND_OMAP_PREFETCH_POLLED:
		chip->legacy.read_buf = omap_read_buf_pref;
		chip->legacy.write_buf = omap_write_buf_pref;
		break;

	case NAND_OMAP_POLLED:
		/* Use nand_base defaults for {read,write}_buf */
		break;

	case NAND_OMAP_PREFETCH_DMA:
		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);
		info->dma = dma_request_chan(dev->parent, "rxtx");

		if (IS_ERR(info->dma)) {
			dev_err(dev, "DMA engine request failed\n");
			return PTR_ERR(info->dma);
		} else {
			struct dma_slave_config cfg;

			memset(&cfg, 0, sizeof(cfg));
			cfg.src_addr = info->phys_base;
			cfg.dst_addr = info->phys_base;
			cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
			cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
			cfg.src_maxburst = 16;
			cfg.dst_maxburst = 16;
			err = dmaengine_slave_config(info->dma, &cfg);
			if (err) {
				dev_err(dev,
					"DMA engine slave config failed: %d\n",
					err);
				return err;
			}
			chip->legacy.read_buf = omap_read_buf_dma_pref;
			chip->legacy.write_buf = omap_write_buf_dma_pref;
		}
		break;

	case NAND_OMAP_PREFETCH_IRQ:
		info->gpmc_irq_fifo = platform_get_irq(info->pdev, 0);
		if (info->gpmc_irq_fifo <= 0)
			return -ENODEV;
		err = devm_request_irq(dev, info->gpmc_irq_fifo,
				       omap_nand_irq, IRQF_SHARED,
				       "gpmc-nand-fifo", info);
		if (err) {
			dev_err(dev, "Requesting IRQ %d, error %d\n",
				info->gpmc_irq_fifo, err);
			info->gpmc_irq_fifo = 0;
			return err;
		}

		info->gpmc_irq_count = platform_get_irq(info->pdev, 1);
		if (info->gpmc_irq_count <= 0)
			return -ENODEV;
		err = devm_request_irq(dev, info->gpmc_irq_count,
				       omap_nand_irq, IRQF_SHARED,
				       "gpmc-nand-count", info);
		if (err) {
			dev_err(dev, "Requesting IRQ %d, error %d\n",
				info->gpmc_irq_count, err);
			info->gpmc_irq_count = 0;
			return err;
		}

		chip->legacy.read_buf = omap_read_buf_irq_pref;
		chip->legacy.write_buf = omap_write_buf_irq_pref;

		break;

	default:
		dev_err(dev, "xfer_type %d not supported!\n", info->xfer_type);
		return -EINVAL;
	}

	if (!omap2_nand_ecc_check(info))
		return -EINVAL;

	/*
	 * Bail out earlier to let NAND_ECC_ENGINE_TYPE_SOFT code create its own
	 * ooblayout instead of using ours.
	 */
	if (info->ecc_opt == OMAP_ECC_HAM1_CODE_SW) {
		chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_SOFT;
		chip->ecc.algo = NAND_ECC_ALGO_HAMMING;
		return 0;
	}

	/* Populate MTD interface based on ECC scheme */
	switch (info->ecc_opt) {
	case OMAP_ECC_HAM1_CODE_HW:
		dev_info(dev, "nand: using OMAP_ECC_HAM1_CODE_HW\n");
		chip->ecc.engine_type	= NAND_ECC_ENGINE_TYPE_ON_HOST;
		chip->ecc.bytes		= 3;
		chip->ecc.size		= 512;
		chip->ecc.strength	= 1;
		chip->ecc.calculate	= omap_calculate_ecc;
		chip->ecc.hwctl		= omap_enable_hwecc;
		chip->ecc.correct	= omap_correct_data;
		mtd_set_ooblayout(mtd, &omap_ooblayout_ops);
		oobbytes_per_step	= chip->ecc.bytes;

		if (!(chip->options & NAND_BUSWIDTH_16))
			min_oobbytes	= 1;

		break;

	case OMAP_ECC_BCH4_CODE_HW_DETECTION_SW:
		pr_info("nand: using OMAP_ECC_BCH4_CODE_HW_DETECTION_SW\n");
		chip->ecc.engine_type	= NAND_ECC_ENGINE_TYPE_ON_HOST;
		chip->ecc.size		= 512;
		chip->ecc.bytes		= 7;
		chip->ecc.strength	= 4;
		chip->ecc.hwctl		= omap_enable_hwecc_bch;
		chip->ecc.correct	= rawnand_sw_bch_correct;
		chip->ecc.calculate	= omap_calculate_ecc_bch_sw;
		mtd_set_ooblayout(mtd, &omap_sw_ooblayout_ops);
		/* Reserve one byte for the OMAP marker */
		oobbytes_per_step	= chip->ecc.bytes + 1;
		/* Software BCH library is used for locating errors */
		err = rawnand_sw_bch_init(chip);
		if (err) {
			dev_err(dev, "Unable to use BCH library\n");
			return err;
		}
		break;

	case OMAP_ECC_BCH4_CODE_HW:
		pr_info("nand: using OMAP_ECC_BCH4_CODE_HW ECC scheme\n");
		chip->ecc.engine_type	= NAND_ECC_ENGINE_TYPE_ON_HOST;
		chip->ecc.size		= 512;
		/* 14th bit is kept reserved for ROM-code compatibility */
		chip->ecc.bytes		= 7 + 1;
		chip->ecc.strength	= 4;
		chip->ecc.hwctl		= omap_enable_hwecc_bch;
		chip->ecc.correct	= omap_elm_correct_data;
		chip->ecc.read_page	= omap_read_page_bch;
		chip->ecc.write_page	= omap_write_page_bch;
		chip->ecc.write_subpage	= omap_write_subpage_bch;
		mtd_set_ooblayout(mtd, &omap_ooblayout_ops);
		oobbytes_per_step	= chip->ecc.bytes;
		elm_bch_strength = BCH4_ECC;
		break;

	case OMAP_ECC_BCH8_CODE_HW_DETECTION_SW:
		pr_info("nand: using OMAP_ECC_BCH8_CODE_HW_DETECTION_SW\n");
		chip->ecc.engine_type	= NAND_ECC_ENGINE_TYPE_ON_HOST;
		chip->ecc.size		= 512;
		chip->ecc.bytes		= 13;
		chip->ecc.strength	= 8;
		chip->ecc.hwctl		= omap_enable_hwecc_bch;
		chip->ecc.correct	= rawnand_sw_bch_correct;
		chip->ecc.calculate	= omap_calculate_ecc_bch_sw;
		mtd_set_ooblayout(mtd, &omap_sw_ooblayout_ops);
		/* Reserve one byte for the OMAP marker */
		oobbytes_per_step	= chip->ecc.bytes + 1;
		/* Software BCH library is used for locating errors */
		err = rawnand_sw_bch_init(chip);
		if (err) {
			dev_err(dev, "unable to use BCH library\n");
			return err;
		}
		break;

	case OMAP_ECC_BCH8_CODE_HW:
		pr_info("nand: using OMAP_ECC_BCH8_CODE_HW ECC scheme\n");
		chip->ecc.engine_type	= NAND_ECC_ENGINE_TYPE_ON_HOST;
		chip->ecc.size		= 512;
		/* 14th bit is kept reserved for ROM-code compatibility */
		chip->ecc.bytes		= 13 + 1;
		chip->ecc.strength	= 8;
		chip->ecc.hwctl		= omap_enable_hwecc_bch;
		chip->ecc.correct	= omap_elm_correct_data;
		chip->ecc.read_page	= omap_read_page_bch;
		chip->ecc.write_page	= omap_write_page_bch;
		chip->ecc.write_subpage	= omap_write_subpage_bch;
		mtd_set_ooblayout(mtd, &omap_ooblayout_ops);
		oobbytes_per_step	= chip->ecc.bytes;
		elm_bch_strength = BCH8_ECC;
		break;

	case OMAP_ECC_BCH16_CODE_HW:
		pr_info("Using OMAP_ECC_BCH16_CODE_HW ECC scheme\n");
		chip->ecc.engine_type	= NAND_ECC_ENGINE_TYPE_ON_HOST;
		chip->ecc.size		= 512;
		chip->ecc.bytes		= 26;
		chip->ecc.strength	= 16;
		chip->ecc.hwctl		= omap_enable_hwecc_bch;
		chip->ecc.correct	= omap_elm_correct_data;
		chip->ecc.read_page	= omap_read_page_bch;
		chip->ecc.write_page	= omap_write_page_bch;
		chip->ecc.write_subpage	= omap_write_subpage_bch;
		mtd_set_ooblayout(mtd, &omap_ooblayout_ops);
		oobbytes_per_step	= chip->ecc.bytes;
		elm_bch_strength = BCH16_ECC;
		break;
	default:
		dev_err(dev, "Invalid or unsupported ECC scheme\n");
		return -EINVAL;
	}

	if (elm_bch_strength >= 0) {
		chip->ecc.steps = mtd->writesize / chip->ecc.size;
		info->neccpg = chip->ecc.steps / ERROR_VECTOR_MAX;
		if (info->neccpg) {
			info->nsteps_per_eccpg = ERROR_VECTOR_MAX;
		} else {
			info->neccpg = 1;
			info->nsteps_per_eccpg = chip->ecc.steps;
		}
		info->eccpg_size = info->nsteps_per_eccpg * chip->ecc.size;
		info->eccpg_bytes = info->nsteps_per_eccpg * chip->ecc.bytes;

		err = elm_config(info->elm_dev, elm_bch_strength,
				 info->nsteps_per_eccpg, chip->ecc.size,
				 chip->ecc.bytes);
		if (err < 0)
			return err;
	}

	/* Check if NAND device's OOB is enough to store ECC signatures */
	min_oobbytes += (oobbytes_per_step *
			 (mtd->writesize / chip->ecc.size));
	if (mtd->oobsize < min_oobbytes) {
		dev_err(dev,
			"Not enough OOB bytes: required = %d, available=%d\n",
			min_oobbytes, mtd->oobsize);
		return -EINVAL;
	}

	return 0;
}

static const struct nand_controller_ops omap_nand_controller_ops = {
	.attach_chip = omap_nand_attach_chip,
};

/* Shared among all NAND instances to synchronize access to the ECC Engine */
static struct nand_controller omap_gpmc_controller;
static bool omap_gpmc_controller_initialized;

static int omap_nand_probe(struct platform_device *pdev)
{
	struct omap_nand_info		*info;
	struct mtd_info			*mtd;
	struct nand_chip		*nand_chip;
	int				err;
	struct resource			*res;
	struct device			*dev = &pdev->dev;

	info = devm_kzalloc(&pdev->dev, sizeof(struct omap_nand_info),
				GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->pdev = pdev;

	err = omap_get_dt_info(dev, info);
	if (err)
		return err;

	info->ops = gpmc_omap_get_nand_ops(&info->reg, info->gpmc_cs);
	if (!info->ops) {
		dev_err(&pdev->dev, "Failed to get GPMC->NAND interface\n");
		return -ENODEV;
	}

	nand_chip		= &info->nand;
	mtd			= nand_to_mtd(nand_chip);
	mtd->dev.parent		= &pdev->dev;
	nand_set_flash_node(nand_chip, dev->of_node);

	if (!mtd->name) {
		mtd->name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
					   "omap2-nand.%d", info->gpmc_cs);
		if (!mtd->name) {
			dev_err(&pdev->dev, "Failed to set MTD name\n");
			return -ENOMEM;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nand_chip->legacy.IO_ADDR_R = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nand_chip->legacy.IO_ADDR_R))
		return PTR_ERR(nand_chip->legacy.IO_ADDR_R);

	info->phys_base = res->start;

	if (!omap_gpmc_controller_initialized) {
		omap_gpmc_controller.ops = &omap_nand_controller_ops;
		nand_controller_init(&omap_gpmc_controller);
		omap_gpmc_controller_initialized = true;
	}

	nand_chip->controller = &omap_gpmc_controller;

	nand_chip->legacy.IO_ADDR_W = nand_chip->legacy.IO_ADDR_R;
	nand_chip->legacy.cmd_ctrl  = omap_hwcontrol;

	info->ready_gpiod = devm_gpiod_get_optional(&pdev->dev, "rb",
						    GPIOD_IN);
	if (IS_ERR(info->ready_gpiod)) {
		dev_err(dev, "failed to get ready gpio\n");
		return PTR_ERR(info->ready_gpiod);
	}

	/*
	 * If RDY/BSY line is connected to OMAP then use the omap ready
	 * function and the generic nand_wait function which reads the status
	 * register after monitoring the RDY/BSY line. Otherwise use a standard
	 * chip delay which is slightly more than tR (AC Timing) of the NAND
	 * device and read status register until you get a failure or success
	 */
	if (info->ready_gpiod) {
		nand_chip->legacy.dev_ready = omap_dev_ready;
		nand_chip->legacy.chip_delay = 0;
	} else {
		nand_chip->legacy.waitfunc = omap_wait;
		nand_chip->legacy.chip_delay = 50;
	}

	if (info->flash_bbt)
		nand_chip->bbt_options |= NAND_BBT_USE_FLASH;

	/* scan NAND device connected to chip controller */
	nand_chip->options |= info->devsize & NAND_BUSWIDTH_16;

	err = nand_scan(nand_chip, 1);
	if (err)
		goto return_error;

	err = mtd_device_register(mtd, NULL, 0);
	if (err)
		goto cleanup_nand;

	platform_set_drvdata(pdev, mtd);

	return 0;

cleanup_nand:
	nand_cleanup(nand_chip);

return_error:
	if (!IS_ERR_OR_NULL(info->dma))
		dma_release_channel(info->dma);

	rawnand_sw_bch_cleanup(nand_chip);

	return err;
}

static int omap_nand_remove(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct omap_nand_info *info = mtd_to_omap(mtd);
	int ret;

	rawnand_sw_bch_cleanup(nand_chip);

	if (info->dma)
		dma_release_channel(info->dma);
	ret = mtd_device_unregister(mtd);
	WARN_ON(ret);
	nand_cleanup(nand_chip);
	return ret;
}

static const struct of_device_id omap_nand_ids[] = {
	{ .compatible = "ti,omap2-nand", },
	{},
};
MODULE_DEVICE_TABLE(of, omap_nand_ids);

static struct platform_driver omap_nand_driver = {
	.probe		= omap_nand_probe,
	.remove		= omap_nand_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(omap_nand_ids),
	},
};

module_platform_driver(omap_nand_driver);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Glue layer for NAND flash on TI OMAP boards");
