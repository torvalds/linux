#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/system.h>
#include <linux/string.h>
#include <mach/cru.h>
#include <mach/iomux.h>
#include <mach/loader.h>
#include <mach/board.h>
#include <mach/pmu.h>

static bool is_panic = false;

static int panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	is_panic = true;
	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call	= panic_event,
};

static int __init arch_reset_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);
	return 0;
}
core_initcall(arch_reset_init);

static void rk30_arch_reset(char mode, const char *cmd)
{
	u32 boot_flag = 0;
	u32 boot_mode = BOOT_MODE_REBOOT;

	if (cmd) {
		if (!strcmp(cmd, "loader") || !strcmp(cmd, "bootloader")) 
			boot_flag = SYS_LOADER_REBOOT_FLAG + BOOT_LOADER;
		else if(!strcmp(cmd, "recovery"))
			boot_flag = SYS_LOADER_REBOOT_FLAG + BOOT_RECOVER;
		else if (!strcmp(cmd, "charge"))
			boot_mode = BOOT_MODE_CHARGE;
	} else {
		if (is_panic)
			boot_mode = BOOT_MODE_PANIC;
	}
#ifndef RK30_PMU_BASE
	writel_relaxed(boot_flag, RK30_GRF_BASE + GRF_OS_REG4);	// for loader
	writel_relaxed(boot_mode, RK30_GRF_BASE + GRF_OS_REG5);	// for linux
#else
	writel_relaxed(boot_flag, RK30_PMU_BASE + PMU_SYS_REG0);	// for loader
	writel_relaxed(boot_mode, RK30_PMU_BASE + PMU_SYS_REG1);	// for linux
#endif
	dsb();

	/* restore clk_cpu:aclk_cpu to default value for RK3168 */
#if defined(CONFIG_ARCH_RK3066B)
	writel_relaxed(0x00070001 , RK30_CRU_BASE + CRU_CLKSELS_CON(1));
#endif

        /* disable remap */
        writel_relaxed(1 << (12 + 16), RK30_GRF_BASE + GRF_SOC_CON0);
	/* pll enter slow mode */
	writel_relaxed(PLL_MODE_SLOW(APLL_ID) | PLL_MODE_SLOW(CPLL_ID) | PLL_MODE_SLOW(GPLL_ID), RK30_CRU_BASE + CRU_MODE_CON);
	dsb();
	writel_relaxed(0xeca8, RK30_CRU_BASE + CRU_GLB_SRST_SND);
	dsb();
}

void (*arch_reset)(char, const char *) = rk30_arch_reset;
