// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

/* Support for NVIDIA specific attributes. */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/topology.h>

#include "arm_cspmu.h"

#define NV_PCIE_PORT_COUNT           10ULL
#define NV_PCIE_FILTER_ID_MASK       GENMASK_ULL(NV_PCIE_PORT_COUNT - 1, 0)

#define NV_NVL_C2C_PORT_COUNT        2ULL
#define NV_NVL_C2C_FILTER_ID_MASK    GENMASK_ULL(NV_NVL_C2C_PORT_COUNT - 1, 0)

#define NV_CNVL_PORT_COUNT           4ULL
#define NV_CNVL_FILTER_ID_MASK       GENMASK_ULL(NV_CNVL_PORT_COUNT - 1, 0)

#define NV_UCF_SRC_COUNT             3ULL
#define NV_UCF_DST_COUNT             4ULL
#define NV_UCF_FILTER_ID_MASK        GENMASK_ULL(11, 0)
#define NV_UCF_FILTER_SRC            GENMASK_ULL(2, 0)
#define NV_UCF_FILTER_DST            GENMASK_ULL(11, 8)
#define NV_UCF_FILTER_DEFAULT        (NV_UCF_FILTER_SRC | NV_UCF_FILTER_DST)

#define NV_PCIE_V2_PORT_COUNT        8ULL
#define NV_PCIE_V2_FILTER_ID_MASK    GENMASK_ULL(24, 0)
#define NV_PCIE_V2_FILTER_PORT       GENMASK_ULL(NV_PCIE_V2_PORT_COUNT - 1, 0)
#define NV_PCIE_V2_FILTER_BDF_VAL    GENMASK_ULL(23, NV_PCIE_V2_PORT_COUNT)
#define NV_PCIE_V2_FILTER_BDF_EN     BIT(24)
#define NV_PCIE_V2_FILTER_BDF_VAL_EN GENMASK_ULL(24, NV_PCIE_V2_PORT_COUNT)
#define NV_PCIE_V2_FILTER_DEFAULT    NV_PCIE_V2_FILTER_PORT

#define NV_PCIE_V2_DST_COUNT         5ULL
#define NV_PCIE_V2_FILTER2_ID_MASK   GENMASK_ULL(4, 0)
#define NV_PCIE_V2_FILTER2_DST       GENMASK_ULL(NV_PCIE_V2_DST_COUNT - 1, 0)
#define NV_PCIE_V2_FILTER2_DEFAULT   NV_PCIE_V2_FILTER2_DST

#define NV_PCIE_TGT_PORT_COUNT       8ULL
#define NV_PCIE_TGT_EV_TYPE_CC       0x4
#define NV_PCIE_TGT_EV_TYPE_COUNT    3ULL
#define NV_PCIE_TGT_EV_TYPE_MASK     GENMASK_ULL(NV_PCIE_TGT_EV_TYPE_COUNT - 1, 0)
#define NV_PCIE_TGT_FILTER2_MASK     GENMASK_ULL(NV_PCIE_TGT_PORT_COUNT, 0)
#define NV_PCIE_TGT_FILTER2_PORT     GENMASK_ULL(NV_PCIE_TGT_PORT_COUNT - 1, 0)
#define NV_PCIE_TGT_FILTER2_ADDR_EN  BIT(NV_PCIE_TGT_PORT_COUNT)
#define NV_PCIE_TGT_FILTER2_ADDR     GENMASK_ULL(15, NV_PCIE_TGT_PORT_COUNT)
#define NV_PCIE_TGT_FILTER2_DEFAULT  NV_PCIE_TGT_FILTER2_PORT

#define NV_PCIE_TGT_ADDR_COUNT       8ULL
#define NV_PCIE_TGT_ADDR_STRIDE      20
#define NV_PCIE_TGT_ADDR_CTRL        0xD38
#define NV_PCIE_TGT_ADDR_BASE_LO     0xD3C
#define NV_PCIE_TGT_ADDR_BASE_HI     0xD40
#define NV_PCIE_TGT_ADDR_MASK_LO     0xD44
#define NV_PCIE_TGT_ADDR_MASK_HI     0xD48

#define NV_GENERIC_FILTER_ID_MASK    GENMASK_ULL(31, 0)

#define NV_PRODID_MASK	(PMIIDR_PRODUCTID | PMIIDR_VARIANT | PMIIDR_REVISION)

#define NV_FORMAT_NAME_GENERIC	0

#define to_nv_cspmu_ctx(cspmu)	((struct nv_cspmu_ctx *)(cspmu->impl.ctx))

#define NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _num, _suff, _config)	\
	ARM_CSPMU_EVENT_ATTR(_pref##_num##_suff, _config)

#define NV_CSPMU_EVENT_ATTR_4(_pref, _suff, _config)			\
	NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _0_, _suff, _config),	\
	NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _1_, _suff, _config + 1),	\
	NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _2_, _suff, _config + 2),	\
	NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _3_, _suff, _config + 3)

struct nv_cspmu_ctx {
	const char *name;

	struct attribute **event_attr;
	struct attribute **format_attr;

	u32 filter_mask;
	u32 filter_default_val;
	u32 filter2_mask;
	u32 filter2_default_val;

	u32 (*get_filter)(const struct perf_event *event);
	u32 (*get_filter2)(const struct perf_event *event);

