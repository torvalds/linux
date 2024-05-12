// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015, 2016 ARM Ltd.
 */

#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/kvm_host.h>
#include <kvm/arm_vgic.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>
#include "vgic.h"

/*
 * Initialization rules: there are multiple stages to the vgic
 * initialization, both for the distributor and the CPU interfaces.  The basic
 * idea is that even though the VGIC is not functional or not requested from
 * user space, the critical path of the run loop can still call VGIC functions
 * that just won't do anything, without them having to check additional
 * initialization flags to ensure they don't look at uninitialized data
 * structures.
 *
 * Distributor:
 *
 * - kvm_vgic_early_init(): initialization of static data that doesn't
 *   depend on any sizing information or emulation type. No allocation
 *   is allowed there.
 *
 * - vgic_init(): allocation and initialization of the generic data
 *   structures that depend on sizing information (number of CPUs,
 *   number of interrupts). Also initializes the vcpu specific data
 *   structures. Can be executed lazily for GICv2.
 *
 * CPU Interface:
 *
 * - kvm_vgic_vcpu_init(): initialization of static data that
 *   doesn't depend on any sizing information or emulation type. No
 *   allocation is allowed there.
 */

/* EARLY INIT */

/**
 * kvm_vgic_early_init() - Initialize static VGIC VCPU data structures
 * @kvm: The VM whose VGIC districutor should be initialized
 *
 * Only do initialization of static structures that don't require any
 * allocation or sizing information from userspace.  vgic_init() called
 * kvm_vgic_dist_init() which takes care of the rest.
 */
void kvm_vgic_early_init(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;

	xa_init_flags(&dist->lpi_xa, XA_FLAGS_LOCK_IRQ);
}

/* CREATION */

/**
 * kvm_vgic_create: triggered by the instantiation of the VGIC device by
 * user space, either through the legacy KVM_CREATE_IRQCHIP ioctl (v2 only)
 * or through the generic KVM_CREATE_DEVICE API ioctl.
 * irqchip_in_kernel() tells you if this function succeeded or not.
 * @kvm: kvm struct pointer
 * @type: KVM_DEV_TYPE_ARM_VGIC_V[23]
 */
int kvm_vgic_create(struct kvm *kvm, u32 type)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;
	int ret;

	/*
	 * This function is also called by the KVM_CREATE_IRQCHIP handler,
	 * which had no chance yet to check the availability of the GICv2
	 * emulation. So check this here again. KVM_CREATE_DEVICE does
	 * the proper checks already.
	 */
	if (type == KVM_DEV_TYPE_ARM_VGIC_V2 &&
		!kvm_vgic_global_state.can_emulate_gicv2)
		return -ENODEV;

	/* Must be held to avoid race with vCPU creation */
	lockdep_assert_held(&kvm->lock);

	ret = -EBUSY;
	if (!lock_all_vcpus(kvm))
		return ret;

	mutex_lock(&kvm->arch.config_lock);

	if (irqchip_in_kernel(kvm)) {
		ret = -EEXIST;
		goto out_unlock;
	}

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (vcpu_has_run_once(vcpu))
			goto out_unlock;
	}
	ret = 0;

	if (type == KVM_DEV_TYPE_ARM_VGIC_V2)
		kvm->max_vcpus = VGIC_V2_MAX_CPUS;
	else
		kvm->max_vcpus = VGIC_V3_MAX_CPUS;

	if (atomic_read(&kvm->online_vcpus) > kvm->max_vcpus) {
		ret = -E2BIG;
		goto out_unlock;
	}

	kvm->arch.vgic.in_kernel = true;
	kvm->arch.vgic.vgic_model = type;

	kvm->arch.vgic.vgic_dist_base = VGIC_ADDR_UNDEF;

	if (type == KVM_DEV_TYPE_ARM_VGIC_V2)
		kvm->arch.vgic.vgic_cpu_base = VGIC_ADDR_UNDEF;
	else
		INIT_LIST_HEAD(&kvm->arch.vgic.rd_regions);

