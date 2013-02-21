/*
 * Copyright © 2004 Texas Instruments, Jian Zhang <jzhang@ti.com>
 * Copyright © 2004 Micron Technology Inc.
 * Copyright © 2004 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/omap-dma.h>
#include <linux/io.h>
#include <linux/slab.h>

#ifdef CONFIG_MTD_NAND_OMAP_BCH
#include <linux/bch.h>
#endif

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

#define OMAP24XX_DMA_GPMC		4

/* oob info generated runtime depending on ecc algorithm and layout selected */
static struct nand_ecclayout omap_oobinfo;
/* Define some generic bad / good block scan pattern which are used
 * while scanning a device for factory marked good / bad blocks
 */
static uint8_t scan_ff_pattern[] = { 0xff };
static struct nand_bbt_descr bb_descrip_flashbased = {
	.options = NAND_BBT_SCANEMPTY | NAND_BBT_SCANALLPAGES,
	.offs = 0,
	.len = 1,
	.pattern = scan_ff_pattern,
};


struct omap_nand_info {
	struct nand_hw_control		controller;
	struct omap_nand_platform_data	*pdata;
	struct mtd_info			mtd;
	struct nand_chip		nand;
	struct platform_device		*pdev;

	int				gpmc_cs;
	unsigned long			phys_base;
	unsigned long			mem_size;
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
	struct gpmc_nand_regs		reg;

#ifdef CONFIG_MTD_NAND_OMAP_BCH
	struct bch_control             *bch;
	struct nand_ecclayout           ecclayout;
#endif
};

/**
 * omap_prefetch_enable - configures and starts prefetch transfer
 * @cs: cs (chip select) number
 * @fifo_th: fifo threshold to be used for read/ write
 * @dma_mode: dma mode enable (1) or disable (0)
 * @u32_count: number of bytes to be transferred
 * @is_write: prefetch read(0) or write post(1) mode
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
		(dma_mode << DMA_MPU_MODE_SHIFT) | (0x1 & is_write));
	writel(val, info->reg.gpmc_prefetch_config1);

	/*  Start the prefetch engine */
	writel(0x1, info->reg.gpmc_prefetch_control);

	return 0;
}

/**
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
 * @mtd: MTD device structure
 * @cmd: command to device
 * @ctrl:
 * NAND_NCE: bit 0 -> don't care
 * NAND_CLE: bit 1 -> Command Latch
 * NAND_ALE: bit 2 -> Address Latch
 *
 * NOTE: boards may use different bits for these!!
 */
static void omap_hwcontrol(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct omap_nand_info *info = container_of(mtd,
					struct omap_nand_info, mtd);

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
	struct nand_chip *nand = mtd->priv;

	ioread8_rep(nand->IO_ADDR_R, buf, len);
}

/**
 * omap_write_buf8 - write buffer to NAND controller
 * @mtd: MTD device structure
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf8(struct mtd_info *mtd, const u_char *buf, int len)
{
	struct omap_nand_info *info = container_of(mtd,
						struct omap_nand_info, mtd);
	u_char *p = (u_char *)buf;
	u32	status = 0;

	while (len--) {
		iowrite8(*p++, info->nand.IO_ADDR_W);
		/* wait until buffer is available for write */
		do {
			status = readl(info->reg.gpmc_status) &
					STATUS_BUFF_EMPTY;
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
	struct nand_chip *nand = mtd->priv;

	ioread16_rep(nand->IO_ADDR_R, buf, len / 2);
}

/**
 * omap_write_buf16 - write buffer to NAND controller
 * @mtd: MTD device structure
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf16(struct mtd_info *mtd, const u_char * buf, int len)
{
	struct omap_nand_info *info = container_of(mtd,
						struct omap_nand_info, mtd);
	u16 *p = (u16 *) buf;
	u32	status = 0;
	/* FIXME try bursts of writesw() or DMA ... */
	len >>= 1;

	while (len--) {
		iowrite16(*p++, info->nand.IO_ADDR_W);
		/* wait until buffer is available for write */
		do {
			status = readl(info->reg.gpmc_status) &
					STATUS_BUFF_EMPTY;
		} while (!status);
	}
}

