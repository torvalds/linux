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
