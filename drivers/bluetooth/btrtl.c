/*
 *  Bluetooth support for Realtek devices
 *
 *  Copyright (C) 2015 Endless Mobile, Inc.
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
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>
#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btrtl.h"

#define VERSION "0.1"

#define RTL_EPATCH_SIGNATURE	"Realtech"
#define RTL_ROM_LMP_3499	0x3499
#define RTL_ROM_LMP_8723A	0x1200
#define RTL_ROM_LMP_8723B	0x8723
#define RTL_ROM_LMP_8821A	0x8821
#define RTL_ROM_LMP_8761A	0x8761
#define RTL_ROM_LMP_8822B	0x8822

static int rtl_read_rom_version(struct hci_dev *hdev, u8 *version)
{
	struct rtl_rom_version_evt *rom_version;
	struct sk_buff *skb;

	/* Read RTL ROM version command */
	skb = __hci_cmd_sync(hdev, 0xfc6d, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: Read ROM version failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*rom_version)) {
		BT_ERR("%s: RTL version event length mismatch", hdev->name);
		kfree_skb(skb);
		return -EIO;
	}

	rom_version = (struct rtl_rom_version_evt *)skb->data;
	BT_INFO("%s: rom_version status=%x version=%x",
		hdev->name, rom_version->status, rom_version->version);

	*version = rom_version->version;

	kfree_skb(skb);
	return 0;
}

static int rtl8723b_parse_firmware(struct hci_dev *hdev, u16 lmp_subver,
				   const struct firmware *fw,
				   unsigned char **_buf)
{
	const u8 extension_sig[] = { 0x51, 0x04, 0xfd, 0x77 };
	struct rtl_epatch_header *epatch_info;
	unsigned char *buf;
	int i, ret, len;
	size_t min_size;
	u8 opcode, length, data, rom_version = 0;
	int project_id = -1;
	const unsigned char *fwptr, *chip_id_base;
	const unsigned char *patch_length_base, *patch_offset_base;
	u32 patch_offset = 0;
	u16 patch_length, num_patches;
	static const struct {
		__u16 lmp_subver;
		__u8 id;
	} project_id_to_lmp_subver[] = {
		{ RTL_ROM_LMP_8723A, 0 },
		{ RTL_ROM_LMP_8723B, 1 },
		{ RTL_ROM_LMP_8821A, 2 },
		{ RTL_ROM_LMP_8761A, 3 },
		{ RTL_ROM_LMP_8822B, 8 },
	};

	ret = rtl_read_rom_version(hdev, &rom_version);
	if (ret)
		return ret;

	min_size = sizeof(struct rtl_epatch_header) + sizeof(extension_sig) + 3;
	if (fw->size < min_size)
		return -EINVAL;

	fwptr = fw->data + fw->size - sizeof(extension_sig);
	if (memcmp(fwptr, extension_sig, sizeof(extension_sig)) != 0) {
		BT_ERR("%s: extension section signature mismatch", hdev->name);
		return -EINVAL;
	}

	/* Loop from the end of the firmware parsing instructions, until
	 * we find an instruction that identifies the "project ID" for the
	 * hardware supported by this firwmare file.
	 * Once we have that, we double-check that that project_id is suitable
	 * for the hardware we are working with.
	 */
	while (fwptr >= fw->data + (sizeof(struct rtl_epatch_header) + 3)) {
		opcode = *--fwptr;
		length = *--fwptr;
		data = *--fwptr;

		BT_DBG("check op=%x len=%x data=%x", opcode, length, data);

		if (opcode == 0xff) /* EOF */
			break;

		if (length == 0) {
			BT_ERR("%s: found instruction with length 0",
			       hdev->name);
			return -EINVAL;
		}

		if (opcode == 0 && length == 1) {
			project_id = data;
			break;
		}

		fwptr -= length;
	}

	if (project_id < 0) {
		BT_ERR("%s: failed to find version instruction", hdev->name);
		return -EINVAL;
	}

	/* Find project_id in table */
	for (i = 0; i < ARRAY_SIZE(project_id_to_lmp_subver); i++) {
		if (project_id == project_id_to_lmp_subver[i].id)
			break;
	}

	if (i >= ARRAY_SIZE(project_id_to_lmp_subver)) {
		BT_ERR("%s: unknown project id %d", hdev->name, project_id);
		return -EINVAL;
	}

	if (lmp_subver != project_id_to_lmp_subver[i].lmp_subver) {
		BT_ERR("%s: firmware is for %x but this is a %x", hdev->name,
		       project_id_to_lmp_subver[i].lmp_subver, lmp_subver);
		return -EINVAL;
	}

	epatch_info = (struct rtl_epatch_header *)fw->data;
	if (memcmp(epatch_info->signature, RTL_EPATCH_SIGNATURE, 8) != 0) {
		BT_ERR("%s: bad EPATCH signature", hdev->name);
		return -EINVAL;
	}

	num_patches = le16_to_cpu(epatch_info->num_patches);
	BT_DBG("fw_version=%x, num_patches=%d",
	       le32_to_cpu(epatch_info->fw_version), num_patches);

	/* After the rtl_epatch_header there is a funky patch metadata section.
	 * Assuming 2 patches, the layout is:
	 * ChipID1 ChipID2 PatchLength1 PatchLength2 PatchOffset1 PatchOffset2
	 *
	 * Find the right patch for this chip.
	 */
	min_size += 8 * num_patches;
	if (fw->size < min_size)
		return -EINVAL;

	chip_id_base = fw->data + sizeof(struct rtl_epatch_header);
	patch_length_base = chip_id_base + (sizeof(u16) * num_patches);
	patch_offset_base = patch_length_base + (sizeof(u16) * num_patches);
	for (i = 0; i < num_patches; i++) {
		u16 chip_id = get_unaligned_le16(chip_id_base +
						 (i * sizeof(u16)));
		if (chip_id == rom_version + 1) {
			patch_length = get_unaligned_le16(patch_length_base +
							  (i * sizeof(u16)));
			patch_offset = get_unaligned_le32(patch_offset_base +
							  (i * sizeof(u32)));
			break;
		}
	}

	if (!patch_offset) {
		BT_ERR("%s: didn't find patch for chip id %d",
		       hdev->name, rom_version);
		return -EINVAL;
	}

	BT_DBG("length=%x offset=%x index %d", patch_length, patch_offset, i);
	min_size = patch_offset + patch_length;
	if (fw->size < min_size)
		return -EINVAL;

	/* Copy the firmware into a new buffer and write the version at
	 * the end.
	 */
	len = patch_length;
	buf = kmemdup(fw->data + patch_offset, patch_length, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf + patch_length - 4, &epatch_info->fw_version, 4);

	*_buf = buf;
	return len;
}

