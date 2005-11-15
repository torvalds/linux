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

#define DEBUG 1

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/semaphore.h>
#include <asm/spu.h>
#include <asm/mmu_context.h>

#include "interrupt.h"

static int __spu_trap_invalid_dma(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	force_sig(SIGBUS, /* info, */ current);
	return 0;
}

static int __spu_trap_dma_align(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	force_sig(SIGBUS, /* info, */ current);
	return 0;
}

static int __spu_trap_error(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	force_sig(SIGILL, /* info, */ current);
	return 0;
}

static void spu_restart_dma(struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	out_be64(&priv2->mfc_control_RW, MFC_CNTL_RESTART_DMA_COMMAND);
}

static int __spu_trap_data_seg(struct spu *spu, unsigned long ea)
{
	struct spu_priv2 __iomem *priv2;
	struct mm_struct *mm;

	pr_debug("%s\n", __FUNCTION__);

	if (REGION_ID(ea) != USER_REGION_ID) {
		pr_debug("invalid region access at %016lx\n", ea);
		return 1;
	}

	priv2 = spu->priv2;
	mm = spu->mm;

	if (spu->slb_replace >= 8)
		spu->slb_replace = 0;

	out_be64(&priv2->slb_index_W, spu->slb_replace);
	out_be64(&priv2->slb_vsid_RW,
		(get_vsid(mm->context.id, ea) << SLB_VSID_SHIFT)
						 | SLB_VSID_USER);
	out_be64(&priv2->slb_esid_RW, (ea & ESID_MASK) | SLB_ESID_V);

	spu_restart_dma(spu);

	pr_debug("set slb %d context %lx, ea %016lx, vsid %016lx, esid %016lx\n",
		spu->slb_replace, mm->context.id, ea,
		(get_vsid(mm->context.id, ea) << SLB_VSID_SHIFT)| SLB_VSID_USER,
		 (ea & ESID_MASK) | SLB_ESID_V);
	return 0;
}

static int __spu_trap_data_map(struct spu *spu, unsigned long ea)
{
	unsigned long dsisr;
	struct spu_priv1 __iomem *priv1;

	pr_debug("%s\n", __FUNCTION__);
	priv1 = spu->priv1;
	dsisr = in_be64(&priv1->mfc_dsisr_RW);

	wake_up(&spu->stop_wq);

	return 0;
}

static int __spu_trap_mailbox(struct spu *spu)
{
	wake_up_all(&spu->ibox_wq);
	kill_fasync(&spu->ibox_fasync, SIGIO, POLLIN);

	/* atomically disable SPU mailbox interrupts */
	spin_lock(&spu->register_lock);
	out_be64(&spu->priv1->int_mask_class2_RW,
		in_be64(&spu->priv1->int_mask_class2_RW) & ~0x1);
	spin_unlock(&spu->register_lock);
	return 0;
}

static int __spu_trap_stop(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	spu->stop_code = in_be32(&spu->problem->spu_status_R);
	wake_up(&spu->stop_wq);
	return 0;
}

static int __spu_trap_halt(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	spu->stop_code = in_be32(&spu->problem->spu_status_R);
	wake_up(&spu->stop_wq);
	return 0;
}

static int __spu_trap_tag_group(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	/* wake_up(&spu->dma_wq); */
	return 0;
}

static int __spu_trap_spubox(struct spu *spu)
{
	wake_up_all(&spu->wbox_wq);
	kill_fasync(&spu->wbox_fasync, SIGIO, POLLOUT);

	/* atomically disable SPU mailbox interrupts */
	spin_lock(&spu->register_lock);
	out_be64(&spu->priv1->int_mask_class2_RW,
		in_be64(&spu->priv1->int_mask_class2_RW) & ~0x10);
	spin_unlock(&spu->register_lock);
	return 0;
}

static irqreturn_t
spu_irq_class_0(int irq, void *data, struct pt_regs *regs)
{
	struct spu *spu;

	spu = data;
	spu->class_0_pending = 1;
	wake_up(&spu->stop_wq);

	return IRQ_HANDLED;
}

static int
spu_irq_class_0_bottom(struct spu *spu)
{
	unsigned long stat;

	spu->class_0_pending = 0;

	stat = in_be64(&spu->priv1->int_stat_class0_RW);

	if (stat & 1) /* invalid MFC DMA */
		__spu_trap_invalid_dma(spu);

	if (stat & 2) /* invalid DMA alignment */
		__spu_trap_dma_align(spu);

	if (stat & 4) /* error on SPU */
		__spu_trap_error(spu);

	out_be64(&spu->priv1->int_stat_class0_RW, stat);
	return 0;
}

