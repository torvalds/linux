// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009-2017, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2019, Linaro Ltd.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>
#include <linux/string.h>
#include <linux/sys_soc.h>
#include <linux/types.h>
#include <soc/qcom/socinfo.h>

#include <asm/unaligned.h>

/*
 * SoC version type with major number in the upper 16 bits and minor
 * number in the lower 16 bits.
 */
#define SOCINFO_MAJOR(ver) (((ver) >> 16) & 0xffff)
#define SOCINFO_MINOR(ver) ((ver) & 0xffff)
#define SOCINFO_VERSION(maj, min)  ((((maj) & 0xffff) << 16)|((min) & 0xffff))

#define SMEM_SOCINFO_BUILD_ID_LENGTH           32
#define SMEM_SOCINFO_CHIP_ID_LENGTH            32

static uint32_t socinfo_format;
static const char *sku;

enum {
	HW_PLATFORM_UNKNOWN = 0,
	HW_PLATFORM_SURF    = 1,
	HW_PLATFORM_FFA     = 2,
	HW_PLATFORM_FLUID   = 3,
	HW_PLATFORM_SVLTE_FFA   = 4,
	HW_PLATFORM_SVLTE_SURF  = 5,
	HW_PLATFORM_MTP_MDM = 7,
	HW_PLATFORM_MTP  = 8,
	HW_PLATFORM_LIQUID  = 9,
	/* Dragonboard platform id is assigned as 10 in CDT */
	HW_PLATFORM_DRAGON      = 10,
	HW_PLATFORM_QRD = 11,
	HW_PLATFORM_HRD = 13,
	HW_PLATFORM_DTV = 14,
	HW_PLATFORM_RCM = 21,
	HW_PLATFORM_STP = 23,
	HW_PLATFORM_SBC = 24,
	HW_PLATFORM_ADP = 25,
	HW_PLATFORM_HDK = 31,
	HW_PLATFORM_ATP = 33,
	HW_PLATFORM_IDP = 34,
	HW_PLATFORM_INVALID
};

static const char * const hw_platform[] = {
	[HW_PLATFORM_UNKNOWN] = "Unknown",
	[HW_PLATFORM_SURF] = "Surf",
	[HW_PLATFORM_FFA] = "FFA",
	[HW_PLATFORM_FLUID] = "Fluid",
	[HW_PLATFORM_SVLTE_FFA] = "SVLTE_FFA",
	[HW_PLATFORM_SVLTE_SURF] = "SLVTE_SURF",
	[HW_PLATFORM_MTP_MDM] = "MDM_MTP_NO_DISPLAY",
	[HW_PLATFORM_MTP] = "MTP",
	[HW_PLATFORM_RCM] = "RCM",
	[HW_PLATFORM_LIQUID] = "Liquid",
	[HW_PLATFORM_DRAGON] = "Dragon",
	[HW_PLATFORM_QRD] = "QRD",
	[HW_PLATFORM_HRD] = "HRD",
	[HW_PLATFORM_DTV] = "DTV",
	[HW_PLATFORM_STP] = "STP",
	[HW_PLATFORM_SBC] = "SBC",
	[HW_PLATFORM_ADP] = "ADP",
	[HW_PLATFORM_HDK] = "HDK",
	[HW_PLATFORM_ATP] = "ATP",
	[HW_PLATFORM_IDP] = "IDP",
};

enum {
	PLATFORM_SUBTYPE_QRD = 0x0,
	PLATFORM_SUBTYPE_SKUAA = 0x1,
	PLATFORM_SUBTYPE_SKUF = 0x2,
	PLATFORM_SUBTYPE_SKUAB = 0x3,
	PLATFORM_SUBTYPE_SKUG = 0x5,
	PLATFORM_SUBTYPE_QRD_INVALID,
};

static const char * const qrd_hw_platform_subtype[] = {
	[PLATFORM_SUBTYPE_QRD] = "QRD",
	[PLATFORM_SUBTYPE_SKUAA] = "SKUAA",
	[PLATFORM_SUBTYPE_SKUF] = "SKUF",
	[PLATFORM_SUBTYPE_SKUAB] = "SKUAB",
	[PLATFORM_SUBTYPE_SKUG] = "SKUG",
	[PLATFORM_SUBTYPE_QRD_INVALID] = "INVALID",
};

enum {
	PLATFORM_SUBTYPE_UNKNOWN = 0x0,
	PLATFORM_SUBTYPE_CHARM = 0x1,
	PLATFORM_SUBTYPE_STRANGE = 0x2,
	PLATFORM_SUBTYPE_STRANGE_2A = 0x3,
	PLATFORM_SUBTYPE_INVALID,
};

static const char * const hw_platform_subtype[] = {
	[PLATFORM_SUBTYPE_UNKNOWN] = "Unknown",
	[PLATFORM_SUBTYPE_CHARM] = "charm",
	[PLATFORM_SUBTYPE_STRANGE] = "strange",
	[PLATFORM_SUBTYPE_STRANGE_2A] = "strange_2a",
	[PLATFORM_SUBTYPE_INVALID] = "Invalid",
};

static const char * const hw_platform_feature_code[] = {
	[SOCINFO_FC_UNKNOWN] = "Unknown",
	[SOCINFO_FC_AA] = "AA",
	[SOCINFO_FC_AB] = "AB",
	[SOCINFO_FC_AC] = "AC",
	[SOCINFO_FC_AD] = "AD",
	[SOCINFO_FC_AE] = "AE",
	[SOCINFO_FC_AF] = "AF",
	[SOCINFO_FC_AG] = "AG",
	[SOCINFO_FC_AH] = "AH",
};

static const char * const hw_platform_ifeature_code[] = {
	[SOCINFO_FC_Y0 - SOCINFO_FC_Y0] = "Y0",
	[SOCINFO_FC_Y1 - SOCINFO_FC_Y0] = "Y1",
	[SOCINFO_FC_Y2 - SOCINFO_FC_Y0] = "Y2",
	[SOCINFO_FC_Y3 - SOCINFO_FC_Y0] = "Y3",
	[SOCINFO_FC_Y4 - SOCINFO_FC_Y0] = "Y4",
	[SOCINFO_FC_Y5 - SOCINFO_FC_Y0] = "Y5",
	[SOCINFO_FC_Y6 - SOCINFO_FC_Y0] = "Y6",
	[SOCINFO_FC_Y7 - SOCINFO_FC_Y0] = "Y7",
	[SOCINFO_FC_Y8 - SOCINFO_FC_Y0] = "Y8",
	[SOCINFO_FC_Y9 - SOCINFO_FC_Y0] = "Y9",
	[SOCINFO_FC_YA - SOCINFO_FC_Y0] = "YA",
	[SOCINFO_FC_YB - SOCINFO_FC_Y0] = "YB",
	[SOCINFO_FC_YC - SOCINFO_FC_Y0] = "YC",
	[SOCINFO_FC_YD - SOCINFO_FC_Y0] = "YD",
	[SOCINFO_FC_YE - SOCINFO_FC_Y0] = "YE",
	[SOCINFO_FC_YF - SOCINFO_FC_Y0] = "YF",
};

