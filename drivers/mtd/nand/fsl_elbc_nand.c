/* Freescale Enhanced Local Bus Controller NAND driver
 *
 * Copyright © 2006-2007, 2010 Freescale Semiconductor
 *
 * Authors: Nick Spence <nick.spence@freescale.com>,
 *          Scott Wood <scottwood@freescale.com>
 *          Jack Lan <jack.lan@freescale.com>
 *          Roy Zang <tie-fei.zang@freescale.com>
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
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/fsl_lbc.h>

#define MAX_BANKS 8
#define ERR_BYTE 0xFF /* Value returned for read bytes when read failed */
#define FCM_TIMEOUT_MSECS 500 /* Maximum number of mSecs to wait for FCM */

/* mtd information per set */

struct fsl_elbc_mtd {
	struct nand_chip chip;
	struct fsl_lbc_ctrl *ctrl;

	struct device *dev;
	int bank;               /* Chip select bank number           */
	u8 __iomem *vbase;      /* Chip select base virtual address  */
	int page_size;          /* NAND page size (0=512, 1=2048)    */
	unsigned int fmr;       /* FCM Flash Mode Register value     */
};

/* Freescale eLBC FCM controller information */

struct fsl_elbc_fcm_ctrl {
	struct nand_hw_control controller;
	struct fsl_elbc_mtd *chips[MAX_BANKS];

	u8 __iomem *addr;        /* Address of assigned FCM buffer        */
	unsigned int page;       /* Last page written to / read from      */
	unsigned int read_bytes; /* Number of bytes read during command   */
	unsigned int column;     /* Saved column from SEQIN               */
	unsigned int index;      /* Pointer to next byte to 'read'        */
	unsigned int status;     /* status read from LTESR after last op  */
	unsigned int mdr;        /* UPM/FCM Data Register value           */
	unsigned int use_mdr;    /* Non zero if the MDR is to be set      */
	unsigned int oob;        /* Non zero if operating on OOB data     */
	unsigned int counter;	 /* counter for the initializations	  */
	unsigned int max_bitflips;  /* Saved during READ0 cmd		  */
};

/* These map to the positions used by the FCM hardware ECC generator */

static int fsl_elbc_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (16 * section) + 6;
	if (priv->fmr & FMR_ECCM)
		oobregion->offset += 2;

	oobregion->length = chip->ecc.bytes;

	return 0;
}

static int fsl_elbc_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);

	if (section > chip->ecc.steps)
		return -ERANGE;

	if (!section) {
		oobregion->offset = 0;
		if (mtd->writesize > 512)
			oobregion->offset++;
		oobregion->length = (priv->fmr & FMR_ECCM) ? 7 : 5;
	} else {
		oobregion->offset = (16 * section) -
				    ((priv->fmr & FMR_ECCM) ? 5 : 7);
		if (section < chip->ecc.steps)
			oobregion->length = 13;
		else
			oobregion->length = mtd->oobsize - oobregion->offset;
	}

	return 0;
}

static const struct mtd_ooblayout_ops fsl_elbc_ooblayout_ops = {
	.ecc = fsl_elbc_ooblayout_ecc,
	.free = fsl_elbc_ooblayout_free,
};

/*
 * ELBC may use HW ECC, so that OOB offsets, that NAND core uses for bbt,
 * interfere with ECC positions, that's why we implement our own descriptors.
 * OOB {11, 5}, works for both SP and LP chips, with ECCM = 1 and ECCM = 0.
 */
static u8 bbt_pattern[] = {'B', 'b', 't', '0' };
static u8 mirror_pattern[] = {'1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE |
		   NAND_BBT_2BIT | NAND_BBT_VERSION,
	.offs =	11,
	.len = 4,
	.veroffs = 15,
	.maxblocks = 4,
	.pattern = bbt_pattern,
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE |
		   NAND_BBT_2BIT | NAND_BBT_VERSION,
	.offs =	11,
	.len = 4,
	.veroffs = 15,
	.maxblocks = 4,
	.pattern = mirror_pattern,
};

/*=================================*/

/*
 * Set up the FCM hardware block and page address fields, and the fcm
 * structure addr field to point to the correct FCM buffer in memory
 */
