// SPDX-License-Identifier: GPL-2.0
/*
 * arch/x86/kernel/nmi-selftest.c
 *
 * Testsuite for NMI: IPIs
 *
 * Started by Don Zickus:
 * (using lib/locking-selftest.c as a guide)
 *
 *   Copyright (C) 2011 Red Hat, Inc., Don Zickus <dzickus@redhat.com>
 */

#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/percpu.h>

#include <asm/apic.h>
#include <asm/nmi.h>

#define SUCCESS		0
#define FAILURE		1
#define TIMEOUT		2

static int __initdata nmi_fail;

/* check to see if NMI IPIs work on this machine */
static DECLARE_BITMAP(nmi_ipi_mask, NR_CPUS) __initdata;

static int __initdata testcase_total;
static int __initdata testcase_successes;
static int __initdata expected_testcase_failures;
static int __initdata unexpected_testcase_failures;
static int __initdata unexpected_testcase_unknowns;

static int __init nmi_unk_cb(unsigned int val, struct pt_regs *regs)
{
	unexpected_testcase_unknowns++;
	return NMI_HANDLED;
}

static void __init init_nmi_testsuite(void)
{
	/* trap all the unknown NMIs we may generate */
	register_nmi_handler(NMI_UNKNOWN, nmi_unk_cb, 0, "nmi_selftest_unk",
			__initdata);
}

static void __init cleanup_nmi_testsuite(void)
{
	unregister_nmi_handler(NMI_UNKNOWN, "nmi_selftest_unk");
}

static int __init test_nmi_ipi_callback(unsigned int val, struct pt_regs *regs)
{
        int cpu = raw_smp_processor_id();

        if (cpumask_test_and_clear_cpu(cpu, to_cpumask(nmi_ipi_mask)))
                return NMI_HANDLED;

        return NMI_DONE;
}

static void __init test_nmi_ipi(struct cpumask *mask)
{
	unsigned long timeout;

	if (register_nmi_handler(NMI_LOCAL, test_nmi_ipi_callback,
				 NMI_FLAG_FIRST, "nmi_selftest", __initdata)) {
		nmi_fail = FAILURE;
		return;
	}

	/* sync above data before sending NMI */
	wmb();

	apic->send_IPI_mask(mask, NMI_VECTOR);

	/* Don't wait longer than a second */
	timeout = USEC_PER_SEC;
	while (!cpumask_empty(mask) && --timeout)
	        udelay(1);

	/* What happens if we timeout, do we still unregister?? */
	unregister_nmi_handler(NMI_LOCAL, "nmi_selftest");

	if (!timeout)
		nmi_fail = TIMEOUT;
	return;
}

static void __init remote_ipi(void)
{
	cpumask_copy(to_cpumask(nmi_ipi_mask), cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), to_cpumask(nmi_ipi_mask));
	if (!cpumask_empty(to_cpumask(nmi_ipi_mask)))
		test_nmi_ipi(to_cpumask(nmi_ipi_mask));
}

static void __init local_ipi(void)
{
	cpumask_clear(to_cpumask(nmi_ipi_mask));
	cpumask_set_cpu(smp_processor_id(), to_cpumask(nmi_ipi_mask));
	test_nmi_ipi(to_cpumask(nmi_ipi_mask));
}

static void __init reset_nmi(void)
{
	nmi_fail = 0;
}

static void __init dotest(void (*testcase_fn)(void), int expected)
{
	testcase_fn();
	/*
	 * Filter out expected failures:
	 */
	if (nmi_fail != expected) {
		unexpected_testcase_failures++;

		if (nmi_fail == FAILURE)
			printk(KERN_CONT "FAILED |");
		else if (nmi_fail == TIMEOUT)
			printk(KERN_CONT "TIMEOUT|");
		else
			printk(KERN_CONT "ERROR  |");
		dump_stack();
	} else {
		testcase_successes++;
		printk(KERN_CONT "  ok  |");
	}
	testcase_total++;

	reset_nmi();
}

static inline void __init print_testname(const char *testname)
{
	printk("%12s:", testname);
}

void __init nmi_selftest(void)
{
	init_nmi_testsuite();

        /*
	 * Run the testsuite:
	 */
	printk("----------------\n");
	printk("| NMI testsuite:\n");
	printk("--------------------\n");

	print_testname("remote IPI");
	dotest(remote_ipi, SUCCESS);
	printk(KERN_CONT "\n");
	print_testname("local IPI");
	dotest(local_ipi, SUCCESS);
	printk(KERN_CONT "\n");

	cleanup_nmi_testsuite();

	if (unexpected_testcase_failures) {
		printk("--------------------\n");
		printk("BUG: %3d unexpected failures (out of %3d) - debugging disabled! |\n",
			unexpected_testcase_failures, testcase_total);
		printk("-----------------------------------------------------------------\n");
	} else if (expected_testcase_failures && testcase_successes) {
		printk("--------------------\n");
		printk("%3d out of %3d testcases failed, as expected. |\n",
			expected_testcase_failures, testcase_total);
		printk("----------------------------------------------------\n");
	} else if (expected_testcase_failures && !testcase_successes) {
		printk("--------------------\n");
		printk("All %3d testcases failed, as expected. |\n",
			expected_testcase_failures);
		printk("----------------------------------------\n");
	} else {
		printk("--------------------\n");
		printk("Good, all %3d testcases passed! |\n",
			testcase_successes);
		printk("---------------------------------\n");
	}
}
