// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include <linux/ctype.h>
#include <linux/firmware.h>
#include "otx2_cptpf_ucode.h"
#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"
#include "otx2_cptlf.h"
#include "otx2_cpt_reqmgr.h"
#include "rvu_reg.h"

#define CSR_DELAY 30

#define LOADFVC_RLEN 8
#define LOADFVC_MAJOR_OP 0x01
#define LOADFVC_MINOR_OP 0x08

#define CTX_FLUSH_TIMER_CNT 0xFFFFFF

struct fw_info_t {
	struct list_head ucodes;
};

static struct otx2_cpt_bitmap get_cores_bmap(struct device *dev,
					struct otx2_cpt_eng_grp_info *eng_grp)
{
	struct otx2_cpt_bitmap bmap = { {0} };
	bool found = false;
	int i;

	if (eng_grp->g->engs_num < 0 ||
	    eng_grp->g->engs_num > OTX2_CPT_MAX_ENGINES) {
		dev_err(dev, "unsupported number of engines %d on octeontx2\n",
			eng_grp->g->engs_num);
		return bmap;
	}

	for (i = 0; i  < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
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

static int is_2nd_ucode_used(struct otx2_cpt_eng_grp_info *eng_grp)
{
	if (eng_grp->ucode[1].type)
		return true;
	else
		return false;
}

static void set_ucode_filename(struct otx2_cpt_ucode *ucode,
			       const char *filename)
{
	strlcpy(ucode->filename, filename, OTX2_CPT_NAME_LENGTH);
}

static char *get_eng_type_str(int eng_type)
{
	char *str = "unknown";

	switch (eng_type) {
	case OTX2_CPT_SE_TYPES:
		str = "SE";
		break;

	case OTX2_CPT_IE_TYPES:
		str = "IE";
		break;

	case OTX2_CPT_AE_TYPES:
		str = "AE";
		break;
	}
	return str;
}

static char *get_ucode_type_str(int ucode_type)
{
	char *str = "unknown";

	switch (ucode_type) {
	case (1 << OTX2_CPT_SE_TYPES):
		str = "SE";
		break;

	case (1 << OTX2_CPT_IE_TYPES):
		str = "IE";
		break;

	case (1 << OTX2_CPT_AE_TYPES):
		str = "AE";
		break;

	case (1 << OTX2_CPT_SE_TYPES | 1 << OTX2_CPT_IE_TYPES):
		str = "SE+IPSEC";
		break;
	}
	return str;
}

static int get_ucode_type(struct device *dev,
			  struct otx2_cpt_ucode_hdr *ucode_hdr,
			  int *ucode_type)
{
	struct otx2_cptpf_dev *cptpf = dev_get_drvdata(dev);
	char ver_str_prefix[OTX2_CPT_UCODE_VER_STR_SZ];
	char tmp_ver_str[OTX2_CPT_UCODE_VER_STR_SZ];
	struct pci_dev *pdev = cptpf->pdev;
	int i, val = 0;
	u8 nn;

	strlcpy(tmp_ver_str, ucode_hdr->ver_str, OTX2_CPT_UCODE_VER_STR_SZ);
	for (i = 0; i < strlen(tmp_ver_str); i++)
		tmp_ver_str[i] = tolower(tmp_ver_str[i]);

	sprintf(ver_str_prefix, "ocpt-%02d", pdev->revision);
	if (!strnstr(tmp_ver_str, ver_str_prefix, OTX2_CPT_UCODE_VER_STR_SZ))
		return -EINVAL;

	nn = ucode_hdr->ver_num.nn;
	if (strnstr(tmp_ver_str, "se-", OTX2_CPT_UCODE_VER_STR_SZ) &&
	    (nn == OTX2_CPT_SE_UC_TYPE1 || nn == OTX2_CPT_SE_UC_TYPE2 ||
	     nn == OTX2_CPT_SE_UC_TYPE3))
		val |= 1 << OTX2_CPT_SE_TYPES;
	if (strnstr(tmp_ver_str, "ie-", OTX2_CPT_UCODE_VER_STR_SZ) &&
	    (nn == OTX2_CPT_IE_UC_TYPE1 || nn == OTX2_CPT_IE_UC_TYPE2 ||
	     nn == OTX2_CPT_IE_UC_TYPE3))
		val |= 1 << OTX2_CPT_IE_TYPES;
	if (strnstr(tmp_ver_str, "ae", OTX2_CPT_UCODE_VER_STR_SZ) &&
	    nn == OTX2_CPT_AE_UC_TYPE)
		val |= 1 << OTX2_CPT_AE_TYPES;

	*ucode_type = val;

	if (!val)
		return -EINVAL;

	return 0;
}

static int __write_ucode_base(struct otx2_cptpf_dev *cptpf, int eng,
			      dma_addr_t dma_addr, int blkaddr)
{
	return otx2_cpt_write_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
				     CPT_AF_EXEX_UCODE_BASE(eng),
				     (u64)dma_addr, blkaddr);
}

static int cptx_set_ucode_base(struct otx2_cpt_eng_grp_info *eng_grp,
			       struct otx2_cptpf_dev *cptpf, int blkaddr)
{
	struct otx2_cpt_engs_rsvd *engs;
	dma_addr_t dma_addr;
	int i, bit, ret;

	/* Set PF number for microcode fetches */
	ret = otx2_cpt_write_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
				    CPT_AF_PF_FUNC,
				    cptpf->pf_id << RVU_PFVF_PF_SHIFT, blkaddr);
	if (ret)
		return ret;

	for (i = 0; i < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
		engs = &eng_grp->engs[i];
		if (!engs->type)
			continue;

		dma_addr = engs->ucode->dma;

		/*
		 * Set UCODE_BASE only for the cores which are not used,
		 * other cores should have already valid UCODE_BASE set
		 */
		for_each_set_bit(bit, engs->bmap, eng_grp->g->engs_num)
			if (!eng_grp->g->eng_ref_cnt[bit]) {
				ret = __write_ucode_base(cptpf, bit, dma_addr,
							 blkaddr);
				if (ret)
					return ret;
			}
	}
	return 0;
}

static int cpt_set_ucode_base(struct otx2_cpt_eng_grp_info *eng_grp, void *obj)
{
	struct otx2_cptpf_dev *cptpf = obj;
	int ret;

	if (cptpf->has_cpt1) {
		ret = cptx_set_ucode_base(eng_grp, cptpf, BLKADDR_CPT1);
		if (ret)
			return ret;
	}
	return cptx_set_ucode_base(eng_grp, cptpf, BLKADDR_CPT0);
}

