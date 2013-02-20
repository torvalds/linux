#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/msi.h>
#include <linux/irq.h>
#include <linux/pci.h>

#include <asm/hw_irq.h>
#include <asm/irq_remapping.h>
#include <asm/processor.h>
#include <asm/x86_init.h>
#include <asm/apic.h>

#include "irq_remapping.h"

int irq_remapping_enabled;

int disable_irq_remap;
int disable_sourceid_checking;
int no_x2apic_optout;

static struct irq_remap_ops *remap_ops;

static int msi_alloc_remapped_irq(struct pci_dev *pdev, int irq, int nvec);
static int msi_setup_remapped_irq(struct pci_dev *pdev, unsigned int irq,
				  int index, int sub_handle);
static int set_remapped_irq_affinity(struct irq_data *data,
				     const struct cpumask *mask,
				     bool force);

static bool irq_remapped(struct irq_cfg *cfg)
{
	return (cfg->remapped == 1);
}

static void irq_remapping_disable_io_apic(void)
{
	/*
	 * With interrupt-remapping, for now we will use virtual wire A
	 * mode, as virtual wire B is little complex (need to configure
	 * both IOAPIC RTE as well as interrupt-remapping table entry).
	 * As this gets called during crash dump, keep this simple for
	 * now.
	 */
	if (cpu_has_apic || apic_from_smp_config())
		disconnect_bsp_APIC(0);
}

static int do_setup_msi_irqs(struct pci_dev *dev, int nvec)
{
	int node, ret, sub_handle, index = 0;
	unsigned int irq;
	struct msi_desc *msidesc;

	nvec = __roundup_pow_of_two(nvec);

	WARN_ON(!list_is_singular(&dev->msi_list));
	msidesc = list_entry(dev->msi_list.next, struct msi_desc, list);
	WARN_ON(msidesc->irq);
	WARN_ON(msidesc->msi_attrib.multiple);

	node = dev_to_node(&dev->dev);
	irq = __create_irqs(get_nr_irqs_gsi(), nvec, node);
	if (irq == 0)
		return -ENOSPC;

	msidesc->msi_attrib.multiple = ilog2(nvec);
	for (sub_handle = 0; sub_handle < nvec; sub_handle++) {
		if (!sub_handle) {
			index = msi_alloc_remapped_irq(dev, irq, nvec);
			if (index < 0) {
				ret = index;
				goto error;
			}
		} else {
			ret = msi_setup_remapped_irq(dev, irq + sub_handle,
						     index, sub_handle);
			if (ret < 0)
				goto error;
		}
		ret = setup_msi_irq(dev, msidesc, irq, sub_handle);
		if (ret < 0)
			goto error;
	}
	return 0;

error:
	destroy_irqs(irq, nvec);

	/*
	 * Restore altered MSI descriptor fields and prevent just destroyed
	 * IRQs from tearing down again in default_teardown_msi_irqs()
	 */
	msidesc->irq = 0;
	msidesc->msi_attrib.multiple = 0;

	return ret;
}

static int do_setup_msix_irqs(struct pci_dev *dev, int nvec)
{
	int node, ret, sub_handle, index = 0;
	struct msi_desc *msidesc;
	unsigned int irq;

	node		= dev_to_node(&dev->dev);
	irq		= get_nr_irqs_gsi();
	sub_handle	= 0;

	list_for_each_entry(msidesc, &dev->msi_list, list) {

		irq = create_irq_nr(irq, node);
		if (irq == 0)
			return -1;

		if (sub_handle == 0)
			ret = index = msi_alloc_remapped_irq(dev, irq, nvec);
		else
			ret = msi_setup_remapped_irq(dev, irq, index, sub_handle);

		if (ret < 0)
			goto error;

		ret = setup_msi_irq(dev, msidesc, irq, 0);
		if (ret < 0)
			goto error;

		sub_handle += 1;
		irq        += 1;
	}

	return 0;

error:
	destroy_irq(irq);
	return ret;
}

static int irq_remapping_setup_msi_irqs(struct pci_dev *dev,
					int nvec, int type)
{
	if (type == PCI_CAP_ID_MSI)
		return do_setup_msi_irqs(dev, nvec);
	else
		return do_setup_msix_irqs(dev, nvec);
}

void eoi_ioapic_pin_remapped(int apic, int pin, int vector)
{
	/*
	 * Intr-remapping uses pin number as the virtual vector
	 * in the RTE. Actual vector is programmed in
	 * intr-remapping table entry. Hence for the io-apic
	 * EOI we use the pin number.
	 */
	io_apic_eoi(apic, pin);
}

static void __init irq_remapping_modify_x86_ops(void)
{
	x86_io_apic_ops.disable		= irq_remapping_disable_io_apic;
	x86_io_apic_ops.set_affinity	= set_remapped_irq_affinity;
	x86_io_apic_ops.setup_entry	= setup_ioapic_remapped_entry;
	x86_io_apic_ops.eoi_ioapic_pin	= eoi_ioapic_pin_remapped;
	x86_msi.setup_msi_irqs		= irq_remapping_setup_msi_irqs;
	x86_msi.setup_hpet_msi		= setup_hpet_msi_remapped;
	x86_msi.compose_msi_msg		= compose_remapped_msi_msg;
}

static __init int setup_nointremap(char *str)
{
	disable_irq_remap = 1;
	return 0;
}
early_param("nointremap", setup_nointremap);

