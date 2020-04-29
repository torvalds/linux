/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _MALIDP_UTILS_
#define _MALIDP_UTILS_

#include <linux/delay.h>
#include <linux/errno.h>

#define has_bit(nr, mask)	(BIT(nr) & (mask))
#define has_bits(bits, mask)	(((bits) & (mask)) == (bits))

#define dp_for_each_set_bit(bit, mask) \
	for_each_set_bit((bit), ((unsigned long *)&(mask)), sizeof(mask) * 8)

#define dp_wait_cond(__cond, __tries, __min_range, __max_range)	\
({							\
	int num_tries = __tries;			\
	while (!__cond && (num_tries > 0)) {		\
		usleep_range(__min_range, __max_range);	\
		num_tries--;				\
	}						\
	(__cond) ? 0 : -ETIMEDOUT;			\
})

/* the restriction of range is [start, end] */
struct malidp_range {
	u32 start;
	u32 end;
};

static inline void set_range(struct malidp_range *rg, u32 start, u32 end)
{
	rg->start = start;
	rg->end   = end;
}

static inline bool in_range(struct malidp_range *rg, u32 v)
{
	return (v >= rg->start) && (v <= rg->end);
}

#endif /* _MALIDP_UTILS_ */
