// SPDX-License-Identifier: GPL-2.0-only
/*
 * CXL EDAC memory feature driver.
 *
 * Copyright (c) 2024-2025 HiSilicon Limited.
 *
 *  - Supports functions to configure EDAC features of the
 *    CXL memory devices.
 *  - Registers with the EDAC device subsystem driver to expose
 *    the features sysfs attributes to the user for configuring
 *    CXL memory RAS feature.
 */

#include <linux/cleanup.h>
#include <linux/edac.h>
#include <linux/limits.h>
#include <cxl/features.h>
#include <cxl.h>
#include <cxlmem.h>
#include "core.h"

#define CXL_NR_EDAC_DEV_FEATURES 2

#define CXL_SCRUB_NO_REGION -1

struct cxl_patrol_scrub_context {
	u8 instance;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 effects;
	struct cxl_memdev *cxlmd;
	struct cxl_region *cxlr;
};

/*
 * See CXL spec rev 3.2 @8.2.10.9.11.1 Table 8-222 Device Patrol Scrub Control
 * Feature Readable Attributes.
 */
struct cxl_scrub_rd_attrbs {
	u8 scrub_cycle_cap;
	__le16 scrub_cycle_hours;
	u8 scrub_flags;
} __packed;

/*
 * See CXL spec rev 3.2 @8.2.10.9.11.1 Table 8-223 Device Patrol Scrub Control
 * Feature Writable Attributes.
 */
struct cxl_scrub_wr_attrbs {
	u8 scrub_cycle_hours;
	u8 scrub_flags;
} __packed;

#define CXL_SCRUB_CONTROL_CHANGEABLE BIT(0)
#define CXL_SCRUB_CONTROL_REALTIME BIT(1)
#define CXL_SCRUB_CONTROL_CYCLE_MASK GENMASK(7, 0)
#define CXL_SCRUB_CONTROL_MIN_CYCLE_MASK GENMASK(15, 8)
#define CXL_SCRUB_CONTROL_ENABLE BIT(0)

#define CXL_GET_SCRUB_CYCLE_CHANGEABLE(cap) \
	FIELD_GET(CXL_SCRUB_CONTROL_CHANGEABLE, cap)
#define CXL_GET_SCRUB_CYCLE(cycle) \
	FIELD_GET(CXL_SCRUB_CONTROL_CYCLE_MASK, cycle)
#define CXL_GET_SCRUB_MIN_CYCLE(cycle) \
	FIELD_GET(CXL_SCRUB_CONTROL_MIN_CYCLE_MASK, cycle)
#define CXL_GET_SCRUB_EN_STS(flags) FIELD_GET(CXL_SCRUB_CONTROL_ENABLE, flags)

#define CXL_SET_SCRUB_CYCLE(cycle) \
	FIELD_PREP(CXL_SCRUB_CONTROL_CYCLE_MASK, cycle)
#define CXL_SET_SCRUB_EN(en) FIELD_PREP(CXL_SCRUB_CONTROL_ENABLE, en)

static int cxl_mem_scrub_get_attrbs(struct cxl_mailbox *cxl_mbox, u8 *cap,
				    u16 *cycle, u8 *flags, u8 *min_cycle)
{
	size_t rd_data_size = sizeof(struct cxl_scrub_rd_attrbs);
	size_t data_size;
	struct cxl_scrub_rd_attrbs *rd_attrbs __free(kfree) =
		kzalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrbs)
		return -ENOMEM;

	data_size = cxl_get_feature(cxl_mbox, &CXL_FEAT_PATROL_SCRUB_UUID,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE, rd_attrbs,
				    rd_data_size, 0, NULL);
	if (!data_size)
		return -EIO;

	*cap = rd_attrbs->scrub_cycle_cap;
	*cycle = le16_to_cpu(rd_attrbs->scrub_cycle_hours);
	*flags = rd_attrbs->scrub_flags;
	if (min_cycle)
		*min_cycle = CXL_GET_SCRUB_MIN_CYCLE(*cycle);

	return 0;
}

