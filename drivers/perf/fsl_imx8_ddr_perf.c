// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2017 NXP
 * Copyright 2016 Freescale Semiconductor, Inc.
 */

#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define COUNTER_CNTL		0x0
#define COUNTER_READ		0x20

#define COUNTER_DPCR1		0x30
#define COUNTER_MUX_CNTL	0x50
#define COUNTER_MASK_COMP	0x54

#define CNTL_OVER		0x1
#define CNTL_CLEAR		0x2
#define CNTL_EN			0x4
#define CNTL_EN_MASK		0xFFFFFFFB
#define CNTL_CLEAR_MASK		0xFFFFFFFD
#define CNTL_OVER_MASK		0xFFFFFFFE

#define CNTL_CP_SHIFT		16
#define CNTL_CP_MASK		(0xFF << CNTL_CP_SHIFT)
#define CNTL_CSV_SHIFT		24
#define CNTL_CSV_MASK		(0xFFU << CNTL_CSV_SHIFT)

#define READ_PORT_SHIFT		0
#define READ_PORT_MASK		(0x7 << READ_PORT_SHIFT)
#define READ_CHANNEL_REVERT	0x00000008	/* bit 3 for read channel select */
#define WRITE_PORT_SHIFT	8
#define WRITE_PORT_MASK		(0x7 << WRITE_PORT_SHIFT)
#define WRITE_CHANNEL_REVERT	0x00000800	/* bit 11 for write channel select */

#define EVENT_CYCLES_ID		0
#define EVENT_CYCLES_COUNTER	0
#define NUM_COUNTERS		4

/* For removing bias if cycle counter CNTL.CP is set to 0xf0 */
#define CYCLES_COUNTER_MASK	0x0FFFFFFF
#define AXI_MASKING_REVERT	0xffff0000	/* AXI_MASKING(MSB 16bits) + AXI_ID(LSB 16bits) */

#define to_ddr_pmu(p)		container_of(p, struct ddr_pmu, pmu)

#define DDR_PERF_DEV_NAME	"imx8_ddr"
#define DDR_CPUHP_CB_NAME	DDR_PERF_DEV_NAME "_perf_pmu"

static DEFINE_IDA(ddr_ida);

/* DDR Perf hardware feature */
#define DDR_CAP_AXI_ID_FILTER			0x1     /* support AXI ID filter */
#define DDR_CAP_AXI_ID_FILTER_ENHANCED		0x3     /* support enhanced AXI ID filter */
#define DDR_CAP_AXI_ID_PORT_CHANNEL_FILTER	0x4	/* support AXI ID PORT CHANNEL filter */

struct fsl_ddr_devtype_data {
	unsigned int quirks;    /* quirks needed for different DDR Perf core */
	const char *identifier;	/* system PMU identifier for userspace */
};

static const struct fsl_ddr_devtype_data imx8_devtype_data;

static const struct fsl_ddr_devtype_data imx8m_devtype_data = {
	.quirks = DDR_CAP_AXI_ID_FILTER,
};

static const struct fsl_ddr_devtype_data imx8mq_devtype_data = {
	.quirks = DDR_CAP_AXI_ID_FILTER,
	.identifier = "i.MX8MQ",
};

static const struct fsl_ddr_devtype_data imx8mm_devtype_data = {
	.quirks = DDR_CAP_AXI_ID_FILTER,
	.identifier = "i.MX8MM",
};

static const struct fsl_ddr_devtype_data imx8mn_devtype_data = {
	.quirks = DDR_CAP_AXI_ID_FILTER,
	.identifier = "i.MX8MN",
};

static const struct fsl_ddr_devtype_data imx8mp_devtype_data = {
	.quirks = DDR_CAP_AXI_ID_FILTER_ENHANCED,
	.identifier = "i.MX8MP",
};

static const struct fsl_ddr_devtype_data imx8dxl_devtype_data = {
	.quirks = DDR_CAP_AXI_ID_PORT_CHANNEL_FILTER,
	.identifier = "i.MX8DXL",
};

