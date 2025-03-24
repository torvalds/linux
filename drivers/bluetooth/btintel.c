// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Bluetooth support for Intel devices
 *
 *  Copyright (C) 2015  Intel Corporation
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <linux/string_choices.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <linux/unaligned.h>
#include <linux/efi.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btintel.h"

#define VERSION "0.1"

#define BDADDR_INTEL		(&(bdaddr_t){{0x00, 0x8b, 0x9e, 0x19, 0x03, 0x00}})
#define RSA_HEADER_LEN		644
#define CSS_HEADER_OFFSET	8
#define ECDSA_OFFSET		644
#define ECDSA_HEADER_LEN	320

#define BTINTEL_EFI_DSBR	L"UefiCnvCommonDSBR"

enum {
	DSM_SET_WDISABLE2_DELAY = 1,
	DSM_SET_RESET_METHOD = 3,
};

#define CMD_WRITE_BOOT_PARAMS	0xfc0e
struct cmd_write_boot_params {
	__le32 boot_addr;
	u8  fw_build_num;
	u8  fw_build_ww;
	u8  fw_build_yy;
} __packed;

static struct {
	const char *driver_name;
	u8         hw_variant;
	u32        fw_build_num;
} coredump_info;

static const guid_t btintel_guid_dsm =
	GUID_INIT(0xaa10f4e0, 0x81ac, 0x4233,
		  0xab, 0xf6, 0x3b, 0x2a, 0xc5, 0x0e, 0x28, 0xd9);

int btintel_check_bdaddr(struct hci_dev *hdev)
{
	struct hci_rp_read_bd_addr *bda;
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_BD_ADDR, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		bt_dev_err(hdev, "Reading Intel device address failed (%d)",
			   err);
		return err;
	}

	if (skb->len != sizeof(*bda)) {
		bt_dev_err(hdev, "Intel device address length mismatch");
		kfree_skb(skb);
		return -EIO;
	}

	bda = (struct hci_rp_read_bd_addr *)skb->data;

	/* For some Intel based controllers, the default Bluetooth device
	 * address 00:03:19:9E:8B:00 can be found. These controllers are
	 * fully operational, but have the danger of duplicate addresses
	 * and that in turn can cause problems with Bluetooth operation.
	 */
	if (!bacmp(&bda->bdaddr, BDADDR_INTEL)) {
		bt_dev_err(hdev, "Found Intel default device address (%pMR)",
			   &bda->bdaddr);
		set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_check_bdaddr);

int btintel_enter_mfg(struct hci_dev *hdev)
{
	static const u8 param[] = { 0x01, 0x00 };
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, 0xfc11, 2, param, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Entering manufacturer mode failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_enter_mfg);

int btintel_exit_mfg(struct hci_dev *hdev, bool reset, bool patched)
{
	u8 param[] = { 0x00, 0x00 };
	struct sk_buff *skb;

	/* The 2nd command parameter specifies the manufacturing exit method:
	 * 0x00: Just disable the manufacturing mode (0x00).
	 * 0x01: Disable manufacturing mode and reset with patches deactivated.
	 * 0x02: Disable manufacturing mode and reset with patches activated.
	 */
	if (reset)
		param[1] |= patched ? 0x02 : 0x01;

	skb = __hci_cmd_sync(hdev, 0xfc11, 2, param, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Exiting manufacturer mode failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_exit_mfg);

int btintel_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync(hdev, 0xfc31, 6, bdaddr, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Changing Intel device address failed (%d)",
			   err);
		return err;
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_set_bdaddr);

static int btintel_set_event_mask(struct hci_dev *hdev, bool debug)
{
	u8 mask[8] = { 0x87, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	struct sk_buff *skb;
	int err;

	if (debug)
		mask[1] |= 0x62;

	skb = __hci_cmd_sync(hdev, 0xfc52, 8, mask, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Setting Intel event mask failed (%d)", err);
		return err;
	}
	kfree_skb(skb);

	return 0;
}

int btintel_set_diag(struct hci_dev *hdev, bool enable)
{
	struct sk_buff *skb;
	u8 param[3];
	int err;

	if (enable) {
		param[0] = 0x03;
		param[1] = 0x03;
		param[2] = 0x03;
	} else {
		param[0] = 0x00;
		param[1] = 0x00;
		param[2] = 0x00;
	}

	skb = __hci_cmd_sync(hdev, 0xfc43, 3, param, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		if (err == -ENODATA)
			goto done;
		bt_dev_err(hdev, "Changing Intel diagnostic mode failed (%d)",
			   err);
		return err;
	}
	kfree_skb(skb);

done:
	btintel_set_event_mask(hdev, enable);
	return 0;
}
EXPORT_SYMBOL_GPL(btintel_set_diag);

static int btintel_set_diag_mfg(struct hci_dev *hdev, bool enable)
{
	int err, ret;

	err = btintel_enter_mfg(hdev);
	if (err)
		return err;

	ret = btintel_set_diag(hdev, enable);

	err = btintel_exit_mfg(hdev, false, false);
	if (err)
		return err;

	return ret;
}

static int btintel_set_diag_combined(struct hci_dev *hdev, bool enable)
{
	int ret;

	/* Legacy ROM device needs to be in the manufacturer mode to apply
	 * diagnostic setting
	 *
	 * This flag is set after reading the Intel version.
	 */
	if (btintel_test_flag(hdev, INTEL_ROM_LEGACY))
		ret = btintel_set_diag_mfg(hdev, enable);
	else
		ret = btintel_set_diag(hdev, enable);

	return ret;
}

void btintel_hw_error(struct hci_dev *hdev, u8 code)
{
	struct sk_buff *skb;
	u8 type = 0x00;

	bt_dev_err(hdev, "Hardware error 0x%2.2x", code);

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reset after hardware error failed (%ld)",
			   PTR_ERR(skb));
		return;
	}
	kfree_skb(skb);

	skb = __hci_cmd_sync(hdev, 0xfc22, 1, &type, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Retrieving Intel exception info failed (%ld)",
			   PTR_ERR(skb));
		return;
	}

	if (skb->len != 13) {
		bt_dev_err(hdev, "Exception info size mismatch");
		kfree_skb(skb);
		return;
	}

	bt_dev_err(hdev, "Exception info %s", (char *)(skb->data + 1));

	kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(btintel_hw_error);

int btintel_version_info(struct hci_dev *hdev, struct intel_version *ver)
{
	const char *variant;

	/* The hardware platform number has a fixed value of 0x37 and
	 * for now only accept this single value.
	 */
	if (ver->hw_platform != 0x37) {
		bt_dev_err(hdev, "Unsupported Intel hardware platform (%u)",
			   ver->hw_platform);
		return -EINVAL;
	}

	/* Check for supported iBT hardware variants of this firmware
	 * loading method.
	 *
	 * This check has been put in place to ensure correct forward
	 * compatibility options when newer hardware variants come along.
	 */
	switch (ver->hw_variant) {
	case 0x07:	/* WP - Legacy ROM */
	case 0x08:	/* StP - Legacy ROM */
	case 0x0b:      /* SfP */
	case 0x0c:      /* WsP */
	case 0x11:      /* JfP */
	case 0x12:      /* ThP */
	case 0x13:      /* HrP */
	case 0x14:      /* CcP */
		break;
	default:
		bt_dev_err(hdev, "Unsupported Intel hardware variant (%u)",
			   ver->hw_variant);
		return -EINVAL;
	}

	switch (ver->fw_variant) {
	case 0x01:
		variant = "Legacy ROM 2.5";
		break;
	case 0x06:
		variant = "Bootloader";
		break;
	case 0x22:
		variant = "Legacy ROM 2.x";
		break;
	case 0x23:
		variant = "Firmware";
		break;
	default:
		bt_dev_err(hdev, "Unsupported firmware variant(%02x)", ver->fw_variant);
		return -EINVAL;
	}

	coredump_info.hw_variant = ver->hw_variant;
	coredump_info.fw_build_num = ver->fw_build_num;

	bt_dev_info(hdev, "%s revision %u.%u build %u week %u %u",
		    variant, ver->fw_revision >> 4, ver->fw_revision & 0x0f,
		    ver->fw_build_num, ver->fw_build_ww,
		    2000 + ver->fw_build_yy);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_version_info);

static int btintel_secure_send(struct hci_dev *hdev, u8 fragment_type, u32 plen,
			       const void *param)
{
	while (plen > 0) {
		struct sk_buff *skb;
		u8 cmd_param[253], fragment_len = (plen > 252) ? 252 : plen;

		cmd_param[0] = fragment_type;
		memcpy(cmd_param + 1, param, fragment_len);

		skb = __hci_cmd_sync(hdev, 0xfc09, fragment_len + 1,
				     cmd_param, HCI_INIT_TIMEOUT);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		kfree_skb(skb);

		plen -= fragment_len;
		param += fragment_len;
	}

	return 0;
}

int btintel_load_ddc_config(struct hci_dev *hdev, const char *ddc_name)
{
	const struct firmware *fw;
	struct sk_buff *skb;
	const u8 *fw_ptr;
	int err;

	err = request_firmware_direct(&fw, ddc_name, &hdev->dev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to load Intel DDC file %s (%d)",
			   ddc_name, err);
		return err;
	}

	bt_dev_info(hdev, "Found Intel DDC parameters: %s", ddc_name);

	fw_ptr = fw->data;

	/* DDC file contains one or more DDC structure which has
	 * Length (1 byte), DDC ID (2 bytes), and DDC value (Length - 2).
	 */
	while (fw->size > fw_ptr - fw->data) {
		u8 cmd_plen = fw_ptr[0] + sizeof(u8);

		skb = __hci_cmd_sync(hdev, 0xfc8b, cmd_plen, fw_ptr,
				     HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			bt_dev_err(hdev, "Failed to send Intel_Write_DDC (%ld)",
				   PTR_ERR(skb));
			release_firmware(fw);
			return PTR_ERR(skb);
		}

		fw_ptr += cmd_plen;
		kfree_skb(skb);
	}

	release_firmware(fw);

	bt_dev_info(hdev, "Applying Intel DDC parameters completed");

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_load_ddc_config);

int btintel_set_event_mask_mfg(struct hci_dev *hdev, bool debug)
{
	int err, ret;

	err = btintel_enter_mfg(hdev);
	if (err)
		return err;

	ret = btintel_set_event_mask(hdev, debug);

	err = btintel_exit_mfg(hdev, false, false);
	if (err)
		return err;

	return ret;
}
EXPORT_SYMBOL_GPL(btintel_set_event_mask_mfg);

int btintel_read_version(struct hci_dev *hdev, struct intel_version *ver)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, 0xfc05, 0, NULL, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading Intel version information failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (!skb || skb->len != sizeof(*ver)) {
		bt_dev_err(hdev, "Intel version event size mismatch");
		kfree_skb(skb);
		return -EILSEQ;
	}

	memcpy(ver, skb->data, sizeof(*ver));

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_read_version);

int btintel_version_info_tlv(struct hci_dev *hdev,
			     struct intel_version_tlv *version)
{
	const char *variant;

	/* The hardware platform number has a fixed value of 0x37 and
	 * for now only accept this single value.
	 */
	if (INTEL_HW_PLATFORM(version->cnvi_bt) != 0x37) {
		bt_dev_err(hdev, "Unsupported Intel hardware platform (0x%2x)",
			   INTEL_HW_PLATFORM(version->cnvi_bt));
		return -EINVAL;
	}

	/* Check for supported iBT hardware variants of this firmware
	 * loading method.
	 *
	 * This check has been put in place to ensure correct forward
	 * compatibility options when newer hardware variants come along.
	 */
	switch (INTEL_HW_VARIANT(version->cnvi_bt)) {
	case 0x17:	/* TyP */
	case 0x18:	/* Slr */
	case 0x19:	/* Slr-F */
	case 0x1b:      /* Mgr */
	case 0x1c:	/* Gale Peak (GaP) */
	case 0x1d:	/* BlazarU (BzrU) */
	case 0x1e:	/* BlazarI (Bzr) */
		break;
	default:
		bt_dev_err(hdev, "Unsupported Intel hardware variant (0x%x)",
			   INTEL_HW_VARIANT(version->cnvi_bt));
		return -EINVAL;
	}

