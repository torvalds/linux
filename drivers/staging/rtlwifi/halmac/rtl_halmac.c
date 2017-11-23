/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "halmac_api.h"
#include "rtl_halmac.h"
#include <linux/module.h>
#include <linux/vmalloc.h>

#define DEFAULT_INDICATOR_TIMELMT msecs_to_jiffies(1000) /* ms */
#define FIRMWARE_MAX_SIZE HALMAC_FW_SIZE_MAX_88XX

static struct rtl_halmac_ops rtl_halmac_operation = {
	.halmac_init_adapter = rtl_halmac_init_adapter,
	.halmac_deinit_adapter = rtl_halmac_deinit_adapter,
	.halmac_init_hal = rtl_halmac_init_hal,
	.halmac_deinit_hal = rtl_halmac_deinit_hal,
	.halmac_poweron = rtl_halmac_poweron,
	.halmac_poweroff = rtl_halmac_poweroff,

	.halmac_phy_power_switch = rtl_halmac_phy_power_switch,
	.halmac_set_mac_address = rtl_halmac_set_mac_address,
	.halmac_set_bssid = rtl_halmac_set_bssid,

	.halmac_get_physical_efuse_size = rtl_halmac_get_physical_efuse_size,
	.halmac_read_physical_efuse_map = rtl_halmac_read_physical_efuse_map,
	.halmac_get_logical_efuse_size = rtl_halmac_get_logical_efuse_size,
	.halmac_read_logical_efuse_map = rtl_halmac_read_logical_efuse_map,

	.halmac_set_bandwidth = rtl_halmac_set_bandwidth,

	.halmac_c2h_handle = rtl_halmac_c2h_handle,

	.halmac_chk_txdesc = rtl_halmac_chk_txdesc,
};

struct rtl_halmac_ops *rtl_halmac_get_ops_pointer(void)
{
	return &rtl_halmac_operation;
}
EXPORT_SYMBOL(rtl_halmac_get_ops_pointer);

/*
 * Driver API for HALMAC operations
 */

static u8 _halmac_reg_read_8(void *p, u32 offset)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p;

	return rtl_read_byte(rtlpriv, offset);
}

static u16 _halmac_reg_read_16(void *p, u32 offset)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p;

	return rtl_read_word(rtlpriv, offset);
}

static u32 _halmac_reg_read_32(void *p, u32 offset)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p;

	return rtl_read_dword(rtlpriv, offset);
}

static void _halmac_reg_write_8(void *p, u32 offset, u8 val)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p;

	rtl_write_byte(rtlpriv, offset, val);
}

static void _halmac_reg_write_16(void *p, u32 offset, u16 val)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p;

	rtl_write_word(rtlpriv, offset, val);
}

static void _halmac_reg_write_32(void *p, u32 offset, u32 val)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p;

	rtl_write_dword(rtlpriv, offset, val);
}

static bool _halmac_write_data_rsvd_page(void *p, u8 *buf, u32 size)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p;

	if (rtlpriv->cfg->ops->halmac_cb_write_data_rsvd_page &&
	    rtlpriv->cfg->ops->halmac_cb_write_data_rsvd_page(rtlpriv, buf,
							      size))
		return true;

	return false;
}

static bool _halmac_write_data_h2c(void *p, u8 *buf, u32 size)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p;

	if (rtlpriv->cfg->ops->halmac_cb_write_data_h2c &&
	    rtlpriv->cfg->ops->halmac_cb_write_data_h2c(rtlpriv, buf, size))
		return true;

	return false;
}

static const char *const RTL_HALMAC_FEATURE_NAME[] = {
	"HALMAC_FEATURE_CFG_PARA",
	"HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE",
	"HALMAC_FEATURE_DUMP_LOGICAL_EFUSE",
	"HALMAC_FEATURE_UPDATE_PACKET",
	"HALMAC_FEATURE_UPDATE_DATAPACK",
	"HALMAC_FEATURE_RUN_DATAPACK",
	"HALMAC_FEATURE_CHANNEL_SWITCH",
	"HALMAC_FEATURE_IQK",
	"HALMAC_FEATURE_POWER_TRACKING",
	"HALMAC_FEATURE_PSD",
	"HALMAC_FEATURE_ALL"};

