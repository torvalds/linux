// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Lin Huang <hl@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/devfreq-event.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/seqlock.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/perf_event.h>

#include <soc/rockchip/rockchip_grf.h>
#include <soc/rockchip/rk3399_grf.h>
#include <soc/rockchip/rk3568_grf.h>
#include <soc/rockchip/rk3588_grf.h>

#define DMC_MAX_CHANNELS	4

#define HIWORD_UPDATE(val, mask)	((val) | (mask) << 16)

/* DDRMON_CTRL */
#define DDRMON_CTRL	0x04
#define DDRMON_CTRL_DDR4		BIT(5)
#define DDRMON_CTRL_LPDDR4		BIT(4)
#define DDRMON_CTRL_HARDWARE_EN		BIT(3)
#define DDRMON_CTRL_LPDDR23		BIT(2)
#define DDRMON_CTRL_SOFTWARE_EN		BIT(1)
#define DDRMON_CTRL_TIMER_CNT_EN	BIT(0)
#define DDRMON_CTRL_DDR_TYPE_MASK	(DDRMON_CTRL_DDR4 | \
					 DDRMON_CTRL_LPDDR4 | \
					 DDRMON_CTRL_LPDDR23)

#define DDRMON_CH0_WR_NUM		0x20
#define DDRMON_CH0_RD_NUM		0x24
#define DDRMON_CH0_COUNT_NUM		0x28
#define DDRMON_CH0_DFI_ACCESS_NUM	0x2c
#define DDRMON_CH1_COUNT_NUM		0x3c
#define DDRMON_CH1_DFI_ACCESS_NUM	0x40

#define PERF_EVENT_CYCLES		0x0
#define PERF_EVENT_READ_BYTES		0x1
#define PERF_EVENT_WRITE_BYTES		0x2
#define PERF_EVENT_READ_BYTES0		0x3
#define PERF_EVENT_WRITE_BYTES0		0x4
#define PERF_EVENT_READ_BYTES1		0x5
#define PERF_EVENT_WRITE_BYTES1		0x6
#define PERF_EVENT_READ_BYTES2		0x7
#define PERF_EVENT_WRITE_BYTES2		0x8
#define PERF_EVENT_READ_BYTES3		0x9
#define PERF_EVENT_WRITE_BYTES3		0xa
#define PERF_EVENT_BYTES		0xb
#define PERF_ACCESS_TYPE_MAX		0xc

/**
 * struct dmc_count_channel - structure to hold counter values from the DDR controller
 * @access:       Number of read and write accesses
 * @clock_cycles: DDR clock cycles
 * @read_access:  number of read accesses
 * @write_access: number of write accesses
 */
struct dmc_count_channel {
	u64 access;
	u64 clock_cycles;
	u64 read_access;
	u64 write_access;
};

struct dmc_count {
	struct dmc_count_channel c[DMC_MAX_CHANNELS];
};

/*
 * The dfi controller can monitor DDR load. It has an upper and lower threshold
 * for the operating points. Whenever the usage leaves these bounds an event is
 * generated to indicate the DDR frequency should be changed.
 */
struct rockchip_dfi {
	struct devfreq_event_dev *edev;
	struct devfreq_event_desc desc;
	struct dmc_count last_event_count;

	struct dmc_count last_perf_count;
	struct dmc_count total_count;
	seqlock_t count_seqlock; /* protects last_perf_count and total_count */

	struct device *dev;
	void __iomem *regs;
	struct regmap *regmap_pmu;
	struct clk *clk;
	int usecount;
	struct mutex mutex;
	u32 ddr_type;
	unsigned int channel_mask;
	unsigned int max_channels;
	enum cpuhp_state cpuhp_state;
	struct hlist_node node;
	struct pmu pmu;
	struct hrtimer timer;
	unsigned int cpu;
	int active_events;
	int burst_len;
	int buswidth[DMC_MAX_CHANNELS];
	int ddrmon_stride;
	bool ddrmon_ctrl_single;
};

