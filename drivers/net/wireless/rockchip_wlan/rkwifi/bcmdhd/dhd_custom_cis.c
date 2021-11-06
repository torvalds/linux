/*
 * Process CIS information from OTP for customer platform
 * (Handle the MAC address and module information)
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <ethernet.h>
#include <dngl_stats.h>
#include <bcmutils.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_linux.h>
#include <bcmdevs.h>

#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <bcmiov.h>

#ifdef DHD_USE_CISINFO_FROM_OTP
#include <bcmdevs_legacy.h>    /* need to still support chips no longer in trunk firmware */
#include <siutils.h>
#include <pcie_core.h>
#include <dhd_pcie.h>
#endif /* DHD_USE_CISINFO_FROM_OTP */

#ifdef DHD_USE_CISINFO_FROM_OTP
#define CIS_TUPLE_HDR_LEN		2
#if defined(BCM4375_CHIP)
#define CIS_TUPLE_START_ADDRESS     0x18011120
#define CIS_TUPLE_END_ADDRESS       0x18011177
#elif defined(BCM4389_CHIP_DEF)
#define CIS_TUPLE_START_ADDRESS     0x18011058
#define CIS_TUPLE_END_ADDRESS       0x180110AF
#else
#define CIS_TUPLE_START_ADDRESS     0x18011110
#define CIS_TUPLE_END_ADDRESS       0x18011167
#endif /* defined(BCM4375_CHIP) */
#define CIS_TUPLE_MAX_COUNT            (uint32)((CIS_TUPLE_END_ADDRESS - CIS_TUPLE_START_ADDRESS\
						+ 1) / sizeof(uint32))
#define CIS_TUPLE_TAG_START			0x80
#define CIS_TUPLE_TAG_VENDOR		0x81
#define CIS_TUPLE_TAG_BOARDTYPE		0x1b
#define CIS_TUPLE_TAG_LENGTH		1

typedef struct cis_tuple_format {
	uint8	id;
	uint8	len;	/* total length of tag and data */
	uint8	tag;
	uint8	data[1];
} cis_tuple_format_t;

static int
read_otp_from_bp(dhd_bus_t *bus, uint32 *data_buf)
{
	int int_val = 0, i = 0, bp_idx = 0;
	int boardtype_backplane_addr[] = {
		0x18010324, /* OTP Control 1 */
		0x18012618, /* PMU min resource mask */
	};
	int boardtype_backplane_data[] = {
		0x00fa0000,
		0x0e4fffff /* Keep on ARMHTAVAIL */
	};

	uint32 cis_start_addr = CIS_TUPLE_START_ADDRESS;
	uint32 org_boardtype_backplane_data[] = {
		0,
		0
	};

	for (bp_idx = 0; bp_idx < ARRAYSIZE(boardtype_backplane_addr); bp_idx++) {
		/* Read OTP Control 1 and PMU min_rsrc_mask before writing */
		if (si_backplane_access(bus->sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&org_boardtype_backplane_data[bp_idx], TRUE) != BCME_OK) {
			DHD_ERROR(("invalid size/addr combination\n"));
			return BCME_ERROR;
		}

		/* Write new OTP and PMU configuration */
		if (si_backplane_access(bus->sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&boardtype_backplane_data[bp_idx], FALSE) != BCME_OK) {
			DHD_ERROR(("invalid size/addr combination\n"));
			return BCME_ERROR;
		}

		if (si_backplane_access(bus->sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&int_val, TRUE) != BCME_OK) {
			DHD_ERROR(("invalid size/addr combination\n"));
			return BCME_ERROR;
		}

		DHD_INFO(("%s: boardtype_backplane_addr 0x%08x rdata 0x%04x\n",
			__FUNCTION__, boardtype_backplane_addr[bp_idx], int_val));
	}

	/* read tuple raw data */
	for (i = 0; i < CIS_TUPLE_MAX_COUNT; i++) {
		if (si_backplane_access(bus->sih, cis_start_addr + i * sizeof(uint32),
				sizeof(uint32),	&data_buf[i], TRUE) != BCME_OK) {
			break;
		}
		DHD_INFO(("%s: tuple index %d, raw data 0x%08x\n", __FUNCTION__, i,  data_buf[i]));
	}

	for (bp_idx = 0; bp_idx < ARRAYSIZE(boardtype_backplane_addr); bp_idx++) {
		/* Write original OTP and PMU configuration */
		if (si_backplane_access(bus->sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&org_boardtype_backplane_data[bp_idx], FALSE) != BCME_OK) {
			DHD_ERROR(("invalid size/addr combination\n"));
			return BCME_ERROR;
		}

		if (si_backplane_access(bus->sih, boardtype_backplane_addr[bp_idx], sizeof(int),
				&int_val, TRUE) != BCME_OK) {
			DHD_ERROR(("invalid size/addr combination\n"));
			return BCME_ERROR;
		}

		DHD_INFO(("%s: boardtype_backplane_addr 0x%08x rdata 0x%04x\n",
			__FUNCTION__, boardtype_backplane_addr[bp_idx], int_val));
	}

	return i * sizeof(uint32);
}

static int
dhd_parse_board_information_bcm(dhd_bus_t *bus, int *boardtype,
		unsigned char *vid, int *vid_length)
{
	int totlen, len;
	uint32 raw_data[CIS_TUPLE_MAX_COUNT];
	cis_tuple_format_t *tuple;

	totlen = read_otp_from_bp(bus, raw_data);
	if (totlen == BCME_ERROR || totlen == 0) {
		DHD_ERROR(("%s : Can't read the OTP\n", __FUNCTION__));
		return BCME_ERROR;
	}

	tuple = (cis_tuple_format_t *)raw_data;

	/* check the first tuple has tag 'start' */
	if (tuple->id != CIS_TUPLE_TAG_START) {
		DHD_ERROR(("%s: Can not find the TAG\n", __FUNCTION__));
		return BCME_ERROR;
	}

	*vid_length = *boardtype = 0;

	/* find tagged parameter */
	while ((totlen >= (tuple->len + CIS_TUPLE_HDR_LEN)) &&
			(*vid_length == 0 || *boardtype == 0)) {
		len = tuple->len;

		if ((tuple->tag == CIS_TUPLE_TAG_VENDOR) &&
				(totlen >= (int)(len + CIS_TUPLE_HDR_LEN))) {
			/* found VID */
			memcpy(vid, tuple->data, tuple->len - CIS_TUPLE_TAG_LENGTH);
			*vid_length = tuple->len - CIS_TUPLE_TAG_LENGTH;
			prhex("OTP VID", tuple->data, tuple->len - CIS_TUPLE_TAG_LENGTH);
		}
		else if ((tuple->tag == CIS_TUPLE_TAG_BOARDTYPE) &&
				(totlen >= (int)(len + CIS_TUPLE_HDR_LEN))) {
			/* found boardtype */
			*boardtype = (int)tuple->data[0];
			prhex("OTP boardtype", tuple->data, tuple->len - CIS_TUPLE_TAG_LENGTH);
		}

		tuple = (cis_tuple_format_t*)((uint8*)tuple + (len + CIS_TUPLE_HDR_LEN));
		totlen -= (len + CIS_TUPLE_HDR_LEN);
	}

	if (*vid_length <= 0 || *boardtype <= 0) {
		DHD_ERROR(("failed to parse information (vid=%d, boardtype=%d)\n",
			*vid_length, *boardtype));
		return BCME_ERROR;
	}

	return BCME_OK;
}

#ifdef USE_CID_CHECK
#define CHIP_REV_A0	1
#define CHIP_REV_A1	2
#define CHIP_REV_B0	3
#define CHIP_REV_B1	4
#define CHIP_REV_B2	5
#define CHIP_REV_C0	6
#define BOARD_TYPE_EPA				0x080f
#define BOARD_TYPE_IPA				0x0827
#define BOARD_TYPE_IPA_OLD			0x081a
#define DEFAULT_CIDINFO_FOR_EPA		"r00a_e000_a0_ePA"
#define DEFAULT_CIDINFO_FOR_IPA		"r00a_e000_a0_iPA"
#define DEFAULT_CIDINFO_FOR_A1		"r01a_e30a_a1"
#define DEFAULT_CIDINFO_FOR_B0		"r01i_e32_b0"

naming_info_t bcm4361_naming_table[] = {
	{ {""}, {""}, {""} },
	{ {"r00a_e000_a0_ePA"}, {"_a0_ePA"}, {"_a0_ePA"} },
	{ {"r00a_e000_a0_iPA"}, {"_a0"}, {"_a1"} },
	{ {"r01a_e30a_a1"}, {"_r01a_a1"}, {"_a1"} },
	{ {"r02a_e30a_a1"}, {"_r02a_a1"}, {"_a1"} },
	{ {"r02c_e30a_a1"}, {"_r02c_a1"}, {"_a1"} },
	{ {"r01d_e31_b0"}, {"_r01d_b0"}, {"_b0"} },
	{ {"r01f_e31_b0"}, {"_r01f_b0"}, {"_b0"} },
	{ {"r02g_e31_b0"}, {"_r02g_b0"}, {"_b0"} },
	{ {"r01h_e32_b0"}, {"_r01h_b0"}, {"_b0"} },
	{ {"r01i_e32_b0"}, {"_r01i_b0"}, {"_b0"} },
	{ {"r02j_e32_b0"}, {"_r02j_b0"}, {"_b0"} },
	{ {"r012_1kl_a1"}, {"_r012_a1"}, {"_a1"} },
	{ {"r013_1kl_b0"}, {"_r013_b0"}, {"_b0"} },
	{ {"r013_1kl_b0"}, {"_r013_b0"}, {"_b0"} },
	{ {"r014_1kl_b0"}, {"_r014_b0"}, {"_b0"} },
	{ {"r015_1kl_b0"}, {"_r015_b0"}, {"_b0"} },
	{ {"r020_1kl_b0"}, {"_r020_b0"}, {"_b0"} },
	{ {"r021_1kl_b0"}, {"_r021_b0"}, {"_b0"} },
	{ {"r022_1kl_b0"}, {"_r022_b0"}, {"_b0"} },
	{ {"r023_1kl_b0"}, {"_r023_b0"}, {"_b0"} },
	{ {"r024_1kl_b0"}, {"_r024_b0"}, {"_b0"} },
	{ {"r030_1kl_b0"}, {"_r030_b0"}, {"_b0"} },
	{ {"r031_1kl_b0"}, {"_r030_b0"}, {"_b0"} },	/* exceptional case : r31 -> r30 */
	{ {"r032_1kl_b0"}, {"_r032_b0"}, {"_b0"} },
	{ {"r033_1kl_b0"}, {"_r033_b0"}, {"_b0"} },
	{ {"r034_1kl_b0"}, {"_r034_b0"}, {"_b0"} },
	{ {"r02a_e32a_b2"}, {"_r02a_b2"}, {"_b2"} },
	{ {"r02b_e32a_b2"}, {"_r02b_b2"}, {"_b2"} },
	{ {"r020_1qw_b2"}, {"_r020_b2"}, {"_b2"} },
	{ {"r021_1qw_b2"}, {"_r021_b2"}, {"_b2"} },
	{ {"r022_1qw_b2"}, {"_r022_b2"}, {"_b2"} },
	{ {"r031_1qw_b2"}, {"_r031_b2"}, {"_b2"} }
};