	switch (version->img_type) {
	case BTINTEL_IMG_BOOTLOADER:
		variant = "Bootloader";
		/* It is required that every single firmware fragment is acknowledged
		 * with a command complete event. If the boot parameters indicate
		 * that this bootloader does not send them, then abort the setup.
		 */
		if (version->limited_cce != 0x00) {
			bt_dev_err(hdev, "Unsupported Intel firmware loading method (0x%x)",
				   version->limited_cce);
			return -EINVAL;
		}

		/* Secure boot engine type should be either 1 (ECDSA) or 0 (RSA) */
		if (version->sbe_type > 0x01) {
			bt_dev_err(hdev, "Unsupported Intel secure boot engine type (0x%x)",
				   version->sbe_type);
			return -EINVAL;
		}

		bt_dev_info(hdev, "Device revision is %u", version->dev_rev_id);
		bt_dev_info(hdev, "Secure boot is %s",
			    str_enabled_disabled(version->secure_boot));
		bt_dev_info(hdev, "OTP lock is %s",
			    str_enabled_disabled(version->otp_lock));
		bt_dev_info(hdev, "API lock is %s",
			    str_enabled_disabled(version->api_lock));
		bt_dev_info(hdev, "Debug lock is %s",
			    str_enabled_disabled(version->debug_lock));
		bt_dev_info(hdev, "Minimum firmware build %u week %u %u",
			    version->min_fw_build_nn, version->min_fw_build_cw,
			    2000 + version->min_fw_build_yy);
		break;
	case BTINTEL_IMG_IML:
		variant = "Intermediate loader";
		break;
	case BTINTEL_IMG_OP:
		variant = "Firmware";
		break;
	default:
		bt_dev_err(hdev, "Unsupported image type(%02x)", version->img_type);
		return -EINVAL;
	}

	coredump_info.hw_variant = INTEL_HW_VARIANT(version->cnvi_bt);
	coredump_info.fw_build_num = version->build_num;

	bt_dev_info(hdev, "%s timestamp %u.%u buildtype %u build %u", variant,
		    2000 + (version->timestamp >> 8), version->timestamp & 0xff,
		    version->build_type, version->build_num);
	if (version->img_type == BTINTEL_IMG_OP)
		bt_dev_info(hdev, "Firmware SHA1: 0x%8.8x", version->git_sha1);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_version_info_tlv);

int btintel_parse_version_tlv(struct hci_dev *hdev,
			      struct intel_version_tlv *version,
			      struct sk_buff *skb)
{
	/* Consume Command Complete Status field */
	skb_pull(skb, 1);

	/* Event parameters contatin multiple TLVs. Read each of them
	 * and only keep the required data. Also, it use existing legacy
	 * version field like hw_platform, hw_variant, and fw_variant
	 * to keep the existing setup flow
	 */
	while (skb->len) {
		struct intel_tlv *tlv;

		/* Make sure skb has a minimum length of the header */
		if (skb->len < sizeof(*tlv))
			return -EINVAL;

		tlv = (struct intel_tlv *)skb->data;

		/* Make sure skb has a enough data */
		if (skb->len < tlv->len + sizeof(*tlv))
			return -EINVAL;

		switch (tlv->type) {
		case INTEL_TLV_CNVI_TOP:
			version->cnvi_top = get_unaligned_le32(tlv->val);
			break;
		case INTEL_TLV_CNVR_TOP:
			version->cnvr_top = get_unaligned_le32(tlv->val);
			break;
		case INTEL_TLV_CNVI_BT:
			version->cnvi_bt = get_unaligned_le32(tlv->val);
			break;
		case INTEL_TLV_CNVR_BT:
			version->cnvr_bt = get_unaligned_le32(tlv->val);
			break;
		case INTEL_TLV_DEV_REV_ID:
			version->dev_rev_id = get_unaligned_le16(tlv->val);
			break;
		case INTEL_TLV_IMAGE_TYPE:
			version->img_type = tlv->val[0];
			break;
		case INTEL_TLV_TIME_STAMP:
			/* If image type is Operational firmware (0x03), then
			 * running FW Calendar Week and Year information can
			 * be extracted from Timestamp information
			 */
			version->min_fw_build_cw = tlv->val[0];
			version->min_fw_build_yy = tlv->val[1];
			version->timestamp = get_unaligned_le16(tlv->val);
			break;
		case INTEL_TLV_BUILD_TYPE:
			version->build_type = tlv->val[0];
			break;
		case INTEL_TLV_BUILD_NUM:
			/* If image type is Operational firmware (0x03), then
			 * running FW build number can be extracted from the
			 * Build information
			 */
			version->min_fw_build_nn = tlv->val[0];
			version->build_num = get_unaligned_le32(tlv->val);
			break;
		case INTEL_TLV_SECURE_BOOT:
			version->secure_boot = tlv->val[0];
			break;
		case INTEL_TLV_OTP_LOCK:
			version->otp_lock = tlv->val[0];
			break;
		case INTEL_TLV_API_LOCK:
			version->api_lock = tlv->val[0];
			break;
		case INTEL_TLV_DEBUG_LOCK:
			version->debug_lock = tlv->val[0];
			break;
		case INTEL_TLV_MIN_FW:
			version->min_fw_build_nn = tlv->val[0];
			version->min_fw_build_cw = tlv->val[1];
			version->min_fw_build_yy = tlv->val[2];
			break;
		case INTEL_TLV_LIMITED_CCE:
			version->limited_cce = tlv->val[0];
			break;
		case INTEL_TLV_SBE_TYPE:
			version->sbe_type = tlv->val[0];
			break;
		case INTEL_TLV_OTP_BDADDR:
			memcpy(&version->otp_bd_addr, tlv->val,
							sizeof(bdaddr_t));
			break;
		case INTEL_TLV_GIT_SHA1:
			version->git_sha1 = get_unaligned_le32(tlv->val);
			break;
		case INTEL_TLV_FW_ID:
			snprintf(version->fw_id, sizeof(version->fw_id),
				 "%s", tlv->val);
			break;
		default:
			/* Ignore rest of information */
			break;
		}
		/* consume the current tlv and move to next*/
		skb_pull(skb, tlv->len + sizeof(*tlv));
	}

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_parse_version_tlv);

static int btintel_read_version_tlv(struct hci_dev *hdev,
				    struct intel_version_tlv *version)
{
	struct sk_buff *skb;
	const u8 param[1] = { 0xFF };

	if (!version)
		return -EINVAL;

	skb = __hci_cmd_sync(hdev, 0xfc05, 1, param, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading Intel version information failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->data[0]) {
		bt_dev_err(hdev, "Intel Read Version command failed (%02x)",
			   skb->data[0]);
		kfree_skb(skb);
		return -EIO;
	}

	btintel_parse_version_tlv(hdev, version, skb);

	kfree_skb(skb);
	return 0;
}

/* ------- REGMAP IBT SUPPORT ------- */

#define IBT_REG_MODE_8BIT  0x00
#define IBT_REG_MODE_16BIT 0x01
#define IBT_REG_MODE_32BIT 0x02

struct regmap_ibt_context {
	struct hci_dev *hdev;
	__u16 op_write;
	__u16 op_read;
};

struct ibt_cp_reg_access {
	__le32  addr;
	__u8    mode;
	__u8    len;
	__u8    data[];
} __packed;

struct ibt_rp_reg_access {
	__u8    status;
	__le32  addr;
	__u8    data[];
} __packed;

static int regmap_ibt_read(void *context, const void *addr, size_t reg_size,
			   void *val, size_t val_size)
{
	struct regmap_ibt_context *ctx = context;
	struct ibt_cp_reg_access cp;
	struct ibt_rp_reg_access *rp;
	struct sk_buff *skb;
	int err = 0;

	if (reg_size != sizeof(__le32))
		return -EINVAL;

	switch (val_size) {
	case 1:
		cp.mode = IBT_REG_MODE_8BIT;
		break;
	case 2:
		cp.mode = IBT_REG_MODE_16BIT;
		break;
	case 4:
		cp.mode = IBT_REG_MODE_32BIT;
		break;
	default:
		return -EINVAL;
	}

	/* regmap provides a little-endian formatted addr */
	cp.addr = *(__le32 *)addr;
	cp.len = val_size;

	bt_dev_dbg(ctx->hdev, "Register (0x%x) read", le32_to_cpu(cp.addr));

	skb = hci_cmd_sync(ctx->hdev, ctx->op_read, sizeof(cp), &cp,
			   HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(ctx->hdev, "regmap: Register (0x%x) read error (%d)",
			   le32_to_cpu(cp.addr), err);
		return err;
	}

	if (skb->len != sizeof(*rp) + val_size) {
		bt_dev_err(ctx->hdev, "regmap: Register (0x%x) read error, bad len",
			   le32_to_cpu(cp.addr));
		err = -EINVAL;
		goto done;
	}

	rp = (struct ibt_rp_reg_access *)skb->data;

	if (rp->addr != cp.addr) {
		bt_dev_err(ctx->hdev, "regmap: Register (0x%x) read error, bad addr",
			   le32_to_cpu(rp->addr));
		err = -EINVAL;
		goto done;
	}

	memcpy(val, rp->data, val_size);

done:
	kfree_skb(skb);
	return err;
}

static int regmap_ibt_gather_write(void *context,
				   const void *addr, size_t reg_size,
				   const void *val, size_t val_size)
{
	struct regmap_ibt_context *ctx = context;
	struct ibt_cp_reg_access *cp;
	struct sk_buff *skb;
	int plen = sizeof(*cp) + val_size;
	u8 mode;
	int err = 0;

	if (reg_size != sizeof(__le32))
		return -EINVAL;

	switch (val_size) {
	case 1:
		mode = IBT_REG_MODE_8BIT;
		break;
	case 2:
		mode = IBT_REG_MODE_16BIT;
		break;
	case 4:
		mode = IBT_REG_MODE_32BIT;
		break;
	default:
		return -EINVAL;
	}

	cp = kmalloc(plen, GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	/* regmap provides a little-endian formatted addr/value */
	cp->addr = *(__le32 *)addr;
	cp->mode = mode;
	cp->len = val_size;
	memcpy(&cp->data, val, val_size);

	bt_dev_dbg(ctx->hdev, "Register (0x%x) write", le32_to_cpu(cp->addr));

	skb = hci_cmd_sync(ctx->hdev, ctx->op_write, plen, cp, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(ctx->hdev, "regmap: Register (0x%x) write error (%d)",
			   le32_to_cpu(cp->addr), err);
		goto done;
	}
	kfree_skb(skb);

done:
	kfree(cp);
	return err;
}

static int regmap_ibt_write(void *context, const void *data, size_t count)
{
	/* data contains register+value, since we only support 32bit addr,
	 * minimum data size is 4 bytes.
	 */
	if (WARN_ONCE(count < 4, "Invalid register access"))
		return -EINVAL;

	return regmap_ibt_gather_write(context, data, 4, data + 4, count - 4);
}

static void regmap_ibt_free_context(void *context)
{
	kfree(context);
}

static const struct regmap_bus regmap_ibt = {
	.read = regmap_ibt_read,
	.write = regmap_ibt_write,
	.gather_write = regmap_ibt_gather_write,
	.free_context = regmap_ibt_free_context,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

/* Config is the same for all register regions */
static const struct regmap_config regmap_ibt_cfg = {
	.name      = "btintel_regmap",
	.reg_bits  = 32,
	.val_bits  = 32,
};

struct regmap *btintel_regmap_init(struct hci_dev *hdev, u16 opcode_read,
				   u16 opcode_write)
{
	struct regmap_ibt_context *ctx;

	bt_dev_info(hdev, "regmap: Init R%x-W%x region", opcode_read,
		    opcode_write);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->op_read = opcode_read;
	ctx->op_write = opcode_write;
	ctx->hdev = hdev;

	return regmap_init(&hdev->dev, &regmap_ibt, ctx, &regmap_ibt_cfg);
}
EXPORT_SYMBOL_GPL(btintel_regmap_init);

int btintel_send_intel_reset(struct hci_dev *hdev, u32 boot_param)
{
	struct intel_reset params = { 0x00, 0x01, 0x00, 0x01, 0x00000000 };
	struct sk_buff *skb;

	params.boot_param = cpu_to_le32(boot_param);

	skb = __hci_cmd_sync(hdev, 0xfc01, sizeof(params), &params,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Failed to send Intel Reset command");
		return PTR_ERR(skb);
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_send_intel_reset);

int btintel_read_boot_params(struct hci_dev *hdev,
			     struct intel_boot_params *params)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, 0xfc0d, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading Intel boot parameters failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*params)) {
		bt_dev_err(hdev, "Intel boot parameters size mismatch");
		kfree_skb(skb);
		return -EILSEQ;
	}

	memcpy(params, skb->data, sizeof(*params));

	kfree_skb(skb);

	if (params->status) {
		bt_dev_err(hdev, "Intel boot parameters command failed (%02x)",
			   params->status);
		return -bt_to_errno(params->status);
	}

	bt_dev_info(hdev, "Device revision is %u",
		    le16_to_cpu(params->dev_revid));

	bt_dev_info(hdev, "Secure boot is %s",
		    str_enabled_disabled(params->secure_boot));

	bt_dev_info(hdev, "OTP lock is %s",
		    str_enabled_disabled(params->otp_lock));

	bt_dev_info(hdev, "API lock is %s",
		    str_enabled_disabled(params->api_lock));

	bt_dev_info(hdev, "Debug lock is %s",
		    str_enabled_disabled(params->debug_lock));

	bt_dev_info(hdev, "Minimum firmware build %u week %u %u",
		    params->min_fw_build_nn, params->min_fw_build_cw,
		    2000 + params->min_fw_build_yy);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_read_boot_params);

