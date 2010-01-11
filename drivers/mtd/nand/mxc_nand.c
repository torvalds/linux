/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include <asm/mach/flash.h>
#include <mach/mxc_nand.h>
#include <mach/hardware.h>

#define DRIVER_NAME "mxc_nand"

#define nfc_is_v21()		(cpu_is_mx25() || cpu_is_mx35())
#define nfc_is_v1()		(cpu_is_mx31() || cpu_is_mx27())

/* Addresses for NFC registers */
#define NFC_BUF_SIZE		0xE00
#define NFC_BUF_ADDR		0xE04
#define NFC_FLASH_ADDR		0xE06
#define NFC_FLASH_CMD		0xE08
#define NFC_CONFIG		0xE0A
#define NFC_ECC_STATUS_RESULT	0xE0C
#define NFC_RSLTMAIN_AREA	0xE0E
#define NFC_RSLTSPARE_AREA	0xE10
#define NFC_WRPROT		0xE12
#define NFC_V1_UNLOCKSTART_BLKADDR	0xe14
#define NFC_V1_UNLOCKEND_BLKADDR	0xe16
#define NFC_V21_UNLOCKSTART_BLKADDR	0xe20
#define NFC_V21_UNLOCKEND_BLKADDR	0xe22
#define NFC_NF_WRPRST		0xE18
#define NFC_CONFIG1		0xE1A
#define NFC_CONFIG2		0xE1C

/* Set INT to 0, FCMD to 1, rest to 0 in NFC_CONFIG2 Register
 * for Command operation */
#define NFC_CMD            0x1

/* Set INT to 0, FADD to 1, rest to 0 in NFC_CONFIG2 Register
 * for Address operation */
#define NFC_ADDR           0x2

/* Set INT to 0, FDI to 1, rest to 0 in NFC_CONFIG2 Register
 * for Input operation */
#define NFC_INPUT          0x4

/* Set INT to 0, FDO to 001, rest to 0 in NFC_CONFIG2 Register
 * for Data Output operation */
#define NFC_OUTPUT         0x8

/* Set INT to 0, FD0 to 010, rest to 0 in NFC_CONFIG2 Register
 * for Read ID operation */
#define NFC_ID             0x10

/* Set INT to 0, FDO to 100, rest to 0 in NFC_CONFIG2 Register
 * for Read Status operation */
#define NFC_STATUS         0x20

/* Set INT to 1, rest to 0 in NFC_CONFIG2 Register for Read
 * Status operation */
#define NFC_INT            0x8000

#define NFC_SP_EN           (1 << 2)
#define NFC_ECC_EN          (1 << 3)
#define NFC_INT_MSK         (1 << 4)
#define NFC_BIG             (1 << 5)
#define NFC_RST             (1 << 6)
#define NFC_CE              (1 << 7)
#define NFC_ONE_CYCLE       (1 << 8)

struct mxc_nand_host {
	struct mtd_info		mtd;
	struct nand_chip	nand;
	struct mtd_partition	*parts;
	struct device		*dev;

	void			*spare0;
	void			*main_area0;
	void			*main_area1;

	void __iomem		*base;
	void __iomem		*regs;
	int			status_request;
	struct clk		*clk;
	int			clk_act;
	int			irq;

	wait_queue_head_t	irq_waitq;

	uint8_t			*data_buf;
	unsigned int		buf_start;
	int			spare_len;
};

/* OOB placement block for use with hardware ecc generation */
static struct nand_ecclayout nandv1_hw_eccoob_smallpage = {
	.eccbytes = 5,
	.eccpos = {6, 7, 8, 9, 10},
	.oobfree = {{0, 5}, {12, 4}, }
};

static struct nand_ecclayout nandv1_hw_eccoob_largepage = {
	.eccbytes = 20,
	.eccpos = {6, 7, 8, 9, 10, 22, 23, 24, 25, 26,
		   38, 39, 40, 41, 42, 54, 55, 56, 57, 58},
	.oobfree = {{2, 4}, {11, 10}, {27, 10}, {43, 10}, {59, 5}, }
};

/* OOB description for 512 byte pages with 16 byte OOB */
static struct nand_ecclayout nandv2_hw_eccoob_smallpage = {
	.eccbytes = 1 * 9,
	.eccpos = {
		 7,  8,  9, 10, 11, 12, 13, 14, 15
	},
	.oobfree = {
		{.offset = 0, .length = 5}
	}
};