	void *data;

	int (*init_data)(struct arm_cspmu *cspmu);
};

static struct attribute *scf_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(bus_cycles,			0x1d),

	ARM_CSPMU_EVENT_ATTR(scf_cache_allocate,		0xF0),
	ARM_CSPMU_EVENT_ATTR(scf_cache_refill,			0xF1),
	ARM_CSPMU_EVENT_ATTR(scf_cache,				0xF2),
	ARM_CSPMU_EVENT_ATTR(scf_cache_wb,			0xF3),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_data,			0x101),
	NV_CSPMU_EVENT_ATTR_4(socket, wb_data,			0x109),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_outstanding,		0x115),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_access,		0x12d),
	NV_CSPMU_EVENT_ATTR_4(socket, wb_access,		0x135),
	NV_CSPMU_EVENT_ATTR_4(socket, wr_access,		0x139),

	ARM_CSPMU_EVENT_ATTR(gmem_rd_data,			0x16d),
	ARM_CSPMU_EVENT_ATTR(gmem_rd_access,			0x16e),
	ARM_CSPMU_EVENT_ATTR(gmem_rd_outstanding,		0x16f),
	ARM_CSPMU_EVENT_ATTR(gmem_wb_data,			0x173),
	ARM_CSPMU_EVENT_ATTR(gmem_wb_access,			0x174),
	ARM_CSPMU_EVENT_ATTR(gmem_wr_data,			0x179),
	ARM_CSPMU_EVENT_ATTR(gmem_wr_access,			0x17b),

	NV_CSPMU_EVENT_ATTR_4(socket, wr_data,			0x17c),

	ARM_CSPMU_EVENT_ATTR(gmem_wr_total_bytes,		0x1a0),
	ARM_CSPMU_EVENT_ATTR(remote_socket_wr_total_bytes,	0x1a1),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_data,		0x1a2),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_outstanding,	0x1a3),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_access,		0x1a4),

	ARM_CSPMU_EVENT_ATTR(cmem_rd_data,			0x1a5),
	ARM_CSPMU_EVENT_ATTR(cmem_rd_access,			0x1a6),
	ARM_CSPMU_EVENT_ATTR(cmem_rd_outstanding,		0x1a7),
	ARM_CSPMU_EVENT_ATTR(cmem_wb_data,			0x1ab),
	ARM_CSPMU_EVENT_ATTR(cmem_wb_access,			0x1ac),
	ARM_CSPMU_EVENT_ATTR(cmem_wr_data,			0x1b1),

	ARM_CSPMU_EVENT_ATTR(cmem_wr_access,			0x1ca),

	ARM_CSPMU_EVENT_ATTR(cmem_wr_total_bytes,		0x1db),

	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL,
};

static struct attribute *mcf_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(rd_bytes_loc,			0x0),
	ARM_CSPMU_EVENT_ATTR(rd_bytes_rem,			0x1),
	ARM_CSPMU_EVENT_ATTR(wr_bytes_loc,			0x2),
	ARM_CSPMU_EVENT_ATTR(wr_bytes_rem,			0x3),
	ARM_CSPMU_EVENT_ATTR(total_bytes_loc,			0x4),
	ARM_CSPMU_EVENT_ATTR(total_bytes_rem,			0x5),
	ARM_CSPMU_EVENT_ATTR(rd_req_loc,			0x6),
	ARM_CSPMU_EVENT_ATTR(rd_req_rem,			0x7),
	ARM_CSPMU_EVENT_ATTR(wr_req_loc,			0x8),
	ARM_CSPMU_EVENT_ATTR(wr_req_rem,			0x9),
	ARM_CSPMU_EVENT_ATTR(total_req_loc,			0xa),
	ARM_CSPMU_EVENT_ATTR(total_req_rem,			0xb),
	ARM_CSPMU_EVENT_ATTR(rd_cum_outs_loc,			0xc),
	ARM_CSPMU_EVENT_ATTR(rd_cum_outs_rem,			0xd),
	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL,
};

static struct attribute *ucf_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(bus_cycles,            0x1D),

	ARM_CSPMU_EVENT_ATTR(slc_allocate,          0xF0),
	ARM_CSPMU_EVENT_ATTR(slc_wb,                0xF3),
	ARM_CSPMU_EVENT_ATTR(slc_refill_rd,         0x109),
	ARM_CSPMU_EVENT_ATTR(slc_refill_wr,         0x10A),
	ARM_CSPMU_EVENT_ATTR(slc_hit_rd,            0x119),

	ARM_CSPMU_EVENT_ATTR(slc_access_dataless,   0x183),
	ARM_CSPMU_EVENT_ATTR(slc_access_atomic,     0x184),

	ARM_CSPMU_EVENT_ATTR(slc_access_rd,         0x111),
	ARM_CSPMU_EVENT_ATTR(slc_access_wr,         0x112),
	ARM_CSPMU_EVENT_ATTR(slc_bytes_rd,          0x113),
	ARM_CSPMU_EVENT_ATTR(slc_bytes_wr,          0x114),

	ARM_CSPMU_EVENT_ATTR(mem_access_rd,         0x121),
	ARM_CSPMU_EVENT_ATTR(mem_access_wr,         0x122),
	ARM_CSPMU_EVENT_ATTR(mem_bytes_rd,          0x123),
	ARM_CSPMU_EVENT_ATTR(mem_bytes_wr,          0x124),

	ARM_CSPMU_EVENT_ATTR(local_snoop,           0x180),
	ARM_CSPMU_EVENT_ATTR(ext_snp_access,        0x181),
	ARM_CSPMU_EVENT_ATTR(ext_snp_evict,         0x182),

	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL
};

