// SPDX-License-Identifier: GPL-2.0
/*
 *  cpuidle-pseries - idle state cpuidle driver.
 *  Adapted from drivers/idle/intel_idle.c and
 *  drivers/acpi/processor_idle.c
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/cpuidle.h>
#include <linux/cpu.h>
#include <linux/notifier.h>

#include <asm/paca.h>
#include <asm/reg.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/runlatch.h>
#include <asm/idle.h>
#include <asm/plpar_wrappers.h>
#include <asm/rtas.h>

static struct cpuidle_driver pseries_idle_driver = {
	.name             = "pseries_idle",
	.owner            = THIS_MODULE,
};

static int max_idle_state __read_mostly;
static struct cpuidle_state *cpuidle_state_table __read_mostly;
static u64 snooze_timeout __read_mostly;
static bool snooze_timeout_en __read_mostly;

static int snooze_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	u64 snooze_exit_time;

	set_thread_flag(TIF_POLLING_NRFLAG);

	pseries_idle_prolog();
	local_irq_enable();
	snooze_exit_time = get_tb() + snooze_timeout;

	while (!need_resched()) {
		HMT_low();
		HMT_very_low();
		if (likely(snooze_timeout_en) && get_tb() > snooze_exit_time) {
			/*
			 * Task has not woken up but we are exiting the polling
			 * loop anyway. Require a barrier after polling is
			 * cleared to order subsequent test of need_resched().
			 */
			clear_thread_flag(TIF_POLLING_NRFLAG);
			smp_mb();
			break;
		}
	}

	HMT_medium();
	clear_thread_flag(TIF_POLLING_NRFLAG);

	local_irq_disable();

	pseries_idle_epilog();

	return index;
}

static void check_and_cede_processor(void)
{
	/*
	 * Ensure our interrupt state is properly tracked,
	 * also checks if no interrupt has occurred while we
	 * were soft-disabled
	 */
	if (prep_irq_for_idle()) {
		cede_processor();
#ifdef CONFIG_TRACE_IRQFLAGS
		/* Ensure that H_CEDE returns with IRQs on */
		if (WARN_ON(!(mfmsr() & MSR_EE)))
			__hard_irq_enable();
#endif
	}
}

/*
 * XCEDE: Extended CEDE states discovered through the
 *        "ibm,get-systems-parameter" RTAS call with the token
 *        CEDE_LATENCY_TOKEN
 */

/*
 * Section 7.3.16 System Parameters Option of PAPR version 2.8.1 has a
 * table with all the parameters to ibm,get-system-parameters.
 * CEDE_LATENCY_TOKEN corresponds to the token value for Cede Latency
 * Settings Information.
 */
#define CEDE_LATENCY_TOKEN	45

/*
 * If the platform supports the cede latency settings information system
 * parameter it must provide the following information in the NULL terminated
 * parameter string:
 *
 * a. The first byte is the length “N” of each cede latency setting record minus
 *    one (zero indicates a length of 1 byte).
 *
 * b. For each supported cede latency setting a cede latency setting record
 *    consisting of the first “N” bytes as per the following table.
 *
 *    -----------------------------
 *    | Field           | Field   |
 *    | Name            | Length  |
 *    -----------------------------
 *    | Cede Latency    | 1 Byte  |
 *    | Specifier Value |         |
 *    -----------------------------
 *    | Maximum wakeup  |         |
 *    | latency in      | 8 Bytes |
 *    | tb-ticks        |         |
 *    -----------------------------
 *    | Responsive to   |         |
 *    | external        | 1 Byte  |
 *    | interrupts      |         |
 *    -----------------------------
 *
 * This version has cede latency record size = 10.
 *
 * The structure xcede_latency_payload represents a) and b) with
 * xcede_latency_record representing the table in b).
 *
 * xcede_latency_parameter is what gets returned by
 * ibm,get-systems-parameter RTAS call when made with
 * CEDE_LATENCY_TOKEN.
 *
 * These structures are only used to represent the data obtained by the RTAS
 * call. The data is in big-endian.
 */
struct xcede_latency_record {
	u8	hint;
	__be64	latency_ticks;
	u8	wake_on_irqs;
} __packed;

// Make space for 16 records, which "should be enough".
struct xcede_latency_payload {
	u8     record_size;
	struct xcede_latency_record records[16];
} __packed;

struct xcede_latency_parameter {
	__be16  payload_size;
	struct xcede_latency_payload payload;
	u8 null_char;
} __packed;

static unsigned int nr_xcede_records;
static struct xcede_latency_parameter xcede_latency_parameter __initdata;

