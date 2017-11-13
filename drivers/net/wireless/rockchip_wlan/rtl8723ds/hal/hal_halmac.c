/******************************************************************************
 *
 * Copyright(c) 2015 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _HAL_HALMAC_C_

#include <drv_types.h>		/* PADAPTER, struct dvobj_priv, SDIO_ERR_VAL8 and etc. */
#include <hal_data.h>		/* efuse, PHAL_DATA_TYPE and etc. */
#include "hal_halmac.h"		/* dvobj_to_halmac() and ect. */

#define DEFAULT_INDICATOR_TIMELMT	1000	/* ms */
#define MSG_PREFIX			"[HALMAC]"

/*#define RTW_HALMAC_DLFW_MEM_NO_STOP_TX*/

/*
 * Driver API for HALMAC operations
 */

#ifdef CONFIG_SDIO_HCI
#include <rtw_sdio.h>

static u8 _halmac_mac_reg_page0_chk(const char *func, struct dvobj_priv *dvobj, u32 offset)
{
#if defined(CONFIG_IO_CHECK_IN_ANA_LOW_CLK) && defined(CONFIG_LPS_LCLK)
	struct pwrctrl_priv *pwrpriv = &dvobj->pwrctl_priv;
	u32 mac_reg_offset = 0;

	if (pwrpriv->pwr_mode == PS_MODE_ACTIVE)
		return _TRUE;

	if (pwrpriv->lps_level == LPS_NORMAL)
		return _TRUE;

	if (pwrpriv->rpwm >= PS_STATE_S2)
		return _TRUE;

	if (offset & (WLAN_IOREG_DEVICE_ID << 13))  { /*WLAN_IOREG_OFFSET*/
		mac_reg_offset = offset & HALMAC_WLAN_MAC_REG_MSK;
		if (mac_reg_offset < 0x100) {
			RTW_ERR(FUNC_ADPT_FMT
				"access MAC REG -0x%04x in PS-mode:0x%02x (rpwm:0x%02x, lps_level:0x%02x)\n",
				FUNC_ADPT_ARG(dvobj_get_primary_adapter(dvobj)), mac_reg_offset,
				pwrpriv->pwr_mode, pwrpriv->rpwm, pwrpriv->lps_level);
			rtw_warn_on(1);
			return _FALSE;
		}
	}
#endif
	return _TRUE;
}

static u8 _halmac_sdio_cmd52_read(void *p, u32 offset)
{
	struct dvobj_priv *d;
	u8 val;
	u8 ret;


	d = (struct dvobj_priv *)p;
	_halmac_mac_reg_page0_chk(__func__, d, offset);
	ret = rtw_sdio_read_cmd52(d, offset, &val, 1);
	if (_FAIL == ret) {
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
		return SDIO_ERR_VAL8;
	}

	return val;
}

static void _halmac_sdio_cmd52_write(void *p, u32 offset, u8 val)
{
	struct dvobj_priv *d;
	u8 ret;


	d = (struct dvobj_priv *)p;
	_halmac_mac_reg_page0_chk(__func__, d, offset);
	ret = rtw_sdio_write_cmd52(d, offset, &val, 1);
	if (_FAIL == ret)
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
}

static u8 _halmac_sdio_reg_read_8(void *p, u32 offset)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	u8 val;
	u8 ret;


	d = (struct dvobj_priv *)p;
	val = SDIO_ERR_VAL8;
	_halmac_mac_reg_page0_chk(__func__, d, offset);
	pbuf = rtw_zmalloc(1);
	if (!pbuf)
		return val;

	ret = rtw_sdio_read_cmd53(d, offset, pbuf, 1);
	if (ret == _FAIL) {
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
		goto exit;
	}

	val = *pbuf;

exit:
	rtw_mfree(pbuf, 1);

	return val;
}

static u16 _halmac_sdio_reg_read_16(void *p, u32 offset)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	u16 val;
	u8 ret;


	d = (struct dvobj_priv *)p;
	val = SDIO_ERR_VAL16;
	_halmac_mac_reg_page0_chk(__func__, d, offset);
	pbuf = rtw_zmalloc(2);
	if (!pbuf)
		return val;

	ret = rtw_sdio_read_cmd53(d, offset, pbuf, 2);
	if (ret == _FAIL) {
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
		goto exit;
	}

	val = le16_to_cpu(*(u16 *)pbuf);

exit:
	rtw_mfree(pbuf, 2);

	return val;
}

static u32 _halmac_sdio_reg_read_32(void *p, u32 offset)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	u32 val;
	u8 ret;


	d = (struct dvobj_priv *)p;
	val = SDIO_ERR_VAL32;
	_halmac_mac_reg_page0_chk(__func__, d, offset);
	pbuf = rtw_zmalloc(4);
	if (!pbuf)
		return val;

	ret = rtw_sdio_read_cmd53(d, offset, pbuf, 4);
	if (ret == _FAIL) {
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
		goto exit;
	}

	val = le32_to_cpu(*(u32 *)pbuf);

exit:
	rtw_mfree(pbuf, 4);

	return val;
}

static u8 _halmac_sdio_reg_read_n(void *p, u32 offset, u32 size, u8 *data)
{
	struct dvobj_priv *d = (struct dvobj_priv *)p;
	PSDIO_DATA psdio = &d->intf_data;
	u8 *pbuf;
	u8 ret;
	u8 rst = _FALSE;
	u32 sdio_read_size;


	sdio_read_size = RND4(size);
	if (sdio_read_size > psdio->block_transfer_len)
		sdio_read_size = _RND(sdio_read_size, psdio->block_transfer_len);

	pbuf = rtw_zmalloc(sdio_read_size);
	if ((!pbuf) || (!data))
		return rst;

	ret = rtw_sdio_read_cmd53(d, offset, pbuf, sdio_read_size);
	if (ret == _FAIL) {
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
		goto exit;
	}

	_rtw_memcpy(data, pbuf, size);
	rst = _TRUE;
exit:
	rtw_mfree(pbuf, sdio_read_size);

	return rst;
}

static void _halmac_sdio_reg_write_8(void *p, u32 offset, u8 val)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	u8 ret;


	d = (struct dvobj_priv *)p;
	_halmac_mac_reg_page0_chk(__func__, d, offset);
	pbuf = rtw_zmalloc(1);
	if (!pbuf)
		return;
	_rtw_memcpy(pbuf, &val, 1);

	ret = rtw_sdio_write_cmd53(d, offset, pbuf, 1);
	if (ret == _FAIL)
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);

	rtw_mfree(pbuf, 1);
}

static void _halmac_sdio_reg_write_16(void *p, u32 offset, u16 val)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	u8 ret;


	d = (struct dvobj_priv *)p;
	_halmac_mac_reg_page0_chk(__func__, d, offset);
	val = cpu_to_le16(val);
	pbuf = rtw_zmalloc(2);
	if (!pbuf)
		return;
	_rtw_memcpy(pbuf, &val, 2);

	ret = rtw_sdio_write_cmd53(d, offset, pbuf, 2);
	if (ret == _FAIL)
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);

	rtw_mfree(pbuf, 2);
}

static void _halmac_sdio_reg_write_32(void *p, u32 offset, u32 val)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	u8 ret;


	d = (struct dvobj_priv *)p;
	_halmac_mac_reg_page0_chk(__func__, d, offset);
	val = cpu_to_le32(val);
	pbuf = rtw_zmalloc(4);
	if (!pbuf)
		return;
	_rtw_memcpy(pbuf, &val, 4);

	ret = rtw_sdio_write_cmd53(d, offset, pbuf, 4);
	if (ret == _FAIL)
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);

	rtw_mfree(pbuf, 4);
}

#else /* !CONFIG_SDIO_HCI */

static u8 _halmac_reg_read_8(void *p, u32 offset)
{
	struct dvobj_priv *d;
	PADAPTER adapter;


	d = (struct dvobj_priv *)p;
	adapter = dvobj_get_primary_adapter(d);

	return rtw_read8(adapter, offset);
}

static u16 _halmac_reg_read_16(void *p, u32 offset)
{
	struct dvobj_priv *d;
	PADAPTER adapter;


	d = (struct dvobj_priv *)p;
	adapter = dvobj_get_primary_adapter(d);

	return rtw_read16(adapter, offset);
}

static u32 _halmac_reg_read_32(void *p, u32 offset)
{
	struct dvobj_priv *d;
	PADAPTER adapter;


	d = (struct dvobj_priv *)p;
	adapter = dvobj_get_primary_adapter(d);

	return rtw_read32(adapter, offset);
}

static void _halmac_reg_write_8(void *p, u32 offset, u8 val)
{
	struct dvobj_priv *d;
	PADAPTER adapter;
	int err;


	d = (struct dvobj_priv *)p;
	adapter = dvobj_get_primary_adapter(d);

	err = rtw_write8(adapter, offset, val);
	if (err == _FAIL)
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
}

static void _halmac_reg_write_16(void *p, u32 offset, u16 val)
{
	struct dvobj_priv *d;
	PADAPTER adapter;
	int err;


	d = (struct dvobj_priv *)p;
	adapter = dvobj_get_primary_adapter(d);

	err = rtw_write16(adapter, offset, val);
	if (err == _FAIL)
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
}

static void _halmac_reg_write_32(void *p, u32 offset, u32 val)
{
	struct dvobj_priv *d;
	PADAPTER adapter;
	int err;


	d = (struct dvobj_priv *)p;
	adapter = dvobj_get_primary_adapter(d);

	err = rtw_write32(adapter, offset, val);
	if (err == _FAIL)
		RTW_ERR("%s: I/O FAIL!\n", __FUNCTION__);
}
#endif /* !CONFIG_SDIO_HCI */

static u8 _halmac_mfree(void *p, void *buffer, u32 size)
{
	rtw_mfree(buffer, size);
	return _TRUE;
}

static void *_halmac_malloc(void *p, u32 size)
{
	return rtw_zmalloc(size);
}

static u8 _halmac_memcpy(void *p, void *dest, void *src, u32 size)
{
	_rtw_memcpy(dest, src, size);
	return _TRUE;
}

static u8 _halmac_memset(void *p, void *addr, u8 value, u32 size)
{
	_rtw_memset(addr, value, size);
	return _TRUE;
}

static void _halmac_udelay(void *p, u32 us)
{
	rtw_udelay_os(us);
}

static u8 _halmac_mutex_init(void *p, HALMAC_MUTEX *pMutex)
{
	_rtw_mutex_init(pMutex);
	return _TRUE;
}

static u8 _halmac_mutex_deinit(void *p, HALMAC_MUTEX *pMutex)
{
	_rtw_mutex_free(pMutex);
	return _TRUE;
}

static u8 _halmac_mutex_lock(void *p, HALMAC_MUTEX *pMutex)
{
	int err;

	err = _enter_critical_mutex(pMutex, NULL);
	if (err)
		return _FALSE;

	return _TRUE;
}

static u8 _halmac_mutex_unlock(void *p, HALMAC_MUTEX *pMutex)
{
	_exit_critical_mutex(pMutex, NULL);
	return _TRUE;
}

static u8 _halmac_msg_print(void *p, u32 msg_type, u8 msg_level, s8 *fmt, ...)
{
#define MSG_LEN		100
	va_list args;
	u8 str[MSG_LEN] = {0};
	int err;
	u8 ret = _TRUE;


	str[0] = '\n';
	va_start(args, fmt);
	err = vsnprintf(str, MSG_LEN, fmt, args);
	va_end(args);

	/* An output error is encountered */
	if (err < 0)
		return _FALSE;
	/* Output may be truncated due to size limit */
	if ((err == (MSG_LEN - 1)) && (str[MSG_LEN - 2] != '\n'))
		ret = _FALSE;

	if (msg_level == HALMAC_DBG_ALWAYS)
		RTW_PRINT(MSG_PREFIX "%s", str);
	else if (msg_level <= HALMAC_DBG_ERR)
		RTW_ERR(MSG_PREFIX "%s", str);
	else if (msg_level <= HALMAC_DBG_WARN)
		RTW_WARN(MSG_PREFIX "%s", str);
	else
		RTW_DBG(MSG_PREFIX "%s", str);

	return ret;
}

static u8 _halmac_buff_print(void *p, u32 msg_type, u8 msg_level, s8 *buf, u32 size)
{
	if (msg_level <= HALMAC_DBG_WARN)
		RTW_INFO_DUMP(MSG_PREFIX, buf, size);
	else
		RTW_DBG_DUMP(MSG_PREFIX, buf, size);

	return _TRUE;
}


const char *const RTW_HALMAC_FEATURE_NAME[] = {
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
	"HALMAC_FEATURE_ALL"
};

