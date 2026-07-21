// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/string_choices.h>
#include <net/mac80211.h>
#include <linux/elf.h>
#include <linux/crc32.h>
#include "fw.h"
#include "mac.h"
#include "bus.h"

/*
 * Maximum wait time (microseconds) for firmware to boot (for host table
 * pointer to be available)
 */
#define HOST_TABLE_PTR_POLL_TIMEOUT_US 1200000
#define HOST_TABLE_PTR_POLL_PERIOD_US 10000

/* Number of times to attempt flashing FW */
#define FW_FLASH_ATTEMPT_COUNT 3

static int mm81x_fw_get_header(const u8 *data, Elf32_Ehdr *ehdr)
{
	const struct mm81x_elf32_ehdr *p =
		(const struct mm81x_elf32_ehdr *)data;

	/* Magic check */
	if (p->e_ident[EI_MAG0] != ELFMAG0 || p->e_ident[EI_MAG1] != ELFMAG1 ||
	    p->e_ident[EI_MAG2] != ELFMAG2 || p->e_ident[EI_MAG3] != ELFMAG3)
		return -EINVAL;

	/* elf32 and little endian */
	if (p->e_ident[EI_DATA] != ELFDATA2LSB ||
	    p->e_ident[EI_CLASS] != ELFCLASS32)
		return -EINVAL;

	ehdr->e_phoff = le32_to_cpu(p->e_phoff);
	ehdr->e_phentsize = le16_to_cpu(p->e_phentsize);
	ehdr->e_phnum = le16_to_cpu(p->e_phnum);
	ehdr->e_shoff = le32_to_cpu(p->e_shoff);
	ehdr->e_shentsize = le16_to_cpu(p->e_shentsize);
	ehdr->e_shnum = le16_to_cpu(p->e_shnum);
	ehdr->e_shstrndx = le16_to_cpu(p->e_shstrndx);
	ehdr->e_entry = le32_to_cpu(p->e_entry);

	return 0;
}

static void mm81x_fw_parse_info(struct mm81x *mors, const u8 *data, int length)
{
	const struct mm81x_fw_info_tlv *tlv =
		(const struct mm81x_fw_info_tlv *)data;

	while ((u8 *)tlv < (data + length)) {
		switch (le16_to_cpu(tlv->type)) {
		case MM81X_FW_INFO_TLV_BCF_ADDR:
			mors->bcf_address = get_unaligned_le32(tlv->val);
			break;
		default:
			break;
		}
		tlv = (const struct mm81x_fw_info_tlv *)((u8 *)tlv +
							 le16_to_cpu(
								 tlv->length) +
							 sizeof(*tlv));
	}
}

static int mm81x_fw_get_section_header(const u8 *data, Elf32_Ehdr *ehdr,
				       Elf32_Shdr *shdr, int i)
{
	const struct mm81x_elf32_shdr *p =
		(void *)(data + ehdr->e_shoff + (i * ehdr->e_shentsize));

	shdr->sh_name = le32_to_cpu(p->sh_name);
	shdr->sh_type = le32_to_cpu(p->sh_type);
	shdr->sh_offset = le32_to_cpu(p->sh_offset);
	shdr->sh_addr = le32_to_cpu(p->sh_addr);
	shdr->sh_size = le32_to_cpu(p->sh_size);
	shdr->sh_flags = le32_to_cpu(p->sh_flags);

	return 0;
}

static int mm81x_fw_set_boot_addr(struct mm81x *mors, uint32_t addr)
{
	int status;

	dev_dbg(mors->dev, "Overwriting boot address to 0x%x", addr);
	mm81x_claim_bus(mors);
	status = mm81x_reg32_write(mors, MM81X_REG_BOOT_ADDR(mors), addr);
	mm81x_release_bus(mors);
	return status;
}

