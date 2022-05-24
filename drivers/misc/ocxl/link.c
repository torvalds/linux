// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include <linux/sched/mm.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/mmu_context.h>
#include <linux/mmu_notifier.h>
#include <asm/copro.h>
#include <asm/pnv-ocxl.h>
#include <asm/xive.h>
#include <misc/ocxl.h>
#include "ocxl_internal.h"
#include "trace.h"


#define SPA_PASID_BITS		15
#define SPA_PASID_MAX		((1 << SPA_PASID_BITS) - 1)
#define SPA_PE_MASK		SPA_PASID_MAX
#define SPA_SPA_SIZE_LOG	22 /* Each SPA is 4 Mb */

#define SPA_CFG_SF		(1ull << (63-0))
#define SPA_CFG_TA		(1ull << (63-1))
#define SPA_CFG_HV		(1ull << (63-3))
#define SPA_CFG_UV		(1ull << (63-4))
#define SPA_CFG_XLAT_hpt	(0ull << (63-6)) /* Hashed page table (HPT) mode */
#define SPA_CFG_XLAT_roh	(2ull << (63-6)) /* Radix on HPT mode */
#define SPA_CFG_XLAT_ror	(3ull << (63-6)) /* Radix on Radix mode */
#define SPA_CFG_PR		(1ull << (63-49))
#define SPA_CFG_TC		(1ull << (63-54))
#define SPA_CFG_DR		(1ull << (63-59))

#define SPA_XSL_TF		(1ull << (63-3))  /* Translation fault */
#define SPA_XSL_S		(1ull << (63-38)) /* Store operation */

#define SPA_PE_VALID		0x80000000

struct ocxl_link;

struct pe_data {
	struct mm_struct *mm;
	/* callback to trigger when a translation fault occurs */
	void (*xsl_err_cb)(void *data, u64 addr, u64 dsisr);
	/* opaque pointer to be passed to the above callback */
	void *xsl_err_data;
	struct rcu_head rcu;
	struct ocxl_link *link;
	struct mmu_notifier mmu_notifier;
};

struct spa {
	struct ocxl_process_element *spa_mem;
	int spa_order;
	struct mutex spa_lock;
	struct radix_tree_root pe_tree; /* Maps PE handles to pe_data */
	char *irq_name;
	int virq;
	void __iomem *reg_dsisr;
	void __iomem *reg_dar;
	void __iomem *reg_tfc;
	void __iomem *reg_pe_handle;
	/*
	 * The following field are used by the memory fault
	 * interrupt handler. We can only have one interrupt at a
	 * time. The NPU won't raise another interrupt until the
	 * previous one has been ack'd by writing to the TFC register
	 */
	struct xsl_fault {
		struct work_struct fault_work;
		u64 pe;
		u64 dsisr;
		u64 dar;
		struct pe_data pe_data;
	} xsl_fault;
};

/*
 * A opencapi link can be used be by several PCI functions. We have
 * one link per device slot.
 *
 * A linked list of opencapi links should suffice, as there's a
 * limited number of opencapi slots on a system and lookup is only
 * done when the device is probed
 */
struct ocxl_link {
	struct list_head list;
	struct kref ref;
	int domain;
	int bus;
	int dev;
	void __iomem *arva;     /* ATSD register virtual address */
	spinlock_t atsd_lock;   /* to serialize shootdowns */
	atomic_t irq_available;
	struct spa *spa;
	void *platform_data;
};
static LIST_HEAD(links_list);
static DEFINE_MUTEX(links_list_lock);

enum xsl_response {
	CONTINUE,
	ADDRESS_ERROR,
	RESTART,
};


static void read_irq(struct spa *spa, u64 *dsisr, u64 *dar, u64 *pe)
{
	u64 reg;

	*dsisr = in_be64(spa->reg_dsisr);
	*dar = in_be64(spa->reg_dar);
	reg = in_be64(spa->reg_pe_handle);
	*pe = reg & SPA_PE_MASK;
}

static void ack_irq(struct spa *spa, enum xsl_response r)
{
	u64 reg = 0;

	/* continue is not supported */
	if (r == RESTART)
		reg = PPC_BIT(31);
	else if (r == ADDRESS_ERROR)
		reg = PPC_BIT(30);
	else
		WARN(1, "Invalid irq response %d\n", r);

	if (reg) {
		trace_ocxl_fault_ack(spa->spa_mem, spa->xsl_fault.pe,
				spa->xsl_fault.dsisr, spa->xsl_fault.dar, reg);
		out_be64(spa->reg_tfc, reg);
	}
}

