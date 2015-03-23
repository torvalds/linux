/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012-2013 Cavium Inc., All Rights Reserved.
 *
 * MD5 instruction definitions added by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 */
#ifndef __LINUX_OCTEON_CRYPTO_H
#define __LINUX_OCTEON_CRYPTO_H

#include <linux/sched.h>
#include <asm/mipsregs.h>

#define OCTEON_CR_OPCODE_PRIORITY 300

extern unsigned long octeon_crypto_enable(struct octeon_cop2_state *state);
extern void octeon_crypto_disable(struct octeon_cop2_state *state,
				  unsigned long flags);

/*
 * Macros needed to implement MD5:
 */

/*
 * The index can be 0-1.
 */
#define write_octeon_64bit_hash_dword(value, index)	\
do {							\
	__asm__ __volatile__ (				\
	"dmtc2 %[rt],0x0048+" STR(index)		\
	:						\
	: [rt] "d" (value));				\
} while (0)

/*
 * The index can be 0-1.
 */
#define read_octeon_64bit_hash_dword(index)		\
({							\
	u64 __value;					\
							\
	__asm__ __volatile__ (				\
	"dmfc2 %[rt],0x0048+" STR(index)		\
	: [rt] "=d" (__value)				\
	: );						\
							\
	__value;					\
})

/*
 * The index can be 0-6.
 */
#define write_octeon_64bit_block_dword(value, index)	\
do {							\
	__asm__ __volatile__ (				\
	"dmtc2 %[rt],0x0040+" STR(index)		\
	:						\
	: [rt] "d" (value));				\
} while (0)

/*
 * The value is the final block dword (64-bit).
 */
#define octeon_md5_start(value)				\
do {							\
	__asm__ __volatile__ (				\
	"dmtc2 %[rt],0x4047"				\
	:						\
	: [rt] "d" (value));				\
} while (0)

#endif /* __LINUX_OCTEON_CRYPTO_H */
