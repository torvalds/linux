/*
 * access guest memory
 *
 * Copyright IBM Corp. 2008, 2014
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 */

#ifndef __KVM_S390_GACCESS_H
#define __KVM_S390_GACCESS_H

#include <linux/compiler.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include "kvm-s390.h"

/* Convert real to absolute address by applying the prefix of the CPU */
static inline unsigned long kvm_s390_real_to_abs(struct kvm_vcpu *vcpu,
						 unsigned long gaddr)
{
	unsigned long prefix  = vcpu->arch.sie_block->prefix;
	if (gaddr < 2 * PAGE_SIZE)
		gaddr += prefix;
	else if (gaddr >= prefix && gaddr < prefix + 2 * PAGE_SIZE)
		gaddr -= prefix;
	return gaddr;
}

/**
 * kvm_s390_logical_to_effective - convert guest logical to effective address
 * @vcpu: guest virtual cpu
 * @ga: guest logical address
 *
 * Convert a guest vcpu logical address to a guest vcpu effective address by
 * applying the rules of the vcpu's addressing mode defined by PSW bits 31
 * and 32 (extendended/basic addressing mode).
 *
 * Depending on the vcpu's addressing mode the upper 40 bits (24 bit addressing
 * mode), 33 bits (31 bit addressing mode) or no bits (64 bit addressing mode)
 * of @ga will be zeroed and the remaining bits will be returned.
 */
static inline unsigned long kvm_s390_logical_to_effective(struct kvm_vcpu *vcpu,
							  unsigned long ga)
{
	psw_t *psw = &vcpu->arch.sie_block->gpsw;

	if (psw_bits(*psw).eaba == PSW_AMODE_64BIT)
		return ga;
	if (psw_bits(*psw).eaba == PSW_AMODE_31BIT)
		return ga & ((1UL << 31) - 1);
	return ga & ((1UL << 24) - 1);
}

static inline void __user *__gptr_to_uptr(struct kvm_vcpu *vcpu,
					  void __user *gptr,
					  int prefixing)
{
	unsigned long gaddr = (unsigned long) gptr;
	unsigned long uaddr;

	if (prefixing)
		gaddr = kvm_s390_real_to_abs(vcpu, gaddr);
	uaddr = gmap_fault(gaddr, vcpu->arch.gmap);
	if (IS_ERR_VALUE(uaddr))
		uaddr = -EFAULT;
	return (void __user *)uaddr;
}

#define get_guest(vcpu, x, gptr)				\
({								\
	__typeof__(gptr) __uptr = __gptr_to_uptr(vcpu, gptr, 1);\
	int __mask = sizeof(__typeof__(*(gptr))) - 1;		\
	int __ret;						\
								\
	if (IS_ERR((void __force *)__uptr)) {			\
		__ret = PTR_ERR((void __force *)__uptr);	\
	} else {						\
		BUG_ON((unsigned long)__uptr & __mask);		\
		__ret = get_user(x, __uptr);			\
	}							\
	__ret;							\
})

#define put_guest(vcpu, x, gptr)				\
({								\
	__typeof__(gptr) __uptr = __gptr_to_uptr(vcpu, gptr, 1);\
	int __mask = sizeof(__typeof__(*(gptr))) - 1;		\
	int __ret;						\
								\
	if (IS_ERR((void __force *)__uptr)) {			\
		__ret = PTR_ERR((void __force *)__uptr);	\
	} else {						\
		BUG_ON((unsigned long)__uptr & __mask);		\
		__ret = put_user(x, __uptr);			\
	}							\
	__ret;							\
})

static inline int __copy_guest(struct kvm_vcpu *vcpu, unsigned long to,
			       unsigned long from, unsigned long len,
			       int to_guest, int prefixing)
{
	unsigned long _len, rc;
	void __user *uptr;

	while (len) {
		uptr = to_guest ? (void __user *)to : (void __user *)from;
		uptr = __gptr_to_uptr(vcpu, uptr, prefixing);
		if (IS_ERR((void __force *)uptr))
			return -EFAULT;
		_len = PAGE_SIZE - ((unsigned long)uptr & (PAGE_SIZE - 1));
		_len = min(_len, len);
		if (to_guest)
			rc = copy_to_user((void __user *) uptr, (void *)from, _len);
		else
			rc = copy_from_user((void *)to, (void __user *)uptr, _len);
		if (rc)
			return -EFAULT;
		len -= _len;
		from += _len;
		to += _len;
	}
	return 0;
}

