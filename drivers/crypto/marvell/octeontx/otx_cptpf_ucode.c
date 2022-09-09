// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ctype.h>
#include <linux/firmware.h>
#include "otx_cpt_common.h"
#include "otx_cptpf_ucode.h"
#include "otx_cptpf.h"

#define CSR_DELAY 30
/* Tar archive defines */
#define TAR_MAGIC		"ustar"
#define TAR_MAGIC_LEN		6
#define TAR_BLOCK_LEN		512
#define REGTYPE			'0'
#define AREGTYPE		'\0'

/* tar header as defined in POSIX 1003.1-1990. */
struct tar_hdr_t {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
};

struct tar_blk_t {
	union {
		struct tar_hdr_t hdr;
		char block[TAR_BLOCK_LEN];
	};
};

struct tar_arch_info_t {
	struct list_head ucodes;
	const struct firmware *fw;
};

static struct otx_cpt_bitmap get_cores_bmap(struct device *dev,
					   struct otx_cpt_eng_grp_info *eng_grp)
{
	struct otx_cpt_bitmap bmap = { {0} };
	bool found = false;
	int i;

	if (eng_grp->g->engs_num > OTX_CPT_MAX_ENGINES) {
		dev_err(dev, "unsupported number of engines %d on octeontx\n",
			eng_grp->g->engs_num);
		return bmap;
	}

	for (i = 0; i < OTX_CPT_MAX_ETYPES_PER_GRP; i++) {
		if (eng_grp->engs[i].type) {
			bitmap_or(bmap.bits, bmap.bits,
				  eng_grp->engs[i].bmap,
				  eng_grp->g->engs_num);
			bmap.size = eng_grp->g->engs_num;
			found = true;
		}
	}

	if (!found)
		dev_err(dev, "No engines reserved for engine group %d\n",
			eng_grp->idx);
	return bmap;
}

static int is_eng_type(int val, int eng_type)
{
	return val & (1 << eng_type);
}

static int dev_supports_eng_type(struct otx_cpt_eng_grps *eng_grps,
				 int eng_type)
{
	return is_eng_type(eng_grps->eng_types_supported, eng_type);
}

static void set_ucode_filename(struct otx_cpt_ucode *ucode,
			       const char *filename)
{
	strlcpy(ucode->filename, filename, OTX_CPT_UCODE_NAME_LENGTH);
}

static char *get_eng_type_str(int eng_type)
{
	char *str = "unknown";

	switch (eng_type) {
	case OTX_CPT_SE_TYPES:
		str = "SE";
		break;

	case OTX_CPT_AE_TYPES:
		str = "AE";
		break;
	}
	return str;
}

static char *get_ucode_type_str(int ucode_type)
{
	char *str = "unknown";

	switch (ucode_type) {
	case (1 << OTX_CPT_SE_TYPES):
		str = "SE";
		break;

	case (1 << OTX_CPT_AE_TYPES):
		str = "AE";
		break;
	}
	return str;
}

static int get_ucode_type(struct otx_cpt_ucode_hdr *ucode_hdr, int *ucode_type)
{
	char tmp_ver_str[OTX_CPT_UCODE_VER_STR_SZ];
	u32 i, val = 0;
	u8 nn;

	strlcpy(tmp_ver_str, ucode_hdr->ver_str, OTX_CPT_UCODE_VER_STR_SZ);
	for (i = 0; i < strlen(tmp_ver_str); i++)
		tmp_ver_str[i] = tolower(tmp_ver_str[i]);

	nn = ucode_hdr->ver_num.nn;
	if (strnstr(tmp_ver_str, "se-", OTX_CPT_UCODE_VER_STR_SZ) &&
	    (nn == OTX_CPT_SE_UC_TYPE1 || nn == OTX_CPT_SE_UC_TYPE2 ||
	     nn == OTX_CPT_SE_UC_TYPE3))
		val |= 1 << OTX_CPT_SE_TYPES;
	if (strnstr(tmp_ver_str, "ae", OTX_CPT_UCODE_VER_STR_SZ) &&
	    nn == OTX_CPT_AE_UC_TYPE)
		val |= 1 << OTX_CPT_AE_TYPES;

	*ucode_type = val;

	if (!val)
		return -EINVAL;
	if (is_eng_type(val, OTX_CPT_AE_TYPES) &&
	    is_eng_type(val, OTX_CPT_SE_TYPES))
		return -EINVAL;
	return 0;
}

static int is_mem_zero(const char *ptr, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (ptr[i])
			return 0;
	}
	return 1;
}

static int cpt_set_ucode_base(struct otx_cpt_eng_grp_info *eng_grp, void *obj)
{
	struct otx_cpt_device *cpt = (struct otx_cpt_device *) obj;
	dma_addr_t dma_addr;
	struct otx_cpt_bitmap bmap;
	int i;

	bmap = get_cores_bmap(&cpt->pdev->dev, eng_grp);
	if (!bmap.size)
		return -EINVAL;

	if (eng_grp->mirror.is_ena)
		dma_addr =
		       eng_grp->g->grp[eng_grp->mirror.idx].ucode[0].align_dma;
	else
		dma_addr = eng_grp->ucode[0].align_dma;

	/*
	 * Set UCODE_BASE only for the cores which are not used,
	 * other cores should have already valid UCODE_BASE set
	 */
	for_each_set_bit(i, bmap.bits, bmap.size)
		if (!eng_grp->g->eng_ref_cnt[i])
			writeq((u64) dma_addr, cpt->reg_base +
				OTX_CPT_PF_ENGX_UCODE_BASE(i));
	return 0;
}

static int cpt_detach_and_disable_cores(struct otx_cpt_eng_grp_info *eng_grp,
					void *obj)
{
	struct otx_cpt_device *cpt = (struct otx_cpt_device *) obj;
	struct otx_cpt_bitmap bmap = { {0} };
	int timeout = 10;
	int i, busy;
	u64 reg;

	bmap = get_cores_bmap(&cpt->pdev->dev, eng_grp);
	if (!bmap.size)
		return -EINVAL;

	/* Detach the cores from group */
	reg = readq(cpt->reg_base + OTX_CPT_PF_GX_EN(eng_grp->idx));
	for_each_set_bit(i, bmap.bits, bmap.size) {
		if (reg & (1ull << i)) {
			eng_grp->g->eng_ref_cnt[i]--;
			reg &= ~(1ull << i);
		}
	}
	writeq(reg, cpt->reg_base + OTX_CPT_PF_GX_EN(eng_grp->idx));

	/* Wait for cores to become idle */
	do {
		busy = 0;
		usleep_range(10000, 20000);
		if (timeout-- < 0)
			return -EBUSY;

		reg = readq(cpt->reg_base + OTX_CPT_PF_EXEC_BUSY);
		for_each_set_bit(i, bmap.bits, bmap.size)
			if (reg & (1ull << i)) {
				busy = 1;
				break;
			}
	} while (busy);

	/* Disable the cores only if they are not used anymore */
	reg = readq(cpt->reg_base + OTX_CPT_PF_EXE_CTL);
	for_each_set_bit(i, bmap.bits, bmap.size)
		if (!eng_grp->g->eng_ref_cnt[i])
			reg &= ~(1ull << i);
	writeq(reg, cpt->reg_base + OTX_CPT_PF_EXE_CTL);

	return 0;
}