naming_info_t bcm4375_naming_table[] = {
	{ {""}, {""}, {""} },
	{ {"e41_es11"}, {"_ES00_semco_b0"}, {"_b0"} },
	{ {"e43_es33"}, {"_ES01_semco_b0"}, {"_b0"} },
	{ {"e43_es34"}, {"_ES02_semco_b0"}, {"_b0"} },
	{ {"e43_es35"}, {"_ES02_semco_b0"}, {"_b0"} },
	{ {"e43_es36"}, {"_ES03_semco_b0"}, {"_b0"} },
	{ {"e43_cs41"}, {"_CS00_semco_b1"}, {"_b1"} },
	{ {"e43_cs51"}, {"_CS01_semco_b1"}, {"_b1"} },
	{ {"e43_cs53"}, {"_CS01_semco_b1"}, {"_b1"} },
	{ {"e43_cs61"}, {"_CS00_skyworks_b1"}, {"_b1"} },
	{ {"1rh_es10"}, {"_1rh_es10_b0"}, {"_b0"} },
	{ {"1rh_es11"}, {"_1rh_es11_b0"}, {"_b0"} },
	{ {"1rh_es12"}, {"_1rh_es12_b0"}, {"_b0"} },
	{ {"1rh_es13"}, {"_1rh_es13_b0"}, {"_b0"} },
	{ {"1rh_es20"}, {"_1rh_es20_b0"}, {"_b0"} },
	{ {"1rh_es32"}, {"_1rh_es32_b0"}, {"_b0"} },
	{ {"1rh_es41"}, {"_1rh_es41_b1"}, {"_b1"} },
	{ {"1rh_es42"}, {"_1rh_es42_b1"}, {"_b1"} },
	{ {"1rh_es43"}, {"_1rh_es43_b1"}, {"_b1"} },
	{ {"1rh_es44"}, {"_1rh_es44_b1"}, {"_b1"} }
};

naming_info_t bcm4389_naming_table[] = {
	{ {""}, {""}, {""} },
	{ {"e53_es23"}, {"_ES10_semco_b0"}, {"_b0"} },
	{ {"e53_es24"}, {"_ES20_semco_b0"}, {"_b0"} },
	{ {"e53_es25"}, {"_ES21_semco_b0"}, {"_b0"} },
	{ {"e53_es31"}, {"_ES30_semco_c0"}, {"_c0"} },
	{ {"e53_es32"}, {"_ES32_semco_c0"}, {"_c0"} },
	{ {"e53_es40"}, {"_ES40_semco_c1"}, {"_c1"} },
	{ {"1wk_es21"}, {"_1wk_es21_b0"}, {"_b0"} },
	{ {"1wk_es30"}, {"_1wk_es30_b0"}, {"_b0"} },
	{ {"1wk_es31"}, {"_1wk_es31_b0"}, {"_b0"} },
	{ {"1wk_es32"}, {"_1wk_es32_b0"}, {"_b0"} },
	{ {"1wk_es40"}, {"_1wk_es40_c0"}, {"_c0"} },
	{ {"1wk_es41"}, {"_1wk_es41_c0"}, {"_c0"} },
	{ {"1wk_es42"}, {"_1wk_es42_c0"}, {"_c0"} },
	{ {"1wk_es43"}, {"_1wk_es43_c0"}, {"_c0"} },
	{ {"1wk_es50"}, {"_1wk_es50_c1"}, {"_c1"} }
};

/* select the NVRAM/FW tag naming table */
naming_info_t *
select_naming_table(dhd_pub_t *dhdp, int *table_size)
{
	naming_info_t * info = NULL;

	if (!dhdp || !dhdp->bus || !dhdp->bus->sih)
	{
		DHD_ERROR(("%s : Invalid pointer \n", __FUNCTION__));
		return info;
	}

	switch (si_chipid(dhdp->bus->sih)) {
		case BCM4361_CHIP_ID:
		case BCM4347_CHIP_ID:
			info = &bcm4361_naming_table[0];
			*table_size = ARRAYSIZE(bcm4361_naming_table);
			DHD_INFO(("%s: info %p, ret %d\n", __FUNCTION__, info, *table_size));
			break;
		case BCM4375_CHIP_ID:
			info = &bcm4375_naming_table[0];
			*table_size = ARRAYSIZE(bcm4375_naming_table);
			DHD_INFO(("%s: info %p, ret %d\n", __FUNCTION__, info, *table_size));
			break;
		case BCM4389_CHIP_ID:
			info = &bcm4389_naming_table[0];
			*table_size = ARRAYSIZE(bcm4389_naming_table);
			DHD_INFO(("%s: info %p, ret %d\n", __FUNCTION__, info, *table_size));
			break;
		default:
			DHD_ERROR(("%s: No MODULE NAMING TABLE found\n", __FUNCTION__));
			break;
	}

	return info;
}

#define CID_FEM_MURATA				"_mur_"
naming_info_t *
dhd_find_naming_info(dhd_pub_t *dhdp, char *module_type)
{
	int i = 0;
	naming_info_t *info = NULL;
	int table_size = 0;

	info = select_naming_table(dhdp, &table_size);
	if (!info || !table_size) {
		DHD_ERROR(("%s : Can't select the naming table\n", __FUNCTION__));
		return NULL;
	}

	if (module_type && strlen(module_type) > 0) {
		for (i = 1, info++; i < table_size; info++, i++) {
			DHD_INFO(("%s : info %p, %d, info->cid_ext : %s\n",
				__FUNCTION__, info, i, info->cid_ext));
			if (!strncmp(info->cid_ext, module_type, strlen(info->cid_ext))) {
				break;
			}
		}
	}

	return info;
}

static naming_info_t *
dhd_find_naming_info_by_cid(dhd_pub_t *dhdp, char *cid_info)
{
	int i = 0;
	char *ptr;
	naming_info_t *info = NULL;
	int table_size = 0;

	info = select_naming_table(dhdp, &table_size);
	if (!info || !table_size) {
		DHD_ERROR(("%s : Can't select the naming table\n", __FUNCTION__));
		return NULL;
	}

	/* truncate extension */
	for (i = 1, ptr = cid_info; i < MODULE_NAME_INDEX_MAX && ptr; i++) {
		ptr = bcmstrstr(ptr, "_");
		if (ptr) {
			ptr++;
		}
	}

	for (i = 1, info++; i < table_size && ptr; info++, i++) {
		DHD_INFO(("%s : info %p, %d, info->cid_ext : %s\n",
				__FUNCTION__, info, i, info->cid_ext));
		if (!strncmp(info->cid_ext, ptr, strlen(info->cid_ext))) {
			break;
		}
	}

	return info;
}

naming_info_t *
dhd_find_naming_info_by_chip_rev(dhd_pub_t *dhdp, bool *is_murata_fem)
{
	int board_type = 0, chip_rev = 0, vid_length = 0;
	unsigned char vid[MAX_VID_LEN];
	naming_info_t *info = NULL;
	char *cid_info = NULL;
	dhd_bus_t *bus = NULL;

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL \n", __FUNCTION__));
		return NULL;
	}

	bus = dhdp->bus;

	if (!bus || !bus->sih) {
		DHD_ERROR(("%s:bus(%p) or bus->sih is NULL\n", __FUNCTION__, bus));
		return NULL;
	}

	chip_rev = bus->sih->chiprev;

	if (dhd_parse_board_information_bcm(bus, &board_type, vid, &vid_length)
			!= BCME_OK) {
		DHD_ERROR(("%s:failed to parse board information\n", __FUNCTION__));
		return NULL;
	}

	DHD_INFO(("%s:chip version %d\n", __FUNCTION__, chip_rev));

#ifdef BCM4361_CHIP
	/* A0 chipset has exception only */
	if (chip_rev == CHIP_REV_A0) {
		if (board_type == BOARD_TYPE_EPA) {
			info = dhd_find_naming_info(dhdp, DEFAULT_CIDINFO_FOR_EPA);
		} else if ((board_type == BOARD_TYPE_IPA) ||
				(board_type == BOARD_TYPE_IPA_OLD)) {
			info = dhd_find_naming_info(dhdp, DEFAULT_CIDINFO_FOR_IPA);
		}
	} else
#endif /* BCM4361_CHIP */
	{
		cid_info = dhd_get_cid_info(vid, vid_length);
		if (cid_info) {
			info = dhd_find_naming_info_by_cid(dhdp, cid_info);
			if (strstr(cid_info, CID_FEM_MURATA)) {
				*is_murata_fem = TRUE;
			}
		}
	}

	return info;
}
#endif /* USE_CID_CHECK */
#ifdef USE_DIRECT_VID_TAG
static int
concate_nvram_by_vid(dhd_pub_t *dhdp, char *nv_path, char *chipstr)
{
	unsigned char vid[MAX_VID_LEN];
	unsigned char vid2str[MAX_VID_LEN];

	memset(vid, 0, sizeof(vid));
	memset(vid2str, 0, sizeof(vid2str));

	if (dhd_check_stored_module_info(vid) == BCME_OK) {
		/* concate chip string tag */
		strncat(nv_path, chipstr, strlen(nv_path));
		/* concate nvram tag */
		snprintf(vid2str, sizeof(vid2str), "_%x%x", vid[VENDOR_OFF], vid[MD_REV_OFF]);
		strncat(nv_path, vid2str, strlen(nv_path));
		DHD_ERROR(("%s: nvram_path : %s\n", __FUNCTION__, nv_path));
	} else {
		int board_type = 0, vid_length = 0;
		dhd_bus_t *bus = NULL;
		if (!dhdp) {

			DHD_ERROR(("%s : dhdp is NULL \n", __FUNCTION__));
			return BCME_ERROR;
		}
		bus = dhdp->bus;
		if (dhd_parse_board_information_bcm(bus, &board_type, vid, &vid_length)
				!= BCME_OK) {
			DHD_ERROR(("%s:failed to parse board information\n", __FUNCTION__));
			return BCME_ERROR;
		} else {
			/* concate chip string tag */
			strncat(nv_path, chipstr, strlen(nv_path));
			/* vid from CIS - vid[1] = vendor, vid[0] - module rev. */
			snprintf(vid2str, sizeof(vid2str), "_%x%x",
					vid[VENDOR_OFF], vid[MD_REV_OFF]);
			/* concate nvram tag */
			strncat(nv_path, vid2str, strlen(nv_path));
			DHD_ERROR(("%s: nvram_path : %s\n", __FUNCTION__, nv_path));
		}
	}
	return BCME_OK;
}
#endif /* USE_DIRECT_VID_TAG */
#endif /* DHD_USE_CISINFO_FROM_OTP */

