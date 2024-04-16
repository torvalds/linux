/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 */
#ifndef __BERLIN2_COMMON_H
#define __BERLIN2_COMMON_H

struct berlin2_gate_data {
	const char *name;
	const char *parent_name;
	u8 bit_idx;
	unsigned long flags;
};

#endif /* BERLIN2_COMMON_H */
