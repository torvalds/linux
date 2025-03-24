/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_MEM_ENCRYPT_H
#define __ASM_MEM_ENCRYPT_H

#include <asm/rsi.h>

struct arm64_mem_crypt_ops {
	int (*encrypt)(unsigned long addr, int numpages);
	int (*decrypt)(unsigned long addr, int numpages);
};

int arm64_mem_crypt_ops_register(const struct arm64_mem_crypt_ops *ops);

int set_memory_encrypted(unsigned long addr, int numpages);
int set_memory_decrypted(unsigned long addr, int numpages);

int realm_register_memory_enc_ops(void);

static inline bool force_dma_unencrypted(struct device *dev)
{
	return is_realm_world();
}

#endif	/* __ASM_MEM_ENCRYPT_H */
