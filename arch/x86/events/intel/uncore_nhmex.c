/* Nehalem-EX/Westmere-EX uncore support */
#include "uncore.h"

/* NHM-EX event control */
#define NHMEX_PMON_CTL_EV_SEL_MASK	0x000000ff
#define NHMEX_PMON_CTL_UMASK_MASK	0x0000ff00
#define NHMEX_PMON_CTL_EN_BIT0		(1 << 0)
#define NHMEX_PMON_CTL_EDGE_DET		(1 << 18)
#define NHMEX_PMON_CTL_PMI_EN		(1 << 20)
#define NHMEX_PMON_CTL_EN_BIT22		(1 << 22)
#define NHMEX_PMON_CTL_INVERT		(1 << 23)
#define NHMEX_PMON_CTL_TRESH_MASK	0xff000000
#define NHMEX_PMON_RAW_EVENT_MASK	(NHMEX_PMON_CTL_EV_SEL_MASK | \
					 NHMEX_PMON_CTL_UMASK_MASK | \
					 NHMEX_PMON_CTL_EDGE_DET | \
					 NHMEX_PMON_CTL_INVERT | \
					 NHMEX_PMON_CTL_TRESH_MASK)

/* NHM-EX Ubox */
#define NHMEX_U_MSR_PMON_GLOBAL_CTL		0xc00
#define NHMEX_U_MSR_PMON_CTR			0xc11
#define NHMEX_U_MSR_PMON_EV_SEL			0xc10

#define NHMEX_U_PMON_GLOBAL_EN			(1 << 0)
#define NHMEX_U_PMON_GLOBAL_PMI_CORE_SEL	0x0000001e
#define NHMEX_U_PMON_GLOBAL_EN_ALL		(1 << 28)
#define NHMEX_U_PMON_GLOBAL_RST_ALL		(1 << 29)
#define NHMEX_U_PMON_GLOBAL_FRZ_ALL		(1 << 31)

#define NHMEX_U_PMON_RAW_EVENT_MASK		\
		(NHMEX_PMON_CTL_EV_SEL_MASK |	\
		 NHMEX_PMON_CTL_EDGE_DET)

/* NHM-EX Cbox */
#define NHMEX_C0_MSR_PMON_GLOBAL_CTL		0xd00
#define NHMEX_C0_MSR_PMON_CTR0			0xd11
#define NHMEX_C0_MSR_PMON_EV_SEL0		0xd10
#define NHMEX_C_MSR_OFFSET			0x20

/* NHM-EX Bbox */
#define NHMEX_B0_MSR_PMON_GLOBAL_CTL		0xc20
#define NHMEX_B0_MSR_PMON_CTR0			0xc31
#define NHMEX_B0_MSR_PMON_CTL0			0xc30
#define NHMEX_B_MSR_OFFSET			0x40
#define NHMEX_B0_MSR_MATCH			0xe45
#define NHMEX_B0_MSR_MASK			0xe46
#define NHMEX_B1_MSR_MATCH			0xe4d
#define NHMEX_B1_MSR_MASK			0xe4e

#define NHMEX_B_PMON_CTL_EN			(1 << 0)
#define NHMEX_B_PMON_CTL_EV_SEL_SHIFT		1
#define NHMEX_B_PMON_CTL_EV_SEL_MASK		\
		(0x1f << NHMEX_B_PMON_CTL_EV_SEL_SHIFT)
#define NHMEX_B_PMON_CTR_SHIFT		6
#define NHMEX_B_PMON_CTR_MASK		\
		(0x3 << NHMEX_B_PMON_CTR_SHIFT)
#define NHMEX_B_PMON_RAW_EVENT_MASK		\
		(NHMEX_B_PMON_CTL_EV_SEL_MASK | \
		 NHMEX_B_PMON_CTR_MASK)

/* NHM-EX Sbox */
#define NHMEX_S0_MSR_PMON_GLOBAL_CTL		0xc40
#define NHMEX_S0_MSR_PMON_CTR0			0xc51
#define NHMEX_S0_MSR_PMON_CTL0			0xc50
#define NHMEX_S_MSR_OFFSET			0x80
#define NHMEX_S0_MSR_MM_CFG			0xe48
#define NHMEX_S0_MSR_MATCH			0xe49
#define NHMEX_S0_MSR_MASK			0xe4a
#define NHMEX_S1_MSR_MM_CFG			0xe58
#define NHMEX_S1_MSR_MATCH			0xe59
#define NHMEX_S1_MSR_MASK			0xe5a

#define NHMEX_S_PMON_MM_CFG_EN			(0x1ULL << 63)
#define NHMEX_S_EVENT_TO_R_PROG_EV		0

/* NHM-EX Mbox */
#define NHMEX_M0_MSR_GLOBAL_CTL			0xca0
#define NHMEX_M0_MSR_PMU_DSP			0xca5
#define NHMEX_M0_MSR_PMU_ISS			0xca6
#define NHMEX_M0_MSR_PMU_MAP			0xca7
#define NHMEX_M0_MSR_PMU_MSC_THR		0xca8
#define NHMEX_M0_MSR_PMU_PGT			0xca9
#define NHMEX_M0_MSR_PMU_PLD			0xcaa
#define NHMEX_M0_MSR_PMU_ZDP_CTL_FVC		0xcab
#define NHMEX_M0_MSR_PMU_CTL0			0xcb0
#define NHMEX_M0_MSR_PMU_CNT0			0xcb1
#define NHMEX_M_MSR_OFFSET			0x40
#define NHMEX_M0_MSR_PMU_MM_CFG			0xe54
#define NHMEX_M1_MSR_PMU_MM_CFG			0xe5c

#define NHMEX_M_PMON_MM_CFG_EN			(1ULL << 63)
#define NHMEX_M_PMON_ADDR_MATCH_MASK		0x3ffffffffULL
#define NHMEX_M_PMON_ADDR_MASK_MASK		0x7ffffffULL
#define NHMEX_M_PMON_ADDR_MASK_SHIFT		34

#define NHMEX_M_PMON_CTL_EN			(1 << 0)
#define NHMEX_M_PMON_CTL_PMI_EN			(1 << 1)
#define NHMEX_M_PMON_CTL_COUNT_MODE_SHIFT	2
#define NHMEX_M_PMON_CTL_COUNT_MODE_MASK	\
	(0x3 << NHMEX_M_PMON_CTL_COUNT_MODE_SHIFT)