static void set_addr(struct mtd_info *mtd, int column, int page_addr, int oob)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_lbc_ctrl *ctrl = priv->ctrl;
	struct fsl_lbc_regs __iomem *lbc = ctrl->regs;
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = ctrl->nand;
	int buf_num;

	elbc_fcm_ctrl->page = page_addr;

	if (priv->page_size) {
		/*
		 * large page size chip : FPAR[PI] save the lowest 6 bits,
		 *                        FBAR[BLK] save the other bits.
		 */
		out_be32(&lbc->fbar, page_addr >> 6);
		out_be32(&lbc->fpar,
		         ((page_addr << FPAR_LP_PI_SHIFT) & FPAR_LP_PI) |
		         (oob ? FPAR_LP_MS : 0) | column);
		buf_num = (page_addr & 1) << 2;
	} else {
		/*
		 * small page size chip : FPAR[PI] save the lowest 5 bits,
		 *                        FBAR[BLK] save the other bits.
		 */
		out_be32(&lbc->fbar, page_addr >> 5);
		out_be32(&lbc->fpar,
		         ((page_addr << FPAR_SP_PI_SHIFT) & FPAR_SP_PI) |
		         (oob ? FPAR_SP_MS : 0) | column);
		buf_num = page_addr & 7;
	}

	elbc_fcm_ctrl->addr = priv->vbase + buf_num * 1024;
	elbc_fcm_ctrl->index = column;

	/* for OOB data point to the second half of the buffer */
	if (oob)
		elbc_fcm_ctrl->index += priv->page_size ? 2048 : 512;

	dev_vdbg(priv->dev, "set_addr: bank=%d, "
			    "elbc_fcm_ctrl->addr=0x%p (0x%p), "
	                    "index %x, pes %d ps %d\n",
		 buf_num, elbc_fcm_ctrl->addr, priv->vbase,
		 elbc_fcm_ctrl->index,
	         chip->phys_erase_shift, chip->page_shift);
}

/*
 * execute FCM command and wait for it to complete
 */
static int fsl_elbc_run_command(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_lbc_ctrl *ctrl = priv->ctrl;
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = ctrl->nand;
	struct fsl_lbc_regs __iomem *lbc = ctrl->regs;

	/* Setup the FMR[OP] to execute without write protection */
	out_be32(&lbc->fmr, priv->fmr | 3);
	if (elbc_fcm_ctrl->use_mdr)
		out_be32(&lbc->mdr, elbc_fcm_ctrl->mdr);

	dev_vdbg(priv->dev,
	         "fsl_elbc_run_command: fmr=%08x fir=%08x fcr=%08x\n",
	         in_be32(&lbc->fmr), in_be32(&lbc->fir), in_be32(&lbc->fcr));
	dev_vdbg(priv->dev,
	         "fsl_elbc_run_command: fbar=%08x fpar=%08x "
	         "fbcr=%08x bank=%d\n",
	         in_be32(&lbc->fbar), in_be32(&lbc->fpar),
	         in_be32(&lbc->fbcr), priv->bank);

	ctrl->irq_status = 0;
	/* execute special operation */
	out_be32(&lbc->lsor, priv->bank);

	/* wait for FCM complete flag or timeout */
	wait_event_timeout(ctrl->irq_wait, ctrl->irq_status,
	                   FCM_TIMEOUT_MSECS * HZ/1000);
	elbc_fcm_ctrl->status = ctrl->irq_status;
	/* store mdr value in case it was needed */
	if (elbc_fcm_ctrl->use_mdr)
		elbc_fcm_ctrl->mdr = in_be32(&lbc->mdr);

	elbc_fcm_ctrl->use_mdr = 0;

	if (elbc_fcm_ctrl->status != LTESR_CC) {
		dev_info(priv->dev,
		         "command failed: fir %x fcr %x status %x mdr %x\n",
		         in_be32(&lbc->fir), in_be32(&lbc->fcr),
			 elbc_fcm_ctrl->status, elbc_fcm_ctrl->mdr);
		return -EIO;
	}

	if (chip->ecc.mode != NAND_ECC_HW)
		return 0;

	elbc_fcm_ctrl->max_bitflips = 0;

	if (elbc_fcm_ctrl->read_bytes == mtd->writesize + mtd->oobsize) {
		uint32_t lteccr = in_be32(&lbc->lteccr);
		/*
		 * if command was a full page read and the ELBC
		 * has the LTECCR register, then bits 12-15 (ppc order) of
		 * LTECCR indicates which 512 byte sub-pages had fixed errors.
		 * bits 28-31 are uncorrectable errors, marked elsewhere.
		 * for small page nand only 1 bit is used.
		 * if the ELBC doesn't have the lteccr register it reads 0
		 * FIXME: 4 bits can be corrected on NANDs with 2k pages, so
		 * count the number of sub-pages with bitflips and update
		 * ecc_stats.corrected accordingly.
		 */
		if (lteccr & 0x000F000F)
			out_be32(&lbc->lteccr, 0x000F000F); /* clear lteccr */
		if (lteccr & 0x000F0000) {
			mtd->ecc_stats.corrected++;
			elbc_fcm_ctrl->max_bitflips = 1;
		}
	}

	return 0;
}

