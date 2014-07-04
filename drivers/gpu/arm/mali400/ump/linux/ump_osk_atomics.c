/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2010 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_osk_atomics.c
 * Implementation of the OS abstraction layer for the UMP kernel device driver
 */

#include "ump_osk.h"
#include <asm/atomic.h>

int _ump_osk_atomic_dec_and_read( _mali_osk_atomic_t *atom )
{
	return atomic_dec_return((atomic_t *)&atom->u.val);
}

int _ump_osk_atomic_inc_and_read( _mali_osk_atomic_t *atom )
{
	return atomic_inc_return((atomic_t *)&atom->u.val);
}
