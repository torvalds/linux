/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2014 ARM Limited
 */

#include <linux/ctype.h>
#include <linux/hrtimer.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define CCN_NUM_XP_PORTS 2
#define CCN_NUM_VCS 4
#define CCN_NUM_REGIONS	256
#define CCN_REGION_SIZE	0x10000

#define CCN_ALL_OLY_ID			0xff00
#define CCN_ALL_OLY_ID__OLY_ID__SHIFT			0
#define CCN_ALL_OLY_ID__OLY_ID__MASK			0x1f
#define CCN_ALL_OLY_ID__NODE_ID__SHIFT			8
#define CCN_ALL_OLY_ID__NODE_ID__MASK			0x3f

#define CCN_MN_ERRINT_STATUS		0x0008
#define CCN_MN_ERRINT_STATUS__INTREQ__DESSERT		0x11
#define CCN_MN_ERRINT_STATUS__ALL_ERRORS__ENABLE	0x02
#define CCN_MN_ERRINT_STATUS__ALL_ERRORS__DISABLED	0x20
#define CCN_MN_ERRINT_STATUS__ALL_ERRORS__DISABLE	0x22
#define CCN_MN_ERRINT_STATUS__CORRECTED_ERRORS_ENABLE	0x04
#define CCN_MN_ERRINT_STATUS__CORRECTED_ERRORS_DISABLED	0x40
#define CCN_MN_ERRINT_STATUS__CORRECTED_ERRORS_DISABLE	0x44
#define CCN_MN_ERRINT_STATUS__PMU_EVENTS__ENABLE	0x08
#define CCN_MN_ERRINT_STATUS__PMU_EVENTS__DISABLED	0x80
#define CCN_MN_ERRINT_STATUS__PMU_EVENTS__DISABLE	0x88
#define CCN_MN_OLY_COMP_LIST_63_0	0x01e0
#define CCN_MN_ERR_SIG_VAL_63_0		0x0300
#define CCN_MN_ERR_SIG_VAL_63_0__DT			(1 << 1)

#define CCN_DT_ACTIVE_DSM		0x0000
#define CCN_DT_ACTIVE_DSM__DSM_ID__SHIFT(n)		((n) * 8)
#define CCN_DT_ACTIVE_DSM__DSM_ID__MASK			0xff
#define CCN_DT_CTL			0x0028
#define CCN_DT_CTL__DT_EN				(1 << 0)
#define CCN_DT_PMEVCNT(n)		(0x0100 + (n) * 0x8)
#define CCN_DT_PMCCNTR			0x0140
#define CCN_DT_PMCCNTRSR		0x0190
#define CCN_DT_PMOVSR			0x0198
#define CCN_DT_PMOVSR_CLR		0x01a0
#define CCN_DT_PMOVSR_CLR__MASK				0x1f
#define CCN_DT_PMCR			0x01a8
#define CCN_DT_PMCR__OVFL_INTR_EN			(1 << 6)
#define CCN_DT_PMCR__PMU_EN				(1 << 0)
#define CCN_DT_PMSR			0x01b0
#define CCN_DT_PMSR_REQ			0x01b8
#define CCN_DT_PMSR_CLR			0x01c0

#define CCN_HNF_PMU_EVENT_SEL		0x0600
#define CCN_HNF_PMU_EVENT_SEL__ID__SHIFT(n)		((n) * 4)
#define CCN_HNF_PMU_EVENT_SEL__ID__MASK			0xf

#define CCN_XP_DT_CONFIG		0x0300
#define CCN_XP_DT_CONFIG__DT_CFG__SHIFT(n)		((n) * 4)
#define CCN_XP_DT_CONFIG__DT_CFG__MASK			0xf
#define CCN_XP_DT_CONFIG__DT_CFG__PASS_THROUGH		0x0
#define CCN_XP_DT_CONFIG__DT_CFG__WATCHPOINT_0_OR_1	0x1
#define CCN_XP_DT_CONFIG__DT_CFG__WATCHPOINT(n)		(0x2 + (n))
#define CCN_XP_DT_CONFIG__DT_CFG__XP_PMU_EVENT(n)	(0x4 + (n))
#define CCN_XP_DT_CONFIG__DT_CFG__DEVICE_PMU_EVENT(d, n) (0x8 + (d) * 4 + (n))
#define CCN_XP_DT_INTERFACE_SEL		0x0308
#define CCN_XP_DT_INTERFACE_SEL__DT_IO_SEL__SHIFT(n)	(0 + (n) * 8)
#define CCN_XP_DT_INTERFACE_SEL__DT_IO_SEL__MASK	0x1
#define CCN_XP_DT_INTERFACE_SEL__DT_DEV_SEL__SHIFT(n)	(1 + (n) * 8)
#define CCN_XP_DT_INTERFACE_SEL__DT_DEV_SEL__MASK	0x1
#define CCN_XP_DT_INTERFACE_SEL__DT_VC_SEL__SHIFT(n)	(2 + (n) * 8)
#define CCN_XP_DT_INTERFACE_SEL__DT_VC_SEL__MASK	0x3
#define CCN_XP_DT_CMP_VAL_L(n)		(0x0310 + (n) * 0x40)
#define CCN_XP_DT_CMP_VAL_H(n)		(0x0318 + (n) * 0x40)
#define CCN_XP_DT_CMP_MASK_L(n)		(0x0320 + (n) * 0x40)
#define CCN_XP_DT_CMP_MASK_H(n)		(0x0328 + (n) * 0x40)
#define CCN_XP_DT_CONTROL		0x0370
#define CCN_XP_DT_CONTROL__DT_ENABLE			(1 << 0)
#define CCN_XP_DT_CONTROL__WP_ARM_SEL__SHIFT(n)		(12 + (n) * 4)
#define CCN_XP_DT_CONTROL__WP_ARM_SEL__MASK		0xf
#define CCN_XP_DT_CONTROL__WP_ARM_SEL__ALWAYS		0xf
#define CCN_XP_PMU_EVENT_SEL		0x0600
#define CCN_XP_PMU_EVENT_SEL__ID__SHIFT(n)		((n) * 7)
#define CCN_XP_PMU_EVENT_SEL__ID__MASK			0x3f

#define CCN_SBAS_PMU_EVENT_SEL		0x0600
#define CCN_SBAS_PMU_EVENT_SEL__ID__SHIFT(n)		((n) * 4)
#define CCN_SBAS_PMU_EVENT_SEL__ID__MASK		0xf

#define CCN_RNI_PMU_EVENT_SEL		0x0600
#define CCN_RNI_PMU_EVENT_SEL__ID__SHIFT(n)		((n) * 4)
#define CCN_RNI_PMU_EVENT_SEL__ID__MASK			0xf

#define CCN_TYPE_MN	0x01
#define CCN_TYPE_DT	0x02
#define CCN_TYPE_HNF	0x04
#define CCN_TYPE_HNI	0x05
#define CCN_TYPE_XP	0x08
#define CCN_TYPE_SBSX	0x0c
#define CCN_TYPE_SBAS	0x10
#define CCN_TYPE_RNI_1P	0x14
#define CCN_TYPE_RNI_2P	0x15
#define CCN_TYPE_RNI_3P	0x16
#define CCN_TYPE_RND_1P	0x18 /* RN-D = RN-I + DVM */
#define CCN_TYPE_RND_2P	0x19
#define CCN_TYPE_RND_3P	0x1a
#define CCN_TYPE_CYCLES	0xff /* Pseudotype */

#define CCN_EVENT_WATCHPOINT 0xfe /* Pseudoevent */

#define CCN_NUM_PMU_EVENTS		4
#define CCN_NUM_XP_WATCHPOINTS		2 /* See DT.dbg_id.num_watchpoints */
#define CCN_NUM_PMU_EVENT_COUNTERS	8 /* See DT.dbg_id.num_pmucntr */
#define CCN_IDX_PMU_CYCLE_COUNTER	CCN_NUM_PMU_EVENT_COUNTERS

#define CCN_NUM_PREDEFINED_MASKS	4
#define CCN_IDX_MASK_ANY		(CCN_NUM_PMU_EVENT_COUNTERS + 0)
#define CCN_IDX_MASK_EXACT		(CCN_NUM_PMU_EVENT_COUNTERS + 1)
#define CCN_IDX_MASK_ORDER		(CCN_NUM_PMU_EVENT_COUNTERS + 2)
#define CCN_IDX_MASK_OPCODE		(CCN_NUM_PMU_EVENT_COUNTERS + 3)

struct arm_ccn_component {
	void __iomem *base;
	u32 type;

