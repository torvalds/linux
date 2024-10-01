/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#ifndef _IOSM_IPC_FLASH_H
#define _IOSM_IPC_FLASH_H

/* Buffer size used to read the fls image */
#define IOSM_FLS_BUF_SIZE 0x00100000
/* Full erase start address */
#define IOSM_ERASE_START_ADDR 0x00000000
/* Erase length for NAND flash */
#define IOSM_ERASE_LEN 0xFFFFFFFF
/* EBL response Header size */
#define IOSM_EBL_HEAD_SIZE  8
/* EBL payload size */
#define IOSM_EBL_W_PAYL_SIZE  2048
/* Total EBL pack size */
#define IOSM_EBL_W_PACK_SIZE  (IOSM_EBL_HEAD_SIZE + IOSM_EBL_W_PAYL_SIZE)
/* EBL payload size */
#define IOSM_EBL_DW_PAYL_SIZE  16384
/* Total EBL pack size */
#define IOSM_EBL_DW_PACK_SIZE  (IOSM_EBL_HEAD_SIZE + IOSM_EBL_DW_PAYL_SIZE)
/* EBL name size */
#define IOSM_EBL_NAME  32
/* Maximum supported error types */
#define IOSM_MAX_ERRORS 8
/* Read size for RPSI/EBL response */
#define IOSM_READ_SIZE 2
/* Link establishment response ack size */
#define IOSM_LER_ACK_SIZE 2
/* PSI ACK len */
#define IOSM_PSI_ACK 8
/* SWID capability for packed swid type */
#define IOSM_EXT_CAP_SWID_OOS_PACK     0x02
/* EBL error response buffer */
#define IOSM_EBL_RSP_BUFF 0x0041
/* SWID string length */
#define IOSM_SWID_STR 64
/* Load EBL command size */
#define IOSM_RPSI_LOAD_SIZE 0
/* EBL payload checksum */
#define IOSM_EBL_CKSM 0x0000FFFF
/* SWID msg len and argument */
#define IOSM_MSG_LEN_ARG 0
/* Data to be sent to modem */
#define IOSM_MDM_SEND_DATA 0x0000
/* Data received from modem as part of erase check */
#define IOSM_MDM_ERASE_RSP 0x0001
/* Bit shift to calculate Checksum */
#define IOSM_EBL_PAYL_SHIFT 16
/* Flag To be set */
#define IOSM_SET_FLAG 1
/* Set flash erase check timeout to 100 msec */
#define IOSM_FLASH_ERASE_CHECK_TIMEOUT 100
/* Set flash erase check interval to 20 msec */
#define IOSM_FLASH_ERASE_CHECK_INTERVAL 20
/* Link establishment response ack size */
#define IOSM_LER_RSP_SIZE 60

/**
 * enum iosm_flash_package_type -	Enum for the flashing operations
 * @FLASH_SET_PROT_CONF:	Write EBL capabilities
 * @FLASH_SEC_START:		Start writing the secpack
 * @FLASH_SEC_END:		Validate secpack end
 * @FLASH_SET_ADDRESS:		Set the address for flashing
 * @FLASH_ERASE_START:		Start erase before flashing
 * @FLASH_ERASE_CHECK:		Validate the erase functionality
 * @FLASH_OOS_CONTROL:		Retrieve data based on oos actions
 * @FLASH_OOS_DATA_READ:	Read data from EBL
 * @FLASH_WRITE_IMAGE_RAW:	Write the raw image to flash
 */
enum iosm_flash_package_type {
	FLASH_SET_PROT_CONF = 0x0086,
	FLASH_SEC_START = 0x0204,
	FLASH_SEC_END,
	FLASH_SET_ADDRESS = 0x0802,
	FLASH_ERASE_START = 0x0805,
	FLASH_ERASE_CHECK,
	FLASH_OOS_CONTROL = 0x080C,
	FLASH_OOS_DATA_READ = 0x080E,
	FLASH_WRITE_IMAGE_RAW,
};

/**
 * enum iosm_out_of_session_action -	Actions possible over the
 *					OutOfSession command interface
 * @FLASH_OOSC_ACTION_READ:		Read data according to its type
 * @FLASH_OOSC_ACTION_ERASE:		Erase data according to its type
 */
enum iosm_out_of_session_action {
	FLASH_OOSC_ACTION_READ = 2,
	FLASH_OOSC_ACTION_ERASE = 3,
};

/**
 * enum iosm_out_of_session_type -	Data types that can be handled over the
 *					Out Of Session command Interface
 * @FLASH_OOSC_TYPE_ALL_FLASH:		The whole flash area
 * @FLASH_OOSC_TYPE_SWID_TABLE:		Read the swid table from the target
 */
enum iosm_out_of_session_type {
	FLASH_OOSC_TYPE_ALL_FLASH = 8,
	FLASH_OOSC_TYPE_SWID_TABLE = 16,
};

