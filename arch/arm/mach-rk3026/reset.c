#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/system.h>
#include <linux/string.h>
#include <mach/cru.h>
#include <mach/grf.h>
#include <mach/loader.h>
#include <mach/board.h>

//#define DEBUG // for jtag debug

#define cru_readl(offset)     readl_relaxed(RK30_CRU_BASE + offset)
#define cru_writel(v, offset) do { writel_relaxed(v, RK30_CRU_BASE + offset); } while (0)
#define grf_readl(offset)     readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v, offset) do { writel_relaxed(v, RK30_GRF_BASE + offset); dsb(); } while (0)

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

#ifdef DEBUG
__sramdata volatile int reset_loop = 1;
#endif

static void __sramfunc __noreturn soft_reset(void)
{
	/* pll enter slow mode */
	cru_writel(0xffff0000, CRU_MODE_CON);
	dsb();

	/* restore clock select and divide */
	cru_writel(0xffff0200, CRU_CLKSELS_CON(0));
	cru_writel(0xffff3113, CRU_CLKSELS_CON(1));
	cru_writel(0xfff00000, CRU_CLKSELS_CON(2)); // 3:0 reserved
	cru_writel(0xffff0200, CRU_CLKSELS_CON(3));
	cru_writel(0x003f0000, CRU_CLKSELS_CON(4)); // 15:6 reserved
	cru_writel(0x003f0000, CRU_CLKSELS_CON(5)); // 15:6 reserved
	cru_writel(0x0bb8ea60, CRU_CLKSELS_CON(6));
	cru_writel(0x0bb8ea60, CRU_CLKSELS_CON(7));
	cru_writel(0xffff2100, CRU_CLKSELS_CON(10));
	cru_writel(0x007f0017, CRU_CLKSELS_CON(11)); // 15:7 reserved
	cru_writel(0xffff1717, CRU_CLKSELS_CON(12));
	cru_writel(0xffff0200, CRU_CLKSELS_CON(13));
	cru_writel(0xffff0200, CRU_CLKSELS_CON(14));
	cru_writel(0xffff0200, CRU_CLKSELS_CON(15));
	cru_writel(0x0bb8ea60, CRU_CLKSELS_CON(17));
	cru_writel(0x0bb8ea60, CRU_CLKSELS_CON(18));
	cru_writel(0x0bb8ea60, CRU_CLKSELS_CON(19));
	cru_writel(0x0bb8ea60, CRU_CLKSELS_CON(23));
	cru_writel(0xffff1700, CRU_CLKSELS_CON(24));
	cru_writel(0x017f0107, CRU_CLKSELS_CON(25)); // 15:9 7 reserved
	cru_writel(0xffff0000, CRU_CLKSELS_CON(26));
	cru_writel(0xffff0700, CRU_CLKSELS_CON(27));
	cru_writel(0xffff0700, CRU_CLKSELS_CON(28));
	cru_writel(0xffff0012, CRU_CLKSELS_CON(29));
	cru_writel(0xffff0300, CRU_CLKSELS_CON(30));
	cru_writel(0xffff0001, CRU_CLKSELS_CON(31));
	cru_writel(0xffff0303, CRU_CLKSELS_CON(32));
	cru_writel(0xffff0003, CRU_CLKSELS_CON(34));
	dsb();

	/* idle request PERI/VIO/VPU/GPU */
	grf_writel(0x1e00ffff, GRF_SOC_CON2);
	while ((grf_readl(GRF_SOC_STATUS0) & (0x1e << 16)) != (0x1e << 16))
		;
	while ((grf_readl(GRF_SOC_STATUS0) & (0x1e << 22)) != (0x1e << 22))
		;

	/* software reset modules */
#ifdef DEBUG
	cru_writel(0xfff3ffff, CRU_SOFTRSTS_CON(8)); // CORE_DBG/DBG_APB
#else
	cru_writel(0xffffffff, CRU_SOFTRSTS_CON(8));
#endif
	cru_writel(0xffffffff, CRU_SOFTRSTS_CON(7));
	cru_writel(0xffffffff, CRU_SOFTRSTS_CON(6));
	cru_writel(0xffffffff, CRU_SOFTRSTS_CON(5));
	cru_writel(0x7fffffff, CRU_SOFTRSTS_CON(4)); // DDRMSCH
#ifdef DEBUG
	cru_writel(0xff63ffff, CRU_SOFTRSTS_CON(3)); // DAP_PO/DAP/DAP_SYS
#else
	cru_writel(0xff7fffff, CRU_SOFTRSTS_CON(3)); // GRF
#endif
	cru_writel(0xfff0ffff, CRU_SOFTRSTS_CON(2)); // GPIO0/1/2/3
	cru_writel(0xffdfffff, CRU_SOFTRSTS_CON(1)); // INTMEM
#ifdef DEBUG
	cru_writel(0x1e60ffff, CRU_SOFTRSTS_CON(0)); // MCORE_DBG/CORE0_DBG
#else
	cru_writel(0x1fe0ffff, CRU_SOFTRSTS_CON(0)); // CORE_SRST_WDT_SEL/MCORE/CORE0/CORE1/ACLK_CORE/STRC_SYS_AXI/L2C
#endif
	dsb();

	sram_udelay(1000);

	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(0));
	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(1));
	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(2));
	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(3));
	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(4));
	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(5));
	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(6));
	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(7));
	cru_writel(0xffff0000, CRU_SOFTRSTS_CON(8));
	dsb();

	/* disable idle request */
	grf_writel(0x3f000000, GRF_SOC_CON2);
	while (grf_readl(GRF_SOC_STATUS0) & (0x3f << 16))
		;
	while (grf_readl(GRF_SOC_STATUS0) & (0x3f << 22))
		;

#ifdef DEBUG
//	while (reset_loop);
#endif

	cru_writel(0x801cffff, CRU_SOFTRSTS_CON(0)); // MCORE/CORE0/CORE1/L2C
	dsb();

	while (1);
}

static void rk30_arch_reset(char mode, const char *cmd)
{
	unsigned i;
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
	grf_writel(boot_flag, GRF_OS_REG4);	// for loader
	grf_writel(boot_mode, GRF_OS_REG5);	// for linux

	/* enable all clock */
	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++)
		cru_writel(0xffff0000, CRU_CLKGATES_CON(i));

	/* disable remap */
	grf_writel(1 << (12 + 16), GRF_SOC_CON0);

	((void(*)(void))((u32)soft_reset - (u32)RK30_IMEM_BASE + (u32)RK30_IMEM_NONCACHED))();
}

void (*arch_reset)(char, const char *) = rk30_arch_reset;
