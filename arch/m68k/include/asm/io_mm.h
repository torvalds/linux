/*
 * linux/include/asm-m68k/io.h
 *
 * 4/1/00 RZ: - rewritten to avoid clashes between ISA/PCI and other
 *              IO access
 *            - added Q40 support
 *            - added skeleton for GG-II and Amiga PCMCIA
 * 2/3/01 RZ: - moved a few more defs into raw_io.h
 *
 * inX/outX should not be used by any driver unless it does
 * ISA access. Other drivers should use function defined in raw_io.h
 * or define its own macros on top of these.
 *
 *    inX(),outX()              are for ISA I/O
 *    isa_readX(),isa_writeX()  are for ISA memory
 */

#ifndef _IO_H
#define _IO_H

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <asm/raw_io.h>
#include <asm/virtconvert.h>

#include <asm-generic/iomap.h>

#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#endif


/*
 * IO/MEM definitions for various ISA bridges
 */


#ifdef CONFIG_Q40

#define q40_isa_io_base  0xff400000
#define q40_isa_mem_base 0xff800000

#define Q40_ISA_IO_B(ioaddr) (q40_isa_io_base+1+4*((unsigned long)(ioaddr)))
#define Q40_ISA_IO_W(ioaddr) (q40_isa_io_base+  4*((unsigned long)(ioaddr)))
#define Q40_ISA_MEM_B(madr)  (q40_isa_mem_base+1+4*((unsigned long)(madr)))
#define Q40_ISA_MEM_W(madr)  (q40_isa_mem_base+  4*((unsigned long)(madr)))

#define MULTI_ISA 0
#endif /* Q40 */

#ifdef CONFIG_AMIGA_PCMCIA
#include <asm/amigayle.h>

#define AG_ISA_IO_B(ioaddr) ( GAYLE_IO+(ioaddr)+(((ioaddr)&1)*GAYLE_ODD) )
#define AG_ISA_IO_W(ioaddr) ( GAYLE_IO+(ioaddr) )

#ifndef MULTI_ISA
#define MULTI_ISA 0
#else
#undef MULTI_ISA
#define MULTI_ISA 1
#endif
#endif /* AMIGA_PCMCIA */



#if defined(CONFIG_PCI) && defined(CONFIG_COLDFIRE)

#define HAVE_ARCH_PIO_SIZE
#define PIO_OFFSET	0
#define PIO_MASK	0xffff
#define PIO_RESERVED	0x10000

u8 mcf_pci_inb(u32 addr);
u16 mcf_pci_inw(u32 addr);
u32 mcf_pci_inl(u32 addr);
void mcf_pci_insb(u32 addr, u8 *buf, u32 len);
void mcf_pci_insw(u32 addr, u16 *buf, u32 len);
void mcf_pci_insl(u32 addr, u32 *buf, u32 len);

void mcf_pci_outb(u8 v, u32 addr);
void mcf_pci_outw(u16 v, u32 addr);
void mcf_pci_outl(u32 v, u32 addr);
void mcf_pci_outsb(u32 addr, const u8 *buf, u32 len);
void mcf_pci_outsw(u32 addr, const u16 *buf, u32 len);
void mcf_pci_outsl(u32 addr, const u32 *buf, u32 len);

#define	inb	mcf_pci_inb
#define	inb_p	mcf_pci_inb
#define	inw	mcf_pci_inw
#define	inw_p	mcf_pci_inw
#define	inl	mcf_pci_inl
#define	inl_p	mcf_pci_inl
#define	insb	mcf_pci_insb
#define	insw	mcf_pci_insw
#define	insl	mcf_pci_insl

#define	outb	mcf_pci_outb
#define	outb_p	mcf_pci_outb
#define	outw	mcf_pci_outw
#define	outw_p	mcf_pci_outw
#define	outl	mcf_pci_outl
#define	outl_p	mcf_pci_outl
#define	outsb	mcf_pci_outsb
#define	outsw	mcf_pci_outsw
#define	outsl	mcf_pci_outsl

#define readb(addr)	in_8(addr)
#define writeb(v, addr)	out_8((addr), (v))
#define readw(addr)	in_le16(addr)
#define writew(v, addr)	out_le16((addr), (v))

#elif defined(CONFIG_ISA)

#if MULTI_ISA == 0
#undef MULTI_ISA
#endif

#define ISA_TYPE_Q40 (1)
#define ISA_TYPE_AG  (2)

