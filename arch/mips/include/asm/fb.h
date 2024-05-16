#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <asm/page.h>

struct file;

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
}
#define fb_pgprotect fb_pgprotect

/*
 * MIPS doesn't define __raw_ I/O macros, so the helpers
 * in <asm-generic/fb.h> don't generate fb_readq() and
 * fb_write(). We have to provide them here.
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

#include <asm-generic/fb.h>

#endif /* _ASM_FB_H_ */
