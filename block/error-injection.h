/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BLK_ERROR_INJECTION_H
#define _BLK_ERROR_INJECTION_H 1

#include <linux/jump_label.h>

DECLARE_STATIC_KEY_FALSE(blk_error_injection_enabled);

void blk_error_injection_init(struct gendisk *disk);
void blk_error_injection_exit(struct gendisk *disk);
bool __blk_error_inject(struct bio *bio);
static inline bool blk_error_inject(struct bio *bio)
{
	if (IS_ENABLED(CONFIG_BLK_ERROR_INJECTION) &&
	    static_branch_unlikely(&blk_error_injection_enabled) &&
	    test_bit(GD_ERROR_INJECT, &bio->bi_bdev->bd_disk->state))
		return __blk_error_inject(bio);
	return false;
}

#endif /* _BLK_ERROR_INJECTION_H */
