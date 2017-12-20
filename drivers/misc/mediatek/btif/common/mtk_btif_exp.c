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

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG "MTK-BTIF-EXP"

/*#include "mtk_btif_exp.h"*/
#include "mtk_btif.h"

/*---------------------------------Function----------------------------------*/

p_mtk_btif btif_exp_srh_id(unsigned long u_id)
{
	int index = 0;
	p_mtk_btif p_btif = NULL;
	struct list_head *p_list = NULL;
	struct list_head *tmp = NULL;
	p_mtk_btif_user p_user = NULL;

	for (index = 0; (index < BTIF_PORT_NR) && (p_btif == NULL); index++) {
		p_list = &(g_btif[index].user_list);
		list_for_each(tmp, p_list) {
			p_user = container_of(tmp, mtk_btif_user, entry);
			if (u_id == p_user->u_id) {
				p_btif = p_user->p_btif;
				BTIF_DBG_FUNC
				    ("BTIF's user id(0x%p), p_btif(0x%p)\n",
				     p_user->u_id, p_btif);
				break;
			}
		}
	}
	if (p_btif == NULL) {
		BTIF_INFO_FUNC
		    ("no btif structure found for BTIF's user id(0x%lx)\n",
		     u_id);
	}
	return p_btif;
}

/*-----Normal Mode API declearation-------*/

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_open
* DESCRIPTION
*  open BTIF interface, will do BTIF module HW and SW initialization
* PARAMETERS
*  p_owner      [IN] pointer to owner who call this API,
*                    currently there are 2 owner ("stp" or "btif_tester")
*                    may use this module
*  for "stp", BTIF will call rx callback function to route rx data to STP module
*  for "stp_tester", BTIF will save rx data and wait for native process to access
*  p_id            [IN] BTIF's user id will be put to this address
* RETURNS
*  int  0 = BTIF module initialization fail; negative = BTIF module initialization success
*       if open success, value p_id will be the only identifier
*       for user to access BTIF's other operations
*       including read/write/dpidle_ctrl/rx_cb_retister
*       this user id is only an identifier used for owner identification
*****************************************************************************/
int mtk_wcn_btif_open(char *p_owner, unsigned long *p_id)
{
	int i_ret = -1;
	unsigned int index = 0;
	p_mtk_btif_user p_new_user = NULL;
	p_mtk_btif p_btif = &g_btif[index];
	struct list_head *p_user_list = &(p_btif->user_list);

	BTIF_DBG_FUNC("++");
	BTIF_DBG_FUNC("p_btif(0x%p)\n", p_btif);

	if (mutex_lock_killable(&(p_btif->ops_mtx))) {
		BTIF_ERR_FUNC("mutex_lock_killable return failed\n");
		return E_BTIF_INTR;
	}
	if ((p_owner == NULL) || (p_id == NULL)) {
		if (p_id)
			*p_id = 0;
		BTIF_ERR_FUNC("parameter invalid, p_owner(0x%p), p_id(0x%p)\n",
			      p_owner, p_id);
		BTIF_MUTEX_UNLOCK(&(p_btif->ops_mtx));
		return E_BTIF_INVAL_PARAM;
	}

/*check if btif is already opened or not, if yes, just return fail*/
	if (!list_empty(p_user_list)) {
		struct list_head *pos;
		p_mtk_btif_user p_user;

		BTIF_ERR_FUNC("BTIF's user list is not empty\n");
		list_for_each(pos, p_user_list) {
			p_user = container_of(pos, mtk_btif_user, entry);
			BTIF_INFO_FUNC("BTIF's user id(0x%lx), name(%s)\n",
				       p_user->u_id, p_user->u_name);
		}
/*leave p_id alone*/
		BTIF_MUTEX_UNLOCK(&(p_btif->ops_mtx));
		return E_BTIF_ALREADY_OPEN;
	}
	p_new_user = vmalloc(sizeof(mtk_btif_user));

	if (p_new_user != NULL) {
		INIT_LIST_HEAD(&(p_new_user->entry));
		p_new_user->enable = false;
		p_new_user->p_btif = p_btif;
		p_new_user->u_id = (unsigned long)p_new_user;
		strncpy(p_new_user->u_name, p_owner, sizeof(p_new_user->u_name) - 1);
		p_new_user->u_name[sizeof(p_new_user->u_name) - 1] = '\0';
		BTIF_DBG_FUNC("owner name:%s, recorded name:%s\n",
			       p_owner, p_new_user->u_name);

		i_ret = btif_open(p_btif);
		if (i_ret) {
			BTIF_ERR_FUNC("btif_open failed, i_ret(%d)\n", i_ret);
			*p_id = 0;
/*free btif new user's structure*/
			vfree(p_new_user);
			p_new_user = NULL;
		} else {
			BTIF_INFO_FUNC("btif_open succeed\n");
			*p_id = p_new_user->u_id;
/*mark enable flag to true*/
			p_new_user->enable = true;
/*add to uer lsit*/
			list_add_tail(&(p_new_user->entry), p_user_list);
		}
	} else {
		*p_id = 0;
		i_ret = -ENOMEM;
		BTIF_ERR_FUNC("allocate memory for mtk_btif_user failed\n");
	}
	BTIF_MUTEX_UNLOCK(&(p_btif->ops_mtx));
	BTIF_DBG_FUNC("--");
	return i_ret;
}
EXPORT_SYMBOL(mtk_wcn_btif_open);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_close
* DESCRIPTION
*  close BTIF interface, will do BTIF module HW and SW de-initialization
*  once this API is called, p_btif should never be used by BTIF's user again
* PARAMETERS
*  u_id        [IN] BTIF's user id
* RETURNS
*  int     0 = succeed;
*          others = fail,
*          for detailed information, please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_close(unsigned long u_id)
{
	int i_ret = -1;
	p_mtk_btif p_btif = NULL;
	struct list_head *pos = NULL;
	struct list_head *p_user_list = NULL;

	BTIF_DBG_FUNC("++");
	p_btif = btif_exp_srh_id(u_id);

	if (p_btif == NULL)
		return E_BTIF_INVAL_PARAM;

	if (mutex_lock_killable(&(p_btif->ops_mtx))) {
		BTIF_ERR_FUNC("mutex_lock_killable return failed\n");
		return E_BTIF_INTR;
	}
	p_user_list = &(p_btif->user_list);
	list_for_each(pos, p_user_list) {
		p_mtk_btif_user p_user =
		    container_of(pos, mtk_btif_user, entry);

		if (p_user->u_id == u_id) {
			BTIF_INFO_FUNC
			    ("user who's id is 0x%lx deleted from user list\n",
			     u_id);
			list_del(pos);
			vfree(p_user);
			i_ret = btif_close(p_btif);
			if (i_ret)
				BTIF_WARN_FUNC("BTIF close failed");
			break;
		}
	}
	BTIF_MUTEX_UNLOCK(&(p_btif->ops_mtx));
	BTIF_DBG_FUNC("--");
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_btif_close);


