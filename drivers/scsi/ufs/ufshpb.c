// SPDX-License-Identifier: GPL-2.0
/*
 * Universal Flash Storage Host Performance Booster
 *
 * Copyright (C) 2017-2021 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *	Yongmyung Lee <ymhungry.lee@samsung.com>
 *	Jinyoung Choi <j-young.choi@samsung.com>
 */

#include <asm/unaligned.h>
#include <linux/async.h>

#include "ufshcd.h"
#include "ufshpb.h"
#include "../sd.h"

bool ufshpb_is_allowed(struct ufs_hba *hba)
{
	return !(hba->ufshpb_dev.hpb_disabled);
}

static struct ufshpb_lu *ufshpb_get_hpb_data(struct scsi_device *sdev)
{
	return sdev->hostdata;
}

static int ufshpb_get_state(struct ufshpb_lu *hpb)
{
	return atomic_read(&hpb->hpb_state);
}

static void ufshpb_set_state(struct ufshpb_lu *hpb, int state)
{
	atomic_set(&hpb->hpb_state, state);
}

static void ufshpb_init_subregion_tbl(struct ufshpb_lu *hpb,
				      struct ufshpb_region *rgn, bool last)
{
	int srgn_idx;
	struct ufshpb_subregion *srgn;

	for (srgn_idx = 0; srgn_idx < rgn->srgn_cnt; srgn_idx++) {
		srgn = rgn->srgn_tbl + srgn_idx;

		srgn->rgn_idx = rgn->rgn_idx;
		srgn->srgn_idx = srgn_idx;
		srgn->srgn_state = HPB_SRGN_UNUSED;
	}

	if (unlikely(last && hpb->last_srgn_entries))
		srgn->is_last = true;
}

static int ufshpb_alloc_subregion_tbl(struct ufshpb_lu *hpb,
				      struct ufshpb_region *rgn, int srgn_cnt)
{
	rgn->srgn_tbl = kvcalloc(srgn_cnt, sizeof(struct ufshpb_subregion),
				 GFP_KERNEL);
	if (!rgn->srgn_tbl)
		return -ENOMEM;

	rgn->srgn_cnt = srgn_cnt;
	return 0;
}

static void ufshpb_lu_parameter_init(struct ufs_hba *hba,
				     struct ufshpb_lu *hpb,
				     struct ufshpb_dev_info *hpb_dev_info,
				     struct ufshpb_lu_info *hpb_lu_info)
{
	u32 entries_per_rgn;
	u64 rgn_mem_size, tmp;

	hpb->lu_pinned_start = hpb_lu_info->pinned_start;
	hpb->lu_pinned_end = hpb_lu_info->num_pinned ?
		(hpb_lu_info->pinned_start + hpb_lu_info->num_pinned - 1)
		: PINNED_NOT_SET;

	rgn_mem_size = (1ULL << hpb_dev_info->rgn_size) * HPB_RGN_SIZE_UNIT
			* HPB_ENTRY_SIZE;
	do_div(rgn_mem_size, HPB_ENTRY_BLOCK_SIZE);
	hpb->srgn_mem_size = (1ULL << hpb_dev_info->srgn_size)
		* HPB_RGN_SIZE_UNIT / HPB_ENTRY_BLOCK_SIZE * HPB_ENTRY_SIZE;

	tmp = rgn_mem_size;
	do_div(tmp, HPB_ENTRY_SIZE);
	entries_per_rgn = (u32)tmp;
	hpb->entries_per_rgn_shift = ilog2(entries_per_rgn);
	hpb->entries_per_rgn_mask = entries_per_rgn - 1;

	hpb->entries_per_srgn = hpb->srgn_mem_size / HPB_ENTRY_SIZE;
	hpb->entries_per_srgn_shift = ilog2(hpb->entries_per_srgn);
	hpb->entries_per_srgn_mask = hpb->entries_per_srgn - 1;

	tmp = rgn_mem_size;
	do_div(tmp, hpb->srgn_mem_size);
	hpb->srgns_per_rgn = (int)tmp;

	hpb->rgns_per_lu = DIV_ROUND_UP(hpb_lu_info->num_blocks,
				entries_per_rgn);
	hpb->srgns_per_lu = DIV_ROUND_UP(hpb_lu_info->num_blocks,
				(hpb->srgn_mem_size / HPB_ENTRY_SIZE));
	hpb->last_srgn_entries = hpb_lu_info->num_blocks
				 % (hpb->srgn_mem_size / HPB_ENTRY_SIZE);

	hpb->pages_per_srgn = DIV_ROUND_UP(hpb->srgn_mem_size, PAGE_SIZE);
}

