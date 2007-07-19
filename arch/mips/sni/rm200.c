/*
 * RM200 specific code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
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

static struct plat_serial8250_port rm200_data[] = {
	PORT(0x3f8, 4),
	PORT(0x2f8, 3),
	{ },
};

static struct platform_device rm200_serial8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= rm200_data,
	},
};

static struct resource rm200_ds1216_rsrc[] = {
        {
                .start = 0x1cd41ffc,
                .end   = 0x1cd41fff,
                .flags = IORESOURCE_MEM
        }
};

static struct platform_device rm200_ds1216_device = {
        .name           = "rtc-ds1216",
        .num_resources  = ARRAY_SIZE(rm200_ds1216_rsrc),
        .resource       = rm200_ds1216_rsrc
};

static struct resource snirm_82596_rm200_rsrc[] = {
	{
		.start = 0x18000000,
		.end   = 0x180fffff,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 0x1b000000,
		.end   = 0x1b000004,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 0x1ff00000,
		.end   = 0x1ff00020,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 27,
		.end   = 27,
		.flags = IORESOURCE_IRQ
	},
	{
		.flags = 0x00
	}
};

static struct platform_device snirm_82596_rm200_pdev = {
	.name           = "snirm_82596",
	.num_resources  = ARRAY_SIZE(snirm_82596_rm200_rsrc),
	.resource       = snirm_82596_rm200_rsrc
};

static struct resource snirm_53c710_rm200_rsrc[] = {
	{
		.start = 0x19000000,
		.end   = 0x190fffff,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 26,
		.end   = 26,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device snirm_53c710_rm200_pdev = {
	.name           = "snirm_53c710",
	.num_resources  = ARRAY_SIZE(snirm_53c710_rm200_rsrc),
	.resource       = snirm_53c710_rm200_rsrc
};

static int __init snirm_setup_devinit(void)
{
	if (sni_brd_type == SNI_BRD_RM200) {
		platform_device_register(&rm200_serial8250_device);
		platform_device_register(&rm200_ds1216_device);
		platform_device_register(&snirm_82596_rm200_pdev);
		platform_device_register(&snirm_53c710_rm200_pdev);
	}
	return 0;
}

device_initcall(snirm_setup_devinit);


#define SNI_RM200_INT_STAT_REG  0xbc000000
#define SNI_RM200_INT_ENA_REG   0xbc080000

#define SNI_RM200_INT_START  24
#define SNI_RM200_INT_END    28

static void enable_rm200_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - SNI_RM200_INT_START);

	*(volatile u8 *)SNI_RM200_INT_ENA_REG &= ~mask;
}

void disable_rm200_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - SNI_RM200_INT_START);

	*(volatile u8 *)SNI_RM200_INT_ENA_REG |= mask;
}

void end_rm200_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_rm200_irq(irq);
}

static struct irq_chip rm200_irq_type = {
	.typename = "RM200",
	.ack = disable_rm200_irq,
	.mask = disable_rm200_irq,
	.mask_ack = disable_rm200_irq,
	.unmask = enable_rm200_irq,
	.end = end_rm200_irq,
};

static void sni_rm200_hwint(void)
{
	u32 pending = read_c0_cause() & read_c0_status();
	u8 mask;
	u8 stat;
	int irq;

	if (pending & C_IRQ5)
		do_IRQ (MIPS_CPU_IRQ_BASE + 7);
	else if (pending & C_IRQ0) {
		clear_c0_status (IE_IRQ0);
		mask = *(volatile u8 *)SNI_RM200_INT_ENA_REG ^ 0x1f;
		stat = *(volatile u8 *)SNI_RM200_INT_STAT_REG ^ 0x14;
		irq = ffs(stat & mask & 0x1f);

		if (likely(irq > 0))
			do_IRQ (irq + SNI_RM200_INT_START - 1);
		set_c0_status (IE_IRQ0);
	}
}

void __init sni_rm200_irq_init(void)
{
	int i;

	* (volatile u8 *)SNI_RM200_INT_ENA_REG = 0x1f;

	mips_cpu_irq_init();
	/* Actually we've got more interrupts to handle ...  */
	for (i = SNI_RM200_INT_START; i <= SNI_RM200_INT_END; i++)
		set_irq_chip(i, &rm200_irq_type);
	sni_hwint = sni_rm200_hwint;
	change_c0_status(ST0_IM, IE_IRQ0);
	setup_irq (SNI_RM200_INT_START + 0, &sni_isa_irq);
}

void __init sni_rm200_init(void)
{
	set_io_port_base(SNI_PORT_BASE + 0x02000000);
	ioport_resource.end += 0x02000000;
	board_time_init = sni_cpu_time_init;
}
