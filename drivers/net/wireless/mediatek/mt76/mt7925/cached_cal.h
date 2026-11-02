/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2026 MediaTek Inc. */

#ifndef __CACHED_CAL_H
#define __CACHED_CAL_H

enum mt7925_axidma_info_tag {
	MT7925_AXIDMA_TAG_REMAP = 0,
	MT7925_AXIDMA_TAG_FACT_CAL = 1,
	MT7925_AXIDMA_TAG_NUM
};

enum mt7925_fact_cal_tag {
	MT7925_FACT_CAL_TAG_TRIGGER	= 2,
	MT7925_FACT_CAL_TAG_UPDATE_FLAG	= 3,
	MT7925_FACT_CAL_TAG_RAPID_GET	= 4,
	MT7925_FACT_CAL_TAG_RAPID_SET	= 5,
};

enum mt7925_fact_cal_type {
	MT7925_FACT_CAL_TYPE_COMMON		= 0,
	MT7925_FACT_CAL_TYPE_GROUP		= 1,
	MT7925_FACT_CAL_TYPE_CHANNEL		= 2,
	MT7925_FACT_CAL_TYPE_MAPPING_TBL	= 4,
};

#define MT7925_FACT_CAL_DATA_BUF_LEN		1400
#define MT7925_FACT_CAL_BUF_CFG_LEN			16
#define MT7925_FACT_CAL_DATA_BUF_NUM_MAX	4
#define MT7925_FACT_CAL_BUF_CFG_U32_LEN		4
#define MT7925_FACT_CAL_DATA_MAX_BUF_LEN \
	(MT7925_FACT_CAL_DATA_BUF_NUM_MAX * MT7925_FACT_CAL_BUF_CFG_U32_LEN)

#define MT7925_FACT_CAL_COMMON_BAND_NUM		2
#define MT7925_FACT_CAL_GROUP_NUM		35
#define MT7925_FACT_CAL_CH_NUM_2G		14
#define MT7925_FACT_CAL_CH_NUM_5G		68
#define MT7925_FACT_CAL_CH_NUM_6G		109
#define MT7925_FACT_CAL_CH_NUM_ALL \
	(MT7925_FACT_CAL_CH_NUM_2G + MT7925_FACT_CAL_CH_NUM_5G + \
	 MT7925_FACT_CAL_CH_NUM_6G)

#define MT7925_FACT_CAL_LEN_COM			600
#define MT7925_FACT_CAL_LEN_GRP			4500
#define MT7925_FACT_CAL_LEN_CH			2800

struct mt7925_fact_cal_buf_info {
	__le32 cal_type;
	__le32 cal_param;
	__le32 buf_seq_num;
	__le32 total_buf_num;
	__le32 buf_data_len;
	u8 valid;
	u8 done;
	u8 rsv[2];
	__le32 buf_cfg_info[MT7925_FACT_CAL_DATA_MAX_BUF_LEN];
} __packed;

static_assert(sizeof(struct mt7925_fact_cal_buf_info) == 88,
	      "buf_info must match the MT7928 FW (88 bytes) or it rejects the mapping table");

struct mt7925_fact_cal_common_map {
	__le32 phy_addr_h;
	__le32 phy_addr_l;
	__le32 offset;
	u8 band[MT7925_FACT_CAL_COMMON_BAND_NUM];
	u8 rsv[2];
} __packed;

struct mt7925_fact_cal_group_map {
	__le32 phy_addr_h;
	__le32 phy_addr_l;
	__le32 offset;
	u8 group[MT7925_FACT_CAL_GROUP_NUM];
	u8 rsv;
} __packed;

struct mt7925_fact_cal_channel_map {
	__le32 phy_addr_h;
	__le32 phy_addr_l;
	__le32 offset;
	__le32 cal_param[MT7925_FACT_CAL_CH_NUM_ALL];
	u8 total_buf_num[MT7925_FACT_CAL_CH_NUM_ALL];
	u8 rsv;
} __packed;

struct mt7925_fact_cal_mapping_tbl {
	struct mt7925_fact_cal_common_map common;
	struct mt7925_fact_cal_group_map group;
	struct mt7925_fact_cal_channel_map channel;
	__le32 buf_info_size;
	__le32 phy_addr_h;
	__le32 phy_addr_l;
} __packed;

static_assert(sizeof(struct mt7925_fact_cal_mapping_tbl) == 1044,
	      "mapping table must match the MT7928 FW (1044 bytes) or the FW parse corrupts");

struct mt7925_cal_cache_layout {
	u32 common_off, group_off, channel_off, mapping_off;
	u32 common_stride, group_stride, channel_stride;
	u32 total;
};

struct mt7925_axidma_info_req {
	u8 _rsv[4];

	struct {
		__le16 tag;
		__le16 len;
		__le32 remap_addr;
		__le32 reserved;
	} __packed remap;

	struct {
		__le16 tag;
		__le16 len;
		__le32 host_addr_offset;
		__le32 size;
		__le32 bound;
		__le32 reserved;
	} __packed fact_cal;
} __packed;

struct mt7925_fact_cal_req {
	u8 _rsv[4];

	struct {
		__le16 tag;
		__le16 len;
		__le32 data;
		__le32 seq_num;
		__le32 buf_data_len;
		u8 cal_type;
		u8 done;
		u8 rsv[2];
		u8 buf_data[MT7925_FACT_CAL_DATA_BUF_LEN];
	} __packed set;
} __packed;

int mt7925_axidma_alloc(struct mt792x_dev *dev);

void mt7925_axidma_free(struct mt792x_dev *dev);

int mt7925_mcu_send_axidma_info(struct mt792x_dev *dev);

int mt7925_mcu_send_fact_cal_mapping_tbl(struct mt792x_dev *dev);

int mt7925_mcu_fact_cal_set_bypass(struct mt792x_dev *dev, bool enable);

#endif /* __CACHED_CAL_H */
