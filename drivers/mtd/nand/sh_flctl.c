/*
 * SuperH FLCTL nand controller
 *
 * Copyright (c) 2008 Renesas Solutions Corp.
 * Copyright (c) 2008 Atom Create Engineering Co., Ltd.
 *
 * Based on fsl_elbc_nand.c, Copyright (c) 2006-2007 Freescale Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/sh_flctl.h>

static struct nand_ecclayout flctl_4secc_oob_16 = {
	.eccbytes = 10,
	.eccpos = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
	.oobfree = {
		{.offset = 12,
		. length = 4} },
};

static struct nand_ecclayout flctl_4secc_oob_64 = {
	.eccbytes = 10,
	.eccpos = {48, 49, 50, 51, 52, 53, 54, 55, 56, 57},
	.oobfree = {
		{.offset = 60,
		. length = 4} },
};

static uint8_t scan_ff_pattern[] = { 0xff, 0xff };

static struct nand_bbt_descr flctl_4secc_smallpage = {
	.options = NAND_BBT_SCAN2NDPAGE,
	.offs = 11,
	.len = 1,
	.pattern = scan_ff_pattern,
};

static struct nand_bbt_descr flctl_4secc_largepage = {
	.options = NAND_BBT_SCAN2NDPAGE,
	.offs = 58,
	.len = 2,
	.pattern = scan_ff_pattern,
};

static void empty_fifo(struct sh_flctl *flctl)
{
	writel(0x000c0000, FLINTDMACR(flctl));	/* FIFO Clear */
	writel(0x00000000, FLINTDMACR(flctl));	/* Clear Error flags */
}

static void start_translation(struct sh_flctl *flctl)
{
	writeb(TRSTRT, FLTRCR(flctl));
}

static void timeout_error(struct sh_flctl *flctl, const char *str)
{
	dev_err(&flctl->pdev->dev, "Timeout occurred in %s\n", str);
}

static void wait_completion(struct sh_flctl *flctl)
{
	uint32_t timeout = LOOP_TIMEOUT_MAX;

	while (timeout--) {
		if (readb(FLTRCR(flctl)) & TREND) {
			writeb(0x0, FLTRCR(flctl));
			return;
		}
		udelay(1);
	}

	timeout_error(flctl, __func__);
	writeb(0x0, FLTRCR(flctl));
}

static void set_addr(struct mtd_info *mtd, int column, int page_addr)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	uint32_t addr = 0;

	if (column == -1) {
		addr = page_addr;	/* ERASE1 */
	} else if (page_addr != -1) {
		/* SEQIN, READ0, etc.. */
		if (flctl->chip.options & NAND_BUSWIDTH_16)
			column >>= 1;
		if (flctl->page_size) {
			addr = column & 0x0FFF;
			addr |= (page_addr & 0xff) << 16;
			addr |= ((page_addr >> 8) & 0xff) << 24;
			/* big than 128MB */
			if (flctl->rw_ADRCNT == ADRCNT2_E) {
				uint32_t 	addr2;
				addr2 = (page_addr >> 16) & 0xff;
				writel(addr2, FLADR2(flctl));
			}
		} else {
			addr = column;
			addr |= (page_addr & 0xff) << 8;
			addr |= ((page_addr >> 8) & 0xff) << 16;
			addr |= ((page_addr >> 16) & 0xff) << 24;
		}
	}
	writel(addr, FLADR(flctl));
}

static void wait_rfifo_ready(struct sh_flctl *flctl)
{
	uint32_t timeout = LOOP_TIMEOUT_MAX;

	while (timeout--) {
		uint32_t val;
		/* check FIFO */
		val = readl(FLDTCNTR(flctl)) >> 16;
		if (val & 0xFF)
			return;
		udelay(1);
	}
	timeout_error(flctl, __func__);
}

static void wait_wfifo_ready(struct sh_flctl *flctl)
{
	uint32_t len, timeout = LOOP_TIMEOUT_MAX;

	while (timeout--) {
		/* check FIFO */
		len = (readl(FLDTCNTR(flctl)) >> 16) & 0xFF;
		if (len >= 4)
			return;
		udelay(1);
	}
	timeout_error(flctl, __func__);
}