static void fsl_elbc_do_read(struct nand_chip *chip, int oob)
{
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_lbc_ctrl *ctrl = priv->ctrl;
	struct fsl_lbc_regs __iomem *lbc = ctrl->regs;

	if (priv->page_size) {
		out_be32(&lbc->fir,
		         (FIR_OP_CM0 << FIR_OP0_SHIFT) |
		         (FIR_OP_CA  << FIR_OP1_SHIFT) |
		         (FIR_OP_PA  << FIR_OP2_SHIFT) |
		         (FIR_OP_CM1 << FIR_OP3_SHIFT) |
		         (FIR_OP_RBW << FIR_OP4_SHIFT));

		out_be32(&lbc->fcr, (NAND_CMD_READ0 << FCR_CMD0_SHIFT) |
		                    (NAND_CMD_READSTART << FCR_CMD1_SHIFT));
	} else {
		out_be32(&lbc->fir,
		         (FIR_OP_CM0 << FIR_OP0_SHIFT) |
		         (FIR_OP_CA  << FIR_OP1_SHIFT) |
		         (FIR_OP_PA  << FIR_OP2_SHIFT) |
		         (FIR_OP_RBW << FIR_OP3_SHIFT));

		if (oob)
			out_be32(&lbc->fcr, NAND_CMD_READOOB << FCR_CMD0_SHIFT);
		else
			out_be32(&lbc->fcr, NAND_CMD_READ0 << FCR_CMD0_SHIFT);
	}
}

