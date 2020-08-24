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

#include "halmac_efuse_88xx.h"
#include "halmac_88xx_cfg.h"
#include "halmac_common_88xx.h"
#include "halmac_init_88xx.h"

#if HALMAC_88XX_SUPPORT

#define RSVD_EFUSE_SIZE		16
#define RSVD_CS_EFUSE_SIZE	24
#define FEATURE_DUMP_PHY_EFUSE	HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE
#define FEATURE_DUMP_LOG_EFUSE	HALMAC_FEATURE_DUMP_LOGICAL_EFUSE
#define FEATURE_DUMP_LOG_EFUSE_MASK	HALMAC_FEATURE_DUMP_LOGICAL_EFUSE_MASK

#define SUPER_USB_ZONE0_START	0x150
#define SUPER_USB_ZONE0_END	0x199
#define SUPER_USB_ZONE1_START	0x200
#define SUPER_USB_ZONE1_END	0x217
#define SUPER_USB_RE_PG_CK_ZONE0_START	0x15D
#define SUPER_USB_RE_PG_CK_ZONE0_END	0x164

static enum halmac_cmd_construct_state
efuse_cmd_cnstr_state_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
proc_dump_efuse_88xx(struct halmac_adapter *adapter,
		     enum halmac_efuse_read_cfg cfg);

static enum halmac_ret_status
read_hw_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u32 size,
		   u8 *map);

static enum halmac_ret_status
read_log_efuse_map_88xx(struct halmac_adapter *adapter, u8 *map);

static enum halmac_ret_status
proc_pg_efuse_by_map_88xx(struct halmac_adapter *adapter,
			  struct halmac_pg_efuse_info *info,
			  enum halmac_efuse_read_cfg cfg);

static enum halmac_ret_status
dump_efuse_fw_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
dump_efuse_drv_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
proc_write_log_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 value);

static enum halmac_ret_status
proc_write_log_efuse_word_88xx(struct halmac_adapter *adapter, u32 offset,
			       u16 value);

static enum halmac_ret_status
update_eeprom_mask_88xx(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 *updated_mask);

static enum halmac_ret_status
check_efuse_enough_88xx(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 *updated_mask);

static enum halmac_ret_status
pg_extend_efuse_88xx(struct halmac_adapter *adapter,
		     struct halmac_pg_efuse_info *info, u8 word_en,
		     u8 pre_word_en, u32 eeprom_offset);

static enum halmac_ret_status
proc_pg_efuse_88xx(struct halmac_adapter *adapter,
		   struct halmac_pg_efuse_info *info, u8 word_en,
		   u8 pre_word_en, u32 eeprom_offset);

static enum halmac_ret_status
pg_super_usb_efuse_88xx(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 word_en,
			u8 pre_word_en, u32 eeprom_offset);

static enum halmac_ret_status
program_efuse_88xx(struct halmac_adapter *adapter,
		   struct halmac_pg_efuse_info *info, u8 *updated_mask);

static void
mask_eeprom_88xx(struct halmac_adapter *adapter,
		 struct halmac_pg_efuse_info *info);

static enum halmac_ret_status
proc_gen_super_usb_map_88xx(struct halmac_adapter *adapter, u8 *drv_map,
			    u8 *updated_map, u8 *updated_mask);

static enum halmac_ret_status
super_usb_efuse_parser_88xx(struct halmac_adapter *adapter, u8 *phy_map,
			    u8 *log_map, u8 *log_mask);

static enum halmac_ret_status
super_usb_chk_88xx(struct halmac_adapter *adapter, u8 *super_usb);

static enum halmac_ret_status
log_efuse_re_pg_chk_88xx(struct halmac_adapter *adapter, u8 *efuse_mask,
			 u32 addr, u8 *re_pg);

static enum halmac_ret_status
super_usb_fmt_chk_88xx(struct halmac_adapter *adapter, u8 *re_pg);

static enum halmac_ret_status
super_usb_re_pg_chk_88xx(struct halmac_adapter *adapter, u8 *phy_map,
			 u8 *re_pg);