static struct attribute *pcie_v2_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(rd_bytes,		0x0),
	ARM_CSPMU_EVENT_ATTR(wr_bytes,		0x1),
	ARM_CSPMU_EVENT_ATTR(rd_req,		0x2),
	ARM_CSPMU_EVENT_ATTR(wr_req,		0x3),
	ARM_CSPMU_EVENT_ATTR(rd_cum_outs,	0x4),
	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL
};

static struct attribute *pcie_tgt_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(rd_bytes,		0x0),
	ARM_CSPMU_EVENT_ATTR(wr_bytes,		0x1),
	ARM_CSPMU_EVENT_ATTR(rd_req,		0x2),
	ARM_CSPMU_EVENT_ATTR(wr_req,		0x3),
	ARM_CSPMU_EVENT_ATTR(cycles, NV_PCIE_TGT_EV_TYPE_CC),
	NULL
};

static struct attribute *generic_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL,
};

static struct attribute *scf_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	NULL,
};

static struct attribute *pcie_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(root_port, "config1:0-9"),
	NULL,
};

static struct attribute *nvlink_c2c_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(port, "config1:0-1"),
	NULL,
};

static struct attribute *cnvlink_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(rem_socket, "config1:0-3"),
	NULL,
};

static struct attribute *ucf_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(src_loc_noncpu, "config1:0"),
	ARM_CSPMU_FORMAT_ATTR(src_loc_cpu, "config1:1"),
	ARM_CSPMU_FORMAT_ATTR(src_rem, "config1:2"),
	ARM_CSPMU_FORMAT_ATTR(dst_loc_cmem, "config1:8"),
	ARM_CSPMU_FORMAT_ATTR(dst_loc_gmem, "config1:9"),
	ARM_CSPMU_FORMAT_ATTR(dst_loc_other, "config1:10"),
	ARM_CSPMU_FORMAT_ATTR(dst_rem, "config1:11"),
	NULL
};

static struct attribute *pcie_v2_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(src_rp_mask, "config1:0-7"),
	ARM_CSPMU_FORMAT_ATTR(src_bdf, "config1:8-23"),
	ARM_CSPMU_FORMAT_ATTR(src_bdf_en, "config1:24"),
	ARM_CSPMU_FORMAT_ATTR(dst_loc_cmem, "config2:0"),
	ARM_CSPMU_FORMAT_ATTR(dst_loc_gmem, "config2:1"),
	ARM_CSPMU_FORMAT_ATTR(dst_loc_pcie_p2p, "config2:2"),
	ARM_CSPMU_FORMAT_ATTR(dst_loc_pcie_cxl, "config2:3"),
	ARM_CSPMU_FORMAT_ATTR(dst_rem, "config2:4"),
	NULL
};

static struct attribute *pcie_tgt_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_ATTR(event, "config:0-2"),
	ARM_CSPMU_FORMAT_ATTR(dst_rp_mask, "config:3-10"),
	ARM_CSPMU_FORMAT_ATTR(dst_addr_en, "config:11"),
	ARM_CSPMU_FORMAT_ATTR(dst_addr_base, "config1:0-63"),
	ARM_CSPMU_FORMAT_ATTR(dst_addr_mask, "config2:0-63"),
	NULL
};

static struct attribute *generic_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_FILTER_ATTR,
	ARM_CSPMU_FORMAT_FILTER2_ATTR,
	NULL,
};

static struct attribute **
nv_cspmu_get_event_attrs(const struct arm_cspmu *cspmu)
{
	const struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);

	return ctx->event_attr;
}

static struct attribute **
nv_cspmu_get_format_attrs(const struct arm_cspmu *cspmu)
{
	const struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);

	return ctx->format_attr;
}

static const char *
nv_cspmu_get_name(const struct arm_cspmu *cspmu)
{
	const struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);

	return ctx->name;
}

#if defined(CONFIG_ACPI) && defined(CONFIG_ARM64)
static int nv_cspmu_get_inst_id(const struct arm_cspmu *cspmu, u32 *id)
{
	struct fwnode_handle *fwnode;
	struct acpi_device *adev;
	int ret;

	adev = arm_cspmu_acpi_dev_get(cspmu);
	if (!adev)
		return -ENODEV;

	fwnode = acpi_fwnode_handle(adev);
	ret = fwnode_property_read_u32(fwnode, "instance_id", id);
	if (ret)
		dev_err(cspmu->dev, "Failed to get instance ID\n");

	acpi_dev_put(adev);
	return ret;
}
#else
static int nv_cspmu_get_inst_id(const struct arm_cspmu *cspmu, u32 *id)
{
	return -EINVAL;
}
#endif

static u32 nv_cspmu_event_filter(const struct perf_event *event)
{
	const struct nv_cspmu_ctx *ctx =
		to_nv_cspmu_ctx(to_arm_cspmu(event->pmu));

	const u32 filter_val = event->attr.config1 & ctx->filter_mask;

	if (filter_val == 0)
		return ctx->filter_default_val;

	return filter_val;
}

