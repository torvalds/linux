/*
 * Copyright (C) 2008 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_UNALIGNED_H
#define _ASM_MICROBLAZE_UNALIGNED_H

# ifdef __KERNEL__

#  ifdef __MICROBLAZEEL__
#   include <linux/unaligned/le_struct.h>
#   include <linux/unaligned/be_byteshift.h>
#   define get_unaligned	__get_unaligned_le
#   define put_unaligned	__put_unaligned_le
#  else
#   include <linux/unaligned/be_struct.h>
#   include <linux/unaligned/le_byteshift.h>
#   define get_unaligned	__get_unaligned_be
#   define put_unaligned	__put_unaligned_be
#  endif

# include <linux/unaligned/generic.h>

# endif	/* __KERNEL__ */
#endif /* _ASM_MICROBLAZE_UNALIGNED_H */
