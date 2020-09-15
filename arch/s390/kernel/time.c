// SPDX-License-Identifier: GPL-2.0
/*
 *    Time of day based timer functions.
 *
 *  S390 version
 *    Copyright IBM Corp. 1999, 2008
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "arch/i386/kernel/time.c"
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */

#define KMSG_COMPONENT "time"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/stop_machine.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/profile.h>
#include <linux/timex.h>
#include <linux/notifier.h>
#include <linux/timekeeper_internal.h>
#include <linux/clockchips.h>
#include <linux/gfp.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <vdso/vsyscall.h>
#include <vdso/clocksource.h>
#include <vdso/helpers.h>
#include <asm/facility.h>
#include <asm/delay.h>
#include <asm/div64.h>
#include <asm/vdso.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/vtimer.h>
#include <asm/stp.h>
#include <asm/cio.h>
#include "entry.h"

unsigned char tod_clock_base[16] __aligned(8) = {
	/* Force to data section. */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
EXPORT_SYMBOL_GPL(tod_clock_base);

u64 clock_comparator_max = -1ULL;
EXPORT_SYMBOL_GPL(clock_comparator_max);

static DEFINE_PER_CPU(struct clock_event_device, comparators);

ATOMIC_NOTIFIER_HEAD(s390_epoch_delta_notifier);
EXPORT_SYMBOL(s390_epoch_delta_notifier);

unsigned char ptff_function_mask[16];

static unsigned long long lpar_offset;
static unsigned long long initial_leap_seconds;
static unsigned long long tod_steering_end;
static long long tod_steering_delta;

/*
 * Get time offsets with PTFF
 */
void __init time_early_init(void)
{
	struct ptff_qto qto;
	struct ptff_qui qui;

	/* Initialize TOD steering parameters */
	tod_steering_end = *(unsigned long long *) &tod_clock_base[1];
	vdso_data->arch_data.tod_steering_end = tod_steering_end;

	if (!test_facility(28))
		return;

	ptff(&ptff_function_mask, sizeof(ptff_function_mask), PTFF_QAF);

	/* get LPAR offset */
	if (ptff_query(PTFF_QTO) && ptff(&qto, sizeof(qto), PTFF_QTO) == 0)
		lpar_offset = qto.tod_epoch_difference;

	/* get initial leap seconds */
	if (ptff_query(PTFF_QUI) && ptff(&qui, sizeof(qui), PTFF_QUI) == 0)
		initial_leap_seconds = (unsigned long long)
			((long) qui.old_leap * 4096000000L);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long notrace sched_clock(void)
{
	return tod_to_ns(get_tod_clock_monotonic());
}
NOKPROBE_SYMBOL(sched_clock);

static void ext_to_timespec64(unsigned char *clk, struct timespec64 *xt)
{
	unsigned long long high, low, rem, sec, nsec;

	/* Split extendnd TOD clock to micro-seconds and sub-micro-seconds */
	high = (*(unsigned long long *) clk) >> 4;
	low = (*(unsigned long long *)&clk[7]) << 4;
	/* Calculate seconds and nano-seconds */
	sec = high;
	rem = do_div(sec, 1000000);
	nsec = (((low >> 32) + (rem << 32)) * 1000) >> 32;

	xt->tv_sec = sec;
	xt->tv_nsec = nsec;
}

void clock_comparator_work(void)
{
	struct clock_event_device *cd;

	S390_lowcore.clock_comparator = clock_comparator_max;
	cd = this_cpu_ptr(&comparators);
	cd->event_handler(cd);
}

static int s390_next_event(unsigned long delta,
			   struct clock_event_device *evt)
{
	S390_lowcore.clock_comparator = get_tod_clock() + delta;
	set_clock_comparator(S390_lowcore.clock_comparator);
	return 0;
}

/*
 * Set up lowcore and control register of the current cpu to
 * enable TOD clock and clock comparator interrupts.
 */
void init_cpu_timer(void)
{
	struct clock_event_device *cd;
	int cpu;

	S390_lowcore.clock_comparator = clock_comparator_max;
	set_clock_comparator(S390_lowcore.clock_comparator);

	cpu = smp_processor_id();
	cd = &per_cpu(comparators, cpu);
	cd->name		= "comparator";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT;
	cd->mult		= 16777;
	cd->shift		= 12;
	cd->min_delta_ns	= 1;
	cd->min_delta_ticks	= 1;
	cd->max_delta_ns	= LONG_MAX;
	cd->max_delta_ticks	= ULONG_MAX;
	cd->rating		= 400;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= s390_next_event;

	clockevents_register_device(cd);

	/* Enable clock comparator timer interrupt. */
	__ctl_set_bit(0,11);

	/* Always allow the timing alert external interrupt. */
	__ctl_set_bit(0, 4);
}

static void clock_comparator_interrupt(struct ext_code ext_code,
				       unsigned int param32,
				       unsigned long param64)
{
	inc_irq_stat(IRQEXT_CLK);
	if (S390_lowcore.clock_comparator == clock_comparator_max)
		set_clock_comparator(S390_lowcore.clock_comparator);
}

static void stp_timing_alert(struct stp_irq_parm *);

static void timing_alert_interrupt(struct ext_code ext_code,
				   unsigned int param32, unsigned long param64)
{
	inc_irq_stat(IRQEXT_TLA);
	if (param32 & 0x00038000)
		stp_timing_alert((struct stp_irq_parm *) &param32);
}

static void stp_reset(void);

void read_persistent_clock64(struct timespec64 *ts)
{
	unsigned char clk[STORE_CLOCK_EXT_SIZE];
	__u64 delta;

	delta = initial_leap_seconds + TOD_UNIX_EPOCH;
	get_tod_clock_ext(clk);
	*(__u64 *) &clk[1] -= delta;
	if (*(__u64 *) &clk[1] > delta)
		clk[0]--;
	ext_to_timespec64(clk, ts);
}

void __init read_persistent_wall_and_boot_offset(struct timespec64 *wall_time,
						 struct timespec64 *boot_offset)
{
	unsigned char clk[STORE_CLOCK_EXT_SIZE];
	struct timespec64 boot_time;
	__u64 delta;

	delta = initial_leap_seconds + TOD_UNIX_EPOCH;
	memcpy(clk, tod_clock_base, STORE_CLOCK_EXT_SIZE);
	*(__u64 *)&clk[1] -= delta;
	if (*(__u64 *)&clk[1] > delta)
		clk[0]--;
	ext_to_timespec64(clk, &boot_time);

	read_persistent_clock64(wall_time);
	*boot_offset = timespec64_sub(*wall_time, boot_time);
}

static u64 read_tod_clock(struct clocksource *cs)
{
	unsigned long long now, adj;

	preempt_disable(); /* protect from changes to steering parameters */
	now = get_tod_clock();
	adj = tod_steering_end - now;
	if (unlikely((s64) adj > 0))
		/*
		 * manually steer by 1 cycle every 2^16 cycles. This
		 * corresponds to shifting the tod delta by 15. 1s is
		 * therefore steered in ~9h. The adjust will decrease
		 * over time, until it finally reaches 0.
		 */
		now += (tod_steering_delta < 0) ? (adj >> 15) : -(adj >> 15);
	preempt_enable();
	return now;
}

static struct clocksource clocksource_tod = {
	.name		= "tod",
	.rating		= 400,
	.read		= read_tod_clock,
	.mask		= CLOCKSOURCE_MASK(64),
	.mult		= 1000,
	.shift		= 12,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.vdso_clock_mode = VDSO_CLOCKMODE_TOD,
};

struct clocksource * __init clocksource_default_clock(void)
{
	return &clocksource_tod;
}

/*
 * Initialize the TOD clock and the CPU timer of
 * the boot cpu.
 */
void __init time_init(void)
{
	/* Reset time synchronization interfaces. */
	stp_reset();

	/* request the clock comparator external interrupt */
	if (register_external_irq(EXT_IRQ_CLK_COMP, clock_comparator_interrupt))
		panic("Couldn't request external interrupt 0x1004");

	/* request the timing alert external interrupt */
	if (register_external_irq(EXT_IRQ_TIMING_ALERT, timing_alert_interrupt))
		panic("Couldn't request external interrupt 0x1406");

	if (__clocksource_register(&clocksource_tod) != 0)
		panic("Could not register TOD clock source");

	/* Enable TOD clock interrupts on the boot cpu. */
	init_cpu_timer();

	/* Enable cpu timer interrupts on the boot cpu. */
	vtime_init();
}

static DEFINE_PER_CPU(atomic_t, clock_sync_word);
static DEFINE_MUTEX(clock_sync_mutex);
static unsigned long clock_sync_flags;

#define CLOCK_SYNC_HAS_STP		0
#define CLOCK_SYNC_STP			1
#define CLOCK_SYNC_STPINFO_VALID	2

/*
 * The get_clock function for the physical clock. It will get the current
 * TOD clock, subtract the LPAR offset and write the result to *clock.
 * The function returns 0 if the clock is in sync with the external time
 * source. If the clock mode is local it will return -EOPNOTSUPP and
 * -EAGAIN if the clock is not in sync with the external reference.
 */
int get_phys_clock(unsigned long *clock)
{
	atomic_t *sw_ptr;
	unsigned int sw0, sw1;

	sw_ptr = &get_cpu_var(clock_sync_word);
	sw0 = atomic_read(sw_ptr);
	*clock = get_tod_clock() - lpar_offset;
	sw1 = atomic_read(sw_ptr);
	put_cpu_var(clock_sync_word);
	if (sw0 == sw1 && (sw0 & 0x80000000U))
		/* Success: time is in sync. */
		return 0;
	if (!test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags))
		return -EOPNOTSUPP;
	if (!test_bit(CLOCK_SYNC_STP, &clock_sync_flags))
		return -EACCES;
	return -EAGAIN;
}
EXPORT_SYMBOL(get_phys_clock);

/*
 * Make get_phys_clock() return -EAGAIN.
 */
static void disable_sync_clock(void *dummy)
{
	atomic_t *sw_ptr = this_cpu_ptr(&clock_sync_word);
	/*
	 * Clear the in-sync bit 2^31. All get_phys_clock calls will
	 * fail until the sync bit is turned back on. In addition
	 * increase the "sequence" counter to avoid the race of an
	 * stp event and the complete recovery against get_phys_clock.
	 */
	atomic_andnot(0x80000000, sw_ptr);
	atomic_inc(sw_ptr);
}

/*
 * Make get_phys_clock() return 0 again.
 * Needs to be called from a context disabled for preemption.
 */
static void enable_sync_clock(void)
{
	atomic_t *sw_ptr = this_cpu_ptr(&clock_sync_word);
	atomic_or(0x80000000, sw_ptr);
}

/*
 * Function to check if the clock is in sync.
 */
static inline int check_sync_clock(void)
{
	atomic_t *sw_ptr;
	int rc;

	sw_ptr = &get_cpu_var(clock_sync_word);
	rc = (atomic_read(sw_ptr) & 0x80000000U) != 0;
	put_cpu_var(clock_sync_word);
	return rc;
}

/*
 * Apply clock delta to the global data structures.
 * This is called once on the CPU that performed the clock sync.
 */
static void clock_sync_global(unsigned long long delta)
{
	unsigned long now, adj;
	struct ptff_qto qto;

	/* Fixup the monotonic sched clock. */
	*(unsigned long long *) &tod_clock_base[1] += delta;
	if (*(unsigned long long *) &tod_clock_base[1] < delta)
		/* Epoch overflow */
		tod_clock_base[0]++;
	/* Adjust TOD steering parameters. */
	now = get_tod_clock();
	adj = tod_steering_end - now;
	if (unlikely((s64) adj >= 0))
		/* Calculate how much of the old adjustment is left. */
		tod_steering_delta = (tod_steering_delta < 0) ?
			-(adj >> 15) : (adj >> 15);
	tod_steering_delta += delta;
	if ((abs(tod_steering_delta) >> 48) != 0)
		panic("TOD clock sync offset %lli is too large to drift\n",
		      tod_steering_delta);
	tod_steering_end = now + (abs(tod_steering_delta) << 15);
	vdso_data->arch_data.tod_steering_end = tod_steering_end;

	/* Update LPAR offset. */
	if (ptff_query(PTFF_QTO) && ptff(&qto, sizeof(qto), PTFF_QTO) == 0)
		lpar_offset = qto.tod_epoch_difference;
	/* Call the TOD clock change notifier. */
	atomic_notifier_call_chain(&s390_epoch_delta_notifier, 0, &delta);
}

/*
 * Apply clock delta to the per-CPU data structures of this CPU.
 * This is called for each online CPU after the call to clock_sync_global.
 */
static void clock_sync_local(unsigned long long delta)
{
	/* Add the delta to the clock comparator. */
	if (S390_lowcore.clock_comparator != clock_comparator_max) {
		S390_lowcore.clock_comparator += delta;
		set_clock_comparator(S390_lowcore.clock_comparator);
	}
	/* Adjust the last_update_clock time-stamp. */
	S390_lowcore.last_update_clock += delta;
}

/* Single threaded workqueue used for stp sync events */
static struct workqueue_struct *time_sync_wq;

static void __init time_init_wq(void)
{
	if (time_sync_wq)
		return;
	time_sync_wq = create_singlethread_workqueue("timesync");
}

struct clock_sync_data {
	atomic_t cpus;
	int in_sync;
	unsigned long long clock_delta;
};

/*
 * Server Time Protocol (STP) code.
 */
static bool stp_online;
static struct stp_sstpi stp_info;
static void *stp_page;

static void stp_work_fn(struct work_struct *work);
static DEFINE_MUTEX(stp_work_mutex);
static DECLARE_WORK(stp_work, stp_work_fn);
static struct timer_list stp_timer;

static int __init early_parse_stp(char *p)
{
	return kstrtobool(p, &stp_online);
}
early_param("stp", early_parse_stp);

/*
 * Reset STP attachment.
 */
static void __init stp_reset(void)
{
	int rc;

	stp_page = (void *) get_zeroed_page(GFP_ATOMIC);
	rc = chsc_sstpc(stp_page, STP_OP_CTRL, 0x0000, NULL);
	if (rc == 0)
		set_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags);
	else if (stp_online) {
		pr_warn("The real or virtual hardware system does not provide an STP interface\n");
		free_page((unsigned long) stp_page);
		stp_page = NULL;
		stp_online = false;
	}
}

