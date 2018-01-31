/* SPDX-License-Identifier: GPL-2.0 */
#include "halmac_2_platform.h"
#include "halmac_type.h"
#if HALMAC_PLATFORM_WINDOWS == 1
#include "halmac_88xx/halmac_api_win8822b.h"
#include "halmac_88xx/halmac_win8822b_cfg.h"
#include "halmac_88xx/halmac_api_win8821c.h"
#include "halmac_88xx/halmac_win8821c_cfg.h"
#include "halmac_88xx/halmac_api_win8197f.h"
#include "halmac_88xx/halmac_win8197f_cfg.h"
#else
#include "halmac_88xx/halmac_api_88xx.h"
#include "halmac_88xx/halmac_88xx_cfg.h"
#endif
#include "halmac_88xx/halmac_8822b/halmac_8822b_cfg.h"
#include "halmac_88xx/halmac_8821c/halmac_8821c_cfg.h"
#include "halmac_88xx/halmac_8197f/halmac_8197f_cfg.h"


HALMAC_RET_STATUS
halmac_check_platform_api(
	IN VOID *pDriver_adapter,
	IN HALMAC_INTERFACE halmac_interface,
	IN PHALMAC_PLATFORM_API pHalmac_platform_api
);

/**
 * halmac_init_adapter() - init halmac_adapter
 * @pDriver_adapter
 * @pHalmac_platform_api : platform api for halmac used
 * @halmac_interface : PCIE, USB, or SDIO
 * @ppHalmac_adapter
 * @ppHalmac_api
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_init_adapter(
	IN VOID	*pDriver_adapter,
	IN PHALMAC_PLATFORM_API pHalmac_platform_api,
	IN HALMAC_INTERFACE	halmac_interface,
	OUT	PHALMAC_ADAPTER *ppHalmac_adapter,
	OUT	PHALMAC_API *ppHalmac_api
)
{
	PHALMAC_ADAPTER pHalmac_adapter = (PHALMAC_ADAPTER)NULL;
	HALMAC_RET_STATUS status = HALMAC_RET_SUCCESS;

#if HALMAC_PLATFORM_WINDOWS == 1
	u8 chip_id = 0;
	u32 polling_count;
#endif
	union {
		u32	i;
		u8	x[4];
	} ENDIAN_CHECK = { 0x01000000 };

	status = halmac_check_platform_api(pDriver_adapter, halmac_interface, pHalmac_platform_api);
	if (HALMAC_RET_SUCCESS != status)
		return status;
	pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, HALMAC_SVN_VER "\n");
	pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "HALMAC_MAJOR_VER = %x\n", HALMAC_MAJOR_VER);
	pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "HALMAC_PROTOTYPE_VER = %x\n", HALMAC_PROTOTYPE_VER);
	pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "HALMAC_MINOR_VER = %x\n", HALMAC_MINOR_VER);

	pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_adapter_88xx ==========>\n");

	/* Check endian setting - Little endian : 1, Big endian : 0*/
	if (HALMAC_SYSTEM_ENDIAN == ENDIAN_CHECK.x[0]) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Endian setting Err!!\n");
		pHalmac_adapter = (PHALMAC_ADAPTER)NULL;
		return HALMAC_RET_ENDIAN_ERR;
	}

	pHalmac_adapter = (PHALMAC_ADAPTER)pHalmac_platform_api->RTL_MALLOC(pDriver_adapter, sizeof(HALMAC_ADAPTER));
	if (NULL == pHalmac_adapter) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Malloc HAL Adapter Err!!\n");
		return HALMAC_RET_MALLOC_FAIL;
	}

	/* return halmac adapter address to caller */
	*ppHalmac_adapter = pHalmac_adapter;

	/* Record caller info */
	pHalmac_adapter->pHalmac_platform_api = pHalmac_platform_api;
	pHalmac_adapter->pDriver_adapter = pDriver_adapter;
	pHalmac_adapter->halmac_interface = halmac_interface;

	PLATFORM_MUTEX_INIT(pDriver_adapter, &(pHalmac_adapter->EfuseMutex));
	PLATFORM_MUTEX_INIT(pDriver_adapter, &(pHalmac_adapter->h2c_seq_mutex));

	/* Assign function pointer to halmac API */
#if HALMAC_PLATFORM_WINDOWS == 0
	halmac_init_adapter_para_88xx(pHalmac_adapter);
	status = halmac_mount_api_88xx(pHalmac_adapter);