/**
 * omap_read_buf_pref - read data from NAND controller into buffer
 * @mtd: MTD device structure
 * @buf: buffer to store date
 * @len: number of bytes to read
 */
static void omap_read_buf_pref(struct mtd_info *mtd, u_char *buf, int len)
{
	struct omap_nand_info *info = container_of(mtd,
						struct omap_nand_info, mtd);
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
			ioread32_rep(info->nand.IO_ADDR_R, p, r_count);
			p += r_count;
			len -= r_count << 2;
		} while (len);
		/* disable and stop the PFPW engine */
		omap_prefetch_reset(info->gpmc_cs, info);
	}
}

/**
 * omap_write_buf_pref - write buffer to NAND controller
 * @mtd: MTD device structure
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf_pref(struct mtd_info *mtd,
					const u_char *buf, int len)
{
	struct omap_nand_info *info = container_of(mtd,
						struct omap_nand_info, mtd);
	uint32_t w_count = 0;
	int i = 0, ret = 0;
	u16 *p = (u16 *)buf;
	unsigned long tim, limit;
	u32 val;

	/* take care of subpage writes */
	if (len % 2 != 0) {
		writeb(*buf, info->nand.IO_ADDR_W);
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
				iowrite16(*p++, info->nand.IO_ADDR_W);
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
	struct omap_nand_info *info = container_of(mtd,
					struct omap_nand_info, mtd);
	struct dma_async_tx_descriptor *tx;
	enum dma_data_direction dir = is_write ? DMA_TO_DEVICE :
							DMA_FROM_DEVICE;
	struct scatterlist sg;
	unsigned long tim, limit;
	unsigned n;
	int ret;
	u32 val;

	if (addr >= high_memory) {
		struct page *p1;

		if (((size_t)addr & PAGE_MASK) !=
			((size_t)(addr + len - 1) & PAGE_MASK))
			goto out_copy;
		p1 = vmalloc_to_page(addr);
		if (!p1)
			goto out_copy;
		addr = page_address(p1) + ((size_t)addr & ~PAGE_MASK);
	}

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

	/*  configure and start prefetch transfer */
	ret = omap_prefetch_enable(info->gpmc_cs,
		PREFETCH_FIFOTHRESHOLD_MAX, 0x1, len, is_write, info);
	if (ret)
		/* PFPW engine is busy, use cpu copy method */
		goto out_copy_unmap;

	init_completion(&info->comp);
	dma_async_issue_pending(info->dma);

	/* setup and start DMA using dma_addr */
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
 * @mtd: MTD device structure
 * @buf: buffer to store date
 * @len: number of bytes to read
 */
static void omap_read_buf_dma_pref(struct mtd_info *mtd, u_char *buf, int len)
{
	if (len <= mtd->oobsize)
		omap_read_buf_pref(mtd, buf, len);
	else
		/* start transfer in DMA mode */
		omap_nand_dma_transfer(mtd, buf, len, 0x0);
}

/**
 * omap_write_buf_dma_pref - write buffer to NAND controller
 * @mtd: MTD device structure
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf_dma_pref(struct mtd_info *mtd,
					const u_char *buf, int len)
{
	if (len <= mtd->oobsize)
		omap_write_buf_pref(mtd, buf, len);
	else
		/* start transfer in DMA mode */
		omap_nand_dma_transfer(mtd, (u_char *) buf, len, 0x1);
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
		iowrite32_rep(info->nand.IO_ADDR_W,
						(u32 *)info->buf, bytes >> 2);
		info->buf = info->buf + bytes;
		info->buf_len -= bytes;

	} else {
		ioread32_rep(info->nand.IO_ADDR_R,
						(u32 *)info->buf, bytes >> 2);
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
 * @mtd: MTD device structure
 * @buf: buffer to store date
 * @len: number of bytes to read
 */
static void omap_read_buf_irq_pref(struct mtd_info *mtd, u_char *buf, int len)
{
	struct omap_nand_info *info = container_of(mtd,
						struct omap_nand_info, mtd);
	int ret = 0;

	if (len <= mtd->oobsize) {
		omap_read_buf_pref(mtd, buf, len);
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
 * @mtd: MTD device structure
 * @buf: data buffer
 * @len: number of bytes to write
 */
static void omap_write_buf_irq_pref(struct mtd_info *mtd,
					const u_char *buf, int len)
{
	struct omap_nand_info *info = container_of(mtd,
						struct omap_nand_info, mtd);
	int ret = 0;
	unsigned long tim, limit;
	u32 val;

	if (len <= mtd->oobsize) {
		omap_write_buf_pref(mtd, buf, len);
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
		return -1;

	case 11:
		/* UN-Correctable error */
		pr_debug("ECC UNCORRECTED_ERROR B\n");
		return -1;

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
		return -1;
	}
}

/**
 * omap_correct_data - Compares the ECC read with HW generated ECC
 * @mtd: MTD device structure
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
static int omap_correct_data(struct mtd_info *mtd, u_char *dat,
				u_char *read_ecc, u_char *calc_ecc)
{
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
							mtd);
	int blockCnt = 0, i = 0, ret = 0;
	int stat = 0;

	/* Ex NAND_ECC_HW12_2048 */
	if ((info->nand.ecc.mode == NAND_ECC_HW) &&
			(info->nand.ecc.size  == 2048))
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
 * @mtd: MTD device structure
 * @dat: The pointer to data on which ecc is computed
 * @ecc_code: The ecc_code buffer
 *
 * Using noninverted ECC can be considered ugly since writing a blank
 * page ie. padding will clear the ECC bytes. This is no problem as long
 * nobody is trying to write data on the seemingly unused page. Reading
 * an erased page will produce an ECC mismatch between generated and read
 * ECC bytes that has to be dealt with separately.
 */
static int omap_calculate_ecc(struct mtd_info *mtd, const u_char *dat,
				u_char *ecc_code)
{
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
							mtd);
	u32 val;

	val = readl(info->reg.gpmc_ecc_config);
	if (((val >> ECC_CONFIG_CS_SHIFT)  & ~CS_MASK) != info->gpmc_cs)
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
 * @mtd: MTD device structure
 * @mode: Read/Write mode
 */
static void omap_enable_hwecc(struct mtd_info *mtd, int mode)
{
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
							mtd);
	struct nand_chip *chip = mtd->priv;
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
 * @mtd: MTD device structure
 * @chip: NAND Chip structure
 *
 * Wait function is called during Program and erase operations and
 * the way it is called from MTD layer, we should wait till the NAND
 * chip is ready after the programming/erase operation has completed.
 *
 * Erase can take up to 400ms and program up to 20ms according to
 * general NAND and SmartMedia specs
 */
static int omap_wait(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct nand_chip *this = mtd->priv;
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
							mtd);
	unsigned long timeo = jiffies;
	int status, state = this->state;

	if (state == FL_ERASING)
		timeo += (HZ * 400) / 1000;
	else
		timeo += (HZ * 20) / 1000;

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
 * omap_dev_ready - calls the platform specific dev_ready function
 * @mtd: MTD device structure
 */
static int omap_dev_ready(struct mtd_info *mtd)
{
	unsigned int val = 0;
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
							mtd);

	val = readl(info->reg.gpmc_status);

	if ((val & 0x100) == 0x100) {
		return 1;
	} else {
		return 0;
	}
}

#ifdef CONFIG_MTD_NAND_OMAP_BCH

/**
 * omap3_enable_hwecc_bch - Program OMAP3 GPMC to perform BCH ECC correction
 * @mtd: MTD device structure
 * @mode: Read/Write mode
 */
static void omap3_enable_hwecc_bch(struct mtd_info *mtd, int mode)
{
	int nerrors;
	unsigned int dev_width, nsectors;
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
						   mtd);
	struct nand_chip *chip = mtd->priv;
	u32 val;

	nerrors = (info->nand.ecc.bytes == 13) ? 8 : 4;
	dev_width = (chip->options & NAND_BUSWIDTH_16) ? 1 : 0;
	nsectors = 1;
	/*
	 * Program GPMC to perform correction on one 512-byte sector at a time.
	 * Using 4 sectors at a time (i.e. ecc.size = 2048) is also possible and
	 * gives a slight (5%) performance gain (but requires additional code).
	 */

	writel(ECC1, info->reg.gpmc_ecc_control);

	/*
	 * When using BCH, sector size is hardcoded to 512 bytes.
	 * Here we are using wrapping mode 6 both for reading and writing, with:
	 *  size0 = 0  (no additional protected byte in spare area)
	 *  size1 = 32 (skip 32 nibbles = 16 bytes per sector in spare area)
	 */
	val = (32 << ECCSIZE1_SHIFT) | (0 << ECCSIZE0_SHIFT);
	writel(val, info->reg.gpmc_ecc_size_config);

	/* BCH configuration */
	val = ((1                        << 16) | /* enable BCH */
	       (((nerrors == 8) ? 1 : 0) << 12) | /* 8 or 4 bits */
	       (0x06                     <<  8) | /* wrap mode = 6 */
	       (dev_width                <<  7) | /* bus width */
	       (((nsectors-1) & 0x7)     <<  4) | /* number of sectors */
	       (info->gpmc_cs            <<  1) | /* ECC CS */
	       (0x1));                            /* enable ECC */

	writel(val, info->reg.gpmc_ecc_config);

	/* clear ecc and enable bits */
	writel(ECCCLEAR | ECC1, info->reg.gpmc_ecc_control);
}

/**
 * omap3_calculate_ecc_bch4 - Generate 7 bytes of ECC bytes
 * @mtd: MTD device structure
 * @dat: The pointer to data on which ecc is computed
 * @ecc_code: The ecc_code buffer
 */
static int omap3_calculate_ecc_bch4(struct mtd_info *mtd, const u_char *dat,
				    u_char *ecc_code)
{
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
						   mtd);
	unsigned long nsectors, val1, val2;
	int i;

	nsectors = ((readl(info->reg.gpmc_ecc_config) >> 4) & 0x7) + 1;

	for (i = 0; i < nsectors; i++) {

		/* Read hw-computed remainder */
		val1 = readl(info->reg.gpmc_bch_result0[i]);
		val2 = readl(info->reg.gpmc_bch_result1[i]);

		/*
		 * Add constant polynomial to remainder, in order to get an ecc
		 * sequence of 0xFFs for a buffer filled with 0xFFs; and
		 * left-justify the resulting polynomial.
		 */
		*ecc_code++ = 0x28 ^ ((val2 >> 12) & 0xFF);
		*ecc_code++ = 0x13 ^ ((val2 >>  4) & 0xFF);
		*ecc_code++ = 0xcc ^ (((val2 & 0xF) << 4)|((val1 >> 28) & 0xF));
		*ecc_code++ = 0x39 ^ ((val1 >> 20) & 0xFF);
		*ecc_code++ = 0x96 ^ ((val1 >> 12) & 0xFF);
		*ecc_code++ = 0xac ^ ((val1 >> 4) & 0xFF);
		*ecc_code++ = 0x7f ^ ((val1 & 0xF) << 4);
	}

	return 0;
}

