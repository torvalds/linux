#ifndef __ASM_POWERPC_IMC_PMU_H
#define __ASM_POWERPC_IMC_PMU_H

/*
 * IMC Nest Performance Monitor counter support.
 *
 * Copyright (C) 2017 Madhavan Srinivasan, IBM Corporation.
 *           (C) 2017 Anju T Sudhakar, IBM Corporation.
 *           (C) 2017 Hemant K Shaw, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or later version.
 */

#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <asm/opal.h>

/*
 * For static allocation of some of the structures.
 */
#define IMC_MAX_PMUS			32

/*
 * Compatibility macros for IMC devices
 */
#define IMC_DTB_COMPAT			"ibm,opal-in-memory-counters"
#define IMC_DTB_UNIT_COMPAT		"ibm,imc-counters"


/*
 * LDBAR: Counter address and Enable/Disable macro.
 * perf/imc-pmu.c has the LDBAR layout information.
 */
#define THREAD_IMC_LDBAR_MASK           0x0003ffffffffe000ULL
#define THREAD_IMC_ENABLE               0x8000000000000000ULL

/*
 * Structure to hold memory address information for imc units.
 */
struct imc_mem_info {
	u64 *vbase;
	u32 id;
};

/*
 * Place holder for nest pmu events and values.
 */
struct imc_events {
	u32 value;
	char *name;
	char *unit;
	char *scale;
};

/* Event attribute array index */
#define IMC_FORMAT_ATTR		0
#define IMC_EVENT_ATTR		1
#define IMC_CPUMASK_ATTR	2
#define IMC_NULL_ATTR		3

/* PMU Format attribute macros */
#define IMC_EVENT_OFFSET_MASK	0xffffffffULL

/*
 * Device tree parser code detects IMC pmu support and
 * registers new IMC pmus. This structure will hold the
 * pmu functions, events, counter memory information
 * and attrs for each imc pmu and will be referenced at
 * the time of pmu registration.
 */
struct imc_pmu {
	struct pmu pmu;
	struct imc_mem_info *mem_info;
	struct imc_events **events;
	/*
	 * Attribute groups for the PMU. Slot 0 used for
	 * format attribute, slot 1 used for cpusmask attribute,
	 * slot 2 used for event attribute. Slot 3 keep as
	 * NULL.
	 */
	const struct attribute_group *attr_groups[4];
	u32 counter_mem_size;
	int domain;
	/*
	 * flag to notify whether the memory is mmaped
	 * or allocated by kernel.
	 */
	bool imc_counter_mmaped;
};

/*
 * Structure to hold id, lock and reference count for the imc events which
 * are inited.
 */
struct imc_pmu_ref {
	struct mutex lock;
	unsigned int id;
	int refc;
};

/*
 * In-Memory Collection Counters type.
 * Data comes from Device tree.
 * Three device type are supported.
 */

enum {
	IMC_TYPE_THREAD		= 0x1,
	IMC_TYPE_CORE		= 0x4,
	IMC_TYPE_CHIP           = 0x10,
};

/*
 * Domains for IMC PMUs
 */
#define IMC_DOMAIN_NEST		1
#define IMC_DOMAIN_CORE		2
#define IMC_DOMAIN_THREAD	3

extern int init_imc_pmu(struct device_node *parent,
				struct imc_pmu *pmu_ptr, int pmu_id);
extern void thread_imc_disable(void);
#endif /* __ASM_POWERPC_IMC_PMU_H */
