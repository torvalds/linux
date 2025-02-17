// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv-imsic: " fmt
#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/bitmap.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <asm/hwcap.h>

#include "irq-riscv-imsic-state.h"

#define IMSIC_DISABLE_EIDELIVERY		0
#define IMSIC_ENABLE_EIDELIVERY			1
#define IMSIC_DISABLE_EITHRESHOLD		1
#define IMSIC_ENABLE_EITHRESHOLD		0

static inline void imsic_csr_write(unsigned long reg, unsigned long val)
{
	csr_write(CSR_ISELECT, reg);
	csr_write(CSR_IREG, val);
}

static inline unsigned long imsic_csr_read(unsigned long reg)
{
	csr_write(CSR_ISELECT, reg);
	return csr_read(CSR_IREG);
}

static inline unsigned long imsic_csr_read_clear(unsigned long reg, unsigned long val)
{
	csr_write(CSR_ISELECT, reg);
	return csr_read_clear(CSR_IREG, val);
}

static inline void imsic_csr_set(unsigned long reg, unsigned long val)
{
	csr_write(CSR_ISELECT, reg);
	csr_set(CSR_IREG, val);
}

static inline void imsic_csr_clear(unsigned long reg, unsigned long val)
{
	csr_write(CSR_ISELECT, reg);
	csr_clear(CSR_IREG, val);
}

struct imsic_priv *imsic;

const struct imsic_global_config *imsic_get_global_config(void)
{
	return imsic ? &imsic->global : NULL;
}
EXPORT_SYMBOL_GPL(imsic_get_global_config);

static bool __imsic_eix_read_clear(unsigned long id, bool pend)
{
	unsigned long isel, imask;

	isel = id / BITS_PER_LONG;
	isel *= BITS_PER_LONG / IMSIC_EIPx_BITS;
	isel += pend ? IMSIC_EIP0 : IMSIC_EIE0;
	imask = BIT(id & (__riscv_xlen - 1));

	return !!(imsic_csr_read_clear(isel, imask) & imask);
}

static inline bool __imsic_id_read_clear_enabled(unsigned long id)
{
	return __imsic_eix_read_clear(id, false);
}

static inline bool __imsic_id_read_clear_pending(unsigned long id)
{
	return __imsic_eix_read_clear(id, true);
}

void __imsic_eix_update(unsigned long base_id, unsigned long num_id, bool pend, bool val)
{
	unsigned long id = base_id, last_id = base_id + num_id;
	unsigned long i, isel, ireg;

	while (id < last_id) {
		isel = id / BITS_PER_LONG;
		isel *= BITS_PER_LONG / IMSIC_EIPx_BITS;
		isel += pend ? IMSIC_EIP0 : IMSIC_EIE0;

		/*
		 * Prepare the ID mask to be programmed in the
		 * IMSIC EIEx and EIPx registers. These registers
		 * are XLEN-wide and we must not touch IDs which
		 * are < base_id and >= (base_id + num_id).
		 */
		ireg = 0;
		for (i = id & (__riscv_xlen - 1); id < last_id && i < __riscv_xlen; i++) {
			ireg |= BIT(i);
			id++;
		}

		/*
		 * The IMSIC EIEx and EIPx registers are indirectly
		 * accessed via using ISELECT and IREG CSRs so we
		 * need to access these CSRs without getting preempted.
		 *
		 * All existing users of this function call this
		 * function with local IRQs disabled so we don't
		 * need to do anything special here.
		 */
		if (val)
			imsic_csr_set(isel, ireg);
		else
			imsic_csr_clear(isel, ireg);
	}
}

