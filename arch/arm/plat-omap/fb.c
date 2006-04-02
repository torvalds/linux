#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/bootmem.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <asm/arch/board.h>
#include <asm/arch/sram.h>
#include <asm/arch/omapfb.h>

#if defined(CONFIG_FB_OMAP) || defined(CONFIG_FB_OMAP_MODULE)

static struct omapfb_platform_data omapfb_config;

static u64 omap_fb_dma_mask = ~(u32)0;

static struct platform_device omap_fb_device = {
	.name		= "omapfb",
	.id		= -1,
	.dev = {
		.dma_mask		= &omap_fb_dma_mask,
		.coherent_dma_mask	= ~(u32)0,
		.platform_data		= &omapfb_config,
	},
	.num_resources = 0,
};

/* called from map_io */
void omapfb_reserve_mem(void)
{
	const struct omap_fbmem_config *fbmem_conf;

	omapfb_config.fbmem.fb_sram_start = omap_fb_sram_start;
	omapfb_config.fbmem.fb_sram_size = omap_fb_sram_size;

	fbmem_conf = omap_get_config(OMAP_TAG_FBMEM, struct omap_fbmem_config);

	if (fbmem_conf != NULL) {
		/* indicate that the bootloader already initialized the
		 * fb device, so we'll skip that part in the fb driver
		 */
		omapfb_config.fbmem.fb_sdram_start = fbmem_conf->fb_sdram_start;
		omapfb_config.fbmem.fb_sdram_size = fbmem_conf->fb_sdram_size;
		if (fbmem_conf->fb_sdram_size) {
			pr_info("Reserving %u bytes SDRAM for frame buffer\n",
				fbmem_conf->fb_sdram_size);
			reserve_bootmem(fbmem_conf->fb_sdram_start,
					fbmem_conf->fb_sdram_size);
		}
	}
}

static inline int omap_init_fb(void)
{
	const struct omap_lcd_config *conf;

	conf = omap_get_config(OMAP_TAG_LCD, struct omap_lcd_config);
	if (conf == NULL)
		return 0;

	omapfb_config.lcd = *conf;

	return platform_device_register(&omap_fb_device);
}

arch_initcall(omap_init_fb);

#else

void omapfb_reserve_mem(void) {}

#endif


