// SPDX-License-Identifier: GPL-2.0
/*
 * Alibaba DDR Sub-System Driveway PMU driver
 *
 * Copyright (C) 2022 Alibaba Inc
 */

#define ALI_DRW_PMUNAME		"ali_drw"
#define ALI_DRW_DRVNAME		ALI_DRW_PMUNAME "_pmu"
#define pr_fmt(fmt)		ALI_DRW_DRVNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/refcount.h>


#define ALI_DRW_PMU_COMMON_MAX_COUNTERS			16
#define ALI_DRW_PMU_TEST_SEL_COMMON_COUNTER_BASE	19

#define ALI_DRW_PMU_PA_SHIFT			12
#define ALI_DRW_PMU_CNT_INIT			0x00000000
#define ALI_DRW_CNT_MAX_PERIOD			0xffffffff
#define ALI_DRW_PMU_CYCLE_EVT_ID		0x80

#define ALI_DRW_PMU_CNT_CTRL			0xC00
#define ALI_DRW_PMU_CNT_RST			BIT(2)
#define ALI_DRW_PMU_CNT_STOP			BIT(1)
#define ALI_DRW_PMU_CNT_START			BIT(0)

#define ALI_DRW_PMU_CNT_STATE			0xC04
#define ALI_DRW_PMU_TEST_CTRL			0xC08
#define ALI_DRW_PMU_CNT_PRELOAD			0xC0C

#define ALI_DRW_PMU_CYCLE_CNT_HIGH_MASK		GENMASK(23, 0)
#define ALI_DRW_PMU_CYCLE_CNT_LOW_MASK		GENMASK(31, 0)
#define ALI_DRW_PMU_CYCLE_CNT_HIGH		0xC10
#define ALI_DRW_PMU_CYCLE_CNT_LOW		0xC14

/* PMU EVENT SEL 0-3 are paired in 32-bit registers on a 4-byte stride */
#define ALI_DRW_PMU_EVENT_SEL0			0xC68
/* counter 0-3 use sel0, counter 4-7 use sel1...*/
#define ALI_DRW_PMU_EVENT_SELn(n) \
	(ALI_DRW_PMU_EVENT_SEL0 + (n / 4) * 0x4)
#define ALI_DRW_PMCOM_CNT_EN			BIT(7)
#define ALI_DRW_PMCOM_CNT_EVENT_MASK		GENMASK(5, 0)
#define ALI_DRW_PMCOM_CNT_EVENT_OFFSET(n) \
	(8 * (n % 4))

/* PMU COMMON COUNTER 0-15, are paired in 32-bit registers on a 4-byte stride */
#define ALI_DRW_PMU_COMMON_COUNTER0		0xC78
#define ALI_DRW_PMU_COMMON_COUNTERn(n) \
	(ALI_DRW_PMU_COMMON_COUNTER0 + 0x4 * (n))

#define ALI_DRW_PMU_OV_INTR_ENABLE_CTL		0xCB8
#define ALI_DRW_PMU_OV_INTR_DISABLE_CTL		0xCBC
#define ALI_DRW_PMU_OV_INTR_ENABLE_STATUS	0xCC0
#define ALI_DRW_PMU_OV_INTR_CLR			0xCC4
#define ALI_DRW_PMU_OV_INTR_STATUS		0xCC8
#define ALI_DRW_PMCOM_CNT_OV_INTR_MASK		GENMASK(23, 8)
#define ALI_DRW_PMBW_CNT_OV_INTR_MASK		GENMASK(7, 0)
#define ALI_DRW_PMU_OV_INTR_MASK		GENMASK_ULL(63, 0)

static int ali_drw_cpuhp_state_num;

static LIST_HEAD(ali_drw_pmu_irqs);
static DEFINE_MUTEX(ali_drw_pmu_irqs_lock);

struct ali_drw_pmu_irq {
	struct hlist_node node;
	struct list_head irqs_node;
	struct list_head pmus_node;
	int irq_num;
	int cpu;
	refcount_t refcount;
};

