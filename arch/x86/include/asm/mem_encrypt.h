/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#ifndef __X86_MEM_ENCRYPT_H__
#define __X86_MEM_ENCRYPT_H__

#ifndef __ASSEMBLER__

#include <linux/init.h>
#include <linux/cc_platform.h>

#include <asm/asm.h>
struct boot_params;

#ifdef CONFIG_X86_MEM_ENCRYPT
void __init mem_encrypt_init(void);
void __init mem_encrypt_setup_arch(void);
#else
static inline void mem_encrypt_init(void) { }
static inline void __init mem_encrypt_setup_arch(void) { }
#endif

#ifdef CONFIG_AMD_MEM_ENCRYPT

extern u64 sme_me_mask;
extern u64 sev_status;

void sme_encrypt_execute(unsigned long encrypted_kernel_vaddr,
			 unsigned long decrypted_kernel_vaddr,
			 unsigned long kernel_len,
			 unsigned long encryption_wa,
			 unsigned long encryption_pgd);

void __init sme_early_encrypt(resource_size_t paddr,
			      unsigned long size);
void __init sme_early_decrypt(resource_size_t paddr,
			      unsigned long size);

void __init sme_map_bootdata(char *real_mode_data);
void __init sme_unmap_bootdata(char *real_mode_data);

void __init sme_early_init(void);

void sme_encrypt_kernel(struct boot_params *bp);
void sme_enable(struct boot_params *bp);

int __init early_set_memory_decrypted(unsigned long vaddr, unsigned long size);
int __init early_set_memory_encrypted(unsigned long vaddr, unsigned long size);
void __init early_set_mem_enc_dec_hypercall(unsigned long vaddr,
					    unsigned long size, bool enc);

void __init mem_encrypt_free_decrypted_mem(void);

void __init sev_es_init_vc_handling(void);

static inline u64 sme_get_me_mask(void)
{
	return RIP_REL_REF(sme_me_mask);
}

#define __bss_decrypted __section(".bss..decrypted")

#else	/* !CONFIG_AMD_MEM_ENCRYPT */

#define sme_me_mask	0ULL
#define sev_status	0ULL

static inline void __init sme_early_encrypt(resource_size_t paddr,
					    unsigned long size) { }
static inline void __init sme_early_decrypt(resource_size_t paddr,
					    unsigned long size) { }

static inline void __init sme_map_bootdata(char *real_mode_data) { }
static inline void __init sme_unmap_bootdata(char *real_mode_data) { }

static inline void __init sme_early_init(void) { }

static inline void sme_encrypt_kernel(struct boot_params *bp) { }
static inline void sme_enable(struct boot_params *bp) { }

static inline void sev_es_init_vc_handling(void) { }

static inline int __init
early_set_memory_decrypted(unsigned long vaddr, unsigned long size) { return 0; }
static inline int __init
early_set_memory_encrypted(unsigned long vaddr, unsigned long size) { return 0; }
static inline void __init
early_set_mem_enc_dec_hypercall(unsigned long vaddr, unsigned long size, bool enc) {}

static inline void mem_encrypt_free_decrypted_mem(void) { }

static inline u64 sme_get_me_mask(void) { return 0; }

#define __bss_decrypted

#endif	/* CONFIG_AMD_MEM_ENCRYPT */

void add_encrypt_protection_map(void);

/*
 * The __sme_pa() and __sme_pa_nodebug() macros are meant for use when
 * writing to or comparing values from the cr3 register.  Having the
 * encryption mask set in cr3 enables the PGD entry to be encrypted and
 * avoid special case handling of PGD allocations.
 */
#define __sme_pa(x)		(__pa(x) | sme_me_mask)
#define __sme_pa_nodebug(x)	(__pa_nodebug(x) | sme_me_mask)

extern char __start_bss_decrypted[], __end_bss_decrypted[], __start_bss_decrypted_unused[];

#endif	/* __ASSEMBLER__ */

#endif	/* __X86_MEM_ENCRYPT_H__ */