/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_write
* DESCRIPTION
*  send data throuth BTIF module
*  there's no internal buffer to cache STP data in BTIF driver,
*  if in DMA mode
*  btif driver will check if there's enough space in vFIFO for data to send in DMA mode
*    if yes, put data to vFIFO and return corresponding data length to caller
*    if no, corresponding error code will be returned to called
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
*  p_buf       [IN] pointer to target data to send
*  len         [IN] data length (should be less than 2014 bytes per STP package)
*
*  if in non-DMA mode, BTIF driver will try to write to THR of BTIF controller
*  if btif driver detected that no space is available in Tx FIFO,
*  will return E_BTIF_NO_SPACE, mostly something is wrong with BTIF or
*  consys when this return value is returned
* RETURNS
*  int  positive: data length send through BTIF;
*       negative: please see ENUM_BTIF_OP_ERROR_CODE
*       E_BTIF_AGAIN (0) will be returned to caller
*       if btif does not have enough vFIFO to send data,
*       when caller get 0, he should wait for a moment
*       (5~10ms maybe) and try a few times (maybe 10~20)
*        if still get E_BTIF_AGAIN,
*        should call BTIF's debug API and dump BTIF driver
*        and BTIF/DMA register information to kernel log for debug
*        E_BTIF_BAD_POINTER will be returned to caller
*        if btif is not opened successfully before call this API
*        E_BTIF_INVAL_PARAM will be returned if parameter is not valid

