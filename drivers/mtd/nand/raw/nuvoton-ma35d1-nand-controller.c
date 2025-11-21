// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Nuvoton Technology Corp.
 */
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* NFI Registers */
#define MA35_NFI_REG_DMACTL		0x400
#define   DMA_EN				BIT(0)
#define   DMA_RST				BIT(1)
#define   DMA_BUSY				BIT(9)

#define MA35_NFI_REG_DMASA		0x408
#define MA35_NFI_REG_GCTL		0x800
#define   GRST					BIT(0)
#define   NAND_EN				BIT(3)

#define MA35_NFI_REG_NANDCTL		0x8A0
#define   SWRST				BIT(0)
#define   DMA_R_EN				BIT(1)
#define   DMA_W_EN				BIT(2)
#define   ECC_CHK				BIT(7)
#define   PROT3BEN				BIT(8)
#define   PSIZE_2K				BIT(16)
#define   PSIZE_4K				BIT(17)
#define   PSIZE_8K				GENMASK(17, 16)
#define   PSIZE_MASK				GENMASK(17, 16)
#define   BCH_T24				BIT(18)
#define   BCH_T8				BIT(20)
#define   BCH_T12				BIT(21)
#define   BCH_NONE				(0x0)
#define   BCH_MASK				GENMASK(22, 18)
#define   ECC_EN				BIT(23)
#define   DISABLE_CS0				BIT(25)

#define MA35_NFI_REG_NANDINTEN	0x8A8
#define MA35_NFI_REG_NANDINTSTS	0x8AC
#define   INT_DMA				BIT(0)
#define   INT_ECC				BIT(2)
#define   INT_RB0				BIT(10)

#define MA35_NFI_REG_NANDCMD		0x8B0
#define MA35_NFI_REG_NANDADDR		0x8B4
#define   ENDADDR				BIT(31)

#define MA35_NFI_REG_NANDDATA		0x8B8
#define MA35_NFI_REG_NANDRACTL	0x8BC
#define MA35_NFI_REG_NANDECTL		0x8C0
#define   ENABLE_WP				0x0
#define   DISABLE_WP				BIT(0)

#define MA35_NFI_REG_NANDECCES0	0x8D0
#define   ECC_STATUS_MASK			GENMASK(1, 0)
#define   ECC_ERR_CNT_MASK			GENMASK(4, 0)

#define MA35_NFI_REG_NANDECCEA0	0x900
#define MA35_NFI_REG_NANDECCED0	0x960
#define MA35_NFI_REG_NANDRA0		0xA00

/* Define for the BCH hardware ECC engine */
/* define the total padding bytes for 512/1024 data segment */
#define MA35_BCH_PADDING_512	32
#define MA35_BCH_PADDING_1024	64
/* define the BCH parity code length for 512 bytes data pattern */
#define MA35_PARITY_BCH8	15
#define MA35_PARITY_BCH12	23
/* define the BCH parity code length for 1024 bytes data pattern */
#define MA35_PARITY_BCH24	45

#define MA35_MAX_NSELS		(2)
#define PREFIX_RA_IS_EMPTY(reg)	FIELD_GET(GENMASK(31, 16), (reg))

struct ma35_nand_chip {
	struct list_head node;
	struct nand_chip chip;

	u32 eccstatus;
	u8 nsels;
	u8 sels[] __counted_by(nsels);
};

struct ma35_nand_info {
	struct nand_controller controller;
	struct device *dev;
	void __iomem *regs;
	int irq;
	struct clk *clk;
	struct completion complete;
	struct list_head chips;

	u8 *buffer;
	unsigned long assigned_cs;
};

static inline struct ma35_nand_chip *to_ma35_nand(struct nand_chip *chip)
{
	return container_of(chip, struct ma35_nand_chip, chip);
}

static int ma35_ooblayout_ecc(struct mtd_info *mtd, int section,
			      struct mtd_oob_region *oob_region)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oob_region->length = chip->ecc.total;
	oob_region->offset = mtd->oobsize - oob_region->length;

	return 0;
}

static int ma35_ooblayout_free(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oob_region)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oob_region->length = mtd->oobsize - chip->ecc.total - 2;
	oob_region->offset = 2;

	return 0;
}

static const struct mtd_ooblayout_ops ma35_ooblayout_ops = {
	.free = ma35_ooblayout_free,
	.ecc = ma35_ooblayout_ecc,
};