static int cxl_scrub_get_attrbs(struct cxl_patrol_scrub_context *cxl_ps_ctx,
				u8 *cap, u16 *cycle, u8 *flags, u8 *min_cycle)
{
	struct cxl_mailbox *cxl_mbox;
	u8 min_scrub_cycle = U8_MAX;
	struct cxl_region_params *p;
	struct cxl_memdev *cxlmd;
	struct cxl_region *cxlr;
	int i, ret;

	if (!cxl_ps_ctx->cxlr) {
		cxl_mbox = &cxl_ps_ctx->cxlmd->cxlds->cxl_mbox;
		return cxl_mem_scrub_get_attrbs(cxl_mbox, cap, cycle,
						flags, min_cycle);
	}

	struct rw_semaphore *region_lock __free(rwsem_read_release) =
		rwsem_read_intr_acquire(&cxl_region_rwsem);
	if (!region_lock)
		return -EINTR;

	cxlr = cxl_ps_ctx->cxlr;
	p = &cxlr->params;

	for (i = 0; i < p->nr_targets; i++) {
		struct cxl_endpoint_decoder *cxled = p->targets[i];

		cxlmd = cxled_to_memdev(cxled);
		cxl_mbox = &cxlmd->cxlds->cxl_mbox;
		ret = cxl_mem_scrub_get_attrbs(cxl_mbox, cap, cycle, flags,
					       min_cycle);
		if (ret)
			return ret;

		if (min_cycle)
			min_scrub_cycle = min(*min_cycle, min_scrub_cycle);
	}

	if (min_cycle)
		*min_cycle = min_scrub_cycle;

	return 0;
}

static int cxl_scrub_set_attrbs_region(struct device *dev,
				       struct cxl_patrol_scrub_context *cxl_ps_ctx,
				       u8 cycle, u8 flags)
{
	struct cxl_scrub_wr_attrbs wr_attrbs;
	struct cxl_mailbox *cxl_mbox;
	struct cxl_region_params *p;
	struct cxl_memdev *cxlmd;
	struct cxl_region *cxlr;
	int ret, i;

	struct rw_semaphore *region_lock __free(rwsem_read_release) =
		rwsem_read_intr_acquire(&cxl_region_rwsem);
	if (!region_lock)
		return -EINTR;

	cxlr = cxl_ps_ctx->cxlr;
	p = &cxlr->params;
	wr_attrbs.scrub_cycle_hours = cycle;
	wr_attrbs.scrub_flags = flags;

	for (i = 0; i < p->nr_targets; i++) {
		struct cxl_endpoint_decoder *cxled = p->targets[i];

		cxlmd = cxled_to_memdev(cxled);
		cxl_mbox = &cxlmd->cxlds->cxl_mbox;
		ret = cxl_set_feature(cxl_mbox, &CXL_FEAT_PATROL_SCRUB_UUID,
				      cxl_ps_ctx->set_version, &wr_attrbs,
				      sizeof(wr_attrbs),
				      CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET,
				      0, NULL);
		if (ret)
			return ret;

		if (cycle != cxlmd->scrub_cycle) {
			if (cxlmd->scrub_region_id != CXL_SCRUB_NO_REGION)
				dev_info(dev,
					 "Device scrub rate(%d hours) set by region%d rate overwritten by region%d scrub rate(%d hours)\n",
					 cxlmd->scrub_cycle,
					 cxlmd->scrub_region_id, cxlr->id,
					 cycle);

			cxlmd->scrub_cycle = cycle;
			cxlmd->scrub_region_id = cxlr->id;
		}
	}

	return 0;
}

static int cxl_scrub_set_attrbs_device(struct device *dev,
				       struct cxl_patrol_scrub_context *cxl_ps_ctx,
				       u8 cycle, u8 flags)
{
	struct cxl_scrub_wr_attrbs wr_attrbs;
	struct cxl_mailbox *cxl_mbox;
	struct cxl_memdev *cxlmd;
	int ret;

	wr_attrbs.scrub_cycle_hours = cycle;
	wr_attrbs.scrub_flags = flags;

	cxlmd = cxl_ps_ctx->cxlmd;
	cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	ret = cxl_set_feature(cxl_mbox, &CXL_FEAT_PATROL_SCRUB_UUID,
			      cxl_ps_ctx->set_version, &wr_attrbs,
			      sizeof(wr_attrbs),
			      CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET, 0,
			      NULL);
	if (ret)
		return ret;

	if (cycle != cxlmd->scrub_cycle) {
		if (cxlmd->scrub_region_id != CXL_SCRUB_NO_REGION)
			dev_info(dev,
				 "Device scrub rate(%d hours) set by region%d rate overwritten with device local scrub rate(%d hours)\n",
				 cxlmd->scrub_cycle, cxlmd->scrub_region_id,
				 cycle);

		cxlmd->scrub_cycle = cycle;
		cxlmd->scrub_region_id = CXL_SCRUB_NO_REGION;
	}

	return 0;
}