/* OOB description for 2048 byte pages with 64 byte OOB */
static struct nand_ecclayout nandv2_hw_eccoob_largepage = {
	.eccbytes = 4 * 9,
	.eccpos = {
		 7,  8,  9, 10, 11, 12, 13, 14, 15,
		23, 24, 25, 26, 27, 28, 29, 30, 31,
		39, 40, 41, 42, 43, 44, 45, 46, 47,
		55, 56, 57, 58, 59, 60, 61, 62, 63
	},
	.oobfree = {
		{.offset = 2, .length = 4},
		{.offset = 16, .length = 7},
		{.offset = 32, .length = 7},
		{.offset = 48, .length = 7}
	}
};

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "RedBoot", "cmdlinepart", NULL };
#endif

static irqreturn_t mxc_nfc_irq(int irq, void *dev_id)
{
	struct mxc_nand_host *host = dev_id;

	uint16_t tmp;

	tmp = readw(host->regs + NFC_CONFIG1);
	tmp |= NFC_INT_MSK; /* Disable interrupt */
	writew(tmp, host->regs + NFC_CONFIG1);

	wake_up(&host->irq_waitq);

	return IRQ_HANDLED;
}

/* This function polls the NANDFC to wait for the basic operation to
 * complete by checking the INT bit of config2 register.
 */
static void wait_op_done(struct mxc_nand_host *host, int useirq)
{
	uint32_t tmp;
	int max_retries = 2000;

	if (useirq) {
		if ((readw(host->regs + NFC_CONFIG2) & NFC_INT) == 0) {

			tmp = readw(host->regs + NFC_CONFIG1);
			tmp  &= ~NFC_INT_MSK;	/* Enable interrupt */
			writew(tmp, host->regs + NFC_CONFIG1);

			wait_event(host->irq_waitq,
				readw(host->regs + NFC_CONFIG2) & NFC_INT);

			tmp = readw(host->regs + NFC_CONFIG2);
			tmp  &= ~NFC_INT;
			writew(tmp, host->regs + NFC_CONFIG2);
		}
	} else {
		while (max_retries-- > 0) {
			if (readw(host->regs + NFC_CONFIG2) & NFC_INT) {
				tmp = readw(host->regs + NFC_CONFIG2);
				tmp  &= ~NFC_INT;
				writew(tmp, host->regs + NFC_CONFIG2);
				break;
			}
			udelay(1);
		}
		if (max_retries < 0)
			DEBUG(MTD_DEBUG_LEVEL0, "%s: INT not set\n",
			      __func__);
	}
}

/* This function issues the specified command to the NAND device and
 * waits for completion. */
static void send_cmd(struct mxc_nand_host *host, uint16_t cmd, int useirq)
{
	DEBUG(MTD_DEBUG_LEVEL3, "send_cmd(host, 0x%x, %d)\n", cmd, useirq);

	writew(cmd, host->regs + NFC_FLASH_CMD);
	writew(NFC_CMD, host->regs + NFC_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, useirq);
}

/* This function sends an address (or partial address) to the
 * NAND device. The address is used to select the source/destination for
 * a NAND command. */
static void send_addr(struct mxc_nand_host *host, uint16_t addr, int islast)
{
	DEBUG(MTD_DEBUG_LEVEL3, "send_addr(host, 0x%x %d)\n", addr, islast);

	writew(addr, host->regs + NFC_FLASH_ADDR);
	writew(NFC_ADDR, host->regs + NFC_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, islast);
}

static void send_page(struct mtd_info *mtd, unsigned int ops)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	int bufs, i;

	if (nfc_is_v1() && mtd->writesize > 512)
		bufs = 4;
	else
		bufs = 1;

	for (i = 0; i < bufs; i++) {

		/* NANDFC buffer 0 is used for page read/write */
		writew(i, host->regs + NFC_BUF_ADDR);

		writew(ops, host->regs + NFC_CONFIG2);

		/* Wait for operation to complete */
		wait_op_done(host, true);
	}
}

/* Request the NANDFC to perform a read of the NAND device ID. */
static void send_read_id(struct mxc_nand_host *host)
{
	struct nand_chip *this = &host->nand;

	/* NANDFC buffer 0 is used for device ID output */
	writew(0x0, host->regs + NFC_BUF_ADDR);

	writew(NFC_ID, host->regs + NFC_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, true);

	if (this->options & NAND_BUSWIDTH_16) {
		void __iomem *main_buf = host->main_area0;
		/* compress the ID info */
		writeb(readb(main_buf + 2), main_buf + 1);
		writeb(readb(main_buf + 4), main_buf + 2);
		writeb(readb(main_buf + 6), main_buf + 3);
		writeb(readb(main_buf + 8), main_buf + 4);
		writeb(readb(main_buf + 10), main_buf + 5);
	}
	memcpy(host->data_buf, host->main_area0, 16);
}

