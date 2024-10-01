// SPDX-License-Identifier: GPL-2.0
/*
 * setup.c - boot time setup code
 */

#include <linux/init.h>
#include <linux/export.h>

#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <linux/ioport.h>

#include <asm/mach-rc32434/rb.h>
#include <asm/mach-rc32434/pci.h>

struct pci_reg __iomem *pci_reg;
EXPORT_SYMBOL(pci_reg);

static struct resource pci0_res[] = {
	{
		.name = "pci_reg0",
		.start = PCI0_BASE_ADDR,
		.end = PCI0_BASE_ADDR + sizeof(struct pci_reg),
		.flags = IORESOURCE_MEM,
	}
};

static void rb_machine_restart(char *command)
{
	/* just jump to the reset vector */
	writel(0x80000001, IDT434_REG_BASE + RST);
	((void (*)(void)) KSEG1ADDR(0x1FC00000u))();
}

static void rb_machine_halt(void)
{
	for (;;)
		continue;
}

void __init plat_mem_setup(void)
{
	u32 val;

	_machine_restart = rb_machine_restart;
	_machine_halt = rb_machine_halt;
	pm_power_off = rb_machine_halt;

	set_io_port_base(KSEG1);

	pci_reg = ioremap(pci0_res[0].start,
				pci0_res[0].end - pci0_res[0].start);
	if (!pci_reg) {
		printk(KERN_ERR "Could not remap PCI registers\n");
		return;
	}

	val = __raw_readl(&pci_reg->pcic);
	val &= 0xFFFFFF7;
	__raw_writel(val, (void *)&pci_reg->pcic);

#ifdef CONFIG_PCI
	/* Enable PCI interrupts in EPLD Mask register */
	*epld_mask = 0x0;
	*(epld_mask + 1) = 0x0;
#endif
	write_c0_wired(0);
}

const char *get_system_type(void)
{
	switch (mips_machtype) {
	case MACH_MIKROTIK_RB532A:
		return "Mikrotik RB532A";
		break;
	default:
		return "Mikrotik RB532";
		break;
	}
}
