/*
 * Efika 5K2 platform code
 * Some code really inspired from the lite5200b platform.
 *
 * Copyright (C) 2006 bplan GmbH
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/init.h>
#include <generated/utsrelease.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/mpc52xx.h>

#define EFIKA_PLATFORM_NAME "Efika"


/* ------------------------------------------------------------------------ */
/* PCI accesses thru RTAS                                                   */
/* ------------------------------------------------------------------------ */

#ifdef CONFIG_PCI

/*
 * Access functions for PCI config space using RTAS calls.
 */
static int rtas_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
			    int len, u32 * val)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	unsigned long addr = (offset & 0xff) | ((devfn & 0xff) << 8)
	    | (((bus->number - hose->first_busno) & 0xff) << 16)
	    | (hose->global_number << 24);
	int ret = -1;
	int rval;

	rval = rtas_call(rtas_token("read-pci-config"), 2, 2, &ret, addr, len);
	*val = ret;
	return rval ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int rtas_write_config(struct pci_bus *bus, unsigned int devfn,
			     int offset, int len, u32 val)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	unsigned long addr = (offset & 0xff) | ((devfn & 0xff) << 8)
	    | (((bus->number - hose->first_busno) & 0xff) << 16)
	    | (hose->global_number << 24);
	int rval;

	rval = rtas_call(rtas_token("write-pci-config"), 3, 1, NULL,
			 addr, len, val);
	return rval ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static struct pci_ops rtas_pci_ops = {
	.read = rtas_read_config,
	.write = rtas_write_config,
};


static void __init efika_pcisetup(void)
{
	const int *bus_range;
	int len;
	struct pci_controller *hose;
	struct device_node *root;
	struct device_node *pcictrl;

	root = of_find_node_by_path("/");
	if (root == NULL) {
		printk(KERN_WARNING EFIKA_PLATFORM_NAME
		       ": Unable to find the root node\n");
		return;
	}

	for (pcictrl = NULL;;) {
		pcictrl = of_get_next_child(root, pcictrl);
		if ((pcictrl == NULL) || (strcmp(pcictrl->name, "pci") == 0))
			break;
	}

	of_node_put(root);

	if (pcictrl == NULL) {
		printk(KERN_WARNING EFIKA_PLATFORM_NAME
		       ": Unable to find the PCI bridge node\n");
		return;
	}

	bus_range = of_get_property(pcictrl, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING EFIKA_PLATFORM_NAME
		       ": Can't get bus-range for %s\n", pcictrl->full_name);
		goto out_put;
	}

	if (bus_range[1] == bus_range[0])
		printk(KERN_INFO EFIKA_PLATFORM_NAME ": PCI bus %d",
		       bus_range[0]);
	else
		printk(KERN_INFO EFIKA_PLATFORM_NAME ": PCI buses %d..%d",
		       bus_range[0], bus_range[1]);
	printk(" controlled by %s\n", pcictrl->full_name);
	printk("\n");

	hose = pcibios_alloc_controller(pcictrl);
	if (!hose) {
		printk(KERN_WARNING EFIKA_PLATFORM_NAME
		       ": Can't allocate PCI controller structure for %s\n",
		       pcictrl->full_name);
		goto out_put;
	}

	hose->first_busno = bus_range[0];
	hose->last_busno = bus_range[1];
	hose->ops = &rtas_pci_ops;

	pci_process_bridge_OF_ranges(hose, pcictrl, 0);
	return;
out_put:
	of_node_put(pcictrl);
}

#else
static void __init efika_pcisetup(void)
{}
#endif



/* ------------------------------------------------------------------------ */
/* Platform setup                                                           */
/* ------------------------------------------------------------------------ */

static void efika_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *revision;
	const char *codegendescription;
	const char *codegenvendor;

	root = of_find_node_by_path("/");
	if (!root)
		return;

	revision = of_get_property(root, "revision", NULL);
	codegendescription = of_get_property(root, "CODEGEN,description", NULL);
	codegenvendor = of_get_property(root, "CODEGEN,vendor", NULL);

	if (codegendescription)
		seq_printf(m, "machine\t\t: %s\n", codegendescription);
	else
		seq_printf(m, "machine\t\t: Efika\n");

	if (revision)
		seq_printf(m, "revision\t: %s\n", revision);

	if (codegenvendor)
		seq_printf(m, "vendor\t\t: %s\n", codegenvendor);

	of_node_put(root);
}

#ifdef CONFIG_PM
static void efika_suspend_prepare(void __iomem *mbar)
{
	u8 pin = 4;	/* GPIO_WKUP_4 (GPIO_PSC6_0 - IRDA_RX) */
	u8 level = 1;	/* wakeup on high level */
	/* IOW. to wake it up, short pins 1 and 3 on IRDA connector */
	mpc52xx_set_wakeup_gpio(pin, level);
}
#endif

static void __init efika_setup_arch(void)
{
	rtas_initialize();

	/* Map important registers from the internal memory map */
	mpc52xx_map_common_devices();

	efika_pcisetup();

#ifdef CONFIG_PM
	mpc52xx_suspend.board_suspend_prepare = efika_suspend_prepare;
	mpc52xx_pm_init();
#endif

	if (ppc_md.progress)
		ppc_md.progress("Linux/PPC " UTS_RELEASE " running on Efika ;-)\n", 0x0);
}

static int __init efika_probe(void)
{
	const char *model = of_get_flat_dt_prop(of_get_flat_dt_root(),
						"model", NULL);

	if (model == NULL)
		return 0;
	if (strcmp(model, "EFIKA5K2"))
		return 0;

	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	return 1;
}

define_machine(efika)
{
	.name			= EFIKA_PLATFORM_NAME,
	.probe			= efika_probe,
	.setup_arch		= efika_setup_arch,
	.init			= mpc52xx_declare_of_platform_devices,
	.show_cpuinfo		= efika_show_cpuinfo,
	.init_IRQ		= mpc52xx_init_irq,
	.get_irq		= mpc52xx_get_irq,
	.restart		= rtas_restart,
	.power_off		= rtas_power_off,
	.halt			= rtas_halt,
	.set_rtc_time		= rtas_set_rtc_time,
	.get_rtc_time		= rtas_get_rtc_time,
	.progress		= rtas_progress,
	.get_boot_time		= rtas_get_boot_time,
	.calibrate_decr		= generic_calibrate_decr,
#ifdef CONFIG_PCI
	.phys_mem_access_prot	= pci_phys_mem_access_prot,
#endif
};

