// SPDX-License-Identifier: GPL-2.0
/*
 * Ampere SoC PMU (Performance Monitor Unit)
 *
 * Copyright (c) 2023, Ampere Computing LLC
 */
#include <linux/io.h>
#include <linux/module.h>
#include <linux/topology.h>

#include "arm_cspmu.h"

#define PMAUXR0		PMIMPDEF
#define PMAUXR1		(PMIMPDEF + 0x4)
#define PMAUXR2		(PMIMPDEF + 0x8)
#define PMAUXR3		(PMIMPDEF + 0xC)

#define to_ampere_cspmu_ctx(cspmu)	((struct ampere_cspmu_ctx *)(cspmu->impl.ctx))

struct ampere_cspmu_ctx {
	const char *name;
	struct attribute **event_attr;
	struct attribute **format_attr;
};

static DEFINE_IDA(mcu_pmu_ida);

#define SOC_PMU_EVENT_ATTR_EXTRACTOR(_name, _config, _start, _end)        \
	static inline u32 get_##_name(const struct perf_event *event)     \
	{                                                                 \
		return FIELD_GET(GENMASK_ULL(_end, _start),               \
				 event->attr._config);                    \
	}                                                                 \

SOC_PMU_EVENT_ATTR_EXTRACTOR(event, config, 0, 8);
SOC_PMU_EVENT_ATTR_EXTRACTOR(threshold, config1, 0, 7);
SOC_PMU_EVENT_ATTR_EXTRACTOR(rank, config1, 8, 23);
SOC_PMU_EVENT_ATTR_EXTRACTOR(bank, config1, 24, 55);

