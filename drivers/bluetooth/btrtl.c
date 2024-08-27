// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Bluetooth support for Realtek devices
 *
 *  Copyright (C) 2015 Endless Mobile, Inc.
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>
#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btrtl.h"

#define VERSION "0.1"

#define RTL_CHIP_8723CS_CG	3
#define RTL_CHIP_8723CS_VF	4
#define RTL_CHIP_8723CS_XX	5
#define RTL_EPATCH_SIGNATURE	"Realtech"
#define RTL_EPATCH_SIGNATURE_V2	"RTBTCore"
#define RTL_ROM_LMP_8703B	0x8703
#define RTL_ROM_LMP_8723A	0x1200
#define RTL_ROM_LMP_8723B	0x8723
#define RTL_ROM_LMP_8821A	0x8821
#define RTL_ROM_LMP_8761A	0x8761
#define RTL_ROM_LMP_8822B	0x8822
#define RTL_ROM_LMP_8852A	0x8852
#define RTL_ROM_LMP_8851B	0x8851
#define RTL_CONFIG_MAGIC	0x8723ab55

#define RTL_VSC_OP_COREDUMP	0xfcff

#define IC_MATCH_FL_LMPSUBV	(1 << 0)
#define IC_MATCH_FL_HCIREV	(1 << 1)
#define IC_MATCH_FL_HCIVER	(1 << 2)
#define IC_MATCH_FL_HCIBUS	(1 << 3)
#define IC_MATCH_FL_CHIP_TYPE	(1 << 4)
#define IC_INFO(lmps, hcir, hciv, bus) \
	.match_flags = IC_MATCH_FL_LMPSUBV | IC_MATCH_FL_HCIREV | \
		       IC_MATCH_FL_HCIVER | IC_MATCH_FL_HCIBUS, \
	.lmp_subver = (lmps), \
	.hci_rev = (hcir), \
	.hci_ver = (hciv), \
	.hci_bus = (bus)

#define	RTL_CHIP_SUBVER (&(struct rtl_vendor_cmd) {{0x10, 0x38, 0x04, 0x28, 0x80}})
#define	RTL_CHIP_REV    (&(struct rtl_vendor_cmd) {{0x10, 0x3A, 0x04, 0x28, 0x80}})
#define	RTL_SEC_PROJ    (&(struct rtl_vendor_cmd) {{0x10, 0xA4, 0x0D, 0x00, 0xb0}})

#define RTL_PATCH_SNIPPETS		0x01
#define RTL_PATCH_DUMMY_HEADER		0x02
#define RTL_PATCH_SECURITY_HEADER	0x03

enum btrtl_chip_id {
	CHIP_ID_8723A,
	CHIP_ID_8723B,
	CHIP_ID_8821A,
	CHIP_ID_8761A,
	CHIP_ID_8822B = 8,
	CHIP_ID_8723D,
	CHIP_ID_8821C,
	CHIP_ID_8822C = 13,
	CHIP_ID_8761B,
	CHIP_ID_8852A = 18,
	CHIP_ID_8852B = 20,
	CHIP_ID_8852C = 25,
	CHIP_ID_8851B = 36,
	CHIP_ID_8852BT = 47,
};

struct id_table {
	__u16 match_flags;
	__u16 lmp_subver;
	__u16 hci_rev;
	__u8 hci_ver;
	__u8 hci_bus;
	__u8 chip_type;
	bool config_needed;
	bool has_rom_version;
	bool has_msft_ext;
	char *fw_name;
	char *cfg_name;
	char *hw_info;
};

struct btrtl_device_info {
	const struct id_table *ic_info;
	u8 rom_version;
	u8 *fw_data;
	int fw_len;
	u8 *cfg_data;
	int cfg_len;
	bool drop_fw;
	int project_id;
	u8 key_id;
	struct list_head patch_subsecs;
};

