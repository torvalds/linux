/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_PCI_IO_H
#define _ASM_S390_PCI_IO_H

#ifdef CONFIG_PCI

#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/pci_insn.h>

/* I/O Map */
#define ZPCI_IOMAP_SHIFT		48
#define ZPCI_IOMAP_ADDR_BASE		0x8000000000000000UL
#define ZPCI_IOMAP_ADDR_OFF_MASK	((1UL << ZPCI_IOMAP_SHIFT) - 1)
#define ZPCI_IOMAP_MAX_ENTRIES							\
	((ULONG_MAX - ZPCI_IOMAP_ADDR_BASE + 1) / (1UL << ZPCI_IOMAP_SHIFT))
#define ZPCI_IOMAP_ADDR_IDX_MASK						\
	(~ZPCI_IOMAP_ADDR_OFF_MASK - ZPCI_IOMAP_ADDR_BASE)

struct zpci_iomap_entry {
	u32 fh;
	u8 bar;
	u16 count;
};

extern struct zpci_iomap_entry *zpci_iomap_start;

#define ZPCI_ADDR(idx) (ZPCI_IOMAP_ADDR_BASE | ((u64) idx << ZPCI_IOMAP_SHIFT))
#define ZPCI_IDX(addr)								\
	(((__force u64) addr & ZPCI_IOMAP_ADDR_IDX_MASK) >> ZPCI_IOMAP_SHIFT)
#define ZPCI_OFFSET(addr)							\
	((__force u64) addr & ZPCI_IOMAP_ADDR_OFF_MASK)

#define ZPCI_CREATE_REQ(handle, space, len)					\
	((u64) handle << 32 | space << 16 | len)

#define zpci_read(LENGTH, RETTYPE)						\
static inline RETTYPE zpci_read_##RETTYPE(const volatile void __iomem *addr)	\
{										\
	u64 data;								\
	int rc;									\
										\
	rc = zpci_load(&data, addr, LENGTH);					\
	if (rc)									\
		data = -1ULL;							\
	return (RETTYPE) data;							\
}

#define zpci_write(LENGTH, VALTYPE)						\
static inline void zpci_write_##VALTYPE(VALTYPE val,				\
					const volatile void __iomem *addr)	\
{										\
	u64 data = (VALTYPE) val;						\
										\
	zpci_store(addr, data, LENGTH);						\
}

zpci_read(8, u64)
zpci_read(4, u32)
zpci_read(2, u16)
zpci_read(1, u8)
zpci_write(8, u64)
zpci_write(4, u32)
zpci_write(2, u16)
zpci_write(1, u8)

static inline int zpci_write_single(volatile void __iomem *dst, const void *src,
				    unsigned long len)
{
	u64 val;

	switch (len) {
	case 1:
		val = (u64) *((u8 *) src);
		break;
	case 2:
		val = (u64) *((u16 *) src);
		break;
	case 4:
		val = (u64) *((u32 *) src);
		break;
	case 8:
		val = (u64) *((u64 *) src);
		break;
	default:
		val = 0;		/* let FW report error */
		break;
	}
	return zpci_store(dst, val, len);
}

static inline int zpci_read_single(void *dst, const volatile void __iomem *src,
				   unsigned long len)
{
	u64 data;
	int cc;

	cc = zpci_load(&data, src, len);
	if (cc)
		goto out;

	switch (len) {
	case 1:
		*((u8 *) dst) = (u8) data;
		break;
	case 2:
		*((u16 *) dst) = (u16) data;
		break;
	case 4:
		*((u32 *) dst) = (u32) data;
		break;
	case 8:
		*((u64 *) dst) = (u64) data;
		break;
	}
out:
	return cc;
}

int zpci_write_block(volatile void __iomem *dst, const void *src,
		     unsigned long len);

static inline u8 zpci_get_max_write_size(u64 src, u64 dst, int len, int max)
{
	int count = len > max ? max : len, size = 1;

	while (!(src & 0x1) && !(dst & 0x1) && ((size << 1) <= count)) {
		dst = dst >> 1;
		src = src >> 1;
		size = size << 1;
	}
	return size;
}

static inline int zpci_memcpy_fromio(void *dst,
				     const volatile void __iomem *src,
				     unsigned long n)
{
	int size, rc = 0;

	while (n > 0) {
		size = zpci_get_max_write_size((u64 __force) src,
					       (u64) dst, n, 8);
		rc = zpci_read_single(dst, src, size);
		if (rc)
			break;
		src += size;
		dst += size;
		n -= size;
	}
	return rc;
}

static inline int zpci_memcpy_toio(volatile void __iomem *dst,
				   const void *src, unsigned long n)
{
	int size, rc = 0;

	if (!src)
		return -EINVAL;

	while (n > 0) {
		size = zpci_get_max_write_size((u64 __force) dst,
					       (u64) src, n, 128);
		if (size > 8) /* main path */
			rc = zpci_write_block(dst, src, size);
		else
			rc = zpci_write_single(dst, src, size);
		if (rc)
			break;
		src += size;
		dst += size;
		n -= size;
	}
	return rc;
}

static inline int zpci_memset_io(volatile void __iomem *dst,
				 unsigned char val, size_t count)
{
	u8 *src = kmalloc(count, GFP_KERNEL);
	int rc;

	if (src == NULL)
		return -ENOMEM;
	memset(src, val, count);

	rc = zpci_memcpy_toio(dst, src, count);
	kfree(src);
	return rc;
}

#endif /* CONFIG_PCI */

#endif /* _ASM_S390_PCI_IO_H */
