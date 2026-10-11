// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, 2026 Arm Ltd.
 */

#include <kvm/arm_vgic.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xarray.h>
#include <asm/kvm_mmu.h>

#include "vgic.h"
#include "vgic-v5-tables.h"

#define irs_caps	kvm_vgic_global_state.vgic_v5_irs_caps

static struct vgic_v5_vmt *vmt_info;

/* Serialises IRS MMIO interaction (commands) and accesses to IRS tables. */
DEFINE_RAW_SPINLOCK(vgic_v5_irs_lock);

/* Serialises lazy installation of shared second-level VMTs. */
static DEFINE_MUTEX(vmt_l2_lock);

static DEFINE_XARRAY(vm_info);

/* Level 1 Virtual Machine Table Entry */
#define GICV5_VMTEL1E_VALID		BIT_ULL(0)
/* Note that there is no shift for the address by design */
#define GICV5_VMTEL1E_L2_ADDR		GENMASK(51, 12)

#define GICV5_VMTEL2E_SIZE		32ULL
/* An L2 table (two-level VMT) is ALWAYS 4kB! */
#define GICV5_VMT_L2_TABLE_SIZE		4096ULL
#define GICV5_VMT_L2_TABLE_ENTRIES	(GICV5_VMT_L2_TABLE_SIZE / GICV5_VMTEL2E_SIZE)

/*
 * As the L2 VMTE is a large data structure, we are splitting it into 4 parts.
 * We only mask and shift WITHIN each part for simplicity.
 */
/* First 64-bit chunk */
#define GICV5_VMTEL2E_VALID		BIT_ULL(0)
#define GICV5_VMTEL2E_VMD_ADDR_SHIFT	3ULL
#define GICV5_VMTEL2E_VMD_ADDR		GENMASK_ULL(55, 3)
/* Second 64-bit chunk */
#define GICV5_VMTEL2E_VPET_ADDR_SHIFT	3ULL
#define GICV5_VMTEL2E_VPET_ADDR		GENMASK_ULL(55, 3)
#define GICV5_VMTEL2E_VPE_ID_BITS	GENMASK_ULL(63, 59)
/* Third & fourth 64-bit chunks (the encodings are the same for each) */
#define GICV5_VMTEL2E_IST_VALID		BIT_ULL(0)
#define GICV5_VMTEL2E_IST_L2SZ		GENMASK_ULL(2, 1)
#define GICV5_VMTEL2E_IST_ADDR_SHIFT	6ULL
#define GICV5_VMTEL2E_IST_ADDR		GENMASK_ULL(55, 6)
#define GICV5_VMTEL2E_IST_ISTSZ		GENMASK_ULL(57, 56)
#define GICV5_VMTEL2E_IST_STRUCTURE	BIT_ULL(58)
#define GICV5_VMTEL2E_IST_ID_BITS	GENMASK_ULL(63, 59)

/* Virtual PE Table Entry */
#define GICV5_VPE_VALID			BIT_ULL(0)
/* Note that there is no shift for the address by design. */
#define GICV5_VPED_ADDR			GENMASK_ULL(55, 3)

/* L2 Interrupt State Table Entry */
#define GICV5_ISTL2E_PENDING		BIT(0)
#define GICV5_ISTL2E_ACTIVE		BIT(1)
#define GICV5_ISTL2E_HM			BIT(2)
#define GICV5_ISTL2E_ENABLE		BIT(3)
#define GICV5_ISTL2E_IRM		BIT(4)
#define GICV5_ISTL2E_HWU		GENMASK(10, 9)
#define GICV5_ISTL2E_PRIORITY		GENMASK(15, 11)
#define GICV5_ISTL2E_IAFFID		GENMASK(31, 16)

#define GICV5_ISTE_SIZE(istsz)		BIT((istsz) + 2)
#define GICV5_LINEAR_IST_SIZE(id_bits, istsz)	\
	(BIT(id_bits) * GICV5_ISTE_SIZE(istsz))

/*
 * The LPI and SPI configuration is stored in the 2nd and 3rd 64-bit chunks of
 * the VMTE (0-based). We call this a section here in an attempt to simplify the
 * code.
 */
#define GICV5_VMTEL2_LPI_SECTION	2
#define GICV5_VMTEL2_SPI_SECTION	3

struct vgic_v5_ist_desc {
	struct vgic_v5_vm_info	*vmi;
	void			*base;
	unsigned int		id_bits;
	unsigned int		istsz;
	unsigned int		l2sz;
	size_t			iste_size;
	bool			present;
};

struct vgic_v5_two_level_ist_shape {
	size_t	l1_entries;
	size_t	l2_entries;
};

struct vgic_v5_pending_irq {
	u32			irq;
	struct list_head	next;
};

static int vgic_v5_alloc_linear_ist(struct kvm *kvm, bool spi_ist,
				    unsigned int id_bits,
				    unsigned int istsz);
static int vgic_v5_alloc_l1_ist(struct kvm *kvm, unsigned int id_bits,
				unsigned int istsz, unsigned int l2_split);
static int vgic_v5_alloc_l2_ists(struct kvm *kvm, unsigned int id_bits,
				 unsigned int istsz, unsigned int l2_split);
static int vgic_v5_alloc_two_level_lpi_ist(struct kvm *kvm,
					   unsigned int id_bits,
					   unsigned int istsz,
					   unsigned int l2_split);
static int vgic_v5_linear_ist_free(struct kvm *kvm, bool spi);
static int vgic_v5_two_level_ist_free(struct kvm *kvm, bool spi);
static int vgic_v5_spi_ist_free(struct kvm *kvm);

/*
 * Our IRS might be coherent or non-coherent. If coherent, we can just emit a
 * DSB to ensure that we're in sync. However, when non-coherent, we need to
 * manage our cached data explicitly.
 *
 * This helper is used to handle both coherent and non-coherent IRSes, and
 * handles all combinations of cleaning and invalidating to the PoC.
 */
static void vgic_v5_clean_inval(void *va, size_t size)
{
	unsigned long base = (unsigned long)va;

	dsb(ishst);

	if (kvm_vgic_global_state.vgic_v5_irs_caps.non_coherent)
		dcache_clean_inval_poc(base, base + size);
}

static void vgic_v5_drain_pending_irqs(struct kvm *kvm,
				       struct vgic_v5_vm_info *vmi,
				       bool reinject)
{
	struct vgic_v5_pending_irq *pirq, *tmp;

	list_for_each_entry_safe(pirq, tmp, &vmi->pending_irqs, next) {
		if (reinject)
			kvm_call_hyp(__vgic_v5_vdpend, pirq->irq, true,
				     vgic_v5_vm_id(kvm));

		list_del(&pirq->next);
		kfree(pirq);
	}
}

/*
 * Create a linear VM Table. Directly using the number of entries supplied as
 * the size of an L2 VMTE (32 bytes) guarantees that our allocation is aligned per
 * the GICv5 requirements for the IRS_VMT_BASER.
 */
static int vgic_v5_alloc_vmt_linear(unsigned int num_entries)
{
	vmt_info->linear.vmt_base = kzalloc_objs(*vmt_info->linear.vmt_base,
						 num_entries);
	if (!vmt_info->linear.vmt_base)
		return -ENOMEM;

	vgic_v5_clean_inval(vmt_info->linear.vmt_base,
			    num_entries * sizeof(struct vmtl2_entry));

	return 0;
}

/*
 * Allocate the first level of a two-level VM table. The second-level VM tables
 * are allocated on demand (by vgic_v5_alloc_l2_vmt()).
 */
