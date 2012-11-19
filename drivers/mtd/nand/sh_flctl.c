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
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_mtd.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/string.h>

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
	.eccbytes = 4 * 10,
	.eccpos = {
		 6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
		22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
		54, 55, 56, 57, 58, 59, 60, 61, 62, 63 },
	.oobfree = {
		{.offset =  2, .length = 4},
		{.offset = 16, .length = 6},
		{.offset = 32, .length = 6},
		{.offset = 48, .length = 6} },
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
	.offs = 0,
	.len = 2,
	.pattern = scan_ff_pattern,
};

static void empty_fifo(struct sh_flctl *flctl)
{
	writel(flctl->flintdmacr_base | AC1CLR | AC0CLR, FLINTDMACR(flctl));
	writel(flctl->flintdmacr_base, FLINTDMACR(flctl));
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

static void flctl_dma_complete(void *param)
{
	struct sh_flctl *flctl = param;

	complete(&flctl->dma_complete);
}

static void flctl_release_dma(struct sh_flctl *flctl)
{
	if (flctl->chan_fifo0_rx) {
		dma_release_channel(flctl->chan_fifo0_rx);
		flctl->chan_fifo0_rx = NULL;
	}
	if (flctl->chan_fifo0_tx) {
		dma_release_channel(flctl->chan_fifo0_tx);
		flctl->chan_fifo0_tx = NULL;
	}
}

static void flctl_setup_dma(struct sh_flctl *flctl)
{
	dma_cap_mask_t mask;
	struct dma_slave_config cfg;
	struct platform_device *pdev = flctl->pdev;
	struct sh_flctl_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	if (!pdata)
		return;

	if (pdata->slave_id_fifo0_tx <= 0 || pdata->slave_id_fifo0_rx <= 0)
		return;

	/* We can only either use DMA for both Tx and Rx or not use it at all */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	flctl->chan_fifo0_tx = dma_request_channel(mask, shdma_chan_filter,
					    (void *)pdata->slave_id_fifo0_tx);
	dev_dbg(&pdev->dev, "%s: TX: got channel %p\n", __func__,
		flctl->chan_fifo0_tx);

	if (!flctl->chan_fifo0_tx)
		return;

	memset(&cfg, 0, sizeof(cfg));
	cfg.slave_id = pdata->slave_id_fifo0_tx;
	cfg.direction = DMA_MEM_TO_DEV;
	cfg.dst_addr = (dma_addr_t)FLDTFIFO(flctl);
	cfg.src_addr = 0;
	ret = dmaengine_slave_config(flctl->chan_fifo0_tx, &cfg);
	if (ret < 0)
		goto err;

	flctl->chan_fifo0_rx = dma_request_channel(mask, shdma_chan_filter,
					    (void *)pdata->slave_id_fifo0_rx);
	dev_dbg(&pdev->dev, "%s: RX: got channel %p\n", __func__,
		flctl->chan_fifo0_rx);

	if (!flctl->chan_fifo0_rx)
		goto err;

	cfg.slave_id = pdata->slave_id_fifo0_rx;
	cfg.direction = DMA_DEV_TO_MEM;
	cfg.dst_addr = 0;
	cfg.src_addr = (dma_addr_t)FLDTFIFO(flctl);
	ret = dmaengine_slave_config(flctl->chan_fifo0_rx, &cfg);
	if (ret < 0)
		goto err;

	init_completion(&flctl->dma_complete);

	return;

err:
	flctl_release_dma(flctl);
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

static enum flctl_ecc_res_t wait_recfifo_ready
		(struct sh_flctl *flctl, int sector_number)
{
	uint32_t timeout = LOOP_TIMEOUT_MAX;
	void __iomem *ecc_reg[4];
	int i;
	int state = FL_SUCCESS;
	uint32_t data, size;

	/*
	 * First this loops checks in FLDTCNTR if we are ready to read out the
	 * oob data. This is the case if either all went fine without errors or
	 * if the bottom part of the loop corrected the errors or marked them as
	 * uncorrectable and the controller is given time to push the data into
	 * the FIFO.
	 */
	while (timeout--) {
		/* check if all is ok and we can read out the OOB */
		size = readl(FLDTCNTR(flctl)) >> 24;
		if ((size & 0xFF) == 4)
			return state;

		/* check if a correction code has been calculated */
		if (!(readl(FL4ECCCR(flctl)) & _4ECCEND)) {
			/*
			 * either we wait for the fifo to be filled or a
			 * correction pattern is being generated
			 */
			udelay(1);
			continue;
		}

		/* check for an uncorrectable error */
		if (readl(FL4ECCCR(flctl)) & _4ECCFA) {
			/* check if we face a non-empty page */
			for (i = 0; i < 512; i++) {
				if (flctl->done_buff[i] != 0xff) {
					state = FL_ERROR; /* can't correct */
					break;
				}
			}

			if (state == FL_SUCCESS)
				dev_dbg(&flctl->pdev->dev,
				"reading empty sector %d, ecc error ignored\n",
				sector_number);

			writel(0, FL4ECCCR(flctl));
			continue;
		}

		/* start error correction */
		ecc_reg[0] = FL4ECCRESULT0(flctl);
		ecc_reg[1] = FL4ECCRESULT1(flctl);
		ecc_reg[2] = FL4ECCRESULT2(flctl);
		ecc_reg[3] = FL4ECCRESULT3(flctl);

		for (i = 0; i < 3; i++) {
			uint8_t org;
			unsigned int index;

			data = readl(ecc_reg[i]);

			if (flctl->page_size)
				index = (512 * sector_number) +
					(data >> 16);
			else
				index = data >> 16;

			org = flctl->done_buff[index];
			flctl->done_buff[index] = org ^ (data & 0xFF);
		}
		state = FL_REPAIRABLE;
		writel(0, FL4ECCCR(flctl));
	}

	timeout_error(flctl, __func__);
	return FL_TIMEOUT;	/* timeout */
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

static int flctl_dma_fifo0_transfer(struct sh_flctl *flctl, unsigned long *buf,
					int len, enum dma_data_direction dir)
{
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan;
	enum dma_transfer_direction tr_dir;
	dma_addr_t dma_addr;
	dma_cookie_t cookie = -EINVAL;
	uint32_t reg;
	int ret;

	if (dir == DMA_FROM_DEVICE) {
		chan = flctl->chan_fifo0_rx;
		tr_dir = DMA_DEV_TO_MEM;
	} else {
		chan = flctl->chan_fifo0_tx;
		tr_dir = DMA_MEM_TO_DEV;
	}

	dma_addr = dma_map_single(chan->device->dev, buf, len, dir);

	if (dma_addr)
		desc = dmaengine_prep_slave_single(chan, dma_addr, len,
			tr_dir, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (desc) {
		reg = readl(FLINTDMACR(flctl));
		reg |= DREQ0EN;
		writel(reg, FLINTDMACR(flctl));

		desc->callback = flctl_dma_complete;
		desc->callback_param = flctl;
		cookie = dmaengine_submit(desc);

		dma_async_issue_pending(chan);
	} else {
		/* DMA failed, fall back to PIO */
		flctl_release_dma(flctl);
		dev_warn(&flctl->pdev->dev,
			 "DMA failed, falling back to PIO\n");
		ret = -EIO;
		goto out;
	}

	ret =
	wait_for_completion_timeout(&flctl->dma_complete,
				msecs_to_jiffies(3000));

	if (ret <= 0) {
		chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
		dev_err(&flctl->pdev->dev, "wait_for_completion_timeout\n");
	}

out:
	reg = readl(FLINTDMACR(flctl));
	reg &= ~DREQ0EN;
	writel(reg, FLINTDMACR(flctl));

	dma_unmap_single(chan->device->dev, dma_addr, len, dir);

	/* ret > 0 is success */
	return ret;
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

	len_4align = (rlen + 3) / 4;

	/* initiate DMA transfer */
	if (flctl->chan_fifo0_rx && rlen >= 32 &&
		flctl_dma_fifo0_transfer(flctl, buf, rlen, DMA_DEV_TO_MEM) > 0)
			goto convert;	/* DMA success */

	/* do polling transfer */
	for (i = 0; i < len_4align; i++) {
		wait_rfifo_ready(flctl);
		buf[i] = readl(FLDTFIFO(flctl));
	}

convert:
	for (i = 0; i < len_4align; i++)
		buf[i] = be32_to_cpu(buf[i]);
}

static enum flctl_ecc_res_t read_ecfiforeg
		(struct sh_flctl *flctl, uint8_t *buff, int sector)
{
	int i;
	enum flctl_ecc_res_t res;
	unsigned long *ecc_buf = (unsigned long *)buff;

	res = wait_recfifo_ready(flctl , sector);

	if (res != FL_ERROR) {
		for (i = 0; i < 4; i++) {
			ecc_buf[i] = readl(FLECFIFO(flctl));
			ecc_buf[i] = be32_to_cpu(ecc_buf[i]);
		}
	}

	return res;
}

static void write_fiforeg(struct sh_flctl *flctl, int rlen,
						unsigned int offset)
{
	int i, len_4align;
	unsigned long *buf = (unsigned long *)&flctl->done_buff[offset];

	len_4align = (rlen + 3) / 4;
	for (i = 0; i < len_4align; i++) {
		wait_wfifo_ready(flctl);
		writel(cpu_to_be32(buf[i]), FLDTFIFO(flctl));
	}
}

static void write_ec_fiforeg(struct sh_flctl *flctl, int rlen,
						unsigned int offset)
{
	int i, len_4align;
	unsigned long *buf = (unsigned long *)&flctl->done_buff[offset];

	len_4align = (rlen + 3) / 4;

	for (i = 0; i < len_4align; i++)
		buf[i] = cpu_to_be32(buf[i]);

	/* initiate DMA transfer */
	if (flctl->chan_fifo0_tx && rlen >= 32 &&
		flctl_dma_fifo0_transfer(flctl, buf, rlen, DMA_MEM_TO_DEV) > 0)
			return;	/* DMA success */

	/* do polling transfer */
	for (i = 0; i < len_4align; i++) {
		wait_wecfifo_ready(flctl);
		writel(buf[i], FLECFIFO(flctl));
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
	chip->read_buf(mtd, buf, mtd->writesize);
	if (oob_required)
		chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);
	return 0;
}

static int flctl_write_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
				   const uint8_t *buf, int oob_required)
{
	chip->write_buf(mtd, buf, mtd->writesize);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	return 0;
}

static void execmd_read_page_sector(struct mtd_info *mtd, int page_addr)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int sector, page_sectors;
	enum flctl_ecc_res_t ecc_result;

	page_sectors = flctl->page_size ? 4 : 1;

	set_cmd_regs(mtd, NAND_CMD_READ0,
		(NAND_CMD_READSTART << 8) | NAND_CMD_READ0);

	writel(readl(FLCMNCR(flctl)) | ACM_SACCES_MODE | _4ECCCORRECT,
		 FLCMNCR(flctl));
	writel(readl(FLCMDCR(flctl)) | page_sectors, FLCMDCR(flctl));
	writel(page_addr << 2, FLADR(flctl));

	empty_fifo(flctl);
	start_translation(flctl);

	for (sector = 0; sector < page_sectors; sector++) {
		read_fiforeg(flctl, 512, 512 * sector);

		ecc_result = read_ecfiforeg(flctl,
			&flctl->done_buff[mtd->writesize + 16 * sector],
			sector);

		switch (ecc_result) {
		case FL_REPAIRABLE:
			dev_info(&flctl->pdev->dev,
				"applied ecc on page 0x%x", page_addr);
			flctl->mtd.ecc_stats.corrected++;
			break;
		case FL_ERROR:
			dev_warn(&flctl->pdev->dev,
				"page 0x%x contains corrupted data\n",
				page_addr);
			flctl->mtd.ecc_stats.failed++;
			break;
		default:
			;
		}
	}

	wait_completion(flctl);

	writel(readl(FLCMNCR(flctl)) & ~(ACM_SACCES_MODE | _4ECCCORRECT),
			FLCMNCR(flctl));
}

static void execmd_read_oob(struct mtd_info *mtd, int page_addr)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int page_sectors = flctl->page_size ? 4 : 1;
	int i;

	set_cmd_regs(mtd, NAND_CMD_READ0,
		(NAND_CMD_READSTART << 8) | NAND_CMD_READ0);

	empty_fifo(flctl);

	for (i = 0; i < page_sectors; i++) {
		set_addr(mtd, (512 + 16) * i + 512 , page_addr);
		writel(16, FLDTCNTR(flctl));

		start_translation(flctl);
		read_fiforeg(flctl, 16, 16 * i);
		wait_completion(flctl);
	}
}

static void execmd_write_page_sector(struct mtd_info *mtd)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int page_addr = flctl->seqin_page_addr;
	int sector, page_sectors;

	page_sectors = flctl->page_size ? 4 : 1;

	set_cmd_regs(mtd, NAND_CMD_PAGEPROG,
			(NAND_CMD_PAGEPROG << 8) | NAND_CMD_SEQIN);

	empty_fifo(flctl);
	writel(readl(FLCMNCR(flctl)) | ACM_SACCES_MODE, FLCMNCR(flctl));
	writel(readl(FLCMDCR(flctl)) | page_sectors, FLCMDCR(flctl));
	writel(page_addr << 2, FLADR(flctl));
	start_translation(flctl);

	for (sector = 0; sector < page_sectors; sector++) {
		write_fiforeg(flctl, 512, 512 * sector);
		write_ec_fiforeg(flctl, 16, mtd->writesize + 16 * sector);
	}

	wait_completion(flctl);
	writel(readl(FLCMNCR(flctl)) & ~ACM_SACCES_MODE, FLCMNCR(flctl));
}