static int cxl_scrub_set_attrbs(struct device *dev,
				struct cxl_patrol_scrub_context *cxl_ps_ctx,
				u8 cycle, u8 flags)
{
	if (cxl_ps_ctx->cxlr)
		return cxl_scrub_set_attrbs_region(dev, cxl_ps_ctx, cycle, flags);

	return cxl_scrub_set_attrbs_device(dev, cxl_ps_ctx, cycle, flags);
}

static int cxl_patrol_scrub_get_enabled_bg(struct device *dev, void *drv_data,
					   bool *enabled)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	u8 cap, flags;
	u16 cycle;
	int ret;

	ret = cxl_scrub_get_attrbs(ctx, &cap, &cycle, &flags, NULL);
	if (ret)
		return ret;

	*enabled = CXL_GET_SCRUB_EN_STS(flags);

	return 0;
}

static int cxl_patrol_scrub_set_enabled_bg(struct device *dev, void *drv_data,
					   bool enable)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	u8 cap, flags, wr_cycle;
	u16 rd_cycle;
	int ret;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	ret = cxl_scrub_get_attrbs(ctx, &cap, &rd_cycle, &flags, NULL);
	if (ret)
		return ret;

	wr_cycle = CXL_GET_SCRUB_CYCLE(rd_cycle);
	flags = CXL_SET_SCRUB_EN(enable);

	return cxl_scrub_set_attrbs(dev, ctx, wr_cycle, flags);
}

static int cxl_patrol_scrub_get_min_scrub_cycle(struct device *dev,
						void *drv_data, u32 *min)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	u8 cap, flags, min_cycle;
	u16 cycle;
	int ret;

	ret = cxl_scrub_get_attrbs(ctx, &cap, &cycle, &flags, &min_cycle);
	if (ret)
		return ret;

	*min = min_cycle * 3600;

	return 0;
}

static int cxl_patrol_scrub_get_max_scrub_cycle(struct device *dev,
						void *drv_data, u32 *max)
{
	*max = U8_MAX * 3600; /* Max set by register size */

	return 0;
}

static int cxl_patrol_scrub_get_scrub_cycle(struct device *dev, void *drv_data,
					    u32 *scrub_cycle_secs)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	u8 cap, flags;
	u16 cycle;
	int ret;

	ret = cxl_scrub_get_attrbs(ctx, &cap, &cycle, &flags, NULL);
	if (ret)
		return ret;

	*scrub_cycle_secs = CXL_GET_SCRUB_CYCLE(cycle) * 3600;

	return 0;
}

static int cxl_patrol_scrub_set_scrub_cycle(struct device *dev, void *drv_data,
					    u32 scrub_cycle_secs)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	u8 scrub_cycle_hours = scrub_cycle_secs / 3600;
	u8 cap, wr_cycle, flags, min_cycle;
	u16 rd_cycle;
	int ret;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	ret = cxl_scrub_get_attrbs(ctx, &cap, &rd_cycle, &flags, &min_cycle);
	if (ret)
		return ret;

	if (!CXL_GET_SCRUB_CYCLE_CHANGEABLE(cap))
		return -EOPNOTSUPP;

	if (scrub_cycle_hours < min_cycle) {
		dev_dbg(dev, "Invalid CXL patrol scrub cycle(%d) to set\n",
			scrub_cycle_hours);
		dev_dbg(dev,
			"Minimum supported CXL patrol scrub cycle in hour %d\n",
			min_cycle);
		return -EINVAL;
	}
	wr_cycle = CXL_SET_SCRUB_CYCLE(scrub_cycle_hours);

	return cxl_scrub_set_attrbs(dev, ctx, wr_cycle, flags);
}

static const struct edac_scrub_ops cxl_ps_scrub_ops = {
	.get_enabled_bg = cxl_patrol_scrub_get_enabled_bg,
	.set_enabled_bg = cxl_patrol_scrub_set_enabled_bg,
	.get_min_cycle = cxl_patrol_scrub_get_min_scrub_cycle,
	.get_max_cycle = cxl_patrol_scrub_get_max_scrub_cycle,
	.get_cycle_duration = cxl_patrol_scrub_get_scrub_cycle,
	.set_cycle_duration = cxl_patrol_scrub_set_scrub_cycle,
};

