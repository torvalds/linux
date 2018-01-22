/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include "i40e_prototype.h"

/**
 * i40e_init_nvm_ops - Initialize NVM function pointers
 * @hw: pointer to the HW structure
 *
 * Setup the function pointers and the NVM info structure. Should be called
 * once per NVM initialization, e.g. inside the i40e_init_shared_code().
 * Please notice that the NVM term is used here (& in all methods covered
 * in this file) as an equivalent of the FLASH part mapped into the SR.
 * We are accessing FLASH always thru the Shadow RAM.
 **/
i40e_status i40e_init_nvm(struct i40e_hw *hw)
{
	struct i40e_nvm_info *nvm = &hw->nvm;
	i40e_status ret_code = 0;
	u32 fla, gens;
	u8 sr_size;

	/* The SR size is stored regardless of the nvm programming mode
	 * as the blank mode may be used in the factory line.
	 */
	gens = rd32(hw, I40E_GLNVM_GENS);
	sr_size = ((gens & I40E_GLNVM_GENS_SR_SIZE_MASK) >>
			   I40E_GLNVM_GENS_SR_SIZE_SHIFT);
	/* Switching to words (sr_size contains power of 2KB) */
	nvm->sr_size = BIT(sr_size) * I40E_SR_WORDS_IN_1KB;

	/* Check if we are in the normal or blank NVM programming mode */
	fla = rd32(hw, I40E_GLNVM_FLA);
	if (fla & I40E_GLNVM_FLA_LOCKED_MASK) { /* Normal programming mode */
		/* Max NVM timeout */
		nvm->timeout = I40E_MAX_NVM_TIMEOUT;
		nvm->blank_nvm_mode = false;
	} else { /* Blank programming mode */
		nvm->blank_nvm_mode = true;
		ret_code = I40E_ERR_NVM_BLANK_MODE;
		i40e_debug(hw, I40E_DEBUG_NVM, "NVM init error: unsupported blank mode.\n");
	}

	return ret_code;
}

/**
 * i40e_acquire_nvm - Generic request for acquiring the NVM ownership
 * @hw: pointer to the HW structure
 * @access: NVM access type (read or write)
 *
 * This function will request NVM ownership for reading
 * via the proper Admin Command.
 **/
i40e_status i40e_acquire_nvm(struct i40e_hw *hw,
				       enum i40e_aq_resource_access_type access)
{
	i40e_status ret_code = 0;
	u64 gtime, timeout;
	u64 time_left = 0;

	if (hw->nvm.blank_nvm_mode)
		goto i40e_i40e_acquire_nvm_exit;

	ret_code = i40e_aq_request_resource(hw, I40E_NVM_RESOURCE_ID, access,
					    0, &time_left, NULL);
	/* Reading the Global Device Timer */
	gtime = rd32(hw, I40E_GLVFGEN_TIMER);

	/* Store the timeout */
	hw->nvm.hw_semaphore_timeout = I40E_MS_TO_GTIME(time_left) + gtime;

	if (ret_code)
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM acquire type %d failed time_left=%llu ret=%d aq_err=%d\n",
			   access, time_left, ret_code, hw->aq.asq_last_status);

	if (ret_code && time_left) {
		/* Poll until the current NVM owner timeouts */
		timeout = I40E_MS_TO_GTIME(I40E_MAX_NVM_TIMEOUT) + gtime;
		while ((gtime < timeout) && time_left) {
			usleep_range(10000, 20000);
			gtime = rd32(hw, I40E_GLVFGEN_TIMER);
			ret_code = i40e_aq_request_resource(hw,
							I40E_NVM_RESOURCE_ID,
							access, 0, &time_left,
							NULL);
			if (!ret_code) {
				hw->nvm.hw_semaphore_timeout =
					    I40E_MS_TO_GTIME(time_left) + gtime;
				break;
			}
		}
		if (ret_code) {
			hw->nvm.hw_semaphore_timeout = 0;
			i40e_debug(hw, I40E_DEBUG_NVM,
				   "NVM acquire timed out, wait %llu ms before trying again. status=%d aq_err=%d\n",
				   time_left, ret_code, hw->aq.asq_last_status);
		}
	}

i40e_i40e_acquire_nvm_exit:
	return ret_code;
}

/**
 * i40e_release_nvm - Generic request for releasing the NVM ownership
 * @hw: pointer to the HW structure
 *
 * This function will release NVM resource via the proper Admin Command.
 **/
void i40e_release_nvm(struct i40e_hw *hw)
{
	i40e_status ret_code = I40E_SUCCESS;
	u32 total_delay = 0;

	if (hw->nvm.blank_nvm_mode)
		return;

	ret_code = i40e_aq_release_resource(hw, I40E_NVM_RESOURCE_ID, 0, NULL);

	/* there are some rare cases when trying to release the resource
	 * results in an admin Q timeout, so handle them correctly
	 */
	while ((ret_code == I40E_ERR_ADMIN_QUEUE_TIMEOUT) &&
	       (total_delay < hw->aq.asq_cmd_timeout)) {
		usleep_range(1000, 2000);
		ret_code = i40e_aq_release_resource(hw,
						    I40E_NVM_RESOURCE_ID,
						    0, NULL);
		total_delay++;
	}
}

/**
 * i40e_poll_sr_srctl_done_bit - Polls the GLNVM_SRCTL done bit
 * @hw: pointer to the HW structure
 *
 * Polls the SRCTL Shadow RAM register done bit.
 **/
static i40e_status i40e_poll_sr_srctl_done_bit(struct i40e_hw *hw)
{
	i40e_status ret_code = I40E_ERR_TIMEOUT;
	u32 srctl, wait_cnt;

	/* Poll the I40E_GLNVM_SRCTL until the done bit is set */
	for (wait_cnt = 0; wait_cnt < I40E_SRRD_SRCTL_ATTEMPTS; wait_cnt++) {
		srctl = rd32(hw, I40E_GLNVM_SRCTL);
		if (srctl & I40E_GLNVM_SRCTL_DONE_MASK) {
			ret_code = 0;
			break;
		}
		udelay(5);
	}
	if (ret_code == I40E_ERR_TIMEOUT)
		i40e_debug(hw, I40E_DEBUG_NVM, "Done bit in GLNVM_SRCTL not set");
	return ret_code;
}

/**
 * i40e_read_nvm_word_srctl - Reads Shadow RAM via SRCTL register
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using the GLNVM_SRCTL register.
 **/