static void xsl_fault_handler_bh(struct work_struct *fault_work)
{
	vm_fault_t flt = 0;
	unsigned long access, flags, inv_flags = 0;
	enum xsl_response r;
	struct xsl_fault *fault = container_of(fault_work, struct xsl_fault,
					fault_work);
	struct spa *spa = container_of(fault, struct spa, xsl_fault);

	int rc;

	/*
	 * We must release a reference on mm_users whenever exiting this
	 * function (taken in the memory fault interrupt handler)
	 */
	rc = copro_handle_mm_fault(fault->pe_data.mm, fault->dar, fault->dsisr,
				&flt);
	if (rc) {
		pr_debug("copro_handle_mm_fault failed: %d\n", rc);
		if (fault->pe_data.xsl_err_cb) {
			fault->pe_data.xsl_err_cb(
				fault->pe_data.xsl_err_data,
				fault->dar, fault->dsisr);
		}
		r = ADDRESS_ERROR;
		goto ack;
	}

	if (!radix_enabled()) {
		/*
		 * update_mmu_cache() will not have loaded the hash
		 * since current->trap is not a 0x400 or 0x300, so
		 * just call hash_page_mm() here.
		 */
		access = _PAGE_PRESENT | _PAGE_READ;
		if (fault->dsisr & SPA_XSL_S)
			access |= _PAGE_WRITE;

		if (get_region_id(fault->dar) != USER_REGION_ID)
			access |= _PAGE_PRIVILEGED;

		local_irq_save(flags);
		hash_page_mm(fault->pe_data.mm, fault->dar, access, 0x300,
			inv_flags);
		local_irq_restore(flags);
	}
	r = RESTART;
ack:
	mmput(fault->pe_data.mm);
	ack_irq(spa, r);
}

static irqreturn_t xsl_fault_handler(int irq, void *data)
{
	struct ocxl_link *link = (struct ocxl_link *) data;
	struct spa *spa = link->spa;
	u64 dsisr, dar, pe_handle;
	struct pe_data *pe_data;
	struct ocxl_process_element *pe;
	int pid;
	bool schedule = false;

	read_irq(spa, &dsisr, &dar, &pe_handle);
	trace_ocxl_fault(spa->spa_mem, pe_handle, dsisr, dar, -1);

	WARN_ON(pe_handle > SPA_PE_MASK);
	pe = spa->spa_mem + pe_handle;
	pid = be32_to_cpu(pe->pid);
	/* We could be reading all null values here if the PE is being
	 * removed while an interrupt kicks in. It's not supposed to
	 * happen if the driver notified the AFU to terminate the
	 * PASID, and the AFU waited for pending operations before
	 * acknowledging. But even if it happens, we won't find a
	 * memory context below and fail silently, so it should be ok.
	 */
	if (!(dsisr & SPA_XSL_TF)) {
		WARN(1, "Invalid xsl interrupt fault register %#llx\n", dsisr);
		ack_irq(spa, ADDRESS_ERROR);
		return IRQ_HANDLED;
	}

	rcu_read_lock();
	pe_data = radix_tree_lookup(&spa->pe_tree, pe_handle);
	if (!pe_data) {
		/*
		 * Could only happen if the driver didn't notify the
		 * AFU about PASID termination before removing the PE,
		 * or the AFU didn't wait for all memory access to
		 * have completed.
		 *
		 * Either way, we fail early, but we shouldn't log an
		 * error message, as it is a valid (if unexpected)
		 * scenario
		 */
		rcu_read_unlock();
		pr_debug("Unknown mm context for xsl interrupt\n");
		ack_irq(spa, ADDRESS_ERROR);
		return IRQ_HANDLED;
	}

	if (!pe_data->mm) {
		/*
		 * translation fault from a kernel context - an OpenCAPI
		 * device tried to access a bad kernel address
		 */
		rcu_read_unlock();
		pr_warn("Unresolved OpenCAPI xsl fault in kernel context\n");
		ack_irq(spa, ADDRESS_ERROR);
		return IRQ_HANDLED;
	}
	WARN_ON(pe_data->mm->context.id != pid);

	if (mmget_not_zero(pe_data->mm)) {
			spa->xsl_fault.pe = pe_handle;
			spa->xsl_fault.dar = dar;
			spa->xsl_fault.dsisr = dsisr;
			spa->xsl_fault.pe_data = *pe_data;
			schedule = true;
			/* mm_users count released by bottom half */
	}
	rcu_read_unlock();
	if (schedule)
		schedule_work(&spa->xsl_fault.fault_work);
	else
		ack_irq(spa, ADDRESS_ERROR);
	return IRQ_HANDLED;
}

