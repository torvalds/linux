/* 
 * cafe_nand.c
 *
 * Copyright © 2006 Red Hat, Inc.
 * Copyright © 2006 David Woodhouse <dwmw2@infradead.org>
 */

#define DEBUG

#include <linux/device.h>
#undef DEBUG
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#define CAFE_NAND_CTRL1		0x00
#define CAFE_NAND_CTRL2		0x04
#define CAFE_NAND_CTRL3		0x08
#define CAFE_NAND_STATUS	0x0c
#define CAFE_NAND_IRQ		0x10
#define CAFE_NAND_IRQ_MASK	0x14
#define CAFE_NAND_DATA_LEN	0x18
#define CAFE_NAND_ADDR1		0x1c
#define CAFE_NAND_ADDR2		0x20
#define CAFE_NAND_TIMING1	0x24
#define CAFE_NAND_TIMING2	0x28
#define CAFE_NAND_TIMING3	0x2c
#define CAFE_NAND_NONMEM	0x30
#define CAFE_NAND_ECC_RESULT	0x3C
#define CAFE_NAND_ECC_SYN01	0x50
#define CAFE_NAND_ECC_SYN23	0x54
#define CAFE_NAND_ECC_SYN45	0x58
#define CAFE_NAND_ECC_SYN67	0x5c
#define CAFE_NAND_DMA_CTRL	0x40
#define CAFE_NAND_DMA_ADDR0	0x44
#define CAFE_NAND_DMA_ADDR1	0x48
#define CAFE_NAND_READ_DATA	0x1000
#define CAFE_NAND_WRITE_DATA	0x2000

int cafe_correct_ecc(unsigned char *buf,
		     unsigned short *chk_syndrome_list);

struct cafe_priv {
	struct nand_chip nand;
	struct pci_dev *pdev;
	void __iomem *mmio;
	uint32_t ctl1;
	uint32_t ctl2;
	int datalen;
	int nr_data;
	int data_pos;
	int page_addr;
	dma_addr_t dmaaddr;
	unsigned char *dmabuf;
	
};

static int usedma = 0;
module_param(usedma, int, 0644);

static int skipbbt = 0;
module_param(skipbbt, int, 0644);

static int debug = 0;
module_param(debug, int, 0644);

/* Hrm. Why isn't this already conditional on something in the struct device? */
#define cafe_dev_dbg(dev, args...) do { if (debug) dev_dbg(dev, ##args); } while(0)


static int cafe_device_ready(struct mtd_info *mtd)
{
	struct cafe_priv *cafe = mtd->priv;
	int result = !!(readl(cafe->mmio + CAFE_NAND_STATUS) | 0x40000000);

	uint32_t irqs = readl(cafe->mmio + CAFE_NAND_IRQ);
	writel(irqs, cafe->mmio+CAFE_NAND_IRQ);
	cafe_dev_dbg(&cafe->pdev->dev, "NAND device is%s ready, IRQ %x (%x) (%x,%x)\n",
		result?"":" not", irqs, readl(cafe->mmio + CAFE_NAND_IRQ),
		readl(cafe->mmio + 0x3008), readl(cafe->mmio + 0x300c));
	return result;
}


static void cafe_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct cafe_priv *cafe = mtd->priv;

	if (usedma)
		memcpy(cafe->dmabuf + cafe->datalen, buf, len);
	else
		memcpy_toio(cafe->mmio + CAFE_NAND_WRITE_DATA + cafe->datalen, buf, len);
	cafe->datalen += len;

	cafe_dev_dbg(&cafe->pdev->dev, "Copy 0x%x bytes to write buffer. datalen 0x%x\n",
		len, cafe->datalen);
}

static void cafe_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct cafe_priv *cafe = mtd->priv;

	if (usedma)
		memcpy(buf, cafe->dmabuf + cafe->datalen, len);
	else
		memcpy_fromio(buf, cafe->mmio + CAFE_NAND_READ_DATA + cafe->datalen, len);

	cafe_dev_dbg(&cafe->pdev->dev, "Copy 0x%x bytes from position 0x%x in read buffer.\n",
		  len, cafe->datalen);
	cafe->datalen += len;
}

