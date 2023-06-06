// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2019, 2021 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/firmware.h>
#include <linux/rtnetlink.h>
#include "iwl-trans.h"
#include "iwl-csr.h"
#include "mvm.h"
#include "iwl-eeprom-parse.h"
#include "iwl-eeprom-read.h"
#include "iwl-nvm-parse.h"
#include "iwl-prph.h"
#include "fw/acpi.h"

/* Default NVM size to read */
#define IWL_NVM_DEFAULT_CHUNK_SIZE (2 * 1024)

#define NVM_WRITE_OPCODE 1
#define NVM_READ_OPCODE 0

/* load nvm chunk response */
enum {
	READ_NVM_CHUNK_SUCCEED = 0,
	READ_NVM_CHUNK_NOT_VALID_ADDRESS = 1
};

/*
 * prepare the NVM host command w/ the pointers to the nvm buffer
 * and send it to fw
 */
static int iwl_nvm_write_chunk(struct iwl_mvm *mvm, u16 section,
			       u16 offset, u16 length, const u8 *data)
{
	struct iwl_nvm_access_cmd nvm_access_cmd = {
		.offset = cpu_to_le16(offset),
		.length = cpu_to_le16(length),
		.type = cpu_to_le16(section),
		.op_code = NVM_WRITE_OPCODE,
	};
	struct iwl_host_cmd cmd = {
		.id = NVM_ACCESS_CMD,
		.len = { sizeof(struct iwl_nvm_access_cmd), length },
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &nvm_access_cmd, data },
		/* data may come from vmalloc, so use _DUP */
		.dataflags = { 0, IWL_HCMD_DFL_DUP },
	};
	struct iwl_rx_packet *pkt;
	struct iwl_nvm_access_resp *nvm_resp;
	int ret;

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret)
		return ret;

	pkt = cmd.resp_pkt;
	/* Extract & check NVM write response */
	nvm_resp = (void *)pkt->data;
	if (le16_to_cpu(nvm_resp->status) != READ_NVM_CHUNK_SUCCEED) {
		IWL_ERR(mvm,
			"NVM access write command failed for section %u (status = 0x%x)\n",
			section, le16_to_cpu(nvm_resp->status));
		ret = -EIO;
	}

	iwl_free_resp(&cmd);
	return ret;
}