static void stp_timeout(struct timer_list *unused)
{
	queue_work(time_sync_wq, &stp_work);
}

static int __init stp_init(void)
{
	if (!test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags))
		return 0;
	timer_setup(&stp_timer, stp_timeout, 0);
	time_init_wq();
	if (!stp_online)
		return 0;
	queue_work(time_sync_wq, &stp_work);
	return 0;
}

arch_initcall(stp_init);

/*
 * STP timing alert. There are three causes:
 * 1) timing status change
 * 2) link availability change
 * 3) time control parameter change
 * In all three cases we are only interested in the clock source state.
 * If a STP clock source is now available use it.
 */
static void stp_timing_alert(struct stp_irq_parm *intparm)
{
	if (intparm->tsc || intparm->lac || intparm->tcpc)
		queue_work(time_sync_wq, &stp_work);
}

/*
 * STP sync check machine check. This is called when the timing state
 * changes from the synchronized state to the unsynchronized state.
 * After a STP sync check the clock is not in sync. The machine check
 * is broadcasted to all cpus at the same time.
 */
int stp_sync_check(void)
{
	disable_sync_clock(NULL);
	return 1;
}

/*
 * STP island condition machine check. This is called when an attached
 * server  attempts to communicate over an STP link and the servers
 * have matching CTN ids and have a valid stratum-1 configuration
 * but the configurations do not match.
 */