static int vgic_v5_alloc_vmt_two_level(unsigned int num_entries)
{
	/*
	 * Each L2 VMT array is always 4k-sized (covering 128 VMs). This is
	 * mandated by the GICv5 specification (GICv5 EAC0 Specification rule
	 * D_LSPBK). Hence, round up the number of entries to be at least 128
	 * (or the next highest power of two as we give the HW the number of VM
	 * ID bits).
	 */
	if (num_entries < GICV5_VMT_L2_TABLE_ENTRIES)
		num_entries = GICV5_VMT_L2_TABLE_ENTRIES;
	num_entries = roundup_pow_of_two(num_entries);

	vmt_info->l2.num_l1_ents = (num_entries / GICV5_VMT_L2_TABLE_ENTRIES);
	vmt_info->l2.vmt_base = kzalloc_objs(*vmt_info->l2.vmt_base,
					     vmt_info->l2.num_l1_ents);
	if (!vmt_info->l2.vmt_base)
		return -ENOMEM;

	vmt_info->l2.l2ptrs = kzalloc_objs(*vmt_info->l2.l2ptrs,
					   vmt_info->l2.num_l1_ents,
					   GFP_KERNEL);
	if (!vmt_info->l2.l2ptrs) {
		kfree(vmt_info->l2.vmt_base);
		return -ENOMEM;
	}

	vgic_v5_clean_inval(vmt_info->l2.vmt_base,
			    vmt_info->l2.num_l1_ents * sizeof(vmtl1_entry));

	return 0;
}

/*
 * Allocate a second level VMT, if required. This can be called eagerly, and
 * will only perform the allocation if required.
 */
static int vgic_v5_alloc_l2_vmt(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu0 = kvm_get_vcpu(kvm, 0);
	u32 vm_id = vgic_v5_vm_id(kvm);
	enum gicv5_vcpu_cmd cmd = VMT_L2_MAP;
	struct vmtl2_entry *l2_table;
	unsigned int l1_index;
	int ret;

	/* Nothing to do if we have linear tables! */
	if (!vmt_info->two_level)
		return 0;

	if (vm_id == VGIC_V5_VM_ID_INVAL)
		return -EINVAL;

	/*
	 * We have 4k-sized L2 tables - this is mandated by the spec for
	 * two-level VMTs (GICv5 EAC0 Specification rule D_LSPBK). This means
	 * that we have 128 entries per L1 VMTE.
	 */
	l1_index = vm_id / GICV5_VMT_L2_TABLE_ENTRIES;

	guard(mutex)(&vmt_l2_lock);

	/* Already valid? Great! */
	if (vmt_info->l2.l2ptrs[l1_index])
		return 0;

	l2_table = kzalloc_objs(*l2_table, GICV5_VMT_L2_TABLE_ENTRIES);
	if (!l2_table)
		return -ENOMEM;

	/* The VMT is shared between all VMs. */
	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		vgic_v5_clean_inval(l2_table, GICV5_VMT_L2_TABLE_SIZE);
		vgic_v5_clean_inval(vmt_info->l2.vmt_base + l1_index,
				    sizeof(vmtl1_entry));

		WRITE_ONCE(vmt_info->l2.vmt_base[l1_index],
			   cpu_to_le64(virt_to_phys(l2_table)));

		vgic_v5_clean_inval(vmt_info->l2.vmt_base + l1_index,
				    sizeof(vmtl1_entry));

	}

	/*
	 * VMAP in the L2 VMT via the IRS. We use any of the VM's CPUs as a
	 * conduit for interacting with the host's IRS. In the current case,
	 * this lets us resolve the VM ID to pass to the hardware.
	 */
	ret = irq_set_vcpu_affinity(vgic_v5_vpe_db(vcpu0), &cmd);

	/* We've failed to make the L2 VMT valid - things are very broken! */
	if (ret) {
		scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
			/* Remove the pointer from L1 table */
			WRITE_ONCE(vmt_info->l2.vmt_base[l1_index], 0);

			vgic_v5_clean_inval(vmt_info->l2.vmt_base + l1_index,
					    sizeof(vmtl1_entry));
		}

		kfree(l2_table);
		return ret;
	}

	vmt_info->l2.l2ptrs[l1_index] = l2_table;

	return 0;
}

/*
 * Allocate the top-level VMT. This can either be linear or two-level.
 */
int vgic_v5_vmt_allocate(unsigned int max_vpes)
{
	int ret;

	/* Allocate the tracking structure */
	vmt_info = kzalloc_obj(*vmt_info, GFP_KERNEL);
	if (!vmt_info)
		return -ENOMEM;

	ida_init(&vmt_info->vm_id_ida);
	vmt_info->max_vpes = max_vpes;
	vmt_info->vmd_size = vgic_v5_irs_vmd_size(&irs_caps);
	vmt_info->vped_size = vgic_v5_irs_vped_size(&irs_caps);
	vmt_info->two_level = vgic_v5_irs_two_level_vmt_support(&irs_caps);
	vmt_info->num_entries = vgic_v5_irs_max_vms(&irs_caps);

	if (vmt_info->two_level)
		ret = vgic_v5_alloc_vmt_two_level(vmt_info->num_entries);
	else
		ret = vgic_v5_alloc_vmt_linear(vmt_info->num_entries);

	/* If anything failed, free our tracking structure before returning */
	if (ret) {
		kfree(vmt_info);
		vmt_info = NULL;
	}

	return ret;
}

/*
 * Free the VMT and associated tracking structures. This isn't strictly expected
 * to be called in general operation, but instead exists for completeness.
 */
int vgic_v5_vmt_free(void)
{
	if (!vmt_info)
		return 0;

	if (!vmt_info->two_level) {
		kfree(vmt_info->linear.vmt_base);
	} else {
		/* Free the L2 tables; kfree(NULL) is safe */
		for (int i = 0; i < vmt_info->l2.num_l1_ents; ++i)
			kfree(vmt_info->l2.l2ptrs[i]);
		kfree(vmt_info->l2.l2ptrs);

		/* And now free the L1 table */
		kfree(vmt_info->l2.vmt_base);
	}

	ida_destroy(&vmt_info->vm_id_ida);
	kfree(vmt_info);
	vmt_info = NULL;

	return 0;
}

/*
 * Look up a VMT Entry by VM ID.
 */
static struct vmtl2_entry *vgic_v5_get_l2_vmte(u32 vm_id)
{
	unsigned int l1_index, l2_index;
	struct vmtl2_entry *l2_table;

	if (vm_id == VGIC_V5_VM_ID_INVAL)
		return ERR_PTR(-EINVAL);

	if (!vmt_info->two_level)
		return &vmt_info->linear.vmt_base[vm_id];

	l1_index = vm_id / GICV5_VMT_L2_TABLE_ENTRIES;
	l2_index = vm_id % GICV5_VMT_L2_TABLE_ENTRIES;

	if (l1_index >= vmt_info->l2.num_l1_ents)
		return ERR_PTR(-E2BIG);

	if (!vmt_info->l2.l2ptrs[l1_index])
		return ERR_PTR(-EINVAL);

	l2_table = vmt_info->l2.l2ptrs[l1_index];
	return &l2_table[l2_index];
}

/*
 * Zero a VMT Entry, and flush & invalidate to the PoC, if required.
 */
static int vgic_v5_reset_vmte(struct kvm *kvm)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vmtl2_entry *vmte;

	vmte = vgic_v5_get_l2_vmte(vm_id);
	if (IS_ERR(vmte))
		return PTR_ERR(vmte);

	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		/*
		 * The VMT is normal memory shared with the IRS. Invalidate before
		 * rewriting the entry so that cacheline-granular maintenance cannot
		 * later push stale data for neighbouring IRS-visible state back to
		 * memory.
		 */
		vgic_v5_clean_inval(vmte, sizeof(*vmte));

		/*
		 * Prevent the compiler from eliding the individual VMTE
		 * stores. Ordering and visibility to the IRS are provided by the
		 * surrounding cache maintenance and command protocol, not by
		 * WRITE_ONCE().
		 *
		 * The same compiler-access constraint applies to READ_ONCE() users in
		 * this file: when inspecting IRS-visible table entries, read the field
		 * exactly once and prevent the compiler from reusing, merging or
		 * tearing the access. Coherency and freshness for non-coherent IRSes
		 * still come from the surrounding cache maintenance.
		 */
		WRITE_ONCE(vmte->val[0], cpu_to_le64(0ULL));
		WRITE_ONCE(vmte->val[1], cpu_to_le64(0ULL));
		WRITE_ONCE(vmte->val[2], cpu_to_le64(0ULL));
		WRITE_ONCE(vmte->val[3], cpu_to_le64(0ULL));

		/* And make our write visible to the IRS (if non-coherent) */
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
	}

	return 0;
}