/**
 * dump_efuse_map_88xx() - dump "physical" efuse map
 * @adapter : the adapter of halmac
 * @cfg : dump efuse method
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
dump_efuse_map_88xx(struct halmac_adapter *adapter,
		    enum halmac_efuse_read_cfg cfg)
{
	u8 *map = NULL;
	u8 *efuse_map;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;
	u32 prtct_efuse_size = adapter->hw_cfg_info.prtct_efuse_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	if (cfg == HALMAC_EFUSE_R_FW &&
	    halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]cfg = %d\n", cfg);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF)
		PLTFM_MSG_ERR("[ERR]Dump efuse in suspend\n");

	*proc_status = HALMAC_CMD_PROCESS_IDLE;
	adapter->evnt.phy_efuse_map = 1;

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}

	status = proc_dump_efuse_88xx(adapter, cfg);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump efuse!!\n");
		return status;
	}

	if (adapter->efuse_map_valid == 1) {
		*proc_status = HALMAC_CMD_PROCESS_DONE;
		efuse_map = adapter->efuse_map;

		map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc!!\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, efuse_size);
		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
#if HALMAC_PLATFORM_WINDOWS
		PLTFM_MEMCPY(map, efuse_map, efuse_size);
#else
		PLTFM_MEMCPY(map, efuse_map, efuse_size - prtct_efuse_size);
		PLTFM_MEMCPY(map + efuse_size - prtct_efuse_size +
			     RSVD_CS_EFUSE_SIZE,
			     efuse_map + efuse_size - prtct_efuse_size +
			     RSVD_CS_EFUSE_SIZE,
			     prtct_efuse_size - RSVD_EFUSE_SIZE -
			     RSVD_CS_EFUSE_SIZE);
#endif
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_EVENT_SIG(HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE,
				*proc_status, map, efuse_size);
		adapter->evnt.phy_efuse_map = 0;

		PLTFM_FREE(map, efuse_size);
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * dump_efuse_map_bt_88xx() - dump "BT physical" efuse map
 * @adapter : the adapter of halmac
 * @bank : bt efuse bank
 * @size : bt efuse map size. get from halmac_get_efuse_size API
 * @map : bt efuse map
 * Author : Soar / Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
dump_efuse_map_bt_88xx(struct halmac_adapter *adapter,
		       enum halmac_efuse_bank bank, u32 size, u8 *map)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (adapter->hw_cfg_info.bt_efuse_size != size)
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;

	if (bank >= HALMAC_EFUSE_BANK_MAX || bank == HALMAC_EFUSE_BANK_WIFI) {
		PLTFM_MSG_ERR("[ERR]Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = switch_efuse_bank_88xx(adapter, bank);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}

	status = read_hw_efuse_88xx(adapter, 0, size, map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read hw efuse\n");
		return status;
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * write_efuse_bt_88xx() - write "BT physical" efuse offset
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : Write value
 * @map : bt efuse map
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
write_efuse_bt_88xx(struct halmac_adapter *adapter, u32 offset, u8 value,
		    enum halmac_efuse_bank bank)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (offset >= adapter->hw_cfg_info.efuse_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (bank > HALMAC_EFUSE_BANK_MAX || bank == HALMAC_EFUSE_BANK_WIFI) {
		PLTFM_MSG_ERR("[ERR]Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	status = switch_efuse_bank_88xx(adapter, bank);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank!!\n");
		return status;
	}

	status = write_hw_efuse_88xx(adapter, offset, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write efuse\n");
		return status;
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * read_efuse_bt_88xx() - read "BT physical" efuse offset
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : 1 byte efuse value
 * @bank : efuse bank
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
read_efuse_bt_88xx(struct halmac_adapter *adapter, u32 offset, u8 *value,
		   enum halmac_efuse_bank bank)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (offset >= adapter->hw_cfg_info.efuse_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (bank > HALMAC_EFUSE_BANK_MAX || bank == HALMAC_EFUSE_BANK_WIFI) {
		PLTFM_MSG_ERR("[ERR]Undefined BT bank\n");
		return HALMAC_RET_EFUSE_BANK_INCORRECT;
	}

	status = switch_efuse_bank_88xx(adapter, bank);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = read_efuse_88xx(adapter, offset, 1, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read efuse\n");
		return status;
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_efuse_auto_check_88xx() - check efuse after writing it
 * @adapter : the adapter of halmac
 * @enable : 1, enable efuse auto check. others, disable
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_efuse_auto_check_88xx(struct halmac_adapter *adapter, u8 enable)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	adapter->efuse_auto_check_en = enable;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * get_efuse_available_size_88xx() - get efuse available size
 * @adapter : the adapter of halmac
 * @size : physical efuse available size
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_efuse_available_size_88xx(struct halmac_adapter *adapter, u32 *size)
{
	enum halmac_ret_status status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	status = dump_log_efuse_map_88xx(adapter, HALMAC_EFUSE_R_DRV);

	if (status != HALMAC_RET_SUCCESS)
		return status;

	*size = adapter->hw_cfg_info.efuse_size -
		adapter->hw_cfg_info.prtct_efuse_size -	adapter->efuse_end;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * get_efuse_size_88xx() - get "physical" efuse size
 * @adapter : the adapter of halmac
 * @size : physical efuse size
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_efuse_size_88xx(struct halmac_adapter *adapter, u32 *size)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	*size = adapter->hw_cfg_info.efuse_size;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * get_log_efuse_size_88xx() - get "logical" efuse size
 * @adapter : the adapter of halmac
 * @size : logical efuse size
 * Author : Ivan Lin/KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_log_efuse_size_88xx(struct halmac_adapter *adapter, u32 *size)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	*size = adapter->hw_cfg_info.eeprom_size;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * dump_log_efuse_map_88xx() - dump "logical" efuse map
 * @adapter : the adapter of halmac
 * @cfg : dump efuse method
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
dump_log_efuse_map_88xx(struct halmac_adapter *adapter,
			enum halmac_efuse_read_cfg cfg)
{
	u8 *map = NULL;
	u32 size = adapter->hw_cfg_info.eeprom_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	if (cfg == HALMAC_EFUSE_R_FW &&
	    halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]cfg = %d\n", cfg);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF)
		PLTFM_MSG_ERR("[ERR]Dump efuse in suspend\n");

	*proc_status = HALMAC_CMD_PROCESS_IDLE;
	adapter->evnt.log_efuse_map = 1;

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_dump_efuse_88xx(adapter, cfg);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump efuse\n");
		return status;
	}

	if (adapter->efuse_map_valid == 1) {
		*proc_status = HALMAC_CMD_PROCESS_DONE;

		map = (u8 *)PLTFM_MALLOC(size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, size);

		if (eeprom_parser_88xx(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLTFM_EVENT_SIG(HALMAC_FEATURE_DUMP_LOGICAL_EFUSE,
				*proc_status, map, size);
		adapter->evnt.log_efuse_map = 0;

		PLTFM_FREE(map, size);
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
dump_log_efuse_mask_88xx(struct halmac_adapter *adapter,
			 enum halmac_efuse_read_cfg cfg)
{
	u8 *map = NULL;
	u32 size = adapter->hw_cfg_info.eeprom_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	if (cfg == HALMAC_EFUSE_R_FW &&
	    halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]cfg = %d\n", cfg);

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF)
		PLTFM_MSG_ERR("[ERR]Dump efuse in suspend\n");

	*proc_status = HALMAC_CMD_PROCESS_IDLE;
	adapter->evnt.log_efuse_mask = 1;

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_dump_efuse_88xx(adapter, cfg);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump efuse\n");
		return status;
	}

	if (adapter->efuse_map_valid == 1) {
		*proc_status = HALMAC_CMD_PROCESS_DONE;

		map = (u8 *)PLTFM_MALLOC(size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, size);

		if (eeprom_mask_parser_88xx(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLTFM_EVENT_SIG(HALMAC_FEATURE_DUMP_LOGICAL_EFUSE_MASK,
				*proc_status, map, size);
		adapter->evnt.log_efuse_mask = 0;

		PLTFM_FREE(map, size);
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * read_logical_efuse_88xx() - read logical efuse map 1 byte
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : 1 byte efuse value
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
read_logical_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 *value)
{
	u8 *map = NULL;
	u32 size = adapter->hw_cfg_info.eeprom_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}
	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	map = (u8 *)PLTFM_MALLOC(size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, size);

	status = read_log_efuse_map_88xx(adapter, map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read logical efuse\n");
		PLTFM_FREE(map, size);
		return status;
	}

	*value = *(map + offset);

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, size);
		return HALMAC_RET_ERROR_STATE;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	PLTFM_FREE(map, size);

	return HALMAC_RET_SUCCESS;
}

/**
 * write_log_efuse_88xx() - write "logical" efuse offset
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : value
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
write_log_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= adapter->hw_cfg_info.eeprom_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_write_log_efuse_88xx(adapter, offset, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write logical efuse\n");
		return status;
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * write_log_efuse_word_88xx() - write "logical" efuse offset word 
 * @adapter : the adapter of halmac
 * @offset : offset
 * @value : value
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
write_log_efuse_word_88xx(struct halmac_adapter *adapter, u32 offset, u16 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= adapter->hw_cfg_info.eeprom_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_write_log_efuse_word_88xx(adapter, offset, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write logical efuse\n");
		return status;
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * pg_efuse_by_map_88xx() - pg logical efuse by map
 * @adapter : the adapter of halmac
 * @info : efuse map information
 * @cfg : dump efuse method
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pg_efuse_by_map_88xx(struct halmac_adapter *adapter,
		     struct halmac_pg_efuse_info *info,
		     enum halmac_efuse_read_cfg cfg)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (info->efuse_map_size != adapter->hw_cfg_info.eeprom_size) {
		PLTFM_MSG_ERR("[ERR]map size error\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if ((info->efuse_map_size & 0xF) > 0) {
		PLTFM_MSG_ERR("[ERR]not multiple of 16\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (info->efuse_mask_size != info->efuse_map_size >> 4) {
		PLTFM_MSG_ERR("[ERR]mask size error\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (!info->efuse_map) {
		PLTFM_MSG_ERR("[ERR]map is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (!info->efuse_mask) {
		PLTFM_MSG_ERR("[ERR]mask is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = proc_pg_efuse_by_map_88xx(adapter, info, cfg);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]pg efuse\n");
		return status;
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * mask_log_efuse_88xx() - mask logical efuse
 * @adapter : the adapter of halmac
 * @info : efuse map information
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
mask_log_efuse_88xx(struct halmac_adapter *adapter,
		    struct halmac_pg_efuse_info *info)
{
	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (info->efuse_map_size != adapter->hw_cfg_info.eeprom_size) {
		PLTFM_MSG_ERR("[ERR]map size error\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if ((info->efuse_map_size & 0xF) > 0) {
		PLTFM_MSG_ERR("[ERR]not multiple of 16\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (info->efuse_mask_size != info->efuse_map_size >> 4) {
		PLTFM_MSG_ERR("[ERR]mask size error\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (!info->efuse_map) {
		PLTFM_MSG_ERR("[ERR]map is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (!info->efuse_mask) {
		PLTFM_MSG_ERR("[ERR]mask is NULL\n");
		return HALMAC_RET_NULL_POINTER;
	}

	mask_eeprom_88xx(adapter, info);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_cmd_construct_state
efuse_cmd_cnstr_state_88xx(struct halmac_adapter *adapter)
{
	return adapter->halmac_state.efuse_state.cmd_cnstr_state;
}

enum halmac_ret_status
switch_efuse_bank_88xx(struct halmac_adapter *adapter,
		       enum halmac_efuse_bank bank)
{
	u8 reg_value;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	reg_value = HALMAC_REG_R8(REG_LDO_EFUSE_CTRL + 1);

	if (bank == (reg_value & (BIT(0) | BIT(1))))
		return HALMAC_RET_SUCCESS;

	reg_value &= ~(BIT(0) | BIT(1));
	reg_value |= bank;
	HALMAC_REG_W8(REG_LDO_EFUSE_CTRL + 1, reg_value);

	reg_value = HALMAC_REG_R8(REG_LDO_EFUSE_CTRL + 1);
	if ((reg_value & (BIT(0) | BIT(1))) != bank)
		return HALMAC_RET_SWITCH_EFUSE_BANK_FAIL;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_dump_efuse_88xx(struct halmac_adapter *adapter,
		     enum halmac_efuse_read_cfg cfg)
{
	u32 h2c_init;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_H2C_SENT) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	if (cfg == HALMAC_EFUSE_R_AUTO) {
		h2c_init = HALMAC_REG_R32(REG_H2C_PKT_READADDR);
		if (adapter->halmac_state.dlfw_state == HALMAC_DLFW_NONE ||
		    h2c_init == 0)
			status = dump_efuse_drv_88xx(adapter);
		else
			status = dump_efuse_fw_88xx(adapter);
	} else if (cfg == HALMAC_EFUSE_R_FW) {
		status = dump_efuse_fw_88xx(adapter);
	} else {
		status = dump_efuse_drv_88xx(adapter);
	}

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump efsue drv/fw\n");
		return status;
	}

	return status;
}

enum halmac_ret_status
cnv_efuse_state_88xx(struct halmac_adapter *adapter,
		     enum halmac_cmd_construct_state dest_state)
{
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	if (state->cmd_cnstr_state != HALMAC_CMD_CNSTR_IDLE &&
	    state->cmd_cnstr_state != HALMAC_CMD_CNSTR_BUSY &&
	    state->cmd_cnstr_state != HALMAC_CMD_CNSTR_H2C_SENT)
		return HALMAC_RET_ERROR_STATE;

	if (state->cmd_cnstr_state == dest_state)
		return HALMAC_RET_ERROR_STATE;

	if (dest_state == HALMAC_CMD_CNSTR_BUSY) {
		if (state->cmd_cnstr_state == HALMAC_CMD_CNSTR_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CMD_CNSTR_H2C_SENT) {
		if (state->cmd_cnstr_state == HALMAC_CMD_CNSTR_IDLE)
			return HALMAC_RET_ERROR_STATE;
	}

	state->cmd_cnstr_state = dest_state;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
read_hw_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u32 size,
		   u8 *map)
{
	u8 enable;
	u32 value32;
	u32 addr;
	u32 tmp32;
	u32 cnt;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	/* Read efuse no need 2.5V LDO */
	enable = 0;
	status = api->halmac_set_hw_value(adapter, HALMAC_HW_LDO25_EN, &enable);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dis ldo25\n");
		return status;
	}
	value32 = HALMAC_REG_R32(REG_EFUSE_CTRL);

	for (addr = offset; addr < offset + size; addr++) {
		value32 &= ~(BIT_MASK_EF_DATA | BITS_EF_ADDR);
		value32 |= ((addr & BIT_MASK_EF_ADDR) << BIT_SHIFT_EF_ADDR);
		HALMAC_REG_W32(REG_EFUSE_CTRL, value32 & (~BIT_EF_FLAG));

		cnt = 1000000;
		do {
			PLTFM_DELAY_US(1);
			tmp32 = HALMAC_REG_R32(REG_EFUSE_CTRL);
			cnt--;
			if (cnt == 0) {
				PLTFM_MSG_ERR("[ERR]read\n");
				return HALMAC_RET_EFUSE_R_FAIL;
			}
		} while ((tmp32 & BIT_EF_FLAG) == 0);

		*(map + addr - offset) = (u8)(tmp32 & BIT_MASK_EF_DATA);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
write_hw_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	const u8 unlock_code = 0x69;
	u8 value_read = 0;
	u8 enable;
	u32 value32;
	u32 tmp32;
	u32 cnt;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
	adapter->efuse_map_valid = 0;
	PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

	HALMAC_REG_W8(REG_PMC_DBG_CTRL2 + 3, unlock_code);

	/* Enable 2.5V LDO */
	enable = 1;
	status = api->halmac_set_hw_value(adapter, HALMAC_HW_LDO25_EN, &enable);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]en ldo25\n");
		return status;
	}

	value32 = HALMAC_REG_R32(REG_EFUSE_CTRL);
	value32 &= ~(BIT_MASK_EF_DATA | BITS_EF_ADDR);
	value32 = value32 | ((offset & BIT_MASK_EF_ADDR) << BIT_SHIFT_EF_ADDR) |
			(value & BIT_MASK_EF_DATA);
	HALMAC_REG_W32(REG_EFUSE_CTRL, value32 | BIT_EF_FLAG);

	cnt = 1000000;
	do {
		PLTFM_DELAY_US(1);
		tmp32 = HALMAC_REG_R32(REG_EFUSE_CTRL);
		cnt--;
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]write!!\n");
			return HALMAC_RET_EFUSE_W_FAIL;
		}
	} while (BIT_EF_FLAG == (tmp32 & BIT_EF_FLAG));

	HALMAC_REG_W8(REG_PMC_DBG_CTRL2 + 3, 0x00);

	/* Disable 2.5V LDO */
	enable = 0;
	status = api->halmac_set_hw_value(adapter, HALMAC_HW_LDO25_EN, &enable);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dis ldo25\n");
		return status;
	}

	if (adapter->efuse_auto_check_en == 1) {
		if (read_hw_efuse_88xx(adapter, offset, 1, &value_read) !=
		    HALMAC_RET_SUCCESS)
			return HALMAC_RET_EFUSE_R_FAIL;
		if (value_read != value) {
			PLTFM_MSG_ERR("[ERR]efuse compare\n");
			return HALMAC_RET_EFUSE_W_FAIL;
		}
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
eeprom_parser_88xx(struct halmac_adapter *adapter, u8 *phy_map, u8 *log_map)
{
	u8 i;
	u8 value8;
	u8 blk_idx;
	u8 word_en;
	u8 valid;
	u8 hdr;
	u8 hdr2 = 0;
	u32 eeprom_idx;
	u32 efuse_idx = 0;
	u32 prtct_efuse_size = adapter->hw_cfg_info.prtct_efuse_size;
	struct halmac_hw_cfg_info *hw_info = &adapter->hw_cfg_info;

	PLTFM_MEMSET(log_map, 0xFF, hw_info->eeprom_size);

	do {
		value8 = *(phy_map + efuse_idx);
		hdr = value8;

		if ((hdr & 0x1f) == 0x0f) {
			efuse_idx++;
			value8 = *(phy_map + efuse_idx);
			hdr2 = value8;
			if (hdr2 == 0xff)
				break;
			blk_idx = ((hdr2 & 0xF0) >> 1) | ((hdr >> 5) & 0x07);
			word_en = hdr2 & 0x0F;
		} else {
			blk_idx = (hdr & 0xF0) >> 4;
			word_en = hdr & 0x0F;
		}

		if (hdr == 0xff)
			break;

		efuse_idx++;

		if (efuse_idx >= hw_info->efuse_size - prtct_efuse_size - 1)
			return HALMAC_RET_EEPROM_PARSING_FAIL;

		for (i = 0; i < 4; i++) {
			valid = (u8)((~(word_en >> i)) & BIT(0));
			if (valid == 1) {
				eeprom_idx = (blk_idx << 3) + (i << 1);

				if ((eeprom_idx + 1) > hw_info->eeprom_size) {
					PLTFM_MSG_ERR("[ERR]efuse idx:0x%X\n",
						      efuse_idx - 1);

					PLTFM_MSG_ERR("[ERR]read hdr:0x%X\n",
						      hdr);

					PLTFM_MSG_ERR("[ERR]rad hdr2:0x%X\n",
						      hdr2);

					return HALMAC_RET_EEPROM_PARSING_FAIL;
				}

				value8 = *(phy_map + efuse_idx);
				*(log_map + eeprom_idx) = value8;

				eeprom_idx++;
				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size -
				    prtct_efuse_size - 1)
					return HALMAC_RET_EEPROM_PARSING_FAIL;

				value8 = *(phy_map + efuse_idx);
				*(log_map + eeprom_idx) = value8;

				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size -
				    prtct_efuse_size)
					return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
		}
	} while (1);

	adapter->efuse_end = efuse_idx;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
eeprom_mask_parser_88xx(struct halmac_adapter *adapter, u8 *phy_map,
			u8 *log_mask)
{
	u8 i;
	u8 value8;
	u8 blk_idx;
	u8 word_en;
	u8 valid;
	u8 hdr;
	u8 hdr2 = 0;
	u32 eeprom_idx;
	u32 efuse_idx = 0;
	u32 prtct_efuse_size = adapter->hw_cfg_info.prtct_efuse_size;
	struct halmac_hw_cfg_info *hw_info = &adapter->hw_cfg_info;

	PLTFM_MEMSET(log_mask, 0xFF, hw_info->eeprom_size);

	do {
		value8 = *(phy_map + efuse_idx);
		hdr = value8;

		if ((hdr & 0x1f) == 0x0f) {
			efuse_idx++;
			value8 = *(phy_map + efuse_idx);
			hdr2 = value8;
			if (hdr2 == 0xff)
				break;
			blk_idx = ((hdr2 & 0xF0) >> 1) | ((hdr >> 5) & 0x07);
			word_en = hdr2 & 0x0F;
		} else {
			blk_idx = (hdr & 0xF0) >> 4;
			word_en = hdr & 0x0F;
		}

		if (hdr == 0xff)
			break;

		efuse_idx++;

		if (efuse_idx >= hw_info->efuse_size - prtct_efuse_size - 1)
			return HALMAC_RET_EEPROM_PARSING_FAIL;

		for (i = 0; i < 4; i++) {
			valid = (u8)((~(word_en >> i)) & BIT(0));
			if (valid == 1) {
				eeprom_idx = (blk_idx << 3) + (i << 1);

				if ((eeprom_idx + 1) > hw_info->eeprom_size) {
					PLTFM_MSG_ERR("[ERR]efuse idx:0x%X\n",
						      efuse_idx - 1);

					PLTFM_MSG_ERR("[ERR]read hdr:0x%X\n",
						      hdr);

					PLTFM_MSG_ERR("[ERR]read hdr2:0x%X\n",
						      hdr2);

					return HALMAC_RET_EEPROM_PARSING_FAIL;
				}

				*(log_mask + eeprom_idx) = 0x00;

				eeprom_idx++;
				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size -
				    prtct_efuse_size - 1)
					return HALMAC_RET_EEPROM_PARSING_FAIL;

				*(log_mask + eeprom_idx) = 0x00;

				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size -
				    prtct_efuse_size)
					return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
		}
	} while (1);

	adapter->efuse_end = efuse_idx;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
read_log_efuse_map_88xx(struct halmac_adapter *adapter, u8 *map)
{
	u8 *local_map = NULL;
	u32 efuse_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->efuse_map_valid == 0) {
		efuse_size = adapter->hw_cfg_info.efuse_size;

		local_map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!local_map) {
			PLTFM_MSG_ERR("[ERR]local map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}

		status = read_efuse_88xx(adapter, 0, efuse_size, local_map);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]read efuse\n");
			PLTFM_FREE(local_map, efuse_size);
			return status;
		}

		if (!adapter->efuse_map) {
			adapter->efuse_map = (u8 *)PLTFM_MALLOC(efuse_size);
			if (!adapter->efuse_map) {
				PLTFM_MSG_ERR("[ERR]malloc adapter map\n");
				PLTFM_FREE(local_map, efuse_size);
				return HALMAC_RET_MALLOC_FAIL;
			}
		}

		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
		PLTFM_MEMCPY(adapter->efuse_map, local_map, efuse_size);
		adapter->efuse_map_valid = 1;
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_FREE(local_map, efuse_size);
	}

	if (eeprom_parser_88xx(adapter, adapter->efuse_map, map) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_EEPROM_PARSING_FAIL;

	return status;
}

