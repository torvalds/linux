// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S3C64XX/S5PC1XX OneNAND driver
 *
 *  Copyright © 2008-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Implementation:
 *	S3C64XX: emulate the pseudo BufferRAM
 *	S5PC110: use DMA
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include "samsung.h"

enum soc_type {
	TYPE_S3C6400,
	TYPE_S3C6410,
	TYPE_S5PC110,
};

#define ONENAND_ERASE_STATUS		0x00
#define ONENAND_MULTI_ERASE_SET		0x01
#define ONENAND_ERASE_START		0x03
#define ONENAND_UNLOCK_START		0x08
#define ONENAND_UNLOCK_END		0x09
#define ONENAND_LOCK_START		0x0A
#define ONENAND_LOCK_END		0x0B
#define ONENAND_LOCK_TIGHT_START	0x0C
#define ONENAND_LOCK_TIGHT_END		0x0D
#define ONENAND_UNLOCK_ALL		0x0E
#define ONENAND_OTP_ACCESS		0x12
#define ONENAND_SPARE_ACCESS_ONLY	0x13
#define ONENAND_MAIN_ACCESS_ONLY	0x14
#define ONENAND_ERASE_VERIFY		0x15
#define ONENAND_MAIN_SPARE_ACCESS	0x16
#define ONENAND_PIPELINE_READ		0x4000

#define MAP_00				(0x0)
#define MAP_01				(0x1)
#define MAP_10				(0x2)
#define MAP_11				(0x3)

#define S3C64XX_CMD_MAP_SHIFT		24

#define S3C6400_FBA_SHIFT		10
#define S3C6400_FPA_SHIFT		4
#define S3C6400_FSA_SHIFT		2

#define S3C6410_FBA_SHIFT		12
#define S3C6410_FPA_SHIFT		6
#define S3C6410_FSA_SHIFT		4

/* S5PC110 specific definitions */
#define S5PC110_DMA_SRC_ADDR		0x400
#define S5PC110_DMA_SRC_CFG		0x404
#define S5PC110_DMA_DST_ADDR		0x408
#define S5PC110_DMA_DST_CFG		0x40C
#define S5PC110_DMA_TRANS_SIZE		0x414
#define S5PC110_DMA_TRANS_CMD		0x418
#define S5PC110_DMA_TRANS_STATUS	0x41C
#define S5PC110_DMA_TRANS_DIR		0x420
#define S5PC110_INTC_DMA_CLR		0x1004
#define S5PC110_INTC_ONENAND_CLR	0x1008
#define S5PC110_INTC_DMA_MASK		0x1024
#define S5PC110_INTC_ONENAND_MASK	0x1028
#define S5PC110_INTC_DMA_PEND		0x1044
#define S5PC110_INTC_ONENAND_PEND	0x1048
#define S5PC110_INTC_DMA_STATUS		0x1064
#define S5PC110_INTC_ONENAND_STATUS	0x1068

#define S5PC110_INTC_DMA_TD		(1 << 24)
#define S5PC110_INTC_DMA_TE		(1 << 16)

#define S5PC110_DMA_CFG_SINGLE		(0x0 << 16)
#define S5PC110_DMA_CFG_4BURST		(0x2 << 16)
#define S5PC110_DMA_CFG_8BURST		(0x3 << 16)
#define S5PC110_DMA_CFG_16BURST		(0x4 << 16)

#define S5PC110_DMA_CFG_INC		(0x0 << 8)
#define S5PC110_DMA_CFG_CNT		(0x1 << 8)

#define S5PC110_DMA_CFG_8BIT		(0x0 << 0)
#define S5PC110_DMA_CFG_16BIT		(0x1 << 0)
#define S5PC110_DMA_CFG_32BIT		(0x2 << 0)

#define S5PC110_DMA_SRC_CFG_READ	(S5PC110_DMA_CFG_16BURST | \
					S5PC110_DMA_CFG_INC | \
					S5PC110_DMA_CFG_16BIT)
#define S5PC110_DMA_DST_CFG_READ	(S5PC110_DMA_CFG_16BURST | \
					S5PC110_DMA_CFG_INC | \
					S5PC110_DMA_CFG_32BIT)
#define S5PC110_DMA_SRC_CFG_WRITE	(S5PC110_DMA_CFG_16BURST | \
					S5PC110_DMA_CFG_INC | \
					S5PC110_DMA_CFG_32BIT)