static u32 nv_cspmu_event_filter2(const struct perf_event *event)
{
	const struct nv_cspmu_ctx *ctx =
		to_nv_cspmu_ctx(to_arm_cspmu(event->pmu));

	const u32 filter_val = event->attr.config2 & ctx->filter2_mask;

	if (filter_val == 0)
		return ctx->filter2_default_val;

	return filter_val;
}

static void nv_cspmu_set_ev_filter(struct arm_cspmu *cspmu,
				   const struct perf_event *event)
{
	u32 filter, offset;
	const struct nv_cspmu_ctx *ctx =
		to_nv_cspmu_ctx(to_arm_cspmu(event->pmu));
	offset = 4 * event->hw.idx;

	if (ctx->get_filter) {
		filter = ctx->get_filter(event);
		writel(filter, cspmu->base0 + PMEVFILTR + offset);
	}

	if (ctx->get_filter2) {
		filter = ctx->get_filter2(event);
		writel(filter, cspmu->base0 + PMEVFILT2R + offset);
	}
}

static void nv_cspmu_reset_ev_filter(struct arm_cspmu *cspmu,
				     const struct perf_event *event)
{
	const struct nv_cspmu_ctx *ctx =
		to_nv_cspmu_ctx(to_arm_cspmu(event->pmu));
	const u32 offset = 4 * event->hw.idx;

	if (ctx->get_filter)
		writel(0, cspmu->base0 + PMEVFILTR + offset);

	if (ctx->get_filter2)
		writel(0, cspmu->base0 + PMEVFILT2R + offset);
}

static void nv_cspmu_set_cc_filter(struct arm_cspmu *cspmu,
				   const struct perf_event *event)
{
	u32 filter = nv_cspmu_event_filter(event);

	writel(filter, cspmu->base0 + PMCCFILTR);
}

static u32 ucf_pmu_event_filter(const struct perf_event *event)
{
	u32 ret, filter, src, dst;

	filter = nv_cspmu_event_filter(event);

	/* Monitor all sources if none is selected. */
	src = FIELD_GET(NV_UCF_FILTER_SRC, filter);
	if (src == 0)
		src = GENMASK_ULL(NV_UCF_SRC_COUNT - 1, 0);

	/* Monitor all destinations if none is selected. */
	dst = FIELD_GET(NV_UCF_FILTER_DST, filter);
	if (dst == 0)
		dst = GENMASK_ULL(NV_UCF_DST_COUNT - 1, 0);

	ret = FIELD_PREP(NV_UCF_FILTER_SRC, src);
	ret |= FIELD_PREP(NV_UCF_FILTER_DST, dst);

	return ret;
}

static u32 pcie_v2_pmu_bdf_val_en(u32 filter)
{
	const u32 bdf_en = FIELD_GET(NV_PCIE_V2_FILTER_BDF_EN, filter);

	/* Returns both BDF value and enable bit if BDF filtering is enabled. */
	if (bdf_en)
		return FIELD_GET(NV_PCIE_V2_FILTER_BDF_VAL_EN, filter);

	/* Ignore the BDF value if BDF filter is not enabled. */
	return 0;
}

static u32 pcie_v2_pmu_event_filter(const struct perf_event *event)
{
	u32 filter, lead_filter, lead_bdf;
	struct perf_event *leader;
	const struct nv_cspmu_ctx *ctx =
		to_nv_cspmu_ctx(to_arm_cspmu(event->pmu));

	filter = event->attr.config1 & ctx->filter_mask;
	if (filter != 0)
		return filter;

	leader = event->group_leader;

	/* Use leader's filter value if its BDF filtering is enabled. */
	if (event != leader) {
		lead_filter = pcie_v2_pmu_event_filter(leader);
		lead_bdf = pcie_v2_pmu_bdf_val_en(lead_filter);
		if (lead_bdf != 0)
			return lead_filter;
	}

	/* Otherwise, return default filter value. */
	return ctx->filter_default_val;
}

static int pcie_v2_pmu_validate_event(struct arm_cspmu *cspmu,
				   struct perf_event *new_ev)
{
	/*
	 * Make sure the events are using same BDF filter since the PCIE-SRC PMU
	 * only supports one common BDF filter setting for all of the counters.
	 */

	int idx;
	u32 new_filter, new_rp, new_bdf, new_lead_filter, new_lead_bdf;
	struct perf_event *new_leader;

	if (cspmu->impl.ops.is_cycle_counter_event(new_ev))
		return 0;

	new_leader = new_ev->group_leader;

	new_filter = pcie_v2_pmu_event_filter(new_ev);
	new_lead_filter = pcie_v2_pmu_event_filter(new_leader);

	new_bdf = pcie_v2_pmu_bdf_val_en(new_filter);
	new_lead_bdf = pcie_v2_pmu_bdf_val_en(new_lead_filter);

	new_rp = FIELD_GET(NV_PCIE_V2_FILTER_PORT, new_filter);

	if (new_rp != 0 && new_bdf != 0) {
		dev_err(cspmu->dev,
			"RP and BDF filtering are mutually exclusive\n");
		return -EINVAL;
	}