static int mm81x_fw_load_fw(struct mm81x *mors, const struct firmware *fw)
{
	int i;
	int ret = 0;
	Elf32_Ehdr ehdr;
	Elf32_Phdr phdr;
	Elf32_Shdr shdr;
	Elf32_Shdr sh_strtab;
	const char *sh_strs;

	u8 *fw_buf = devm_kmalloc(mors->dev, ROUND_BYTES_TO_WORD(fw->size),
				  GFP_KERNEL);

	if (!fw_buf)
		return -ENOMEM;

	if (mm81x_fw_get_header(fw->data, &ehdr)) {
		dev_err(mors->dev, "Wrong file format");
		return -EINVAL;
	}

	if (mm81x_fw_get_section_header(fw->data, &ehdr, &sh_strtab,
					ehdr.e_shstrndx)) {
		dev_err(mors->dev, "Invalid firmware. Missing string table");
		return -ENOENT;
	}

	sh_strs = (const char *)fw->data + sh_strtab.sh_offset;

	for (i = 0; i < ehdr.e_phnum; i++) {
		int status;
		int address;
		const struct mm81x_elf32_phdr *p =
			(void *)(fw->data + ehdr.e_phoff +
				 i * ehdr.e_phentsize);

		phdr.p_type = le32_to_cpu(p->p_type);
		phdr.p_offset = le32_to_cpu(p->p_offset);
		phdr.p_paddr = le32_to_cpu(p->p_paddr);
		phdr.p_filesz = le32_to_cpu(p->p_filesz);
		phdr.p_memsz = le32_to_cpu(p->p_memsz);

		address = phdr.p_paddr;

		if (phdr.p_type != PT_LOAD || !phdr.p_memsz)
			continue;

		if (phdr.p_filesz && phdr.p_offset &&
		    (phdr.p_offset + phdr.p_filesz) < fw->size) {
			u32 padded_size = ROUND_BYTES_TO_WORD(phdr.p_filesz);

			memcpy(fw_buf, fw->data + phdr.p_offset, padded_size);
			/* Set padding to 0xff */
			memset(fw_buf + phdr.p_filesz, 0xff,
			       padded_size - phdr.p_filesz);
			mm81x_claim_bus(mors);
			status = mm81x_dm_write(mors, address, fw_buf,
						padded_size);
			mm81x_release_bus(mors);
			if (status) {
				ret = -EIO;
				break;
			}
		}
	}

	for (i = 0; i < ehdr.e_shnum; i++) {
		if (mm81x_fw_get_section_header(fw->data, &ehdr, &shdr, i))
			continue;

		/* This is the firmware info. Parse it */
		if (!strncmp(sh_strs + shdr.sh_name, ".fw_info",
			     sizeof(".fw_info")))
			mm81x_fw_parse_info(mors, fw->data + shdr.sh_offset,
					    shdr.sh_size);
	}

	if (ehdr.e_entry)
		ret = mm81x_fw_set_boot_addr(mors, ehdr.e_entry);

	devm_kfree(mors->dev, fw_buf);
	return ret;
}

static int __mm81x_fw_load_bcf(struct mm81x *mors, unsigned int addr,
			       const void *src, size_t src_len, u8 *scratch,
			       size_t scratch_cap)
{
	size_t rounded = ROUND_BYTES_TO_WORD(src_len);
	int st;

	if (rounded > scratch_cap)
		return -EINVAL;
	if (rounded > BCF_DATABASE_SIZE)
		return -EFBIG;

	memcpy(scratch, src, src_len);
	if (rounded > src_len)
		memset(scratch + src_len, 0xff, rounded - src_len);

	mm81x_claim_bus(mors);
	st = mm81x_dm_write(mors, addr, scratch, rounded);
	mm81x_release_bus(mors);

	return st ? -EIO : 0;
}

