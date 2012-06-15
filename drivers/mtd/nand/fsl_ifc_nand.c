/*
 * Freescale Integrated Flash Controller NAND driver
 *
 * Copyright 2011-2012 Freescale Semiconductor, Inc
 *
 * Author: Dipen Dudhat <Dipen.Dudhat@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand_ecc.h>
#include <asm/fsl_ifc.h>

#define ERR_BYTE		0xFF /* Value returned for read
					bytes when read failed	*/
#define IFC_TIMEOUT_MSECS	500  /* Maximum number of mSecs to wait
					for IFC NAND Machine	*/

struct fsl_ifc_ctrl;

/* mtd information per set */
struct fsl_ifc_mtd {
	struct mtd_info mtd;
	struct nand_chip chip;
	struct fsl_ifc_ctrl *ctrl;

	struct device *dev;
	int bank;		/* Chip select bank number		*/
	unsigned int bufnum_mask; /* bufnum = page & bufnum_mask */
	u8 __iomem *vbase;      /* Chip select base virtual address	*/
};

/* overview of the fsl ifc controller */
struct fsl_ifc_nand_ctrl {
	struct nand_hw_control controller;
	struct fsl_ifc_mtd *chips[FSL_IFC_BANK_COUNT];

	u8 __iomem *addr;	/* Address of assigned IFC buffer	*/
	unsigned int page;	/* Last page written to / read from	*/
	unsigned int read_bytes;/* Number of bytes read during command	*/
	unsigned int column;	/* Saved column from SEQIN		*/
	unsigned int index;	/* Pointer to next byte to 'read'	*/
	unsigned int oob;	/* Non zero if operating on OOB data	*/
	unsigned int eccread;	/* Non zero for a full-page ECC read	*/
	unsigned int counter;	/* counter for the initializations	*/
	unsigned int max_bitflips;  /* Saved during READ0 cmd		*/
};

static struct fsl_ifc_nand_ctrl *ifc_nand_ctrl;

/* 512-byte page with 4-bit ECC, 8-bit */
static struct nand_ecclayout oob_512_8bit_ecc4 = {
	.eccbytes = 8,
	.eccpos = {8, 9, 10, 11, 12, 13, 14, 15},
	.oobfree = { {0, 5}, {6, 2} },
};

/* 512-byte page with 4-bit ECC, 16-bit */
static struct nand_ecclayout oob_512_16bit_ecc4 = {
	.eccbytes = 8,
	.eccpos = {8, 9, 10, 11, 12, 13, 14, 15},
	.oobfree = { {2, 6}, },
};

/* 2048-byte page size with 4-bit ECC */
static struct nand_ecclayout oob_2048_ecc4 = {
	.eccbytes = 32,
	.eccpos = {
		8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23,
		24, 25, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39,
	},
	.oobfree = { {2, 6}, {40, 24} },
};

/* 4096-byte page size with 4-bit ECC */
static struct nand_ecclayout oob_4096_ecc4 = {
	.eccbytes = 64,
	.eccpos = {
		8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23,
		24, 25, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39,
		40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55,
		56, 57, 58, 59, 60, 61, 62, 63,
		64, 65, 66, 67, 68, 69, 70, 71,
	},
	.oobfree = { {2, 6}, {72, 56} },
};

/* 4096-byte page size with 8-bit ECC -- requires 218-byte OOB */
static struct nand_ecclayout oob_4096_ecc8 = {
	.eccbytes = 128,
	.eccpos = {
		8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23,
		24, 25, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39,
		40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55,
		56, 57, 58, 59, 60, 61, 62, 63,
		64, 65, 66, 67, 68, 69, 70, 71,
		72, 73, 74, 75, 76, 77, 78, 79,
		80, 81, 82, 83, 84, 85, 86, 87,
		88, 89, 90, 91, 92, 93, 94, 95,
		96, 97, 98, 99, 100, 101, 102, 103,
		104, 105, 106, 107, 108, 109, 110, 111,
		112, 113, 114, 115, 116, 117, 118, 119,
		120, 121, 122, 123, 124, 125, 126, 127,
		128, 129, 130, 131, 132, 133, 134, 135,
	},
	.oobfree = { {2, 6}, {136, 82} },
};


/*
 * Generic flash bbt descriptors
 */
static u8 bbt_pattern[] = {'B', 'b', 't', '0' };
static u8 mirror_pattern[] = {'1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE |
		   NAND_BBT_2BIT | NAND_BBT_VERSION,
	.offs =	2, /* 0 on 8-bit small page */
	.len = 4,
	.veroffs = 6,
	.maxblocks = 4,
	.pattern = bbt_pattern,
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE |
		   NAND_BBT_2BIT | NAND_BBT_VERSION,
	.offs =	2, /* 0 on 8-bit small page */
	.len = 4,
	.veroffs = 6,
	.maxblocks = 4,
	.pattern = mirror_pattern,
};