#ifdef DHD_USE_CISINFO

/* File Location to keep each information */
#ifdef OEM_ANDROID
#define MACINFO PLATFORM_PATH".mac.info"
#define CIDINFO PLATFORM_PATH".cid.info"
#ifdef PLATFORM_SLP
#define MACINFO_EFS "/csa/.mac.info"
#else
#define MACINFO_EFS "/efs/wifi/.mac.info"
#define CIDINFO_DATA "/data/.cid.info"
#endif /* PLATFORM_SLP */
#else
#define MACINFO "/opt/.mac.info"
#define MACINFO_EFS "/opt/.efs.mac.info"
#define CIDINFO "/opt/.cid.info"
#endif /* OEM_ANDROID */

/* Definitions for MAC address */
#define MAC_BUF_SIZE 20
#define MAC_CUSTOM_FORMAT	"%02X:%02X:%02X:%02X:%02X:%02X"

/* Definitions for CIS information */
#if defined(BCM4359_CHIP) || defined(BCM4361_CHIP) || defined(BCM4375_CHIP) || \
	defined(BCM4389_CHIP_DEF)
#define CIS_BUF_SIZE            1280
#else
#define CIS_BUF_SIZE            512
#endif /* BCM4359_CHIP */

#define DUMP_CIS_SIZE	48

#define CIS_TUPLE_TAG_START		0x80
#define CIS_TUPLE_TAG_VENDOR		0x81
#define CIS_TUPLE_TAG_MACADDR		0x19
#define CIS_TUPLE_TAG_BOARDTYPE		0x1b
#define CIS_TUPLE_LEN_MACADDR		7
#define CIS_DUMP_END                    0xff
#define CIS_TUPLE_NULL                  0X00

#ifdef CONFIG_BCMDHD_PCIE
#if defined(BCM4361_CHIP) || defined(BCM4375_CHIP)
#define OTP_OFFSET 208
#elif defined(BCM4389_CHIP_DEF)
#define OTP_OFFSET 0
#else
#define OTP_OFFSET 128
#endif /* BCM4361 | BCM4375 = 208, BCM4389 = 0, Others = 128 */
#else /* CONFIG_BCMDHD_PCIE */
#define OTP_OFFSET 12 /* SDIO */
#endif /* CONFIG_BCMDHD_PCIE */

unsigned char *g_cis_buf = NULL;

/* Definitions for common interface */
typedef struct tuple_entry {
	struct list_head list;	/* head of the list */
	uint32 cis_idx;		/* index of each tuples */
} tuple_entry_t;

extern int _dhd_set_mac_address(struct dhd_info *dhd, int ifidx, struct ether_addr *addr);
#if defined(GET_MAC_FROM_OTP) || defined(USE_CID_CHECK)
static tuple_entry_t *dhd_alloc_tuple_entry(dhd_pub_t *dhdp, const int idx);
static void dhd_free_tuple_entry(dhd_pub_t *dhdp, struct list_head *head);
static int dhd_find_tuple_list_from_otp(dhd_pub_t *dhdp, int req_tup,
	unsigned char* req_tup_len, struct list_head *head);
#endif /* GET_MAC_FROM_OTP || USE_CID_CHECK */

/* otp region read/write information */
typedef struct otp_rgn_rw_info {
	uint8 rgnid;
	uint8 preview;
	uint8 integrity_chk;
	uint16 rgnsize;
	uint16 datasize;
	uint8 *data;
} otp_rgn_rw_info_t;

/* otp region status information */
typedef struct otp_rgn_stat_info {
	uint8 rgnid;
	uint16 rgnstart;
	uint16 rgnsize;
} otp_rgn_stat_info_t;

typedef int (pack_handler_t)(void *ctx, uint8 *buf, uint16 *buflen);

/* Common Interface Functions */
int
dhd_alloc_cis(dhd_pub_t *dhdp)
{
	if (g_cis_buf == NULL) {
		g_cis_buf = MALLOCZ(dhdp->osh, CIS_BUF_SIZE);
		if (g_cis_buf == NULL) {
			DHD_ERROR(("%s: Failed to alloc buffer for CIS\n", __FUNCTION__));
			return BCME_NOMEM;
		} else {
			DHD_ERROR(("%s: Local CIS buffer is alloced\n", __FUNCTION__));
			memset(g_cis_buf, 0, CIS_BUF_SIZE);
		}
	}
	return BCME_OK;
}

void
dhd_clear_cis(dhd_pub_t *dhdp)
{
	if (g_cis_buf) {
		MFREE(dhdp->osh, g_cis_buf, CIS_BUF_SIZE);
		g_cis_buf = NULL;
		DHD_ERROR(("%s: Local CIS buffer is freed\n", __FUNCTION__));
	}
}

#ifdef DHD_READ_CIS_FROM_BP
int
dhd_read_cis(dhd_pub_t *dhdp)
{
	int ret = 0, totlen = 0;
	uint32 raw_data[CIS_TUPLE_MAX_COUNT];

	int cis_offset = OTP_OFFSET + sizeof(cis_rw_t);
#if defined(BCM4389_CHIP_DEF)
	/* override OTP_OFFSET for 4389 */
	cis_offset = OTP_OFFSET;
#endif /* BCM4389_CHIP_DEF */

	totlen = read_otp_from_bp(dhdp->bus, raw_data);
	if (totlen == BCME_ERROR || totlen == 0) {
		DHD_ERROR(("%s : Can't read the OTP\n", __FUNCTION__));
		return BCME_ERROR;
	}

	(void)memcpy_s(g_cis_buf + cis_offset, CIS_BUF_SIZE, raw_data, totlen);
	return ret;
}
#else
int
dhd_read_cis(dhd_pub_t *dhdp)
{
	int ret = 0;
	cis_rw_t *cish;
	int buf_size = CIS_BUF_SIZE;
	int length = strlen("cisdump");

	if (length >= buf_size) {
		DHD_ERROR(("%s: check CIS_BUF_SIZE\n", __FUNCTION__));
		return BCME_BADLEN;
	}

	/* Try reading out from CIS */
	cish = (cis_rw_t *)(g_cis_buf + 8);
	cish->source = 0;
	cish->byteoff = 0;
	cish->nbytes = buf_size;
	strlcpy(g_cis_buf, "cisdump", buf_size);

	ret = dhd_wl_ioctl_cmd(dhdp, WLC_GET_VAR, g_cis_buf, buf_size, 0, 0);
	if (ret < 0) {
		if (ret == BCME_UNSUPPORTED) {
			DHD_ERROR(("%s: get cisdump, UNSUPPORTED\n", __FUNCTION__));
		} else {
			DHD_ERROR(("%s : get cisdump err(%d)\n",
				__FUNCTION__, ret));
		}
		/* free local buf */
		dhd_clear_cis(dhdp);
	}

	return ret;
}
#endif /* DHD_READ_CIS_FROM_BP */

static int
dhd_otp_process_iov_resp_buf(void *ctx, void *iov_resp, uint16 cmd_id,
		bcm_xtlv_unpack_cbfn_t cbfn)
{
	bcm_iov_buf_t *p_resp = NULL;
	int ret = BCME_OK;
	uint16 version;

	/* check for version */
	version = dtoh16(*(uint16 *)iov_resp);
	if (version != WL_OTP_IOV_VERSION) {
		return BCME_VERSION;
	}

	p_resp = (bcm_iov_buf_t *)iov_resp;
	if ((p_resp->id == cmd_id) && (cbfn != NULL)) {
		ret = bcm_unpack_xtlv_buf(ctx, (uint8 *)p_resp->data, p_resp->len,
			BCM_XTLV_OPTION_ALIGN32, cbfn);
	}

	return ret;
}

static int
dhd_otp_get_iov_resp(dhd_pub_t *dhdp, const uint16 cmd_id, void *ctx,
	pack_handler_t packfn, bcm_xtlv_unpack_cbfn_t cbfn)
{
	bcm_iov_buf_t *iov_buf = NULL;
	uint8 *iov_resp = NULL;
	int ret = BCME_OK;
	int buf_size = CIS_BUF_SIZE;
	uint16 iovlen = 0, buflen = 0, buflen_start = 0;

	/* allocate input buffer */
	iov_buf = MALLOCZ(dhdp->osh, WLC_IOCTL_SMLEN);
	if (iov_buf == NULL) {
		DHD_ERROR(("%s: Failed to alloc buffer for iovar input\n", __FUNCTION__));
		ret = BCME_NOMEM;
		goto fail;
	}

	iov_resp = MALLOCZ(dhdp->osh, WLC_IOCTL_MAXLEN);
	if (iov_resp == NULL) {
		DHD_ERROR(("%s: Failed to alloc buffer for iovar response\n", __FUNCTION__));
		ret = BCME_NOMEM;
		goto fail;
	}

	/* parse and pack config parameters */
	buflen = buflen_start = (WLC_IOCTL_SMLEN - sizeof(*iov_buf));
	ret = packfn(ctx, (uint8 *)&iov_buf->data[0], &buflen);
	if (ret != BCME_OK) {
		goto fail;
	}

	/* fill header portion */
	iov_buf->version = WL_OTP_IOV_VERSION;
	iov_buf->len = (buflen_start - buflen);
	iov_buf->id = cmd_id;

	/* issue get iovar and process response */
	iovlen = sizeof(*iov_buf) + iov_buf->len;
	ret = dhd_iovar(dhdp, 0, "otp", (char *)iov_buf, iovlen,
			iov_resp, WLC_IOCTL_MAXLEN, FALSE);
	if (ret == BCME_OK) {
		ret = dhd_otp_process_iov_resp_buf(ctx, iov_resp, cmd_id, cbfn);
	} else {
		DHD_ERROR(("%s: Failed to get otp iovar\n", __FUNCTION__));
	}

fail:
	if (iov_buf) {
		MFREE(dhdp->osh, iov_buf, WLC_IOCTL_SMLEN);
	}
	if (iov_resp) {
		MFREE(dhdp->osh, iov_resp, buf_size);
	}
	if (ret < 0) {
		/* free local buf */
		dhd_clear_cis(dhdp);
	}
	return ret;
}

