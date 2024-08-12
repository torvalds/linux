// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright(c) 2023 Huawei
 *
 * The CXL 3.0 specification includes a standard Performance Monitoring Unit,
 * called the CXL PMU, or CPMU. In order to allow a high degree of
 * implementation flexibility the specification provides a wide range of
 * options all of which are self describing.
 *
 * Details in CXL rev 3.0 section 8.2.7 CPMU Register Interface
 */

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/perf_event.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/bits.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/pci.h>

#include "../cxl/cxlpci.h"
#include "../cxl/cxl.h"
#include "../cxl/pmu.h"

#define CXL_PMU_CAP_REG			0x0
#define   CXL_PMU_CAP_NUM_COUNTERS_MSK			GENMASK_ULL(5, 0)
#define   CXL_PMU_CAP_COUNTER_WIDTH_MSK			GENMASK_ULL(15, 8)
#define   CXL_PMU_CAP_NUM_EVN_CAP_REG_SUP_MSK		GENMASK_ULL(24, 20)
#define   CXL_PMU_CAP_FILTERS_SUP_MSK			GENMASK_ULL(39, 32)
#define     CXL_PMU_FILTER_HDM				BIT(0)
#define     CXL_PMU_FILTER_CHAN_RANK_BANK		BIT(1)
#define   CXL_PMU_CAP_MSI_N_MSK				GENMASK_ULL(47, 44)
#define   CXL_PMU_CAP_WRITEABLE_WHEN_FROZEN		BIT_ULL(48)
#define   CXL_PMU_CAP_FREEZE				BIT_ULL(49)
#define   CXL_PMU_CAP_INT				BIT_ULL(50)
#define   CXL_PMU_CAP_VERSION_MSK			GENMASK_ULL(63, 60)

#define CXL_PMU_OVERFLOW_REG		0x10
#define CXL_PMU_FREEZE_REG		0x18
#define CXL_PMU_EVENT_CAP_REG(n)	(0x100 + 8 * (n))
#define   CXL_PMU_EVENT_CAP_SUPPORTED_EVENTS_MSK	GENMASK_ULL(31, 0)
#define   CXL_PMU_EVENT_CAP_GROUP_ID_MSK		GENMASK_ULL(47, 32)
#define   CXL_PMU_EVENT_CAP_VENDOR_ID_MSK		GENMASK_ULL(63, 48)

#define CXL_PMU_COUNTER_CFG_REG(n)	(0x200 + 8 * (n))
#define   CXL_PMU_COUNTER_CFG_TYPE_MSK			GENMASK_ULL(1, 0)
#define     CXL_PMU_COUNTER_CFG_TYPE_FREE_RUN		0
#define     CXL_PMU_COUNTER_CFG_TYPE_FIXED_FUN		1
#define     CXL_PMU_COUNTER_CFG_TYPE_CONFIGURABLE	2
#define   CXL_PMU_COUNTER_CFG_ENABLE			BIT_ULL(8)
#define   CXL_PMU_COUNTER_CFG_INT_ON_OVRFLW		BIT_ULL(9)
#define   CXL_PMU_COUNTER_CFG_FREEZE_ON_OVRFLW		BIT_ULL(10)
#define   CXL_PMU_COUNTER_CFG_EDGE			BIT_ULL(11)
#define   CXL_PMU_COUNTER_CFG_INVERT			BIT_ULL(12)
#define   CXL_PMU_COUNTER_CFG_THRESHOLD_MSK		GENMASK_ULL(23, 16)
#define   CXL_PMU_COUNTER_CFG_EVENTS_MSK		GENMASK_ULL(55, 24)
#define   CXL_PMU_COUNTER_CFG_EVENT_GRP_ID_IDX_MSK	GENMASK_ULL(63, 59)

#define CXL_PMU_FILTER_CFG_REG(n, f)	(0x400 + 4 * ((f) + (n) * 8))
#define   CXL_PMU_FILTER_CFG_VALUE_MSK			GENMASK(31, 0)

#define CXL_PMU_COUNTER_REG(n)		(0xc00 + 8 * (n))

/* CXL rev 3.0 Table 13-5 Events under CXL Vendor ID */
#define CXL_PMU_GID_CLOCK_TICKS		0x00
#define CXL_PMU_GID_D2H_REQ		0x0010
#define CXL_PMU_GID_D2H_RSP		0x0011
#define CXL_PMU_GID_H2D_REQ		0x0012
#define CXL_PMU_GID_H2D_RSP		0x0013
#define CXL_PMU_GID_CACHE_DATA		0x0014
#define CXL_PMU_GID_M2S_REQ		0x0020
#define CXL_PMU_GID_M2S_RWD		0x0021
#define CXL_PMU_GID_M2S_BIRSP		0x0022
#define CXL_PMU_GID_S2M_BISNP		0x0023
#define CXL_PMU_GID_S2M_NDR		0x0024
#define CXL_PMU_GID_S2M_DRS		0x0025
#define CXL_PMU_GID_DDR			0x8000

static int cxl_pmu_cpuhp_state_num;

struct cxl_pmu_ev_cap {
	u16 vid;
	u16 gid;
	u32 msk;
	union {
		int counter_idx; /* fixed counters */
		int event_idx; /* configurable counters */
	};
	struct list_head node;
};

