/*
 * access guest memory
 *
 * Copyright IBM Corp. 2008, 2009
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
#include <asm/uaccess.h>
#include "kvm-s390.h"

static inline void __user *__gptr_to_uptr(struct kvm_vcpu *vcpu,
					  void __user *gptr,
					  int prefixing)
{
	unsigned long prefix  = vcpu->arch.sie_block->prefix;
	unsigned long gaddr = (unsigned long) gptr;
	unsigned long uaddr;

	if (prefixing) {
		if (gaddr < 2 * PAGE_SIZE)
			gaddr += prefix;
		else if ((gaddr >= prefix) && (gaddr < prefix + 2 * PAGE_SIZE))
			gaddr -= prefix;
	}
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

#endif /* __KVM_S390_GACCESS_H */
