/*
 * NCI based driver for Samsung S3FWRN5 NFC chip
 *
 * Copyright (C) 2015 Samsung Electrnoics
 * Robert Baldyga <r.baldyga@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LOCAL_S3FWRN5_FIRMWARE_H_
#define __LOCAL_S3FWRN5_FIRMWARE_H_

/* FW Message Types */
#define S3FWRN5_FW_MSG_CMD			0x00
#define S3FWRN5_FW_MSG_RSP			0x01
#define S3FWRN5_FW_MSG_DATA			0x02

/* FW Return Codes */
#define S3FWRN5_FW_RET_SUCCESS			0x00
#define S3FWRN5_FW_RET_MESSAGE_TYPE_INVALID	0x01
#define S3FWRN5_FW_RET_COMMAND_INVALID		0x02
#define S3FWRN5_FW_RET_PAGE_DATA_OVERFLOW	0x03
#define S3FWRN5_FW_RET_SECT_DATA_OVERFLOW	0x04
#define S3FWRN5_FW_RET_AUTHENTICATION_FAIL	0x05
#define S3FWRN5_FW_RET_FLASH_OPERATION_FAIL	0x06
#define S3FWRN5_FW_RET_ADDRESS_OUT_OF_RANGE	0x07
#define S3FWRN5_FW_RET_PARAMETER_INVALID	0x08

/* ---- FW Packet structures ---- */
#define S3FWRN5_FW_HDR_SIZE 4

struct s3fwrn5_fw_header {
	__u8 type;
	__u8 code;
	__u16 len;
};

#define S3FWRN5_FW_CMD_RESET			0x00

#define S3FWRN5_FW_CMD_GET_BOOTINFO		0x01

struct s3fwrn5_fw_cmd_get_bootinfo_rsp {
	__u8 hw_version[4];
	__u16 sector_size;
	__u16 page_size;
	__u16 frame_max_size;
	__u16 hw_buffer_size;
};

#define S3FWRN5_FW_CMD_ENTER_UPDATE_MODE	0x02

struct s3fwrn5_fw_cmd_enter_updatemode {
	__u16 hashcode_size;
	__u16 signature_size;
};

#define S3FWRN5_FW_CMD_UPDATE_SECTOR		0x04

struct s3fwrn5_fw_cmd_update_sector {
	__u32 base_address;
};

#define S3FWRN5_FW_CMD_COMPLETE_UPDATE_MODE	0x05

struct s3fwrn5_fw_image {
	const struct firmware *fw;

	char date[13];
	u32 version;
	const void *sig;
	u32 sig_size;
	const void *image;
	u32 image_sectors;
	const void *custom_sig;
	u32 custom_sig_size;
};

struct s3fwrn5_fw_info {
	struct nci_dev *ndev;
	struct s3fwrn5_fw_image fw;
	char fw_name[NFC_FIRMWARE_NAME_MAXSIZE + 1];

	const void *sig;
	u32 sig_size;
	u32 sector_size;
	u32 base_addr;

	struct completion completion;
	struct sk_buff *rsp;
	char parity;
};

void s3fwrn5_fw_init(struct s3fwrn5_fw_info *fw_info, const char *fw_name);
int s3fwrn5_fw_setup(struct s3fwrn5_fw_info *fw_info);
bool s3fwrn5_fw_check_version(struct s3fwrn5_fw_info *fw_info, u32 version);
int s3fwrn5_fw_download(struct s3fwrn5_fw_info *fw_info);
void s3fwrn5_fw_cleanup(struct s3fwrn5_fw_info *fw_info);

int s3fwrn5_fw_recv_frame(struct nci_dev *ndev, struct sk_buff *skb);

#endif /* __LOCAL_S3FWRN5_FIRMWARE_H_ */
