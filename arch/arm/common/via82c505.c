#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/system.h>

#include <asm/mach/pci.h>

#define MAX_SLOTS		7

#define CONFIG_CMD(bus, devfn, where)   (0x80000000 | (bus->number << 16) | (devfn << 8) | (where & ~3))

static int
via82c505_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		      int size, u32 *value)
{
	outl(CONFIG_CMD(bus,devfn,where),0xCF8);
	switch (size) {
	case 1:
		*value=inb(0xCFC + (where&3));
		break;
	case 2:
		*value=inw(0xCFC + (where&2));
		break;
	case 4:
		*value=inl(0xCFC);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int
via82c505_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		       int size, u32 value)
{
	outl(CONFIG_CMD(bus,devfn,where),0xCF8);
	switch (size) {
	case 1:
		outb(value, 0xCFC + (where&3));
		break;
	case 2:
		outw(value, 0xCFC + (where&2));
		break;
	case 4:
		outl(value, 0xCFC);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops via82c505_ops = {
	.read	= via82c505_read_config,
	.write	= via82c505_write_config,
};

void __init via82c505_preinit(void)
{
	printk(KERN_DEBUG "PCI: VIA 82c505\n");
	if (!request_region(0xA8,2,"via config")) {
		printk(KERN_WARNING"VIA 82c505: Unable to request region 0xA8\n");
		return;
	}
	if (!request_region(0xCF8,8,"pci config")) {
		printk(KERN_WARNING"VIA 82c505: Unable to request region 0xCF8\n");
		release_region(0xA8, 2);
		return;
	}

	/* Enable compatible Mode */
	outb(0x96,0xA8);
	outb(0x18,0xA9);
	outb(0x93,0xA8);
	outb(0xd0,0xA9);

}

int __init via82c505_setup(int nr, struct pci_sys_data *sys)
{
	return (nr == 0);
}

struct pci_bus * __init via82c505_scan_bus(int nr, struct pci_sys_data *sysdata)
{
	if (nr == 0)
		return pci_scan_bus(0, &via82c505_ops, sysdata);

	return NULL;
}