static bool __imsic_local_sync(struct imsic_local_priv *lpriv)
{
	struct imsic_local_config *tlocal, *mlocal;
	struct imsic_vector *vec, *tvec, *mvec;
	bool ret = true;
	int i;

	lockdep_assert_held(&lpriv->lock);

	for_each_set_bit(i, lpriv->dirty_bitmap, imsic->global.nr_ids + 1) {
		if (!i || i == IMSIC_IPI_ID)
			goto skip;
		vec = &lpriv->vectors[i];

		if (READ_ONCE(vec->enable))
			__imsic_id_set_enable(i);
		else
			__imsic_id_clear_enable(i);

		/*
		 * Clear the previous vector pointer of the new vector only
		 * after the movement is complete on the old CPU.
		 */
		mvec = READ_ONCE(vec->move_prev);
		if (mvec) {
			/*
			 * If the old vector has not been updated then
			 * try again in the next sync-up call.
			 */
			if (READ_ONCE(mvec->move_next)) {
				ret = false;
				continue;
			}

			WRITE_ONCE(vec->move_prev, NULL);
		}

		/*
		 * If a vector was being moved to a new vector on some other
		 * CPU then we can get a MSI during the movement so check the
		 * ID pending bit and re-trigger the new ID on other CPU using
		 * MMIO write.
		 */
		mvec = READ_ONCE(vec->move_next);
		if (mvec) {
			/*
			 * Devices having non-atomic MSI update might see
			 * an intermediate state so check both old ID and
			 * new ID for pending interrupts.
			 *
			 * For details, see imsic_irq_set_affinity().
			 */
			tvec = vec->local_id == mvec->local_id ?
				NULL : &lpriv->vectors[mvec->local_id];

			if (tvec && !irq_can_move_in_process_context(irq_get_irq_data(vec->irq)) &&
			    __imsic_id_read_clear_pending(tvec->local_id)) {
				/* Retrigger temporary vector if it was already in-use */
				if (READ_ONCE(tvec->enable)) {
					tlocal = per_cpu_ptr(imsic->global.local, tvec->cpu);
					writel_relaxed(tvec->local_id, tlocal->msi_va);
				}

				mlocal = per_cpu_ptr(imsic->global.local, mvec->cpu);
				writel_relaxed(mvec->local_id, mlocal->msi_va);
			}

			if (__imsic_id_read_clear_pending(vec->local_id)) {
				mlocal = per_cpu_ptr(imsic->global.local, mvec->cpu);
				writel_relaxed(mvec->local_id, mlocal->msi_va);
			}

			WRITE_ONCE(vec->move_next, NULL);
			imsic_vector_free(vec);
		}

skip:
		bitmap_clear(lpriv->dirty_bitmap, i, 1);
	}

	return ret;
}

#ifdef CONFIG_SMP
static void __imsic_local_timer_start(struct imsic_local_priv *lpriv)
{
	lockdep_assert_held(&lpriv->lock);

	if (!timer_pending(&lpriv->timer)) {
		lpriv->timer.expires = jiffies + 1;
		add_timer_on(&lpriv->timer, smp_processor_id());
	}
}
#else
static inline void __imsic_local_timer_start(struct imsic_local_priv *lpriv)
{
}
#endif

void imsic_local_sync_all(bool force_all)
{
	struct imsic_local_priv *lpriv = this_cpu_ptr(imsic->lpriv);
	unsigned long flags;

	raw_spin_lock_irqsave(&lpriv->lock, flags);

	if (force_all)
		bitmap_fill(lpriv->dirty_bitmap, imsic->global.nr_ids + 1);
	if (!__imsic_local_sync(lpriv))
		__imsic_local_timer_start(lpriv);

	raw_spin_unlock_irqrestore(&lpriv->lock, flags);
}

void imsic_local_delivery(bool enable)
{
	if (enable) {
		imsic_csr_write(IMSIC_EITHRESHOLD, IMSIC_ENABLE_EITHRESHOLD);
		imsic_csr_write(IMSIC_EIDELIVERY, IMSIC_ENABLE_EIDELIVERY);
		return;
	}

	imsic_csr_write(IMSIC_EIDELIVERY, IMSIC_DISABLE_EIDELIVERY);
	imsic_csr_write(IMSIC_EITHRESHOLD, IMSIC_DISABLE_EITHRESHOLD);
}

#ifdef CONFIG_SMP
static void imsic_local_timer_callback(struct timer_list *timer)
{
	imsic_local_sync_all(false);
}