static i40e_status i40e_read_nvm_word_srctl(struct i40e_hw *hw, u16 offset,
					    u16 *data)
{
	i40e_status ret_code = I40E_ERR_TIMEOUT;
	u32 sr_reg;

	if (offset >= hw->nvm.sr_size) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM read error: offset %d beyond Shadow RAM limit %d\n",
			   offset, hw->nvm.sr_size);
		ret_code = I40E_ERR_PARAM;
		goto read_nvm_exit;
	}

	/* Poll the done bit first */
	ret_code = i40e_poll_sr_srctl_done_bit(hw);
	if (!ret_code) {
		/* Write the address and start reading */
		sr_reg = ((u32)offset << I40E_GLNVM_SRCTL_ADDR_SHIFT) |
			 BIT(I40E_GLNVM_SRCTL_START_SHIFT);
		wr32(hw, I40E_GLNVM_SRCTL, sr_reg);

		/* Poll I40E_GLNVM_SRCTL until the done bit is set */
		ret_code = i40e_poll_sr_srctl_done_bit(hw);
		if (!ret_code) {
			sr_reg = rd32(hw, I40E_GLNVM_SRDATA);
			*data = (u16)((sr_reg &
				       I40E_GLNVM_SRDATA_RDDATA_MASK)
				    >> I40E_GLNVM_SRDATA_RDDATA_SHIFT);
		}
	}
	if (ret_code)
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM read error: Couldn't access Shadow RAM address: 0x%x\n",
			   offset);

read_nvm_exit:
	return ret_code;
}

/**
 * i40e_read_nvm_aq - Read Shadow RAM.
 * @hw: pointer to the HW structure.
 * @module_pointer: module pointer location in words from the NVM beginning
 * @offset: offset in words from module start
 * @words: number of words to write
 * @data: buffer with words to write to the Shadow RAM
 * @last_command: tells the AdminQ that this is the last command
 *
 * Writes a 16 bit words buffer to the Shadow RAM using the admin command.
 **/
static i40e_status i40e_read_nvm_aq(struct i40e_hw *hw, u8 module_pointer,
				    u32 offset, u16 words, void *data,
				    bool last_command)
{
	i40e_status ret_code = I40E_ERR_NVM;
	struct i40e_asq_cmd_details cmd_details;

	memset(&cmd_details, 0, sizeof(cmd_details));
	cmd_details.wb_desc = &hw->nvm_wb_desc;

	/* Here we are checking the SR limit only for the flat memory model.
	 * We cannot do it for the module-based model, as we did not acquire
	 * the NVM resource yet (we cannot get the module pointer value).
	 * Firmware will check the module-based model.
	 */
	if ((offset + words) > hw->nvm.sr_size)
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM write error: offset %d beyond Shadow RAM limit %d\n",
			   (offset + words), hw->nvm.sr_size);
	else if (words > I40E_SR_SECTOR_SIZE_IN_WORDS)
		/* We can write only up to 4KB (one sector), in one AQ write */
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM write fail error: tried to write %d words, limit is %d.\n",
			   words, I40E_SR_SECTOR_SIZE_IN_WORDS);
	else if (((offset + (words - 1)) / I40E_SR_SECTOR_SIZE_IN_WORDS)
		 != (offset / I40E_SR_SECTOR_SIZE_IN_WORDS))
		/* A single write cannot spread over two sectors */
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM write error: cannot spread over two sectors in a single write offset=%d words=%d\n",
			   offset, words);
	else
		ret_code = i40e_aq_read_nvm(hw, module_pointer,
					    2 * offset,  /*bytes*/
					    2 * words,   /*bytes*/
					    data, last_command, &cmd_details);

	return ret_code;
}

/**
 * i40e_read_nvm_word_aq - Reads Shadow RAM via AQ
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using the AdminQ
 **/
static i40e_status i40e_read_nvm_word_aq(struct i40e_hw *hw, u16 offset,
					 u16 *data)
{
	i40e_status ret_code = I40E_ERR_TIMEOUT;

	ret_code = i40e_read_nvm_aq(hw, 0x0, offset, 1, data, true);
	*data = le16_to_cpu(*(__le16 *)data);

	return ret_code;
}

/**
 * __i40e_read_nvm_word - Reads nvm word, assumes caller does the locking
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM.
 *
 * Do not use this function except in cases where the nvm lock is already
 * taken via i40e_acquire_nvm().
 **/
static i40e_status __i40e_read_nvm_word(struct i40e_hw *hw,
					u16 offset, u16 *data)
{
	if (hw->flags & I40E_HW_FLAG_AQ_SRCTL_ACCESS_ENABLE)
		return i40e_read_nvm_word_aq(hw, offset, data);

	return i40e_read_nvm_word_srctl(hw, offset, data);
}

/**
 * i40e_read_nvm_word - Reads nvm word and acquire lock if necessary
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM.
 **/
i40e_status i40e_read_nvm_word(struct i40e_hw *hw, u16 offset,
			       u16 *data)
{
	i40e_status ret_code = 0;

	if (hw->flags & I40E_HW_FLAG_NVM_READ_REQUIRES_LOCK)
		ret_code = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret_code)
		return ret_code;

	ret_code = __i40e_read_nvm_word(hw, offset, data);

	if (hw->flags & I40E_HW_FLAG_NVM_READ_REQUIRES_LOCK)
		i40e_release_nvm(hw);

	return ret_code;
}

/**
 * i40e_read_nvm_buffer_srctl - Reads Shadow RAM buffer via SRCTL register
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF).
 * @words: (in) number of words to read; (out) number of words actually read
 * @data: words read from the Shadow RAM
 *
 * Reads 16 bit words (data buffer) from the SR using the i40e_read_nvm_srrd()
 * method. The buffer read is preceded by the NVM ownership take
 * and followed by the release.
 **/
static i40e_status i40e_read_nvm_buffer_srctl(struct i40e_hw *hw, u16 offset,
					      u16 *words, u16 *data)
{
	i40e_status ret_code = 0;
	u16 index, word;

	/* Loop thru the selected region */
	for (word = 0; word < *words; word++) {
		index = offset + word;
		ret_code = i40e_read_nvm_word_srctl(hw, index, &data[word]);
		if (ret_code)
			break;
	}

	/* Update the number of words read from the Shadow RAM */
	*words = word;

	return ret_code;
}

/**
 * i40e_read_nvm_buffer_aq - Reads Shadow RAM buffer via AQ
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF).
 * @words: (in) number of words to read; (out) number of words actually read
 * @data: words read from the Shadow RAM
 *
 * Reads 16 bit words (data buffer) from the SR using the i40e_read_nvm_aq()
 * method. The buffer read is preceded by the NVM ownership take
 * and followed by the release.
 **/