/* cmdfunc send commands to the FCM */
static void fsl_elbc_cmdfunc(struct mtd_info *mtd, unsigned int command,
                             int column, int page_addr)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_lbc_ctrl *ctrl = priv->ctrl;
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = ctrl->nand;
	struct fsl_lbc_regs __iomem *lbc = ctrl->regs;

	elbc_fcm_ctrl->use_mdr = 0;

	/* clear the read buffer */
	elbc_fcm_ctrl->read_bytes = 0;
	if (command != NAND_CMD_PAGEPROG)
		elbc_fcm_ctrl->index = 0;

	switch (command) {
	/* READ0 and READ1 read the entire buffer to use hardware ECC. */
	case NAND_CMD_READ1:
		column += 256;

	/* fall-through */
	case NAND_CMD_READ0:
		dev_dbg(priv->dev,
		        "fsl_elbc_cmdfunc: NAND_CMD_READ0, page_addr:"
		        " 0x%x, column: 0x%x.\n", page_addr, column);


		out_be32(&lbc->fbcr, 0); /* read entire page to enable ECC */
		set_addr(mtd, 0, page_addr, 0);

		elbc_fcm_ctrl->read_bytes = mtd->writesize + mtd->oobsize;
		elbc_fcm_ctrl->index += column;

		fsl_elbc_do_read(chip, 0);
		fsl_elbc_run_command(mtd);
		return;

	/* READOOB reads only the OOB because no ECC is performed. */
	case NAND_CMD_READOOB:
		dev_vdbg(priv->dev,
		         "fsl_elbc_cmdfunc: NAND_CMD_READOOB, page_addr:"
			 " 0x%x, column: 0x%x.\n", page_addr, column);

		out_be32(&lbc->fbcr, mtd->oobsize - column);
		set_addr(mtd, column, page_addr, 1);

		elbc_fcm_ctrl->read_bytes = mtd->writesize + mtd->oobsize;

		fsl_elbc_do_read(chip, 1);
		fsl_elbc_run_command(mtd);
		return;

	case NAND_CMD_READID:
	case NAND_CMD_PARAM:
		dev_vdbg(priv->dev, "fsl_elbc_cmdfunc: NAND_CMD %x\n", command);

		out_be32(&lbc->fir, (FIR_OP_CM0 << FIR_OP0_SHIFT) |
		                    (FIR_OP_UA  << FIR_OP1_SHIFT) |
		                    (FIR_OP_RBW << FIR_OP2_SHIFT));
		out_be32(&lbc->fcr, command << FCR_CMD0_SHIFT);
		/*
		 * although currently it's 8 bytes for READID, we always read
		 * the maximum 256 bytes(for PARAM)
		 */
		out_be32(&lbc->fbcr, 256);
		elbc_fcm_ctrl->read_bytes = 256;
		elbc_fcm_ctrl->use_mdr = 1;
		elbc_fcm_ctrl->mdr = column;
		set_addr(mtd, 0, 0, 0);
		fsl_elbc_run_command(mtd);
		return;

	/* ERASE1 stores the block and page address */
	case NAND_CMD_ERASE1:
		dev_vdbg(priv->dev,
		         "fsl_elbc_cmdfunc: NAND_CMD_ERASE1, "
		         "page_addr: 0x%x.\n", page_addr);
		set_addr(mtd, 0, page_addr, 0);
		return;

	/* ERASE2 uses the block and page address from ERASE1 */
	case NAND_CMD_ERASE2:
		dev_vdbg(priv->dev, "fsl_elbc_cmdfunc: NAND_CMD_ERASE2.\n");

		out_be32(&lbc->fir,
		         (FIR_OP_CM0 << FIR_OP0_SHIFT) |
		         (FIR_OP_PA  << FIR_OP1_SHIFT) |
		         (FIR_OP_CM2 << FIR_OP2_SHIFT) |
		         (FIR_OP_CW1 << FIR_OP3_SHIFT) |
		         (FIR_OP_RS  << FIR_OP4_SHIFT));

		out_be32(&lbc->fcr,
		         (NAND_CMD_ERASE1 << FCR_CMD0_SHIFT) |
		         (NAND_CMD_STATUS << FCR_CMD1_SHIFT) |
		         (NAND_CMD_ERASE2 << FCR_CMD2_SHIFT));

		out_be32(&lbc->fbcr, 0);
		elbc_fcm_ctrl->read_bytes = 0;
		elbc_fcm_ctrl->use_mdr = 1;

		fsl_elbc_run_command(mtd);
		return;

	/* SEQIN sets up the addr buffer and all registers except the length */
	case NAND_CMD_SEQIN: {
		__be32 fcr;
		dev_vdbg(priv->dev,
			 "fsl_elbc_cmdfunc: NAND_CMD_SEQIN/PAGE_PROG, "
		         "page_addr: 0x%x, column: 0x%x.\n",
		         page_addr, column);

		elbc_fcm_ctrl->column = column;
		elbc_fcm_ctrl->use_mdr = 1;

		if (column >= mtd->writesize) {
			/* OOB area */
			column -= mtd->writesize;
			elbc_fcm_ctrl->oob = 1;
		} else {
			WARN_ON(column != 0);
			elbc_fcm_ctrl->oob = 0;
		}

		fcr = (NAND_CMD_STATUS   << FCR_CMD1_SHIFT) |
		      (NAND_CMD_SEQIN    << FCR_CMD2_SHIFT) |
		      (NAND_CMD_PAGEPROG << FCR_CMD3_SHIFT);

		if (priv->page_size) {
			out_be32(&lbc->fir,
			         (FIR_OP_CM2 << FIR_OP0_SHIFT) |
			         (FIR_OP_CA  << FIR_OP1_SHIFT) |
			         (FIR_OP_PA  << FIR_OP2_SHIFT) |
			         (FIR_OP_WB  << FIR_OP3_SHIFT) |
			         (FIR_OP_CM3 << FIR_OP4_SHIFT) |
			         (FIR_OP_CW1 << FIR_OP5_SHIFT) |
			         (FIR_OP_RS  << FIR_OP6_SHIFT));
		} else {
			out_be32(&lbc->fir,
			         (FIR_OP_CM0 << FIR_OP0_SHIFT) |
			         (FIR_OP_CM2 << FIR_OP1_SHIFT) |
			         (FIR_OP_CA  << FIR_OP2_SHIFT) |
			         (FIR_OP_PA  << FIR_OP3_SHIFT) |
			         (FIR_OP_WB  << FIR_OP4_SHIFT) |
			         (FIR_OP_CM3 << FIR_OP5_SHIFT) |
			         (FIR_OP_CW1 << FIR_OP6_SHIFT) |
			         (FIR_OP_RS  << FIR_OP7_SHIFT));

			if (elbc_fcm_ctrl->oob)
				/* OOB area --> READOOB */
				fcr |= NAND_CMD_READOOB << FCR_CMD0_SHIFT;
			else
				/* First 256 bytes --> READ0 */
				fcr |= NAND_CMD_READ0 << FCR_CMD0_SHIFT;
		}

		out_be32(&lbc->fcr, fcr);
		set_addr(mtd, column, page_addr, elbc_fcm_ctrl->oob);
		return;
	}

	/* PAGEPROG reuses all of the setup from SEQIN and adds the length */
	case NAND_CMD_PAGEPROG: {
		dev_vdbg(priv->dev,
		         "fsl_elbc_cmdfunc: NAND_CMD_PAGEPROG "
			 "writing %d bytes.\n", elbc_fcm_ctrl->index);

		/* if the write did not start at 0 or is not a full page
		 * then set the exact length, otherwise use a full page
		 * write so the HW generates the ECC.
		 */
		if (elbc_fcm_ctrl->oob || elbc_fcm_ctrl->column != 0 ||
		    elbc_fcm_ctrl->index != mtd->writesize + mtd->oobsize)
			out_be32(&lbc->fbcr,
				elbc_fcm_ctrl->index - elbc_fcm_ctrl->column);
		else
			out_be32(&lbc->fbcr, 0);

		fsl_elbc_run_command(mtd);
		return;
	}

	/* CMD_STATUS must read the status byte while CEB is active */
	/* Note - it does not wait for the ready line */
	case NAND_CMD_STATUS:
		out_be32(&lbc->fir,
		         (FIR_OP_CM0 << FIR_OP0_SHIFT) |
		         (FIR_OP_RBW << FIR_OP1_SHIFT));
		out_be32(&lbc->fcr, NAND_CMD_STATUS << FCR_CMD0_SHIFT);
		out_be32(&lbc->fbcr, 1);
		set_addr(mtd, 0, 0, 0);
		elbc_fcm_ctrl->read_bytes = 1;

		fsl_elbc_run_command(mtd);

		/* The chip always seems to report that it is
		 * write-protected, even when it is not.
		 */
		setbits8(elbc_fcm_ctrl->addr, NAND_STATUS_WP);
		return;

	/* RESET without waiting for the ready line */
	case NAND_CMD_RESET:
		dev_dbg(priv->dev, "fsl_elbc_cmdfunc: NAND_CMD_RESET.\n");
		out_be32(&lbc->fir, FIR_OP_CM0 << FIR_OP0_SHIFT);
		out_be32(&lbc->fcr, NAND_CMD_RESET << FCR_CMD0_SHIFT);
		fsl_elbc_run_command(mtd);
		return;

	default:
		dev_err(priv->dev,
		        "fsl_elbc_cmdfunc: error, unsupported command 0x%x.\n",
		        command);
	}
}