	DECLARE_BITMAP(pmu_events_mask, CCN_NUM_PMU_EVENTS);
	union {
		struct {
			DECLARE_BITMAP(dt_cmp_mask, CCN_NUM_XP_WATCHPOINTS);
		} xp;
	};
};

#define pmu_to_arm_ccn(_pmu) container_of(container_of(_pmu, \
	struct arm_ccn_dt, pmu), struct arm_ccn, dt)

struct arm_ccn_dt {
	int id;
	void __iomem *base;

	spinlock_t config_lock;

	DECLARE_BITMAP(pmu_counters_mask, CCN_NUM_PMU_EVENT_COUNTERS + 1);
	struct {
		struct arm_ccn_component *source;
		struct perf_event *event;
	} pmu_counters[CCN_NUM_PMU_EVENT_COUNTERS + 1];

	struct {
	       u64 l, h;
	} cmp_mask[CCN_NUM_PMU_EVENT_COUNTERS + CCN_NUM_PREDEFINED_MASKS];

	struct hrtimer hrtimer;

	cpumask_t cpu;
	struct hlist_node node;

	struct pmu pmu;
};

struct arm_ccn {
	struct device *dev;
	void __iomem *base;
	unsigned int irq;

	unsigned sbas_present:1;
	unsigned sbsx_present:1;

	int num_nodes;
	struct arm_ccn_component *node;

	int num_xps;
	struct arm_ccn_component *xp;

	struct arm_ccn_dt dt;
	int mn_id;
};

static int arm_ccn_node_to_xp(int node)
{
	return node / CCN_NUM_XP_PORTS;
}

static int arm_ccn_node_to_xp_port(int node)
{
	return node % CCN_NUM_XP_PORTS;
}


/*
 * Bit shifts and masks in these defines must be kept in sync with
 * arm_ccn_pmu_config_set() and CCN_FORMAT_ATTRs below!
 */
#define CCN_CONFIG_NODE(_config)	(((_config) >> 0) & 0xff)
#define CCN_CONFIG_XP(_config)		(((_config) >> 0) & 0xff)
#define CCN_CONFIG_TYPE(_config)	(((_config) >> 8) & 0xff)
#define CCN_CONFIG_EVENT(_config)	(((_config) >> 16) & 0xff)
#define CCN_CONFIG_PORT(_config)	(((_config) >> 24) & 0x3)
#define CCN_CONFIG_BUS(_config)		(((_config) >> 24) & 0x3)
#define CCN_CONFIG_VC(_config)		(((_config) >> 26) & 0x7)
#define CCN_CONFIG_DIR(_config)		(((_config) >> 29) & 0x1)
#define CCN_CONFIG_MASK(_config)	(((_config) >> 30) & 0xf)

static void arm_ccn_pmu_config_set(u64 *config, u32 node_xp, u32 type, u32 port)
{
	*config &= ~((0xff << 0) | (0xff << 8) | (0x3 << 24));
	*config |= (node_xp << 0) | (type << 8) | (port << 24);
}

static ssize_t arm_ccn_pmu_format_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = container_of(attr,
			struct dev_ext_attribute, attr);

	return snprintf(buf, PAGE_SIZE, "%s\n", (char *)ea->var);
}

#define CCN_FORMAT_ATTR(_name, _config) \
	struct dev_ext_attribute arm_ccn_pmu_format_attr_##_name = \
			{ __ATTR(_name, S_IRUGO, arm_ccn_pmu_format_show, \
			NULL), _config }

static CCN_FORMAT_ATTR(node, "config:0-7");
static CCN_FORMAT_ATTR(xp, "config:0-7");
static CCN_FORMAT_ATTR(type, "config:8-15");
static CCN_FORMAT_ATTR(event, "config:16-23");
static CCN_FORMAT_ATTR(port, "config:24-25");
static CCN_FORMAT_ATTR(bus, "config:24-25");
static CCN_FORMAT_ATTR(vc, "config:26-28");
static CCN_FORMAT_ATTR(dir, "config:29-29");
static CCN_FORMAT_ATTR(mask, "config:30-33");
static CCN_FORMAT_ATTR(cmp_l, "config1:0-62");
static CCN_FORMAT_ATTR(cmp_h, "config2:0-59");

static struct attribute *arm_ccn_pmu_format_attrs[] = {
	&arm_ccn_pmu_format_attr_node.attr.attr,
	&arm_ccn_pmu_format_attr_xp.attr.attr,
	&arm_ccn_pmu_format_attr_type.attr.attr,
	&arm_ccn_pmu_format_attr_event.attr.attr,
	&arm_ccn_pmu_format_attr_port.attr.attr,
	&arm_ccn_pmu_format_attr_bus.attr.attr,
	&arm_ccn_pmu_format_attr_vc.attr.attr,
	&arm_ccn_pmu_format_attr_dir.attr.attr,
	&arm_ccn_pmu_format_attr_mask.attr.attr,
	&arm_ccn_pmu_format_attr_cmp_l.attr.attr,
	&arm_ccn_pmu_format_attr_cmp_h.attr.attr,
	NULL
};

static struct attribute_group arm_ccn_pmu_format_attr_group = {
	.name = "format",
	.attrs = arm_ccn_pmu_format_attrs,
};


struct arm_ccn_pmu_event {
	struct device_attribute attr;
	u32 type;
	u32 event;
	int num_ports;
	int num_vcs;
	const char *def;
	int mask;
};

#define CCN_EVENT_ATTR(_name) \
	__ATTR(_name, S_IRUGO, arm_ccn_pmu_event_show, NULL)

/*
 * Events defined in TRM for MN, HN-I and SBSX are actually watchpoints set on
 * their ports in XP they are connected to. For the sake of usability they are
 * explicitly defined here (and translated into a relevant watchpoint in
 * arm_ccn_pmu_event_init()) so the user can easily request them without deep
 * knowledge of the flit format.
 */

#define CCN_EVENT_MN(_name, _def, _mask) { .attr = CCN_EVENT_ATTR(mn_##_name), \
		.type = CCN_TYPE_MN, .event = CCN_EVENT_WATCHPOINT, \
		.num_ports = CCN_NUM_XP_PORTS, .num_vcs = CCN_NUM_VCS, \
		.def = _def, .mask = _mask, }

#define CCN_EVENT_HNI(_name, _def, _mask) { \
		.attr = CCN_EVENT_ATTR(hni_##_name), .type = CCN_TYPE_HNI, \
		.event = CCN_EVENT_WATCHPOINT, .num_ports = CCN_NUM_XP_PORTS, \
		.num_vcs = CCN_NUM_VCS, .def = _def, .mask = _mask, }

#define CCN_EVENT_SBSX(_name, _def, _mask) { \
		.attr = CCN_EVENT_ATTR(sbsx_##_name), .type = CCN_TYPE_SBSX, \
		.event = CCN_EVENT_WATCHPOINT, .num_ports = CCN_NUM_XP_PORTS, \
		.num_vcs = CCN_NUM_VCS, .def = _def, .mask = _mask, }

#define CCN_EVENT_HNF(_name, _event) { .attr = CCN_EVENT_ATTR(hnf_##_name), \
		.type = CCN_TYPE_HNF, .event = _event, }

#define CCN_EVENT_XP(_name, _event) { .attr = CCN_EVENT_ATTR(xp_##_name), \
		.type = CCN_TYPE_XP, .event = _event, \
		.num_ports = CCN_NUM_XP_PORTS, .num_vcs = CCN_NUM_VCS, }

/*
 * RN-I & RN-D (RN-D = RN-I + DVM) nodes have different type ID depending
 * on configuration. One of them is picked to represent the whole group,
 * as they all share the same event types.
 */
#define CCN_EVENT_RNI(_name, _event) { .attr = CCN_EVENT_ATTR(rni_##_name), \
		.type = CCN_TYPE_RNI_3P, .event = _event, }

#define CCN_EVENT_SBAS(_name, _event) { .attr = CCN_EVENT_ATTR(sbas_##_name), \
		.type = CCN_TYPE_SBAS, .event = _event, }

#define CCN_EVENT_CYCLES(_name) { .attr = CCN_EVENT_ATTR(_name), \
		.type = CCN_TYPE_CYCLES }