static irqreturn_t
spu_irq_class_1(int irq, void *data, struct pt_regs *regs)
{
	struct spu *spu;
	unsigned long stat, dar;

	spu = data;
	stat  = in_be64(&spu->priv1->int_stat_class1_RW);
	dar   = in_be64(&spu->priv1->mfc_dar_RW);

	if (stat & 1) /* segment fault */
		__spu_trap_data_seg(spu, dar);

	if (stat & 2) { /* mapping fault */
		__spu_trap_data_map(spu, dar);
	}

	if (stat & 4) /* ls compare & suspend on get */
		;

	if (stat & 8) /* ls compare & suspend on put */
		;

	out_be64(&spu->priv1->int_stat_class1_RW, stat);
	return stat ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t
spu_irq_class_2(int irq, void *data, struct pt_regs *regs)
{
	struct spu *spu;
	unsigned long stat;

	spu = data;
	stat = in_be64(&spu->priv1->int_stat_class2_RW);

	pr_debug("class 2 interrupt %d, %lx, %lx\n", irq, stat,
		in_be64(&spu->priv1->int_mask_class2_RW));


	if (stat & 1)  /* PPC core mailbox */
		__spu_trap_mailbox(spu);

	if (stat & 2) /* SPU stop-and-signal */
		__spu_trap_stop(spu);

	if (stat & 4) /* SPU halted */
		__spu_trap_halt(spu);

	if (stat & 8) /* DMA tag group complete */
		__spu_trap_tag_group(spu);

	if (stat & 0x10) /* SPU mailbox threshold */
		__spu_trap_spubox(spu);

	out_be64(&spu->priv1->int_stat_class2_RW, stat);
	return stat ? IRQ_HANDLED : IRQ_NONE;
}

static int
spu_request_irqs(struct spu *spu)
{
	int ret;
	int irq_base;

	irq_base = IIC_NODE_STRIDE * spu->node + IIC_SPE_OFFSET;

	snprintf(spu->irq_c0, sizeof (spu->irq_c0), "spe%02d.0", spu->number);
	ret = request_irq(irq_base + spu->isrc,
		 spu_irq_class_0, 0, spu->irq_c0, spu);
	if (ret)
		goto out;
	out_be64(&spu->priv1->int_mask_class0_RW, 0x7);

	snprintf(spu->irq_c1, sizeof (spu->irq_c1), "spe%02d.1", spu->number);
	ret = request_irq(irq_base + IIC_CLASS_STRIDE + spu->isrc,
		 spu_irq_class_1, 0, spu->irq_c1, spu);
	if (ret)
		goto out1;
	out_be64(&spu->priv1->int_mask_class1_RW, 0x3);

	snprintf(spu->irq_c2, sizeof (spu->irq_c2), "spe%02d.2", spu->number);
	ret = request_irq(irq_base + 2*IIC_CLASS_STRIDE + spu->isrc,
		 spu_irq_class_2, 0, spu->irq_c2, spu);
	if (ret)
		goto out2;
	out_be64(&spu->priv1->int_mask_class2_RW, 0xe);
	goto out;

out2:
	free_irq(irq_base + IIC_CLASS_STRIDE + spu->isrc, spu);
out1:
	free_irq(irq_base + spu->isrc, spu);
out:
	return ret;
}

static void
spu_free_irqs(struct spu *spu)
{
	int irq_base;

	irq_base = IIC_NODE_STRIDE * spu->node + IIC_SPE_OFFSET;

	free_irq(irq_base + spu->isrc, spu);
	free_irq(irq_base + IIC_CLASS_STRIDE + spu->isrc, spu);
	free_irq(irq_base + 2*IIC_CLASS_STRIDE + spu->isrc, spu);
}

static LIST_HEAD(spu_list);
static DECLARE_MUTEX(spu_mutex);

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
	struct spu_priv2 *priv2;
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

static void spu_init_regs(struct spu *spu)
{
	out_be64(&spu->priv1->int_mask_class0_RW, 0x7);
	out_be64(&spu->priv1->int_mask_class1_RW, 0x3);
	out_be64(&spu->priv1->int_mask_class2_RW, 0xe);
}

struct spu *spu_alloc(void)
{
	struct spu *spu;

	down(&spu_mutex);
	if (!list_empty(&spu_list)) {
		spu = list_entry(spu_list.next, struct spu, list);
		list_del_init(&spu->list);
		pr_debug("Got SPU %x %d\n", spu->isrc, spu->number);
	} else {
		pr_debug("No SPU left\n");
		spu = NULL;
	}
	up(&spu_mutex);

	if (spu) {
		spu_init_channels(spu);
		spu_init_regs(spu);
	}

	return spu;
}
EXPORT_SYMBOL(spu_alloc);

void spu_free(struct spu *spu)
{
	down(&spu_mutex);
	spu->ibox_fasync = NULL;
	spu->wbox_fasync = NULL;
	list_add_tail(&spu->list, &spu_list);
	up(&spu_mutex);
}
EXPORT_SYMBOL(spu_free);

extern int hash_page(unsigned long ea, unsigned long access, unsigned long trap); //XXX
static int spu_handle_mm_fault(struct spu *spu)
{
	struct spu_priv1 __iomem *priv1;
	struct mm_struct *mm = spu->mm;
	struct vm_area_struct *vma;
	u64 ea, dsisr, is_write;
	int ret;

	priv1 = spu->priv1;
	ea = in_be64(&priv1->mfc_dar_RW);
	dsisr = in_be64(&priv1->mfc_dsisr_RW);
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

static int spu_handle_pte_fault(struct spu *spu)
{
	struct spu_priv1 __iomem *priv1;
	u64 ea, dsisr, access, error = 0UL;
	int ret = 0;

	priv1 = spu->priv1;
	ea = in_be64(&priv1->mfc_dar_RW);
	dsisr = in_be64(&priv1->mfc_dsisr_RW);
	access = (_PAGE_PRESENT | _PAGE_USER);
	if (dsisr & MFC_DSISR_PTE_NOT_FOUND) {
		if (hash_page(ea, access, 0x300) != 0)
			error |= CLASS1_ENABLE_STORAGE_FAULT_INTR;
	}
	if ((error & CLASS1_ENABLE_STORAGE_FAULT_INTR) ||
	    (dsisr & MFC_DSISR_ACCESS_DENIED)) {
		if ((ret = spu_handle_mm_fault(spu)) != 0)
			error |= CLASS1_ENABLE_STORAGE_FAULT_INTR;
		else
			error &= ~CLASS1_ENABLE_STORAGE_FAULT_INTR;
	}
	if (!error)
		spu_restart_dma(spu);

	return ret;
}

int spu_run(struct spu *spu)
{
	struct spu_problem __iomem *prob;
	struct spu_priv1 __iomem *priv1;
	struct spu_priv2 __iomem *priv2;
	unsigned long status;
	int ret;

	prob = spu->problem;
	priv1 = spu->priv1;
	priv2 = spu->priv2;

	/* Let SPU run.  */
	spu->mm = current->mm;
	eieio();
	out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_RUNNABLE);

	do {
		ret = wait_event_interruptible(spu->stop_wq,
			 (!((status = in_be32(&prob->spu_status_R)) & 0x1))
			|| (in_be64(&priv1->mfc_dsisr_RW) & MFC_DSISR_PTE_NOT_FOUND)
			|| spu->class_0_pending);

		if (status & SPU_STATUS_STOPPED_BY_STOP)
			ret = -EAGAIN;
		else if (status & SPU_STATUS_STOPPED_BY_HALT)
			ret = -EIO;
		else if (in_be64(&priv1->mfc_dsisr_RW) & MFC_DSISR_PTE_NOT_FOUND)
			ret = spu_handle_pte_fault(spu);

		if (spu->class_0_pending)
			spu_irq_class_0_bottom(spu);

		if (!ret && signal_pending(current))
			ret = -ERESTARTSYS;

	} while (!ret);

	/* Ensure SPU is stopped.  */
	out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_STOP);
	eieio();
	while (in_be32(&prob->spu_status_R) & SPU_STATUS_RUNNING)
		cpu_relax();

	out_be64(&priv2->slb_invalidate_all_W, 0);
	out_be64(&priv1->tlb_invalidate_entry_W, 0UL);
	eieio();

	spu->mm = NULL;

	/* Check for SPU breakpoint.  */
	if (unlikely(current->ptrace & PT_PTRACED)) {
		status = in_be32(&prob->spu_status_R);

		if ((status & SPU_STATUS_STOPPED_BY_STOP)
		    && status >> SPU_STOP_STATUS_SHIFT == 0x3fff) {
			force_sig(SIGTRAP, current);
			ret = -ERESTARTSYS;
		}
	}

	return ret;
}
EXPORT_SYMBOL(spu_run);

