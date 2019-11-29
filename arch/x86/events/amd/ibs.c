/*
 * Performance events - AMD IBS
 *
 *  Copyright (C) 2011 Advanced Micro Devices, Inc., Robert Richter
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/perf_event.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/syscore_ops.h>
#include <linux/sched/clock.h>

#include <asm/apic.h>

#include "../perf_event.h"

static u32 ibs_caps;

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_AMD)

#include <linux/kprobes.h>
#include <linux/hardirq.h>

#include <asm/nmi.h>

#define IBS_FETCH_CONFIG_MASK	(IBS_FETCH_RAND_EN | IBS_FETCH_MAX_CNT)
#define IBS_OP_CONFIG_MASK	IBS_OP_MAX_CNT


/*
 * IBS states:
 *
 * ENABLED; tracks the pmu::add(), pmu::del() state, when set the counter is taken
 * and any further add()s must fail.
 *
 * STARTED/STOPPING/STOPPED; deal with pmu::start(), pmu::stop() state but are
 * complicated by the fact that the IBS hardware can send late NMIs (ie. after
 * we've cleared the EN bit).
 *
 * In order to consume these late NMIs we have the STOPPED state, any NMI that
 * happens after we've cleared the EN state will clear this bit and report the
 * NMI handled (this is fundamentally racy in the face or multiple NMI sources,
 * someone else can consume our BIT and our NMI will go unhandled).
 *
 * And since we cannot set/clear this separate bit together with the EN bit,
 * there are races; if we cleared STARTED early, an NMI could land in
 * between clearing STARTED and clearing the EN bit (in fact multiple NMIs
 * could happen if the period is small enough), and consume our STOPPED bit
 * and trigger streams of unhandled NMIs.
 *
 * If, however, we clear STARTED late, an NMI can hit between clearing the
 * EN bit and clearing STARTED, still see STARTED set and process the event.
 * If this event will have the VALID bit clear, we bail properly, but this
 * is not a given. With VALID set we can end up calling pmu::stop() again
 * (the throttle logic) and trigger the WARNs in there.
 *
 * So what we do is set STOPPING before clearing EN to avoid the pmu::stop()
 * nesting, and clear STARTED late, so that we have a well defined state over
 * the clearing of the EN bit.
 *
 * XXX: we could probably be using !atomic bitops for all this.
 */

enum ibs_states {
	IBS_ENABLED	= 0,
	IBS_STARTED	= 1,
	IBS_STOPPING	= 2,
	IBS_STOPPED	= 3,

	IBS_MAX_STATES,
};

struct cpu_perf_ibs {
	struct perf_event	*event;
	unsigned long		state[BITS_TO_LONGS(IBS_MAX_STATES)];
};

struct perf_ibs {
	struct pmu			pmu;
	unsigned int			msr;
	u64				config_mask;
	u64				cnt_mask;
	u64				enable_mask;
	u64				valid_mask;
	u64				max_period;
	unsigned long			offset_mask[1];
	int				offset_max;
	struct cpu_perf_ibs __percpu	*pcpu;

	struct attribute		**format_attrs;
	struct attribute_group		format_group;
	const struct attribute_group	*attr_groups[2];

	u64				(*get_count)(u64 config);
};

struct perf_ibs_data {
	u32		size;
	union {
		u32	data[0];	/* data buffer starts here */
		u32	caps;
	};
	u64		regs[MSR_AMD64_IBS_REG_COUNT_MAX];
};

static int
perf_event_set_period(struct hw_perf_event *hwc, u64 min, u64 max, u64 *hw_period)
{
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int overflow = 0;

	/*
	 * If we are way outside a reasonable range then just skip forward:
	 */
	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		overflow = 1;
	}

	if (unlikely(left < (s64)min)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		overflow = 1;
	}

	/*
	 * If the hw period that triggers the sw overflow is too short
	 * we might hit the irq handler. This biases the results.
	 * Thus we shorten the next-to-last period and set the last
	 * period to the max period.
	 */
	if (left > max) {
		left -= max;
		if (left > max)
			left = max;
		else if (left < min)
			left = min;
	}

	*hw_period = (u64)left;

	return overflow;
}