static int wait_recfifo_ready(struct sh_flctl *flctl, int sector_number)
{
	uint32_t timeout = LOOP_TIMEOUT_MAX;
	int checked[4];
	void __iomem *ecc_reg[4];
	int i;
	uint32_t data, size;

	memset(checked, 0, sizeof(checked));

	while (timeout--) {
		size = readl(FLDTCNTR(flctl)) >> 24;
		if (size & 0xFF)
			return 0;	/* success */

		if (readl(FL4ECCCR(flctl)) & _4ECCFA)
			return 1;	/* can't correct */

		udelay(1);
		if (!(readl(FL4ECCCR(flctl)) & _4ECCEND))
			continue;

		/* start error correction */
		ecc_reg[0] = FL4ECCRESULT0(flctl);
		ecc_reg[1] = FL4ECCRESULT1(flctl);
		ecc_reg[2] = FL4ECCRESULT2(flctl);
		ecc_reg[3] = FL4ECCRESULT3(flctl);

		for (i = 0; i < 3; i++) {
			data = readl(ecc_reg[i]);
			if (data != INIT_FL4ECCRESULT_VAL && !checked[i]) {
				uint8_t org;
				int index;

				if (flctl->page_size)
					index = (512 * sector_number) +
						(data >> 16);
				else
					index = data >> 16;

				org = flctl->done_buff[index];
				flctl->done_buff[index] = org ^ (data & 0xFF);
				checked[i] = 1;
			}
		}

		writel(0, FL4ECCCR(flctl));
	}

	timeout_error(flctl, __func__);
	return 1;	/* timeout */
}

static void wait_wecfifo_ready(struct sh_flctl *flctl)
{
	uint32_t timeout = LOOP_TIMEOUT_MAX;
	uint32_t len;

	while (timeout--) {
		/* check FLECFIFO */
		len = (readl(FLDTCNTR(flctl)) >> 24) & 0xFF;
		if (len >= 4)
			return;
		udelay(1);
	}
	timeout_error(flctl, __func__);
}

static void read_datareg(struct sh_flctl *flctl, int offset)
{
	unsigned long data;
	unsigned long *buf = (unsigned long *)&flctl->done_buff[offset];

	wait_completion(flctl);

	data = readl(FLDATAR(flctl));
	*buf = le32_to_cpu(data);
}

static void read_fiforeg(struct sh_flctl *flctl, int rlen, int offset)
{
	int i, len_4align;
	unsigned long *buf = (unsigned long *)&flctl->done_buff[offset];
	void *fifo_addr = (void *)FLDTFIFO(flctl);

	len_4align = (rlen + 3) / 4;

	for (i = 0; i < len_4align; i++) {
		wait_rfifo_ready(flctl);
		buf[i] = readl(fifo_addr);
		buf[i] = be32_to_cpu(buf[i]);
	}
}

static int read_ecfiforeg(struct sh_flctl *flctl, uint8_t *buff, int sector)
{
	int i;
	unsigned long *ecc_buf = (unsigned long *)buff;
	void *fifo_addr = (void *)FLECFIFO(flctl);

	for (i = 0; i < 4; i++) {
		if (wait_recfifo_ready(flctl , sector))
			return 1;
		ecc_buf[i] = readl(fifo_addr);
		ecc_buf[i] = be32_to_cpu(ecc_buf[i]);
	}

	return 0;
}

static void write_fiforeg(struct sh_flctl *flctl, int rlen, int offset)
{
	int i, len_4align;
	unsigned long *data = (unsigned long *)&flctl->done_buff[offset];
	void *fifo_addr = (void *)FLDTFIFO(flctl);

	len_4align = (rlen + 3) / 4;
	for (i = 0; i < len_4align; i++) {
		wait_wfifo_ready(flctl);
		writel(cpu_to_be32(data[i]), fifo_addr);
	}
}