/* This function requests the NANDFC to perform a read of the
 * NAND device status and returns the current status. */
static uint16_t get_dev_status(struct mxc_nand_host *host)
{
	void __iomem *main_buf = host->main_area1;
	uint32_t store;
	uint16_t ret;
	/* Issue status request to NAND device */

	/* store the main area1 first word, later do recovery */
	store = readl(main_buf);
	/* NANDFC buffer 1 is used for device status to prevent
	 * corruption of read/write buffer on status requests. */
	writew(1, host->regs + NFC_BUF_ADDR);

	writew(NFC_STATUS, host->regs + NFC_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, true);

	/* Status is placed in first word of main buffer */
	/* get status, then recovery area 1 data */
	ret = readw(main_buf);
	writel(store, main_buf);

	return ret;
}

/* This functions is used by upper layer to checks if device is ready */
static int mxc_nand_dev_ready(struct mtd_info *mtd)
{
	/*
	 * NFC handles R/B internally. Therefore, this function
	 * always returns status as ready.
	 */
	return 1;
}

static void mxc_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	/*
	 * If HW ECC is enabled, we turn it on during init. There is
	 * no need to enable again here.
	 */
}

static int mxc_nand_correct_data(struct mtd_info *mtd, u_char *dat,
				 u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;

	/*
	 * 1-Bit errors are automatically corrected in HW.  No need for
	 * additional correction.  2-Bit errors cannot be corrected by
	 * HW ECC, so we need to return failure
	 */
	uint16_t ecc_status = readw(host->regs + NFC_ECC_STATUS_RESULT);

	if (((ecc_status & 0x3) == 2) || ((ecc_status >> 2) == 2)) {
		DEBUG(MTD_DEBUG_LEVEL0,
		      "MXC_NAND: HWECC uncorrectable 2-bit ECC error\n");
		return -1;
	}

	return 0;
}

static int mxc_nand_calculate_ecc(struct mtd_info *mtd, const u_char *dat,
				  u_char *ecc_code)
{
	return 0;
}

static u_char mxc_nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	uint8_t ret;

	/* Check for status request */
	if (host->status_request)
		return get_dev_status(host) & 0xFF;

	ret = *(uint8_t *)(host->data_buf + host->buf_start);
	host->buf_start++;

	return ret;
}

static uint16_t mxc_nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	uint16_t ret;

	ret = *(uint16_t *)(host->data_buf + host->buf_start);
	host->buf_start += 2;

	return ret;
}

/* Write data of length len to buffer buf. The data to be
 * written on NAND Flash is first copied to RAMbuffer. After the Data Input
 * Operation by the NFC, the data is written to NAND Flash */
static void mxc_nand_write_buf(struct mtd_info *mtd,
				const u_char *buf, int len)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	u16 col = host->buf_start;
	int n = mtd->oobsize + mtd->writesize - col;

	n = min(n, len);

	memcpy(host->data_buf + col, buf, n);

	host->buf_start += n;
}

/* Read the data buffer from the NAND Flash. To read the data from NAND
 * Flash first the data output cycle is initiated by the NFC, which copies
 * the data to RAMbuffer. This data of length len is then copied to buffer buf.
 */
static void mxc_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	u16 col = host->buf_start;
	int n = mtd->oobsize + mtd->writesize - col;

	n = min(n, len);

	memcpy(buf, host->data_buf + col, len);

	host->buf_start += len;
}

/* Used by the upper layer to verify the data in NAND Flash
 * with the data in the buf. */
static int mxc_nand_verify_buf(struct mtd_info *mtd,
				const u_char *buf, int len)
{
	return -EFAULT;
}

/* This function is used by upper layer for select and
 * deselect of the NAND chip */
static void mxc_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;

	switch (chip) {
	case -1:
		/* Disable the NFC clock */
		if (host->clk_act) {
			clk_disable(host->clk);
			host->clk_act = 0;
		}
		break;
	case 0:
		/* Enable the NFC clock */
		if (!host->clk_act) {
			clk_enable(host->clk);
			host->clk_act = 1;
		}
		break;

	default:
		break;
	}
}

/*
 * Function to transfer data to/from spare area.
 */