static int btintel_sfi_rsa_header_secure_send(struct hci_dev *hdev,
					      const struct firmware *fw)
{
	int err;

	/* Start the firmware download transaction with the Init fragment
	 * represented by the 128 bytes of CSS header.
	 */
	err = btintel_secure_send(hdev, 0x00, 128, fw->data);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware header (%d)", err);
		goto done;
	}

	/* Send the 256 bytes of public key information from the firmware
	 * as the PKey fragment.
	 */
	err = btintel_secure_send(hdev, 0x03, 256, fw->data + 128);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware pkey (%d)", err);
		goto done;
	}

	/* Send the 256 bytes of signature information from the firmware
	 * as the Sign fragment.
	 */
	err = btintel_secure_send(hdev, 0x02, 256, fw->data + 388);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware signature (%d)", err);
		goto done;
	}

done:
	return err;
}

static int btintel_sfi_ecdsa_header_secure_send(struct hci_dev *hdev,
						const struct firmware *fw)
{
	int err;

	/* Start the firmware download transaction with the Init fragment
	 * represented by the 128 bytes of CSS header.
	 */
	err = btintel_secure_send(hdev, 0x00, 128, fw->data + 644);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware header (%d)", err);
		return err;
	}

	/* Send the 96 bytes of public key information from the firmware
	 * as the PKey fragment.
	 */
	err = btintel_secure_send(hdev, 0x03, 96, fw->data + 644 + 128);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware pkey (%d)", err);
		return err;
	}

	/* Send the 96 bytes of signature information from the firmware
	 * as the Sign fragment
	 */
	err = btintel_secure_send(hdev, 0x02, 96, fw->data + 644 + 224);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware signature (%d)",
			   err);
		return err;
	}
	return 0;
}

static int btintel_download_firmware_payload(struct hci_dev *hdev,
					     const struct firmware *fw,
					     size_t offset)
{
	int err;
	const u8 *fw_ptr;
	u32 frag_len;

	fw_ptr = fw->data + offset;
	frag_len = 0;
	err = -EINVAL;

	while (fw_ptr - fw->data < fw->size) {
		struct hci_command_hdr *cmd = (void *)(fw_ptr + frag_len);

		frag_len += sizeof(*cmd) + cmd->plen;

		/* The parameter length of the secure send command requires
		 * a 4 byte alignment. It happens so that the firmware file
		 * contains proper Intel_NOP commands to align the fragments
		 * as needed.
		 *
		 * Send set of commands with 4 byte alignment from the
		 * firmware data buffer as a single Data fragment.
		 */
		if (!(frag_len % 4)) {
			err = btintel_secure_send(hdev, 0x01, frag_len, fw_ptr);
			if (err < 0) {
				bt_dev_err(hdev,
					   "Failed to send firmware data (%d)",
					   err);
				goto done;
			}

			fw_ptr += frag_len;
			frag_len = 0;
		}
	}

done:
	return err;
}

static bool btintel_firmware_version(struct hci_dev *hdev,
				     u8 num, u8 ww, u8 yy,
				     const struct firmware *fw,
				     u32 *boot_addr)
{
	const u8 *fw_ptr;

	fw_ptr = fw->data;

	while (fw_ptr - fw->data < fw->size) {
		struct hci_command_hdr *cmd = (void *)(fw_ptr);

		/* Each SKU has a different reset parameter to use in the
		 * HCI_Intel_Reset command and it is embedded in the firmware
		 * data. So, instead of using static value per SKU, check
		 * the firmware data and save it for later use.
		 */
		if (le16_to_cpu(cmd->opcode) == CMD_WRITE_BOOT_PARAMS) {
			struct cmd_write_boot_params *params;

			params = (void *)(fw_ptr + sizeof(*cmd));

			*boot_addr = le32_to_cpu(params->boot_addr);

			bt_dev_info(hdev, "Boot Address: 0x%x", *boot_addr);

			bt_dev_info(hdev, "Firmware Version: %u-%u.%u",
				    params->fw_build_num, params->fw_build_ww,
				    params->fw_build_yy);

			return (num == params->fw_build_num &&
				ww == params->fw_build_ww &&
				yy == params->fw_build_yy);
		}

		fw_ptr += sizeof(*cmd) + cmd->plen;
	}

	return false;
}

int btintel_download_firmware(struct hci_dev *hdev,
			      struct intel_version *ver,
			      const struct firmware *fw,
			      u32 *boot_param)
{
	int err;

	/* SfP and WsP don't seem to update the firmware version on file
	 * so version checking is currently not possible.
	 */
	switch (ver->hw_variant) {
	case 0x0b:	/* SfP */
	case 0x0c:	/* WsP */
		/* Skip version checking */
		break;
	default:

		/* Skip download if firmware has the same version */
		if (btintel_firmware_version(hdev, ver->fw_build_num,
					     ver->fw_build_ww, ver->fw_build_yy,
					     fw, boot_param)) {
			bt_dev_info(hdev, "Firmware already loaded");
			/* Return -EALREADY to indicate that the firmware has
			 * already been loaded.
			 */
			return -EALREADY;
		}
	}

	/* The firmware variant determines if the device is in bootloader
	 * mode or is running operational firmware. The value 0x06 identifies
	 * the bootloader and the value 0x23 identifies the operational
	 * firmware.
	 *
	 * If the firmware version has changed that means it needs to be reset
	 * to bootloader when operational so the new firmware can be loaded.
	 */
	if (ver->fw_variant == 0x23)
		return -EINVAL;

	err = btintel_sfi_rsa_header_secure_send(hdev, fw);
	if (err)
		return err;

	return btintel_download_firmware_payload(hdev, fw, RSA_HEADER_LEN);
}
EXPORT_SYMBOL_GPL(btintel_download_firmware);

static int btintel_download_fw_tlv(struct hci_dev *hdev,
				   struct intel_version_tlv *ver,
				   const struct firmware *fw, u32 *boot_param,
				   u8 hw_variant, u8 sbe_type)
{
	int err;
	u32 css_header_ver;

	/* Skip download if firmware has the same version */
	if (btintel_firmware_version(hdev, ver->min_fw_build_nn,
				     ver->min_fw_build_cw,
				     ver->min_fw_build_yy,
				     fw, boot_param)) {
		bt_dev_info(hdev, "Firmware already loaded");
		/* Return -EALREADY to indicate that firmware has
		 * already been loaded.
		 */
		return -EALREADY;
	}

	/* The firmware variant determines if the device is in bootloader
	 * mode or is running operational firmware. The value 0x01 identifies
	 * the bootloader and the value 0x03 identifies the operational
	 * firmware.
	 *
	 * If the firmware version has changed that means it needs to be reset
	 * to bootloader when operational so the new firmware can be loaded.
	 */
	if (ver->img_type == BTINTEL_IMG_OP)
		return -EINVAL;

	/* iBT hardware variants 0x0b, 0x0c, 0x11, 0x12, 0x13, 0x14 support
	 * only RSA secure boot engine. Hence, the corresponding sfi file will
	 * have RSA header of 644 bytes followed by Command Buffer.
	 *
	 * iBT hardware variants 0x17, 0x18 onwards support both RSA and ECDSA
	 * secure boot engine. As a result, the corresponding sfi file will
	 * have RSA header of 644, ECDSA header of 320 bytes followed by
	 * Command Buffer.
	 *
	 * CSS Header byte positions 0x08 to 0x0B represent the CSS Header
	 * version: RSA(0x00010000) , ECDSA (0x00020000)
	 */
	css_header_ver = get_unaligned_le32(fw->data + CSS_HEADER_OFFSET);
	if (css_header_ver != 0x00010000) {
		bt_dev_err(hdev, "Invalid CSS Header version");
		return -EINVAL;
	}

	if (hw_variant <= 0x14) {
		if (sbe_type != 0x00) {
			bt_dev_err(hdev, "Invalid SBE type for hardware variant (%d)",
				   hw_variant);
			return -EINVAL;
		}

		err = btintel_sfi_rsa_header_secure_send(hdev, fw);
		if (err)
			return err;

		err = btintel_download_firmware_payload(hdev, fw, RSA_HEADER_LEN);
		if (err)
			return err;
	} else if (hw_variant >= 0x17) {
		/* Check if CSS header for ECDSA follows the RSA header */
		if (fw->data[ECDSA_OFFSET] != 0x06)
			return -EINVAL;

		/* Check if the CSS Header version is ECDSA(0x00020000) */
		css_header_ver = get_unaligned_le32(fw->data + ECDSA_OFFSET + CSS_HEADER_OFFSET);
		if (css_header_ver != 0x00020000) {
			bt_dev_err(hdev, "Invalid CSS Header version");
			return -EINVAL;
		}

		if (sbe_type == 0x00) {
			err = btintel_sfi_rsa_header_secure_send(hdev, fw);
			if (err)
				return err;

			err = btintel_download_firmware_payload(hdev, fw,
								RSA_HEADER_LEN + ECDSA_HEADER_LEN);
			if (err)
				return err;
		} else if (sbe_type == 0x01) {
			err = btintel_sfi_ecdsa_header_secure_send(hdev, fw);
			if (err)
				return err;

			err = btintel_download_firmware_payload(hdev, fw,
								RSA_HEADER_LEN + ECDSA_HEADER_LEN);
			if (err)
				return err;
		}
	}
	return 0;
}

static void btintel_reset_to_bootloader(struct hci_dev *hdev)
{
	struct intel_reset params;
	struct sk_buff *skb;

	/* PCIe transport uses shared hardware reset mechanism for recovery
	 * which gets triggered in pcie *setup* function on error.
	 */
	if (hdev->bus == HCI_PCI)
		return;

	/* Send Intel Reset command. This will result in
	 * re-enumeration of BT controller.
	 *
	 * Intel Reset parameter description:
	 * reset_type :   0x00 (Soft reset),
	 *		  0x01 (Hard reset)
	 * patch_enable : 0x00 (Do not enable),
	 *		  0x01 (Enable)
	 * ddc_reload :   0x00 (Do not reload),
	 *		  0x01 (Reload)
	 * boot_option:   0x00 (Current image),
	 *                0x01 (Specified boot address)
	 * boot_param:    Boot address
	 *
	 */

	params.reset_type = 0x01;
	params.patch_enable = 0x01;
	params.ddc_reload = 0x01;
	params.boot_option = 0x00;
	params.boot_param = cpu_to_le32(0x00000000);

	skb = __hci_cmd_sync(hdev, 0xfc01, sizeof(params),
			     &params, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "FW download error recovery failed (%ld)",
			   PTR_ERR(skb));
		return;
	}
	bt_dev_info(hdev, "Intel reset sent to retry FW download");
	kfree_skb(skb);

	/* Current Intel BT controllers(ThP/JfP) hold the USB reset
	 * lines for 2ms when it receives Intel Reset in bootloader mode.
	 * Whereas, the upcoming Intel BT controllers will hold USB reset
	 * for 150ms. To keep the delay generic, 150ms is chosen here.
	 */
	msleep(150);
}

static int btintel_read_debug_features(struct hci_dev *hdev,
				       struct intel_debug_features *features)
{
	struct sk_buff *skb;
	u8 page_no = 1;

	/* Intel controller supports two pages, each page is of 128-bit
	 * feature bit mask. And each bit defines specific feature support
	 */
	skb = __hci_cmd_sync(hdev, 0xfca6, sizeof(page_no), &page_no,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading supported features failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != (sizeof(features->page1) + 3)) {
		bt_dev_err(hdev, "Supported features event size mismatch");
		kfree_skb(skb);
		return -EILSEQ;
	}

	memcpy(features->page1, skb->data + 3, sizeof(features->page1));

	/* Read the supported features page2 if required in future.
	 */
	kfree_skb(skb);
	return 0;
}

