/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_BITOPS_H
#define _ASM_MICROBLAZE_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

#include <asm/byteorder.h> /* swab32 */
#include <asm/system.h> /* save_flags */

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()
#include <asm-generic/bitops.h>
#include <asm-generic/bitops/__fls.h>

#endif /* _ASM_MICROBLAZE_BITOPS_H */
