/*
 * PowerPC 4xx OCM memory allocation support
 *
 * (C) Copyright 2009, Applied Micro Circuits Corporation
 * Victor Gallardo (vgallardo@amcc.com)
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
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