static int btintel_set_debug_features(struct hci_dev *hdev,
			       const struct intel_debug_features *features)
{
	u8 mask[11] = { 0x0a, 0x92, 0x02, 0x7f, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00 };
	u8 period[5] = { 0x04, 0x91, 0x02, 0x05, 0x00 };
	u8 trace_enable = 0x02;
	struct sk_buff *skb;

	if (!features) {
		bt_dev_warn(hdev, "Debug features not read");
		return -EINVAL;
	}

	if (!(features->page1[0] & 0x3f)) {
		bt_dev_info(hdev, "Telemetry exception format not supported");
		return 0;
	}

	skb = __hci_cmd_sync(hdev, 0xfc8b, 11, mask, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting Intel telemetry ddc write event mask failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	skb = __hci_cmd_sync(hdev, 0xfc8b, 5, period, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting periodicity for link statistics traces failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	skb = __hci_cmd_sync(hdev, 0xfca1, 1, &trace_enable, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Enable tracing of link statistics events failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	bt_dev_info(hdev, "set debug features: trace_enable 0x%02x mask 0x%02x",
		    trace_enable, mask[3]);

	return 0;
}

static int btintel_reset_debug_features(struct hci_dev *hdev,
				 const struct intel_debug_features *features)
{
	u8 mask[11] = { 0x0a, 0x92, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00 };
	u8 trace_enable = 0x00;
	struct sk_buff *skb;

	if (!features) {
		bt_dev_warn(hdev, "Debug features not read");
		return -EINVAL;
	}

	if (!(features->page1[0] & 0x3f)) {
		bt_dev_info(hdev, "Telemetry exception format not supported");
		return 0;
	}

	/* Should stop the trace before writing ddc event mask. */
	skb = __hci_cmd_sync(hdev, 0xfca1, 1, &trace_enable, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Stop tracing of link statistics events failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	skb = __hci_cmd_sync(hdev, 0xfc8b, 11, mask, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting Intel telemetry ddc write event mask failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	bt_dev_info(hdev, "reset debug features: trace_enable 0x%02x mask 0x%02x",
		    trace_enable, mask[3]);

	return 0;
}

int btintel_set_quality_report(struct hci_dev *hdev, bool enable)
{
	struct intel_debug_features features;
	int err;

	bt_dev_dbg(hdev, "enable %d", enable);

	/* Read the Intel supported features and if new exception formats
	 * supported, need to load the additional DDC config to enable.
	 */
	err = btintel_read_debug_features(hdev, &features);
	if (err)
		return err;

	/* Set or reset the debug features. */
	if (enable)
		err = btintel_set_debug_features(hdev, &features);
	else
		err = btintel_reset_debug_features(hdev, &features);

	return err;
}
EXPORT_SYMBOL_GPL(btintel_set_quality_report);

static void btintel_coredump(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, 0xfc4e, 0, NULL, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Coredump failed (%ld)", PTR_ERR(skb));
		return;
	}

	kfree_skb(skb);
}

static void btintel_dmp_hdr(struct hci_dev *hdev, struct sk_buff *skb)
{
	char buf[80];

	snprintf(buf, sizeof(buf), "Controller Name: 0x%X\n",
		 coredump_info.hw_variant);
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Firmware Version: 0x%X\n",
		 coredump_info.fw_build_num);
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Driver: %s\n", coredump_info.driver_name);
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Vendor: Intel\n");
	skb_put_data(skb, buf, strlen(buf));
}

static int btintel_register_devcoredump_support(struct hci_dev *hdev)
{
	struct intel_debug_features features;
	int err;

	err = btintel_read_debug_features(hdev, &features);
	if (err) {
		bt_dev_info(hdev, "Error reading debug features");
		return err;
	}

	if (!(features.page1[0] & 0x3f)) {
		bt_dev_dbg(hdev, "Telemetry exception format not supported");
		return -EOPNOTSUPP;
	}

	hci_devcd_register(hdev, btintel_coredump, btintel_dmp_hdr, NULL);

	return err;
}

static const struct firmware *btintel_legacy_rom_get_fw(struct hci_dev *hdev,
					       struct intel_version *ver)
{
	const struct firmware *fw;
	char fwname[64];
	int ret;

	snprintf(fwname, sizeof(fwname),
		 "intel/ibt-hw-%x.%x.%x-fw-%x.%x.%x.%x.%x.bseq",
		 ver->hw_platform, ver->hw_variant, ver->hw_revision,
		 ver->fw_variant,  ver->fw_revision, ver->fw_build_num,
		 ver->fw_build_ww, ver->fw_build_yy);

	ret = request_firmware(&fw, fwname, &hdev->dev);
	if (ret < 0) {
		if (ret == -EINVAL) {
			bt_dev_err(hdev, "Intel firmware file request failed (%d)",
				   ret);
			return NULL;
		}

		bt_dev_err(hdev, "failed to open Intel firmware file: %s (%d)",
			   fwname, ret);

		/* If the correct firmware patch file is not found, use the
		 * default firmware patch file instead
		 */
		snprintf(fwname, sizeof(fwname), "intel/ibt-hw-%x.%x.bseq",
			 ver->hw_platform, ver->hw_variant);
		if (request_firmware(&fw, fwname, &hdev->dev) < 0) {
			bt_dev_err(hdev, "failed to open default fw file: %s",
				   fwname);
			return NULL;
		}
	}

	bt_dev_info(hdev, "Intel Bluetooth firmware file: %s", fwname);

	return fw;
}

static int btintel_legacy_rom_patching(struct hci_dev *hdev,
				      const struct firmware *fw,
				      const u8 **fw_ptr, int *disable_patch)
{
	struct sk_buff *skb;
	struct hci_command_hdr *cmd;
	const u8 *cmd_param;
	struct hci_event_hdr *evt = NULL;
	const u8 *evt_param = NULL;
	int remain = fw->size - (*fw_ptr - fw->data);

	/* The first byte indicates the types of the patch command or event.
	 * 0x01 means HCI command and 0x02 is HCI event. If the first bytes
	 * in the current firmware buffer doesn't start with 0x01 or
	 * the size of remain buffer is smaller than HCI command header,
	 * the firmware file is corrupted and it should stop the patching
	 * process.
	 */
	if (remain > HCI_COMMAND_HDR_SIZE && *fw_ptr[0] != 0x01) {
		bt_dev_err(hdev, "Intel fw corrupted: invalid cmd read");
		return -EINVAL;
	}
	(*fw_ptr)++;
	remain--;

	cmd = (struct hci_command_hdr *)(*fw_ptr);
	*fw_ptr += sizeof(*cmd);
	remain -= sizeof(*cmd);

	/* Ensure that the remain firmware data is long enough than the length
	 * of command parameter. If not, the firmware file is corrupted.
	 */
	if (remain < cmd->plen) {
		bt_dev_err(hdev, "Intel fw corrupted: invalid cmd len");
		return -EFAULT;
	}

	/* If there is a command that loads a patch in the firmware
	 * file, then enable the patch upon success, otherwise just
	 * disable the manufacturer mode, for example patch activation
	 * is not required when the default firmware patch file is used
	 * because there are no patch data to load.
	 */
	if (*disable_patch && le16_to_cpu(cmd->opcode) == 0xfc8e)
		*disable_patch = 0;

	cmd_param = *fw_ptr;
	*fw_ptr += cmd->plen;
	remain -= cmd->plen;

	/* This reads the expected events when the above command is sent to the
	 * device. Some vendor commands expects more than one events, for
	 * example command status event followed by vendor specific event.
	 * For this case, it only keeps the last expected event. so the command
	 * can be sent with __hci_cmd_sync_ev() which returns the sk_buff of
	 * last expected event.
	 */
	while (remain > HCI_EVENT_HDR_SIZE && *fw_ptr[0] == 0x02) {
		(*fw_ptr)++;
		remain--;

		evt = (struct hci_event_hdr *)(*fw_ptr);
		*fw_ptr += sizeof(*evt);
		remain -= sizeof(*evt);

		if (remain < evt->plen) {
			bt_dev_err(hdev, "Intel fw corrupted: invalid evt len");
			return -EFAULT;
		}

		evt_param = *fw_ptr;
		*fw_ptr += evt->plen;
		remain -= evt->plen;
	}

	/* Every HCI commands in the firmware file has its correspond event.
	 * If event is not found or remain is smaller than zero, the firmware
	 * file is corrupted.
	 */
	if (!evt || !evt_param || remain < 0) {
		bt_dev_err(hdev, "Intel fw corrupted: invalid evt read");
		return -EFAULT;
	}

	skb = __hci_cmd_sync_ev(hdev, le16_to_cpu(cmd->opcode), cmd->plen,
				cmd_param, evt->evt, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "sending Intel patch command (0x%4.4x) failed (%ld)",
			   cmd->opcode, PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	/* It ensures that the returned event matches the event data read from
	 * the firmware file. At fist, it checks the length and then
	 * the contents of the event.
	 */
	if (skb->len != evt->plen) {
		bt_dev_err(hdev, "mismatch event length (opcode 0x%4.4x)",
			   le16_to_cpu(cmd->opcode));
		kfree_skb(skb);
		return -EFAULT;
	}

	if (memcmp(skb->data, evt_param, evt->plen)) {
		bt_dev_err(hdev, "mismatch event parameter (opcode 0x%4.4x)",
			   le16_to_cpu(cmd->opcode));
		kfree_skb(skb);
		return -EFAULT;
	}
	kfree_skb(skb);

	return 0;
}

static int btintel_legacy_rom_setup(struct hci_dev *hdev,
				    struct intel_version *ver)
{
	const struct firmware *fw;
	const u8 *fw_ptr;
	int disable_patch, err;
	struct intel_version new_ver;

	BT_DBG("%s", hdev->name);

	/* fw_patch_num indicates the version of patch the device currently
	 * have. If there is no patch data in the device, it is always 0x00.
	 * So, if it is other than 0x00, no need to patch the device again.
	 */
	if (ver->fw_patch_num) {
		bt_dev_info(hdev,
			    "Intel device is already patched. patch num: %02x",
			    ver->fw_patch_num);
		goto complete;
	}

	/* Opens the firmware patch file based on the firmware version read
	 * from the controller. If it fails to open the matching firmware
	 * patch file, it tries to open the default firmware patch file.
	 * If no patch file is found, allow the device to operate without
	 * a patch.
	 */
	fw = btintel_legacy_rom_get_fw(hdev, ver);
	if (!fw)
		goto complete;
	fw_ptr = fw->data;

	/* Enable the manufacturer mode of the controller.
	 * Only while this mode is enabled, the driver can download the
	 * firmware patch data and configuration parameters.
	 */
	err = btintel_enter_mfg(hdev);
	if (err) {
		release_firmware(fw);
		return err;
	}

	disable_patch = 1;

	/* The firmware data file consists of list of Intel specific HCI
	 * commands and its expected events. The first byte indicates the
	 * type of the message, either HCI command or HCI event.
	 *
	 * It reads the command and its expected event from the firmware file,
	 * and send to the controller. Once __hci_cmd_sync_ev() returns,
	 * the returned event is compared with the event read from the firmware
	 * file and it will continue until all the messages are downloaded to
	 * the controller.
	 *
	 * Once the firmware patching is completed successfully,
	 * the manufacturer mode is disabled with reset and activating the
	 * downloaded patch.
	 *
	 * If the firmware patching fails, the manufacturer mode is
	 * disabled with reset and deactivating the patch.
	 *
	 * If the default patch file is used, no reset is done when disabling
	 * the manufacturer.
	 */
	while (fw->size > fw_ptr - fw->data) {
		int ret;

		ret = btintel_legacy_rom_patching(hdev, fw, &fw_ptr,
						 &disable_patch);
		if (ret < 0)
			goto exit_mfg_deactivate;
	}

	release_firmware(fw);

	if (disable_patch)
		goto exit_mfg_disable;

	/* Patching completed successfully and disable the manufacturer mode
	 * with reset and activate the downloaded firmware patches.
	 */
	err = btintel_exit_mfg(hdev, true, true);
	if (err)
		return err;

	/* Need build number for downloaded fw patches in
	 * every power-on boot
	 */
	err = btintel_read_version(hdev, &new_ver);
	if (err)
		return err;

	bt_dev_info(hdev, "Intel BT fw patch 0x%02x completed & activated",
		    new_ver.fw_patch_num);

	goto complete;

exit_mfg_disable:
	/* Disable the manufacturer mode without reset */
	err = btintel_exit_mfg(hdev, false, false);
	if (err)
		return err;

	bt_dev_info(hdev, "Intel firmware patch completed");

	goto complete;

exit_mfg_deactivate:
	release_firmware(fw);

	/* Patching failed. Disable the manufacturer mode with reset and
	 * deactivate the downloaded firmware patches.
	 */
	err = btintel_exit_mfg(hdev, true, false);
	if (err)
		return err;

	bt_dev_info(hdev, "Intel firmware patch completed and deactivated");

complete:
	/* Set the event mask for Intel specific vendor events. This enables
	 * a few extra events that are useful during general operation.
	 */
	btintel_set_event_mask_mfg(hdev, false);

	btintel_check_bdaddr(hdev);

	return 0;
}

static int btintel_download_wait(struct hci_dev *hdev, ktime_t calltime, int msec)
{
	ktime_t delta, rettime;
	unsigned long long duration;
	int err;

	btintel_set_flag(hdev, INTEL_FIRMWARE_LOADED);

	bt_dev_info(hdev, "Waiting for firmware download to complete");

	err = btintel_wait_on_flag_timeout(hdev, INTEL_DOWNLOADING,
					   TASK_INTERRUPTIBLE,
					   msecs_to_jiffies(msec));
	if (err == -EINTR) {
		bt_dev_err(hdev, "Firmware loading interrupted");
		return err;
	}

	if (err) {
		bt_dev_err(hdev, "Firmware loading timeout");
		return -ETIMEDOUT;
	}

	if (btintel_test_flag(hdev, INTEL_FIRMWARE_FAILED)) {
		bt_dev_err(hdev, "Firmware loading failed");
		return -ENOEXEC;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long)ktime_to_ns(delta) >> 10;

	bt_dev_info(hdev, "Firmware loaded in %llu usecs", duration);

	return 0;
}

static int btintel_boot_wait(struct hci_dev *hdev, ktime_t calltime, int msec)
{
	ktime_t delta, rettime;
	unsigned long long duration;
	int err;

	bt_dev_info(hdev, "Waiting for device to boot");

	err = btintel_wait_on_flag_timeout(hdev, INTEL_BOOTING,
					   TASK_INTERRUPTIBLE,
					   msecs_to_jiffies(msec));
	if (err == -EINTR) {
		bt_dev_err(hdev, "Device boot interrupted");
		return -EINTR;
	}

	if (err) {
		bt_dev_err(hdev, "Device boot timeout");
		return -ETIMEDOUT;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;

	bt_dev_info(hdev, "Device booted in %llu usecs", duration);

	return 0;
}

static int btintel_boot_wait_d0(struct hci_dev *hdev, ktime_t calltime,
				int msec)
{
	ktime_t delta, rettime;
	unsigned long long duration;
	int err;

	bt_dev_info(hdev, "Waiting for device transition to d0");

	err = btintel_wait_on_flag_timeout(hdev, INTEL_WAIT_FOR_D0,
					   TASK_INTERRUPTIBLE,
					   msecs_to_jiffies(msec));
	if (err == -EINTR) {
		bt_dev_err(hdev, "Device d0 move interrupted");
		return -EINTR;
	}

	if (err) {
		bt_dev_err(hdev, "Device d0 move timeout");
		return -ETIMEDOUT;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long)ktime_to_ns(delta) >> 10;

	bt_dev_info(hdev, "Device moved to D0 in %llu usecs", duration);

	return 0;
}

static int btintel_boot(struct hci_dev *hdev, u32 boot_addr)
{
	ktime_t calltime;
	int err;

	calltime = ktime_get();

	btintel_set_flag(hdev, INTEL_BOOTING);
	btintel_set_flag(hdev, INTEL_WAIT_FOR_D0);

	err = btintel_send_intel_reset(hdev, boot_addr);
	if (err) {
		bt_dev_err(hdev, "Intel Soft Reset failed (%d)", err);
		btintel_reset_to_bootloader(hdev);
		return err;
	}

	/* The bootloader will not indicate when the device is ready. This
	 * is done by the operational firmware sending bootup notification.
	 *
	 * Booting into operational firmware should not take longer than
	 * 5 second. However if that happens, then just fail the setup
	 * since something went wrong.
	 */
	err = btintel_boot_wait(hdev, calltime, 5000);
	if (err == -ETIMEDOUT) {
		btintel_reset_to_bootloader(hdev);
		goto exit_error;
	}

	if (hdev->bus == HCI_PCI) {
		/* In case of PCIe, after receiving bootup event, driver performs
		 * D0 entry by writing 0 to sleep control register (check
		 * btintel_pcie_recv_event())
		 * Firmware acks with alive interrupt indicating host is full ready to
		 * perform BT operation. Lets wait here till INTEL_WAIT_FOR_D0
		 * bit is cleared.
		 */
		calltime = ktime_get();
		err = btintel_boot_wait_d0(hdev, calltime, 2000);
	}

exit_error:
	return err;
}

static int btintel_get_fw_name(struct intel_version *ver,
					     struct intel_boot_params *params,
					     char *fw_name, size_t len,
					     const char *suffix)
{
	switch (ver->hw_variant) {
	case 0x0b:	/* SfP */
	case 0x0c:	/* WsP */
		snprintf(fw_name, len, "intel/ibt-%u-%u.%s",
			 ver->hw_variant,
			 le16_to_cpu(params->dev_revid),
			 suffix);
		break;
	case 0x11:	/* JfP */
	case 0x12:	/* ThP */
	case 0x13:	/* HrP */
	case 0x14:	/* CcP */
		snprintf(fw_name, len, "intel/ibt-%u-%u-%u.%s",
			 ver->hw_variant,
			 ver->hw_revision,
			 ver->fw_revision,
			 suffix);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int btintel_download_fw(struct hci_dev *hdev,
					 struct intel_version *ver,
					 struct intel_boot_params *params,
					 u32 *boot_param)
{
	const struct firmware *fw;
	char fwname[64];
	int err;
	ktime_t calltime;

	if (!ver || !params)
		return -EINVAL;

	/* The firmware variant determines if the device is in bootloader
	 * mode or is running operational firmware. The value 0x06 identifies
	 * the bootloader and the value 0x23 identifies the operational
	 * firmware.
	 *
	 * When the operational firmware is already present, then only
	 * the check for valid Bluetooth device address is needed. This
	 * determines if the device will be added as configured or
	 * unconfigured controller.
	 *
	 * It is not possible to use the Secure Boot Parameters in this
	 * case since that command is only available in bootloader mode.
	 */
	if (ver->fw_variant == 0x23) {
		btintel_clear_flag(hdev, INTEL_BOOTLOADER);
		btintel_check_bdaddr(hdev);

		/* SfP and WsP don't seem to update the firmware version on file
		 * so version checking is currently possible.
		 */
		switch (ver->hw_variant) {
		case 0x0b:	/* SfP */
		case 0x0c:	/* WsP */
			return 0;
		}

		/* Proceed to download to check if the version matches */
		goto download;
	}

	/* Read the secure boot parameters to identify the operating
	 * details of the bootloader.
	 */
	err = btintel_read_boot_params(hdev, params);
	if (err)
		return err;

	/* It is required that every single firmware fragment is acknowledged
	 * with a command complete event. If the boot parameters indicate
	 * that this bootloader does not send them, then abort the setup.
	 */
	if (params->limited_cce != 0x00) {
		bt_dev_err(hdev, "Unsupported Intel firmware loading method (%u)",
			   params->limited_cce);
		return -EINVAL;
	}

	/* If the OTP has no valid Bluetooth device address, then there will
	 * also be no valid address for the operational firmware.
	 */
	if (!bacmp(&params->otp_bdaddr, BDADDR_ANY)) {
		bt_dev_info(hdev, "No device address configured");
		set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
	}

download:
	/* With this Intel bootloader only the hardware variant and device
	 * revision information are used to select the right firmware for SfP
	 * and WsP.
	 *
	 * The firmware filename is ibt-<hw_variant>-<dev_revid>.sfi.
	 *
	 * Currently the supported hardware variants are:
	 *   11 (0x0b) for iBT3.0 (LnP/SfP)
	 *   12 (0x0c) for iBT3.5 (WsP)
	 *
	 * For ThP/JfP and for future SKU's, the FW name varies based on HW
	 * variant, HW revision and FW revision, as these are dependent on CNVi
	 * and RF Combination.
	 *
	 *   17 (0x11) for iBT3.5 (JfP)
	 *   18 (0x12) for iBT3.5 (ThP)
	 *
	 * The firmware file name for these will be
	 * ibt-<hw_variant>-<hw_revision>-<fw_revision>.sfi.
	 *
	 */
	err = btintel_get_fw_name(ver, params, fwname, sizeof(fwname), "sfi");
	if (err < 0) {
		if (!btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
			/* Firmware has already been loaded */
			btintel_set_flag(hdev, INTEL_FIRMWARE_LOADED);
			return 0;
		}

		bt_dev_err(hdev, "Unsupported Intel firmware naming");
		return -EINVAL;
	}

	err = firmware_request_nowarn(&fw, fwname, &hdev->dev);
	if (err < 0) {
		if (!btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
			/* Firmware has already been loaded */
			btintel_set_flag(hdev, INTEL_FIRMWARE_LOADED);
			return 0;
		}

		bt_dev_err(hdev, "Failed to load Intel firmware file %s (%d)",
			   fwname, err);
		return err;
	}

	bt_dev_info(hdev, "Found device firmware: %s", fwname);

	if (fw->size < 644) {
		bt_dev_err(hdev, "Invalid size of firmware file (%zu)",
			   fw->size);
		err = -EBADF;
		goto done;
	}

	calltime = ktime_get();

	btintel_set_flag(hdev, INTEL_DOWNLOADING);

	/* Start firmware downloading and get boot parameter */
	err = btintel_download_firmware(hdev, ver, fw, boot_param);
	if (err < 0) {
		if (err == -EALREADY) {
			/* Firmware has already been loaded */
			btintel_set_flag(hdev, INTEL_FIRMWARE_LOADED);
			err = 0;
			goto done;
		}

		/* When FW download fails, send Intel Reset to retry
		 * FW download.
		 */
		btintel_reset_to_bootloader(hdev);
		goto done;
	}

	/* Before switching the device into operational mode and with that
	 * booting the loaded firmware, wait for the bootloader notification
	 * that all fragments have been successfully received.
	 *
	 * When the event processing receives the notification, then the
	 * INTEL_DOWNLOADING flag will be cleared.
	 *
	 * The firmware loading should not take longer than 5 seconds
	 * and thus just timeout if that happens and fail the setup
	 * of this device.
	 */
	err = btintel_download_wait(hdev, calltime, 5000);
	if (err == -ETIMEDOUT)
		btintel_reset_to_bootloader(hdev);

done:
	release_firmware(fw);
	return err;
}

static int btintel_bootloader_setup(struct hci_dev *hdev,
				    struct intel_version *ver)
{
	struct intel_version new_ver;
	struct intel_boot_params params;
	u32 boot_param;
	char ddcname[64];
	int err;

	BT_DBG("%s", hdev->name);

	/* Set the default boot parameter to 0x0 and it is updated to
	 * SKU specific boot parameter after reading Intel_Write_Boot_Params
	 * command while downloading the firmware.
	 */
	boot_param = 0x00000000;

	btintel_set_flag(hdev, INTEL_BOOTLOADER);

	err = btintel_download_fw(hdev, ver, &params, &boot_param);
	if (err)
		return err;

	/* controller is already having an operational firmware */
	if (ver->fw_variant == 0x23)
		goto finish;

	err = btintel_boot(hdev, boot_param);
	if (err)
		return err;

	btintel_clear_flag(hdev, INTEL_BOOTLOADER);

	err = btintel_get_fw_name(ver, &params, ddcname,
						sizeof(ddcname), "ddc");

	if (err < 0) {
		bt_dev_err(hdev, "Unsupported Intel firmware naming");
	} else {
		/* Once the device is running in operational mode, it needs to
		 * apply the device configuration (DDC) parameters.
		 *
		 * The device can work without DDC parameters, so even if it
		 * fails to load the file, no need to fail the setup.
		 */
		btintel_load_ddc_config(hdev, ddcname);
	}

	hci_dev_clear_flag(hdev, HCI_QUALITY_REPORT);

	/* Read the Intel version information after loading the FW  */
	err = btintel_read_version(hdev, &new_ver);
	if (err)
		return err;

	btintel_version_info(hdev, &new_ver);

finish:
	/* Set the event mask for Intel specific vendor events. This enables
	 * a few extra events that are useful during general operation. It
	 * does not enable any debugging related events.
	 *
	 * The device will function correctly without these events enabled
	 * and thus no need to fail the setup.
	 */
	btintel_set_event_mask(hdev, false);

	return 0;
}

static void btintel_get_fw_name_tlv(const struct intel_version_tlv *ver,
				    char *fw_name, size_t len,
				    const char *suffix)
{
	const char *format;
	u32 cnvi, cnvr;

	cnvi = INTEL_CNVX_TOP_PACK_SWAB(INTEL_CNVX_TOP_TYPE(ver->cnvi_top),
					INTEL_CNVX_TOP_STEP(ver->cnvi_top));

	cnvr = INTEL_CNVX_TOP_PACK_SWAB(INTEL_CNVX_TOP_TYPE(ver->cnvr_top),
					INTEL_CNVX_TOP_STEP(ver->cnvr_top));

	/* Only Blazar  product supports downloading of intermediate loader
	 * image
	 */
	if (INTEL_HW_VARIANT(ver->cnvi_bt) >= 0x1e) {
		u8 zero[BTINTEL_FWID_MAXLEN];

		if (ver->img_type == BTINTEL_IMG_BOOTLOADER) {
			format = "intel/ibt-%04x-%04x-iml.%s";
			snprintf(fw_name, len, format, cnvi, cnvr, suffix);
			return;
		}

		memset(zero, 0, sizeof(zero));

		/* ibt-<cnvi_top type+cnvi_top step>-<cnvr_top type+cnvr_top step-fw_id> */
		if (memcmp(ver->fw_id, zero, sizeof(zero))) {
			format = "intel/ibt-%04x-%04x-%s.%s";
			snprintf(fw_name, len, format, cnvi, cnvr,
				 ver->fw_id, suffix);
			return;
		}
		/* If firmware id is not present, fallback to legacy naming
		 * convention
		 */
	}
	/* Fallback to legacy naming convention for other controllers
	 * ibt-<cnvi_top type+cnvi_top step>-<cnvr_top type+cnvr_top step>
	 */
	format = "intel/ibt-%04x-%04x.%s";
	snprintf(fw_name, len, format, cnvi, cnvr, suffix);
}

static void btintel_get_iml_tlv(const struct intel_version_tlv *ver,
				char *fw_name, size_t len,
				const char *suffix)
{
	const char *format;
	u32 cnvi, cnvr;

	cnvi = INTEL_CNVX_TOP_PACK_SWAB(INTEL_CNVX_TOP_TYPE(ver->cnvi_top),
					INTEL_CNVX_TOP_STEP(ver->cnvi_top));

	cnvr = INTEL_CNVX_TOP_PACK_SWAB(INTEL_CNVX_TOP_TYPE(ver->cnvr_top),
					INTEL_CNVX_TOP_STEP(ver->cnvr_top));

	format = "intel/ibt-%04x-%04x-iml.%s";
	snprintf(fw_name, len, format, cnvi, cnvr, suffix);
}

static int btintel_prepare_fw_download_tlv(struct hci_dev *hdev,
					   struct intel_version_tlv *ver,
					   u32 *boot_param)
{
	const struct firmware *fw;
	char fwname[128];
	int err;
	ktime_t calltime;

	if (!ver || !boot_param)
		return -EINVAL;

	/* The firmware variant determines if the device is in bootloader
	 * mode or is running operational firmware. The value 0x03 identifies
	 * the bootloader and the value 0x23 identifies the operational
	 * firmware.
	 *
	 * When the operational firmware is already present, then only
	 * the check for valid Bluetooth device address is needed. This
	 * determines if the device will be added as configured or
	 * unconfigured controller.
	 *
	 * It is not possible to use the Secure Boot Parameters in this
	 * case since that command is only available in bootloader mode.
	 */
	if (ver->img_type == BTINTEL_IMG_OP) {
		btintel_clear_flag(hdev, INTEL_BOOTLOADER);
		btintel_check_bdaddr(hdev);
	} else {
		/*
		 * Check for valid bd address in boot loader mode. Device
		 * will be marked as unconfigured if empty bd address is
		 * found.
		 */
		if (!bacmp(&ver->otp_bd_addr, BDADDR_ANY)) {
			bt_dev_info(hdev, "No device address configured");
			set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
		}
	}

	if (ver->img_type == BTINTEL_IMG_OP) {
		/* Controller running OP image. In case of FW downgrade,
		 * FWID TLV may not be present and driver may attempt to load
		 * firmware image which doesn't exist. Lets compare the version
		 * of IML image
		 */
		if (INTEL_HW_VARIANT(ver->cnvi_bt) >= 0x1e)
			btintel_get_iml_tlv(ver, fwname, sizeof(fwname), "sfi");
		else
			btintel_get_fw_name_tlv(ver, fwname, sizeof(fwname), "sfi");
	} else {
		btintel_get_fw_name_tlv(ver, fwname, sizeof(fwname), "sfi");
	}

	err = firmware_request_nowarn(&fw, fwname, &hdev->dev);
	if (err < 0) {
		if (!btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
			/* Firmware has already been loaded */
			btintel_set_flag(hdev, INTEL_FIRMWARE_LOADED);
			return 0;
		}

		bt_dev_err(hdev, "Failed to load Intel firmware file %s (%d)",
			   fwname, err);

		return err;
	}

	bt_dev_info(hdev, "Found device firmware: %s", fwname);

	if (fw->size < 644) {
		bt_dev_err(hdev, "Invalid size of firmware file (%zu)",
			   fw->size);
		err = -EBADF;
		goto done;
	}

	calltime = ktime_get();

	btintel_set_flag(hdev, INTEL_DOWNLOADING);

	/* Start firmware downloading and get boot parameter */
	err = btintel_download_fw_tlv(hdev, ver, fw, boot_param,
					       INTEL_HW_VARIANT(ver->cnvi_bt),
					       ver->sbe_type);
	if (err < 0) {
		if (err == -EALREADY) {
			/* Firmware has already been loaded */
			btintel_set_flag(hdev, INTEL_FIRMWARE_LOADED);
			err = 0;
			goto done;
		}

		/* When FW download fails, send Intel Reset to retry
		 * FW download.
		 */
		btintel_reset_to_bootloader(hdev);
		goto done;
	}

	/* Before switching the device into operational mode and with that
	 * booting the loaded firmware, wait for the bootloader notification
	 * that all fragments have been successfully received.
	 *
	 * When the event processing receives the notification, then the
	 * BTUSB_DOWNLOADING flag will be cleared.
	 *
	 * The firmware loading should not take longer than 5 seconds
	 * and thus just timeout if that happens and fail the setup
	 * of this device.
	 */
	err = btintel_download_wait(hdev, calltime, 5000);
	if (err == -ETIMEDOUT)
		btintel_reset_to_bootloader(hdev);

done:
	release_firmware(fw);
	return err;
}

static int btintel_get_codec_config_data(struct hci_dev *hdev,
					 __u8 link, struct bt_codec *codec,
					 __u8 *ven_len, __u8 **ven_data)
{
	int err = 0;

	if (!ven_data || !ven_len)
		return -EINVAL;

	*ven_len = 0;
	*ven_data = NULL;

	if (link != ESCO_LINK) {
		bt_dev_err(hdev, "Invalid link type(%u)", link);
		return -EINVAL;
	}

	*ven_data = kmalloc(sizeof(__u8), GFP_KERNEL);
	if (!*ven_data) {
		err = -ENOMEM;
		goto error;
	}

	/* supports only CVSD and mSBC offload codecs */
	switch (codec->id) {
	case 0x02:
		**ven_data = 0x00;
		break;
	case 0x05:
		**ven_data = 0x01;
		break;
	default:
		err = -EINVAL;
		bt_dev_err(hdev, "Invalid codec id(%u)", codec->id);
		goto error;
	}
	/* codec and its capabilities are pre-defined to ids
	 * preset id = 0x00 represents CVSD codec with sampling rate 8K
	 * preset id = 0x01 represents mSBC codec with sampling rate 16K
	 */
	*ven_len = sizeof(__u8);
	return err;

error:
	kfree(*ven_data);
	*ven_data = NULL;
	return err;
}

static int btintel_get_data_path_id(struct hci_dev *hdev, __u8 *data_path_id)
{
	/* Intel uses 1 as data path id for all the usecases */
	*data_path_id = 1;
	return 0;
}

static int btintel_configure_offload(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	int err = 0;
	struct intel_offload_use_cases *use_cases;

	skb = __hci_cmd_sync(hdev, 0xfc86, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading offload use cases failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len < sizeof(*use_cases)) {
		err = -EIO;
		goto error;
	}

	use_cases = (void *)skb->data;

	if (use_cases->status) {
		err = -bt_to_errno(skb->data[0]);
		goto error;
	}

	if (use_cases->preset[0] & 0x03) {
		hdev->get_data_path_id = btintel_get_data_path_id;
		hdev->get_codec_config_data = btintel_get_codec_config_data;
	}
error:
	kfree_skb(skb);
	return err;
}

static void btintel_set_ppag(struct hci_dev *hdev, struct intel_version_tlv *ver)
{
	struct sk_buff *skb;
	struct hci_ppag_enable_cmd ppag_cmd;
	acpi_handle handle;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *p, *elements;
	u32 domain, mode;
	acpi_status status;

	/* PPAG is not supported if CRF is HrP2, Jfp2, JfP1 */
	switch (ver->cnvr_top & 0xFFF) {
	case 0x504:     /* Hrp2 */
	case 0x202:     /* Jfp2 */
	case 0x201:     /* Jfp1 */
		bt_dev_dbg(hdev, "PPAG not supported for Intel CNVr (0x%3x)",
			   ver->cnvr_top & 0xFFF);
		return;
	}

	handle = ACPI_HANDLE(GET_HCIDEV_DEV(hdev));
	if (!handle) {
		bt_dev_info(hdev, "No support for BT device in ACPI firmware");
		return;
	}

	status = acpi_evaluate_object(handle, "PPAG", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		if (status == AE_NOT_FOUND) {
			bt_dev_dbg(hdev, "PPAG-BT: ACPI entry not found");
			return;
		}
		bt_dev_warn(hdev, "PPAG-BT: ACPI Failure: %s", acpi_format_exception(status));
		return;
	}

	p = buffer.pointer;
	if (p->type != ACPI_TYPE_PACKAGE || p->package.count != 2) {
		bt_dev_warn(hdev, "PPAG-BT: Invalid object type: %d or package count: %d",
			    p->type, p->package.count);
		kfree(buffer.pointer);
		return;
	}

	elements = p->package.elements;

	/* PPAG table is located at element[1] */
	p = &elements[1];

	domain = (u32)p->package.elements[0].integer.value;
	mode = (u32)p->package.elements[1].integer.value;
	kfree(buffer.pointer);

	if (domain != 0x12) {
		bt_dev_dbg(hdev, "PPAG-BT: Bluetooth domain is disabled in ACPI firmware");
		return;
	}

	/* PPAG mode
	 * BIT 0 : 0 Disabled in EU
	 *         1 Enabled in EU
	 * BIT 1 : 0 Disabled in China
	 *         1 Enabled in China
	 */
	mode &= 0x03;

	if (!mode) {
		bt_dev_dbg(hdev, "PPAG-BT: EU, China mode are disabled in BIOS");
		return;
	}

	ppag_cmd.ppag_enable_flags = cpu_to_le32(mode);

	skb = __hci_cmd_sync(hdev, INTEL_OP_PPAG_CMD, sizeof(ppag_cmd),
			     &ppag_cmd, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_warn(hdev, "Failed to send PPAG Enable (%ld)", PTR_ERR(skb));
		return;
	}
	bt_dev_info(hdev, "PPAG-BT: Enabled (Mode %d)", mode);
	kfree_skb(skb);
}

static int btintel_acpi_reset_method(struct hci_dev *hdev)
{
	int ret = 0;
	acpi_status status;
	union acpi_object *p, *ref;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	status = acpi_evaluate_object(ACPI_HANDLE(GET_HCIDEV_DEV(hdev)), "_PRR", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		bt_dev_err(hdev, "Failed to run _PRR method");
		ret = -ENODEV;
		return ret;
	}
	p = buffer.pointer;

	if (p->package.count != 1 || p->type != ACPI_TYPE_PACKAGE) {
		bt_dev_err(hdev, "Invalid arguments");
		ret = -EINVAL;
		goto exit_on_error;
	}

	ref = &p->package.elements[0];
	if (ref->type != ACPI_TYPE_LOCAL_REFERENCE) {
		bt_dev_err(hdev, "Invalid object type: 0x%x", ref->type);
		ret = -EINVAL;
		goto exit_on_error;
	}

	status = acpi_evaluate_object(ref->reference.handle, "_RST", NULL, NULL);
	if (ACPI_FAILURE(status)) {
		bt_dev_err(hdev, "Failed to run_RST method");
		ret = -ENODEV;
		goto exit_on_error;
	}

exit_on_error:
	kfree(buffer.pointer);
	return ret;
}

static void btintel_set_dsm_reset_method(struct hci_dev *hdev,
					 struct intel_version_tlv *ver_tlv)
{
	struct btintel_data *data = hci_get_priv(hdev);
	acpi_handle handle = ACPI_HANDLE(GET_HCIDEV_DEV(hdev));
	u8 reset_payload[4] = {0x01, 0x00, 0x01, 0x00};
	union acpi_object *obj, argv4;
	enum {
		RESET_TYPE_WDISABLE2,
		RESET_TYPE_VSEC
	};

	handle = ACPI_HANDLE(GET_HCIDEV_DEV(hdev));

	if (!handle) {
		bt_dev_dbg(hdev, "No support for bluetooth device in ACPI firmware");
		return;
	}

	if (!acpi_has_method(handle, "_PRR")) {
		bt_dev_err(hdev, "No support for _PRR ACPI method");
		return;
	}

	switch (ver_tlv->cnvi_top & 0xfff) {
	case 0x910: /* GalePeak2 */
		reset_payload[2] = RESET_TYPE_VSEC;
		break;
	default:
		/* WDISABLE2 is the default reset method */
		reset_payload[2] = RESET_TYPE_WDISABLE2;

		if (!acpi_check_dsm(handle, &btintel_guid_dsm, 0,
				    BIT(DSM_SET_WDISABLE2_DELAY))) {
			bt_dev_err(hdev, "No dsm support to set reset delay");
			return;
		}
		argv4.integer.type = ACPI_TYPE_INTEGER;
		/* delay required to toggle BT power */
		argv4.integer.value = 160;
		obj = acpi_evaluate_dsm(handle, &btintel_guid_dsm, 0,
					DSM_SET_WDISABLE2_DELAY, &argv4);
		if (!obj) {
			bt_dev_err(hdev, "Failed to call dsm to set reset delay");
			return;
		}
		ACPI_FREE(obj);
	}

	bt_dev_info(hdev, "DSM reset method type: 0x%02x", reset_payload[2]);

	if (!acpi_check_dsm(handle, &btintel_guid_dsm, 0,
			    DSM_SET_RESET_METHOD)) {
		bt_dev_warn(hdev, "No support for dsm to set reset method");
		return;
	}
	argv4.buffer.type = ACPI_TYPE_BUFFER;
	argv4.buffer.length = sizeof(reset_payload);
	argv4.buffer.pointer = reset_payload;

	obj = acpi_evaluate_dsm(handle, &btintel_guid_dsm, 0,
				DSM_SET_RESET_METHOD, &argv4);
	if (!obj) {
		bt_dev_err(hdev, "Failed to call dsm to set reset method");
		return;
	}
	ACPI_FREE(obj);
	data->acpi_reset_method = btintel_acpi_reset_method;
}

#define BTINTEL_ISODATA_HANDLE_BASE 0x900

static u8 btintel_classify_pkt_type(struct hci_dev *hdev, struct sk_buff *skb)
{
	/*
	 * Distinguish ISO data packets form ACL data packets
	 * based on their connection handle value range.
	 */
	if (hci_skb_pkt_type(skb) == HCI_ACLDATA_PKT) {
		__u16 handle = __le16_to_cpu(hci_acl_hdr(skb)->handle);

		if (hci_handle(handle) >= BTINTEL_ISODATA_HANDLE_BASE)
			return HCI_ISODATA_PKT;
	}

	return hci_skb_pkt_type(skb);
}

/*
 * UefiCnvCommonDSBR UEFI variable provides information from the OEM platforms
 * if they have replaced the BRI (Bluetooth Radio Interface) resistor to
 * overcome the potential STEP errors on their designs. Based on the
 * configauration, bluetooth firmware shall adjust the BRI response line drive
 * strength. The below structure represents DSBR data.
 * struct {
 *	u8 header;
 *	u32 dsbr;
 * } __packed;
 *
 * header - defines revision number of the structure
 * dsbr - defines drive strength BRI response
 *	bit0
 *		0 - instructs bluetooth firmware to use default values
 *		1 - instructs bluetooth firmware to override default values
 *	bit3:1
 *		Reserved
 *	bit7:4
 *		DSBR override values (only if bit0 is set. Default value is 0xF
 *	bit31:7
 *		Reserved
 * Expected values for dsbr field:
 *	1. 0xF1 - indicates that the resistor on board is 33 Ohm
 *	2. 0x00 or 0xB1 - indicates that the resistor on board is 10 Ohm
 *	3. Non existing UEFI variable or invalid (none of the above) - indicates
 *	   that the resistor on board is 10 Ohm
 * Even if uefi variable is not present, driver shall send 0xfc0a command to
 * firmware to use default values.
 *
 */
static int btintel_uefi_get_dsbr(u32 *dsbr_var)
{
	struct btintel_dsbr {
		u8 header;
		u32 dsbr;
	} __packed data;

	efi_status_t status;
	unsigned long data_size = 0;
	efi_guid_t guid = EFI_GUID(0xe65d8884, 0xd4af, 0x4b20, 0x8d, 0x03,
				   0x77, 0x2e, 0xcc, 0x3d, 0xa5, 0x31);

	if (!IS_ENABLED(CONFIG_EFI))
		return -EOPNOTSUPP;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE))
		return -EOPNOTSUPP;

	status = efi.get_variable(BTINTEL_EFI_DSBR, &guid, NULL, &data_size,
				  NULL);

	if (status != EFI_BUFFER_TOO_SMALL || !data_size)
		return -EIO;

	status = efi.get_variable(BTINTEL_EFI_DSBR, &guid, NULL, &data_size,
				  &data);

	if (status != EFI_SUCCESS)
		return -ENXIO;

	*dsbr_var = data.dsbr;
	return 0;
}

static int btintel_set_dsbr(struct hci_dev *hdev, struct intel_version_tlv *ver)
{
	struct btintel_dsbr_cmd {
		u8 enable;
		u8 dsbr;
	} __packed;

	struct btintel_dsbr_cmd cmd;
	struct sk_buff *skb;
	u32 dsbr, cnvi;
	u8 status;
	int err;

	cnvi = ver->cnvi_top & 0xfff;
	/* DSBR command needs to be sent for,
	 * 1. BlazarI or BlazarIW + B0 step product in IML image.
	 * 2. Gale Peak2 or BlazarU in OP image.
	 */

	switch (cnvi) {
	case BTINTEL_CNVI_BLAZARI:
	case BTINTEL_CNVI_BLAZARIW:
		if (ver->img_type == BTINTEL_IMG_IML &&
		    INTEL_CNVX_TOP_STEP(ver->cnvi_top) == 0x01)
			break;
		return 0;
	case BTINTEL_CNVI_GAP:
	case BTINTEL_CNVI_BLAZARU:
		if (ver->img_type == BTINTEL_IMG_OP &&
		    hdev->bus == HCI_USB)
			break;
		return 0;
	default:
		return 0;
	}

	dsbr = 0;
	err = btintel_uefi_get_dsbr(&dsbr);
	if (err < 0)
		bt_dev_dbg(hdev, "Error reading efi: %ls  (%d)",
			   BTINTEL_EFI_DSBR, err);

	cmd.enable = dsbr & BIT(0);
	cmd.dsbr = dsbr >> 4 & 0xF;

	bt_dev_info(hdev, "dsbr: enable: 0x%2.2x value: 0x%2.2x", cmd.enable,
		    cmd.dsbr);

	skb = __hci_cmd_sync(hdev, 0xfc0a, sizeof(cmd), &cmd,  HCI_CMD_TIMEOUT);
	if (IS_ERR(skb))
		return -bt_to_errno(PTR_ERR(skb));

	status = skb->data[0];
	kfree_skb(skb);

	if (status)
		return -bt_to_errno(status);

	return 0;
}

int btintel_bootloader_setup_tlv(struct hci_dev *hdev,
				 struct intel_version_tlv *ver)
{
	u32 boot_param;
	char ddcname[64];
	int err;
	struct intel_version_tlv new_ver;

	bt_dev_dbg(hdev, "");

	/* Set the default boot parameter to 0x0 and it is updated to
	 * SKU specific boot parameter after reading Intel_Write_Boot_Params
	 * command while downloading the firmware.
	 */
	boot_param = 0x00000000;

	/* In case of PCIe, this function might get called multiple times with
	 * same hdev instance if there is any error on firmware download.
	 * Need to clear stale bits of previous firmware download attempt.
	 */
	for (int i = 0; i < __INTEL_NUM_FLAGS; i++)
		btintel_clear_flag(hdev, i);

	btintel_set_flag(hdev, INTEL_BOOTLOADER);

	err = btintel_prepare_fw_download_tlv(hdev, ver, &boot_param);
	if (err)
		return err;

	/* check if controller is already having an operational firmware */
	if (ver->img_type == BTINTEL_IMG_OP)
		goto finish;

	err = btintel_boot(hdev, boot_param);
	if (err)
		return err;

	err = btintel_read_version_tlv(hdev, ver);
	if (err)
		return err;

	/* set drive strength of BRI response */
	err = btintel_set_dsbr(hdev, ver);
	if (err) {
		bt_dev_err(hdev, "Failed to send dsbr command (%d)", err);
		return err;
	}

	/* If image type returned is BTINTEL_IMG_IML, then controller supports
	 * intermediate loader image
	 */
	if (ver->img_type == BTINTEL_IMG_IML) {
		err = btintel_prepare_fw_download_tlv(hdev, ver, &boot_param);
		if (err)
			return err;

		err = btintel_boot(hdev, boot_param);
		if (err)
			return err;
	}

	btintel_clear_flag(hdev, INTEL_BOOTLOADER);

	btintel_get_fw_name_tlv(ver, ddcname, sizeof(ddcname), "ddc");
	/* Once the device is running in operational mode, it needs to
	 * apply the device configuration (DDC) parameters.
	 *
	 * The device can work without DDC parameters, so even if it
	 * fails to load the file, no need to fail the setup.
	 */
	btintel_load_ddc_config(hdev, ddcname);

	/* Read supported use cases and set callbacks to fetch datapath id */
	btintel_configure_offload(hdev);

	hci_dev_clear_flag(hdev, HCI_QUALITY_REPORT);

	/* Set PPAG feature */
	btintel_set_ppag(hdev, ver);

	/* Read the Intel version information after loading the FW  */
	err = btintel_read_version_tlv(hdev, &new_ver);
	if (err)
		return err;

	btintel_version_info_tlv(hdev, &new_ver);

finish:
	/* Set the event mask for Intel specific vendor events. This enables
	 * a few extra events that are useful during general operation. It
	 * does not enable any debugging related events.
	 *
	 * The device will function correctly without these events enabled
	 * and thus no need to fail the setup.
	 */
	btintel_set_event_mask(hdev, false);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_bootloader_setup_tlv);

void btintel_set_msft_opcode(struct hci_dev *hdev, u8 hw_variant)
{
	switch (hw_variant) {
	/* Legacy bootloader devices that supports MSFT Extension */
	case 0x11:	/* JfP */
	case 0x12:	/* ThP */
	case 0x13:	/* HrP */
	case 0x14:	/* CcP */
	/* All Intel new generation controllers support the Microsoft vendor
	 * extension are using 0xFC1E for VsMsftOpCode.
	 */
	case 0x17:
	case 0x18:
	case 0x19:
	case 0x1b:
	case 0x1c:
	case 0x1d:
	case 0x1e:
		hci_set_msft_opcode(hdev, 0xFC1E);
		break;
	default:
		/* Not supported */
		break;
	}
}
EXPORT_SYMBOL_GPL(btintel_set_msft_opcode);

void btintel_print_fseq_info(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	u8 *p;
	u32 val;
	const char *str;

	skb = __hci_cmd_sync(hdev, 0xfcb3, 0, NULL, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_dbg(hdev, "Reading fseq status command failed (%ld)",
			   PTR_ERR(skb));
		return;
	}

	if (skb->len < (sizeof(u32) * 16 + 2)) {
		bt_dev_dbg(hdev, "Malformed packet of length %u received",
			   skb->len);
		kfree_skb(skb);
		return;
	}

	p = skb_pull_data(skb, 1);
	if (*p) {
		bt_dev_dbg(hdev, "Failed to get fseq status (0x%2.2x)", *p);
		kfree_skb(skb);
		return;
	}

	p = skb_pull_data(skb, 1);
	switch (*p) {
	case 0:
		str = "Success";
		break;
	case 1:
		str = "Fatal error";
		break;
	case 2:
		str = "Semaphore acquire error";
		break;
	default:
		str = "Unknown error";
		break;
	}

	if (*p) {
		bt_dev_err(hdev, "Fseq status: %s (0x%2.2x)", str, *p);
		kfree_skb(skb);
		return;
	}

	bt_dev_info(hdev, "Fseq status: %s (0x%2.2x)", str, *p);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Reason: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Global version: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Installed version: 0x%8.8x", val);

	p = skb->data;
	skb_pull_data(skb, 4);
	bt_dev_info(hdev, "Fseq executed: %2.2u.%2.2u.%2.2u.%2.2u", p[0], p[1],
		    p[2], p[3]);

	p = skb->data;
	skb_pull_data(skb, 4);
	bt_dev_info(hdev, "Fseq BT Top: %2.2u.%2.2u.%2.2u.%2.2u", p[0], p[1],
		    p[2], p[3]);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq Top init version: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq Cnvio init version: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq MBX Wifi file version: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq BT version: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq Top reset address: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq MBX timeout: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq MBX ack: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq CNVi id: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq CNVr id: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq Error handle: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq Magic noalive indication: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq OTP version: 0x%8.8x", val);

	val = get_unaligned_le32(skb_pull_data(skb, 4));
	bt_dev_dbg(hdev, "Fseq MBX otp version: 0x%8.8x", val);

	kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(btintel_print_fseq_info);

static int btintel_setup_combined(struct hci_dev *hdev)
{
	const u8 param[1] = { 0xFF };
	struct intel_version ver;
	struct intel_version_tlv ver_tlv;
	struct sk_buff *skb;
	int err;

	BT_DBG("%s", hdev->name);

	/* The some controllers have a bug with the first HCI command sent to it
	 * returning number of completed commands as zero. This would stall the
	 * command processing in the Bluetooth core.
	 *
	 * As a workaround, send HCI Reset command first which will reset the
	 * number of completed commands and allow normal command processing
	 * from now on.
	 *
	 * Regarding the INTEL_BROKEN_SHUTDOWN_LED flag, these devices maybe
	 * in the SW_RFKILL ON state as a workaround of fixing LED issue during
	 * the shutdown() procedure, and once the device is in SW_RFKILL ON
	 * state, the only way to exit out of it is sending the HCI_Reset
	 * command.
	 */
	if (btintel_test_flag(hdev, INTEL_BROKEN_INITIAL_NCMD) ||
	    btintel_test_flag(hdev, INTEL_BROKEN_SHUTDOWN_LED)) {
		skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL,
				     HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			bt_dev_err(hdev,
				   "sending initial HCI reset failed (%ld)",
				   PTR_ERR(skb));
			return PTR_ERR(skb);
		}
		kfree_skb(skb);
	}

	/* Starting from TyP device, the command parameter and response are
	 * changed even though the OCF for HCI_Intel_Read_Version command
	 * remains same. The legacy devices can handle even if the
	 * command has a parameter and returns a correct version information.
	 * So, it uses new format to support both legacy and new format.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc05, 1, param, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading Intel version command failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	/* Check the status */
	if (skb->data[0]) {
		bt_dev_err(hdev, "Intel Read Version command failed (%02x)",
			   skb->data[0]);
		err = -EIO;
		goto exit_error;
	}

	/* Apply the common HCI quirks for Intel device */
	set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);
	set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);
	set_bit(HCI_QUIRK_NON_PERSISTENT_DIAG, &hdev->quirks);

	/* Set up the quality report callback for Intel devices */
	hdev->set_quality_report = btintel_set_quality_report;

	/* For Legacy device, check the HW platform value and size */
	if (skb->len == sizeof(ver) && skb->data[1] == 0x37) {
		bt_dev_dbg(hdev, "Read the legacy Intel version information");

		memcpy(&ver, skb->data, sizeof(ver));

		/* Display version information */
		btintel_version_info(hdev, &ver);

		/* Check for supported iBT hardware variants of this firmware
		 * loading method.
		 *
		 * This check has been put in place to ensure correct forward
		 * compatibility options when newer hardware variants come
		 * along.
		 */
		switch (ver.hw_variant) {
		case 0x07:	/* WP */
		case 0x08:	/* StP */
			/* Legacy ROM product */
			btintel_set_flag(hdev, INTEL_ROM_LEGACY);

			/* Apply the device specific HCI quirks
			 *
			 * WBS for SdP - For the Legacy ROM products, only SdP
			 * supports the WBS. But the version information is not
			 * enough to use here because the StP2 and SdP have same
			 * hw_variant and fw_variant. So, this flag is set by
			 * the transport driver (btusb) based on the HW info
			 * (idProduct)
			 */
			if (!btintel_test_flag(hdev,
					       INTEL_ROM_LEGACY_NO_WBS_SUPPORT))
				set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED,
					&hdev->quirks);

			err = btintel_legacy_rom_setup(hdev, &ver);
			break;
		case 0x0b:      /* SfP */
		case 0x11:      /* JfP */
		case 0x12:      /* ThP */
		case 0x13:      /* HrP */
		case 0x14:      /* CcP */
			fallthrough;
		case 0x0c:	/* WsP */
			/* Apply the device specific HCI quirks
			 *
			 * All Legacy bootloader devices support WBS
			 */
			set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED,
				&hdev->quirks);

			/* These variants don't seem to support LE Coded PHY */
			set_bit(HCI_QUIRK_BROKEN_LE_CODED, &hdev->quirks);

			/* Setup MSFT Extension support */
			btintel_set_msft_opcode(hdev, ver.hw_variant);

			err = btintel_bootloader_setup(hdev, &ver);
			btintel_register_devcoredump_support(hdev);
			break;
		default:
			bt_dev_err(hdev, "Unsupported Intel hw variant (%u)",
				   ver.hw_variant);
			err = -EINVAL;
		}

		hci_set_hw_info(hdev,
				"INTEL platform=%u variant=%u revision=%u",
				ver.hw_platform, ver.hw_variant,
				ver.hw_revision);

		goto exit_error;
	}

	/* memset ver_tlv to start with clean state as few fields are exclusive
	 * to bootloader mode and are not populated in operational mode
	 */
	memset(&ver_tlv, 0, sizeof(ver_tlv));
	/* For TLV type device, parse the tlv data */
	err = btintel_parse_version_tlv(hdev, &ver_tlv, skb);
	if (err) {
		bt_dev_err(hdev, "Failed to parse TLV version information");
		goto exit_error;
	}

	if (INTEL_HW_PLATFORM(ver_tlv.cnvi_bt) != 0x37) {
		bt_dev_err(hdev, "Unsupported Intel hardware platform (0x%2x)",
			   INTEL_HW_PLATFORM(ver_tlv.cnvi_bt));
		err = -EINVAL;
		goto exit_error;
	}

	/* Check for supported iBT hardware variants of this firmware
	 * loading method.
	 *
	 * This check has been put in place to ensure correct forward
	 * compatibility options when newer hardware variants come
	 * along.
	 */
	switch (INTEL_HW_VARIANT(ver_tlv.cnvi_bt)) {
	case 0x11:      /* JfP */
	case 0x12:      /* ThP */
	case 0x13:      /* HrP */
	case 0x14:      /* CcP */
		/* Some legacy bootloader devices starting from JfP,
		 * the operational firmware supports both old and TLV based
		 * HCI_Intel_Read_Version command based on the command
		 * parameter.
		 *
		 * For upgrading firmware case, the TLV based version cannot
		 * be used because the firmware filename for legacy bootloader
		 * is based on the old format.
		 *
		 * Also, it is not easy to convert TLV based version from the
		 * legacy version format.
		 *
		 * So, as a workaround for those devices, use the legacy
		 * HCI_Intel_Read_Version to get the version information and
		 * run the legacy bootloader setup.
		 */
		err = btintel_read_version(hdev, &ver);
		if (err)
			break;

		/* Apply the device specific HCI quirks
		 *
		 * All Legacy bootloader devices support WBS
		 */
		set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED, &hdev->quirks);

		/* These variants don't seem to support LE Coded PHY */
		set_bit(HCI_QUIRK_BROKEN_LE_CODED, &hdev->quirks);

		/* Setup MSFT Extension support */
		btintel_set_msft_opcode(hdev, ver.hw_variant);

		err = btintel_bootloader_setup(hdev, &ver);
		btintel_register_devcoredump_support(hdev);
		break;
	case 0x18: /* GfP2 */
	case 0x1c: /* GaP */
		/* Re-classify packet type for controllers with LE audio */
		hdev->classify_pkt_type = btintel_classify_pkt_type;
		fallthrough;
	case 0x17:
	case 0x19:
	case 0x1b:
	case 0x1d:
	case 0x1e:
		/* Display version information of TLV type */
		btintel_version_info_tlv(hdev, &ver_tlv);

		/* Apply the device specific HCI quirks for TLV based devices
		 *
		 * All TLV based devices support WBS
		 */
		set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED, &hdev->quirks);

		/* Setup MSFT Extension support */
		btintel_set_msft_opcode(hdev,
					INTEL_HW_VARIANT(ver_tlv.cnvi_bt));
		btintel_set_dsm_reset_method(hdev, &ver_tlv);

		err = btintel_bootloader_setup_tlv(hdev, &ver_tlv);
		if (err)
			goto exit_error;

		btintel_register_devcoredump_support(hdev);
		btintel_print_fseq_info(hdev);
		break;
	default:
		bt_dev_err(hdev, "Unsupported Intel hw variant (%u)",
			   INTEL_HW_VARIANT(ver_tlv.cnvi_bt));
		err = -EINVAL;
		break;
	}

	hci_set_hw_info(hdev, "INTEL platform=%u variant=%u",
			INTEL_HW_PLATFORM(ver_tlv.cnvi_bt),
			INTEL_HW_VARIANT(ver_tlv.cnvi_bt));

