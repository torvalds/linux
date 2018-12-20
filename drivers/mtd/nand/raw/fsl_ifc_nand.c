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
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/fsl_ifc.h>
#include <linux/iopoll.h>

#define ERR_BYTE		0xFF /* Value returned for read
					bytes when read failed	*/
#define IFC_TIMEOUT_MSECS	500  /* Maximum number of mSecs to wait
					for IFC NAND Machine	*/

struct fsl_ifc_ctrl;

/* mtd information per set */
struct fsl_ifc_mtd {
	struct nand_chip chip;
	struct fsl_ifc_ctrl *ctrl;

	struct device *dev;
	int bank;		/* Chip select bank number		*/
	unsigned int bufnum_mask; /* bufnum = page & bufnum_mask */
	u8 __iomem *vbase;      /* Chip select base virtual address	*/
};

/* overview of the fsl ifc controller */
struct fsl_ifc_nand_ctrl {
	struct nand_controller controller;
	struct fsl_ifc_mtd *chips[FSL_IFC_BANK_COUNT];

	void __iomem *addr;	/* Address of assigned IFC buffer	*/
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

static int fsl_ifc_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = 8;
	oobregion->length = chip->ecc.total;

	return 0;
}

static int fsl_ifc_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section > 1)
		return -ERANGE;

	if (mtd->writesize == 512 &&
	    !(chip->options & NAND_BUSWIDTH_16)) {
		if (!section) {
			oobregion->offset = 0;
			oobregion->length = 5;
		} else {
			oobregion->offset = 6;
			oobregion->length = 2;
		}

		return 0;
	}

	if (!section) {
		oobregion->offset = 2;
		oobregion->length = 6;
	} else {
		oobregion->offset = chip->ecc.total + 8;
		oobregion->length = mtd->oobsize - oobregion->offset;
	}

	return 0;
}

static const struct mtd_ooblayout_ops fsl_ifc_ooblayout_ops = {
	.ecc = fsl_ifc_ooblayout_ecc,
	.free = fsl_ifc_ooblayout_free,
};

/*
 * Set up the IFC hardware block and page address fields, and the ifc nand
 * structure addr field to point to the correct IFC buffer in memory
 */
static void set_addr(struct mtd_info *mtd, int column, int page_addr, int oob)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_runtime __iomem *ifc = ctrl->rregs;
	int buf_num;

	ifc_nand_ctrl->page = page_addr;
	/* Program ROW0/COL0 */
	ifc_out32(page_addr, &ifc->ifc_nand.row0);
	ifc_out32((oob ? IFC_NAND_COL_MS : 0) | column, &ifc->ifc_nand.col0);

	buf_num = page_addr & priv->bufnum_mask;

	ifc_nand_ctrl->addr = priv->vbase + buf_num * (mtd->writesize * 2);
	ifc_nand_ctrl->index = column;

	/* for OOB data point to the second half of the buffer */
	if (oob)
		ifc_nand_ctrl->index += mtd->writesize;
}

/* returns nonzero if entire page is blank */
static int check_read_ecc(struct mtd_info *mtd, struct fsl_ifc_ctrl *ctrl,
			  u32 eccstat, unsigned int bufnum)
{
	return  (eccstat >> ((3 - bufnum % 4) * 8)) & 15;
}

/*
 * execute IFC NAND command and wait for it to complete
 */