static enum halmac_ret_status
proc_pg_efuse_by_map_88xx(struct halmac_adapter *adapter,
			  struct halmac_pg_efuse_info *info,
			  enum halmac_efuse_read_cfg cfg)
{
	u8 *updated_mask = NULL;
	u8 *updated_map = NULL;
	u32 map_size = adapter->hw_cfg_info.eeprom_size;
	u32 mask_size = adapter->hw_cfg_info.eeprom_size >> 4;
	u8 super_usb;
	struct halmac_pg_efuse_info local_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = super_usb_chk_88xx(adapter, &super_usb);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]super_usb_chk\n");
		return status;
	}

	updated_mask = (u8 *)PLTFM_MALLOC(mask_size);
	if (!updated_mask) {
		PLTFM_MSG_ERR("[ERR]malloc updated mask\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(updated_mask, 0x00, mask_size);

	status = update_eeprom_mask_88xx(adapter, info, updated_mask);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]update eeprom mask\n");
		PLTFM_FREE(updated_mask, mask_size);
		return status;
	}

	if (super_usb) {
		updated_map = (u8 *)PLTFM_MALLOC(map_size);
		if (!updated_map) {
			PLTFM_MSG_ERR("[ERR]malloc updated map\n");
			PLTFM_FREE(updated_mask, mask_size);
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(updated_map, 0xFF, map_size);

		status = proc_gen_super_usb_map_88xx(adapter, info->efuse_map,
						     updated_map, updated_mask);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]gen eeprom mask/map\n");
			PLTFM_FREE(updated_mask, mask_size);
			PLTFM_FREE(updated_map, map_size);
			return status;
		}

		local_info.efuse_map = updated_map;
		local_info.efuse_mask = updated_mask;
		local_info.efuse_map_size = map_size;
		local_info.efuse_mask_size = mask_size;
	}

	if (super_usb)
		status = check_efuse_enough_88xx(adapter, &local_info,
						 updated_mask);
	else
	status = check_efuse_enough_88xx(adapter, info, updated_mask);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]chk efuse enough\n");
		PLTFM_FREE(updated_mask, mask_size);
		if (super_usb)
			PLTFM_FREE(updated_map, map_size);
		return status;
	}

	if (super_usb)
		status = program_efuse_88xx(adapter, &local_info, updated_mask);
	else
	status = program_efuse_88xx(adapter, info, updated_mask);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]pg efuse\n");
		PLTFM_FREE(updated_mask, mask_size);
		if (super_usb)
			PLTFM_FREE(updated_map, map_size);
		return status;
	}

	PLTFM_FREE(updated_mask, mask_size);
	if (super_usb)
		PLTFM_FREE(updated_map, map_size);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
