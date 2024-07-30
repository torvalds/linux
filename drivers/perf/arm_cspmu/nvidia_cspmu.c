// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

/* Support for NVIDIA specific attributes. */

#include <linux/module.h>
#include <linux/topology.h>

#include "arm_cspmu.h"

#define NV_PCIE_PORT_COUNT           10ULL
#define NV_PCIE_FILTER_ID_MASK       GENMASK_ULL(NV_PCIE_PORT_COUNT - 1, 0)

#define NV_NVL_C2C_PORT_COUNT        2ULL
#define NV_NVL_C2C_FILTER_ID_MASK    GENMASK_ULL(NV_NVL_C2C_PORT_COUNT - 1, 0)

#define NV_CNVL_PORT_COUNT           4ULL
#define NV_CNVL_FILTER_ID_MASK       GENMASK_ULL(NV_CNVL_PORT_COUNT - 1, 0)

#define NV_GENERIC_FILTER_ID_MASK    GENMASK_ULL(31, 0)

#define NV_PRODID_MASK               GENMASK(31, 0)

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
	u32 filter_mask;
	u32 filter_default_val;
	struct attribute **event_attr;
	struct attribute **format_attr;
};

static struct attribute *scf_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(bus_cycles,			0x1d),

	ARM_CSPMU_EVENT_ATTR(scf_cache_allocate,		0xF0),
	ARM_CSPMU_EVENT_ATTR(scf_cache_refill,			0xF1),
	ARM_CSPMU_EVENT_ATTR(scf_cache,				0xF2),
	ARM_CSPMU_EVENT_ATTR(scf_cache_wb,			0xF3),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_data,			0x101),
	NV_CSPMU_EVENT_ATTR_4(socket, dl_rsp,			0x105),
	NV_CSPMU_EVENT_ATTR_4(socket, wb_data,			0x109),
	NV_CSPMU_EVENT_ATTR_4(socket, ev_rsp,			0x10d),
	NV_CSPMU_EVENT_ATTR_4(socket, prb_data,			0x111),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_outstanding,		0x115),
	NV_CSPMU_EVENT_ATTR_4(socket, dl_outstanding,		0x119),
	NV_CSPMU_EVENT_ATTR_4(socket, wb_outstanding,		0x11d),
	NV_CSPMU_EVENT_ATTR_4(socket, wr_outstanding,		0x121),
	NV_CSPMU_EVENT_ATTR_4(socket, ev_outstanding,		0x125),
	NV_CSPMU_EVENT_ATTR_4(socket, prb_outstanding,		0x129),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_access,		0x12d),
	NV_CSPMU_EVENT_ATTR_4(socket, dl_access,		0x131),
	NV_CSPMU_EVENT_ATTR_4(socket, wb_access,		0x135),
	NV_CSPMU_EVENT_ATTR_4(socket, wr_access,		0x139),
	NV_CSPMU_EVENT_ATTR_4(socket, ev_access,		0x13d),
	NV_CSPMU_EVENT_ATTR_4(socket, prb_access,		0x141),

	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_rd_data,		0x145),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_rd_access,		0x149),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wb_access,		0x14d),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_rd_outstanding,		0x151),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wr_outstanding,		0x155),

	NV_CSPMU_EVENT_ATTR_4(ocu, rem_rd_data,			0x159),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_rd_access,		0x15d),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wb_access,		0x161),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_rd_outstanding,		0x165),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wr_outstanding,		0x169),

	ARM_CSPMU_EVENT_ATTR(gmem_rd_data,			0x16d),
	ARM_CSPMU_EVENT_ATTR(gmem_rd_access,			0x16e),
	ARM_CSPMU_EVENT_ATTR(gmem_rd_outstanding,		0x16f),
	ARM_CSPMU_EVENT_ATTR(gmem_dl_rsp,			0x170),
	ARM_CSPMU_EVENT_ATTR(gmem_dl_access,			0x171),
	ARM_CSPMU_EVENT_ATTR(gmem_dl_outstanding,		0x172),
	ARM_CSPMU_EVENT_ATTR(gmem_wb_data,			0x173),
	ARM_CSPMU_EVENT_ATTR(gmem_wb_access,			0x174),
	ARM_CSPMU_EVENT_ATTR(gmem_wb_outstanding,		0x175),
	ARM_CSPMU_EVENT_ATTR(gmem_ev_rsp,			0x176),
	ARM_CSPMU_EVENT_ATTR(gmem_ev_access,			0x177),
	ARM_CSPMU_EVENT_ATTR(gmem_ev_outstanding,		0x178),
	ARM_CSPMU_EVENT_ATTR(gmem_wr_data,			0x179),
	ARM_CSPMU_EVENT_ATTR(gmem_wr_outstanding,		0x17a),
	ARM_CSPMU_EVENT_ATTR(gmem_wr_access,			0x17b),

	NV_CSPMU_EVENT_ATTR_4(socket, wr_data,			0x17c),

	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wr_data,		0x180),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wb_data,		0x184),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wr_access,		0x188),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wb_outstanding,		0x18c),

	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wr_data,			0x190),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wb_data,			0x194),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wr_access,		0x198),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wb_outstanding,		0x19c),

	ARM_CSPMU_EVENT_ATTR(gmem_wr_total_bytes,		0x1a0),
	ARM_CSPMU_EVENT_ATTR(remote_socket_wr_total_bytes,	0x1a1),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_data,		0x1a2),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_outstanding,	0x1a3),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_access,		0x1a4),

	ARM_CSPMU_EVENT_ATTR(cmem_rd_data,			0x1a5),
	ARM_CSPMU_EVENT_ATTR(cmem_rd_access,			0x1a6),
	ARM_CSPMU_EVENT_ATTR(cmem_rd_outstanding,		0x1a7),
	ARM_CSPMU_EVENT_ATTR(cmem_dl_rsp,			0x1a8),
	ARM_CSPMU_EVENT_ATTR(cmem_dl_access,			0x1a9),
	ARM_CSPMU_EVENT_ATTR(cmem_dl_outstanding,		0x1aa),
	ARM_CSPMU_EVENT_ATTR(cmem_wb_data,			0x1ab),
	ARM_CSPMU_EVENT_ATTR(cmem_wb_access,			0x1ac),
	ARM_CSPMU_EVENT_ATTR(cmem_wb_outstanding,		0x1ad),
	ARM_CSPMU_EVENT_ATTR(cmem_ev_rsp,			0x1ae),
	ARM_CSPMU_EVENT_ATTR(cmem_ev_access,			0x1af),
	ARM_CSPMU_EVENT_ATTR(cmem_ev_outstanding,		0x1b0),
	ARM_CSPMU_EVENT_ATTR(cmem_wr_data,			0x1b1),
	ARM_CSPMU_EVENT_ATTR(cmem_wr_outstanding,		0x1b2),

	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_rd_data,		0x1b3),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_rd_access,		0x1b7),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wb_access,		0x1bb),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_rd_outstanding,		0x1bf),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wr_outstanding,		0x1c3),

	ARM_CSPMU_EVENT_ATTR(ocu_prb_access,			0x1c7),
	ARM_CSPMU_EVENT_ATTR(ocu_prb_data,			0x1c8),
	ARM_CSPMU_EVENT_ATTR(ocu_prb_outstanding,		0x1c9),

	ARM_CSPMU_EVENT_ATTR(cmem_wr_access,			0x1ca),

	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wr_access,		0x1cb),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wb_data,		0x1cf),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wr_data,		0x1d3),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wb_outstanding,		0x1d7),

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
	NULL,
};

