// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2016-2020 Arm Limited
// CMN-600 Coherent Mesh Network PMU driver

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sort.h>

/* Common register stuff */
#define CMN_NODE_INFO			0x0000
#define CMN_NI_NODE_TYPE		GENMASK_ULL(15, 0)
#define CMN_NI_NODE_ID			GENMASK_ULL(31, 16)
#define CMN_NI_LOGICAL_ID		GENMASK_ULL(47, 32)

#define CMN_NODEID_DEVID(reg)		((reg) & 3)
#define CMN_NODEID_EXT_DEVID(reg)	((reg) & 1)
#define CMN_NODEID_PID(reg)		(((reg) >> 2) & 1)
#define CMN_NODEID_EXT_PID(reg)		(((reg) >> 1) & 3)
#define CMN_NODEID_1x1_PID(reg)		(((reg) >> 2) & 7)
#define CMN_NODEID_X(reg, bits)		((reg) >> (3 + (bits)))
#define CMN_NODEID_Y(reg, bits)		(((reg) >> 3) & ((1U << (bits)) - 1))

#define CMN_CHILD_INFO			0x0080
#define CMN_CI_CHILD_COUNT		GENMASK_ULL(15, 0)
#define CMN_CI_CHILD_PTR_OFFSET		GENMASK_ULL(31, 16)

#define CMN_CHILD_NODE_ADDR		GENMASK(27, 0)
#define CMN_CHILD_NODE_EXTERNAL		BIT(31)

#define CMN_MAX_DIMENSION		8
#define CMN_MAX_XPS			(CMN_MAX_DIMENSION * CMN_MAX_DIMENSION)
#define CMN_MAX_DTMS			(CMN_MAX_XPS + (CMN_MAX_DIMENSION - 1) * 4)

/* The CFG node has various info besides the discovery tree */
#define CMN_CFGM_PERIPH_ID_2		0x0010
#define CMN_CFGM_PID2_REVISION		GENMASK(7, 4)

#define CMN_CFGM_INFO_GLOBAL		0x900
#define CMN_INFO_MULTIPLE_DTM_EN	BIT_ULL(63)
#define CMN_INFO_RSP_VC_NUM		GENMASK_ULL(53, 52)
#define CMN_INFO_DAT_VC_NUM		GENMASK_ULL(51, 50)

/* XPs also have some local topology info which has uses too */
#define CMN_MXP__CONNECT_INFO_P0	0x0008
#define CMN_MXP__CONNECT_INFO_P1	0x0010
#define CMN_MXP__CONNECT_INFO_P2	0x0028
#define CMN_MXP__CONNECT_INFO_P3	0x0030
#define CMN_MXP__CONNECT_INFO_P4	0x0038
#define CMN_MXP__CONNECT_INFO_P5	0x0040

/* PMU registers occupy the 3rd 4KB page of each node's region */
#define CMN_PMU_OFFSET			0x2000

/* For most nodes, this is all there is */
#define CMN_PMU_EVENT_SEL		0x000
#define CMN_PMU_EVENTn_ID_SHIFT(n)	((n) * 8)

/* DTMs live in the PMU space of XP registers */
#define CMN_DTM_WPn(n)			(0x1A0 + (n) * 0x18)
#define CMN_DTM_WPn_CONFIG(n)		(CMN_DTM_WPn(n) + 0x00)
#define CMN_DTM_WPn_CONFIG_WP_DEV_SEL2	GENMASK_ULL(18,17)
#define CMN_DTM_WPn_CONFIG_WP_COMBINE	BIT(6)
#define CMN_DTM_WPn_CONFIG_WP_EXCLUSIVE	BIT(5)
#define CMN_DTM_WPn_CONFIG_WP_GRP	BIT(4)
#define CMN_DTM_WPn_CONFIG_WP_CHN_SEL	GENMASK_ULL(3, 1)
#define CMN_DTM_WPn_CONFIG_WP_DEV_SEL	BIT(0)
#define CMN_DTM_WPn_VAL(n)		(CMN_DTM_WPn(n) + 0x08)
#define CMN_DTM_WPn_MASK(n)		(CMN_DTM_WPn(n) + 0x10)

#define CMN_DTM_PMU_CONFIG		0x210
#define CMN__PMEVCNT0_INPUT_SEL		GENMASK_ULL(37, 32)
#define CMN__PMEVCNT0_INPUT_SEL_WP	0x00
#define CMN__PMEVCNT0_INPUT_SEL_XP	0x04
#define CMN__PMEVCNT0_INPUT_SEL_DEV	0x10
#define CMN__PMEVCNT0_GLOBAL_NUM	GENMASK_ULL(18, 16)
#define CMN__PMEVCNTn_GLOBAL_NUM_SHIFT(n)	((n) * 4)
#define CMN__PMEVCNT_PAIRED(n)		BIT(4 + (n))
#define CMN__PMEVCNT23_COMBINED		BIT(2)
#define CMN__PMEVCNT01_COMBINED		BIT(1)
#define CMN_DTM_PMU_CONFIG_PMU_EN	BIT(0)

#define CMN_DTM_PMEVCNT			0x220

#define CMN_DTM_PMEVCNTSR		0x240

#define CMN_DTM_UNIT_INFO		0x0910

#define CMN_DTM_NUM_COUNTERS		4
/* Want more local counters? Why not replicate the whole DTM! Ugh... */
#define CMN_DTM_OFFSET(n)		((n) * 0x200)

/* The DTC node is where the magic happens */
#define CMN_DT_DTC_CTL			0x0a00
#define CMN_DT_DTC_CTL_DT_EN		BIT(0)

/* DTC counters are paired in 64-bit registers on a 16-byte stride. Yuck */
#define _CMN_DT_CNT_REG(n)		((((n) / 2) * 4 + (n) % 2) * 4)
#define CMN_DT_PMEVCNT(n)		(CMN_PMU_OFFSET + _CMN_DT_CNT_REG(n))
#define CMN_DT_PMCCNTR			(CMN_PMU_OFFSET + 0x40)

#define CMN_DT_PMEVCNTSR(n)		(CMN_PMU_OFFSET + 0x50 + _CMN_DT_CNT_REG(n))
#define CMN_DT_PMCCNTRSR		(CMN_PMU_OFFSET + 0x90)

#define CMN_DT_PMCR			(CMN_PMU_OFFSET + 0x100)
#define CMN_DT_PMCR_PMU_EN		BIT(0)
#define CMN_DT_PMCR_CNTR_RST		BIT(5)
#define CMN_DT_PMCR_OVFL_INTR_EN	BIT(6)

#define CMN_DT_PMOVSR			(CMN_PMU_OFFSET + 0x118)
#define CMN_DT_PMOVSR_CLR		(CMN_PMU_OFFSET + 0x120)

#define CMN_DT_PMSSR			(CMN_PMU_OFFSET + 0x128)
#define CMN_DT_PMSSR_SS_STATUS(n)	BIT(n)

#define CMN_DT_PMSRR			(CMN_PMU_OFFSET + 0x130)
#define CMN_DT_PMSRR_SS_REQ		BIT(0)

#define CMN_DT_NUM_COUNTERS		8
#define CMN_MAX_DTCS			4

/*
 * Even in the worst case a DTC counter can't wrap in fewer than 2^42 cycles,
 * so throwing away one bit to make overflow handling easy is no big deal.
 */
#define CMN_COUNTER_INIT		0x80000000
/* Similarly for the 40-bit cycle counter */
#define CMN_CC_INIT			0x8000000000ULL


/* Event attributes */
#define CMN_CONFIG_TYPE			GENMASK_ULL(15, 0)
#define CMN_CONFIG_EVENTID		GENMASK_ULL(23, 16)
#define CMN_CONFIG_OCCUPID		GENMASK_ULL(27, 24)
#define CMN_CONFIG_BYNODEID		BIT_ULL(31)
#define CMN_CONFIG_NODEID		GENMASK_ULL(47, 32)

#define CMN_EVENT_TYPE(event)		FIELD_GET(CMN_CONFIG_TYPE, (event)->attr.config)
#define CMN_EVENT_EVENTID(event)	FIELD_GET(CMN_CONFIG_EVENTID, (event)->attr.config)
#define CMN_EVENT_OCCUPID(event)	FIELD_GET(CMN_CONFIG_OCCUPID, (event)->attr.config)
#define CMN_EVENT_BYNODEID(event)	FIELD_GET(CMN_CONFIG_BYNODEID, (event)->attr.config)
#define CMN_EVENT_NODEID(event)		FIELD_GET(CMN_CONFIG_NODEID, (event)->attr.config)

#define CMN_CONFIG_WP_COMBINE		GENMASK_ULL(27, 24)
#define CMN_CONFIG_WP_DEV_SEL		GENMASK_ULL(50, 48)
#define CMN_CONFIG_WP_CHN_SEL		GENMASK_ULL(55, 51)
#define CMN_CONFIG_WP_GRP		BIT_ULL(56)
#define CMN_CONFIG_WP_EXCLUSIVE		BIT_ULL(57)
#define CMN_CONFIG1_WP_VAL		GENMASK_ULL(63, 0)
#define CMN_CONFIG2_WP_MASK		GENMASK_ULL(63, 0)

#define CMN_EVENT_WP_COMBINE(event)	FIELD_GET(CMN_CONFIG_WP_COMBINE, (event)->attr.config)
#define CMN_EVENT_WP_DEV_SEL(event)	FIELD_GET(CMN_CONFIG_WP_DEV_SEL, (event)->attr.config)
#define CMN_EVENT_WP_CHN_SEL(event)	FIELD_GET(CMN_CONFIG_WP_CHN_SEL, (event)->attr.config)
#define CMN_EVENT_WP_GRP(event)		FIELD_GET(CMN_CONFIG_WP_GRP, (event)->attr.config)
#define CMN_EVENT_WP_EXCLUSIVE(event)	FIELD_GET(CMN_CONFIG_WP_EXCLUSIVE, (event)->attr.config)
#define CMN_EVENT_WP_VAL(event)		FIELD_GET(CMN_CONFIG1_WP_VAL, (event)->attr.config1)
#define CMN_EVENT_WP_MASK(event)	FIELD_GET(CMN_CONFIG2_WP_MASK, (event)->attr.config2)

/* Made-up event IDs for watchpoint direction */
#define CMN_WP_UP			0
#define CMN_WP_DOWN			2


enum cmn_model {
	CMN_ANY = -1,
	CMN600 = 1,
	CI700 = 2,
};

/* CMN-600 r0px shouldn't exist in silicon, thankfully */
enum cmn_revision {
	CMN600_R1P0,
	CMN600_R1P1,
	CMN600_R1P2,
	CMN600_R1P3,
	CMN600_R2P0,
	CMN600_R3P0,
	CMN600_R3P1,
	CI700_R0P0 = 0,
	CI700_R1P0,
	CI700_R2P0,
};

