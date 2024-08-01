/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *  Bluetooth support for Intel devices
 *
 *  Copyright (C) 2015  Intel Corporation
 */

/* List of tlv type */
enum {
	INTEL_TLV_CNVI_TOP = 0x10,
	INTEL_TLV_CNVR_TOP,
	INTEL_TLV_CNVI_BT,
	INTEL_TLV_CNVR_BT,
	INTEL_TLV_CNVI_OTP,
	INTEL_TLV_CNVR_OTP,
	INTEL_TLV_DEV_REV_ID,
	INTEL_TLV_USB_VENDOR_ID,
	INTEL_TLV_USB_PRODUCT_ID,
	INTEL_TLV_PCIE_VENDOR_ID,
	INTEL_TLV_PCIE_DEVICE_ID,
	INTEL_TLV_PCIE_SUBSYSTEM_ID,
	INTEL_TLV_IMAGE_TYPE,
	INTEL_TLV_TIME_STAMP,
	INTEL_TLV_BUILD_TYPE,
	INTEL_TLV_BUILD_NUM,
	INTEL_TLV_FW_BUILD_PRODUCT,
	INTEL_TLV_FW_BUILD_HW,
	INTEL_TLV_FW_STEP,
	INTEL_TLV_BT_SPEC,
	INTEL_TLV_MFG_NAME,
	INTEL_TLV_HCI_REV,
	INTEL_TLV_LMP_SUBVER,
	INTEL_TLV_OTP_PATCH_VER,
	INTEL_TLV_SECURE_BOOT,
	INTEL_TLV_KEY_FROM_HDR,
	INTEL_TLV_OTP_LOCK,
	INTEL_TLV_API_LOCK,
	INTEL_TLV_DEBUG_LOCK,
	INTEL_TLV_MIN_FW,
	INTEL_TLV_LIMITED_CCE,
	INTEL_TLV_SBE_TYPE,
	INTEL_TLV_OTP_BDADDR,
	INTEL_TLV_UNLOCKED_STATE,
	INTEL_TLV_GIT_SHA1,
	INTEL_TLV_FW_ID = 0x50
};

struct intel_tlv {
	u8 type;
	u8 len;
	u8 val[];
} __packed;

#define BTINTEL_CNVI_BLAZARI		0x900

#define BTINTEL_IMG_BOOTLOADER		0x01	/* Bootloader image */
#define BTINTEL_IMG_IML			0x02	/* Intermediate image */
#define BTINTEL_IMG_OP			0x03	/* Operational image */

#define BTINTEL_FWID_MAXLEN 64

struct intel_version_tlv {
	u32	cnvi_top;
	u32	cnvr_top;
	u32	cnvi_bt;
	u32	cnvr_bt;
	u16	dev_rev_id;
	u8	img_type;
	u16	timestamp;
	u8	build_type;
	u32	build_num;
	u8	secure_boot;
	u8	otp_lock;
	u8	api_lock;
	u8	debug_lock;
	u8	min_fw_build_nn;
	u8	min_fw_build_cw;
	u8	min_fw_build_yy;
	u8	limited_cce;
	u8	sbe_type;
	u32	git_sha1;
	u8	fw_id[BTINTEL_FWID_MAXLEN];
	bdaddr_t otp_bd_addr;
};

struct intel_version {
	u8 status;
	u8 hw_platform;
	u8 hw_variant;
	u8 hw_revision;
	u8 fw_variant;
	u8 fw_revision;
	u8 fw_build_num;
	u8 fw_build_ww;
	u8 fw_build_yy;
	u8 fw_patch_num;
} __packed;

struct intel_boot_params {
	__u8     status;
	__u8     otp_format;
	__u8     otp_content;
	__u8     otp_patch;
	__le16   dev_revid;
	__u8     secure_boot;
	__u8     key_from_hdr;
	__u8     key_type;
	__u8     otp_lock;
	__u8     api_lock;
	__u8     debug_lock;
	bdaddr_t otp_bdaddr;
	__u8     min_fw_build_nn;
	__u8     min_fw_build_cw;
	__u8     min_fw_build_yy;
	__u8     limited_cce;
	__u8     unlocked_state;
} __packed;

struct intel_bootup {
	__u8     zero;
	__u8     num_cmds;
	__u8     source;
	__u8     reset_type;
	__u8     reset_reason;
	__u8     ddc_status;
} __packed;

struct intel_secure_send_result {
	__u8     result;
	__le16   opcode;
	__u8     status;
} __packed;

