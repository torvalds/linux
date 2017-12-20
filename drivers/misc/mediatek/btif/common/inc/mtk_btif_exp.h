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

#ifndef _MTK_BTIF_EXP_H_
#define _MTK_BTIF_EXP_H_

/*--------------marco defination---------------*/
#define BTIF_MAX_LEN_PER_PKT 2048
#define BTIF_RXD_BE_BLOCKED_DETECT 1
/*--------------Enum Defination---------------*/
typedef enum _ENUM_BTIF_DPIDLE_ {
	BTIF_DPIDLE_DISABLE = 0,
	BTIF_DPIDLE_ENABLE = BTIF_DPIDLE_DISABLE + 1,
	BTIF_DPIDLE_MAX,
} ENUM_BTIF_DPIDLE_CTRL;

typedef enum _ENUM_BTIF_LPBK_MODE_ {
	BTIF_LPBK_DISABLE = 0,
	BTIF_LPBK_ENABLE = BTIF_LPBK_DISABLE + 1,
	BTIF_LPBK_MAX,
} ENUM_BTIF_LPBK_MODE;

typedef enum _ENUM_BTIF_DBG_ID_ {
	BTIF_DISABLE_LOGGER = 0,
	BTIF_ENABLE_LOGGER = BTIF_DISABLE_LOGGER + 1,
	BTIF_DUMP_LOG = BTIF_ENABLE_LOGGER + 1,
	BTIF_CLR_LOG = BTIF_DUMP_LOG + 1,
	BTIF_DUMP_BTIF_REG = BTIF_CLR_LOG + 1,
	BTIF_ENABLE_RT_LOG = BTIF_DUMP_BTIF_REG + 1,
	BTIF_DISABLE_RT_LOG = BTIF_ENABLE_RT_LOG + 1,
	BTIF_DBG_MAX,
} ENUM_BTIF_DBG_ID;

typedef enum _ENUM_BTIF_OP_ERROR_CODE_ {
	E_BTIF_AGAIN = 0,
	E_BTIF_FAIL = -1,
	E_BTIF_BAD_POINTER = -2,
	E_BTIF_NO_SPACE = -3,
	E_BTIF_INTR = -4,
	E_BTIF_INVAL_PARAM = -5,
	E_BTIF_ALREADY_OPEN = -6,
	E_BTIF_NOT_OPEN = -7,
	E_BTIF_INVAL_STATE = -8,
} ENUM_BTIF_OP_ERROR_CODE;

/*--------------End of Enum Defination---------------*/

/*--------------Type Definition---------------*/

typedef int (*MTK_WCN_BTIF_RX_CB) (const unsigned char *p_buf,
				   unsigned int len);

/*--------------End of Type Definition---------------*/

/*--------------Normal Mode API declearation---------------*/

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_open
* DESCRIPTION
*  open BTIF interface, will do BTIF module HW and SW initialization
* PARAMETERS
*  p_owner      [IN] pointer to owner who call this API,
*                    currently there are 2 owner ("stp" or "btif_tester")
*                    may use this module
*                    user's id string must be less than 32 bytes
*  for "stp", BTIF will call rx callback function to route rx data to STP module
*  for "stp_tester", BTIF will save rx data
*  and wait for native process to access
*  p_id            [IN] BTIF's user id will be put to this address
* RETURNS
*  int 0 = succeed; others = fail, for detailed information,
*      please see ENUM_BTIF_OP_ERROR_CODE
*      if open success, value p_id will be the only identifier for
*      user to access BTIF's other operations
*      including read/write/dpidle_ctrl/rx_cb_retister
*      this user id is only an identifier used for owner identification
*****************************************************************************/
int mtk_wcn_btif_open(char *p_owner, unsigned long *p_id);
//EXPORT_SYMBOL(mtk_wcn_btif_open)
/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_close
* DESCRIPTION
*  close BTIF interface, will do BTIF module HW and SW de-initialization
*  once this API is called, p_btif should never be used by BTIF's user again
* PARAMETERS
*  u_id        [IN] BTIF's user id
* RETURNS
*  int         0 = succeed;
*  others = fail, for detailed information, please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_close(unsigned long u_id);
//EXPORT_SYMBOL(mtk_wcn_btif_close)
/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_write
* DESCRIPTION
*  send data throuth BTIF module
*  there's no internal buffer to cache STP data in BTIF driver,
*  if in DMA mode
*  btif driver will check if there's enough space
*  in vFIFO for data to send in DMA mode
*    if yes, put data to vFIFO and return corresponding data length to caller
*    if no, corresponding error code will be returned to called
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
*  p_buf       [IN] pointer to target data to send
*  len         [IN] data length (should be less than 2014 bytes per STP package)
*
*  if in non-DMA mode, BTIF driver will try to write to THR of BTIF controller
*      if btif driver detected that no space is available in Tx FIFO,
*      will return E_BTIF_NO_SPACE,
*      mostly something is wrong with BTIF or consys when this
*       return value is returned
* RETURNS
*  int          positive: data length send through BTIF;
*        negative: please see ENUM_BTIF_OP_ERROR_CODE
*        E_BTIF_AGAIN (0) will be returned to caller if btif does not have
*        enough vFIFO to send data, when caller get 0,
*        he should wait for a moment (5~10ms maybe) and
*        try a few times (maybe 10~20)
*            if still get E_BTIF_AGAIN, should call BTIF's debug API and
*        dump BTIF driver and BTIF/DMA register information to kernel log
*        for debug
*        E_BTIF_BAD_POINTER will be returned to caller if btif is not
*        opened successfully before call this API
*        E_BTIF_INVAL_PARAM will be returned if parameter is not valid

