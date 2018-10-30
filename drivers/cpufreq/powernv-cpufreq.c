/*
 * POWERNV cpufreq driver for the IBM POWER processors
 *
 * (C) Copyright IBM 2014
 *
 * Author: Vaidyanathan Srinivasan <svaidy at linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"powernv-cpufreq: " fmt

#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/smp.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/hashtable.h>
#include <trace/events/power.h>

#include <asm/cputhreads.h>
#include <asm/firmware.h>
#include <asm/reg.h>
#include <asm/smp.h> /* Required for cpu_sibling_mask() in UP configs */
#include <asm/opal.h>
#include <linux/timer.h>

#define POWERNV_MAX_PSTATES_ORDER  8
#define POWERNV_MAX_PSTATES	(1UL << (POWERNV_MAX_PSTATES_ORDER))
#define PMSR_PSAFE_ENABLE	(1UL << 30)
#define PMSR_SPR_EM_DISABLE	(1UL << 31)
#define MAX_PSTATE_SHIFT	32
#define LPSTATE_SHIFT		48
#define GPSTATE_SHIFT		56

#define MAX_RAMP_DOWN_TIME				5120
/*
 * On an idle system we want the global pstate to ramp-down from max value to
 * min over a span of ~5 secs. Also we want it to initially ramp-down slowly and
 * then ramp-down rapidly later on.
 *
 * This gives a percentage rampdown for time elapsed in milliseconds.
 * ramp_down_percentage = ((ms * ms) >> 18)
 *			~= 3.8 * (sec * sec)
 *
 * At 0 ms	ramp_down_percent = 0
 * At 5120 ms	ramp_down_percent = 100
 */
#define ramp_down_percent(time)		((time * time) >> 18)

/* Interval after which the timer is queued to bring down global pstate */
#define GPSTATE_TIMER_INTERVAL				2000

/**
 * struct global_pstate_info -	Per policy data structure to maintain history of
 *				global pstates
 * @highest_lpstate_idx:	The local pstate index from which we are
 *				ramping down
 * @elapsed_time:		Time in ms spent in ramping down from
 *				highest_lpstate_idx
 * @last_sampled_time:		Time from boot in ms when global pstates were
 *				last set
 * @last_lpstate_idx,		Last set value of local pstate and global
 * last_gpstate_idx		pstate in terms of cpufreq table index
 * @timer:			Is used for ramping down if cpu goes idle for
 *				a long time with global pstate held high
 * @gpstate_lock:		A spinlock to maintain synchronization between
 *				routines called by the timer handler and
 *				governer's target_index calls
 */
struct global_pstate_info {
	int highest_lpstate_idx;
	unsigned int elapsed_time;
	unsigned int last_sampled_time;
	int last_lpstate_idx;
	int last_gpstate_idx;
	spinlock_t gpstate_lock;
	struct timer_list timer;
	struct cpufreq_policy *policy;
};

static struct cpufreq_frequency_table powernv_freqs[POWERNV_MAX_PSTATES+1];

DEFINE_HASHTABLE(pstate_revmap, POWERNV_MAX_PSTATES_ORDER);
/**
 * struct pstate_idx_revmap_data: Entry in the hashmap pstate_revmap
 *				  indexed by a function of pstate id.
 *
 * @pstate_id: pstate id for this entry.
 *
 * @cpufreq_table_idx: Index into the powernv_freqs
 *		       cpufreq_frequency_table for frequency
 *		       corresponding to pstate_id.
 *
 * @hentry: hlist_node that hooks this entry into the pstate_revmap
 *	    hashtable
 */
struct pstate_idx_revmap_data {
	u8 pstate_id;
	unsigned int cpufreq_table_idx;
	struct hlist_node hentry;
};

static bool rebooting, throttled, occ_reset;

static const char * const throttle_reason[] = {
	"No throttling",
	"Power Cap",
	"Processor Over Temperature",
	"Power Supply Failure",
	"Over Current",
	"OCC Reset"
};

enum throttle_reason_type {
	NO_THROTTLE = 0,
	POWERCAP,
	CPU_OVERTEMP,
	POWER_SUPPLY_FAILURE,
	OVERCURRENT,
	OCC_RESET_THROTTLE,
	OCC_MAX_REASON
};

static struct chip {
	unsigned int id;
	bool throttled;
	bool restore;
	u8 throttle_reason;
	cpumask_t mask;
	struct work_struct throttle;
	int throttle_turbo;
	int throttle_sub_turbo;
	int reason[OCC_MAX_REASON];
} *chips;

static int nr_chips;
static DEFINE_PER_CPU(struct chip *, chip_info);

/*
 * Note:
 * The set of pstates consists of contiguous integers.
 * powernv_pstate_info stores the index of the frequency table for
 * max, min and nominal frequencies. It also stores number of
 * available frequencies.
 *
 * powernv_pstate_info.nominal indicates the index to the highest
 * non-turbo frequency.
 */
