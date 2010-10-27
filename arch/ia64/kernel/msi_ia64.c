/*
 * MSI hooks for standard x86 apic
 */

#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/dmar.h>
#include <asm/smp.h>
#include <asm/msidef.h>

static struct irq_chip	ia64_msi_chip;

#ifdef CONFIG_SMP
static int ia64_set_msi_irq_affinity(unsigned int irq,
				      const cpumask_t *cpu_mask)
{
	struct msi_msg msg;
	u32 addr, data;
	int cpu = first_cpu(*cpu_mask);

	if (!cpu_online(cpu))
		return -1;

	if (irq_prepare_move(irq, cpu))
		return -1;

	get_cached_msi_msg(irq, &msg);

	addr = msg.address_lo;
	addr &= MSI_ADDR_DEST_ID_MASK;
	addr |= MSI_ADDR_DEST_ID_CPU(cpu_physical_id(cpu));
	msg.address_lo = addr;

	data = msg.data;
	data &= MSI_DATA_VECTOR_MASK;
	data |= MSI_DATA_VECTOR(irq_to_vector(irq));
	msg.data = data;

	write_msi_msg(irq, &msg);
	cpumask_copy(irq_desc[irq].affinity, cpumask_of(cpu));

	return 0;
}
#endif /* CONFIG_SMP */

int ia64_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	struct msi_msg	msg;
	unsigned long	dest_phys_id;
	int	irq, vector;
	cpumask_t mask;

	irq = create_irq();
	if (irq < 0)
		return irq;

	set_irq_msi(irq, desc);
	cpus_and(mask, irq_to_domain(irq), cpu_online_map);
	dest_phys_id = cpu_physical_id(first_cpu(mask));
	vector = irq_to_vector(irq);

	msg.address_hi = 0;
	msg.address_lo =
		MSI_ADDR_HEADER |
		MSI_ADDR_DEST_MODE_PHYS |
		MSI_ADDR_REDIRECTION_CPU |
		MSI_ADDR_DEST_ID_CPU(dest_phys_id);

	msg.data =
		MSI_DATA_TRIGGER_EDGE |
		MSI_DATA_LEVEL_ASSERT |
		MSI_DATA_DELIVERY_FIXED |
		MSI_DATA_VECTOR(vector);

	write_msi_msg(irq, &msg);
	set_irq_chip_and_handler(irq, &ia64_msi_chip, handle_edge_irq);

	return 0;
}

void ia64_teardown_msi_irq(unsigned int irq)
{
	destroy_irq(irq);
}

static void ia64_ack_msi_irq(unsigned int irq)
{
	irq_complete_move(irq);
	move_native_irq(irq);
	ia64_eoi();
}

static int ia64_msi_retrigger_irq(unsigned int irq)
{
	unsigned int vector = irq_to_vector(irq);
	ia64_resend_irq(vector);

	return 1;
}

/*
 * Generic ops used on most IA64 platforms.
 */
static struct irq_chip ia64_msi_chip = {
	.name		= "PCI-MSI",
	.mask		= mask_msi_irq,
	.unmask		= unmask_msi_irq,
	.ack		= ia64_ack_msi_irq,
#ifdef CONFIG_SMP
	.set_affinity	= ia64_set_msi_irq_affinity,
#endif
	.retrigger	= ia64_msi_retrigger_irq,
};


int arch_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	if (platform_setup_msi_irq)
		return platform_setup_msi_irq(pdev, desc);

	return ia64_setup_msi_irq(pdev, desc);
}

void arch_teardown_msi_irq(unsigned int irq)
{
	if (platform_teardown_msi_irq)
		return platform_teardown_msi_irq(irq);

	return ia64_teardown_msi_irq(irq);
}

#ifdef CONFIG_DMAR
#ifdef CONFIG_SMP
static int dmar_msi_set_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	struct msi_msg msg;
	int cpu = cpumask_first(mask);

	if (!cpu_online(cpu))
		return -1;

	if (irq_prepare_move(irq, cpu))
		return -1;

	dmar_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID_CPU(cpu_physical_id(cpu));

	dmar_msi_write(irq, &msg);
	cpumask_copy(irq_desc[irq].affinity, mask);

	return 0;
}
#endif /* CONFIG_SMP */

static struct irq_chip dmar_msi_type = {
	.name = "DMAR_MSI",
	.unmask = dmar_msi_unmask,
	.mask = dmar_msi_mask,
	.ack = ia64_ack_msi_irq,
#ifdef CONFIG_SMP
	.set_affinity = dmar_msi_set_affinity,
#endif
	.retrigger = ia64_msi_retrigger_irq,
};

static int
msi_compose_msg(struct pci_dev *pdev, unsigned int irq, struct msi_msg *msg)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	unsigned dest;
	cpumask_t mask;

	cpus_and(mask, irq_to_domain(irq), cpu_online_map);
	dest = cpu_physical_id(first_cpu(mask));

	msg->address_hi = 0;
	msg->address_lo =
		MSI_ADDR_HEADER |
		MSI_ADDR_DEST_MODE_PHYS |
		MSI_ADDR_REDIRECTION_CPU |
		MSI_ADDR_DEST_ID_CPU(dest);

	msg->data =
		MSI_DATA_TRIGGER_EDGE |
		MSI_DATA_LEVEL_ASSERT |
		MSI_DATA_DELIVERY_FIXED |
		MSI_DATA_VECTOR(cfg->vector);
	return 0;
}

int arch_setup_dmar_msi(unsigned int irq)
{
	int ret;
	struct msi_msg msg;

	ret = msi_compose_msg(NULL, irq, &msg);
	if (ret < 0)
		return ret;
	dmar_msi_write(irq, &msg);
	set_irq_chip_and_handler_name(irq, &dmar_msi_type, handle_edge_irq,
		"edge");
	return 0;
}
#endif /* CONFIG_DMAR */

