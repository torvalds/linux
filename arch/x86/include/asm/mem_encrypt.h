/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __X86_MEM_ENCRYPT_H__
#define __X86_MEM_ENCRYPT_H__

#ifndef __ASSEMBLY__

#include <linux/init.h>

#include <asm/bootparam.h>

#ifdef CONFIG_AMD_MEM_ENCRYPT

extern u64 sme_me_mask;

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

void __init sme_encrypt_kernel(void);
void __init sme_enable(struct boot_params *bp);

/* Architecture __weak replacement functions */
void __init mem_encrypt_init(void);

void swiotlb_set_mem_attributes(void *vaddr, unsigned long size);

#else	/* !CONFIG_AMD_MEM_ENCRYPT */

#define sme_me_mask	0ULL

static inline void __init sme_early_encrypt(resource_size_t paddr,
					    unsigned long size) { }
static inline void __init sme_early_decrypt(resource_size_t paddr,
					    unsigned long size) { }

static inline void __init sme_map_bootdata(char *real_mode_data) { }
static inline void __init sme_unmap_bootdata(char *real_mode_data) { }

static inline void __init sme_early_init(void) { }

static inline void __init sme_encrypt_kernel(void) { }
static inline void __init sme_enable(struct boot_params *bp) { }

#endif	/* CONFIG_AMD_MEM_ENCRYPT */

/*
 * The __sme_pa() and __sme_pa_nodebug() macros are meant for use when
 * writing to or comparing values from the cr3 register.  Having the
 * encryption mask set in cr3 enables the PGD entry to be encrypted and
 * avoid special case handling of PGD allocations.
 */
#define __sme_pa(x)		(__pa(x) | sme_me_mask)
#define __sme_pa_nodebug(x)	(__pa_nodebug(x) | sme_me_mask)

#endif	/* __ASSEMBLY__ */

#endif	/* __X86_MEM_ENCRYPT_H__ */
