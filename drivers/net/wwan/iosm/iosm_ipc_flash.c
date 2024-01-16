// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#include "iosm_ipc_coredump.h"
#include "iosm_ipc_devlink.h"
#include "iosm_ipc_flash.h"

/* This function will pack the data to be sent to the modem using the
 * payload, payload length and pack id
 */
static int ipc_flash_proc_format_ebl_pack(struct iosm_flash_data *flash_req,
					  u32 pack_length, u16 pack_id,
					  u8 *payload, u32 payload_length)
{
	u16 checksum = pack_id;
	u32 i;

	if (payload_length + IOSM_EBL_HEAD_SIZE > pack_length)
		return -EINVAL;

	flash_req->pack_id = cpu_to_le16(pack_id);
	flash_req->msg_length = cpu_to_le32(payload_length);
	checksum += (payload_length >> IOSM_EBL_PAYL_SHIFT) +
		     (payload_length & IOSM_EBL_CKSM);

	for (i = 0; i < payload_length; i++)
		checksum += payload[i];

	flash_req->checksum = cpu_to_le16(checksum);

	return 0;
}

/* validate the response received from modem and
 * check the type of errors received
 */
static int ipc_flash_proc_check_ebl_rsp(void *hdr_rsp, void *payload_rsp)
{
	struct iosm_ebl_error  *err_info = payload_rsp;
	u16 *rsp_code = hdr_rsp;
	u32 i;

	if (*rsp_code == IOSM_EBL_RSP_BUFF) {
		for (i = 0; i < IOSM_MAX_ERRORS; i++) {
			if (!err_info->error[i].error_code) {
				pr_err("EBL: error_class = %d, error_code = %d",
				       err_info->error[i].error_class,
				       err_info->error[i].error_code);
			}
		}
		return -EINVAL;
	}

	return 0;
}

/* Send data to the modem */
static int ipc_flash_send_data(struct iosm_devlink *ipc_devlink, u32 size,
			       u16 pack_id, u8 *payload, u32 payload_length)
{
	struct iosm_flash_data flash_req;
	int ret;

	ret = ipc_flash_proc_format_ebl_pack(&flash_req, size,
					     pack_id, payload, payload_length);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL2 pack failed for pack_id:%d",
			pack_id);
		goto ipc_free_payload;
	}

	ret = ipc_imem_sys_devlink_write(ipc_devlink, (u8 *)&flash_req,
					 IOSM_EBL_HEAD_SIZE);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL Header write failed for Id:%x",
			pack_id);
		goto ipc_free_payload;
	}

	ret = ipc_imem_sys_devlink_write(ipc_devlink, payload, payload_length);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL Payload write failed for Id:%x",
			pack_id);
	}

ipc_free_payload:
	return ret;
}

/**
 * ipc_flash_link_establish - Flash link establishment
 * @ipc_imem:           Pointer to struct iosm_imem
 *
 * Returns:     0 on success and failure value on error
 */
int ipc_flash_link_establish(struct iosm_imem *ipc_imem)
{
	u8 ler_data[IOSM_LER_RSP_SIZE];
	u32 bytes_read;

	/* Allocate channel for flashing/cd collection */
	ipc_imem->ipc_devlink->devlink_sio.channel =
					ipc_imem_sys_devlink_open(ipc_imem);

	if (!ipc_imem->ipc_devlink->devlink_sio.channel)
		goto chl_open_fail;

	if (ipc_imem_sys_devlink_read(ipc_imem->ipc_devlink, ler_data,
				      IOSM_LER_RSP_SIZE, &bytes_read))
		goto devlink_read_fail;

	if (bytes_read != IOSM_LER_RSP_SIZE)
		goto devlink_read_fail;

	return 0;

devlink_read_fail:
	ipc_imem_sys_devlink_close(ipc_imem->ipc_devlink);
chl_open_fail:
	return -EIO;
}

/* Receive data from the modem */
static int ipc_flash_receive_data(struct iosm_devlink *ipc_devlink, u32 size,
				  u8 *mdm_rsp)
{
	u8 mdm_rsp_hdr[IOSM_EBL_HEAD_SIZE];
	u32 bytes_read;
	int ret;

	ret = ipc_imem_sys_devlink_read(ipc_devlink, mdm_rsp_hdr,
					IOSM_EBL_HEAD_SIZE, &bytes_read);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL rsp to read %d bytes failed",
			IOSM_EBL_HEAD_SIZE);
		goto ipc_flash_recv_err;
	}

	if (bytes_read != IOSM_EBL_HEAD_SIZE) {
		ret = -EINVAL;
		goto ipc_flash_recv_err;
	}

	ret = ipc_imem_sys_devlink_read(ipc_devlink, mdm_rsp, size,
					&bytes_read);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL rsp to read %d bytes failed",
			size);
		goto ipc_flash_recv_err;
	}

	if (bytes_read != size) {
		ret = -EINVAL;
		goto ipc_flash_recv_err;
	}

	ret = ipc_flash_proc_check_ebl_rsp(mdm_rsp_hdr + 2, mdm_rsp);

