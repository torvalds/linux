// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_common.h"

/**
 * ice_aq_read_nvm
 * @hw: pointer to the HW struct
 * @module_typeid: module pointer location in words from the NVM beginning
 * @offset: byte offset from the module beginning
 * @length: length of the section to be read (in bytes from the offset)
 * @data: command buffer (size [bytes] = length)
 * @last_command: tells if this is the last command in a series
 * @read_shadow_ram: tell if this is a shadow RAM read
 * @cd: pointer to command details structure or NULL
 *
 * Read the NVM using the admin queue commands (0x0701)
 */
static enum ice_status
ice_aq_read_nvm(struct ice_hw *hw, u16 module_typeid, u32 offset, u16 length,
		void *data, bool last_command, bool read_shadow_ram,
		struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	struct ice_aqc_nvm *cmd;

	cmd = &desc.params.nvm;

	if (offset > ICE_AQC_NVM_MAX_OFFSET)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_read);

	if (!read_shadow_ram && module_typeid == ICE_AQC_NVM_START_POINT)
		cmd->cmd_flags |= ICE_AQC_NVM_FLASH_ONLY;

	/* If this is the last command in a series, set the proper flag. */
	if (last_command)
		cmd->cmd_flags |= ICE_AQC_NVM_LAST_CMD;
	cmd->module_typeid = cpu_to_le16(module_typeid);
	cmd->offset_low = cpu_to_le16(offset & 0xFFFF);
	cmd->offset_high = (offset >> 16) & 0xFF;
	cmd->length = cpu_to_le16(length);

	return ice_aq_send_cmd(hw, &desc, data, length, cd);
}

/**
 * ice_read_flat_nvm - Read portion of NVM by flat offset
 * @hw: pointer to the HW struct
 * @offset: offset from beginning of NVM
 * @length: (in) number of bytes to read; (out) number of bytes actually read
 * @data: buffer to return data in (sized to fit the specified length)
 * @read_shadow_ram: if true, read from shadow RAM instead of NVM
 *
 * Reads a portion of the NVM, as a flat memory space. This function correctly
 * breaks read requests across Shadow RAM sectors and ensures that no single
 * read request exceeds the maximum 4KB read for a single AdminQ command.
 *
 * Returns a status code on failure. Note that the data pointer may be
 * partially updated if some reads succeed before a failure.
 */
enum ice_status
ice_read_flat_nvm(struct ice_hw *hw, u32 offset, u32 *length, u8 *data,
		  bool read_shadow_ram)
{
	enum ice_status status;
	u32 inlen = *length;
	u32 bytes_read = 0;
	bool last_cmd;

	*length = 0;

	/* Verify the length of the read if this is for the Shadow RAM */
	if (read_shadow_ram && ((offset + inlen) > (hw->nvm.sr_words * 2u))) {
		ice_debug(hw, ICE_DBG_NVM, "NVM error: requested offset is beyond Shadow RAM limit\n");
		return ICE_ERR_PARAM;
	}

	do {
		u32 read_size, sector_offset;

		/* ice_aq_read_nvm cannot read more than 4KB at a time.
		 * Additionally, a read from the Shadow RAM may not cross over
		 * a sector boundary. Conveniently, the sector size is also
		 * 4KB.
		 */
		sector_offset = offset % ICE_AQ_MAX_BUF_LEN;
		read_size = min_t(u32, ICE_AQ_MAX_BUF_LEN - sector_offset,
				  inlen - bytes_read);

		last_cmd = !(bytes_read + read_size < inlen);

		status = ice_aq_read_nvm(hw, ICE_AQC_NVM_START_POINT,
					 offset, read_size,
					 data + bytes_read, last_cmd,
					 read_shadow_ram, NULL);
		if (status)
			break;

		bytes_read += read_size;
		offset += read_size;
	} while (!last_cmd);

	*length = bytes_read;
	return status;
}

/**
 * ice_aq_update_nvm
 * @hw: pointer to the HW struct
 * @module_typeid: module pointer location in words from the NVM beginning
 * @offset: byte offset from the module beginning
 * @length: length of the section to be written (in bytes from the offset)
 * @data: command buffer (size [bytes] = length)
 * @last_command: tells if this is the last command in a series
 * @command_flags: command parameters
 * @cd: pointer to command details structure or NULL
 *
 * Update the NVM using the admin queue commands (0x0703)
 */
