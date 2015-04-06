#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/platform.h>

extern void shared_3ds_printf(const char *str, ...);

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc nintendo3ds_ctr_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(NINTENDO3DS_CTR_PRIV_MEM_BASE),
		.pfn		= __phys_to_pfn(NINTENDO3DS_CTR_PRIV_MEM_BASE),
		.length		= NINTENDO3DS_CTR_PRIV_MEM_SIZE,
		.type		= MT_DEVICE,
	}
};

void __init nintendo3ds_ctr_map_io(void)
{
	shared_3ds_printf("nintendo3ds_ctr_map_io\n");
	iotable_init(nintendo3ds_ctr_io_desc, ARRAY_SIZE(nintendo3ds_ctr_io_desc));
}

void nintendo3ds_ctr_restart(enum reboot_mode mode, const char *cmd)
{
	shared_3ds_printf("nintendo3ds_ctr_restart\n");
}

static void __init nintendo3ds_ctr_timer_init(void)
{
	shared_3ds_printf("nintendo3ds_ctr_timer_init\n");
}

static void __init nintendo3ds_ctr_init_irq(void)
{
	unsigned int idcr;

	shared_3ds_printf("nintendo3ds_ctr_init_irq\n");

	// Disable GIC
	idcr = readl(__io_address(NINTENDO3DS_CTR_GIC_IDCR));
	idcr &= ~0b1;
	writel(idcr, __io_address(NINTENDO3DS_CTR_GIC_IDCR));
}

static void __init nintendo3ds_ctr_init_early(void)
{
	shared_3ds_printf("nintendo3ds_ctr_init_early\n");
}

/* Board Init */
static void __init nintendo3ds_ctr_init(void)
{
	shared_3ds_printf("nintendo3ds_ctr_init\n");
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