static ssize_t arm_ccn_pmu_event_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(dev_get_drvdata(dev));
	struct arm_ccn_pmu_event *event = container_of(attr,
			struct arm_ccn_pmu_event, attr);
	ssize_t res;

	res = snprintf(buf, PAGE_SIZE, "type=0x%x", event->type);
	if (event->event)
		res += snprintf(buf + res, PAGE_SIZE - res, ",event=0x%x",
				event->event);
	if (event->def)
		res += snprintf(buf + res, PAGE_SIZE - res, ",%s",
				event->def);
	if (event->mask)
		res += snprintf(buf + res, PAGE_SIZE - res, ",mask=0x%x",
				event->mask);

	/* Arguments required by an event */
	switch (event->type) {
	case CCN_TYPE_CYCLES:
		break;
	case CCN_TYPE_XP:
		res += snprintf(buf + res, PAGE_SIZE - res,
				",xp=?,vc=?");
		if (event->event == CCN_EVENT_WATCHPOINT)
			res += snprintf(buf + res, PAGE_SIZE - res,
					",port=?,dir=?,cmp_l=?,cmp_h=?,mask=?");
		else
			res += snprintf(buf + res, PAGE_SIZE - res,
					",bus=?");

		break;
	case CCN_TYPE_MN:
		res += snprintf(buf + res, PAGE_SIZE - res, ",node=%d", ccn->mn_id);
		break;
	default:
		res += snprintf(buf + res, PAGE_SIZE - res, ",node=?");
		break;
	}

	res += snprintf(buf + res, PAGE_SIZE - res, "\n");

	return res;
}

static umode_t arm_ccn_pmu_events_is_visible(struct kobject *kobj,
				     struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct arm_ccn *ccn = pmu_to_arm_ccn(dev_get_drvdata(dev));
	struct device_attribute *dev_attr = container_of(attr,
			struct device_attribute, attr);
	struct arm_ccn_pmu_event *event = container_of(dev_attr,
			struct arm_ccn_pmu_event, attr);

	if (event->type == CCN_TYPE_SBAS && !ccn->sbas_present)
		return 0;
	if (event->type == CCN_TYPE_SBSX && !ccn->sbsx_present)
		return 0;

	return attr->mode;
}

static struct arm_ccn_pmu_event arm_ccn_pmu_events[] = {
	CCN_EVENT_MN(eobarrier, "dir=1,vc=0,cmp_h=0x1c00", CCN_IDX_MASK_OPCODE),
	CCN_EVENT_MN(ecbarrier, "dir=1,vc=0,cmp_h=0x1e00", CCN_IDX_MASK_OPCODE),
	CCN_EVENT_MN(dvmop, "dir=1,vc=0,cmp_h=0x2800", CCN_IDX_MASK_OPCODE),
	CCN_EVENT_HNI(txdatflits, "dir=1,vc=3", CCN_IDX_MASK_ANY),
	CCN_EVENT_HNI(rxdatflits, "dir=0,vc=3", CCN_IDX_MASK_ANY),
	CCN_EVENT_HNI(txreqflits, "dir=1,vc=0", CCN_IDX_MASK_ANY),
	CCN_EVENT_HNI(rxreqflits, "dir=0,vc=0", CCN_IDX_MASK_ANY),
	CCN_EVENT_HNI(rxreqflits_order, "dir=0,vc=0,cmp_h=0x8000",
			CCN_IDX_MASK_ORDER),
	CCN_EVENT_SBSX(txdatflits, "dir=1,vc=3", CCN_IDX_MASK_ANY),
	CCN_EVENT_SBSX(rxdatflits, "dir=0,vc=3", CCN_IDX_MASK_ANY),
	CCN_EVENT_SBSX(txreqflits, "dir=1,vc=0", CCN_IDX_MASK_ANY),
	CCN_EVENT_SBSX(rxreqflits, "dir=0,vc=0", CCN_IDX_MASK_ANY),
	CCN_EVENT_SBSX(rxreqflits_order, "dir=0,vc=0,cmp_h=0x8000",
			CCN_IDX_MASK_ORDER),
	CCN_EVENT_HNF(cache_miss, 0x1),
	CCN_EVENT_HNF(l3_sf_cache_access, 0x02),
	CCN_EVENT_HNF(cache_fill, 0x3),
	CCN_EVENT_HNF(pocq_retry, 0x4),
	CCN_EVENT_HNF(pocq_reqs_recvd, 0x5),
	CCN_EVENT_HNF(sf_hit, 0x6),
	CCN_EVENT_HNF(sf_evictions, 0x7),
	CCN_EVENT_HNF(snoops_sent, 0x8),
	CCN_EVENT_HNF(snoops_broadcast, 0x9),
	CCN_EVENT_HNF(l3_eviction, 0xa),
	CCN_EVENT_HNF(l3_fill_invalid_way, 0xb),
	CCN_EVENT_HNF(mc_retries, 0xc),
	CCN_EVENT_HNF(mc_reqs, 0xd),
	CCN_EVENT_HNF(qos_hh_retry, 0xe),
	CCN_EVENT_RNI(rdata_beats_p0, 0x1),
	CCN_EVENT_RNI(rdata_beats_p1, 0x2),
	CCN_EVENT_RNI(rdata_beats_p2, 0x3),
	CCN_EVENT_RNI(rxdat_flits, 0x4),
	CCN_EVENT_RNI(txdat_flits, 0x5),
	CCN_EVENT_RNI(txreq_flits, 0x6),
	CCN_EVENT_RNI(txreq_flits_retried, 0x7),
	CCN_EVENT_RNI(rrt_full, 0x8),
	CCN_EVENT_RNI(wrt_full, 0x9),
	CCN_EVENT_RNI(txreq_flits_replayed, 0xa),
	CCN_EVENT_XP(upload_starvation, 0x1),
	CCN_EVENT_XP(download_starvation, 0x2),
	CCN_EVENT_XP(respin, 0x3),
	CCN_EVENT_XP(valid_flit, 0x4),
	CCN_EVENT_XP(watchpoint, CCN_EVENT_WATCHPOINT),
	CCN_EVENT_SBAS(rdata_beats_p0, 0x1),
	CCN_EVENT_SBAS(rxdat_flits, 0x4),
	CCN_EVENT_SBAS(txdat_flits, 0x5),
	CCN_EVENT_SBAS(txreq_flits, 0x6),
	CCN_EVENT_SBAS(txreq_flits_retried, 0x7),
	CCN_EVENT_SBAS(rrt_full, 0x8),
	CCN_EVENT_SBAS(wrt_full, 0x9),
	CCN_EVENT_SBAS(txreq_flits_replayed, 0xa),
	CCN_EVENT_CYCLES(cycles),
};

/* Populated in arm_ccn_init() */
static struct attribute
		*arm_ccn_pmu_events_attrs[ARRAY_SIZE(arm_ccn_pmu_events) + 1];

static struct attribute_group arm_ccn_pmu_events_attr_group = {
	.name = "events",
	.is_visible = arm_ccn_pmu_events_is_visible,
	.attrs = arm_ccn_pmu_events_attrs,
};


static u64 *arm_ccn_pmu_get_cmp_mask(struct arm_ccn *ccn, const char *name)
{
	unsigned long i;

	if (WARN_ON(!name || !name[0] || !isxdigit(name[0]) || !name[1]))
		return NULL;
	i = isdigit(name[0]) ? name[0] - '0' : 0xa + tolower(name[0]) - 'a';

	switch (name[1]) {
	case 'l':
		return &ccn->dt.cmp_mask[i].l;
	case 'h':
		return &ccn->dt.cmp_mask[i].h;
	default:
		return NULL;
	}
}

static ssize_t arm_ccn_pmu_cmp_mask_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(dev_get_drvdata(dev));
	u64 *mask = arm_ccn_pmu_get_cmp_mask(ccn, attr->attr.name);

	return mask ? snprintf(buf, PAGE_SIZE, "0x%016llx\n", *mask) : -EINVAL;
}

static ssize_t arm_ccn_pmu_cmp_mask_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(dev_get_drvdata(dev));
	u64 *mask = arm_ccn_pmu_get_cmp_mask(ccn, attr->attr.name);
	int err = -EINVAL;

	if (mask)
		err = kstrtoull(buf, 0, mask);

	return err ? err : count;
}

#define CCN_CMP_MASK_ATTR(_name) \
	struct device_attribute arm_ccn_pmu_cmp_mask_attr_##_name = \
			__ATTR(_name, S_IRUGO | S_IWUSR, \
			arm_ccn_pmu_cmp_mask_show, arm_ccn_pmu_cmp_mask_store)

#define CCN_CMP_MASK_ATTR_RO(_name) \
	struct device_attribute arm_ccn_pmu_cmp_mask_attr_##_name = \
			__ATTR(_name, S_IRUGO, arm_ccn_pmu_cmp_mask_show, NULL)