enum ice_status
ice_aq_update_nvm(struct ice_hw *hw, u16 module_typeid, u32 offset,
		  u16 length, void *data, bool last_command, u8 command_flags,
		  struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	struct ice_aqc_nvm *cmd;

	cmd = &desc.params.nvm;

	/* In offset the highest byte must be zeroed. */
	if (offset & 0xFF000000)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_write);

	cmd->cmd_flags |= command_flags;

	/* If this is the last command in a series, set the proper flag. */
	if (last_command)
		cmd->cmd_flags |= ICE_AQC_NVM_LAST_CMD;
	cmd->module_typeid = cpu_to_le16(module_typeid);
	cmd->offset_low = cpu_to_le16(offset & 0xFFFF);
	cmd->offset_high = (offset >> 16) & 0xFF;
	cmd->length = cpu_to_le16(length);

	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	return ice_aq_send_cmd(hw, &desc, data, length, cd);
}

/**
 * ice_aq_erase_nvm
 * @hw: pointer to the HW struct
 * @module_typeid: module pointer location in words from the NVM beginning
 * @cd: pointer to command details structure or NULL
 *
 * Erase the NVM sector using the admin queue commands (0x0702)
 */
enum ice_status
ice_aq_erase_nvm(struct ice_hw *hw, u16 module_typeid, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	struct ice_aqc_nvm *cmd;

	cmd = &desc.params.nvm;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_erase);

	cmd->module_typeid = cpu_to_le16(module_typeid);
	cmd->length = cpu_to_le16(ICE_AQC_NVM_ERASE_LEN);
	cmd->offset_low = 0;
	cmd->offset_high = 0;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_read_sr_word_aq - Reads Shadow RAM via AQ
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using ice_read_flat_nvm.
 */
static enum ice_status
ice_read_sr_word_aq(struct ice_hw *hw, u16 offset, u16 *data)
{
	u32 bytes = sizeof(u16);
	enum ice_status status;
	__le16 data_local;

	/* Note that ice_read_flat_nvm takes into account the 4Kb AdminQ and
	 * Shadow RAM sector restrictions necessary when reading from the NVM.
	 */
	status = ice_read_flat_nvm(hw, offset * sizeof(u16), &bytes,
				   (__force u8 *)&data_local, true);
	if (status)
		return status;

	*data = le16_to_cpu(data_local);
	return 0;
}

/**
 * ice_acquire_nvm - Generic request for acquiring the NVM ownership
 * @hw: pointer to the HW structure
 * @access: NVM access type (read or write)
 *
 * This function will request NVM ownership.
 */
enum ice_status
ice_acquire_nvm(struct ice_hw *hw, enum ice_aq_res_access_type access)
{
	if (hw->nvm.blank_nvm_mode)
		return 0;

	return ice_acquire_res(hw, ICE_NVM_RES_ID, access, ICE_NVM_TIMEOUT);
}

/**
 * ice_release_nvm - Generic request for releasing the NVM ownership
 * @hw: pointer to the HW structure
 *
 * This function will release NVM ownership.
 */
void ice_release_nvm(struct ice_hw *hw)
{
	if (hw->nvm.blank_nvm_mode)
		return;

	ice_release_res(hw, ICE_NVM_RES_ID);
}

/**
 * ice_read_sr_word - Reads Shadow RAM word and acquire NVM if necessary
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using the ice_read_sr_word_aq.
 */
enum ice_status ice_read_sr_word(struct ice_hw *hw, u16 offset, u16 *data)
{
	enum ice_status status;

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (!status) {
		status = ice_read_sr_word_aq(hw, offset, data);
		ice_release_nvm(hw);
	}

	return status;
}

/**
 * ice_get_pfa_module_tlv - Reads sub module TLV from NVM PFA
 * @hw: pointer to hardware structure
 * @module_tlv: pointer to module TLV to return
 * @module_tlv_len: pointer to module TLV length to return
 * @module_type: module type requested
 *
 * Finds the requested sub module TLV type from the Preserved Field
 * Area (PFA) and returns the TLV pointer and length. The caller can
 * use these to read the variable length TLV value.
 */