#define S5PC110_DMA_DST_CFG_WRITE	(S5PC110_DMA_CFG_16BURST | \
					S5PC110_DMA_CFG_INC | \
					S5PC110_DMA_CFG_16BIT)

#define S5PC110_DMA_TRANS_CMD_TDC	(0x1 << 18)
#define S5PC110_DMA_TRANS_CMD_TEC	(0x1 << 16)
#define S5PC110_DMA_TRANS_CMD_TR	(0x1 << 0)

#define S5PC110_DMA_TRANS_STATUS_TD	(0x1 << 18)
#define S5PC110_DMA_TRANS_STATUS_TB	(0x1 << 17)
#define S5PC110_DMA_TRANS_STATUS_TE	(0x1 << 16)

#define S5PC110_DMA_DIR_READ		0x0
#define S5PC110_DMA_DIR_WRITE		0x1

struct s3c_onenand {
	struct mtd_info	*mtd;
	struct platform_device	*pdev;
	enum soc_type	type;
	void __iomem	*base;
	void __iomem	*ahb_addr;
	int		bootram_command;
	void		*page_buf;
	void		*oob_buf;
	unsigned int	(*mem_addr)(int fba, int fpa, int fsa);
	unsigned int	(*cmd_map)(unsigned int type, unsigned int val);
	void __iomem	*dma_addr;
	unsigned long	phys_base;
	struct completion	complete;
};

#define CMD_MAP_00(dev, addr)		(dev->cmd_map(MAP_00, ((addr) << 1)))
#define CMD_MAP_01(dev, mem_addr)	(dev->cmd_map(MAP_01, (mem_addr)))
#define CMD_MAP_10(dev, mem_addr)	(dev->cmd_map(MAP_10, (mem_addr)))
#define CMD_MAP_11(dev, addr)		(dev->cmd_map(MAP_11, ((addr) << 2)))

static struct s3c_onenand *onenand;

static inline int s3c_read_reg(int offset)
{
	return readl(onenand->base + offset);
}

static inline void s3c_write_reg(int value, int offset)
{
	writel(value, onenand->base + offset);
}

static inline int s3c_read_cmd(unsigned int cmd)
{
	return readl(onenand->ahb_addr + cmd);
}

static inline void s3c_write_cmd(int value, unsigned int cmd)
{
	writel(value, onenand->ahb_addr + cmd);
}

#ifdef SAMSUNG_DEBUG
static void s3c_dump_reg(void)
{
	int i;

	for (i = 0; i < 0x400; i += 0x40) {
		printk(KERN_INFO "0x%08X: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			(unsigned int) onenand->base + i,
			s3c_read_reg(i), s3c_read_reg(i + 0x10),
			s3c_read_reg(i + 0x20), s3c_read_reg(i + 0x30));
	}
}
#endif

static unsigned int s3c64xx_cmd_map(unsigned type, unsigned val)
{
	return (type << S3C64XX_CMD_MAP_SHIFT) | val;
}

static unsigned int s3c6400_mem_addr(int fba, int fpa, int fsa)
{
	return (fba << S3C6400_FBA_SHIFT) | (fpa << S3C6400_FPA_SHIFT) |
		(fsa << S3C6400_FSA_SHIFT);
}

static unsigned int s3c6410_mem_addr(int fba, int fpa, int fsa)
{
	return (fba << S3C6410_FBA_SHIFT) | (fpa << S3C6410_FPA_SHIFT) |
		(fsa << S3C6410_FSA_SHIFT);
}

static void s3c_onenand_reset(void)
{
	unsigned long timeout = 0x10000;
	int stat;

	s3c_write_reg(ONENAND_MEM_RESET_COLD, MEM_RESET_OFFSET);
	while (1 && timeout--) {
		stat = s3c_read_reg(INT_ERR_STAT_OFFSET);
		if (stat & RST_CMP)
			break;
	}
	stat = s3c_read_reg(INT_ERR_STAT_OFFSET);
	s3c_write_reg(stat, INT_ERR_ACK_OFFSET);

	/* Clear interrupt */
	s3c_write_reg(0x0, INT_ERR_ACK_OFFSET);
	/* Clear the ECC status */
	s3c_write_reg(0x0, ECC_ERR_STAT_OFFSET);
}