static const struct of_device_id imx_ddr_pmu_dt_ids[] = {
	{ .compatible = "fsl,imx8-ddr-pmu", .data = &imx8_devtype_data},
	{ .compatible = "fsl,imx8m-ddr-pmu", .data = &imx8m_devtype_data},
	{ .compatible = "fsl,imx8mq-ddr-pmu", .data = &imx8mq_devtype_data},
	{ .compatible = "fsl,imx8mm-ddr-pmu", .data = &imx8mm_devtype_data},
	{ .compatible = "fsl,imx8mn-ddr-pmu", .data = &imx8mn_devtype_data},
	{ .compatible = "fsl,imx8mp-ddr-pmu", .data = &imx8mp_devtype_data},
	{ .compatible = "fsl,imx8dxl-ddr-pmu", .data = &imx8dxl_devtype_data},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_ddr_pmu_dt_ids);

struct ddr_pmu {
	struct pmu pmu;
	void __iomem *base;
	unsigned int cpu;
	struct	hlist_node node;
	struct	device *dev;
	struct perf_event *events[NUM_COUNTERS];
	enum cpuhp_state cpuhp_state;
	const struct fsl_ddr_devtype_data *devtype_data;
	int irq;
	int id;
	int active_counter;
};

static ssize_t ddr_perf_identifier_show(struct device *dev,
					struct device_attribute *attr,
					char *page)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev);

	return sysfs_emit(page, "%s\n", pmu->devtype_data->identifier);
}

static umode_t ddr_perf_identifier_attr_visible(struct kobject *kobj,
						struct attribute *attr,
						int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct ddr_pmu *pmu = dev_get_drvdata(dev);

	if (!pmu->devtype_data->identifier)
		return 0;
	return attr->mode;
};

static struct device_attribute ddr_perf_identifier_attr =
	__ATTR(identifier, 0444, ddr_perf_identifier_show, NULL);

static struct attribute *ddr_perf_identifier_attrs[] = {
	&ddr_perf_identifier_attr.attr,
	NULL,
};

static const struct attribute_group ddr_perf_identifier_attr_group = {
	.attrs = ddr_perf_identifier_attrs,
	.is_visible = ddr_perf_identifier_attr_visible,
};

enum ddr_perf_filter_capabilities {
	PERF_CAP_AXI_ID_FILTER = 0,
	PERF_CAP_AXI_ID_FILTER_ENHANCED,
	PERF_CAP_AXI_ID_PORT_CHANNEL_FILTER,
	PERF_CAP_AXI_ID_FEAT_MAX,
};

static u32 ddr_perf_filter_cap_get(struct ddr_pmu *pmu, int cap)
{
	u32 quirks = pmu->devtype_data->quirks;

	switch (cap) {
	case PERF_CAP_AXI_ID_FILTER:
		return !!(quirks & DDR_CAP_AXI_ID_FILTER);
	case PERF_CAP_AXI_ID_FILTER_ENHANCED:
		quirks &= DDR_CAP_AXI_ID_FILTER_ENHANCED;
		return quirks == DDR_CAP_AXI_ID_FILTER_ENHANCED;
	case PERF_CAP_AXI_ID_PORT_CHANNEL_FILTER:
		return !!(quirks & DDR_CAP_AXI_ID_PORT_CHANNEL_FILTER);
	default:
		WARN(1, "unknown filter cap %d\n", cap);
	}

	return 0;
}

static ssize_t ddr_perf_filter_cap_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev);
	struct dev_ext_attribute *ea =
		container_of(attr, struct dev_ext_attribute, attr);
	int cap = (long)ea->var;

	return sysfs_emit(buf, "%u\n", ddr_perf_filter_cap_get(pmu, cap));
}

#define PERF_EXT_ATTR_ENTRY(_name, _func, _var)				\
	(&((struct dev_ext_attribute) {					\
		__ATTR(_name, 0444, _func, NULL), (void *)_var		\
	}).attr.attr)

#define PERF_FILTER_EXT_ATTR_ENTRY(_name, _var)				\
	PERF_EXT_ATTR_ENTRY(_name, ddr_perf_filter_cap_show, _var)

