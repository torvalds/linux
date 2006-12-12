/*
 * Renesas Technology Sales RTS7751R2D Support.
 *
 * Copyright (C) 2002 Atom Create Engineering Co., Ltd.
 * Copyright (C) 2004 - 2006 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/pm.h>
#include <asm/machvec.h>
#include <asm/mach/rts7751r2d.h>
#include <asm/io.h>
#include <asm/voyagergx.h>

extern void heartbeat_rts7751r2d(void);
extern void init_rts7751r2d_IRQ(void);
extern int rts7751r2d_irq_demux(int irq);

extern void *voyagergx_consistent_alloc(struct device *, size_t, dma_addr_t *, gfp_t);
extern int voyagergx_consistent_free(struct device *, size_t, void *, dma_addr_t);

static struct plat_serial8250_port uart_platform_data[] = {
	{
		.membase	= (void *)VOYAGER_UART_BASE,
		.mapbase	= VOYAGER_UART_BASE,
		.iotype		= UPIO_MEM,
		.irq		= VOYAGER_UART0_IRQ,
		.flags		= UPF_BOOT_AUTOCONF,
		.regshift	= 2,
		.uartclk	= (9600 * 16),
	}, {
		.flags		= 0,
	},
};

static void __init voyagergx_serial_init(void)
{
	unsigned long val;

	/*
	 * GPIO Control
	 */
	val = inl(GPIO_MUX_HIGH);
	val |= 0x00001fe0;
	outl(val, GPIO_MUX_HIGH);

	/*
	 * Power Mode Gate
	 */
	val = inl(POWER_MODE0_GATE);
	val |= (POWER_MODE0_GATE_U0 | POWER_MODE0_GATE_U1);
	outl(val, POWER_MODE0_GATE);

	val = inl(POWER_MODE1_GATE);
	val |= (POWER_MODE1_GATE_U0 | POWER_MODE1_GATE_U1);
	outl(val, POWER_MODE1_GATE);
}

static struct platform_device uart_device = {
	.name		= "serial8250",
	.id		= -1,
	.dev		= {
		.platform_data	= uart_platform_data,
	},
};

static struct platform_device *rts7751r2d_devices[] __initdata = {
	&uart_device,
};

static int __init rts7751r2d_devices_setup(void)
{
	return platform_add_devices(rts7751r2d_devices,
				    ARRAY_SIZE(rts7751r2d_devices));
}

static void rts7751r2d_power_off(void)
{
	ctrl_outw(0x0001, PA_POWOFF);
}

/*
 * Initialize the board
 */
static void __init rts7751r2d_setup(char **cmdline_p)
{
	device_initcall(rts7751r2d_devices_setup);

	ctrl_outw(0x0000, PA_OUTPORT);
	pm_power_off = rts7751r2d_power_off;

	voyagergx_serial_init();

	printk(KERN_INFO "Renesas Technology Sales RTS7751R2D support.\n");
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_rts7751r2d __initmv = {
	.mv_name		= "RTS7751R2D",
	.mv_setup		= rts7751r2d_setup,
	.mv_nr_irqs		= 72,

	.mv_inb			= rts7751r2d_inb,
	.mv_inw			= rts7751r2d_inw,
	.mv_inl			= rts7751r2d_inl,
	.mv_outb		= rts7751r2d_outb,
	.mv_outw		= rts7751r2d_outw,
	.mv_outl		= rts7751r2d_outl,

	.mv_inb_p		= rts7751r2d_inb_p,
	.mv_inw_p		= rts7751r2d_inw,
	.mv_inl_p		= rts7751r2d_inl,
	.mv_outb_p		= rts7751r2d_outb_p,
	.mv_outw_p		= rts7751r2d_outw,
	.mv_outl_p		= rts7751r2d_outl,

	.mv_insb		= rts7751r2d_insb,
	.mv_insw		= rts7751r2d_insw,
	.mv_insl		= rts7751r2d_insl,
	.mv_outsb		= rts7751r2d_outsb,
	.mv_outsw		= rts7751r2d_outsw,
	.mv_outsl		= rts7751r2d_outsl,

	.mv_init_irq		= init_rts7751r2d_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_rts7751r2d,
#endif
	.mv_irq_demux		= rts7751r2d_irq_demux,

#ifdef CONFIG_USB_SM501
	.mv_consistent_alloc	= voyagergx_consistent_alloc,
	.mv_consistent_free	= voyagergx_consistent_free,
#endif
};
ALIAS_MV(rts7751r2d)