/**
 * enum iosm_ebl_caps -	EBL capability settings
 * @IOSM_CAP_NOT_ENHANCED:	If capability not supported
 * @IOSM_CAP_USE_EXT_CAP:	To be set if extended capability is set
 * @IOSM_EXT_CAP_ERASE_ALL:	Set Erase all capability
 * @IOSM_EXT_CAP_COMMIT_ALL:	Set the commit all capability
 */
enum iosm_ebl_caps {
	IOSM_CAP_NOT_ENHANCED = 0x00,
	IOSM_CAP_USE_EXT_CAP = 0x01,
	IOSM_EXT_CAP_ERASE_ALL = 0x08,
	IOSM_EXT_CAP_COMMIT_ALL = 0x20,
};

/**
 * enum iosm_ebl_rsp -  EBL response field
 * @EBL_CAPS_FLAG:	EBL capability flag
 * @EBL_SKIP_ERASE:	EBL skip erase flag
 * @EBL_SKIP_CRC:	EBL skip wr_pack crc
 * @EBL_EXT_CAPS_HANDLED:	EBL extended capability handled flag
 * @EBL_OOS_CONFIG:	EBL oos configuration
 * @EBL_RSP_SW_INFO_VER: EBL SW info version
 */
enum iosm_ebl_rsp {
	EBL_CAPS_FLAG = 50,
	EBL_SKIP_ERASE = 54,
	EBL_SKIP_CRC = 55,
	EBL_EXT_CAPS_HANDLED = 57,
	EBL_OOS_CONFIG = 64,
	EBL_RSP_SW_INFO_VER = 70,
};

/**
 * enum iosm_mdm_send_recv_data - Data to send to modem
 * @IOSM_MDM_SEND_2:	Send 2 bytes of payload
 * @IOSM_MDM_SEND_4:	Send 4 bytes of payload
 * @IOSM_MDM_SEND_8:	Send 8 bytes of payload
 * @IOSM_MDM_SEND_16:	Send 16 bytes of payload
 */
enum iosm_mdm_send_recv_data {
	IOSM_MDM_SEND_2 = 2,
	IOSM_MDM_SEND_4 = 4,
	IOSM_MDM_SEND_8 = 8,
	IOSM_MDM_SEND_16 = 16,
};

/**
 * struct iosm_ebl_one_error -	Structure containing error details
 * @error_class:		Error type- standard, security and text error
 * @error_code:			Specific error from error type
 */
struct iosm_ebl_one_error {
	u16 error_class;
	u16 error_code;
};

/**
 * struct iosm_ebl_error- Structure with max error type supported
 * @error:		Array of one_error structure with max errors
 */
struct iosm_ebl_error {
	struct iosm_ebl_one_error error[IOSM_MAX_ERRORS];
};

/**
 * struct iosm_swid_table - SWID table data for modem
 * @number_of_data_sets:	Number of swid types
 * @sw_id_type:			SWID type - SWID
 * @sw_id_val:			SWID value
 * @rf_engine_id_type:		RF engine ID type - RF_ENGINE_ID
 * @rf_engine_id_val:		RF engine ID value
 */
struct iosm_swid_table {
	u32 number_of_data_sets;
	char sw_id_type[IOSM_EBL_NAME];
	u32 sw_id_val;
	char rf_engine_id_type[IOSM_EBL_NAME];
	u32 rf_engine_id_val;
};

/**
 * struct iosm_flash_msg_control - Data sent to modem
 * @action:	Action to be performed
 * @type:	Type of action
 * @length:	Length of the action
 * @arguments:	Argument value sent to modem
 */
struct iosm_flash_msg_control {
	__le32 action;
	__le32 type;
	__le32 length;
	__le32 arguments;
};

/**
 * struct iosm_flash_data -  Header Data to be sent to modem
 * @checksum:	Checksum value calculated for the payload data
 * @pack_id:	Flash Action type
 * @msg_length:	Payload length
 */
struct iosm_flash_data {
	__le16  checksum;
	__le16  pack_id;
	__le32  msg_length;
};

int ipc_flash_boot_psi(struct iosm_devlink *ipc_devlink,
		       const struct firmware *fw);

int ipc_flash_boot_ebl(struct iosm_devlink *ipc_devlink,
		       const struct firmware *fw);

int ipc_flash_boot_set_capabilities(struct iosm_devlink *ipc_devlink,
				    u8 *mdm_rsp);

int ipc_flash_link_establish(struct iosm_imem *ipc_imem);

int ipc_flash_read_swid(struct iosm_devlink *ipc_devlink, u8 *mdm_rsp);

int ipc_flash_send_fls(struct iosm_devlink *ipc_devlink,
		       const struct firmware *fw, u8 *mdm_rsp);
#endif
