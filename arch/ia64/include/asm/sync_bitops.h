#ifndef _ASM_IA64_SYNC_BITOPS_H
#define _ASM_IA64_SYNC_BITOPS_H

/*
 * Copyright (C) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *
 * Based on synch_bitops.h which Dan Magenhaimer wrote.
 *
 * bit operations which provide guaranteed strong synchronisation
 * when communicating with Xen or other guest OSes running on other CPUs.
 */

static inline void sync_set_bit(int nr, volatile void *addr)
{
	set_bit(nr, addr);
}

static inline void sync_clear_bit(int nr, volatile void *addr)
{
	clear_bit(nr, addr);
}

static inline void sync_change_bit(int nr, volatile void *addr)
{
	change_bit(nr, addr);
}

static inline int sync_test_and_set_bit(int nr, volatile void *addr)
{
	return test_and_set_bit(nr, addr);
}

static inline int sync_test_and_clear_bit(int nr, volatile void *addr)
{
	return test_and_clear_bit(nr, addr);
}

static inline int sync_test_and_change_bit(int nr, volatile void *addr)
{
	return test_and_change_bit(nr, addr);
}

static inline int sync_test_bit(int nr, const volatile void *addr)
{
	return test_bit(nr, addr);
}

#define sync_cmpxchg(ptr, old, new)				\
	((__typeof__(*(ptr)))cmpxchg_acq((ptr), (old), (new)))

#endif /* _ASM_IA64_SYNC_BITOPS_H */
