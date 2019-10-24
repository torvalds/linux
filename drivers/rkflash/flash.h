/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __FLASH_H
#define __FLASH_H

#ifndef BIT
#define BIT(nr)			(1 << (nr))
#endif

#define MAX_FLASH_NUM			2
#define MAX_IDB_RESERVED_BLOCK		12

#define NAND_CACHE_READ_EN		BIT(0)
#define NAND_CACHE_RANDOM_READ_EN	BIT(1)
#define NAND_CACHE_PROG_EN		BIT(2)
#define NAND_MULTI_READ_EN		BIT(3)

#define NAND_MULTI_PROG_EN		BIT(4)
#define NAND_INTERLEAVE_EN		BIT(5)
#define NAND_READ_RETRY_EN		BIT(6)
#define NAND_RANDOMIZER_EN		BIT(7)

#define NAND_INTER_MODE_OFFSET		(0x8)
#define NAND_INTER_MODE_MARK		(0x07)
#define NAND_INTER_SDR_EN		BIT(0)
#define NAND_INTER_ONFI_EN		BIT(1)
#define NAND_INTER_TOGGLE_EN		BIT(2)

#define NAND_SDR_EN			BIT(8)
#define NAND_ONFI_EN			BIT(9)
#define NAND_TOGGLE_EN			BIT(10)
#define NAND_UNIQUE_ID_EN		BIT(11)

#define RESET_CMD		0xff
#define READ_ID_CMD		0x90
#define READ_STATUS_CMD		0x70
#define PAGE_PROG_CMD		0x8010
#define BLOCK_ERASE_CMD		0x60d0
#define READ_CMD		0x0030
#define READ_DP_OUT_CMD		0x05E0
#define READ_ECC_STATUS_CMD	0x7A

#define SAMSUNG			0x00	/* SAMSUNG */
#define TOSHIBA			0x01	/* TOSHIBA */
#define HYNIX			0x02	/* HYNIX */
#define INFINEON		0x03	/* INFINEON */
#define MICRON			0x04	/* MICRON */
#define RENESAS			0x05	/* RENESAS */
#define ST			0x06	/* ST */
#define INTEL			0x07	/* intel */
#define Sandisk			0x08	/* Sandisk */

#define RR_NONE			0x00
#define RR_HY_1			0x01	/* hynix H27UCG8T2M */
#define RR_HY_2			0x02	/* hynix H27UBG08U0B */
#define RR_HY_3			0x03	/* hynix H27UCG08U0B H27UBG08U0C */
#define RR_HY_4                 0x04	/* hynix H27UCG8T2A */
#define RR_HY_5                 0x05	/* hynix H27UCG8T2E */
#define RR_HY_6                 0x06	/* hynix H27QCG8T2F5R-BCG */
#define RR_MT_1                 0x11	/* micron */
#define RR_MT_2                 0x12	/* micron L94C L95B */
#define RR_TH_1                 0x21	/* toshiba */
#define RR_TH_2                 0x22	/* toshiba */
#define RR_TH_3                 0x23	/* toshiba */
#define RR_SS_1                 0x31	/* samsung */
#define RR_SD_1                 0x41	/* Sandisk */
#define RR_SD_2                 0x42	/* Sandisk */
#define RR_SD_3                 0x43	/* Sandisk */
#define RR_SD_4                 0x44	/* Sandisk */

/*  0 1 2 3 4 5 6 7 8 9 slc */
#define LSB_0	0
/*  0 1 2 3 6 7 A B E F hynix, micron 74A */
#define LSB_1	1
/*  0 1 3 5 7 9 B D toshiba samsung sandisk */
#define LSB_2	2
/*  0 1 2 3 4 5 8 9 C D 10 11 micron 84A */
#define LSB_3	3
/*  0 1 2 3 4 5 7 8 A B E F micron L95B */
#define LSB_4	4
/*  0 1 2 3 4 5 8 9 14 15 20 21 26 27 micron B74A TLC */
#define LSB_6	6
/*  0 3 6 9 C F 12 15 18 15 1B 1E 21 24 K9ABGD8U0C TLC */
#define LSB_7	7

/* BadBlockFlagMode */
/* first spare @ first page of each blocks */
#define BBF_1	1
/* first spare @ last page of each blocks */
#define BBF_2	2
/* first spare @ first and last page of each blocks */
#define BBF_11	3
/* sandisk 15nm flash prog first page without data and check status */
#define BBF_3	4

#define MPM_0	0	/* block 0 ~ 1 */
#define MPM_1	1	/* block 0 ~ 2048... */

struct NAND_PARA_INFO_T {
	u8	id_bytes;
	u8	nand_id[6];
	u8	vendor;
	u8	die_per_chip;
	u8	sec_per_page;
	u16	page_per_blk;
	u8	cell;	/* 1 slc , 2 mlc , 3 tlc */
	u8	plane_per_die;
	u16	 blk_per_plane;
	u16	operation_opt;
	u8	lsb_mode;
	u8	read_retry_mode;
	u8	ecc_bits;
	u8	access_freq;
	u8	opt_mode;
	u8	die_gap;
	u8	bad_block_mode;
	u8	multi_plane_mode;
	u8	reversd2[6];	/* 32 bytes */
};

extern struct nand_phy_info	g_nand_phy_info;
extern struct nand_ops		g_nand_ops;
extern void __iomem *nandc_base;

void nandc_flash_get_id(u8 cs, void *buf);
void nandc_flash_reset(u8 chip_sel);
u32 nandc_flash_init(void __iomem *nandc_addr);
u32 nandc_flash_deinit(void);

#endif
