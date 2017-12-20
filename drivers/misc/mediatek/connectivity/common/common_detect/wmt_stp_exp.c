/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/
#include "osal_typedef.h"
#include "wmt_stp_exp.h"

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-STP-EXP]"

#define WMT_STP_EXP_INFO_FUNC(fmt, arg...)   pr_debug(DFT_TAG "[I]%s: "  fmt, __func__ , ##arg)
#define WMT_STP_EXP_WARN_FUNC(fmt, arg...)   pr_warn(DFT_TAG "[W]%s: "  fmt, __func__ , ##arg)
#define WMT_STP_EXP_ERR_FUNC(fmt, arg...)    pr_err(DFT_TAG "[E]%s(%d):ERROR! "   fmt, __func__ , __LINE__, ##arg)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
#ifdef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
/*STP exp*/
MTK_WCN_STP_SEND_DATA mtk_wcn_stp_send_data_f = NULL;
MTK_WCN_STP_SEND_DATA mtk_wcn_stp_send_data_raw_f = NULL;
MTK_WCN_STP_PARSER_DATA mtk_wcn_stp_parser_data_f = NULL;
MTK_WCN_STP_RECV_DATA mtk_wcn_stp_receive_data_f = NULL;
MTK_WCN_STP_IS_RXQ_EMPTY mtk_wcn_stp_is_rxqueue_empty_f = NULL;
MTK_WCN_STP_IS_RDY mtk_wcn_stp_is_ready_f = NULL;
MTK_WCN_STP_SET_BLUEZ mtk_wcn_stp_set_bluez_f = NULL;
MTK_WCN_STP_REG_IF_TX mtk_wcn_stp_if_tx_f = NULL;
MTK_WCN_STP_REG_IF_RX mtk_wcn_stp_if_rx_f = NULL;
MTK_WCN_STP_REG_EVENT_CB mtk_wcn_stp_reg_event_cb_f = NULL;
MTK_WCN_STP_RGE_TX_EVENT_CB mtk_wcn_stp_reg_tx_event_cb_f = NULL;
MTK_WCN_STP_COREDUMP_START_GET mtk_wcn_stp_coredump_start_get_f = NULL;

/*WMT exp*/
MTK_WCN_WMT_FUNC_CTRL mtk_wcn_wmt_func_on_f = NULL;
MTK_WCN_WMT_FUNC_CTRL mtk_wcn_wmt_func_off_f = NULL;
MTK_WCN_WMT_THERM_CTRL mtk_wcn_wmt_therm_ctrl_f = NULL;
MTK_WCN_WMT_HWVER_GET mtk_wcn_wmt_hwver_get_f = NULL;
MTK_WCN_WMT_DSNS_CTRL mtk_wcn_wmt_dsns_ctrl_f = NULL;
MTK_WCN_WMT_MSGCB_REG mtk_wcn_wmt_msgcb_reg_f = NULL;
MTK_WCN_WMT_MSGCB_UNREG mtk_wcn_wmt_msgcb_unreg_f = NULL;
MTK_WCN_WMT_SDIO_OP_REG mtk_wcn_wmt_sdio_op_reg_f = NULL;
MTK_WCN_WMT_SDIO_HOST_AWAKE mtk_wcn_wmt_sdio_host_awake_f = NULL;
MTK_WCN_WMT_ASSERT mtk_wcn_wmt_assert_f = NULL;
MTK_WCN_WMT_ASSERT_TIMEOUT mtk_wcn_wmt_assert_timeout_f = NULL;
MTK_WCN_WMT_IC_INFO_GET mtk_wcn_wmt_ic_info_get_f = NULL;
MTK_WCN_WMT_PSM_CTRL mtk_wcn_wmt_psm_ctrl_f = NULL;

/*******************************************************************************
*                          F U N C T I O N S
********************************************************************************
*/

