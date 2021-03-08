/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright IBM Corp. 2021
 * Interface implementation for communication with the CPU Measurement
 * counter facility device driver.
 *
 * Author(s): Thomas Richter <tmricht@linux.ibm.com>
 *
 * Define for ioctl() commands to communicate with the CPU Measurement
 * counter facility device driver.
 */

#ifndef _PERF_CPUM_CF_DIAG_H
#define _PERF_CPUM_CF_DIAG_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define S390_HWCTR_DEVICE		"hwctr"
#define S390_HWCTR_START_VERSION	1

struct s390_ctrset_start {		/* Set CPUs to operate on */
	__u64 version;			/* Version of interface */
	__u64 data_bytes;		/* # of bytes required */
	__u64 cpumask_len;		/* Length of CPU mask in bytes */
	__u64 *cpumask;			/* Pointer to CPU mask */
	__u64 counter_sets;		/* Bit mask of counter sets to get */
};

struct s390_ctrset_setdata {		/* Counter set data */
	__u32 set;			/* Counter set number */
	__u32 no_cnts;			/* # of counters stored in cv[] */
	__u64 cv[0];			/* Counter values (variable length) */
};

struct s390_ctrset_cpudata {		/* Counter set data per CPU */
	__u32 cpu_nr;			/* CPU number */
	__u32 no_sets;			/* # of counters sets in data[] */
	struct s390_ctrset_setdata data[0];
};

struct s390_ctrset_read {		/* Structure to get all ctr sets */
	__u64 no_cpus;			/* Total # of CPUs data taken from */
	struct s390_ctrset_cpudata data[0];
};

#define S390_HWCTR_MAGIC	'C'	/* Random magic # for ioctls */
#define	S390_HWCTR_START	_IOWR(S390_HWCTR_MAGIC, 1, struct s390_ctrset_start)
#define	S390_HWCTR_STOP		_IO(S390_HWCTR_MAGIC, 2)
#define	S390_HWCTR_READ		_IOWR(S390_HWCTR_MAGIC, 3, struct s390_ctrset_read)
#endif
