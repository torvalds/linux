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
#include "halmac_88xx_cfg.h"

static enum halmac_ret_status
halmac_dump_efuse_fw_88xx(struct halmac_adapter *halmac_adapter);

static enum halmac_ret_status
halmac_dump_efuse_drv_88xx(struct halmac_adapter *halmac_adapter);

static enum halmac_ret_status
halmac_update_eeprom_mask_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_pg_efuse_info *pg_efuse_info,
			       u8 *eeprom_mask_updated);

static enum halmac_ret_status
halmac_check_efuse_enough_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_pg_efuse_info *pg_efuse_info,
			       u8 *eeprom_mask_updated);

static enum halmac_ret_status
halmac_program_efuse_88xx(struct halmac_adapter *halmac_adapter,
			  struct halmac_pg_efuse_info *pg_efuse_info,
			  u8 *eeprom_mask_updated);

static enum halmac_ret_status
halmac_pwr_sub_seq_parer_88xx(struct halmac_adapter *halmac_adapter, u8 cut,
			      u8 fab, u8 intf,
			      struct halmac_wl_pwr_cfg_ *pwr_sub_seq_cfg);

static enum halmac_ret_status
halmac_parse_c2h_debug_88xx(struct halmac_adapter *halmac_adapter, u8 *c2h_buf,
			    u32 c2h_size);

static enum halmac_ret_status
halmac_parse_scan_status_rpt_88xx(struct halmac_adapter *halmac_adapter,
				  u8 *c2h_buf, u32 c2h_size);

static enum halmac_ret_status
halmac_parse_psd_data_88xx(struct halmac_adapter *halmac_adapter, u8 *c2h_buf,
			   u32 c2h_size);

static enum halmac_ret_status
halmac_parse_efuse_data_88xx(struct halmac_adapter *halmac_adapter, u8 *c2h_buf,
			     u32 c2h_size);

static enum halmac_ret_status
halmac_parse_h2c_ack_88xx(struct halmac_adapter *halmac_adapter, u8 *c2h_buf,
			  u32 c2h_size);

static enum halmac_ret_status
halmac_enqueue_para_buff_88xx(struct halmac_adapter *halmac_adapter,
			      struct halmac_phy_parameter_info *para_info,
			      u8 *curr_buff_wptr, bool *end_cmd);

static enum halmac_ret_status
halmac_parse_h2c_ack_phy_efuse_88xx(struct halmac_adapter *halmac_adapter,
				    u8 *c2h_buf, u32 c2h_size);

static enum halmac_ret_status
halmac_parse_h2c_ack_cfg_para_88xx(struct halmac_adapter *halmac_adapter,
				   u8 *c2h_buf, u32 c2h_size);

static enum halmac_ret_status
halmac_gen_cfg_para_h2c_88xx(struct halmac_adapter *halmac_adapter,
			     u8 *h2c_buff);

static enum halmac_ret_status
halmac_parse_h2c_ack_update_packet_88xx(struct halmac_adapter *halmac_adapter,
					u8 *c2h_buf, u32 c2h_size);

static enum halmac_ret_status
halmac_parse_h2c_ack_update_datapack_88xx(struct halmac_adapter *halmac_adapter,
					  u8 *c2h_buf, u32 c2h_size);

static enum halmac_ret_status
halmac_parse_h2c_ack_run_datapack_88xx(struct halmac_adapter *halmac_adapter,
				       u8 *c2h_buf, u32 c2h_size);

static enum halmac_ret_status
halmac_parse_h2c_ack_channel_switch_88xx(struct halmac_adapter *halmac_adapter,
					 u8 *c2h_buf, u32 c2h_size);

static enum halmac_ret_status
halmac_parse_h2c_ack_iqk_88xx(struct halmac_adapter *halmac_adapter,
			      u8 *c2h_buf, u32 c2h_size);

static enum halmac_ret_status
halmac_parse_h2c_ack_power_tracking_88xx(struct halmac_adapter *halmac_adapter,
					 u8 *c2h_buf, u32 c2h_size);

void halmac_init_offload_feature_state_machine_88xx(
	struct halmac_adapter *halmac_adapter)
{
	struct halmac_state *state = &halmac_adapter->halmac_state;

	state->efuse_state_set.efuse_cmd_construct_state =
		HALMAC_EFUSE_CMD_CONSTRUCT_IDLE;
	state->efuse_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	state->efuse_state_set.seq_num = halmac_adapter->h2c_packet_seq;

	state->cfg_para_state_set.cfg_para_cmd_construct_state =
		HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE;
	state->cfg_para_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	state->cfg_para_state_set.seq_num = halmac_adapter->h2c_packet_seq;

	state->scan_state_set.scan_cmd_construct_state =
		HALMAC_SCAN_CMD_CONSTRUCT_IDLE;
	state->scan_state_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	state->scan_state_set.seq_num = halmac_adapter->h2c_packet_seq;

	state->update_packet_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	state->update_packet_set.seq_num = halmac_adapter->h2c_packet_seq;

	state->iqk_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	state->iqk_set.seq_num = halmac_adapter->h2c_packet_seq;

	state->power_tracking_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	state->power_tracking_set.seq_num = halmac_adapter->h2c_packet_seq;

	state->psd_set.process_status = HALMAC_CMD_PROCESS_IDLE;
	state->psd_set.seq_num = halmac_adapter->h2c_packet_seq;
	state->psd_set.data_size = 0;
	state->psd_set.segment_size = 0;
	state->psd_set.data = NULL;
}

enum halmac_ret_status
halmac_dump_efuse_88xx(struct halmac_adapter *halmac_adapter,
		       enum halmac_efuse_read_cfg cfg)
{
	u32 chk_h2c_init;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api =
		(struct halmac_api *)halmac_adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.efuse_state_set.process_status;

	driver_adapter = halmac_adapter->driver_adapter;

	*process_status = HALMAC_CMD_PROCESS_SENDING;

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_H2C_SENT) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	if (cfg == HALMAC_EFUSE_R_AUTO) {
		chk_h2c_init = HALMAC_REG_READ_32(halmac_adapter,
						  REG_H2C_PKT_READADDR);
		if (halmac_adapter->halmac_state.dlfw_state ==
			    HALMAC_DLFW_NONE ||
		    chk_h2c_init == 0)
			status = halmac_dump_efuse_drv_88xx(halmac_adapter);
		else
			status = halmac_dump_efuse_fw_88xx(halmac_adapter);
	} else if (cfg == HALMAC_EFUSE_R_FW) {
		status = halmac_dump_efuse_fw_88xx(halmac_adapter);
	} else {
		status = halmac_dump_efuse_drv_88xx(halmac_adapter);
	}

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_read_efuse error = %x\n", status);
		return status;
	}

	return status;
}