out_unlock:
	mutex_unlock(&kvm->arch.config_lock);
	unlock_all_vcpus(kvm);
	return ret;
}

/* INIT/DESTROY */

/**
 * kvm_vgic_dist_init: initialize the dist data structures
 * @kvm: kvm struct pointer
 * @nr_spis: number of spis, frozen by caller
 */
static int kvm_vgic_dist_init(struct kvm *kvm, unsigned int nr_spis)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu0 = kvm_get_vcpu(kvm, 0);
	int i;

	dist->spis = kcalloc(nr_spis, sizeof(struct vgic_irq), GFP_KERNEL_ACCOUNT);
	if (!dist->spis)
		return  -ENOMEM;

	/*
	 * In the following code we do not take the irq struct lock since
	 * no other action on irq structs can happen while the VGIC is
	 * not initialized yet:
	 * If someone wants to inject an interrupt or does a MMIO access, we
	 * require prior initialization in case of a virtual GICv3 or trigger
	 * initialization when using a virtual GICv2.
	 */
	for (i = 0; i < nr_spis; i++) {
		struct vgic_irq *irq = &dist->spis[i];

		irq->intid = i + VGIC_NR_PRIVATE_IRQS;
		INIT_LIST_HEAD(&irq->ap_list);
		raw_spin_lock_init(&irq->irq_lock);
		irq->vcpu = NULL;
		irq->target_vcpu = vcpu0;
		kref_init(&irq->refcount);
		switch (dist->vgic_model) {
		case KVM_DEV_TYPE_ARM_VGIC_V2:
			irq->targets = 0;
			irq->group = 0;
			break;
		case KVM_DEV_TYPE_ARM_VGIC_V3:
			irq->mpidr = 0;
			irq->group = 1;
			break;
		default:
			kfree(dist->spis);
			dist->spis = NULL;
			return -EINVAL;
		}
	}
	return 0;
}

static int vgic_allocate_private_irqs_locked(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	int i;

	lockdep_assert_held(&vcpu->kvm->arch.config_lock);

	if (vgic_cpu->private_irqs)
		return 0;

	vgic_cpu->private_irqs = kcalloc(VGIC_NR_PRIVATE_IRQS,
					 sizeof(struct vgic_irq),
					 GFP_KERNEL_ACCOUNT);

	if (!vgic_cpu->private_irqs)
		return -ENOMEM;

	/*
	 * Enable and configure all SGIs to be edge-triggered and
	 * configure all PPIs as level-triggered.
	 */
	for (i = 0; i < VGIC_NR_PRIVATE_IRQS; i++) {
		struct vgic_irq *irq = &vgic_cpu->private_irqs[i];

		INIT_LIST_HEAD(&irq->ap_list);
		raw_spin_lock_init(&irq->irq_lock);
		irq->intid = i;
		irq->vcpu = NULL;
		irq->target_vcpu = vcpu;
		kref_init(&irq->refcount);
		if (vgic_irq_is_sgi(i)) {
			/* SGIs */
			irq->enabled = 1;
			irq->config = VGIC_CONFIG_EDGE;
		} else {
			/* PPIs */
			irq->config = VGIC_CONFIG_LEVEL;
		}
	}

	return 0;
}

static int vgic_allocate_private_irqs(struct kvm_vcpu *vcpu)
{
	int ret;

	mutex_lock(&vcpu->kvm->arch.config_lock);
	ret = vgic_allocate_private_irqs_locked(vcpu);
	mutex_unlock(&vcpu->kvm->arch.config_lock);

	return ret;
}

/**
 * kvm_vgic_vcpu_init() - Initialize static VGIC VCPU data
 * structures and register VCPU-specific KVM iodevs
 *
 * @vcpu: pointer to the VCPU being created and initialized
 *
 * Only do initialization, but do not actually enable the
 * VGIC CPU interface
 */