/**
 * omap3_calculate_ecc_bch8 - Generate 13 bytes of ECC bytes
 * @mtd: MTD device structure
 * @dat: The pointer to data on which ecc is computed
 * @ecc_code: The ecc_code buffer
 */
static int omap3_calculate_ecc_bch8(struct mtd_info *mtd, const u_char *dat,
				    u_char *ecc_code)
{
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
						   mtd);
	unsigned long nsectors, val1, val2, val3, val4;
	int i;

	nsectors = ((readl(info->reg.gpmc_ecc_config) >> 4) & 0x7) + 1;

	for (i = 0; i < nsectors; i++) {

		/* Read hw-computed remainder */
		val1 = readl(info->reg.gpmc_bch_result0[i]);
		val2 = readl(info->reg.gpmc_bch_result1[i]);
		val3 = readl(info->reg.gpmc_bch_result2[i]);
		val4 = readl(info->reg.gpmc_bch_result3[i]);

		/*
		 * Add constant polynomial to remainder, in order to get an ecc
		 * sequence of 0xFFs for a buffer filled with 0xFFs.
		 */
		*ecc_code++ = 0xef ^ (val4 & 0xFF);
		*ecc_code++ = 0x51 ^ ((val3 >> 24) & 0xFF);
		*ecc_code++ = 0x2e ^ ((val3 >> 16) & 0xFF);
		*ecc_code++ = 0x09 ^ ((val3 >> 8) & 0xFF);
		*ecc_code++ = 0xed ^ (val3 & 0xFF);
		*ecc_code++ = 0x93 ^ ((val2 >> 24) & 0xFF);
		*ecc_code++ = 0x9a ^ ((val2 >> 16) & 0xFF);
		*ecc_code++ = 0xc2 ^ ((val2 >> 8) & 0xFF);
		*ecc_code++ = 0x97 ^ (val2 & 0xFF);
		*ecc_code++ = 0x79 ^ ((val1 >> 24) & 0xFF);
		*ecc_code++ = 0xe5 ^ ((val1 >> 16) & 0xFF);
		*ecc_code++ = 0x24 ^ ((val1 >> 8) & 0xFF);
		*ecc_code++ = 0xb5 ^ (val1 & 0xFF);
	}

	return 0;
}

