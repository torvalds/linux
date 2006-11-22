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
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/xmon.h>

const struct spu_management_ops *spu_management_ops;
const struct spu_priv1_ops *spu_priv1_ops;

EXPORT_SYMBOL_GPL(spu_priv1_ops);

static int __spu_trap_invalid_dma(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	spu->dma_callback(spu, SPE_EVENT_INVALID_DMA);
	return 0;
}

static int __spu_trap_dma_align(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	spu->dma_callback(spu, SPE_EVENT_DMA_ALIGNMENT);
	return 0;
}

static int __spu_trap_error(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	spu->dma_callback(spu, SPE_EVENT_SPE_ERROR);
	return 0;
}

static void spu_restart_dma(struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	if (!test_bit(SPU_CONTEXT_SWITCH_PENDING, &spu->flags))
		out_be64(&priv2->mfc_control_RW, MFC_CNTL_RESTART_DMA_COMMAND);
}

static int __spu_trap_data_seg(struct spu *spu, unsigned long ea)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	struct mm_struct *mm = spu->mm;
	u64 esid, vsid, llp;

	pr_debug("%s\n", __FUNCTION__);

	if (test_bit(SPU_CONTEXT_SWITCH_ACTIVE, &spu->flags)) {
		/* SLBs are pre-loaded for context switch, so
		 * we should never get here!
		 */
		printk("%s: invalid access during switch!\n", __func__);
		return 1;
	}
	esid = (ea & ESID_MASK) | SLB_ESID_V;

	switch(REGION_ID(ea)) {
	case USER_REGION_ID:
#ifdef CONFIG_HUGETLB_PAGE
		if (in_hugepage_area(mm->context, ea))
			llp = mmu_psize_defs[mmu_huge_psize].sllp;
		else
#endif
			llp = mmu_psize_defs[mmu_virtual_psize].sllp;
		vsid = (get_vsid(mm->context.id, ea) << SLB_VSID_SHIFT) |
				SLB_VSID_USER | llp;
		break;
	case VMALLOC_REGION_ID:
		llp = mmu_psize_defs[mmu_virtual_psize].sllp;
		vsid = (get_kernel_vsid(ea) << SLB_VSID_SHIFT) |
			SLB_VSID_KERNEL | llp;
		break;
	case KERNEL_REGION_ID:
		llp = mmu_psize_defs[mmu_linear_psize].sllp;
		vsid = (get_kernel_vsid(ea) << SLB_VSID_SHIFT) |
			SLB_VSID_KERNEL | llp;
		break;
	default:
		/* Future: support kernel segments so that drivers
		 * can use SPUs.
		 */
		pr_debug("invalid region access at %016lx\n", ea);
		return 1;
	}

	out_be64(&priv2->slb_index_W, spu->slb_replace);
	out_be64(&priv2->slb_vsid_RW, vsid);
	out_be64(&priv2->slb_esid_RW, esid);

	spu->slb_replace++;
	if (spu->slb_replace >= 8)
		spu->slb_replace = 0;

	spu_restart_dma(spu);

	return 0;
}

extern int hash_page(unsigned long ea, unsigned long access, unsigned long trap); //XXX
static int __spu_trap_data_map(struct spu *spu, unsigned long ea, u64 dsisr)
{
	pr_debug("%s, %lx, %lx\n", __FUNCTION__, dsisr, ea);

	/* Handle kernel space hash faults immediately.
	   User hash faults need to be deferred to process context. */
	if ((dsisr & MFC_DSISR_PTE_NOT_FOUND)
	    && REGION_ID(ea) != USER_REGION_ID
	    && hash_page(ea, _PAGE_PRESENT, 0x300) == 0) {
		spu_restart_dma(spu);
		return 0;
	}

	if (test_bit(SPU_CONTEXT_SWITCH_ACTIVE, &spu->flags)) {
		printk("%s: invalid access during switch!\n", __func__);
		return 1;
	}

	spu->dar = ea;
	spu->dsisr = dsisr;
	mb();
	spu->stop_callback(spu);
	return 0;
}

static irqreturn_t
spu_irq_class_0(int irq, void *data)
{
	struct spu *spu;

	spu = data;
	spu->class_0_pending = 1;
	spu->stop_callback(spu);

	return IRQ_HANDLED;
}

