/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __ASM_POWERPC_IMC_PMU_H
#define __ASM_POWERPC_IMC_PMU_H

/*
 * IMC Nest Performance Monitor counter support.
 *
 * Copyright (C) 2017 Madhavan Srinivasan, IBM Corporation.
 *           (C) 2017 Anju T Sudhakar, IBM Corporation.
 *           (C) 2017 Hemant K Shaw, IBM Corporation.
 */

#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <asm/opal.h>

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
#define TRACE_IMC_ENABLE		0x4000000000000000ULL

/*
 * For debugfs interface for imc-mode and imc-command
 */
#define IMC_CNTL_BLK_OFFSET		0x3FC00
#define IMC_CNTL_BLK_CMD_OFFSET		8
#define IMC_CNTL_BLK_MODE_OFFSET	32

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

/*
 * Trace IMC hardware updates a 64bytes record on
 * Core Performance Monitoring Counter (CPMC)
 * overflow. Here is the layout for the trace imc record
 *
 * DW 0 : Timebase
 * DW 1 : Program Counter
 * DW 2 : PIDR information
 * DW 3 : CPMC1
 * DW 4 : CPMC2
 * DW 5 : CPMC3
 * Dw 6 : CPMC4
 * DW 7 : Timebase
 * .....
 *
 * The following is the data structure to hold trace imc data.
 */
struct trace_imc_data {
	__be64 tb1;
	__be64 ip;
	__be64 val;
	__be64 cpmc1;
	__be64 cpmc2;
	__be64 cpmc3;
	__be64 cpmc4;
	__be64 tb2;
};

/* Event attribute array index */
#define IMC_FORMAT_ATTR		0
#define IMC_EVENT_ATTR		1
#define IMC_CPUMASK_ATTR	2
#define IMC_NULL_ATTR		3

/* PMU Format attribute macros */
#define IMC_EVENT_OFFSET_MASK	0xffffffffULL

/*
 * Macro to mask bits 0:21 of first double word(which is the timebase) to
 * compare with 8th double word (timebase) of trace imc record data.
 */
#define IMC_TRACE_RECORD_TB1_MASK      0x3ffffffffffULL

/*
 * Bit 0:1 in third DW of IMC trace record
 * specifies the MSR[HV PR] values.
 */
#define IMC_TRACE_RECORD_VAL_HVPR(x)	((x) >> 62)

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
	struct imc_events *events;
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
	spinlock_t lock;
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
	IMC_TYPE_TRACE		= 0x2,
	IMC_TYPE_CORE		= 0x4,
	IMC_TYPE_CHIP           = 0x10,
};

/*
 * Domains for IMC PMUs
 */
#define IMC_DOMAIN_NEST		1
#define IMC_DOMAIN_CORE		2
#define IMC_DOMAIN_THREAD	3
/* For trace-imc the domain is still thread but it operates in trace-mode */
#define IMC_DOMAIN_TRACE	4

extern int init_imc_pmu(struct device_node *parent,
				struct imc_pmu *pmu_ptr, int pmu_id);
extern void thread_imc_disable(void);
extern int get_max_nest_dev(void);
extern void unregister_thread_imc(void);
#endif /* __ASM_POWERPC_IMC_PMU_H */