/*
 * Use the IDA to allocate a new VM ID, and track it in the gicv5_vm data
 * structure. If we're out of VM IDs, the IDA catches that, and we return the
 * error (-ENOSPC). If we've previously allocated a VM ID, we catch that too and
 * return -EBUSY.
 */
int vgic_v5_allocate_vm_id(struct kvm *kvm)
{
	int id;

	if (kvm->arch.vgic.gicv5_vm.vm_id != VGIC_V5_VM_ID_INVAL)
		return -EBUSY;

	id = ida_alloc_max(&vmt_info->vm_id_ida, vmt_info->num_entries - 1u,
			   GFP_KERNEL);
	if (id < 0)
		return id;

	kvm->arch.vgic.gicv5_vm.vm_id = id;

	return 0;
}

/*
 * Release the VM ID to allow it to be reallocated in the future.
 */
void vgic_v5_release_vm_id(struct kvm *kvm)
{
	if (kvm->arch.vgic.gicv5_vm.vm_id == VGIC_V5_VM_ID_INVAL)
		return;

	ida_free(&vmt_info->vm_id_ida, kvm->arch.vgic.gicv5_vm.vm_id);
	kvm->arch.vgic.gicv5_vm.vm_id = VGIC_V5_VM_ID_INVAL;
}

/*
 * Initialise an entry in the VMT based on the index of the VM.
 *
 * Note: We don't mark the VMTE as valid as this needs to be done by
 * the hardware.
 */
int vgic_v5_vmte_init(struct kvm *kvm)
{
	size_t vmd_alloc_size, vpet_alloc_size, vped_alloc_size;
	void *vped_base = NULL, *vmd = NULL;
	struct vgic_v5_vm_info *vmi = NULL;
	u64 tmp, vmte_val0 = 0, vmte_val1;
	u32 vm_id = vgic_v5_vm_id(kvm);
	int ret, nr_cpus, nr_vcpus;
	bool vmi_inserted = false;
	struct vmtl2_entry *vmte;
	vpe_entry *vpet = NULL;
	struct kvm_vcpu *vcpu;
	u16 max_vpe_id = 0;
	unsigned long i;

	nr_vcpus = atomic_read(&kvm->online_vcpus);
	if (nr_vcpus > vmt_info->max_vpes)
		return -E2BIG;

	/*
	 * If we're using two-level VMTs, L2 is allocated on demand. For linear
	 * VMTs, this is a NOP.
	 */
	ret = vgic_v5_alloc_l2_vmt(kvm);
	if (ret)
		return ret;

	vmte = vgic_v5_get_l2_vmte(vm_id);
	if (IS_ERR(vmte))
		return PTR_ERR(vmte);

	/* If the entry is already valid, something went wrong */
	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
		if (le64_to_cpu(READ_ONCE(vmte->val[0])) & GICV5_VMTEL2E_VALID)
			return -EINVAL;
	}

	ret = vgic_v5_reset_vmte(kvm);
	if (ret)
		return ret;

	vmi = kzalloc_obj(*vmi);
	if (!vmi) {
		ret = -ENOMEM;
		goto out_fail;
	}

	ret = xa_insert(&vm_info, vm_id, vmi, GFP_KERNEL);
	if (ret)
		goto out_fail;
	vmi_inserted = true;

	/*
	 * If we are restoring the state of a guest, we need to re-inject any
	 * IRQs that were pending when the state of the guest was originally
	 * saved. We use the pending_irqs list for this.
	 */
	INIT_LIST_HEAD(&vmi->pending_irqs);

	/* Allocate and assign the VM Descriptor, if required. */
	if (vmt_info->vmd_size != 0) {
		vmd_alloc_size = round_up(vmt_info->vmd_size,
					  dma_get_cache_alignment());
		vmd = kzalloc(vmd_alloc_size, GFP_KERNEL);
		if (!vmd) {
			ret = -ENOMEM;
			goto out_fail;
		}

		/* Stash the VA so we can free it later */
		vmi->vmd_base = vmd;

		tmp = FIELD_PREP(GICV5_VMTEL2E_VMD_ADDR,
				 virt_to_phys(vmd) >> GICV5_VMTEL2E_VMD_ADDR_SHIFT);
		vmte_val0 = tmp;
	}

	/*
	 * Allocate and assign the VPE Table.
	 *
	 * First of all, iterate over all vcpus to find the highest VPE ID we
	 * require - we need to ensure that we have enough storage for all
	 * vcpu_id values that userspace has picked and not just the total
	 * number of vcpus. This gives us the number of VPEs required for the
	 * VM.
	 *
	 * Round up the number of VPEs to a whole power of two as we cannot
	 * describe non-powers-of-two in the VMTE field as it conveys the number
	 * of ID bits used and not the number of vPEs. IRS_IDR1.IAFFID_BITS is
	 * encoded as N - 1, so expose at least one VPE ID bit even for a
	 * single-vCPU VM to keep the views consistent.
	 */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		u16 vpe_id = vgic_v5_vpe_id(vcpu);

		if (vpe_id > max_vpe_id)
			max_vpe_id = vpe_id;
	}

	nr_cpus = max(2UL, roundup_pow_of_two(max_vpe_id + 1));
	vmi->vpe_id_bits = fls(nr_cpus) - 1;

	vpet_alloc_size = round_up((size_t)nr_cpus * sizeof(*vpet),
				   dma_get_cache_alignment());
	vpet = kzalloc(vpet_alloc_size, GFP_KERNEL);
	if (!vpet) {
		ret = -ENOMEM;
		goto out_fail;
	}

	/* Stash the VA so we can free it later */
	vmi->vpet_base = vpet;

	tmp = FIELD_PREP(GICV5_VMTEL2E_VPET_ADDR,
			 virt_to_phys(vpet) >> GICV5_VMTEL2E_VPET_ADDR_SHIFT);
	tmp |= FIELD_PREP(GICV5_VMTEL2E_VPE_ID_BITS, vmi->vpe_id_bits);
	vmte_val1 = tmp;

	/*
	 * Allocate a dense VPED array indexed by vcpu_idx. The VPET is indexed
	 * by the potentially sparse vcpu_id, but using that ID here would waste
	 * memory. Given that this is not userspace visible, we can cheat a bit
	 * and use the dense index instead. This is the ONLY place that we do
	 * this.
	 *
	 * Round the requested size up to a whole cacheline. kzalloc() gurantees
	 * natural alignment, so we ensure that the cachelines cannot be shared
	 * with unrelated slab objects. VPED and cacheline sizes are powers of
	 * two, so this also preserves the required VPED alignment when a VPED
	 * is larger than a cacheline.
	 */
	vped_alloc_size = round_up((size_t)nr_vcpus * vmt_info->vped_size,
				   dma_get_cache_alignment());
	vped_base = kzalloc(vped_alloc_size, GFP_KERNEL);
	if (!vped_base) {
		ret = -ENOMEM;
		goto out_fail;
	}
	vmi->vped_base = vped_base;

	if (vmd)
		vgic_v5_clean_inval(vmd, vmd_alloc_size);
	vgic_v5_clean_inval(vpet, vpet_alloc_size);
	vgic_v5_clean_inval(vped_base, vped_alloc_size);

	/* Publish the VMTE while serialising access to the shared VMT. */
	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		WRITE_ONCE(vmte->val[0], cpu_to_le64(vmte_val0));
		WRITE_ONCE(vmte->val[1], cpu_to_le64(vmte_val1));
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
	}

	kvm->arch.vgic.gicv5_vm.vmte_allocated = true;

	return 0;