static inline u8 is_valid_id_status(HALMAC_FEATURE_ID id, HALMAC_CMD_PROCESS_STATUS status)
{
	switch (id) {
	case HALMAC_FEATURE_CFG_PARA:
		RTW_DBG("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		if (HALMAC_CMD_PROCESS_DONE != status)
			RTW_INFO("%s: id(%d) unspecified status(%d)!\n",
				 __FUNCTION__, id, status);
		break;
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		if (HALMAC_CMD_PROCESS_DONE != status)
			RTW_INFO("%s: id(%d) unspecified status(%d)!\n",
				 __FUNCTION__, id, status);
		break;
	case HALMAC_FEATURE_UPDATE_PACKET:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_UPDATE_DATAPACK:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_RUN_DATAPACK:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_CHANNEL_SWITCH:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_IQK:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_POWER_TRACKING:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_PSD:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_ALL:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	default:
		RTW_ERR("%s: unknown feature id(%d)\n", __FUNCTION__, id);
		return _FALSE;
	}

	return _TRUE;
}

static int init_halmac_event_with_waittime(struct dvobj_priv *d, HALMAC_FEATURE_ID id, u8 *buf, u32 size, u32 time)
{
	struct submit_ctx *sctx;


	if (!d->hmpriv.indicator[id].sctx) {
		sctx = (struct submit_ctx *)rtw_zmalloc(sizeof(*sctx));
		if (!sctx)
			return -1;
	} else {
		RTW_WARN("%s: id(%d) sctx is not NULL!!\n", __FUNCTION__, id);
		sctx = d->hmpriv.indicator[id].sctx;
		d->hmpriv.indicator[id].sctx = NULL;
	}

	rtw_sctx_init(sctx, time);
	d->hmpriv.indicator[id].buffer = buf;
	d->hmpriv.indicator[id].buf_size = size;
	d->hmpriv.indicator[id].ret_size = 0;
	d->hmpriv.indicator[id].status = 0;
	/* fill sctx at least to sure other variables are all ready! */
	d->hmpriv.indicator[id].sctx = sctx;

	return 0;
}

static inline int init_halmac_event(struct dvobj_priv *d, HALMAC_FEATURE_ID id, u8 *buf, u32 size)
{
	return init_halmac_event_with_waittime(d, id, buf, size, DEFAULT_INDICATOR_TIMELMT);
}

static void free_halmac_event(struct dvobj_priv *d, HALMAC_FEATURE_ID id)
{
	struct submit_ctx *sctx;


	if (!d->hmpriv.indicator[id].sctx)
		return;

	sctx = d->hmpriv.indicator[id].sctx;
	d->hmpriv.indicator[id].sctx = NULL;
	rtw_mfree((u8 *)sctx, sizeof(*sctx));
}

static int wait_halmac_event(struct dvobj_priv *d, HALMAC_FEATURE_ID id)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	struct submit_ctx *sctx;
	int ret;


	sctx = d->hmpriv.indicator[id].sctx;
	if (!sctx)
		return -1;

	ret = rtw_sctx_wait(sctx, RTW_HALMAC_FEATURE_NAME[id]);
	free_halmac_event(d, id);
	if (_SUCCESS == ret)
		return 0;

	/* timeout! We have to reset halmac state */
	RTW_ERR("%s: Wait id(%d, %s) TIMEOUT! Reset HALMAC state!\n",
		__FUNCTION__, id, RTW_HALMAC_FEATURE_NAME[id]);
	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	api->halmac_reset_feature(mac, id);

	return -1;
}

/*
 * Return:
 *	Always return _TRUE, HALMAC don't care the return value.
 */
static u8 _halmac_event_indication(void *p, HALMAC_FEATURE_ID feature_id, HALMAC_CMD_PROCESS_STATUS process_status, u8 *buf, u32 size)
{
	struct dvobj_priv *d;
	PADAPTER adapter;
	PHAL_DATA_TYPE hal;
	struct halmac_indicator *tbl, *indicator;
	struct submit_ctx *sctx;
	u32 cpsz;
	u8 ret;


	d = (struct dvobj_priv *)p;
	adapter = dvobj_get_primary_adapter(d);
	hal = GET_HAL_DATA(adapter);
	tbl = d->hmpriv.indicator;

	/* Filter(Skip) middle status indication */
	ret = is_valid_id_status(feature_id, process_status);
	if (_FALSE == ret)
		goto exit;

	indicator = &tbl[feature_id];
	indicator->status = process_status;
	indicator->ret_size = size;
	if (!indicator->sctx) {
		RTW_WARN("%s: No feature id(%d, %s) waiting!!\n", __FUNCTION__, feature_id, RTW_HALMAC_FEATURE_NAME[feature_id]);
		goto exit;
	}
	sctx = indicator->sctx;

	if (HALMAC_CMD_PROCESS_ERROR == process_status) {
		RTW_ERR("%s: Something wrong id(%d, %s)!!\n", __FUNCTION__, feature_id, RTW_HALMAC_FEATURE_NAME[feature_id]);
		rtw_sctx_done_err(&sctx, RTW_SCTX_DONE_UNKNOWN);
		goto exit;
	}

	if (size > indicator->buf_size) {
		RTW_WARN("%s: id(%d, %s) buffer is not enough(%d<%d), data will be truncated!\n",
			 __FUNCTION__, feature_id, RTW_HALMAC_FEATURE_NAME[feature_id], indicator->buf_size, size);
		cpsz = indicator->buf_size;
	} else {
		cpsz = size;
	}
	if (cpsz && indicator->buffer)
		_rtw_memcpy(indicator->buffer, buf, cpsz);

	rtw_sctx_done(&sctx);

exit:
	return _TRUE;
}

HALMAC_PLATFORM_API rtw_halmac_platform_api = {
	/* R/W register */
#ifdef CONFIG_SDIO_HCI
	.SDIO_CMD52_READ = _halmac_sdio_cmd52_read,
	.SDIO_CMD53_READ_8 = _halmac_sdio_reg_read_8,
	.SDIO_CMD53_READ_16 = _halmac_sdio_reg_read_16,
	.SDIO_CMD53_READ_32 = _halmac_sdio_reg_read_32,
	.SDIO_CMD53_READ_N = _halmac_sdio_reg_read_n,
	.SDIO_CMD52_WRITE = _halmac_sdio_cmd52_write,
	.SDIO_CMD53_WRITE_8 = _halmac_sdio_reg_write_8,
	.SDIO_CMD53_WRITE_16 = _halmac_sdio_reg_write_16,
	.SDIO_CMD53_WRITE_32 = _halmac_sdio_reg_write_32,

#endif /* CONFIG_SDIO_HCI */
#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCIE_HCI)
	.REG_READ_8 = _halmac_reg_read_8,
	.REG_READ_16 = _halmac_reg_read_16,
	.REG_READ_32 = _halmac_reg_read_32,
	.REG_WRITE_8 = _halmac_reg_write_8,
	.REG_WRITE_16 = _halmac_reg_write_16,
	.REG_WRITE_32 = _halmac_reg_write_32,
#endif /* CONFIG_USB_HCI || CONFIG_PCIE_HCI */

	/* Write data */
#if 0
	/* impletement in HAL-IC level */
	.SEND_RSVD_PAGE = sdio_write_data_rsvd_page,
	.SEND_H2C_PKT = sdio_write_data_h2c,
#endif
	/* Memory allocate */
	.RTL_FREE = _halmac_mfree,
	.RTL_MALLOC = _halmac_malloc,
	.RTL_MEMCPY = _halmac_memcpy,
	.RTL_MEMSET = _halmac_memset,

	/* Sleep */
	.RTL_DELAY_US = _halmac_udelay,

	/* Process Synchronization */
	.MUTEX_INIT = _halmac_mutex_init,
	.MUTEX_DEINIT = _halmac_mutex_deinit,
	.MUTEX_LOCK = _halmac_mutex_lock,
	.MUTEX_UNLOCK = _halmac_mutex_unlock,

	.MSG_PRINT = _halmac_msg_print,
	.BUFF_PRINT = _halmac_buff_print,
	.EVENT_INDICATION = _halmac_event_indication,
};

u8 rtw_halmac_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;


	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	return api->halmac_reg_read_8(mac, addr);
}

u16 rtw_halmac_read16(struct intf_hdl *pintfhdl, u32 addr)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;


	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	return api->halmac_reg_read_16(mac, addr);
}

u32 rtw_halmac_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;


	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	return api->halmac_reg_read_32(mac, addr);
}

static void _read_register(struct dvobj_priv *d, u32 addr, u32 cnt, u8 *buf)
{
#if 1
	struct _ADAPTER *a;
	u32 i, n;
	u16 val16;
	u32 val32;


	a = dvobj_get_primary_adapter(d);

	i = addr & 0x3;
	/* Handle address not start from 4 bytes alignment case */
	if (i) {
		val32 = cpu_to_le32(rtw_read32(a, addr & ~0x3));
		n = 4 - i;
		_rtw_memcpy(buf, ((u8 *)&val32) + i, n);
		i = n;
		cnt -= n;
	}

	while (cnt) {
		if (cnt >= 4)
			n = 4;
		else if (cnt >= 2)
			n = 2;
		else
			n = 1;
		cnt -= n;

		switch (n) {
		case 1:
			buf[i] = rtw_read8(a, addr+i);
			i++;
			break;
		case 2:
			val16 = cpu_to_le16(rtw_read16(a, addr+i));
			_rtw_memcpy(&buf[i], &val16, 2);
			i += 2;
			break;
		case 4:
			val32 = cpu_to_le32(rtw_read32(a, addr+i));
			_rtw_memcpy(&buf[i], &val32, 4);
			i += 4;
			break;
		}
	}
#else
	struct _ADAPTER *a;
	u32 i;


	a = dvobj_get_primary_adapter(d);
	for (i = 0; i < cnt; i++)
		buf[i] = rtw_read8(a, addr + i);
#endif
}

#ifdef CONFIG_SDIO_HCI
static int _sdio_read_local(struct dvobj_priv *d, u32 addr, u32 cnt, u8 *buf)
{
	struct _HALMAC_ADAPTER *mac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;


	if (buf == NULL)
		return -1;

	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_reg_sdio_cmd53_read_n(mac, addr, cnt, buf);
	if (status != HALMAC_RET_SUCCESS) {
		RTW_ERR("%s: addr=0x%08x cnt=%d err=%d\n",
			__FUNCTION__, addr, cnt, status);
		return -1;
	}

	return 0;
}
#endif /* CONFIG_SDIO_HCI */

void rtw_halmac_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem)
{
	struct dvobj_priv *d;


	if (pmem == NULL) {
		RTW_ERR("pmem is NULL\n");
		return;
	}

	d = pintfhdl->pintf_dev;

#ifdef CONFIG_SDIO_HCI
	if (addr & 0xFFFF0000) {
		int err = 0;

		err = _sdio_read_local(d, addr, cnt, pmem);
		if (!err)
			return;
	}
#endif /* CONFIG_SDIO_HCI */

	_read_register(d, addr, cnt, pmem);
}

#ifdef CONFIG_SDIO_INDIRECT_ACCESS
u8 rtw_halmac_iread8(struct intf_hdl *pintfhdl, u32 addr)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;

	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	/*return api->halmac_reg_read_indirect_8(mac, addr);*/
	return api->halmac_reg_read_8(mac, addr);
}

u16 rtw_halmac_iread16(struct intf_hdl *pintfhdl, u32 addr)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	u16 val16 = 0;

	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	/*return api->halmac_reg_read_indirect_16(mac, addr);*/
	return api->halmac_reg_read_16(mac, addr);
}

u32 rtw_halmac_iread32(struct intf_hdl *pintfhdl, u32 addr)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;


	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	return api->halmac_reg_read_indirect_32(mac, addr);
}
#endif /* CONFIG_SDIO_INDIRECT_ACCESS */

int rtw_halmac_write8(struct intf_hdl *pintfhdl, u32 addr, u8 value)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	status = api->halmac_reg_write_8(mac, addr, value);

	if (status == HALMAC_RET_SUCCESS)
		return 0;

	return -1;
}

int rtw_halmac_write16(struct intf_hdl *pintfhdl, u32 addr, u16 value)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	status = api->halmac_reg_write_16(mac, addr, value);

	if (status == HALMAC_RET_SUCCESS)
		return 0;

	return -1;
}

int rtw_halmac_write32(struct intf_hdl *pintfhdl, u32 addr, u32 value)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	status = api->halmac_reg_write_32(mac, addr, value);

	if (status == HALMAC_RET_SUCCESS)
		return 0;

	return -1;
}

static int init_write_rsvd_page_size(struct dvobj_priv *d)
{
	struct _HALMAC_ADAPTER *mac;
	struct _HALMAC_API *api;
	u32 size = 0;
	struct _HALMAC_OFLD_FUNC_INFO ofld_info;
	enum _HALMAC_RET_STATUS status;
	int err = 0;


#ifdef CONFIG_USB_HCI
	/* for USB do not exceed MAX_CMDBUF_SZ */
	size = 0x1000;
#elif defined(CONFIG_PCIE_HCI)
	size = MAX_CMDBUF_SZ - TXDESC_OFFSET;
#elif defined(CONFIG_SDIO_HCI)
	size = 0x7000; /* 28KB */
#endif

	/* If size==0, use HALMAC default setting and don't call any function */
	if (!size)
		return 0;

	err = rtw_halmac_set_max_dl_fw_size(d, size);
	if (err) {
		RTW_ERR("%s: Fail to set max download fw size!\n", __FUNCTION__);
		return -1;
	}

	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	_rtw_memset(&ofld_info, 0, sizeof(ofld_info));
	ofld_info.halmac_malloc_max_sz = 0xFFFFFFFF;
	ofld_info.rsvd_pg_drv_buf_max_sz = size;
	status = api->halmac_ofld_func_cfg(mac, &ofld_info);
	if (status != HALMAC_RET_SUCCESS) {
		RTW_ERR("%s: Fail to config offload parameters!\n", __FUNCTION__);
		return -1;
	}

	return 0;
}

static int init_priv(struct halmacpriv *priv)
{
	struct halmac_indicator *indicator;
	u32 count, size;


	if (priv->indicator)
		RTW_WARN("%s: HALMAC private data is not CLEAR!\n", __FUNCTION__);
	count = HALMAC_FEATURE_ALL + 1;
	size = sizeof(*indicator) * count;
	indicator = (struct halmac_indicator *)rtw_zmalloc(size);
	if (!indicator)
		return -1;
	priv->indicator = indicator;

	return 0;
}

static void deinit_priv(struct halmacpriv *priv)
{
	struct halmac_indicator *indicator;


	indicator = priv->indicator;
	priv->indicator = NULL;
	if (indicator) {
		u32 count, size;

		count = HALMAC_FEATURE_ALL + 1;
#ifdef CONFIG_RTW_DEBUG
		{
			struct submit_ctx *sctx;
			u32 i;

			for (i = 0; i < count; i++) {
				if (!indicator[i].sctx)
					continue;

				RTW_WARN("%s: %s id(%d) sctx still exist!!\n",
					__FUNCTION__, RTW_HALMAC_FEATURE_NAME[i], i);
				sctx = indicator[i].sctx;
				indicator[i].sctx = NULL;
				rtw_mfree((u8 *)sctx, sizeof(*sctx));
			}
		}
#endif /* !CONFIG_RTW_DEBUG */
		size = sizeof(*indicator) * count;
		rtw_mfree((u8 *)indicator, size);
	}
}

#ifdef CONFIG_SDIO_HCI
static enum _HALMAC_SDIO_SPEC_VER _sdio_ver_drv2halmac(struct dvobj_priv *d)
{
	bool v3;
	enum _HALMAC_SDIO_SPEC_VER ver;


