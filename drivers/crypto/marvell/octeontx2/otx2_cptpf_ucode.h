/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPTPF_UCODE_H
#define __OTX2_CPTPF_UCODE_H

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/module.h>
#include "otx2_cpt_hw_types.h"
#include "otx2_cpt_common.h"

/*
 * On OcteonTX2 platform IPSec ucode can use both IE and SE engines therefore
 * IE and SE engines can be attached to the same engine group.
 */
#define OTX2_CPT_MAX_ETYPES_PER_GRP 2

/* CPT ucode signature size */
#define OTX2_CPT_UCODE_SIGN_LEN     256

/* Microcode version string length */
#define OTX2_CPT_UCODE_VER_STR_SZ   44

/* Maximum number of supported engines/cores on OcteonTX2/CN10K platform */
#define OTX2_CPT_MAX_ENGINES        144

#define OTX2_CPT_ENGS_BITMASK_LEN   BITS_TO_LONGS(OTX2_CPT_MAX_ENGINES)

#define OTX2_CPT_UCODE_SZ           (64 * 1024)

/* Microcode types */
enum otx2_cpt_ucode_type {
	OTX2_CPT_AE_UC_TYPE = 1,  /* AE-MAIN */
	OTX2_CPT_SE_UC_TYPE1 = 20,/* SE-MAIN - combination of 21 and 22 */
	OTX2_CPT_SE_UC_TYPE2 = 21,/* Fast Path IPSec + AirCrypto */
	OTX2_CPT_SE_UC_TYPE3 = 22,/*
				   * Hash + HMAC + FlexiCrypto + RNG +
				   * Full Feature IPSec + AirCrypto + Kasumi
				   */
	OTX2_CPT_IE_UC_TYPE1 = 30, /* IE-MAIN - combination of 31 and 32 */
	OTX2_CPT_IE_UC_TYPE2 = 31, /* Fast Path IPSec */
	OTX2_CPT_IE_UC_TYPE3 = 32, /*
				    * Hash + HMAC + FlexiCrypto + RNG +
				    * Full Future IPSec
				    */
};

struct otx2_cpt_bitmap {
	unsigned long bits[OTX2_CPT_ENGS_BITMASK_LEN];
	int size;
};

struct otx2_cpt_engines {
	int type;
	int count;
};

/* Microcode version number */
struct otx2_cpt_ucode_ver_num {
	u8 nn;
	u8 xx;
	u8 yy;
	u8 zz;
};

struct otx2_cpt_ucode_hdr {
	struct otx2_cpt_ucode_ver_num ver_num;
	u8 ver_str[OTX2_CPT_UCODE_VER_STR_SZ];
	__be32 code_length;
	u32 padding[3];
};

struct otx2_cpt_ucode {
	u8 ver_str[OTX2_CPT_UCODE_VER_STR_SZ];/*
					       * ucode version in readable
					       * format
					       */
	struct otx2_cpt_ucode_ver_num ver_num;/* ucode version number */
	char filename[OTX2_CPT_NAME_LENGTH];/* ucode filename */
	dma_addr_t dma;		/* phys address of ucode image */
	void *va;		/* virt address of ucode image */
	u32 size;		/* ucode image size */
	int type;		/* ucode image type SE, IE, AE or SE+IE */
};

struct otx2_cpt_uc_info_t {
	struct list_head list;
	struct otx2_cpt_ucode ucode;/* microcode information */
	const struct firmware *fw;
};

/* Maximum and current number of engines available for all engine groups */
struct otx2_cpt_engs_available {
	int max_se_cnt;
	int max_ie_cnt;
	int max_ae_cnt;
	int se_cnt;
	int ie_cnt;
	int ae_cnt;
};

/* Engines reserved to an engine group */
struct otx2_cpt_engs_rsvd {
	int type;	/* engine type */
	int count;	/* number of engines attached */
	int offset;     /* constant offset of engine type in the bitmap */
	unsigned long *bmap;		/* attached engines bitmap */
	struct otx2_cpt_ucode *ucode;	/* ucode used by these engines */
};

struct otx2_cpt_mirror_info {
	int is_ena;	/*
			 * is mirroring enabled, it is set only for engine
			 * group which mirrors another engine group
			 */
	int idx;	/*
			 * index of engine group which is mirrored by this
			 * group, set only for engine group which mirrors
			 * another group
			 */
	int ref_count;	/*
			 * number of times this engine group is mirrored by
			 * other groups, this is set only for engine group
			 * which is mirrored by other group(s)
			 */
};

struct otx2_cpt_eng_grp_info {
	struct otx2_cpt_eng_grps *g; /* pointer to engine_groups structure */
	/* engines attached */
	struct otx2_cpt_engs_rsvd engs[OTX2_CPT_MAX_ETYPES_PER_GRP];
	/* ucodes information */
	struct otx2_cpt_ucode ucode[OTX2_CPT_MAX_ETYPES_PER_GRP];
	/* engine group mirroring information */
	struct otx2_cpt_mirror_info mirror;
	int idx;	 /* engine group index */
	bool is_enabled; /*
			  * is engine group enabled, engine group is enabled
			  * when it has engines attached and ucode loaded
			  */
};

struct otx2_cpt_eng_grps {
	struct mutex lock;
	struct otx2_cpt_eng_grp_info grp[OTX2_CPT_MAX_ENGINE_GROUPS];
	struct otx2_cpt_engs_available avail;
	void *obj;			/* device specific data */
	int engs_num;			/* total number of engines supported */
	u8 eng_ref_cnt[OTX2_CPT_MAX_ENGINES];/* engines reference count */
	bool is_grps_created; /* Is the engine groups are already created */
};
struct otx2_cptpf_dev;
int otx2_cpt_init_eng_grps(struct pci_dev *pdev,
			   struct otx2_cpt_eng_grps *eng_grps);
void otx2_cpt_cleanup_eng_grps(struct pci_dev *pdev,
			       struct otx2_cpt_eng_grps *eng_grps);
int otx2_cpt_create_eng_grps(struct otx2_cptpf_dev *cptpf,
			     struct otx2_cpt_eng_grps *eng_grps);
int otx2_cpt_disable_all_cores(struct otx2_cptpf_dev *cptpf);
int otx2_cpt_get_eng_grp(struct otx2_cpt_eng_grps *eng_grps, int eng_type);
int otx2_cpt_discover_eng_capabilities(struct otx2_cptpf_dev *cptpf);
int otx2_cpt_dl_custom_egrp_create(struct otx2_cptpf_dev *cptpf,
				   struct devlink_param_gset_ctx *ctx);
int otx2_cpt_dl_custom_egrp_delete(struct otx2_cptpf_dev *cptpf,
				   struct devlink_param_gset_ctx *ctx);
void otx2_cpt_print_uc_dbg_info(struct otx2_cptpf_dev *cptpf);
struct otx2_cpt_engs_rsvd *find_engines_by_type(
					struct otx2_cpt_eng_grp_info *eng_grp,
					int eng_type);
#endif /* __OTX2_CPTPF_UCODE_H */
