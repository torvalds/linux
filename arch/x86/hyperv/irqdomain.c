// SPDX-License-Identifier: GPL-2.0

/*
 * Irqdomain for Linux to run as the root partition on Microsoft Hypervisor.
 *
 * Authors:
 *  Sunil Muthuswamy <sunilmut@microsoft.com>
 *  Wei Liu <wei.liu@kernel.org>
 */

#include <linux/pci.h>
#include <linux/irq.h>
#include <asm/mshyperv.h>

static int hv_map_interrupt(union hv_device_id device_id, bool level,
		int cpu, int vector, struct hv_interrupt_entry *entry)
{
	struct hv_input_map_device_interrupt *input;
	struct hv_output_map_device_interrupt *output;
	struct hv_device_interrupt_descriptor *intr_desc;
	unsigned long flags;
	u64 status;
	int nr_bank, var_size;

	local_irq_save(flags);

	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	output = *this_cpu_ptr(hyperv_pcpu_output_arg);

	intr_desc = &input->interrupt_descriptor;
	memset(input, 0, sizeof(*input));
	input->partition_id = hv_current_partition_id;
	input->device_id = device_id.as_uint64;
	intr_desc->interrupt_type = HV_X64_INTERRUPT_TYPE_FIXED;
	intr_desc->vector_count = 1;
	intr_desc->target.vector = vector;

	if (level)
		intr_desc->trigger_mode = HV_INTERRUPT_TRIGGER_MODE_LEVEL;
	else
		intr_desc->trigger_mode = HV_INTERRUPT_TRIGGER_MODE_EDGE;

	intr_desc->target.vp_set.valid_bank_mask = 0;
	intr_desc->target.vp_set.format = HV_GENERIC_SET_SPARSE_4K;
	nr_bank = cpumask_to_vpset(&(intr_desc->target.vp_set), cpumask_of(cpu));
	if (nr_bank < 0) {
		local_irq_restore(flags);
		pr_err("%s: unable to generate VP set\n", __func__);
		return EINVAL;
	}
	intr_desc->target.flags = HV_DEVICE_INTERRUPT_TARGET_PROCESSOR_SET;

	/*
	 * var-sized hypercall, var-size starts after vp_mask (thus
	 * vp_set.format does not count, but vp_set.valid_bank_mask
	 * does).
	 */
	var_size = nr_bank + 1;

	status = hv_do_rep_hypercall(HVCALL_MAP_DEVICE_INTERRUPT, 0, var_size,
			input, output);
	*entry = output->interrupt_entry;

	local_irq_restore(flags);

	if ((status & HV_HYPERCALL_RESULT_MASK) != HV_STATUS_SUCCESS)
		pr_err("%s: hypercall failed, status %lld\n", __func__, status);

	return status & HV_HYPERCALL_RESULT_MASK;
}

static int hv_unmap_interrupt(u64 id, struct hv_interrupt_entry *old_entry)
{
	unsigned long flags;
	struct hv_input_unmap_device_interrupt *input;
	struct hv_interrupt_entry *intr_entry;
	u64 status;

	local_irq_save(flags);
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);

	memset(input, 0, sizeof(*input));
	intr_entry = &input->interrupt_entry;
	input->partition_id = hv_current_partition_id;
	input->device_id = id;
	*intr_entry = *old_entry;

	status = hv_do_hypercall(HVCALL_UNMAP_DEVICE_INTERRUPT, input, NULL);
	local_irq_restore(flags);

	return status & HV_HYPERCALL_RESULT_MASK;
}

#ifdef CONFIG_PCI_MSI
struct rid_data {
	struct pci_dev *bridge;
	u32 rid;
};

static int get_rid_cb(struct pci_dev *pdev, u16 alias, void *data)
{
	struct rid_data *rd = data;
	u8 bus = PCI_BUS_NUM(rd->rid);

	if (pdev->bus->number != bus || PCI_BUS_NUM(alias) != bus) {
		rd->bridge = pdev;
		rd->rid = alias;
	}

	return 0;
}

static union hv_device_id hv_build_pci_dev_id(struct pci_dev *dev)
{
	union hv_device_id dev_id;
	struct rid_data data = {
		.bridge = NULL,
		.rid = PCI_DEVID(dev->bus->number, dev->devfn)
	};

	pci_for_each_dma_alias(dev, get_rid_cb, &data);

	dev_id.as_uint64 = 0;
	dev_id.device_type = HV_DEVICE_TYPE_PCI;
	dev_id.pci.segment = pci_domain_nr(dev->bus);