static int cxl_memdev_scrub_init(struct cxl_memdev *cxlmd,
				 struct edac_dev_feature *ras_feature,
				 u8 scrub_inst)
{
	struct cxl_patrol_scrub_context *cxl_ps_ctx;
	struct cxl_feat_entry *feat_entry;
	u8 cap, flags;
	u16 cycle;
	int rc;

	feat_entry = cxl_feature_info(to_cxlfs(cxlmd->cxlds),
				      &CXL_FEAT_PATROL_SCRUB_UUID);
	if (IS_ERR(feat_entry))
		return -EOPNOTSUPP;

	if (!(le32_to_cpu(feat_entry->flags) & CXL_FEATURE_F_CHANGEABLE))
		return -EOPNOTSUPP;

	cxl_ps_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_ps_ctx), GFP_KERNEL);
	if (!cxl_ps_ctx)
		return -ENOMEM;

	*cxl_ps_ctx = (struct cxl_patrol_scrub_context){
		.get_feat_size = le16_to_cpu(feat_entry->get_feat_size),
		.set_feat_size = le16_to_cpu(feat_entry->set_feat_size),
		.get_version = feat_entry->get_feat_ver,
		.set_version = feat_entry->set_feat_ver,
		.effects = le16_to_cpu(feat_entry->effects),
		.instance = scrub_inst,
		.cxlmd = cxlmd,
	};

	rc = cxl_mem_scrub_get_attrbs(&cxlmd->cxlds->cxl_mbox, &cap, &cycle,
				      &flags, NULL);
	if (rc)
		return rc;

	cxlmd->scrub_cycle = CXL_GET_SCRUB_CYCLE(cycle);
	cxlmd->scrub_region_id = CXL_SCRUB_NO_REGION;

	ras_feature->ft_type = RAS_FEAT_SCRUB;
	ras_feature->instance = cxl_ps_ctx->instance;
	ras_feature->scrub_ops = &cxl_ps_scrub_ops;
	ras_feature->ctx = cxl_ps_ctx;

	return 0;
}

static int cxl_region_scrub_init(struct cxl_region *cxlr,
				 struct edac_dev_feature *ras_feature,
				 u8 scrub_inst)
{
	struct cxl_patrol_scrub_context *cxl_ps_ctx;
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_feat_entry *feat_entry = NULL;
	struct cxl_memdev *cxlmd;
	u8 cap, flags;
	u16 cycle;
	int i, rc;

	/*
	 * The cxl_region_rwsem must be held if the code below is used in a context
	 * other than when the region is in the probe state, as shown here.
	 */
	for (i = 0; i < p->nr_targets; i++) {
		struct cxl_endpoint_decoder *cxled = p->targets[i];

		cxlmd = cxled_to_memdev(cxled);
		feat_entry = cxl_feature_info(to_cxlfs(cxlmd->cxlds),
					      &CXL_FEAT_PATROL_SCRUB_UUID);
		if (IS_ERR(feat_entry))
			return -EOPNOTSUPP;

		if (!(le32_to_cpu(feat_entry->flags) &
		      CXL_FEATURE_F_CHANGEABLE))
			return -EOPNOTSUPP;

		rc = cxl_mem_scrub_get_attrbs(&cxlmd->cxlds->cxl_mbox, &cap,
					      &cycle, &flags, NULL);
		if (rc)
			return rc;

		cxlmd->scrub_cycle = CXL_GET_SCRUB_CYCLE(cycle);
		cxlmd->scrub_region_id = CXL_SCRUB_NO_REGION;
	}

	cxl_ps_ctx = devm_kzalloc(&cxlr->dev, sizeof(*cxl_ps_ctx), GFP_KERNEL);
	if (!cxl_ps_ctx)
		return -ENOMEM;

	*cxl_ps_ctx = (struct cxl_patrol_scrub_context){
		.get_feat_size = le16_to_cpu(feat_entry->get_feat_size),
		.set_feat_size = le16_to_cpu(feat_entry->set_feat_size),
		.get_version = feat_entry->get_feat_ver,
		.set_version = feat_entry->set_feat_ver,
		.effects = le16_to_cpu(feat_entry->effects),
		.instance = scrub_inst,
		.cxlr = cxlr,
	};

	ras_feature->ft_type = RAS_FEAT_SCRUB;
	ras_feature->instance = cxl_ps_ctx->instance;
	ras_feature->scrub_ops = &cxl_ps_scrub_ops;
	ras_feature->ctx = cxl_ps_ctx;

	return 0;
}