static struct powernv_pstate_info {
	unsigned int min;
	unsigned int max;
	unsigned int nominal;
	unsigned int nr_pstates;
	bool wof_enabled;
} powernv_pstate_info;

static inline u8 extract_pstate(u64 pmsr_val, unsigned int shift)
{
	return ((pmsr_val >> shift) & 0xFF);
}

#define extract_local_pstate(x) extract_pstate(x, LPSTATE_SHIFT)
#define extract_global_pstate(x) extract_pstate(x, GPSTATE_SHIFT)
#define extract_max_pstate(x)  extract_pstate(x, MAX_PSTATE_SHIFT)

/* Use following functions for conversions between pstate_id and index */

/**
 * idx_to_pstate : Returns the pstate id corresponding to the
 *		   frequency in the cpufreq frequency table
 *		   powernv_freqs indexed by @i.
 *
 *		   If @i is out of bound, this will return the pstate
 *		   corresponding to the nominal frequency.
 */
static inline u8 idx_to_pstate(unsigned int i)
{
	if (unlikely(i >= powernv_pstate_info.nr_pstates)) {
		pr_warn_once("idx_to_pstate: index %u is out of bound\n", i);
		return powernv_freqs[powernv_pstate_info.nominal].driver_data;
	}

	return powernv_freqs[i].driver_data;
}

/**
 * pstate_to_idx : Returns the index in the cpufreq frequencytable
 *		   powernv_freqs for the frequency whose corresponding
 *		   pstate id is @pstate.
 *
 *		   If no frequency corresponding to @pstate is found,
 *		   this will return the index of the nominal
 *		   frequency.
 */
static unsigned int pstate_to_idx(u8 pstate)
{
	unsigned int key = pstate % POWERNV_MAX_PSTATES;
	struct pstate_idx_revmap_data *revmap_data;

	hash_for_each_possible(pstate_revmap, revmap_data, hentry, key) {
		if (revmap_data->pstate_id == pstate)
			return revmap_data->cpufreq_table_idx;
	}

	pr_warn_once("pstate_to_idx: pstate 0x%x not found\n", pstate);
	return powernv_pstate_info.nominal;
}

static inline void reset_gpstates(struct cpufreq_policy *policy)
{
	struct global_pstate_info *gpstates = policy->driver_data;

	gpstates->highest_lpstate_idx = 0;
	gpstates->elapsed_time = 0;
	gpstates->last_sampled_time = 0;
	gpstates->last_lpstate_idx = 0;
	gpstates->last_gpstate_idx = 0;
}

/*
 * Initialize the freq table based on data obtained
 * from the firmware passed via device-tree
 */