/*
 * SMEM item id, used to acquire handles to respective
 * SMEM region.
 */
#define SMEM_HW_SW_BUILD_ID            137

#ifdef CONFIG_DEBUG_FS
#define SMEM_IMAGE_VERSION_BLOCKS_COUNT        32
#define SMEM_IMAGE_VERSION_SIZE                4096
#define SMEM_IMAGE_VERSION_NAME_SIZE           75
#define SMEM_IMAGE_VERSION_VARIANT_SIZE        20
#define SMEM_IMAGE_VERSION_OEM_SIZE            32

/*
 * SMEM Image table indices
 */
#define SMEM_IMAGE_TABLE_BOOT_INDEX     0
#define SMEM_IMAGE_TABLE_TZ_INDEX       1
#define SMEM_IMAGE_TABLE_RPM_INDEX      3
#define SMEM_IMAGE_TABLE_APPS_INDEX     10
#define SMEM_IMAGE_TABLE_MPSS_INDEX     11
#define SMEM_IMAGE_TABLE_ADSP_INDEX     12
#define SMEM_IMAGE_TABLE_CNSS_INDEX     13
#define SMEM_IMAGE_TABLE_VIDEO_INDEX    14
#define SMEM_IMAGE_VERSION_TABLE       469

/*
 * SMEM Image table names
 */
static const char *const socinfo_image_names[] = {
	[SMEM_IMAGE_TABLE_ADSP_INDEX] = "adsp",
	[SMEM_IMAGE_TABLE_APPS_INDEX] = "apps",
	[SMEM_IMAGE_TABLE_BOOT_INDEX] = "boot",
	[SMEM_IMAGE_TABLE_CNSS_INDEX] = "cnss",
	[SMEM_IMAGE_TABLE_MPSS_INDEX] = "mpss",
	[SMEM_IMAGE_TABLE_RPM_INDEX] = "rpm",
	[SMEM_IMAGE_TABLE_TZ_INDEX] = "tz",
	[SMEM_IMAGE_TABLE_VIDEO_INDEX] = "video",
};

static const char *const pmic_models[] = {
	[0]  = "Unknown PMIC model",
	[1]  = "PM8941",
	[2]  = "PM8841",
	[3]  = "PM8019",
	[4]  = "PM8226",
	[5]  = "PM8110",
	[6]  = "PMA8084",
	[7]  = "PMI8962",
	[8]  = "PMD9635",
	[9]  = "PM8994",
	[10] = "PMI8994",
	[11] = "PM8916",
	[12] = "PM8004",
	[13] = "PM8909/PM8058",
	[14] = "PM8028",
	[15] = "PM8901",
	[16] = "PM8950/PM8027",
	[17] = "PMI8950/ISL9519",
	[18] = "PMK8001/PM8921",
	[19] = "PMI8996/PM8018",
	[20] = "PM8998/PM8015",
	[21] = "PMI8998/PM8014",
	[22] = "PM8821",
	[23] = "PM8038",
	[24] = "PM8005/PM8922",
	[25] = "PM8917",
	[26] = "PM660L",
	[27] = "PM660",
	[30] = "PM8150",
	[31] = "PM8150L",
	[32] = "PM8150B",
	[33] = "PMK8002",
	[36] = "PM8009",
	[38] = "PM8150C",
	[41] = "SMB2351",
	[45] = "PM6125",
	[47] = "PMK8350",
	[48] = "PM8350",
	[49] = "PM8350C",
	[50] = "PM8350B",
	[51] = "PMR735A",
	[52] = "PMR735B",
	[58] = "PM8450",
	[65] = "PM8010",
};
#endif /* CONFIG_DEBUG_FS */

/* Socinfo SMEM item structure */
struct socinfo {
	__le32 fmt;
	__le32 id;
	__le32 ver;
	char build_id[SMEM_SOCINFO_BUILD_ID_LENGTH];
	/* Version 2 */
	__le32 raw_id;
	__le32 raw_ver;
	/* Version 3 */
	__le32 hw_plat;
	/* Version 4 */
	__le32 plat_ver;
	/* Version 5 */
	__le32 accessory_chip;
	/* Version 6 */
	__le32 hw_plat_subtype;
	/* Version 7 */
	__le32 pmic_model;
	__le32 pmic_die_rev;
	/* Version 8 */
	__le32 pmic_model_1;
	__le32 pmic_die_rev_1;
	__le32 pmic_model_2;
	__le32 pmic_die_rev_2;
	/* Version 9 */
	__le32 foundry_id;
	/* Version 10 */
	__le32 serial_num;
	/* Version 11 */
	__le32 num_pmics;
	__le32 pmic_array_offset;
	/* Version 12 */
	__le32 chip_family;
	__le32 raw_device_family;
	__le32 raw_device_num;
	/* Version 13 */
	__le32 nproduct_id;
	char chip_id[SMEM_SOCINFO_CHIP_ID_LENGTH];
	/* Version 14 */
	__le32 num_clusters;
	__le32 ncluster_array_offset;
	__le32 num_subset_parts;
	__le32 nsubset_parts_array_offset;
	/* Version 15 */
	__le32 nmodem_supported;
	/* Version 16 */
	__le32  feature_code;
	__le32  pcode;
	__le32  npartnamemap_offset;
	__le32  nnum_partname_mapping;
	/* Version 17 */
	__le32 oem_variant;
	/* Version 18 */
	__le32 num_kvps;
	__le32 kvps_offset;
	/* Version 19 */
	__le32 num_func_clusters;
	__le32 boot_cluster;
	__le32 boot_core;
} *socinfo;

#ifdef CONFIG_DEBUG_FS
struct socinfo_params {
	u32 raw_device_family;
	u32 hw_plat_subtype;
	u32 accessory_chip;
	u32 raw_device_num;
	u32 chip_family;
	u32 foundry_id;
	u32 plat_ver;
	u32 raw_ver;
	u32 hw_plat;
	u32 fmt;
	u32 nproduct_id;
	u32 num_clusters;
	u32 ncluster_array_offset;
	u32 num_subset_parts;
	u32 nsubset_parts_array_offset;
	u32 nmodem_supported;
	u32 feature_code;
	u32 pcode;
	u32 oem_variant;
	u32 num_func_clusters;
	u32 boot_cluster;
	u32 boot_core;
};

struct smem_image_version {
	char name[SMEM_IMAGE_VERSION_NAME_SIZE];
	char variant[SMEM_IMAGE_VERSION_VARIANT_SIZE];
	char pad;
	char oem[SMEM_IMAGE_VERSION_OEM_SIZE];
};
#endif /* CONFIG_DEBUG_FS */