#define CXL_PMU_MAX_COUNTERS 64
struct cxl_pmu_info {
	struct pmu pmu;
	void __iomem *base;
	struct perf_event **hw_events;
	struct list_head event_caps_configurable;
	struct list_head event_caps_fixed;
	DECLARE_BITMAP(used_counter_bm, CXL_PMU_MAX_COUNTERS);
	DECLARE_BITMAP(conf_counter_bm, CXL_PMU_MAX_COUNTERS);
	u16 counter_width;
	u8 num_counters;
	u8 num_event_capabilities;
	int on_cpu;
	struct hlist_node node;
	bool filter_hdm;
	int irq;
};

#define pmu_to_cxl_pmu_info(_pmu) container_of(_pmu, struct cxl_pmu_info, pmu)

/*
 * All CPMU counters are discoverable via the Event Capabilities Registers.
 * Each Event Capability register contains a a VID / GroupID.
 * A counter may then count any combination (by summing) of events in
 * that group which are in the Supported Events Bitmask.
 * However, there are some complexities to the scheme.
 *  - Fixed function counters refer to an Event Capabilities register.
 *    That event capability register is not then used for Configurable
 *    counters.
 */
static int cxl_pmu_parse_caps(struct device *dev, struct cxl_pmu_info *info)
{
	unsigned long fixed_counter_event_cap_bm = 0;
	void __iomem *base = info->base;
	bool freeze_for_enable;
	u64 val, eval;
	int i;

	val = readq(base + CXL_PMU_CAP_REG);
	freeze_for_enable = FIELD_GET(CXL_PMU_CAP_WRITEABLE_WHEN_FROZEN, val) &&
		FIELD_GET(CXL_PMU_CAP_FREEZE, val);
	if (!freeze_for_enable) {
		dev_err(dev, "Counters not writable while frozen\n");
		return -ENODEV;
	}

	info->num_counters = FIELD_GET(CXL_PMU_CAP_NUM_COUNTERS_MSK, val) + 1;
	info->counter_width = FIELD_GET(CXL_PMU_CAP_COUNTER_WIDTH_MSK, val);
	info->num_event_capabilities = FIELD_GET(CXL_PMU_CAP_NUM_EVN_CAP_REG_SUP_MSK, val) + 1;

	info->filter_hdm = FIELD_GET(CXL_PMU_CAP_FILTERS_SUP_MSK, val) & CXL_PMU_FILTER_HDM;
	if (FIELD_GET(CXL_PMU_CAP_INT, val))
		info->irq = FIELD_GET(CXL_PMU_CAP_MSI_N_MSK, val);
	else
		info->irq = -1;

	/* First handle fixed function counters; note if configurable counters found */
	for (i = 0; i < info->num_counters; i++) {
		struct cxl_pmu_ev_cap *pmu_ev;
		u32 events_msk;
		u8 group_idx;

		val = readq(base + CXL_PMU_COUNTER_CFG_REG(i));

		if (FIELD_GET(CXL_PMU_COUNTER_CFG_TYPE_MSK, val) ==
			CXL_PMU_COUNTER_CFG_TYPE_CONFIGURABLE) {
			set_bit(i, info->conf_counter_bm);
		}

		if (FIELD_GET(CXL_PMU_COUNTER_CFG_TYPE_MSK, val) !=
		    CXL_PMU_COUNTER_CFG_TYPE_FIXED_FUN)
			continue;

		/* In this case we know which fields are const */
		group_idx = FIELD_GET(CXL_PMU_COUNTER_CFG_EVENT_GRP_ID_IDX_MSK, val);
		events_msk = FIELD_GET(CXL_PMU_COUNTER_CFG_EVENTS_MSK, val);
		eval = readq(base + CXL_PMU_EVENT_CAP_REG(group_idx));
		pmu_ev = devm_kzalloc(dev, sizeof(*pmu_ev), GFP_KERNEL);
		if (!pmu_ev)
			return -ENOMEM;

		pmu_ev->vid = FIELD_GET(CXL_PMU_EVENT_CAP_VENDOR_ID_MSK, eval);
		pmu_ev->gid = FIELD_GET(CXL_PMU_EVENT_CAP_GROUP_ID_MSK, eval);
		/* For a fixed purpose counter use the events mask from the counter CFG */
		pmu_ev->msk = events_msk;
		pmu_ev->counter_idx = i;
		/* This list add is never unwound as all entries deleted on remove */
		list_add(&pmu_ev->node, &info->event_caps_fixed);
		/*
		 * Configurable counters must not use an Event Capability registers that
		 * is in use for a Fixed counter
		 */
		set_bit(group_idx, &fixed_counter_event_cap_bm);
	}

	if (!bitmap_empty(info->conf_counter_bm, CXL_PMU_MAX_COUNTERS)) {
		struct cxl_pmu_ev_cap *pmu_ev;
		int j;
		/* Walk event capabilities unused by fixed counters */
		for_each_clear_bit(j, &fixed_counter_event_cap_bm,
				   info->num_event_capabilities) {
			pmu_ev = devm_kzalloc(dev, sizeof(*pmu_ev), GFP_KERNEL);
			if (!pmu_ev)
				return -ENOMEM;

			eval = readq(base + CXL_PMU_EVENT_CAP_REG(j));
			pmu_ev->vid = FIELD_GET(CXL_PMU_EVENT_CAP_VENDOR_ID_MSK, eval);
			pmu_ev->gid = FIELD_GET(CXL_PMU_EVENT_CAP_GROUP_ID_MSK, eval);
			pmu_ev->msk = FIELD_GET(CXL_PMU_EVENT_CAP_SUPPORTED_EVENTS_MSK, eval);
			pmu_ev->event_idx = j;
			list_add(&pmu_ev->node, &info->event_caps_configurable);
		}
	}

	return 0;
}

