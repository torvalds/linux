// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/odm_precomp.h"
/*  ODM IO Relative API. */

/*  ODM Memory relative API. */
s32 ODM_CompareMemory(struct odm_dm_struct *pDM_Odm, void *pBuf1, void *pBuf2, u32 length)
{
	return !memcmp(pBuf1, pBuf2, length);
}

/*  ODM Timer relative API. */
void ODM_delay_ms(u32 ms)
{
	mdelay(ms);
}

void ODM_delay_us(u32 us)
{
	udelay(us);
}

void ODM_sleep_ms(u32 ms)
{
	msleep(ms);
}