dump_efuse_drv_88xx(struct halmac_adapter *adapter)
{
	u8 *map = NULL;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;

	if (!adapter->efuse_map) {
		adapter->efuse_map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!adapter->efuse_map) {
			PLTFM_MSG_ERR("[ERR]malloc adapter map!!\n");
			reset_ofld_feature_88xx(adapter,
						FEATURE_DUMP_PHY_EFUSE);
			return HALMAC_RET_MALLOC_FAIL;
		}
	}

	if (adapter->efuse_map_valid == 0) {
		map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}

		if (read_hw_efuse_88xx(adapter, 0, efuse_size, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, efuse_size);
			return HALMAC_RET_EFUSE_R_FAIL;
		}

		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
		PLTFM_MEMCPY(adapter->efuse_map, map, efuse_size);
		adapter->efuse_map_valid = 1;
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_FREE(map, efuse_size);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
dump_efuse_fw_88xx(struct halmac_adapter *adapter)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num = 0;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	hdr_info.sub_cmd_id = SUB_CMD_ID_DUMP_PHYSICAL_EFUSE;
	hdr_info.content_size = 0;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	adapter->halmac_state.efuse_state.seq_num = seq_num;

	if (!adapter->efuse_map) {
		adapter->efuse_map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!adapter->efuse_map) {
			PLTFM_MSG_ERR("[ERR]malloc adapter map\n");
			reset_ofld_feature_88xx(adapter,
						FEATURE_DUMP_PHY_EFUSE);
			return HALMAC_RET_MALLOC_FAIL;
		}
	}

	if (adapter->efuse_map_valid == 0) {
		status = send_h2c_pkt_88xx(adapter, h2c_buf);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]send h2c pkt\n");
			reset_ofld_feature_88xx(adapter,
						FEATURE_DUMP_PHY_EFUSE);
			return status;
		}
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_write_log_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	u8 byte1;
	u8 byte2;
	u8 blk;
	u8 blk_idx;
	u8 hdr;
	u8 hdr2;
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	u32 prtct_efuse_size = adapter->hw_cfg_info.prtct_efuse_size;
	u32 end;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, eeprom_size);

	status = read_log_efuse_map_88xx(adapter, map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read logical efuse\n");
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	if (*(map + offset) != value) {
		end = adapter->efuse_end;
		blk = (u8)(offset >> 3);
		blk_idx = (u8)((offset & (8 - 1)) >> 1);

		if (offset > 0x7f) {
			hdr = (((blk & 0x07) << 5) & 0xE0) | 0x0F;
			hdr2 = (u8)(((blk & 0x78) << 1) +
						((0x1 << blk_idx) ^ 0x0F));
		} else {
			hdr = (u8)((blk << 4) + ((0x01 << blk_idx) ^ 0x0F));
		}

		if ((offset & 1) == 0) {
			byte1 = value;
			byte2 = *(map + offset + 1);
		} else {
			byte1 = *(map + offset - 1);
			byte2 = value;
		}

		if (offset > 0x7f) {
			if (adapter->hw_cfg_info.efuse_size <=
			    4 + prtct_efuse_size + end) {
				PLTFM_FREE(map, eeprom_size);
				return HALMAC_RET_EFUSE_NOT_ENOUGH;
			}

			status = write_hw_efuse_88xx(adapter, end, hdr);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return status;
			}

			status = write_hw_efuse_88xx(adapter, end + 1, hdr2);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return status;
			}

			status = write_hw_efuse_88xx(adapter, end + 2, byte1);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return status;
			}

			status = write_hw_efuse_88xx(adapter, end + 3, byte2);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return status;
			}
		} else {
			if (adapter->hw_cfg_info.efuse_size <=
			    3 + prtct_efuse_size + end) {
				PLTFM_FREE(map, eeprom_size);
				return HALMAC_RET_EFUSE_NOT_ENOUGH;
			}

			status = write_hw_efuse_88xx(adapter, end, hdr);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return status;
			}

			status = write_hw_efuse_88xx(adapter, end + 1, byte1);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return status;
			}

			status = write_hw_efuse_88xx(adapter, end + 2, byte2);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return status;
			}
		}
	}

	PLTFM_FREE(map, eeprom_size);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_write_log_efuse_word_88xx(struct halmac_adapter *adapter, u32 offset,
			       u16 value)
{
	u8 byte1;
	u8 byte2;
	u8 blk;
	u8 blk_idx;
	u8 hdr;
	u8 hdr2;
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	u32 prtct_efuse_size = adapter->hw_cfg_info.prtct_efuse_size;
	u32 end;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, eeprom_size);

	status = read_log_efuse_map_88xx(adapter, map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read logical efuse\n");
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	end = adapter->efuse_end;
	blk = (u8)(offset >> 3);
	blk_idx = (u8)((offset & (8 - 1)) >> 1);

	if (offset > 0x7f) {
		hdr = (((blk & 0x07) << 5) & 0xE0) | 0x0F;
		hdr2 = (u8)(((blk & 0x78) << 1) +
					((0x1 << blk_idx) ^ 0x0F));
	} else {
		hdr = (u8)((blk << 4) + ((0x01 << blk_idx) ^ 0x0F));
	}

	if ((offset & 1) == 0) {
		byte1 = (u8)(value & 0xFF);
		byte2 = (u8)((value >> 8) & 0xFF);
	} else {
		PLTFM_FREE(map, eeprom_size);
		return HALMAC_RET_ADR_NOT_ALIGN;
	}

	if (offset > 0x7f) {
		if (adapter->hw_cfg_info.efuse_size <=
		    4 + prtct_efuse_size + end) {
			PLTFM_FREE(map, eeprom_size);
			return HALMAC_RET_EFUSE_NOT_ENOUGH;
		}

		status = write_hw_efuse_88xx(adapter, end, hdr);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}

		status = write_hw_efuse_88xx(adapter, end + 1, hdr2);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}

		status = write_hw_efuse_88xx(adapter, end + 2, byte1);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}

		status = write_hw_efuse_88xx(adapter, end + 3, byte2);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}
	} else {
		if (adapter->hw_cfg_info.efuse_size <=
		    3 + prtct_efuse_size + end) {
			PLTFM_FREE(map, eeprom_size);
			return HALMAC_RET_EFUSE_NOT_ENOUGH;
		}

		status = write_hw_efuse_88xx(adapter, end, hdr);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}

		status = write_hw_efuse_88xx(adapter, end + 1, byte1);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}

		status = write_hw_efuse_88xx(adapter, end + 2, byte2);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return status;
		}
	}

	PLTFM_FREE(map, eeprom_size);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
