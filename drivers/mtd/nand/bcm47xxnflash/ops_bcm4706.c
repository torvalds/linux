/*
 * BCM47XX NAND flash driver
 *
 * Copyright (C) 2012 Rafał Miłecki <zajec5@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/bcma/bcma.h>

#include "bcm47xxnflash.h"

/* Broadcom uses 1'000'000 but it seems to be too many. Tests on WNDR4500 has
 * shown ~1000 retries as maxiumum. */
#define NFLASH_READY_RETRIES		10000

#define NFLASH_SECTOR_SIZE		512

#define NCTL_CMD0			0x00010000
#define NCTL_CMD1W			0x00080000
#define NCTL_READ			0x00100000
#define NCTL_WRITE			0x00200000
#define NCTL_SPECADDR			0x01000000
#define NCTL_READY			0x04000000
#define NCTL_ERR			0x08000000
#define NCTL_CSA			0x40000000
#define NCTL_START			0x80000000

/**************************************************
 * Various helpers
 **************************************************/

static inline u8 bcm47xxnflash_ops_bcm4706_ns_to_cycle(u16 ns, u16 clock)
{
	return ((ns * 1000 * clock) / 1000000) + 1;
}

static int bcm47xxnflash_ops_bcm4706_ctl_cmd(struct bcma_drv_cc *cc, u32 code)
{
	int i = 0;

	bcma_cc_write32(cc, BCMA_CC_NFLASH_CTL, NCTL_START | code);
	for (i = 0; i < NFLASH_READY_RETRIES; i++) {
		if (!(bcma_cc_read32(cc, BCMA_CC_NFLASH_CTL) & NCTL_START)) {
			i = 0;
			break;
		}
	}
	if (i) {
		pr_err("NFLASH control command not ready!\n");
		return -EBUSY;
	}
	return 0;
}

static int bcm47xxnflash_ops_bcm4706_poll(struct bcma_drv_cc *cc)
{
	int i;

	for (i = 0; i < NFLASH_READY_RETRIES; i++) {
		if (bcma_cc_read32(cc, BCMA_CC_NFLASH_CTL) & NCTL_READY) {
			if (bcma_cc_read32(cc, BCMA_CC_NFLASH_CTL) &
			    BCMA_CC_NFLASH_CTL_ERR) {
				pr_err("Error on polling\n");
				return -EBUSY;
			} else {
				return 0;
			}
		}
	}

	pr_err("Polling timeout!\n");
	return -EBUSY;
}

/**************************************************
 * R/W
 **************************************************/

static void bcm47xxnflash_ops_bcm4706_read(struct mtd_info *mtd, uint8_t *buf,
					   int len)
{
	struct nand_chip *nand_chip = (struct nand_chip *)mtd->priv;
	struct bcm47xxnflash *b47n = (struct bcm47xxnflash *)nand_chip->priv;

	u32 ctlcode;
	u32 *dest = (u32 *)buf;
	int i;
	int toread;

	BUG_ON(b47n->curr_page_addr & ~nand_chip->pagemask);
	/* Don't validate column using nand_chip->page_shift, it may be bigger
	 * when accessing OOB */

	while (len) {
		/* We can read maximum of 0x200 bytes at once */
		toread = min(len, 0x200);

		/* Set page and column */
		bcma_cc_write32(b47n->cc, BCMA_CC_NFLASH_COL_ADDR,
				b47n->curr_column);
		bcma_cc_write32(b47n->cc, BCMA_CC_NFLASH_ROW_ADDR,
				b47n->curr_page_addr);

		/* Prepare to read */
		ctlcode = NCTL_CSA | NCTL_CMD1W | 0x00040000 | 0x00020000 |
			  NCTL_CMD0;
		ctlcode |= NAND_CMD_READSTART << 8;
		if (bcm47xxnflash_ops_bcm4706_ctl_cmd(b47n->cc, ctlcode))
			return;
		if (bcm47xxnflash_ops_bcm4706_poll(b47n->cc))
			return;

		/* Eventually read some data :) */
		for (i = 0; i < toread; i += 4, dest++) {
			ctlcode = NCTL_CSA | 0x30000000 | NCTL_READ;
			if (i == toread - 4) /* Last read goes without that */
				ctlcode &= ~NCTL_CSA;
			if (bcm47xxnflash_ops_bcm4706_ctl_cmd(b47n->cc,
							      ctlcode))
				return;
			*dest = bcma_cc_read32(b47n->cc, BCMA_CC_NFLASH_DATA);
		}

		b47n->curr_column += toread;
		len -= toread;
	}
}

