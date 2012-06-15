#include <linux/module.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include "perf_event.h"

#define UNCORE_PMU_NAME_LEN		32
#define UNCORE_BOX_HASH_SIZE		8

#define UNCORE_PMU_HRTIMER_INTERVAL	(60 * NSEC_PER_SEC)

#define UNCORE_FIXED_EVENT		0xffff
#define UNCORE_PMC_IDX_MAX_GENERIC	8
#define UNCORE_PMC_IDX_FIXED		UNCORE_PMC_IDX_MAX_GENERIC
#define UNCORE_PMC_IDX_MAX		(UNCORE_PMC_IDX_FIXED + 1)

#define UNCORE_EVENT_CONSTRAINT(c, n) EVENT_CONSTRAINT(c, n, 0xff)

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
