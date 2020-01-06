/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KMAP_H
#define _KMAP_H

#ifdef CONFIG_MMU

#define ARCH_HAS_IOREMAP_WT

/* Values for nocacheflag and cmode */
#define IOMAP_FULL_CACHING		0
#define IOMAP_NOCACHE_SER		1
#define IOMAP_NOCACHE_NONSER		2
#define IOMAP_WRITETHROUGH		3

/*
 * These functions exported by arch/m68k/mm/kmap.c.
 * Only needed on MMU enabled systems.
 */
extern void __iomem *__ioremap(unsigned long physaddr, unsigned long size,
			       int cacheflag);
#define iounmap iounmap
extern void iounmap(void __iomem *addr);

#define ioremap ioremap
static inline void __iomem *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}

#define ioremap_nocache ioremap
#define ioremap_uc ioremap
#define ioremap_wt ioremap_wt
static inline void __iomem *ioremap_wt(unsigned long physaddr,
				       unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_WRITETHROUGH);
}

#define memset_io memset_io
static inline void memset_io(volatile void __iomem *addr, unsigned char val,
			     int count)
{
	__builtin_memset((void __force *) addr, val, count);
}

#define memcpy_fromio memcpy_fromio
static inline void memcpy_fromio(void *dst, const volatile void __iomem *src,
				 int count)
{
	__builtin_memcpy(dst, (void __force *) src, count);
}

#define memcpy_toio memcpy_toio
static inline void memcpy_toio(volatile void __iomem *dst, const void *src,
			       int count)
{
	__builtin_memcpy((void __force *) dst, src, count);
}

#endif /* CONFIG_MMU */

#define ioport_map ioport_map
static inline void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	return (void __iomem *) port;
}

#define ioport_unmap ioport_unmap
static inline void ioport_unmap(void __iomem *p)
{
}

#endif /* _KMAP_H */