	if (new_bdf != new_lead_bdf) {
		dev_err(cspmu->dev,
			"sibling and leader BDF value should be equal\n");
		return -EINVAL;
	}

	/* Compare BDF filter on existing events. */
	idx = find_first_bit(cspmu->hw_events.used_ctrs,
			     cspmu->cycle_counter_logical_idx);

	if (idx != cspmu->cycle_counter_logical_idx) {
		struct perf_event *leader = cspmu->hw_events.events[idx]->group_leader;

		const u32 lead_filter = pcie_v2_pmu_event_filter(leader);
		const u32 lead_bdf = pcie_v2_pmu_bdf_val_en(lead_filter);

		if (new_lead_bdf != lead_bdf) {
			dev_err(cspmu->dev, "only one BDF value is supported\n");
			return -EINVAL;
		}
	}

	return 0;
}

struct pcie_tgt_addr_filter {
	u32 refcount;
	u64 base;
	u64 mask;
};

struct pcie_tgt_data {
	struct pcie_tgt_addr_filter addr_filter[NV_PCIE_TGT_ADDR_COUNT];
	void __iomem *addr_filter_reg;
};

#if defined(CONFIG_ACPI) && defined(CONFIG_ARM64)
static int pcie_tgt_init_data(struct arm_cspmu *cspmu)
{
	int ret;
	struct acpi_device *adev;
	struct pcie_tgt_data *data;
	struct list_head resource_list;
	struct resource_entry *rentry;
	struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);
	struct device *dev = cspmu->dev;

	data = devm_kzalloc(dev, sizeof(struct pcie_tgt_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	adev = arm_cspmu_acpi_dev_get(cspmu);
	if (!adev) {
		dev_err(dev, "failed to get associated PCIE-TGT device\n");
		return -ENODEV;
	}

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_memory_resources(adev, &resource_list);
	if (ret < 0) {
		dev_err(dev, "failed to get PCIE-TGT device memory resources\n");
		acpi_dev_put(adev);
		return ret;
	}

	rentry = list_first_entry_or_null(
		&resource_list, struct resource_entry, node);
	if (rentry) {
		data->addr_filter_reg = devm_ioremap_resource(dev, rentry->res);
		ret = 0;
	}

	if (IS_ERR(data->addr_filter_reg)) {
		dev_err(dev, "failed to get address filter resource\n");
		ret = PTR_ERR(data->addr_filter_reg);
	}

	acpi_dev_free_resource_list(&resource_list);
	acpi_dev_put(adev);

	ctx->data = data;

	return ret;
}
#else
static int pcie_tgt_init_data(struct arm_cspmu *cspmu)
{
	return -ENODEV;
}
#endif

static struct pcie_tgt_data *pcie_tgt_get_data(struct arm_cspmu *cspmu)
{
	struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);

	return ctx->data;
}

/* Find the first available address filter slot. */
static int pcie_tgt_find_addr_idx(struct arm_cspmu *cspmu, u64 base, u64 mask,
	bool is_reset)
{
	int i;
	struct pcie_tgt_data *data = pcie_tgt_get_data(cspmu);

	for (i = 0; i < NV_PCIE_TGT_ADDR_COUNT; i++) {
		if (!is_reset && data->addr_filter[i].refcount == 0)
			return i;

		if (data->addr_filter[i].base == base &&
			data->addr_filter[i].mask == mask)
			return i;
	}

	return -ENODEV;
}

static u32 pcie_tgt_pmu_event_filter(const struct perf_event *event)
{
	u32 filter;

	filter = (event->attr.config >> NV_PCIE_TGT_EV_TYPE_COUNT) &
		NV_PCIE_TGT_FILTER2_MASK;

	return filter;
}

static bool pcie_tgt_pmu_addr_en(const struct perf_event *event)
{
	u32 filter = pcie_tgt_pmu_event_filter(event);

	return FIELD_GET(NV_PCIE_TGT_FILTER2_ADDR_EN, filter) != 0;
}

static u32 pcie_tgt_pmu_port_filter(const struct perf_event *event)
{
	u32 filter = pcie_tgt_pmu_event_filter(event);

	return FIELD_GET(NV_PCIE_TGT_FILTER2_PORT, filter);
}

static u64 pcie_tgt_pmu_dst_addr_base(const struct perf_event *event)
{
	return event->attr.config1;
}

static u64 pcie_tgt_pmu_dst_addr_mask(const struct perf_event *event)
{
	return event->attr.config2;
}

static int pcie_tgt_pmu_validate_event(struct arm_cspmu *cspmu,
				   struct perf_event *new_ev)
{
	u64 base, mask;
	int idx;

	if (!pcie_tgt_pmu_addr_en(new_ev))
		return 0;

	/* Make sure there is a slot available for the address filter. */
	base = pcie_tgt_pmu_dst_addr_base(new_ev);
	mask = pcie_tgt_pmu_dst_addr_mask(new_ev);
	idx = pcie_tgt_find_addr_idx(cspmu, base, mask, false);
	if (idx < 0)
		return -EINVAL;

	return 0;
}