static struct attribute *ddr_perf_filter_cap_attr[] = {
	PERF_FILTER_EXT_ATTR_ENTRY(filter, PERF_CAP_AXI_ID_FILTER),
	PERF_FILTER_EXT_ATTR_ENTRY(enhanced_filter, PERF_CAP_AXI_ID_FILTER_ENHANCED),
	PERF_FILTER_EXT_ATTR_ENTRY(super_filter, PERF_CAP_AXI_ID_PORT_CHANNEL_FILTER),
	NULL,
};

static const struct attribute_group ddr_perf_filter_cap_attr_group = {
	.name = "caps",
	.attrs = ddr_perf_filter_cap_attr,
};

static ssize_t ddr_perf_cpumask_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(pmu->cpu));
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

static ssize_t
ddr_pmu_event_show(struct device *dev, struct device_attribute *attr,
		   char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

#define IMX8_DDR_PMU_EVENT_ATTR(_name, _id)		\
	PMU_EVENT_ATTR_ID(_name, ddr_pmu_event_show, _id)

static struct attribute *ddr_perf_events_attrs[] = {
	IMX8_DDR_PMU_EVENT_ATTR(cycles, EVENT_CYCLES_ID),
	IMX8_DDR_PMU_EVENT_ATTR(selfresh, 0x01),
	IMX8_DDR_PMU_EVENT_ATTR(read-accesses, 0x04),
	IMX8_DDR_PMU_EVENT_ATTR(write-accesses, 0x05),
	IMX8_DDR_PMU_EVENT_ATTR(read-queue-depth, 0x08),
	IMX8_DDR_PMU_EVENT_ATTR(write-queue-depth, 0x09),
	IMX8_DDR_PMU_EVENT_ATTR(lp-read-credit-cnt, 0x10),
	IMX8_DDR_PMU_EVENT_ATTR(hp-read-credit-cnt, 0x11),
	IMX8_DDR_PMU_EVENT_ATTR(write-credit-cnt, 0x12),
	IMX8_DDR_PMU_EVENT_ATTR(read-command, 0x20),
	IMX8_DDR_PMU_EVENT_ATTR(write-command, 0x21),
	IMX8_DDR_PMU_EVENT_ATTR(read-modify-write-command, 0x22),
	IMX8_DDR_PMU_EVENT_ATTR(hp-read, 0x23),
	IMX8_DDR_PMU_EVENT_ATTR(hp-req-nocredit, 0x24),
	IMX8_DDR_PMU_EVENT_ATTR(hp-xact-credit, 0x25),
	IMX8_DDR_PMU_EVENT_ATTR(lp-req-nocredit, 0x26),
	IMX8_DDR_PMU_EVENT_ATTR(lp-xact-credit, 0x27),
	IMX8_DDR_PMU_EVENT_ATTR(wr-xact-credit, 0x29),
	IMX8_DDR_PMU_EVENT_ATTR(read-cycles, 0x2a),
	IMX8_DDR_PMU_EVENT_ATTR(write-cycles, 0x2b),
	IMX8_DDR_PMU_EVENT_ATTR(read-write-transition, 0x30),
	IMX8_DDR_PMU_EVENT_ATTR(precharge, 0x31),
	IMX8_DDR_PMU_EVENT_ATTR(activate, 0x32),
	IMX8_DDR_PMU_EVENT_ATTR(load-mode, 0x33),
	IMX8_DDR_PMU_EVENT_ATTR(perf-mwr, 0x34),
	IMX8_DDR_PMU_EVENT_ATTR(read, 0x35),
	IMX8_DDR_PMU_EVENT_ATTR(read-activate, 0x36),
	IMX8_DDR_PMU_EVENT_ATTR(refresh, 0x37),
	IMX8_DDR_PMU_EVENT_ATTR(write, 0x38),
	IMX8_DDR_PMU_EVENT_ATTR(raw-hazard, 0x39),
	IMX8_DDR_PMU_EVENT_ATTR(axid-read, 0x41),
	IMX8_DDR_PMU_EVENT_ATTR(axid-write, 0x42),
	NULL,
};

static const struct attribute_group ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = ddr_perf_events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-7");
PMU_FORMAT_ATTR(axi_id, "config1:0-15");
PMU_FORMAT_ATTR(axi_mask, "config1:16-31");
PMU_FORMAT_ATTR(axi_port, "config2:0-2");
PMU_FORMAT_ATTR(axi_channel, "config2:3-3");

static struct attribute *ddr_perf_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_axi_id.attr,
	&format_attr_axi_mask.attr,
	&format_attr_axi_port.attr,
	&format_attr_axi_channel.attr,
	NULL,
};

