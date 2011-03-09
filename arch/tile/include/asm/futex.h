/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * These routines make two important assumptions:
 *
 * 1. atomic_t is really an int and can be freely cast back and forth
 *    (validated in __init_atomic_per_cpu).
 *
 * 2. userspace uses sys_cmpxchg() for all atomic operations, thus using
 *    the same locking convention that all the kernel atomic routines use.
 */

#ifndef _ASM_TILE_FUTEX_H
#define _ASM_TILE_FUTEX_H

#ifndef __ASSEMBLY__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

extern struct __get_user futex_set(int __user *v, int i);
extern struct __get_user futex_add(int __user *v, int n);
extern struct __get_user futex_or(int __user *v, int n);
extern struct __get_user futex_andn(int __user *v, int n);
extern struct __get_user futex_cmpxchg(int __user *v, int o, int n);

#ifndef __tilegx__
extern struct __get_user futex_xor(int __user *v, int n);
#else
static inline struct __get_user futex_xor(int __user *uaddr, int n)
{
	struct __get_user asm_ret = __get_user_4(uaddr);
	if (!asm_ret.err) {
		int oldval, newval;
		do {
			oldval = asm_ret.val;
			newval = oldval ^ n;
			asm_ret = futex_cmpxchg(uaddr, oldval, newval);
		} while (asm_ret.err == 0 && oldval != asm_ret.val);
	}
	return asm_ret;
}
#endif

static inline int futex_atomic_op_inuser(int encoded_op, int __user *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int ret;
	struct __get_user asm_ret;

	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(int)))
		return -EFAULT;

	pagefault_disable();
	switch (op) {
	case FUTEX_OP_SET:
		asm_ret = futex_set(uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		asm_ret = futex_add(uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		asm_ret = futex_or(uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		asm_ret = futex_andn(uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		asm_ret = futex_xor(uaddr, oparg);
		break;
	default:
		asm_ret.err = -ENOSYS;
	}
	pagefault_enable();

	ret = asm_ret.err;

	if (!ret) {
		switch (cmp) {
		case FUTEX_OP_CMP_EQ:
			ret = (asm_ret.val == cmparg);
			break;
		case FUTEX_OP_CMP_NE:
			ret = (asm_ret.val != cmparg);
			break;
		case FUTEX_OP_CMP_LT:
			ret = (asm_ret.val < cmparg);
			break;
		case FUTEX_OP_CMP_GE:
			ret = (asm_ret.val >= cmparg);
			break;
		case FUTEX_OP_CMP_LE:
			ret = (asm_ret.val <= cmparg);
			break;
		case FUTEX_OP_CMP_GT:
			ret = (asm_ret.val > cmparg);
			break;
		default:
			ret = -ENOSYS;
		}
	}
	return ret;
}

static inline int futex_atomic_cmpxchg_inatomic(int __user *uaddr, int oldval,
						int newval)
{
	struct __get_user asm_ret;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(int)))
		return -EFAULT;

	asm_ret = futex_cmpxchg(uaddr, oldval, newval);
	return asm_ret.err ? asm_ret.err : asm_ret.val;
}

#ifndef __tilegx__
/* Return failure from the atomic wrappers. */
struct __get_user __atomic_bad_address(int __user *addr);
#endif

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_TILE_FUTEX_H */