static int
dhd_otp_cbfn_rgnstatus(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	otp_rgn_stat_info_t *stat_info = (otp_rgn_stat_info_t *)ctx;

	BCM_REFERENCE(len);

	if (data == NULL) {
		DHD_ERROR(("%s: bad argument !!!\n", __FUNCTION__));
		return BCME_BADARG;
	}

	switch (type) {
		case WL_OTP_XTLV_RGN:
			stat_info->rgnid = *data;
			break;
		case WL_OTP_XTLV_ADDR:
			stat_info->rgnstart = dtoh16((uint16)*data);
			break;
		case WL_OTP_XTLV_SIZE:
			stat_info->rgnsize = dtoh16((uint16)*data);
			break;
		default:
			DHD_ERROR(("%s: unknown tlv %u\n", __FUNCTION__, type));
			break;
	}

	return BCME_OK;
}

static int
dhd_otp_packfn_rgnstatus(void *ctx, uint8 *buf, uint16 *buflen)
{
	uint8 *pxtlv = buf;
	int ret = BCME_OK;
	uint16 len = *buflen;
	uint8 rgnid = OTP_RGN_SW;

	BCM_REFERENCE(ctx);

	/* pack option <-r region> */
	ret = bcm_pack_xtlv_entry(&pxtlv, &len, WL_OTP_XTLV_RGN, sizeof(rgnid),
			&rgnid, BCM_XTLV_OPTION_ALIGN32);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: Failed pack xtlv entry of region: %d\n", __FUNCTION__, ret));
		return ret;
	}

	*buflen = len;
	return ret;
}

static int
dhd_otp_packfn_rgndump(void *ctx, uint8 *buf, uint16 *buflen)
{
	uint8 *pxtlv = buf;
	int ret = BCME_OK;
	uint16 len = *buflen, size = WLC_IOCTL_MAXLEN;
	uint8 rgnid = OTP_RGN_SW;

	/* pack option <-r region> */
	ret = bcm_pack_xtlv_entry(&pxtlv, &len, WL_OTP_XTLV_RGN,
			sizeof(rgnid), &rgnid, BCM_XTLV_OPTION_ALIGN32);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: Failed pack xtlv entry of region: %d\n", __FUNCTION__, ret));
		goto fail;
	}

	/* pack option [-s size] */
	ret = bcm_pack_xtlv_entry(&pxtlv, &len, WL_OTP_XTLV_SIZE,
			sizeof(size), (uint8 *)&size, BCM_XTLV_OPTION_ALIGN32);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: Failed pack xtlv entry of size: %d\n", __FUNCTION__, ret));
		goto fail;
	}
	*buflen = len;
fail:
	return ret;
}

static int
dhd_otp_cbfn_rgndump(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	otp_rgn_rw_info_t *rw_info = (otp_rgn_rw_info_t *)ctx;

	BCM_REFERENCE(len);

	if (data == NULL) {
		DHD_ERROR(("%s: bad argument !!!\n", __FUNCTION__));
		return BCME_BADARG;
	}

	switch (type) {
		case WL_OTP_XTLV_RGN:
			rw_info->rgnid = *data;
			break;
		case WL_OTP_XTLV_DATA:
			/*
			 * intentionally ignoring the return value of memcpy_s as it is just
			 * a variable copy and because of this size is within the bounds
			 */
			(void)memcpy_s(&rw_info->data, sizeof(rw_info->data),
					&data, sizeof(rw_info->data));
			rw_info->datasize = len;
			break;
		default:
			DHD_ERROR(("%s: unknown tlv %u\n", __FUNCTION__, type));
			break;
	}
	return BCME_OK;
}

int
dhd_read_otp_sw_rgn(dhd_pub_t *dhdp)
{
	int ret = BCME_OK;
	otp_rgn_rw_info_t rw_info;
	otp_rgn_stat_info_t stat_info;

	memset(&rw_info, 0, sizeof(rw_info));
	memset(&stat_info, 0, sizeof(stat_info));

	ret = dhd_otp_get_iov_resp(dhdp, WL_OTP_CMD_RGNSTATUS, &stat_info,
			dhd_otp_packfn_rgnstatus, dhd_otp_cbfn_rgnstatus);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: otp region status failed, ret=%d\n", __FUNCTION__, ret));
		goto fail;
	}

	rw_info.rgnsize = stat_info.rgnsize;
	ret = dhd_otp_get_iov_resp(dhdp, WL_OTP_CMD_RGNDUMP, &rw_info,
			dhd_otp_packfn_rgndump, dhd_otp_cbfn_rgndump);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: otp region dump failed, ret=%d\n", __FUNCTION__, ret));
		goto fail;
	}

	ret = memcpy_s(g_cis_buf, CIS_BUF_SIZE, rw_info.data, rw_info.datasize);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: Failed to copy otp dump, ret=%d\n", __FUNCTION__, ret));
	}
fail:
	return ret;

}

#if defined(GET_MAC_FROM_OTP) || defined(USE_CID_CHECK)
static tuple_entry_t*
dhd_alloc_tuple_entry(dhd_pub_t *dhdp, const int idx)
{
	tuple_entry_t *entry;

	entry = MALLOCZ(dhdp->osh, sizeof(tuple_entry_t));
	if (!entry) {
		DHD_ERROR(("%s: failed to alloc entry\n", __FUNCTION__));
		return NULL;
	}

	entry->cis_idx = idx;

	return entry;
}

static void
dhd_free_tuple_entry(dhd_pub_t *dhdp, struct list_head *head)
{
	tuple_entry_t *entry;

	while (!list_empty(head)) {
		entry = list_entry(head->next, tuple_entry_t, list);
		list_del(&entry->list);

		MFREE(dhdp->osh, entry, sizeof(tuple_entry_t));
	}
}

static int
dhd_find_tuple_list_from_otp(dhd_pub_t *dhdp, int req_tup,
	unsigned char* req_tup_len, struct list_head *head)
{
	int idx = OTP_OFFSET + sizeof(cis_rw_t);
	int tup, tup_len = 0;
	int buf_len = CIS_BUF_SIZE;
	int found = 0;

#if defined(BCM4389_CHIP_DEF)
	/* override OTP_OFFEST for 4389 */
	idx = OTP_OFFSET;
#endif /* BCM4389_CHIP_DEF */

	if (!g_cis_buf) {
		DHD_ERROR(("%s: Couldn't find cis info from"
			" local buffer\n", __FUNCTION__));
		return BCME_ERROR;
	}

	do {
		tup = g_cis_buf[idx++];
		if (tup == CIS_TUPLE_NULL || tup == CIS_DUMP_END) {
			tup_len = 0;
		} else {
			tup_len = g_cis_buf[idx++];
			if ((idx + tup_len) > buf_len) {
				return BCME_ERROR;
			}

			if (tup == CIS_TUPLE_TAG_START &&
				tup_len != CIS_TUPLE_NULL &&
				g_cis_buf[idx] == req_tup) {
				idx++;
				if (head) {
					tuple_entry_t *entry;
					entry = dhd_alloc_tuple_entry(dhdp, idx);
					if (entry) {
						list_add_tail(&entry->list, head);
						found++;
					}
				}
				if (found == 1 && req_tup_len) {
					*req_tup_len = tup_len;
				}
				tup_len--;
			}
		}
		idx += tup_len;
	} while (tup != CIS_DUMP_END && (idx < buf_len));

	return (found > 0) ? found : BCME_ERROR;
}
#endif /* GET_MAC_FROM_OTP || USE_CID_CHECK */

#ifdef DUMP_CIS
static void
dhd_dump_cis_buf(dhd_pub_t *dhdp, int size)
{
	int i;
	int cis_offset = 0;

	cis_offset =  OTP_OFFSET + sizeof(cis_rw_t);
#if defined(BCM4389_CHIP_DEF)
	/* override OTP_OFFEST for 4389 */
	cis_offset = OTP_OFFSET;
#endif /* BCM4389_CHIP_DEF */

	if (size <= 0) {
		return;
	}

	if (size > CIS_BUF_SIZE) {
		size = CIS_BUF_SIZE;
	}

	DHD_ERROR(("========== START CIS DUMP ==========\n"));
	for (i = 0; i < size; i++) {
		DHD_ERROR(("%02X ", g_cis_buf[i + cis_offset]));
		if ((i % 16) == 15) {
			DHD_ERROR(("\n"));
		}
	}
	if ((i % 16) != 15) {
		DHD_ERROR(("\n"));
	}
	DHD_ERROR(("========== END CIS DUMP ==========\n"));
}
#endif /* DUMP_CIS */

/* MAC address mangement functions */
#ifdef READ_MACADDR
static void
dhd_create_random_mac(char *buf, unsigned int buf_len)
{
	char random_mac[3];

	memset(random_mac, 0, sizeof(random_mac));
	get_random_bytes(random_mac, 3);

	snprintf(buf, buf_len, MAC_CUSTOM_FORMAT, 0x00, 0x12, 0x34,
		(uint32)random_mac[0], (uint32)random_mac[1], (uint32)random_mac[2]);

	DHD_ERROR(("%s: The Random Generated MAC ID: %s\n",
		__FUNCTION__, random_mac));
}