static int ufshpb_alloc_region_tbl(struct ufs_hba *hba, struct ufshpb_lu *hpb)
{
	struct ufshpb_region *rgn_table, *rgn;
	int rgn_idx, i;
	int ret = 0;

	rgn_table = kvcalloc(hpb->rgns_per_lu, sizeof(struct ufshpb_region),
			    GFP_KERNEL);
	if (!rgn_table)
		return -ENOMEM;

	hpb->rgn_tbl = rgn_table;

	for (rgn_idx = 0; rgn_idx < hpb->rgns_per_lu; rgn_idx++) {
		int srgn_cnt = hpb->srgns_per_rgn;
		bool last_srgn = false;

		rgn = rgn_table + rgn_idx;
		rgn->rgn_idx = rgn_idx;

		if (rgn_idx == hpb->rgns_per_lu - 1) {
			srgn_cnt = ((hpb->srgns_per_lu - 1) %
				    hpb->srgns_per_rgn) + 1;
			last_srgn = true;
		}

		ret = ufshpb_alloc_subregion_tbl(hpb, rgn, srgn_cnt);
		if (ret)
			goto release_srgn_table;
		ufshpb_init_subregion_tbl(hpb, rgn, last_srgn);

		rgn->rgn_state = HPB_RGN_INACTIVE;
	}

	return 0;

release_srgn_table:
	for (i = 0; i < rgn_idx; i++)
		kvfree(rgn_table[i].srgn_tbl);

	kvfree(rgn_table);
	return ret;
}

static void ufshpb_destroy_subregion_tbl(struct ufshpb_lu *hpb,
					 struct ufshpb_region *rgn)
{
	int srgn_idx;

	for (srgn_idx = 0; srgn_idx < rgn->srgn_cnt; srgn_idx++) {
		struct ufshpb_subregion *srgn;

		srgn = rgn->srgn_tbl + srgn_idx;
		srgn->srgn_state = HPB_SRGN_UNUSED;
	}
}

static void ufshpb_destroy_region_tbl(struct ufshpb_lu *hpb)
{
	int rgn_idx;

	for (rgn_idx = 0; rgn_idx < hpb->rgns_per_lu; rgn_idx++) {
		struct ufshpb_region *rgn;

		rgn = hpb->rgn_tbl + rgn_idx;
		if (rgn->rgn_state != HPB_RGN_INACTIVE) {
			rgn->rgn_state = HPB_RGN_INACTIVE;

			ufshpb_destroy_subregion_tbl(hpb, rgn);
		}

		kvfree(rgn->srgn_tbl);
	}

	kvfree(hpb->rgn_tbl);
}

/* SYSFS functions */
#define ufshpb_sysfs_attr_show_func(__name)				\
static ssize_t __name##_show(struct device *dev,			\
	struct device_attribute *attr, char *buf)			\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);		\
									\
	if (!hpb)							\
		return -ENODEV;						\
									\
	return sysfs_emit(buf, "%llu\n", hpb->stats.__name);		\
}									\
\
static DEVICE_ATTR_RO(__name)

ufshpb_sysfs_attr_show_func(hit_cnt);
ufshpb_sysfs_attr_show_func(miss_cnt);
ufshpb_sysfs_attr_show_func(rb_noti_cnt);
ufshpb_sysfs_attr_show_func(rb_active_cnt);
ufshpb_sysfs_attr_show_func(rb_inactive_cnt);
ufshpb_sysfs_attr_show_func(map_req_cnt);

static struct attribute *hpb_dev_attrs[] = {
	&dev_attr_hit_cnt.attr,
	&dev_attr_miss_cnt.attr,
	&dev_attr_rb_noti_cnt.attr,
	&dev_attr_rb_active_cnt.attr,
	&dev_attr_rb_inactive_cnt.attr,
	&dev_attr_map_req_cnt.attr,
	NULL,
};

struct attribute_group ufs_sysfs_hpb_stat_group = {
	.name = "hpb_stats",
	.attrs = hpb_dev_attrs,
};

static void ufshpb_stat_init(struct ufshpb_lu *hpb)
{
	hpb->stats.hit_cnt = 0;
	hpb->stats.miss_cnt = 0;
	hpb->stats.rb_noti_cnt = 0;
	hpb->stats.rb_active_cnt = 0;
	hpb->stats.rb_inactive_cnt = 0;
	hpb->stats.map_req_cnt = 0;
}

static int ufshpb_lu_hpb_init(struct ufs_hba *hba, struct ufshpb_lu *hpb)
{
	int ret;

	ret = ufshpb_alloc_region_tbl(hba, hpb);

	ufshpb_stat_init(hpb);

	return 0;
}