int
spu_irq_class_0_bottom(struct spu *spu)
{
	unsigned long stat, mask;

	spu->class_0_pending = 0;

	mask = spu_int_mask_get(spu, 0);
	stat = spu_int_stat_get(spu, 0);

	stat &= mask;

	if (stat & 1) /* invalid DMA alignment */
		__spu_trap_dma_align(spu);

	if (stat & 2) /* invalid MFC DMA */
		__spu_trap_invalid_dma(spu);

	if (stat & 4) /* error on SPU */
		__spu_trap_error(spu);

	spu_int_stat_clear(spu, 0, stat);

	return (stat & 0x7) ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(spu_irq_class_0_bottom);

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
	if (stat & 2) /* mapping fault */
		spu_mfc_dsisr_set(spu, 0ul);
	spu_int_stat_clear(spu, 1, stat);
	spin_unlock(&spu->register_lock);
	pr_debug("%s: %lx %lx %lx %lx\n", __FUNCTION__, mask, stat,
			dar, dsisr);

	if (stat & 1) /* segment fault */
		__spu_trap_data_seg(spu, dar);

	if (stat & 2) { /* mapping fault */
		__spu_trap_data_map(spu, dar, dsisr);
	}

	if (stat & 4) /* ls compare & suspend on get */
		;

	if (stat & 8) /* ls compare & suspend on put */
		;

	return stat ? IRQ_HANDLED : IRQ_NONE;
}
EXPORT_SYMBOL_GPL(spu_irq_class_1_bottom);

static irqreturn_t
spu_irq_class_2(int irq, void *data)
{
	struct spu *spu;
	unsigned long stat;
	unsigned long mask;

	spu = data;
	spin_lock(&spu->register_lock);
	stat = spu_int_stat_get(spu, 2);
	mask = spu_int_mask_get(spu, 2);
	/* ignore interrupts we're not waiting for */
	stat &= mask;
	/*
	 * mailbox interrupts (0x1 and 0x10) are level triggered.
	 * mask them now before acknowledging.
	 */
	if (stat & 0x11)
		spu_int_mask_and(spu, 2, ~(stat & 0x11));
	/* acknowledge all interrupts before the callbacks */
	spu_int_stat_clear(spu, 2, stat);
	spin_unlock(&spu->register_lock);

	pr_debug("class 2 interrupt %d, %lx, %lx\n", irq, stat, mask);

	if (stat & 1)  /* PPC core mailbox */
		spu->ibox_callback(spu);

	if (stat & 2) /* SPU stop-and-signal */
		spu->stop_callback(spu);

	if (stat & 4) /* SPU halted */
		spu->stop_callback(spu);

	if (stat & 8) /* DMA tag group complete */
		spu->mfc_callback(spu);

	if (stat & 0x10) /* SPU mailbox threshold */
		spu->wbox_callback(spu);

	return stat ? IRQ_HANDLED : IRQ_NONE;
}

