/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _M68K_IO_MM_H
#define _M68K_IO_MM_H

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <asm/raw_io.h>
#include <asm/virtconvert.h>
#include <asm/kmap.h>

#ifdef CONFIG_ATARI
#define atari_readb   raw_inb
#define atari_writeb  raw_outb

#define atari_inb_p   raw_inb
#define atari_outb_p  raw_outb
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

#ifdef CONFIG_ATARI_ROM_ISA

#define enec_isa_read_base  0xfffa0000
#define enec_isa_write_base 0xfffb0000

#define ENEC_ISA_IO_B(ioaddr)	(enec_isa_read_base+((((unsigned long)(ioaddr))&0x7F)<<9))
#define ENEC_ISA_IO_W(ioaddr)	(enec_isa_read_base+((((unsigned long)(ioaddr))&0x7F)<<9))
#define ENEC_ISA_MEM_B(madr)	(enec_isa_read_base+((((unsigned long)(madr))&0x7F)<<9))
#define ENEC_ISA_MEM_W(madr)	(enec_isa_read_base+((((unsigned long)(madr))&0x7F)<<9))

#ifndef MULTI_ISA
#define MULTI_ISA 0
#else
#undef MULTI_ISA
#define MULTI_ISA 1
#endif
#endif /* ATARI_ROM_ISA */


#if defined(CONFIG_ISA) || defined(CONFIG_ATARI_ROM_ISA)

#if MULTI_ISA == 0
#undef MULTI_ISA
#endif

#define ISA_TYPE_Q40  (1)
#define ISA_TYPE_AG   (2)
#define ISA_TYPE_ENEC (3)

#if defined(CONFIG_Q40) && !defined(MULTI_ISA)
#define ISA_TYPE ISA_TYPE_Q40
#define ISA_SEX  0
#endif
#if defined(CONFIG_AMIGA_PCMCIA) && !defined(MULTI_ISA)
#define ISA_TYPE ISA_TYPE_AG
#define ISA_SEX  1
#endif
#if defined(CONFIG_ATARI_ROM_ISA) && !defined(MULTI_ISA)
#define ISA_TYPE ISA_TYPE_ENEC
#define ISA_SEX  0
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
#ifdef CONFIG_ATARI_ROM_ISA
    case ISA_TYPE_ENEC: return (u8 __iomem *)ENEC_ISA_IO_B(addr);
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
#ifdef CONFIG_ATARI_ROM_ISA
    case ISA_TYPE_ENEC: return (u16 __iomem *)ENEC_ISA_IO_W(addr);
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
#ifdef CONFIG_ATARI_ROM_ISA
    case ISA_TYPE_ENEC: return (u8 __iomem *)ENEC_ISA_MEM_B(addr);
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
#ifdef CONFIG_ATARI_ROM_ISA
    case ISA_TYPE_ENEC: return (u16 __iomem *)ENEC_ISA_MEM_W(addr);
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

#ifdef CONFIG_ATARI_ROM_ISA
#define isa_rom_inb(port)      rom_in_8(isa_itb(port))
#define isa_rom_inw(port)	\
	(ISA_SEX ? rom_in_be16(isa_itw(port))	\
		 : rom_in_le16(isa_itw(port)))

#define isa_rom_outb(val, port) rom_out_8(isa_itb(port), (val))
#define isa_rom_outw(val, port)	\
	(ISA_SEX ? rom_out_be16(isa_itw(port), (val))	\
		 : rom_out_le16(isa_itw(port), (val)))

#define isa_rom_readb(p)       rom_in_8(isa_mtb((unsigned long)(p)))
#define isa_rom_readw(p)       \
	(ISA_SEX ? rom_in_be16(isa_mtw((unsigned long)(p)))	\
		 : rom_in_le16(isa_mtw((unsigned long)(p))))
#define isa_rom_readw_swap(p)       \
	(ISA_SEX ? rom_in_le16(isa_mtw((unsigned long)(p)))	\
		 : rom_in_be16(isa_mtw((unsigned long)(p))))
#define isa_rom_readw_raw(p)   rom_in_be16(isa_mtw((unsigned long)(p)))

#define isa_rom_writeb(val, p)  rom_out_8(isa_mtb((unsigned long)(p)), (val))
#define isa_rom_writew(val, p)  \
	(ISA_SEX ? rom_out_be16(isa_mtw((unsigned long)(p)), (val))	\
		 : rom_out_le16(isa_mtw((unsigned long)(p)), (val)))
#define isa_rom_writew_swap(val, p)  \
	(ISA_SEX ? rom_out_le16(isa_mtw((unsigned long)(p)), (val))	\
		 : rom_out_be16(isa_mtw((unsigned long)(p)), (val)))
#define isa_rom_writew_raw(val, p)  rom_out_be16(isa_mtw((unsigned long)(p)), (val))
#endif /* CONFIG_ATARI_ROM_ISA */

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
#ifdef CONFIG_ATARI_ROM_ISA
    case ISA_TYPE_ENEC: break;
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