static int iwl_nvm_read_chunk(struct iwl_mvm *mvm, u16 section,
			      u16 offset, u16 length, u8 *data)
{
	struct iwl_nvm_access_cmd nvm_access_cmd = {
		.offset = cpu_to_le16(offset),
		.length = cpu_to_le16(length),
		.type = cpu_to_le16(section),
		.op_code = NVM_READ_OPCODE,
	};
	struct iwl_nvm_access_resp *nvm_resp;
	struct iwl_rx_packet *pkt;
	struct iwl_host_cmd cmd = {
		.id = NVM_ACCESS_CMD,
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &nvm_access_cmd, },
	};
	int ret, bytes_read, offset_read;
	u8 *resp_data;

	cmd.len[0] = sizeof(struct iwl_nvm_access_cmd);

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret)
		return ret;

	pkt = cmd.resp_pkt;

	/* Extract NVM response */
	nvm_resp = (void *)pkt->data;
	ret = le16_to_cpu(nvm_resp->status);
	bytes_read = le16_to_cpu(nvm_resp->length);
	offset_read = le16_to_cpu(nvm_resp->offset);
	resp_data = nvm_resp->data;
	if (ret) {
		if ((offset != 0) &&
		    (ret == READ_NVM_CHUNK_NOT_VALID_ADDRESS)) {
			/*
			 * meaning of NOT_VALID_ADDRESS:
			 * driver try to read chunk from address that is
			 * multiple of 2K and got an error since addr is empty.
			 * meaning of (offset != 0): driver already
			 * read valid data from another chunk so this case
			 * is not an error.
			 */
			IWL_DEBUG_EEPROM(mvm->trans->dev,
					 "NVM access command failed on offset 0x%x since that section size is multiple 2K\n",
					 offset);
			ret = 0;
		} else {
			IWL_DEBUG_EEPROM(mvm->trans->dev,
					 "NVM access command failed with status %d (device: %s)\n",
					 ret, mvm->trans->name);
			ret = -ENODATA;
		}
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

static int iwl_nvm_write_section(struct iwl_mvm *mvm, u16 section,
				 const u8 *data, u16 length)
{
	int offset = 0;

	/* copy data in chunks of 2k (and remainder if any) */

	while (offset < length) {
		int chunk_size, ret;

		chunk_size = min(IWL_NVM_DEFAULT_CHUNK_SIZE,
				 length - offset);

		ret = iwl_nvm_write_chunk(mvm, section, offset,
					  chunk_size, data + offset);
		if (ret < 0)
			return ret;

		offset += chunk_size;
	}

	return 0;
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
				u8 *data, u32 size_read)
{
	u16 length, offset = 0;
	int ret;

	/* Set nvm section read length */
	length = IWL_NVM_DEFAULT_CHUNK_SIZE;

	ret = length;

	/* Read the NVM until exhausted (reading less than requested) */
	while (ret == length) {
		/* Check no memory assumptions fail and cause an overflow */
		if ((size_read + offset + length) >
		    mvm->trans->trans_cfg->base_params->eeprom_size) {
			IWL_ERR(mvm, "EEPROM size is too small for NVM\n");
			return -ENOBUFS;
		}

		ret = iwl_nvm_read_chunk(mvm, section, offset, length, data);
		if (ret < 0) {
			IWL_DEBUG_EEPROM(mvm->trans->dev,
					 "Cannot read NVM from section %d offset %d, length %d\n",
					 section, offset, length);
			return ret;
		}
		offset += ret;
	}

	iwl_nvm_fixups(mvm->trans->hw_id, section, data, offset);

	IWL_DEBUG_EEPROM(mvm->trans->dev,
			 "NVM section %d read completed\n", section);
	return offset;
}

static struct iwl_nvm_data *
iwl_parse_nvm_sections(struct iwl_mvm *mvm)
{
	struct iwl_nvm_section *sections = mvm->nvm_sections;
	const __be16 *hw;
	const __le16 *sw, *calib, *regulatory, *mac_override, *phy_sku;
	int regulatory_type;

	/* Checking for required sections */
	if (mvm->trans->cfg->nvm_type == IWL_NVM) {
		if (!mvm->nvm_sections[NVM_SECTION_TYPE_SW].data ||
		    !mvm->nvm_sections[mvm->cfg->nvm_hw_section_num].data) {
			IWL_ERR(mvm, "Can't parse empty OTP/NVM sections\n");
			return NULL;
		}
	} else {
		if (mvm->trans->cfg->nvm_type == IWL_NVM_SDP)
			regulatory_type = NVM_SECTION_TYPE_REGULATORY_SDP;
		else
			regulatory_type = NVM_SECTION_TYPE_REGULATORY;

		/* SW and REGULATORY sections are mandatory */
		if (!mvm->nvm_sections[NVM_SECTION_TYPE_SW].data ||
		    !mvm->nvm_sections[regulatory_type].data) {
			IWL_ERR(mvm,
				"Can't parse empty family 8000 OTP/NVM sections\n");
			return NULL;
		}
		/* MAC_OVERRIDE or at least HW section must exist */
		if (!mvm->nvm_sections[mvm->cfg->nvm_hw_section_num].data &&
		    !mvm->nvm_sections[NVM_SECTION_TYPE_MAC_OVERRIDE].data) {
			IWL_ERR(mvm,
				"Can't parse mac_address, empty sections\n");
			return NULL;
		}

		/* PHY_SKU section is mandatory in B0 */
		if (mvm->trans->cfg->nvm_type == IWL_NVM_EXT &&
		    !mvm->nvm_sections[NVM_SECTION_TYPE_PHY_SKU].data) {
			IWL_ERR(mvm,
				"Can't parse phy_sku in B0, empty sections\n");
			return NULL;
		}
	}

	hw = (const __be16 *)sections[mvm->cfg->nvm_hw_section_num].data;
	sw = (const __le16 *)sections[NVM_SECTION_TYPE_SW].data;
	calib = (const __le16 *)sections[NVM_SECTION_TYPE_CALIBRATION].data;
	mac_override =
		(const __le16 *)sections[NVM_SECTION_TYPE_MAC_OVERRIDE].data;
	phy_sku = (const __le16 *)sections[NVM_SECTION_TYPE_PHY_SKU].data;

	regulatory = mvm->trans->cfg->nvm_type == IWL_NVM_SDP ?
		(const __le16 *)sections[NVM_SECTION_TYPE_REGULATORY_SDP].data :
		(const __le16 *)sections[NVM_SECTION_TYPE_REGULATORY].data;

	return iwl_parse_nvm_data(mvm->trans, mvm->cfg, mvm->fw, hw, sw, calib,
				  regulatory, mac_override, phy_sku,
				  mvm->fw->valid_tx_ant, mvm->fw->valid_rx_ant);
}

/* Loads the NVM data stored in mvm->nvm_sections into the NIC */
int iwl_mvm_load_nvm_to_nic(struct iwl_mvm *mvm)
{
	int i, ret = 0;
	struct iwl_nvm_section *sections = mvm->nvm_sections;

	IWL_DEBUG_EEPROM(mvm->trans->dev, "'Write to NVM\n");

	for (i = 0; i < ARRAY_SIZE(mvm->nvm_sections); i++) {
		if (!mvm->nvm_sections[i].data || !mvm->nvm_sections[i].length)
			continue;
		ret = iwl_nvm_write_section(mvm, i, sections[i].data,
					    sections[i].length);
		if (ret < 0) {
			IWL_ERR(mvm, "iwl_mvm_send_cmd failed: %d\n", ret);
			break;
		}
	}
	return ret;
}

int iwl_nvm_init(struct iwl_mvm *mvm)
{
	int ret, section;
	u32 size_read = 0;
	u8 *nvm_buffer, *temp;
	const char *nvm_file_C = mvm->cfg->default_nvm_file_C_step;

	if (WARN_ON_ONCE(mvm->cfg->nvm_hw_section_num >= NVM_MAX_NUM_SECTIONS))
		return -EINVAL;

	/* load NVM values from nic */
	/* Read From FW NVM */
	IWL_DEBUG_EEPROM(mvm->trans->dev, "Read from NVM\n");

	nvm_buffer = kmalloc(mvm->trans->trans_cfg->base_params->eeprom_size,
			     GFP_KERNEL);
	if (!nvm_buffer)
		return -ENOMEM;
	for (section = 0; section < NVM_MAX_NUM_SECTIONS; section++) {
		/* we override the constness for initial read */
		ret = iwl_nvm_read_section(mvm, section, nvm_buffer,
					   size_read);
		if (ret == -ENODATA) {
			ret = 0;
			continue;
		}
		if (ret < 0)
			break;
		size_read += ret;
		temp = kmemdup(nvm_buffer, ret, GFP_KERNEL);
		if (!temp) {
			ret = -ENOMEM;
			break;
		}

		iwl_nvm_fixups(mvm->trans->hw_id, section, temp, ret);

		mvm->nvm_sections[section].data = temp;
		mvm->nvm_sections[section].length = ret;

#ifdef CONFIG_IWLWIFI_DEBUGFS
		switch (section) {
		case NVM_SECTION_TYPE_SW:
			mvm->nvm_sw_blob.data = temp;
			mvm->nvm_sw_blob.size  = ret;
			break;
		case NVM_SECTION_TYPE_CALIBRATION:
			mvm->nvm_calib_blob.data = temp;
			mvm->nvm_calib_blob.size  = ret;
			break;
		case NVM_SECTION_TYPE_PRODUCTION:
			mvm->nvm_prod_blob.data = temp;
			mvm->nvm_prod_blob.size  = ret;
			break;
		case NVM_SECTION_TYPE_PHY_SKU:
			mvm->nvm_phy_sku_blob.data = temp;
			mvm->nvm_phy_sku_blob.size  = ret;
			break;
		case NVM_SECTION_TYPE_REGULATORY_SDP:
		case NVM_SECTION_TYPE_REGULATORY:
			mvm->nvm_reg_blob.data = temp;
			mvm->nvm_reg_blob.size  = ret;
			break;
		default:
			if (section == mvm->cfg->nvm_hw_section_num) {
				mvm->nvm_hw_blob.data = temp;
				mvm->nvm_hw_blob.size = ret;
				break;
			}
		}
#endif
	}
	if (!size_read)
		IWL_ERR(mvm, "OTP is blank\n");
	kfree(nvm_buffer);

	/* Only if PNVM selected in the mod param - load external NVM  */
	if (mvm->nvm_file_name) {
		/* read External NVM file from the mod param */
		ret = iwl_read_external_nvm(mvm->trans, mvm->nvm_file_name,
					    mvm->nvm_sections);
		if (ret) {
			mvm->nvm_file_name = nvm_file_C;

			if ((ret == -EFAULT || ret == -ENOENT) &&
			    mvm->nvm_file_name) {
				/* in case nvm file was failed try again */
				ret = iwl_read_external_nvm(mvm->trans,
							    mvm->nvm_file_name,
							    mvm->nvm_sections);
				if (ret)
					return ret;
			} else {
				return ret;
			}
		}
	}

	/* parse the relevant nvm sections */
	mvm->nvm_data = iwl_parse_nvm_sections(mvm);
	if (!mvm->nvm_data)
		return -ENODATA;
	IWL_DEBUG_EEPROM(mvm->trans->dev, "nvm version = %x\n",
			 mvm->nvm_data->nvm_version);

	return ret < 0 ? ret : 0;
}

struct iwl_mcc_update_resp *
iwl_mvm_update_mcc(struct iwl_mvm *mvm, const char *alpha2,
		   enum iwl_mcc_source src_id)
{
	struct iwl_mcc_update_cmd mcc_update_cmd = {
		.mcc = cpu_to_le16(alpha2[0] << 8 | alpha2[1]),
		.source_id = (u8)src_id,
	};
	struct iwl_mcc_update_resp *resp_cp;
	struct iwl_rx_packet *pkt;
	struct iwl_host_cmd cmd = {
		.id = MCC_UPDATE_CMD,
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &mcc_update_cmd },
	};

	int ret;
	u32 status;
	int resp_len, n_channels;
	u16 mcc;

	if (WARN_ON_ONCE(!iwl_mvm_is_lar_supported(mvm)))
		return ERR_PTR(-EOPNOTSUPP);

	cmd.len[0] = sizeof(struct iwl_mcc_update_cmd);

	IWL_DEBUG_LAR(mvm, "send MCC update to FW with '%c%c' src = %d\n",
		      alpha2[0], alpha2[1], src_id);

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret)
		return ERR_PTR(ret);

	pkt = cmd.resp_pkt;

	/* Extract MCC response */
	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_MCC_UPDATE_11AX_SUPPORT)) {
		struct iwl_mcc_update_resp *mcc_resp = (void *)pkt->data;

		n_channels =  __le32_to_cpu(mcc_resp->n_channels);
		resp_len = sizeof(struct iwl_mcc_update_resp) +
			   n_channels * sizeof(__le32);
		resp_cp = kmemdup(mcc_resp, resp_len, GFP_KERNEL);
		if (!resp_cp) {
			resp_cp = ERR_PTR(-ENOMEM);
			goto exit;
		}
	} else {
		struct iwl_mcc_update_resp_v3 *mcc_resp_v3 = (void *)pkt->data;

		n_channels =  __le32_to_cpu(mcc_resp_v3->n_channels);
		resp_len = sizeof(struct iwl_mcc_update_resp) +
			   n_channels * sizeof(__le32);
		resp_cp = kzalloc(resp_len, GFP_KERNEL);
		if (!resp_cp) {
			resp_cp = ERR_PTR(-ENOMEM);
			goto exit;
		}

		resp_cp->status = mcc_resp_v3->status;
		resp_cp->mcc = mcc_resp_v3->mcc;
		resp_cp->cap = cpu_to_le16(mcc_resp_v3->cap);
		resp_cp->source_id = mcc_resp_v3->source_id;
		resp_cp->time = mcc_resp_v3->time;
		resp_cp->geo_info = mcc_resp_v3->geo_info;
		resp_cp->n_channels = mcc_resp_v3->n_channels;
		memcpy(resp_cp->channels, mcc_resp_v3->channels,
		       n_channels * sizeof(__le32));
	}

	status = le32_to_cpu(resp_cp->status);

	mcc = le16_to_cpu(resp_cp->mcc);

	/* W/A for a FW/NVM issue - returns 0x00 for the world domain */
	if (mcc == 0) {
		mcc = 0x3030;  /* "00" - world */
		resp_cp->mcc = cpu_to_le16(mcc);
	}

	IWL_DEBUG_LAR(mvm,
		      "MCC response status: 0x%x. new MCC: 0x%x ('%c%c') n_chans: %d\n",
		      status, mcc, mcc >> 8, mcc & 0xff, n_channels);