static void fsl_elbc_select_chip(struct mtd_info *mtd, int chip)
{
	/* The hardware does not seem to support multiple
	 * chips per bank.
	 */
}

/*
 * Write buf to the FCM Controller Data Buffer
 */
static void fsl_elbc_write_buf(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = priv->ctrl->nand;
	unsigned int bufsize = mtd->writesize + mtd->oobsize;

	if (len <= 0) {
		dev_err(priv->dev, "write_buf of %d bytes", len);
		elbc_fcm_ctrl->status = 0;
		return;
	}

	if ((unsigned int)len > bufsize - elbc_fcm_ctrl->index) {
		dev_err(priv->dev,
		        "write_buf beyond end of buffer "
		        "(%d requested, %u available)\n",
			len, bufsize - elbc_fcm_ctrl->index);
		len = bufsize - elbc_fcm_ctrl->index;
	}

	memcpy_toio(&elbc_fcm_ctrl->addr[elbc_fcm_ctrl->index], buf, len);
	/*
	 * This is workaround for the weird elbc hangs during nand write,
	 * Scott Wood says: "...perhaps difference in how long it takes a
	 * write to make it through the localbus compared to a write to IMMR
	 * is causing problems, and sync isn't helping for some reason."
	 * Reading back the last byte helps though.
	 */
	in_8(&elbc_fcm_ctrl->addr[elbc_fcm_ctrl->index] + len - 1);

	elbc_fcm_ctrl->index += len;
}

/*
 * read a byte from either the FCM hardware buffer if it has any data left
 * otherwise issue a command to read a single byte.
 */
static u8 fsl_elbc_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = priv->ctrl->nand;

	/* If there are still bytes in the FCM, then use the next byte. */
	if (elbc_fcm_ctrl->index < elbc_fcm_ctrl->read_bytes)
		return in_8(&elbc_fcm_ctrl->addr[elbc_fcm_ctrl->index++]);

	dev_err(priv->dev, "read_byte beyond end of buffer\n");
	return ERR_BYTE;
}