	v3 = rtw_is_sdio30(dvobj_get_primary_adapter(d));
	if (v3)
		ver = HALMAC_SDIO_SPEC_VER_3_00;
	else
		ver = HALMAC_SDIO_SPEC_VER_2_00;

	return ver;
}
#endif /* CONFIG_SDIO_HCI */

void rtw_halmac_get_version(char *str, u32 len)
{
	HALMAC_RET_STATUS status;
	HALMAC_VER ver;


	status = halmac_get_version(&ver);
	if (status != HALMAC_RET_SUCCESS)
		return;

	rtw_sprintf(str, len, "V%d_%02d_%02d",
		    ver.major_ver, ver.prototype_ver, ver.minor_ver);
}

int rtw_halmac_init_adapter(struct dvobj_priv *d, PHALMAC_PLATFORM_API pf_api)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_INTERFACE intf;
	HALMAC_RET_STATUS status;
	int err = 0;
#ifdef CONFIG_SDIO_HCI
	HALMAC_SDIO_HW_INFO info;
#endif /* CONFIG_SDIO_HCI */


	halmac = dvobj_to_halmac(d);
	if (halmac) {
		err = 0;
		goto out;
	}

	err = init_priv(&d->hmpriv);
	if (err)
		goto out;

#ifdef CONFIG_SDIO_HCI
	intf = HALMAC_INTERFACE_SDIO;
#elif defined(CONFIG_USB_HCI)
	intf = HALMAC_INTERFACE_USB;
#elif defined(CONFIG_PCIE_HCI)
	intf = HALMAC_INTERFACE_PCIE;
#else
#warning "INTERFACE(CONFIG_XXX_HCI) not be defined!!"
	intf = HALMAC_INTERFACE_UNDEFINE;
#endif
	status = halmac_init_adapter(d, pf_api, intf, &halmac, &api);
	if (HALMAC_RET_SUCCESS != status) {
		RTW_ERR("%s: halmac_init_adapter fail!(status=%d)\n", __FUNCTION__, status);
		err = -1;
		goto out;
	}

	dvobj_set_halmac(d, halmac);

	status = api->halmac_interface_integration_tuning(halmac);
	if (status != HALMAC_RET_SUCCESS) {
		RTW_ERR("%s: halmac_interface_integration_tuning fail!(status=%d)\n", __FUNCTION__, status);
		err = -1;
		goto out;
	}

	status = api->halmac_phy_cfg(halmac, HALMAC_INTF_PHY_PLATFORM_ALL);
	if (status != HALMAC_RET_SUCCESS) {
		RTW_ERR("%s: halmac_phy_cfg fail!(status=%d)\n", __FUNCTION__, status);
		err = -1;
		goto out;
	}

	init_write_rsvd_page_size(d);

#ifdef CONFIG_SDIO_HCI
	info.spec_ver = _sdio_ver_drv2halmac(d);
	/* clock unit is MHz */
	info.clock_speed = RTW_DIV_ROUND_UP(rtw_sdio_get_clock(d), 1000000);
	RTW_DBG("%s: SDIO clock=%uMHz ver=%u\n", __FUNCTION__, info.clock_speed, info.spec_ver+2);
	status = api->halmac_sdio_hw_info(halmac, &info);
	if (status != HALMAC_RET_SUCCESS) {
		RTW_ERR("%s: halmac_sdio_hw_info fail!(status=%d)\n", __FUNCTION__, status);
		err = -1;
		goto out;
	}
#endif /* CONFIG_SDIO_HCI */

out:
	if (err)
		rtw_halmac_deinit_adapter(d);

	return err;
}

int rtw_halmac_deinit_adapter(struct dvobj_priv *d)
{
	PHALMAC_ADAPTER halmac;
	HALMAC_RET_STATUS status;
	int err = 0;


	halmac = dvobj_to_halmac(d);
	if (!halmac) {
		err = 0;
		goto out;
	}

	deinit_priv(&d->hmpriv);

	status = halmac_deinit_adapter(halmac);
	dvobj_set_halmac(d, NULL);
	if (status != HALMAC_RET_SUCCESS) {
		err = -1;
		goto out;
	}

out:
	return err;
}

static inline u8 _hw_port_drv2halmac(enum _hw_port hwport)
{
	u8 port = 0;


	switch (hwport) {
	case HW_PORT0:
		port = 0;
		break;
	case HW_PORT1:
		port = 1;
		break;
	case HW_PORT2:
		port = 2;
		break;
	case HW_PORT3:
		port = 3;
		break;
	case HW_PORT4:
		port = 4;
		break;
	default:
		port = hwport;
		break;
	}

	return port;
}

static enum _HALMAC_NETWORK_TYPE_SELECT _network_type_drv2halmac(u8 type)
{
	enum _HALMAC_NETWORK_TYPE_SELECT network = HALMAC_NETWORK_UNDEFINE;


	switch (type) {
	case _HW_STATE_NOLINK_:
	case _HW_STATE_MONITOR_:
		network = HALMAC_NETWORK_NO_LINK;
		break;

	case _HW_STATE_ADHOC_:
		network = HALMAC_NETWORK_ADHOC;
		break;

	case _HW_STATE_STATION_:
		network = HALMAC_NETWORK_INFRASTRUCTURE;
		break;

	case _HW_STATE_AP_:
		network = HALMAC_NETWORK_AP;
		break;
	}

	return network;
}

static u8 _network_type_halmac2drv(enum _HALMAC_NETWORK_TYPE_SELECT network)
{
	u8 type = _HW_STATE_NOLINK_;


	switch (network) {
	case HALMAC_NETWORK_NO_LINK:
	case HALMAC_NETWORK_UNDEFINE:
		type = _HW_STATE_NOLINK_;
		break;

	case HALMAC_NETWORK_ADHOC:
		type = _HW_STATE_ADHOC_;
		break;

	case HALMAC_NETWORK_INFRASTRUCTURE:
		type = _HW_STATE_STATION_;
		break;

	case HALMAC_NETWORK_AP:
		type = _HW_STATE_AP_;
		break;
	}

	return type;
}

static void _beacon_ctrl_halmac2drv(struct HALMAC_BCN_CTRL *ctrl,
				struct rtw_halmac_bcn_ctrl *drv_ctrl)
{
	drv_ctrl->rx_bssid_fit = ctrl->dis_rx_bssid_fit ? 0 : 1;
	drv_ctrl->txbcn_rpt = ctrl->en_txbcn_rpt ? 1 : 0;
	drv_ctrl->tsf_update = ctrl->dis_tsf_udt ? 0 : 1;
	drv_ctrl->enable_bcn = ctrl->en_bcn ? 1 : 0;
	drv_ctrl->rxbcn_rpt = ctrl->ex_rxbcn_rpt ? 1 : 0;
	drv_ctrl->p2p_ctwin = ctrl->en_p2p_ctwin ? 1 : 0;
	drv_ctrl->p2p_bcn_area = ctrl->en_p2p_bcn_area ? 1 : 0;
}

static void _beacon_ctrl_drv2halmac(struct rtw_halmac_bcn_ctrl *drv_ctrl,
				struct HALMAC_BCN_CTRL *ctrl)
{
	ctrl->dis_rx_bssid_fit = drv_ctrl->rx_bssid_fit ? 0 : 1;
	ctrl->en_txbcn_rpt = drv_ctrl->txbcn_rpt ? 1 : 0;
	ctrl->dis_tsf_udt = drv_ctrl->tsf_update ? 0 : 1;
	ctrl->en_bcn = drv_ctrl->enable_bcn ? 1 : 0;
	ctrl->ex_rxbcn_rpt = drv_ctrl->rxbcn_rpt ? 1 : 0;
	ctrl->en_p2p_ctwin = drv_ctrl->p2p_ctwin ? 1 : 0;
	ctrl->en_p2p_bcn_area = drv_ctrl->p2p_bcn_area ? 1 : 0;
}

int rtw_halmac_get_hw_value(struct dvobj_priv *d, HALMAC_HW_ID hw_id, VOID *pvalue)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_hw_value(mac, hw_id, pvalue);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	return 0;
}

/**
 * rtw_halmac_get_tx_fifo_size() - TX FIFO size
 * @d:		struct dvobj_priv*
 * @size:	TX FIFO size, unit is byte.
 *
 * Get TX FIFO size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_tx_fifo_size(struct dvobj_priv *d, u32 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u32 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_TXFIFO_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
}

/**
 * rtw_halmac_get_rx_fifo_size() - RX FIFO size
 * @d:		struct dvobj_priv*
 * @size:	RX FIFO size, unit is byte
 *
 * Get RX FIFO size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_rx_fifo_size(struct dvobj_priv *d, u32 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u32 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_RXFIFO_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
}

/**
 * rtw_halmac_get_rsvd_drv_pg_bndy() - Reserve page boundary of driver
 * @d:		struct dvobj_priv*
 * @size:	Page size, unit is byte
 *
 * Get reserve page boundary of driver from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_rsvd_drv_pg_bndy(struct dvobj_priv *d, u16 *bndy)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u16 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_RSVD_PG_BNDY, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*bndy = val;

	return 0;
}

/**
 * rtw_halmac_get_page_size() - Page size
 * @d:		struct dvobj_priv*
 * @size:	Page size, unit is byte
 *
 * Get TX/RX page size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_page_size(struct dvobj_priv *d, u32 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u32 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_PAGE_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
}

/**
 * rtw_halmac_get_tx_agg_align_size() - TX aggregation align size
 * @d:		struct dvobj_priv*
 * @size:	TX aggregation align size, unit is byte
 *
 * Get TX aggregation align size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_tx_agg_align_size(struct dvobj_priv *d, u16 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u16 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_TX_AGG_ALIGN_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
}

/**
 * rtw_halmac_get_rx_agg_align_size() - RX aggregation align size
 * @d:		struct dvobj_priv*
 * @size:	RX aggregation align size, unit is byte
 *
 * Get RX aggregation align size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_rx_agg_align_size(struct dvobj_priv *d, u8 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u8 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_RX_AGG_ALIGN_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
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
int rtw_halmac_get_rx_drv_info_sz(struct dvobj_priv *d, u8 *sz)
{
	HALMAC_RET_STATUS status;
	PHALMAC_ADAPTER halmac = dvobj_to_halmac(d);
	PHALMAC_API api = HALMAC_GET_API(halmac);
	u8 dw = 0;

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_DRV_INFO_SIZE, &dw);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*sz = dw * 8;
	return 0;
}

/**
 * rtw_halmac_get_tx_desc_size() - TX descriptor size
 * @d:		struct dvobj_priv*
 * @size:	TX descriptor size, unit is byte.
 *
 * Get TX descriptor size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_tx_desc_size(struct dvobj_priv *d, u32 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u32 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_TX_DESC_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
}

/**
 * rtw_halmac_get_rx_desc_size() - RX descriptor size
 * @d:		struct dvobj_priv*
 * @size:	RX descriptor size, unit is byte.
 *
 * Get RX descriptor size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_rx_desc_size(struct dvobj_priv *d, u32 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u32 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_RX_DESC_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
}


/**
 * rtw_halmac_get_fw_max_size() - Firmware MAX size
 * @d:		struct dvobj_priv*
 * @size:	MAX Firmware size, unit is byte.
 *
 * Get Firmware MAX size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
static int rtw_halmac_get_fw_max_size(struct dvobj_priv *d, u32 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u32 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_FW_MAX_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
}

/**
 * rtw_halmac_get_ori_h2c_size() - Original H2C MAX size
 * @d:		struct dvobj_priv*
 * @size:	H2C MAX size, unit is byte.
 *
 * Get original H2C MAX size(byte) from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_ori_h2c_size(struct dvobj_priv *d, u32 *size)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u32 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_ORI_H2C_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;

	return 0;
}

int rtw_halmac_get_oqt_size(struct dvobj_priv *d, u8 *size)
{
	enum _HALMAC_RET_STATUS status;
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 val;


	if (!size)
		return -1;

	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_AC_OQT_SIZE, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*size = val;
	return 0;
}

int rtw_halmac_get_ac_queue_number(struct dvobj_priv *d, u8 *num)
{
	enum _HALMAC_RET_STATUS status;
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 val;


	if (!num)
		return -1;

	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_AC_QUEUE_NUM, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*num = val;
	return 0;
}

/**
 * rtw_halmac_get_mac_address() - Get MAC address of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @addr:	buffer for storing MAC address
 *
 * Get MAC address of specific port from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_mac_address(struct dvobj_priv *d, enum _hw_port hwport, u8 *addr)
{
#if 0
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
#endif
	u8 port;
	union _HALMAC_WLAN_ADDR hwa;
#if 0
	enum _HALMAC_RET_STATUS status;
#endif
	int err = -1;


	if (!addr)
		goto out;
#if 0
	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
#endif
	port = _hw_port_drv2halmac(hwport);

	_rtw_memset(&hwa, 0, sizeof(hwa));

#if 0
	status = api->halmac_get_mac_addr(halmac, port, &hwa);
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#else
	{
	u32 offset;

	switch (port) {
	case 0:
		offset = REG_MACID;
		break;

	case 1:
		offset = REG_MACID1;
		break;

	case 2:
		offset = REG_MACID2;
		break;

	case 3:
		offset = REG_MACID3;
		break;

	case 4:
		offset = REG_MACID4;
		break;

	default:
		goto out;
	}

	_read_register(d, offset, 6, hwa.Address);
	}
#endif
	_rtw_memcpy(addr, hwa.Address, 6);

	err = 0;
out:
	return err;
}

/**
 * rtw_halmac_get_network_type() - Get network type of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @type:	buffer to put network type (_HW_STATE_*)
 *
 * Get network type of specific port from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_network_type(struct dvobj_priv *d, enum _hw_port hwport, u8 *type)
{
#if 0
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 port;
	enum _HALMAC_NETWORK_TYPE_SELECT network;
	enum _HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);
	network = HALMAC_NETWORK_UNDEFINE;

	status = api->halmac_get_net_type(halmac, port, &network);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	*type = _network_type_halmac2drv(network);

	err = 0;
out:
	return err;
#else
	struct _ADAPTER *a;
	u8 port;
	enum _HALMAC_NETWORK_TYPE_SELECT network;
	u32 val;
	int err = -1;


	a = dvobj_get_primary_adapter(d);
	port = _hw_port_drv2halmac(hwport);
	network = HALMAC_NETWORK_UNDEFINE;

	switch (port) {
	case 0:
		val = rtw_read32(a, REG_CR);
		network = BIT_GET_NETYPE0(val);
		break;

	case 1:
		val = rtw_read32(a, REG_CR);
		network = BIT_GET_NETYPE1(val);
		break;

	case 2:
		val = rtw_read32(a, REG_CR_EXT);
		network = BIT_GET_NETYPE2(val);
		break;

	case 3:
		val = rtw_read32(a, REG_CR_EXT);
		network = BIT_GET_NETYPE3(val);
		break;

	case 4:
		val = rtw_read32(a, REG_CR_EXT);
		network = BIT_GET_NETYPE4(val);
		break;

	default:
		goto out;
	}

	*type = _network_type_halmac2drv(network);

	err = 0;
out:
	return err;
#endif
}

/**
 * rtw_halmac_get_bcn_ctrl() - Get beacon control setting of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @bcn_ctrl:	setting of beacon control
 *
 * Get beacon control setting of specific port from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_get_bcn_ctrl(struct dvobj_priv *d, enum _hw_port hwport,
			struct rtw_halmac_bcn_ctrl *bcn_ctrl)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 port;
	struct HALMAC_BCN_CTRL ctrl;
	enum _HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);
	_rtw_memset(&ctrl, 0, sizeof(struct HALMAC_BCN_CTRL));

	status = api->halmac_rw_bcn_ctrl(halmac, port, 0, &ctrl);
	if (status != HALMAC_RET_SUCCESS)
		goto out;
	_beacon_ctrl_halmac2drv(&ctrl, bcn_ctrl);

	err = 0;
out:
	return err;
}

/*
 * Note:
 *	When this function return, the register REG_RCR may be changed.
 */
