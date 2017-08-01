/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _HAL_HALMAC_C_

#include <drv_types.h>		/* PADAPTER, struct dvobj_priv, SDIO_ERR_VAL8 and etc. */
#include <hal_data.h>		/* efuse, PHAL_DATA_TYPE and etc. */
#include "halmac/halmac_api.h"	/* HALMAC_FW_SIZE_MAX_88XX and etc. */
#include "hal_halmac.h"		/* dvobj_to_halmac() and ect. */

#define DEFAULT_INDICATOR_TIMELMT	1000	/* ms */
#define FIRMWARE_MAX_SIZE		HALMAC_FW_SIZE_MAX_88XX

/*
 * Driver API for HALMAC operations
 */

#ifdef CONFIG_SDIO_HCI
#include <rtw_sdio.h>
static u8 _halmac_sdio_cmd52_read(void *p, u32 offset)
{
	struct dvobj_priv *d;
	u8 val;
	u8 ret;


	d = (struct dvobj_priv *)p;
	ret = rtw_sdio_read_cmd52(d, offset, &val, 1);
	if (_FAIL == ret) {
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);
		return SDIO_ERR_VAL8;
	}

	return val;
}

static void _halmac_sdio_cmd52_write(void *p, u32 offset, u8 val)
{
	struct dvobj_priv *d;
	u8 ret;


	d = (struct dvobj_priv *)p;
	ret = rtw_sdio_write_cmd52(d, offset, &val, 1);
	if (_FAIL == ret)
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);
}

static u8 _halmac_sdio_reg_read_8(void *p, u32 offset)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	u8 val;
	int err;


	d = (struct dvobj_priv *)p;
	val = SDIO_ERR_VAL8;
	pbuf = rtw_zmalloc(1);
	if (!pbuf)
		return val;

	err = d->intf_ops->read(d, offset, pbuf, 1, 0);
	if (err) {
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);
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
	int err;


	d = (struct dvobj_priv *)p;
	val = SDIO_ERR_VAL16;
	pbuf = rtw_zmalloc(2);
	if (!pbuf)
		return val;

	err = d->intf_ops->read(d, offset, pbuf, 2, 0);
	if (err) {
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);
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
	int err;


	d = (struct dvobj_priv *)p;
	val = SDIO_ERR_VAL32;
	pbuf = rtw_zmalloc(4);
	if (!pbuf)
		return val;

	err = d->intf_ops->read(d, offset, pbuf, 4, 0);
	if (err) {
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);
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
	int err;
	u8 rst = _FALSE;
	u32 sdio_read_size;

	sdio_read_size = RND4(size);
	if (sdio_read_size > psdio->block_transfer_len)
		sdio_read_size = _RND(sdio_read_size, psdio->block_transfer_len);

	pbuf = rtw_zmalloc(sdio_read_size);
	if ((!pbuf) || (!data))
		return rst;

	err = d->intf_ops->read(d, offset, pbuf, sdio_read_size, 0);
	if (err) {
		RTW_ERR("%s: [ERROR] I/O FAIL!\n", __func__);
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
	int err;


	d = (struct dvobj_priv *)p;
	pbuf = rtw_zmalloc(1);
	if (!pbuf)
		return;
	_rtw_memcpy(pbuf, &val, 1);

	err = d->intf_ops->write(d, offset, pbuf, 1, 0);
	if (err)
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);

	rtw_mfree(pbuf, 1);
}

static void _halmac_sdio_reg_write_16(void *p, u32 offset, u16 val)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	int err;


	d = (struct dvobj_priv *)p;
	val = cpu_to_le16(val);
	pbuf = rtw_zmalloc(2);
	if (!pbuf)
		return;
	_rtw_memcpy(pbuf, &val, 2);

	err = d->intf_ops->write(d, offset, pbuf, 2, 0);
	if (err)
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);

	rtw_mfree(pbuf, 2);
}

