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
#include <mach/ddr.h>
#include <mach/cpu.h>
#include <mach/cpu_axi.h>
#include <mach/debug_uart.h>

static void __init cpu_axi_init(void)
{
	CPU_AXI_SET_QOS_PRIORITY(0, 0, CPU0);
	CPU_AXI_SET_QOS_PRIORITY(0, 0, CPU1R);
	CPU_AXI_SET_QOS_PRIORITY(0, 0, CPU1W);
	CPU_AXI_SET_QOS_PRIORITY(0, 0, PERI);
	CPU_AXI_SET_QOS_PRIORITY(3, 3, LCDC0);
	CPU_AXI_SET_QOS_PRIORITY(3, 3, LCDC1);
	CPU_AXI_SET_QOS_PRIORITY(2, 1, GPU);

	writel_relaxed(0x3f, RK30_CPU_AXI_BUS_BASE + 0x0014);	// memory scheduler read latency
	dsb();
}

#define L2_LY_SP_OFF (0)
#define L2_LY_SP_MSK (0x7)

#define L2_LY_RD_OFF (4)
#define L2_LY_RD_MSK (0x7)

#define L2_LY_WR_OFF (8)
#define L2_LY_WR_MSK (0x7)
#define L2_LY_SET(ly,off) (((ly)-1)<<(off))

#define L2_LATENCY(setup_cycles, read_cycles, write_cycles) \
	L2_LY_SET(setup_cycles, L2_LY_SP_OFF) | \
	L2_LY_SET(read_cycles, L2_LY_RD_OFF) | \
	L2_LY_SET(write_cycles, L2_LY_WR_OFF)

static void __init l2_cache_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	u32 aux_ctrl, aux_ctrl_mask;

	writel_relaxed(L2_LATENCY(1, 1, 1), RK30_L2C_BASE + L2X0_TAG_LATENCY_CTRL);
	writel_relaxed(L2_LATENCY(2, 3, 1), RK30_L2C_BASE + L2X0_DATA_LATENCY_CTRL);

	/* L2X0 Prefetch Control */
	writel_relaxed(0x70000003, RK30_L2C_BASE + L2X0_PREFETCH_CTRL);

	/* L2X0 Power Control */
	writel_relaxed(L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN, RK30_L2C_BASE + L2X0_POWER_CTRL);

	/* force 16-way, 16KB way-size on RK3026 */
	aux_ctrl = (
			(1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) | // 16-way
			(0x1 << 25) |		// Round-robin cache replacement policy
			(0x1 << 0) |		// Full Line of Zero Enable
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) | // 16KB way-size
			(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT) );

	aux_ctrl_mask = ~(
			(1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) |
			(0x1 << 25) |		// Cache replacement policy
			(0x1 << 0) |		// Full Line of Zero Enable
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			(0x7 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT) );

	l2x0_init(RK30_L2C_BASE, aux_ctrl, aux_ctrl_mask);
#endif
}

static int boot_mode;

static void __init boot_mode_init(void)
{
	u32 boot_flag = readl_relaxed(RK30_GRF_BASE + GRF_OS_REG4);
	boot_mode = readl_relaxed(RK30_GRF_BASE + GRF_OS_REG5);

	if (boot_flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER)) {
		boot_mode = BOOT_MODE_RECOVERY;
	}
	if (boot_mode || ((boot_flag & 0xff) && ((boot_flag & 0xffffff00) == SYS_KERNRL_REBOOT_FLAG)))
		printk("Boot mode: %s (%d) flag: %s (0x%08x)\n", boot_mode_name(boot_mode), boot_mode, boot_flag_name(boot_flag), boot_flag);
#ifdef CONFIG_RK29_WATCHDOG
	writel_relaxed(BOOT_MODE_WATCHDOG, RK30_GRF_BASE + GRF_OS_REG5);
#endif
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
	soc_gpio_init();
}

static unsigned int __initdata ddr_freq = DDR_FREQ;
static int __init ddr_freq_setup(char *str)
{
	get_option(&str, &ddr_freq);
	return 0;
}
early_param("ddr_freq", ddr_freq_setup);

static void usb_uart_init(void)
{
#ifdef DEBUG_UART_BASE
	writel_relaxed(0x34000000, RK2928_GRF_BASE + GRF_UOC1_CON0);
#ifdef CONFIG_RK_USB_UART
	writel_relaxed(0x34000000, RK30_GRF_BASE + GRF_UOC1_CON0);

	if((readl_relaxed(RK30_GRF_BASE + GRF_SOC_STATUS0) & (1<<10)))//detect id
	{
		if(!(readl_relaxed(RK30_GRF_BASE + GRF_SOC_STATUS0) & (1<<7)))//detect vbus
		{
			writel_relaxed(0x007f0055, RK30_GRF_BASE + GRF_UOC0_CON0);
			writel_relaxed(0x34003000, RK30_GRF_BASE + GRF_UOC1_CON0);
		}
		else
		{
			writel_relaxed(0x34000000, RK30_GRF_BASE + GRF_UOC1_CON0);
		}
	}

#endif // end of CONFIG_RK_USB_UART
    writel_relaxed(0x07, DEBUG_UART_BASE + 0x88);
    writel_relaxed(0x07, DEBUG_UART_BASE + 0x88);
    writel_relaxed(0x00, DEBUG_UART_BASE + 0x04);
    writel_relaxed(0x83, DEBUG_UART_BASE + 0x0c);
    writel_relaxed(0x0d, DEBUG_UART_BASE + 0x00);
    writel_relaxed(0x00, DEBUG_UART_BASE + 0x04);
    writel_relaxed(0x03, DEBUG_UART_BASE + 0x0c);
#endif //end of DEBUG_UART_BASE
}

void __init rk2928_map_io(void)
{
	rk2928_map_common_io();
	usb_uart_init();
	rk29_setup_early_printk();
	cpu_axi_init();
	rk29_sram_init();
	board_clock_init();
	l2_cache_init();
	ddr_init(DDR_TYPE, ddr_freq);
	iomux_init();
	boot_mode_init();
}

static __init u32 get_ddr_size(void)
{
	u32 size;
	u32 v[1], a[1];
	u32 pgtbl = PAGE_OFFSET + TEXT_OFFSET - 0x4000;
	u32 flag = PMD_TYPE_SECT | PMD_SECT_XN | PMD_SECT_AP_WRITE | PMD_SECT_AP_READ;

	a[0] = pgtbl + (((u32)RK30_GRF_BASE >> 20) << 2);
	v[0] = readl_relaxed(a[0]);
	writel_relaxed(flag | ((RK30_GRF_PHYS >> 20) << 20), a[0]);

	size = ddr_get_cap();

	writel_relaxed(v[0], a[0]);

	return size;
}

void __init rk2928_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = get_ddr_size();
}