static  int
perf_event_try_update(struct perf_event *event, u64 new_raw_count, int width)
{
	struct hw_perf_event *hwc = &event->hw;
	int shift = 64 - width;
	u64 prev_raw_count;
	u64 delta;

	/*
	 * Careful: an NMI might modify the previous event value.
	 *
	 * Our tactic to handle this is to first atomically read and
	 * exchange a new raw count - then add that new-prev delta
	 * count to the generic event atomically:
	 */
	prev_raw_count = local64_read(&hwc->prev_count);
	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					new_raw_count) != prev_raw_count)
		return 0;

	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (event-)time and add that to the generic event.
	 *
	 * Careful, not all hw sign-extends above the physical width
	 * of the count.
	 */
	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);

	return 1;
}

static struct perf_ibs perf_ibs_fetch;
static struct perf_ibs perf_ibs_op;

static struct perf_ibs *get_ibs_pmu(int type)
{
	if (perf_ibs_fetch.pmu.type == type)
		return &perf_ibs_fetch;
	if (perf_ibs_op.pmu.type == type)
		return &perf_ibs_op;
	return NULL;
}

/*
 * Use IBS for precise event sampling:
 *
 *  perf record -a -e cpu-cycles:p ...    # use ibs op counting cycle count
 *  perf record -a -e r076:p ...          # same as -e cpu-cycles:p
 *  perf record -a -e r0C1:p ...          # use ibs op counting micro-ops
 *
 * IbsOpCntCtl (bit 19) of IBS Execution Control Register (IbsOpCtl,
 * MSRC001_1033) is used to select either cycle or micro-ops counting
 * mode.
 *
 * The rip of IBS samples has skid 0. Thus, IBS supports precise
 * levels 1 and 2 and the PERF_EFLAGS_EXACT is set. In rare cases the
 * rip is invalid when IBS was not able to record the rip correctly.
 * We clear PERF_EFLAGS_EXACT and take the rip from pt_regs then.
 *
 */
static int perf_ibs_precise_event(struct perf_event *event, u64 *config)
{
	switch (event->attr.precise_ip) {
	case 0:
		return -ENOENT;
	case 1:
	case 2:
		break;
	default:
		return -EOPNOTSUPP;
	}

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		switch (event->attr.config) {
		case PERF_COUNT_HW_CPU_CYCLES:
			*config = 0;
			return 0;
		}
		break;
	case PERF_TYPE_RAW:
		switch (event->attr.config) {
		case 0x0076:
			*config = 0;
			return 0;
		case 0x00C1:
			*config = IBS_OP_CNT_CTL;
			return 0;
		}
		break;
	default:
		return -ENOENT;
	}

	return -EOPNOTSUPP;
}

static int perf_ibs_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_ibs *perf_ibs;
	u64 max_cnt, config;
	int ret;

	perf_ibs = get_ibs_pmu(event->attr.type);
	if (perf_ibs) {
		config = event->attr.config;
	} else {
		perf_ibs = &perf_ibs_op;
		ret = perf_ibs_precise_event(event, &config);
		if (ret)
			return ret;
	}

	if (event->pmu != &perf_ibs->pmu)
		return -ENOENT;

	if (config & ~perf_ibs->config_mask)
		return -EINVAL;

	if (hwc->sample_period) {
		if (config & perf_ibs->cnt_mask)
			/* raw max_cnt may not be set */
			return -EINVAL;
		if (!event->attr.sample_freq && hwc->sample_period & 0x0f)
			/*
			 * lower 4 bits can not be set in ibs max cnt,
			 * but allowing it in case we adjust the
			 * sample period to set a frequency.
			 */
			return -EINVAL;
		hwc->sample_period &= ~0x0FULL;
		if (!hwc->sample_period)
			hwc->sample_period = 0x10;
	} else {
		max_cnt = config & perf_ibs->cnt_mask;
		config &= ~perf_ibs->cnt_mask;
		event->attr.sample_period = max_cnt << 4;
		hwc->sample_period = event->attr.sample_period;
	}

	if (!hwc->sample_period)
		return -EINVAL;

	/*
	 * If we modify hwc->sample_period, we also need to update
	 * hwc->last_period and hwc->period_left.
	 */
	hwc->last_period = hwc->sample_period;
	local64_set(&hwc->period_left, hwc->sample_period);

	hwc->config_base = perf_ibs->msr;
	hwc->config = config;

	return 0;
}

static int perf_ibs_set_period(struct perf_ibs *perf_ibs,
			       struct hw_perf_event *hwc, u64 *period)
{
	int overflow;

