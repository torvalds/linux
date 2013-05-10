#ifndef _IO_H
#define __IO_H

#include "types.h"

/*
 * Low-level I/O routines.
 *
 * Copied from <file:arch/powerpc/include/asm/io.h> (which has no copyright)
 */
static inline int in_8(const volatile unsigned char *addr)
{
	int ret;

	__asm__ __volatile__("lbz%U1%X1 %0,%1; twi 0,%0,0; isync"
			     : "=r" (ret) : "m" (*addr));
	return ret;
}

static inline void out_8(volatile unsigned char *addr, int val)
{
	__asm__ __volatile__("stb%U0%X0 %1,%0; sync"
			     : "=m" (*addr) : "r" (val));
}

static inline unsigned in_le16(const volatile u16 *addr)
{
	unsigned ret;

	__asm__ __volatile__("lhbrx %0,0,%1; twi 0,%0,0; isync"
			     : "=r" (ret) : "r" (addr), "m" (*addr));

	return ret;
}

static inline unsigned in_be16(const volatile u16 *addr)
{
	unsigned ret;

	__asm__ __volatile__("lhz%U1%X1 %0,%1; twi 0,%0,0; isync"
			     : "=r" (ret) : "m" (*addr));
	return ret;
}

static inline void out_le16(volatile u16 *addr, int val)
{
	__asm__ __volatile__("sthbrx %1,0,%2; sync" : "=m" (*addr)
			     : "r" (val), "r" (addr));
}

static inline void out_be16(volatile u16 *addr, int val)
{
	__asm__ __volatile__("sth%U0%X0 %1,%0; sync"
			     : "=m" (*addr) : "r" (val));
}

static inline unsigned in_le32(const volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("lwbrx %0,0,%1; twi 0,%0,0; isync"
			     : "=r" (ret) : "r" (addr), "m" (*addr));
	return ret;
}

static inline unsigned in_be32(const volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("lwz%U1%X1 %0,%1; twi 0,%0,0; isync"
			     : "=r" (ret) : "m" (*addr));
	return ret;
}

static inline void out_le32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stwbrx %1,0,%2; sync" : "=m" (*addr)
			     : "r" (val), "r" (addr));
}

static inline void out_be32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stw%U0%X0 %1,%0; sync"
			     : "=m" (*addr) : "r" (val));
}

static inline void sync(void)
{
	asm volatile("sync" : : : "memory");
}

static inline void eieio(void)
{
	asm volatile("eieio" : : : "memory");
}

static inline void barrier(void)
{
	asm volatile("" : : : "memory");
}

#endif /* _IO_H */