UINT32 mtk_wcn_stp_exp_cb_reg(P_MTK_WCN_STP_EXP_CB_INFO pStpExpCb)
{
	WMT_STP_EXP_INFO_FUNC("call stp exp cb reg\n");

	mtk_wcn_stp_send_data_f = pStpExpCb->stp_send_data_cb;
	mtk_wcn_stp_send_data_raw_f = pStpExpCb->stp_send_data_raw_cb;
	mtk_wcn_stp_parser_data_f = pStpExpCb->stp_parser_data_cb;
	mtk_wcn_stp_receive_data_f = pStpExpCb->stp_receive_data_cb;
	mtk_wcn_stp_is_rxqueue_empty_f = pStpExpCb->stp_is_rxqueue_empty_cb;
	mtk_wcn_stp_is_ready_f = pStpExpCb->stp_is_ready_cb;
	mtk_wcn_stp_set_bluez_f = pStpExpCb->stp_set_bluez_cb;
	mtk_wcn_stp_if_tx_f = pStpExpCb->stp_if_tx_cb;
	mtk_wcn_stp_if_rx_f = pStpExpCb->stp_if_rx_cb;
	mtk_wcn_stp_reg_event_cb_f = pStpExpCb->stp_reg_event_cb;
	mtk_wcn_stp_reg_tx_event_cb_f = pStpExpCb->stp_reg_tx_event_cb;
	mtk_wcn_stp_coredump_start_get_f = pStpExpCb->stp_coredump_start_get_cb;

	return 0;
}
EXPORT_SYMBOL(mtk_wcn_stp_exp_cb_reg);

UINT32 mtk_wcn_stp_exp_cb_unreg(VOID)
{
	WMT_STP_EXP_INFO_FUNC("call stp exp cb unreg\n");

	mtk_wcn_stp_send_data_f = NULL;
	mtk_wcn_stp_send_data_raw_f = NULL;
	mtk_wcn_stp_parser_data_f = NULL;
	mtk_wcn_stp_receive_data_f = NULL;
	mtk_wcn_stp_is_rxqueue_empty_f = NULL;
	mtk_wcn_stp_is_ready_f = NULL;
	mtk_wcn_stp_set_bluez_f = NULL;
	mtk_wcn_stp_if_tx_f = NULL;
	mtk_wcn_stp_if_rx_f = NULL;
	mtk_wcn_stp_reg_event_cb_f = NULL;
	mtk_wcn_stp_reg_tx_event_cb_f = NULL;
	mtk_wcn_stp_coredump_start_get_f = NULL;

	return 0;
}
EXPORT_SYMBOL(mtk_wcn_stp_exp_cb_unreg);

INT32 mtk_wcn_stp_send_data(const PUINT8 buffer, const UINT32 length, const UINT8 type)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_send_data_f) {
		ret = (*mtk_wcn_stp_send_data_f) (buffer, length, type);
		/* WMT_STP_EXP_INFO_FUNC("mtk_wcn_stp_send_data_f send data(%d)\n",ret); */
	} else {
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_send_data_f cb is null\n");
	}

	return ret;

}
EXPORT_SYMBOL(mtk_wcn_stp_send_data);

INT32 mtk_wcn_stp_send_data_raw(const PUINT8 buffer, const UINT32 length, const UINT8 type)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_send_data_raw_f)
		ret = (*mtk_wcn_stp_send_data_raw_f) (buffer, length, type);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_send_data_raw_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_send_data_raw);

INT32 mtk_wcn_stp_parser_data(PUINT8 buffer, UINT32 length)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_parser_data_f)
		ret = (*mtk_wcn_stp_parser_data_f) (buffer, length);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_parser_data_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_parser_data);

INT32 mtk_wcn_stp_receive_data(PUINT8 buffer, UINT32 length, UINT8 type)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_receive_data_f)
		ret = (*mtk_wcn_stp_receive_data_f) (buffer, length, type);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_receive_data_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_receive_data);

MTK_WCN_BOOL mtk_wcn_stp_is_rxqueue_empty(UINT8 type)
{
	MTK_WCN_BOOL ret = MTK_WCN_BOOL_FALSE;

	if (mtk_wcn_stp_is_rxqueue_empty_f)
		ret = (*mtk_wcn_stp_is_rxqueue_empty_f) (type);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_is_rxqueue_empty_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_is_rxqueue_empty);

MTK_WCN_BOOL mtk_wcn_stp_is_ready(void)
{
	MTK_WCN_BOOL ret = MTK_WCN_BOOL_FALSE;

	if (mtk_wcn_stp_is_ready_f)
		ret = (*mtk_wcn_stp_is_ready_f) ();
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_is_ready_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_is_ready);

void mtk_wcn_stp_set_bluez(MTK_WCN_BOOL flags)
{

	if (mtk_wcn_stp_set_bluez_f)
		(*mtk_wcn_stp_set_bluez_f) (flags);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_set_bluez_f cb is null\n");

}
EXPORT_SYMBOL(mtk_wcn_stp_set_bluez);

INT32 mtk_wcn_stp_register_if_tx(ENUM_STP_TX_IF_TYPE stp_if, MTK_WCN_STP_IF_TX func)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_if_tx_f)
		ret = (*mtk_wcn_stp_if_tx_f) (stp_if, func);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_if_tx_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_register_if_tx);