exit:
	iwl_free_resp(&cmd);
	return resp_cp;
}

int iwl_mvm_init_mcc(struct iwl_mvm *mvm)
{
	bool tlv_lar;
	bool nvm_lar;
	int retval;
	struct ieee80211_regdomain *regd;
	char mcc[3];

	if (mvm->cfg->nvm_type == IWL_NVM_EXT) {
		tlv_lar = fw_has_capa(&mvm->fw->ucode_capa,
				      IWL_UCODE_TLV_CAPA_LAR_SUPPORT);
		nvm_lar = mvm->nvm_data->lar_enabled;
		if (tlv_lar != nvm_lar)
			IWL_INFO(mvm,
				 "Conflict between TLV & NVM regarding enabling LAR (TLV = %s NVM =%s)\n",
				 tlv_lar ? "enabled" : "disabled",
				 nvm_lar ? "enabled" : "disabled");
	}

	if (!iwl_mvm_is_lar_supported(mvm))
		return 0;

	/*
	 * try to replay the last set MCC to FW. If it doesn't exist,
	 * queue an update to cfg80211 to retrieve the default alpha2 from FW.
	 */
	retval = iwl_mvm_init_fw_regd(mvm);
	if (retval != -ENOENT)
		return retval;

	/*
	 * Driver regulatory hint for initial update, this also informs the
	 * firmware we support wifi location updates.
	 * Disallow scans that might crash the FW while the LAR regdomain
	 * is not set.
	 */
	mvm->lar_regdom_set = false;

	regd = iwl_mvm_get_current_regdomain(mvm, NULL);
	if (IS_ERR_OR_NULL(regd))
		return -EIO;

	if (iwl_mvm_is_wifi_mcc_supported(mvm) &&
	    !iwl_acpi_get_mcc(mvm->dev, mcc)) {
		kfree(regd);
		regd = iwl_mvm_get_regdomain(mvm->hw->wiphy, mcc,
					     MCC_SOURCE_BIOS, NULL);
		if (IS_ERR_OR_NULL(regd))
			return -EIO;
	}

	retval = regulatory_set_wiphy_regd_sync(mvm->hw->wiphy, regd);
	kfree(regd);
	return retval;
}