static inline void ma35_clear_spare(struct nand_chip *chip, int size)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	int i;

	for (i = 0; i < size / 4; i++)
		writel(0xff, nand->regs + MA35_NFI_REG_NANDRA0);
}

static inline void read_remaining_bytes(struct ma35_nand_info *nand, u32 *buf,
					u32 offset, int size, int swap)
{
	u32 value = readl(nand->regs + MA35_NFI_REG_NANDRA0 + offset);
	u8 *ptr = (u8 *)buf;
	int i, shift;

	for (i = 0; i < size; i++) {
		shift = (swap ? 3 - i : i) * 8;
		ptr[i] = (value >> shift) & 0xff;
	}
}

static inline void ma35_read_spare(struct nand_chip *chip, int size, u32 *buf, u32 offset)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	u32 off = round_down(offset, 4);
	int len = offset % 4;
	int i;

	if (len) {
		read_remaining_bytes(nand, buf, off, 4 - len, 1);
		off += 4;
		size -= (4 - len);
	}

	for (i = 0; i < size / 4; i++)
		*buf++ = readl(nand->regs + MA35_NFI_REG_NANDRA0 + off + (i * 4));

	read_remaining_bytes(nand, buf, off + (size & ~3), size % 4, 0);
}

static inline void ma35_write_spare(struct nand_chip *chip, int size, u32 *buf)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	u32 value;
	int i, j;
	u8 *ptr;

	for (i = 0, j = 0; i < size / 4; i++, j += 4)
		writel(*buf++, nand->regs + MA35_NFI_REG_NANDRA0 + j);

	ptr = (u8 *)buf;
	switch (size % 4) {
	case 1:
		writel(*ptr, nand->regs + MA35_NFI_REG_NANDRA0 + j);
		break;
	case 2:
		value = *ptr | (*(ptr + 1) << 8);
		writel(value, nand->regs + MA35_NFI_REG_NANDRA0 + j);
		break;
	case 3:
		value = *ptr | (*(ptr + 1) << 8) | (*(ptr + 2) << 16);
		writel(value, nand->regs + MA35_NFI_REG_NANDRA0 + j);
		break;
	default:
		break;
	}
}

static void ma35_nand_target_enable(struct nand_chip *chip, unsigned int cs)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	u32 reg;

	switch (cs) {
	case 0:
		reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
		writel(reg & ~DISABLE_CS0, nand->regs + MA35_NFI_REG_NANDCTL);

		reg = readl(nand->regs + MA35_NFI_REG_NANDINTSTS);
		reg |= INT_RB0;
		writel(reg, nand->regs + MA35_NFI_REG_NANDINTSTS);
		break;
	default:
		break;
	}
}