static unsigned short s3c_onenand_readw(void __iomem *addr)
{
	struct onenand_chip *this = onenand->mtd->priv;
	struct device *dev = &onenand->pdev->dev;
	int reg = addr - this->base;
	int word_addr = reg >> 1;
	int value;

	/* It's used for probing time */
	switch (reg) {
	case ONENAND_REG_MANUFACTURER_ID:
		return s3c_read_reg(MANUFACT_ID_OFFSET);
	case ONENAND_REG_DEVICE_ID:
		return s3c_read_reg(DEVICE_ID_OFFSET);
	case ONENAND_REG_VERSION_ID:
		return s3c_read_reg(FLASH_VER_ID_OFFSET);
	case ONENAND_REG_DATA_BUFFER_SIZE:
		return s3c_read_reg(DATA_BUF_SIZE_OFFSET);
	case ONENAND_REG_TECHNOLOGY:
		return s3c_read_reg(TECH_OFFSET);
	case ONENAND_REG_SYS_CFG1:
		return s3c_read_reg(MEM_CFG_OFFSET);

	/* Used at unlock all status */
	case ONENAND_REG_CTRL_STATUS:
		return 0;

	case ONENAND_REG_WP_STATUS:
		return ONENAND_WP_US;

	default:
		break;
	}

	/* BootRAM access control */
	if ((unsigned long)addr < ONENAND_DATARAM && onenand->bootram_command) {
		if (word_addr == 0)
			return s3c_read_reg(MANUFACT_ID_OFFSET);
		if (word_addr == 1)
			return s3c_read_reg(DEVICE_ID_OFFSET);
		if (word_addr == 2)
			return s3c_read_reg(FLASH_VER_ID_OFFSET);
	}

	value = s3c_read_cmd(CMD_MAP_11(onenand, word_addr)) & 0xffff;
	dev_info(dev, "%s: Illegal access at reg 0x%x, value 0x%x\n", __func__,
		 word_addr, value);
	return value;
}

static void s3c_onenand_writew(unsigned short value, void __iomem *addr)
{
	struct onenand_chip *this = onenand->mtd->priv;
	struct device *dev = &onenand->pdev->dev;
	unsigned int reg = addr - this->base;
	unsigned int word_addr = reg >> 1;

	/* It's used for probing time */
	switch (reg) {
	case ONENAND_REG_SYS_CFG1:
		s3c_write_reg(value, MEM_CFG_OFFSET);
		return;

	case ONENAND_REG_START_ADDRESS1:
	case ONENAND_REG_START_ADDRESS2:
		return;

	/* Lock/lock-tight/unlock/unlock_all */
	case ONENAND_REG_START_BLOCK_ADDRESS:
		return;

	default:
		break;
	}

	/* BootRAM access control */
	if ((unsigned long)addr < ONENAND_DATARAM) {
		if (value == ONENAND_CMD_READID) {
			onenand->bootram_command = 1;
			return;
		}
		if (value == ONENAND_CMD_RESET) {
			s3c_write_reg(ONENAND_MEM_RESET_COLD, MEM_RESET_OFFSET);
			onenand->bootram_command = 0;
			return;
		}
	}

	dev_info(dev, "%s: Illegal access at reg 0x%x, value 0x%x\n", __func__,
		 word_addr, value);

	s3c_write_cmd(value, CMD_MAP_11(onenand, word_addr));
}

static int s3c_onenand_wait(struct mtd_info *mtd, int state)
{
	struct device *dev = &onenand->pdev->dev;
	unsigned int flags = INT_ACT;
	unsigned int stat, ecc;
	unsigned long timeout;

	switch (state) {
	case FL_READING:
		flags |= BLK_RW_CMP | LOAD_CMP;
		break;
	case FL_WRITING:
		flags |= BLK_RW_CMP | PGM_CMP;
		break;
	case FL_ERASING:
		flags |= BLK_RW_CMP | ERS_CMP;
		break;
	case FL_LOCKING:
		flags |= BLK_RW_CMP;
		break;
	default:
		break;
	}

	/* The 20 msec is enough */
	timeout = jiffies + msecs_to_jiffies(20);
	while (time_before(jiffies, timeout)) {
		stat = s3c_read_reg(INT_ERR_STAT_OFFSET);
		if (stat & flags)
			break;

		if (state != FL_READING)
			cond_resched();
	}
	/* To get correct interrupt status in timeout case */
	stat = s3c_read_reg(INT_ERR_STAT_OFFSET);
	s3c_write_reg(stat, INT_ERR_ACK_OFFSET);

	/*
	 * In the Spec. it checks the controller status first
	 * However if you get the correct information in case of
	 * power off recovery (POR) test, it should read ECC status first
	 */
	if (stat & LOAD_CMP) {
		ecc = s3c_read_reg(ECC_ERR_STAT_OFFSET);
		if (ecc & ONENAND_ECC_4BIT_UNCORRECTABLE) {
			dev_info(dev, "%s: ECC error = 0x%04x\n", __func__,
				 ecc);
			mtd->ecc_stats.failed++;
			return -EBADMSG;
		}
	}

	if (stat & (LOCKED_BLK | ERS_FAIL | PGM_FAIL | LD_FAIL_ECC_ERR)) {
		dev_info(dev, "%s: controller error = 0x%04x\n", __func__,
			 stat);
		if (stat & LOCKED_BLK)
			dev_info(dev, "%s: it's locked error = 0x%04x\n",
				 __func__, stat);

		return -EIO;
	}

	return 0;
}