static int mm81x_fw_load_bcf(struct mm81x *mors, const struct firmware *bcf,
			     unsigned int bcf_address)
{
	int i, ret = 0;
	size_t reg_prefix_len, cfg_len_rounded = 0, reg_len_rounded;
	Elf32_Ehdr ehdr;
	Elf32_Shdr shdr, sh_strtab;
	const char *sh_strs, *reg_prefix = ".regdom_", *reg_src;
	size_t reg_len;
	u8 *bcf_buf;

	bcf_buf = devm_kmalloc(mors->dev, ROUND_BYTES_TO_WORD(bcf->size),
			       GFP_KERNEL);
	if (!bcf_buf)
		return -ENOMEM;

	if (mm81x_fw_get_header(bcf->data, &ehdr)) {
		dev_err(mors->dev, "Wrong file format");
		ret = -EINVAL;
		goto out_free;
	}

	if (mm81x_fw_get_section_header(bcf->data, &ehdr, &sh_strtab,
					ehdr.e_shstrndx)) {
		dev_err(mors->dev, "Invalid BCF - missing string table");
		ret = -ENOENT;
		goto out_free;
	}

	sh_strs = (const char *)bcf->data + sh_strtab.sh_offset;
	reg_prefix_len = strlen(reg_prefix);

	for (i = 0; i < ehdr.e_shnum; i++) {
		if (mm81x_fw_get_section_header(bcf->data, &ehdr, &shdr, i))
			continue;
		if (strcmp(sh_strs + shdr.sh_name, ".board_config"))
			continue;

		cfg_len_rounded = ROUND_BYTES_TO_WORD(shdr.sh_size);
		dev_dbg(mors->dev,
			"Write BCF board_config - addr 0x%x size %zu",
			bcf_address, cfg_len_rounded);

		ret = __mm81x_fw_load_bcf(mors, bcf_address,
					  bcf->data + shdr.sh_offset,
					  shdr.sh_size, bcf_buf,
					  ROUND_BYTES_TO_WORD(bcf->size));
		if (ret)
			goto out_free;

		bcf_address += cfg_len_rounded;
		break;
	}

	ret = -EINVAL;
	for (; i < ehdr.e_shnum; i++) {
		if (mm81x_fw_get_section_header(bcf->data, &ehdr, &shdr, i))
			continue;
		if (strncmp(sh_strs + shdr.sh_name, reg_prefix, reg_prefix_len))
			continue;
		if (strncmp(sh_strs + shdr.sh_name + reg_prefix_len,
			    mors->country, 2))
			continue;

		reg_src = bcf->data + shdr.sh_offset;
		reg_len = shdr.sh_size;
		dev_dbg(mors->dev, "Write BCF %s - addr 0x%x size %zu",
			sh_strs + shdr.sh_name, bcf_address,
			ROUND_BYTES_TO_WORD(reg_len));
		ret = 0;
		break;
	}

	if (ret)
		goto out_free;

	reg_len_rounded = ROUND_BYTES_TO_WORD(reg_len);
	if ((cfg_len_rounded + reg_len_rounded) > BCF_DATABASE_SIZE) {
		ret = -EFBIG;
		goto out_free;
	}

	ret = __mm81x_fw_load_bcf(mors, bcf_address, reg_src, reg_len, bcf_buf,
				  ROUND_BYTES_TO_WORD(bcf->size));

out_free:
	devm_kfree(mors->dev, bcf_buf);
	return ret;
}

static void mm81x_fw_clear_aon(struct mm81x *mors)
{
	int idx;
	u8 count = MM81X_REG_AON_COUNT(mors);
	u32 address = MM81X_REG_AON_ADDR(mors);

	for (idx = 0; idx < count; idx++, address += 4) {
		if (mors->bus_type == MM81X_BUS_TYPE_USB && idx == 0)
			/* Keep the USB power domain enabled in AON. */
			mm81x_reg32_write(mors, address,
					  MM81X_REG_AON_USB_RESET(mors));
		else
			/* clear AON */
			mm81x_reg32_write(mors, address, 0x0);
	}

	mm81x_hw_toggle_aon_latch(mors);
}

static void mm81x_fw_trigger(struct mm81x *mors)
{
	const unsigned int wait_after_msi_trigger_ms = 1;

	mm81x_claim_bus(mors);
	/*
	 * If not coming from a full reset, some AON flags may be latched.
	 * Make sure to clear any hanging AON bits (can affect booting).
	 */
	mm81x_fw_clear_aon(mors);

	if (MM81X_REG_CLK_CTRL(mors))
		mm81x_reg32_write(mors, MM81X_REG_CLK_CTRL(mors),
				  MM81X_REG_CLK_CTRL_VALUE(mors));

	mm81x_reg32_write(mors, MM81X_REG_MSI(mors),
			  MM81X_REG_MSI_HOST_INT(mors));
	mm81x_release_bus(mors);

	/* Give the chip a chance to boot */
	mdelay(wait_after_msi_trigger_ms);
}

static int mm81x_fw_verify_magic(struct mm81x *mors)
{
	int ret = 0;
	int magic = ~MM81X_REG_HOST_MAGIC_VALUE(mors);

	mm81x_claim_bus(mors);
	mm81x_reg32_read(mors,
			 mors->host_table_ptr +
				 offsetof(struct host_table, magic_number),
			 &magic);

	if (magic != MM81X_REG_HOST_MAGIC_VALUE(mors)) {
		dev_err(mors->dev, "FW magic mismatch 0x%08x:0x%08x",
			MM81X_REG_HOST_MAGIC_VALUE(mors), magic);
		ret = -EIO;
	}

	mm81x_release_bus(mors);
	return ret;
}

