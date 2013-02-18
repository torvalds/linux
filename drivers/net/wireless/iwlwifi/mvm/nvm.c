/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include "iwl-trans.h"
#include "mvm.h"
#include "iwl-eeprom-parse.h"
#include "iwl-eeprom-read.h"
#include "iwl-nvm-parse.h"

/* list of NVM sections we are allowed/need to read */
static const int nvm_to_read[] = {
	NVM_SECTION_TYPE_HW,
	NVM_SECTION_TYPE_SW,
	NVM_SECTION_TYPE_CALIBRATION,
	NVM_SECTION_TYPE_PRODUCTION,
};

/* used to simplify the shared operations on NCM_ACCESS_CMD versions */
union iwl_nvm_access_cmd {
	struct iwl_nvm_access_cmd_ver1 ver1;
	struct iwl_nvm_access_cmd_ver2 ver2;
};
union iwl_nvm_access_resp {
	struct iwl_nvm_access_resp_ver1 ver1;
	struct iwl_nvm_access_resp_ver2 ver2;
};

static inline void iwl_nvm_fill_read_ver1(struct iwl_nvm_access_cmd_ver1 *cmd,
					  u16 offset, u16 length)
{
	cmd->offset = cpu_to_le16(offset);
	cmd->length = cpu_to_le16(length);
	cmd->cache_refresh = 1;
}

static inline void iwl_nvm_fill_read_ver2(struct iwl_nvm_access_cmd_ver2 *cmd,
					  u16 offset, u16 length, u16 section)
{
	cmd->offset = cpu_to_le16(offset);
	cmd->length = cpu_to_le16(length);
	cmd->type = cpu_to_le16(section);
}

static int iwl_nvm_read_chunk(struct iwl_mvm *mvm, u16 section,
			      u16 offset, u16 length, u8 *data)
{
	union iwl_nvm_access_cmd nvm_access_cmd;
	union iwl_nvm_access_resp *nvm_resp;
	struct iwl_rx_packet *pkt;
	struct iwl_host_cmd cmd = {
		.id = NVM_ACCESS_CMD,
		.flags = CMD_SYNC | CMD_WANT_SKB,
		.data = { &nvm_access_cmd, },
	};
	int ret, bytes_read, offset_read;
	u8 *resp_data;

	memset(&nvm_access_cmd, 0, sizeof(nvm_access_cmd));

	/* TODO: not sure family should be the decider, maybe FW version? */
	if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		iwl_nvm_fill_read_ver2(&(nvm_access_cmd.ver2),
				       offset, length, section);
		cmd.len[0] = sizeof(struct iwl_nvm_access_cmd_ver2);
	} else {
		iwl_nvm_fill_read_ver1(&(nvm_access_cmd.ver1),
				       offset, length);
		cmd.len[0] = sizeof(struct iwl_nvm_access_cmd_ver1);
	}

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret)
		return ret;

	pkt = cmd.resp_pkt;
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(mvm, "Bad return from NVM_ACCES_COMMAND (0x%08X)\n",
			pkt->hdr.flags);
		ret = -EIO;
		goto exit;
	}

	/* Extract NVM response */
	nvm_resp = (void *)pkt->data;
	if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		ret = le16_to_cpu(nvm_resp->ver2.status);
		bytes_read = le16_to_cpu(nvm_resp->ver2.length);
		offset_read = le16_to_cpu(nvm_resp->ver2.offset);
		resp_data = nvm_resp->ver2.data;
	} else {
		ret = le16_to_cpu(nvm_resp->ver1.length) <= 0;
		bytes_read = le16_to_cpu(nvm_resp->ver1.length);
		offset_read = le16_to_cpu(nvm_resp->ver1.offset);
		resp_data = nvm_resp->ver1.data;
	}
	if (ret) {
		IWL_ERR(mvm,
			"NVM access command failed with status %d (device: %s)\n",
			ret, mvm->cfg->name);
		ret = -EINVAL;
		goto exit;
	}

	if (offset_read != offset) {
		IWL_ERR(mvm, "NVM ACCESS response with invalid offset %d\n",
			offset_read);
		ret = -EINVAL;
		goto exit;
	}

	/* Write data to NVM */
	memcpy(data + offset, resp_data, bytes_read);
	ret = bytes_read;

exit:
	iwl_free_resp(&cmd);
	return ret;
}

/*
 * Reads an NVM section completely.
 * NICs prior to 7000 family doesn't have a real NVM, but just read
 * section 0 which is the EEPROM. Because the EEPROM reading is unlimited
 * by uCode, we need to manually check in this case that we don't
 * overflow and try to read more than the EEPROM size.
 * For 7000 family NICs, we supply the maximal size we can read, and
 * the uCode fills the response with as much data as we can,
 * without overflowing, so no check is needed.
 */