static uint8_t cafe_read_byte(struct mtd_info *mtd)
{
	struct cafe_priv *cafe = mtd->priv;
	uint8_t d;

	cafe_read_buf(mtd, &d, 1);
	cafe_dev_dbg(&cafe->pdev->dev, "Read %02x\n", d);

	return d;
}

static void cafe_nand_cmdfunc(struct mtd_info *mtd, unsigned command,
			      int column, int page_addr)
{
	struct cafe_priv *cafe = mtd->priv;
	int adrbytes = 0;
	uint32_t ctl1;
	uint32_t doneint = 0x80000000;
	int i;

	cafe_dev_dbg(&cafe->pdev->dev, "cmdfunc %02x, 0x%x, 0x%x\n",
		command, column, page_addr);

	if (command == NAND_CMD_ERASE2 || command == NAND_CMD_PAGEPROG) {
		/* Second half of a command we already calculated */
		writel(cafe->ctl2 | 0x100 | command, cafe->mmio + 0x04);
		ctl1 = cafe->ctl1;
		cafe_dev_dbg(&cafe->pdev->dev, "Continue command, ctl1 %08x, #data %d\n",
			  cafe->ctl1, cafe->nr_data);
		goto do_command;
	}
	/* Reset ECC engine */
	writel(0, cafe->mmio + CAFE_NAND_CTRL2);

	/* Emulate NAND_CMD_READOOB on large-page chips */
	if (mtd->writesize > 512 &&
	    command == NAND_CMD_READOOB) {
		column += mtd->writesize;
		command = NAND_CMD_READ0;
	}

	/* FIXME: Do we need to send read command before sending data
	   for small-page chips, to position the buffer correctly? */

	if (column != -1) {
		writel(column, cafe->mmio + 0x1c);
		adrbytes = 2;
		if (page_addr != -1)
			goto write_adr2;
	} else if (page_addr != -1) {
		writel(page_addr & 0xffff, cafe->mmio + 0x1c);
		page_addr >>= 16;
	write_adr2:
		writel(page_addr, cafe->mmio+0x20);
		adrbytes += 2;
		if (mtd->size > mtd->writesize << 16)
			adrbytes++;
	}

	cafe->data_pos = cafe->datalen = 0;

	/* Set command valid bit */
	ctl1 = 0x80000000 | command;

	/* Set RD or WR bits as appropriate */
	if (command == NAND_CMD_READID || command == NAND_CMD_STATUS) {
		ctl1 |= (1<<26); /* rd */
		/* Always 5 bytes, for now */
		cafe->datalen = 4;
		/* And one address cycle -- even for STATUS, since the controller doesn't work without */
		adrbytes = 1;
	} else if (command == NAND_CMD_READ0 || command == NAND_CMD_READ1 ||
		   command == NAND_CMD_READOOB || command == NAND_CMD_RNDOUT) {
		ctl1 |= 1<<26; /* rd */
		/* For now, assume just read to end of page */
		cafe->datalen = mtd->writesize + mtd->oobsize - column;
	} else if (command == NAND_CMD_SEQIN)
		ctl1 |= 1<<25; /* wr */

	/* Set number of address bytes */
	if (adrbytes)
		ctl1 |= ((adrbytes-1)|8) << 27;

	if (command == NAND_CMD_SEQIN || command == NAND_CMD_ERASE1) {
		/* Ignore the first command of a pair; the hardware 
		   deals with them both at once, later */
		cafe->ctl1 = ctl1;
		cafe->ctl2 = 0;
		cafe_dev_dbg(&cafe->pdev->dev, "Setup for delayed command, ctl1 %08x, dlen %x\n",
			  cafe->ctl1, cafe->datalen);
		return;
	}
	/* RNDOUT and READ0 commands need a following byte */
	if (command == NAND_CMD_RNDOUT)
		writel(cafe->ctl2 | 0x100 | NAND_CMD_RNDOUTSTART, cafe->mmio + CAFE_NAND_CTRL2);
	else if (command == NAND_CMD_READ0 && mtd->writesize > 512)
		writel(cafe->ctl2 | 0x100 | NAND_CMD_READSTART, cafe->mmio + CAFE_NAND_CTRL2);

 do_command:
#if 0
	// ECC on read only works if we ...
	if (cafe->datalen == 2112)
		cafe->datalen = 2062;
#endif
	cafe_dev_dbg(&cafe->pdev->dev, "dlen %x, ctl1 %x, ctl2 %x\n", 
		cafe->datalen, ctl1, readl(cafe->mmio+CAFE_NAND_CTRL2));
	/* NB: The datasheet lies -- we really should be subtracting 1 here */
	writel(cafe->datalen, cafe->mmio + CAFE_NAND_DATA_LEN);
	writel(0x90000000, cafe->mmio + CAFE_NAND_IRQ);
	if (usedma && (ctl1 & (3<<25))) {
		uint32_t dmactl = 0xc0000000 + cafe->datalen;
		/* If WR or RD bits set, set up DMA */
		if (ctl1 & (1<<26)) {
			/* It's a read */
			dmactl |= (1<<29);
			/* ... so it's done when the DMA is done, not just
			   the command. */
			doneint = 0x10000000;
		}
		writel(dmactl, cafe->mmio + 0x40);
	}
#if 0
	printk("DMA setup is %x, status %x, ctl1 %x\n", readl(cafe->mmio + 0x40), readl(cafe->mmio + 0x0c), readl(cafe->mmio));
	printk("DMA setup is %x, status %x, ctl1 %x\n", readl(cafe->mmio + 0x40), readl(cafe->mmio + 0x0c), readl(cafe->mmio));
#endif
	cafe->datalen = 0;

#if 0
	printk("About to write command %08x\n", ctl1);
	for (i=0; i< 0x5c; i+=4)
		printk("Register %x: %08x\n", i, readl(cafe->mmio + i));
#endif
	writel(ctl1, cafe->mmio + CAFE_NAND_CTRL1);
	/* Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine. */
	ndelay(100);

	if (1) {
		int c = 500000;
		uint32_t irqs;

		while (c--) {
			irqs = readl(cafe->mmio + CAFE_NAND_IRQ);
			if (irqs & doneint)
				break;
			udelay(1);
			if (!(c % 100000))
				cafe_dev_dbg(&cafe->pdev->dev, "Wait for ready, IRQ %x\n", irqs);
			cpu_relax();
		}
		writel(doneint, cafe->mmio + CAFE_NAND_IRQ);
		cafe_dev_dbg(&cafe->pdev->dev, "Command %x completed after %d usec, irqs %x (%x)\n", command, 50000-c, irqs, readl(cafe->mmio + CAFE_NAND_IRQ));
	}


	cafe->ctl2 &= ~(1<<8);
	cafe->ctl2 &= ~(1<<30);

	switch (command) {

	case NAND_CMD_CACHEDPROG:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_RNDIN:
	case NAND_CMD_STATUS:
	case NAND_CMD_DEPLETE1:
	case NAND_CMD_RNDOUT:
	case NAND_CMD_STATUS_ERROR:
	case NAND_CMD_STATUS_ERROR0:
	case NAND_CMD_STATUS_ERROR1:
	case NAND_CMD_STATUS_ERROR2:
	case NAND_CMD_STATUS_ERROR3:
		writel(cafe->ctl2, cafe->mmio + CAFE_NAND_CTRL2);
		return;
	}
	nand_wait_ready(mtd);
	writel(cafe->ctl2, cafe->mmio + CAFE_NAND_CTRL2);
}