int rtw_halmac_config_rx_info(struct dvobj_priv *d, HALMAC_DRV_INFO info)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_cfg_drv_info(halmac, info);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/**
 * rtw_halmac_set_max_dl_fw_size() - Set the MAX download firmware size
 * @d:		struct dvobj_priv*
 * @size:	the max download firmware size in one I/O
 *
 * Set the max download firmware size in one I/O.
 * Please also consider the max size of the callback function "SEND_RSVD_PAGE"
 * could accept, because download firmware would call "SEND_RSVD_PAGE" to send
 * firmware to IC.
 *
 * If the value of "size" is not even, it would be rounded down to nearest
 * even, and 0 and 1 are both invalid value.
 *
 * Return 0 for setting OK, otherwise fail.
 */
int rtw_halmac_set_max_dl_fw_size(struct dvobj_priv *d, u32 size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	if (!size || (size == 1))
		return -1;

	mac = dvobj_to_halmac(d);
	if (!mac) {
		RTW_ERR("%s: HALMAC is not ready!!\n", __FUNCTION__);
		return -1;
	}
	api = HALMAC_GET_API(mac);

	size &= ~1; /* round down to even */
	status = api->halmac_cfg_max_dl_size(mac, size);
	if (status != HALMAC_RET_SUCCESS) {
		RTW_WARN("%s: Fail to cfg_max_dl_size(%d), err=%d!!\n",
			 __FUNCTION__, size, status);
		return -1;
	}

	return 0;
}

/**
 * rtw_halmac_set_mac_address() - Set mac address of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @addr:	mac address
 *
 * Set self mac address of specific port to HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_set_mac_address(struct dvobj_priv *d, enum _hw_port hwport, u8 *addr)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	u8 port;
	HALMAC_WLAN_ADDR hwa;
	HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	port = _hw_port_drv2halmac(hwport);
	_rtw_memset(&hwa, 0, sizeof(hwa));
	_rtw_memcpy(hwa.Address, addr, 6);

	status = api->halmac_cfg_mac_addr(halmac, port, &hwa);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/**
 * rtw_halmac_set_bssid() - Set BSSID of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @addr:	BSSID, mac address of AP
 *
 * Set BSSID of specific port to HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_set_bssid(struct dvobj_priv *d, enum _hw_port hwport, u8 *addr)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	u8 port;
	HALMAC_WLAN_ADDR hwa;
	HALMAC_RET_STATUS status;
	int err = -1;

	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);

	_rtw_memset(&hwa, 0, sizeof(HALMAC_WLAN_ADDR));
	_rtw_memcpy(hwa.Address, addr, 6);
	status = api->halmac_cfg_bssid(halmac, port, &hwa);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/**
 * rtw_halmac_set_tx_address() - Set transmitter address of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @addr:	transmitter address
 *
 * Set transmitter address of specific port to HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_set_tx_address(struct dvobj_priv *d, enum _hw_port hwport, u8 *addr)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 port;
	union _HALMAC_WLAN_ADDR hwa;
	enum _HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);
	_rtw_memset(&hwa, 0, sizeof(union _HALMAC_WLAN_ADDR));
	_rtw_memcpy(hwa.Address, addr, 6);

	status = api->halmac_cfg_transmitter_addr(halmac, port, &hwa);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/**
 * rtw_halmac_set_network_type() - Set network type of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @type:	network type (_HW_STATE_*)
 *
 * Set network type of specific port to HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_set_network_type(struct dvobj_priv *d, enum _hw_port hwport, u8 type)
{
#if 0 /* Fail to set AP mode for port0 */
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 port;
	enum _HALMAC_NETWORK_TYPE_SELECT network;
	enum _HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);
	network = _network_type_drv2halmac(type);

	status = api->halmac_cfg_net_type(halmac, port, network);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
#else
	struct _ADAPTER *a;
	u8 port;
	enum _HALMAC_NETWORK_TYPE_SELECT network;
	u32 val;
	int err = -1;


	a = dvobj_get_primary_adapter(d);
	port = _hw_port_drv2halmac(hwport);
	network = _network_type_drv2halmac(type);

	if ((port > 0) && (network == HALMAC_NETWORK_AP))
		goto out;

	switch (port) {
	case 0:
		val = rtw_read32(a, REG_CR);
		val = BIT_SET_NETYPE0(val, network);
		rtw_write32(a, REG_CR, val);
		break;

	case 1:
		val = rtw_read32(a, REG_CR);
		val = BIT_SET_NETYPE1(val, network);
		rtw_write32(a, REG_CR, val);
		break;

	case 2:
		val = rtw_read32(a, REG_CR_EXT);
		val = BIT_SET_NETYPE2(val, network);
		rtw_write32(a, REG_CR_EXT, val);
		break;

	case 3:
		val = rtw_read32(a, REG_CR_EXT);
		val = BIT_SET_NETYPE3(val, network);
		rtw_write32(a, REG_CR_EXT, val);
		break;

	case 4:
		val = rtw_read32(a, REG_CR_EXT);
		val = BIT_SET_NETYPE4(val, network);
		rtw_write32(a, REG_CR_EXT, val);
		break;

	default:
		goto out;
	}


	err = 0;
out:
	return err;
#endif
}

/**
 * rtw_halmac_reset_tsf() - Reset TSF timer of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 *
 * Notice HALMAC to reset timing synchronization function(TSF) timer of
 * specific port.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_reset_tsf(struct dvobj_priv *d, enum _hw_port hwport)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 port;
	enum _HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);

	status = api->halmac_cfg_tsf_rst(halmac, port);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/**
 * rtw_halmac_set_bcn_interval() - Set beacon interval of each port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @space:	beacon interval, unit is ms
 *
 * Set beacon interval of specific port to HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_set_bcn_interval(struct dvobj_priv *d, enum _hw_port hwport,
				u32 interval)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 port;
	enum _HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);

	status = api->halmac_cfg_bcn_space(halmac, port, interval);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/**
 * rtw_halmac_set_bcn_ctrl() - Set beacon control setting of each port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @bcn_ctrl:	setting of beacon control
 *
 * Set beacon control setting of specific port to HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_set_bcn_ctrl(struct dvobj_priv *d, enum _hw_port hwport,
			struct rtw_halmac_bcn_ctrl *bcn_ctrl)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 port;
	struct HALMAC_BCN_CTRL ctrl;
	enum _HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);
	_rtw_memset(&ctrl, 0, sizeof(struct HALMAC_BCN_CTRL));
	_beacon_ctrl_drv2halmac(bcn_ctrl, &ctrl);

	status = api->halmac_rw_bcn_ctrl(halmac, port, 1, &ctrl);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/**
 * rtw_halmac_set_aid() - Set association identifier(AID) of specific port
 * @d:		struct dvobj_priv*
 * @hwport:	port
 * @aid:	Association identifier
 *
 * Set association identifier(AID) of specific port to HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_set_aid(struct dvobj_priv *d, enum _hw_port hwport, u16 aid)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	u8 port;
	enum _HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	port = _hw_port_drv2halmac(hwport);

#if 0
	status = api->halmac_cfg_aid(halmac, port, aid);
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#else
{
	struct _ADAPTER *a;
	u32 addr;
	u16 val;

	a = dvobj_get_primary_adapter(d);

	switch (port) {
	case 0:
		addr = REG_BCN_PSR_RPT;
		val = rtw_read16(a, addr);
		val = BIT_SET_PS_AID_0(val, aid);
		rtw_write16(a, addr, val);
		break;

	case 1:
		addr = REG_BCN_PSR_RPT1;
		val = rtw_read16(a, addr);
		val = BIT_SET_PS_AID_1(val, aid);
		rtw_write16(a, addr, val);
		break;

	case 2:
		addr = REG_BCN_PSR_RPT2;
		val = rtw_read16(a, addr);
		val = BIT_SET_PS_AID_2(val, aid);
		rtw_write16(a, addr, val);
		break;

	case 3:
		addr = REG_BCN_PSR_RPT3;
		val = rtw_read16(a, addr);
		val = BIT_SET_PS_AID_3(val, aid);
		rtw_write16(a, addr, val);
		break;

	case 4:
		addr = REG_BCN_PSR_RPT4;
		val = rtw_read16(a, addr);
		val = BIT_SET_PS_AID_4(val, aid);
		rtw_write16(a, addr, val);
		break;

	default:
		goto out;
	}
}
#endif

	err = 0;
out:
	return err;
}

int rtw_halmac_set_bandwidth(struct dvobj_priv *d, u8 channel, u8 pri_ch_idx, u8 bw)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_cfg_ch_bw(mac, channel, pri_ch_idx, bw);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	return 0;
}

/**
 * rtw_halmac_set_edca() - config edca parameter
 * @d:		struct dvobj_priv*
 * @queue:	XMIT_[VO/VI/BE/BK]_QUEUE
 * @aifs:	Arbitration inter-frame space(AIFS)
 * @cw:		Contention window(CW)
 * @txop:	MAX Transmit Opportunity(TXOP)
 *
 * Return: 0 if process OK, otherwise -1.
 */
int rtw_halmac_set_edca(struct dvobj_priv *d, u8 queue, u8 aifs, u8 cw, u16 txop)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_ACQ_ID ac;
	HALMAC_EDCA_PARA edca;
	HALMAC_RET_STATUS status;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	switch (queue) {
	case XMIT_VO_QUEUE:
		ac = HALMAC_ACQ_ID_VO;
		break;
	case XMIT_VI_QUEUE:
		ac = HALMAC_ACQ_ID_VI;
		break;
	case XMIT_BE_QUEUE:
		ac = HALMAC_ACQ_ID_BE;
		break;
	case XMIT_BK_QUEUE:
		ac = HALMAC_ACQ_ID_BK;
		break;
	default:
		return -1;
	}

	edca.aifs = aifs;
	edca.cw = cw;
	edca.txop_limit = txop;

	status = api->halmac_cfg_edca_para(mac, ac, &edca);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

/*
 * Description:
 *	Power on device hardware.
 *	[Notice!] If device's power state is on before,
 *	it would be power off first and turn on power again.
 *
 * Return:
 *	0	power on success
 *	-1	power on fail
 *	-2	power state unchange
 */
