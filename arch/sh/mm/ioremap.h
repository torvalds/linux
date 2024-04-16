#ifndef _SH_MM_IORMEMAP_H
#define _SH_MM_IORMEMAP_H 1

#ifdef CONFIG_IOREMAP_FIXED
void __iomem *ioremap_fixed(phys_addr_t, unsigned long, pgprot_t);
int iounmap_fixed(void __iomem *);
void ioremap_fixed_init(void);
#else
static inline void __iomem *
ioremap_fixed(phys_addr_t phys_addr, unsigned long size, pgprot_t prot)
{
	BUG();
	return NULL;
}
static inline void ioremap_fixed_init(void)
{
}
static inline int iounmap_fixed(void __iomem *addr)
{
	return -EINVAL;
}
#endif /* CONFIG_IOREMAP_FIXED */
#endif /* _SH_MM_IORMEMAP_H */
