#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/system.h>
#include <linux/string.h>
#include <mach/cru.h>
#include <mach/iomux.h>
#include <mach/loader.h>

static void rk30_arch_reset(char mode, const char *cmd)
{
	unsigned int reason = 0;

	if(cmd) {
		if (!strcmp(cmd, "loader") || !strcmp(cmd, "bootloader")) 
			reason = SYS_LOADER_REBOOT_FLAG + BOOT_LOADER;
		else if(!strcmp(cmd, "recovery"))
			reason = SYS_LOADER_REBOOT_FLAG + BOOT_RECOVER;
	}
	writel_relaxed(reason, RK30_PMU_PHYS + 0x40);  //PMU_OS_REG0
	dsb();
	/* disable remap */
	writel_relaxed(1 << (12 + 16), RK30_GRF_BASE + GRF_SOC_CON0);
	dsb();
	writel_relaxed(0xeca8, RK30_CRU_BASE + CRU_GLB_SRST_SND);
	dsb();
}

void (*arch_reset)(char, const char *) = rk30_arch_reset;
