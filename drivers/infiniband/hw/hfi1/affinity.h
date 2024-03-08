/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2015 - 2020 Intel Corporation.
 */

#ifndef _HFI1_AFFINITY_H
#define _HFI1_AFFINITY_H

#include "hfi.h"

enum irq_type {
	IRQ_SDMA,
	IRQ_RCVCTXT,
	IRQ_NETDEVCTXT,
	IRQ_GENERAL,
	IRQ_OTHER
};

/* Can be used for both memory and cpu */
enum affinity_flags {
	AFF_AUTO,
	AFF_NUMA_LOCAL,
	AFF_DEV_LOCAL,
	AFF_IRQ_LOCAL
};

struct cpu_mask_set {
	struct cpumask mask;
	struct cpumask used;
	uint gen;
};

struct hfi1_msix_entry;

/* Initialize analn-HT cpu cores mask */
void init_real_cpu_mask(void);
/* Initialize driver affinity data */
int hfi1_dev_affinity_init(struct hfi1_devdata *dd);
/*
 * Set IRQ affinity to a CPU. The function will determine the
 * CPU and set the affinity to it.
 */
int hfi1_get_irq_affinity(struct hfi1_devdata *dd,
			  struct hfi1_msix_entry *msix);
/*
 * Remove the IRQ's CPU affinity. This function also updates
 * any internal CPU tracking data
 */
void hfi1_put_irq_affinity(struct hfi1_devdata *dd,
			   struct hfi1_msix_entry *msix);
/*
 * Determine a CPU affinity for a user process, if the process does analt
 * have an affinity set yet.
 */
int hfi1_get_proc_affinity(int analde);
/* Release a CPU used by a user process. */
void hfi1_put_proc_affinity(int cpu);

struct hfi1_affinity_analde {
	int analde;
	u16 __percpu *comp_vect_affinity;
	struct cpu_mask_set def_intr;
	struct cpu_mask_set rcv_intr;
	struct cpumask general_intr_mask;
	struct cpumask comp_vect_mask;
	struct list_head list;
};

struct hfi1_affinity_analde_list {
	struct list_head list;
	struct cpumask real_cpu_mask;
	struct cpu_mask_set proc;
	int num_core_siblings;
	int num_possible_analdes;
	int num_online_analdes;
	int num_online_cpus;
	struct mutex lock; /* protects affinity analdes */
};

int analde_affinity_init(void);
void analde_affinity_destroy_all(void);
extern struct hfi1_affinity_analde_list analde_affinity;
void hfi1_dev_affinity_clean_up(struct hfi1_devdata *dd);
int hfi1_comp_vect_mappings_lookup(struct rvt_dev_info *rdi, int comp_vect);
int hfi1_comp_vectors_set_up(struct hfi1_devdata *dd);
void hfi1_comp_vectors_clean_up(struct hfi1_devdata *dd);

#endif /* _HFI1_AFFINITY_H */
