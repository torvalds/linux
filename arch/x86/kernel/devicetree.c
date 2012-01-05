/*
 * Architecture specific OF callbacks.
 */
#include <linux/bootmem.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/of_pci.h>
#include <linux/initrd.h>

#include <asm/hpet.h>
#include <asm/irq_controller.h>
#include <asm/apic.h>
#include <asm/pci_x86.h>

__initdata u64 initial_dtb;
char __initdata cmd_line[COMMAND_LINE_SIZE];
static LIST_HEAD(irq_domains);
static DEFINE_RAW_SPINLOCK(big_irq_lock);

int __initdata of_ioapic;

#ifdef CONFIG_X86_IO_APIC
static void add_interrupt_host(struct irq_domain *ih)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&big_irq_lock, flags);
	list_add(&ih->l, &irq_domains);
	raw_spin_unlock_irqrestore(&big_irq_lock, flags);
}
#endif

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
		return 0;
	if (type == IRQ_TYPE_NONE)
		return virq;
	irq_set_irq_type(virq, type);
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

#ifdef CONFIG_BLK_DEV_INITRD
void __init early_init_dt_setup_initrd_arch(unsigned long start,
					    unsigned long end)
{
	initrd_start = (unsigned long)__va(start);
	initrd_end = (unsigned long)__va(end);
	initrd_below_start_ok = 1;
}
#endif

void __init add_dtb(u64 data)
{
	initial_dtb = data + offsetof(struct setup_data, data);
}

/*
 * CE4100 ids. Will be moved to machine_device_initcall() once we have it.
 */
static struct of_device_id __initdata ce4100_ids[] = {
	{ .compatible = "intel,ce4100-cp", },
	{ .compatible = "isa", },
	{ .compatible = "pci", },
	{},
};

static int __init add_bus_probe(void)
{
	if (!of_have_populated_dt())
		return 0;

	return of_platform_bus_probe(NULL, ce4100_ids, NULL);
}
module_init(add_bus_probe);

#ifdef CONFIG_PCI
struct device_node *pcibios_get_phb_of_node(struct pci_bus *bus)
{
	struct device_node *np;

	for_each_node_by_type(np, "pci") {
		const void *prop;
		unsigned int bus_min;

		prop = of_get_property(np, "bus-range", NULL);
		if (!prop)
			continue;
		bus_min = be32_to_cpup(prop);
		if (bus->number == bus_min)
			return np;
	}
	return NULL;
}

static int x86_of_pci_irq_enable(struct pci_dev *dev)
{
	struct of_irq oirq;
	u32 virq;
	int ret;
	u8 pin;

	ret = pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	if (ret)
		return ret;
	if (!pin)
		return 0;

	ret = of_irq_map_pci(dev, &oirq);
	if (ret)
		return ret;

	virq = irq_create_of_mapping(oirq.controller, oirq.specifier,
			oirq.size);
	if (virq == 0)
		return -EINVAL;
	dev->irq = virq;
	return 0;
}

static void x86_of_pci_irq_disable(struct pci_dev *dev)
{
}

void __cpuinit x86_of_pci_init(void)
{
	pcibios_enable_irq = x86_of_pci_irq_enable;
	pcibios_disable_irq = x86_of_pci_irq_disable;
}
#endif

static void __init dtb_setup_hpet(void)
{
#ifdef CONFIG_HPET_TIMER
	struct device_node *dn;
	struct resource r;
	int ret;

	dn = of_find_compatible_node(NULL, NULL, "intel,ce4100-hpet");
	if (!dn)
		return;
	ret = of_address_to_resource(dn, 0, &r);
	if (ret) {
		WARN_ON(1);
		return;
	}
	hpet_address = r.start;
#endif
}