#define MAX_SOCINFO_ATTRS 50
/* sysfs attributes */
#define ATTR_DEFINE(param)      \
	static DEVICE_ATTR(param, 0644, msm_get_##param, NULL)

/* sysfs attributes for subpart information */
#define CREATE_PART_FUNCTION(part, part_enum)  \
	static ssize_t \
	msm_get_##part(struct device *dev, \
			struct device_attribute *attr, \
			char *buf) \
	{ \
		u32 *part_info; \
		int num_parts = 0; \
		int str_pos = 0, i = 0; \
		num_parts = socinfo_get_part_count(part_enum); \
		part_info = kmalloc_array(num_parts, sizeof(*part_info), GFP_KERNEL); \
		socinfo_get_subpart_info(part_enum, part_info, num_parts); \
		for (i = 0; i < num_parts; i++) { \
			str_pos += scnprintf(buf+str_pos, PAGE_SIZE-str_pos, "0x%x", \
					part_info[i]); \
			if (i != num_parts-1) \
				str_pos += scnprintf(buf+str_pos, PAGE_SIZE-str_pos, ","); \
		} \
		str_pos += scnprintf(buf+str_pos, PAGE_SIZE-str_pos, "\n"); \
		kfree(part_info); \
		return str_pos; \
	} \
	ATTR_DEFINE(part) \

struct qcom_socinfo {
	struct soc_device *soc_dev;
	struct soc_device_attribute attr;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_root;
	struct socinfo_params info;
#endif /* CONFIG_DEBUG_FS */
};

struct soc_id {
	unsigned int id;
	const char *name;
};

#define PART_NAME_MAX		32
struct gpu_info {
	__le32 gpu_chip_id;
	__le32 vulkan_id;
	char part_name[PART_NAME_MAX];
};

struct socinfo_partinfo {
	__le32 part_type;
	union {
		struct gpu_info gpu_info;
	};
};
struct socinfo_partinfo partinfo[SOCINFO_PART_MAX_PARTTYPE];

static const struct soc_id soc_id[] = {
	{ 87, "MSM8960" },
	{ 109, "APQ8064" },
	{ 122, "MSM8660A" },
	{ 123, "MSM8260A" },
	{ 124, "APQ8060A" },
	{ 126, "MSM8974" },
	{ 130, "MPQ8064" },
	{ 138, "MSM8960AB" },
	{ 139, "APQ8060AB" },
	{ 140, "MSM8260AB" },
	{ 141, "MSM8660AB" },
	{ 145, "MSM8626" },
	{ 147, "MSM8610" },
	{ 153, "APQ8064AB" },
	{ 158, "MSM8226" },
	{ 159, "MSM8526" },
	{ 161, "MSM8110" },
	{ 162, "MSM8210" },
	{ 163, "MSM8810" },
	{ 164, "MSM8212" },
	{ 165, "MSM8612" },
	{ 166, "MSM8112" },
	{ 168, "MSM8225Q" },
	{ 169, "MSM8625Q" },
	{ 170, "MSM8125Q" },
	{ 172, "APQ8064AA" },
	{ 178, "APQ8084" },
	{ 184, "APQ8074" },
	{ 185, "MSM8274" },
	{ 186, "MSM8674" },
	{ 194, "MSM8974PRO-AC" },
	{ 198, "MSM8126" },
	{ 199, "APQ8026" },
	{ 200, "MSM8926" },
	{ 205, "MSM8326" },
	{ 206, "MSM8916" },
	{ 207, "MSM8994" },
	{ 208, "APQ8074PRO-AA" },
	{ 209, "APQ8074PRO-AB" },
	{ 210, "APQ8074PRO-AC" },
	{ 211, "MSM8274PRO-AA" },
	{ 212, "MSM8274PRO-AB" },
	{ 213, "MSM8274PRO-AC" },
	{ 214, "MSM8674PRO-AA" },
	{ 215, "MSM8674PRO-AB" },
	{ 216, "MSM8674PRO-AC" },
	{ 217, "MSM8974PRO-AA" },
	{ 218, "MSM8974PRO-AB" },
	{ 219, "APQ8028" },
	{ 220, "MSM8128" },
	{ 221, "MSM8228" },
	{ 222, "MSM8528" },
	{ 223, "MSM8628" },
	{ 224, "MSM8928" },
	{ 225, "MSM8510" },
	{ 226, "MSM8512" },
	{ 233, "MSM8936" },
	{ 239, "MSM8939" },
	{ 240, "APQ8036" },
	{ 241, "APQ8039" },
	{ 246, "MSM8996" },
	{ 247, "APQ8016" },
	{ 248, "MSM8216" },
	{ 249, "MSM8116" },
	{ 250, "MSM8616" },
	{ 251, "MSM8992" },
	{ 253, "APQ8094" },
	{ 290, "MDM9607" },
	{ 291, "APQ8096" },
	{ 292, "MSM8998" },
	{ 293, "MSM8953" },
	{ 296, "MDM8207" },
	{ 297, "MDM9207" },
	{ 298, "MDM9307" },
	{ 299, "MDM9628" },
	{ 304, "APQ8053" },
	{ 305, "MSM8996SG" },
	{ 310, "MSM8996AU" },
	{ 311, "APQ8096AU" },
	{ 312, "APQ8096SG" },
	{ 317, "SDM660" },
	{ 318, "SDM630" },
	{ 319, "APQ8098" },
	{ 321, "SDM845" },
	{ 322, "MDM9206" },
	{ 323, "IPQ8074" },
	{ 324, "SDA660" },
	{ 325, "SDM658" },
	{ 326, "SDA658" },
	{ 327, "SDA630" },
	{ 338, "SDM450" },
	{ 341, "SDA845" },
	{ 342, "IPQ8072" },
	{ 343, "IPQ8076" },
	{ 344, "IPQ8078" },
	{ 345, "SDM636" },
	{ 346, "SDA636" },
	{ 349, "SDM632" },
	{ 350, "SDA632" },
	{ 351, "SDA450" },
	{ 356, "SM8250" },
	{ 362, "SA8155" },
	{ 367, "SA8155P" },
	{ 375, "IPQ8070" },
	{ 376, "IPQ8071" },
	{ 377, "SA6155P" },
	{ 384, "SA6155" },
	{ 389, "IPQ8072A" },
	{ 390, "IPQ8074A" },
	{ 391, "IPQ8076A" },
	{ 392, "IPQ8078A" },
	{ 394, "SM6125" },
	{ 395, "IPQ8070A" },
	{ 396, "IPQ8071A" },
	{ 402, "IPQ6018" },
	{ 403, "IPQ6028" },
	{ 405, "SA8195P" },
	{ 421, "IPQ6000" },
	{ 422, "IPQ6010" },
	{ 425, "SC7180" },
	{ 434, "SM6350" },
	{ 439, "SM8350" },
	{ 449, "SC8280XP" },
	{ 453, "IPQ6005" },
	{ 455, "QRB5165" },
	{ 457, "SM8450" },
	{ 459, "SM7225" },
	{ 460, "SA8295P" },
	{ 461, "SA8540P" },
	{ 480, "SM8450" },
	{ 482, "SM8450" },
	{ 487, "SC7280" },
	{ 495, "SC7180P" },
	{ 507, "BLAIR" },
	{ 518, "KHAJE" },
	{ 519, "KALAMA" },
	{ 536, "KALAMAP" },
	{ 539, "CINDERRU" },
	{ 545, "CINDERDU" },
	{ 557, "PINEAPPLE" },
	{ 577, "PINEAPPLEP" },
	{ 614, "CLIFFS" },
};

