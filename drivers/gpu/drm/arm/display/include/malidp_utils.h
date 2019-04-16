/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _MALIDP_UTILS_
#define _MALIDP_UTILS_

#define has_bit(nr, mask)	(BIT(nr) & (mask))
#define has_bits(bits, mask)	(((bits) & (mask)) == (bits))

#define dp_for_each_set_bit(bit, mask) \
	for_each_set_bit((bit), ((unsigned long *)&(mask)), sizeof(mask) * 8)

#endif /* _MALIDP_UTILS_ */