static int ma35_nand_hwecc_init(struct nand_chip *chip, struct ma35_nand_info *nand)
{
	struct ma35_nand_chip *nvtnand = to_ma35_nand(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct device *dev = mtd->dev.parent;
	u32 reg;

	nand->buffer = devm_kzalloc(dev, mtd->writesize, GFP_KERNEL);
	if (!nand->buffer)
		return -ENOMEM;

	/* Redundant area size */
	writel(mtd->oobsize, nand->regs + MA35_NFI_REG_NANDRACTL);

	/* Protect redundant 3 bytes and disable ECC engine */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	reg |= (PROT3BEN | ECC_CHK);
	reg &= ~ECC_EN;

	if (chip->ecc.strength != 0) {
		chip->ecc.steps = mtd->writesize / chip->ecc.size;
		nvtnand->eccstatus = (chip->ecc.steps < 4) ? 1 : chip->ecc.steps / 4;
		/* Set BCH algorithm */
		reg &= ~BCH_MASK;
		switch (chip->ecc.strength) {
		case 8:
			chip->ecc.total = chip->ecc.steps * MA35_PARITY_BCH8;
			reg |= BCH_T8;
			break;
		case 12:
			chip->ecc.total = chip->ecc.steps * MA35_PARITY_BCH12;
			reg |= BCH_T12;
			break;
		case 24:
			chip->ecc.total = chip->ecc.steps * MA35_PARITY_BCH24;
			reg |= BCH_T24;
			break;
		default:
			dev_err(nand->dev, "ECC strength unsupported\n");
			return -EINVAL;
		}

		chip->ecc.bytes = chip->ecc.total / chip->ecc.steps;
	}
	writel(reg, nand->regs + MA35_NFI_REG_NANDCTL);
	return 0;
}

/* Correct data by BCH alrogithm */
static void ma35_nfi_correct(struct nand_chip *chip, u8 index,
			     u8 err_cnt, u8 *addr)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	u32 temp_data[24], temp_addr[24];
	u32 padding_len, parity_len;
	u32 value, offset, remain;
	u32 err_data[6];
	u8  i, j;

	/* Configurations */
	if (chip->ecc.strength <= 8) {
		parity_len = MA35_PARITY_BCH8;
		padding_len = MA35_BCH_PADDING_512;
	} else if (chip->ecc.strength <= 12) {
		parity_len = MA35_PARITY_BCH12;
		padding_len = MA35_BCH_PADDING_512;
	} else if (chip->ecc.strength <= 24) {
		parity_len = MA35_PARITY_BCH24;
		padding_len = MA35_BCH_PADDING_1024;
	} else {
		dev_err(nand->dev, "Invalid BCH_TSEL = 0x%lx\n",
			readl(nand->regs + MA35_NFI_REG_NANDCTL) & BCH_MASK);
		return;
	}

	/*
	 * got valid BCH_ECC_DATAx and parse them to temp_data[]
	 * got the valid register number of BCH_ECC_DATAx since
	 * one register include 4 error bytes
	 */
	j = (err_cnt + 3) / 4;
	j = (j > 6) ? 6 : j;
	for (i = 0; i < j; i++)
		err_data[i] = readl(nand->regs + MA35_NFI_REG_NANDECCED0 + i * 4);

	for (i = 0; i < j; i++) {
		temp_data[i * 4 + 0] = err_data[i] & 0xff;
		temp_data[i * 4 + 1] = (err_data[i] >> 8) & 0xff;
		temp_data[i * 4 + 2] = (err_data[i] >> 16) & 0xff;
		temp_data[i * 4 + 3] = (err_data[i] >> 24) & 0xff;
	}

	/*
	 * got valid REG_BCH_ECC_ADDRx and parse them to temp_addr[]
	 * got the valid register number of REG_BCH_ECC_ADDRx since
	 * one register include 2 error addresses
	 */
	j = (err_cnt + 1) / 2;
	j = (j > 12) ? 12 : j;
	for (i = 0; i < j; i++) {
		temp_addr[i * 2 + 0] = readl(nand->regs + MA35_NFI_REG_NANDECCEA0 + i * 4)
					& 0x07ff;
		temp_addr[i * 2 + 1] = (readl(nand->regs + MA35_NFI_REG_NANDECCEA0 + i * 4)
					>> 16) & 0x07ff;
	}

	/* pointer to begin address of field that with data error */
	addr += index * chip->ecc.size;

	/* correct each error bytes */
	for (i = 0; i < err_cnt; i++) {
		u32 corrected_index = temp_addr[i];

		if (corrected_index < chip->ecc.size) {
			/* for wrong data in field */
			*(addr + corrected_index) ^= temp_data[i];
		} else if (corrected_index < (chip->ecc.size + 3)) {
			/* for wrong first-3-bytes in redundancy area */
			corrected_index -= chip->ecc.size;
			temp_addr[i] += (parity_len * index);	/* field offset */

			value = readl(nand->regs + MA35_NFI_REG_NANDRA0);
			value ^= temp_data[i] << (8 * corrected_index);
			writel(value, nand->regs + MA35_NFI_REG_NANDRA0);
		} else {
			/*
			 * for wrong parity code in redundancy area
			 * ERR_ADDRx = [data in field] + [3 bytes] + [xx] + [parity code]
			 *                               |<--     padding bytes      -->|
			 * The ERR_ADDRx for last parity code always = field size + padding size.
			 * The first parity code = field size + padding size - parity code length.
			 * For example, for BCH T12, the first parity code = 512 + 32 - 23 = 521.
			 * That is, error byte address offset within field is
			 */
			corrected_index -= (chip->ecc.size + padding_len - parity_len);

			/*
			 * final address = first parity code of first field +
			 *                 offset of fields +
			 *                 offset within field
			 */
			offset = (readl(nand->regs + MA35_NFI_REG_NANDRACTL) & 0x1ff) -
				(parity_len * chip->ecc.steps) +
				(parity_len * index) + corrected_index;

			remain = offset % 4;
			value = readl(nand->regs + MA35_NFI_REG_NANDRA0 + offset - remain);
			value ^= temp_data[i] << (8 * remain);
			writel(value, nand->regs + MA35_NFI_REG_NANDRA0 + offset - remain);
		}
	}
}

