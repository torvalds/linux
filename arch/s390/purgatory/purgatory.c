// SPDX-License-Identifier: GPL-2.0
/*
 * Purgatory code running between two kernels.
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Philipp Rudo <prudo@linux.vnet.ibm.com>
 */

#include <linux/kexec.h>
#include <linux/sha256.h>
#include <linux/string.h>
#include <asm/purgatory.h>

struct kexec_sha_region purgatory_sha_regions[KEXEC_SEGMENT_MAX];
u8 purgatory_sha256_digest[SHA256_DIGEST_SIZE];

u64 kernel_entry;
u64 kernel_type;

u64 crash_start;
u64 crash_size;

int verify_sha256_digest(void)
{
	struct kexec_sha_region *ptr, *end;
	u8 digest[SHA256_DIGEST_SIZE];
	struct sha256_state sctx;

	sha256_init(&sctx);
	end = purgatory_sha_regions + ARRAY_SIZE(purgatory_sha_regions);

	for (ptr = purgatory_sha_regions; ptr < end; ptr++)
		sha256_update(&sctx, (uint8_t *)(ptr->start), ptr->len);

	sha256_final(&sctx, digest);

	if (memcmp(digest, purgatory_sha256_digest, sizeof(digest)))
		return 1;

	return 0;
}