struct intel_reset {
	__u8     reset_type;
	__u8     patch_enable;
	__u8     ddc_reload;
	__u8     boot_option;
	__le32   boot_param;
} __packed;

struct intel_debug_features {
	__u8    page1[16];
} __packed;

struct intel_offload_use_cases {
	__u8	status;
	__u8	preset[8];
} __packed;

#define INTEL_OP_PPAG_CMD		0xFE0B
struct hci_ppag_enable_cmd {
	__le32	ppag_enable_flags;
} __packed;

#define INTEL_TLV_TYPE_ID		0x01

#define INTEL_TLV_SYSTEM_EXCEPTION	0x00
#define INTEL_TLV_FATAL_EXCEPTION	0x01
#define INTEL_TLV_DEBUG_EXCEPTION	0x02
#define INTEL_TLV_TEST_EXCEPTION	0xDE

#define INTEL_HW_PLATFORM(cnvx_bt)	((u8)(((cnvx_bt) & 0x0000ff00) >> 8))
#define INTEL_HW_VARIANT(cnvx_bt)	((u8)(((cnvx_bt) & 0x003f0000) >> 16))
#define INTEL_CNVX_TOP_TYPE(cnvx_top)	((cnvx_top) & 0x00000fff)
#define INTEL_CNVX_TOP_STEP(cnvx_top)	(((cnvx_top) & 0x0f000000) >> 24)
#define INTEL_CNVX_TOP_PACK_SWAB(t, s)	__swab16(((__u16)(((t) << 4) | (s))))

enum {
	INTEL_BOOTLOADER,
	INTEL_DOWNLOADING,
	INTEL_FIRMWARE_LOADED,
	INTEL_FIRMWARE_FAILED,
	INTEL_BOOTING,
	INTEL_BROKEN_INITIAL_NCMD,
	INTEL_BROKEN_SHUTDOWN_LED,
	INTEL_ROM_LEGACY,
	INTEL_ROM_LEGACY_NO_WBS_SUPPORT,
	INTEL_ACPI_RESET_ACTIVE,

	__INTEL_NUM_FLAGS,
};

struct btintel_data {
	DECLARE_BITMAP(flags, __INTEL_NUM_FLAGS);
	int (*acpi_reset_method)(struct hci_dev *hdev);
};

#define btintel_set_flag(hdev, nr)					\
	do {								\
		struct btintel_data *intel = hci_get_priv((hdev));	\
		set_bit((nr), intel->flags);				\
	} while (0)

#define btintel_clear_flag(hdev, nr)					\
	do {								\
		struct btintel_data *intel = hci_get_priv((hdev));	\
		clear_bit((nr), intel->flags);				\
	} while (0)

#define btintel_wake_up_flag(hdev, nr)					\
	do {								\
		struct btintel_data *intel = hci_get_priv((hdev));	\
		wake_up_bit(intel->flags, (nr));			\
	} while (0)

#define btintel_get_flag(hdev)						\
	(((struct btintel_data *)hci_get_priv(hdev))->flags)

#define btintel_test_flag(hdev, nr)	test_bit((nr), btintel_get_flag(hdev))
#define btintel_test_and_clear_flag(hdev, nr) test_and_clear_bit((nr), btintel_get_flag(hdev))
#define btintel_wait_on_flag_timeout(hdev, nr, m, to)			\
		wait_on_bit_timeout(btintel_get_flag(hdev), (nr), m, to)

#if IS_ENABLED(CONFIG_BT_INTEL) || IS_ENABLED(CONFIG_BT_INTEL_PCIE)

int btintel_check_bdaddr(struct hci_dev *hdev);
int btintel_enter_mfg(struct hci_dev *hdev);
int btintel_exit_mfg(struct hci_dev *hdev, bool reset, bool patched);
int btintel_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr);
int btintel_set_diag(struct hci_dev *hdev, bool enable);

int btintel_version_info(struct hci_dev *hdev, struct intel_version *ver);
int btintel_load_ddc_config(struct hci_dev *hdev, const char *ddc_name);
int btintel_set_event_mask_mfg(struct hci_dev *hdev, bool debug);
int btintel_read_version(struct hci_dev *hdev, struct intel_version *ver);
struct regmap *btintel_regmap_init(struct hci_dev *hdev, u16 opcode_read,
				   u16 opcode_write);
