#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
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
#include <asm/smp_twd.h>

#include <mach/hardware.h>
#include <mach/platform.h>

#include <mach/bottom_lcd.h>
#include <mach/pxi.h>

static void __init nintendo3ds_pdn_set_spi_new(void)
{
	void __iomem *pdn_spi_cnt;
	u16 val;

	pdn_spi_cnt = ioremap(NINTENDO3DS_REG_PDN_SPI_CNT, 4);

	val = ioread16(pdn_spi_cnt);
	val |= 0b111;
	iowrite16(val, pdn_spi_cnt);

	iounmap(pdn_spi_cnt);
}

static void __init nintendo3ds_ctr_dt_init_machine(void)
{
	printk("nintendo3ds_ctr_dt_init_machine\n");

	nintendo3ds_bottom_setup_fb();
	nintendo3ds_bottom_lcd_map_fb();
	nintendo3ds_pdn_set_spi_new();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static void __init nintendo3ds_ctr_restart(enum reboot_mode mode, const char *cmd)
{
	printk("nintendo3ds_ctr_restart\n");

	nintendo3ds_bottom_lcd_unmap_fb();
}


static const char *nintendo3ds_ctr_dt_platform_compat[] __initconst = {
	"nintendo3ds,ctr",
	NULL,
};

DT_MACHINE_START(NINTENDO3DS_DT, "Nintendo 3DS (CTR) (Device Tree)")
	.init_machine	= nintendo3ds_ctr_dt_init_machine,
	.dt_compat	= nintendo3ds_ctr_dt_platform_compat,
	.restart	= nintendo3ds_ctr_restart,
MACHINE_END