	/* ignore lower 4 bits in min count: */
	overflow = perf_event_set_period(hwc, 1<<4, perf_ibs->max_period, period);
	local64_set(&hwc->prev_count, 0);

	return overflow;
}

static u64 get_ibs_fetch_count(u64 config)
{
	return (config & IBS_FETCH_CNT) >> 12;
}

static u64 get_ibs_op_count(u64 config)
{
	u64 count = 0;

	if (config & IBS_OP_VAL)
		count += (config & IBS_OP_MAX_CNT) << 4; /* cnt rolled over */

	if (ibs_caps & IBS_CAPS_RDWROPCNT)
		count += (config & IBS_OP_CUR_CNT) >> 32;

	return count;
}

static void
perf_ibs_event_update(struct perf_ibs *perf_ibs, struct perf_event *event,
		      u64 *config)
{
	u64 count = perf_ibs->get_count(*config);

	/*
	 * Set width to 64 since we do not overflow on max width but
	 * instead on max count. In perf_ibs_set_period() we clear
	 * prev count manually on overflow.
	 */
	while (!perf_event_try_update(event, count, 64)) {
		rdmsrl(event->hw.config_base, *config);
		count = perf_ibs->get_count(*config);
	}
}

static inline void perf_ibs_enable_event(struct perf_ibs *perf_ibs,
					 struct hw_perf_event *hwc, u64 config)
{
	wrmsrl(hwc->config_base, hwc->config | config | perf_ibs->enable_mask);
}

/*
 * Erratum #420 Instruction-Based Sampling Engine May Generate
 * Interrupt that Cannot Be Cleared:
 *
 * Must clear counter mask first, then clear the enable bit. See
 * Revision Guide for AMD Family 10h Processors, Publication #41322.
 */
static inline void perf_ibs_disable_event(struct perf_ibs *perf_ibs,
					  struct hw_perf_event *hwc, u64 config)
{
	config &= ~perf_ibs->cnt_mask;
	if (boot_cpu_data.x86 == 0x10)
		wrmsrl(hwc->config_base, config);
	config &= ~perf_ibs->enable_mask;
	wrmsrl(hwc->config_base, config);
}

/*
 * We cannot restore the ibs pmu state, so we always needs to update
 * the event while stopping it and then reset the state when starting
 * again. Thus, ignoring PERF_EF_RELOAD and PERF_EF_UPDATE flags in
 * perf_ibs_start()/perf_ibs_stop() and instead always do it.
 */
static void perf_ibs_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_ibs *perf_ibs = container_of(event->pmu, struct perf_ibs, pmu);
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);
	u64 period;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	perf_ibs_set_period(perf_ibs, hwc, &period);
	/*
	 * Set STARTED before enabling the hardware, such that a subsequent NMI
	 * must observe it.
	 */
	set_bit(IBS_STARTED,    pcpu->state);
	clear_bit(IBS_STOPPING, pcpu->state);
	perf_ibs_enable_event(perf_ibs, hwc, period >> 4);

	perf_event_update_userpage(event);
}

static void perf_ibs_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_ibs *perf_ibs = container_of(event->pmu, struct perf_ibs, pmu);
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);
	u64 config;
	int stopping;

	if (test_and_set_bit(IBS_STOPPING, pcpu->state))
		return;

	stopping = test_bit(IBS_STARTED, pcpu->state);

	if (!stopping && (hwc->state & PERF_HES_UPTODATE))
		return;

	rdmsrl(hwc->config_base, config);

	if (stopping) {
		/*
		 * Set STOPPED before disabling the hardware, such that it
		 * must be visible to NMIs the moment we clear the EN bit,
		 * at which point we can generate an !VALID sample which
		 * we need to consume.
		 */
		set_bit(IBS_STOPPED, pcpu->state);
		perf_ibs_disable_event(perf_ibs, hwc, config);
		/*
		 * Clear STARTED after disabling the hardware; if it were
		 * cleared before an NMI hitting after the clear but before
		 * clearing the EN bit might think it a spurious NMI and not
		 * handle it.
		 *
		 * Clearing it after, however, creates the problem of the NMI
		 * handler seeing STARTED but not having a valid sample.
		 */
		clear_bit(IBS_STARTED, pcpu->state);
		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;
	}

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	/*
	 * Clear valid bit to not count rollovers on update, rollovers
	 * are only updated in the irq handler.
	 */
	config &= ~perf_ibs->valid_mask;

	perf_ibs_event_update(perf_ibs, event, &config);
	hwc->state |= PERF_HES_UPTODATE;
}