struct cxl_ecs_context {
	u16 num_media_frus;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 effects;
	struct cxl_memdev *cxlmd;
};

/*
 * See CXL spec rev 3.2 @8.2.10.9.11.2 Table 8-225 DDR5 ECS Control Feature
 * Readable Attributes.
 */
struct cxl_ecs_fru_rd_attrbs {
	u8 ecs_cap;
	__le16 ecs_config;
	u8 ecs_flags;
} __packed;

struct cxl_ecs_rd_attrbs {
	u8 ecs_log_cap;
	struct cxl_ecs_fru_rd_attrbs fru_attrbs[];
} __packed;

/*
 * See CXL spec rev 3.2 @8.2.10.9.11.2 Table 8-226 DDR5 ECS Control Feature
 * Writable Attributes.
 */
struct cxl_ecs_fru_wr_attrbs {
	__le16 ecs_config;
} __packed;

struct cxl_ecs_wr_attrbs {
	u8 ecs_log_cap;
	struct cxl_ecs_fru_wr_attrbs fru_attrbs[];
} __packed;

#define CXL_ECS_LOG_ENTRY_TYPE_MASK GENMASK(1, 0)
#define CXL_ECS_REALTIME_REPORT_CAP_MASK BIT(0)
#define CXL_ECS_THRESHOLD_COUNT_MASK GENMASK(2, 0)
#define CXL_ECS_COUNT_MODE_MASK BIT(3)
#define CXL_ECS_RESET_COUNTER_MASK BIT(4)
#define CXL_ECS_RESET_COUNTER 1

enum {
	ECS_THRESHOLD_256 = 256,
	ECS_THRESHOLD_1024 = 1024,
	ECS_THRESHOLD_4096 = 4096,
};

enum {
	ECS_THRESHOLD_IDX_256 = 3,
	ECS_THRESHOLD_IDX_1024 = 4,
	ECS_THRESHOLD_IDX_4096 = 5,
};

static const u16 ecs_supp_threshold[] = {
	[ECS_THRESHOLD_IDX_256] = 256,
	[ECS_THRESHOLD_IDX_1024] = 1024,
	[ECS_THRESHOLD_IDX_4096] = 4096,
};

enum {
	ECS_LOG_ENTRY_TYPE_DRAM = 0x0,
	ECS_LOG_ENTRY_TYPE_MEM_MEDIA_FRU = 0x1,
};

enum cxl_ecs_count_mode {
	ECS_MODE_COUNTS_ROWS = 0,
	ECS_MODE_COUNTS_CODEWORDS = 1,
};

static int cxl_mem_ecs_get_attrbs(struct device *dev,
				  struct cxl_ecs_context *cxl_ecs_ctx,
				  int fru_id, u8 *log_cap, u16 *config)
{
	struct cxl_memdev *cxlmd = cxl_ecs_ctx->cxlmd;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_ecs_fru_rd_attrbs *fru_rd_attrbs;
	size_t rd_data_size;
	size_t data_size;

	rd_data_size = cxl_ecs_ctx->get_feat_size;

	struct cxl_ecs_rd_attrbs *rd_attrbs __free(kvfree) =
		kvzalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrbs)
		return -ENOMEM;

	data_size = cxl_get_feature(cxl_mbox, &CXL_FEAT_ECS_UUID,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE, rd_attrbs,
				    rd_data_size, 0, NULL);
	if (!data_size)
		return -EIO;

	fru_rd_attrbs = rd_attrbs->fru_attrbs;
	*log_cap = rd_attrbs->ecs_log_cap;
	*config = le16_to_cpu(fru_rd_attrbs[fru_id].ecs_config);

	return 0;
}