static void set_cmd_regs(struct mtd_info *mtd, uint32_t cmd, uint32_t flcmcdr_val)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	uint32_t flcmncr_val = flctl->flcmncr_base & ~SEL_16BIT;
	uint32_t flcmdcr_val, addr_len_bytes = 0;

	/* Set SNAND bit if page size is 2048byte */
	if (flctl->page_size)
		flcmncr_val |= SNAND_E;
	else
		flcmncr_val &= ~SNAND_E;

	/* default FLCMDCR val */
	flcmdcr_val = DOCMD1_E | DOADR_E;

	/* Set for FLCMDCR */
	switch (cmd) {
	case NAND_CMD_ERASE1:
		addr_len_bytes = flctl->erase_ADRCNT;
		flcmdcr_val |= DOCMD2_E;
		break;
	case NAND_CMD_READ0:
	case NAND_CMD_READOOB:
	case NAND_CMD_RNDOUT:
		addr_len_bytes = flctl->rw_ADRCNT;
		flcmdcr_val |= CDSRC_E;
		if (flctl->chip.options & NAND_BUSWIDTH_16)
			flcmncr_val |= SEL_16BIT;
		break;
	case NAND_CMD_SEQIN:
		/* This case is that cmd is READ0 or READ1 or READ00 */
		flcmdcr_val &= ~DOADR_E;	/* ONLY execute 1st cmd */
		break;
	case NAND_CMD_PAGEPROG:
		addr_len_bytes = flctl->rw_ADRCNT;
		flcmdcr_val |= DOCMD2_E | CDSRC_E | SELRW;
		if (flctl->chip.options & NAND_BUSWIDTH_16)
			flcmncr_val |= SEL_16BIT;
		break;
	case NAND_CMD_READID:
		flcmncr_val &= ~SNAND_E;
		flcmdcr_val |= CDSRC_E;
		addr_len_bytes = ADRCNT_1;
		break;
	case NAND_CMD_STATUS:
	case NAND_CMD_RESET:
		flcmncr_val &= ~SNAND_E;
		flcmdcr_val &= ~(DOADR_E | DOSR_E);
		break;
	default:
		break;
	}

	/* Set address bytes parameter */
	flcmdcr_val |= addr_len_bytes;

	/* Now actually write */
	writel(flcmncr_val, FLCMNCR(flctl));
	writel(flcmdcr_val, FLCMDCR(flctl));
	writel(flcmcdr_val, FLCMCDR(flctl));
}

static int flctl_read_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf, int oob_required, int page)
{
	int i, eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	uint8_t *p = buf;
	struct sh_flctl *flctl = mtd_to_flctl(mtd);

	for (i = 0; eccsteps; eccsteps--, i += eccbytes, p += eccsize)
		chip->read_buf(mtd, p, eccsize);

	for (i = 0; eccsteps; eccsteps--, i += eccbytes, p += eccsize) {
		if (flctl->hwecc_cant_correct[i])
			mtd->ecc_stats.failed++;
		else
			mtd->ecc_stats.corrected += 0; /* FIXME */
	}

	return 0;
}

static void flctl_write_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
				   const uint8_t *buf, int oob_required)
{
	int i, eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	const uint8_t *p = buf;

	for (i = 0; eccsteps; eccsteps--, i += eccbytes, p += eccsize)
		chip->write_buf(mtd, p, eccsize);
}

static void execmd_read_page_sector(struct mtd_info *mtd, int page_addr)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int sector, page_sectors;

	if (flctl->page_size)
		page_sectors = 4;
	else
		page_sectors = 1;

	writel(readl(FLCMNCR(flctl)) | ACM_SACCES_MODE | _4ECCCORRECT,
		 FLCMNCR(flctl));

	set_cmd_regs(mtd, NAND_CMD_READ0,
		(NAND_CMD_READSTART << 8) | NAND_CMD_READ0);

	for (sector = 0; sector < page_sectors; sector++) {
		int ret;

		empty_fifo(flctl);
		writel(readl(FLCMDCR(flctl)) | 1, FLCMDCR(flctl));
		writel(page_addr << 2 | sector, FLADR(flctl));

		start_translation(flctl);
		read_fiforeg(flctl, 512, 512 * sector);

		ret = read_ecfiforeg(flctl,
			&flctl->done_buff[mtd->writesize + 16 * sector],
			sector);

		if (ret)
			flctl->hwecc_cant_correct[sector] = 1;

		writel(0x0, FL4ECCCR(flctl));
		wait_completion(flctl);
	}
	writel(readl(FLCMNCR(flctl)) & ~(ACM_SACCES_MODE | _4ECCCORRECT),
			FLCMNCR(flctl));
}

