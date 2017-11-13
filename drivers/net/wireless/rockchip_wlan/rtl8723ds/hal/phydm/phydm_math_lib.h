/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/


#ifndef	__PHYDM_MATH_LIB_H__
#define    __PHYDM_MATH_LIB_H__

#define AUTO_MATH_LIB_VERSION	"1.0"		/* 2017.06.06*/


/* 1 ============================================================
 * 1  Definition
 * 1 ============================================================ */




/* 1 ============================================================
 * 1  enumeration
 * 1 ============================================================ */



/* 1 ============================================================
 * 1  structure
 * 1 ============================================================ */


/* 1 ============================================================
 * 1  function prototype
 * 1 ============================================================ */

s32
odm_pwdb_conversion(
	s32 X,
	u32 total_bit,
	u32 decimal_bit
);

s32
odm_sign_conversion(
	s32 value,
	u32 total_bit
);

void
phydm_seq_sorting(
	void	*p_dm_void,
	u32	*p_value,
	u32	*rank_idx,
	u32	*p_idx_out,
	u8	seq_length
);

u32 
odm_convert_to_db(
	u32	 value
);

u32
odm_convert_to_linear(
	u32	value
);

#endif