static int s3c_onenand_command(struct mtd_info *mtd, int cmd, loff_t addr,
			       size_t len)
{
	struct onenand_chip *this = mtd->priv;
	unsigned int *m, *s;
	int fba, fpa, fsa = 0;
	unsigned int mem_addr, cmd_map_01, cmd_map_10;
	int i, mcount, scount;
	int index;

	fba = (int) (addr >> this->erase_shift);
	fpa = (int) (addr >> this->page_shift);
	fpa &= this->page_mask;

	mem_addr = onenand->mem_addr(fba, fpa, fsa);
	cmd_map_01 = CMD_MAP_01(onenand, mem_addr);
	cmd_map_10 = CMD_MAP_10(onenand, mem_addr);

	switch (cmd) {
	case ONENAND_CMD_READ:
	case ONENAND_CMD_READOOB:
	case ONENAND_CMD_BUFFERRAM:
		ONENAND_SET_NEXT_BUFFERRAM(this);
	default:
		break;
	}

	index = ONENAND_CURRENT_BUFFERRAM(this);

	/*
	 * Emulate Two BufferRAMs and access with 4 bytes pointer
	 */
	m = onenand->page_buf;
	s = onenand->oob_buf;

	if (index) {
		m += (this->writesize >> 2);
		s += (mtd->oobsize >> 2);
	}

	mcount = mtd->writesize >> 2;
	scount = mtd->oobsize >> 2;

	switch (cmd) {
	case ONENAND_CMD_READ:
		/* Main */
		for (i = 0; i < mcount; i++)
			*m++ = s3c_read_cmd(cmd_map_01);
		return 0;

	case ONENAND_CMD_READOOB:
		s3c_write_reg(TSRF, TRANS_SPARE_OFFSET);
		/* Main */
		for (i = 0; i < mcount; i++)
			*m++ = s3c_read_cmd(cmd_map_01);

		/* Spare */
		for (i = 0; i < scount; i++)
			*s++ = s3c_read_cmd(cmd_map_01);

		s3c_write_reg(0, TRANS_SPARE_OFFSET);
		return 0;

	case ONENAND_CMD_PROG:
		/* Main */
		for (i = 0; i < mcount; i++)
			s3c_write_cmd(*m++, cmd_map_01);
		return 0;

	case ONENAND_CMD_PROGOOB:
		s3c_write_reg(TSRF, TRANS_SPARE_OFFSET);

		/* Main - dummy write */
		for (i = 0; i < mcount; i++)
			s3c_write_cmd(0xffffffff, cmd_map_01);

		/* Spare */
		for (i = 0; i < scount; i++)
			s3c_write_cmd(*s++, cmd_map_01);

		s3c_write_reg(0, TRANS_SPARE_OFFSET);
		return 0;

	case ONENAND_CMD_UNLOCK_ALL:
		s3c_write_cmd(ONENAND_UNLOCK_ALL, cmd_map_10);
		return 0;

	case ONENAND_CMD_ERASE:
		s3c_write_cmd(ONENAND_ERASE_START, cmd_map_10);
		return 0;

	default:
		break;
	}

	return 0;
}

static unsigned char *s3c_get_bufferram(struct mtd_info *mtd, int area)
{
	struct onenand_chip *this = mtd->priv;
	int index = ONENAND_CURRENT_BUFFERRAM(this);
	unsigned char *p;

	if (area == ONENAND_DATARAM) {
		p = onenand->page_buf;
		if (index == 1)
			p += this->writesize;
	} else {
		p = onenand->oob_buf;
		if (index == 1)
			p += mtd->oobsize;
	}

	return p;
}