int stp_island_check(void)
{
	disable_sync_clock(NULL);
	return 1;
}

void stp_queue_work(void)
{
	queue_work(time_sync_wq, &stp_work);
}

static int __store_stpinfo(void)
{
	int rc = chsc_sstpi(stp_page, &stp_info, sizeof(struct stp_sstpi));

	if (rc)
		clear_bit(CLOCK_SYNC_STPINFO_VALID, &clock_sync_flags);
	else
		set_bit(CLOCK_SYNC_STPINFO_VALID, &clock_sync_flags);
	return rc;
}

static int stpinfo_valid(void)
{
	return stp_online && test_bit(CLOCK_SYNC_STPINFO_VALID, &clock_sync_flags);
}

static int stp_sync_clock(void *data)
{
	struct clock_sync_data *sync = data;
	unsigned long long clock_delta, flags;
	static int first;
	int rc;

	enable_sync_clock();
	if (xchg(&first, 1) == 0) {
		/* Wait until all other cpus entered the sync function. */
		while (atomic_read(&sync->cpus) != 0)
			cpu_relax();
		rc = 0;
		if (stp_info.todoff[0] || stp_info.todoff[1] ||
		    stp_info.todoff[2] || stp_info.todoff[3] ||
		    stp_info.tmd != 2) {
			flags = vdso_update_begin();
			rc = chsc_sstpc(stp_page, STP_OP_SYNC, 0,
					&clock_delta);
			if (rc == 0) {
				sync->clock_delta = clock_delta;
				clock_sync_global(clock_delta);
				rc = __store_stpinfo();
				if (rc == 0 && stp_info.tmd != 2)
					rc = -EAGAIN;
			}
			vdso_update_end(flags);
		}
		sync->in_sync = rc ? -EAGAIN : 1;
		xchg(&first, 0);
	} else {
		/* Slave */
		atomic_dec(&sync->cpus);
		/* Wait for in_sync to be set. */
		while (READ_ONCE(sync->in_sync) == 0)
			__udelay(1);
	}
	if (sync->in_sync != 1)
		/* Didn't work. Clear per-cpu in sync bit again. */
		disable_sync_clock(NULL);
	/* Apply clock delta to per-CPU fields of this CPU. */
	clock_sync_local(sync->clock_delta);

	return 0;
}

