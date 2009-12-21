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
	PORT(0x3f8, 0),
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

static struct resource pcit_cmos_rsrc[] = {
        {
                .start = 0x70,
                .end   = 0x71,
                .flags = IORESOURCE_IO
        },
        {
                .start = 8,
                .end   = 8,
                .flags = IORESOURCE_IRQ
        }
};

static struct platform_device pcit_cmos_device = {
        .name           = "rtc_cmos",
        .num_resources  = ARRAY_SIZE(pcit_cmos_rsrc),
        .resource       = pcit_cmos_rsrc
};

static struct platform_device pcit_pcspeaker_pdev = {
	.name		= "pcspkr",
	.id		= -1,
};

static struct resource sni_io_resource = {
	.start	= 0x00000000UL,
	.end	= 0x03bfffffUL,
	.name	= "PCIT IO",
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
		.start	=  0xcf8,
		.end	= 0xcfb,
		.name	= "PCI config addr",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	=  0xcfc,
		.end	= 0xcff,
		.name	= "PCI config data",
		.flags	= IORESOURCE_BUSY
	}
};

static struct resource sni_mem_resource = {
	.start	= 0x18000000UL,
	.end	= 0x1fbfffffUL,
	.name	= "PCIT PCI MEM",
	.flags	= IORESOURCE_MEM
};

static void __init sni_pcit_resource_init(void)
{
	int i;

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(pcit_io_resources); i++)
		request_resource(&sni_io_resource, pcit_io_resources + i);
}


extern struct pci_ops sni_pcit_ops;

static struct pci_controller sni_pcit_controller = {
	.pci_ops	= &sni_pcit_ops,
	.mem_resource	= &sni_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_resource	= &sni_io_resource,
	.io_offset	= 0x00000000UL,
	.io_map_base    = SNI_PORT_BASE
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
	.name = "PCIT",
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
		do_IRQ(irq + SNI_PCIT_INT_START - 1);
	set_c0_status(IE_IRQ1);
}

static void pcit_hwint0(void)
{
	u32 pending = *(volatile u32 *)SNI_PCIT_INT_REG;
	int irq;

	clear_c0_status(IE_IRQ0);
	irq = ffs((pending >> 16) & 0x3f);

	if (likely(irq > 0))
		do_IRQ(irq + SNI_PCIT_INT_START - 1);
	set_c0_status(IE_IRQ0);
}

static void sni_pcit_hwint(void)
{
	u32 pending = read_c0_cause() & read_c0_status();

	if (pending & C_IRQ1)
		pcit_hwint1();
	else if (pending & C_IRQ2)
		do_IRQ(MIPS_CPU_IRQ_BASE + 4);
	else if (pending & C_IRQ3)
		do_IRQ(MIPS_CPU_IRQ_BASE + 5);
	else if (pending & C_IRQ5)
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
}

static void sni_pcit_hwint_cplus(void)
{
	u32 pending = read_c0_cause() & read_c0_status();

	if (pending & C_IRQ0)
		pcit_hwint0();
	else if (pending & C_IRQ1)
		do_IRQ(MIPS_CPU_IRQ_BASE + 3);
	else if (pending & C_IRQ2)
		do_IRQ(MIPS_CPU_IRQ_BASE + 4);
	else if (pending & C_IRQ3)
		do_IRQ(MIPS_CPU_IRQ_BASE + 5);
	else if (pending & C_IRQ5)
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
}

void __init sni_pcit_irq_init(void)
{
	int i;

	mips_cpu_irq_init();
	for (i = SNI_PCIT_INT_START; i <= SNI_PCIT_INT_END; i++)
		set_irq_chip_and_handler(i, &pcit_irq_type, handle_level_irq);
	*(volatile u32 *)SNI_PCIT_INT_REG = 0;
	sni_hwint = sni_pcit_hwint;
	change_c0_status(ST0_IM, IE_IRQ1);
	setup_irq(SNI_PCIT_INT_START + 6, &sni_isa_irq);
}

void __init sni_pcit_cplus_irq_init(void)
{
	int i;

	mips_cpu_irq_init();
	for (i = SNI_PCIT_INT_START; i <= SNI_PCIT_INT_END; i++)
		set_irq_chip_and_handler(i, &pcit_irq_type, handle_level_irq);
	*(volatile u32 *)SNI_PCIT_INT_REG = 0x40000000;
	sni_hwint = sni_pcit_hwint_cplus;
	change_c0_status(ST0_IM, IE_IRQ0);
	setup_irq(MIPS_CPU_IRQ_BASE + 3, &sni_isa_irq);
}

void __init sni_pcit_init(void)
{
	ioport_resource.end = sni_io_resource.end;
#ifdef CONFIG_PCI
	PCIBIOS_MIN_IO = 0x9000;
	register_pci_controller(&sni_pcit_controller);
#endif
	sni_pcit_resource_init();
}

static int __init snirm_pcit_setup_devinit(void)
{
	switch (sni_brd_type) {
	case SNI_BRD_PCI_TOWER:
	        platform_device_register(&pcit_serial8250_device);
	        platform_device_register(&pcit_cmos_device);
		platform_device_register(&pcit_pcspeaker_pdev);
	        break;

	case SNI_BRD_PCI_TOWER_CPLUS:
	        platform_device_register(&pcit_cplus_serial8250_device);
	        platform_device_register(&pcit_cmos_device);
		platform_device_register(&pcit_pcspeaker_pdev);
	        break;
	}
	return 0;
}

device_initcall(snirm_pcit_setup_devinit);
