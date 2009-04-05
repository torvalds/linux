#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

/*
 * Nothing too fancy for now.
 *
 * On ARM we already have well known fixed virtual addresses imposed by
 * the architecture such as the vector page which is located at 0xffff0000,
 * therefore a second level page table is already allocated covering
 * 0xfff00000 upwards.
 *
 * The cache flushing code in proc-xscale.S uses the virtual area between
 * 0xfffe0000 and 0xfffeffff.
 */

#define FIXADDR_START		0xfff00000UL
#define FIXADDR_TOP		0xfffe0000UL
#define FIXADDR_SIZE		(FIXADDR_TOP - FIXADDR_START)

#define FIX_KMAP_BEGIN		0
#define FIX_KMAP_END		(FIXADDR_SIZE >> PAGE_SHIFT)

#define __fix_to_virt(x)	(FIXADDR_START + ((x) << PAGE_SHIFT))
#define __virt_to_fix(x)	(((x) - FIXADDR_START) >> PAGE_SHIFT)

extern void __this_fixmap_does_not_exist(void);

static inline unsigned long fix_to_virt(const unsigned int idx)
{
	if (idx >= FIX_KMAP_END)
		__this_fixmap_does_not_exist();
	return __fix_to_virt(idx);
}

static inline unsigned int virt_to_fix(const unsigned long vaddr)
{
	BUG_ON(vaddr >= FIXADDR_TOP || vaddr < FIXADDR_START);
	return __virt_to_fix(vaddr);
}

#endif