static void __iomem * __init map_spe_prop(struct device_node *n,
						 const char *name)
{
	struct address_prop {
		unsigned long address;
		unsigned int len;
	} __attribute__((packed)) *prop;

	void *p;
	int proplen;

	p = get_property(n, name, &proplen);
	if (proplen != sizeof (struct address_prop))
		return NULL;

	prop = p;

	return ioremap(prop->address, prop->len);
}

static void spu_unmap(struct spu *spu)
{
	iounmap(spu->priv2);
	iounmap(spu->priv1);
	iounmap(spu->problem);
	iounmap((u8 __iomem *)spu->local_store);
}

static int __init spu_map_device(struct spu *spu, struct device_node *spe)
{
	char *prop;
	int ret;

	ret = -ENODEV;
	prop = get_property(spe, "isrc", NULL);
	if (!prop)
		goto out;
	spu->isrc = *(unsigned int *)prop;

	spu->name = get_property(spe, "name", NULL);
	if (!spu->name)
		goto out;

	prop = get_property(spe, "local-store", NULL);
	if (!prop)
		goto out;
	spu->local_store_phys = *(unsigned long *)prop;

	/* we use local store as ram, not io memory */
	spu->local_store = (void __force *)map_spe_prop(spe, "local-store");
	if (!spu->local_store)
		goto out;

	spu->problem= map_spe_prop(spe, "problem");
	if (!spu->problem)
		goto out_unmap;

	spu->priv1= map_spe_prop(spe, "priv1");
	if (!spu->priv1)
		goto out_unmap;

	spu->priv2= map_spe_prop(spe, "priv2");
	if (!spu->priv2)
		goto out_unmap;
	ret = 0;
	goto out;

out_unmap:
	spu_unmap(spu);
out:
	return ret;
}