out_fail:
	/* kfree(NULL) is safe so we can just kfree() at leisure */
	kfree(vmd);
	kfree(vpet);
	kfree(vped_base);
	if (vmi_inserted)
		xa_erase(&vm_info, vm_id);
	kfree(vmi);

	vgic_v5_reset_vmte(kvm);

	return ret;
}

/*
 * Release the VMT Entry, freeing up any allocated data structures before
 * zeroing the VMTE.
 *
 * The VMTE must be marked as invalid before it is released.
 */
int vgic_v5_vmte_release(struct kvm *kvm)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;
	struct vmtl2_entry *vmte;
	int ret;

	vmte = vgic_v5_get_l2_vmte(vm_id);
	if (IS_ERR(vmte))
		return PTR_ERR(vmte);

	/* Reject if the VMTE has not been marked as invalid! */
	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
		if (le64_to_cpu(READ_ONCE(vmte->val[0])) & GICV5_VMTEL2E_VALID)
			return -EINVAL;
	}

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		goto no_vmi;

	/* Unlikely, but possible. Avoid leaking the memory. */
	vgic_v5_drain_pending_irqs(kvm, vmi, false);

	/* If we have an LPI IST, free it */
	if (vmi->h_lpi_ist) {
		ret = vgic_v5_lpi_ist_free(kvm);
		if (ret)
			return ret;
	}
	vmi->h_lpi_ist = NULL;

	/* If we have an SPI IST, free it */
	if (vmi->h_spi_ist) {
		ret = vgic_v5_spi_ist_free(kvm);
		if (ret)
			return ret;
	}
	vmi->h_spi_ist = NULL;

	kfree(vmi->vped_base);
	kfree(vmi->vpet_base);
	kfree(vmi->vmd_base);

	xa_erase(&vm_info, vm_id);
	kfree(vmi);

no_vmi:
	/*
	 * If we didn't get far enough into allocating a VMTE to create the VM
	 * info structure, then we just zero the VMTE and move on. There's
	 * nothing else we can realistically do here.
	 */
	ret = vgic_v5_reset_vmte(kvm);
	if (ret)
		return ret;

	kvm->arch.vgic.gicv5_vm.vmte_allocated = false;

	return 0;
}

/*
 * Provide a way for the IRS MMIO emulation to correctly populate the number of
 * IAFFID bits (which correspond to our vpe_id_bits.
 */
u8 vgic_v5_vmte_vpe_id_bits(struct kvm_vcpu *vcpu)
{
	u32 vm_id = vgic_v5_vm_id(vcpu->kvm);
	struct vgic_v5_vm_info *vmi;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return 0;

	return vmi->vpe_id_bits;
}

/* Provide the preallocated VPE descriptor to the hardware via the VPE Table. */
int vgic_v5_vmte_alloc_vpe(struct kvm_vcpu *vcpu)
{
	u32 vm_id = vgic_v5_vm_id(vcpu->kvm);
	u16 vpe_id = vgic_v5_vpe_id(vcpu);
	struct vgic_v5_vm_info *vmi;
	vpe_entry *vpet_base;
	void *vped;

	/* Make sure we're not over what the hardware supports */
	if (vpe_id >= vmt_info->max_vpes)
		return -E2BIG;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	if (vpe_id >= 1 << vmi->vpe_id_bits)
		return -E2BIG;

	vpet_base = vmi->vpet_base;

	/* If the VPETE for this CPU is already valid we've gone wrong */
	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		vgic_v5_clean_inval(&vpet_base[vpe_id], sizeof(*vpet_base));
		if (le64_to_cpu(READ_ONCE(vpet_base[vpe_id])) & GICV5_VPE_VALID)
			return -EBUSY;
	}

	vped = (u8 *)vmi->vped_base +
		(size_t)vcpu->vcpu_idx * vmt_info->vped_size;

	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		phys_addr_t paddr = virt_to_phys(vped) & GICV5_VPED_ADDR;
		WRITE_ONCE(vpet_base[vpe_id], cpu_to_le64(paddr));
		vgic_v5_clean_inval(vpet_base + vpe_id, sizeof(vpe_entry));
	}

	return 0;
}

/* Clear the VPE's table entry after the VMTE has been made invalid. */
int vgic_v5_vmte_free_vpe(struct kvm_vcpu *vcpu)
{
	u32 vm_id = vgic_v5_vm_id(vcpu->kvm);
	u16 vpe_id = vgic_v5_vpe_id(vcpu);
	struct vgic_v5_vm_info *vmi;
	struct vmtl2_entry *vmte;
	vpe_entry *vpet_base;

	vmte = vgic_v5_get_l2_vmte(vm_id);
	if (IS_ERR(vmte))
		return PTR_ERR(vmte);

	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
		if (le64_to_cpu(READ_ONCE(vmte->val[0])) & GICV5_VMTEL2E_VALID)
			return -EBUSY;
	}

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	if (vpe_id >= 1 << vmi->vpe_id_bits)
		return -E2BIG;

	vpet_base = vmi->vpet_base;
	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		WRITE_ONCE(vpet_base[vpe_id], 0ULL);
		vgic_v5_clean_inval(vpet_base + vpe_id, sizeof(vpe_entry));
	}

	return 0;
}

phys_addr_t vgic_v5_get_vmt_base(void)
{
	phys_addr_t vmt_base;

	if (!vmt_info->two_level)
		vmt_base = virt_to_phys(vmt_info->linear.vmt_base);
	else
		vmt_base = virt_to_phys(vmt_info->l2.vmt_base);

	return vmt_base;
}

/*
 * Assign an already allocated IST to the VM by populating the fields in the
 * corresponding VMTE. We re-use this code for both an SPI IST and LPI IST, even
 * if the paths to reach it might be vastly different.
 */
static int vgic_v5_vmte_assign_ist(struct kvm *kvm, phys_addr_t ist_base,
				   bool two_level, unsigned int id_bits,
				   unsigned int l2sz, unsigned int istsz,
				   bool spi_ist)
{
	struct kvm_vcpu *vcpu0 = kvm_get_vcpu(kvm, 0);
	u32 vm_id = vgic_v5_vm_id(kvm);
	enum gicv5_vcpu_cmd cmd;
	struct vmtl2_entry *vmte;
	unsigned int section;
	u64 tmp;
	int ret;

	/*
	 * The L2 VMTE comprises four 64-bit "sections", where sections 2 & 3
	 * describe the LPI and SPI ISTs, respectively. Both the LPI and SPI
	 * sections have the same layout, and as we are either operating on SPIs
	 * or LPIs we pick a section of the VMTE to modify up-front.
	 *
	 * See the GICv5 EAC0 Specification 11.2.2 for more details about the
	 * VMTE layout.
	 */
	section = spi_ist ? GICV5_VMTEL2_SPI_SECTION : GICV5_VMTEL2_LPI_SECTION;

	if (ist_base & ~GICV5_VMTEL2E_IST_ADDR) {
		pr_err_ratelimited("kvm [%i]: IST misaligned: address 0x%llx, mask 0x%llx\n",
				   task_pid_nr(current), ist_base,
				   GICV5_VMTEL2E_IST_ADDR);
		return -EINVAL;
	}

	vmte = vgic_v5_get_l2_vmte(vm_id);
	if (IS_ERR(vmte))
		return PTR_ERR(vmte);

	tmp = FIELD_PREP(GICV5_VMTEL2E_IST_L2SZ, l2sz);
	tmp |= FIELD_PREP(GICV5_VMTEL2E_IST_ADDR,
			ist_base >> GICV5_VMTEL2E_IST_ADDR_SHIFT);
	tmp |= FIELD_PREP(GICV5_VMTEL2E_IST_ISTSZ, istsz);
	tmp |= FIELD_PREP(GICV5_VMTEL2E_IST_ID_BITS, id_bits);
	if (two_level)
		tmp |= GICV5_VMTEL2E_IST_STRUCTURE;

	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		/* Bail if already allocated */
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
		if (le64_to_cpu(READ_ONCE(vmte->val[section])) &
		    GICV5_VMTEL2E_IST_VALID)
			return -EINVAL;

		WRITE_ONCE(vmte->val[section], cpu_to_le64(tmp));
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
	}

	/* Finally, mark the entry as valid */
	cmd = spi_ist ? SPI_VIST_MAKE_VALID : LPI_VIST_MAKE_VALID;
	ret = irq_set_vcpu_affinity(vgic_v5_vpe_db(vcpu0), &cmd);
	if (ret) {
		/*
		 * A timeout does not tell us whether the IRS consumed the
		 * command, so we're in some indeterminate and potentially
		 * transient state.  It may still make the VIST valid and access
		 * the backing memory, so leave both the VMTE and allocation
		 * intact. Killing the VM ensures that teardown is the only path
		 * that can reclaim them, after it has successfully made the
		 * complete VMTE invalid.
		 */
		if (ret == -ETIMEDOUT) {
			kvm_vm_dead(kvm);
			return ret;
		}

		scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
			WRITE_ONCE(vmte->val[section], 0ULL);
			vgic_v5_clean_inval(vmte, sizeof(*vmte));
		}

		return ret;
	}

	return 0;
}