static int rtl_download_firmware(struct hci_dev *hdev,
				 const unsigned char *data, int fw_len)
{
	struct rtl_download_cmd *dl_cmd;
	int frag_num = fw_len / RTL_FRAG_LEN + 1;
	int frag_len = RTL_FRAG_LEN;
	int ret = 0;
	int i;

	dl_cmd = kmalloc(sizeof(struct rtl_download_cmd), GFP_KERNEL);
	if (!dl_cmd)
		return -ENOMEM;

	for (i = 0; i < frag_num; i++) {
		struct sk_buff *skb;

		BT_DBG("download fw (%d/%d)", i, frag_num);

		dl_cmd->index = i;
		if (i == (frag_num - 1)) {
			dl_cmd->index |= 0x80; /* data end */
			frag_len = fw_len % RTL_FRAG_LEN;
		}
		memcpy(dl_cmd->data, data, frag_len);

		/* Send download command */
		skb = __hci_cmd_sync(hdev, 0xfc20, frag_len + 1, dl_cmd,
				     HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			BT_ERR("%s: download fw command failed (%ld)",
			       hdev->name, PTR_ERR(skb));
			ret = -PTR_ERR(skb);
			goto out;
		}

		if (skb->len != sizeof(struct rtl_download_response)) {
			BT_ERR("%s: download fw event length mismatch",
			       hdev->name);
			kfree_skb(skb);
			ret = -EIO;
			goto out;
		}

		kfree_skb(skb);
		data += RTL_FRAG_LEN;
	}

out:
	kfree(dl_cmd);
	return ret;
}

static int rtl_load_config(struct hci_dev *hdev, const char *name, u8 **buff)
{
	const struct firmware *fw;
	int ret;

	BT_INFO("%s: rtl: loading %s", hdev->name, name);
	ret = request_firmware(&fw, name, &hdev->dev);
	if (ret < 0)
		return ret;
	ret = fw->size;
	*buff = kmemdup(fw->data, ret, GFP_KERNEL);
	if (!*buff)
		ret = -ENOMEM;

	release_firmware(fw);

	return ret;
}

static int btrtl_setup_rtl8723a(struct hci_dev *hdev)
{
	const struct firmware *fw;
	int ret;

	BT_INFO("%s: rtl: loading rtl_bt/rtl8723a_fw.bin", hdev->name);
	ret = request_firmware(&fw, "rtl_bt/rtl8723a_fw.bin", &hdev->dev);
	if (ret < 0) {
		BT_ERR("%s: Failed to load rtl_bt/rtl8723a_fw.bin", hdev->name);
		return ret;
	}

	if (fw->size < 8) {
		ret = -EINVAL;
		goto out;
	}

	/* Check that the firmware doesn't have the epatch signature
	 * (which is only for RTL8723B and newer).
	 */
	if (!memcmp(fw->data, RTL_EPATCH_SIGNATURE, 8)) {
		BT_ERR("%s: unexpected EPATCH signature!", hdev->name);
		ret = -EINVAL;
		goto out;
	}

	ret = rtl_download_firmware(hdev, fw->data, fw->size);

out:
	release_firmware(fw);
	return ret;
}