static int rockchip_dfi_enable(struct rockchip_dfi *dfi)
{
	void __iomem *dfi_regs = dfi->regs;
	int i, ret = 0;

	mutex_lock(&dfi->mutex);

	dfi->usecount++;
	if (dfi->usecount > 1)
		goto out;

	ret = clk_prepare_enable(dfi->clk);
	if (ret) {
		dev_err(&dfi->edev->dev, "failed to enable dfi clk: %d\n", ret);
		goto out;
	}

	for (i = 0; i < dfi->max_channels; i++) {
		u32 ctrl = 0;

		if (!(dfi->channel_mask & BIT(i)))
			continue;

		/* clear DDRMON_CTRL setting */
		writel_relaxed(HIWORD_UPDATE(0, DDRMON_CTRL_TIMER_CNT_EN |
			       DDRMON_CTRL_SOFTWARE_EN | DDRMON_CTRL_HARDWARE_EN),
			       dfi_regs + i * dfi->ddrmon_stride + DDRMON_CTRL);

		/* set ddr type to dfi */
		switch (dfi->ddr_type) {
		case ROCKCHIP_DDRTYPE_LPDDR2:
		case ROCKCHIP_DDRTYPE_LPDDR3:
			ctrl = DDRMON_CTRL_LPDDR23;
			break;
		case ROCKCHIP_DDRTYPE_LPDDR4:
		case ROCKCHIP_DDRTYPE_LPDDR4X:
			ctrl = DDRMON_CTRL_LPDDR4;
			break;
		default:
			break;
		}

		writel_relaxed(HIWORD_UPDATE(ctrl, DDRMON_CTRL_DDR_TYPE_MASK),
			       dfi_regs + i * dfi->ddrmon_stride + DDRMON_CTRL);

		/* enable count, use software mode */
		writel_relaxed(HIWORD_UPDATE(DDRMON_CTRL_SOFTWARE_EN, DDRMON_CTRL_SOFTWARE_EN),
			       dfi_regs + i * dfi->ddrmon_stride + DDRMON_CTRL);

		if (dfi->ddrmon_ctrl_single)
			break;
	}
out:
	mutex_unlock(&dfi->mutex);

	return ret;
}

static void rockchip_dfi_disable(struct rockchip_dfi *dfi)
{
	void __iomem *dfi_regs = dfi->regs;
	int i;

	mutex_lock(&dfi->mutex);

	dfi->usecount--;

	WARN_ON_ONCE(dfi->usecount < 0);

	if (dfi->usecount > 0)
		goto out;

	for (i = 0; i < dfi->max_channels; i++) {
		if (!(dfi->channel_mask & BIT(i)))
			continue;

		writel_relaxed(HIWORD_UPDATE(0, DDRMON_CTRL_SOFTWARE_EN),
			      dfi_regs + i * dfi->ddrmon_stride + DDRMON_CTRL);

		if (dfi->ddrmon_ctrl_single)
			break;
	}

	clk_disable_unprepare(dfi->clk);
out:
	mutex_unlock(&dfi->mutex);
}

static void rockchip_dfi_read_counters(struct rockchip_dfi *dfi, struct dmc_count *res)
{
	u32 i;
	void __iomem *dfi_regs = dfi->regs;

	for (i = 0; i < dfi->max_channels; i++) {
		if (!(dfi->channel_mask & BIT(i)))
			continue;
		res->c[i].read_access = readl_relaxed(dfi_regs +
				DDRMON_CH0_RD_NUM + i * dfi->ddrmon_stride);
		res->c[i].write_access = readl_relaxed(dfi_regs +
				DDRMON_CH0_WR_NUM + i * dfi->ddrmon_stride);
		res->c[i].access = readl_relaxed(dfi_regs +
				DDRMON_CH0_DFI_ACCESS_NUM + i * dfi->ddrmon_stride);
		res->c[i].clock_cycles = readl_relaxed(dfi_regs +
				DDRMON_CH0_COUNT_NUM + i * dfi->ddrmon_stride);
	}
}

static int rockchip_dfi_event_disable(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);

	rockchip_dfi_disable(dfi);

	return 0;
}

static int rockchip_dfi_event_enable(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);

	return rockchip_dfi_enable(dfi);
}

static int rockchip_dfi_set_event(struct devfreq_event_dev *edev)
{
	return 0;
}