#ifndef DHD_MAC_ADDR_EXPORT
int
dhd_set_macaddr_from_file(dhd_pub_t *dhdp)
{
	char mac_buf[MAC_BUF_SIZE];
	char *filepath_efs = MACINFO_EFS;
#ifdef PLATFORM_SLP
	char *filepath_mac = MACINFO;
#endif /* PLATFORM_SLP */
	int ret;
	struct dhd_info *dhd;
	struct ether_addr *mac;
	char *invalid_mac = "00:00:00:00:00:00";

	if (dhdp) {
		dhd = dhdp->info;
		mac = &dhdp->mac;
	} else {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	memset(mac_buf, 0, sizeof(mac_buf));

	/* Read MAC address from the specified file */
	ret = dhd_read_file(filepath_efs, mac_buf, sizeof(mac_buf) - 1);

	/* Check if the file does not exist or contains invalid data */
	if (ret || (!ret && strstr(mac_buf, invalid_mac))) {
		/* Generate a new random MAC address */
		dhd_create_random_mac(mac_buf, sizeof(mac_buf));

		/* Write random MAC address to the file */
		if (dhd_write_file(filepath_efs, mac_buf, strlen(mac_buf)) < 0) {
			DHD_ERROR(("%s: MAC address [%s] Failed to write into File:"
				" %s\n", __FUNCTION__, mac_buf, filepath_efs));
			return BCME_ERROR;
		} else {
			DHD_ERROR(("%s: MAC address [%s] written into File: %s\n",
				__FUNCTION__, mac_buf, filepath_efs));
		}
	}
#ifdef PLATFORM_SLP
	/* Write random MAC address for framework */
	if (dhd_write_file(filepath_mac, mac_buf, strlen(mac_buf)) < 0) {
		DHD_ERROR(("%s: MAC address [%c%c:xx:xx:xx:x%c:%c%c] Failed to write into File:"
			" %s\n", __FUNCTION__, mac_buf[0], mac_buf[1],
			mac_buf[13], mac_buf[15], mac_buf[16], filepath_mac));
	} else {
		DHD_ERROR(("%s: MAC address [%c%c:xx:xx:xx:x%c:%c%c] written into File: %s\n",
			__FUNCTION__, mac_buf[0], mac_buf[1], mac_buf[13],
			mac_buf[15], mac_buf[16], filepath_mac));
	}
#endif /* PLATFORM_SLP */

	mac_buf[sizeof(mac_buf) - 1] = '\0';

	/* Write the MAC address to the Dongle */
	sscanf(mac_buf, MAC_CUSTOM_FORMAT,
		(uint32 *)&(mac->octet[0]), (uint32 *)&(mac->octet[1]),
		(uint32 *)&(mac->octet[2]), (uint32 *)&(mac->octet[3]),
		(uint32 *)&(mac->octet[4]), (uint32 *)&(mac->octet[5]));

	if (_dhd_set_mac_address(dhd, 0, mac) == 0) {
		DHD_INFO(("%s: MAC Address is overwritten\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: _dhd_set_mac_address() failed\n", __FUNCTION__));
	}

	return 0;
}
#else
int
dhd_set_macaddr_from_file(dhd_pub_t *dhdp)
{
	char mac_buf[MAC_BUF_SIZE];

	struct dhd_info *dhd;
	struct ether_addr *mac;

	if (dhdp) {
		dhd = dhdp->info;
		mac = &dhdp->mac;
	} else {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	memset(mac_buf, 0, sizeof(mac_buf));
	if (ETHER_ISNULLADDR(&sysfs_mac_addr)) {
		/* Generate a new random MAC address */
		dhd_create_random_mac(mac_buf, sizeof(mac_buf));
		if (!bcm_ether_atoe(mac_buf, &sysfs_mac_addr)) {
			DHD_ERROR(("%s : mac parsing err\n", __FUNCTION__));
			return BCME_ERROR;
		}
	}

	/* Write the MAC address to the Dongle */
	memcpy(mac, &sysfs_mac_addr, sizeof(sysfs_mac_addr));

	if (_dhd_set_mac_address(dhd, 0, mac) == 0) {
		DHD_INFO(("%s: MAC Address is overwritten\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: _dhd_set_mac_address() failed\n", __FUNCTION__));
	}

	return 0;
}
#endif /* !DHD_MAC_ADDR_EXPORT */
#endif /* READ_MACADDR */

#ifdef GET_MAC_FROM_OTP
static int
dhd_set_default_macaddr(dhd_pub_t *dhdp)
{
	char iovbuf[WLC_IOCTL_SMLEN];
	struct ether_addr *mac;
	int ret;

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return BCME_BADARG;
	}

	mac = &dhdp->mac;

	/* Read the default MAC address */
	ret = dhd_iovar(dhdp, 0, "cur_etheraddr", NULL, 0, iovbuf, sizeof(iovbuf),
			FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: Can't get the default MAC address\n", __FUNCTION__));
		return BCME_NOTUP;
	}

	/* Update the default MAC address */
	memcpy(mac, iovbuf, ETHER_ADDR_LEN);
#ifdef DHD_MAC_ADDR_EXPORT
	memcpy(&sysfs_mac_addr, mac, sizeof(sysfs_mac_addr));
#endif /* DHD_MAC_ADDR_EXPORT */

	return 0;
}

static int
dhd_verify_macaddr(dhd_pub_t *dhdp, struct list_head *head)
{
	tuple_entry_t *cur, *next;
	int idx = -1; /* Invalid index */

	list_for_each_entry(cur, head, list) {
		list_for_each_entry(next, &cur->list, list) {
			if ((unsigned long)next == (unsigned long)head) {
				DHD_INFO(("%s: next ptr %p is same as head ptr %p\n",
					__FUNCTION__, next, head));
				break;
			}
			if (!memcmp(&g_cis_buf[cur->cis_idx],
				&g_cis_buf[next->cis_idx], ETHER_ADDR_LEN)) {
				idx = cur->cis_idx;
				break;
			}
		}
	}

	return idx;
}

int
dhd_check_module_mac(dhd_pub_t *dhdp)
{
#ifndef DHD_MAC_ADDR_EXPORT
	char *filepath_efs = MACINFO_EFS;
#endif /* !DHD_MAC_ADDR_EXPORT */
	unsigned char otp_mac_buf[MAC_BUF_SIZE];
	struct ether_addr *mac;
	struct dhd_info *dhd;

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return BCME_BADARG;
	}

	dhd = dhdp->info;
	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return BCME_BADARG;
	}

#if defined(DHD_READ_CIS_FROM_BP) && defined(READ_MACADDR)
	/*
	 * For KOR Module, CID update is required only
	 * so, clearing and making g_cis_buf = NULL  before processing it when read_cis from STA FW
	 * It will get MAC from sysfs && won't update sysfs mac
	 */
	if (dhd_bus_get_fw_mode(dhdp) == DHD_FLAG_STA_MODE) {
		dhd_clear_cis(dhdp);
	}
#endif /* DHD_READ_CIS_FROM_BP && READ_MACADDR  */

	mac = &dhdp->mac;
	memset(otp_mac_buf, 0, sizeof(otp_mac_buf));

	if (!g_cis_buf) {
#ifndef DHD_MAC_ADDR_EXPORT
		char eabuf[ETHER_ADDR_STR_LEN];
		DHD_INFO(("%s: Couldn't read CIS information\n", __FUNCTION__));

		/* Read the MAC address from the specified file */
		if (dhd_read_file(filepath_efs, otp_mac_buf, sizeof(otp_mac_buf) - 1) < 0) {
			DHD_ERROR(("%s: Couldn't read the file, "
				"use the default MAC Address\n", __FUNCTION__));
			if (dhd_set_default_macaddr(dhdp) < 0) {
				return BCME_BADARG;
			}
		} else {
			bzero((char *)eabuf, sizeof(eabuf));
			strlcpy(eabuf, otp_mac_buf, sizeof(eabuf));
			if (!bcm_ether_atoe(eabuf, mac)) {
				DHD_ERROR(("%s : mac parsing err\n", __FUNCTION__));
				if (dhd_set_default_macaddr(dhdp) < 0) {
					return BCME_BADARG;
				}
			}
		}
#else
		DHD_INFO(("%s: Couldn't read CIS information\n", __FUNCTION__));

		/* Read the MAC address from the specified file */
		if (ETHER_ISNULLADDR(&sysfs_mac_addr)) {
			DHD_ERROR(("%s: Couldn't read the file, "
				"use the default MAC Address\n", __FUNCTION__));
			if (dhd_set_default_macaddr(dhdp) < 0) {
				return BCME_BADARG;
			}
		} else {
			/* sysfs mac addr is confirmed with valid format in set_mac_addr */
			memcpy(mac, &sysfs_mac_addr, sizeof(sysfs_mac_addr));
		}
#endif /* !DHD_MAC_ADDR_EXPORT */
	} else {
		struct list_head mac_list;
		unsigned char tuple_len = 0;
		int found = 0;
		int idx = -1; /* Invalid index */

#ifdef DUMP_CIS
		dhd_dump_cis_buf(dhdp, DUMP_CIS_SIZE);
#endif /* DUMP_CIS */

		/* Find a new tuple tag */
		INIT_LIST_HEAD(&mac_list);
		found = dhd_find_tuple_list_from_otp(dhdp, CIS_TUPLE_TAG_MACADDR,
			&tuple_len, &mac_list);
		if ((found > 0) && tuple_len == CIS_TUPLE_LEN_MACADDR) {
			if (found == 1) {
				tuple_entry_t *cur = list_entry((&mac_list)->next,
					tuple_entry_t, list);
				idx = cur->cis_idx;
			} else {
				/* Find the start index of MAC address */
				idx = dhd_verify_macaddr(dhdp, &mac_list);
			}
		}

		/* Find the MAC address */
		if (idx > 0) {
#ifdef DHD_EXPORT_CNTL_FILE
			/*
			 * WAR for incorrect otp mac address (including multicast bit)
			 * for SEMCo e53_es31 module
			 */
			if (strcmp(cidinfostr, "semco_sem_e53_es31") == 0) {
				g_cis_buf[idx] &= 0xFE;
			}
#endif /* DHD_EXPORT_CNTL_FILE */
			/* update MAC address */
			snprintf(otp_mac_buf, sizeof(otp_mac_buf), MAC_CUSTOM_FORMAT,
				(uint32)g_cis_buf[idx], (uint32)g_cis_buf[idx + 1],
				(uint32)g_cis_buf[idx + 2], (uint32)g_cis_buf[idx + 3],
				(uint32)g_cis_buf[idx + 4], (uint32)g_cis_buf[idx + 5]);
			DHD_ERROR(("%s: MAC address is taken from OTP: " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG(&g_cis_buf[idx])));
		} else {
			/* Not found MAC address info from the OTP, use the default value */
			if (dhd_set_default_macaddr(dhdp) < 0) {
				dhd_free_tuple_entry(dhdp, &mac_list);
				return BCME_BADARG;
			}
			snprintf(otp_mac_buf, sizeof(otp_mac_buf), MAC_CUSTOM_FORMAT,
				(uint32)mac->octet[0], (uint32)mac->octet[1],
				(uint32)mac->octet[2], (uint32)mac->octet[3],
				(uint32)mac->octet[4], (uint32)mac->octet[5]);
			DHD_ERROR(("%s: Cannot find MAC address info from OTP,"
				" Check module mac by initial value: " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG(mac->octet)));
		}

		dhd_free_tuple_entry(dhdp, &mac_list);
#ifndef DHD_MAC_ADDR_EXPORT
		dhd_write_file(filepath_efs, otp_mac_buf, strlen(otp_mac_buf));
#else
		/* Export otp_mac_buf to the sys/mac_addr */
		if (!bcm_ether_atoe(otp_mac_buf, &sysfs_mac_addr)) {
			DHD_ERROR(("%s : mac parsing err\n", __FUNCTION__));
			if (dhd_set_default_macaddr(dhdp) < 0) {
				return BCME_BADARG;
			}
		} else {
			DHD_INFO(("%s : set mac address properly\n", __FUNCTION__));
			/* set otp mac to sysfs */
			memcpy(mac, &sysfs_mac_addr, sizeof(sysfs_mac_addr));
		}
#endif /* !DHD_MAC_ADDR_EXPORT */
	}

	if (_dhd_set_mac_address(dhd, 0, mac) == 0) {
		DHD_INFO(("%s: MAC Address is set\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: Failed to set MAC address\n", __FUNCTION__));
	}

	return 0;
}
#endif /* GET_MAC_FROM_OTP */