/**
 * omap3_correct_data_bch - Decode received data and correct errors
 * @mtd: MTD device structure
 * @data: page data
 * @read_ecc: ecc read from nand flash
 * @calc_ecc: ecc read from HW ECC registers
 */
static int omap3_correct_data_bch(struct mtd_info *mtd, u_char *data,
				  u_char *read_ecc, u_char *calc_ecc)
{
	int i, count;
	/* cannot correct more than 8 errors */
	unsigned int errloc[8];
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
						   mtd);

	count = decode_bch(info->bch, NULL, 512, read_ecc, calc_ecc, NULL,
			   errloc);
	if (count > 0) {
		/* correct errors */
		for (i = 0; i < count; i++) {
			/* correct data only, not ecc bytes */
			if (errloc[i] < 8*512)
				data[errloc[i]/8] ^= 1 << (errloc[i] & 7);
			pr_debug("corrected bitflip %u\n", errloc[i]);
		}
	} else if (count < 0) {
		pr_err("ecc unrecoverable error\n");
	}
	return count;
}

/**
 * omap3_free_bch - Release BCH ecc resources
 * @mtd: MTD device structure
 */
static void omap3_free_bch(struct mtd_info *mtd)
{
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
						   mtd);
	if (info->bch) {
		free_bch(info->bch);
		info->bch = NULL;
	}
}

