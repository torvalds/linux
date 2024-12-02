/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
 */

#ifndef _ASM_MIPS_BOARDS_SIM_H
#define _ASM_MIPS_BOARDS_SIM_H

#define STATS_ON	1
#define STATS_OFF	2
#define STATS_CLEAR	3
#define STATS_DUMP	4
#define TRACE_ON		5
#define TRACE_OFF	6


#define simcfg(code)						\
({					   \
	__asm__	 __volatile__( \
	"sltiu $0,$0, %0" \
		::"i"(code)					\
		); \
})



#endif
