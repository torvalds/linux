#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/sram.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/fiq.h>



static void __init rk30_l2_cache_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	u32 aux_ctrl, aux_ctrl_mask;

	//Tag Ram Latency All 1-cycle
	writel_relaxed(0x0, RK30_L2C_BASE + L2X0_TAG_LATENCY_CTRL);
	// Data Ram Latency [10:8] 1-cycle [6:4] 4-cycles [2:0] 2 cycles
	writel_relaxed(0x031, RK30_L2C_BASE + L2X0_DATA_LATENCY_CTRL);
	/*
         * 16-way associativity, parity disabled
         * Way size - 32KB
         */
	aux_ctrl = ((1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) | // 16-way
			(0x1 << 25) | 	// round-robin
			(0x1 << 0) |		// Full Line of Zero Enable
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			(0x2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) | // 32KB way-size
			(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT) );

	aux_ctrl_mask = ~((1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) | // 16-way
			(0x1 << 25) | 	// round-robin
			(0x1 << 0) |		// Full Line of Zero Enable
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			(0x7 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) | // 32KB way-size
			(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT) );

	l2x0_init(RK30_L2C_BASE, aux_ctrl, aux_ctrl_mask);
#endif
}


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
	rk30_l2_cache_init();
	rk30_iomux_init();
}

void __init rk30_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = SZ_1G;
}

