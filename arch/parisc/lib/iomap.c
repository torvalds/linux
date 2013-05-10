/*
 * iomap.c - Implement iomap interface for PA-RISC
 * Copyright (c) 2004 Matthew Wilcox
 */

#include <linux/ioport.h>
#include <linux/pci.h>
#include <asm/io.h>

/*
 * The iomap space on 32-bit PA-RISC is intended to look like this:
 * 00000000-7fffffff virtual mapped IO
 * 80000000-8fffffff ISA/EISA port space that can't be virtually mapped
 * 90000000-9fffffff Dino port space
 * a0000000-afffffff Astro port space
 * b0000000-bfffffff PAT port space
 * c0000000-cfffffff non-swapped memory IO
 * f0000000-ffffffff legacy IO memory pointers
 *
 * For the moment, here's what it looks like:
 * 80000000-8fffffff All ISA/EISA port space
 * f0000000-ffffffff legacy IO memory pointers
 *
 * On 64-bit, everything is extended, so:
 * 8000000000000000-8fffffffffffffff All ISA/EISA port space
 * f000000000000000-ffffffffffffffff legacy IO memory pointers
 */

/*
 * Technically, this should be 'if (VMALLOC_START < addr < VMALLOC_END),
 * but that's slow and we know it'll be within the first 2GB.
 */
#ifdef CONFIG_64BIT
#define INDIRECT_ADDR(addr)	(((unsigned long)(addr) & 1UL<<63) != 0)
#define ADDR_TO_REGION(addr)    (((unsigned long)addr >> 60) & 7)
#define IOPORT_MAP_BASE		(8UL << 60)
#else
#define INDIRECT_ADDR(addr)     (((unsigned long)(addr) & 1UL<<31) != 0)
#define ADDR_TO_REGION(addr)    (((unsigned long)addr >> 28) & 7)
#define IOPORT_MAP_BASE		(8UL << 28)
#endif

struct iomap_ops {
	unsigned int (*read8)(void __iomem *);
	unsigned int (*read16)(void __iomem *);
	unsigned int (*read16be)(void __iomem *);
	unsigned int (*read32)(void __iomem *);
	unsigned int (*read32be)(void __iomem *);
	void (*write8)(u8, void __iomem *);
	void (*write16)(u16, void __iomem *);
	void (*write16be)(u16, void __iomem *);
	void (*write32)(u32, void __iomem *);
	void (*write32be)(u32, void __iomem *);
	void (*read8r)(void __iomem *, void *, unsigned long);
	void (*read16r)(void __iomem *, void *, unsigned long);
	void (*read32r)(void __iomem *, void *, unsigned long);
	void (*write8r)(void __iomem *, const void *, unsigned long);
	void (*write16r)(void __iomem *, const void *, unsigned long);
	void (*write32r)(void __iomem *, const void *, unsigned long);
};

/* Generic ioport ops.  To be replaced later by specific dino/elroy/wax code */

#define ADDR2PORT(addr) ((unsigned long __force)(addr) & 0xffffff)

static unsigned int ioport_read8(void __iomem *addr)
{
	return inb(ADDR2PORT(addr));
}

static unsigned int ioport_read16(void __iomem *addr)
{
	return inw(ADDR2PORT(addr));
}

static unsigned int ioport_read32(void __iomem *addr)
{
	return inl(ADDR2PORT(addr));
}

static void ioport_write8(u8 datum, void __iomem *addr)
{
	outb(datum, ADDR2PORT(addr));
}

static void ioport_write16(u16 datum, void __iomem *addr)
{
	outw(datum, ADDR2PORT(addr));
}

static void ioport_write32(u32 datum, void __iomem *addr)
{
	outl(datum, ADDR2PORT(addr));
}

static void ioport_read8r(void __iomem *addr, void *dst, unsigned long count)
{
	insb(ADDR2PORT(addr), dst, count);
}

static void ioport_read16r(void __iomem *addr, void *dst, unsigned long count)
{
	insw(ADDR2PORT(addr), dst, count);
}

static void ioport_read32r(void __iomem *addr, void *dst, unsigned long count)
{
	insl(ADDR2PORT(addr), dst, count);
}

static void ioport_write8r(void __iomem *addr, const void *s, unsigned long n)
{
	outsb(ADDR2PORT(addr), s, n);
}