static int mm81x_fw_get_flags(struct mm81x *mors)
{
	int ret = 0;
	int fw_flags = 0;

	mm81x_claim_bus(mors);
	ret = mm81x_reg32_read(mors,
			       mors->host_table_ptr +
				       offsetof(struct host_table, fw_flags),
			       &fw_flags);
	mors->fw_flags = fw_flags;
	mm81x_release_bus(mors);

	return ret;
}

static int mm81x_fw_check_compatibility(struct mm81x *mors)
{
	int ret = 0;
	u32 fw_version;
	u32 major;
	u32 minor;
	u32 patch;

	mm81x_claim_bus(mors);
	ret = mm81x_reg32_read(mors,
			       mors->host_table_ptr +
				       offsetof(struct host_table,
						fw_version_number),
			       &fw_version);
	mm81x_release_bus(mors);

	major = MM81X_SEMVER_GET_MAJOR(fw_version);
	minor = MM81X_SEMVER_GET_MINOR(fw_version);
	patch = MM81X_SEMVER_GET_PATCH(fw_version);

	/* Firmware on device must match the firmware file we requested */
	if (ret == 0 && major != mors->fw_major) {
		dev_err(mors->dev,
			"Incompatible FW version: (Requested) v%u, (Chip) %d.%d.%d\n",
			mors->fw_major, major, minor, patch);
		ret = -EPERM;
	} else if (ret == 0 && major != HOST_CMD_SEMVER_MAJOR) {
		dev_warn(
			mors->dev,
			"Running FW v%d.%d.%d, driver supports up to v%d, some features might not be supported",
			major, minor, patch, HOST_CMD_SEMVER_MAJOR);
	} else if (ret == 0 && minor != HOST_CMD_SEMVER_MINOR) {
		dev_warn(
			mors->dev,
			"FW version mismatch, some features might not be supported: (Driver) %d.%d.%d, (Chip) %d.%d.%d",
			HOST_CMD_SEMVER_MAJOR, HOST_CMD_SEMVER_MINOR,
			HOST_CMD_SEMVER_PATCH, major, minor, patch);
	}

	return ret;
}

static int mm81x_fw_invalidate_host_ptr(struct mm81x *mors)
{
	int ret;

	mors->host_table_ptr = 0;
	mm81x_claim_bus(mors);
	ret = mm81x_reg32_write(mors, MM81X_REG_HOST_MANIFEST_PTR(mors), 0);
	mm81x_release_bus(mors);
	return ret;
}

static int mm81x_fw_get_host_table_ptr(struct mm81x *mors)
{
	int ret, err;

	mm81x_claim_bus(mors);
	ret = read_poll_timeout(mm81x_reg32_read, err,
				err || mors->host_table_ptr,
				HOST_TABLE_PTR_POLL_PERIOD_US,
				HOST_TABLE_PTR_POLL_TIMEOUT_US, false, mors,
				MM81X_REG_HOST_MANIFEST_PTR(mors),
				&mors->host_table_ptr);
	mm81x_release_bus(mors);

	return ret ? ret : err;
}

static int mm81x_fw_read_ext_host_table(struct mm81x *mors,
					struct ext_host_tbl **ext_host_table)
{
	int ret = 0;
	u32 host_tbl_ptr = mors->host_table_ptr;
	u32 ext_host_tbl_ptr;
	u32 ext_host_tbl_ptr_addr =
		host_tbl_ptr + offsetof(struct host_table, ext_host_tbl_addr);
	u32 ext_host_tbl_len;
	u32 ext_host_tbl_len_ptr_addr;
	struct ext_host_tbl *host_tbl = NULL;

	mm81x_claim_bus(mors);
	ret = mm81x_reg32_read(mors, ext_host_tbl_ptr_addr, &ext_host_tbl_ptr);
	if (ret)
		goto exit;

	if (!ext_host_tbl_ptr) {
		ret = -ENXIO;
		goto exit;
	}

	ext_host_tbl_len_ptr_addr =
		ext_host_tbl_ptr +
		offsetof(struct ext_host_tbl, ext_host_tbl_length);

	ret = mm81x_reg32_read(mors, ext_host_tbl_len_ptr_addr,
			       &ext_host_tbl_len);
	if (ret)
		goto exit;

	ext_host_tbl_len = ROUND_BYTES_TO_WORD(ext_host_tbl_len);
	if (WARN_ON(ext_host_tbl_len == 0 || ext_host_tbl_len > INT_MAX)) {
		ret = -EINVAL;
		goto exit;
	}

