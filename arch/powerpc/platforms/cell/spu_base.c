/*
 * Low-level SPU handling
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#undef DEBUG

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/linux_logo.h>
#include <linux/syscore_ops.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/spu_csa.h>
#include <asm/xmon.h>
#include <asm/prom.h>
#include <asm/kexec.h>

const struct spu_management_ops *spu_management_ops;
EXPORT_SYMBOL_GPL(spu_management_ops);

const struct spu_priv1_ops *spu_priv1_ops;
EXPORT_SYMBOL_GPL(spu_priv1_ops);

struct cbe_spu_info cbe_spu_info[MAX_NUMNODES];
EXPORT_SYMBOL_GPL(cbe_spu_info);

/*
 * The spufs fault-handling code needs to call force_sig_info to raise signals
 * on DMA errors. Export it here to avoid general kernel-wide access to this
 * function
 */
EXPORT_SYMBOL_GPL(force_sig_info);

/*
 * Protects cbe_spu_info and spu->number.
 */
static DEFINE_SPINLOCK(spu_lock);

/*
 * List of all spus in the system.
 *
 * This list is iterated by callers from irq context and callers that
 * want to sleep.  Thus modifications need to be done with both
 * spu_full_list_lock and spu_full_list_mutex held, while iterating
 * through it requires either of these locks.
 *
 * In addition spu_full_list_lock protects all assignments to
 * spu->mm.
 */
static LIST_HEAD(spu_full_list);
static DEFINE_SPINLOCK(spu_full_list_lock);
static DEFINE_MUTEX(spu_full_list_mutex);

void spu_invalidate_slbs(struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	unsigned long flags;

	spin_lock_irqsave(&spu->register_lock, flags);
	if (spu_mfc_sr1_get(spu) & MFC_STATE1_RELOCATE_MASK)
		out_be64(&priv2->slb_invalidate_all_W, 0UL);
	spin_unlock_irqrestore(&spu->register_lock, flags);
}
EXPORT_SYMBOL_GPL(spu_invalidate_slbs);

/* This is called by the MM core when a segment size is changed, to
 * request a flush of all the SPEs using a given mm
 */
void spu_flush_all_slbs(struct mm_struct *mm)
{
	struct spu *spu;
	unsigned long flags;

	spin_lock_irqsave(&spu_full_list_lock, flags);
	list_for_each_entry(spu, &spu_full_list, full_list) {
		if (spu->mm == mm)
			spu_invalidate_slbs(spu);
	}
	spin_unlock_irqrestore(&spu_full_list_lock, flags);
}

/* The hack below stinks... try to do something better one of
 * these days... Does it even work properly with NR_CPUS == 1 ?
 */
static inline void mm_needs_global_tlbie(struct mm_struct *mm)
{
	int nr = (NR_CPUS > 1) ? NR_CPUS : NR_CPUS + 1;

	/* Global TLBIE broadcast required with SPEs. */
	bitmap_fill(cpumask_bits(mm_cpumask(mm)), nr);
}

void spu_associate_mm(struct spu *spu, struct mm_struct *mm)
{
	unsigned long flags;

	spin_lock_irqsave(&spu_full_list_lock, flags);
	spu->mm = mm;
	spin_unlock_irqrestore(&spu_full_list_lock, flags);
	if (mm)
		mm_needs_global_tlbie(mm);
}
EXPORT_SYMBOL_GPL(spu_associate_mm);

int spu_64k_pages_available(void)
{
	return mmu_psize_defs[MMU_PAGE_64K].shift != 0;
}
EXPORT_SYMBOL_GPL(spu_64k_pages_available);

static void spu_restart_dma(struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	if (!test_bit(SPU_CONTEXT_SWITCH_PENDING, &spu->flags))
		out_be64(&priv2->mfc_control_RW, MFC_CNTL_RESTART_DMA_COMMAND);
	else {
		set_bit(SPU_CONTEXT_FAULT_PENDING, &spu->flags);
		mb();
	}
}

