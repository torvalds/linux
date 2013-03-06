#ifndef __BCM47XXSFLASH_H
#define __BCM47XXSFLASH_H

#include <linux/mtd/mtd.h>

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