static void unmap_irq_registers(struct spa *spa)
{
	pnv_ocxl_unmap_xsl_regs(spa->reg_dsisr, spa->reg_dar, spa->reg_tfc,
				spa->reg_pe_handle);
}

static int map_irq_registers(struct pci_dev *dev, struct spa *spa)
{
	return pnv_ocxl_map_xsl_regs(dev, &spa->reg_dsisr, &spa->reg_dar,
				&spa->reg_tfc, &spa->reg_pe_handle);
}

static int setup_xsl_irq(struct pci_dev *dev, struct ocxl_link *link)
{
	struct spa *spa = link->spa;
	int rc;
	int hwirq;

	rc = pnv_ocxl_get_xsl_irq(dev, &hwirq);
	if (rc)
		return rc;

	rc = map_irq_registers(dev, spa);
	if (rc)
		return rc;

	spa->irq_name = kasprintf(GFP_KERNEL, "ocxl-xsl-%x-%x-%x",
				link->domain, link->bus, link->dev);
	if (!spa->irq_name) {
		dev_err(&dev->dev, "Can't allocate name for xsl interrupt\n");
		rc = -ENOMEM;
		goto err_xsl;
	}
	/*
	 * At some point, we'll need to look into allowing a higher
	 * number of interrupts. Could we have an IRQ domain per link?
	 */
	spa->virq = irq_create_mapping(NULL, hwirq);
	if (!spa->virq) {
		dev_err(&dev->dev,
			"irq_create_mapping failed for translation interrupt\n");
		rc = -EINVAL;
		goto err_name;
	}

	dev_dbg(&dev->dev, "hwirq %d mapped to virq %d\n", hwirq, spa->virq);

	rc = request_irq(spa->virq, xsl_fault_handler, 0, spa->irq_name,
			link);
	if (rc) {
		dev_err(&dev->dev,
			"request_irq failed for translation interrupt: %d\n",
			rc);
		rc = -EINVAL;
		goto err_mapping;
	}
	return 0;

err_mapping:
	irq_dispose_mapping(spa->virq);
err_name:
	kfree(spa->irq_name);
err_xsl:
	unmap_irq_registers(spa);
	return rc;
}

static void release_xsl_irq(struct ocxl_link *link)
{
	struct spa *spa = link->spa;

	if (spa->virq) {
		free_irq(spa->virq, link);
		irq_dispose_mapping(spa->virq);
	}
	kfree(spa->irq_name);
	unmap_irq_registers(spa);
}

static int alloc_spa(struct pci_dev *dev, struct ocxl_link *link)
{
	struct spa *spa;

	spa = kzalloc(sizeof(struct spa), GFP_KERNEL);
	if (!spa)
		return -ENOMEM;

	mutex_init(&spa->spa_lock);
	INIT_RADIX_TREE(&spa->pe_tree, GFP_KERNEL);
	INIT_WORK(&spa->xsl_fault.fault_work, xsl_fault_handler_bh);

	spa->spa_order = SPA_SPA_SIZE_LOG - PAGE_SHIFT;
	spa->spa_mem = (struct ocxl_process_element *)
		__get_free_pages(GFP_KERNEL | __GFP_ZERO, spa->spa_order);
	if (!spa->spa_mem) {
		dev_err(&dev->dev, "Can't allocate Shared Process Area\n");
		kfree(spa);
		return -ENOMEM;
	}
	pr_debug("Allocated SPA for %x:%x:%x at %p\n", link->domain, link->bus,
		link->dev, spa->spa_mem);

	link->spa = spa;
	return 0;
}

static void free_spa(struct ocxl_link *link)
{
	struct spa *spa = link->spa;

	pr_debug("Freeing SPA for %x:%x:%x\n", link->domain, link->bus,
		link->dev);

	if (spa && spa->spa_mem) {
		free_pages((unsigned long) spa->spa_mem, spa->spa_order);
		kfree(spa);
		link->spa = NULL;
	}
}