static CCN_CMP_MASK_ATTR(0l);
static CCN_CMP_MASK_ATTR(0h);
static CCN_CMP_MASK_ATTR(1l);
static CCN_CMP_MASK_ATTR(1h);
static CCN_CMP_MASK_ATTR(2l);
static CCN_CMP_MASK_ATTR(2h);
static CCN_CMP_MASK_ATTR(3l);
static CCN_CMP_MASK_ATTR(3h);
static CCN_CMP_MASK_ATTR(4l);
static CCN_CMP_MASK_ATTR(4h);
static CCN_CMP_MASK_ATTR(5l);
static CCN_CMP_MASK_ATTR(5h);
static CCN_CMP_MASK_ATTR(6l);
static CCN_CMP_MASK_ATTR(6h);
static CCN_CMP_MASK_ATTR(7l);
static CCN_CMP_MASK_ATTR(7h);
static CCN_CMP_MASK_ATTR_RO(8l);
static CCN_CMP_MASK_ATTR_RO(8h);
static CCN_CMP_MASK_ATTR_RO(9l);
static CCN_CMP_MASK_ATTR_RO(9h);
static CCN_CMP_MASK_ATTR_RO(al);
static CCN_CMP_MASK_ATTR_RO(ah);
static CCN_CMP_MASK_ATTR_RO(bl);
static CCN_CMP_MASK_ATTR_RO(bh);

static struct attribute *arm_ccn_pmu_cmp_mask_attrs[] = {
	&arm_ccn_pmu_cmp_mask_attr_0l.attr, &arm_ccn_pmu_cmp_mask_attr_0h.attr,
	&arm_ccn_pmu_cmp_mask_attr_1l.attr, &arm_ccn_pmu_cmp_mask_attr_1h.attr,
	&arm_ccn_pmu_cmp_mask_attr_2l.attr, &arm_ccn_pmu_cmp_mask_attr_2h.attr,
	&arm_ccn_pmu_cmp_mask_attr_3l.attr, &arm_ccn_pmu_cmp_mask_attr_3h.attr,
	&arm_ccn_pmu_cmp_mask_attr_4l.attr, &arm_ccn_pmu_cmp_mask_attr_4h.attr,
	&arm_ccn_pmu_cmp_mask_attr_5l.attr, &arm_ccn_pmu_cmp_mask_attr_5h.attr,
	&arm_ccn_pmu_cmp_mask_attr_6l.attr, &arm_ccn_pmu_cmp_mask_attr_6h.attr,
	&arm_ccn_pmu_cmp_mask_attr_7l.attr, &arm_ccn_pmu_cmp_mask_attr_7h.attr,
	&arm_ccn_pmu_cmp_mask_attr_8l.attr, &arm_ccn_pmu_cmp_mask_attr_8h.attr,
	&arm_ccn_pmu_cmp_mask_attr_9l.attr, &arm_ccn_pmu_cmp_mask_attr_9h.attr,
	&arm_ccn_pmu_cmp_mask_attr_al.attr, &arm_ccn_pmu_cmp_mask_attr_ah.attr,
	&arm_ccn_pmu_cmp_mask_attr_bl.attr, &arm_ccn_pmu_cmp_mask_attr_bh.attr,
	NULL
};

static struct attribute_group arm_ccn_pmu_cmp_mask_attr_group = {
	.name = "cmp_mask",
	.attrs = arm_ccn_pmu_cmp_mask_attrs,
};

static ssize_t arm_ccn_pmu_cpumask_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, &ccn->dt.cpu);
}

static struct device_attribute arm_ccn_pmu_cpumask_attr =
		__ATTR(cpumask, S_IRUGO, arm_ccn_pmu_cpumask_show, NULL);

static struct attribute *arm_ccn_pmu_cpumask_attrs[] = {
	&arm_ccn_pmu_cpumask_attr.attr,
	NULL,
};

static struct attribute_group arm_ccn_pmu_cpumask_attr_group = {
	.attrs = arm_ccn_pmu_cpumask_attrs,
};

/*
 * Default poll period is 10ms, which is way over the top anyway,
 * as in the worst case scenario (an event every cycle), with 1GHz
 * clocked bus, the smallest, 32 bit counter will overflow in
 * more than 4s.
 */
static unsigned int arm_ccn_pmu_poll_period_us = 10000;
module_param_named(pmu_poll_period_us, arm_ccn_pmu_poll_period_us, uint,
		S_IRUGO | S_IWUSR);

static ktime_t arm_ccn_pmu_timer_period(void)
{
	return ns_to_ktime((u64)arm_ccn_pmu_poll_period_us * 1000);
}


static const struct attribute_group *arm_ccn_pmu_attr_groups[] = {
	&arm_ccn_pmu_events_attr_group,
	&arm_ccn_pmu_format_attr_group,
	&arm_ccn_pmu_cmp_mask_attr_group,
	&arm_ccn_pmu_cpumask_attr_group,
	NULL
};


static int arm_ccn_pmu_alloc_bit(unsigned long *bitmap, unsigned long size)
{
	int bit;

	do {
		bit = find_first_zero_bit(bitmap, size);
		if (bit >= size)
			return -EAGAIN;
	} while (test_and_set_bit(bit, bitmap));

	return bit;
}

/* All RN-I and RN-D nodes have identical PMUs */
static int arm_ccn_pmu_type_eq(u32 a, u32 b)
{
	if (a == b)
		return 1;

	switch (a) {
	case CCN_TYPE_RNI_1P:
	case CCN_TYPE_RNI_2P:
	case CCN_TYPE_RNI_3P:
	case CCN_TYPE_RND_1P:
	case CCN_TYPE_RND_2P:
	case CCN_TYPE_RND_3P:
		switch (b) {
		case CCN_TYPE_RNI_1P:
		case CCN_TYPE_RNI_2P:
		case CCN_TYPE_RNI_3P:
		case CCN_TYPE_RND_1P:
		case CCN_TYPE_RND_2P:
		case CCN_TYPE_RND_3P:
			return 1;
		}
		break;
	}

	return 0;
}

static int arm_ccn_pmu_event_alloc(struct perf_event *event)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;
	u32 node_xp, type, event_id;
	struct arm_ccn_component *source;
	int bit;

	node_xp = CCN_CONFIG_NODE(event->attr.config);
	type = CCN_CONFIG_TYPE(event->attr.config);
	event_id = CCN_CONFIG_EVENT(event->attr.config);

	/* Allocate the cycle counter */
	if (type == CCN_TYPE_CYCLES) {
		if (test_and_set_bit(CCN_IDX_PMU_CYCLE_COUNTER,
				ccn->dt.pmu_counters_mask))
			return -EAGAIN;

		hw->idx = CCN_IDX_PMU_CYCLE_COUNTER;
		ccn->dt.pmu_counters[CCN_IDX_PMU_CYCLE_COUNTER].event = event;

		return 0;
	}

	/* Allocate an event counter */
	hw->idx = arm_ccn_pmu_alloc_bit(ccn->dt.pmu_counters_mask,
			CCN_NUM_PMU_EVENT_COUNTERS);
	if (hw->idx < 0) {
		dev_dbg(ccn->dev, "No more counters available!\n");
		return -EAGAIN;
	}

	if (type == CCN_TYPE_XP)
		source = &ccn->xp[node_xp];
	else
		source = &ccn->node[node_xp];
	ccn->dt.pmu_counters[hw->idx].source = source;

	/* Allocate an event source or a watchpoint */
	if (type == CCN_TYPE_XP && event_id == CCN_EVENT_WATCHPOINT)
		bit = arm_ccn_pmu_alloc_bit(source->xp.dt_cmp_mask,
				CCN_NUM_XP_WATCHPOINTS);
	else
		bit = arm_ccn_pmu_alloc_bit(source->pmu_events_mask,
				CCN_NUM_PMU_EVENTS);
	if (bit < 0) {
		dev_dbg(ccn->dev, "No more event sources/watchpoints on node/XP %d!\n",
				node_xp);
		clear_bit(hw->idx, ccn->dt.pmu_counters_mask);
		return -EAGAIN;
	}
	hw->config_base = bit;

	ccn->dt.pmu_counters[hw->idx].event = event;

	return 0;
}

static void arm_ccn_pmu_event_release(struct perf_event *event)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;

	if (hw->idx == CCN_IDX_PMU_CYCLE_COUNTER) {
		clear_bit(CCN_IDX_PMU_CYCLE_COUNTER, ccn->dt.pmu_counters_mask);
	} else {
		struct arm_ccn_component *source =
				ccn->dt.pmu_counters[hw->idx].source;

		if (CCN_CONFIG_TYPE(event->attr.config) == CCN_TYPE_XP &&
				CCN_CONFIG_EVENT(event->attr.config) ==
				CCN_EVENT_WATCHPOINT)
			clear_bit(hw->config_base, source->xp.dt_cmp_mask);
		else
			clear_bit(hw->config_base, source->pmu_events_mask);
		clear_bit(hw->idx, ccn->dt.pmu_counters_mask);
	}

	ccn->dt.pmu_counters[hw->idx].source = NULL;
	ccn->dt.pmu_counters[hw->idx].event = NULL;
}