static struct attribute *cnvlink_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(rem_socket, "config1:0-3"),
	NULL,
};

static struct attribute *generic_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_FILTER_ATTR,
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

static u32 nv_cspmu_event_filter(const struct perf_event *event)
{
	const struct nv_cspmu_ctx *ctx =
		to_nv_cspmu_ctx(to_arm_cspmu(event->pmu));

	if (ctx->filter_mask == 0)
		return ctx->filter_default_val;

	return event->attr.config1 & ctx->filter_mask;
}

enum nv_cspmu_name_fmt {
	NAME_FMT_GENERIC,
	NAME_FMT_SOCKET
};

struct nv_cspmu_match {
	u32 prodid;
	u32 prodid_mask;
	u64 filter_mask;
	u32 filter_default_val;
	const char *name_pattern;
	enum nv_cspmu_name_fmt name_fmt;
	struct attribute **event_attr;
	struct attribute **format_attr;
};

static const struct nv_cspmu_match nv_cspmu_match[] = {
	{
	  .prodid = 0x103,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = NV_PCIE_FILTER_ID_MASK,
	  .filter_default_val = NV_PCIE_FILTER_ID_MASK,
	  .name_pattern = "nvidia_pcie_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = pcie_pmu_format_attrs
	},
	{
	  .prodid = 0x104,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = 0x0,
	  .filter_default_val = NV_NVL_C2C_FILTER_ID_MASK,
	  .name_pattern = "nvidia_nvlink_c2c1_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = nvlink_c2c_pmu_format_attrs
	},
	{
	  .prodid = 0x105,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = 0x0,
	  .filter_default_val = NV_NVL_C2C_FILTER_ID_MASK,
	  .name_pattern = "nvidia_nvlink_c2c0_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = nvlink_c2c_pmu_format_attrs
	},
	{
	  .prodid = 0x106,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = NV_CNVL_FILTER_ID_MASK,
	  .filter_default_val = NV_CNVL_FILTER_ID_MASK,
	  .name_pattern = "nvidia_cnvlink_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = cnvlink_pmu_format_attrs
	},
	{
	  .prodid = 0x2CF,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = 0x0,
	  .filter_default_val = 0x0,
	  .name_pattern = "nvidia_scf_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = scf_pmu_event_attrs,
	  .format_attr = scf_pmu_format_attrs
	},
	{
	  .prodid = 0,
	  .prodid_mask = 0,
	  .filter_mask = NV_GENERIC_FILTER_ID_MASK,
	  .filter_default_val = NV_GENERIC_FILTER_ID_MASK,
	  .name_pattern = "nvidia_uncore_pmu_%u",
	  .name_fmt = NAME_FMT_GENERIC,
	  .event_attr = generic_pmu_event_attrs,
	  .format_attr = generic_pmu_format_attrs
	},
};