static int spu_request_irqs(struct spu *spu)
{
	int ret = 0;

	if (spu->irqs[0] != NO_IRQ) {
		snprintf(spu->irq_c0, sizeof (spu->irq_c0), "spe%02d.0",
			 spu->number);
		ret = request_irq(spu->irqs[0], spu_irq_class_0,
				  IRQF_DISABLED,
				  spu->irq_c0, spu);
		if (ret)
			goto bail0;
	}
	if (spu->irqs[1] != NO_IRQ) {
		snprintf(spu->irq_c1, sizeof (spu->irq_c1), "spe%02d.1",
			 spu->number);
		ret = request_irq(spu->irqs[1], spu_irq_class_1,
				  IRQF_DISABLED,
				  spu->irq_c1, spu);
		if (ret)
			goto bail1;
	}
	if (spu->irqs[2] != NO_IRQ) {
		snprintf(spu->irq_c2, sizeof (spu->irq_c2), "spe%02d.2",
			 spu->number);
		ret = request_irq(spu->irqs[2], spu_irq_class_2,
				  IRQF_DISABLED,
				  spu->irq_c2, spu);
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

static struct list_head spu_list[MAX_NUMNODES];
static LIST_HEAD(spu_full_list);
static DEFINE_MUTEX(spu_mutex);

static void spu_init_channels(struct spu *spu)
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

struct spu *spu_alloc_node(int node)
{
	struct spu *spu = NULL;

	mutex_lock(&spu_mutex);
	if (!list_empty(&spu_list[node])) {
		spu = list_entry(spu_list[node].next, struct spu, list);
		list_del_init(&spu->list);
		pr_debug("Got SPU %d %d\n", spu->number, spu->node);
		spu_init_channels(spu);
	}
	mutex_unlock(&spu_mutex);

	return spu;
}
EXPORT_SYMBOL_GPL(spu_alloc_node);

struct spu *spu_alloc(void)
{
	struct spu *spu = NULL;
	int node;

	for (node = 0; node < MAX_NUMNODES; node++) {
		spu = spu_alloc_node(node);
		if (spu)
			break;
	}

	return spu;
}

void spu_free(struct spu *spu)
{
	mutex_lock(&spu_mutex);
	list_add_tail(&spu->list, &spu_list[spu->node]);
	mutex_unlock(&spu_mutex);
}
EXPORT_SYMBOL_GPL(spu_free);

static int spu_handle_mm_fault(struct spu *spu)
{
	struct mm_struct *mm = spu->mm;
	struct vm_area_struct *vma;
	u64 ea, dsisr, is_write;
	int ret;

	ea = spu->dar;
	dsisr = spu->dsisr;
#if 0
	if (!IS_VALID_EA(ea)) {
		return -EFAULT;
	}
#endif /* XXX */
	if (mm == NULL) {
		return -EFAULT;
	}
	if (mm->pgd == NULL) {
		return -EFAULT;
	}

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, ea);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= ea)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
#if 0
	if (expand_stack(vma, ea))
		goto bad_area;
#endif /* XXX */
good_area:
	is_write = dsisr & MFC_DSISR_ACCESS_PUT;
	if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if (dsisr & MFC_DSISR_ACCESS_DENIED)
			goto bad_area;
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}
	ret = 0;
	switch (handle_mm_fault(mm, vma, ea, is_write)) {
	case VM_FAULT_MINOR:
		current->min_flt++;
		break;
	case VM_FAULT_MAJOR:
		current->maj_flt++;
		break;
	case VM_FAULT_SIGBUS:
		ret = -EFAULT;
		goto bad_area;
	case VM_FAULT_OOM:
		ret = -ENOMEM;
		goto bad_area;
	default:
		BUG();
	}
	up_read(&mm->mmap_sem);
	return ret;

bad_area:
	up_read(&mm->mmap_sem);
	return -EFAULT;
}

int spu_irq_class_1_bottom(struct spu *spu)
{
	u64 ea, dsisr, access, error = 0UL;
	int ret = 0;

	ea = spu->dar;
	dsisr = spu->dsisr;
	if (dsisr & (MFC_DSISR_PTE_NOT_FOUND | MFC_DSISR_ACCESS_DENIED)) {
		u64 flags;

		access = (_PAGE_PRESENT | _PAGE_USER);
		access |= (dsisr & MFC_DSISR_ACCESS_PUT) ? _PAGE_RW : 0UL;
		local_irq_save(flags);
		if (hash_page(ea, access, 0x300) != 0)
			error |= CLASS1_ENABLE_STORAGE_FAULT_INTR;
		local_irq_restore(flags);
	}
	if (error & CLASS1_ENABLE_STORAGE_FAULT_INTR) {
		if ((ret = spu_handle_mm_fault(spu)) != 0)
			error |= CLASS1_ENABLE_STORAGE_FAULT_INTR;
		else
			error &= ~CLASS1_ENABLE_STORAGE_FAULT_INTR;
	}
	spu->dar = 0UL;
	spu->dsisr = 0UL;
	if (!error) {
		spu_restart_dma(spu);
	} else {
		spu->dma_callback(spu, SPE_EVENT_SPE_DATA_STORAGE);
	}
	return ret;
}

struct sysdev_class spu_sysdev_class = {
	set_kset_name("spu")
};

