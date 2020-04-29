/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __ODM_TYPES_H__
#define __ODM_TYPES_H__

#include <drv_types.h>

/*  Deifne HW endian support */
#define	ODM_ENDIAN_BIG	0
#define	ODM_ENDIAN_LITTLE	1

#define GET_ODM(__padapter)	((PDM_ODM_T)(&((GET_HAL_DATA(__padapter))->odmpriv)))

typedef enum _HAL_STATUS {
	HAL_STATUS_SUCCESS,
	HAL_STATUS_FAILURE,
	/*RT_STATUS_PENDING,
	RT_STATUS_RESOURCE,
	RT_STATUS_INVALID_CONTEXT,
	RT_STATUS_INVALID_PARAMETER,
	RT_STATUS_NOT_SUPPORT,
	RT_STATUS_OS_API_FAILED,*/
} HAL_STATUS, *PHAL_STATUS;


/*  */
/*  Declare for ODM spin lock definition temporarily from compile pass. */
/*  */
typedef enum _RT_SPINLOCK_TYPE {
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
	RT_RF_STATE_SPINLOCK = 12, /*  For RF state. Added by Bruce, 2007-10-30. */
	/* Shall we define Ndis 6.2 SpinLock Here ? */
	RT_PORT_SPINLOCK = 16,
	RT_H2C_SPINLOCK = 20, /*  For H2C cmd. Added by tynli. 2009.11.09. */

	RT_BTData_SPINLOCK = 25,

	RT_WAPI_OPTION_SPINLOCK = 26,
	RT_WAPI_RX_SPINLOCK = 27,

	/*  add for 92D CCK control issue */
	RT_CCK_PAGEA_SPINLOCK = 28,
	RT_BUFFER_SPINLOCK = 29,
	RT_CHANNEL_AND_BANDWIDTH_SPINLOCK = 30,
	RT_GEN_TEMP_BUF_SPINLOCK = 31,
	RT_AWB_SPINLOCK = 32,
	RT_FW_PS_SPINLOCK = 33,
	RT_HW_TIMER_SPIN_LOCK = 34,
	RT_MPT_WI_SPINLOCK = 35,
	RT_P2P_SPIN_LOCK = 36,	/*  Protect P2P context */
	RT_DBG_SPIN_LOCK = 37,
	RT_IQK_SPINLOCK = 38,
	RT_PENDED_OID_SPINLOCK = 39,
	RT_CHNLLIST_SPINLOCK = 40,
	RT_INDIC_SPINLOCK = 41,	/* protect indication */
} RT_SPINLOCK_TYPE;

	#if defined(__LITTLE_ENDIAN)
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_LITTLE
	#else
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_BIG
	#endif

	typedef struct timer_list		RT_TIMER, *PRT_TIMER;
	typedef  void *RT_TIMER_CALL_BACK;
	#define	STA_INFO_T			struct sta_info
	#define	PSTA_INFO_T		struct sta_info *

	#define SET_TX_DESC_ANTSEL_A_88E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 24, 1, __Value)
	#define SET_TX_DESC_ANTSEL_B_88E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 25, 1, __Value)
	#define SET_TX_DESC_ANTSEL_C_88E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 29, 1, __Value)

	/* define useless flag to avoid compile warning */
	#define	USE_WORKITEM 0
	#define   FPGA_TWO_MAC_VERIFICATION	0

#define READ_NEXT_PAIR(v1, v2, i) do { if (i+2 >= ArrayLen) break; i += 2; v1 = Array[i]; v2 = Array[i+1]; } while (0)
#define COND_ELSE  2
#define COND_ENDIF 3

#endif /*  __ODM_TYPES_H__ */