static inline bool is_valid_id_status(struct rtl_priv *rtlpriv,
				      enum halmac_feature_id id,
				      enum halmac_cmd_process_status status)
{
	switch (id) {
	case HALMAC_FEATURE_CFG_PARA:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		if (status != HALMAC_CMD_PROCESS_DONE) {
			RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
				 "%s: <WARN> id(%d) unspecified status(%d)!\n",
				 __func__, id, status);
		}
		break;
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		if (status != HALMAC_CMD_PROCESS_DONE) {
			RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
				 "%s: <WARN> id(%d) unspecified status(%d)!\n",
				 __func__, id, status);
		}
		break;
	case HALMAC_FEATURE_UPDATE_PACKET:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_UPDATE_DATAPACK:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_RUN_DATAPACK:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_CHANNEL_SWITCH:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_IQK:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_POWER_TRACKING:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_PSD:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_ALL:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: %s\n", __func__,
			 RTL_HALMAC_FEATURE_NAME[id]);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
			 "%s: unknown feature id(%d)\n", __func__, id);
		return false;
	}

	return true;
}

static int init_halmac_event_with_waittime(struct rtl_priv *rtlpriv,
					   enum halmac_feature_id id, u8 *buf,
					   u32 size, u32 time)
{
	struct completion *comp;

	if (!rtlpriv->halmac.indicator[id].comp) {
		comp = kzalloc(sizeof(*comp), GFP_KERNEL);
		if (!comp)
			return -1;
	} else {
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
			 "%s: <WARN> id(%d) sctx is not NULL!!\n", __func__,
			 id);
		comp = rtlpriv->halmac.indicator[id].comp;
		rtlpriv->halmac.indicator[id].comp = NULL;
	}

	init_completion(comp);
	rtlpriv->halmac.indicator[id].wait_ms = time;

	rtlpriv->halmac.indicator[id].buffer = buf;
	rtlpriv->halmac.indicator[id].buf_size = size;
	rtlpriv->halmac.indicator[id].ret_size = 0;
	rtlpriv->halmac.indicator[id].status = 0;
	/* fill sctx at least to sure other variables are all ready! */
	rtlpriv->halmac.indicator[id].comp = comp;

	return 0;
}

static inline int init_halmac_event(struct rtl_priv *rtlpriv,
				    enum halmac_feature_id id, u8 *buf,
				    u32 size)
{
	return init_halmac_event_with_waittime(rtlpriv, id, buf, size,
					       DEFAULT_INDICATOR_TIMELMT);
}

static void free_halmac_event(struct rtl_priv *rtlpriv,
			      enum halmac_feature_id id)
{
	struct completion *comp;

	if (!rtlpriv->halmac.indicator[id].comp)
		return;

	comp = rtlpriv->halmac.indicator[id].comp;
	rtlpriv->halmac.indicator[id].comp = NULL;
	kfree(comp);
}

static int wait_halmac_event(struct rtl_priv *rtlpriv,
			     enum halmac_feature_id id)
{
	struct completion *comp;
	int ret;

	comp = rtlpriv->halmac.indicator[id].comp;
	if (!comp)
		return -1;

	ret = wait_for_completion_timeout(
		comp, rtlpriv->halmac.indicator[id].wait_ms);
	free_halmac_event(rtlpriv, id);
	if (ret > 0)
		return 0;

	return -1;
}

/*
 * Return:
 *	Always return true, HALMAC don't care the return value.
 */
