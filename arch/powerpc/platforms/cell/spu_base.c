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
#include <linux/poll.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <linux/mutex.h>
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

	if (!test_bit(SPU_CONTEXT_SWITCH_PENDING, &spu->flags))
		out_be64(&priv2->mfc_control_RW, MFC_CNTL_RESTART_DMA_COMMAND);
}

static int __spu_trap_data_seg(struct spu *spu, unsigned long ea)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	struct mm_struct *mm = spu->mm;
	u64 esid, vsid;

	pr_debug("%s\n", __FUNCTION__);

	if (test_bit(SPU_CONTEXT_SWITCH_ACTIVE, &spu->flags)) {
		/* SLBs are pre-loaded for context switch, so
		 * we should never get here!
		 */
		printk("%s: invalid access during switch!\n", __func__);
		return 1;
	}
	if (!mm || (REGION_ID(ea) != USER_REGION_ID)) {
		/* Future: support kernel segments so that drivers
		 * can use SPUs.
		 */
		pr_debug("invalid region access at %016lx\n", ea);
		return 1;
	}

	esid = (ea & ESID_MASK) | SLB_ESID_V;
	vsid = (get_vsid(mm->context.id, ea) << SLB_VSID_SHIFT) | SLB_VSID_USER;
	if (in_hugepage_area(mm->context, ea))
		vsid |= SLB_VSID_L;

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
	if (spu->stop_callback)
		spu->stop_callback(spu);
	return 0;
}

static int __spu_trap_mailbox(struct spu *spu)
{
	if (spu->ibox_callback)
		spu->ibox_callback(spu);

	/* atomically disable SPU mailbox interrupts */
	spin_lock(&spu->register_lock);
	spu_int_mask_and(spu, 2, ~0x1);
	spin_unlock(&spu->register_lock);
	return 0;
}

static int __spu_trap_stop(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	spu->stop_code = in_be32(&spu->problem->spu_status_R);
	if (spu->stop_callback)
		spu->stop_callback(spu);
	return 0;
}

static int __spu_trap_halt(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	spu->stop_code = in_be32(&spu->problem->spu_status_R);
	if (spu->stop_callback)
		spu->stop_callback(spu);
	return 0;
}

static int __spu_trap_tag_group(struct spu *spu)
{
	pr_debug("%s\n", __FUNCTION__);
	spu->mfc_callback(spu);
	return 0;
}

static int __spu_trap_spubox(struct spu *spu)
{
	if (spu->wbox_callback)
		spu->wbox_callback(spu);

	/* atomically disable SPU mailbox interrupts */
	spin_lock(&spu->register_lock);
	spu_int_mask_and(spu, 2, ~0x10);
	spin_unlock(&spu->register_lock);
	return 0;
}

static irqreturn_t
spu_irq_class_0(int irq, void *data, struct pt_regs *regs)
{
	struct spu *spu;

	spu = data;
	spu->class_0_pending = 1;
	if (spu->stop_callback)
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

	if (stat & 1) /* invalid MFC DMA */
		__spu_trap_invalid_dma(spu);

	if (stat & 2) /* invalid DMA alignment */
		__spu_trap_dma_align(spu);

	if (stat & 4) /* error on SPU */
		__spu_trap_error(spu);

	spu_int_stat_clear(spu, 0, stat);

	return (stat & 0x7) ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(spu_irq_class_0_bottom);

static irqreturn_t
spu_irq_class_1(int irq, void *data, struct pt_regs *regs)
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
spu_irq_class_2(int irq, void *data, struct pt_regs *regs)
{
	struct spu *spu;
	unsigned long stat;
	unsigned long mask;

	spu = data;
	stat = spu_int_stat_get(spu, 2);
	mask = spu_int_mask_get(spu, 2);

	pr_debug("class 2 interrupt %d, %lx, %lx\n", irq, stat, mask);

	stat &= mask;

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

	spu_int_stat_clear(spu, 2, stat);
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
		 spu_irq_class_0, SA_INTERRUPT, spu->irq_c0, spu);
	if (ret)
		goto out;

	snprintf(spu->irq_c1, sizeof (spu->irq_c1), "spe%02d.1", spu->number);
	ret = request_irq(irq_base + IIC_CLASS_STRIDE + spu->isrc,
		 spu_irq_class_1, SA_INTERRUPT, spu->irq_c1, spu);
	if (ret)
		goto out1;

	snprintf(spu->irq_c2, sizeof (spu->irq_c2), "spe%02d.2", spu->number);
	ret = request_irq(irq_base + 2*IIC_CLASS_STRIDE + spu->isrc,
		 spu_irq_class_2, SA_INTERRUPT, spu->irq_c2, spu);
	if (ret)
		goto out2;
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

struct spu *spu_alloc(void)
{
	struct spu *spu;

	mutex_lock(&spu_mutex);
	if (!list_empty(&spu_list)) {
		spu = list_entry(spu_list.next, struct spu, list);
		list_del_init(&spu->list);
		pr_debug("Got SPU %x %d\n", spu->isrc, spu->number);
	} else {
		pr_debug("No SPU left\n");
		spu = NULL;
	}
	mutex_unlock(&spu_mutex);

	if (spu)
		spu_init_channels(spu);

	return spu;
}
EXPORT_SYMBOL_GPL(spu_alloc);

void spu_free(struct spu *spu)
{
	mutex_lock(&spu_mutex);
	list_add_tail(&spu->list, &spu_list);
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
		__spu_trap_invalid_dma(spu);
	}
	return ret;
}

