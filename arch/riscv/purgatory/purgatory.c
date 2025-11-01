// SPDX-License-Identifier: GPL-2.0-only
/*
 * purgatory: Runs between two kernels
 *
 * Copyright (C) 2022 Huawei Technologies Co, Ltd.
 *
 * Author: Li Zhengyu (lizhengyu3@huawei.com)
 *
 */

#include <linux/purgatory.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/string.h>

u8 purgatory_sha256_digest[SHA256_DIGEST_SIZE] __section(".kexec-purgatory");

struct kexec_sha_region purgatory_sha_regions[KEXEC_SEGMENT_MAX] __section(".kexec-purgatory");

static int verify_sha256_digest(void)
{
	struct kexec_sha_region *ptr, *end;
	struct sha256_ctx sctx;
	u8 digest[SHA256_DIGEST_SIZE];

	sha256_init(&sctx);
	end = purgatory_sha_regions + ARRAY_SIZE(purgatory_sha_regions);
	for (ptr = purgatory_sha_regions; ptr < end; ptr++)
		sha256_update(&sctx, (uint8_t *)(ptr->start), ptr->len);
	sha256_final(&sctx, digest);
	if (memcmp(digest, purgatory_sha256_digest, sizeof(digest)) != 0)
		return 1;
	return 0;
}

/* workaround for a warning with -Wmissing-prototypes */
void purgatory(void);

void purgatory(void)
{
	if (verify_sha256_digest())
		for (;;)
			/* loop forever */
			;
}