enum ice_status
ice_get_pfa_module_tlv(struct ice_hw *hw, u16 *module_tlv, u16 *module_tlv_len,
		       u16 module_type)
{
	enum ice_status status;
	u16 pfa_len, pfa_ptr;
	u16 next_tlv;

	status = ice_read_sr_word(hw, ICE_SR_PFA_PTR, &pfa_ptr);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Preserved Field Array pointer.\n");
		return status;
	}
	status = ice_read_sr_word(hw, pfa_ptr, &pfa_len);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read PFA length.\n");
		return status;
	}
	/* Starting with first TLV after PFA length, iterate through the list
	 * of TLVs to find the requested one.
	 */
	next_tlv = pfa_ptr + 1;
	while (next_tlv < pfa_ptr + pfa_len) {
		u16 tlv_sub_module_type;
		u16 tlv_len;

		/* Read TLV type */
		status = ice_read_sr_word(hw, next_tlv, &tlv_sub_module_type);
		if (status) {
			ice_debug(hw, ICE_DBG_INIT, "Failed to read TLV type.\n");
			break;
		}
		/* Read TLV length */
		status = ice_read_sr_word(hw, next_tlv + 1, &tlv_len);
		if (status) {
			ice_debug(hw, ICE_DBG_INIT, "Failed to read TLV length.\n");
			break;
		}
		if (tlv_sub_module_type == module_type) {
			if (tlv_len) {
				*module_tlv = next_tlv;
				*module_tlv_len = tlv_len;
				return 0;
			}
			return ICE_ERR_INVAL_SIZE;
		}
		/* Check next TLV, i.e. current TLV pointer + length + 2 words
		 * (for current TLV's type and length)
		 */
		next_tlv = next_tlv + tlv_len + 2;
	}
	/* Module does not exist */
	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_read_pba_string - Reads part number string from NVM
 * @hw: pointer to hardware structure
 * @pba_num: stores the part number string from the NVM
 * @pba_num_size: part number string buffer length
 *
 * Reads the part number string from the NVM.
 */
enum ice_status
ice_read_pba_string(struct ice_hw *hw, u8 *pba_num, u32 pba_num_size)
{
	u16 pba_tlv, pba_tlv_len;
	enum ice_status status;
	u16 pba_word, pba_size;
	u16 i;

	status = ice_get_pfa_module_tlv(hw, &pba_tlv, &pba_tlv_len,
					ICE_SR_PBA_BLOCK_PTR);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read PBA Block TLV.\n");
		return status;
	}

	/* pba_size is the next word */
	status = ice_read_sr_word(hw, (pba_tlv + 2), &pba_size);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read PBA Section size.\n");
		return status;
	}

	if (pba_tlv_len < pba_size) {
		ice_debug(hw, ICE_DBG_INIT, "Invalid PBA Block TLV size.\n");
		return ICE_ERR_INVAL_SIZE;
	}

	/* Subtract one to get PBA word count (PBA Size word is included in
	 * total size)
	 */
	pba_size--;
	if (pba_num_size < (((u32)pba_size * 2) + 1)) {
		ice_debug(hw, ICE_DBG_INIT, "Buffer too small for PBA data.\n");
		return ICE_ERR_PARAM;
	}

	for (i = 0; i < pba_size; i++) {
		status = ice_read_sr_word(hw, (pba_tlv + 2 + 1) + i, &pba_word);
		if (status) {
			ice_debug(hw, ICE_DBG_INIT, "Failed to read PBA Block word %d.\n", i);
			return status;
		}

		pba_num[(i * 2)] = (pba_word >> 8) & 0xFF;
		pba_num[(i * 2) + 1] = pba_word & 0xFF;
	}
	pba_num[(pba_size * 2)] = '\0';

	return status;
}

/**
 * ice_get_orom_ver_info - Read Option ROM version information
 * @hw: pointer to the HW struct
 *
 * Read the Combo Image version data from the Boot Configuration TLV and fill
 * in the option ROM version data.
 */
static enum ice_status ice_get_orom_ver_info(struct ice_hw *hw)
{
	u16 combo_hi, combo_lo, boot_cfg_tlv, boot_cfg_tlv_len;
	struct ice_orom_info *orom = &hw->nvm.orom;
	enum ice_status status;
	u32 combo_ver;