static int ma35_nfi_ecc_check(struct nand_chip *chip, u8 *addr)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct ma35_nand_chip *nvtnand = to_ma35_nand(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int maxbitflips = 0;
	int cnt = 0;
	u32 status;
	int i, j;

	for (j = 0; j < nvtnand->eccstatus; j++) {
		status = readl(nand->regs + MA35_NFI_REG_NANDECCES0 + j * 4);
		if (!status)
			continue;

		for (i = 0; i < 4; i++) {
			if ((status & ECC_STATUS_MASK) == 0x01) {
				/* Correctable error */
				cnt = (status >> 2) & ECC_ERR_CNT_MASK;
				ma35_nfi_correct(chip, j * 4 + i, cnt, addr);
				maxbitflips = max_t(u32, maxbitflips, cnt);
				mtd->ecc_stats.corrected += cnt;
			} else {
				/* Uncorrectable error */
				mtd->ecc_stats.failed++;
				dev_err(nand->dev, "uncorrectable error! 0x%4x\n", status);
				return -EBADMSG;
			}
			status >>= 8;
		}
	}
	return maxbitflips;
}

static void ma35_nand_dmac_init(struct ma35_nand_info *nand)
{
	/* DMAC reset and enable */
	writel(DMA_RST | DMA_EN, nand->regs + MA35_NFI_REG_DMACTL);
	writel(DMA_EN, nand->regs + MA35_NFI_REG_DMACTL);

	/* Clear DMA finished flag and enable */
	writel(INT_DMA | INT_ECC, nand->regs + MA35_NFI_REG_NANDINTSTS);
	writel(INT_DMA, nand->regs + MA35_NFI_REG_NANDINTEN);
}

static int ma35_nand_do_write(struct nand_chip *chip, const u8 *addr, u32 len)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	dma_addr_t dma_addr;
	int ret = 0, i;
	u32 reg;

	if (len != mtd->writesize) {
		for (i = 0; i < len; i++)
			writel(addr[i], nand->regs + MA35_NFI_REG_NANDDATA);
		return 0;
	}

	ma35_nand_dmac_init(nand);

	/* To mark this page as dirty. */
	reg = readl(nand->regs + MA35_NFI_REG_NANDRA0);
	if (reg & 0xffff0000)
		writel(reg & 0xffff, nand->regs + MA35_NFI_REG_NANDRA0);

	dma_addr = dma_map_single(nand->dev, (void *)addr, len, DMA_TO_DEVICE);
	ret = dma_mapping_error(nand->dev, dma_addr);
	if (ret) {
		dev_err(nand->dev, "dma mapping error\n");
		return -EINVAL;
	}
	dma_sync_single_for_device(nand->dev, dma_addr, len, DMA_TO_DEVICE);

	reinit_completion(&nand->complete);
	writel(dma_addr, nand->regs + MA35_NFI_REG_DMASA);
	writel(readl(nand->regs + MA35_NFI_REG_NANDCTL) | DMA_W_EN,
	       nand->regs + MA35_NFI_REG_NANDCTL);
	ret = wait_for_completion_timeout(&nand->complete, msecs_to_jiffies(1000));
	if (!ret) {
		dev_err(nand->dev, "write timeout\n");
		ret = -ETIMEDOUT;
	}

	dma_unmap_single(nand->dev, dma_addr, len, DMA_TO_DEVICE);

	return ret;
}

