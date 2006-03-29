#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/mmu.h>
#include <asm/prom.h>
#include <asm/serial.h>
#include <asm/udbg.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt...) do { printk(fmt); } while(0)
#else
#define DBG(fmt...) do { } while(0)
#endif

#define MAX_LEGACY_SERIAL_PORTS	8

static struct plat_serial8250_port
legacy_serial_ports[MAX_LEGACY_SERIAL_PORTS+1];
static struct legacy_serial_info {
	struct device_node		*np;
	unsigned int			speed;
	unsigned int			clock;
	phys_addr_t			taddr;
} legacy_serial_infos[MAX_LEGACY_SERIAL_PORTS];
static unsigned int legacy_serial_count;
static int legacy_serial_console = -1;

static int __init add_legacy_port(struct device_node *np, int want_index,
				  int iotype, phys_addr_t base,
				  phys_addr_t taddr, unsigned long irq,
				  upf_t flags)
{
	u32 *clk, *spd, clock = BASE_BAUD * 16;
	int index;

	/* get clock freq. if present */
	clk = (u32 *)get_property(np, "clock-frequency", NULL);
	if (clk && *clk)
		clock = *clk;

	/* get default speed if present */
	spd = (u32 *)get_property(np, "current-speed", NULL);

	/* If we have a location index, then try to use it */
	if (want_index >= 0 && want_index < MAX_LEGACY_SERIAL_PORTS)
		index = want_index;
	else
		index = legacy_serial_count;

	/* if our index is still out of range, that mean that
	 * array is full, we could scan for a free slot but that
	 * make little sense to bother, just skip the port
	 */
	if (index >= MAX_LEGACY_SERIAL_PORTS)
		return -1;
	if (index >= legacy_serial_count)
		legacy_serial_count = index + 1;

	/* Check if there is a port who already claimed our slot */
	if (legacy_serial_infos[index].np != 0) {
		/* if we still have some room, move it, else override */
		if (legacy_serial_count < MAX_LEGACY_SERIAL_PORTS) {
			printk(KERN_INFO "Moved legacy port %d -> %d\n",
			       index, legacy_serial_count);
			legacy_serial_ports[legacy_serial_count] =
				legacy_serial_ports[index];
			legacy_serial_infos[legacy_serial_count] =
				legacy_serial_infos[index];
			legacy_serial_count++;
		} else {
			printk(KERN_INFO "Replacing legacy port %d\n", index);
		}
	}

	/* Now fill the entry */
	memset(&legacy_serial_ports[index], 0,
	       sizeof(struct plat_serial8250_port));
	if (iotype == UPIO_PORT)
		legacy_serial_ports[index].iobase = base;
	else
		legacy_serial_ports[index].mapbase = base;
	legacy_serial_ports[index].iotype = iotype;
	legacy_serial_ports[index].uartclk = clock;
	legacy_serial_ports[index].irq = irq;
	legacy_serial_ports[index].flags = flags;
	legacy_serial_infos[index].taddr = taddr;
	legacy_serial_infos[index].np = of_node_get(np);
	legacy_serial_infos[index].clock = clock;
	legacy_serial_infos[index].speed = spd ? *spd : 0;

	printk(KERN_INFO "Found legacy serial port %d for %s\n",
	       index, np->full_name);
	printk(KERN_INFO "  %s=%llx, taddr=%llx, irq=%lx, clk=%d, speed=%d\n",
	       (iotype == UPIO_PORT) ? "port" : "mem",
	       (unsigned long long)base, (unsigned long long)taddr, irq,
	       legacy_serial_ports[index].uartclk,
	       legacy_serial_infos[index].speed);

	return index;
}

static int __init add_legacy_soc_port(struct device_node *np,
				      struct device_node *soc_dev)
{
	phys_addr_t addr;
	u32 *addrp;
	upf_t flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_SHARE_IRQ;

	/* We only support ports that have a clock frequency properly
	 * encoded in the device-tree.
	 */
	if (get_property(np, "clock-frequency", NULL) == NULL)
		return -1;

	/* Get the address */
	addrp = of_get_address(soc_dev, 0, NULL, NULL);
	if (addrp == NULL)
		return -1;

	addr = of_translate_address(soc_dev, addrp);

	/* Add port, irq will be dealt with later. We passed a translated
	 * IO port value. It will be fixed up later along with the irq
	 */
	return add_legacy_port(np, -1, UPIO_MEM, addr, addr, NO_IRQ, flags);
}