static void execmd_read_oob(struct mtd_info *mtd, int page_addr)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);

	set_cmd_regs(mtd, NAND_CMD_READ0,
		(NAND_CMD_READSTART << 8) | NAND_CMD_READ0);

	empty_fifo(flctl);
	if (flctl->page_size) {
		int i;
		/* In case that the page size is 2k */
		for (i = 0; i < 16 * 3; i++)
			flctl->done_buff[i] = 0xFF;

		set_addr(mtd, 3 * 528 + 512, page_addr);
		writel(16, FLDTCNTR(flctl));

		start_translation(flctl);
		read_fiforeg(flctl, 16, 16 * 3);
		wait_completion(flctl);
	} else {
		/* In case that the page size is 512b */
		set_addr(mtd, 512, page_addr);
		writel(16, FLDTCNTR(flctl));

		start_translation(flctl);
		read_fiforeg(flctl, 16, 0);
		wait_completion(flctl);
	}
}

static void execmd_write_page_sector(struct mtd_info *mtd)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int i, page_addr = flctl->seqin_page_addr;
	int sector, page_sectors;

	if (flctl->page_size)
		page_sectors = 4;
	else
		page_sectors = 1;

	writel(readl(FLCMNCR(flctl)) | ACM_SACCES_MODE, FLCMNCR(flctl));

	set_cmd_regs(mtd, NAND_CMD_PAGEPROG,
			(NAND_CMD_PAGEPROG << 8) | NAND_CMD_SEQIN);

	for (sector = 0; sector < page_sectors; sector++) {
		empty_fifo(flctl);
		writel(readl(FLCMDCR(flctl)) | 1, FLCMDCR(flctl));
		writel(page_addr << 2 | sector, FLADR(flctl));

		start_translation(flctl);
		write_fiforeg(flctl, 512, 512 * sector);

		for (i = 0; i < 4; i++) {
			wait_wecfifo_ready(flctl); /* wait for write ready */
			writel(0xFFFFFFFF, FLECFIFO(flctl));
		}
		wait_completion(flctl);
	}

	writel(readl(FLCMNCR(flctl)) & ~ACM_SACCES_MODE, FLCMNCR(flctl));
}

static void execmd_write_oob(struct mtd_info *mtd)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int page_addr = flctl->seqin_page_addr;
	int sector, page_sectors;

	if (flctl->page_size) {
		sector = 3;
		page_sectors = 4;
	} else {
		sector = 0;
		page_sectors = 1;
	}

	set_cmd_regs(mtd, NAND_CMD_PAGEPROG,
			(NAND_CMD_PAGEPROG << 8) | NAND_CMD_SEQIN);

	for (; sector < page_sectors; sector++) {
		empty_fifo(flctl);
		set_addr(mtd, sector * 528 + 512, page_addr);
		writel(16, FLDTCNTR(flctl));	/* set read size */

		start_translation(flctl);
		write_fiforeg(flctl, 16, 16 * sector);
		wait_completion(flctl);
	}
}