#else
	/* Get Chip_id and Chip_version */
	if (HALMAC_INTERFACE_SDIO == pHalmac_adapter->halmac_interface) {
		chip_id = pHalmac_platform_api->SDIO_CMD52_READ(pDriver_adapter, REG_SYS_CFG2);
		if (chip_id == 0xEA)
			pHalmac_platform_api->SDIO_CMD52_WRITE(pDriver_adapter, REG_SDIO_HSUS_CTRL, pHalmac_platform_api->SDIO_CMD52_READ(pHalmac_adapter, REG_SDIO_HSUS_CTRL) & ~(BIT(0)));

		polling_count = HALMAC_POLLING_READY_TIMEOUT_COUNT;
		while (!(pHalmac_platform_api->SDIO_CMD52_READ(pDriver_adapter, REG_SDIO_HSUS_CTRL) & 0x02)) {
			polling_count--;
			if (polling_count == 0)
				return HALMAC_RET_SDIO_LEAVE_SUSPEND_FAIL;
		}

		chip_id = pHalmac_platform_api->SDIO_CMD52_READ(pDriver_adapter, REG_SYS_CFG2);
	} else {
		chip_id = pHalmac_platform_api->REG_READ_8(pDriver_adapter, REG_SYS_CFG2);
	}

#if HALMAC_8822B_SUPPORT
	if (HALMAC_CHIP_ID_HW_DEF_8822B == chip_id) {
		halmac_init_adapter_para_win8822b(pHalmac_adapter);
		status = halmac_mount_api_win8822b(pHalmac_adapter);
	}
#endif
#if HALMAC_8821C_SUPPORT
	if (HALMAC_CHIP_ID_HW_DEF_8821C == chip_id) {
		halmac_init_adapter_para_win8821c(pHalmac_adapter);
		status = halmac_mount_api_win8821c(pHalmac_adapter);
	}
#endif
#if HALMAC_8197F_SUPPORT
	if (HALMAC_CHIP_ID_HW_DEF_8197F == chip_id) {
		halmac_init_adapter_para_win8197f(pHalmac_adapter);
		status = halmac_mount_api_win8197f(pHalmac_adapter);
	}
#endif

#endif

	/* Return halmac API function pointer */
	*ppHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_adapter_88xx <==========\n");

	return status;
}

