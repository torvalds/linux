/*
 * Copyright (C) 2016,2017 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
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

	vpe_base_irq = __irq_domain_alloc_irqs(vm->domain, -1, vm->nr_vpes,
					       NUMA_NO_NODE, vm,
					       false, NULL);
	if (vpe_base_irq <= 0)
		goto err;

	for (i = 0; i < vm->nr_vpes; i++)
		vm->vpes[i]->irq = vpe_base_irq + i;

	return 0;

err:
	if (vm->domain)
		irq_domain_remove(vm->domain);
	if (vm->fwnode)
		irq_domain_free_fwnode(vm->fwnode);

	return -ENOMEM;
}

void its_free_vcpu_irqs(struct its_vm *vm)
{
	irq_domain_free_irqs(vm->vpes[0]->irq, vm->nr_vpes);
	irq_domain_remove(vm->domain);
	irq_domain_free_fwnode(vm->fwnode);
}

static int its_send_vpe_cmd(struct its_vpe *vpe, struct its_cmd_info *info)
{
	return irq_set_vcpu_affinity(vpe->irq, info);
}

int its_schedule_vpe(struct its_vpe *vpe, bool on)
{
	struct its_cmd_info info;

	WARN_ON(preemptible());

	info.cmd_type = on ? SCHEDULE_VPE : DESCHEDULE_VPE;

	return its_send_vpe_cmd(vpe, &info);
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
		.map      = map,
	};

	/*
	 * The host will never see that interrupt firing again, so it
	 * is vital that we don't do any lazy masking.
	 */
	irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY);

	return irq_set_vcpu_affinity(irq, &info);
}

int its_get_vlpi(int irq, struct its_vlpi_map *map)
{
	struct its_cmd_info info = {
		.cmd_type = GET_VLPI,
		.map      = map,
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
		.config   = config,
	};

	return irq_set_vcpu_affinity(irq, &info);
}

int its_init_v4(struct irq_domain *domain, const struct irq_domain_ops *ops)
{
	if (domain) {
		pr_info("ITS: Enabling GICv4 support\n");
		gic_domain = domain;
		vpe_domain_ops = ops;
		return 0;
	}

	pr_err("ITS: No GICv4 VPE domain allocated\n");
	return -ENODEV;
}
