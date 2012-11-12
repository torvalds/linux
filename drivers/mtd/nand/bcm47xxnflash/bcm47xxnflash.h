#ifndef __BCM47XXNFLASH_H
#define __BCM47XXNFLASH_H

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>

struct bcm47xxnflash {
	struct bcma_drv_cc *cc;

	struct nand_chip nand_chip;
	struct mtd_info mtd;
};

#endif /* BCM47XXNFLASH */
