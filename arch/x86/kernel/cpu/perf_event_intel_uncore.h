#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/perf_event.h>
#include "perf_event.h"

#define UNCORE_PMU_NAME_LEN		32
#define UNCORE_BOX_HASH_SIZE		8

#define UNCORE_PMU_HRTIMER_INTERVAL	(60 * NSEC_PER_SEC)

#define UNCORE_FIXED_EVENT		0xff
#define UNCORE_PMC_IDX_MAX_GENERIC	8
#define UNCORE_PMC_IDX_FIXED		UNCORE_PMC_IDX_MAX_GENERIC
#define UNCORE_PMC_IDX_MAX		(UNCORE_PMC_IDX_FIXED + 1)

#define UNCORE_EVENT_CONSTRAINT(c, n) EVENT_CONSTRAINT(c, n, 0xff)

/* SNB event control */
#define SNB_UNC_CTL_EV_SEL_MASK			0x000000ff
#define SNB_UNC_CTL_UMASK_MASK			0x0000ff00
#define SNB_UNC_CTL_EDGE_DET			(1 << 18)
#define SNB_UNC_CTL_EN				(1 << 22)
#define SNB_UNC_CTL_INVERT			(1 << 23)
#define SNB_UNC_CTL_CMASK_MASK			0x1f000000
#define NHM_UNC_CTL_CMASK_MASK			0xff000000
#define NHM_UNC_FIXED_CTR_CTL_EN		(1 << 0)

#define SNB_UNC_RAW_EVENT_MASK			(SNB_UNC_CTL_EV_SEL_MASK | \
						 SNB_UNC_CTL_UMASK_MASK | \
						 SNB_UNC_CTL_EDGE_DET | \
						 SNB_UNC_CTL_INVERT | \
						 SNB_UNC_CTL_CMASK_MASK)

#define NHM_UNC_RAW_EVENT_MASK			(SNB_UNC_CTL_EV_SEL_MASK | \
						 SNB_UNC_CTL_UMASK_MASK | \
						 SNB_UNC_CTL_EDGE_DET | \
						 SNB_UNC_CTL_INVERT | \
						 NHM_UNC_CTL_CMASK_MASK)

/* SNB global control register */
#define SNB_UNC_PERF_GLOBAL_CTL                 0x391
#define SNB_UNC_FIXED_CTR_CTRL                  0x394
#define SNB_UNC_FIXED_CTR                       0x395

/* SNB uncore global control */
#define SNB_UNC_GLOBAL_CTL_CORE_ALL             ((1 << 4) - 1)
#define SNB_UNC_GLOBAL_CTL_EN                   (1 << 29)

/* SNB Cbo register */
#define SNB_UNC_CBO_0_PERFEVTSEL0               0x700
#define SNB_UNC_CBO_0_PER_CTR0                  0x706
#define SNB_UNC_CBO_MSR_OFFSET                  0x10

/* NHM global control register */
#define NHM_UNC_PERF_GLOBAL_CTL                 0x391
#define NHM_UNC_FIXED_CTR                       0x394
#define NHM_UNC_FIXED_CTR_CTRL                  0x395

/* NHM uncore global control */
#define NHM_UNC_GLOBAL_CTL_EN_PC_ALL            ((1ULL << 8) - 1)
#define NHM_UNC_GLOBAL_CTL_EN_FC                (1ULL << 32)

/* NHM uncore register */
#define NHM_UNC_PERFEVTSEL0                     0x3c0
#define NHM_UNC_UNCORE_PMC0                     0x3b0

/* SNB-EP Box level control */
#define SNBEP_PMON_BOX_CTL_RST_CTRL	(1 << 0)
#define SNBEP_PMON_BOX_CTL_RST_CTRS	(1 << 1)
#define SNBEP_PMON_BOX_CTL_FRZ		(1 << 8)
#define SNBEP_PMON_BOX_CTL_FRZ_EN	(1 << 16)
#define SNBEP_PMON_BOX_CTL_INT		(SNBEP_PMON_BOX_CTL_RST_CTRL | \
					 SNBEP_PMON_BOX_CTL_RST_CTRS | \
					 SNBEP_PMON_BOX_CTL_FRZ_EN)
/* SNB-EP event control */
#define SNBEP_PMON_CTL_EV_SEL_MASK	0x000000ff
#define SNBEP_PMON_CTL_UMASK_MASK	0x0000ff00
#define SNBEP_PMON_CTL_RST		(1 << 17)
#define SNBEP_PMON_CTL_EDGE_DET		(1 << 18)
#define SNBEP_PMON_CTL_EV_SEL_EXT	(1 << 21)	/* only for QPI */
#define SNBEP_PMON_CTL_EN		(1 << 22)
#define SNBEP_PMON_CTL_INVERT		(1 << 23)
#define SNBEP_PMON_CTL_TRESH_MASK	0xff000000
#define SNBEP_PMON_RAW_EVENT_MASK	(SNBEP_PMON_CTL_EV_SEL_MASK | \
					 SNBEP_PMON_CTL_UMASK_MASK | \
					 SNBEP_PMON_CTL_EDGE_DET | \
					 SNBEP_PMON_CTL_INVERT | \
					 SNBEP_PMON_CTL_TRESH_MASK)

