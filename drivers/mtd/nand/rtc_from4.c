/*
 *  drivers/mtd/nand/rtc_from4.c
 *
 *  Copyright (C) 2004  Red Hat, Inc.
 *
 *  Derived from drivers/mtd/nand/spia.c
 *       Copyright (C) 2000 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Overview:
 *   This is a device driver for the AG-AND flash device found on the
 *   Renesas Technology Corp. Flash ROM 4-slot interface board (FROM_BOARD4),
 *   which utilizes the Renesas HN29V1G91T-30 part.
 *   This chip is a 1 GBibit (128MiB x 8 bits) AG-AND flash device.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/rslib.h>
#include <linux/bitrev.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>

/*
 * MTD structure for Renesas board
 */
static struct mtd_info *rtc_from4_mtd = NULL;

#define RTC_FROM4_MAX_CHIPS	2

/* HS77x9 processor register defines */
#define SH77X9_BCR1	((volatile unsigned short *)(0xFFFFFF60))
#define SH77X9_BCR2	((volatile unsigned short *)(0xFFFFFF62))
#define SH77X9_WCR1	((volatile unsigned short *)(0xFFFFFF64))
#define SH77X9_WCR2	((volatile unsigned short *)(0xFFFFFF66))
#define SH77X9_MCR	((volatile unsigned short *)(0xFFFFFF68))
#define SH77X9_PCR	((volatile unsigned short *)(0xFFFFFF6C))
#define SH77X9_FRQCR	((volatile unsigned short *)(0xFFFFFF80))

/*
 * Values specific to the Renesas Technology Corp. FROM_BOARD4 (used with HS77x9 processor)
 */
/* Address where flash is mapped */
#define RTC_FROM4_FIO_BASE	0x14000000

/* CLE and ALE are tied to address lines 5 & 4, respectively */
#define RTC_FROM4_CLE		(1 << 5)
#define RTC_FROM4_ALE		(1 << 4)

/* address lines A24-A22 used for chip selection */
#define RTC_FROM4_NAND_ADDR_SLOT3	(0x00800000)
#define RTC_FROM4_NAND_ADDR_SLOT4	(0x00C00000)
#define RTC_FROM4_NAND_ADDR_FPGA	(0x01000000)
/* mask address lines A24-A22 used for chip selection */
#define RTC_FROM4_NAND_ADDR_MASK	(RTC_FROM4_NAND_ADDR_SLOT3 | RTC_FROM4_NAND_ADDR_SLOT4 | RTC_FROM4_NAND_ADDR_FPGA)

/* FPGA status register for checking device ready (bit zero) */
#define RTC_FROM4_FPGA_SR		(RTC_FROM4_NAND_ADDR_FPGA | 0x00000002)
#define RTC_FROM4_DEVICE_READY		0x0001

/* FPGA Reed-Solomon ECC Control register */

#define RTC_FROM4_RS_ECC_CTL		(RTC_FROM4_NAND_ADDR_FPGA | 0x00000050)
#define RTC_FROM4_RS_ECC_CTL_CLR	(1 << 7)
#define RTC_FROM4_RS_ECC_CTL_GEN	(1 << 6)
#define RTC_FROM4_RS_ECC_CTL_FD_E	(1 << 5)

/* FPGA Reed-Solomon ECC code base */
#define RTC_FROM4_RS_ECC		(RTC_FROM4_NAND_ADDR_FPGA | 0x00000060)
#define RTC_FROM4_RS_ECCN		(RTC_FROM4_NAND_ADDR_FPGA | 0x00000080)

/* FPGA Reed-Solomon ECC check register */
#define RTC_FROM4_RS_ECC_CHK		(RTC_FROM4_NAND_ADDR_FPGA | 0x00000070)
#define RTC_FROM4_RS_ECC_CHK_ERROR	(1 << 7)

#define ERR_STAT_ECC_AVAILABLE		0x20

/* Undefine for software ECC */
#define RTC_FROM4_HWECC	1

/* Define as 1 for no virtual erase blocks (in JFFS2) */
#define RTC_FROM4_NO_VIRTBLOCKS	0

/*
 * Module stuff
 */
static void __iomem *rtc_from4_fio_base = (void *)P2SEGADDR(RTC_FROM4_FIO_BASE);

static const struct mtd_partition partition_info[] = {
	{
	 .name = "Renesas flash partition 1",
	 .offset = 0,
	 .size = MTDPART_SIZ_FULL},
};

#define NUM_PARTITIONS 1

/*
 *	hardware specific flash bbt decriptors
 *	Note: this is to allow debugging by disabling
 *		NAND_BBT_CREATE and/or NAND_BBT_WRITE
 *
 */