/**
 * halmac_halt_api() - halt all halmac api
 * @pHalmac_adapter
 * Author : Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_halt_api(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_PLATFORM_API pHalmac_platform_api = (PHALMAC_PLATFORM_API)NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_platform_api = pHalmac_adapter->pHalmac_platform_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_halt_api ==========>\n");
	pHalmac_adapter->halmac_state.api_state = HALMAC_API_STATE_HALT;
	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_halt_api ==========>\n");
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_deinit_adapter() - deinit halmac adapter
 * @pHalmac_adapter
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_deinit_adapter(
	IN PHALMAC_ADAPTER pHalmac_adapter
)
{
	VOID *pDriver_adapter = NULL;
	PHALMAC_PLATFORM_API pHalmac_platform_api = (PHALMAC_PLATFORM_API)NULL;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_platform_api = pHalmac_adapter->pHalmac_platform_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_deinit_adapter_88xx ==========>\n");

	PLATFORM_MUTEX_DEINIT(pDriver_adapter, &(pHalmac_adapter->EfuseMutex));
	PLATFORM_MUTEX_DEINIT(pDriver_adapter, &(pHalmac_adapter->h2c_seq_mutex));

	if (NULL != pHalmac_adapter->pHalEfuse_map) {
		PLATFORM_RTL_FREE(pDriver_adapter, pHalmac_adapter->pHalEfuse_map, pHalmac_adapter->hw_config_info.efuse_size);
		pHalmac_adapter->pHalEfuse_map = (u8 *)NULL;
	}

	if (NULL != pHalmac_adapter->halmac_state.psd_set.pData) {
		PLATFORM_RTL_FREE(pDriver_adapter, pHalmac_adapter->halmac_state.psd_set.pData, pHalmac_adapter->halmac_state.psd_set.data_size);
		pHalmac_adapter->halmac_state.psd_set.pData = (u8 *)NULL;
	}

	if (NULL != pHalmac_adapter->pHalmac_api) {
		PLATFORM_RTL_FREE(pDriver_adapter, pHalmac_adapter->pHalmac_api, sizeof(HALMAC_API));
		pHalmac_adapter->pHalmac_api = NULL;
	}

	if (NULL != pHalmac_adapter) {
		pHalmac_adapter->pHalAdapter_backup = NULL;
		PLATFORM_RTL_FREE(pDriver_adapter, pHalmac_adapter, sizeof(HALMAC_ADAPTER));
		pHalmac_adapter = (PHALMAC_ADAPTER)NULL;
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_check_platform_api() - check platform api pointers
 * @pDriver_adapter
 * @halmac_interface : PCIE, USB or SDIO
 * @pHalmac_platform_api
 * Author : KaiYuan Chang
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_check_platform_api(
	IN VOID *pDriver_adapter,
	IN HALMAC_INTERFACE	halmac_interface,
	IN PHALMAC_PLATFORM_API pHalmac_platform_api
)
{
	VOID *pAdapter_Local = NULL;

	pAdapter_Local = pDriver_adapter;

	if (NULL == pHalmac_platform_api)
		return HALMAC_RET_PLATFORM_API_NULL;

	if (NULL == pHalmac_platform_api->MSG_PRINT)
		return HALMAC_RET_PLATFORM_API_NULL;

	if (HALMAC_INTERFACE_SDIO == halmac_interface) {
		if (NULL == pHalmac_platform_api->SDIO_CMD52_READ) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->SDIO_CMD52_READ)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->SDIO_CMD53_READ_8) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->SDIO_CMD53_READ_8)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->SDIO_CMD53_READ_16) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->SDIO_CMD53_READ_16)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->SDIO_CMD53_READ_32) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->SDIO_CMD53_READ_32)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->SDIO_CMD52_WRITE) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->SDIO_CMD52_WRITE)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->SDIO_CMD53_WRITE_8) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->SDIO_CMD53_WRITE_8)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->SDIO_CMD53_WRITE_16) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->SDIO_CMD53_WRITE_16)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->SDIO_CMD53_WRITE_32) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->SDIO_CMD53_WRITE_32)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
	}

	if ((HALMAC_INTERFACE_USB == halmac_interface) || (HALMAC_INTERFACE_PCIE == halmac_interface)) {
		if (NULL == pHalmac_platform_api->REG_READ_8) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->REG_READ_8)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->REG_READ_16) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->REG_READ_16)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->REG_READ_32) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->REG_READ_32)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->REG_WRITE_8) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->REG_WRITE_8)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->REG_WRITE_16) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->REG_WRITE_16)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
		if (NULL == pHalmac_platform_api->REG_WRITE_32) {
			pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->REG_WRITE_32)\n");
			return HALMAC_RET_PLATFORM_API_NULL;
		}
	}

	if (NULL == pHalmac_platform_api->RTL_FREE) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->RTL_FREE)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}

	if (NULL == pHalmac_platform_api->RTL_MALLOC) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->RTL_MALLOC)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (NULL == pHalmac_platform_api->RTL_MEMCPY) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->RTL_MEMCPY)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (NULL == pHalmac_platform_api->RTL_MEMSET) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->RTL_MEMSET)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (NULL == pHalmac_platform_api->RTL_DELAY_US) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->RTL_DELAY_US)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}

	if (NULL == pHalmac_platform_api->MUTEX_INIT) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->MUTEX_INIT)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (NULL == pHalmac_platform_api->MUTEX_DEINIT) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->MUTEX_DEINIT)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (NULL == pHalmac_platform_api->MUTEX_LOCK) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->MUTEX_LOCK)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (NULL == pHalmac_platform_api->MUTEX_UNLOCK) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->MUTEX_UNLOCK)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}
	if (NULL == pHalmac_platform_api->EVENT_INDICATION) {
		pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "(NULL==pHalmac_platform_api->EVENT_INDICATION)\n");
		return HALMAC_RET_PLATFORM_API_NULL;
	}

	pHalmac_platform_api->MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_check_platform_api ==========>\n");

	return HALMAC_RET_SUCCESS;
}

HALMAC_RET_STATUS
halmac_get_version(
	OUT	HALMAC_VER * version
)
{
	version->major_ver = (u8)HALMAC_MAJOR_VER;
	version->prototype_ver = (u8)HALMAC_PROTOTYPE_VER;
	version->minor_ver = (u8)HALMAC_MINOR_VER;

	return HALMAC_RET_SUCCESS;
}