read_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u32 size, u8 *map)
{
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (adapter->efuse_map_valid == 1) {
		PLTFM_MEMCPY(map, adapter->efuse_map + offset, size);
	} else {
		if (read_hw_efuse_88xx(adapter, offset, size, map) !=
		    HALMAC_RET_SUCCESS)
			return HALMAC_RET_EFUSE_R_FAIL;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
update_eeprom_mask_88xx(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 *updated_mask)
{
	u8 *map = NULL;
	u8 clr_bit = 0;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	u8 *map_pg;
	u8 *efuse_mask;
	u16 i;
	u16 j;
	u16 map_offset;
	u16 mask_offset;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, eeprom_size);

	PLTFM_MEMSET(updated_mask, 0x00, info->efuse_mask_size);

	status = read_log_efuse_map_88xx(adapter, map);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, eeprom_size);
		return status;
	}

	map_pg = info->efuse_map;
	efuse_mask = info->efuse_mask;

	for (i = 0; i < info->efuse_mask_size; i++)
		*(updated_mask + i) = *(efuse_mask + i);

	for (i = 0; i < info->efuse_map_size; i += 16) {
		for (j = 0; j < 16; j += 2) {
			map_offset = i + j;
			mask_offset = i >> 4;
			if (*(u16 *)(map_pg + map_offset) ==
			    *(u16 *)(map + map_offset)) {
				switch (j) {
				case 0:
					clr_bit = BIT(4);
					break;
				case 2:
					clr_bit = BIT(5);
					break;
				case 4:
					clr_bit = BIT(6);
					break;
				case 6:
					clr_bit = BIT(7);
					break;
				case 8:
					clr_bit = BIT(0);
					break;
				case 10:
					clr_bit = BIT(1);
					break;
				case 12:
					clr_bit = BIT(2);
					break;
				case 14:
					clr_bit = BIT(3);
					break;
				default:
					break;
				}
				*(updated_mask + mask_offset) &= ~clr_bit;
			}
		}
	}

	PLTFM_FREE(map, eeprom_size);

	return status;
}

static enum halmac_ret_status
check_efuse_enough_88xx(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 *updated_mask)
{
	u8 pre_word_en;
	u16 i;
	u16 j;
	u32 eeprom_offset;
	u32 pg_num = 0;
	u8 super_usb;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = super_usb_chk_88xx(adapter, &super_usb);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]super_usb_chk\n");
		return status;
	}

	for (i = 0; i < info->efuse_map_size; i = i + 8) {
		eeprom_offset = i;

		if ((eeprom_offset & 7) > 0)
			pre_word_en = (*(updated_mask + (i >> 4)) & 0x0F);
		else
			pre_word_en = (*(updated_mask + (i >> 4)) >> 4);

		if (pre_word_en > 0) {
			if (super_usb &&
			    ((eeprom_offset >= SUPER_USB_ZONE0_START &&
			    eeprom_offset <= SUPER_USB_ZONE0_END) ||
			    (eeprom_offset >= SUPER_USB_ZONE1_START &&
			    eeprom_offset <= SUPER_USB_ZONE1_END))) {
				for (j = 0; j < 4; j++) {
					if (((pre_word_en >> j) & 0x1) > 0)
						pg_num += 4;
				}
			} else {
				if (eeprom_offset > 0x7f)
					pg_num += 2;
				else
					pg_num++;

				for (j = 0; j < 4; j++) {
					if (((pre_word_en >> j) & 0x1) > 0)
						pg_num += 2;
				}
			}
		}
	}

	if (adapter->hw_cfg_info.efuse_size <=
	    (pg_num + adapter->hw_cfg_info.prtct_efuse_size +
	    adapter->efuse_end))
		return HALMAC_RET_EFUSE_NOT_ENOUGH;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
pg_extend_efuse_88xx(struct halmac_adapter *adapter,
		     struct halmac_pg_efuse_info *info, u8 word_en,
		     u8 pre_word_en, u32 eeprom_offset)
{
	u8 blk;
	u8 hdr;
	u8 hdr2;
	u16 i;
	u32 efuse_end;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	efuse_end = adapter->efuse_end;

	blk = (u8)(eeprom_offset >> 3);
	hdr = (((blk & 0x07) << 5) & 0xE0) | 0x0F;
	hdr2 = (u8)(((blk & 0x78) << 1) + word_en);

	status = write_hw_efuse_88xx(adapter, efuse_end, hdr);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write efuse\n");
		return status;
	}

	status = write_hw_efuse_88xx(adapter, efuse_end + 1, hdr2);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write efuse(+1)\n");
		return status;
	}

	efuse_end = efuse_end + 2;
	for (i = 0; i < 4; i++) {
		if (((pre_word_en >> i) & 0x1) > 0) {
			status = write_hw_efuse_88xx(adapter, efuse_end,
						     *(info->efuse_map +
						     eeprom_offset +
						     (i << 1)));
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(<<1)\n");
				return status;
			}

			status = write_hw_efuse_88xx(adapter, efuse_end + 1,
						     *(info->efuse_map +
						     eeprom_offset + (i << 1)
						     + 1));
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(<<1)+1\n");
				return status;
			}
			efuse_end = efuse_end + 2;
		}
	}
	adapter->efuse_end = efuse_end;
	return status;
}

static enum halmac_ret_status
proc_pg_efuse_88xx(struct halmac_adapter *adapter,
		   struct halmac_pg_efuse_info *info, u8 word_en,
		   u8 pre_word_en, u32 eeprom_offset)
{
	u8 blk;
	u8 hdr;
	u16 i;
	u32 efuse_end;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	efuse_end = adapter->efuse_end;

	blk = (u8)(eeprom_offset >> 3);
	hdr = (u8)((blk << 4) + word_en);

	status = write_hw_efuse_88xx(adapter, efuse_end, hdr);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write efuse\n");
		return status;
	}
	efuse_end = efuse_end + 1;
	for (i = 0; i < 4; i++) {
		if (((pre_word_en >> i) & 0x1) > 0) {
			status = write_hw_efuse_88xx(adapter, efuse_end,
						     *(info->efuse_map +
						     eeprom_offset +
						     (i << 1)));
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(<<1)\n");
				return status;
			}
			status = write_hw_efuse_88xx(adapter, efuse_end + 1,
						     *(info->efuse_map +
						     eeprom_offset + (i << 1)
						     + 1));
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(<<1)+1\n");
				return status;
			}
			efuse_end = efuse_end + 2;
		}
	}
	adapter->efuse_end = efuse_end;
	return status;
}

