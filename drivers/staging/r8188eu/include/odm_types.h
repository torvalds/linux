/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __ODM_TYPES_H__
#define __ODM_TYPES_H__

/*  */
/*  Define Different SW team support */
/*  */
#define	ODM_AP			0x01	 /* BIT(0) */
#define	ODM_ADSL		0x02	/* BIT(1) */
#define	ODM_CE			0x04	/* BIT(2) */
#define	ODM_MP			0x08	/* BIT(3) */

enum HAL_STATUS {
	HAL_STATUS_SUCCESS,
	HAL_STATUS_FAILURE,
};

#define SET_TX_DESC_ANTSEL_A_88E(__ptxdesc, __value)			\
	le32p_replace_bits((__le32 *)(__ptxdesc + 8), __value, BIT(24))
#define SET_TX_DESC_ANTSEL_B_88E(__ptxdesc, __value)			\
	le32p_replace_bits((__le32 *)(__ptxdesc + 8), __value, BIT(25))
#define SET_TX_DESC_ANTSEL_C_88E(__ptxdesc, __value)			\
	le32p_replace_bits((__le32 *)(__ptxdesc + 28), __value, BIT(29))

#endif /*  __ODM_TYPES_H__ */
