#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#define FIXADDR_START		0xffc00000UL
#define FIXADDR_TOP		0xffe00000UL
#define FIXADDR_SIZE		(FIXADDR_TOP - FIXADDR_START)

#define FIX_KMAP_NR_PTES	(FIXADDR_SIZE >> PAGE_SHIFT)

#define __fix_to_virt(x)	(FIXADDR_START + ((x) << PAGE_SHIFT))
#define __virt_to_fix(x)	(((x) - FIXADDR_START) >> PAGE_SHIFT)

extern void __this_fixmap_does_not_exist(void);

static inline unsigned long fix_to_virt(const unsigned int idx)
{
	if (idx >= FIX_KMAP_NR_PTES)
		__this_fixmap_does_not_exist();
	return __fix_to_virt(idx);
}

static inline unsigned int virt_to_fix(const unsigned long vaddr)
{
	BUG_ON(vaddr >= FIXADDR_TOP || vaddr < FIXADDR_START);
	return __virt_to_fix(vaddr);
}

#endif