int kvm_vgic_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;
	int ret = 0;

	vgic_cpu->rd_iodev.base_addr = VGIC_ADDR_UNDEF;

	INIT_LIST_HEAD(&vgic_cpu->ap_list_head);
	raw_spin_lock_init(&vgic_cpu->ap_list_lock);
	atomic_set(&vgic_cpu->vgic_v3.its_vpe.vlpi_count, 0);

	if (!irqchip_in_kernel(vcpu->kvm))
		return 0;

	ret = vgic_allocate_private_irqs(vcpu);
	if (ret)
		return ret;

	/*
	 * If we are creating a VCPU with a GICv3 we must also register the
	 * KVM io device for the redistributor that belongs to this VCPU.
	 */
	if (dist->vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3) {
		mutex_lock(&vcpu->kvm->slots_lock);
		ret = vgic_register_redist_iodev(vcpu);
		mutex_unlock(&vcpu->kvm->slots_lock);
	}
	return ret;
}

static void kvm_vgic_vcpu_enable(struct kvm_vcpu *vcpu)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_enable(vcpu);
	else
		vgic_v3_enable(vcpu);
}

/*
 * vgic_init: allocates and initializes dist and vcpu data structures
 * depending on two dimensioning parameters:
 * - the number of spis
 * - the number of vcpus
 * The function is generally called when nr_spis has been explicitly set
 * by the guest through the KVM DEVICE API. If not nr_spis is set to 256.
 * vgic_initialized() returns true when this function has succeeded.
 */
int vgic_init(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu;
	int ret = 0, i;
	unsigned long idx;

	lockdep_assert_held(&kvm->arch.config_lock);

	if (vgic_initialized(kvm))
		return 0;

	/* Are we also in the middle of creating a VCPU? */
	if (kvm->created_vcpus != atomic_read(&kvm->online_vcpus))
		return -EBUSY;

	/* freeze the number of spis */
	if (!dist->nr_spis)
		dist->nr_spis = VGIC_NR_IRQS_LEGACY - VGIC_NR_PRIVATE_IRQS;

	ret = kvm_vgic_dist_init(kvm, dist->nr_spis);
	if (ret)
		goto out;

	/* Initialize groups on CPUs created before the VGIC type was known */
	kvm_for_each_vcpu(idx, vcpu, kvm) {
		ret = vgic_allocate_private_irqs_locked(vcpu);
		if (ret)
			goto out;

		for (i = 0; i < VGIC_NR_PRIVATE_IRQS; i++) {
			struct vgic_irq *irq = vgic_get_irq(kvm, vcpu, i);

			switch (dist->vgic_model) {
			case KVM_DEV_TYPE_ARM_VGIC_V3:
				irq->group = 1;
				irq->mpidr = kvm_vcpu_get_mpidr_aff(vcpu);
				break;
			case KVM_DEV_TYPE_ARM_VGIC_V2:
				irq->group = 0;
				irq->targets = 1U << idx;
				break;
			default:
				ret = -EINVAL;
			}

			vgic_put_irq(kvm, irq);

			if (ret)
				goto out;
		}
	}

	/*
	 * If we have GICv4.1 enabled, unconditionally request enable the
	 * v4 support so that we get HW-accelerated vSGIs. Otherwise, only
	 * enable it if we present a virtual ITS to the guest.
	 */
	if (vgic_supports_direct_msis(kvm)) {
		ret = vgic_v4_init(kvm);
		if (ret)
			goto out;
	}

	kvm_for_each_vcpu(idx, vcpu, kvm)
		kvm_vgic_vcpu_enable(vcpu);

	ret = kvm_vgic_setup_default_irq_routing(kvm);
	if (ret)
		goto out;

	vgic_debug_init(kvm);

	/*
	 * If userspace didn't set the GIC implementation revision,
	 * default to the latest and greatest. You know want it.
	 */
	if (!dist->implementation_rev)
		dist->implementation_rev = KVM_VGIC_IMP_REV_LATEST;
	dist->initialized = true;

out:
	return ret;
}