/*
 * Read from the FCM Controller Data Buffer
 */
static void fsl_elbc_read_buf(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = priv->ctrl->nand;
	int avail;

	if (len < 0)
		return;

	avail = min((unsigned int)len,
			elbc_fcm_ctrl->read_bytes - elbc_fcm_ctrl->index);
	memcpy_fromio(buf, &elbc_fcm_ctrl->addr[elbc_fcm_ctrl->index], avail);
	elbc_fcm_ctrl->index += avail;

	if (len > avail)
		dev_err(priv->dev,
		        "read_buf beyond end of buffer "
		        "(%d requested, %d available)\n",
		        len, avail);
}

/* This function is called after Program and Erase Operations to
 * check for success or failure.
 */
static int fsl_elbc_wait(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = priv->ctrl->nand;

	if (elbc_fcm_ctrl->status != LTESR_CC)
		return NAND_STATUS_FAIL;

	/* The chip always seems to report that it is
	 * write-protected, even when it is not.
	 */
	return (elbc_fcm_ctrl->mdr & 0xff) | NAND_STATUS_WP;
}

static int fsl_elbc_chip_init_tail(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_lbc_ctrl *ctrl = priv->ctrl;
	struct fsl_lbc_regs __iomem *lbc = ctrl->regs;
	unsigned int al;

	/* calculate FMR Address Length field */
	al = 0;
	if (chip->pagemask & 0xffff0000)
		al++;
	if (chip->pagemask & 0xff000000)
		al++;

	priv->fmr |= al << FMR_AL_SHIFT;

	dev_dbg(priv->dev, "fsl_elbc_init: nand->numchips = %d\n",
	        chip->numchips);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->chipsize = %lld\n",
	        chip->chipsize);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->pagemask = %8x\n",
	        chip->pagemask);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->chip_delay = %d\n",
	        chip->chip_delay);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->badblockpos = %d\n",
	        chip->badblockpos);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->chip_shift = %d\n",
	        chip->chip_shift);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->page_shift = %d\n",
	        chip->page_shift);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->phys_erase_shift = %d\n",
	        chip->phys_erase_shift);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->ecc.mode = %d\n",
	        chip->ecc.mode);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->ecc.steps = %d\n",
	        chip->ecc.steps);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->ecc.bytes = %d\n",
	        chip->ecc.bytes);
	dev_dbg(priv->dev, "fsl_elbc_init: nand->ecc.total = %d\n",
	        chip->ecc.total);
	dev_dbg(priv->dev, "fsl_elbc_init: mtd->ooblayout = %p\n",
		mtd->ooblayout);
	dev_dbg(priv->dev, "fsl_elbc_init: mtd->flags = %08x\n", mtd->flags);
	dev_dbg(priv->dev, "fsl_elbc_init: mtd->size = %lld\n", mtd->size);
	dev_dbg(priv->dev, "fsl_elbc_init: mtd->erasesize = %d\n",
	        mtd->erasesize);
	dev_dbg(priv->dev, "fsl_elbc_init: mtd->writesize = %d\n",
	        mtd->writesize);
	dev_dbg(priv->dev, "fsl_elbc_init: mtd->oobsize = %d\n",
	        mtd->oobsize);

	/* adjust Option Register and ECC to match Flash page size */
	if (mtd->writesize == 512) {
		priv->page_size = 0;
		clrbits32(&lbc->bank[priv->bank].or, OR_FCM_PGS);
	} else if (mtd->writesize == 2048) {
		priv->page_size = 1;
		setbits32(&lbc->bank[priv->bank].or, OR_FCM_PGS);
	} else {
		dev_err(priv->dev,
		        "fsl_elbc_init: page size %d is not supported\n",
		        mtd->writesize);
		return -1;
	}

	return 0;
}

static int fsl_elbc_read_page(struct mtd_info *mtd, struct nand_chip *chip,
			      uint8_t *buf, int oob_required, int page)
{
	struct fsl_elbc_mtd *priv = nand_get_controller_data(chip);
	struct fsl_lbc_ctrl *ctrl = priv->ctrl;
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = ctrl->nand;

	fsl_elbc_read_buf(mtd, buf, mtd->writesize);
	if (oob_required)
		fsl_elbc_read_buf(mtd, chip->oob_poi, mtd->oobsize);

	if (fsl_elbc_wait(mtd, chip) & NAND_STATUS_FAIL)
		mtd->ecc_stats.failed++;

	return elbc_fcm_ctrl->max_bitflips;
}