static int onenand_read_bufferram(struct mtd_info *mtd, int area,
				  unsigned char *buffer, int offset,
				  size_t count)
{
	unsigned char *p;

	p = s3c_get_bufferram(mtd, area);
	memcpy(buffer, p + offset, count);
	return 0;
}

static int onenand_write_bufferram(struct mtd_info *mtd, int area,
				   const unsigned char *buffer, int offset,
				   size_t count)
{
	unsigned char *p;

	p = s3c_get_bufferram(mtd, area);
	memcpy(p + offset, buffer, count);
	return 0;
}

static int (*s5pc110_dma_ops)(dma_addr_t dst, dma_addr_t src, size_t count, int direction);

static int s5pc110_dma_poll(dma_addr_t dst, dma_addr_t src, size_t count, int direction)
{
	void __iomem *base = onenand->dma_addr;
	int status;
	unsigned long timeout;

	writel(src, base + S5PC110_DMA_SRC_ADDR);
	writel(dst, base + S5PC110_DMA_DST_ADDR);

	if (direction == S5PC110_DMA_DIR_READ) {
		writel(S5PC110_DMA_SRC_CFG_READ, base + S5PC110_DMA_SRC_CFG);
		writel(S5PC110_DMA_DST_CFG_READ, base + S5PC110_DMA_DST_CFG);
	} else {
		writel(S5PC110_DMA_SRC_CFG_WRITE, base + S5PC110_DMA_SRC_CFG);
		writel(S5PC110_DMA_DST_CFG_WRITE, base + S5PC110_DMA_DST_CFG);
	}

	writel(count, base + S5PC110_DMA_TRANS_SIZE);
	writel(direction, base + S5PC110_DMA_TRANS_DIR);

	writel(S5PC110_DMA_TRANS_CMD_TR, base + S5PC110_DMA_TRANS_CMD);

	/*
	 * There's no exact timeout values at Spec.
	 * In real case it takes under 1 msec.
	 * So 20 msecs are enough.
	 */
	timeout = jiffies + msecs_to_jiffies(20);

	do {
		status = readl(base + S5PC110_DMA_TRANS_STATUS);
		if (status & S5PC110_DMA_TRANS_STATUS_TE) {
			writel(S5PC110_DMA_TRANS_CMD_TEC,
					base + S5PC110_DMA_TRANS_CMD);
			return -EIO;
		}
	} while (!(status & S5PC110_DMA_TRANS_STATUS_TD) &&
		time_before(jiffies, timeout));

	writel(S5PC110_DMA_TRANS_CMD_TDC, base + S5PC110_DMA_TRANS_CMD);

	return 0;
}

static irqreturn_t s5pc110_onenand_irq(int irq, void *data)
{
	void __iomem *base = onenand->dma_addr;
	int status, cmd = 0;

	status = readl(base + S5PC110_INTC_DMA_STATUS);

	if (likely(status & S5PC110_INTC_DMA_TD))
		cmd = S5PC110_DMA_TRANS_CMD_TDC;

	if (unlikely(status & S5PC110_INTC_DMA_TE))
		cmd = S5PC110_DMA_TRANS_CMD_TEC;

	writel(cmd, base + S5PC110_DMA_TRANS_CMD);
	writel(status, base + S5PC110_INTC_DMA_CLR);

	if (!onenand->complete.done)
		complete(&onenand->complete);

	return IRQ_HANDLED;
}

static int s5pc110_dma_irq(dma_addr_t dst, dma_addr_t src, size_t count, int direction)
{
	void __iomem *base = onenand->dma_addr;
	int status;

	status = readl(base + S5PC110_INTC_DMA_MASK);
	if (status) {
		status &= ~(S5PC110_INTC_DMA_TD | S5PC110_INTC_DMA_TE);
		writel(status, base + S5PC110_INTC_DMA_MASK);
	}

	writel(src, base + S5PC110_DMA_SRC_ADDR);
	writel(dst, base + S5PC110_DMA_DST_ADDR);

	if (direction == S5PC110_DMA_DIR_READ) {
		writel(S5PC110_DMA_SRC_CFG_READ, base + S5PC110_DMA_SRC_CFG);
		writel(S5PC110_DMA_DST_CFG_READ, base + S5PC110_DMA_DST_CFG);
	} else {
		writel(S5PC110_DMA_SRC_CFG_WRITE, base + S5PC110_DMA_SRC_CFG);
		writel(S5PC110_DMA_DST_CFG_WRITE, base + S5PC110_DMA_DST_CFG);
	}

	writel(count, base + S5PC110_DMA_TRANS_SIZE);
	writel(direction, base + S5PC110_DMA_TRANS_DIR);

	writel(S5PC110_DMA_TRANS_CMD_TR, base + S5PC110_DMA_TRANS_CMD);

	wait_for_completion_timeout(&onenand->complete, msecs_to_jiffies(20));

	return 0;
}