#ifdef CONFIG_ATARI_ROM_ISA
#define isa_rom_inb_p(p)	({ u8 _v = isa_rom_inb(p); isa_delay(); _v; })
#define isa_rom_inw_p(p)	({ u16 _v = isa_rom_inw(p); isa_delay(); _v; })
#define isa_rom_outb_p(v, p)	({ isa_rom_outb((v), (p)); isa_delay(); })
#define isa_rom_outw_p(v, p)	({ isa_rom_outw((v), (p)); isa_delay(); })

#define isa_rom_insb(port, buf, nr) raw_rom_insb(isa_itb(port), (u8 *)(buf), (nr))

#define isa_rom_insw(port, buf, nr)     \
       (ISA_SEX ? raw_rom_insw(isa_itw(port), (u16 *)(buf), (nr)) :    \
		  raw_rom_insw_swapw(isa_itw(port), (u16 *)(buf), (nr)))

#define isa_rom_outsb(port, buf, nr) raw_rom_outsb(isa_itb(port), (u8 *)(buf), (nr))

#define isa_rom_outsw(port, buf, nr)    \
       (ISA_SEX ? raw_rom_outsw(isa_itw(port), (u16 *)(buf), (nr)) :  \
		  raw_rom_outsw_swapw(isa_itw(port), (u16 *)(buf), (nr)))
#endif /* CONFIG_ATARI_ROM_ISA */

#endif  /* CONFIG_ISA || CONFIG_ATARI_ROM_ISA */


#if defined(CONFIG_ISA) && !defined(CONFIG_ATARI_ROM_ISA)
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
#endif  /* CONFIG_ISA && !CONFIG_ATARI_ROM_ISA */

#ifdef CONFIG_ATARI_ROM_ISA
/*
 * kernel with both ROM port ISA and IDE compiled in, those have
 * conflicting defs for in/out. Simply consider port < 1024
 * ROM port ISA and everything else regular ISA for IDE. read,write defined
 * below.
 */
#define inb(port)	((port) < 1024 ? isa_rom_inb(port) : in_8(port))
#define inb_p(port)	((port) < 1024 ? isa_rom_inb_p(port) : in_8(port))
#define inw(port)	((port) < 1024 ? isa_rom_inw(port) : in_le16(port))
#define inw_p(port)	((port) < 1024 ? isa_rom_inw_p(port) : in_le16(port))
#define inl		isa_inl
#define inl_p		isa_inl_p

#define outb(val, port)	((port) < 1024 ? isa_rom_outb((val), (port)) : out_8((port), (val)))
#define outb_p(val, port) ((port) < 1024 ? isa_rom_outb_p((val), (port)) : out_8((port), (val)))
#define outw(val, port)	((port) < 1024 ? isa_rom_outw((val), (port)) : out_le16((port), (val)))
#define outw_p(val, port) ((port) < 1024 ? isa_rom_outw_p((val), (port)) : out_le16((port), (val)))
#define outl		isa_outl
#define outl_p		isa_outl_p

#define insb(port, buf, nr)	((port) < 1024 ? isa_rom_insb((port), (buf), (nr)) : isa_insb((port), (buf), (nr)))
#define insw(port, buf, nr)	((port) < 1024 ? isa_rom_insw((port), (buf), (nr)) : isa_insw((port), (buf), (nr)))
#define insl			isa_insl
#define outsb(port, buf, nr)	((port) < 1024 ? isa_rom_outsb((port), (buf), (nr)) : isa_outsb((port), (buf), (nr)))
#define outsw(port, buf, nr)	((port) < 1024 ? isa_rom_outsw((port), (buf), (nr)) : isa_outsw((port), (buf), (nr)))
#define outsl			isa_outsl

#define readb(addr)		in_8(addr)
#define writeb(val, addr)	out_8((addr), (val))
#define readw(addr)		in_le16(addr)
#define writew(val, addr)	out_le16((addr), (val))
#endif /* CONFIG_ATARI_ROM_ISA */

#define readl(addr)      in_le32(addr)
#define writel(val,addr) out_le32((addr),(val))

#define readsb(port, buf, nr)     raw_insb((port), (u8 *)(buf), (nr))
#define readsw(port, buf, nr)     raw_insw((port), (u16 *)(buf), (nr))
#define readsl(port, buf, nr)     raw_insl((port), (u32 *)(buf), (nr))
#define writesb(port, buf, nr)    raw_outsb((port), (u8 *)(buf), (nr))
#define writesw(port, buf, nr)    raw_outsw((port), (u16 *)(buf), (nr))
#define writesl(port, buf, nr)    raw_outsl((port), (u32 *)(buf), (nr))

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

#define readb_relaxed(addr)	readb(addr)
#define readw_relaxed(addr)	readw(addr)
#define readl_relaxed(addr)	readl(addr)

#define writeb_relaxed(b, addr)	writeb(b, addr)
#define writew_relaxed(b, addr)	writew(b, addr)
#define writel_relaxed(b, addr)	writel(b, addr)

#endif /* _M68K_IO_MM_H */
