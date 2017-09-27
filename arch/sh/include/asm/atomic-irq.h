#ifndef __ASM_SH_ATOMIC_IRQ_H
#define __ASM_SH_ATOMIC_IRQ_H

#include <linux/irqflags.h>

/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */

#define ATOMIC_OP(op, c_op)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long flags;						\
									\
	raw_local_irq_save(flags);					\
	v->counter c_op i;						\
	raw_local_irq_restore(flags);					\
}

#define ATOMIC_OP_RETURN(op, c_op)					\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long temp, flags;					\
									\
	raw_local_irq_save(flags);					\
	temp = v->counter;						\
	temp c_op i;							\
	v->counter = temp;						\
	raw_local_irq_restore(flags);					\
									\
	return temp;							\
}

#define ATOMIC_FETCH_OP(op, c_op)					\
static inline int atomic_fetch_##op(int i, atomic_t *v)			\
{									\
	unsigned long temp, flags;					\
									\
	raw_local_irq_save(flags);					\
	temp = v->counter;						\
	v->counter c_op i;						\
	raw_local_irq_restore(flags);					\
									\
	return temp;							\
}

#define ATOMIC_OPS(op, c_op)						\
	ATOMIC_OP(op, c_op)						\
	ATOMIC_OP_RETURN(op, c_op)					\
	ATOMIC_FETCH_OP(op, c_op)

ATOMIC_OPS(add, +=)
ATOMIC_OPS(sub, -=)

#undef ATOMIC_OPS
#define ATOMIC_OPS(op, c_op)						\
	ATOMIC_OP(op, c_op)						\
	ATOMIC_FETCH_OP(op, c_op)

ATOMIC_OPS(and, &=)
ATOMIC_OPS(or, |=)
ATOMIC_OPS(xor, ^=)

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#endif /* __ASM_SH_ATOMIC_IRQ_H */
