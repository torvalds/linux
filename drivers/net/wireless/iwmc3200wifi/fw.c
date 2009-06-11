/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#include <linux/kernel.h>
#include <linux/firmware.h>

#include "iwm.h"
#include "bus.h"
#include "hal.h"
#include "umac.h"
#include "debug.h"
#include "fw.h"
#include "commands.h"

static const char fw_barker[] = "*WESTOPFORNOONE*";

/*
 * @op_code: Op code we're looking for.
 * @index: There can be several instances of the same opcode within
 *         the firmware. Index specifies which one we're looking for.
 */
static int iwm_fw_op_offset(struct iwm_priv *iwm, const struct firmware *fw,
			    u16 op_code, u32 index)
{
	int offset = -EINVAL, fw_offset;
	u32 op_index = 0;
	const u8 *fw_ptr;
	struct iwm_fw_hdr_rec *rec;

	fw_offset = 0;
	fw_ptr = fw->data;

	/* We first need to look for the firmware barker */
	if (memcmp(fw_ptr, fw_barker, IWM_HDR_BARKER_LEN)) {
		IWM_ERR(iwm, "No barker string in this FW\n");
		return -EINVAL;
	}

	if (fw->size < IWM_HDR_LEN) {
		IWM_ERR(iwm, "FW is too small (%zu)\n", fw->size);
		return -EINVAL;
	}

	fw_offset += IWM_HDR_BARKER_LEN;

	while (fw_offset < fw->size) {
		rec = (struct iwm_fw_hdr_rec *)(fw_ptr + fw_offset);

		IWM_DBG_FW(iwm, DBG, "FW: op_code: 0x%x, len: %d @ 0x%x\n",
			   rec->op_code, rec->len, fw_offset);

		if (rec->op_code == IWM_HDR_REC_OP_INVALID) {
			IWM_DBG_FW(iwm, DBG, "Reached INVALID op code\n");
			break;
		}

		if (rec->op_code == op_code) {
			if (op_index == index) {
				fw_offset += sizeof(struct iwm_fw_hdr_rec);
				offset = fw_offset;
				goto out;
			}
			op_index++;
		}

		fw_offset += sizeof(struct iwm_fw_hdr_rec) + rec->len;
	}

 out:
	return offset;
}

static int iwm_load_firmware_chunk(struct iwm_priv *iwm,
				   const struct firmware *fw,
				   struct iwm_fw_img_desc *img_desc)
{
	struct iwm_udma_nonwifi_cmd target_cmd;
	u32 chunk_size;
	const u8 *chunk_ptr;
	int ret = 0;

	IWM_DBG_FW(iwm, INFO, "Loading FW chunk: %d bytes @ 0x%x\n",
		   img_desc->length, img_desc->address);

	target_cmd.opcode = UMAC_HDI_OUT_OPCODE_WRITE;
	target_cmd.handle_by_hw = 1;
	target_cmd.op2 = 0;
	target_cmd.resp = 0;
	target_cmd.eop = 1;

	chunk_size = img_desc->length;
	chunk_ptr = fw->data + img_desc->offset;

	while (chunk_size > 0) {
		u32 tmp_chunk_size;

		tmp_chunk_size = min_t(u32, chunk_size,
				       IWM_MAX_NONWIFI_CMD_BUFF_SIZE);

		target_cmd.addr = cpu_to_le32(img_desc->address +
				    (chunk_ptr - fw->data - img_desc->offset));
		target_cmd.op1_sz = cpu_to_le32(tmp_chunk_size);

		IWM_DBG_FW(iwm, DBG, "\t%d bytes @ 0x%x\n",
			   tmp_chunk_size, target_cmd.addr);

		ret = iwm_hal_send_target_cmd(iwm, &target_cmd, chunk_ptr);
		if (ret < 0) {
			IWM_ERR(iwm, "Couldn't load FW chunk\n");
			break;
		}

		chunk_size -= tmp_chunk_size;
		chunk_ptr += tmp_chunk_size;
	}

	return ret;
}
/*
 * To load a fw image to the target, we basically go through the
 * fw, looking for OP_MEM_DESC records. Once we found one, we
 * pass it to iwm_load_firmware_chunk().
 * The OP_MEM_DESC records contain the actuall memory chunk to be
 * sent, but also the destination address.
 */