INT32 mtk_wcn_stp_register_if_rx(MTK_WCN_STP_IF_RX func)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_if_rx_f)
		ret = (*mtk_wcn_stp_if_rx_f) (func);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_if_rx_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_register_if_rx);

INT32 mtk_wcn_stp_register_event_cb(INT32 type, MTK_WCN_STP_EVENT_CB func)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_reg_event_cb_f)
		ret = (*mtk_wcn_stp_reg_event_cb_f) (type, func);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_reg_event_cb_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_register_event_cb);

INT32 mtk_wcn_stp_register_tx_event_cb(INT32 type, MTK_WCN_STP_EVENT_CB func)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_reg_tx_event_cb_f)
		ret = (*mtk_wcn_stp_reg_tx_event_cb_f) (type, func);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_reg_tx_event_cb_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_register_tx_event_cb);

INT32 mtk_wcn_stp_coredump_start_get(VOID)
{
	INT32 ret = -1;

	if (mtk_wcn_stp_coredump_start_get_f)
		ret = (*mtk_wcn_stp_coredump_start_get_f) ();
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_stp_coredump_start_get_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_coredump_start_get);

UINT32 mtk_wcn_wmt_exp_cb_reg(P_MTK_WCN_WMT_EXP_CB_INFO pWmtExpCb)
{
	WMT_STP_EXP_INFO_FUNC("call wmt exp cb reg\n");

	mtk_wcn_wmt_func_on_f = pWmtExpCb->wmt_func_on_cb;
	mtk_wcn_wmt_func_off_f = pWmtExpCb->wmt_func_off_cb;
	mtk_wcn_wmt_therm_ctrl_f = pWmtExpCb->wmt_therm_ctrl_cb;
	mtk_wcn_wmt_hwver_get_f = pWmtExpCb->wmt_hwver_get_cb;
	mtk_wcn_wmt_dsns_ctrl_f = pWmtExpCb->wmt_dsns_ctrl_cb;
	mtk_wcn_wmt_msgcb_reg_f = pWmtExpCb->wmt_msgcb_reg_cb;
	mtk_wcn_wmt_msgcb_unreg_f = pWmtExpCb->wmt_msgcb_unreg_cb;
	mtk_wcn_wmt_sdio_op_reg_f = pWmtExpCb->wmt_sdio_op_reg_cb;
	mtk_wcn_wmt_sdio_host_awake_f = pWmtExpCb->wmt_sdio_host_awake_cb;
	mtk_wcn_wmt_assert_f = pWmtExpCb->wmt_assert_cb;
	mtk_wcn_wmt_assert_timeout_f = pWmtExpCb->wmt_assert_timeout_cb;
	mtk_wcn_wmt_ic_info_get_f = pWmtExpCb->wmt_ic_info_get_cb;
	mtk_wcn_wmt_psm_ctrl_f = pWmtExpCb->wmt_psm_ctrl_cb;
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_exp_cb_reg);

UINT32 mtk_wcn_wmt_exp_cb_unreg(VOID)
{
	WMT_STP_EXP_INFO_FUNC("call wmt exp cb unreg\n");

	mtk_wcn_wmt_func_on_f = NULL;
	mtk_wcn_wmt_func_off_f = NULL;
	mtk_wcn_wmt_therm_ctrl_f = NULL;
	mtk_wcn_wmt_hwver_get_f = NULL;
	mtk_wcn_wmt_dsns_ctrl_f = NULL;
	mtk_wcn_wmt_msgcb_reg_f = NULL;
	mtk_wcn_wmt_msgcb_unreg_f = NULL;
	mtk_wcn_wmt_sdio_op_reg_f = NULL;
	mtk_wcn_wmt_sdio_host_awake_f = NULL;
	mtk_wcn_wmt_assert_f = NULL;
	mtk_wcn_wmt_assert_timeout_f = NULL;
	mtk_wcn_wmt_ic_info_get_f = NULL;
	mtk_wcn_wmt_psm_ctrl_f = NULL;

	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_exp_cb_unreg);

MTK_WCN_BOOL mtk_wcn_wmt_func_off(ENUM_WMTDRV_TYPE_T type)
{
	MTK_WCN_BOOL ret = MTK_WCN_BOOL_FALSE;

	if (mtk_wcn_wmt_func_off_f)
		ret = (*mtk_wcn_wmt_func_off_f) (type);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_func_off_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_func_off);

MTK_WCN_BOOL mtk_wcn_wmt_func_on(ENUM_WMTDRV_TYPE_T type)
{
	MTK_WCN_BOOL ret = MTK_WCN_BOOL_FALSE;

	if (mtk_wcn_wmt_func_on_f) {
		ret = (*mtk_wcn_wmt_func_on_f) (type);
		WMT_STP_EXP_INFO_FUNC("mtk_wcn_wmt_func_on_f type(%d)\n", type);
	} else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_func_on_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_func_on);