static int init_powernv_pstates(void)
{
	struct device_node *power_mgt;
	int i, nr_pstates = 0;
	const __be32 *pstate_ids, *pstate_freqs;
	u32 len_ids, len_freqs;
	u32 pstate_min, pstate_max, pstate_nominal;
	u32 pstate_turbo, pstate_ultra_turbo;

	power_mgt = of_find_node_by_path("/ibm,opal/power-mgt");
	if (!power_mgt) {
		pr_warn("power-mgt node not found\n");
		return -ENODEV;
	}

	if (of_property_read_u32(power_mgt, "ibm,pstate-min", &pstate_min)) {
		pr_warn("ibm,pstate-min node not found\n");
		return -ENODEV;
	}

	if (of_property_read_u32(power_mgt, "ibm,pstate-max", &pstate_max)) {
		pr_warn("ibm,pstate-max node not found\n");
		return -ENODEV;
	}

	if (of_property_read_u32(power_mgt, "ibm,pstate-nominal",
				 &pstate_nominal)) {
		pr_warn("ibm,pstate-nominal not found\n");
		return -ENODEV;
	}

	if (of_property_read_u32(power_mgt, "ibm,pstate-ultra-turbo",
				 &pstate_ultra_turbo)) {
		powernv_pstate_info.wof_enabled = false;
		goto next;
	}

	if (of_property_read_u32(power_mgt, "ibm,pstate-turbo",
				 &pstate_turbo)) {
		powernv_pstate_info.wof_enabled = false;
		goto next;
	}

	if (pstate_turbo == pstate_ultra_turbo)
		powernv_pstate_info.wof_enabled = false;
	else
		powernv_pstate_info.wof_enabled = true;

next:
	pr_info("cpufreq pstate min 0x%x nominal 0x%x max 0x%x\n", pstate_min,
		pstate_nominal, pstate_max);
	pr_info("Workload Optimized Frequency is %s in the platform\n",
		(powernv_pstate_info.wof_enabled) ? "enabled" : "disabled");

	pstate_ids = of_get_property(power_mgt, "ibm,pstate-ids", &len_ids);
	if (!pstate_ids) {
		pr_warn("ibm,pstate-ids not found\n");
		return -ENODEV;
	}

	pstate_freqs = of_get_property(power_mgt, "ibm,pstate-frequencies-mhz",
				      &len_freqs);
	if (!pstate_freqs) {
		pr_warn("ibm,pstate-frequencies-mhz not found\n");
		return -ENODEV;
	}

	if (len_ids != len_freqs) {
		pr_warn("Entries in ibm,pstate-ids and "
			"ibm,pstate-frequencies-mhz does not match\n");
	}

	nr_pstates = min(len_ids, len_freqs) / sizeof(u32);
	if (!nr_pstates) {
		pr_warn("No PStates found\n");
		return -ENODEV;
	}

	powernv_pstate_info.nr_pstates = nr_pstates;
	pr_debug("NR PStates %d\n", nr_pstates);

	for (i = 0; i < nr_pstates; i++) {
		u32 id = be32_to_cpu(pstate_ids[i]);
		u32 freq = be32_to_cpu(pstate_freqs[i]);
		struct pstate_idx_revmap_data *revmap_data;
		unsigned int key;

		pr_debug("PState id %d freq %d MHz\n", id, freq);
		powernv_freqs[i].frequency = freq * 1000; /* kHz */
		powernv_freqs[i].driver_data = id & 0xFF;

		revmap_data = (struct pstate_idx_revmap_data *)
			      kmalloc(sizeof(*revmap_data), GFP_KERNEL);

		revmap_data->pstate_id = id & 0xFF;
		revmap_data->cpufreq_table_idx = i;
		key = (revmap_data->pstate_id) % POWERNV_MAX_PSTATES;
		hash_add(pstate_revmap, &revmap_data->hentry, key);

		if (id == pstate_max)
			powernv_pstate_info.max = i;
		if (id == pstate_nominal)
			powernv_pstate_info.nominal = i;
		if (id == pstate_min)
			powernv_pstate_info.min = i;

		if (powernv_pstate_info.wof_enabled && id == pstate_turbo) {
			int j;

			for (j = i - 1; j >= (int)powernv_pstate_info.max; j--)
				powernv_freqs[j].flags = CPUFREQ_BOOST_FREQ;
		}
	}

	/* End of list marker entry */
	powernv_freqs[i].frequency = CPUFREQ_TABLE_END;
	return 0;
}

/* Returns the CPU frequency corresponding to the pstate_id. */
static unsigned int pstate_id_to_freq(u8 pstate_id)
{
	int i;

	i = pstate_to_idx(pstate_id);
	if (i >= powernv_pstate_info.nr_pstates || i < 0) {
		pr_warn("PState id 0x%x outside of PState table, reporting nominal id 0x%x instead\n",
			pstate_id, idx_to_pstate(powernv_pstate_info.nominal));
		i = powernv_pstate_info.nominal;
	}

	return powernv_freqs[i].frequency;
}

/*
 * cpuinfo_nominal_freq_show - Show the nominal CPU frequency as indicated by
 * the firmware
 */
static ssize_t cpuinfo_nominal_freq_show(struct cpufreq_policy *policy,
					char *buf)
{
	return sprintf(buf, "%u\n",
		powernv_freqs[powernv_pstate_info.nominal].frequency);
}

struct freq_attr cpufreq_freq_attr_cpuinfo_nominal_freq =
	__ATTR_RO(cpuinfo_nominal_freq);

#define SCALING_BOOST_FREQS_ATTR_INDEX		2

static struct freq_attr *powernv_cpu_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&cpufreq_freq_attr_cpuinfo_nominal_freq,
	&cpufreq_freq_attr_scaling_boost_freqs,
	NULL,
};

#define throttle_attr(name, member)					\
static ssize_t name##_show(struct cpufreq_policy *policy, char *buf)	\
{									\
	struct chip *chip = per_cpu(chip_info, policy->cpu);		\
									\
	return sprintf(buf, "%u\n", chip->member);			\
}									\
									\
static struct freq_attr throttle_attr_##name = __ATTR_RO(name)		\

throttle_attr(unthrottle, reason[NO_THROTTLE]);
throttle_attr(powercap, reason[POWERCAP]);
throttle_attr(overtemp, reason[CPU_OVERTEMP]);
throttle_attr(supply_fault, reason[POWER_SUPPLY_FAILURE]);
throttle_attr(overcurrent, reason[OVERCURRENT]);
throttle_attr(occ_reset, reason[OCC_RESET_THROTTLE]);
throttle_attr(turbo_stat, throttle_turbo);
throttle_attr(sub_turbo_stat, throttle_sub_turbo);