/**
 * omap3_init_bch - Initialize BCH ECC
 * @mtd: MTD device structure
 * @ecc_opt: OMAP ECC mode (OMAP_ECC_BCH4_CODE_HW or OMAP_ECC_BCH8_CODE_HW)
 */
static int omap3_init_bch(struct mtd_info *mtd, int ecc_opt)
{
	int max_errors;
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
						   mtd);
#ifdef CONFIG_MTD_NAND_OMAP_BCH8
	const int hw_errors = 8;
#else
	const int hw_errors = 4;
#endif
	info->bch = NULL;

	max_errors = (ecc_opt == OMAP_ECC_BCH8_CODE_HW) ? 8 : 4;
	if (max_errors != hw_errors) {
		pr_err("cannot configure %d-bit BCH ecc, only %d-bit supported",
		       max_errors, hw_errors);
		goto fail;
	}

	/* software bch library is only used to detect and locate errors */
	info->bch = init_bch(13, max_errors, 0x201b /* hw polynomial */);
	if (!info->bch)
		goto fail;

	info->nand.ecc.size    = 512;
	info->nand.ecc.hwctl   = omap3_enable_hwecc_bch;
	info->nand.ecc.correct = omap3_correct_data_bch;
	info->nand.ecc.mode    = NAND_ECC_HW;

	/*
	 * The number of corrected errors in an ecc block that will trigger
	 * block scrubbing defaults to the ecc strength (4 or 8).
	 * Set mtd->bitflip_threshold here to define a custom threshold.
	 */

	if (max_errors == 8) {
		info->nand.ecc.strength  = 8;
		info->nand.ecc.bytes     = 13;
		info->nand.ecc.calculate = omap3_calculate_ecc_bch8;
	} else {
		info->nand.ecc.strength  = 4;
		info->nand.ecc.bytes     = 7;
		info->nand.ecc.calculate = omap3_calculate_ecc_bch4;
	}

	pr_info("enabling NAND BCH ecc with %d-bit correction\n", max_errors);
	return 0;