static void bcm47xxnflash_ops_bcm4706_write(struct mtd_info *mtd,
					    const uint8_t *buf, int len)
{
	struct nand_chip *nand_chip = (struct nand_chip *)mtd->priv;
	struct bcm47xxnflash *b47n = (struct bcm47xxnflash *)nand_chip->priv;
	struct bcma_drv_cc *cc = b47n->cc;

	u32 ctlcode;
	const u32 *data = (u32 *)buf;
	int i;

	BUG_ON(b47n->curr_page_addr & ~nand_chip->pagemask);
	/* Don't validate column using nand_chip->page_shift, it may be bigger
	 * when accessing OOB */

	for (i = 0; i < len; i += 4, data++) {
		bcma_cc_write32(cc, BCMA_CC_NFLASH_DATA, *data);

		ctlcode = NCTL_CSA | 0x30000000 | NCTL_WRITE;
		if (i == len - 4) /* Last read goes without that */
			ctlcode &= ~NCTL_CSA;
		if (bcm47xxnflash_ops_bcm4706_ctl_cmd(cc, ctlcode)) {
			pr_err("%s ctl_cmd didn't work!\n", __func__);
			return;
		}
	}

	b47n->curr_column += len;
}

/**************************************************
 * NAND chip ops
 **************************************************/

/* Default nand_select_chip calls cmd_ctrl, which is not used in BCM4706 */
static void bcm47xxnflash_ops_bcm4706_select_chip(struct mtd_info *mtd,
						  int chip)
{
	return;
}

/*
 * Default nand_command and nand_command_lp don't match BCM4706 hardware layout.
 * For example, reading chip id is performed in a non-standard way.
 * Setting column and page is also handled differently, we use a special
 * registers of ChipCommon core. Hacking cmd_ctrl to understand and convert
 * standard commands would be much more complicated.
 */