static int rockchip_dfi_get_event(struct devfreq_event_dev *edev,
				  struct devfreq_event_data *edata)
{
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);
	struct dmc_count count;
	struct dmc_count *last = &dfi->last_event_count;
	u32 access = 0, clock_cycles = 0;
	int i;

	rockchip_dfi_read_counters(dfi, &count);

	/* We can only report one channel, so find the busiest one */
	for (i = 0; i < dfi->max_channels; i++) {
		u32 a, c;

		if (!(dfi->channel_mask & BIT(i)))
			continue;

		a = count.c[i].access - last->c[i].access;
		c = count.c[i].clock_cycles - last->c[i].clock_cycles;

		if (a > access) {
			access = a;
			clock_cycles = c;
		}
	}

	edata->load_count = access * 4;
	edata->total_count = clock_cycles;

	dfi->last_event_count = count;

	return 0;
}

static const struct devfreq_event_ops rockchip_dfi_ops = {
	.disable = rockchip_dfi_event_disable,
	.enable = rockchip_dfi_event_enable,
	.get_event = rockchip_dfi_get_event,
	.set_event = rockchip_dfi_set_event,
};

#ifdef CONFIG_PERF_EVENTS

static void rockchip_ddr_perf_counters_add(struct rockchip_dfi *dfi,
					   const struct dmc_count *now,
					   struct dmc_count *res)
{
	const struct dmc_count *last = &dfi->last_perf_count;
	int i;

	for (i = 0; i < dfi->max_channels; i++) {
		res->c[i].read_access = dfi->total_count.c[i].read_access +
			(u32)(now->c[i].read_access - last->c[i].read_access);
		res->c[i].write_access = dfi->total_count.c[i].write_access +
			(u32)(now->c[i].write_access - last->c[i].write_access);
		res->c[i].access = dfi->total_count.c[i].access +
			(u32)(now->c[i].access - last->c[i].access);
		res->c[i].clock_cycles = dfi->total_count.c[i].clock_cycles +
			(u32)(now->c[i].clock_cycles - last->c[i].clock_cycles);
	}
}

static ssize_t ddr_perf_cpumask_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);
	struct rockchip_dfi *dfi = container_of(pmu, struct rockchip_dfi, pmu);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(dfi->cpu));
}

static struct device_attribute ddr_perf_cpumask_attr =
	__ATTR(cpumask, 0444, ddr_perf_cpumask_show, NULL);

static struct attribute *ddr_perf_cpumask_attrs[] = {
	&ddr_perf_cpumask_attr.attr,
	NULL,
};

static const struct attribute_group ddr_perf_cpumask_attr_group = {
	.attrs = ddr_perf_cpumask_attrs,
};

PMU_EVENT_ATTR_STRING(cycles, ddr_pmu_cycles, "event="__stringify(PERF_EVENT_CYCLES))