static int ma35_nand_do_read(struct nand_chip *chip, u8 *addr, u32 len)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret = 0, cnt = 0, i;
	dma_addr_t dma_addr;
	u32 reg;

	if (len != mtd->writesize) {
		for (i = 0; i < len; i++)
			addr[i] = readb(nand->regs + MA35_NFI_REG_NANDDATA);
		return 0;
	}

	ma35_nand_dmac_init(nand);

	/* Setup and start DMA using dma_addr */
	dma_addr = dma_map_single(nand->dev, (void *)addr, len, DMA_FROM_DEVICE);
	ret = dma_mapping_error(nand->dev, dma_addr);
	if (ret) {
		dev_err(nand->dev, "dma mapping error\n");
		return -EINVAL;
	}

	reinit_completion(&nand->complete);
	writel(dma_addr, nand->regs + MA35_NFI_REG_DMASA);
	writel(readl(nand->regs + MA35_NFI_REG_NANDCTL) | DMA_R_EN,
	       nand->regs + MA35_NFI_REG_NANDCTL);
	ret = wait_for_completion_timeout(&nand->complete, msecs_to_jiffies(1000));
	if (!ret) {
		dev_err(nand->dev, "read timeout\n");
		ret = -ETIMEDOUT;
	}

	dma_unmap_single(nand->dev, dma_addr, len, DMA_FROM_DEVICE);

	reg = readl(nand->regs + MA35_NFI_REG_NANDINTSTS);
	if (reg & INT_ECC) {
		cnt = ma35_nfi_ecc_check(chip, addr);
		if (cnt < 0) {
			writel(DMA_RST | DMA_EN, nand->regs + MA35_NFI_REG_DMACTL);
			writel(readl(nand->regs + MA35_NFI_REG_NANDCTL) | SWRST,
			       nand->regs + MA35_NFI_REG_NANDCTL);
		}
		writel(INT_ECC, nand->regs + MA35_NFI_REG_NANDINTSTS);
	}

	ret = ret < 0 ? ret : cnt;
	return ret;
}

static int ma35_nand_format_subpage(struct nand_chip *chip, u32 offset,
				    u32 len, const u8 *buf)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 page_off = round_down(offset, chip->ecc.size);
	u32 end = DIV_ROUND_UP(page_off + len, chip->ecc.size);
	u32 start = page_off / chip->ecc.size;
	u32 reg;
	int i;

	reg = readl(nand->regs + MA35_NFI_REG_NANDRACTL) | 0xffff0000;
	memset(nand->buffer, 0xff, mtd->writesize);
	for (i = start; i < end; i++) {
		memcpy(nand->buffer + i * chip->ecc.size,
		       buf + i * chip->ecc.size, chip->ecc.size);
		reg &= ~(1 << (i + 16));
	}
	writel(reg, nand->regs + MA35_NFI_REG_NANDRACTL);

	return 0;
}

static int ma35_nand_write_subpage_hwecc(struct nand_chip *chip, u32 offset,
					 u32 data_len, const u8 *buf,
					 int oob_required, int page)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 reg, oobpoi, index;
	int i;

	/* Enable HW ECC engine */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	writel(reg | ECC_EN, nand->regs + MA35_NFI_REG_NANDCTL);

	ma35_nand_target_enable(chip, chip->cur_cs);

	ma35_clear_spare(chip, mtd->oobsize);
	ma35_write_spare(chip, mtd->oobsize - chip->ecc.total,
			 (u32 *)chip->oob_poi);

	ma35_nand_format_subpage(chip, offset, data_len, buf);
	nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	ma35_nand_do_write(chip, nand->buffer, mtd->writesize);
	nand_prog_page_end_op(chip);

	oobpoi = mtd->oobsize - chip->ecc.total;
	reg = readl(nand->regs + MA35_NFI_REG_NANDRACTL);
	for (i = 0; i < chip->ecc.steps; i++) {
		index = i * chip->ecc.bytes;
		if (!(reg & (1 << (i + 16)))) {
			ma35_read_spare(chip, chip->ecc.bytes,
					(u32 *)(chip->oob_poi + oobpoi + index),
					oobpoi + index);
		}
	}

	writel(mtd->oobsize, nand->regs + MA35_NFI_REG_NANDRACTL);
	/* Disable HW ECC engine */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	writel(reg & ~ECC_EN, nand->regs + MA35_NFI_REG_NANDCTL);

	return 0;
}

static int ma35_nand_write_page_hwecc(struct nand_chip *chip, const u8 *buf,
				      int oob_required, int page)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 reg;

	/* Enable HW ECC engine */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	writel(reg | ECC_EN, nand->regs + MA35_NFI_REG_NANDCTL);

	ma35_nand_target_enable(chip, chip->cur_cs);

	ma35_clear_spare(chip, mtd->oobsize);
	ma35_write_spare(chip, mtd->oobsize - chip->ecc.total,
			 (u32 *)chip->oob_poi);

	nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	ma35_nand_do_write(chip, buf, mtd->writesize);
	nand_prog_page_end_op(chip);

	ma35_read_spare(chip, chip->ecc.total,
			(u32 *)(chip->oob_poi + (mtd->oobsize - chip->ecc.total)),
			mtd->oobsize - chip->ecc.total);

	/* Disable HW ECC engine */
	writel(reg & ~ECC_EN, nand->regs + MA35_NFI_REG_NANDCTL);

	return 0;
}