static int btrtl_setup_rtl8723b(struct hci_dev *hdev, u16 lmp_subver,
				const char *fw_name)
{
	unsigned char *fw_data = NULL;
	const struct firmware *fw;
	int ret;
	int cfg_sz;
	u8 *cfg_buff = NULL;
	u8 *tbuff;
	char *cfg_name = NULL;
	bool config_needed = false;

	switch (lmp_subver) {
	case RTL_ROM_LMP_8723B:
		cfg_name = "rtl_bt/rtl8723b_config.bin";
		break;
	case RTL_ROM_LMP_8821A:
		cfg_name = "rtl_bt/rtl8821a_config.bin";
		break;
	case RTL_ROM_LMP_8761A:
		cfg_name = "rtl_bt/rtl8761a_config.bin";
		break;
	case RTL_ROM_LMP_8822B:
		cfg_name = "rtl_bt/rtl8822b_config.bin";
		config_needed = true;
		break;
	default:
		BT_ERR("%s: rtl: no config according to lmp_subver %04x",
		       hdev->name, lmp_subver);
		break;
	}

	if (cfg_name) {
		cfg_sz = rtl_load_config(hdev, cfg_name, &cfg_buff);
		if (cfg_sz < 0) {
			cfg_sz = 0;
			if (config_needed)
				BT_ERR("Necessary config file %s not found\n",
				       cfg_name);
		}
	} else
		cfg_sz = 0;

	BT_INFO("%s: rtl: loading %s", hdev->name, fw_name);
	ret = request_firmware(&fw, fw_name, &hdev->dev);
	if (ret < 0) {
		BT_ERR("%s: Failed to load %s", hdev->name, fw_name);
		goto err_req_fw;
	}

	ret = rtl8723b_parse_firmware(hdev, lmp_subver, fw, &fw_data);
	if (ret < 0)
		goto out;

	if (cfg_sz) {
		tbuff = kzalloc(ret + cfg_sz, GFP_KERNEL);
		if (!tbuff) {
			ret = -ENOMEM;
			goto out;
		}

		memcpy(tbuff, fw_data, ret);
		kfree(fw_data);

		memcpy(tbuff + ret, cfg_buff, cfg_sz);
		ret += cfg_sz;

		fw_data = tbuff;
	}

	BT_INFO("cfg_sz %d, total size %d", cfg_sz, ret);

	ret = rtl_download_firmware(hdev, fw_data, ret);

out:
	release_firmware(fw);
	kfree(fw_data);
err_req_fw:
	if (cfg_sz)
		kfree(cfg_buff);
	return ret;
}

static struct sk_buff *btrtl_read_local_version(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: HCI_OP_READ_LOCAL_VERSION failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return skb;
	}

	if (skb->len != sizeof(struct hci_rp_read_local_version)) {
		BT_ERR("%s: HCI_OP_READ_LOCAL_VERSION event length mismatch",
		       hdev->name);
		kfree_skb(skb);
		return ERR_PTR(-EIO);
	}

	return skb;
}

int btrtl_setup_realtek(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct hci_rp_read_local_version *resp;
	u16 lmp_subver;

	skb = btrtl_read_local_version(hdev);
	if (IS_ERR(skb))
		return -PTR_ERR(skb);

	resp = (struct hci_rp_read_local_version *)skb->data;
	BT_INFO("%s: rtl: examining hci_ver=%02x hci_rev=%04x lmp_ver=%02x "
		"lmp_subver=%04x", hdev->name, resp->hci_ver, resp->hci_rev,
		resp->lmp_ver, resp->lmp_subver);

	lmp_subver = le16_to_cpu(resp->lmp_subver);
	kfree_skb(skb);

	/* Match a set of subver values that correspond to stock firmware,
	 * which is not compatible with standard btusb.
	 * If matched, upload an alternative firmware that does conform to
	 * standard btusb. Once that firmware is uploaded, the subver changes
	 * to a different value.
	 */
	switch (lmp_subver) {
	case RTL_ROM_LMP_8723A:
	case RTL_ROM_LMP_3499:
		return btrtl_setup_rtl8723a(hdev);
	case RTL_ROM_LMP_8723B:
		return btrtl_setup_rtl8723b(hdev, lmp_subver,
					    "rtl_bt/rtl8723b_fw.bin");
	case RTL_ROM_LMP_8821A:
		return btrtl_setup_rtl8723b(hdev, lmp_subver,
					    "rtl_bt/rtl8821a_fw.bin");
	case RTL_ROM_LMP_8761A:
		return btrtl_setup_rtl8723b(hdev, lmp_subver,
					    "rtl_bt/rtl8761a_fw.bin");
	case RTL_ROM_LMP_8822B:
		return btrtl_setup_rtl8723b(hdev, lmp_subver,
					    "rtl_bt/rtl8822b_fw.bin");
	default:
		BT_INFO("rtl: assuming no firmware upload needed.");
		return 0;
	}
}
EXPORT_SYMBOL_GPL(btrtl_setup_realtek);

MODULE_AUTHOR("Daniel Drake <drake@endlessm.com>");
MODULE_DESCRIPTION("Bluetooth support for Realtek devices ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