struct ali_drw_pmu {
	void __iomem *cfg_base;
	struct device *dev;

	struct list_head pmus_node;
	struct ali_drw_pmu_irq *irq;
	int irq_num;
	int cpu;
	DECLARE_BITMAP(used_mask, ALI_DRW_PMU_COMMON_MAX_COUNTERS);
	struct perf_event *events[ALI_DRW_PMU_COMMON_MAX_COUNTERS];
	int evtids[ALI_DRW_PMU_COMMON_MAX_COUNTERS];

	struct pmu pmu;
};

#define to_ali_drw_pmu(p) (container_of(p, struct ali_drw_pmu, pmu))

#define DRW_CONFIG_EVENTID		GENMASK(7, 0)
#define GET_DRW_EVENTID(event)	FIELD_GET(DRW_CONFIG_EVENTID, (event)->attr.config)

static ssize_t ali_drw_pmu_format_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sprintf(buf, "%s\n", (char *)eattr->var);
}

/*
 * PMU event attributes
 */
static ssize_t ali_drw_pmu_event_show(struct device *dev,
			       struct device_attribute *attr, char *page)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sprintf(page, "config=0x%lx\n", (unsigned long)eattr->var);
}

#define ALI_DRW_PMU_ATTR(_name, _func, _config)                            \
		(&((struct dev_ext_attribute[]) {                               \
				{ __ATTR(_name, 0444, _func, NULL), (void *)_config }   \
		})[0].attr.attr)

#define ALI_DRW_PMU_FORMAT_ATTR(_name, _config)            \
	ALI_DRW_PMU_ATTR(_name, ali_drw_pmu_format_show, (void *)_config)
#define ALI_DRW_PMU_EVENT_ATTR(_name, _config)             \
	ALI_DRW_PMU_ATTR(_name, ali_drw_pmu_event_show, (unsigned long)_config)