static int perf_ibs_add(struct perf_event *event, int flags)
{
	struct perf_ibs *perf_ibs = container_of(event->pmu, struct perf_ibs, pmu);
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);

	if (test_and_set_bit(IBS_ENABLED, pcpu->state))
		return -ENOSPC;

	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	pcpu->event = event;

	if (flags & PERF_EF_START)
		perf_ibs_start(event, PERF_EF_RELOAD);

	return 0;
}

static void perf_ibs_del(struct perf_event *event, int flags)
{
	struct perf_ibs *perf_ibs = container_of(event->pmu, struct perf_ibs, pmu);
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);

	if (!test_and_clear_bit(IBS_ENABLED, pcpu->state))
		return;

	perf_ibs_stop(event, PERF_EF_UPDATE);

	pcpu->event = NULL;

	perf_event_update_userpage(event);
}

static void perf_ibs_read(struct perf_event *event) { }

PMU_FORMAT_ATTR(rand_en,	"config:57");
PMU_FORMAT_ATTR(cnt_ctl,	"config:19");

static struct attribute *ibs_fetch_format_attrs[] = {
	&format_attr_rand_en.attr,
	NULL,
};

static struct attribute *ibs_op_format_attrs[] = {
	NULL,	/* &format_attr_cnt_ctl.attr if IBS_CAPS_OPCNT */
	NULL,
};

static struct perf_ibs perf_ibs_fetch = {
	.pmu = {
		.task_ctx_nr	= perf_invalid_context,

		.event_init	= perf_ibs_init,
		.add		= perf_ibs_add,
		.del		= perf_ibs_del,
		.start		= perf_ibs_start,
		.stop		= perf_ibs_stop,
		.read		= perf_ibs_read,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	},
	.msr			= MSR_AMD64_IBSFETCHCTL,
	.config_mask		= IBS_FETCH_CONFIG_MASK,
	.cnt_mask		= IBS_FETCH_MAX_CNT,
	.enable_mask		= IBS_FETCH_ENABLE,
	.valid_mask		= IBS_FETCH_VAL,
	.max_period		= IBS_FETCH_MAX_CNT << 4,
	.offset_mask		= { MSR_AMD64_IBSFETCH_REG_MASK },
	.offset_max		= MSR_AMD64_IBSFETCH_REG_COUNT,
	.format_attrs		= ibs_fetch_format_attrs,

	.get_count		= get_ibs_fetch_count,
};

static struct perf_ibs perf_ibs_op = {
	.pmu = {
		.task_ctx_nr	= perf_invalid_context,

		.event_init	= perf_ibs_init,
		.add		= perf_ibs_add,
		.del		= perf_ibs_del,
		.start		= perf_ibs_start,
		.stop		= perf_ibs_stop,
		.read		= perf_ibs_read,
	},
	.msr			= MSR_AMD64_IBSOPCTL,
	.config_mask		= IBS_OP_CONFIG_MASK,
	.cnt_mask		= IBS_OP_MAX_CNT | IBS_OP_CUR_CNT |
				  IBS_OP_CUR_CNT_RAND,
	.enable_mask		= IBS_OP_ENABLE,
	.valid_mask		= IBS_OP_VAL,
	.max_period		= IBS_OP_MAX_CNT << 4,
	.offset_mask		= { MSR_AMD64_IBSOP_REG_MASK },
	.offset_max		= MSR_AMD64_IBSOP_REG_COUNT,
	.format_attrs		= ibs_op_format_attrs,

	.get_count		= get_ibs_op_count,
};