/*
 * STP work. Check for the STP state and take over the clock
 * synchronization if the STP clock source is usable.
 */
static void stp_work_fn(struct work_struct *work)
{
	struct clock_sync_data stp_sync;
	int rc;

	/* prevent multiple execution. */
	mutex_lock(&stp_work_mutex);

	if (!stp_online) {
		chsc_sstpc(stp_page, STP_OP_CTRL, 0x0000, NULL);
		del_timer_sync(&stp_timer);
		goto out_unlock;
	}

	rc = chsc_sstpc(stp_page, STP_OP_CTRL, 0xb0e0, NULL);
	if (rc)
		goto out_unlock;

	rc = __store_stpinfo();
	if (rc || stp_info.c == 0)
		goto out_unlock;

	/* Skip synchronization if the clock is already in sync. */
	if (check_sync_clock())
		goto out_unlock;

	memset(&stp_sync, 0, sizeof(stp_sync));
	cpus_read_lock();
	atomic_set(&stp_sync.cpus, num_online_cpus() - 1);
	stop_machine_cpuslocked(stp_sync_clock, &stp_sync, cpu_online_mask);
	cpus_read_unlock();

	if (!check_sync_clock())
		/*
		 * There is a usable clock but the synchonization failed.
		 * Retry after a second.
		 */
		mod_timer(&stp_timer, jiffies + msecs_to_jiffies(MSEC_PER_SEC));

out_unlock:
	mutex_unlock(&stp_work_mutex);
}

