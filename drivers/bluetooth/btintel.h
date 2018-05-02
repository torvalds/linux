/*
 *
 *  Bluetooth support for Intel devices
 *
 *  Copyright (C) 2015  Intel Corporation
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

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

#if IS_ENABLED(CONFIG_BT_INTEL)

int btintel_check_bdaddr(struct hci_dev *hdev);
int btintel_enter_mfg(struct hci_dev *hdev);
int btintel_exit_mfg(struct hci_dev *hdev, bool reset, bool patched);
int btintel_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr);
int btintel_set_diag(struct hci_dev *hdev, bool enable);
int btintel_set_diag_mfg(struct hci_dev *hdev, bool enable);
void btintel_hw_error(struct hci_dev *hdev, u8 code);

void btintel_version_info(struct hci_dev *hdev, struct intel_version *ver);
int btintel_secure_send(struct hci_dev *hdev, u8 fragment_type, u32 plen,
			const void *param);
int btintel_load_ddc_config(struct hci_dev *hdev, const char *ddc_name);
int btintel_set_event_mask(struct hci_dev *hdev, bool debug);
int btintel_set_event_mask_mfg(struct hci_dev *hdev, bool debug);
int btintel_read_version(struct hci_dev *hdev, struct intel_version *ver);

struct regmap *btintel_regmap_init(struct hci_dev *hdev, u16 opcode_read,
				   u16 opcode_write);
int btintel_send_intel_reset(struct hci_dev *hdev, u32 boot_param);
int btintel_read_boot_params(struct hci_dev *hdev,
			     struct intel_boot_params *params);
int btintel_download_firmware(struct hci_dev *dev, const struct firmware *fw,
			      u32 *boot_param);
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

static inline int btintel_set_diag_mfg(struct hci_dev *hdev, bool enable)
{
	return -EOPNOTSUPP;
}

static inline void btintel_hw_error(struct hci_dev *hdev, u8 code)
{
}

static inline void btintel_version_info(struct hci_dev *hdev,
					struct intel_version *ver)
{
}

static inline int btintel_secure_send(struct hci_dev *hdev, u8 fragment_type,
				      u32 plen, const void *param)
{
	return -EOPNOTSUPP;
}

static inline int btintel_load_ddc_config(struct hci_dev *hdev,
					  const char *ddc_name)
{
	return -EOPNOTSUPP;
}

static inline int btintel_set_event_mask(struct hci_dev *hdev, bool debug)
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
#endif
