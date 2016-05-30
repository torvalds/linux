/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef _HFI1_AFFINITY_H
#define _HFI1_AFFINITY_H

#include "hfi.h"

enum irq_type {
	IRQ_SDMA,
	IRQ_RCVCTXT,
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

struct hfi1_affinity {
	struct cpu_mask_set def_intr;
	struct cpu_mask_set rcv_intr;
	struct cpu_mask_set proc;
	struct cpumask real_cpu_mask;
	/* spin lock to protect affinity struct */
	spinlock_t lock;
};

struct hfi1_msix_entry;

/* Initialize non-HT cpu cores mask */
int init_real_cpu_mask(struct hfi1_devdata *);
/* Initialize driver affinity data */
void hfi1_dev_affinity_init(struct hfi1_devdata *);
/* Free driver affinity data */
void hfi1_dev_affinity_free(struct hfi1_devdata *);
/*
 * Set IRQ affinity to a CPU. The function will determine the
 * CPU and set the affinity to it.
 */
int hfi1_get_irq_affinity(struct hfi1_devdata *, struct hfi1_msix_entry *);
/*
 * Remove the IRQ's CPU affinity. This function also updates
 * any internal CPU tracking data
 */
void hfi1_put_irq_affinity(struct hfi1_devdata *, struct hfi1_msix_entry *);
/*
 * Determine a CPU affinity for a user process, if the process does not
 * have an affinity set yet.
 */
int hfi1_get_proc_affinity(struct hfi1_devdata *, int);
/* Release a CPU used by a user process. */
void hfi1_put_proc_affinity(struct hfi1_devdata *, int);

#endif /* _HFI1_AFFINITY_H */