static enum halmac_ret_status
pg_super_usb_efuse_88xx(struct halmac_adapter *adapter,
			struct halmac_pg_efuse_info *info, u8 word_en,
			u8 pre_word_en, u32 eeprom_offset)
{
	u8 blk;
	u8 hdr;
	u8 hdr2;
	u16 i;
	u32 efuse_end;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	efuse_end = adapter->efuse_end;

	blk = (u8)(eeprom_offset >> 3);
	hdr = (((blk & 0x07) << 5) & 0xE0) | 0x0F;

	for (i = 0; i < 4; i++) {		
		hdr = (((blk & 0x07) << 5) & 0xE0) | 0x0F;
		if (((pre_word_en >> i) & 0x1) > 0) {
			hdr2 = (u8)(((blk & 0x78) << 1) +
				((pre_word_en & BIT(i)) ^ 0x0F));

			status = write_hw_efuse_88xx(adapter, efuse_end, hdr);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse\n");
				return status;
			}
			
			status = write_hw_efuse_88xx(adapter, efuse_end + 1,
						     hdr2);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(+1)\n");
				return status;
			}

			efuse_end = efuse_end + 2;

			status = write_hw_efuse_88xx(adapter, efuse_end,
						     *(info->efuse_map +
						     eeprom_offset +
						     (i << 1)));
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(<<1)\n");
				return status;
			}

			status = write_hw_efuse_88xx(adapter, efuse_end + 1,
						     *(info->efuse_map +
						     eeprom_offset + (i << 1)
						     + 1));
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]write efuse(<<1)+1\n");
				return status;
			}
			efuse_end = efuse_end + 2;
		}
	}
	adapter->efuse_end = efuse_end;
	return status;
}

static enum halmac_ret_status
program_efuse_88xx(struct halmac_adapter *adapter,
		   struct halmac_pg_efuse_info *info, u8 *updated_mask)
{
	u8 pre_word_en;
	u8 word_en;
	u16 i;
	u32 eeprom_offset;
	u8 super_usb;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = super_usb_chk_88xx(adapter, &super_usb);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]super_usb_chk\n");
		return status;
	}

	for (i = 0; i < info->efuse_map_size; i = i + 8) {
		eeprom_offset = i;

		if (((eeprom_offset >> 3) & 1) > 0) {
			pre_word_en = (*(updated_mask + (i >> 4)) & 0x0F);
			word_en = pre_word_en ^ 0x0F;
		} else {
			pre_word_en = (*(updated_mask + (i >> 4)) >> 4);
			word_en = pre_word_en ^ 0x0F;
		}

		if (pre_word_en > 0) {
			if (super_usb &&
			    ((eeprom_offset >= SUPER_USB_ZONE0_START &&
			    eeprom_offset <= SUPER_USB_ZONE0_END) ||
			    (eeprom_offset >= SUPER_USB_ZONE1_START &&
			    eeprom_offset <= SUPER_USB_ZONE1_END))) {
				status = pg_super_usb_efuse_88xx(adapter, info,
								 word_en,
								 pre_word_en,
								 eeprom_offset);
				if (status != HALMAC_RET_SUCCESS) {
					PLTFM_MSG_ERR("[ERR]super usb efuse\n");
					return status;
				}
			} else if (eeprom_offset > 0x7f) {
				status = pg_extend_efuse_88xx(adapter, info,
							      word_en,
							      pre_word_en,
							      eeprom_offset);
				if (status != HALMAC_RET_SUCCESS) {
					PLTFM_MSG_ERR("[ERR]extend efuse\n");
					return status;
				}
			} else {
				status = proc_pg_efuse_88xx(adapter, info,
							    word_en,
							    pre_word_en,
							    eeprom_offset);
				if (status != HALMAC_RET_SUCCESS) {
					PLTFM_MSG_ERR("[ERR]extend efuse");
					return status;
				}
			}
		}
	}

	return status;
}

static void
mask_eeprom_88xx(struct halmac_adapter *adapter,
		 struct halmac_pg_efuse_info *info)
{
	u8 pre_word_en;
	u8 *updated_mask;
	u8 *efuse_map;
	u16 i;
	u16 j;
	u32 offset;

	updated_mask = info->efuse_mask;
	efuse_map = info->efuse_map;

	for (i = 0; i < info->efuse_map_size; i = i + 8) {
		offset = i;

		if (((offset >> 3) & 1) > 0)
			pre_word_en = (*(updated_mask + (i >> 4)) & 0x0F);
		else
			pre_word_en = (*(updated_mask + (i >> 4)) >> 4);

		for (j = 0; j < 4; j++) {
			if (((pre_word_en >> j) & 0x1) == 0) {
				*(efuse_map + offset + (j << 1)) = 0xFF;
				*(efuse_map + offset + (j << 1) + 1) = 0xFF;
			}
		}
	}
}

