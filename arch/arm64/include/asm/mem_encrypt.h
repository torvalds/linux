/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_MEM_ENCRYPT_H
#define __ASM_MEM_ENCRYPT_H

struct arm64_mem_crypt_ops {
	int (*encrypt)(unsigned long addr, int numpages);
	int (*decrypt)(unsigned long addr, int numpages);
};

int arm64_mem_crypt_ops_register(const struct arm64_mem_crypt_ops *ops);

int set_memory_encrypted(unsigned long addr, int numpages);
int set_memory_decrypted(unsigned long addr, int numpages);

#endif	/* __ASM_MEM_ENCRYPT_H */
