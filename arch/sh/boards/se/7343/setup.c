#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/machvec.h>
#include <asm/mach/se7343.h>
#include <asm/irq.h>

void init_7343se_IRQ(void);

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= 0x10000000,
		.end	= 0x1000000F,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/*
		 * shared with other devices via externel
		 * interrupt controller in FPGA...
		 */
		.start	= EXT_IRQ2,
		.end	= EXT_IRQ2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= PA_LED,
		.end	= PA_LED,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct platform_device *sh7343se_platform_devices[] __initdata = {
	&smc91x_device,
	&heartbeat_device,
};

static int __init sh7343se_devices_setup(void)
{
	return platform_add_devices(sh7343se_platform_devices,
				    ARRAY_SIZE(sh7343se_platform_devices));
}

static void __init sh7343se_setup(char **cmdline_p)
{
	device_initcall(sh7343se_devices_setup);
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_7343se __initmv = {
	.mv_name = "SolutionEngine 7343",
	.mv_setup = sh7343se_setup,
	.mv_nr_irqs = 108,
	.mv_inb = sh7343se_inb,
	.mv_inw = sh7343se_inw,
	.mv_inl = sh7343se_inl,
	.mv_outb = sh7343se_outb,
	.mv_outw = sh7343se_outw,
	.mv_outl = sh7343se_outl,

	.mv_inb_p = sh7343se_inb_p,
	.mv_inw_p = sh7343se_inw,
	.mv_inl_p = sh7343se_inl,
	.mv_outb_p = sh7343se_outb_p,
	.mv_outw_p = sh7343se_outw,
	.mv_outl_p = sh7343se_outl,

	.mv_insb = sh7343se_insb,
	.mv_insw = sh7343se_insw,
	.mv_insl = sh7343se_insl,
	.mv_outsb = sh7343se_outsb,
	.mv_outsw = sh7343se_outsw,
	.mv_outsl = sh7343se_outsl,

	.mv_init_irq = init_7343se_IRQ,
	.mv_irq_demux = shmse_irq_demux,
};