static int cxl_mem_ecs_set_attrbs(struct device *dev,
				  struct cxl_ecs_context *cxl_ecs_ctx,
				  int fru_id, u8 log_cap, u16 config)
{
	struct cxl_memdev *cxlmd = cxl_ecs_ctx->cxlmd;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_ecs_fru_rd_attrbs *fru_rd_attrbs;
	struct cxl_ecs_fru_wr_attrbs *fru_wr_attrbs;
	size_t rd_data_size, wr_data_size;
	u16 num_media_frus, count;
	size_t data_size;

	num_media_frus = cxl_ecs_ctx->num_media_frus;
	rd_data_size = cxl_ecs_ctx->get_feat_size;
	wr_data_size = cxl_ecs_ctx->set_feat_size;
	struct cxl_ecs_rd_attrbs *rd_attrbs __free(kvfree) =
		kvzalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrbs)
		return -ENOMEM;

	data_size = cxl_get_feature(cxl_mbox, &CXL_FEAT_ECS_UUID,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE, rd_attrbs,
				    rd_data_size, 0, NULL);
	if (!data_size)
		return -EIO;

	struct cxl_ecs_wr_attrbs *wr_attrbs __free(kvfree) =
		kvzalloc(wr_data_size, GFP_KERNEL);
	if (!wr_attrbs)
		return -ENOMEM;

	/*
	 * Fill writable attributes from the current attributes read
	 * for all the media FRUs.
	 */
	fru_rd_attrbs = rd_attrbs->fru_attrbs;
	fru_wr_attrbs = wr_attrbs->fru_attrbs;
	wr_attrbs->ecs_log_cap = log_cap;
	for (count = 0; count < num_media_frus; count++)
		fru_wr_attrbs[count].ecs_config =
			fru_rd_attrbs[count].ecs_config;

	fru_wr_attrbs[fru_id].ecs_config = cpu_to_le16(config);

	return cxl_set_feature(cxl_mbox, &CXL_FEAT_ECS_UUID,
			       cxl_ecs_ctx->set_version, wr_attrbs,
			       wr_data_size,
			       CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET,
			       0, NULL);
}

static u8 cxl_get_ecs_log_entry_type(u8 log_cap, u16 config)
{
	return FIELD_GET(CXL_ECS_LOG_ENTRY_TYPE_MASK, log_cap);
}

static u16 cxl_get_ecs_threshold(u8 log_cap, u16 config)
{
	u8 index = FIELD_GET(CXL_ECS_THRESHOLD_COUNT_MASK, config);

	return ecs_supp_threshold[index];
}

static u8 cxl_get_ecs_count_mode(u8 log_cap, u16 config)
{
	return FIELD_GET(CXL_ECS_COUNT_MODE_MASK, config);
}

#define CXL_ECS_GET_ATTR(attrb)						    \
	static int cxl_ecs_get_##attrb(struct device *dev, void *drv_data,  \
				       int fru_id, u32 *val)		    \
	{								    \
		struct cxl_ecs_context *ctx = drv_data;			    \
		u8 log_cap;						    \
		u16 config;						    \
		int ret;						    \
									    \
		ret = cxl_mem_ecs_get_attrbs(dev, ctx, fru_id, &log_cap,    \
					     &config);			    \
		if (ret)						    \
			return ret;					    \
									    \
		*val = cxl_get_ecs_##attrb(log_cap, config);		    \
									    \
		return 0;						    \
	}

CXL_ECS_GET_ATTR(log_entry_type)
CXL_ECS_GET_ATTR(count_mode)
CXL_ECS_GET_ATTR(threshold)

static int cxl_set_ecs_log_entry_type(struct device *dev, u8 *log_cap,
				      u16 *config, u32 val)
{
	if (val != ECS_LOG_ENTRY_TYPE_DRAM &&
	    val != ECS_LOG_ENTRY_TYPE_MEM_MEDIA_FRU)
		return -EINVAL;

	*log_cap = FIELD_PREP(CXL_ECS_LOG_ENTRY_TYPE_MASK, val);

	return 0;
}

static int cxl_set_ecs_threshold(struct device *dev, u8 *log_cap, u16 *config,
				 u32 val)
{
	*config &= ~CXL_ECS_THRESHOLD_COUNT_MASK;

	switch (val) {
	case ECS_THRESHOLD_256:
		*config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
				      ECS_THRESHOLD_IDX_256);
		break;
	case ECS_THRESHOLD_1024:
		*config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
				      ECS_THRESHOLD_IDX_1024);
		break;
	case ECS_THRESHOLD_4096:
		*config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
				      ECS_THRESHOLD_IDX_4096);
		break;
	default:
		dev_dbg(dev, "Invalid CXL ECS threshold count(%d) to set\n",
			val);
		dev_dbg(dev, "Supported ECS threshold counts: %u, %u, %u\n",
			ECS_THRESHOLD_256, ECS_THRESHOLD_1024,
			ECS_THRESHOLD_4096);
		return -EINVAL;
	}

	return 0;
}