static void kvm_vgic_dist_destroy(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct vgic_redist_region *rdreg, *next;

	dist->ready = false;
	dist->initialized = false;

	kfree(dist->spis);
	dist->spis = NULL;
	dist->nr_spis = 0;
	dist->vgic_dist_base = VGIC_ADDR_UNDEF;

	if (dist->vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3) {
		list_for_each_entry_safe(rdreg, next, &dist->rd_regions, list)
			vgic_v3_free_redist_region(rdreg);
		INIT_LIST_HEAD(&dist->rd_regions);
	} else {
		dist->vgic_cpu_base = VGIC_ADDR_UNDEF;
	}

	if (vgic_supports_direct_msis(kvm))
		vgic_v4_teardown(kvm);

	xa_destroy(&dist->lpi_xa);
}

static void __kvm_vgic_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

	/*
	 * Retire all pending LPIs on this vcpu anyway as we're
	 * going to destroy it.
	 */
	vgic_flush_pending_lpis(vcpu);

	INIT_LIST_HEAD(&vgic_cpu->ap_list_head);
	kfree(vgic_cpu->private_irqs);
	vgic_cpu->private_irqs = NULL;

	if (vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3) {
		vgic_unregister_redist_iodev(vcpu);
		vgic_cpu->rd_iodev.base_addr = VGIC_ADDR_UNDEF;
	}
}

void kvm_vgic_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	mutex_lock(&kvm->slots_lock);
	__kvm_vgic_vcpu_destroy(vcpu);
	mutex_unlock(&kvm->slots_lock);
}

void kvm_vgic_destroy(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;

	mutex_lock(&kvm->slots_lock);

	vgic_debug_destroy(kvm);

	kvm_for_each_vcpu(i, vcpu, kvm)
		__kvm_vgic_vcpu_destroy(vcpu);

	mutex_lock(&kvm->arch.config_lock);

	kvm_vgic_dist_destroy(kvm);

	mutex_unlock(&kvm->arch.config_lock);
	mutex_unlock(&kvm->slots_lock);
}

/**
 * vgic_lazy_init: Lazy init is only allowed if the GIC exposed to the guest
 * is a GICv2. A GICv3 must be explicitly initialized by userspace using the
 * KVM_DEV_ARM_VGIC_GRP_CTRL KVM_DEVICE group.
 * @kvm: kvm struct pointer
 */
int vgic_lazy_init(struct kvm *kvm)
{
	int ret = 0;

	if (unlikely(!vgic_initialized(kvm))) {
		/*
		 * We only provide the automatic initialization of the VGIC
		 * for the legacy case of a GICv2. Any other type must
		 * be explicitly initialized once setup with the respective
		 * KVM device call.
		 */
		if (kvm->arch.vgic.vgic_model != KVM_DEV_TYPE_ARM_VGIC_V2)
			return -EBUSY;

		mutex_lock(&kvm->arch.config_lock);
		ret = vgic_init(kvm);
		mutex_unlock(&kvm->arch.config_lock);
	}

	return ret;
}

/* RESOURCE MAPPING */

/**
 * kvm_vgic_map_resources - map the MMIO regions
 * @kvm: kvm struct pointer
 *
 * Map the MMIO regions depending on the VGIC model exposed to the guest
 * called on the first VCPU run.
 * Also map the virtual CPU interface into the VM.
 * v2 calls vgic_init() if not already done.
 * v3 and derivatives return an error if the VGIC is not initialized.
 * vgic_ready() returns true if this function has succeeded.
 */
int kvm_vgic_map_resources(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	enum vgic_type type;
	gpa_t dist_base;
	int ret = 0;

	if (likely(vgic_ready(kvm)))
		return 0;

	mutex_lock(&kvm->slots_lock);
	mutex_lock(&kvm->arch.config_lock);
	if (vgic_ready(kvm))
		goto out;

	if (!irqchip_in_kernel(kvm))
		goto out;

	if (dist->vgic_model == KVM_DEV_TYPE_ARM_VGIC_V2) {
		ret = vgic_v2_map_resources(kvm);
		type = VGIC_V2;
	} else {
		ret = vgic_v3_map_resources(kvm);
		type = VGIC_V3;
	}

	if (ret)
		goto out;

	dist->ready = true;
	dist_base = dist->vgic_dist_base;
	mutex_unlock(&kvm->arch.config_lock);

	ret = vgic_register_dist_iodev(kvm, dist_base, type);
	if (ret)
		kvm_err("Unable to register VGIC dist MMIO regions\n");

	goto out_slots;
out:
	mutex_unlock(&kvm->arch.config_lock);
out_slots:
	mutex_unlock(&kvm->slots_lock);

	if (ret)
		kvm_vgic_destroy(kvm);

	return ret;
}

