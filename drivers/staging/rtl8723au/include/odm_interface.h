/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/


#ifndef	__ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__


/*  _cat: implemented by Token-Pasting Operator. */

/*===================================

#define ODM_REG_DIG_11N		0xC50
#define ODM_REG_DIG_11AC	0xDDD

ODM_REG(DIG,_pDM_Odm)
=====================================*/

#define _reg_11N(_name)			ODM_REG_##_name##_11N
#define _reg_11AC(_name)		ODM_REG_##_name##_11AC
#define _bit_11N(_name)			ODM_BIT_##_name##_11N
#define _bit_11AC(_name)		ODM_BIT_##_name##_11AC

#define _cat(_name, _func)					\
	(							\
		_func##_11N(_name)				\
	)

/*  _name: name of register or bit. */
/*  Example: "ODM_REG(R_A_AGC_CORE1, pDM_Odm)" */
/*         gets "ODM_R_A_AGC_CORE1" or "ODM_R_A_AGC_CORE1_8192C", depends on SupportICType. */
#define ODM_REG(_name, _pDM_Odm)	_cat(_name, _reg)
#define ODM_BIT(_name, _pDM_Odm)	_cat(_name, _bit)

/*  */
/*  2012/02/17 MH For non-MP compile pass only. Linux does not support workitem. */
/*  Suggest HW team to use thread instead of workitem. Windows also support the feature. */
/*  */
typedef void (*RT_WORKITEM_CALL_BACK)(struct work_struct *pContext);

/*  */
/*  =========== EXtern Function Prototype */
/*  */

void ODM_SetRFReg(struct dm_odm_t *pDM_Odm, enum RF_RADIO_PATH eRFPath,
		  u32 RegAddr, u32 BitMask, u32 Data);
u32 ODM_GetRFReg(struct dm_odm_t *pDM_Odm, enum RF_RADIO_PATH eRFPath,
		 u32 RegAddr, u32 BitMask);

#endif	/*  __ODM_INTERFACE_H__ */