static __init int setup_irqremap(char *str)
{
	if (!str)
		return -EINVAL;

	while (*str) {
		if (!strncmp(str, "on", 2))
			disable_irq_remap = 0;
		else if (!strncmp(str, "off", 3))
			disable_irq_remap = 1;
		else if (!strncmp(str, "nosid", 5))
			disable_sourceid_checking = 1;
		else if (!strncmp(str, "no_x2apic_optout", 16))
			no_x2apic_optout = 1;

		str += strcspn(str, ",");
		while (*str == ',')
			str++;
	}

	return 0;
}
early_param("intremap", setup_irqremap);

void __init setup_irq_remapping_ops(void)
{
	remap_ops = &intel_irq_remap_ops;

#ifdef CONFIG_AMD_IOMMU
	if (amd_iommu_irq_ops.prepare() == 0)
		remap_ops = &amd_iommu_irq_ops;
#endif
}

int irq_remapping_supported(void)
{
	if (disable_irq_remap)
		return 0;

	if (!remap_ops || !remap_ops->supported)
		return 0;

	return remap_ops->supported();
}

int __init irq_remapping_prepare(void)
{
	if (!remap_ops || !remap_ops->prepare)
		return -ENODEV;

	return remap_ops->prepare();
}

int __init irq_remapping_enable(void)
{
	int ret;

	if (!remap_ops || !remap_ops->enable)
		return -ENODEV;

	ret = remap_ops->enable();

	if (irq_remapping_enabled)
		irq_remapping_modify_x86_ops();

	return ret;
}

void irq_remapping_disable(void)
{
	if (!irq_remapping_enabled ||
	    !remap_ops ||
	    !remap_ops->disable)
		return;

	remap_ops->disable();
}

int irq_remapping_reenable(int mode)
{
	if (!irq_remapping_enabled ||
	    !remap_ops ||
	    !remap_ops->reenable)
		return 0;

	return remap_ops->reenable(mode);
}

int __init irq_remap_enable_fault_handling(void)
{
	if (!irq_remapping_enabled)
		return 0;

	if (!remap_ops || !remap_ops->enable_faulting)
		return -ENODEV;

	return remap_ops->enable_faulting();
}

int setup_ioapic_remapped_entry(int irq,
				struct IO_APIC_route_entry *entry,
				unsigned int destination, int vector,
				struct io_apic_irq_attr *attr)
{
	if (!remap_ops || !remap_ops->setup_ioapic_entry)
		return -ENODEV;

	return remap_ops->setup_ioapic_entry(irq, entry, destination,
					     vector, attr);
}

int set_remapped_irq_affinity(struct irq_data *data, const struct cpumask *mask,
			      bool force)
{
	if (!config_enabled(CONFIG_SMP) || !remap_ops ||
	    !remap_ops->set_affinity)
		return 0;

	return remap_ops->set_affinity(data, mask, force);
}

void free_remapped_irq(int irq)
{
	struct irq_cfg *cfg = irq_get_chip_data(irq);

	if (!remap_ops || !remap_ops->free_irq)
		return;

	if (irq_remapped(cfg))
		remap_ops->free_irq(irq);
}

void compose_remapped_msi_msg(struct pci_dev *pdev,
			      unsigned int irq, unsigned int dest,
			      struct msi_msg *msg, u8 hpet_id)
{
	struct irq_cfg *cfg = irq_get_chip_data(irq);

	if (!irq_remapped(cfg))
		native_compose_msi_msg(pdev, irq, dest, msg, hpet_id);
	else if (remap_ops && remap_ops->compose_msi_msg)
		remap_ops->compose_msi_msg(pdev, irq, dest, msg, hpet_id);
}

static int msi_alloc_remapped_irq(struct pci_dev *pdev, int irq, int nvec)
{
	if (!remap_ops || !remap_ops->msi_alloc_irq)
		return -ENODEV;

	return remap_ops->msi_alloc_irq(pdev, irq, nvec);
}

static int msi_setup_remapped_irq(struct pci_dev *pdev, unsigned int irq,
				  int index, int sub_handle)
{
	if (!remap_ops || !remap_ops->msi_setup_irq)
		return -ENODEV;

	return remap_ops->msi_setup_irq(pdev, irq, index, sub_handle);
}

int setup_hpet_msi_remapped(unsigned int irq, unsigned int id)
{
	if (!remap_ops || !remap_ops->setup_hpet_msi)
		return -ENODEV;

	return remap_ops->setup_hpet_msi(irq, id);
}

void panic_if_irq_remap(const char *msg)
{
	if (irq_remapping_enabled)
		panic(msg);
}

static void ir_ack_apic_edge(struct irq_data *data)
{
	ack_APIC_irq();
}

static void ir_ack_apic_level(struct irq_data *data)
{
	ack_APIC_irq();
	eoi_ioapic_irq(data->irq, data->chip_data);
}

static void ir_print_prefix(struct irq_data *data, struct seq_file *p)
{
	seq_printf(p, " IR-%s", data->chip->name);
}

void irq_remap_modify_chip_defaults(struct irq_chip *chip)
{
	chip->irq_print_chip = ir_print_prefix;
	chip->irq_ack = ir_ack_apic_edge;
	chip->irq_eoi = ir_ack_apic_level;
	chip->irq_set_affinity = x86_io_apic_ops.set_affinity;
}

bool setup_remapped_irq(int irq, struct irq_cfg *cfg, struct irq_chip *chip)
{
	if (!irq_remapped(cfg))
		return false;
	irq_set_status_flags(irq, IRQ_MOVE_PCNTXT);
	irq_remap_modify_chip_defaults(chip);
	return true;
}
