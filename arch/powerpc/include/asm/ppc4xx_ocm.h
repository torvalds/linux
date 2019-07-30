/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PowerPC 4xx OCM memory allocation support
 *
 * (C) Copyright 2009, Applied Micro Circuits Corporation
 * Victor Gallardo (vgallardo@amcc.com)
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 */

#ifndef __ASM_POWERPC_PPC4XX_OCM_H__
#define __ASM_POWERPC_PPC4XX_OCM_H__

#define PPC4XX_OCM_NON_CACHED 0
#define PPC4XX_OCM_CACHED     1

#if defined(CONFIG_PPC4xx_OCM)

void *ppc4xx_ocm_alloc(phys_addr_t *phys, int size, int align,
		  int flags, const char *owner);
void ppc4xx_ocm_free(const void *virt);

#else

#define ppc4xx_ocm_alloc(phys, size, align, flags, owner)	NULL
#define ppc4xx_ocm_free(addr)	((void)0)

#endif /* CONFIG_PPC4xx_OCM */

#endif  /* __ASM_POWERPC_PPC4XX_OCM_H__ */