static void bcm47xxnflash_ops_bcm4706_cmdfunc(struct mtd_info *mtd,
					      unsigned command, int column,
					      int page_addr)
{
	struct nand_chip *nand_chip = (struct nand_chip *)mtd->priv;
	struct bcm47xxnflash *b47n = (struct bcm47xxnflash *)nand_chip->priv;
	struct bcma_drv_cc *cc = b47n->cc;
	u32 ctlcode;
	int i;

	if (column != -1)
		b47n->curr_column = column;
	if (page_addr != -1)
		b47n->curr_page_addr = page_addr;

	switch (command) {
	case NAND_CMD_RESET:
		pr_warn("Chip reset not implemented yet\n");
		break;
	case NAND_CMD_READID:
		ctlcode = NCTL_CSA | 0x01000000 | NCTL_CMD1W | NCTL_CMD0;
		ctlcode |= NAND_CMD_READID;
		if (bcm47xxnflash_ops_bcm4706_ctl_cmd(b47n->cc, ctlcode)) {
			pr_err("READID error\n");
			break;
		}

		/*
		 * Reading is specific, last one has to go without NCTL_CSA
		 * bit. We don't know how many reads NAND subsystem is going
		 * to perform, so cache everything.
		 */
		for (i = 0; i < ARRAY_SIZE(b47n->id_data); i++) {
			ctlcode = NCTL_CSA | NCTL_READ;
			if (i == ARRAY_SIZE(b47n->id_data) - 1)
				ctlcode &= ~NCTL_CSA;
			if (bcm47xxnflash_ops_bcm4706_ctl_cmd(b47n->cc,
							      ctlcode)) {
				pr_err("READID error\n");
				break;
			}
			b47n->id_data[i] =
				bcma_cc_read32(b47n->cc, BCMA_CC_NFLASH_DATA)
				& 0xFF;
		}

		break;
	case NAND_CMD_STATUS:
		ctlcode = NCTL_CSA | NCTL_CMD0 | NAND_CMD_STATUS;
		if (bcm47xxnflash_ops_bcm4706_ctl_cmd(cc, ctlcode))
			pr_err("STATUS command error\n");
		break;
	case NAND_CMD_READ0:
		break;
	case NAND_CMD_READOOB:
		if (page_addr != -1)
			b47n->curr_column += mtd->writesize;
		break;
	case NAND_CMD_ERASE1:
		bcma_cc_write32(cc, BCMA_CC_NFLASH_ROW_ADDR,
				b47n->curr_page_addr);
		ctlcode = 0x00040000 | NCTL_CMD1W | NCTL_CMD0 |
			  NAND_CMD_ERASE1 | (NAND_CMD_ERASE2 << 8);
		if (bcm47xxnflash_ops_bcm4706_ctl_cmd(cc, ctlcode))
			pr_err("ERASE1 failed\n");
		break;
	case NAND_CMD_ERASE2:
		break;
	case NAND_CMD_SEQIN:
		/* Set page and column */
		bcma_cc_write32(cc, BCMA_CC_NFLASH_COL_ADDR,
				b47n->curr_column);
		bcma_cc_write32(cc, BCMA_CC_NFLASH_ROW_ADDR,
				b47n->curr_page_addr);

		/* Prepare to write */
		ctlcode = 0x40000000 | 0x00040000 | 0x00020000 | 0x00010000;
		ctlcode |= NAND_CMD_SEQIN;
		if (bcm47xxnflash_ops_bcm4706_ctl_cmd(cc, ctlcode))
			pr_err("SEQIN failed\n");
		break;
	case NAND_CMD_PAGEPROG:
		if (bcm47xxnflash_ops_bcm4706_ctl_cmd(cc, 0x00010000 |
							  NAND_CMD_PAGEPROG))
			pr_err("PAGEPROG failed\n");
		if (bcm47xxnflash_ops_bcm4706_poll(cc))
			pr_err("PAGEPROG not ready\n");
		break;
	default:
		pr_err("Command 0x%X unsupported\n", command);
		break;
	}
	b47n->curr_command = command;
}

static u8 bcm47xxnflash_ops_bcm4706_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = (struct nand_chip *)mtd->priv;
	struct bcm47xxnflash *b47n = (struct bcm47xxnflash *)nand_chip->priv;
	struct bcma_drv_cc *cc = b47n->cc;
	u32 tmp = 0;

	switch (b47n->curr_command) {
	case NAND_CMD_READID:
		if (b47n->curr_column >= ARRAY_SIZE(b47n->id_data)) {
			pr_err("Requested invalid id_data: %d\n",
			       b47n->curr_column);
			return 0;
		}
		return b47n->id_data[b47n->curr_column++];
	case NAND_CMD_STATUS:
		if (bcm47xxnflash_ops_bcm4706_ctl_cmd(cc, NCTL_READ))
			return 0;
		return bcma_cc_read32(cc, BCMA_CC_NFLASH_DATA) & 0xff;
	case NAND_CMD_READOOB:
		bcm47xxnflash_ops_bcm4706_read(mtd, (u8 *)&tmp, 4);
		return tmp & 0xFF;
	}

	pr_err("Invalid command for byte read: 0x%X\n", b47n->curr_command);
	return 0;
}

static void bcm47xxnflash_ops_bcm4706_read_buf(struct mtd_info *mtd,
					       uint8_t *buf, int len)
{
	struct nand_chip *nand_chip = (struct nand_chip *)mtd->priv;
	struct bcm47xxnflash *b47n = (struct bcm47xxnflash *)nand_chip->priv;

	switch (b47n->curr_command) {
	case NAND_CMD_READ0:
	case NAND_CMD_READOOB:
		bcm47xxnflash_ops_bcm4706_read(mtd, buf, len);
		return;
	}

	pr_err("Invalid command for buf read: 0x%X\n", b47n->curr_command);
}