exit_error:
	kfree_skb(skb);

	return err;
}

int btintel_shutdown_combined(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	int ret;

	/* Send HCI Reset to the controller to stop any BT activity which
	 * were triggered. This will help to save power and maintain the
	 * sync b/w Host and controller
	 */
	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "HCI reset during shutdown failed");
		return PTR_ERR(skb);
	}
	kfree_skb(skb);


	/* Some platforms have an issue with BT LED when the interface is
	 * down or BT radio is turned off, which takes 5 seconds to BT LED
	 * goes off. As a workaround, sends HCI_Intel_SW_RFKILL to put the
	 * device in the RFKILL ON state which turns off the BT LED immediately.
	 */
	if (btintel_test_flag(hdev, INTEL_BROKEN_SHUTDOWN_LED)) {
		skb = __hci_cmd_sync(hdev, 0xfc3f, 0, NULL, HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			ret = PTR_ERR(skb);
			bt_dev_err(hdev, "turning off Intel device LED failed");
			return ret;
		}
		kfree_skb(skb);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_shutdown_combined);

int btintel_configure_setup(struct hci_dev *hdev, const char *driver_name)
{
	hdev->manufacturer = 2;
	hdev->setup = btintel_setup_combined;
	hdev->shutdown = btintel_shutdown_combined;
	hdev->hw_error = btintel_hw_error;
	hdev->set_diag = btintel_set_diag_combined;
	hdev->set_bdaddr = btintel_set_bdaddr;

	coredump_info.driver_name = driver_name;

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_configure_setup);