static void pcie_tgt_pmu_config_addr_filter(struct arm_cspmu *cspmu,
	bool en, u64 base, u64 mask, int idx)
{
	struct pcie_tgt_data *data;
	struct pcie_tgt_addr_filter *filter;
	void __iomem *filter_reg;

	data = pcie_tgt_get_data(cspmu);
	filter = &data->addr_filter[idx];
	filter_reg = data->addr_filter_reg + (idx * NV_PCIE_TGT_ADDR_STRIDE);

	if (en) {
		filter->refcount++;
		if (filter->refcount == 1) {
			filter->base = base;
			filter->mask = mask;

			writel(lower_32_bits(base), filter_reg + NV_PCIE_TGT_ADDR_BASE_LO);
			writel(upper_32_bits(base), filter_reg + NV_PCIE_TGT_ADDR_BASE_HI);
			writel(lower_32_bits(mask), filter_reg + NV_PCIE_TGT_ADDR_MASK_LO);
			writel(upper_32_bits(mask), filter_reg + NV_PCIE_TGT_ADDR_MASK_HI);
			writel(1, filter_reg + NV_PCIE_TGT_ADDR_CTRL);
		}
	} else {
		filter->refcount--;
		if (filter->refcount == 0) {
			writel(0, filter_reg + NV_PCIE_TGT_ADDR_CTRL);
			writel(0, filter_reg + NV_PCIE_TGT_ADDR_BASE_LO);
			writel(0, filter_reg + NV_PCIE_TGT_ADDR_BASE_HI);
			writel(0, filter_reg + NV_PCIE_TGT_ADDR_MASK_LO);
			writel(0, filter_reg + NV_PCIE_TGT_ADDR_MASK_HI);

			filter->base = 0;
			filter->mask = 0;
		}
	}
}

static void pcie_tgt_pmu_set_ev_filter(struct arm_cspmu *cspmu,
				const struct perf_event *event)
{
	bool addr_filter_en;
	int idx;
	u32 filter2_val, filter2_offset, port_filter;
	u64 base, mask;

	filter2_val = 0;
	filter2_offset = PMEVFILT2R + (4 * event->hw.idx);

	addr_filter_en = pcie_tgt_pmu_addr_en(event);
	if (addr_filter_en) {
		base = pcie_tgt_pmu_dst_addr_base(event);
		mask = pcie_tgt_pmu_dst_addr_mask(event);
		idx = pcie_tgt_find_addr_idx(cspmu, base, mask, false);

		if (idx < 0) {
			dev_err(cspmu->dev,
				"Unable to find a slot for address filtering\n");
			writel(0, cspmu->base0 + filter2_offset);
			return;
		}

		/* Configure address range filter registers.*/
		pcie_tgt_pmu_config_addr_filter(cspmu, true, base, mask, idx);

		/* Config the counter to use the selected address filter slot. */
		filter2_val |= FIELD_PREP(NV_PCIE_TGT_FILTER2_ADDR, 1U << idx);
	}

	port_filter = pcie_tgt_pmu_port_filter(event);

	/* Monitor all ports if no filter is selected. */
	if (!addr_filter_en && port_filter == 0)
		port_filter = NV_PCIE_TGT_FILTER2_PORT;

	filter2_val |= FIELD_PREP(NV_PCIE_TGT_FILTER2_PORT, port_filter);

	writel(filter2_val, cspmu->base0 + filter2_offset);
}

static void pcie_tgt_pmu_reset_ev_filter(struct arm_cspmu *cspmu,
				     const struct perf_event *event)
{
	bool addr_filter_en;
	u64 base, mask;
	int idx;

	addr_filter_en = pcie_tgt_pmu_addr_en(event);
	if (!addr_filter_en)
		return;

	base = pcie_tgt_pmu_dst_addr_base(event);
	mask = pcie_tgt_pmu_dst_addr_mask(event);
	idx = pcie_tgt_find_addr_idx(cspmu, base, mask, true);

	if (idx < 0) {
		dev_err(cspmu->dev,
			"Unable to find the address filter slot to reset\n");
		return;
	}

	pcie_tgt_pmu_config_addr_filter(cspmu, false, base, mask, idx);
}

static u32 pcie_tgt_pmu_event_type(const struct perf_event *event)
{
	return event->attr.config & NV_PCIE_TGT_EV_TYPE_MASK;
}

static bool pcie_tgt_pmu_is_cycle_counter_event(const struct perf_event *event)
{
	u32 event_type = pcie_tgt_pmu_event_type(event);

	return event_type == NV_PCIE_TGT_EV_TYPE_CC;
}

enum nv_cspmu_name_fmt {
	NAME_FMT_GENERIC,
	NAME_FMT_SOCKET,
	NAME_FMT_SOCKET_INST,
};

struct nv_cspmu_match {
	u32 prodid;
	u32 prodid_mask;
	const char *name_pattern;
	enum nv_cspmu_name_fmt name_fmt;
	struct nv_cspmu_ctx template_ctx;
	struct arm_cspmu_impl_ops ops;
};

