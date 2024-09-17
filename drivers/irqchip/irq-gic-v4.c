// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016,2017 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/pid.h>
#include <linux/sched.h>

#include <linux/irqchip/arm-gic-v4.h>

/*
 * WARNING: The blurb below assumes that you understand the
 * intricacies of GICv3, GICv4, and how a guest's view of a GICv3 gets
 * translated into GICv4 commands. So it effectively targets at most
 * two individuals. You know who you are.
 *
 * The core GICv4 code is designed to *avoid* exposing too much of the
 * core GIC code (that would in turn leak into the hypervisor code),
 * and instead provide a hypervisor agnostic interface to the HW (of
 * course, the astute reader will quickly realize that hypervisor
 * agnostic actually means KVM-specific - what were you thinking?).
 *
 * In order to achieve a modicum of isolation, we try to hide most of
 * the GICv4 "stuff" behind normal irqchip operations:
 *
 * - Any guest-visible VLPI is backed by a Linux interrupt (and a
 *   physical LPI which gets unmapped when the guest maps the
 *   VLPI). This allows the same DevID/EventID pair to be either
 *   mapped to the LPI (host) or the VLPI (guest). Note that this is
 *   exclusive, and you cannot have both.
 *
 * - Enabling/disabling a VLPI is done by issuing mask/unmask calls.
 *
 * - Guest INT/CLEAR commands are implemented through
 *   irq_set_irqchip_state().
 *
 * - The *bizarre* stuff (mapping/unmapping an interrupt to a VLPI, or
 *   issuing an INV after changing a priority) gets shoved into the
 *   irq_set_vcpu_affinity() method. While this is quite horrible
 *   (let's face it, this is the irqchip version of an ioctl), it
 *   confines the crap to a single location. And map/unmap really is
 *   about setting the affinity of a VLPI to a vcpu, so only INV is
 *   majorly out of place. So there.
 *
 * A number of commands are simply not provided by this interface, as
 * they do not make direct sense. For example, MAPD is purely local to
 * the virtual ITS (because it references a virtual device, and the
 * physical ITS is still very much in charge of the physical
 * device). Same goes for things like MAPC (the physical ITS deals
 * with the actual vPE affinity, and not the braindead concept of
 * collection). SYNC is not provided either, as each and every command
 * is followed by a VSYNC. This could be relaxed in the future, should
 * this be seen as a bottleneck (yes, this means *never*).
 *
 * But handling VLPIs is only one side of the job of the GICv4
 * code. The other (darker) side is to take care of the doorbell
 * interrupts which are delivered when a VLPI targeting a non-running
 * vcpu is being made pending.
 *
 * The choice made here is that each vcpu (VPE in old northern GICv4
 * dialect) gets a single doorbell LPI, no matter how many interrupts
 * are targeting it. This has a nice property, which is that the
 * interrupt becomes a handle for the VPE, and that the hypervisor
 * code can manipulate it through the normal interrupt API:
 *
 * - VMs (or rather the VM abstraction that matters to the GIC)
 *   contain an irq domain where each interrupt maps to a VPE. In
 *   turn, this domain sits on top of the normal LPI allocator, and a
 *   specially crafted irq_chip implementation.
 *
 * - mask/unmask do what is expected on the doorbell interrupt.
 *
 * - irq_set_affinity is used to move a VPE from one redistributor to
 *   another.
 *
 * - irq_set_vcpu_affinity once again gets hijacked for the purpose of
 *   creating a new sub-API, namely scheduling/descheduling a VPE
 *   (which involves programming GICR_V{PROP,PEND}BASER) and
 *   performing INVALL operations.
 */

static struct irq_domain *gic_domain;
static const struct irq_domain_ops *vpe_domain_ops;
static const struct irq_domain_ops *sgi_domain_ops;

#ifdef CONFIG_ARM64
#include <asm/cpufeature.h>

