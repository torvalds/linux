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

#define CXL_NR_EDAC_DEV_FEATURES 1

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