static bool
_halmac_event_indication(void *p, enum halmac_feature_id feature_id,
			 enum halmac_cmd_process_status process_status, u8 *buf,
			 u32 size)
{
	struct rtl_priv *rtlpriv;
	struct rtl_halmac_indicator *tbl, *indicator;
	struct completion *comp;
	u32 cpsz;
	bool ret;

	rtlpriv = (struct rtl_priv *)p;
	tbl = rtlpriv->halmac.indicator;

	ret = is_valid_id_status(rtlpriv, feature_id, process_status);
	if (!ret)
		goto exit;

	indicator = &tbl[feature_id];
	indicator->status = process_status;
	indicator->ret_size = size;
	if (!indicator->comp) {
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
			 "%s: No feature id(%d) waiting!!\n", __func__,
			 feature_id);
		goto exit;
	}
	comp = indicator->comp;

	if (process_status == HALMAC_CMD_PROCESS_ERROR) {
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
			 "%s: Something wrong id(%d)!!\n", __func__,
			 feature_id);
		complete(comp); /* may provide error code */
		goto exit;
	}

	if (size > indicator->buf_size) {
		RT_TRACE(
			rtlpriv, COMP_HALMAC, DBG_LOUD,
			"%s: <WARN> id(%d) buffer is not enough(%d<%d), data will be truncated!\n",
			__func__, feature_id, indicator->buf_size, size);
		cpsz = indicator->buf_size;
	} else {
		cpsz = size;
	}

	if (cpsz && indicator->buffer)
		memcpy(indicator->buffer, buf, cpsz);

	complete(comp);

exit:
	return true;
}

static struct halmac_platform_api rtl_halmac_platform_api = {
	/* R/W register */
	.REG_READ_8 = _halmac_reg_read_8,
	.REG_READ_16 = _halmac_reg_read_16,
	.REG_READ_32 = _halmac_reg_read_32,
	.REG_WRITE_8 = _halmac_reg_write_8,
	.REG_WRITE_16 = _halmac_reg_write_16,
	.REG_WRITE_32 = _halmac_reg_write_32,

	/* Write data */
	/* impletement in HAL-IC level */
	.SEND_RSVD_PAGE = _halmac_write_data_rsvd_page,
	.SEND_H2C_PKT = _halmac_write_data_h2c,

	.EVENT_INDICATION = _halmac_event_indication,
};

static int init_priv(struct rtl_halmac *halmac)
{
	struct rtl_halmac_indicator *indicator;
	u32 count, size;

	halmac->send_general_info = 0;

	count = HALMAC_FEATURE_ALL + 1;
	size = sizeof(*indicator) * count;
	indicator = kzalloc(size, GFP_KERNEL);
	if (!indicator)
		return -1;
	halmac->indicator = indicator;

	return 0;
}

static void deinit_priv(struct rtl_halmac *halmac)
{
	struct rtl_halmac_indicator *indicator;

	indicator = halmac->indicator;
	halmac->indicator = NULL;
	kfree(indicator);
}

int rtl_halmac_init_adapter(struct rtl_priv *rtlpriv)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	enum halmac_interface intf;
	enum halmac_ret_status status;
	int err = 0;
	struct halmac_platform_api *pf_api = &rtl_halmac_platform_api;

	halmac = rtlpriv_to_halmac(rtlpriv);
	if (halmac) {
		err = 0;
		goto out;
	}

	err = init_priv(&rtlpriv->halmac);
	if (err)
		goto out;

	intf = HALMAC_INTERFACE_PCIE;
	status = halmac_init_adapter(rtlpriv, pf_api, intf, &halmac, &api);
	if (status != HALMAC_RET_SUCCESS) {
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
			 "%s: halmac_init_adapter fail!(status=%d)\n", __func__,
			 status);
		err = -1;
		goto out;
	}

	rtlpriv->halmac.internal = halmac;

out:
	if (err)
		rtl_halmac_deinit_adapter(rtlpriv);

	return err;
}

int rtl_halmac_deinit_adapter(struct rtl_priv *rtlpriv)
{
	struct halmac_adapter *halmac;
	enum halmac_ret_status status;
	int err = 0;

	halmac = rtlpriv_to_halmac(rtlpriv);
	if (!halmac) {
		err = 0;
		goto out;
	}

	deinit_priv(&rtlpriv->halmac);

	halmac_halt_api(halmac);

	status = halmac_deinit_adapter(halmac);
	rtlpriv->halmac.internal = NULL;
	if (status != HALMAC_RET_SUCCESS) {
		err = -1;
		goto out;
	}

out:
	return err;
}

