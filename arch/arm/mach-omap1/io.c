/*
 * linux/arch/arm/mach-omap1/io.c
 *
 * OMAP1 I/O mapping code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/arch/tc.h>

extern int clk_init(void);
extern void omap_check_revision(void);
extern void omap_sram_init(void);

/*
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */
static struct map_desc omap_io_desc[] __initdata = {
 { IO_VIRT,      	IO_PHYS,             IO_SIZE,        	   MT_DEVICE },
};

#ifdef CONFIG_ARCH_OMAP730
static struct map_desc omap730_io_desc[] __initdata = {
 { OMAP730_DSP_BASE,    OMAP730_DSP_START,    OMAP730_DSP_SIZE,    MT_DEVICE },
 { OMAP730_DSPREG_BASE, OMAP730_DSPREG_START, OMAP730_DSPREG_SIZE, MT_DEVICE },
};
#endif

#ifdef CONFIG_ARCH_OMAP1510
static struct map_desc omap1510_io_desc[] __initdata = {
 { OMAP1510_DSP_BASE,    OMAP1510_DSP_START,    OMAP1510_DSP_SIZE,    MT_DEVICE },
 { OMAP1510_DSPREG_BASE, OMAP1510_DSPREG_START, OMAP1510_DSPREG_SIZE, MT_DEVICE },
};
#endif

#if defined(CONFIG_ARCH_OMAP16XX)
static struct map_desc omap16xx_io_desc[] __initdata = {
 { OMAP16XX_DSP_BASE,    OMAP16XX_DSP_START,    OMAP16XX_DSP_SIZE,    MT_DEVICE },
 { OMAP16XX_DSPREG_BASE, OMAP16XX_DSPREG_START, OMAP16XX_DSPREG_SIZE, MT_DEVICE },
};
#endif

static int initialized = 0;

static void __init _omap_map_io(void)
{
	initialized = 1;

	/* We have to initialize the IO space mapping before we can run
	 * cpu_is_omapxxx() macros. */
	iotable_init(omap_io_desc, ARRAY_SIZE(omap_io_desc));
	omap_check_revision();

#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730()) {
		iotable_init(omap730_io_desc, ARRAY_SIZE(omap730_io_desc));
	}
#endif
#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		iotable_init(omap1510_io_desc, ARRAY_SIZE(omap1510_io_desc));
	}
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (cpu_is_omap16xx()) {
		iotable_init(omap16xx_io_desc, ARRAY_SIZE(omap16xx_io_desc));
	}
#endif

	omap_sram_init();

	/* REVISIT: Refer to OMAP5910 Errata, Advisory SYS_1: "Timeout Abort
	 * on a Posted Write in the TIPB Bridge".
	 */
	omap_writew(0x0, MPU_PUBLIC_TIPB_CNTL);
	omap_writew(0x0, MPU_PRIVATE_TIPB_CNTL);

	/* Must init clocks early to assure that timer interrupt works
	 */
	clk_init();
}

/*
 * This should only get called from board specific init
 */
void __init omap_map_common_io(void)
{
	if (!initialized)
		_omap_map_io();
}