static int s5pc110_read_bufferram(struct mtd_info *mtd, int area,
		unsigned char *buffer, int offset, size_t count)
{
	struct onenand_chip *this = mtd->priv;
	void __iomem *p;
	void *buf = (void *) buffer;
	dma_addr_t dma_src, dma_dst;
	int err, ofs, page_dma = 0;
	struct device *dev = &onenand->pdev->dev;

	p = this->base + area;
	if (ONENAND_CURRENT_BUFFERRAM(this)) {
		if (area == ONENAND_DATARAM)
			p += this->writesize;
		else
			p += mtd->oobsize;
	}

	if (offset & 3 || (size_t) buf & 3 ||
		!onenand->dma_addr || count != mtd->writesize)
		goto normal;

	/* Handle vmalloc address */
	if (buf >= high_memory) {
		struct page *page;

		if (((size_t) buf & PAGE_MASK) !=
		    ((size_t) (buf + count - 1) & PAGE_MASK))
			goto normal;
		page = vmalloc_to_page(buf);
		if (!page)
			goto normal;

		/* Page offset */
		ofs = ((size_t) buf & ~PAGE_MASK);
		page_dma = 1;

		/* DMA routine */
		dma_src = onenand->phys_base + (p - this->base);
		dma_dst = dma_map_page(dev, page, ofs, count, DMA_FROM_DEVICE);
	} else {
		/* DMA routine */
		dma_src = onenand->phys_base + (p - this->base);
		dma_dst = dma_map_single(dev, buf, count, DMA_FROM_DEVICE);
	}
	if (dma_mapping_error(dev, dma_dst)) {
		dev_err(dev, "Couldn't map a %zu byte buffer for DMA\n", count);
		goto normal;
	}
	err = s5pc110_dma_ops(dma_dst, dma_src,
			count, S5PC110_DMA_DIR_READ);

	if (page_dma)
		dma_unmap_page(dev, dma_dst, count, DMA_FROM_DEVICE);
	else
		dma_unmap_single(dev, dma_dst, count, DMA_FROM_DEVICE);

	if (!err)
		return 0;

normal:
	if (count != mtd->writesize) {
		/* Copy the bufferram to memory to prevent unaligned access */
		memcpy_fromio(this->page_buf, p, mtd->writesize);
		memcpy(buffer, this->page_buf + offset, count);
	} else {
		memcpy_fromio(buffer, p, count);
	}

	return 0;
}

static int s5pc110_chip_probe(struct mtd_info *mtd)
{
	/* Now just return 0 */
	return 0;
}

static int s3c_onenand_bbt_wait(struct mtd_info *mtd, int state)
{
	unsigned int flags = INT_ACT | LOAD_CMP;
	unsigned int stat;
	unsigned long timeout;

	/* The 20 msec is enough */
	timeout = jiffies + msecs_to_jiffies(20);
	while (time_before(jiffies, timeout)) {
		stat = s3c_read_reg(INT_ERR_STAT_OFFSET);
		if (stat & flags)
			break;
	}
	/* To get correct interrupt status in timeout case */
	stat = s3c_read_reg(INT_ERR_STAT_OFFSET);
	s3c_write_reg(stat, INT_ERR_ACK_OFFSET);

	if (stat & LD_FAIL_ECC_ERR) {
		s3c_onenand_reset();
		return ONENAND_BBT_READ_ERROR;
	}

	if (stat & LOAD_CMP) {
		int ecc = s3c_read_reg(ECC_ERR_STAT_OFFSET);
		if (ecc & ONENAND_ECC_4BIT_UNCORRECTABLE) {
			s3c_onenand_reset();
			return ONENAND_BBT_READ_ERROR;
		}
	}

	return 0;
}