/*
 * Allocate a Linear IST - always used for SPIs and potentially LPIs.
 *
 * The calculation for n has been taken from section 11.2.2 of the GICv5 EAC0
 * spec.
 *
 * NOTE: istsz is the FIELD used by GICv5, not the actual size (or log2() of the
 * size).
 */
static int vgic_v5_alloc_linear_ist(struct kvm *kvm, bool spi_ist,
				    unsigned int id_bits, unsigned int istsz)
{
	const size_t n = max(5, id_bits + 1 + istsz);
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;
	__le64 *ist;
	u32 l1sz;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	/*
	 * Allocate the IST. We only have one level, so we just use the L2 ISTE.
	 */
	l1sz = BIT(n + 1);
	ist = kzalloc(l1sz, GFP_KERNEL_ACCOUNT);
	if (!ist)
		return -ENOMEM;

	if (spi_ist) {
		vmi->h_spi_ist = ist;
	} else {
		vmi->h_lpi_ist_structure = false;
		vmi->h_lpi_ist = ist;
	}

	vgic_v5_clean_inval(ist, l1sz);

	return 0;
}

/*
 * Allocate the first level of a two-level IST - LPI, only.
 *
 * The calculation for n has been taken from section 11.2.2 of the GICv5 EAC0
 * spec.
 *
 * NOTE: istsz and l2sz are the FIELDS used by GICv5, not the actual sizes (or
 * log2() of the sizes).
 */
static int vgic_v5_alloc_l1_ist(struct kvm *kvm, unsigned int id_bits,
				unsigned int istsz, unsigned int l2sz)
{
	const size_t n =  max(5, id_bits - ((10 - istsz) + (2 * l2sz)) + 3 - 1);
	u32 vm_id = vgic_v5_vm_id(kvm);
	const u32 l1_size = BIT(n + 1);
	struct vgic_v5_vm_info *vmi;
	__le64 *ist;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	ist = kzalloc(l1_size, GFP_KERNEL_ACCOUNT);
	if (!ist)
		return -ENOMEM;

	vmi->h_lpi_ist_structure = true;
	vmi->h_lpi_ist = ist;

	vgic_v5_clean_inval(ist, l1_size);

	return 0;
}

/*
 * Allocate ALL of the second level ISTs for a two-level IST - LPI, only.
 *
 * The calculation for n has been taken from section 11.2.2 of the GICv5 EAC0
 * spec. The l2_size calculation is from section 11.2.3 of the same document.
 *
 * NOTE: istsz and l2sz are the FIELDS used by GICv5, not the actual sizes (or
 * log2() of the sizes).
 */
static int vgic_v5_alloc_l2_ists(struct kvm *kvm, unsigned int id_bits,
				 unsigned int istsz, unsigned int l2sz)
{
	const size_t n =  max(5, id_bits - ((10 - istsz) + (2 * l2sz)) + 3 - 1);
	const int l1_entries = BIT(n + 1) / GICV5_IRS_ISTL1E_SIZE;
	const size_t l2_size = BIT(11 + (2 * l2sz) + 1);
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;
	__le64 *l2ist;
	__le64 *l1ist;
	int index;
	u64 val;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	l1ist = vmi->h_lpi_ist;

	/*
	 * Allocate the storage for the pointers to the L2 ISTs (used when
	 * freeing later).
	 */
	vmi->h_lpi_l2_ists = kzalloc_objs(*vmi->h_lpi_l2_ists, l1_entries,
					  GFP_KERNEL_ACCOUNT);
	if (!vmi->h_lpi_l2_ists)
		return -ENOMEM;

	/* Allocate the L2 IST for each L1 IST entry */
	for (index = 0; index < l1_entries; ++index) {
		l2ist = kzalloc(l2_size, GFP_KERNEL_ACCOUNT);
		if (!l2ist) {
			while (--index >= 0)
				kfree(vmi->h_lpi_l2_ists[index]);

			kfree(vmi->h_lpi_l2_ists);
			vmi->h_lpi_l2_ists = NULL;

			return -ENOMEM;
		}

		/*
		 * We are not doing on-demand allocation of the L2 ISTs, and are
		 * instead provisioning the whole IST up front. This means that
		 * we are able to mark the L2 ISTs as valid in the L1 ISTEs as
		 * the overall IST is not yet valid.
		 */
		val = (virt_to_phys(l2ist) & GICV5_ISTL1E_L2_ADDR_MASK) |
		      GICV5_ISTL1E_VALID;
		l1ist[index] = cpu_to_le64(val);

		vmi->h_lpi_l2_ists[index] = l2ist;

		vgic_v5_clean_inval(l2ist, l2_size);
	}

	/* Handle CMOs for the whole L1 IST in one go */
	vgic_v5_clean_inval(l1ist, l1_entries * sizeof(*l1ist));

	return 0;
}

/* Allocate a two-level IST - LPIs, only */
static int vgic_v5_alloc_two_level_lpi_ist(struct kvm *kvm, unsigned int id_bits,
					   unsigned int istsz, unsigned int l2sz)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;
	int ret;

	/*
	 * Allocate the L1 IST first, then all of the L2s. Everything
	 * is preallocated and we do no on-demand IST allocation. This
	 * is to avoid needing to track if and when the guest is doing
	 * on-demand IST allocation.
	 */
	ret = vgic_v5_alloc_l1_ist(kvm, id_bits, istsz, l2sz);
	if (ret)
		return ret;

	ret = vgic_v5_alloc_l2_ists(kvm, id_bits, istsz, l2sz);
	if (ret) {
		/* Free the L1 IST again */
		vmi = xa_load(&vm_info, vm_id);
		kfree(vmi->h_lpi_ist);
		vmi->h_lpi_ist = NULL;

		return ret;
	}

	return 0;
}

static void vgic_v5_free_allocated_lpi_ist(struct vgic_v5_vm_info *vmi,
					   unsigned int id_bits,
					   unsigned int istsz,
					   unsigned int l2sz)
{
	if (!vmi->h_lpi_ist_structure) {
		kfree(vmi->h_lpi_ist);
		vmi->h_lpi_ist = NULL;
		return;
	}

	if (vmi->h_lpi_l2_ists) {
		const size_t n = max(5, id_bits - ((10 - istsz) + (2 * l2sz)) + 3 - 1);
		const int l1_entries = BIT(n + 1) / GICV5_IRS_ISTL1E_SIZE;
		int index;

		for (index = 0; index < l1_entries; ++index)
			kfree(vmi->h_lpi_l2_ists[index]);

		kfree(vmi->h_lpi_l2_ists);
		vmi->h_lpi_l2_ists = NULL;
	}

	kfree(vmi->h_lpi_ist);
	vmi->h_lpi_ist = NULL;
}

static void vgic_v5_free_allocated_spi_ist(struct kvm *kvm)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return;

	kfree(vmi->h_spi_ist);
	vmi->h_spi_ist = NULL;
}

/*
 * Free a Linear IST. Can only happen once the VM is dead.
 */