static struct attribute *msm_custom_socinfo_attrs[MAX_SOCINFO_ATTRS];

static const char *socinfo_machine(struct device *dev, unsigned int id)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(soc_id); idx++) {
		if (soc_id[idx].id == id)
			return soc_id[idx].name;
	}

	return NULL;
}

/* Version 3 */
static uint32_t socinfo_get_platform_type(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 3) ?
		 le32_to_cpu(socinfo->hw_plat) : 0)
		: 0;
}

/* Version 4 */
static uint32_t socinfo_get_platform_version(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 4) ?
		 le32_to_cpu(socinfo->plat_ver) : 0)
		: 0;
}

/* Version 6 */
static uint32_t socinfo_get_platform_subtype(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 6) ?
		 le32_to_cpu(socinfo->hw_plat_subtype) : 0)
		: 0;
}

/* Version 12 */
static uint32_t socinfo_get_chip_family(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 12) ?
		 le32_to_cpu(socinfo->chip_family) : 0)
		: 0;
}

/* Version 13 */
static char *socinfo_get_chip_name(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 13) ?
		 socinfo->chip_id : "N/A")
		: "N/A";
}

/* Version 14 */
static uint32_t socinfo_get_num_clusters(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
		 le32_to_cpu(socinfo->num_clusters) : 0)
		: 0;
}

static uint32_t socinfo_get_ncluster_array_offset(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
		 le32_to_cpu(socinfo->ncluster_array_offset) : 0)
		: 0;
}

static uint32_t socinfo_get_num_subset_parts(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
		 le32_to_cpu(socinfo->num_subset_parts) : 0)
		: 0;
}

static uint32_t socinfo_get_nsubset_parts_array_offset(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
		 le32_to_cpu(socinfo->nsubset_parts_array_offset) : 0)
		: 0;
}

static uint32_t
socinfo_get_subset_parts(void)
{
	uint32_t num_parts = socinfo_get_num_subset_parts();
	uint32_t offset = socinfo_get_nsubset_parts_array_offset();
	uint32_t sub_parts = 0;
	void *info = socinfo;
	uint32_t part_entry;
	int i;

	if (!num_parts || !offset)
		return -EINVAL;

	info += offset;
	for (i = 0; i < num_parts; i++) {
		part_entry = get_unaligned_le32(info);
		if (part_entry & 1)
			sub_parts |= BIT(i);
		info += sizeof(uint32_t);
	}
	return sub_parts;
}

/* Version 16 */
static uint32_t socinfo_get_feature_code_id(void)
{
	uint32_t fc_id;

	if (!socinfo || socinfo_format < SOCINFO_VERSION(0, 16))
		return SOCINFO_FC_UNKNOWN;

	fc_id = le32_to_cpu(socinfo->feature_code);
	if (fc_id <= SOCINFO_FC_UNKNOWN || fc_id >= SOCINFO_FC_INT_RESERVE)
		return SOCINFO_FC_UNKNOWN;

	return fc_id;
}

static const char *socinfo_get_feature_code_mapping(void)
{
	uint32_t id = socinfo_get_feature_code_id();

	if (id > SOCINFO_FC_UNKNOWN && id < SOCINFO_FC_EXT_RESERVE)
		return hw_platform_feature_code[id];
	else if (id >= SOCINFO_FC_Y0 && id < SOCINFO_FC_INT_RESERVE)
		return hw_platform_ifeature_code[id - SOCINFO_FC_Y0];

	return NULL;
}

static uint32_t socinfo_get_pcode_id(void)
{
	uint32_t pcode;

	if (!socinfo || socinfo_format < SOCINFO_VERSION(0, 16))
		return SOCINFO_PCODE_RESERVE;

	pcode = le32_to_cpu(socinfo->pcode);
	if (pcode <= SOCINFO_PCODE_UNKNOWN || pcode >= SOCINFO_PCODE_RESERVE)
		return SOCINFO_PCODE_UNKNOWN;

	return pcode;
}

/* Exported APIs */

uint32_t socinfo_get_id(void)
{
	return (socinfo) ? le32_to_cpu(socinfo->id) : 0;
}
EXPORT_SYMBOL(socinfo_get_id);

const char *socinfo_get_id_string(void)
{
	uint32_t id = socinfo_get_id();

	return socinfo_machine(NULL, id);
}
EXPORT_SYMBOL(socinfo_get_id_string);

uint32_t socinfo_get_serial_number(void)
{
	return (socinfo) ? le32_to_cpu(socinfo->serial_num) : 0;
}
EXPORT_SYMBOL(socinfo_get_serial_number);

int socinfo_get_feature_code(void)
{
	if (socinfo_format < SOCINFO_VERSION(0, 16)) {
		pr_warn("socinfo: Feature code is not supported by bootloaders\n");
		return -EINVAL;
	}

	return socinfo_get_feature_code_id();
}
EXPORT_SYMBOL(socinfo_get_feature_code);

int socinfo_get_pcode(void)
{
	if (socinfo_format < SOCINFO_VERSION(0, 16)) {
		pr_warn("socinfo: pcode is not supported by bootloaders\n");
		return -EINVAL;
	}

	return socinfo_get_pcode_id();
}
EXPORT_SYMBOL(socinfo_get_pcode);

char *socinfo_get_partinfo_part_name(unsigned int part_id)
{
	if (socinfo_format < SOCINFO_VERSION(0, 16) || part_id >= SOCINFO_PART_MAX_PARTTYPE)
		return NULL;

	switch (part_id) {
	case SOCINFO_PART_GPU:
		return partinfo[part_id].gpu_info.part_name;
	default:
		break;
	}

	return NULL;
}
EXPORT_SYMBOL(socinfo_get_partinfo_part_name);