static struct attribute *throttle_attrs[] = {
	&throttle_attr_unthrottle.attr,
	&throttle_attr_powercap.attr,
	&throttle_attr_overtemp.attr,
	&throttle_attr_supply_fault.attr,
	&throttle_attr_overcurrent.attr,
	&throttle_attr_occ_reset.attr,
	&throttle_attr_turbo_stat.attr,
	&throttle_attr_sub_turbo_stat.attr,
	NULL,
};

static const struct attribute_group throttle_attr_grp = {
	.name	= "throttle_stats",
	.attrs	= throttle_attrs,
};

/* Helper routines */

/* Access helpers to power mgt SPR */

static inline unsigned long get_pmspr(unsigned long sprn)
{
	switch (sprn) {
	case SPRN_PMCR:
		return mfspr(SPRN_PMCR);

	case SPRN_PMICR:
		return mfspr(SPRN_PMICR);

	case SPRN_PMSR:
		return mfspr(SPRN_PMSR);
	}
	BUG();
}

static inline void set_pmspr(unsigned long sprn, unsigned long val)
{
	switch (sprn) {
	case SPRN_PMCR:
		mtspr(SPRN_PMCR, val);
		return;

	case SPRN_PMICR:
		mtspr(SPRN_PMICR, val);
		return;
	}
	BUG();
}

/*
 * Use objects of this type to query/update
 * pstates on a remote CPU via smp_call_function.
 */
struct powernv_smp_call_data {
	unsigned int freq;
	u8 pstate_id;
	u8 gpstate_id;
};

/*
 * powernv_read_cpu_freq: Reads the current frequency on this CPU.
 *
 * Called via smp_call_function.
 *
 * Note: The caller of the smp_call_function should pass an argument of
 * the type 'struct powernv_smp_call_data *' along with this function.
 *
 * The current frequency on this CPU will be returned via
 * ((struct powernv_smp_call_data *)arg)->freq;
 */
static void powernv_read_cpu_freq(void *arg)
{
	unsigned long pmspr_val;
	struct powernv_smp_call_data *freq_data = arg;

	pmspr_val = get_pmspr(SPRN_PMSR);
	freq_data->pstate_id = extract_local_pstate(pmspr_val);
	freq_data->freq = pstate_id_to_freq(freq_data->pstate_id);

	pr_debug("cpu %d pmsr %016lX pstate_id 0x%x frequency %d kHz\n",
		 raw_smp_processor_id(), pmspr_val, freq_data->pstate_id,
		 freq_data->freq);
}

/*
 * powernv_cpufreq_get: Returns the CPU frequency as reported by the
 * firmware for CPU 'cpu'. This value is reported through the sysfs
 * file cpuinfo_cur_freq.
 */
static unsigned int powernv_cpufreq_get(unsigned int cpu)
{
	struct powernv_smp_call_data freq_data;

	smp_call_function_any(cpu_sibling_mask(cpu), powernv_read_cpu_freq,
			&freq_data, 1);

	return freq_data.freq;
}

/*
 * set_pstate: Sets the pstate on this CPU.
 *
 * This is called via an smp_call_function.
 *
 * The caller must ensure that freq_data is of the type
 * (struct powernv_smp_call_data *) and the pstate_id which needs to be set
 * on this CPU should be present in freq_data->pstate_id.
 */
static void set_pstate(void *data)
{
	unsigned long val;
	struct powernv_smp_call_data *freq_data = data;
	unsigned long pstate_ul = freq_data->pstate_id;
	unsigned long gpstate_ul = freq_data->gpstate_id;

	val = get_pmspr(SPRN_PMCR);
	val = val & 0x0000FFFFFFFFFFFFULL;

	pstate_ul = pstate_ul & 0xFF;
	gpstate_ul = gpstate_ul & 0xFF;

	/* Set both global(bits 56..63) and local(bits 48..55) PStates */
	val = val | (gpstate_ul << 56) | (pstate_ul << 48);

	pr_debug("Setting cpu %d pmcr to %016lX\n",
			raw_smp_processor_id(), val);
	set_pmspr(SPRN_PMCR, val);
}

/*
 * get_nominal_index: Returns the index corresponding to the nominal
 * pstate in the cpufreq table
 */
static inline unsigned int get_nominal_index(void)
{
	return powernv_pstate_info.nominal;
}