void spu_irq_setaffinity(struct spu *spu, int cpu)
{
	u64 target = iic_get_target_id(cpu);
	u64 route = target << 48 | target << 32 | target << 16;
	spu_int_route_set(spu, route);
}
EXPORT_SYMBOL_GPL(spu_irq_setaffinity);

static int __init find_spu_node_id(struct device_node *spe)
{
	unsigned int *id;
	struct device_node *cpu;
	cpu = spe->parent->parent;
	id = (unsigned int *)get_property(cpu, "node-id", NULL);
	return id ? *id : 0;
}

static int __init cell_spuprop_present(struct spu *spu, struct device_node *spe,
		const char *prop)
{
	static DEFINE_MUTEX(add_spumem_mutex);

	struct address_prop {
		unsigned long address;
		unsigned int len;
	} __attribute__((packed)) *p;
	int proplen;

	unsigned long start_pfn, nr_pages;
	struct pglist_data *pgdata;
	struct zone *zone;
	int ret;

	p = (void*)get_property(spe, prop, &proplen);
	WARN_ON(proplen != sizeof (*p));

	start_pfn = p->address >> PAGE_SHIFT;
	nr_pages = ((unsigned long)p->len + PAGE_SIZE - 1) >> PAGE_SHIFT;

	pgdata = NODE_DATA(spu->nid);
	zone = pgdata->node_zones;

	/* XXX rethink locking here */
	mutex_lock(&add_spumem_mutex);
	ret = __add_pages(zone, start_pfn, nr_pages);
	mutex_unlock(&add_spumem_mutex);

	return ret;
}

static void __iomem * __init map_spe_prop(struct spu *spu,
		struct device_node *n, const char *name)
{
	struct address_prop {
		unsigned long address;
		unsigned int len;
	} __attribute__((packed)) *prop;

	void *p;
	int proplen;
	void* ret = NULL;
	int err = 0;

	p = get_property(n, name, &proplen);
	if (proplen != sizeof (struct address_prop))
		return NULL;

	prop = p;

	err = cell_spuprop_present(spu, n, name);
	if (err && (err != -EEXIST))
		goto out;

	ret = ioremap(prop->address, prop->len);

 out:
	return ret;
}

static void spu_unmap(struct spu *spu)
{
	iounmap(spu->priv2);
	iounmap(spu->priv1);
	iounmap(spu->problem);
	iounmap((u8 __iomem *)spu->local_store);
}

static int __init spu_map_device(struct spu *spu, struct device_node *node)
{
	char *prop;
	int ret;

	ret = -ENODEV;
	prop = get_property(node, "isrc", NULL);
	if (!prop)
		goto out;
	spu->isrc = *(unsigned int *)prop;

	spu->name = get_property(node, "name", NULL);
	if (!spu->name)
		goto out;

	prop = get_property(node, "local-store", NULL);
	if (!prop)
		goto out;
	spu->local_store_phys = *(unsigned long *)prop;

	/* we use local store as ram, not io memory */
	spu->local_store = (void __force *)
		map_spe_prop(spu, node, "local-store");
	if (!spu->local_store)
		goto out;

	prop = get_property(node, "problem", NULL);
	if (!prop)
		goto out_unmap;
	spu->problem_phys = *(unsigned long *)prop;

	spu->problem= map_spe_prop(spu, node, "problem");
	if (!spu->problem)
		goto out_unmap;

	spu->priv1= map_spe_prop(spu, node, "priv1");
	/* priv1 is not available on a hypervisor */

	spu->priv2= map_spe_prop(spu, node, "priv2");
	if (!spu->priv2)
		goto out_unmap;
	ret = 0;
	goto out;

out_unmap:
	spu_unmap(spu);
out:
	return ret;
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
	spu->nid = of_node_to_nid(spe);
	if (spu->nid == -1)
		spu->nid = 0;

	spu->stop_code = 0;
	spu->slb_replace = 0;
	spu->mm = NULL;
	spu->ctx = NULL;
	spu->rq = NULL;
	spu->pid = 0;
	spu->class_0_pending = 0;
	spu->flags = 0UL;
	spu->dar = 0UL;
	spu->dsisr = 0UL;
	spin_lock_init(&spu->register_lock);

	spu_mfc_sdr_set(spu, mfspr(SPRN_SDR1));
	spu_mfc_sr1_set(spu, 0x33);

	spu->ibox_callback = NULL;
	spu->wbox_callback = NULL;
	spu->stop_callback = NULL;
	spu->mfc_callback = NULL;

	mutex_lock(&spu_mutex);
	spu->number = number++;
	ret = spu_request_irqs(spu);
	if (ret)
		goto out_unmap;

	list_add(&spu->list, &spu_list);
	mutex_unlock(&spu_mutex);

	pr_debug(KERN_DEBUG "Using SPE %s %02x %p %p %p %p %d\n",
		spu->name, spu->isrc, spu->local_store,
		spu->problem, spu->priv1, spu->priv2, spu->number);
	goto out;

out_unmap:
	mutex_unlock(&spu_mutex);
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
	mutex_lock(&spu_mutex);
	list_for_each_entry_safe(spu, tmp, &spu_list, list)
		destroy_spu(spu);
	mutex_unlock(&spu_mutex);
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
