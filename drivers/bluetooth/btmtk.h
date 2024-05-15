/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2021 MediaTek Inc. */

#define FIRMWARE_MT7622		"mediatek/mt7622pr2h.bin"
#define FIRMWARE_MT7663		"mediatek/mt7663pr2h.bin"
#define FIRMWARE_MT7668		"mediatek/mt7668pr2h.bin"
#define FIRMWARE_MT7922		"mediatek/BT_RAM_CODE_MT7922_1_1_hdr.bin"
#define FIRMWARE_MT7961		"mediatek/BT_RAM_CODE_MT7961_1_2_hdr.bin"
#define FIRMWARE_MT7925		"mediatek/mt7925/BT_RAM_CODE_MT7925_1_1_hdr.bin"

#define HCI_EV_WMT 0xe4
#define HCI_WMT_MAX_EVENT_SIZE		64

#define BTMTK_WMT_REG_WRITE 0x1
#define BTMTK_WMT_REG_READ 0x2

#define MT7921_BTSYS_RST 0x70002610
#define MT7921_BTSYS_RST_WITH_GPIO BIT(7)

#define MT7921_PINMUX_0 0x70005050
#define MT7921_PINMUX_1 0x70005054

#define MT7921_DLSTATUS 0x7c053c10
#define BT_DL_STATE BIT(1)

#define MTK_COREDUMP_SIZE		(1024 * 1000)
#define MTK_COREDUMP_END		"coredump end"
#define MTK_COREDUMP_END_LEN		(sizeof(MTK_COREDUMP_END))
#define MTK_COREDUMP_NUM		255

enum {
	BTMTK_WMT_PATCH_DWNLD = 0x1,
	BTMTK_WMT_TEST = 0x2,
	BTMTK_WMT_WAKEUP = 0x3,
	BTMTK_WMT_HIF = 0x4,
	BTMTK_WMT_FUNC_CTRL = 0x6,
	BTMTK_WMT_RST = 0x7,
	BTMTK_WMT_REGISTER = 0x8,
	BTMTK_WMT_SEMAPHORE = 0x17,
};

enum {
	BTMTK_WMT_INVALID,
	BTMTK_WMT_PATCH_UNDONE,
	BTMTK_WMT_PATCH_PROGRESS,
	BTMTK_WMT_PATCH_DONE,
	BTMTK_WMT_ON_UNDONE,
	BTMTK_WMT_ON_DONE,
	BTMTK_WMT_ON_PROGRESS,
};

struct btmtk_wmt_hdr {
	u8	dir;
	u8	op;
	__le16	dlen;
	u8	flag;
} __packed;

struct btmtk_hci_wmt_cmd {
	struct btmtk_wmt_hdr hdr;
	u8 data[];
} __packed;

struct btmtk_hci_wmt_evt {
	struct hci_event_hdr hhdr;
	struct btmtk_wmt_hdr whdr;
} __packed;

struct btmtk_hci_wmt_evt_funcc {
	struct btmtk_hci_wmt_evt hwhdr;
	__be16 status;
} __packed;

struct btmtk_hci_wmt_evt_reg {
	struct btmtk_hci_wmt_evt hwhdr;
	u8 rsv[2];
	u8 num;
	__le32 addr;
	__le32 val;
} __packed;

struct btmtk_tci_sleep {
	u8 mode;
	__le16 duration;
	__le16 host_duration;
	u8 host_wakeup_pin;
	u8 time_compensation;
} __packed;

struct btmtk_wakeon {
	u8 mode;
	u8 gpo;
	u8 active_high;
	__le16 enable_delay;
	__le16 wakeup_delay;
} __packed;

struct btmtk_sco {
	u8 clock_config;
	u8 transmit_format_config;
	u8 channel_format_config;
	u8 channel_select_config;
} __packed;

struct reg_read_cmd {
	u8 type;
	u8 rsv;
	u8 num;
	__le32 addr;
} __packed;

struct reg_write_cmd {
	u8 type;
	u8 rsv;
	u8 num;
	__le32 addr;
	__le32 data;
	__le32 mask;
} __packed;

struct btmtk_hci_wmt_params {
	u8 op;
	u8 flag;
	u16 dlen;
	const void *data;
	u32 *status;
};

typedef int (*btmtk_reset_sync_func_t)(struct hci_dev *, void *);

struct btmtk_coredump_info {
	const char *driver_name;
	u32 fw_version;
	u16 cnt;
	int state;
};

struct btmediatek_data {
	u32 dev_id;
	btmtk_reset_sync_func_t reset_sync;
	struct btmtk_coredump_info cd_info;
};

typedef int (*wmt_cmd_sync_func_t)(struct hci_dev *,
				   struct btmtk_hci_wmt_params *);

#if IS_ENABLED(CONFIG_BT_MTK)

int btmtk_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr);

int btmtk_setup_firmware_79xx(struct hci_dev *hdev, const char *fwname,
			      wmt_cmd_sync_func_t wmt_cmd_sync);

int btmtk_setup_firmware(struct hci_dev *hdev, const char *fwname,
			 wmt_cmd_sync_func_t wmt_cmd_sync);

void btmtk_reset_sync(struct hci_dev *hdev);

int btmtk_register_coredump(struct hci_dev *hdev, const char *name,
			    u32 fw_version);

int btmtk_process_coredump(struct hci_dev *hdev, struct sk_buff *skb);

void btmtk_fw_get_filename(char *buf, size_t size, u32 dev_id, u32 fw_ver,
			   u32 fw_flavor);
#else

static inline int btmtk_set_bdaddr(struct hci_dev *hdev,
				   const bdaddr_t *bdaddr)
{
	return -EOPNOTSUPP;
}

static int btmtk_setup_firmware_79xx(struct hci_dev *hdev, const char *fwname,
				     wmt_cmd_sync_func_t wmt_cmd_sync)
{
	return -EOPNOTSUPP;
}

static int btmtk_setup_firmware(struct hci_dev *hdev, const char *fwname,
				wmt_cmd_sync_func_t wmt_cmd_sync)
{
	return -EOPNOTSUPP;
}

static void btmtk_reset_sync(struct hci_dev *hdev)
{
}

static int btmtk_register_coredump(struct hci_dev *hdev, const char *name,
				   u32 fw_version)
{
	return -EOPNOTSUPP;
}

static int btmtk_process_coredump(struct hci_dev *hdev, struct sk_buff *skb)
{
	return -EOPNOTSUPP;
}

static void btmtk_fw_get_filename(char *buf, size_t size, u32 dev_id,
				  u32 fw_ver, u32 fw_flavor)
{
}
#endif