int spu_add_sysdev_attr(struct sysdev_attribute *attr)
{
	struct spu *spu;
	mutex_lock(&spu_mutex);

	list_for_each_entry(spu, &spu_full_list, full_list)
		sysdev_create_file(&spu->sysdev, attr);

	mutex_unlock(&spu_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(spu_add_sysdev_attr);

int spu_add_sysdev_attr_group(struct attribute_group *attrs)
{
	struct spu *spu;
	mutex_lock(&spu_mutex);

	list_for_each_entry(spu, &spu_full_list, full_list)
		sysfs_create_group(&spu->sysdev.kobj, attrs);

	mutex_unlock(&spu_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(spu_add_sysdev_attr_group);


void spu_remove_sysdev_attr(struct sysdev_attribute *attr)
{
	struct spu *spu;
	mutex_lock(&spu_mutex);

	list_for_each_entry(spu, &spu_full_list, full_list)
		sysdev_remove_file(&spu->sysdev, attr);

	mutex_unlock(&spu_mutex);
}
EXPORT_SYMBOL_GPL(spu_remove_sysdev_attr);

void spu_remove_sysdev_attr_group(struct attribute_group *attrs)
{
	struct spu *spu;
	mutex_lock(&spu_mutex);

	list_for_each_entry(spu, &spu_full_list, full_list)
		sysfs_remove_group(&spu->sysdev.kobj, attrs);

	mutex_unlock(&spu_mutex);
}
EXPORT_SYMBOL_GPL(spu_remove_sysdev_attr_group);

static int spu_create_sysdev(struct spu *spu)
{
	int ret;

	spu->sysdev.id = spu->number;
	spu->sysdev.cls = &spu_sysdev_class;
	ret = sysdev_register(&spu->sysdev);
	if (ret) {
		printk(KERN_ERR "Can't register SPU %d with sysfs\n",
				spu->number);
		return ret;
	}

	sysfs_add_device_to_node(&spu->sysdev, spu->node);

	return 0;
}

static void spu_destroy_sysdev(struct spu *spu)
{
	sysfs_remove_device_from_node(&spu->sysdev, spu->node);
	sysdev_unregister(&spu->sysdev);
}

static int __init create_spu(void *data)
{
	struct spu *spu;
	int ret;
	static int number;

	ret = -ENOMEM;
	spu = kzalloc(sizeof (*spu), GFP_KERNEL);
	if (!spu)
		goto out;

	spin_lock_init(&spu->register_lock);
	mutex_lock(&spu_mutex);
	spu->number = number++;
	mutex_unlock(&spu_mutex);

	ret = spu_create_spu(spu, data);

	if (ret)
		goto out_free;

	spu_mfc_sdr_setup(spu);
	spu_mfc_sr1_set(spu, 0x33);
	ret = spu_request_irqs(spu);
	if (ret)
		goto out_destroy;

	ret = spu_create_sysdev(spu);
	if (ret)
		goto out_free_irqs;

	mutex_lock(&spu_mutex);
	list_add(&spu->list, &spu_list[spu->node]);
	list_add(&spu->full_list, &spu_full_list);
	mutex_unlock(&spu_mutex);

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

static void destroy_spu(struct spu *spu)
{
	list_del_init(&spu->list);
	list_del_init(&spu->full_list);

	spu_destroy_sysdev(spu);
	spu_free_irqs(spu);
	spu_destroy_spu(spu);
	kfree(spu);
}

static void cleanup_spu_base(void)
{
	struct spu *spu, *tmp;
	int node;

	mutex_lock(&spu_mutex);
	for (node = 0; node < MAX_NUMNODES; node++) {
		list_for_each_entry_safe(spu, tmp, &spu_list[node], list)
			destroy_spu(spu);
	}
	mutex_unlock(&spu_mutex);
	sysdev_class_unregister(&spu_sysdev_class);
}
module_exit(cleanup_spu_base);

static int __init init_spu_base(void)
{
	int i, ret;

	/* create sysdev class for spus */
	ret = sysdev_class_register(&spu_sysdev_class);
	if (ret)
		return ret;

	for (i = 0; i < MAX_NUMNODES; i++)
		INIT_LIST_HEAD(&spu_list[i]);

	ret = spu_enumerate_spus(create_spu);

	if (ret) {
		printk(KERN_WARNING "%s: Error initializing spus\n",
			__FUNCTION__);
		cleanup_spu_base();
		return ret;
	}

	xmon_register_spus(&spu_full_list);

	return ret;
}
module_init(init_spu_base);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnd Bergmann <arndb@de.ibm.com>");
