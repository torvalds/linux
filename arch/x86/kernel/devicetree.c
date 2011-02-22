/*
 * Architecture specific OF callbacks.
 */
#include <linux/bootmem.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include <asm/irq_controller.h>
#include <asm/apic.h>

__initdata u64 initial_dtb;
char __initdata cmd_line[COMMAND_LINE_SIZE];
static LIST_HEAD(irq_domains);
static DEFINE_RAW_SPINLOCK(big_irq_lock);

int __initdata of_ioapic;

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
	initial_dtb = data + offsetof(struct setup_data, data);
}

static void __init dtb_lapic_setup(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	if (apic_force_enable())
		return;

	smp_found_config = 1;
	pic_mode = 1;
	/* Required for ioapic registration */
	set_fixmap_nocache(FIX_APIC_BASE, mp_lapic_addr);
	if (boot_cpu_physical_apicid == -1U)
		boot_cpu_physical_apicid = read_apic_id();

	generic_processor_info(boot_cpu_physical_apicid,
			GET_APIC_VERSION(apic_read(APIC_LVR)));
#endif
}

#ifdef CONFIG_X86_IO_APIC
static unsigned int ioapic_id;

static void __init dtb_add_ioapic(struct device_node *dn)
{
	struct resource r;
	int ret;

	ret = of_address_to_resource(dn, 0, &r);
	if (ret) {
		printk(KERN_ERR "Can't obtain address from node %s.\n",
				dn->full_name);
		return;
	}
	mp_register_ioapic(++ioapic_id, r.start, gsi_top);
}

static void __init dtb_ioapic_setup(void)
{
	struct device_node *dn;

	if (!smp_found_config)
		return;

	for_each_compatible_node(dn, NULL, "intel,ce4100-ioapic")
		dtb_add_ioapic(dn);

	if (nr_ioapics) {
		of_ioapic = 1;
		return;
	}
	printk(KERN_ERR "Error: No information about IO-APIC in OF.\n");
	smp_found_config = 0;
}
#else
static void __init dtb_ioapic_setup(void) {}
#endif

static void __init dtb_apic_setup(void)
{
	dtb_lapic_setup();
	dtb_ioapic_setup();
}

void __init x86_dtb_find_config(void)
{
	if (initial_dtb)
		smp_found_config = 1;
	else
		printk(KERN_ERR "Missing device tree!.\n");
}

void __init x86_dtb_get_config(unsigned int unused)
{
	u32 size, map_len;
	void *new_dtb;

	if (!initial_dtb)
		return;

	map_len = max(PAGE_SIZE - (initial_dtb & ~PAGE_MASK),
			(u64)sizeof(struct boot_param_header));

	initial_boot_params = early_memremap(initial_dtb, map_len);
	size = be32_to_cpu(initial_boot_params->totalsize);
	if (map_len < size) {
		early_iounmap(initial_boot_params, map_len);
		initial_boot_params = early_memremap(initial_dtb, size);
		map_len = size;
	}

	new_dtb = alloc_bootmem(size);
	memcpy(new_dtb, initial_boot_params, size);
	early_iounmap(initial_boot_params, map_len);

	initial_boot_params = new_dtb;

	/* root level address cells */
	of_scan_flat_dt(early_init_dt_scan_root, NULL);

	unflatten_device_tree();
	dtb_apic_setup();
}
