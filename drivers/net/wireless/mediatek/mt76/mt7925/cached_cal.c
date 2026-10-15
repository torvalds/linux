// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2026 MediaTek Inc. */

#include <linux/dma-mapping.h>
#include "../mt792x.h"
#include "cached_cal.h"

static void mt7925_cal_cache_calc_layout(struct mt7925_cal_cache_layout *l)
{
	u32 hdr = sizeof(struct mt7925_fact_cal_buf_info);

	l->common_stride  = hdr + MT7925_FACT_CAL_LEN_COM;
	l->group_stride   = hdr + MT7925_FACT_CAL_LEN_GRP;
	l->channel_stride = hdr + MT7925_FACT_CAL_LEN_CH;

	l->common_off  = 0;
	l->group_off   = l->common_off +
		l->common_stride * MT7925_FACT_CAL_COMMON_BAND_NUM;
	l->channel_off = l->group_off +
		l->group_stride * MT7925_FACT_CAL_GROUP_NUM;
	l->mapping_off = l->channel_off +
		l->channel_stride * MT7925_FACT_CAL_CH_NUM_ALL;
	l->total = l->mapping_off +
		sizeof(struct mt7925_fact_cal_mapping_tbl);
}

static u32 mt7925_cal_cache_region_size(void)
{
	struct mt7925_cal_cache_layout l;

	mt7925_cal_cache_calc_layout(&l);

	return l.total;
}

int mt7925_axidma_alloc(struct mt792x_dev *dev)
{
	u32 total_size;
	dma_addr_t dma_addr;
	void *va;

	if (dev->cached_cal.va)
		return 0;

	total_size = mt7925_cal_cache_region_size();

	va = dma_alloc_coherent(dev->mt76.dev, total_size, &dma_addr,
				GFP_KERNEL);
	if (!va)
		return -ENOMEM;

	dev->cached_cal.va = va;
	dev->cached_cal.dma_addr = dma_addr;
	dev->cached_cal.size = total_size;

	return 0;
}

void mt7925_axidma_free(struct mt792x_dev *dev)
{
	if (!dev->cached_cal.va)
		return;

	dma_free_coherent(dev->mt76.dev, dev->cached_cal.size,
			  dev->cached_cal.va, dev->cached_cal.dma_addr);

	memset(&dev->cached_cal, 0, sizeof(dev->cached_cal));
}

int mt7925_mcu_send_axidma_info(struct mt792x_dev *dev)
{
	struct mt7925_axidma_info_req *req;
	u32 remap_base, pa_offset;
	int ret;

	if (!dev->cached_cal.va)
		return -EINVAL;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	remap_base = (u32)(dev->cached_cal.dma_addr >> 16);
	pa_offset = (u32)(dev->cached_cal.dma_addr & 0xFFFF);

	req->remap.tag = cpu_to_le16(MT7925_AXIDMA_TAG_REMAP);
	req->remap.len = cpu_to_le16(sizeof(req->remap));
	req->remap.remap_addr = cpu_to_le32(remap_base);

	req->fact_cal.tag = cpu_to_le16(MT7925_AXIDMA_TAG_FACT_CAL);
	req->fact_cal.len = cpu_to_le16(sizeof(req->fact_cal));
	req->fact_cal.host_addr_offset = cpu_to_le32(pa_offset);
	req->fact_cal.size = cpu_to_le32(dev->cached_cal.size);
	req->fact_cal.bound = cpu_to_le32(pa_offset + dev->cached_cal.size - 1);

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(AXIDMA),
				req, sizeof(*req), true);

	kfree(req);

	if (ret)
		dev_err(dev->mt76.dev,
			"failed to send AXI DMA info: %d\n", ret);

	return ret;
}

static bool mt7925_cal_cache_read_slot(struct mt792x_dev *dev, u32 region_off,
				       u32 stride, u32 idx,
				       u32 *cal_param, u32 *total_buf_num)
{
	const struct mt7925_fact_cal_buf_info *bi;
	u32 off = region_off + stride * idx;

	if (!dev->cached_cal.va || off + sizeof(*bi) > dev->cached_cal.size)
		return false;

	bi = (const struct mt7925_fact_cal_buf_info *)
		((const u8 *)dev->cached_cal.va + off);
	if (cal_param)
		*cal_param = le32_to_cpu(bi->cal_param);
	if (total_buf_num)
		*total_buf_num = le32_to_cpu(bi->total_buf_num);
	return true;
}

static void mt7925_fact_cal_build_mapping_tbl(struct mt792x_dev *dev,
					      struct mt7925_fact_cal_mapping_tbl *tbl)
{
	dma_addr_t common_pa, group_pa, channel_pa, mapping_pa;
	u32 hdr = sizeof(struct mt7925_fact_cal_buf_info);
	dma_addr_t base = dev->cached_cal.dma_addr;
	struct mt7925_cal_cache_layout l;

	mt7925_cal_cache_calc_layout(&l);

	common_pa  = base + l.common_off;
	group_pa   = base + l.group_off;
	channel_pa = base + l.channel_off;
	mapping_pa = base + l.mapping_off;

	memset(tbl, 0, sizeof(*tbl));

	tbl->common.phy_addr_h = cpu_to_le32(upper_32_bits(common_pa));
	tbl->common.phy_addr_l = cpu_to_le32(lower_32_bits(common_pa));
	tbl->common.offset     = cpu_to_le32(l.common_stride);

	tbl->group.phy_addr_h  = cpu_to_le32(upper_32_bits(group_pa));
	tbl->group.phy_addr_l  = cpu_to_le32(lower_32_bits(group_pa));
	tbl->group.offset      = cpu_to_le32(l.group_stride);

