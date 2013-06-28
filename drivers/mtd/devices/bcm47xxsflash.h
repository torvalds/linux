#ifndef __BCM47XXSFLASH_H
#define __BCM47XXSFLASH_H

#include <linux/mtd/mtd.h>

/* Used for ST flashes only. */
#define OPCODE_ST_WREN		0x0006		/* Write Enable */
#define OPCODE_ST_WRDIS		0x0004		/* Write Disable */
#define OPCODE_ST_RDSR		0x0105		/* Read Status Register */
#define OPCODE_ST_WRSR		0x0101		/* Write Status Register */
#define OPCODE_ST_READ		0x0303		/* Read Data Bytes */
#define OPCODE_ST_PP		0x0302		/* Page Program */
#define OPCODE_ST_SE		0x02d8		/* Sector Erase */
#define OPCODE_ST_BE		0x00c7		/* Bulk Erase */
#define OPCODE_ST_DP		0x00b9		/* Deep Power-down */
#define OPCODE_ST_RES		0x03ab		/* Read Electronic Signature */
#define OPCODE_ST_CSA		0x1000		/* Keep chip select asserted */
#define OPCODE_ST_SSE		0x0220		/* Sub-sector Erase */

/* Used for Atmel flashes only. */
#define OPCODE_AT_READ				0x07e8
#define OPCODE_AT_PAGE_READ			0x07d2
#define OPCODE_AT_STATUS			0x01d7
#define OPCODE_AT_BUF1_WRITE			0x0384
#define OPCODE_AT_BUF2_WRITE			0x0387
#define OPCODE_AT_BUF1_ERASE_PROGRAM		0x0283
#define OPCODE_AT_BUF2_ERASE_PROGRAM		0x0286
#define OPCODE_AT_BUF1_PROGRAM			0x0288
#define OPCODE_AT_BUF2_PROGRAM			0x0289
#define OPCODE_AT_PAGE_ERASE			0x0281
#define OPCODE_AT_BLOCK_ERASE			0x0250
#define OPCODE_AT_BUF1_WRITE_ERASE_PROGRAM	0x0382
#define OPCODE_AT_BUF2_WRITE_ERASE_PROGRAM	0x0385
#define OPCODE_AT_BUF1_LOAD			0x0253
#define OPCODE_AT_BUF2_LOAD			0x0255
#define OPCODE_AT_BUF1_COMPARE			0x0260
#define OPCODE_AT_BUF2_COMPARE			0x0261
#define OPCODE_AT_BUF1_REPROGRAM		0x0258
#define OPCODE_AT_BUF2_REPROGRAM		0x0259

/* Status register bits for ST flashes */
#define SR_ST_WIP		0x01		/* Write In Progress */
#define SR_ST_WEL		0x02		/* Write Enable Latch */
#define SR_ST_BP_MASK		0x1c		/* Block Protect */
#define SR_ST_BP_SHIFT		2
#define SR_ST_SRWD		0x80		/* Status Register Write Disable */

/* Status register bits for Atmel flashes */
#define SR_AT_READY		0x80
#define SR_AT_MISMATCH		0x40
#define SR_AT_ID_MASK		0x38
#define SR_AT_ID_SHIFT		3

struct bcma_drv_cc;

enum bcm47xxsflash_type {
	BCM47XXSFLASH_TYPE_ATMEL,
	BCM47XXSFLASH_TYPE_ST,
};

struct bcm47xxsflash {
	struct bcma_drv_cc *bcma_cc;

	enum bcm47xxsflash_type type;

	u32 window;
	u32 blocksize;
	u16 numblocks;
	u32 size;

	struct mtd_info mtd;
};

#endif /* BCM47XXSFLASH */