static int __init parse_cede_parameters(void)
{
	struct xcede_latency_payload *payload;
	u32 total_xcede_records_size;
	u8 xcede_record_size;
	u16 payload_size;
	int ret, i;

	ret = rtas_call(rtas_token("ibm,get-system-parameter"), 3, 1,
			NULL, CEDE_LATENCY_TOKEN, __pa(&xcede_latency_parameter),
			sizeof(xcede_latency_parameter));
	if (ret) {
		pr_err("xcede: Error parsing CEDE_LATENCY_TOKEN\n");
		return ret;
	}

	payload_size = be16_to_cpu(xcede_latency_parameter.payload_size);
	payload = &xcede_latency_parameter.payload;

	xcede_record_size = payload->record_size + 1;

	if (xcede_record_size != sizeof(struct xcede_latency_record)) {
		pr_err("xcede: Expected record-size %lu. Observed size %u.\n",
		       sizeof(struct xcede_latency_record), xcede_record_size);
		return -EINVAL;
	}

	pr_info("xcede: xcede_record_size = %d\n", xcede_record_size);

	/*
	 * Since the payload_size includes the last NULL byte and the
	 * xcede_record_size, the remaining bytes correspond to array of all
	 * cede_latency settings.
	 */
	total_xcede_records_size = payload_size - 2;
	nr_xcede_records = total_xcede_records_size / xcede_record_size;

	for (i = 0; i < nr_xcede_records; i++) {
		struct xcede_latency_record *record = &payload->records[i];
		u64 latency_ticks = be64_to_cpu(record->latency_ticks);
		u8 wake_on_irqs = record->wake_on_irqs;
		u8 hint = record->hint;

		pr_info("xcede: Record %d : hint = %u, latency = 0x%llx tb ticks, Wake-on-irq = %u\n",
			i, hint, latency_ticks, wake_on_irqs);
	}

	return 0;
}

#define NR_DEDICATED_STATES	2 /* snooze, CEDE */
static u8 cede_latency_hint[NR_DEDICATED_STATES];

static int dedicated_cede_loop(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	u8 old_latency_hint;

	pseries_idle_prolog();
	get_lppaca()->donate_dedicated_cpu = 1;
	old_latency_hint = get_lppaca()->cede_latency_hint;
	get_lppaca()->cede_latency_hint = cede_latency_hint[index];

	HMT_medium();
	check_and_cede_processor();

	local_irq_disable();
	get_lppaca()->donate_dedicated_cpu = 0;
	get_lppaca()->cede_latency_hint = old_latency_hint;

	pseries_idle_epilog();

	return index;
}

static int shared_cede_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{

	pseries_idle_prolog();

	/*
	 * Yield the processor to the hypervisor.  We return if
	 * an external interrupt occurs (which are driven prior
	 * to returning here) or if a prod occurs from another
	 * processor. When returning here, external interrupts
	 * are enabled.
	 */
	check_and_cede_processor();

	local_irq_disable();
	pseries_idle_epilog();

	return index;
}

/*
 * States for dedicated partition case.
 */
static struct cpuidle_state dedicated_states[NR_DEDICATED_STATES] = {
	{ /* Snooze */
		.name = "snooze",
		.desc = "snooze",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = &snooze_loop },
	{ /* CEDE */
		.name = "CEDE",
		.desc = "CEDE",
		.exit_latency = 10,
		.target_residency = 100,
		.enter = &dedicated_cede_loop },
};

/*
 * States for shared partition case.
 */
static struct cpuidle_state shared_states[] = {
	{ /* Snooze */
		.name = "snooze",
		.desc = "snooze",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = &snooze_loop },
	{ /* Shared Cede */
		.name = "Shared Cede",
		.desc = "Shared Cede",
		.exit_latency = 10,
		.target_residency = 100,
		.enter = &shared_cede_loop },
};

static int pseries_cpuidle_cpu_online(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	if (dev && cpuidle_get_driver()) {
		cpuidle_pause_and_lock();
		cpuidle_enable_device(dev);
		cpuidle_resume_and_unlock();
	}
	return 0;
}

static int pseries_cpuidle_cpu_dead(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	if (dev && cpuidle_get_driver()) {
		cpuidle_pause_and_lock();
		cpuidle_disable_device(dev);
		cpuidle_resume_and_unlock();
	}
	return 0;
}

/*
 * pseries_cpuidle_driver_init()
 */
static int pseries_cpuidle_driver_init(void)
{
	int idle_state;
	struct cpuidle_driver *drv = &pseries_idle_driver;

	drv->state_count = 0;

	for (idle_state = 0; idle_state < max_idle_state; ++idle_state) {
		/* Is the state not enabled? */
		if (cpuidle_state_table[idle_state].enter == NULL)
			continue;

		drv->states[drv->state_count] =	/* structure copy */
			cpuidle_state_table[idle_state];

		drv->state_count += 1;
	}

	return 0;
}