static int __init add_legacy_isa_port(struct device_node *np,
				      struct device_node *isa_brg)
{
	u32 *reg;
	char *typep;
	int index = -1;
	phys_addr_t taddr;

	/* Get the ISA port number */
	reg = (u32 *)get_property(np, "reg", NULL);
	if (reg == NULL)
		return -1;

	/* Verify it's an IO port, we don't support anything else */
	if (!(reg[0] & 0x00000001))
		return -1;

	/* Now look for an "ibm,aix-loc" property that gives us ordering
	 * if any...
	 */
	typep = (char *)get_property(np, "ibm,aix-loc", NULL);

	/* If we have a location index, then use it */
	if (typep && *typep == 'S')
		index = simple_strtol(typep+1, NULL, 0) - 1;

	/* Translate ISA address */
	taddr = of_translate_address(np, reg);

	/* Add port, irq will be dealt with later */
	return add_legacy_port(np, index, UPIO_PORT, reg[1], taddr, NO_IRQ, UPF_BOOT_AUTOCONF);

}

#ifdef CONFIG_PCI
static int __init add_legacy_pci_port(struct device_node *np,
				      struct device_node *pci_dev)
{
	phys_addr_t addr, base;
	u32 *addrp;
	unsigned int flags;
	int iotype, index = -1, lindex = 0;

	/* We only support ports that have a clock frequency properly
	 * encoded in the device-tree (that is have an fcode). Anything
	 * else can't be used that early and will be normally probed by
	 * the generic 8250_pci driver later on. The reason is that 8250
	 * compatible UARTs on PCI need all sort of quirks (port offsets
	 * etc...) that this code doesn't know about
	 */
	if (get_property(np, "clock-frequency", NULL) == NULL)
		return -1;

	/* Get the PCI address. Assume BAR 0 */
	addrp = of_get_pci_address(pci_dev, 0, NULL, &flags);
	if (addrp == NULL)
		return -1;

	/* We only support BAR 0 for now */
	iotype = (flags & IORESOURCE_MEM) ? UPIO_MEM : UPIO_PORT;
	addr = of_translate_address(pci_dev, addrp);

	/* Set the IO base to the same as the translated address for MMIO,
	 * or to the domain local IO base for PIO (it will be fixed up later)
	 */
	if (iotype == UPIO_MEM)
		base = addr;
	else
		base = addrp[2];

	/* Try to guess an index... If we have subdevices of the pci dev,
	 * we get to their "reg" property
	 */
	if (np != pci_dev) {
		u32 *reg = (u32 *)get_property(np, "reg", NULL);
		if (reg && (*reg < 4))
			index = lindex = *reg;
	}

	/* Local index means it's the Nth port in the PCI chip. Unfortunately
	 * the offset to add here is device specific. We know about those
	 * EXAR ports and we default to the most common case. If your UART
	 * doesn't work for these settings, you'll have to add your own special
	 * cases here
	 */
	if (device_is_compatible(pci_dev, "pci13a8,152") ||
	    device_is_compatible(pci_dev, "pci13a8,154") ||
	    device_is_compatible(pci_dev, "pci13a8,158")) {
		addr += 0x200 * lindex;
		base += 0x200 * lindex;
	} else {
		addr += 8 * lindex;
		base += 8 * lindex;
	}

	/* Add port, irq will be dealt with later. We passed a translated
	 * IO port value. It will be fixed up later along with the irq
	 */
	return add_legacy_port(np, index, iotype, base, addr, NO_IRQ, UPF_BOOT_AUTOCONF);
}
#endif

static void __init setup_legacy_serial_console(int console)
{
	struct legacy_serial_info *info =
		&legacy_serial_infos[console];
	void __iomem *addr;

	if (info->taddr == 0)
		return;
	addr = ioremap(info->taddr, 0x1000);
	if (addr == NULL)
		return;
	if (info->speed == 0)
		info->speed = udbg_probe_uart_speed(addr, info->clock);
	DBG("default console speed = %d\n", info->speed);
	udbg_init_uart(addr, info->speed, info->clock);
}

/*
 * This is called very early, as part of setup_system() or eventually
 * setup_arch(), basically before anything else in this file. This function
 * will try to build a list of all the available 8250-compatible serial ports
 * in the machine using the Open Firmware device-tree. It currently only deals
 * with ISA and PCI busses but could be extended. It allows a very early boot
 * console to be initialized, that list is also used later to provide 8250 with
 * the machine non-PCI ports and to properly pick the default console port
 */