int rtw_halmac_poweron(struct dvobj_priv *d)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	if (!halmac)
		goto out;

	api = HALMAC_GET_API(halmac);

	status = api->halmac_pre_init_system_cfg(halmac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

#ifdef CONFIG_SDIO_HCI
	status = api->halmac_sdio_cmd53_4byte(halmac, HALMAC_SDIO_CMD53_4BYTE_MODE_RW);
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#endif /* CONFIG_SDIO_HCI */

	status = api->halmac_mac_power_switch(halmac, HALMAC_MAC_POWER_ON);
	if (HALMAC_RET_PWR_UNCHANGE == status) {
		/*
		 * Work around for warm reboot but device not power off,
		 * but it would also fall into this case when auto power on is enabled.
		 */
		api->halmac_mac_power_switch(halmac, HALMAC_MAC_POWER_OFF);
		status = api->halmac_mac_power_switch(halmac, HALMAC_MAC_POWER_ON);
		RTW_WARN("%s: Power state abnormal, try to recover...%s\n",
			 __FUNCTION__, (HALMAC_RET_SUCCESS == status)?"OK":"FAIL!");
	}
	if (HALMAC_RET_SUCCESS != status) {
		if (HALMAC_RET_PWR_UNCHANGE == status)
			err = -2;
		goto out;
	}

	status = api->halmac_init_system_cfg(halmac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

/*
 * Description:
 *	Power off device hardware.
 *
 * Return:
 *	0	Power off success
 *	-1	Power off fail
 */
int rtw_halmac_poweroff(struct dvobj_priv *d)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	int err = -1;


	halmac = dvobj_to_halmac(d);
	if (!halmac)
		goto out;

	api = HALMAC_GET_API(halmac);

	status = api->halmac_mac_power_switch(halmac, HALMAC_MAC_POWER_OFF);
	if ((HALMAC_RET_SUCCESS != status)
	    && (HALMAC_RET_PWR_UNCHANGE != status))
		goto out;

	err = 0;
out:
	return err;
}

#ifdef CONFIG_SUPPORT_TRX_SHARED
static inline HALMAC_RX_FIFO_EXPANDING_MODE _trx_share_mode_drv2halmac(u8 trx_share_mode)
{
	if (0 == trx_share_mode)
		return HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE;
	else if (1 == trx_share_mode)
		return HALMAC_RX_FIFO_EXPANDING_MODE_1_BLOCK;
	else if (2 == trx_share_mode)
		return HALMAC_RX_FIFO_EXPANDING_MODE_2_BLOCK;
	else if (3 == trx_share_mode)
		return HALMAC_RX_FIFO_EXPANDING_MODE_3_BLOCK;
	else
		return HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE;
}
static HALMAC_RX_FIFO_EXPANDING_MODE _rtw_get_trx_share_mode(_adapter *adapter)
{
	struct registry_priv  *registry_par = &adapter->registrypriv;

	return _trx_share_mode_drv2halmac(registry_par->trx_share_mode);
}
void dump_trx_share_mode(void *sel, _adapter *adapter)
{
	struct registry_priv  *registry_par = &adapter->registrypriv;
	u8 mode =  _trx_share_mode_drv2halmac(registry_par->trx_share_mode);

	if (HALMAC_RX_FIFO_EXPANDING_MODE_1_BLOCK == mode)
		RTW_PRINT_SEL(sel, "TRx share mode : %s\n", "RX_FIFO_EXPANDING_MODE_1");
	else if (HALMAC_RX_FIFO_EXPANDING_MODE_2_BLOCK == mode)
		RTW_PRINT_SEL(sel, "TRx share mode : %s\n", "RX_FIFO_EXPANDING_MODE_2");
	else if (HALMAC_RX_FIFO_EXPANDING_MODE_3_BLOCK == mode)
		RTW_PRINT_SEL(sel, "TRx share mode : %s\n", "RX_FIFO_EXPANDING_MODE_3");
	else
		RTW_PRINT_SEL(sel, "TRx share mode : %s\n", "DISABLE");
}
#endif

static enum _HALMAC_DRV_RSVD_PG_NUM _rsvd_page_num_drv2halmac(u8 num)
{
	if (num <= 8)
		return HALMAC_RSVD_PG_NUM8;
	if (num <= 16)
		return HALMAC_RSVD_PG_NUM16;
	if (num <= 24)
		return HALMAC_RSVD_PG_NUM24;

	if (num > 32)
		RTW_WARN("%s: Fail to allocate RSVD page(%d)!!"
			 " The MAX RSVD page number is 32...\n",
			 __FUNCTION__, num);

	return HALMAC_RSVD_PG_NUM32;
}

static u8 _rsvd_page_num_halmac2drv(HALMAC_DRV_RSVD_PG_NUM rsvd_page_number)
{
	u8 num = 0;


	switch (rsvd_page_number) {
	case HALMAC_RSVD_PG_NUM8:
		num = 8;
		break;

	case HALMAC_RSVD_PG_NUM16:
		num = 16;
		break;

	case HALMAC_RSVD_PG_NUM24:
		num = 24;
		break;

	case HALMAC_RSVD_PG_NUM32:
		num = 32;
		break;
	}

	return num;
}

static HALMAC_TRX_MODE _choose_trx_mode(struct dvobj_priv *d)
{
	PADAPTER p;


	p = dvobj_get_primary_adapter(d);

	if (p->registrypriv.wifi_spec)
		return HALMAC_TRX_MODE_WMM;

#ifdef CONFIG_SUPPORT_TRX_SHARED
	if (_rtw_get_trx_share_mode(p))
		return HALMAC_TRX_MODE_TRXSHARE;
#endif

	return HALMAC_TRX_MODE_NORMAL;
}

static inline enum _HALMAC_RF_TYPE _rf_type_drv2halmac(enum rf_type rf_drv)
{
	enum _HALMAC_RF_TYPE rf_mac;


	switch (rf_drv) {
	case RF_1T1R:
		rf_mac = HALMAC_RF_1T1R;
		break;
	case RF_1T2R:
		rf_mac = HALMAC_RF_1T2R;
		break;
	case RF_2T2R:
		rf_mac = HALMAC_RF_2T2R;
		break;
	case RF_2T3R:
		rf_mac = HALMAC_RF_2T3R;
		break;
	case RF_2T4R:
		rf_mac = HALMAC_RF_2T4R;
		break;
	case RF_3T3R:
		rf_mac = HALMAC_RF_3T3R;
		break;
	case RF_3T4R:
		rf_mac = HALMAC_RF_3T4R;
		break;
	case RF_4T4R:
		rf_mac = HALMAC_RF_4T4R;
		break;
	default:
		rf_mac = HALMAC_RF_MAX_TYPE;
		RTW_ERR("%s: Invalid RF type(0x%x)!\n", __FUNCTION__, rf_drv);
		break;
	}

	return rf_mac;
}

static void _ant_status_by_rf_type(enum rf_type rf, u8 *tx, u8 *rx)
{
	switch (rf) {
	case RF_1T1R:
	case RF_1T2R:
		*tx = BB_PATH_A;
		break;
	case RF_2T2R:
	case RF_2T3R:
	case RF_2T4R:
		*tx = BB_PATH_AB;
		break;
	case RF_3T3R:
	case RF_3T4R:
		*tx = BB_PATH_ABC;
		break;
	case RF_4T4R:
	default:
		*tx = BB_PATH_ABCD;
		break;
	}

	switch (rf) {
	case RF_1T1R:
		*rx = BB_PATH_A;
		break;
	case RF_1T2R:
	case RF_2T2R:
		*rx = BB_PATH_AB;
		break;
	case RF_2T3R:
	case RF_3T3R:
		*rx = BB_PATH_ABC;
		break;
	case RF_2T4R:
	case RF_3T4R:
	case RF_4T4R:
	default:
		*rx = BB_PATH_ABCD;
		break;
	}
}

static int _send_general_info(struct dvobj_priv *d)
{
	struct _ADAPTER *adapter;
	struct hal_com_data *hal;
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	struct _HALMAC_GENERAL_INFO info;
	enum _HALMAC_RET_STATUS status;
	enum rf_type rf;
	u8 val8;


	adapter = dvobj_get_primary_adapter(d);
	hal = GET_HAL_DATA(adapter);
	halmac = dvobj_to_halmac(d);
	if (!halmac)
		return -1;
	api = HALMAC_GET_API(halmac);

	_rtw_memset(&info, 0, sizeof(info));
	info.rfe_type = (u8)hal->rfe_type;
	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, (u8 *)&val8);
	rf = (enum rf_type)val8;
	info.rf_type = _rf_type_drv2halmac(rf);
	_ant_status_by_rf_type(rf, &info.tx_ant_status, &info.rx_ant_status);

	status = api->halmac_send_general_info(halmac, &info);
	switch (status) {
	case HALMAC_RET_SUCCESS:
		break;
	case HALMAC_RET_NO_DLFW:
		RTW_WARN("%s: halmac_send_general_info() fail because fw not dl!\n",
			 __FUNCTION__);
		/* go through */
	default:
		return -1;
	}

	return 0;
}

static int _cfg_drv_rsvd_pg_num(struct dvobj_priv *d)
{
	struct _ADAPTER *a;
	struct hal_com_data *hal;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	enum _HALMAC_DRV_RSVD_PG_NUM rsvd_page_number;
	HALMAC_RET_STATUS status;
	u8 drv_rsvd_num;


	a = dvobj_get_primary_adapter(d);
	hal = GET_HAL_DATA(a);
	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	drv_rsvd_num = rtw_hal_get_rsvd_page_num(a);
	rsvd_page_number = _rsvd_page_num_drv2halmac(drv_rsvd_num);
	status = api->halmac_cfg_drv_rsvd_pg_num(halmac, rsvd_page_number);
	if (status != HALMAC_RET_SUCCESS)
		return -1;
	hal->drv_rsvd_page_number = _rsvd_page_num_halmac2drv(rsvd_page_number);

	if (drv_rsvd_num != hal->drv_rsvd_page_number)
		RTW_INFO("%s: request %d pages, but allocate %d pages\n",
			 __FUNCTION__, drv_rsvd_num, hal->drv_rsvd_page_number);

	return 0;
}

/*
 * Description:
 *	Downlaod Firmware Flow
 *
 * Parameters:
 *	d	pointer of struct dvobj_priv
 *	fw	firmware array
 *	fwsize	firmware size
 *	re_dl	re-download firmware or not
 *		0: run in init hal flow, not re-download
 *		1: it is a stand alone operation, not in init hal flow
 *
 * Return:
 *	0	Success
 *	others	Fail
 */
static int download_fw(struct dvobj_priv *d, u8 *fw, u32 fwsize, u8 re_dl)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	int err = 0;
	PHAL_DATA_TYPE hal;
	HALMAC_FW_VERSION fw_vesion;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	hal = GET_HAL_DATA(dvobj_get_primary_adapter(d));

	if ((!fw) || (!fwsize))
		return -1;

	/* 1. Driver Stop Tx */
	/* ToDo */

	/* 2. Driver Check Tx FIFO is empty */
	err = rtw_halmac_txfifo_wait_empty(d, 2000); /* wait 2s */
	if (err) {
		err = -1;
		goto resume_tx;
	}

	/* 3. Config MAX download size */
	/*
	 * Already done in rtw_halmac_init_adapter() or
	 * somewhere calling rtw_halmac_set_max_dl_fw_size().
	 */

	/* 4. Download Firmware */
	status = api->halmac_download_firmware(mac, fw, fwsize);
	if (status != HALMAC_RET_SUCCESS) {
		RTW_ERR("%s: download firmware FAIL! status=0x%02x\n",
			__FUNCTION__, status);
		err = -1;
		goto resume_tx;
	}

resume_tx:
	/* 5. Driver resume TX if needed */
	/* ToDo */

	if (err)
		goto exit;

	if (re_dl) {
		HALMAC_TRX_MODE mode;

		/* 6. Change reserved page size */
		err = _cfg_drv_rsvd_pg_num(d);
		if (err)
			return -1;

		/* 7. Init TRX Configuration */
		mode = _choose_trx_mode(d);
		status = api->halmac_init_trx_cfg(mac, mode);
		if (HALMAC_RET_SUCCESS != status)
			return -1;

		/* 8. Config RX Aggregation */
		err = rtw_halmac_rx_agg_switch(d, _TRUE);
		if (err)
			return -1;

		/* 9. Send General Info */
		err = _send_general_info(d);
		if (err)
			return -1;
	}

	/* 10. (Driver) Reset driver variables if needed */
	hal->LastHMEBoxNum = 0;

	/* 11. (Driver) Get FW version */
	status = api->halmac_get_fw_version(mac, &fw_vesion);
	if (status == HALMAC_RET_SUCCESS) {
		hal->firmware_version = fw_vesion.version;
		hal->firmware_sub_version = fw_vesion.sub_version;
		hal->firmware_size = fwsize;
	}

exit:
	return err;
}

static int init_mac_flow(struct dvobj_priv *d)
{
	PADAPTER p;
	struct hal_com_data *hal;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	enum _HALMAC_DRV_RSVD_PG_NUM rsvd_page_number;
	HALMAC_WLAN_ADDR hwa;
	HALMAC_TRX_MODE trx_mode;
	HALMAC_RET_STATUS status;
	u8 drv_rsvd_num;
	u8 nettype;
	int err, err_ret = -1;


	p = dvobj_get_primary_adapter(d);
	hal = GET_HAL_DATA(p);
	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

#ifdef CONFIG_SUPPORT_TRX_SHARED
	status = api->halmac_cfg_rx_fifo_expanding_mode(halmac, _rtw_get_trx_share_mode(p));
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#endif

#if 0 /* It is not necessary to call this in normal driver */
	status = api->halmac_cfg_la_mode(halmac, HALMAC_LA_MODE_DISABLE);
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#endif

	err = _cfg_drv_rsvd_pg_num(d);
	if (err)
		goto out;

#ifdef CONFIG_USB_HCI
	status = api->halmac_set_bulkout_num(halmac, d->RtNumOutPipes);
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#endif /* CONFIG_USB_HCI */

	trx_mode = _choose_trx_mode(d);
	status = api->halmac_init_mac_cfg(halmac, trx_mode);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = rtw_halmac_rx_agg_switch(d, _TRUE);
	if (err)
		goto out;

	nettype = dvobj_to_regsty(d)->wireless_mode;
	if (is_supported_vht(nettype) == _TRUE)
		status = api->halmac_cfg_operation_mode(halmac, HALMAC_WIRELESS_MODE_AC);
	else if (is_supported_ht(nettype) == _TRUE)
		status = api->halmac_cfg_operation_mode(halmac, HALMAC_WIRELESS_MODE_N);
	else if (IsSupportedTxOFDM(nettype) == _TRUE)
		status = api->halmac_cfg_operation_mode(halmac, HALMAC_WIRELESS_MODE_G);
	else
		status = api->halmac_cfg_operation_mode(halmac, HALMAC_WIRELESS_MODE_B);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err_ret = 0;
out:
	return err_ret;
}

static int _drv_enable_trx(struct dvobj_priv *d)
{
	struct _ADAPTER *adapter;
	u32 status;


	adapter = dvobj_get_primary_adapter(d);
	if (adapter->bup == _FALSE) {
		status = rtw_start_drv_threads(adapter);
		if (status == _FAIL) {
			RTW_ERR("%s: Start threads Failed!\n", __FUNCTION__);
			return -1;
		}
	}

	rtw_intf_start(adapter);

	return 0;
}

/*
 * Notices:
 *	Make sure
 *	1. rtw_hal_get_hwreg(HW_VAR_RF_TYPE)
 *	2. HAL_DATA_TYPE.rfe_type
 *	already ready for use before calling this function.
 */