static int vgic_v5_linear_ist_free(struct kvm *kvm, bool spi)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vmtl2_entry *vmte;
	struct vgic_v5_vm_info *vmi;
	int section;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	vmte = vgic_v5_get_l2_vmte(vm_id);
	if (IS_ERR(vmte))
		return PTR_ERR(vmte);

	if (spi) {
		section = GICV5_VMTEL2_SPI_SECTION;
		vgic_v5_free_allocated_spi_ist(kvm);
	} else {
		section = GICV5_VMTEL2_LPI_SECTION;
		vgic_v5_free_allocated_lpi_ist(vmi, 0, 0, 0);
	}

	/* The VM should be dead here, so we can just zero the VMT section */
	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		WRITE_ONCE(vmte->val[section], cpu_to_le64(0));
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
	}

	return 0;
}

/*
 * Free a Two-Level IST. Can only happen once the VM is dead.
 */
static int vgic_v5_two_level_ist_free(struct kvm *kvm, bool spi)
{
	unsigned int id_bits, istsz, l2sz;
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;
	struct vmtl2_entry *vmte;
	u64 tmp;
	int section;

	/* We don't create two-level SPI ISTs, so freeing is a bad idea! */
	if (spi)
		return -EINVAL;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	section = GICV5_VMTEL2_LPI_SECTION;

	if (!vmi->h_lpi_ist_structure)
		return -EINVAL;

	vmte = vgic_v5_get_l2_vmte(vm_id);
	if (IS_ERR(vmte))
		return PTR_ERR(vmte);

	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
		tmp = le64_to_cpu(READ_ONCE(vmte->val[section]));
	}

	id_bits = FIELD_GET(GICV5_VMTEL2E_IST_ID_BITS, tmp);
	istsz = FIELD_GET(GICV5_VMTEL2E_IST_ISTSZ, tmp);
	l2sz = FIELD_GET(GICV5_VMTEL2E_IST_L2SZ, tmp);

	vgic_v5_free_allocated_lpi_ist(vmi, id_bits, istsz, l2sz);

	/* The VM must be dead, so we can just zero the VMT section */
	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		WRITE_ONCE(vmte->val[section], cpu_to_le64(0));
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
	}

	return 0;
}

/* Helper to determine ISTE size based on metadata requirements */
static unsigned int vgic_v5_ist_istsz(unsigned int id_bits)
{
	if (!vgic_v5_irs_istmd(&irs_caps))
		return GICV5_IRS_IST_CFGR_ISTSZ_4;

	if (id_bits >= vgic_v5_irs_istmd_sz(&irs_caps))
		return GICV5_IRS_IST_CFGR_ISTSZ_16;

	return GICV5_IRS_IST_CFGR_ISTSZ_8;
}

/*
 * Allocate an IST for SPIs.
 *
 * We don't anticipate a large number of SPIs being allocated. Therefore, we
 * always allocate a Linear IST for SPIs. This will need to be revisited should
 * that assumption no longer hold.
 */
int vgic_v5_spi_ist_alloc(struct kvm *kvm, unsigned int id_bits)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;
	phys_addr_t base_addr;
	unsigned int istsz;
	int ret;

	lockdep_assert_held(&kvm->arch.config_lock);

	istsz = vgic_v5_ist_istsz(id_bits);

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	if (vmi->h_spi_ist)
		return -EBUSY;

	ret = vgic_v5_alloc_linear_ist(kvm, true, id_bits, istsz);
	if (ret)
		return ret;
	base_addr = virt_to_phys(vmi->h_spi_ist);

	ret = vgic_v5_vmte_assign_ist(kvm, base_addr, false, id_bits, 0, istsz,
				      true);
	if (ret) {
		if (ret != -ETIMEDOUT)
			vgic_v5_free_allocated_spi_ist(kvm);
		return ret;
	}

	return 0;
}

/*
 * Free the IST for SPIs. Should only happen once the VM is dead.
 */
static int vgic_v5_spi_ist_free(struct kvm *kvm)
{
	lockdep_assert_held(&kvm->arch.config_lock);

	return vgic_v5_linear_ist_free(kvm, true);
}

int vgic_v5_lpi_ist_exists(struct kvm *kvm)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -ENXIO;

	return !!vmi->h_lpi_ist;
}

/*
 * Allocate an IST for LPIs.
 *
 * Unlike with SPIs, we anticipate that the guest will allocate a relatively
 * large number of LPIs. Therefore, while we support doing a linear LPI IST, it
 * is expected that LPI ISTs will be two-level.
 */
int vgic_v5_lpi_ist_alloc(struct kvm *kvm, unsigned int id_bits)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;
	unsigned int istsz, l2sz;
	phys_addr_t phys_addr;
	bool two_level;
	int ret;

	lockdep_assert_held(&kvm->arch.config_lock);

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -EINVAL;

	if (vmi->h_lpi_ist)
		return -EBUSY;

	istsz = vgic_v5_ist_istsz(id_bits);
	l2sz = gicv5_irs_l2_sz(vgic_v5_irs_ist_l2sz(&irs_caps));

	/*
	 * Determine if we want to create a Linear or a Two-Level IST.
	 *
	 * A two-level IST is only required when a single L2 IST cannot cover
	 * the requested ID space. This depends on the L2 IST size selected for
	 * the IRS, not PAGE_SIZE. Using PAGE_SIZE here would switch to
	 * two-level too early when the selected L2 IST is larger than a page,
	 * and the allocation sizing arithmetic would underflow.
	 */
	two_level = vgic_v5_irs_ist_levels(&irs_caps) &&
		id_bits > ((10 - istsz) + (2 * l2sz));

	if (!two_level)
		ret = vgic_v5_alloc_linear_ist(kvm, false /* LPIs, not SPIs */,
					       id_bits, istsz);
	else
		ret = vgic_v5_alloc_two_level_lpi_ist(kvm, id_bits, istsz,
						      l2sz);

	if (ret)
		return ret;

	phys_addr = virt_to_phys(vmi->h_lpi_ist);
	ret = vgic_v5_vmte_assign_ist(kvm, phys_addr, two_level, id_bits, l2sz,
				      istsz, false);
	if (ret && ret != -ETIMEDOUT)
		vgic_v5_free_allocated_lpi_ist(vmi, id_bits, istsz, l2sz);

	return ret;
}

/* Free the LPI IST again */
int vgic_v5_lpi_ist_free(struct kvm *kvm)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;

	lockdep_assert_held(&kvm->arch.config_lock);

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -ENXIO;

	if (!vmi->h_lpi_ist_structure)
		return vgic_v5_linear_ist_free(kvm, false);
	else
		return vgic_v5_two_level_ist_free(kvm, false);
}

static struct vgic_v5_two_level_ist_shape
vgic_v5_two_level_ist_shape(const struct vgic_v5_ist_desc *ist)
{
	struct vgic_v5_two_level_ist_shape shape;
	size_t l2bits, n;

	l2bits = (10 - ist->istsz) + (2 * ist->l2sz);
	n = max(2, ist->id_bits - l2bits + 3 - 1);

	shape.l1_entries = BIT(n + 1) / GICV5_IRS_ISTL1E_SIZE;
	shape.l2_entries = BIT(l2bits);

	return shape;
}

static int vgic_v5_read_vm_ist_desc(struct kvm *kvm, unsigned int section,
				    struct vgic_v5_ist_desc *ist)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vmtl2_entry *vmte;
	u64 vmte_ist_section;

	vmte = vgic_v5_get_l2_vmte(vm_id);
	if (IS_ERR(vmte))
		return PTR_ERR(vmte);

	scoped_guard(raw_spinlock_irqsave, &vgic_v5_irs_lock) {
		vgic_v5_clean_inval(vmte, sizeof(*vmte));
		vmte_ist_section = le64_to_cpu(READ_ONCE(vmte->val[section]));
	}

	ist->id_bits = FIELD_GET(GICV5_VMTEL2E_IST_ID_BITS, vmte_ist_section);
	ist->istsz = FIELD_GET(GICV5_VMTEL2E_IST_ISTSZ, vmte_ist_section);
	ist->l2sz = FIELD_GET(GICV5_VMTEL2E_IST_L2SZ, vmte_ist_section);
	ist->iste_size = GICV5_ISTE_SIZE(ist->istsz);

	return !!(vmte_ist_section & GICV5_VMTEL2E_IST_VALID);
}

