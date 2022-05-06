// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/nmi.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>

#include "ifs.h"

/*
 * Note all code and data in this file is protected by
 * ifs_sem. On HT systems all threads on a core will
 * execute together, but only the first thread on the
 * core will update results of the test.
 */

/* Max retries on the same chunk */
#define MAX_IFS_RETRIES  5

/*
 * Number of TSC cycles that a logical CPU will wait for the other
 * logical CPU on the core in the WRMSR(ACTIVATE_SCAN).
 */
#define IFS_THREAD_WAIT 100000

enum ifs_status_err_code {
	IFS_NO_ERROR				= 0,
	IFS_OTHER_THREAD_COULD_NOT_JOIN		= 1,
	IFS_INTERRUPTED_BEFORE_RENDEZVOUS	= 2,
	IFS_POWER_MGMT_INADEQUATE_FOR_SCAN	= 3,
	IFS_INVALID_CHUNK_RANGE			= 4,
	IFS_MISMATCH_ARGUMENTS_BETWEEN_THREADS	= 5,
	IFS_CORE_NOT_CAPABLE_CURRENTLY		= 6,
	IFS_UNASSIGNED_ERROR_CODE		= 7,
	IFS_EXCEED_NUMBER_OF_THREADS_CONCURRENT	= 8,
	IFS_INTERRUPTED_DURING_EXECUTION	= 9,
};

static const char * const scan_test_status[] = {
	[IFS_NO_ERROR] = "SCAN no error",
	[IFS_OTHER_THREAD_COULD_NOT_JOIN] = "Other thread could not join.",
	[IFS_INTERRUPTED_BEFORE_RENDEZVOUS] = "Interrupt occurred prior to SCAN coordination.",
	[IFS_POWER_MGMT_INADEQUATE_FOR_SCAN] =
	"Core Abort SCAN Response due to power management condition.",
	[IFS_INVALID_CHUNK_RANGE] = "Non valid chunks in the range",
	[IFS_MISMATCH_ARGUMENTS_BETWEEN_THREADS] = "Mismatch in arguments between threads T0/T1.",
	[IFS_CORE_NOT_CAPABLE_CURRENTLY] = "Core not capable of performing SCAN currently",
	[IFS_UNASSIGNED_ERROR_CODE] = "Unassigned error code 0x7",
	[IFS_EXCEED_NUMBER_OF_THREADS_CONCURRENT] =
	"Exceeded number of Logical Processors (LP) allowed to run Scan-At-Field concurrently",
	[IFS_INTERRUPTED_DURING_EXECUTION] = "Interrupt occurred prior to SCAN start",
};

static void message_not_tested(struct device *dev, int cpu, union ifs_status status)
{
	if (status.error_code < ARRAY_SIZE(scan_test_status)) {
		dev_info(dev, "CPU(s) %*pbl: SCAN operation did not start. %s\n",
			 cpumask_pr_args(cpu_smt_mask(cpu)),
			 scan_test_status[status.error_code]);
	} else if (status.error_code == IFS_SW_TIMEOUT) {
		dev_info(dev, "CPU(s) %*pbl: software timeout during scan\n",
			 cpumask_pr_args(cpu_smt_mask(cpu)));
	} else if (status.error_code == IFS_SW_PARTIAL_COMPLETION) {
		dev_info(dev, "CPU(s) %*pbl: %s\n",
			 cpumask_pr_args(cpu_smt_mask(cpu)),
			 "Not all scan chunks were executed. Maximum forward progress retries exceeded");
	} else {
		dev_info(dev, "CPU(s) %*pbl: SCAN unknown status %llx\n",
			 cpumask_pr_args(cpu_smt_mask(cpu)), status.data);
	}
}

static void message_fail(struct device *dev, int cpu, union ifs_status status)
{
	/*
	 * control_error is set when the microcode runs into a problem
	 * loading the image from the reserved BIOS memory, or it has
	 * been corrupted. Reloading the image may fix this issue.
	 */
	if (status.control_error) {
		dev_err(dev, "CPU(s) %*pbl: could not execute from loaded scan image\n",
			cpumask_pr_args(cpu_smt_mask(cpu)));
	}

	/*
	 * signature_error is set when the output from the scan chains does not
	 * match the expected signature. This might be a transient problem (e.g.
	 * due to a bit flip from an alpha particle or neutron). If the problem
	 * repeats on a subsequent test, then it indicates an actual problem in
	 * the core being tested.
	 */
	if (status.signature_error) {
		dev_err(dev, "CPU(s) %*pbl: test signature incorrect.\n",
			cpumask_pr_args(cpu_smt_mask(cpu)));
	}
}