static int _halmac_init_hal(struct dvobj_priv *d, u8 *fw, u32 fwsize)
{
	PADAPTER adapter;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 ok = _TRUE;
	u8 fw_ok = _FALSE;
	int err, err_ret = -1;


	adapter = dvobj_get_primary_adapter(d);
	halmac = dvobj_to_halmac(d);
	if (!halmac)
		goto out;
	api = HALMAC_GET_API(halmac);

	/* StatePowerOff */

	/* SKIP: halmac_init_adapter (Already done before) */

	/* halmac_pre_Init_system_cfg */
	/* halmac_mac_power_switch(on) */
	/* halmac_Init_system_cfg */
	ok = rtw_hal_power_on(adapter);
	if (_FAIL == ok)
		goto out;

	/* StatePowerOn */

	/* DownloadFW */
	if (fw && fwsize) {
		err = download_fw(d, fw, fwsize, 0);
		if (err)
			goto out;
		fw_ok = _TRUE;
	}

	/* InitMACFlow */
	err = init_mac_flow(d);
	if (err)
		goto out;

	/* Driver insert flow: Enable TR/RX */
	err = _drv_enable_trx(d);
	if (err)
		goto out;

	/* halmac_send_general_info */
	if (_TRUE == fw_ok) {
		err = _send_general_info(d);
		if (err)
			goto out;
	}

	/* Init Phy parameter-MAC */
	ok = rtw_hal_init_mac_register(adapter);
	if (_FALSE == ok)
		goto out;

	/* StateMacInitialized */

	/* halmac_cfg_drv_info */
	err = rtw_halmac_config_rx_info(d, HALMAC_DRV_INFO_PHY_STATUS);
	if (err)
		goto out;

	/* halmac_set_hw_value(HALMAC_HW_EN_BB_RF) */
	/* Init BB, RF */
	ok = rtw_hal_init_phy(adapter);
	if (_FALSE == ok)
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

int rtw_halmac_init_hal(struct dvobj_priv *d)
{
	return _halmac_init_hal(d, NULL, 0);
}

/*
 * Notices:
 *	Make sure
 *	1. rtw_hal_get_hwreg(HW_VAR_RF_TYPE)
 *	2. HAL_DATA_TYPE.rfe_type
 *	already ready for use before calling this function.
 */
int rtw_halmac_init_hal_fw(struct dvobj_priv *d, u8 *fw, u32 fwsize)
{
	return _halmac_init_hal(d, fw, fwsize);
}

/*
 * Notices:
 *	Make sure
 *	1. rtw_hal_get_hwreg(HW_VAR_RF_TYPE)
 *	2. HAL_DATA_TYPE.rfe_type
 *	already ready for use before calling this function.
 */
int rtw_halmac_init_hal_fw_file(struct dvobj_priv *d, u8 *fwpath)
{
	u8 *fw = NULL;
	u32 fwmaxsize = 0, size = 0;
	int err = 0;


	err = rtw_halmac_get_fw_max_size(d, &fwmaxsize);
	if (err) {
		RTW_ERR("%s: Fail to get Firmware MAX size(err=%d)\n", __FUNCTION__, err);
		return -1;
	}

	fw = rtw_zmalloc(fwmaxsize);
	if (!fw)
		return -1;

	size = rtw_retrieve_from_file(fwpath, fw, fwmaxsize);
	if (!size) {
		err = -1;
		goto exit;
	}

	err = _halmac_init_hal(d, fw, size);

exit:
	rtw_mfree(fw, fwmaxsize);
	fw = NULL;

	return err;
}

int rtw_halmac_deinit_hal(struct dvobj_priv *d)
{
	PADAPTER adapter;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	int err = -1;


	adapter = dvobj_get_primary_adapter(d);
	halmac = dvobj_to_halmac(d);
	if (!halmac)
		goto out;
	api = HALMAC_GET_API(halmac);

	status = api->halmac_deinit_interface_cfg(halmac);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	rtw_hal_power_off(adapter);

	err = 0;
out:
	return err;
}

int rtw_halmac_self_verify(struct dvobj_priv *d)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	int err = -1;


	mac = dvobj_to_halmac(d);
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

static u8 rtw_halmac_txfifo_is_empty(struct dvobj_priv *d)
{
	struct _HALMAC_ADAPTER *mac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u32 chk_num = 10;
	u8 rst = _FALSE;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_txfifo_is_empty(mac, chk_num);
	if (status == HALMAC_RET_SUCCESS)
		rst = _TRUE;

	return rst;
}

/**
 * rtw_halmac_txfifo_wait_empty() - Wait TX FIFO to be emtpy
 * @d:		struct dvobj_priv*
 * @timeout:	time limit of wait, unit is ms
 *		0 for no limit
 *
 * Wait TX FIFO to be emtpy.
 *
 * Rteurn 0 for TX FIFO is empty, otherwise not empty.
 */
int rtw_halmac_txfifo_wait_empty(struct dvobj_priv *d, u32 timeout)
{
	struct _ADAPTER *a;
	u8 empty = _FALSE;
	u32 cnt = 0;
	systime start_time = 0;
	u32 pass_time; /* ms */


	a = dvobj_get_primary_adapter(d);
	start_time = rtw_get_current_time();

	do {
		cnt++;
		empty = rtw_halmac_txfifo_is_empty(d);
		if (empty == _TRUE)
			break;

		if (timeout) {
			pass_time = rtw_get_passing_time_ms(start_time);
			if (pass_time > timeout)
				break;
		}
		if (RTW_CANNOT_IO(a)) {
			RTW_WARN("%s: Interrupted by I/O forbiden!\n", __FUNCTION__);
			break;
		}

		rtw_msleep_os(2);
	} while (1);

	if (empty == _FALSE) {
#ifdef CONFIG_RTW_DEBUG
		u16 dbg_reg[] = {0x210, 0x230, 0x234, 0x238, 0x23C, 0x240,
				 0x41A, 0x10FC, 0x10F8, 0x11F4, 0x11F8};
		u8 i;
		u32 val;

		if (!RTW_CANNOT_IO(a)) {
			for (i = 0; i < ARRAY_SIZE(dbg_reg); i++) {
				val = rtw_read32(a, dbg_reg[i]);
				RTW_ERR("REG_%X:0x%08x\n", dbg_reg[i], val);
			}
		}
#endif /* CONFIG_RTW_DEBUG */

		RTW_ERR("%s: Fail to wait txfifo empty!(cnt=%d)\n",
			__FUNCTION__, cnt);
		return -1;
	}

	return 0;
}

static enum _HALMAC_DLFW_MEM _fw_mem_drv2halmac(enum fw_mem mem, u8 tx_stop)
{
	enum _HALMAC_DLFW_MEM mem_halmac = HALMAC_DLFW_MEM_UNDEFINE;


	switch (mem) {
	case FW_EMEM:
		if (tx_stop == _FALSE)
			mem_halmac = HALMAC_DLFW_MEM_EMEM_RSVD_PG;
		else
			mem_halmac = HALMAC_DLFW_MEM_EMEM;
		break;

	case FW_IMEM:
	case FW_DMEM:
		mem_halmac = HALMAC_DLFW_MEM_UNDEFINE;
		break;
	}

	return mem_halmac;
}

int rtw_halmac_dlfw_mem(struct dvobj_priv *d, u8 *fw, u32 fwsize, enum fw_mem mem)
{
	struct _HALMAC_ADAPTER *mac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	enum _HALMAC_DLFW_MEM dlfw_mem;
	u8 tx_stop = _FALSE;
	u32 chk_timeout = 2000; /* unit: ms */
	int err = 0;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	if ((!fw) || (!fwsize))
		return -1;

#ifndef RTW_HALMAC_DLFW_MEM_NO_STOP_TX
	/* 1. Driver Stop Tx */
	/* ToDo */

	/* 2. Driver Check Tx FIFO is empty */
	err = rtw_halmac_txfifo_wait_empty(d, chk_timeout);
	if (err)
		tx_stop = _FALSE;
	else
		tx_stop = _TRUE;
#endif /* !RTW_HALMAC_DLFW_MEM_NO_STOP_TX */

	/* 3. Download Firmware MEM */
	dlfw_mem = _fw_mem_drv2halmac(mem, tx_stop);
	if (dlfw_mem == HALMAC_DLFW_MEM_UNDEFINE) {
		err = -1;
		goto resume_tx;
	}
	status = api->halmac_free_download_firmware(mac, dlfw_mem, fw, fwsize);
	if (status != HALMAC_RET_SUCCESS) {
		RTW_ERR("%s: halmac_free_download_firmware fail(err=0x%x)\n",
			__FUNCTION__, status);
		err = -1;
		goto resume_tx;
	}

resume_tx:
#ifndef RTW_HALMAC_DLFW_MEM_NO_STOP_TX
	/* 4. Driver resume TX if needed */
	/* ToDo */
#endif /* !RTW_HALMAC_DLFW_MEM_NO_STOP_TX */

	return err;
}

int rtw_halmac_dlfw_mem_from_file(struct dvobj_priv *d, u8 *fwpath, enum fw_mem mem)
{
	u8 *fw = NULL;
	u32 fwmaxsize = 0, size = 0;
	int err = 0;


	err = rtw_halmac_get_fw_max_size(d, &fwmaxsize);
	if (err) {
		RTW_ERR("%s: Fail to get Firmware MAX size(err=%d)\n", __FUNCTION__, err);
		return -1;
	}

	fw = rtw_zmalloc(fwmaxsize);
	if (!fw)
		return -1;

	size = rtw_retrieve_from_file(fwpath, fw, fwmaxsize);
	if (size)
		err = rtw_halmac_dlfw_mem(d, fw, size, mem);
	else
		err = -1;

	rtw_mfree(fw, fwmaxsize);
	fw = NULL;

	return err;
}

/*
 * Return:
 *	0	Success
 *	-22	Invalid arguemnt
 */
int rtw_halmac_dlfw(struct dvobj_priv *d, u8 *fw, u32 fwsize)
{
	PADAPTER adapter;
	HALMAC_RET_STATUS status;
	u32 ok = _TRUE;
	int err, err_ret = -1;


	if (!fw || !fwsize)
		return -22;

	adapter = dvobj_get_primary_adapter(d);

	/* re-download firmware */
	if (rtw_is_hw_init_completed(adapter))
		return download_fw(d, fw, fwsize, 1);

	/* Download firmware before hal init */
	/* Power on, download firmware and init mac */
	ok = rtw_hal_power_on(adapter);
	if (_FAIL == ok)
		goto out;

	err = download_fw(d, fw, fwsize, 0);
	if (err) {
		err_ret = err;
		goto out;
	}

	err = init_mac_flow(d);
	if (err)
		goto out;

	err = _send_general_info(d);
	if (err)
		goto out;

	err_ret = 0;

out:
	return err_ret;
}

int rtw_halmac_dlfw_from_file(struct dvobj_priv *d, u8 *fwpath)
{
	u8 *fw = NULL;
	u32 fwmaxsize = 0, size = 0;
	int err = 0;


	err = rtw_halmac_get_fw_max_size(d, &fwmaxsize);
	if (err) {
		RTW_ERR("%s: Fail to get Firmware MAX size(err=%d)\n", __FUNCTION__, err);
		return -1;
	}

	fw = rtw_zmalloc(fwmaxsize);
	if (!fw)
		return -1;

	size = rtw_retrieve_from_file(fwpath, fw, fwmaxsize);
	if (size)
		err = rtw_halmac_dlfw(d, fw, size);
	else
		err = -1;

	rtw_mfree(fw, fwmaxsize);
	fw = NULL;

	return err;
}

/*
 * Description:
 *	Power on/off BB/RF domain.
 *
 * Parameters:
 *	enable	_TRUE/_FALSE for power on/off
 *
 * Return:
 *	0	Success
 *	others	Fail
 */
int rtw_halmac_phy_power_switch(struct dvobj_priv *d, u8 enable)
{
	PADAPTER adapter;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	adapter = dvobj_get_primary_adapter(d);
	halmac = dvobj_to_halmac(d);
	if (!halmac)
		return -1;
	api = HALMAC_GET_API(halmac);

	status = api->halmac_set_hw_value(halmac, HALMAC_HW_EN_BB_RF, &enable);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

static u8 _is_fw_read_cmd_down(PADAPTER adapter, u8 msgbox_num)
{
	u8 read_down = _FALSE;
	int retry_cnts = 100;
	u8 valid;

	do {
		valid = rtw_read8(adapter, REG_HMETFR) & BIT(msgbox_num);
		if (0 == valid)
			read_down = _TRUE;
		else
			rtw_msleep_os(1);
	} while ((!read_down) && (retry_cnts--));

	if (_FALSE == read_down)
		RTW_WARN("%s, reg_1cc(%x), msg_box(%d)...\n", __func__, rtw_read8(adapter, REG_HMETFR), msgbox_num);

	return read_down;
}

/**
 * rtw_halmac_send_h2c() - Send H2C to firmware
 * @d:		struct dvobj_priv*
 * @h2c:	H2C data buffer, suppose to be 8 bytes
 *
 * Send H2C to firmware by message box register(0x1D0~0x1D3 & 0x1F0~0x1F3).
 *
 * Return: 0 if process OK, otherwise fail to send this H2C.
 */
int rtw_halmac_send_h2c(struct dvobj_priv *d, u8 *h2c)
{
	PADAPTER adapter = dvobj_get_primary_adapter(d);
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 h2c_box_num = 0;
	u32 msgbox_addr = 0;
	u32 msgbox_ex_addr = 0;
	u32 h2c_cmd = 0;
	u32 h2c_cmd_ex = 0;
	int err = -1;

	if (hal->bFWReady == _FALSE) {
		RTW_WARN("%s: return H2C cmd because fw is not ready\n", __FUNCTION__);
		return err;
	}

	if (!h2c) {
		RTW_WARN("%s: pbuf is NULL\n", __FUNCTION__);
		return err;
	}

	if (rtw_is_surprise_removed(adapter)) {
		RTW_WARN("%s: surprise removed\n", __FUNCTION__);
		return err;
	}

	_enter_critical_mutex(&d->h2c_fwcmd_mutex, NULL);

	/* pay attention to if race condition happened in H2C cmd setting */
	h2c_box_num = hal->LastHMEBoxNum;

	if (!_is_fw_read_cmd_down(adapter, h2c_box_num)) {
		RTW_WARN(" fw read cmd failed...\n");
#ifdef DBG_CONFIG_ERROR_DETECT
		hal->srestpriv.self_dect_fw = _TRUE;
		hal->srestpriv.self_dect_fw_cnt++;
#endif /* DBG_CONFIG_ERROR_DETECT */
		goto exit;
	}

	/* Write Ext command (byte 4~7) */
	msgbox_ex_addr = REG_HMEBOX_E0 + (h2c_box_num * EX_MESSAGE_BOX_SIZE);
	_rtw_memcpy((u8 *)(&h2c_cmd_ex), h2c + 4, EX_MESSAGE_BOX_SIZE);
	h2c_cmd_ex = le32_to_cpu(h2c_cmd_ex);
	rtw_write32(adapter, msgbox_ex_addr, h2c_cmd_ex);

	/* Write command (byte 0~3) */
	msgbox_addr = REG_HMEBOX0 + (h2c_box_num * MESSAGE_BOX_SIZE);
	_rtw_memcpy((u8 *)(&h2c_cmd), h2c, 4);
	h2c_cmd = le32_to_cpu(h2c_cmd);
	rtw_write32(adapter, msgbox_addr, h2c_cmd);

	/* update last msg box number */
	hal->LastHMEBoxNum = (h2c_box_num + 1) % MAX_H2C_BOX_NUMS;
	err = 0;

#ifdef DBG_H2C_CONTENT
	RTW_INFO_DUMP("[H2C] - ", h2c, RTW_HALMAC_H2C_MAX_SIZE);
#endif
exit:
	_exit_critical_mutex(&d->h2c_fwcmd_mutex, NULL);
	return err;
}

/**
 * rtw_halmac_c2h_handle() - Handle C2H for HALMAC
 * @d:		struct dvobj_priv*
 * @c2h:	Full C2H packet, including RX description and payload
 * @size:	Size(byte) of c2h
 *
 * Send C2H packet to HALMAC to process C2H packets, and the expected C2H ID is
 * 0xFF. This function won't have any I/O, so caller doesn't have to call it in
 * I/O safe place(ex. command thread).
 *
 * Please sure doesn't call this function in the same thread as someone is
 * waiting HALMAC C2H ack, otherwise there is a deadlock happen.
 *
 * Return: 0 if process OK, otherwise no action for this C2H.
 */
int rtw_halmac_c2h_handle(struct dvobj_priv *d, u8 *c2h, u32 size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_c2h_info(mac, c2h, size);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	return 0;
}

int rtw_halmac_get_available_efuse_size(struct dvobj_priv *d, u32 *size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 val;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_efuse_available_size(mac, &val);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	*size = val;
	return 0;
}

int rtw_halmac_get_physical_efuse_size(struct dvobj_priv *d, u32 *size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 val;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_efuse_size(mac, &val);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	*size = val;
	return 0;
}

int rtw_halmac_read_physical_efuse_map(struct dvobj_priv *d, u8 *map, u32 size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	HALMAC_FEATURE_ID id;
	int ret;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	id = HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE;

	ret = init_halmac_event(d, id, map, size);
	if (ret)
		return -1;

	status = api->halmac_dump_efuse_map(mac, HALMAC_EFUSE_R_DRV);
	if (HALMAC_RET_SUCCESS != status) {
		free_halmac_event(d, id);
		return -1;
	}

	ret = wait_halmac_event(d, id);
	if (ret)
		return -1;

	return 0;
}

int rtw_halmac_read_physical_efuse(struct dvobj_priv *d, u32 offset, u32 cnt, u8 *data)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u8 v;
	u32 i;
	u8 *efuse = NULL;
	u32 size = 0;
	int err = 0;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	if (api->halmac_read_efuse) {
		for (i = 0; i < cnt; i++) {
			status = api->halmac_read_efuse(mac, offset + i, &v);
			if (HALMAC_RET_SUCCESS != status)
				return -1;
			data[i] = v;
		}
	} else {
		err = rtw_halmac_get_physical_efuse_size(d, &size);
		if (err)
			return -1;

		efuse = rtw_zmalloc(size);
		if (!efuse)
			return -1;

		err = rtw_halmac_read_physical_efuse_map(d, efuse, size);
		if (err)
			err = -1;
		else
			_rtw_memcpy(data, efuse + offset, cnt);

		rtw_mfree(efuse, size);
	}

	return err;
}

int rtw_halmac_write_physical_efuse(struct dvobj_priv *d, u32 offset, u32 cnt, u8 *data)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 i;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	if (api->halmac_write_efuse == NULL)
		return -1;

	for (i = 0; i < cnt; i++) {
		status = api->halmac_write_efuse(mac, offset + i, data[i]);
		if (HALMAC_RET_SUCCESS != status)
			return -1;
	}

	return 0;
}