static void flctl_cmdfunc(struct mtd_info *mtd, unsigned int command,
			int column, int page_addr)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	uint32_t read_cmd = 0;

	pm_runtime_get_sync(&flctl->pdev->dev);

	flctl->read_bytes = 0;
	if (command != NAND_CMD_PAGEPROG)
		flctl->index = 0;

	switch (command) {
	case NAND_CMD_READ1:
	case NAND_CMD_READ0:
		if (flctl->hwecc) {
			/* read page with hwecc */
			execmd_read_page_sector(mtd, page_addr);
			break;
		}
		if (flctl->page_size)
			set_cmd_regs(mtd, command, (NAND_CMD_READSTART << 8)
				| command);
		else
			set_cmd_regs(mtd, command, command);

		set_addr(mtd, 0, page_addr);

		flctl->read_bytes = mtd->writesize + mtd->oobsize;
		if (flctl->chip.options & NAND_BUSWIDTH_16)
			column >>= 1;
		flctl->index += column;
		goto read_normal_exit;

	case NAND_CMD_READOOB:
		if (flctl->hwecc) {
			/* read page with hwecc */
			execmd_read_oob(mtd, page_addr);
			break;
		}

		if (flctl->page_size) {
			set_cmd_regs(mtd, command, (NAND_CMD_READSTART << 8)
				| NAND_CMD_READ0);
			set_addr(mtd, mtd->writesize, page_addr);
		} else {
			set_cmd_regs(mtd, command, command);
			set_addr(mtd, 0, page_addr);
		}
		flctl->read_bytes = mtd->oobsize;
		goto read_normal_exit;

	case NAND_CMD_RNDOUT:
		if (flctl->hwecc)
			break;

		if (flctl->page_size)
			set_cmd_regs(mtd, command, (NAND_CMD_RNDOUTSTART << 8)
				| command);
		else
			set_cmd_regs(mtd, command, command);

		set_addr(mtd, column, 0);

		flctl->read_bytes = mtd->writesize + mtd->oobsize - column;
		goto read_normal_exit;

	case NAND_CMD_READID:
		set_cmd_regs(mtd, command, command);

		/* READID is always performed using an 8-bit bus */
		if (flctl->chip.options & NAND_BUSWIDTH_16)
			column <<= 1;
		set_addr(mtd, column, 0);

		flctl->read_bytes = 8;
		writel(flctl->read_bytes, FLDTCNTR(flctl)); /* set read size */
		empty_fifo(flctl);
		start_translation(flctl);
		read_fiforeg(flctl, flctl->read_bytes, 0);
		wait_completion(flctl);
		break;

	case NAND_CMD_ERASE1:
		flctl->erase1_page_addr = page_addr;
		break;

	case NAND_CMD_ERASE2:
		set_cmd_regs(mtd, NAND_CMD_ERASE1,
			(command << 8) | NAND_CMD_ERASE1);
		set_addr(mtd, -1, flctl->erase1_page_addr);
		start_translation(flctl);
		wait_completion(flctl);
		break;

	case NAND_CMD_SEQIN:
		if (!flctl->page_size) {
			/* output read command */
			if (column >= mtd->writesize) {
				column -= mtd->writesize;
				read_cmd = NAND_CMD_READOOB;
			} else if (column < 256) {
				read_cmd = NAND_CMD_READ0;
			} else {
				column -= 256;
				read_cmd = NAND_CMD_READ1;
			}
		}
		flctl->seqin_column = column;
		flctl->seqin_page_addr = page_addr;
		flctl->seqin_read_cmd = read_cmd;
		break;

	case NAND_CMD_PAGEPROG:
		empty_fifo(flctl);
		if (!flctl->page_size) {
			set_cmd_regs(mtd, NAND_CMD_SEQIN,
					flctl->seqin_read_cmd);
			set_addr(mtd, -1, -1);
			writel(0, FLDTCNTR(flctl));	/* set 0 size */
			start_translation(flctl);
			wait_completion(flctl);
		}
		if (flctl->hwecc) {
			/* write page with hwecc */
			if (flctl->seqin_column == mtd->writesize)
				execmd_write_oob(mtd);
			else if (!flctl->seqin_column)
				execmd_write_page_sector(mtd);
			else
				printk(KERN_ERR "Invalid address !?\n");
			break;
		}
		set_cmd_regs(mtd, command, (command << 8) | NAND_CMD_SEQIN);
		set_addr(mtd, flctl->seqin_column, flctl->seqin_page_addr);
		writel(flctl->index, FLDTCNTR(flctl));	/* set write size */
		start_translation(flctl);
		write_fiforeg(flctl, flctl->index, 0);
		wait_completion(flctl);
		break;

	case NAND_CMD_STATUS:
		set_cmd_regs(mtd, command, command);
		set_addr(mtd, -1, -1);

		flctl->read_bytes = 1;
		writel(flctl->read_bytes, FLDTCNTR(flctl)); /* set read size */
		start_translation(flctl);
		read_datareg(flctl, 0); /* read and end */
		break;

	case NAND_CMD_RESET:
		set_cmd_regs(mtd, command, command);
		set_addr(mtd, -1, -1);

		writel(0, FLDTCNTR(flctl));	/* set 0 size */
		start_translation(flctl);
		wait_completion(flctl);
		break;

	default:
		break;
	}
	goto runtime_exit;