enum halmac_ret_status
halmac_func_read_efuse_88xx(struct halmac_adapter *halmac_adapter, u32 offset,
			    u32 size, u8 *efuse_map)
{
	void *driver_adapter = NULL;

	driver_adapter = halmac_adapter->driver_adapter;

	if (!efuse_map) {
		pr_err("Malloc for dump efuse map error\n");
		return HALMAC_RET_NULL_POINTER;
	}

	if (halmac_adapter->hal_efuse_map_valid)
		memcpy(efuse_map, halmac_adapter->hal_efuse_map + offset, size);
	else if (halmac_read_hw_efuse_88xx(halmac_adapter, offset, size,
					   efuse_map) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_EFUSE_R_FAIL;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_read_hw_efuse_88xx(struct halmac_adapter *halmac_adapter, u32 offset,
			  u32 size, u8 *efuse_map)
{
	u8 value8;
	u32 value32;
	u32 address;
	u32 tmp32, counter;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	/* Read efuse no need 2.5V LDO */
	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_LDO_EFUSE_CTRL + 3);
	if (value8 & BIT(7))
		HALMAC_REG_WRITE_8(halmac_adapter, REG_LDO_EFUSE_CTRL + 3,
				   (u8)(value8 & ~(BIT(7))));

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_EFUSE_CTRL);

	for (address = offset; address < offset + size; address++) {
		value32 = value32 &
			  ~((BIT_MASK_EF_DATA) |
			    (BIT_MASK_EF_ADDR << BIT_SHIFT_EF_ADDR));
		value32 = value32 |
			  ((address & BIT_MASK_EF_ADDR) << BIT_SHIFT_EF_ADDR);
		HALMAC_REG_WRITE_32(halmac_adapter, REG_EFUSE_CTRL,
				    value32 & (~BIT_EF_FLAG));

		counter = 1000000;
		do {
			udelay(1);
			tmp32 = HALMAC_REG_READ_32(halmac_adapter,
						   REG_EFUSE_CTRL);
			counter--;
			if (counter == 0) {
				pr_err("HALMAC_RET_EFUSE_R_FAIL\n");
				return HALMAC_RET_EFUSE_R_FAIL;
			}
		} while ((tmp32 & BIT_EF_FLAG) == 0);

		*(efuse_map + address - offset) =
			(u8)(tmp32 & BIT_MASK_EF_DATA);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_dump_efuse_drv_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 *efuse_map = NULL;
	u32 efuse_size;
	void *driver_adapter = NULL;

	driver_adapter = halmac_adapter->driver_adapter;

	efuse_size = halmac_adapter->hw_config_info.efuse_size;

	if (!halmac_adapter->hal_efuse_map) {
		halmac_adapter->hal_efuse_map = kzalloc(efuse_size, GFP_KERNEL);
		if (!halmac_adapter->hal_efuse_map)
			return HALMAC_RET_MALLOC_FAIL;
	}

	efuse_map = kzalloc(efuse_size, GFP_KERNEL);
	if (!efuse_map)
		return HALMAC_RET_MALLOC_FAIL;

	if (halmac_read_hw_efuse_88xx(halmac_adapter, 0, efuse_size,
				      efuse_map) != HALMAC_RET_SUCCESS) {
		kfree(efuse_map);
		return HALMAC_RET_EFUSE_R_FAIL;
	}

	spin_lock(&halmac_adapter->efuse_lock);
	memcpy(halmac_adapter->hal_efuse_map, efuse_map, efuse_size);
	halmac_adapter->hal_efuse_map_valid = true;
	spin_unlock(&halmac_adapter->efuse_lock);

	kfree(efuse_map);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_dump_efuse_fw_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = NULL;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_DUMP_PHYSICAL_EFUSE;
	h2c_header_info.content_size = 0;
	h2c_header_info.ack = true;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);
	halmac_adapter->halmac_state.efuse_state_set.seq_num = h2c_seq_mum;

	if (!halmac_adapter->hal_efuse_map) {
		halmac_adapter->hal_efuse_map = kzalloc(
			halmac_adapter->hw_config_info.efuse_size, GFP_KERNEL);
		if (!halmac_adapter->hal_efuse_map)
			return HALMAC_RET_MALLOC_FAIL;
	}

	if (!halmac_adapter->hal_efuse_map_valid) {
		status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
						  HALMAC_H2C_CMD_SIZE_88XX,
						  true);
		if (status != HALMAC_RET_SUCCESS) {
			pr_err("halmac_read_efuse_fw Fail = %x!!\n", status);
			return status;
		}
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_func_write_efuse_88xx(struct halmac_adapter *halmac_adapter, u32 offset,
			     u8 value)
{
	const u8 wite_protect_code = 0x69;
	u32 value32, tmp32, counter;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	spin_lock(&halmac_adapter->efuse_lock);
	halmac_adapter->hal_efuse_map_valid = false;
	spin_unlock(&halmac_adapter->efuse_lock);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_PMC_DBG_CTRL2 + 3,
			   wite_protect_code);

	/* Enable 2.5V LDO */
	HALMAC_REG_WRITE_8(
		halmac_adapter, REG_LDO_EFUSE_CTRL + 3,
		(u8)(HALMAC_REG_READ_8(halmac_adapter, REG_LDO_EFUSE_CTRL + 3) |
		     BIT(7)));

	value32 = HALMAC_REG_READ_32(halmac_adapter, REG_EFUSE_CTRL);
	value32 =
		value32 &
		~((BIT_MASK_EF_DATA) | (BIT_MASK_EF_ADDR << BIT_SHIFT_EF_ADDR));
	value32 = value32 | ((offset & BIT_MASK_EF_ADDR) << BIT_SHIFT_EF_ADDR) |
		  (value & BIT_MASK_EF_DATA);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_EFUSE_CTRL,
			    value32 | BIT_EF_FLAG);

	counter = 1000000;
	do {
		udelay(1);
		tmp32 = HALMAC_REG_READ_32(halmac_adapter, REG_EFUSE_CTRL);
		counter--;
		if (counter == 0) {
			pr_err("halmac_write_efuse Fail !!\n");
			return HALMAC_RET_EFUSE_W_FAIL;
		}
	} while ((tmp32 & BIT_EF_FLAG) == BIT_EF_FLAG);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_PMC_DBG_CTRL2 + 3, 0x00);

	/* Disable 2.5V LDO */
	HALMAC_REG_WRITE_8(
		halmac_adapter, REG_LDO_EFUSE_CTRL + 3,
		(u8)(HALMAC_REG_READ_8(halmac_adapter, REG_LDO_EFUSE_CTRL + 3) &
		     ~(BIT(7))));

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_func_switch_efuse_bank_88xx(struct halmac_adapter *halmac_adapter,
				   enum halmac_efuse_bank efuse_bank)
{
	u8 reg_value;
	struct halmac_api *halmac_api;

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (halmac_transition_efuse_state_88xx(
		    halmac_adapter, HALMAC_EFUSE_CMD_CONSTRUCT_BUSY) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	reg_value = HALMAC_REG_READ_8(halmac_adapter, REG_LDO_EFUSE_CTRL + 1);

	if (efuse_bank == (reg_value & (BIT(0) | BIT(1))))
		return HALMAC_RET_SUCCESS;

	reg_value &= ~(BIT(0) | BIT(1));
	reg_value |= efuse_bank;
	HALMAC_REG_WRITE_8(halmac_adapter, REG_LDO_EFUSE_CTRL + 1, reg_value);

	if ((HALMAC_REG_READ_8(halmac_adapter, REG_LDO_EFUSE_CTRL + 1) &
	     (BIT(0) | BIT(1))) != efuse_bank)
		return HALMAC_RET_SWITCH_EFUSE_BANK_FAIL;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_eeprom_parser_88xx(struct halmac_adapter *halmac_adapter,
			  u8 *physical_efuse_map, u8 *logical_efuse_map)
{
	u8 j;
	u8 value8;
	u8 block_index;
	u8 valid_word_enable, word_enable;
	u8 efuse_read_header, efuse_read_header2 = 0;
	u32 eeprom_index;
	u32 efuse_index = 0;
	u32 eeprom_size = halmac_adapter->hw_config_info.eeprom_size;
	void *driver_adapter = NULL;

	driver_adapter = halmac_adapter->driver_adapter;

	memset(logical_efuse_map, 0xFF, eeprom_size);

	do {
		value8 = *(physical_efuse_map + efuse_index);
		efuse_read_header = value8;

		if ((efuse_read_header & 0x1f) == 0x0f) {
			efuse_index++;
			value8 = *(physical_efuse_map + efuse_index);
			efuse_read_header2 = value8;
			block_index = ((efuse_read_header2 & 0xF0) >> 1) |
				      ((efuse_read_header >> 5) & 0x07);
			word_enable = efuse_read_header2 & 0x0F;
		} else {
			block_index = (efuse_read_header & 0xF0) >> 4;
			word_enable = efuse_read_header & 0x0F;
		}

		if (efuse_read_header == 0xff)
			break;

		efuse_index++;

		if (efuse_index >= halmac_adapter->hw_config_info.efuse_size -
					   HALMAC_PROTECTED_EFUSE_SIZE_88XX - 1)
			return HALMAC_RET_EEPROM_PARSING_FAIL;

		for (j = 0; j < 4; j++) {
			valid_word_enable =
				(u8)((~(word_enable >> j)) & BIT(0));
			if (valid_word_enable != 1)
				continue;

			eeprom_index = (block_index << 3) + (j << 1);

			if ((eeprom_index + 1) > eeprom_size) {
				pr_err("Error: EEPROM addr exceeds eeprom_size:0x%X, at eFuse 0x%X\n",
				       eeprom_size, efuse_index - 1);
				if ((efuse_read_header & 0x1f) == 0x0f)
					pr_err("Error: EEPROM header: 0x%X, 0x%X,\n",
					       efuse_read_header,
					       efuse_read_header2);
				else
					pr_err("Error: EEPROM header: 0x%X,\n",
					       efuse_read_header);

				return HALMAC_RET_EEPROM_PARSING_FAIL;
			}

			value8 = *(physical_efuse_map + efuse_index);
			*(logical_efuse_map + eeprom_index) = value8;

			eeprom_index++;
			efuse_index++;

			if (efuse_index >
			    halmac_adapter->hw_config_info.efuse_size -
				    HALMAC_PROTECTED_EFUSE_SIZE_88XX - 1)
				return HALMAC_RET_EEPROM_PARSING_FAIL;

			value8 = *(physical_efuse_map + efuse_index);
			*(logical_efuse_map + eeprom_index) = value8;

			efuse_index++;

			if (efuse_index >
			    halmac_adapter->hw_config_info.efuse_size -
				    HALMAC_PROTECTED_EFUSE_SIZE_88XX)
				return HALMAC_RET_EEPROM_PARSING_FAIL;
		}
	} while (1);

	halmac_adapter->efuse_end = efuse_index;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_read_logical_efuse_map_88xx(struct halmac_adapter *halmac_adapter,
				   u8 *map)
{
	u8 *efuse_map = NULL;
	u32 efuse_size;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;
	efuse_size = halmac_adapter->hw_config_info.efuse_size;

	if (!halmac_adapter->hal_efuse_map_valid) {
		efuse_map = kzalloc(efuse_size, GFP_KERNEL);
		if (!efuse_map)
			return HALMAC_RET_MALLOC_FAIL;

		status = halmac_func_read_efuse_88xx(halmac_adapter, 0,
						     efuse_size, efuse_map);
		if (status != HALMAC_RET_SUCCESS) {
			pr_err("[ERR]halmac_read_efuse error = %x\n", status);
			kfree(efuse_map);
			return status;
		}

		if (!halmac_adapter->hal_efuse_map) {
			halmac_adapter->hal_efuse_map =
				kzalloc(efuse_size, GFP_KERNEL);
			if (!halmac_adapter->hal_efuse_map) {
				kfree(efuse_map);
				return HALMAC_RET_MALLOC_FAIL;
			}
		}

		spin_lock(&halmac_adapter->efuse_lock);
		memcpy(halmac_adapter->hal_efuse_map, efuse_map, efuse_size);
		halmac_adapter->hal_efuse_map_valid = true;
		spin_unlock(&halmac_adapter->efuse_lock);

		kfree(efuse_map);
	}

	if (halmac_eeprom_parser_88xx(halmac_adapter,
				      halmac_adapter->hal_efuse_map,
				      map) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_EEPROM_PARSING_FAIL;

	return status;
}

enum halmac_ret_status
halmac_func_write_logical_efuse_88xx(struct halmac_adapter *halmac_adapter,
				     u32 offset, u8 value)
{
	u8 pg_efuse_byte1, pg_efuse_byte2;
	u8 pg_block, pg_block_index;
	u8 pg_efuse_header, pg_efuse_header2;
	u8 *eeprom_map = NULL;
	u32 eeprom_size = halmac_adapter->hw_config_info.eeprom_size;
	u32 efuse_end, pg_efuse_num;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	eeprom_map = kzalloc(eeprom_size, GFP_KERNEL);
	if (!eeprom_map)
		return HALMAC_RET_MALLOC_FAIL;
	memset(eeprom_map, 0xFF, eeprom_size);

	status = halmac_read_logical_efuse_map_88xx(halmac_adapter, eeprom_map);
	if (status != HALMAC_RET_SUCCESS) {
		pr_err("[ERR]halmac_read_logical_efuse_map_88xx error = %x\n",
		       status);
		kfree(eeprom_map);
		return status;
	}

	if (*(eeprom_map + offset) != value) {
		efuse_end = halmac_adapter->efuse_end;
		pg_block = (u8)(offset >> 3);
		pg_block_index = (u8)((offset & (8 - 1)) >> 1);

		if (offset > 0x7f) {
			pg_efuse_header =
				(((pg_block & 0x07) << 5) & 0xE0) | 0x0F;
			pg_efuse_header2 =
				(u8)(((pg_block & 0x78) << 1) +
				     ((0x1 << pg_block_index) ^ 0x0F));
		} else {
			pg_efuse_header =
				(u8)((pg_block << 4) +
				     ((0x01 << pg_block_index) ^ 0x0F));
		}

		if ((offset & 1) == 0) {
			pg_efuse_byte1 = value;
			pg_efuse_byte2 = *(eeprom_map + offset + 1);
		} else {
			pg_efuse_byte1 = *(eeprom_map + offset - 1);
			pg_efuse_byte2 = value;
		}

		if (offset > 0x7f) {
			pg_efuse_num = 4;
			if (halmac_adapter->hw_config_info.efuse_size <=
			    (pg_efuse_num + HALMAC_PROTECTED_EFUSE_SIZE_88XX +
			     halmac_adapter->efuse_end)) {
				kfree(eeprom_map);
				return HALMAC_RET_EFUSE_NOT_ENOUGH;
			}
			halmac_func_write_efuse_88xx(halmac_adapter, efuse_end,
						     pg_efuse_header);
			halmac_func_write_efuse_88xx(halmac_adapter,
						     efuse_end + 1,
						     pg_efuse_header2);
			halmac_func_write_efuse_88xx(
				halmac_adapter, efuse_end + 2, pg_efuse_byte1);
			status = halmac_func_write_efuse_88xx(
				halmac_adapter, efuse_end + 3, pg_efuse_byte2);
		} else {
			pg_efuse_num = 3;
			if (halmac_adapter->hw_config_info.efuse_size <=
			    (pg_efuse_num + HALMAC_PROTECTED_EFUSE_SIZE_88XX +
			     halmac_adapter->efuse_end)) {
				kfree(eeprom_map);
				return HALMAC_RET_EFUSE_NOT_ENOUGH;
			}
			halmac_func_write_efuse_88xx(halmac_adapter, efuse_end,
						     pg_efuse_header);
			halmac_func_write_efuse_88xx(
				halmac_adapter, efuse_end + 1, pg_efuse_byte1);
			status = halmac_func_write_efuse_88xx(
				halmac_adapter, efuse_end + 2, pg_efuse_byte2);
		}

		if (status != HALMAC_RET_SUCCESS) {
			pr_err("[ERR]halmac_write_logical_efuse error = %x\n",
			       status);
			kfree(eeprom_map);
			return status;
		}
	}

	kfree(eeprom_map);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_func_pg_efuse_by_map_88xx(struct halmac_adapter *halmac_adapter,
				 struct halmac_pg_efuse_info *pg_efuse_info,
				 enum halmac_efuse_read_cfg cfg)
{
	u8 *eeprom_mask_updated = NULL;
	u32 eeprom_mask_size = halmac_adapter->hw_config_info.eeprom_size >> 4;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	eeprom_mask_updated = kzalloc(eeprom_mask_size, GFP_KERNEL);
	if (!eeprom_mask_updated)
		return HALMAC_RET_MALLOC_FAIL;

	status = halmac_update_eeprom_mask_88xx(halmac_adapter, pg_efuse_info,
						eeprom_mask_updated);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("[ERR]halmac_update_eeprom_mask_88xx error = %x\n",
		       status);
		kfree(eeprom_mask_updated);
		return status;
	}

	status = halmac_check_efuse_enough_88xx(halmac_adapter, pg_efuse_info,
						eeprom_mask_updated);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("[ERR]halmac_check_efuse_enough_88xx error = %x\n",
		       status);
		kfree(eeprom_mask_updated);
		return status;
	}

	status = halmac_program_efuse_88xx(halmac_adapter, pg_efuse_info,
					   eeprom_mask_updated);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("[ERR]halmac_program_efuse_88xx error = %x\n", status);
		kfree(eeprom_mask_updated);
		return status;
	}

	kfree(eeprom_mask_updated);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_update_eeprom_mask_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_pg_efuse_info *pg_efuse_info,
			       u8 *eeprom_mask_updated)
{
	u8 *eeprom_map = NULL;
	u32 eeprom_size = halmac_adapter->hw_config_info.eeprom_size;
	u8 *eeprom_map_pg, *eeprom_mask;
	u16 i, j;
	u16 map_byte_offset, mask_byte_offset;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	void *driver_adapter = NULL;

	driver_adapter = halmac_adapter->driver_adapter;

	eeprom_map = kzalloc(eeprom_size, GFP_KERNEL);
	if (!eeprom_map)
		return HALMAC_RET_MALLOC_FAIL;

	memset(eeprom_map, 0xFF, eeprom_size);
	memset(eeprom_mask_updated, 0x00, pg_efuse_info->efuse_mask_size);

	status = halmac_read_logical_efuse_map_88xx(halmac_adapter, eeprom_map);

	if (status != HALMAC_RET_SUCCESS) {
		kfree(eeprom_map);
		return status;
	}

	eeprom_map_pg = pg_efuse_info->efuse_map;
	eeprom_mask = pg_efuse_info->efuse_mask;

	for (i = 0; i < pg_efuse_info->efuse_mask_size; i++)
		*(eeprom_mask_updated + i) = *(eeprom_mask + i);

	for (i = 0; i < pg_efuse_info->efuse_map_size; i = i + 16) {
		for (j = 0; j < 16; j = j + 2) {
			map_byte_offset = i + j;
			mask_byte_offset = i >> 4;
			if (*(eeprom_map_pg + map_byte_offset) ==
			    *(eeprom_map + map_byte_offset)) {
				if (*(eeprom_map_pg + map_byte_offset + 1) ==
				    *(eeprom_map + map_byte_offset + 1)) {
					switch (j) {
					case 0:
						*(eeprom_mask_updated +
						  mask_byte_offset) =
							*(eeprom_mask_updated +
							  mask_byte_offset) &
							(BIT(4) ^ 0xFF);
						break;
					case 2:
						*(eeprom_mask_updated +
						  mask_byte_offset) =
							*(eeprom_mask_updated +
							  mask_byte_offset) &
							(BIT(5) ^ 0xFF);
						break;
					case 4:
						*(eeprom_mask_updated +
						  mask_byte_offset) =
							*(eeprom_mask_updated +
							  mask_byte_offset) &
							(BIT(6) ^ 0xFF);
						break;
					case 6:
						*(eeprom_mask_updated +
						  mask_byte_offset) =
							*(eeprom_mask_updated +
							  mask_byte_offset) &
							(BIT(7) ^ 0xFF);
						break;
					case 8:
						*(eeprom_mask_updated +
						  mask_byte_offset) =
							*(eeprom_mask_updated +
							  mask_byte_offset) &
							(BIT(0) ^ 0xFF);
						break;
					case 10:
						*(eeprom_mask_updated +
						  mask_byte_offset) =
							*(eeprom_mask_updated +
							  mask_byte_offset) &
							(BIT(1) ^ 0xFF);
						break;
					case 12:
						*(eeprom_mask_updated +
						  mask_byte_offset) =
							*(eeprom_mask_updated +
							  mask_byte_offset) &
							(BIT(2) ^ 0xFF);
						break;
					case 14:
						*(eeprom_mask_updated +
						  mask_byte_offset) =
							*(eeprom_mask_updated +
							  mask_byte_offset) &
							(BIT(3) ^ 0xFF);
						break;
					default:
						break;
					}
				}
			}
		}
	}

	kfree(eeprom_map);

	return status;
}

static enum halmac_ret_status
halmac_check_efuse_enough_88xx(struct halmac_adapter *halmac_adapter,
			       struct halmac_pg_efuse_info *pg_efuse_info,
			       u8 *eeprom_mask_updated)
{
	u8 pre_word_enb, word_enb;
	u8 pg_efuse_header, pg_efuse_header2;
	u8 pg_block;
	u16 i, j;
	u32 efuse_end;
	u32 tmp_eeprom_offset, pg_efuse_num = 0;

	efuse_end = halmac_adapter->efuse_end;

	for (i = 0; i < pg_efuse_info->efuse_map_size; i = i + 8) {
		tmp_eeprom_offset = i;

		if ((tmp_eeprom_offset & 7) > 0) {
			pre_word_enb =
				(*(eeprom_mask_updated + (i >> 4)) & 0x0F);
			word_enb = pre_word_enb ^ 0x0F;
		} else {
			pre_word_enb = (*(eeprom_mask_updated + (i >> 4)) >> 4);
			word_enb = pre_word_enb ^ 0x0F;
		}

		pg_block = (u8)(tmp_eeprom_offset >> 3);

		if (pre_word_enb > 0) {
			if (tmp_eeprom_offset > 0x7f) {
				pg_efuse_header =
					(((pg_block & 0x07) << 5) & 0xE0) |
					0x0F;
				pg_efuse_header2 = (u8)(
					((pg_block & 0x78) << 1) + word_enb);
			} else {
				pg_efuse_header =
					(u8)((pg_block << 4) + word_enb);
			}

			if (tmp_eeprom_offset > 0x7f) {
				pg_efuse_num++;
				pg_efuse_num++;
				efuse_end = efuse_end + 2;
				for (j = 0; j < 4; j++) {
					if (((pre_word_enb >> j) & 0x1) > 0) {
						pg_efuse_num++;
						pg_efuse_num++;
						efuse_end = efuse_end + 2;
					}
				}
			} else {
				pg_efuse_num++;
				efuse_end = efuse_end + 1;
				for (j = 0; j < 4; j++) {
					if (((pre_word_enb >> j) & 0x1) > 0) {
						pg_efuse_num++;
						pg_efuse_num++;
						efuse_end = efuse_end + 2;
					}
				}
			}
		}
	}

	if (halmac_adapter->hw_config_info.efuse_size <=
	    (pg_efuse_num + HALMAC_PROTECTED_EFUSE_SIZE_88XX +
	     halmac_adapter->efuse_end))
		return HALMAC_RET_EFUSE_NOT_ENOUGH;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_program_efuse_88xx(struct halmac_adapter *halmac_adapter,
			  struct halmac_pg_efuse_info *pg_efuse_info,
			  u8 *eeprom_mask_updated)
{
	u8 pre_word_enb, word_enb;
	u8 pg_efuse_header, pg_efuse_header2;
	u8 pg_block;
	u16 i, j;
	u32 efuse_end;
	u32 tmp_eeprom_offset;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	efuse_end = halmac_adapter->efuse_end;

	for (i = 0; i < pg_efuse_info->efuse_map_size; i = i + 8) {
		tmp_eeprom_offset = i;

		if (((tmp_eeprom_offset >> 3) & 1) > 0) {
			pre_word_enb =
				(*(eeprom_mask_updated + (i >> 4)) & 0x0F);
			word_enb = pre_word_enb ^ 0x0F;
		} else {
			pre_word_enb = (*(eeprom_mask_updated + (i >> 4)) >> 4);
			word_enb = pre_word_enb ^ 0x0F;
		}

		pg_block = (u8)(tmp_eeprom_offset >> 3);

		if (pre_word_enb <= 0)
			continue;

		if (tmp_eeprom_offset > 0x7f) {
			pg_efuse_header =
				(((pg_block & 0x07) << 5) & 0xE0) | 0x0F;
			pg_efuse_header2 =
				(u8)(((pg_block & 0x78) << 1) + word_enb);
		} else {
			pg_efuse_header = (u8)((pg_block << 4) + word_enb);
		}

		if (tmp_eeprom_offset > 0x7f) {
			halmac_func_write_efuse_88xx(halmac_adapter, efuse_end,
						     pg_efuse_header);
			status = halmac_func_write_efuse_88xx(halmac_adapter,
							      efuse_end + 1,
							      pg_efuse_header2);
			efuse_end = efuse_end + 2;
			for (j = 0; j < 4; j++) {
				if (((pre_word_enb >> j) & 0x1) > 0) {
					halmac_func_write_efuse_88xx(
						halmac_adapter, efuse_end,
						*(pg_efuse_info->efuse_map +
						  tmp_eeprom_offset +
						  (j << 1)));
					status = halmac_func_write_efuse_88xx(
						halmac_adapter, efuse_end + 1,
						*(pg_efuse_info->efuse_map +
						  tmp_eeprom_offset + (j << 1) +
						  1));
					efuse_end = efuse_end + 2;
				}
			}
		} else {
			status = halmac_func_write_efuse_88xx(
				halmac_adapter, efuse_end, pg_efuse_header);
			efuse_end = efuse_end + 1;
			for (j = 0; j < 4; j++) {
				if (((pre_word_enb >> j) & 0x1) > 0) {
					halmac_func_write_efuse_88xx(
						halmac_adapter, efuse_end,
						*(pg_efuse_info->efuse_map +
						  tmp_eeprom_offset +
						  (j << 1)));
					status = halmac_func_write_efuse_88xx(
						halmac_adapter, efuse_end + 1,
						*(pg_efuse_info->efuse_map +
						  tmp_eeprom_offset + (j << 1) +
						  1));
					efuse_end = efuse_end + 2;
				}
			}
		}
	}

	return status;
}

enum halmac_ret_status
halmac_dlfw_to_mem_88xx(struct halmac_adapter *halmac_adapter, u8 *ram_code,
			u32 dest, u32 code_size)
{
	u8 *code_ptr;
	u8 first_part;
	u32 mem_offset;
	u32 pkt_size_tmp, send_pkt_size;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	code_ptr = ram_code;
	mem_offset = 0;
	first_part = 1;
	pkt_size_tmp = code_size;

	HALMAC_REG_WRITE_32(
		halmac_adapter, REG_DDMA_CH0CTRL,
		HALMAC_REG_READ_32(halmac_adapter, REG_DDMA_CH0CTRL) |
			BIT_DDMACH0_RESET_CHKSUM_STS);

	while (pkt_size_tmp != 0) {
		if (pkt_size_tmp >= halmac_adapter->max_download_size)
			send_pkt_size = halmac_adapter->max_download_size;
		else
			send_pkt_size = pkt_size_tmp;

		if (halmac_send_fwpkt_88xx(
			    halmac_adapter, code_ptr + mem_offset,
			    send_pkt_size) != HALMAC_RET_SUCCESS) {
			pr_err("halmac_send_fwpkt_88xx fail!!\n");
			return HALMAC_RET_DLFW_FAIL;
		}

		if (halmac_iddma_dlfw_88xx(
			    halmac_adapter,
			    HALMAC_OCPBASE_TXBUF_88XX +
				    halmac_adapter->hw_config_info.txdesc_size,
			    dest + mem_offset, send_pkt_size,
			    first_part) != HALMAC_RET_SUCCESS) {
			pr_err("halmac_iddma_dlfw_88xx fail!!\n");
			return HALMAC_RET_DLFW_FAIL;
		}

		first_part = 0;
		mem_offset += send_pkt_size;
		pkt_size_tmp -= send_pkt_size;
	}

	if (halmac_check_fw_chksum_88xx(halmac_adapter, dest) !=
	    HALMAC_RET_SUCCESS) {
		pr_err("halmac_check_fw_chksum_88xx fail!!\n");
		return HALMAC_RET_DLFW_FAIL;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_send_fwpkt_88xx(struct halmac_adapter *halmac_adapter, u8 *ram_code,
		       u32 code_size)
{
	if (halmac_download_rsvd_page_88xx(halmac_adapter, ram_code,
					   code_size) != HALMAC_RET_SUCCESS) {
		pr_err("PLATFORM_SEND_RSVD_PAGE 0 error!!\n");
		return HALMAC_RET_DL_RSVD_PAGE_FAIL;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_iddma_dlfw_88xx(struct halmac_adapter *halmac_adapter, u32 source,
		       u32 dest, u32 length, u8 first)
{
	u32 counter;
	u32 ch0_control = (u32)(BIT_DDMACH0_CHKSUM_EN | BIT_DDMACH0_OWN);
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	counter = HALMC_DDMA_POLLING_COUNT;
	while (HALMAC_REG_READ_32(halmac_adapter, REG_DDMA_CH0CTRL) &
	       BIT_DDMACH0_OWN) {
		counter--;
		if (counter == 0) {
			pr_err("%s error-1!!\n", __func__);
			return HALMAC_RET_DDMA_FAIL;
		}
	}

	ch0_control |= (length & BIT_MASK_DDMACH0_DLEN);
	if (first == 0)
		ch0_control |= BIT_DDMACH0_CHKSUM_CONT;

	HALMAC_REG_WRITE_32(halmac_adapter, REG_DDMA_CH0SA, source);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_DDMA_CH0DA, dest);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_DDMA_CH0CTRL, ch0_control);

	counter = HALMC_DDMA_POLLING_COUNT;
	while (HALMAC_REG_READ_32(halmac_adapter, REG_DDMA_CH0CTRL) &
	       BIT_DDMACH0_OWN) {
		counter--;
		if (counter == 0) {
			pr_err("%s error-2!!\n", __func__);
			return HALMAC_RET_DDMA_FAIL;
		}
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_check_fw_chksum_88xx(struct halmac_adapter *halmac_adapter,
			    u32 memory_address)
{
	u8 mcu_fw_ctrl;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	mcu_fw_ctrl = HALMAC_REG_READ_8(halmac_adapter, REG_MCUFW_CTRL);

	if (HALMAC_REG_READ_32(halmac_adapter, REG_DDMA_CH0CTRL) &
	    BIT_DDMACH0_CHKSUM_STS) {
		if (memory_address < HALMAC_OCPBASE_DMEM_88XX) {
			mcu_fw_ctrl |= BIT_IMEM_DW_OK;
			HALMAC_REG_WRITE_8(
				halmac_adapter, REG_MCUFW_CTRL,
				(u8)(mcu_fw_ctrl & ~(BIT_IMEM_CHKSUM_OK)));
		} else {
			mcu_fw_ctrl |= BIT_DMEM_DW_OK;
			HALMAC_REG_WRITE_8(
				halmac_adapter, REG_MCUFW_CTRL,
				(u8)(mcu_fw_ctrl & ~(BIT_DMEM_CHKSUM_OK)));
		}

		pr_err("%s error!!\n", __func__);

		status = HALMAC_RET_FW_CHECKSUM_FAIL;
	} else {
		if (memory_address < HALMAC_OCPBASE_DMEM_88XX) {
			mcu_fw_ctrl |= BIT_IMEM_DW_OK;
			HALMAC_REG_WRITE_8(
				halmac_adapter, REG_MCUFW_CTRL,
				(u8)(mcu_fw_ctrl | BIT_IMEM_CHKSUM_OK));
		} else {
			mcu_fw_ctrl |= BIT_DMEM_DW_OK;
			HALMAC_REG_WRITE_8(
				halmac_adapter, REG_MCUFW_CTRL,
				(u8)(mcu_fw_ctrl | BIT_DMEM_CHKSUM_OK));
		}

		status = HALMAC_RET_SUCCESS;
	}

	return status;
}

enum halmac_ret_status
halmac_dlfw_end_flow_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 value8;
	u32 counter;
	void *driver_adapter = halmac_adapter->driver_adapter;
	struct halmac_api *halmac_api =
		(struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_32(halmac_adapter, REG_TXDMA_STATUS, BIT(2));

	/* Check IMEM & DMEM checksum is OK or not */
	if ((HALMAC_REG_READ_8(halmac_adapter, REG_MCUFW_CTRL) & 0x50) == 0x50)
		HALMAC_REG_WRITE_16(halmac_adapter, REG_MCUFW_CTRL,
				    (u16)(HALMAC_REG_READ_16(halmac_adapter,
							     REG_MCUFW_CTRL) |
					  BIT_FW_DW_RDY));
	else
		return HALMAC_RET_DLFW_FAIL;

	HALMAC_REG_WRITE_8(
		halmac_adapter, REG_MCUFW_CTRL,
		(u8)(HALMAC_REG_READ_8(halmac_adapter, REG_MCUFW_CTRL) &
		     ~(BIT(0))));

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_RSV_CTRL + 1);
	value8 = (u8)(value8 | BIT(0));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_RSV_CTRL + 1, value8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_SYS_FUNC_EN + 1);
	value8 = (u8)(value8 | BIT(2));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_SYS_FUNC_EN + 1,
			   value8); /* Release MCU reset */
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"Download Finish, Reset CPU\n");

	counter = 10000;
	while (HALMAC_REG_READ_16(halmac_adapter, REG_MCUFW_CTRL) != 0xC078) {
		if (counter == 0) {
			pr_err("Check 0x80 = 0xC078 fail\n");
			if ((HALMAC_REG_READ_32(halmac_adapter, REG_FW_DBG7) &
			     0xFFFFFF00) == 0xFAAAAA00)
				pr_err("Key fail\n");
			return HALMAC_RET_DLFW_FAIL;
		}
		counter--;
		usleep_range(50, 60);
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"Check 0x80 = 0xC078 counter = %d\n", counter);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_free_dl_fw_end_flow_88xx(struct halmac_adapter *halmac_adapter)
{
	u32 counter;
	struct halmac_api *halmac_api =
		(struct halmac_api *)halmac_adapter->halmac_api;

	counter = 100;
	while (HALMAC_REG_READ_8(halmac_adapter, REG_HMETFR + 3) != 0) {
		counter--;
		if (counter == 0) {
			pr_err("[ERR]0x1CF != 0\n");
			return HALMAC_RET_DLFW_FAIL;
		}
		usleep_range(50, 60);
	}

	HALMAC_REG_WRITE_8(halmac_adapter, REG_HMETFR + 3,
			   ID_INFORM_DLEMEM_RDY);

	counter = 10000;
	while (HALMAC_REG_READ_8(halmac_adapter, REG_C2HEVT_3 + 3) !=
	       ID_INFORM_DLEMEM_RDY) {
		counter--;
		if (counter == 0) {
			pr_err("[ERR]0x1AF != 0x80\n");
			return HALMAC_RET_DLFW_FAIL;
		}
		usleep_range(50, 60);
	}

	HALMAC_REG_WRITE_8(halmac_adapter, REG_C2HEVT_3 + 3, 0);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_pwr_seq_parser_88xx(struct halmac_adapter *halmac_adapter, u8 cut,
			   u8 fab, u8 intf,
			   struct halmac_wl_pwr_cfg_ **pp_pwr_seq_cfg)
{
	u32 seq_idx = 0;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_wl_pwr_cfg_ *seq_cmd;

	driver_adapter = halmac_adapter->driver_adapter;

	do {
		seq_cmd = pp_pwr_seq_cfg[seq_idx];

		if (!seq_cmd)
			break;

		status = halmac_pwr_sub_seq_parer_88xx(halmac_adapter, cut, fab,
						       intf, seq_cmd);
		if (status != HALMAC_RET_SUCCESS) {
			pr_err("[Err]pwr sub seq parser fail, status = 0x%X!\n",
			       status);
			return status;
		}

		seq_idx++;
	} while (1);

	return status;
}

static enum halmac_ret_status
halmac_pwr_sub_seq_parer_do_cmd_88xx(struct halmac_adapter *halmac_adapter,
				     struct halmac_wl_pwr_cfg_ *sub_seq_cmd,
				     bool *reti)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	u8 value, flag;
	u8 polling_bit;
	u32 polling_count;
	static u32 poll_to_static;
	u32 offset;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;
	*reti = true;

	switch (sub_seq_cmd->cmd) {
	case HALMAC_PWR_CMD_WRITE:
		if (sub_seq_cmd->base == HALMAC_PWR_BASEADDR_SDIO)
			offset = sub_seq_cmd->offset | SDIO_LOCAL_OFFSET;
		else
			offset = sub_seq_cmd->offset;

		value = HALMAC_REG_READ_8(halmac_adapter, offset);
		value = (u8)(value & (u8)(~(sub_seq_cmd->msk)));
		value = (u8)(value |
			     (u8)(sub_seq_cmd->value & sub_seq_cmd->msk));

		HALMAC_REG_WRITE_8(halmac_adapter, offset, value);
		break;
	case HALMAC_PWR_CMD_POLLING:
		polling_bit = 0;
		polling_count = HALMAC_POLLING_READY_TIMEOUT_COUNT;
		flag = 0;

		if (sub_seq_cmd->base == HALMAC_PWR_BASEADDR_SDIO)
			offset = sub_seq_cmd->offset | SDIO_LOCAL_OFFSET;
		else
			offset = sub_seq_cmd->offset;

		do {
			polling_count--;
			value = HALMAC_REG_READ_8(halmac_adapter, offset);
			value = (u8)(value & sub_seq_cmd->msk);

			if (value == (sub_seq_cmd->value & sub_seq_cmd->msk)) {
				polling_bit = 1;
				continue;
			}

			if (polling_count != 0) {
				usleep_range(50, 60);
				continue;
			}

			if (halmac_adapter->halmac_interface ==
				    HALMAC_INTERFACE_PCIE &&
			    flag == 0) {
				/* For PCIE + USB package poll power bit
				 * timeout issue
				 */
				poll_to_static++;
				HALMAC_RT_TRACE(
					driver_adapter, HALMAC_MSG_PWR,
					DBG_WARNING,
					"[WARN]PCIE polling timeout : %d!!\n",
					poll_to_static);
				HALMAC_REG_WRITE_8(
					halmac_adapter, REG_SYS_PW_CTRL,
					HALMAC_REG_READ_8(halmac_adapter,
							  REG_SYS_PW_CTRL) |
						BIT(3));
				HALMAC_REG_WRITE_8(
					halmac_adapter, REG_SYS_PW_CTRL,
					HALMAC_REG_READ_8(halmac_adapter,
							  REG_SYS_PW_CTRL) &
						~BIT(3));
				polling_bit = 0;
				polling_count =
					HALMAC_POLLING_READY_TIMEOUT_COUNT;
				flag = 1;
			} else {
				pr_err("[ERR]Pwr cmd polling timeout!!\n");
				pr_err("[ERR]Pwr cmd offset : %X!!\n",
				       sub_seq_cmd->offset);
				pr_err("[ERR]Pwr cmd value : %X!!\n",
				       sub_seq_cmd->value);
				pr_err("[ERR]Pwr cmd msk : %X!!\n",
				       sub_seq_cmd->msk);
				pr_err("[ERR]Read offset = %X value = %X!!\n",
				       offset, value);
				return HALMAC_RET_PWRSEQ_POLLING_FAIL;
			}
		} while (!polling_bit);
		break;
	case HALMAC_PWR_CMD_DELAY:
		if (sub_seq_cmd->value == HALMAC_PWRSEQ_DELAY_US)
			udelay(sub_seq_cmd->offset);
		else
			usleep_range(1000 * sub_seq_cmd->offset,
				     1000 * sub_seq_cmd->offset + 100);

		break;
	case HALMAC_PWR_CMD_READ:
		break;
	case HALMAC_PWR_CMD_END:
		return HALMAC_RET_SUCCESS;
	default:
		return HALMAC_RET_PWRSEQ_CMD_INCORRECT;
	}

	*reti = false;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_pwr_sub_seq_parer_88xx(struct halmac_adapter *halmac_adapter, u8 cut,
			      u8 fab, u8 intf,
			      struct halmac_wl_pwr_cfg_ *pwr_sub_seq_cfg)
{
	struct halmac_wl_pwr_cfg_ *sub_seq_cmd;
	bool reti;
	enum halmac_ret_status status;

	for (sub_seq_cmd = pwr_sub_seq_cfg;; sub_seq_cmd++) {
		if ((sub_seq_cmd->interface_msk & intf) &&
		    (sub_seq_cmd->fab_msk & fab) &&
		    (sub_seq_cmd->cut_msk & cut)) {
			status = halmac_pwr_sub_seq_parer_do_cmd_88xx(
				halmac_adapter, sub_seq_cmd, &reti);

			if (reti)
				return status;
		}
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_get_h2c_buff_free_space_88xx(struct halmac_adapter *halmac_adapter)
{
	u32 hw_wptr, fw_rptr;
	struct halmac_api *halmac_api =
		(struct halmac_api *)halmac_adapter->halmac_api;

	hw_wptr = HALMAC_REG_READ_32(halmac_adapter, REG_H2C_PKT_WRITEADDR) &
		  BIT_MASK_H2C_WR_ADDR;
	fw_rptr = HALMAC_REG_READ_32(halmac_adapter, REG_H2C_PKT_READADDR) &
		  BIT_MASK_H2C_READ_ADDR;

	if (hw_wptr >= fw_rptr)
		halmac_adapter->h2c_buf_free_space =
			halmac_adapter->h2c_buff_size - (hw_wptr - fw_rptr);
	else
		halmac_adapter->h2c_buf_free_space = fw_rptr - hw_wptr;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_send_h2c_pkt_88xx(struct halmac_adapter *halmac_adapter, u8 *hal_h2c_cmd,
			 u32 size, bool ack)
{
	u32 counter = 100;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	while (halmac_adapter->h2c_buf_free_space <=
	       HALMAC_H2C_CMD_SIZE_UNIT_88XX) {
		halmac_get_h2c_buff_free_space_88xx(halmac_adapter);
		counter--;
		if (counter == 0) {
			pr_err("h2c free space is not enough!!\n");
			return HALMAC_RET_H2C_SPACE_FULL;
		}
	}

	/* Send TxDesc + H2C_CMD */
	if (!PLATFORM_SEND_H2C_PKT(driver_adapter, hal_h2c_cmd, size)) {
		pr_err("Send H2C_CMD pkt error!!\n");
		return HALMAC_RET_SEND_H2C_FAIL;
	}

	halmac_adapter->h2c_buf_free_space -= HALMAC_H2C_CMD_SIZE_UNIT_88XX;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"H2C free space : %d\n",
			halmac_adapter->h2c_buf_free_space);

	return status;
}

enum halmac_ret_status
halmac_download_rsvd_page_88xx(struct halmac_adapter *halmac_adapter,
			       u8 *hal_buf, u32 size)
{
	u8 restore[3];
	u8 value8;
	u32 counter;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (size == 0) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
				"Rsvd page packet size is zero!!\n");
		return HALMAC_RET_ZERO_LEN_RSVD_PACKET;
	}

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_FIFOPAGE_CTRL_2 + 1);
	value8 = (u8)(value8 | BIT(7));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_FIFOPAGE_CTRL_2 + 1, value8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_CR + 1);
	restore[0] = value8;
	value8 = (u8)(value8 | BIT(0));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR + 1, value8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_BCN_CTRL);
	restore[1] = value8;
	value8 = (u8)((value8 & ~(BIT(3))) | BIT(4));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_BCN_CTRL, value8);

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_FWHW_TXQ_CTRL + 2);
	restore[2] = value8;
	value8 = (u8)(value8 & ~(BIT(6)));
	HALMAC_REG_WRITE_8(halmac_adapter, REG_FWHW_TXQ_CTRL + 2, value8);