	host_tbl = kmalloc(ext_host_tbl_len, GFP_KERNEL);
	if (!host_tbl) {
		ret = -ENOMEM;
		goto exit;
	}

	ret = mm81x_dm_read(mors, ext_host_tbl_ptr, (u8 *)host_tbl,
			    (int)ext_host_tbl_len);
	if (ret)
		goto exit;

	mm81x_release_bus(mors);
	*ext_host_table = host_tbl;
	return ret;

exit:
	mm81x_release_bus(mors);
	kfree(host_tbl);
	return ret;
}

static void mm81x_fw_update_capabilities(struct mm81x *mors,
					 struct ext_host_tbl_s1g_caps *caps)
{
	int i;

	for (i = 0; i < FW_CAPABILITIES_FLAGS_WIDTH; i++) {
		mors->fw_caps.flags[i] = le32_to_cpu(caps->flags[i]);
		dev_dbg(mors->dev, "Firmware Manifest Flags%d: 0x%x", i,
			le32_to_cpu(caps->flags[i]));
	}
	mors->fw_caps.ampdu_mss = caps->ampdu_mss;
	mors->fw_caps.mm81x_mmss_offset = caps->mm81x_mmss_offset;
	mors->fw_caps.beamformee_sts_capability =
		caps->beamformee_sts_capability;
	mors->fw_caps.maximum_ampdu_length_exponent =
		caps->maximum_ampdu_length;
	mors->fw_caps.number_sounding_dimensions =
		caps->number_sounding_dimensions;

	dev_dbg(mors->dev, "\tAMPDU Minimum start spacing: %u",
		caps->ampdu_mss);
	dev_dbg(mors->dev, "\tMorse Minimum Start Spacing offset: %u",
		caps->mm81x_mmss_offset);
	dev_dbg(mors->dev, "\tBeamformee STS Capability: %u",
		caps->beamformee_sts_capability);
	dev_dbg(mors->dev, "\tNumber of Sounding Dimensions: %u",
		caps->number_sounding_dimensions);
	dev_dbg(mors->dev, "\tMaximum AMPDU Length Exponent: %u",
		caps->maximum_ampdu_length);
}

static void mm81x_fw_update_validate_skb_checksum(
	struct mm81x *mors,
	struct ext_host_tbl_insert_skb_checksum *validate_checksum)
{
	mors->hif.validate_skb_checksum =
		validate_checksum->insert_and_validate_checksum;
	dev_dbg(mors->dev, "Validate checksum inserted by fw %s",
		str_enabled_disabled(mors->hif.validate_skb_checksum));
}

int mm81x_fw_parse_ext_host_tbl(struct mm81x *mors)
{
	int ret;
	u8 *head;
	u8 *end;
	struct ext_host_tbl *ext_host_table = NULL;

	ret = mm81x_fw_read_ext_host_table(mors, &ext_host_table);
	if (ret || !ext_host_table)
		goto exit;

	/* Parse the TLVs */
	head = ext_host_table->ext_host_table_data_tlvs;
	end = ((u8 *)ext_host_table) +
	      le32_to_cpu(ext_host_table->ext_host_tbl_length);

	while (head < end) {
		struct ext_host_tbl_tlv_hdr *hdr =
			(struct ext_host_tbl_tlv_hdr *)head;

		switch (le16_to_cpu(hdr->tag)) {
		case MM81X_FW_HOST_TABLE_TAG_S1G_CAPABILITIES:
			mm81x_fw_update_capabilities(
				mors, (struct ext_host_tbl_s1g_caps *)hdr);
			break;

		case MM81X_FW_HOST_TABLE_TAG_INSERT_SKB_CHECKSUM:
			mm81x_fw_update_validate_skb_checksum(
				mors,
				(struct ext_host_tbl_insert_skb_checksum *)hdr);
			break;

		case MM81X_FW_HOST_TABLE_TAG_YAPS_TABLE:
			mm81x_yaps_hw_read_table(
				mors, &((struct ext_host_tbl_yaps_table *)hdr)
					       ->yaps_table);
			break;
		default:
			break;
		}

		head += le16_to_cpu(hdr->length);
		if (!hdr->length)
			break;
	}

	kfree(ext_host_table);
	return ret;
exit:
	dev_err(mors->dev, "failed to parse ext host table %d", ret);
	return ret;
}