int rtl_halmac_poweron(struct rtl_priv *rtlpriv)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	int err = -1;

	halmac = rtlpriv_to_halmac(rtlpriv);
	if (!halmac)
		goto out;

	api = HALMAC_GET_API(halmac);

	status = api->halmac_pre_init_system_cfg(halmac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	status = api->halmac_mac_power_switch(halmac, HALMAC_MAC_POWER_ON);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	status = api->halmac_init_system_cfg(halmac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

int rtl_halmac_poweroff(struct rtl_priv *rtlpriv)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	int err = -1;

	halmac = rtlpriv_to_halmac(rtlpriv);
	if (!halmac)
		goto out;

	api = HALMAC_GET_API(halmac);

	status = api->halmac_mac_power_switch(halmac, HALMAC_MAC_POWER_OFF);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/*
 * Note:
 *	When this function return, the register REG_RCR may be changed.
 */
int rtl_halmac_config_rx_info(struct rtl_priv *rtlpriv,
			      enum halmac_drv_info info)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	int err = -1;

	halmac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_cfg_drv_info(halmac, info);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

static enum halmac_ret_status init_mac_flow(struct rtl_priv *rtlpriv)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	u8 wifi_test = 0;
	int err;

	halmac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(halmac);

	if (wifi_test)
		status = api->halmac_init_mac_cfg(halmac, HALMAC_TRX_MODE_WMM);
	else
		status = api->halmac_init_mac_cfg(halmac,
						  HALMAC_TRX_MODE_NORMAL);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = rtl_halmac_rx_agg_switch(rtlpriv, true);
	if (err)
		goto out;

	if (rtlpriv->cfg->maps[RTL_RC_VHT_RATE_1SS_MCS7])
		status = api->halmac_cfg_operation_mode(
			halmac, HALMAC_WIRELESS_MODE_AC);
	else if (rtlpriv->cfg->maps[RTL_RC_HT_RATEMCS7])
		status = api->halmac_cfg_operation_mode(halmac,
							HALMAC_WIRELESS_MODE_N);
	else if (rtlpriv->cfg->maps[RTL_RC_OFDM_RATE6M])
		status = api->halmac_cfg_operation_mode(halmac,
							HALMAC_WIRELESS_MODE_G);
	else
		status = api->halmac_cfg_operation_mode(halmac,
							HALMAC_WIRELESS_MODE_B);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

out:
	return status;
}

static inline enum halmac_rf_type _rf_type_drv2halmac(enum rf_type rf_drv)
{
	enum halmac_rf_type rf_mac;

	switch (rf_drv) {
	case RF_1T2R:
		rf_mac = HALMAC_RF_1T2R;
		break;
	case RF_2T2R:
		rf_mac = HALMAC_RF_2T2R;
		break;
	case RF_1T1R:
		rf_mac = HALMAC_RF_1T1R;
		break;
	case RF_2T2R_GREEN:
		rf_mac = HALMAC_RF_2T2R_GREEN;
		break;
	default:
		rf_mac = (enum halmac_rf_type)rf_drv;
		break;
	}

	return rf_mac;
}

static int _send_general_info(struct rtl_priv *rtlpriv)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	struct halmac_general_info info;
	enum halmac_ret_status status;

	halmac = rtlpriv_to_halmac(rtlpriv);
	if (!halmac)
		return -1;
	api = HALMAC_GET_API(halmac);

	memset(&info, 0, sizeof(info));
	info.rfe_type = rtlpriv->rtlhal.rfe_type;
	info.rf_type = _rf_type_drv2halmac(rtlpriv->phy.rf_type);

	status = api->halmac_send_general_info(halmac, &info);
	switch (status) {
	case HALMAC_RET_SUCCESS:
		break;
	case HALMAC_RET_NO_DLFW:
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_WARNING,
			 "%s: halmac_send_general_info() fail because fw not dl!\n",
			 __func__);
	/* fall through */
	default:
		return -1;
	}

	return 0;
}

/*
 * Notices:
 *	Make sure
 *	1. rtl_hal_get_hwreg(HW_VAR_RF_TYPE)
 *	2. HAL_DATA_TYPE.rfe_type
 *	already ready for use before calling this function.
 */
