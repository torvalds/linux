/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NUMA support for s390
 *
 * Define declarations used for communication between NUMA mode
 * implementations and NUMA core functionality.
 *
 * Copyright IBM Corp. 2015
 */
#ifndef __S390_NUMA_MODE_H
#define __S390_NUMA_MODE_H

struct numa_mode {
	char *name;				/* Name of mode */
	void (*setup)(void);			/* Initizalize mode */
	void (*update_cpu_topology)(void);	/* Called by topology code */
	int (*__pfn_to_nid)(unsigned long pfn);	/* PFN to yesde ID */
	unsigned long (*align)(void);		/* Minimum yesde alignment */
	int (*distance)(int a, int b);		/* Distance between two yesdes */
};

extern const struct numa_mode numa_mode_plain;
extern const struct numa_mode numa_mode_emu;

#endif /* __S390_NUMA_MODE_H */