static int cpt_attach_and_enable_cores(struct otx_cpt_eng_grp_info *eng_grp,
				       void *obj)
{
	struct otx_cpt_device *cpt = (struct otx_cpt_device *) obj;
	struct otx_cpt_bitmap bmap;
	u64 reg;
	int i;

	bmap = get_cores_bmap(&cpt->pdev->dev, eng_grp);
	if (!bmap.size)
		return -EINVAL;

	/* Attach the cores to the group */
	reg = readq(cpt->reg_base + OTX_CPT_PF_GX_EN(eng_grp->idx));
	for_each_set_bit(i, bmap.bits, bmap.size) {
		if (!(reg & (1ull << i))) {
			eng_grp->g->eng_ref_cnt[i]++;
			reg |= 1ull << i;
		}
	}
	writeq(reg, cpt->reg_base + OTX_CPT_PF_GX_EN(eng_grp->idx));

	/* Enable the cores */
	reg = readq(cpt->reg_base + OTX_CPT_PF_EXE_CTL);
	for_each_set_bit(i, bmap.bits, bmap.size)
		reg |= 1ull << i;
	writeq(reg, cpt->reg_base + OTX_CPT_PF_EXE_CTL);

	return 0;
}

static int process_tar_file(struct device *dev,
			    struct tar_arch_info_t *tar_arch, char *filename,
			    const u8 *data, u32 size)
{
	struct tar_ucode_info_t *tar_info;
	struct otx_cpt_ucode_hdr *ucode_hdr;
	int ucode_type, ucode_size;

	/*
	 * If size is less than microcode header size then don't report
	 * an error because it might not be microcode file, just process
	 * next file from archive
	 */
	if (size < sizeof(struct otx_cpt_ucode_hdr))
		return 0;

	ucode_hdr = (struct otx_cpt_ucode_hdr *) data;
	/*
	 * If microcode version can't be found don't report an error
	 * because it might not be microcode file, just process next file
	 */
	if (get_ucode_type(ucode_hdr, &ucode_type))
		return 0;

	ucode_size = ntohl(ucode_hdr->code_length) * 2;
	if (!ucode_size || (size < round_up(ucode_size, 16) +
	    sizeof(struct otx_cpt_ucode_hdr) + OTX_CPT_UCODE_SIGN_LEN)) {
		dev_err(dev, "Ucode %s invalid size\n", filename);
		return -EINVAL;
	}

	tar_info = kzalloc(sizeof(struct tar_ucode_info_t), GFP_KERNEL);
	if (!tar_info)
		return -ENOMEM;

	tar_info->ucode_ptr = data;
	set_ucode_filename(&tar_info->ucode, filename);
	memcpy(tar_info->ucode.ver_str, ucode_hdr->ver_str,
	       OTX_CPT_UCODE_VER_STR_SZ);
	tar_info->ucode.ver_num = ucode_hdr->ver_num;
	tar_info->ucode.type = ucode_type;
	tar_info->ucode.size = ucode_size;
	list_add_tail(&tar_info->list, &tar_arch->ucodes);

	return 0;
}

static void release_tar_archive(struct tar_arch_info_t *tar_arch)
{
	struct tar_ucode_info_t *curr, *temp;

	if (!tar_arch)
		return;

	list_for_each_entry_safe(curr, temp, &tar_arch->ucodes, list) {
		list_del(&curr->list);
		kfree(curr);
	}

	if (tar_arch->fw)
		release_firmware(tar_arch->fw);
	kfree(tar_arch);
}

static struct tar_ucode_info_t *get_uc_from_tar_archive(
					struct tar_arch_info_t *tar_arch,
					int ucode_type)
{
	struct tar_ucode_info_t *curr, *uc_found = NULL;

	list_for_each_entry(curr, &tar_arch->ucodes, list) {
		if (!is_eng_type(curr->ucode.type, ucode_type))
			continue;

		if (!uc_found) {
			uc_found = curr;
			continue;
		}

		switch (ucode_type) {
		case OTX_CPT_AE_TYPES:
			break;

		case OTX_CPT_SE_TYPES:
			if (uc_found->ucode.ver_num.nn == OTX_CPT_SE_UC_TYPE2 ||
			    (uc_found->ucode.ver_num.nn == OTX_CPT_SE_UC_TYPE3
			     && curr->ucode.ver_num.nn == OTX_CPT_SE_UC_TYPE1))
				uc_found = curr;
			break;
		}
	}

	return uc_found;
}

static void print_tar_dbg_info(struct tar_arch_info_t *tar_arch,
			       char *tar_filename)
{
	struct tar_ucode_info_t *curr;

	pr_debug("Tar archive filename %s\n", tar_filename);
	pr_debug("Tar archive pointer %p, size %ld\n", tar_arch->fw->data,
		 tar_arch->fw->size);
	list_for_each_entry(curr, &tar_arch->ucodes, list) {
		pr_debug("Ucode filename %s\n", curr->ucode.filename);
		pr_debug("Ucode version string %s\n", curr->ucode.ver_str);
		pr_debug("Ucode version %d.%d.%d.%d\n",
			 curr->ucode.ver_num.nn, curr->ucode.ver_num.xx,
			 curr->ucode.ver_num.yy, curr->ucode.ver_num.zz);
		pr_debug("Ucode type (%d) %s\n", curr->ucode.type,
			 get_ucode_type_str(curr->ucode.type));
		pr_debug("Ucode size %d\n", curr->ucode.size);
		pr_debug("Ucode ptr %p\n", curr->ucode_ptr);
	}
}

static struct tar_arch_info_t *load_tar_archive(struct device *dev,
						char *tar_filename)
{
	struct tar_arch_info_t *tar_arch = NULL;
	struct tar_blk_t *tar_blk;
	unsigned int cur_size;
	size_t tar_offs = 0;
	size_t tar_size;
	int ret;

	tar_arch = kzalloc(sizeof(struct tar_arch_info_t), GFP_KERNEL);
	if (!tar_arch)
		return NULL;

	INIT_LIST_HEAD(&tar_arch->ucodes);

	/* Load tar archive */
	ret = request_firmware(&tar_arch->fw, tar_filename, dev);
	if (ret)
		goto release_tar_arch;

	if (tar_arch->fw->size < TAR_BLOCK_LEN) {
		dev_err(dev, "Invalid tar archive %s\n", tar_filename);
		goto release_tar_arch;
	}

	tar_size = tar_arch->fw->size;
	tar_blk = (struct tar_blk_t *) tar_arch->fw->data;
	if (strncmp(tar_blk->hdr.magic, TAR_MAGIC, TAR_MAGIC_LEN - 1)) {
		dev_err(dev, "Unsupported format of tar archive %s\n",
			tar_filename);
		goto release_tar_arch;
	}

	while (1) {
		/* Read current file size */
		ret = kstrtouint(tar_blk->hdr.size, 8, &cur_size);
		if (ret)
			goto release_tar_arch;

		if (tar_offs + cur_size > tar_size ||
		    tar_offs + 2*TAR_BLOCK_LEN > tar_size) {
			dev_err(dev, "Invalid tar archive %s\n", tar_filename);
			goto release_tar_arch;
		}

		tar_offs += TAR_BLOCK_LEN;
		if (tar_blk->hdr.typeflag == REGTYPE ||
		    tar_blk->hdr.typeflag == AREGTYPE) {
			ret = process_tar_file(dev, tar_arch,
					       tar_blk->hdr.name,
					       &tar_arch->fw->data[tar_offs],
					       cur_size);
			if (ret)
				goto release_tar_arch;
		}

		tar_offs += (cur_size/TAR_BLOCK_LEN) * TAR_BLOCK_LEN;
		if (cur_size % TAR_BLOCK_LEN)
			tar_offs += TAR_BLOCK_LEN;

		/* Check for the end of the archive */
		if (tar_offs + 2*TAR_BLOCK_LEN > tar_size) {
			dev_err(dev, "Invalid tar archive %s\n", tar_filename);
			goto release_tar_arch;
		}

		if (is_mem_zero(&tar_arch->fw->data[tar_offs],
		    2*TAR_BLOCK_LEN))
			break;

		/* Read next block from tar archive */
		tar_blk = (struct tar_blk_t *) &tar_arch->fw->data[tar_offs];
	}

	print_tar_dbg_info(tar_arch, tar_filename);
	return tar_arch;
release_tar_arch:
	release_tar_archive(tar_arch);
	return NULL;
}