#define CXL_PMU_FORMAT_ATTR(_name, _format)\
	(&((struct dev_ext_attribute[]) {					\
		{								\
			.attr = __ATTR(_name, 0444, device_show_string, NULL),	\
			.var = (void *)_format					\
		}								\
		})[0].attr.attr)

enum {
	cxl_pmu_mask_attr,
	cxl_pmu_gid_attr,
	cxl_pmu_vid_attr,
	cxl_pmu_threshold_attr,
	cxl_pmu_invert_attr,
	cxl_pmu_edge_attr,
	cxl_pmu_hdm_filter_en_attr,
	cxl_pmu_hdm_attr,
};

static struct attribute *cxl_pmu_format_attr[] = {
	[cxl_pmu_mask_attr] = CXL_PMU_FORMAT_ATTR(mask, "config:0-31"),
	[cxl_pmu_gid_attr] = CXL_PMU_FORMAT_ATTR(gid, "config:32-47"),
	[cxl_pmu_vid_attr] = CXL_PMU_FORMAT_ATTR(vid, "config:48-63"),
	[cxl_pmu_threshold_attr] = CXL_PMU_FORMAT_ATTR(threshold, "config1:0-15"),
	[cxl_pmu_invert_attr] = CXL_PMU_FORMAT_ATTR(invert, "config1:16"),
	[cxl_pmu_edge_attr] = CXL_PMU_FORMAT_ATTR(edge, "config1:17"),
	[cxl_pmu_hdm_filter_en_attr] = CXL_PMU_FORMAT_ATTR(hdm_filter_en, "config1:18"),
	[cxl_pmu_hdm_attr] = CXL_PMU_FORMAT_ATTR(hdm, "config2:0-15"),
	NULL
};

#define CXL_PMU_ATTR_CONFIG_MASK_MSK		GENMASK_ULL(31, 0)
#define CXL_PMU_ATTR_CONFIG_GID_MSK		GENMASK_ULL(47, 32)
#define CXL_PMU_ATTR_CONFIG_VID_MSK		GENMASK_ULL(63, 48)
#define CXL_PMU_ATTR_CONFIG1_THRESHOLD_MSK	GENMASK_ULL(15, 0)
#define CXL_PMU_ATTR_CONFIG1_INVERT_MSK		BIT(16)
#define CXL_PMU_ATTR_CONFIG1_EDGE_MSK		BIT(17)
#define CXL_PMU_ATTR_CONFIG1_FILTER_EN_MSK	BIT(18)
#define CXL_PMU_ATTR_CONFIG2_HDM_MSK		GENMASK(15, 0)

static umode_t cxl_pmu_format_is_visible(struct kobject *kobj,
					 struct attribute *attr, int a)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_pmu_info *info = dev_get_drvdata(dev);

	/*
	 * Filter capability at the CPMU level, so hide the attributes if the particular
	 * filter is not supported.
	 */
	if (!info->filter_hdm &&
	    (attr == cxl_pmu_format_attr[cxl_pmu_hdm_filter_en_attr] ||
	     attr == cxl_pmu_format_attr[cxl_pmu_hdm_attr]))
		return 0;

	return attr->mode;
}

static const struct attribute_group cxl_pmu_format_group = {
	.name = "format",
	.attrs = cxl_pmu_format_attr,
	.is_visible = cxl_pmu_format_is_visible,
};

static u32 cxl_pmu_config_get_mask(struct perf_event *event)
{
	return FIELD_GET(CXL_PMU_ATTR_CONFIG_MASK_MSK, event->attr.config);
}

static u16 cxl_pmu_config_get_gid(struct perf_event *event)
{
	return FIELD_GET(CXL_PMU_ATTR_CONFIG_GID_MSK, event->attr.config);
}

static u16 cxl_pmu_config_get_vid(struct perf_event *event)
{
	return FIELD_GET(CXL_PMU_ATTR_CONFIG_VID_MSK, event->attr.config);
}

static u8 cxl_pmu_config1_get_threshold(struct perf_event *event)
{
	return FIELD_GET(CXL_PMU_ATTR_CONFIG1_THRESHOLD_MSK, event->attr.config1);
}

static bool cxl_pmu_config1_get_invert(struct perf_event *event)
{
	return FIELD_GET(CXL_PMU_ATTR_CONFIG1_INVERT_MSK, event->attr.config1);
}

static bool cxl_pmu_config1_get_edge(struct perf_event *event)
{
	return FIELD_GET(CXL_PMU_ATTR_CONFIG1_EDGE_MSK, event->attr.config1);
}

/*
 * CPMU specification allows for 8 filters, each with a 32 bit value...
 * So we need to find 8x32bits to store it in.
 * As the value used for disable is 0xffff_ffff, a separate enable switch
 * is needed.
 */

static bool cxl_pmu_config1_hdm_filter_en(struct perf_event *event)
{
	return FIELD_GET(CXL_PMU_ATTR_CONFIG1_FILTER_EN_MSK, event->attr.config1);
}

static u16 cxl_pmu_config2_get_hdm_decoder(struct perf_event *event)
{
	return FIELD_GET(CXL_PMU_ATTR_CONFIG2_HDM_MSK, event->attr.config2);
}

static ssize_t cxl_pmu_event_sysfs_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct perf_pmu_events_attr *pmu_attr =
		container_of(attr, struct perf_pmu_events_attr, attr);

	return sysfs_emit(buf, "config=%#llx\n", pmu_attr->id);
}

