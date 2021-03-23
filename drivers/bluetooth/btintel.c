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
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btintel.h"

#define VERSION "0.1"

#define BDADDR_INTEL		(&(bdaddr_t){{0x00, 0x8b, 0x9e, 0x19, 0x03, 0x00}})
#define RSA_HEADER_LEN		644
#define CSS_HEADER_OFFSET	8
#define ECDSA_OFFSET		644
#define ECDSA_HEADER_LEN	320

#define CMD_WRITE_BOOT_PARAMS	0xfc0e
struct cmd_write_boot_params {
	u32 boot_addr;
	u8  fw_build_num;
	u8  fw_build_ww;
	u8  fw_build_yy;
} __packed;

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

int btintel_set_diag_mfg(struct hci_dev *hdev, bool enable)
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
EXPORT_SYMBOL_GPL(btintel_set_diag_mfg);

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
	case 0x06:
		variant = "Bootloader";
		break;
	case 0x23:
		variant = "Firmware";
		break;
	default:
		bt_dev_err(hdev, "Unsupported firmware variant(%02x)", ver->fw_variant);
		return -EINVAL;
	}

	bt_dev_info(hdev, "%s revision %u.%u build %u week %u %u",
		    variant, ver->fw_revision >> 4, ver->fw_revision & 0x0f,
		    ver->fw_build_num, ver->fw_build_ww,
		    2000 + ver->fw_build_yy);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_version_info);

int btintel_secure_send(struct hci_dev *hdev, u8 fragment_type, u32 plen,
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
EXPORT_SYMBOL_GPL(btintel_secure_send);

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

int btintel_set_event_mask(struct hci_dev *hdev, bool debug)
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
EXPORT_SYMBOL_GPL(btintel_set_event_mask);

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

	if (skb->len != sizeof(*ver)) {
		bt_dev_err(hdev, "Intel version event size mismatch");
		kfree_skb(skb);
		return -EILSEQ;
	}

	memcpy(ver, skb->data, sizeof(*ver));

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_read_version);

int btintel_version_info_tlv(struct hci_dev *hdev, struct intel_version_tlv *version)
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
		break;
	default:
		bt_dev_err(hdev, "Unsupported Intel hardware variant (0x%x)",
			   INTEL_HW_VARIANT(version->cnvi_bt));
		return -EINVAL;
	}

	switch (version->img_type) {
	case 0x01:
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
			    version->secure_boot ? "enabled" : "disabled");
		bt_dev_info(hdev, "OTP lock is %s",
			    version->otp_lock ? "enabled" : "disabled");
		bt_dev_info(hdev, "API lock is %s",
			    version->api_lock ? "enabled" : "disabled");
		bt_dev_info(hdev, "Debug lock is %s",
			    version->debug_lock ? "enabled" : "disabled");
		bt_dev_info(hdev, "Minimum firmware build %u week %u %u",
			    version->min_fw_build_nn, version->min_fw_build_cw,
			    2000 + version->min_fw_build_yy);
		break;
	case 0x03:
		variant = "Firmware";
		break;
	default:
		bt_dev_err(hdev, "Unsupported image type(%02x)", version->img_type);
		return -EINVAL;
	}

	bt_dev_info(hdev, "%s timestamp %u.%u buildtype %u build %u", variant,
		    2000 + (version->timestamp >> 8), version->timestamp & 0xff,
		    version->build_type, version->build_num);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_version_info_tlv);

int btintel_read_version_tlv(struct hci_dev *hdev, struct intel_version_tlv *version)
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

	/* Consume Command Complete Status field */
	skb_pull(skb, 1);

	/* Event parameters contatin multiple TLVs. Read each of them
	 * and only keep the required data. Also, it use existing legacy
	 * version field like hw_platform, hw_variant, and fw_variant
	 * to keep the existing setup flow
	 */
	while (skb->len) {
		struct intel_tlv *tlv;

		tlv = (struct intel_tlv *)skb->data;
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
			version->timestamp = get_unaligned_le16(tlv->val);
			break;
		case INTEL_TLV_BUILD_TYPE:
			version->build_type = tlv->val[0];
			break;
		case INTEL_TLV_BUILD_NUM:
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
			memcpy(&version->otp_bd_addr, tlv->val, tlv->len);
			break;
		default:
			/* Ignore rest of information */
			break;
		}
		/* consume the current tlv and move to next*/
		skb_pull(skb, tlv->len + sizeof(*tlv));
	}

	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL_GPL(btintel_read_version_tlv);

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

static struct regmap_bus regmap_ibt = {
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
		    params->secure_boot ? "enabled" : "disabled");

	bt_dev_info(hdev, "OTP lock is %s",
		    params->otp_lock ? "enabled" : "disabled");

	bt_dev_info(hdev, "API lock is %s",
		    params->api_lock ? "enabled" : "disabled");

	bt_dev_info(hdev, "Debug lock is %s",
		    params->debug_lock ? "enabled" : "disabled");

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
		 * firmware data buffer as a single Data fragement.
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

			bt_dev_info(hdev, "Boot Address: 0x%x",
				    le32_to_cpu(params->boot_addr));

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

int btintel_download_firmware_newgen(struct hci_dev *hdev,
				     struct intel_version_tlv *ver,
				     const struct firmware *fw, u32 *boot_param,
				     u8 hw_variant, u8 sbe_type)
{
	int err;
	u32 css_header_ver;

	/* Skip download if firmware has the same version */
	if (btintel_firmware_version(hdev, ver->min_fw_build_nn,
				     ver->min_fw_build_cw, ver->min_fw_build_yy,
				     fw, boot_param)) {
		bt_dev_info(hdev, "Firmware already loaded");
		/* Return -EALREADY to indicate that firmware has already been
		 * loaded.
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
	if (ver->img_type == 0x03)
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
EXPORT_SYMBOL_GPL(btintel_download_firmware_newgen);

void btintel_reset_to_bootloader(struct hci_dev *hdev)
{
	struct intel_reset params;
	struct sk_buff *skb;

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
EXPORT_SYMBOL_GPL(btintel_reset_to_bootloader);

int btintel_read_debug_features(struct hci_dev *hdev,
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
EXPORT_SYMBOL_GPL(btintel_read_debug_features);

int btintel_set_debug_features(struct hci_dev *hdev,
			       const struct intel_debug_features *features)
{
	u8 mask[11] = { 0x0a, 0x92, 0x02, 0x07, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00 };
	struct sk_buff *skb;

	if (!features)
		return -EINVAL;

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
	return 0;
}
EXPORT_SYMBOL_GPL(btintel_set_debug_features);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth support for Intel devices ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("intel/ibt-11-5.sfi");
MODULE_FIRMWARE("intel/ibt-11-5.ddc");
MODULE_FIRMWARE("intel/ibt-12-16.sfi");
MODULE_FIRMWARE("intel/ibt-12-16.ddc");
