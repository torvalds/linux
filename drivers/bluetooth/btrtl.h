/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Bluetooth support for Realtek devices
 *
 *  Copyright (C) 2015 Endless Mobile, Inc.
 */

#define RTL_FRAG_LEN 252

#define rtl_dev_err(dev, fmt, ...) bt_dev_err(dev, "RTL: " fmt, ##__VA_ARGS__)
#define rtl_dev_warn(dev, fmt, ...) bt_dev_warn(dev, "RTL: " fmt, ##__VA_ARGS__)
#define rtl_dev_info(dev, fmt, ...) bt_dev_info(dev, "RTL: " fmt, ##__VA_ARGS__)
#define rtl_dev_dbg(dev, fmt, ...) bt_dev_dbg(dev, "RTL: " fmt, ##__VA_ARGS__)

struct btrtl_device_info;

struct rtl_chip_type_evt {
	__u8 status;
	__u8 type;
} __packed;

struct rtl_download_cmd {
	__u8 index;
	__u8 data[RTL_FRAG_LEN];
} __packed;

struct rtl_download_response {
	__u8 status;
	__u8 index;
} __packed;

struct rtl_rom_version_evt {
	__u8 status;
	__u8 version;
} __packed;

struct rtl_epatch_header {
	__u8 signature[8];
	__le32 fw_version;
	__le16 num_patches;
} __packed;

struct rtl_vendor_config_entry {
	__le16 offset;
	__u8 len;
	__u8 data[];
} __packed;

struct rtl_vendor_config {
	__le32 signature;
	__le16 total_len;
	struct rtl_vendor_config_entry entry[];
} __packed;

enum {
	REALTEK_ALT6_CONTINUOUS_TX_CHIP,

	__REALTEK_NUM_FLAGS,
};

struct btrealtek_data {
	DECLARE_BITMAP(flags, __REALTEK_NUM_FLAGS);
};

#define btrealtek_set_flag(hdev, nr)					\
	do {								\
		struct btrealtek_data *realtek = hci_get_priv((hdev));	\
		set_bit((nr), realtek->flags);				\
	} while (0)

#define btrealtek_get_flag(hdev)					\
	(((struct btrealtek_data *)hci_get_priv(hdev))->flags)

#define btrealtek_test_flag(hdev, nr)	test_bit((nr), btrealtek_get_flag(hdev))

#if IS_ENABLED(CONFIG_BT_RTL)

struct btrtl_device_info *btrtl_initialize(struct hci_dev *hdev,
					   const char *postfix);
void btrtl_free(struct btrtl_device_info *btrtl_dev);
int btrtl_download_firmware(struct hci_dev *hdev,
			    struct btrtl_device_info *btrtl_dev);
void btrtl_set_quirks(struct hci_dev *hdev,
		      struct btrtl_device_info *btrtl_dev);
int btrtl_setup_realtek(struct hci_dev *hdev);
int btrtl_shutdown_realtek(struct hci_dev *hdev);
int btrtl_get_uart_settings(struct hci_dev *hdev,
			    struct btrtl_device_info *btrtl_dev,
			    unsigned int *controller_baudrate,
			    u32 *device_baudrate, bool *flow_control);

#else

static inline struct btrtl_device_info *btrtl_initialize(struct hci_dev *hdev,
							 const char *postfix)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void btrtl_free(struct btrtl_device_info *btrtl_dev)
{
}

static inline int btrtl_download_firmware(struct hci_dev *hdev,
					  struct btrtl_device_info *btrtl_dev)
{
	return -EOPNOTSUPP;
}

static inline void btrtl_set_quirks(struct hci_dev *hdev,
				    struct btrtl_device_info *btrtl_dev)
{
}

static inline int btrtl_setup_realtek(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int btrtl_shutdown_realtek(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int btrtl_get_uart_settings(struct hci_dev *hdev,
					  struct btrtl_device_info *btrtl_dev,
					  unsigned int *controller_baudrate,
					  u32 *device_baudrate,
					  bool *flow_control)
{
	return -ENOENT;
}

#endif