void __init find_legacy_serial_ports(void)
{
	struct device_node *np, *stdout = NULL;
	char *path;
	int index;

	DBG(" -> find_legacy_serial_port()\n");

	/* Now find out if one of these is out firmware console */
	path = (char *)get_property(of_chosen, "linux,stdout-path", NULL);
	if (path != NULL) {
		stdout = of_find_node_by_path(path);
		if (stdout)
			DBG("stdout is %s\n", stdout->full_name);
	} else {
		DBG(" no linux,stdout-path !\n");
	}

	/* First fill our array with SOC ports */
	for (np = NULL; (np = of_find_compatible_node(np, "serial", "ns16550")) != NULL;) {
		struct device_node *soc = of_get_parent(np);
		if (soc && !strcmp(soc->type, "soc")) {
			index = add_legacy_soc_port(np, np);
			if (index >= 0 && np == stdout)
				legacy_serial_console = index;
		}
		of_node_put(soc);
	}

	/* First fill our array with ISA ports */
	for (np = NULL; (np = of_find_node_by_type(np, "serial"));) {
		struct device_node *isa = of_get_parent(np);
		if (isa && !strcmp(isa->name, "isa")) {
			index = add_legacy_isa_port(np, isa);
			if (index >= 0 && np == stdout)
				legacy_serial_console = index;
		}
		of_node_put(isa);
	}

#ifdef CONFIG_PCI
	/* Next, try to locate PCI ports */
	for (np = NULL; (np = of_find_all_nodes(np));) {
		struct device_node *pci, *parent = of_get_parent(np);
		if (parent && !strcmp(parent->name, "isa")) {
			of_node_put(parent);
			continue;
		}
		if (strcmp(np->name, "serial") && strcmp(np->type, "serial")) {
			of_node_put(parent);
			continue;
		}
		/* Check for known pciclass, and also check wether we have
		 * a device with child nodes for ports or not
		 */
		if (device_is_compatible(np, "pciclass,0700") ||
		    device_is_compatible(np, "pciclass,070002"))
			pci = np;
		else if (device_is_compatible(parent, "pciclass,0700") ||
			 device_is_compatible(parent, "pciclass,070002"))
			pci = parent;
		else {
			of_node_put(parent);
			continue;
		}
		index = add_legacy_pci_port(np, pci);
		if (index >= 0 && np == stdout)
			legacy_serial_console = index;
		of_node_put(parent);
	}
#endif

	DBG("legacy_serial_console = %d\n", legacy_serial_console);
	if (legacy_serial_console >= 0)
		setup_legacy_serial_console(legacy_serial_console);
	DBG(" <- find_legacy_serial_port()\n");
}

static struct platform_device serial_device = {
	.name	= "serial8250",
	.id	= PLAT8250_DEV_PLATFORM,
	.dev	= {
		.platform_data = legacy_serial_ports,
	},
};

static void __init fixup_port_irq(int index,
				  struct device_node *np,
				  struct plat_serial8250_port *port)
{
	DBG("fixup_port_irq(%d)\n", index);

	/* Check for interrupts in that node */
	if (np->n_intrs > 0) {
		port->irq = np->intrs[0].line;
		DBG(" port %d (%s), irq=%d\n",
		    index, np->full_name, port->irq);
		return;
	}

	/* Check for interrupts in the parent */
	np = of_get_parent(np);
	if (np == NULL)
		return;

	if (np->n_intrs > 0) {
		port->irq = np->intrs[0].line;
		DBG(" port %d (%s), irq=%d\n",
		    index, np->full_name, port->irq);
	}
	of_node_put(np);
}

static void __init fixup_port_pio(int index,
				  struct device_node *np,
				  struct plat_serial8250_port *port)
{
#ifdef CONFIG_PCI
	struct pci_controller *hose;

	DBG("fixup_port_pio(%d)\n", index);

	hose = pci_find_hose_for_OF_device(np);
	if (hose) {
		unsigned long offset = (unsigned long)hose->io_base_virt -
#ifdef CONFIG_PPC64
			pci_io_base;
#else
			isa_io_base;
#endif
		DBG("port %d, IO %lx -> %lx\n",
		    index, port->iobase, port->iobase + offset);
		port->iobase += offset;
	}
#endif
}

static void __init fixup_port_mmio(int index,
				   struct device_node *np,
				   struct plat_serial8250_port *port)
{
	DBG("fixup_port_mmio(%d)\n", index);

	port->membase = ioremap(port->mapbase, 0x100);
}

