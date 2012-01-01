/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "e1000.h"

enum e1000_mng_mode {
	e1000_mng_mode_none = 0,
	e1000_mng_mode_asf,
	e1000_mng_mode_pt,
	e1000_mng_mode_ipmi,
	e1000_mng_mode_host_if_only
};

#define E1000_FACTPS_MNGCG		0x20000000

/* Intel(R) Active Management Technology signature */
#define E1000_IAMT_SIGNATURE		0x544D4149

/**
 *  e1000_calculate_checksum - Calculate checksum for buffer
 *  @buffer: pointer to EEPROM
 *  @length: size of EEPROM to calculate a checksum for
 *
 *  Calculates the checksum for some buffer on a specified length.  The
 *  checksum calculated is returned.
 **/
static u8 e1000_calculate_checksum(u8 *buffer, u32 length)
{
	u32 i;
	u8 sum = 0;

	if (!buffer)
		return 0;

	for (i = 0; i < length; i++)
		sum += buffer[i];

	return (u8)(0 - sum);
}

/**
 *  e1000_mng_enable_host_if - Checks host interface is enabled
 *  @hw: pointer to the HW structure
 *
 *  Returns E1000_success upon success, else E1000_ERR_HOST_INTERFACE_COMMAND
 *
 *  This function checks whether the HOST IF is enabled for command operation
 *  and also checks whether the previous command is completed.  It busy waits
 *  in case of previous command is not completed.
 **/