static const struct id_table ic_id_table[] = {
	/* 8723A */
	{ IC_INFO(RTL_ROM_LMP_8723A, 0xb, 0x6, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = false,
	  .fw_name = "rtl_bt/rtl8723a_fw",
	  .cfg_name = NULL,
	  .hw_info = "rtl8723au" },

	/* 8723BS */
	{ IC_INFO(RTL_ROM_LMP_8723B, 0xb, 0x6, HCI_UART),
	  .config_needed = true,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8723bs_fw",
	  .cfg_name = "rtl_bt/rtl8723bs_config",
	  .hw_info  = "rtl8723bs" },

	/* 8723B */
	{ IC_INFO(RTL_ROM_LMP_8723B, 0xb, 0x6, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8723b_fw",
	  .cfg_name = "rtl_bt/rtl8723b_config",
	  .hw_info  = "rtl8723bu" },

	/* 8723CS-CG */
	{ .match_flags = IC_MATCH_FL_LMPSUBV | IC_MATCH_FL_CHIP_TYPE |
			 IC_MATCH_FL_HCIBUS,
	  .lmp_subver = RTL_ROM_LMP_8703B,
	  .chip_type = RTL_CHIP_8723CS_CG,
	  .hci_bus = HCI_UART,
	  .config_needed = true,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8723cs_cg_fw",
	  .cfg_name = "rtl_bt/rtl8723cs_cg_config",
	  .hw_info  = "rtl8723cs-cg" },

	/* 8723CS-VF */
	{ .match_flags = IC_MATCH_FL_LMPSUBV | IC_MATCH_FL_CHIP_TYPE |
			 IC_MATCH_FL_HCIBUS,
	  .lmp_subver = RTL_ROM_LMP_8703B,
	  .chip_type = RTL_CHIP_8723CS_VF,
	  .hci_bus = HCI_UART,
	  .config_needed = true,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8723cs_vf_fw",
	  .cfg_name = "rtl_bt/rtl8723cs_vf_config",
	  .hw_info  = "rtl8723cs-vf" },

	/* 8723CS-XX */
	{ .match_flags = IC_MATCH_FL_LMPSUBV | IC_MATCH_FL_CHIP_TYPE |
			 IC_MATCH_FL_HCIBUS,
	  .lmp_subver = RTL_ROM_LMP_8703B,
	  .chip_type = RTL_CHIP_8723CS_XX,
	  .hci_bus = HCI_UART,
	  .config_needed = true,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8723cs_xx_fw",
	  .cfg_name = "rtl_bt/rtl8723cs_xx_config",
	  .hw_info  = "rtl8723cs" },

	/* 8723D */
	{ IC_INFO(RTL_ROM_LMP_8723B, 0xd, 0x8, HCI_USB),
	  .config_needed = true,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8723d_fw",
	  .cfg_name = "rtl_bt/rtl8723d_config",
	  .hw_info  = "rtl8723du" },

	/* 8723DS */
	{ IC_INFO(RTL_ROM_LMP_8723B, 0xd, 0x8, HCI_UART),
	  .config_needed = true,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8723ds_fw",
	  .cfg_name = "rtl_bt/rtl8723ds_config",
	  .hw_info  = "rtl8723ds" },

	/* 8821A */
	{ IC_INFO(RTL_ROM_LMP_8821A, 0xa, 0x6, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8821a_fw",
	  .cfg_name = "rtl_bt/rtl8821a_config",
	  .hw_info  = "rtl8821au" },

	/* 8821C */
	{ IC_INFO(RTL_ROM_LMP_8821A, 0xc, 0x8, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8821c_fw",
	  .cfg_name = "rtl_bt/rtl8821c_config",
	  .hw_info  = "rtl8821cu" },

	/* 8821CS */
	{ IC_INFO(RTL_ROM_LMP_8821A, 0xc, 0x8, HCI_UART),
	  .config_needed = true,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8821cs_fw",
	  .cfg_name = "rtl_bt/rtl8821cs_config",
	  .hw_info  = "rtl8821cs" },

	/* 8761A */
	{ IC_INFO(RTL_ROM_LMP_8761A, 0xa, 0x6, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8761a_fw",
	  .cfg_name = "rtl_bt/rtl8761a_config",
	  .hw_info  = "rtl8761au" },

	/* 8761B */
	{ IC_INFO(RTL_ROM_LMP_8761A, 0xb, 0xa, HCI_UART),
	  .config_needed = false,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8761b_fw",
	  .cfg_name = "rtl_bt/rtl8761b_config",
	  .hw_info  = "rtl8761btv" },

	/* 8761BU */
	{ IC_INFO(RTL_ROM_LMP_8761A, 0xb, 0xa, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .fw_name  = "rtl_bt/rtl8761bu_fw",
	  .cfg_name = "rtl_bt/rtl8761bu_config",
	  .hw_info  = "rtl8761bu" },

	/* 8822C with UART interface */
	{ IC_INFO(RTL_ROM_LMP_8822B, 0xc, 0x8, HCI_UART),
	  .config_needed = true,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8822cs_fw",
	  .cfg_name = "rtl_bt/rtl8822cs_config",
	  .hw_info  = "rtl8822cs" },

	/* 8822C with UART interface */
	{ IC_INFO(RTL_ROM_LMP_8822B, 0xc, 0xa, HCI_UART),
	  .config_needed = true,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8822cs_fw",
	  .cfg_name = "rtl_bt/rtl8822cs_config",
	  .hw_info  = "rtl8822cs" },

	/* 8822C with USB interface */
	{ IC_INFO(RTL_ROM_LMP_8822B, 0xc, 0xa, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8822cu_fw",
	  .cfg_name = "rtl_bt/rtl8822cu_config",
	  .hw_info  = "rtl8822cu" },

	/* 8822B */
	{ IC_INFO(RTL_ROM_LMP_8822B, 0xb, 0x7, HCI_USB),
	  .config_needed = true,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8822b_fw",
	  .cfg_name = "rtl_bt/rtl8822b_config",
	  .hw_info  = "rtl8822bu" },

	/* 8852A */
	{ IC_INFO(RTL_ROM_LMP_8852A, 0xa, 0xb, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8852au_fw",
	  .cfg_name = "rtl_bt/rtl8852au_config",
	  .hw_info  = "rtl8852au" },

	/* 8852B with UART interface */
	{ IC_INFO(RTL_ROM_LMP_8852A, 0xb, 0xb, HCI_UART),
	  .config_needed = true,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8852bs_fw",
	  .cfg_name = "rtl_bt/rtl8852bs_config",
	  .hw_info  = "rtl8852bs" },

	/* 8852B */
	{ IC_INFO(RTL_ROM_LMP_8852A, 0xb, 0xb, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8852bu_fw",
	  .cfg_name = "rtl_bt/rtl8852bu_config",
	  .hw_info  = "rtl8852bu" },

	/* 8852C */
	{ IC_INFO(RTL_ROM_LMP_8852A, 0xc, 0xc, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8852cu_fw",
	  .cfg_name = "rtl_bt/rtl8852cu_config",
	  .hw_info  = "rtl8852cu" },

	/* 8851B */
	{ IC_INFO(RTL_ROM_LMP_8851B, 0xb, 0xc, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .has_msft_ext = false,
	  .fw_name  = "rtl_bt/rtl8851bu_fw",
	  .cfg_name = "rtl_bt/rtl8851bu_config",
	  .hw_info  = "rtl8851bu" },

	/* 8852BT/8852BE-VT */
	{ IC_INFO(RTL_ROM_LMP_8852A, 0x87, 0xc, HCI_USB),
	  .config_needed = false,
	  .has_rom_version = true,
	  .has_msft_ext = true,
	  .fw_name  = "rtl_bt/rtl8852btu_fw",
	  .cfg_name = "rtl_bt/rtl8852btu_config",
	  .hw_info  = "rtl8852btu" },
	};

static const struct id_table *btrtl_match_ic(u16 lmp_subver, u16 hci_rev,
					     u8 hci_ver, u8 hci_bus,
					     u8 chip_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ic_id_table); i++) {
		if ((ic_id_table[i].match_flags & IC_MATCH_FL_LMPSUBV) &&
		    (ic_id_table[i].lmp_subver != lmp_subver))
			continue;
		if ((ic_id_table[i].match_flags & IC_MATCH_FL_HCIREV) &&
		    (ic_id_table[i].hci_rev != hci_rev))
			continue;
		if ((ic_id_table[i].match_flags & IC_MATCH_FL_HCIVER) &&
		    (ic_id_table[i].hci_ver != hci_ver))
			continue;
		if ((ic_id_table[i].match_flags & IC_MATCH_FL_HCIBUS) &&
		    (ic_id_table[i].hci_bus != hci_bus))
			continue;
		if ((ic_id_table[i].match_flags & IC_MATCH_FL_CHIP_TYPE) &&
		    (ic_id_table[i].chip_type != chip_type))
			continue;

		break;
	}
	if (i >= ARRAY_SIZE(ic_id_table))
		return NULL;

	return &ic_id_table[i];
}

static struct sk_buff *btrtl_read_local_version(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		rtl_dev_err(hdev, "HCI_OP_READ_LOCAL_VERSION failed (%ld)",
			    PTR_ERR(skb));
		return skb;
	}

	if (skb->len != sizeof(struct hci_rp_read_local_version)) {
		rtl_dev_err(hdev, "HCI_OP_READ_LOCAL_VERSION event length mismatch");
		kfree_skb(skb);
		return ERR_PTR(-EIO);
	}

	return skb;
}

static int rtl_read_rom_version(struct hci_dev *hdev, u8 *version)
{
	struct rtl_rom_version_evt *rom_version;
	struct sk_buff *skb;

	/* Read RTL ROM version command */
	skb = __hci_cmd_sync(hdev, 0xfc6d, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		rtl_dev_err(hdev, "Read ROM version failed (%ld)",
			    PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*rom_version)) {
		rtl_dev_err(hdev, "version event length mismatch");
		kfree_skb(skb);
		return -EIO;
	}

	rom_version = (struct rtl_rom_version_evt *)skb->data;
	rtl_dev_info(hdev, "rom_version status=%x version=%x",
		     rom_version->status, rom_version->version);

	*version = rom_version->version;

	kfree_skb(skb);
	return 0;
}

static int btrtl_vendor_read_reg16(struct hci_dev *hdev,
				   struct rtl_vendor_cmd *cmd, u8 *rp)
{
	struct sk_buff *skb;
	int err = 0;

	skb = __hci_cmd_sync(hdev, 0xfc61, sizeof(*cmd), cmd,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		rtl_dev_err(hdev, "RTL: Read reg16 failed (%d)", err);
		return err;
	}

	if (skb->len != 3 || skb->data[0]) {
		bt_dev_err(hdev, "RTL: Read reg16 length mismatch");
		kfree_skb(skb);
		return -EIO;
	}

	if (rp)
		memcpy(rp, skb->data + 1, 2);

	kfree_skb(skb);

	return 0;
}

static void *rtl_iov_pull_data(struct rtl_iovec *iov, u32 len)
{
	void *data = iov->data;

	if (iov->len < len)
		return NULL;

	iov->data += len;
	iov->len  -= len;

	return data;
}

static void btrtl_insert_ordered_subsec(struct rtl_subsection *node,
					struct btrtl_device_info *btrtl_dev)
{
	struct list_head *pos;
	struct list_head *next;
	struct rtl_subsection *subsec;

	list_for_each_safe(pos, next, &btrtl_dev->patch_subsecs) {
		subsec = list_entry(pos, struct rtl_subsection, list);
		if (subsec->prio >= node->prio)
			break;
	}
	__list_add(&node->list, pos->prev, pos);
}

static int btrtl_parse_section(struct hci_dev *hdev,
			       struct btrtl_device_info *btrtl_dev, u32 opcode,
			       u8 *data, u32 len)
{
	struct rtl_section_hdr *hdr;
	struct rtl_subsection *subsec;
	struct rtl_common_subsec *common_subsec;
	struct rtl_sec_hdr *sec_hdr;
	int i;
	u8 *ptr;
	u16 num_subsecs;
	u32 subsec_len;
	int rc = 0;
	struct rtl_iovec iov = {
		.data = data,
		.len  = len,
	};

	hdr = rtl_iov_pull_data(&iov, sizeof(*hdr));
	if (!hdr)
		return -EINVAL;
	num_subsecs = le16_to_cpu(hdr->num);

	for (i = 0; i < num_subsecs; i++) {
		common_subsec = rtl_iov_pull_data(&iov, sizeof(*common_subsec));
		if (!common_subsec)
			break;
		subsec_len = le32_to_cpu(common_subsec->len);

		rtl_dev_dbg(hdev, "subsec, eco 0x%02x, len %08x",
			    common_subsec->eco, subsec_len);

		ptr = rtl_iov_pull_data(&iov, subsec_len);
		if (!ptr)
			break;

		if (common_subsec->eco != btrtl_dev->rom_version + 1)
			continue;

		switch (opcode) {
		case RTL_PATCH_SECURITY_HEADER:
			sec_hdr = (void *)common_subsec;
			if (sec_hdr->key_id != btrtl_dev->key_id)
				continue;
			break;
		}

		subsec = kzalloc(sizeof(*subsec), GFP_KERNEL);
		if (!subsec)
			return -ENOMEM;
		subsec->opcode = opcode;
		subsec->prio = common_subsec->prio;
		subsec->len  = subsec_len;
		subsec->data = ptr;
		btrtl_insert_ordered_subsec(subsec, btrtl_dev);
		rc  += subsec_len;
	}

	return rc;
}

static int rtlbt_parse_firmware_v2(struct hci_dev *hdev,
				   struct btrtl_device_info *btrtl_dev,
				   unsigned char **_buf)
{
	struct rtl_epatch_header_v2 *hdr;
	int rc;
	u8 reg_val[2];
	u8 key_id;
	u32 num_sections;
	struct rtl_section *section;
	struct rtl_subsection *entry, *tmp;
	u32 section_len;
	u32 opcode;
	int len = 0;
	int i;
	u8 *ptr;
	struct rtl_iovec iov = {
		.data = btrtl_dev->fw_data,
		.len  = btrtl_dev->fw_len - 7, /* Cut the tail */
	};

	rc = btrtl_vendor_read_reg16(hdev, RTL_SEC_PROJ, reg_val);
	if (rc < 0)
		return -EIO;
	key_id = reg_val[0];

	rtl_dev_dbg(hdev, "%s: key id %u", __func__, key_id);

	btrtl_dev->key_id = key_id;

	hdr = rtl_iov_pull_data(&iov, sizeof(*hdr));
	if (!hdr)
		return -EINVAL;
	num_sections = le32_to_cpu(hdr->num_sections);

	rtl_dev_dbg(hdev, "FW version %08x-%08x", *((u32 *)hdr->fw_version),
		    *((u32 *)(hdr->fw_version + 4)));

	for (i = 0; i < num_sections; i++) {
		section = rtl_iov_pull_data(&iov, sizeof(*section));
		if (!section)
			break;
		section_len = le32_to_cpu(section->len);
		opcode      = le32_to_cpu(section->opcode);

		rtl_dev_dbg(hdev, "opcode 0x%04x", section->opcode);

		ptr = rtl_iov_pull_data(&iov, section_len);
		if (!ptr)
			break;

		switch (opcode) {
		case RTL_PATCH_SNIPPETS:
			rc = btrtl_parse_section(hdev, btrtl_dev, opcode,
						 ptr, section_len);
			break;
		case RTL_PATCH_SECURITY_HEADER:
			/* If key_id from chip is zero, ignore all security
			 * headers.
			 */
			if (!key_id)
				break;
			rc = btrtl_parse_section(hdev, btrtl_dev, opcode,
						 ptr, section_len);
			break;
		case RTL_PATCH_DUMMY_HEADER:
			rc = btrtl_parse_section(hdev, btrtl_dev, opcode,
						 ptr, section_len);
			break;
		default:
			rc = 0;
			break;
		}
		if (rc < 0) {
			rtl_dev_err(hdev, "RTL: Parse section (%u) err %d",
				    opcode, rc);
			return rc;
		}
		len += rc;
	}

	if (!len)
		return -ENODATA;

	/* Allocate mem and copy all found subsecs. */
	ptr = kvmalloc(len, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	len = 0;
	list_for_each_entry_safe(entry, tmp, &btrtl_dev->patch_subsecs, list) {
		rtl_dev_dbg(hdev, "RTL: opcode %08x, addr %p, len 0x%x",
			    entry->opcode, entry->data, entry->len);
		memcpy(ptr + len, entry->data, entry->len);
		len += entry->len;
	}

	if (!len)
		return -EPERM;

	*_buf = ptr;
	return len;
}

static int rtlbt_parse_firmware(struct hci_dev *hdev,
				struct btrtl_device_info *btrtl_dev,
				unsigned char **_buf)
{
	static const u8 extension_sig[] = { 0x51, 0x04, 0xfd, 0x77 };
	struct btrealtek_data *coredump_info = hci_get_priv(hdev);
	struct rtl_epatch_header *epatch_info;
	unsigned char *buf;
	int i, len;
	size_t min_size;
	u8 opcode, length, data;
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
		{ RTL_ROM_LMP_8703B, 7 },
		{ RTL_ROM_LMP_8822B, 8 },
		{ RTL_ROM_LMP_8723B, 9 },	/* 8723D */
		{ RTL_ROM_LMP_8821A, 10 },	/* 8821C */
		{ RTL_ROM_LMP_8822B, 13 },	/* 8822C */
		{ RTL_ROM_LMP_8761A, 14 },	/* 8761B */
		{ RTL_ROM_LMP_8852A, 18 },	/* 8852A */
		{ RTL_ROM_LMP_8852A, 20 },	/* 8852B */
		{ RTL_ROM_LMP_8852A, 25 },	/* 8852C */
		{ RTL_ROM_LMP_8851B, 36 },	/* 8851B */
		{ RTL_ROM_LMP_8852A, 47 },	/* 8852BT */
	};

	if (btrtl_dev->fw_len <= 8)
		return -EINVAL;

	if (!memcmp(btrtl_dev->fw_data, RTL_EPATCH_SIGNATURE, 8))
		min_size = sizeof(struct rtl_epatch_header) +
				sizeof(extension_sig) + 3;
	else if (!memcmp(btrtl_dev->fw_data, RTL_EPATCH_SIGNATURE_V2, 8))
		min_size = sizeof(struct rtl_epatch_header_v2) +
				sizeof(extension_sig) + 3;
	else
		return -EINVAL;

	if (btrtl_dev->fw_len < min_size)
		return -EINVAL;

	fwptr = btrtl_dev->fw_data + btrtl_dev->fw_len - sizeof(extension_sig);
	if (memcmp(fwptr, extension_sig, sizeof(extension_sig)) != 0) {
		rtl_dev_err(hdev, "extension section signature mismatch");
		return -EINVAL;
	}

	/* Loop from the end of the firmware parsing instructions, until
	 * we find an instruction that identifies the "project ID" for the
	 * hardware supported by this firwmare file.
	 * Once we have that, we double-check that project_id is suitable
	 * for the hardware we are working with.
	 */
	while (fwptr >= btrtl_dev->fw_data + (sizeof(*epatch_info) + 3)) {
		opcode = *--fwptr;
		length = *--fwptr;
		data = *--fwptr;

		BT_DBG("check op=%x len=%x data=%x", opcode, length, data);

		if (opcode == 0xff) /* EOF */
			break;

		if (length == 0) {
			rtl_dev_err(hdev, "found instruction with length 0");
			return -EINVAL;
		}

		if (opcode == 0 && length == 1) {
			project_id = data;
			break;
		}

		fwptr -= length;
	}

	if (project_id < 0) {
		rtl_dev_err(hdev, "failed to find version instruction");
		return -EINVAL;
	}

	/* Find project_id in table */
	for (i = 0; i < ARRAY_SIZE(project_id_to_lmp_subver); i++) {
		if (project_id == project_id_to_lmp_subver[i].id) {
			btrtl_dev->project_id = project_id;
			break;
		}
	}

	if (i >= ARRAY_SIZE(project_id_to_lmp_subver)) {
		rtl_dev_err(hdev, "unknown project id %d", project_id);
		return -EINVAL;
	}

	if (btrtl_dev->ic_info->lmp_subver !=
				project_id_to_lmp_subver[i].lmp_subver) {
		rtl_dev_err(hdev, "firmware is for %x but this is a %x",
			    project_id_to_lmp_subver[i].lmp_subver,
			    btrtl_dev->ic_info->lmp_subver);
		return -EINVAL;
	}

	if (memcmp(btrtl_dev->fw_data, RTL_EPATCH_SIGNATURE, 8) != 0) {
		if (!memcmp(btrtl_dev->fw_data, RTL_EPATCH_SIGNATURE_V2, 8))
			return rtlbt_parse_firmware_v2(hdev, btrtl_dev, _buf);
		rtl_dev_err(hdev, "bad EPATCH signature");
		return -EINVAL;
	}

	epatch_info = (struct rtl_epatch_header *)btrtl_dev->fw_data;
	num_patches = le16_to_cpu(epatch_info->num_patches);

	BT_DBG("fw_version=%x, num_patches=%d",
	       le32_to_cpu(epatch_info->fw_version), num_patches);
	coredump_info->rtl_dump.fw_version = le32_to_cpu(epatch_info->fw_version);

	/* After the rtl_epatch_header there is a funky patch metadata section.
	 * Assuming 2 patches, the layout is:
	 * ChipID1 ChipID2 PatchLength1 PatchLength2 PatchOffset1 PatchOffset2
	 *
	 * Find the right patch for this chip.
	 */
	min_size += 8 * num_patches;
	if (btrtl_dev->fw_len < min_size)
		return -EINVAL;

	chip_id_base = btrtl_dev->fw_data + sizeof(struct rtl_epatch_header);
	patch_length_base = chip_id_base + (sizeof(u16) * num_patches);
	patch_offset_base = patch_length_base + (sizeof(u16) * num_patches);
	for (i = 0; i < num_patches; i++) {
		u16 chip_id = get_unaligned_le16(chip_id_base +
						 (i * sizeof(u16)));
		if (chip_id == btrtl_dev->rom_version + 1) {
			patch_length = get_unaligned_le16(patch_length_base +
							  (i * sizeof(u16)));
			patch_offset = get_unaligned_le32(patch_offset_base +
							  (i * sizeof(u32)));
			break;
		}
	}

	if (!patch_offset) {
		rtl_dev_err(hdev, "didn't find patch for chip id %d",
			    btrtl_dev->rom_version);
		return -EINVAL;
	}

	BT_DBG("length=%x offset=%x index %d", patch_length, patch_offset, i);
	min_size = patch_offset + patch_length;
	if (btrtl_dev->fw_len < min_size)
		return -EINVAL;

	/* Copy the firmware into a new buffer and write the version at
	 * the end.
	 */
	len = patch_length;
	buf = kvmalloc(patch_length, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, btrtl_dev->fw_data + patch_offset, patch_length - 4);
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
	int j = 0;
	struct sk_buff *skb;
	struct hci_rp_read_local_version *rp;

	dl_cmd = kmalloc(sizeof(*dl_cmd), GFP_KERNEL);
	if (!dl_cmd)
		return -ENOMEM;

	for (i = 0; i < frag_num; i++) {
		struct sk_buff *skb;

		dl_cmd->index = j++;
		if (dl_cmd->index == 0x7f)
			j = 1;

		if (i == (frag_num - 1)) {
			dl_cmd->index |= 0x80; /* data end */
			frag_len = fw_len % RTL_FRAG_LEN;
		}
		rtl_dev_dbg(hdev, "download fw (%d/%d). index = %d", i,
				frag_num, dl_cmd->index);
		memcpy(dl_cmd->data, data, frag_len);

		/* Send download command */
		skb = __hci_cmd_sync(hdev, 0xfc20, frag_len + 1, dl_cmd,
				     HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			rtl_dev_err(hdev, "download fw command failed (%ld)",
				    PTR_ERR(skb));
			ret = PTR_ERR(skb);
			goto out;
		}

		if (skb->len != sizeof(struct rtl_download_response)) {
			rtl_dev_err(hdev, "download fw event length mismatch");
			kfree_skb(skb);
			ret = -EIO;
			goto out;
		}

		kfree_skb(skb);
		data += RTL_FRAG_LEN;
	}

	skb = btrtl_read_local_version(hdev);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		rtl_dev_err(hdev, "read local version failed");
		goto out;
	}

	rp = (struct hci_rp_read_local_version *)skb->data;
	rtl_dev_info(hdev, "fw version 0x%04x%04x",
		     __le16_to_cpu(rp->hci_rev), __le16_to_cpu(rp->lmp_subver));
	kfree_skb(skb);

out:
	kfree(dl_cmd);
	return ret;
}

static int rtl_load_file(struct hci_dev *hdev, const char *name, u8 **buff)
{
	const struct firmware *fw;
	int ret;

	rtl_dev_info(hdev, "loading %s", name);
	ret = request_firmware(&fw, name, &hdev->dev);
	if (ret < 0)
		return ret;
	ret = fw->size;
	*buff = kvmalloc(fw->size, GFP_KERNEL);
	if (*buff)
		memcpy(*buff, fw->data, ret);
	else
		ret = -ENOMEM;

	release_firmware(fw);

	return ret;
}

static int btrtl_setup_rtl8723a(struct hci_dev *hdev,
				struct btrtl_device_info *btrtl_dev)
{
	if (btrtl_dev->fw_len < 8)
		return -EINVAL;

	/* Check that the firmware doesn't have the epatch signature
	 * (which is only for RTL8723B and newer).
	 */
	if (!memcmp(btrtl_dev->fw_data, RTL_EPATCH_SIGNATURE, 8)) {
		rtl_dev_err(hdev, "unexpected EPATCH signature!");
		return -EINVAL;
	}

	return rtl_download_firmware(hdev, btrtl_dev->fw_data,
				     btrtl_dev->fw_len);
}

static int btrtl_setup_rtl8723b(struct hci_dev *hdev,
				struct btrtl_device_info *btrtl_dev)
{
	unsigned char *fw_data = NULL;
	int ret;
	u8 *tbuff;

	ret = rtlbt_parse_firmware(hdev, btrtl_dev, &fw_data);
	if (ret < 0)
		goto out;

	if (btrtl_dev->cfg_len > 0) {
		tbuff = kvzalloc(ret + btrtl_dev->cfg_len, GFP_KERNEL);
		if (!tbuff) {
			ret = -ENOMEM;
			goto out;
		}

		memcpy(tbuff, fw_data, ret);
		kvfree(fw_data);

		memcpy(tbuff + ret, btrtl_dev->cfg_data, btrtl_dev->cfg_len);
		ret += btrtl_dev->cfg_len;

		fw_data = tbuff;
	}

	rtl_dev_info(hdev, "cfg_sz %d, total sz %d", btrtl_dev->cfg_len, ret);

	ret = rtl_download_firmware(hdev, fw_data, ret);

out:
	kvfree(fw_data);
	return ret;
}

static void btrtl_coredump(struct hci_dev *hdev)
{
	static const u8 param[] = { 0x00, 0x00 };

	__hci_cmd_send(hdev, RTL_VSC_OP_COREDUMP, sizeof(param), param);
}

static void btrtl_dmp_hdr(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btrealtek_data *coredump_info = hci_get_priv(hdev);
	char buf[80];

	if (coredump_info->rtl_dump.controller)
		snprintf(buf, sizeof(buf), "Controller Name: %s\n",
			 coredump_info->rtl_dump.controller);
	else
		snprintf(buf, sizeof(buf), "Controller Name: Unknown\n");
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Firmware Version: 0x%X\n",
		 coredump_info->rtl_dump.fw_version);
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Driver: %s\n", coredump_info->rtl_dump.driver_name);
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Vendor: Realtek\n");
	skb_put_data(skb, buf, strlen(buf));
}

static void btrtl_register_devcoredump_support(struct hci_dev *hdev)
{
	hci_devcd_register(hdev, btrtl_coredump, btrtl_dmp_hdr, NULL);

}

void btrtl_set_driver_name(struct hci_dev *hdev, const char *driver_name)
{
	struct btrealtek_data *coredump_info = hci_get_priv(hdev);

	coredump_info->rtl_dump.driver_name = driver_name;
}
EXPORT_SYMBOL_GPL(btrtl_set_driver_name);

static bool rtl_has_chip_type(u16 lmp_subver)
{
	switch (lmp_subver) {
	case RTL_ROM_LMP_8703B:
		return true;
	default:
		break;
	}

	return  false;
}

static int rtl_read_chip_type(struct hci_dev *hdev, u8 *type)
{
	struct rtl_chip_type_evt *chip_type;
	struct sk_buff *skb;
	const unsigned char cmd_buf[] = {0x00, 0x94, 0xa0, 0x00, 0xb0};

	/* Read RTL chip type command */
	skb = __hci_cmd_sync(hdev, 0xfc61, 5, cmd_buf, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		rtl_dev_err(hdev, "Read chip type failed (%ld)",
			    PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	chip_type = skb_pull_data(skb, sizeof(*chip_type));
	if (!chip_type) {
		rtl_dev_err(hdev, "RTL chip type event length mismatch");
		kfree_skb(skb);
		return -EIO;
	}

	rtl_dev_info(hdev, "chip_type status=%x type=%x",
		     chip_type->status, chip_type->type);

	*type = chip_type->type & 0x0f;

	kfree_skb(skb);
	return 0;
}

void btrtl_free(struct btrtl_device_info *btrtl_dev)
{
	struct rtl_subsection *entry, *tmp;

	kvfree(btrtl_dev->fw_data);
	kvfree(btrtl_dev->cfg_data);

	list_for_each_entry_safe(entry, tmp, &btrtl_dev->patch_subsecs, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	kfree(btrtl_dev);
}
EXPORT_SYMBOL_GPL(btrtl_free);

struct btrtl_device_info *btrtl_initialize(struct hci_dev *hdev,
					   const char *postfix)
{
	struct btrealtek_data *coredump_info = hci_get_priv(hdev);
	struct btrtl_device_info *btrtl_dev;
	struct sk_buff *skb;
	struct hci_rp_read_local_version *resp;
	struct hci_command_hdr *cmd;
	char fw_name[40];
	char cfg_name[40];
	u16 hci_rev, lmp_subver;
	u8 hci_ver, lmp_ver, chip_type = 0;
	int ret;
	u8 reg_val[2];

	btrtl_dev = kzalloc(sizeof(*btrtl_dev), GFP_KERNEL);
	if (!btrtl_dev) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	INIT_LIST_HEAD(&btrtl_dev->patch_subsecs);

check_version:
	ret = btrtl_vendor_read_reg16(hdev, RTL_CHIP_SUBVER, reg_val);
	if (ret < 0)
		goto err_free;
	lmp_subver = get_unaligned_le16(reg_val);

	if (lmp_subver == RTL_ROM_LMP_8822B) {
		ret = btrtl_vendor_read_reg16(hdev, RTL_CHIP_REV, reg_val);
		if (ret < 0)
			goto err_free;
		hci_rev = get_unaligned_le16(reg_val);

		/* 8822E */
		if (hci_rev == 0x000e) {
			hci_ver = 0x0c;
			lmp_ver = 0x0c;
			btrtl_dev->ic_info = btrtl_match_ic(lmp_subver, hci_rev,
							    hci_ver, hdev->bus,
							    chip_type);
			goto next;
		}
	}

	skb = btrtl_read_local_version(hdev);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		goto err_free;
	}

	resp = (struct hci_rp_read_local_version *)skb->data;

	hci_ver    = resp->hci_ver;
	hci_rev    = le16_to_cpu(resp->hci_rev);
	lmp_ver    = resp->lmp_ver;
	lmp_subver = le16_to_cpu(resp->lmp_subver);

	kfree_skb(skb);

	if (rtl_has_chip_type(lmp_subver)) {
		ret = rtl_read_chip_type(hdev, &chip_type);
		if (ret)
			goto err_free;
	}

	btrtl_dev->ic_info = btrtl_match_ic(lmp_subver, hci_rev, hci_ver,
					    hdev->bus, chip_type);

next:
	rtl_dev_info(hdev, "examining hci_ver=%02x hci_rev=%04x lmp_ver=%02x lmp_subver=%04x",
		     hci_ver, hci_rev,
		     lmp_ver, lmp_subver);

	if (!btrtl_dev->ic_info && !btrtl_dev->drop_fw)
		btrtl_dev->drop_fw = true;
	else
		btrtl_dev->drop_fw = false;

	if (btrtl_dev->drop_fw) {
		skb = bt_skb_alloc(sizeof(*cmd), GFP_KERNEL);
		if (!skb)
			goto err_free;

		cmd = skb_put(skb, HCI_COMMAND_HDR_SIZE);
		cmd->opcode = cpu_to_le16(0xfc66);
		cmd->plen = 0;

		hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;

		ret = hdev->send(hdev, skb);
		if (ret < 0) {
			bt_dev_err(hdev, "sending frame failed (%d)", ret);
			kfree_skb(skb);
			goto err_free;
		}

		/* Ensure the above vendor command is sent to controller and
		 * process has done.
		 */
		msleep(200);

		goto check_version;
	}

	if (!btrtl_dev->ic_info) {
		rtl_dev_info(hdev, "unknown IC info, lmp subver %04x, hci rev %04x, hci ver %04x",
			    lmp_subver, hci_rev, hci_ver);
		return btrtl_dev;
	}

	if (btrtl_dev->ic_info->has_rom_version) {
		ret = rtl_read_rom_version(hdev, &btrtl_dev->rom_version);
		if (ret)
			goto err_free;
	}

	if (!btrtl_dev->ic_info->fw_name) {
		ret = -ENOMEM;
		goto err_free;
	}

	btrtl_dev->fw_len = -EIO;
	if (lmp_subver == RTL_ROM_LMP_8852A && hci_rev == 0x000c) {
		snprintf(fw_name, sizeof(fw_name), "%s_v2.bin",
				btrtl_dev->ic_info->fw_name);
		btrtl_dev->fw_len = rtl_load_file(hdev, fw_name,
				&btrtl_dev->fw_data);
	}

	if (btrtl_dev->fw_len < 0) {
		snprintf(fw_name, sizeof(fw_name), "%s.bin",
				btrtl_dev->ic_info->fw_name);
		btrtl_dev->fw_len = rtl_load_file(hdev, fw_name,
				&btrtl_dev->fw_data);
	}

	if (btrtl_dev->fw_len < 0) {
		rtl_dev_err(hdev, "firmware file %s not found",
			    btrtl_dev->ic_info->fw_name);
		ret = btrtl_dev->fw_len;
		goto err_free;
	}

	if (btrtl_dev->ic_info->cfg_name) {
		if (postfix) {
			snprintf(cfg_name, sizeof(cfg_name), "%s-%s.bin",
				 btrtl_dev->ic_info->cfg_name, postfix);
		} else {
			snprintf(cfg_name, sizeof(cfg_name), "%s.bin",
				 btrtl_dev->ic_info->cfg_name);
		}
		btrtl_dev->cfg_len = rtl_load_file(hdev, cfg_name,
						   &btrtl_dev->cfg_data);
		if (btrtl_dev->ic_info->config_needed &&
		    btrtl_dev->cfg_len <= 0) {
			rtl_dev_err(hdev, "mandatory config file %s not found",
				    btrtl_dev->ic_info->cfg_name);
			ret = btrtl_dev->cfg_len;
			goto err_free;
		}
	}

	/* The following chips supports the Microsoft vendor extension,
	 * therefore set the corresponding VsMsftOpCode.
	 */
	if (btrtl_dev->ic_info->has_msft_ext)
		hci_set_msft_opcode(hdev, 0xFCF0);

	if (btrtl_dev->ic_info)
		coredump_info->rtl_dump.controller = btrtl_dev->ic_info->hw_info;

	return btrtl_dev;

err_free:
	btrtl_free(btrtl_dev);
err_alloc:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(btrtl_initialize);

int btrtl_download_firmware(struct hci_dev *hdev,
			    struct btrtl_device_info *btrtl_dev)
{
	int err = 0;

	/* Match a set of subver values that correspond to stock firmware,
	 * which is not compatible with standard btusb.
	 * If matched, upload an alternative firmware that does conform to
	 * standard btusb. Once that firmware is uploaded, the subver changes
	 * to a different value.
	 */
	if (!btrtl_dev->ic_info) {
		rtl_dev_info(hdev, "assuming no firmware upload needed");
		err = 0;
		goto done;
	}

	switch (btrtl_dev->ic_info->lmp_subver) {
	case RTL_ROM_LMP_8723A:
		err = btrtl_setup_rtl8723a(hdev, btrtl_dev);
		break;
	case RTL_ROM_LMP_8723B:
	case RTL_ROM_LMP_8821A:
	case RTL_ROM_LMP_8761A:
	case RTL_ROM_LMP_8822B:
	case RTL_ROM_LMP_8852A:
	case RTL_ROM_LMP_8703B:
	case RTL_ROM_LMP_8851B:
		err = btrtl_setup_rtl8723b(hdev, btrtl_dev);
		break;
	default:
		rtl_dev_info(hdev, "assuming no firmware upload needed");
		break;
	}

done:
	btrtl_register_devcoredump_support(hdev);

	return err;
}
EXPORT_SYMBOL_GPL(btrtl_download_firmware);

void btrtl_set_quirks(struct hci_dev *hdev, struct btrtl_device_info *btrtl_dev)
{
	/* Enable controller to do both LE scan and BR/EDR inquiry
	 * simultaneously.
	 */
	set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);

	/* Enable central-peripheral role (able to create new connections with
	 * an existing connection in slave role).
	 */
	/* Enable WBS supported for the specific Realtek devices. */
	switch (btrtl_dev->project_id) {
	case CHIP_ID_8822C:
	case CHIP_ID_8852A:
	case CHIP_ID_8852B:
	case CHIP_ID_8852C:
	case CHIP_ID_8851B:
	case CHIP_ID_8852BT:
		set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED, &hdev->quirks);

		/* RTL8852C needs to transmit mSBC data continuously without
		 * the zero length of USB packets for the ALT 6 supported chips
		 */
		if (btrtl_dev->project_id == CHIP_ID_8852C)
			btrealtek_set_flag(hdev, REALTEK_ALT6_CONTINUOUS_TX_CHIP);

		if (btrtl_dev->project_id == CHIP_ID_8852A ||
		    btrtl_dev->project_id == CHIP_ID_8852C)
			set_bit(HCI_QUIRK_USE_MSFT_EXT_ADDRESS_FILTER, &hdev->quirks);

		hci_set_aosp_capable(hdev);
		break;
	default:
		rtl_dev_dbg(hdev, "Central-peripheral role not enabled.");
		rtl_dev_dbg(hdev, "WBS supported not enabled.");
		break;
	}

	if (!btrtl_dev->ic_info)
		return;

	switch (btrtl_dev->ic_info->lmp_subver) {
	case RTL_ROM_LMP_8703B:
		/* 8723CS reports two pages for local ext features,
		 * but it doesn't support any features from page 2 -
		 * it either responds with garbage or with error status
		 */
		set_bit(HCI_QUIRK_BROKEN_LOCAL_EXT_FEATURES_PAGE_2,
			&hdev->quirks);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(btrtl_set_quirks);

int btrtl_setup_realtek(struct hci_dev *hdev)
{
	struct btrtl_device_info *btrtl_dev;
	int ret;

	btrtl_dev = btrtl_initialize(hdev, NULL);
	if (IS_ERR(btrtl_dev))
		return PTR_ERR(btrtl_dev);

	ret = btrtl_download_firmware(hdev, btrtl_dev);

	btrtl_set_quirks(hdev, btrtl_dev);

	hci_set_hw_info(hdev,
			"RTL lmp_subver=%u hci_rev=%u hci_ver=%u hci_bus=%u",
			btrtl_dev->ic_info->lmp_subver,
			btrtl_dev->ic_info->hci_rev,
			btrtl_dev->ic_info->hci_ver,
			btrtl_dev->ic_info->hci_bus);

	btrtl_free(btrtl_dev);
	return ret;
}
EXPORT_SYMBOL_GPL(btrtl_setup_realtek);

int btrtl_shutdown_realtek(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	int ret;

	/* According to the vendor driver, BT must be reset on close to avoid
	 * firmware crash.
	 */
	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		bt_dev_err(hdev, "HCI reset during shutdown failed");
		return ret;
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btrtl_shutdown_realtek);

static unsigned int btrtl_convert_baudrate(u32 device_baudrate)
{
	switch (device_baudrate) {
	case 0x0252a00a:
		return 230400;

	case 0x05f75004:
		return 921600;

	case 0x00005004:
		return 1000000;

	case 0x04928002:
	case 0x01128002:
		return 1500000;

	case 0x00005002:
		return 2000000;

	case 0x0000b001:
		return 2500000;

	case 0x04928001:
		return 3000000;

	case 0x052a6001:
		return 3500000;

	case 0x00005001:
		return 4000000;

	case 0x0252c014:
	default:
		return 115200;
	}
}

int btrtl_get_uart_settings(struct hci_dev *hdev,
			    struct btrtl_device_info *btrtl_dev,
			    unsigned int *controller_baudrate,
			    u32 *device_baudrate, bool *flow_control)
{
	struct rtl_vendor_config *config;
	struct rtl_vendor_config_entry *entry;
	int i, total_data_len;
	bool found = false;

	total_data_len = btrtl_dev->cfg_len - sizeof(*config);
	if (total_data_len <= 0) {
		rtl_dev_warn(hdev, "no config loaded");
		return -EINVAL;
	}

	config = (struct rtl_vendor_config *)btrtl_dev->cfg_data;
	if (le32_to_cpu(config->signature) != RTL_CONFIG_MAGIC) {
		rtl_dev_err(hdev, "invalid config magic");
		return -EINVAL;
	}

	if (total_data_len < le16_to_cpu(config->total_len)) {
		rtl_dev_err(hdev, "config is too short");
		return -EINVAL;
	}

	for (i = 0; i < total_data_len; ) {
		entry = ((void *)config->entry) + i;

		switch (le16_to_cpu(entry->offset)) {
		case 0xc:
			if (entry->len < sizeof(*device_baudrate)) {
				rtl_dev_err(hdev, "invalid UART config entry");
				return -EINVAL;
			}

			*device_baudrate = get_unaligned_le32(entry->data);
			*controller_baudrate = btrtl_convert_baudrate(
							*device_baudrate);

			if (entry->len >= 13)
				*flow_control = !!(entry->data[12] & BIT(2));
			else
				*flow_control = false;

			found = true;
			break;

		default:
			rtl_dev_dbg(hdev, "skipping config entry 0x%x (len %u)",
				   le16_to_cpu(entry->offset), entry->len);
			break;
		}

		i += sizeof(*entry) + entry->len;
	}

	if (!found) {
		rtl_dev_err(hdev, "no UART config entry found");
		return -ENOENT;
	}

	rtl_dev_dbg(hdev, "device baudrate = 0x%08x", *device_baudrate);
	rtl_dev_dbg(hdev, "controller baudrate = %u", *controller_baudrate);
	rtl_dev_dbg(hdev, "flow control %d", *flow_control);

	return 0;
}
EXPORT_SYMBOL_GPL(btrtl_get_uart_settings);

MODULE_AUTHOR("Daniel Drake <drake@endlessm.com>");
MODULE_DESCRIPTION("Bluetooth support for Realtek devices ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("rtl_bt/rtl8723a_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723b_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723b_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723bs_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723bs_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723cs_cg_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723cs_cg_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723cs_vf_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723cs_vf_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723cs_xx_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723cs_xx_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723d_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723d_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723ds_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8723ds_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8761a_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8761a_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8761b_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8761b_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8761bu_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8761bu_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8821a_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8821a_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8821c_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8821c_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8821cs_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8821cs_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8822b_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8822b_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8822cs_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8822cs_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8822cu_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8822cu_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8851bu_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8851bu_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852au_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852au_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852bs_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852bs_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852bu_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852bu_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852btu_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852btu_config.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852cu_fw.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852cu_fw_v2.bin");
MODULE_FIRMWARE("rtl_bt/rtl8852cu_config.bin");