*****************************************************************************/
int mtk_wcn_btif_write(unsigned long u_id,
		       const unsigned char *p_buf, unsigned int len);
//EXPORT_SYMBOL(mtk_wcn_btif_write)
/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_read
* DESCRIPTION
*  read data from BTIF module
* PARAMETERS
*  p_btif     [IN] pointer returned by mtk_wcn_btif_open
*  p_buf      [IN/OUT] pointer to buffer where rx data will be put
*  max_len    [IN] max buffer length
* RETURNS
*  int          positive: data length read from BTIF;
*  negative: please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_read(unsigned long u_id,
		      unsigned char *p_buf, unsigned int max_len);
//EXPORT_SYMBOL(mtk_wcn_btif_read)
/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_dpidle_ctrl
* DESCRIPTION
*  control if BTIF module allow system enter deepidle state or not
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
*  en_flag    [IN] one of ENUM_BTIF_DPIDLE_CTRL
* RETURNS
*  int          always return 0
*****************************************************************************/
int mtk_wcn_btif_dpidle_ctrl(unsigned long u_id, ENUM_BTIF_DPIDLE_CTRL en_flag);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_rx_cb_register
* DESCRIPTION
*  register rx callback function to BTIF module by btif user
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
*  rx_cb       [IN] pointer to stp rx handler callback function,
*  should be comply with MTK_WCN_BTIF_RX_CB
* RETURNS
*  int          0 = succeed;
*  others = fail, for detailed information, please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_rx_cb_register(unsigned long u_id, MTK_WCN_BTIF_RX_CB rx_cb);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_wakeup_consys
* DESCRIPTION
*  once sleep command is sent to con sys,
*  should call this API before send wakeup command to
*  make con sys aware host want to send data to consys
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
* RETURNS
*  int          0 = succeed;
*  others = fail, for detailed information, please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_wakeup_consys(unsigned long u_id);

/*--------------End of Normal Mode API declearation----------------*/

/*--------------Debug Purpose API declearation----------------*/

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_loopback_ctrl
* DESCRIPTION
*  enable/disable BTIF internal loopback function,
*  when this function is enabled,
*  data send to btif will be received by btif itself
*  only for debug purpose, should never use this function in normal mode
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
*  enable     [IN] loopback mode control flag, enable or disable,
*  shou be one of ENUM_BTIF_LPBK_MODE
* RETURNS
*  int          0 = succeed;
*  others = fail, for detailed information, please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_loopback_ctrl(unsigned long u_id, ENUM_BTIF_LPBK_MODE enable);
//EXPORT_SYMBOL(mtk_wcn_btif_loopback_ctrl)
/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_logger_ctrl
* DESCRIPTION
*  control BTIF logger function's behavior
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
*  flag         [IN] should be one of ENUM_BTIF_DBG_ID
*                      BTIF_DISABLE_LOGGER  - disable btif logger
*                      BTIF_ENABLE_LOGGER   - enable btif logger
*                      BTIF_DUMP_LOG           - dump log logged by btif
*                      BTIF_CLR_LOG             - clear btif log buffer
*                      BTIF_DUMP_BTIF_REG   - dump btif controller's register
*                      BTIF_DUMP_DMA_REG   - dump DMA controller's register
* RETURNS
*  int   0 = succeed;
*        others = fail, for detailed information,
*        please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_dbg_ctrl(unsigned long u_id, ENUM_BTIF_DBG_ID flag);
//EXPORT_SYMBOL(mtk_wcn_btif_dbg_ctrl);
/*-----------End of Debug Purpose API declearation------------*/

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_parser_wmt_evt
* DESCRIPTION
*  parser wmt sleep/wakeup evt in btif bbs buffer for debug
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
*  sub_str     [IN] the str to be parsered
*  str_len     [IN] the length of sub_str
* RETURNS
*  bool  true = succeed;
*        false = fail;
*****************************************************************************/
bool mtk_wcn_btif_parser_wmt_evt(unsigned long u_id,
	const char *sub_str, unsigned int str_len);

int mtk_btif_exp_open_test(void);
int mtk_btif_exp_close_test(void);
int mtk_btif_exp_write_test(void);
int mtk_btif_exp_suspend_test(void);
int mtk_btif_exp_resume_test(void);
int mtk_btif_exp_enter_dpidle_test(void);
int mtk_btif_exp_exit_dpidle_test(void);
int mtk_btif_exp_write_stress_test(unsigned int length, unsigned int loop);
int mtk_btif_exp_log_debug_test(int flag);
int mtk_btif_exp_restore_noirq_test(void);
int btif_wakeup_consys_no_id(void);
int mtk_btif_exp_clock_ctrl(int en);
#if BTIF_RXD_BE_BLOCKED_DETECT
int mtk_btif_rxd_be_blocked_flag_get(void);
#endif
void mtk_btif_read_cpu_sw_rst_debug_exp(void);
#endif /*_MTK_BTIF_EXP_H_*/
