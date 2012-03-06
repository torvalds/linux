/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2012 Intel Corporation. All rights reserved.
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
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/module.h>

#include "iwl-drv.h"
#include "iwl-trans.h"
#include "iwl-wifi.h"
#include "iwl-shared.h"
#include "iwl-op-mode.h"

/* private includes */
#include "iwl-ucode.h"

static void iwl_free_fw_desc(struct iwl_nic *nic, struct fw_desc *desc)
{
	if (desc->v_addr)
		dma_free_coherent(trans(nic)->dev, desc->len,
				  desc->v_addr, desc->p_addr);
	desc->v_addr = NULL;
	desc->len = 0;
}

static void iwl_free_fw_img(struct iwl_nic *nic, struct fw_img *img)
{
	iwl_free_fw_desc(nic, &img->code);
	iwl_free_fw_desc(nic, &img->data);
}

static void iwl_dealloc_ucode(struct iwl_nic *nic)
{
	iwl_free_fw_img(nic, &nic->fw.ucode_rt);
	iwl_free_fw_img(nic, &nic->fw.ucode_init);
	iwl_free_fw_img(nic, &nic->fw.ucode_wowlan);
}

static int iwl_alloc_fw_desc(struct iwl_nic *nic, struct fw_desc *desc,
		      const void *data, size_t len)
{
	if (!len) {
		desc->v_addr = NULL;
		return -EINVAL;
	}

	desc->v_addr = dma_alloc_coherent(trans(nic)->dev, len,
					  &desc->p_addr, GFP_KERNEL);
	if (!desc->v_addr)
		return -ENOMEM;

	desc->len = len;
	memcpy(desc->v_addr, data, len);
	return 0;
}

static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context);

#define UCODE_EXPERIMENTAL_INDEX	100
#define UCODE_EXPERIMENTAL_TAG		"exp"

static int iwl_request_firmware(struct iwl_nic *nic, bool first)
{
	const struct iwl_cfg *cfg = cfg(nic);
	const char *name_pre = cfg->fw_name_pre;
	char tag[8];

	if (first) {
#ifdef CONFIG_IWLWIFI_DEBUG_EXPERIMENTAL_UCODE
		nic->fw_index = UCODE_EXPERIMENTAL_INDEX;
		strcpy(tag, UCODE_EXPERIMENTAL_TAG);
	} else if (nic->fw_index == UCODE_EXPERIMENTAL_INDEX) {
#endif
		nic->fw_index = cfg->ucode_api_max;
		sprintf(tag, "%d", nic->fw_index);
	} else {
		nic->fw_index--;
		sprintf(tag, "%d", nic->fw_index);
	}

	if (nic->fw_index < cfg->ucode_api_min) {
		IWL_ERR(nic, "no suitable firmware found!\n");
		return -ENOENT;
	}

	sprintf(nic->firmware_name, "%s%s%s", name_pre, tag, ".ucode");

	IWL_DEBUG_INFO(nic, "attempting to load firmware %s'%s'\n",
		       (nic->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? "EXPERIMENTAL " : "",
		       nic->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, nic->firmware_name,
				       trans(nic)->dev,
				       GFP_KERNEL, nic, iwl_ucode_callback);
}

struct iwlagn_firmware_pieces {
	const void *inst, *data, *init, *init_data, *wowlan_inst, *wowlan_data;
	size_t inst_size, data_size, init_size, init_data_size,
	       wowlan_inst_size, wowlan_data_size;

	u32 init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
	u32 inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;
};

static int iwl_parse_v1_v2_firmware(struct iwl_nic *nic,
				       const struct firmware *ucode_raw,
				       struct iwlagn_firmware_pieces *pieces)
{
	struct iwl_ucode_header *ucode = (void *)ucode_raw->data;
	u32 api_ver, hdr_size, build;
	char buildstr[25];
	const u8 *src;

	nic->fw.ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(nic->fw.ucode_ver);

	switch (api_ver) {
	default:
		hdr_size = 28;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(nic, "File size too small!\n");
			return -EINVAL;
		}
		build = le32_to_cpu(ucode->u.v2.build);
		pieces->inst_size = le32_to_cpu(ucode->u.v2.inst_size);
		pieces->data_size = le32_to_cpu(ucode->u.v2.data_size);
		pieces->init_size = le32_to_cpu(ucode->u.v2.init_size);
		pieces->init_data_size =
			le32_to_cpu(ucode->u.v2.init_data_size);
		src = ucode->u.v2.data;
		break;
	case 0:
	case 1:
	case 2:
		hdr_size = 24;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(nic, "File size too small!\n");
			return -EINVAL;
		}
		build = 0;
		pieces->inst_size = le32_to_cpu(ucode->u.v1.inst_size);
		pieces->data_size = le32_to_cpu(ucode->u.v1.data_size);
		pieces->init_size = le32_to_cpu(ucode->u.v1.init_size);
		pieces->init_data_size =
			le32_to_cpu(ucode->u.v1.init_data_size);
		src = ucode->u.v1.data;
		break;
	}

	if (build)
		sprintf(buildstr, " build %u%s", build,
		       (nic->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? " (EXP)" : "");
	else
		buildstr[0] = '\0';

	snprintf(nic->fw.fw_version,
		 sizeof(nic->fw.fw_version),
		 "%u.%u.%u.%u%s",
		 IWL_UCODE_MAJOR(nic->fw.ucode_ver),
		 IWL_UCODE_MINOR(nic->fw.ucode_ver),
		 IWL_UCODE_API(nic->fw.ucode_ver),
		 IWL_UCODE_SERIAL(nic->fw.ucode_ver),
		 buildstr);

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size != hdr_size + pieces->inst_size +
				pieces->data_size + pieces->init_size +
				pieces->init_data_size) {

		IWL_ERR(nic,
			"uCode file size %d does not match expected size\n",
			(int)ucode_raw->size);
		return -EINVAL;
	}

	pieces->inst = src;
	src += pieces->inst_size;
	pieces->data = src;
	src += pieces->data_size;
	pieces->init = src;
	src += pieces->init_size;
	pieces->init_data = src;
	src += pieces->init_data_size;

	return 0;
}