#define copy_to_guest(vcpu, to, from, size) \
	__copy_guest(vcpu, to, (unsigned long)from, size, 1, 1)
#define copy_from_guest(vcpu, to, from, size) \
	__copy_guest(vcpu, (unsigned long)to, from, size, 0, 1)
#define copy_to_guest_absolute(vcpu, to, from, size) \
	__copy_guest(vcpu, to, (unsigned long)from, size, 1, 0)
#define copy_from_guest_absolute(vcpu, to, from, size) \
	__copy_guest(vcpu, (unsigned long)to, from, size, 0, 0)

/*
 * put_guest_lc, read_guest_lc and write_guest_lc are guest access functions
 * which shall only be used to access the lowcore of a vcpu.
 * These functions should be used for e.g. interrupt handlers where no
 * guest memory access protection facilities, like key or low address
 * protection, are applicable.
 * At a later point guest vcpu lowcore access should happen via pinned
 * prefix pages, so that these pages can be accessed directly via the
 * kernel mapping. All of these *_lc functions can be removed then.
 */

/**
 * put_guest_lc - write a simple variable to a guest vcpu's lowcore
 * @vcpu: virtual cpu
 * @x: value to copy to guest
 * @gra: vcpu's destination guest real address
 *
 * Copies a simple value from kernel space to a guest vcpu's lowcore.
 * The size of the variable may be 1, 2, 4 or 8 bytes. The destination
 * must be located in the vcpu's lowcore. Otherwise the result is undefined.
 *
 * Returns zero on success or -EFAULT on error.
 *
 * Note: an error indicates that either the kernel is out of memory or
 *	 the guest memory mapping is broken. In any case the best solution
 *	 would be to terminate the guest.
 *	 It is wrong to inject a guest exception.
 */
#define put_guest_lc(vcpu, x, gra)				\
({								\
	struct kvm_vcpu *__vcpu = (vcpu);			\
	__typeof__(*(gra)) __x = (x);				\
	unsigned long __gpa;					\
								\
	__gpa = (unsigned long)(gra);				\
	__gpa += __vcpu->arch.sie_block->prefix;		\
	kvm_write_guest(__vcpu->kvm, __gpa, &__x, sizeof(__x));	\
})

/**
 * write_guest_lc - copy data from kernel space to guest vcpu's lowcore
 * @vcpu: virtual cpu
 * @gra: vcpu's source guest real address
 * @data: source address in kernel space
 * @len: number of bytes to copy
 *
 * Copy data from kernel space to guest vcpu's lowcore. The entire range must
 * be located within the vcpu's lowcore, otherwise the result is undefined.
 *
 * Returns zero on success or -EFAULT on error.
 *
 * Note: an error indicates that either the kernel is out of memory or
 *	 the guest memory mapping is broken. In any case the best solution
 *	 would be to terminate the guest.
 *	 It is wrong to inject a guest exception.
 */
static inline __must_check
int write_guest_lc(struct kvm_vcpu *vcpu, unsigned long gra, void *data,
		   unsigned long len)
{
	unsigned long gpa = gra + vcpu->arch.sie_block->prefix;

	return kvm_write_guest(vcpu->kvm, gpa, data, len);
}

/**
 * read_guest_lc - copy data from guest vcpu's lowcore to kernel space
 * @vcpu: virtual cpu
 * @gra: vcpu's source guest real address
 * @data: destination address in kernel space
 * @len: number of bytes to copy
 *
 * Copy data from guest vcpu's lowcore to kernel space. The entire range must
 * be located within the vcpu's lowcore, otherwise the result is undefined.
 *
 * Returns zero on success or -EFAULT on error.
 *
 * Note: an error indicates that either the kernel is out of memory or
 *	 the guest memory mapping is broken. In any case the best solution
 *	 would be to terminate the guest.
 *	 It is wrong to inject a guest exception.
 */
static inline __must_check
int read_guest_lc(struct kvm_vcpu *vcpu, unsigned long gra, void *data,
		  unsigned long len)
{
	unsigned long gpa = gra + vcpu->arch.sie_block->prefix;

	return kvm_read_guest(vcpu->kvm, gpa, data, len);
}
#endif /* __KVM_S390_GACCESS_H */