static int perf_ibs_handle_irq(struct perf_ibs *perf_ibs, struct pt_regs *iregs)
{
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);
	struct perf_event *event = pcpu->event;
	struct hw_perf_event *hwc;
	struct perf_sample_data data;
	struct perf_raw_record raw;
	struct pt_regs regs;
	struct perf_ibs_data ibs_data;
	int offset, size, check_rip, offset_max, throttle = 0;
	unsigned int msr;
	u64 *buf, *config, period;

	if (!test_bit(IBS_STARTED, pcpu->state)) {
fail:
		/*
		 * Catch spurious interrupts after stopping IBS: After
		 * disabling IBS there could be still incoming NMIs
		 * with samples that even have the valid bit cleared.
		 * Mark all this NMIs as handled.
		 */
		if (test_and_clear_bit(IBS_STOPPED, pcpu->state))
			return 1;

		return 0;
	}

	if (WARN_ON_ONCE(!event))
		goto fail;

	hwc = &event->hw;
	msr = hwc->config_base;
	buf = ibs_data.regs;
	rdmsrl(msr, *buf);
	if (!(*buf++ & perf_ibs->valid_mask))
		goto fail;

	config = &ibs_data.regs[0];
	perf_ibs_event_update(perf_ibs, event, config);
	perf_sample_data_init(&data, 0, hwc->last_period);
	if (!perf_ibs_set_period(perf_ibs, hwc, &period))
		goto out;	/* no sw counter overflow */

	ibs_data.caps = ibs_caps;
	size = 1;
	offset = 1;
	check_rip = (perf_ibs == &perf_ibs_op && (ibs_caps & IBS_CAPS_RIPINVALIDCHK));
	if (event->attr.sample_type & PERF_SAMPLE_RAW)
		offset_max = perf_ibs->offset_max;
	else if (check_rip)
		offset_max = 3;
	else
		offset_max = 1;
	do {
		rdmsrl(msr + offset, *buf++);
		size++;
		offset = find_next_bit(perf_ibs->offset_mask,
				       perf_ibs->offset_max,
				       offset + 1);
	} while (offset < offset_max);
	if (event->attr.sample_type & PERF_SAMPLE_RAW) {
		/*
		 * Read IbsBrTarget and IbsOpData4 separately
		 * depending on their availability.
		 * Can't add to offset_max as they are staggered
		 */
		if (ibs_caps & IBS_CAPS_BRNTRGT) {
			rdmsrl(MSR_AMD64_IBSBRTARGET, *buf++);
			size++;
		}
		if (ibs_caps & IBS_CAPS_OPDATA4) {
			rdmsrl(MSR_AMD64_IBSOPDATA4, *buf++);
			size++;
		}
	}
	ibs_data.size = sizeof(u64) * size;

	regs = *iregs;
	if (check_rip && (ibs_data.regs[2] & IBS_RIP_INVALID)) {
		regs.flags &= ~PERF_EFLAGS_EXACT;
	} else {
		set_linear_ip(&regs, ibs_data.regs[1]);
		regs.flags |= PERF_EFLAGS_EXACT;
	}

	if (event->attr.sample_type & PERF_SAMPLE_RAW) {
		raw = (struct perf_raw_record){
			.frag = {
				.size = sizeof(u32) + ibs_data.size,
				.data = ibs_data.data,
			},
		};
		data.raw = &raw;
	}

	throttle = perf_event_overflow(event, &data, &regs);
out:
	if (throttle) {
		perf_ibs_stop(event, 0);
	} else {
		period >>= 4;

		if ((ibs_caps & IBS_CAPS_RDWROPCNT) &&
		    (*config & IBS_OP_CNT_CTL))
			period |= *config & IBS_OP_CUR_CNT_RAND;

		perf_ibs_enable_event(perf_ibs, hwc, period);
	}

	perf_event_update_userpage(event);

	return 1;
}

static int
perf_ibs_nmi_handler(unsigned int cmd, struct pt_regs *regs)
{
	u64 stamp = sched_clock();
	int handled = 0;

	handled += perf_ibs_handle_irq(&perf_ibs_fetch, regs);
	handled += perf_ibs_handle_irq(&perf_ibs_op, regs);

	if (handled)
		inc_irq_stat(apic_perf_irqs);

	perf_sample_event_took(sched_clock() - stamp);

	return handled;
}
NOKPROBE_SYMBOL(perf_ibs_nmi_handler);

static __init int perf_ibs_pmu_init(struct perf_ibs *perf_ibs, char *name)
{
	struct cpu_perf_ibs __percpu *pcpu;
	int ret;

	pcpu = alloc_percpu(struct cpu_perf_ibs);
	if (!pcpu)
		return -ENOMEM;

	perf_ibs->pcpu = pcpu;

	/* register attributes */
	if (perf_ibs->format_attrs[0]) {
		memset(&perf_ibs->format_group, 0, sizeof(perf_ibs->format_group));
		perf_ibs->format_group.name	= "format";
		perf_ibs->format_group.attrs	= perf_ibs->format_attrs;

		memset(&perf_ibs->attr_groups, 0, sizeof(perf_ibs->attr_groups));
		perf_ibs->attr_groups[0]	= &perf_ibs->format_group;
		perf_ibs->pmu.attr_groups	= perf_ibs->attr_groups;
	}

	ret = perf_pmu_register(&perf_ibs->pmu, name, -1);
	if (ret) {
		perf_ibs->pcpu = NULL;
		free_percpu(pcpu);
	}

	return ret;
}

