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

#define DRIVER_NAME "mxc_nand"

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
#define NFC_UNLOCKSTART_BLKADDR	0xE14
#define NFC_UNLOCKEND_BLKADDR	0xE16
#define NFC_NF_WRPRST		0xE18
#define NFC_CONFIG1		0xE1A
#define NFC_CONFIG2		0xE1C

/* Addresses for NFC RAM BUFFER Main area 0 */
#define MAIN_AREA0		0x000
#define MAIN_AREA1		0x200
#define MAIN_AREA2		0x400
#define MAIN_AREA3		0x600

/* Addresses for NFC SPARE BUFFER Spare area 0 */
#define SPARE_AREA0		0x800
#define SPARE_AREA1		0x810
#define SPARE_AREA2		0x820
#define SPARE_AREA3		0x830

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

	void __iomem		*regs;
	int			spare_only;
	int			status_request;
	int			pagesize_2k;
	uint16_t		col_addr;
	struct clk		*clk;
	int			clk_act;
	int			irq;

	wait_queue_head_t	irq_waitq;
};

/* Define delays in microsec for NAND device operations */
#define TROP_US_DELAY   2000
/* Macros to get byte and bit positions of ECC */
#define COLPOS(x)  ((x) >> 3)
#define BITPOS(x) ((x) & 0xf)

/* Define single bit Error positions in Main & Spare area */
#define MAIN_SINGLEBIT_ERROR 0x4
#define SPARE_SINGLEBIT_ERROR 0x1

/* OOB placement block for use with hardware ecc generation */
static struct nand_ecclayout nand_hw_eccoob_8 = {
	.eccbytes = 5,
	.eccpos = {6, 7, 8, 9, 10},
	.oobfree = {{0, 5}, {11, 5}, }
};

static struct nand_ecclayout nand_hw_eccoob_16 = {
	.eccbytes = 5,
	.eccpos = {6, 7, 8, 9, 10},
	.oobfree = {{0, 5}, {11, 5}, }
};