static const struct attribute_group ddr_perf_format_attr_group = {
	.name = "format",
	.attrs = ddr_perf_format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&ddr_perf_events_attr_group,
	&ddr_perf_format_attr_group,
	&ddr_perf_cpumask_attr_group,
	&ddr_perf_filter_cap_attr_group,
	&ddr_perf_identifier_attr_group,
	NULL,
};

static bool ddr_perf_is_filtered(struct perf_event *event)
{
	return event->attr.config == 0x41 || event->attr.config == 0x42;
}

static u32 ddr_perf_filter_val(struct perf_event *event)
{
	return event->attr.config1;
}

static bool ddr_perf_filters_compatible(struct perf_event *a,
					struct perf_event *b)
{
	if (!ddr_perf_is_filtered(a))
		return true;
	if (!ddr_perf_is_filtered(b))
		return true;
	return ddr_perf_filter_val(a) == ddr_perf_filter_val(b);
}

static bool ddr_perf_is_enhanced_filtered(struct perf_event *event)
{
	unsigned int filt;
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);

	filt = pmu->devtype_data->quirks & DDR_CAP_AXI_ID_FILTER_ENHANCED;
	return (filt == DDR_CAP_AXI_ID_FILTER_ENHANCED) &&
		ddr_perf_is_filtered(event);
}

static u32 ddr_perf_alloc_counter(struct ddr_pmu *pmu, int event)
{
	int i;

	/*
	 * Always map cycle event to counter 0
	 * Cycles counter is dedicated for cycle event
	 * can't used for the other events
	 */
	if (event == EVENT_CYCLES_ID) {
		if (pmu->events[EVENT_CYCLES_COUNTER] == NULL)
			return EVENT_CYCLES_COUNTER;
		else
			return -ENOENT;
	}

	for (i = 1; i < NUM_COUNTERS; i++) {
		if (pmu->events[i] == NULL)
			return i;
	}

	return -ENOENT;
}

static void ddr_perf_free_counter(struct ddr_pmu *pmu, int counter)
{
	pmu->events[counter] = NULL;
}

static u32 ddr_perf_read_counter(struct ddr_pmu *pmu, int counter)
{
	struct perf_event *event = pmu->events[counter];
	void __iomem *base = pmu->base;

	/*
	 * return bytes instead of bursts from ddr transaction for
	 * axid-read and axid-write event if PMU core supports enhanced
	 * filter.
	 */
	base += ddr_perf_is_enhanced_filtered(event) ? COUNTER_DPCR1 :
						       COUNTER_READ;
	return readl_relaxed(base + counter * 4);
}

static int ddr_perf_event_init(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	struct perf_event *sibling;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	if (event->cpu < 0) {
		dev_warn(pmu->dev, "Can't provide per-task data!\n");
		return -EOPNOTSUPP;
	}

	/*
	 * We must NOT create groups containing mixed PMUs, although software
	 * events are acceptable (for example to create a CCN group
	 * periodically read when a hrtimer aka cpu-clock leader triggers).
	 */
	if (event->group_leader->pmu != event->pmu &&
			!is_software_event(event->group_leader))
		return -EINVAL;

	if (pmu->devtype_data->quirks & DDR_CAP_AXI_ID_FILTER) {
		if (!ddr_perf_filters_compatible(event, event->group_leader))
			return -EINVAL;
		for_each_sibling_event(sibling, event->group_leader) {
			if (!ddr_perf_filters_compatible(event, sibling))
				return -EINVAL;
		}
	}

	for_each_sibling_event(sibling, event->group_leader) {
		if (sibling->pmu != event->pmu &&
				!is_software_event(sibling))
			return -EINVAL;
	}

	event->cpu = pmu->cpu;
	hwc->idx = -1;

	return 0;
}