static void copy_spare(struct mtd_info *mtd, bool bfrom)
{
	struct nand_chip *this = mtd->priv;
	struct mxc_nand_host *host = this->priv;
	u16 i, j;
	u16 n = mtd->writesize >> 9;
	u8 *d = host->data_buf + mtd->writesize;
	u8 *s = host->spare0;
	u16 t = host->spare_len;

	j = (mtd->oobsize / n >> 1) << 1;

	if (bfrom) {
		for (i = 0; i < n - 1; i++)
			memcpy(d + i * j, s + i * t, j);

		/* the last section */
		memcpy(d + i * j, s + i * t, mtd->oobsize - i * j);
	} else {
		for (i = 0; i < n - 1; i++)
			memcpy(&s[i * t], &d[i * j], j);

		/* the last section */
		memcpy(&s[i * t], &d[i * j], mtd->oobsize - i * j);
	}
}

static void mxc_do_addr_cycle(struct mtd_info *mtd, int column, int page_addr)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;

	/* Write out column address, if necessary */
	if (column != -1) {
		/*
		 * MXC NANDFC can only perform full page+spare or
		 * spare-only read/write.  When the upper layers
		 * layers perform a read/write buf operation,
		 * we will used the saved column adress to index into
		 * the full page.
		 */
		send_addr(host, 0, page_addr == -1);
		if (mtd->writesize > 512)
			/* another col addr cycle for 2k page */
			send_addr(host, 0, false);
	}

	/* Write out page address, if necessary */
	if (page_addr != -1) {
		/* paddr_0 - p_addr_7 */
		send_addr(host, (page_addr & 0xff), false);

		if (mtd->writesize > 512) {
			if (mtd->size >= 0x10000000) {
				/* paddr_8 - paddr_15 */
				send_addr(host, (page_addr >> 8) & 0xff, false);
				send_addr(host, (page_addr >> 16) & 0xff, true);
			} else
				/* paddr_8 - paddr_15 */
				send_addr(host, (page_addr >> 8) & 0xff, true);
		} else {
			/* One more address cycle for higher density devices */
			if (mtd->size >= 0x4000000) {
				/* paddr_8 - paddr_15 */
				send_addr(host, (page_addr >> 8) & 0xff, false);
				send_addr(host, (page_addr >> 16) & 0xff, true);
			} else
				/* paddr_8 - paddr_15 */
				send_addr(host, (page_addr >> 8) & 0xff, true);
		}
	}
}

/* Used by the upper layer to write command to NAND Flash for
 * different operations to be carried out on NAND Flash */
static void mxc_nand_command(struct mtd_info *mtd, unsigned command,
				int column, int page_addr)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "mxc_nand_command (cmd = 0x%x, col = 0x%x, page = 0x%x)\n",
	      command, column, page_addr);

	/* Reset command state information */
	host->status_request = false;

	/* Command pre-processing step */
	switch (command) {

	case NAND_CMD_STATUS:
		host->buf_start = 0;
		host->status_request = true;

		send_cmd(host, command, true);
		mxc_do_addr_cycle(mtd, column, page_addr);
		break;

	case NAND_CMD_READ0:
	case NAND_CMD_READOOB:
		if (command == NAND_CMD_READ0)
			host->buf_start = column;
		else
			host->buf_start = column + mtd->writesize;

		if (mtd->writesize > 512)
			command = NAND_CMD_READ0; /* only READ0 is valid */

		send_cmd(host, command, false);
		mxc_do_addr_cycle(mtd, column, page_addr);

		if (mtd->writesize > 512)
			send_cmd(host, NAND_CMD_READSTART, true);

		send_page(mtd, NFC_OUTPUT);

		memcpy(host->data_buf, host->main_area0, mtd->writesize);
		copy_spare(mtd, true);
		break;

	case NAND_CMD_SEQIN:
		if (column >= mtd->writesize) {
			/*
			 * FIXME: before send SEQIN command for write OOB,
			 * We must read one page out.
			 * For K9F1GXX has no READ1 command to set current HW
			 * pointer to spare area, we must write the whole page
			 * including OOB together.
			 */
			if (mtd->writesize > 512)
				/* call ourself to read a page */
				mxc_nand_command(mtd, NAND_CMD_READ0, 0,
						page_addr);

			host->buf_start = column;

			/* Set program pointer to spare region */
			if (mtd->writesize == 512)
				send_cmd(host, NAND_CMD_READOOB, false);
		} else {
			host->buf_start = column;

			/* Set program pointer to page start */
			if (mtd->writesize == 512)
				send_cmd(host, NAND_CMD_READ0, false);
		}

		send_cmd(host, command, false);
		mxc_do_addr_cycle(mtd, column, page_addr);
		break;

	case NAND_CMD_PAGEPROG:
		memcpy(host->main_area0, host->data_buf, mtd->writesize);
		copy_spare(mtd, false);
		send_page(mtd, NFC_INPUT);
		send_cmd(host, command, true);
		mxc_do_addr_cycle(mtd, column, page_addr);
		break;

	case NAND_CMD_READID:
		send_cmd(host, command, true);
		mxc_do_addr_cycle(mtd, column, page_addr);
		send_read_id(host);
		host->buf_start = column;
		break;

	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
		send_cmd(host, command, false);
		mxc_do_addr_cycle(mtd, column, page_addr);

		break;
	}
}