#define NHMEX_M_PMON_CTL_STORAGE_MODE_SHIFT	4
#define NHMEX_M_PMON_CTL_STORAGE_MODE_MASK	\
	(0x3 << NHMEX_M_PMON_CTL_STORAGE_MODE_SHIFT)
#define NHMEX_M_PMON_CTL_WRAP_MODE		(1 << 6)
#define NHMEX_M_PMON_CTL_FLAG_MODE		(1 << 7)
#define NHMEX_M_PMON_CTL_INC_SEL_SHIFT		9
#define NHMEX_M_PMON_CTL_INC_SEL_MASK		\
	(0x1f << NHMEX_M_PMON_CTL_INC_SEL_SHIFT)
#define NHMEX_M_PMON_CTL_SET_FLAG_SEL_SHIFT	19
#define NHMEX_M_PMON_CTL_SET_FLAG_SEL_MASK	\
	(0x7 << NHMEX_M_PMON_CTL_SET_FLAG_SEL_SHIFT)
#define NHMEX_M_PMON_RAW_EVENT_MASK			\
		(NHMEX_M_PMON_CTL_COUNT_MODE_MASK |	\
		 NHMEX_M_PMON_CTL_STORAGE_MODE_MASK |	\
		 NHMEX_M_PMON_CTL_WRAP_MODE |		\
		 NHMEX_M_PMON_CTL_FLAG_MODE |		\
		 NHMEX_M_PMON_CTL_INC_SEL_MASK |	\
		 NHMEX_M_PMON_CTL_SET_FLAG_SEL_MASK)

#define NHMEX_M_PMON_ZDP_CTL_FVC_MASK		(((1 << 11) - 1) | (1 << 23))
#define NHMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(n)	(0x7ULL << (11 + 3 * (n)))

#define WSMEX_M_PMON_ZDP_CTL_FVC_MASK		(((1 << 12) - 1) | (1 << 24))
#define WSMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(n)	(0x7ULL << (12 + 3 * (n)))

/*
 * use the 9~13 bits to select event If the 7th bit is not set,
 * otherwise use the 19~21 bits to select event.
 */
#define MBOX_INC_SEL(x) ((x) << NHMEX_M_PMON_CTL_INC_SEL_SHIFT)
#define MBOX_SET_FLAG_SEL(x) (((x) << NHMEX_M_PMON_CTL_SET_FLAG_SEL_SHIFT) | \
				NHMEX_M_PMON_CTL_FLAG_MODE)
#define MBOX_INC_SEL_MASK (NHMEX_M_PMON_CTL_INC_SEL_MASK | \
			   NHMEX_M_PMON_CTL_FLAG_MODE)
#define MBOX_SET_FLAG_SEL_MASK (NHMEX_M_PMON_CTL_SET_FLAG_SEL_MASK | \
				NHMEX_M_PMON_CTL_FLAG_MODE)
#define MBOX_INC_SEL_EXTAR_REG(c, r) \
		EVENT_EXTRA_REG(MBOX_INC_SEL(c), NHMEX_M0_MSR_PMU_##r, \
				MBOX_INC_SEL_MASK, (u64)-1, NHMEX_M_##r)
#define MBOX_SET_FLAG_SEL_EXTRA_REG(c, r) \
		EVENT_EXTRA_REG(MBOX_SET_FLAG_SEL(c), NHMEX_M0_MSR_PMU_##r, \
				MBOX_SET_FLAG_SEL_MASK, \
				(u64)-1, NHMEX_M_##r)

/* NHM-EX Rbox */
#define NHMEX_R_MSR_GLOBAL_CTL			0xe00
#define NHMEX_R_MSR_PMON_CTL0			0xe10
#define NHMEX_R_MSR_PMON_CNT0			0xe11
#define NHMEX_R_MSR_OFFSET			0x20

#define NHMEX_R_MSR_PORTN_QLX_CFG(n)		\
		((n) < 4 ? (0xe0c + (n)) : (0xe2c + (n) - 4))
#define NHMEX_R_MSR_PORTN_IPERF_CFG0(n)		(0xe04 + (n))
#define NHMEX_R_MSR_PORTN_IPERF_CFG1(n)		(0xe24 + (n))
#define NHMEX_R_MSR_PORTN_XBR_OFFSET(n)		\
		(((n) < 4 ? 0 : 0x10) + (n) * 4)
#define NHMEX_R_MSR_PORTN_XBR_SET1_MM_CFG(n)	\
		(0xe60 + NHMEX_R_MSR_PORTN_XBR_OFFSET(n))
#define NHMEX_R_MSR_PORTN_XBR_SET1_MATCH(n)	\
		(NHMEX_R_MSR_PORTN_XBR_SET1_MM_CFG(n) + 1)
#define NHMEX_R_MSR_PORTN_XBR_SET1_MASK(n)	\
		(NHMEX_R_MSR_PORTN_XBR_SET1_MM_CFG(n) + 2)
#define NHMEX_R_MSR_PORTN_XBR_SET2_MM_CFG(n)	\
		(0xe70 + NHMEX_R_MSR_PORTN_XBR_OFFSET(n))
#define NHMEX_R_MSR_PORTN_XBR_SET2_MATCH(n)	\
		(NHMEX_R_MSR_PORTN_XBR_SET2_MM_CFG(n) + 1)
#define NHMEX_R_MSR_PORTN_XBR_SET2_MASK(n)	\
		(NHMEX_R_MSR_PORTN_XBR_SET2_MM_CFG(n) + 2)

#define NHMEX_R_PMON_CTL_EN			(1 << 0)
#define NHMEX_R_PMON_CTL_EV_SEL_SHIFT		1
#define NHMEX_R_PMON_CTL_EV_SEL_MASK		\
		(0x1f << NHMEX_R_PMON_CTL_EV_SEL_SHIFT)
#define NHMEX_R_PMON_CTL_PMI_EN			(1 << 6)
#define NHMEX_R_PMON_RAW_EVENT_MASK		NHMEX_R_PMON_CTL_EV_SEL_MASK

/* NHM-EX Wbox */
#define NHMEX_W_MSR_GLOBAL_CTL			0xc80
#define NHMEX_W_MSR_PMON_CNT0			0xc90
#define NHMEX_W_MSR_PMON_EVT_SEL0		0xc91
#define NHMEX_W_MSR_PMON_FIXED_CTR		0x394
#define NHMEX_W_MSR_PMON_FIXED_CTL		0x395

#define NHMEX_W_PMON_GLOBAL_FIXED_EN		(1ULL << 31)

#define __BITS_VALUE(x, i, n)  ((typeof(x))(((x) >> ((i) * (n))) & \
				((1ULL << (n)) - 1)))