INT8 mtk_wcn_wmt_therm_ctrl(ENUM_WMTTHERM_TYPE_T eType)
{
	INT32 ret = -1;

	if (mtk_wcn_wmt_therm_ctrl_f)
		ret = (*mtk_wcn_wmt_therm_ctrl_f) (eType);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_therm_ctrl_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_therm_ctrl);

ENUM_WMTHWVER_TYPE_T mtk_wcn_wmt_hwver_get(VOID)
{
	ENUM_WMTHWVER_TYPE_T ret = WMTHWVER_INVALID;

	if (mtk_wcn_wmt_hwver_get_f)
		ret = (*mtk_wcn_wmt_hwver_get_f) ();
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_hwver_get_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_hwver_get);

MTK_WCN_BOOL mtk_wcn_wmt_dsns_ctrl(ENUM_WMTDSNS_TYPE_T eType)
{
	MTK_WCN_BOOL ret = MTK_WCN_BOOL_FALSE;

	if (mtk_wcn_wmt_dsns_ctrl_f)
		ret = (*mtk_wcn_wmt_dsns_ctrl_f) (eType);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_dsns_ctrl_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_dsns_ctrl);

INT32 mtk_wcn_wmt_msgcb_reg(ENUM_WMTDRV_TYPE_T eType, PF_WMT_CB pCb)
{
	INT32 ret = 0;

	if (mtk_wcn_wmt_msgcb_reg_f)
		ret = (*mtk_wcn_wmt_msgcb_reg_f) (eType, pCb);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_msgcb_reg_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_reg);

INT32 mtk_wcn_wmt_msgcb_unreg(ENUM_WMTDRV_TYPE_T eType)
{
	INT32 ret = 0;

	if (mtk_wcn_wmt_msgcb_unreg_f)
		ret = (*mtk_wcn_wmt_msgcb_unreg_f) (eType);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_msgcb_unreg_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_unreg);

INT32 mtk_wcn_stp_wmt_sdio_op_reg(PF_WMT_SDIO_PSOP own_cb)
{
	INT32 ret = -1;

	if (mtk_wcn_wmt_sdio_op_reg_f)
		ret = (*mtk_wcn_wmt_sdio_op_reg_f) (own_cb);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_sdio_op_reg_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_wmt_sdio_op_reg);

INT32 mtk_wcn_stp_wmt_sdio_host_awake(VOID)
{
	INT32 ret = -1;

	if (mtk_wcn_wmt_sdio_host_awake_f)
		ret = (*mtk_wcn_wmt_sdio_host_awake_f) ();
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_sdio_host_awake_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_stp_wmt_sdio_host_awake);

MTK_WCN_BOOL mtk_wcn_wmt_assert(ENUM_WMTDRV_TYPE_T type, UINT32 reason)
{
	MTK_WCN_BOOL ret = MTK_WCN_BOOL_FALSE;

	if (mtk_wcn_wmt_assert_f)
		ret = (*mtk_wcn_wmt_assert_f) (type, reason);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_assert_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_assert);

MTK_WCN_BOOL mtk_wcn_wmt_assert_timeout(ENUM_WMTDRV_TYPE_T type, UINT32 reason, INT32 timeout)
{
	MTK_WCN_BOOL ret = MTK_WCN_BOOL_FALSE;

	if (mtk_wcn_wmt_assert_timeout_f)
		ret = (*mtk_wcn_wmt_assert_timeout_f)(type, reason, timeout);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_assert_timeout_f cb is null\n");
	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_assert_timeout);

UINT32 mtk_wcn_wmt_ic_info_get(ENUM_WMT_CHIPINFO_TYPE_T type)
{
	UINT32 ret = 0;

	if (mtk_wcn_wmt_ic_info_get_f)
		ret = (*mtk_wcn_wmt_ic_info_get_f) (type);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_ic_info_get_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_ic_info_get);

INT32 mtk_wcn_wmt_psm_ctrl(MTK_WCN_BOOL flag)
{
	UINT32 ret = 0;

	if (mtk_wcn_wmt_psm_ctrl_f)
		ret = (*mtk_wcn_wmt_psm_ctrl_f)(flag);
	else
		WMT_STP_EXP_ERR_FUNC("mtk_wcn_wmt_psm_ctrl_f cb is null\n");

	return ret;
}
EXPORT_SYMBOL(mtk_wcn_wmt_psm_ctrl);

#endif