static void __imsic_remote_sync(struct imsic_local_priv *lpriv, unsigned int cpu)
{
	lockdep_assert_held(&lpriv->lock);

	/*
	 * The spinlock acquire/release semantics ensure that changes
	 * to vector enable, vector move and dirty bitmap are visible
	 * to the target CPU.
	 */

	/*
	 * We schedule a timer on the target CPU if the target CPU is not
	 * same as the current CPU. An offline CPU will unconditionally
	 * synchronize IDs through imsic_starting_cpu() when the
	 * CPU is brought up.
	 */
	if (cpu_online(cpu)) {
		if (cpu == smp_processor_id()) {
			if (__imsic_local_sync(lpriv))
				return;
		}

		__imsic_local_timer_start(lpriv);
	}
}
#else
static void __imsic_remote_sync(struct imsic_local_priv *lpriv, unsigned int cpu)
{
	lockdep_assert_held(&lpriv->lock);
	__imsic_local_sync(lpriv);
}
#endif

void imsic_vector_mask(struct imsic_vector *vec)
{
	struct imsic_local_priv *lpriv;

	lpriv = per_cpu_ptr(imsic->lpriv, vec->cpu);
	if (WARN_ON_ONCE(&lpriv->vectors[vec->local_id] != vec))
		return;

	/*
	 * This function is called through Linux irq subsystem with
	 * irqs disabled so no need to save/restore irq flags.
	 */

	raw_spin_lock(&lpriv->lock);

	WRITE_ONCE(vec->enable, false);
	bitmap_set(lpriv->dirty_bitmap, vec->local_id, 1);
	__imsic_remote_sync(lpriv, vec->cpu);

	raw_spin_unlock(&lpriv->lock);
}

void imsic_vector_unmask(struct imsic_vector *vec)
{
	struct imsic_local_priv *lpriv;

	lpriv = per_cpu_ptr(imsic->lpriv, vec->cpu);
	if (WARN_ON_ONCE(&lpriv->vectors[vec->local_id] != vec))
		return;

	/*
	 * This function is called through Linux irq subsystem with
	 * irqs disabled so no need to save/restore irq flags.
	 */

	raw_spin_lock(&lpriv->lock);

	WRITE_ONCE(vec->enable, true);
	bitmap_set(lpriv->dirty_bitmap, vec->local_id, 1);
	__imsic_remote_sync(lpriv, vec->cpu);

	raw_spin_unlock(&lpriv->lock);
}

void imsic_vector_force_move_cleanup(struct imsic_vector *vec)
{
	struct imsic_local_priv *lpriv;
	struct imsic_vector *mvec;
	unsigned long flags;

	lpriv = per_cpu_ptr(imsic->lpriv, vec->cpu);
	raw_spin_lock_irqsave(&lpriv->lock, flags);

	mvec = READ_ONCE(vec->move_prev);
	WRITE_ONCE(vec->move_prev, NULL);
	if (mvec)
		imsic_vector_free(mvec);

	raw_spin_unlock_irqrestore(&lpriv->lock, flags);
}

static bool imsic_vector_move_update(struct imsic_local_priv *lpriv,
				     struct imsic_vector *vec, bool is_old_vec,
				     bool new_enable, struct imsic_vector *move_vec)
{
	unsigned long flags;
	bool enabled;

	raw_spin_lock_irqsave(&lpriv->lock, flags);

	/* Update enable and move details */
	enabled = READ_ONCE(vec->enable);
	WRITE_ONCE(vec->enable, new_enable);
	if (is_old_vec)
		WRITE_ONCE(vec->move_next, move_vec);
	else
		WRITE_ONCE(vec->move_prev, move_vec);

	/* Mark the vector as dirty and synchronize */
	bitmap_set(lpriv->dirty_bitmap, vec->local_id, 1);
	__imsic_remote_sync(lpriv, vec->cpu);

	raw_spin_unlock_irqrestore(&lpriv->lock, flags);

	return enabled;
}