fail:
	omap3_free_bch(mtd);
	return -1;
}

/**
 * omap3_init_bch_tail - Build an oob layout for BCH ECC correction.
 * @mtd: MTD device structure
 */
static int omap3_init_bch_tail(struct mtd_info *mtd)
{
	int i, steps;
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
						   mtd);
	struct nand_ecclayout *layout = &info->ecclayout;

	/* build oob layout */
	steps = mtd->writesize/info->nand.ecc.size;
	layout->eccbytes = steps*info->nand.ecc.bytes;

	/* do not bother creating special oob layouts for small page devices */
	if (mtd->oobsize < 64) {
		pr_err("BCH ecc is not supported on small page devices\n");
		goto fail;
	}

	/* reserve 2 bytes for bad block marker */
	if (layout->eccbytes+2 > mtd->oobsize) {
		pr_err("no oob layout available for oobsize %d eccbytes %u\n",
		       mtd->oobsize, layout->eccbytes);
		goto fail;
	}

	/* put ecc bytes at oob tail */
	for (i = 0; i < layout->eccbytes; i++)
		layout->eccpos[i] = mtd->oobsize-layout->eccbytes+i;

	layout->oobfree[0].offset = 2;
	layout->oobfree[0].length = mtd->oobsize-2-layout->eccbytes;
	info->nand.ecc.layout = layout;

	if (!(info->nand.options & NAND_BUSWIDTH_16))
		info->nand.badblock_pattern = &bb_descrip_flashbased;
	return 0;
fail:
	omap3_free_bch(mtd);
	return -1;
}

#else
static int omap3_init_bch(struct mtd_info *mtd, int ecc_opt)
{
	pr_err("CONFIG_MTD_NAND_OMAP_BCH is not enabled\n");
	return -1;
}
static int omap3_init_bch_tail(struct mtd_info *mtd)
{
	return -1;
}
static void omap3_free_bch(struct mtd_info *mtd)
{
}
#endif /* CONFIG_MTD_NAND_OMAP_BCH */