static int cptx_detach_and_disable_cores(struct otx2_cpt_eng_grp_info *eng_grp,
					 struct otx2_cptpf_dev *cptpf,
					 struct otx2_cpt_bitmap bmap,
					 int blkaddr)
{
	int i, timeout = 10;
	int busy, ret;
	u64 reg = 0;

	/* Detach the cores from group */
	for_each_set_bit(i, bmap.bits, bmap.size) {
		ret = otx2_cpt_read_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
					   CPT_AF_EXEX_CTL2(i), &reg, blkaddr);
		if (ret)
			return ret;

		if (reg & (1ull << eng_grp->idx)) {
			eng_grp->g->eng_ref_cnt[i]--;
			reg &= ~(1ull << eng_grp->idx);

			ret = otx2_cpt_write_af_reg(&cptpf->afpf_mbox,
						    cptpf->pdev,
						    CPT_AF_EXEX_CTL2(i), reg,
						    blkaddr);
			if (ret)
				return ret;
		}
	}

	/* Wait for cores to become idle */
	do {
		busy = 0;
		usleep_range(10000, 20000);
		if (timeout-- < 0)
			return -EBUSY;

		for_each_set_bit(i, bmap.bits, bmap.size) {
			ret = otx2_cpt_read_af_reg(&cptpf->afpf_mbox,
						   cptpf->pdev,
						   CPT_AF_EXEX_STS(i), &reg,
						   blkaddr);
			if (ret)
				return ret;

			if (reg & 0x1) {
				busy = 1;
				break;
			}
		}
	} while (busy);

	/* Disable the cores only if they are not used anymore */
	for_each_set_bit(i, bmap.bits, bmap.size) {
		if (!eng_grp->g->eng_ref_cnt[i]) {
			ret = otx2_cpt_write_af_reg(&cptpf->afpf_mbox,
						    cptpf->pdev,
						    CPT_AF_EXEX_CTL(i), 0x0,
						    blkaddr);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int cpt_detach_and_disable_cores(struct otx2_cpt_eng_grp_info *eng_grp,
					void *obj)
{
	struct otx2_cptpf_dev *cptpf = obj;
	struct otx2_cpt_bitmap bmap;
	int ret;

	bmap = get_cores_bmap(&cptpf->pdev->dev, eng_grp);
	if (!bmap.size)
		return -EINVAL;

	if (cptpf->has_cpt1) {
		ret = cptx_detach_and_disable_cores(eng_grp, cptpf, bmap,
						    BLKADDR_CPT1);
		if (ret)
			return ret;
	}
	return cptx_detach_and_disable_cores(eng_grp, cptpf, bmap,
					     BLKADDR_CPT0);
}

static int cptx_attach_and_enable_cores(struct otx2_cpt_eng_grp_info *eng_grp,
					struct otx2_cptpf_dev *cptpf,
					struct otx2_cpt_bitmap bmap,
					int blkaddr)
{
	u64 reg = 0;
	int i, ret;

	/* Attach the cores to the group */
	for_each_set_bit(i, bmap.bits, bmap.size) {
		ret = otx2_cpt_read_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
					   CPT_AF_EXEX_CTL2(i), &reg, blkaddr);
		if (ret)
			return ret;

		if (!(reg & (1ull << eng_grp->idx))) {
			eng_grp->g->eng_ref_cnt[i]++;
			reg |= 1ull << eng_grp->idx;

			ret = otx2_cpt_write_af_reg(&cptpf->afpf_mbox,
						    cptpf->pdev,
						    CPT_AF_EXEX_CTL2(i), reg,
						    blkaddr);
			if (ret)
				return ret;
		}
	}

	/* Enable the cores */
	for_each_set_bit(i, bmap.bits, bmap.size) {
		ret = otx2_cpt_add_write_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
						CPT_AF_EXEX_CTL(i), 0x1,
						blkaddr);
		if (ret)
			return ret;
	}
	return otx2_cpt_send_af_reg_requests(&cptpf->afpf_mbox, cptpf->pdev);
}

static int cpt_attach_and_enable_cores(struct otx2_cpt_eng_grp_info *eng_grp,
				       void *obj)
{
	struct otx2_cptpf_dev *cptpf = obj;
	struct otx2_cpt_bitmap bmap;
	int ret;

	bmap = get_cores_bmap(&cptpf->pdev->dev, eng_grp);
	if (!bmap.size)
		return -EINVAL;

	if (cptpf->has_cpt1) {
		ret = cptx_attach_and_enable_cores(eng_grp, cptpf, bmap,
						   BLKADDR_CPT1);
		if (ret)
			return ret;
	}
	return cptx_attach_and_enable_cores(eng_grp, cptpf, bmap, BLKADDR_CPT0);
}

static int load_fw(struct device *dev, struct fw_info_t *fw_info,
		   char *filename)
{
	struct otx2_cpt_ucode_hdr *ucode_hdr;
	struct otx2_cpt_uc_info_t *uc_info;
	int ucode_type, ucode_size;
	int ret;

	uc_info = kzalloc(sizeof(*uc_info), GFP_KERNEL);
	if (!uc_info)
		return -ENOMEM;

	ret = request_firmware(&uc_info->fw, filename, dev);
	if (ret)
		goto free_uc_info;

	ucode_hdr = (struct otx2_cpt_ucode_hdr *)uc_info->fw->data;
	ret = get_ucode_type(dev, ucode_hdr, &ucode_type);
	if (ret)
		goto release_fw;

	ucode_size = ntohl(ucode_hdr->code_length) * 2;
	if (!ucode_size) {
		dev_err(dev, "Ucode %s invalid size\n", filename);
		ret = -EINVAL;
		goto release_fw;
	}

	set_ucode_filename(&uc_info->ucode, filename);
	memcpy(uc_info->ucode.ver_str, ucode_hdr->ver_str,
	       OTX2_CPT_UCODE_VER_STR_SZ);
	uc_info->ucode.ver_num = ucode_hdr->ver_num;
	uc_info->ucode.type = ucode_type;
	uc_info->ucode.size = ucode_size;
	list_add_tail(&uc_info->list, &fw_info->ucodes);

	return 0;

release_fw:
	release_firmware(uc_info->fw);
free_uc_info:
	kfree(uc_info);
	return ret;
}

static void cpt_ucode_release_fw(struct fw_info_t *fw_info)
{
	struct otx2_cpt_uc_info_t *curr, *temp;

	if (!fw_info)
		return;

	list_for_each_entry_safe(curr, temp, &fw_info->ucodes, list) {
		list_del(&curr->list);
		release_firmware(curr->fw);
		kfree(curr);
	}
}

static struct otx2_cpt_uc_info_t *get_ucode(struct fw_info_t *fw_info,
					    int ucode_type)
{
	struct otx2_cpt_uc_info_t *curr;

	list_for_each_entry(curr, &fw_info->ucodes, list) {
		if (!is_eng_type(curr->ucode.type, ucode_type))
			continue;

		return curr;
	}
	return NULL;
}

static void print_uc_info(struct fw_info_t *fw_info)
{
	struct otx2_cpt_uc_info_t *curr;

	list_for_each_entry(curr, &fw_info->ucodes, list) {
		pr_debug("Ucode filename %s\n", curr->ucode.filename);
		pr_debug("Ucode version string %s\n", curr->ucode.ver_str);
		pr_debug("Ucode version %d.%d.%d.%d\n",
			 curr->ucode.ver_num.nn, curr->ucode.ver_num.xx,
			 curr->ucode.ver_num.yy, curr->ucode.ver_num.zz);
		pr_debug("Ucode type (%d) %s\n", curr->ucode.type,
			 get_ucode_type_str(curr->ucode.type));
		pr_debug("Ucode size %d\n", curr->ucode.size);
		pr_debug("Ucode ptr %p\n", curr->fw->data);
	}
}