static uint8_t bbt_pattern[] = { 'B', 'b', 't', '0' };
static uint8_t mirror_pattern[] = { '1', 't', 'b', 'B' };

static struct nand_bbt_descr rtc_from4_bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 40,
	.len = 4,
	.veroffs = 44,
	.maxblocks = 4,
	.pattern = bbt_pattern
};

static struct nand_bbt_descr rtc_from4_bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 40,
	.len = 4,
	.veroffs = 44,
	.maxblocks = 4,
	.pattern = mirror_pattern
};

#ifdef RTC_FROM4_HWECC

/* the Reed Solomon control structure */
static struct rs_control *rs_decoder;

/*
 *      hardware specific Out Of Band information
 */
static struct nand_ecclayout rtc_from4_nand_oobinfo = {
	.eccbytes = 32,
	.eccpos = {
		   0, 1, 2, 3, 4, 5, 6, 7,
		   8, 9, 10, 11, 12, 13, 14, 15,
		   16, 17, 18, 19, 20, 21, 22, 23,
		   24, 25, 26, 27, 28, 29, 30, 31},
	.oobfree = {{32, 32}}
};

#endif

/*
 * rtc_from4_hwcontrol - hardware specific access to control-lines
 * @mtd:	MTD device structure
 * @cmd:	hardware control command
 *
 * Address lines (A5 and A4) are used to control Command and Address Latch
 * Enable on this board, so set the read/write address appropriately.
 *
 * Chip Enable is also controlled by the Chip Select (CS5) and
 * Address lines (A24-A22), so no action is required here.
 *
 */
static void rtc_from4_hwcontrol(struct mtd_info *mtd, int cmd,
				unsigned int ctrl)
{
	struct nand_chip *chip = (mtd->priv);

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, chip->IO_ADDR_W | RTC_FROM4_CLE);
	else
		writeb(cmd, chip->IO_ADDR_W | RTC_FROM4_ALE);
}

/*
 * rtc_from4_nand_select_chip - hardware specific chip select
 * @mtd:	MTD device structure
 * @chip:	Chip to select (0 == slot 3, 1 == slot 4)
 *
 * The chip select is based on address lines A24-A22.
 * This driver uses flash slots 3 and 4 (A23-A22).
 *
 */
static void rtc_from4_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *this = mtd->priv;

	this->IO_ADDR_R = (void __iomem *)((unsigned long)this->IO_ADDR_R & ~RTC_FROM4_NAND_ADDR_MASK);
	this->IO_ADDR_W = (void __iomem *)((unsigned long)this->IO_ADDR_W & ~RTC_FROM4_NAND_ADDR_MASK);

	switch (chip) {

	case 0:		/* select slot 3 chip */
		this->IO_ADDR_R = (void __iomem *)((unsigned long)this->IO_ADDR_R | RTC_FROM4_NAND_ADDR_SLOT3);
		this->IO_ADDR_W = (void __iomem *)((unsigned long)this->IO_ADDR_W | RTC_FROM4_NAND_ADDR_SLOT3);
		break;
	case 1:		/* select slot 4 chip */
		this->IO_ADDR_R = (void __iomem *)((unsigned long)this->IO_ADDR_R | RTC_FROM4_NAND_ADDR_SLOT4);
		this->IO_ADDR_W = (void __iomem *)((unsigned long)this->IO_ADDR_W | RTC_FROM4_NAND_ADDR_SLOT4);
		break;

	}
}

/*
 * rtc_from4_nand_device_ready - hardware specific ready/busy check
 * @mtd:	MTD device structure
 *
 * This board provides the Ready/Busy state in the status register
 * of the FPGA.  Bit zero indicates the RDY(1)/BSY(0) signal.
 *
 */
static int rtc_from4_nand_device_ready(struct mtd_info *mtd)
{
	unsigned short status;

	status = *((volatile unsigned short *)(rtc_from4_fio_base + RTC_FROM4_FPGA_SR));

	return (status & RTC_FROM4_DEVICE_READY);

}

/*
 * deplete - code to perform device recovery in case there was a power loss
 * @mtd:	MTD device structure
 * @chip:	Chip to select (0 == slot 3, 1 == slot 4)
 *
 * If there was a sudden loss of power during an erase operation, a
 * "device recovery" operation must be performed when power is restored
 * to ensure correct operation.  This routine performs the required steps
 * for the requested chip.
 *
 * See page 86 of the data sheet for details.
 *
 */
