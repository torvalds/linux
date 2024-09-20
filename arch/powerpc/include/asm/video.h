/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_VIDEO_H_
#define _ASM_VIDEO_H_

#include <asm/page.h>

static inline pgprot_t pgprot_framebuffer(pgprot_t prot,
					  unsigned long vm_start, unsigned long vm_end,
					  unsigned long offset)
{
	return __phys_mem_access_prot(PHYS_PFN(offset), vm_end - vm_start, prot);
}
#define pgprot_framebuffer pgprot_framebuffer

#include <asm-generic/video.h>

#endif /* _ASM_VIDEO_H_ */