	if (!PLATFORM_SEND_RSVD_PAGE(driver_adapter, hal_buf, size)) {
		pr_err("PLATFORM_SEND_RSVD_PAGE 1 error!!\n");
		status = HALMAC_RET_DL_RSVD_PAGE_FAIL;
	}

	/* Check Bcn_Valid_Bit */
	counter = 1000;
	while (!(HALMAC_REG_READ_8(halmac_adapter, REG_FIFOPAGE_CTRL_2 + 1) &
		 BIT(7))) {
		udelay(10);
		counter--;
		if (counter == 0) {
			pr_err("Polling Bcn_Valid_Fail error!!\n");
			status = HALMAC_RET_POLLING_BCN_VALID_FAIL;
			break;
		}
	}

	value8 = HALMAC_REG_READ_8(halmac_adapter, REG_FIFOPAGE_CTRL_2 + 1);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_FIFOPAGE_CTRL_2 + 1,
			   (value8 | BIT(7)));

	HALMAC_REG_WRITE_8(halmac_adapter, REG_FWHW_TXQ_CTRL + 2, restore[2]);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_BCN_CTRL, restore[1]);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR + 1, restore[0]);

	return status;
}

enum halmac_ret_status
halmac_set_h2c_header_88xx(struct halmac_adapter *halmac_adapter,
			   u8 *hal_h2c_hdr, u16 *seq, bool ack)
{
	void *driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s!!\n", __func__);

	H2C_CMD_HEADER_SET_CATEGORY(hal_h2c_hdr, 0x00);
	H2C_CMD_HEADER_SET_TOTAL_LEN(hal_h2c_hdr, 16);

	spin_lock(&halmac_adapter->h2c_seq_lock);
	H2C_CMD_HEADER_SET_SEQ_NUM(hal_h2c_hdr, halmac_adapter->h2c_packet_seq);
	*seq = halmac_adapter->h2c_packet_seq;
	halmac_adapter->h2c_packet_seq++;
	spin_unlock(&halmac_adapter->h2c_seq_lock);

	if (ack)
		H2C_CMD_HEADER_SET_ACK(hal_h2c_hdr, 1);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_set_fw_offload_h2c_header_88xx(
	struct halmac_adapter *halmac_adapter, u8 *hal_h2c_hdr,
	struct halmac_h2c_header_info *h2c_header_info, u16 *seq_num)
{
	void *driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s!!\n", __func__);

	FW_OFFLOAD_H2C_SET_TOTAL_LEN(hal_h2c_hdr,
				     8 + h2c_header_info->content_size);
	FW_OFFLOAD_H2C_SET_SUB_CMD_ID(hal_h2c_hdr, h2c_header_info->sub_cmd_id);

	FW_OFFLOAD_H2C_SET_CATEGORY(hal_h2c_hdr, 0x01);
	FW_OFFLOAD_H2C_SET_CMD_ID(hal_h2c_hdr, 0xFF);

	spin_lock(&halmac_adapter->h2c_seq_lock);
	FW_OFFLOAD_H2C_SET_SEQ_NUM(hal_h2c_hdr, halmac_adapter->h2c_packet_seq);
	*seq_num = halmac_adapter->h2c_packet_seq;
	halmac_adapter->h2c_packet_seq++;
	spin_unlock(&halmac_adapter->h2c_seq_lock);

	if (h2c_header_info->ack)
		FW_OFFLOAD_H2C_SET_ACK(hal_h2c_hdr, 1);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_send_h2c_set_pwr_mode_88xx(struct halmac_adapter *halmac_adapter,
				  struct halmac_fwlps_option *hal_fw_lps_opt)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX];
	u8 *h2c_header, *h2c_cmd;
	u16 seq = 0;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s!!\n", __func__);

	h2c_header = h2c_buff;
	h2c_cmd = h2c_header + HALMAC_H2C_CMD_HDR_SIZE_88XX;

	memset(h2c_buff, 0x00, HALMAC_H2C_CMD_SIZE_88XX);

	SET_PWR_MODE_SET_CMD_ID(h2c_cmd, CMD_ID_SET_PWR_MODE);
	SET_PWR_MODE_SET_CLASS(h2c_cmd, CLASS_SET_PWR_MODE);
	SET_PWR_MODE_SET_MODE(h2c_cmd, hal_fw_lps_opt->mode);
	SET_PWR_MODE_SET_CLK_REQUEST(h2c_cmd, hal_fw_lps_opt->clk_request);
	SET_PWR_MODE_SET_RLBM(h2c_cmd, hal_fw_lps_opt->rlbm);
	SET_PWR_MODE_SET_SMART_PS(h2c_cmd, hal_fw_lps_opt->smart_ps);
	SET_PWR_MODE_SET_AWAKE_INTERVAL(h2c_cmd,
					hal_fw_lps_opt->awake_interval);
	SET_PWR_MODE_SET_B_ALL_QUEUE_UAPSD(h2c_cmd,
					   hal_fw_lps_opt->all_queue_uapsd);
	SET_PWR_MODE_SET_PWR_STATE(h2c_cmd, hal_fw_lps_opt->pwr_state);
	SET_PWR_MODE_SET_ANT_AUTO_SWITCH(h2c_cmd,
					 hal_fw_lps_opt->ant_auto_switch);
	SET_PWR_MODE_SET_PS_ALLOW_BT_HIGH_PRIORITY(
		h2c_cmd, hal_fw_lps_opt->ps_allow_bt_high_priority);
	SET_PWR_MODE_SET_PROTECT_BCN(h2c_cmd, hal_fw_lps_opt->protect_bcn);
	SET_PWR_MODE_SET_SILENCE_PERIOD(h2c_cmd,
					hal_fw_lps_opt->silence_period);
	SET_PWR_MODE_SET_FAST_BT_CONNECT(h2c_cmd,
					 hal_fw_lps_opt->fast_bt_connect);
	SET_PWR_MODE_SET_TWO_ANTENNA_EN(h2c_cmd,
					hal_fw_lps_opt->two_antenna_en);
	SET_PWR_MODE_SET_ADOPT_USER_SETTING(h2c_cmd,
					    hal_fw_lps_opt->adopt_user_setting);
	SET_PWR_MODE_SET_DRV_BCN_EARLY_SHIFT(
		h2c_cmd, hal_fw_lps_opt->drv_bcn_early_shift);

	halmac_set_h2c_header_88xx(halmac_adapter, h2c_header, &seq, true);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s Fail = %x!!\n", __func__, status);
		return status;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_func_send_original_h2c_88xx(struct halmac_adapter *halmac_adapter,
				   u8 *original_h2c, u16 *seq, u8 ack)
{
	u8 H2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u8 *h2c_header, *h2c_cmd;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_send_original_h2c ==========>\n");

	h2c_header = H2c_buff;
	h2c_cmd = h2c_header + HALMAC_H2C_CMD_HDR_SIZE_88XX;
	memcpy(h2c_cmd, original_h2c, 8); /* Original H2C 8 byte */

	halmac_set_h2c_header_88xx(halmac_adapter, h2c_header, seq, ack);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, H2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, ack);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_original_h2c Fail = %x!!\n", status);
		return status;
	}

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_send_original_h2c <==========\n");

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_media_status_rpt_88xx(struct halmac_adapter *halmac_adapter, u8 op_mode,
			     u8 mac_id_ind, u8 mac_id, u8 mac_id_end)
{
	u8 H2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u8 *h2c_header, *h2c_cmd;
	u16 seq = 0;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_send_h2c_set_pwr_mode_88xx!!\n");

	h2c_header = H2c_buff;
	h2c_cmd = h2c_header + HALMAC_H2C_CMD_HDR_SIZE_88XX;

	memset(H2c_buff, 0x00, HALMAC_H2C_CMD_SIZE_88XX);

	MEDIA_STATUS_RPT_SET_CMD_ID(h2c_cmd, CMD_ID_MEDIA_STATUS_RPT);
	MEDIA_STATUS_RPT_SET_CLASS(h2c_cmd, CLASS_MEDIA_STATUS_RPT);
	MEDIA_STATUS_RPT_SET_OP_MODE(h2c_cmd, op_mode);
	MEDIA_STATUS_RPT_SET_MACID_IN(h2c_cmd, mac_id_ind);
	MEDIA_STATUS_RPT_SET_MACID(h2c_cmd, mac_id);
	MEDIA_STATUS_RPT_SET_MACID_END(h2c_cmd, mac_id_end);

	halmac_set_h2c_header_88xx(halmac_adapter, h2c_header, &seq, true);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, H2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("%s Fail = %x!!\n", __func__, status);
		return status;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_send_h2c_update_packet_88xx(struct halmac_adapter *halmac_adapter,
				   enum halmac_packet_id pkt_id, u8 *pkt,
				   u32 pkt_size)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
			    (u16)(halmac_adapter->txff_allocation
					  .rsvd_h2c_extra_info_pg_bndy &
				  BIT_MASK_BCN_HEAD_1_V1));

	ret_status =
		halmac_download_rsvd_page_88xx(halmac_adapter, pkt, pkt_size);

	if (ret_status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_download_rsvd_page_88xx Fail = %x!!\n",
		       ret_status);
		HALMAC_REG_WRITE_16(
			halmac_adapter, REG_FIFOPAGE_CTRL_2,
			(u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
			      BIT_MASK_BCN_HEAD_1_V1));
		return ret_status;
	}

	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
			    (u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
				  BIT_MASK_BCN_HEAD_1_V1));

	UPDATE_PACKET_SET_SIZE(
		h2c_buff,
		pkt_size + halmac_adapter->hw_config_info.txdesc_size);
	UPDATE_PACKET_SET_PACKET_ID(h2c_buff, pkt_id);
	UPDATE_PACKET_SET_PACKET_LOC(
		h2c_buff,
		halmac_adapter->txff_allocation.rsvd_h2c_extra_info_pg_bndy -
			halmac_adapter->txff_allocation.rsvd_pg_bndy);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_UPDATE_PACKET;
	h2c_header_info.content_size = 8;
	h2c_header_info.ack = true;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);
	halmac_adapter->halmac_state.update_packet_set.seq_num = h2c_seq_mum;

	ret_status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					      HALMAC_H2C_CMD_SIZE_88XX, true);

	if (ret_status != HALMAC_RET_SUCCESS) {
		pr_err("%s Fail = %x!!\n", __func__, ret_status);
		return ret_status;
	}

	return ret_status;
}

