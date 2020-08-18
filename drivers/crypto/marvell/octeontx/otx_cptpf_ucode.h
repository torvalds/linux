/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OTX_CPTPF_UCODE_H
#define __OTX_CPTPF_UCODE_H

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/module.h>
#include "otx_cpt_hw_types.h"

/* CPT ucode name maximum length */
#define OTX_CPT_UCODE_NAME_LENGTH	64
/*
 * On OcteonTX 83xx platform, only one type of engines is allowed to be
 * attached to an engine group.
 */
#define OTX_CPT_MAX_ETYPES_PER_GRP	1

/* Default tar archive file names */
#define OTX_CPT_UCODE_TAR_FILE_NAME	"cpt8x-mc.tar"

/* CPT ucode alignment */
#define OTX_CPT_UCODE_ALIGNMENT		128

/* CPT ucode signature size */
#define OTX_CPT_UCODE_SIGN_LEN		256

/* Microcode version string length */
#define OTX_CPT_UCODE_VER_STR_SZ	44

/* Maximum number of supported engines/cores on OcteonTX 83XX platform */
#define OTX_CPT_MAX_ENGINES		64

#define OTX_CPT_ENGS_BITMASK_LEN	(OTX_CPT_MAX_ENGINES/(BITS_PER_BYTE * \
					 sizeof(unsigned long)))

/* Microcode types */
enum otx_cpt_ucode_type {
	OTX_CPT_AE_UC_TYPE =	1,  /* AE-MAIN */
	OTX_CPT_SE_UC_TYPE1 =	20, /* SE-MAIN - combination of 21 and 22 */
	OTX_CPT_SE_UC_TYPE2 =	21, /* Fast Path IPSec + AirCrypto */
	OTX_CPT_SE_UC_TYPE3 =	22, /*
				     * Hash + HMAC + FlexiCrypto + RNG + Full
				     * Feature IPSec + AirCrypto + Kasumi
				     */
};

struct otx_cpt_bitmap {
	unsigned long bits[OTX_CPT_ENGS_BITMASK_LEN];
	int size;
};

struct otx_cpt_engines {
	int type;
	int count;
};

/* Microcode version number */
struct otx_cpt_ucode_ver_num {
	u8 nn;
	u8 xx;
	u8 yy;
	u8 zz;
};

struct otx_cpt_ucode_hdr {
	struct otx_cpt_ucode_ver_num ver_num;
	u8 ver_str[OTX_CPT_UCODE_VER_STR_SZ];
	__be32 code_length;
	u32 padding[3];
};

struct otx_cpt_ucode {
	u8 ver_str[OTX_CPT_UCODE_VER_STR_SZ];/*
					      * ucode version in readable format
					      */
	struct otx_cpt_ucode_ver_num ver_num;/* ucode version number */
	char filename[OTX_CPT_UCODE_NAME_LENGTH];	 /* ucode filename */
	dma_addr_t dma;		/* phys address of ucode image */
	dma_addr_t align_dma;	/* aligned phys address of ucode image */
	void *va;		/* virt address of ucode image */
	void *align_va;		/* aligned virt address of ucode image */
	u32 size;		/* ucode image size */
	int type;		/* ucode image type SE or AE */
};

struct tar_ucode_info_t {
	struct list_head list;
	struct otx_cpt_ucode ucode;/* microcode information */
	const u8 *ucode_ptr;	/* pointer to microcode in tar archive */
};

/* Maximum and current number of engines available for all engine groups */
struct otx_cpt_engs_available {
	int max_se_cnt;
	int max_ae_cnt;
	int se_cnt;
	int ae_cnt;
};

/* Engines reserved to an engine group */
struct otx_cpt_engs_rsvd {
	int type;	/* engine type */
	int count;	/* number of engines attached */
	int offset;     /* constant offset of engine type in the bitmap */
	unsigned long *bmap;		/* attached engines bitmap */
	struct otx_cpt_ucode *ucode;	/* ucode used by these engines */
};

struct otx_cpt_mirror_info {
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

struct otx_cpt_eng_grp_info {
	struct otx_cpt_eng_grps *g; /* pointer to engine_groups structure */
	struct device_attribute info_attr; /* group info entry attr */
	/* engines attached */
	struct otx_cpt_engs_rsvd engs[OTX_CPT_MAX_ETYPES_PER_GRP];
	/* Microcode information */
	struct otx_cpt_ucode ucode[OTX_CPT_MAX_ETYPES_PER_GRP];
	/* sysfs info entry name */
	char sysfs_info_name[OTX_CPT_UCODE_NAME_LENGTH];
	/* engine group mirroring information */
	struct otx_cpt_mirror_info mirror;
	int idx;	 /* engine group index */
	bool is_enabled; /*
			  * is engine group enabled, engine group is enabled
			  * when it has engines attached and ucode loaded
			  */
};

struct otx_cpt_eng_grps {
	struct otx_cpt_eng_grp_info grp[OTX_CPT_MAX_ENGINE_GROUPS];
	struct device_attribute ucode_load_attr;/* ucode load attr */
	struct otx_cpt_engs_available avail;
	struct mutex lock;
	void *obj;
	int engs_num;			/* total number of engines supported */
	int eng_types_supported;	/* engine types supported SE, AE */
	u8 eng_ref_cnt[OTX_CPT_MAX_ENGINES];/* engines reference count */
	bool is_ucode_load_created;	/* is ucode_load sysfs entry created */
	bool is_first_try; /* is this first try to create kcrypto engine grp */
	bool is_rdonly;	/* do engine groups configuration can be modified */
};

int otx_cpt_init_eng_grps(struct pci_dev *pdev,
			  struct otx_cpt_eng_grps *eng_grps, int pf_type);
void otx_cpt_cleanup_eng_grps(struct pci_dev *pdev,
			      struct otx_cpt_eng_grps *eng_grps);
int otx_cpt_try_create_default_eng_grps(struct pci_dev *pdev,
					struct otx_cpt_eng_grps *eng_grps,
					int pf_type);
void otx_cpt_set_eng_grps_is_rdonly(struct otx_cpt_eng_grps *eng_grps,
				    bool is_rdonly);
int otx_cpt_uc_supports_eng_type(struct otx_cpt_ucode *ucode, int eng_type);
int otx_cpt_eng_grp_has_eng_type(struct otx_cpt_eng_grp_info *eng_grp,
				 int eng_type);

#endif /* __OTX_CPTPF_UCODE_H */
