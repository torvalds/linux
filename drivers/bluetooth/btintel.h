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

#if IS_ENABLED(CONFIG_BT_INTEL)

int btintel_check_bdaddr(struct hci_dev *hdev);
int btintel_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr);

#else

static inline int btintel_check_bdaddr(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int btintel_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	return -EOPNOTSUPP;
}

#endif
