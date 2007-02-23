/*
 * PCI Tower specific code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/serial_8250.h>

#include <asm/mc146818-time.h>
#include <asm/sni.h>
#include <asm/time.h>
#include <asm/irq_cpu.h>


#define PORT(_base,_irq)				\
	{						\
		.iobase		= _base,		\
		.irq		= _irq,			\
		.uartclk	= 1843200,		\
		.iotype		= UPIO_PORT,		\
		.flags		= UPF_BOOT_AUTOCONF,	\
	}

static struct plat_serial8250_port pcit_data[] = {
	PORT(0x3f8, 0),
	PORT(0x2f8, 3),
	{ },
};

static struct platform_device pcit_serial8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= pcit_data,
	},
};

static struct plat_serial8250_port pcit_cplus_data[] = {
	PORT(0x3f8, 4),
	PORT(0x2f8, 3),
	PORT(0x3e8, 4),
	PORT(0x2e8, 3),
	{ },
};

static struct platform_device pcit_cplus_serial8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= pcit_cplus_data,
	},
};

static struct resource sni_io_resource = {
	.start	= 0x00001000UL,
	.end	= 0x03bfffffUL,
	.name	= "PCIT IO MEM",
	.flags	= IORESOURCE_IO,
};

static struct resource pcit_io_resources[] = {
	{
		.start	= 0x00,
		.end	= 0x1f,
		.name	= "dma1",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	=  0x40,
		.end	= 0x5f,
		.name	= "timer",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	=  0x60,
		.end	= 0x6f,
		.name	= "keyboard",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	=  0x80,
		.end	= 0x8f,
		.name	= "dma page reg",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	=  0xc0,
		.end	= 0xdf,
		.name	= "dma2",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	=  0xcfc,
		.end	= 0xcff,
		.name	= "PCI config data",
		.flags	= IORESOURCE_BUSY
	}
};

static struct resource sni_mem_resource = {
	.start	= 0x10000000UL,
	.end	= 0xffffffffUL,
	.name	= "PCIT PCI MEM",
	.flags	= IORESOURCE_MEM
};

/*
 * The RM200/RM300 has a few holes in it's PCI/EISA memory address space used
 * for other purposes.  Be paranoid and allocate all of the before the PCI
 * code gets a chance to to map anything else there ...
 *
 * This leaves the following areas available:
 *
 * 0x10000000 - 0x1009ffff (640kB) PCI/EISA/ISA Bus Memory
 * 0x10100000 - 0x13ffffff ( 15MB) PCI/EISA/ISA Bus Memory
 * 0x18000000 - 0x1fbfffff (124MB) PCI/EISA Bus Memory
 * 0x1ff08000 - 0x1ffeffff (816kB) PCI/EISA Bus Memory
 * 0xa0000000 - 0xffffffff (1.5GB) PCI/EISA Bus Memory
 */
static struct resource pcit_mem_resources[] = {
	{
		.start	= 0x14000000,
		.end	= 0x17bfffff,
		.name	= "PCI IO",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x17c00000,
		.end	= 0x17ffffff,
		.name	= "Cache Replacement Area",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x180a0000,
		.end	= 0x180bffff,
		.name	= "Video RAM area",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x180c0000,
		.end	= 0x180fffff,
		.name	= "ISA Reserved",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x19000000,
		.end	= 0x1fbfffff,
		.name	= "PCI MEM",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x1fc00000,
		.end	= 0x1fc7ffff,
		.name	= "Boot PROM",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x1fc80000,
		.end	= 0x1fcfffff,
		.name	= "Diag PROM",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x1fd00000,
		.end	= 0x1fdfffff,
		.name	= "X-Bus",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x1fe00000,
		.end	= 0x1fefffff,
		.name	= "BIOS map",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x1ff00000,
		.end	= 0x1ff7ffff,
		.name	= "NVRAM / EEPROM",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x1fff0000,
		.end	= 0x1fffefff,
		.name	= "MAUI ASIC",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x1ffff000,
		.end	= 0x1fffffff,
		.name	= "MP Agent",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x20000000,
		.end	= 0x9fffffff,
		.name	= "Main Memory",
		.flags	= IORESOURCE_BUSY
	}
};

static void __init sni_pcit_resource_init(void)
{
	int i;

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(pcit_io_resources); i++)
		request_resource(&ioport_resource, pcit_io_resources + i);

	/* request mem space for pcimt-specific devices */
	for (i = 0; i < ARRAY_SIZE(pcit_mem_resources); i++)
		request_resource(&sni_mem_resource, pcit_mem_resources + i);

	ioport_resource.end = sni_io_resource.end;
}


extern struct pci_ops sni_pcit_ops;