void iwl_mvm_rx_chub_update_mcc(struct iwl_mvm *mvm,
				struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mcc_chub_notif *notif = (void *)pkt->data;
	enum iwl_mcc_source src;
	char mcc[3];
	struct ieee80211_regdomain *regd;
	int wgds_tbl_idx;

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_is_vif_assoc(mvm) && notif->source_id == MCC_SOURCE_WIFI) {
		IWL_DEBUG_LAR(mvm, "Ignore mcc update while associated\n");
		return;
	}

	if (WARN_ON_ONCE(!iwl_mvm_is_lar_supported(mvm)))
		return;

	mcc[0] = le16_to_cpu(notif->mcc) >> 8;
	mcc[1] = le16_to_cpu(notif->mcc) & 0xff;
	mcc[2] = '\0';
	src = notif->source_id;

	IWL_DEBUG_LAR(mvm,
		      "RX: received chub update mcc cmd (mcc '%s' src %d)\n",
		      mcc, src);
	regd = iwl_mvm_get_regdomain(mvm->hw->wiphy, mcc, src, NULL);
	if (IS_ERR_OR_NULL(regd))
		return;

	wgds_tbl_idx = iwl_mvm_get_sar_geo_profile(mvm);
	if (wgds_tbl_idx < 1)
		IWL_DEBUG_INFO(mvm,
			       "SAR WGDS is disabled or error received (%d)\n",
			       wgds_tbl_idx);
	else
		IWL_DEBUG_INFO(mvm, "SAR WGDS: geo profile %d is configured\n",
			       wgds_tbl_idx);

	regulatory_set_wiphy_regd(mvm->hw->wiphy, regd);
	kfree(regd);
}