static int iwl_nvm_read_section(struct iwl_mvm *mvm, u16 section,
				u8 *data)
{
	u16 length, offset = 0;
	int ret;
	bool old_eeprom = mvm->cfg->device_family != IWL_DEVICE_FAMILY_7000;

	length = (iwlwifi_mod_params.amsdu_size_8K ? (8 * 1024) : (4 * 1024))
		- sizeof(union iwl_nvm_access_cmd)
		- sizeof(struct iwl_rx_packet);
	/*
	 * if length is greater than EEPROM size, truncate it because uCode
	 * doesn't check it by itself, and exit the loop when reached.
	 */
	if (old_eeprom && length > mvm->cfg->base_params->eeprom_size)
		length = mvm->cfg->base_params->eeprom_size;
	ret = length;

	/* Read the NVM until exhausted (reading less than requested) */
	while (ret == length) {
		ret = iwl_nvm_read_chunk(mvm, section, offset, length, data);
		if (ret < 0) {
			IWL_ERR(mvm,
				"Cannot read NVM from section %d offset %d, length %d\n",
				section, offset, length);
			return ret;
		}
		offset += ret;
		if (old_eeprom && offset == mvm->cfg->base_params->eeprom_size)
			break;
	}

	IWL_INFO(mvm, "NVM section %d read completed\n", section);
	return offset;
}

static struct iwl_nvm_data *
iwl_parse_nvm_sections(struct iwl_mvm *mvm)
{
	struct iwl_nvm_section *sections = mvm->nvm_sections;
	const __le16 *hw, *sw, *calib;

	/* Checking for required sections */
	if (!mvm->nvm_sections[NVM_SECTION_TYPE_SW].data ||
	    !mvm->nvm_sections[NVM_SECTION_TYPE_HW].data) {
		IWL_ERR(mvm, "Can't parse empty NVM sections\n");
		return NULL;
	}

	if (WARN_ON(!mvm->cfg))
		return NULL;

	hw = (const __le16 *)sections[NVM_SECTION_TYPE_HW].data;
	sw = (const __le16 *)sections[NVM_SECTION_TYPE_SW].data;
	calib = (const __le16 *)sections[NVM_SECTION_TYPE_CALIBRATION].data;
	return iwl_parse_nvm_data(mvm->trans->dev, mvm->cfg, hw, sw, calib);
}

int iwl_nvm_init(struct iwl_mvm *mvm)
{
	int ret, i, section;
	u8 *nvm_buffer, *temp;

	if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		/* TODO: find correct NVM max size for a section */
		nvm_buffer = kmalloc(mvm->cfg->base_params->eeprom_size,
				     GFP_KERNEL);
		if (!nvm_buffer)
			return -ENOMEM;
		for (i = 0; i < ARRAY_SIZE(nvm_to_read); i++) {
			section = nvm_to_read[i];
			/* we override the constness for initial read */
			ret = iwl_nvm_read_section(mvm, section, nvm_buffer);
			if (ret < 0)
				break;
			temp = kmemdup(nvm_buffer, ret, GFP_KERNEL);
			if (!temp) {
				ret = -ENOMEM;
				break;
			}
			mvm->nvm_sections[section].data = temp;
			mvm->nvm_sections[section].length = ret;
		}
		kfree(nvm_buffer);
		if (ret < 0)
			return ret;
	} else {
		/* allocate eeprom */
		mvm->eeprom_blob_size = mvm->cfg->base_params->eeprom_size;
		IWL_DEBUG_EEPROM(mvm->trans->dev, "NVM size = %zd\n",
				 mvm->eeprom_blob_size);
		mvm->eeprom_blob = kzalloc(mvm->eeprom_blob_size, GFP_KERNEL);
		if (!mvm->eeprom_blob)
			return -ENOMEM;

		ret = iwl_nvm_read_section(mvm, 0, mvm->eeprom_blob);
		if (ret != mvm->eeprom_blob_size) {
			IWL_ERR(mvm, "Read partial NVM %d/%zd\n",
				ret, mvm->eeprom_blob_size);
			kfree(mvm->eeprom_blob);
			mvm->eeprom_blob = NULL;
			return -EINVAL;
		}
	}

	ret = 0;
	if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_7000)
		mvm->nvm_data = iwl_parse_nvm_sections(mvm);
	else
		mvm->nvm_data =
			iwl_parse_eeprom_data(mvm->trans->dev,
					      mvm->cfg,
					      mvm->eeprom_blob,
					      mvm->eeprom_blob_size);

	if (!mvm->nvm_data) {
		kfree(mvm->eeprom_blob);
		mvm->eeprom_blob = NULL;
		ret = -ENOMEM;
	}

	return ret;
}
