// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include <linux/vmalloc.h>

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
static int
ice_aq_read_nvm(struct ice_hw *hw, u16 module_typeid, u32 offset, u16 length,
		void *data, bool last_command, bool read_shadow_ram,
		struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	struct ice_aqc_nvm *cmd;

	cmd = &desc.params.nvm;

	if (offset > ICE_AQC_NVM_MAX_OFFSET)
		return -EINVAL;

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
int
ice_read_flat_nvm(struct ice_hw *hw, u32 offset, u32 *length, u8 *data,
		  bool read_shadow_ram)
{
	u32 inlen = *length;
	u32 bytes_read = 0;
	bool last_cmd;
	int status;

	*length = 0;

	/* Verify the length of the read if this is for the Shadow RAM */
	if (read_shadow_ram && ((offset + inlen) > (hw->flash.sr_words * 2u))) {
		ice_debug(hw, ICE_DBG_NVM, "NVM error: requested offset is beyond Shadow RAM limit\n");
		return -EINVAL;
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
int
ice_aq_update_nvm(struct ice_hw *hw, u16 module_typeid, u32 offset,
		  u16 length, void *data, bool last_command, u8 command_flags,
		  struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	struct ice_aqc_nvm *cmd;

	cmd = &desc.params.nvm;

	/* In offset the highest byte must be zeroed. */
	if (offset & 0xFF000000)
		return -EINVAL;

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
int ice_aq_erase_nvm(struct ice_hw *hw, u16 module_typeid, struct ice_sq_cd *cd)
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
static int ice_read_sr_word_aq(struct ice_hw *hw, u16 offset, u16 *data)
{
	u32 bytes = sizeof(u16);
	__le16 data_local;
	int status;

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
int ice_acquire_nvm(struct ice_hw *hw, enum ice_aq_res_access_type access)
{
	if (hw->flash.blank_nvm_mode)
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
	if (hw->flash.blank_nvm_mode)
		return;

	ice_release_res(hw, ICE_NVM_RES_ID);
}

/**
 * ice_get_flash_bank_offset - Get offset into requested flash bank
 * @hw: pointer to the HW structure
 * @bank: whether to read from the active or inactive flash bank
 * @module: the module to read from
 *
 * Based on the module, lookup the module offset from the beginning of the
 * flash.
 *
 * Returns the flash offset. Note that a value of zero is invalid and must be
 * treated as an error.
 */
static u32 ice_get_flash_bank_offset(struct ice_hw *hw, enum ice_bank_select bank, u16 module)
{
	struct ice_bank_info *banks = &hw->flash.banks;
	enum ice_flash_bank active_bank;
	bool second_bank_active;
	u32 offset, size;

	switch (module) {
	case ICE_SR_1ST_NVM_BANK_PTR:
		offset = banks->nvm_ptr;
		size = banks->nvm_size;
		active_bank = banks->nvm_bank;
		break;
	case ICE_SR_1ST_OROM_BANK_PTR:
		offset = banks->orom_ptr;
		size = banks->orom_size;
		active_bank = banks->orom_bank;
		break;
	case ICE_SR_NETLIST_BANK_PTR:
		offset = banks->netlist_ptr;
		size = banks->netlist_size;
		active_bank = banks->netlist_bank;
		break;
	default:
		ice_debug(hw, ICE_DBG_NVM, "Unexpected value for flash module: 0x%04x\n", module);
		return 0;
	}

	switch (active_bank) {
	case ICE_1ST_FLASH_BANK:
		second_bank_active = false;
		break;
	case ICE_2ND_FLASH_BANK:
		second_bank_active = true;
		break;
	default:
		ice_debug(hw, ICE_DBG_NVM, "Unexpected value for active flash bank: %u\n",
			  active_bank);
		return 0;
	}

	/* The second flash bank is stored immediately following the first
	 * bank. Based on whether the 1st or 2nd bank is active, and whether
	 * we want the active or inactive bank, calculate the desired offset.
	 */
	switch (bank) {
	case ICE_ACTIVE_FLASH_BANK:
		return offset + (second_bank_active ? size : 0);
	case ICE_INACTIVE_FLASH_BANK:
		return offset + (second_bank_active ? 0 : size);
	}

	ice_debug(hw, ICE_DBG_NVM, "Unexpected value for flash bank selection: %u\n", bank);
	return 0;
}

/**
 * ice_read_flash_module - Read a word from one of the main NVM modules
 * @hw: pointer to the HW structure
 * @bank: which bank of the module to read
 * @module: the module to read
 * @offset: the offset into the module in bytes
 * @data: storage for the word read from the flash
 * @length: bytes of data to read
 *
 * Read data from the specified flash module. The bank parameter indicates
 * whether or not to read from the active bank or the inactive bank of that
 * module.
 *
 * The word will be read using flat NVM access, and relies on the
 * hw->flash.banks data being setup by ice_determine_active_flash_banks()
 * during initialization.
 */
static int
ice_read_flash_module(struct ice_hw *hw, enum ice_bank_select bank, u16 module,
		      u32 offset, u8 *data, u32 length)
{
	int status;
	u32 start;

	start = ice_get_flash_bank_offset(hw, bank, module);
	if (!start) {
		ice_debug(hw, ICE_DBG_NVM, "Unable to calculate flash bank offset for module 0x%04x\n",
			  module);
		return -EINVAL;
	}

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (status)
		return status;

	status = ice_read_flat_nvm(hw, start + offset, &length, data, false);

	ice_release_nvm(hw);

	return status;
}

/**
 * ice_read_nvm_module - Read from the active main NVM module
 * @hw: pointer to the HW structure
 * @bank: whether to read from active or inactive NVM module
 * @offset: offset into the NVM module to read, in words
 * @data: storage for returned word value
 *
 * Read the specified word from the active NVM module. This includes the CSS
 * header at the start of the NVM module.
 */
static int
ice_read_nvm_module(struct ice_hw *hw, enum ice_bank_select bank, u32 offset, u16 *data)
{
	__le16 data_local;
	int status;

	status = ice_read_flash_module(hw, bank, ICE_SR_1ST_NVM_BANK_PTR, offset * sizeof(u16),
				       (__force u8 *)&data_local, sizeof(u16));
	if (!status)
		*data = le16_to_cpu(data_local);

	return status;
}

/**
 * ice_read_nvm_sr_copy - Read a word from the Shadow RAM copy in the NVM bank
 * @hw: pointer to the HW structure
 * @bank: whether to read from the active or inactive NVM module
 * @offset: offset into the Shadow RAM copy to read, in words
 * @data: storage for returned word value
 *
 * Read the specified word from the copy of the Shadow RAM found in the
 * specified NVM module.
 */
static int
ice_read_nvm_sr_copy(struct ice_hw *hw, enum ice_bank_select bank, u32 offset, u16 *data)
{
	return ice_read_nvm_module(hw, bank, ICE_NVM_SR_COPY_WORD_OFFSET + offset, data);
}

/**
 * ice_read_netlist_module - Read data from the netlist module area
 * @hw: pointer to the HW structure
 * @bank: whether to read from the active or inactive module
 * @offset: offset into the netlist to read from
 * @data: storage for returned word value
 *
 * Read a word from the specified netlist bank.
 */
static int
ice_read_netlist_module(struct ice_hw *hw, enum ice_bank_select bank, u32 offset, u16 *data)
{
	__le16 data_local;
	int status;

	status = ice_read_flash_module(hw, bank, ICE_SR_NETLIST_BANK_PTR, offset * sizeof(u16),
				       (__force u8 *)&data_local, sizeof(u16));
	if (!status)
		*data = le16_to_cpu(data_local);

	return status;
}

/**
 * ice_read_sr_word - Reads Shadow RAM word and acquire NVM if necessary
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using the ice_read_sr_word_aq.
 */
int ice_read_sr_word(struct ice_hw *hw, u16 offset, u16 *data)
{
	int status;

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
int
ice_get_pfa_module_tlv(struct ice_hw *hw, u16 *module_tlv, u16 *module_tlv_len,
		       u16 module_type)
{
	u16 pfa_len, pfa_ptr;
	u16 next_tlv;
	int status;

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
			return -EINVAL;
		}
		/* Check next TLV, i.e. current TLV pointer + length + 2 words
		 * (for current TLV's type and length)
		 */
		next_tlv = next_tlv + tlv_len + 2;
	}
	/* Module does not exist */
	return -ENOENT;
}

/**
 * ice_read_pba_string - Reads part number string from NVM
 * @hw: pointer to hardware structure
 * @pba_num: stores the part number string from the NVM
 * @pba_num_size: part number string buffer length
 *
 * Reads the part number string from the NVM.
 */
int ice_read_pba_string(struct ice_hw *hw, u8 *pba_num, u32 pba_num_size)
{
	u16 pba_tlv, pba_tlv_len;
	u16 pba_word, pba_size;
	int status;
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
		return -EINVAL;
	}

	/* Subtract one to get PBA word count (PBA Size word is included in
	 * total size)
	 */
	pba_size--;
	if (pba_num_size < (((u32)pba_size * 2) + 1)) {
		ice_debug(hw, ICE_DBG_INIT, "Buffer too small for PBA data.\n");
		return -EINVAL;
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
 * ice_get_nvm_ver_info - Read NVM version information
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash bank
 * @nvm: pointer to NVM info structure
 *
 * Read the NVM EETRACK ID and map version of the main NVM image bank, filling
 * in the NVM info structure.
 */
static int
ice_get_nvm_ver_info(struct ice_hw *hw, enum ice_bank_select bank, struct ice_nvm_info *nvm)
{
	u16 eetrack_lo, eetrack_hi, ver;
	int status;

	status = ice_read_nvm_sr_copy(hw, bank, ICE_SR_NVM_DEV_STARTER_VER, &ver);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read DEV starter version.\n");
		return status;
	}

	nvm->major = FIELD_GET(ICE_NVM_VER_HI_MASK, ver);
	nvm->minor = FIELD_GET(ICE_NVM_VER_LO_MASK, ver);

	status = ice_read_nvm_sr_copy(hw, bank, ICE_SR_NVM_EETRACK_LO, &eetrack_lo);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read EETRACK lo.\n");
		return status;
	}
	status = ice_read_nvm_sr_copy(hw, bank, ICE_SR_NVM_EETRACK_HI, &eetrack_hi);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read EETRACK hi.\n");
		return status;
	}

	nvm->eetrack = (eetrack_hi << 16) | eetrack_lo;

	return 0;
}

/**
 * ice_get_inactive_nvm_ver - Read Option ROM version from the inactive bank
 * @hw: pointer to the HW structure
 * @nvm: storage for Option ROM version information
 *
 * Reads the NVM EETRACK ID, Map version, and security revision of the
 * inactive NVM bank. Used to access version data for a pending update that
 * has not yet been activated.
 */
int ice_get_inactive_nvm_ver(struct ice_hw *hw, struct ice_nvm_info *nvm)
{
	return ice_get_nvm_ver_info(hw, ICE_INACTIVE_FLASH_BANK, nvm);
}

/**
 * ice_get_orom_civd_data - Get the combo version information from Option ROM
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash module
 * @civd: storage for the Option ROM CIVD data.
 *
 * Searches through the Option ROM flash contents to locate the CIVD data for
 * the image.
 */
static int
ice_get_orom_civd_data(struct ice_hw *hw, enum ice_bank_select bank,
		       struct ice_orom_civd_info *civd)
{
	u8 *orom_data;
	int status;
	u32 offset;

	/* The CIVD section is located in the Option ROM aligned to 512 bytes.
	 * The first 4 bytes must contain the ASCII characters "$CIV".
	 * A simple modulo 256 sum of all of the bytes of the structure must
	 * equal 0.
	 *
	 * The exact location is unknown and varies between images but is
	 * usually somewhere in the middle of the bank. We need to scan the
	 * Option ROM bank to locate it.
	 *
	 * It's significantly faster to read the entire Option ROM up front
	 * using the maximum page size, than to read each possible location
	 * with a separate firmware command.
	 */
	orom_data = vzalloc(hw->flash.banks.orom_size);
	if (!orom_data)
		return -ENOMEM;

	status = ice_read_flash_module(hw, bank, ICE_SR_1ST_OROM_BANK_PTR, 0,
				       orom_data, hw->flash.banks.orom_size);
	if (status) {
		vfree(orom_data);
		ice_debug(hw, ICE_DBG_NVM, "Unable to read Option ROM data\n");
		return status;
	}

	/* Scan the memory buffer to locate the CIVD data section */
	for (offset = 0; (offset + 512) <= hw->flash.banks.orom_size; offset += 512) {
		struct ice_orom_civd_info *tmp;
		u8 sum = 0, i;

		tmp = (struct ice_orom_civd_info *)&orom_data[offset];

		/* Skip forward until we find a matching signature */
		if (memcmp("$CIV", tmp->signature, sizeof(tmp->signature)) != 0)
			continue;

		ice_debug(hw, ICE_DBG_NVM, "Found CIVD section at offset %u\n",
			  offset);

		/* Verify that the simple checksum is zero */
		for (i = 0; i < sizeof(*tmp); i++)
			sum += ((u8 *)tmp)[i];

		if (sum) {
			ice_debug(hw, ICE_DBG_NVM, "Found CIVD data with invalid checksum of %u\n",
				  sum);
			goto err_invalid_checksum;
		}

		*civd = *tmp;
		vfree(orom_data);
		return 0;
	}

	ice_debug(hw, ICE_DBG_NVM, "Unable to locate CIVD data within the Option ROM\n");

err_invalid_checksum:
	vfree(orom_data);
	return -EIO;
}

/**
 * ice_get_orom_ver_info - Read Option ROM version information
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash module
 * @orom: pointer to Option ROM info structure
 *
 * Read Option ROM version and security revision from the Option ROM flash
 * section.
 */
static int
ice_get_orom_ver_info(struct ice_hw *hw, enum ice_bank_select bank, struct ice_orom_info *orom)
{
	struct ice_orom_civd_info civd;
	u32 combo_ver;
	int status;

	status = ice_get_orom_civd_data(hw, bank, &civd);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to locate valid Option ROM CIVD data\n");
		return status;
	}

	combo_ver = le32_to_cpu(civd.combo_ver);

	orom->major = FIELD_GET(ICE_OROM_VER_MASK, combo_ver);
	orom->patch = FIELD_GET(ICE_OROM_VER_PATCH_MASK, combo_ver);
	orom->build = FIELD_GET(ICE_OROM_VER_BUILD_MASK, combo_ver);

	return 0;
}

/**
 * ice_get_inactive_orom_ver - Read Option ROM version from the inactive bank
 * @hw: pointer to the HW structure
 * @orom: storage for Option ROM version information
 *
 * Reads the Option ROM version and security revision data for the inactive
 * section of flash. Used to access version data for a pending update that has
 * not yet been activated.
 */
int ice_get_inactive_orom_ver(struct ice_hw *hw, struct ice_orom_info *orom)
{
	return ice_get_orom_ver_info(hw, ICE_INACTIVE_FLASH_BANK, orom);
}

/**
 * ice_get_netlist_info
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash bank
 * @netlist: pointer to netlist version info structure
 *
 * Get the netlist version information from the requested bank. Reads the Link
 * Topology section to find the Netlist ID block and extract the relevant
 * information into the netlist version structure.
 */
static int
ice_get_netlist_info(struct ice_hw *hw, enum ice_bank_select bank,
		     struct ice_netlist_info *netlist)
{
	u16 module_id, length, node_count, i;
	u16 *id_blk;
	int status;

	status = ice_read_netlist_module(hw, bank, ICE_NETLIST_TYPE_OFFSET, &module_id);
	if (status)
		return status;

	if (module_id != ICE_NETLIST_LINK_TOPO_MOD_ID) {
		ice_debug(hw, ICE_DBG_NVM, "Expected netlist module_id ID of 0x%04x, but got 0x%04x\n",
			  ICE_NETLIST_LINK_TOPO_MOD_ID, module_id);
		return -EIO;
	}

	status = ice_read_netlist_module(hw, bank, ICE_LINK_TOPO_MODULE_LEN, &length);
	if (status)
		return status;

	/* sanity check that we have at least enough words to store the netlist ID block */
	if (length < ICE_NETLIST_ID_BLK_SIZE) {
		ice_debug(hw, ICE_DBG_NVM, "Netlist Link Topology module too small. Expected at least %u words, but got %u words.\n",
			  ICE_NETLIST_ID_BLK_SIZE, length);
		return -EIO;
	}

	status = ice_read_netlist_module(hw, bank, ICE_LINK_TOPO_NODE_COUNT, &node_count);
	if (status)
		return status;
	node_count &= ICE_LINK_TOPO_NODE_COUNT_M;

	id_blk = kcalloc(ICE_NETLIST_ID_BLK_SIZE, sizeof(*id_blk), GFP_KERNEL);
	if (!id_blk)
		return -ENOMEM;

	/* Read out the entire Netlist ID Block at once. */
	status = ice_read_flash_module(hw, bank, ICE_SR_NETLIST_BANK_PTR,
				       ICE_NETLIST_ID_BLK_OFFSET(node_count) * sizeof(u16),
				       (u8 *)id_blk, ICE_NETLIST_ID_BLK_SIZE * sizeof(u16));
	if (status)
		goto exit_error;

	for (i = 0; i < ICE_NETLIST_ID_BLK_SIZE; i++)
		id_blk[i] = le16_to_cpu(((__force __le16 *)id_blk)[i]);

	netlist->major = id_blk[ICE_NETLIST_ID_BLK_MAJOR_VER_HIGH] << 16 |
			 id_blk[ICE_NETLIST_ID_BLK_MAJOR_VER_LOW];
	netlist->minor = id_blk[ICE_NETLIST_ID_BLK_MINOR_VER_HIGH] << 16 |
			 id_blk[ICE_NETLIST_ID_BLK_MINOR_VER_LOW];
	netlist->type = id_blk[ICE_NETLIST_ID_BLK_TYPE_HIGH] << 16 |
			id_blk[ICE_NETLIST_ID_BLK_TYPE_LOW];
	netlist->rev = id_blk[ICE_NETLIST_ID_BLK_REV_HIGH] << 16 |
		       id_blk[ICE_NETLIST_ID_BLK_REV_LOW];
	netlist->cust_ver = id_blk[ICE_NETLIST_ID_BLK_CUST_VER];
	/* Read the left most 4 bytes of SHA */
	netlist->hash = id_blk[ICE_NETLIST_ID_BLK_SHA_HASH_WORD(15)] << 16 |
			id_blk[ICE_NETLIST_ID_BLK_SHA_HASH_WORD(14)];

exit_error:
	kfree(id_blk);

	return status;
}

/**
 * ice_get_inactive_netlist_ver
 * @hw: pointer to the HW struct
 * @netlist: pointer to netlist version info structure
 *
 * Read the netlist version data from the inactive netlist bank. Used to
 * extract version data of a pending flash update in order to display the
 * version data.
 */
int ice_get_inactive_netlist_ver(struct ice_hw *hw, struct ice_netlist_info *netlist)
{
	return ice_get_netlist_info(hw, ICE_INACTIVE_FLASH_BANK, netlist);
}

/**
 * ice_discover_flash_size - Discover the available flash size.
 * @hw: pointer to the HW struct
 *
 * The device flash could be up to 16MB in size. However, it is possible that
 * the actual size is smaller. Use bisection to determine the accessible size
 * of flash memory.
 */
static int ice_discover_flash_size(struct ice_hw *hw)
{
	u32 min_size = 0, max_size = ICE_AQC_NVM_MAX_OFFSET + 1;
	int status;

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (status)
		return status;

	while ((max_size - min_size) > 1) {
		u32 offset = (max_size + min_size) / 2;
		u32 len = 1;
		u8 data;

		status = ice_read_flat_nvm(hw, offset, &len, &data, false);
		if (status == -EIO &&
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

	hw->flash.flash_size = max_size;

err_read_flat_nvm:
	ice_release_nvm(hw);

	return status;
}

/**
 * ice_read_sr_pointer - Read the value of a Shadow RAM pointer word
 * @hw: pointer to the HW structure
 * @offset: the word offset of the Shadow RAM word to read
 * @pointer: pointer value read from Shadow RAM
 *
 * Read the given Shadow RAM word, and convert it to a pointer value specified
 * in bytes. This function assumes the specified offset is a valid pointer
 * word.
 *
 * Each pointer word specifies whether it is stored in word size or 4KB
 * sector size by using the highest bit. The reported pointer value will be in
 * bytes, intended for flat NVM reads.
 */
static int ice_read_sr_pointer(struct ice_hw *hw, u16 offset, u32 *pointer)
{
	int status;
	u16 value;

	status = ice_read_sr_word(hw, offset, &value);
	if (status)
		return status;

	/* Determine if the pointer is in 4KB or word units */
	if (value & ICE_SR_NVM_PTR_4KB_UNITS)
		*pointer = (value & ~ICE_SR_NVM_PTR_4KB_UNITS) * 4 * 1024;
	else
		*pointer = value * 2;

	return 0;
}

/**
 * ice_read_sr_area_size - Read an area size from a Shadow RAM word
 * @hw: pointer to the HW structure
 * @offset: the word offset of the Shadow RAM to read
 * @size: size value read from the Shadow RAM
 *
 * Read the given Shadow RAM word, and convert it to an area size value
 * specified in bytes. This function assumes the specified offset is a valid
 * area size word.
 *
 * Each area size word is specified in 4KB sector units. This function reports
 * the size in bytes, intended for flat NVM reads.
 */
static int ice_read_sr_area_size(struct ice_hw *hw, u16 offset, u32 *size)
{
	int status;
	u16 value;

	status = ice_read_sr_word(hw, offset, &value);
	if (status)
		return status;

	/* Area sizes are always specified in 4KB units */
	*size = value * 4 * 1024;

	return 0;
}

/**
 * ice_determine_active_flash_banks - Discover active bank for each module
 * @hw: pointer to the HW struct
 *
 * Read the Shadow RAM control word and determine which banks are active for
 * the NVM, OROM, and Netlist modules. Also read and calculate the associated
 * pointer and size. These values are then cached into the ice_flash_info
 * structure for later use in order to calculate the correct offset to read
 * from the active module.
 */
static int ice_determine_active_flash_banks(struct ice_hw *hw)
{
	struct ice_bank_info *banks = &hw->flash.banks;
	u16 ctrl_word;
	int status;

	status = ice_read_sr_word(hw, ICE_SR_NVM_CTRL_WORD, &ctrl_word);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read the Shadow RAM control word\n");
		return status;
	}

	/* Check that the control word indicates validity */
	if (FIELD_GET(ICE_SR_CTRL_WORD_1_M, ctrl_word) !=
	    ICE_SR_CTRL_WORD_VALID) {
		ice_debug(hw, ICE_DBG_NVM, "Shadow RAM control word is invalid\n");
		return -EIO;
	}

	if (!(ctrl_word & ICE_SR_CTRL_WORD_NVM_BANK))
		banks->nvm_bank = ICE_1ST_FLASH_BANK;
	else
		banks->nvm_bank = ICE_2ND_FLASH_BANK;

	if (!(ctrl_word & ICE_SR_CTRL_WORD_OROM_BANK))
		banks->orom_bank = ICE_1ST_FLASH_BANK;
	else
		banks->orom_bank = ICE_2ND_FLASH_BANK;

	if (!(ctrl_word & ICE_SR_CTRL_WORD_NETLIST_BANK))
		banks->netlist_bank = ICE_1ST_FLASH_BANK;
	else
		banks->netlist_bank = ICE_2ND_FLASH_BANK;

	status = ice_read_sr_pointer(hw, ICE_SR_1ST_NVM_BANK_PTR, &banks->nvm_ptr);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read NVM bank pointer\n");
		return status;
	}

	status = ice_read_sr_area_size(hw, ICE_SR_NVM_BANK_SIZE, &banks->nvm_size);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read NVM bank area size\n");
		return status;
	}

	status = ice_read_sr_pointer(hw, ICE_SR_1ST_OROM_BANK_PTR, &banks->orom_ptr);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read OROM bank pointer\n");
		return status;
	}

	status = ice_read_sr_area_size(hw, ICE_SR_OROM_BANK_SIZE, &banks->orom_size);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read OROM bank area size\n");
		return status;
	}

	status = ice_read_sr_pointer(hw, ICE_SR_NETLIST_BANK_PTR, &banks->netlist_ptr);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read Netlist bank pointer\n");
		return status;
	}

	status = ice_read_sr_area_size(hw, ICE_SR_NETLIST_BANK_SIZE, &banks->netlist_size);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to read Netlist bank area size\n");
		return status;
	}

	return 0;
}