void imsic_vector_move(struct imsic_vector *old_vec, struct imsic_vector *new_vec)
{
	struct imsic_local_priv *old_lpriv, *new_lpriv;
	bool enabled;

	if (WARN_ON_ONCE(old_vec->cpu == new_vec->cpu))
		return;

	old_lpriv = per_cpu_ptr(imsic->lpriv, old_vec->cpu);
	if (WARN_ON_ONCE(&old_lpriv->vectors[old_vec->local_id] != old_vec))
		return;

	new_lpriv = per_cpu_ptr(imsic->lpriv, new_vec->cpu);
	if (WARN_ON_ONCE(&new_lpriv->vectors[new_vec->local_id] != new_vec))
		return;

	/*
	 * Move and re-trigger the new vector based on the pending
	 * state of the old vector because we might get a device
	 * interrupt on the old vector while device was being moved
	 * to the new vector.
	 */
	enabled = imsic_vector_move_update(old_lpriv, old_vec, true, false, new_vec);
	imsic_vector_move_update(new_lpriv, new_vec, false, enabled, old_vec);
}

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
void imsic_vector_debug_show(struct seq_file *m, struct imsic_vector *vec, int ind)
{
	struct imsic_local_priv *lpriv;
	struct imsic_vector *mvec;
	bool is_enabled;

	lpriv = per_cpu_ptr(imsic->lpriv, vec->cpu);
	if (WARN_ON_ONCE(&lpriv->vectors[vec->local_id] != vec))
		return;

	is_enabled = imsic_vector_isenabled(vec);
	mvec = imsic_vector_get_move(vec);

	seq_printf(m, "%*starget_cpu      : %5u\n", ind, "", vec->cpu);
	seq_printf(m, "%*starget_local_id : %5u\n", ind, "", vec->local_id);
	seq_printf(m, "%*sis_reserved     : %5u\n", ind, "",
		   (vec->local_id <= IMSIC_IPI_ID) ? 1 : 0);
	seq_printf(m, "%*sis_enabled      : %5u\n", ind, "", is_enabled ? 1 : 0);
	seq_printf(m, "%*sis_move_pending : %5u\n", ind, "", mvec ? 1 : 0);
	if (mvec) {
		seq_printf(m, "%*smove_cpu        : %5u\n", ind, "", mvec->cpu);
		seq_printf(m, "%*smove_local_id   : %5u\n", ind, "", mvec->local_id);
	}
}

void imsic_vector_debug_show_summary(struct seq_file *m, int ind)
{
	irq_matrix_debug_show(m, imsic->matrix, ind);
}
#endif

struct imsic_vector *imsic_vector_from_local_id(unsigned int cpu, unsigned int local_id)
{
	struct imsic_local_priv *lpriv = per_cpu_ptr(imsic->lpriv, cpu);

	if (!lpriv || imsic->global.nr_ids < local_id)
		return NULL;

	return &lpriv->vectors[local_id];
}

struct imsic_vector *imsic_vector_alloc(unsigned int irq, const struct cpumask *mask)
{
	struct imsic_vector *vec = NULL;
	struct imsic_local_priv *lpriv;
	unsigned long flags;
	unsigned int cpu;
	int local_id;

	raw_spin_lock_irqsave(&imsic->matrix_lock, flags);
	local_id = irq_matrix_alloc(imsic->matrix, mask, false, &cpu);
	raw_spin_unlock_irqrestore(&imsic->matrix_lock, flags);
	if (local_id < 0)
		return NULL;

	lpriv = per_cpu_ptr(imsic->lpriv, cpu);
	vec = &lpriv->vectors[local_id];
	vec->irq = irq;
	vec->enable = false;
	vec->move_next = NULL;
	vec->move_prev = NULL;

	return vec;
}

void imsic_vector_free(struct imsic_vector *vec)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&imsic->matrix_lock, flags);
	vec->irq = 0;
	irq_matrix_free(imsic->matrix, vec->cpu, vec->local_id, false);
	raw_spin_unlock_irqrestore(&imsic->matrix_lock, flags);
}

static void __init imsic_local_cleanup(void)
{
	struct imsic_local_priv *lpriv;
	int cpu;

	for_each_possible_cpu(cpu) {
		lpriv = per_cpu_ptr(imsic->lpriv, cpu);

		bitmap_free(lpriv->dirty_bitmap);
		kfree(lpriv->vectors);
	}

	free_percpu(imsic->lpriv);
}