uint32_t socinfo_get_partinfo_chip_id(unsigned int part_id)
{
	uint32_t chip_id;

	if (socinfo_format < SOCINFO_VERSION(0, 16) || part_id >= SOCINFO_PART_MAX_PARTTYPE)
		return 0;

	switch (part_id) {
	case SOCINFO_PART_GPU:
		chip_id = partinfo[part_id].gpu_info.gpu_chip_id;
		break;
	default:
		chip_id = 0;
		break;
	}

	return chip_id;
}
EXPORT_SYMBOL(socinfo_get_partinfo_chip_id);

uint32_t socinfo_get_partinfo_vulkan_id(unsigned int part_id)
{
	if (socinfo_format < SOCINFO_VERSION(0, 16) || part_id != SOCINFO_PART_GPU)
		return  0;

	return partinfo[part_id].gpu_info.vulkan_id;
}
EXPORT_SYMBOL(socinfo_get_partinfo_vulkan_id);

uint32_t
socinfo_get_cluster_info(enum subset_cluster_type cluster)
{
	uint32_t sub_cluster, num_cluster, offset;
	void *cluster_val;
	void *info = socinfo;

	if (cluster >= NUM_CLUSTERS_MAX) {
		pr_err("Bad cluster\n");
		return -EINVAL;
	}

	num_cluster = socinfo_get_num_clusters();
	offset = socinfo_get_ncluster_array_offset();

	if (!num_cluster || !offset)
		return -EINVAL;

	info += offset;
	cluster_val = info + (sizeof(uint32_t) * cluster);
	sub_cluster = get_unaligned_le32(cluster_val);

	return sub_cluster;
}
EXPORT_SYMBOL(socinfo_get_cluster_info);

bool
socinfo_get_part_info(enum subset_part_type part)
{
	uint32_t partinfo;

	if (part >= NUM_PARTS_MAX) {
		pr_err("Bad part number\n");
		return false;
	}

	partinfo = socinfo_get_subset_parts();
	if (partinfo < 0) {
		pr_err("Failed to get part information\n");
		return false;
	}

	return (partinfo & BIT(part));
}
EXPORT_SYMBOL(socinfo_get_part_info);

/**
 * socinfo_get_part_count - Get part count
 * @part: The subset_part_type to be checked
 *
 * Return the number of instances supported by the
 * firmware for the part on success and a negative
 * errno will be returned in error cases.
 */
int
socinfo_get_part_count(enum subset_part_type part)
{
	int part_count = 1;

	/* TODO: part_count to be read from SMEM after firmware adds support */

	if ((part <= PART_UNKNOWN) || (part >= NUM_PARTS_MAX)) {
		pr_err("Bad part number\n");
		return -EINVAL;
	}

	return part_count;
}
EXPORT_SYMBOL(socinfo_get_part_count);

/**
 * socinfo_get_subpart_info - Get subpart information
 * @part: The subset_part_type to be checked
 * @part_info: Pointer to the subpart information.
 *             Used to store the subpart information
 *             for num_parts instances of the part.
 * @num_parts: Number of instances of the part for
 *             which the subpart information is required.
 *
 * On success subpart information will be stored in the part_info
 * array for minimum of the number of instances requested and
 * the number of instances supported by the firmware.
 *
 * A value of zero will be returned on success and a negative
 * errno will be returned in error cases.
 */
int
socinfo_get_subpart_info(enum subset_part_type part,
		u32 *part_info,
		u32 num_parts)
{
	uint32_t num_subset_parts = 0, offset = 0;
	void *info = socinfo;
	u32 i = 0, count = 0;
	int part_count = 0;

	part_count = socinfo_get_part_count(part);
	if (part_count <= 0)
		return -EINVAL;

	num_subset_parts = socinfo_get_num_subset_parts();
	offset = socinfo_get_nsubset_parts_array_offset();
	if (!num_subset_parts || !offset)
		return -EINVAL;

	info += (offset + (part * sizeof(u32)));
	count = min_t(u32, num_parts, part_count);
	for (i = 0; i < count; i++) {
		part_info[i] = get_unaligned_le32(info);
		info += sizeof(u32);
	}

	return 0;
}
EXPORT_SYMBOL(socinfo_get_subpart_info);

/* End Exported APIs */

/* Sysfs interface */

/* Version 3 */
static ssize_t
msm_get_hw_platform(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	uint32_t hw_type;

	hw_type = socinfo_get_platform_type();

	return scnprintf(buf, PAGE_SIZE, "%-.32s\n",
			hw_platform[hw_type]);
}
ATTR_DEFINE(hw_platform);

/* Version 4 */
static ssize_t
msm_get_platform_version(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n",
			socinfo_get_platform_version());
}
ATTR_DEFINE(platform_version);

/* Version 6 */
static ssize_t
msm_get_platform_subtype_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	uint32_t hw_subtype;

	hw_subtype = socinfo_get_platform_subtype();
	return scnprintf(buf, PAGE_SIZE, "%u\n",
			hw_subtype);
}
ATTR_DEFINE(platform_subtype_id);

static ssize_t
msm_get_platform_subtype(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	uint32_t hw_subtype;

	hw_subtype = socinfo_get_platform_subtype();
	if (socinfo_get_platform_type() == HW_PLATFORM_QRD) {
		if (hw_subtype >= PLATFORM_SUBTYPE_QRD_INVALID) {
			pr_err("Invalid hardware platform sub type for qrd found\n");
			hw_subtype = PLATFORM_SUBTYPE_QRD_INVALID;
		}
		return scnprintf(buf, PAGE_SIZE, "%-.32s\n",
				qrd_hw_platform_subtype[hw_subtype]);
	} else {
		if (hw_subtype >= PLATFORM_SUBTYPE_INVALID) {
			pr_err("Invalid hardware platform subtype\n");
			hw_subtype = PLATFORM_SUBTYPE_INVALID;
		}
		return scnprintf(buf, PAGE_SIZE, "%-.32s\n",
				hw_platform_subtype[hw_subtype]);
	}
}
ATTR_DEFINE(platform_subtype);

/* Version 12 */
static ssize_t
msm_get_chip_family(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n",
			socinfo_get_chip_family());
}
ATTR_DEFINE(chip_family);

/* Version 13 */
static ssize_t
msm_get_chip_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%-.32s\n",
			socinfo_get_chip_name());
}
ATTR_DEFINE(chip_id);

/* Version 14 */
static ssize_t
msm_get_num_clusters(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n",
			socinfo_get_num_clusters());
}
ATTR_DEFINE(num_clusters);

static ssize_t
msm_get_ncluster_array_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n",
			socinfo_get_ncluster_array_offset());
}
ATTR_DEFINE(ncluster_array_offset);

static ssize_t
msm_get_subset_cores(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	uint32_t sub_cluster = socinfo_get_cluster_info(CLUSTER_CPUSS);

	return scnprintf(buf, PAGE_SIZE, "%x\n", sub_cluster);
}
ATTR_DEFINE(subset_cores);