static int omap_nand_probe(struct platform_device *pdev)
{
	struct omap_nand_info		*info;
	struct omap_nand_platform_data	*pdata;
	int				err;
	int				i, offset;
	dma_cap_mask_t mask;
	unsigned sig;
	struct resource			*res;
	struct mtd_part_parser_data	ppdata = {};

	pdata = pdev->dev.platform_data;
	if (pdata == NULL) {
		dev_err(&pdev->dev, "platform data missing\n");
		return -ENODEV;
	}

	info = kzalloc(sizeof(struct omap_nand_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);

	spin_lock_init(&info->controller.lock);
	init_waitqueue_head(&info->controller.wq);

	info->pdev = pdev;

	info->gpmc_cs		= pdata->cs;
	info->reg		= pdata->reg;

	info->mtd.priv		= &info->nand;
	info->mtd.name		= dev_name(&pdev->dev);
	info->mtd.owner		= THIS_MODULE;

	info->nand.options	= pdata->devsize;
	info->nand.options	|= NAND_SKIP_BBTSCAN;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		err = -EINVAL;
		dev_err(&pdev->dev, "error getting memory resource\n");
		goto out_free_info;
	}

	info->phys_base = res->start;
	info->mem_size = resource_size(res);

	if (!request_mem_region(info->phys_base, info->mem_size,
				pdev->dev.driver->name)) {
		err = -EBUSY;
		goto out_free_info;
	}

	info->nand.IO_ADDR_R = ioremap(info->phys_base, info->mem_size);
	if (!info->nand.IO_ADDR_R) {
		err = -ENOMEM;
		goto out_release_mem_region;
	}

	info->nand.controller = &info->controller;

	info->nand.IO_ADDR_W = info->nand.IO_ADDR_R;
	info->nand.cmd_ctrl  = omap_hwcontrol;

	/*
	 * If RDY/BSY line is connected to OMAP then use the omap ready
	 * function and the generic nand_wait function which reads the status
	 * register after monitoring the RDY/BSY line. Otherwise use a standard
	 * chip delay which is slightly more than tR (AC Timing) of the NAND
	 * device and read status register until you get a failure or success
	 */
	if (pdata->dev_ready) {
		info->nand.dev_ready = omap_dev_ready;
		info->nand.chip_delay = 0;
	} else {
		info->nand.waitfunc = omap_wait;
		info->nand.chip_delay = 50;
	}

	switch (pdata->xfer_type) {
	case NAND_OMAP_PREFETCH_POLLED:
		info->nand.read_buf   = omap_read_buf_pref;
		info->nand.write_buf  = omap_write_buf_pref;
		break;

	case NAND_OMAP_POLLED:
		if (info->nand.options & NAND_BUSWIDTH_16) {
			info->nand.read_buf   = omap_read_buf16;
			info->nand.write_buf  = omap_write_buf16;
		} else {
			info->nand.read_buf   = omap_read_buf8;
			info->nand.write_buf  = omap_write_buf8;
		}
		break;

	case NAND_OMAP_PREFETCH_DMA:
		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);
		sig = OMAP24XX_DMA_GPMC;
		info->dma = dma_request_channel(mask, omap_dma_filter_fn, &sig);
		if (!info->dma) {
			dev_err(&pdev->dev, "DMA engine request failed\n");
			err = -ENXIO;
			goto out_release_mem_region;
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
				dev_err(&pdev->dev, "DMA engine slave config failed: %d\n",
					err);
				goto out_release_mem_region;
			}
			info->nand.read_buf   = omap_read_buf_dma_pref;
			info->nand.write_buf  = omap_write_buf_dma_pref;
		}
		break;

	case NAND_OMAP_PREFETCH_IRQ:
		info->gpmc_irq_fifo = platform_get_irq(pdev, 0);
		if (info->gpmc_irq_fifo <= 0) {
			dev_err(&pdev->dev, "error getting fifo irq\n");
			err = -ENODEV;
			goto out_release_mem_region;
		}
		err = request_irq(info->gpmc_irq_fifo,	omap_nand_irq,
					IRQF_SHARED, "gpmc-nand-fifo", info);
		if (err) {
			dev_err(&pdev->dev, "requesting irq(%d) error:%d",
						info->gpmc_irq_fifo, err);
			info->gpmc_irq_fifo = 0;
			goto out_release_mem_region;
		}

		info->gpmc_irq_count = platform_get_irq(pdev, 1);
		if (info->gpmc_irq_count <= 0) {
			dev_err(&pdev->dev, "error getting count irq\n");
			err = -ENODEV;
			goto out_release_mem_region;
		}
		err = request_irq(info->gpmc_irq_count,	omap_nand_irq,
					IRQF_SHARED, "gpmc-nand-count", info);
		if (err) {
			dev_err(&pdev->dev, "requesting irq(%d) error:%d",
						info->gpmc_irq_count, err);
			info->gpmc_irq_count = 0;
			goto out_release_mem_region;
		}

		info->nand.read_buf  = omap_read_buf_irq_pref;
		info->nand.write_buf = omap_write_buf_irq_pref;

		break;

	default:
		dev_err(&pdev->dev,
			"xfer_type(%d) not supported!\n", pdata->xfer_type);
		err = -EINVAL;
		goto out_release_mem_region;
	}

	/* select the ecc type */
	if (pdata->ecc_opt == OMAP_ECC_HAMMING_CODE_DEFAULT)
		info->nand.ecc.mode = NAND_ECC_SOFT;
	else if ((pdata->ecc_opt == OMAP_ECC_HAMMING_CODE_HW) ||
		(pdata->ecc_opt == OMAP_ECC_HAMMING_CODE_HW_ROMCODE)) {
		info->nand.ecc.bytes            = 3;
		info->nand.ecc.size             = 512;
		info->nand.ecc.strength         = 1;
		info->nand.ecc.calculate        = omap_calculate_ecc;
		info->nand.ecc.hwctl            = omap_enable_hwecc;
		info->nand.ecc.correct          = omap_correct_data;
		info->nand.ecc.mode             = NAND_ECC_HW;
	} else if ((pdata->ecc_opt == OMAP_ECC_BCH4_CODE_HW) ||
		   (pdata->ecc_opt == OMAP_ECC_BCH8_CODE_HW)) {
		err = omap3_init_bch(&info->mtd, pdata->ecc_opt);
		if (err) {
			err = -EINVAL;
			goto out_release_mem_region;
		}
	}

	/* DIP switches on some boards change between 8 and 16 bit
	 * bus widths for flash.  Try the other width if the first try fails.
	 */
	if (nand_scan_ident(&info->mtd, 1, NULL)) {
		info->nand.options ^= NAND_BUSWIDTH_16;
		if (nand_scan_ident(&info->mtd, 1, NULL)) {
			err = -ENXIO;
			goto out_release_mem_region;
		}
	}

	/* rom code layout */
	if (pdata->ecc_opt == OMAP_ECC_HAMMING_CODE_HW_ROMCODE) {

		if (info->nand.options & NAND_BUSWIDTH_16)
			offset = 2;
		else {
			offset = 1;
			info->nand.badblock_pattern = &bb_descrip_flashbased;
		}
		omap_oobinfo.eccbytes = 3 * (info->mtd.oobsize/16);
		for (i = 0; i < omap_oobinfo.eccbytes; i++)
			omap_oobinfo.eccpos[i] = i+offset;

		omap_oobinfo.oobfree->offset = offset + omap_oobinfo.eccbytes;
		omap_oobinfo.oobfree->length = info->mtd.oobsize -
					(offset + omap_oobinfo.eccbytes);

		info->nand.ecc.layout = &omap_oobinfo;
	} else if ((pdata->ecc_opt == OMAP_ECC_BCH4_CODE_HW) ||
		   (pdata->ecc_opt == OMAP_ECC_BCH8_CODE_HW)) {
		/* build OOB layout for BCH ECC correction */
		err = omap3_init_bch_tail(&info->mtd);
		if (err) {
			err = -EINVAL;
			goto out_release_mem_region;
		}
	}

	/* second phase scan */
	if (nand_scan_tail(&info->mtd)) {
		err = -ENXIO;
		goto out_release_mem_region;
	}

	ppdata.of_node = pdata->of_node;
	mtd_device_parse_register(&info->mtd, NULL, &ppdata, pdata->parts,
				  pdata->nr_parts);

	platform_set_drvdata(pdev, &info->mtd);

	return 0;