static void _halmac_sdio_reg_write_32(void *p, u32 offset, u32 val)
{
	struct dvobj_priv *d;
	u8 *pbuf;
	int err;


	d = (struct dvobj_priv *)p;
	val = cpu_to_le32(val);
	pbuf = rtw_zmalloc(4);
	if (!pbuf)
		return;
	_rtw_memcpy(pbuf, &val, 4);

	err = d->intf_ops->write(d, offset, pbuf, 4, 0);
	if (err)
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);

	rtw_mfree(pbuf, 4);
}

#else /* !CONFIG_SDIO_HCI */

static u8 _halmac_reg_read_8(void *p, u32 offset)
{
	struct dvobj_priv *d;
	PADAPTER adapter;


	d = (struct dvobj_priv *)p;
	adapter = d->padapters[IFACE_ID0];

	return rtw_read8(adapter, offset);
}

static u16 _halmac_reg_read_16(void *p, u32 offset)
{
	struct dvobj_priv *d;
	PADAPTER adapter;


	d = (struct dvobj_priv *)p;
	adapter = d->padapters[IFACE_ID0];

	return rtw_read16(adapter, offset);
}

static u32 _halmac_reg_read_32(void *p, u32 offset)
{
	struct dvobj_priv *d;
	PADAPTER adapter;


	d = (struct dvobj_priv *)p;
	adapter = d->padapters[IFACE_ID0];

	return rtw_read32(adapter, offset);
}

static void _halmac_reg_write_8(void *p, u32 offset, u8 val)
{
	struct dvobj_priv *d;
	PADAPTER adapter;
	int err;


	d = (struct dvobj_priv *)p;
	adapter = d->padapters[IFACE_ID0];

	err = rtw_write8(adapter, offset, val);
	if (err == _FAIL)
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);
}

static void _halmac_reg_write_16(void *p, u32 offset, u16 val)
{
	struct dvobj_priv *d;
	PADAPTER adapter;
	int err;


	d = (struct dvobj_priv *)p;
	adapter = d->padapters[IFACE_ID0];

	err = rtw_write16(adapter, offset, val);
	if (err == _FAIL)
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);
}

static void _halmac_reg_write_32(void *p, u32 offset, u32 val)
{
	struct dvobj_priv *d;
	PADAPTER adapter;
	int err;


	d = (struct dvobj_priv *)p;
	adapter = d->padapters[IFACE_ID0];

	err = rtw_write32(adapter, offset, val);
	if (err == _FAIL)
		RTW_INFO("%s: [ERROR] I/O FAIL!\n", __FUNCTION__);
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
#define MSG_PREFIX	"[HALMAC]"
	va_list args;
	u8 str[MSG_LEN] = {0};
	u32 type;
	u8 level;


	str[0] = '\n';
	type = 0xFFFFFFFF;
	if (rtw_drv_log_level <= _DRV_ERR_)
		level = HALMAC_DBG_ERR;
	else if (rtw_drv_log_level <= _DRV_INFO_)
		level = HALMAC_DBG_WARN;
	else
		level = HALMAC_DBG_TRACE;

	if (!(type & BIT(msg_type)))
		return _TRUE;
	if (level < msg_level)
		return _TRUE;

	va_start(args, fmt);
	vsnprintf(str, MSG_LEN, fmt, args);
	va_end(args);

	if (msg_level <= HALMAC_DBG_ERR)
		RTW_ERR(MSG_PREFIX "%s", str);
	else if (msg_level <= HALMAC_DBG_WARN)
		RTW_WARN(MSG_PREFIX "%s", str);
	else
		RTW_DBG(MSG_PREFIX "%s", str);

	return _TRUE;
}