static int _halmac_init_hal(struct rtl_priv *rtlpriv, u8 *fw, u32 fwsize)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	bool ok;
	bool fw_ok = false;
	int err, err_ret = -1;

	halmac = rtlpriv_to_halmac(rtlpriv);
	if (!halmac)
		goto out;
	api = HALMAC_GET_API(halmac);

	/* StatePowerOff */

	/* SKIP: halmac_init_adapter (Already done before) */

	/* halmac_pre_Init_system_cfg */
	/* halmac_mac_power_switch(on) */
	/* halmac_Init_system_cfg */
	err = rtl_halmac_poweron(rtlpriv);
	if (err)
		goto out;

	/* StatePowerOn */

	/* DownloadFW */
	rtlpriv->halmac.send_general_info = 0;
	if (fw && fwsize) {
		err = rtl_halmac_dlfw(rtlpriv, fw, fwsize);
		if (err)
			goto out;
		fw_ok = true;
	}

	/* InitMACFlow */
	status = init_mac_flow(rtlpriv);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	/* halmac_send_general_info */
	if (fw_ok) {
		rtlpriv->halmac.send_general_info = 0;
		err = _send_general_info(rtlpriv);
		if (err)
			goto out;
	} else {
		rtlpriv->halmac.send_general_info = 1;
	}

	/* Init Phy parameter-MAC */
	if (rtlpriv->cfg->ops->halmac_cb_init_mac_register)
		ok = rtlpriv->cfg->ops->halmac_cb_init_mac_register(rtlpriv);
	else
		ok = false;

	if (!ok)
		goto out;

	/* StateMacInitialized */

	/* halmac_cfg_drv_info */
	err = rtl_halmac_config_rx_info(rtlpriv, HALMAC_DRV_INFO_PHY_STATUS);
	if (err)
		goto out;

	/* halmac_set_hw_value(HALMAC_HW_EN_BB_RF) */
	/* Init BB, RF */
	if (rtlpriv->cfg->ops->halmac_cb_init_bb_rf_register)
		ok = rtlpriv->cfg->ops->halmac_cb_init_bb_rf_register(rtlpriv);
	else
		ok = false;

	if (!ok)
		goto out;

	status = api->halmac_init_interface_cfg(halmac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	/* SKIP: halmac_verify_platform_api */
	/* SKIP: halmac_h2c_lb */

	/* StateRxIdle */

	err_ret = 0;
out:
	return err_ret;
}

int rtl_halmac_init_hal(struct rtl_priv *rtlpriv)
{
	if (!rtlpriv->rtlhal.pfirmware || rtlpriv->rtlhal.fwsize == 0)
		return -1;

	return _halmac_init_hal(rtlpriv, rtlpriv->rtlhal.pfirmware,
				rtlpriv->rtlhal.fwsize);
}