static inline void spu_load_slb(struct spu *spu, int slbe, struct copro_slb *slb)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	pr_debug("%s: adding SLB[%d] 0x%016llx 0x%016llx\n",
			__func__, slbe, slb->vsid, slb->esid);

	out_be64(&priv2->slb_index_W, slbe);
	/* set invalid before writing vsid */
	out_be64(&priv2->slb_esid_RW, 0);
	/* now it's safe to write the vsid */
	out_be64(&priv2->slb_vsid_RW, slb->vsid);
	/* setting the new esid makes the entry valid again */
	out_be64(&priv2->slb_esid_RW, slb->esid);
}

static int __spu_trap_data_seg(struct spu *spu, unsigned long ea)
{
	struct copro_slb slb;
	int ret;

	ret = copro_calculate_slb(spu->mm, ea, &slb);
	if (ret)
		return ret;

	spu_load_slb(spu, spu->slb_replace, &slb);

	spu->slb_replace++;
	if (spu->slb_replace >= 8)
		spu->slb_replace = 0;

	spu_restart_dma(spu);
	spu->stats.slb_flt++;
	return 0;
}

extern int hash_page(unsigned long ea, unsigned long access,
		     unsigned long trap, unsigned long dsisr); //XXX
static int __spu_trap_data_map(struct spu *spu, unsigned long ea, u64 dsisr)
{
	int ret;

	pr_debug("%s, %llx, %lx\n", __func__, dsisr, ea);

	/*
	 * Handle kernel space hash faults immediately. User hash
	 * faults need to be deferred to process context.
	 */
	if ((dsisr & MFC_DSISR_PTE_NOT_FOUND) &&
	    (REGION_ID(ea) != USER_REGION_ID)) {

		spin_unlock(&spu->register_lock);
		ret = hash_page(ea, _PAGE_PRESENT | _PAGE_READ, 0x300, dsisr);
		spin_lock(&spu->register_lock);

		if (!ret) {
			spu_restart_dma(spu);
			return 0;
		}
	}

	spu->class_1_dar = ea;
	spu->class_1_dsisr = dsisr;

	spu->stop_callback(spu, 1);

	spu->class_1_dar = 0;
	spu->class_1_dsisr = 0;

	return 0;
}

static void __spu_kernel_slb(void *addr, struct copro_slb *slb)
{
	unsigned long ea = (unsigned long)addr;
	u64 llp;

	if (REGION_ID(ea) == KERNEL_REGION_ID)
		llp = mmu_psize_defs[mmu_linear_psize].sllp;
	else
		llp = mmu_psize_defs[mmu_virtual_psize].sllp;

	slb->vsid = (get_kernel_vsid(ea, MMU_SEGSIZE_256M) << SLB_VSID_SHIFT) |
		SLB_VSID_KERNEL | llp;
	slb->esid = (ea & ESID_MASK) | SLB_ESID_V;
}

/**
 * Given an array of @nr_slbs SLB entries, @slbs, return non-zero if the
 * address @new_addr is present.
 */
static inline int __slb_present(struct copro_slb *slbs, int nr_slbs,
		void *new_addr)
{
	unsigned long ea = (unsigned long)new_addr;
	int i;

	for (i = 0; i < nr_slbs; i++)
		if (!((slbs[i].esid ^ ea) & ESID_MASK))
			return 1;

	return 0;
}

/**
 * Setup the SPU kernel SLBs, in preparation for a context save/restore. We
 * need to map both the context save area, and the save/restore code.
 *
 * Because the lscsa and code may cross segment boundaries, we check to see
 * if mappings are required for the start and end of each range. We currently
 * assume that the mappings are smaller that one segment - if not, something
 * is seriously wrong.
 */
void spu_setup_kernel_slbs(struct spu *spu, struct spu_lscsa *lscsa,
		void *code, int code_size)
{
	struct copro_slb slbs[4];
	int i, nr_slbs = 0;
	/* start and end addresses of both mappings */
	void *addrs[] = {
		lscsa, (void *)lscsa + sizeof(*lscsa) - 1,
		code, code + code_size - 1
	};

	/* check the set of addresses, and create a new entry in the slbs array
	 * if there isn't already a SLB for that address */
	for (i = 0; i < ARRAY_SIZE(addrs); i++) {
		if (__slb_present(slbs, nr_slbs, addrs[i]))
			continue;

		__spu_kernel_slb(addrs[i], &slbs[nr_slbs]);
		nr_slbs++;
	}

	spin_lock_irq(&spu->register_lock);
	/* Add the set of SLBs */
	for (i = 0; i < nr_slbs; i++)
		spu_load_slb(spu, i, &slbs[i]);
	spin_unlock_irq(&spu->register_lock);
}
EXPORT_SYMBOL_GPL(spu_setup_kernel_slbs);

