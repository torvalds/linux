#ifndef __ALPHA_POLARIS__H__
#define __ALPHA_POLARIS__H__

#include <linux/types.h>
#include <asm/compiler.h>

/*
 * POLARIS is the internal name for a core logic chipset which provides
 * memory controller and PCI access for the 21164PC chip based systems.
 *
 * This file is based on:
 *
 * Polaris System Controller
 * Device Functional Specification
 * 22-Jan-98
 * Rev. 4.2
 *
 */

/* Polaris memory regions */
#define POLARIS_SPARSE_MEM_BASE		(IDENT_ADDR + 0xf800000000UL)
#define POLARIS_DENSE_MEM_BASE		(IDENT_ADDR + 0xf900000000UL)
#define POLARIS_SPARSE_IO_BASE		(IDENT_ADDR + 0xf980000000UL)
#define POLARIS_SPARSE_CONFIG_BASE	(IDENT_ADDR + 0xf9c0000000UL)
#define POLARIS_IACK_BASE		(IDENT_ADDR + 0xf9f8000000UL)
#define POLARIS_DENSE_IO_BASE		(IDENT_ADDR + 0xf9fc000000UL)
#define POLARIS_DENSE_CONFIG_BASE	(IDENT_ADDR + 0xf9fe000000UL)

#define POLARIS_IACK_SC			POLARIS_IACK_BASE

/* The Polaris command/status registers live in PCI Config space for
 * bus 0/device 0.  As such, they may be bytes, words, or doublewords.
 */
#define POLARIS_W_VENID		(POLARIS_DENSE_CONFIG_BASE)
#define POLARIS_W_DEVID		(POLARIS_DENSE_CONFIG_BASE+2)
#define POLARIS_W_CMD		(POLARIS_DENSE_CONFIG_BASE+4)
#define POLARIS_W_STATUS	(POLARIS_DENSE_CONFIG_BASE+6)

/*
 * Data structure for handling POLARIS machine checks:
 */
struct el_POLARIS_sysdata_mcheck {
    u_long      psc_status;
    u_long	psc_pcictl0;
    u_long	psc_pcictl1;
    u_long	psc_pcictl2;
};

#ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

/*
 * I/O functions:
 *
 * POLARIS, the PCI/memory support chipset for the PCA56 (21164PC)
 * processors, can use either a sparse address  mapping scheme, or the 
 * so-called byte-word PCI address space, to get at PCI memory and I/O.
 *
 * However, we will support only the BWX form.
 */

/*
 * Memory functions.  Polaris allows all accesses (byte/word
 * as well as long/quad) to be done through dense space.
 *
 * We will only support DENSE access via BWX insns.
 */

__EXTERN_INLINE void __iomem *polaris_ioportmap(unsigned long addr)
{
	return (void __iomem *)(addr + POLARIS_DENSE_IO_BASE);
}

__EXTERN_INLINE void __iomem *polaris_ioremap(unsigned long addr,
					      unsigned long size)
{
	return (void __iomem *)(addr + POLARIS_DENSE_MEM_BASE);
}

__EXTERN_INLINE int polaris_is_ioaddr(unsigned long addr)
{
	return addr >= POLARIS_SPARSE_MEM_BASE;
}

__EXTERN_INLINE int polaris_is_mmio(const volatile void __iomem *addr)
{
	return (unsigned long)addr < POLARIS_SPARSE_IO_BASE;
}

#undef __IO_PREFIX
#define __IO_PREFIX		polaris
#define polaris_trivial_rw_bw	1
#define polaris_trivial_rw_lq	1
#define polaris_trivial_io_bw	1
#define polaris_trivial_io_lq	1
#define polaris_trivial_iounmap	1
#include <asm/io_trivial.h>

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_POLARIS__H__ */