/* ECC will be calculated automatically, and errors will be detected in
 * waitfunc.
 */
static int fsl_elbc_write_page(struct mtd_info *mtd, struct nand_chip *chip,
				const uint8_t *buf, int oob_required, int page)
{
	fsl_elbc_write_buf(mtd, buf, mtd->writesize);
	fsl_elbc_write_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

/* ECC will be calculated automatically, and errors will be detected in
 * waitfunc.
 */
static int fsl_elbc_write_subpage(struct mtd_info *mtd, struct nand_chip *chip,
				uint32_t offset, uint32_t data_len,
				const uint8_t *buf, int oob_required, int page)
{
	fsl_elbc_write_buf(mtd, buf, mtd->writesize);
	fsl_elbc_write_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

static int fsl_elbc_chip_init(struct fsl_elbc_mtd *priv)
{
	struct fsl_lbc_ctrl *ctrl = priv->ctrl;
	struct fsl_lbc_regs __iomem *lbc = ctrl->regs;
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = ctrl->nand;
	struct nand_chip *chip = &priv->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);

	dev_dbg(priv->dev, "eLBC Set Information for bank %d\n", priv->bank);

	/* Fill in fsl_elbc_mtd structure */
	mtd->dev.parent = priv->dev;
	nand_set_flash_node(chip, priv->dev->of_node);

	/* set timeout to maximum */
	priv->fmr = 15 << FMR_CWTO_SHIFT;
	if (in_be32(&lbc->bank[priv->bank].or) & OR_FCM_PGS)
		priv->fmr |= FMR_ECCM;

	/* fill in nand_chip structure */
	/* set up function call table */
	chip->read_byte = fsl_elbc_read_byte;
	chip->write_buf = fsl_elbc_write_buf;
	chip->read_buf = fsl_elbc_read_buf;
	chip->select_chip = fsl_elbc_select_chip;
	chip->cmdfunc = fsl_elbc_cmdfunc;
	chip->waitfunc = fsl_elbc_wait;
	chip->onfi_set_features = nand_onfi_get_set_features_notsupp;
	chip->onfi_get_features = nand_onfi_get_set_features_notsupp;

	chip->bbt_td = &bbt_main_descr;
	chip->bbt_md = &bbt_mirror_descr;

	/* set up nand options */
	chip->bbt_options = NAND_BBT_USE_FLASH;

	chip->controller = &elbc_fcm_ctrl->controller;
	nand_set_controller_data(chip, priv);

	chip->ecc.read_page = fsl_elbc_read_page;
	chip->ecc.write_page = fsl_elbc_write_page;
	chip->ecc.write_subpage = fsl_elbc_write_subpage;

	/* If CS Base Register selects full hardware ECC then use it */
	if ((in_be32(&lbc->bank[priv->bank].br) & BR_DECC) ==
	    BR_DECC_CHK_GEN) {
		chip->ecc.mode = NAND_ECC_HW;
		mtd_set_ooblayout(mtd, &fsl_elbc_ooblayout_ops);
		chip->ecc.size = 512;
		chip->ecc.bytes = 3;
		chip->ecc.strength = 1;
	} else {
		/* otherwise fall back to default software ECC */
		chip->ecc.mode = NAND_ECC_SOFT;
		chip->ecc.algo = NAND_ECC_HAMMING;
	}

	return 0;
}

static int fsl_elbc_chip_remove(struct fsl_elbc_mtd *priv)
{
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = priv->ctrl->nand;
	struct mtd_info *mtd = nand_to_mtd(&priv->chip);

	nand_release(mtd);

	kfree(mtd->name);

	if (priv->vbase)
		iounmap(priv->vbase);

	elbc_fcm_ctrl->chips[priv->bank] = NULL;
	kfree(priv);
	return 0;
}

static DEFINE_MUTEX(fsl_elbc_nand_mutex);

static int fsl_elbc_nand_probe(struct platform_device *pdev)
{
	struct fsl_lbc_regs __iomem *lbc;
	struct fsl_elbc_mtd *priv;
	struct resource res;
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl;
	static const char *part_probe_types[]
		= { "cmdlinepart", "RedBoot", "ofpart", NULL };
	int ret;
	int bank;
	struct device *dev;
	struct device_node *node = pdev->dev.of_node;
	struct mtd_info *mtd;

	if (!fsl_lbc_ctrl_dev || !fsl_lbc_ctrl_dev->regs)
		return -ENODEV;
	lbc = fsl_lbc_ctrl_dev->regs;
	dev = fsl_lbc_ctrl_dev->dev;

	/* get, allocate and map the memory resource */
	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "failed to get resource\n");
		return ret;
	}

	/* find which chip select it is connected to */
	for (bank = 0; bank < MAX_BANKS; bank++)
		if ((in_be32(&lbc->bank[bank].br) & BR_V) &&
		    (in_be32(&lbc->bank[bank].br) & BR_MSEL) == BR_MS_FCM &&
		    (in_be32(&lbc->bank[bank].br) &
		     in_be32(&lbc->bank[bank].or) & BR_BA)
		     == fsl_lbc_addr(res.start))
			break;

	if (bank >= MAX_BANKS) {
		dev_err(dev, "address did not match any chip selects\n");
		return -ENODEV;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_lock(&fsl_elbc_nand_mutex);
	if (!fsl_lbc_ctrl_dev->nand) {
		elbc_fcm_ctrl = kzalloc(sizeof(*elbc_fcm_ctrl), GFP_KERNEL);
		if (!elbc_fcm_ctrl) {
			mutex_unlock(&fsl_elbc_nand_mutex);
			ret = -ENOMEM;
			goto err;
		}
		elbc_fcm_ctrl->counter++;

		nand_hw_control_init(&elbc_fcm_ctrl->controller);
		fsl_lbc_ctrl_dev->nand = elbc_fcm_ctrl;
	} else {
		elbc_fcm_ctrl = fsl_lbc_ctrl_dev->nand;
	}
	mutex_unlock(&fsl_elbc_nand_mutex);

	elbc_fcm_ctrl->chips[bank] = priv;
	priv->bank = bank;
	priv->ctrl = fsl_lbc_ctrl_dev;
	priv->dev = &pdev->dev;
	dev_set_drvdata(priv->dev, priv);

	priv->vbase = ioremap(res.start, resource_size(&res));
	if (!priv->vbase) {
		dev_err(dev, "failed to map chip region\n");
		ret = -ENOMEM;
		goto err;
	}

	mtd = nand_to_mtd(&priv->chip);
	mtd->name = kasprintf(GFP_KERNEL, "%llx.flash", (u64)res.start);
	if (!nand_to_mtd(&priv->chip)->name) {
		ret = -ENOMEM;
		goto err;
	}

	ret = fsl_elbc_chip_init(priv);
	if (ret)
		goto err;

	ret = nand_scan_ident(mtd, 1, NULL);
	if (ret)
		goto err;

	ret = fsl_elbc_chip_init_tail(mtd);
	if (ret)
		goto err;

	ret = nand_scan_tail(mtd);
	if (ret)
		goto err;

	/* First look for RedBoot table or partitions on the command
	 * line, these take precedence over device tree information */
	mtd_device_parse_register(mtd, part_probe_types, NULL,
				  NULL, 0);

	printk(KERN_INFO "eLBC NAND device at 0x%llx, bank %d\n",
	       (unsigned long long)res.start, priv->bank);
	return 0;

err:
	fsl_elbc_chip_remove(priv);
	return ret;
}