enum cmn_node_type {
	CMN_TYPE_INVALID,
	CMN_TYPE_DVM,
	CMN_TYPE_CFG,
	CMN_TYPE_DTC,
	CMN_TYPE_HNI,
	CMN_TYPE_HNF,
	CMN_TYPE_XP,
	CMN_TYPE_SBSX,
	CMN_TYPE_MPAM_S,
	CMN_TYPE_MPAM_NS,
	CMN_TYPE_RNI,
	CMN_TYPE_RND = 0xd,
	CMN_TYPE_RNSAM = 0xf,
	CMN_TYPE_MTSX,
	CMN_TYPE_CXRA = 0x100,
	CMN_TYPE_CXHA = 0x101,
	CMN_TYPE_CXLA = 0x102,
	/* Not a real node type */
	CMN_TYPE_WP = 0x7770
};

struct arm_cmn_node {
	void __iomem *pmu_base;
	u16 id, logid;
	enum cmn_node_type type;

	int dtm;
	union {
		/* DN/HN-F/CXHA */
		struct {
			u8 occupid_val;
			u8 occupid_count;
		};
		/* XP */
		u8 dtc;
	};
	union {
		u8 event[4];
		__le32 event_sel;
	};
};

struct arm_cmn_dtm {
	void __iomem *base;
	u32 pmu_config_low;
	union {
		u8 input_sel[4];
		__le32 pmu_config_high;
	};
	s8 wp_event[4];
};

struct arm_cmn_dtc {
	void __iomem *base;
	int irq;
	int irq_friend;
	bool cc_active;

	struct perf_event *counters[CMN_DT_NUM_COUNTERS];
	struct perf_event *cycles;
};

#define CMN_STATE_DISABLED	BIT(0)
#define CMN_STATE_TXN		BIT(1)

struct arm_cmn {
	struct device *dev;
	void __iomem *base;
	unsigned int state;

	enum cmn_revision rev;
	enum cmn_model model;
	u8 mesh_x;
	u8 mesh_y;
	u16 num_xps;
	u16 num_dns;
	bool multi_dtm;
	u8 ports_used;
	struct {
		unsigned int rsp_vc_num : 2;
		unsigned int dat_vc_num : 2;
	};

	struct arm_cmn_node *xps;
	struct arm_cmn_node *dns;

	struct arm_cmn_dtm *dtms;
	struct arm_cmn_dtc *dtc;
	unsigned int num_dtcs;

	int cpu;
	struct hlist_node cpuhp_node;

	struct pmu pmu;
	struct dentry *debug;
};

#define to_cmn(p)	container_of(p, struct arm_cmn, pmu)

static int arm_cmn_hp_state;

struct arm_cmn_nodeid {
	u8 x;
	u8 y;
	u8 port;
	u8 dev;
};

static int arm_cmn_xyidbits(const struct arm_cmn *cmn)
{
	int dim = max(cmn->mesh_x, cmn->mesh_y);

	return dim > 4 ? 3 : 2;
}

static struct arm_cmn_nodeid arm_cmn_nid(const struct arm_cmn *cmn, u16 id)
{
	struct arm_cmn_nodeid nid;

	if (cmn->num_xps == 1) {
		nid.x = 0;
		nid.y = 0;
		nid.port = CMN_NODEID_1x1_PID(id);
		nid.dev = CMN_NODEID_DEVID(id);
	} else {
		int bits = arm_cmn_xyidbits(cmn);

		nid.x = CMN_NODEID_X(id, bits);
		nid.y = CMN_NODEID_Y(id, bits);
		if (cmn->ports_used & 0xc) {
			nid.port = CMN_NODEID_EXT_PID(id);
			nid.dev = CMN_NODEID_EXT_DEVID(id);
		} else {
			nid.port = CMN_NODEID_PID(id);
			nid.dev = CMN_NODEID_DEVID(id);
		}
	}
	return nid;
}

static struct arm_cmn_node *arm_cmn_node_to_xp(const struct arm_cmn *cmn,
					       const struct arm_cmn_node *dn)
{
	struct arm_cmn_nodeid nid = arm_cmn_nid(cmn, dn->id);
	int xp_idx = cmn->mesh_x * nid.y + nid.x;

	return cmn->xps + xp_idx;
}
static struct arm_cmn_node *arm_cmn_node(const struct arm_cmn *cmn,
					 enum cmn_node_type type)
{
	struct arm_cmn_node *dn;

	for (dn = cmn->dns; dn->type; dn++)
		if (dn->type == type)
			return dn;
	return NULL;
}

static struct dentry *arm_cmn_debugfs;

#ifdef CONFIG_DEBUG_FS
static const char *arm_cmn_device_type(u8 type)
{
	switch(type) {
		case 0x01: return "  RN-I  |";
		case 0x02: return "  RN-D  |";
		case 0x04: return " RN-F_B |";
		case 0x05: return "RN-F_B_E|";
		case 0x06: return " RN-F_A |";
		case 0x07: return "RN-F_A_E|";
		case 0x08: return "  HN-T  |";
		case 0x09: return "  HN-I  |";
		case 0x0a: return "  HN-D  |";
		case 0x0c: return "  SN-F  |";
		case 0x0d: return "  SBSX  |";
		case 0x0e: return "  HN-F  |";
		case 0x0f: return " SN-F_E |";
		case 0x10: return " SN-F_D |";
		case 0x11: return "  CXHA  |";
		case 0x12: return "  CXRA  |";
		case 0x13: return "  CXRH  |";
		case 0x14: return " RN-F_D |";
		case 0x15: return "RN-F_D_E|";
		case 0x16: return " RN-F_C |";
		case 0x17: return "RN-F_C_E|";
		case 0x1c: return "  MTSX  |";
		default:   return "        |";
	}
}

static void arm_cmn_show_logid(struct seq_file *s, int x, int y, int p, int d)
{
	struct arm_cmn *cmn = s->private;
	struct arm_cmn_node *dn;

	for (dn = cmn->dns; dn->type; dn++) {
		struct arm_cmn_nodeid nid = arm_cmn_nid(cmn, dn->id);

		if (dn->type == CMN_TYPE_XP)
			continue;
		/* Ignore the extra components that will overlap on some ports */
		if (dn->type < CMN_TYPE_HNI)
			continue;

		if (nid.x != x || nid.y != y || nid.port != p || nid.dev != d)
			continue;

		seq_printf(s, "   #%-2d  |", dn->logid);
		return;
	}
	seq_puts(s, "        |");
}

static int arm_cmn_map_show(struct seq_file *s, void *data)
{
	struct arm_cmn *cmn = s->private;
	int x, y, p, pmax = fls(cmn->ports_used);

	seq_puts(s, "     X");
	for (x = 0; x < cmn->mesh_x; x++)
		seq_printf(s, "    %d    ", x);
	seq_puts(s, "\nY P D+");
	y = cmn->mesh_y;
	while (y--) {
		int xp_base = cmn->mesh_x * y;
		u8 port[6][CMN_MAX_DIMENSION];

		for (x = 0; x < cmn->mesh_x; x++)
			seq_puts(s, "--------+");

		seq_printf(s, "\n%d    |", y);
		for (x = 0; x < cmn->mesh_x; x++) {
			struct arm_cmn_node *xp = cmn->xps + xp_base + x;
			void __iomem *base = xp->pmu_base - CMN_PMU_OFFSET;

			port[0][x] = readl_relaxed(base + CMN_MXP__CONNECT_INFO_P0);
			port[1][x] = readl_relaxed(base + CMN_MXP__CONNECT_INFO_P1);
			port[2][x] = readl_relaxed(base + CMN_MXP__CONNECT_INFO_P2);
			port[3][x] = readl_relaxed(base + CMN_MXP__CONNECT_INFO_P3);
			port[4][x] = readl_relaxed(base + CMN_MXP__CONNECT_INFO_P4);
			port[5][x] = readl_relaxed(base + CMN_MXP__CONNECT_INFO_P5);
			seq_printf(s, " XP #%-2d |", xp_base + x);
		}

		seq_puts(s, "\n     |");
		for (x = 0; x < cmn->mesh_x; x++) {
			u8 dtc = cmn->xps[xp_base + x].dtc;

			if (dtc & (dtc - 1))
				seq_puts(s, " DTC ?? |");
			else
				seq_printf(s, " DTC %ld  |", __ffs(dtc));
		}
		seq_puts(s, "\n     |");
		for (x = 0; x < cmn->mesh_x; x++)
			seq_puts(s, "........|");

		for (p = 0; p < pmax; p++) {
			seq_printf(s, "\n  %d  |", p);
			for (x = 0; x < cmn->mesh_x; x++)
				seq_puts(s, arm_cmn_device_type(port[p][x]));
			seq_puts(s, "\n    0|");
			for (x = 0; x < cmn->mesh_x; x++)
				arm_cmn_show_logid(s, x, y, p, 0);
			seq_puts(s, "\n    1|");
			for (x = 0; x < cmn->mesh_x; x++)
				arm_cmn_show_logid(s, x, y, p, 1);
		}
		seq_puts(s, "\n-----+");
	}
	for (x = 0; x < cmn->mesh_x; x++)
		seq_puts(s, "--------+");
	seq_puts(s, "\n");
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(arm_cmn_map);

static void arm_cmn_debugfs_init(struct arm_cmn *cmn, int id)
{
	const char *name  = "map";

	if (id > 0)
		name = devm_kasprintf(cmn->dev, GFP_KERNEL, "map_%d", id);
	if (!name)
		return;

	cmn->debug = debugfs_create_file(name, 0444, arm_cmn_debugfs, cmn, &arm_cmn_map_fops);
}
#else
static void arm_cmn_debugfs_init(struct arm_cmn *cmn, int id) {}
#endif

struct arm_cmn_hw_event {
	struct arm_cmn_node *dn;
	u64 dtm_idx[2];
	unsigned int dtc_idx;
	u8 dtcs_used;
	u8 num_dns;
	u8 dtm_offset;
};

#define for_each_hw_dn(hw, dn, i) \
	for (i = 0, dn = hw->dn; i < hw->num_dns; i++, dn++)

static struct arm_cmn_hw_event *to_cmn_hw(struct perf_event *event)
{
	BUILD_BUG_ON(sizeof(struct arm_cmn_hw_event) > offsetof(struct hw_perf_event, target));
	return (struct arm_cmn_hw_event *)&event->hw;
}

static void arm_cmn_set_index(u64 x[], unsigned int pos, unsigned int val)
{
	x[pos / 32] |= (u64)val << ((pos % 32) * 2);
}

static unsigned int arm_cmn_get_index(u64 x[], unsigned int pos)
{
	return (x[pos / 32] >> ((pos % 32) * 2)) & 3;
}

struct arm_cmn_event_attr {
	struct device_attribute attr;
	enum cmn_model model;
	enum cmn_node_type type;
	u8 eventid;
	u8 occupid;
};

struct arm_cmn_format_attr {
	struct device_attribute attr;
	u64 field;
	int config;
};

#define CMN_EVENT_ATTR(_model, _name, _type, _eventid, _occupid)	\
	(&((struct arm_cmn_event_attr[]) {{				\
		.attr = __ATTR(_name, 0444, arm_cmn_event_show, NULL),	\
		.model = _model,					\
		.type = _type,						\
		.eventid = _eventid,					\
		.occupid = _occupid,					\
	}})[0].attr.attr)

static bool arm_cmn_is_occup_event(enum cmn_model model,
				   enum cmn_node_type type, unsigned int id)
{
	if (type == CMN_TYPE_DVM)
		return (model == CMN600 && id == 0x05) ||
		       (model == CI700 && id == 0x0c);
	return type == CMN_TYPE_HNF && id == 0x0f;
}

static ssize_t arm_cmn_event_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct arm_cmn_event_attr *eattr;

	eattr = container_of(attr, typeof(*eattr), attr);

	if (eattr->type == CMN_TYPE_DTC)
		return sysfs_emit(buf, "type=0x%x\n", eattr->type);

	if (eattr->type == CMN_TYPE_WP)
		return sysfs_emit(buf,
				  "type=0x%x,eventid=0x%x,wp_dev_sel=?,wp_chn_sel=?,wp_grp=?,wp_val=?,wp_mask=?\n",
				  eattr->type, eattr->eventid);

	if (arm_cmn_is_occup_event(eattr->model, eattr->type, eattr->eventid))
		return sysfs_emit(buf, "type=0x%x,eventid=0x%x,occupid=0x%x\n",
				  eattr->type, eattr->eventid, eattr->occupid);

	return sysfs_emit(buf, "type=0x%x,eventid=0x%x\n", eattr->type,
			  eattr->eventid);
}