*****************************************************************************/
int mtk_wcn_btif_write(unsigned long u_id,
		       const unsigned char *p_buf, unsigned int len)
{
	int i_ret = -1;
	p_mtk_btif p_btif = NULL;

	BTIF_DBG_FUNC("++");
	p_btif = btif_exp_srh_id(u_id);

	if (p_btif == NULL)
		return E_BTIF_INVAL_PARAM;
	if (p_buf == NULL) {
		BTIF_ERR_FUNC("invalid p_buf (0x%p)\n", p_buf);
		return E_BTIF_INVAL_PARAM;
	}
	if ((len == 0) || (len > BTIF_MAX_LEN_PER_PKT)) {
		BTIF_ERR_FUNC("invalid buffer length(%d)\n", len);
		return E_BTIF_INVAL_PARAM;
	}

	i_ret = btif_send_data(p_btif, p_buf, len);
	BTIF_DBG_FUNC("--, i_ret:%d\n", i_ret);
	return i_ret;
}
EXPORT_SYMBOL(mtk_wcn_btif_write);


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
*  int   positive: data length read from BTIF;
*        negative: please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_read(unsigned long u_id,
		      unsigned char *p_buf, unsigned int max_len)
{
	return 0;
}

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
int mtk_wcn_btif_dpidle_ctrl(unsigned long u_id, ENUM_BTIF_DPIDLE_CTRL en_flag)
{
	int i_ret = -1;
	p_mtk_btif p_btif = NULL;

	p_btif = btif_exp_srh_id(u_id);

	if (p_btif == NULL)
		return E_BTIF_INVAL_PARAM;

	if (en_flag == BTIF_DPIDLE_DISABLE)
		i_ret = btif_exit_dpidle(p_btif);
	else
		i_ret = btif_enter_dpidle(p_btif);

	return i_ret;
}
EXPORT_SYMBOL(mtk_wcn_btif_dpidle_ctrl);


/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_rx_cb_register
* DESCRIPTION
*  register rx callback function to BTIF module by btif user
* PARAMETERS
*  p_btif  [IN] pointer returned by mtk_wcn_btif_open
*  rx_cb  [IN] pointer to stp rx handler callback function,
*            should be comply with MTK_WCN_BTIF_RX_CB
* RETURNS
*  int      0 = succeed;
*           others = fail, for detailed information,
*           please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_rx_cb_register(unsigned long u_id, MTK_WCN_BTIF_RX_CB rx_cb)
{
	int i_ret = -1;
	p_mtk_btif p_btif = NULL;

	p_btif = btif_exp_srh_id(u_id);

	if (p_btif == NULL)
		return E_BTIF_INVAL_PARAM;

	i_ret = btif_rx_cb_reg(p_btif, rx_cb);

	return i_ret;
}
EXPORT_SYMBOL(mtk_wcn_btif_rx_cb_register);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_wakeup_consys
* DESCRIPTION
*  once sleep command is sent to con sys,
*  should call this API before send wakeup command
*  to make con sys aware host want to send data to consys
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
* RETURNS
*  int          0 = succeed; others = fail, for detailed information, please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_wakeup_consys(unsigned long u_id)
{
	int i_ret = -1;
	p_mtk_btif p_btif = NULL;

	p_btif = btif_exp_srh_id(u_id);

	if (p_btif == NULL)
		return E_BTIF_INVAL_PARAM;

/*i_ret = hal_btif_raise_wak_sig(p_btif->p_btif_info);*/
	i_ret = btif_raise_wak_signal(p_btif);

	return i_ret;
}
EXPORT_SYMBOL(mtk_wcn_btif_wakeup_consys);


/***************End of Normal Mode API declearation**********/

/***************Debug Purpose API declearation**********/