static struct nand_ecclayout nand_hw_eccoob_64 = {
	.eccbytes = 20,
	.eccpos = {6, 7, 8, 9, 10, 22, 23, 24, 25, 26,
		   38, 39, 40, 41, 42, 54, 55, 56, 57, 58},
	.oobfree = {{2, 4}, {11, 10}, {27, 10}, {43, 10}, {59, 5}, }
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
static void wait_op_done(struct mxc_nand_host *host, int max_retries,
				uint16_t param, int useirq)
{
	uint32_t tmp;

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
			DEBUG(MTD_DEBUG_LEVEL0, "%s(%d): INT not set\n",
			      __func__, param);
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
	wait_op_done(host, TROP_US_DELAY, cmd, useirq);
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
	wait_op_done(host, TROP_US_DELAY, addr, islast);
}

/* This function requests the NANDFC to initate the transfer
 * of data currently in the NANDFC RAM buffer to the NAND device. */
static void send_prog_page(struct mxc_nand_host *host, uint8_t buf_id,
			int spare_only)
{
	DEBUG(MTD_DEBUG_LEVEL3, "send_prog_page (%d)\n", spare_only);

	/* NANDFC buffer 0 is used for page read/write */
	writew(buf_id, host->regs + NFC_BUF_ADDR);

	/* Configure spare or page+spare access */
	if (!host->pagesize_2k) {
		uint16_t config1 = readw(host->regs + NFC_CONFIG1);
		if (spare_only)
			config1 |= NFC_SP_EN;
		else
			config1 &= ~(NFC_SP_EN);
		writew(config1, host->regs + NFC_CONFIG1);
	}

	writew(NFC_INPUT, host->regs + NFC_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, TROP_US_DELAY, spare_only, true);
}

/* Requests NANDFC to initated the transfer of data from the
 * NAND device into in the NANDFC ram buffer. */
static void send_read_page(struct mxc_nand_host *host, uint8_t buf_id,
		int spare_only)
{
	DEBUG(MTD_DEBUG_LEVEL3, "send_read_page (%d)\n", spare_only);

	/* NANDFC buffer 0 is used for page read/write */
	writew(buf_id, host->regs + NFC_BUF_ADDR);

	/* Configure spare or page+spare access */
	if (!host->pagesize_2k) {
		uint32_t config1 = readw(host->regs + NFC_CONFIG1);
		if (spare_only)
			config1 |= NFC_SP_EN;
		else
			config1 &= ~NFC_SP_EN;
		writew(config1, host->regs + NFC_CONFIG1);
	}

	writew(NFC_OUTPUT, host->regs + NFC_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, TROP_US_DELAY, spare_only, true);
}

/* Request the NANDFC to perform a read of the NAND device ID. */
static void send_read_id(struct mxc_nand_host *host)
{
	struct nand_chip *this = &host->nand;
	uint16_t tmp;

	/* NANDFC buffer 0 is used for device ID output */
	writew(0x0, host->regs + NFC_BUF_ADDR);

	/* Read ID into main buffer */
	tmp = readw(host->regs + NFC_CONFIG1);
	tmp &= ~NFC_SP_EN;
	writew(tmp, host->regs + NFC_CONFIG1);

	writew(NFC_ID, host->regs + NFC_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, TROP_US_DELAY, 0, true);

	if (this->options & NAND_BUSWIDTH_16) {
		void __iomem *main_buf = host->regs + MAIN_AREA0;
		/* compress the ID info */
		writeb(readb(main_buf + 2), main_buf + 1);
		writeb(readb(main_buf + 4), main_buf + 2);
		writeb(readb(main_buf + 6), main_buf + 3);
		writeb(readb(main_buf + 8), main_buf + 4);
		writeb(readb(main_buf + 10), main_buf + 5);
	}
}

/* This function requests the NANDFC to perform a read of the
 * NAND device status and returns the current status. */
static uint16_t get_dev_status(struct mxc_nand_host *host)
{
	void __iomem *main_buf = host->regs + MAIN_AREA1;
	uint32_t store;
	uint16_t ret, tmp;
	/* Issue status request to NAND device */

	/* store the main area1 first word, later do recovery */
	store = readl(main_buf);
	/* NANDFC buffer 1 is used for device status to prevent
	 * corruption of read/write buffer on status requests. */
	writew(1, host->regs + NFC_BUF_ADDR);

	/* Read status into main buffer */
	tmp = readw(host->regs + NFC_CONFIG1);
	tmp &= ~NFC_SP_EN;
	writew(tmp, host->regs + NFC_CONFIG1);

	writew(NFC_STATUS, host->regs + NFC_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, TROP_US_DELAY, 0, true);

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
	uint8_t ret = 0;
	uint16_t col, rd_word;
	uint16_t __iomem *main_buf = host->regs + MAIN_AREA0;
	uint16_t __iomem *spare_buf = host->regs + SPARE_AREA0;

	/* Check for status request */
	if (host->status_request)
		return get_dev_status(host) & 0xFF;

	/* Get column for 16-bit access */
	col = host->col_addr >> 1;

	/* If we are accessing the spare region */
	if (host->spare_only)
		rd_word = readw(&spare_buf[col]);
	else
		rd_word = readw(&main_buf[col]);

	/* Pick upper/lower byte of word from RAM buffer */
	if (host->col_addr & 0x1)
		ret = (rd_word >> 8) & 0xFF;
	else
		ret = rd_word & 0xFF;

	/* Update saved column address */
	host->col_addr++;

	return ret;
}

static uint16_t mxc_nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	uint16_t col, rd_word, ret;
	uint16_t __iomem *p;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "mxc_nand_read_word(col = %d)\n", host->col_addr);

	col = host->col_addr;
	/* Adjust saved column address */
	if (col < mtd->writesize && host->spare_only)
		col += mtd->writesize;

	if (col < mtd->writesize)
		p = (host->regs + MAIN_AREA0) + (col >> 1);
	else
		p = (host->regs + SPARE_AREA0) + ((col - mtd->writesize) >> 1);

	if (col & 1) {
		rd_word = readw(p);
		ret = (rd_word >> 8) & 0xff;
		rd_word = readw(&p[1]);
		ret |= (rd_word << 8) & 0xff00;

	} else
		ret = readw(p);

	/* Update saved column address */
	host->col_addr = col + 2;

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
	int n, col, i = 0;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "mxc_nand_write_buf(col = %d, len = %d)\n", host->col_addr,
	      len);

	col = host->col_addr;

	/* Adjust saved column address */
	if (col < mtd->writesize && host->spare_only)
		col += mtd->writesize;

	n = mtd->writesize + mtd->oobsize - col;
	n = min(len, n);

	DEBUG(MTD_DEBUG_LEVEL3,
	      "%s:%d: col = %d, n = %d\n", __func__, __LINE__, col, n);

	while (n) {
		void __iomem *p;

		if (col < mtd->writesize)
			p = host->regs + MAIN_AREA0 + (col & ~3);
		else
			p = host->regs + SPARE_AREA0 -
						mtd->writesize + (col & ~3);

		DEBUG(MTD_DEBUG_LEVEL3, "%s:%d: p = %p\n", __func__,
		      __LINE__, p);

		if (((col | (int)&buf[i]) & 3) || n < 16) {
			uint32_t data = 0;

			if (col & 3 || n < 4)
				data = readl(p);

			switch (col & 3) {
			case 0:
				if (n) {
					data = (data & 0xffffff00) |
					    (buf[i++] << 0);
					n--;
					col++;
				}
			case 1:
				if (n) {
					data = (data & 0xffff00ff) |
					    (buf[i++] << 8);
					n--;
					col++;
				}
			case 2:
				if (n) {
					data = (data & 0xff00ffff) |
					    (buf[i++] << 16);
					n--;
					col++;
				}
			case 3:
				if (n) {
					data = (data & 0x00ffffff) |
					    (buf[i++] << 24);
					n--;
					col++;
				}
			}

			writel(data, p);
		} else {
			int m = mtd->writesize - col;

			if (col >= mtd->writesize)
				m += mtd->oobsize;

			m = min(n, m) & ~3;

			DEBUG(MTD_DEBUG_LEVEL3,
			      "%s:%d: n = %d, m = %d, i = %d, col = %d\n",
			      __func__,  __LINE__, n, m, i, col);

			memcpy(p, &buf[i], m);
			col += m;
			i += m;
			n -= m;
		}
	}
	/* Update saved column address */
	host->col_addr = col;
}