DEFINE_UNCORE_FORMAT_ATTR(event, event, "config:0-7");
DEFINE_UNCORE_FORMAT_ATTR(event5, event, "config:1-5");
DEFINE_UNCORE_FORMAT_ATTR(umask, umask, "config:8-15");
DEFINE_UNCORE_FORMAT_ATTR(edge, edge, "config:18");
DEFINE_UNCORE_FORMAT_ATTR(inv, inv, "config:23");
DEFINE_UNCORE_FORMAT_ATTR(thresh8, thresh, "config:24-31");
DEFINE_UNCORE_FORMAT_ATTR(counter, counter, "config:6-7");
DEFINE_UNCORE_FORMAT_ATTR(match, match, "config1:0-63");
DEFINE_UNCORE_FORMAT_ATTR(mask, mask, "config2:0-63");

static void nhmex_uncore_msr_init_box(struct intel_uncore_box *box)
{
	wrmsrl(NHMEX_U_MSR_PMON_GLOBAL_CTL, NHMEX_U_PMON_GLOBAL_EN_ALL);
}

static void nhmex_uncore_msr_exit_box(struct intel_uncore_box *box)
{
	wrmsrl(NHMEX_U_MSR_PMON_GLOBAL_CTL, 0);
}

static void nhmex_uncore_msr_disable_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);
	u64 config;

	if (msr) {
		rdmsrl(msr, config);
		config &= ~((1ULL << uncore_num_counters(box)) - 1);
		/* WBox has a fixed counter */
		if (uncore_msr_fixed_ctl(box))
			config &= ~NHMEX_W_PMON_GLOBAL_FIXED_EN;
		wrmsrl(msr, config);
	}
}

static void nhmex_uncore_msr_enable_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);
	u64 config;

	if (msr) {
		rdmsrl(msr, config);
		config |= (1ULL << uncore_num_counters(box)) - 1;
		/* WBox has a fixed counter */
		if (uncore_msr_fixed_ctl(box))
			config |= NHMEX_W_PMON_GLOBAL_FIXED_EN;
		wrmsrl(msr, config);
	}
}

static void nhmex_uncore_msr_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	wrmsrl(event->hw.config_base, 0);
}

static void nhmex_uncore_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->idx >= UNCORE_PMC_IDX_FIXED)
		wrmsrl(hwc->config_base, NHMEX_PMON_CTL_EN_BIT0);
	else if (box->pmu->type->event_mask & NHMEX_PMON_CTL_EN_BIT0)
		wrmsrl(hwc->config_base, hwc->config | NHMEX_PMON_CTL_EN_BIT22);
	else
		wrmsrl(hwc->config_base, hwc->config | NHMEX_PMON_CTL_EN_BIT0);
}

#define NHMEX_UNCORE_OPS_COMMON_INIT()				\
	.init_box	= nhmex_uncore_msr_init_box,		\
	.exit_box	= nhmex_uncore_msr_exit_box,		\
	.disable_box	= nhmex_uncore_msr_disable_box,		\
	.enable_box	= nhmex_uncore_msr_enable_box,		\
	.disable_event	= nhmex_uncore_msr_disable_event,	\
	.read_counter	= uncore_msr_read_counter

static struct intel_uncore_ops nhmex_uncore_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event	= nhmex_uncore_msr_enable_event,
};

static struct attribute *nhmex_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_edge.attr,
	NULL,
};

static const struct attribute_group nhmex_uncore_ubox_format_group = {
	.name		= "format",
	.attrs		= nhmex_uncore_ubox_formats_attr,
};

static struct intel_uncore_type nhmex_uncore_ubox = {
	.name		= "ubox",
	.num_counters	= 1,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.event_ctl	= NHMEX_U_MSR_PMON_EV_SEL,
	.perf_ctr	= NHMEX_U_MSR_PMON_CTR,
	.event_mask	= NHMEX_U_PMON_RAW_EVENT_MASK,
	.box_ctl	= NHMEX_U_MSR_PMON_GLOBAL_CTL,
	.ops		= &nhmex_uncore_ops,
	.format_group	= &nhmex_uncore_ubox_format_group
};

static struct attribute *nhmex_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static const struct attribute_group nhmex_uncore_cbox_format_group = {
	.name = "format",
	.attrs = nhmex_uncore_cbox_formats_attr,
};

/* msr offset for each instance of cbox */
static unsigned nhmex_cbox_msr_offsets[] = {
	0x0, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x240, 0x2c0,
};

static struct intel_uncore_type nhmex_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 6,
	.num_boxes		= 10,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_C0_MSR_PMON_EV_SEL0,
	.perf_ctr		= NHMEX_C0_MSR_PMON_CTR0,
	.event_mask		= NHMEX_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_C0_MSR_PMON_GLOBAL_CTL,
	.msr_offsets		= nhmex_cbox_msr_offsets,
	.pair_ctr_ctl		= 1,
	.ops			= &nhmex_uncore_ops,
	.format_group		= &nhmex_uncore_cbox_format_group
};

static struct uncore_event_desc nhmex_uncore_wbox_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks, "event=0xff,umask=0"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_type nhmex_uncore_wbox = {
	.name			= "wbox",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_W_MSR_PMON_CNT0,
	.perf_ctr		= NHMEX_W_MSR_PMON_EVT_SEL0,
	.fixed_ctr		= NHMEX_W_MSR_PMON_FIXED_CTR,
	.fixed_ctl		= NHMEX_W_MSR_PMON_FIXED_CTL,
	.event_mask		= NHMEX_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_W_MSR_GLOBAL_CTL,
	.pair_ctr_ctl		= 1,
	.event_descs		= nhmex_uncore_wbox_events,
	.ops			= &nhmex_uncore_ops,
	.format_group		= &nhmex_uncore_cbox_format_group
};

