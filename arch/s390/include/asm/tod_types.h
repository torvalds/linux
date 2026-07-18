/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_TOD_TYPES_H
#define _ASM_S390_TOD_TYPES_H

#include <linux/types.h>

#ifndef __ASSEMBLER__

union tod_clock {
	__uint128_t val;
	struct {
		__uint128_t ei	:  8; /* epoch index */
		__uint128_t tod : 64; /* bits 0-63 of tod clock */
		__uint128_t	: 40;
		__uint128_t pf	: 16; /* programmable field */
	};
	struct {
		__uint128_t eitod : 72; /* epoch index + bits 0-63 tod clock */
		__uint128_t	  : 56;
	};
	struct {
		__uint128_t us	: 60; /* micro-seconds */
		__uint128_t sus	: 12; /* sub-microseconds */
		__uint128_t	: 56;
	};
} __packed;

#endif
#endif