enum halmac_ret_status
halmac_send_h2c_phy_parameter_88xx(struct halmac_adapter *halmac_adapter,
				   struct halmac_phy_parameter_info *para_info,
				   bool full_fifo)
{
	bool drv_trigger_send = false;
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	u32 info_size = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_config_para_info *config_para_info;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;
	config_para_info = &halmac_adapter->config_para_info;

	if (!config_para_info->cfg_para_buf) {
		if (full_fifo)
			config_para_info->para_buf_size =
				HALMAC_EXTRA_INFO_BUFF_SIZE_FULL_FIFO_88XX;
		else
			config_para_info->para_buf_size =
				HALMAC_EXTRA_INFO_BUFF_SIZE_88XX;

		config_para_info->cfg_para_buf =
			kzalloc(config_para_info->para_buf_size, GFP_KERNEL);

		if (config_para_info->cfg_para_buf) {
			memset(config_para_info->cfg_para_buf, 0x00,
			       config_para_info->para_buf_size);
			config_para_info->full_fifo_mode = full_fifo;
			config_para_info->para_buf_w =
				config_para_info->cfg_para_buf;
			config_para_info->para_num = 0;
			config_para_info->avai_para_buf_size =
				config_para_info->para_buf_size;
			config_para_info->value_accumulation = 0;
			config_para_info->offset_accumulation = 0;
		} else {
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C,
					DBG_DMESG,
					"Allocate cfg_para_buf fail!!\n");
			return HALMAC_RET_MALLOC_FAIL;
		}
	}

	if (halmac_transition_cfg_para_state_88xx(
		    halmac_adapter,
		    HALMAC_CFG_PARA_CMD_CONSTRUCT_CONSTRUCTING) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	halmac_enqueue_para_buff_88xx(halmac_adapter, para_info,
				      config_para_info->para_buf_w,
				      &drv_trigger_send);

	if (para_info->cmd_id != HALMAC_PARAMETER_CMD_END) {
		config_para_info->para_num++;
		config_para_info->para_buf_w += HALMAC_FW_OFFLOAD_CMD_SIZE_88XX;
		config_para_info->avai_para_buf_size =
			config_para_info->avai_para_buf_size -
			HALMAC_FW_OFFLOAD_CMD_SIZE_88XX;
	}

	if ((config_para_info->avai_para_buf_size -
	     halmac_adapter->hw_config_info.txdesc_size) >
		    HALMAC_FW_OFFLOAD_CMD_SIZE_88XX &&
	    !drv_trigger_send)
		return HALMAC_RET_SUCCESS;

	if (config_para_info->para_num == 0) {
		kfree(config_para_info->cfg_para_buf);
		config_para_info->cfg_para_buf = NULL;
		config_para_info->para_buf_w = NULL;
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_WARNING,
				"no cfg parameter element!!\n");

		if (halmac_transition_cfg_para_state_88xx(
			    halmac_adapter,
			    HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE) !=
		    HALMAC_RET_SUCCESS)
			return HALMAC_RET_ERROR_STATE;

		return HALMAC_RET_SUCCESS;
	}

	if (halmac_transition_cfg_para_state_88xx(
		    halmac_adapter, HALMAC_CFG_PARA_CMD_CONSTRUCT_H2C_SENT) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	halmac_adapter->halmac_state.cfg_para_state_set.process_status =
		HALMAC_CMD_PROCESS_SENDING;

	if (config_para_info->full_fifo_mode)
		HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2, 0);
	else
		HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
				    (u16)(halmac_adapter->txff_allocation
						  .rsvd_h2c_extra_info_pg_bndy &
					  BIT_MASK_BCN_HEAD_1_V1));

	info_size =
		config_para_info->para_num * HALMAC_FW_OFFLOAD_CMD_SIZE_88XX;

	status = halmac_download_rsvd_page_88xx(
		halmac_adapter, (u8 *)config_para_info->cfg_para_buf,
		info_size);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_download_rsvd_page_88xx Fail!!\n");
	} else {
		halmac_gen_cfg_para_h2c_88xx(halmac_adapter, h2c_buff);

		h2c_header_info.sub_cmd_id = SUB_CMD_ID_CFG_PARAMETER;
		h2c_header_info.content_size = 4;
		h2c_header_info.ack = true;
		halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
						      &h2c_header_info,
						      &h2c_seq_mum);

		halmac_adapter->halmac_state.cfg_para_state_set.seq_num =
			h2c_seq_mum;

		status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
						  HALMAC_H2C_CMD_SIZE_88XX,
						  true);

		if (status != HALMAC_RET_SUCCESS)
			pr_err("halmac_send_h2c_pkt_88xx Fail!!\n");

		HALMAC_RT_TRACE(
			driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"config parameter time = %d\n",
			HALMAC_REG_READ_32(halmac_adapter, REG_FW_DBG6));
	}

	kfree(config_para_info->cfg_para_buf);
	config_para_info->cfg_para_buf = NULL;
	config_para_info->para_buf_w = NULL;

	/* Restore bcn head */
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
			    (u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
				  BIT_MASK_BCN_HEAD_1_V1));

	if (halmac_transition_cfg_para_state_88xx(
		    halmac_adapter, HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	if (!drv_trigger_send) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
				"Buffer full trigger sending H2C!!\n");
		return HALMAC_RET_PARA_SENDING;
	}

	return status;
}

static enum halmac_ret_status
halmac_enqueue_para_buff_88xx(struct halmac_adapter *halmac_adapter,
			      struct halmac_phy_parameter_info *para_info,
			      u8 *curr_buff_wptr, bool *end_cmd)
{
	struct halmac_config_para_info *config_para_info =
		&halmac_adapter->config_para_info;

	*end_cmd = false;

	PHY_PARAMETER_INFO_SET_LENGTH(curr_buff_wptr,
				      HALMAC_FW_OFFLOAD_CMD_SIZE_88XX);
	PHY_PARAMETER_INFO_SET_IO_CMD(curr_buff_wptr, para_info->cmd_id);

	switch (para_info->cmd_id) {
	case HALMAC_PARAMETER_CMD_BB_W8:
	case HALMAC_PARAMETER_CMD_BB_W16:
	case HALMAC_PARAMETER_CMD_BB_W32:
	case HALMAC_PARAMETER_CMD_MAC_W8:
	case HALMAC_PARAMETER_CMD_MAC_W16:
	case HALMAC_PARAMETER_CMD_MAC_W32:
		PHY_PARAMETER_INFO_SET_IO_ADDR(
			curr_buff_wptr, para_info->content.MAC_REG_W.offset);
		PHY_PARAMETER_INFO_SET_DATA(curr_buff_wptr,
					    para_info->content.MAC_REG_W.value);
		PHY_PARAMETER_INFO_SET_MASK(curr_buff_wptr,
					    para_info->content.MAC_REG_W.msk);
		PHY_PARAMETER_INFO_SET_MSK_EN(
			curr_buff_wptr, para_info->content.MAC_REG_W.msk_en);
		config_para_info->value_accumulation +=
			para_info->content.MAC_REG_W.value;
		config_para_info->offset_accumulation +=
			para_info->content.MAC_REG_W.offset;
		break;
	case HALMAC_PARAMETER_CMD_RF_W:
		/*In rf register, the address is only 1 byte*/
		PHY_PARAMETER_INFO_SET_RF_ADDR(
			curr_buff_wptr, para_info->content.RF_REG_W.offset);
		PHY_PARAMETER_INFO_SET_RF_PATH(
			curr_buff_wptr, para_info->content.RF_REG_W.rf_path);
		PHY_PARAMETER_INFO_SET_DATA(curr_buff_wptr,
					    para_info->content.RF_REG_W.value);
		PHY_PARAMETER_INFO_SET_MASK(curr_buff_wptr,
					    para_info->content.RF_REG_W.msk);
		PHY_PARAMETER_INFO_SET_MSK_EN(
			curr_buff_wptr, para_info->content.RF_REG_W.msk_en);
		config_para_info->value_accumulation +=
			para_info->content.RF_REG_W.value;
		config_para_info->offset_accumulation +=
			(para_info->content.RF_REG_W.offset +
			 (para_info->content.RF_REG_W.rf_path << 8));
		break;
	case HALMAC_PARAMETER_CMD_DELAY_US:
	case HALMAC_PARAMETER_CMD_DELAY_MS:
		PHY_PARAMETER_INFO_SET_DELAY_VALUE(
			curr_buff_wptr,
			para_info->content.DELAY_TIME.delay_time);
		break;
	case HALMAC_PARAMETER_CMD_END:
		*end_cmd = true;
		break;
	default:
		pr_err(" halmac_send_h2c_phy_parameter_88xx illegal cmd_id!!\n");
		break;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_gen_cfg_para_h2c_88xx(struct halmac_adapter *halmac_adapter,
			     u8 *h2c_buff)
{
	struct halmac_config_para_info *config_para_info =
		&halmac_adapter->config_para_info;

	CFG_PARAMETER_SET_NUM(h2c_buff, config_para_info->para_num);

	if (config_para_info->full_fifo_mode) {
		CFG_PARAMETER_SET_INIT_CASE(h2c_buff, 0x1);
		CFG_PARAMETER_SET_PHY_PARAMETER_LOC(h2c_buff, 0);
	} else {
		CFG_PARAMETER_SET_INIT_CASE(h2c_buff, 0x0);
		CFG_PARAMETER_SET_PHY_PARAMETER_LOC(
			h2c_buff,
			halmac_adapter->txff_allocation
					.rsvd_h2c_extra_info_pg_bndy -
				halmac_adapter->txff_allocation.rsvd_pg_bndy);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_send_h2c_run_datapack_88xx(struct halmac_adapter *halmac_adapter,
				  enum halmac_data_type halmac_data_type)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = NULL;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s!!\n", __func__);

	RUN_DATAPACK_SET_DATAPACK_ID(h2c_buff, halmac_data_type);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_RUN_DATAPACK;
	h2c_header_info.content_size = 4;
	h2c_header_info.ack = true;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_send_bt_coex_cmd_88xx(struct halmac_adapter *halmac_adapter, u8 *bt_buf,
			     u32 bt_size, u8 ack)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = NULL;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s!!\n", __func__);

	memcpy(h2c_buff + 8, bt_buf, bt_size);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_BT_COEX;
	h2c_header_info.content_size = (u16)bt_size;
	h2c_header_info.ack = ack;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, ack);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_func_ctrl_ch_switch_88xx(struct halmac_adapter *halmac_adapter,
				struct halmac_ch_switch_option *cs_option)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_cmd_process_status *process_status =
		&halmac_adapter->halmac_state.scan_state_set.process_status;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_ctrl_ch_switch!!\n");

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (halmac_transition_scan_state_88xx(
		    halmac_adapter, HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	*process_status = HALMAC_CMD_PROCESS_SENDING;

	if (cs_option->switch_en != 0) {
		HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
				    (u16)(halmac_adapter->txff_allocation
						  .rsvd_h2c_extra_info_pg_bndy &
					  BIT_MASK_BCN_HEAD_1_V1));

		status = halmac_download_rsvd_page_88xx(
			halmac_adapter, halmac_adapter->ch_sw_info.ch_info_buf,
			halmac_adapter->ch_sw_info.total_size);

		if (status != HALMAC_RET_SUCCESS) {
			pr_err("halmac_download_rsvd_page_88xx Fail = %x!!\n",
			       status);
			HALMAC_REG_WRITE_16(
				halmac_adapter, REG_FIFOPAGE_CTRL_2,
				(u16)(halmac_adapter->txff_allocation
					      .rsvd_pg_bndy &
				      BIT_MASK_BCN_HEAD_1_V1));
			return status;
		}

		HALMAC_REG_WRITE_16(
			halmac_adapter, REG_FIFOPAGE_CTRL_2,
			(u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
			      BIT_MASK_BCN_HEAD_1_V1));
	}

	CHANNEL_SWITCH_SET_SWITCH_START(h2c_buff, cs_option->switch_en);
	CHANNEL_SWITCH_SET_CHANNEL_NUM(h2c_buff,
				       halmac_adapter->ch_sw_info.ch_num);
	CHANNEL_SWITCH_SET_CHANNEL_INFO_LOC(
		h2c_buff,
		halmac_adapter->txff_allocation.rsvd_h2c_extra_info_pg_bndy -
			halmac_adapter->txff_allocation.rsvd_pg_bndy);
	CHANNEL_SWITCH_SET_DEST_CH_EN(h2c_buff, cs_option->dest_ch_en);
	CHANNEL_SWITCH_SET_DEST_CH(h2c_buff, cs_option->dest_ch);
	CHANNEL_SWITCH_SET_PRI_CH_IDX(h2c_buff, cs_option->dest_pri_ch_idx);
	CHANNEL_SWITCH_SET_ABSOLUTE_TIME(h2c_buff, cs_option->absolute_time_en);
	CHANNEL_SWITCH_SET_TSF_LOW(h2c_buff, cs_option->tsf_low);
	CHANNEL_SWITCH_SET_PERIODIC_OPTION(h2c_buff,
					   cs_option->periodic_option);
	CHANNEL_SWITCH_SET_NORMAL_CYCLE(h2c_buff, cs_option->normal_cycle);
	CHANNEL_SWITCH_SET_NORMAL_PERIOD(h2c_buff, cs_option->normal_period);
	CHANNEL_SWITCH_SET_SLOW_PERIOD(h2c_buff, cs_option->phase_2_period);
	CHANNEL_SWITCH_SET_CHANNEL_INFO_SIZE(
		h2c_buff, halmac_adapter->ch_sw_info.total_size);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_CHANNEL_SWITCH;
	h2c_header_info.content_size = 20;
	h2c_header_info.ack = true;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);
	halmac_adapter->halmac_state.scan_state_set.seq_num = h2c_seq_mum;

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS)
		pr_err("halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);

	kfree(halmac_adapter->ch_sw_info.ch_info_buf);
	halmac_adapter->ch_sw_info.ch_info_buf = NULL;
	halmac_adapter->ch_sw_info.ch_info_buf_w = NULL;
	halmac_adapter->ch_sw_info.extra_info_en = 0;
	halmac_adapter->ch_sw_info.buf_size = 0;
	halmac_adapter->ch_sw_info.avai_buf_size = 0;
	halmac_adapter->ch_sw_info.total_size = 0;
	halmac_adapter->ch_sw_info.ch_num = 0;

	if (halmac_transition_scan_state_88xx(halmac_adapter,
					      HALMAC_SCAN_CMD_CONSTRUCT_IDLE) !=
	    HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	return status;
}

enum halmac_ret_status
halmac_func_send_general_info_88xx(struct halmac_adapter *halmac_adapter,
				   struct halmac_general_info *general_info)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = NULL;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"halmac_send_general_info!!\n");

	GENERAL_INFO_SET_REF_TYPE(h2c_buff, general_info->rfe_type);
	GENERAL_INFO_SET_RF_TYPE(h2c_buff, general_info->rf_type);
	GENERAL_INFO_SET_FW_TX_BOUNDARY(
		h2c_buff,
		halmac_adapter->txff_allocation.rsvd_fw_txbuff_pg_bndy -
			halmac_adapter->txff_allocation.rsvd_pg_bndy);

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_GENERAL_INFO;
	h2c_header_info.content_size = 4;
	h2c_header_info.ack = false;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS)
		pr_err("halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);

	return status;
}

enum halmac_ret_status halmac_send_h2c_update_bcn_parse_info_88xx(
	struct halmac_adapter *halmac_adapter,
	struct halmac_bcn_ie_info *bcn_ie_info)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u16 h2c_seq_mum = 0;
	void *driver_adapter = halmac_adapter->driver_adapter;
	struct halmac_h2c_header_info h2c_header_info;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s!!\n", __func__);

	UPDATE_BEACON_PARSING_INFO_SET_FUNC_EN(h2c_buff, bcn_ie_info->func_en);
	UPDATE_BEACON_PARSING_INFO_SET_SIZE_TH(h2c_buff, bcn_ie_info->size_th);
	UPDATE_BEACON_PARSING_INFO_SET_TIMEOUT(h2c_buff, bcn_ie_info->timeout);

	UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_0(
		h2c_buff, (u32)(bcn_ie_info->ie_bmp[0]));
	UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_1(
		h2c_buff, (u32)(bcn_ie_info->ie_bmp[1]));
	UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_2(
		h2c_buff, (u32)(bcn_ie_info->ie_bmp[2]));
	UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_3(
		h2c_buff, (u32)(bcn_ie_info->ie_bmp[3]));
	UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_4(
		h2c_buff, (u32)(bcn_ie_info->ie_bmp[4]));

	h2c_header_info.sub_cmd_id = SUB_CMD_ID_UPDATE_BEACON_PARSING_INFO;
	h2c_header_info.content_size = 24;
	h2c_header_info.ack = true;
	halmac_set_fw_offload_h2c_header_88xx(halmac_adapter, h2c_buff,
					      &h2c_header_info, &h2c_seq_mum);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, true);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_pkt_88xx Fail =%x !!\n", status);
		return status;
	}

	return status;
}

