#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <asm/machvec.h>
#include <mach-se/mach/se7343.h>
#include <asm/heartbeat.h>
#include <asm/irq.h>
#include <asm/io.h>

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
		.start	= SMC_IRQ,
		.end	= SMC_IRQ,
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

static struct heartbeat_data heartbeat_data = {
	.regsize = 16,
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.dev = {
		.platform_data = &heartbeat_data,
	},
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct mtd_partition nor_flash_partitions[] = {
	{
		.name		= "loader",
		.offset		= 0x00000000,
		.size		= 128 * 1024,
	},
	{
		.name		= "rootfs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 31 * 1024 * 1024,
	},
	{
		.name		= "data",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
	.parts		= nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(nor_flash_partitions),
};

static struct resource nor_flash_resources[] = {
	[0]	= {
		.start	= 0x00000000,
		.end	= 0x01ffffff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data	= &nor_flash_data,
	},
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.resource	= nor_flash_resources,
};

static struct platform_device *sh7343se_platform_devices[] __initdata = {
	&smc91x_device,
	&heartbeat_device,
	&nor_flash_device,
};

static int __init sh7343se_devices_setup(void)
{
	return platform_add_devices(sh7343se_platform_devices,
				    ARRAY_SIZE(sh7343se_platform_devices));
}
device_initcall(sh7343se_devices_setup);

/*
 * Initialize the board
 */
static void __init sh7343se_setup(char **cmdline_p)
{
	ctrl_outw(0xf900, FPGA_OUT);	/* FPGA */

	ctrl_outw(0x0002, PORT_PECR);	/* PORT E 1 = IRQ5 */
	ctrl_outw(0x0020, PORT_PSELD);

	printk(KERN_INFO "MS7343CP01 Setup...done\n");
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
};