static i40e_status i40e_read_nvm_buffer_aq(struct i40e_hw *hw, u16 offset,
					   u16 *words, u16 *data)
{
	i40e_status ret_code;
	u16 read_size = *words;
	bool last_cmd = false;
	u16 words_read = 0;
	u16 i = 0;

	do {
		/* Calculate number of bytes we should read in this step.
		 * FVL AQ do not allow to read more than one page at a time or
		 * to cross page boundaries.
		 */
		if (offset % I40E_SR_SECTOR_SIZE_IN_WORDS)
			read_size = min(*words,
					(u16)(I40E_SR_SECTOR_SIZE_IN_WORDS -
				      (offset % I40E_SR_SECTOR_SIZE_IN_WORDS)));
		else
			read_size = min((*words - words_read),
					I40E_SR_SECTOR_SIZE_IN_WORDS);

		/* Check if this is last command, if so set proper flag */
		if ((words_read + read_size) >= *words)
			last_cmd = true;

		ret_code = i40e_read_nvm_aq(hw, 0x0, offset, read_size,
					    data + words_read, last_cmd);
		if (ret_code)
			goto read_nvm_buffer_aq_exit;

		/* Increment counter for words already read and move offset to
		 * new read location
		 */
		words_read += read_size;
		offset += read_size;
	} while (words_read < *words);

	for (i = 0; i < *words; i++)
		data[i] = le16_to_cpu(((__le16 *)data)[i]);

read_nvm_buffer_aq_exit:
	*words = words_read;
	return ret_code;
}

/**
 * __i40e_read_nvm_buffer - Reads nvm buffer, caller must acquire lock
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF).
 * @words: (in) number of words to read; (out) number of words actually read
 * @data: words read from the Shadow RAM
 *
 * Reads 16 bit words (data buffer) from the SR using the i40e_read_nvm_srrd()
 * method.
 **/
static i40e_status __i40e_read_nvm_buffer(struct i40e_hw *hw,
					  u16 offset, u16 *words,
					  u16 *data)
{
	if (hw->flags & I40E_HW_FLAG_AQ_SRCTL_ACCESS_ENABLE)
		return i40e_read_nvm_buffer_aq(hw, offset, words, data);

	return i40e_read_nvm_buffer_srctl(hw, offset, words, data);
}

/**
 * i40e_write_nvm_aq - Writes Shadow RAM.
 * @hw: pointer to the HW structure.
 * @module_pointer: module pointer location in words from the NVM beginning
 * @offset: offset in words from module start
 * @words: number of words to write
 * @data: buffer with words to write to the Shadow RAM
 * @last_command: tells the AdminQ that this is the last command
 *
 * Writes a 16 bit words buffer to the Shadow RAM using the admin command.
 **/
static i40e_status i40e_write_nvm_aq(struct i40e_hw *hw, u8 module_pointer,
				     u32 offset, u16 words, void *data,
				     bool last_command)
{
	i40e_status ret_code = I40E_ERR_NVM;
	struct i40e_asq_cmd_details cmd_details;

	memset(&cmd_details, 0, sizeof(cmd_details));
	cmd_details.wb_desc = &hw->nvm_wb_desc;

	/* Here we are checking the SR limit only for the flat memory model.
	 * We cannot do it for the module-based model, as we did not acquire
	 * the NVM resource yet (we cannot get the module pointer value).
	 * Firmware will check the module-based model.
	 */
	if ((offset + words) > hw->nvm.sr_size)
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM write error: offset %d beyond Shadow RAM limit %d\n",
			   (offset + words), hw->nvm.sr_size);
	else if (words > I40E_SR_SECTOR_SIZE_IN_WORDS)
		/* We can write only up to 4KB (one sector), in one AQ write */
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM write fail error: tried to write %d words, limit is %d.\n",
			   words, I40E_SR_SECTOR_SIZE_IN_WORDS);
	else if (((offset + (words - 1)) / I40E_SR_SECTOR_SIZE_IN_WORDS)
		 != (offset / I40E_SR_SECTOR_SIZE_IN_WORDS))
		/* A single write cannot spread over two sectors */
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVM write error: cannot spread over two sectors in a single write offset=%d words=%d\n",
			   offset, words);
	else
		ret_code = i40e_aq_update_nvm(hw, module_pointer,
					      2 * offset,  /*bytes*/
					      2 * words,   /*bytes*/
					      data, last_command, &cmd_details);

	return ret_code;
}

/**
 * i40e_calc_nvm_checksum - Calculates and returns the checksum
 * @hw: pointer to hardware structure
 * @checksum: pointer to the checksum
 *
 * This function calculates SW Checksum that covers the whole 64kB shadow RAM
 * except the VPD and PCIe ALT Auto-load modules. The structure and size of VPD
 * is customer specific and unknown. Therefore, this function skips all maximum
 * possible size of VPD (1kB).
 **/
static i40e_status i40e_calc_nvm_checksum(struct i40e_hw *hw,
						    u16 *checksum)
{
	i40e_status ret_code;
	struct i40e_virt_mem vmem;
	u16 pcie_alt_module = 0;
	u16 checksum_local = 0;
	u16 vpd_module = 0;
	u16 *data;
	u16 i = 0;

	ret_code = i40e_allocate_virt_mem(hw, &vmem,
				    I40E_SR_SECTOR_SIZE_IN_WORDS * sizeof(u16));
	if (ret_code)
		goto i40e_calc_nvm_checksum_exit;
	data = (u16 *)vmem.va;

	/* read pointer to VPD area */
	ret_code = __i40e_read_nvm_word(hw, I40E_SR_VPD_PTR, &vpd_module);
	if (ret_code) {
		ret_code = I40E_ERR_NVM_CHECKSUM;
		goto i40e_calc_nvm_checksum_exit;
	}

	/* read pointer to PCIe Alt Auto-load module */
	ret_code = __i40e_read_nvm_word(hw, I40E_SR_PCIE_ALT_AUTO_LOAD_PTR,
					&pcie_alt_module);
	if (ret_code) {
		ret_code = I40E_ERR_NVM_CHECKSUM;
		goto i40e_calc_nvm_checksum_exit;
	}

	/* Calculate SW checksum that covers the whole 64kB shadow RAM
	 * except the VPD and PCIe ALT Auto-load modules
	 */
	for (i = 0; i < hw->nvm.sr_size; i++) {
		/* Read SR page */
		if ((i % I40E_SR_SECTOR_SIZE_IN_WORDS) == 0) {
			u16 words = I40E_SR_SECTOR_SIZE_IN_WORDS;

			ret_code = __i40e_read_nvm_buffer(hw, i, &words, data);
			if (ret_code) {
				ret_code = I40E_ERR_NVM_CHECKSUM;
				goto i40e_calc_nvm_checksum_exit;
			}
		}

		/* Skip Checksum word */
		if (i == I40E_SR_SW_CHECKSUM_WORD)
			continue;
		/* Skip VPD module (convert byte size to word count) */
		if ((i >= (u32)vpd_module) &&
		    (i < ((u32)vpd_module +
		     (I40E_SR_VPD_MODULE_MAX_SIZE / 2)))) {
			continue;
		}
		/* Skip PCIe ALT module (convert byte size to word count) */
		if ((i >= (u32)pcie_alt_module) &&
		    (i < ((u32)pcie_alt_module +
		     (I40E_SR_PCIE_ALT_MODULE_MAX_SIZE / 2)))) {
			continue;
		}

		checksum_local += data[i % I40E_SR_SECTOR_SIZE_IN_WORDS];
	}

	*checksum = (u16)I40E_SR_SW_CHECKSUM_BASE - checksum_local;

i40e_calc_nvm_checksum_exit:
	i40e_free_virt_mem(hw, &vmem);
	return ret_code;
}

