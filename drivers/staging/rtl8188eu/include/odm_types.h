/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __ODM_TYPES_H__
#define __ODM_TYPES_H__

#define	ODM_CE			0x04	/* BIT2 */

enum HAL_STATUS {
	HAL_STATUS_SUCCESS,
	HAL_STATUS_FAILURE,
};

#define SET_TX_DESC_ANTSEL_A_88E(__pTxDesc, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pTxDesc + 8, 24, 1, __Value)
#define SET_TX_DESC_ANTSEL_B_88E(__pTxDesc, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pTxDesc + 8, 25, 1, __Value)
#define SET_TX_DESC_ANTSEL_C_88E(__pTxDesc, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pTxDesc + 28, 29, 1, __Value)

#endif /*  __ODM_TYPES_H__ */
