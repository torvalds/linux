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

#include "halmac_type.h"
#include "halmac_api.h"

#if (HALMAC_PLATFORM_WINDOWS)

#if HALMAC_8822B_SUPPORT
#include "halmac_88xx/halmac_init_win8822b.h"
#endif

#if HALMAC_8821C_SUPPORT
#include "halmac_88xx/halmac_init_win8821c.h"
#endif

#if HALMAC_8814B_SUPPORT
#include "halmac_88xx_v1/halmac_init_win8814b_v1.h"
#endif

#if HALMAC_8822C_SUPPORT
#include "halmac_88xx/halmac_init_win8822c.h"
#endif

#if HALMAC_8812F_SUPPORT
#include "halmac_88xx/halmac_init_win8812f.h"
#endif

#else

#if HALMAC_88XX_SUPPORT
#include "halmac_88xx/halmac_init_88xx.h"
#endif
#if HALMAC_88XX_V1_SUPPORT
#include "halmac_88xx_v1/halmac_init_88xx_v1.h"
#if defined(HALMAC_DATA_CPU_EN)
#include "halmac_88xxd_v1/halmac_init_88xxd_v1.h"
#endif
#endif

#endif

enum chip_id_hw_def {
	CHIP_ID_HW_DEF_8723A = 0x01,
	CHIP_ID_HW_DEF_8188E = 0x02,
	CHIP_ID_HW_DEF_8881A = 0x03,
	CHIP_ID_HW_DEF_8812A = 0x04,
	CHIP_ID_HW_DEF_8821A = 0x05,
	CHIP_ID_HW_DEF_8723B = 0x06,
	CHIP_ID_HW_DEF_8192E = 0x07,
	CHIP_ID_HW_DEF_8814A = 0x08,
	CHIP_ID_HW_DEF_8821C = 0x09,
	CHIP_ID_HW_DEF_8822B = 0x0A,
	CHIP_ID_HW_DEF_8703B = 0x0B,
	CHIP_ID_HW_DEF_8188F = 0x0C,
	CHIP_ID_HW_DEF_8192F = 0x0D,
	CHIP_ID_HW_DEF_8197F = 0x0E,
	CHIP_ID_HW_DEF_8723D = 0x0F,
	CHIP_ID_HW_DEF_8814B = 0x11,
	CHIP_ID_HW_DEF_8822C = 0x13,
	CHIP_ID_HW_DEF_8812F = 0x14,
	CHIP_ID_HW_DEF_UNDEFINE = 0x7F,
	CHIP_ID_HW_DEF_PS = 0xEA,
};

static enum halmac_ret_status
chk_pltfm_api(void *drv_adapter, enum halmac_interface intf,
	      struct halmac_platform_api *pltfm_api);

static enum halmac_ret_status
get_chip_info(void *drv_adapter, struct halmac_platform_api *pltfm_api,
	      enum halmac_interface intf, struct halmac_adapter *adapter);

static u8
pltfm_reg_r8_sdio(void *drv_adapter, struct halmac_platform_api *pltfm_api,
		  u32 offset);

static enum halmac_ret_status
pltfm_reg_w8_sdio(void *drv_adapter, struct halmac_platform_api *pltfm_api,
		  u32 offset, u8 data);

static u8
pltfm_reg_r_indir_sdio(void *drv_adapter, struct halmac_platform_api *pltfm_api,
		       u32 offset);

static enum halmac_ret_status
cnv_to_sdio_bus_offset(u32 *offset);