static void ioport_write16r(void __iomem *addr, const void *s, unsigned long n)
{
	outsw(ADDR2PORT(addr), s, n);
}

static void ioport_write32r(void __iomem *addr, const void *s, unsigned long n)
{
	outsl(ADDR2PORT(addr), s, n);
}

static const struct iomap_ops ioport_ops = {
	ioport_read8,
	ioport_read16,
	ioport_read16,
	ioport_read32,
	ioport_read32,
	ioport_write8,
	ioport_write16,
	ioport_write16,
	ioport_write32,
	ioport_write32,
	ioport_read8r,
	ioport_read16r,
	ioport_read32r,
	ioport_write8r,
	ioport_write16r,
	ioport_write32r,
};

/* Legacy I/O memory ops */

static unsigned int iomem_read8(void __iomem *addr)
{
	return readb(addr);
}

static unsigned int iomem_read16(void __iomem *addr)
{
	return readw(addr);
}

static unsigned int iomem_read16be(void __iomem *addr)
{
	return __raw_readw(addr);
}

static unsigned int iomem_read32(void __iomem *addr)
{
	return readl(addr);
}

static unsigned int iomem_read32be(void __iomem *addr)
{
	return __raw_readl(addr);
}

static void iomem_write8(u8 datum, void __iomem *addr)
{
	writeb(datum, addr);
}

static void iomem_write16(u16 datum, void __iomem *addr)
{
	writew(datum, addr);
}

static void iomem_write16be(u16 datum, void __iomem *addr)
{
	__raw_writew(datum, addr);
}

static void iomem_write32(u32 datum, void __iomem *addr)
{
	writel(datum, addr);
}

static void iomem_write32be(u32 datum, void __iomem *addr)
{
	__raw_writel(datum, addr);
}

static void iomem_read8r(void __iomem *addr, void *dst, unsigned long count)
{
	while (count--) {
		*(u8 *)dst = __raw_readb(addr);
		dst++;
	}
}

static void iomem_read16r(void __iomem *addr, void *dst, unsigned long count)
{
	while (count--) {
		*(u16 *)dst = __raw_readw(addr);
		dst += 2;
	}
}

static void iomem_read32r(void __iomem *addr, void *dst, unsigned long count)
{
	while (count--) {
		*(u32 *)dst = __raw_readl(addr);
		dst += 4;
	}
}

static void iomem_write8r(void __iomem *addr, const void *s, unsigned long n)
{
	while (n--) {
		__raw_writeb(*(u8 *)s, addr);
		s++;
	}
}

static void iomem_write16r(void __iomem *addr, const void *s, unsigned long n)
{
	while (n--) {
		__raw_writew(*(u16 *)s, addr);
		s += 2;
	}
}

static void iomem_write32r(void __iomem *addr, const void *s, unsigned long n)
{
	while (n--) {
		__raw_writel(*(u32 *)s, addr);
		s += 4;
	}
}

static const struct iomap_ops iomem_ops = {
	iomem_read8,
	iomem_read16,
	iomem_read16be,
	iomem_read32,
	iomem_read32be,
	iomem_write8,
	iomem_write16,
	iomem_write16be,
	iomem_write32,
	iomem_write32be,
	iomem_read8r,
	iomem_read16r,
	iomem_read32r,
	iomem_write8r,
	iomem_write16r,
	iomem_write32r,
};

static const struct iomap_ops *iomap_ops[8] = {
	[0] = &ioport_ops,
	[7] = &iomem_ops
};


unsigned int ioread8(void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr)))
		return iomap_ops[ADDR_TO_REGION(addr)]->read8(addr);
	return *((u8 *)addr);
}

unsigned int ioread16(void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr)))
		return iomap_ops[ADDR_TO_REGION(addr)]->read16(addr);
	return le16_to_cpup((u16 *)addr);
}

unsigned int ioread16be(void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr)))
		return iomap_ops[ADDR_TO_REGION(addr)]->read16be(addr);
	return *((u16 *)addr);
}

unsigned int ioread32(void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr)))
		return iomap_ops[ADDR_TO_REGION(addr)]->read32(addr);
	return le32_to_cpup((u32 *)addr);
}

unsigned int ioread32be(void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr)))
		return iomap_ops[ADDR_TO_REGION(addr)]->read32be(addr);
	return *((u32 *)addr);
}

