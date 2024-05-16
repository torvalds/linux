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
#include <asm/io_apic.h>
#include <asm/pci_x86.h>
#include <asm/setup.h>
#include <asm/i8259.h>
#include <asm/prom.h>

__initdata u64 initial_dtb;
char __initdata cmd_line[COMMAND_LINE_SIZE];

int __initdata of_ioapic;

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
	u32 apic_id;

	for_each_of_cpu_node(dn) {
		apic_id = of_get_cpu_hwid(dn, 0);
		if (apic_id == ~0U) {
			pr_warn("%pOF: missing local APIC ID\n", dn);
			continue;
		}
		topology_register_apic(apic_id, CPU_ACPIID_INVALID, true);
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
		/* Try force enabling, which registers the APIC address */
		if (!apic_force_enable(lapic_addr))
			return;
	} else {
		register_lapic_address(lapic_addr);
	}
	smp_found_config = 1;
	pic_mode = !of_property_read_bool(dn, "intel,virtual-wire-mode");
	pr_info("%s compatibility mode.\n", pic_mode ? "IMCR and PIC" : "Virtual Wire");
}

#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_X86_IO_APIC
static unsigned int ioapic_id;

struct of_ioapic_type {
	u32 out_type;
	u32 is_level;
	u32 active_low;
};

static struct of_ioapic_type of_ioapic_type[] =
{
	{
		.out_type	= IRQ_TYPE_EDGE_FALLING,
		.is_level	= 0,
		.active_low	= 1,
	},
	{
		.out_type	= IRQ_TYPE_LEVEL_HIGH,
		.is_level	= 1,
		.active_low	= 0,
	},
	{
		.out_type	= IRQ_TYPE_LEVEL_LOW,
		.is_level	= 1,
		.active_low	= 1,
	},
	{
		.out_type	= IRQ_TYPE_EDGE_RISING,
		.is_level	= 0,
		.active_low	= 0,
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
	ioapic_set_alloc_attr(&tmp, NUMA_NO_NODE, it->is_level, it->active_low);
	tmp.devid = mpc_ioapic_id(mp_irqdomain_ioapic_idx(domain));
	tmp.ioapic.pin = fwspec->param[0];

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
		pr_err("Can't obtain address from device node %pOF.\n", dn);
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
	pr_err("Error: No information about IO-APIC in OF.\n");
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
void __init x86_flattree_get_config(void)
{
	u32 size, map_len;
	void *dt;

	if (initial_dtb) {
		map_len = max(PAGE_SIZE - (initial_dtb & ~PAGE_MASK), (u64)128);

		dt = early_memremap(initial_dtb, map_len);
		size = fdt_totalsize(dt);
		if (map_len < size) {
			early_memunmap(dt, map_len);
			dt = early_memremap(initial_dtb, size);
			map_len = size;
		}

		early_init_dt_verify(dt);
	}

	unflatten_and_copy_device_tree();

	if (initial_dtb)
		early_memunmap(dt, map_len);
}
#endif

void __init x86_dtb_parse_smp_config(void)
{
	if (!of_have_populated_dt())
		return;

	dtb_setup_hpet();
	dtb_apic_setup();
}