/*
 * This is called as an arch initcall, hopefully before the PCI bus is
 * probed and/or the 8250 driver loaded since we need to register our
 * platform devices before 8250 PCI ones are detected as some of them
 * must properly "override" the platform ones.
 *
 * This function fixes up the interrupt value for platform ports as it
 * couldn't be done earlier before interrupt maps have been parsed. It
 * also "corrects" the IO address for PIO ports for the same reason,
 * since earlier, the PHBs virtual IO space wasn't assigned yet. It then
 * registers all those platform ports for use by the 8250 driver when it
 * finally loads.
 */
static int __init serial_dev_init(void)
{
	int i;

	if (legacy_serial_count == 0)
		return -ENODEV;

	/*
	 * Before we register the platfrom serial devices, we need
	 * to fixup their interrutps and their IO ports.
	 */
	DBG("Fixing serial ports interrupts and IO ports ...\n");

	for (i = 0; i < legacy_serial_count; i++) {
		struct plat_serial8250_port *port = &legacy_serial_ports[i];
		struct device_node *np = legacy_serial_infos[i].np;

		if (port->irq == NO_IRQ)
			fixup_port_irq(i, np, port);
		if (port->iotype == UPIO_PORT)
			fixup_port_pio(i, np, port);
		if (port->iotype == UPIO_MEM)
			fixup_port_mmio(i, np, port);
	}

	DBG("Registering platform serial ports\n");

	return platform_device_register(&serial_device);
}
arch_initcall(serial_dev_init);


/*
 * This is called very early, as part of console_init() (typically just after
 * time_init()). This function is respondible for trying to find a good
 * default console on serial ports. It tries to match the open firmware
 * default output with one of the available serial console drivers, either
 * one of the platform serial ports that have been probed earlier by
 * find_legacy_serial_ports() or some more platform specific ones.
 */
static int __init check_legacy_serial_console(void)
{
	struct device_node *prom_stdout = NULL;
	int speed = 0, offset = 0;
	char *name;
	u32 *spd;

	DBG(" -> check_legacy_serial_console()\n");

	/* The user has requested a console so this is already set up. */
	if (strstr(saved_command_line, "console=")) {
		DBG(" console was specified !\n");
		return -EBUSY;
	}

	if (!of_chosen) {
		DBG(" of_chosen is NULL !\n");
		return -ENODEV;
	}

	if (legacy_serial_console < 0) {
		DBG(" legacy_serial_console not found !\n");
		return -ENODEV;
	}
	/* We are getting a weird phandle from OF ... */
	/* ... So use the full path instead */
	name = (char *)get_property(of_chosen, "linux,stdout-path", NULL);
	if (name == NULL) {
		DBG(" no linux,stdout-path !\n");
		return -ENODEV;
	}
	prom_stdout = of_find_node_by_path(name);
	if (!prom_stdout) {
		DBG(" can't find stdout package %s !\n", name);
		return -ENODEV;
	}
	DBG("stdout is %s\n", prom_stdout->full_name);

	name = (char *)get_property(prom_stdout, "name", NULL);
	if (!name) {
		DBG(" stdout package has no name !\n");
		goto not_found;
	}
	spd = (u32 *)get_property(prom_stdout, "current-speed", NULL);
	if (spd)
		speed = *spd;

	if (0)
		;
#ifdef CONFIG_SERIAL_8250_CONSOLE
	else if (strcmp(name, "serial") == 0) {
		int i;
		/* Look for it in probed array */
		for (i = 0; i < legacy_serial_count; i++) {
			if (prom_stdout != legacy_serial_infos[i].np)
				continue;
			offset = i;
			speed = legacy_serial_infos[i].speed;
			break;
		}
		if (i >= legacy_serial_count)
			goto not_found;
	}
#endif /* CONFIG_SERIAL_8250_CONSOLE */
#ifdef CONFIG_SERIAL_PMACZILOG_CONSOLE
	else if (strcmp(name, "ch-a") == 0)
		offset = 0;
	else if (strcmp(name, "ch-b") == 0)
		offset = 1;
#endif /* CONFIG_SERIAL_PMACZILOG_CONSOLE */
	else
		goto not_found;
	of_node_put(prom_stdout);

	DBG("Found serial console at ttyS%d\n", offset);

	if (speed) {
		static char __initdata opt[16];
		sprintf(opt, "%d", speed);
		return add_preferred_console("ttyS", offset, opt);
	} else
		return add_preferred_console("ttyS", offset, NULL);

 not_found:
	DBG("No preferred console found !\n");
	of_node_put(prom_stdout);
	return -ENODEV;
}
console_initcall(check_legacy_serial_console);