/**
 * i40e_update_nvm_checksum - Updates the NVM checksum
 * @hw: pointer to hardware structure
 *
 * NVM ownership must be acquired before calling this function and released
 * on ARQ completion event reception by caller.
 * This function will commit SR to NVM.
 **/
i40e_status i40e_update_nvm_checksum(struct i40e_hw *hw)
{
	i40e_status ret_code;
	u16 checksum;
	__le16 le_sum;

	ret_code = i40e_calc_nvm_checksum(hw, &checksum);
	if (!ret_code) {
		le_sum = cpu_to_le16(checksum);
		ret_code = i40e_write_nvm_aq(hw, 0x00, I40E_SR_SW_CHECKSUM_WORD,
					     1, &le_sum, true);
	}

	return ret_code;
}

/**
 * i40e_validate_nvm_checksum - Validate EEPROM checksum
 * @hw: pointer to hardware structure
 * @checksum: calculated checksum
 *
 * Performs checksum calculation and validates the NVM SW checksum. If the
 * caller does not need checksum, the value can be NULL.
 **/
i40e_status i40e_validate_nvm_checksum(struct i40e_hw *hw,
						 u16 *checksum)
{
	i40e_status ret_code = 0;
	u16 checksum_sr = 0;
	u16 checksum_local = 0;

	/* We must acquire the NVM lock in order to correctly synchronize the
	 * NVM accesses across multiple PFs. Without doing so it is possible
	 * for one of the PFs to read invalid data potentially indicating that
	 * the checksum is invalid.
	 */
	ret_code = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret_code)
		return ret_code;
	ret_code = i40e_calc_nvm_checksum(hw, &checksum_local);
	__i40e_read_nvm_word(hw, I40E_SR_SW_CHECKSUM_WORD, &checksum_sr);
	i40e_release_nvm(hw);
	if (ret_code)
		return ret_code;

	/* Verify read checksum from EEPROM is the same as
	 * calculated checksum
	 */
	if (checksum_local != checksum_sr)
		ret_code = I40E_ERR_NVM_CHECKSUM;

	/* If the user cares, return the calculated checksum */
	if (checksum)
		*checksum = checksum_local;

	return ret_code;
}

static i40e_status i40e_nvmupd_state_init(struct i40e_hw *hw,
					  struct i40e_nvm_access *cmd,
					  u8 *bytes, int *perrno);
static i40e_status i40e_nvmupd_state_reading(struct i40e_hw *hw,
					     struct i40e_nvm_access *cmd,
					     u8 *bytes, int *perrno);
static i40e_status i40e_nvmupd_state_writing(struct i40e_hw *hw,
					     struct i40e_nvm_access *cmd,
					     u8 *bytes, int *errno);
static enum i40e_nvmupd_cmd i40e_nvmupd_validate_command(struct i40e_hw *hw,
						struct i40e_nvm_access *cmd,
						int *perrno);
static i40e_status i40e_nvmupd_nvm_erase(struct i40e_hw *hw,
					 struct i40e_nvm_access *cmd,
					 int *perrno);
static i40e_status i40e_nvmupd_nvm_write(struct i40e_hw *hw,
					 struct i40e_nvm_access *cmd,
					 u8 *bytes, int *perrno);
static i40e_status i40e_nvmupd_nvm_read(struct i40e_hw *hw,
					struct i40e_nvm_access *cmd,
					u8 *bytes, int *perrno);
static i40e_status i40e_nvmupd_exec_aq(struct i40e_hw *hw,
				       struct i40e_nvm_access *cmd,
				       u8 *bytes, int *perrno);
static i40e_status i40e_nvmupd_get_aq_result(struct i40e_hw *hw,
					     struct i40e_nvm_access *cmd,
					     u8 *bytes, int *perrno);
static inline u8 i40e_nvmupd_get_module(u32 val)
{
	return (u8)(val & I40E_NVM_MOD_PNT_MASK);
}
static inline u8 i40e_nvmupd_get_transaction(u32 val)
{
	return (u8)((val & I40E_NVM_TRANS_MASK) >> I40E_NVM_TRANS_SHIFT);
}

static const char * const i40e_nvm_update_state_str[] = {
	"I40E_NVMUPD_INVALID",
	"I40E_NVMUPD_READ_CON",
	"I40E_NVMUPD_READ_SNT",
	"I40E_NVMUPD_READ_LCB",
	"I40E_NVMUPD_READ_SA",
	"I40E_NVMUPD_WRITE_ERA",
	"I40E_NVMUPD_WRITE_CON",
	"I40E_NVMUPD_WRITE_SNT",
	"I40E_NVMUPD_WRITE_LCB",
	"I40E_NVMUPD_WRITE_SA",
	"I40E_NVMUPD_CSUM_CON",
	"I40E_NVMUPD_CSUM_SA",
	"I40E_NVMUPD_CSUM_LCB",
	"I40E_NVMUPD_STATUS",
	"I40E_NVMUPD_EXEC_AQ",
	"I40E_NVMUPD_GET_AQ_RESULT",
};

/**
 * i40e_nvmupd_command - Process an NVM update command
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command
 * @bytes: pointer to the data buffer
 * @perrno: pointer to return error code
 *
 * Dispatches command depending on what update state is current
 **/