#define CXL_PMU_EVENT_ATTR(_name, _vid, _gid, _msk)			\
	PMU_EVENT_ATTR_ID(_name, cxl_pmu_event_sysfs_show,		\
			  ((u64)(_vid) << 48) | ((u64)(_gid) << 32) | (u64)(_msk))

/* For CXL spec defined events */
#define CXL_PMU_EVENT_CXL_ATTR(_name, _gid, _msk)			\
	CXL_PMU_EVENT_ATTR(_name, PCI_VENDOR_ID_CXL, _gid, _msk)

static struct attribute *cxl_pmu_event_attrs[] = {
	CXL_PMU_EVENT_CXL_ATTR(clock_ticks,			CXL_PMU_GID_CLOCK_TICKS, BIT(0)),
	/* CXL rev 3.0 Table 3-17 - Device to Host Requests */
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_rdcurr,			CXL_PMU_GID_D2H_REQ, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_rdown,			CXL_PMU_GID_D2H_REQ, BIT(2)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_rdshared,		CXL_PMU_GID_D2H_REQ, BIT(3)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_rdany,			CXL_PMU_GID_D2H_REQ, BIT(4)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_rdownnodata,		CXL_PMU_GID_D2H_REQ, BIT(5)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_itomwr,			CXL_PMU_GID_D2H_REQ, BIT(6)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_wrcurr,			CXL_PMU_GID_D2H_REQ, BIT(7)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_clflush,			CXL_PMU_GID_D2H_REQ, BIT(8)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_cleanevict,		CXL_PMU_GID_D2H_REQ, BIT(9)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_dirtyevict,		CXL_PMU_GID_D2H_REQ, BIT(10)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_cleanevictnodata,	CXL_PMU_GID_D2H_REQ, BIT(11)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_wowrinv,			CXL_PMU_GID_D2H_REQ, BIT(12)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_wowrinvf,		CXL_PMU_GID_D2H_REQ, BIT(13)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_wrinv,			CXL_PMU_GID_D2H_REQ, BIT(14)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_req_cacheflushed,		CXL_PMU_GID_D2H_REQ, BIT(16)),
	/* CXL rev 3.0 Table 3-20 - D2H Repsonse Encodings */
	CXL_PMU_EVENT_CXL_ATTR(d2h_rsp_rspihiti,		CXL_PMU_GID_D2H_RSP, BIT(4)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_rsp_rspvhitv,		CXL_PMU_GID_D2H_RSP, BIT(6)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_rsp_rspihitse,		CXL_PMU_GID_D2H_RSP, BIT(5)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_rsp_rspshitse,		CXL_PMU_GID_D2H_RSP, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_rsp_rspsfwdm,		CXL_PMU_GID_D2H_RSP, BIT(7)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_rsp_rspifwdm,		CXL_PMU_GID_D2H_RSP, BIT(15)),
	CXL_PMU_EVENT_CXL_ATTR(d2h_rsp_rspvfwdv,		CXL_PMU_GID_D2H_RSP, BIT(22)),
	/* CXL rev 3.0 Table 3-21 - CXL.cache - Mapping of H2D Requests to D2H Responses */
	CXL_PMU_EVENT_CXL_ATTR(h2d_req_snpdata,			CXL_PMU_GID_H2D_REQ, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(h2d_req_snpinv,			CXL_PMU_GID_H2D_REQ, BIT(2)),
	CXL_PMU_EVENT_CXL_ATTR(h2d_req_snpcur,			CXL_PMU_GID_H2D_REQ, BIT(3)),
	/* CXL rev 3.0 Table 3-22 - H2D Response Opcode Encodings */
	CXL_PMU_EVENT_CXL_ATTR(h2d_rsp_writepull,		CXL_PMU_GID_H2D_RSP, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(h2d_rsp_go,			CXL_PMU_GID_H2D_RSP, BIT(4)),
	CXL_PMU_EVENT_CXL_ATTR(h2d_rsp_gowritepull,		CXL_PMU_GID_H2D_RSP, BIT(5)),
	CXL_PMU_EVENT_CXL_ATTR(h2d_rsp_extcmp,			CXL_PMU_GID_H2D_RSP, BIT(6)),
	CXL_PMU_EVENT_CXL_ATTR(h2d_rsp_gowritepulldrop,		CXL_PMU_GID_H2D_RSP, BIT(8)),
	CXL_PMU_EVENT_CXL_ATTR(h2d_rsp_fastgowritepull,		CXL_PMU_GID_H2D_RSP, BIT(13)),
	CXL_PMU_EVENT_CXL_ATTR(h2d_rsp_goerrwritepull,		CXL_PMU_GID_H2D_RSP, BIT(15)),
	/* CXL rev 3.0 Table 13-5 directly lists these */
	CXL_PMU_EVENT_CXL_ATTR(cachedata_d2h_data,		CXL_PMU_GID_CACHE_DATA, BIT(0)),
	CXL_PMU_EVENT_CXL_ATTR(cachedata_h2d_data,		CXL_PMU_GID_CACHE_DATA, BIT(1)),
	/* CXL rev 3.0 Table 3-29 M2S Req Memory Opcodes */
	CXL_PMU_EVENT_CXL_ATTR(m2s_req_meminv,			CXL_PMU_GID_M2S_REQ, BIT(0)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_req_memrd,			CXL_PMU_GID_M2S_REQ, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_req_memrddata,		CXL_PMU_GID_M2S_REQ, BIT(2)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_req_memrdfwd,		CXL_PMU_GID_M2S_REQ, BIT(3)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_req_memwrfwd,		CXL_PMU_GID_M2S_REQ, BIT(4)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_req_memspecrd,		CXL_PMU_GID_M2S_REQ, BIT(8)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_req_meminvnt,		CXL_PMU_GID_M2S_REQ, BIT(9)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_req_memcleanevict,		CXL_PMU_GID_M2S_REQ, BIT(10)),
	/* CXL rev 3.0 Table 3-35 M2S RwD Memory Opcodes */
	CXL_PMU_EVENT_CXL_ATTR(m2s_rwd_memwr,			CXL_PMU_GID_M2S_RWD, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_rwd_memwrptl,		CXL_PMU_GID_M2S_RWD, BIT(2)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_rwd_biconflict,		CXL_PMU_GID_M2S_RWD, BIT(4)),
	/* CXL rev 3.0 Table 3-38 M2S BIRsp Memory Opcodes */
	CXL_PMU_EVENT_CXL_ATTR(m2s_birsp_i,			CXL_PMU_GID_M2S_BIRSP, BIT(0)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_birsp_s,			CXL_PMU_GID_M2S_BIRSP, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_birsp_e,			CXL_PMU_GID_M2S_BIRSP, BIT(2)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_birsp_iblk,			CXL_PMU_GID_M2S_BIRSP, BIT(4)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_birsp_sblk,			CXL_PMU_GID_M2S_BIRSP, BIT(5)),
	CXL_PMU_EVENT_CXL_ATTR(m2s_birsp_eblk,			CXL_PMU_GID_M2S_BIRSP, BIT(6)),
	/* CXL rev 3.0 Table 3-40 S2M BISnp Opcodes */
	CXL_PMU_EVENT_CXL_ATTR(s2m_bisnp_cur,			CXL_PMU_GID_S2M_BISNP, BIT(0)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_bisnp_data,			CXL_PMU_GID_S2M_BISNP, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_bisnp_inv,			CXL_PMU_GID_S2M_BISNP, BIT(2)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_bisnp_curblk,		CXL_PMU_GID_S2M_BISNP, BIT(4)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_bisnp_datblk,		CXL_PMU_GID_S2M_BISNP, BIT(5)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_bisnp_invblk,		CXL_PMU_GID_S2M_BISNP, BIT(6)),
	/* CXL rev 3.0 Table 3-43 S2M NDR Opcopdes */
	CXL_PMU_EVENT_CXL_ATTR(s2m_ndr_cmp,			CXL_PMU_GID_S2M_NDR, BIT(0)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_ndr_cmps,			CXL_PMU_GID_S2M_NDR, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_ndr_cmpe,			CXL_PMU_GID_S2M_NDR, BIT(2)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_ndr_biconflictack,		CXL_PMU_GID_S2M_NDR, BIT(4)),
	/* CXL rev 3.0 Table 3-46 S2M DRS opcodes */
	CXL_PMU_EVENT_CXL_ATTR(s2m_drs_memdata,			CXL_PMU_GID_S2M_DRS, BIT(0)),
	CXL_PMU_EVENT_CXL_ATTR(s2m_drs_memdatanxm,		CXL_PMU_GID_S2M_DRS, BIT(1)),
	/* CXL rev 3.0 Table 13-5 directly lists these */
	CXL_PMU_EVENT_CXL_ATTR(ddr_act,				CXL_PMU_GID_DDR, BIT(0)),
	CXL_PMU_EVENT_CXL_ATTR(ddr_pre,				CXL_PMU_GID_DDR, BIT(1)),
	CXL_PMU_EVENT_CXL_ATTR(ddr_casrd,			CXL_PMU_GID_DDR, BIT(2)),
	CXL_PMU_EVENT_CXL_ATTR(ddr_caswr,			CXL_PMU_GID_DDR, BIT(3)),
	CXL_PMU_EVENT_CXL_ATTR(ddr_refresh,			CXL_PMU_GID_DDR, BIT(4)),
	CXL_PMU_EVENT_CXL_ATTR(ddr_selfrefreshent,		CXL_PMU_GID_DDR, BIT(5)),
	CXL_PMU_EVENT_CXL_ATTR(ddr_rfm,				CXL_PMU_GID_DDR, BIT(6)),
	NULL
};

static struct cxl_pmu_ev_cap *cxl_pmu_find_fixed_counter_ev_cap(struct cxl_pmu_info *info,
								int vid, int gid, int msk)
{
	struct cxl_pmu_ev_cap *pmu_ev;

	list_for_each_entry(pmu_ev, &info->event_caps_fixed, node) {
		if (vid != pmu_ev->vid || gid != pmu_ev->gid)
			continue;

		/* Precise match for fixed counter */
		if (msk == pmu_ev->msk)
			return pmu_ev;
	}

	return ERR_PTR(-EINVAL);
}

static struct cxl_pmu_ev_cap *cxl_pmu_find_config_counter_ev_cap(struct cxl_pmu_info *info,
								 int vid, int gid, int msk)
{
	struct cxl_pmu_ev_cap *pmu_ev;

	list_for_each_entry(pmu_ev, &info->event_caps_configurable, node) {
		if (vid != pmu_ev->vid || gid != pmu_ev->gid)
			continue;

		/* Request mask must be subset of supported */
		if (msk & ~pmu_ev->msk)
			continue;

		return pmu_ev;
	}

	return ERR_PTR(-EINVAL);
}

static umode_t cxl_pmu_event_is_visible(struct kobject *kobj, struct attribute *attr, int a)
{
	struct device_attribute *dev_attr = container_of(attr, struct device_attribute, attr);
	struct perf_pmu_events_attr *pmu_attr =
		container_of(dev_attr, struct perf_pmu_events_attr, attr);
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_pmu_info *info = dev_get_drvdata(dev);
	int vid = FIELD_GET(CXL_PMU_ATTR_CONFIG_VID_MSK, pmu_attr->id);
	int gid = FIELD_GET(CXL_PMU_ATTR_CONFIG_GID_MSK, pmu_attr->id);
	int msk = FIELD_GET(CXL_PMU_ATTR_CONFIG_MASK_MSK, pmu_attr->id);

	if (!IS_ERR(cxl_pmu_find_fixed_counter_ev_cap(info, vid, gid, msk)))
		return attr->mode;

	if (!IS_ERR(cxl_pmu_find_config_counter_ev_cap(info, vid, gid, msk)))
		return attr->mode;

	return 0;
}

static const struct attribute_group cxl_pmu_events = {
	.name = "events",
	.attrs = cxl_pmu_event_attrs,
	.is_visible = cxl_pmu_event_is_visible,
};

static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct cxl_pmu_info *info = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(info->on_cpu));
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *cxl_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static const struct attribute_group cxl_pmu_cpumask_group = {
	.attrs = cxl_pmu_cpumask_attrs,
};