static void cafe_select_chip(struct mtd_info *mtd, int chipnr)
{
	//struct cafe_priv *cafe = mtd->priv;
	//	cafe_dev_dbg(&cafe->pdev->dev, "select_chip %d\n", chipnr);
}
static int cafe_nand_interrupt(int irq, void *id, struct pt_regs *regs)
{
	struct mtd_info *mtd = id;
	struct cafe_priv *cafe = mtd->priv;
	uint32_t irqs = readl(cafe->mmio + CAFE_NAND_IRQ);
	writel(irqs & ~0x90000000, cafe->mmio + CAFE_NAND_IRQ);
	if (!irqs)
		return IRQ_NONE;

	cafe_dev_dbg(&cafe->pdev->dev, "irq, bits %x (%x)\n", irqs, readl(cafe->mmio + CAFE_NAND_IRQ));
	return IRQ_HANDLED;
}

static void cafe_nand_bug(struct mtd_info *mtd)
{
	BUG();
}

static int cafe_nand_write_oob(struct mtd_info *mtd,
			       struct nand_chip *chip, int page)
{
	int status = 0;

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

/* Don't use -- use nand_read_oob_std for now */
static int cafe_nand_read_oob(struct mtd_info *mtd, struct nand_chip *chip,
			      int page, int sndcmd)
{
	chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);
	return 1;
}
/**
 * cafe_nand_read_page_syndrome - {REPLACABLE] hardware ecc syndrom based page read
 * @mtd:	mtd info structure
 * @chip:	nand chip info structure
 * @buf:	buffer to store read data
 *
 * The hw generator calculates the error syndrome automatically. Therefor
 * we need a special oob layout and handling.
 */