/*
 * The generic flash bbt decriptors overlap with our ecc
 * hardware, so define some i.MX specific ones.
 */
static uint8_t bbt_pattern[] = { 'B', 'b', 't', '0' };
static uint8_t mirror_pattern[] = { '1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
	    | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 0,
	.len = 4,
	.veroffs = 4,
	.maxblocks = 4,
	.pattern = bbt_pattern,
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
	    | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 0,
	.len = 4,
	.veroffs = 4,
	.maxblocks = 4,
	.pattern = mirror_pattern,
};

static int __init mxcnd_probe(struct platform_device *pdev)
{
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct mxc_nand_platform_data *pdata = pdev->dev.platform_data;
	struct mxc_nand_host *host;
	struct resource *res;
	uint16_t tmp;
	int err = 0, nr_parts = 0;
	struct nand_ecclayout *oob_smallpage, *oob_largepage;

	/* Allocate memory for MTD device structure and private data */
	host = kzalloc(sizeof(struct mxc_nand_host) + NAND_MAX_PAGESIZE +
			NAND_MAX_OOBSIZE, GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->data_buf = (uint8_t *)(host + 1);

	host->dev = &pdev->dev;
	/* structures must be linked */
	this = &host->nand;
	mtd = &host->mtd;
	mtd->priv = this;
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = &pdev->dev;
	mtd->name = DRIVER_NAME;

	/* 50 us command delay time */
	this->chip_delay = 5;

	this->priv = host;
	this->dev_ready = mxc_nand_dev_ready;
	this->cmdfunc = mxc_nand_command;
	this->select_chip = mxc_nand_select_chip;
	this->read_byte = mxc_nand_read_byte;
	this->read_word = mxc_nand_read_word;
	this->write_buf = mxc_nand_write_buf;
	this->read_buf = mxc_nand_read_buf;
	this->verify_buf = mxc_nand_verify_buf;

	host->clk = clk_get(&pdev->dev, "nfc");
	if (IS_ERR(host->clk)) {
		err = PTR_ERR(host->clk);
		goto eclk;
	}

	clk_enable(host->clk);
	host->clk_act = 1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENODEV;
		goto eres;
	}

	host->base = ioremap(res->start, resource_size(res));
	if (!host->base) {
		err = -ENOMEM;
		goto eres;
	}

	host->main_area0 = host->base;
	host->main_area1 = host->base + 0x200;

	if (nfc_is_v21()) {
		host->regs = host->base + 0x1000;
		host->spare0 = host->base + 0x1000;
		host->spare_len = 64;
		oob_smallpage = &nandv2_hw_eccoob_smallpage;
		oob_largepage = &nandv2_hw_eccoob_largepage;
	} else if (nfc_is_v1()) {
		host->regs = host->base;
		host->spare0 = host->base + 0x800;
		host->spare_len = 16;
		oob_smallpage = &nandv1_hw_eccoob_smallpage;
		oob_largepage = &nandv1_hw_eccoob_largepage;
	} else
		BUG();

	/* disable interrupt and spare enable */
	tmp = readw(host->regs + NFC_CONFIG1);
	tmp |= NFC_INT_MSK;
	tmp &= ~NFC_SP_EN;
	writew(tmp, host->regs + NFC_CONFIG1);

	init_waitqueue_head(&host->irq_waitq);

	host->irq = platform_get_irq(pdev, 0);

	err = request_irq(host->irq, mxc_nfc_irq, 0, DRIVER_NAME, host);
	if (err)
		goto eirq;

	/* Reset NAND */
	this->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);

	/* preset operation */
	/* Unlock the internal RAM Buffer */
	writew(0x2, host->regs + NFC_CONFIG);

	/* Blocks to be unlocked */
	if (nfc_is_v21()) {
		writew(0x0, host->regs + NFC_V21_UNLOCKSTART_BLKADDR);
	        writew(0xffff, host->regs + NFC_V21_UNLOCKEND_BLKADDR);
		this->ecc.bytes = 9;
	} else if (nfc_is_v1()) {
		writew(0x0, host->regs + NFC_V1_UNLOCKSTART_BLKADDR);
	        writew(0x4000, host->regs + NFC_V1_UNLOCKEND_BLKADDR);
		this->ecc.bytes = 3;
	} else
		BUG();

	/* Unlock Block Command for given address range */
	writew(0x4, host->regs + NFC_WRPROT);

	this->ecc.size = 512;
	this->ecc.layout = oob_smallpage;

	if (pdata->hw_ecc) {
		this->ecc.calculate = mxc_nand_calculate_ecc;
		this->ecc.hwctl = mxc_nand_enable_hwecc;
		this->ecc.correct = mxc_nand_correct_data;
		this->ecc.mode = NAND_ECC_HW;
		tmp = readw(host->regs + NFC_CONFIG1);
		tmp |= NFC_ECC_EN;
		writew(tmp, host->regs + NFC_CONFIG1);
	} else {
		this->ecc.mode = NAND_ECC_SOFT;
		tmp = readw(host->regs + NFC_CONFIG1);
		tmp &= ~NFC_ECC_EN;
		writew(tmp, host->regs + NFC_CONFIG1);
	}

	/* NAND bus width determines access funtions used by upper layer */
	if (pdata->width == 2)
		this->options |= NAND_BUSWIDTH_16;

	if (pdata->flash_bbt) {
		this->bbt_td = &bbt_main_descr;
		this->bbt_md = &bbt_mirror_descr;
		/* update flash based bbt */
		this->options |= NAND_USE_FLASH_BBT;
	}

	/* first scan to find the device and get the page size */
	if (nand_scan_ident(mtd, 1)) {
		err = -ENXIO;
		goto escan;
	}

	if (mtd->writesize == 2048)
		this->ecc.layout = oob_largepage;

	/* second phase scan */
	if (nand_scan_tail(mtd)) {
		err = -ENXIO;
		goto escan;
	}

	/* Register the partitions */
