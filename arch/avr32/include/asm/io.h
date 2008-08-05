#ifndef __ASM_AVR32_IO_H
#define __ASM_AVR32_IO_H

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/byteorder.h>

#include <mach/io.h>

/* virt_to_phys will only work when address is in P1 or P2 */
static __inline__ unsigned long virt_to_phys(volatile void *address)
{
	return PHYSADDR(address);
}

static __inline__ void * phys_to_virt(unsigned long address)
{
	return (void *)P1SEGADDR(address);
}

#define cached_to_phys(addr)	((unsigned long)PHYSADDR(addr))
#define uncached_to_phys(addr)	((unsigned long)PHYSADDR(addr))
#define phys_to_cached(addr)	((void *)P1SEGADDR(addr))
#define phys_to_uncached(addr)	((void *)P2SEGADDR(addr))

/*
 * Generic IO read/write.  These perform native-endian accesses.  Note
 * that some architectures will want to re-define __raw_{read,write}w.
 */
extern void __raw_writesb(void __iomem *addr, const void *data, int bytelen);
extern void __raw_writesw(void __iomem *addr, const void *data, int wordlen);
extern void __raw_writesl(void __iomem *addr, const void *data, int longlen);

extern void __raw_readsb(const void __iomem *addr, void *data, int bytelen);
extern void __raw_readsw(const void __iomem *addr, void *data, int wordlen);
extern void __raw_readsl(const void __iomem *addr, void *data, int longlen);

static inline void __raw_writeb(u8 v, volatile void __iomem *addr)
{
	*(volatile u8 __force *)addr = v;
}
static inline void __raw_writew(u16 v, volatile void __iomem *addr)
{
	*(volatile u16 __force *)addr = v;
}
static inline void __raw_writel(u32 v, volatile void __iomem *addr)
{
	*(volatile u32 __force *)addr = v;
}

static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	return *(const volatile u8 __force *)addr;
}
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	return *(const volatile u16 __force *)addr;
}
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	return *(const volatile u32 __force *)addr;
}

/* Convert I/O port address to virtual address */
#ifndef __io
# define __io(p)	((void *)phys_to_uncached(p))
#endif

/*
 * Not really sure about the best way to slow down I/O on
 * AVR32. Defining it as a no-op until we have an actual test case.
 */
#define SLOW_DOWN_IO	do { } while (0)

#define __BUILD_MEMORY_SINGLE(pfx, bwl, type)				\
static inline void							\
pfx##write##bwl(type val, volatile void __iomem *addr)			\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	__addr = (void *)__swizzle_addr_##bwl((unsigned long)(addr));	\
	__val = pfx##ioswab##bwl(__addr, val);				\
									\
	BUILD_BUG_ON(sizeof(type) > sizeof(unsigned long));		\
									\
	*__addr = __val;						\
}									\
									\
static inline type pfx##read##bwl(const volatile void __iomem *addr)	\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	__addr = (void *)__swizzle_addr_##bwl((unsigned long)(addr));	\
									\
	BUILD_BUG_ON(sizeof(type) > sizeof(unsigned long));		\
									\
	__val = *__addr;						\
	return pfx##ioswab##bwl(__addr, __val);				\
}

#define __BUILD_IOPORT_SINGLE(pfx, bwl, type, p, slow)			\
static inline void pfx##out##bwl##p(type val, unsigned long port)	\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	__addr = __io(__swizzle_addr_##bwl(port));			\
	__val = pfx##ioswab##bwl(__addr, val);				\
									\
	BUILD_BUG_ON(sizeof(type) > sizeof(unsigned long));		\
									\
	*__addr = __val;						\
	slow;								\
}									\
									\
static inline type pfx##in##bwl##p(unsigned long port)			\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	__addr = __io(__swizzle_addr_##bwl(port));			\
									\
	BUILD_BUG_ON(sizeof(type) > sizeof(unsigned long));		\
									\
	__val = *__addr;						\
	slow;								\
									\
	return pfx##ioswab##bwl(__addr, __val);				\
}

#define __BUILD_MEMORY_PFX(bus, bwl, type)				\
	__BUILD_MEMORY_SINGLE(bus, bwl, type)

#define BUILDIO_MEM(bwl, type)						\
	__BUILD_MEMORY_PFX(, bwl, type)					\
	__BUILD_MEMORY_PFX(__mem_, bwl, type)

#define __BUILD_IOPORT_PFX(bus, bwl, type)				\
	__BUILD_IOPORT_SINGLE(bus, bwl, type, ,)			\
	__BUILD_IOPORT_SINGLE(bus, bwl, type, _p, SLOW_DOWN_IO)

#define BUILDIO_IOPORT(bwl, type)					\
	__BUILD_IOPORT_PFX(, bwl, type)					\
	__BUILD_IOPORT_PFX(__mem_, bwl, type)