/* SNB-EP Ubox event control */
#define SNBEP_U_MSR_PMON_CTL_TRESH_MASK		0x1f000000
#define SNBEP_U_MSR_PMON_RAW_EVENT_MASK		\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PMON_CTL_UMASK_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PMON_CTL_INVERT | \
				 SNBEP_U_MSR_PMON_CTL_TRESH_MASK)

/* SNB-EP PCU event control */
#define SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK	0x0000c000
#define SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK	0x1f000000
#define SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT	(1 << 30)
#define SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET	(1 << 31)
#define SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PMON_CTL_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)

/* SNB-EP pci control register */
#define SNBEP_PCI_PMON_BOX_CTL			0xf4
#define SNBEP_PCI_PMON_CTL0			0xd8
/* SNB-EP pci counter register */
#define SNBEP_PCI_PMON_CTR0			0xa0

/* SNB-EP home agent register */
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0	0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1	0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH	0x48
/* SNB-EP memory controller register */
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTL		0xf0
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTR		0xd0
/* SNB-EP QPI register */
#define SNBEP_Q_Py_PCI_PMON_PKT_MATCH0		0x228
#define SNBEP_Q_Py_PCI_PMON_PKT_MATCH1		0x22c
#define SNBEP_Q_Py_PCI_PMON_PKT_MASK0		0x238
#define SNBEP_Q_Py_PCI_PMON_PKT_MASK1		0x23c

/* SNB-EP Ubox register */
#define SNBEP_U_MSR_PMON_CTR0			0xc16
#define SNBEP_U_MSR_PMON_CTL0			0xc10

#define SNBEP_U_MSR_PMON_UCLK_FIXED_CTL		0xc08
#define SNBEP_U_MSR_PMON_UCLK_FIXED_CTR		0xc09

/* SNB-EP Cbo register */
#define SNBEP_C0_MSR_PMON_CTR0			0xd16
#define SNBEP_C0_MSR_PMON_CTL0			0xd10
#define SNBEP_C0_MSR_PMON_BOX_FILTER		0xd14
#define SNBEP_C0_MSR_PMON_BOX_CTL		0xd04
#define SNBEP_CBO_MSR_OFFSET			0x20

/* SNB-EP PCU register */
#define SNBEP_PCU_MSR_PMON_CTR0			0xc36
#define SNBEP_PCU_MSR_PMON_CTL0			0xc30
#define SNBEP_PCU_MSR_PMON_BOX_FILTER		0xc34
#define SNBEP_PCU_MSR_PMON_BOX_CTL		0xc24
#define SNBEP_PCU_MSR_CORE_C3_CTR		0x3fc
#define SNBEP_PCU_MSR_CORE_C6_CTR		0x3fd

struct intel_uncore_ops;
struct intel_uncore_pmu;
struct intel_uncore_box;
struct uncore_event_desc;

struct intel_uncore_type {
	const char *name;
	int num_counters;
	int num_boxes;
	int perf_ctr_bits;
	int fixed_ctr_bits;
	int single_fixed;
	unsigned perf_ctr;
	unsigned event_ctl;
	unsigned event_mask;
	unsigned fixed_ctr;
	unsigned fixed_ctl;
	unsigned box_ctl;
	unsigned msr_offset;
	struct event_constraint unconstrainted;
	struct event_constraint *constraints;
	struct intel_uncore_pmu *pmus;
	struct intel_uncore_ops *ops;
	struct uncore_event_desc *event_descs;
	const struct attribute_group *attr_groups[3];
};

#define format_group attr_groups[0]

struct intel_uncore_ops {
	void (*init_box)(struct intel_uncore_box *);
	void (*disable_box)(struct intel_uncore_box *);
	void (*enable_box)(struct intel_uncore_box *);
	void (*disable_event)(struct intel_uncore_box *, struct perf_event *);
	void (*enable_event)(struct intel_uncore_box *, struct perf_event *);
	u64 (*read_counter)(struct intel_uncore_box *, struct perf_event *);
};

struct intel_uncore_pmu {
	struct pmu pmu;
	char name[UNCORE_PMU_NAME_LEN];
	int pmu_idx;
	int func_id;
	struct intel_uncore_type *type;
	struct intel_uncore_box ** __percpu box;
	struct list_head box_list;
};

struct intel_uncore_box {
	int phys_id;
	int n_active;	/* number of active events */
	int n_events;
	int cpu;	/* cpu to collect events */
	unsigned long flags;
	atomic_t refcnt;
	struct perf_event *events[UNCORE_PMC_IDX_MAX];
	struct perf_event *event_list[UNCORE_PMC_IDX_MAX];
	unsigned long active_mask[BITS_TO_LONGS(UNCORE_PMC_IDX_MAX)];
	u64 tags[UNCORE_PMC_IDX_MAX];
	struct pci_dev *pci_dev;
	struct intel_uncore_pmu *pmu;
	struct hrtimer hrtimer;
	struct list_head list;
};

#define UNCORE_BOX_FLAG_INITIATED	0