/*
 * Set up the IFC hardware block and page address fields, and the ifc nand
 * structure addr field to point to the correct IFC buffer in memory
 */
static void set_addr(struct mtd_info *mtd, int column, int page_addr, int oob)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;
	int buf_num;

	ifc_nand_ctrl->page = page_addr;
	/* Program ROW0/COL0 */
	out_be32(&ifc->ifc_nand.row0, page_addr);
	out_be32(&ifc->ifc_nand.col0, (oob ? IFC_NAND_COL_MS : 0) | column);

	buf_num = page_addr & priv->bufnum_mask;

	ifc_nand_ctrl->addr = priv->vbase + buf_num * (mtd->writesize * 2);
	ifc_nand_ctrl->index = column;

	/* for OOB data point to the second half of the buffer */
	if (oob)
		ifc_nand_ctrl->index += mtd->writesize;
}

static int is_blank(struct mtd_info *mtd, unsigned int bufnum)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;
	u8 __iomem *addr = priv->vbase + bufnum * (mtd->writesize * 2);
	u32 __iomem *mainarea = (u32 *)addr;
	u8 __iomem *oob = addr + mtd->writesize;
	int i;

	for (i = 0; i < mtd->writesize / 4; i++) {
		if (__raw_readl(&mainarea[i]) != 0xffffffff)
			return 0;
	}

	for (i = 0; i < chip->ecc.layout->eccbytes; i++) {
		int pos = chip->ecc.layout->eccpos[i];

		if (__raw_readb(&oob[pos]) != 0xff)
			return 0;
	}

	return 1;
}

/* returns nonzero if entire page is blank */
static int check_read_ecc(struct mtd_info *mtd, struct fsl_ifc_ctrl *ctrl,
			  u32 *eccstat, unsigned int bufnum)
{
	u32 reg = eccstat[bufnum / 4];
	int errors;

	errors = (reg >> ((3 - bufnum % 4) * 8)) & 15;

	return errors;
}

/*
 * execute IFC NAND command and wait for it to complete
 */
static void fsl_ifc_run_command(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_nand_ctrl *nctrl = ifc_nand_ctrl;
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;
	u32 eccstat[4];
	int i;

	/* set the chip select for NAND Transaction */
	out_be32(&ifc->ifc_nand.nand_csel, priv->bank << IFC_NAND_CSEL_SHIFT);

	dev_vdbg(priv->dev,
			"%s: fir0=%08x fcr0=%08x\n",
			__func__,
			in_be32(&ifc->ifc_nand.nand_fir0),
			in_be32(&ifc->ifc_nand.nand_fcr0));

	ctrl->nand_stat = 0;

	/* start read/write seq */
	out_be32(&ifc->ifc_nand.nandseq_strt, IFC_NAND_SEQ_STRT_FIR_STRT);

	/* wait for command complete flag or timeout */
	wait_event_timeout(ctrl->nand_wait, ctrl->nand_stat,
			   IFC_TIMEOUT_MSECS * HZ/1000);

	/* ctrl->nand_stat will be updated from IRQ context */
	if (!ctrl->nand_stat)
		dev_err(priv->dev, "Controller is not responding\n");
	if (ctrl->nand_stat & IFC_NAND_EVTER_STAT_FTOER)
		dev_err(priv->dev, "NAND Flash Timeout Error\n");
	if (ctrl->nand_stat & IFC_NAND_EVTER_STAT_WPER)
		dev_err(priv->dev, "NAND Flash Write Protect Error\n");

	nctrl->max_bitflips = 0;

	if (nctrl->eccread) {
		int errors;
		int bufnum = nctrl->page & priv->bufnum_mask;
		int sector = bufnum * chip->ecc.steps;
		int sector_end = sector + chip->ecc.steps - 1;

		for (i = sector / 4; i <= sector_end / 4; i++)
			eccstat[i] = in_be32(&ifc->ifc_nand.nand_eccstat[i]);

		for (i = sector; i <= sector_end; i++) {
			errors = check_read_ecc(mtd, ctrl, eccstat, i);

			if (errors == 15) {
				/*
				 * Uncorrectable error.
				 * OK only if the whole page is blank.
				 *
				 * We disable ECCER reporting due to...
				 * erratum IFC-A002770 -- so report it now if we
				 * see an uncorrectable error in ECCSTAT.
				 */
				if (!is_blank(mtd, bufnum))
					ctrl->nand_stat |=
						IFC_NAND_EVTER_STAT_ECCER;
				break;
			}

			mtd->ecc_stats.corrected += errors;
			nctrl->max_bitflips = max_t(unsigned int,
						    nctrl->max_bitflips,
						    errors);
		}

		nctrl->eccread = 0;
	}
}

static void fsl_ifc_do_read(struct nand_chip *chip,
			    int oob,
			    struct mtd_info *mtd)
{
	struct fsl_ifc_mtd *priv = chip->priv;
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;