int btintel_diagnostics(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct intel_tlv *tlv = (void *)&skb->data[5];

	/* The first event is always an event type TLV */
	if (tlv->type != INTEL_TLV_TYPE_ID)
		goto recv_frame;

	switch (tlv->val[0]) {
	case INTEL_TLV_SYSTEM_EXCEPTION:
	case INTEL_TLV_FATAL_EXCEPTION:
	case INTEL_TLV_DEBUG_EXCEPTION:
	case INTEL_TLV_TEST_EXCEPTION:
		/* Generate devcoredump from exception */
		if (!hci_devcd_init(hdev, skb->len)) {
			hci_devcd_append(hdev, skb_clone(skb, GFP_ATOMIC));
			hci_devcd_complete(hdev);
		} else {
			bt_dev_err(hdev, "Failed to generate devcoredump");
		}
	break;
	default:
		bt_dev_err(hdev, "Invalid exception type %02X", tlv->val[0]);
	}

recv_frame:
	return hci_recv_frame(hdev, skb);
}
EXPORT_SYMBOL_GPL(btintel_diagnostics);

int btintel_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (void *)skb->data;
	const char diagnostics_hdr[] = { 0x87, 0x80, 0x03 };

	if (skb->len > HCI_EVENT_HDR_SIZE && hdr->evt == 0xff &&
	    hdr->plen > 0) {
		const void *ptr = skb->data + HCI_EVENT_HDR_SIZE + 1;
		unsigned int len = skb->len - HCI_EVENT_HDR_SIZE - 1;

		if (btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
			switch (skb->data[2]) {
			case 0x02:
				/* When switching to the operational firmware
				 * the device sends a vendor specific event
				 * indicating that the bootup completed.
				 */
				btintel_bootup(hdev, ptr, len);
				kfree_skb(skb);
				return 0;
			case 0x06:
				/* When the firmware loading completes the
				 * device sends out a vendor specific event
				 * indicating the result of the firmware
				 * loading.
				 */
				btintel_secure_send_result(hdev, ptr, len);
				kfree_skb(skb);
				return 0;
			}
		}

		/* Handle all diagnostics events separately. May still call
		 * hci_recv_frame.
		 */
		if (len >= sizeof(diagnostics_hdr) &&
		    memcmp(&skb->data[2], diagnostics_hdr,
			   sizeof(diagnostics_hdr)) == 0) {
			return btintel_diagnostics(hdev, skb);
		}
	}

	return hci_recv_frame(hdev, skb);
}
EXPORT_SYMBOL_GPL(btintel_recv_event);

