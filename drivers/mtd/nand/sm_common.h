/*
 * Copyright © 2009 - Maxim Levitsky
 * Common routines & support for SmartMedia/xD format
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/bitops.h>
#include <linux/mtd/mtd.h>

/* Full oob structure as written on the flash */
struct sm_oob {
	uint32_t reserved;
	uint8_t data_status;
	uint8_t block_status;
	uint8_t lba_copy1[2];
	uint8_t ecc2[3];
	uint8_t lba_copy2[2];
	uint8_t ecc1[3];
} __attribute__((packed));


/* one sector is always 512 bytes, but it can consist of two nand pages */
#define SM_SECTOR_SIZE		512

/* oob area is also 16 bytes, but might be from two pages */
#define SM_OOB_SIZE		16

/* This is maximum zone size, and all devices that have more that one zone
   have this size */
#define SM_MAX_ZONE_SIZE 	1024

/* support for small page nand */
#define SM_SMALL_PAGE 		256
#define SM_SMALL_OOB_SIZE	8


extern int sm_register_device(struct mtd_info *mtd, int smartmedia);


static inline int sm_sector_valid(struct sm_oob *oob)
{
	return hweight16(oob->data_status) >= 5;
}

static inline int sm_block_valid(struct sm_oob *oob)
{
	return hweight16(oob->block_status) >= 7;
}

static inline int sm_block_erased(struct sm_oob *oob)
{
	static const uint32_t erased_pattern[4] = {
		0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

	/* First test for erased block */
	if (!memcmp(oob, erased_pattern, sizeof(*oob)))
		return 1;
	return 0;
}