static int iwm_load_img(struct iwm_priv *iwm, const char *img_name)
{
	const struct firmware *fw;
	struct iwm_fw_img_desc *img_desc;
	struct iwm_fw_img_ver *ver;
	int ret = 0, fw_offset;
	u32 opcode_idx = 0, build_date;
	char *build_tag;

	ret = request_firmware(&fw, img_name, iwm_to_dev(iwm));
	if (ret) {
		IWM_ERR(iwm, "Request firmware failed");
		return ret;
	}

	IWM_DBG_FW(iwm, INFO, "Start to load FW %s\n", img_name);

	while (1) {
		fw_offset = iwm_fw_op_offset(iwm, fw,
					     IWM_HDR_REC_OP_MEM_DESC,
					     opcode_idx);
		if (fw_offset < 0)
			break;

		img_desc = (struct iwm_fw_img_desc *)(fw->data + fw_offset);
		ret = iwm_load_firmware_chunk(iwm, fw, img_desc);
		if (ret < 0)
			goto err_release_fw;
		opcode_idx++;
	};

	/* Read firmware version */
	fw_offset = iwm_fw_op_offset(iwm, fw, IWM_HDR_REC_OP_SW_VER, 0);
	if (fw_offset < 0)
		goto err_release_fw;

	ver = (struct iwm_fw_img_ver *)(fw->data + fw_offset);

	/* Read build tag */
	fw_offset = iwm_fw_op_offset(iwm, fw, IWM_HDR_REC_OP_BUILD_TAG, 0);
	if (fw_offset < 0)
		goto err_release_fw;

	build_tag = (char *)(fw->data + fw_offset);

	/* Read build date */
	fw_offset = iwm_fw_op_offset(iwm, fw, IWM_HDR_REC_OP_BUILD_DATE, 0);
	if (fw_offset < 0)
		goto err_release_fw;

	build_date = *(u32 *)(fw->data + fw_offset);

	IWM_INFO(iwm, "%s:\n", img_name);
	IWM_INFO(iwm, "\tVersion:    %02X.%02X\n", ver->major, ver->minor);
	IWM_INFO(iwm, "\tBuild tag:  %s\n", build_tag);
	IWM_INFO(iwm, "\tBuild date: %x-%x-%x\n",
		 IWM_BUILD_YEAR(build_date), IWM_BUILD_MONTH(build_date),
		 IWM_BUILD_DAY(build_date));


 err_release_fw:
	release_firmware(fw);

	return ret;
}

static int iwm_load_umac(struct iwm_priv *iwm)
{
	struct iwm_udma_nonwifi_cmd target_cmd;
	int ret;

	ret = iwm_load_img(iwm, iwm->bus_ops->umac_name);
	if (ret < 0)
		return ret;

	/* We've loaded the UMAC, we can tell the target to jump there */
	target_cmd.opcode = UMAC_HDI_OUT_OPCODE_JUMP;
	target_cmd.addr = cpu_to_le32(UMAC_MU_FW_INST_DATA_12_ADDR);
	target_cmd.op1_sz = 0;
	target_cmd.op2 = 0;
	target_cmd.handle_by_hw = 0;
	target_cmd.resp = 1 ;
	target_cmd.eop = 1;

	ret = iwm_hal_send_target_cmd(iwm, &target_cmd, NULL);
	if (ret < 0)
		IWM_ERR(iwm, "Couldn't send JMP command\n");

	return ret;
}

static int iwm_load_lmac(struct iwm_priv *iwm, const char *img_name)
{
	int ret;

	ret = iwm_load_img(iwm, img_name);
	if (ret < 0)
		return ret;

	return iwm_send_umac_reset(iwm,
			cpu_to_le32(UMAC_RST_CTRL_FLG_LARC_CLK_EN), 0);
}

/*
 * We currently have to load 3 FWs:
 * 1) The UMAC (Upper MAC).
 * 2) The calibration LMAC (Lower MAC).
 *    We then send the calibration init command, so that the device can
 *    run a first calibration round.
 * 3) The operational LMAC, which replaces the calibration one when it's
 *    done with the first calibration round.
 *
 * Once those 3 FWs have been loaded, we send the periodic calibration
 * command, and then the device is available for regular 802.11 operations.
 */