static u8 _halmac_buff_print(void *p, u32 msg_type, u8 msg_level, s8 *buf, u32 size)
{
#define MSG_PREFIX	"[HALMAC]"
	u32 type;
	u8 level;

	type = 0xFFFFFFFF;
	if (rtw_drv_log_level <= _DRV_ERR_)
		level = HALMAC_DBG_ERR;
	else if (rtw_drv_log_level <= _DRV_INFO_)
		level = HALMAC_DBG_WARN;
	else
		level = HALMAC_DBG_TRACE;

	if (!(type & BIT(msg_type)))
		return _TRUE;
	if (level < msg_level)
		return _TRUE;

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
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		break;
	case HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		if (HALMAC_CMD_PROCESS_DONE != status) {
			RTW_INFO("%s: <WARN> id(%d) unspecified status(%d)!\n",
				 __FUNCTION__, id, status);
		}
		break;
	case HALMAC_FEATURE_DUMP_LOGICAL_EFUSE:
		RTW_INFO("%s: %s\n", __FUNCTION__, RTW_HALMAC_FEATURE_NAME[id]);
		if (HALMAC_CMD_PROCESS_DONE != status) {
			RTW_INFO("%s: <WARN> id(%d) unspecified status(%d)!\n",
				 __FUNCTION__, id, status);
		}
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
		RTW_INFO("%s: unknown feature id(%d)\n", __FUNCTION__, id);
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
		RTW_INFO("%s: <WARN> id(%d) sctx is not NULL!!\n", __FUNCTION__, id);
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
	struct submit_ctx *sctx;
	int ret;


	sctx = d->hmpriv.indicator[id].sctx;
	if (!sctx)
		return -1;

	ret = rtw_sctx_wait(sctx, RTW_HALMAC_FEATURE_NAME[id]);
	free_halmac_event(d, id);
	if (_SUCCESS == ret)
		return 0;

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
	adapter = d->padapters[IFACE_ID0];
	hal = GET_HAL_DATA(adapter);
	tbl = d->hmpriv.indicator;

	ret = is_valid_id_status(feature_id, process_status);
	if (_FALSE == ret)
		goto exit;

	indicator = &tbl[feature_id];
	indicator->status = process_status;
	indicator->ret_size = size;
	if (!indicator->sctx) {
		RTW_INFO("%s: No feature id(%d) waiting!!\n", __FUNCTION__, feature_id);
		goto exit;
	}
	sctx = indicator->sctx;

	if (HALMAC_CMD_PROCESS_ERROR == process_status) {
		RTW_INFO("%s: Something wrong id(%d)!!\n", __FUNCTION__, feature_id);
		rtw_sctx_done_err(&sctx, RTW_SCTX_DONE_UNKNOWN);
		goto exit;
	}

	if (size > indicator->buf_size) {
		RTW_INFO("%s: <WARN> id(%d) buffer is not enough(%d<%d), data will be truncated!\n",
			 __FUNCTION__, feature_id, indicator->buf_size, size);
		cpsz = indicator->buf_size;
	} else
		cpsz = size;
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

void rtw_halmac_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem)
{
#if defined(CONFIG_SDIO_HCI)
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;

	if (pmem == NULL) {
		RTW_ERR("pmem is NULL\n");
		return;
	}
	/* WARNING: pintf_dev should not be null! */
	mac = dvobj_to_halmac(pintfhdl->pintf_dev);
	api = HALMAC_GET_API(mac);

	api->halmac_reg_sdio_cmd53_read_n(mac, addr, cnt, pmem);
#endif
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
#endif

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

static int init_priv(struct halmacpriv *priv)
{
	struct halmac_indicator *indicator;
	u32 count, size;


	size = sizeof(*priv);
	_rtw_memset(priv, 0, size);

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

				RTW_INFO("%s: <WARN> %s id(%d) sctx still exist!!\n",
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

int rtw_halmac_init_adapter(struct dvobj_priv *d, PHALMAC_PLATFORM_API pf_api)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_INTERFACE intf;
	HALMAC_RET_STATUS status;
	int err = 0;


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
		RTW_INFO("%s: halmac_init_adapter fail!(status=%d)\n", __FUNCTION__, status);
		err = -1;
		goto out;
	}

	dvobj_set_halmac(d, halmac);

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

static HALMAC_RET_STATUS init_mac_flow(struct dvobj_priv *d)
{
	PADAPTER p;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_WLAN_ADDR hwa;
	HALMAC_RET_STATUS status;
	u8 wifi_test = 0;
	u8 nettype;
	int err;


	p = d->padapters[IFACE_ID0];
	halmac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(halmac);
	if (p->registrypriv.wifi_spec)
		wifi_test = 1;

#ifdef CONFIG_SUPPORT_TRX_SHARED
	status = api->halmac_cfg_rx_fifo_expanding_mode(halmac, _rtw_get_trx_share_mode(p));
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#endif

#if 0
	status = api->halmac_cfg_drv_rsvd_pg_num(halmac, HALMAC_RSVD_PG_NUM16);/*HALMAC_RSVD_PG_NUM24/HALMAC_RSVD_PG_NUM32*/
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#endif

#ifdef CONFIG_USB_HCI
	status = api->halmac_set_bulkout_num(halmac, d->RtNumOutPipes);
	if (status != HALMAC_RET_SUCCESS)
		goto out;
#endif /* CONFIG_USB_HCI */

	if (wifi_test)
		status = api->halmac_init_mac_cfg(halmac, HALMAC_TRX_MODE_WMM);
	else
		status = api->halmac_init_mac_cfg(halmac, HALMAC_TRX_MODE_NORMAL);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	err = rtw_halmac_rx_agg_switch(d, _TRUE);
	if (err)
		goto out;

	nettype = dvobj_to_regsty(d)->wireless_mode;
	if (IsSupportedVHT(nettype) == _TRUE)
		status = api->halmac_cfg_operation_mode(halmac, HALMAC_WIRELESS_MODE_AC);
	else if (IsSupportedHT(nettype) == _TRUE)
		status = api->halmac_cfg_operation_mode(halmac, HALMAC_WIRELESS_MODE_N);
	else if (IsSupportedTxOFDM(nettype) == _TRUE)
		status = api->halmac_cfg_operation_mode(halmac, HALMAC_WIRELESS_MODE_G);
	else
		status = api->halmac_cfg_operation_mode(halmac, HALMAC_WIRELESS_MODE_B);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

out:
	return status;
}

static inline HALMAC_RF_TYPE _rf_type_drv2halmac(RT_RF_TYPE_DEF_E rf_drv)
{
	HALMAC_RF_TYPE rf_mac;


	switch (rf_drv) {
	case RF_1T2R:
		rf_mac = HALMAC_RF_1T2R;
		break;
	case RF_2T4R:
		rf_mac = HALMAC_RF_2T4R;
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
	case RF_2T3R:
		rf_mac = HALMAC_RF_2T3R;
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
		rf_mac = (HALMAC_RF_TYPE)rf_drv;
		break;
	}

	return rf_mac;
}

static int _send_general_info(struct dvobj_priv *d)
{
	PADAPTER adapter;
	PHAL_DATA_TYPE hal;
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;
	HALMAC_GENERAL_INFO info;
	HALMAC_RET_STATUS status;
	u8 val8;


	adapter = d->padapters[IFACE_ID0];
	hal = GET_HAL_DATA(adapter);
	halmac = dvobj_to_halmac(d);
	if (!halmac)
		return -1;
	api = HALMAC_GET_API(halmac);

	_rtw_memset(&info, 0, sizeof(info));
	info.rfe_type = (u8)hal->RFEType;
	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, &val8);
	info.rf_type = _rf_type_drv2halmac(val8);

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


	adapter = d->padapters[IFACE_ID0];
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
	if (_FALSE == ok)
		goto out;

	/* StatePowerOn */

	/* DownloadFW */
	d->hmpriv.send_general_info = 0;
	if (fw && fwsize) {
		err = rtw_halmac_dlfw(d, fw, fwsize);
		if (err)
			goto out;
		fw_ok = _TRUE;
	}

	/* InitMACFlow */
	status = init_mac_flow(d);
	if (status != HALMAC_RET_SUCCESS)
		goto out;

	/* halmac_send_general_info */
	if (_TRUE == fw_ok) {
		d->hmpriv.send_general_info = 0;
		err = _send_general_info(d);
		if (err)
			goto out;
	} else
		d->hmpriv.send_general_info = 1;

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
	u32 fwmaxsize, size = 0;
	int err = 0;


	fwmaxsize = FIRMWARE_MAX_SIZE;
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


	adapter = d->padapters[IFACE_ID0];
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

int rtw_halmac_dlfw(struct dvobj_priv *d, u8 *fw, u32 fwsize)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	int err = 0;
	PHAL_DATA_TYPE hal;
	HALMAC_FW_VERSION fw_vesion;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	hal = GET_HAL_DATA(d->padapters[IFACE_ID0]);

	if ((!fw) || (!fwsize))
		return -1;

	/* 1. Driver Stop Tx */
	/* ToDo */

	/* 2. Driver Check Tx FIFO is empty */
	/* ToDo */

	/* 3. Config MAX download size */
#ifdef CONFIG_USB_HCI
	/* for USB do not exceed MAX_CMDBUF_SZ */
	api->halmac_cfg_max_dl_size(mac, 0x1000);
#elif defined CONFIG_PCIE_HCI                                                   
	/* required a even length from u32 */
        api->halmac_cfg_max_dl_size(mac, (MAX_CMDBUF_SZ - TXDESC_OFFSET) & 0xFFFFFFFE);
#endif

	/* 4. Download Firmware */
	status = api->halmac_download_firmware(mac, fw, fwsize);
	if (HALMAC_RET_SUCCESS != status)
		return -1;

	if (d->hmpriv.send_general_info) {
		d->hmpriv.send_general_info = 0;
		err = _send_general_info(d);
	}

	/* 5. Driver resume TX if needed */
	/* ToDo */

	/* 6. Reset driver variables if needed */
	hal->LastHMEBoxNum = 0;


	/* 7. Get FW version */
	status = api->halmac_get_fw_version(mac, &fw_vesion);
	if (status == HALMAC_RET_SUCCESS) {
		hal->FirmwareVersion = fw_vesion.version;
		hal->FirmwareSubVersion = fw_vesion.sub_version;
	}

	return err;
}

int rtw_halmac_dlfw_from_file(struct dvobj_priv *d, u8 *fwpath)
{
	u8 *fw = NULL;
	u32 fwmaxsize, size = 0;
	int err = 0;


	fwmaxsize = FIRMWARE_MAX_SIZE;
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


	adapter = d->padapters[IFACE_ID0];
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

	/* RTW_INFO("_is_fw_read_cmd_down, reg_1cc(%x), msg_box(%d)...\n", rtw_read8(adapter, REG_HMETFR), msgbox_num); */

	do {
		valid = rtw_read8(adapter, REG_HMETFR) & BIT(msgbox_num);
		if (0 == valid)
			read_down = _TRUE;
		else
			rtw_msleep_os(1);
	} while ((!read_down) && (retry_cnts--));

	return read_down;
}

int rtw_halmac_send_h2c(struct dvobj_priv *d, u8 *h2c)
{
	PADAPTER adapter = d->padapters[IFACE_ID0];
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 h2c_box_num = 0;
	u32 msgbox_addr = 0;
	u32 msgbox_ex_addr = 0;
	u32 h2c_cmd = 0;
	u32 h2c_cmd_ex = 0;
	s32 ret = _FAIL;

	if (adapter->bFWReady == _FALSE) {
		RTW_INFO("%s: return H2C cmd because fw is not ready\n", __FUNCTION__);
		return ret;
	}

	if (!h2c) {
		RTW_INFO("%s: pbuf is NULL\n", __FUNCTION__);
		return ret;
	}

	if (rtw_is_surprise_removed(adapter)) {
		RTW_INFO("%s: surprise removed\n", __FUNCTION__);
		return ret;
	}

	_enter_critical_mutex(&d->h2c_fwcmd_mutex, NULL);

	/* pay attention to if race condition happened in  H2C cmd setting */
	h2c_box_num = hal->LastHMEBoxNum;

	if (!_is_fw_read_cmd_down(adapter, h2c_box_num)) {
		RTW_INFO(" fw read cmd failed...\n");
		goto exit;
	}

	/* Write Ext command(byte 4 -7) */
	msgbox_ex_addr = REG_HMEBOX_E0 + (h2c_box_num * EX_MESSAGE_BOX_SIZE);
	_rtw_memcpy((u8 *)(&h2c_cmd_ex), h2c + 4, EX_MESSAGE_BOX_SIZE);
	h2c_cmd_ex = le32_to_cpu(h2c_cmd_ex);
	rtw_write32(adapter, msgbox_ex_addr, h2c_cmd_ex);

	/* Write command (byte 0 -3 ) */
	msgbox_addr = REG_HMEBOX0 + (h2c_box_num * MESSAGE_BOX_SIZE);
	_rtw_memcpy((u8 *)(&h2c_cmd), h2c, 4);
	h2c_cmd = le32_to_cpu(h2c_cmd);
	rtw_write32(adapter, msgbox_addr, h2c_cmd);

	/* update last msg box number */
	hal->LastHMEBoxNum = (h2c_box_num + 1) % MAX_H2C_BOX_NUMS;
	ret = _SUCCESS;

exit:
	_exit_critical_mutex(&d->h2c_fwcmd_mutex, NULL);
	return ret;
}

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

	status = api->halmac_dump_efuse_map(mac, HALMAC_EFUSE_R_AUTO);
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


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	for (i = 0; i < cnt; i++) {
		status = api->halmac_read_efuse(mac, offset + i, &v);
		if (HALMAC_RET_SUCCESS != status)
			return -1;
		data[i] = v;
	}

	return 0;
}