int rtw_halmac_get_logical_efuse_size(struct dvobj_priv *d, u32 *size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 val;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_logical_efuse_size(mac, &val);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	*size = val;
	return 0;
}

int rtw_halmac_read_logical_efuse_map(struct dvobj_priv *d, u8 *map, u32 size, u8 *maskmap, u32 masksize)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	HALMAC_FEATURE_ID id;
	int ret;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	id = HALMAC_FEATURE_DUMP_LOGICAL_EFUSE;

	ret = init_halmac_event(d, id, map, size);
	if (ret)
		return -1;

	status = api->halmac_dump_logical_efuse_map(mac, HALMAC_EFUSE_R_DRV);
	if (HALMAC_RET_SUCCESS != status) {
		free_halmac_event(d, id);
		return -1;
	}

	ret = wait_halmac_event(d, id);
	if (ret)
		return -1;

	if (maskmap && masksize) {
		struct _HALMAC_PG_EFUSE_INFO pginfo;

		pginfo.pEfuse_map = map;
		pginfo.efuse_map_size = size;
		pginfo.pEfuse_mask = maskmap;
		pginfo.efuse_mask_size = masksize;

		status = api->halmac_mask_logical_efuse(mac, &pginfo);
		if (status != HALMAC_RET_SUCCESS)
			RTW_WARN("%s: mask logical efuse FAIL!\n", __FUNCTION__);
	}

	return 0;
}

int rtw_halmac_write_logical_efuse_map(struct dvobj_priv *d, u8 *map, u32 size, u8 *maskmap, u32 masksize)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_PG_EFUSE_INFO pginfo;
	HALMAC_RET_STATUS status;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	pginfo.pEfuse_map = map;
	pginfo.efuse_map_size = size;
	pginfo.pEfuse_mask = maskmap;
	pginfo.efuse_mask_size = masksize;

	status = api->halmac_pg_efuse_by_map(mac, &pginfo, HALMAC_EFUSE_R_AUTO);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	return 0;
}

int rtw_halmac_read_logical_efuse(struct dvobj_priv *d, u32 offset, u32 cnt, u8 *data)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u8 v;
	u32 i;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	for (i = 0; i < cnt; i++) {
		status = api->halmac_read_logical_efuse(mac, offset + i, &v);
		if (HALMAC_RET_SUCCESS != status)
			return -1;
		data[i] = v;
	}

	return 0;
}

int rtw_halmac_write_logical_efuse(struct dvobj_priv *d, u32 offset, u32 cnt, u8 *data)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 i;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	for (i = 0; i < cnt; i++) {
		status = api->halmac_write_logical_efuse(mac, offset + i, data[i]);
		if (HALMAC_RET_SUCCESS != status)
			return -1;
	}

	return 0;
}

int rtw_halmac_write_bt_physical_efuse(struct dvobj_priv *d, u32 offset, u32 cnt, u8 *data)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 i;
	u8 bank = 1;

	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	for (i = 0; i < cnt; i++) {
		status = api->halmac_write_efuse_bt(mac, offset + i, data[i], bank);
		if (HALMAC_RET_SUCCESS != status) {
			printk("%s: halmac_write_efuse_bt status = %d\n", __FUNCTION__, status);
			return -1;
		}
	}
	printk("%s: halmac_write_efuse_bt status = HALMAC_RET_SUCCESS %d\n", __FUNCTION__, status);
	return 0;
}


int rtw_halmac_read_bt_physical_efuse_map(struct dvobj_priv *d, u8 *map, u32 size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	HALMAC_FEATURE_ID id;
	int ret;
	int bank = 1;

	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_dump_efuse_map_bt(mac, bank, size, map);
	if (HALMAC_RET_SUCCESS != status) {
		printk("%s: halmac_dump_efuse_map_bt fail!\n", __FUNCTION__);
		return -1;
	}

	printk("%s: OK!\n", __FUNCTION__);

	return 0;
}

static enum _HAL_FIFO_SEL _fifo_sel_drv2halmac(u8 fifo_sel)
{
	switch (fifo_sel) {
	case 0:
		return HAL_FIFO_SEL_TX;
	case 1:
		return HAL_FIFO_SEL_RX;
	case 2:
		return HAL_FIFO_SEL_RSVD_PAGE;
	case 3:
		return HAL_FIFO_SEL_REPORT;
	case 4:
		return HAL_FIFO_SEL_LLT;
	case 5:
		return HAL_FIFO_SEL_RXBUF_FW;
	}

	return HAL_FIFO_SEL_RSVD_PAGE;
}

/*#define CONFIG_HALMAC_FIFO_DUMP*/
int rtw_halmac_dump_fifo(struct dvobj_priv *d, u8 fifo_sel, u32 addr, u32 size, u8 *buffer)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u8 *pfifo_map = NULL;
	u32 fifo_size = 0;
	s8 ret = 0;/* 0:success, -1:error */
	u8 mem_created = _FALSE;

	HAL_FIFO_SEL halmac_fifo_sel;

	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	if ((size != 0) && (buffer == NULL))
		return -1;

	halmac_fifo_sel = _fifo_sel_drv2halmac(fifo_sel);

	if ((size) && (buffer)) {
		pfifo_map = buffer;
		fifo_size = size;
	} else {
		fifo_size = api->halmac_get_fifo_size(mac, halmac_fifo_sel);

		if (fifo_size)
			pfifo_map = rtw_zvmalloc(fifo_size);
		if (pfifo_map == NULL)
			return -1;
		mem_created = _TRUE;
	}

	status = api->halmac_dump_fifo(mac, halmac_fifo_sel, addr, fifo_size, pfifo_map);
	if (HALMAC_RET_SUCCESS != status) {
		ret = -1;
		goto _exit;
	}

#ifdef CONFIG_HALMAC_FIFO_DUMP
	{
		static const char * const fifo_sel_str[] = {
			"TX", "RX", "RSVD_PAGE", "REPORT", "LLT", "RXBUF_FW"
		};

		RTW_INFO("%s FIFO DUMP [start_addr:0x%04x , size:%d]\n", fifo_sel_str[halmac_fifo_sel], addr, fifo_size);
		RTW_INFO_DUMP("\n", pfifo_map, fifo_size);
		RTW_INFO(" ==================================================\n");
	}
#endif /* CONFIG_HALMAC_FIFO_DUMP */

_exit:
	if ((mem_created == _TRUE) && pfifo_map)
		rtw_vmfree(pfifo_map, fifo_size);

	return ret;
}

int rtw_halmac_rx_agg_switch(struct dvobj_priv *d, u8 enable)
{
	PADAPTER adapter;
	PHAL_DATA_TYPE hal;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_RXAGG_CFG rxaggcfg;
	HALMAC_RET_STATUS status;
	int err = -1;


	adapter = dvobj_get_primary_adapter(d);
	hal = GET_HAL_DATA(adapter);
	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	_rtw_memset((void *)&rxaggcfg, 0, sizeof(rxaggcfg));

	if (_TRUE == enable) {
#ifdef CONFIG_SDIO_HCI
		rxaggcfg.mode = HALMAC_RX_AGG_MODE_DMA;
		rxaggcfg.threshold.drv_define = 0;
#elif defined(CONFIG_USB_HCI) && defined(CONFIG_USB_RX_AGGREGATION)
		switch (hal->rxagg_mode) {
		case RX_AGG_DISABLE:
			rxaggcfg.mode = HALMAC_RX_AGG_MODE_NONE;
			break;

		case RX_AGG_DMA:
			rxaggcfg.mode = HALMAC_RX_AGG_MODE_DMA;
			if (hal->rxagg_dma_size || hal->rxagg_dma_timeout) {
				rxaggcfg.threshold.drv_define = 1;
				rxaggcfg.threshold.timeout = hal->rxagg_dma_timeout;
				rxaggcfg.threshold.size = hal->rxagg_dma_size;
			}
			break;

		case RX_AGG_USB:
		case RX_AGG_MIX:
			rxaggcfg.mode = HALMAC_RX_AGG_MODE_USB;
			if (hal->rxagg_usb_size || hal->rxagg_usb_timeout) {
				rxaggcfg.threshold.drv_define = 1;
				rxaggcfg.threshold.timeout = hal->rxagg_usb_timeout;
				rxaggcfg.threshold.size = hal->rxagg_usb_size;
			}
			break;
		}
#endif /* CONFIG_USB_HCI */
	} else
		rxaggcfg.mode = HALMAC_RX_AGG_MODE_NONE;

	status = api->halmac_cfg_rx_aggregation(halmac, &rxaggcfg);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = 0;
out:
	return err;
}

int rtw_halmac_download_rsvd_page(struct dvobj_priv *dvobj, u8 pg_offset, u8 *pbuf, u32 size)
{
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	PHALMAC_ADAPTER halmac = dvobj_to_halmac(dvobj);
	PHALMAC_API api = HALMAC_GET_API(halmac);

	status = api->halmac_dl_drv_rsvd_page(halmac, pg_offset, pbuf, size);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
}

/*
 * Description
 *	Fill following spec info from HALMAC API:
 *	sec_cam_ent_num
 *
 * Return
 *	0	Success
 *	others	Fail
 */
int rtw_halmac_fill_hal_spec(struct dvobj_priv *dvobj, struct hal_spec_t *spec)
{
	HALMAC_RET_STATUS status;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	u8 cam = 0;	/* Security Cam Entry Number */


	halmac = dvobj_to_halmac(dvobj);
	api = HALMAC_GET_API(halmac);

	/* Prepare data from HALMAC */
	status = api->halmac_get_hw_value(halmac, HALMAC_HW_CAM_ENTRY_NUM, &cam);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	/* Fill data to hal_spec_t */
	spec->sec_cam_ent_num = cam;

	return 0;
}