static void bcm47xxnflash_ops_bcm4706_write_buf(struct mtd_info *mtd,
						const uint8_t *buf, int len)
{
	struct nand_chip *nand_chip = (struct nand_chip *)mtd->priv;
	struct bcm47xxnflash *b47n = (struct bcm47xxnflash *)nand_chip->priv;

	switch (b47n->curr_command) {
	case NAND_CMD_SEQIN:
		bcm47xxnflash_ops_bcm4706_write(mtd, buf, len);
		return;
	}

	pr_err("Invalid command for buf write: 0x%X\n", b47n->curr_command);
}

/**************************************************
 * Init
 **************************************************/

int bcm47xxnflash_ops_bcm4706_init(struct bcm47xxnflash *b47n)
{
	int err;
	u32 freq;
	u16 clock;
	u8 w0, w1, w2, w3, w4;

	unsigned long chipsize; /* MiB */
	u8 tbits, col_bits, col_size, row_bits, row_bsize;
	u32 val;

	b47n->nand_chip.select_chip = bcm47xxnflash_ops_bcm4706_select_chip;
	b47n->nand_chip.cmdfunc = bcm47xxnflash_ops_bcm4706_cmdfunc;
	b47n->nand_chip.read_byte = bcm47xxnflash_ops_bcm4706_read_byte;
	b47n->nand_chip.read_buf = bcm47xxnflash_ops_bcm4706_read_buf;
	b47n->nand_chip.write_buf = bcm47xxnflash_ops_bcm4706_write_buf;
	b47n->nand_chip.bbt_options = NAND_BBT_USE_FLASH;
	b47n->nand_chip.ecc.mode = NAND_ECC_NONE; /* TODO: implement ECC */

	/* Enable NAND flash access */
	bcma_cc_set32(b47n->cc, BCMA_CC_4706_FLASHSCFG,
		      BCMA_CC_4706_FLASHSCFG_NF1);

	/* Configure wait counters */
	if (b47n->cc->status & BCMA_CC_CHIPST_4706_PKG_OPTION) {
		freq = 100000000;
	} else {
		freq = bcma_chipco_pll_read(b47n->cc, 4);
		freq = (freq * 0xFFF) >> 3;
		freq = (freq * 25000000) >> 3;
	}
	clock = freq / 1000000;
	w0 = bcm47xxnflash_ops_bcm4706_ns_to_cycle(15, clock);
	w1 = bcm47xxnflash_ops_bcm4706_ns_to_cycle(20, clock);
	w2 = bcm47xxnflash_ops_bcm4706_ns_to_cycle(10, clock);
	w3 = bcm47xxnflash_ops_bcm4706_ns_to_cycle(10, clock);
	w4 = bcm47xxnflash_ops_bcm4706_ns_to_cycle(100, clock);
	bcma_cc_write32(b47n->cc, BCMA_CC_NFLASH_WAITCNT0,
			(w4 << 24 | w3 << 18 | w2 << 12 | w1 << 6 | w0));

	/* Scan NAND */
	err = nand_scan(&b47n->mtd, 1);
	if (err) {
		pr_err("Could not scan NAND flash: %d\n", err);
		goto exit;
	}

	/* Configure FLASH */
	chipsize = b47n->nand_chip.chipsize >> 20;
	tbits = ffs(chipsize); /* find first bit set */
	if (!tbits || tbits != fls(chipsize)) {
		pr_err("Invalid flash size: 0x%lX\n", chipsize);
		err = -ENOTSUPP;
		goto exit;
	}
	tbits += 19; /* Broadcom increases *index* by 20, we increase *pos* */

	col_bits = b47n->nand_chip.page_shift + 1;
	col_size = (col_bits + 7) / 8;

	row_bits = tbits - col_bits + 1;
	row_bsize = (row_bits + 7) / 8;

	val = ((row_bsize - 1) << 6) | ((col_size - 1) << 4) | 2;
	bcma_cc_write32(b47n->cc, BCMA_CC_NFLASH_CONF, val);

exit:
	if (err)
		bcma_cc_mask32(b47n->cc, BCMA_CC_4706_FLASHSCFG,
			       ~BCMA_CC_4706_FLASHSCFG_NF1);
	return err;
}