static int nhmex_bbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;
	int ctr, ev_sel;

	ctr = (hwc->config & NHMEX_B_PMON_CTR_MASK) >>
		NHMEX_B_PMON_CTR_SHIFT;
	ev_sel = (hwc->config & NHMEX_B_PMON_CTL_EV_SEL_MASK) >>
		  NHMEX_B_PMON_CTL_EV_SEL_SHIFT;

	/* events that do not use the match/mask registers */
	if ((ctr == 0 && ev_sel > 0x3) || (ctr == 1 && ev_sel > 0x6) ||
	    (ctr == 2 && ev_sel != 0x4) || ctr == 3)
		return 0;

	if (box->pmu->pmu_idx == 0)
		reg1->reg = NHMEX_B0_MSR_MATCH;
	else
		reg1->reg = NHMEX_B1_MSR_MATCH;
	reg1->idx = 0;
	reg1->config = event->attr.config1;
	reg2->config = event->attr.config2;
	return 0;
}

static void nhmex_bbox_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		wrmsrl(reg1->reg, reg1->config);
		wrmsrl(reg1->reg + 1, reg2->config);
	}
	wrmsrl(hwc->config_base, NHMEX_PMON_CTL_EN_BIT0 |
		(hwc->config & NHMEX_B_PMON_CTL_EV_SEL_MASK));
}

/*
 * The Bbox has 4 counters, but each counter monitors different events.
 * Use bits 6-7 in the event config to select counter.
 */
static struct event_constraint nhmex_uncore_bbox_constraints[] = {
	EVENT_CONSTRAINT(0 , 1, 0xc0),
	EVENT_CONSTRAINT(0x40, 2, 0xc0),
	EVENT_CONSTRAINT(0x80, 4, 0xc0),
	EVENT_CONSTRAINT(0xc0, 8, 0xc0),
	EVENT_CONSTRAINT_END,
};

static struct attribute *nhmex_uncore_bbox_formats_attr[] = {
	&format_attr_event5.attr,
	&format_attr_counter.attr,
	&format_attr_match.attr,
	&format_attr_mask.attr,
	NULL,
};

static const struct attribute_group nhmex_uncore_bbox_format_group = {
	.name = "format",
	.attrs = nhmex_uncore_bbox_formats_attr,
};

static struct intel_uncore_ops nhmex_uncore_bbox_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event		= nhmex_bbox_msr_enable_event,
	.hw_config		= nhmex_bbox_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

static struct intel_uncore_type nhmex_uncore_bbox = {
	.name			= "bbox",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_B0_MSR_PMON_CTL0,
	.perf_ctr		= NHMEX_B0_MSR_PMON_CTR0,
	.event_mask		= NHMEX_B_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_B0_MSR_PMON_GLOBAL_CTL,
	.msr_offset		= NHMEX_B_MSR_OFFSET,
	.pair_ctr_ctl		= 1,
	.num_shared_regs	= 1,
	.constraints		= nhmex_uncore_bbox_constraints,
	.ops			= &nhmex_uncore_bbox_ops,
	.format_group		= &nhmex_uncore_bbox_format_group
};

static int nhmex_sbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	/* only TO_R_PROG_EV event uses the match/mask register */
	if ((hwc->config & NHMEX_PMON_CTL_EV_SEL_MASK) !=
	    NHMEX_S_EVENT_TO_R_PROG_EV)
		return 0;

	if (box->pmu->pmu_idx == 0)
		reg1->reg = NHMEX_S0_MSR_MM_CFG;
	else
		reg1->reg = NHMEX_S1_MSR_MM_CFG;
	reg1->idx = 0;
	reg1->config = event->attr.config1;
	reg2->config = event->attr.config2;
	return 0;
}

static void nhmex_sbox_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		wrmsrl(reg1->reg, 0);
		wrmsrl(reg1->reg + 1, reg1->config);
		wrmsrl(reg1->reg + 2, reg2->config);
		wrmsrl(reg1->reg, NHMEX_S_PMON_MM_CFG_EN);
	}
	wrmsrl(hwc->config_base, hwc->config | NHMEX_PMON_CTL_EN_BIT22);
}

static struct attribute *nhmex_uncore_sbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_match.attr,
	&format_attr_mask.attr,
	NULL,
};

static const struct attribute_group nhmex_uncore_sbox_format_group = {
	.name			= "format",
	.attrs			= nhmex_uncore_sbox_formats_attr,
};

static struct intel_uncore_ops nhmex_uncore_sbox_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event		= nhmex_sbox_msr_enable_event,
	.hw_config		= nhmex_sbox_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

static struct intel_uncore_type nhmex_uncore_sbox = {
	.name			= "sbox",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_S0_MSR_PMON_CTL0,
	.perf_ctr		= NHMEX_S0_MSR_PMON_CTR0,
	.event_mask		= NHMEX_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_S0_MSR_PMON_GLOBAL_CTL,
	.msr_offset		= NHMEX_S_MSR_OFFSET,
	.pair_ctr_ctl		= 1,
	.num_shared_regs	= 1,
	.ops			= &nhmex_uncore_sbox_ops,
	.format_group		= &nhmex_uncore_sbox_format_group
};

enum {
	EXTRA_REG_NHMEX_M_FILTER,
	EXTRA_REG_NHMEX_M_DSP,
	EXTRA_REG_NHMEX_M_ISS,
	EXTRA_REG_NHMEX_M_MAP,
	EXTRA_REG_NHMEX_M_MSC_THR,
	EXTRA_REG_NHMEX_M_PGT,
	EXTRA_REG_NHMEX_M_PLD,
	EXTRA_REG_NHMEX_M_ZDP_CTL_FVC,
};

static struct extra_reg nhmex_uncore_mbox_extra_regs[] = {
	MBOX_INC_SEL_EXTAR_REG(0x0, DSP),
	MBOX_INC_SEL_EXTAR_REG(0x4, MSC_THR),
	MBOX_INC_SEL_EXTAR_REG(0x5, MSC_THR),
	MBOX_INC_SEL_EXTAR_REG(0x9, ISS),
	/* event 0xa uses two extra registers */
	MBOX_INC_SEL_EXTAR_REG(0xa, ISS),
	MBOX_INC_SEL_EXTAR_REG(0xa, PLD),
	MBOX_INC_SEL_EXTAR_REG(0xb, PLD),
	/* events 0xd ~ 0x10 use the same extra register */
	MBOX_INC_SEL_EXTAR_REG(0xd, ZDP_CTL_FVC),
	MBOX_INC_SEL_EXTAR_REG(0xe, ZDP_CTL_FVC),
	MBOX_INC_SEL_EXTAR_REG(0xf, ZDP_CTL_FVC),
	MBOX_INC_SEL_EXTAR_REG(0x10, ZDP_CTL_FVC),
	MBOX_INC_SEL_EXTAR_REG(0x16, PGT),
	MBOX_SET_FLAG_SEL_EXTRA_REG(0x0, DSP),
	MBOX_SET_FLAG_SEL_EXTRA_REG(0x1, ISS),
	MBOX_SET_FLAG_SEL_EXTRA_REG(0x5, PGT),
	MBOX_SET_FLAG_SEL_EXTRA_REG(0x6, MAP),
	EVENT_EXTRA_END
};

