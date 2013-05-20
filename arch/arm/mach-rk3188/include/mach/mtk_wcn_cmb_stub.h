/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

/*******************************************************************************
* Copyright (c) 2009 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

#ifndef _MTK_WCN_CMB_STUB_H_
#define _MTK_WCN_CMB_STUB_H_

#include <linux/types.h>
	typedef enum {
		COMBO_AUDIO_STATE_0 = 0, /* 0000: BT_PCM_OFF & FM analog (line in/out) */
		COMBO_AUDIO_STATE_1 = 1, /* 0001: BT_PCM_ON & FM analog (in/out) */
		COMBO_AUDIO_STATE_2 = 2, /* 0010: BT_PCM_OFF & FM digital (I2S) */
		COMBO_AUDIO_STATE_3 = 3, /* 0011: BT_PCM_ON & FM digital (I2S) (invalid in 73evb & 1.2 phone configuration) */
		COMBO_AUDIO_STATE_MAX = 4,
	} COMBO_AUDIO_STATE;
	
	typedef enum {
		COMBO_FUNC_TYPE_BT = 0,
		COMBO_FUNC_TYPE_FM = 1,
		COMBO_FUNC_TYPE_GPS = 2,
		COMBO_FUNC_TYPE_WIFI = 3,
		COMBO_FUNC_TYPE_WMT = 4,
		COMBO_FUNC_TYPE_STP = 5,
		COMBO_FUNC_TYPE_NUM = 6
	} COMBO_FUNC_TYPE;
	
	typedef enum {
		COMBO_IF_UART = 0,
		COMBO_IF_MSDC = 1,
		COMBO_IF_MAX,
	} COMBO_IF;
	
	
	/******************************************************************************
	*					F U N C T I O N   D E C L A R A T I O N S
	*******************************************************************************
	*/
	
	
	/* [GeorgeKuo] Stub functions for other kernel built-in modules to call.
	 * Keep them unchanged temporarily. Move mt_combo functions to mtk_wcn_combo.
	 */
	//extern int mt_combo_audio_ctrl_ex(COMBO_AUDIO_STATE state, u32 clt_ctrl);
	static inline int mt_combo_audio_ctrl(COMBO_AUDIO_STATE state) {
		//return mt_combo_audio_ctrl_ex(state, 1);
		return 0;
	}
	//extern int mt_combo_plt_enter_deep_idle(COMBO_IF src);
	//extern int mt_combo_plt_exit_deep_idle(COMBO_IF src);
	
	/* Use new mtk_wcn_stub APIs instead of old mt_combo ones for kernel to control
	 * function on/off.
	 */
	//extern void mtk_wcn_cmb_stub_func_ctrl (unsigned int type, unsigned int on);
	//extern int board_sdio_ctrl (unsigned int sdio_port_num, unsigned int on);
//#include <mach/mt_combo.h> jake 
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/



/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/



/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum {
    CMB_STUB_AIF_0 = 0, /* 0000: BT_PCM_OFF & FM analog (line in/out) */
    CMB_STUB_AIF_1 = 1, /* 0001: BT_PCM_ON & FM analog (in/out) */
    CMB_STUB_AIF_2 = 2, /* 0010: BT_PCM_OFF & FM digital (I2S) */
    CMB_STUB_AIF_3 = 3, /* 0011: BT_PCM_ON & FM digital (I2S) (invalid in 73evb & 1.2 phone configuration) */
    CMB_STUB_AIF_MAX = 4,
} CMB_STUB_AIF_X;

/*COMBO_CHIP_AUDIO_PIN_CTRL*/
typedef enum {
    CMB_STUB_AIF_CTRL_DIS = 0,
    CMB_STUB_AIF_CTRL_EN = 1,
    CMB_STUB_AIF_CTRL_MAX = 2,
} CMB_STUB_AIF_CTRL;

typedef void (*wmt_bgf_eirq_cb)(void);
typedef int (*wmt_aif_ctrl_cb)(CMB_STUB_AIF_X, CMB_STUB_AIF_CTRL);
typedef void (*wmt_func_ctrl_cb)(unsigned int, unsigned int);

typedef struct _CMB_STUB_CB_ {
    unsigned int size; //structure size
    /*wmt_bgf_eirq_cb bgf_eirq_cb;*//* remove bgf_eirq_cb from stub. handle it in platform */
    wmt_aif_ctrl_cb aif_ctrl_cb;
    wmt_func_ctrl_cb func_ctrl_cb;
} CMB_STUB_CB, *P_CMB_STUB_CB;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/



/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/





/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

extern int mtk_wcn_cmb_stub_reg (P_CMB_STUB_CB p_stub_cb);
extern int mtk_wcn_cmb_stub_unreg (void);

extern int mtk_wcn_cmb_stub_aif_ctrl (CMB_STUB_AIF_X state, CMB_STUB_AIF_CTRL ctrl);

// TODO: [FixMe][GeorgeKuo]: put prototypes into mt_combo.h for board.c temporarily for non-finished porting
// TODO: old: rfkill->board.c->mt_combo->wmt_lib_plat
// TODO: new: rfkill->mtk_wcn_cmb_stub_alps->wmt_plat_alps
#if 0
extern int mtk_wcn_cmb_stub_func_ctrl(unsigned int type, unsigned int on);
#endif

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/


#endif /* _MTK_WCN_CMB_STUB_H_ */