#if defined(CONFIG_Q40) && !defined(MULTI_ISA)
#define ISA_TYPE ISA_TYPE_Q40
#define ISA_SEX  0
#endif
#if defined(CONFIG_AMIGA_PCMCIA) && !defined(MULTI_ISA)
#define ISA_TYPE ISA_TYPE_AG
#define ISA_SEX  1
#endif

#ifdef MULTI_ISA
extern int isa_type;
extern int isa_sex;

#define ISA_TYPE isa_type
#define ISA_SEX  isa_sex
#endif

/*
 * define inline addr translation functions. Normally only one variant will
 * be compiled in so the case statement will be optimised away
 */

static inline u8 __iomem *isa_itb(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case ISA_TYPE_Q40: return (u8 __iomem *)Q40_ISA_IO_B(addr);
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case ISA_TYPE_AG: return (u8 __iomem *)AG_ISA_IO_B(addr);
#endif
    default: return NULL; /* avoid warnings, just in case */
    }
}
static inline u16 __iomem *isa_itw(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case ISA_TYPE_Q40: return (u16 __iomem *)Q40_ISA_IO_W(addr);
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case ISA_TYPE_AG: return (u16 __iomem *)AG_ISA_IO_W(addr);
#endif
    default: return NULL; /* avoid warnings, just in case */
    }
}
static inline u32 __iomem *isa_itl(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_AMIGA_PCMCIA
    case ISA_TYPE_AG: return (u32 __iomem *)AG_ISA_IO_W(addr);
#endif
    default: return 0; /* avoid warnings, just in case */
    }
}
static inline u8 __iomem *isa_mtb(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case ISA_TYPE_Q40: return (u8 __iomem *)Q40_ISA_MEM_B(addr);
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case ISA_TYPE_AG: return (u8 __iomem *)addr;
#endif
    default: return NULL; /* avoid warnings, just in case */
    }
}
static inline u16 __iomem *isa_mtw(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case ISA_TYPE_Q40: return (u16 __iomem *)Q40_ISA_MEM_W(addr);
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case ISA_TYPE_AG: return (u16 __iomem *)addr;
#endif
    default: return NULL; /* avoid warnings, just in case */
    }
}


#define isa_inb(port)      in_8(isa_itb(port))
#define isa_inw(port)      (ISA_SEX ? in_be16(isa_itw(port)) : in_le16(isa_itw(port)))
#define isa_inl(port)      (ISA_SEX ? in_be32(isa_itl(port)) : in_le32(isa_itl(port)))
#define isa_outb(val,port) out_8(isa_itb(port),(val))
#define isa_outw(val,port) (ISA_SEX ? out_be16(isa_itw(port),(val)) : out_le16(isa_itw(port),(val)))
#define isa_outl(val,port) (ISA_SEX ? out_be32(isa_itl(port),(val)) : out_le32(isa_itl(port),(val)))

#define isa_readb(p)       in_8(isa_mtb((unsigned long)(p)))
#define isa_readw(p)       \
	(ISA_SEX ? in_be16(isa_mtw((unsigned long)(p)))	\
		 : in_le16(isa_mtw((unsigned long)(p))))
#define isa_writeb(val,p)  out_8(isa_mtb((unsigned long)(p)),(val))
#define isa_writew(val,p)  \
	(ISA_SEX ? out_be16(isa_mtw((unsigned long)(p)),(val))	\
		 : out_le16(isa_mtw((unsigned long)(p)),(val)))

static inline void isa_delay(void)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case ISA_TYPE_Q40: isa_outb(0,0x80); break;
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case ISA_TYPE_AG: break;
#endif
    default: break; /* avoid warnings */
    }
}

#define isa_inb_p(p)      ({u8 v=isa_inb(p);isa_delay();v;})
#define isa_outb_p(v,p)   ({isa_outb((v),(p));isa_delay();})
#define isa_inw_p(p)      ({u16 v=isa_inw(p);isa_delay();v;})
#define isa_outw_p(v,p)   ({isa_outw((v),(p));isa_delay();})
#define isa_inl_p(p)      ({u32 v=isa_inl(p);isa_delay();v;})
#define isa_outl_p(v,p)   ({isa_outl((v),(p));isa_delay();})

#define isa_insb(port, buf, nr) raw_insb(isa_itb(port), (u8 *)(buf), (nr))
#define isa_outsb(port, buf, nr) raw_outsb(isa_itb(port), (u8 *)(buf), (nr))

#define isa_insw(port, buf, nr)     \
       (ISA_SEX ? raw_insw(isa_itw(port), (u16 *)(buf), (nr)) :    \
                  raw_insw_swapw(isa_itw(port), (u16 *)(buf), (nr)))