static int __init find_spu_node_id(struct device_node *spe)
{
	unsigned int *id;
	struct device_node *cpu;

	cpu = spe->parent->parent;
	id = (unsigned int *)get_property(cpu, "node-id", NULL);

	return id ? *id : 0;
}

static int __init create_spu(struct device_node *spe)
{
	struct spu *spu;
	int ret;
	static int number;

	ret = -ENOMEM;
	spu = kmalloc(sizeof (*spu), GFP_KERNEL);
	if (!spu)
		goto out;

	ret = spu_map_device(spu, spe);
	if (ret)
		goto out_free;

	spu->node = find_spu_node_id(spe);
	spu->stop_code = 0;
	spu->slb_replace = 0;
	spu->mm = NULL;
	spu->class_0_pending = 0;
	spin_lock_init(&spu->register_lock);

	out_be64(&spu->priv1->mfc_sdr_RW, mfspr(SPRN_SDR1));
	out_be64(&spu->priv1->mfc_sr1_RW, 0x33);

	init_waitqueue_head(&spu->stop_wq);
	init_waitqueue_head(&spu->wbox_wq);
	init_waitqueue_head(&spu->ibox_wq);

	spu->ibox_fasync = NULL;
	spu->wbox_fasync = NULL;

	down(&spu_mutex);
	spu->number = number++;
	ret = spu_request_irqs(spu);
	if (ret)
		goto out_unmap;

	list_add(&spu->list, &spu_list);
	up(&spu_mutex);

	pr_debug(KERN_DEBUG "Using SPE %s %02x %p %p %p %p %d\n",
		spu->name, spu->isrc, spu->local_store,
		spu->problem, spu->priv1, spu->priv2, spu->number);
	goto out;

out_unmap:
	up(&spu_mutex);
	spu_unmap(spu);
out_free:
	kfree(spu);
out:
	return ret;
}

static void destroy_spu(struct spu *spu)
{
	list_del_init(&spu->list);

	spu_free_irqs(spu);
	spu_unmap(spu);
	kfree(spu);
}

static void cleanup_spu_base(void)
{
	struct spu *spu, *tmp;
	down(&spu_mutex);
	list_for_each_entry_safe(spu, tmp, &spu_list, list)
		destroy_spu(spu);
	up(&spu_mutex);
}
module_exit(cleanup_spu_base);

static int __init init_spu_base(void)
{
	struct device_node *node;
	int ret;

	ret = -ENODEV;
	for (node = of_find_node_by_type(NULL, "spe");
			node; node = of_find_node_by_type(node, "spe")) {
		ret = create_spu(node);
		if (ret) {
			printk(KERN_WARNING "%s: Error initializing %s\n",
				__FUNCTION__, node->name);
			cleanup_spu_base();
			break;
		}
	}
	/* in some old firmware versions, the spe is called 'spc', so we
	   look for that as well */
	for (node = of_find_node_by_type(NULL, "spc");
			node; node = of_find_node_by_type(node, "spc")) {
		ret = create_spu(node);
		if (ret) {
			printk(KERN_WARNING "%s: Error initializing %s\n",
				__FUNCTION__, node->name);
			cleanup_spu_base();
			break;
		}
	}
	return ret;
}
module_init(init_spu_base);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnd Bergmann <arndb@de.ibm.com>");
