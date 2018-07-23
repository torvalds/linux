/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __ODM_TYPES_H__
#define __ODM_TYPES_H__

/*Define Different SW team support*/
#define ODM_AP 0x01 /*BIT0*/
#define ODM_CE 0x04 /*BIT2*/
#define ODM_WIN 0x08 /*BIT3*/
#define ODM_ADSL 0x10 /*BIT4*/
#define ODM_IOT 0x20 /*BIT5*/

/*Deifne HW endian support*/
#define ODM_ENDIAN_BIG 0
#define ODM_ENDIAN_LITTLE 1

#define GET_PDM_ODM(__padapter)                                                \
	((struct phy_dm_struct *)(&(GET_HAL_DATA(__padapter))->odmpriv))

enum hal_status {
	HAL_STATUS_SUCCESS,
	HAL_STATUS_FAILURE,
};

/*
 * Declare for ODM spin lock definition temporarily fro compile pass.
 */
enum rt_spinlock_type {
	RT_TX_SPINLOCK = 1,
	RT_RX_SPINLOCK = 2,
	RT_RM_SPINLOCK = 3,
	RT_CAM_SPINLOCK = 4,
	RT_SCAN_SPINLOCK = 5,
	RT_LOG_SPINLOCK = 7,
	RT_BW_SPINLOCK = 8,
	RT_CHNLOP_SPINLOCK = 9,
	RT_RF_OPERATE_SPINLOCK = 10,
	RT_INITIAL_SPINLOCK = 11,
	RT_RF_STATE_SPINLOCK =
		12, /* For RF state. Added by Bruce, 2007-10-30. */
	/* Shall we define Ndis 6.2 SpinLock Here ? */
	RT_PORT_SPINLOCK = 16,
	RT_VNIC_SPINLOCK = 17,
	RT_HVL_SPINLOCK = 18,
	RT_H2C_SPINLOCK = 20, /* For H2C cmd. Added by tynli. 2009.11.09. */

	rt_bt_data_spinlock = 25,

	RT_WAPI_OPTION_SPINLOCK = 26,
	RT_WAPI_RX_SPINLOCK = 27,

	/* add for 92D CCK control issue */
	RT_CCK_PAGEA_SPINLOCK = 28,
	RT_BUFFER_SPINLOCK = 29,
	RT_CHANNEL_AND_BANDWIDTH_SPINLOCK = 30,
	RT_GEN_TEMP_BUF_SPINLOCK = 31,
	RT_AWB_SPINLOCK = 32,
	RT_FW_PS_SPINLOCK = 33,
	RT_HW_TIMER_SPIN_LOCK = 34,
	RT_MPT_WI_SPINLOCK = 35,
	RT_P2P_SPIN_LOCK = 36, /* Protect P2P context */
	RT_DBG_SPIN_LOCK = 37,
	RT_IQK_SPINLOCK = 38,
	RT_PENDED_OID_SPINLOCK = 39,
	RT_CHNLLIST_SPINLOCK = 40,
	RT_INDIC_SPINLOCK = 41, /* protect indication */
	RT_RFD_SPINLOCK = 42,
	RT_SYNC_IO_CNT_SPINLOCK = 43,
	RT_LAST_SPINLOCK,
};

#include <asm/byteorder.h>

#if defined(__LITTLE_ENDIAN)
#define ODM_ENDIAN_TYPE ODM_ENDIAN_LITTLE
#elif defined(__BIG_ENDIAN)
#define ODM_ENDIAN_TYPE ODM_ENDIAN_BIG
#else
#error
#endif

#define COND_ELSE 2
#define COND_ENDIF 3

#define MASKBYTE0 0xff
#define MASKBYTE1 0xff00
#define MASKBYTE2 0xff0000
#define MASKBYTE3 0xff000000
#define MASKHWORD 0xffff0000
#define MASKLWORD 0x0000ffff
#define MASKDWORD 0xffffffff
#define MASK7BITS 0x7f
#define MASK12BITS 0xfff
#define MASKH4BITS 0xf0000000
#define MASK20BITS 0xfffff
#define MASKOFDM_D 0xffc00000
#define MASKCCK 0x3f3f3f3f
#define RFREGOFFSETMASK 0xfffff
#define MASKH3BYTES 0xffffff00
#define MASKL3BYTES 0x00ffffff
#define MASKBYTE2HIGHNIBBLE 0x00f00000
#define MASKBYTE3LOWNIBBLE 0x0f000000
#define MASKL3BYTES 0x00ffffff
#define RFREGOFFSETMASK 0xfffff

#include "phydm_features.h"

#endif /* __ODM_TYPES_H__ */