static int cafe_nand_read_page(struct mtd_info *mtd, struct nand_chip *chip,
			       uint8_t *buf)
{
	struct cafe_priv *cafe = mtd->priv;

	dev_dbg(&cafe->pdev->dev, "ECC result %08x SYN1,2 %08x\n",
		readl(cafe->mmio + CAFE_NAND_ECC_RESULT),
		readl(cafe->mmio + CAFE_NAND_ECC_SYN01));

	chip->read_buf(mtd, buf, mtd->writesize);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	if (readl(cafe->mmio + CAFE_NAND_ECC_RESULT) & (1<<18)) {
		unsigned short syn[8];
		int i;

		for (i=0; i<8; i+=2) {
			uint32_t tmp = readl(cafe->mmio + CAFE_NAND_ECC_SYN01 + (i*2));
			syn[i] = tmp & 0xfff;
			syn[i+1] = (tmp >> 16) & 0xfff;
		} 

		if ((i = cafe_correct_ecc(buf, syn)) < 0) {
			dev_dbg(&cafe->pdev->dev, "Failed to correct ECC\n");
			mtd->ecc_stats.failed++;
		} else {
			dev_dbg(&cafe->pdev->dev, "Corrected %d symbol errors\n", i);
			mtd->ecc_stats.corrected += i;
		}
	}


	return 0;
}

static struct nand_ecclayout cafe_oobinfo_2048 = {
	.eccbytes = 14,
	.eccpos = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},
	.oobfree = {{14, 50}}
};

/* Ick. The BBT code really ought to be able to work this bit out 
   for itself from the above */
static uint8_t cafe_bbt_pattern[] = {'B', 'b', 't', '0' };
static uint8_t cafe_mirror_pattern[] = {'1', 't', 'b', 'B' };

static struct nand_bbt_descr cafe_bbt_main_descr_2048 = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs =	14,
	.len = 4,
	.veroffs = 18,
	.maxblocks = 4,
	.pattern = cafe_bbt_pattern
};

static struct nand_bbt_descr cafe_bbt_mirror_descr_2048 = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs =	14,
	.len = 4,
	.veroffs = 18,
	.maxblocks = 4,
	.pattern = cafe_mirror_pattern
};

static struct nand_ecclayout cafe_oobinfo_512 = {
	.eccbytes = 14,
	.eccpos = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},
	.oobfree = {{14, 2}}
};