/* Nehalem-EX or Westmere-EX ? */
static bool uncore_nhmex;

static bool nhmex_mbox_get_shared_reg(struct intel_uncore_box *box, int idx, u64 config)
{
	struct intel_uncore_extra_reg *er;
	unsigned long flags;
	bool ret = false;
	u64 mask;

	if (idx < EXTRA_REG_NHMEX_M_ZDP_CTL_FVC) {
		er = &box->shared_regs[idx];
		raw_spin_lock_irqsave(&er->lock, flags);
		if (!atomic_read(&er->ref) || er->config == config) {
			atomic_inc(&er->ref);
			er->config = config;
			ret = true;
		}
		raw_spin_unlock_irqrestore(&er->lock, flags);

		return ret;
	}
	/*
	 * The ZDP_CTL_FVC MSR has 4 fields which are used to control
	 * events 0xd ~ 0x10. Besides these 4 fields, there are additional
	 * fields which are shared.
	 */
	idx -= EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
	if (WARN_ON_ONCE(idx >= 4))
		return false;

	/* mask of the shared fields */
	if (uncore_nhmex)
		mask = NHMEX_M_PMON_ZDP_CTL_FVC_MASK;
	else
		mask = WSMEX_M_PMON_ZDP_CTL_FVC_MASK;
	er = &box->shared_regs[EXTRA_REG_NHMEX_M_ZDP_CTL_FVC];

	raw_spin_lock_irqsave(&er->lock, flags);
	/* add mask of the non-shared field if it's in use */
	if (__BITS_VALUE(atomic_read(&er->ref), idx, 8)) {
		if (uncore_nhmex)
			mask |= NHMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
		else
			mask |= WSMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
	}

	if (!atomic_read(&er->ref) || !((er->config ^ config) & mask)) {
		atomic_add(1 << (idx * 8), &er->ref);
		if (uncore_nhmex)
			mask = NHMEX_M_PMON_ZDP_CTL_FVC_MASK |
				NHMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
		else
			mask = WSMEX_M_PMON_ZDP_CTL_FVC_MASK |
				WSMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
		er->config &= ~mask;
		er->config |= (config & mask);
		ret = true;
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	return ret;
}

static void nhmex_mbox_put_shared_reg(struct intel_uncore_box *box, int idx)
{
	struct intel_uncore_extra_reg *er;

	if (idx < EXTRA_REG_NHMEX_M_ZDP_CTL_FVC) {
		er = &box->shared_regs[idx];
		atomic_dec(&er->ref);
		return;
	}

	idx -= EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
	er = &box->shared_regs[EXTRA_REG_NHMEX_M_ZDP_CTL_FVC];
	atomic_sub(1 << (idx * 8), &er->ref);
}

static u64 nhmex_mbox_alter_er(struct perf_event *event, int new_idx, bool modify)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	u64 idx, orig_idx = __BITS_VALUE(reg1->idx, 0, 8);
	u64 config = reg1->config;

	/* get the non-shared control bits and shift them */
	idx = orig_idx - EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
	if (uncore_nhmex)
		config &= NHMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
	else
		config &= WSMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
	if (new_idx > orig_idx) {
		idx = new_idx - orig_idx;
		config <<= 3 * idx;
	} else {
		idx = orig_idx - new_idx;
		config >>= 3 * idx;
	}

	/* add the shared control bits back */
	if (uncore_nhmex)
		config |= NHMEX_M_PMON_ZDP_CTL_FVC_MASK & reg1->config;
	else
		config |= WSMEX_M_PMON_ZDP_CTL_FVC_MASK & reg1->config;
	config |= NHMEX_M_PMON_ZDP_CTL_FVC_MASK & reg1->config;
	if (modify) {
		/* adjust the main event selector */
		if (new_idx > orig_idx)
			hwc->config += idx << NHMEX_M_PMON_CTL_INC_SEL_SHIFT;
		else
			hwc->config -= idx << NHMEX_M_PMON_CTL_INC_SEL_SHIFT;
		reg1->config = config;
		reg1->idx = ~0xff | new_idx;
	}
	return config;
}

static struct event_constraint *
nhmex_mbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;
	int i, idx[2], alloc = 0;
	u64 config1 = reg1->config;

	idx[0] = __BITS_VALUE(reg1->idx, 0, 8);
	idx[1] = __BITS_VALUE(reg1->idx, 1, 8);
again:
	for (i = 0; i < 2; i++) {
		if (!uncore_box_is_fake(box) && (reg1->alloc & (0x1 << i)))
			idx[i] = 0xff;

		if (idx[i] == 0xff)
			continue;

		if (!nhmex_mbox_get_shared_reg(box, idx[i],
				__BITS_VALUE(config1, i, 32)))
			goto fail;
		alloc |= (0x1 << i);
	}

	/* for the match/mask registers */
	if (reg2->idx != EXTRA_REG_NONE &&
	    (uncore_box_is_fake(box) || !reg2->alloc) &&
	    !nhmex_mbox_get_shared_reg(box, reg2->idx, reg2->config))
		goto fail;

	/*
	 * If it's a fake box -- as per validate_{group,event}() we
	 * shouldn't touch event state and we can avoid doing so
	 * since both will only call get_event_constraints() once
	 * on each event, this avoids the need for reg->alloc.
	 */
	if (!uncore_box_is_fake(box)) {
		if (idx[0] != 0xff && idx[0] != __BITS_VALUE(reg1->idx, 0, 8))
			nhmex_mbox_alter_er(event, idx[0], true);
		reg1->alloc |= alloc;
		if (reg2->idx != EXTRA_REG_NONE)
			reg2->alloc = 1;
	}
	return NULL;