static umode_t arm_cmn_event_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr,
					     int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct arm_cmn *cmn = to_cmn(dev_get_drvdata(dev));
	struct arm_cmn_event_attr *eattr;

	eattr = container_of(attr, typeof(*eattr), attr.attr);

	if (!(eattr->model & cmn->model))
		return 0;

	/* Watchpoints aren't nodes, so avoid confusion */
	if (eattr->type == CMN_TYPE_WP)
		return attr->mode;

	/* Hide XP events for unused interfaces/channels */
	if (eattr->type == CMN_TYPE_XP) {
		unsigned int intf = (eattr->eventid >> 2) & 7;
		unsigned int chan = eattr->eventid >> 5;

		if ((intf & 4) && !(cmn->ports_used & BIT(intf & 3)))
			return 0;

		if (chan == 4 && cmn->model == CMN600)
			return 0;

		if ((chan == 5 && cmn->rsp_vc_num < 2) ||
		    (chan == 6 && cmn->dat_vc_num < 2))
			return 0;
	}

	/* Revision-specific differences */
	if (cmn->model == CMN600 && cmn->rev < CMN600_R1P2) {
		if (eattr->type == CMN_TYPE_HNF && eattr->eventid == 0x1b)
			return 0;
	}

	if (!arm_cmn_node(cmn, eattr->type))
		return 0;

	return attr->mode;
}