enum halmac_ret_status
halmac_send_h2c_ps_tuning_para_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 h2c_buff[HALMAC_H2C_CMD_SIZE_88XX] = {0};
	u8 *h2c_header, *h2c_cmd;
	u16 seq = 0;
	void *driver_adapter = NULL;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"%s!!\n", __func__);

	h2c_header = h2c_buff;
	h2c_cmd = h2c_header + HALMAC_H2C_CMD_HDR_SIZE_88XX;

	halmac_set_h2c_header_88xx(halmac_adapter, h2c_header, &seq, false);

	status = halmac_send_h2c_pkt_88xx(halmac_adapter, h2c_buff,
					  HALMAC_H2C_CMD_SIZE_88XX, false);

	if (status != HALMAC_RET_SUCCESS) {
		pr_err("halmac_send_h2c_pkt_88xx Fail = %x!!\n", status);
		return status;
	}

	return status;
}

enum halmac_ret_status
halmac_parse_c2h_packet_88xx(struct halmac_adapter *halmac_adapter,
			     u8 *halmac_buf, u32 halmac_size)
{
	u8 c2h_cmd, c2h_sub_cmd_id;
	u8 *c2h_buf = halmac_buf + halmac_adapter->hw_config_info.rxdesc_size;
	u32 c2h_size = halmac_size - halmac_adapter->hw_config_info.rxdesc_size;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	c2h_cmd = (u8)C2H_HDR_GET_CMD_ID(c2h_buf);

	/* FW offload C2H cmd is 0xFF */
	if (c2h_cmd != 0xFF) {
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
				"C2H_PKT not for FwOffloadC2HFormat!!\n");
		return HALMAC_RET_C2H_NOT_HANDLED;
	}

	/* Get C2H sub cmd ID */
	c2h_sub_cmd_id = (u8)C2H_HDR_GET_C2H_SUB_CMD_ID(c2h_buf);

	switch (c2h_sub_cmd_id) {
	case C2H_SUB_CMD_ID_C2H_DBG:
		status = halmac_parse_c2h_debug_88xx(halmac_adapter, c2h_buf,
						     c2h_size);
		break;
	case C2H_SUB_CMD_ID_H2C_ACK_HDR:
		status = halmac_parse_h2c_ack_88xx(halmac_adapter, c2h_buf,
						   c2h_size);
		break;
	case C2H_SUB_CMD_ID_BT_COEX_INFO:
		status = HALMAC_RET_C2H_NOT_HANDLED;
		break;
	case C2H_SUB_CMD_ID_SCAN_STATUS_RPT:
		status = halmac_parse_scan_status_rpt_88xx(halmac_adapter,
							   c2h_buf, c2h_size);
		break;
	case C2H_SUB_CMD_ID_PSD_DATA:
		status = halmac_parse_psd_data_88xx(halmac_adapter, c2h_buf,
						    c2h_size);
		break;

	case C2H_SUB_CMD_ID_EFUSE_DATA:
		status = halmac_parse_efuse_data_88xx(halmac_adapter, c2h_buf,
						      c2h_size);
		break;
	default:
		pr_err("c2h_sub_cmd_id switch case out of boundary!!\n");
		pr_err("[ERR]c2h pkt : %.8X %.8X!!\n", *(u32 *)c2h_buf,
		       *(u32 *)(c2h_buf + 4));
		status = HALMAC_RET_C2H_NOT_HANDLED;
		break;
	}

	return status;
}

