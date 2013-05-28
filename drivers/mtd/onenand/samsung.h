/*
 * linux/arch/arm/plat-s3c/include/plat/regs-onenand.h
 *
 *  Copyright (C) 2008-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __SAMSUNG_ONENAND_H__
#define __SAMSUNG_ONENAND_H__

/*
 * OneNAND Controller
 */
#define MEM_CFG_OFFSET		0x0000
#define BURST_LEN_OFFSET	0x0010
#define MEM_RESET_OFFSET	0x0020
#define INT_ERR_STAT_OFFSET	0x0030
#define INT_ERR_MASK_OFFSET	0x0040
#define INT_ERR_ACK_OFFSET	0x0050
#define ECC_ERR_STAT_OFFSET	0x0060
#define MANUFACT_ID_OFFSET	0x0070
#define DEVICE_ID_OFFSET	0x0080
#define DATA_BUF_SIZE_OFFSET	0x0090
#define BOOT_BUF_SIZE_OFFSET	0x00A0
#define BUF_AMOUNT_OFFSET	0x00B0
#define TECH_OFFSET		0x00C0
#define FBA_WIDTH_OFFSET	0x00D0
#define FPA_WIDTH_OFFSET	0x00E0
#define FSA_WIDTH_OFFSET	0x00F0
#define TRANS_SPARE_OFFSET	0x0140
#define DBS_DFS_WIDTH_OFFSET	0x0160
#define INT_PIN_ENABLE_OFFSET	0x01A0
#define ACC_CLOCK_OFFSET	0x01C0
#define FLASH_VER_ID_OFFSET	0x01F0
#define FLASH_AUX_CNTRL_OFFSET	0x0300		/* s3c64xx only */

#define ONENAND_MEM_RESET_HOT	0x3
#define ONENAND_MEM_RESET_COLD	0x2
#define ONENAND_MEM_RESET_WARM	0x1

#define CACHE_OP_ERR		(1 << 13)
#define RST_CMP			(1 << 12)
#define RDY_ACT			(1 << 11)
#define INT_ACT			(1 << 10)
#define UNSUP_CMD		(1 << 9)
#define LOCKED_BLK		(1 << 8)
#define BLK_RW_CMP		(1 << 7)
#define ERS_CMP			(1 << 6)
#define PGM_CMP			(1 << 5)
#define LOAD_CMP		(1 << 4)
#define ERS_FAIL		(1 << 3)
#define PGM_FAIL		(1 << 2)
#define INT_TO			(1 << 1)
#define LD_FAIL_ECC_ERR		(1 << 0)

#define TSRF			(1 << 0)

#endif