static void deplete(struct mtd_info *mtd, int chip)
{
	struct nand_chip *this = mtd->priv;

	/* wait until device is ready */
	while (!this->dev_ready(mtd)) ;

	this->select_chip(mtd, chip);

	/* Send the commands for device recovery, phase 1 */
	this->cmdfunc(mtd, NAND_CMD_DEPLETE1, 0x0000, 0x0000);
	this->cmdfunc(mtd, NAND_CMD_DEPLETE2, -1, -1);

	/* Send the commands for device recovery, phase 2 */
	this->cmdfunc(mtd, NAND_CMD_DEPLETE1, 0x0000, 0x0004);
	this->cmdfunc(mtd, NAND_CMD_DEPLETE2, -1, -1);

}

#ifdef RTC_FROM4_HWECC
/*
 * rtc_from4_enable_hwecc - hardware specific hardware ECC enable function
 * @mtd:	MTD device structure
 * @mode:	I/O mode; read or write
 *
 * enable hardware ECC for data read or write
 *
 */
static void rtc_from4_enable_hwecc(struct mtd_info *mtd, int mode)
{
	volatile unsigned short *rs_ecc_ctl = (volatile unsigned short *)(rtc_from4_fio_base + RTC_FROM4_RS_ECC_CTL);
	unsigned short status;

	switch (mode) {
	case NAND_ECC_READ:
		status = RTC_FROM4_RS_ECC_CTL_CLR | RTC_FROM4_RS_ECC_CTL_FD_E;

		*rs_ecc_ctl = status;
		break;

	case NAND_ECC_READSYN:
		status = 0x00;

		*rs_ecc_ctl = status;
		break;

	case NAND_ECC_WRITE:
		status = RTC_FROM4_RS_ECC_CTL_CLR | RTC_FROM4_RS_ECC_CTL_GEN | RTC_FROM4_RS_ECC_CTL_FD_E;

		*rs_ecc_ctl = status;
		break;

	default:
		BUG();
		break;
	}

}

/*
 * rtc_from4_calculate_ecc - hardware specific code to read ECC code
 * @mtd:	MTD device structure
 * @dat:	buffer containing the data to generate ECC codes
 * @ecc_code	ECC codes calculated
 *
 * The ECC code is calculated by the FPGA.  All we have to do is read the values
 * from the FPGA registers.
 *
 * Note: We read from the inverted registers, since data is inverted before
 * the code is calculated. So all 0xff data (blank page) results in all 0xff rs code
 *
 */
static void rtc_from4_calculate_ecc(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code)
{
	volatile unsigned short *rs_eccn = (volatile unsigned short *)(rtc_from4_fio_base + RTC_FROM4_RS_ECCN);
	unsigned short value;
	int i;

	for (i = 0; i < 8; i++) {
		value = *rs_eccn;
		ecc_code[i] = (unsigned char)value;
		rs_eccn++;
	}
	ecc_code[7] |= 0x0f;	/* set the last four bits (not used) */
}

/*
 * rtc_from4_correct_data - hardware specific code to correct data using ECC code
 * @mtd:	MTD device structure
 * @buf:	buffer containing the data to generate ECC codes
 * @ecc1	ECC codes read
 * @ecc2	ECC codes calculated
 *
 * The FPGA tells us fast, if there's an error or not. If no, we go back happy
 * else we read the ecc results from the fpga and call the rs library to decode
 * and hopefully correct the error.
 *
 */