int btintel_send_intel_reset(struct hci_dev *hdev, u32 boot_param);
int btintel_read_boot_params(struct hci_dev *hdev,
			     struct intel_boot_params *params);
int btintel_download_firmware(struct hci_dev *dev, struct intel_version *ver,
			      const struct firmware *fw, u32 *boot_param);
int btintel_configure_setup(struct hci_dev *hdev, const char *driver_name);
int btintel_recv_event(struct hci_dev *hdev, struct sk_buff *skb);
void btintel_bootup(struct hci_dev *hdev, const void *ptr, unsigned int len);
void btintel_secure_send_result(struct hci_dev *hdev,
				const void *ptr, unsigned int len);
int btintel_set_quality_report(struct hci_dev *hdev, bool enable);
int btintel_version_info_tlv(struct hci_dev *hdev,
			     struct intel_version_tlv *version);
int btintel_parse_version_tlv(struct hci_dev *hdev,
			      struct intel_version_tlv *version,
			      struct sk_buff *skb);
void btintel_set_msft_opcode(struct hci_dev *hdev, u8 hw_variant);
int btintel_bootloader_setup_tlv(struct hci_dev *hdev,
				 struct intel_version_tlv *ver);
int btintel_shutdown_combined(struct hci_dev *hdev);
void btintel_hw_error(struct hci_dev *hdev, u8 code);
void btintel_print_fseq_info(struct hci_dev *hdev);
#else

static inline int btintel_check_bdaddr(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int btintel_enter_mfg(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int btintel_exit_mfg(struct hci_dev *hdev, bool reset, bool patched)
{
	return -EOPNOTSUPP;
}

static inline int btintel_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	return -EOPNOTSUPP;
}

static inline int btintel_set_diag(struct hci_dev *hdev, bool enable)
{
	return -EOPNOTSUPP;
}

static inline int btintel_version_info(struct hci_dev *hdev,
				       struct intel_version *ver)
{
	return -EOPNOTSUPP;
}

static inline int btintel_load_ddc_config(struct hci_dev *hdev,
					  const char *ddc_name)
{
	return -EOPNOTSUPP;
}

static inline int btintel_set_event_mask_mfg(struct hci_dev *hdev, bool debug)
{
	return -EOPNOTSUPP;
}

static inline int btintel_read_version(struct hci_dev *hdev,
				       struct intel_version *ver)
{
	return -EOPNOTSUPP;
}

static inline struct regmap *btintel_regmap_init(struct hci_dev *hdev,
						 u16 opcode_read,
						 u16 opcode_write)
{
	return ERR_PTR(-EINVAL);
}

static inline int btintel_send_intel_reset(struct hci_dev *hdev,
					   u32 reset_param)
{
	return -EOPNOTSUPP;
}

static inline int btintel_read_boot_params(struct hci_dev *hdev,
					   struct intel_boot_params *params)
{
	return -EOPNOTSUPP;
}

static inline int btintel_download_firmware(struct hci_dev *dev,
					    const struct firmware *fw,
					    u32 *boot_param)
{
	return -EOPNOTSUPP;
}

static inline int btintel_configure_setup(struct hci_dev *hdev,
					  const char *driver_name)
{
	return -ENODEV;
}

static inline void btintel_bootup(struct hci_dev *hdev,
				  const void *ptr, unsigned int len)
{
}

static inline void btintel_secure_send_result(struct hci_dev *hdev,
				const void *ptr, unsigned int len)
{
}

static inline int btintel_set_quality_report(struct hci_dev *hdev, bool enable)
{
	return -ENODEV;
}

static inline int btintel_version_info_tlv(struct hci_dev *hdev,
					   struct intel_version_tlv *version)
{
	return -EOPNOTSUPP;
}

static inline int btintel_parse_version_tlv(struct hci_dev *hdev,
					    struct intel_version_tlv *version,
					    struct sk_buff *skb)
{
	return -EOPNOTSUPP;
}

static inline void btintel_set_msft_opcode(struct hci_dev *hdev, u8 hw_variant)

{
}

static inline int btintel_bootloader_setup_tlv(struct hci_dev *hdev,
					       struct intel_version_tlv *ver)
{
	return -ENODEV;
}

static inline int btintel_shutdown_combined(struct hci_dev *hdev)
{
	return -ENODEV;
}

static inline void btintel_hw_error(struct hci_dev *hdev, u8 code)
{
}

static inline void btintel_print_fseq_info(struct hci_dev *hdev)
{
}
#endif