i40e_status i40e_nvmupd_command(struct i40e_hw *hw,
				struct i40e_nvm_access *cmd,
				u8 *bytes, int *perrno)
{
	i40e_status status;
	enum i40e_nvmupd_cmd upd_cmd;

	/* assume success */
	*perrno = 0;

	/* early check for status command and debug msgs */
	upd_cmd = i40e_nvmupd_validate_command(hw, cmd, perrno);

	i40e_debug(hw, I40E_DEBUG_NVM, "%s state %d nvm_release_on_hold %d opc 0x%04x cmd 0x%08x config 0x%08x offset 0x%08x data_size 0x%08x\n",
		   i40e_nvm_update_state_str[upd_cmd],
		   hw->nvmupd_state,
		   hw->nvm_release_on_done, hw->nvm_wait_opcode,
		   cmd->command, cmd->config, cmd->offset, cmd->data_size);

	if (upd_cmd == I40E_NVMUPD_INVALID) {
		*perrno = -EFAULT;
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_validate_command returns %d errno %d\n",
			   upd_cmd, *perrno);
	}

	/* a status request returns immediately rather than
	 * going into the state machine
	 */
	if (upd_cmd == I40E_NVMUPD_STATUS) {
		if (!cmd->data_size) {
			*perrno = -EFAULT;
			return I40E_ERR_BUF_TOO_SHORT;
		}

		bytes[0] = hw->nvmupd_state;

		if (cmd->data_size >= 4) {
			bytes[1] = 0;
			*((u16 *)&bytes[2]) = hw->nvm_wait_opcode;
		}

		/* Clear error status on read */
		if (hw->nvmupd_state == I40E_NVMUPD_STATE_ERROR)
			hw->nvmupd_state = I40E_NVMUPD_STATE_INIT;

		return 0;
	}

	/* Clear status even it is not read and log */
	if (hw->nvmupd_state == I40E_NVMUPD_STATE_ERROR) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "Clearing I40E_NVMUPD_STATE_ERROR state without reading\n");
		hw->nvmupd_state = I40E_NVMUPD_STATE_INIT;
	}

	/* Acquire lock to prevent race condition where adminq_task
	 * can execute after i40e_nvmupd_nvm_read/write but before state
	 * variables (nvm_wait_opcode, nvm_release_on_done) are updated.
	 *
	 * During NVMUpdate, it is observed that lock could be held for
	 * ~5ms for most commands. However lock is held for ~60ms for
	 * NVMUPD_CSUM_LCB command.
	 */
	mutex_lock(&hw->aq.arq_mutex);
	switch (hw->nvmupd_state) {
	case I40E_NVMUPD_STATE_INIT:
		status = i40e_nvmupd_state_init(hw, cmd, bytes, perrno);
		break;

	case I40E_NVMUPD_STATE_READING:
		status = i40e_nvmupd_state_reading(hw, cmd, bytes, perrno);
		break;

	case I40E_NVMUPD_STATE_WRITING:
		status = i40e_nvmupd_state_writing(hw, cmd, bytes, perrno);
		break;

	case I40E_NVMUPD_STATE_INIT_WAIT:
	case I40E_NVMUPD_STATE_WRITE_WAIT:
		/* if we need to stop waiting for an event, clear
		 * the wait info and return before doing anything else
		 */
		if (cmd->offset == 0xffff) {
			i40e_nvmupd_check_wait_event(hw, hw->nvm_wait_opcode);
			status = 0;
			goto exit;
		}

		status = I40E_ERR_NOT_READY;
		*perrno = -EBUSY;
		break;

	default:
		/* invalid state, should never happen */
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVMUPD: no such state %d\n", hw->nvmupd_state);
		status = I40E_NOT_SUPPORTED;
		*perrno = -ESRCH;
		break;
	}
exit:
	mutex_unlock(&hw->aq.arq_mutex);
	return status;
}

/**
 * i40e_nvmupd_state_init - Handle NVM update state Init
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @bytes: pointer to the data buffer
 * @perrno: pointer to return error code
 *
 * Process legitimate commands of the Init state and conditionally set next
 * state. Reject all other commands.
 **/
static i40e_status i40e_nvmupd_state_init(struct i40e_hw *hw,
					  struct i40e_nvm_access *cmd,
					  u8 *bytes, int *perrno)
{
	i40e_status status = 0;
	enum i40e_nvmupd_cmd upd_cmd;

	upd_cmd = i40e_nvmupd_validate_command(hw, cmd, perrno);

	switch (upd_cmd) {
	case I40E_NVMUPD_READ_SA:
		status = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
		if (status) {
			*perrno = i40e_aq_rc_to_posix(status,
						     hw->aq.asq_last_status);
		} else {
			status = i40e_nvmupd_nvm_read(hw, cmd, bytes, perrno);
			i40e_release_nvm(hw);
		}
		break;

	case I40E_NVMUPD_READ_SNT:
		status = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
		if (status) {
			*perrno = i40e_aq_rc_to_posix(status,
						     hw->aq.asq_last_status);
		} else {
			status = i40e_nvmupd_nvm_read(hw, cmd, bytes, perrno);
			if (status)
				i40e_release_nvm(hw);
			else
				hw->nvmupd_state = I40E_NVMUPD_STATE_READING;
		}
		break;

	case I40E_NVMUPD_WRITE_ERA:
		status = i40e_acquire_nvm(hw, I40E_RESOURCE_WRITE);
		if (status) {
			*perrno = i40e_aq_rc_to_posix(status,
						     hw->aq.asq_last_status);
		} else {
			status = i40e_nvmupd_nvm_erase(hw, cmd, perrno);
			if (status) {
				i40e_release_nvm(hw);
			} else {
				hw->nvm_release_on_done = true;
				hw->nvm_wait_opcode = i40e_aqc_opc_nvm_erase;
				hw->nvmupd_state = I40E_NVMUPD_STATE_INIT_WAIT;
			}
		}
		break;

	case I40E_NVMUPD_WRITE_SA:
		status = i40e_acquire_nvm(hw, I40E_RESOURCE_WRITE);
		if (status) {
			*perrno = i40e_aq_rc_to_posix(status,
						     hw->aq.asq_last_status);
		} else {
			status = i40e_nvmupd_nvm_write(hw, cmd, bytes, perrno);
			if (status) {
				i40e_release_nvm(hw);
			} else {
				hw->nvm_release_on_done = true;
				hw->nvm_wait_opcode = i40e_aqc_opc_nvm_update;
				hw->nvmupd_state = I40E_NVMUPD_STATE_INIT_WAIT;
			}
		}
		break;

	case I40E_NVMUPD_WRITE_SNT:
		status = i40e_acquire_nvm(hw, I40E_RESOURCE_WRITE);
		if (status) {
			*perrno = i40e_aq_rc_to_posix(status,
						     hw->aq.asq_last_status);
		} else {
			status = i40e_nvmupd_nvm_write(hw, cmd, bytes, perrno);
			if (status) {
				i40e_release_nvm(hw);
			} else {
				hw->nvm_wait_opcode = i40e_aqc_opc_nvm_update;
				hw->nvmupd_state = I40E_NVMUPD_STATE_WRITE_WAIT;
			}
		}
		break;

	case I40E_NVMUPD_CSUM_SA:
		status = i40e_acquire_nvm(hw, I40E_RESOURCE_WRITE);
		if (status) {
			*perrno = i40e_aq_rc_to_posix(status,
						     hw->aq.asq_last_status);
		} else {
			status = i40e_update_nvm_checksum(hw);
			if (status) {
				*perrno = hw->aq.asq_last_status ?
				   i40e_aq_rc_to_posix(status,
						       hw->aq.asq_last_status) :
				   -EIO;
				i40e_release_nvm(hw);
			} else {
				hw->nvm_release_on_done = true;
				hw->nvm_wait_opcode = i40e_aqc_opc_nvm_update;
				hw->nvmupd_state = I40E_NVMUPD_STATE_INIT_WAIT;
			}
		}
		break;

	case I40E_NVMUPD_EXEC_AQ:
		status = i40e_nvmupd_exec_aq(hw, cmd, bytes, perrno);
		break;

	case I40E_NVMUPD_GET_AQ_RESULT:
		status = i40e_nvmupd_get_aq_result(hw, cmd, bytes, perrno);
		break;

	default:
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVMUPD: bad cmd %s in init state\n",
			   i40e_nvm_update_state_str[upd_cmd]);
		status = I40E_ERR_NVM;
		*perrno = -ESRCH;
		break;
	}
	return status;
}