static int vgic_v5_get_spi_ist_desc(struct kvm *kvm,
				    struct vgic_v5_ist_desc *ist)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	int ret;

	memset(ist, 0, sizeof(*ist));

	ist->vmi = xa_load(&vm_info, vm_id);
	if (!ist->vmi)
		return -ENXIO;

	ret = vgic_v5_read_vm_ist_desc(kvm, GICV5_VMTEL2_SPI_SECTION, ist);
	if (ret < 0)
		return ret;

	ist->base = ist->vmi->h_spi_ist;
	if (!ret || !ist->base)
		return -ENXIO;

	ist->present = true;
	return 0;
}

static int vgic_v5_get_lpi_ist_desc(struct kvm *kvm,
				    struct vgic_v5_ist_desc *ist)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	bool guest_valid, host_valid;
	int ret;

	memset(ist, 0, sizeof(*ist));

	ist->vmi = xa_load(&vm_info, vm_id);
	if (WARN_ON_ONCE(!ist->vmi))
		return -ENXIO;

	ret = vgic_v5_read_vm_ist_desc(kvm, GICV5_VMTEL2_LPI_SECTION, ist);
	if (ret < 0)
		return ret;

	host_valid = ret;
	guest_valid = kvm->arch.vgic.vgic_v5_irs_data->ist_baser.valid;
	ist->base = ist->vmi->h_lpi_ist;

	/* If there is no IST to save/restore, return without error. */
	if (!guest_valid && !host_valid && !ist->base)
		return 0;

	/* Mismatched combination of valid state */
	if (!guest_valid || !host_valid || !ist->base)
		return -ENXIO;

	if (ist->vmi->h_lpi_ist_structure && !ist->vmi->h_lpi_l2_ists)
		return -ENXIO;

	ist->present = true;
	return 0;
}

/*
 * Save a linear host IST to userspace memory.
 *
 * Only the architected 32-bit ISTE state is stored. Metadata is skipped when
 * striding through the host IST.
 */
static int vgic_v5_save_linear_ist(const struct vgic_v5_ist_desc *ist,
				   u32 __user *uaddr, size_t nr_entries)
{
	size_t index;
	int ret;

	vgic_v5_clean_inval(ist->base,
			    GICV5_LINEAR_IST_SIZE(ist->id_bits, ist->istsz));

	for (index = 0; index < nr_entries; index++) {
		__le32 *h_iste_addr = ist->base + index * ist->iste_size;
		u32 h_iste;

		h_iste = le32_to_cpu(READ_ONCE(*h_iste_addr));
		ret = put_user(h_iste, uaddr);
		if (ret)
			return ret;

		uaddr++;
	}

	return 0;
}

/*
 * Save a two-level host IST to userspace memory.
 *
 * Only the architected 32-bit ISTE state is stored. Metadata is skipped when
 * striding through the host IST.
 */
static int vgic_v5_save_two_level_ist(const struct vgic_v5_ist_desc *ist,
				      u32 __user *uaddr)
{
	struct vgic_v5_two_level_ist_shape shape;
	size_t h_l1_index, h_l2_index;
	void *h_l2_ist_base;
	int ret;

	shape = vgic_v5_two_level_ist_shape(ist);

	vgic_v5_clean_inval(ist->base,
			    shape.l1_entries * sizeof(*ist->vmi->h_lpi_ist));

	for (h_l1_index = 0; h_l1_index < shape.l1_entries; h_l1_index++) {
		u64 l1_iste;

		/*
		 * Host L2 ISTs are preallocated. Any invalid L1 entry means the
		 * host IST state is inconsistent.
		 */
		l1_iste = le64_to_cpu(READ_ONCE(ist->vmi->h_lpi_ist[h_l1_index]));
		if (!FIELD_GET(GICV5_ISTL1E_VALID, l1_iste))
			return -ENXIO;

		h_l2_ist_base = ist->vmi->h_lpi_l2_ists[h_l1_index];
		if (!h_l2_ist_base)
			return -ENXIO;

		vgic_v5_clean_inval(h_l2_ist_base,
				    shape.l2_entries * ist->iste_size);

		for (h_l2_index = 0; h_l2_index < shape.l2_entries; h_l2_index++) {
			void *h_iste_addr = h_l2_ist_base + h_l2_index * ist->iste_size;
			u32 h_iste = le32_to_cpu(*(__le32 *)h_iste_addr);

			ret = put_user(h_iste, uaddr);
			if (ret)
				return ret;

			uaddr++;
		}
	}

	return 0;
}

/*
 * Save the SPI IST to userspace-provided memory.
 */
int vgic_v5_save_spi_ist(struct kvm *kvm, struct kvm_vgic_v5_ist *ist_attr)
{
	struct vgic_v5_ist_desc ist;
	u32 __user *uaddr;
	int ret;

	ret = vgic_v5_get_spi_ist_desc(kvm, &ist);
	if (ret)
		return ret;

	uaddr = (u32 __user *)(unsigned long)ist_attr->spi_ist_addr;

	/* The host SPI IST is always linear. */
	return vgic_v5_save_linear_ist(&ist, uaddr,
				       kvm->arch.vgic.nr_spis);
}

/*
 * Save the LPI IST to userspace memory.
 *
 * The LPI IST may be linear or two-level, so host iteration depends on the
 * allocated host shape.
 */
int vgic_v5_save_lpi_ist(struct kvm *kvm, struct kvm_vgic_v5_ist *ist_attr)
{
	struct vgic_v5_ist_desc ist;
	u32 __user *uaddr;
	int ret;

	ret = vgic_v5_get_lpi_ist_desc(kvm, &ist);
	if (ret)
		return ret;

	if (!ist.present)
		return 0;

	/*
	 * Userspace sized the buffer from the guest-visible configuration.
	 * Refuse to walk a host IST with a different number of entries.
	 */
	if (ist.id_bits !=
	    kvm->arch.vgic.vgic_v5_irs_data->ist_cfgr.lpi_id_bits)
		return -EINVAL;

	uaddr = (u32 __user *)(unsigned long)ist_attr->lpi_ist_addr;

	if (!ist.vmi->h_lpi_ist_structure)
		return vgic_v5_save_linear_ist(&ist, uaddr,
					       BIT(ist.id_bits));

	return vgic_v5_save_two_level_ist(&ist, uaddr);
}

/*
 * Track any SPIs and LPIs that were marked as pending at the point where the
 * IST was restored.
 *
 * Restored pending state is cleared from the host ISTE and replayed with VDPEND
 * before the VM first runs.
 */
static int vgic_v5_track_pending_irq(struct list_head *pending_irqs, u32 intid,
				     u32 type)
{
	struct vgic_v5_pending_irq *pirq;

	pirq = kzalloc_obj(*pirq, GFP_KERNEL);
	if (!pirq)
		return -ENOMEM;

	/* Encode the interrupt as a GICv5 IntID. */
	pirq->irq = FIELD_PREP(GICV5_HWIRQ_TYPE, type) |
		    FIELD_PREP(GICV5_HWIRQ_ID, intid);

	INIT_LIST_HEAD(&pirq->next);
	list_add_tail(&pirq->next, pending_irqs);

	return 0;
}

/*
 * Process and sanitise each restored ISTE.
 *
 * HWU is for hardware use and must not survive migration. Pending state is
 * tracked, cleared from the ISTE, and replayed before the VM first runs.
 */