static int __init imsic_local_init(void)
{
	struct imsic_global_config *global = &imsic->global;
	struct imsic_local_priv *lpriv;
	struct imsic_vector *vec;
	int cpu, i;

	/* Allocate per-CPU private state */
	imsic->lpriv = alloc_percpu(typeof(*imsic->lpriv));
	if (!imsic->lpriv)
		return -ENOMEM;

	/* Setup per-CPU private state */
	for_each_possible_cpu(cpu) {
		lpriv = per_cpu_ptr(imsic->lpriv, cpu);

		raw_spin_lock_init(&lpriv->lock);

		/* Allocate dirty bitmap */
		lpriv->dirty_bitmap = bitmap_zalloc(global->nr_ids + 1, GFP_KERNEL);
		if (!lpriv->dirty_bitmap)
			goto fail_local_cleanup;

#ifdef CONFIG_SMP
		/* Setup lazy timer for synchronization */
		timer_setup(&lpriv->timer, imsic_local_timer_callback, TIMER_PINNED);
#endif

		/* Allocate vector array */
		lpriv->vectors = kcalloc(global->nr_ids + 1, sizeof(*lpriv->vectors),
					 GFP_KERNEL);
		if (!lpriv->vectors)
			goto fail_local_cleanup;

		/* Setup vector array */
		for (i = 0; i <= global->nr_ids; i++) {
			vec = &lpriv->vectors[i];
			vec->cpu = cpu;
			vec->local_id = i;
			vec->irq = 0;
		}
	}

	return 0;

fail_local_cleanup:
	imsic_local_cleanup();
	return -ENOMEM;
}

void imsic_state_online(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&imsic->matrix_lock, flags);
	irq_matrix_online(imsic->matrix);
	raw_spin_unlock_irqrestore(&imsic->matrix_lock, flags);
}

void imsic_state_offline(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&imsic->matrix_lock, flags);
	irq_matrix_offline(imsic->matrix);
	raw_spin_unlock_irqrestore(&imsic->matrix_lock, flags);

#ifdef CONFIG_SMP
	struct imsic_local_priv *lpriv = this_cpu_ptr(imsic->lpriv);

	raw_spin_lock_irqsave(&lpriv->lock, flags);
	WARN_ON_ONCE(try_to_del_timer_sync(&lpriv->timer) < 0);
	raw_spin_unlock_irqrestore(&lpriv->lock, flags);
#endif
}

static int __init imsic_matrix_init(void)
{
	struct imsic_global_config *global = &imsic->global;

	raw_spin_lock_init(&imsic->matrix_lock);
	imsic->matrix = irq_alloc_matrix(global->nr_ids + 1,
					 0, global->nr_ids + 1);
	if (!imsic->matrix)
		return -ENOMEM;

	/* Reserve ID#0 because it is special and never implemented */
	irq_matrix_assign_system(imsic->matrix, 0, false);

	/* Reserve IPI ID because it is special and used internally */
	irq_matrix_assign_system(imsic->matrix, IMSIC_IPI_ID, false);

	return 0;
}