/**
 * ice_init_nvm - initializes NVM setting
 * @hw: pointer to the HW struct
 *
 * This function reads and populates NVM settings such as Shadow RAM size,
 * max_timeout, and blank_nvm_mode
 */
int ice_init_nvm(struct ice_hw *hw)
{
	struct ice_flash_info *flash = &hw->flash;
	u32 fla, gens_stat;
	u8 sr_size;
	int status;

	/* The SR size is stored regardless of the NVM programming mode
	 * as the blank mode may be used in the factory line.
	 */
	gens_stat = rd32(hw, GLNVM_GENS);
	sr_size = FIELD_GET(GLNVM_GENS_SR_SIZE_M, gens_stat);

	/* Switching to words (sr_size contains power of 2) */
	flash->sr_words = BIT(sr_size) * ICE_SR_WORDS_IN_1KB;

	/* Check if we are in the normal or blank NVM programming mode */
	fla = rd32(hw, GLNVM_FLA);
	if (fla & GLNVM_FLA_LOCKED_M) { /* Normal programming mode */
		flash->blank_nvm_mode = false;
	} else {
		/* Blank programming mode */
		flash->blank_nvm_mode = true;
		ice_debug(hw, ICE_DBG_NVM, "NVM init error: unsupported blank mode.\n");
		return -EIO;
	}

	status = ice_discover_flash_size(hw);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "NVM init error: failed to discover flash size.\n");
		return status;
	}

	status = ice_determine_active_flash_banks(hw);
	if (status) {
		ice_debug(hw, ICE_DBG_NVM, "Failed to determine active flash banks.\n");
		return status;
	}

	status = ice_get_nvm_ver_info(hw, ICE_ACTIVE_FLASH_BANK, &flash->nvm);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Failed to read NVM info.\n");
		return status;
	}

	status = ice_get_orom_ver_info(hw, ICE_ACTIVE_FLASH_BANK, &flash->orom);
	if (status)
		ice_debug(hw, ICE_DBG_INIT, "Failed to read Option ROM info.\n");

	/* read the netlist version information */
	status = ice_get_netlist_info(hw, ICE_ACTIVE_FLASH_BANK, &flash->netlist);
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
int ice_nvm_validate_checksum(struct ice_hw *hw)
{
	struct ice_aqc_nvm_checksum *cmd;
	struct ice_aq_desc desc;
	int status;

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
			status = -EIO;

	return status;
}