ipc_flash_recv_err:
	return ret;
}

/* Function to send command to modem and receive response */
static int ipc_flash_send_receive(struct iosm_devlink *ipc_devlink, u16 pack_id,
				  u8 *payload, u32 payload_length, u8 *mdm_rsp)
{
	size_t frame_len = IOSM_EBL_DW_PACK_SIZE;
	int ret;

	if (pack_id == FLASH_SET_PROT_CONF)
		frame_len = IOSM_EBL_W_PACK_SIZE;

	ret = ipc_flash_send_data(ipc_devlink, frame_len, pack_id, payload,
				  payload_length);
	if (ret)
		goto ipc_flash_send_rcv;

	ret = ipc_flash_receive_data(ipc_devlink,
				     frame_len - IOSM_EBL_HEAD_SIZE, mdm_rsp);

ipc_flash_send_rcv:
	return ret;
}

/**
 * ipc_flash_boot_set_capabilities  - Set modem boot capabilities in flash
 * @ipc_devlink:        Pointer to devlink structure
 * @mdm_rsp:            Pointer to modem response buffer
 *
 * Returns:             0 on success and failure value on error
 */
int ipc_flash_boot_set_capabilities(struct iosm_devlink *ipc_devlink,
				    u8 *mdm_rsp)
{
	ipc_devlink->ebl_ctx.ebl_sw_info_version =
			ipc_devlink->ebl_ctx.m_ebl_resp[EBL_RSP_SW_INFO_VER];
	ipc_devlink->ebl_ctx.m_ebl_resp[EBL_SKIP_ERASE] = IOSM_CAP_NOT_ENHANCED;
	ipc_devlink->ebl_ctx.m_ebl_resp[EBL_SKIP_CRC] = IOSM_CAP_NOT_ENHANCED;

	if (ipc_devlink->ebl_ctx.m_ebl_resp[EBL_CAPS_FLAG] &
							IOSM_CAP_USE_EXT_CAP) {
		if (ipc_devlink->param.erase_full_flash)
			ipc_devlink->ebl_ctx.m_ebl_resp[EBL_OOS_CONFIG] &=
				~((u8)IOSM_EXT_CAP_ERASE_ALL);
		else
			ipc_devlink->ebl_ctx.m_ebl_resp[EBL_OOS_CONFIG] &=
				~((u8)IOSM_EXT_CAP_COMMIT_ALL);
		ipc_devlink->ebl_ctx.m_ebl_resp[EBL_EXT_CAPS_HANDLED] =
				IOSM_CAP_USE_EXT_CAP;
	}

	/* Write back the EBL capability to modem
	 * Request Set Protcnf command
	 */
	return ipc_flash_send_receive(ipc_devlink, FLASH_SET_PROT_CONF,
				     ipc_devlink->ebl_ctx.m_ebl_resp,
				     IOSM_EBL_RSP_SIZE, mdm_rsp);
}

/* Read the SWID type and SWID value from the EBL */
int ipc_flash_read_swid(struct iosm_devlink *ipc_devlink, u8 *mdm_rsp)
{
	struct iosm_flash_msg_control cmd_msg;
	struct iosm_swid_table *swid;
	char ebl_swid[IOSM_SWID_STR];
	int ret;

	if (ipc_devlink->ebl_ctx.ebl_sw_info_version !=
			IOSM_EXT_CAP_SWID_OOS_PACK)
		return -EINVAL;

	cmd_msg.action = cpu_to_le32(FLASH_OOSC_ACTION_READ);
	cmd_msg.type = cpu_to_le32(FLASH_OOSC_TYPE_SWID_TABLE);
	cmd_msg.length = cpu_to_le32(IOSM_MSG_LEN_ARG);
	cmd_msg.arguments = cpu_to_le32(IOSM_MSG_LEN_ARG);

	ret = ipc_flash_send_receive(ipc_devlink, FLASH_OOS_CONTROL,
				     (u8 *)&cmd_msg, IOSM_MDM_SEND_16, mdm_rsp);
	if (ret)
		goto ipc_swid_err;

	cmd_msg.action = cpu_to_le32(*((u32 *)mdm_rsp));

	ret = ipc_flash_send_receive(ipc_devlink, FLASH_OOS_DATA_READ,
				     (u8 *)&cmd_msg, IOSM_MDM_SEND_4, mdm_rsp);
	if (ret)
		goto ipc_swid_err;

	swid = (struct iosm_swid_table *)mdm_rsp;
	dev_dbg(ipc_devlink->dev, "SWID %x RF_ENGINE_ID %x", swid->sw_id_val,
		swid->rf_engine_id_val);

	snprintf(ebl_swid, sizeof(ebl_swid), "SWID: %x, RF_ENGINE_ID: %x",
		 swid->sw_id_val, swid->rf_engine_id_val);

	devlink_flash_update_status_notify(ipc_devlink->devlink_ctx, ebl_swid,
					   NULL, 0, 0);
ipc_swid_err:
	return ret;
}