#ifdef CONFIG_MTD_PARTITIONS
	nr_parts =
	    parse_mtd_partitions(mtd, part_probes, &host->parts, 0);
	if (nr_parts > 0)
		add_mtd_partitions(mtd, host->parts, nr_parts);
	else
#endif
	{
		pr_info("Registering %s as whole device\n", mtd->name);
		add_mtd_device(mtd);
	}

	platform_set_drvdata(pdev, host);

	return 0;

escan:
	free_irq(host->irq, host);
eirq:
	iounmap(host->base);
eres:
	clk_put(host->clk);
eclk:
	kfree(host);

	return err;
}

static int __devexit mxcnd_remove(struct platform_device *pdev)
{
	struct mxc_nand_host *host = platform_get_drvdata(pdev);

	clk_put(host->clk);

	platform_set_drvdata(pdev, NULL);

	nand_release(&host->mtd);
	free_irq(host->irq, host);
	iounmap(host->base);
	kfree(host);

	return 0;
}

#ifdef CONFIG_PM
static int mxcnd_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "MXC_ND : NAND suspend\n");

	ret = mtd->suspend(mtd);

	/*
	 * nand_suspend locks the device for exclusive access, so
	 * the clock must already be off.
	 */
	BUG_ON(!ret && host->clk_act);

	return ret;
}

static int mxcnd_resume(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "MXC_ND : NAND resume\n");

	mtd->resume(mtd);

	return ret;
}

#else
# define mxcnd_suspend   NULL
# define mxcnd_resume    NULL
#endif				/* CONFIG_PM */

static struct platform_driver mxcnd_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   },
	.remove = __devexit_p(mxcnd_remove),
	.suspend = mxcnd_suspend,
	.resume = mxcnd_resume,
};

static int __init mxc_nd_init(void)
{
	return platform_driver_probe(&mxcnd_driver, mxcnd_probe);
}

static void __exit mxc_nd_cleanup(void)
{
	/* Unregister the device structure */
	platform_driver_unregister(&mxcnd_driver);
}

module_init(mxc_nd_init);
module_exit(mxc_nd_cleanup);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXC NAND MTD driver");
MODULE_LICENSE("GPL");
