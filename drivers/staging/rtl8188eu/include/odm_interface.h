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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#ifndef	__ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__

/*  */
/*  =========== Constant/Structure/Enum/... Define */
/*  */

/*  */
/*  =========== Macro Define */
/*  */

#define _reg_all(_name)			ODM_##_name
#define _reg_ic(_name, _ic)		ODM_##_name##_ic
#define _bit_all(_name)			BIT_##_name
#define _bit_ic(_name, _ic)		BIT_##_name##_ic

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

#define _cat(_name, _ic_type, _func)					\
	(								\
		(_ic_type) ? _func##_11N(_name) :			\
		_func##_11AC(_name)					\
	)

/*  _name: name of register or bit. */
/*  Example: "ODM_REG(R_A_AGC_CORE1, pDM_Odm)" */
/*         gets "ODM_R_A_AGC_CORE1" or "ODM_R_A_AGC_CORE1_8192C",
 *	   depends on SupportICType. */
#define ODM_REG(_name, _pDM_Odm) _cat(_name, _pDM_Odm->SupportICType, _reg)
#define ODM_BIT(_name, _pDM_Odm) _cat(_name, _pDM_Odm->SupportICType, _bit)

enum odm_h2c_cmd {
	ODM_H2C_RSSI_REPORT = 0,
	ODM_H2C_PSD_RESULT = 1,
	ODM_H2C_PathDiv = 2,
	ODM_MAX_H2CCMD
};

/*  2012/02/17 MH For non-MP compile pass only. Linux does not support workitem. */
/*  Suggest HW team to use thread instead of workitem. Windows also support the feature. */
typedef void (*RT_WORKITEM_CALL_BACK)(void *pContext);

/*  =========== Extern Variable ??? It should be forbidden. */

/*  =========== EXtern Function Prototype */

/*  Memory Relative Function. */

/*  ODM Timer relative API. */

void ODM_CancelTimer(struct odm_dm_struct *pDM_Odm, struct timer_list *pTimer);

/*  ODM FW relative API. */
u32 ODM_FillH2CCmd(u8 *pH2CBuffer, u32 H2CBufferLen, u32 CmdNum,
		   u32 *pElementID, u32 *pCmdLen, u8 **pCmbBuffer,
		   u8 *CmdStartSeq);

#endif	/*  __ODM_INTERFACE_H__ */