bool gic_cpuif_has_vsgi(void)
{
	unsigned long fld, reg = read_sanitised_ftr_reg(SYS_ID_AA64PFR0_EL1);

	fld = cpuid_feature_extract_unsigned_field(reg, ID_AA64PFR0_EL1_GIC_SHIFT);

	return fld >= ID_AA64PFR0_EL1_GIC_V4P1;
}
#else
bool gic_cpuif_has_vsgi(void)
{
	return false;
}
#endif

static bool has_v4_1(void)
{
	return !!sgi_domain_ops;
}

static bool has_v4_1_sgi(void)
{
	return has_v4_1() && gic_cpuif_has_vsgi();
}

static int its_alloc_vcpu_sgis(struct its_vpe *vpe, int idx)
{
	char *name;
	int sgi_base;

	if (!has_v4_1_sgi())
		return 0;

	name = kasprintf(GFP_KERNEL, "GICv4-sgi-%d", task_pid_nr(current));
	if (!name)
		goto err;

	vpe->fwnode = irq_domain_alloc_named_id_fwnode(name, idx);
	if (!vpe->fwnode)
		goto err;

	kfree(name);
	name = NULL;

	vpe->sgi_domain = irq_domain_create_linear(vpe->fwnode, 16,
						   sgi_domain_ops, vpe);
	if (!vpe->sgi_domain)
		goto err;

	sgi_base = irq_domain_alloc_irqs(vpe->sgi_domain, 16, NUMA_NO_NODE, vpe);
	if (sgi_base <= 0)
		goto err;

	return 0;

err:
	if (vpe->sgi_domain)
		irq_domain_remove(vpe->sgi_domain);
	if (vpe->fwnode)
		irq_domain_free_fwnode(vpe->fwnode);
	kfree(name);
	return -ENOMEM;
}

int its_alloc_vcpu_irqs(struct its_vm *vm)
{
	int vpe_base_irq, i;

	vm->fwnode = irq_domain_alloc_named_id_fwnode("GICv4-vpe",
						      task_pid_nr(current));
	if (!vm->fwnode)
		goto err;

	vm->domain = irq_domain_create_hierarchy(gic_domain, 0, vm->nr_vpes,
						 vm->fwnode, vpe_domain_ops,
						 vm);
	if (!vm->domain)
		goto err;

	for (i = 0; i < vm->nr_vpes; i++) {
		vm->vpes[i]->its_vm = vm;
		vm->vpes[i]->idai = true;
	}

	vpe_base_irq = irq_domain_alloc_irqs(vm->domain, vm->nr_vpes,
					     NUMA_NO_NODE, vm);
	if (vpe_base_irq <= 0)
		goto err;

	for (i = 0; i < vm->nr_vpes; i++) {
		int ret;
		vm->vpes[i]->irq = vpe_base_irq + i;
		ret = its_alloc_vcpu_sgis(vm->vpes[i], i);
		if (ret)
			goto err;
	}

	return 0;

err:
	if (vm->domain)
		irq_domain_remove(vm->domain);
	if (vm->fwnode)
		irq_domain_free_fwnode(vm->fwnode);

	return -ENOMEM;
}

static void its_free_sgi_irqs(struct its_vm *vm)
{
	int i;

	if (!has_v4_1_sgi())
		return;

	for (i = 0; i < vm->nr_vpes; i++) {
		unsigned int irq = irq_find_mapping(vm->vpes[i]->sgi_domain, 0);

		if (WARN_ON(!irq))
			continue;

		irq_domain_free_irqs(irq, 16);
		irq_domain_remove(vm->vpes[i]->sgi_domain);
		irq_domain_free_fwnode(vm->vpes[i]->fwnode);
	}
}

void its_free_vcpu_irqs(struct its_vm *vm)
{
	its_free_sgi_irqs(vm);
	irq_domain_free_irqs(vm->vpes[0]->irq, vm->nr_vpes);
	irq_domain_remove(vm->domain);
	irq_domain_free_fwnode(vm->fwnode);
}

static int its_send_vpe_cmd(struct its_vpe *vpe, struct its_cmd_info *info)
{
	return irq_set_vcpu_affinity(vpe->irq, info);
}