static int rtc_from4_correct_data(struct mtd_info *mtd, const u_char *buf, u_char *ecc1, u_char *ecc2)
{
	int i, j, res;
	unsigned short status;
	uint16_t par[6], syn[6];
	uint8_t ecc[8];
	volatile unsigned short *rs_ecc;

	status = *((volatile unsigned short *)(rtc_from4_fio_base + RTC_FROM4_RS_ECC_CHK));

	if (!(status & RTC_FROM4_RS_ECC_CHK_ERROR)) {
		return 0;
	}

	/* Read the syndrom pattern from the FPGA and correct the bitorder */
	rs_ecc = (volatile unsigned short *)(rtc_from4_fio_base + RTC_FROM4_RS_ECC);
	for (i = 0; i < 8; i++) {
		ecc[i] = bitrev8(*rs_ecc);
		rs_ecc++;
	}

	/* convert into 6 10bit syndrome fields */
	par[5] = rs_decoder->index_of[(((uint16_t) ecc[0] >> 0) & 0x0ff) | (((uint16_t) ecc[1] << 8) & 0x300)];
	par[4] = rs_decoder->index_of[(((uint16_t) ecc[1] >> 2) & 0x03f) | (((uint16_t) ecc[2] << 6) & 0x3c0)];
	par[3] = rs_decoder->index_of[(((uint16_t) ecc[2] >> 4) & 0x00f) | (((uint16_t) ecc[3] << 4) & 0x3f0)];
	par[2] = rs_decoder->index_of[(((uint16_t) ecc[3] >> 6) & 0x003) | (((uint16_t) ecc[4] << 2) & 0x3fc)];
	par[1] = rs_decoder->index_of[(((uint16_t) ecc[5] >> 0) & 0x0ff) | (((uint16_t) ecc[6] << 8) & 0x300)];
	par[0] = (((uint16_t) ecc[6] >> 2) & 0x03f) | (((uint16_t) ecc[7] << 6) & 0x3c0);

	/* Convert to computable syndrome */
	for (i = 0; i < 6; i++) {
		syn[i] = par[0];
		for (j = 1; j < 6; j++)
			if (par[j] != rs_decoder->nn)
				syn[i] ^= rs_decoder->alpha_to[rs_modnn(rs_decoder, par[j] + i * j)];

		/* Convert to index form */
		syn[i] = rs_decoder->index_of[syn[i]];
	}

	/* Let the library code do its magic. */
	res = decode_rs8(rs_decoder, (uint8_t *) buf, par, 512, syn, 0, NULL, 0xff, NULL);
	if (res > 0) {
		DEBUG(MTD_DEBUG_LEVEL0, "rtc_from4_correct_data: " "ECC corrected %d errors on read\n", res);
	}
	return res;
}

/**
 * rtc_from4_errstat - perform additional error status checks
 * @mtd:	MTD device structure
 * @this:	NAND chip structure
 * @state:	state or the operation
 * @status:	status code returned from read status
 * @page:	startpage inside the chip, must be called with (page & this->pagemask)
 *
 * Perform additional error status checks on erase and write failures
 * to determine if errors are correctable.  For this device, correctable
 * 1-bit errors on erase and write are considered acceptable.
 *
 * note: see pages 34..37 of data sheet for details.
 *
 */
static int rtc_from4_errstat(struct mtd_info *mtd, struct nand_chip *this,
			     int state, int status, int page)
{
	int er_stat = 0;
	int rtn, retlen;
	size_t len;
	uint8_t *buf;
	int i;

	this->cmdfunc(mtd, NAND_CMD_STATUS_CLEAR, -1, -1);

	if (state == FL_ERASING) {

		for (i = 0; i < 4; i++) {
			if (!(status & 1 << (i + 1)))
				continue;
			this->cmdfunc(mtd, (NAND_CMD_STATUS_ERROR + i + 1),
				      -1, -1);
			rtn = this->read_byte(mtd);
			this->cmdfunc(mtd, NAND_CMD_STATUS_RESET, -1, -1);

			/* err_ecc_not_avail */
			if (!(rtn & ERR_STAT_ECC_AVAILABLE))
				er_stat |= 1 << (i + 1);
		}

	} else if (state == FL_WRITING) {

		unsigned long corrected = mtd->ecc_stats.corrected;

		/* single bank write logic */
		this->cmdfunc(mtd, NAND_CMD_STATUS_ERROR, -1, -1);
		rtn = this->read_byte(mtd);
		this->cmdfunc(mtd, NAND_CMD_STATUS_RESET, -1, -1);

		if (!(rtn & ERR_STAT_ECC_AVAILABLE)) {
			/* err_ecc_not_avail */
			er_stat |= 1 << 1;
			goto out;
		}

		len = mtd->writesize;
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf) {
			printk(KERN_ERR "rtc_from4_errstat: Out of memory!\n");
			er_stat = 1;
			goto out;
		}

		/* recovery read */
		rtn = nand_do_read(mtd, page, len, &retlen, buf);

		/* if read failed or > 1-bit error corrected */
		if (rtn || (mtd->ecc_stats.corrected - corrected) > 1)
			er_stat |= 1 << 1;
		kfree(buf);
	}
out:
	rtn = status;
	if (er_stat == 0) {	/* if ECC is available   */
		rtn = (status & ~NAND_STATUS_FAIL);	/*   clear the error bit */
	}

	return rtn;
}
#endif

/*
 * Main initialization routine
 */
