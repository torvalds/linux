// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_common.h"

/**
 * ice_aq_read_nvm
 * @hw: pointer to the hw struct
 * @module_typeid: module pointer location in words from the NVM beginning
 * @offset: byte offset from the module beginning
 * @length: length of the section to be read (in bytes from the offset)
 * @data: command buffer (size [bytes] = length)
 * @last_command: tells if this is the last command in a series
 * @cd: pointer to command details structure or NULL
 *
 * Read the NVM using the admin queue commands (0x0701)
 */
static enum ice_status
ice_aq_read_nvm(struct ice_hw *hw, u16 module_typeid, u32 offset, u16 length,
		void *data, bool last_command, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	struct ice_aqc_nvm *cmd;

	cmd = &desc.params.nvm;

	/* In offset the highest byte must be zeroed. */
	if (offset & 0xFF000000)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_read);

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
 * ice_check_sr_access_params - verify params for Shadow RAM R/W operations.
 * @hw: pointer to the HW structure
 * @offset: offset in words from module start
 * @words: number of words to access
 */
static enum ice_status
ice_check_sr_access_params(struct ice_hw *hw, u32 offset, u16 words)
{
	if ((offset + words) > hw->nvm.sr_words) {
		ice_debug(hw, ICE_DBG_NVM,
			  "NVM error: offset beyond SR lmt.\n");
		return ICE_ERR_PARAM;
	}

	if (words > ICE_SR_SECTOR_SIZE_IN_WORDS) {
		/* We can access only up to 4KB (one sector), in one AQ write */
		ice_debug(hw, ICE_DBG_NVM,
			  "NVM error: tried to access %d words, limit is %d.\n",
			  words, ICE_SR_SECTOR_SIZE_IN_WORDS);
		return ICE_ERR_PARAM;
	}

	if (((offset + (words - 1)) / ICE_SR_SECTOR_SIZE_IN_WORDS) !=
	    (offset / ICE_SR_SECTOR_SIZE_IN_WORDS)) {
		/* A single access cannot spread over two sectors */
		ice_debug(hw, ICE_DBG_NVM,
			  "NVM error: cannot spread over two sectors.\n");
		return ICE_ERR_PARAM;
	}

	return 0;
}

/**
 * ice_read_sr_aq - Read Shadow RAM.
 * @hw: pointer to the HW structure
 * @offset: offset in words from module start
 * @words: number of words to read
 * @data: buffer for words reads from Shadow RAM
 * @last_command: tells the AdminQ that this is the last command
 *
 * Reads 16-bit word buffers from the Shadow RAM using the admin command.
 */
static enum ice_status
ice_read_sr_aq(struct ice_hw *hw, u32 offset, u16 words, u16 *data,
	       bool last_command)
{
	enum ice_status status;

	status = ice_check_sr_access_params(hw, offset, words);

	/* values in "offset" and "words" parameters are sized as words
	 * (16 bits) but ice_aq_read_nvm expects these values in bytes.
	 * So do this conversion while calling ice_aq_read_nvm.
	 */
	if (!status)
		status = ice_aq_read_nvm(hw, 0, 2 * offset, 2 * words, data,
					 last_command, NULL);

	return status;
}

/**
 * ice_read_sr_word_aq - Reads Shadow RAM via AQ
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using the ice_read_sr_aq method.
 */
static enum ice_status
ice_read_sr_word_aq(struct ice_hw *hw, u16 offset, u16 *data)
{
	enum ice_status status;

	status = ice_read_sr_aq(hw, offset, 1, data, true);
	if (!status)
		*data = le16_to_cpu(*(__le16 *)data);

	return status;
}

/**
 * ice_acquire_nvm - Generic request for acquiring the NVM ownership
 * @hw: pointer to the HW structure
 * @access: NVM access type (read or write)
 *
 * This function will request NVM ownership.
 */
static enum
ice_status ice_acquire_nvm(struct ice_hw *hw,
			   enum ice_aq_res_access_type access)
{
	if (hw->nvm.blank_nvm_mode)
		return 0;

	return ice_acquire_res(hw, ICE_NVM_RES_ID, access);
}

/**
 * ice_release_nvm - Generic request for releasing the NVM ownership
 * @hw: pointer to the HW structure
 *
 * This function will release NVM ownership.
 */
static void ice_release_nvm(struct ice_hw *hw)
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
static enum ice_status
ice_read_sr_word(struct ice_hw *hw, u16 offset, u16 *data)
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
 * ice_init_nvm - initializes NVM setting
 * @hw: pointer to the hw struct
 *
 * This function reads and populates NVM settings such as Shadow RAM size,
 * max_timeout, and blank_nvm_mode
 */
enum ice_status ice_init_nvm(struct ice_hw *hw)
{
	struct ice_nvm_info *nvm = &hw->nvm;
	u16 eetrack_lo, eetrack_hi;
	enum ice_status status = 0;
	u32 fla, gens_stat;
	u8 sr_size;

	/* The SR size is stored regardless of the nvm programming mode
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
	} else { /* Blank programming mode */
		nvm->blank_nvm_mode = true;
		status = ICE_ERR_NVM_BLANK_MODE;
		ice_debug(hw, ICE_DBG_NVM,
			  "NVM init error: unsupported blank mode.\n");
		return status;
	}

	status = ice_read_sr_word(hw, ICE_SR_NVM_DEV_STARTER_VER, &hw->nvm.ver);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT,
			  "Failed to read DEV starter version.\n");
		return status;
	}

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

	hw->nvm.eetrack = (eetrack_hi << 16) | eetrack_lo;

	return status;
}