static void powernv_cpufreq_throttle_check(void *data)
{
	struct chip *chip;
	unsigned int cpu = smp_processor_id();
	unsigned long pmsr;
	u8 pmsr_pmax;
	unsigned int pmsr_pmax_idx;

	pmsr = get_pmspr(SPRN_PMSR);
	chip = this_cpu_read(chip_info);

	/* Check for Pmax Capping */
	pmsr_pmax = extract_max_pstate(pmsr);
	pmsr_pmax_idx = pstate_to_idx(pmsr_pmax);
	if (pmsr_pmax_idx != powernv_pstate_info.max) {
		if (chip->throttled)
			goto next;
		chip->throttled = true;
		if (pmsr_pmax_idx > powernv_pstate_info.nominal) {
			pr_warn_once("CPU %d on Chip %u has Pmax(0x%x) reduced below that of nominal frequency(0x%x)\n",
				     cpu, chip->id, pmsr_pmax,
				     idx_to_pstate(powernv_pstate_info.nominal));
			chip->throttle_sub_turbo++;
		} else {
			chip->throttle_turbo++;
		}
		trace_powernv_throttle(chip->id,
				      throttle_reason[chip->throttle_reason],
				      pmsr_pmax);
	} else if (chip->throttled) {
		chip->throttled = false;
		trace_powernv_throttle(chip->id,
				      throttle_reason[chip->throttle_reason],
				      pmsr_pmax);
	}

	/* Check if Psafe_mode_active is set in PMSR. */
next:
	if (pmsr & PMSR_PSAFE_ENABLE) {
		throttled = true;
		pr_info("Pstate set to safe frequency\n");
	}

	/* Check if SPR_EM_DISABLE is set in PMSR */
	if (pmsr & PMSR_SPR_EM_DISABLE) {
		throttled = true;
		pr_info("Frequency Control disabled from OS\n");
	}

	if (throttled) {
		pr_info("PMSR = %16lx\n", pmsr);
		pr_warn("CPU Frequency could be throttled\n");
	}
}

/**
 * calc_global_pstate - Calculate global pstate
 * @elapsed_time:		Elapsed time in milliseconds
 * @local_pstate_idx:		New local pstate
 * @highest_lpstate_idx:	pstate from which its ramping down
 *
 * Finds the appropriate global pstate based on the pstate from which its
 * ramping down and the time elapsed in ramping down. It follows a quadratic
 * equation which ensures that it reaches ramping down to pmin in 5sec.
 */
static inline int calc_global_pstate(unsigned int elapsed_time,
				     int highest_lpstate_idx,
				     int local_pstate_idx)
{
	int index_diff;

	/*
	 * Using ramp_down_percent we get the percentage of rampdown
	 * that we are expecting to be dropping. Difference between
	 * highest_lpstate_idx and powernv_pstate_info.min will give a absolute
	 * number of how many pstates we will drop eventually by the end of
	 * 5 seconds, then just scale it get the number pstates to be dropped.
	 */
	index_diff =  ((int)ramp_down_percent(elapsed_time) *
			(powernv_pstate_info.min - highest_lpstate_idx)) / 100;

	/* Ensure that global pstate is >= to local pstate */
	if (highest_lpstate_idx + index_diff >= local_pstate_idx)
		return local_pstate_idx;
	else
		return highest_lpstate_idx + index_diff;
}

static inline void  queue_gpstate_timer(struct global_pstate_info *gpstates)
{
	unsigned int timer_interval;

	/*
	 * Setting up timer to fire after GPSTATE_TIMER_INTERVAL ms, But
	 * if it exceeds MAX_RAMP_DOWN_TIME ms for ramp down time.
	 * Set timer such that it fires exactly at MAX_RAMP_DOWN_TIME
	 * seconds of ramp down time.
	 */
	if ((gpstates->elapsed_time + GPSTATE_TIMER_INTERVAL)
	     > MAX_RAMP_DOWN_TIME)
		timer_interval = MAX_RAMP_DOWN_TIME - gpstates->elapsed_time;
	else
		timer_interval = GPSTATE_TIMER_INTERVAL;

	mod_timer(&gpstates->timer, jiffies + msecs_to_jiffies(timer_interval));
}

/**
 * gpstate_timer_handler
 *
 * @data: pointer to cpufreq_policy on which timer was queued
 *
 * This handler brings down the global pstate closer to the local pstate
 * according quadratic equation. Queues a new timer if it is still not equal
 * to local pstate
 */