static struct pci_controller sni_pcit_controller = {
	.pci_ops	= &sni_pcit_ops,
	.mem_resource	= &sni_mem_resource,
	.mem_offset	= 0x10000000UL,
	.io_resource	= &sni_io_resource,
	.io_offset	= 0x00000000UL
};

static void enable_pcit_irq(unsigned int irq)
{
	u32 mask = 1 << (irq - SNI_PCIT_INT_START + 24);

	*(volatile u32 *)SNI_PCIT_INT_REG |= mask;
}

void disable_pcit_irq(unsigned int irq)
{
	u32 mask = 1 << (irq - SNI_PCIT_INT_START + 24);

	*(volatile u32 *)SNI_PCIT_INT_REG &= ~mask;
}

void end_pcit_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_pcit_irq(irq);
}

static struct irq_chip pcit_irq_type = {
	.typename = "PCIT",
	.ack = disable_pcit_irq,
	.mask = disable_pcit_irq,
	.mask_ack = disable_pcit_irq,
	.unmask = enable_pcit_irq,
	.end = end_pcit_irq,
};

static void pcit_hwint1(void)
{
	u32 pending = *(volatile u32 *)SNI_PCIT_INT_REG;
	int irq;

	clear_c0_status(IE_IRQ1);
	irq = ffs((pending >> 16) & 0x7f);

	if (likely(irq > 0))
		do_IRQ (irq + SNI_PCIT_INT_START - 1);
	set_c0_status (IE_IRQ1);
}

static void pcit_hwint0(void)
{
	u32 pending = *(volatile u32 *)SNI_PCIT_INT_REG;
	int irq;

	clear_c0_status(IE_IRQ0);
	irq = ffs((pending >> 16) & 0x7f);

	if (likely(irq > 0))
		do_IRQ (irq + SNI_PCIT_INT_START - 1);
	set_c0_status (IE_IRQ0);
}

static void sni_pcit_hwint(void)
{
	u32 pending = (read_c0_cause() & read_c0_status());

	if (pending & C_IRQ1)
		pcit_hwint1();
	else if (pending & C_IRQ2)
		do_IRQ (MIPS_CPU_IRQ_BASE + 4);
	else if (pending & C_IRQ3)
		do_IRQ (MIPS_CPU_IRQ_BASE + 5);
	else if (pending & C_IRQ5)
		do_IRQ (MIPS_CPU_IRQ_BASE + 7);
}

static void sni_pcit_hwint_cplus(void)
{
	u32 pending = (read_c0_cause() & read_c0_status());

	if (pending & C_IRQ0)
		pcit_hwint0();
	else if (pending & C_IRQ2)
		do_IRQ (MIPS_CPU_IRQ_BASE + 4);
	else if (pending & C_IRQ3)
		do_IRQ (MIPS_CPU_IRQ_BASE + 5);
	else if (pending & C_IRQ5)
		do_IRQ (MIPS_CPU_IRQ_BASE + 7);
}

void __init sni_pcit_irq_init(void)
{
	int i;

	mips_cpu_irq_init();
	for (i = SNI_PCIT_INT_START; i <= SNI_PCIT_INT_END; i++)
		set_irq_chip(i, &pcit_irq_type);
	*(volatile u32 *)SNI_PCIT_INT_REG = 0;
	sni_hwint = sni_pcit_hwint;
	change_c0_status(ST0_IM, IE_IRQ1);
	setup_irq (SNI_PCIT_INT_START + 6, &sni_isa_irq);
}

void __init sni_pcit_cplus_irq_init(void)
{
	int i;

	mips_cpu_irq_init();
	for (i = SNI_PCIT_INT_START; i <= SNI_PCIT_INT_END; i++)
		set_irq_chip(i, &pcit_irq_type);
	*(volatile u32 *)SNI_PCIT_INT_REG = 0;
	sni_hwint = sni_pcit_hwint_cplus;
	change_c0_status(ST0_IM, IE_IRQ0);
	setup_irq (SNI_PCIT_INT_START + 6, &sni_isa_irq);
}

void sni_pcit_init(void)
{
	sni_pcit_resource_init();
	rtc_mips_get_time = mc146818_get_cmos_time;
	rtc_mips_set_time = mc146818_set_rtc_mmss;
	board_time_init = sni_cpu_time_init;
#ifdef CONFIG_PCI
	register_pci_controller(&sni_pcit_controller);
#endif
}

static int __init snirm_pcit_setup_devinit(void)
{
	switch (sni_brd_type) {
	case SNI_BRD_PCI_TOWER:
	        platform_device_register(&pcit_serial8250_device);
	        break;

	case SNI_BRD_PCI_TOWER_CPLUS:
	        platform_device_register(&pcit_cplus_serial8250_device);
	        break;
	}
	return 0;
}

device_initcall(snirm_pcit_setup_devinit);
