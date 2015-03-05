/*
 * Copyright (C) 2010, 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_osk_misc.c
 * Implementation of the OS abstraction layer for the UMP kernel device driver
 */


#include "ump_osk.h"

#include <linux/kernel.h>
#include "ump_kernel_linux.h"

/* is called from ump_kernel_constructor in common code */
_mali_osk_errcode_t _ump_osk_init(void)
{
	if (0 != ump_kernel_device_initialize()) {
		return _MALI_OSK_ERR_FAULT;
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _ump_osk_term(void)
{
	ump_kernel_device_terminate();
	return _MALI_OSK_ERR_OK;
}