static enum halmac_ret_status
halmac_parse_c2h_debug_88xx(struct halmac_adapter *halmac_adapter, u8 *c2h_buf,
			    u32 c2h_size)
{
	void *driver_adapter = NULL;
	u8 *c2h_buf_local = (u8 *)NULL;
	u32 c2h_size_local = 0;
	u8 dbg_content_length = 0;
	u8 dbg_seq_num = 0;

	driver_adapter = halmac_adapter->driver_adapter;
	c2h_buf_local = c2h_buf;
	c2h_size_local = c2h_size;

	dbg_content_length = (u8)C2H_HDR_GET_LEN((u8 *)c2h_buf_local);

	if (dbg_content_length > C2H_DBG_CONTENT_MAX_LENGTH)
		return HALMAC_RET_SUCCESS;

	*(c2h_buf_local + C2H_DBG_HEADER_LENGTH + dbg_content_length - 2) =
		'\n';
	dbg_seq_num = (u8)(*(c2h_buf_local + C2H_DBG_HEADER_LENGTH));
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[RTKFW, SEQ=%d]: %s", dbg_seq_num,
			(char *)(c2h_buf_local + C2H_DBG_HEADER_LENGTH + 1));

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_scan_status_rpt_88xx(struct halmac_adapter *halmac_adapter,
				  u8 *c2h_buf, u32 c2h_size)
{
	u8 h2c_return_code;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status;

	h2c_return_code = (u8)SCAN_STATUS_RPT_GET_H2C_RETURN_CODE(c2h_buf);
	process_status = (enum halmac_h2c_return_code)h2c_return_code ==
					 HALMAC_H2C_RETURN_SUCCESS ?
				 HALMAC_CMD_PROCESS_DONE :
				 HALMAC_CMD_PROCESS_ERROR;

	PLATFORM_EVENT_INDICATION(driver_adapter, HALMAC_FEATURE_CHANNEL_SWITCH,
				  process_status, NULL, 0);

	halmac_adapter->halmac_state.scan_state_set.process_status =
		process_status;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]scan status : %X\n", process_status);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_psd_data_88xx(struct halmac_adapter *halmac_adapter, u8 *c2h_buf,
			   u32 c2h_size)
{
	u8 segment_id = 0, segment_size = 0, h2c_seq = 0;
	u16 total_size;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status;
	struct halmac_psd_state_set *psd_set =
		&halmac_adapter->halmac_state.psd_set;

	h2c_seq = (u8)PSD_DATA_GET_H2C_SEQ(c2h_buf);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]Seq num : h2c -> %d c2h -> %d\n",
			psd_set->seq_num, h2c_seq);
	if (h2c_seq != psd_set->seq_num) {
		pr_err("[ERR]Seq num mismatch : h2c -> %d c2h -> %d\n",
		       psd_set->seq_num, h2c_seq);
		return HALMAC_RET_SUCCESS;
	}

	if (psd_set->process_status != HALMAC_CMD_PROCESS_SENDING) {
		pr_err("[ERR]Not in HALMAC_CMD_PROCESS_SENDING\n");
		return HALMAC_RET_SUCCESS;
	}

	total_size = (u16)PSD_DATA_GET_TOTAL_SIZE(c2h_buf);
	segment_id = (u8)PSD_DATA_GET_SEGMENT_ID(c2h_buf);
	segment_size = (u8)PSD_DATA_GET_SEGMENT_SIZE(c2h_buf);
	psd_set->data_size = total_size;

	if (!psd_set->data)
		psd_set->data = kzalloc(psd_set->data_size, GFP_KERNEL);

	if (segment_id == 0)
		psd_set->segment_size = segment_size;

	memcpy(psd_set->data + segment_id * psd_set->segment_size,
	       c2h_buf + HALMAC_C2H_DATA_OFFSET_88XX, segment_size);

	if (!PSD_DATA_GET_END_SEGMENT(c2h_buf))
		return HALMAC_RET_SUCCESS;

	process_status = HALMAC_CMD_PROCESS_DONE;
	psd_set->process_status = process_status;

	PLATFORM_EVENT_INDICATION(driver_adapter, HALMAC_FEATURE_PSD,
				  process_status, psd_set->data,
				  psd_set->data_size);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_efuse_data_88xx(struct halmac_adapter *halmac_adapter, u8 *c2h_buf,
			     u32 c2h_size)
{
	u8 segment_id = 0, segment_size = 0, h2c_seq = 0;
	u8 *eeprom_map = NULL;
	u32 eeprom_size = halmac_adapter->hw_config_info.eeprom_size;
	u8 h2c_return_code = 0;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status;

	h2c_seq = (u8)EFUSE_DATA_GET_H2C_SEQ(c2h_buf);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]Seq num : h2c -> %d c2h -> %d\n",
			halmac_adapter->halmac_state.efuse_state_set.seq_num,
			h2c_seq);
	if (h2c_seq != halmac_adapter->halmac_state.efuse_state_set.seq_num) {
		pr_err("[ERR]Seq num mismatch : h2c -> %d c2h -> %d\n",
		       halmac_adapter->halmac_state.efuse_state_set.seq_num,
		       h2c_seq);
		return HALMAC_RET_SUCCESS;
	}

	if (halmac_adapter->halmac_state.efuse_state_set.process_status !=
	    HALMAC_CMD_PROCESS_SENDING) {
		pr_err("[ERR]Not in HALMAC_CMD_PROCESS_SENDING\n");
		return HALMAC_RET_SUCCESS;
	}

	segment_id = (u8)EFUSE_DATA_GET_SEGMENT_ID(c2h_buf);
	segment_size = (u8)EFUSE_DATA_GET_SEGMENT_SIZE(c2h_buf);
	if (segment_id == 0)
		halmac_adapter->efuse_segment_size = segment_size;

	eeprom_map = kzalloc(eeprom_size, GFP_KERNEL);
	if (!eeprom_map)
		return HALMAC_RET_MALLOC_FAIL;
	memset(eeprom_map, 0xFF, eeprom_size);

	spin_lock(&halmac_adapter->efuse_lock);
	memcpy(halmac_adapter->hal_efuse_map +
		       segment_id * halmac_adapter->efuse_segment_size,
	       c2h_buf + HALMAC_C2H_DATA_OFFSET_88XX, segment_size);
	spin_unlock(&halmac_adapter->efuse_lock);

	if (!EFUSE_DATA_GET_END_SEGMENT(c2h_buf)) {
		kfree(eeprom_map);
		return HALMAC_RET_SUCCESS;
	}

	h2c_return_code =
		halmac_adapter->halmac_state.efuse_state_set.fw_return_code;

	if ((enum halmac_h2c_return_code)h2c_return_code ==
	    HALMAC_H2C_RETURN_SUCCESS) {
		process_status = HALMAC_CMD_PROCESS_DONE;
		halmac_adapter->halmac_state.efuse_state_set.process_status =
			process_status;

		spin_lock(&halmac_adapter->efuse_lock);
		halmac_adapter->hal_efuse_map_valid = true;
		spin_unlock(&halmac_adapter->efuse_lock);

		if (halmac_adapter->event_trigger.physical_efuse_map == 1) {
			PLATFORM_EVENT_INDICATION(
				driver_adapter,
				HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE,
				process_status, halmac_adapter->hal_efuse_map,
				halmac_adapter->hw_config_info.efuse_size);
			halmac_adapter->event_trigger.physical_efuse_map = 0;
		}

		if (halmac_adapter->event_trigger.logical_efuse_map == 1) {
			if (halmac_eeprom_parser_88xx(
				    halmac_adapter,
				    halmac_adapter->hal_efuse_map,
				    eeprom_map) != HALMAC_RET_SUCCESS) {
				kfree(eeprom_map);
				return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
			PLATFORM_EVENT_INDICATION(
				driver_adapter,
				HALMAC_FEATURE_DUMP_LOGICAL_EFUSE,
				process_status, eeprom_map, eeprom_size);
			halmac_adapter->event_trigger.logical_efuse_map = 0;
		}
	} else {
		process_status = HALMAC_CMD_PROCESS_ERROR;
		halmac_adapter->halmac_state.efuse_state_set.process_status =
			process_status;

		if (halmac_adapter->event_trigger.physical_efuse_map == 1) {
			PLATFORM_EVENT_INDICATION(
				driver_adapter,
				HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE,
				process_status,
				&halmac_adapter->halmac_state.efuse_state_set
					 .fw_return_code,
				1);
			halmac_adapter->event_trigger.physical_efuse_map = 0;
		}

		if (halmac_adapter->event_trigger.logical_efuse_map == 1) {
			if (halmac_eeprom_parser_88xx(
				    halmac_adapter,
				    halmac_adapter->hal_efuse_map,
				    eeprom_map) != HALMAC_RET_SUCCESS) {
				kfree(eeprom_map);
				return HALMAC_RET_EEPROM_PARSING_FAIL;
			}
			PLATFORM_EVENT_INDICATION(
				driver_adapter,
				HALMAC_FEATURE_DUMP_LOGICAL_EFUSE,
				process_status,
				&halmac_adapter->halmac_state.efuse_state_set
					 .fw_return_code,
				1);
			halmac_adapter->event_trigger.logical_efuse_map = 0;
		}
	}

	kfree(eeprom_map);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_88xx(struct halmac_adapter *halmac_adapter, u8 *c2h_buf,
			  u32 c2h_size)
{
	u8 h2c_cmd_id, h2c_sub_cmd_id;
	u8 h2c_return_code;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"Ack for C2H!!\n");

	h2c_return_code = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(c2h_buf);
	if ((enum halmac_h2c_return_code)h2c_return_code !=
	    HALMAC_H2C_RETURN_SUCCESS)
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
				"C2H_PKT Status Error!! Status = %d\n",
				h2c_return_code);

	h2c_cmd_id = (u8)H2C_ACK_HDR_GET_H2C_CMD_ID(c2h_buf);

	if (h2c_cmd_id != 0xFF) {
		pr_err("original h2c ack is not handled!!\n");
		status = HALMAC_RET_C2H_NOT_HANDLED;
	} else {
		h2c_sub_cmd_id = (u8)H2C_ACK_HDR_GET_H2C_SUB_CMD_ID(c2h_buf);

		switch (h2c_sub_cmd_id) {
		case H2C_SUB_CMD_ID_DUMP_PHYSICAL_EFUSE_ACK:
			status = halmac_parse_h2c_ack_phy_efuse_88xx(
				halmac_adapter, c2h_buf, c2h_size);
			break;
		case H2C_SUB_CMD_ID_CFG_PARAMETER_ACK:
			status = halmac_parse_h2c_ack_cfg_para_88xx(
				halmac_adapter, c2h_buf, c2h_size);
			break;
		case H2C_SUB_CMD_ID_UPDATE_PACKET_ACK:
			status = halmac_parse_h2c_ack_update_packet_88xx(
				halmac_adapter, c2h_buf, c2h_size);
			break;
		case H2C_SUB_CMD_ID_UPDATE_DATAPACK_ACK:
			status = halmac_parse_h2c_ack_update_datapack_88xx(
				halmac_adapter, c2h_buf, c2h_size);
			break;
		case H2C_SUB_CMD_ID_RUN_DATAPACK_ACK:
			status = halmac_parse_h2c_ack_run_datapack_88xx(
				halmac_adapter, c2h_buf, c2h_size);
			break;
		case H2C_SUB_CMD_ID_CHANNEL_SWITCH_ACK:
			status = halmac_parse_h2c_ack_channel_switch_88xx(
				halmac_adapter, c2h_buf, c2h_size);
			break;
		case H2C_SUB_CMD_ID_IQK_ACK:
			status = halmac_parse_h2c_ack_iqk_88xx(
				halmac_adapter, c2h_buf, c2h_size);
			break;
		case H2C_SUB_CMD_ID_POWER_TRACKING_ACK:
			status = halmac_parse_h2c_ack_power_tracking_88xx(
				halmac_adapter, c2h_buf, c2h_size);
			break;
		case H2C_SUB_CMD_ID_PSD_ACK:
			break;
		default:
			pr_err("h2c_sub_cmd_id switch case out of boundary!!\n");
			status = HALMAC_RET_C2H_NOT_HANDLED;
			break;
		}
	}

	return status;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_phy_efuse_88xx(struct halmac_adapter *halmac_adapter,
				    u8 *c2h_buf, u32 c2h_size)
{
	u8 h2c_seq = 0;
	u8 h2c_return_code;
	void *driver_adapter = halmac_adapter->driver_adapter;

	h2c_seq = (u8)H2C_ACK_HDR_GET_H2C_SEQ(c2h_buf);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]Seq num : h2c -> %d c2h -> %d\n",
			halmac_adapter->halmac_state.efuse_state_set.seq_num,
			h2c_seq);
	if (h2c_seq != halmac_adapter->halmac_state.efuse_state_set.seq_num) {
		pr_err("[ERR]Seq num mismatch : h2c -> %d c2h -> %d\n",
		       halmac_adapter->halmac_state.efuse_state_set.seq_num,
		       h2c_seq);
		return HALMAC_RET_SUCCESS;
	}

	if (halmac_adapter->halmac_state.efuse_state_set.process_status !=
	    HALMAC_CMD_PROCESS_SENDING) {
		pr_err("[ERR]Not in HALMAC_CMD_PROCESS_SENDING\n");
		return HALMAC_RET_SUCCESS;
	}

	h2c_return_code = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(c2h_buf);
	halmac_adapter->halmac_state.efuse_state_set.fw_return_code =
		h2c_return_code;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_cfg_para_88xx(struct halmac_adapter *halmac_adapter,
				   u8 *c2h_buf, u32 c2h_size)
{
	u8 h2c_seq = 0;
	u8 h2c_return_code;
	u32 offset_accu = 0, value_accu = 0;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status =
		HALMAC_CMD_PROCESS_UNDEFINE;

	h2c_seq = (u8)H2C_ACK_HDR_GET_H2C_SEQ(c2h_buf);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"Seq num : h2c -> %d c2h -> %d\n",
			halmac_adapter->halmac_state.cfg_para_state_set.seq_num,
			h2c_seq);
	if (h2c_seq !=
	    halmac_adapter->halmac_state.cfg_para_state_set.seq_num) {
		pr_err("Seq num mismatch : h2c -> %d c2h -> %d\n",
		       halmac_adapter->halmac_state.cfg_para_state_set.seq_num,
		       h2c_seq);
		return HALMAC_RET_SUCCESS;
	}

	if (halmac_adapter->halmac_state.cfg_para_state_set.process_status !=
	    HALMAC_CMD_PROCESS_SENDING) {
		pr_err("Not in HALMAC_CMD_PROCESS_SENDING\n");
		return HALMAC_RET_SUCCESS;
	}

	h2c_return_code = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(c2h_buf);
	halmac_adapter->halmac_state.cfg_para_state_set.fw_return_code =
		h2c_return_code;
	offset_accu = CFG_PARAMETER_ACK_GET_OFFSET_ACCUMULATION(c2h_buf);
	value_accu = CFG_PARAMETER_ACK_GET_VALUE_ACCUMULATION(c2h_buf);

	if ((offset_accu !=
	     halmac_adapter->config_para_info.offset_accumulation) ||
	    (value_accu !=
	     halmac_adapter->config_para_info.value_accumulation)) {
		pr_err("[C2H]offset_accu : %x, value_accu : %x!!\n",
		       offset_accu, value_accu);
		pr_err("[Adapter]offset_accu : %x, value_accu : %x!!\n",
		       halmac_adapter->config_para_info.offset_accumulation,
		       halmac_adapter->config_para_info.value_accumulation);
		process_status = HALMAC_CMD_PROCESS_ERROR;
	}

	if ((enum halmac_h2c_return_code)h2c_return_code ==
		    HALMAC_H2C_RETURN_SUCCESS &&
	    process_status != HALMAC_CMD_PROCESS_ERROR) {
		process_status = HALMAC_CMD_PROCESS_DONE;
		halmac_adapter->halmac_state.cfg_para_state_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(driver_adapter,
					  HALMAC_FEATURE_CFG_PARA,
					  process_status, NULL, 0);
	} else {
		process_status = HALMAC_CMD_PROCESS_ERROR;
		halmac_adapter->halmac_state.cfg_para_state_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(
			driver_adapter, HALMAC_FEATURE_CFG_PARA, process_status,
			&halmac_adapter->halmac_state.cfg_para_state_set
				 .fw_return_code,
			1);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_update_packet_88xx(struct halmac_adapter *halmac_adapter,
					u8 *c2h_buf, u32 c2h_size)
{
	u8 h2c_seq = 0;
	u8 h2c_return_code;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status;

	h2c_seq = (u8)H2C_ACK_HDR_GET_H2C_SEQ(c2h_buf);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]Seq num : h2c -> %d c2h -> %d\n",
			halmac_adapter->halmac_state.update_packet_set.seq_num,
			h2c_seq);
	if (h2c_seq != halmac_adapter->halmac_state.update_packet_set.seq_num) {
		pr_err("[ERR]Seq num mismatch : h2c -> %d c2h -> %d\n",
		       halmac_adapter->halmac_state.update_packet_set.seq_num,
		       h2c_seq);
		return HALMAC_RET_SUCCESS;
	}

	if (halmac_adapter->halmac_state.update_packet_set.process_status !=
	    HALMAC_CMD_PROCESS_SENDING) {
		pr_err("[ERR]Not in HALMAC_CMD_PROCESS_SENDING\n");
		return HALMAC_RET_SUCCESS;
	}

	h2c_return_code = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(c2h_buf);
	halmac_adapter->halmac_state.update_packet_set.fw_return_code =
		h2c_return_code;

	if ((enum halmac_h2c_return_code)h2c_return_code ==
	    HALMAC_H2C_RETURN_SUCCESS) {
		process_status = HALMAC_CMD_PROCESS_DONE;
		halmac_adapter->halmac_state.update_packet_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(driver_adapter,
					  HALMAC_FEATURE_UPDATE_PACKET,
					  process_status, NULL, 0);
	} else {
		process_status = HALMAC_CMD_PROCESS_ERROR;
		halmac_adapter->halmac_state.update_packet_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(
			driver_adapter, HALMAC_FEATURE_UPDATE_PACKET,
			process_status,
			&halmac_adapter->halmac_state.update_packet_set
				 .fw_return_code,
			1);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_update_datapack_88xx(struct halmac_adapter *halmac_adapter,
					  u8 *c2h_buf, u32 c2h_size)
{
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status =
		HALMAC_CMD_PROCESS_UNDEFINE;

	PLATFORM_EVENT_INDICATION(driver_adapter,
				  HALMAC_FEATURE_UPDATE_DATAPACK,
				  process_status, NULL, 0);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_run_datapack_88xx(struct halmac_adapter *halmac_adapter,
				       u8 *c2h_buf, u32 c2h_size)
{
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status =
		HALMAC_CMD_PROCESS_UNDEFINE;

	PLATFORM_EVENT_INDICATION(driver_adapter, HALMAC_FEATURE_RUN_DATAPACK,
				  process_status, NULL, 0);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_channel_switch_88xx(struct halmac_adapter *halmac_adapter,
					 u8 *c2h_buf, u32 c2h_size)
{
	u8 h2c_seq = 0;
	u8 h2c_return_code;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status;

	h2c_seq = (u8)H2C_ACK_HDR_GET_H2C_SEQ(c2h_buf);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]Seq num : h2c -> %d c2h -> %d\n",
			halmac_adapter->halmac_state.scan_state_set.seq_num,
			h2c_seq);
	if (h2c_seq != halmac_adapter->halmac_state.scan_state_set.seq_num) {
		pr_err("[ERR]Seq num misactch : h2c -> %d c2h -> %d\n",
		       halmac_adapter->halmac_state.scan_state_set.seq_num,
		       h2c_seq);
		return HALMAC_RET_SUCCESS;
	}

	if (halmac_adapter->halmac_state.scan_state_set.process_status !=
	    HALMAC_CMD_PROCESS_SENDING) {
		pr_err("[ERR]Not in HALMAC_CMD_PROCESS_SENDING\n");
		return HALMAC_RET_SUCCESS;
	}

	h2c_return_code = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(c2h_buf);
	halmac_adapter->halmac_state.scan_state_set.fw_return_code =
		h2c_return_code;

	if ((enum halmac_h2c_return_code)h2c_return_code ==
	    HALMAC_H2C_RETURN_SUCCESS) {
		process_status = HALMAC_CMD_PROCESS_RCVD;
		halmac_adapter->halmac_state.scan_state_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(driver_adapter,
					  HALMAC_FEATURE_CHANNEL_SWITCH,
					  process_status, NULL, 0);
	} else {
		process_status = HALMAC_CMD_PROCESS_ERROR;
		halmac_adapter->halmac_state.scan_state_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(
			driver_adapter, HALMAC_FEATURE_CHANNEL_SWITCH,
			process_status, &halmac_adapter->halmac_state
						 .scan_state_set.fw_return_code,
			1);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_iqk_88xx(struct halmac_adapter *halmac_adapter,
			      u8 *c2h_buf, u32 c2h_size)
{
	u8 h2c_seq = 0;
	u8 h2c_return_code;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status;

	h2c_seq = (u8)H2C_ACK_HDR_GET_H2C_SEQ(c2h_buf);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]Seq num : h2c -> %d c2h -> %d\n",
			halmac_adapter->halmac_state.iqk_set.seq_num, h2c_seq);
	if (h2c_seq != halmac_adapter->halmac_state.iqk_set.seq_num) {
		pr_err("[ERR]Seq num misactch : h2c -> %d c2h -> %d\n",
		       halmac_adapter->halmac_state.iqk_set.seq_num, h2c_seq);
		return HALMAC_RET_SUCCESS;
	}

	if (halmac_adapter->halmac_state.iqk_set.process_status !=
	    HALMAC_CMD_PROCESS_SENDING) {
		pr_err("[ERR]Not in HALMAC_CMD_PROCESS_SENDING\n");
		return HALMAC_RET_SUCCESS;
	}

	h2c_return_code = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(c2h_buf);
	halmac_adapter->halmac_state.iqk_set.fw_return_code = h2c_return_code;

	if ((enum halmac_h2c_return_code)h2c_return_code ==
	    HALMAC_H2C_RETURN_SUCCESS) {
		process_status = HALMAC_CMD_PROCESS_DONE;
		halmac_adapter->halmac_state.iqk_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(driver_adapter, HALMAC_FEATURE_IQK,
					  process_status, NULL, 0);
	} else {
		process_status = HALMAC_CMD_PROCESS_ERROR;
		halmac_adapter->halmac_state.iqk_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(
			driver_adapter, HALMAC_FEATURE_IQK, process_status,
			&halmac_adapter->halmac_state.iqk_set.fw_return_code,
			1);
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
halmac_parse_h2c_ack_power_tracking_88xx(struct halmac_adapter *halmac_adapter,
					 u8 *c2h_buf, u32 c2h_size)
{
	u8 h2c_seq = 0;
	u8 h2c_return_code;
	void *driver_adapter = halmac_adapter->driver_adapter;
	enum halmac_cmd_process_status process_status;

	h2c_seq = (u8)H2C_ACK_HDR_GET_H2C_SEQ(c2h_buf);
	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_H2C, DBG_DMESG,
			"[TRACE]Seq num : h2c -> %d c2h -> %d\n",
			halmac_adapter->halmac_state.power_tracking_set.seq_num,
			h2c_seq);
	if (h2c_seq !=
	    halmac_adapter->halmac_state.power_tracking_set.seq_num) {
		pr_err("[ERR]Seq num mismatch : h2c -> %d c2h -> %d\n",
		       halmac_adapter->halmac_state.power_tracking_set.seq_num,
		       h2c_seq);
		return HALMAC_RET_SUCCESS;
	}

	if (halmac_adapter->halmac_state.power_tracking_set.process_status !=
	    HALMAC_CMD_PROCESS_SENDING) {
		pr_err("[ERR]Not in HALMAC_CMD_PROCESS_SENDING\n");
		return HALMAC_RET_SUCCESS;
	}

	h2c_return_code = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(c2h_buf);
	halmac_adapter->halmac_state.power_tracking_set.fw_return_code =
		h2c_return_code;

	if ((enum halmac_h2c_return_code)h2c_return_code ==
	    HALMAC_H2C_RETURN_SUCCESS) {
		process_status = HALMAC_CMD_PROCESS_DONE;
		halmac_adapter->halmac_state.power_tracking_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(driver_adapter,
					  HALMAC_FEATURE_POWER_TRACKING,
					  process_status, NULL, 0);
	} else {
		process_status = HALMAC_CMD_PROCESS_ERROR;
		halmac_adapter->halmac_state.power_tracking_set.process_status =
			process_status;
		PLATFORM_EVENT_INDICATION(
			driver_adapter, HALMAC_FEATURE_POWER_TRACKING,
			process_status,
			&halmac_adapter->halmac_state.power_tracking_set
				 .fw_return_code,
			1);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_convert_to_sdio_bus_offset_88xx(struct halmac_adapter *halmac_adapter,
				       u32 *halmac_offset)
{
	void *driver_adapter = NULL;

	driver_adapter = halmac_adapter->driver_adapter;

	switch ((*halmac_offset) & 0xFFFF0000) {
	case WLAN_IOREG_OFFSET:
		*halmac_offset = (HALMAC_SDIO_CMD_ADDR_MAC_REG << 13) |
				 (*halmac_offset & HALMAC_WLAN_MAC_REG_MSK);
		break;
	case SDIO_LOCAL_OFFSET:
		*halmac_offset = (HALMAC_SDIO_CMD_ADDR_SDIO_REG << 13) |
				 (*halmac_offset & HALMAC_SDIO_LOCAL_MSK);
		break;
	default:
		*halmac_offset = 0xFFFFFFFF;
		pr_err("Unknown base address!!\n");
		return HALMAC_RET_CONVERT_SDIO_OFFSET_FAIL;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_update_sdio_free_page_88xx(struct halmac_adapter *halmac_adapter)
{
	u32 free_page = 0, free_page2 = 0, free_page3 = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	struct halmac_sdio_free_space *sdio_free_space;
	u8 data[12] = {0};

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	sdio_free_space = &halmac_adapter->sdio_free_space;
	/*need to use HALMAC_REG_READ_N, 20160316, Soar*/
	HALMAC_REG_SDIO_CMD53_READ_N(halmac_adapter, REG_SDIO_FREE_TXPG, 12,
				     data);
	free_page =
		data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
	free_page2 =
		data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
	free_page3 =
		data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);

	sdio_free_space->high_queue_number =
		(u16)BIT_GET_HIQ_FREEPG_V1(free_page);
	sdio_free_space->normal_queue_number =
		(u16)BIT_GET_MID_FREEPG_V1(free_page);
	sdio_free_space->low_queue_number =
		(u16)BIT_GET_LOW_FREEPG_V1(free_page2);
	sdio_free_space->public_queue_number =
		(u16)BIT_GET_PUB_FREEPG_V1(free_page2);
	sdio_free_space->extra_queue_number =
		(u16)BIT_GET_EXQ_FREEPG_V1(free_page3);
	sdio_free_space->ac_oqt_number = (u8)((free_page3 >> 16) & 0xFF);
	sdio_free_space->non_ac_oqt_number = (u8)((free_page3 >> 24) & 0xFF);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_update_oqt_free_space_88xx(struct halmac_adapter *halmac_adapter)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	struct halmac_sdio_free_space *sdio_free_space;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s ==========>\n", __func__);

	sdio_free_space = &halmac_adapter->sdio_free_space;

	sdio_free_space->ac_oqt_number = HALMAC_REG_READ_8(
		halmac_adapter, REG_SDIO_OQT_FREE_TXPG_V1 + 2);
	sdio_free_space->ac_empty =
		HALMAC_REG_READ_8(halmac_adapter, REG_TXPKT_EMPTY);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT, DBG_DMESG,
			"%s <==========\n", __func__);

	return HALMAC_RET_SUCCESS;
}

enum halmac_efuse_cmd_construct_state
halmac_query_efuse_curr_state_88xx(struct halmac_adapter *halmac_adapter)
{
	return halmac_adapter->halmac_state.efuse_state_set
		.efuse_cmd_construct_state;
}

enum halmac_ret_status halmac_transition_efuse_state_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_efuse_cmd_construct_state dest_state)
{
	struct halmac_efuse_state_set *efuse_state =
		&halmac_adapter->halmac_state.efuse_state_set;

	if (efuse_state->efuse_cmd_construct_state !=
		    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE &&
	    efuse_state->efuse_cmd_construct_state !=
		    HALMAC_EFUSE_CMD_CONSTRUCT_BUSY &&
	    efuse_state->efuse_cmd_construct_state !=
		    HALMAC_EFUSE_CMD_CONSTRUCT_H2C_SENT)
		return HALMAC_RET_ERROR_STATE;

	if (efuse_state->efuse_cmd_construct_state == dest_state)
		return HALMAC_RET_ERROR_STATE;

	if (dest_state == HALMAC_EFUSE_CMD_CONSTRUCT_BUSY) {
		if (efuse_state->efuse_cmd_construct_state ==
		    HALMAC_EFUSE_CMD_CONSTRUCT_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_EFUSE_CMD_CONSTRUCT_H2C_SENT) {
		if (efuse_state->efuse_cmd_construct_state ==
		    HALMAC_EFUSE_CMD_CONSTRUCT_IDLE)
			return HALMAC_RET_ERROR_STATE;
	}

	efuse_state->efuse_cmd_construct_state = dest_state;

	return HALMAC_RET_SUCCESS;
}

enum halmac_cfg_para_cmd_construct_state
halmac_query_cfg_para_curr_state_88xx(struct halmac_adapter *halmac_adapter)
{
	return halmac_adapter->halmac_state.cfg_para_state_set
		.cfg_para_cmd_construct_state;
}

enum halmac_ret_status halmac_transition_cfg_para_state_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cfg_para_cmd_construct_state dest_state)
{
	struct halmac_cfg_para_state_set *cfg_para =
		&halmac_adapter->halmac_state.cfg_para_state_set;

	if (cfg_para->cfg_para_cmd_construct_state !=
		    HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE &&
	    cfg_para->cfg_para_cmd_construct_state !=
		    HALMAC_CFG_PARA_CMD_CONSTRUCT_CONSTRUCTING &&
	    cfg_para->cfg_para_cmd_construct_state !=
		    HALMAC_CFG_PARA_CMD_CONSTRUCT_H2C_SENT)
		return HALMAC_RET_ERROR_STATE;

	if (dest_state == HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE) {
		if (cfg_para->cfg_para_cmd_construct_state ==
		    HALMAC_CFG_PARA_CMD_CONSTRUCT_CONSTRUCTING)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CFG_PARA_CMD_CONSTRUCT_CONSTRUCTING) {
		if (cfg_para->cfg_para_cmd_construct_state ==
		    HALMAC_CFG_PARA_CMD_CONSTRUCT_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CFG_PARA_CMD_CONSTRUCT_H2C_SENT) {
		if (cfg_para->cfg_para_cmd_construct_state ==
			    HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE ||
		    cfg_para->cfg_para_cmd_construct_state ==
			    HALMAC_CFG_PARA_CMD_CONSTRUCT_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	}

	cfg_para->cfg_para_cmd_construct_state = dest_state;

	return HALMAC_RET_SUCCESS;
}

enum halmac_scan_cmd_construct_state
halmac_query_scan_curr_state_88xx(struct halmac_adapter *halmac_adapter)
{
	return halmac_adapter->halmac_state.scan_state_set
		.scan_cmd_construct_state;
}

enum halmac_ret_status halmac_transition_scan_state_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_scan_cmd_construct_state dest_state)
{
	struct halmac_scan_state_set *scan =
		&halmac_adapter->halmac_state.scan_state_set;

	if (scan->scan_cmd_construct_state > HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT)
		return HALMAC_RET_ERROR_STATE;

	if (dest_state == HALMAC_SCAN_CMD_CONSTRUCT_IDLE) {
		if (scan->scan_cmd_construct_state ==
			    HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED ||
		    scan->scan_cmd_construct_state ==
			    HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED) {
		if (scan->scan_cmd_construct_state ==
		    HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING) {
		if (scan->scan_cmd_construct_state ==
			    HALMAC_SCAN_CMD_CONSTRUCT_IDLE ||
		    scan->scan_cmd_construct_state ==
			    HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT) {
		if (scan->scan_cmd_construct_state !=
			    HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING &&
		    scan->scan_cmd_construct_state !=
			    HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED)
			return HALMAC_RET_ERROR_STATE;
	}

	scan->scan_cmd_construct_state = dest_state;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_query_cfg_para_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size)
{
	struct halmac_cfg_para_state_set *cfg_para_state_set =
		&halmac_adapter->halmac_state.cfg_para_state_set;

	*process_status = cfg_para_state_set->process_status;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_query_dump_physical_efuse_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size)
{
	void *driver_adapter = NULL;
	struct halmac_efuse_state_set *efuse_state_set =
		&halmac_adapter->halmac_state.efuse_state_set;

	driver_adapter = halmac_adapter->driver_adapter;

	*process_status = efuse_state_set->process_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*process_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < halmac_adapter->hw_config_info.efuse_size) {
			*size = halmac_adapter->hw_config_info.efuse_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = halmac_adapter->hw_config_info.efuse_size;
		memcpy(data, halmac_adapter->hal_efuse_map, *size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_query_dump_logical_efuse_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size)
{
	u8 *eeprom_map = NULL;
	u32 eeprom_size = halmac_adapter->hw_config_info.eeprom_size;
	void *driver_adapter = NULL;
	struct halmac_efuse_state_set *efuse_state_set =
		&halmac_adapter->halmac_state.efuse_state_set;

	driver_adapter = halmac_adapter->driver_adapter;

	*process_status = efuse_state_set->process_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*process_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < eeprom_size) {
			*size = eeprom_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = eeprom_size;

		eeprom_map = kzalloc(eeprom_size, GFP_KERNEL);
		if (!eeprom_map)
			return HALMAC_RET_MALLOC_FAIL;
		memset(eeprom_map, 0xFF, eeprom_size);

		if (halmac_eeprom_parser_88xx(
			    halmac_adapter, halmac_adapter->hal_efuse_map,
			    eeprom_map) != HALMAC_RET_SUCCESS) {
			kfree(eeprom_map);
			return HALMAC_RET_EEPROM_PARSING_FAIL;
		}

		memcpy(data, eeprom_map, *size);

		kfree(eeprom_map);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_query_channel_switch_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size)
{
	struct halmac_scan_state_set *scan_state_set =
		&halmac_adapter->halmac_state.scan_state_set;

	*process_status = scan_state_set->process_status;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_query_update_packet_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size)
{
	struct halmac_update_packet_state_set *update_packet_set =
		&halmac_adapter->halmac_state.update_packet_set;

	*process_status = update_packet_set->process_status;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_query_iqk_status_88xx(struct halmac_adapter *halmac_adapter,
			     enum halmac_cmd_process_status *process_status,
			     u8 *data, u32 *size)
{
	struct halmac_iqk_state_set *iqk_set =
		&halmac_adapter->halmac_state.iqk_set;

	*process_status = iqk_set->process_status;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status halmac_query_power_tracking_status_88xx(
	struct halmac_adapter *halmac_adapter,
	enum halmac_cmd_process_status *process_status, u8 *data, u32 *size)
{
	struct halmac_power_tracking_state_set *power_tracking_state_set =
		&halmac_adapter->halmac_state.power_tracking_set;
	;

	*process_status = power_tracking_state_set->process_status;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_query_psd_status_88xx(struct halmac_adapter *halmac_adapter,
			     enum halmac_cmd_process_status *process_status,
			     u8 *data, u32 *size)
{
	void *driver_adapter = NULL;
	struct halmac_psd_state_set *psd_set =
		&halmac_adapter->halmac_state.psd_set;

	driver_adapter = halmac_adapter->driver_adapter;

	*process_status = psd_set->process_status;

	if (!data)
		return HALMAC_RET_NULL_POINTER;

	if (!size)
		return HALMAC_RET_NULL_POINTER;

	if (*process_status == HALMAC_CMD_PROCESS_DONE) {
		if (*size < psd_set->data_size) {
			*size = psd_set->data_size;
			return HALMAC_RET_BUFFER_TOO_SMALL;
		}

		*size = psd_set->data_size;
		memcpy(data, psd_set->data, *size);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_verify_io_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 value8, wvalue8;
	u32 value32, value32_2, wvalue32;
	u32 halmac_offset;
	void *driver_adapter = NULL;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		halmac_offset = REG_PAGE5_DUMMY;
		if ((halmac_offset & 0xFFFF0000) == 0)
			halmac_offset |= WLAN_IOREG_OFFSET;

		ret_status = halmac_convert_to_sdio_bus_offset_88xx(
			halmac_adapter, &halmac_offset);

		/* Verify CMD52 R/W */
		wvalue8 = 0xab;
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset,
					  wvalue8);

		value8 =
			PLATFORM_SDIO_CMD52_READ(driver_adapter, halmac_offset);

		if (value8 != wvalue8) {
			pr_err("cmd52 r/w fail write = %X read = %X\n", wvalue8,
			       value8);
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		} else {
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "cmd52 r/w ok\n");
		}

		/* Verify CMD53 R/W */
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset, 0xaa);
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset + 1,
					  0xbb);
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset + 2,
					  0xcc);
		PLATFORM_SDIO_CMD52_WRITE(driver_adapter, halmac_offset + 3,
					  0xdd);

		value32 = PLATFORM_SDIO_CMD53_READ_32(driver_adapter,
						      halmac_offset);

		if (value32 != 0xddccbbaa) {
			pr_err("cmd53 r fail : read = %X\n", value32);
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		} else {
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "cmd53 r ok\n");
		}

		wvalue32 = 0x11223344;
		PLATFORM_SDIO_CMD53_WRITE_32(driver_adapter, halmac_offset,
					     wvalue32);

		value32 = PLATFORM_SDIO_CMD53_READ_32(driver_adapter,
						      halmac_offset);

		if (value32 != wvalue32) {
			pr_err("cmd53 w fail\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		} else {
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "cmd53 w ok\n");
		}

		value32 = PLATFORM_SDIO_CMD53_READ_32(
			driver_adapter,
			halmac_offset + 2); /* value32 should be 0x33441122 */

		wvalue32 = 0x11225566;
		PLATFORM_SDIO_CMD53_WRITE_32(driver_adapter, halmac_offset,
					     wvalue32);

		value32_2 = PLATFORM_SDIO_CMD53_READ_32(
			driver_adapter,
			halmac_offset + 2); /* value32 should be 0x55661122 */
		if (value32_2 == value32) {
			pr_err("cmd52 is used for HAL_SDIO_CMD53_READ_32\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		} else {
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "cmd53 is correctly used\n");
		}
	} else {
		wvalue32 = 0x77665511;
		PLATFORM_REG_WRITE_32(driver_adapter, REG_PAGE5_DUMMY,
				      wvalue32);

		value32 = PLATFORM_REG_READ_32(driver_adapter, REG_PAGE5_DUMMY);
		if (value32 != wvalue32) {
			pr_err("reg rw\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		} else {
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "reg rw ok\n");
		}
	}

	return ret_status;
}

enum halmac_ret_status
halmac_verify_send_rsvd_page_88xx(struct halmac_adapter *halmac_adapter)
{
	u8 *rsvd_buf = NULL;
	u8 *rsvd_page = NULL;
	u32 i;
	u32 h2c_pkt_verify_size = 64, h2c_pkt_verify_payload = 0xab;
	void *driver_adapter = NULL;
	enum halmac_ret_status ret_status = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;

	rsvd_buf = kzalloc(h2c_pkt_verify_size, GFP_KERNEL);

	if (!rsvd_buf)
		return HALMAC_RET_MALLOC_FAIL;

	memset(rsvd_buf, (u8)h2c_pkt_verify_payload, h2c_pkt_verify_size);

	ret_status = halmac_download_rsvd_page_88xx(halmac_adapter, rsvd_buf,
						    h2c_pkt_verify_size);

	if (ret_status != HALMAC_RET_SUCCESS) {
		kfree(rsvd_buf);
		return ret_status;
	}

	rsvd_page = kzalloc(h2c_pkt_verify_size +
				    halmac_adapter->hw_config_info.txdesc_size,
			    GFP_KERNEL);

	if (!rsvd_page) {
		kfree(rsvd_buf);
		return HALMAC_RET_MALLOC_FAIL;
	}

	ret_status = halmac_dump_fifo_88xx(
		halmac_adapter, HAL_FIFO_SEL_RSVD_PAGE, 0,
		h2c_pkt_verify_size +
			halmac_adapter->hw_config_info.txdesc_size,
		rsvd_page);

	if (ret_status != HALMAC_RET_SUCCESS) {
		kfree(rsvd_buf);
		kfree(rsvd_page);
		return ret_status;
	}

	for (i = 0; i < h2c_pkt_verify_size; i++) {
		if (*(rsvd_buf + i) !=
		    *(rsvd_page +
		      (i + halmac_adapter->hw_config_info.txdesc_size))) {
			pr_err("[ERR]Compare RSVD page Fail\n");
			ret_status = HALMAC_RET_PLATFORM_API_INCORRECT;
		}
	}

	kfree(rsvd_buf);
	kfree(rsvd_page);

	return ret_status;
}

void halmac_power_save_cb_88xx(void *cb_data)
{
	void *driver_adapter = NULL;
	struct halmac_adapter *halmac_adapter = (struct halmac_adapter *)NULL;

	halmac_adapter = (struct halmac_adapter *)cb_data;
	driver_adapter = halmac_adapter->driver_adapter;

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_PWR, DBG_DMESG,
			"%s\n", __func__);
}

enum halmac_ret_status
halmac_buffer_read_88xx(struct halmac_adapter *halmac_adapter, u32 offset,
			u32 size, enum hal_fifo_sel halmac_fifo_sel,
			u8 *fifo_map)
{
	u32 start_page, value_read;
	u32 i, counter = 0, residue;
	struct halmac_api *halmac_api;

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (halmac_fifo_sel == HAL_FIFO_SEL_RSVD_PAGE)
		offset = offset +
			 (halmac_adapter->txff_allocation.rsvd_pg_bndy << 7);

	start_page = offset >> 12;
	residue = offset & (4096 - 1);

	if (halmac_fifo_sel == HAL_FIFO_SEL_TX ||
	    halmac_fifo_sel == HAL_FIFO_SEL_RSVD_PAGE)
		start_page += 0x780;
	else if (halmac_fifo_sel == HAL_FIFO_SEL_RX)
		start_page += 0x700;
	else if (halmac_fifo_sel == HAL_FIFO_SEL_REPORT)
		start_page += 0x660;
	else if (halmac_fifo_sel == HAL_FIFO_SEL_LLT)
		start_page += 0x650;
	else
		return HALMAC_RET_NOT_SUPPORT;

	value_read = HALMAC_REG_READ_16(halmac_adapter, REG_PKTBUF_DBG_CTRL);

	do {
		HALMAC_REG_WRITE_16(halmac_adapter, REG_PKTBUF_DBG_CTRL,
				    (u16)(start_page | (value_read & 0xF000)));

		for (i = 0x8000 + residue; i <= 0x8FFF; i += 4) {
			*(u32 *)(fifo_map + counter) =
				HALMAC_REG_READ_32(halmac_adapter, i);
			*(u32 *)(fifo_map + counter) =
				le32_to_cpu(*(__le32 *)(fifo_map + counter));
			counter += 4;
			if (size == counter)
				goto HALMAC_BUF_READ_OK;
		}

		residue = 0;
		start_page++;
	} while (1);

HALMAC_BUF_READ_OK:
	HALMAC_REG_WRITE_16(halmac_adapter, REG_PKTBUF_DBG_CTRL,
			    (u16)value_read);

	return HALMAC_RET_SUCCESS;
}

void halmac_restore_mac_register_88xx(struct halmac_adapter *halmac_adapter,
				      struct halmac_restore_info *restore_info,
				      u32 restore_num)
{
	u8 value_length;
	u32 i;
	u32 mac_register;
	u32 mac_value;
	struct halmac_api *halmac_api;
	struct halmac_restore_info *curr_restore_info = restore_info;

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	for (i = 0; i < restore_num; i++) {
		mac_register = curr_restore_info->mac_register;
		mac_value = curr_restore_info->value;
		value_length = curr_restore_info->length;

		if (value_length == 1)
			HALMAC_REG_WRITE_8(halmac_adapter, mac_register,
					   (u8)mac_value);
		else if (value_length == 2)
			HALMAC_REG_WRITE_16(halmac_adapter, mac_register,
					    (u16)mac_value);
		else if (value_length == 4)
			HALMAC_REG_WRITE_32(halmac_adapter, mac_register,
					    mac_value);

		curr_restore_info++;
	}
}

void halmac_api_record_id_88xx(struct halmac_adapter *halmac_adapter,
			       enum halmac_api_id api_id)
{
}

enum halmac_ret_status
halmac_set_usb_mode_88xx(struct halmac_adapter *halmac_adapter,
			 enum halmac_usb_mode usb_mode)
{
	u32 usb_temp;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	enum halmac_usb_mode current_usb_mode;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	current_usb_mode =
		HALMAC_REG_READ_8(halmac_adapter, REG_SYS_CFG2 + 3) == 0x20 ?
			HALMAC_USB_MODE_U3 :
			HALMAC_USB_MODE_U2;

	/*check if HW supports usb2_usb3 swtich*/
	usb_temp = HALMAC_REG_READ_32(halmac_adapter, REG_PAD_CTRL2);
	if (!BIT_GET_USB23_SW_MODE_V1(usb_temp) &&
	    !(usb_temp & BIT_USB3_USB2_TRANSITION)) {
		pr_err("HALMAC_HW_USB_MODE usb mode HW unsupport\n");
		return HALMAC_RET_USB2_3_SWITCH_UNSUPPORT;
	}

	if (usb_mode == current_usb_mode) {
		pr_err("HALMAC_HW_USB_MODE usb mode unchange\n");
		return HALMAC_RET_USB_MODE_UNCHANGE;
	}

	usb_temp &= ~(BIT_USB23_SW_MODE_V1(0x3));

	if (usb_mode == HALMAC_USB_MODE_U2) {
		/* usb3 to usb2 */
		HALMAC_REG_WRITE_32(
			halmac_adapter, REG_PAD_CTRL2,
			usb_temp | BIT_USB23_SW_MODE_V1(HALMAC_USB_MODE_U2) |
				BIT_RSM_EN_V1);
	} else {
		/* usb2 to usb3 */
		HALMAC_REG_WRITE_32(
			halmac_adapter, REG_PAD_CTRL2,
			usb_temp | BIT_USB23_SW_MODE_V1(HALMAC_USB_MODE_U3) |
				BIT_RSM_EN_V1);
	}

	HALMAC_REG_WRITE_8(halmac_adapter, REG_PAD_CTRL2 + 1,
			   4); /* set counter down timer 4x64 ms */
	HALMAC_REG_WRITE_16(
		halmac_adapter, REG_SYS_PW_CTRL,
		HALMAC_REG_READ_16(halmac_adapter, REG_SYS_PW_CTRL) |
			BIT_APFM_OFFMAC);
	usleep_range(1000, 1100);
	HALMAC_REG_WRITE_32(halmac_adapter, REG_PAD_CTRL2,
			    HALMAC_REG_READ_32(halmac_adapter, REG_PAD_CTRL2) |
				    BIT_NO_PDN_CHIPOFF_V1);

	return HALMAC_RET_SUCCESS;
}

void halmac_enable_bb_rf_88xx(struct halmac_adapter *halmac_adapter, u8 enable)
{
	u8 value8;
	u32 value32;
	struct halmac_api *halmac_api;

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (enable == 1) {
		value8 = HALMAC_REG_READ_8(halmac_adapter, REG_SYS_FUNC_EN);
		value8 = value8 | BIT(0) | BIT(1);
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SYS_FUNC_EN, value8);

		value8 = HALMAC_REG_READ_8(halmac_adapter, REG_RF_CTRL);
		value8 = value8 | BIT(0) | BIT(1) | BIT(2);
		HALMAC_REG_WRITE_8(halmac_adapter, REG_RF_CTRL, value8);

		value32 = HALMAC_REG_READ_32(halmac_adapter, REG_WLRF1);
		value32 = value32 | BIT(24) | BIT(25) | BIT(26);
		HALMAC_REG_WRITE_32(halmac_adapter, REG_WLRF1, value32);
	} else {
		value8 = HALMAC_REG_READ_8(halmac_adapter, REG_SYS_FUNC_EN);
		value8 = value8 & (~(BIT(0) | BIT(1)));
		HALMAC_REG_WRITE_8(halmac_adapter, REG_SYS_FUNC_EN, value8);

		value8 = HALMAC_REG_READ_8(halmac_adapter, REG_RF_CTRL);
		value8 = value8 & (~(BIT(0) | BIT(1) | BIT(2)));
		HALMAC_REG_WRITE_8(halmac_adapter, REG_RF_CTRL, value8);

		value32 = HALMAC_REG_READ_32(halmac_adapter, REG_WLRF1);
		value32 = value32 & (~(BIT(24) | BIT(25) | BIT(26)));
		HALMAC_REG_WRITE_32(halmac_adapter, REG_WLRF1, value32);
	}
}

void halmac_config_sdio_tx_page_threshold_88xx(
	struct halmac_adapter *halmac_adapter,
	struct halmac_tx_page_threshold_info *threshold_info)
{
	struct halmac_api *halmac_api;

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	switch (threshold_info->dma_queue_sel) {
	case HALMAC_MAP2_HQ:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_TQPNT1,
				    threshold_info->threshold);
		break;
	case HALMAC_MAP2_NQ:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_TQPNT2,
				    threshold_info->threshold);
		break;
	case HALMAC_MAP2_LQ:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_TQPNT3,
				    threshold_info->threshold);
		break;
	case HALMAC_MAP2_EXQ:
		HALMAC_REG_WRITE_32(halmac_adapter, REG_TQPNT4,
				    threshold_info->threshold);
		break;
	default:
		break;
	}
}