int rtl_halmac_deinit_hal(struct rtl_priv *rtlpriv)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	int err = -1;

	halmac = rtlpriv_to_halmac(rtlpriv);
	if (!halmac)
		goto out;
	api = HALMAC_GET_API(halmac);

	status = api->halmac_deinit_interface_cfg(halmac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	/* rtw_hal_power_off(adapter); */
	status = api->halmac_mac_power_switch(halmac, HALMAC_MAC_POWER_OFF);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

int rtl_halmac_self_verify(struct rtl_priv *rtlpriv)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	int err = -1;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	status = api->halmac_verify_platform_api(mac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	status = api->halmac_h2c_lb(mac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

int rtl_halmac_dlfw(struct rtl_priv *rtlpriv, u8 *fw, u32 fwsize)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	struct halmac_fw_version fw_version;
	int err = 0;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	if ((!fw) || (!fwsize))
		return -1;

	/* 1. Driver Stop Tx */
	/* ToDo */

	/* 2. Driver Check Tx FIFO is empty */
	/* ToDo */

	/* 3. Config MAX download size */
	api->halmac_cfg_max_dl_size(mac, 0x1000);

	/* 4. Download Firmware */
	mac->h2c_packet_seq = 0;
	status = api->halmac_download_firmware(mac, fw, fwsize);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	status = api->halmac_get_fw_version(mac, &fw_version);
	if (status == HALMAC_RET_SUCCESS) {
		rtlpriv->rtlhal.fw_version = fw_version.version;
		rtlpriv->rtlhal.fw_subversion =
			(fw_version.sub_version << 8) | (fw_version.sub_index);

		RT_TRACE(
			rtlpriv, COMP_HALMAC, DBG_DMESG,
			"halmac report firmware version %04X.%04X\n",
			rtlpriv->rtlhal.fw_version,
			rtlpriv->rtlhal.fw_subversion);
	}

	if (rtlpriv->halmac.send_general_info) {
		rtlpriv->halmac.send_general_info = 0;
		err = _send_general_info(rtlpriv);
	}

	/* 5. Driver resume TX if needed */
	/* ToDo */

	/* 6. Reset driver variables if needed */
	/*hal->LastHMEBoxNum = 0;*/

	return err;
}

/*
 * Description:
 *	Power on/off BB/RF domain.
 *
 * Parameters:
 *	enable	true/false for power on/off
 *
 * Return:
 *	0	Success
 *	others	Fail
 */
int rtl_halmac_phy_power_switch(struct rtl_priv *rtlpriv, u8 enable)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	enum halmac_ret_status status;

	halmac = rtlpriv_to_halmac(rtlpriv);
	if (!halmac)
		return -1;
	api = HALMAC_GET_API(halmac);

	status = api->halmac_set_hw_value(halmac, HALMAC_HW_EN_BB_RF, &enable);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

static bool _is_fw_read_cmd_down(struct rtl_priv *rtlpriv, u8 msgbox_num)
{
	bool read_down = false;
	int retry_cnts = 100;
	u8 valid;

	RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
		 "%s, reg_1cc(%x), msg_box(%d)...\n", __func__,
		 rtl_read_byte(rtlpriv, REG_HMETFR), msgbox_num);

	do {
		valid = rtl_read_byte(rtlpriv, REG_HMETFR) & BIT(msgbox_num);
		if (valid == 0)
			read_down = true;
		else
			schedule();
	} while ((!read_down) && (retry_cnts--));

	return read_down;
}

int rtl_halmac_send_h2c(struct rtl_priv *rtlpriv, u8 *h2c)
{
	u8 h2c_box_num = 0;
	u32 msgbox_addr = 0;
	u32 msgbox_ex_addr = 0;
	__le32 h2c_cmd = 0;
	__le32 h2c_cmd_ex = 0;
	s32 ret = -1;
	unsigned long flag = 0;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	if (!h2c) {
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD, "%s: pbuf is NULL\n",
			 __func__);
		return ret;
	}

	spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);

	/* pay attention to if race condition happened in  H2C cmd setting */
	h2c_box_num = rtlhal->last_hmeboxnum;

	if (!_is_fw_read_cmd_down(rtlpriv, h2c_box_num)) {
		RT_TRACE(rtlpriv, COMP_HALMAC, DBG_LOUD,
			 " fw read cmd failed...\n");
		goto exit;
	}

	/* Write Ext command(byte 4 -7) */
	msgbox_ex_addr = REG_HMEBOX_E0 + (h2c_box_num * EX_MESSAGE_BOX_SIZE);
	memcpy((u8 *)(&h2c_cmd_ex), h2c + 4, EX_MESSAGE_BOX_SIZE);
	rtl_write_dword(rtlpriv, msgbox_ex_addr, le32_to_cpu(h2c_cmd_ex));

	/* Write command (byte 0 -3 ) */
	msgbox_addr = REG_HMEBOX0 + (h2c_box_num * MESSAGE_BOX_SIZE);
	memcpy((u8 *)(&h2c_cmd), h2c, 4);
	rtl_write_dword(rtlpriv, msgbox_addr, le32_to_cpu(h2c_cmd));

	/* update last msg box number */
	rtlhal->last_hmeboxnum = (h2c_box_num + 1) % MAX_H2C_BOX_NUMS;
	ret = 0;

exit:
	spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flag);
	return ret;
}

int rtl_halmac_c2h_handle(struct rtl_priv *rtlpriv, u8 *c2h, u32 size)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_c2h_info(mac, c2h, size);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

int rtl_halmac_get_physical_efuse_size(struct rtl_priv *rtlpriv, u32 *size)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	u32 val;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_efuse_size(mac, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;
	return 0;
}