/**
 * halmac_init_adapter() - init halmac_adapter
 * @drv_adapter : the adapter of caller
 * @pltfm_api : the platform APIs which is used in halmac
 * @intf : bus interface
 * @halmac_adapter : the adapter of halmac
 * @halmac_api : the function pointer of APIs
 * Author : KaiYuan Chang / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_init_adapter(void *drv_adapter, struct halmac_platform_api *pltfm_api,
		    enum halmac_interface intf,
		    struct halmac_adapter **halmac_adapter,
		    struct halmac_api **halmac_api)
{
	struct halmac_adapter *adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	u8 *buf = NULL;

	union {
		u32 i;
		u8 x[4];
	} ENDIAN_CHECK = { 0x01000000 };

	status = chk_pltfm_api(drv_adapter, intf, pltfm_api);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ALWAYS,
			     HALMAC_SVN_VER "\n"
			     "HALMAC_MAJOR_VER = %d\n"
			     "HALMAC_PROTOTYPE_VER = %d\n"
			     "HALMAC_MINOR_VER = %d\n"
			     "HALMAC_PATCH_VER = %d\n",
			     HALMAC_MAJOR_VER, HALMAC_PROTOTYPE_VER,
			     HALMAC_MINOR_VER, HALMAC_PATCH_VER);

	if (ENDIAN_CHECK.x[0] == HALMAC_SYSTEM_ENDIAN) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR,
				     "[ERR]Endian setting err!!\n");
		return HALMAC_RET_ENDIAN_ERR;
	}

	buf = (u8 *)pltfm_api->RTL_MALLOC(drv_adapter, sizeof(*adapter));

	if (!buf) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR,
				     "[ERR]Malloc HAL adapter err!!\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	pltfm_api->RTL_MEMSET(drv_adapter, buf, 0x00, sizeof(*adapter));
	adapter = (struct halmac_adapter *)buf;

	*halmac_adapter = adapter;

	adapter->pltfm_api = pltfm_api;
	adapter->drv_adapter = drv_adapter;
	intf = (intf == HALMAC_INTERFACE_AXI) ? HALMAC_INTERFACE_PCIE : intf;
	adapter->intf = intf;

	if (get_chip_info(drv_adapter, pltfm_api, intf, adapter)
	    != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(*halmac_adapter, sizeof(**halmac_adapter));
		*halmac_adapter = NULL;
		return HALMAC_RET_CHIP_NOT_SUPPORT;
	}

	PLTFM_MUTEX_INIT(&adapter->efuse_mutex);
	PLTFM_MUTEX_INIT(&adapter->h2c_seq_mutex);
	PLTFM_MUTEX_INIT(&adapter->sdio_indir_mutex);

#if (HALMAC_PLATFORM_WINDOWS == 0)

#if HALMAC_88XX_SUPPORT
	if (adapter->chip_id == HALMAC_CHIP_ID_8822B ||
	    adapter->chip_id == HALMAC_CHIP_ID_8821C ||
	    adapter->chip_id == HALMAC_CHIP_ID_8822C ||
	    adapter->chip_id == HALMAC_CHIP_ID_8812F) {
		init_adapter_param_88xx(adapter);
		status = mount_api_88xx(adapter);
	}
#endif

#if HALMAC_88XX_V1_SUPPORT
	if (adapter->chip_id == HALMAC_CHIP_ID_8814B) {
		init_adapter_param_88xx_v1(adapter);
		status = mount_api_88xx_v1(adapter);
	}
#if defined(HALMAC_DATA_CPU_EN)
	if (adapter->chip_id == HALMAC_CHIP_ID_8814B) {
		init_adapter_param_88xxd_v1(adapter);
		status = mount_api_88xxd_v1(adapter);
	}
#endif
#endif

#else

#if HALMAC_8822B_SUPPORT
	if (adapter->chip_id == HALMAC_CHIP_ID_8822B) {
		init_adapter_param_win8822b(adapter);
		status = mount_api_win8822b(adapter);
	}
#endif

#if HALMAC_8821C_SUPPORT
	if (adapter->chip_id == HALMAC_CHIP_ID_8821C) {
		init_adapter_param_win8821c(adapter);
		status = mount_api_win8821c(adapter);
	}
#endif

#if HALMAC_8814B_SUPPORT
	if (adapter->chip_id == HALMAC_CHIP_ID_8814B) {
		init_adapter_param_win8814b_v1(adapter);
		status = mount_api_win8814b_v1(adapter);
	}
#endif

#if HALMAC_8822C_SUPPORT
	if (adapter->chip_id == HALMAC_CHIP_ID_8822C) {
		init_adapter_param_win8822c(adapter);
		status = mount_api_win8822c(adapter);
	}
#endif

#if HALMAC_8812F_SUPPORT
	if (adapter->chip_id == HALMAC_CHIP_ID_8812F) {
		init_adapter_param_win8812f(adapter);
		status = mount_api_win8812f(adapter);
	}
#endif

#endif
	*halmac_api = (struct halmac_api *)adapter->halmac_api;

#if HALMAC_DBG_MONITOR_IO
	mount_api_dbg(adapter);
#endif
	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return status;
}

/**
 * halmac_halt_api() - stop halmac_api action
 * @adapter : the adapter of halmac
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_halt_api(struct halmac_adapter *adapter)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	adapter->halmac_state.api_state = HALMAC_API_STATE_HALT;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_deinit_adapter() - deinit halmac adapter
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_deinit_adapter(struct halmac_adapter *adapter)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	PLTFM_MUTEX_DEINIT(&adapter->efuse_mutex);
	PLTFM_MUTEX_DEINIT(&adapter->h2c_seq_mutex);
	PLTFM_MUTEX_DEINIT(&adapter->sdio_indir_mutex);

	if (adapter->efuse_map) {
		PLTFM_FREE(adapter->efuse_map, adapter->hw_cfg_info.efuse_size);
		adapter->efuse_map = (u8 *)NULL;
	}

	if (adapter->sdio_fs.macid_map) {
		PLTFM_FREE(adapter->sdio_fs.macid_map,
			   adapter->sdio_fs.macid_map_size);
		adapter->sdio_fs.macid_map = (u8 *)NULL;
	}

	if (adapter->halmac_state.psd_state.data) {
		PLTFM_FREE(adapter->halmac_state.psd_state.data,
			   adapter->halmac_state.psd_state.data_size);
		adapter->halmac_state.psd_state.data = (u8 *)NULL;
	}

	if (adapter->halmac_api) {
		PLTFM_FREE(adapter->halmac_api, sizeof(struct halmac_api));
		adapter->halmac_api = NULL;
	}

	PLTFM_FREE(adapter, sizeof(*adapter));

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
chk_pltfm_api(void *drv_adapter, enum halmac_interface intf,
	      struct halmac_platform_api *pltfm_api)
{
	if (!pltfm_api)
		return HALMAC_RET_PLATFORM_API_NULL;

	if (!pltfm_api->MSG_PRINT)
		return HALMAC_RET_PLATFORM_API_NULL;

	if (intf == HALMAC_INTERFACE_SDIO) {
		if (!pltfm_api->SDIO_CMD52_READ) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-r\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD53_READ_8) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-r8\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD53_READ_16) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-r16\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD53_READ_32) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-r32\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD53_READ_N) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-rn\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD52_WRITE) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-w\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD53_WRITE_8) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-w8\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD53_WRITE_16) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-w16\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD53_WRITE_32) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-w32\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->SDIO_CMD52_CIA_READ) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]sdio-cia\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
	}

	if (intf == HALMAC_INTERFACE_USB || intf == HALMAC_INTERFACE_PCIE) {
		if (!pltfm_api->REG_READ_8) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]reg-r8\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->REG_READ_16) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]reg-r16\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->REG_READ_32) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]reg-r32\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->REG_WRITE_8) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]reg-w8\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->REG_WRITE_16) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]reg-w16\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (!pltfm_api->REG_WRITE_32) {
			pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
					     HALMAC_DBG_ERR, "[ERR]reg-w32\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
	}

	if (!pltfm_api->RTL_FREE) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]mem-free\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}

	if (!pltfm_api->RTL_MALLOC) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]mem-malloc\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (!pltfm_api->RTL_MEMCPY) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]mem-cpy\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (!pltfm_api->RTL_MEMSET) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]mem-set\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (!pltfm_api->RTL_DELAY_US) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]time-delay\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}

	if (!pltfm_api->MUTEX_INIT) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]mutex-init\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (!pltfm_api->MUTEX_DEINIT) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]mutex-deinit\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (!pltfm_api->MUTEX_LOCK) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]mutex-lock\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (!pltfm_api->MUTEX_UNLOCK) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]mutex-unlock\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (!pltfm_api->EVENT_INDICATION) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]event-indication\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
#if HALMAC_DBG_MONITOR_IO
	if (!pltfm_api->READ_MONITOR) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]read-monitor\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (!pltfm_api->WRITE_MONITOR) {
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]write-monitor\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
#endif
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_version() - get HALMAC version
 * @version : return version of major, prototype and minor information
 * Author : KaiYuan Chang / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
halmac_get_version(struct halmac_ver *version)
{
	version->major_ver = (u8)HALMAC_MAJOR_VER;
	version->prototype_ver = (u8)HALMAC_PROTOTYPE_VER;
	version->minor_ver = (u8)HALMAC_MINOR_VER;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
get_chip_info(void *drv_adapter, struct halmac_platform_api *pltfm_api,
	      enum halmac_interface intf, struct halmac_adapter *adapter)
{
	u8 chip_id;
	u8 chip_ver;
	u32 cnt;

	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
		pltfm_reg_w8_sdio(drv_adapter, pltfm_api, REG_SDIO_HSUS_CTRL,
				  pltfm_reg_r8_sdio(drv_adapter, pltfm_api,
						    REG_SDIO_HSUS_CTRL) &
						    ~(BIT(0)));

		cnt = 10000;
		while (!(pltfm_reg_r8_sdio(drv_adapter, pltfm_api,
					   REG_SDIO_HSUS_CTRL) & BIT(1))) {
			cnt--;
			if (cnt == 0)
				return HALMAC_RET_SDIO_LEAVE_SUSPEND_FAIL;
		}

		chip_id = pltfm_reg_r_indir_sdio(drv_adapter, pltfm_api,
						 REG_SYS_CFG2);
		chip_ver = pltfm_reg_r_indir_sdio(drv_adapter, pltfm_api,
						  REG_SYS_CFG1 + 1) >> 4;
	} else {
		chip_id = pltfm_api->REG_READ_8(drv_adapter, REG_SYS_CFG2);
		chip_ver = pltfm_api->REG_READ_8(drv_adapter,
						 REG_SYS_CFG1 + 1) >> 4;
	}

	adapter->chip_ver = (enum halmac_chip_ver)chip_ver;

	if (chip_id == CHIP_ID_HW_DEF_8822B) {
		adapter->chip_id = HALMAC_CHIP_ID_8822B;
	} else if (chip_id == CHIP_ID_HW_DEF_8821C) {
		adapter->chip_id = HALMAC_CHIP_ID_8821C;
	} else if (chip_id == CHIP_ID_HW_DEF_8814B) {
		adapter->chip_id = HALMAC_CHIP_ID_8814B;
	} else if (chip_id == CHIP_ID_HW_DEF_8197F) {
		adapter->chip_id = HALMAC_CHIP_ID_8197F;
	} else if (chip_id == CHIP_ID_HW_DEF_8822C) {
		adapter->chip_id = HALMAC_CHIP_ID_8822C;
	} else if (chip_id == CHIP_ID_HW_DEF_8812F) {
		adapter->chip_id = HALMAC_CHIP_ID_8812F;
	} else {
		adapter->chip_id = HALMAC_CHIP_ID_UNDEFINE;
		PLTFM_MSG_ERR("[ERR]Chip id is undefined\n");
		return HALMAC_RET_CHIP_NOT_SUPPORT;
	}

	return HALMAC_RET_SUCCESS;
}

static u8
pltfm_reg_r8_sdio(void *drv_adapter, struct halmac_platform_api *pltfm_api,
		  u32 offset)
{
	u8 value8;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (0 == (offset & 0xFFFF0000))
		offset |= WLAN_IOREG_OFFSET;

	status = cnv_to_sdio_bus_offset(&offset);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	value8 = pltfm_api->SDIO_CMD52_READ(drv_adapter, offset);

	return value8;
}

static enum halmac_ret_status
pltfm_reg_w8_sdio(void *drv_adapter, struct halmac_platform_api *pltfm_api,
		  u32 offset, u8 data)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (0 == (offset & 0xFFFF0000))
		offset |= WLAN_IOREG_OFFSET;

	status = cnv_to_sdio_bus_offset(&offset);

	if (status != HALMAC_RET_SUCCESS)
		return status;

	pltfm_api->SDIO_CMD52_WRITE(drv_adapter, offset, data);

	return HALMAC_RET_SUCCESS;
}

static u8
pltfm_reg_r_indir_sdio(void *drv_adapter, struct halmac_platform_api *pltfm_api,
		       u32 offset)
{
	u8 value8, tmp, cnt = 50;
	u32 reg_cfg = REG_SDIO_INDIRECT_REG_CFG;
	u32 reg_data = REG_SDIO_INDIRECT_REG_DATA;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = cnv_to_sdio_bus_offset(&reg_cfg);
	if (status != HALMAC_RET_SUCCESS)
		return status;
	status = cnv_to_sdio_bus_offset(&reg_data);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	pltfm_api->SDIO_CMD52_WRITE(drv_adapter, reg_cfg, (u8)offset);
	pltfm_api->SDIO_CMD52_WRITE(drv_adapter, reg_cfg + 1,
				    (u8)(offset >> 8));
	pltfm_api->SDIO_CMD52_WRITE(drv_adapter, reg_cfg + 2,
				    (u8)(BIT(3) | BIT(4)));

	do {
		tmp = pltfm_api->SDIO_CMD52_READ(drv_adapter, reg_cfg + 2);
		cnt--;
	} while (((tmp & BIT(4)) == 0) && (cnt > 0));

	if (((cnt & BIT(4)) == 0) && cnt == 0)
		pltfm_api->MSG_PRINT(drv_adapter, HALMAC_MSG_INIT,
				     HALMAC_DBG_ERR, "[ERR]sdio indir read\n");

	value8 = pltfm_api->SDIO_CMD52_READ(drv_adapter, reg_data);

	return value8;
}

/*Note: copy from cnv_to_sdio_bus_offset_88xx*/
static enum halmac_ret_status
cnv_to_sdio_bus_offset(u32 *offset)
{
	switch ((*offset) & 0xFFFF0000) {
	case WLAN_IOREG_OFFSET:
		*offset &= HALMAC_WLAN_MAC_REG_MSK;
		*offset |= HALMAC_SDIO_CMD_ADDR_MAC_REG << 13;
		break;
	case SDIO_LOCAL_OFFSET:
		*offset &= HALMAC_SDIO_LOCAL_MSK;
		*offset |= HALMAC_SDIO_CMD_ADDR_SDIO_REG << 13;
		break;
	default:
		*offset = 0xFFFFFFFF;
		return HALMAC_RET_CONVERT_SDIO_OFFSET_FAIL;
	}

	return HALMAC_RET_SUCCESS;
}

