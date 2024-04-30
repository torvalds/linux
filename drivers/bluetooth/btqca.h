/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Bluetooth supports for Qualcomm Atheros ROME chips
 *
 *  Copyright (c) 2015 The Linux Foundation. All rights reserved.
 */

#define EDL_PATCH_CMD_OPCODE		(0xFC00)
#define EDL_NVM_ACCESS_OPCODE		(0xFC0B)
#define EDL_WRITE_BD_ADDR_OPCODE	(0xFC14)
#define EDL_PATCH_CMD_LEN		(1)
#define EDL_PATCH_VER_REQ_CMD		(0x19)
#define EDL_PATCH_TLV_REQ_CMD		(0x1E)
#define EDL_GET_BUILD_INFO_CMD		(0x20)
#define EDL_GET_BID_REQ_CMD			(0x23)
#define EDL_NVM_ACCESS_SET_REQ_CMD	(0x01)
#define EDL_PATCH_CONFIG_CMD		(0x28)
#define MAX_SIZE_PER_TLV_SEGMENT	(243)
#define QCA_PRE_SHUTDOWN_CMD		(0xFC08)
#define QCA_DISABLE_LOGGING		(0xFC17)

#define EDL_CMD_REQ_RES_EVT		(0x00)
#define EDL_PATCH_VER_RES_EVT		(0x19)
#define EDL_APP_VER_RES_EVT		(0x02)
#define EDL_TVL_DNLD_RES_EVT		(0x04)
#define EDL_CMD_EXE_STATUS_EVT		(0x00)
#define EDL_SET_BAUDRATE_RSP_EVT	(0x92)
#define EDL_NVM_ACCESS_CODE_EVT		(0x0B)
#define EDL_PATCH_CONFIG_RES_EVT	(0x00)
#define QCA_DISABLE_LOGGING_SUB_OP	(0x14)

#define EDL_TAG_ID_BD_ADDR		2
#define EDL_TAG_ID_HCI			(17)
#define EDL_TAG_ID_DEEP_SLEEP		(27)

#define QCA_WCN3990_POWERON_PULSE	0xFC
#define QCA_WCN3990_POWEROFF_PULSE	0xC0

#define QCA_HCI_CC_OPCODE		0xFC00
#define QCA_HCI_CC_SUCCESS		0x00

#define QCA_WCN3991_SOC_ID		(0x40014320)

/* QCA chipset version can be decided by patch and SoC
 * version, combination with upper 2 bytes from SoC
 * and lower 2 bytes from patch will be used.
 */
#define get_soc_ver(soc_id, rom_ver)	\
	((le32_to_cpu(soc_id) << 16) | (le16_to_cpu(rom_ver)))

#define QCA_FW_BUILD_VER_LEN		255
#define QCA_HSP_GF_SOC_ID			0x1200
#define QCA_HSP_GF_SOC_MASK			0x0000ff00

enum qca_baudrate {
	QCA_BAUDRATE_115200 	= 0,
	QCA_BAUDRATE_57600,
	QCA_BAUDRATE_38400,
	QCA_BAUDRATE_19200,
	QCA_BAUDRATE_9600,
	QCA_BAUDRATE_230400,
	QCA_BAUDRATE_250000,
	QCA_BAUDRATE_460800,
	QCA_BAUDRATE_500000,
	QCA_BAUDRATE_720000,
	QCA_BAUDRATE_921600,
	QCA_BAUDRATE_1000000,
	QCA_BAUDRATE_1250000,
	QCA_BAUDRATE_2000000,
	QCA_BAUDRATE_3000000,
	QCA_BAUDRATE_4000000,
	QCA_BAUDRATE_1600000,
	QCA_BAUDRATE_3200000,
	QCA_BAUDRATE_3500000,
	QCA_BAUDRATE_AUTO 	= 0xFE,
	QCA_BAUDRATE_RESERVED
};

enum qca_tlv_dnld_mode {
	QCA_SKIP_EVT_NONE,
	QCA_SKIP_EVT_VSE,
	QCA_SKIP_EVT_CC,
	QCA_SKIP_EVT_VSE_CC
};

enum qca_tlv_type {
	TLV_TYPE_PATCH = 1,
	TLV_TYPE_NVM,
	ELF_TYPE_PATCH,
};

struct qca_fw_config {
	u8 type;
	char fwname[64];
	uint8_t user_baud_rate;
	enum qca_tlv_dnld_mode dnld_mode;
	enum qca_tlv_dnld_mode dnld_type;
	bdaddr_t bdaddr;
};

struct edl_event_hdr {
	__u8 cresp;
	__u8 rtype;
	__u8 data[];
} __packed;

struct qca_btsoc_version {
	__le32 product_id;
	__le16 patch_ver;
	__le16 rom_ver;
	__le32 soc_id;
} __packed;

struct tlv_seg_resp {
	__u8 result;
} __packed;

struct tlv_type_patch {
	__le32 total_size;
	__le32 data_length;
	__u8   format_version;
	__u8   signature;
	__u8   download_mode;
	__u8   reserved1;
	__le16 product_id;
	__le16 rom_build;
	__le16 patch_version;
	__le16 reserved2;
	__le32 entry;
} __packed;

struct tlv_type_nvm {
	__le16 tag_id;
	__le16 tag_len;
	__le32 reserve1;
	__le32 reserve2;
	__u8   data[];
} __packed;

struct tlv_type_hdr {
	__le32 type_len;
	__u8   data[];
} __packed;

enum qca_btsoc_type {
	QCA_INVALID = -1,
	QCA_AR3002,
	QCA_ROME,
	QCA_WCN3988,
	QCA_WCN3990,
	QCA_WCN3998,
	QCA_WCN3991,
	QCA_QCA2066,
	QCA_QCA6390,
	QCA_WCN6750,
	QCA_WCN6855,
	QCA_WCN7850,
};

#if IS_ENABLED(CONFIG_BT_QCA)

int qca_set_bdaddr_rome(struct hci_dev *hdev, const bdaddr_t *bdaddr);
int qca_uart_setup(struct hci_dev *hdev, uint8_t baudrate,
		   enum qca_btsoc_type soc_type, struct qca_btsoc_version ver,
		   const char *firmware_name);
int qca_read_soc_version(struct hci_dev *hdev, struct qca_btsoc_version *ver,
			 enum qca_btsoc_type);
int qca_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr);
int qca_send_pre_shutdown_cmd(struct hci_dev *hdev);
#else

static inline int qca_set_bdaddr_rome(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	return -EOPNOTSUPP;
}

static inline int qca_uart_setup(struct hci_dev *hdev, uint8_t baudrate,
				 enum qca_btsoc_type soc_type,
				 struct qca_btsoc_version ver,
				 const char *firmware_name)
{
	return -EOPNOTSUPP;
}

static inline int qca_read_soc_version(struct hci_dev *hdev,
				       struct qca_btsoc_version *ver,
				       enum qca_btsoc_type)
{
	return -EOPNOTSUPP;
}

static inline int qca_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	return -EOPNOTSUPP;
}

static inline int qca_send_pre_shutdown_cmd(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}
#endif