int rtl_halmac_read_physical_efuse_map(struct rtl_priv *rtlpriv, u8 *map,
				       u32 size)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	enum halmac_feature_id id;
	int ret;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);
	id = HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE;

	ret = init_halmac_event(rtlpriv, id, map, size);
	if (ret)
		return -1;

	status = api->halmac_dump_efuse_map(mac, HALMAC_EFUSE_R_DRV);
	if (status != HALMAC_RET_SUCCESS) {
		free_halmac_event(rtlpriv, id);
		return -1;
	}

	ret = wait_halmac_event(rtlpriv, id);
	if (ret)
		return -1;

	return 0;
}

int rtl_halmac_read_physical_efuse(struct rtl_priv *rtlpriv, u32 offset,
				   u32 cnt, u8 *data)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	u8 v;
	u32 i;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	for (i = 0; i < cnt; i++) {
		status = api->halmac_read_efuse(mac, offset + i, &v);
		if (status != HALMAC_RET_SUCCESS)
			return -1;
		data[i] = v;
	}

	return 0;
}

int rtl_halmac_write_physical_efuse(struct rtl_priv *rtlpriv, u32 offset,
				    u32 cnt, u8 *data)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	u32 i;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	for (i = 0; i < cnt; i++) {
		status = api->halmac_write_efuse(mac, offset + i, data[i]);
		if (status != HALMAC_RET_SUCCESS)
			return -1;
	}

	return 0;
}

int rtl_halmac_get_logical_efuse_size(struct rtl_priv *rtlpriv, u32 *size)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	u32 val;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_logical_efuse_size(mac, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;
	return 0;
}

int rtl_halmac_read_logical_efuse_map(struct rtl_priv *rtlpriv, u8 *map,
				      u32 size)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	enum halmac_feature_id id;
	int ret;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);
	id = HALMAC_FEATURE_DUMP_LOGICAL_EFUSE;

	ret = init_halmac_event(rtlpriv, id, map, size);
	if (ret)
		return -1;

	status = api->halmac_dump_logical_efuse_map(mac, HALMAC_EFUSE_R_AUTO);
	if (status != HALMAC_RET_SUCCESS) {
		free_halmac_event(rtlpriv, id);
		return -1;
	}

	ret = wait_halmac_event(rtlpriv, id);
	if (ret)
		return -1;

	return 0;
}

int rtl_halmac_write_logical_efuse_map(struct rtl_priv *rtlpriv, u8 *map,
				       u32 size, u8 *maskmap, u32 masksize)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	struct halmac_pg_efuse_info pginfo;
	enum halmac_ret_status status;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	pginfo.efuse_map = map;
	pginfo.efuse_map_size = size;
	pginfo.efuse_mask = maskmap;
	pginfo.efuse_mask_size = masksize;

	status = api->halmac_pg_efuse_by_map(mac, &pginfo, HALMAC_EFUSE_R_AUTO);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

int rtl_halmac_read_logical_efuse(struct rtl_priv *rtlpriv, u32 offset, u32 cnt,
				  u8 *data)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	u8 v;
	u32 i;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	for (i = 0; i < cnt; i++) {
		status = api->halmac_read_logical_efuse(mac, offset + i, &v);
		if (status != HALMAC_RET_SUCCESS)
			return -1;
		data[i] = v;
	}

	return 0;
}

int rtl_halmac_write_logical_efuse(struct rtl_priv *rtlpriv, u32 offset,
				   u32 cnt, u8 *data)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	u32 i;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	for (i = 0; i < cnt; i++) {
		status = api->halmac_write_logical_efuse(mac, offset + i,
							 data[i]);
		if (status != HALMAC_RET_SUCCESS)
			return -1;
	}

	return 0;
}

int rtl_halmac_set_mac_address(struct rtl_priv *rtlpriv, u8 hwport, u8 *addr)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	u8 port;
	union halmac_wlan_addr hwa;
	enum halmac_ret_status status;
	int err = -1;

	halmac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(halmac);

	port = hwport;
	memset(&hwa, 0, sizeof(hwa));
	memcpy(hwa.address, addr, 6);

	status = api->halmac_cfg_mac_addr(halmac, port, &hwa);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

