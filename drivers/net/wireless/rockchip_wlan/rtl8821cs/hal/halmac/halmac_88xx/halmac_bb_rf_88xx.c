/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "halmac_bb_rf_88xx.h"
#include "halmac_88xx_cfg.h"
#include "halmac_common_88xx.h"
#include "halmac_init_88xx.h"

#if HALMAC_88XX_SUPPORT
/**
 * start_dpk_88xx() -trigger FW DPK
 * @adapter : the adapter of halmac
 * Author : Yong-Ching Lin/KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
start_dpk_88xx(struct halmac_adapter *adapter)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.dpk_state.proc_status;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (adapter->fw_ver.h2c_version < 15)
		return HALMAC_RET_FW_NO_SUPPORT;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(dpk)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	hdr_info.sub_cmd_id = SUB_CMD_ID_DPK;
	hdr_info.content_size = 1;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	adapter->halmac_state.dpk_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c pkt fail!!\n");
		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_DPK);
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * start_iqk_88xx() -trigger FW IQK
 * @adapter : the adapter of halmac
 * @param : IQK parameter
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
start_iqk_88xx(struct halmac_adapter *adapter, struct halmac_iqk_para *param)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.iqk_state.proc_status;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(iqk)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	IQK_SET_CLEAR(h2c_buf, param->clear);
	IQK_SET_SEGMENT_IQK(h2c_buf, param->segment_iqk);

	hdr_info.sub_cmd_id = SUB_CMD_ID_IQK;
	hdr_info.content_size = 1;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	adapter->halmac_state.iqk_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c pkt fail!!\n");
		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_IQK);
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * ctrl_pwr_tracking_88xx() -trigger FW power tracking
 * @adapter : the adapter of halmac
 * @opt : power tracking option
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
ctrl_pwr_tracking_88xx(struct halmac_adapter *adapter,
		       struct halmac_pwr_tracking_option *opt)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_h2c_header_info hdr_info;
	struct halmac_pwr_tracking_para *param;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.pwr_trk_state.proc_status;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(pwr tracking)...\n");
		return HALMAC_RET_BUSY_STATE;
	}

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	PWR_TRK_SET_TYPE(h2c_buf, opt->type);
	PWR_TRK_SET_BBSWING_INDEX(h2c_buf, opt->bbswing_index);

	param = &opt->pwr_tracking_para[HALMAC_RF_PATH_A];
	PWR_TRK_SET_ENABLE_A(h2c_buf, param->enable);
	PWR_TRK_SET_TX_PWR_INDEX_A(h2c_buf, param->tx_pwr_index);
	PWR_TRK_SET_TSSI_VALUE_A(h2c_buf, param->tssi_value);
	PWR_TRK_SET_OFFSET_VALUE_A(h2c_buf, param->pwr_tracking_offset_value);

	param = &opt->pwr_tracking_para[HALMAC_RF_PATH_B];
	PWR_TRK_SET_ENABLE_B(h2c_buf, param->enable);
	PWR_TRK_SET_TX_PWR_INDEX_B(h2c_buf, param->tx_pwr_index);
	PWR_TRK_SET_TSSI_VALUE_B(h2c_buf, param->tssi_value);
	PWR_TRK_SET_OFFSET_VALUE_B(h2c_buf, param->pwr_tracking_offset_value);

	param = &opt->pwr_tracking_para[HALMAC_RF_PATH_C];
	PWR_TRK_SET_ENABLE_C(h2c_buf, param->enable);
	PWR_TRK_SET_TX_PWR_INDEX_C(h2c_buf, param->tx_pwr_index);
	PWR_TRK_SET_TSSI_VALUE_C(h2c_buf, param->tssi_value);
	PWR_TRK_SET_OFFSET_VALUE_C(h2c_buf, param->pwr_tracking_offset_value);

	param = &opt->pwr_tracking_para[HALMAC_RF_PATH_D];
	PWR_TRK_SET_ENABLE_D(h2c_buf, param->enable);
	PWR_TRK_SET_TX_PWR_INDEX_D(h2c_buf, param->tx_pwr_index);
	PWR_TRK_SET_TSSI_VALUE_D(h2c_buf, param->tssi_value);
	PWR_TRK_SET_OFFSET_VALUE_D(h2c_buf, param->pwr_tracking_offset_value);

	hdr_info.sub_cmd_id = SUB_CMD_ID_PWR_TRK;
	hdr_info.content_size = 20;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	adapter->halmac_state.pwr_trk_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c pkt fail!!\n");
		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_POWER_TRACKING);
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_iqk_status_88xx(struct halmac_adapter *adapter,
		    enum halmac_cmd_process_status *proc_status)
{
	*proc_status = adapter->halmac_state.iqk_state.proc_status;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_dpk_status_88xx(struct halmac_adapter *adapter,
		    enum halmac_cmd_process_status *proc_status, u8 *data,
		    u32 *size)
{
	struct halmac_dpk_state *state = &adapter->halmac_state.dpk_state;

	*proc_status = adapter->halmac_state.dpk_state.proc_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*proc_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < state->data_size) {
			*size = state->data_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = state->data_size;
		PLTFM_MEMCPY(data, state->data, *size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_pwr_trk_status_88xx(struct halmac_adapter *adapter,
			enum halmac_cmd_process_status *proc_status)
{
	*proc_status = adapter->halmac_state.pwr_trk_state.proc_status;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_psd_status_88xx(struct halmac_adapter *adapter,
		    enum halmac_cmd_process_status *proc_status, u8 *data,
		    u32 *size)
{
	struct halmac_psd_state *state = &adapter->halmac_state.psd_state;

	*proc_status = state->proc_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*proc_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < state->data_size) {
			*size = state->data_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = state->data_size;
		PLTFM_MEMCPY(data, state->data, *size);
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * psd_88xx() - trigger fw psd
 * @adapter : the adapter of halmac
 * @start_psd : start PSD
 * @end_psd : end PSD
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
psd_88xx(struct halmac_adapter *adapter, u16 start_psd, u16 end_psd)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.psd_state.proc_status;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(psd)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (adapter->halmac_state.psd_state.data) {
		PLTFM_FREE(adapter->halmac_state.psd_state.data,
			   adapter->halmac_state.psd_state.data_size);
		adapter->halmac_state.psd_state.data = (u8 *)NULL;
	}

	adapter->halmac_state.psd_state.data_size = 0;
	adapter->halmac_state.psd_state.seg_size = 0;

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	PSD_SET_START_PSD(h2c_buf, start_psd);
	PSD_SET_END_PSD(h2c_buf, end_psd);

	hdr_info.sub_cmd_id = SUB_CMD_ID_PSD;
	hdr_info.content_size = 4;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c pkt fail!!\n");
		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_PSD);
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_h2c_ack_iqk_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num;
	u8 fw_rc;
	struct halmac_iqk_state *state = &adapter->halmac_state.iqk_state;
	enum halmac_cmd_process_status proc_status;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	if ((enum halmac_h2c_return_code)fw_rc == HALMAC_H2C_RETURN_SUCCESS) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_IQK, proc_status, NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_IQK, proc_status, &fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_h2c_ack_dpk_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num;
	u8 fw_rc;
	struct halmac_dpk_state *state = &adapter->halmac_state.dpk_state;
	enum halmac_cmd_process_status proc_status;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	if ((enum halmac_h2c_return_code)fw_rc == HALMAC_H2C_RETURN_SUCCESS) {
		proc_status = HALMAC_CMD_PROCESS_RCVD;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_DPK, proc_status, NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_DPK, proc_status, &fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_dpk_data_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num;
	u8 seg_id;
	u8 seg_size;
	u16 total_size;
	enum halmac_cmd_process_status proc_status;
	struct halmac_dpk_state *state = &adapter->halmac_state.dpk_state;

	seq_num = (u8)DPK_DATA_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_RCVD) {
		PLTFM_MSG_ERR("[ERR]not receive dpk ack\n");
		if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
			PLTFM_MSG_ERR("[ERR]not cmd sending\n");
			return HALMAC_RET_SUCCESS;
		}
	}

	total_size = (u16)DPK_DATA_GET_TOTAL_SIZE(buf);
	seg_id = (u8)DPK_DATA_GET_SEGMENT_ID(buf);
	seg_size = (u8)DPK_DATA_GET_SEGMENT_SIZE(buf);
	state->data_size = total_size;

	if (!state->data) {
		state->data = (u8 *)PLTFM_MALLOC(state->data_size);
		if (!state->data) {
			PLTFM_MSG_ERR("[ERR]malloc fail!!\n");
			return HALMAC_RET_NULL_POINTER;
		}
	}

	if (seg_id == 0)
		state->seg_size = seg_size;

	PLTFM_MEMCPY(state->data + seg_id * state->seg_size,
		     buf + C2H_DATA_OFFSET_88XX, seg_size);

	if (DPK_DATA_GET_END_SEGMENT(buf) == 0)
		return HALMAC_RET_SUCCESS;

	proc_status = HALMAC_CMD_PROCESS_DONE;
	state->proc_status = proc_status;

	PLTFM_EVENT_SIG(HALMAC_FEATURE_DPK, state->proc_status, state->data,
			state->data_size);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_h2c_ack_pwr_trk_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num;
	u8 fw_rc;
	struct halmac_pwr_tracking_state *state;
	enum halmac_cmd_process_status proc_status;

	state = &adapter->halmac_state.pwr_trk_state;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	if ((enum halmac_h2c_return_code)fw_rc == HALMAC_H2C_RETURN_SUCCESS) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_POWER_TRACKING, proc_status,
				NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_POWER_TRACKING, proc_status,
				&fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_psd_data_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seg_id;
	u8 seg_size;
	u8 seq_num;
	u16 total_size;
	enum halmac_cmd_process_status proc_status;
	struct halmac_psd_state *state = &adapter->halmac_state.psd_state;

	seq_num = (u8)PSD_DATA_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]seq num : h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]seq num mismatch : h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not cmd sending\n");
		return HALMAC_RET_SUCCESS;
	}

	total_size = (u16)PSD_DATA_GET_TOTAL_SIZE(buf);
	seg_id = (u8)PSD_DATA_GET_SEGMENT_ID(buf);
	seg_size = (u8)PSD_DATA_GET_SEGMENT_SIZE(buf);
	state->data_size = total_size;

	if (!state->data)
		state->data = (u8 *)PLTFM_MALLOC(state->data_size);

	if (seg_id == 0)
		state->seg_size = seg_size;

	PLTFM_MEMCPY(state->data + seg_id * state->seg_size,
		     buf + C2H_DATA_OFFSET_88XX, seg_size);

	if (PSD_DATA_GET_END_SEGMENT(buf) == 0)
		return HALMAC_RET_SUCCESS;

	proc_status = HALMAC_CMD_PROCESS_DONE;
	state->proc_status = proc_status;

	PLTFM_EVENT_SIG(HALMAC_FEATURE_PSD, proc_status, state->data,
			state->data_size);

	return HALMAC_RET_SUCCESS;
}

#endif /* HALMAC_88XX_SUPPORT */
