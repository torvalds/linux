#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/system.h>
#include <mach/cru.h>
#include <mach/iomux.h>

static void rk30_arch_reset(char mode, const char *cmd)
{
	/* disable remap */
	writel_relaxed(1 << (12 + 16), RK30_GRF_BASE + GRF_SOC_CON0);
	dsb();
	writel_relaxed(0xeca8, RK30_CRU_BASE + CRU_GLB_SRST_SND);
	dsb();
}

void (*arch_reset)(char, const char *) = rk30_arch_reset;
