// SPDX-License-Identifier: GPL-2.0
/*
 * Architecture specific OF callbacks.
 */
#include <linux/export.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/libfdt.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/of_pci.h>
#include <linux/initrd.h>

#include <asm/irqdomain.h>
#include <asm/hpet.h>
#include <asm/apic.h>
#include <asm/pci_x86.h>
#include <asm/setup.h>
#include <asm/i8259.h>

__initdata u64 initial_dtb;
char __initdata cmd_line[COMMAND_LINE_SIZE];

int __initdata of_ioapic;

void __init early_init_dt_scan_chosen_arch(unsigned long node)
{
	BUG();
}

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	BUG();
}

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
device_initcall(add_bus_probe);

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
	u32 virq;
	int ret;
	u8 pin;

	ret = pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	if (ret)
		return ret;
	if (!pin)
		return 0;

	virq = of_irq_parse_and_map_pci(dev, 0, 0);
	if (virq == 0)
		return -EINVAL;
	dev->irq = virq;
	return 0;
}

static void x86_of_pci_irq_disable(struct pci_dev *dev)
{
}

void x86_of_pci_init(void)
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

#ifdef CONFIG_X86_LOCAL_APIC

static void __init dtb_cpu_setup(void)
{
	struct device_node *dn;
	u32 apic_id, version;
	int ret;

	version = GET_APIC_VERSION(apic_read(APIC_LVR));
	for_each_of_cpu_node(dn) {
		ret = of_property_read_u32(dn, "reg", &apic_id);
		if (ret < 0) {
			pr_warn("%pOF: missing local APIC ID\n", dn);
			continue;
		}
		generic_processor_info(apic_id, version);
	}
}

static void __init dtb_lapic_setup(void)
{
	struct device_node *dn;
	struct resource r;
	unsigned long lapic_addr = APIC_DEFAULT_PHYS_BASE;
	int ret;

	dn = of_find_compatible_node(NULL, NULL, "intel,ce4100-lapic");
	if (dn) {
		ret = of_address_to_resource(dn, 0, &r);
		if (WARN_ON(ret))
			return;
		lapic_addr = r.start;
	}

	/* Did the boot loader setup the local APIC ? */
	if (!boot_cpu_has(X86_FEATURE_APIC)) {
		if (apic_force_enable(lapic_addr))
			return;
	}
	smp_found_config = 1;
	pic_mode = 1;
	register_lapic_address(lapic_addr);
}

#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_X86_IO_APIC
static unsigned int ioapic_id;

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

static int dt_irqdomain_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs, void *arg)
{
	struct irq_fwspec *fwspec = (struct irq_fwspec *)arg;
	struct of_ioapic_type *it;
	struct irq_alloc_info tmp;
	int type_index;

	if (WARN_ON(fwspec->param_count < 2))
		return -EINVAL;

	type_index = fwspec->param[1];
	if (type_index >= ARRAY_SIZE(of_ioapic_type))
		return -EINVAL;

	it = &of_ioapic_type[type_index];
	ioapic_set_alloc_attr(&tmp, NUMA_NO_NODE, it->trigger, it->polarity);
	tmp.ioapic_id = mpc_ioapic_id(mp_irqdomain_ioapic_idx(domain));
	tmp.ioapic_pin = fwspec->param[0];

	return mp_irqdomain_alloc(domain, virq, nr_irqs, &tmp);
}

static const struct irq_domain_ops ioapic_irq_domain_ops = {
	.alloc		= dt_irqdomain_alloc,
	.free		= mp_irqdomain_free,
	.activate	= mp_irqdomain_activate,
	.deactivate	= mp_irqdomain_deactivate,
};

static void __init dtb_add_ioapic(struct device_node *dn)
{
	struct resource r;
	int ret;
	struct ioapic_domain_cfg cfg = {
		.type = IOAPIC_DOMAIN_DYNAMIC,
		.ops = &ioapic_irq_domain_ops,
		.dev = dn,
	};

	ret = of_address_to_resource(dn, 0, &r);
	if (ret) {
		printk(KERN_ERR "Can't obtain address from device node %pOF.\n", dn);
		return;
	}
	mp_register_ioapic(++ioapic_id, r.start, gsi_top, &cfg);
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
#ifdef CONFIG_X86_LOCAL_APIC
	dtb_lapic_setup();
	dtb_cpu_setup();
#endif
	dtb_ioapic_setup();
}

#ifdef CONFIG_OF_EARLY_FLATTREE
static void __init x86_flattree_get_config(void)
{
	u32 size, map_len;
	void *dt;

	if (!initial_dtb)
		return;

	map_len = max(PAGE_SIZE - (initial_dtb & ~PAGE_MASK), (u64)128);

	dt = early_memremap(initial_dtb, map_len);
	size = fdt_totalsize(dt);
	if (map_len < size) {
		early_memunmap(dt, map_len);
		dt = early_memremap(initial_dtb, size);
		map_len = size;
	}

	early_init_dt_verify(dt);
	unflatten_and_copy_device_tree();
	early_memunmap(dt, map_len);
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
