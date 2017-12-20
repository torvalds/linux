/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*! \file
 *   \brief  Declaration of library functions
 *
 *   Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

#ifndef _MTK_WCN_CMB_STUB_H_
#define _MTK_WCN_CMB_STUB_H_

#include <linux/pm.h>

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
/* Audio GPIO naming style for 73/75/77 */
/* #define MTK_WCN_CMB_AUD_IO_NAMING_STYLE_0 1 */
/* Audio GPIO naming style for 89/8135 */
/* #define MTK_WCN_CMB_AUD_IO_NAMING_STYLE_1 1 */
/* Audio GPIO naming style for 6592 */
/* #define MTK_WCN_CMB_AUD_IO_NAMING_STYLE_2 1 */
/* Audio GPIO naming style for 6595 */
#define MTK_WCN_CMB_AUD_IO_NAMING_STYLE_3 1
#define MTK_WCN_CMB_FOR_SDIO_1V_AUTOK 1

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
	CMB_STUB_AIF_0 = 0,	/* 0000: BT_PCM_OFF & FM analog (line in/out) */
	CMB_STUB_AIF_1 = 1,	/* 0001: BT_PCM_ON & FM analog (in/out) */
	CMB_STUB_AIF_2 = 2,	/* 0010: BT_PCM_OFF & FM digital (I2S) */
	CMB_STUB_AIF_3 = 3,	/* 0011: BT_PCM_ON & FM digital (I2S) (invalid in 73evb & 1.2 phone configuration) */
	CMB_STUB_AIF_4 = 4, /* 0100: BT_I2S & FM disable in special projects, e.g. protea*/
	CMB_STUB_AIF_MAX = 5,
} CMB_STUB_AIF_X;

/*COMBO_CHIP_AUDIO_PIN_CTRL*/
typedef enum {
	CMB_STUB_AIF_CTRL_DIS = 0,
	CMB_STUB_AIF_CTRL_EN = 1,
	CMB_STUB_AIF_CTRL_MAX = 2,
} CMB_STUB_AIF_CTRL;

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
	COMBO_IF_BTIF = 2,
	COMBO_IF_MAX,
} COMBO_IF;

typedef void (*wmt_bgf_eirq_cb) (void);
typedef int (*wmt_aif_ctrl_cb) (CMB_STUB_AIF_X, CMB_STUB_AIF_CTRL);
typedef void (*wmt_func_ctrl_cb) (unsigned int, unsigned int);
typedef signed long (*wmt_thermal_query_cb) (void);
typedef int (*wmt_deep_idle_ctrl_cb) (unsigned int);
typedef int (*wmt_func_do_reset) (unsigned int);

/* for DVFS driver do 1v autok */
#if MTK_WCN_CMB_FOR_SDIO_1V_AUTOK
typedef unsigned int (*wmt_get_drv_status)(unsigned int);
#endif

typedef void (*msdc_sdio_irq_handler_t) (void *);	/* external irq handler */
typedef void (*pm_callback_t) (pm_message_t state, void *data);

struct sdio_ops {
	void (*sdio_request_eirq)(msdc_sdio_irq_handler_t irq_handler, void *data);
	void (*sdio_enable_eirq)(void);
	void (*sdio_disable_eirq)(void);
	void (*sdio_register_pm)(pm_callback_t pm_cb, void *data);
};

typedef struct _CMB_STUB_CB_ {
	unsigned int size;	/* structure size */
	/*wmt_bgf_eirq_cb bgf_eirq_cb; *//* remove bgf_eirq_cb from stub. handle it in platform */
	wmt_aif_ctrl_cb aif_ctrl_cb;
	wmt_func_ctrl_cb func_ctrl_cb;
	wmt_thermal_query_cb thermal_query_cb;
	wmt_deep_idle_ctrl_cb deep_idle_ctrl_cb;
	wmt_func_do_reset wmt_do_reset_cb;
#if MTK_WCN_CMB_FOR_SDIO_1V_AUTOK
	wmt_get_drv_status get_drv_status_cb;
#endif
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

extern struct sdio_ops mt_sdio_ops[4];

extern int mtk_wcn_cmb_stub_reg(P_CMB_STUB_CB p_stub_cb);
extern int mtk_wcn_cmb_stub_unreg(void);

extern int mtk_wcn_cmb_stub_aif_ctrl(CMB_STUB_AIF_X state, CMB_STUB_AIF_CTRL ctrl);

static inline int mtk_wcn_cmb_stub_audio_ctrl(CMB_STUB_AIF_X state)
{
/* return mtk_wcn_cmb_stub_aif_ctrl(state, 1); */
	return 0;
}

extern int mt_combo_plt_enter_deep_idle(COMBO_IF src);
extern int mt_combo_plt_exit_deep_idle(COMBO_IF src);

/* Use new mtk_wcn_stub APIs instead of old mt_combo ones for kernel to control
 * function on/off.
 */
extern void mtk_wcn_cmb_stub_func_ctrl(unsigned int type, unsigned int on);
extern int mtk_wcn_cmb_stub_query_ctrl(void);
extern int board_sdio_ctrl(unsigned int sdio_port_num, unsigned int on);
extern int mtk_wcn_sdio_irq_flag_set(int falg);

#if MTK_WCN_CMB_FOR_SDIO_1V_AUTOK
extern int mtk_wcn_cmb_stub_1vautok_for_dvfs(void);
#endif

extern int mtk_wcn_wmt_chipid_query(void);
extern void mtk_wcn_wmt_set_chipid(int chipid);

/* mtk_uart_pdn_enable -- request uart port enter/exit deep idle mode, this API is defined in uart driver
 *
 * @ port - uart port name, Eg: "ttyMT0", "ttyMT1", "ttyMT2"
 * @ enable - "1", enable deep idle; "0", disable deep idle
 *
 * Return 0 if success, else -1
 */
extern unsigned int mtk_uart_pdn_enable(char *port, int enable);
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _MTK_WCN_CMB_STUB_H_ */
