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
#include <asm/amd-ibs.h>

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
	unsigned int			fetch_count_reset_broken : 1;
	unsigned int			fetch_ignore_if_zero_rip : 1;
	struct cpu_perf_ibs __percpu	*pcpu;

	u64				(*get_count)(u64 config);
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
 * core pmu config -> IBS config
 *
 *  perf record -a -e cpu-cycles:p ...    # use ibs op counting cycle count
 *  perf record -a -e r076:p ...          # same as -e cpu-cycles:p
 *  perf record -a -e r0C1:p ...          # use ibs op counting micro-ops
 *
 * IbsOpCntCtl (bit 19) of IBS Execution Control Register (IbsOpCtl,
 * MSRC001_1033) is used to select either cycle or micro-ops counting
 * mode.
 */
static int core_pmu_ibs_config(struct perf_event *event, u64 *config)
{
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

/*
 * The rip of IBS samples has skid 0. Thus, IBS supports precise
 * levels 1 and 2 and the PERF_EFLAGS_EXACT is set. In rare cases the
 * rip is invalid when IBS was not able to record the rip correctly.
 * We clear PERF_EFLAGS_EXACT and take the rip from pt_regs then.
 */
int forward_event_to_ibs(struct perf_event *event)
{
	u64 config = 0;

	if (!event->attr.precise_ip || event->attr.precise_ip > 2)
		return -EOPNOTSUPP;

	if (!core_pmu_ibs_config(event, &config)) {
		event->attr.type = perf_ibs_op.pmu.type;
		event->attr.config = config;
	}
	return -ENOENT;
}

static int perf_ibs_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_ibs *perf_ibs;
	u64 max_cnt, config;

	perf_ibs = get_ibs_pmu(event->attr.type);
	if (!perf_ibs)
		return -ENOENT;

	config = event->attr.config;

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
	union ibs_fetch_ctl fetch_ctl = (union ibs_fetch_ctl)config;

	return fetch_ctl.fetch_cnt << 4;
}

