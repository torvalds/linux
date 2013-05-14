/*
 *  Copyright (C) 2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on asm/futex.h from sh platform.
 *
 */

#ifndef __ASM_NIOS2_FUTEX_IRQ_H
#define __ASM_NIOS2_FUTEX_IRQ_H


static inline int atomic_futex_op_xchg_set(int oparg, u32 __user *uaddr,
					   int *oldval)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);

	ret = get_user(*oldval, uaddr);
	if (!ret)
		ret = put_user(oparg, uaddr);

	local_irq_restore(flags);

	return ret;
}

static inline int atomic_futex_op_xchg_add(int oparg, u32 __user *uaddr,
					   int *oldval)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);

	ret = get_user(*oldval, uaddr);
	if (!ret)
		ret = put_user(*oldval + oparg, uaddr);

	local_irq_restore(flags);

	return ret;
}

static inline int atomic_futex_op_xchg_or(int oparg, u32 __user *uaddr,
					  int *oldval)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);

	ret = get_user(*oldval, uaddr);
	if (!ret)
		ret = put_user(*oldval | oparg, uaddr);

	local_irq_restore(flags);

	return ret;
}

static inline int atomic_futex_op_xchg_and(int oparg, u32 __user *uaddr,
					   int *oldval)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);

	ret = get_user(*oldval, uaddr);
	if (!ret)
		ret = put_user(*oldval & oparg, uaddr);

	local_irq_restore(flags);

	return ret;
}

static inline int atomic_futex_op_xchg_xor(int oparg, u32 __user *uaddr,
					   int *oldval)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);

	ret = get_user(*oldval, uaddr);
	if (!ret)
		ret = put_user(*oldval ^ oparg, uaddr);

	local_irq_restore(flags);

	return ret;
}

static inline int atomic_futex_op_cmpxchg_inatomic(u32 *uval,
						u32 __user *uaddr,
						u32 oldval, u32 newval)
{
	unsigned long flags;
	int ret;
	u32 prev = 0;

	local_irq_save(flags);

	ret = get_user(prev, uaddr);
	if (!ret && oldval == prev)
		ret = put_user(newval, uaddr);

	local_irq_restore(flags);

	*uval = prev;
	return ret;
}

#endif /* __ASM_NIOS2_FUTEX_IRQ_H */
