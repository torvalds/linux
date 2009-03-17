#ifndef __ASM_SH_MMU_CONTEXT_32_H
#define __ASM_SH_MMU_CONTEXT_32_H

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	/* Do nothing */
}

#ifdef CONFIG_CPU_HAS_PTEAEX
static inline void set_asid(unsigned long asid)
{
	__raw_writel(asid, MMU_PTEAEX);
}

static inline unsigned long get_asid(void)
{
	return __raw_readl(MMU_PTEAEX) & MMU_CONTEXT_ASID_MASK;
}
#else
static inline void set_asid(unsigned long asid)
{
	unsigned long __dummy;

	__asm__ __volatile__ ("mov.l	%2, %0\n\t"
			      "and	%3, %0\n\t"
			      "or	%1, %0\n\t"
			      "mov.l	%0, %2"
			      : "=&r" (__dummy)
			      : "r" (asid), "m" (__m(MMU_PTEH)),
			        "r" (0xffffff00));
}

static inline unsigned long get_asid(void)
{
	unsigned long asid;

	__asm__ __volatile__ ("mov.l	%1, %0"
			      : "=r" (asid)
			      : "m" (__m(MMU_PTEH)));
	asid &= MMU_CONTEXT_ASID_MASK;
	return asid;
}
#endif /* CONFIG_CPU_HAS_PTEAEX */

/* MMU_TTB is used for optimizing the fault handling. */
static inline void set_TTB(pgd_t *pgd)
{
	ctrl_outl((unsigned long)pgd, MMU_TTB);
}

static inline pgd_t *get_TTB(void)
{
	return (pgd_t *)ctrl_inl(MMU_TTB);
}
#endif /* __ASM_SH_MMU_CONTEXT_32_H */
