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

#include <asm-generic/fb.h>

#endif /* _ASM_FB_H_ */
