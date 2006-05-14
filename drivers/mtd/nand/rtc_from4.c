/*
 *  drivers/mtd/nand/rtc_from4.c
 *
 *  Copyright (C) 2004  Red Hat, Inc.
 *
 *  Derived from drivers/mtd/nand/spia.c
 *       Copyright (C) 2000 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * $Id: rtc_from4.c,v 1.10 2005/11/07 11:14:31 gleixner Exp $
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
#include <linux/module.h>
#include <linux/mtd/compatmac.h>
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
static struct nand_oobinfo rtc_from4_nand_oobinfo = {
	.useecc = MTD_NANDECC_AUTOPLACE,
	.eccbytes = 32,
	.eccpos = {
		   0, 1, 2, 3, 4, 5, 6, 7,
		   8, 9, 10, 11, 12, 13, 14, 15,
		   16, 17, 18, 19, 20, 21, 22, 23,
		   24, 25, 26, 27, 28, 29, 30, 31},
	.oobfree = {{32, 32}}
};

/* Aargh. I missed the reversed bit order, when I
 * was talking to Renesas about the FPGA.
 *
 * The table is used for bit reordering and inversion
 * of the ecc byte which we get from the FPGA
 */
static uint8_t revbits[256] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
	0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
	0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
	0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
	0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
	0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
	0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
	0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
	0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
	0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
	0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
	0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
	0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
	0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
	0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
	0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
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
static void rtc_from4_hwcontrol(struct mtd_info *mtd, int cmd)
{
	struct nand_chip *this = (struct nand_chip *)(mtd->priv);

	switch (cmd) {

	case NAND_CTL_SETCLE:
		this->IO_ADDR_W = (void __iomem *)((unsigned long)this->IO_ADDR_W | RTC_FROM4_CLE);
		break;
	case NAND_CTL_CLRCLE:
		this->IO_ADDR_W = (void __iomem *)((unsigned long)this->IO_ADDR_W & ~RTC_FROM4_CLE);
		break;

	case NAND_CTL_SETALE:
		this->IO_ADDR_W = (void __iomem *)((unsigned long)this->IO_ADDR_W | RTC_FROM4_ALE);
		break;
	case NAND_CTL_CLRALE:
		this->IO_ADDR_W = (void __iomem *)((unsigned long)this->IO_ADDR_W & ~RTC_FROM4_ALE);
		break;

	case NAND_CTL_SETNCE:
		break;
	case NAND_CTL_CLRNCE:
		break;

	}
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
		ecc[i] = revbits[(*rs_ecc) & 0xFF];
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
static int rtc_from4_errstat(struct mtd_info *mtd, struct nand_chip *this, int state, int status, int page)
{
	int er_stat = 0;
	int rtn, retlen;
	size_t len;
	uint8_t *buf;
	int i;

	this->cmdfunc(mtd, NAND_CMD_STATUS_CLEAR, -1, -1);

	if (state == FL_ERASING) {
		for (i = 0; i < 4; i++) {
			if (status & 1 << (i + 1)) {
				this->cmdfunc(mtd, (NAND_CMD_STATUS_ERROR + i + 1), -1, -1);
				rtn = this->read_byte(mtd);
				this->cmdfunc(mtd, NAND_CMD_STATUS_RESET, -1, -1);
				if (!(rtn & ERR_STAT_ECC_AVAILABLE)) {
					er_stat |= 1 << (i + 1);	/* err_ecc_not_avail */
				}
			}
		}
	} else if (state == FL_WRITING) {
		/* single bank write logic */
		this->cmdfunc(mtd, NAND_CMD_STATUS_ERROR, -1, -1);
		rtn = this->read_byte(mtd);
		this->cmdfunc(mtd, NAND_CMD_STATUS_RESET, -1, -1);
		if (!(rtn & ERR_STAT_ECC_AVAILABLE)) {
			er_stat |= 1 << 1;	/* err_ecc_not_avail */
		} else {
			len = mtd->oobblock;
			buf = kmalloc(len, GFP_KERNEL);
			if (!buf) {
				printk(KERN_ERR "rtc_from4_errstat: Out of memory!\n");
				er_stat = 1;	/* if we can't check, assume failed */
			} else {
				/* recovery read */
				/* page read */
				rtn = nand_do_read_ecc(mtd, page, len, &retlen, buf, NULL, this->autooob, 1);
				if (rtn) {	/* if read failed or > 1-bit error corrected */
					er_stat |= 1 << 1;	/* ECC read failed */
				}
				kfree(buf);
			}
		}
	}

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
int __init rtc_from4_init(void)
{
	struct nand_chip *this;
	unsigned short bcr1, bcr2, wcr2;
	int i;

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
	this->hwcontrol = rtc_from4_hwcontrol;
	/* Set address of chip select function */
	this->select_chip = rtc_from4_nand_select_chip;
	/* command delay time (in us) */
	this->chip_delay = 100;
	/* return the status of the Ready/Busy line */
	this->dev_ready = rtc_from4_nand_device_ready;

#ifdef RTC_FROM4_HWECC
	printk(KERN_INFO "rtc_from4_init: using hardware ECC detection.\n");

	this->eccmode = NAND_ECC_HW8_512;
	this->options |= NAND_HWECC_SYNDROME;
	/* return the status of extra status and ECC checks */
	this->errstat = rtc_from4_errstat;
	/* set the nand_oobinfo to support FPGA H/W error detection */
	this->autooob = &rtc_from4_nand_oobinfo;
	this->enable_hwecc = rtc_from4_enable_hwecc;
	this->calculate_ecc = rtc_from4_calculate_ecc;
	this->correct_data = rtc_from4_correct_data;
#else
	printk(KERN_INFO "rtc_from4_init: using software ECC detection.\n");

	this->eccmode = NAND_ECC_SOFT;
#endif

	/* set the bad block tables to support debugging */
	this->bbt_td = &rtc_from4_bbt_main_descr;
	this->bbt_md = &rtc_from4_bbt_mirror_descr;

	/* Scan to find existence of the device */
	if (nand_scan(rtc_from4_mtd, RTC_FROM4_MAX_CHIPS)) {
		kfree(rtc_from4_mtd);
		return -ENXIO;
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
	add_mtd_partitions(rtc_from4_mtd, partition_info, NUM_PARTITIONS);

#ifdef RTC_FROM4_HWECC
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
		nand_release(rtc_from4_mtd);
		kfree(rtc_from4_mtd);
		return -ENOMEM;
	}
#endif
	/* Return happy */
	return 0;
}

module_init(rtc_from4_init);

/*
 * Clean up routine
 */
#ifdef MODULE
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
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("d.marlin <dmarlin@redhat.com");
MODULE_DESCRIPTION("Board-specific glue layer for AG-AND flash on Renesas FROM_BOARD4");