read_normal_exit:
	writel(flctl->read_bytes, FLDTCNTR(flctl));	/* set read size */
	empty_fifo(flctl);
	start_translation(flctl);
	read_fiforeg(flctl, flctl->read_bytes, 0);
	wait_completion(flctl);
runtime_exit:
	pm_runtime_put_sync(&flctl->pdev->dev);
	return;
}

static void flctl_select_chip(struct mtd_info *mtd, int chipnr)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int ret;

	switch (chipnr) {
	case -1:
		flctl->flcmncr_base &= ~CE0_ENABLE;

		pm_runtime_get_sync(&flctl->pdev->dev);
		writel(flctl->flcmncr_base, FLCMNCR(flctl));

		if (flctl->qos_request) {
			dev_pm_qos_remove_request(&flctl->pm_qos);
			flctl->qos_request = 0;
		}

		pm_runtime_put_sync(&flctl->pdev->dev);
		break;
	case 0:
		flctl->flcmncr_base |= CE0_ENABLE;

		if (!flctl->qos_request) {
			ret = dev_pm_qos_add_request(&flctl->pdev->dev,
							&flctl->pm_qos, 100);
			if (ret < 0)
				dev_err(&flctl->pdev->dev,
					"PM QoS request failed: %d\n", ret);
			flctl->qos_request = 1;
		}

		if (flctl->holden) {
			pm_runtime_get_sync(&flctl->pdev->dev);
			writel(HOLDEN, FLHOLDCR(flctl));
			pm_runtime_put_sync(&flctl->pdev->dev);
		}
		break;
	default:
		BUG();
	}
}

static void flctl_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int i, index = flctl->index;

	for (i = 0; i < len; i++)
		flctl->done_buff[index + i] = buf[i];
	flctl->index += len;
}

static uint8_t flctl_read_byte(struct mtd_info *mtd)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int index = flctl->index;
	uint8_t data;

	data = flctl->done_buff[index];
	flctl->index++;
	return data;
}

static uint16_t flctl_read_word(struct mtd_info *mtd)
{
       struct sh_flctl *flctl = mtd_to_flctl(mtd);
       int index = flctl->index;
       uint16_t data;
       uint16_t *buf = (uint16_t *)&flctl->done_buff[index];

       data = *buf;
       flctl->index += 2;
       return data;
}

static void flctl_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		buf[i] = flctl_read_byte(mtd);
}

static int flctl_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (buf[i] != flctl_read_byte(mtd))
			return -EFAULT;
	return 0;
}

static int flctl_chip_init_tail(struct mtd_info *mtd)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	struct nand_chip *chip = &flctl->chip;

	if (mtd->writesize == 512) {
		flctl->page_size = 0;
		if (chip->chipsize > (32 << 20)) {
			/* big than 32MB */
			flctl->rw_ADRCNT = ADRCNT_4;
			flctl->erase_ADRCNT = ADRCNT_3;
		} else if (chip->chipsize > (2 << 16)) {
			/* big than 128KB */
			flctl->rw_ADRCNT = ADRCNT_3;
			flctl->erase_ADRCNT = ADRCNT_2;
		} else {
			flctl->rw_ADRCNT = ADRCNT_2;
			flctl->erase_ADRCNT = ADRCNT_1;
		}
	} else {
		flctl->page_size = 1;
		if (chip->chipsize > (128 << 20)) {
			/* big than 128MB */
			flctl->rw_ADRCNT = ADRCNT2_E;
			flctl->erase_ADRCNT = ADRCNT_3;
		} else if (chip->chipsize > (8 << 16)) {
			/* big than 512KB */
			flctl->rw_ADRCNT = ADRCNT_4;
			flctl->erase_ADRCNT = ADRCNT_2;
		} else {
			flctl->rw_ADRCNT = ADRCNT_3;
			flctl->erase_ADRCNT = ADRCNT_1;
		}
	}

	if (flctl->hwecc) {
		if (mtd->writesize == 512) {
			chip->ecc.layout = &flctl_4secc_oob_16;
			chip->badblock_pattern = &flctl_4secc_smallpage;
		} else {
			chip->ecc.layout = &flctl_4secc_oob_64;
			chip->badblock_pattern = &flctl_4secc_largepage;
		}

		chip->ecc.size = 512;
		chip->ecc.bytes = 10;
		chip->ecc.strength = 4;
		chip->ecc.read_page = flctl_read_page_hwecc;
		chip->ecc.write_page = flctl_write_page_hwecc;
		chip->ecc.mode = NAND_ECC_HW;

		/* 4 symbols ECC enabled */
		flctl->flcmncr_base |= _4ECCEN | ECCPOS2 | ECCPOS_02;
	} else {
		chip->ecc.mode = NAND_ECC_SOFT;
	}

	return 0;
}

