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

#ifdef CONFIG_AMD_MEM_ENCRYPT

extern unsigned long sme_me_mask;

void __init sme_early_init(void);

void __init sme_encrypt_kernel(void);
void __init sme_enable(void);

#else	/* !CONFIG_AMD_MEM_ENCRYPT */

#define sme_me_mask	0UL

static inline void __init sme_early_init(void) { }

static inline void __init sme_encrypt_kernel(void) { }
static inline void __init sme_enable(void) { }

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