static void ddr_perf_counter_enable(struct ddr_pmu *pmu, int config,
				  int counter, bool enable)
{
	u8 reg = counter * 4 + COUNTER_CNTL;
	int val;

	if (enable) {
		/*
		 * cycle counter is special which should firstly write 0 then
		 * write 1 into CLEAR bit to clear it. Other counters only
		 * need write 0 into CLEAR bit and it turns out to be 1 by
		 * hardware. Below enable flow is harmless for all counters.
		 */
		writel(0, pmu->base + reg);
		val = CNTL_EN | CNTL_CLEAR;
		val |= FIELD_PREP(CNTL_CSV_MASK, config);

		/*
		 * On i.MX8MP we need to bias the cycle counter to overflow more often.
		 * We do this by initializing bits [23:16] of the counter value via the
		 * COUNTER_CTRL Counter Parameter (CP) field.
		 */
		if (pmu->devtype_data->quirks & DDR_CAP_AXI_ID_FILTER_ENHANCED) {
			if (counter == EVENT_CYCLES_COUNTER)
				val |= FIELD_PREP(CNTL_CP_MASK, 0xf0);
		}

		writel(val, pmu->base + reg);
	} else {
		/* Disable counter */
		val = readl_relaxed(pmu->base + reg) & CNTL_EN_MASK;
		writel(val, pmu->base + reg);
	}
}

static bool ddr_perf_counter_overflow(struct ddr_pmu *pmu, int counter)
{
	int val;

	val = readl_relaxed(pmu->base + counter * 4 + COUNTER_CNTL);

	return val & CNTL_OVER;
}

static void ddr_perf_counter_clear(struct ddr_pmu *pmu, int counter)
{
	u8 reg = counter * 4 + COUNTER_CNTL;
	int val;

	val = readl_relaxed(pmu->base + reg);
	val &= ~CNTL_CLEAR;
	writel(val, pmu->base + reg);

	val |= CNTL_CLEAR;
	writel(val, pmu->base + reg);
}

static void ddr_perf_event_update(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 new_raw_count;
	int counter = hwc->idx;
	int ret;

	new_raw_count = ddr_perf_read_counter(pmu, counter);
	/* Remove the bias applied in ddr_perf_counter_enable(). */
	if (pmu->devtype_data->quirks & DDR_CAP_AXI_ID_FILTER_ENHANCED) {
		if (counter == EVENT_CYCLES_COUNTER)
			new_raw_count &= CYCLES_COUNTER_MASK;
	}

	local64_add(new_raw_count, &event->count);

	/*
	 * For legacy SoCs: event counter continue counting when overflow,
	 *                  no need to clear the counter.
	 * For new SoCs: event counter stop counting when overflow, need
	 *               clear counter to let it count again.
	 */
	if (counter != EVENT_CYCLES_COUNTER) {
		ret = ddr_perf_counter_overflow(pmu, counter);
		if (ret)
			dev_warn_ratelimited(pmu->dev,  "events lost due to counter overflow (config 0x%llx)\n",
					     event->attr.config);
	}

	/* clear counter every time for both cycle counter and event counter */
	ddr_perf_counter_clear(pmu, counter);
}

static void ddr_perf_event_start(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	local64_set(&hwc->prev_count, 0);

	ddr_perf_counter_enable(pmu, event->attr.config, counter, true);

	if (!pmu->active_counter++)
		ddr_perf_counter_enable(pmu, EVENT_CYCLES_ID,
			EVENT_CYCLES_COUNTER, true);

	hwc->state = 0;
}