static int cpt_ucode_load_fw(struct pci_dev *pdev, struct fw_info_t *fw_info)
{
	char filename[OTX2_CPT_NAME_LENGTH];
	char eng_type[8] = {0};
	int ret, e, i;

	INIT_LIST_HEAD(&fw_info->ucodes);

	for (e = 1; e < OTX2_CPT_MAX_ENG_TYPES; e++) {
		strcpy(eng_type, get_eng_type_str(e));
		for (i = 0; i < strlen(eng_type); i++)
			eng_type[i] = tolower(eng_type[i]);

		snprintf(filename, sizeof(filename), "mrvl/cpt%02d/%s.out",
			 pdev->revision, eng_type);
		/* Request firmware for each engine type */
		ret = load_fw(&pdev->dev, fw_info, filename);
		if (ret)
			goto release_fw;
	}
	print_uc_info(fw_info);
	return 0;

release_fw:
	cpt_ucode_release_fw(fw_info);
	return ret;
}

static struct otx2_cpt_engs_rsvd *find_engines_by_type(
					struct otx2_cpt_eng_grp_info *eng_grp,
					int eng_type)
{
	int i;

	for (i = 0; i < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
		if (!eng_grp->engs[i].type)
			continue;

		if (eng_grp->engs[i].type == eng_type)
			return &eng_grp->engs[i];
	}
	return NULL;
}

static int eng_grp_has_eng_type(struct otx2_cpt_eng_grp_info *eng_grp,
				int eng_type)
{
	struct otx2_cpt_engs_rsvd *engs;

	engs = find_engines_by_type(eng_grp, eng_type);

	return (engs != NULL ? 1 : 0);
}

static int update_engines_avail_count(struct device *dev,
				      struct otx2_cpt_engs_available *avail,
				      struct otx2_cpt_engs_rsvd *engs, int val)
{
	switch (engs->type) {
	case OTX2_CPT_SE_TYPES:
		avail->se_cnt += val;
		break;

	case OTX2_CPT_IE_TYPES:
		avail->ie_cnt += val;
		break;

	case OTX2_CPT_AE_TYPES:
		avail->ae_cnt += val;
		break;

	default:
		dev_err(dev, "Invalid engine type %d\n", engs->type);
		return -EINVAL;
	}
	return 0;
}

static int update_engines_offset(struct device *dev,
				 struct otx2_cpt_engs_available *avail,
				 struct otx2_cpt_engs_rsvd *engs)
{
	switch (engs->type) {
	case OTX2_CPT_SE_TYPES:
		engs->offset = 0;
		break;

	case OTX2_CPT_IE_TYPES:
		engs->offset = avail->max_se_cnt;
		break;

	case OTX2_CPT_AE_TYPES:
		engs->offset = avail->max_se_cnt + avail->max_ie_cnt;
		break;

	default:
		dev_err(dev, "Invalid engine type %d\n", engs->type);
		return -EINVAL;
	}
	return 0;
}

static int release_engines(struct device *dev,
			   struct otx2_cpt_eng_grp_info *grp)
{
	int i, ret = 0;

	for (i = 0; i < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
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
			      struct otx2_cpt_eng_grp_info *grp,
			      struct otx2_cpt_engines *req_engs)
{
	struct otx2_cpt_engs_rsvd *engs = NULL;
	int i, ret;

	for (i = 0; i < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
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
				      struct otx2_cpt_eng_grp_info *grp,
				      struct otx2_cpt_engines *req_eng)
{
	int avail_cnt = 0;