struct uncore_event_desc {
	struct kobj_attribute attr;
	const char *config;
};

#define INTEL_UNCORE_EVENT_DESC(_name, _config)			\
{								\
	.attr	= __ATTR(_name, 0444, uncore_event_show, NULL),	\
	.config	= _config,					\
}

#define DEFINE_UNCORE_FORMAT_ATTR(_var, _name, _format)			\
static ssize_t __uncore_##_var##_show(struct kobject *kobj,		\
				struct kobj_attribute *attr,		\
				char *page)				\
{									\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);			\
	return sprintf(page, _format "\n");				\
}									\
static struct kobj_attribute format_attr_##_var =			\
	__ATTR(_name, 0444, __uncore_##_var##_show, NULL)


static ssize_t uncore_event_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct uncore_event_desc *event =
		container_of(attr, struct uncore_event_desc, attr);
	return sprintf(buf, "%s", event->config);
}

static inline unsigned uncore_pci_box_ctl(struct intel_uncore_box *box)
{
	return box->pmu->type->box_ctl;
}

static inline unsigned uncore_pci_fixed_ctl(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctl;
}

static inline unsigned uncore_pci_fixed_ctr(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr;
}

static inline
unsigned uncore_pci_event_ctl(struct intel_uncore_box *box, int idx)
{
	return idx * 4 + box->pmu->type->event_ctl;
}

static inline
unsigned uncore_pci_perf_ctr(struct intel_uncore_box *box, int idx)
{
	return idx * 8 + box->pmu->type->perf_ctr;
}

static inline
unsigned uncore_msr_box_ctl(struct intel_uncore_box *box)
{
	if (!box->pmu->type->box_ctl)
		return 0;
	return box->pmu->type->box_ctl +
		box->pmu->type->msr_offset * box->pmu->pmu_idx;
}

static inline
unsigned uncore_msr_fixed_ctl(struct intel_uncore_box *box)
{
	if (!box->pmu->type->fixed_ctl)
		return 0;
	return box->pmu->type->fixed_ctl +
		box->pmu->type->msr_offset * box->pmu->pmu_idx;
}

static inline
unsigned uncore_msr_fixed_ctr(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr +
		box->pmu->type->msr_offset * box->pmu->pmu_idx;
}

static inline
unsigned uncore_msr_event_ctl(struct intel_uncore_box *box, int idx)
{
	return idx + box->pmu->type->event_ctl +
		box->pmu->type->msr_offset * box->pmu->pmu_idx;
}

static inline
unsigned uncore_msr_perf_ctr(struct intel_uncore_box *box, int idx)
{
	return idx + box->pmu->type->perf_ctr +
		box->pmu->type->msr_offset * box->pmu->pmu_idx;
}

static inline
unsigned uncore_fixed_ctl(struct intel_uncore_box *box)
{
	if (box->pci_dev)
		return uncore_pci_fixed_ctl(box);
	else
		return uncore_msr_fixed_ctl(box);
}

static inline
unsigned uncore_fixed_ctr(struct intel_uncore_box *box)
{
	if (box->pci_dev)
		return uncore_pci_fixed_ctr(box);
	else
		return uncore_msr_fixed_ctr(box);
}

static inline
unsigned uncore_event_ctl(struct intel_uncore_box *box, int idx)
{
	if (box->pci_dev)
		return uncore_pci_event_ctl(box, idx);
	else
		return uncore_msr_event_ctl(box, idx);
}

static inline
unsigned uncore_perf_ctr(struct intel_uncore_box *box, int idx)
{
	if (box->pci_dev)
		return uncore_pci_perf_ctr(box, idx);
	else
		return uncore_msr_perf_ctr(box, idx);
}

static inline int uncore_perf_ctr_bits(struct intel_uncore_box *box)
{
	return box->pmu->type->perf_ctr_bits;
}

static inline int uncore_fixed_ctr_bits(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr_bits;
}

static inline int uncore_num_counters(struct intel_uncore_box *box)
{
	return box->pmu->type->num_counters;
}

static inline void uncore_disable_box(struct intel_uncore_box *box)
{
	if (box->pmu->type->ops->disable_box)
		box->pmu->type->ops->disable_box(box);
}

static inline void uncore_enable_box(struct intel_uncore_box *box)
{
	if (box->pmu->type->ops->enable_box)
		box->pmu->type->ops->enable_box(box);
}

static inline void uncore_disable_event(struct intel_uncore_box *box,
				struct perf_event *event)
{
	box->pmu->type->ops->disable_event(box, event);
}

static inline void uncore_enable_event(struct intel_uncore_box *box,
				struct perf_event *event)
{
	box->pmu->type->ops->enable_event(box, event);
}

static inline u64 uncore_read_counter(struct intel_uncore_box *box,
				struct perf_event *event)
{
	return box->pmu->type->ops->read_counter(box, event);
}

static inline void uncore_box_init(struct intel_uncore_box *box)
{
	if (!test_and_set_bit(UNCORE_BOX_FLAG_INITIATED, &box->flags)) {
		if (box->pmu->type->ops->init_box)
			box->pmu->type->ops->init_box(box);
	}
}
