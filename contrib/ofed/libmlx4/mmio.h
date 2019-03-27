/* Licensed under the OpenIB.org BSD license (FreeBSD Variant) - See COPYING.md
 */
#ifndef MMIO_H
#define MMIO_H

#include <unistd.h>
#include <sys/syscall.h>
#ifdef __s390x__

static inline long mmio_writeb(const unsigned long mmio_addr,
			       const uint8_t val)
{
	return syscall(__NR_s390_pci_mmio_write, mmio_addr, &val, sizeof(val));
}

static inline long mmio_writew(const unsigned long mmio_addr,
			       const uint16_t val)
{
	return syscall(__NR_s390_pci_mmio_write, mmio_addr, &val, sizeof(val));
}

static inline long mmio_writel(const unsigned long mmio_addr,
			       const uint32_t val)
{
	return syscall(__NR_s390_pci_mmio_write, mmio_addr, &val, sizeof(val));
}

static inline long mmio_writeq(const unsigned long mmio_addr,
			       const uint64_t val)
{
	return syscall(__NR_s390_pci_mmio_write, mmio_addr, &val, sizeof(val));
}

static inline long mmio_write(const unsigned long mmio_addr,
			      const void *val,
			      const size_t length)
{
	return syscall(__NR_s390_pci_mmio_write, mmio_addr, val, length);
}

static inline long mmio_readb(const unsigned long mmio_addr, uint8_t *val)
{
	return syscall(__NR_s390_pci_mmio_read, mmio_addr, val, sizeof(*val));
}

static inline long mmio_readw(const unsigned long mmio_addr, uint16_t *val)
{
	return syscall(__NR_s390_pci_mmio_read, mmio_addr, val, sizeof(*val));
}

static inline long mmio_readl(const unsigned long mmio_addr, uint32_t *val)
{
	return syscall(__NR_s390_pci_mmio_read, mmio_addr, val, sizeof(*val));
}

static inline long mmio_readq(const unsigned long mmio_addr, uint64_t *val)
{
	return syscall(__NR_s390_pci_mmio_read, mmio_addr, val, sizeof(*val));
}

static inline long mmio_read(const unsigned long mmio_addr,
			     void *val,
			     const size_t length)
{
	return syscall(__NR_s390_pci_mmio_read, mmio_addr, val, length);
}

static inline void mlx4_bf_copy(unsigned long *dst,
				unsigned long *src,
				unsigned bytecnt)
{
	mmio_write((unsigned long)dst, src, bytecnt);
}

#else

#define mmio_writeb(addr, value) \
	(*((volatile uint8_t *)addr) = value)
#define mmio_writew(addr, value) \
	(*((volatile uint16_t *)addr) = value)
#define mmio_writel(addr, value) \
	(*((volatile uint32_t *)addr) = value)
#define mmio_writeq(addr, value) \
	(*((volatile uint64_t *)addr) = value)
#define mmio_write(addr, value, length) \
	memcpy(addr, value, length)

#define mmio_readb(addr, value) \
	(value = *((volatile uint8_t *)addr))
#define mmio_readw(addr, value) \
	(value = *((volatile uint16_t *)addr))
#define mmio_readl(addr, value) \
	(value = *((volatile uint32_t *)addr))
#define mmio_readq(addr, value) \
	(value = *((volatile uint64_t *)addr))
#define mmio_read(addr, value, length) \
	memcpy(value, addr, length)

/*
 * Avoid using memcpy() to copy to BlueFlame page, since memcpy()
 * implementations may use move-string-buffer assembler instructions,
 * which do not guarantee order of copying.
 */
static inline void mlx4_bf_copy(unsigned long *dst,
				unsigned long *src,
				unsigned bytecnt)
{
	while (bytecnt > 0) {
		*dst++ = *src++;
		*dst++ = *src++;
		bytecnt -= 2 * sizeof(long);
	}
}
#endif

#endif
