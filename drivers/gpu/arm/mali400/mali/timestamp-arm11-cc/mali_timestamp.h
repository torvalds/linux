/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2010-2011, 2013-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_TIMESTAMP_H__
#define __MALI_TIMESTAMP_H__

#include "mali_osk.h"

MALI_STATIC_INLINE _mali_osk_errcode_t _mali_timestamp_reset(void)
{
	/*
	 * reset counters and overflow flags
	 */

	u32 mask = (1 << 0) | /* enable all three counters */
		   (0 << 1) | /* reset both Count Registers to 0x0 */
		   (1 << 2) | /* reset the Cycle Counter Register to 0x0 */
		   (0 << 3) | /* 1 = Cycle Counter Register counts every 64th processor clock cycle */
		   (0 << 4) | /* Count Register 0 interrupt enable */
		   (0 << 5) | /* Count Register 1 interrupt enable */
		   (0 << 6) | /* Cycle Counter interrupt enable */
		   (0 << 8) | /* Count Register 0 overflow flag (clear or write, flag on read) */
		   (0 << 9) | /* Count Register 1 overflow flag (clear or write, flag on read) */
		   (1 << 10); /* Cycle Counter Register overflow flag (clear or write, flag on read) */

	__asm__ __volatile__("MCR    p15, 0, %0, c15, c12, 0" : : "r"(mask));

	return _MALI_OSK_ERR_OK;
}

MALI_STATIC_INLINE u64 _mali_timestamp_get(void)
{
	u32 result;

	/* this is for the clock cycles */
	__asm__ __volatile__("MRC    p15, 0, %0, c15, c12, 1" : "=r"(result));

	return (u64)result;
}

#endif /* __MALI_TIMESTAMP_H__ */