/* Function to check if full erase or conditional erase was successful */
static int ipc_flash_erase_check(struct iosm_devlink *ipc_devlink, u8 *mdm_rsp)
{
	int ret, count = 0;
	u16 mdm_rsp_data;

	/* Request Flash Erase Check */
	do {
		mdm_rsp_data = IOSM_MDM_SEND_DATA;
		ret = ipc_flash_send_receive(ipc_devlink, FLASH_ERASE_CHECK,
					     (u8 *)&mdm_rsp_data,
					     IOSM_MDM_SEND_2, mdm_rsp);
		if (ret)
			goto ipc_erase_chk_err;

		mdm_rsp_data = *((u16 *)mdm_rsp);
		if (mdm_rsp_data > IOSM_MDM_ERASE_RSP) {
			dev_err(ipc_devlink->dev,
				"Flash Erase Check resp wrong 0x%04X",
				mdm_rsp_data);
			ret = -EINVAL;
			goto ipc_erase_chk_err;
		}
		count++;
		msleep(IOSM_FLASH_ERASE_CHECK_INTERVAL);
	} while ((mdm_rsp_data != IOSM_MDM_ERASE_RSP) &&
		(count < (IOSM_FLASH_ERASE_CHECK_TIMEOUT /
		IOSM_FLASH_ERASE_CHECK_INTERVAL)));

	if (mdm_rsp_data != IOSM_MDM_ERASE_RSP) {
		dev_err(ipc_devlink->dev, "Modem erase check timeout failure!");
		ret = -ETIMEDOUT;
	}

ipc_erase_chk_err:
	return ret;
}

/* Full erase function which will erase the nand flash through EBL command */
static int ipc_flash_full_erase(struct iosm_devlink *ipc_devlink, u8 *mdm_rsp)
{
	u32 erase_address = IOSM_ERASE_START_ADDR;
	struct iosm_flash_msg_control cmd_msg;
	u32 erase_length = IOSM_ERASE_LEN;
	int ret;

	dev_dbg(ipc_devlink->dev, "Erase full nand flash");
	cmd_msg.action = cpu_to_le32(FLASH_OOSC_ACTION_ERASE);
	cmd_msg.type = cpu_to_le32(FLASH_OOSC_TYPE_ALL_FLASH);
	cmd_msg.length = cpu_to_le32(erase_length);
	cmd_msg.arguments = cpu_to_le32(erase_address);

	ret = ipc_flash_send_receive(ipc_devlink, FLASH_OOS_CONTROL,
				     (unsigned char *)&cmd_msg,
				     IOSM_MDM_SEND_16, mdm_rsp);
	if (ret)
		goto ipc_flash_erase_err;

	ipc_devlink->param.erase_full_flash_done = IOSM_SET_FLAG;
	ret = ipc_flash_erase_check(ipc_devlink, mdm_rsp);

ipc_flash_erase_err:
	return ret;
}