static const struct attribute_group *cxl_pmu_attr_groups[] = {
	&cxl_pmu_events,
	&cxl_pmu_format_group,
	&cxl_pmu_cpumask_group,
	NULL
};

/* If counter_idx == NULL, don't try to allocate a counter. */
static int cxl_pmu_get_event_idx(struct perf_event *event, int *counter_idx,
				 int *event_idx)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(event->pmu);
	DECLARE_BITMAP(configurable_and_free, CXL_PMU_MAX_COUNTERS);
	struct cxl_pmu_ev_cap *pmu_ev;
	u32 mask;
	u16 gid, vid;
	int i;

	vid = cxl_pmu_config_get_vid(event);
	gid = cxl_pmu_config_get_gid(event);
	mask = cxl_pmu_config_get_mask(event);

	pmu_ev = cxl_pmu_find_fixed_counter_ev_cap(info, vid, gid, mask);
	if (!IS_ERR(pmu_ev)) {
		if (!counter_idx)
			return 0;
		if (!test_bit(pmu_ev->counter_idx, info->used_counter_bm)) {
			*counter_idx = pmu_ev->counter_idx;
			return 0;
		}
		/* Fixed counter is in use, but maybe a configurable one? */
	}

	pmu_ev = cxl_pmu_find_config_counter_ev_cap(info, vid, gid, mask);
	if (!IS_ERR(pmu_ev)) {
		if (!counter_idx)
			return 0;

		bitmap_andnot(configurable_and_free, info->conf_counter_bm,
			info->used_counter_bm, CXL_PMU_MAX_COUNTERS);

		i = find_first_bit(configurable_and_free, CXL_PMU_MAX_COUNTERS);
		if (i == CXL_PMU_MAX_COUNTERS)
			return -EINVAL;

		*counter_idx = i;
		return 0;
	}

	return -EINVAL;
}