enum halmac_ret_status
get_efuse_data_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seg_id;
	u8 seg_size;
	u8 seq_num;
	u8 fw_rc;
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;
	enum halmac_cmd_process_status proc_status;

	seq_num = (u8)EFUSE_DATA_GET_H2C_SEQ(buf);
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

	seg_id = (u8)EFUSE_DATA_GET_SEGMENT_ID(buf);
	seg_size = (u8)EFUSE_DATA_GET_SEGMENT_SIZE(buf);
	if (seg_id == 0)
		adapter->efuse_seg_size = seg_size;

	map = (u8 *)PLTFM_MALLOC(eeprom_size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, eeprom_size);

	PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
	PLTFM_MEMCPY(adapter->efuse_map + seg_id * adapter->efuse_seg_size,
		     buf + C2H_DATA_OFFSET_88XX, seg_size);
	PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

	if (EFUSE_DATA_GET_END_SEGMENT(buf) == 0) {
		PLTFM_FREE(map, eeprom_size);
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = state->fw_rc;

	if ((enum halmac_h2c_return_code)fw_rc == HALMAC_H2C_RETURN_SUCCESS) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;

		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
		adapter->efuse_map_valid = 1;
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		if (adapter->evnt.phy_efuse_map == 1) {
			PLTFM_EVENT_SIG(FEATURE_DUMP_PHY_EFUSE,
					proc_status, adapter->efuse_map,
					adapter->hw_cfg_info.efuse_size);
			adapter->evnt.phy_efuse_map = 0;
		}

		if (adapter->evnt.log_efuse_map == 1) {
			if (eeprom_parser_88xx(adapter, adapter->efuse_map,
					       map) != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
			PLTFM_EVENT_SIG(FEATURE_DUMP_LOG_EFUSE, proc_status,
					map, eeprom_size);
			adapter->evnt.log_efuse_map = 0;
		}

		if (adapter->evnt.log_efuse_mask == 1) {
			if (eeprom_mask_parser_88xx(adapter, adapter->efuse_map,
						    map)
						    != HALMAC_RET_SUCCESS) {
				PLTFM_FREE(map, eeprom_size);
				return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
			PLTFM_EVENT_SIG(FEATURE_DUMP_LOG_EFUSE_MASK,
					proc_status, map, eeprom_size);
			adapter->evnt.log_efuse_mask = 0;
		}

	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;

		if (adapter->evnt.phy_efuse_map == 1) {
			PLTFM_EVENT_SIG(FEATURE_DUMP_PHY_EFUSE, proc_status,
					&state->fw_rc, 1);
			adapter->evnt.phy_efuse_map = 0;
		}

		if (adapter->evnt.log_efuse_map == 1) {
			PLTFM_EVENT_SIG(FEATURE_DUMP_LOG_EFUSE, proc_status,
					&state->fw_rc, 1);
			adapter->evnt.log_efuse_map = 0;
		}

		if (adapter->evnt.log_efuse_mask == 1) {
			PLTFM_EVENT_SIG(FEATURE_DUMP_LOG_EFUSE_MASK,
					proc_status, &state->fw_rc, 1);
			adapter->evnt.log_efuse_mask = 0;
		}
	}

	PLTFM_FREE(map, eeprom_size);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_dump_phy_efuse_status_88xx(struct halmac_adapter *adapter,
			       enum halmac_cmd_process_status *proc_status,
			       u8 *data, u32 *size)
{
	u8 *map = NULL;
	u32 efuse_size = adapter->hw_cfg_info.efuse_size;
	u32 prtct_efuse_size = adapter->hw_cfg_info.prtct_efuse_size;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	*proc_status = state->proc_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*proc_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < efuse_size) {
			*size = efuse_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = efuse_size;

		map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, efuse_size);
		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
#if HALMAC_PLATFORM_WINDOWS
		PLTFM_MEMCPY(map, adapter->efuse_map, efuse_size);
#else
		PLTFM_MEMCPY(map, adapter->efuse_map,
			     efuse_size - prtct_efuse_size);
		PLTFM_MEMCPY(map + efuse_size - prtct_efuse_size +
			     RSVD_CS_EFUSE_SIZE,
			     adapter->efuse_map + efuse_size -
			     prtct_efuse_size + RSVD_CS_EFUSE_SIZE,
			     prtct_efuse_size - RSVD_EFUSE_SIZE -
			     RSVD_CS_EFUSE_SIZE);
#endif
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_MEMCPY(data, map, *size);

		PLTFM_FREE(map, efuse_size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_dump_log_efuse_status_88xx(struct halmac_adapter *adapter,
			       enum halmac_cmd_process_status *proc_status,
			       u8 *data, u32 *size)
{
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	*proc_status = state->proc_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*proc_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < eeprom_size) {
			*size = eeprom_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = eeprom_size;

		map = (u8 *)PLTFM_MALLOC(eeprom_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, eeprom_size);

		if (eeprom_parser_88xx(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLTFM_MEMCPY(data, map, *size);

		PLTFM_FREE(map, eeprom_size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_dump_log_efuse_mask_status_88xx(struct halmac_adapter *adapter,
				    enum halmac_cmd_process_status *proc_status,
				    u8 *data, u32 *size)
{
	u8 *map = NULL;
	u32 eeprom_size = adapter->hw_cfg_info.eeprom_size;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

	*proc_status = state->proc_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*proc_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < eeprom_size) {
			*size = eeprom_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = eeprom_size;

		map = (u8 *)PLTFM_MALLOC(eeprom_size);
		if (!map) {
			PLTFM_MSG_ERR("[ERR]malloc map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
		PLTFM_MEMSET(map, 0xFF, eeprom_size);

		if (eeprom_mask_parser_88xx(adapter, adapter->efuse_map, map) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_FREE(map, eeprom_size);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		PLTFM_MEMCPY(data, map, *size);

		PLTFM_FREE(map, eeprom_size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_h2c_ack_phy_efuse_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num = 0;
	u8 fw_rc;
	struct halmac_efuse_state *state = &adapter->halmac_state.efuse_state;

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

	return HALMAC_RET_SUCCESS;
}

u32
get_rsvd_efuse_size_88xx(struct halmac_adapter *adapter)
{
	return adapter->hw_cfg_info.prtct_efuse_size;
}

/**
 * write_wifi_phy_efuse_88xx() - write wifi physical efuse
 * @adapter : the adapter of halmac
 * @offset : the efuse offset to be written
 * @value : the value to be written
 * Author : Yong-Ching Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
write_wifi_phy_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= adapter->hw_cfg_info.efuse_size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = write_hw_efuse_88xx(adapter, offset, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]write physical efuse\n");
		return status;
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * read_wifi_phy_efuse_88xx() - read wifi physical efuse
 * @adapter : the adapter of halmac
 * @offset : the efuse offset to be read
 * @size : the length to be read
 * @value : pointer to the pre-allocated space where
 the efuse content is to be copied
 * Author : Yong-Ching Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
read_wifi_phy_efuse_88xx(struct halmac_adapter *adapter, u32 offset, u32 size,
			 u8 *value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *proc_status;

	proc_status = &adapter->halmac_state.efuse_state.proc_status;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (offset >= adapter->hw_cfg_info.efuse_size ||
	    offset + size >= adapter->hw_cfg_info.efuse_size) {
		PLTFM_MSG_ERR("[ERR] Wrong efuse index\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_WARN("[WARN]Wait event(efuse)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (efuse_cmd_cnstr_state_88xx(adapter) != HALMAC_CMD_CNSTR_IDLE) {
		PLTFM_MSG_WARN("[WARN]Not idle(efuse)\n");
		return HALMAC_RET_ERROR_STATE;
	}

	status = switch_efuse_bank_88xx(adapter, HALMAC_EFUSE_BANK_WIFI);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]switch efuse bank\n");
		return status;
	}

	status = read_hw_efuse_88xx(adapter, offset, size, value);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]read hw efuse\n");
		return status;
	}

	if (cnv_efuse_state_88xx(adapter, HALMAC_CMD_CNSTR_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
proc_gen_super_usb_map_88xx(struct halmac_adapter *adapter, u8 *drv_map,
			    u8 *updated_map, u8 *updated_mask)
{
	u8 *local_map = NULL;
	u8 *super_usb_map = NULL;
	u8 *super_usb_mask = NULL;
	u8 mask_val_0;
	u8 mask_val_1;
	u32 efuse_size;
	u32 i;
	u32 j;
	u32 val32;
	u32 map_size = adapter->hw_cfg_info.eeprom_size;
	u32 mask_size = adapter->hw_cfg_info.eeprom_size >> 4;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->efuse_map_valid == 0) {
		efuse_size = adapter->hw_cfg_info.efuse_size;

		local_map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!local_map) {
			PLTFM_MSG_ERR("[ERR]local map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}

		status = read_efuse_88xx(adapter, 0, efuse_size, local_map);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]read efuse\n");
			PLTFM_FREE(local_map, efuse_size);
			return status;
		}

		if (!adapter->efuse_map) {
			adapter->efuse_map = (u8 *)PLTFM_MALLOC(efuse_size);
			if (!adapter->efuse_map) {
				PLTFM_MSG_ERR("[ERR]malloc adapter map\n");
				PLTFM_FREE(local_map, efuse_size);
				return HALMAC_RET_MALLOC_FAIL;
			}
		}

		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
		PLTFM_MEMCPY(adapter->efuse_map, local_map, efuse_size);
		adapter->efuse_map_valid = 1;
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_FREE(local_map, efuse_size);
	}

	super_usb_mask = (u8 *)PLTFM_MALLOC(mask_size);
	if (!super_usb_mask) {
		PLTFM_MSG_ERR("[ERR]malloc updated mask\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(super_usb_mask, 0x00, mask_size);

	super_usb_map = (u8 *)PLTFM_MALLOC(map_size);
	if (!super_usb_map) {
		PLTFM_MSG_ERR("[ERR]malloc updated map\n");
		PLTFM_FREE(super_usb_mask, mask_size);
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(super_usb_map, 0xFF, map_size);

	status = super_usb_efuse_parser_88xx(adapter, adapter->efuse_map,
					     super_usb_map, super_usb_mask);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(super_usb_mask, mask_size);
		PLTFM_FREE(super_usb_map, map_size);
		return HALMAC_RET_EEPROM_PARSING_FAIL;
	}

	for (i = 0; i < map_size; i = i + 16) {
		mask_val_0 = *(updated_mask + (i >> 4));
		mask_val_1 = *(super_usb_mask + (i >> 4));
		if (mask_val_0 || mask_val_1) {
			for (j = 0; j < 4; j++) {
				val32 = i + (j << 1);
				if (mask_val_0 & BIT(j + 4)) {
					*(updated_map + val32) =
						*(drv_map + val32);
					*(updated_map + val32 + 1) =
						*(drv_map + val32 + 1);
				} else if (mask_val_1 & BIT(j + 4)) {
					*(updated_map + val32) =
						*(super_usb_map + val32);
					*(updated_map + val32 + 1) =
						*(super_usb_map + val32 + 1);
				}
			}
			for (j = 0; j < 4; j++) {
				val32 = i + (j << 1);
				if (mask_val_0 & BIT(j)) {
					*(updated_map + val32 + 8) =
						*(drv_map + val32 + 8);
					*(updated_map + val32 + 9) =
						*(drv_map + val32 + 9);
				} else if (mask_val_1 & BIT(j)) {
					*(updated_map + val32 + 8) =
						*(super_usb_map + val32 + 8);
					*(updated_map + val32 + 9) =
						*(super_usb_map + val32 + 9);
				}
			}
			*(updated_mask + (i >> 4)) |= mask_val_1;
		}
	}

	PLTFM_FREE(super_usb_mask, mask_size);
	PLTFM_FREE(super_usb_map, map_size);

	return status;
}

static enum halmac_ret_status
super_usb_efuse_parser_88xx(struct halmac_adapter *adapter, u8 *phy_map,
			    u8 *log_map, u8 *log_mask)
{
	u8 i;
	u8 value8;
	u8 blk_idx;
	u8 word_en;
	u8 valid;
	u8 hdr;
	u8 hdr2 = 0;
	u8 usb_addr;
	u32 eeprom_idx;
	u32 efuse_idx = 0;
	u32 start_offset;
	u32 prtct_efuse_size = adapter->hw_cfg_info.prtct_efuse_size;
	struct halmac_hw_cfg_info *hw_info = &adapter->hw_cfg_info;

	do {
		value8 = *(phy_map + efuse_idx);
		hdr = value8;

		if ((hdr & 0x1f) == 0x0f) {
			efuse_idx++;
			value8 = *(phy_map + efuse_idx);
			hdr2 = value8;
			if (hdr2 == 0xff)
				break;
			blk_idx = ((hdr2 & 0xF0) >> 1) | ((hdr >> 5) & 0x07);
			word_en = hdr2 & 0x0F;
		} else {
			blk_idx = (hdr & 0xF0) >> 4;
			word_en = hdr & 0x0F;
		}

		if (hdr == 0xff)
			break;

		efuse_idx++;

		if (efuse_idx >= hw_info->efuse_size - prtct_efuse_size - 1)
			return HALMAC_RET_EEPROM_PARSING_FAIL;

		for (i = 0; i < 4; i++) {
			valid = (u8)((~(word_en >> i)) & BIT(0));
			if (valid == 1) {
				eeprom_idx = (blk_idx << 3) + (i << 1);

				if ((eeprom_idx + 1) > hw_info->eeprom_size) {
					PLTFM_MSG_ERR("[ERR]efuse idx:0x%X\n",
						      efuse_idx - 1);

					PLTFM_MSG_ERR("[ERR]read hdr:0x%X\n",
						      hdr);

					PLTFM_MSG_ERR("[ERR]rad hdr2:0x%X\n",
						      hdr2);

					return HALMAC_RET_EEPROM_PARSING_FAIL;
				}

				value8 = *(phy_map + efuse_idx);
				*(log_map + eeprom_idx) = value8;

				eeprom_idx++;
				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size -
				    prtct_efuse_size - 1)
					return HALMAC_RET_EEPROM_PARSING_FAIL;

				value8 = *(phy_map + efuse_idx);
				*(log_map + eeprom_idx) = value8;

				efuse_idx++;

				if (efuse_idx > hw_info->efuse_size -
				    prtct_efuse_size)
					return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
		}

		start_offset = blk_idx << 3;
		if ((start_offset >= SUPER_USB_ZONE0_START &&
		     start_offset <= SUPER_USB_ZONE0_END) ||
		    (start_offset >= SUPER_USB_ZONE1_START &&
		     start_offset <= SUPER_USB_ZONE1_END))
			usb_addr = 1;
		else
			usb_addr = 0;
		if (usb_addr) {
			if (word_en != 0xE && word_en != 0xD &&
			    word_en != 0xB && word_en != 0x7) {
				if (blk_idx & 1)
					*(log_mask + (blk_idx >> 1)) |=
								~word_en & 0x0F;
				else
					*(log_mask + (blk_idx >> 1)) |=
							~(word_en << 4) & 0xF0;
			} else {
				if (blk_idx & 1)
					*(log_mask + (blk_idx >> 1)) &=
								word_en | 0xF0;
				else
					*(log_mask + (blk_idx >> 1)) &=
							(word_en << 4) | 0x0F;
			}
		}
	} while (1);

	adapter->efuse_end = efuse_idx;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
super_usb_chk_88xx(struct halmac_adapter *adapter, u8 *super_usb)
{
	u8 *local_map = NULL;
	u32 efuse_size;
	u8 re_pg;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->chip_id == HALMAC_CHIP_ID_8822C &&
	    (adapter->intf == HALMAC_INTERFACE_PCIE ||
	    adapter->intf == HALMAC_INTERFACE_USB)) {
		*super_usb = 1;
	} else {
		*super_usb = 0;
		return HALMAC_RET_SUCCESS;
	}

	if (adapter->efuse_map_valid == 0) {
		efuse_size = adapter->hw_cfg_info.efuse_size;

		local_map = (u8 *)PLTFM_MALLOC(efuse_size);
		if (!local_map) {
			PLTFM_MSG_ERR("[ERR]local map\n");
			return HALMAC_RET_MALLOC_FAIL;
		}

		status = read_efuse_88xx(adapter, 0, efuse_size, local_map);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]read efuse\n");
			PLTFM_FREE(local_map, efuse_size);
			return status;
		}

		if (!adapter->efuse_map) {
			adapter->efuse_map = (u8 *)PLTFM_MALLOC(efuse_size);
			if (!adapter->efuse_map) {
				PLTFM_MSG_ERR("[ERR]malloc adapter map\n");
				PLTFM_FREE(local_map, efuse_size);
				return HALMAC_RET_MALLOC_FAIL;
			}
		}

		PLTFM_MUTEX_LOCK(&adapter->efuse_mutex);
		PLTFM_MEMCPY(adapter->efuse_map, local_map, efuse_size);
		adapter->efuse_map_valid = 1;
		PLTFM_MUTEX_UNLOCK(&adapter->efuse_mutex);

		PLTFM_FREE(local_map, efuse_size);
	}

	status = super_usb_re_pg_chk_88xx(adapter, adapter->efuse_map, &re_pg);
	if (status != HALMAC_RET_SUCCESS)
		return status;
	if (re_pg) {
		status = super_usb_fmt_chk_88xx(adapter, &re_pg);
		if (status != HALMAC_RET_SUCCESS)
			return status;
		if (re_pg == 1) {
			*super_usb = 0;
			return HALMAC_RET_SUCCESS;
		}
	}

	return status;
}

static enum halmac_ret_status
log_efuse_re_pg_chk_88xx(struct halmac_adapter *adapter, u8 *efuse_mask,
			 u32 addr, u8 *re_pg)
{
	u32 size = adapter->hw_cfg_info.eeprom_size;
	u8 mask_val;
	u8 mask_offset;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (addr >= size) {
		PLTFM_MSG_ERR("[ERR]Offset is too large\n");
		return HALMAC_RET_EFUSE_SIZE_INCORRECT;
	}

	mask_val = *(efuse_mask + (addr >> 4));
	if (addr & 0x8)
		mask_offset = BIT((addr & 0x7) >> 1);
	else
		mask_offset = BIT((addr & 0x7) >> 1) << 4;

	if (mask_val & mask_offset)
		*re_pg = 1;
	else
		*re_pg = 0;

	return status;
}

static enum halmac_ret_status
super_usb_fmt_chk_88xx(struct halmac_adapter *adapter, u8 *re_pg)
{
	u32 map_size = adapter->hw_cfg_info.eeprom_size;
	u32 mask_size = adapter->hw_cfg_info.eeprom_size >> 4;
	u32 addr;
	u8 *super_usb_map = NULL;
	u8 *super_usb_mask = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	super_usb_mask = (u8 *)PLTFM_MALLOC(mask_size);
	if (!super_usb_mask) {
		PLTFM_MSG_ERR("[ERR]malloc updated mask\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(super_usb_mask, 0x00, mask_size);

	super_usb_map = (u8 *)PLTFM_MALLOC(map_size);
	if (!super_usb_map) {
		PLTFM_MSG_ERR("[ERR]malloc updated map\n");
		PLTFM_FREE(super_usb_mask, mask_size);
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(super_usb_map, 0xFF, map_size);

	status = super_usb_efuse_parser_88xx(adapter, adapter->efuse_map,
					     super_usb_map, super_usb_mask);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(super_usb_mask, mask_size);
		PLTFM_FREE(super_usb_map, map_size);
		return HALMAC_RET_EEPROM_PARSING_FAIL;
	}

	for (addr = SUPER_USB_ZONE0_START;
	     addr <= SUPER_USB_ZONE0_END; addr++) {
		status = log_efuse_re_pg_chk_88xx(adapter, super_usb_mask, addr,
						  re_pg);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(super_usb_mask, mask_size);
			PLTFM_FREE(super_usb_map, map_size);
			return status;
		}
		if (*re_pg == 1) {
			PLTFM_FREE(super_usb_mask, mask_size);
			PLTFM_FREE(super_usb_map, map_size);
			return status;
		}
	}

	for (addr = SUPER_USB_ZONE1_START;
	     addr <= SUPER_USB_ZONE1_END; addr++) {
		status = log_efuse_re_pg_chk_88xx(adapter, super_usb_mask, addr,
						  re_pg);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_FREE(super_usb_mask, mask_size);
			PLTFM_FREE(super_usb_map, map_size);
			return status;
		}
		if (*re_pg == 1) {
			PLTFM_FREE(super_usb_mask, mask_size);
			PLTFM_FREE(super_usb_map, map_size);
			return status;
		}
	}

	*re_pg = 0;

	PLTFM_FREE(super_usb_mask, mask_size);
	PLTFM_FREE(super_usb_map, map_size);

	return status;
}

static enum halmac_ret_status
super_usb_re_pg_chk_88xx(struct halmac_adapter *adapter, u8 *phy_map, u8 *re_pg)
{
	u32 size = adapter->hw_cfg_info.eeprom_size;
	u32 addr;
	u8 *map = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	map = (u8 *)PLTFM_MALLOC(size);
	if (!map) {
		PLTFM_MSG_ERR("[ERR]malloc map\n");
		return HALMAC_RET_MALLOC_FAIL;
	}
	PLTFM_MEMSET(map, 0xFF, size);

	status = eeprom_mask_parser_88xx(adapter, phy_map, map);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_FREE(map, size);
		return status;
	}

	for (addr = SUPER_USB_RE_PG_CK_ZONE0_START;
	     addr <= SUPER_USB_RE_PG_CK_ZONE0_END; addr++) {
		if (*(map + addr) != 0xFF) {
			PLTFM_FREE(map, size);
			*re_pg = 1;
			return status;
		}
	}

	*re_pg = 0;

	PLTFM_FREE(map, size);

	return status;
}
#endif /* HALMAC_88XX_SUPPORT */
