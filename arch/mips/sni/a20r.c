/*
 * A20R specific code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>

#include <asm/sni.h>
#include <asm/time.h>

#define PORT(_base,_irq)				\
	{						\
		.iobase		= _base,		\
		.irq		= _irq,			\
		.uartclk	= 1843200,		\
		.iotype		= UPIO_PORT,		\
		.flags		= UPF_BOOT_AUTOCONF,	\
	}

static struct plat_serial8250_port a20r_data[] = {
	PORT(0x3f8, 4),
	PORT(0x2f8, 3),
	{ },
};

static struct platform_device a20r_serial8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= a20r_data,
	},
};

static struct resource a20r_ds1216_rsrc[] = {
	{
		.start = 0x1c081ffc,
		.end   = 0x1c081fff,
		.flags = IORESOURCE_MEM
	}
};

static struct platform_device a20r_ds1216_device = {
	.name		= "rtc-ds1216",
	.num_resources	= ARRAY_SIZE(a20r_ds1216_rsrc),
	.resource	= a20r_ds1216_rsrc
};

static struct resource snirm_82596_rsrc[] = {
	{
		.start = 0x18000000,
		.end   = 0x18000004,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 0x18010000,
		.end   = 0x18010004,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 0x1ff00000,
		.end   = 0x1ff00020,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 22,
		.end   = 22,
		.flags = IORESOURCE_IRQ
	},
	{
		.flags = 0x01		     /* 16bit mpu port access */
	}
};

static struct platform_device snirm_82596_pdev = {
	.name		= "snirm_82596",
	.num_resources	= ARRAY_SIZE(snirm_82596_rsrc),
	.resource	= snirm_82596_rsrc
};

static struct resource snirm_53c710_rsrc[] = {
	{
		.start = 0x19000000,
		.end   = 0x190fffff,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 19,
		.end   = 19,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device snirm_53c710_pdev = {
	.name		= "snirm_53c710",
	.num_resources	= ARRAY_SIZE(snirm_53c710_rsrc),
	.resource	= snirm_53c710_rsrc
};

static struct resource sc26xx_rsrc[] = {
	{
		.start = 0x1c070000,
		.end   = 0x1c0700ff,
		.flags = IORESOURCE_MEM
	},
	{
		.start = 20,
		.end   = 20,
		.flags = IORESOURCE_IRQ
	}
};

#include <linux/platform_data/serial-sccnxp.h>

static struct sccnxp_pdata sccnxp_data = {
	.reg_shift	= 2,
	.mctrl_cfg[0]	= MCTRL_SIG(DTR_OP, LINE_OP7) |
			  MCTRL_SIG(RTS_OP, LINE_OP3) |
			  MCTRL_SIG(DSR_IP, LINE_IP5) |
			  MCTRL_SIG(DCD_IP, LINE_IP6),
	.mctrl_cfg[1]	= MCTRL_SIG(DTR_OP, LINE_OP2) |
			  MCTRL_SIG(RTS_OP, LINE_OP1) |
			  MCTRL_SIG(DSR_IP, LINE_IP0) |
			  MCTRL_SIG(CTS_IP, LINE_IP1) |
			  MCTRL_SIG(DCD_IP, LINE_IP2) |
			  MCTRL_SIG(RNG_IP, LINE_IP3),
};

static struct platform_device sc26xx_pdev = {
	.name		= "sc2681",
	.resource	= sc26xx_rsrc,
	.num_resources	= ARRAY_SIZE(sc26xx_rsrc),
	.dev	= {
		.platform_data	= &sccnxp_data,
	},
};

static u32 a20r_ack_hwint(void)
{
	u32 status = read_c0_status();

	write_c0_status(status | 0x00010000);
	asm volatile(
	"	.set	push			\n"
	"	.set	noat			\n"
	"	.set	noreorder		\n"
	"	lw	$1, 0(%0)		\n"
	"	sb	$0, 0(%1)		\n"
	"	sync				\n"
	"	lb	%1, 0(%1)		\n"
	"	b	1f			\n"
	"	ori	%1, $1, 2		\n"
	"	.align	8			\n"
	"1:					\n"
	"	nop				\n"
	"	sw	%1, 0(%0)		\n"
	"	sync				\n"
	"	li	%1, 0x20		\n"
	"2:					\n"
	"	nop				\n"
	"	bnez	%1,2b			\n"
	"	addiu	%1, -1			\n"
	"	sw	$1, 0(%0)		\n"
	"	sync				\n"
		".set	pop			\n"
	:
	: "Jr" (PCIMT_UCONF), "Jr" (0xbc000000));
	write_c0_status(status);

	return status;
}

static inline void unmask_a20r_irq(struct irq_data *d)
{
	set_c0_status(0x100 << (d->irq - SNI_A20R_IRQ_BASE));
	irq_enable_hazard();
}

static inline void mask_a20r_irq(struct irq_data *d)
{
	clear_c0_status(0x100 << (d->irq - SNI_A20R_IRQ_BASE));
	irq_disable_hazard();
}

static struct irq_chip a20r_irq_type = {
	.name		= "A20R",
	.irq_mask	= mask_a20r_irq,
	.irq_unmask	= unmask_a20r_irq,
};

/*
 * hwint 0 receive all interrupts
 */
static void a20r_hwint(void)
{
	u32 cause, status;
	int irq;

	clear_c0_status(IE_IRQ0);
	status = a20r_ack_hwint();
	cause = read_c0_cause();

	irq = ffs(((cause & status) >> 8) & 0xf8);
	if (likely(irq > 0))
		do_IRQ(SNI_A20R_IRQ_BASE + irq - 1);
	set_c0_status(IE_IRQ0);
}

void __init sni_a20r_irq_init(void)
{
	int i;

	for (i = SNI_A20R_IRQ_BASE + 2 ; i < SNI_A20R_IRQ_BASE + 8; i++)
		irq_set_chip_and_handler(i, &a20r_irq_type, handle_level_irq);
	sni_hwint = a20r_hwint;
	change_c0_status(ST0_IM, IE_IRQ0);
	if (request_irq(SNI_A20R_IRQ_BASE + 3, sni_isa_irq_handler,
			IRQF_SHARED, "ISA", sni_isa_irq_handler))
		pr_err("Failed to register ISA interrupt\n");
}

void sni_a20r_init(void)
{
	/* FIXME, remove if not needed */
}

static int __init snirm_a20r_setup_devinit(void)
{
	switch (sni_brd_type) {
	case SNI_BRD_TOWER_OASIC:
	case SNI_BRD_MINITOWER:
		platform_device_register(&snirm_82596_pdev);
		platform_device_register(&snirm_53c710_pdev);
		platform_device_register(&sc26xx_pdev);
		platform_device_register(&a20r_serial8250_device);
		platform_device_register(&a20r_ds1216_device);
		sni_eisa_root_init();
		break;
	}
	return 0;
}

device_initcall(snirm_a20r_setup_devinit);