static int cxl_pmu_event_init(struct perf_event *event)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(event->pmu);
	int rc;

	/* Top level type sanity check - is this a Hardware Event being requested */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;
	/* TODO: Validation of any filter */

	/*
	 * Verify that it is possible to count what was requested. Either must
	 * be a fixed counter that is a precise match or a configurable counter
	 * where this is a subset.
	 */
	rc = cxl_pmu_get_event_idx(event, NULL, NULL);
	if (rc < 0)
		return rc;

	event->cpu = info->on_cpu;

	return 0;
}

static void cxl_pmu_enable(struct pmu *pmu)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(pmu);
	void __iomem *base = info->base;

	/* Can assume frozen at this stage */
	writeq(0, base + CXL_PMU_FREEZE_REG);
}

static void cxl_pmu_disable(struct pmu *pmu)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(pmu);
	void __iomem *base = info->base;

	/*
	 * Whilst bits above number of counters are RsvdZ
	 * they are unlikely to be repurposed given
	 * number of counters is allowed to be 64 leaving
	 * no reserved bits.  Hence this is only slightly
	 * naughty.
	 */
	writeq(GENMASK_ULL(63, 0), base + CXL_PMU_FREEZE_REG);
}

static void cxl_pmu_event_start(struct perf_event *event, int flags)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	void __iomem *base = info->base;
	u64 cfg;

	/*
	 * All paths to here should either set these flags directly or
	 * call cxl_pmu_event_stop() which will ensure the correct state.
	 */
	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	/*
	 * Currently only hdm filter control is implemnted, this code will
	 * want generalizing when more filters are added.
	 */
	if (info->filter_hdm) {
		if (cxl_pmu_config1_hdm_filter_en(event))
			cfg = cxl_pmu_config2_get_hdm_decoder(event);
		else
			cfg = GENMASK(31, 0); /* No filtering if 0xFFFF_FFFF */
		writeq(cfg, base + CXL_PMU_FILTER_CFG_REG(hwc->idx, 0));
	}

	cfg = readq(base + CXL_PMU_COUNTER_CFG_REG(hwc->idx));
	cfg |= FIELD_PREP(CXL_PMU_COUNTER_CFG_INT_ON_OVRFLW, 1);
	cfg |= FIELD_PREP(CXL_PMU_COUNTER_CFG_FREEZE_ON_OVRFLW, 1);
	cfg |= FIELD_PREP(CXL_PMU_COUNTER_CFG_ENABLE, 1);
	cfg |= FIELD_PREP(CXL_PMU_COUNTER_CFG_EDGE,
			  cxl_pmu_config1_get_edge(event) ? 1 : 0);
	cfg |= FIELD_PREP(CXL_PMU_COUNTER_CFG_INVERT,
			  cxl_pmu_config1_get_invert(event) ? 1 : 0);

	/* Fixed purpose counters have next two fields RO */
	if (test_bit(hwc->idx, info->conf_counter_bm)) {
		cfg |= FIELD_PREP(CXL_PMU_COUNTER_CFG_EVENT_GRP_ID_IDX_MSK,
				  hwc->event_base);
		cfg |= FIELD_PREP(CXL_PMU_COUNTER_CFG_EVENTS_MSK,
				  cxl_pmu_config_get_mask(event));
	}
	cfg &= ~CXL_PMU_COUNTER_CFG_THRESHOLD_MSK;
	/*
	 * For events that generate only 1 count per clock the CXL 3.0 spec
	 * states the threshold shall be set to 1 but if set to 0 it will
	 * count the raw value anwyay?
	 * There is no definition of what events will count multiple per cycle
	 * and hence to which non 1 values of threshold can apply.
	 * (CXL 3.0 8.2.7.2.1 Counter Configuration - threshold field definition)
	 */
	cfg |= FIELD_PREP(CXL_PMU_COUNTER_CFG_THRESHOLD_MSK,
			  cxl_pmu_config1_get_threshold(event));
	writeq(cfg, base + CXL_PMU_COUNTER_CFG_REG(hwc->idx));

	local64_set(&hwc->prev_count, 0);
	writeq(0, base + CXL_PMU_COUNTER_REG(hwc->idx));

	perf_event_update_userpage(event);
}

