/*
 * SuperH FLCTL nand controller
 *
 * Copyright © 2008 Renesas Solutions Corp.
 * Copyright © 2008 Atom Create Engineering Co., Ltd.
 *
 * Based on fsl_elbc_nand.c, Copyright © 2006-2007 Freescale Semiconductor
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
	.options = 0,
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

	printk(KERN_ERR "wait_completion(): Timeout occured \n");
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
	printk(KERN_ERR "wait_rfifo_ready(): Timeout occured \n");
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
	printk(KERN_ERR "wait_wfifo_ready(): Timeout occured \n");
}

static int wait_recfifo_ready(struct sh_flctl *flctl)
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

				index = data >> 16;
				org = flctl->done_buff[index];
				flctl->done_buff[index] = org ^ (data & 0xFF);
				checked[i] = 1;
			}
		}

		writel(0, FL4ECCCR(flctl));
	}

	printk(KERN_ERR "wait_recfifo_ready(): Timeout occured \n");
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
	printk(KERN_ERR "wait_wecfifo_ready(): Timeout occured \n");
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

static int read_ecfiforeg(struct sh_flctl *flctl, uint8_t *buff)
{
	int i;
	unsigned long *ecc_buf = (unsigned long *)buff;
	void *fifo_addr = (void *)FLECFIFO(flctl);

	for (i = 0; i < 4; i++) {
		if (wait_recfifo_ready(flctl))
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
	uint32_t flcmncr_val = readl(FLCMNCR(flctl));
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
		addr_len_bytes = flctl->rw_ADRCNT;
		flcmdcr_val |= CDSRC_E;
		break;
	case NAND_CMD_SEQIN:
		/* This case is that cmd is READ0 or READ1 or READ00 */
		flcmdcr_val &= ~DOADR_E;	/* ONLY execute 1st cmd */
		break;
	case NAND_CMD_PAGEPROG:
		addr_len_bytes = flctl->rw_ADRCNT;