	/* Program FIR/IFC_NAND_FCR0 for Small/Large page */
	if (mtd->writesize > 512) {
		out_be32(&ifc->ifc_nand.nand_fir0,
			 (IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
			 (IFC_FIR_OP_CA0 << IFC_NAND_FIR0_OP1_SHIFT) |
			 (IFC_FIR_OP_RA0 << IFC_NAND_FIR0_OP2_SHIFT) |
			 (IFC_FIR_OP_CMD1 << IFC_NAND_FIR0_OP3_SHIFT) |
			 (IFC_FIR_OP_RBCD << IFC_NAND_FIR0_OP4_SHIFT));
		out_be32(&ifc->ifc_nand.nand_fir1, 0x0);

		out_be32(&ifc->ifc_nand.nand_fcr0,
			(NAND_CMD_READ0 << IFC_NAND_FCR0_CMD0_SHIFT) |
			(NAND_CMD_READSTART << IFC_NAND_FCR0_CMD1_SHIFT));
	} else {
		out_be32(&ifc->ifc_nand.nand_fir0,
			 (IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
			 (IFC_FIR_OP_CA0 << IFC_NAND_FIR0_OP1_SHIFT) |
			 (IFC_FIR_OP_RA0  << IFC_NAND_FIR0_OP2_SHIFT) |
			 (IFC_FIR_OP_RBCD << IFC_NAND_FIR0_OP3_SHIFT));
		out_be32(&ifc->ifc_nand.nand_fir1, 0x0);

		if (oob)
			out_be32(&ifc->ifc_nand.nand_fcr0,
				 NAND_CMD_READOOB << IFC_NAND_FCR0_CMD0_SHIFT);
		else
			out_be32(&ifc->ifc_nand.nand_fcr0,
				NAND_CMD_READ0 << IFC_NAND_FCR0_CMD0_SHIFT);
	}
}

/* cmdfunc send commands to the IFC NAND Machine */
static void fsl_ifc_cmdfunc(struct mtd_info *mtd, unsigned int command,
			     int column, int page_addr) {
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;

	/* clear the read buffer */
	ifc_nand_ctrl->read_bytes = 0;
	if (command != NAND_CMD_PAGEPROG)
		ifc_nand_ctrl->index = 0;

	switch (command) {
	/* READ0 read the entire buffer to use hardware ECC. */
	case NAND_CMD_READ0:
		out_be32(&ifc->ifc_nand.nand_fbcr, 0);
		set_addr(mtd, 0, page_addr, 0);

		ifc_nand_ctrl->read_bytes = mtd->writesize + mtd->oobsize;
		ifc_nand_ctrl->index += column;

		if (chip->ecc.mode == NAND_ECC_HW)
			ifc_nand_ctrl->eccread = 1;

		fsl_ifc_do_read(chip, 0, mtd);
		fsl_ifc_run_command(mtd);
		return;

	/* READOOB reads only the OOB because no ECC is performed. */
	case NAND_CMD_READOOB:
		out_be32(&ifc->ifc_nand.nand_fbcr, mtd->oobsize - column);
		set_addr(mtd, column, page_addr, 1);

		ifc_nand_ctrl->read_bytes = mtd->writesize + mtd->oobsize;

		fsl_ifc_do_read(chip, 1, mtd);
		fsl_ifc_run_command(mtd);

		return;

	case NAND_CMD_READID:
	case NAND_CMD_PARAM: {
		int timing = IFC_FIR_OP_RB;
		if (command == NAND_CMD_PARAM)
			timing = IFC_FIR_OP_RBCD;

		out_be32(&ifc->ifc_nand.nand_fir0,
				(IFC_FIR_OP_CMD0 << IFC_NAND_FIR0_OP0_SHIFT) |
				(IFC_FIR_OP_UA  << IFC_NAND_FIR0_OP1_SHIFT) |
				(timing << IFC_NAND_FIR0_OP2_SHIFT));
		out_be32(&ifc->ifc_nand.nand_fcr0,
				command << IFC_NAND_FCR0_CMD0_SHIFT);
		out_be32(&ifc->ifc_nand.row3, column);

		/*
		 * although currently it's 8 bytes for READID, we always read
		 * the maximum 256 bytes(for PARAM)
		 */
		out_be32(&ifc->ifc_nand.nand_fbcr, 256);
		ifc_nand_ctrl->read_bytes = 256;

		set_addr(mtd, 0, 0, 0);
		fsl_ifc_run_command(mtd);
		return;
	}

	/* ERASE1 stores the block and page address */
	case NAND_CMD_ERASE1:
		set_addr(mtd, 0, page_addr, 0);
		return;

	/* ERASE2 uses the block and page address from ERASE1 */
	case NAND_CMD_ERASE2:
		out_be32(&ifc->ifc_nand.nand_fir0,
			 (IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
			 (IFC_FIR_OP_RA0 << IFC_NAND_FIR0_OP1_SHIFT) |
			 (IFC_FIR_OP_CMD1 << IFC_NAND_FIR0_OP2_SHIFT));

		out_be32(&ifc->ifc_nand.nand_fcr0,
			 (NAND_CMD_ERASE1 << IFC_NAND_FCR0_CMD0_SHIFT) |
			 (NAND_CMD_ERASE2 << IFC_NAND_FCR0_CMD1_SHIFT));

		out_be32(&ifc->ifc_nand.nand_fbcr, 0);
		ifc_nand_ctrl->read_bytes = 0;
		fsl_ifc_run_command(mtd);
		return;

	/* SEQIN sets up the addr buffer and all registers except the length */
	case NAND_CMD_SEQIN: {
		u32 nand_fcr0;
		ifc_nand_ctrl->column = column;
		ifc_nand_ctrl->oob = 0;

		if (mtd->writesize > 512) {
			nand_fcr0 =
				(NAND_CMD_SEQIN << IFC_NAND_FCR0_CMD0_SHIFT) |
				(NAND_CMD_PAGEPROG << IFC_NAND_FCR0_CMD1_SHIFT);

			out_be32(&ifc->ifc_nand.nand_fir0,
				 (IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
				 (IFC_FIR_OP_CA0 << IFC_NAND_FIR0_OP1_SHIFT) |
				 (IFC_FIR_OP_RA0 << IFC_NAND_FIR0_OP2_SHIFT) |
				 (IFC_FIR_OP_WBCD  << IFC_NAND_FIR0_OP3_SHIFT) |
				 (IFC_FIR_OP_CW1 << IFC_NAND_FIR0_OP4_SHIFT));
		} else {
			nand_fcr0 = ((NAND_CMD_PAGEPROG <<
					IFC_NAND_FCR0_CMD1_SHIFT) |
				    (NAND_CMD_SEQIN <<
					IFC_NAND_FCR0_CMD2_SHIFT));

			out_be32(&ifc->ifc_nand.nand_fir0,
				 (IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
				 (IFC_FIR_OP_CMD2 << IFC_NAND_FIR0_OP1_SHIFT) |
				 (IFC_FIR_OP_CA0 << IFC_NAND_FIR0_OP2_SHIFT) |
				 (IFC_FIR_OP_RA0 << IFC_NAND_FIR0_OP3_SHIFT) |
				 (IFC_FIR_OP_WBCD << IFC_NAND_FIR0_OP4_SHIFT));
			out_be32(&ifc->ifc_nand.nand_fir1,
				 (IFC_FIR_OP_CW1 << IFC_NAND_FIR1_OP5_SHIFT));

			if (column >= mtd->writesize)
				nand_fcr0 |=
				NAND_CMD_READOOB << IFC_NAND_FCR0_CMD0_SHIFT;
			else
				nand_fcr0 |=
				NAND_CMD_READ0 << IFC_NAND_FCR0_CMD0_SHIFT;
		}

		if (column >= mtd->writesize) {
			/* OOB area --> READOOB */
			column -= mtd->writesize;
			ifc_nand_ctrl->oob = 1;
		}
		out_be32(&ifc->ifc_nand.nand_fcr0, nand_fcr0);
		set_addr(mtd, column, page_addr, ifc_nand_ctrl->oob);
		return;
	}

	/* PAGEPROG reuses all of the setup from SEQIN and adds the length */
	case NAND_CMD_PAGEPROG: {
		if (ifc_nand_ctrl->oob) {
			out_be32(&ifc->ifc_nand.nand_fbcr,
				ifc_nand_ctrl->index - ifc_nand_ctrl->column);
		} else {
			out_be32(&ifc->ifc_nand.nand_fbcr, 0);
		}

		fsl_ifc_run_command(mtd);
		return;
	}

	case NAND_CMD_STATUS:
		out_be32(&ifc->ifc_nand.nand_fir0,
				(IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
				(IFC_FIR_OP_RB << IFC_NAND_FIR0_OP1_SHIFT));
		out_be32(&ifc->ifc_nand.nand_fcr0,
				NAND_CMD_STATUS << IFC_NAND_FCR0_CMD0_SHIFT);
		out_be32(&ifc->ifc_nand.nand_fbcr, 1);
		set_addr(mtd, 0, 0, 0);
		ifc_nand_ctrl->read_bytes = 1;

		fsl_ifc_run_command(mtd);

		/*
		 * The chip always seems to report that it is
		 * write-protected, even when it is not.
		 */
		setbits8(ifc_nand_ctrl->addr, NAND_STATUS_WP);
		return;

	case NAND_CMD_RESET:
		out_be32(&ifc->ifc_nand.nand_fir0,
				IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT);
		out_be32(&ifc->ifc_nand.nand_fcr0,
				NAND_CMD_RESET << IFC_NAND_FCR0_CMD0_SHIFT);
		fsl_ifc_run_command(mtd);
		return;

	default:
		dev_err(priv->dev, "%s: error, unsupported command 0x%x.\n",
					__func__, command);
	}
}

static void fsl_ifc_select_chip(struct mtd_info *mtd, int chip)
{
	/* The hardware does not seem to support multiple
	 * chips per bank.
	 */
}

/*
 * Write buf to the IFC NAND Controller Data Buffer
 */
static void fsl_ifc_write_buf(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;
	unsigned int bufsize = mtd->writesize + mtd->oobsize;

	if (len <= 0) {
		dev_err(priv->dev, "%s: len %d bytes", __func__, len);
		return;
	}

	if ((unsigned int)len > bufsize - ifc_nand_ctrl->index) {
		dev_err(priv->dev,
			"%s: beyond end of buffer (%d requested, %u available)\n",
			__func__, len, bufsize - ifc_nand_ctrl->index);
		len = bufsize - ifc_nand_ctrl->index;
	}

	memcpy_toio(&ifc_nand_ctrl->addr[ifc_nand_ctrl->index], buf, len);
	ifc_nand_ctrl->index += len;
}

/*
 * Read a byte from either the IFC hardware buffer
 * read function for 8-bit buswidth
 */
static uint8_t fsl_ifc_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;

	/*
	 * If there are still bytes in the IFC buffer, then use the
	 * next byte.
	 */
	if (ifc_nand_ctrl->index < ifc_nand_ctrl->read_bytes)
		return in_8(&ifc_nand_ctrl->addr[ifc_nand_ctrl->index++]);

	dev_err(priv->dev, "%s: beyond end of buffer\n", __func__);
	return ERR_BYTE;
}

/*
 * Read two bytes from the IFC hardware buffer
 * read function for 16-bit buswith
 */
static uint8_t fsl_ifc_read_byte16(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;
	uint16_t data;

	/*
	 * If there are still bytes in the IFC buffer, then use the
	 * next byte.
	 */
	if (ifc_nand_ctrl->index < ifc_nand_ctrl->read_bytes) {
		data = in_be16((uint16_t *)&ifc_nand_ctrl->
					addr[ifc_nand_ctrl->index]);
		ifc_nand_ctrl->index += 2;
		return (uint8_t) data;
	}

	dev_err(priv->dev, "%s: beyond end of buffer\n", __func__);
	return ERR_BYTE;
}

/*
 * Read from the IFC Controller Data Buffer
 */
static void fsl_ifc_read_buf(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;
	int avail;

	if (len < 0) {
		dev_err(priv->dev, "%s: len %d bytes", __func__, len);
		return;
	}

	avail = min((unsigned int)len,
			ifc_nand_ctrl->read_bytes - ifc_nand_ctrl->index);
	memcpy_fromio(buf, &ifc_nand_ctrl->addr[ifc_nand_ctrl->index], avail);
	ifc_nand_ctrl->index += avail;

	if (len > avail)
		dev_err(priv->dev,
			"%s: beyond end of buffer (%d requested, %d available)\n",
			__func__, len, avail);
}

/*
 * Verify buffer against the IFC Controller Data Buffer
 */
static int fsl_ifc_verify_buf(struct mtd_info *mtd,
			       const u_char *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_nand_ctrl *nctrl = ifc_nand_ctrl;
	int i;

	if (len < 0) {
		dev_err(priv->dev, "%s: write_buf of %d bytes", __func__, len);
		return -EINVAL;
	}

	if ((unsigned int)len > nctrl->read_bytes - nctrl->index) {
		dev_err(priv->dev,
			"%s: beyond end of buffer (%d requested, %u available)\n",
			__func__, len, nctrl->read_bytes - nctrl->index);

		nctrl->index = nctrl->read_bytes;
		return -EINVAL;
	}

	for (i = 0; i < len; i++)
		if (in_8(&nctrl->addr[nctrl->index + i]) != buf[i])
			break;

	nctrl->index += len;

	if (i != len)
		return -EIO;
	if (ctrl->nand_stat != IFC_NAND_EVTER_STAT_OPC)
		return -EIO;

	return 0;
}

/*
 * This function is called after Program and Erase Operations to
 * check for success or failure.
 */
static int fsl_ifc_wait(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct fsl_ifc_mtd *priv = chip->priv;
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;
	u32 nand_fsr;

	/* Use READ_STATUS command, but wait for the device to be ready */
	out_be32(&ifc->ifc_nand.nand_fir0,
		 (IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
		 (IFC_FIR_OP_RDSTAT << IFC_NAND_FIR0_OP1_SHIFT));
	out_be32(&ifc->ifc_nand.nand_fcr0, NAND_CMD_STATUS <<
			IFC_NAND_FCR0_CMD0_SHIFT);
	out_be32(&ifc->ifc_nand.nand_fbcr, 1);
	set_addr(mtd, 0, 0, 0);
	ifc_nand_ctrl->read_bytes = 1;

	fsl_ifc_run_command(mtd);

	nand_fsr = in_be32(&ifc->ifc_nand.nand_fsr);

	/*
	 * The chip always seems to report that it is
	 * write-protected, even when it is not.
	 */
	return nand_fsr | NAND_STATUS_WP;
}

static int fsl_ifc_read_page(struct mtd_info *mtd, struct nand_chip *chip,
			     uint8_t *buf, int oob_required, int page)
{
	struct fsl_ifc_mtd *priv = chip->priv;
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_nand_ctrl *nctrl = ifc_nand_ctrl;

	fsl_ifc_read_buf(mtd, buf, mtd->writesize);
	if (oob_required)
		fsl_ifc_read_buf(mtd, chip->oob_poi, mtd->oobsize);

	if (ctrl->nand_stat & IFC_NAND_EVTER_STAT_ECCER)
		dev_err(priv->dev, "NAND Flash ECC Uncorrectable Error\n");

	if (ctrl->nand_stat != IFC_NAND_EVTER_STAT_OPC)
		mtd->ecc_stats.failed++;

	return nctrl->max_bitflips;
}

/* ECC will be calculated automatically, and errors will be detected in
 * waitfunc.
 */
static void fsl_ifc_write_page(struct mtd_info *mtd, struct nand_chip *chip,
			       const uint8_t *buf, int oob_required)
{
	fsl_ifc_write_buf(mtd, buf, mtd->writesize);
	fsl_ifc_write_buf(mtd, chip->oob_poi, mtd->oobsize);
}

static int fsl_ifc_chip_init_tail(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_ifc_mtd *priv = chip->priv;

	dev_dbg(priv->dev, "%s: nand->numchips = %d\n", __func__,
							chip->numchips);
	dev_dbg(priv->dev, "%s: nand->chipsize = %lld\n", __func__,
							chip->chipsize);
	dev_dbg(priv->dev, "%s: nand->pagemask = %8x\n", __func__,
							chip->pagemask);
	dev_dbg(priv->dev, "%s: nand->chip_delay = %d\n", __func__,
							chip->chip_delay);
	dev_dbg(priv->dev, "%s: nand->badblockpos = %d\n", __func__,
							chip->badblockpos);
	dev_dbg(priv->dev, "%s: nand->chip_shift = %d\n", __func__,
							chip->chip_shift);
	dev_dbg(priv->dev, "%s: nand->page_shift = %d\n", __func__,
							chip->page_shift);
	dev_dbg(priv->dev, "%s: nand->phys_erase_shift = %d\n", __func__,
							chip->phys_erase_shift);
	dev_dbg(priv->dev, "%s: nand->ecclayout = %p\n", __func__,
							chip->ecclayout);
	dev_dbg(priv->dev, "%s: nand->ecc.mode = %d\n", __func__,
							chip->ecc.mode);
	dev_dbg(priv->dev, "%s: nand->ecc.steps = %d\n", __func__,
							chip->ecc.steps);
	dev_dbg(priv->dev, "%s: nand->ecc.bytes = %d\n", __func__,
							chip->ecc.bytes);
	dev_dbg(priv->dev, "%s: nand->ecc.total = %d\n", __func__,
							chip->ecc.total);
	dev_dbg(priv->dev, "%s: nand->ecc.layout = %p\n", __func__,
							chip->ecc.layout);
	dev_dbg(priv->dev, "%s: mtd->flags = %08x\n", __func__, mtd->flags);
	dev_dbg(priv->dev, "%s: mtd->size = %lld\n", __func__, mtd->size);
	dev_dbg(priv->dev, "%s: mtd->erasesize = %d\n", __func__,
							mtd->erasesize);
	dev_dbg(priv->dev, "%s: mtd->writesize = %d\n", __func__,
							mtd->writesize);
	dev_dbg(priv->dev, "%s: mtd->oobsize = %d\n", __func__,
							mtd->oobsize);

	return 0;
}

static int fsl_ifc_chip_init(struct fsl_ifc_mtd *priv)
{
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;
	struct nand_chip *chip = &priv->chip;
	struct nand_ecclayout *layout;
	u32 csor;

	/* Fill in fsl_ifc_mtd structure */
	priv->mtd.priv = chip;
	priv->mtd.owner = THIS_MODULE;

	/* fill in nand_chip structure */
	/* set up function call table */
	if ((in_be32(&ifc->cspr_cs[priv->bank].cspr)) & CSPR_PORT_SIZE_16)
		chip->read_byte = fsl_ifc_read_byte16;
	else
		chip->read_byte = fsl_ifc_read_byte;

	chip->write_buf = fsl_ifc_write_buf;
	chip->read_buf = fsl_ifc_read_buf;
	chip->verify_buf = fsl_ifc_verify_buf;
	chip->select_chip = fsl_ifc_select_chip;
	chip->cmdfunc = fsl_ifc_cmdfunc;
	chip->waitfunc = fsl_ifc_wait;

	chip->bbt_td = &bbt_main_descr;
	chip->bbt_md = &bbt_mirror_descr;

	out_be32(&ifc->ifc_nand.ncfgr, 0x0);

	/* set up nand options */
	chip->options = NAND_NO_READRDY;
	chip->bbt_options = NAND_BBT_USE_FLASH;


	if (in_be32(&ifc->cspr_cs[priv->bank].cspr) & CSPR_PORT_SIZE_16) {
		chip->read_byte = fsl_ifc_read_byte16;
		chip->options |= NAND_BUSWIDTH_16;
	} else {
		chip->read_byte = fsl_ifc_read_byte;
	}

	chip->controller = &ifc_nand_ctrl->controller;
	chip->priv = priv;

	chip->ecc.read_page = fsl_ifc_read_page;
	chip->ecc.write_page = fsl_ifc_write_page;

	csor = in_be32(&ifc->csor_cs[priv->bank].csor);

	/* Hardware generates ECC per 512 Bytes */
	chip->ecc.size = 512;
	chip->ecc.bytes = 8;
	chip->ecc.strength = 4;

	switch (csor & CSOR_NAND_PGS_MASK) {
	case CSOR_NAND_PGS_512:
		if (chip->options & NAND_BUSWIDTH_16) {
			layout = &oob_512_16bit_ecc4;
		} else {
			layout = &oob_512_8bit_ecc4;

			/* Avoid conflict with bad block marker */
			bbt_main_descr.offs = 0;
			bbt_mirror_descr.offs = 0;
		}

		priv->bufnum_mask = 15;
		break;

	case CSOR_NAND_PGS_2K:
		layout = &oob_2048_ecc4;
		priv->bufnum_mask = 3;
		break;

	case CSOR_NAND_PGS_4K:
		if ((csor & CSOR_NAND_ECC_MODE_MASK) ==
		    CSOR_NAND_ECC_MODE_4) {
			layout = &oob_4096_ecc4;
		} else {
			layout = &oob_4096_ecc8;
			chip->ecc.bytes = 16;
		}

		priv->bufnum_mask = 1;
		break;

	default:
		dev_err(priv->dev, "bad csor %#x: bad page size\n", csor);
		return -ENODEV;
	}

	/* Must also set CSOR_NAND_ECC_ENC_EN if DEC_EN set */
	if (csor & CSOR_NAND_ECC_DEC_EN) {
		chip->ecc.mode = NAND_ECC_HW;
		chip->ecc.layout = layout;
	} else {
		chip->ecc.mode = NAND_ECC_SOFT;
	}

	return 0;
}

static int fsl_ifc_chip_remove(struct fsl_ifc_mtd *priv)
{
	nand_release(&priv->mtd);

	kfree(priv->mtd.name);

	if (priv->vbase)
		iounmap(priv->vbase);

	ifc_nand_ctrl->chips[priv->bank] = NULL;
	dev_set_drvdata(priv->dev, NULL);
	kfree(priv);

	return 0;
}

static int match_bank(struct fsl_ifc_regs __iomem *ifc, int bank,
		      phys_addr_t addr)
{
	u32 cspr = in_be32(&ifc->cspr_cs[bank].cspr);

	if (!(cspr & CSPR_V))
		return 0;
	if ((cspr & CSPR_MSEL) != CSPR_MSEL_NAND)
		return 0;

	return (cspr & CSPR_BA) == convert_ifc_address(addr);
}

static DEFINE_MUTEX(fsl_ifc_nand_mutex);

static int __devinit fsl_ifc_nand_probe(struct platform_device *dev)
{
	struct fsl_ifc_regs __iomem *ifc;
	struct fsl_ifc_mtd *priv;
	struct resource res;
	static const char *part_probe_types[]
		= { "cmdlinepart", "RedBoot", "ofpart", NULL };
	int ret;
	int bank;
	struct device_node *node = dev->dev.of_node;
	struct mtd_part_parser_data ppdata;

	ppdata.of_node = dev->dev.of_node;
	if (!fsl_ifc_ctrl_dev || !fsl_ifc_ctrl_dev->regs)
		return -ENODEV;
	ifc = fsl_ifc_ctrl_dev->regs;

	/* get, allocate and map the memory resource */
	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(&dev->dev, "%s: failed to get resource\n", __func__);
		return ret;
	}

	/* find which chip select it is connected to */
	for (bank = 0; bank < FSL_IFC_BANK_COUNT; bank++) {
		if (match_bank(ifc, bank, res.start))
			break;
	}

	if (bank >= FSL_IFC_BANK_COUNT) {
		dev_err(&dev->dev, "%s: address did not match any chip selects\n",
			__func__);
		return -ENODEV;
	}

	priv = devm_kzalloc(&dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_lock(&fsl_ifc_nand_mutex);
	if (!fsl_ifc_ctrl_dev->nand) {
		ifc_nand_ctrl = kzalloc(sizeof(*ifc_nand_ctrl), GFP_KERNEL);
		if (!ifc_nand_ctrl) {
			dev_err(&dev->dev, "failed to allocate memory\n");
			mutex_unlock(&fsl_ifc_nand_mutex);
			return -ENOMEM;
		}

		ifc_nand_ctrl->read_bytes = 0;
		ifc_nand_ctrl->index = 0;
		ifc_nand_ctrl->addr = NULL;
		fsl_ifc_ctrl_dev->nand = ifc_nand_ctrl;

		spin_lock_init(&ifc_nand_ctrl->controller.lock);
		init_waitqueue_head(&ifc_nand_ctrl->controller.wq);
	} else {
		ifc_nand_ctrl = fsl_ifc_ctrl_dev->nand;
	}
	mutex_unlock(&fsl_ifc_nand_mutex);

	ifc_nand_ctrl->chips[bank] = priv;
	priv->bank = bank;
	priv->ctrl = fsl_ifc_ctrl_dev;
	priv->dev = &dev->dev;

	priv->vbase = ioremap(res.start, resource_size(&res));
	if (!priv->vbase) {
		dev_err(priv->dev, "%s: failed to map chip region\n", __func__);
		ret = -ENOMEM;
		goto err;
	}

	dev_set_drvdata(priv->dev, priv);

	out_be32(&ifc->ifc_nand.nand_evter_en,
			IFC_NAND_EVTER_EN_OPC_EN |
			IFC_NAND_EVTER_EN_FTOER_EN |
			IFC_NAND_EVTER_EN_WPER_EN);

	/* enable NAND Machine Interrupts */
	out_be32(&ifc->ifc_nand.nand_evter_intr_en,
			IFC_NAND_EVTER_INTR_OPCIR_EN |
			IFC_NAND_EVTER_INTR_FTOERIR_EN |
			IFC_NAND_EVTER_INTR_WPERIR_EN);

	priv->mtd.name = kasprintf(GFP_KERNEL, "%x.flash", (unsigned)res.start);
	if (!priv->mtd.name) {
		ret = -ENOMEM;
		goto err;
	}

	ret = fsl_ifc_chip_init(priv);
	if (ret)
		goto err;

	ret = nand_scan_ident(&priv->mtd, 1, NULL);
	if (ret)
		goto err;

	ret = fsl_ifc_chip_init_tail(&priv->mtd);
	if (ret)
		goto err;

	ret = nand_scan_tail(&priv->mtd);
	if (ret)
		goto err;

	/* First look for RedBoot table or partitions on the command
	 * line, these take precedence over device tree information */
	mtd_device_parse_register(&priv->mtd, part_probe_types, &ppdata,
						NULL, 0);

	dev_info(priv->dev, "IFC NAND device at 0x%llx, bank %d\n",
		 (unsigned long long)res.start, priv->bank);
	return 0;

err:
	fsl_ifc_chip_remove(priv);
	return ret;
}

static int fsl_ifc_nand_remove(struct platform_device *dev)
{
	struct fsl_ifc_mtd *priv = dev_get_drvdata(&dev->dev);

	fsl_ifc_chip_remove(priv);

	mutex_lock(&fsl_ifc_nand_mutex);
	ifc_nand_ctrl->counter--;
	if (!ifc_nand_ctrl->counter) {
		fsl_ifc_ctrl_dev->nand = NULL;
		kfree(ifc_nand_ctrl);
	}
	mutex_unlock(&fsl_ifc_nand_mutex);

	return 0;
}

static const struct of_device_id fsl_ifc_nand_match[] = {
	{
		.compatible = "fsl,ifc-nand",
	},
	{}
};

static struct platform_driver fsl_ifc_nand_driver = {
	.driver = {
		.name	= "fsl,ifc-nand",
		.owner = THIS_MODULE,
		.of_match_table = fsl_ifc_nand_match,
	},
	.probe       = fsl_ifc_nand_probe,
	.remove      = fsl_ifc_nand_remove,
};

static int __init fsl_ifc_nand_init(void)
{
	int ret;

	ret = platform_driver_register(&fsl_ifc_nand_driver);
	if (ret)
		printk(KERN_ERR "fsl-ifc: Failed to register platform"
				"driver\n");

	return ret;
}

static void __exit fsl_ifc_nand_exit(void)
{
	platform_driver_unregister(&fsl_ifc_nand_driver);
}

module_init(fsl_ifc_nand_init);
module_exit(fsl_ifc_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale");
MODULE_DESCRIPTION("Freescale Integrated Flash Controller MTD NAND driver");