#define isa_outsw(port, buf, nr)    \
       (ISA_SEX ? raw_outsw(isa_itw(port), (u16 *)(buf), (nr)) :  \
                  raw_outsw_swapw(isa_itw(port), (u16 *)(buf), (nr)))

#define isa_insl(port, buf, nr)     \
       (ISA_SEX ? raw_insl(isa_itl(port), (u32 *)(buf), (nr)) :    \
                  raw_insw_swapw(isa_itw(port), (u16 *)(buf), (nr)<<1))

#define isa_outsl(port, buf, nr)    \
       (ISA_SEX ? raw_outsl(isa_itl(port), (u32 *)(buf), (nr)) :  \
                  raw_outsw_swapw(isa_itw(port), (u16 *)(buf), (nr)<<1))


#define inb     isa_inb
#define inb_p   isa_inb_p
#define outb    isa_outb
#define outb_p  isa_outb_p
#define inw     isa_inw
#define inw_p   isa_inw_p
#define outw    isa_outw
#define outw_p  isa_outw_p
#define inl     isa_inl
#define inl_p   isa_inl_p
#define outl    isa_outl
#define outl_p  isa_outl_p
#define insb    isa_insb
#define insw    isa_insw
#define insl    isa_insl
#define outsb   isa_outsb
#define outsw   isa_outsw
#define outsl   isa_outsl
#define readb   isa_readb
#define readw   isa_readw
#define writeb  isa_writeb
#define writew  isa_writew

#else  /* CONFIG_ISA */

/*
 * We need to define dummy functions for GENERIC_IOMAP support.
 */
#define inb(port)          0xff
#define inb_p(port)        0xff
#define outb(val,port)     ((void)0)
#define outb_p(val,port)   ((void)0)
#define inw(port)          0xffff
#define inw_p(port)        0xffff
#define outw(val,port)     ((void)0)
#define outw_p(val,port)   ((void)0)
#define inl(port)          0xffffffffUL
#define inl_p(port)        0xffffffffUL
#define outl(val,port)     ((void)0)
#define outl_p(val,port)   ((void)0)

#define insb(port,buf,nr)  ((void)0)
#define outsb(port,buf,nr) ((void)0)
#define insw(port,buf,nr)  ((void)0)
#define outsw(port,buf,nr) ((void)0)
#define insl(port,buf,nr)  ((void)0)
#define outsl(port,buf,nr) ((void)0)

/*
 * These should be valid on any ioremap()ed region
 */
#define readb(addr)      in_8(addr)
#define writeb(val,addr) out_8((addr),(val))
#define readw(addr)      in_le16(addr)
#define writew(val,addr) out_le16((addr),(val))

#endif /* CONFIG_ISA */

#define readl(addr)      in_le32(addr)
#define writel(val,addr) out_le32((addr),(val))

#define readsb(port, buf, nr)     raw_insb((port), (u8 *)(buf), (nr))
#define readsw(port, buf, nr)     raw_insw((port), (u16 *)(buf), (nr))
#define readsl(port, buf, nr)     raw_insl((port), (u32 *)(buf), (nr))
#define writesb(port, buf, nr)    raw_outsb((port), (u8 *)(buf), (nr))
#define writesw(port, buf, nr)    raw_outsw((port), (u16 *)(buf), (nr))
#define writesl(port, buf, nr)    raw_outsl((port), (u32 *)(buf), (nr))

#define mmiowb()

static inline void __iomem *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
static inline void __iomem *ioremap_nocache(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
static inline void __iomem *ioremap_writethrough(unsigned long physaddr,
					 unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_WRITETHROUGH);
}
static inline void __iomem *ioremap_fullcache(unsigned long physaddr,
				      unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_FULL_CACHING);
}

static inline void memset_io(volatile void __iomem *addr, unsigned char val, int count)
{
	__builtin_memset((void __force *) addr, val, count);
}
static inline void memcpy_fromio(void *dst, const volatile void __iomem *src, int count)
{
	__builtin_memcpy(dst, (void __force *) src, count);
}
static inline void memcpy_toio(volatile void __iomem *dst, const void *src, int count)
{
	__builtin_memcpy((void __force *) dst, src, count);
}

#ifndef CONFIG_SUN3
#define IO_SPACE_LIMIT 0xffff
#else
#define IO_SPACE_LIMIT 0x0fffffff
#endif

#endif /* __KERNEL__ */

#define __ARCH_HAS_NO_PAGE_ZERO_MAPPED		1

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#define ioport_map(port, nr)	((void __iomem *)(port))

#endif /* _IO_H */