void iowrite8(u8 datum, void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->write8(datum, addr);
	} else {
		*((u8 *)addr) = datum;
	}
}

void iowrite16(u16 datum, void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->write16(datum, addr);
	} else {
		*((u16 *)addr) = cpu_to_le16(datum);
	}
}

void iowrite16be(u16 datum, void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->write16be(datum, addr);
	} else {
		*((u16 *)addr) = datum;
	}
}

void iowrite32(u32 datum, void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->write32(datum, addr);
	} else {
		*((u32 *)addr) = cpu_to_le32(datum);
	}
}

void iowrite32be(u32 datum, void __iomem *addr)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->write32be(datum, addr);
	} else {
		*((u32 *)addr) = datum;
	}
}

/* Repeating interfaces */

void ioread8_rep(void __iomem *addr, void *dst, unsigned long count)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->read8r(addr, dst, count);
	} else {
		while (count--) {
			*(u8 *)dst = *(u8 *)addr;
			dst++;
		}
	}
}

void ioread16_rep(void __iomem *addr, void *dst, unsigned long count)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->read16r(addr, dst, count);
	} else {
		while (count--) {
			*(u16 *)dst = *(u16 *)addr;
			dst += 2;
		}
	}
}

void ioread32_rep(void __iomem *addr, void *dst, unsigned long count)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->read32r(addr, dst, count);
	} else {
		while (count--) {
			*(u32 *)dst = *(u32 *)addr;
			dst += 4;
		}
	}
}

void iowrite8_rep(void __iomem *addr, const void *src, unsigned long count)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->write8r(addr, src, count);
	} else {
		while (count--) {
			*(u8 *)addr = *(u8 *)src;
			src++;
		}
	}
}

void iowrite16_rep(void __iomem *addr, const void *src, unsigned long count)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->write16r(addr, src, count);
	} else {
		while (count--) {
			*(u16 *)addr = *(u16 *)src;
			src += 2;
		}
	}
}

void iowrite32_rep(void __iomem *addr, const void *src, unsigned long count)
{
	if (unlikely(INDIRECT_ADDR(addr))) {
		iomap_ops[ADDR_TO_REGION(addr)]->write32r(addr, src, count);
	} else {
		while (count--) {
			*(u32 *)addr = *(u32 *)src;
			src += 4;
		}
	}
}

/* Mapping interfaces */

void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	return (void __iomem *)(IOPORT_MAP_BASE | port);
}

void ioport_unmap(void __iomem *addr)
{
	if (!INDIRECT_ADDR(addr)) {
		iounmap(addr);
	}
}

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	resource_size_t start = pci_resource_start(dev, bar);
	resource_size_t len = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (!len || !start)
		return NULL;
	if (maxlen && len > maxlen)
		len = maxlen;
	if (flags & IORESOURCE_IO)
		return ioport_map(start, len);
	if (flags & IORESOURCE_MEM) {
		if (flags & IORESOURCE_CACHEABLE)
			return ioremap(start, len);
		return ioremap_nocache(start, len);
	}
	/* What? */
	return NULL;
}

void pci_iounmap(struct pci_dev *dev, void __iomem * addr)
{
	if (!INDIRECT_ADDR(addr)) {
		iounmap(addr);
	}
}

EXPORT_SYMBOL(ioread8);
EXPORT_SYMBOL(ioread16);
EXPORT_SYMBOL(ioread16be);
EXPORT_SYMBOL(ioread32);
EXPORT_SYMBOL(ioread32be);
EXPORT_SYMBOL(iowrite8);
EXPORT_SYMBOL(iowrite16);
EXPORT_SYMBOL(iowrite16be);
EXPORT_SYMBOL(iowrite32);
EXPORT_SYMBOL(iowrite32be);
EXPORT_SYMBOL(ioread8_rep);
EXPORT_SYMBOL(ioread16_rep);
EXPORT_SYMBOL(ioread32_rep);
EXPORT_SYMBOL(iowrite8_rep);
EXPORT_SYMBOL(iowrite16_rep);
EXPORT_SYMBOL(iowrite32_rep);
EXPORT_SYMBOL(ioport_map);
EXPORT_SYMBOL(ioport_unmap);
EXPORT_SYMBOL(pci_iomap);
EXPORT_SYMBOL(pci_iounmap);