static __init void perf_event_ibs_init(void)
{
	struct attribute **attr = ibs_op_format_attrs;

	perf_ibs_pmu_init(&perf_ibs_fetch, "ibs_fetch");

	if (ibs_caps & IBS_CAPS_OPCNT) {
		perf_ibs_op.config_mask |= IBS_OP_CNT_CTL;
		*attr++ = &format_attr_cnt_ctl.attr;
	}
	perf_ibs_pmu_init(&perf_ibs_op, "ibs_op");

	register_nmi_handler(NMI_LOCAL, perf_ibs_nmi_handler, 0, "perf_ibs");
	pr_info("perf: AMD IBS detected (0x%08x)\n", ibs_caps);
}

#else /* defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_AMD) */

static __init void perf_event_ibs_init(void) { }

#endif

/* IBS - apic initialization, for perf and oprofile */

static __init u32 __get_ibs_caps(void)
{
	u32 caps;
	unsigned int max_level;

	if (!boot_cpu_has(X86_FEATURE_IBS))
		return 0;

	/* check IBS cpuid feature flags */
	max_level = cpuid_eax(0x80000000);
	if (max_level < IBS_CPUID_FEATURES)
		return IBS_CAPS_DEFAULT;

	caps = cpuid_eax(IBS_CPUID_FEATURES);
	if (!(caps & IBS_CAPS_AVAIL))
		/* cpuid flags not valid */
		return IBS_CAPS_DEFAULT;

	return caps;
}

u32 get_ibs_caps(void)
{
	return ibs_caps;
}

EXPORT_SYMBOL(get_ibs_caps);

static inline int get_eilvt(int offset)
{
	return !setup_APIC_eilvt(offset, 0, APIC_EILVT_MSG_NMI, 1);
}

static inline int put_eilvt(int offset)
{
	return !setup_APIC_eilvt(offset, 0, 0, 1);
}

/*
 * Check and reserve APIC extended interrupt LVT offset for IBS if available.
 */
static inline int ibs_eilvt_valid(void)
{
	int offset;
	u64 val;
	int valid = 0;

	preempt_disable();

	rdmsrl(MSR_AMD64_IBSCTL, val);
	offset = val & IBSCTL_LVT_OFFSET_MASK;

	if (!(val & IBSCTL_LVT_OFFSET_VALID)) {
		pr_err(FW_BUG "cpu %d, invalid IBS interrupt offset %d (MSR%08X=0x%016llx)\n",
		       smp_processor_id(), offset, MSR_AMD64_IBSCTL, val);
		goto out;
	}

	if (!get_eilvt(offset)) {
		pr_err(FW_BUG "cpu %d, IBS interrupt offset %d not available (MSR%08X=0x%016llx)\n",
		       smp_processor_id(), offset, MSR_AMD64_IBSCTL, val);
		goto out;
	}

	valid = 1;
out:
	preempt_enable();

	return valid;
}