int rtw_halmac_p2pps(struct dvobj_priv *dvobj, PHAL_P2P_PS_PARA pp2p_ps_para)
{
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;
	PHALMAC_ADAPTER halmac = dvobj_to_halmac(dvobj);
	PHALMAC_API api = HALMAC_GET_API(halmac);
	HALMAC_P2PPS halmac_p2p_ps;

	(&halmac_p2p_ps)->offload_en = pp2p_ps_para->offload_en;
	(&halmac_p2p_ps)->role = pp2p_ps_para->role;
	(&halmac_p2p_ps)->ctwindow_en = pp2p_ps_para->ctwindow_en;
	(&halmac_p2p_ps)->noa_en = pp2p_ps_para->noa_en;
	(&halmac_p2p_ps)->noa_sel = pp2p_ps_para->noa_sel;
	(&halmac_p2p_ps)->all_sta_sleep = pp2p_ps_para->all_sta_sleep;
	(&halmac_p2p_ps)->discovery = pp2p_ps_para->discovery;
	(&halmac_p2p_ps)->p2p_port_id = _hw_port_drv2halmac(pp2p_ps_para->p2p_port_id);
	(&halmac_p2p_ps)->p2p_group = pp2p_ps_para->p2p_group;
	(&halmac_p2p_ps)->p2p_macid = pp2p_ps_para->p2p_macid;
	(&halmac_p2p_ps)->ctwindow_length = pp2p_ps_para->ctwindow_length;
	(&halmac_p2p_ps)->noa_duration_para = pp2p_ps_para->noa_duration_para;
	(&halmac_p2p_ps)->noa_interval_para = pp2p_ps_para->noa_interval_para;
	(&halmac_p2p_ps)->noa_start_time_para = pp2p_ps_para->noa_start_time_para;
	(&halmac_p2p_ps)->noa_count_para = pp2p_ps_para->noa_count_para;

	status = api->halmac_p2pps(halmac, (&halmac_p2p_ps));
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;

}

/**
 * rtw_halmac_iqk() - Run IQ Calibration
 * @d:		struct dvobj_priv*
 * @clear:	IQK parameters
 * @segment:	IQK parameters
 *
 * Process IQ Calibration(IQK).
 *
 * Rteurn: 0 for OK, otherwise fail.
 */
int rtw_halmac_iqk(struct dvobj_priv *d, u8 clear, u8 segment)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	HALMAC_FEATURE_ID id;
	HALMAC_IQK_PARA para;
	int ret;
	u8 retry = 3;
	u8 delay = 1; /* ms */


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	id = HALMAC_FEATURE_IQK;

	ret = init_halmac_event(d, id, NULL, 0);
	if (ret)
		return -1;

	para.clear = clear;
	para.segment_iqk = segment;

	do {
		status = api->halmac_start_iqk(mac, &para);
		if (status != HALMAC_RET_BUSY_STATE)
			break;
		RTW_WARN("%s: Fail to start IQK, status is BUSY! retry=%d\n", __FUNCTION__, retry);
		if (!retry)
			break;
		retry--;
		rtw_msleep_os(delay);
	} while (1);
	if (status != HALMAC_RET_SUCCESS) {
		free_halmac_event(d, id);
		return -1;
	}

	ret = wait_halmac_event(d, id);
	if (ret)
		return -1;

	return 0;
}

static int _phy_parameter_drv2halmac(struct rtw_phy_parameter *para, struct _HALMAC_PHY_PARAMETER_INFO *info)
{
	if (!para || !info)
		return -1;

	_rtw_memset(info, 0, sizeof(*info));

	switch (para->cmd) {
	case 0:
		/* MAC register */
		switch (para->data.mac.size) {
		case 1:
			info->cmd_id = HALMAC_PARAMETER_CMD_MAC_W8;
			break;
		case 2:
			info->cmd_id = HALMAC_PARAMETER_CMD_MAC_W16;
			break;
		default:
			info->cmd_id = HALMAC_PARAMETER_CMD_MAC_W32;
			break;
		}
		info->content.MAC_REG_W.value = para->data.mac.value;
		info->content.MAC_REG_W.msk = para->data.mac.msk;
		info->content.MAC_REG_W.offset = para->data.mac.offset;
		info->content.MAC_REG_W.msk_en = para->data.mac.msk_en;
		break;

	case 1:
		/* BB register */
		switch (para->data.bb.size) {
		case 1:
			info->cmd_id = HALMAC_PARAMETER_CMD_BB_W8;
			break;
		case 2:
			info->cmd_id = HALMAC_PARAMETER_CMD_BB_W16;
			break;
		default:
			info->cmd_id = HALMAC_PARAMETER_CMD_BB_W32;
			break;
		}
		info->content.BB_REG_W.value = para->data.mac.value;
		info->content.BB_REG_W.msk = para->data.mac.msk;
		info->content.BB_REG_W.offset = para->data.mac.offset;
		info->content.BB_REG_W.msk_en = para->data.mac.msk_en;
		break;

	case 2:
		/* RF register */
		info->cmd_id = HALMAC_PARAMETER_CMD_RF_W;
		info->content.RF_REG_W.value = para->data.rf.value;
		info->content.RF_REG_W.msk = para->data.rf.msk;
		info->content.RF_REG_W.offset = para->data.rf.offset;
		info->content.RF_REG_W.msk_en = para->data.rf.msk_en;
		info->content.RF_REG_W.rf_path = para->data.rf.path;
		break;

	case 3:
		/* Delay register */
		if (para->data.delay.unit == 0)
			info->cmd_id = HALMAC_PARAMETER_CMD_DELAY_US;
		else
			info->cmd_id = HALMAC_PARAMETER_CMD_DELAY_MS;
		info->content.DELAY_TIME.delay_time = para->data.delay.value;
		break;

	case 0xFF:
		/* Latest(End) command */
		info->cmd_id = HALMAC_PARAMETER_CMD_END;
		break;

	default:
		return -1;
	}

	return 0;
}

/**
 * rtw_halmac_cfg_phy_para() - Register(Phy parameter) configuration
 * @d:		struct dvobj_priv*
 * @para:	phy parameter
 *
 * Configure registers by firmware using H2C/C2H mechanism.
 * The latest command should be para->cmd==0xFF(End command) to finish all
 * processes.
 *
 * Return: 0 for OK, otherwise fail.
 */
int rtw_halmac_cfg_phy_para(struct dvobj_priv *d, struct rtw_phy_parameter *para)
{
	struct _HALMAC_ADAPTER *mac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	enum _HALMAC_FEATURE_ID id;
	struct _HALMAC_PHY_PARAMETER_INFO info;
	u8 full_fifo;
	int err, ret;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	id = HALMAC_FEATURE_CFG_PARA;
	full_fifo = 1; /* ToDo: How to deciede? */
	ret = 0;

	err = _phy_parameter_drv2halmac(para, &info);
	if (err)
		return -1;

	err = init_halmac_event(d, id, NULL, 0);
	if (err)
		return -1;

	status = api->halmac_cfg_parameter(mac, &info, full_fifo);
	if (info.cmd_id == HALMAC_PARAMETER_CMD_END) {
		if (status == HALMAC_RET_SUCCESS) {
			err = wait_halmac_event(d, id);
			if (err)
				ret = -1;
		} else {
			free_halmac_event(d, id);
			ret = -1;
			RTW_ERR("%s: Fail to send END of cfg parameter, status is 0x%x!\n", __FUNCTION__, status);
		}
	} else {
		if (status == HALMAC_RET_PARA_SENDING) {
			err = wait_halmac_event(d, id);
			if (err)
				ret = -1;
		} else {
			free_halmac_event(d, id);
			if (status != HALMAC_RET_SUCCESS) {
				ret = -1;
				RTW_ERR("%s: Fail to cfg parameter, status is 0x%x!\n", __FUNCTION__, status);
			}
		}
	}

	return ret;
}

#ifdef CONFIG_SDIO_HCI

/*
 * Description:
 *	Update queue allocated page number to driver
 *
 * Parameter:
 *	d	pointer to struct dvobj_priv of driver
 *
 * Rteurn:
 *	0	Success, "page" is valid.
 *	others	Fail, "page" is invalid.
 */
int rtw_halmac_query_tx_page_num(struct dvobj_priv *d)
{
	PADAPTER adapter;
	struct halmacpriv *hmpriv;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_RQPN_MAP rqpn;
	HALMAC_DMA_MAPPING dmaqueue;
	HALMAC_TXFF_ALLOCATION fifosize;
	HALMAC_RET_STATUS status;
	u8 i;


	adapter = dvobj_get_primary_adapter(d);
	hmpriv = &d->hmpriv;
	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	_rtw_memset((void *)&rqpn, 0, sizeof(rqpn));
	_rtw_memset((void *)&fifosize, 0, sizeof(fifosize));

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_RQPN_MAPPING, &rqpn);
	if (status != HALMAC_RET_SUCCESS)
		return -1;
	status = api->halmac_get_hw_value(halmac, HALMAC_HW_TXFF_ALLOCATION, &fifosize);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	for (i = 0; i < HW_QUEUE_ENTRY; i++) {
		hmpriv->txpage[i] = 0;

		/* Driver index mapping to HALMAC DMA queue */
		dmaqueue = HALMAC_DMA_MAPPING_UNDEFINE;
		switch (i) {
		case VO_QUEUE_INX:
			dmaqueue = rqpn.dma_map_vo;
			break;
		case VI_QUEUE_INX:
			dmaqueue = rqpn.dma_map_vi;
			break;
		case BE_QUEUE_INX:
			dmaqueue = rqpn.dma_map_be;
			break;
		case BK_QUEUE_INX:
			dmaqueue = rqpn.dma_map_bk;
			break;
		case MGT_QUEUE_INX:
			dmaqueue = rqpn.dma_map_mg;
			break;
		case HIGH_QUEUE_INX:
			dmaqueue = rqpn.dma_map_hi;
			break;
		case BCN_QUEUE_INX:
		case TXCMD_QUEUE_INX:
			/* Unlimited */
			hmpriv->txpage[i] = 0xFFFF;
			continue;
		}

		switch (dmaqueue) {
		case HALMAC_DMA_MAPPING_EXTRA:
			hmpriv->txpage[i] = fifosize.extra_queue_pg_num;
			break;
		case HALMAC_DMA_MAPPING_LOW:
			hmpriv->txpage[i] = fifosize.low_queue_pg_num;
			break;
		case HALMAC_DMA_MAPPING_NORMAL:
			hmpriv->txpage[i] = fifosize.normal_queue_pg_num;
			break;
		case HALMAC_DMA_MAPPING_HIGH:
			hmpriv->txpage[i] = fifosize.high_queue_pg_num;
			break;
		case HALMAC_DMA_MAPPING_UNDEFINE:
			break;
		}
		hmpriv->txpage[i] += fifosize.pub_queue_pg_num;
	}

	return 0;
}

/*
 * Description:
 *	Get specific queue allocated page number
 *
 * Parameter:
 *	d	pointer to struct dvobj_priv of driver
 *	queue	target queue to query, VO/VI/BE/BK/.../TXCMD_QUEUE_INX
 *	page	return allocated page number
 *
 * Rteurn:
 *	0	Success, "page" is valid.
 *	others	Fail, "page" is invalid.
 */
int rtw_halmac_get_tx_queue_page_num(struct dvobj_priv *d, u8 queue, u32 *page)
{
	*page = 0;
	if (queue < HW_QUEUE_ENTRY)
		*page = d->hmpriv.txpage[queue];

	return 0;
}

/*
 * Return:
 *	address for SDIO command
 */
u32 rtw_halmac_sdio_get_tx_addr(struct dvobj_priv *d, u8 *desc, u32 size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 addr;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_sdio_tx_addr(mac, desc, size, &addr);
	if (HALMAC_RET_SUCCESS != status)
		return 0;

	return addr;
}

int rtw_halmac_sdio_tx_allowed(struct dvobj_priv *d, u8 *buf, u32 size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_tx_allowed_sdio(mac, buf, size);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	return 0;
}

u32 rtw_halmac_sdio_get_rx_addr(struct dvobj_priv *d, u8 *seq)
{
	u8 id;

#define RTW_SDIO_ADDR_RX_RX0FF_PRFIX	0x0E000
#define RTW_SDIO_ADDR_RX_RX0FF_GEN(a)	(RTW_SDIO_ADDR_RX_RX0FF_PRFIX|(a&0x3))

	id = *seq;
	(*seq)++;
	return RTW_SDIO_ADDR_RX_RX0FF_GEN(id);
}
#endif /* CONFIG_SDIO_HCI */

#ifdef CONFIG_USB_HCI
u8 rtw_halmac_usb_get_bulkout_id(struct dvobj_priv *d, u8 *buf, u32 size)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u8 bulkout_id;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	status = api->halmac_get_usb_bulkout_id(mac, buf, size, &bulkout_id);
	if (HALMAC_RET_SUCCESS != status)
		return 0;

	return bulkout_id;
}

/**
 * rtw_halmac_usb_get_txagg_desc_num() - MAX descriptor number in one bulk for TX
 * @d:		struct dvobj_priv*
 * @size:	TX FIFO size, unit is byte.
 *
 * Get MAX descriptor number in one bulk out from HALMAC.
 *
 * Rteurn 0 for OK, otherwise fail.
 */
int rtw_halmac_usb_get_txagg_desc_num(struct dvobj_priv *d, u8 *num)
{
	struct _HALMAC_ADAPTER *halmac;
	struct _HALMAC_API *api;
	enum _HALMAC_RET_STATUS status;
	u8 val = 0;


	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_USB_TXAGG_DESC_NUM, &val);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	*num = val;

	return 0;
}

static inline HALMAC_USB_MODE _usb_mode_drv2halmac(enum RTW_USB_SPEED usb_mode)
{
	HALMAC_USB_MODE halmac_usb_mode = HALMAC_USB_MODE_U2;

	switch (usb_mode) {
	case RTW_USB_SPEED_2:
		halmac_usb_mode = HALMAC_USB_MODE_U2;
		break;
	case RTW_USB_SPEED_3:
		halmac_usb_mode = HALMAC_USB_MODE_U3;
		break;
	default:
		halmac_usb_mode = HALMAC_USB_MODE_U2;
		break;
	}

	return halmac_usb_mode;
}

u8 rtw_halmac_switch_usb_mode(struct dvobj_priv *d, enum RTW_USB_SPEED usb_mode)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	PADAPTER adapter;
	HALMAC_USB_MODE halmac_usb_mode;

	adapter = dvobj_get_primary_adapter(d);
	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	halmac_usb_mode = _usb_mode_drv2halmac(usb_mode);
	status = api->halmac_set_hw_value(mac, HALMAC_HW_USB_MODE, (void *)&halmac_usb_mode);

	if (HALMAC_RET_SUCCESS != status)
		return _FAIL;

	return _SUCCESS;
}
#endif /* CONFIG_USB_HCI */
