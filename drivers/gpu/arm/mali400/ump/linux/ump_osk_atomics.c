/*
 * Copyright (C) 2010, 2014, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_osk_atomics.c
 * Implementation of the OS abstraction layer for the UMP kernel device driver
 */

#include "ump_osk.h"
#include <asm/atomic.h>

int _ump_osk_atomic_dec_and_read(_mali_osk_atomic_t *atom)
{
	return atomic_dec_return((atomic_t *)&atom->u.val);
}

int _ump_osk_atomic_inc_and_read(_mali_osk_atomic_t *atom)
{
	return atomic_inc_return((atomic_t *)&atom->u.val);
}
