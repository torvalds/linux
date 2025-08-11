/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#ifndef _IRQ_RISCV_IMSIC_STATE_H
#define _IRQ_RISCV_IMSIC_STATE_H

#include <linux/irqchip/riscv-imsic.h>
#include <linux/irqdomain.h>
#include <linux/fwnode.h>
#include <linux/timer.h>

#define IMSIC_IPI_ID				1
#define IMSIC_NR_IPI				8

struct imsic_vector {
	/* Fixed details of the vector */
	unsigned int				cpu;
	unsigned int				local_id;
	/* Details saved by driver in the vector */
	unsigned int				irq;
	/* Details accessed using local lock held */
	bool					enable;
	struct imsic_vector			*move_next;
	struct imsic_vector			*move_prev;
};

struct imsic_local_priv {
	/* Local lock to protect vector enable/move variables and dirty bitmap */
	raw_spinlock_t				lock;

	/* Local dirty bitmap for synchronization */
	unsigned long				*dirty_bitmap;

#ifdef CONFIG_SMP
	/* Local timer for synchronization */
	struct timer_list			timer;
#endif

	/* Local vector table */
	struct imsic_vector			*vectors;
};

struct imsic_priv {
	/* Device details */
	struct fwnode_handle			*fwnode;

	/* Global configuration common for all HARTs */
	struct imsic_global_config		global;

	/* Per-CPU state */
	struct imsic_local_priv __percpu	*lpriv;

	/* State of IRQ matrix allocator */
	raw_spinlock_t				matrix_lock;
	struct irq_matrix			*matrix;

	/* IRQ domains (created by platform driver) */
	struct irq_domain			*base_domain;
};

extern bool imsic_noipi;
extern struct imsic_priv *imsic;

void __imsic_eix_update(unsigned long base_id, unsigned long num_id, bool pend, bool val);

static inline void __imsic_id_set_enable(unsigned long id)
{
	__imsic_eix_update(id, 1, false, true);
}

static inline void __imsic_id_clear_enable(unsigned long id)
{
	__imsic_eix_update(id, 1, false, false);
}

void imsic_local_sync_all(bool force_all);
void imsic_local_delivery(bool enable);

void imsic_vector_mask(struct imsic_vector *vec);
void imsic_vector_unmask(struct imsic_vector *vec);

static inline bool imsic_vector_isenabled(struct imsic_vector *vec)
{
	return READ_ONCE(vec->enable);
}

static inline struct imsic_vector *imsic_vector_get_move(struct imsic_vector *vec)
{
	return READ_ONCE(vec->move_prev);
}

void imsic_vector_force_move_cleanup(struct imsic_vector *vec);
void imsic_vector_move(struct imsic_vector *old_vec, struct imsic_vector *new_vec);

struct imsic_vector *imsic_vector_from_local_id(unsigned int cpu, unsigned int local_id);

struct imsic_vector *imsic_vector_alloc(unsigned int irq, const struct cpumask *mask);
void imsic_vector_free(struct imsic_vector *vector);

void imsic_vector_debug_show(struct seq_file *m, struct imsic_vector *vec, int ind);
void imsic_vector_debug_show_summary(struct seq_file *m, int ind);

void imsic_state_online(void);
void imsic_state_offline(void);
int imsic_setup_state(struct fwnode_handle *fwnode, void *opaque);
int imsic_irqdomain_init(void);

#endif
