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

#ifdef CONFIG_AMD_MEM_ENCRYPT

extern unsigned long sme_me_mask;

#else	/* !CONFIG_AMD_MEM_ENCRYPT */

#define sme_me_mask	0UL

#endif	/* CONFIG_AMD_MEM_ENCRYPT */

#endif	/* __ASSEMBLY__ */

#endif	/* __X86_MEM_ENCRYPT_H__ */