static int arm_ccn_pmu_event_init(struct perf_event *event)
{
	struct arm_ccn *ccn;
	struct hw_perf_event *hw = &event->hw;
	u32 node_xp, type, event_id;
	int valid;
	int i;
	struct perf_event *sibling;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	ccn = pmu_to_arm_ccn(event->pmu);

	if (hw->sample_period) {
		dev_warn(ccn->dev, "Sampling not supported!\n");
		return -EOPNOTSUPP;
	}

	if (has_branch_stack(event) || event->attr.exclude_user ||
			event->attr.exclude_kernel || event->attr.exclude_hv ||
			event->attr.exclude_idle || event->attr.exclude_host ||
			event->attr.exclude_guest) {
		dev_warn(ccn->dev, "Can't exclude execution levels!\n");
		return -EINVAL;
	}

	if (event->cpu < 0) {
		dev_warn(ccn->dev, "Can't provide per-task data!\n");
		return -EOPNOTSUPP;
	}
	/*
	 * Many perf core operations (eg. events rotation) operate on a
	 * single CPU context. This is obvious for CPU PMUs, where one
	 * expects the same sets of events being observed on all CPUs,
	 * but can lead to issues for off-core PMUs, like CCN, where each
	 * event could be theoretically assigned to a different CPU. To
	 * mitigate this, we enforce CPU assignment to one, selected
	 * processor (the one described in the "cpumask" attribute).
	 */
	event->cpu = cpumask_first(&ccn->dt.cpu);

	node_xp = CCN_CONFIG_NODE(event->attr.config);
	type = CCN_CONFIG_TYPE(event->attr.config);
	event_id = CCN_CONFIG_EVENT(event->attr.config);

	/* Validate node/xp vs topology */
	switch (type) {
	case CCN_TYPE_MN:
		if (node_xp != ccn->mn_id) {
			dev_warn(ccn->dev, "Invalid MN ID %d!\n", node_xp);
			return -EINVAL;
		}
		break;
	case CCN_TYPE_XP:
		if (node_xp >= ccn->num_xps) {
			dev_warn(ccn->dev, "Invalid XP ID %d!\n", node_xp);
			return -EINVAL;
		}
		break;
	case CCN_TYPE_CYCLES:
		break;
	default:
		if (node_xp >= ccn->num_nodes) {
			dev_warn(ccn->dev, "Invalid node ID %d!\n", node_xp);
			return -EINVAL;
		}
		if (!arm_ccn_pmu_type_eq(type, ccn->node[node_xp].type)) {
			dev_warn(ccn->dev, "Invalid type 0x%x for node %d!\n",
					type, node_xp);
			return -EINVAL;
		}
		break;
	}

	/* Validate event ID vs available for the type */
	for (i = 0, valid = 0; i < ARRAY_SIZE(arm_ccn_pmu_events) && !valid;
			i++) {
		struct arm_ccn_pmu_event *e = &arm_ccn_pmu_events[i];
		u32 port = CCN_CONFIG_PORT(event->attr.config);
		u32 vc = CCN_CONFIG_VC(event->attr.config);

		if (!arm_ccn_pmu_type_eq(type, e->type))
			continue;
		if (event_id != e->event)
			continue;
		if (e->num_ports && port >= e->num_ports) {
			dev_warn(ccn->dev, "Invalid port %d for node/XP %d!\n",
					port, node_xp);
			return -EINVAL;
		}
		if (e->num_vcs && vc >= e->num_vcs) {
			dev_warn(ccn->dev, "Invalid vc %d for node/XP %d!\n",
					vc, node_xp);
			return -EINVAL;
		}
		valid = 1;
	}
	if (!valid) {
		dev_warn(ccn->dev, "Invalid event 0x%x for node/XP %d!\n",
				event_id, node_xp);
		return -EINVAL;
	}

	/* Watchpoint-based event for a node is actually set on XP */
	if (event_id == CCN_EVENT_WATCHPOINT && type != CCN_TYPE_XP) {
		u32 port;

		type = CCN_TYPE_XP;
		port = arm_ccn_node_to_xp_port(node_xp);
		node_xp = arm_ccn_node_to_xp(node_xp);

		arm_ccn_pmu_config_set(&event->attr.config,
				node_xp, type, port);
	}

	/*
	 * We must NOT create groups containing mixed PMUs, although software
	 * events are acceptable (for example to create a CCN group
	 * periodically read when a hrtimer aka cpu-clock leader triggers).
	 */
	if (event->group_leader->pmu != event->pmu &&
			!is_software_event(event->group_leader))
		return -EINVAL;

	list_for_each_entry(sibling, &event->group_leader->sibling_list,
			group_entry)
		if (sibling->pmu != event->pmu &&
				!is_software_event(sibling))
			return -EINVAL;

	return 0;
}

static u64 arm_ccn_pmu_read_counter(struct arm_ccn *ccn, int idx)
{
	u64 res;

	if (idx == CCN_IDX_PMU_CYCLE_COUNTER) {
#ifdef readq
		res = readq(ccn->dt.base + CCN_DT_PMCCNTR);
#else
		/* 40 bit counter, can do snapshot and read in two parts */
		writel(0x1, ccn->dt.base + CCN_DT_PMSR_REQ);
		while (!(readl(ccn->dt.base + CCN_DT_PMSR) & 0x1))
			;
		writel(0x1, ccn->dt.base + CCN_DT_PMSR_CLR);
		res = readl(ccn->dt.base + CCN_DT_PMCCNTRSR + 4) & 0xff;
		res <<= 32;
		res |= readl(ccn->dt.base + CCN_DT_PMCCNTRSR);
#endif
	} else {
		res = readl(ccn->dt.base + CCN_DT_PMEVCNT(idx));
	}

	return res;
}

static void arm_ccn_pmu_event_update(struct perf_event *event)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;
	u64 prev_count, new_count, mask;

	do {
		prev_count = local64_read(&hw->prev_count);
		new_count = arm_ccn_pmu_read_counter(ccn, hw->idx);
	} while (local64_xchg(&hw->prev_count, new_count) != prev_count);

	mask = (1LLU << (hw->idx == CCN_IDX_PMU_CYCLE_COUNTER ? 40 : 32)) - 1;

	local64_add((new_count - prev_count) & mask, &event->count);
}

static void arm_ccn_pmu_xp_dt_config(struct perf_event *event, int enable)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;
	struct arm_ccn_component *xp;
	u32 val, dt_cfg;

	/* Nothing to do for cycle counter */
	if (hw->idx == CCN_IDX_PMU_CYCLE_COUNTER)
		return;

	if (CCN_CONFIG_TYPE(event->attr.config) == CCN_TYPE_XP)
		xp = &ccn->xp[CCN_CONFIG_XP(event->attr.config)];
	else
		xp = &ccn->xp[arm_ccn_node_to_xp(
				CCN_CONFIG_NODE(event->attr.config))];

	if (enable)
		dt_cfg = hw->event_base;
	else
		dt_cfg = CCN_XP_DT_CONFIG__DT_CFG__PASS_THROUGH;

	spin_lock(&ccn->dt.config_lock);

	val = readl(xp->base + CCN_XP_DT_CONFIG);
	val &= ~(CCN_XP_DT_CONFIG__DT_CFG__MASK <<
			CCN_XP_DT_CONFIG__DT_CFG__SHIFT(hw->idx));
	val |= dt_cfg << CCN_XP_DT_CONFIG__DT_CFG__SHIFT(hw->idx);
	writel(val, xp->base + CCN_XP_DT_CONFIG);

	spin_unlock(&ccn->dt.config_lock);
}

static void arm_ccn_pmu_event_start(struct perf_event *event, int flags)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;

	local64_set(&event->hw.prev_count,
			arm_ccn_pmu_read_counter(ccn, hw->idx));
	hw->state = 0;

	/* Set the DT bus input, engaging the counter */
	arm_ccn_pmu_xp_dt_config(event, 1);
}

static void arm_ccn_pmu_event_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hw = &event->hw;

	/* Disable counting, setting the DT bus to pass-through mode */
	arm_ccn_pmu_xp_dt_config(event, 0);

	if (flags & PERF_EF_UPDATE)
		arm_ccn_pmu_event_update(event);

	hw->state |= PERF_HES_STOPPED;
}