static struct attribute *ali_drw_pmu_events_attrs[] = {
	ALI_DRW_PMU_EVENT_ATTR(hif_rd_or_wr,			0x0),
	ALI_DRW_PMU_EVENT_ATTR(hif_wr,				0x1),
	ALI_DRW_PMU_EVENT_ATTR(hif_rd,				0x2),
	ALI_DRW_PMU_EVENT_ATTR(hif_rmw,				0x3),
	ALI_DRW_PMU_EVENT_ATTR(hif_hi_pri_rd,			0x4),
	ALI_DRW_PMU_EVENT_ATTR(dfi_wr_data_cycles,		0x7),
	ALI_DRW_PMU_EVENT_ATTR(dfi_rd_data_cycles,		0x8),
	ALI_DRW_PMU_EVENT_ATTR(hpr_xact_when_critical,		0x9),
	ALI_DRW_PMU_EVENT_ATTR(lpr_xact_when_critical,		0xA),
	ALI_DRW_PMU_EVENT_ATTR(wr_xact_when_critical,		0xB),
	ALI_DRW_PMU_EVENT_ATTR(op_is_activate,			0xC),
	ALI_DRW_PMU_EVENT_ATTR(op_is_rd_or_wr,			0xD),
	ALI_DRW_PMU_EVENT_ATTR(op_is_rd_activate,		0xE),
	ALI_DRW_PMU_EVENT_ATTR(op_is_rd,			0xF),
	ALI_DRW_PMU_EVENT_ATTR(op_is_wr,			0x10),
	ALI_DRW_PMU_EVENT_ATTR(op_is_mwr,			0x11),
	ALI_DRW_PMU_EVENT_ATTR(op_is_precharge,			0x12),
	ALI_DRW_PMU_EVENT_ATTR(precharge_for_rdwr,		0x13),
	ALI_DRW_PMU_EVENT_ATTR(precharge_for_other,		0x14),
	ALI_DRW_PMU_EVENT_ATTR(rdwr_transitions,		0x15),
	ALI_DRW_PMU_EVENT_ATTR(write_combine,			0x16),
	ALI_DRW_PMU_EVENT_ATTR(war_hazard,			0x17),
	ALI_DRW_PMU_EVENT_ATTR(raw_hazard,			0x18),
	ALI_DRW_PMU_EVENT_ATTR(waw_hazard,			0x19),
	ALI_DRW_PMU_EVENT_ATTR(op_is_enter_selfref_rk0,		0x1A),
	ALI_DRW_PMU_EVENT_ATTR(op_is_enter_selfref_rk1,		0x1B),
	ALI_DRW_PMU_EVENT_ATTR(op_is_enter_selfref_rk2,		0x1C),
	ALI_DRW_PMU_EVENT_ATTR(op_is_enter_selfref_rk3,		0x1D),
	ALI_DRW_PMU_EVENT_ATTR(op_is_enter_powerdown_rk0,	0x1E),
	ALI_DRW_PMU_EVENT_ATTR(op_is_enter_powerdown_rk1,	0x1F),
	ALI_DRW_PMU_EVENT_ATTR(op_is_enter_powerdown_rk2,	0x20),
	ALI_DRW_PMU_EVENT_ATTR(op_is_enter_powerdown_rk3,	0x21),
	ALI_DRW_PMU_EVENT_ATTR(selfref_mode_rk0,		0x26),
	ALI_DRW_PMU_EVENT_ATTR(selfref_mode_rk1,		0x27),
	ALI_DRW_PMU_EVENT_ATTR(selfref_mode_rk2,		0x28),
	ALI_DRW_PMU_EVENT_ATTR(selfref_mode_rk3,		0x29),
	ALI_DRW_PMU_EVENT_ATTR(op_is_refresh,			0x2A),
	ALI_DRW_PMU_EVENT_ATTR(op_is_crit_ref,			0x2B),
	ALI_DRW_PMU_EVENT_ATTR(op_is_load_mode,			0x2D),
	ALI_DRW_PMU_EVENT_ATTR(op_is_zqcl,			0x2E),
	ALI_DRW_PMU_EVENT_ATTR(visible_window_limit_reached_rd, 0x30),
	ALI_DRW_PMU_EVENT_ATTR(visible_window_limit_reached_wr, 0x31),
	ALI_DRW_PMU_EVENT_ATTR(op_is_dqsosc_mpc,		0x34),
	ALI_DRW_PMU_EVENT_ATTR(op_is_dqsosc_mrr,		0x35),
	ALI_DRW_PMU_EVENT_ATTR(op_is_tcr_mrr,			0x36),
	ALI_DRW_PMU_EVENT_ATTR(op_is_zqstart,			0x37),
	ALI_DRW_PMU_EVENT_ATTR(op_is_zqlatch,			0x38),
	ALI_DRW_PMU_EVENT_ATTR(chi_txreq,			0x39),
	ALI_DRW_PMU_EVENT_ATTR(chi_txdat,			0x3A),
	ALI_DRW_PMU_EVENT_ATTR(chi_rxdat,			0x3B),
	ALI_DRW_PMU_EVENT_ATTR(chi_rxrsp,			0x3C),
	ALI_DRW_PMU_EVENT_ATTR(tsz_vio,				0x3D),
	ALI_DRW_PMU_EVENT_ATTR(cycle,				0x80),
	NULL,
};

static struct attribute_group ali_drw_pmu_events_attr_group = {
	.name = "events",
	.attrs = ali_drw_pmu_events_attrs,
};

static struct attribute *ali_drw_pmu_format_attr[] = {
	ALI_DRW_PMU_FORMAT_ATTR(event, "config:0-7"),
	NULL,
};

static const struct attribute_group ali_drw_pmu_format_group = {
	.name = "format",
	.attrs = ali_drw_pmu_format_attr,
};

static ssize_t ali_drw_pmu_cpumask_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(drw_pmu->cpu));
}

static struct device_attribute ali_drw_pmu_cpumask_attr =
		__ATTR(cpumask, 0444, ali_drw_pmu_cpumask_show, NULL);