/* Logic for flashing all the Loadmaps available for individual fls file */
static int ipc_flash_download_region(struct iosm_devlink *ipc_devlink,
				     const struct firmware *fw, u8 *mdm_rsp)
{
	u32 raw_len, rest_len = fw->size - IOSM_DEVLINK_HDR_SIZE;
	struct iosm_devlink_image *fls_data;
	__le32 reg_info[2]; /* 0th position region address, 1st position size */
	u32 nand_address;
	char *file_ptr;
	int ret;

	fls_data = (struct iosm_devlink_image *)fw->data;
	file_ptr = (void *)(fls_data + 1);
	nand_address = le32_to_cpu(fls_data->region_address);
	reg_info[0] = cpu_to_le32(nand_address);

	if (!ipc_devlink->param.erase_full_flash_done) {
		reg_info[1] = cpu_to_le32(nand_address + rest_len - 2);
		ret = ipc_flash_send_receive(ipc_devlink, FLASH_ERASE_START,
					     (u8 *)reg_info, IOSM_MDM_SEND_8,
					     mdm_rsp);
		if (ret)
			goto dl_region_fail;

		ret = ipc_flash_erase_check(ipc_devlink, mdm_rsp);
		if (ret)
			goto dl_region_fail;
	}

	/* Request Flash Set Address */
	ret = ipc_flash_send_receive(ipc_devlink, FLASH_SET_ADDRESS,
				     (u8 *)reg_info, IOSM_MDM_SEND_4, mdm_rsp);
	if (ret)
		goto dl_region_fail;

	/* Request Flash Write Raw Image */
	ret = ipc_flash_send_data(ipc_devlink, IOSM_EBL_DW_PACK_SIZE,
				  FLASH_WRITE_IMAGE_RAW, (u8 *)&rest_len,
				  IOSM_MDM_SEND_4);
	if (ret)
		goto dl_region_fail;

	do {
		raw_len = (rest_len > IOSM_FLS_BUF_SIZE) ? IOSM_FLS_BUF_SIZE :
				rest_len;
		ret = ipc_imem_sys_devlink_write(ipc_devlink, file_ptr,
						 raw_len);
		if (ret) {
			dev_err(ipc_devlink->dev, "Image write failed");
			goto dl_region_fail;
		}
		file_ptr += raw_len;
		rest_len -= raw_len;
	} while (rest_len);

	ret = ipc_flash_receive_data(ipc_devlink, IOSM_EBL_DW_PAYL_SIZE,
				     mdm_rsp);

dl_region_fail:
	return ret;
}

/**
 * ipc_flash_send_fls  - Inject Modem subsystem fls file to device
 * @ipc_devlink:        Pointer to devlink structure
 * @fw:                 FW image
 * @mdm_rsp:            Pointer to modem response buffer
 *
 * Returns:             0 on success and failure value on error
 */
int ipc_flash_send_fls(struct iosm_devlink *ipc_devlink,
		       const struct firmware *fw, u8 *mdm_rsp)
{
	u32 fw_size = fw->size - IOSM_DEVLINK_HDR_SIZE;
	struct iosm_devlink_image *fls_data;
	u16 flash_cmd;
	int ret;

	fls_data = (struct iosm_devlink_image *)fw->data;
	if (ipc_devlink->param.erase_full_flash) {
		ipc_devlink->param.erase_full_flash = false;
		ret = ipc_flash_full_erase(ipc_devlink, mdm_rsp);
		if (ret)
			goto ipc_flash_err;
	}

	/* Request Sec Start */
	if (!fls_data->download_region) {
		ret = ipc_flash_send_receive(ipc_devlink, FLASH_SEC_START,
					     (u8 *)fw->data +
					     IOSM_DEVLINK_HDR_SIZE, fw_size,
					     mdm_rsp);
		if (ret)
			goto ipc_flash_err;
	} else {
		/* Download regions */
		ret = ipc_flash_download_region(ipc_devlink, fw, mdm_rsp);
		if (ret)
			goto ipc_flash_err;

		if (fls_data->last_region) {
			/* Request Sec End */
			flash_cmd = IOSM_MDM_SEND_DATA;
			ret = ipc_flash_send_receive(ipc_devlink, FLASH_SEC_END,
						     (u8 *)&flash_cmd,
						     IOSM_MDM_SEND_2, mdm_rsp);
		}
	}

ipc_flash_err:
	return ret;
}

/**
 * ipc_flash_boot_psi - Inject PSI image
 * @ipc_devlink:        Pointer to devlink structure
 * @fw:                 FW image
 *
 * Returns:             0 on success and failure value on error
 */