void gpstate_timer_handler(struct timer_list *t)
{
	struct global_pstate_info *gpstates = from_timer(gpstates, t, timer);
	struct cpufreq_policy *policy = gpstates->policy;
	int gpstate_idx, lpstate_idx;
	unsigned long val;
	unsigned int time_diff = jiffies_to_msecs(jiffies)
					- gpstates->last_sampled_time;
	struct powernv_smp_call_data freq_data;

	if (!spin_trylock(&gpstates->gpstate_lock))
		return;
	/*
	 * If the timer has migrated to the different cpu then bring
	 * it back to one of the policy->cpus
	 */
	if (!cpumask_test_cpu(raw_smp_processor_id(), policy->cpus)) {
		gpstates->timer.expires = jiffies + msecs_to_jiffies(1);
		add_timer_on(&gpstates->timer, cpumask_first(policy->cpus));
		spin_unlock(&gpstates->gpstate_lock);
		return;
	}

	/*
	 * If PMCR was last updated was using fast_swtich then
	 * We may have wrong in gpstate->last_lpstate_idx
	 * value. Hence, read from PMCR to get correct data.
	 */
	val = get_pmspr(SPRN_PMCR);
	freq_data.gpstate_id = extract_global_pstate(val);
	freq_data.pstate_id = extract_local_pstate(val);
	if (freq_data.gpstate_id  == freq_data.pstate_id) {
		reset_gpstates(policy);
		spin_unlock(&gpstates->gpstate_lock);
		return;
	}

	gpstates->last_sampled_time += time_diff;
	gpstates->elapsed_time += time_diff;

	if (gpstates->elapsed_time > MAX_RAMP_DOWN_TIME) {
		gpstate_idx = pstate_to_idx(freq_data.pstate_id);
		lpstate_idx = gpstate_idx;
		reset_gpstates(policy);
		gpstates->highest_lpstate_idx = gpstate_idx;
	} else {
		lpstate_idx = pstate_to_idx(freq_data.pstate_id);
		gpstate_idx = calc_global_pstate(gpstates->elapsed_time,
						 gpstates->highest_lpstate_idx,
						 lpstate_idx);
	}
	freq_data.gpstate_id = idx_to_pstate(gpstate_idx);
	gpstates->last_gpstate_idx = gpstate_idx;
	gpstates->last_lpstate_idx = lpstate_idx;
	/*
	 * If local pstate is equal to global pstate, rampdown is over
	 * So timer is not required to be queued.
	 */
	if (gpstate_idx != gpstates->last_lpstate_idx)
		queue_gpstate_timer(gpstates);

	set_pstate(&freq_data);
	spin_unlock(&gpstates->gpstate_lock);
}

/*
 * powernv_cpufreq_target_index: Sets the frequency corresponding to
 * the cpufreq table entry indexed by new_index on the cpus in the
 * mask policy->cpus
 */
static int powernv_cpufreq_target_index(struct cpufreq_policy *policy,
					unsigned int new_index)
{
	struct powernv_smp_call_data freq_data;
	unsigned int cur_msec, gpstate_idx;
	struct global_pstate_info *gpstates = policy->driver_data;

	if (unlikely(rebooting) && new_index != get_nominal_index())
		return 0;

	if (!throttled) {
		/* we don't want to be preempted while
		 * checking if the CPU frequency has been throttled
		 */
		preempt_disable();
		powernv_cpufreq_throttle_check(NULL);
		preempt_enable();
	}

	cur_msec = jiffies_to_msecs(get_jiffies_64());

	freq_data.pstate_id = idx_to_pstate(new_index);
	if (!gpstates) {
		freq_data.gpstate_id = freq_data.pstate_id;
		goto no_gpstate;
	}

	spin_lock(&gpstates->gpstate_lock);

	if (!gpstates->last_sampled_time) {
		gpstate_idx = new_index;
		gpstates->highest_lpstate_idx = new_index;
		goto gpstates_done;
	}

	if (gpstates->last_gpstate_idx < new_index) {
		gpstates->elapsed_time += cur_msec -
						 gpstates->last_sampled_time;

		/*
		 * If its has been ramping down for more than MAX_RAMP_DOWN_TIME
		 * we should be resetting all global pstate related data. Set it
		 * equal to local pstate to start fresh.
		 */
		if (gpstates->elapsed_time > MAX_RAMP_DOWN_TIME) {
			reset_gpstates(policy);
			gpstates->highest_lpstate_idx = new_index;
			gpstate_idx = new_index;
		} else {
		/* Elaspsed_time is less than 5 seconds, continue to rampdown */
			gpstate_idx = calc_global_pstate(gpstates->elapsed_time,
							 gpstates->highest_lpstate_idx,
							 new_index);
		}
	} else {
		reset_gpstates(policy);
		gpstates->highest_lpstate_idx = new_index;
		gpstate_idx = new_index;
	}

	/*
	 * If local pstate is equal to global pstate, rampdown is over
	 * So timer is not required to be queued.
	 */
	if (gpstate_idx != new_index)
		queue_gpstate_timer(gpstates);
	else
		del_timer_sync(&gpstates->timer);

gpstates_done:
	freq_data.gpstate_id = idx_to_pstate(gpstate_idx);
	gpstates->last_sampled_time = cur_msec;
	gpstates->last_gpstate_idx = gpstate_idx;
	gpstates->last_lpstate_idx = new_index;

	spin_unlock(&gpstates->gpstate_lock);

no_gpstate:
	/*
	 * Use smp_call_function to send IPI and execute the
	 * mtspr on target CPU.  We could do that without IPI
	 * if current CPU is within policy->cpus (core)
	 */
	smp_call_function_any(policy->cpus, set_pstate, &freq_data, 1);
	return 0;
}