static void __init dtb_lapic_setup(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	struct device_node *dn;
	struct resource r;
	int ret;

	dn = of_find_compatible_node(NULL, NULL, "intel,ce4100-lapic");
	if (!dn)
		return;

	ret = of_address_to_resource(dn, 0, &r);
	if (WARN_ON(ret))
		return;

	/* Did the boot loader setup the local APIC ? */
	if (!cpu_has_apic) {
		if (apic_force_enable(r.start))
			return;
	}
	smp_found_config = 1;
	pic_mode = 1;
	register_lapic_address(r.start);
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

	for_each_compatible_node(dn, NULL, "intel,ce4100-ioapic")
		dtb_add_ioapic(dn);

	if (nr_ioapics) {
		of_ioapic = 1;
		return;
	}
	printk(KERN_ERR "Error: No information about IO-APIC in OF.\n");
}
#else
static void __init dtb_ioapic_setup(void) {}
#endif

static void __init dtb_apic_setup(void)
{
	dtb_lapic_setup();
	dtb_ioapic_setup();
}

#ifdef CONFIG_OF_FLATTREE
static void __init x86_flattree_get_config(void)
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
}
#else
static inline void x86_flattree_get_config(void) { }
#endif

void __init x86_dtb_init(void)
{
	x86_flattree_get_config();

	if (!of_have_populated_dt())
		return;

	dtb_setup_hpet();
	dtb_apic_setup();
}

#ifdef CONFIG_X86_IO_APIC

struct of_ioapic_type {
	u32 out_type;
	u32 trigger;
	u32 polarity;
};

static struct of_ioapic_type of_ioapic_type[] =
{
	{
		.out_type	= IRQ_TYPE_EDGE_RISING,
		.trigger	= IOAPIC_EDGE,
		.polarity	= 1,
	},
	{
		.out_type	= IRQ_TYPE_LEVEL_LOW,
		.trigger	= IOAPIC_LEVEL,
		.polarity	= 0,
	},
	{
		.out_type	= IRQ_TYPE_LEVEL_HIGH,
		.trigger	= IOAPIC_LEVEL,
		.polarity	= 1,
	},
	{
		.out_type	= IRQ_TYPE_EDGE_FALLING,
		.trigger	= IOAPIC_EDGE,
		.polarity	= 0,
	},
};

static int ioapic_xlate(struct irq_domain *id, const u32 *intspec, u32 intsize,
			u32 *out_hwirq, u32 *out_type)
{
	struct mp_ioapic_gsi *gsi_cfg;
	struct io_apic_irq_attr attr;
	struct of_ioapic_type *it;
	u32 line, idx, type;

	if (intsize < 2)
		return -EINVAL;

	line = *intspec;
	idx = (u32) id->priv;
	gsi_cfg = mp_ioapic_gsi_routing(idx);
	*out_hwirq = line + gsi_cfg->gsi_base;

	intspec++;
	type = *intspec;

	if (type >= ARRAY_SIZE(of_ioapic_type))
		return -EINVAL;

	it = of_ioapic_type + type;
	*out_type = it->out_type;

	set_io_apic_irq_attr(&attr, idx, line, it->trigger, it->polarity);

	return io_apic_setup_irq_pin_once(*out_hwirq, cpu_to_node(0), &attr);
}

static void __init ioapic_add_ofnode(struct device_node *np)
{
	struct resource r;
	int i, ret;

	ret = of_address_to_resource(np, 0, &r);
	if (ret) {
		printk(KERN_ERR "Failed to obtain address for %s\n",
				np->full_name);
		return;
	}

	for (i = 0; i < nr_ioapics; i++) {
		if (r.start == mpc_ioapic_addr(i)) {
			struct irq_domain *id;

			id = kzalloc(sizeof(*id), GFP_KERNEL);
			BUG_ON(!id);
			id->controller = np;
			id->xlate = ioapic_xlate;
			id->priv = (void *)i;
			add_interrupt_host(id);
			return;
		}
	}
	printk(KERN_ERR "IOxAPIC at %s is not registered.\n", np->full_name);
}

void __init x86_add_irq_domains(void)
{
	struct device_node *dp;

	if (!of_have_populated_dt())
		return;

	for_each_node_with_property(dp, "interrupt-controller") {
		if (of_device_is_compatible(dp, "intel,ce4100-ioapic"))
			ioapic_add_ofnode(dp);
	}
}
#else
void __init x86_add_irq_domains(void) { }
#endif