static char *nv_cspmu_format_name(const struct arm_cspmu *cspmu,
				  const struct nv_cspmu_match *match)
{
	char *name;
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
	case NAME_FMT_GENERIC:
		name = devm_kasprintf(dev, GFP_KERNEL, match->name_pattern,
				       atomic_fetch_inc(&pmu_generic_idx));
		break;
	default:
		name = NULL;
		break;
	}

	return name;
}

static int nv_cspmu_init_ops(struct arm_cspmu *cspmu)
{
	u32 prodid;
	struct nv_cspmu_ctx *ctx;
	struct device *dev = cspmu->dev;
	struct arm_cspmu_impl_ops *impl_ops = &cspmu->impl.ops;
	const struct nv_cspmu_match *match = nv_cspmu_match;

	ctx = devm_kzalloc(dev, sizeof(struct nv_cspmu_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	prodid = FIELD_GET(ARM_CSPMU_PMIIDR_PRODUCTID, cspmu->impl.pmiidr);

	/* Find matching PMU. */
	for (; match->prodid; match++) {
		const u32 prodid_mask = match->prodid_mask;

		if ((match->prodid & prodid_mask) == (prodid & prodid_mask))
			break;
	}

	ctx->name		= nv_cspmu_format_name(cspmu, match);
	ctx->filter_mask	= match->filter_mask;
	ctx->filter_default_val = match->filter_default_val;
	ctx->event_attr		= match->event_attr;
	ctx->format_attr	= match->format_attr;

	cspmu->impl.ctx = ctx;

	/* NVIDIA specific callbacks. */
	impl_ops->event_filter			= nv_cspmu_event_filter;
	impl_ops->get_event_attrs		= nv_cspmu_get_event_attrs;
	impl_ops->get_format_attrs		= nv_cspmu_get_format_attrs;
	impl_ops->get_name			= nv_cspmu_get_name;

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