#define DFI_PMU_EVENT_ATTR(_name, _var, _str) \
	PMU_EVENT_ATTR_STRING(_name, _var, _str); \
	PMU_EVENT_ATTR_STRING(_name.unit, _var##_unit, "MB"); \
	PMU_EVENT_ATTR_STRING(_name.scale, _var##_scale, "9.536743164e-07")

DFI_PMU_EVENT_ATTR(read-bytes0, ddr_pmu_read_bytes0, "event="__stringify(PERF_EVENT_READ_BYTES0));
DFI_PMU_EVENT_ATTR(write-bytes0, ddr_pmu_write_bytes0, "event="__stringify(PERF_EVENT_WRITE_BYTES0));

DFI_PMU_EVENT_ATTR(read-bytes1, ddr_pmu_read_bytes1, "event="__stringify(PERF_EVENT_READ_BYTES1));
DFI_PMU_EVENT_ATTR(write-bytes1, ddr_pmu_write_bytes1, "event="__stringify(PERF_EVENT_WRITE_BYTES1));

DFI_PMU_EVENT_ATTR(read-bytes2, ddr_pmu_read_bytes2, "event="__stringify(PERF_EVENT_READ_BYTES2));
DFI_PMU_EVENT_ATTR(write-bytes2, ddr_pmu_write_bytes2, "event="__stringify(PERF_EVENT_WRITE_BYTES2));

DFI_PMU_EVENT_ATTR(read-bytes3, ddr_pmu_read_bytes3, "event="__stringify(PERF_EVENT_READ_BYTES3));
DFI_PMU_EVENT_ATTR(write-bytes3, ddr_pmu_write_bytes3, "event="__stringify(PERF_EVENT_WRITE_BYTES3));

DFI_PMU_EVENT_ATTR(read-bytes, ddr_pmu_read_bytes, "event="__stringify(PERF_EVENT_READ_BYTES));
DFI_PMU_EVENT_ATTR(write-bytes, ddr_pmu_write_bytes, "event="__stringify(PERF_EVENT_WRITE_BYTES));

DFI_PMU_EVENT_ATTR(bytes, ddr_pmu_bytes, "event="__stringify(PERF_EVENT_BYTES));

#define DFI_ATTR_MB(_name) 		\
	&_name.attr.attr,		\
	&_name##_unit.attr.attr,	\
	&_name##_scale.attr.attr

static struct attribute *ddr_perf_events_attrs[] = {
	&ddr_pmu_cycles.attr.attr,
	DFI_ATTR_MB(ddr_pmu_read_bytes),
	DFI_ATTR_MB(ddr_pmu_write_bytes),
	DFI_ATTR_MB(ddr_pmu_read_bytes0),
	DFI_ATTR_MB(ddr_pmu_write_bytes0),
	DFI_ATTR_MB(ddr_pmu_read_bytes1),
	DFI_ATTR_MB(ddr_pmu_write_bytes1),
	DFI_ATTR_MB(ddr_pmu_read_bytes2),
	DFI_ATTR_MB(ddr_pmu_write_bytes2),
	DFI_ATTR_MB(ddr_pmu_read_bytes3),
	DFI_ATTR_MB(ddr_pmu_write_bytes3),
	DFI_ATTR_MB(ddr_pmu_bytes),
	NULL,
};

static const struct attribute_group ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = ddr_perf_events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-7");

static struct attribute *ddr_perf_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static const struct attribute_group ddr_perf_format_attr_group = {
	.name = "format",
	.attrs = ddr_perf_format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&ddr_perf_events_attr_group,
	&ddr_perf_cpumask_attr_group,
	&ddr_perf_format_attr_group,
	NULL,
};

static int rockchip_ddr_perf_event_init(struct perf_event *event)
{
	struct rockchip_dfi *dfi = container_of(event->pmu, struct rockchip_dfi, pmu);

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (event->attach_state & PERF_ATTACH_TASK)
		return -EINVAL;

	if (event->cpu < 0) {
		dev_warn(dfi->dev, "Can't provide per-task data!\n");
		return -EINVAL;
	}

	return 0;
}

static u64 rockchip_ddr_perf_event_get_count(struct perf_event *event)
{
	struct rockchip_dfi *dfi = container_of(event->pmu, struct rockchip_dfi, pmu);
	int blen = dfi->burst_len;
	struct dmc_count total, now;
	unsigned int seq;
	u64 count = 0;
	int i;

	rockchip_dfi_read_counters(dfi, &now);

	do {
		seq = read_seqbegin(&dfi->count_seqlock);
		rockchip_ddr_perf_counters_add(dfi, &now, &total);
	} while (read_seqretry(&dfi->count_seqlock, seq));

	switch (event->attr.config) {
	case PERF_EVENT_CYCLES:
		count = total.c[0].clock_cycles;
		break;
	case PERF_EVENT_READ_BYTES:
		for (i = 0; i < dfi->max_channels; i++)
			count += total.c[i].read_access * blen * dfi->buswidth[i];
		break;
	case PERF_EVENT_WRITE_BYTES:
		for (i = 0; i < dfi->max_channels; i++)
			count += total.c[i].write_access * blen * dfi->buswidth[i];
		break;
	case PERF_EVENT_READ_BYTES0:
		count = total.c[0].read_access * blen * dfi->buswidth[0];
		break;
	case PERF_EVENT_WRITE_BYTES0:
		count = total.c[0].write_access * blen * dfi->buswidth[0];
		break;
	case PERF_EVENT_READ_BYTES1:
		count = total.c[1].read_access * blen * dfi->buswidth[1];
		break;
	case PERF_EVENT_WRITE_BYTES1:
		count = total.c[1].write_access * blen * dfi->buswidth[1];
		break;
	case PERF_EVENT_READ_BYTES2:
		count = total.c[2].read_access * blen * dfi->buswidth[2];
		break;
	case PERF_EVENT_WRITE_BYTES2:
		count = total.c[2].write_access * blen * dfi->buswidth[2];
		break;
	case PERF_EVENT_READ_BYTES3:
		count = total.c[3].read_access * blen * dfi->buswidth[3];
		break;
	case PERF_EVENT_WRITE_BYTES3:
		count = total.c[3].write_access * blen * dfi->buswidth[3];
		break;
	case PERF_EVENT_BYTES:
		for (i = 0; i < dfi->max_channels; i++)
			count += total.c[i].access * blen * dfi->buswidth[i];
		break;
	}

	return count;
}

static void rockchip_ddr_perf_event_update(struct perf_event *event)
{
	u64 now;
	s64 prev;

	if (event->attr.config >= PERF_ACCESS_TYPE_MAX)
		return;

	now = rockchip_ddr_perf_event_get_count(event);
	prev = local64_xchg(&event->hw.prev_count, now);
	local64_add(now - prev, &event->count);
}

static void rockchip_ddr_perf_event_start(struct perf_event *event, int flags)
{
	u64 now = rockchip_ddr_perf_event_get_count(event);

	local64_set(&event->hw.prev_count, now);
}

static int rockchip_ddr_perf_event_add(struct perf_event *event, int flags)
{
	struct rockchip_dfi *dfi = container_of(event->pmu, struct rockchip_dfi, pmu);

	dfi->active_events++;

	if (dfi->active_events == 1) {
		dfi->total_count = (struct dmc_count){};
		rockchip_dfi_read_counters(dfi, &dfi->last_perf_count);
		hrtimer_start(&dfi->timer, ns_to_ktime(NSEC_PER_SEC), HRTIMER_MODE_REL);
	}

	if (flags & PERF_EF_START)
		rockchip_ddr_perf_event_start(event, flags);

	return 0;
}

static void rockchip_ddr_perf_event_stop(struct perf_event *event, int flags)
{
	rockchip_ddr_perf_event_update(event);
}

static void rockchip_ddr_perf_event_del(struct perf_event *event, int flags)
{
	struct rockchip_dfi *dfi = container_of(event->pmu, struct rockchip_dfi, pmu);

	rockchip_ddr_perf_event_stop(event, PERF_EF_UPDATE);

	dfi->active_events--;

	if (dfi->active_events == 0)
		hrtimer_cancel(&dfi->timer);
}

static enum hrtimer_restart rockchip_dfi_timer(struct hrtimer *timer)
{
	struct rockchip_dfi *dfi = container_of(timer, struct rockchip_dfi, timer);
	struct dmc_count now, total;

	rockchip_dfi_read_counters(dfi, &now);

	write_seqlock(&dfi->count_seqlock);

	rockchip_ddr_perf_counters_add(dfi, &now, &total);
	dfi->total_count = total;
	dfi->last_perf_count = now;

	write_sequnlock(&dfi->count_seqlock);

	hrtimer_forward_now(&dfi->timer, ns_to_ktime(NSEC_PER_SEC));

	return HRTIMER_RESTART;
};

static int ddr_perf_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct rockchip_dfi *dfi = hlist_entry_safe(node, struct rockchip_dfi, node);
	int target;

	if (cpu != dfi->cpu)
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&dfi->pmu, cpu, target);
	dfi->cpu = target;

	return 0;
}