static int iwl_parse_tlv_firmware(struct iwl_nic *nic,
				const struct firmware *ucode_raw,
				struct iwlagn_firmware_pieces *pieces,
				struct iwl_ucode_capabilities *capa)
{
	struct iwl_tlv_ucode_header *ucode = (void *)ucode_raw->data;
	struct iwl_ucode_tlv *tlv;
	size_t len = ucode_raw->size;
	const u8 *data;
	int wanted_alternative = iwlagn_mod_params.wanted_ucode_alternative;
	int tmp;
	u64 alternatives;
	u32 tlv_len;
	enum iwl_ucode_tlv_type tlv_type;
	const u8 *tlv_data;
	char buildstr[25];
	u32 build;

	if (len < sizeof(*ucode)) {
		IWL_ERR(nic, "uCode has invalid length: %zd\n", len);
		return -EINVAL;
	}

	if (ucode->magic != cpu_to_le32(IWL_TLV_UCODE_MAGIC)) {
		IWL_ERR(nic, "invalid uCode magic: 0X%x\n",
			le32_to_cpu(ucode->magic));
		return -EINVAL;
	}

	/*
	 * Check which alternatives are present, and "downgrade"
	 * when the chosen alternative is not present, warning
	 * the user when that happens. Some files may not have
	 * any alternatives, so don't warn in that case.
	 */
	alternatives = le64_to_cpu(ucode->alternatives);
	tmp = wanted_alternative;
	if (wanted_alternative > 63)
		wanted_alternative = 63;
	while (wanted_alternative && !(alternatives & BIT(wanted_alternative)))
		wanted_alternative--;
	if (wanted_alternative && wanted_alternative != tmp)
		IWL_WARN(nic,
			 "uCode alternative %d not available, choosing %d\n",
			 tmp, wanted_alternative);

	nic->fw.ucode_ver = le32_to_cpu(ucode->ver);
	build = le32_to_cpu(ucode->build);