int rtw_halmac_write_physical_efuse(struct dvobj_priv *d, u32 offset, u32 cnt, u8 *data)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u32 i;


	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

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

int rtw_halmac_read_logical_efuse_map(struct dvobj_priv *d, u8 *map, u32 size)
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

	status = api->halmac_dump_logical_efuse_map(mac, HALMAC_EFUSE_R_AUTO);
	if (HALMAC_RET_SUCCESS != status) {
		free_halmac_event(d, id);
		return -1;
	}

	ret = wait_halmac_event(d, id);
	if (ret)
		return -1;

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

int rtw_halmac_dump_fifo(struct dvobj_priv *d, HAL_FIFO_SEL halmac_fifo_sel)
{
	PHALMAC_ADAPTER mac;
	PHALMAC_API api;
	HALMAC_RET_STATUS status;
	u8 *pfifo_map = NULL;
	u32 fifo_size = 0;
	s8	ret = 0;

	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);

	fifo_size = api->halmac_get_fifo_size(mac, halmac_fifo_sel);
	if (fifo_size)
		pfifo_map = rtw_vmalloc(fifo_size);
	if (pfifo_map == NULL)
		return -1;

	status = api->halmac_dump_fifo(mac, halmac_fifo_sel, pfifo_map, fifo_size);
	if (HALMAC_RET_SUCCESS != status) {
		ret = -1;
		goto _exit;
	}