/**
 * ice_nvm_write_activate
 * @hw: pointer to the HW struct
 * @cmd_flags: flags for write activate command
 * @response_flags: response indicators from firmware
 *
 * Update the control word with the required banks' validity bits
 * and dumps the Shadow RAM to flash (0x0707)
 *
 * cmd_flags controls which banks to activate, the preservation level to use
 * when activating the NVM bank, and whether an EMP reset is required for
 * activation.
 *
 * Note that the 16bit cmd_flags value is split between two separate 1 byte
 * flag values in the descriptor.
 *
 * On successful return of the firmware command, the response_flags variable
 * is updated with the flags reported by firmware indicating certain status,
 * such as whether EMP reset is enabled.
 */
int ice_nvm_write_activate(struct ice_hw *hw, u16 cmd_flags, u8 *response_flags)
{
	struct ice_aqc_nvm *cmd;
	struct ice_aq_desc desc;
	int err;

	cmd = &desc.params.nvm;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_write_activate);

	cmd->cmd_flags = (u8)(cmd_flags & 0xFF);
	cmd->offset_high = (u8)((cmd_flags >> 8) & 0xFF);

	err = ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
	if (!err && response_flags)
		*response_flags = cmd->cmd_flags;

	return err;
}

/**
 * ice_aq_nvm_update_empr
 * @hw: pointer to the HW struct
 *
 * Update empr (0x0709). This command allows SW to
 * request an EMPR to activate new FW.
 */
int ice_aq_nvm_update_empr(struct ice_hw *hw)
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

int
ice_nvm_set_pkg_data(struct ice_hw *hw, bool del_pkg_data_flag, u8 *data,
		     u16 length, struct ice_sq_cd *cd)
{
	struct ice_aqc_nvm_pkg_data *cmd;
	struct ice_aq_desc desc;

	if (length != 0 && !data)
		return -EINVAL;

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

int
ice_nvm_pass_component_tbl(struct ice_hw *hw, u8 *data, u16 length,
			   u8 transfer_flag, u8 *comp_response,
			   u8 *comp_response_code, struct ice_sq_cd *cd)
{
	struct ice_aqc_nvm_pass_comp_tbl *cmd;
	struct ice_aq_desc desc;
	int status;

	if (!data || !comp_response || !comp_response_code)
		return -EINVAL;

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