fail:
	if (idx[0] != 0xff && !(alloc & 0x1) &&
	    idx[0] >= EXTRA_REG_NHMEX_M_ZDP_CTL_FVC) {
		/*
		 * events 0xd ~ 0x10 are functional identical, but are
		 * controlled by different fields in the ZDP_CTL_FVC
		 * register. If we failed to take one field, try the
		 * rest 3 choices.
		 */
		BUG_ON(__BITS_VALUE(reg1->idx, 1, 8) != 0xff);
		idx[0] -= EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
		idx[0] = (idx[0] + 1) % 4;
		idx[0] += EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
		if (idx[0] != __BITS_VALUE(reg1->idx, 0, 8)) {
			config1 = nhmex_mbox_alter_er(event, idx[0], false);
			goto again;
		}
	}

	if (alloc & 0x1)
		nhmex_mbox_put_shared_reg(box, idx[0]);
	if (alloc & 0x2)
		nhmex_mbox_put_shared_reg(box, idx[1]);
	return &uncore_constraint_empty;
}

static void nhmex_mbox_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;

	if (uncore_box_is_fake(box))
		return;

	if (reg1->alloc & 0x1)
		nhmex_mbox_put_shared_reg(box, __BITS_VALUE(reg1->idx, 0, 8));
	if (reg1->alloc & 0x2)
		nhmex_mbox_put_shared_reg(box, __BITS_VALUE(reg1->idx, 1, 8));
	reg1->alloc = 0;

	if (reg2->alloc) {
		nhmex_mbox_put_shared_reg(box, reg2->idx);
		reg2->alloc = 0;
	}
}

static int nhmex_mbox_extra_reg_idx(struct extra_reg *er)
{
	if (er->idx < EXTRA_REG_NHMEX_M_ZDP_CTL_FVC)
		return er->idx;
	return er->idx + (er->event >> NHMEX_M_PMON_CTL_INC_SEL_SHIFT) - 0xd;
}

static int nhmex_mbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_type *type = box->pmu->type;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;
	struct extra_reg *er;
	unsigned msr;
	int reg_idx = 0;
	/*
	 * The mbox events may require 2 extra MSRs at the most. But only
	 * the lower 32 bits in these MSRs are significant, so we can use
	 * config1 to pass two MSRs' config.
	 */
	for (er = nhmex_uncore_mbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		if (event->attr.config1 & ~er->valid_mask)
			return -EINVAL;

		msr = er->msr + type->msr_offset * box->pmu->pmu_idx;
		if (WARN_ON_ONCE(msr >= 0xffff || er->idx >= 0xff))
			return -EINVAL;

		/* always use the 32~63 bits to pass the PLD config */
		if (er->idx == EXTRA_REG_NHMEX_M_PLD)
			reg_idx = 1;
		else if (WARN_ON_ONCE(reg_idx > 0))
			return -EINVAL;

		reg1->idx &= ~(0xff << (reg_idx * 8));
		reg1->reg &= ~(0xffff << (reg_idx * 16));
		reg1->idx |= nhmex_mbox_extra_reg_idx(er) << (reg_idx * 8);
		reg1->reg |= msr << (reg_idx * 16);
		reg1->config = event->attr.config1;
		reg_idx++;
	}
	/*
	 * The mbox only provides ability to perform address matching
	 * for the PLD events.
	 */
	if (reg_idx == 2) {
		reg2->idx = EXTRA_REG_NHMEX_M_FILTER;
		if (event->attr.config2 & NHMEX_M_PMON_MM_CFG_EN)
			reg2->config = event->attr.config2;
		else
			reg2->config = ~0ULL;
		if (box->pmu->pmu_idx == 0)
			reg2->reg = NHMEX_M0_MSR_PMU_MM_CFG;
		else
			reg2->reg = NHMEX_M1_MSR_PMU_MM_CFG;
	}
	return 0;
}

static u64 nhmex_mbox_shared_reg_config(struct intel_uncore_box *box, int idx)
{
	struct intel_uncore_extra_reg *er;
	unsigned long flags;
	u64 config;

	if (idx < EXTRA_REG_NHMEX_M_ZDP_CTL_FVC)
		return box->shared_regs[idx].config;

	er = &box->shared_regs[EXTRA_REG_NHMEX_M_ZDP_CTL_FVC];
	raw_spin_lock_irqsave(&er->lock, flags);
	config = er->config;
	raw_spin_unlock_irqrestore(&er->lock, flags);
	return config;
}

static void nhmex_mbox_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;
	int idx;

	idx = __BITS_VALUE(reg1->idx, 0, 8);
	if (idx != 0xff)
		wrmsrl(__BITS_VALUE(reg1->reg, 0, 16),
			nhmex_mbox_shared_reg_config(box, idx));
	idx = __BITS_VALUE(reg1->idx, 1, 8);
	if (idx != 0xff)
		wrmsrl(__BITS_VALUE(reg1->reg, 1, 16),
			nhmex_mbox_shared_reg_config(box, idx));

	if (reg2->idx != EXTRA_REG_NONE) {
		wrmsrl(reg2->reg, 0);
		if (reg2->config != ~0ULL) {
			wrmsrl(reg2->reg + 1,
				reg2->config & NHMEX_M_PMON_ADDR_MATCH_MASK);
			wrmsrl(reg2->reg + 2, NHMEX_M_PMON_ADDR_MASK_MASK &
				(reg2->config >> NHMEX_M_PMON_ADDR_MASK_SHIFT));
			wrmsrl(reg2->reg, NHMEX_M_PMON_MM_CFG_EN);
		}
	}

	wrmsrl(hwc->config_base, hwc->config | NHMEX_PMON_CTL_EN_BIT0);
}