/*
 * XXX:SWWLAN-210178 SysFS MAC ADDR export
 * framework controls mac addr with sysfs mac_addr kernel object without file system
 * For this reason, DHD doesn't need to write mac address to file system directly
 */
#ifndef DHD_MAC_ADDR_EXPORT
#ifdef WRITE_MACADDR
int
dhd_write_macaddr(struct ether_addr *mac)
{
	char *filepath_data = MACINFO;
	char *filepath_efs = MACINFO_EFS;
	char mac_buf[MAC_BUF_SIZE];
	int ret = 0;
	int retry_cnt = 0;

	memset(mac_buf, 0, sizeof(mac_buf));
	snprintf(mac_buf, sizeof(mac_buf), MAC_CUSTOM_FORMAT,
		(uint32)mac->octet[0], (uint32)mac->octet[1],
		(uint32)mac->octet[2], (uint32)mac->octet[3],
		(uint32)mac->octet[4], (uint32)mac->octet[5]);

	if (filepath_data) {
		for (retry_cnt = 0; retry_cnt < 3; retry_cnt++) {
			/* Write MAC information into /data/.mac.info */
			ret = dhd_write_file_and_check(filepath_data, mac_buf, strlen(mac_buf));
			if (!ret) {
				break;
			}
		}

		if (ret < 0) {
			DHD_ERROR(("%s: MAC address [%s] Failed to write into"
				" File: %s\n", __FUNCTION__, mac_buf, filepath_data));
			return BCME_ERROR;
		}
	} else {
		DHD_ERROR(("%s: filepath_data doesn't exist\n", __FUNCTION__));
	}

	if (filepath_efs) {
		for (retry_cnt = 0; retry_cnt < 3; retry_cnt++) {
			/* Write MAC information into /efs/wifi/.mac.info */
			ret = dhd_write_file_and_check(filepath_efs, mac_buf, strlen(mac_buf));
			if (!ret) {
				break;
			}
		}

		if (ret < 0) {
			DHD_ERROR(("%s: MAC address [%s] Failed to write into"
				" File: %s\n", __FUNCTION__, mac_buf, filepath_efs));
			return BCME_ERROR;
		}
	} else {
		DHD_ERROR(("%s: filepath_efs doesn't exist\n", __FUNCTION__));
	}

	return ret;
}
#endif /* WRITE_MACADDR */
#endif /* !DHD_MAC_ADDR_EXPORT */

#if defined(USE_CID_CHECK) || defined(USE_DIRECT_VID_TAG)
static int
dhd_find_tuple_idx_from_otp(dhd_pub_t *dhdp, int req_tup, unsigned char *req_tup_len)
{
	struct list_head head;
	int start_idx;
	int entry_num;

	if (!g_cis_buf) {
		DHD_ERROR(("%s: Couldn't find cis info from"
			" local buffer\n", __FUNCTION__));
		return BCME_ERROR;
	}

	INIT_LIST_HEAD(&head);
	entry_num = dhd_find_tuple_list_from_otp(dhdp, req_tup, req_tup_len, &head);
	/* find the first cis index from the tuple list */
	if (entry_num > 0) {
		tuple_entry_t *cur = list_entry((&head)->next, tuple_entry_t, list);
		start_idx = cur->cis_idx;
	} else {
		start_idx = -1; /* Invalid index */
	}

	dhd_free_tuple_entry(dhdp, &head);

	return start_idx;
}
#endif /* USE_CID_CHECK || USE_DIRECT_VID_TAG */

#ifdef USE_CID_CHECK
/* Definitions for module information */
#define MAX_VID_LEN		8

#ifdef	SUPPORT_MULTIPLE_BOARDTYPE
#define MAX_BNAME_LEN		6

typedef struct {
	uint8 b_len;
	unsigned char btype[MAX_VID_LEN];
	char bname[MAX_BNAME_LEN];
} board_info_t;

#if defined(BCM4361_CHIP)
board_info_t semco_PA_info[] = {
	{ 3, { 0x0f, 0x08, }, { "_ePA" } },     /* semco All ePA */
	{ 3, { 0x27, 0x08, }, { "_iPA" } },     /* semco 2g iPA, 5g ePA */
	{ 3, { 0x1a, 0x08, }, { "_iPA" } },		/* semco 2g iPA, 5g ePA old */
	{ 0, { 0x00, }, { "" } }   /* Default: Not specified yet */
};
#else
board_info_t semco_board_info[] = {
	{ 3, { 0x51, 0x07, }, { "_b90b" } },     /* semco three antenna */
	{ 3, { 0x61, 0x07, }, { "_b90b" } },     /* semco two antenna */
	{ 0, { 0x00, }, { "" } }   /* Default: Not specified yet */
};
board_info_t murata_board_info[] = {
	{ 3, { 0xa5, 0x07, }, { "_b90" } },      /* murata three antenna */
	{ 3, { 0xb0, 0x07, }, { "_b90b" } },     /* murata two antenna */
	{ 3, { 0xb1, 0x07, }, { "_es5" } },     /* murata two antenna */
	{ 0, { 0x00, }, { "" } }   /* Default: Not specified yet */
};
#endif /* BCM4361_CHIP */
#endif /* SUPPORT_MULTIPLE_BOARDTYPE */

typedef struct {
	uint8 vid_length;
	unsigned char vid[MAX_VID_LEN];
	char cid_info[MAX_VNAME_LEN];
} vid_info_t;

#if defined(BCM4335_CHIP)
vid_info_t vid_info[] = {
	{ 3, { 0x33, 0x66, }, { "semcosh" } },		/* B0 Sharp 5G-FEM */
	{ 3, { 0x33, 0x33, }, { "semco" } },		/* B0 Skyworks 5G-FEM and A0 chip */
	{ 3, { 0x33, 0x88, }, { "semco3rd" } },		/* B0 Syri 5G-FEM */
	{ 3, { 0x00, 0x11, }, { "muratafem1" } },	/* B0 ANADIGICS 5G-FEM */
	{ 3, { 0x00, 0x22, }, { "muratafem2" } },	/* B0 TriQuint 5G-FEM */
	{ 3, { 0x00, 0x33, }, { "muratafem3" } },	/* 3rd FEM: Reserved */
	{ 0, { 0x00, }, { "murata" } }	/* Default: for Murata A0 module */
};
#elif defined(BCM4339_CHIP) || defined(BCM4354_CHIP) || \
	defined(BCM4356_CHIP)
