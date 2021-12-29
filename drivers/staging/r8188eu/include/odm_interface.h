/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef	__ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__

/*  2012/02/17 MH For non-MP compile pass only. Linux does not support workitem. */
/*  Suggest HW team to use thread instead of workitem. Windows also support the feature. */
typedef void (*RT_WORKITEM_CALL_BACK)(void *pContext);

/*  =========== Extern Variable ??? It should be forbidden. */

/*  =========== EXtern Function Prototype */

/*  Memory Relative Function. */
s32 ODM_CompareMemory(struct odm_dm_struct *pDM_Odm, void *pBuf1, void *pBuf2,
		      u32 length);

/*  ODM Timer relative API. */
void ODM_delay_ms(u32 ms);

void ODM_delay_us(u32 us);

void ODM_sleep_ms(u32 ms);

#endif	/*  __ODM_INTERFACE_H__ */