static struct otx_cpt_engs_rsvd *find_engines_by_type(
					struct otx_cpt_eng_grp_info *eng_grp,
					int eng_type)
{
	int i;

	for (i = 0; i < OTX_CPT_MAX_ETYPES_PER_GRP; i++) {
		if (!eng_grp->engs[i].type)
			continue;

		if (eng_grp->engs[i].type == eng_type)
			return &eng_grp->engs[i];
	}
	return NULL;
}

int otx_cpt_uc_supports_eng_type(struct otx_cpt_ucode *ucode, int eng_type)
{
	return is_eng_type(ucode->type, eng_type);
}
EXPORT_SYMBOL_GPL(otx_cpt_uc_supports_eng_type);

int otx_cpt_eng_grp_has_eng_type(struct otx_cpt_eng_grp_info *eng_grp,
				 int eng_type)
{
	struct otx_cpt_engs_rsvd *engs;

	engs = find_engines_by_type(eng_grp, eng_type);

	return (engs != NULL ? 1 : 0);
}
EXPORT_SYMBOL_GPL(otx_cpt_eng_grp_has_eng_type);

static void print_ucode_info(struct otx_cpt_eng_grp_info *eng_grp,
			     char *buf, int size)
{
	if (eng_grp->mirror.is_ena) {
		scnprintf(buf, size, "%s (shared with engine_group%d)",
			  eng_grp->g->grp[eng_grp->mirror.idx].ucode[0].ver_str,
			  eng_grp->mirror.idx);
	} else {
		scnprintf(buf, size, "%s", eng_grp->ucode[0].ver_str);
	}
}

static void print_engs_info(struct otx_cpt_eng_grp_info *eng_grp,
			    char *buf, int size, int idx)
{
	struct otx_cpt_engs_rsvd *mirrored_engs = NULL;
	struct otx_cpt_engs_rsvd *engs;
	int len, i;

	buf[0] = '\0';
	for (i = 0; i < OTX_CPT_MAX_ETYPES_PER_GRP; i++) {
		engs = &eng_grp->engs[i];
		if (!engs->type)
			continue;
		if (idx != -1 && idx != i)
			continue;

		if (eng_grp->mirror.is_ena)
			mirrored_engs = find_engines_by_type(
					&eng_grp->g->grp[eng_grp->mirror.idx],
					engs->type);
		if (i > 0 && idx == -1) {
			len = strlen(buf);
			scnprintf(buf+len, size-len, ", ");
		}

		len = strlen(buf);
		scnprintf(buf+len, size-len, "%d %s ", mirrored_engs ?
			  engs->count + mirrored_engs->count : engs->count,
			  get_eng_type_str(engs->type));
		if (mirrored_engs) {
			len = strlen(buf);
			scnprintf(buf+len, size-len,
				  "(%d shared with engine_group%d) ",
				  engs->count <= 0 ? engs->count +
				  mirrored_engs->count : mirrored_engs->count,
				  eng_grp->mirror.idx);
		}
	}
}

static void print_ucode_dbg_info(struct otx_cpt_ucode *ucode)
{
	pr_debug("Ucode info\n");
	pr_debug("Ucode version string %s\n", ucode->ver_str);
	pr_debug("Ucode version %d.%d.%d.%d\n", ucode->ver_num.nn,
		 ucode->ver_num.xx, ucode->ver_num.yy, ucode->ver_num.zz);
	pr_debug("Ucode type %s\n", get_ucode_type_str(ucode->type));
	pr_debug("Ucode size %d\n", ucode->size);
	pr_debug("Ucode virt address %16.16llx\n", (u64)ucode->align_va);
	pr_debug("Ucode phys address %16.16llx\n", ucode->align_dma);
}

static void cpt_print_engines_mask(struct otx_cpt_eng_grp_info *eng_grp,
				   struct device *dev, char *buf, int size)
{
	struct otx_cpt_bitmap bmap;
	u32 mask[2];

	bmap = get_cores_bmap(dev, eng_grp);
	if (!bmap.size) {
		scnprintf(buf, size, "unknown");
		return;
	}
	bitmap_to_arr32(mask, bmap.bits, bmap.size);
	scnprintf(buf, size, "%8.8x %8.8x", mask[1], mask[0]);
}


static void print_dbg_info(struct device *dev,
			   struct otx_cpt_eng_grps *eng_grps)
{
	char engs_info[2*OTX_CPT_UCODE_NAME_LENGTH];
	struct otx_cpt_eng_grp_info *mirrored_grp;
	char engs_mask[OTX_CPT_UCODE_NAME_LENGTH];
	struct otx_cpt_eng_grp_info *grp;
	struct otx_cpt_engs_rsvd *engs;
	u32 mask[4];
	int i, j;

	pr_debug("Engine groups global info\n");
	pr_debug("max SE %d, max AE %d\n",
		 eng_grps->avail.max_se_cnt, eng_grps->avail.max_ae_cnt);
	pr_debug("free SE %d\n", eng_grps->avail.se_cnt);
	pr_debug("free AE %d\n", eng_grps->avail.ae_cnt);