static void s3c_onenand_check_lock_status(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;
	struct device *dev = &onenand->pdev->dev;
	unsigned int block, end;

	end = this->chipsize >> this->erase_shift;

	for (block = 0; block < end; block++) {
		unsigned int mem_addr = onenand->mem_addr(block, 0, 0);
		s3c_read_cmd(CMD_MAP_01(onenand, mem_addr));

		if (s3c_read_reg(INT_ERR_STAT_OFFSET) & LOCKED_BLK) {
			dev_err(dev, "block %d is write-protected!\n", block);
			s3c_write_reg(LOCKED_BLK, INT_ERR_ACK_OFFSET);
		}
	}
}

static void s3c_onenand_do_lock_cmd(struct mtd_info *mtd, loff_t ofs,
				    size_t len, int cmd)
{
	struct onenand_chip *this = mtd->priv;
	int start, end, start_mem_addr, end_mem_addr;

	start = ofs >> this->erase_shift;
	start_mem_addr = onenand->mem_addr(start, 0, 0);
	end = start + (len >> this->erase_shift) - 1;
	end_mem_addr = onenand->mem_addr(end, 0, 0);

	if (cmd == ONENAND_CMD_LOCK) {
		s3c_write_cmd(ONENAND_LOCK_START, CMD_MAP_10(onenand,
							     start_mem_addr));
		s3c_write_cmd(ONENAND_LOCK_END, CMD_MAP_10(onenand,
							   end_mem_addr));
	} else {
		s3c_write_cmd(ONENAND_UNLOCK_START, CMD_MAP_10(onenand,
							       start_mem_addr));
		s3c_write_cmd(ONENAND_UNLOCK_END, CMD_MAP_10(onenand,
							     end_mem_addr));
	}

	this->wait(mtd, FL_LOCKING);
}

static void s3c_unlock_all(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;
	loff_t ofs = 0;
	size_t len = this->chipsize;

	if (this->options & ONENAND_HAS_UNLOCK_ALL) {
		/* Write unlock command */
		this->command(mtd, ONENAND_CMD_UNLOCK_ALL, 0, 0);

		/* No need to check return value */
		this->wait(mtd, FL_LOCKING);

		/* Workaround for all block unlock in DDP */
		if (!ONENAND_IS_DDP(this)) {
			s3c_onenand_check_lock_status(mtd);
			return;
		}

		/* All blocks on another chip */
		ofs = this->chipsize >> 1;
		len = this->chipsize >> 1;
	}

	s3c_onenand_do_lock_cmd(mtd, ofs, len, ONENAND_CMD_UNLOCK);

	s3c_onenand_check_lock_status(mtd);
}

static void s3c_onenand_setup(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;

	onenand->mtd = mtd;

	if (onenand->type == TYPE_S3C6400) {
		onenand->mem_addr = s3c6400_mem_addr;
		onenand->cmd_map = s3c64xx_cmd_map;
	} else if (onenand->type == TYPE_S3C6410) {
		onenand->mem_addr = s3c6410_mem_addr;
		onenand->cmd_map = s3c64xx_cmd_map;
	} else if (onenand->type == TYPE_S5PC110) {
		/* Use generic onenand functions */
		this->read_bufferram = s5pc110_read_bufferram;
		this->chip_probe = s5pc110_chip_probe;
		return;
	} else {
		BUG();
	}

	this->read_word = s3c_onenand_readw;
	this->write_word = s3c_onenand_writew;

	this->wait = s3c_onenand_wait;
	this->bbt_wait = s3c_onenand_bbt_wait;
	this->unlock_all = s3c_unlock_all;
	this->command = s3c_onenand_command;

	this->read_bufferram = onenand_read_bufferram;
	this->write_bufferram = onenand_write_bufferram;
}