static int __init imsic_populate_global_dt(struct fwnode_handle *fwnode,
					   struct imsic_global_config *global,
					   u32 *nr_parent_irqs)
{
	int rc;

	/* Find number of guest index bits in MSI address */
	rc = of_property_read_u32(to_of_node(fwnode), "riscv,guest-index-bits",
				  &global->guest_index_bits);
	if (rc)
		global->guest_index_bits = 0;

	/* Find number of HART index bits */
	rc = of_property_read_u32(to_of_node(fwnode), "riscv,hart-index-bits",
				  &global->hart_index_bits);
	if (rc) {
		/* Assume default value */
		global->hart_index_bits = __fls(*nr_parent_irqs);
		if (BIT(global->hart_index_bits) < *nr_parent_irqs)
			global->hart_index_bits++;
	}

	/* Find number of group index bits */
	rc = of_property_read_u32(to_of_node(fwnode), "riscv,group-index-bits",
				  &global->group_index_bits);
	if (rc)
		global->group_index_bits = 0;

	/*
	 * Find first bit position of group index.
	 * If not specified assumed the default APLIC-IMSIC configuration.
	 */
	rc = of_property_read_u32(to_of_node(fwnode), "riscv,group-index-shift",
				  &global->group_index_shift);
	if (rc)
		global->group_index_shift = IMSIC_MMIO_PAGE_SHIFT * 2;

	/* Find number of interrupt identities */
	rc = of_property_read_u32(to_of_node(fwnode), "riscv,num-ids",
				  &global->nr_ids);
	if (rc) {
		pr_err("%pfwP: number of interrupt identities not found\n", fwnode);
		return rc;
	}

	/* Find number of guest interrupt identities */
	rc = of_property_read_u32(to_of_node(fwnode), "riscv,num-guest-ids",
				  &global->nr_guest_ids);
	if (rc)
		global->nr_guest_ids = global->nr_ids;

	return 0;
}

static int __init imsic_populate_global_acpi(struct fwnode_handle *fwnode,
					     struct imsic_global_config *global,
					     u32 *nr_parent_irqs, void *opaque)
{
	struct acpi_madt_imsic *imsic = (struct acpi_madt_imsic *)opaque;

	global->guest_index_bits = imsic->guest_index_bits;
	global->hart_index_bits = imsic->hart_index_bits;
	global->group_index_bits = imsic->group_index_bits;
	global->group_index_shift = imsic->group_index_shift;
	global->nr_ids = imsic->num_ids;
	global->nr_guest_ids = imsic->num_guest_ids;
	return 0;
}

static int __init imsic_get_parent_hartid(struct fwnode_handle *fwnode,
					  u32 index, unsigned long *hartid)
{
	struct of_phandle_args parent;
	int rc;

	if (!is_of_node(fwnode)) {
		if (hartid)
			*hartid = acpi_rintc_index_to_hartid(index);

		if (!hartid || (*hartid == INVALID_HARTID))
			return -EINVAL;

		return 0;
	}

	rc = of_irq_parse_one(to_of_node(fwnode), index, &parent);
	if (rc)
		return rc;

	/*
	 * Skip interrupts other than external interrupts for
	 * current privilege level.
	 */
	if (parent.args[0] != RV_IRQ_EXT)
		return -EINVAL;

	return riscv_of_parent_hartid(parent.np, hartid);
}

static int __init imsic_get_mmio_resource(struct fwnode_handle *fwnode,
					  u32 index, struct resource *res)
{
	if (!is_of_node(fwnode))
		return acpi_rintc_get_imsic_mmio_info(index, res);

	return of_address_to_resource(to_of_node(fwnode), index, res);
}

