/******************************************************************************
 *
 * Copyright(c) 2018 - 2019 Realtek Corporation. All rights reserved.
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
#include "halmac_dbg.h"
#if HALMAC_DBG_MONITOR_IO
static u8
monitor_reg_read_8(struct halmac_adapter *adapter, u32 offset,
		   const char *func, const u32 line);
static u16
monitor_reg_read_16(struct halmac_adapter *adapter, u32 offset,
		    const char *func, const u32 line);
static u32
monitor_reg_read_32(struct halmac_adapter *adapter, u32 offset,
		    const char *func, const u32 line);
static enum halmac_ret_status
monitor_reg_sdio_cmd53_read_n(struct halmac_adapter *adapter,
			      u32 offset, u32 size, u8 *value,
			      const char *func, const u32 line);
static enum halmac_ret_status
monitor_reg_write_8(struct halmac_adapter *adapter, u32 offset,
		    u8 value, const char *func, const u32 line);
static enum halmac_ret_status
monitor_reg_write_16(struct halmac_adapter *adapter, u32 offset,
		     u16 value, const char *func, const u32 line);
static enum halmac_ret_status
monitor_reg_write_32(struct halmac_adapter *adapter, u32 offset,
		     u32 value, const char *func, const u32 line);

enum halmac_ret_status
mount_api_dbg(struct halmac_adapter *adapter)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	api->halmac_mon_reg_read_8 = monitor_reg_read_8;
	api->halmac_mon_reg_read_16 = monitor_reg_read_16;
	api->halmac_mon_reg_read_32 = monitor_reg_read_32;
	api->halmac_mon_reg_sdio_cmd53_read_n = monitor_reg_sdio_cmd53_read_n;
	api->halmac_mon_reg_write_8 = monitor_reg_write_8;
	api->halmac_mon_reg_write_16 = monitor_reg_write_16;
	api->halmac_mon_reg_write_32 = monitor_reg_write_32;

	return HALMAC_RET_SUCCESS;
}

u8
monitor_reg_read_8(struct halmac_adapter *adapter, u32 offset,
		   const char *func, const u32 line)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u8 val;

	val = api->halmac_reg_read_8(adapter, offset);
	PLTFM_MONITOR_READ(offset, 1, val, func, line);
	return val;
}

u16
monitor_reg_read_16(struct halmac_adapter *adapter, u32 offset,
		    const char *func, const u32 line)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u16 val;

	val = api->halmac_reg_read_16(adapter, offset);
	PLTFM_MONITOR_READ(offset, 2, val, func, line);
	return val;
}

u32
monitor_reg_read_32(struct halmac_adapter *adapter, u32 offset,
		    const char *func, const u32 line)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u32 val;

	val = api->halmac_reg_read_32(adapter, offset);
	PLTFM_MONITOR_READ(offset, 4, val, func, line);
	return val;
}

enum halmac_ret_status
monitor_reg_sdio_cmd53_read_n(struct halmac_adapter *adapter,
			      u32 offset, u32 size, u8 *value,
			      const char *func, const u32 line)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MONITOR_READ(offset, size, 0, func, line);
	return api->halmac_reg_sdio_cmd53_read_n(adapter, offset, size, value);
}

enum halmac_ret_status
monitor_reg_write_8(struct halmac_adapter *adapter, u32 offset,
		    u8 value, const char *func, const u32 line)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MONITOR_WRITE(offset, 1, value, func, line);
	return api->halmac_reg_write_8(adapter, offset, value);
}

enum halmac_ret_status
monitor_reg_write_16(struct halmac_adapter *adapter, u32 offset,
		     u16 value, const char *func, const u32 line)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MONITOR_WRITE(offset, 2, value, func, line);
	return api->halmac_reg_write_16(adapter, offset, value);
}

enum halmac_ret_status
monitor_reg_write_32(struct halmac_adapter *adapter, u32 offset,
		     u32 value, const char *func, const u32 line)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MONITOR_WRITE(offset, 4, value, func, line);
	return api->halmac_reg_write_32(adapter, offset, value);
}
#endif