static int s3c_onenand_probe(struct platform_device *pdev)
{
	struct onenand_platform_data *pdata;
	struct onenand_chip *this;
	struct mtd_info *mtd;
	struct resource *r;
	int size, err;

	pdata = dev_get_platdata(&pdev->dev);
	/* No need to check pdata. the platform data is optional */

	size = sizeof(struct mtd_info) + sizeof(struct onenand_chip);
	mtd = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!mtd)
		return -ENOMEM;

	onenand = devm_kzalloc(&pdev->dev, sizeof(struct s3c_onenand),
			       GFP_KERNEL);
	if (!onenand)
		return -ENOMEM;

	this = (struct onenand_chip *) &mtd[1];
	mtd->priv = this;
	mtd->dev.parent = &pdev->dev;
	onenand->pdev = pdev;
	onenand->type = platform_get_device_id(pdev)->driver_data;

	s3c_onenand_setup(mtd);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	onenand->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(onenand->base))
		return PTR_ERR(onenand->base);

	onenand->phys_base = r->start;

	/* Set onenand_chip also */
	this->base = onenand->base;

	/* Use runtime badblock check */
	this->options |= ONENAND_SKIP_UNLOCK_CHECK;

	if (onenand->type != TYPE_S5PC110) {
		r = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		onenand->ahb_addr = devm_ioremap_resource(&pdev->dev, r);
		if (IS_ERR(onenand->ahb_addr))
			return PTR_ERR(onenand->ahb_addr);

		/* Allocate 4KiB BufferRAM */
		onenand->page_buf = devm_kzalloc(&pdev->dev, SZ_4K,
						 GFP_KERNEL);
		if (!onenand->page_buf)
			return -ENOMEM;

		/* Allocate 128 SpareRAM */
		onenand->oob_buf = devm_kzalloc(&pdev->dev, 128, GFP_KERNEL);
		if (!onenand->oob_buf)
			return -ENOMEM;

		/* S3C doesn't handle subpage write */
		mtd->subpage_sft = 0;
		this->subpagesize = mtd->writesize;

	} else { /* S5PC110 */
		r = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		onenand->dma_addr = devm_ioremap_resource(&pdev->dev, r);
		if (IS_ERR(onenand->dma_addr))
			return PTR_ERR(onenand->dma_addr);

		s5pc110_dma_ops = s5pc110_dma_poll;
		/* Interrupt support */
		r = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
		if (r) {
			init_completion(&onenand->complete);
			s5pc110_dma_ops = s5pc110_dma_irq;
			err = devm_request_irq(&pdev->dev, r->start,
					       s5pc110_onenand_irq,
					       IRQF_SHARED, "onenand",
					       &onenand);
			if (err) {
				dev_err(&pdev->dev, "failed to get irq\n");
				return err;
			}
		}
	}

	err = onenand_scan(mtd, 1);
	if (err)
		return err;

	if (onenand->type != TYPE_S5PC110) {
		/* S3C doesn't handle subpage write */
		mtd->subpage_sft = 0;
		this->subpagesize = mtd->writesize;
	}

	if (s3c_read_reg(MEM_CFG_OFFSET) & ONENAND_SYS_CFG1_SYNC_READ)
		dev_info(&onenand->pdev->dev, "OneNAND Sync. Burst Read enabled\n");

	err = mtd_device_register(mtd, pdata ? pdata->parts : NULL,
				  pdata ? pdata->nr_parts : 0);
	if (err) {
		dev_err(&pdev->dev, "failed to parse partitions and register the MTD device\n");
		onenand_release(mtd);
		return err;
	}

	platform_set_drvdata(pdev, mtd);

	return 0;
}

static int s3c_onenand_remove(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);

	onenand_release(mtd);

	return 0;
}

static int s3c_pm_ops_suspend(struct device *dev)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct onenand_chip *this = mtd->priv;

	this->wait(mtd, FL_PM_SUSPENDED);
	return 0;
}

static  int s3c_pm_ops_resume(struct device *dev)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct onenand_chip *this = mtd->priv;

	this->unlock_all(mtd);
	return 0;
}

static const struct dev_pm_ops s3c_pm_ops = {
	.suspend	= s3c_pm_ops_suspend,
	.resume		= s3c_pm_ops_resume,
};

static const struct platform_device_id s3c_onenand_driver_ids[] = {
	{
		.name		= "s3c6400-onenand",
		.driver_data	= TYPE_S3C6400,
	}, {
		.name		= "s3c6410-onenand",
		.driver_data	= TYPE_S3C6410,
	}, {
		.name		= "s5pc110-onenand",
		.driver_data	= TYPE_S5PC110,
	}, { },
};
MODULE_DEVICE_TABLE(platform, s3c_onenand_driver_ids);

static struct platform_driver s3c_onenand_driver = {
	.driver         = {
		.name	= "samsung-onenand",
		.pm	= &s3c_pm_ops,
	},
	.id_table	= s3c_onenand_driver_ids,
	.probe          = s3c_onenand_probe,
	.remove         = s3c_onenand_remove,
};

module_platform_driver(s3c_onenand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_DESCRIPTION("Samsung OneNAND controller support");