static s32 e1000_mng_enable_host_if(struct e1000_hw *hw)
{
	u32 hicr;
	u8 i;

	if (!(hw->mac.arc_subsystem_valid)) {
		e_dbg("ARC subsystem not valid.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Check that the host interface is enabled. */
	hicr = er32(HICR);
	if ((hicr & E1000_HICR_EN) == 0) {
		e_dbg("E1000_HOST_EN bit disabled.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}
	/* check the previous command is completed */
	for (i = 0; i < E1000_MNG_DHCP_COMMAND_TIMEOUT; i++) {
		hicr = er32(HICR);
		if (!(hicr & E1000_HICR_C))
			break;
		mdelay(1);
	}

	if (i == E1000_MNG_DHCP_COMMAND_TIMEOUT) {
		e_dbg("Previous command timeout failed .\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	return 0;
}

/**
 *  e1000e_check_mng_mode_generic - check management mode
 *  @hw: pointer to the HW structure
 *
 *  Reads the firmware semaphore register and returns true (>0) if
 *  manageability is enabled, else false (0).
 **/
bool e1000e_check_mng_mode_generic(struct e1000_hw *hw)
{
	u32 fwsm = er32(FWSM);

	return (fwsm & E1000_FWSM_MODE_MASK) ==
	    (E1000_MNG_IAMT_MODE << E1000_FWSM_MODE_SHIFT);
}

/**
 *  e1000e_enable_tx_pkt_filtering - Enable packet filtering on Tx
 *  @hw: pointer to the HW structure
 *
 *  Enables packet filtering on transmit packets if manageability is enabled
 *  and host interface is enabled.
 **/
bool e1000e_enable_tx_pkt_filtering(struct e1000_hw *hw)
{
	struct e1000_host_mng_dhcp_cookie *hdr = &hw->mng_cookie;
	u32 *buffer = (u32 *)&hw->mng_cookie;
	u32 offset;
	s32 ret_val, hdr_csum, csum;
	u8 i, len;

	hw->mac.tx_pkt_filtering = true;

	/* No manageability, no filtering */
	if (!e1000e_check_mng_mode(hw)) {
		hw->mac.tx_pkt_filtering = false;
		goto out;
	}

	/*
	 * If we can't read from the host interface for whatever
	 * reason, disable filtering.
	 */
	ret_val = e1000_mng_enable_host_if(hw);
	if (ret_val) {
		hw->mac.tx_pkt_filtering = false;
		goto out;
	}

	/* Read in the header.  Length and offset are in dwords. */
	len = E1000_MNG_DHCP_COOKIE_LENGTH >> 2;
	offset = E1000_MNG_DHCP_COOKIE_OFFSET >> 2;
	for (i = 0; i < len; i++)
		*(buffer + i) = E1000_READ_REG_ARRAY(hw, E1000_HOST_IF,
						     offset + i);
	hdr_csum = hdr->checksum;
	hdr->checksum = 0;
	csum = e1000_calculate_checksum((u8 *)hdr,
					E1000_MNG_DHCP_COOKIE_LENGTH);
	/*
	 * If either the checksums or signature don't match, then
	 * the cookie area isn't considered valid, in which case we
	 * take the safe route of assuming Tx filtering is enabled.
	 */
	if ((hdr_csum != csum) || (hdr->signature != E1000_IAMT_SIGNATURE)) {
		hw->mac.tx_pkt_filtering = true;
		goto out;
	}

	/* Cookie area is valid, make the final check for filtering. */
	if (!(hdr->status & E1000_MNG_DHCP_COOKIE_STATUS_PARSING)) {
		hw->mac.tx_pkt_filtering = false;
		goto out;
	}

out:
	return hw->mac.tx_pkt_filtering;
}

/**
 *  e1000_mng_write_cmd_header - Writes manageability command header
 *  @hw: pointer to the HW structure
 *  @hdr: pointer to the host interface command header
 *
 *  Writes the command header after does the checksum calculation.
 **/
static s32 e1000_mng_write_cmd_header(struct e1000_hw *hw,
				      struct e1000_host_mng_command_header *hdr)
{
	u16 i, length = sizeof(struct e1000_host_mng_command_header);

	/* Write the whole command header structure with new checksum. */

	hdr->checksum = e1000_calculate_checksum((u8 *)hdr, length);

	length >>= 2;
	/* Write the relevant command block into the ram area. */
	for (i = 0; i < length; i++) {
		E1000_WRITE_REG_ARRAY(hw, E1000_HOST_IF, i, *((u32 *)hdr + i));
		e1e_flush();
	}

	return 0;
}

/**
 *  e1000_mng_host_if_write - Write to the manageability host interface
 *  @hw: pointer to the HW structure
 *  @buffer: pointer to the host interface buffer
 *  @length: size of the buffer
 *  @offset: location in the buffer to write to
 *  @sum: sum of the data (not checksum)
 *
 *  This function writes the buffer content at the offset given on the host if.
 *  It also does alignment considerations to do the writes in most efficient
 *  way.  Also fills up the sum of the buffer in *buffer parameter.
 **/
static s32 e1000_mng_host_if_write(struct e1000_hw *hw, u8 *buffer,
				   u16 length, u16 offset, u8 *sum)
{
	u8 *tmp;
	u8 *bufptr = buffer;
	u32 data = 0;
	u16 remaining, i, j, prev_bytes;

	/* sum = only sum of the data and it is not checksum */

	if (length == 0 || offset + length > E1000_HI_MAX_MNG_DATA_LENGTH)
		return -E1000_ERR_PARAM;

	tmp = (u8 *)&data;
	prev_bytes = offset & 0x3;
	offset >>= 2;

	if (prev_bytes) {
		data = E1000_READ_REG_ARRAY(hw, E1000_HOST_IF, offset);
		for (j = prev_bytes; j < sizeof(u32); j++) {
			*(tmp + j) = *bufptr++;
			*sum += *(tmp + j);
		}
		E1000_WRITE_REG_ARRAY(hw, E1000_HOST_IF, offset, data);
		length -= j - prev_bytes;
		offset++;
	}

	remaining = length & 0x3;
	length -= remaining;

	/* Calculate length in DWORDs */
	length >>= 2;

	/*
	 * The device driver writes the relevant command block into the
	 * ram area.
	 */
	for (i = 0; i < length; i++) {
		for (j = 0; j < sizeof(u32); j++) {
			*(tmp + j) = *bufptr++;
			*sum += *(tmp + j);
		}

		E1000_WRITE_REG_ARRAY(hw, E1000_HOST_IF, offset + i, data);
	}
	if (remaining) {
		for (j = 0; j < sizeof(u32); j++) {
			if (j < remaining)
				*(tmp + j) = *bufptr++;
			else
				*(tmp + j) = 0;

			*sum += *(tmp + j);
		}
		E1000_WRITE_REG_ARRAY(hw, E1000_HOST_IF, offset + i, data);
	}

	return 0;
}

/**
 *  e1000e_mng_write_dhcp_info - Writes DHCP info to host interface
 *  @hw: pointer to the HW structure
 *  @buffer: pointer to the host interface
 *  @length: size of the buffer
 *
 *  Writes the DHCP information to the host interface.
 **/
s32 e1000e_mng_write_dhcp_info(struct e1000_hw *hw, u8 *buffer, u16 length)
{
	struct e1000_host_mng_command_header hdr;
	s32 ret_val;
	u32 hicr;

	hdr.command_id = E1000_MNG_DHCP_TX_PAYLOAD_CMD;
	hdr.command_length = length;
	hdr.reserved1 = 0;
	hdr.reserved2 = 0;
	hdr.checksum = 0;

	/* Enable the host interface */
	ret_val = e1000_mng_enable_host_if(hw);
	if (ret_val)
		return ret_val;

	/* Populate the host interface with the contents of "buffer". */
	ret_val = e1000_mng_host_if_write(hw, buffer, length,
					  sizeof(hdr), &(hdr.checksum));
	if (ret_val)
		return ret_val;

	/* Write the manageability command header */
	ret_val = e1000_mng_write_cmd_header(hw, &hdr);
	if (ret_val)
		return ret_val;

	/* Tell the ARC a new command is pending. */
	hicr = er32(HICR);
	ew32(HICR, hicr | E1000_HICR_C);

	return 0;
}

/**
 *  e1000e_enable_mng_pass_thru - Check if management passthrough is needed
 *  @hw: pointer to the HW structure
 *
 *  Verifies the hardware needs to leave interface enabled so that frames can
 *  be directed to and from the management interface.
 **/
bool e1000e_enable_mng_pass_thru(struct e1000_hw *hw)
{
	u32 manc;
	u32 fwsm, factps;
	bool ret_val = false;

	manc = er32(MANC);

	if (!(manc & E1000_MANC_RCV_TCO_EN))
		goto out;

	if (hw->mac.has_fwsm) {
		fwsm = er32(FWSM);
		factps = er32(FACTPS);

		if (!(factps & E1000_FACTPS_MNGCG) &&
		    ((fwsm & E1000_FWSM_MODE_MASK) ==
		     (e1000_mng_mode_pt << E1000_FWSM_MODE_SHIFT))) {
			ret_val = true;
			goto out;
		}
	} else if ((hw->mac.type == e1000_82574) ||
		   (hw->mac.type == e1000_82583)) {
		u16 data;

		factps = er32(FACTPS);
		e1000_read_nvm(hw, NVM_INIT_CONTROL2_REG, 1, &data);

		if (!(factps & E1000_FACTPS_MNGCG) &&
		    ((data & E1000_NVM_INIT_CTRL2_MNGM) ==
		     (e1000_mng_mode_pt << 13))) {
			ret_val = true;
			goto out;
		}
	} else if ((manc & E1000_MANC_SMBUS_EN) &&
		   !(manc & E1000_MANC_ASF_EN)) {
		ret_val = true;
		goto out;
	}

out:
	return ret_val;
}