void halmac_config_ampdu_88xx(struct halmac_adapter *halmac_adapter,
			      struct halmac_ampdu_config *ampdu_config)
{
	struct halmac_api *halmac_api;

	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_8(halmac_adapter, REG_PROT_MODE_CTRL + 2,
			   ampdu_config->max_agg_num);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_PROT_MODE_CTRL + 3,
			   ampdu_config->max_agg_num);
};

enum halmac_ret_status
halmac_check_oqt_88xx(struct halmac_adapter *halmac_adapter, u32 tx_agg_num,
		      u8 *halmac_buf)
{
	u32 counter = 10;

	/*S0, S1 are not allowed to use, 0x4E4[0] should be 0. Soar 20160323*/
	/*no need to check non_ac_oqt_number. HI and MGQ blocked will cause
	 *protocal issue before H_OQT being full
	 */
	switch ((enum halmac_queue_select)GET_TX_DESC_QSEL(halmac_buf)) {
	case HALMAC_QUEUE_SELECT_VO:
	case HALMAC_QUEUE_SELECT_VO_V2:
	case HALMAC_QUEUE_SELECT_VI:
	case HALMAC_QUEUE_SELECT_VI_V2:
	case HALMAC_QUEUE_SELECT_BE:
	case HALMAC_QUEUE_SELECT_BE_V2:
	case HALMAC_QUEUE_SELECT_BK:
	case HALMAC_QUEUE_SELECT_BK_V2:
		counter = 10;
		do {
			if (halmac_adapter->sdio_free_space.ac_empty > 0) {
				halmac_adapter->sdio_free_space.ac_empty -= 1;
				break;
			}

			if (halmac_adapter->sdio_free_space.ac_oqt_number >=
			    tx_agg_num) {
				halmac_adapter->sdio_free_space.ac_oqt_number -=
					(u8)tx_agg_num;
				break;
			}

			halmac_update_oqt_free_space_88xx(halmac_adapter);

			counter--;
			if (counter == 0)
				return HALMAC_RET_OQT_NOT_ENOUGH;
		} while (1);
		break;
	default:
		break;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_rqpn_parser_88xx(struct halmac_adapter *halmac_adapter,
			enum halmac_trx_mode halmac_trx_mode,
			struct halmac_rqpn_ *rqpn_table)
{
	u8 search_flag;
	u32 i;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	search_flag = 0;
	for (i = 0; i < HALMAC_TRX_MODE_MAX; i++) {
		if (halmac_trx_mode == rqpn_table[i].mode) {
			halmac_adapter
				->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] =
				rqpn_table[i].dma_map_vo;
			halmac_adapter
				->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] =
				rqpn_table[i].dma_map_vi;
			halmac_adapter
				->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] =
				rqpn_table[i].dma_map_be;
			halmac_adapter
				->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] =
				rqpn_table[i].dma_map_bk;
			halmac_adapter
				->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] =
				rqpn_table[i].dma_map_mg;
			halmac_adapter
				->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] =
				rqpn_table[i].dma_map_hi;
			search_flag = 1;
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "%s done\n", __func__);
			break;
		}
	}

	if (search_flag == 0) {
		pr_err("HALMAC_RET_TRX_MODE_NOT_SUPPORT 1 switch case not support\n");
		return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_pg_num_parser_88xx(struct halmac_adapter *halmac_adapter,
			  enum halmac_trx_mode halmac_trx_mode,
			  struct halmac_pg_num_ *pg_num_table)
{
	u8 search_flag;
	u16 HPQ_num = 0, lpq_nnum = 0, NPQ_num = 0, GAPQ_num = 0;
	u16 EXPQ_num = 0, PUBQ_num = 0;
	u32 i = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	search_flag = 0;
	for (i = 0; i < HALMAC_TRX_MODE_MAX; i++) {
		if (halmac_trx_mode == pg_num_table[i].mode) {
			HPQ_num = pg_num_table[i].hq_num;
			lpq_nnum = pg_num_table[i].lq_num;
			NPQ_num = pg_num_table[i].nq_num;
			EXPQ_num = pg_num_table[i].exq_num;
			GAPQ_num = pg_num_table[i].gap_num;
			PUBQ_num = halmac_adapter->txff_allocation.ac_q_pg_num -
				   HPQ_num - lpq_nnum - NPQ_num - EXPQ_num -
				   GAPQ_num;
			search_flag = 1;
			HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_INIT,
					DBG_DMESG, "%s done\n", __func__);
			break;
		}
	}

	if (search_flag == 0) {
		pr_err("HALMAC_RET_TRX_MODE_NOT_SUPPORT 1 switch case not support\n");
		return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
	}

	if (halmac_adapter->txff_allocation.ac_q_pg_num <
	    HPQ_num + lpq_nnum + NPQ_num + EXPQ_num + GAPQ_num)
		return HALMAC_RET_CFG_TXFIFO_PAGE_FAIL;

	halmac_adapter->txff_allocation.high_queue_pg_num = HPQ_num;
	halmac_adapter->txff_allocation.low_queue_pg_num = lpq_nnum;
	halmac_adapter->txff_allocation.normal_queue_pg_num = NPQ_num;
	halmac_adapter->txff_allocation.extra_queue_pg_num = EXPQ_num;
	halmac_adapter->txff_allocation.pub_queue_pg_num = PUBQ_num;

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_parse_intf_phy_88xx(struct halmac_adapter *halmac_adapter,
			   struct halmac_intf_phy_para_ *intf_phy_para,
			   enum halmac_intf_phy_platform platform,
			   enum hal_intf_phy intf_phy)
{
	u16 value;
	u16 curr_cut;
	u16 offset;
	u16 ip_sel;
	struct halmac_intf_phy_para_ *curr_phy_para;
	struct halmac_api *halmac_api;
	void *driver_adapter = NULL;
	u8 result = HALMAC_RET_SUCCESS;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	switch (halmac_adapter->chip_version) {
	case HALMAC_CHIP_VER_A_CUT:
		curr_cut = (u16)HALMAC_INTF_PHY_CUT_A;
		break;
	case HALMAC_CHIP_VER_B_CUT:
		curr_cut = (u16)HALMAC_INTF_PHY_CUT_B;
		break;
	case HALMAC_CHIP_VER_C_CUT:
		curr_cut = (u16)HALMAC_INTF_PHY_CUT_C;
		break;
	case HALMAC_CHIP_VER_D_CUT:
		curr_cut = (u16)HALMAC_INTF_PHY_CUT_D;
		break;
	case HALMAC_CHIP_VER_E_CUT:
		curr_cut = (u16)HALMAC_INTF_PHY_CUT_E;
		break;
	case HALMAC_CHIP_VER_F_CUT:
		curr_cut = (u16)HALMAC_INTF_PHY_CUT_F;
		break;
	case HALMAC_CHIP_VER_TEST:
		curr_cut = (u16)HALMAC_INTF_PHY_CUT_TESTCHIP;
		break;
	default:
		return HALMAC_RET_FAIL;
	}

	for (curr_phy_para = intf_phy_para;; curr_phy_para++) {
		if (!(curr_phy_para->cut & curr_cut) ||
		    !(curr_phy_para->plaform & (u16)platform))
			continue;

		offset = curr_phy_para->offset;
		value = curr_phy_para->value;
		ip_sel = curr_phy_para->ip_sel;

		if (offset == 0xFFFF)
			break;

		if (ip_sel == HALMAC_IP_SEL_MAC) {
			HALMAC_REG_WRITE_8(halmac_adapter, (u32)offset,
					   (u8)value);
		} else if (intf_phy == HAL_INTF_PHY_USB2) {
			result = halmac_usbphy_write_88xx(halmac_adapter,
							  (u8)offset, value,
							  HAL_INTF_PHY_USB2);

			if (result != HALMAC_RET_SUCCESS)
				pr_err("[ERR]Write USB2PHY fail!\n");

		} else if (intf_phy == HAL_INTF_PHY_USB3) {
			result = halmac_usbphy_write_88xx(halmac_adapter,
							  (u8)offset, value,
							  HAL_INTF_PHY_USB3);

			if (result != HALMAC_RET_SUCCESS)
				pr_err("[ERR]Write USB3PHY fail!\n");

		} else if (intf_phy == HAL_INTF_PHY_PCIE_GEN1) {
			if (ip_sel == HALMAC_IP_SEL_INTF_PHY)
				result = halmac_mdio_write_88xx(
					halmac_adapter, (u8)offset, value,
					HAL_INTF_PHY_PCIE_GEN1);
			else
				result = halmac_dbi_write8_88xx(
					halmac_adapter, offset, (u8)value);

			if (result != HALMAC_RET_SUCCESS)
				pr_err("[ERR]MDIO write GEN1 fail!\n");

		} else if (intf_phy == HAL_INTF_PHY_PCIE_GEN2) {
			if (ip_sel == HALMAC_IP_SEL_INTF_PHY)
				result = halmac_mdio_write_88xx(
					halmac_adapter, (u8)offset, value,
					HAL_INTF_PHY_PCIE_GEN2);
			else
				result = halmac_dbi_write8_88xx(
					halmac_adapter, offset, (u8)value);

			if (result != HALMAC_RET_SUCCESS)
				pr_err("[ERR]MDIO write GEN2 fail!\n");
		} else {
			pr_err("[ERR]Parse intf phy cfg error!\n");
		}
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_dbi_write32_88xx(struct halmac_adapter *halmac_adapter, u16 addr,
			u32 data)
{
	u8 tmp_u1b = 0;
	u32 count = 0;
	u16 write_addr = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_32(halmac_adapter, REG_DBI_WDATA_V1, data);

	write_addr = ((addr & 0x0ffc) | (0x000F << 12));
	HALMAC_REG_WRITE_16(halmac_adapter, REG_DBI_FLAG_V1, write_addr);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_DBI, DBG_DMESG,
			"WriteAddr = %x\n", write_addr);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_DBI_FLAG_V1 + 2, 0x01);
	tmp_u1b = HALMAC_REG_READ_8(halmac_adapter, REG_DBI_FLAG_V1 + 2);

	count = 20;
	while (tmp_u1b && count != 0) {
		udelay(10);
		tmp_u1b =
			HALMAC_REG_READ_8(halmac_adapter, REG_DBI_FLAG_V1 + 2);
		count--;
	}

	if (tmp_u1b) {
		pr_err("DBI write fail!\n");
		return HALMAC_RET_FAIL;
	} else {
		return HALMAC_RET_SUCCESS;
	}
}

u32 halmac_dbi_read32_88xx(struct halmac_adapter *halmac_adapter, u16 addr)
{
	u16 read_addr = addr & 0x0ffc;
	u8 tmp_u1b = 0;
	u32 count = 0;
	u32 ret = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_16(halmac_adapter, REG_DBI_FLAG_V1, read_addr);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_DBI_FLAG_V1 + 2, 0x2);
	tmp_u1b = HALMAC_REG_READ_8(halmac_adapter, REG_DBI_FLAG_V1 + 2);

	count = 20;
	while (tmp_u1b && count != 0) {
		udelay(10);
		tmp_u1b =
			HALMAC_REG_READ_8(halmac_adapter, REG_DBI_FLAG_V1 + 2);
		count--;
	}

	if (tmp_u1b) {
		ret = 0xFFFF;
		pr_err("DBI read fail!\n");
	} else {
		ret = HALMAC_REG_READ_32(halmac_adapter, REG_DBI_RDATA_V1);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_DBI, DBG_DMESG,
				"Read Value = %x\n", ret);
	}

	return ret;
}