/* Read the data buffer from the NAND Flash. To read the data from NAND
 * Flash first the data output cycle is initiated by the NFC, which copies
 * the data to RAMbuffer. This data of length len is then copied to buffer buf.
 */
static void mxc_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	int n, col, i = 0;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "mxc_nand_read_buf(col = %d, len = %d)\n", host->col_addr, len);

	col = host->col_addr;

	/* Adjust saved column address */
	if (col < mtd->writesize && host->spare_only)
		col += mtd->writesize;

	n = mtd->writesize + mtd->oobsize - col;
	n = min(len, n);

	while (n) {
		void __iomem *p;

		if (col < mtd->writesize)
			p = host->regs + MAIN_AREA0 + (col & ~3);
		else
			p = host->regs + SPARE_AREA0 -
					mtd->writesize + (col & ~3);

		if (((col | (int)&buf[i]) & 3) || n < 16) {
			uint32_t data;

			data = readl(p);
			switch (col & 3) {
			case 0:
				if (n) {
					buf[i++] = (uint8_t) (data);
					n--;
					col++;
				}
			case 1:
				if (n) {
					buf[i++] = (uint8_t) (data >> 8);
					n--;
					col++;
				}
			case 2:
				if (n) {
					buf[i++] = (uint8_t) (data >> 16);
					n--;
					col++;
				}
			case 3:
				if (n) {
					buf[i++] = (uint8_t) (data >> 24);
					n--;
					col++;
				}
			}
		} else {
			int m = mtd->writesize - col;

			if (col >= mtd->writesize)
				m += mtd->oobsize;

			m = min(n, m) & ~3;
			memcpy(&buf[i], p, m);
			col += m;
			i += m;
			n -= m;
		}
	}
	/* Update saved column address */
	host->col_addr = col;

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