static int alloc_link(struct pci_dev *dev, int PE_mask, struct ocxl_link **out_link)
{
	struct ocxl_link *link;
	int rc;

	link = kzalloc(sizeof(struct ocxl_link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	kref_init(&link->ref);
	link->domain = pci_domain_nr(dev->bus);
	link->bus = dev->bus->number;
	link->dev = PCI_SLOT(dev->devfn);
	atomic_set(&link->irq_available, MAX_IRQ_PER_LINK);
	spin_lock_init(&link->atsd_lock);

	rc = alloc_spa(dev, link);
	if (rc)
		goto err_free;

	rc = setup_xsl_irq(dev, link);
	if (rc)
		goto err_spa;

	/* platform specific hook */
	rc = pnv_ocxl_spa_setup(dev, link->spa->spa_mem, PE_mask,
				&link->platform_data);
	if (rc)
		goto err_xsl_irq;

	/* if link->arva is not defeined, MMIO registers are not used to
	 * generate TLB invalidate. PowerBus snooping is enabled.
	 * Otherwise, PowerBus snooping is disabled. TLB Invalidates are
	 * initiated using MMIO registers.
	 */
	pnv_ocxl_map_lpar(dev, mfspr(SPRN_LPID), 0, &link->arva);

	*out_link = link;
	return 0;

err_xsl_irq:
	release_xsl_irq(link);
err_spa:
	free_spa(link);
err_free:
	kfree(link);
	return rc;
}

static void free_link(struct ocxl_link *link)
{
	release_xsl_irq(link);
	free_spa(link);
	kfree(link);
}

int ocxl_link_setup(struct pci_dev *dev, int PE_mask, void **link_handle)
{
	int rc = 0;
	struct ocxl_link *link;

	mutex_lock(&links_list_lock);
	list_for_each_entry(link, &links_list, list) {
		/* The functions of a device all share the same link */
		if (link->domain == pci_domain_nr(dev->bus) &&
			link->bus == dev->bus->number &&
			link->dev == PCI_SLOT(dev->devfn)) {
			kref_get(&link->ref);
			*link_handle = link;
			goto unlock;
		}
	}
	rc = alloc_link(dev, PE_mask, &link);
	if (rc)
		goto unlock;

	list_add(&link->list, &links_list);
	*link_handle = link;
unlock:
	mutex_unlock(&links_list_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(ocxl_link_setup);

static void release_xsl(struct kref *ref)
{
	struct ocxl_link *link = container_of(ref, struct ocxl_link, ref);

	if (link->arva) {
		pnv_ocxl_unmap_lpar(link->arva);
		link->arva = NULL;
	}

	list_del(&link->list);
	/* call platform code before releasing data */
	pnv_ocxl_spa_release(link->platform_data);
	free_link(link);
}

void ocxl_link_release(struct pci_dev *dev, void *link_handle)
{
	struct ocxl_link *link = (struct ocxl_link *) link_handle;

	mutex_lock(&links_list_lock);
	kref_put(&link->ref, release_xsl);
	mutex_unlock(&links_list_lock);
}
EXPORT_SYMBOL_GPL(ocxl_link_release);

static void invalidate_range(struct mmu_notifier *mn,
			     struct mm_struct *mm,
			     unsigned long start, unsigned long end)
{
	struct pe_data *pe_data = container_of(mn, struct pe_data, mmu_notifier);
	struct ocxl_link *link = pe_data->link;
	unsigned long addr, pid, page_size = PAGE_SIZE;

	pid = mm->context.id;
	trace_ocxl_mmu_notifier_range(start, end, pid);

	spin_lock(&link->atsd_lock);
	for (addr = start; addr < end; addr += page_size)
		pnv_ocxl_tlb_invalidate(link->arva, pid, addr, page_size);
	spin_unlock(&link->atsd_lock);
}

static const struct mmu_notifier_ops ocxl_mmu_notifier_ops = {
	.invalidate_range = invalidate_range,
};

static u64 calculate_cfg_state(bool kernel)
{
	u64 state;

	state = SPA_CFG_DR;
	if (mfspr(SPRN_LPCR) & LPCR_TC)
		state |= SPA_CFG_TC;
	if (radix_enabled())
		state |= SPA_CFG_XLAT_ror;
	else
		state |= SPA_CFG_XLAT_hpt;
	state |= SPA_CFG_HV;
	if (kernel) {
		if (mfmsr() & MSR_SF)
			state |= SPA_CFG_SF;
	} else {
		state |= SPA_CFG_PR;
		if (!test_tsk_thread_flag(current, TIF_32BIT))
			state |= SPA_CFG_SF;
	}
	return state;
}

int ocxl_link_add_pe(void *link_handle, int pasid, u32 pidr, u32 tidr,
		u64 amr, u16 bdf, struct mm_struct *mm,
		void (*xsl_err_cb)(void *data, u64 addr, u64 dsisr),
		void *xsl_err_data)
{
	struct ocxl_link *link = (struct ocxl_link *) link_handle;
	struct spa *spa = link->spa;
	struct ocxl_process_element *pe;
	int pe_handle, rc = 0;
	struct pe_data *pe_data;

	BUILD_BUG_ON(sizeof(struct ocxl_process_element) != 128);
	if (pasid > SPA_PASID_MAX)
		return -EINVAL;

	mutex_lock(&spa->spa_lock);
	pe_handle = pasid & SPA_PE_MASK;
	pe = spa->spa_mem + pe_handle;

	if (pe->software_state) {
		rc = -EBUSY;
		goto unlock;
	}

	pe_data = kmalloc(sizeof(*pe_data), GFP_KERNEL);
	if (!pe_data) {
		rc = -ENOMEM;
		goto unlock;
	}

	pe_data->mm = mm;
	pe_data->xsl_err_cb = xsl_err_cb;
	pe_data->xsl_err_data = xsl_err_data;
	pe_data->link = link;
	pe_data->mmu_notifier.ops = &ocxl_mmu_notifier_ops;

	memset(pe, 0, sizeof(struct ocxl_process_element));
	pe->config_state = cpu_to_be64(calculate_cfg_state(pidr == 0));
	pe->pasid = cpu_to_be32(pasid << (31 - 19));
	pe->bdf = cpu_to_be16(bdf);
	pe->lpid = cpu_to_be32(mfspr(SPRN_LPID));
	pe->pid = cpu_to_be32(pidr);
	pe->tid = cpu_to_be32(tidr);
	pe->amr = cpu_to_be64(amr);
	pe->software_state = cpu_to_be32(SPA_PE_VALID);

	/*
	 * For user contexts, register a copro so that TLBIs are seen
	 * by the nest MMU. If we have a kernel context, TLBIs are
	 * already global.
	 */
	if (mm) {
		mm_context_add_copro(mm);
		if (link->arva) {
			/* Use MMIO registers for the TLB Invalidate
			 * operations.
			 */
			trace_ocxl_init_mmu_notifier(pasid, mm->context.id);
			mmu_notifier_register(&pe_data->mmu_notifier, mm);
		}
	}

	/*
	 * Barrier is to make sure PE is visible in the SPA before it
	 * is used by the device. It also helps with the global TLBI
	 * invalidation
	 */
	mb();
	radix_tree_insert(&spa->pe_tree, pe_handle, pe_data);

	/*
	 * The mm must stay valid for as long as the device uses it. We
	 * lower the count when the context is removed from the SPA.
	 *
	 * We grab mm_count (and not mm_users), as we don't want to
	 * end up in a circular dependency if a process mmaps its
	 * mmio, therefore incrementing the file ref count when
	 * calling mmap(), and forgets to unmap before exiting. In
	 * that scenario, when the kernel handles the death of the
	 * process, the file is not cleaned because unmap was not
	 * called, and the mm wouldn't be freed because we would still
	 * have a reference on mm_users. Incrementing mm_count solves
	 * the problem.
	 */
	if (mm)
		mmgrab(mm);
	trace_ocxl_context_add(current->pid, spa->spa_mem, pasid, pidr, tidr);
unlock:
	mutex_unlock(&spa->spa_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(ocxl_link_add_pe);

int ocxl_link_update_pe(void *link_handle, int pasid, __u16 tid)
{
	struct ocxl_link *link = (struct ocxl_link *) link_handle;
	struct spa *spa = link->spa;
	struct ocxl_process_element *pe;
	int pe_handle, rc;

	if (pasid > SPA_PASID_MAX)
		return -EINVAL;

	pe_handle = pasid & SPA_PE_MASK;
	pe = spa->spa_mem + pe_handle;

	mutex_lock(&spa->spa_lock);

	pe->tid = cpu_to_be32(tid);

	/*
	 * The barrier makes sure the PE is updated
	 * before we clear the NPU context cache below, so that the
	 * old PE cannot be reloaded erroneously.
	 */
	mb();

	/*
	 * hook to platform code
	 * On powerpc, the entry needs to be cleared from the context
	 * cache of the NPU.
	 */
	rc = pnv_ocxl_spa_remove_pe_from_cache(link->platform_data, pe_handle);
	WARN_ON(rc);

	mutex_unlock(&spa->spa_lock);
	return rc;
}

int ocxl_link_remove_pe(void *link_handle, int pasid)
{
	struct ocxl_link *link = (struct ocxl_link *) link_handle;
	struct spa *spa = link->spa;
	struct ocxl_process_element *pe;
	struct pe_data *pe_data;
	int pe_handle, rc;

	if (pasid > SPA_PASID_MAX)
		return -EINVAL;

	/*
	 * About synchronization with our memory fault handler:
	 *
	 * Before removing the PE, the driver is supposed to have
	 * notified the AFU, which should have cleaned up and make
	 * sure the PASID is no longer in use, including pending
	 * interrupts. However, there's no way to be sure...
	 *
	 * We clear the PE and remove the context from our radix
	 * tree. From that point on, any new interrupt for that
	 * context will fail silently, which is ok. As mentioned
	 * above, that's not expected, but it could happen if the
	 * driver or AFU didn't do the right thing.
	 *
	 * There could still be a bottom half running, but we don't
	 * need to wait/flush, as it is managing a reference count on
	 * the mm it reads from the radix tree.
	 */
	pe_handle = pasid & SPA_PE_MASK;
	pe = spa->spa_mem + pe_handle;

	mutex_lock(&spa->spa_lock);

	if (!(be32_to_cpu(pe->software_state) & SPA_PE_VALID)) {
		rc = -EINVAL;
		goto unlock;
	}

	trace_ocxl_context_remove(current->pid, spa->spa_mem, pasid,
				be32_to_cpu(pe->pid), be32_to_cpu(pe->tid));

	memset(pe, 0, sizeof(struct ocxl_process_element));
	/*
	 * The barrier makes sure the PE is removed from the SPA
	 * before we clear the NPU context cache below, so that the
	 * old PE cannot be reloaded erroneously.
	 */
	mb();

	/*
	 * hook to platform code
	 * On powerpc, the entry needs to be cleared from the context
	 * cache of the NPU.
	 */
	rc = pnv_ocxl_spa_remove_pe_from_cache(link->platform_data, pe_handle);
	WARN_ON(rc);

	pe_data = radix_tree_delete(&spa->pe_tree, pe_handle);
	if (!pe_data) {
		WARN(1, "Couldn't find pe data when removing PE\n");
	} else {
		if (pe_data->mm) {
			if (link->arva) {
				trace_ocxl_release_mmu_notifier(pasid,
								pe_data->mm->context.id);
				mmu_notifier_unregister(&pe_data->mmu_notifier,
							pe_data->mm);
				spin_lock(&link->atsd_lock);
				pnv_ocxl_tlb_invalidate(link->arva,
							pe_data->mm->context.id,
							0ull,
							PAGE_SIZE);
				spin_unlock(&link->atsd_lock);
			}
			mm_context_remove_copro(pe_data->mm);
			mmdrop(pe_data->mm);
		}
		kfree_rcu(pe_data, rcu);
	}
unlock:
	mutex_unlock(&spa->spa_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(ocxl_link_remove_pe);

int ocxl_link_irq_alloc(void *link_handle, int *hw_irq)
{
	struct ocxl_link *link = (struct ocxl_link *) link_handle;
	int irq;

	if (atomic_dec_if_positive(&link->irq_available) < 0)
		return -ENOSPC;

	irq = xive_native_alloc_irq();
	if (!irq) {
		atomic_inc(&link->irq_available);
		return -ENXIO;
	}

	*hw_irq = irq;
	return 0;
}
EXPORT_SYMBOL_GPL(ocxl_link_irq_alloc);

void ocxl_link_free_irq(void *link_handle, int hw_irq)
{
	struct ocxl_link *link = (struct ocxl_link *) link_handle;

	xive_native_free_irq(hw_irq);
	atomic_inc(&link->irq_available);
}
EXPORT_SYMBOL_GPL(ocxl_link_free_irq);