	dev_id.pci.bdf.bus = PCI_BUS_NUM(data.rid);
	dev_id.pci.bdf.device = PCI_SLOT(data.rid);
	dev_id.pci.bdf.function = PCI_FUNC(data.rid);
	dev_id.pci.source_shadow = HV_SOURCE_SHADOW_NONE;

	if (data.bridge) {
		int pos;

		/*
		 * Microsoft Hypervisor requires a bus range when the bridge is
		 * running in PCI-X mode.
		 *
		 * To distinguish conventional vs PCI-X bridge, we can check
		 * the bridge's PCI-X Secondary Status Register, Secondary Bus
		 * Mode and Frequency bits. See PCI Express to PCI/PCI-X Bridge
		 * Specification Revision 1.0 5.2.2.1.3.
		 *
		 * Value zero means it is in conventional mode, otherwise it is
		 * in PCI-X mode.
		 */

		pos = pci_find_capability(data.bridge, PCI_CAP_ID_PCIX);
		if (pos) {
			u16 status;

			pci_read_config_word(data.bridge, pos +
					PCI_X_BRIDGE_SSTATUS, &status);

			if (status & PCI_X_SSTATUS_FREQ) {
				/* Non-zero, PCI-X mode */
				u8 sec_bus, sub_bus;

				dev_id.pci.source_shadow = HV_SOURCE_SHADOW_BRIDGE_BUS_RANGE;

				pci_read_config_byte(data.bridge, PCI_SECONDARY_BUS, &sec_bus);
				dev_id.pci.shadow_bus_range.secondary_bus = sec_bus;
				pci_read_config_byte(data.bridge, PCI_SUBORDINATE_BUS, &sub_bus);
				dev_id.pci.shadow_bus_range.subordinate_bus = sub_bus;
			}
		}
	}

	return dev_id;
}

static int hv_map_msi_interrupt(struct pci_dev *dev, int cpu, int vector,
				struct hv_interrupt_entry *entry)
{
	union hv_device_id device_id = hv_build_pci_dev_id(dev);

	return hv_map_interrupt(device_id, false, cpu, vector, entry);
}

static inline void entry_to_msi_msg(struct hv_interrupt_entry *entry, struct msi_msg *msg)
{
	/* High address is always 0 */
	msg->address_hi = 0;
	msg->address_lo = entry->msi_entry.address.as_uint32;
	msg->data = entry->msi_entry.data.as_uint32;
}

static int hv_unmap_msi_interrupt(struct pci_dev *dev, struct hv_interrupt_entry *old_entry);
static void hv_irq_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct msi_desc *msidesc;
	struct pci_dev *dev;
	struct hv_interrupt_entry out_entry, *stored_entry;
	struct irq_cfg *cfg = irqd_cfg(data);
	cpumask_t *affinity;
	int cpu;
	u64 status;

	msidesc = irq_data_get_msi_desc(data);
	dev = msi_desc_to_pci_dev(msidesc);

	if (!cfg) {
		pr_debug("%s: cfg is NULL", __func__);
		return;
	}

	affinity = irq_data_get_effective_affinity_mask(data);
	cpu = cpumask_first_and(affinity, cpu_online_mask);

	if (data->chip_data) {
		/*
		 * This interrupt is already mapped. Let's unmap first.
		 *
		 * We don't use retarget interrupt hypercalls here because
		 * Microsoft Hypervisor doens't allow root to change the vector
		 * or specify VPs outside of the set that is initially used
		 * during mapping.
		 */
		stored_entry = data->chip_data;
		data->chip_data = NULL;

		status = hv_unmap_msi_interrupt(dev, stored_entry);

		kfree(stored_entry);

		if (status != HV_STATUS_SUCCESS) {
			pr_debug("%s: failed to unmap, status %lld", __func__, status);
			return;
		}
	}

	stored_entry = kzalloc(sizeof(*stored_entry), GFP_ATOMIC);
	if (!stored_entry) {
		pr_debug("%s: failed to allocate chip data\n", __func__);
		return;
	}

	status = hv_map_msi_interrupt(dev, cpu, cfg->vector, &out_entry);
	if (status != HV_STATUS_SUCCESS) {
		kfree(stored_entry);
		return;
	}

	*stored_entry = out_entry;
	data->chip_data = stored_entry;
	entry_to_msi_msg(&out_entry, msg);

	return;
}