static u64 cxl_pmu_read_counter(struct perf_event *event)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(event->pmu);
	void __iomem *base = info->base;

	return readq(base + CXL_PMU_COUNTER_REG(event->hw.idx));
}

static void __cxl_pmu_read(struct perf_event *event, bool overflow)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 new_cnt, prev_cnt, delta;

	do {
		prev_cnt = local64_read(&hwc->prev_count);
		new_cnt = cxl_pmu_read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count, prev_cnt, new_cnt) != prev_cnt);

	/*
	 * If we know an overflow occur then take that into account.
	 * Note counter is not reset as that would lose events
	 */
	delta = (new_cnt - prev_cnt) & GENMASK_ULL(info->counter_width - 1, 0);
	if (overflow && delta < GENMASK_ULL(info->counter_width - 1, 0))
		delta += (1UL << info->counter_width);

	local64_add(delta, &event->count);
}

static void cxl_pmu_read(struct perf_event *event)
{
	__cxl_pmu_read(event, false);
}

static void cxl_pmu_event_stop(struct perf_event *event, int flags)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(event->pmu);
	void __iomem *base = info->base;
	struct hw_perf_event *hwc = &event->hw;
	u64 cfg;

	cxl_pmu_read(event);
	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	cfg = readq(base + CXL_PMU_COUNTER_CFG_REG(hwc->idx));
	cfg &= ~(FIELD_PREP(CXL_PMU_COUNTER_CFG_INT_ON_OVRFLW, 1) |
		 FIELD_PREP(CXL_PMU_COUNTER_CFG_ENABLE, 1));
	writeq(cfg, base + CXL_PMU_COUNTER_CFG_REG(hwc->idx));

	hwc->state |= PERF_HES_UPTODATE;
}

static int cxl_pmu_event_add(struct perf_event *event, int flags)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx, rc;
	int event_idx = 0;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	rc = cxl_pmu_get_event_idx(event, &idx, &event_idx);
	if (rc < 0)
		return rc;

	hwc->idx = idx;

	/* Only set for configurable counters */
	hwc->event_base = event_idx;
	info->hw_events[idx] = event;
	set_bit(idx, info->used_counter_bm);

	if (flags & PERF_EF_START)
		cxl_pmu_event_start(event, PERF_EF_RELOAD);

	return 0;
}

