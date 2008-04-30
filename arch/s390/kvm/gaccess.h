/*
 * gaccess.h -  access guest memory
 *
 * Copyright IBM Corp. 2008
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

static inline void __user *__guestaddr_to_user(struct kvm_vcpu *vcpu,
					       u64 guestaddr)
{
	u64 prefix  = vcpu->arch.sie_block->prefix;
	u64 origin  = vcpu->kvm->arch.guest_origin;
	u64 memsize = vcpu->kvm->arch.guest_memsize;

	if (guestaddr < 2 * PAGE_SIZE)
		guestaddr += prefix;
	else if ((guestaddr >= prefix) && (guestaddr < prefix + 2 * PAGE_SIZE))
		guestaddr -= prefix;

	if (guestaddr > memsize)
		return (void __user __force *) ERR_PTR(-EFAULT);

	guestaddr += origin;

	return (void __user *) guestaddr;
}

static inline int get_guest_u64(struct kvm_vcpu *vcpu, u64 guestaddr,
				u64 *result)
{
	void __user *uptr = __guestaddr_to_user(vcpu, guestaddr);

	BUG_ON(guestaddr & 7);

	if (IS_ERR((void __force *) uptr))
		return PTR_ERR((void __force *) uptr);

	return get_user(*result, (u64 __user *) uptr);
}

static inline int get_guest_u32(struct kvm_vcpu *vcpu, u64 guestaddr,
				u32 *result)
{
	void __user *uptr = __guestaddr_to_user(vcpu, guestaddr);

	BUG_ON(guestaddr & 3);

	if (IS_ERR((void __force *) uptr))
		return PTR_ERR((void __force *) uptr);

	return get_user(*result, (u32 __user *) uptr);
}

static inline int get_guest_u16(struct kvm_vcpu *vcpu, u64 guestaddr,
				u16 *result)
{
	void __user *uptr = __guestaddr_to_user(vcpu, guestaddr);

	BUG_ON(guestaddr & 1);

	if (IS_ERR(uptr))
		return PTR_ERR(uptr);

	return get_user(*result, (u16 __user *) uptr);
}

static inline int get_guest_u8(struct kvm_vcpu *vcpu, u64 guestaddr,
			       u8 *result)
{
	void __user *uptr = __guestaddr_to_user(vcpu, guestaddr);

	if (IS_ERR((void __force *) uptr))
		return PTR_ERR((void __force *) uptr);

	return get_user(*result, (u8 __user *) uptr);
}

static inline int put_guest_u64(struct kvm_vcpu *vcpu, u64 guestaddr,
				u64 value)
{
	void __user *uptr = __guestaddr_to_user(vcpu, guestaddr);

	BUG_ON(guestaddr & 7);

	if (IS_ERR((void __force *) uptr))
		return PTR_ERR((void __force *) uptr);

	return put_user(value, (u64 __user *) uptr);
}

static inline int put_guest_u32(struct kvm_vcpu *vcpu, u64 guestaddr,
				u32 value)
{
	void __user *uptr = __guestaddr_to_user(vcpu, guestaddr);

	BUG_ON(guestaddr & 3);

	if (IS_ERR((void __force *) uptr))
		return PTR_ERR((void __force *) uptr);

	return put_user(value, (u32 __user *) uptr);
}

static inline int put_guest_u16(struct kvm_vcpu *vcpu, u64 guestaddr,
				u16 value)
{
	void __user *uptr = __guestaddr_to_user(vcpu, guestaddr);

	BUG_ON(guestaddr & 1);

	if (IS_ERR((void __force *) uptr))
		return PTR_ERR((void __force *) uptr);

	return put_user(value, (u16 __user *) uptr);
}

static inline int put_guest_u8(struct kvm_vcpu *vcpu, u64 guestaddr,
			       u8 value)
{
	void __user *uptr = __guestaddr_to_user(vcpu, guestaddr);

	if (IS_ERR((void __force *) uptr))
		return PTR_ERR((void __force *) uptr);

	return put_user(value, (u8 __user *) uptr);
}


static inline int __copy_to_guest_slow(struct kvm_vcpu *vcpu, u64 guestdest,
				       const void *from, unsigned long n)
{
	int rc;
	unsigned long i;
	const u8 *data = from;

	for (i = 0; i < n; i++) {
		rc = put_guest_u8(vcpu, guestdest++, *(data++));
		if (rc < 0)
			return rc;
	}
	return 0;
}

static inline int copy_to_guest(struct kvm_vcpu *vcpu, u64 guestdest,
				const void *from, unsigned long n)
{
	u64 prefix  = vcpu->arch.sie_block->prefix;
	u64 origin  = vcpu->kvm->arch.guest_origin;
	u64 memsize = vcpu->kvm->arch.guest_memsize;

	if ((guestdest < 2 * PAGE_SIZE) && (guestdest + n > 2 * PAGE_SIZE))
		goto slowpath;

	if ((guestdest < prefix) && (guestdest + n > prefix))
		goto slowpath;

	if ((guestdest < prefix + 2 * PAGE_SIZE)
	    && (guestdest + n > prefix + 2 * PAGE_SIZE))
		goto slowpath;

	if (guestdest < 2 * PAGE_SIZE)
		guestdest += prefix;
	else if ((guestdest >= prefix) && (guestdest < prefix + 2 * PAGE_SIZE))
		guestdest -= prefix;

	if (guestdest + n > memsize)
		return -EFAULT;

	if (guestdest + n < guestdest)
		return -EFAULT;

	guestdest += origin;

	return copy_to_user((void __user *) guestdest, from, n);
slowpath:
	return __copy_to_guest_slow(vcpu, guestdest, from, n);
}

static inline int __copy_from_guest_slow(struct kvm_vcpu *vcpu, void *to,
					 u64 guestsrc, unsigned long n)
{
	int rc;
	unsigned long i;
	u8 *data = to;

	for (i = 0; i < n; i++) {
		rc = get_guest_u8(vcpu, guestsrc++, data++);
		if (rc < 0)
			return rc;
	}
	return 0;
}

static inline int copy_from_guest(struct kvm_vcpu *vcpu, void *to,
				  u64 guestsrc, unsigned long n)
{
	u64 prefix  = vcpu->arch.sie_block->prefix;
	u64 origin  = vcpu->kvm->arch.guest_origin;
	u64 memsize = vcpu->kvm->arch.guest_memsize;

	if ((guestsrc < 2 * PAGE_SIZE) && (guestsrc + n > 2 * PAGE_SIZE))
		goto slowpath;

	if ((guestsrc < prefix) && (guestsrc + n > prefix))
		goto slowpath;

	if ((guestsrc < prefix + 2 * PAGE_SIZE)
	    && (guestsrc + n > prefix + 2 * PAGE_SIZE))
		goto slowpath;

	if (guestsrc < 2 * PAGE_SIZE)
		guestsrc += prefix;
	else if ((guestsrc >= prefix) && (guestsrc < prefix + 2 * PAGE_SIZE))
		guestsrc -= prefix;

	if (guestsrc + n > memsize)
		return -EFAULT;

	if (guestsrc + n < guestsrc)
		return -EFAULT;

	guestsrc += origin;

	return copy_from_user(to, (void __user *) guestsrc, n);
slowpath:
	return __copy_from_guest_slow(vcpu, to, guestsrc, n);
}

static inline int copy_to_guest_absolute(struct kvm_vcpu *vcpu, u64 guestdest,
					 const void *from, unsigned long n)
{
	u64 origin  = vcpu->kvm->arch.guest_origin;
	u64 memsize = vcpu->kvm->arch.guest_memsize;

	if (guestdest + n > memsize)
		return -EFAULT;

	if (guestdest + n < guestdest)
		return -EFAULT;

	guestdest += origin;

	return copy_to_user((void __user *) guestdest, from, n);
}

static inline int copy_from_guest_absolute(struct kvm_vcpu *vcpu, void *to,
					   u64 guestsrc, unsigned long n)
{
	u64 origin  = vcpu->kvm->arch.guest_origin;
	u64 memsize = vcpu->kvm->arch.guest_memsize;

	if (guestsrc + n > memsize)
		return -EFAULT;

	if (guestsrc + n < guestsrc)
		return -EFAULT;

	guestsrc += origin;

	return copy_from_user(to, (void __user *) guestsrc, n);
}
#endif