static int cxl_set_ecs_count_mode(struct device *dev, u8 *log_cap, u16 *config,
				  u32 val)
{
	if (val != ECS_MODE_COUNTS_ROWS && val != ECS_MODE_COUNTS_CODEWORDS) {
		dev_dbg(dev, "Invalid CXL ECS scrub mode(%d) to set\n", val);
		dev_dbg(dev,
			"Supported ECS Modes: 0: ECS counts rows with errors,"
			" 1: ECS counts codewords with errors\n");
		return -EINVAL;
	}

	*config &= ~CXL_ECS_COUNT_MODE_MASK;
	*config |= FIELD_PREP(CXL_ECS_COUNT_MODE_MASK, val);

	return 0;
}

static int cxl_set_ecs_reset_counter(struct device *dev, u8 *log_cap,
				     u16 *config, u32 val)
{
	if (val != CXL_ECS_RESET_COUNTER)
		return -EINVAL;

	*config &= ~CXL_ECS_RESET_COUNTER_MASK;
	*config |= FIELD_PREP(CXL_ECS_RESET_COUNTER_MASK, val);

	return 0;
}

#define CXL_ECS_SET_ATTR(attrb)						    \
	static int cxl_ecs_set_##attrb(struct device *dev, void *drv_data,  \
					int fru_id, u32 val)		    \
	{								    \
		struct cxl_ecs_context *ctx = drv_data;			    \
		u8 log_cap;						    \
		u16 config;						    \
		int ret;						    \
									    \
		if (!capable(CAP_SYS_RAWIO))				    \
			return -EPERM;					    \
									    \
		ret = cxl_mem_ecs_get_attrbs(dev, ctx, fru_id, &log_cap,    \
					     &config);			    \
		if (ret)						    \
			return ret;					    \
									    \
		ret = cxl_set_ecs_##attrb(dev, &log_cap, &config, val);     \
		if (ret)						    \
			return ret;					    \
									    \
		return cxl_mem_ecs_set_attrbs(dev, ctx, fru_id, log_cap,    \
					      config);			    \
	}
CXL_ECS_SET_ATTR(log_entry_type)
CXL_ECS_SET_ATTR(count_mode)
CXL_ECS_SET_ATTR(reset_counter)
CXL_ECS_SET_ATTR(threshold)

static const struct edac_ecs_ops cxl_ecs_ops = {
	.get_log_entry_type = cxl_ecs_get_log_entry_type,
	.set_log_entry_type = cxl_ecs_set_log_entry_type,
	.get_mode = cxl_ecs_get_count_mode,
	.set_mode = cxl_ecs_set_count_mode,
	.reset = cxl_ecs_set_reset_counter,
	.get_threshold = cxl_ecs_get_threshold,
	.set_threshold = cxl_ecs_set_threshold,
};

static int cxl_memdev_ecs_init(struct cxl_memdev *cxlmd,
			       struct edac_dev_feature *ras_feature)
{
	struct cxl_ecs_context *cxl_ecs_ctx;
	struct cxl_feat_entry *feat_entry;
	int num_media_frus;

	feat_entry =
		cxl_feature_info(to_cxlfs(cxlmd->cxlds), &CXL_FEAT_ECS_UUID);
	if (IS_ERR(feat_entry))
		return -EOPNOTSUPP;

	if (!(le32_to_cpu(feat_entry->flags) & CXL_FEATURE_F_CHANGEABLE))
		return -EOPNOTSUPP;

	num_media_frus = (le16_to_cpu(feat_entry->get_feat_size) -
			  sizeof(struct cxl_ecs_rd_attrbs)) /
			 sizeof(struct cxl_ecs_fru_rd_attrbs);
	if (!num_media_frus)
		return -EOPNOTSUPP;

	cxl_ecs_ctx =
		devm_kzalloc(&cxlmd->dev, sizeof(*cxl_ecs_ctx), GFP_KERNEL);
	if (!cxl_ecs_ctx)
		return -ENOMEM;