	for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++) {
		grp = &eng_grps->grp[i];
		pr_debug("engine_group%d, state %s\n", i, grp->is_enabled ?
			 "enabled" : "disabled");
		if (grp->is_enabled) {
			mirrored_grp = &eng_grps->grp[grp->mirror.idx];
			pr_debug("Ucode0 filename %s, version %s\n",
				 grp->mirror.is_ena ?
				 mirrored_grp->ucode[0].filename :
				 grp->ucode[0].filename,
				 grp->mirror.is_ena ?
				 mirrored_grp->ucode[0].ver_str :
				 grp->ucode[0].ver_str);
		}

		for (j = 0; j < OTX_CPT_MAX_ETYPES_PER_GRP; j++) {
			engs = &grp->engs[j];
			if (engs->type) {
				print_engs_info(grp, engs_info,
						2*OTX_CPT_UCODE_NAME_LENGTH, j);
				pr_debug("Slot%d: %s\n", j, engs_info);
				bitmap_to_arr32(mask, engs->bmap,
						eng_grps->engs_num);
				pr_debug("Mask: %8.8x %8.8x %8.8x %8.8x\n",
					 mask[3], mask[2], mask[1], mask[0]);
			} else
				pr_debug("Slot%d not used\n", j);
		}
		if (grp->is_enabled) {
			cpt_print_engines_mask(grp, dev, engs_mask,
					       OTX_CPT_UCODE_NAME_LENGTH);
			pr_debug("Cmask: %s\n", engs_mask);
		}
	}
}

static int update_engines_avail_count(struct device *dev,
				      struct otx_cpt_engs_available *avail,
				      struct otx_cpt_engs_rsvd *engs, int val)
{
	switch (engs->type) {
	case OTX_CPT_SE_TYPES:
		avail->se_cnt += val;
		break;

	case OTX_CPT_AE_TYPES:
		avail->ae_cnt += val;
		break;

	default:
		dev_err(dev, "Invalid engine type %d\n", engs->type);
		return -EINVAL;
	}

	return 0;
}

static int update_engines_offset(struct device *dev,
				 struct otx_cpt_engs_available *avail,
				 struct otx_cpt_engs_rsvd *engs)
{
	switch (engs->type) {
	case OTX_CPT_SE_TYPES:
		engs->offset = 0;
		break;

	case OTX_CPT_AE_TYPES:
		engs->offset = avail->max_se_cnt;
		break;

	default:
		dev_err(dev, "Invalid engine type %d\n", engs->type);
		return -EINVAL;
	}

	return 0;
}

static int release_engines(struct device *dev, struct otx_cpt_eng_grp_info *grp)
{
	int i, ret = 0;

	for (i = 0; i < OTX_CPT_MAX_ETYPES_PER_GRP; i++) {
		if (!grp->engs[i].type)
			continue;

		if (grp->engs[i].count > 0) {
			ret = update_engines_avail_count(dev, &grp->g->avail,
							 &grp->engs[i],
							 grp->engs[i].count);
			if (ret)
				return ret;
		}

		grp->engs[i].type = 0;
		grp->engs[i].count = 0;
		grp->engs[i].offset = 0;
		grp->engs[i].ucode = NULL;
		bitmap_zero(grp->engs[i].bmap, grp->g->engs_num);
	}

	return 0;
}

static int do_reserve_engines(struct device *dev,
			      struct otx_cpt_eng_grp_info *grp,
			      struct otx_cpt_engines *req_engs)
{
	struct otx_cpt_engs_rsvd *engs = NULL;
	int i, ret;

	for (i = 0; i < OTX_CPT_MAX_ETYPES_PER_GRP; i++) {
		if (!grp->engs[i].type) {
			engs = &grp->engs[i];
			break;
		}
	}

	if (!engs)
		return -ENOMEM;

	engs->type = req_engs->type;
	engs->count = req_engs->count;

	ret = update_engines_offset(dev, &grp->g->avail, engs);
	if (ret)
		return ret;

	if (engs->count > 0) {
		ret = update_engines_avail_count(dev, &grp->g->avail, engs,
						 -engs->count);
		if (ret)
			return ret;
	}

	return 0;
}

static int check_engines_availability(struct device *dev,
				      struct otx_cpt_eng_grp_info *grp,
				      struct otx_cpt_engines *req_eng)
{
	int avail_cnt = 0;

	switch (req_eng->type) {
	case OTX_CPT_SE_TYPES:
		avail_cnt = grp->g->avail.se_cnt;
		break;

	case OTX_CPT_AE_TYPES:
		avail_cnt = grp->g->avail.ae_cnt;
		break;

	default:
		dev_err(dev, "Invalid engine type %d\n", req_eng->type);
		return -EINVAL;
	}

	if (avail_cnt < req_eng->count) {
		dev_err(dev,
			"Error available %s engines %d < than requested %d\n",
			get_eng_type_str(req_eng->type),
			avail_cnt, req_eng->count);
		return -EBUSY;
	}

	return 0;
}

static int reserve_engines(struct device *dev, struct otx_cpt_eng_grp_info *grp,
			   struct otx_cpt_engines *req_engs, int req_cnt)
{
	int i, ret;

	/* Validate if a number of requested engines is available */
	for (i = 0; i < req_cnt; i++) {
		ret = check_engines_availability(dev, grp, &req_engs[i]);
		if (ret)
			return ret;
	}