static ssize_t
msm_get_num_subset_parts(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n",
			socinfo_get_num_subset_parts());
}
ATTR_DEFINE(num_subset_parts);

static ssize_t
msm_get_nsubset_parts_array_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n",
			socinfo_get_nsubset_parts_array_offset());
}
ATTR_DEFINE(nsubset_parts_array_offset);

static ssize_t
msm_get_subset_parts(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	uint32_t sub_parts = socinfo_get_subset_parts();

	return scnprintf(buf, PAGE_SIZE, "%x\n", sub_parts);
}
ATTR_DEFINE(subset_parts);

CREATE_PART_FUNCTION(gpu, PART_GPU);
CREATE_PART_FUNCTION(video, PART_VIDEO);
CREATE_PART_FUNCTION(camera, PART_CAMERA);
CREATE_PART_FUNCTION(display, PART_DISPLAY);
CREATE_PART_FUNCTION(audio, PART_AUDIO);
CREATE_PART_FUNCTION(modem, PART_MODEM);
CREATE_PART_FUNCTION(wlan, PART_WLAN);
CREATE_PART_FUNCTION(comp, PART_COMP);
CREATE_PART_FUNCTION(sensors, PART_SENSORS);
CREATE_PART_FUNCTION(npu, PART_NPU);
CREATE_PART_FUNCTION(spss, PART_SPSS);
CREATE_PART_FUNCTION(nav, PART_NAV);
CREATE_PART_FUNCTION(comp1, PART_COMP1);
CREATE_PART_FUNCTION(display1, PART_DISPLAY1);
CREATE_PART_FUNCTION(nsp, PART_NSP);
CREATE_PART_FUNCTION(eva, PART_EVA);

/* Version 16 */
static ssize_t
msm_get_sku(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return sysfs_emit(buf, "%s\n", sku ? sku : "Unknown");
}
ATTR_DEFINE(sku);

static ssize_t
msm_get_pcode(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n", socinfo_get_pcode_id());
}
ATTR_DEFINE(pcode);

static ssize_t
msm_get_feature_code(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	const char *feature_code = socinfo_get_feature_code_mapping();

	return sysfs_emit(buf, "%s\n", feature_code ? feature_code : "Unknown");
}
ATTR_DEFINE(feature_code);

/* End Sysfs Interfaces */

static umode_t soc_info_attribute(struct kobject *kobj,
		struct attribute *attr,
		int index)
{
	return attr->mode;
}

static const struct attribute_group custom_soc_attr_group = {
	.attrs = msm_custom_socinfo_attrs,
	.is_visible = soc_info_attribute,
};

static void socinfo_populate_sysfs(struct qcom_socinfo *qcom_socinfo)
{
	int i = 0;

	switch (socinfo_format) {
	case SOCINFO_VERSION(0, 19):
	case SOCINFO_VERSION(0, 18):
	case SOCINFO_VERSION(0, 17):
	case SOCINFO_VERSION(0, 16):
		msm_custom_socinfo_attrs[i++] = &dev_attr_sku.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_feature_code.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_pcode.attr;
		fallthrough;
	case SOCINFO_VERSION(0, 15):
	case SOCINFO_VERSION(0, 14):
		msm_custom_socinfo_attrs[i++] = &dev_attr_num_clusters.attr;
		msm_custom_socinfo_attrs[i++] =
			&dev_attr_ncluster_array_offset.attr;
		msm_custom_socinfo_attrs[i++] =
			&dev_attr_num_subset_parts.attr;
		msm_custom_socinfo_attrs[i++] =
			&dev_attr_nsubset_parts_array_offset.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_subset_cores.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_subset_parts.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_gpu.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_video.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_camera.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_display.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_audio.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_modem.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_wlan.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_comp.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_sensors.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_npu.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_spss.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_nav.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_comp1.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_display1.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_nsp.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_eva.attr;
		fallthrough;
	case SOCINFO_VERSION(0, 13):
		msm_custom_socinfo_attrs[i++] = &dev_attr_chip_id.attr;
		fallthrough;
	case SOCINFO_VERSION(0, 12):
		msm_custom_socinfo_attrs[i++] = &dev_attr_chip_family.attr;
		fallthrough;
	case SOCINFO_VERSION(0, 11):
	case SOCINFO_VERSION(0, 10):
	case SOCINFO_VERSION(0, 9):
	case SOCINFO_VERSION(0, 8):
	case SOCINFO_VERSION(0, 7):
	case SOCINFO_VERSION(0, 6):
		msm_custom_socinfo_attrs[i++] =
			&dev_attr_platform_subtype_id.attr;
		msm_custom_socinfo_attrs[i++] = &dev_attr_platform_subtype.attr;
		fallthrough;
	case SOCINFO_VERSION(0, 5):
	case SOCINFO_VERSION(0, 4):
		msm_custom_socinfo_attrs[i++] = &dev_attr_platform_version.attr;
		fallthrough;
	case SOCINFO_VERSION(0, 3):
		msm_custom_socinfo_attrs[i++] = &dev_attr_hw_platform.attr;
		fallthrough;
	case SOCINFO_VERSION(0, 2):
	case SOCINFO_VERSION(0, 1):
		break;
	default:
		pr_err("Unknown socinfo format: v%u.%u\n",
				SOCINFO_MAJOR(socinfo_format),
				SOCINFO_MINOR(socinfo_format));
		break;
	}

	msm_custom_socinfo_attrs[i++] = NULL;
	qcom_socinfo->attr.custom_attr_group = &custom_soc_attr_group;
}

void socinfo_enumerate_partinfo_details(void)
{
	unsigned int partinfo_array_offset;
	unsigned int nnum_partname_mapping;
	void *ptr = socinfo;
	int i, part_type;

	if (socinfo_format < SOCINFO_VERSION(0, 16))
		return;

	partinfo_array_offset = le32_to_cpu(socinfo->npartnamemap_offset);
	nnum_partname_mapping = le32_to_cpu(socinfo->nnum_partname_mapping);

	if (nnum_partname_mapping >  SOCINFO_PART_MAX_PARTTYPE) {
		pr_warn("socinfo: Mismatch between bootloaders and hlos\n");
		return;
	}

	ptr += partinfo_array_offset;
	for (i = 0; i < nnum_partname_mapping; i++) {
		part_type = get_unaligned_le32(ptr);
		if (part_type > SOCINFO_PART_MAX_PARTTYPE)
			pr_warn("socinfo: part type mismatch\n");

		partinfo[part_type].part_type = part_type;
		ptr += sizeof(u32);

		partinfo[part_type].gpu_info.gpu_chip_id = get_unaligned_le32(ptr);
		ptr += sizeof(u32);

		partinfo[part_type].gpu_info.vulkan_id = get_unaligned_le32(ptr);
		ptr += sizeof(u32);

		strscpy(partinfo[part_type].gpu_info.part_name, ptr, PART_NAME_MAX);
		ptr += PART_NAME_MAX;
	}
}