/*****************************************************************************
* FUNCTION
*  mtk_wcn_btif_loopback_ctrl
* DESCRIPTION
*  enable/disable BTIF internal loopback function,
*  when this function is enabled data send to btif
*  will be received by btif itself
*  only for debug purpose, should never use this function in normal mode
* PARAMETERS
*  p_btif      [IN] pointer returned by mtk_wcn_btif_open
*  enable     [IN] loopback mode control flag, enable or disable,
*  shou be one of ENUM_BTIF_LPBK_MODE
* RETURNS
*  int          0 = succeed;
*  others = fail, for detailed information, please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_loopback_ctrl(unsigned long u_id, ENUM_BTIF_LPBK_MODE enable)
{
	int i_ret = -1;
	p_mtk_btif p_btif = NULL;

	p_btif = btif_exp_srh_id(u_id);

	if (p_btif == NULL)
		return E_BTIF_INVAL_PARAM;
	i_ret =
	    btif_lpbk_ctrl(p_btif, enable == BTIF_LPBK_ENABLE ? true : false);

	return i_ret;
}
EXPORT_SYMBOL(mtk_wcn_btif_loopback_ctrl);

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
*  int          0 = succeed; others = fail, for detailed information, please see ENUM_BTIF_OP_ERROR_CODE
*****************************************************************************/
int mtk_wcn_btif_dbg_ctrl(unsigned long u_id, ENUM_BTIF_DBG_ID flag)
{
	int i_ret = -1;
	p_mtk_btif p_btif = NULL;

	p_btif = btif_exp_srh_id(u_id);

	if (p_btif == NULL)
		return E_BTIF_INVAL_PARAM;

	i_ret = 0;
	switch (flag) {
	case BTIF_DISABLE_LOGGER:{
			BTIF_INFO_FUNC
			    ("disable btif log function for both Tx and Rx\n");
			btif_log_buf_disable(&p_btif->tx_log);
			btif_log_buf_disable(&p_btif->rx_log);
		}
		break;
	case BTIF_ENABLE_LOGGER:{
			BTIF_INFO_FUNC
			    ("enable btif log function for both Tx and Rx\n");
			btif_log_buf_enable(&p_btif->tx_log);
			btif_log_buf_enable(&p_btif->rx_log);
		}
		break;
	case BTIF_DUMP_LOG:{
			BTIF_INFO_FUNC("dump btif log for both Tx and Rx\n");
			btif_log_buf_dmp_out(&p_btif->tx_log);
			btif_log_buf_dmp_out(&p_btif->rx_log);
		}
		break;

	case BTIF_CLR_LOG:{
			BTIF_INFO_FUNC("clear btif log for both Tx and Rx\n");
			btif_log_buf_reset(&p_btif->tx_log);
			btif_log_buf_reset(&p_btif->rx_log);
		}
		break;
	case BTIF_DUMP_BTIF_REG:
		 /*TBD*/ btif_dump_reg(p_btif);
		break;
	case BTIF_ENABLE_RT_LOG:
		BTIF_INFO_FUNC
		    ("enable btif real time log for both Tx and Rx\n");
		btif_log_output_enable(&p_btif->tx_log);
		btif_log_output_enable(&p_btif->rx_log);
		break;
	case BTIF_DISABLE_RT_LOG:
		BTIF_INFO_FUNC
		    ("disable btif real time log for both Tx and Rx\n");
		btif_log_output_disable(&p_btif->tx_log);
		btif_log_output_disable(&p_btif->rx_log);
		break;
	default:
		BTIF_INFO_FUNC("not supported flag:%d\n", flag);
		i_ret = -2;
		break;
	}

	return i_ret;
}
EXPORT_SYMBOL(mtk_wcn_btif_dbg_ctrl);

bool mtk_wcn_btif_parser_wmt_evt(unsigned long u_id,
	const char *sub_str, unsigned int str_len)
{
	bool b_ret = false;
	p_mtk_btif p_btif = NULL;

	p_btif = btif_exp_srh_id(u_id);

	if (p_btif == NULL)
		return E_BTIF_INVAL_PARAM;
	b_ret = btif_parser_wmt_evt(p_btif, sub_str, str_len);
	BTIF_INFO_FUNC("parser wmt evt %s\n", b_ret ? "ok" : "fail");

	return b_ret;
}

/**********End of Debug Purpose API declearation**********/

int btif_open_no_id(void)
{
	int i_ret = 0;
	p_mtk_btif p_btif = &g_btif[0];

	i_ret = btif_open(p_btif);

	if (i_ret)
		BTIF_ERR_FUNC("btif_open failed, i_ret(%d)\n", i_ret);
	else
		BTIF_INFO_FUNC("btif_open succeed\n");

	return i_ret;
}