static struct attribute *ampereone_mcu_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(cycle_count,		0x00),
	ARM_CSPMU_EVENT_ATTR(act_sent,			0x01),
	ARM_CSPMU_EVENT_ATTR(pre_sent,			0x02),
	ARM_CSPMU_EVENT_ATTR(rd_sent,			0x03),
	ARM_CSPMU_EVENT_ATTR(rda_sent,			0x04),
	ARM_CSPMU_EVENT_ATTR(wr_sent,			0x05),
	ARM_CSPMU_EVENT_ATTR(wra_sent,			0x06),
	ARM_CSPMU_EVENT_ATTR(pd_entry_vld,		0x07),
	ARM_CSPMU_EVENT_ATTR(sref_entry_vld,		0x08),
	ARM_CSPMU_EVENT_ATTR(prea_sent,			0x09),
	ARM_CSPMU_EVENT_ATTR(pre_sb_sent,		0x0a),
	ARM_CSPMU_EVENT_ATTR(ref_sent,			0x0b),
	ARM_CSPMU_EVENT_ATTR(rfm_sent,			0x0c),
	ARM_CSPMU_EVENT_ATTR(ref_sb_sent,		0x0d),
	ARM_CSPMU_EVENT_ATTR(rfm_sb_sent,		0x0e),
	ARM_CSPMU_EVENT_ATTR(rd_rda_sent,		0x0f),
	ARM_CSPMU_EVENT_ATTR(wr_wra_sent,		0x10),
	ARM_CSPMU_EVENT_ATTR(raw_hazard,		0x11),
	ARM_CSPMU_EVENT_ATTR(war_hazard,		0x12),
	ARM_CSPMU_EVENT_ATTR(waw_hazard,		0x13),
	ARM_CSPMU_EVENT_ATTR(rar_hazard,		0x14),
	ARM_CSPMU_EVENT_ATTR(raw_war_waw_hazard,	0x15),
	ARM_CSPMU_EVENT_ATTR(hprd_lprd_wr_req_vld,	0x16),
	ARM_CSPMU_EVENT_ATTR(lprd_req_vld,		0x17),
	ARM_CSPMU_EVENT_ATTR(hprd_req_vld,		0x18),
	ARM_CSPMU_EVENT_ATTR(hprd_lprd_req_vld,		0x19),
	ARM_CSPMU_EVENT_ATTR(prefetch_tgt,		0x1a),
	ARM_CSPMU_EVENT_ATTR(wr_req_vld,		0x1b),
	ARM_CSPMU_EVENT_ATTR(partial_wr_req_vld,	0x1c),
	ARM_CSPMU_EVENT_ATTR(rd_retry,			0x1d),
	ARM_CSPMU_EVENT_ATTR(wr_retry,			0x1e),
	ARM_CSPMU_EVENT_ATTR(retry_gnt,			0x1f),
	ARM_CSPMU_EVENT_ATTR(rank_change,		0x20),
	ARM_CSPMU_EVENT_ATTR(dir_change,		0x21),
	ARM_CSPMU_EVENT_ATTR(rank_dir_change,		0x22),
	ARM_CSPMU_EVENT_ATTR(rank_active,		0x23),
	ARM_CSPMU_EVENT_ATTR(rank_idle,			0x24),
	ARM_CSPMU_EVENT_ATTR(rank_pd,			0x25),
	ARM_CSPMU_EVENT_ATTR(rank_sref,			0x26),
	ARM_CSPMU_EVENT_ATTR(queue_fill_gt_thresh,	0x27),
	ARM_CSPMU_EVENT_ATTR(queue_rds_gt_thresh,	0x28),
	ARM_CSPMU_EVENT_ATTR(queue_wrs_gt_thresh,	0x29),
	ARM_CSPMU_EVENT_ATTR(phy_updt_complt,		0x2a),
	ARM_CSPMU_EVENT_ATTR(tz_fail,			0x2b),
	ARM_CSPMU_EVENT_ATTR(dram_errc,			0x2c),
	ARM_CSPMU_EVENT_ATTR(dram_errd,			0x2d),
	ARM_CSPMU_EVENT_ATTR(read_data_return,		0x32),
	ARM_CSPMU_EVENT_ATTR(chi_wr_data_delta,		0x33),
	ARM_CSPMU_EVENT_ATTR(zq_start,			0x34),
	ARM_CSPMU_EVENT_ATTR(zq_latch,			0x35),
	ARM_CSPMU_EVENT_ATTR(wr_fifo_full,		0x36),
	ARM_CSPMU_EVENT_ATTR(info_fifo_full,		0x37),
	ARM_CSPMU_EVENT_ATTR(cmd_fifo_full,		0x38),
	ARM_CSPMU_EVENT_ATTR(dfi_nop,			0x39),
	ARM_CSPMU_EVENT_ATTR(dfi_cmd,			0x3a),
	ARM_CSPMU_EVENT_ATTR(rd_run_len,		0x3b),
	ARM_CSPMU_EVENT_ATTR(wr_run_len,		0x3c),

	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL,
};

static struct attribute *ampereone_mcu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(threshold, "config1:0-7"),
	ARM_CSPMU_FORMAT_ATTR(rank, "config1:8-23"),
	ARM_CSPMU_FORMAT_ATTR(bank, "config1:24-55"),
	NULL,
};

static struct attribute **
ampere_cspmu_get_event_attrs(const struct arm_cspmu *cspmu)
{
	const struct ampere_cspmu_ctx *ctx = to_ampere_cspmu_ctx(cspmu);

	return ctx->event_attr;
}

static struct attribute **
ampere_cspmu_get_format_attrs(const struct arm_cspmu *cspmu)
{
	const struct ampere_cspmu_ctx *ctx = to_ampere_cspmu_ctx(cspmu);

	return ctx->format_attr;
}

static const char *
ampere_cspmu_get_name(const struct arm_cspmu *cspmu)
{
	const struct ampere_cspmu_ctx *ctx = to_ampere_cspmu_ctx(cspmu);

	return ctx->name;
}

static void ampere_cspmu_set_cc_filter(struct arm_cspmu *cspmu,
				       const struct perf_event *event)
{
	/*
	 * PMCCFILTR is RES0, so this is just a dummy callback to override
	 * the default implementation and avoid writing to it.
	 */
}