#define _CMN_EVENT_DVM(_model, _name, _event, _occup)		\
	CMN_EVENT_ATTR(_model, dn_##_name, CMN_TYPE_DVM, _event, _occup)
#define CMN_EVENT_DTC(_name)					\
	CMN_EVENT_ATTR(CMN_ANY, dtc_##_name, CMN_TYPE_DTC, 0, 0)
#define _CMN_EVENT_HNF(_model, _name, _event, _occup)		\
	CMN_EVENT_ATTR(_model, hnf_##_name, CMN_TYPE_HNF, _event, _occup)
#define CMN_EVENT_HNI(_name, _event)				\
	CMN_EVENT_ATTR(CMN_ANY, hni_##_name, CMN_TYPE_HNI, _event, 0)
#define __CMN_EVENT_XP(_name, _event)				\
	CMN_EVENT_ATTR(CMN_ANY, mxp_##_name, CMN_TYPE_XP, _event, 0)
#define CMN_EVENT_SBSX(_model, _name, _event)			\
	CMN_EVENT_ATTR(_model, sbsx_##_name, CMN_TYPE_SBSX, _event, 0)
#define CMN_EVENT_RNID(_model, _name, _event)			\
	CMN_EVENT_ATTR(_model, rnid_##_name, CMN_TYPE_RNI, _event, 0)
#define CMN_EVENT_MTSX(_name, _event)				\
	CMN_EVENT_ATTR(CMN_ANY, mtsx_##_name, CMN_TYPE_MTSX, _event, 0)

#define CMN_EVENT_DVM(_model, _name, _event)			\
	_CMN_EVENT_DVM(_model, _name, _event, 0)
#define CMN_EVENT_HNF(_model, _name, _event)			\
	_CMN_EVENT_HNF(_model, _name, _event, 0)
#define _CMN_EVENT_XP(_name, _event)				\
	__CMN_EVENT_XP(e_##_name, (_event) | (0 << 2)),		\
	__CMN_EVENT_XP(w_##_name, (_event) | (1 << 2)),		\
	__CMN_EVENT_XP(n_##_name, (_event) | (2 << 2)),		\
	__CMN_EVENT_XP(s_##_name, (_event) | (3 << 2)),		\
	__CMN_EVENT_XP(p0_##_name, (_event) | (4 << 2)),	\
	__CMN_EVENT_XP(p1_##_name, (_event) | (5 << 2)),	\
	__CMN_EVENT_XP(p2_##_name, (_event) | (6 << 2)),	\
	__CMN_EVENT_XP(p3_##_name, (_event) | (7 << 2))

/* Good thing there are only 3 fundamental XP events... */
#define CMN_EVENT_XP(_name, _event)				\
	_CMN_EVENT_XP(req_##_name, (_event) | (0 << 5)),	\
	_CMN_EVENT_XP(rsp_##_name, (_event) | (1 << 5)),	\
	_CMN_EVENT_XP(snp_##_name, (_event) | (2 << 5)),	\
	_CMN_EVENT_XP(dat_##_name, (_event) | (3 << 5)),	\
	_CMN_EVENT_XP(pub_##_name, (_event) | (4 << 5)),	\
	_CMN_EVENT_XP(rsp2_##_name, (_event) | (5 << 5)),	\
	_CMN_EVENT_XP(dat2_##_name, (_event) | (6 << 5))


static struct attribute *arm_cmn_event_attrs[] = {
	CMN_EVENT_DTC(cycles),

	/*
	 * DVM node events conflict with HN-I events in the equivalent PMU
	 * slot, but our lazy short-cut of using the DTM counter index for
	 * the PMU index as well happens to avoid that by construction.
	 */
	CMN_EVENT_DVM(CMN600, rxreq_dvmop,		0x01),
	CMN_EVENT_DVM(CMN600, rxreq_dvmsync,		0x02),
	CMN_EVENT_DVM(CMN600, rxreq_dvmop_vmid_filtered, 0x03),
	CMN_EVENT_DVM(CMN600, rxreq_retried,		0x04),
	_CMN_EVENT_DVM(CMN600, rxreq_trk_occupancy_all, 0x05, 0),
	_CMN_EVENT_DVM(CMN600, rxreq_trk_occupancy_dvmop, 0x05, 1),
	_CMN_EVENT_DVM(CMN600, rxreq_trk_occupancy_dvmsync, 0x05, 2),
	CMN_EVENT_DVM(CI700, dvmop_tlbi,		0x01),
	CMN_EVENT_DVM(CI700, dvmop_bpi,			0x02),
	CMN_EVENT_DVM(CI700, dvmop_pici,		0x03),
	CMN_EVENT_DVM(CI700, dvmop_vici,		0x04),
	CMN_EVENT_DVM(CI700, dvmsync,			0x05),
	CMN_EVENT_DVM(CI700, vmid_filtered,		0x06),
	CMN_EVENT_DVM(CI700, rndop_filtered,		0x07),
	CMN_EVENT_DVM(CI700, retry,			0x08),
	CMN_EVENT_DVM(CI700, txsnp_flitv,		0x09),
	CMN_EVENT_DVM(CI700, txsnp_stall,		0x0a),
	CMN_EVENT_DVM(CI700, trkfull,			0x0b),
	_CMN_EVENT_DVM(CI700, trk_occupancy_all,	0x0c, 0),
	_CMN_EVENT_DVM(CI700, trk_occupancy_dvmop,	0x0c, 1),
	_CMN_EVENT_DVM(CI700, trk_occupancy_dvmsync,	0x0c, 2),

	CMN_EVENT_HNF(CMN_ANY, cache_miss,		0x01),
	CMN_EVENT_HNF(CMN_ANY, slc_sf_cache_access,	0x02),
	CMN_EVENT_HNF(CMN_ANY, cache_fill,		0x03),
	CMN_EVENT_HNF(CMN_ANY, pocq_retry,		0x04),
	CMN_EVENT_HNF(CMN_ANY, pocq_reqs_recvd,		0x05),
	CMN_EVENT_HNF(CMN_ANY, sf_hit,			0x06),
	CMN_EVENT_HNF(CMN_ANY, sf_evictions,		0x07),
	CMN_EVENT_HNF(CMN_ANY, dir_snoops_sent,		0x08),
	CMN_EVENT_HNF(CMN_ANY, brd_snoops_sent,		0x09),
	CMN_EVENT_HNF(CMN_ANY, slc_eviction,		0x0a),
	CMN_EVENT_HNF(CMN_ANY, slc_fill_invalid_way,	0x0b),
	CMN_EVENT_HNF(CMN_ANY, mc_retries,		0x0c),
	CMN_EVENT_HNF(CMN_ANY, mc_reqs,			0x0d),
	CMN_EVENT_HNF(CMN_ANY, qos_hh_retry,		0x0e),
	_CMN_EVENT_HNF(CMN_ANY, qos_pocq_occupancy_all,	0x0f, 0),
	_CMN_EVENT_HNF(CMN_ANY, qos_pocq_occupancy_read, 0x0f, 1),
	_CMN_EVENT_HNF(CMN_ANY, qos_pocq_occupancy_write, 0x0f, 2),
	_CMN_EVENT_HNF(CMN_ANY, qos_pocq_occupancy_atomic, 0x0f, 3),
	_CMN_EVENT_HNF(CMN_ANY, qos_pocq_occupancy_stash, 0x0f, 4),
	CMN_EVENT_HNF(CMN_ANY, pocq_addrhaz,		0x10),
	CMN_EVENT_HNF(CMN_ANY, pocq_atomic_addrhaz,	0x11),
	CMN_EVENT_HNF(CMN_ANY, ld_st_swp_adq_full,	0x12),
	CMN_EVENT_HNF(CMN_ANY, cmp_adq_full,		0x13),
	CMN_EVENT_HNF(CMN_ANY, txdat_stall,		0x14),
	CMN_EVENT_HNF(CMN_ANY, txrsp_stall,		0x15),
	CMN_EVENT_HNF(CMN_ANY, seq_full,		0x16),
	CMN_EVENT_HNF(CMN_ANY, seq_hit,			0x17),
	CMN_EVENT_HNF(CMN_ANY, snp_sent,		0x18),
	CMN_EVENT_HNF(CMN_ANY, sfbi_dir_snp_sent,	0x19),
	CMN_EVENT_HNF(CMN_ANY, sfbi_brd_snp_sent,	0x1a),
	CMN_EVENT_HNF(CMN_ANY, snp_sent_untrk,		0x1b),
	CMN_EVENT_HNF(CMN_ANY, intv_dirty,		0x1c),
	CMN_EVENT_HNF(CMN_ANY, stash_snp_sent,		0x1d),
	CMN_EVENT_HNF(CMN_ANY, stash_data_pull,		0x1e),
	CMN_EVENT_HNF(CMN_ANY, snp_fwded,		0x1f),
	CMN_EVENT_HNF(CI700, atomic_fwd,		0x20),
	CMN_EVENT_HNF(CI700, mpam_hardlim,		0x21),
	CMN_EVENT_HNF(CI700, mpam_softlim,		0x22),

	CMN_EVENT_HNI(rrt_rd_occ_cnt_ovfl,		0x20),
	CMN_EVENT_HNI(rrt_wr_occ_cnt_ovfl,		0x21),
	CMN_EVENT_HNI(rdt_rd_occ_cnt_ovfl,		0x22),
	CMN_EVENT_HNI(rdt_wr_occ_cnt_ovfl,		0x23),
	CMN_EVENT_HNI(wdb_occ_cnt_ovfl,			0x24),
	CMN_EVENT_HNI(rrt_rd_alloc,			0x25),
	CMN_EVENT_HNI(rrt_wr_alloc,			0x26),
	CMN_EVENT_HNI(rdt_rd_alloc,			0x27),
	CMN_EVENT_HNI(rdt_wr_alloc,			0x28),
	CMN_EVENT_HNI(wdb_alloc,			0x29),
	CMN_EVENT_HNI(txrsp_retryack,			0x2a),
	CMN_EVENT_HNI(arvalid_no_arready,		0x2b),
	CMN_EVENT_HNI(arready_no_arvalid,		0x2c),
	CMN_EVENT_HNI(awvalid_no_awready,		0x2d),
	CMN_EVENT_HNI(awready_no_awvalid,		0x2e),
	CMN_EVENT_HNI(wvalid_no_wready,			0x2f),
	CMN_EVENT_HNI(txdat_stall,			0x30),
	CMN_EVENT_HNI(nonpcie_serialization,		0x31),
	CMN_EVENT_HNI(pcie_serialization,		0x32),

	CMN_EVENT_XP(txflit_valid,			0x01),
	CMN_EVENT_XP(txflit_stall,			0x02),
	CMN_EVENT_XP(partial_dat_flit,			0x03),
	/* We treat watchpoints as a special made-up class of XP events */
	CMN_EVENT_ATTR(CMN_ANY, watchpoint_up, CMN_TYPE_WP, CMN_WP_UP, 0),
	CMN_EVENT_ATTR(CMN_ANY, watchpoint_down, CMN_TYPE_WP, CMN_WP_DOWN, 0),

	CMN_EVENT_SBSX(CMN_ANY, rd_req,			0x01),
	CMN_EVENT_SBSX(CMN_ANY, wr_req,			0x02),
	CMN_EVENT_SBSX(CMN_ANY, cmo_req,		0x03),
	CMN_EVENT_SBSX(CMN_ANY, txrsp_retryack,		0x04),
	CMN_EVENT_SBSX(CMN_ANY, txdat_flitv,		0x05),
	CMN_EVENT_SBSX(CMN_ANY, txrsp_flitv,		0x06),
	CMN_EVENT_SBSX(CMN_ANY, rd_req_trkr_occ_cnt_ovfl, 0x11),
	CMN_EVENT_SBSX(CMN_ANY, wr_req_trkr_occ_cnt_ovfl, 0x12),
	CMN_EVENT_SBSX(CMN_ANY, cmo_req_trkr_occ_cnt_ovfl, 0x13),
	CMN_EVENT_SBSX(CMN_ANY, wdb_occ_cnt_ovfl,	0x14),
	CMN_EVENT_SBSX(CMN_ANY, rd_axi_trkr_occ_cnt_ovfl, 0x15),
	CMN_EVENT_SBSX(CMN_ANY, cmo_axi_trkr_occ_cnt_ovfl, 0x16),
	CMN_EVENT_SBSX(CI700, rdb_occ_cnt_ovfl,		0x17),
	CMN_EVENT_SBSX(CMN_ANY, arvalid_no_arready,	0x21),
	CMN_EVENT_SBSX(CMN_ANY, awvalid_no_awready,	0x22),
	CMN_EVENT_SBSX(CMN_ANY, wvalid_no_wready,	0x23),
	CMN_EVENT_SBSX(CMN_ANY, txdat_stall,		0x24),
	CMN_EVENT_SBSX(CMN_ANY, txrsp_stall,		0x25),

	CMN_EVENT_RNID(CMN_ANY, s0_rdata_beats,		0x01),
	CMN_EVENT_RNID(CMN_ANY, s1_rdata_beats,		0x02),
	CMN_EVENT_RNID(CMN_ANY, s2_rdata_beats,		0x03),
	CMN_EVENT_RNID(CMN_ANY, rxdat_flits,		0x04),
	CMN_EVENT_RNID(CMN_ANY, txdat_flits,		0x05),
	CMN_EVENT_RNID(CMN_ANY, txreq_flits_total,	0x06),
	CMN_EVENT_RNID(CMN_ANY, txreq_flits_retried,	0x07),
	CMN_EVENT_RNID(CMN_ANY, rrt_occ_ovfl,		0x08),
	CMN_EVENT_RNID(CMN_ANY, wrt_occ_ovfl,		0x09),
	CMN_EVENT_RNID(CMN_ANY, txreq_flits_replayed,	0x0a),
	CMN_EVENT_RNID(CMN_ANY, wrcancel_sent,		0x0b),
	CMN_EVENT_RNID(CMN_ANY, s0_wdata_beats,		0x0c),
	CMN_EVENT_RNID(CMN_ANY, s1_wdata_beats,		0x0d),
	CMN_EVENT_RNID(CMN_ANY, s2_wdata_beats,		0x0e),
	CMN_EVENT_RNID(CMN_ANY, rrt_alloc,		0x0f),
	CMN_EVENT_RNID(CMN_ANY, wrt_alloc,		0x10),
	CMN_EVENT_RNID(CMN600, rdb_unord,		0x11),
	CMN_EVENT_RNID(CMN600, rdb_replay,		0x12),
	CMN_EVENT_RNID(CMN600, rdb_hybrid,		0x13),
	CMN_EVENT_RNID(CMN600, rdb_ord,			0x14),
	CMN_EVENT_RNID(CI700, padb_occ_ovfl,		0x11),
	CMN_EVENT_RNID(CI700, rpdb_occ_ovfl,		0x12),
	CMN_EVENT_RNID(CI700, rrt_occup_ovfl_slice1,	0x13),
	CMN_EVENT_RNID(CI700, rrt_occup_ovfl_slice2,	0x14),
	CMN_EVENT_RNID(CI700, rrt_occup_ovfl_slice3,	0x15),
	CMN_EVENT_RNID(CI700, wrt_throttled,		0x16),

	CMN_EVENT_MTSX(tc_lookup,			0x01),
	CMN_EVENT_MTSX(tc_fill,				0x02),
	CMN_EVENT_MTSX(tc_miss,				0x03),
	CMN_EVENT_MTSX(tdb_forward,			0x04),
	CMN_EVENT_MTSX(tcq_hazard,			0x05),
	CMN_EVENT_MTSX(tcq_rd_alloc,			0x06),
	CMN_EVENT_MTSX(tcq_wr_alloc,			0x07),
	CMN_EVENT_MTSX(tcq_cmo_alloc,			0x08),
	CMN_EVENT_MTSX(axi_rd_req,			0x09),
	CMN_EVENT_MTSX(axi_wr_req,			0x0a),
	CMN_EVENT_MTSX(tcq_occ_cnt_ovfl,		0x0b),
	CMN_EVENT_MTSX(tdb_occ_cnt_ovfl,		0x0c),

	NULL
};

static const struct attribute_group arm_cmn_event_attrs_group = {
	.name = "events",
	.attrs = arm_cmn_event_attrs,
	.is_visible = arm_cmn_event_attr_is_visible,
};

static ssize_t arm_cmn_format_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct arm_cmn_format_attr *fmt = container_of(attr, typeof(*fmt), attr);
	int lo = __ffs(fmt->field), hi = __fls(fmt->field);

	if (lo == hi)
		return sysfs_emit(buf, "config:%d\n", lo);

	if (!fmt->config)
		return sysfs_emit(buf, "config:%d-%d\n", lo, hi);

	return sysfs_emit(buf, "config%d:%d-%d\n", fmt->config, lo, hi);
}

#define _CMN_FORMAT_ATTR(_name, _cfg, _fld)				\
	(&((struct arm_cmn_format_attr[]) {{				\
		.attr = __ATTR(_name, 0444, arm_cmn_format_show, NULL),	\
		.config = _cfg,						\
		.field = _fld,						\
	}})[0].attr.attr)
#define CMN_FORMAT_ATTR(_name, _fld)	_CMN_FORMAT_ATTR(_name, 0, _fld)

static struct attribute *arm_cmn_format_attrs[] = {
	CMN_FORMAT_ATTR(type, CMN_CONFIG_TYPE),
	CMN_FORMAT_ATTR(eventid, CMN_CONFIG_EVENTID),
	CMN_FORMAT_ATTR(occupid, CMN_CONFIG_OCCUPID),
	CMN_FORMAT_ATTR(bynodeid, CMN_CONFIG_BYNODEID),
	CMN_FORMAT_ATTR(nodeid, CMN_CONFIG_NODEID),

	CMN_FORMAT_ATTR(wp_dev_sel, CMN_CONFIG_WP_DEV_SEL),
	CMN_FORMAT_ATTR(wp_chn_sel, CMN_CONFIG_WP_CHN_SEL),
	CMN_FORMAT_ATTR(wp_grp, CMN_CONFIG_WP_GRP),
	CMN_FORMAT_ATTR(wp_exclusive, CMN_CONFIG_WP_EXCLUSIVE),
	CMN_FORMAT_ATTR(wp_combine, CMN_CONFIG_WP_COMBINE),

	_CMN_FORMAT_ATTR(wp_val, 1, CMN_CONFIG1_WP_VAL),
	_CMN_FORMAT_ATTR(wp_mask, 2, CMN_CONFIG2_WP_MASK),

	NULL
};

static const struct attribute_group arm_cmn_format_attrs_group = {
	.name = "format",
	.attrs = arm_cmn_format_attrs,
};

static ssize_t arm_cmn_cpumask_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct arm_cmn *cmn = to_cmn(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(cmn->cpu));
}

static struct device_attribute arm_cmn_cpumask_attr =
		__ATTR(cpumask, 0444, arm_cmn_cpumask_show, NULL);

static struct attribute *arm_cmn_cpumask_attrs[] = {
	&arm_cmn_cpumask_attr.attr,
	NULL,
};

static const struct attribute_group arm_cmn_cpumask_attr_group = {
	.attrs = arm_cmn_cpumask_attrs,
};

static const struct attribute_group *arm_cmn_attr_groups[] = {
	&arm_cmn_event_attrs_group,
	&arm_cmn_format_attrs_group,
	&arm_cmn_cpumask_attr_group,
	NULL
};

static int arm_cmn_wp_idx(struct perf_event *event)
{
	return CMN_EVENT_EVENTID(event) + CMN_EVENT_WP_GRP(event);
}

static u32 arm_cmn_wp_config(struct perf_event *event)
{
	u32 config;
	u32 dev = CMN_EVENT_WP_DEV_SEL(event);
	u32 chn = CMN_EVENT_WP_CHN_SEL(event);
	u32 grp = CMN_EVENT_WP_GRP(event);
	u32 exc = CMN_EVENT_WP_EXCLUSIVE(event);
	u32 combine = CMN_EVENT_WP_COMBINE(event);

	config = FIELD_PREP(CMN_DTM_WPn_CONFIG_WP_DEV_SEL, dev) |
		 FIELD_PREP(CMN_DTM_WPn_CONFIG_WP_CHN_SEL, chn) |
		 FIELD_PREP(CMN_DTM_WPn_CONFIG_WP_GRP, grp) |
		 FIELD_PREP(CMN_DTM_WPn_CONFIG_WP_EXCLUSIVE, exc) |
		 FIELD_PREP(CMN_DTM_WPn_CONFIG_WP_DEV_SEL2, dev >> 1);
	if (combine && !grp)
		config |= CMN_DTM_WPn_CONFIG_WP_COMBINE;

	return config;
}

static void arm_cmn_set_state(struct arm_cmn *cmn, u32 state)
{
	if (!cmn->state)
		writel_relaxed(0, cmn->dtc[0].base + CMN_DT_PMCR);
	cmn->state |= state;
}

static void arm_cmn_clear_state(struct arm_cmn *cmn, u32 state)
{
	cmn->state &= ~state;
	if (!cmn->state)
		writel_relaxed(CMN_DT_PMCR_PMU_EN | CMN_DT_PMCR_OVFL_INTR_EN,
			       cmn->dtc[0].base + CMN_DT_PMCR);
}

static void arm_cmn_pmu_enable(struct pmu *pmu)
{
	arm_cmn_clear_state(to_cmn(pmu), CMN_STATE_DISABLED);
}

static void arm_cmn_pmu_disable(struct pmu *pmu)
{
	arm_cmn_set_state(to_cmn(pmu), CMN_STATE_DISABLED);
}

static u64 arm_cmn_read_dtm(struct arm_cmn *cmn, struct arm_cmn_hw_event *hw,
			    bool snapshot)
{
	struct arm_cmn_dtm *dtm = NULL;
	struct arm_cmn_node *dn;
	unsigned int i, offset, dtm_idx;
	u64 reg, count = 0;

	offset = snapshot ? CMN_DTM_PMEVCNTSR : CMN_DTM_PMEVCNT;
	for_each_hw_dn(hw, dn, i) {
		if (dtm != &cmn->dtms[dn->dtm]) {
			dtm = &cmn->dtms[dn->dtm] + hw->dtm_offset;
			reg = readq_relaxed(dtm->base + offset);
		}
		dtm_idx = arm_cmn_get_index(hw->dtm_idx, i);
		count += (u16)(reg >> (dtm_idx * 16));
	}
	return count;
}

static u64 arm_cmn_read_cc(struct arm_cmn_dtc *dtc)
{
	u64 val = readq_relaxed(dtc->base + CMN_DT_PMCCNTR);

	writeq_relaxed(CMN_CC_INIT, dtc->base + CMN_DT_PMCCNTR);
	return (val - CMN_CC_INIT) & ((CMN_CC_INIT << 1) - 1);
}

static u32 arm_cmn_read_counter(struct arm_cmn_dtc *dtc, int idx)
{
	u32 val, pmevcnt = CMN_DT_PMEVCNT(idx);

	val = readl_relaxed(dtc->base + pmevcnt);
	writel_relaxed(CMN_COUNTER_INIT, dtc->base + pmevcnt);
	return val - CMN_COUNTER_INIT;
}

static void arm_cmn_init_counter(struct perf_event *event)
{
	struct arm_cmn *cmn = to_cmn(event->pmu);
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	unsigned int i, pmevcnt = CMN_DT_PMEVCNT(hw->dtc_idx);
	u64 count;

	for (i = 0; hw->dtcs_used & (1U << i); i++) {
		writel_relaxed(CMN_COUNTER_INIT, cmn->dtc[i].base + pmevcnt);
		cmn->dtc[i].counters[hw->dtc_idx] = event;
	}

	count = arm_cmn_read_dtm(cmn, hw, false);
	local64_set(&event->hw.prev_count, count);
}

static void arm_cmn_event_read(struct perf_event *event)
{
	struct arm_cmn *cmn = to_cmn(event->pmu);
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	u64 delta, new, prev;
	unsigned long flags;
	unsigned int i;

	if (hw->dtc_idx == CMN_DT_NUM_COUNTERS) {
		i = __ffs(hw->dtcs_used);
		delta = arm_cmn_read_cc(cmn->dtc + i);
		local64_add(delta, &event->count);
		return;
	}
	new = arm_cmn_read_dtm(cmn, hw, false);
	prev = local64_xchg(&event->hw.prev_count, new);

	delta = new - prev;

	local_irq_save(flags);
	for (i = 0; hw->dtcs_used & (1U << i); i++) {
		new = arm_cmn_read_counter(cmn->dtc + i, hw->dtc_idx);
		delta += new << 16;
	}
	local_irq_restore(flags);
	local64_add(delta, &event->count);
}

static void arm_cmn_event_start(struct perf_event *event, int flags)
{
	struct arm_cmn *cmn = to_cmn(event->pmu);
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	struct arm_cmn_node *dn;
	enum cmn_node_type type = CMN_EVENT_TYPE(event);
	int i;

	if (type == CMN_TYPE_DTC) {
		i = __ffs(hw->dtcs_used);
		writeq_relaxed(CMN_CC_INIT, cmn->dtc[i].base + CMN_DT_PMCCNTR);
		cmn->dtc[i].cc_active = true;
	} else if (type == CMN_TYPE_WP) {
		int wp_idx = arm_cmn_wp_idx(event);
		u64 val = CMN_EVENT_WP_VAL(event);
		u64 mask = CMN_EVENT_WP_MASK(event);

		for_each_hw_dn(hw, dn, i) {
			void __iomem *base = dn->pmu_base + CMN_DTM_OFFSET(hw->dtm_offset);

			writeq_relaxed(val, base + CMN_DTM_WPn_VAL(wp_idx));
			writeq_relaxed(mask, base + CMN_DTM_WPn_MASK(wp_idx));
		}
	} else for_each_hw_dn(hw, dn, i) {
		int dtm_idx = arm_cmn_get_index(hw->dtm_idx, i);

		dn->event[dtm_idx] = CMN_EVENT_EVENTID(event);
		writel_relaxed(le32_to_cpu(dn->event_sel), dn->pmu_base + CMN_PMU_EVENT_SEL);
	}
}

static void arm_cmn_event_stop(struct perf_event *event, int flags)
{
	struct arm_cmn *cmn = to_cmn(event->pmu);
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	struct arm_cmn_node *dn;
	enum cmn_node_type type = CMN_EVENT_TYPE(event);
	int i;

	if (type == CMN_TYPE_DTC) {
		i = __ffs(hw->dtcs_used);
		cmn->dtc[i].cc_active = false;
	} else if (type == CMN_TYPE_WP) {
		int wp_idx = arm_cmn_wp_idx(event);

		for_each_hw_dn(hw, dn, i) {
			void __iomem *base = dn->pmu_base + CMN_DTM_OFFSET(hw->dtm_offset);

			writeq_relaxed(0, base + CMN_DTM_WPn_MASK(wp_idx));
			writeq_relaxed(~0ULL, base + CMN_DTM_WPn_VAL(wp_idx));
		}
	} else for_each_hw_dn(hw, dn, i) {
		int dtm_idx = arm_cmn_get_index(hw->dtm_idx, i);

		dn->event[dtm_idx] = 0;
		writel_relaxed(le32_to_cpu(dn->event_sel), dn->pmu_base + CMN_PMU_EVENT_SEL);
	}

	arm_cmn_event_read(event);
}

struct arm_cmn_val {
	u8 dtm_count[CMN_MAX_DTMS];
	u8 occupid[CMN_MAX_DTMS];
	u8 wp[CMN_MAX_DTMS][4];
	int dtc_count;
	bool cycles;
};

static void arm_cmn_val_add_event(struct arm_cmn *cmn, struct arm_cmn_val *val,
				  struct perf_event *event)
{
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	struct arm_cmn_node *dn;
	enum cmn_node_type type;
	int i;
	u8 occupid;

	if (is_software_event(event))
		return;

	type = CMN_EVENT_TYPE(event);
	if (type == CMN_TYPE_DTC) {
		val->cycles = true;
		return;
	}

	val->dtc_count++;
	if (arm_cmn_is_occup_event(cmn->model, type, CMN_EVENT_EVENTID(event)))
		occupid = CMN_EVENT_OCCUPID(event) + 1;
	else
		occupid = 0;

	for_each_hw_dn(hw, dn, i) {
		int wp_idx, dtm = dn->dtm;

		val->dtm_count[dtm]++;
		val->occupid[dtm] = occupid;

		if (type != CMN_TYPE_WP)
			continue;

		wp_idx = arm_cmn_wp_idx(event);
		val->wp[dtm][wp_idx] = CMN_EVENT_WP_COMBINE(event) + 1;
	}
}

static int arm_cmn_validate_group(struct arm_cmn *cmn, struct perf_event *event)
{
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	struct arm_cmn_node *dn;
	struct perf_event *sibling, *leader = event->group_leader;
	enum cmn_node_type type;
	struct arm_cmn_val *val;
	int i, ret = -EINVAL;
	u8 occupid;

	if (leader == event)
		return 0;

	if (event->pmu != leader->pmu && !is_software_event(leader))
		return -EINVAL;

	val = kzalloc(sizeof(*val), GFP_KERNEL);
	if (!val)
		return -ENOMEM;

	arm_cmn_val_add_event(cmn, val, leader);
	for_each_sibling_event(sibling, leader)
		arm_cmn_val_add_event(cmn, val, sibling);

	type = CMN_EVENT_TYPE(event);
	if (type == CMN_TYPE_DTC) {
		ret = val->cycles ? -EINVAL : 0;
		goto done;
	}

	if (val->dtc_count == CMN_DT_NUM_COUNTERS)
		goto done;

	if (arm_cmn_is_occup_event(cmn->model, type, CMN_EVENT_EVENTID(event)))
		occupid = CMN_EVENT_OCCUPID(event) + 1;
	else
		occupid = 0;

	for_each_hw_dn(hw, dn, i) {
		int wp_idx, wp_cmb, dtm = dn->dtm;

		if (val->dtm_count[dtm] == CMN_DTM_NUM_COUNTERS)
			goto done;

		if (occupid && val->occupid[dtm] && occupid != val->occupid[dtm])
			goto done;

		if (type != CMN_TYPE_WP)
			continue;

		wp_idx = arm_cmn_wp_idx(event);
		if (val->wp[dtm][wp_idx])
			goto done;

		wp_cmb = val->wp[dtm][wp_idx ^ 1];
		if (wp_cmb && wp_cmb != CMN_EVENT_WP_COMBINE(event) + 1)
			goto done;
	}

	ret = 0;
done:
	kfree(val);
	return ret;
}

static int arm_cmn_event_init(struct perf_event *event)
{
	struct arm_cmn *cmn = to_cmn(event->pmu);
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	struct arm_cmn_node *dn;
	enum cmn_node_type type;
	bool bynodeid;
	u16 nodeid, eventid;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EINVAL;

	event->cpu = cmn->cpu;
	if (event->cpu < 0)
		return -EINVAL;

	type = CMN_EVENT_TYPE(event);
	/* DTC events (i.e. cycles) already have everything they need */
	if (type == CMN_TYPE_DTC)
		return 0;

	/* For watchpoints we need the actual XP node here */
	if (type == CMN_TYPE_WP) {
		type = CMN_TYPE_XP;
		/* ...and we need a "real" direction */
		eventid = CMN_EVENT_EVENTID(event);
		if (eventid != CMN_WP_UP && eventid != CMN_WP_DOWN)
			return -EINVAL;
		/* ...but the DTM may depend on which port we're watching */
		if (cmn->multi_dtm)
			hw->dtm_offset = CMN_EVENT_WP_DEV_SEL(event) / 2;
	}

	bynodeid = CMN_EVENT_BYNODEID(event);
	nodeid = CMN_EVENT_NODEID(event);

	hw->dn = arm_cmn_node(cmn, type);
	if (!hw->dn)
		return -EINVAL;
	for (dn = hw->dn; dn->type == type; dn++) {
		if (bynodeid && dn->id != nodeid) {
			hw->dn++;
			continue;
		}
		hw->dtcs_used |= arm_cmn_node_to_xp(cmn, dn)->dtc;
		hw->num_dns++;
		if (bynodeid)
			break;
	}

	if (!hw->num_dns) {
		struct arm_cmn_nodeid nid = arm_cmn_nid(cmn, nodeid);

		dev_dbg(cmn->dev, "invalid node 0x%x (%d,%d,%d,%d) type 0x%x\n",
			nodeid, nid.x, nid.y, nid.port, nid.dev, type);
		return -EINVAL;
	}

	return arm_cmn_validate_group(cmn, event);
}

static void arm_cmn_event_clear(struct arm_cmn *cmn, struct perf_event *event,
				int i)
{
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	enum cmn_node_type type = CMN_EVENT_TYPE(event);

	while (i--) {
		struct arm_cmn_dtm *dtm = &cmn->dtms[hw->dn[i].dtm] + hw->dtm_offset;
		unsigned int dtm_idx = arm_cmn_get_index(hw->dtm_idx, i);

		if (type == CMN_TYPE_WP)
			dtm->wp_event[arm_cmn_wp_idx(event)] = -1;

		if (arm_cmn_is_occup_event(cmn->model, type, CMN_EVENT_EVENTID(event)))
			hw->dn[i].occupid_count--;

		dtm->pmu_config_low &= ~CMN__PMEVCNT_PAIRED(dtm_idx);
		writel_relaxed(dtm->pmu_config_low, dtm->base + CMN_DTM_PMU_CONFIG);
	}
	memset(hw->dtm_idx, 0, sizeof(hw->dtm_idx));

	for (i = 0; hw->dtcs_used & (1U << i); i++)
		cmn->dtc[i].counters[hw->dtc_idx] = NULL;
}

static int arm_cmn_event_add(struct perf_event *event, int flags)
{
	struct arm_cmn *cmn = to_cmn(event->pmu);
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	struct arm_cmn_dtc *dtc = &cmn->dtc[0];
	struct arm_cmn_node *dn;
	enum cmn_node_type type = CMN_EVENT_TYPE(event);
	unsigned int i, dtc_idx, input_sel;

	if (type == CMN_TYPE_DTC) {
		i = 0;
		while (cmn->dtc[i].cycles)
			if (++i == cmn->num_dtcs)
				return -ENOSPC;

		cmn->dtc[i].cycles = event;
		hw->dtc_idx = CMN_DT_NUM_COUNTERS;
		hw->dtcs_used = 1U << i;

		if (flags & PERF_EF_START)
			arm_cmn_event_start(event, 0);
		return 0;
	}

	/* Grab a free global counter first... */
	dtc_idx = 0;
	while (dtc->counters[dtc_idx])
		if (++dtc_idx == CMN_DT_NUM_COUNTERS)
			return -ENOSPC;

	hw->dtc_idx = dtc_idx;

	/* ...then the local counters to feed it. */
	for_each_hw_dn(hw, dn, i) {
		struct arm_cmn_dtm *dtm = &cmn->dtms[dn->dtm] + hw->dtm_offset;
		unsigned int dtm_idx, shift;
		u64 reg;

		dtm_idx = 0;
		while (dtm->pmu_config_low & CMN__PMEVCNT_PAIRED(dtm_idx))
			if (++dtm_idx == CMN_DTM_NUM_COUNTERS)
				goto free_dtms;

		if (type == CMN_TYPE_XP) {
			input_sel = CMN__PMEVCNT0_INPUT_SEL_XP + dtm_idx;
		} else if (type == CMN_TYPE_WP) {
			int tmp, wp_idx = arm_cmn_wp_idx(event);
			u32 cfg = arm_cmn_wp_config(event);

			if (dtm->wp_event[wp_idx] >= 0)
				goto free_dtms;

			tmp = dtm->wp_event[wp_idx ^ 1];
			if (tmp >= 0 && CMN_EVENT_WP_COMBINE(event) !=
					CMN_EVENT_WP_COMBINE(dtc->counters[tmp]))
				goto free_dtms;

			input_sel = CMN__PMEVCNT0_INPUT_SEL_WP + wp_idx;
			dtm->wp_event[wp_idx] = dtc_idx;
			writel_relaxed(cfg, dtm->base + CMN_DTM_WPn_CONFIG(wp_idx));
		} else {
			struct arm_cmn_nodeid nid = arm_cmn_nid(cmn, dn->id);

			if (cmn->multi_dtm)
				nid.port %= 2;

			input_sel = CMN__PMEVCNT0_INPUT_SEL_DEV + dtm_idx +
				    (nid.port << 4) + (nid.dev << 2);

			if (arm_cmn_is_occup_event(cmn->model, type, CMN_EVENT_EVENTID(event))) {
				u8 occupid = CMN_EVENT_OCCUPID(event);

				if (dn->occupid_count == 0) {
					dn->occupid_val = occupid;
					writel_relaxed(occupid,
						       dn->pmu_base + CMN_PMU_EVENT_SEL + 4);
				} else if (dn->occupid_val != occupid) {
					goto free_dtms;
				}
				dn->occupid_count++;
			}
		}

		arm_cmn_set_index(hw->dtm_idx, i, dtm_idx);

		dtm->input_sel[dtm_idx] = input_sel;
		shift = CMN__PMEVCNTn_GLOBAL_NUM_SHIFT(dtm_idx);
		dtm->pmu_config_low &= ~(CMN__PMEVCNT0_GLOBAL_NUM << shift);
		dtm->pmu_config_low |= FIELD_PREP(CMN__PMEVCNT0_GLOBAL_NUM, dtc_idx) << shift;
		dtm->pmu_config_low |= CMN__PMEVCNT_PAIRED(dtm_idx);
		reg = (u64)le32_to_cpu(dtm->pmu_config_high) << 32 | dtm->pmu_config_low;
		writeq_relaxed(reg, dtm->base + CMN_DTM_PMU_CONFIG);
	}

	/* Go go go! */
	arm_cmn_init_counter(event);

	if (flags & PERF_EF_START)
		arm_cmn_event_start(event, 0);

	return 0;

free_dtms:
	arm_cmn_event_clear(cmn, event, i);
	return -ENOSPC;
}

static void arm_cmn_event_del(struct perf_event *event, int flags)
{
	struct arm_cmn *cmn = to_cmn(event->pmu);
	struct arm_cmn_hw_event *hw = to_cmn_hw(event);
	enum cmn_node_type type = CMN_EVENT_TYPE(event);

	arm_cmn_event_stop(event, PERF_EF_UPDATE);

	if (type == CMN_TYPE_DTC)
		cmn->dtc[__ffs(hw->dtcs_used)].cycles = NULL;
	else
		arm_cmn_event_clear(cmn, event, hw->num_dns);
}

/*
 * We stop the PMU for both add and read, to avoid skew across DTM counters.
 * In theory we could use snapshots to read without stopping, but then it
 * becomes a lot trickier to deal with overlow and racing against interrupts,
 * plus it seems they don't work properly on some hardware anyway :(
 */
static void arm_cmn_start_txn(struct pmu *pmu, unsigned int flags)
{
	arm_cmn_set_state(to_cmn(pmu), CMN_STATE_TXN);
}

static void arm_cmn_end_txn(struct pmu *pmu)
{
	arm_cmn_clear_state(to_cmn(pmu), CMN_STATE_TXN);
}

static int arm_cmn_commit_txn(struct pmu *pmu)
{
	arm_cmn_end_txn(pmu);
	return 0;
}

static void arm_cmn_migrate(struct arm_cmn *cmn, unsigned int cpu)
{
	unsigned int i;

	perf_pmu_migrate_context(&cmn->pmu, cmn->cpu, cpu);
	for (i = 0; i < cmn->num_dtcs; i++)
		irq_set_affinity(cmn->dtc[i].irq, cpumask_of(cpu));
	cmn->cpu = cpu;
}

static int arm_cmn_pmu_online_cpu(unsigned int cpu, struct hlist_node *cpuhp_node)
{
	struct arm_cmn *cmn;
	int node;

	cmn = hlist_entry_safe(cpuhp_node, struct arm_cmn, cpuhp_node);
	node = dev_to_node(cmn->dev);
	if (node != NUMA_NO_NODE && cpu_to_node(cmn->cpu) != node && cpu_to_node(cpu) == node)
		arm_cmn_migrate(cmn, cpu);
	return 0;
}

static int arm_cmn_pmu_offline_cpu(unsigned int cpu, struct hlist_node *cpuhp_node)
{
	struct arm_cmn *cmn;
	unsigned int target;
	int node;
	cpumask_t mask;

	cmn = hlist_entry_safe(cpuhp_node, struct arm_cmn, cpuhp_node);
	if (cpu != cmn->cpu)
		return 0;

	node = dev_to_node(cmn->dev);
	if (cpumask_and(&mask, cpumask_of_node(node), cpu_online_mask) &&
	    cpumask_andnot(&mask, &mask, cpumask_of(cpu)))
		target = cpumask_any(&mask);
	else
		target = cpumask_any_but(cpu_online_mask, cpu);
	if (target < nr_cpu_ids)
		arm_cmn_migrate(cmn, target);
	return 0;
}

static irqreturn_t arm_cmn_handle_irq(int irq, void *dev_id)
{
	struct arm_cmn_dtc *dtc = dev_id;
	irqreturn_t ret = IRQ_NONE;

	for (;;) {
		u32 status = readl_relaxed(dtc->base + CMN_DT_PMOVSR);
		u64 delta;
		int i;

		for (i = 0; i < CMN_DTM_NUM_COUNTERS; i++) {
			if (status & (1U << i)) {
				ret = IRQ_HANDLED;
				if (WARN_ON(!dtc->counters[i]))
					continue;
				delta = (u64)arm_cmn_read_counter(dtc, i) << 16;
				local64_add(delta, &dtc->counters[i]->count);
			}
		}

		if (status & (1U << CMN_DT_NUM_COUNTERS)) {
			ret = IRQ_HANDLED;
			if (dtc->cc_active && !WARN_ON(!dtc->cycles)) {
				delta = arm_cmn_read_cc(dtc);
				local64_add(delta, &dtc->cycles->count);
			}
		}

		writel_relaxed(status, dtc->base + CMN_DT_PMOVSR_CLR);

		if (!dtc->irq_friend)
			return ret;
		dtc += dtc->irq_friend;
	}
}

/* We can reasonably accommodate DTCs of the same CMN sharing IRQs */
static int arm_cmn_init_irqs(struct arm_cmn *cmn)
{
	int i, j, irq, err;

	for (i = 0; i < cmn->num_dtcs; i++) {
		irq = cmn->dtc[i].irq;
		for (j = i; j--; ) {
			if (cmn->dtc[j].irq == irq) {
				cmn->dtc[j].irq_friend = i - j;
				goto next;
			}
		}
		err = devm_request_irq(cmn->dev, irq, arm_cmn_handle_irq,
				       IRQF_NOBALANCING | IRQF_NO_THREAD,
				       dev_name(cmn->dev), &cmn->dtc[i]);
		if (err)
			return err;

		err = irq_set_affinity(irq, cpumask_of(cmn->cpu));
		if (err)
			return err;
	next:
		; /* isn't C great? */
	}
	return 0;
}

static void arm_cmn_init_dtm(struct arm_cmn_dtm *dtm, struct arm_cmn_node *xp, int idx)
{
	int i;

	dtm->base = xp->pmu_base + CMN_DTM_OFFSET(idx);
	dtm->pmu_config_low = CMN_DTM_PMU_CONFIG_PMU_EN;
	for (i = 0; i < 4; i++) {
		dtm->wp_event[i] = -1;
		writeq_relaxed(0, dtm->base + CMN_DTM_WPn_MASK(i));
		writeq_relaxed(~0ULL, dtm->base + CMN_DTM_WPn_VAL(i));
	}
}

static int arm_cmn_init_dtc(struct arm_cmn *cmn, struct arm_cmn_node *dn, int idx)
{
	struct arm_cmn_dtc *dtc = cmn->dtc + idx;

	dtc->base = dn->pmu_base - CMN_PMU_OFFSET;
	dtc->irq = platform_get_irq(to_platform_device(cmn->dev), idx);
	if (dtc->irq < 0)
		return dtc->irq;

	writel_relaxed(0, dtc->base + CMN_DT_PMCR);
	writel_relaxed(0x1ff, dtc->base + CMN_DT_PMOVSR_CLR);
	writel_relaxed(CMN_DT_PMCR_OVFL_INTR_EN, dtc->base + CMN_DT_PMCR);

	return 0;
}

static int arm_cmn_node_cmp(const void *a, const void *b)
{
	const struct arm_cmn_node *dna = a, *dnb = b;
	int cmp;

	cmp = dna->type - dnb->type;
	if (!cmp)
		cmp = dna->logid - dnb->logid;
	return cmp;
}

static int arm_cmn_init_dtcs(struct arm_cmn *cmn)
{
	struct arm_cmn_node *dn, *xp;
	int dtc_idx = 0;
	u8 dtcs_present = (1 << cmn->num_dtcs) - 1;

	cmn->dtc = devm_kcalloc(cmn->dev, cmn->num_dtcs, sizeof(cmn->dtc[0]), GFP_KERNEL);
	if (!cmn->dtc)
		return -ENOMEM;

	sort(cmn->dns, cmn->num_dns, sizeof(cmn->dns[0]), arm_cmn_node_cmp, NULL);

	cmn->xps = arm_cmn_node(cmn, CMN_TYPE_XP);

	for (dn = cmn->dns; dn->type; dn++) {
		if (dn->type == CMN_TYPE_XP) {
			dn->dtc &= dtcs_present;
			continue;
		}

		xp = arm_cmn_node_to_xp(cmn, dn);
		dn->dtm = xp->dtm;
		if (cmn->multi_dtm)
			dn->dtm += arm_cmn_nid(cmn, dn->id).port / 2;

		if (dn->type == CMN_TYPE_DTC) {
			int err;
			/* We do at least know that a DTC's XP must be in that DTC's domain */
			if (xp->dtc == 0xf)
				xp->dtc = 1 << dtc_idx;
			err = arm_cmn_init_dtc(cmn, dn, dtc_idx++);
			if (err)
				return err;
		}

		/* To the PMU, RN-Ds don't add anything over RN-Is, so smoosh them together */
		if (dn->type == CMN_TYPE_RND)
			dn->type = CMN_TYPE_RNI;
	}

	writel_relaxed(CMN_DT_DTC_CTL_DT_EN, cmn->dtc[0].base + CMN_DT_DTC_CTL);

	return 0;
}

static void arm_cmn_init_node_info(struct arm_cmn *cmn, u32 offset, struct arm_cmn_node *node)
{
	int level;
	u64 reg = readq_relaxed(cmn->base + offset + CMN_NODE_INFO);

	node->type = FIELD_GET(CMN_NI_NODE_TYPE, reg);
	node->id = FIELD_GET(CMN_NI_NODE_ID, reg);
	node->logid = FIELD_GET(CMN_NI_LOGICAL_ID, reg);

	node->pmu_base = cmn->base + offset + CMN_PMU_OFFSET;

	if (node->type == CMN_TYPE_CFG)
		level = 0;
	else if (node->type == CMN_TYPE_XP)
		level = 1;
	else
		level = 2;

	dev_dbg(cmn->dev, "node%*c%#06hx%*ctype:%-#6x id:%-4hd off:%#x\n",
			(level * 2) + 1, ' ', node->id, 5 - (level * 2), ' ',
			node->type, node->logid, offset);
}

static int arm_cmn_discover(struct arm_cmn *cmn, unsigned int rgn_offset)
{
	void __iomem *cfg_region;
	struct arm_cmn_node cfg, *dn;
	struct arm_cmn_dtm *dtm;
	u16 child_count, child_poff;
	u32 xp_offset[CMN_MAX_XPS];
	u64 reg;
	int i, j;
	size_t sz;

	arm_cmn_init_node_info(cmn, rgn_offset, &cfg);
	if (cfg.type != CMN_TYPE_CFG)
		return -ENODEV;

	cfg_region = cmn->base + rgn_offset;
	reg = readl_relaxed(cfg_region + CMN_CFGM_PERIPH_ID_2);
	cmn->rev = FIELD_GET(CMN_CFGM_PID2_REVISION, reg);

	reg = readq_relaxed(cfg_region + CMN_CFGM_INFO_GLOBAL);
	cmn->multi_dtm = reg & CMN_INFO_MULTIPLE_DTM_EN;
	cmn->rsp_vc_num = FIELD_GET(CMN_INFO_RSP_VC_NUM, reg);
	cmn->dat_vc_num = FIELD_GET(CMN_INFO_DAT_VC_NUM, reg);

	reg = readq_relaxed(cfg_region + CMN_CHILD_INFO);
	child_count = FIELD_GET(CMN_CI_CHILD_COUNT, reg);
	child_poff = FIELD_GET(CMN_CI_CHILD_PTR_OFFSET, reg);

	cmn->num_xps = child_count;
	cmn->num_dns = cmn->num_xps;

	/* Pass 1: visit the XPs, enumerate their children */
	for (i = 0; i < cmn->num_xps; i++) {
		reg = readq_relaxed(cfg_region + child_poff + i * 8);
		xp_offset[i] = reg & CMN_CHILD_NODE_ADDR;

		reg = readq_relaxed(cmn->base + xp_offset[i] + CMN_CHILD_INFO);
		cmn->num_dns += FIELD_GET(CMN_CI_CHILD_COUNT, reg);
	}

	/* Cheeky +1 to help terminate pointer-based iteration later */
	dn = devm_kcalloc(cmn->dev, cmn->num_dns + 1, sizeof(*dn), GFP_KERNEL);
	if (!dn)
		return -ENOMEM;

	/* Initial safe upper bound on DTMs for any possible mesh layout */
	i = cmn->num_xps;
	if (cmn->multi_dtm)
		i += cmn->num_xps + 1;
	dtm = devm_kcalloc(cmn->dev, i, sizeof(*dtm), GFP_KERNEL);
	if (!dtm)
		return -ENOMEM;

	/* Pass 2: now we can actually populate the nodes */
	cmn->dns = dn;
	cmn->dtms = dtm;
	for (i = 0; i < cmn->num_xps; i++) {
		void __iomem *xp_region = cmn->base + xp_offset[i];
		struct arm_cmn_node *xp = dn++;
		unsigned int xp_ports = 0;

		arm_cmn_init_node_info(cmn, xp_offset[i], xp);
		/*
		 * Thanks to the order in which XP logical IDs seem to be
		 * assigned, we can handily infer the mesh X dimension by
		 * looking out for the XP at (0,1) without needing to know
		 * the exact node ID format, which we can later derive.
		 */
		if (xp->id == (1 << 3))
			cmn->mesh_x = xp->logid;

		if (cmn->model == CMN600)
			xp->dtc = 0xf;
		else
			xp->dtc = 1 << readl_relaxed(xp_region + CMN_DTM_UNIT_INFO);

		xp->dtm = dtm - cmn->dtms;
		arm_cmn_init_dtm(dtm++, xp, 0);
		/*
		 * Keeping track of connected ports will let us filter out
		 * unnecessary XP events easily. We can also reliably infer the
		 * "extra device ports" configuration for the node ID format
		 * from this, since in that case we will see at least one XP
		 * with port 2 connected, for the HN-D.
		 */
		if (readq_relaxed(xp_region + CMN_MXP__CONNECT_INFO_P0))
			xp_ports |= BIT(0);
		if (readq_relaxed(xp_region + CMN_MXP__CONNECT_INFO_P1))
			xp_ports |= BIT(1);
		if (readq_relaxed(xp_region + CMN_MXP__CONNECT_INFO_P2))
			xp_ports |= BIT(2);
		if (readq_relaxed(xp_region + CMN_MXP__CONNECT_INFO_P3))
			xp_ports |= BIT(3);
		if (readq_relaxed(xp_region + CMN_MXP__CONNECT_INFO_P4))
			xp_ports |= BIT(4);
		if (readq_relaxed(xp_region + CMN_MXP__CONNECT_INFO_P5))
			xp_ports |= BIT(5);

		if (cmn->multi_dtm && (xp_ports & 0xc))
			arm_cmn_init_dtm(dtm++, xp, 1);
		if (cmn->multi_dtm && (xp_ports & 0x30))
			arm_cmn_init_dtm(dtm++, xp, 2);

		cmn->ports_used |= xp_ports;

		reg = readq_relaxed(xp_region + CMN_CHILD_INFO);
		child_count = FIELD_GET(CMN_CI_CHILD_COUNT, reg);
		child_poff = FIELD_GET(CMN_CI_CHILD_PTR_OFFSET, reg);

		for (j = 0; j < child_count; j++) {
			reg = readq_relaxed(xp_region + child_poff + j * 8);
			/*
			 * Don't even try to touch anything external, since in general
			 * we haven't a clue how to power up arbitrary CHI requesters.
			 * As of CMN-600r1 these could only be RN-SAMs or CXLAs,
			 * neither of which have any PMU events anyway.
			 * (Actually, CXLAs do seem to have grown some events in r1p2,
			 * but they don't go to regular XP DTMs, and they depend on
			 * secure configuration which we can't easily deal with)
			 */
			if (reg & CMN_CHILD_NODE_EXTERNAL) {
				dev_dbg(cmn->dev, "ignoring external node %llx\n", reg);
				continue;
			}

			arm_cmn_init_node_info(cmn, reg & CMN_CHILD_NODE_ADDR, dn);

			switch (dn->type) {
			case CMN_TYPE_DTC:
				cmn->num_dtcs++;
				dn++;
				break;
			/* These guys have PMU events */
			case CMN_TYPE_DVM:
			case CMN_TYPE_HNI:
			case CMN_TYPE_HNF:
			case CMN_TYPE_SBSX:
			case CMN_TYPE_RNI:
			case CMN_TYPE_RND:
			case CMN_TYPE_MTSX:
			case CMN_TYPE_CXRA:
			case CMN_TYPE_CXHA:
				dn++;
				break;
			/* Nothing to see here */
			case CMN_TYPE_MPAM_S:
			case CMN_TYPE_MPAM_NS:
			case CMN_TYPE_RNSAM:
			case CMN_TYPE_CXLA:
				break;
			/* Something has gone horribly wrong */
			default:
				dev_err(cmn->dev, "invalid device node type: 0x%x\n", dn->type);
				return -ENODEV;
			}
		}
	}

	/* Correct for any nodes we skipped */
	cmn->num_dns = dn - cmn->dns;

	sz = (void *)(dn + 1) - (void *)cmn->dns;
	dn = devm_krealloc(cmn->dev, cmn->dns, sz, GFP_KERNEL);
	if (dn)
		cmn->dns = dn;

	sz = (void *)dtm - (void *)cmn->dtms;
	dtm = devm_krealloc(cmn->dev, cmn->dtms, sz, GFP_KERNEL);
	if (dtm)
		cmn->dtms = dtm;

	/*
	 * If mesh_x wasn't set during discovery then we never saw
	 * an XP at (0,1), thus we must have an Nx1 configuration.
	 */
	if (!cmn->mesh_x)
		cmn->mesh_x = cmn->num_xps;
	cmn->mesh_y = cmn->num_xps / cmn->mesh_x;

	/* 1x1 config plays havoc with XP event encodings */
	if (cmn->num_xps == 1)
		dev_warn(cmn->dev, "1x1 config not fully supported, translate XP events manually\n");

	dev_dbg(cmn->dev, "model %d, periph_id_2 revision %d\n", cmn->model, cmn->rev);
	reg = cmn->ports_used;
	dev_dbg(cmn->dev, "mesh %dx%d, ID width %d, ports %6pbl%s\n",
		cmn->mesh_x, cmn->mesh_y, arm_cmn_xyidbits(cmn), &reg,
		cmn->multi_dtm ? ", multi-DTM" : "");

	return 0;
}

static int arm_cmn600_acpi_probe(struct platform_device *pdev, struct arm_cmn *cmn)
{
	struct resource *cfg, *root;

	cfg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!cfg)
		return -EINVAL;

	root = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!root)
		return -EINVAL;

	if (!resource_contains(cfg, root))
		swap(cfg, root);
	/*
	 * Note that devm_ioremap_resource() is dumb and won't let the platform
	 * device claim cfg when the ACPI companion device has already claimed
	 * root within it. But since they *are* already both claimed in the
	 * appropriate name, we don't really need to do it again here anyway.
	 */
	cmn->base = devm_ioremap(cmn->dev, cfg->start, resource_size(cfg));
	if (!cmn->base)
		return -ENOMEM;

	return root->start - cfg->start;
}

static int arm_cmn600_of_probe(struct device_node *np)
{
	u32 rootnode;

	return of_property_read_u32(np, "arm,root-node", &rootnode) ?: rootnode;
}

static int arm_cmn_probe(struct platform_device *pdev)
{
	struct arm_cmn *cmn;
	const char *name;
	static atomic_t id;
	int err, rootnode, this_id;

	cmn = devm_kzalloc(&pdev->dev, sizeof(*cmn), GFP_KERNEL);
	if (!cmn)
		return -ENOMEM;

	cmn->dev = &pdev->dev;
	cmn->model = (unsigned long)device_get_match_data(cmn->dev);
	platform_set_drvdata(pdev, cmn);

	if (cmn->model == CMN600 && has_acpi_companion(cmn->dev)) {
		rootnode = arm_cmn600_acpi_probe(pdev, cmn);
	} else {
		rootnode = 0;
		cmn->base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(cmn->base))
			return PTR_ERR(cmn->base);
		if (cmn->model == CMN600)
			rootnode = arm_cmn600_of_probe(pdev->dev.of_node);
	}
	if (rootnode < 0)
		return rootnode;

	err = arm_cmn_discover(cmn, rootnode);
	if (err)
		return err;

	err = arm_cmn_init_dtcs(cmn);
	if (err)
		return err;

	err = arm_cmn_init_irqs(cmn);
	if (err)
		return err;

	cmn->cpu = cpumask_local_spread(0, dev_to_node(cmn->dev));
	cmn->pmu = (struct pmu) {
		.module = THIS_MODULE,
		.attr_groups = arm_cmn_attr_groups,
		.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
		.task_ctx_nr = perf_invalid_context,
		.pmu_enable = arm_cmn_pmu_enable,
		.pmu_disable = arm_cmn_pmu_disable,
		.event_init = arm_cmn_event_init,
		.add = arm_cmn_event_add,
		.del = arm_cmn_event_del,
		.start = arm_cmn_event_start,
		.stop = arm_cmn_event_stop,
		.read = arm_cmn_event_read,
		.start_txn = arm_cmn_start_txn,
		.commit_txn = arm_cmn_commit_txn,
		.cancel_txn = arm_cmn_end_txn,
	};

	this_id = atomic_fetch_inc(&id);
	name = devm_kasprintf(cmn->dev, GFP_KERNEL, "arm_cmn_%d", this_id);
	if (!name)
		return -ENOMEM;

	err = cpuhp_state_add_instance(arm_cmn_hp_state, &cmn->cpuhp_node);
	if (err)
		return err;

	err = perf_pmu_register(&cmn->pmu, name, -1);
	if (err)
		cpuhp_state_remove_instance_nocalls(arm_cmn_hp_state, &cmn->cpuhp_node);
	else
		arm_cmn_debugfs_init(cmn, this_id);

	return err;
}

static int arm_cmn_remove(struct platform_device *pdev)
{
	struct arm_cmn *cmn = platform_get_drvdata(pdev);

	writel_relaxed(0, cmn->dtc[0].base + CMN_DT_DTC_CTL);

	perf_pmu_unregister(&cmn->pmu);
	cpuhp_state_remove_instance_nocalls(arm_cmn_hp_state, &cmn->cpuhp_node);
	debugfs_remove(cmn->debug);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id arm_cmn_of_match[] = {
	{ .compatible = "arm,cmn-600", .data = (void *)CMN600 },
	{ .compatible = "arm,ci-700", .data = (void *)CI700 },
	{}
};
MODULE_DEVICE_TABLE(of, arm_cmn_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id arm_cmn_acpi_match[] = {
	{ "ARMHC600", CMN600 },
	{}
};
MODULE_DEVICE_TABLE(acpi, arm_cmn_acpi_match);
#endif

static struct platform_driver arm_cmn_driver = {
	.driver = {
		.name = "arm-cmn",
		.of_match_table = of_match_ptr(arm_cmn_of_match),
		.acpi_match_table = ACPI_PTR(arm_cmn_acpi_match),
	},
	.probe = arm_cmn_probe,
	.remove = arm_cmn_remove,
};

static int __init arm_cmn_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/arm/cmn:online",
				      arm_cmn_pmu_online_cpu,
				      arm_cmn_pmu_offline_cpu);
	if (ret < 0)
		return ret;

	arm_cmn_hp_state = ret;
	arm_cmn_debugfs = debugfs_create_dir("arm-cmn", NULL);

	ret = platform_driver_register(&arm_cmn_driver);
	if (ret) {
		cpuhp_remove_multi_state(arm_cmn_hp_state);
		debugfs_remove(arm_cmn_debugfs);
	}
	return ret;
}

static void __exit arm_cmn_exit(void)
{
	platform_driver_unregister(&arm_cmn_driver);
	cpuhp_remove_multi_state(arm_cmn_hp_state);
	debugfs_remove(arm_cmn_debugfs);
}

module_init(arm_cmn_init);
module_exit(arm_cmn_exit);

MODULE_AUTHOR("Robin Murphy <robin.murphy@arm.com>");
MODULE_DESCRIPTION("Arm CMN-600 PMU driver");
MODULE_LICENSE("GPL v2");