	status = ice_get_pfa_module_tlv(hw, &boot_cfg_tlv, &boot_cfg_tlv_len,
					ICE_SR_BOOT_CFG_PTR);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read Boot Configuration Block TLV.\n");
		return status;
	}

	/* Boot Configuration Block must have length at least 2 words
	 * (Combo Image Version High and Combo Image Version Low)
	 */
	if (boot_cfg_tlv_len < 2) {
		ice_debug(hw, ICE_DBG_INIT, "Invalid Boot Configuration Block TLV size.\n");
		return ICE_ERR_INVAL_SIZE;
	}

	status = ice_read_sr_word(hw, (boot_cfg_tlv + ICE_NVM_OROM_VER_OFF),
				  &combo_hi);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read OROM_VER hi.\n");
		return status;
	}

	status = ice_read_sr_word(hw, (boot_cfg_tlv + ICE_NVM_OROM_VER_OFF + 1),
				  &combo_lo);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read OROM_VER lo.\n");
		return status;
	}

	combo_ver = ((u32)combo_hi << 16) | combo_lo;

	orom->major = (u8)((combo_ver & ICE_OROM_VER_MASK) >>
			   ICE_OROM_VER_SHIFT);
	orom->patch = (u8)(combo_ver & ICE_OROM_VER_PATCH_MASK);
	orom->build = (u16)((combo_ver & ICE_OROM_VER_BUILD_MASK) >>
			    ICE_OROM_VER_BUILD_SHIFT);

	return 0;
}

/**
 * ice_get_netlist_ver_info
 * @hw: pointer to the HW struct
 *
 * Get the netlist version information
 */
static enum ice_status ice_get_netlist_ver_info(struct ice_hw *hw)
{
	struct ice_netlist_ver_info *ver = &hw->netlist_ver;
	enum ice_status ret;
	u32 id_blk_start;
	__le16 raw_data;
	u16 data, i;
	u16 *buff;

	ret = ice_acquire_nvm(hw, ICE_RES_READ);
	if (ret)
		return ret;
	buff = kcalloc(ICE_AQC_NVM_NETLIST_ID_BLK_LEN, sizeof(*buff),
		       GFP_KERNEL);
	if (!buff) {
		ret = ICE_ERR_NO_MEMORY;
		goto exit_no_mem;
	}

	/* read module length */
	ret = ice_aq_read_nvm(hw, ICE_AQC_NVM_LINK_TOPO_NETLIST_MOD_ID,
			      ICE_AQC_NVM_LINK_TOPO_NETLIST_LEN_OFFSET * 2,
			      ICE_AQC_NVM_LINK_TOPO_NETLIST_LEN, &raw_data,
			      false, false, NULL);
	if (ret)
		goto exit_error;

	data = le16_to_cpu(raw_data);
	/* exit if length is = 0 */
	if (!data)
		goto exit_error;

	/* read node count */
	ret = ice_aq_read_nvm(hw, ICE_AQC_NVM_LINK_TOPO_NETLIST_MOD_ID,
			      ICE_AQC_NVM_NETLIST_NODE_COUNT_OFFSET * 2,
			      ICE_AQC_NVM_NETLIST_NODE_COUNT_LEN, &raw_data,
			      false, false, NULL);
	if (ret)
		goto exit_error;
	data = le16_to_cpu(raw_data) & ICE_AQC_NVM_NETLIST_NODE_COUNT_M;

	/* netlist ID block starts from offset 4 + node count * 2 */
	id_blk_start = ICE_AQC_NVM_NETLIST_ID_BLK_START_OFFSET + data * 2;

	/* read the entire netlist ID block */
	ret = ice_aq_read_nvm(hw, ICE_AQC_NVM_LINK_TOPO_NETLIST_MOD_ID,
			      id_blk_start * 2,
			      ICE_AQC_NVM_NETLIST_ID_BLK_LEN * 2, buff, false,
			      false, NULL);
	if (ret)
		goto exit_error;

	for (i = 0; i < ICE_AQC_NVM_NETLIST_ID_BLK_LEN; i++)
		buff[i] = le16_to_cpu(((__force __le16 *)buff)[i]);

	ver->major = (buff[ICE_AQC_NVM_NETLIST_ID_BLK_MAJOR_VER_HIGH] << 16) |
		buff[ICE_AQC_NVM_NETLIST_ID_BLK_MAJOR_VER_LOW];
	ver->minor = (buff[ICE_AQC_NVM_NETLIST_ID_BLK_MINOR_VER_HIGH] << 16) |
		buff[ICE_AQC_NVM_NETLIST_ID_BLK_MINOR_VER_LOW];
	ver->type = (buff[ICE_AQC_NVM_NETLIST_ID_BLK_TYPE_HIGH] << 16) |
		buff[ICE_AQC_NVM_NETLIST_ID_BLK_TYPE_LOW];
	ver->rev = (buff[ICE_AQC_NVM_NETLIST_ID_BLK_REV_HIGH] << 16) |
		buff[ICE_AQC_NVM_NETLIST_ID_BLK_REV_LOW];
	ver->cust_ver = buff[ICE_AQC_NVM_NETLIST_ID_BLK_CUST_VER];
	/* Read the left most 4 bytes of SHA */
	ver->hash = buff[ICE_AQC_NVM_NETLIST_ID_BLK_SHA_HASH + 15] << 16 |
		buff[ICE_AQC_NVM_NETLIST_ID_BLK_SHA_HASH + 14];

exit_error:
	kfree(buff);
exit_no_mem:
	ice_release_nvm(hw);
	return ret;
}