static void ampere_cspmu_set_ev_filter(struct arm_cspmu *cspmu,
				       const struct perf_event *event)
{
	u32 threshold, rank, bank;

	threshold	= get_threshold(event);
	rank		= get_rank(event);
	bank		= get_bank(event);

	writel(threshold, cspmu->base0 + PMAUXR0);
	writel(rank, cspmu->base0 + PMAUXR1);
	writel(bank, cspmu->base0 + PMAUXR2);
}

static int ampere_cspmu_validate_configs(struct perf_event *event,
					 struct perf_event *event2)
{
	if (get_threshold(event) != get_threshold(event2) ||
	    get_rank(event) != get_rank(event2) ||
	    get_bank(event) != get_bank(event2))
		return -EINVAL;

	return 0;
}

static int ampere_cspmu_validate_event(struct arm_cspmu *cspmu,
				       struct perf_event *new)
{
	struct perf_event *curr, *leader = new->group_leader;
	unsigned int idx;
	int ret;

	ret = ampere_cspmu_validate_configs(new, leader);
	if (ret)
		return ret;

	/* We compare the global filter settings to the existing events */
	idx = find_first_bit(cspmu->hw_events.used_ctrs,
			     cspmu->cycle_counter_logical_idx);

	/* This is the first event, thus any configuration is fine */
	if (idx == cspmu->cycle_counter_logical_idx)
		return 0;

	curr = cspmu->hw_events.events[idx];

	return ampere_cspmu_validate_configs(curr, new);
}

static char *ampere_cspmu_format_name(const struct arm_cspmu *cspmu,
				      const char *name_pattern)
{
	struct device *dev = cspmu->dev;
	int id;

	id = ida_alloc(&mcu_pmu_ida, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	return devm_kasprintf(dev, GFP_KERNEL, name_pattern, id);
}

static int ampere_cspmu_init_ops(struct arm_cspmu *cspmu)
{
	struct device *dev = cspmu->dev;
	struct ampere_cspmu_ctx *ctx;
	struct arm_cspmu_impl_ops *impl_ops = &cspmu->impl.ops;

	ctx = devm_kzalloc(dev, sizeof(struct ampere_cspmu_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->event_attr	= ampereone_mcu_pmu_event_attrs;
	ctx->format_attr = ampereone_mcu_format_attrs;
	ctx->name = ampere_cspmu_format_name(cspmu, "ampere_mcu_pmu_%d");
	if (IS_ERR_OR_NULL(ctx->name))
		return ctx->name ? PTR_ERR(ctx->name) : -ENOMEM;

	cspmu->impl.ctx = ctx;

	impl_ops->set_cc_filter		= ampere_cspmu_set_cc_filter;
	impl_ops->set_ev_filter		= ampere_cspmu_set_ev_filter;
	impl_ops->validate_event	= ampere_cspmu_validate_event;
	impl_ops->get_name		= ampere_cspmu_get_name;
	impl_ops->get_event_attrs	= ampere_cspmu_get_event_attrs;
	impl_ops->get_format_attrs	= ampere_cspmu_get_format_attrs;

	return 0;
}

/* Match all Ampere Coresight PMU devices */
static const struct arm_cspmu_impl_match ampere_cspmu_param = {
	.pmiidr_val	= ARM_CSPMU_IMPL_ID_AMPERE,
	.module		= THIS_MODULE,
	.impl_init_ops	= ampere_cspmu_init_ops
};

static int __init ampere_cspmu_init(void)
{
	int ret;

	ret = arm_cspmu_impl_register(&ampere_cspmu_param);
	if (ret)
		pr_err("ampere_cspmu backend registration error: %d\n", ret);

	return ret;
}

static void __exit ampere_cspmu_exit(void)
{
	arm_cspmu_impl_unregister(&ampere_cspmu_param);
}

module_init(ampere_cspmu_init);
module_exit(ampere_cspmu_exit);

MODULE_DESCRIPTION("Ampere SoC Performance Monitor Driver");
MODULE_LICENSE("GPL");
