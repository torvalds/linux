#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

//#include <mach/nintendo3ds.h>


/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc nintendo3ds_ctr_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long) 0,
		.pfn		= 0,
		.length		= 0,
		.type		= MT_DEVICE,
	}
};

void __init nintendo3ds_ctr_map_io(void)
{
	iotable_init(nintendo3ds_ctr_io_desc, ARRAY_SIZE(nintendo3ds_ctr_io_desc));
}

void nintendo3ds_ctr_restart(enum reboot_mode mode, const char *cmd)
{
}

static void __init nintendo3ds_ctr_timer_init(void)
{
}

static void __init nintendo3ds_ctr_init_irq(void)
{
}

static void __init nintendo3ds_ctr_init_early(void)
{
}

/* Board Init */
static void __init nintendo3ds_ctr_init(void)
{
}



MACHINE_START(NINTENDO3DS_CTR, "Nintendo 3DS (CTR)")
	.atag_offset	= 0x100,
	.init_machine	= nintendo3ds_ctr_init,
	.map_io		= nintendo3ds_ctr_map_io,
	.init_early	= nintendo3ds_ctr_init_early,
	.init_irq	= nintendo3ds_ctr_init_irq,
	.init_time	= nintendo3ds_ctr_timer_init,
	.restart	= nintendo3ds_ctr_restart,
MACHINE_END

