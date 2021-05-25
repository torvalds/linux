/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_M68K_CMPXCHG__
#define __ARCH_M68K_CMPXCHG__

#include <linux/irqflags.h>

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

extern unsigned long __invalid_xchg_size(unsigned long, volatile void *, int);

#ifndef CONFIG_RMW_INSNS
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	unsigned long flags, tmp;

	local_irq_save(flags);

	switch (size) {
	case 1:
		tmp = *(u8 *)ptr;
		*(u8 *)ptr = x;
		x = tmp;
		break;
	case 2:
		tmp = *(u16 *)ptr;
		*(u16 *)ptr = x;
		x = tmp;
		break;
	case 4:
		tmp = *(u32 *)ptr;
		*(u32 *)ptr = x;
		x = tmp;
		break;
	default:
		tmp = __invalid_xchg_size(x, ptr, size);
		break;
	}

	local_irq_restore(flags);
	return x;
}
#else
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
	case 1:
		__asm__ __volatile__
			("moveb %2,%0\n\t"
			 "1:\n\t"
			 "casb %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	case 2:
		__asm__ __volatile__
			("movew %2,%0\n\t"
			 "1:\n\t"
			 "casw %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	case 4:
		__asm__ __volatile__
			("movel %2,%0\n\t"
			 "1:\n\t"
			 "casl %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	default:
		x = __invalid_xchg_size(x, ptr, size);
		break;
	}
	return x;
}
#endif

#define arch_xchg(ptr,x) ({(__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr)));})

#include <asm-generic/cmpxchg-local.h>

#define arch_cmpxchg64_local(ptr, o, n) __generic_cmpxchg64_local((ptr), (o), (n))

extern unsigned long __invalid_cmpxchg_size(volatile void *,
					    unsigned long, unsigned long, int);

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
#ifdef CONFIG_RMW_INSNS

static inline unsigned long __cmpxchg(volatile void *p, unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 1:
		__asm__ __volatile__ ("casb %0,%2,%1"
				      : "=d" (old), "=m" (*(char *)p)
				      : "d" (new), "0" (old), "m" (*(char *)p));
		break;
	case 2:
		__asm__ __volatile__ ("casw %0,%2,%1"
				      : "=d" (old), "=m" (*(short *)p)
				      : "d" (new), "0" (old), "m" (*(short *)p));
		break;
	case 4:
		__asm__ __volatile__ ("casl %0,%2,%1"
				      : "=d" (old), "=m" (*(int *)p)
				      : "d" (new), "0" (old), "m" (*(int *)p));
		break;
	default:
		old = __invalid_cmpxchg_size(p, old, new, size);
		break;
	}
	return old;
}

#define arch_cmpxchg(ptr, o, n)						    \
	({(__typeof__(*(ptr)))__cmpxchg((ptr), (unsigned long)(o),	    \
			(unsigned long)(n), sizeof(*(ptr)));})
#define arch_cmpxchg_local(ptr, o, n)					    \
	({(__typeof__(*(ptr)))__cmpxchg((ptr), (unsigned long)(o),	    \
			(unsigned long)(n), sizeof(*(ptr)));})

#define arch_cmpxchg64(ptr, o, n)	arch_cmpxchg64_local((ptr), (o), (n))

#else

#include <asm-generic/cmpxchg.h>

#endif

#endif /* __ARCH_M68K_CMPXCHG__ */