	/* Reserve requested engines for this engine group */
	for (i = 0; i < req_cnt; i++) {
		ret = do_reserve_engines(dev, grp, &req_engs[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static ssize_t eng_grp_info_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	char ucode_info[2*OTX_CPT_UCODE_NAME_LENGTH];
	char engs_info[2*OTX_CPT_UCODE_NAME_LENGTH];
	char engs_mask[OTX_CPT_UCODE_NAME_LENGTH];
	struct otx_cpt_eng_grp_info *eng_grp;
	int ret;

	eng_grp = container_of(attr, struct otx_cpt_eng_grp_info, info_attr);
	mutex_lock(&eng_grp->g->lock);

	print_engs_info(eng_grp, engs_info, 2*OTX_CPT_UCODE_NAME_LENGTH, -1);
	print_ucode_info(eng_grp, ucode_info, 2*OTX_CPT_UCODE_NAME_LENGTH);
	cpt_print_engines_mask(eng_grp, dev, engs_mask,
			       OTX_CPT_UCODE_NAME_LENGTH);
	ret = scnprintf(buf, PAGE_SIZE,
			"Microcode : %s\nEngines: %s\nEngines mask: %s\n",
			ucode_info, engs_info, engs_mask);

	mutex_unlock(&eng_grp->g->lock);
	return ret;
}

static int create_sysfs_eng_grps_info(struct device *dev,
				      struct otx_cpt_eng_grp_info *eng_grp)
{
	eng_grp->info_attr.show = eng_grp_info_show;
	eng_grp->info_attr.store = NULL;
	eng_grp->info_attr.attr.name = eng_grp->sysfs_info_name;
	eng_grp->info_attr.attr.mode = 0440;
	sysfs_attr_init(&eng_grp->info_attr.attr);
	return device_create_file(dev, &eng_grp->info_attr);
}

static void ucode_unload(struct device *dev, struct otx_cpt_ucode *ucode)
{
	if (ucode->va) {
		dma_free_coherent(dev, ucode->size + OTX_CPT_UCODE_ALIGNMENT,
				  ucode->va, ucode->dma);
		ucode->va = NULL;
		ucode->align_va = NULL;
		ucode->dma = 0;
		ucode->align_dma = 0;
		ucode->size = 0;
	}

	memset(&ucode->ver_str, 0, OTX_CPT_UCODE_VER_STR_SZ);
	memset(&ucode->ver_num, 0, sizeof(struct otx_cpt_ucode_ver_num));
	set_ucode_filename(ucode, "");
	ucode->type = 0;
}

static int copy_ucode_to_dma_mem(struct device *dev,
				 struct otx_cpt_ucode *ucode,
				 const u8 *ucode_data)
{
	u32 i;

	/*  Allocate DMAable space */
	ucode->va = dma_alloc_coherent(dev, ucode->size +
				       OTX_CPT_UCODE_ALIGNMENT,
				       &ucode->dma, GFP_KERNEL);
	if (!ucode->va) {
		dev_err(dev, "Unable to allocate space for microcode\n");
		return -ENOMEM;
	}
	ucode->align_va = PTR_ALIGN(ucode->va, OTX_CPT_UCODE_ALIGNMENT);
	ucode->align_dma = PTR_ALIGN(ucode->dma, OTX_CPT_UCODE_ALIGNMENT);

	memcpy((void *) ucode->align_va, (void *) ucode_data +
	       sizeof(struct otx_cpt_ucode_hdr), ucode->size);

	/* Byte swap 64-bit */
	for (i = 0; i < (ucode->size / 8); i++)
		((__be64 *)ucode->align_va)[i] =
				cpu_to_be64(((u64 *)ucode->align_va)[i]);
	/*  Ucode needs 16-bit swap */
	for (i = 0; i < (ucode->size / 2); i++)
		((__be16 *)ucode->align_va)[i] =
				cpu_to_be16(((u16 *)ucode->align_va)[i]);
	return 0;
}

static int ucode_load(struct device *dev, struct otx_cpt_ucode *ucode,
		      const char *ucode_filename)
{
	struct otx_cpt_ucode_hdr *ucode_hdr;
	const struct firmware *fw;
	int ret;

	set_ucode_filename(ucode, ucode_filename);
	ret = request_firmware(&fw, ucode->filename, dev);
	if (ret)
		return ret;

	ucode_hdr = (struct otx_cpt_ucode_hdr *) fw->data;
	memcpy(ucode->ver_str, ucode_hdr->ver_str, OTX_CPT_UCODE_VER_STR_SZ);
	ucode->ver_num = ucode_hdr->ver_num;
	ucode->size = ntohl(ucode_hdr->code_length) * 2;
	if (!ucode->size || (fw->size < round_up(ucode->size, 16)
	    + sizeof(struct otx_cpt_ucode_hdr) + OTX_CPT_UCODE_SIGN_LEN)) {
		dev_err(dev, "Ucode %s invalid size\n", ucode_filename);
		ret = -EINVAL;
		goto release_fw;
	}

	ret = get_ucode_type(ucode_hdr, &ucode->type);
	if (ret) {
		dev_err(dev, "Microcode %s unknown type 0x%x\n",
			ucode->filename, ucode->type);
		goto release_fw;
	}

	ret = copy_ucode_to_dma_mem(dev, ucode, fw->data);
	if (ret)
		goto release_fw;

	print_ucode_dbg_info(ucode);
release_fw:
	release_firmware(fw);
	return ret;
}

static int enable_eng_grp(struct otx_cpt_eng_grp_info *eng_grp,
			  void *obj)
{
	int ret;

	ret = cpt_set_ucode_base(eng_grp, obj);
	if (ret)
		return ret;

	ret = cpt_attach_and_enable_cores(eng_grp, obj);
	return ret;
}

static int disable_eng_grp(struct device *dev,
			   struct otx_cpt_eng_grp_info *eng_grp,
			   void *obj)
{
	int i, ret;

	ret = cpt_detach_and_disable_cores(eng_grp, obj);
	if (ret)
		return ret;

	/* Unload ucode used by this engine group */
	ucode_unload(dev, &eng_grp->ucode[0]);

	for (i = 0; i < OTX_CPT_MAX_ETYPES_PER_GRP; i++) {
		if (!eng_grp->engs[i].type)
			continue;

		eng_grp->engs[i].ucode = &eng_grp->ucode[0];
	}

	ret = cpt_set_ucode_base(eng_grp, obj);

	return ret;
}

static void setup_eng_grp_mirroring(struct otx_cpt_eng_grp_info *dst_grp,
				    struct otx_cpt_eng_grp_info *src_grp)
{
	/* Setup fields for engine group which is mirrored */
	src_grp->mirror.is_ena = false;
	src_grp->mirror.idx = 0;
	src_grp->mirror.ref_count++;

	/* Setup fields for mirroring engine group */
	dst_grp->mirror.is_ena = true;
	dst_grp->mirror.idx = src_grp->idx;
	dst_grp->mirror.ref_count = 0;
}

static void remove_eng_grp_mirroring(struct otx_cpt_eng_grp_info *dst_grp)
{
	struct otx_cpt_eng_grp_info *src_grp;

	if (!dst_grp->mirror.is_ena)
		return;

	src_grp = &dst_grp->g->grp[dst_grp->mirror.idx];

	src_grp->mirror.ref_count--;
	dst_grp->mirror.is_ena = false;
	dst_grp->mirror.idx = 0;
	dst_grp->mirror.ref_count = 0;
}

static void update_requested_engs(struct otx_cpt_eng_grp_info *mirrored_eng_grp,
				  struct otx_cpt_engines *engs, int engs_cnt)
{
	struct otx_cpt_engs_rsvd *mirrored_engs;
	int i;

	for (i = 0; i < engs_cnt; i++) {
		mirrored_engs = find_engines_by_type(mirrored_eng_grp,
						     engs[i].type);
		if (!mirrored_engs)
			continue;

		/*
		 * If mirrored group has this type of engines attached then
		 * there are 3 scenarios possible:
		 * 1) mirrored_engs.count == engs[i].count then all engines
		 * from mirrored engine group will be shared with this engine
		 * group
		 * 2) mirrored_engs.count > engs[i].count then only a subset of
		 * engines from mirrored engine group will be shared with this
		 * engine group
		 * 3) mirrored_engs.count < engs[i].count then all engines
		 * from mirrored engine group will be shared with this group
		 * and additional engines will be reserved for exclusively use
		 * by this engine group
		 */
		engs[i].count -= mirrored_engs->count;
	}
}

static struct otx_cpt_eng_grp_info *find_mirrored_eng_grp(
					struct otx_cpt_eng_grp_info *grp)
{
	struct otx_cpt_eng_grps *eng_grps = grp->g;
	int i;

	for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++) {
		if (!eng_grps->grp[i].is_enabled)
			continue;
		if (eng_grps->grp[i].ucode[0].type)
			continue;
		if (grp->idx == i)
			continue;
		if (!strncasecmp(eng_grps->grp[i].ucode[0].ver_str,
				 grp->ucode[0].ver_str,
				 OTX_CPT_UCODE_VER_STR_SZ))
			return &eng_grps->grp[i];
	}

	return NULL;
}

static struct otx_cpt_eng_grp_info *find_unused_eng_grp(
					struct otx_cpt_eng_grps *eng_grps)
{
	int i;

	for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++) {
		if (!eng_grps->grp[i].is_enabled)
			return &eng_grps->grp[i];
	}
	return NULL;
}

static int eng_grp_update_masks(struct device *dev,
				struct otx_cpt_eng_grp_info *eng_grp)
{
	struct otx_cpt_engs_rsvd *engs, *mirrored_engs;
	struct otx_cpt_bitmap tmp_bmap = { {0} };
	int i, j, cnt, max_cnt;
	int bit;