static struct attribute *ali_drw_pmu_cpumask_attrs[] = {
	&ali_drw_pmu_cpumask_attr.attr,
	NULL,
};

static const struct attribute_group ali_drw_pmu_cpumask_attr_group = {
	.attrs = ali_drw_pmu_cpumask_attrs,
};

static const struct attribute_group *ali_drw_pmu_attr_groups[] = {
	&ali_drw_pmu_events_attr_group,
	&ali_drw_pmu_cpumask_attr_group,
	&ali_drw_pmu_format_group,
	NULL,
};

/* find a counter for event, then in add func, hw.idx will equal to counter */
static int ali_drw_get_counter_idx(struct perf_event *event)
{
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);
	int idx;

	for (idx = 0; idx < ALI_DRW_PMU_COMMON_MAX_COUNTERS; ++idx) {
		if (!test_and_set_bit(idx, drw_pmu->used_mask))
			return idx;
	}

	/* The counters are all in use. */
	return -EBUSY;
}

static u64 ali_drw_pmu_read_counter(struct perf_event *event)
{
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);
	u64 cycle_high, cycle_low;

	if (GET_DRW_EVENTID(event) == ALI_DRW_PMU_CYCLE_EVT_ID) {
		cycle_high = readl(drw_pmu->cfg_base + ALI_DRW_PMU_CYCLE_CNT_HIGH);
		cycle_high &= ALI_DRW_PMU_CYCLE_CNT_HIGH_MASK;
		cycle_low = readl(drw_pmu->cfg_base + ALI_DRW_PMU_CYCLE_CNT_LOW);
		cycle_low &= ALI_DRW_PMU_CYCLE_CNT_LOW_MASK;
		return (cycle_high << 32 | cycle_low);
	}

	return readl(drw_pmu->cfg_base +
		     ALI_DRW_PMU_COMMON_COUNTERn(event->hw.idx));
}

static void ali_drw_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev, now;

	do {
		prev = local64_read(&hwc->prev_count);
		now = ali_drw_pmu_read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count, prev, now) != prev);

	/* handle overflow. */
	delta = now - prev;
	if (GET_DRW_EVENTID(event) == ALI_DRW_PMU_CYCLE_EVT_ID)
		delta &= ALI_DRW_PMU_OV_INTR_MASK;
	else
		delta &= ALI_DRW_CNT_MAX_PERIOD;
	local64_add(delta, &event->count);
}

static void ali_drw_pmu_event_set_period(struct perf_event *event)
{
	u64 pre_val;
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);

	/* set a preload counter for test purpose */
	writel(ALI_DRW_PMU_TEST_SEL_COMMON_COUNTER_BASE + event->hw.idx,
	       drw_pmu->cfg_base + ALI_DRW_PMU_TEST_CTRL);

	/* set conunter initial value */
	pre_val = ALI_DRW_PMU_CNT_INIT;
	writel(pre_val, drw_pmu->cfg_base + ALI_DRW_PMU_CNT_PRELOAD);
	local64_set(&event->hw.prev_count, pre_val);

	/* set sel mode to zero to start test */
	writel(0x0, drw_pmu->cfg_base + ALI_DRW_PMU_TEST_CTRL);
}

static void ali_drw_pmu_enable_counter(struct perf_event *event)
{
	u32 val, subval, reg, shift;
	int counter = event->hw.idx;
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);

	reg = ALI_DRW_PMU_EVENT_SELn(counter);
	val = readl(drw_pmu->cfg_base + reg);
	subval = FIELD_PREP(ALI_DRW_PMCOM_CNT_EN, 1) |
		 FIELD_PREP(ALI_DRW_PMCOM_CNT_EVENT_MASK, drw_pmu->evtids[counter]);

	shift = ALI_DRW_PMCOM_CNT_EVENT_OFFSET(counter);
	val &= ~(GENMASK(7, 0) << shift);
	val |= subval << shift;

	writel(val, drw_pmu->cfg_base + reg);
}