static int ddr_perf_event_add(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter;
	int cfg = event->attr.config;
	int cfg1 = event->attr.config1;
	int cfg2 = event->attr.config2;

	if (pmu->devtype_data->quirks & DDR_CAP_AXI_ID_FILTER) {
		int i;

		for (i = 1; i < NUM_COUNTERS; i++) {
			if (pmu->events[i] &&
			    !ddr_perf_filters_compatible(event, pmu->events[i]))
				return -EINVAL;
		}

		if (ddr_perf_is_filtered(event)) {
			/* revert axi id masking(axi_mask) value */
			cfg1 ^= AXI_MASKING_REVERT;
			writel(cfg1, pmu->base + COUNTER_DPCR1);
		}
	}

	counter = ddr_perf_alloc_counter(pmu, cfg);
	if (counter < 0) {
		dev_dbg(pmu->dev, "There are not enough counters\n");
		return -EOPNOTSUPP;
	}

	if (pmu->devtype_data->quirks & DDR_CAP_AXI_ID_PORT_CHANNEL_FILTER) {
		if (ddr_perf_is_filtered(event)) {
			/* revert axi id masking(axi_mask) value */
			cfg1 ^= AXI_MASKING_REVERT;
			writel(cfg1, pmu->base + COUNTER_MASK_COMP + ((counter - 1) << 4));

			if (cfg == 0x41) {
				/* revert axi read channel(axi_channel) value */
				cfg2 ^= READ_CHANNEL_REVERT;
				cfg2 |= FIELD_PREP(READ_PORT_MASK, cfg2);
			} else {
				/* revert axi write channel(axi_channel) value */
				cfg2 ^= WRITE_CHANNEL_REVERT;
				cfg2 |= FIELD_PREP(WRITE_PORT_MASK, cfg2);
			}

			writel(cfg2, pmu->base + COUNTER_MUX_CNTL + ((counter - 1) << 4));
		}
	}

	pmu->events[counter] = event;
	hwc->idx = counter;

	hwc->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		ddr_perf_event_start(event, flags);

	return 0;
}

static void ddr_perf_event_stop(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	ddr_perf_counter_enable(pmu, event->attr.config, counter, false);
	ddr_perf_event_update(event);

	if (!--pmu->active_counter)
		ddr_perf_counter_enable(pmu, EVENT_CYCLES_ID,
			EVENT_CYCLES_COUNTER, false);

	hwc->state |= PERF_HES_STOPPED;
}

static void ddr_perf_event_del(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	ddr_perf_event_stop(event, PERF_EF_UPDATE);

	ddr_perf_free_counter(pmu, counter);
	hwc->idx = -1;
}

static void ddr_perf_pmu_enable(struct pmu *pmu)
{
}

static void ddr_perf_pmu_disable(struct pmu *pmu)
{
}

static int ddr_perf_init(struct ddr_pmu *pmu, void __iomem *base,
			 struct device *dev)
{
	*pmu = (struct ddr_pmu) {
		.pmu = (struct pmu) {
			.module	      = THIS_MODULE,
			.parent      = dev,
			.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
			.task_ctx_nr = perf_invalid_context,
			.attr_groups = attr_groups,
			.event_init  = ddr_perf_event_init,
			.add	     = ddr_perf_event_add,
			.del	     = ddr_perf_event_del,
			.start	     = ddr_perf_event_start,
			.stop	     = ddr_perf_event_stop,
			.read	     = ddr_perf_event_update,
			.pmu_enable  = ddr_perf_pmu_enable,
			.pmu_disable = ddr_perf_pmu_disable,
		},
		.base = base,
		.dev = dev,
	};

	pmu->id = ida_alloc(&ddr_ida, GFP_KERNEL);
	return pmu->id;
}

static irqreturn_t ddr_perf_irq_handler(int irq, void *p)
{
	int i;
	struct ddr_pmu *pmu = (struct ddr_pmu *) p;
	struct perf_event *event;

	/* all counter will stop if cycle counter disabled */
	ddr_perf_counter_enable(pmu,
			      EVENT_CYCLES_ID,
			      EVENT_CYCLES_COUNTER,
			      false);
	/*
	 * When the cycle counter overflows, all counters are stopped,
	 * and an IRQ is raised. If any other counter overflows, it
	 * continues counting, and no IRQ is raised. But for new SoCs,
	 * such as i.MX8MP, event counter would stop when overflow, so
	 * we need use cycle counter to stop overflow of event counter.
	 *
	 * Cycles occur at least 4 times as often as other events, so we
	 * can update all events on a cycle counter overflow and not
	 * lose events.
	 *
	 */
	for (i = 0; i < NUM_COUNTERS; i++) {

		if (!pmu->events[i])
			continue;

		event = pmu->events[i];

		ddr_perf_event_update(event);
	}

	ddr_perf_counter_enable(pmu,
			      EVENT_CYCLES_ID,
			      EVENT_CYCLES_COUNTER,
			      true);

	return IRQ_HANDLED;
}