static int __init rtc_from4_init(void)
{
	struct nand_chip *this;
	unsigned short bcr1, bcr2, wcr2;
	int i;
	int ret;

	/* Allocate memory for MTD device structure and private data */
	rtc_from4_mtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip), GFP_KERNEL);
	if (!rtc_from4_mtd) {
		printk("Unable to allocate Renesas NAND MTD device structure.\n");
		return -ENOMEM;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&rtc_from4_mtd[1]);

	/* Initialize structures */
	memset(rtc_from4_mtd, 0, sizeof(struct mtd_info));
	memset(this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	rtc_from4_mtd->priv = this;
	rtc_from4_mtd->owner = THIS_MODULE;

	/* set area 5 as PCMCIA mode to clear the spec of tDH(Data hold time;9ns min) */
	bcr1 = *SH77X9_BCR1 & ~0x0002;
	bcr1 |= 0x0002;
	*SH77X9_BCR1 = bcr1;

	/* set */
	bcr2 = *SH77X9_BCR2 & ~0x0c00;
	bcr2 |= 0x0800;
	*SH77X9_BCR2 = bcr2;

	/* set area 5 wait states */
	wcr2 = *SH77X9_WCR2 & ~0x1c00;
	wcr2 |= 0x1c00;
	*SH77X9_WCR2 = wcr2;

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = rtc_from4_fio_base;
	this->IO_ADDR_W = rtc_from4_fio_base;
	/* Set address of hardware control function */
	this->cmd_ctrl = rtc_from4_hwcontrol;
	/* Set address of chip select function */
	this->select_chip = rtc_from4_nand_select_chip;
	/* command delay time (in us) */
	this->chip_delay = 100;
	/* return the status of the Ready/Busy line */
	this->dev_ready = rtc_from4_nand_device_ready;

#ifdef RTC_FROM4_HWECC
	printk(KERN_INFO "rtc_from4_init: using hardware ECC detection.\n");

	this->ecc.mode = NAND_ECC_HW_SYNDROME;
	this->ecc.size = 512;
	this->ecc.bytes = 8;
	/* return the status of extra status and ECC checks */
	this->errstat = rtc_from4_errstat;
	/* set the nand_oobinfo to support FPGA H/W error detection */
	this->ecc.layout = &rtc_from4_nand_oobinfo;
	this->ecc.hwctl = rtc_from4_enable_hwecc;
	this->ecc.calculate = rtc_from4_calculate_ecc;
	this->ecc.correct = rtc_from4_correct_data;

	/* We could create the decoder on demand, if memory is a concern.
	 * This way we have it handy, if an error happens
	 *
	 * Symbolsize is 10 (bits)
	 * Primitve polynomial is x^10+x^3+1
	 * first consecutive root is 0
	 * primitve element to generate roots = 1
	 * generator polinomial degree = 6
	 */
	rs_decoder = init_rs(10, 0x409, 0, 1, 6);
	if (!rs_decoder) {
		printk(KERN_ERR "Could not create a RS decoder\n");
		ret = -ENOMEM;
		goto err_1;
	}
#else
	printk(KERN_INFO "rtc_from4_init: using software ECC detection.\n");

	this->ecc.mode = NAND_ECC_SOFT;
#endif

	/* set the bad block tables to support debugging */
	this->bbt_td = &rtc_from4_bbt_main_descr;
	this->bbt_md = &rtc_from4_bbt_mirror_descr;

	/* Scan to find existence of the device */
	if (nand_scan(rtc_from4_mtd, RTC_FROM4_MAX_CHIPS)) {
		ret = -ENXIO;
		goto err_2;
	}

	/* Perform 'device recovery' for each chip in case there was a power loss. */
	for (i = 0; i < this->numchips; i++) {
		deplete(rtc_from4_mtd, i);
	}

#if RTC_FROM4_NO_VIRTBLOCKS
	/* use a smaller erase block to minimize wasted space when a block is bad */
	/* note: this uses eight times as much RAM as using the default and makes */
	/*       mounts take four times as long. */
	rtc_from4_mtd->flags |= MTD_NO_VIRTBLOCKS;
#endif

	/* Register the partitions */
	ret = mtd_device_register(rtc_from4_mtd, partition_info,
				  NUM_PARTITIONS);
	if (ret)
		goto err_3;

	/* Return happy */
	return 0;
err_3:
	nand_release(rtc_from4_mtd);
err_2:
	free_rs(rs_decoder);
err_1:
	kfree(rtc_from4_mtd);
	return ret;
}

module_init(rtc_from4_init);

/*
 * Clean up routine
 */
static void __exit rtc_from4_cleanup(void)
{
	/* Release resource, unregister partitions */
	nand_release(rtc_from4_mtd);

	/* Free the MTD device structure */
	kfree(rtc_from4_mtd);

#ifdef RTC_FROM4_HWECC
	/* Free the reed solomon resources */
	if (rs_decoder) {
		free_rs(rs_decoder);
	}
#endif
}

module_exit(rtc_from4_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("d.marlin <dmarlin@redhat.com");
MODULE_DESCRIPTION("Board-specific glue layer for AG-AND flash on Renesas FROM_BOARD4");
