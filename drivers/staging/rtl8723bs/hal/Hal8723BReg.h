/* SPDX-License-Identifier: GPL-2.0 */
/*****************************************************************************
 *Copyright(c) 2009,  RealTEK Technology Inc. All Right Reserved.
 *
 * Module:	__INC_HAL8723BREG_H
 *
 *
 * Note:	1. Define Mac register address and corresponding bit mask map
 *
 *
 * Export:	Constants, macro, functions(API), global variables(None).
 *
 * Abbrev:
 *
 * History:
 *	Data		Who		Remark
 *
 *****************************************************************************/
#ifndef __INC_HAL8723BREG_H
#define __INC_HAL8723BREG_H

/*  */
/*  */
/*	0x0100h ~ 0x01FFh	MACTOP General Configuration */
/*  */
/*  */
#define REG_C2HEVT_CMD_SEQ_88XX			0x01A1
#define REG_C2HEVT_CMD_LEN_88XX			0x01AE

/*  */
/*  */
/*	0x0200h ~ 0x027Fh	TXDMA Configuration */
/*  */
/*  */
#define REG_DWBCN1_CTRL_8723B			0x0228

/*  spec version 11 */
/*  */
/*  */
/*	0x0400h ~ 0x047Fh	Protocol Configuration */
/*  */
/*  */
#define REG_FWHW_TXQ_CTRL_8723B			0x0420
#define REG_ARFR0_8723B				0x0444
#define REG_ARFR1_8723B				0x044C
#define REG_CCK_CHECK_8723B			0x0454
#define REG_AMPDU_MAX_TIME_8723B		0x0456

#define REG_AMPDU_MAX_LENGTH_8723B		0x0458
#define REG_DATA_SC_8723B			0x0483
#define REG_MAX_AGGR_NUM_8723B			0x04CA

/*  */
/*  */
/*	0x0500h ~ 0x05FFh	EDCA Configuration */
/*  */
/*  */
#define REG_PIFS_8723B				0x0512

/*	0x0600h ~ 0x07FFh	WMAC Configuration */
#define REG_RX_PKT_LIMIT_8723B			0x060C

#define REG_TRXPTCL_CTL_8723B			0x0668

#endif /*  #ifndef __INC_HAL8723BREG_H */
