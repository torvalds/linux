/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Intel Corporation. */

#ifndef _IFS_H_
#define _IFS_H_

/**
 * DOC: In-Field Scan
 *
 * =============
 * In-Field Scan
 * =============
 *
 * Introduction
 * ------------
 *
 * In Field Scan (IFS) is a hardware feature to run circuit level tests on
 * a CPU core to detect problems that are not caught by parity or ECC checks.
 * Future CPUs will support more than one type of test which will show up
 * with a new platform-device instance-id, for now only .0 is exposed.
 *
 *
 * IFS Image
 * ---------
 *
 * Intel provides a firmware file containing the scan tests via
 * github [#f1]_.  Similar to microcode there is a separate file for each
 * family-model-stepping.
 *
 * IFS Image Loading
 * -----------------
 *
 * The driver loads the tests into memory reserved BIOS local to each CPU
 * socket in a two step process using writes to MSRs to first load the
 * SHA hashes for the test. Then the tests themselves. Status MSRs provide
 * feedback on the success/failure of these steps. When a new test file
 * is installed it can be loaded by writing to the driver reload file::
 *
 *   # echo 1 > /sys/devices/virtual/misc/intel_ifs_0/reload
 *
 * Similar to microcode, the current version of the scan tests is stored
 * in a fixed location: /lib/firmware/intel/ifs.0/family-model-stepping.scan
 *
 * Running tests
 * -------------
 *
 * Tests are run by the driver synchronizing execution of all threads on a
 * core and then writing to the ACTIVATE_SCAN MSR on all threads. Instruction
 * execution continues when:
 *
 * 1) All tests have completed.
 * 2) Execution was interrupted.
 * 3) A test detected a problem.
 *
 * Note that ALL THREADS ON THE CORE ARE EFFECTIVELY OFFLINE FOR THE
 * DURATION OF THE TEST. This can be up to 200 milliseconds. If the system
 * is running latency sensitive applications that cannot tolerate an
 * interruption of this magnitude, the system administrator must arrange
 * to migrate those applications to other cores before running a core test.
 * It may also be necessary to redirect interrupts to other CPUs.
 *
 * In all cases reading the SCAN_STATUS MSR provides details on what
 * happened. The driver makes the value of this MSR visible to applications
 * via the "details" file (see below). Interrupted tests may be restarted.
 *
 * The IFS driver provides sysfs interfaces via /sys/devices/virtual/misc/intel_ifs_0/
 * to control execution:
 *
 * Test a specific core::
 *
 *   # echo <cpu#> > /sys/devices/virtual/misc/intel_ifs_0/run_test
 *
 * when HT is enabled any of the sibling cpu# can be specified to test
 * its corresponding physical core. Since the tests are per physical core,
 * the result of testing any thread is same. All siblings must be online
 * to run a core test. It is only necessary to test one thread.
 *
 * For e.g. to test core corresponding to cpu5
 *
 *   # echo 5 > /sys/devices/virtual/misc/intel_ifs_0/run_test
 *
 * Results of the last test is provided in /sys::
 *
 *   $ cat /sys/devices/virtual/misc/intel_ifs_0/status
 *   pass
 *
 * Status can be one of pass, fail, untested
 *
 * Additional details of the last test is provided by the details file::
 *
 *   $ cat /sys/devices/virtual/misc/intel_ifs_0/details
 *   0x8081
 *
 * The details file reports the hex value of the SCAN_STATUS MSR.
 * Hardware defined error codes are documented in volume 4 of the Intel
 * Software Developer's Manual but the error_code field may contain one of
 * the following driver defined software codes:
 *
 * +------+--------------------+
 * | 0xFD | Software timeout   |
 * +------+--------------------+
 * | 0xFE | Partial completion |
 * +------+--------------------+
 *
 * Driver design choices
 * ---------------------
 *
 * 1) The ACTIVATE_SCAN MSR allows for running any consecutive subrange of
 * available tests. But the driver always tries to run all tests and only
 * uses the subrange feature to restart an interrupted test.
 *
 * 2) Hardware allows for some number of cores to be tested in parallel.
 * The driver does not make use of this, it only tests one core at a time.
 *
 * .. [#f1] https://github.com/intel/TBD
 */