#ifdef CONFIG_MTD_NAND_MXC_FORCE_CE
	if (chip > 0) {
		DEBUG(MTD_DEBUG_LEVEL0,
		      "ERROR:  Illegal chip select (chip = %d)\n", chip);
		return;
	}

	if (chip == -1) {
		writew(readw(host->regs + NFC_CONFIG1) & ~NFC_CE,
				host->regs + NFC_CONFIG1);
		return;
	}

	writew(readw(host->regs + NFC_CONFIG1) | NFC_CE,
			host->regs + NFC_CONFIG1);
#endif

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

/* Used by the upper layer to write command to NAND Flash for
 * different operations to be carried out on NAND Flash */
static void mxc_nand_command(struct mtd_info *mtd, unsigned command,
				int column, int page_addr)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	int useirq = true;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "mxc_nand_command (cmd = 0x%x, col = 0x%x, page = 0x%x)\n",
	      command, column, page_addr);

	/* Reset command state information */
	host->status_request = false;

	/* Command pre-processing step */
	switch (command) {

	case NAND_CMD_STATUS:
		host->col_addr = 0;
		host->status_request = true;
		break;

	case NAND_CMD_READ0:
		host->col_addr = column;
		host->spare_only = false;
		useirq = false;
		break;

	case NAND_CMD_READOOB:
		host->col_addr = column;
		host->spare_only = true;
		useirq = false;
		if (host->pagesize_2k)
			command = NAND_CMD_READ0; /* only READ0 is valid */
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
			if (host->pagesize_2k)
				/* call ourself to read a page */
				mxc_nand_command(mtd, NAND_CMD_READ0, 0,
						page_addr);

			host->col_addr = column - mtd->writesize;
			host->spare_only = true;

			/* Set program pointer to spare region */
			if (!host->pagesize_2k)
				send_cmd(host, NAND_CMD_READOOB, false);
		} else {
			host->spare_only = false;
			host->col_addr = column;

			/* Set program pointer to page start */
			if (!host->pagesize_2k)
				send_cmd(host, NAND_CMD_READ0, false);
		}
		useirq = false;
		break;

	case NAND_CMD_PAGEPROG:
		send_prog_page(host, 0, host->spare_only);

		if (host->pagesize_2k) {
			/* data in 4 areas datas */
			send_prog_page(host, 1, host->spare_only);
			send_prog_page(host, 2, host->spare_only);
			send_prog_page(host, 3, host->spare_only);
		}

		break;

	case NAND_CMD_ERASE1:
		useirq = false;
		break;
	}

	/* Write out the command to the device. */
	send_cmd(host, command, useirq);

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
		if (host->pagesize_2k)
			/* another col addr cycle for 2k page */
			send_addr(host, 0, false);
	}

	/* Write out page address, if necessary */
	if (page_addr != -1) {
		/* paddr_0 - p_addr_7 */
		send_addr(host, (page_addr & 0xff), false);

		if (host->pagesize_2k) {
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

	/* Command post-processing step */
	switch (command) {

	case NAND_CMD_RESET:
		break;

	case NAND_CMD_READOOB:
	case NAND_CMD_READ0:
		if (host->pagesize_2k) {
			/* send read confirm command */
			send_cmd(host, NAND_CMD_READSTART, true);
			/* read for each AREA */
			send_read_page(host, 0, host->spare_only);
			send_read_page(host, 1, host->spare_only);
			send_read_page(host, 2, host->spare_only);
			send_read_page(host, 3, host->spare_only);
		} else
			send_read_page(host, 0, host->spare_only);
		break;

	case NAND_CMD_READID:
		host->col_addr = 0;
		send_read_id(host);
		break;

	case NAND_CMD_PAGEPROG:
		break;

	case NAND_CMD_STATUS:
		break;

	case NAND_CMD_ERASE2:
		break;
	}
}

static int __init mxcnd_probe(struct platform_device *pdev)
{
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct mxc_nand_platform_data *pdata = pdev->dev.platform_data;
	struct mxc_nand_host *host;
	struct resource *res;
	uint16_t tmp;
	int err = 0, nr_parts = 0;

	/* Allocate memory for MTD device structure and private data */
	host = kzalloc(sizeof(struct mxc_nand_host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = &pdev->dev;
	/* structures must be linked */
	this = &host->nand;
	mtd = &host->mtd;
	mtd->priv = this;
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = &pdev->dev;
	mtd->name = "mxc_nand";

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

	host->regs = ioremap(res->start, res->end - res->start + 1);
	if (!host->regs) {
		err = -ENOMEM;
		goto eres;
	}

	tmp = readw(host->regs + NFC_CONFIG1);
	tmp |= NFC_INT_MSK;
	writew(tmp, host->regs + NFC_CONFIG1);

	init_waitqueue_head(&host->irq_waitq);

	host->irq = platform_get_irq(pdev, 0);

	err = request_irq(host->irq, mxc_nfc_irq, 0, "mxc_nd", host);
	if (err)
		goto eirq;

	if (pdata->hw_ecc) {
		this->ecc.calculate = mxc_nand_calculate_ecc;
		this->ecc.hwctl = mxc_nand_enable_hwecc;
		this->ecc.correct = mxc_nand_correct_data;
		this->ecc.mode = NAND_ECC_HW;
		this->ecc.size = 512;
		this->ecc.bytes = 3;
		tmp = readw(host->regs + NFC_CONFIG1);
		tmp |= NFC_ECC_EN;
		writew(tmp, host->regs + NFC_CONFIG1);
	} else {
		this->ecc.size = 512;
		this->ecc.bytes = 3;
		this->ecc.layout = &nand_hw_eccoob_8;
		this->ecc.mode = NAND_ECC_SOFT;
		tmp = readw(host->regs + NFC_CONFIG1);
		tmp &= ~NFC_ECC_EN;
		writew(tmp, host->regs + NFC_CONFIG1);
	}

	/* Reset NAND */
	this->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);

	/* preset operation */
	/* Unlock the internal RAM Buffer */
	writew(0x2, host->regs + NFC_CONFIG);

	/* Blocks to be unlocked */
	writew(0x0, host->regs + NFC_UNLOCKSTART_BLKADDR);
	writew(0x4000, host->regs + NFC_UNLOCKEND_BLKADDR);

	/* Unlock Block Command for given address range */
	writew(0x4, host->regs + NFC_WRPROT);

	/* NAND bus width determines access funtions used by upper layer */
	if (pdata->width == 2) {
		this->options |= NAND_BUSWIDTH_16;
		this->ecc.layout = &nand_hw_eccoob_16;
	}

	/* first scan to find the device and get the page size */
	if (nand_scan_ident(mtd, 1)) {
		err = -ENXIO;
		goto escan;
	}

	host->pagesize_2k = (mtd->writesize == 2048) ? 1 : 0;

	if (this->ecc.mode == NAND_ECC_HW) {
		switch (mtd->oobsize) {
		case 8:
			this->ecc.layout = &nand_hw_eccoob_8;
			break;
		case 16:
			this->ecc.layout = &nand_hw_eccoob_16;
			break;
		case 64:
			this->ecc.layout = &nand_hw_eccoob_64;
			break;
		default:
			/* page size not handled by HW ECC */
			/* switching back to soft ECC */
			this->ecc.size = 512;
			this->ecc.bytes = 3;
			this->ecc.layout = &nand_hw_eccoob_8;
			this->ecc.mode = NAND_ECC_SOFT;
			this->ecc.calculate = NULL;
			this->ecc.correct = NULL;
			this->ecc.hwctl = NULL;
			tmp = readw(host->regs + NFC_CONFIG1);
			tmp &= ~NFC_ECC_EN;
			writew(tmp, host->regs + NFC_CONFIG1);
			break;
		}
	}

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
	iounmap(host->regs);
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
	iounmap(host->regs);
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
	if (mtd) {
		ret = mtd->suspend(mtd);
		/* Disable the NFC clock */
		clk_disable(host->clk);
	}

	return ret;
}

static int mxcnd_resume(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "MXC_ND : NAND resume\n");

	if (mtd) {
		/* Enable the NFC clock */
		clk_enable(host->clk);
		mtd->resume(mtd);
	}

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
	.remove = __exit_p(mxcnd_remove),
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
