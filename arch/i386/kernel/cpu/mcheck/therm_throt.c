/*
 * linux/arch/i386/kerne/cpu/mcheck/therm_throt.c
 *
 * Thermal throttle event support code.
 *
 * Author: Dmitriy Zavin (dmitriyz@google.com)
 *
 * Credits: Adapted from Zwane Mwaikambo's original code in mce_intel.c.
 *
 */

#include <linux/percpu.h>
#include <linux/cpu.h>
#include <asm/cpu.h>
#include <linux/notifier.h>
#include <asm/therm_throt.h>

/* How long to wait between reporting thermal events */
#define CHECK_INTERVAL              (300 * HZ)

static DEFINE_PER_CPU(unsigned long, next_check);

/***
 * therm_throt_process - Process thermal throttling event
 * @curr: Whether the condition is current or not (boolean), since the
 *        thermal interrupt normally gets called both when the thermal
 *        event begins and once the event has ended.
 *
 * This function is normally called by the thermal interrupt after the
 * IRQ has been acknowledged.
 *
 * It will take care of rate limiting and printing messages to the syslog.
 *
 * Returns: 0 : Event should NOT be further logged, i.e. still in
 *              "timeout" from previous log message.
 *          1 : Event should be logged further, and a message has been
 *              printed to the syslog.
 */
int therm_throt_process(int curr)
{
	unsigned int cpu = smp_processor_id();

	if (time_before(jiffies, __get_cpu_var(next_check)))
		return 0;

	__get_cpu_var(next_check) = jiffies + CHECK_INTERVAL;

	/* if we just entered the thermal event */
	if (curr) {
		printk(KERN_CRIT "CPU%d: Temperature above threshold, "
		       "cpu clock throttled\n", cpu);
		add_taint(TAINT_MACHINE_CHECK);
	} else {
		printk(KERN_CRIT "CPU%d: Temperature/speed normal\n", cpu);
	}

	return 1;
}