/**
 * ice_discover_flash_size - Discover the available flash size.
 * @hw: pointer to the HW struct
 *
 * The device flash could be up to 16MB in size. However, it is possible that
 * the actual size is smaller. Use bisection to determine the accessible size
 * of flash memory.
 */
static enum ice_status ice_discover_flash_size(struct ice_hw *hw)
{
	u32 min_size = 0, max_size = ICE_AQC_NVM_MAX_OFFSET + 1;
	enum ice_status status;

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (status)
		return status;

	while ((max_size - min_size) > 1) {
		u32 offset = (max_size + min_size) / 2;
		u32 len = 1;
		u8 data;

		status = ice_read_flat_nvm(hw, offset, &len, &data, false);
		if (status == ICE_ERR_AQ_ERROR &&
		    hw->adminq.sq_last_status == ICE_AQ_RC_EINVAL) {
			ice_debug(hw, ICE_DBG_NVM, "%s: New upper bound of %u bytes\n",
				  __func__, offset);
			status = 0;
			max_size = offset;
		} else if (!status) {
			ice_debug(hw, ICE_DBG_NVM, "%s: New lower bound of %u bytes\n",
				  __func__, offset);
			min_size = offset;
		} else {
			/* an unexpected error occurred */
			goto err_read_flat_nvm;
		}
	}

	ice_debug(hw, ICE_DBG_NVM, "Predicted flash size is %u bytes\n", max_size);

	hw->nvm.flash_size = max_size;

err_read_flat_nvm:
	ice_release_nvm(hw);

	return status;
}

/**
 * ice_init_nvm - initializes NVM setting
 * @hw: pointer to the HW struct
 *
 * This function reads and populates NVM settings such as Shadow RAM size,
 * max_timeout, and blank_nvm_mode
 */
enum ice_status ice_init_nvm(struct ice_hw *hw)
{
	struct ice_nvm_info *nvm = &hw->nvm;
	u16 eetrack_lo, eetrack_hi, ver;
	enum ice_status status;
	u32 fla, gens_stat;
	u8 sr_size;

	/* The SR size is stored regardless of the NVM programming mode
	 * as the blank mode may be used in the factory line.
	 */
	gens_stat = rd32(hw, GLNVM_GENS);
	sr_size = (gens_stat & GLNVM_GENS_SR_SIZE_M) >> GLNVM_GENS_SR_SIZE_S;

	/* Switching to words (sr_size contains power of 2) */
	nvm->sr_words = BIT(sr_size) * ICE_SR_WORDS_IN_1KB;

	/* Check if we are in the normal or blank NVM programming mode */
	fla = rd32(hw, GLNVM_FLA);
	if (fla & GLNVM_FLA_LOCKED_M) { /* Normal programming mode */
		nvm->blank_nvm_mode = false;
	} else {
		/* Blank programming mode */
		nvm->blank_nvm_mode = true;
		ice_debug(hw, ICE_DBG_NVM, "NVM init error: unsupported blank mode.\n");
		return ICE_ERR_NVM_BLANK_MODE;
	}

	status = ice_read_sr_word(hw, ICE_SR_NVM_DEV_STARTER_VER, &ver);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read DEV starter version.\n");
		return status;
	}
	nvm->major_ver = (ver & ICE_NVM_VER_HI_MASK) >> ICE_NVM_VER_HI_SHIFT;
	nvm->minor_ver = (ver & ICE_NVM_VER_LO_MASK) >> ICE_NVM_VER_LO_SHIFT;

	status = ice_read_sr_word(hw, ICE_SR_NVM_EETRACK_LO, &eetrack_lo);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read EETRACK lo.\n");
		return status;
	}
	status = ice_read_sr_word(hw, ICE_SR_NVM_EETRACK_HI, &eetrack_hi);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read EETRACK hi.\n");
		return status;
	}

	nvm->eetrack = (eetrack_hi << 16) | eetrack_lo;

	status = ice_discover_flash_size(hw);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "NVM init error: failed to discover flash size.\n");
		return status;
	}

	status = ice_get_orom_ver_info(hw);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read Option ROM info.\n");
		return status;
	}

	/* read the netlist version information */
	status = ice_get_netlist_ver_info(hw);
	if (status)
		ice_debug(hw, ICE_DBG_INIT, "Failed to read netlist info.\n");

	return 0;
}