enum halmac_ret_status
halmac_dbi_write8_88xx(struct halmac_adapter *halmac_adapter, u16 addr, u8 data)
{
	u8 tmp_u1b = 0;
	u32 count = 0;
	u16 write_addr = 0;
	u16 remainder = addr & (4 - 1);
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_8(halmac_adapter, REG_DBI_WDATA_V1 + remainder, data);

	write_addr = ((addr & 0x0ffc) | (BIT(0) << (remainder + 12)));

	HALMAC_REG_WRITE_16(halmac_adapter, REG_DBI_FLAG_V1, write_addr);

	HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_DBI, DBG_DMESG,
			"WriteAddr = %x\n", write_addr);

	HALMAC_REG_WRITE_8(halmac_adapter, REG_DBI_FLAG_V1 + 2, 0x01);

	tmp_u1b = HALMAC_REG_READ_8(halmac_adapter, REG_DBI_FLAG_V1 + 2);

	count = 20;
	while (tmp_u1b && count != 0) {
		udelay(10);
		tmp_u1b =
			HALMAC_REG_READ_8(halmac_adapter, REG_DBI_FLAG_V1 + 2);
		count--;
	}

	if (tmp_u1b) {
		pr_err("DBI write fail!\n");
		return HALMAC_RET_FAIL;
	} else {
		return HALMAC_RET_SUCCESS;
	}
}

u8 halmac_dbi_read8_88xx(struct halmac_adapter *halmac_adapter, u16 addr)
{
	u16 read_addr = addr & 0x0ffc;
	u8 tmp_u1b = 0;
	u32 count = 0;
	u8 ret = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_16(halmac_adapter, REG_DBI_FLAG_V1, read_addr);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_DBI_FLAG_V1 + 2, 0x2);

	tmp_u1b = HALMAC_REG_READ_8(halmac_adapter, REG_DBI_FLAG_V1 + 2);

	count = 20;
	while (tmp_u1b && count != 0) {
		udelay(10);
		tmp_u1b =
			HALMAC_REG_READ_8(halmac_adapter, REG_DBI_FLAG_V1 + 2);
		count--;
	}

	if (tmp_u1b) {
		ret = 0xFF;
		pr_err("DBI read fail!\n");
	} else {
		ret = HALMAC_REG_READ_8(halmac_adapter,
					REG_DBI_RDATA_V1 + (addr & (4 - 1)));
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_DBI, DBG_DMESG,
				"Read Value = %x\n", ret);
	}

	return ret;
}

enum halmac_ret_status
halmac_mdio_write_88xx(struct halmac_adapter *halmac_adapter, u8 addr, u16 data,
		       u8 speed)
{
	u8 tmp_u1b = 0;
	u32 count = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	u8 real_addr = 0;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	HALMAC_REG_WRITE_16(halmac_adapter, REG_MDIO_V1, data);

	/* address : 5bit */
	real_addr = (addr & 0x1F);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG, real_addr);

	if (speed == HAL_INTF_PHY_PCIE_GEN1) {
		/* GEN1 page 0 */
		if (addr < 0x20) {
			/* select MDIO PHY Addr : reg 0x3F8[28:24]=5'b00 */
			HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG + 3,
					   0x00);

			/* GEN1 page 1 */
		} else {
			/* select MDIO PHY Addr : reg 0x3F8[28:24]=5'b01 */
			HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG + 3,
					   0x01);
		}

	} else if (speed == HAL_INTF_PHY_PCIE_GEN2) {
		/* GEN2 page 0 */
		if (addr < 0x20) {
			/* select MDIO PHY Addr : reg 0x3F8[28:24]=5'b10 */
			HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG + 3,
					   0x02);

			/* GEN2 page 1 */
		} else {
			/* select MDIO PHY Addr : reg 0x3F8[28:24]=5'b11 */
			HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG + 3,
					   0x03);
		}
	} else {
		pr_err("Error Speed !\n");
	}

	HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG,
			   HALMAC_REG_READ_8(halmac_adapter, REG_PCIE_MIX_CFG) |
				   BIT_MDIO_WFLAG_V1);

	tmp_u1b = HALMAC_REG_READ_8(halmac_adapter, REG_PCIE_MIX_CFG) &
		  BIT_MDIO_WFLAG_V1;
	count = 20;

	while (tmp_u1b && count != 0) {
		udelay(10);
		tmp_u1b = HALMAC_REG_READ_8(halmac_adapter, REG_PCIE_MIX_CFG) &
			  BIT_MDIO_WFLAG_V1;
		count--;
	}

	if (tmp_u1b) {
		pr_err("MDIO write fail!\n");
		return HALMAC_RET_FAIL;
	} else {
		return HALMAC_RET_SUCCESS;
	}
}

u16 halmac_mdio_read_88xx(struct halmac_adapter *halmac_adapter, u8 addr,
			  u8 speed

			  )
{
	u16 ret = 0;
	u8 tmp_u1b = 0;
	u32 count = 0;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	u8 real_addr = 0;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	/* address : 5bit */
	real_addr = (addr & 0x1F);
	HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG, real_addr);

	if (speed == HAL_INTF_PHY_PCIE_GEN1) {
		/* GEN1 page 0 */
		if (addr < 0x20) {
			/* select MDIO PHY Addr : reg 0x3F8[28:24]=5'b00 */
			HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG + 3,
					   0x00);

			/* GEN1 page 1 */
		} else {
			/* select MDIO PHY Addr : reg 0x3F8[28:24]=5'b01 */
			HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG + 3,
					   0x01);
		}

	} else if (speed == HAL_INTF_PHY_PCIE_GEN2) {
		/* GEN2 page 0 */
		if (addr < 0x20) {
			/* select MDIO PHY Addr : reg 0x3F8[28:24]=5'b10 */
			HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG + 3,
					   0x02);

			/* GEN2 page 1 */
		} else {
			/* select MDIO PHY Addr : reg 0x3F8[28:24]=5'b11 */
			HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG + 3,
					   0x03);
		}
	} else {
		pr_err("Error Speed !\n");
	}

	HALMAC_REG_WRITE_8(halmac_adapter, REG_PCIE_MIX_CFG,
			   HALMAC_REG_READ_8(halmac_adapter, REG_PCIE_MIX_CFG) |
				   BIT_MDIO_RFLAG_V1);

	tmp_u1b = HALMAC_REG_READ_8(halmac_adapter, REG_PCIE_MIX_CFG) &
		  BIT_MDIO_RFLAG_V1;
	count = 20;

	while (tmp_u1b && count != 0) {
		udelay(10);
		tmp_u1b = HALMAC_REG_READ_8(halmac_adapter, REG_PCIE_MIX_CFG) &
			  BIT_MDIO_RFLAG_V1;
		count--;
	}

	if (tmp_u1b) {
		ret = 0xFFFF;
		pr_err("MDIO read fail!\n");

	} else {
		ret = HALMAC_REG_READ_16(halmac_adapter, REG_MDIO_V1 + 2);
		HALMAC_RT_TRACE(driver_adapter, HALMAC_MSG_MDIO, DBG_DMESG,
				"Read Value = %x\n", ret);
	}

	return ret;
}

enum halmac_ret_status
halmac_usbphy_write_88xx(struct halmac_adapter *halmac_adapter, u8 addr,
			 u16 data, u8 speed)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (speed == HAL_INTF_PHY_USB3) {
		HALMAC_REG_WRITE_8(halmac_adapter, 0xff0d, (u8)data);
		HALMAC_REG_WRITE_8(halmac_adapter, 0xff0e, (u8)(data >> 8));
		HALMAC_REG_WRITE_8(halmac_adapter, 0xff0c, addr | BIT(7));
	} else if (speed == HAL_INTF_PHY_USB2) {
		HALMAC_REG_WRITE_8(halmac_adapter, 0xfe41, (u8)data);
		HALMAC_REG_WRITE_8(halmac_adapter, 0xfe40, addr);
		HALMAC_REG_WRITE_8(halmac_adapter, 0xfe42, 0x81);
	} else {
		pr_err("[ERR]Error USB Speed !\n");
		return HALMAC_RET_NOT_SUPPORT;
	}

	return HALMAC_RET_SUCCESS;
}

u16 halmac_usbphy_read_88xx(struct halmac_adapter *halmac_adapter, u8 addr,
			    u8 speed)
{
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;
	u16 value = 0;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (speed == HAL_INTF_PHY_USB3) {
		HALMAC_REG_WRITE_8(halmac_adapter, 0xff0c, addr | BIT(6));
		value = (u16)(HALMAC_REG_READ_32(halmac_adapter, 0xff0c) >> 8);
	} else if (speed == HAL_INTF_PHY_USB2) {
		if ((addr >= 0xE0) /*&& (addr <= 0xFF)*/)
			addr -= 0x20;
		if ((addr >= 0xC0) && (addr <= 0xDF)) {
			HALMAC_REG_WRITE_8(halmac_adapter, 0xfe40, addr);
			HALMAC_REG_WRITE_8(halmac_adapter, 0xfe42, 0x81);
			value = HALMAC_REG_READ_8(halmac_adapter, 0xfe43);
		} else {
			pr_err("[ERR]Error USB2PHY offset!\n");
			return HALMAC_RET_NOT_SUPPORT;
		}
	} else {
		pr_err("[ERR]Error USB Speed !\n");
		return HALMAC_RET_NOT_SUPPORT;
	}

	return value;
}