int btif_close_no_id(void)
{
	int i_ret = 0;
	p_mtk_btif p_btif = &g_btif[0];

	i_ret = btif_close(p_btif);

	if (i_ret)
		BTIF_ERR_FUNC("btif_close failed, i_ret(%d)\n", i_ret);
	else
		BTIF_INFO_FUNC("btif_close succeed\n");
	return i_ret;
}

int btif_write_no_id(const unsigned char *p_buf, unsigned int len)
{
	int i_ret = -1;
	p_mtk_btif p_btif = &g_btif[0];

	BTIF_DBG_FUNC("++");

	if (p_buf == NULL) {
		BTIF_ERR_FUNC("invalid p_buf (0x%p)\n", p_buf);
		return E_BTIF_INVAL_PARAM;
	}
	if ((len == 0) || (len > BTIF_MAX_LEN_PER_PKT)) {
		BTIF_ERR_FUNC("invalid buffer length(%d)\n", len);
		return E_BTIF_INVAL_PARAM;
	}

	i_ret = btif_send_data(p_btif, p_buf, len);
	BTIF_DBG_FUNC("--, i_ret:%d\n", i_ret);
	return i_ret;
}

int btif_dpidle_ctrl_no_id(ENUM_BTIF_DPIDLE_CTRL en_flag)
{
	int i_ret = -1;
	p_mtk_btif p_btif = &g_btif[0];

	if (en_flag == BTIF_DPIDLE_DISABLE)
		i_ret = btif_exit_dpidle(p_btif);
	else
		i_ret = btif_enter_dpidle(p_btif);

	return i_ret;
}

int btif_wakeup_consys_no_id(void)
{
	int i_ret = -1;
	p_mtk_btif p_btif = &g_btif[0];

/*i_ret = hal_btif_raise_wak_sig(p_btif->p_btif_info);*/
	i_ret = btif_raise_wak_signal(p_btif);

	return i_ret;
}

int btif_loopback_ctrl_no_id(ENUM_BTIF_LPBK_MODE enable)
{
	int i_ret = -1;
	p_mtk_btif p_btif = &g_btif[0];

	i_ret =
	    btif_lpbk_ctrl(p_btif, enable == BTIF_LPBK_ENABLE ? true : false);

	return i_ret;
}

int btif_dbg_ctrl_no_id(ENUM_BTIF_DBG_ID flag)
{
	int i_ret = -1;
	p_mtk_btif p_btif = &g_btif[0];

	i_ret = 0;
	switch (flag) {
	case BTIF_DISABLE_LOGGER:{
			BTIF_INFO_FUNC
			    ("disable btif log function for both Tx and Rx\n");
			btif_log_buf_disable(&p_btif->tx_log);
			btif_log_buf_disable(&p_btif->rx_log);
		}
		break;
	case BTIF_ENABLE_LOGGER:{
			BTIF_INFO_FUNC
			    ("enable btif log function for both Tx and Rx\n");
			btif_log_buf_enable(&p_btif->tx_log);
			btif_log_buf_enable(&p_btif->rx_log);
		}
		break;
	case BTIF_DUMP_LOG:{
			BTIF_INFO_FUNC("dump btif log for both Tx and Rx\n");
			btif_log_buf_dmp_out(&p_btif->tx_log);
			btif_log_buf_dmp_out(&p_btif->rx_log);
		}
		break;

	case BTIF_CLR_LOG:{
			BTIF_INFO_FUNC("clear btif log for both Tx and Rx\n");
			btif_log_buf_reset(&p_btif->tx_log);
			btif_log_buf_reset(&p_btif->rx_log);
		}
		break;
	case BTIF_DUMP_BTIF_REG:
		 /*TBD*/ btif_dump_reg(p_btif);
		break;
	case BTIF_ENABLE_RT_LOG:
		BTIF_INFO_FUNC
		    ("enable btif real time log for both Tx and Rx\n");
		btif_log_output_enable(&p_btif->tx_log);
		btif_log_output_enable(&p_btif->rx_log);
		break;
	case BTIF_DISABLE_RT_LOG:
		BTIF_INFO_FUNC
		    ("disable btif real time log for both Tx and Rx\n");
		btif_log_output_disable(&p_btif->tx_log);
		btif_log_output_disable(&p_btif->rx_log);
		break;
	default:
		BTIF_INFO_FUNC("not supported flag:%d\n", flag);
		i_ret = -2;
		break;
	}

	return i_ret;
}