static void arm_ccn_pmu_xp_watchpoint_config(struct perf_event *event)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;
	struct arm_ccn_component *source =
			ccn->dt.pmu_counters[hw->idx].source;
	unsigned long wp = hw->config_base;
	u32 val;
	u64 cmp_l = event->attr.config1;
	u64 cmp_h = event->attr.config2;
	u64 mask_l = ccn->dt.cmp_mask[CCN_CONFIG_MASK(event->attr.config)].l;
	u64 mask_h = ccn->dt.cmp_mask[CCN_CONFIG_MASK(event->attr.config)].h;

	hw->event_base = CCN_XP_DT_CONFIG__DT_CFG__WATCHPOINT(wp);

	/* Direction (RX/TX), device (port) & virtual channel */
	val = readl(source->base + CCN_XP_DT_INTERFACE_SEL);
	val &= ~(CCN_XP_DT_INTERFACE_SEL__DT_IO_SEL__MASK <<
			CCN_XP_DT_INTERFACE_SEL__DT_IO_SEL__SHIFT(wp));
	val |= CCN_CONFIG_DIR(event->attr.config) <<
			CCN_XP_DT_INTERFACE_SEL__DT_IO_SEL__SHIFT(wp);
	val &= ~(CCN_XP_DT_INTERFACE_SEL__DT_DEV_SEL__MASK <<
			CCN_XP_DT_INTERFACE_SEL__DT_DEV_SEL__SHIFT(wp));
	val |= CCN_CONFIG_PORT(event->attr.config) <<
			CCN_XP_DT_INTERFACE_SEL__DT_DEV_SEL__SHIFT(wp);
	val &= ~(CCN_XP_DT_INTERFACE_SEL__DT_VC_SEL__MASK <<
			CCN_XP_DT_INTERFACE_SEL__DT_VC_SEL__SHIFT(wp));
	val |= CCN_CONFIG_VC(event->attr.config) <<
			CCN_XP_DT_INTERFACE_SEL__DT_VC_SEL__SHIFT(wp);
	writel(val, source->base + CCN_XP_DT_INTERFACE_SEL);

	/* Comparison values */
	writel(cmp_l & 0xffffffff, source->base + CCN_XP_DT_CMP_VAL_L(wp));
	writel((cmp_l >> 32) & 0x7fffffff,
			source->base + CCN_XP_DT_CMP_VAL_L(wp) + 4);
	writel(cmp_h & 0xffffffff, source->base + CCN_XP_DT_CMP_VAL_H(wp));
	writel((cmp_h >> 32) & 0x0fffffff,
			source->base + CCN_XP_DT_CMP_VAL_H(wp) + 4);

	/* Mask */
	writel(mask_l & 0xffffffff, source->base + CCN_XP_DT_CMP_MASK_L(wp));
	writel((mask_l >> 32) & 0x7fffffff,
			source->base + CCN_XP_DT_CMP_MASK_L(wp) + 4);
	writel(mask_h & 0xffffffff, source->base + CCN_XP_DT_CMP_MASK_H(wp));
	writel((mask_h >> 32) & 0x0fffffff,
			source->base + CCN_XP_DT_CMP_MASK_H(wp) + 4);
}

static void arm_ccn_pmu_xp_event_config(struct perf_event *event)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;
	struct arm_ccn_component *source =
			ccn->dt.pmu_counters[hw->idx].source;
	u32 val, id;

	hw->event_base = CCN_XP_DT_CONFIG__DT_CFG__XP_PMU_EVENT(hw->config_base);

	id = (CCN_CONFIG_VC(event->attr.config) << 4) |
			(CCN_CONFIG_BUS(event->attr.config) << 3) |
			(CCN_CONFIG_EVENT(event->attr.config) << 0);

	val = readl(source->base + CCN_XP_PMU_EVENT_SEL);
	val &= ~(CCN_XP_PMU_EVENT_SEL__ID__MASK <<
			CCN_XP_PMU_EVENT_SEL__ID__SHIFT(hw->config_base));
	val |= id << CCN_XP_PMU_EVENT_SEL__ID__SHIFT(hw->config_base);
	writel(val, source->base + CCN_XP_PMU_EVENT_SEL);
}

static void arm_ccn_pmu_node_event_config(struct perf_event *event)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;
	struct arm_ccn_component *source =
			ccn->dt.pmu_counters[hw->idx].source;
	u32 type = CCN_CONFIG_TYPE(event->attr.config);
	u32 val, port;

	port = arm_ccn_node_to_xp_port(CCN_CONFIG_NODE(event->attr.config));
	hw->event_base = CCN_XP_DT_CONFIG__DT_CFG__DEVICE_PMU_EVENT(port,
			hw->config_base);

	/* These *_event_sel regs should be identical, but let's make sure... */
	BUILD_BUG_ON(CCN_HNF_PMU_EVENT_SEL != CCN_SBAS_PMU_EVENT_SEL);
	BUILD_BUG_ON(CCN_SBAS_PMU_EVENT_SEL != CCN_RNI_PMU_EVENT_SEL);
	BUILD_BUG_ON(CCN_HNF_PMU_EVENT_SEL__ID__SHIFT(1) !=
			CCN_SBAS_PMU_EVENT_SEL__ID__SHIFT(1));
	BUILD_BUG_ON(CCN_SBAS_PMU_EVENT_SEL__ID__SHIFT(1) !=
			CCN_RNI_PMU_EVENT_SEL__ID__SHIFT(1));
	BUILD_BUG_ON(CCN_HNF_PMU_EVENT_SEL__ID__MASK !=
			CCN_SBAS_PMU_EVENT_SEL__ID__MASK);
	BUILD_BUG_ON(CCN_SBAS_PMU_EVENT_SEL__ID__MASK !=
			CCN_RNI_PMU_EVENT_SEL__ID__MASK);
	if (WARN_ON(type != CCN_TYPE_HNF && type != CCN_TYPE_SBAS &&
			!arm_ccn_pmu_type_eq(type, CCN_TYPE_RNI_3P)))
		return;

	/* Set the event id for the pre-allocated counter */
	val = readl(source->base + CCN_HNF_PMU_EVENT_SEL);
	val &= ~(CCN_HNF_PMU_EVENT_SEL__ID__MASK <<
		CCN_HNF_PMU_EVENT_SEL__ID__SHIFT(hw->config_base));
	val |= CCN_CONFIG_EVENT(event->attr.config) <<
		CCN_HNF_PMU_EVENT_SEL__ID__SHIFT(hw->config_base);
	writel(val, source->base + CCN_HNF_PMU_EVENT_SEL);
}

static void arm_ccn_pmu_event_config(struct perf_event *event)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);
	struct hw_perf_event *hw = &event->hw;
	u32 xp, offset, val;

	/* Cycle counter requires no setup */
	if (hw->idx == CCN_IDX_PMU_CYCLE_COUNTER)
		return;

	if (CCN_CONFIG_TYPE(event->attr.config) == CCN_TYPE_XP)
		xp = CCN_CONFIG_XP(event->attr.config);
	else
		xp = arm_ccn_node_to_xp(CCN_CONFIG_NODE(event->attr.config));

	spin_lock(&ccn->dt.config_lock);

	/* Set the DT bus "distance" register */
	offset = (hw->idx / 4) * 4;
	val = readl(ccn->dt.base + CCN_DT_ACTIVE_DSM + offset);
	val &= ~(CCN_DT_ACTIVE_DSM__DSM_ID__MASK <<
			CCN_DT_ACTIVE_DSM__DSM_ID__SHIFT(hw->idx % 4));
	val |= xp << CCN_DT_ACTIVE_DSM__DSM_ID__SHIFT(hw->idx % 4);
	writel(val, ccn->dt.base + CCN_DT_ACTIVE_DSM + offset);

	if (CCN_CONFIG_TYPE(event->attr.config) == CCN_TYPE_XP) {
		if (CCN_CONFIG_EVENT(event->attr.config) ==
				CCN_EVENT_WATCHPOINT)
			arm_ccn_pmu_xp_watchpoint_config(event);
		else
			arm_ccn_pmu_xp_event_config(event);
	} else {
		arm_ccn_pmu_node_event_config(event);
	}

	spin_unlock(&ccn->dt.config_lock);
}

static int arm_ccn_pmu_active_counters(struct arm_ccn *ccn)
{
	return bitmap_weight(ccn->dt.pmu_counters_mask,
			     CCN_NUM_PMU_EVENT_COUNTERS + 1);
}