/*
 * STP subsys sysfs interface functions
 */
static struct bus_type stp_subsys = {
	.name		= "stp",
	.dev_name	= "stp",
};

static ssize_t ctn_id_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid())
		ret = sprintf(buf, "%016llx\n",
			      *(unsigned long long *) stp_info.ctnid);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(ctn_id);

static ssize_t ctn_type_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid())
		ret = sprintf(buf, "%i\n", stp_info.ctn);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(ctn_type);

static ssize_t dst_offset_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid() && (stp_info.vbits & 0x2000))
		ret = sprintf(buf, "%i\n", (int)(s16) stp_info.dsto);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(dst_offset);

static ssize_t leap_seconds_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid() && (stp_info.vbits & 0x8000))
		ret = sprintf(buf, "%i\n", (int)(s16) stp_info.leaps);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(leap_seconds);

static ssize_t stratum_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid())
		ret = sprintf(buf, "%i\n", (int)(s16) stp_info.stratum);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(stratum);

static ssize_t time_offset_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid() && (stp_info.vbits & 0x0800))
		ret = sprintf(buf, "%i\n", (int) stp_info.tto);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(time_offset);

static ssize_t time_zone_offset_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid() && (stp_info.vbits & 0x4000))
		ret = sprintf(buf, "%i\n", (int)(s16) stp_info.tzo);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(time_zone_offset);