static void cafe_nand_write_page_lowlevel(struct mtd_info *mtd,
					  struct nand_chip *chip, const uint8_t *buf)
{
	struct cafe_priv *cafe = mtd->priv;

	chip->write_buf(mtd, buf, mtd->writesize);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);

	/* Set up ECC autogeneration */
	cafe->ctl2 |= (1<<27) | (1<<30);
	if (mtd->writesize == 2048)
		cafe->ctl2 |= (1<<29);
}

static int cafe_nand_write_page(struct mtd_info *mtd, struct nand_chip *chip,
				const uint8_t *buf, int page, int cached, int raw)
{
	int status;

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, 0x00, page);

	if (unlikely(raw))
		chip->ecc.write_page_raw(mtd, chip, buf);
	else
		chip->ecc.write_page(mtd, chip, buf);

	/*
	 * Cached progamming disabled for now, Not sure if its worth the
	 * trouble. The speed gain is not very impressive. (2.3->2.6Mib/s)
	 */
	cached = 0;

	if (!cached || !(chip->options & NAND_CACHEPRG)) {

		chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
		status = chip->waitfunc(mtd, chip);
		/*
		 * See if operation failed and additional status checks are
		 * available
		 */
		if ((status & NAND_STATUS_FAIL) && (chip->errstat))
			status = chip->errstat(mtd, chip, FL_WRITING, status,
					       page);

		if (status & NAND_STATUS_FAIL)
			return -EIO;
	} else {
		chip->cmdfunc(mtd, NAND_CMD_CACHEDPROG, -1, -1);
		status = chip->waitfunc(mtd, chip);
	}

#ifdef CONFIG_MTD_NAND_VERIFY_WRITE
	/* Send command to read back the data */
	chip->cmdfunc(mtd, NAND_CMD_READ0, 0, page);

	if (chip->verify_buf(mtd, buf, mtd->writesize))
		return -EIO;
#endif
	return 0;
}

static int cafe_nand_block_bad(struct mtd_info *mtd, loff_t ofs, int getchip)
{
	return 0;
}