int iwm_load_fw(struct iwm_priv *iwm)
{
	int ret;

	/* We first start downloading the UMAC */
	ret = iwm_load_umac(iwm);
	if (ret < 0) {
		IWM_ERR(iwm, "UMAC loading failed\n");
		return ret;
	}

	/* Handle UMAC_ALIVE notification */
	ret = iwm_notif_handle(iwm, UMAC_NOTIFY_OPCODE_ALIVE, IWM_SRC_UMAC,
			       WAIT_NOTIF_TIMEOUT);
	if (ret) {
		IWM_ERR(iwm, "Handle UMAC_ALIVE failed: %d\n", ret);
		return ret;
	}

	/* UMAC is alive, we can download the calibration LMAC */
	ret = iwm_load_lmac(iwm, iwm->bus_ops->calib_lmac_name);
	if (ret) {
		IWM_ERR(iwm, "Calibration LMAC loading failed\n");
		return ret;
	}

	/* Handle UMAC_INIT_COMPLETE notification */
	ret = iwm_notif_handle(iwm, UMAC_NOTIFY_OPCODE_INIT_COMPLETE,
			       IWM_SRC_UMAC, WAIT_NOTIF_TIMEOUT);
	if (ret) {
		IWM_ERR(iwm, "Handle INIT_COMPLETE failed for calibration "
			"LMAC: %d\n", ret);
		return ret;
	}

	/* Read EEPROM data */
	ret = iwm_eeprom_init(iwm);
	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't init eeprom array\n");
		return ret;
	}

#ifdef CONFIG_IWM_B0_HW_SUPPORT
	if (iwm->conf.hw_b0) {
		clear_bit(PHY_CALIBRATE_RX_IQ_CMD, &iwm->conf.init_calib_map);
		clear_bit(PHY_CALIBRATE_RX_IQ_CMD,
			  &iwm->conf.periodic_calib_map);
	}
#endif
	/* Read RX IQ calibration result from EEPROM */
	if (test_bit(PHY_CALIBRATE_RX_IQ_CMD, &iwm->conf.init_calib_map)) {
		iwm_store_rxiq_calib_result(iwm);
		set_bit(PHY_CALIBRATE_RX_IQ_CMD, &iwm->calib_done_map);
	}

	iwm_send_prio_table(iwm);
	iwm_send_init_calib_cfg(iwm, iwm->conf.init_calib_map);

	while (iwm->calib_done_map != iwm->conf.init_calib_map) {
		ret = iwm_notif_handle(iwm, CALIBRATION_RES_NOTIFICATION,
				       IWM_SRC_LMAC, WAIT_NOTIF_TIMEOUT);
		if (ret) {
			IWM_ERR(iwm, "Wait for calibration result timeout\n");
			goto out;
		}
		IWM_DBG_FW(iwm, DBG, "Got calibration result. calib_done_map: "
			   "0x%lx, requested calibrations: 0x%lx\n",
			   iwm->calib_done_map, iwm->conf.init_calib_map);
	}

	/* Handle LMAC CALIBRATION_COMPLETE notification */
	ret = iwm_notif_handle(iwm, CALIBRATION_COMPLETE_NOTIFICATION,
			       IWM_SRC_LMAC, WAIT_NOTIF_TIMEOUT);
	if (ret) {
		IWM_ERR(iwm, "Wait for CALIBRATION_COMPLETE timeout\n");
		goto out;
	}

	IWM_INFO(iwm, "LMAC calibration done: 0x%lx\n", iwm->calib_done_map);

	iwm_send_umac_reset(iwm, cpu_to_le32(UMAC_RST_CTRL_FLG_LARC_RESET), 1);

	ret = iwm_notif_handle(iwm, UMAC_CMD_OPCODE_RESET, IWM_SRC_UMAC,
			       WAIT_NOTIF_TIMEOUT);
	if (ret) {
		IWM_ERR(iwm, "Wait for UMAC RESET timeout\n");
		goto out;
	}

	/* Download the operational LMAC */
	ret = iwm_load_lmac(iwm, iwm->bus_ops->lmac_name);
	if (ret) {
		IWM_ERR(iwm, "LMAC loading failed\n");
		goto out;
	}

	ret = iwm_notif_handle(iwm, UMAC_NOTIFY_OPCODE_INIT_COMPLETE,
			       IWM_SRC_UMAC, WAIT_NOTIF_TIMEOUT);
	if (ret) {
		IWM_ERR(iwm, "Handle INIT_COMPLETE failed for LMAC: %d\n", ret);
		goto out;
	}

	iwm_send_prio_table(iwm);
	iwm_send_calib_results(iwm);
	iwm_send_periodic_calib_cfg(iwm, iwm->conf.periodic_calib_map);

	return 0;

 out:
	iwm_eeprom_exit(iwm);
	return ret;
}