static ssize_t timing_mode_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid())
		ret = sprintf(buf, "%i\n", stp_info.tmd);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(timing_mode);

static ssize_t timing_state_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t ret = -ENODATA;

	mutex_lock(&stp_work_mutex);
	if (stpinfo_valid())
		ret = sprintf(buf, "%i\n", stp_info.tst);
	mutex_unlock(&stp_work_mutex);
	return ret;
}

static DEVICE_ATTR_RO(timing_state);

static ssize_t online_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%i\n", stp_online);
}

static ssize_t online_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned int value;

	value = simple_strtoul(buf, NULL, 0);
	if (value != 0 && value != 1)
		return -EINVAL;
	if (!test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags))
		return -EOPNOTSUPP;
	mutex_lock(&clock_sync_mutex);
	stp_online = value;
	if (stp_online)
		set_bit(CLOCK_SYNC_STP, &clock_sync_flags);
	else
		clear_bit(CLOCK_SYNC_STP, &clock_sync_flags);
	queue_work(time_sync_wq, &stp_work);
	mutex_unlock(&clock_sync_mutex);
	return count;
}

/*
 * Can't use DEVICE_ATTR because the attribute should be named
 * stp/online but dev_attr_online already exists in this file ..
 */
static DEVICE_ATTR_RW(online);

static struct device_attribute *stp_attributes[] = {
	&dev_attr_ctn_id,
	&dev_attr_ctn_type,
	&dev_attr_dst_offset,
	&dev_attr_leap_seconds,
	&dev_attr_online,
	&dev_attr_stratum,
	&dev_attr_time_offset,
	&dev_attr_time_zone_offset,
	&dev_attr_timing_mode,
	&dev_attr_timing_state,
	NULL
};

static int __init stp_init_sysfs(void)
{
	struct device_attribute **attr;
	int rc;

	rc = subsys_system_register(&stp_subsys, NULL);
	if (rc)
		goto out;
	for (attr = stp_attributes; *attr; attr++) {
		rc = device_create_file(stp_subsys.dev_root, *attr);
		if (rc)
			goto out_unreg;
	}
	return 0;
out_unreg:
	for (; attr >= stp_attributes; attr--)
		device_remove_file(stp_subsys.dev_root, *attr);
	bus_unregister(&stp_subsys);
out:
	return rc;
}

device_initcall(stp_init_sysfs);