static int __mm81x_fw_flash(struct mm81x *mors, const struct firmware *fw,
			    const struct firmware *bcf, bool reset)
{
	int ret;

	if (reset || !mors->chip_was_reset) {
		ret = mm81x_hw_digital_reset(mors);
		if (ret)
			return ret;
	}

	mm81x_hw_pre_firmware_ndr_hook(mors);

	ret = mm81x_fw_invalidate_host_ptr(mors);
	if (ret)
		return ret;

	ret = mm81x_fw_load_fw(mors, fw);
	if (ret)
		return ret;

	ret = mm81x_fw_load_bcf(mors, bcf, mors->bcf_address);
	if (ret)
		return ret;

	mm81x_fw_trigger(mors);
	mm81x_hw_post_firmware_ndr_hook(mors);

	ret = mm81x_fw_get_host_table_ptr(mors);
	if (ret)
		return ret;

	ret = mm81x_fw_verify_magic(mors);
	if (ret)
		return ret;

	return mm81x_fw_check_compatibility(mors);
}

static int mm81x_fw_flash(struct mm81x *mors, const struct firmware *fw,
			  const struct firmware *bcf, bool reset)
{
	int ret;
	int retries = FW_FLASH_ATTEMPT_COUNT;

	while (retries--) {
		ret = __mm81x_fw_flash(mors, fw, bcf, reset);
		if (!ret)
			return 0;

		mors->chip_was_reset = false;
	}

	return ret;
}

static uint32_t binary_crc(const struct firmware *fw)
{
	return ~crc32_le(~0, (unsigned char const *)fw->data, fw->size) &
	       0xffffffff;
}

static int mm81x_fw_request(struct mm81x *mors, const struct firmware **fw)
{
	int ret = -ENOENT;
	int ver;
	char *fw_path;

	for (ver = MM81X_FW_VER_MAX; ver >= MM81X_FW_VER_MIN; ver--) {
		fw_path = mm81x_core_get_fw_path(mors->chip_id, ver);
		if (!fw_path)
			return -ENOMEM;

		ret = firmware_request_nowarn(fw, fw_path, mors->dev);
		if (!ret) {
			dev_info(
				mors->dev,
				"Loaded firmware from %s, size %zu, crc32 0x%08x\n",
				fw_path, (*fw)->size, binary_crc(*fw));
			mors->fw_major = ver;
		}

		kfree(fw_path);
		if (!ret)
			return 0;
	}

	dev_err(mors->dev, "no firmware found (tried v%d down to v%d): %d\n",
		MM81X_FW_VER_MAX, MM81X_FW_VER_MIN, ret);
	return ret;
}

int mm81x_fw_init(struct mm81x *mors, bool reset)
{
	int ret;
	int board_id;
	char *bcf_path = NULL;
	const struct firmware *fw = NULL;
	const struct firmware *bcf = NULL;

	board_id = mm81x_hw_otp_get_board_type(mors);

	if (!mm81x_hw_otp_valid_board_type(board_id)) {
		dev_err(mors->dev,
			"OTP not set, unable to determine BCF to use");
		ret = -EINVAL;
		goto out;
	}

	dev_dbg(mors->dev, "Using board type 0x%04x from OTP", board_id);

	ret = mm81x_fw_request(mors, &fw);
	if (ret)
		goto out;

	bcf_path = kasprintf(GFP_KERNEL,
			     MM81X_FW_DIR
			     "/v%u/bcf_boardtype_%04x" MM81X_FW_EXT,
			     mors->fw_major, board_id);
	if (!bcf_path) {
		ret = -ENOMEM;
		goto out;
	}

	ret = request_firmware(&bcf, bcf_path, mors->dev);
	if (ret) {
		if (ret == -ENOENT)
			dev_err(mors->dev, "BCF %s not found\n", bcf_path);
		goto out;
	}

	dev_info(mors->dev, "Loaded BCF from %s, size %zu, crc32 0x%08x\n",
		 bcf_path, bcf->size, binary_crc(bcf));

	ret = mm81x_fw_flash(mors, fw, bcf, reset);
	if (ret) {
		dev_err(mors->dev, "failed to flash firmware: %d", ret);
		goto out;
	}

	ret = mm81x_fw_get_flags(mors);

out:
	release_firmware(fw);
	release_firmware(bcf);
	kfree(bcf_path);

	if (ret)
		dev_err(mors->dev, "failed to init firmware: %d", ret);
	else
		dev_dbg(mors->dev, "firmware initialised");

	return ret;
}
