#ifndef _ASM_VIDEO_H_
#define _ASM_VIDEO_H_

#include <asm/page.h>

static inline pgprot_t pgprot_framebuffer(pgprot_t prot,
					  unsigned long vm_start, unsigned long vm_end,
					  unsigned long offset)
{
	return pgprot_noncached(prot);
}
#define pgprot_framebuffer pgprot_framebuffer

/*
 * MIPS doesn't define __raw_ I/O macros, so the helpers
 * in <asm-generic/video.h> don't generate fb_readq() and
 * fb_writeq(). We have to provide them here.
 *
 * TODO: Convert MIPS to generic I/O. The helpers below can
 *       then be removed.
 */
#ifdef CONFIG_64BIT
static inline u64 fb_readq(const volatile void __iomem *addr)
{
	return __raw_readq(addr);
}
#define fb_readq fb_readq

static inline void fb_writeq(u64 b, volatile void __iomem *addr)
{
	__raw_writeq(b, addr);
}
#define fb_writeq fb_writeq
#endif

#include <asm-generic/video.h>

#endif /* _ASM_VIDEO_H_ */