static const struct nv_cspmu_match nv_cspmu_match[] = {
	{
	  .prodid = 0x10300000,
	  .prodid_mask = NV_PRODID_MASK,
	  .name_pattern = "nvidia_pcie_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .template_ctx = {
		.event_attr = mcf_pmu_event_attrs,
		.format_attr = pcie_pmu_format_attrs,
		.filter_mask = NV_PCIE_FILTER_ID_MASK,
		.filter_default_val = NV_PCIE_FILTER_ID_MASK,
		.filter2_mask = 0x0,
		.filter2_default_val = 0x0,
		.get_filter = nv_cspmu_event_filter,
		.get_filter2 = NULL,
		.data = NULL,
		.init_data = NULL
	  },
	},
	{
	  .prodid = 0x10400000,
	  .prodid_mask = NV_PRODID_MASK,
	  .name_pattern = "nvidia_nvlink_c2c1_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .template_ctx = {
		.event_attr = mcf_pmu_event_attrs,
		.format_attr = nvlink_c2c_pmu_format_attrs,
		.filter_mask = NV_NVL_C2C_FILTER_ID_MASK,
		.filter_default_val = NV_NVL_C2C_FILTER_ID_MASK,
		.filter2_mask = 0x0,
		.filter2_default_val = 0x0,
		.get_filter = nv_cspmu_event_filter,
		.get_filter2 = NULL,
		.data = NULL,
		.init_data = NULL
	  },
	},
	{
	  .prodid = 0x10500000,
	  .prodid_mask = NV_PRODID_MASK,
	  .name_pattern = "nvidia_nvlink_c2c0_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .template_ctx = {
		.event_attr = mcf_pmu_event_attrs,
		.format_attr = nvlink_c2c_pmu_format_attrs,
		.filter_mask = NV_NVL_C2C_FILTER_ID_MASK,
		.filter_default_val = NV_NVL_C2C_FILTER_ID_MASK,
		.filter2_mask = 0x0,
		.filter2_default_val = 0x0,
		.get_filter = nv_cspmu_event_filter,
		.get_filter2 = NULL,
		.data = NULL,
		.init_data = NULL
	  },
	},
	{
	  .prodid = 0x10600000,
	  .prodid_mask = NV_PRODID_MASK,
	  .name_pattern = "nvidia_cnvlink_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .template_ctx = {
		.event_attr = mcf_pmu_event_attrs,
		.format_attr = cnvlink_pmu_format_attrs,
		.filter_mask = NV_CNVL_FILTER_ID_MASK,
		.filter_default_val = NV_CNVL_FILTER_ID_MASK,
		.filter2_mask = 0x0,
		.filter2_default_val = 0x0,
		.get_filter = nv_cspmu_event_filter,
		.get_filter2 = NULL,
		.data = NULL,
		.init_data = NULL
	  },
	},
	{
	  .prodid = 0x2CF00000,
	  .prodid_mask = NV_PRODID_MASK,
	  .name_pattern = "nvidia_scf_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .template_ctx = {
		.event_attr = scf_pmu_event_attrs,
		.format_attr = scf_pmu_format_attrs,
		.filter_mask = 0x0,
		.filter_default_val = 0x0,
		.filter2_mask = 0x0,
		.filter2_default_val = 0x0,
		.get_filter = nv_cspmu_event_filter,
		.get_filter2 = NULL,
		.data = NULL,
		.init_data = NULL
	  },
	},
	{
	  .prodid = 0x2CF20000,
	  .prodid_mask = NV_PRODID_MASK,
	  .name_pattern = "nvidia_ucf_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .template_ctx = {
		.event_attr = ucf_pmu_event_attrs,
		.format_attr = ucf_pmu_format_attrs,
		.filter_mask = NV_UCF_FILTER_ID_MASK,
		.filter_default_val = NV_UCF_FILTER_DEFAULT,
		.filter2_mask = 0x0,
		.filter2_default_val = 0x0,
		.get_filter = ucf_pmu_event_filter,
	  },
	},
	{
	  .prodid = 0x10301000,
	  .prodid_mask = NV_PRODID_MASK,
	  .name_pattern = "nvidia_pcie_pmu_%u_rc_%u",
	  .name_fmt = NAME_FMT_SOCKET_INST,
	  .template_ctx = {
		.event_attr = pcie_v2_pmu_event_attrs,
		.format_attr = pcie_v2_pmu_format_attrs,
		.filter_mask = NV_PCIE_V2_FILTER_ID_MASK,
		.filter_default_val = NV_PCIE_V2_FILTER_DEFAULT,
		.filter2_mask = NV_PCIE_V2_FILTER2_ID_MASK,
		.filter2_default_val = NV_PCIE_V2_FILTER2_DEFAULT,
		.get_filter = pcie_v2_pmu_event_filter,
		.get_filter2 = nv_cspmu_event_filter2,
	  },
	  .ops = {
		.validate_event = pcie_v2_pmu_validate_event,
		.reset_ev_filter = nv_cspmu_reset_ev_filter,
	  }
	},
	{
	  .prodid = 0x10700000,
	  .prodid_mask = NV_PRODID_MASK,
	  .name_pattern = "nvidia_pcie_tgt_pmu_%u_rc_%u",
	  .name_fmt = NAME_FMT_SOCKET_INST,
	  .template_ctx = {
		.event_attr = pcie_tgt_pmu_event_attrs,
		.format_attr = pcie_tgt_pmu_format_attrs,
		.filter_mask = 0x0,
		.filter_default_val = 0x0,
		.filter2_mask = NV_PCIE_TGT_FILTER2_MASK,
		.filter2_default_val = NV_PCIE_TGT_FILTER2_DEFAULT,
		.init_data = pcie_tgt_init_data
	  },
	  .ops = {
		.is_cycle_counter_event = pcie_tgt_pmu_is_cycle_counter_event,
		.event_type = pcie_tgt_pmu_event_type,
		.validate_event = pcie_tgt_pmu_validate_event,
		.set_ev_filter = pcie_tgt_pmu_set_ev_filter,
		.reset_ev_filter = pcie_tgt_pmu_reset_ev_filter,
	  }
	},
	{
	  .prodid = 0,
	  .prodid_mask = 0,
	  .name_pattern = "nvidia_uncore_pmu_%u",
	  .name_fmt = NAME_FMT_GENERIC,
	  .template_ctx = {
		.event_attr = generic_pmu_event_attrs,
		.format_attr = generic_pmu_format_attrs,
		.filter_mask = NV_GENERIC_FILTER_ID_MASK,
		.filter_default_val = NV_GENERIC_FILTER_ID_MASK,
		.filter2_mask = NV_GENERIC_FILTER_ID_MASK,
		.filter2_default_val = NV_GENERIC_FILTER_ID_MASK,
		.get_filter = nv_cspmu_event_filter,
		.get_filter2 = nv_cspmu_event_filter2,
		.data = NULL,
		.init_data = NULL
	  },
	},
};