/**
 * i40e_nvmupd_state_reading - Handle NVM update state Reading
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @bytes: pointer to the data buffer
 * @perrno: pointer to return error code
 *
 * NVM ownership is already held.  Process legitimate commands and set any
 * change in state; reject all other commands.
 **/
static i40e_status i40e_nvmupd_state_reading(struct i40e_hw *hw,
					     struct i40e_nvm_access *cmd,
					     u8 *bytes, int *perrno)
{
	i40e_status status = 0;
	enum i40e_nvmupd_cmd upd_cmd;

	upd_cmd = i40e_nvmupd_validate_command(hw, cmd, perrno);

	switch (upd_cmd) {
	case I40E_NVMUPD_READ_SA:
	case I40E_NVMUPD_READ_CON:
		status = i40e_nvmupd_nvm_read(hw, cmd, bytes, perrno);
		break;

	case I40E_NVMUPD_READ_LCB:
		status = i40e_nvmupd_nvm_read(hw, cmd, bytes, perrno);
		i40e_release_nvm(hw);
		hw->nvmupd_state = I40E_NVMUPD_STATE_INIT;
		break;

	default:
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVMUPD: bad cmd %s in reading state.\n",
			   i40e_nvm_update_state_str[upd_cmd]);
		status = I40E_NOT_SUPPORTED;
		*perrno = -ESRCH;
		break;
	}
	return status;
}

/**
 * i40e_nvmupd_state_writing - Handle NVM update state Writing
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @bytes: pointer to the data buffer
 * @perrno: pointer to return error code
 *
 * NVM ownership is already held.  Process legitimate commands and set any
 * change in state; reject all other commands
 **/
static i40e_status i40e_nvmupd_state_writing(struct i40e_hw *hw,
					     struct i40e_nvm_access *cmd,
					     u8 *bytes, int *perrno)
{
	i40e_status status = 0;
	enum i40e_nvmupd_cmd upd_cmd;
	bool retry_attempt = false;

	upd_cmd = i40e_nvmupd_validate_command(hw, cmd, perrno);

retry:
	switch (upd_cmd) {
	case I40E_NVMUPD_WRITE_CON:
		status = i40e_nvmupd_nvm_write(hw, cmd, bytes, perrno);
		if (!status) {
			hw->nvm_wait_opcode = i40e_aqc_opc_nvm_update;
			hw->nvmupd_state = I40E_NVMUPD_STATE_WRITE_WAIT;
		}
		break;

	case I40E_NVMUPD_WRITE_LCB:
		status = i40e_nvmupd_nvm_write(hw, cmd, bytes, perrno);
		if (status) {
			*perrno = hw->aq.asq_last_status ?
				   i40e_aq_rc_to_posix(status,
						       hw->aq.asq_last_status) :
				   -EIO;
			hw->nvmupd_state = I40E_NVMUPD_STATE_INIT;
		} else {
			hw->nvm_release_on_done = true;
			hw->nvm_wait_opcode = i40e_aqc_opc_nvm_update;
			hw->nvmupd_state = I40E_NVMUPD_STATE_INIT_WAIT;
		}
		break;

	case I40E_NVMUPD_CSUM_CON:
		/* Assumes the caller has acquired the nvm */
		status = i40e_update_nvm_checksum(hw);
		if (status) {
			*perrno = hw->aq.asq_last_status ?
				   i40e_aq_rc_to_posix(status,
						       hw->aq.asq_last_status) :
				   -EIO;
			hw->nvmupd_state = I40E_NVMUPD_STATE_INIT;
		} else {
			hw->nvm_wait_opcode = i40e_aqc_opc_nvm_update;
			hw->nvmupd_state = I40E_NVMUPD_STATE_WRITE_WAIT;
		}
		break;

	case I40E_NVMUPD_CSUM_LCB:
		/* Assumes the caller has acquired the nvm */
		status = i40e_update_nvm_checksum(hw);
		if (status) {
			*perrno = hw->aq.asq_last_status ?
				   i40e_aq_rc_to_posix(status,
						       hw->aq.asq_last_status) :
				   -EIO;
			hw->nvmupd_state = I40E_NVMUPD_STATE_INIT;
		} else {
			hw->nvm_release_on_done = true;
			hw->nvm_wait_opcode = i40e_aqc_opc_nvm_update;
			hw->nvmupd_state = I40E_NVMUPD_STATE_INIT_WAIT;
		}
		break;

	default:
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVMUPD: bad cmd %s in writing state.\n",
			   i40e_nvm_update_state_str[upd_cmd]);
		status = I40E_NOT_SUPPORTED;
		*perrno = -ESRCH;
		break;
	}

	/* In some circumstances, a multi-write transaction takes longer
	 * than the default 3 minute timeout on the write semaphore.  If
	 * the write failed with an EBUSY status, this is likely the problem,
	 * so here we try to reacquire the semaphore then retry the write.
	 * We only do one retry, then give up.
	 */
	if (status && (hw->aq.asq_last_status == I40E_AQ_RC_EBUSY) &&
	    !retry_attempt) {
		i40e_status old_status = status;
		u32 old_asq_status = hw->aq.asq_last_status;
		u32 gtime;

		gtime = rd32(hw, I40E_GLVFGEN_TIMER);
		if (gtime >= hw->nvm.hw_semaphore_timeout) {
			i40e_debug(hw, I40E_DEBUG_ALL,
				   "NVMUPD: write semaphore expired (%d >= %lld), retrying\n",
				   gtime, hw->nvm.hw_semaphore_timeout);
			i40e_release_nvm(hw);
			status = i40e_acquire_nvm(hw, I40E_RESOURCE_WRITE);
			if (status) {
				i40e_debug(hw, I40E_DEBUG_ALL,
					   "NVMUPD: write semaphore reacquire failed aq_err = %d\n",
					   hw->aq.asq_last_status);
				status = old_status;
				hw->aq.asq_last_status = old_asq_status;
			} else {
				retry_attempt = true;
				goto retry;
			}
		}
	}

	return status;
}