/**
 * ice_nvm_validate_checksum
 * @hw: pointer to the HW struct
 *
 * Verify NVM PFA checksum validity (0x0706)
 */
enum ice_status ice_nvm_validate_checksum(struct ice_hw *hw)
{
	struct ice_aqc_nvm_checksum *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (status)
		return status;

	cmd = &desc.params.nvm_checksum;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_checksum);
	cmd->flags = ICE_AQC_NVM_CHECKSUM_VERIFY;

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
	ice_release_nvm(hw);

	if (!status)
		if (le16_to_cpu(cmd->checksum) != ICE_AQC_NVM_CHECKSUM_CORRECT)
			status = ICE_ERR_NVM_CHECKSUM;

	return status;
}

/**
 * ice_nvm_write_activate
 * @hw: pointer to the HW struct
 * @cmd_flags: NVM activate admin command bits (banks to be validated)
 *
 * Update the control word with the required banks' validity bits
 * and dumps the Shadow RAM to flash (0x0707)
 */
enum ice_status ice_nvm_write_activate(struct ice_hw *hw, u8 cmd_flags)
{
	struct ice_aqc_nvm *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.nvm;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_write_activate);

	cmd->cmd_flags = cmd_flags;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_aq_nvm_update_empr
 * @hw: pointer to the HW struct
 *
 * Update empr (0x0709). This command allows SW to
 * request an EMPR to activate new FW.
 */
enum ice_status ice_aq_nvm_update_empr(struct ice_hw *hw)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_update_empr);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/* ice_nvm_set_pkg_data
 * @hw: pointer to the HW struct
 * @del_pkg_data_flag: If is set then the current pkg_data store by FW
 *		       is deleted.
 *		       If bit is set to 1, then buffer should be size 0.
 * @data: pointer to buffer
 * @length: length of the buffer
 * @cd: pointer to command details structure or NULL
 *
 * Set package data (0x070A). This command is equivalent to the reception
 * of a PLDM FW Update GetPackageData cmd. This command should be sent
 * as part of the NVM update as the first cmd in the flow.
 */

enum ice_status
ice_nvm_set_pkg_data(struct ice_hw *hw, bool del_pkg_data_flag, u8 *data,
		     u16 length, struct ice_sq_cd *cd)
{
	struct ice_aqc_nvm_pkg_data *cmd;
	struct ice_aq_desc desc;

	if (length != 0 && !data)
		return ICE_ERR_PARAM;

	cmd = &desc.params.pkg_data;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_pkg_data);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	if (del_pkg_data_flag)
		cmd->cmd_flags |= ICE_AQC_NVM_PKG_DELETE;

	return ice_aq_send_cmd(hw, &desc, data, length, cd);
}

/* ice_nvm_pass_component_tbl
 * @hw: pointer to the HW struct
 * @data: pointer to buffer
 * @length: length of the buffer
 * @transfer_flag: parameter for determining stage of the update
 * @comp_response: a pointer to the response from the 0x070B AQC.
 * @comp_response_code: a pointer to the response code from the 0x070B AQC.
 * @cd: pointer to command details structure or NULL
 *
 * Pass component table (0x070B). This command is equivalent to the reception
 * of a PLDM FW Update PassComponentTable cmd. This command should be sent once
 * per component. It can be only sent after Set Package Data cmd and before
 * actual update. FW will assume these commands are going to be sent until
 * the TransferFlag is set to End or StartAndEnd.
 */

enum ice_status
ice_nvm_pass_component_tbl(struct ice_hw *hw, u8 *data, u16 length,
			   u8 transfer_flag, u8 *comp_response,
			   u8 *comp_response_code, struct ice_sq_cd *cd)
{
	struct ice_aqc_nvm_pass_comp_tbl *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	if (!data || !comp_response || !comp_response_code)
		return ICE_ERR_PARAM;

	cmd = &desc.params.pass_comp_tbl;

	ice_fill_dflt_direct_cmd_desc(&desc,
				      ice_aqc_opc_nvm_pass_component_tbl);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	cmd->transfer_flag = transfer_flag;
	status = ice_aq_send_cmd(hw, &desc, data, length, cd);

	if (!status) {
		*comp_response = cmd->component_response;
		*comp_response_code = cmd->component_response_code;
	}
	return status;
}
