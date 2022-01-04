/* SPDX-License-Identifier: GPL-2.0-only */
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef __HINIC_DEVLINK_H__
#define __HINIC_DEVLINK_H__

#include <net/devlink.h>
#include "hinic_dev.h"

#define MAX_FW_TYPE_NUM 30
#define HINIC_MAGIC_NUM 0x18221100
#define UPDATEFW_IMAGE_HEAD_SIZE 1024
#define FW_UPDATE_COLD 0
#define FW_UPDATE_HOT  1

#define UP_TYPE_A 0x0
#define UP_TYPE_B 0x1

#define MAX_FW_FRAGMENT_LEN 1536
#define HINIC_FW_DISMATCH_ERROR 10

enum hinic_fw_type {
	UP_FW_UPDATE_UP_TEXT_A = 0x0,
	UP_FW_UPDATE_UP_DATA_A,
	UP_FW_UPDATE_UP_TEXT_B,
	UP_FW_UPDATE_UP_DATA_B,
	UP_FW_UPDATE_UP_DICT,

	UP_FW_UPDATE_HLINK_ONE = 0x5,
	UP_FW_UPDATE_HLINK_TWO,
	UP_FW_UPDATE_HLINK_THR,
	UP_FW_UPDATE_PHY,
	UP_FW_UPDATE_TILE_TEXT,

	UP_FW_UPDATE_TILE_DATA = 0xa,
	UP_FW_UPDATE_TILE_DICT,
	UP_FW_UPDATE_PPE_STATE,
	UP_FW_UPDATE_PPE_BRANCH,
	UP_FW_UPDATE_PPE_EXTACT,

	UP_FW_UPDATE_CLP_LEGACY = 0xf,
	UP_FW_UPDATE_PXE_LEGACY,
	UP_FW_UPDATE_ISCSI_LEGACY,
	UP_FW_UPDATE_CLP_EFI,
	UP_FW_UPDATE_PXE_EFI,

	UP_FW_UPDATE_ISCSI_EFI = 0x14,
	UP_FW_UPDATE_CFG,
	UP_FW_UPDATE_BOOT,
	UP_FW_UPDATE_VPD,
	FILE_TYPE_TOTAL_NUM
};

#define _IMAGE_UP_ALL_IN ((1 << UP_FW_UPDATE_UP_TEXT_A) | \
			  (1 << UP_FW_UPDATE_UP_DATA_A) | \
			  (1 << UP_FW_UPDATE_UP_TEXT_B) | \
			  (1 << UP_FW_UPDATE_UP_DATA_B) | \
			  (1 << UP_FW_UPDATE_UP_DICT) | \
			  (1 << UP_FW_UPDATE_BOOT) | \
			  (1 << UP_FW_UPDATE_HLINK_ONE) | \
			  (1 << UP_FW_UPDATE_HLINK_TWO) | \
			  (1 << UP_FW_UPDATE_HLINK_THR))

#define _IMAGE_UCODE_ALL_IN ((1 << UP_FW_UPDATE_TILE_TEXT) | \
			     (1 << UP_FW_UPDATE_TILE_DICT) | \
			     (1 << UP_FW_UPDATE_PPE_STATE) | \
			     (1 << UP_FW_UPDATE_PPE_BRANCH) | \
			     (1 << UP_FW_UPDATE_PPE_EXTACT))

#define _IMAGE_COLD_SUB_MODULES_MUST_IN (_IMAGE_UP_ALL_IN | _IMAGE_UCODE_ALL_IN)
#define _IMAGE_HOT_SUB_MODULES_MUST_IN (_IMAGE_UP_ALL_IN | _IMAGE_UCODE_ALL_IN)
#define _IMAGE_CFG_SUB_MODULES_MUST_IN BIT(UP_FW_UPDATE_CFG)
#define UP_FW_UPDATE_UP_TEXT  0x0
#define UP_FW_UPDATE_UP_DATA  0x1
#define UP_FW_UPDATE_VPD_B    0x15

struct fw_section_info_st {
	u32 fw_section_len;
	u32 fw_section_offset;
	u32 fw_section_version;
	u32 fw_section_type;
	u32 fw_section_crc;
};

struct fw_image_st {
	u32 fw_version;
	u32 fw_len;
	u32 fw_magic;
	struct {
		u32 fw_section_cnt:16;
		u32 resd:16;
	} fw_info;
	struct fw_section_info_st fw_section_info[MAX_FW_TYPE_NUM];
	u32 device_id;
	u32 res[101];
	void *bin_data;
};

struct host_image_st {
	struct fw_section_info_st image_section_info[MAX_FW_TYPE_NUM];
	struct {
		u32 up_total_len;
		u32 fw_version;
	} image_info;
	u32 section_type_num;
	u32 device_id;
};

struct devlink *hinic_devlink_alloc(struct device *dev);
void hinic_devlink_free(struct devlink *devlink);
int hinic_devlink_register(struct hinic_devlink_priv *priv);
void hinic_devlink_unregister(struct hinic_devlink_priv *priv);

int hinic_health_reporters_create(struct hinic_devlink_priv *priv);
void hinic_health_reporters_destroy(struct hinic_devlink_priv *priv);

#endif /* __HINIC_DEVLINK_H__ */