static void ali_drw_pmu_disable_counter(struct perf_event *event)
{
	u32 val, reg, subval, shift;
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);
	int counter = event->hw.idx;

	reg = ALI_DRW_PMU_EVENT_SELn(counter);
	val = readl(drw_pmu->cfg_base + reg);
	subval = FIELD_PREP(ALI_DRW_PMCOM_CNT_EN, 0) |
		 FIELD_PREP(ALI_DRW_PMCOM_CNT_EVENT_MASK, 0);

	shift = ALI_DRW_PMCOM_CNT_EVENT_OFFSET(counter);
	val &= ~(GENMASK(7, 0) << shift);
	val |= subval << shift;

	writel(val, drw_pmu->cfg_base + reg);
}

static irqreturn_t ali_drw_pmu_isr(int irq_num, void *data)
{
	struct ali_drw_pmu_irq *irq = data;
	struct ali_drw_pmu *drw_pmu;
	irqreturn_t ret = IRQ_NONE;

	rcu_read_lock();
	list_for_each_entry_rcu(drw_pmu, &irq->pmus_node, pmus_node) {
		unsigned long status, clr_status;
		struct perf_event *event;
		unsigned int idx;

		for (idx = 0; idx < ALI_DRW_PMU_COMMON_MAX_COUNTERS; idx++) {
			event = drw_pmu->events[idx];
			if (!event)
				continue;
			ali_drw_pmu_disable_counter(event);
		}

		/* common counter intr status */
		status = readl(drw_pmu->cfg_base + ALI_DRW_PMU_OV_INTR_STATUS);
		status = FIELD_GET(ALI_DRW_PMCOM_CNT_OV_INTR_MASK, status);
		if (status) {
			for_each_set_bit(idx, &status,
					 ALI_DRW_PMU_COMMON_MAX_COUNTERS) {
				event = drw_pmu->events[idx];
				if (WARN_ON_ONCE(!event))
					continue;
				ali_drw_pmu_event_update(event);
				ali_drw_pmu_event_set_period(event);
			}

			/* clear common counter intr status */
			clr_status = FIELD_PREP(ALI_DRW_PMCOM_CNT_OV_INTR_MASK, status);
			writel(clr_status,
			       drw_pmu->cfg_base + ALI_DRW_PMU_OV_INTR_CLR);
		}

		for (idx = 0; idx < ALI_DRW_PMU_COMMON_MAX_COUNTERS; idx++) {
			event = drw_pmu->events[idx];
			if (!event)
				continue;
			if (!(event->hw.state & PERF_HES_STOPPED))
				ali_drw_pmu_enable_counter(event);
		}
		if (status)
			ret = IRQ_HANDLED;
	}
	rcu_read_unlock();
	return ret;
}

static struct ali_drw_pmu_irq *__ali_drw_pmu_init_irq(struct platform_device
						      *pdev, int irq_num)
{
	int ret;
	struct ali_drw_pmu_irq *irq;

	list_for_each_entry(irq, &ali_drw_pmu_irqs, irqs_node) {
		if (irq->irq_num == irq_num
		    && refcount_inc_not_zero(&irq->refcount))
			return irq;
	}

	irq = kzalloc(sizeof(*irq), GFP_KERNEL);
	if (!irq)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&irq->pmus_node);

	/* Pick one CPU to be the preferred one to use */
	irq->cpu = smp_processor_id();
	refcount_set(&irq->refcount, 1);

	/*
	 * FIXME: one of DDRSS Driveway PMU overflow interrupt shares the same
	 * irq number with MPAM ERR_IRQ. To register DDRSS PMU and MPAM drivers
	 * successfully, add IRQF_SHARED flag. Howerer, PMU interrupt should not
	 * share with other component.
	 */
	ret = devm_request_irq(&pdev->dev, irq_num, ali_drw_pmu_isr,
			       IRQF_SHARED, dev_name(&pdev->dev), irq);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Fail to request IRQ:%d ret:%d\n", irq_num, ret);
		goto out_free;
	}

	ret = irq_set_affinity_hint(irq_num, cpumask_of(irq->cpu));
	if (ret)
		goto out_free;

	ret = cpuhp_state_add_instance_nocalls(ali_drw_cpuhp_state_num,
					     &irq->node);
	if (ret)
		goto out_free;

	irq->irq_num = irq_num;
	list_add(&irq->irqs_node, &ali_drw_pmu_irqs);

	return irq;