vid_info_t vid_info[] = {			  /* 4339:2G FEM+5G FEM ,4354: 2G FEM+5G FEM */
	{ 3, { 0x33, 0x33, }, { "semco" } },      /* 4339:Skyworks+sharp,4354:Panasonic+Panasonic */
	{ 3, { 0x33, 0x66, }, { "semco" } },      /* 4339:  , 4354:Panasonic+SEMCO */
	{ 3, { 0x33, 0x88, }, { "semco3rd" } },   /* 4339:  , 4354:SEMCO+SEMCO */
	{ 3, { 0x90, 0x01, }, { "wisol" } },      /* 4339:  , 4354:Microsemi+Panasonic */
	{ 3, { 0x90, 0x02, }, { "wisolfem1" } },  /* 4339:  , 4354:Panasonic+Panasonic */
	{ 3, { 0x90, 0x03, }, { "wisolfem2" } },  /* 4354:Murata+Panasonic */
	{ 3, { 0x00, 0x11, }, { "muratafem1" } }, /* 4339:  , 4354:Murata+Anadigics */
	{ 3, { 0x00, 0x22, }, { "muratafem2"} },  /* 4339:  , 4354:Murata+Triquint */
	{ 0, { 0x00, }, { "samsung" } }           /* Default: Not specified yet */
};
#elif defined(BCM4358_CHIP)
vid_info_t vid_info[] = {
	{ 3, { 0x33, 0x33, }, { "semco_b85" } },
	{ 3, { 0x33, 0x66, }, { "semco_b85" } },
	{ 3, { 0x33, 0x88, }, { "semco3rd_b85" } },
	{ 3, { 0x90, 0x01, }, { "wisol_b85" } },
	{ 3, { 0x90, 0x02, }, { "wisolfem1_b85" } },
	{ 3, { 0x90, 0x03, }, { "wisolfem2_b85" } },
	{ 3, { 0x31, 0x90, }, { "wisol_b85b" } },
	{ 3, { 0x00, 0x11, }, { "murata_b85" } },
	{ 3, { 0x00, 0x22, }, { "murata_b85"} },
	{ 6, { 0x00, 0xFF, 0xFF, 0x00, 0x00, }, { "murata_b85"} },
	{ 3, { 0x10, 0x33, }, { "semco_b85a" } },
	{ 3, { 0x30, 0x33, }, { "semco_b85b" } },
	{ 3, { 0x31, 0x33, }, { "semco_b85b" } },
	{ 3, { 0x10, 0x22, }, { "murata_b85a" } },
	{ 3, { 0x20, 0x22, }, { "murata_b85a" } },
	{ 3, { 0x21, 0x22, }, { "murata_b85a" } },
	{ 3, { 0x23, 0x22, }, { "murata_b85a" } },
	{ 3, { 0x31, 0x22, }, { "murata_b85b" } },
	{ 0, { 0x00, }, { "samsung" } }           /* Default: Not specified yet */
};
#elif defined(BCM4359_CHIP)
vid_info_t vid_info[] = {
#if defined(SUPPORT_BCM4359_MIXED_MODULES)
	{ 3, { 0x34, 0x33, }, { "semco_b90b" } },
	{ 3, { 0x40, 0x33, }, { "semco_b90b" } },
	{ 3, { 0x41, 0x33, }, { "semco_b90b" } },
	{ 3, { 0x11, 0x33, }, { "semco_b90b" } },
	{ 3, { 0x33, 0x66, }, { "semco_b90b" } },
	{ 3, { 0x23, 0x22, }, { "murata_b90b" } },
	{ 3, { 0x40, 0x22, }, { "murata_b90b" } },
	{ 3, { 0x10, 0x90, }, { "wisol_b90b" } },
	{ 3, { 0x33, 0x33, }, { "semco_b90s_b1" } },
	{ 3, { 0x66, 0x33, }, { "semco_b90s_c0" } },
	{ 3, { 0x60, 0x22, }, { "murata_b90s_b1" } },
	{ 3, { 0x61, 0x22, }, { "murata_b90s_b1" } },
	{ 3, { 0x62, 0x22, }, { "murata_b90s_b1" } },
	{ 3, { 0x63, 0x22, }, { "murata_b90s_b1" } },
	{ 3, { 0x70, 0x22, }, { "murata_b90s_c0" } },
	{ 3, { 0x71, 0x22, }, { "murata_b90s_c0" } },
	{ 3, { 0x72, 0x22, }, { "murata_b90s_c0" } },
	{ 3, { 0x73, 0x22, }, { "murata_b90s_c0" } },
	{ 0, { 0x00, }, { "samsung" } }           /* Default: Not specified yet */
#else /* SUPPORT_BCM4359_MIXED_MODULES */
	{ 3, { 0x34, 0x33, }, { "semco" } },
	{ 3, { 0x40, 0x33, }, { "semco" } },
	{ 3, { 0x41, 0x33, }, { "semco" } },
	{ 3, { 0x11, 0x33, }, { "semco" } },
	{ 3, { 0x33, 0x66, }, { "semco" } },
	{ 3, { 0x23, 0x22, }, { "murata" } },
	{ 3, { 0x40, 0x22, }, { "murata" } },
	{ 3, { 0x51, 0x22, }, { "murata" } },
	{ 3, { 0x52, 0x22, }, { "murata" } },
	{ 3, { 0x10, 0x90, }, { "wisol" } },
	{ 0, { 0x00, }, { "samsung" } }           /* Default: Not specified yet */
#endif /* SUPPORT_BCM4359_MIXED_MODULES */
};
#elif defined(BCM4361_CHIP)
vid_info_t vid_info[] = {
#if defined(SUPPORT_MIXED_MODULES)
	{ 3, { 0x66, 0x33, }, { "semco_sky_r00a_e000_a0" } },
	{ 3, { 0x30, 0x33, }, { "semco_sky_r01a_e30a_a1" } },
	{ 3, { 0x31, 0x33, }, { "semco_sky_r02a_e30a_a1" } },
	{ 3, { 0x32, 0x33, }, { "semco_sky_r02a_e30a_a1" } },
	{ 3, { 0x51, 0x33, }, { "semco_sky_r01d_e31_b0" } },
	{ 3, { 0x61, 0x33, }, { "semco_sem_r01f_e31_b0" } },
	{ 3, { 0x62, 0x33, }, { "semco_sem_r02g_e31_b0" } },
	{ 3, { 0x71, 0x33, }, { "semco_sky_r01h_e32_b0" } },
	{ 3, { 0x81, 0x33, }, { "semco_sem_r01i_e32_b0" } },
	{ 3, { 0x82, 0x33, }, { "semco_sem_r02j_e32_b0" } },
	{ 3, { 0x91, 0x33, }, { "semco_sem_r02a_e32a_b2" } },
	{ 3, { 0xa1, 0x33, }, { "semco_sem_r02b_e32a_b2" } },
	{ 3, { 0x12, 0x22, }, { "murata_nxp_r012_1kl_a1" } },
	{ 3, { 0x13, 0x22, }, { "murata_mur_r013_1kl_b0" } },
	{ 3, { 0x14, 0x22, }, { "murata_mur_r014_1kl_b0" } },
	{ 3, { 0x15, 0x22, }, { "murata_mur_r015_1kl_b0" } },
	{ 3, { 0x20, 0x22, }, { "murata_mur_r020_1kl_b0" } },
	{ 3, { 0x21, 0x22, }, { "murata_mur_r021_1kl_b0" } },
	{ 3, { 0x22, 0x22, }, { "murata_mur_r022_1kl_b0" } },
	{ 3, { 0x23, 0x22, }, { "murata_mur_r023_1kl_b0" } },
	{ 3, { 0x24, 0x22, }, { "murata_mur_r024_1kl_b0" } },
	{ 3, { 0x30, 0x22, }, { "murata_mur_r030_1kl_b0" } },
	{ 3, { 0x31, 0x22, }, { "murata_mur_r031_1kl_b0" } },
	{ 3, { 0x32, 0x22, }, { "murata_mur_r032_1kl_b0" } },
	{ 3, { 0x33, 0x22, }, { "murata_mur_r033_1kl_b0" } },
	{ 3, { 0x34, 0x22, }, { "murata_mur_r034_1kl_b0" } },
	{ 3, { 0x50, 0x22, }, { "murata_mur_r020_1qw_b2" } },
	{ 3, { 0x51, 0x22, }, { "murata_mur_r021_1qw_b2" } },
	{ 3, { 0x52, 0x22, }, { "murata_mur_r022_1qw_b2" } },
	{ 3, { 0x61, 0x22, }, { "murata_mur_r031_1qw_b2" } },
	{ 0, { 0x00, }, { "samsung" } }           /* Default: Not specified yet */
#endif /* SUPPORT_MIXED_MODULES */
};
#elif defined(BCM4375_CHIP)
vid_info_t vid_info[] = {
#if defined(SUPPORT_MIXED_MODULES)
	{ 3, { 0x11, 0x33, }, { "semco_sky_e41_es11" } },
	{ 3, { 0x33, 0x33, }, { "semco_sem_e43_es33" } },
	{ 3, { 0x34, 0x33, }, { "semco_sem_e43_es34" } },
	{ 3, { 0x35, 0x33, }, { "semco_sem_e43_es35" } },
	{ 3, { 0x36, 0x33, }, { "semco_sem_e43_es36" } },
	{ 3, { 0x41, 0x33, }, { "semco_sem_e43_cs41" } },
	{ 3, { 0x51, 0x33, }, { "semco_sem_e43_cs51" } },
	{ 3, { 0x53, 0x33, }, { "semco_sem_e43_cs53" } },
	{ 3, { 0x61, 0x33, }, { "semco_sky_e43_cs61" } },
	{ 3, { 0x10, 0x22, }, { "murata_mur_1rh_es10" } },
	{ 3, { 0x11, 0x22, }, { "murata_mur_1rh_es11" } },
	{ 3, { 0x12, 0x22, }, { "murata_mur_1rh_es12" } },
	{ 3, { 0x13, 0x22, }, { "murata_mur_1rh_es13" } },
	{ 3, { 0x20, 0x22, }, { "murata_mur_1rh_es20" } },
	{ 3, { 0x32, 0x22, }, { "murata_mur_1rh_es32" } },
	{ 3, { 0x41, 0x22, }, { "murata_mur_1rh_es41" } },
	{ 3, { 0x42, 0x22, }, { "murata_mur_1rh_es42" } },
	{ 3, { 0x43, 0x22, }, { "murata_mur_1rh_es43" } },
	{ 3, { 0x44, 0x22, }, { "murata_mur_1rh_es44" } }
#endif /* SUPPORT_MIXED_MODULES */
};
#elif defined(BCM4389_CHIP_DEF)
vid_info_t vid_info[] = {
#if defined(SUPPORT_MIXED_MODULES)
	{ 3, { 0x21, 0x33, }, { "semco_sem_e53_es23" } },
	{ 3, { 0x23, 0x33, }, { "semco_sem_e53_es23" } },
	{ 3, { 0x24, 0x33, }, { "semco_sem_e53_es24" } },
	{ 3, { 0x25, 0x33, }, { "semco_sem_e53_es25" } },
	{ 3, { 0x31, 0x33, }, { "semco_sem_e53_es31" } },
	{ 3, { 0x32, 0x33, }, { "semco_sem_e53_es32" } },
	{ 3, { 0x40, 0x33, }, { "semco_sem_e53_es40" } },
	{ 3, { 0x21, 0x22, }, { "murata_mur_1wk_es21" } },
	{ 3, { 0x30, 0x22, }, { "murata_mur_1wk_es30" } },
	{ 3, { 0x31, 0x22, }, { "murata_mur_1wk_es31" } },
	{ 3, { 0x32, 0x22, }, { "murata_mur_1wk_es32" } },
	{ 3, { 0x40, 0x22, }, { "murata_mur_1wk_es40" } },
	{ 3, { 0x41, 0x22, }, { "murata_mur_1wk_es41" } },
	{ 3, { 0x42, 0x22, }, { "murata_mur_1wk_es42" } },
	{ 3, { 0x43, 0x22, }, { "murata_mur_1wk_es43" } },
	{ 3, { 0x50, 0x22, }, { "murata_mur_1wk_es50" } }
#endif /* SUPPORT_MIXED_MODULES */
};
#else
vid_info_t vid_info[] = {
	{ 0, { 0x00, }, { "samsung" } }			/* Default: Not specified yet */
};
#endif /* BCM_CHIP_ID */

/* CID managment functions */

char *
dhd_get_cid_info(unsigned char *vid, int vid_length)
{
	int i;

	for (i = 0; i < ARRAYSIZE(vid_info); i++) {
		if (vid_info[i].vid_length-1 == vid_length &&
				!memcmp(vid_info[i].vid, vid, vid_length)) {
			return vid_info[i].cid_info;
		}
	}

	DHD_ERROR(("%s : Can't find the cid info\n", __FUNCTION__));
	return NULL;
}

