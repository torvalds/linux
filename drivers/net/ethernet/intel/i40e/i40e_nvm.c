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
	nvm->sr_size = (1 << sr_size) * I40E_SR_WORDS_IN_1KB;

	/* Check if we are in the normal or blank NVM programming mode */
	fla = rd32(hw, I40E_GLNVM_FLA);
	if (fla & I40E_GLNVM_FLA_LOCKED_MASK) { /* Normal programming mode */
		/* Max NVM timeout */
		nvm->timeout = I40E_MAX_NVM_TIMEOUT;
		nvm->blank_nvm_mode = false;
	} else { /* Blank programming mode */
		nvm->blank_nvm_mode = true;
		ret_code = I40E_ERR_NVM_BLANK_MODE;
		hw_dbg(hw, "NVM init error: unsupported blank mode.\n");
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
	u64 time = 0;

	if (hw->nvm.blank_nvm_mode)
		goto i40e_i40e_acquire_nvm_exit;

	ret_code = i40e_aq_request_resource(hw, I40E_NVM_RESOURCE_ID, access,
					    0, &time, NULL);
	/* Reading the Global Device Timer */
	gtime = rd32(hw, I40E_GLVFGEN_TIMER);

	/* Store the timeout */
	hw->nvm.hw_semaphore_timeout = I40E_MS_TO_GTIME(time) + gtime;

	if (ret_code) {
		/* Set the polling timeout */
		if (time > I40E_MAX_NVM_TIMEOUT)
			timeout = I40E_MS_TO_GTIME(I40E_MAX_NVM_TIMEOUT)
				  + gtime;
		else
			timeout = hw->nvm.hw_semaphore_timeout;
		/* Poll until the current NVM owner timeouts */
		while (gtime < timeout) {
			usleep_range(10000, 20000);
			ret_code = i40e_aq_request_resource(hw,
							I40E_NVM_RESOURCE_ID,
							access, 0, &time,
							NULL);
			if (!ret_code) {
				hw->nvm.hw_semaphore_timeout =
						I40E_MS_TO_GTIME(time) + gtime;
				break;
			}
			gtime = rd32(hw, I40E_GLVFGEN_TIMER);
		}
		if (ret_code) {
			hw->nvm.hw_semaphore_timeout = 0;
			hw->nvm.hw_semaphore_wait =
						I40E_MS_TO_GTIME(time) + gtime;
			hw_dbg(hw, "NVM acquire timed out, wait %llu ms before trying again.\n",
				  time);
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
	if (!hw->nvm.blank_nvm_mode)
		i40e_aq_release_resource(hw, I40E_NVM_RESOURCE_ID, 0, NULL);
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
		hw_dbg(hw, "Done bit in GLNVM_SRCTL not set\n");
	return ret_code;
}

/**
 * i40e_read_nvm_word - Reads Shadow RAM
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using the GLNVM_SRCTL register.
 **/
i40e_status i40e_read_nvm_word(struct i40e_hw *hw, u16 offset,
					 u16 *data)
{
	i40e_status ret_code = I40E_ERR_TIMEOUT;
	u32 sr_reg;

	if (offset >= hw->nvm.sr_size) {
		hw_dbg(hw, "NVM read error: Offset beyond Shadow RAM limit.\n");
		ret_code = I40E_ERR_PARAM;
		goto read_nvm_exit;
	}

	/* Poll the done bit first */
	ret_code = i40e_poll_sr_srctl_done_bit(hw);
	if (!ret_code) {
		/* Write the address and start reading */
		sr_reg = (u32)(offset << I40E_GLNVM_SRCTL_ADDR_SHIFT) |
			 (1 << I40E_GLNVM_SRCTL_START_SHIFT);
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
		hw_dbg(hw, "NVM read error: Couldn't access Shadow RAM address: 0x%x\n",
			  offset);

read_nvm_exit:
	return ret_code;
}

/**
 * i40e_read_nvm_buffer - Reads Shadow RAM buffer
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF).
 * @words: (in) number of words to read; (out) number of words actually read
 * @data: words read from the Shadow RAM
 *
 * Reads 16 bit words (data buffer) from the SR using the i40e_read_nvm_srrd()
 * method. The buffer read is preceded by the NVM ownership take
 * and followed by the release.
 **/
i40e_status i40e_read_nvm_buffer(struct i40e_hw *hw, u16 offset,
					   u16 *words, u16 *data)
{
	i40e_status ret_code = 0;
	u16 index, word;

	/* Loop thru the selected region */
	for (word = 0; word < *words; word++) {
		index = offset + word;
		ret_code = i40e_read_nvm_word(hw, index, &data[word]);
		if (ret_code)
			break;
	}

	/* Update the number of words read from the Shadow RAM */
	*words = word;

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
	i40e_status ret_code = 0;
	u16 pcie_alt_module = 0;
	u16 checksum_local = 0;
	u16 vpd_module = 0;
	u16 word = 0;
	u32 i = 0;

	/* read pointer to VPD area */
	ret_code = i40e_read_nvm_word(hw, I40E_SR_VPD_PTR, &vpd_module);
	if (ret_code) {
		ret_code = I40E_ERR_NVM_CHECKSUM;
		goto i40e_calc_nvm_checksum_exit;
	}

	/* read pointer to PCIe Alt Auto-load module */
	ret_code = i40e_read_nvm_word(hw, I40E_SR_PCIE_ALT_AUTO_LOAD_PTR,
				       &pcie_alt_module);
	if (ret_code) {
		ret_code = I40E_ERR_NVM_CHECKSUM;
		goto i40e_calc_nvm_checksum_exit;
	}

	/* Calculate SW checksum that covers the whole 64kB shadow RAM
	 * except the VPD and PCIe ALT Auto-load modules
	 */
	for (i = 0; i < hw->nvm.sr_size; i++) {
		/* Skip Checksum word */
		if (i == I40E_SR_SW_CHECKSUM_WORD)
			i++;
		/* Skip VPD module (convert byte size to word count) */
		if (i == (u32)vpd_module) {
			i += (I40E_SR_VPD_MODULE_MAX_SIZE / 2);
			if (i >= hw->nvm.sr_size)
				break;
		}
		/* Skip PCIe ALT module (convert byte size to word count) */
		if (i == (u32)pcie_alt_module) {
			i += (I40E_SR_PCIE_ALT_MODULE_MAX_SIZE / 2);
			if (i >= hw->nvm.sr_size)
				break;
		}

		ret_code = i40e_read_nvm_word(hw, (u16)i, &word);
		if (ret_code) {
			ret_code = I40E_ERR_NVM_CHECKSUM;
			goto i40e_calc_nvm_checksum_exit;
		}
		checksum_local += word;
	}

	*checksum = (u16)I40E_SR_SW_CHECKSUM_BASE - checksum_local;

i40e_calc_nvm_checksum_exit:
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

	ret_code = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret_code)
		goto i40e_validate_nvm_checksum_exit;

	ret_code = i40e_calc_nvm_checksum(hw, &checksum_local);
	if (ret_code)
		goto i40e_validate_nvm_checksum_free;

	/* Do not use i40e_read_nvm_word() because we do not want to take
	 * the synchronization semaphores twice here.
	 */
	i40e_read_nvm_word(hw, I40E_SR_SW_CHECKSUM_WORD, &checksum_sr);

	/* Verify read checksum from EEPROM is the same as
	 * calculated checksum
	 */
	if (checksum_local != checksum_sr)
		ret_code = I40E_ERR_NVM_CHECKSUM;

	/* If the user cares, return the calculated checksum */
	if (checksum)
		*checksum = checksum_local;

i40e_validate_nvm_checksum_free:
	i40e_release_nvm(hw);

i40e_validate_nvm_checksum_exit:
	return ret_code;
}
