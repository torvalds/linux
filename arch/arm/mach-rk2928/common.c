#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/pgtable-hwdef.h>
#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/sram.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/fiq.h>
#include <mach/loader.h>
//#include <mach/ddr.h>

static void __init rk2928_cpu_axi_init(void)
{
	writel_relaxed(0x0, RK2928_CPU_AXI_BUS_BASE + 0x0088);	// cpu0
	writel_relaxed(0x0, RK2928_CPU_AXI_BUS_BASE + 0x0188);	// cpu1r
	writel_relaxed(0x0, RK2928_CPU_AXI_BUS_BASE + 0x0388);	// cpu1w
	writel_relaxed(0x0, RK2928_CPU_AXI_BUS_BASE + 0x4008);	// peri
	writel_relaxed(0x0, RK2928_CPU_AXI_BUS_BASE + 0x5008);	// gpu
	writel_relaxed(0x0, RK2928_CPU_AXI_BUS_BASE + 0x6008);	// vpu
	writel_relaxed(0xa, RK2928_CPU_AXI_BUS_BASE + 0x7188);	// lcdc
	writel_relaxed(0x0, RK2928_CPU_AXI_BUS_BASE + 0x7208);	// cif
	writel_relaxed(0x0, RK2928_CPU_AXI_BUS_BASE + 0x7288);	// rga
	writel_relaxed(0x3f, RK2928_CPU_AXI_BUS_BASE + 0x0014);	// memory scheduler read latency
	dsb();
}

#define L2_LY_SP_OFF (0)
#define L2_LY_SP_MSK (0x7)

#define L2_LY_RD_OFF (4)
#define L2_LY_RD_MSK (0x7)

#define L2_LY_WR_OFF (8)
#define L2_LY_WR_MSK (0x7)
#define L2_LY_SET(ly,off) (((ly)-1)<<(off))

static void __init rk2928_l2_cache_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	u32 aux_ctrl, aux_ctrl_mask;

	writel_relaxed(L2_LY_SET(1,L2_LY_SP_OFF)
				|L2_LY_SET(1,L2_LY_RD_OFF)
				|L2_LY_SET(1,L2_LY_WR_OFF), RK2928_L2C_BASE + L2X0_TAG_LATENCY_CTRL);
	writel_relaxed(L2_LY_SET(2,L2_LY_SP_OFF)
				|L2_LY_SET(3,L2_LY_RD_OFF)
				|L2_LY_SET(1,L2_LY_WR_OFF), RK2928_L2C_BASE + L2X0_DATA_LATENCY_CTRL);

	/* L2X0 Prefetch Control */
	writel_relaxed(0x70000003, RK2928_L2C_BASE + L2X0_PREFETCH_CTRL);

	/* L2X0 Power Control */
	writel_relaxed(L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN, RK2928_L2C_BASE + L2X0_POWER_CTRL);

	aux_ctrl = (
//			(1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) | // 16-way
			(0x1 << 25) |		// Round-robin cache replacement policy
			(0x1 << 0) |		// Full Line of Zero Enable
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
//			(0x2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) | // 32KB way-size
			(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT) );

	aux_ctrl_mask = ~(
//			(1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) | // 16-way
			(0x1 << 25) |		// Cache replacement policy
			(0x1 << 0) |		// Full Line of Zero Enable
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
//			(0x7 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) | // 32KB way-size
			(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT) );

	l2x0_init(RK2928_L2C_BASE, aux_ctrl, aux_ctrl_mask);
#endif
}

static int boot_mode;
static void __init rk2928_boot_mode_init(void)
{
	u32 boot_flag = (readl_relaxed(RK2928_GRF_BASE + GRF_OS_REG4) | (readl_relaxed(RK2928_GRF_BASE + GRF_OS_REG5) << 16)) - SYS_KERNRL_REBOOT_FLAG;
	boot_mode = readl_relaxed(RK2928_GRF_BASE + GRF_OS_REG6);

	if (boot_flag == BOOT_RECOVER) {
		boot_mode = BOOT_MODE_RECOVERY;
	}
	if (boot_mode || boot_flag)
		printk("Boot mode: %d flag: %d\n", boot_mode, boot_flag);
}

int board_boot_mode(void)
{
	return boot_mode;
}
EXPORT_SYMBOL(board_boot_mode);

void __init rk2928_init_irq(void)
{
	gic_init(0, IRQ_LOCALTIMER, GIC_DIST_BASE, GIC_CPU_BASE);
#ifdef CONFIG_FIQ
	rk_fiq_init();
#endif
	rk30_gpio_init();
}

extern void __init rk2928_map_common_io(void);
extern int __init clk_disable_unused(void);

void __init rk2928_map_io(void)
{
	rk2928_map_common_io();
	rk29_setup_early_printk();
	rk2928_cpu_axi_init();
	rk29_sram_init();
	board_clock_init();
	rk2928_l2_cache_init();
//	ddr_init(DDR_TYPE, DDR_FREQ);
//	clk_disable_unused();
	rk2928_iomux_init();
	rk2928_boot_mode_init();
}

extern u32 ddr_get_cap(void);
static __init u32 rk2928_get_ddr_size(void)
{
#ifdef CONFIG_MACH_RK2928_FPGA
	return SZ_64M;
#else
	return SZ_512M;
#endif
}

void __init rk2928_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = rk2928_get_ddr_size();
}