int
dhd_check_module_cid(dhd_pub_t *dhdp)
{
	int ret = -1;
#ifndef DHD_EXPORT_CNTL_FILE
	const char *cidfilepath = CIDINFO;
#endif /* DHD_EXPORT_CNTL_FILE */
	int idx, max;
	vid_info_t *cur_info;
	unsigned char *tuple_start = NULL;
	unsigned char tuple_length = 0;
	unsigned char cid_info[MAX_VNAME_LEN];
	int found = FALSE;
#ifdef SUPPORT_MULTIPLE_BOARDTYPE
	board_info_t *cur_b_info = NULL;
	board_info_t *vendor_b_info = NULL;
	unsigned char *btype_start;
	unsigned char boardtype_len = 0;
#endif /* SUPPORT_MULTIPLE_BOARDTYPE */

	/* Try reading out from CIS */
	if (!g_cis_buf) {
		DHD_INFO(("%s: Couldn't read CIS info\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_INFO(("%s: Reading CIS from local buffer\n", __FUNCTION__));
#ifdef DUMP_CIS
	dhd_dump_cis_buf(dhdp, DUMP_CIS_SIZE);
#endif /* DUMP_CIS */

	idx = dhd_find_tuple_idx_from_otp(dhdp, CIS_TUPLE_TAG_VENDOR, &tuple_length);
	if (idx > 0) {
		found = TRUE;
		tuple_start = &g_cis_buf[idx];
	}

	if (found) {
		max = sizeof(vid_info) / sizeof(vid_info_t);
		for (idx = 0; idx < max; idx++) {
			cur_info = &vid_info[idx];
#ifdef BCM4358_CHIP
			if (cur_info->vid_length  == 6 && tuple_length == 6) {
				if (cur_info->vid[0] == tuple_start[0] &&
					cur_info->vid[3] == tuple_start[3] &&
					cur_info->vid[4] == tuple_start[4]) {
					goto check_board_type;
				}
			}
#endif /* BCM4358_CHIP */
			if ((cur_info->vid_length == tuple_length) &&
				(cur_info->vid_length != 0) &&
				(memcmp(cur_info->vid, tuple_start,
					cur_info->vid_length - 1) == 0)) {
				goto check_board_type;
			}
		}
	}

	/* find default nvram, if exist */
	DHD_ERROR(("%s: cannot find CIS TUPLE set as default\n", __FUNCTION__));
	max = sizeof(vid_info) / sizeof(vid_info_t);
	for (idx = 0; idx < max; idx++) {
		cur_info = &vid_info[idx];
		if (cur_info->vid_length == 0) {
			goto write_cid;
		}
	}
	DHD_ERROR(("%s: cannot find default CID\n", __FUNCTION__));
	return BCME_ERROR;

check_board_type:
#ifdef SUPPORT_MULTIPLE_BOARDTYPE
	idx = dhd_find_tuple_idx_from_otp(dhdp, CIS_TUPLE_TAG_BOARDTYPE, &tuple_length);
	if (idx > 0) {
		btype_start = &g_cis_buf[idx];
		boardtype_len = tuple_length;
		DHD_INFO(("%s: board type found.\n", __FUNCTION__));
	} else {
		boardtype_len = 0;
	}
#if defined(BCM4361_CHIP)
	vendor_b_info = semco_PA_info;
	max = sizeof(semco_PA_info) / sizeof(board_info_t);
#else
	if (strcmp(cur_info->cid_info, "semco") == 0) {
		vendor_b_info = semco_board_info;
		max = sizeof(semco_board_info) / sizeof(board_info_t);
	} else if (strcmp(cur_info->cid_info, "murata") == 0) {
		vendor_b_info = murata_board_info;
		max = sizeof(murata_board_info) / sizeof(board_info_t);
	} else {
		max = 0;
	}
#endif /* BCM4361_CHIP */
	if (boardtype_len) {
		for (idx = 0; idx < max; idx++) {
			cur_b_info = vendor_b_info;
			if ((cur_b_info->b_len == boardtype_len) &&
				(cur_b_info->b_len != 0) &&
				(memcmp(cur_b_info->btype, btype_start,
					cur_b_info->b_len - 1) == 0)) {
				DHD_INFO(("%s : board type name : %s\n",
					__FUNCTION__, cur_b_info->bname));
				break;
			}
			cur_b_info = NULL;
			vendor_b_info++;
		}
	}
#endif /* SUPPORT_MULTIPLE_BOARDTYPE */

write_cid:
#ifdef SUPPORT_MULTIPLE_BOARDTYPE
	if (cur_b_info && cur_b_info->b_len > 0) {
		strcpy(cid_info, cur_info->cid_info);
		strcpy(cid_info + strlen(cur_info->cid_info), cur_b_info->bname);
	} else
#endif /* SUPPORT_MULTIPLE_BOARDTYPE */
		strcpy(cid_info, cur_info->cid_info);

	DHD_INFO(("%s: CIS MATCH FOUND : %s\n", __FUNCTION__, cid_info));
#ifndef DHD_EXPORT_CNTL_FILE
	dhd_write_file(cidfilepath, cid_info, strlen(cid_info) + 1);
#else
	strlcpy(cidinfostr, cid_info, MAX_VNAME_LEN);
#endif /* DHD_EXPORT_CNTL_FILE */

	return ret;
}

#ifdef SUPPORT_MULTIPLE_MODULE_CIS
#ifndef DHD_EXPORT_CNTL_FILE
static bool
dhd_check_module(char *module_name)
{
	char vname[MAX_VNAME_LEN];
	const char *cidfilepath = CIDINFO;
	int ret;

	memset(vname, 0, sizeof(vname));
	ret = dhd_read_file(cidfilepath, vname, sizeof(vname) - 1);
	if (ret < 0) {
		return FALSE;
	}
	DHD_INFO(("%s: This module is %s \n", __FUNCTION__, vname));
	return strstr(vname, module_name) ? TRUE : FALSE;
}
#else
bool
dhd_check_module(char *module_name)
{
	return strstr(cidinfostr, module_name) ? TRUE : FALSE;
}
#endif /* !DHD_EXPORT_CNTL_FILE */

int
dhd_check_module_b85a(void)
{
	int ret;
	char *vname_b85a = "_b85a";

	if (dhd_check_module(vname_b85a)) {
		DHD_INFO(("%s: It's a b85a module\n", __FUNCTION__));
		ret = 1;
	} else {
		DHD_INFO(("%s: It is not a b85a module\n", __FUNCTION__));
		ret = -1;
	}

	return ret;
}

int
dhd_check_module_b90(void)
{
	int ret = 0;
	char *vname_b90b = "_b90b";
	char *vname_b90s = "_b90s";

	if (dhd_check_module(vname_b90b)) {
		DHD_INFO(("%s: It's a b90b module \n", __FUNCTION__));
		ret = BCM4359_MODULE_TYPE_B90B;
	} else if (dhd_check_module(vname_b90s)) {
		DHD_INFO(("%s: It's a b90s module\n", __FUNCTION__));
		ret = BCM4359_MODULE_TYPE_B90S;
	} else {
		DHD_ERROR(("%s: It's neither b90b nor b90s\n", __FUNCTION__));
		ret = BCME_ERROR;
	}

	return ret;
}
#endif /* SUPPORT_MULTIPLE_MODULE_CIS */

#define CID_FEM_MURATA	"_mur_"
/* extract module type from cid information */
/* XXX: extract string by delimiter '_' at specific counting position.
 * it would be used for module type information.
 * for example, cid information is 'semco_sky_r02a_e30a_a1',
 * then output (module type) is 'r02a_e30a_a1' when index is 3.
 */
int
dhd_check_module_bcm(char *module_type, int index, bool *is_murata_fem)
{
	int ret = 0, i;
	char vname[MAX_VNAME_LEN];
	char *ptr = NULL;
#ifndef DHD_EXPORT_CNTL_FILE
	const char *cidfilepath = CIDINFO;
#endif /* DHD_EXPORT_CNTL_FILE */

	memset(vname, 0, sizeof(vname));

#ifndef DHD_EXPORT_CNTL_FILE
	ret = dhd_read_file(cidfilepath, vname, sizeof(vname) - 1);
	if (ret < 0) {
		DHD_ERROR(("%s: failed to get module infomaion from .cid.info\n",
			__FUNCTION__));
		return ret;
	}
#else
	strlcpy(vname, cidinfostr, MAX_VNAME_LEN);
#endif /* DHD_EXPORT_CNTL_FILE */

	for (i = 1, ptr = vname; i < index && ptr; i++) {
		ptr = bcmstrstr(ptr, "_");
		if (ptr) {
			ptr++;
		}
	}

	if (bcmstrnstr(vname, MAX_VNAME_LEN, CID_FEM_MURATA, 5)) {
		*is_murata_fem = TRUE;
	}

	if (ptr) {
		memcpy(module_type, ptr, strlen(ptr));
	} else {
		DHD_ERROR(("%s: failed to get module infomaion\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_INFO(("%s: module type = %s \n", __FUNCTION__, module_type));

	return ret;
}
#endif /* USE_CID_CHECK */

#ifdef USE_DIRECT_VID_TAG
int
dhd_check_module_cid(dhd_pub_t *dhdp)
{
	int ret = BCME_ERROR;
	int idx;
	unsigned char tuple_length = 0;
	unsigned char *vid = NULL;
	unsigned char cid_info[MAX_VNAME_LEN];
#ifndef DHD_EXPORT_CNTL_FILE
	const char *cidfilepath = CIDINFO;
#endif /* DHD_EXPORT_CNTL_FILE */

	/* Try reading out from CIS */
	if (!g_cis_buf) {
		DHD_INFO(("%s: Couldn't read CIS info\n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_INFO(("%s: Reading CIS from local buffer\n", __FUNCTION__));
#ifdef DUMP_CIS
	dhd_dump_cis_buf(dhdp, DUMP_CIS_SIZE);
#endif /* DUMP_CIS */
	idx = dhd_find_tuple_idx_from_otp(dhdp, CIS_TUPLE_TAG_VENDOR, &tuple_length);
	if (idx > 0) {
		vid = &g_cis_buf[idx];
		DHD_INFO(("%s: VID FOUND : 0x%x%x\n", __FUNCTION__,
			vid[VENDOR_OFF], vid[MD_REV_OFF]));
	} else {
		DHD_ERROR(("%s: use nvram default\n", __FUNCTION__));
		return BCME_ERROR;
	}

	memset(cid_info, 0, sizeof(MAX_VNAME_LEN));
	cid_info[MD_REV_OFF] = vid[MD_REV_OFF];
	cid_info[VENDOR_OFF] = vid[VENDOR_OFF];
#ifndef DHD_EXPORT_CNTL_FILE
	dhd_write_file(cidfilepath, cid_info, strlen(cid_info) + 1);
#else
	strlcpy(cidinfostr, cid_info, MAX_VNAME_LEN);
#endif /* DHD_EXPORT_CNTL_FILE */

	DHD_INFO(("%s: cidinfostr %x%x\n", __FUNCTION__,
			cidinfostr[VENDOR_OFF], cidinfostr[MD_REV_OFF]));
	return ret;
}

int
dhd_check_stored_module_info(char *vid)
{
	int ret = BCME_OK;
#ifndef DHD_EXPORT_CNTL_FILE
	const char *cidfilepath = CIDINFO;
#endif /* DHD_EXPORT_CNTL_FILE */

	memset(vid, 0, MAX_VID_LEN);

#ifndef DHD_EXPORT_CNTL_FILE
	ret = dhd_read_file(cidfilepath, vid, MAX_VID_LEN - 1);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: failed to get module infomaion from .cid.info\n",
			__FUNCTION__));
		return ret;
	}
#else
	strlcpy(vid, cidinfostr, MAX_VID_LEN);
#endif /* DHD_EXPORT_CNTL_FILE */

	if (vid[0] == (char)0) {
		DHD_ERROR(("%s : Failed to get module information \n", __FUNCTION__));
		return BCME_ERROR;
	}

	DHD_INFO(("%s: stored VID= 0x%x%x\n", __FUNCTION__, vid[VENDOR_OFF], vid[MD_REV_OFF]));
	return ret;
}
#endif /* USE_DIRECT_VID_TAG */
#endif /* DHD_USE_CISINFO */
