/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <asm/page.h>
#include <asm/setup.h>

static inline pgprot_t pgprot_framebuffer(pgprot_t prot,
					  unsigned long vm_start, unsigned long vm_end,
					  unsigned long offset)
{
#ifdef CONFIG_MMU
#ifdef CONFIG_SUN3
	pgprot_val(prot) |= SUN3_PAGE_NOCACHE;
#else
	if (CPU_IS_020_OR_030)
		pgprot_val(prot) |= _PAGE_NOCACHE030;
	if (CPU_IS_040_OR_060) {
		pgprot_val(prot) &= _CACHEMASK040;
		/* Use no-cache mode, serialized */
		pgprot_val(prot) |= _PAGE_NOCACHE_S;
	}
#endif /* CONFIG_SUN3 */
#endif /* CONFIG_MMU */

	return prot;
}
#define pgprot_framebuffer pgprot_framebuffer

#include <asm-generic/fb.h>

#endif /* _ASM_FB_H_ */