static int arm_ccn_pmu_event_add(struct perf_event *event, int flags)
{
	int err;
	struct hw_perf_event *hw = &event->hw;
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);

	err = arm_ccn_pmu_event_alloc(event);
	if (err)
		return err;

	/*
	 * Pin the timer, so that the overflows are handled by the chosen
	 * event->cpu (this is the same one as presented in "cpumask"
	 * attribute).
	 */
	if (!ccn->irq && arm_ccn_pmu_active_counters(ccn) == 1)
		hrtimer_start(&ccn->dt.hrtimer, arm_ccn_pmu_timer_period(),
			      HRTIMER_MODE_REL_PINNED);

	arm_ccn_pmu_event_config(event);

	hw->state = PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		arm_ccn_pmu_event_start(event, PERF_EF_UPDATE);

	return 0;
}

static void arm_ccn_pmu_event_del(struct perf_event *event, int flags)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(event->pmu);

	arm_ccn_pmu_event_stop(event, PERF_EF_UPDATE);

	arm_ccn_pmu_event_release(event);

	if (!ccn->irq && arm_ccn_pmu_active_counters(ccn) == 0)
		hrtimer_cancel(&ccn->dt.hrtimer);
}

static void arm_ccn_pmu_event_read(struct perf_event *event)
{
	arm_ccn_pmu_event_update(event);
}

static void arm_ccn_pmu_enable(struct pmu *pmu)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(pmu);

	u32 val = readl(ccn->dt.base + CCN_DT_PMCR);
	val |= CCN_DT_PMCR__PMU_EN;
	writel(val, ccn->dt.base + CCN_DT_PMCR);
}

static void arm_ccn_pmu_disable(struct pmu *pmu)
{
	struct arm_ccn *ccn = pmu_to_arm_ccn(pmu);

	u32 val = readl(ccn->dt.base + CCN_DT_PMCR);
	val &= ~CCN_DT_PMCR__PMU_EN;
	writel(val, ccn->dt.base + CCN_DT_PMCR);
}

static irqreturn_t arm_ccn_pmu_overflow_handler(struct arm_ccn_dt *dt)
{
	u32 pmovsr = readl(dt->base + CCN_DT_PMOVSR);
	int idx;

	if (!pmovsr)
		return IRQ_NONE;

	writel(pmovsr, dt->base + CCN_DT_PMOVSR_CLR);

	BUILD_BUG_ON(CCN_IDX_PMU_CYCLE_COUNTER != CCN_NUM_PMU_EVENT_COUNTERS);

	for (idx = 0; idx < CCN_NUM_PMU_EVENT_COUNTERS + 1; idx++) {
		struct perf_event *event = dt->pmu_counters[idx].event;
		int overflowed = pmovsr & BIT(idx);

		WARN_ON_ONCE(overflowed && !event &&
				idx != CCN_IDX_PMU_CYCLE_COUNTER);

		if (!event || !overflowed)
			continue;

		arm_ccn_pmu_event_update(event);
	}

	return IRQ_HANDLED;
}

static enum hrtimer_restart arm_ccn_pmu_timer_handler(struct hrtimer *hrtimer)
{
	struct arm_ccn_dt *dt = container_of(hrtimer, struct arm_ccn_dt,
			hrtimer);
	unsigned long flags;

	local_irq_save(flags);
	arm_ccn_pmu_overflow_handler(dt);
	local_irq_restore(flags);

	hrtimer_forward_now(hrtimer, arm_ccn_pmu_timer_period());
	return HRTIMER_RESTART;
}


static int arm_ccn_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct arm_ccn_dt *dt = hlist_entry_safe(node, struct arm_ccn_dt, node);
	struct arm_ccn *ccn = container_of(dt, struct arm_ccn, dt);
	unsigned int target;

	if (!cpumask_test_and_clear_cpu(cpu, &dt->cpu))
		return 0;
	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;
	perf_pmu_migrate_context(&dt->pmu, cpu, target);
	cpumask_set_cpu(target, &dt->cpu);
	if (ccn->irq)
		WARN_ON(irq_set_affinity_hint(ccn->irq, &dt->cpu) != 0);
	return 0;
}

static DEFINE_IDA(arm_ccn_pmu_ida);

static int arm_ccn_pmu_init(struct arm_ccn *ccn)
{
	int i;
	char *name;
	int err;

	/* Initialize DT subsystem */
	ccn->dt.base = ccn->base + CCN_REGION_SIZE;
	spin_lock_init(&ccn->dt.config_lock);
	writel(CCN_DT_PMOVSR_CLR__MASK, ccn->dt.base + CCN_DT_PMOVSR_CLR);
	writel(CCN_DT_CTL__DT_EN, ccn->dt.base + CCN_DT_CTL);
	writel(CCN_DT_PMCR__OVFL_INTR_EN | CCN_DT_PMCR__PMU_EN,
			ccn->dt.base + CCN_DT_PMCR);
	writel(0x1, ccn->dt.base + CCN_DT_PMSR_CLR);
	for (i = 0; i < ccn->num_xps; i++) {
		writel(0, ccn->xp[i].base + CCN_XP_DT_CONFIG);
		writel((CCN_XP_DT_CONTROL__WP_ARM_SEL__ALWAYS <<
				CCN_XP_DT_CONTROL__WP_ARM_SEL__SHIFT(0)) |
				(CCN_XP_DT_CONTROL__WP_ARM_SEL__ALWAYS <<
				CCN_XP_DT_CONTROL__WP_ARM_SEL__SHIFT(1)) |
				CCN_XP_DT_CONTROL__DT_ENABLE,
				ccn->xp[i].base + CCN_XP_DT_CONTROL);
	}
	ccn->dt.cmp_mask[CCN_IDX_MASK_ANY].l = ~0;
	ccn->dt.cmp_mask[CCN_IDX_MASK_ANY].h = ~0;
	ccn->dt.cmp_mask[CCN_IDX_MASK_EXACT].l = 0;
	ccn->dt.cmp_mask[CCN_IDX_MASK_EXACT].h = 0;
	ccn->dt.cmp_mask[CCN_IDX_MASK_ORDER].l = ~0;
	ccn->dt.cmp_mask[CCN_IDX_MASK_ORDER].h = ~(0x1 << 15);
	ccn->dt.cmp_mask[CCN_IDX_MASK_OPCODE].l = ~0;
	ccn->dt.cmp_mask[CCN_IDX_MASK_OPCODE].h = ~(0x1f << 9);

	/* Get a convenient /sys/event_source/devices/ name */
	ccn->dt.id = ida_simple_get(&arm_ccn_pmu_ida, 0, 0, GFP_KERNEL);
	if (ccn->dt.id == 0) {
		name = "ccn";
	} else {
		int len = snprintf(NULL, 0, "ccn_%d", ccn->dt.id);

		name = devm_kzalloc(ccn->dev, len + 1, GFP_KERNEL);
		snprintf(name, len + 1, "ccn_%d", ccn->dt.id);
	}

	/* Perf driver registration */
	ccn->dt.pmu = (struct pmu) {
		.attr_groups = arm_ccn_pmu_attr_groups,
		.task_ctx_nr = perf_invalid_context,
		.event_init = arm_ccn_pmu_event_init,
		.add = arm_ccn_pmu_event_add,
		.del = arm_ccn_pmu_event_del,
		.start = arm_ccn_pmu_event_start,
		.stop = arm_ccn_pmu_event_stop,
		.read = arm_ccn_pmu_event_read,
		.pmu_enable = arm_ccn_pmu_enable,
		.pmu_disable = arm_ccn_pmu_disable,
	};

	/* No overflow interrupt? Have to use a timer instead. */
	if (!ccn->irq) {
		dev_info(ccn->dev, "No access to interrupts, using timer.\n");
		hrtimer_init(&ccn->dt.hrtimer, CLOCK_MONOTONIC,
				HRTIMER_MODE_REL);
		ccn->dt.hrtimer.function = arm_ccn_pmu_timer_handler;
	}

	/* Pick one CPU which we will use to collect data from CCN... */
	cpumask_set_cpu(smp_processor_id(), &ccn->dt.cpu);

	/* Also make sure that the overflow interrupt is handled by this CPU */
	if (ccn->irq) {
		err = irq_set_affinity_hint(ccn->irq, &ccn->dt.cpu);
		if (err) {
			dev_err(ccn->dev, "Failed to set interrupt affinity!\n");
			goto error_set_affinity;
		}
	}

	err = perf_pmu_register(&ccn->dt.pmu, name, -1);
	if (err)
		goto error_pmu_register;

	cpuhp_state_add_instance_nocalls(CPUHP_AP_PERF_ARM_CCN_ONLINE,
					 &ccn->dt.node);
	return 0;

error_pmu_register:
error_set_affinity:
	ida_simple_remove(&arm_ccn_pmu_ida, ccn->dt.id);
	for (i = 0; i < ccn->num_xps; i++)
		writel(0, ccn->xp[i].base + CCN_XP_DT_CONTROL);
	writel(0, ccn->dt.base + CCN_DT_PMCR);
	return err;
}