int its_make_vpe_non_resident(struct its_vpe *vpe, bool db)
{
	struct irq_desc *desc = irq_to_desc(vpe->irq);
	struct its_cmd_info info = { };
	int ret;

	WARN_ON(preemptible());

	info.cmd_type = DESCHEDULE_VPE;
	if (has_v4_1()) {
		/* GICv4.1 can directly deal with doorbells */
		info.req_db = db;
	} else {
		/* Undo the nested disable_irq() calls... */
		while (db && irqd_irq_disabled(&desc->irq_data))
			enable_irq(vpe->irq);
	}

	ret = its_send_vpe_cmd(vpe, &info);
	if (!ret)
		vpe->resident = false;

	vpe->ready = false;

	return ret;
}

int its_make_vpe_resident(struct its_vpe *vpe, bool g0en, bool g1en)
{
	struct its_cmd_info info = { };
	int ret;

	WARN_ON(preemptible());

	info.cmd_type = SCHEDULE_VPE;
	if (has_v4_1()) {
		info.g0en = g0en;
		info.g1en = g1en;
	} else {
		/* Disabled the doorbell, as we're about to enter the guest */
		disable_irq_nosync(vpe->irq);
	}

	ret = its_send_vpe_cmd(vpe, &info);
	if (!ret)
		vpe->resident = true;

	return ret;
}

int its_commit_vpe(struct its_vpe *vpe)
{
	struct its_cmd_info info = {
		.cmd_type = COMMIT_VPE,
	};
	int ret;

	WARN_ON(preemptible());

	ret = its_send_vpe_cmd(vpe, &info);
	if (!ret)
		vpe->ready = true;

	return ret;
}


int its_invall_vpe(struct its_vpe *vpe)
{
	struct its_cmd_info info = {
		.cmd_type = INVALL_VPE,
	};

	return its_send_vpe_cmd(vpe, &info);
}

int its_map_vlpi(int irq, struct its_vlpi_map *map)
{
	struct its_cmd_info info = {
		.cmd_type = MAP_VLPI,
		{
			.map      = map,
		},
	};
	int ret;

	/*
	 * The host will never see that interrupt firing again, so it
	 * is vital that we don't do any lazy masking.
	 */
	irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY);

	ret = irq_set_vcpu_affinity(irq, &info);
	if (ret)
		irq_clear_status_flags(irq, IRQ_DISABLE_UNLAZY);

	return ret;
}

int its_get_vlpi(int irq, struct its_vlpi_map *map)
{
	struct its_cmd_info info = {
		.cmd_type = GET_VLPI,
		{
			.map      = map,
		},
	};

	return irq_set_vcpu_affinity(irq, &info);
}

int its_unmap_vlpi(int irq)
{
	irq_clear_status_flags(irq, IRQ_DISABLE_UNLAZY);
	return irq_set_vcpu_affinity(irq, NULL);
}

int its_prop_update_vlpi(int irq, u8 config, bool inv)
{
	struct its_cmd_info info = {
		.cmd_type = inv ? PROP_UPDATE_AND_INV_VLPI : PROP_UPDATE_VLPI,
		{
			.config   = config,
		},
	};

	return irq_set_vcpu_affinity(irq, &info);
}

int its_prop_update_vsgi(int irq, u8 priority, bool group)
{
	struct its_cmd_info info = {
		.cmd_type = PROP_UPDATE_VSGI,
		{
			.priority	= priority,
			.group		= group,
		},
	};

	return irq_set_vcpu_affinity(irq, &info);
}

int its_init_v4(struct irq_domain *domain,
		const struct irq_domain_ops *vpe_ops,
		const struct irq_domain_ops *sgi_ops)
{
	if (domain) {
		pr_info("ITS: Enabling GICv4 support\n");
		gic_domain = domain;
		vpe_domain_ops = vpe_ops;
		sgi_domain_ops = sgi_ops;
		return 0;
	}

	pr_err("ITS: No GICv4 VPE domain allocated\n");
	return -ENODEV;
}
