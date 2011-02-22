/*
 * Architecture specific OF callbacks.
 */
#include <linux/bootmem.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include <asm/irq_controller.h>

char __initdata cmd_line[COMMAND_LINE_SIZE];
static LIST_HEAD(irq_domains);
static DEFINE_RAW_SPINLOCK(big_irq_lock);

void add_interrupt_host(struct irq_domain *ih)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&big_irq_lock, flags);
	list_add(&ih->l, &irq_domains);
	raw_spin_unlock_irqrestore(&big_irq_lock, flags);
}

static struct irq_domain *get_ih_from_node(struct device_node *controller)
{
	struct irq_domain *ih, *found = NULL;
	unsigned long flags;

	raw_spin_lock_irqsave(&big_irq_lock, flags);
	list_for_each_entry(ih, &irq_domains, l) {
		if (ih->controller ==  controller) {
			found = ih;
			break;
		}
	}
	raw_spin_unlock_irqrestore(&big_irq_lock, flags);
	return found;
}

unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	struct irq_domain *ih;
	u32 virq, type;
	int ret;

	ih = get_ih_from_node(controller);
	if (!ih)
		return 0;
	ret = ih->xlate(ih, intspec, intsize, &virq, &type);
	if (ret)
		return ret;
	if (type == IRQ_TYPE_NONE)
		return virq;
	/* set the mask if it is different from current */
	if (type == (irq_to_desc(virq)->status & IRQF_TRIGGER_MASK))
		set_irq_type(virq, type);
	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

unsigned long pci_address_to_pio(phys_addr_t address)
{
	/*
	 * The ioport address can be directly used by inX / outX
	 */
	BUG_ON(address >= (1 << 16));
	return (unsigned long)address;
}
EXPORT_SYMBOL_GPL(pci_address_to_pio);

void __init early_init_dt_scan_chosen_arch(unsigned long node)
{
	BUG();
}

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	BUG();
}

void * __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return __alloc_bootmem(size, align, __pa(MAX_DMA_ADDRESS));
}

void __init add_dtb(u64 data)
{
	initial_boot_params = phys_to_virt((u64) (u32) data +
				offsetof(struct setup_data, data));
}