/**
 * i40e_nvmupd_check_wait_event - handle NVM update operation events
 * @hw: pointer to the hardware structure
 * @opcode: the event that just happened
 **/
void i40e_nvmupd_check_wait_event(struct i40e_hw *hw, u16 opcode)
{
	if (opcode == hw->nvm_wait_opcode) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVMUPD: clearing wait on opcode 0x%04x\n", opcode);
		if (hw->nvm_release_on_done) {
			i40e_release_nvm(hw);
			hw->nvm_release_on_done = false;
		}
		hw->nvm_wait_opcode = 0;

		if (hw->aq.arq_last_status) {
			hw->nvmupd_state = I40E_NVMUPD_STATE_ERROR;
			return;
		}

		switch (hw->nvmupd_state) {
		case I40E_NVMUPD_STATE_INIT_WAIT:
			hw->nvmupd_state = I40E_NVMUPD_STATE_INIT;
			break;

		case I40E_NVMUPD_STATE_WRITE_WAIT:
			hw->nvmupd_state = I40E_NVMUPD_STATE_WRITING;
			break;

		default:
			break;
		}
	}
}

/**
 * i40e_nvmupd_validate_command - Validate given command
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @perrno: pointer to return error code
 *
 * Return one of the valid command types or I40E_NVMUPD_INVALID
 **/
static enum i40e_nvmupd_cmd i40e_nvmupd_validate_command(struct i40e_hw *hw,
						 struct i40e_nvm_access *cmd,
						 int *perrno)
{
	enum i40e_nvmupd_cmd upd_cmd;
	u8 module, transaction;

	/* anything that doesn't match a recognized case is an error */
	upd_cmd = I40E_NVMUPD_INVALID;

	transaction = i40e_nvmupd_get_transaction(cmd->config);
	module = i40e_nvmupd_get_module(cmd->config);

	/* limits on data size */
	if ((cmd->data_size < 1) ||
	    (cmd->data_size > I40E_NVMUPD_MAX_DATA)) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_validate_command data_size %d\n",
			   cmd->data_size);
		*perrno = -EFAULT;
		return I40E_NVMUPD_INVALID;
	}

	switch (cmd->command) {
	case I40E_NVM_READ:
		switch (transaction) {
		case I40E_NVM_CON:
			upd_cmd = I40E_NVMUPD_READ_CON;
			break;
		case I40E_NVM_SNT:
			upd_cmd = I40E_NVMUPD_READ_SNT;
			break;
		case I40E_NVM_LCB:
			upd_cmd = I40E_NVMUPD_READ_LCB;
			break;
		case I40E_NVM_SA:
			upd_cmd = I40E_NVMUPD_READ_SA;
			break;
		case I40E_NVM_EXEC:
			if (module == 0xf)
				upd_cmd = I40E_NVMUPD_STATUS;
			else if (module == 0)
				upd_cmd = I40E_NVMUPD_GET_AQ_RESULT;
			break;
		}
		break;

	case I40E_NVM_WRITE:
		switch (transaction) {
		case I40E_NVM_CON:
			upd_cmd = I40E_NVMUPD_WRITE_CON;
			break;
		case I40E_NVM_SNT:
			upd_cmd = I40E_NVMUPD_WRITE_SNT;
			break;
		case I40E_NVM_LCB:
			upd_cmd = I40E_NVMUPD_WRITE_LCB;
			break;
		case I40E_NVM_SA:
			upd_cmd = I40E_NVMUPD_WRITE_SA;
			break;
		case I40E_NVM_ERA:
			upd_cmd = I40E_NVMUPD_WRITE_ERA;
			break;
		case I40E_NVM_CSUM:
			upd_cmd = I40E_NVMUPD_CSUM_CON;
			break;
		case (I40E_NVM_CSUM|I40E_NVM_SA):
			upd_cmd = I40E_NVMUPD_CSUM_SA;
			break;
		case (I40E_NVM_CSUM|I40E_NVM_LCB):
			upd_cmd = I40E_NVMUPD_CSUM_LCB;
			break;
		case I40E_NVM_EXEC:
			if (module == 0)
				upd_cmd = I40E_NVMUPD_EXEC_AQ;
			break;
		}
		break;
	}

	return upd_cmd;
}

/**
 * i40e_nvmupd_exec_aq - Run an AQ command
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @bytes: pointer to the data buffer
 * @perrno: pointer to return error code
 *
 * cmd structure contains identifiers and data buffer
 **/
static i40e_status i40e_nvmupd_exec_aq(struct i40e_hw *hw,
				       struct i40e_nvm_access *cmd,
				       u8 *bytes, int *perrno)
{
	struct i40e_asq_cmd_details cmd_details;
	i40e_status status;
	struct i40e_aq_desc *aq_desc;
	u32 buff_size = 0;
	u8 *buff = NULL;
	u32 aq_desc_len;
	u32 aq_data_len;

	i40e_debug(hw, I40E_DEBUG_NVM, "NVMUPD: %s\n", __func__);
	memset(&cmd_details, 0, sizeof(cmd_details));
	cmd_details.wb_desc = &hw->nvm_wb_desc;

	aq_desc_len = sizeof(struct i40e_aq_desc);
	memset(&hw->nvm_wb_desc, 0, aq_desc_len);

	/* get the aq descriptor */
	if (cmd->data_size < aq_desc_len) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "NVMUPD: not enough aq desc bytes for exec, size %d < %d\n",
			   cmd->data_size, aq_desc_len);
		*perrno = -EINVAL;
		return I40E_ERR_PARAM;
	}
	aq_desc = (struct i40e_aq_desc *)bytes;

	/* if data buffer needed, make sure it's ready */
	aq_data_len = cmd->data_size - aq_desc_len;
	buff_size = max_t(u32, aq_data_len, le16_to_cpu(aq_desc->datalen));
	if (buff_size) {
		if (!hw->nvm_buff.va) {
			status = i40e_allocate_virt_mem(hw, &hw->nvm_buff,
							hw->aq.asq_buf_size);
			if (status)
				i40e_debug(hw, I40E_DEBUG_NVM,
					   "NVMUPD: i40e_allocate_virt_mem for exec buff failed, %d\n",
					   status);
		}

		if (hw->nvm_buff.va) {
			buff = hw->nvm_buff.va;
			memcpy(buff, &bytes[aq_desc_len], aq_data_len);
		}
	}

	/* and away we go! */
	status = i40e_asq_send_command(hw, aq_desc, buff,
				       buff_size, &cmd_details);
	if (status) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_exec_aq err %s aq_err %s\n",
			   i40e_stat_str(hw, status),
			   i40e_aq_str(hw, hw->aq.asq_last_status));
		*perrno = i40e_aq_rc_to_posix(status, hw->aq.asq_last_status);
	}

	/* should we wait for a followup event? */
	if (cmd->offset) {
		hw->nvm_wait_opcode = cmd->offset;
		hw->nvmupd_state = I40E_NVMUPD_STATE_INIT_WAIT;
	}

	return status;
}