int rtl_halmac_set_bssid(struct rtl_priv *rtlpriv, u8 hwport, u8 *addr)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	u8 port;
	union halmac_wlan_addr hwa;
	enum halmac_ret_status status;
	int err = -1;

	halmac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(halmac);
	port = hwport;

	memset(&hwa, 0, sizeof(union halmac_wlan_addr));
	memcpy(hwa.address, addr, 6);
	status = api->halmac_cfg_bssid(halmac, port, &hwa);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

int rtl_halmac_set_bandwidth(struct rtl_priv *rtlpriv, u8 channel,
			     u8 pri_ch_idx, u8 bw)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	status = api->halmac_cfg_ch_bw(mac, channel, pri_ch_idx, bw);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

int rtl_halmac_get_hw_value(struct rtl_priv *rtlpriv, enum halmac_hw_id hw_id,
			    void *pvalue)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_hw_value(mac, hw_id, pvalue);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

int rtl_halmac_dump_fifo(struct rtl_priv *rtlpriv,
			 enum hal_fifo_sel halmac_fifo_sel)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;
	u8 *pfifo_map = NULL;
	u32 fifo_size = 0;
	s8 ret = 0;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	fifo_size = api->halmac_get_fifo_size(mac, halmac_fifo_sel);
	if (fifo_size)
		pfifo_map = vmalloc(fifo_size);
	if (!pfifo_map)
		return -1;

	status = api->halmac_dump_fifo(mac, halmac_fifo_sel, 0, fifo_size,
				       pfifo_map);

	if (status != HALMAC_RET_SUCCESS) {
		ret = -1;
		goto _exit;
	}

_exit:
	if (pfifo_map)
		vfree(pfifo_map);
	return ret;
}

int rtl_halmac_rx_agg_switch(struct rtl_priv *rtlpriv, bool enable)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;
	struct halmac_rxagg_cfg rxaggcfg;
	enum halmac_ret_status status;
	int err = -1;

	halmac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(halmac);
	memset((void *)&rxaggcfg, 0, sizeof(rxaggcfg));

	if (enable) {
		/* enable RX agg. */
		/* PCIE do nothing */
	} else {
		/* disable RX agg. */
		rxaggcfg.mode = HALMAC_RX_AGG_MODE_NONE;
	}

	status = api->halmac_cfg_rx_aggregation(halmac, &rxaggcfg);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

int rtl_halmac_get_wow_reason(struct rtl_priv *rtlpriv, u8 *reason)
{
	u8 val8;
	int err = -1;

	val8 = rtl_read_byte(rtlpriv, 0x1C7);
	if (val8 == 0xEA)
		goto out;

	*reason = val8;
	err = 0;
out:
	return err;
}

/*
 * Description:
 *	Get RX driver info size. RX driver info is a small memory space between
 *	scriptor and RX payload.
 *
 *	+-------------------------+
 *	| RX descriptor           |
 *	| usually 24 bytes        |
 *	+-------------------------+
 *	| RX driver info          |
 *	| depends on driver cfg   |
 *	+-------------------------+
 *	| RX paylad               |
 *	|                         |
 *	+-------------------------+
 *
 * Parameter:
 *	d	pointer to struct dvobj_priv of driver
 *	sz	rx driver info size in bytes.
 *
 * Rteurn:
 *	0	Success
 *	other	Fail
 */
int rtl_halmac_get_drv_info_sz(struct rtl_priv *rtlpriv, u8 *sz)
{
	/*	enum halmac_ret_status status; */
	u8 dw = 6; /* max number */

	*sz = dw * 8;
	return 0;
}

int rtl_halmac_get_rsvd_drv_pg_bndy(struct rtl_priv *rtlpriv, u16 *drv_pg)
{
	enum halmac_ret_status status;
	struct halmac_adapter *halmac = rtlpriv_to_halmac(rtlpriv);
	struct halmac_api *api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_RSVD_PG_BNDY,
					  drv_pg);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

int rtl_halmac_chk_txdesc(struct rtl_priv *rtlpriv, u8 *txdesc, u32 size)
{
	struct halmac_adapter *mac;
	struct halmac_api *api;
	enum halmac_ret_status status;

	mac = rtlpriv_to_halmac(rtlpriv);
	api = HALMAC_GET_API(mac);

	status = api->halmac_chk_txdesc(mac, txdesc, size);

	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.FInger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 802.11n PCI wireless core");
