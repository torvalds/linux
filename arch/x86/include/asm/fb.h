#ifndef _ASM_X86_FB_H
#define _ASM_X86_FB_H

#include <linux/fb.h>
#include <linux/fs.h>
#include <asm/page.h>

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	unsigned long prot;

	prot = pgprot_val(vma->vm_page_prot) & ~_PAGE_CACHE_MASK;
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) =
			prot | cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS);
}

extern int fb_is_primary_device(struct fb_info *info);

#endif /* _ASM_X86_FB_H */