static void execmd_write_oob(struct mtd_info *mtd)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	int page_addr = flctl->seqin_page_addr;
	int sector, page_sectors;

	page_sectors = flctl->page_size ? 4 : 1;

	set_cmd_regs(mtd, NAND_CMD_PAGEPROG,
			(NAND_CMD_PAGEPROG << 8) | NAND_CMD_SEQIN);

	for (sector = 0; sector < page_sectors; sector++) {
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

	memcpy(&flctl->done_buff[flctl->index], buf, len);
	flctl->index += len;
}

static uint8_t flctl_read_byte(struct mtd_info *mtd)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	uint8_t data;

	data = flctl->done_buff[flctl->index];
	flctl->index++;
	return data;
}

static uint16_t flctl_read_word(struct mtd_info *mtd)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);
	uint16_t *buf = (uint16_t *)&flctl->done_buff[flctl->index];

	flctl->index += 2;
	return *buf;
}

static void flctl_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct sh_flctl *flctl = mtd_to_flctl(mtd);

	memcpy(buf, &flctl->done_buff[flctl->index], len);
	flctl->index += len;
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
		flctl->flcmncr_base |= _4ECCEN;
	} else {
		chip->ecc.mode = NAND_ECC_SOFT;
	}

	return 0;
}

static irqreturn_t flctl_handle_flste(int irq, void *dev_id)
{
	struct sh_flctl *flctl = dev_id;

	dev_err(&flctl->pdev->dev, "flste irq: %x\n", readl(FLINTDMACR(flctl)));
	writel(flctl->flintdmacr_base, FLINTDMACR(flctl));

	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
struct flctl_soc_config {
	unsigned long flcmncr_val;
	unsigned has_hwecc:1;
	unsigned use_holden:1;
};

static struct flctl_soc_config flctl_sh7372_config = {
	.flcmncr_val = CLK_16B_12L_4H | TYPESEL_SET | SHBUSSEL,
	.has_hwecc = 1,
	.use_holden = 1,
};

static const struct of_device_id of_flctl_match[] = {
	{ .compatible = "renesas,shmobile-flctl-sh7372",
				.data = &flctl_sh7372_config },
	{},
};
MODULE_DEVICE_TABLE(of, of_flctl_match);

static struct sh_flctl_platform_data *flctl_parse_dt(struct device *dev)
{
	const struct of_device_id *match;
	struct flctl_soc_config *config;
	struct sh_flctl_platform_data *pdata;
	struct device_node *dn = dev->of_node;
	int ret;

	match = of_match_device(of_flctl_match, dev);
	if (match)
		config = (struct flctl_soc_config *)match->data;
	else {
		dev_err(dev, "%s: no OF configuration attached\n", __func__);
		return NULL;
	}

	pdata = devm_kzalloc(dev, sizeof(struct sh_flctl_platform_data),
								GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "%s: failed to allocate config data\n", __func__);
		return NULL;
	}

	/* set SoC specific options */
	pdata->flcmncr_val = config->flcmncr_val;
	pdata->has_hwecc = config->has_hwecc;
	pdata->use_holden = config->use_holden;

	/* parse user defined options */
	ret = of_get_nand_bus_width(dn);
	if (ret == 16)
		pdata->flcmncr_val |= SEL_16BIT;
	else if (ret != 8) {
		dev_err(dev, "%s: invalid bus width\n", __func__);
		return NULL;
	}

	return pdata;
}
#else /* CONFIG_OF */
#define of_flctl_match NULL
static struct sh_flctl_platform_data *flctl_parse_dt(struct device *dev)
{
	return NULL;
}
#endif /* CONFIG_OF */