static int powernv_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int base, i;
	struct kernfs_node *kn;
	struct global_pstate_info *gpstates;

	base = cpu_first_thread_sibling(policy->cpu);

	for (i = 0; i < threads_per_core; i++)
		cpumask_set_cpu(base + i, policy->cpus);

	kn = kernfs_find_and_get(policy->kobj.sd, throttle_attr_grp.name);
	if (!kn) {
		int ret;

		ret = sysfs_create_group(&policy->kobj, &throttle_attr_grp);
		if (ret) {
			pr_info("Failed to create throttle stats directory for cpu %d\n",
				policy->cpu);
			return ret;
		}
	} else {
		kernfs_put(kn);
	}

	policy->freq_table = powernv_freqs;
	policy->fast_switch_possible = true;

	if (pvr_version_is(PVR_POWER9))
		return 0;

	/* Initialise Gpstate ramp-down timer only on POWER8 */
	gpstates =  kzalloc(sizeof(*gpstates), GFP_KERNEL);
	if (!gpstates)
		return -ENOMEM;

	policy->driver_data = gpstates;

	/* initialize timer */
	gpstates->policy = policy;
	timer_setup(&gpstates->timer, gpstate_timer_handler,
		    TIMER_PINNED | TIMER_DEFERRABLE);
	gpstates->timer.expires = jiffies +
				msecs_to_jiffies(GPSTATE_TIMER_INTERVAL);
	spin_lock_init(&gpstates->gpstate_lock);

	return 0;
}

static int powernv_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	/* timer is deleted in cpufreq_cpu_stop() */
	kfree(policy->driver_data);

	return 0;
}

static int powernv_cpufreq_reboot_notifier(struct notifier_block *nb,
				unsigned long action, void *unused)
{
	int cpu;
	struct cpufreq_policy cpu_policy;

	rebooting = true;
	for_each_online_cpu(cpu) {
		cpufreq_get_policy(&cpu_policy, cpu);
		powernv_cpufreq_target_index(&cpu_policy, get_nominal_index());
	}

	return NOTIFY_DONE;
}

static struct notifier_block powernv_cpufreq_reboot_nb = {
	.notifier_call = powernv_cpufreq_reboot_notifier,
};

void powernv_cpufreq_work_fn(struct work_struct *work)
{
	struct chip *chip = container_of(work, struct chip, throttle);
	unsigned int cpu;
	cpumask_t mask;

	get_online_cpus();
	cpumask_and(&mask, &chip->mask, cpu_online_mask);
	smp_call_function_any(&mask,
			      powernv_cpufreq_throttle_check, NULL, 0);

	if (!chip->restore)
		goto out;

	chip->restore = false;
	for_each_cpu(cpu, &mask) {
		int index;
		struct cpufreq_policy policy;

		cpufreq_get_policy(&policy, cpu);
		index = cpufreq_table_find_index_c(&policy, policy.cur);
		powernv_cpufreq_target_index(&policy, index);
		cpumask_andnot(&mask, &mask, policy.cpus);
	}
out:
	put_online_cpus();
}

static int powernv_cpufreq_occ_msg(struct notifier_block *nb,
				   unsigned long msg_type, void *_msg)
{
	struct opal_msg *msg = _msg;
	struct opal_occ_msg omsg;
	int i;

	if (msg_type != OPAL_MSG_OCC)
		return 0;

	omsg.type = be64_to_cpu(msg->params[0]);

	switch (omsg.type) {
	case OCC_RESET:
		occ_reset = true;
		pr_info("OCC (On Chip Controller - enforces hard thermal/power limits) Resetting\n");
		/*
		 * powernv_cpufreq_throttle_check() is called in
		 * target() callback which can detect the throttle state
		 * for governors like ondemand.
		 * But static governors will not call target() often thus
		 * report throttling here.
		 */
		if (!throttled) {
			throttled = true;
			pr_warn("CPU frequency is throttled for duration\n");
		}

		break;
	case OCC_LOAD:
		pr_info("OCC Loading, CPU frequency is throttled until OCC is started\n");
		break;
	case OCC_THROTTLE:
		omsg.chip = be64_to_cpu(msg->params[1]);
		omsg.throttle_status = be64_to_cpu(msg->params[2]);

		if (occ_reset) {
			occ_reset = false;
			throttled = false;
			pr_info("OCC Active, CPU frequency is no longer throttled\n");

			for (i = 0; i < nr_chips; i++) {
				chips[i].restore = true;
				schedule_work(&chips[i].throttle);
			}

			return 0;
		}

		for (i = 0; i < nr_chips; i++)
			if (chips[i].id == omsg.chip)
				break;

		if (omsg.throttle_status >= 0 &&
		    omsg.throttle_status <= OCC_MAX_THROTTLE_STATUS) {
			chips[i].throttle_reason = omsg.throttle_status;
			chips[i].reason[omsg.throttle_status]++;
		}

		if (!omsg.throttle_status)
			chips[i].restore = true;

		schedule_work(&chips[i].throttle);
	}
	return 0;
}