BUILDIO_MEM(b, u8)
BUILDIO_MEM(w, u16)
BUILDIO_MEM(l, u32)

BUILDIO_IOPORT(b, u8)
BUILDIO_IOPORT(w, u16)
BUILDIO_IOPORT(l, u32)

#define readb_relaxed			readb
#define readw_relaxed			readw
#define readl_relaxed			readl

#define __BUILD_MEMORY_STRING(bwl, type)				\
static inline void writes##bwl(volatile void __iomem *addr,		\
			       const void *data, unsigned int count)	\
{									\
	const type *__data = data;					\
									\
	while (count--)							\
		__mem_write##bwl(*__data++, addr);			\
}									\
									\
static inline void reads##bwl(const volatile void __iomem *addr,	\
			      void *data, unsigned int count)		\
{									\
	type *__data = data;						\
									\
	while (count--)							\
		*__data++ = __mem_read##bwl(addr);			\
}

#define __BUILD_IOPORT_STRING(bwl, type)				\
static inline void outs##bwl(unsigned long port, const void *data,	\
			     unsigned int count)			\
{									\
	const type *__data = data;					\
									\
	while (count--)							\
		__mem_out##bwl(*__data++, port);			\
}									\
									\
static inline void ins##bwl(unsigned long port, void *data,		\
			   unsigned int count)				\
{									\
	type *__data = data;						\
									\
	while (count--)							\
		*__data++ = __mem_in##bwl(port);			\
}

#define BUILDSTRING(bwl, type)						\
	__BUILD_MEMORY_STRING(bwl, type)				\
	__BUILD_IOPORT_STRING(bwl, type)

BUILDSTRING(b, u8)
BUILDSTRING(w, u16)
BUILDSTRING(l, u32)

/*
 * io{read,write}{8,16,32} macros in both le (for PCI style consumers) and native be
 */
#ifndef ioread8

#define ioread8(p)		((unsigned int)readb(p))

#define ioread16(p)		((unsigned int)readw(p))
#define ioread16be(p)		((unsigned int)__raw_readw(p))

#define ioread32(p)		((unsigned int)readl(p))
#define ioread32be(p)		((unsigned int)__raw_readl(p))

#define iowrite8(v,p)		writeb(v, p)

#define iowrite16(v,p)		writew(v, p)
#define iowrite16be(v,p)	__raw_writew(v, p)

#define iowrite32(v,p)		writel(v, p)
#define iowrite32be(v,p)	__raw_writel(v, p)

#define ioread8_rep(p,d,c)	readsb(p,d,c)
#define ioread16_rep(p,d,c)	readsw(p,d,c)
#define ioread32_rep(p,d,c)	readsl(p,d,c)

#define iowrite8_rep(p,s,c)	writesb(p,s,c)
#define iowrite16_rep(p,s,c)	writesw(p,s,c)
#define iowrite32_rep(p,s,c)	writesl(p,s,c)

#endif

static inline void memcpy_fromio(void * to, const volatile void __iomem *from,
				 unsigned long count)
{
	memcpy(to, (const void __force *)from, count);
}

static inline void  memcpy_toio(volatile void __iomem *to, const void * from,
				unsigned long count)
{
	memcpy((void __force *)to, from, count);
}

static inline void memset_io(volatile void __iomem *addr, unsigned char val,
			     unsigned long count)
{
	memset((void __force *)addr, val, count);
}

#define mmiowb()

#define IO_SPACE_LIMIT	0xffffffff

extern void __iomem *__ioremap(unsigned long offset, size_t size,
			       unsigned long flags);
extern void __iounmap(void __iomem *addr);

/*
 * ioremap	-   map bus memory into CPU space
 * @offset	bus address of the memory
 * @size	size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to make
 * bus memory CPU accessible via the readb/.../writel functions and
 * the other mmio helpers. The returned address is not guaranteed to
 * be usable directly as a virtual address.
 */
#define ioremap(offset, size)			\
	__ioremap((offset), (size), 0)

#define ioremap_nocache(offset, size)		\
	__ioremap((offset), (size), 0)

#define iounmap(addr)				\
	__iounmap(addr)

#define cached(addr) P1SEGADDR(addr)
#define uncached(addr) P2SEGADDR(addr)

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt
#define page_to_bus page_to_phys
#define bus_to_page phys_to_page

/*
 * Create a virtual mapping cookie for an IO port range.  There exists
 * no such thing as port-based I/O on AVR32, so a regular ioremap()
 * should do what we need.
 */
#define ioport_map(port, nr)	ioremap(port, nr)
#define ioport_unmap(port)	iounmap(port)

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)    __va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)   p

#endif /* __ASM_AVR32_IO_H */