void btintel_bootup(struct hci_dev *hdev, const void *ptr, unsigned int len)
{
	const struct intel_bootup *evt = ptr;

	if (len != sizeof(*evt))
		return;

	if (btintel_test_and_clear_flag(hdev, INTEL_BOOTING))
		btintel_wake_up_flag(hdev, INTEL_BOOTING);
}
EXPORT_SYMBOL_GPL(btintel_bootup);

void btintel_secure_send_result(struct hci_dev *hdev,
				const void *ptr, unsigned int len)
{
	const struct intel_secure_send_result *evt = ptr;

	if (len != sizeof(*evt))
		return;

	if (evt->result)
		btintel_set_flag(hdev, INTEL_FIRMWARE_FAILED);

	if (btintel_test_and_clear_flag(hdev, INTEL_DOWNLOADING) &&
	    btintel_test_flag(hdev, INTEL_FIRMWARE_LOADED))
		btintel_wake_up_flag(hdev, INTEL_DOWNLOADING);
}
EXPORT_SYMBOL_GPL(btintel_secure_send_result);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth support for Intel devices ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("intel/ibt-11-5.sfi");
MODULE_FIRMWARE("intel/ibt-11-5.ddc");
MODULE_FIRMWARE("intel/ibt-12-16.sfi");
MODULE_FIRMWARE("intel/ibt-12-16.ddc");