static int ma35_nand_read_subpage_hwecc(struct nand_chip *chip, u32 offset,
					u32 data_len, u8 *buf, int page)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int bitflips = 0;
	u32 reg;

	/* Enable HW ECC engine */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	writel(reg | ECC_EN, nand->regs + MA35_NFI_REG_NANDCTL);

	ma35_nand_target_enable(chip, chip->cur_cs);
	nand_read_oob_op(chip, page, 0, chip->oob_poi, mtd->oobsize);
	ma35_write_spare(chip, mtd->oobsize, (u32 *)chip->oob_poi);

	reg = readl(nand->regs + MA35_NFI_REG_NANDRA0);
	if (PREFIX_RA_IS_EMPTY(reg)) {
		memset((void *)buf, 0xff, mtd->writesize);
	} else {
		nand_read_page_op(chip, page, offset, NULL, 0);
		bitflips = ma35_nand_do_read(chip, buf + offset, data_len);
		ma35_read_spare(chip, mtd->oobsize, (u32 *)chip->oob_poi, 0);
	}

	/* Disable HW ECC engine */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	writel(reg & ~ECC_EN, nand->regs + MA35_NFI_REG_NANDCTL);

	return bitflips;
}

static int ma35_nand_read_page_hwecc(struct nand_chip *chip, u8 *buf,
				     int oob_required, int page)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int bitflips = 0;
	u32 reg;

	/* Enable HW ECC engine */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	writel(reg | ECC_EN, nand->regs + MA35_NFI_REG_NANDCTL);

	ma35_nand_target_enable(chip, chip->cur_cs);
	nand_read_oob_op(chip, page, 0, chip->oob_poi, mtd->oobsize);
	ma35_write_spare(chip, mtd->oobsize, (u32 *)chip->oob_poi);

	reg = readl(nand->regs + MA35_NFI_REG_NANDRA0);
	if (PREFIX_RA_IS_EMPTY(reg)) {
		memset((void *)buf, 0xff, mtd->writesize);
	} else {
		nand_read_page_op(chip, page, 0, NULL, 0);
		bitflips = ma35_nand_do_read(chip, buf, mtd->writesize);
		ma35_read_spare(chip, mtd->oobsize, (u32 *)chip->oob_poi, 0);
	}

	/* Disable HW ECC engine */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	writel(reg & ~ECC_EN, nand->regs + MA35_NFI_REG_NANDCTL);

	return bitflips;
}

static int ma35_nand_read_oob_hwecc(struct nand_chip *chip, int page)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 reg;

	ma35_nand_target_enable(chip, chip->cur_cs);
	nand_read_oob_op(chip, page, 0, chip->oob_poi, mtd->oobsize);

	/* copy OOB data to controller redundant area for page read */
	ma35_write_spare(chip, mtd->oobsize, (u32 *)chip->oob_poi);

	reg = readl(nand->regs + MA35_NFI_REG_NANDRA0);
	if (PREFIX_RA_IS_EMPTY(reg))
		memset((void *)chip->oob_poi, 0xff, mtd->oobsize);

	return 0;
}

static inline void ma35_hw_init(struct ma35_nand_info *nand)
{
	u32 reg;

	/* Disable flash wp. */
	writel(DISABLE_WP, nand->regs + MA35_NFI_REG_NANDECTL);

	/* resets the internal state machine and counters */
	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL);
	reg |= SWRST;
	writel(reg, nand->regs + MA35_NFI_REG_NANDCTL);
}