static int flctl_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct sh_flctl *flctl;
	struct mtd_info *flctl_mtd;
	struct nand_chip *nand;
	struct sh_flctl_platform_data *pdata;
	int ret = -ENXIO;
	int irq;
	struct mtd_part_parser_data ppdata = {};

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

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get flste irq data\n");
		goto err_flste;
	}

	ret = request_irq(irq, flctl_handle_flste, IRQF_SHARED, "flste", flctl);
	if (ret) {
		dev_err(&pdev->dev, "request interrupt failed.\n");
		goto err_flste;
	}

	if (pdev->dev.of_node)
		pdata = flctl_parse_dt(&pdev->dev);
	else
		pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "no setup data defined\n");
		ret = -EINVAL;
		goto err_pdata;
	}

	platform_set_drvdata(pdev, flctl);
	flctl_mtd = &flctl->mtd;
	nand = &flctl->chip;
	flctl_mtd->priv = nand;
	flctl->pdev = pdev;
	flctl->hwecc = pdata->has_hwecc;
	flctl->holden = pdata->use_holden;
	flctl->flcmncr_base = pdata->flcmncr_val;
	flctl->flintdmacr_base = flctl->hwecc ? (STERINTE | ECERB) : STERINTE;

	/* Set address of hardware control function */
	/* 20 us command delay time */
	nand->chip_delay = 20;

	nand->read_byte = flctl_read_byte;
	nand->write_buf = flctl_write_buf;
	nand->read_buf = flctl_read_buf;
	nand->select_chip = flctl_select_chip;
	nand->cmdfunc = flctl_cmdfunc;

	if (pdata->flcmncr_val & SEL_16BIT) {
		nand->options |= NAND_BUSWIDTH_16;
		nand->read_word = flctl_read_word;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	flctl_setup_dma(flctl);

	ret = nand_scan_ident(flctl_mtd, 1, NULL);
	if (ret)
		goto err_chip;

	ret = flctl_chip_init_tail(flctl_mtd);
	if (ret)
		goto err_chip;

	ret = nand_scan_tail(flctl_mtd);
	if (ret)
		goto err_chip;

	ppdata.of_node = pdev->dev.of_node;
	ret = mtd_device_parse_register(flctl_mtd, NULL, &ppdata, pdata->parts,
			pdata->nr_parts);

	return 0;

err_chip:
	flctl_release_dma(flctl);
	pm_runtime_disable(&pdev->dev);
err_pdata:
	free_irq(irq, flctl);
err_flste:
	iounmap(flctl->reg);
err_iomap:
	kfree(flctl);
	return ret;
}

static int __devexit flctl_remove(struct platform_device *pdev)
{
	struct sh_flctl *flctl = platform_get_drvdata(pdev);

	flctl_release_dma(flctl);
	nand_release(&flctl->mtd);
	pm_runtime_disable(&pdev->dev);
	free_irq(platform_get_irq(pdev, 0), flctl);
	iounmap(flctl->reg);
	kfree(flctl);

	return 0;
}

static struct platform_driver flctl_driver = {
	.remove		= flctl_remove,
	.driver = {
		.name	= "sh_flctl",
		.owner	= THIS_MODULE,
		.of_match_table = of_flctl_match,
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