	if (build)
		sprintf(buildstr, " build %u%s", build,
		       (nic->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? " (EXP)" : "");
	else
		buildstr[0] = '\0';

	snprintf(nic->fw.fw_version,
		 sizeof(nic->fw.fw_version),
		 "%u.%u.%u.%u%s",
		 IWL_UCODE_MAJOR(nic->fw.ucode_ver),
		 IWL_UCODE_MINOR(nic->fw.ucode_ver),
		 IWL_UCODE_API(nic->fw.ucode_ver),
		 IWL_UCODE_SERIAL(nic->fw.ucode_ver),
		 buildstr);

	data = ucode->data;

	len -= sizeof(*ucode);

	while (len >= sizeof(*tlv)) {
		u16 tlv_alt;

		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le16_to_cpu(tlv->type);
		tlv_alt = le16_to_cpu(tlv->alternative);
		tlv_data = tlv->data;

		if (len < tlv_len) {
			IWL_ERR(nic, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		/*
		 * Alternative 0 is always valid.
		 *
		 * Skip alternative TLVs that are not selected.
		 */
		if (tlv_alt != 0 && tlv_alt != wanted_alternative)
			continue;

		switch (tlv_type) {
		case IWL_UCODE_TLV_INST:
			pieces->inst = tlv_data;
			pieces->inst_size = tlv_len;
			break;
		case IWL_UCODE_TLV_DATA:
			pieces->data = tlv_data;
			pieces->data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_INIT:
			pieces->init = tlv_data;
			pieces->init_size = tlv_len;
			break;
		case IWL_UCODE_TLV_INIT_DATA:
			pieces->init_data = tlv_data;
			pieces->init_data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_BOOT:
			IWL_ERR(nic, "Found unexpected BOOT ucode\n");
			break;
		case IWL_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->max_probe_length =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_PAN:
			if (tlv_len)
				goto invalid_tlv_len;
			capa->flags |= IWL_UCODE_TLV_FLAGS_PAN;
			break;
		case IWL_UCODE_TLV_FLAGS:
			/* must be at least one u32 */
			if (tlv_len < sizeof(u32))
				goto invalid_tlv_len;
			/* and a proper number of u32s */
			if (tlv_len % sizeof(u32))
				goto invalid_tlv_len;
			/*
			 * This driver only reads the first u32 as
			 * right now no more features are defined,
			 * if that changes then either the driver
			 * will not work with the new firmware, or
			 * it'll not take advantage of new features.
			 */
			capa->flags = le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_ENHANCE_SENS_TBL:
			if (tlv_len)
				goto invalid_tlv_len;
			nic->fw.enhance_sensitivity_table = true;
			break;
		case IWL_UCODE_TLV_WOWLAN_INST:
			pieces->wowlan_inst = tlv_data;
			pieces->wowlan_inst_size = tlv_len;
			break;
		case IWL_UCODE_TLV_WOWLAN_DATA:
			pieces->wowlan_data = tlv_data;
			pieces->wowlan_data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_PHY_CALIBRATION_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->standard_phy_calibration_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		default:
			IWL_DEBUG_INFO(nic, "unknown TLV: %d\n", tlv_type);
			break;
		}
	}

	if (len) {
		IWL_ERR(nic, "invalid TLV after parsing: %zd\n", len);
		iwl_print_hex_dump(nic, IWL_DL_FW, (u8 *)data, len);
		return -EINVAL;
	}

	return 0;

 invalid_tlv_len:
	IWL_ERR(nic, "TLV %d has invalid size: %u\n", tlv_type, tlv_len);
	iwl_print_hex_dump(nic, IWL_DL_FW, tlv_data, tlv_len);

	return -EINVAL;
}

/**
 * iwl_ucode_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context)
{
	struct iwl_nic *nic = context;
	const struct iwl_cfg *cfg = cfg(nic);
	struct iwl_fw *fw = &nic->fw;
	struct iwl_ucode_header *ucode;
	int err;
	struct iwlagn_firmware_pieces pieces;
	const unsigned int api_max = cfg->ucode_api_max;
	unsigned int api_ok = cfg->ucode_api_ok;
	const unsigned int api_min = cfg->ucode_api_min;
	u32 api_ver;

	fw->ucode_capa.max_probe_length = 200;
	fw->ucode_capa.standard_phy_calibration_size =
			IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	if (!api_ok)
		api_ok = api_max;

	memset(&pieces, 0, sizeof(pieces));

	if (!ucode_raw) {
		if (nic->fw_index <= api_ok)
			IWL_ERR(nic,
				"request for firmware file '%s' failed.\n",
				nic->firmware_name);
		goto try_again;
	}

	IWL_DEBUG_INFO(nic, "Loaded firmware file '%s' (%zd bytes).\n",
		       nic->firmware_name, ucode_raw->size);

	/* Make sure that we got at least the API version number */
	if (ucode_raw->size < 4) {
		IWL_ERR(nic, "File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (struct iwl_ucode_header *)ucode_raw->data;

	if (ucode->ver)
		err = iwl_parse_v1_v2_firmware(nic, ucode_raw, &pieces);
	else
		err = iwl_parse_tlv_firmware(nic, ucode_raw, &pieces,
					   &fw->ucode_capa);

	if (err)
		goto try_again;

	api_ver = IWL_UCODE_API(nic->fw.ucode_ver);

	/*
	 * api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward
	 */
	/* no api version check required for experimental uCode */
	if (nic->fw_index != UCODE_EXPERIMENTAL_INDEX) {
		if (api_ver < api_min || api_ver > api_max) {
			IWL_ERR(nic,
				"Driver unable to support your firmware API. "
				"Driver supports v%u, firmware is v%u.\n",
				api_max, api_ver);
			goto try_again;
		}

		if (api_ver < api_ok) {
			if (api_ok != api_max)
				IWL_ERR(nic, "Firmware has old API version, "
					"expected v%u through v%u, got v%u.\n",
					api_ok, api_max, api_ver);
			else
				IWL_ERR(nic, "Firmware has old API version, "
					"expected v%u, got v%u.\n",
					api_max, api_ver);
			IWL_ERR(nic, "New firmware can be obtained from "
				      "http://www.intellinuxwireless.org/.\n");
		}
	}

	IWL_INFO(nic, "loaded firmware version %s", nic->fw.fw_version);

	/*
	 * For any of the failures below (before allocating pci memory)
	 * we will try to load a version with a smaller API -- maybe the
	 * user just got a corrupted version of the latest API.
	 */

	IWL_DEBUG_INFO(nic, "f/w package hdr ucode version raw = 0x%x\n",
		       nic->fw.ucode_ver);
	IWL_DEBUG_INFO(nic, "f/w package hdr runtime inst size = %Zd\n",
		       pieces.inst_size);
	IWL_DEBUG_INFO(nic, "f/w package hdr runtime data size = %Zd\n",
		       pieces.data_size);
	IWL_DEBUG_INFO(nic, "f/w package hdr init inst size = %Zd\n",
		       pieces.init_size);
	IWL_DEBUG_INFO(nic, "f/w package hdr init data size = %Zd\n",
		       pieces.init_data_size);

	/* Verify that uCode images will fit in card's SRAM */
	if (pieces.inst_size > cfg->max_inst_size) {
		IWL_ERR(nic, "uCode instr len %Zd too large to fit in\n",
			pieces.inst_size);
		goto try_again;
	}

	if (pieces.data_size > cfg->max_data_size) {
		IWL_ERR(nic, "uCode data len %Zd too large to fit in\n",
			pieces.data_size);
		goto try_again;
	}

	if (pieces.init_size > cfg->max_inst_size) {
		IWL_ERR(nic, "uCode init instr len %Zd too large to fit in\n",
			pieces.init_size);
		goto try_again;
	}

	if (pieces.init_data_size > cfg->max_data_size) {
		IWL_ERR(nic, "uCode init data len %Zd too large to fit in\n",
			pieces.init_data_size);
		goto try_again;
	}

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	if (iwl_alloc_fw_desc(nic, &nic->fw.ucode_rt.code,
			      pieces.inst, pieces.inst_size))
		goto err_pci_alloc;
	if (iwl_alloc_fw_desc(nic, &nic->fw.ucode_rt.data,
			      pieces.data, pieces.data_size))
		goto err_pci_alloc;

	/* Initialization instructions and data */
	if (pieces.init_size && pieces.init_data_size) {
		if (iwl_alloc_fw_desc(nic,
				      &nic->fw.ucode_init.code,
				      pieces.init, pieces.init_size))
			goto err_pci_alloc;
		if (iwl_alloc_fw_desc(nic,
				      &nic->fw.ucode_init.data,
				      pieces.init_data, pieces.init_data_size))
			goto err_pci_alloc;
	}

	/* WoWLAN instructions and data */
	if (pieces.wowlan_inst_size && pieces.wowlan_data_size) {
		if (iwl_alloc_fw_desc(nic,
				      &nic->fw.ucode_wowlan.code,
				      pieces.wowlan_inst,
				      pieces.wowlan_inst_size))
			goto err_pci_alloc;
		if (iwl_alloc_fw_desc(nic,
				      &nic->fw.ucode_wowlan.data,
				      pieces.wowlan_data,
				      pieces.wowlan_data_size))
			goto err_pci_alloc;
	}

	/* Now that we can no longer fail, copy information */

	/*
	 * The (size - 16) / 12 formula is based on the information recorded
	 * for each event, which is of mode 1 (including timestamp) for all
	 * new microcodes that include this information.
	 */
	fw->init_evtlog_ptr = pieces.init_evtlog_ptr;
	if (pieces.init_evtlog_size)
		fw->init_evtlog_size = (pieces.init_evtlog_size - 16)/12;
	else
		fw->init_evtlog_size =
			cfg->base_params->max_event_log_size;
	fw->init_errlog_ptr = pieces.init_errlog_ptr;
	fw->inst_evtlog_ptr = pieces.inst_evtlog_ptr;
	if (pieces.inst_evtlog_size)
		fw->inst_evtlog_size = (pieces.inst_evtlog_size - 16)/12;
	else
		fw->inst_evtlog_size =
			cfg->base_params->max_event_log_size;
	fw->inst_errlog_ptr = pieces.inst_errlog_ptr;

	/*
	 * figure out the offset of chain noise reset and gain commands
	 * base on the size of standard phy calibration commands table size
	 */
	if (fw->ucode_capa.standard_phy_calibration_size >
	    IWL_MAX_PHY_CALIBRATE_TBL_SIZE)
		fw->ucode_capa.standard_phy_calibration_size =
			IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	complete(&nic->request_firmware_complete);

	nic->op_mode = iwl_dvm_ops.start(nic->shrd->trans, &nic->fw);

	if (!nic->op_mode)
		goto out_unbind;

	return;

 try_again:
	/* try next, if any */
	release_firmware(ucode_raw);
	if (iwl_request_firmware(nic, false))
		goto out_unbind;
	return;

 err_pci_alloc:
	IWL_ERR(nic, "failed to allocate pci memory\n");
	iwl_dealloc_ucode(nic);
	release_firmware(ucode_raw);
 out_unbind:
	complete(&nic->request_firmware_complete);
	device_release_driver(trans(nic)->dev);
}

int iwl_drv_start(struct iwl_shared *shrd,
		  struct iwl_trans *trans, const struct iwl_cfg *cfg)
{
	struct iwl_nic *nic;
	int ret;

	shrd->cfg = cfg;

	nic = kzalloc(sizeof(*nic), GFP_KERNEL);
	if (!nic) {
		dev_printk(KERN_ERR, trans->dev, "Couldn't allocate iwl_nic");
		return -ENOMEM;
	}
	nic->shrd = shrd;
	shrd->nic = nic;

	init_completion(&nic->request_firmware_complete);

	ret = iwl_request_firmware(nic, true);

	if (ret) {
		dev_printk(KERN_ERR, trans->dev, "Couldn't request the fw");
		kfree(nic);
		shrd->nic = NULL;
	}

	return ret;
}

void iwl_drv_stop(struct iwl_shared *shrd)
{
	struct iwl_nic *nic = shrd->nic;

	wait_for_completion(&nic->request_firmware_complete);

	/* op_mode can be NULL if its start failed */
	if (nic->op_mode)
		iwl_op_mode_stop(nic->op_mode);

	iwl_dealloc_ucode(nic);

	kfree(nic);
	shrd->nic = NULL;
}
