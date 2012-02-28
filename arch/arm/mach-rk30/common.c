#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>

#include <plat/sram.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/fiq.h>

extern void __init rk29_setup_early_printk(void);

void __init rk30_init_irq(void)
{
	gic_init(0, IRQ_LOCALTIMER, RK30_GICD_BASE, RK30_GICC_BASE);
#ifdef CONFIG_FIQ
	rk30_fiq_init();
#endif
	rk30_gpio_init();
}

void __init rk30_map_io(void)
{
	rk30_map_common_io();
	rk29_setup_early_printk();
	rk29_sram_init();
	rk30_clock_init();
	rk30_iomux_init();
}

void __init rk30_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = SZ_128M;
}
