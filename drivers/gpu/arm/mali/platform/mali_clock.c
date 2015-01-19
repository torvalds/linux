#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/pm.h>
#include <mach/register.h>
#include <mach/irqs.h>
#include <linux/io.h>
#include <mach/io.h>
#include <plat/io.h>
#include <asm/io.h>
#include "meson_main.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD

#define FCLK_MPLL2 (2 << 9)

static DEFINE_SPINLOCK(lock);
static mali_plat_info_t* pmali_plat = NULL;
static u32 mali_extr_backup = 0;
static u32 mali_extr_sample_backup = 0;

int mali_clock_init(mali_plat_info_t* mali_plat)
{
	u32 def_clk_data;
	if (mali_plat == NULL) {
		printk(" Mali platform data is NULL!!!\n");
		return -1;
	}

	pmali_plat = mali_plat;
	if (pmali_plat->have_switch) {
		def_clk_data = pmali_plat->clk[pmali_plat->def_clock];
		writel(def_clk_data | (def_clk_data << 16), (u32*)P_HHI_MALI_CLK_CNTL);
		setbits_le32((u32)P_HHI_MALI_CLK_CNTL, 1 << 24);
		setbits_le32((u32)P_HHI_MALI_CLK_CNTL, 1 << 8);
	} else {
		mali_clock_set(pmali_plat->def_clock);
	}

	mali_extr_backup = pmali_plat->clk[pmali_plat->clk_len - 1];
	mali_extr_sample_backup = pmali_plat->clk_sample[pmali_plat->clk_len - 1];
	return 0;
}

int mali_clock_critical(critical_t critical, size_t param)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	ret = critical(param);
	spin_unlock_irqrestore(&lock, flags);
	return ret;
}

static int critical_clock_set(size_t param)
{
	unsigned int idx = param;
	if (pmali_plat->have_switch) {
		u32 clk_value;
		setbits_le32((u32)P_HHI_MALI_CLK_CNTL, 1 << 31);
		clk_value = readl((u32 *)P_HHI_MALI_CLK_CNTL) & 0xffff0000;
		clk_value = clk_value | pmali_plat->clk[idx] | (1 << 8);
		writel(clk_value, (u32*)P_HHI_MALI_CLK_CNTL);
		clrbits_le32((u32)P_HHI_MALI_CLK_CNTL, 1 << 31);
	} else {
		clrbits_le32((u32)P_HHI_MALI_CLK_CNTL, 1 << 8);
		clrbits_le32((u32)P_HHI_MALI_CLK_CNTL, (0x7F | (0x7 << 9)));
		writel(pmali_plat->clk[idx], (u32*)P_HHI_MALI_CLK_CNTL);
		setbits_le32((u32)P_HHI_MALI_CLK_CNTL, 1 << 8);
	}
	return 0;
}

int mali_clock_set(unsigned int clock)
{
	return mali_clock_critical(critical_clock_set, (size_t)clock);
}

void disable_clock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	clrbits_le32((u32)P_HHI_MALI_CLK_CNTL, 1 << 8);
	spin_unlock_irqrestore(&lock, flags);
}

void enable_clock(void)
{
	u32 ret;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	setbits_le32((u32)P_HHI_MALI_CLK_CNTL, 1 << 8);
	ret = readl((u32 *)P_HHI_MALI_CLK_CNTL) & (1 << 8);
	spin_unlock_irqrestore(&lock, flags);
}

u32 get_mali_freq(u32 idx)
{
	return pmali_plat->clk_sample[idx];
}

void set_str_src(u32 data)
{
	if (data == 11) {
		writel(0x0004d000, (u32*)P_HHI_MPLL_CNTL9);
	} else if (data > 11) {
		writel(data, (u32*)P_HHI_MPLL_CNTL9);
	}
	if (data == 0) {
		pmali_plat->clk[pmali_plat->clk_len - 1] = mali_extr_backup;
		pmali_plat->clk_sample[pmali_plat->clk_len - 1] = mali_extr_sample_backup;
	} else if (data > 10) {
		pmali_plat->clk_sample[pmali_plat->clk_len - 1] = 600;
		pmali_plat->clk[pmali_plat->clk_len - 1] = FCLK_MPLL2;
	}
}
#endif