static int vgic_v5_process_iste(__le32 *iste, struct list_head *pending_irqs,
				u32 intid, u32 type)
{
	u32 iste_data = le32_to_cpu(READ_ONCE(*iste));
	int ret;

	/* Pending state is replayed later with VDPEND. */
	if (iste_data & GICV5_ISTL2E_PENDING) {
		ret = vgic_v5_track_pending_irq(pending_irqs, intid, type);
		if (ret)
			return ret;
	}

	iste_data &= ~GICV5_ISTL2E_PENDING;
	iste_data &= ~GICV5_ISTL2E_HWU;

	WRITE_ONCE(*iste, cpu_to_le32(iste_data));

	return 0;
}

static void vgic_v5_restore_spi_config(struct kvm *kvm, __le32 iste, u32 spi)
{
	u32 iste_data = le32_to_cpu(iste);
	bool pending = iste_data & GICV5_ISTL2E_PENDING;
	struct vgic_irq *irq;
	unsigned long flags;

	irq = vgic_get_irq(kvm, vgic_v5_make_spi(spi));
	if (WARN_ON_ONCE(!irq))
		return;

	raw_spin_lock_irqsave(&irq->irq_lock, flags);

	if (iste_data & GICV5_ISTL2E_HM)
		irq->config = VGIC_CONFIG_LEVEL;
	else
		irq->config = VGIC_CONFIG_EDGE;

	if (irq->config == VGIC_CONFIG_EDGE)
		irq->pending_latch = pending;
	else if (pending)
		irq->pending_latch = true;
	else if (!irq->active)
		irq->pending_latch = false;

	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
	vgic_put_irq(kvm, irq);
}

static int vgic_v5_restore_ist_entry(struct kvm *kvm,
				     const struct vgic_v5_ist_desc *ist,
				     void *h_iste_addr, __le32 h_iste,
				     u32 intid, u32 intid_type)
{
	__le32 raw_iste = h_iste;
	int ret;

	/*
	 * Sanitise the IST, clearing HWU & pending fields. Pending state is
	 * later replayed via GIC VDPEND.
	 */
	ret = vgic_v5_process_iste(&h_iste, &ist->vmi->pending_irqs,
				   intid, intid_type);
	if (ret)
		return ret;

	if (intid_type == GICV5_HWIRQ_TYPE_SPI)
		vgic_v5_restore_spi_config(kvm, raw_iste, intid);

	/*
	 * Zero the full ISTE (incl metadata), and write back the non-metadata
	 * region, only.
	 */
	memset(h_iste_addr, 0, ist->iste_size);
	WRITE_ONCE(*(__le32 *)h_iste_addr, h_iste);
	vgic_v5_clean_inval(h_iste_addr, ist->iste_size);

	return 0;
}

/*
 * Restore a userspace IST image to a linear host IST.
 *
 * The userspace IST image is a linear array of 32-bit ISTEs.
 */
static int vgic_v5_restore_linear_ist(struct kvm *kvm,
				      const struct vgic_v5_ist_desc *ist,
				      u32 __user *uaddr, size_t nr_entries,
				      u32 intid_type)
{
	size_t index;
	int ret;

	for (index = 0; index < nr_entries; index++) {
		void *h_iste_addr = ist->base + index * ist->iste_size;
		u32 h_iste;

		ret = get_user(h_iste, uaddr);
		if (ret)
			return ret;

		ret = vgic_v5_restore_ist_entry(kvm, ist, h_iste_addr,
						cpu_to_le32(h_iste), index, intid_type);
		if (ret)
			return ret;

		uaddr++;
	}

	return 0;
}

/*
 * Restore a userspace IST image to a two-level host IST.
 *
 * The userspace IST image is a linear array of 32-bit ISTEs.
 */
static int vgic_v5_restore_two_level_ist(struct kvm *kvm,
					 const struct vgic_v5_ist_desc *ist,
					 u32 __user *uaddr, u32 intid_type)
{
	struct vgic_v5_two_level_ist_shape shape;
	size_t h_l1_index, h_l2_index;
	void *h_l2_ist_base;
	int ret;

	shape = vgic_v5_two_level_ist_shape(ist);

	vgic_v5_clean_inval(ist->vmi->h_lpi_ist,
			    shape.l1_entries * sizeof(*ist->vmi->h_lpi_ist));

	for (h_l1_index = 0; h_l1_index < shape.l1_entries; ++h_l1_index) {
		u64 l1_iste;

		/*
		 * Host L2 ISTs are preallocated. Any invalid L1 entry means the
		 * host IST state is inconsistent.
		 */
		l1_iste = le64_to_cpu(READ_ONCE(ist->vmi->h_lpi_ist[h_l1_index]));
		if (!FIELD_GET(GICV5_ISTL1E_VALID, l1_iste))
			return -ENXIO;

		h_l2_ist_base = ist->vmi->h_lpi_l2_ists[h_l1_index];
		if (!h_l2_ist_base)
			return -ENXIO;

		for (h_l2_index = 0; h_l2_index < shape.l2_entries; h_l2_index++) {
			void *h_iste_addr = h_l2_ist_base +
					    h_l2_index * ist->iste_size;
			u32 intid = h_l1_index * shape.l2_entries + h_l2_index;
			u32 h_iste;

			ret = get_user(h_iste, uaddr);
			if (ret)
				return ret;

			ret = vgic_v5_restore_ist_entry(kvm, ist, h_iste_addr,
							cpu_to_le32(h_iste), intid,
							intid_type);
			if (ret)
				return ret;

			uaddr++;
		}
	}

	return 0;
}

/*
 * Restore the SPI IST from userspace-provided buffer to the host-allocated IST.
 */
int vgic_v5_restore_spi_ist(struct kvm *kvm, struct kvm_vgic_v5_ist *ist_attr)
{
	u32 __user *uaddr;
	struct vgic_v5_ist_desc ist;
	int ret;

	ret = vgic_v5_get_spi_ist_desc(kvm, &ist);
	if (ret)
		return ret;

	uaddr = (u32 __user *)(unsigned long)ist_attr->spi_ist_addr;

	/* The host SPI IST is always linear. */
	return vgic_v5_restore_linear_ist(kvm, &ist, uaddr,
					  kvm->arch.vgic.nr_spis,
					  GICV5_HWIRQ_TYPE_SPI);
}

/*
 * Restore the LPI IST from userspace memory to the host-allocated LPI IST.
 *
 * The host LPI IST may be linear or two-level, so host iteration depends on the
 * host IST's shape.
 */
int vgic_v5_restore_lpi_ist(struct kvm *kvm, struct kvm_vgic_v5_ist *ist_attr)
{
	u32 __user *uaddr;
	struct vgic_v5_ist_desc ist;
	int ret;

	ret = vgic_v5_get_lpi_ist_desc(kvm, &ist);
	if (ret)
		return ret;

	if (!ist.present)
		return 0;

	uaddr = (u32 __user *)(unsigned long)ist_attr->lpi_ist_addr;

	if (!ist.vmi->h_lpi_ist_structure)
		return vgic_v5_restore_linear_ist(kvm, &ist, uaddr,
						  BIT(ist.id_bits),
						  GICV5_HWIRQ_TYPE_LPI);

	return vgic_v5_restore_two_level_ist(kvm, &ist, uaddr,
					     GICV5_HWIRQ_TYPE_LPI);
}

/*
 * Process the pending IRQs removing them from the list and optionally injecting
 * them.
 */
static int vgic_v5_process_pending_irqs(struct kvm *kvm, bool inject)
{
	u32 vm_id = vgic_v5_vm_id(kvm);
	struct vgic_v5_vm_info *vmi;

	lockdep_assert_held(&kvm->arch.config_lock);

	vmi = xa_load(&vm_info, vm_id);
	if (!vmi)
		return -ENXIO;

	vgic_v5_drain_pending_irqs(kvm, vmi, inject);

	return 0;
}

/* Replay pending state that was cleared while restoring guest IST state. */
int vgic_v5_restore_pending_irqs(struct kvm *kvm)
{
	return vgic_v5_process_pending_irqs(kvm, true);
}

/* Drop pending state collected by a failed IST restore. */
void vgic_v5_discard_pending_irqs(struct kvm *kvm)
{
	vgic_v5_process_pending_irqs(kvm, false);
}