static int hv_unmap_msi_interrupt(struct pci_dev *dev, struct hv_interrupt_entry *old_entry)
{
	return hv_unmap_interrupt(hv_build_pci_dev_id(dev).as_uint64, old_entry);
}

static void hv_teardown_msi_irq_common(struct pci_dev *dev, struct msi_desc *msidesc, int irq)
{
	u64 status;
	struct hv_interrupt_entry old_entry;
	struct irq_desc *desc;
	struct irq_data *data;
	struct msi_msg msg;

	desc = irq_to_desc(irq);
	if (!desc) {
		pr_debug("%s: no irq desc\n", __func__);
		return;
	}

	data = &desc->irq_data;
	if (!data) {
		pr_debug("%s: no irq data\n", __func__);
		return;
	}

	if (!data->chip_data) {
		pr_debug("%s: no chip data\n!", __func__);
		return;
	}

	old_entry = *(struct hv_interrupt_entry *)data->chip_data;
	entry_to_msi_msg(&old_entry, &msg);

	kfree(data->chip_data);
	data->chip_data = NULL;

	status = hv_unmap_msi_interrupt(dev, &old_entry);

	if (status != HV_STATUS_SUCCESS) {
		pr_err("%s: hypercall failed, status %lld\n", __func__, status);
		return;
	}
}

static void hv_msi_domain_free_irqs(struct irq_domain *domain, struct device *dev)
{
	int i;
	struct msi_desc *entry;
	struct pci_dev *pdev;

	if (WARN_ON_ONCE(!dev_is_pci(dev)))
		return;

	pdev = to_pci_dev(dev);

	for_each_pci_msi_entry(entry, pdev) {
		if (entry->irq) {
			for (i = 0; i < entry->nvec_used; i++) {
				hv_teardown_msi_irq_common(pdev, entry, entry->irq + i);
				irq_domain_free_irqs(entry->irq + i, 1);
			}
		}
	}
}

/*
 * IRQ Chip for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI or MSI-X Capability Structure.
 */
static struct irq_chip hv_pci_msi_controller = {
	.name			= "HV-PCI-MSI",
	.irq_unmask		= pci_msi_unmask_irq,
	.irq_mask		= pci_msi_mask_irq,
	.irq_ack		= irq_chip_ack_parent,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_compose_msi_msg	= hv_irq_compose_msi_msg,
	.irq_set_affinity	= msi_domain_set_affinity,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

static struct msi_domain_ops pci_msi_domain_ops = {
	.domain_free_irqs	= hv_msi_domain_free_irqs,
	.msi_prepare		= pci_msi_prepare,
};

static struct msi_domain_info hv_pci_msi_domain_info = {
	.flags		= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
			  MSI_FLAG_PCI_MSIX,
	.ops		= &pci_msi_domain_ops,
	.chip		= &hv_pci_msi_controller,
	.handler	= handle_edge_irq,
	.handler_name	= "edge",
};

struct irq_domain * __init hv_create_pci_msi_domain(void)
{
	struct irq_domain *d = NULL;
	struct fwnode_handle *fn;

	fn = irq_domain_alloc_named_fwnode("HV-PCI-MSI");
	if (fn)
		d = pci_msi_create_irq_domain(fn, &hv_pci_msi_domain_info, x86_vector_domain);

	/* No point in going further if we can't get an irq domain */
	BUG_ON(!d);

	return d;
}

#endif /* CONFIG_PCI_MSI */

int hv_unmap_ioapic_interrupt(int ioapic_id, struct hv_interrupt_entry *entry)
{
	union hv_device_id device_id;

	device_id.as_uint64 = 0;
	device_id.device_type = HV_DEVICE_TYPE_IOAPIC;
	device_id.ioapic.ioapic_id = (u8)ioapic_id;

	return hv_unmap_interrupt(device_id.as_uint64, entry);
}
EXPORT_SYMBOL_GPL(hv_unmap_ioapic_interrupt);

int hv_map_ioapic_interrupt(int ioapic_id, bool level, int cpu, int vector,
		struct hv_interrupt_entry *entry)
{
	union hv_device_id device_id;

	device_id.as_uint64 = 0;
	device_id.device_type = HV_DEVICE_TYPE_IOAPIC;
	device_id.ioapic.ioapic_id = (u8)ioapic_id;

	return hv_map_interrupt(device_id, level, cpu, vector, entry);
}
EXPORT_SYMBOL_GPL(hv_map_ioapic_interrupt);