	*cxl_ecs_ctx = (struct cxl_ecs_context){
		.get_feat_size = le16_to_cpu(feat_entry->get_feat_size),
		.set_feat_size = le16_to_cpu(feat_entry->set_feat_size),
		.get_version = feat_entry->get_feat_ver,
		.set_version = feat_entry->set_feat_ver,
		.effects = le16_to_cpu(feat_entry->effects),
		.num_media_frus = num_media_frus,
		.cxlmd = cxlmd,
	};

	ras_feature->ft_type = RAS_FEAT_ECS;
	ras_feature->ecs_ops = &cxl_ecs_ops;
	ras_feature->ctx = cxl_ecs_ctx;
	ras_feature->ecs_info.num_media_frus = num_media_frus;

	return 0;
}

/*
 * Perform Maintenance CXL 3.2 Spec 8.2.10.7.1
 */

/*
 * Perform Maintenance input payload
 * CXL rev 3.2 section 8.2.10.7.1 Table 8-117
 */
struct cxl_mbox_maintenance_hdr {
	u8 op_class;
	u8 op_subclass;
} __packed;

static int cxl_perform_maintenance(struct cxl_mailbox *cxl_mbox, u8 class,
				   u8 subclass, void *data_in,
				   size_t data_in_size)
{
	struct cxl_memdev_maintenance_pi {
		struct cxl_mbox_maintenance_hdr hdr;
		u8 data[];
	} __packed;
	struct cxl_mbox_cmd mbox_cmd;
	size_t hdr_size;

	struct cxl_memdev_maintenance_pi *pi __free(kvfree) =
		kvzalloc(cxl_mbox->payload_size, GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	pi->hdr.op_class = class;
	pi->hdr.op_subclass = subclass;
	hdr_size = sizeof(pi->hdr);
	/*
	 * Check minimum mbox payload size is available for
	 * the maintenance data transfer.
	 */
	if (hdr_size + data_in_size > cxl_mbox->payload_size)
		return -ENOMEM;

	memcpy(pi->data, data_in, data_in_size);
	mbox_cmd = (struct cxl_mbox_cmd){
		.opcode = CXL_MBOX_OP_DO_MAINTENANCE,
		.size_in = hdr_size + data_in_size,
		.payload_in = pi,
	};

	return cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
}

int devm_cxl_memdev_edac_register(struct cxl_memdev *cxlmd)
{
	struct edac_dev_feature ras_features[CXL_NR_EDAC_DEV_FEATURES];
	int num_ras_features = 0;
	int rc;

	if (IS_ENABLED(CONFIG_CXL_EDAC_SCRUB)) {
		rc = cxl_memdev_scrub_init(cxlmd, &ras_features[num_ras_features], 0);
		if (rc < 0 && rc != -EOPNOTSUPP)
			return rc;

		if (rc != -EOPNOTSUPP)
			num_ras_features++;
	}

	if (IS_ENABLED(CONFIG_CXL_EDAC_ECS)) {
		rc = cxl_memdev_ecs_init(cxlmd, &ras_features[num_ras_features]);
		if (rc < 0 && rc != -EOPNOTSUPP)
			return rc;

		if (rc != -EOPNOTSUPP)
			num_ras_features++;
	}

	if (!num_ras_features)
		return -EINVAL;

	char *cxl_dev_name __free(kfree) =
		kasprintf(GFP_KERNEL, "cxl_%s", dev_name(&cxlmd->dev));
	if (!cxl_dev_name)
		return -ENOMEM;

	return edac_dev_register(&cxlmd->dev, cxl_dev_name, NULL,
				 num_ras_features, ras_features);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_memdev_edac_register, "CXL");

int devm_cxl_region_edac_register(struct cxl_region *cxlr)
{
	struct edac_dev_feature ras_features[CXL_NR_EDAC_DEV_FEATURES];
	int num_ras_features = 0;
	int rc;

	if (!IS_ENABLED(CONFIG_CXL_EDAC_SCRUB))
		return 0;

	rc = cxl_region_scrub_init(cxlr, &ras_features[num_ras_features], 0);
	if (rc < 0)
		return rc;

	num_ras_features++;

	char *cxl_dev_name __free(kfree) =
		kasprintf(GFP_KERNEL, "cxl_%s", dev_name(&cxlr->dev));
	if (!cxl_dev_name)
		return -ENOMEM;

	return edac_dev_register(&cxlr->dev, cxl_dev_name, NULL,
				 num_ras_features, ras_features);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_region_edac_register, "CXL");
