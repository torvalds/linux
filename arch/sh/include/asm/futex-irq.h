#ifndef __ASM_SH_FUTEX_IRQ_H
#define __ASM_SH_FUTEX_IRQ_H

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

#endif /* __ASM_SH_FUTEX_IRQ_H */