static struct ufshpb_lu *
ufshpb_alloc_hpb_lu(struct ufs_hba *hba, int lun,
		    struct ufshpb_dev_info *hpb_dev_info,
		    struct ufshpb_lu_info *hpb_lu_info)
{
	struct ufshpb_lu *hpb;
	int ret;

	hpb = kzalloc(sizeof(struct ufshpb_lu), GFP_KERNEL);
	if (!hpb)
		return NULL;

	hpb->lun = lun;

	ufshpb_lu_parameter_init(hba, hpb, hpb_dev_info, hpb_lu_info);

	ret = ufshpb_lu_hpb_init(hba, hpb);
	if (ret) {
		dev_err(hba->dev, "hpb lu init failed. ret %d", ret);
		goto release_hpb;
	}

	return hpb;

release_hpb:
	kfree(hpb);
	return NULL;
}

static bool ufshpb_check_hpb_reset_query(struct ufs_hba *hba)
{
	int err = 0;
	bool flag_res = true;
	int try;

	/* wait for the device to complete HPB reset query */
	for (try = 0; try < HPB_RESET_REQ_RETRIES; try++) {
		dev_dbg(hba->dev,
			"%s start flag reset polling %d times\n",
			__func__, try);

		/* Poll fHpbReset flag to be cleared */
		err = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_READ_FLAG,
				QUERY_FLAG_IDN_HPB_RESET, 0, &flag_res);

		if (err) {
			dev_err(hba->dev,
				"%s reading fHpbReset flag failed with error %d\n",
				__func__, err);
			return flag_res;
		}

		if (!flag_res)
			goto out;

		usleep_range(1000, 1100);
	}
	if (flag_res) {
		dev_err(hba->dev,
			"%s fHpbReset was not cleared by the device\n",
			__func__);
	}
out:
	return flag_res;
}

void ufshpb_reset(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if (ufshpb_get_state(hpb) != HPB_RESET)
			continue;

		ufshpb_set_state(hpb, HPB_PRESENT);
	}
}

void ufshpb_reset_host(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if (ufshpb_get_state(hpb) != HPB_PRESENT)
			continue;
		ufshpb_set_state(hpb, HPB_RESET);
	}
}

void ufshpb_suspend(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if (ufshpb_get_state(hpb) != HPB_PRESENT)
			continue;
		ufshpb_set_state(hpb, HPB_SUSPEND);
	}
}

void ufshpb_resume(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if ((ufshpb_get_state(hpb) != HPB_PRESENT) &&
		    (ufshpb_get_state(hpb) != HPB_SUSPEND))
			continue;
		ufshpb_set_state(hpb, HPB_PRESENT);
	}
}

static int ufshpb_get_lu_info(struct ufs_hba *hba, int lun,
			      struct ufshpb_lu_info *hpb_lu_info)
{
	u16 max_active_rgns;
	u8 lu_enable;
	int size;
	int ret;
	char desc_buf[QUERY_DESC_MAX_SIZE];

	ufshcd_map_desc_id_to_length(hba, QUERY_DESC_IDN_UNIT, &size);

	pm_runtime_get_sync(hba->dev);
	ret = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
					    QUERY_DESC_IDN_UNIT, lun, 0,
					    desc_buf, &size);
	pm_runtime_put_sync(hba->dev);

	if (ret) {
		dev_err(hba->dev,
			"%s: idn: %d lun: %d  query request failed",
			__func__, QUERY_DESC_IDN_UNIT, lun);
		return ret;
	}

	lu_enable = desc_buf[UNIT_DESC_PARAM_LU_ENABLE];
	if (lu_enable != LU_ENABLED_HPB_FUNC)
		return -ENODEV;

	max_active_rgns = get_unaligned_be16(
			desc_buf + UNIT_DESC_PARAM_HPB_LU_MAX_ACTIVE_RGNS);
	if (!max_active_rgns) {
		dev_err(hba->dev,
			"lun %d wrong number of max active regions\n", lun);
		return -ENODEV;
	}

	hpb_lu_info->num_blocks = get_unaligned_be64(
			desc_buf + UNIT_DESC_PARAM_LOGICAL_BLK_COUNT);
	hpb_lu_info->pinned_start = get_unaligned_be16(
			desc_buf + UNIT_DESC_PARAM_HPB_PIN_RGN_START_OFF);
	hpb_lu_info->num_pinned = get_unaligned_be16(
			desc_buf + UNIT_DESC_PARAM_HPB_NUM_PIN_RGNS);
	hpb_lu_info->max_active_rgns = max_active_rgns;

	return 0;
}