static int __init imsic_parse_fwnode(struct fwnode_handle *fwnode,
				     struct imsic_global_config *global,
				     u32 *nr_parent_irqs,
				     u32 *nr_mmios,
				     void *opaque)
{
	unsigned long hartid;
	struct resource res;
	int rc;
	u32 i;

	*nr_parent_irqs = 0;
	*nr_mmios = 0;

	/* Find number of parent interrupts */
	while (!imsic_get_parent_hartid(fwnode, *nr_parent_irqs, &hartid))
		(*nr_parent_irqs)++;
	if (!*nr_parent_irqs) {
		pr_err("%pfwP: no parent irqs available\n", fwnode);
		return -EINVAL;
	}

	if (is_of_node(fwnode))
		rc = imsic_populate_global_dt(fwnode, global, nr_parent_irqs);
	else
		rc = imsic_populate_global_acpi(fwnode, global, nr_parent_irqs, opaque);

	if (rc)
		return rc;

	/* Sanity check guest index bits */
	i = BITS_PER_LONG - IMSIC_MMIO_PAGE_SHIFT;
	if (i < global->guest_index_bits) {
		pr_err("%pfwP: guest index bits too big\n", fwnode);
		return -EINVAL;
	}

	/* Sanity check HART index bits */
	i = BITS_PER_LONG - IMSIC_MMIO_PAGE_SHIFT - global->guest_index_bits;
	if (i < global->hart_index_bits) {
		pr_err("%pfwP: HART index bits too big\n", fwnode);
		return -EINVAL;
	}

	/* Sanity check group index bits */
	i = BITS_PER_LONG - IMSIC_MMIO_PAGE_SHIFT -
	    global->guest_index_bits - global->hart_index_bits;
	if (i < global->group_index_bits) {
		pr_err("%pfwP: group index bits too big\n", fwnode);
		return -EINVAL;
	}

	/* Sanity check group index shift */
	i = global->group_index_bits + global->group_index_shift - 1;
	if (i >= BITS_PER_LONG) {
		pr_err("%pfwP: group index shift too big\n", fwnode);
		return -EINVAL;
	}

	/* Sanity check number of interrupt identities */
	if (global->nr_ids < IMSIC_MIN_ID ||
	    global->nr_ids >= IMSIC_MAX_ID ||
	    (global->nr_ids & IMSIC_MIN_ID) != IMSIC_MIN_ID) {
		pr_err("%pfwP: invalid number of interrupt identities\n", fwnode);
		return -EINVAL;
	}

	/* Sanity check number of guest interrupt identities */
	if (global->nr_guest_ids < IMSIC_MIN_ID ||
	    global->nr_guest_ids >= IMSIC_MAX_ID ||
	    (global->nr_guest_ids & IMSIC_MIN_ID) != IMSIC_MIN_ID) {
		pr_err("%pfwP: invalid number of guest interrupt identities\n", fwnode);
		return -EINVAL;
	}

	/* Compute base address */
	rc = imsic_get_mmio_resource(fwnode, 0, &res);
	if (rc) {
		pr_err("%pfwP: first MMIO resource not found\n", fwnode);
		return -EINVAL;
	}
	global->base_addr = res.start;
	global->base_addr &= ~(BIT(global->guest_index_bits +
				   global->hart_index_bits +
				   IMSIC_MMIO_PAGE_SHIFT) - 1);
	global->base_addr &= ~((BIT(global->group_index_bits) - 1) <<
			       global->group_index_shift);

	/* Find number of MMIO register sets */
	while (!imsic_get_mmio_resource(fwnode, *nr_mmios, &res))
		(*nr_mmios)++;

	return 0;
}