static int __devinit cafe_nand_probe(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct mtd_info *mtd;
	struct cafe_priv *cafe;
	uint32_t ctrl;
	int err = 0;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	pci_set_master(pdev);

	mtd = kzalloc(sizeof(*mtd) + sizeof(struct cafe_priv), GFP_KERNEL);
	if (!mtd) {
		dev_warn(&pdev->dev, "failed to alloc mtd_info\n");
		return  -ENOMEM;
	}
	cafe = (void *)(&mtd[1]);

	mtd->priv = cafe;
	mtd->owner = THIS_MODULE;

	cafe->pdev = pdev;
	cafe->mmio = pci_iomap(pdev, 0, 0);
	if (!cafe->mmio) {
		dev_warn(&pdev->dev, "failed to iomap\n");
		err = -ENOMEM;
		goto out_free_mtd;
	}
	cafe->dmabuf = dma_alloc_coherent(&cafe->pdev->dev, 2112 + sizeof(struct nand_buffers),
					  &cafe->dmaaddr, GFP_KERNEL);
	if (!cafe->dmabuf) {
		err = -ENOMEM;
		goto out_ior;
	}
	cafe->nand.buffers = (void *)cafe->dmabuf + 2112;

	cafe->nand.cmdfunc = cafe_nand_cmdfunc;
	cafe->nand.dev_ready = cafe_device_ready;
	cafe->nand.read_byte = cafe_read_byte;
	cafe->nand.read_buf = cafe_read_buf;
	cafe->nand.write_buf = cafe_write_buf;
	cafe->nand.select_chip = cafe_select_chip;

	cafe->nand.chip_delay = 0;

	/* Enable the following for a flash based bad block table */
	cafe->nand.options = NAND_USE_FLASH_BBT | NAND_NO_AUTOINCR | NAND_OWN_BUFFERS;

	if (skipbbt) {
		cafe->nand.options |= NAND_SKIP_BBTSCAN;
		cafe->nand.block_bad = cafe_nand_block_bad;
	}
	
	/* Timings from Marvell's test code (not verified or calculated by us) */
	writel(0xffffffff, cafe->mmio + CAFE_NAND_IRQ_MASK);
#if 1
	writel(0x01010a0a, cafe->mmio + CAFE_NAND_TIMING1);
	writel(0x24121212, cafe->mmio + CAFE_NAND_TIMING2);
	writel(0x11000000, cafe->mmio + CAFE_NAND_TIMING3);
#else
	writel(0xffffffff, cafe->mmio + CAFE_NAND_TIMING1);
	writel(0xffffffff, cafe->mmio + CAFE_NAND_TIMING2);
	writel(0xffffffff, cafe->mmio + CAFE_NAND_TIMING3);
#endif
	writel(0xffffffff, cafe->mmio + CAFE_NAND_IRQ_MASK);
	err = request_irq(pdev->irq, &cafe_nand_interrupt, SA_SHIRQ, "CAFE NAND", mtd);
	if (err) {
		dev_warn(&pdev->dev, "Could not register IRQ %d\n", pdev->irq);
		
		goto out_free_dma;
	}
#if 1
	/* Disable master reset, enable NAND clock */
	ctrl = readl(cafe->mmio + 0x3004);
	ctrl &= 0xffffeff0;
	ctrl |= 0x00007000;
	writel(ctrl | 0x05, cafe->mmio + 0x3004);
	writel(ctrl | 0x0a, cafe->mmio + 0x3004);
	writel(0, cafe->mmio + 0x40);

	writel(0x7006, cafe->mmio + 0x3004);
	writel(0x700a, cafe->mmio + 0x3004);

	/* Set up DMA address */
	writel(cafe->dmaaddr & 0xffffffff, cafe->mmio + 0x44);
	if (sizeof(cafe->dmaaddr) > 4)
		writel((cafe->dmaaddr >> 16) >> 16, cafe->mmio + 0x48);
	else
		writel(0, cafe->mmio + 0x48);
	cafe_dev_dbg(&cafe->pdev->dev, "Set DMA address to %x (virt %p)\n",
		readl(cafe->mmio+0x44), cafe->dmabuf);

	/* Enable NAND IRQ in global IRQ mask register */
	writel(0x80000007, cafe->mmio + 0x300c);
	cafe_dev_dbg(&cafe->pdev->dev, "Control %x, IRQ mask %x\n",
		readl(cafe->mmio + 0x3004), readl(cafe->mmio + 0x300c));
#endif
#if 1
	mtd->writesize=2048;
	mtd->oobsize = 0x40;
	memset(cafe->dmabuf, 0x5a, 2112);
	cafe->nand.cmdfunc(mtd, NAND_CMD_READID, 0, -1);
	cafe->nand.read_byte(mtd);
	cafe->nand.read_byte(mtd);
	cafe->nand.read_byte(mtd);
	cafe->nand.read_byte(mtd);
	cafe->nand.read_byte(mtd);
#endif
#if 0
	cafe->nand.cmdfunc(mtd, NAND_CMD_READ0, 0, 0);
	//	nand_wait_ready(mtd);
	cafe->nand.read_byte(mtd);
	cafe->nand.read_byte(mtd);
	cafe->nand.read_byte(mtd);
	cafe->nand.read_byte(mtd);
#endif
#if 0
	writel(0x84600070, cafe->mmio);
	udelay(10);
	cafe_dev_dbg(&cafe->pdev->dev, "Status %x\n", readl(cafe->mmio + 0x30));
#endif		
	/* Scan to find existance of the device */
	if (nand_scan_ident(mtd, 1)) {
		err = -ENXIO;
		goto out_irq;
	}

	cafe->ctl2 = 1<<27; /* Reed-Solomon ECC */
	if (mtd->writesize == 2048)
		cafe->ctl2 |= 1<<29; /* 2KiB page size */

	/* Set up ECC according to the type of chip we found */
	if (mtd->writesize == 512 || mtd->writesize == 2048) {
		cafe->nand.ecc.mode = NAND_ECC_HW_SYNDROME;
		cafe->nand.ecc.size = mtd->writesize;
		cafe->nand.ecc.bytes = 14;
		cafe->nand.ecc.layout = &cafe_oobinfo_2048;
		cafe->nand.bbt_td = &cafe_bbt_main_descr_2048;
		cafe->nand.bbt_md = &cafe_bbt_mirror_descr_2048;
		cafe->nand.ecc.hwctl  = (void *)cafe_nand_bug;
		cafe->nand.ecc.calculate = (void *)cafe_nand_bug;
		cafe->nand.ecc.correct  = (void *)cafe_nand_bug;
		cafe->nand.write_page = cafe_nand_write_page;
		cafe->nand.ecc.write_page = cafe_nand_write_page_lowlevel;
		cafe->nand.ecc.write_oob = cafe_nand_write_oob;
		cafe->nand.ecc.read_page = cafe_nand_read_page;
		cafe->nand.ecc.read_oob = cafe_nand_read_oob;

	} else {
		printk(KERN_WARNING "Unexpected NAND flash writesize %d. Using software ECC\n",
		       mtd->writesize);
		cafe->nand.ecc.mode = NAND_ECC_NONE;
	}

	err = nand_scan_tail(mtd);
	if (err)
		goto out_irq;

	pci_set_drvdata(pdev, mtd);
	add_mtd_device(mtd);
	goto out;

 out_irq:
	/* Disable NAND IRQ in global IRQ mask register */
	writel(~1 & readl(cafe->mmio + 0x300c), cafe->mmio + 0x300c);
	free_irq(pdev->irq, mtd);
 out_free_dma:
	dma_free_coherent(&cafe->pdev->dev, 2112, cafe->dmabuf, cafe->dmaaddr);
 out_ior:
	pci_iounmap(pdev, cafe->mmio);
 out_free_mtd:
	kfree(mtd);
 out:
	return err;
}