	tbl->channel.phy_addr_h = cpu_to_le32(upper_32_bits(channel_pa));
	tbl->channel.phy_addr_l = cpu_to_le32(lower_32_bits(channel_pa));
	tbl->channel.offset     = cpu_to_le32(l.channel_stride);

	if (dev->cached_cal.reused) {
		u32 i, cal_param, total_buf_num;

		for (i = 0; i < MT7925_FACT_CAL_COMMON_BAND_NUM; i++)
			if (mt7925_cal_cache_read_slot(dev, l.common_off,
						       l.common_stride, i,
						       &cal_param, NULL))
				tbl->common.band[i] = cal_param & 0xff;

		for (i = 0; i < MT7925_FACT_CAL_GROUP_NUM; i++)
			if (mt7925_cal_cache_read_slot(dev, l.group_off,
						       l.group_stride, i,
						       &cal_param, NULL))
				tbl->group.group[i] = cal_param & 0xff;

		for (i = 0; i < MT7925_FACT_CAL_CH_NUM_ALL; i++)
			if (mt7925_cal_cache_read_slot(dev, l.channel_off,
						       l.channel_stride, i,
						       &cal_param,
						       &total_buf_num)) {
				tbl->channel.cal_param[i] =
					cpu_to_le32(cal_param);
				tbl->channel.total_buf_num[i] =
					total_buf_num & 0xff;
			}
	}

	tbl->buf_info_size = cpu_to_le32(hdr);
	tbl->phy_addr_h    = cpu_to_le32(upper_32_bits(mapping_pa));
	tbl->phy_addr_l    = cpu_to_le32(lower_32_bits(mapping_pa));
}

static int mt7925_mcu_fact_cal_rapid_set(struct mt792x_dev *dev, u8 cal_type,
					 u32 seq_num, bool done,
					 const void *data, u32 data_len)
{
	struct mt7925_fact_cal_req *req;
	int ret;

	if (data_len > MT7925_FACT_CAL_DATA_BUF_LEN)
		return -EINVAL;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->set.tag = cpu_to_le16(MT7925_FACT_CAL_TAG_RAPID_SET);
	req->set.len = cpu_to_le16(sizeof(req->set));
	req->set.seq_num = cpu_to_le32(seq_num);
	req->set.buf_data_len = cpu_to_le32(data_len);
	req->set.cal_type = cal_type;
	req->set.done = done;
	if (data && data_len)
		memcpy(req->set.buf_data, data, data_len);

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(FACT_CAL),
				req, sizeof(*req), true);
	kfree(req);

	return ret;
}

int mt7925_mcu_send_fact_cal_mapping_tbl(struct mt792x_dev *dev)
{
	struct mt7925_fact_cal_mapping_tbl *tbl;
	__le32 cfg[MT7925_FACT_CAL_BUF_CFG_LEN / sizeof(__le32)] = {0};
	u32 seq = 0, off = 0, left;
	int ret;

	if (!dev->cached_cal.va)
		return -EINVAL;

	tbl = kzalloc(sizeof(*tbl), GFP_KERNEL);
	if (!tbl)
		return -ENOMEM;

	mt7925_fact_cal_build_mapping_tbl(dev, tbl);

	cfg[1] = cpu_to_le32(sizeof(*tbl));
	cfg[2] = cpu_to_le32(MT7925_FACT_CAL_DATA_BUF_NUM_MAX);
	ret = mt7925_mcu_fact_cal_rapid_set(dev, MT7925_FACT_CAL_TYPE_MAPPING_TBL,
					    seq, false, cfg, sizeof(cfg));
	if (ret) {
		dev_err(dev->mt76.dev,
			"fact-cal mapping tbl header send failed (seq=%u): %d\n",
			seq, ret);
		goto out;
	}

	left = sizeof(*tbl);
	while (left) {
		u32 chunk = min_t(u32, left, MT7925_FACT_CAL_DATA_BUF_LEN);

		ret = mt7925_mcu_fact_cal_rapid_set(dev,
						    MT7925_FACT_CAL_TYPE_MAPPING_TBL,
						    ++seq, false,
						    (u8 *)tbl + off, chunk);
		if (ret) {
			dev_err(dev->mt76.dev,
				"fact-cal mapping tbl data send failed (seq=%u, off=%u, chunk=%u): %d\n",
				seq, off, chunk, ret);
			goto out;
		}

		off  += chunk;
		left -= chunk;
	}

	ret = mt7925_mcu_fact_cal_rapid_set(dev, MT7925_FACT_CAL_TYPE_MAPPING_TBL,
					    ++seq, true, NULL, 0);
	if (ret) {
		dev_err(dev->mt76.dev,
			"fact-cal mapping tbl done send failed (seq=%u): %d\n",
			seq, ret);
		goto out;
	}
out:
	kfree(tbl);

	return ret;
}

int mt7925_mcu_fact_cal_set_bypass(struct mt792x_dev *dev, bool enable)
{
	struct {
		u8 _rsv[4];

		struct {
			__le16 tag;
			__le16 len;
			__le32 data;
		} __packed update_flag;
	} req = {
		.update_flag = {
			.tag  = cpu_to_le16(MT7925_FACT_CAL_TAG_UPDATE_FLAG),
			.len  = cpu_to_le16(sizeof(req.update_flag)),
			.data = cpu_to_le32(enable ? 1 : 0),
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(FACT_CAL),
				 &req, sizeof(req), true);
}
