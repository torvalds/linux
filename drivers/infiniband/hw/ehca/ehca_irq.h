/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Function definitions and structs for EQs, NEQs and interrupts
 *
 *  Authors: Heiko J Schick <schickhj@de.ibm.com>
 *           Khadija Souissi <souissi@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __EHCA_IRQ_H
#define __EHCA_IRQ_H


struct ehca_shca;

#include <linux/interrupt.h>
#include <linux/types.h>

int ehca_error_data(struct ehca_shca *shca, void *data, u64 resource);

irqreturn_t ehca_interrupt_neq(int irq, void *dev_id);
void ehca_tasklet_neq(unsigned long data);

irqreturn_t ehca_interrupt_eq(int irq, void *dev_id);
void ehca_tasklet_eq(unsigned long data);
void ehca_process_eq(struct ehca_shca *shca, int is_irq);

struct ehca_cpu_comp_task {
	struct list_head cq_list;
	spinlock_t task_lock;
	int cq_jobs;
	int active;
};

struct ehca_comp_pool {
	struct ehca_cpu_comp_task __percpu *cpu_comp_tasks;
	struct task_struct * __percpu *cpu_comp_threads;
	int last_cpu;
	spinlock_t last_cpu_lock;
};

int ehca_create_comp_pool(void);
void ehca_destroy_comp_pool(void);

#endif
