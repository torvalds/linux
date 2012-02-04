#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>

#include <mach/board.h>
#include <mach/gpio.h>

void __init rk30_init_irq(void)
{
	gic_init(0, IRQ_LOCALTIMER, RK30_GICD_BASE, RK30_GICC_BASE);
}

void __init rk30_map_io(void)
{
        rk30_map_common_io();
}

void __init rk30_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = SZ_128M;
}
