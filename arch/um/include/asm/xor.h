/* SPDX-License-Identifier: GPL-2.0 */
#include <asm-generic/xor.h>
#include <shared/timer-internal.h>

/* pick an arbitrary one - measuring isn't possible with inf-cpu */
#define XOR_SELECT_TEMPLATE(x)	\
	(time_travel_mode == TT_MODE_INFCPU ? &xor_block_8regs : NULL)
