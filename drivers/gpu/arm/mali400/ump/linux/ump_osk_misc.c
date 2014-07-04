/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2010, 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_osk_misc.c
 * Implementation of the OS abstraction layer for the UMP kernel device driver
 */


#include "ump_osk.h"

#include <linux/kernel.h>
#include "ump_kernel_linux.h"

/* is called from ump_kernel_constructor in common code */
_mali_osk_errcode_t _ump_osk_init( void )
{
	if (0 != ump_kernel_device_initialize()) {
		return _MALI_OSK_ERR_FAULT;
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _ump_osk_term( void )
{
	ump_kernel_device_terminate();
	return _MALI_OSK_ERR_OK;
}