	for (i = 0; i < OTX_CPT_MAX_ETYPES_PER_GRP; i++) {
		engs = &eng_grp->engs[i];
		if (!engs->type)
			continue;
		if (engs->count <= 0)
			continue;

		switch (engs->type) {
		case OTX_CPT_SE_TYPES:
			max_cnt = eng_grp->g->avail.max_se_cnt;
			break;

		case OTX_CPT_AE_TYPES:
			max_cnt = eng_grp->g->avail.max_ae_cnt;
			break;

		default:
			dev_err(dev, "Invalid engine type %d\n", engs->type);
			return -EINVAL;
		}

		cnt = engs->count;
		WARN_ON(engs->offset + max_cnt > OTX_CPT_MAX_ENGINES);
		bitmap_zero(tmp_bmap.bits, eng_grp->g->engs_num);
		for (j = engs->offset; j < engs->offset + max_cnt; j++) {
			if (!eng_grp->g->eng_ref_cnt[j]) {
				bitmap_set(tmp_bmap.bits, j, 1);
				cnt--;
				if (!cnt)
					break;
			}
		}

		if (cnt)
			return -ENOSPC;

		bitmap_copy(engs->bmap, tmp_bmap.bits, eng_grp->g->engs_num);
	}

	if (!eng_grp->mirror.is_ena)
		return 0;

	for (i = 0; i < OTX_CPT_MAX_ETYPES_PER_GRP; i++) {
		engs = &eng_grp->engs[i];
		if (!engs->type)
			continue;

		mirrored_engs = find_engines_by_type(
					&eng_grp->g->grp[eng_grp->mirror.idx],
					engs->type);
		WARN_ON(!mirrored_engs && engs->count <= 0);
		if (!mirrored_engs)
			continue;

		bitmap_copy(tmp_bmap.bits, mirrored_engs->bmap,
			    eng_grp->g->engs_num);
		if (engs->count < 0) {
			bit = find_first_bit(mirrored_engs->bmap,
					     eng_grp->g->engs_num);
			bitmap_clear(tmp_bmap.bits, bit, -engs->count);
		}
		bitmap_or(engs->bmap, engs->bmap, tmp_bmap.bits,
			  eng_grp->g->engs_num);
	}
	return 0;
}

static int delete_engine_group(struct device *dev,
			       struct otx_cpt_eng_grp_info *eng_grp)
{
	int i, ret;

	if (!eng_grp->is_enabled)
		return -EINVAL;

	if (eng_grp->mirror.ref_count) {
		dev_err(dev, "Can't delete engine_group%d as it is used by engine_group(s):",
			eng_grp->idx);
		for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++) {
			if (eng_grp->g->grp[i].mirror.is_ena &&
			    eng_grp->g->grp[i].mirror.idx == eng_grp->idx)
				pr_cont(" %d", i);
		}
		pr_cont("\n");
		return -EINVAL;
	}

	/* Removing engine group mirroring if enabled */
	remove_eng_grp_mirroring(eng_grp);

	/* Disable engine group */
	ret = disable_eng_grp(dev, eng_grp, eng_grp->g->obj);
	if (ret)
		return ret;

	/* Release all engines held by this engine group */
	ret = release_engines(dev, eng_grp);
	if (ret)
		return ret;

	device_remove_file(dev, &eng_grp->info_attr);
	eng_grp->is_enabled = false;

	return 0;
}

static int validate_1_ucode_scenario(struct device *dev,
				     struct otx_cpt_eng_grp_info *eng_grp,
				     struct otx_cpt_engines *engs, int engs_cnt)
{
	int i;

	/* Verify that ucode loaded supports requested engine types */
	for (i = 0; i < engs_cnt; i++) {
		if (!otx_cpt_uc_supports_eng_type(&eng_grp->ucode[0],
						  engs[i].type)) {
			dev_err(dev,
				"Microcode %s does not support %s engines\n",
				eng_grp->ucode[0].filename,
				get_eng_type_str(engs[i].type));
			return -EINVAL;
		}
	}
	return 0;
}

static void update_ucode_ptrs(struct otx_cpt_eng_grp_info *eng_grp)
{
	struct otx_cpt_ucode *ucode;

	if (eng_grp->mirror.is_ena)
		ucode = &eng_grp->g->grp[eng_grp->mirror.idx].ucode[0];
	else
		ucode = &eng_grp->ucode[0];
	WARN_ON(!eng_grp->engs[0].type);
	eng_grp->engs[0].ucode = ucode;
}

static int create_engine_group(struct device *dev,
			       struct otx_cpt_eng_grps *eng_grps,
			       struct otx_cpt_engines *engs, int engs_cnt,
			       void *ucode_data[], int ucodes_cnt,
			       bool use_uc_from_tar_arch)
{
	struct otx_cpt_eng_grp_info *mirrored_eng_grp;
	struct tar_ucode_info_t *tar_info;
	struct otx_cpt_eng_grp_info *eng_grp;
	int i, ret = 0;

	if (ucodes_cnt > OTX_CPT_MAX_ETYPES_PER_GRP)
		return -EINVAL;

	/* Validate if requested engine types are supported by this device */
	for (i = 0; i < engs_cnt; i++)
		if (!dev_supports_eng_type(eng_grps, engs[i].type)) {
			dev_err(dev, "Device does not support %s engines\n",
				get_eng_type_str(engs[i].type));
			return -EPERM;
		}

	/* Find engine group which is not used */
	eng_grp = find_unused_eng_grp(eng_grps);
	if (!eng_grp) {
		dev_err(dev, "Error all engine groups are being used\n");
		return -ENOSPC;
	}

	/* Load ucode */
	for (i = 0; i < ucodes_cnt; i++) {
		if (use_uc_from_tar_arch) {
			tar_info = (struct tar_ucode_info_t *) ucode_data[i];
			eng_grp->ucode[i] = tar_info->ucode;
			ret = copy_ucode_to_dma_mem(dev, &eng_grp->ucode[i],
						    tar_info->ucode_ptr);
		} else
			ret = ucode_load(dev, &eng_grp->ucode[i],
					 (char *) ucode_data[i]);
		if (ret)
			goto err_ucode_unload;
	}

	/* Validate scenario where 1 ucode is used */
	ret = validate_1_ucode_scenario(dev, eng_grp, engs, engs_cnt);
	if (ret)
		goto err_ucode_unload;

	/* Check if this group mirrors another existing engine group */
	mirrored_eng_grp = find_mirrored_eng_grp(eng_grp);
	if (mirrored_eng_grp) {
		/* Setup mirroring */
		setup_eng_grp_mirroring(eng_grp, mirrored_eng_grp);

		/*
		 * Update count of requested engines because some
		 * of them might be shared with mirrored group
		 */
		update_requested_engs(mirrored_eng_grp, engs, engs_cnt);
	}

	/* Reserve engines */
	ret = reserve_engines(dev, eng_grp, engs, engs_cnt);
	if (ret)
		goto err_ucode_unload;

	/* Update ucode pointers used by engines */
	update_ucode_ptrs(eng_grp);

	/* Update engine masks used by this group */
	ret = eng_grp_update_masks(dev, eng_grp);
	if (ret)
		goto err_release_engs;

	/* Create sysfs entry for engine group info */
	ret = create_sysfs_eng_grps_info(dev, eng_grp);
	if (ret)
		goto err_release_engs;

	/* Enable engine group */
	ret = enable_eng_grp(eng_grp, eng_grps->obj);
	if (ret)
		goto err_release_engs;

	/*
	 * If this engine group mirrors another engine group
	 * then we need to unload ucode as we will use ucode
	 * from mirrored engine group
	 */
	if (eng_grp->mirror.is_ena)
		ucode_unload(dev, &eng_grp->ucode[0]);

	eng_grp->is_enabled = true;
	if (eng_grp->mirror.is_ena)
		dev_info(dev,
			 "Engine_group%d: reuse microcode %s from group %d\n",
			 eng_grp->idx, mirrored_eng_grp->ucode[0].ver_str,
			 mirrored_eng_grp->idx);
	else
		dev_info(dev, "Engine_group%d: microcode loaded %s\n",
			 eng_grp->idx, eng_grp->ucode[0].ver_str);

	return 0;

err_release_engs:
	release_engines(dev, eng_grp);
err_ucode_unload:
	ucode_unload(dev, &eng_grp->ucode[0]);
	return ret;
}