/**
 * i40e_nvmupd_get_aq_result - Get the results from the previous exec_aq
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @bytes: pointer to the data buffer
 * @perrno: pointer to return error code
 *
 * cmd structure contains identifiers and data buffer
 **/
static i40e_status i40e_nvmupd_get_aq_result(struct i40e_hw *hw,
					     struct i40e_nvm_access *cmd,
					     u8 *bytes, int *perrno)
{
	u32 aq_total_len;
	u32 aq_desc_len;
	int remainder;
	u8 *buff;

	i40e_debug(hw, I40E_DEBUG_NVM, "NVMUPD: %s\n", __func__);

	aq_desc_len = sizeof(struct i40e_aq_desc);
	aq_total_len = aq_desc_len + le16_to_cpu(hw->nvm_wb_desc.datalen);

	/* check offset range */
	if (cmd->offset > aq_total_len) {
		i40e_debug(hw, I40E_DEBUG_NVM, "%s: offset too big %d > %d\n",
			   __func__, cmd->offset, aq_total_len);
		*perrno = -EINVAL;
		return I40E_ERR_PARAM;
	}

	/* check copylength range */
	if (cmd->data_size > (aq_total_len - cmd->offset)) {
		int new_len = aq_total_len - cmd->offset;

		i40e_debug(hw, I40E_DEBUG_NVM, "%s: copy length %d too big, trimming to %d\n",
			   __func__, cmd->data_size, new_len);
		cmd->data_size = new_len;
	}

	remainder = cmd->data_size;
	if (cmd->offset < aq_desc_len) {
		u32 len = aq_desc_len - cmd->offset;

		len = min(len, cmd->data_size);
		i40e_debug(hw, I40E_DEBUG_NVM, "%s: aq_desc bytes %d to %d\n",
			   __func__, cmd->offset, cmd->offset + len);

		buff = ((u8 *)&hw->nvm_wb_desc) + cmd->offset;
		memcpy(bytes, buff, len);

		bytes += len;
		remainder -= len;
		buff = hw->nvm_buff.va;
	} else {
		buff = hw->nvm_buff.va + (cmd->offset - aq_desc_len);
	}

	if (remainder > 0) {
		int start_byte = buff - (u8 *)hw->nvm_buff.va;

		i40e_debug(hw, I40E_DEBUG_NVM, "%s: databuf bytes %d to %d\n",
			   __func__, start_byte, start_byte + remainder);
		memcpy(bytes, buff, remainder);
	}

	return 0;
}

/**
 * i40e_nvmupd_nvm_read - Read NVM
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @bytes: pointer to the data buffer
 * @perrno: pointer to return error code
 *
 * cmd structure contains identifiers and data buffer
 **/
static i40e_status i40e_nvmupd_nvm_read(struct i40e_hw *hw,
					struct i40e_nvm_access *cmd,
					u8 *bytes, int *perrno)
{
	struct i40e_asq_cmd_details cmd_details;
	i40e_status status;
	u8 module, transaction;
	bool last;

	transaction = i40e_nvmupd_get_transaction(cmd->config);
	module = i40e_nvmupd_get_module(cmd->config);
	last = (transaction == I40E_NVM_LCB) || (transaction == I40E_NVM_SA);

	memset(&cmd_details, 0, sizeof(cmd_details));
	cmd_details.wb_desc = &hw->nvm_wb_desc;

	status = i40e_aq_read_nvm(hw, module, cmd->offset, (u16)cmd->data_size,
				  bytes, last, &cmd_details);
	if (status) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_nvm_read mod 0x%x  off 0x%x  len 0x%x\n",
			   module, cmd->offset, cmd->data_size);
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_nvm_read status %d aq %d\n",
			   status, hw->aq.asq_last_status);
		*perrno = i40e_aq_rc_to_posix(status, hw->aq.asq_last_status);
	}

	return status;
}

/**
 * i40e_nvmupd_nvm_erase - Erase an NVM module
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @perrno: pointer to return error code
 *
 * module, offset, data_size and data are in cmd structure
 **/
static i40e_status i40e_nvmupd_nvm_erase(struct i40e_hw *hw,
					 struct i40e_nvm_access *cmd,
					 int *perrno)
{
	i40e_status status = 0;
	struct i40e_asq_cmd_details cmd_details;
	u8 module, transaction;
	bool last;

	transaction = i40e_nvmupd_get_transaction(cmd->config);
	module = i40e_nvmupd_get_module(cmd->config);
	last = (transaction & I40E_NVM_LCB);

	memset(&cmd_details, 0, sizeof(cmd_details));
	cmd_details.wb_desc = &hw->nvm_wb_desc;

	status = i40e_aq_erase_nvm(hw, module, cmd->offset, (u16)cmd->data_size,
				   last, &cmd_details);
	if (status) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_nvm_erase mod 0x%x  off 0x%x len 0x%x\n",
			   module, cmd->offset, cmd->data_size);
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_nvm_erase status %d aq %d\n",
			   status, hw->aq.asq_last_status);
		*perrno = i40e_aq_rc_to_posix(status, hw->aq.asq_last_status);
	}

	return status;
}

/**
 * i40e_nvmupd_nvm_write - Write NVM
 * @hw: pointer to hardware structure
 * @cmd: pointer to nvm update command buffer
 * @bytes: pointer to the data buffer
 * @perrno: pointer to return error code
 *
 * module, offset, data_size and data are in cmd structure
 **/
static i40e_status i40e_nvmupd_nvm_write(struct i40e_hw *hw,
					 struct i40e_nvm_access *cmd,
					 u8 *bytes, int *perrno)
{
	i40e_status status = 0;
	struct i40e_asq_cmd_details cmd_details;
	u8 module, transaction;
	bool last;

	transaction = i40e_nvmupd_get_transaction(cmd->config);
	module = i40e_nvmupd_get_module(cmd->config);
	last = (transaction & I40E_NVM_LCB);

	memset(&cmd_details, 0, sizeof(cmd_details));
	cmd_details.wb_desc = &hw->nvm_wb_desc;

	status = i40e_aq_update_nvm(hw, module, cmd->offset,
				    (u16)cmd->data_size, bytes, last,
				    &cmd_details);
	if (status) {
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_nvm_write mod 0x%x off 0x%x len 0x%x\n",
			   module, cmd->offset, cmd->data_size);
		i40e_debug(hw, I40E_DEBUG_NVM,
			   "i40e_nvmupd_nvm_write status %d aq %d\n",
			   status, hw->aq.asq_last_status);
		*perrno = i40e_aq_rc_to_posix(status, hw->aq.asq_last_status);
	}

	return status;
}