#include <linux/device.h>
#include <linux/miscdevice.h>

#define MSR_COPY_SCAN_HASHES			0x000002c2
#define MSR_SCAN_HASHES_STATUS			0x000002c3
#define MSR_AUTHENTICATE_AND_COPY_CHUNK		0x000002c4
#define MSR_CHUNKS_AUTHENTICATION_STATUS	0x000002c5
#define MSR_ACTIVATE_SCAN			0x000002c6
#define MSR_SCAN_STATUS				0x000002c7
#define SCAN_NOT_TESTED				0
#define SCAN_TEST_PASS				1
#define SCAN_TEST_FAIL				2

/* MSR_SCAN_HASHES_STATUS bit fields */
union ifs_scan_hashes_status {
	u64	data;
	struct {
		u32	chunk_size	:16;
		u32	num_chunks	:8;
		u32	rsvd1		:8;
		u32	error_code	:8;
		u32	rsvd2		:11;
		u32	max_core_limit	:12;
		u32	valid		:1;
	};
};

/* MSR_CHUNKS_AUTH_STATUS bit fields */
union ifs_chunks_auth_status {
	u64	data;
	struct {
		u32	valid_chunks	:8;
		u32	total_chunks	:8;
		u32	rsvd1		:16;
		u32	error_code	:8;
		u32	rsvd2		:24;
	};
};

/* MSR_ACTIVATE_SCAN bit fields */
union ifs_scan {
	u64	data;
	struct {
		u32	start	:8;
		u32	stop	:8;
		u32	rsvd	:16;
		u32	delay	:31;
		u32	sigmce	:1;
	};
};

/* MSR_SCAN_STATUS bit fields */
union ifs_status {
	u64	data;
	struct {
		u32	chunk_num		:8;
		u32	chunk_stop_index	:8;
		u32	rsvd1			:16;
		u32	error_code		:8;
		u32	rsvd2			:22;
		u32	control_error		:1;
		u32	signature_error		:1;
	};
};

/*
 * Driver populated error-codes
 * 0xFD: Test timed out before completing all the chunks.
 * 0xFE: not all scan chunks were executed. Maximum forward progress retries exceeded.
 */
#define IFS_SW_TIMEOUT				0xFD
#define IFS_SW_PARTIAL_COMPLETION		0xFE

/**
 * struct ifs_data - attributes related to intel IFS driver
 * @integrity_cap_bit: MSR_INTEGRITY_CAPS bit enumerating this test
 * @loaded_version: stores the currently loaded ifs image version.
 * @loaded: If a valid test binary has been loaded into the memory
 * @loading_error: Error occurred on another CPU while loading image
 * @valid_chunks: number of chunks which could be validated.
 * @status: it holds simple status pass/fail/untested
 * @scan_details: opaque scan status code from h/w
 */
struct ifs_data {
	int	integrity_cap_bit;
	int	loaded_version;
	bool	loaded;
	bool	loading_error;
	int	valid_chunks;
	int	status;
	u64	scan_details;
};

struct ifs_work {
	struct work_struct w;
	struct device *dev;
};

struct ifs_device {
	struct ifs_data data;
	struct miscdevice misc;
};

static inline struct ifs_data *ifs_get_data(struct device *dev)
{
	struct miscdevice *m = dev_get_drvdata(dev);
	struct ifs_device *d = container_of(m, struct ifs_device, misc);

	return &d->data;
}

void ifs_load_firmware(struct device *dev);
int do_core_test(int cpu, struct device *dev);
const struct attribute_group **ifs_get_groups(void);

extern struct semaphore ifs_sem;

#endif