static int setup_ibs_ctl(int ibs_eilvt_off)
{
	struct pci_dev *cpu_cfg;
	int nodes;
	u32 value = 0;

	nodes = 0;
	cpu_cfg = NULL;
	do {
		cpu_cfg = pci_get_device(PCI_VENDOR_ID_AMD,
					 PCI_DEVICE_ID_AMD_10H_NB_MISC,
					 cpu_cfg);
		if (!cpu_cfg)
			break;
		++nodes;
		pci_write_config_dword(cpu_cfg, IBSCTL, ibs_eilvt_off
				       | IBSCTL_LVT_OFFSET_VALID);
		pci_read_config_dword(cpu_cfg, IBSCTL, &value);
		if (value != (ibs_eilvt_off | IBSCTL_LVT_OFFSET_VALID)) {
			pci_dev_put(cpu_cfg);
			pr_debug("Failed to setup IBS LVT offset, IBSCTL = 0x%08x\n",
				 value);
			return -EINVAL;
		}
	} while (1);

	if (!nodes) {
		pr_debug("No CPU node configured for IBS\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * This runs only on the current cpu. We try to find an LVT offset and
 * setup the local APIC. For this we must disable preemption. On
 * success we initialize all nodes with this offset. This updates then
 * the offset in the IBS_CTL per-node msr. The per-core APIC setup of
 * the IBS interrupt vector is handled by perf_ibs_cpu_notifier that
 * is using the new offset.
 */
static void force_ibs_eilvt_setup(void)
{
	int offset;
	int ret;

	preempt_disable();
	/* find the next free available EILVT entry, skip offset 0 */
	for (offset = 1; offset < APIC_EILVT_NR_MAX; offset++) {
		if (get_eilvt(offset))
			break;
	}
	preempt_enable();

	if (offset == APIC_EILVT_NR_MAX) {
		pr_debug("No EILVT entry available\n");
		return;
	}

	ret = setup_ibs_ctl(offset);
	if (ret)
		goto out;

	if (!ibs_eilvt_valid())
		goto out;

	pr_info("LVT offset %d assigned\n", offset);

	return;
out:
	preempt_disable();
	put_eilvt(offset);
	preempt_enable();
	return;
}

static void ibs_eilvt_setup(void)
{
	/*
	 * Force LVT offset assignment for family 10h: The offsets are
	 * not assigned by the BIOS for this family, so the OS is
	 * responsible for doing it. If the OS assignment fails, fall
	 * back to BIOS settings and try to setup this.
	 */
	if (boot_cpu_data.x86 == 0x10)
		force_ibs_eilvt_setup();
}

static inline int get_ibs_lvt_offset(void)
{
	u64 val;

	rdmsrl(MSR_AMD64_IBSCTL, val);
	if (!(val & IBSCTL_LVT_OFFSET_VALID))
		return -EINVAL;

	return val & IBSCTL_LVT_OFFSET_MASK;
}

static void setup_APIC_ibs(void)
{
	int offset;

	offset = get_ibs_lvt_offset();
	if (offset < 0)
		goto failed;

	if (!setup_APIC_eilvt(offset, 0, APIC_EILVT_MSG_NMI, 0))
		return;
failed:
	pr_warn("perf: IBS APIC setup failed on cpu #%d\n",
		smp_processor_id());
}

static void clear_APIC_ibs(void)
{
	int offset;

	offset = get_ibs_lvt_offset();
	if (offset >= 0)
		setup_APIC_eilvt(offset, 0, APIC_EILVT_MSG_FIX, 1);
}

static int x86_pmu_amd_ibs_starting_cpu(unsigned int cpu)
{
	setup_APIC_ibs();
	return 0;
}

#ifdef CONFIG_PM

static int perf_ibs_suspend(void)
{
	clear_APIC_ibs();
	return 0;
}

static void perf_ibs_resume(void)
{
	ibs_eilvt_setup();
	setup_APIC_ibs();
}

static struct syscore_ops perf_ibs_syscore_ops = {
	.resume		= perf_ibs_resume,
	.suspend	= perf_ibs_suspend,
};

static void perf_ibs_pm_init(void)
{
	register_syscore_ops(&perf_ibs_syscore_ops);
}

#else

static inline void perf_ibs_pm_init(void) { }

#endif

static int x86_pmu_amd_ibs_dying_cpu(unsigned int cpu)
{
	clear_APIC_ibs();
	return 0;
}

static __init int amd_ibs_init(void)
{
	u32 caps;

	caps = __get_ibs_caps();
	if (!caps)
		return -ENODEV;	/* ibs not supported by the cpu */

	ibs_eilvt_setup();

	if (!ibs_eilvt_valid())
		return -EINVAL;

	perf_ibs_pm_init();

	ibs_caps = caps;
	/* make ibs_caps visible to other cpus: */
	smp_mb();
	/*
	 * x86_pmu_amd_ibs_starting_cpu will be called from core on
	 * all online cpus.
	 */
	cpuhp_setup_state(CPUHP_AP_PERF_X86_AMD_IBS_STARTING,
			  "perf/x86/amd/ibs:starting",
			  x86_pmu_amd_ibs_starting_cpu,
			  x86_pmu_amd_ibs_dying_cpu);

	perf_event_ibs_init();

	return 0;
}

/* Since we need the pci subsystem to init ibs we can't do this earlier: */
device_initcall(amd_ibs_init);