static void arm_ccn_pmu_cleanup(struct arm_ccn *ccn)
{
	int i;

	cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_CCN_ONLINE,
					    &ccn->dt.node);
	if (ccn->irq)
		irq_set_affinity_hint(ccn->irq, NULL);
	for (i = 0; i < ccn->num_xps; i++)
		writel(0, ccn->xp[i].base + CCN_XP_DT_CONTROL);
	writel(0, ccn->dt.base + CCN_DT_PMCR);
	perf_pmu_unregister(&ccn->dt.pmu);
	ida_simple_remove(&arm_ccn_pmu_ida, ccn->dt.id);
}

static int arm_ccn_for_each_valid_region(struct arm_ccn *ccn,
		int (*callback)(struct arm_ccn *ccn, int region,
		void __iomem *base, u32 type, u32 id))
{
	int region;

	for (region = 0; region < CCN_NUM_REGIONS; region++) {
		u32 val, type, id;
		void __iomem *base;
		int err;

		val = readl(ccn->base + CCN_MN_OLY_COMP_LIST_63_0 +
				4 * (region / 32));
		if (!(val & (1 << (region % 32))))
			continue;

		base = ccn->base + region * CCN_REGION_SIZE;
		val = readl(base + CCN_ALL_OLY_ID);
		type = (val >> CCN_ALL_OLY_ID__OLY_ID__SHIFT) &
				CCN_ALL_OLY_ID__OLY_ID__MASK;
		id = (val >> CCN_ALL_OLY_ID__NODE_ID__SHIFT) &
				CCN_ALL_OLY_ID__NODE_ID__MASK;

		err = callback(ccn, region, base, type, id);
		if (err)
			return err;
	}

	return 0;
}

static int arm_ccn_get_nodes_num(struct arm_ccn *ccn, int region,
		void __iomem *base, u32 type, u32 id)
{

	if (type == CCN_TYPE_XP && id >= ccn->num_xps)
		ccn->num_xps = id + 1;
	else if (id >= ccn->num_nodes)
		ccn->num_nodes = id + 1;

	return 0;
}

static int arm_ccn_init_nodes(struct arm_ccn *ccn, int region,
		void __iomem *base, u32 type, u32 id)
{
	struct arm_ccn_component *component;

	dev_dbg(ccn->dev, "Region %d: id=%u, type=0x%02x\n", region, id, type);

	switch (type) {
	case CCN_TYPE_MN:
		ccn->mn_id = id;
		return 0;
	case CCN_TYPE_DT:
		return 0;
	case CCN_TYPE_XP:
		component = &ccn->xp[id];
		break;
	case CCN_TYPE_SBSX:
		ccn->sbsx_present = 1;
		component = &ccn->node[id];
		break;
	case CCN_TYPE_SBAS:
		ccn->sbas_present = 1;
		/* Fall-through */
	default:
		component = &ccn->node[id];
		break;
	}

	component->base = base;
	component->type = type;

	return 0;
}


static irqreturn_t arm_ccn_error_handler(struct arm_ccn *ccn,
		const u32 *err_sig_val)
{
	/* This should be really handled by firmware... */
	dev_err(ccn->dev, "Error reported in %08x%08x%08x%08x%08x%08x.\n",
			err_sig_val[5], err_sig_val[4], err_sig_val[3],
			err_sig_val[2], err_sig_val[1], err_sig_val[0]);
	dev_err(ccn->dev, "Disabling interrupt generation for all errors.\n");
	writel(CCN_MN_ERRINT_STATUS__ALL_ERRORS__DISABLE,
			ccn->base + CCN_MN_ERRINT_STATUS);

	return IRQ_HANDLED;
}


static irqreturn_t arm_ccn_irq_handler(int irq, void *dev_id)
{
	irqreturn_t res = IRQ_NONE;
	struct arm_ccn *ccn = dev_id;
	u32 err_sig_val[6];
	u32 err_or;
	int i;

	/* PMU overflow is a special case */
	err_or = err_sig_val[0] = readl(ccn->base + CCN_MN_ERR_SIG_VAL_63_0);
	if (err_or & CCN_MN_ERR_SIG_VAL_63_0__DT) {
		err_or &= ~CCN_MN_ERR_SIG_VAL_63_0__DT;
		res = arm_ccn_pmu_overflow_handler(&ccn->dt);
	}

	/* Have to read all err_sig_vals to clear them */
	for (i = 1; i < ARRAY_SIZE(err_sig_val); i++) {
		err_sig_val[i] = readl(ccn->base +
				CCN_MN_ERR_SIG_VAL_63_0 + i * 4);
		err_or |= err_sig_val[i];
	}
	if (err_or)
		res |= arm_ccn_error_handler(ccn, err_sig_val);

	if (res != IRQ_NONE)
		writel(CCN_MN_ERRINT_STATUS__INTREQ__DESSERT,
				ccn->base + CCN_MN_ERRINT_STATUS);

	return res;
}


static int arm_ccn_probe(struct platform_device *pdev)
{
	struct arm_ccn *ccn;
	struct resource *res;
	unsigned int irq;
	int err;

	ccn = devm_kzalloc(&pdev->dev, sizeof(*ccn), GFP_KERNEL);
	if (!ccn)
		return -ENOMEM;
	ccn->dev = &pdev->dev;
	platform_set_drvdata(pdev, ccn);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	if (!devm_request_mem_region(ccn->dev, res->start,
			resource_size(res), pdev->name))
		return -EBUSY;

	ccn->base = devm_ioremap(ccn->dev, res->start,
				resource_size(res));
	if (!ccn->base)
		return -EFAULT;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res)
		return -EINVAL;
	irq = res->start;

	/* Check if we can use the interrupt */
	writel(CCN_MN_ERRINT_STATUS__PMU_EVENTS__DISABLE,
			ccn->base + CCN_MN_ERRINT_STATUS);
	if (readl(ccn->base + CCN_MN_ERRINT_STATUS) &
			CCN_MN_ERRINT_STATUS__PMU_EVENTS__DISABLED) {
		/* Can set 'disable' bits, so can acknowledge interrupts */
		writel(CCN_MN_ERRINT_STATUS__PMU_EVENTS__ENABLE,
				ccn->base + CCN_MN_ERRINT_STATUS);
		err = devm_request_irq(ccn->dev, irq, arm_ccn_irq_handler,
				       IRQF_NOBALANCING | IRQF_NO_THREAD,
				       dev_name(ccn->dev), ccn);
		if (err)
			return err;

		ccn->irq = irq;
	}


	/* Build topology */

	err = arm_ccn_for_each_valid_region(ccn, arm_ccn_get_nodes_num);
	if (err)
		return err;

	ccn->node = devm_kzalloc(ccn->dev, sizeof(*ccn->node) * ccn->num_nodes,
		GFP_KERNEL);
	ccn->xp = devm_kzalloc(ccn->dev, sizeof(*ccn->node) * ccn->num_xps,
		GFP_KERNEL);
	if (!ccn->node || !ccn->xp)
		return -ENOMEM;

	err = arm_ccn_for_each_valid_region(ccn, arm_ccn_init_nodes);
	if (err)
		return err;

	return arm_ccn_pmu_init(ccn);
}

static int arm_ccn_remove(struct platform_device *pdev)
{
	struct arm_ccn *ccn = platform_get_drvdata(pdev);

	arm_ccn_pmu_cleanup(ccn);

	return 0;
}

static const struct of_device_id arm_ccn_match[] = {
	{ .compatible = "arm,ccn-504", },
	{},
};

static struct platform_driver arm_ccn_driver = {
	.driver = {
		.name = "arm-ccn",
		.of_match_table = arm_ccn_match,
	},
	.probe = arm_ccn_probe,
	.remove = arm_ccn_remove,
};

static int __init arm_ccn_init(void)
{
	int i, ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_CCN_ONLINE,
				      "perf/arm/ccn:online", NULL,
				      arm_ccn_pmu_offline_cpu);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(arm_ccn_pmu_events); i++)
		arm_ccn_pmu_events_attrs[i] = &arm_ccn_pmu_events[i].attr.attr;

	ret = platform_driver_register(&arm_ccn_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_CCN_ONLINE);
	return ret;
}

static void __exit arm_ccn_exit(void)
{
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_CCN_ONLINE);
	platform_driver_unregister(&arm_ccn_driver);
}

module_init(arm_ccn_init);
module_exit(arm_ccn_exit);

MODULE_AUTHOR("Pawel Moll <pawel.moll@arm.com>");
MODULE_LICENSE("GPL");
