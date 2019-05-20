/*
 * CRC vpmsum tester
 * Copyright 2017 Daniel Axtens, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/crc-t10dif.h>
#include <linux/crc32.h>
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <asm/switch_to.h>

static unsigned long iterations = 10000;

#define MAX_CRC_LENGTH 65535


static int __init crc_test_init(void)
{
	u16 crc16 = 0, verify16 = 0;
	u32 crc32 = 0, verify32 = 0;
	__le32 verify32le = 0;
	unsigned char *data;
	unsigned long i;
	int ret;

	struct crypto_shash *crct10dif_tfm;
	struct crypto_shash *crc32c_tfm;

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	data = kmalloc(MAX_CRC_LENGTH, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	crct10dif_tfm = crypto_alloc_shash("crct10dif", 0, 0);

	if (IS_ERR(crct10dif_tfm)) {
		pr_err("Error allocating crc-t10dif\n");
		goto free_buf;
	}

	crc32c_tfm = crypto_alloc_shash("crc32c", 0, 0);

	if (IS_ERR(crc32c_tfm)) {
		pr_err("Error allocating crc32c\n");
		goto free_16;
	}

	do {
		SHASH_DESC_ON_STACK(crct10dif_shash, crct10dif_tfm);
		SHASH_DESC_ON_STACK(crc32c_shash, crc32c_tfm);

		crct10dif_shash->tfm = crct10dif_tfm;
		ret = crypto_shash_init(crct10dif_shash);

		if (ret) {
			pr_err("Error initing crc-t10dif\n");
			goto free_32;
		}


		crc32c_shash->tfm = crc32c_tfm;
		ret = crypto_shash_init(crc32c_shash);

		if (ret) {
			pr_err("Error initing crc32c\n");
			goto free_32;
		}

		pr_info("crc-vpmsum_test begins, %lu iterations\n", iterations);
		for (i=0; i<iterations; i++) {
			size_t offset = prandom_u32_max(16);
			size_t len = prandom_u32_max(MAX_CRC_LENGTH);

			if (len <= offset)
				continue;
			prandom_bytes(data, len);
			len -= offset;

			crypto_shash_update(crct10dif_shash, data+offset, len);
			crypto_shash_final(crct10dif_shash, (u8 *)(&crc16));
			verify16 = crc_t10dif_generic(verify16, data+offset, len);


			if (crc16 != verify16) {
				pr_err("FAILURE in CRC16: got 0x%04x expected 0x%04x (len %lu)\n",
				       crc16, verify16, len);
				break;
			}

			crypto_shash_update(crc32c_shash, data+offset, len);
			crypto_shash_final(crc32c_shash, (u8 *)(&crc32));
			verify32 = le32_to_cpu(verify32le);
		        verify32le = ~cpu_to_le32(__crc32c_le(~verify32, data+offset, len));
			if (crc32 != (u32)verify32le) {
				pr_err("FAILURE in CRC32: got 0x%08x expected 0x%08x (len %lu)\n",
				       crc32, verify32, len);
				break;
			}
		}
		pr_info("crc-vpmsum_test done, completed %lu iterations\n", i);
	} while (0);

free_32:
	crypto_free_shash(crc32c_tfm);

free_16:
	crypto_free_shash(crct10dif_tfm);

free_buf:
	kfree(data);

	return 0;
}

static void __exit crc_test_exit(void) {}

module_init(crc_test_init);
module_exit(crc_test_exit);
module_param(iterations, long, 0400);

MODULE_AUTHOR("Daniel Axtens <dja@axtens.net>");
MODULE_DESCRIPTION("Vector polynomial multiply-sum CRC tester");
MODULE_LICENSE("GPL");