static void rockchip_ddr_cpuhp_remove_state(void *data)
{
	struct rockchip_dfi *dfi = data;

	cpuhp_remove_multi_state(dfi->cpuhp_state);

	rockchip_dfi_disable(dfi);
}

static void rockchip_ddr_cpuhp_remove_instance(void *data)
{
	struct rockchip_dfi *dfi = data;

	cpuhp_state_remove_instance_nocalls(dfi->cpuhp_state, &dfi->node);
}

static void rockchip_ddr_perf_remove(void *data)
{
	struct rockchip_dfi *dfi = data;

	perf_pmu_unregister(&dfi->pmu);
}

static int rockchip_ddr_perf_init(struct rockchip_dfi *dfi)
{
	struct pmu *pmu = &dfi->pmu;
	int ret;

	seqlock_init(&dfi->count_seqlock);

	pmu->module = THIS_MODULE;
	pmu->capabilities = PERF_PMU_CAP_NO_EXCLUDE;
	pmu->task_ctx_nr = perf_invalid_context;
	pmu->attr_groups = attr_groups;
	pmu->event_init  = rockchip_ddr_perf_event_init;
	pmu->add = rockchip_ddr_perf_event_add;
	pmu->del = rockchip_ddr_perf_event_del;
	pmu->start = rockchip_ddr_perf_event_start;
	pmu->stop = rockchip_ddr_perf_event_stop;
	pmu->read = rockchip_ddr_perf_event_update;

	dfi->cpu = raw_smp_processor_id();

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "rockchip_ddr_perf_pmu",
				      NULL,
				      ddr_perf_offline_cpu);

	if (ret < 0) {
		dev_err(dfi->dev, "cpuhp_setup_state_multi failed: %d\n", ret);
		return ret;
	}

	dfi->cpuhp_state = ret;

	rockchip_dfi_enable(dfi);

	ret = devm_add_action_or_reset(dfi->dev, rockchip_ddr_cpuhp_remove_state, dfi);
	if (ret)
		return ret;

	ret = cpuhp_state_add_instance_nocalls(dfi->cpuhp_state, &dfi->node);
	if (ret) {
		dev_err(dfi->dev, "Error %d registering hotplug\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dfi->dev, rockchip_ddr_cpuhp_remove_instance, dfi);
	if (ret)
		return ret;

	hrtimer_init(&dfi->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dfi->timer.function = rockchip_dfi_timer;

	switch (dfi->ddr_type) {
	case ROCKCHIP_DDRTYPE_LPDDR2:
	case ROCKCHIP_DDRTYPE_LPDDR3:
		dfi->burst_len = 8;
		break;
	case ROCKCHIP_DDRTYPE_LPDDR4:
	case ROCKCHIP_DDRTYPE_LPDDR4X:
		dfi->burst_len = 16;
		break;
	}

	ret = perf_pmu_register(pmu, "rockchip_ddr", -1);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dfi->dev, rockchip_ddr_perf_remove, dfi);
}
#else
static int rockchip_ddr_perf_init(struct rockchip_dfi *dfi)
{
	return 0;
}
#endif

static int rk3399_dfi_init(struct rockchip_dfi *dfi)
{
	struct regmap *regmap_pmu = dfi->regmap_pmu;
	u32 val;

	dfi->clk = devm_clk_get(dfi->dev, "pclk_ddr_mon");
	if (IS_ERR(dfi->clk))
		return dev_err_probe(dfi->dev, PTR_ERR(dfi->clk),
				     "Cannot get the clk pclk_ddr_mon\n");

	/* get ddr type */
	regmap_read(regmap_pmu, RK3399_PMUGRF_OS_REG2, &val);
	dfi->ddr_type = FIELD_GET(RK3399_PMUGRF_OS_REG2_DDRTYPE, val);

	dfi->channel_mask = GENMASK(1, 0);
	dfi->max_channels = 2;

	dfi->buswidth[0] = FIELD_GET(RK3399_PMUGRF_OS_REG2_BW_CH0, val) == 0 ? 4 : 2;
	dfi->buswidth[1] = FIELD_GET(RK3399_PMUGRF_OS_REG2_BW_CH1, val) == 0 ? 4 : 2;

	dfi->ddrmon_stride = 0x14;
	dfi->ddrmon_ctrl_single = true;

	return 0;
};

static int rk3568_dfi_init(struct rockchip_dfi *dfi)
{
	struct regmap *regmap_pmu = dfi->regmap_pmu;
	u32 reg2, reg3;

	regmap_read(regmap_pmu, RK3568_PMUGRF_OS_REG2, &reg2);
	regmap_read(regmap_pmu, RK3568_PMUGRF_OS_REG3, &reg3);

	/* lower 3 bits of the DDR type */
	dfi->ddr_type = FIELD_GET(RK3568_PMUGRF_OS_REG2_DRAMTYPE_INFO, reg2);

	/*
	 * For version three and higher the upper two bits of the DDR type are
	 * in RK3568_PMUGRF_OS_REG3
	 */
	if (FIELD_GET(RK3568_PMUGRF_OS_REG3_SYSREG_VERSION, reg3) >= 0x3)
		dfi->ddr_type |= FIELD_GET(RK3568_PMUGRF_OS_REG3_DRAMTYPE_INFO_V3, reg3) << 3;

	dfi->channel_mask = BIT(0);
	dfi->max_channels = 1;

	dfi->buswidth[0] = FIELD_GET(RK3568_PMUGRF_OS_REG2_BW_CH0, reg2) == 0 ? 4 : 2;

	dfi->ddrmon_stride = 0x0; /* not relevant, we only have a single channel on this SoC */
	dfi->ddrmon_ctrl_single = true;

	return 0;
};

static int rk3588_dfi_init(struct rockchip_dfi *dfi)
{
	struct regmap *regmap_pmu = dfi->regmap_pmu;
	u32 reg2, reg3, reg4;

	regmap_read(regmap_pmu, RK3588_PMUGRF_OS_REG2, &reg2);
	regmap_read(regmap_pmu, RK3588_PMUGRF_OS_REG3, &reg3);
	regmap_read(regmap_pmu, RK3588_PMUGRF_OS_REG4, &reg4);

	/* lower 3 bits of the DDR type */
	dfi->ddr_type = FIELD_GET(RK3588_PMUGRF_OS_REG2_DRAMTYPE_INFO, reg2);

	/*
	 * For version three and higher the upper two bits of the DDR type are
	 * in RK3588_PMUGRF_OS_REG3
	 */
	if (FIELD_GET(RK3588_PMUGRF_OS_REG3_SYSREG_VERSION, reg3) >= 0x3)
		dfi->ddr_type |= FIELD_GET(RK3588_PMUGRF_OS_REG3_DRAMTYPE_INFO_V3, reg3) << 3;

	dfi->buswidth[0] = FIELD_GET(RK3588_PMUGRF_OS_REG2_BW_CH0, reg2) == 0 ? 4 : 2;
	dfi->buswidth[1] = FIELD_GET(RK3588_PMUGRF_OS_REG2_BW_CH1, reg2) == 0 ? 4 : 2;
	dfi->buswidth[2] = FIELD_GET(RK3568_PMUGRF_OS_REG2_BW_CH0, reg4) == 0 ? 4 : 2;
	dfi->buswidth[3] = FIELD_GET(RK3588_PMUGRF_OS_REG2_BW_CH1, reg4) == 0 ? 4 : 2;
	dfi->channel_mask = FIELD_GET(RK3588_PMUGRF_OS_REG2_CH_INFO, reg2) |
			    FIELD_GET(RK3588_PMUGRF_OS_REG2_CH_INFO, reg4) << 2;
	dfi->max_channels = 4;

	dfi->ddrmon_stride = 0x4000;

	return 0;
};

static const struct of_device_id rockchip_dfi_id_match[] = {
	{ .compatible = "rockchip,rk3399-dfi", .data = rk3399_dfi_init },
	{ .compatible = "rockchip,rk3568-dfi", .data = rk3568_dfi_init },
	{ .compatible = "rockchip,rk3588-dfi", .data = rk3588_dfi_init },
	{ },
};

MODULE_DEVICE_TABLE(of, rockchip_dfi_id_match);

static int rockchip_dfi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_dfi *dfi;
	struct devfreq_event_desc *desc;
	struct device_node *np = pdev->dev.of_node, *node;
	int (*soc_init)(struct rockchip_dfi *dfi);
	int ret;

	soc_init = of_device_get_match_data(&pdev->dev);
	if (!soc_init)
		return -EINVAL;

	dfi = devm_kzalloc(dev, sizeof(*dfi), GFP_KERNEL);
	if (!dfi)
		return -ENOMEM;

	dfi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dfi->regs))
		return PTR_ERR(dfi->regs);

	node = of_parse_phandle(np, "rockchip,pmu", 0);
	if (!node)
		return dev_err_probe(&pdev->dev, -ENODEV, "Can't find pmu_grf registers\n");

	dfi->regmap_pmu = syscon_node_to_regmap(node);
	of_node_put(node);
	if (IS_ERR(dfi->regmap_pmu))
		return PTR_ERR(dfi->regmap_pmu);

	dfi->dev = dev;
	mutex_init(&dfi->mutex);

	desc = &dfi->desc;
	desc->ops = &rockchip_dfi_ops;
	desc->driver_data = dfi;
	desc->name = np->name;

	ret = soc_init(dfi);
	if (ret)
		return ret;

	dfi->edev = devm_devfreq_event_add_edev(&pdev->dev, desc);
	if (IS_ERR(dfi->edev)) {
		dev_err(&pdev->dev,
			"failed to add devfreq-event device\n");
		return PTR_ERR(dfi->edev);
	}

	ret = rockchip_ddr_perf_init(dfi);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, dfi);

	return 0;
}

static struct platform_driver rockchip_dfi_driver = {
	.probe	= rockchip_dfi_probe,
	.driver = {
		.name	= "rockchip-dfi",
		.of_match_table = rockchip_dfi_id_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(rockchip_dfi_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lin Huang <hl@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip DFI driver");