#ifdef CONFIG_DEBUG_FS

#define QCOM_OPEN(name, _func)						\
static int qcom_open_##name(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, _func, inode->i_private);		\
}									\
									\
static const struct file_operations qcom_ ##name## _ops = {		\
	.open = qcom_open_##name,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
}

#define DEBUGFS_ADD(info, name)						\
	debugfs_create_file(__stringify(name), 0444,			\
			    qcom_socinfo->dbg_root,			\
			    info, &qcom_ ##name## _ops)


static int qcom_show_build_id(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;

	seq_printf(seq, "%s\n", socinfo->build_id);

	return 0;
}

static int qcom_show_pmic_model(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;
	int model = SOCINFO_MINOR(le32_to_cpu(socinfo->pmic_model));

	if (model < 0)
		return -EINVAL;

	if (model < ARRAY_SIZE(pmic_models) && pmic_models[model])
		seq_printf(seq, "%s\n", pmic_models[model]);
	else
		seq_printf(seq, "unknown (%d)\n", model);

	return 0;
}

static int qcom_show_pmic_model_array(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;
	unsigned int num_pmics = le32_to_cpu(socinfo->num_pmics);
	unsigned int pmic_array_offset = le32_to_cpu(socinfo->pmic_array_offset);
	int i;
	void *ptr = socinfo;

	ptr += pmic_array_offset;

	/* No need for bounds checking, it happened at socinfo_debugfs_init */
	for (i = 0; i < num_pmics; i++) {
		unsigned int model = SOCINFO_MINOR(get_unaligned_le32(ptr + 2 * i * sizeof(u32)));
		unsigned int die_rev = get_unaligned_le32(ptr + (2 * i + 1) * sizeof(u32));

		if (model < ARRAY_SIZE(pmic_models) && pmic_models[model])
			seq_printf(seq, "%s %u.%u\n", pmic_models[model],
				   SOCINFO_MAJOR(die_rev),
				   SOCINFO_MINOR(die_rev));
		else
			seq_printf(seq, "unknown (%d)\n", model);
	}

	return 0;
}

static int qcom_show_pmic_die_revision(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;

	seq_printf(seq, "%u.%u\n",
		   SOCINFO_MAJOR(le32_to_cpu(socinfo->pmic_die_rev)),
		   SOCINFO_MINOR(le32_to_cpu(socinfo->pmic_die_rev)));

	return 0;
}

static int qcom_show_chip_id(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;

	seq_printf(seq, "%s\n", socinfo->chip_id);

	return 0;
}

QCOM_OPEN(build_id, qcom_show_build_id);
QCOM_OPEN(pmic_model, qcom_show_pmic_model);
QCOM_OPEN(pmic_model_array, qcom_show_pmic_model_array);
QCOM_OPEN(pmic_die_rev, qcom_show_pmic_die_revision);
QCOM_OPEN(chip_id, qcom_show_chip_id);

#define DEFINE_IMAGE_OPS(type)					\
static int show_image_##type(struct seq_file *seq, void *p)		  \
{								  \
	struct smem_image_version *image_version = seq->private;  \
	if (image_version->type[0] != '\0')			  \
		seq_printf(seq, "%s\n", image_version->type);	  \
	return 0;						  \
}								  \
static int open_image_##type(struct inode *inode, struct file *file)	  \
{									  \
	return single_open(file, show_image_##type, inode->i_private); \
}									  \
									  \
static const struct file_operations qcom_image_##type##_ops = {	  \
	.open = open_image_##type,					  \
	.read = seq_read,						  \
	.llseek = seq_lseek,						  \
	.release = single_release,					  \
}

DEFINE_IMAGE_OPS(name);
DEFINE_IMAGE_OPS(variant);
DEFINE_IMAGE_OPS(oem);

static void socinfo_debugfs_init(struct qcom_socinfo *qcom_socinfo,
				 struct socinfo *info, size_t info_size)
{
	struct smem_image_version *versions;
	struct dentry *dentry;
	size_t size;
	int i;
	unsigned int num_pmics;
	unsigned int pmic_array_offset;

	qcom_socinfo->dbg_root = debugfs_create_dir("qcom_socinfo", NULL);

	qcom_socinfo->info.fmt = __le32_to_cpu(info->fmt);

	debugfs_create_x32("info_fmt", 0444, qcom_socinfo->dbg_root,
			   &qcom_socinfo->info.fmt);

	switch (qcom_socinfo->info.fmt) {
	case SOCINFO_VERSION(0, 19):
		qcom_socinfo->info.num_func_clusters = __le32_to_cpu(info->num_func_clusters);
		qcom_socinfo->info.boot_cluster = __le32_to_cpu(info->boot_cluster);
		qcom_socinfo->info.boot_core = __le32_to_cpu(info->boot_core);

		debugfs_create_u32("num_func_clusters", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.num_func_clusters);
		debugfs_create_u32("boot_cluster", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.boot_cluster);
		debugfs_create_u32("boot_core", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.boot_core);
		fallthrough;
	case SOCINFO_VERSION(0, 18):
	case SOCINFO_VERSION(0, 17):
		qcom_socinfo->info.oem_variant = __le32_to_cpu(info->oem_variant);
		debugfs_create_u32("oem_variant", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.oem_variant);
		fallthrough;
	case SOCINFO_VERSION(0, 16):
		qcom_socinfo->info.feature_code = __le32_to_cpu(info->feature_code);
		qcom_socinfo->info.pcode = __le32_to_cpu(info->pcode);

		debugfs_create_u32("feature_code", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.feature_code);
		debugfs_create_u32("pcode", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.pcode);
		fallthrough;
	case SOCINFO_VERSION(0, 15):
		qcom_socinfo->info.nmodem_supported = __le32_to_cpu(info->nmodem_supported);

		debugfs_create_u32("nmodem_supported", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.nmodem_supported);
		fallthrough;
	case SOCINFO_VERSION(0, 14):
		qcom_socinfo->info.num_clusters = __le32_to_cpu(info->num_clusters);
		qcom_socinfo->info.ncluster_array_offset = __le32_to_cpu(info->ncluster_array_offset);
		qcom_socinfo->info.num_subset_parts = __le32_to_cpu(info->num_subset_parts);
		qcom_socinfo->info.nsubset_parts_array_offset =
					__le32_to_cpu(info->nsubset_parts_array_offset);

		debugfs_create_u32("num_clusters", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.num_clusters);
		debugfs_create_u32("ncluster_array_offset", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.ncluster_array_offset);
		debugfs_create_u32("num_subset_parts", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.num_subset_parts);
		debugfs_create_u32("nsubset_parts_array_offset", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.nsubset_parts_array_offset);
		fallthrough;
	case SOCINFO_VERSION(0, 13):
		qcom_socinfo->info.nproduct_id = __le32_to_cpu(info->nproduct_id);

		debugfs_create_u32("nproduct_id", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.nproduct_id);
		DEBUGFS_ADD(info, chip_id);
		fallthrough;
	case SOCINFO_VERSION(0, 12):
		qcom_socinfo->info.chip_family =
			__le32_to_cpu(info->chip_family);
		qcom_socinfo->info.raw_device_family =
			__le32_to_cpu(info->raw_device_family);
		qcom_socinfo->info.raw_device_num =
			__le32_to_cpu(info->raw_device_num);

		debugfs_create_x32("chip_family", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.chip_family);
		debugfs_create_x32("raw_device_family", 0444,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_device_family);
		debugfs_create_x32("raw_device_number", 0444,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_device_num);
		fallthrough;
	case SOCINFO_VERSION(0, 11):
		num_pmics = le32_to_cpu(info->num_pmics);
		pmic_array_offset = le32_to_cpu(info->pmic_array_offset);
		if (pmic_array_offset + 2 * num_pmics * sizeof(u32) <= info_size)
			DEBUGFS_ADD(info, pmic_model_array);
		fallthrough;
	case SOCINFO_VERSION(0, 10):
	case SOCINFO_VERSION(0, 9):
		qcom_socinfo->info.foundry_id = __le32_to_cpu(info->foundry_id);

		debugfs_create_u32("foundry_id", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.foundry_id);
		fallthrough;
	case SOCINFO_VERSION(0, 8):
	case SOCINFO_VERSION(0, 7):
		DEBUGFS_ADD(info, pmic_model);
		DEBUGFS_ADD(info, pmic_die_rev);
		fallthrough;
	case SOCINFO_VERSION(0, 6):
		qcom_socinfo->info.hw_plat_subtype =
			__le32_to_cpu(info->hw_plat_subtype);

		debugfs_create_u32("hardware_platform_subtype", 0444,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.hw_plat_subtype);
		fallthrough;
	case SOCINFO_VERSION(0, 5):
		qcom_socinfo->info.accessory_chip =
			__le32_to_cpu(info->accessory_chip);

		debugfs_create_u32("accessory_chip", 0444,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.accessory_chip);
		fallthrough;
	case SOCINFO_VERSION(0, 4):
		qcom_socinfo->info.plat_ver = __le32_to_cpu(info->plat_ver);

		debugfs_create_u32("platform_version", 0444,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.plat_ver);
		fallthrough;
	case SOCINFO_VERSION(0, 3):
		qcom_socinfo->info.hw_plat = __le32_to_cpu(info->hw_plat);

		debugfs_create_u32("hardware_platform", 0444,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.hw_plat);
		fallthrough;
	case SOCINFO_VERSION(0, 2):
		qcom_socinfo->info.raw_ver  = __le32_to_cpu(info->raw_ver);

		debugfs_create_u32("raw_version", 0444, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_ver);
		fallthrough;
	case SOCINFO_VERSION(0, 1):
		DEBUGFS_ADD(info, build_id);
		break;
	}

	versions = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_IMAGE_VERSION_TABLE,
				 &size);

	for (i = 0; i < ARRAY_SIZE(socinfo_image_names); i++) {
		if (!socinfo_image_names[i])
			continue;

		dentry = debugfs_create_dir(socinfo_image_names[i],
					    qcom_socinfo->dbg_root);
		debugfs_create_file("name", 0444, dentry, &versions[i],
				    &qcom_image_name_ops);
		debugfs_create_file("variant", 0444, dentry, &versions[i],
				    &qcom_image_variant_ops);
		debugfs_create_file("oem", 0444, dentry, &versions[i],
				    &qcom_image_oem_ops);
	}
}