DEFINE_UNCORE_FORMAT_ATTR(count_mode,		count_mode,	"config:2-3");
DEFINE_UNCORE_FORMAT_ATTR(storage_mode,		storage_mode,	"config:4-5");
DEFINE_UNCORE_FORMAT_ATTR(wrap_mode,		wrap_mode,	"config:6");
DEFINE_UNCORE_FORMAT_ATTR(flag_mode,		flag_mode,	"config:7");
DEFINE_UNCORE_FORMAT_ATTR(inc_sel,		inc_sel,	"config:9-13");
DEFINE_UNCORE_FORMAT_ATTR(set_flag_sel,		set_flag_sel,	"config:19-21");
DEFINE_UNCORE_FORMAT_ATTR(filter_cfg_en,	filter_cfg_en,	"config2:63");
DEFINE_UNCORE_FORMAT_ATTR(filter_match,		filter_match,	"config2:0-33");
DEFINE_UNCORE_FORMAT_ATTR(filter_mask,		filter_mask,	"config2:34-61");
DEFINE_UNCORE_FORMAT_ATTR(dsp,			dsp,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(thr,			thr,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(fvc,			fvc,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(pgt,			pgt,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(map,			map,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(iss,			iss,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(pld,			pld,		"config1:32-63");

static struct attribute *nhmex_uncore_mbox_formats_attr[] = {
	&format_attr_count_mode.attr,
	&format_attr_storage_mode.attr,
	&format_attr_wrap_mode.attr,
	&format_attr_flag_mode.attr,
	&format_attr_inc_sel.attr,
	&format_attr_set_flag_sel.attr,
	&format_attr_filter_cfg_en.attr,
	&format_attr_filter_match.attr,
	&format_attr_filter_mask.attr,
	&format_attr_dsp.attr,
	&format_attr_thr.attr,
	&format_attr_fvc.attr,
	&format_attr_pgt.attr,
	&format_attr_map.attr,
	&format_attr_iss.attr,
	&format_attr_pld.attr,
	NULL,
};

static const struct attribute_group nhmex_uncore_mbox_format_group = {
	.name		= "format",
	.attrs		= nhmex_uncore_mbox_formats_attr,
};

static struct uncore_event_desc nhmex_uncore_mbox_events[] = {
	INTEL_UNCORE_EVENT_DESC(bbox_cmds_read, "inc_sel=0xd,fvc=0x2800"),
	INTEL_UNCORE_EVENT_DESC(bbox_cmds_write, "inc_sel=0xd,fvc=0x2820"),
	{ /* end: all zeroes */ },
};

static struct uncore_event_desc wsmex_uncore_mbox_events[] = {
	INTEL_UNCORE_EVENT_DESC(bbox_cmds_read, "inc_sel=0xd,fvc=0x5000"),
	INTEL_UNCORE_EVENT_DESC(bbox_cmds_write, "inc_sel=0xd,fvc=0x5040"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_ops nhmex_uncore_mbox_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event	= nhmex_mbox_msr_enable_event,
	.hw_config	= nhmex_mbox_hw_config,
	.get_constraint	= nhmex_mbox_get_constraint,
	.put_constraint	= nhmex_mbox_put_constraint,
};

static struct intel_uncore_type nhmex_uncore_mbox = {
	.name			= "mbox",
	.num_counters		= 6,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_M0_MSR_PMU_CTL0,
	.perf_ctr		= NHMEX_M0_MSR_PMU_CNT0,
	.event_mask		= NHMEX_M_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_M0_MSR_GLOBAL_CTL,
	.msr_offset		= NHMEX_M_MSR_OFFSET,
	.pair_ctr_ctl		= 1,
	.num_shared_regs	= 8,
	.event_descs		= nhmex_uncore_mbox_events,
	.ops			= &nhmex_uncore_mbox_ops,
	.format_group		= &nhmex_uncore_mbox_format_group,
};

static void nhmex_rbox_alter_er(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	/* adjust the main event selector and extra register index */
	if (reg1->idx % 2) {
		reg1->idx--;
		hwc->config -= 1 << NHMEX_R_PMON_CTL_EV_SEL_SHIFT;
	} else {
		reg1->idx++;
		hwc->config += 1 << NHMEX_R_PMON_CTL_EV_SEL_SHIFT;
	}

	/* adjust extra register config */
	switch (reg1->idx % 6) {
	case 2:
		/* shift the 8~15 bits to the 0~7 bits */
		reg1->config >>= 8;
		break;
	case 3:
		/* shift the 0~7 bits to the 8~15 bits */
		reg1->config <<= 8;
		break;
	}
}

/*
 * Each rbox has 4 event set which monitor PQI port 0~3 or 4~7.
 * An event set consists of 6 events, the 3rd and 4th events in
 * an event set use the same extra register. So an event set uses
 * 5 extra registers.
 */
static struct event_constraint *
nhmex_rbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;
	struct intel_uncore_extra_reg *er;
	unsigned long flags;
	int idx, er_idx;
	u64 config1;
	bool ok = false;

	if (!uncore_box_is_fake(box) && reg1->alloc)
		return NULL;

	idx = reg1->idx % 6;
	config1 = reg1->config;
again:
	er_idx = idx;
	/* the 3rd and 4th events use the same extra register */
	if (er_idx > 2)
		er_idx--;
	er_idx += (reg1->idx / 6) * 5;

	er = &box->shared_regs[er_idx];
	raw_spin_lock_irqsave(&er->lock, flags);
	if (idx < 2) {
		if (!atomic_read(&er->ref) || er->config == reg1->config) {
			atomic_inc(&er->ref);
			er->config = reg1->config;
			ok = true;
		}
	} else if (idx == 2 || idx == 3) {
		/*
		 * these two events use different fields in a extra register,
		 * the 0~7 bits and the 8~15 bits respectively.
		 */
		u64 mask = 0xff << ((idx - 2) * 8);
		if (!__BITS_VALUE(atomic_read(&er->ref), idx - 2, 8) ||
				!((er->config ^ config1) & mask)) {
			atomic_add(1 << ((idx - 2) * 8), &er->ref);
			er->config &= ~mask;
			er->config |= config1 & mask;
			ok = true;
		}
	} else {
		if (!atomic_read(&er->ref) ||
				(er->config == (hwc->config >> 32) &&
				 er->config1 == reg1->config &&
				 er->config2 == reg2->config)) {
			atomic_inc(&er->ref);
			er->config = (hwc->config >> 32);
			er->config1 = reg1->config;
			er->config2 = reg2->config;
			ok = true;
		}
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	if (!ok) {
		/*
		 * The Rbox events are always in pairs. The paired
		 * events are functional identical, but use different
		 * extra registers. If we failed to take an extra
		 * register, try the alternative.
		 */
		idx ^= 1;
		if (idx != reg1->idx % 6) {
			if (idx == 2)
				config1 >>= 8;
			else if (idx == 3)
				config1 <<= 8;
			goto again;
		}
	} else {
		if (!uncore_box_is_fake(box)) {
			if (idx != reg1->idx % 6)
				nhmex_rbox_alter_er(box, event);
			reg1->alloc = 1;
		}
		return NULL;
	}
	return &uncore_constraint_empty;
}

static void nhmex_rbox_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_extra_reg *er;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	int idx, er_idx;

	if (uncore_box_is_fake(box) || !reg1->alloc)
		return;

	idx = reg1->idx % 6;
	er_idx = idx;
	if (er_idx > 2)
		er_idx--;
	er_idx += (reg1->idx / 6) * 5;

	er = &box->shared_regs[er_idx];
	if (idx == 2 || idx == 3)
		atomic_sub(1 << ((idx - 2) * 8), &er->ref);
	else
		atomic_dec(&er->ref);

	reg1->alloc = 0;
}

static int nhmex_rbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;
	int idx;

	idx = (event->hw.config & NHMEX_R_PMON_CTL_EV_SEL_MASK) >>
		NHMEX_R_PMON_CTL_EV_SEL_SHIFT;
	if (idx >= 0x18)
		return -EINVAL;

	reg1->idx = idx;
	reg1->config = event->attr.config1;

	switch (idx % 6) {
	case 4:
	case 5:
		hwc->config |= event->attr.config & (~0ULL << 32);
		reg2->config = event->attr.config2;
		break;
	}
	return 0;
}

static void nhmex_rbox_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;
	int idx, port;

	idx = reg1->idx;
	port = idx / 6 + box->pmu->pmu_idx * 4;

	switch (idx % 6) {
	case 0:
		wrmsrl(NHMEX_R_MSR_PORTN_IPERF_CFG0(port), reg1->config);
		break;
	case 1:
		wrmsrl(NHMEX_R_MSR_PORTN_IPERF_CFG1(port), reg1->config);
		break;
	case 2:
	case 3:
		wrmsrl(NHMEX_R_MSR_PORTN_QLX_CFG(port),
			uncore_shared_reg_config(box, 2 + (idx / 6) * 5));
		break;
	case 4:
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET1_MM_CFG(port),
			hwc->config >> 32);
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET1_MATCH(port), reg1->config);
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET1_MASK(port), reg2->config);
		break;
	case 5:
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET2_MM_CFG(port),
			hwc->config >> 32);
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET2_MATCH(port), reg1->config);
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET2_MASK(port), reg2->config);
		break;
	}

	wrmsrl(hwc->config_base, NHMEX_PMON_CTL_EN_BIT0 |
		(hwc->config & NHMEX_R_PMON_CTL_EV_SEL_MASK));
}