/* GENERIC PROBE */

void kvm_vgic_cpu_up(void)
{
	enable_percpu_irq(kvm_vgic_global_state.maint_irq, 0);
}


void kvm_vgic_cpu_down(void)
{
	disable_percpu_irq(kvm_vgic_global_state.maint_irq);
}

static irqreturn_t vgic_maintenance_handler(int irq, void *data)
{
	/*
	 * We cannot rely on the vgic maintenance interrupt to be
	 * delivered synchronously. This means we can only use it to
	 * exit the VM, and we perform the handling of EOIed
	 * interrupts on the exit path (see vgic_fold_lr_state).
	 */
	return IRQ_HANDLED;
}

static struct gic_kvm_info *gic_kvm_info;

void __init vgic_set_kvm_info(const struct gic_kvm_info *info)
{
	BUG_ON(gic_kvm_info != NULL);
	gic_kvm_info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (gic_kvm_info)
		*gic_kvm_info = *info;
}

/**
 * kvm_vgic_init_cpu_hardware - initialize the GIC VE hardware
 *
 * For a specific CPU, initialize the GIC VE hardware.
 */
void kvm_vgic_init_cpu_hardware(void)
{
	BUG_ON(preemptible());

	/*
	 * We want to make sure the list registers start out clear so that we
	 * only have the program the used registers.
	 */
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_init_lrs();
	else
		kvm_call_hyp(__vgic_v3_init_lrs);
}

/**
 * kvm_vgic_hyp_init: populates the kvm_vgic_global_state variable
 * according to the host GIC model. Accordingly calls either
 * vgic_v2/v3_probe which registers the KVM_DEVICE that can be
 * instantiated by a guest later on .
 */
int kvm_vgic_hyp_init(void)
{
	bool has_mask;
	int ret;

	if (!gic_kvm_info)
		return -ENODEV;

	has_mask = !gic_kvm_info->no_maint_irq_mask;

	if (has_mask && !gic_kvm_info->maint_irq) {
		kvm_err("No vgic maintenance irq\n");
		return -ENXIO;
	}

	/*
	 * If we get one of these oddball non-GICs, taint the kernel,
	 * as we have no idea of how they *really* behave.
	 */
	if (gic_kvm_info->no_hw_deactivation) {
		kvm_info("Non-architectural vgic, tainting kernel\n");
		add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);
		kvm_vgic_global_state.no_hw_deactivation = true;
	}

	switch (gic_kvm_info->type) {
	case GIC_V2:
		ret = vgic_v2_probe(gic_kvm_info);
		break;
	case GIC_V3:
		ret = vgic_v3_probe(gic_kvm_info);
		if (!ret) {
			static_branch_enable(&kvm_vgic_global_state.gicv3_cpuif);
			kvm_info("GIC system register CPU interface enabled\n");
		}
		break;
	default:
		ret = -ENODEV;
	}

	kvm_vgic_global_state.maint_irq = gic_kvm_info->maint_irq;

	kfree(gic_kvm_info);
	gic_kvm_info = NULL;

	if (ret)
		return ret;

	if (!has_mask && !kvm_vgic_global_state.maint_irq)
		return 0;

	ret = request_percpu_irq(kvm_vgic_global_state.maint_irq,
				 vgic_maintenance_handler,
				 "vgic", kvm_get_running_vcpus());
	if (ret) {
		kvm_err("Cannot register interrupt %d\n",
			kvm_vgic_global_state.maint_irq);
		return ret;
	}

	kvm_info("vgic interrupt IRQ%d\n", kvm_vgic_global_state.maint_irq);
	return 0;
}
