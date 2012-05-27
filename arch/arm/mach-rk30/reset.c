#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/system.h>
#include <linux/string.h>
#include <mach/cru.h>
#include <mach/iomux.h>
#include <mach/loader.h>
#include <mach/board.h>
#include <mach/pmu.h>

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
		if (system_state != SYSTEM_RESTART)
			boot_mode = BOOT_MODE_PANIC;
	}
	writel_relaxed(boot_flag, RK30_PMU_BASE + PMU_SYS_REG0);	// for loader
	writel_relaxed(boot_mode, RK30_PMU_BASE + PMU_SYS_REG1);	// for linux
	dsb();
	/* disable remap */
	writel_relaxed(1 << (12 + 16), RK30_GRF_BASE + GRF_SOC_CON0);
	/* pll enter slow mode */
	writel_relaxed(PLL_MODE_SLOW(APLL_ID) | PLL_MODE_SLOW(CPLL_ID) | PLL_MODE_SLOW(GPLL_ID), RK30_CRU_BASE + CRU_MODE_CON);
	dsb();
	writel_relaxed(0xeca8, RK30_CRU_BASE + CRU_GLB_SRST_SND);
	dsb();
}

void (*arch_reset)(char, const char *) = rk30_arch_reset;