static ssize_t ucode_load_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct otx_cpt_engines engs[OTX_CPT_MAX_ETYPES_PER_GRP] = { {0} };
	char *ucode_filename[OTX_CPT_MAX_ETYPES_PER_GRP];
	char tmp_buf[OTX_CPT_UCODE_NAME_LENGTH] = { 0 };
	char *start, *val, *err_msg, *tmp;
	struct otx_cpt_eng_grps *eng_grps;
	int grp_idx = 0, ret = -EINVAL;
	bool has_se, has_ie, has_ae;
	int del_grp_idx = -1;
	int ucode_idx = 0;

	if (strlen(buf) > OTX_CPT_UCODE_NAME_LENGTH)
		return -EINVAL;

	eng_grps = container_of(attr, struct otx_cpt_eng_grps, ucode_load_attr);
	err_msg = "Invalid engine group format";
	strlcpy(tmp_buf, buf, OTX_CPT_UCODE_NAME_LENGTH);
	start = tmp_buf;

	has_se = has_ie = has_ae = false;

	for (;;) {
		val = strsep(&start, ";");
		if (!val)
			break;
		val = strim(val);
		if (!*val)
			continue;

		if (!strncasecmp(val, "engine_group", 12)) {
			if (del_grp_idx != -1)
				goto err_print;
			tmp = strim(strsep(&val, ":"));
			if (!val)
				goto err_print;
			if (strlen(tmp) != 13)
				goto err_print;
			if (kstrtoint((tmp + 12), 10, &del_grp_idx))
				goto err_print;
			val = strim(val);
			if (strncasecmp(val, "null", 4))
				goto err_print;
			if (strlen(val) != 4)
				goto err_print;
		} else if (!strncasecmp(val, "se", 2) && strchr(val, ':')) {
			if (has_se || ucode_idx)
				goto err_print;
			tmp = strim(strsep(&val, ":"));
			if (!val)
				goto err_print;
			if (strlen(tmp) != 2)
				goto err_print;
			if (kstrtoint(strim(val), 10, &engs[grp_idx].count))
				goto err_print;
			engs[grp_idx++].type = OTX_CPT_SE_TYPES;
			has_se = true;
		} else if (!strncasecmp(val, "ae", 2) && strchr(val, ':')) {
			if (has_ae || ucode_idx)
				goto err_print;
			tmp = strim(strsep(&val, ":"));
			if (!val)
				goto err_print;
			if (strlen(tmp) != 2)
				goto err_print;
			if (kstrtoint(strim(val), 10, &engs[grp_idx].count))
				goto err_print;
			engs[grp_idx++].type = OTX_CPT_AE_TYPES;
			has_ae = true;
		} else {
			if (ucode_idx > 1)
				goto err_print;
			if (!strlen(val))
				goto err_print;
			if (strnstr(val, " ", strlen(val)))
				goto err_print;
			ucode_filename[ucode_idx++] = val;
		}
	}

	/* Validate input parameters */
	if (del_grp_idx == -1) {
		if (!(grp_idx && ucode_idx))
			goto err_print;

		if (ucode_idx > 1 && grp_idx < 2)
			goto err_print;

		if (grp_idx > OTX_CPT_MAX_ETYPES_PER_GRP) {
			err_msg = "Error max 2 engine types can be attached";
			goto err_print;
		}

	} else {
		if (del_grp_idx < 0 ||
		    del_grp_idx >= OTX_CPT_MAX_ENGINE_GROUPS) {
			dev_err(dev, "Invalid engine group index %d\n",
				del_grp_idx);
			ret = -EINVAL;
			return ret;
		}

		if (!eng_grps->grp[del_grp_idx].is_enabled) {
			dev_err(dev, "Error engine_group%d is not configured\n",
				del_grp_idx);
			ret = -EINVAL;
			return ret;
		}

		if (grp_idx || ucode_idx)
			goto err_print;
	}

	mutex_lock(&eng_grps->lock);

	if (eng_grps->is_rdonly) {
		dev_err(dev, "Disable VFs before modifying engine groups\n");
		ret = -EACCES;
		goto err_unlock;
	}

	if (del_grp_idx == -1)
		/* create engine group */
		ret = create_engine_group(dev, eng_grps, engs, grp_idx,
					  (void **) ucode_filename,
					  ucode_idx, false);
	else
		/* delete engine group */
		ret = delete_engine_group(dev, &eng_grps->grp[del_grp_idx]);
	if (ret)
		goto err_unlock;

	print_dbg_info(dev, eng_grps);
err_unlock:
	mutex_unlock(&eng_grps->lock);
	return ret ? ret : count;
err_print:
	dev_err(dev, "%s\n", err_msg);

	return ret;
}