static void __init fixup_cede0_latency(void)
{
	struct xcede_latency_payload *payload;
	u64 min_latency_us;
	int i;

	min_latency_us = dedicated_states[1].exit_latency; // CEDE latency

	if (parse_cede_parameters())
		return;

	pr_info("cpuidle: Skipping the %d Extended CEDE idle states\n",
		nr_xcede_records);

	payload = &xcede_latency_parameter.payload;
	for (i = 0; i < nr_xcede_records; i++) {
		struct xcede_latency_record *record = &payload->records[i];
		u64 latency_tb = be64_to_cpu(record->latency_ticks);
		u64 latency_us = DIV_ROUND_UP_ULL(tb_to_ns(latency_tb), NSEC_PER_USEC);

		if (latency_us == 0)
			pr_warn("cpuidle: xcede record %d has an unrealistic latency of 0us.\n", i);

		if (latency_us < min_latency_us)
			min_latency_us = latency_us;
	}

	/*
	 * By default, we assume that CEDE(0) has exit latency 10us,
	 * since there is no way for us to query from the platform.
	 *
	 * However, if the wakeup latency of an Extended CEDE state is
	 * smaller than 10us, then we can be sure that CEDE(0)
	 * requires no more than that.
	 *
	 * Perform the fix-up.
	 */
	if (min_latency_us < dedicated_states[1].exit_latency) {
		/*
		 * We set a minimum of 1us wakeup latency for cede0 to
		 * distinguish it from snooze
		 */
		u64 cede0_latency = 1;

		if (min_latency_us > cede0_latency)
			cede0_latency = min_latency_us - 1;

		dedicated_states[1].exit_latency = cede0_latency;
		dedicated_states[1].target_residency = 10 * (cede0_latency);
		pr_info("cpuidle: Fixed up CEDE exit latency to %llu us\n",
			cede0_latency);
	}

}

/*
 * pseries_idle_probe()
 * Choose state table for shared versus dedicated partition
 */
static int pseries_idle_probe(void)
{

	if (cpuidle_disable != IDLE_NO_OVERRIDE)
		return -ENODEV;

	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		/*
		 * Use local_paca instead of get_lppaca() since
		 * preemption is not disabled, and it is not required in
		 * fact, since lppaca_ptr does not need to be the value
		 * associated to the current CPU, it can be from any CPU.
		 */
		if (lppaca_shared_proc(local_paca->lppaca_ptr)) {
			cpuidle_state_table = shared_states;
			max_idle_state = ARRAY_SIZE(shared_states);
		} else {
			/*
			 * Use firmware provided latency values
			 * starting with POWER10 platforms. In the
			 * case that we are running on a POWER10
			 * platform but in an earlier compat mode, we
			 * can still use the firmware provided values.
			 *
			 * However, on platforms prior to POWER10, we
			 * cannot rely on the accuracy of the firmware
			 * provided latency values. On such platforms,
			 * go with the conservative default estimate
			 * of 10us.
			 */
			if (cpu_has_feature(CPU_FTR_ARCH_31) || pvr_version_is(PVR_POWER10))
				fixup_cede0_latency();
			cpuidle_state_table = dedicated_states;
			max_idle_state = NR_DEDICATED_STATES;
		}
	} else
		return -ENODEV;

	if (max_idle_state > 1) {
		snooze_timeout_en = true;
		snooze_timeout = cpuidle_state_table[1].target_residency *
				 tb_ticks_per_usec;
	}
	return 0;
}

static int __init pseries_processor_idle_init(void)
{
	int retval;

	retval = pseries_idle_probe();
	if (retval)
		return retval;

	pseries_cpuidle_driver_init();
	retval = cpuidle_register(&pseries_idle_driver, NULL);
	if (retval) {
		printk(KERN_DEBUG "Registration of pseries driver failed.\n");
		return retval;
	}

	retval = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					   "cpuidle/pseries:online",
					   pseries_cpuidle_cpu_online, NULL);
	WARN_ON(retval < 0);
	retval = cpuhp_setup_state_nocalls(CPUHP_CPUIDLE_DEAD,
					   "cpuidle/pseries:DEAD", NULL,
					   pseries_cpuidle_cpu_dead);
	WARN_ON(retval < 0);
	printk(KERN_DEBUG "pseries_idle_driver registered\n");
	return 0;
}

device_initcall(pseries_processor_idle_init);
