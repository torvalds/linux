/*
 * linux/arch/arm/mach-sa1100/simpad.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/string.h> 
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/arch/mcp.h>
#include <asm/arch/simpad.h>

#include <linux/serial_core.h>
#include <linux/ioport.h>
#include <asm/io.h>

#include "generic.h"

long cs3_shadow;

long get_cs3_shadow(void)
{
	return cs3_shadow;
}

void set_cs3(long value)
{
	*(CS3BUSTYPE *)(CS3_BASE) = cs3_shadow = value;
}

void set_cs3_bit(int value)
{
	cs3_shadow |= value;
	*(CS3BUSTYPE *)(CS3_BASE) = cs3_shadow;
}

void clear_cs3_bit(int value)
{
	cs3_shadow &= ~value;
	*(CS3BUSTYPE *)(CS3_BASE) = cs3_shadow;
}

EXPORT_SYMBOL(set_cs3_bit);
EXPORT_SYMBOL(clear_cs3_bit);

static struct map_desc simpad_io_desc[] __initdata = {
	{	/* MQ200 */
		.virtual	=  0xf2800000,
		.pfn		= __phys_to_pfn(0x4b800000),
		.length		= 0x00800000,
		.type		= MT_DEVICE
	}, {	/* Paules CS3, write only */
		.virtual	=  0xf1000000,
		.pfn		= __phys_to_pfn(0x18000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	},
};


static void simpad_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == (u_int)&Ser1UTCR0) {
		if (state)
		{
			clear_cs3_bit(RS232_ON);
			clear_cs3_bit(DECT_POWER_ON);
		}else
		{
			set_cs3_bit(RS232_ON);
			set_cs3_bit(DECT_POWER_ON);
		}
	}
}

static struct sa1100_port_fns simpad_port_fns __initdata = {
	.pm	   = simpad_uart_pm,
};


static struct mtd_partition simpad_partitions[] = {
	{
		.name       = "SIMpad boot firmware",
		.size       = 0x00080000,
		.offset     = 0,
		.mask_flags = MTD_WRITEABLE,
	}, {
		.name       = "SIMpad kernel",
		.size       = 0x0010000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "SIMpad root jffs2",
		.size       = MTDPART_SIZ_FULL,
		.offset     = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data simpad_flash_data = {
	.map_name    = "cfi_probe",
	.parts       = simpad_partitions,
	.nr_parts    = ARRAY_SIZE(simpad_partitions),
};


static struct resource simpad_flash_resources [] = {
	{
		.start     = SA1100_CS0_PHYS,
		.end       = SA1100_CS0_PHYS + SZ_16M -1,
		.flags     = IORESOURCE_MEM,
	}, {
		.start     = SA1100_CS1_PHYS,
		.end       = SA1100_CS1_PHYS + SZ_16M -1,
		.flags     = IORESOURCE_MEM,
	}
};

static struct mcp_plat_data simpad_mcp_data = {
	.mccr0		= MCCR0_ADM,
	.sclk_rate	= 11981000,
};



static void __init simpad_map_io(void)
{
	sa1100_map_io();

	iotable_init(simpad_io_desc, ARRAY_SIZE(simpad_io_desc));

	set_cs3_bit (EN1 | EN0 | LED2_ON | DISPLAY_ON | RS232_ON |
		      ENABLE_5V | RESET_SIMCARD | DECT_POWER_ON);


        sa1100_register_uart_fns(&simpad_port_fns);
	sa1100_register_uart(0, 3);  /* serial interface */
	sa1100_register_uart(1, 1);  /* DECT             */

	// Reassign UART 1 pins
	GAFR |= GPIO_UART_TXD | GPIO_UART_RXD;
	GPDR |= GPIO_UART_TXD | GPIO_LDD13 | GPIO_LDD15;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;

	/*
	 * Set up registers for sleep mode.
	 */


	PWER = PWER_GPIO0| PWER_RTC;
	PGSR = 0x818;
	PCFR = 0;
	PSDR = 0;

	sa11x0_set_flash_data(&simpad_flash_data, simpad_flash_resources,
			      ARRAY_SIZE(simpad_flash_resources));
	sa11x0_set_mcp_data(&simpad_mcp_data);
}

static void simpad_power_off(void)
{
	local_irq_disable(); // was cli
	set_cs3(0x800);        /* only SD_MEDIAQ */

	/* disable internal oscillator, float CS lines */
	PCFR = (PCFR_OPDE | PCFR_FP | PCFR_FS);
	/* enable wake-up on GPIO0 (Assabet...) */
	PWER = GFER = GRER = 1;
	/*
	 * set scratchpad to zero, just in case it is used as a
	 * restart address by the bootloader.
	 */
	PSPR = 0;
	PGSR = 0;
	/* enter sleep mode */
	PMCR = PMCR_SF;
	while(1);

	local_irq_enable(); /* we won't ever call it */


}


/*
 * MediaQ Video Device
 */
static struct platform_device simpad_mq200fb = {
	.name = "simpad-mq200",
	.id   = 0,
};

static struct platform_device *devices[] __initdata = {
	&simpad_mq200fb
};



static int __init simpad_init(void)
{
	int ret;

	pm_power_off = simpad_power_off;

	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if(ret)
		printk(KERN_WARNING "simpad: Unable to register mq200 framebuffer device");

	return 0;
}

arch_initcall(simpad_init);


MACHINE_START(SIMPAD, "Simpad")
	/* Maintainer: Holger Freyther */
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= simpad_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
MACHINE_END