static bool can_restart(union ifs_status status)
{
	enum ifs_status_err_code err_code = status.error_code;

	/* Signature for chunk is bad, or scan test failed */
	if (status.signature_error || status.control_error)
		return false;

	switch (err_code) {
	case IFS_NO_ERROR:
	case IFS_OTHER_THREAD_COULD_NOT_JOIN:
	case IFS_INTERRUPTED_BEFORE_RENDEZVOUS:
	case IFS_POWER_MGMT_INADEQUATE_FOR_SCAN:
	case IFS_EXCEED_NUMBER_OF_THREADS_CONCURRENT:
	case IFS_INTERRUPTED_DURING_EXECUTION:
		return true;
	case IFS_INVALID_CHUNK_RANGE:
	case IFS_MISMATCH_ARGUMENTS_BETWEEN_THREADS:
	case IFS_CORE_NOT_CAPABLE_CURRENTLY:
	case IFS_UNASSIGNED_ERROR_CODE:
		break;
	}
	return false;
}

/*
 * Execute the scan. Called "simultaneously" on all threads of a core
 * at high priority using the stop_cpus mechanism.
 */
static int doscan(void *data)
{
	int cpu = smp_processor_id();
	u64 *msrs = data;
	int first;

	/* Only the first logical CPU on a core reports result */
	first = cpumask_first(cpu_smt_mask(cpu));

	/*
	 * This WRMSR will wait for other HT threads to also write
	 * to this MSR (at most for activate.delay cycles). Then it
	 * starts scan of each requested chunk. The core scan happens
	 * during the "execution" of the WRMSR. This instruction can
	 * take up to 200 milliseconds (in the case where all chunks
	 * are processed in a single pass) before it retires.
	 */
	wrmsrl(MSR_ACTIVATE_SCAN, msrs[0]);

	if (cpu == first) {
		/* Pass back the result of the scan */
		rdmsrl(MSR_SCAN_STATUS, msrs[1]);
	}

	return 0;
}

/*
 * Use stop_core_cpuslocked() to synchronize writing to MSR_ACTIVATE_SCAN
 * on all threads of the core to be tested. Loop if necessary to complete
 * run of all chunks. Include some defensive tests to make sure forward
 * progress is made, and that the whole test completes in a reasonable time.
 */
static void ifs_test_core(int cpu, struct device *dev)
{
	union ifs_scan activate;
	union ifs_status status;
	unsigned long timeout;
	struct ifs_data *ifsd;
	u64 msrvals[2];
	int retries;

	ifsd = ifs_get_data(dev);

	activate.rsvd = 0;
	activate.delay = IFS_THREAD_WAIT;
	activate.sigmce = 0;
	activate.start = 0;
	activate.stop = ifsd->valid_chunks - 1;

	timeout = jiffies + HZ / 2;
	retries = MAX_IFS_RETRIES;

	while (activate.start <= activate.stop) {
		if (time_after(jiffies, timeout)) {
			status.error_code = IFS_SW_TIMEOUT;
			break;
		}

		msrvals[0] = activate.data;
		stop_core_cpuslocked(cpu, doscan, msrvals);

		status.data = msrvals[1];

		/* Some cases can be retried, give up for others */
		if (!can_restart(status))
			break;

		if (status.chunk_num == activate.start) {
			/* Check for forward progress */
			if (--retries == 0) {
				if (status.error_code == IFS_NO_ERROR)
					status.error_code = IFS_SW_PARTIAL_COMPLETION;
				break;
			}
		} else {
			retries = MAX_IFS_RETRIES;
			activate.start = status.chunk_num;
		}
	}

	/* Update status for this core */
	ifsd->scan_details = status.data;

	if (status.control_error || status.signature_error) {
		ifsd->status = SCAN_TEST_FAIL;
		message_fail(dev, cpu, status);
	} else if (status.error_code) {
		ifsd->status = SCAN_NOT_TESTED;
		message_not_tested(dev, cpu, status);
	} else {
		ifsd->status = SCAN_TEST_PASS;
	}
}

/*
 * Initiate per core test. It wakes up work queue threads on the target cpu and
 * its sibling cpu. Once all sibling threads wake up, the scan test gets executed and
 * wait for all sibling threads to finish the scan test.
 */
int do_core_test(int cpu, struct device *dev)
{
	int ret = 0;

	/* Prevent CPUs from being taken offline during the scan test */
	cpus_read_lock();

	if (!cpu_online(cpu)) {
		dev_info(dev, "cannot test on the offline cpu %d\n", cpu);
		ret = -EINVAL;
		goto out;
	}

	ifs_test_core(cpu, dev);
out:
	cpus_read_unlock();
	return ret;
}