out_release_mem_region:
	if (info->dma)
		dma_release_channel(info->dma);
	if (info->gpmc_irq_count > 0)
		free_irq(info->gpmc_irq_count, info);
	if (info->gpmc_irq_fifo > 0)
		free_irq(info->gpmc_irq_fifo, info);
	release_mem_region(info->phys_base, info->mem_size);
out_free_info:
	kfree(info);

	return err;
}

static int omap_nand_remove(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);
	struct omap_nand_info *info = container_of(mtd, struct omap_nand_info,
							mtd);
	omap3_free_bch(&info->mtd);

	platform_set_drvdata(pdev, NULL);
	if (info->dma)
		dma_release_channel(info->dma);

	if (info->gpmc_irq_count > 0)
		free_irq(info->gpmc_irq_count, info);
	if (info->gpmc_irq_fifo > 0)
		free_irq(info->gpmc_irq_fifo, info);

	/* Release NAND device, its internal structures and partitions */
	nand_release(&info->mtd);
	iounmap(info->nand.IO_ADDR_R);
	release_mem_region(info->phys_base, info->mem_size);
	kfree(info);
	return 0;
}

static struct platform_driver omap_nand_driver = {
	.probe		= omap_nand_probe,
	.remove		= omap_nand_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(omap_nand_driver);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Glue layer for NAND flash on TI OMAP boards");
