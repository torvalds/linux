/*
 * Copyright (C) 2007-2008 Michal Simek <monstr@monstr.eu>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_SELFMOD_H
#define _ASM_MICROBLAZE_SELFMOD_H

/*
 * BARRIER_BASE_ADDR is constant address for selfmod function.
 * do not change this value - selfmod function is in
 * arch/microblaze/kernel/selfmod.c: selfmod_function()
 *
 * last 16 bits is used for storing register offset
 */

#define BARRIER_BASE_ADDR	0x1234ff00

void selfmod_function(const int *arr_fce, const unsigned int base);

#endif /* _ASM_MICROBLAZE_SELFMOD_H */