static int __devinit flctl_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct sh_flctl *flctl;
	struct mtd_info *flctl_mtd;
	struct nand_chip *nand;
	struct sh_flctl_platform_data *pdata;
	int ret = -ENXIO;

	pdata = pdev->dev.platform_data;
	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	flctl = kzalloc(sizeof(struct sh_flctl), GFP_KERNEL);
	if (!flctl) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		goto err_iomap;
	}

	flctl->reg = ioremap(res->start, resource_size(res));
	if (flctl->reg == NULL) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		goto err_iomap;
	}

	platform_set_drvdata(pdev, flctl);
	flctl_mtd = &flctl->mtd;
	nand = &flctl->chip;
	flctl_mtd->priv = nand;
	flctl->pdev = pdev;
	flctl->flcmncr_base = pdata->flcmncr_val;
	flctl->hwecc = pdata->has_hwecc;
	flctl->holden = pdata->use_holden;

	/* Set address of hardware control function */
	/* 20 us command delay time */
	nand->chip_delay = 20;

	nand->read_byte = flctl_read_byte;
	nand->write_buf = flctl_write_buf;
	nand->read_buf = flctl_read_buf;
	nand->verify_buf = flctl_verify_buf;
	nand->select_chip = flctl_select_chip;
	nand->cmdfunc = flctl_cmdfunc;

	if (pdata->flcmncr_val & SEL_16BIT) {
		nand->options |= NAND_BUSWIDTH_16;
		nand->read_word = flctl_read_word;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	ret = nand_scan_ident(flctl_mtd, 1, NULL);
	if (ret)
		goto err_chip;

	ret = flctl_chip_init_tail(flctl_mtd);
	if (ret)
		goto err_chip;

	ret = nand_scan_tail(flctl_mtd);
	if (ret)
		goto err_chip;

	mtd_device_register(flctl_mtd, pdata->parts, pdata->nr_parts);

	return 0;

err_chip:
	pm_runtime_disable(&pdev->dev);
	iounmap(flctl->reg);
err_iomap:
	kfree(flctl);
	return ret;
}

static int __devexit flctl_remove(struct platform_device *pdev)
{
	struct sh_flctl *flctl = platform_get_drvdata(pdev);

	nand_release(&flctl->mtd);
	pm_runtime_disable(&pdev->dev);
	iounmap(flctl->reg);
	kfree(flctl);

	return 0;
}

static struct platform_driver flctl_driver = {
	.remove		= flctl_remove,
	.driver = {
		.name	= "sh_flctl",
		.owner	= THIS_MODULE,
	},
};

static int __init flctl_nand_init(void)
{
	return platform_driver_probe(&flctl_driver, flctl_probe);
}

static void __exit flctl_nand_cleanup(void)
{
	platform_driver_unregister(&flctl_driver);
}

module_init(flctl_nand_init);
module_exit(flctl_nand_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_DESCRIPTION("SuperH FLCTL driver");
MODULE_ALIAS("platform:sh_flctl");