static struct notifier_block powernv_cpufreq_opal_nb = {
	.notifier_call	= powernv_cpufreq_occ_msg,
	.next		= NULL,
	.priority	= 0,
};

static void powernv_cpufreq_stop_cpu(struct cpufreq_policy *policy)
{
	struct powernv_smp_call_data freq_data;
	struct global_pstate_info *gpstates = policy->driver_data;

	freq_data.pstate_id = idx_to_pstate(powernv_pstate_info.min);
	freq_data.gpstate_id = idx_to_pstate(powernv_pstate_info.min);
	smp_call_function_single(policy->cpu, set_pstate, &freq_data, 1);
	if (gpstates)
		del_timer_sync(&gpstates->timer);
}

static unsigned int powernv_fast_switch(struct cpufreq_policy *policy,
					unsigned int target_freq)
{
	int index;
	struct powernv_smp_call_data freq_data;

	index = cpufreq_table_find_index_dl(policy, target_freq);
	freq_data.pstate_id = powernv_freqs[index].driver_data;
	freq_data.gpstate_id = powernv_freqs[index].driver_data;
	set_pstate(&freq_data);

	return powernv_freqs[index].frequency;
}

static struct cpufreq_driver powernv_cpufreq_driver = {
	.name		= "powernv-cpufreq",
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= powernv_cpufreq_cpu_init,
	.exit		= powernv_cpufreq_cpu_exit,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= powernv_cpufreq_target_index,
	.fast_switch	= powernv_fast_switch,
	.get		= powernv_cpufreq_get,
	.stop_cpu	= powernv_cpufreq_stop_cpu,
	.attr		= powernv_cpu_freq_attr,
};

static int init_chip_info(void)
{
	unsigned int chip[256];
	unsigned int cpu, i;
	unsigned int prev_chip_id = UINT_MAX;

	for_each_possible_cpu(cpu) {
		unsigned int id = cpu_to_chip_id(cpu);

		if (prev_chip_id != id) {
			prev_chip_id = id;
			chip[nr_chips++] = id;
		}
	}

	chips = kcalloc(nr_chips, sizeof(struct chip), GFP_KERNEL);
	if (!chips)
		return -ENOMEM;

	for (i = 0; i < nr_chips; i++) {
		chips[i].id = chip[i];
		cpumask_copy(&chips[i].mask, cpumask_of_node(chip[i]));
		INIT_WORK(&chips[i].throttle, powernv_cpufreq_work_fn);
		for_each_cpu(cpu, &chips[i].mask)
			per_cpu(chip_info, cpu) =  &chips[i];
	}

	return 0;
}

static inline void clean_chip_info(void)
{
	kfree(chips);
}

static inline void unregister_all_notifiers(void)
{
	opal_message_notifier_unregister(OPAL_MSG_OCC,
					 &powernv_cpufreq_opal_nb);
	unregister_reboot_notifier(&powernv_cpufreq_reboot_nb);
}

static int __init powernv_cpufreq_init(void)
{
	int rc = 0;

	/* Don't probe on pseries (guest) platforms */
	if (!firmware_has_feature(FW_FEATURE_OPAL))
		return -ENODEV;

	/* Discover pstates from device tree and init */
	rc = init_powernv_pstates();
	if (rc)
		goto out;

	/* Populate chip info */
	rc = init_chip_info();
	if (rc)
		goto out;

	register_reboot_notifier(&powernv_cpufreq_reboot_nb);
	opal_message_notifier_register(OPAL_MSG_OCC, &powernv_cpufreq_opal_nb);

	if (powernv_pstate_info.wof_enabled)
		powernv_cpufreq_driver.boost_enabled = true;
	else
		powernv_cpu_freq_attr[SCALING_BOOST_FREQS_ATTR_INDEX] = NULL;

	rc = cpufreq_register_driver(&powernv_cpufreq_driver);
	if (rc) {
		pr_info("Failed to register the cpufreq driver (%d)\n", rc);
		goto cleanup_notifiers;
	}

	if (powernv_pstate_info.wof_enabled)
		cpufreq_enable_boost_support();

	return 0;
cleanup_notifiers:
	unregister_all_notifiers();
	clean_chip_info();
out:
	pr_info("Platform driver disabled. System does not support PState control\n");
	return rc;
}
module_init(powernv_cpufreq_init);

static void __exit powernv_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&powernv_cpufreq_driver);
	unregister_all_notifiers();
	clean_chip_info();
}
module_exit(powernv_cpufreq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vaidyanathan Srinivasan <svaidy at linux.vnet.ibm.com>");