int __init imsic_setup_state(struct fwnode_handle *fwnode, void *opaque)
{
	u32 i, j, index, nr_parent_irqs, nr_mmios, nr_handlers = 0;
	struct imsic_global_config *global;
	struct imsic_local_config *local;
	void __iomem **mmios_va = NULL;
	struct resource *mmios = NULL;
	unsigned long reloff, hartid;
	phys_addr_t base_addr;
	int rc, cpu;

	/*
	 * Only one IMSIC instance allowed in a platform for clean
	 * implementation of SMP IRQ affinity and per-CPU IPIs.
	 *
	 * This means on a multi-socket (or multi-die) platform we
	 * will have multiple MMIO regions for one IMSIC instance.
	 */
	if (imsic) {
		pr_err("%pfwP: already initialized hence ignoring\n", fwnode);
		return -EALREADY;
	}

	if (!riscv_isa_extension_available(NULL, SxAIA)) {
		pr_err("%pfwP: AIA support not available\n", fwnode);
		return -ENODEV;
	}

	imsic = kzalloc(sizeof(*imsic), GFP_KERNEL);
	if (!imsic)
		return -ENOMEM;
	imsic->fwnode = fwnode;
	global = &imsic->global;

	global->local = alloc_percpu(typeof(*global->local));
	if (!global->local) {
		rc = -ENOMEM;
		goto out_free_priv;
	}

	/* Parse IMSIC fwnode */
	rc = imsic_parse_fwnode(fwnode, global, &nr_parent_irqs, &nr_mmios, opaque);
	if (rc)
		goto out_free_local;

	/* Allocate MMIO resource array */
	mmios = kcalloc(nr_mmios, sizeof(*mmios), GFP_KERNEL);
	if (!mmios) {
		rc = -ENOMEM;
		goto out_free_local;
	}

	/* Allocate MMIO virtual address array */
	mmios_va = kcalloc(nr_mmios, sizeof(*mmios_va), GFP_KERNEL);
	if (!mmios_va) {
		rc = -ENOMEM;
		goto out_iounmap;
	}

	/* Parse and map MMIO register sets */
	for (i = 0; i < nr_mmios; i++) {
		rc = imsic_get_mmio_resource(fwnode, i, &mmios[i]);
		if (rc) {
			pr_err("%pfwP: unable to parse MMIO regset %d\n", fwnode, i);
			goto out_iounmap;
		}

		base_addr = mmios[i].start;
		base_addr &= ~(BIT(global->guest_index_bits +
				   global->hart_index_bits +
				   IMSIC_MMIO_PAGE_SHIFT) - 1);
		base_addr &= ~((BIT(global->group_index_bits) - 1) <<
			       global->group_index_shift);
		if (base_addr != global->base_addr) {
			rc = -EINVAL;
			pr_err("%pfwP: address mismatch for regset %d\n", fwnode, i);
			goto out_iounmap;
		}

		mmios_va[i] = ioremap(mmios[i].start, resource_size(&mmios[i]));
		if (!mmios_va[i]) {
			rc = -EIO;
			pr_err("%pfwP: unable to map MMIO regset %d\n", fwnode, i);
			goto out_iounmap;
		}
	}

	/* Initialize local (or per-CPU )state */
	rc = imsic_local_init();
	if (rc) {
		pr_err("%pfwP: failed to initialize local state\n",
		       fwnode);
		goto out_iounmap;
	}

	/* Configure handlers for target CPUs */
	for (i = 0; i < nr_parent_irqs; i++) {
		rc = imsic_get_parent_hartid(fwnode, i, &hartid);
		if (rc) {
			pr_warn("%pfwP: hart ID for parent irq%d not found\n", fwnode, i);
			continue;
		}

		cpu = riscv_hartid_to_cpuid(hartid);
		if (cpu < 0) {
			pr_warn("%pfwP: invalid cpuid for parent irq%d\n", fwnode, i);
			continue;
		}

		/* Find MMIO location of MSI page */
		index = nr_mmios;
		reloff = i * BIT(global->guest_index_bits) *
			 IMSIC_MMIO_PAGE_SZ;
		for (j = 0; nr_mmios; j++) {
			if (reloff < resource_size(&mmios[j])) {
				index = j;
				break;
			}

			/*
			 * MMIO region size may not be aligned to
			 * BIT(global->guest_index_bits) * IMSIC_MMIO_PAGE_SZ
			 * if holes are present.
			 */
			reloff -= ALIGN(resource_size(&mmios[j]),
			BIT(global->guest_index_bits) * IMSIC_MMIO_PAGE_SZ);
		}
		if (index >= nr_mmios) {
			pr_warn("%pfwP: MMIO not found for parent irq%d\n", fwnode, i);
			continue;
		}

		local = per_cpu_ptr(global->local, cpu);
		local->msi_pa = mmios[index].start + reloff;
		local->msi_va = mmios_va[index] + reloff;

		nr_handlers++;
	}

	/* If no CPU handlers found then can't take interrupts */
	if (!nr_handlers) {
		pr_err("%pfwP: No CPU handlers found\n", fwnode);
		rc = -ENODEV;
		goto out_local_cleanup;
	}

	/* Initialize matrix allocator */
	rc = imsic_matrix_init();
	if (rc) {
		pr_err("%pfwP: failed to create matrix allocator\n", fwnode);
		goto out_local_cleanup;
	}

	/* We don't need MMIO arrays anymore so let's free-up */
	kfree(mmios_va);
	kfree(mmios);

	return 0;

out_local_cleanup:
	imsic_local_cleanup();
out_iounmap:
	for (i = 0; i < nr_mmios; i++) {
		if (mmios_va[i])
			iounmap(mmios_va[i]);
	}
	kfree(mmios_va);
	kfree(mmios);
out_free_local:
	free_percpu(imsic->global.local);
out_free_priv:
	kfree(imsic);
	imsic = NULL;
	return rc;
}
