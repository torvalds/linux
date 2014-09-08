/*
 * purgatory: Runs between two kernels
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Author:
 *       Vivek Goyal <vgoyal@redhat.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include "sha256.h"
#include "../boot/string.h"

struct sha_region {
	unsigned long start;
	unsigned long len;
};

unsigned long backup_dest = 0;
unsigned long backup_src = 0;
unsigned long backup_sz = 0;

u8 sha256_digest[SHA256_DIGEST_SIZE] = { 0 };

struct sha_region sha_regions[16] = {};

/*
 * On x86, second kernel requries first 640K of memory to boot. Copy
 * first 640K to a backup region in reserved memory range so that second
 * kernel can use first 640K.
 */
static int copy_backup_region(void)
{
	if (backup_dest)
		memcpy((void *)backup_dest, (void *)backup_src, backup_sz);

	return 0;
}

int verify_sha256_digest(void)
{
	struct sha_region *ptr, *end;
	u8 digest[SHA256_DIGEST_SIZE];
	struct sha256_state sctx;

	sha256_init(&sctx);
	end = &sha_regions[sizeof(sha_regions)/sizeof(sha_regions[0])];
	for (ptr = sha_regions; ptr < end; ptr++)
		sha256_update(&sctx, (uint8_t *)(ptr->start), ptr->len);

	sha256_final(&sctx, digest);

	if (memcmp(digest, sha256_digest, sizeof(digest)))
		return 1;

	return 0;
}

void purgatory(void)
{
	int ret;

	ret = verify_sha256_digest();
	if (ret) {
		/* loop forever */
		for (;;)
			;
	}
	copy_backup_region();
}