static void __devexit cafe_nand_remove(struct pci_dev *pdev)
{
	struct mtd_info *mtd = pci_get_drvdata(pdev);
	struct cafe_priv *cafe = mtd->priv;

	del_mtd_device(mtd);
	/* Disable NAND IRQ in global IRQ mask register */
	writel(~1 & readl(cafe->mmio + 0x300c), cafe->mmio + 0x300c);
	free_irq(pdev->irq, mtd);
	nand_release(mtd);
	pci_iounmap(pdev, cafe->mmio);
	dma_free_coherent(&cafe->pdev->dev, 2112, cafe->dmabuf, cafe->dmaaddr);
	kfree(mtd);
}

static struct pci_device_id cafe_nand_tbl[] = {
	{ 0x11ab, 0x4100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_MEMORY_FLASH << 8, 0xFFFF0 }
};

MODULE_DEVICE_TABLE(pci, cafe_nand_tbl);

static struct pci_driver cafe_nand_pci_driver = {
	.name = "CAFÉ NAND",
	.id_table = cafe_nand_tbl,
	.probe = cafe_nand_probe,
	.remove = __devexit_p(cafe_nand_remove),
#ifdef CONFIG_PMx
	.suspend = cafe_nand_suspend,
	.resume = cafe_nand_resume,
#endif
};

static int cafe_nand_init(void)
{
	return pci_register_driver(&cafe_nand_pci_driver);
}

static void cafe_nand_exit(void)
{
	pci_unregister_driver(&cafe_nand_pci_driver);
}
module_init(cafe_nand_init);
module_exit(cafe_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("NAND flash driver for OLPC CAFE chip");

/* Correct ECC for 2048 bytes of 0xff:
   41 a0 71 65 54 27 f3 93 ec a9 be ed 0b a1 */

/* dwmw2's B-test board, in case of completely screwing it:
Bad eraseblock 2394 at 0x12b40000
Bad eraseblock 2627 at 0x14860000
Bad eraseblock 3349 at 0x1a2a0000
*/