static u64 get_ibs_op_count(u64 config)
{
	union ibs_op_ctl op_ctl = (union ibs_op_ctl)config;
	u64 count = 0;

	/*
	 * If the internal 27-bit counter rolled over, the count is MaxCnt
	 * and the lower 7 bits of CurCnt are randomized.
	 * Otherwise CurCnt has the full 27-bit current counter value.
	 */
	if (op_ctl.op_val) {
		count = op_ctl.opmaxcnt << 4;
		if (ibs_caps & IBS_CAPS_OPCNTEXT)
			count += op_ctl.opmaxcnt_ext << 20;
	} else if (ibs_caps & IBS_CAPS_RDWROPCNT) {
		count = op_ctl.opcurcnt;
	}

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
	u64 tmp = hwc->config | config;

	if (perf_ibs->fetch_count_reset_broken)
		wrmsrl(hwc->config_base, tmp & ~perf_ibs->enable_mask);

	wrmsrl(hwc->config_base, tmp | perf_ibs->enable_mask);
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
	u64 period, config = 0;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	perf_ibs_set_period(perf_ibs, hwc, &period);
	if (perf_ibs == &perf_ibs_op && (ibs_caps & IBS_CAPS_OPCNTEXT)) {
		config |= period & IBS_OP_MAX_CNT_EXT_MASK;
		period &= ~IBS_OP_MAX_CNT_EXT_MASK;
	}
	config |= period >> 4;

	/*
	 * Set STARTED before enabling the hardware, such that a subsequent NMI
	 * must observe it.
	 */
	set_bit(IBS_STARTED,    pcpu->state);
	clear_bit(IBS_STOPPING, pcpu->state);
	perf_ibs_enable_event(perf_ibs, hwc, config);

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

/*
 * We need to initialize with empty group if all attributes in the
 * group are dynamic.
 */
static struct attribute *attrs_empty[] = {
	NULL,
};

static struct attribute_group empty_format_group = {
	.name = "format",
	.attrs = attrs_empty,
};

static struct attribute_group empty_caps_group = {
	.name = "caps",
	.attrs = attrs_empty,
};

static const struct attribute_group *empty_attr_groups[] = {
	&empty_format_group,
	&empty_caps_group,
	NULL,
};

PMU_FORMAT_ATTR(rand_en,	"config:57");
PMU_FORMAT_ATTR(cnt_ctl,	"config:19");
PMU_EVENT_ATTR_STRING(l3missonly, fetch_l3missonly, "config:59");
PMU_EVENT_ATTR_STRING(l3missonly, op_l3missonly, "config:16");
PMU_EVENT_ATTR_STRING(zen4_ibs_extensions, zen4_ibs_extensions, "1");

static umode_t
zen4_ibs_extensions_is_visible(struct kobject *kobj, struct attribute *attr, int i)
{
	return ibs_caps & IBS_CAPS_ZEN4 ? attr->mode : 0;
}

static struct attribute *rand_en_attrs[] = {
	&format_attr_rand_en.attr,
	NULL,
};

static struct attribute *fetch_l3missonly_attrs[] = {
	&fetch_l3missonly.attr.attr,
	NULL,
};

static struct attribute *zen4_ibs_extensions_attrs[] = {
	&zen4_ibs_extensions.attr.attr,
	NULL,
};

static struct attribute_group group_rand_en = {
	.name = "format",
	.attrs = rand_en_attrs,
};

static struct attribute_group group_fetch_l3missonly = {
	.name = "format",
	.attrs = fetch_l3missonly_attrs,
	.is_visible = zen4_ibs_extensions_is_visible,
};

static struct attribute_group group_zen4_ibs_extensions = {
	.name = "caps",
	.attrs = zen4_ibs_extensions_attrs,
	.is_visible = zen4_ibs_extensions_is_visible,
};

static const struct attribute_group *fetch_attr_groups[] = {
	&group_rand_en,
	&empty_caps_group,
	NULL,
};

static const struct attribute_group *fetch_attr_update[] = {
	&group_fetch_l3missonly,
	&group_zen4_ibs_extensions,
	NULL,
};

static umode_t
cnt_ctl_is_visible(struct kobject *kobj, struct attribute *attr, int i)
{
	return ibs_caps & IBS_CAPS_OPCNT ? attr->mode : 0;
}

static struct attribute *cnt_ctl_attrs[] = {
	&format_attr_cnt_ctl.attr,
	NULL,
};

static struct attribute *op_l3missonly_attrs[] = {
	&op_l3missonly.attr.attr,
	NULL,
};

static struct attribute_group group_cnt_ctl = {
	.name = "format",
	.attrs = cnt_ctl_attrs,
	.is_visible = cnt_ctl_is_visible,
};

static struct attribute_group group_op_l3missonly = {
	.name = "format",
	.attrs = op_l3missonly_attrs,
	.is_visible = zen4_ibs_extensions_is_visible,
};

static const struct attribute_group *op_attr_update[] = {
	&group_cnt_ctl,
	&group_op_l3missonly,
	&group_zen4_ibs_extensions,
	NULL,
};

static struct perf_ibs perf_ibs_fetch = {
	.pmu = {
		.task_ctx_nr	= perf_hw_context,

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

	.get_count		= get_ibs_fetch_count,
};

static struct perf_ibs perf_ibs_op = {
	.pmu = {
		.task_ctx_nr	= perf_hw_context,

		.event_init	= perf_ibs_init,
		.add		= perf_ibs_add,
		.del		= perf_ibs_del,
		.start		= perf_ibs_start,
		.stop		= perf_ibs_stop,
		.read		= perf_ibs_read,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
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

	.get_count		= get_ibs_op_count,
};

static void perf_ibs_get_mem_op(union ibs_op_data3 *op_data3,
				struct perf_sample_data *data)
{
	union perf_mem_data_src *data_src = &data->data_src;

	data_src->mem_op = PERF_MEM_OP_NA;

	if (op_data3->ld_op)
		data_src->mem_op = PERF_MEM_OP_LOAD;
	else if (op_data3->st_op)
		data_src->mem_op = PERF_MEM_OP_STORE;
}

/*
 * Processors having CPUID_Fn8000001B_EAX[11] aka IBS_CAPS_ZEN4 has
 * more fine granular DataSrc encodings. Others have coarse.
 */
static u8 perf_ibs_data_src(union ibs_op_data2 *op_data2)
{
	if (ibs_caps & IBS_CAPS_ZEN4)
		return (op_data2->data_src_hi << 3) | op_data2->data_src_lo;

	return op_data2->data_src_lo;
}

static void perf_ibs_get_mem_lvl(union ibs_op_data2 *op_data2,
				 union ibs_op_data3 *op_data3,
				 struct perf_sample_data *data)
{
	union perf_mem_data_src *data_src = &data->data_src;
	u8 ibs_data_src = perf_ibs_data_src(op_data2);

	data_src->mem_lvl = 0;

	/*
	 * DcMiss, L2Miss, DataSrc, DcMissLat etc. are all invalid for Uncached
	 * memory accesses. So, check DcUcMemAcc bit early.
	 */
	if (op_data3->dc_uc_mem_acc && ibs_data_src != IBS_DATA_SRC_EXT_IO) {
		data_src->mem_lvl = PERF_MEM_LVL_UNC | PERF_MEM_LVL_HIT;
		return;
	}

	/* L1 Hit */
	if (op_data3->dc_miss == 0) {
		data_src->mem_lvl = PERF_MEM_LVL_L1 | PERF_MEM_LVL_HIT;
		return;
	}

	/* L2 Hit */
	if (op_data3->l2_miss == 0) {
		/* Erratum #1293 */
		if (boot_cpu_data.x86 != 0x19 || boot_cpu_data.x86_model > 0xF ||
		    !(op_data3->sw_pf || op_data3->dc_miss_no_mab_alloc)) {
			data_src->mem_lvl = PERF_MEM_LVL_L2 | PERF_MEM_LVL_HIT;
			return;
		}
	}

	/*
	 * OP_DATA2 is valid only for load ops. Skip all checks which
	 * uses OP_DATA2[DataSrc].
	 */
	if (data_src->mem_op != PERF_MEM_OP_LOAD)
		goto check_mab;

	/* L3 Hit */
	if (ibs_caps & IBS_CAPS_ZEN4) {
		if (ibs_data_src == IBS_DATA_SRC_EXT_LOC_CACHE) {
			data_src->mem_lvl = PERF_MEM_LVL_L3 | PERF_MEM_LVL_HIT;
			return;
		}
	} else {
		if (ibs_data_src == IBS_DATA_SRC_LOC_CACHE) {
			data_src->mem_lvl = PERF_MEM_LVL_L3 | PERF_MEM_LVL_REM_CCE1 |
					    PERF_MEM_LVL_HIT;
			return;
		}
	}

	/* A peer cache in a near CCX */
	if (ibs_caps & IBS_CAPS_ZEN4 &&
	    ibs_data_src == IBS_DATA_SRC_EXT_NEAR_CCX_CACHE) {
		data_src->mem_lvl = PERF_MEM_LVL_REM_CCE1 | PERF_MEM_LVL_HIT;
		return;
	}

	/* A peer cache in a far CCX */
	if (ibs_caps & IBS_CAPS_ZEN4) {
		if (ibs_data_src == IBS_DATA_SRC_EXT_FAR_CCX_CACHE) {
			data_src->mem_lvl = PERF_MEM_LVL_REM_CCE2 | PERF_MEM_LVL_HIT;
			return;
		}
	} else {
		if (ibs_data_src == IBS_DATA_SRC_REM_CACHE) {
			data_src->mem_lvl = PERF_MEM_LVL_REM_CCE2 | PERF_MEM_LVL_HIT;
			return;
		}
	}

	/* DRAM */
	if (ibs_data_src == IBS_DATA_SRC_EXT_DRAM) {
		if (op_data2->rmt_node == 0)
			data_src->mem_lvl = PERF_MEM_LVL_LOC_RAM | PERF_MEM_LVL_HIT;
		else
			data_src->mem_lvl = PERF_MEM_LVL_REM_RAM1 | PERF_MEM_LVL_HIT;
		return;
	}

	/* PMEM */
	if (ibs_caps & IBS_CAPS_ZEN4 && ibs_data_src == IBS_DATA_SRC_EXT_PMEM) {
		data_src->mem_lvl_num = PERF_MEM_LVLNUM_PMEM;
		if (op_data2->rmt_node) {
			data_src->mem_remote = PERF_MEM_REMOTE_REMOTE;
			/* IBS doesn't provide Remote socket detail */
			data_src->mem_hops = PERF_MEM_HOPS_1;
		}
		return;
	}

	/* Extension Memory */
	if (ibs_caps & IBS_CAPS_ZEN4 &&
	    ibs_data_src == IBS_DATA_SRC_EXT_EXT_MEM) {
		data_src->mem_lvl_num = PERF_MEM_LVLNUM_CXL;
		if (op_data2->rmt_node) {
			data_src->mem_remote = PERF_MEM_REMOTE_REMOTE;
			/* IBS doesn't provide Remote socket detail */
			data_src->mem_hops = PERF_MEM_HOPS_1;
		}
		return;
	}

	/* IO */
	if (ibs_data_src == IBS_DATA_SRC_EXT_IO) {
		data_src->mem_lvl = PERF_MEM_LVL_IO;
		data_src->mem_lvl_num = PERF_MEM_LVLNUM_IO;
		if (op_data2->rmt_node) {
			data_src->mem_remote = PERF_MEM_REMOTE_REMOTE;
			/* IBS doesn't provide Remote socket detail */
			data_src->mem_hops = PERF_MEM_HOPS_1;
		}
		return;
	}

check_mab:
	/*
	 * MAB (Miss Address Buffer) Hit. MAB keeps track of outstanding
	 * DC misses. However, such data may come from any level in mem
	 * hierarchy. IBS provides detail about both MAB as well as actual
	 * DataSrc simultaneously. Prioritize DataSrc over MAB, i.e. set
	 * MAB only when IBS fails to provide DataSrc.
	 */
	if (op_data3->dc_miss_no_mab_alloc) {
		data_src->mem_lvl = PERF_MEM_LVL_LFB | PERF_MEM_LVL_HIT;
		return;
	}

	data_src->mem_lvl = PERF_MEM_LVL_NA;
}

static bool perf_ibs_cache_hit_st_valid(void)
{
	/* 0: Uninitialized, 1: Valid, -1: Invalid */
	static int cache_hit_st_valid;

	if (unlikely(!cache_hit_st_valid)) {
		if (boot_cpu_data.x86 == 0x19 &&
		    (boot_cpu_data.x86_model <= 0xF ||
		    (boot_cpu_data.x86_model >= 0x20 &&
		     boot_cpu_data.x86_model <= 0x5F))) {
			cache_hit_st_valid = -1;
		} else {
			cache_hit_st_valid = 1;
		}
	}

	return cache_hit_st_valid == 1;
}

static void perf_ibs_get_mem_snoop(union ibs_op_data2 *op_data2,
				   struct perf_sample_data *data)
{
	union perf_mem_data_src *data_src = &data->data_src;
	u8 ibs_data_src;

	data_src->mem_snoop = PERF_MEM_SNOOP_NA;

	if (!perf_ibs_cache_hit_st_valid() ||
	    data_src->mem_op != PERF_MEM_OP_LOAD ||
	    data_src->mem_lvl & PERF_MEM_LVL_L1 ||
	    data_src->mem_lvl & PERF_MEM_LVL_L2 ||
	    op_data2->cache_hit_st)
		return;

	ibs_data_src = perf_ibs_data_src(op_data2);

	if (ibs_caps & IBS_CAPS_ZEN4) {
		if (ibs_data_src == IBS_DATA_SRC_EXT_LOC_CACHE ||
		    ibs_data_src == IBS_DATA_SRC_EXT_NEAR_CCX_CACHE ||
		    ibs_data_src == IBS_DATA_SRC_EXT_FAR_CCX_CACHE)
			data_src->mem_snoop = PERF_MEM_SNOOP_HITM;
	} else if (ibs_data_src == IBS_DATA_SRC_LOC_CACHE) {
		data_src->mem_snoop = PERF_MEM_SNOOP_HITM;
	}
}

static void perf_ibs_get_tlb_lvl(union ibs_op_data3 *op_data3,
				 struct perf_sample_data *data)
{
	union perf_mem_data_src *data_src = &data->data_src;

	data_src->mem_dtlb = PERF_MEM_TLB_NA;

	if (!op_data3->dc_lin_addr_valid)
		return;

	if (!op_data3->dc_l1tlb_miss) {
		data_src->mem_dtlb = PERF_MEM_TLB_L1 | PERF_MEM_TLB_HIT;
		return;
	}

	if (!op_data3->dc_l2tlb_miss) {
		data_src->mem_dtlb = PERF_MEM_TLB_L2 | PERF_MEM_TLB_HIT;
		return;
	}

	data_src->mem_dtlb = PERF_MEM_TLB_L2 | PERF_MEM_TLB_MISS;
}

static void perf_ibs_get_mem_lock(union ibs_op_data3 *op_data3,
				  struct perf_sample_data *data)
{
	union perf_mem_data_src *data_src = &data->data_src;

	data_src->mem_lock = PERF_MEM_LOCK_NA;

	if (op_data3->dc_locked_op)
		data_src->mem_lock = PERF_MEM_LOCK_LOCKED;
}

#define ibs_op_msr_idx(msr)	(msr - MSR_AMD64_IBSOPCTL)

static void perf_ibs_get_data_src(struct perf_ibs_data *ibs_data,
				  struct perf_sample_data *data,
				  union ibs_op_data2 *op_data2,
				  union ibs_op_data3 *op_data3)
{
	perf_ibs_get_mem_lvl(op_data2, op_data3, data);
	perf_ibs_get_mem_snoop(op_data2, data);
	perf_ibs_get_tlb_lvl(op_data3, data);
	perf_ibs_get_mem_lock(op_data3, data);
}

static __u64 perf_ibs_get_op_data2(struct perf_ibs_data *ibs_data,
				   union ibs_op_data3 *op_data3)
{
	__u64 val = ibs_data->regs[ibs_op_msr_idx(MSR_AMD64_IBSOPDATA2)];

	/* Erratum #1293 */
	if (boot_cpu_data.x86 == 0x19 && boot_cpu_data.x86_model <= 0xF &&
	    (op_data3->sw_pf || op_data3->dc_miss_no_mab_alloc)) {
		/*
		 * OP_DATA2 has only two fields on Zen3: DataSrc and RmtNode.
		 * DataSrc=0 is 'No valid status' and RmtNode is invalid when
		 * DataSrc=0.
		 */
		val = 0;
	}
	return val;
}

static void perf_ibs_parse_ld_st_data(__u64 sample_type,
				      struct perf_ibs_data *ibs_data,
				      struct perf_sample_data *data)
{
	union ibs_op_data3 op_data3;
	union ibs_op_data2 op_data2;
	union ibs_op_data op_data;

	data->data_src.val = PERF_MEM_NA;
	op_data3.val = ibs_data->regs[ibs_op_msr_idx(MSR_AMD64_IBSOPDATA3)];

	perf_ibs_get_mem_op(&op_data3, data);
	if (data->data_src.mem_op != PERF_MEM_OP_LOAD &&
	    data->data_src.mem_op != PERF_MEM_OP_STORE)
		return;

	op_data2.val = perf_ibs_get_op_data2(ibs_data, &op_data3);

	if (sample_type & PERF_SAMPLE_DATA_SRC) {
		perf_ibs_get_data_src(ibs_data, data, &op_data2, &op_data3);
		data->sample_flags |= PERF_SAMPLE_DATA_SRC;
	}

	if (sample_type & PERF_SAMPLE_WEIGHT_TYPE && op_data3.dc_miss &&
	    data->data_src.mem_op == PERF_MEM_OP_LOAD) {
		op_data.val = ibs_data->regs[ibs_op_msr_idx(MSR_AMD64_IBSOPDATA)];

		if (sample_type & PERF_SAMPLE_WEIGHT_STRUCT) {
			data->weight.var1_dw = op_data3.dc_miss_lat;
			data->weight.var2_w = op_data.tag_to_ret_ctr;
		} else if (sample_type & PERF_SAMPLE_WEIGHT) {
			data->weight.full = op_data3.dc_miss_lat;
		}
		data->sample_flags |= PERF_SAMPLE_WEIGHT_TYPE;
	}

	if (sample_type & PERF_SAMPLE_ADDR && op_data3.dc_lin_addr_valid) {
		data->addr = ibs_data->regs[ibs_op_msr_idx(MSR_AMD64_IBSDCLINAD)];
		data->sample_flags |= PERF_SAMPLE_ADDR;
	}

	if (sample_type & PERF_SAMPLE_PHYS_ADDR && op_data3.dc_phy_addr_valid) {
		data->phys_addr = ibs_data->regs[ibs_op_msr_idx(MSR_AMD64_IBSDCPHYSAD)];
		data->sample_flags |= PERF_SAMPLE_PHYS_ADDR;
	}
}

static int perf_ibs_get_offset_max(struct perf_ibs *perf_ibs, u64 sample_type,
				   int check_rip)
{
	if (sample_type & PERF_SAMPLE_RAW ||
	    (perf_ibs == &perf_ibs_op &&
	     (sample_type & PERF_SAMPLE_DATA_SRC ||
	      sample_type & PERF_SAMPLE_WEIGHT_TYPE ||
	      sample_type & PERF_SAMPLE_ADDR ||
	      sample_type & PERF_SAMPLE_PHYS_ADDR)))
		return perf_ibs->offset_max;
	else if (check_rip)
		return 3;
	return 1;
}

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
	u64 *buf, *config, period, new_config = 0;

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

	offset_max = perf_ibs_get_offset_max(perf_ibs, event->attr.sample_type, check_rip);

	do {
		rdmsrl(msr + offset, *buf++);
		size++;
		offset = find_next_bit(perf_ibs->offset_mask,
				       perf_ibs->offset_max,
				       offset + 1);
	} while (offset < offset_max);
	/*
	 * Read IbsBrTarget, IbsOpData4, and IbsExtdCtl separately
	 * depending on their availability.
	 * Can't add to offset_max as they are staggered
	 */
	if (event->attr.sample_type & PERF_SAMPLE_RAW) {
		if (perf_ibs == &perf_ibs_op) {
			if (ibs_caps & IBS_CAPS_BRNTRGT) {
				rdmsrl(MSR_AMD64_IBSBRTARGET, *buf++);
				size++;
			}
			if (ibs_caps & IBS_CAPS_OPDATA4) {
				rdmsrl(MSR_AMD64_IBSOPDATA4, *buf++);
				size++;
			}
		}
		if (perf_ibs == &perf_ibs_fetch && (ibs_caps & IBS_CAPS_FETCHCTLEXTD)) {
			rdmsrl(MSR_AMD64_ICIBSEXTDCTL, *buf++);
			size++;
		}
	}
	ibs_data.size = sizeof(u64) * size;

	regs = *iregs;
	if (check_rip && (ibs_data.regs[2] & IBS_RIP_INVALID)) {
		regs.flags &= ~PERF_EFLAGS_EXACT;
	} else {
		/* Workaround for erratum #1197 */
		if (perf_ibs->fetch_ignore_if_zero_rip && !(ibs_data.regs[1]))
			goto out;

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
		perf_sample_save_raw_data(&data, &raw);
	}

	if (perf_ibs == &perf_ibs_op)
		perf_ibs_parse_ld_st_data(event->attr.sample_type, &ibs_data, &data);

	/*
	 * rip recorded by IbsOpRip will not be consistent with rsp and rbp
	 * recorded as part of interrupt regs. Thus we need to use rip from
	 * interrupt regs while unwinding call stack.
	 */
	if (event->attr.sample_type & PERF_SAMPLE_CALLCHAIN)
		perf_sample_save_callchain(&data, event, iregs);

	throttle = perf_event_overflow(event, &data, &regs);
out:
	if (throttle) {
		perf_ibs_stop(event, 0);
	} else {
		if (perf_ibs == &perf_ibs_op) {
			if (ibs_caps & IBS_CAPS_OPCNTEXT) {
				new_config = period & IBS_OP_MAX_CNT_EXT_MASK;
				period &= ~IBS_OP_MAX_CNT_EXT_MASK;
			}
			if ((ibs_caps & IBS_CAPS_RDWROPCNT) && (*config & IBS_OP_CNT_CTL))
				new_config |= *config & IBS_OP_CUR_CNT_RAND;
		}
		new_config |= period >> 4;

		perf_ibs_enable_event(perf_ibs, hwc, new_config);
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

	ret = perf_pmu_register(&perf_ibs->pmu, name, -1);
	if (ret) {
		perf_ibs->pcpu = NULL;
		free_percpu(pcpu);
	}

	return ret;
}

static __init int perf_ibs_fetch_init(void)
{
	/*
	 * Some chips fail to reset the fetch count when it is written; instead
	 * they need a 0-1 transition of IbsFetchEn.
	 */
	if (boot_cpu_data.x86 >= 0x16 && boot_cpu_data.x86 <= 0x18)
		perf_ibs_fetch.fetch_count_reset_broken = 1;

	if (boot_cpu_data.x86 == 0x19 && boot_cpu_data.x86_model < 0x10)
		perf_ibs_fetch.fetch_ignore_if_zero_rip = 1;

	if (ibs_caps & IBS_CAPS_ZEN4)
		perf_ibs_fetch.config_mask |= IBS_FETCH_L3MISSONLY;

	perf_ibs_fetch.pmu.attr_groups = fetch_attr_groups;
	perf_ibs_fetch.pmu.attr_update = fetch_attr_update;

	return perf_ibs_pmu_init(&perf_ibs_fetch, "ibs_fetch");
}

static __init int perf_ibs_op_init(void)
{
	if (ibs_caps & IBS_CAPS_OPCNT)
		perf_ibs_op.config_mask |= IBS_OP_CNT_CTL;

	if (ibs_caps & IBS_CAPS_OPCNTEXT) {
		perf_ibs_op.max_period  |= IBS_OP_MAX_CNT_EXT_MASK;
		perf_ibs_op.config_mask	|= IBS_OP_MAX_CNT_EXT_MASK;
		perf_ibs_op.cnt_mask    |= IBS_OP_MAX_CNT_EXT_MASK;
	}

	if (ibs_caps & IBS_CAPS_ZEN4)
		perf_ibs_op.config_mask |= IBS_OP_L3MISSONLY;

	perf_ibs_op.pmu.attr_groups = empty_attr_groups;
	perf_ibs_op.pmu.attr_update = op_attr_update;

	return perf_ibs_pmu_init(&perf_ibs_op, "ibs_op");
}

static __init int perf_event_ibs_init(void)
{
	int ret;

	ret = perf_ibs_fetch_init();
	if (ret)
		return ret;

	ret = perf_ibs_op_init();
	if (ret)
		goto err_op;

	ret = register_nmi_handler(NMI_LOCAL, perf_ibs_nmi_handler, 0, "perf_ibs");
	if (ret)
		goto err_nmi;

	pr_info("perf: AMD IBS detected (0x%08x)\n", ibs_caps);
	return 0;

err_nmi:
	perf_pmu_unregister(&perf_ibs_op.pmu);
	free_percpu(perf_ibs_op.pcpu);
	perf_ibs_op.pcpu = NULL;
err_op:
	perf_pmu_unregister(&perf_ibs_fetch.pmu);
	free_percpu(perf_ibs_fetch.pcpu);
	perf_ibs_fetch.pcpu = NULL;

	return ret;
}

#else /* defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_AMD) */

static __init int perf_event_ibs_init(void)
{
	return 0;
}

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

	return perf_event_ibs_init();
}

/* Since we need the pci subsystem to init ibs we can't do this earlier: */
device_initcall(amd_ibs_init);