void ufshpb_destroy_lu(struct ufs_hba *hba, struct scsi_device *sdev)
{
	struct ufshpb_lu *hpb = sdev->hostdata;

	if (!hpb)
		return;

	ufshpb_set_state(hpb, HPB_FAILED);

	sdev = hpb->sdev_ufs_lu;
	sdev->hostdata = NULL;

	ufshpb_destroy_region_tbl(hpb);

	list_del_init(&hpb->list_hpb_lu);

	kfree(hpb);
}

static void ufshpb_hpb_lu_prepared(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;
	bool init_success;

	init_success = !ufshpb_check_hpb_reset_query(hba);

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if (init_success) {
			ufshpb_set_state(hpb, HPB_PRESENT);
		} else {
			dev_err(hba->dev, "destroy HPB lu %d\n", hpb->lun);
			ufshpb_destroy_lu(hba, sdev);
		}
	}
}

void ufshpb_init_hpb_lu(struct ufs_hba *hba, struct scsi_device *sdev)
{
	struct ufshpb_lu *hpb;
	int ret;
	struct ufshpb_lu_info hpb_lu_info = { 0 };
	int lun = sdev->lun;

	if (lun >= hba->dev_info.max_lu_supported)
		goto out;

	ret = ufshpb_get_lu_info(hba, lun, &hpb_lu_info);
	if (ret)
		goto out;

	hpb = ufshpb_alloc_hpb_lu(hba, lun, &hba->ufshpb_dev,
				  &hpb_lu_info);
	if (!hpb)
		goto out;

	hpb->sdev_ufs_lu = sdev;
	sdev->hostdata = hpb;

out:
	/* All LUs are initialized */
	if (atomic_dec_and_test(&hba->ufshpb_dev.slave_conf_cnt))
		ufshpb_hpb_lu_prepared(hba);
}

void ufshpb_get_geo_info(struct ufs_hba *hba, u8 *geo_buf)
{
	struct ufshpb_dev_info *hpb_info = &hba->ufshpb_dev;
	int max_active_rgns = 0;
	int hpb_num_lu;

	hpb_num_lu = geo_buf[GEOMETRY_DESC_PARAM_HPB_NUMBER_LU];
	if (hpb_num_lu == 0) {
		dev_err(hba->dev, "No HPB LU supported\n");
		hpb_info->hpb_disabled = true;
		return;
	}

	hpb_info->rgn_size = geo_buf[GEOMETRY_DESC_PARAM_HPB_REGION_SIZE];
	hpb_info->srgn_size = geo_buf[GEOMETRY_DESC_PARAM_HPB_SUBREGION_SIZE];
	max_active_rgns = get_unaligned_be16(geo_buf +
			  GEOMETRY_DESC_PARAM_HPB_MAX_ACTIVE_REGS);

	if (hpb_info->rgn_size == 0 || hpb_info->srgn_size == 0 ||
	    max_active_rgns == 0) {
		dev_err(hba->dev, "No HPB supported device\n");
		hpb_info->hpb_disabled = true;
		return;
	}
}

void ufshpb_get_dev_info(struct ufs_hba *hba, u8 *desc_buf)
{
	struct ufshpb_dev_info *hpb_dev_info = &hba->ufshpb_dev;
	int version;
	u8 hpb_mode;

	hpb_mode = desc_buf[DEVICE_DESC_PARAM_HPB_CONTROL];
	if (hpb_mode == HPB_HOST_CONTROL) {
		dev_err(hba->dev, "%s: host control mode is not supported.\n",
			__func__);
		hpb_dev_info->hpb_disabled = true;
		return;
	}

	version = get_unaligned_be16(desc_buf + DEVICE_DESC_PARAM_HPB_VER);
	if (version != HPB_SUPPORT_VERSION) {
		dev_err(hba->dev, "%s: HPB %x version is not supported.\n",
			__func__, version);
		hpb_dev_info->hpb_disabled = true;
		return;
	}

	/*
	 * Get the number of user logical unit to check whether all
	 * scsi_device finish initialization
	 */
	hpb_dev_info->num_lu = desc_buf[DEVICE_DESC_PARAM_NUM_LU];
}

void ufshpb_init(struct ufs_hba *hba)
{
	struct ufshpb_dev_info *hpb_dev_info = &hba->ufshpb_dev;
	int try;
	int ret;

	if (!ufshpb_is_allowed(hba) || !hba->dev_info.hpb_enabled)
		return;

	atomic_set(&hpb_dev_info->slave_conf_cnt, hpb_dev_info->num_lu);
	/* issue HPB reset query */
	for (try = 0; try < HPB_RESET_REQ_RETRIES; try++) {
		ret = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_SET_FLAG,
					QUERY_FLAG_IDN_HPB_RESET, 0, NULL);
		if (!ret)
			break;
	}
}