static char *nv_cspmu_format_name(const struct arm_cspmu *cspmu,
				  const struct nv_cspmu_match *match)
{
	char *name = NULL;
	struct device *dev = cspmu->dev;

	static atomic_t pmu_generic_idx = {0};

	switch (match->name_fmt) {
	case NAME_FMT_SOCKET: {
		const int cpu = cpumask_first(&cspmu->associated_cpus);
		const int socket = cpu_to_node(cpu);

		name = devm_kasprintf(dev, GFP_KERNEL, match->name_pattern,
				       socket);
		break;
	}
	case NAME_FMT_SOCKET_INST: {
		const int cpu = cpumask_first(&cspmu->associated_cpus);
		const int socket = cpu_to_node(cpu);
		u32 inst_id;

		if (!nv_cspmu_get_inst_id(cspmu, &inst_id))
			name = devm_kasprintf(dev, GFP_KERNEL,
					match->name_pattern, socket, inst_id);
		break;
	}
	case NAME_FMT_GENERIC:
		name = devm_kasprintf(dev, GFP_KERNEL, match->name_pattern,
				       atomic_fetch_inc(&pmu_generic_idx));
		break;
	}

	return name;
}

#define SET_OP(name, impl, match, default_op) \
	do { \
		if (match->ops.name) \
			impl->name = match->ops.name; \
		else if (default_op != NULL) \
			impl->name = default_op; \
	} while (false)

static int nv_cspmu_init_ops(struct arm_cspmu *cspmu)
{
	struct nv_cspmu_ctx *ctx;
	struct device *dev = cspmu->dev;
	struct arm_cspmu_impl_ops *impl_ops = &cspmu->impl.ops;
	const struct nv_cspmu_match *match = nv_cspmu_match;

	ctx = devm_kzalloc(dev, sizeof(struct nv_cspmu_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/* Find matching PMU. */
	for (; match->prodid; match++) {
		const u32 prodid_mask = match->prodid_mask;

		if ((match->prodid & prodid_mask) ==
		    (cspmu->impl.pmiidr & prodid_mask))
			break;
	}

	/* Initialize the context with the matched template. */
	memcpy(ctx, &match->template_ctx, sizeof(struct nv_cspmu_ctx));
	ctx->name = nv_cspmu_format_name(cspmu, match);

	cspmu->impl.ctx = ctx;

	/* NVIDIA specific callbacks. */
	SET_OP(validate_event, impl_ops, match, NULL);
	SET_OP(event_type, impl_ops, match, NULL);
	SET_OP(is_cycle_counter_event, impl_ops, match, NULL);
	SET_OP(set_cc_filter, impl_ops, match, nv_cspmu_set_cc_filter);
	SET_OP(set_ev_filter, impl_ops, match, nv_cspmu_set_ev_filter);
	SET_OP(reset_ev_filter, impl_ops, match, NULL);
	SET_OP(get_event_attrs, impl_ops, match, nv_cspmu_get_event_attrs);
	SET_OP(get_format_attrs, impl_ops, match, nv_cspmu_get_format_attrs);
	SET_OP(get_name, impl_ops, match, nv_cspmu_get_name);

	if (ctx->init_data)
		return ctx->init_data(cspmu);

	return 0;
}

/* Match all NVIDIA Coresight PMU devices */
static const struct arm_cspmu_impl_match nv_cspmu_param = {
	.pmiidr_val	= ARM_CSPMU_IMPL_ID_NVIDIA,
	.module		= THIS_MODULE,
	.impl_init_ops	= nv_cspmu_init_ops
};

static int __init nvidia_cspmu_init(void)
{
	int ret;

	ret = arm_cspmu_impl_register(&nv_cspmu_param);
	if (ret)
		pr_err("nvidia_cspmu backend registration error: %d\n", ret);

	return ret;
}

static void __exit nvidia_cspmu_exit(void)
{
	arm_cspmu_impl_unregister(&nv_cspmu_param);
}

module_init(nvidia_cspmu_init);
module_exit(nvidia_cspmu_exit);

MODULE_DESCRIPTION("NVIDIA Coresight Architecture Performance Monitor Driver");
MODULE_LICENSE("GPL v2");