static void fsl_ifc_run_command(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_nand_ctrl *nctrl = ifc_nand_ctrl;
	struct fsl_ifc_runtime __iomem *ifc = ctrl->rregs;
	u32 eccstat;
	int i;

	/* set the chip select for NAND Transaction */
	ifc_out32(priv->bank << IFC_NAND_CSEL_SHIFT,
		  &ifc->ifc_nand.nand_csel);

	dev_vdbg(priv->dev,
			"%s: fir0=%08x fcr0=%08x\n",
			__func__,
			ifc_in32(&ifc->ifc_nand.nand_fir0),
			ifc_in32(&ifc->ifc_nand.nand_fcr0));

	ctrl->nand_stat = 0;

	/* start read/write seq */
	ifc_out32(IFC_NAND_SEQ_STRT_FIR_STRT, &ifc->ifc_nand.nandseq_strt);

	/* wait for command complete flag or timeout */
	wait_event_timeout(ctrl->nand_wait, ctrl->nand_stat,
			   msecs_to_jiffies(IFC_TIMEOUT_MSECS));

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
		int sector_start = bufnum * chip->ecc.steps;
		int sector_end = sector_start + chip->ecc.steps - 1;
		__be32 __iomem *eccstat_regs;

		eccstat_regs = ifc->ifc_nand.nand_eccstat;
		eccstat = ifc_in32(&eccstat_regs[sector_start / 4]);

		for (i = sector_start; i <= sector_end; i++) {
			if (i != sector_start && !(i % 4))
				eccstat = ifc_in32(&eccstat_regs[i / 4]);

			errors = check_read_ecc(mtd, ctrl, eccstat, i);

			if (errors == 15) {
				/*
				 * Uncorrectable error.
				 * We'll check for blank pages later.
				 *
				 * We disable ECCER reporting due to...
				 * erratum IFC-A002770 -- so report it now if we
				 * see an uncorrectable error in ECCSTAT.
				 */
				ctrl->nand_stat |= IFC_NAND_EVTER_STAT_ECCER;
				continue;
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
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_runtime __iomem *ifc = ctrl->rregs;

	/* Program FIR/IFC_NAND_FCR0 for Small/Large page */
	if (mtd->writesize > 512) {
		ifc_out32((IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
			  (IFC_FIR_OP_CA0 << IFC_NAND_FIR0_OP1_SHIFT) |
			  (IFC_FIR_OP_RA0 << IFC_NAND_FIR0_OP2_SHIFT) |
			  (IFC_FIR_OP_CMD1 << IFC_NAND_FIR0_OP3_SHIFT) |
			  (IFC_FIR_OP_RBCD << IFC_NAND_FIR0_OP4_SHIFT),
			  &ifc->ifc_nand.nand_fir0);
		ifc_out32(0x0, &ifc->ifc_nand.nand_fir1);

		ifc_out32((NAND_CMD_READ0 << IFC_NAND_FCR0_CMD0_SHIFT) |
			  (NAND_CMD_READSTART << IFC_NAND_FCR0_CMD1_SHIFT),
			  &ifc->ifc_nand.nand_fcr0);
	} else {
		ifc_out32((IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
			  (IFC_FIR_OP_CA0 << IFC_NAND_FIR0_OP1_SHIFT) |
			  (IFC_FIR_OP_RA0  << IFC_NAND_FIR0_OP2_SHIFT) |
			  (IFC_FIR_OP_RBCD << IFC_NAND_FIR0_OP3_SHIFT),
			  &ifc->ifc_nand.nand_fir0);
		ifc_out32(0x0, &ifc->ifc_nand.nand_fir1);

		if (oob)
			ifc_out32(NAND_CMD_READOOB <<
				  IFC_NAND_FCR0_CMD0_SHIFT,
				  &ifc->ifc_nand.nand_fcr0);
		else
			ifc_out32(NAND_CMD_READ0 <<
				  IFC_NAND_FCR0_CMD0_SHIFT,
				  &ifc->ifc_nand.nand_fcr0);
	}
}

/* cmdfunc send commands to the IFC NAND Machine */
static void fsl_ifc_cmdfunc(struct nand_chip *chip, unsigned int command,
			    int column, int page_addr) {
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_runtime __iomem *ifc = ctrl->rregs;

	/* clear the read buffer */
	ifc_nand_ctrl->read_bytes = 0;
	if (command != NAND_CMD_PAGEPROG)
		ifc_nand_ctrl->index = 0;

	switch (command) {
	/* READ0 read the entire buffer to use hardware ECC. */
	case NAND_CMD_READ0:
		ifc_out32(0, &ifc->ifc_nand.nand_fbcr);
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
		ifc_out32(mtd->oobsize - column, &ifc->ifc_nand.nand_fbcr);
		set_addr(mtd, column, page_addr, 1);

		ifc_nand_ctrl->read_bytes = mtd->writesize + mtd->oobsize;

		fsl_ifc_do_read(chip, 1, mtd);
		fsl_ifc_run_command(mtd);

		return;

	case NAND_CMD_READID:
	case NAND_CMD_PARAM: {
		/*
		 * For READID, read 8 bytes that are currently used.
		 * For PARAM, read all 3 copies of 256-bytes pages.
		 */
		int len = 8;
		int timing = IFC_FIR_OP_RB;
		if (command == NAND_CMD_PARAM) {
			timing = IFC_FIR_OP_RBCD;
			len = 256 * 3;
		}

		ifc_out32((IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
			  (IFC_FIR_OP_UA  << IFC_NAND_FIR0_OP1_SHIFT) |
			  (timing << IFC_NAND_FIR0_OP2_SHIFT),
			  &ifc->ifc_nand.nand_fir0);
		ifc_out32(command << IFC_NAND_FCR0_CMD0_SHIFT,
			  &ifc->ifc_nand.nand_fcr0);
		ifc_out32(column, &ifc->ifc_nand.row3);

		ifc_out32(len, &ifc->ifc_nand.nand_fbcr);
		ifc_nand_ctrl->read_bytes = len;

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
		ifc_out32((IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
			  (IFC_FIR_OP_RA0 << IFC_NAND_FIR0_OP1_SHIFT) |
			  (IFC_FIR_OP_CMD1 << IFC_NAND_FIR0_OP2_SHIFT),
			  &ifc->ifc_nand.nand_fir0);

		ifc_out32((NAND_CMD_ERASE1 << IFC_NAND_FCR0_CMD0_SHIFT) |
			  (NAND_CMD_ERASE2 << IFC_NAND_FCR0_CMD1_SHIFT),
			  &ifc->ifc_nand.nand_fcr0);

		ifc_out32(0, &ifc->ifc_nand.nand_fbcr);
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
				(NAND_CMD_STATUS << IFC_NAND_FCR0_CMD1_SHIFT) |
				(NAND_CMD_PAGEPROG << IFC_NAND_FCR0_CMD2_SHIFT);

			ifc_out32(
				(IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
				(IFC_FIR_OP_CA0 << IFC_NAND_FIR0_OP1_SHIFT) |
				(IFC_FIR_OP_RA0 << IFC_NAND_FIR0_OP2_SHIFT) |
				(IFC_FIR_OP_WBCD << IFC_NAND_FIR0_OP3_SHIFT) |
				(IFC_FIR_OP_CMD2 << IFC_NAND_FIR0_OP4_SHIFT),
				&ifc->ifc_nand.nand_fir0);
			ifc_out32(
				(IFC_FIR_OP_CW1 << IFC_NAND_FIR1_OP5_SHIFT) |
				(IFC_FIR_OP_RDSTAT << IFC_NAND_FIR1_OP6_SHIFT) |
				(IFC_FIR_OP_NOP << IFC_NAND_FIR1_OP7_SHIFT),
				&ifc->ifc_nand.nand_fir1);
		} else {
			nand_fcr0 = ((NAND_CMD_PAGEPROG <<
					IFC_NAND_FCR0_CMD1_SHIFT) |
				    (NAND_CMD_SEQIN <<
					IFC_NAND_FCR0_CMD2_SHIFT) |
				    (NAND_CMD_STATUS <<
					IFC_NAND_FCR0_CMD3_SHIFT));

			ifc_out32(
				(IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
				(IFC_FIR_OP_CMD2 << IFC_NAND_FIR0_OP1_SHIFT) |
				(IFC_FIR_OP_CA0 << IFC_NAND_FIR0_OP2_SHIFT) |
				(IFC_FIR_OP_RA0 << IFC_NAND_FIR0_OP3_SHIFT) |
				(IFC_FIR_OP_WBCD << IFC_NAND_FIR0_OP4_SHIFT),
				&ifc->ifc_nand.nand_fir0);
			ifc_out32(
				(IFC_FIR_OP_CMD1 << IFC_NAND_FIR1_OP5_SHIFT) |
				(IFC_FIR_OP_CW3 << IFC_NAND_FIR1_OP6_SHIFT) |
				(IFC_FIR_OP_RDSTAT << IFC_NAND_FIR1_OP7_SHIFT) |
				(IFC_FIR_OP_NOP << IFC_NAND_FIR1_OP8_SHIFT),
				&ifc->ifc_nand.nand_fir1);

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
		ifc_out32(nand_fcr0, &ifc->ifc_nand.nand_fcr0);
		set_addr(mtd, column, page_addr, ifc_nand_ctrl->oob);
		return;
	}

	/* PAGEPROG reuses all of the setup from SEQIN and adds the length */
	case NAND_CMD_PAGEPROG: {
		if (ifc_nand_ctrl->oob) {
			ifc_out32(ifc_nand_ctrl->index -
				  ifc_nand_ctrl->column,
				  &ifc->ifc_nand.nand_fbcr);
		} else {
			ifc_out32(0, &ifc->ifc_nand.nand_fbcr);
		}

		fsl_ifc_run_command(mtd);
		return;
	}

	case NAND_CMD_STATUS: {
		void __iomem *addr;

		ifc_out32((IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
			  (IFC_FIR_OP_RB << IFC_NAND_FIR0_OP1_SHIFT),
			  &ifc->ifc_nand.nand_fir0);
		ifc_out32(NAND_CMD_STATUS << IFC_NAND_FCR0_CMD0_SHIFT,
			  &ifc->ifc_nand.nand_fcr0);
		ifc_out32(1, &ifc->ifc_nand.nand_fbcr);
		set_addr(mtd, 0, 0, 0);
		ifc_nand_ctrl->read_bytes = 1;

		fsl_ifc_run_command(mtd);

		/*
		 * The chip always seems to report that it is
		 * write-protected, even when it is not.
		 */
		addr = ifc_nand_ctrl->addr;
		if (chip->options & NAND_BUSWIDTH_16)
			ifc_out16(ifc_in16(addr) | (NAND_STATUS_WP), addr);
		else
			ifc_out8(ifc_in8(addr) | (NAND_STATUS_WP), addr);
		return;
	}

	case NAND_CMD_RESET:
		ifc_out32(IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT,
			  &ifc->ifc_nand.nand_fir0);
		ifc_out32(NAND_CMD_RESET << IFC_NAND_FCR0_CMD0_SHIFT,
			  &ifc->ifc_nand.nand_fcr0);
		fsl_ifc_run_command(mtd);
		return;

	default:
		dev_err(priv->dev, "%s: error, unsupported command 0x%x.\n",
					__func__, command);
	}
}

static void fsl_ifc_select_chip(struct nand_chip *chip, int cs)
{
	/* The hardware does not seem to support multiple
	 * chips per bank.
	 */
}

/*
 * Write buf to the IFC NAND Controller Data Buffer
 */
static void fsl_ifc_write_buf(struct nand_chip *chip, const u8 *buf, int len)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
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

	memcpy_toio(ifc_nand_ctrl->addr + ifc_nand_ctrl->index, buf, len);
	ifc_nand_ctrl->index += len;
}

/*
 * Read a byte from either the IFC hardware buffer
 * read function for 8-bit buswidth
 */
static uint8_t fsl_ifc_read_byte(struct nand_chip *chip)
{
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	unsigned int offset;

	/*
	 * If there are still bytes in the IFC buffer, then use the
	 * next byte.
	 */
	if (ifc_nand_ctrl->index < ifc_nand_ctrl->read_bytes) {
		offset = ifc_nand_ctrl->index++;
		return ifc_in8(ifc_nand_ctrl->addr + offset);
	}

	dev_err(priv->dev, "%s: beyond end of buffer\n", __func__);
	return ERR_BYTE;
}

/*
 * Read two bytes from the IFC hardware buffer
 * read function for 16-bit buswith
 */
static uint8_t fsl_ifc_read_byte16(struct nand_chip *chip)
{
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	uint16_t data;

	/*
	 * If there are still bytes in the IFC buffer, then use the
	 * next byte.
	 */
	if (ifc_nand_ctrl->index < ifc_nand_ctrl->read_bytes) {
		data = ifc_in16(ifc_nand_ctrl->addr + ifc_nand_ctrl->index);
		ifc_nand_ctrl->index += 2;
		return (uint8_t) data;
	}

	dev_err(priv->dev, "%s: beyond end of buffer\n", __func__);
	return ERR_BYTE;
}

/*
 * Read from the IFC Controller Data Buffer
 */
static void fsl_ifc_read_buf(struct nand_chip *chip, u8 *buf, int len)
{
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	int avail;

	if (len < 0) {
		dev_err(priv->dev, "%s: len %d bytes", __func__, len);
		return;
	}

	avail = min((unsigned int)len,
			ifc_nand_ctrl->read_bytes - ifc_nand_ctrl->index);
	memcpy_fromio(buf, ifc_nand_ctrl->addr + ifc_nand_ctrl->index, avail);
	ifc_nand_ctrl->index += avail;

	if (len > avail)
		dev_err(priv->dev,
			"%s: beyond end of buffer (%d requested, %d available)\n",
			__func__, len, avail);
}

/*
 * This function is called after Program and Erase Operations to
 * check for success or failure.
 */
static int fsl_ifc_wait(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_runtime __iomem *ifc = ctrl->rregs;
	u32 nand_fsr;
	int status;

	/* Use READ_STATUS command, but wait for the device to be ready */
	ifc_out32((IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
		  (IFC_FIR_OP_RDSTAT << IFC_NAND_FIR0_OP1_SHIFT),
		  &ifc->ifc_nand.nand_fir0);
	ifc_out32(NAND_CMD_STATUS << IFC_NAND_FCR0_CMD0_SHIFT,
		  &ifc->ifc_nand.nand_fcr0);
	ifc_out32(1, &ifc->ifc_nand.nand_fbcr);
	set_addr(mtd, 0, 0, 0);
	ifc_nand_ctrl->read_bytes = 1;

	fsl_ifc_run_command(mtd);

	nand_fsr = ifc_in32(&ifc->ifc_nand.nand_fsr);
	status = nand_fsr >> 24;
	/*
	 * The chip always seems to report that it is
	 * write-protected, even when it is not.
	 */
	return status | NAND_STATUS_WP;
}

/*
 * The controller does not check for bitflips in erased pages,
 * therefore software must check instead.
 */
static int check_erased_page(struct nand_chip *chip, u8 *buf)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 *ecc = chip->oob_poi;
	const int ecc_size = chip->ecc.bytes;
	const int pkt_size = chip->ecc.size;
	int i, res, bitflips = 0;
	struct mtd_oob_region oobregion = { };

	mtd_ooblayout_ecc(mtd, 0, &oobregion);
	ecc += oobregion.offset;

	for (i = 0; i < chip->ecc.steps; ++i) {
		res = nand_check_erased_ecc_chunk(buf, pkt_size, ecc, ecc_size,
						  NULL, 0,
						  chip->ecc.strength);
		if (res < 0)
			mtd->ecc_stats.failed++;
		else
			mtd->ecc_stats.corrected += res;

		bitflips = max(res, bitflips);
		buf += pkt_size;
		ecc += ecc_size;
	}

	return bitflips;
}

static int fsl_ifc_read_page(struct nand_chip *chip, uint8_t *buf,
			     int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_nand_ctrl *nctrl = ifc_nand_ctrl;

	nand_read_page_op(chip, page, 0, buf, mtd->writesize);
	if (oob_required)
		fsl_ifc_read_buf(chip, chip->oob_poi, mtd->oobsize);

	if (ctrl->nand_stat & IFC_NAND_EVTER_STAT_ECCER) {
		if (!oob_required)
			fsl_ifc_read_buf(chip, chip->oob_poi, mtd->oobsize);

		return check_erased_page(chip, buf);
	}

	if (ctrl->nand_stat != IFC_NAND_EVTER_STAT_OPC)
		mtd->ecc_stats.failed++;

	return nctrl->max_bitflips;
}

/* ECC will be calculated automatically, and errors will be detected in
 * waitfunc.
 */
static int fsl_ifc_write_page(struct nand_chip *chip, const uint8_t *buf,
			      int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	nand_prog_page_begin_op(chip, page, 0, buf, mtd->writesize);
	fsl_ifc_write_buf(chip, chip->oob_poi, mtd->oobsize);

	return nand_prog_page_end_op(chip);
}

static int fsl_ifc_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct fsl_ifc_mtd *priv = nand_get_controller_data(chip);

	dev_dbg(priv->dev, "%s: nand->numchips = %d\n", __func__,
							chip->numchips);
	dev_dbg(priv->dev, "%s: nand->chipsize = %lld\n", __func__,
							chip->chipsize);
	dev_dbg(priv->dev, "%s: nand->pagemask = %8x\n", __func__,
							chip->pagemask);
	dev_dbg(priv->dev, "%s: nand->legacy.chip_delay = %d\n", __func__,
		chip->legacy.chip_delay);
	dev_dbg(priv->dev, "%s: nand->badblockpos = %d\n", __func__,
							chip->badblockpos);
	dev_dbg(priv->dev, "%s: nand->chip_shift = %d\n", __func__,
							chip->chip_shift);
	dev_dbg(priv->dev, "%s: nand->page_shift = %d\n", __func__,
							chip->page_shift);
	dev_dbg(priv->dev, "%s: nand->phys_erase_shift = %d\n", __func__,
							chip->phys_erase_shift);
	dev_dbg(priv->dev, "%s: nand->ecc.mode = %d\n", __func__,
							chip->ecc.mode);
	dev_dbg(priv->dev, "%s: nand->ecc.steps = %d\n", __func__,
							chip->ecc.steps);
	dev_dbg(priv->dev, "%s: nand->ecc.bytes = %d\n", __func__,
							chip->ecc.bytes);
	dev_dbg(priv->dev, "%s: nand->ecc.total = %d\n", __func__,
							chip->ecc.total);
	dev_dbg(priv->dev, "%s: mtd->ooblayout = %p\n", __func__,
							mtd->ooblayout);
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

static const struct nand_controller_ops fsl_ifc_controller_ops = {
	.attach_chip = fsl_ifc_attach_chip,
};

static int fsl_ifc_sram_init(struct fsl_ifc_mtd *priv)
{
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_runtime __iomem *ifc_runtime = ctrl->rregs;
	struct fsl_ifc_global __iomem *ifc_global = ctrl->gregs;
	uint32_t csor = 0, csor_8k = 0, csor_ext = 0;
	uint32_t cs = priv->bank;

	if (ctrl->version < FSL_IFC_VERSION_1_1_0)
		return 0;

	if (ctrl->version > FSL_IFC_VERSION_1_1_0) {
		u32 ncfgr, status;
		int ret;

		/* Trigger auto initialization */
		ncfgr = ifc_in32(&ifc_runtime->ifc_nand.ncfgr);
		ifc_out32(ncfgr | IFC_NAND_NCFGR_SRAM_INIT_EN, &ifc_runtime->ifc_nand.ncfgr);

		/* Wait until done */
		ret = readx_poll_timeout(ifc_in32, &ifc_runtime->ifc_nand.ncfgr,
					 status, !(status & IFC_NAND_NCFGR_SRAM_INIT_EN),
					 10, IFC_TIMEOUT_MSECS * 1000);
		if (ret)
			dev_err(priv->dev, "Failed to initialize SRAM!\n");

		return ret;
	}

	/* Save CSOR and CSOR_ext */
	csor = ifc_in32(&ifc_global->csor_cs[cs].csor);
	csor_ext = ifc_in32(&ifc_global->csor_cs[cs].csor_ext);

	/* chage PageSize 8K and SpareSize 1K*/
	csor_8k = (csor & ~(CSOR_NAND_PGS_MASK)) | 0x0018C000;
	ifc_out32(csor_8k, &ifc_global->csor_cs[cs].csor);
	ifc_out32(0x0000400, &ifc_global->csor_cs[cs].csor_ext);

	/* READID */
	ifc_out32((IFC_FIR_OP_CW0 << IFC_NAND_FIR0_OP0_SHIFT) |
		    (IFC_FIR_OP_UA  << IFC_NAND_FIR0_OP1_SHIFT) |
		    (IFC_FIR_OP_RB << IFC_NAND_FIR0_OP2_SHIFT),
		    &ifc_runtime->ifc_nand.nand_fir0);
	ifc_out32(NAND_CMD_READID << IFC_NAND_FCR0_CMD0_SHIFT,
		    &ifc_runtime->ifc_nand.nand_fcr0);
	ifc_out32(0x0, &ifc_runtime->ifc_nand.row3);

	ifc_out32(0x0, &ifc_runtime->ifc_nand.nand_fbcr);

	/* Program ROW0/COL0 */
	ifc_out32(0x0, &ifc_runtime->ifc_nand.row0);
	ifc_out32(0x0, &ifc_runtime->ifc_nand.col0);

	/* set the chip select for NAND Transaction */
	ifc_out32(cs << IFC_NAND_CSEL_SHIFT,
		&ifc_runtime->ifc_nand.nand_csel);

	/* start read seq */
	ifc_out32(IFC_NAND_SEQ_STRT_FIR_STRT,
		&ifc_runtime->ifc_nand.nandseq_strt);

	/* wait for command complete flag or timeout */
	wait_event_timeout(ctrl->nand_wait, ctrl->nand_stat,
			   msecs_to_jiffies(IFC_TIMEOUT_MSECS));

	if (ctrl->nand_stat != IFC_NAND_EVTER_STAT_OPC) {
		pr_err("fsl-ifc: Failed to Initialise SRAM\n");
		return -ETIMEDOUT;
	}

	/* Restore CSOR and CSOR_ext */
	ifc_out32(csor, &ifc_global->csor_cs[cs].csor);
	ifc_out32(csor_ext, &ifc_global->csor_cs[cs].csor_ext);

	return 0;
}

static int fsl_ifc_chip_init(struct fsl_ifc_mtd *priv)
{
	struct fsl_ifc_ctrl *ctrl = priv->ctrl;
	struct fsl_ifc_global __iomem *ifc_global = ctrl->gregs;
	struct fsl_ifc_runtime __iomem *ifc_runtime = ctrl->rregs;
	struct nand_chip *chip = &priv->chip;
	struct mtd_info *mtd = nand_to_mtd(&priv->chip);
	u32 csor;
	int ret;

	/* Fill in fsl_ifc_mtd structure */
	mtd->dev.parent = priv->dev;
	nand_set_flash_node(chip, priv->dev->of_node);

	/* fill in nand_chip structure */
	/* set up function call table */
	if ((ifc_in32(&ifc_global->cspr_cs[priv->bank].cspr))
		& CSPR_PORT_SIZE_16)
		chip->legacy.read_byte = fsl_ifc_read_byte16;
	else
		chip->legacy.read_byte = fsl_ifc_read_byte;

	chip->legacy.write_buf = fsl_ifc_write_buf;
	chip->legacy.read_buf = fsl_ifc_read_buf;
	chip->select_chip = fsl_ifc_select_chip;
	chip->legacy.cmdfunc = fsl_ifc_cmdfunc;
	chip->legacy.waitfunc = fsl_ifc_wait;
	chip->legacy.set_features = nand_get_set_features_notsupp;
	chip->legacy.get_features = nand_get_set_features_notsupp;

	chip->bbt_td = &bbt_main_descr;
	chip->bbt_md = &bbt_mirror_descr;

	ifc_out32(0x0, &ifc_runtime->ifc_nand.ncfgr);

	/* set up nand options */
	chip->bbt_options = NAND_BBT_USE_FLASH;
	chip->options = NAND_NO_SUBPAGE_WRITE;

	if (ifc_in32(&ifc_global->cspr_cs[priv->bank].cspr)
		& CSPR_PORT_SIZE_16) {
		chip->legacy.read_byte = fsl_ifc_read_byte16;
		chip->options |= NAND_BUSWIDTH_16;
	} else {
		chip->legacy.read_byte = fsl_ifc_read_byte;
	}

	chip->controller = &ifc_nand_ctrl->controller;
	nand_set_controller_data(chip, priv);

	chip->ecc.read_page = fsl_ifc_read_page;
	chip->ecc.write_page = fsl_ifc_write_page;

	csor = ifc_in32(&ifc_global->csor_cs[priv->bank].csor);

	switch (csor & CSOR_NAND_PGS_MASK) {
	case CSOR_NAND_PGS_512:
		if (!(chip->options & NAND_BUSWIDTH_16)) {
			/* Avoid conflict with bad block marker */
			bbt_main_descr.offs = 0;
			bbt_mirror_descr.offs = 0;
		}

		priv->bufnum_mask = 15;
		break;

	case CSOR_NAND_PGS_2K:
		priv->bufnum_mask = 3;
		break;

	case CSOR_NAND_PGS_4K:
		priv->bufnum_mask = 1;
		break;

	case CSOR_NAND_PGS_8K:
		priv->bufnum_mask = 0;
		break;

	default:
		dev_err(priv->dev, "bad csor %#x: bad page size\n", csor);
		return -ENODEV;
	}

	/* Must also set CSOR_NAND_ECC_ENC_EN if DEC_EN set */
	if (csor & CSOR_NAND_ECC_DEC_EN) {
		chip->ecc.mode = NAND_ECC_HW;
		mtd_set_ooblayout(mtd, &fsl_ifc_ooblayout_ops);

		/* Hardware generates ECC per 512 Bytes */
		chip->ecc.size = 512;
		if ((csor & CSOR_NAND_ECC_MODE_MASK) == CSOR_NAND_ECC_MODE_4) {
			chip->ecc.bytes = 8;
			chip->ecc.strength = 4;
		} else {
			chip->ecc.bytes = 16;
			chip->ecc.strength = 8;
		}
	} else {
		chip->ecc.mode = NAND_ECC_SOFT;
		chip->ecc.algo = NAND_ECC_HAMMING;
	}

	ret = fsl_ifc_sram_init(priv);
	if (ret)
		return ret;

	/*
	 * As IFC version 2.0.0 has 16KB of internal SRAM as compared to older
	 * versions which had 8KB. Hence bufnum mask needs to be updated.
	 */
	if (ctrl->version >= FSL_IFC_VERSION_2_0_0)
		priv->bufnum_mask = (priv->bufnum_mask * 2) + 1;

	return 0;
}

static int fsl_ifc_chip_remove(struct fsl_ifc_mtd *priv)
{
	struct mtd_info *mtd = nand_to_mtd(&priv->chip);

	kfree(mtd->name);

	if (priv->vbase)
		iounmap(priv->vbase);

	ifc_nand_ctrl->chips[priv->bank] = NULL;

	return 0;
}

static int match_bank(struct fsl_ifc_global __iomem *ifc_global, int bank,
		      phys_addr_t addr)
{
	u32 cspr = ifc_in32(&ifc_global->cspr_cs[bank].cspr);

	if (!(cspr & CSPR_V))
		return 0;
	if ((cspr & CSPR_MSEL) != CSPR_MSEL_NAND)
		return 0;

	return (cspr & CSPR_BA) == convert_ifc_address(addr);
}

static DEFINE_MUTEX(fsl_ifc_nand_mutex);

static int fsl_ifc_nand_probe(struct platform_device *dev)
{
	struct fsl_ifc_runtime __iomem *ifc;
	struct fsl_ifc_mtd *priv;
	struct resource res;
	static const char *part_probe_types[]
		= { "cmdlinepart", "RedBoot", "ofpart", NULL };
	int ret;
	int bank;
	struct device_node *node = dev->dev.of_node;
	struct mtd_info *mtd;

	if (!fsl_ifc_ctrl_dev || !fsl_ifc_ctrl_dev->rregs)
		return -ENODEV;
	ifc = fsl_ifc_ctrl_dev->rregs;

	/* get, allocate and map the memory resource */
	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(&dev->dev, "%s: failed to get resource\n", __func__);
		return ret;
	}

	/* find which chip select it is connected to */
	for (bank = 0; bank < fsl_ifc_ctrl_dev->banks; bank++) {
		if (match_bank(fsl_ifc_ctrl_dev->gregs, bank, res.start))
			break;
	}

	if (bank >= fsl_ifc_ctrl_dev->banks) {
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
			mutex_unlock(&fsl_ifc_nand_mutex);
			return -ENOMEM;
		}

		ifc_nand_ctrl->read_bytes = 0;
		ifc_nand_ctrl->index = 0;
		ifc_nand_ctrl->addr = NULL;
		fsl_ifc_ctrl_dev->nand = ifc_nand_ctrl;

		nand_controller_init(&ifc_nand_ctrl->controller);
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

	ifc_out32(IFC_NAND_EVTER_EN_OPC_EN |
		  IFC_NAND_EVTER_EN_FTOER_EN |
		  IFC_NAND_EVTER_EN_WPER_EN,
		  &ifc->ifc_nand.nand_evter_en);

	/* enable NAND Machine Interrupts */
	ifc_out32(IFC_NAND_EVTER_INTR_OPCIR_EN |
		  IFC_NAND_EVTER_INTR_FTOERIR_EN |
		  IFC_NAND_EVTER_INTR_WPERIR_EN,
		  &ifc->ifc_nand.nand_evter_intr_en);

	mtd = nand_to_mtd(&priv->chip);
	mtd->name = kasprintf(GFP_KERNEL, "%llx.flash", (u64)res.start);
	if (!mtd->name) {
		ret = -ENOMEM;
		goto err;
	}

	ret = fsl_ifc_chip_init(priv);
	if (ret)
		goto err;

	priv->chip.controller->ops = &fsl_ifc_controller_ops;
	ret = nand_scan(&priv->chip, 1);
	if (ret)
		goto err;

	/* First look for RedBoot table or partitions on the command
	 * line, these take precedence over device tree information */
	ret = mtd_device_parse_register(mtd, part_probe_types, NULL, NULL, 0);
	if (ret)
		goto cleanup_nand;

	dev_info(priv->dev, "IFC NAND device at 0x%llx, bank %d\n",
		 (unsigned long long)res.start, priv->bank);

	return 0;

cleanup_nand:
	nand_cleanup(&priv->chip);
err:
	fsl_ifc_chip_remove(priv);

	return ret;
}

static int fsl_ifc_nand_remove(struct platform_device *dev)
{
	struct fsl_ifc_mtd *priv = dev_get_drvdata(&dev->dev);

	nand_release(&priv->chip);
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
MODULE_DEVICE_TABLE(of, fsl_ifc_nand_match);

static struct platform_driver fsl_ifc_nand_driver = {
	.driver = {
		.name	= "fsl,ifc-nand",
		.of_match_table = fsl_ifc_nand_match,
	},
	.probe       = fsl_ifc_nand_probe,
	.remove      = fsl_ifc_nand_remove,
};

module_platform_driver(fsl_ifc_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale");
MODULE_DESCRIPTION("Freescale Integrated Flash Controller MTD NAND driver");
