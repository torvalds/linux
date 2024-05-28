// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2023
 */

#include <asm/facility.h>

unsigned int stfle_size(void)
{
	static unsigned int size;
	unsigned int r;
	u64 dummy;

	r = READ_ONCE(size);
	if (!r) {
		r = __stfle_asm(&dummy, 1) + 1;
		WRITE_ONCE(size, r);
	}
	return r;
}
EXPORT_SYMBOL(stfle_size);
