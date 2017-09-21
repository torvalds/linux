#ifndef _ASM_POWERPC_PGALLOC_H
#define _ASM_POWERPC_PGALLOC_H

#include <linux/mm.h>

#ifndef MODULE
static inline gfp_t pgtable_gfp_flags(struct mm_struct *mm, gfp_t gfp)
{
	if (unlikely(mm == &init_mm))
		return gfp;
	return gfp | __GFP_ACCOUNT;
}
#else /* !MODULE */
static inline gfp_t pgtable_gfp_flags(struct mm_struct *mm, gfp_t gfp)
{
	return gfp | __GFP_ACCOUNT;
}
#endif /* MODULE */

#define PGALLOC_GFP (GFP_KERNEL | __GFP_NOTRACK | __GFP_ZERO)

#ifdef CONFIG_PPC_BOOK3S
#include <asm/book3s/pgalloc.h>
#else
#include <asm/nohash/pgalloc.h>
#endif

#endif /* _ASM_POWERPC_PGALLOC_H */