static int fsl_elbc_nand_remove(struct platform_device *pdev)
{
	struct fsl_elbc_fcm_ctrl *elbc_fcm_ctrl = fsl_lbc_ctrl_dev->nand;
	struct fsl_elbc_mtd *priv = dev_get_drvdata(&pdev->dev);

	fsl_elbc_chip_remove(priv);

	mutex_lock(&fsl_elbc_nand_mutex);
	elbc_fcm_ctrl->counter--;
	if (!elbc_fcm_ctrl->counter) {
		fsl_lbc_ctrl_dev->nand = NULL;
		kfree(elbc_fcm_ctrl);
	}
	mutex_unlock(&fsl_elbc_nand_mutex);

	return 0;

}

static const struct of_device_id fsl_elbc_nand_match[] = {
	{ .compatible = "fsl,elbc-fcm-nand", },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_elbc_nand_match);

static struct platform_driver fsl_elbc_nand_driver = {
	.driver = {
		.name = "fsl,elbc-fcm-nand",
		.of_match_table = fsl_elbc_nand_match,
	},
	.probe = fsl_elbc_nand_probe,
	.remove = fsl_elbc_nand_remove,
};

module_platform_driver(fsl_elbc_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale");
MODULE_DESCRIPTION("Freescale Enhanced Local Bus Controller MTD NAND driver");