static int ddr_perf_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct ddr_pmu *pmu = hlist_entry_safe(node, struct ddr_pmu, node);
	int target;

	if (cpu != pmu->cpu)
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&pmu->pmu, cpu, target);
	pmu->cpu = target;

	WARN_ON(irq_set_affinity(pmu->irq, cpumask_of(pmu->cpu)));

	return 0;
}

static int ddr_perf_probe(struct platform_device *pdev)
{
	struct ddr_pmu *pmu;
	struct device_node *np;
	void __iomem *base;
	char *name;
	int num;
	int ret;
	int irq;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	np = pdev->dev.of_node;

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	num = ddr_perf_init(pmu, base, &pdev->dev);

	platform_set_drvdata(pdev, pmu);

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, DDR_PERF_DEV_NAME "%d",
			      num);
	if (!name) {
		ret = -ENOMEM;
		goto cpuhp_state_err;
	}

	pmu->devtype_data = of_device_get_match_data(&pdev->dev);

	pmu->cpu = raw_smp_processor_id();
	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      DDR_CPUHP_CB_NAME,
				      NULL,
				      ddr_perf_offline_cpu);

	if (ret < 0) {
		dev_err(&pdev->dev, "cpuhp_setup_state_multi failed\n");
		goto cpuhp_state_err;
	}

	pmu->cpuhp_state = ret;

	/* Register the pmu instance for cpu hotplug */
	ret = cpuhp_state_add_instance_nocalls(pmu->cpuhp_state, &pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		goto cpuhp_instance_err;
	}

	/* Request irq */
	irq = of_irq_get(np, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq: %d", irq);
		ret = irq;
		goto ddr_perf_err;
	}

	ret = devm_request_irq(&pdev->dev, irq,
					ddr_perf_irq_handler,
					IRQF_NOBALANCING | IRQF_NO_THREAD,
					DDR_CPUHP_CB_NAME,
					pmu);
	if (ret < 0) {
		dev_err(&pdev->dev, "Request irq failed: %d", ret);
		goto ddr_perf_err;
	}

	pmu->irq = irq;
	ret = irq_set_affinity(pmu->irq, cpumask_of(pmu->cpu));
	if (ret) {
		dev_err(pmu->dev, "Failed to set interrupt affinity!\n");
		goto ddr_perf_err;
	}

	ret = perf_pmu_register(&pmu->pmu, name, -1);
	if (ret)
		goto ddr_perf_err;

	return 0;

ddr_perf_err:
	cpuhp_state_remove_instance_nocalls(pmu->cpuhp_state, &pmu->node);
cpuhp_instance_err:
	cpuhp_remove_multi_state(pmu->cpuhp_state);
cpuhp_state_err:
	ida_free(&ddr_ida, pmu->id);
	dev_warn(&pdev->dev, "i.MX8 DDR Perf PMU failed (%d), disabled\n", ret);
	return ret;
}

static void ddr_perf_remove(struct platform_device *pdev)
{
	struct ddr_pmu *pmu = platform_get_drvdata(pdev);

	cpuhp_state_remove_instance_nocalls(pmu->cpuhp_state, &pmu->node);
	cpuhp_remove_multi_state(pmu->cpuhp_state);

	perf_pmu_unregister(&pmu->pmu);

	ida_free(&ddr_ida, pmu->id);
}

static struct platform_driver imx_ddr_pmu_driver = {
	.driver         = {
		.name   = "imx-ddr-pmu",
		.of_match_table = imx_ddr_pmu_dt_ids,
		.suppress_bind_attrs = true,
	},
	.probe          = ddr_perf_probe,
	.remove_new     = ddr_perf_remove,
};

module_platform_driver(imx_ddr_pmu_driver);
MODULE_LICENSE("GPL v2");