	switch (req_eng->type) {
	case OTX2_CPT_SE_TYPES:
		avail_cnt = grp->g->avail.se_cnt;
		break;

	case OTX2_CPT_IE_TYPES:
		avail_cnt = grp->g->avail.ie_cnt;
		break;

	case OTX2_CPT_AE_TYPES:
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

static int reserve_engines(struct device *dev,
			   struct otx2_cpt_eng_grp_info *grp,
			   struct otx2_cpt_engines *req_engs, int ucodes_cnt)
{
	int i, ret = 0;

	/* Validate if a number of requested engines are available */
	for (i = 0; i < ucodes_cnt; i++) {
		ret = check_engines_availability(dev, grp, &req_engs[i]);
		if (ret)
			return ret;
	}

	/* Reserve requested engines for this engine group */
	for (i = 0; i < ucodes_cnt; i++) {
		ret = do_reserve_engines(dev, grp, &req_engs[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static void ucode_unload(struct device *dev, struct otx2_cpt_ucode *ucode)
{
	if (ucode->va) {
		dma_free_coherent(dev, OTX2_CPT_UCODE_SZ, ucode->va,
				  ucode->dma);
		ucode->va = NULL;
		ucode->dma = 0;
		ucode->size = 0;
	}

	memset(&ucode->ver_str, 0, OTX2_CPT_UCODE_VER_STR_SZ);
	memset(&ucode->ver_num, 0, sizeof(struct otx2_cpt_ucode_ver_num));
	set_ucode_filename(ucode, "");
	ucode->type = 0;
}

static int copy_ucode_to_dma_mem(struct device *dev,
				 struct otx2_cpt_ucode *ucode,
				 const u8 *ucode_data)
{
	u32 i;

	/*  Allocate DMAable space */
	ucode->va = dma_alloc_coherent(dev, OTX2_CPT_UCODE_SZ, &ucode->dma,
				       GFP_KERNEL);
	if (!ucode->va)
		return -ENOMEM;

	memcpy(ucode->va, ucode_data + sizeof(struct otx2_cpt_ucode_hdr),
	       ucode->size);

	/* Byte swap 64-bit */
	for (i = 0; i < (ucode->size / 8); i++)
		cpu_to_be64s(&((u64 *)ucode->va)[i]);
	/*  Ucode needs 16-bit swap */
	for (i = 0; i < (ucode->size / 2); i++)
		cpu_to_be16s(&((u16 *)ucode->va)[i]);
	return 0;
}

static int enable_eng_grp(struct otx2_cpt_eng_grp_info *eng_grp,
			  void *obj)
{
	int ret;

	/* Point microcode to each core of the group */
	ret = cpt_set_ucode_base(eng_grp, obj);
	if (ret)
		return ret;

	/* Attach the cores to the group and enable them */
	ret = cpt_attach_and_enable_cores(eng_grp, obj);

	return ret;
}

static int disable_eng_grp(struct device *dev,
			   struct otx2_cpt_eng_grp_info *eng_grp,
			   void *obj)
{
	int i, ret;

	/* Disable all engines used by this group */
	ret = cpt_detach_and_disable_cores(eng_grp, obj);
	if (ret)
		return ret;

	/* Unload ucode used by this engine group */
	ucode_unload(dev, &eng_grp->ucode[0]);
	ucode_unload(dev, &eng_grp->ucode[1]);

	for (i = 0; i < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
		if (!eng_grp->engs[i].type)
			continue;

		eng_grp->engs[i].ucode = &eng_grp->ucode[0];
	}

	/* Clear UCODE_BASE register for each engine used by this group */
	ret = cpt_set_ucode_base(eng_grp, obj);

	return ret;
}

static void setup_eng_grp_mirroring(struct otx2_cpt_eng_grp_info *dst_grp,
				    struct otx2_cpt_eng_grp_info *src_grp)
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

static void remove_eng_grp_mirroring(struct otx2_cpt_eng_grp_info *dst_grp)
{
	struct otx2_cpt_eng_grp_info *src_grp;

	if (!dst_grp->mirror.is_ena)
		return;

	src_grp = &dst_grp->g->grp[dst_grp->mirror.idx];

	src_grp->mirror.ref_count--;
	dst_grp->mirror.is_ena = false;
	dst_grp->mirror.idx = 0;
	dst_grp->mirror.ref_count = 0;
}

static void update_requested_engs(struct otx2_cpt_eng_grp_info *mirror_eng_grp,
				  struct otx2_cpt_engines *engs, int engs_cnt)
{
	struct otx2_cpt_engs_rsvd *mirrored_engs;
	int i;

	for (i = 0; i < engs_cnt; i++) {
		mirrored_engs = find_engines_by_type(mirror_eng_grp,
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

static struct otx2_cpt_eng_grp_info *find_mirrored_eng_grp(
					struct otx2_cpt_eng_grp_info *grp)
{
	struct otx2_cpt_eng_grps *eng_grps = grp->g;
	int i;

	for (i = 0; i < OTX2_CPT_MAX_ENGINE_GROUPS; i++) {
		if (!eng_grps->grp[i].is_enabled)
			continue;
		if (eng_grps->grp[i].ucode[0].type &&
		    eng_grps->grp[i].ucode[1].type)
			continue;
		if (grp->idx == i)
			continue;
		if (!strncasecmp(eng_grps->grp[i].ucode[0].ver_str,
				 grp->ucode[0].ver_str,
				 OTX2_CPT_UCODE_VER_STR_SZ))
			return &eng_grps->grp[i];
	}

	return NULL;
}

static struct otx2_cpt_eng_grp_info *find_unused_eng_grp(
					struct otx2_cpt_eng_grps *eng_grps)
{
	int i;

	for (i = 0; i < OTX2_CPT_MAX_ENGINE_GROUPS; i++) {
		if (!eng_grps->grp[i].is_enabled)
			return &eng_grps->grp[i];
	}
	return NULL;
}

static int eng_grp_update_masks(struct device *dev,
				struct otx2_cpt_eng_grp_info *eng_grp)
{
	struct otx2_cpt_engs_rsvd *engs, *mirrored_engs;
	struct otx2_cpt_bitmap tmp_bmap = { {0} };
	int i, j, cnt, max_cnt;
	int bit;

	for (i = 0; i < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
		engs = &eng_grp->engs[i];
		if (!engs->type)
			continue;
		if (engs->count <= 0)
			continue;

		switch (engs->type) {
		case OTX2_CPT_SE_TYPES:
			max_cnt = eng_grp->g->avail.max_se_cnt;
			break;

		case OTX2_CPT_IE_TYPES:
			max_cnt = eng_grp->g->avail.max_ie_cnt;
			break;

		case OTX2_CPT_AE_TYPES:
			max_cnt = eng_grp->g->avail.max_ae_cnt;
			break;

		default:
			dev_err(dev, "Invalid engine type %d\n", engs->type);
			return -EINVAL;
		}

		cnt = engs->count;
		WARN_ON(engs->offset + max_cnt > OTX2_CPT_MAX_ENGINES);
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

	for (i = 0; i < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
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
			       struct otx2_cpt_eng_grp_info *eng_grp)
{
	int ret;

	if (!eng_grp->is_enabled)
		return 0;

	if (eng_grp->mirror.ref_count)
		return -EINVAL;

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

	eng_grp->is_enabled = false;

	return 0;
}

static void update_ucode_ptrs(struct otx2_cpt_eng_grp_info *eng_grp)
{
	struct otx2_cpt_ucode *ucode;

	if (eng_grp->mirror.is_ena)
		ucode = &eng_grp->g->grp[eng_grp->mirror.idx].ucode[0];
	else
		ucode = &eng_grp->ucode[0];
	WARN_ON(!eng_grp->engs[0].type);
	eng_grp->engs[0].ucode = ucode;

	if (eng_grp->engs[1].type) {
		if (is_2nd_ucode_used(eng_grp))
			eng_grp->engs[1].ucode = &eng_grp->ucode[1];
		else
			eng_grp->engs[1].ucode = ucode;
	}
}

static int create_engine_group(struct device *dev,
			       struct otx2_cpt_eng_grps *eng_grps,
			       struct otx2_cpt_engines *engs, int ucodes_cnt,
			       void *ucode_data[], int is_print)
{
	struct otx2_cpt_eng_grp_info *mirrored_eng_grp;
	struct otx2_cpt_eng_grp_info *eng_grp;
	struct otx2_cpt_uc_info_t *uc_info;
	int i, ret = 0;

	/* Find engine group which is not used */
	eng_grp = find_unused_eng_grp(eng_grps);
	if (!eng_grp) {
		dev_err(dev, "Error all engine groups are being used\n");
		return -ENOSPC;
	}
	/* Load ucode */
	for (i = 0; i < ucodes_cnt; i++) {
		uc_info = (struct otx2_cpt_uc_info_t *) ucode_data[i];
		eng_grp->ucode[i] = uc_info->ucode;
		ret = copy_ucode_to_dma_mem(dev, &eng_grp->ucode[i],
					    uc_info->fw->data);
		if (ret)
			goto unload_ucode;
	}

	/* Check if this group mirrors another existing engine group */
	mirrored_eng_grp = find_mirrored_eng_grp(eng_grp);
	if (mirrored_eng_grp) {
		/* Setup mirroring */
		setup_eng_grp_mirroring(eng_grp, mirrored_eng_grp);

		/*
		 * Update count of requested engines because some
		 * of them might be shared with mirrored group
		 */
		update_requested_engs(mirrored_eng_grp, engs, ucodes_cnt);
	}
	ret = reserve_engines(dev, eng_grp, engs, ucodes_cnt);
	if (ret)
		goto unload_ucode;

	/* Update ucode pointers used by engines */
	update_ucode_ptrs(eng_grp);

	/* Update engine masks used by this group */
	ret = eng_grp_update_masks(dev, eng_grp);
	if (ret)
		goto release_engs;

	/* Enable engine group */
	ret = enable_eng_grp(eng_grp, eng_grps->obj);
	if (ret)
		goto release_engs;

	/*
	 * If this engine group mirrors another engine group
	 * then we need to unload ucode as we will use ucode
	 * from mirrored engine group
	 */
	if (eng_grp->mirror.is_ena)
		ucode_unload(dev, &eng_grp->ucode[0]);

	eng_grp->is_enabled = true;

	if (!is_print)
		return 0;

	if (mirrored_eng_grp)
		dev_info(dev,
			 "Engine_group%d: reuse microcode %s from group %d\n",
			 eng_grp->idx, mirrored_eng_grp->ucode[0].ver_str,
			 mirrored_eng_grp->idx);
	else
		dev_info(dev, "Engine_group%d: microcode loaded %s\n",
			 eng_grp->idx, eng_grp->ucode[0].ver_str);
	if (is_2nd_ucode_used(eng_grp))
		dev_info(dev, "Engine_group%d: microcode loaded %s\n",
			 eng_grp->idx, eng_grp->ucode[1].ver_str);

	return 0;

release_engs:
	release_engines(dev, eng_grp);
unload_ucode:
	ucode_unload(dev, &eng_grp->ucode[0]);
	ucode_unload(dev, &eng_grp->ucode[1]);
	return ret;
}

static void delete_engine_grps(struct pci_dev *pdev,
			       struct otx2_cpt_eng_grps *eng_grps)
{
	int i;

	/* First delete all mirroring engine groups */
	for (i = 0; i < OTX2_CPT_MAX_ENGINE_GROUPS; i++)
		if (eng_grps->grp[i].mirror.is_ena)
			delete_engine_group(&pdev->dev, &eng_grps->grp[i]);

	/* Delete remaining engine groups */
	for (i = 0; i < OTX2_CPT_MAX_ENGINE_GROUPS; i++)
		delete_engine_group(&pdev->dev, &eng_grps->grp[i]);
}

int otx2_cpt_get_eng_grp(struct otx2_cpt_eng_grps *eng_grps, int eng_type)
{

	int eng_grp_num = OTX2_CPT_INVALID_CRYPTO_ENG_GRP;
	struct otx2_cpt_eng_grp_info *grp;
	int i;

	for (i = 0; i < OTX2_CPT_MAX_ENGINE_GROUPS; i++) {
		grp = &eng_grps->grp[i];
		if (!grp->is_enabled)
			continue;

		if (eng_type == OTX2_CPT_SE_TYPES) {
			if (eng_grp_has_eng_type(grp, eng_type) &&
			    !eng_grp_has_eng_type(grp, OTX2_CPT_IE_TYPES)) {
				eng_grp_num = i;
				break;
			}
		} else {
			if (eng_grp_has_eng_type(grp, eng_type)) {
				eng_grp_num = i;
				break;
			}
		}
	}
	return eng_grp_num;
}

int otx2_cpt_create_eng_grps(struct otx2_cptpf_dev *cptpf,
			     struct otx2_cpt_eng_grps *eng_grps)
{
	struct otx2_cpt_uc_info_t *uc_info[OTX2_CPT_MAX_ETYPES_PER_GRP] = {  };
	struct otx2_cpt_engines engs[OTX2_CPT_MAX_ETYPES_PER_GRP] = { {0} };
	struct pci_dev *pdev = cptpf->pdev;
	struct fw_info_t fw_info;
	int ret = 0;

	mutex_lock(&eng_grps->lock);
	/*
	 * We don't create engine groups if it was already
	 * made (when user enabled VFs for the first time)
	 */
	if (eng_grps->is_grps_created)
		goto unlock;

	ret = cpt_ucode_load_fw(pdev, &fw_info);
	if (ret)
		goto unlock;

	/*
	 * Create engine group with SE engines for kernel
	 * crypto functionality (symmetric crypto)
	 */
	uc_info[0] = get_ucode(&fw_info, OTX2_CPT_SE_TYPES);
	if (uc_info[0] == NULL) {
		dev_err(&pdev->dev, "Unable to find firmware for SE\n");
		ret = -EINVAL;
		goto release_fw;
	}
	engs[0].type = OTX2_CPT_SE_TYPES;
	engs[0].count = eng_grps->avail.max_se_cnt;

	ret = create_engine_group(&pdev->dev, eng_grps, engs, 1,
				  (void **) uc_info, 1);
	if (ret)
		goto release_fw;

	/*
	 * Create engine group with SE+IE engines for IPSec.
	 * All SE engines will be shared with engine group 0.
	 */
	uc_info[0] = get_ucode(&fw_info, OTX2_CPT_SE_TYPES);
	uc_info[1] = get_ucode(&fw_info, OTX2_CPT_IE_TYPES);

	if (uc_info[1] == NULL) {
		dev_err(&pdev->dev, "Unable to find firmware for IE");
		ret = -EINVAL;
		goto delete_eng_grp;
	}
	engs[0].type = OTX2_CPT_SE_TYPES;
	engs[0].count = eng_grps->avail.max_se_cnt;
	engs[1].type = OTX2_CPT_IE_TYPES;
	engs[1].count = eng_grps->avail.max_ie_cnt;

	ret = create_engine_group(&pdev->dev, eng_grps, engs, 2,
				  (void **) uc_info, 1);
	if (ret)
		goto delete_eng_grp;

	/*
	 * Create engine group with AE engines for asymmetric
	 * crypto functionality.
	 */
	uc_info[0] = get_ucode(&fw_info, OTX2_CPT_AE_TYPES);
	if (uc_info[0] == NULL) {
		dev_err(&pdev->dev, "Unable to find firmware for AE");
		ret = -EINVAL;
		goto delete_eng_grp;
	}
	engs[0].type = OTX2_CPT_AE_TYPES;
	engs[0].count = eng_grps->avail.max_ae_cnt;

	ret = create_engine_group(&pdev->dev, eng_grps, engs, 1,
				  (void **) uc_info, 1);
	if (ret)
		goto delete_eng_grp;

	eng_grps->is_grps_created = true;

	cpt_ucode_release_fw(&fw_info);

	if (is_dev_otx2(pdev))
		goto unlock;
	/*
	 * Configure engine group mask to allow context prefetching
	 * for the groups.
	 */
	otx2_cpt_write_af_reg(&cptpf->afpf_mbox, pdev, CPT_AF_CTL,
			      OTX2_CPT_ALL_ENG_GRPS_MASK << 3 | BIT_ULL(16),
			      BLKADDR_CPT0);
	/*
	 * Set interval to periodically flush dirty data for the next
	 * CTX cache entry. Set the interval count to maximum supported
	 * value.
	 */
	otx2_cpt_write_af_reg(&cptpf->afpf_mbox, pdev, CPT_AF_CTX_FLUSH_TIMER,
			      CTX_FLUSH_TIMER_CNT, BLKADDR_CPT0);
	mutex_unlock(&eng_grps->lock);
	return 0;

delete_eng_grp:
	delete_engine_grps(pdev, eng_grps);
release_fw:
	cpt_ucode_release_fw(&fw_info);
unlock:
	mutex_unlock(&eng_grps->lock);
	return ret;
}

static int cptx_disable_all_cores(struct otx2_cptpf_dev *cptpf, int total_cores,
				  int blkaddr)
{
	int timeout = 10, ret;
	int i, busy;
	u64 reg;

	/* Disengage the cores from groups */
	for (i = 0; i < total_cores; i++) {
		ret = otx2_cpt_add_write_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
						CPT_AF_EXEX_CTL2(i), 0x0,
						blkaddr);
		if (ret)
			return ret;

		cptpf->eng_grps.eng_ref_cnt[i] = 0;
	}
	ret = otx2_cpt_send_af_reg_requests(&cptpf->afpf_mbox, cptpf->pdev);
	if (ret)
		return ret;

	/* Wait for cores to become idle */
	do {
		busy = 0;
		usleep_range(10000, 20000);
		if (timeout-- < 0)
			return -EBUSY;

		for (i = 0; i < total_cores; i++) {
			ret = otx2_cpt_read_af_reg(&cptpf->afpf_mbox,
						   cptpf->pdev,
						   CPT_AF_EXEX_STS(i), &reg,
						   blkaddr);
			if (ret)
				return ret;

			if (reg & 0x1) {
				busy = 1;
				break;
			}
		}
	} while (busy);

	/* Disable the cores */
	for (i = 0; i < total_cores; i++) {
		ret = otx2_cpt_add_write_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
						CPT_AF_EXEX_CTL(i), 0x0,
						blkaddr);
		if (ret)
			return ret;
	}
	return otx2_cpt_send_af_reg_requests(&cptpf->afpf_mbox, cptpf->pdev);
}

int otx2_cpt_disable_all_cores(struct otx2_cptpf_dev *cptpf)
{
	int total_cores, ret;

	total_cores = cptpf->eng_grps.avail.max_se_cnt +
		      cptpf->eng_grps.avail.max_ie_cnt +
		      cptpf->eng_grps.avail.max_ae_cnt;

	if (cptpf->has_cpt1) {
		ret = cptx_disable_all_cores(cptpf, total_cores, BLKADDR_CPT1);
		if (ret)
			return ret;
	}
	return cptx_disable_all_cores(cptpf, total_cores, BLKADDR_CPT0);
}

void otx2_cpt_cleanup_eng_grps(struct pci_dev *pdev,
			       struct otx2_cpt_eng_grps *eng_grps)
{
	struct otx2_cpt_eng_grp_info *grp;
	int i, j;

	mutex_lock(&eng_grps->lock);
	delete_engine_grps(pdev, eng_grps);
	/* Release memory */
	for (i = 0; i < OTX2_CPT_MAX_ENGINE_GROUPS; i++) {
		grp = &eng_grps->grp[i];
		for (j = 0; j < OTX2_CPT_MAX_ETYPES_PER_GRP; j++) {
			kfree(grp->engs[j].bmap);
			grp->engs[j].bmap = NULL;
		}
	}
	mutex_unlock(&eng_grps->lock);
}

int otx2_cpt_init_eng_grps(struct pci_dev *pdev,
			   struct otx2_cpt_eng_grps *eng_grps)
{
	struct otx2_cpt_eng_grp_info *grp;
	int i, j, ret;

	mutex_init(&eng_grps->lock);
	eng_grps->obj = pci_get_drvdata(pdev);
	eng_grps->avail.se_cnt = eng_grps->avail.max_se_cnt;
	eng_grps->avail.ie_cnt = eng_grps->avail.max_ie_cnt;
	eng_grps->avail.ae_cnt = eng_grps->avail.max_ae_cnt;

	eng_grps->engs_num = eng_grps->avail.max_se_cnt +
			     eng_grps->avail.max_ie_cnt +
			     eng_grps->avail.max_ae_cnt;
	if (eng_grps->engs_num > OTX2_CPT_MAX_ENGINES) {
		dev_err(&pdev->dev,
			"Number of engines %d > than max supported %d\n",
			eng_grps->engs_num, OTX2_CPT_MAX_ENGINES);
		ret = -EINVAL;
		goto cleanup_eng_grps;
	}

	for (i = 0; i < OTX2_CPT_MAX_ENGINE_GROUPS; i++) {
		grp = &eng_grps->grp[i];
		grp->g = eng_grps;
		grp->idx = i;

		for (j = 0; j < OTX2_CPT_MAX_ETYPES_PER_GRP; j++) {
			grp->engs[j].bmap =
				kcalloc(BITS_TO_LONGS(eng_grps->engs_num),
					sizeof(long), GFP_KERNEL);
			if (!grp->engs[j].bmap) {
				ret = -ENOMEM;
				goto cleanup_eng_grps;
			}
		}
	}
	return 0;

cleanup_eng_grps:
	otx2_cpt_cleanup_eng_grps(pdev, eng_grps);
	return ret;
}

static int create_eng_caps_discovery_grps(struct pci_dev *pdev,
					  struct otx2_cpt_eng_grps *eng_grps)
{
	struct otx2_cpt_uc_info_t *uc_info[OTX2_CPT_MAX_ETYPES_PER_GRP] = {  };
	struct otx2_cpt_engines engs[OTX2_CPT_MAX_ETYPES_PER_GRP] = { {0} };
	struct fw_info_t fw_info;
	int ret;

	mutex_lock(&eng_grps->lock);
	ret = cpt_ucode_load_fw(pdev, &fw_info);
	if (ret) {
		mutex_unlock(&eng_grps->lock);
		return ret;
	}

	uc_info[0] = get_ucode(&fw_info, OTX2_CPT_AE_TYPES);
	if (uc_info[0] == NULL) {
		dev_err(&pdev->dev, "Unable to find firmware for AE\n");
		ret = -EINVAL;
		goto release_fw;
	}
	engs[0].type = OTX2_CPT_AE_TYPES;
	engs[0].count = 2;

	ret = create_engine_group(&pdev->dev, eng_grps, engs, 1,
				  (void **) uc_info, 0);
	if (ret)
		goto release_fw;

	uc_info[0] = get_ucode(&fw_info, OTX2_CPT_SE_TYPES);
	if (uc_info[0] == NULL) {
		dev_err(&pdev->dev, "Unable to find firmware for SE\n");
		ret = -EINVAL;
		goto delete_eng_grp;
	}
	engs[0].type = OTX2_CPT_SE_TYPES;
	engs[0].count = 2;

	ret = create_engine_group(&pdev->dev, eng_grps, engs, 1,
				  (void **) uc_info, 0);
	if (ret)
		goto delete_eng_grp;

	uc_info[0] = get_ucode(&fw_info, OTX2_CPT_IE_TYPES);
	if (uc_info[0] == NULL) {
		dev_err(&pdev->dev, "Unable to find firmware for IE\n");
		ret = -EINVAL;
		goto delete_eng_grp;
	}
	engs[0].type = OTX2_CPT_IE_TYPES;
	engs[0].count = 2;

	ret = create_engine_group(&pdev->dev, eng_grps, engs, 1,
				  (void **) uc_info, 0);
	if (ret)
		goto delete_eng_grp;

	cpt_ucode_release_fw(&fw_info);
	mutex_unlock(&eng_grps->lock);
	return 0;

delete_eng_grp:
	delete_engine_grps(pdev, eng_grps);
release_fw:
	cpt_ucode_release_fw(&fw_info);
	mutex_unlock(&eng_grps->lock);
	return ret;
}

/*
 * Get CPT HW capabilities using LOAD_FVC operation.
 */
int otx2_cpt_discover_eng_capabilities(struct otx2_cptpf_dev *cptpf)
{
	struct otx2_cptlfs_info *lfs = &cptpf->lfs;
	struct otx2_cpt_iq_command iq_cmd;
	union otx2_cpt_opcode opcode;
	union otx2_cpt_res_s *result;
	union otx2_cpt_inst_s inst;
	dma_addr_t rptr_baddr;
	struct pci_dev *pdev;
	u32 len, compl_rlen;
	int ret, etype;
	void *rptr;

	/*
	 * We don't get capabilities if it was already done
	 * (when user enabled VFs for the first time)
	 */
	if (cptpf->is_eng_caps_discovered)
		return 0;

	pdev = cptpf->pdev;
	/*
	 * Create engine groups for each type to submit LOAD_FVC op and
	 * get engine's capabilities.
	 */
	ret = create_eng_caps_discovery_grps(pdev, &cptpf->eng_grps);
	if (ret)
		goto delete_grps;

	lfs->pdev = pdev;
	lfs->reg_base = cptpf->reg_base;
	lfs->mbox = &cptpf->afpf_mbox;
	lfs->blkaddr = BLKADDR_CPT0;
	ret = otx2_cptlf_init(&cptpf->lfs, OTX2_CPT_ALL_ENG_GRPS_MASK,
			      OTX2_CPT_QUEUE_HI_PRIO, 1);
	if (ret)
		goto delete_grps;

	compl_rlen = ALIGN(sizeof(union otx2_cpt_res_s), OTX2_CPT_DMA_MINALIGN);
	len = compl_rlen + LOADFVC_RLEN;

	result = kzalloc(len, GFP_KERNEL);
	if (!result) {
		ret = -ENOMEM;
		goto lf_cleanup;
	}
	rptr_baddr = dma_map_single(&pdev->dev, (void *)result, len,
				    DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&pdev->dev, rptr_baddr)) {
		dev_err(&pdev->dev, "DMA mapping failed\n");
		ret = -EFAULT;
		goto free_result;
	}
	rptr = (u8 *)result + compl_rlen;

	/* Fill in the command */
	opcode.s.major = LOADFVC_MAJOR_OP;
	opcode.s.minor = LOADFVC_MINOR_OP;

	iq_cmd.cmd.u = 0;
	iq_cmd.cmd.s.opcode = cpu_to_be16(opcode.flags);

	/* 64-bit swap for microcode data reads, not needed for addresses */
	cpu_to_be64s(&iq_cmd.cmd.u);
	iq_cmd.dptr = 0;
	iq_cmd.rptr = rptr_baddr + compl_rlen;
	iq_cmd.cptr.u = 0;

	for (etype = 1; etype < OTX2_CPT_MAX_ENG_TYPES; etype++) {
		result->s.compcode = OTX2_CPT_COMPLETION_CODE_INIT;
		iq_cmd.cptr.s.grp = otx2_cpt_get_eng_grp(&cptpf->eng_grps,
							 etype);
		otx2_cpt_fill_inst(&inst, &iq_cmd, rptr_baddr);
		lfs->ops->send_cmd(&inst, 1, &cptpf->lfs.lf[0]);

		while (lfs->ops->cpt_get_compcode(result) ==
						OTX2_CPT_COMPLETION_CODE_INIT)
			cpu_relax();

		cptpf->eng_caps[etype].u = be64_to_cpup(rptr);
	}
	dma_unmap_single(&pdev->dev, rptr_baddr, len, DMA_BIDIRECTIONAL);
	cptpf->is_eng_caps_discovered = true;

free_result:
	kfree(result);
lf_cleanup:
	otx2_cptlf_shutdown(&cptpf->lfs);
delete_grps:
	delete_engine_grps(pdev, &cptpf->eng_grps);

	return ret;
}

int otx2_cpt_dl_custom_egrp_create(struct otx2_cptpf_dev *cptpf,
				   struct devlink_param_gset_ctx *ctx)
{
	struct otx2_cpt_engines engs[OTX2_CPT_MAX_ETYPES_PER_GRP] = { { 0 } };
	struct otx2_cpt_uc_info_t *uc_info[OTX2_CPT_MAX_ETYPES_PER_GRP] = {};
	struct otx2_cpt_eng_grps *eng_grps = &cptpf->eng_grps;
	char *ucode_filename[OTX2_CPT_MAX_ETYPES_PER_GRP];
	char tmp_buf[OTX2_CPT_NAME_LENGTH] = { 0 };
	struct device *dev = &cptpf->pdev->dev;
	char *start, *val, *err_msg, *tmp;
	int grp_idx = 0, ret = -EINVAL;
	bool has_se, has_ie, has_ae;
	struct fw_info_t fw_info;
	int ucode_idx = 0;

	if (!eng_grps->is_grps_created) {
		dev_err(dev, "Not allowed before creating the default groups\n");
		return -EINVAL;
	}
	err_msg = "Invalid engine group format";
	strscpy(tmp_buf, ctx->val.vstr, strlen(ctx->val.vstr) + 1);
	start = tmp_buf;

	has_se = has_ie = has_ae = false;

	for (;;) {
		val = strsep(&start, ";");
		if (!val)
			break;
		val = strim(val);
		if (!*val)
			continue;

		if (!strncasecmp(val, "se", 2) && strchr(val, ':')) {
			if (has_se || ucode_idx)
				goto err_print;
			tmp = strim(strsep(&val, ":"));
			if (!val)
				goto err_print;
			if (strlen(tmp) != 2)
				goto err_print;
			if (kstrtoint(strim(val), 10, &engs[grp_idx].count))
				goto err_print;
			engs[grp_idx++].type = OTX2_CPT_SE_TYPES;
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
			engs[grp_idx++].type = OTX2_CPT_AE_TYPES;
			has_ae = true;
		} else if (!strncasecmp(val, "ie", 2) && strchr(val, ':')) {
			if (has_ie || ucode_idx)
				goto err_print;
			tmp = strim(strsep(&val, ":"));
			if (!val)
				goto err_print;
			if (strlen(tmp) != 2)
				goto err_print;
			if (kstrtoint(strim(val), 10, &engs[grp_idx].count))
				goto err_print;
			engs[grp_idx++].type = OTX2_CPT_IE_TYPES;
			has_ie = true;
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
	if (!(grp_idx && ucode_idx))
		goto err_print;

	if (ucode_idx > 1 && grp_idx < 2)
		goto err_print;

	if (grp_idx > OTX2_CPT_MAX_ETYPES_PER_GRP) {
		err_msg = "Error max 2 engine types can be attached";
		goto err_print;
	}

	if (grp_idx > 1) {
		if ((engs[0].type + engs[1].type) !=
		    (OTX2_CPT_SE_TYPES + OTX2_CPT_IE_TYPES)) {
			err_msg = "Only combination of SE+IE engines is allowed";
			goto err_print;
		}
		/* Keep SE engines at zero index */
		if (engs[1].type == OTX2_CPT_SE_TYPES)
			swap(engs[0], engs[1]);
	}
	mutex_lock(&eng_grps->lock);

	if (cptpf->enabled_vfs) {
		dev_err(dev, "Disable VFs before modifying engine groups\n");
		ret = -EACCES;
		goto err_unlock;
	}
	INIT_LIST_HEAD(&fw_info.ucodes);
	ret = load_fw(dev, &fw_info, ucode_filename[0]);
	if (ret) {
		dev_err(dev, "Unable to load firmware %s\n", ucode_filename[0]);
		goto err_unlock;
	}
	if (ucode_idx > 1) {
		ret = load_fw(dev, &fw_info, ucode_filename[1]);
		if (ret) {
			dev_err(dev, "Unable to load firmware %s\n",
				ucode_filename[1]);
			goto release_fw;
		}
	}
	uc_info[0] = get_ucode(&fw_info, engs[0].type);
	if (uc_info[0] == NULL) {
		dev_err(dev, "Unable to find firmware for %s\n",
			get_eng_type_str(engs[0].type));
		ret = -EINVAL;
		goto release_fw;
	}
	if (ucode_idx > 1) {
		uc_info[1] = get_ucode(&fw_info, engs[1].type);
		if (uc_info[1] == NULL) {
			dev_err(dev, "Unable to find firmware for %s\n",
				get_eng_type_str(engs[1].type));
			ret = -EINVAL;
			goto release_fw;
		}
	}
	ret = create_engine_group(dev, eng_grps, engs, grp_idx,
				  (void **)uc_info, 1);

release_fw:
	cpt_ucode_release_fw(&fw_info);
err_unlock:
	mutex_unlock(&eng_grps->lock);
	return ret;
err_print:
	dev_err(dev, "%s\n", err_msg);
	return ret;
}

int otx2_cpt_dl_custom_egrp_delete(struct otx2_cptpf_dev *cptpf,
				   struct devlink_param_gset_ctx *ctx)
{
	struct otx2_cpt_eng_grps *eng_grps = &cptpf->eng_grps;
	struct device *dev = &cptpf->pdev->dev;
	char *tmp, *err_msg;
	int egrp;
	int ret;

	err_msg = "Invalid input string format(ex: egrp:0)";
	if (strncasecmp(ctx->val.vstr, "egrp", 4))
		goto err_print;
	tmp = ctx->val.vstr;
	strsep(&tmp, ":");
	if (!tmp)
		goto err_print;
	if (kstrtoint(tmp, 10, &egrp))
		goto err_print;

	if (egrp < 0 || egrp >= OTX2_CPT_MAX_ENGINE_GROUPS) {
		dev_err(dev, "Invalid engine group %d", egrp);
		return -EINVAL;
	}
	if (!eng_grps->grp[egrp].is_enabled) {
		dev_err(dev, "Error engine_group%d is not configured", egrp);
		return -EINVAL;
	}
	mutex_lock(&eng_grps->lock);
	ret = delete_engine_group(dev, &eng_grps->grp[egrp]);
	mutex_unlock(&eng_grps->lock);

	return ret;

err_print:
	dev_err(dev, "%s\n", err_msg);
	return -EINVAL;
}

static void get_engs_info(struct otx2_cpt_eng_grp_info *eng_grp, char *buf,
			  int size, int idx)
{
	struct otx2_cpt_engs_rsvd *mirrored_engs = NULL;
	struct otx2_cpt_engs_rsvd *engs;
	int len, i;

	buf[0] = '\0';
	for (i = 0; i < OTX2_CPT_MAX_ETYPES_PER_GRP; i++) {
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
			scnprintf(buf + len, size - len, ", ");
		}

		len = strlen(buf);
		scnprintf(buf + len, size - len, "%d %s ",
			  mirrored_engs ? engs->count + mirrored_engs->count :
					  engs->count,
			  get_eng_type_str(engs->type));
		if (mirrored_engs) {
			len = strlen(buf);
			scnprintf(buf + len, size - len,
				  "(%d shared with engine_group%d) ",
				  engs->count <= 0 ?
					  engs->count + mirrored_engs->count :
					  mirrored_engs->count,
				  eng_grp->mirror.idx);
		}
	}
}

void otx2_cpt_print_uc_dbg_info(struct otx2_cptpf_dev *cptpf)
{
	struct otx2_cpt_eng_grps *eng_grps = &cptpf->eng_grps;
	struct otx2_cpt_eng_grp_info *mirrored_grp;
	char engs_info[2 * OTX2_CPT_NAME_LENGTH];
	struct otx2_cpt_eng_grp_info *grp;
	struct otx2_cpt_engs_rsvd *engs;
	u32 mask[4];
	int i, j;

	pr_debug("Engine groups global info");
	pr_debug("max SE %d, max IE %d, max AE %d", eng_grps->avail.max_se_cnt,
		 eng_grps->avail.max_ie_cnt, eng_grps->avail.max_ae_cnt);
	pr_debug("free SE %d", eng_grps->avail.se_cnt);
	pr_debug("free IE %d", eng_grps->avail.ie_cnt);
	pr_debug("free AE %d", eng_grps->avail.ae_cnt);

	for (i = 0; i < OTX2_CPT_MAX_ENGINE_GROUPS; i++) {
		grp = &eng_grps->grp[i];
		pr_debug("engine_group%d, state %s", i,
			 grp->is_enabled ? "enabled" : "disabled");
		if (grp->is_enabled) {
			mirrored_grp = &eng_grps->grp[grp->mirror.idx];
			pr_debug("Ucode0 filename %s, version %s",
				 grp->mirror.is_ena ?
					 mirrored_grp->ucode[0].filename :
					 grp->ucode[0].filename,
				 grp->mirror.is_ena ?
					 mirrored_grp->ucode[0].ver_str :
					 grp->ucode[0].ver_str);
			if (is_2nd_ucode_used(grp))
				pr_debug("Ucode1 filename %s, version %s",
					 grp->ucode[1].filename,
					 grp->ucode[1].ver_str);
		}

		for (j = 0; j < OTX2_CPT_MAX_ETYPES_PER_GRP; j++) {
			engs = &grp->engs[j];
			if (engs->type) {
				get_engs_info(grp, engs_info,
					      2 * OTX2_CPT_NAME_LENGTH, j);
				pr_debug("Slot%d: %s", j, engs_info);
				bitmap_to_arr32(mask, engs->bmap,
						eng_grps->engs_num);
				if (is_dev_otx2(cptpf->pdev))
					pr_debug("Mask: %8.8x %8.8x %8.8x %8.8x",
						 mask[3], mask[2], mask[1],
						 mask[0]);
				else
					pr_debug("Mask: %8.8x %8.8x %8.8x %8.8x %8.8x",
						 mask[4], mask[3], mask[2], mask[1],
						 mask[0]);
			}
		}
	}
}