static irqreturn_t
spu_irq_class_0(int irq, void *data)
{
	struct spu *spu;
	unsigned long stat, mask;

	spu = data;

	spin_lock(&spu->register_lock);
	mask = spu_int_mask_get(spu, 0);
	stat = spu_int_stat_get(spu, 0) & mask;

	spu->class_0_pending |= stat;
	spu->class_0_dar = spu_mfc_dar_get(spu);
	spu->stop_callback(spu, 0);
	spu->class_0_pending = 0;
	spu->class_0_dar = 0;

	spu_int_stat_clear(spu, 0, stat);
	spin_unlock(&spu->register_lock);

	return IRQ_HANDLED;
}

static irqreturn_t
spu_irq_class_1(int irq, void *data)
{
	struct spu *spu;
	unsigned long stat, mask, dar, dsisr;

	spu = data;

	/* atomically read & clear class1 status. */
	spin_lock(&spu->register_lock);
	mask  = spu_int_mask_get(spu, 1);
	stat  = spu_int_stat_get(spu, 1) & mask;
	dar   = spu_mfc_dar_get(spu);
	dsisr = spu_mfc_dsisr_get(spu);
	if (stat & CLASS1_STORAGE_FAULT_INTR)
		spu_mfc_dsisr_set(spu, 0ul);
	spu_int_stat_clear(spu, 1, stat);

	pr_debug("%s: %lx %lx %lx %lx\n", __func__, mask, stat,
			dar, dsisr);

	if (stat & CLASS1_SEGMENT_FAULT_INTR)
		__spu_trap_data_seg(spu, dar);

	if (stat & CLASS1_STORAGE_FAULT_INTR)
		__spu_trap_data_map(spu, dar, dsisr);

	if (stat & CLASS1_LS_COMPARE_SUSPEND_ON_GET_INTR)
		;

	if (stat & CLASS1_LS_COMPARE_SUSPEND_ON_PUT_INTR)
		;

	spu->class_1_dsisr = 0;
	spu->class_1_dar = 0;

	spin_unlock(&spu->register_lock);

	return stat ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t
spu_irq_class_2(int irq, void *data)
{
	struct spu *spu;
	unsigned long stat;
	unsigned long mask;
	const int mailbox_intrs =
		CLASS2_MAILBOX_THRESHOLD_INTR | CLASS2_MAILBOX_INTR;

	spu = data;
	spin_lock(&spu->register_lock);
	stat = spu_int_stat_get(spu, 2);
	mask = spu_int_mask_get(spu, 2);
	/* ignore interrupts we're not waiting for */
	stat &= mask;
	/* mailbox interrupts are level triggered. mask them now before
	 * acknowledging */
	if (stat & mailbox_intrs)
		spu_int_mask_and(spu, 2, ~(stat & mailbox_intrs));
	/* acknowledge all interrupts before the callbacks */
	spu_int_stat_clear(spu, 2, stat);

	pr_debug("class 2 interrupt %d, %lx, %lx\n", irq, stat, mask);

	if (stat & CLASS2_MAILBOX_INTR)
		spu->ibox_callback(spu);

	if (stat & CLASS2_SPU_STOP_INTR)
		spu->stop_callback(spu, 2);

	if (stat & CLASS2_SPU_HALT_INTR)
		spu->stop_callback(spu, 2);

	if (stat & CLASS2_SPU_DMA_TAG_GROUP_COMPLETE_INTR)
		spu->mfc_callback(spu);

	if (stat & CLASS2_MAILBOX_THRESHOLD_INTR)
		spu->wbox_callback(spu);

	spu->stats.class2_intr++;

	spin_unlock(&spu->register_lock);

	return stat ? IRQ_HANDLED : IRQ_NONE;
}

static int spu_request_irqs(struct spu *spu)
{
	int ret = 0;

	if (spu->irqs[0] != NO_IRQ) {
		snprintf(spu->irq_c0, sizeof (spu->irq_c0), "spe%02d.0",
			 spu->number);
		ret = request_irq(spu->irqs[0], spu_irq_class_0,
				  0, spu->irq_c0, spu);
		if (ret)
			goto bail0;
	}
	if (spu->irqs[1] != NO_IRQ) {
		snprintf(spu->irq_c1, sizeof (spu->irq_c1), "spe%02d.1",
			 spu->number);
		ret = request_irq(spu->irqs[1], spu_irq_class_1,
				  0, spu->irq_c1, spu);
		if (ret)
			goto bail1;
	}
	if (spu->irqs[2] != NO_IRQ) {
		snprintf(spu->irq_c2, sizeof (spu->irq_c2), "spe%02d.2",
			 spu->number);
		ret = request_irq(spu->irqs[2], spu_irq_class_2,
				  0, spu->irq_c2, spu);
		if (ret)
			goto bail2;
	}
	return 0;

bail2:
	if (spu->irqs[1] != NO_IRQ)
		free_irq(spu->irqs[1], spu);
bail1:
	if (spu->irqs[0] != NO_IRQ)
		free_irq(spu->irqs[0], spu);
bail0:
	return ret;
}

static void spu_free_irqs(struct spu *spu)
{
	if (spu->irqs[0] != NO_IRQ)
		free_irq(spu->irqs[0], spu);
	if (spu->irqs[1] != NO_IRQ)
		free_irq(spu->irqs[1], spu);
	if (spu->irqs[2] != NO_IRQ)
		free_irq(spu->irqs[2], spu);
}

void spu_init_channels(struct spu *spu)
{
	static const struct {
		 unsigned channel;
		 unsigned count;
	} zero_list[] = {
		{ 0x00, 1, }, { 0x01, 1, }, { 0x03, 1, }, { 0x04, 1, },
		{ 0x18, 1, }, { 0x19, 1, }, { 0x1b, 1, }, { 0x1d, 1, },
	}, count_list[] = {
		{ 0x00, 0, }, { 0x03, 0, }, { 0x04, 0, }, { 0x15, 16, },
		{ 0x17, 1, }, { 0x18, 0, }, { 0x19, 0, }, { 0x1b, 0, },
		{ 0x1c, 1, }, { 0x1d, 0, }, { 0x1e, 1, },
	};
	struct spu_priv2 __iomem *priv2;
	int i;

	priv2 = spu->priv2;

	/* initialize all channel data to zero */
	for (i = 0; i < ARRAY_SIZE(zero_list); i++) {
		int count;

		out_be64(&priv2->spu_chnlcntptr_RW, zero_list[i].channel);
		for (count = 0; count < zero_list[i].count; count++)
			out_be64(&priv2->spu_chnldata_RW, 0);
	}

	/* initialize channel counts to meaningful values */
	for (i = 0; i < ARRAY_SIZE(count_list); i++) {
		out_be64(&priv2->spu_chnlcntptr_RW, count_list[i].channel);
		out_be64(&priv2->spu_chnlcnt_RW, count_list[i].count);
	}
}
EXPORT_SYMBOL_GPL(spu_init_channels);

static struct bus_type spu_subsys = {
	.name = "spu",
	.dev_name = "spu",
};

int spu_add_dev_attr(struct device_attribute *attr)
{
	struct spu *spu;

	mutex_lock(&spu_full_list_mutex);
	list_for_each_entry(spu, &spu_full_list, full_list)
		device_create_file(&spu->dev, attr);
	mutex_unlock(&spu_full_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(spu_add_dev_attr);

int spu_add_dev_attr_group(struct attribute_group *attrs)
{
	struct spu *spu;
	int rc = 0;

	mutex_lock(&spu_full_list_mutex);
	list_for_each_entry(spu, &spu_full_list, full_list) {
		rc = sysfs_create_group(&spu->dev.kobj, attrs);

		/* we're in trouble here, but try unwinding anyway */
		if (rc) {
			printk(KERN_ERR "%s: can't create sysfs group '%s'\n",
					__func__, attrs->name);

			list_for_each_entry_continue_reverse(spu,
					&spu_full_list, full_list)
				sysfs_remove_group(&spu->dev.kobj, attrs);
			break;
		}
	}

	mutex_unlock(&spu_full_list_mutex);

	return rc;
}
EXPORT_SYMBOL_GPL(spu_add_dev_attr_group);


void spu_remove_dev_attr(struct device_attribute *attr)
{
	struct spu *spu;

	mutex_lock(&spu_full_list_mutex);
	list_for_each_entry(spu, &spu_full_list, full_list)
		device_remove_file(&spu->dev, attr);
	mutex_unlock(&spu_full_list_mutex);
}
EXPORT_SYMBOL_GPL(spu_remove_dev_attr);

void spu_remove_dev_attr_group(struct attribute_group *attrs)
{
	struct spu *spu;

	mutex_lock(&spu_full_list_mutex);
	list_for_each_entry(spu, &spu_full_list, full_list)
		sysfs_remove_group(&spu->dev.kobj, attrs);
	mutex_unlock(&spu_full_list_mutex);
}
EXPORT_SYMBOL_GPL(spu_remove_dev_attr_group);

static int spu_create_dev(struct spu *spu)
{
	int ret;

	spu->dev.id = spu->number;
	spu->dev.bus = &spu_subsys;
	ret = device_register(&spu->dev);
	if (ret) {
		printk(KERN_ERR "Can't register SPU %d with sysfs\n",
				spu->number);
		return ret;
	}

	sysfs_add_device_to_node(&spu->dev, spu->node);

	return 0;
}

static int __init create_spu(void *data)
{
	struct spu *spu;
	int ret;
	static int number;
	unsigned long flags;

	ret = -ENOMEM;
	spu = kzalloc(sizeof (*spu), GFP_KERNEL);
	if (!spu)
		goto out;

	spu->alloc_state = SPU_FREE;

	spin_lock_init(&spu->register_lock);
	spin_lock(&spu_lock);
	spu->number = number++;
	spin_unlock(&spu_lock);

	ret = spu_create_spu(spu, data);

	if (ret)
		goto out_free;

	spu_mfc_sdr_setup(spu);
	spu_mfc_sr1_set(spu, 0x33);
	ret = spu_request_irqs(spu);
	if (ret)
		goto out_destroy;

	ret = spu_create_dev(spu);
	if (ret)
		goto out_free_irqs;

	mutex_lock(&cbe_spu_info[spu->node].list_mutex);
	list_add(&spu->cbe_list, &cbe_spu_info[spu->node].spus);
	cbe_spu_info[spu->node].n_spus++;
	mutex_unlock(&cbe_spu_info[spu->node].list_mutex);

	mutex_lock(&spu_full_list_mutex);
	spin_lock_irqsave(&spu_full_list_lock, flags);
	list_add(&spu->full_list, &spu_full_list);
	spin_unlock_irqrestore(&spu_full_list_lock, flags);
	mutex_unlock(&spu_full_list_mutex);

	spu->stats.util_state = SPU_UTIL_IDLE_LOADED;
	spu->stats.tstamp = ktime_get_ns();

	INIT_LIST_HEAD(&spu->aff_list);

	goto out;

out_free_irqs:
	spu_free_irqs(spu);
out_destroy:
	spu_destroy_spu(spu);
out_free:
	kfree(spu);
out:
	return ret;
}

static const char *spu_state_names[] = {
	"user", "system", "iowait", "idle"
};

static unsigned long long spu_acct_time(struct spu *spu,
		enum spu_utilization_state state)
{
	unsigned long long time = spu->stats.times[state];

	/*
	 * If the spu is idle or the context is stopped, utilization
	 * statistics are not updated.  Apply the time delta from the
	 * last recorded state of the spu.
	 */
	if (spu->stats.util_state == state)
		time += ktime_get_ns() - spu->stats.tstamp;

	return time / NSEC_PER_MSEC;
}


static ssize_t spu_stat_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct spu *spu = container_of(dev, struct spu, dev);

	return sprintf(buf, "%s %llu %llu %llu %llu "
		      "%llu %llu %llu %llu %llu %llu %llu %llu\n",
		spu_state_names[spu->stats.util_state],
		spu_acct_time(spu, SPU_UTIL_USER),
		spu_acct_time(spu, SPU_UTIL_SYSTEM),
		spu_acct_time(spu, SPU_UTIL_IOWAIT),
		spu_acct_time(spu, SPU_UTIL_IDLE_LOADED),
		spu->stats.vol_ctx_switch,
		spu->stats.invol_ctx_switch,
		spu->stats.slb_flt,
		spu->stats.hash_flt,
		spu->stats.min_flt,
		spu->stats.maj_flt,
		spu->stats.class2_intr,
		spu->stats.libassist);
}

static DEVICE_ATTR(stat, 0444, spu_stat_show, NULL);

#ifdef CONFIG_KEXEC

struct crash_spu_info {
	struct spu *spu;
	u32 saved_spu_runcntl_RW;
	u32 saved_spu_status_R;
	u32 saved_spu_npc_RW;
	u64 saved_mfc_sr1_RW;
	u64 saved_mfc_dar;
	u64 saved_mfc_dsisr;
};

#define CRASH_NUM_SPUS	16	/* Enough for current hardware */
static struct crash_spu_info crash_spu_info[CRASH_NUM_SPUS];

static void crash_kexec_stop_spus(void)
{
	struct spu *spu;
	int i;
	u64 tmp;

	for (i = 0; i < CRASH_NUM_SPUS; i++) {
		if (!crash_spu_info[i].spu)
			continue;

		spu = crash_spu_info[i].spu;

		crash_spu_info[i].saved_spu_runcntl_RW =
			in_be32(&spu->problem->spu_runcntl_RW);
		crash_spu_info[i].saved_spu_status_R =
			in_be32(&spu->problem->spu_status_R);
		crash_spu_info[i].saved_spu_npc_RW =
			in_be32(&spu->problem->spu_npc_RW);

		crash_spu_info[i].saved_mfc_dar    = spu_mfc_dar_get(spu);
		crash_spu_info[i].saved_mfc_dsisr  = spu_mfc_dsisr_get(spu);
		tmp = spu_mfc_sr1_get(spu);
		crash_spu_info[i].saved_mfc_sr1_RW = tmp;

		tmp &= ~MFC_STATE1_MASTER_RUN_CONTROL_MASK;
		spu_mfc_sr1_set(spu, tmp);

		__delay(200);
	}
}

static void crash_register_spus(struct list_head *list)
{
	struct spu *spu;
	int ret;

	list_for_each_entry(spu, list, full_list) {
		if (WARN_ON(spu->number >= CRASH_NUM_SPUS))
			continue;

		crash_spu_info[spu->number].spu = spu;
	}

	ret = crash_shutdown_register(&crash_kexec_stop_spus);
	if (ret)
		printk(KERN_ERR "Could not register SPU crash handler");
}

#else
static inline void crash_register_spus(struct list_head *list)
{
}
#endif

static void spu_shutdown(void)
{
	struct spu *spu;

	mutex_lock(&spu_full_list_mutex);
	list_for_each_entry(spu, &spu_full_list, full_list) {
		spu_free_irqs(spu);
		spu_destroy_spu(spu);
	}
	mutex_unlock(&spu_full_list_mutex);
}

static struct syscore_ops spu_syscore_ops = {
	.shutdown = spu_shutdown,
};

static int __init init_spu_base(void)
{
	int i, ret = 0;

	for (i = 0; i < MAX_NUMNODES; i++) {
		mutex_init(&cbe_spu_info[i].list_mutex);
		INIT_LIST_HEAD(&cbe_spu_info[i].spus);
	}

	if (!spu_management_ops)
		goto out;

	/* create system subsystem for spus */
	ret = subsys_system_register(&spu_subsys, NULL);
	if (ret)
		goto out;

	ret = spu_enumerate_spus(create_spu);

	if (ret < 0) {
		printk(KERN_WARNING "%s: Error initializing spus\n",
			__func__);
		goto out_unregister_subsys;
	}

	if (ret > 0)
		fb_append_extra_logo(&logo_spe_clut224, ret);

	mutex_lock(&spu_full_list_mutex);
	xmon_register_spus(&spu_full_list);
	crash_register_spus(&spu_full_list);
	mutex_unlock(&spu_full_list_mutex);
	spu_add_dev_attr(&dev_attr_stat);
	register_syscore_ops(&spu_syscore_ops);

	spu_init_affinity();

	return 0;

 out_unregister_subsys:
	bus_unregister(&spu_subsys);
 out:
	return ret;
}
device_initcall(init_spu_base);