static void cxl_pmu_event_del(struct perf_event *event, int flags)
{
	struct cxl_pmu_info *info = pmu_to_cxl_pmu_info(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	cxl_pmu_event_stop(event, PERF_EF_UPDATE);
	clear_bit(hwc->idx, info->used_counter_bm);
	info->hw_events[hwc->idx] = NULL;
	perf_event_update_userpage(event);
}

static irqreturn_t cxl_pmu_irq(int irq, void *data)
{
	struct cxl_pmu_info *info = data;
	void __iomem *base = info->base;
	u64 overflowed;
	DECLARE_BITMAP(overflowedbm, 64);
	int i;

	overflowed = readq(base + CXL_PMU_OVERFLOW_REG);

	/* Interrupt may be shared, so maybe it isn't ours */
	if (!overflowed)
		return IRQ_NONE;

	bitmap_from_arr64(overflowedbm, &overflowed, 64);
	for_each_set_bit(i, overflowedbm, info->num_counters) {
		struct perf_event *event = info->hw_events[i];

		if (!event) {
			dev_dbg(info->pmu.dev,
				"overflow but on non enabled counter %d\n", i);
			continue;
		}

		__cxl_pmu_read(event, true);
	}

	writeq(overflowed, base + CXL_PMU_OVERFLOW_REG);

	return IRQ_HANDLED;
}

static void cxl_pmu_perf_unregister(void *_info)
{
	struct cxl_pmu_info *info = _info;

	perf_pmu_unregister(&info->pmu);
}

static void cxl_pmu_cpuhp_remove(void *_info)
{
	struct cxl_pmu_info *info = _info;

	cpuhp_state_remove_instance_nocalls(cxl_pmu_cpuhp_state_num, &info->node);
}

static int cxl_pmu_probe(struct device *dev)
{
	struct cxl_pmu *pmu = to_cxl_pmu(dev);
	struct pci_dev *pdev = to_pci_dev(dev->parent);
	struct cxl_pmu_info *info;
	char *irq_name;
	char *dev_name;
	int rc, irq;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	dev_set_drvdata(dev, info);
	INIT_LIST_HEAD(&info->event_caps_fixed);
	INIT_LIST_HEAD(&info->event_caps_configurable);

	info->base = pmu->base;

	info->on_cpu = -1;
	rc = cxl_pmu_parse_caps(dev, info);
	if (rc)
		return rc;

	info->hw_events = devm_kcalloc(dev, sizeof(*info->hw_events),
				       info->num_counters, GFP_KERNEL);
	if (!info->hw_events)
		return -ENOMEM;

	switch (pmu->type) {
	case CXL_PMU_MEMDEV:
		dev_name = devm_kasprintf(dev, GFP_KERNEL, "cxl_pmu_mem%d.%d",
					  pmu->assoc_id, pmu->index);
		break;
	}
	if (!dev_name)
		return -ENOMEM;

	info->pmu = (struct pmu) {
		.name = dev_name,
		.parent = dev,
		.module = THIS_MODULE,
		.event_init = cxl_pmu_event_init,
		.pmu_enable = cxl_pmu_enable,
		.pmu_disable = cxl_pmu_disable,
		.add = cxl_pmu_event_add,
		.del = cxl_pmu_event_del,
		.start = cxl_pmu_event_start,
		.stop = cxl_pmu_event_stop,
		.read = cxl_pmu_read,
		.task_ctx_nr = perf_invalid_context,
		.attr_groups = cxl_pmu_attr_groups,
		.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
	};

	if (info->irq <= 0)
		return -EINVAL;

	rc = pci_irq_vector(pdev, info->irq);
	if (rc < 0)
		return rc;
	irq = rc;

	irq_name = devm_kasprintf(dev, GFP_KERNEL, "%s_overflow\n", dev_name);
	if (!irq_name)
		return -ENOMEM;

	rc = devm_request_irq(dev, irq, cxl_pmu_irq, IRQF_SHARED | IRQF_ONESHOT,
			      irq_name, info);
	if (rc)
		return rc;
	info->irq = irq;

	rc = cpuhp_state_add_instance(cxl_pmu_cpuhp_state_num, &info->node);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(dev, cxl_pmu_cpuhp_remove, info);
	if (rc)
		return rc;

	rc = perf_pmu_register(&info->pmu, info->pmu.name, -1);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(dev, cxl_pmu_perf_unregister, info);
	if (rc)
		return rc;

	return 0;
}

static struct cxl_driver cxl_pmu_driver = {
	.name = "cxl_pmu",
	.probe = cxl_pmu_probe,
	.id = CXL_DEVICE_PMU,
};

static int cxl_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct cxl_pmu_info *info = hlist_entry_safe(node, struct cxl_pmu_info, node);

	if (info->on_cpu != -1)
		return 0;

	info->on_cpu = cpu;
	/*
	 * CPU HP lock is held so we should be guaranteed that the CPU hasn't yet
	 * gone away again.
	 */
	WARN_ON(irq_set_affinity(info->irq, cpumask_of(cpu)));

	return 0;
}

static int cxl_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct cxl_pmu_info *info = hlist_entry_safe(node, struct cxl_pmu_info, node);
	unsigned int target;

	if (info->on_cpu != cpu)
		return 0;

	info->on_cpu = -1;
	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids) {
		dev_err(info->pmu.dev, "Unable to find a suitable CPU\n");
		return 0;
	}

	perf_pmu_migrate_context(&info->pmu, cpu, target);
	info->on_cpu = target;
	/*
	 * CPU HP lock is held so we should be guaranteed that this CPU hasn't yet
	 * gone away.
	 */
	WARN_ON(irq_set_affinity(info->irq, cpumask_of(target)));

	return 0;
}

static __init int cxl_pmu_init(void)
{
	int rc;

	rc = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				     "AP_PERF_CXL_PMU_ONLINE",
				     cxl_pmu_online_cpu, cxl_pmu_offline_cpu);
	if (rc < 0)
		return rc;
	cxl_pmu_cpuhp_state_num = rc;

	rc = cxl_driver_register(&cxl_pmu_driver);
	if (rc)
		cpuhp_remove_multi_state(cxl_pmu_cpuhp_state_num);

	return rc;
}

static __exit void cxl_pmu_exit(void)
{
	cxl_driver_unregister(&cxl_pmu_driver);
	cpuhp_remove_multi_state(cxl_pmu_cpuhp_state_num);
}

MODULE_DESCRIPTION("CXL Performance Monitor Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(CXL);
module_init(cxl_pmu_init);
module_exit(cxl_pmu_exit);
MODULE_ALIAS_CXL(CXL_DEVICE_PMU);