static void socinfo_debugfs_exit(struct qcom_socinfo *qcom_socinfo)
{
	debugfs_remove_recursive(qcom_socinfo->dbg_root);
}
#else
static void socinfo_debugfs_init(struct qcom_socinfo *qcom_socinfo,
				 struct socinfo *info, size_t info_size)
{
}
static void socinfo_debugfs_exit(struct qcom_socinfo *qcom_socinfo) {  }
#endif /* CONFIG_DEBUG_FS */

static int qcom_socinfo_probe(struct platform_device *pdev)
{
	struct qcom_socinfo *qs;
	struct socinfo *info;
	size_t item_size;
	const char *machine, *fc;

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_HW_SW_BUILD_ID,
			      &item_size);
	if (IS_ERR(info)) {
		dev_err(&pdev->dev, "Couldn't find socinfo\n");
		return PTR_ERR(info);
	}

	socinfo_format = le32_to_cpu(info->fmt);
	socinfo = info;
	qs = devm_kzalloc(&pdev->dev, sizeof(*qs), GFP_KERNEL);
	if (!qs)
		return -ENOMEM;

	qs->attr.family = "Snapdragon";
	qs->attr.machine = socinfo_machine(&pdev->dev,
					   le32_to_cpu(info->id));
	qs->attr.soc_id = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u",
					 le32_to_cpu(info->id));
	qs->attr.revision = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u.%u",
					   SOCINFO_MAJOR(le32_to_cpu(info->ver)),
					   SOCINFO_MINOR(le32_to_cpu(info->ver)));
	if (offsetof(struct socinfo, serial_num) <= item_size)
		qs->attr.serial_number = devm_kasprintf(&pdev->dev, GFP_KERNEL,
							"%u",
							le32_to_cpu(info->serial_num));

	if (socinfo_format >= SOCINFO_VERSION(0, 16)) {
		socinfo_enumerate_partinfo_details();
		machine = socinfo_machine(&pdev->dev, le32_to_cpu(info->id));
		fc = socinfo_get_feature_code_mapping();
		sku = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s-%u-%s",
			machine, socinfo_get_pcode_id(), fc);
	}

	socinfo_populate_sysfs(qs);
	qs->soc_dev = soc_device_register(&qs->attr);
	if (IS_ERR(qs->soc_dev))
		return PTR_ERR(qs->soc_dev);

	socinfo_debugfs_init(qs, info, item_size);

	/* Feed the soc specific unique data into entropy pool */
	add_device_randomness(info, item_size);

	platform_set_drvdata(pdev, qs);

	return 0;
}

static int qcom_socinfo_remove(struct platform_device *pdev)
{
	struct qcom_socinfo *qs = platform_get_drvdata(pdev);

	soc_device_unregister(qs->soc_dev);

	socinfo_debugfs_exit(qs);

	return 0;
}

static struct platform_driver qcom_socinfo_driver = {
	.probe = qcom_socinfo_probe,
	.remove = qcom_socinfo_remove,
	.driver  = {
		.name = "qcom-socinfo",
	},
};

module_platform_driver(qcom_socinfo_driver);

MODULE_DESCRIPTION("Qualcomm SoCinfo driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-socinfo");