_exit:
	if (pfifo_map)
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


	adapter = d->padapters[IFACE_ID0];
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

int rtw_halmac_get_wow_reason(struct dvobj_priv *d, u8 *reason)
{
	PADAPTER adapter;
	u8 val8;
	int err = -1;


	adapter = d->padapters[IFACE_ID0];

	val8 = rtw_read8(adapter, 0x1C7);
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
int rtw_halmac_get_drv_info_sz(struct dvobj_priv *d, u8 *sz)
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
int rtw_halmac_get_rsvd_drv_pg_bndy(struct dvobj_priv *dvobj, u16 *drv_pg)
{
	HALMAC_RET_STATUS status;
	PHALMAC_ADAPTER halmac = dvobj_to_halmac(dvobj);
	PHALMAC_API api = HALMAC_GET_API(halmac);

	status = api->halmac_get_hw_value(halmac, HALMAC_HW_RSVD_PG_BNDY, drv_pg);
	if (status != HALMAC_RET_SUCCESS)
		return -1;

	return 0;
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


	adapter = d->padapters[IFACE_ID0];
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

	adapter = d->padapters[IFACE_ID0];
	mac = dvobj_to_halmac(d);
	api = HALMAC_GET_API(mac);
	halmac_usb_mode = _usb_mode_drv2halmac(usb_mode);
	status = api->halmac_set_hw_value(mac, HALMAC_HW_USB_MODE, (void *)&halmac_usb_mode);

	if (HALMAC_RET_SUCCESS != status)
		return _FAIL;

	return _SUCCESS;
}
#endif /* CONFIG_USB_HCI */