int otx_cpt_try_create_default_eng_grps(struct pci_dev *pdev,
					struct otx_cpt_eng_grps *eng_grps,
					int pf_type)
{
	struct tar_ucode_info_t *tar_info[OTX_CPT_MAX_ETYPES_PER_GRP] = {};
	struct otx_cpt_engines engs[OTX_CPT_MAX_ETYPES_PER_GRP] = {};
	struct tar_arch_info_t *tar_arch = NULL;
	char *tar_filename;
	int i, ret = 0;

	mutex_lock(&eng_grps->lock);

	/*
	 * We don't create engine group for kernel crypto if attempt to create
	 * it was already made (when user enabled VFs for the first time)
	 */
	if (eng_grps->is_first_try)
		goto unlock_mutex;
	eng_grps->is_first_try = true;

	/* We create group for kcrypto only if no groups are configured */
	for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++)
		if (eng_grps->grp[i].is_enabled)
			goto unlock_mutex;

	switch (pf_type) {
	case OTX_CPT_AE:
	case OTX_CPT_SE:
		tar_filename = OTX_CPT_UCODE_TAR_FILE_NAME;
		break;

	default:
		dev_err(&pdev->dev, "Unknown PF type %d\n", pf_type);
		ret = -EINVAL;
		goto unlock_mutex;
	}

	tar_arch = load_tar_archive(&pdev->dev, tar_filename);
	if (!tar_arch)
		goto unlock_mutex;

	/*
	 * If device supports SE engines and there is SE microcode in tar
	 * archive try to create engine group with SE engines for kernel
	 * crypto functionality (symmetric crypto)
	 */
	tar_info[0] = get_uc_from_tar_archive(tar_arch, OTX_CPT_SE_TYPES);
	if (tar_info[0] &&
	    dev_supports_eng_type(eng_grps, OTX_CPT_SE_TYPES)) {

		engs[0].type = OTX_CPT_SE_TYPES;
		engs[0].count = eng_grps->avail.max_se_cnt;

		ret = create_engine_group(&pdev->dev, eng_grps, engs, 1,
					  (void **) tar_info, 1, true);
		if (ret)
			goto release_tar_arch;
	}
	/*
	 * If device supports AE engines and there is AE microcode in tar
	 * archive try to create engine group with AE engines for asymmetric
	 * crypto functionality.
	 */
	tar_info[0] = get_uc_from_tar_archive(tar_arch, OTX_CPT_AE_TYPES);
	if (tar_info[0] &&
	    dev_supports_eng_type(eng_grps, OTX_CPT_AE_TYPES)) {

		engs[0].type = OTX_CPT_AE_TYPES;
		engs[0].count = eng_grps->avail.max_ae_cnt;

		ret = create_engine_group(&pdev->dev, eng_grps, engs, 1,
					  (void **) tar_info, 1, true);
		if (ret)
			goto release_tar_arch;
	}

	print_dbg_info(&pdev->dev, eng_grps);
release_tar_arch:
	release_tar_archive(tar_arch);
unlock_mutex:
	mutex_unlock(&eng_grps->lock);
	return ret;
}

void otx_cpt_set_eng_grps_is_rdonly(struct otx_cpt_eng_grps *eng_grps,
				    bool is_rdonly)
{
	mutex_lock(&eng_grps->lock);

	eng_grps->is_rdonly = is_rdonly;

	mutex_unlock(&eng_grps->lock);
}

void otx_cpt_disable_all_cores(struct otx_cpt_device *cpt)
{
	int grp, timeout = 100;
	u64 reg;

	/* Disengage the cores from groups */
	for (grp = 0; grp < OTX_CPT_MAX_ENGINE_GROUPS; grp++) {
		writeq(0, cpt->reg_base + OTX_CPT_PF_GX_EN(grp));
		udelay(CSR_DELAY);
	}

	reg = readq(cpt->reg_base + OTX_CPT_PF_EXEC_BUSY);
	while (reg) {
		udelay(CSR_DELAY);
		reg = readq(cpt->reg_base + OTX_CPT_PF_EXEC_BUSY);
		if (timeout--) {
			dev_warn(&cpt->pdev->dev, "Cores still busy\n");
			break;
		}
	}

	/* Disable the cores */
	writeq(0, cpt->reg_base + OTX_CPT_PF_EXE_CTL);
}

void otx_cpt_cleanup_eng_grps(struct pci_dev *pdev,
			      struct otx_cpt_eng_grps *eng_grps)
{
	struct otx_cpt_eng_grp_info *grp;
	int i, j;

	mutex_lock(&eng_grps->lock);
	if (eng_grps->is_ucode_load_created) {
		device_remove_file(&pdev->dev,
				   &eng_grps->ucode_load_attr);
		eng_grps->is_ucode_load_created = false;
	}

	/* First delete all mirroring engine groups */
	for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++)
		if (eng_grps->grp[i].mirror.is_ena)
			delete_engine_group(&pdev->dev, &eng_grps->grp[i]);

	/* Delete remaining engine groups */
	for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++)
		delete_engine_group(&pdev->dev, &eng_grps->grp[i]);

	/* Release memory */
	for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++) {
		grp = &eng_grps->grp[i];
		for (j = 0; j < OTX_CPT_MAX_ETYPES_PER_GRP; j++) {
			kfree(grp->engs[j].bmap);
			grp->engs[j].bmap = NULL;
		}
	}

	mutex_unlock(&eng_grps->lock);
}

int otx_cpt_init_eng_grps(struct pci_dev *pdev,
			  struct otx_cpt_eng_grps *eng_grps, int pf_type)
{
	struct otx_cpt_eng_grp_info *grp;
	int i, j, ret = 0;

	mutex_init(&eng_grps->lock);
	eng_grps->obj = pci_get_drvdata(pdev);
	eng_grps->avail.se_cnt = eng_grps->avail.max_se_cnt;
	eng_grps->avail.ae_cnt = eng_grps->avail.max_ae_cnt;

	eng_grps->engs_num = eng_grps->avail.max_se_cnt +
			     eng_grps->avail.max_ae_cnt;
	if (eng_grps->engs_num > OTX_CPT_MAX_ENGINES) {
		dev_err(&pdev->dev,
			"Number of engines %d > than max supported %d\n",
			eng_grps->engs_num, OTX_CPT_MAX_ENGINES);
		ret = -EINVAL;
		goto err;
	}

	for (i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++) {
		grp = &eng_grps->grp[i];
		grp->g = eng_grps;
		grp->idx = i;

		snprintf(grp->sysfs_info_name, OTX_CPT_UCODE_NAME_LENGTH,
			 "engine_group%d", i);
		for (j = 0; j < OTX_CPT_MAX_ETYPES_PER_GRP; j++) {
			grp->engs[j].bmap =
				kcalloc(BITS_TO_LONGS(eng_grps->engs_num),
					sizeof(long), GFP_KERNEL);
			if (!grp->engs[j].bmap) {
				ret = -ENOMEM;
				goto err;
			}
		}
	}

	switch (pf_type) {
	case OTX_CPT_SE:
		/* OcteonTX 83XX SE CPT PF has only SE engines attached */
		eng_grps->eng_types_supported = 1 << OTX_CPT_SE_TYPES;
		break;

	case OTX_CPT_AE:
		/* OcteonTX 83XX AE CPT PF has only AE engines attached */
		eng_grps->eng_types_supported = 1 << OTX_CPT_AE_TYPES;
		break;

	default:
		dev_err(&pdev->dev, "Unknown PF type %d\n", pf_type);
		ret = -EINVAL;
		goto err;
	}

	eng_grps->ucode_load_attr.show = NULL;
	eng_grps->ucode_load_attr.store = ucode_load_store;
	eng_grps->ucode_load_attr.attr.name = "ucode_load";
	eng_grps->ucode_load_attr.attr.mode = 0220;
	sysfs_attr_init(&eng_grps->ucode_load_attr.attr);
	ret = device_create_file(&pdev->dev,
				 &eng_grps->ucode_load_attr);
	if (ret)
		goto err;
	eng_grps->is_ucode_load_created = true;

	print_dbg_info(&pdev->dev, eng_grps);
	return ret;
err:
	otx_cpt_cleanup_eng_grps(pdev, eng_grps);
	return ret;
}