int ipc_flash_boot_psi(struct iosm_devlink *ipc_devlink,
		       const struct firmware *fw)
{
	u32 bytes_read, psi_size = fw->size - IOSM_DEVLINK_HDR_SIZE;
	u8 psi_ack_byte[IOSM_PSI_ACK], read_data[2];
	u8 *psi_code;
	int ret;

	dev_dbg(ipc_devlink->dev, "Boot transfer PSI");
	psi_code = kmemdup(fw->data + IOSM_DEVLINK_HDR_SIZE, psi_size,
			   GFP_KERNEL);
	if (!psi_code)
		return -ENOMEM;

	ret = ipc_imem_sys_devlink_write(ipc_devlink, psi_code, psi_size);
	if (ret) {
		dev_err(ipc_devlink->dev, "RPSI Image write failed");
		goto ipc_flash_psi_free;
	}

	ret = ipc_imem_sys_devlink_read(ipc_devlink, read_data,
					IOSM_LER_ACK_SIZE, &bytes_read);
	if (ret) {
		dev_err(ipc_devlink->dev, "ipc_devlink_sio_read ACK failed");
		goto ipc_flash_psi_free;
	}

	if (bytes_read != IOSM_LER_ACK_SIZE) {
		ret = -EINVAL;
		goto ipc_flash_psi_free;
	}

	snprintf(psi_ack_byte, sizeof(psi_ack_byte), "%x%x", read_data[0],
		 read_data[1]);
	devlink_flash_update_status_notify(ipc_devlink->devlink_ctx,
					   psi_ack_byte, "PSI ACK", 0, 0);

	if (read_data[0] == 0x00 && read_data[1] == 0xCD) {
		dev_dbg(ipc_devlink->dev, "Coredump detected");
		ret = ipc_coredump_get_list(ipc_devlink,
					    rpsi_cmd_coredump_start);
		if (ret)
			dev_err(ipc_devlink->dev, "Failed to get cd list");
	}

ipc_flash_psi_free:
	kfree(psi_code);
	return ret;
}

/**
 * ipc_flash_boot_ebl  - Inject EBL image
 * @ipc_devlink:        Pointer to devlink structure
 * @fw:                 FW image
 *
 * Returns:             0 on success and failure value on error
 */
int ipc_flash_boot_ebl(struct iosm_devlink *ipc_devlink,
		       const struct firmware *fw)
{
	u32 ebl_size = fw->size - IOSM_DEVLINK_HDR_SIZE;
	u8 read_data[2];
	u32 bytes_read;
	int ret;

	if (ipc_mmio_get_exec_stage(ipc_devlink->pcie->imem->mmio) !=
				    IPC_MEM_EXEC_STAGE_PSI) {
		devlink_flash_update_status_notify(ipc_devlink->devlink_ctx,
						   "Invalid execution stage",
						   NULL, 0, 0);
		return -EINVAL;
	}

	dev_dbg(ipc_devlink->dev, "Boot transfer EBL");
	ret = ipc_devlink_send_cmd(ipc_devlink, rpsi_cmd_code_ebl,
				   IOSM_RPSI_LOAD_SIZE);
	if (ret) {
		dev_err(ipc_devlink->dev, "Sending rpsi_cmd_code_ebl failed");
		goto ipc_flash_ebl_err;
	}

	ret = ipc_imem_sys_devlink_read(ipc_devlink, read_data, IOSM_READ_SIZE,
					&bytes_read);
	if (ret) {
		dev_err(ipc_devlink->dev, "rpsi_cmd_code_ebl read failed");
		goto ipc_flash_ebl_err;
	}

	if (bytes_read != IOSM_READ_SIZE) {
		ret = -EINVAL;
		goto ipc_flash_ebl_err;
	}

	ret = ipc_imem_sys_devlink_write(ipc_devlink, (u8 *)&ebl_size,
					 sizeof(ebl_size));
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL length write failed");
		goto ipc_flash_ebl_err;
	}

	ret = ipc_imem_sys_devlink_read(ipc_devlink, read_data, IOSM_READ_SIZE,
					&bytes_read);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL read failed");
		goto ipc_flash_ebl_err;
	}

	if (bytes_read != IOSM_READ_SIZE) {
		ret = -EINVAL;
		goto ipc_flash_ebl_err;
	}

	ret = ipc_imem_sys_devlink_write(ipc_devlink,
					 (u8 *)fw->data + IOSM_DEVLINK_HDR_SIZE,
					 ebl_size);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL data transfer failed");
		goto ipc_flash_ebl_err;
	}

	ret = ipc_imem_sys_devlink_read(ipc_devlink, read_data, IOSM_READ_SIZE,
					&bytes_read);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL read failed");
		goto ipc_flash_ebl_err;
	}

	if (bytes_read != IOSM_READ_SIZE) {
		ret = -EINVAL;
		goto ipc_flash_ebl_err;
	}

	ret = ipc_imem_sys_devlink_read(ipc_devlink,
					ipc_devlink->ebl_ctx.m_ebl_resp,
					IOSM_EBL_RSP_SIZE, &bytes_read);
	if (ret) {
		dev_err(ipc_devlink->dev, "EBL response read failed");
		goto ipc_flash_ebl_err;
	}

	if (bytes_read != IOSM_EBL_RSP_SIZE)
		ret = -EINVAL;

ipc_flash_ebl_err:
	return ret;
}