out_free:
	kfree(irq);
	return ERR_PTR(ret);
}

static int ali_drw_pmu_init_irq(struct ali_drw_pmu *drw_pmu,
				struct platform_device *pdev)
{
	int irq_num;
	struct ali_drw_pmu_irq *irq;

	/* Read and init IRQ */
	irq_num = platform_get_irq(pdev, 0);
	if (irq_num < 0)
		return irq_num;

	mutex_lock(&ali_drw_pmu_irqs_lock);
	irq = __ali_drw_pmu_init_irq(pdev, irq_num);
	mutex_unlock(&ali_drw_pmu_irqs_lock);

	if (IS_ERR(irq))
		return PTR_ERR(irq);

	drw_pmu->irq = irq;

	mutex_lock(&ali_drw_pmu_irqs_lock);
	list_add_rcu(&drw_pmu->pmus_node, &irq->pmus_node);
	mutex_unlock(&ali_drw_pmu_irqs_lock);

	return 0;
}

static void ali_drw_pmu_uninit_irq(struct ali_drw_pmu *drw_pmu)
{
	struct ali_drw_pmu_irq *irq = drw_pmu->irq;

	mutex_lock(&ali_drw_pmu_irqs_lock);
	list_del_rcu(&drw_pmu->pmus_node);

	if (!refcount_dec_and_test(&irq->refcount)) {
		mutex_unlock(&ali_drw_pmu_irqs_lock);
		return;
	}

	list_del(&irq->irqs_node);
	mutex_unlock(&ali_drw_pmu_irqs_lock);

	WARN_ON(irq_set_affinity_hint(irq->irq_num, NULL));
	cpuhp_state_remove_instance_nocalls(ali_drw_cpuhp_state_num,
					    &irq->node);
	kfree(irq);
}

static int ali_drw_pmu_event_init(struct perf_event *event)
{
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	struct perf_event *sibling;
	struct device *dev = drw_pmu->pmu.dev;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event)) {
		dev_err(dev, "Sampling not supported!\n");
		return -EOPNOTSUPP;
	}

	if (event->attach_state & PERF_ATTACH_TASK) {
		dev_err(dev, "Per-task counter cannot allocate!\n");
		return -EOPNOTSUPP;
	}

	event->cpu = drw_pmu->cpu;
	if (event->cpu < 0) {
		dev_err(dev, "Per-task mode not supported!\n");
		return -EOPNOTSUPP;
	}

	if (event->group_leader != event &&
	    !is_software_event(event->group_leader)) {
		dev_err(dev, "driveway only allow one event!\n");
		return -EINVAL;
	}

	for_each_sibling_event(sibling, event->group_leader) {
		if (sibling != event && !is_software_event(sibling)) {
			dev_err(dev, "driveway event not allowed!\n");
			return -EINVAL;
		}
	}

	/* reset all the pmu counters */
	writel(ALI_DRW_PMU_CNT_RST, drw_pmu->cfg_base + ALI_DRW_PMU_CNT_CTRL);

	hwc->idx = -1;

	return 0;
}

static void ali_drw_pmu_start(struct perf_event *event, int flags)
{
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);

	event->hw.state = 0;

	if (GET_DRW_EVENTID(event) == ALI_DRW_PMU_CYCLE_EVT_ID) {
		writel(ALI_DRW_PMU_CNT_START,
		       drw_pmu->cfg_base + ALI_DRW_PMU_CNT_CTRL);
		return;
	}

	ali_drw_pmu_event_set_period(event);
	if (flags & PERF_EF_RELOAD) {
		unsigned long prev_raw_count =
		    local64_read(&event->hw.prev_count);
		writel(prev_raw_count,
		       drw_pmu->cfg_base + ALI_DRW_PMU_CNT_PRELOAD);
	}

	ali_drw_pmu_enable_counter(event);

	writel(ALI_DRW_PMU_CNT_START, drw_pmu->cfg_base + ALI_DRW_PMU_CNT_CTRL);
}