static irqreturn_t ma35_nand_irq(int irq, void *id)
{
	struct ma35_nand_info *nand = (struct ma35_nand_info *)id;
	u32 isr;

	isr = readl(nand->regs + MA35_NFI_REG_NANDINTSTS);
	if (isr & INT_DMA) {
		writel(INT_DMA, nand->regs + MA35_NFI_REG_NANDINTSTS);
		complete(&nand->complete);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int ma35_nand_attach_chip(struct nand_chip *chip)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct device *dev = mtd->dev.parent;
	u32 reg;

	if (chip->options & NAND_BUSWIDTH_16) {
		dev_err(dev, "16 bits bus width not supported");
		return -EINVAL;
	}

	reg = readl(nand->regs + MA35_NFI_REG_NANDCTL) & (~PSIZE_MASK);
	switch (mtd->writesize) {
	case SZ_2K:
		writel(reg | PSIZE_2K, nand->regs + MA35_NFI_REG_NANDCTL);
		break;
	case SZ_4K:
		writel(reg | PSIZE_4K, nand->regs + MA35_NFI_REG_NANDCTL);
		break;
	case SZ_8K:
		writel(reg | PSIZE_8K, nand->regs + MA35_NFI_REG_NANDCTL);
		break;
	default:
		dev_err(dev, "Unsupported page size");
		return -EINVAL;
	}

	switch (chip->ecc.engine_type) {
	case NAND_ECC_ENGINE_TYPE_ON_HOST:
		/* Do not store BBT bits in the OOB section as it is not protected */
		if (chip->bbt_options & NAND_BBT_USE_FLASH)
			chip->bbt_options |= NAND_BBT_NO_OOB;
		chip->options |= NAND_USES_DMA | NAND_SUBPAGE_READ;
		chip->ecc.write_subpage = ma35_nand_write_subpage_hwecc;
		chip->ecc.write_page = ma35_nand_write_page_hwecc;
		chip->ecc.read_subpage = ma35_nand_read_subpage_hwecc;
		chip->ecc.read_page  = ma35_nand_read_page_hwecc;
		chip->ecc.read_oob   = ma35_nand_read_oob_hwecc;
		return ma35_nand_hwecc_init(chip, nand);
	case NAND_ECC_ENGINE_TYPE_NONE:
	case NAND_ECC_ENGINE_TYPE_SOFT:
	case NAND_ECC_ENGINE_TYPE_ON_DIE:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ma35_nfc_exec_instr(struct nand_chip *chip,
			       const struct nand_op_instr *instr)
{
	struct ma35_nand_info *nand = nand_get_controller_data(chip);
	unsigned int i;
	int ret = 0;
	u32 status;

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		writel(instr->ctx.cmd.opcode, nand->regs + MA35_NFI_REG_NANDCMD);
		break;
	case NAND_OP_ADDR_INSTR:
		for (i = 0; i < instr->ctx.addr.naddrs; i++) {
			if (i == (instr->ctx.addr.naddrs - 1))
				writel(instr->ctx.addr.addrs[i] | ENDADDR,
				       nand->regs + MA35_NFI_REG_NANDADDR);
			else
				writel(instr->ctx.addr.addrs[i],
				       nand->regs + MA35_NFI_REG_NANDADDR);
		}
		break;
	case NAND_OP_DATA_IN_INSTR:
		ret = ma35_nand_do_read(chip, instr->ctx.data.buf.in, instr->ctx.data.len);
		break;
	case NAND_OP_DATA_OUT_INSTR:
		ret = ma35_nand_do_write(chip, instr->ctx.data.buf.out, instr->ctx.data.len);
		break;
	case NAND_OP_WAITRDY_INSTR:
		return readl_poll_timeout(nand->regs + MA35_NFI_REG_NANDINTSTS, status,
					  status & INT_RB0, 20,
					  instr->ctx.waitrdy.timeout_ms * MSEC_PER_SEC);
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ma35_nfc_exec_op(struct nand_chip *chip,
			    const struct nand_operation *op,
			    bool check_only)
{
	int ret = 0;
	u32 i;

	if (check_only)
		return 0;

	ma35_nand_target_enable(chip, op->cs);

	for (i = 0; i < op->ninstrs; i++) {
		ret = ma35_nfc_exec_instr(chip, &op->instrs[i]);
		if (ret)
			break;
	}

	return ret;
}

static const struct nand_controller_ops ma35_nfc_ops = {
	.attach_chip = ma35_nand_attach_chip,
	.exec_op = ma35_nfc_exec_op,
};

static int ma35_nand_chip_init(struct device *dev, struct ma35_nand_info *nand,
			       struct device_node *np)
{
	struct ma35_nand_chip *nvtnand;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	int nsels;
	int ret;
	u32 cs;
	int i;

	nsels = of_property_count_elems_of_size(np, "reg", sizeof(u32));
	if (!nsels || nsels > MA35_MAX_NSELS) {
		dev_err(dev, "invalid reg property size %d\n", nsels);
		return -EINVAL;
	}

	nvtnand = devm_kzalloc(dev, struct_size(nvtnand, sels, nsels),
			       GFP_KERNEL);
	if (!nvtnand)
		return -ENOMEM;

	nvtnand->nsels = nsels;
	for (i = 0; i < nsels; i++) {
		ret = of_property_read_u32_index(np, "reg", i, &cs);
		if (ret) {
			dev_err(dev, "reg property failure : %d\n", ret);
			return ret;
		}

		if (cs >= MA35_MAX_NSELS) {
			dev_err(dev, "invalid CS: %u\n", cs);
			return -EINVAL;
		}

		if (test_and_set_bit(cs, &nand->assigned_cs)) {
			dev_err(dev, "CS %u already assigned\n", cs);
			return -EINVAL;
		}

		nvtnand->sels[i] = cs;
	}

	chip = &nvtnand->chip;
	chip->controller = &nand->controller;

	nand_set_flash_node(chip, np);
	nand_set_controller_data(chip, nand);

	mtd = nand_to_mtd(chip);
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;

	mtd_set_ooblayout(mtd, &ma35_ooblayout_ops);
	ret = nand_scan(chip, nsels);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		nand_cleanup(chip);
		return ret;
	}

	list_add_tail(&nvtnand->node, &nand->chips);

	return 0;
}

static void ma35_chips_cleanup(struct ma35_nand_info *nand)
{
	struct ma35_nand_chip *nvtnand, *tmp;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry_safe(nvtnand, tmp, &nand->chips, node) {
		chip = &nvtnand->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&nvtnand->node);
	}
}

static int ma35_nand_chips_init(struct device *dev, struct ma35_nand_info *nand)
{
	struct device_node *np = dev->of_node;
	int ret;

	for_each_child_of_node_scoped(np, nand_np) {
		ret = ma35_nand_chip_init(dev, nand, nand_np);
		if (ret) {
			ma35_chips_cleanup(nand);
			return ret;
		}
	}
	return 0;
}

static int ma35_nand_probe(struct platform_device *pdev)
{
	struct ma35_nand_info *nand;
	int ret = 0;

	nand = devm_kzalloc(&pdev->dev, sizeof(*nand), GFP_KERNEL);
	if (!nand)
		return -ENOMEM;

	nand_controller_init(&nand->controller);
	INIT_LIST_HEAD(&nand->chips);
	nand->controller.ops = &ma35_nfc_ops;

	init_completion(&nand->complete);

	nand->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nand->regs))
		return PTR_ERR(nand->regs);

	nand->dev = &pdev->dev;

	nand->clk = devm_clk_get_enabled(&pdev->dev, "nand_gate");
	if (IS_ERR(nand->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(nand->clk),
				     "failed to find NAND clock\n");

	nand->irq = platform_get_irq(pdev, 0);
	if (nand->irq < 0)
		return dev_err_probe(&pdev->dev, nand->irq,
				     "failed to get platform irq\n");

	ret = devm_request_irq(&pdev->dev, nand->irq, ma35_nand_irq,
			       IRQF_TRIGGER_HIGH, "ma35d1-nand-controller", nand);
	if (ret) {
		dev_err(&pdev->dev, "failed to request NAND irq\n");
		return -ENXIO;
	}

	platform_set_drvdata(pdev, nand);

	writel(GRST | NAND_EN, nand->regs + MA35_NFI_REG_GCTL);
	ma35_hw_init(nand);
	ret = ma35_nand_chips_init(&pdev->dev, nand);
	if (ret) {
		dev_err(&pdev->dev, "failed to init NAND chips\n");
		clk_disable(nand->clk);
		return ret;
	}

	return ret;
}

static void ma35_nand_remove(struct platform_device *pdev)
{
	struct ma35_nand_info *nand = platform_get_drvdata(pdev);

	ma35_chips_cleanup(nand);
}

static const struct of_device_id ma35_nand_of_match[] = {
	{ .compatible = "nuvoton,ma35d1-nand-controller" },
	{},
};
MODULE_DEVICE_TABLE(of, ma35_nand_of_match);

static struct platform_driver ma35_nand_driver = {
	.driver = {
		.name = "ma35d1-nand-controller",
		.of_match_table = ma35_nand_of_match,
	},
	.probe = ma35_nand_probe,
	.remove = ma35_nand_remove,
};

module_platform_driver(ma35_nand_driver);

MODULE_DESCRIPTION("Nuvoton ma35 NAND driver");
MODULE_AUTHOR("Hui-Ping Chen <hpchen0nvt@gmail.com>");
MODULE_LICENSE("GPL");