DEFINE_UNCORE_FORMAT_ATTR(xbr_mm_cfg, xbr_mm_cfg, "config:32-63");
DEFINE_UNCORE_FORMAT_ATTR(xbr_match, xbr_match, "config1:0-63");
DEFINE_UNCORE_FORMAT_ATTR(xbr_mask, xbr_mask, "config2:0-63");
DEFINE_UNCORE_FORMAT_ATTR(qlx_cfg, qlx_cfg, "config1:0-15");
DEFINE_UNCORE_FORMAT_ATTR(iperf_cfg, iperf_cfg, "config1:0-31");

static struct attribute *nhmex_uncore_rbox_formats_attr[] = {
	&format_attr_event5.attr,
	&format_attr_xbr_mm_cfg.attr,
	&format_attr_xbr_match.attr,
	&format_attr_xbr_mask.attr,
	&format_attr_qlx_cfg.attr,
	&format_attr_iperf_cfg.attr,
	NULL,
};

static const struct attribute_group nhmex_uncore_rbox_format_group = {
	.name = "format",
	.attrs = nhmex_uncore_rbox_formats_attr,
};

static struct uncore_event_desc nhmex_uncore_rbox_events[] = {
	INTEL_UNCORE_EVENT_DESC(qpi0_flit_send,		"event=0x0,iperf_cfg=0x80000000"),
	INTEL_UNCORE_EVENT_DESC(qpi1_filt_send,		"event=0x6,iperf_cfg=0x80000000"),
	INTEL_UNCORE_EVENT_DESC(qpi0_idle_filt,		"event=0x0,iperf_cfg=0x40000000"),
	INTEL_UNCORE_EVENT_DESC(qpi1_idle_filt,		"event=0x6,iperf_cfg=0x40000000"),
	INTEL_UNCORE_EVENT_DESC(qpi0_date_response,	"event=0x0,iperf_cfg=0xc4"),
	INTEL_UNCORE_EVENT_DESC(qpi1_date_response,	"event=0x6,iperf_cfg=0xc4"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_ops nhmex_uncore_rbox_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event		= nhmex_rbox_msr_enable_event,
	.hw_config		= nhmex_rbox_hw_config,
	.get_constraint		= nhmex_rbox_get_constraint,
	.put_constraint		= nhmex_rbox_put_constraint,
};

static struct intel_uncore_type nhmex_uncore_rbox = {
	.name			= "rbox",
	.num_counters		= 8,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_R_MSR_PMON_CTL0,
	.perf_ctr		= NHMEX_R_MSR_PMON_CNT0,
	.event_mask		= NHMEX_R_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_R_MSR_GLOBAL_CTL,
	.msr_offset		= NHMEX_R_MSR_OFFSET,
	.pair_ctr_ctl		= 1,
	.num_shared_regs	= 20,
	.event_descs		= nhmex_uncore_rbox_events,
	.ops			= &nhmex_uncore_rbox_ops,
	.format_group		= &nhmex_uncore_rbox_format_group
};

static struct intel_uncore_type *nhmex_msr_uncores[] = {
	&nhmex_uncore_ubox,
	&nhmex_uncore_cbox,
	&nhmex_uncore_bbox,
	&nhmex_uncore_sbox,
	&nhmex_uncore_mbox,
	&nhmex_uncore_rbox,
	&nhmex_uncore_wbox,
	NULL,
};

void nhmex_uncore_cpu_init(void)
{
	if (boot_cpu_data.x86_model == 46)
		uncore_nhmex = true;
	else
		nhmex_uncore_mbox.event_descs = wsmex_uncore_mbox_events;
	if (nhmex_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		nhmex_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;
	uncore_msr_uncores = nhmex_msr_uncores;
}
/* end of Nehalem-EX uncore support */