static void ali_drw_pmu_stop(struct perf_event *event, int flags)
{
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	if (GET_DRW_EVENTID(event) != ALI_DRW_PMU_CYCLE_EVT_ID)
		ali_drw_pmu_disable_counter(event);

	writel(ALI_DRW_PMU_CNT_STOP, drw_pmu->cfg_base + ALI_DRW_PMU_CNT_CTRL);

	ali_drw_pmu_event_update(event);
	event->hw.state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int ali_drw_pmu_add(struct perf_event *event, int flags)
{
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = -1;
	int evtid;

	evtid = GET_DRW_EVENTID(event);

	if (evtid != ALI_DRW_PMU_CYCLE_EVT_ID) {
		idx = ali_drw_get_counter_idx(event);
		if (idx < 0)
			return idx;
		drw_pmu->events[idx] = event;
		drw_pmu->evtids[idx] = evtid;
	}
	hwc->idx = idx;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (flags & PERF_EF_START)
		ali_drw_pmu_start(event, PERF_EF_RELOAD);

	/* Propagate our changes to the userspace mapping. */
	perf_event_update_userpage(event);

	return 0;
}

static void ali_drw_pmu_del(struct perf_event *event, int flags)
{
	struct ali_drw_pmu *drw_pmu = to_ali_drw_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	ali_drw_pmu_stop(event, PERF_EF_UPDATE);

	if (idx >= 0 && idx < ALI_DRW_PMU_COMMON_MAX_COUNTERS) {
		drw_pmu->events[idx] = NULL;
		drw_pmu->evtids[idx] = 0;
		clear_bit(idx, drw_pmu->used_mask);
	}

	perf_event_update_userpage(event);
}

static void ali_drw_pmu_read(struct perf_event *event)
{
	ali_drw_pmu_event_update(event);
}

static int ali_drw_pmu_probe(struct platform_device *pdev)
{
	struct ali_drw_pmu *drw_pmu;
	struct resource *res;
	char *name;
	int ret;

	drw_pmu = devm_kzalloc(&pdev->dev, sizeof(*drw_pmu), GFP_KERNEL);
	if (!drw_pmu)
		return -ENOMEM;

	drw_pmu->dev = &pdev->dev;
	platform_set_drvdata(pdev, drw_pmu);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drw_pmu->cfg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drw_pmu->cfg_base))
		return PTR_ERR(drw_pmu->cfg_base);

	name = devm_kasprintf(drw_pmu->dev, GFP_KERNEL, "ali_drw_%llx",
			      (u64) (res->start >> ALI_DRW_PMU_PA_SHIFT));
	if (!name)
		return -ENOMEM;

	writel(ALI_DRW_PMU_CNT_RST, drw_pmu->cfg_base + ALI_DRW_PMU_CNT_CTRL);

	/* enable the generation of interrupt by all common counters */
	writel(ALI_DRW_PMCOM_CNT_OV_INTR_MASK,
	       drw_pmu->cfg_base + ALI_DRW_PMU_OV_INTR_ENABLE_CTL);

	/* clearing interrupt status */
	writel(0xffffff, drw_pmu->cfg_base + ALI_DRW_PMU_OV_INTR_CLR);

	drw_pmu->cpu = smp_processor_id();

	ret = ali_drw_pmu_init_irq(drw_pmu, pdev);
	if (ret)
		return ret;

	drw_pmu->pmu = (struct pmu) {
		.module		= THIS_MODULE,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= ali_drw_pmu_event_init,
		.add		= ali_drw_pmu_add,
		.del		= ali_drw_pmu_del,
		.start		= ali_drw_pmu_start,
		.stop		= ali_drw_pmu_stop,
		.read		= ali_drw_pmu_read,
		.attr_groups	= ali_drw_pmu_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	};

	ret = perf_pmu_register(&drw_pmu->pmu, name, -1);
	if (ret) {
		dev_err(drw_pmu->dev, "DRW Driveway PMU PMU register failed!\n");
		ali_drw_pmu_uninit_irq(drw_pmu);
	}

	return ret;
}