int mtk_btif_exp_open_test(void)
{
	int i_ret = 0;

	i_ret = btif_open_no_id();
	if (i_ret < 0) {
		BTIF_INFO_FUNC("mtk_wcn_btif_open failed\n");
		return -1;
	}

	BTIF_INFO_FUNC("mtk_wcn_btif_open succeed\n");

	return i_ret;
}

int mtk_btif_exp_close_test(void)
{
	int i_ret = 0;

	i_ret = btif_close_no_id();
	if (i_ret < 0) {
		BTIF_INFO_FUNC("mtk_wcn_btif_close failed\n");
		return -1;
	}

	BTIF_INFO_FUNC("mtk_wcn_btif_close succeed\n");

	return i_ret;
}

int mtk_btif_exp_write_test(void)
{
	return mtk_btif_exp_write_stress_test(100, 10);
}

int mtk_btif_exp_write_stress_test(unsigned int length, unsigned int max_loop)
{
#define BUF_LEN 1024
	int i_ret = 0;
	int idx = 0;
	int buf_len = length > BUF_LEN ? BUF_LEN : length;
	int loop = max_loop > 1000000 ? 1000000 : max_loop;
	unsigned char *buffer;

	buffer = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buffer) {
		BTIF_ERR_FUNC("btif tester kmalloc failed\n");
		return -1;
	}

	for (idx = 0; idx < buf_len; idx++)
		/* btif_stress_test_buf[idx] = BUF_LEN -idx; */
		*(buffer + idx) = idx % 255;
	i_ret = btif_loopback_ctrl_no_id(BTIF_LPBK_ENABLE);
	BTIF_INFO_FUNC("mtk_wcn_btif_loopback_ctrl returned %d\n", i_ret);
	while (loop--) {
		i_ret = btif_write_no_id(buffer, buf_len);
		BTIF_INFO_FUNC("mtk_wcn_btif_write left loop:%d, i_ret:%d\n",
				   loop, i_ret);
		if (i_ret != buf_len) {
			BTIF_INFO_FUNC
				("mtk_wcn_btif_write failed, target len %d, sent len: %d\n",
				 buf_len, i_ret);
			break;
		}
		buf_len--;
		if (buf_len <= 0)
			buf_len = length > BUF_LEN ? BUF_LEN : length;
	}
	kfree(buffer);
	return i_ret;
}

int mtk_btif_exp_suspend_test(void)
{
	int i_ret = 0;
	p_mtk_btif p_btif = &g_btif[0];

	i_ret = _btif_suspend(p_btif);
	return i_ret;
}

int mtk_btif_exp_restore_noirq_test(void)
{
	int i_ret = 0;
	p_mtk_btif p_btif = &g_btif[0];

	i_ret = _btif_restore_noirq(p_btif);
	return i_ret;
}

int mtk_btif_exp_clock_ctrl(int en)
{
	int i_ret = 0;
	p_mtk_btif p_btif = &g_btif[0];

	i_ret = btif_clock_ctrl(p_btif, en);
	return i_ret;
}

int mtk_btif_exp_resume_test(void)
{
	int i_ret = 0;
	p_mtk_btif p_btif = &g_btif[0];

	i_ret = _btif_resume(p_btif);
	return i_ret;
}

int mtk_btif_exp_enter_dpidle_test(void)
{
	return btif_dpidle_ctrl_no_id(BTIF_DPIDLE_ENABLE);
}

int mtk_btif_exp_exit_dpidle_test(void)
{
	return btif_dpidle_ctrl_no_id(BTIF_DPIDLE_DISABLE);
}

int mtk_btif_exp_log_debug_test(int flag)
{
	int i_ret = 0;

	i_ret = btif_dbg_ctrl_no_id(flag);
	return i_ret;
}

void mtk_btif_read_cpu_sw_rst_debug_exp(void)
{
	mtk_btif_read_cpu_sw_rst_debug();
}

/************End of Function**********/