static int ali_drw_pmu_remove(struct platform_device *pdev)
{
	struct ali_drw_pmu *drw_pmu = platform_get_drvdata(pdev);

	/* disable the generation of interrupt by all common counters */
	writel(ALI_DRW_PMCOM_CNT_OV_INTR_MASK,
	       drw_pmu->cfg_base + ALI_DRW_PMU_OV_INTR_DISABLE_CTL);

	ali_drw_pmu_uninit_irq(drw_pmu);
	perf_pmu_unregister(&drw_pmu->pmu);

	return 0;
}

static int ali_drw_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct ali_drw_pmu_irq *irq;
	struct ali_drw_pmu *drw_pmu;
	unsigned int target;
	int ret;
	cpumask_t node_online_cpus;

	irq = hlist_entry_safe(node, struct ali_drw_pmu_irq, node);
	if (cpu != irq->cpu)
		return 0;

	ret = cpumask_and(&node_online_cpus,
			  cpumask_of_node(cpu_to_node(cpu)), cpu_online_mask);
	if (ret)
		target = cpumask_any_but(&node_online_cpus, cpu);
	else
		target = cpumask_any_but(cpu_online_mask, cpu);

	if (target >= nr_cpu_ids)
		return 0;

	/* We're only reading, but this isn't the place to be involving RCU */
	mutex_lock(&ali_drw_pmu_irqs_lock);
	list_for_each_entry(drw_pmu, &irq->pmus_node, pmus_node)
		perf_pmu_migrate_context(&drw_pmu->pmu, irq->cpu, target);
	mutex_unlock(&ali_drw_pmu_irqs_lock);

	WARN_ON(irq_set_affinity_hint(irq->irq_num, cpumask_of(target)));
	irq->cpu = target;

	return 0;
}

/*
 * Due to historical reasons, the HID used in the production environment is
 * ARMHD700, so we leave ARMHD700 as Compatible ID.
 */
static const struct acpi_device_id ali_drw_acpi_match[] = {
	{"BABA5000", 0},
	{"ARMHD700", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, ali_drw_acpi_match);

static struct platform_driver ali_drw_pmu_driver = {
	.driver = {
		   .name = "ali_drw_pmu",
		   .acpi_match_table = ali_drw_acpi_match,
		   },
	.probe = ali_drw_pmu_probe,
	.remove = ali_drw_pmu_remove,
};

static int __init ali_drw_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "ali_drw_pmu:online",
				      NULL, ali_drw_pmu_offline_cpu);

	if (ret < 0) {
		pr_err("DRW Driveway PMU: setup hotplug failed, ret = %d\n",
		       ret);
		return ret;
	}
	ali_drw_cpuhp_state_num = ret;

	ret = platform_driver_register(&ali_drw_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(ali_drw_cpuhp_state_num);

	return ret;
}

static void __exit ali_drw_pmu_exit(void)
{
	platform_driver_unregister(&ali_drw_pmu_driver);
	cpuhp_remove_multi_state(ali_drw_cpuhp_state_num);
}

module_init(ali_drw_pmu_init);
module_exit(ali_drw_pmu_exit);

MODULE_AUTHOR("Hongbo Yao <yaohongbo@linux.alibaba.com>");
MODULE_AUTHOR("Neng Chen <nengchen@linux.alibaba.com>");
MODULE_AUTHOR("Shuai Xue <xueshuai@linux.alibaba.com>");
MODULE_DESCRIPTION("Alibaba DDR Sub-System Driveway PMU driver");
MODULE_LICENSE("GPL v2");
