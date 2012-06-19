/*
 * U300 clock implementation
 * Copyright (C) 2007-2012 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <mach/syscon.h>

/*
 * The clocking hierarchy currently looks like this.
 * NOTE: the idea is NOT to show how the clocks are routed on the chip!
 * The ideas is to show dependencies, so a clock higher up in the
 * hierarchy has to be on in order for another clock to be on. Now,
 * both CPU and DMA can actually be on top of the hierarchy, and that
 * is not modeled currently. Instead we have the backbone AMBA bus on
 * top. This bus cannot be programmed in any way but conceptually it
 * needs to be active for the bridges and devices to transport data.
 *
 * Please be aware that a few clocks are hw controlled, which mean that
 * the hw itself can turn on/off or change the rate of the clock when
 * needed!
 *
 *  AMBA bus
 *  |
 *  +- CPU
 *  +- FSMC NANDIF NAND Flash interface
 *  +- SEMI Shared Memory interface
 *  +- ISP Image Signal Processor (U335 only)
 *  +- CDS (U335 only)
 *  +- DMA Direct Memory Access Controller
 *  +- AAIF APP/ACC Inteface (Mobile Scalable Link, MSL)
 *  +- APEX
 *  +- VIDEO_ENC AVE2/3 Video Encoder
 *  +- XGAM Graphics Accelerator Controller
 *  +- AHB
 *  |
 *  +- ahb:0 AHB Bridge
 *  |  |
 *  |  +- ahb:1 INTCON Interrupt controller
 *  |  +- ahb:3 MSPRO  Memory Stick Pro controller
 *  |  +- ahb:4 EMIF   External Memory interface
 *  |
 *  +- fast:0 FAST bridge
 *  |  |
 *  |  +- fast:1 MMCSD MMC/SD card reader controller
 *  |  +- fast:2 I2S0  PCM I2S channel 0 controller
 *  |  +- fast:3 I2S1  PCM I2S channel 1 controller
 *  |  +- fast:4 I2C0  I2C channel 0 controller
 *  |  +- fast:5 I2C1  I2C channel 1 controller
 *  |  +- fast:6 SPI   SPI controller
 *  |  +- fast:7 UART1 Secondary UART (U335 only)
 *  |
 *  +- slow:0 SLOW bridge
 *     |
 *     +- slow:1 SYSCON (not possible to control)
 *     +- slow:2 WDOG Watchdog
 *     +- slow:3 UART0 primary UART
 *     +- slow:4 TIMER_APP Application timer - used in Linux
 *     +- slow:5 KEYPAD controller
 *     +- slow:6 GPIO controller
 *     +- slow:7 RTC controller
 *     +- slow:8 BT Bus Tracer (not used currently)
 *     +- slow:9 EH Event Handler (not used currently)
 *     +- slow:a TIMER_ACC Access style timer (not used currently)
 *     +- slow:b PPM (U335 only, what is that?)
 */

/* Global syscon virtual base */
static void __iomem *syscon_vbase;

/**
 * struct clk_syscon - U300 syscon clock
 * @hw: corresponding clock hardware entry
 * @hw_ctrld: whether this clock is hardware controlled (for refcount etc)
 *	and does not need any magic pokes to be enabled/disabled
 * @reset: state holder, whether this block's reset line is asserted or not
 * @res_reg: reset line enable/disable flag register
 * @res_bit: bit for resetting or taking this consumer out of reset
 * @en_reg: clock line enable/disable flag register
 * @en_bit: bit for enabling/disabling this consumer clock line
 * @clk_val: magic value to poke in the register to enable/disable
 *	this one clock
 */
struct clk_syscon {
	struct clk_hw hw;
	bool hw_ctrld;
	bool reset;
	void __iomem *res_reg;
	u8 res_bit;
	void __iomem *en_reg;
	u8 en_bit;
	u16 clk_val;
};

#define to_syscon(_hw) container_of(_hw, struct clk_syscon, hw)

static DEFINE_SPINLOCK(syscon_resetreg_lock);

/*
 * Reset control functions. We remember if a block has been
 * taken out of reset and don't remove the reset assertion again
 * and vice versa. Currently we only remove resets so the
 * enablement function is defined out.
 */
static void syscon_block_reset_enable(struct clk_syscon *sclk)
{
	unsigned long iflags;
	u16 val;

	/* Not all blocks support resetting */
	if (!sclk->res_reg)
		return;
	spin_lock_irqsave(&syscon_resetreg_lock, iflags);
	val = readw(sclk->res_reg);
	val |= BIT(sclk->res_bit);
	writew(val, sclk->res_reg);
	spin_unlock_irqrestore(&syscon_resetreg_lock, iflags);
	sclk->reset = true;
}

static void syscon_block_reset_disable(struct clk_syscon *sclk)
{
	unsigned long iflags;
	u16 val;

	/* Not all blocks support resetting */
	if (!sclk->res_reg)
		return;
	spin_lock_irqsave(&syscon_resetreg_lock, iflags);
	val = readw(sclk->res_reg);
	val &= ~BIT(sclk->res_bit);
	writew(val, sclk->res_reg);
	spin_unlock_irqrestore(&syscon_resetreg_lock, iflags);
	sclk->reset = false;
}

static int syscon_clk_prepare(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* If the block is in reset, bring it out */
	if (sclk->reset)
		syscon_block_reset_disable(sclk);
	return 0;
}

static void syscon_clk_unprepare(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Please don't force the console into reset */
	if (sclk->clk_val == U300_SYSCON_SBCER_UART_CLK_EN)
		return;
	/* When unpreparing, force block into reset */
	if (!sclk->reset)
		syscon_block_reset_enable(sclk);
}

static int syscon_clk_enable(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Don't touch the hardware controlled clocks */
	if (sclk->hw_ctrld)
		return 0;
	/* These cannot be controlled */
	if (sclk->clk_val == 0xFFFFU)
		return 0;

	writew(sclk->clk_val, syscon_vbase + U300_SYSCON_SBCER);
	return 0;
}

static void syscon_clk_disable(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Don't touch the hardware controlled clocks */
	if (sclk->hw_ctrld)
		return;
	if (sclk->clk_val == 0xFFFFU)
		return;
	/* Please don't disable the console port */
	if (sclk->clk_val == U300_SYSCON_SBCER_UART_CLK_EN)
		return;

	writew(sclk->clk_val, syscon_vbase + U300_SYSCON_SBCDR);
}

static int syscon_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 val;

	/* If no enable register defined, it's always-on */
	if (!sclk->en_reg)
		return 1;

	val = readw(sclk->en_reg);
	val &= BIT(sclk->en_bit);

	return val ? 1 : 0;
}

static u16 syscon_get_perf(void)
{
	u16 val;

	val = readw(syscon_vbase + U300_SYSCON_CCR);
	val &= U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK;
	return val;
}

static unsigned long
syscon_clk_recalc_rate(struct clk_hw *hw,
		       unsigned long parent_rate)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 perf = syscon_get_perf();

	switch(sclk->clk_val) {
	case U300_SYSCON_SBCER_FAST_BRIDGE_CLK_EN:
	case U300_SYSCON_SBCER_I2C0_CLK_EN:
	case U300_SYSCON_SBCER_I2C1_CLK_EN:
	case U300_SYSCON_SBCER_MMC_CLK_EN:
	case U300_SYSCON_SBCER_SPI_CLK_EN:
		/* The FAST clocks have one progression */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		default:
			return parent_rate; /* 26 MHz */
		}
	case U300_SYSCON_SBCER_DMAC_CLK_EN:
	case U300_SYSCON_SBCER_NANDIF_CLK_EN:
	case U300_SYSCON_SBCER_XGAM_CLK_EN:
		/* AMBA interconnect peripherals */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 6500000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 26000000;
		default:
			return parent_rate; /* 52 MHz */
		}
	case U300_SYSCON_SBCER_SEMI_CLK_EN:
	case U300_SYSCON_SBCER_EMIF_CLK_EN:
		/* EMIF speeds */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 52000000;
		default:
			return 104000000;
		}
	case U300_SYSCON_SBCER_CPU_CLK_EN:
		/* And the fast CPU clock */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 52000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
			return 104000000;
		default:
			return parent_rate; /* 208 MHz */
		}
	default:
		/*
		 * The SLOW clocks and default just inherit the rate of
		 * their parent (typically PLL13 13 MHz).
		 */
		return parent_rate;
	}
}

static long
syscon_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		      unsigned long *prate)
{
	struct clk_syscon *sclk = to_syscon(hw);

	if (sclk->clk_val != U300_SYSCON_SBCER_CPU_CLK_EN)
		return *prate;
	/* We really only support setting the rate of the CPU clock */
	if (rate <= 13000000)
		return 13000000;
	if (rate <= 52000000)
		return 52000000;
	if (rate <= 104000000)
		return 104000000;
	return 208000000;
}

static int syscon_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 val;

	/* We only support setting the rate of the CPU clock */
	if (sclk->clk_val != U300_SYSCON_SBCER_CPU_CLK_EN)
		return -EINVAL;
	switch (rate) {
	case 13000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER;
		break;
	case 52000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE;
		break;
	case 104000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH;
		break;
	case 208000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST;
		break;
	default:
		return -EINVAL;
	}
	val |= readw(syscon_vbase + U300_SYSCON_CCR) &
		~U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK ;
	writew(val, syscon_vbase + U300_SYSCON_CCR);
	return 0;
}

static const struct clk_ops syscon_clk_ops = {
	.prepare = syscon_clk_prepare,
	.unprepare = syscon_clk_unprepare,
	.enable = syscon_clk_enable,
	.disable = syscon_clk_disable,
	.is_enabled = syscon_clk_is_enabled,
	.recalc_rate = syscon_clk_recalc_rate,
	.round_rate = syscon_clk_round_rate,
	.set_rate = syscon_clk_set_rate,
};

static struct clk * __init
syscon_clk_register(struct device *dev, const char *name,
		    const char *parent_name, unsigned long flags,
		    bool hw_ctrld,
		    void __iomem *res_reg, u8 res_bit,
		    void __iomem *en_reg, u8 en_bit,
		    u16 clk_val)
{
	struct clk *clk;
	struct clk_syscon *sclk;
	struct clk_init_data init;

	sclk = kzalloc(sizeof(struct clk_syscon), GFP_KERNEL);
	if (!sclk) {
		pr_err("could not allocate syscon clock %s\n",
			name);
		return ERR_PTR(-ENOMEM);
	}
	init.name = name;
	init.ops = &syscon_clk_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	sclk->hw.init = &init;
	sclk->hw_ctrld = hw_ctrld;
	/* Assume the block is in reset at registration */
	sclk->reset = true;
	sclk->res_reg = res_reg;
	sclk->res_bit = res_bit;
	sclk->en_reg = en_reg;
	sclk->en_bit = en_bit;
	sclk->clk_val = clk_val;

	clk = clk_register(dev, &sclk->hw);
	if (IS_ERR(clk))
		kfree(sclk);

	return clk;
}

/**
 * struct clk_mclk - U300 MCLK clock (MMC/SD clock)
 * @hw: corresponding clock hardware entry
 * @is_mspro: if this is the memory stick clock rather than MMC/SD
 */
struct clk_mclk {
	struct clk_hw hw;
	bool is_mspro;
};

#define to_mclk(_hw) container_of(_hw, struct clk_mclk, hw)

static int mclk_clk_prepare(struct clk_hw *hw)
{
	struct clk_mclk *mclk = to_mclk(hw);
	u16 val;

	/* The MMC and MSPRO clocks need some special set-up */
	if (!mclk->is_mspro) {
		/* Set default MMC clock divisor to 18.9 MHz */
		writew(0x0054U, syscon_vbase + U300_SYSCON_MMF0R);
		val = readw(syscon_vbase + U300_SYSCON_MMCR);
		/* Disable the MMC feedback clock */
		val &= ~U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE;
		/* Disable MSPRO frequency */
		val &= ~U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE;
		writew(val, syscon_vbase + U300_SYSCON_MMCR);
	} else {
		val = readw(syscon_vbase + U300_SYSCON_MMCR);
		/* Disable the MMC feedback clock */
		val &= ~U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE;
		/* Enable MSPRO frequency */
		val |= U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE;
		writew(val, syscon_vbase + U300_SYSCON_MMCR);
	}

	return 0;
}

static unsigned long
mclk_clk_recalc_rate(struct clk_hw *hw,
		     unsigned long parent_rate)
{
	u16 perf = syscon_get_perf();

	switch (perf) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		/*
		 * Here, the 208 MHz PLL gets shut down and the always
		 * on 13 MHz PLL used for RTC etc kicks into use
		 * instead.
		 */
		return 13000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
	{
		/*
		 * This clock is under program control. The register is
		 * divided in two nybbles, bit 7-4 gives cycles-1 to count
		 * high, bit 3-0 gives cycles-1 to count low. Distribute
		 * these with no more than 1 cycle difference between
		 * low and high and add low and high to get the actual
		 * divisor. The base PLL is 208 MHz. Writing 0x00 will
		 * divide by 1 and 1 so the highest frequency possible
		 * is 104 MHz.
		 *
		 * e.g. 0x54 =>
		 * f = 208 / ((5+1) + (4+1)) = 208 / 11 = 18.9 MHz
		 */
		u16 val = readw(syscon_vbase + U300_SYSCON_MMF0R) &
			U300_SYSCON_MMF0R_MASK;
		switch (val) {
		case 0x0054:
			return 18900000;
		case 0x0044:
			return 20800000;
		case 0x0043:
			return 23100000;
		case 0x0033:
			return 26000000;
		case 0x0032:
			return 29700000;
		case 0x0022:
			return 34700000;
		case 0x0021:
			return 41600000;
		case 0x0011:
			return 52000000;
		case 0x0000:
			return 104000000;
		default:
			break;
		}
	}
	default:
		break;
	}
	return parent_rate;
}

static long
mclk_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		    unsigned long *prate)
{
	if (rate <= 18900000)
		return 18900000;
	if (rate <= 20800000)
		return 20800000;
	if (rate <= 23100000)
		return 23100000;
	if (rate <= 26000000)
		return 26000000;
	if (rate <= 29700000)
		return 29700000;
	if (rate <= 34700000)
		return 34700000;
	if (rate <= 41600000)
		return 41600000;
	/* Highest rate */
	return 52000000;
}

static int mclk_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	u16 val;
	u16 reg;

	switch (rate) {
	case 18900000:
		val = 0x0054;
		break;
	case 20800000:
		val = 0x0044;
		break;
	case 23100000:
		val = 0x0043;
		break;
	case 26000000:
		val = 0x0033;
		break;
	case 29700000:
		val = 0x0032;
		break;
	case 34700000:
		val = 0x0022;
		break;
	case 41600000:
		val = 0x0021;
		break;
	case 52000000:
		val = 0x0011;
		break;
	case 104000000:
		val = 0x0000;
		break;
	default:
		return -EINVAL;
	}

	reg = readw(syscon_vbase + U300_SYSCON_MMF0R) &
		~U300_SYSCON_MMF0R_MASK;
	writew(reg | val, syscon_vbase + U300_SYSCON_MMF0R);
	return 0;
}

static const struct clk_ops mclk_ops = {
	.prepare = mclk_clk_prepare,
	.recalc_rate = mclk_clk_recalc_rate,
	.round_rate = mclk_clk_round_rate,
	.set_rate = mclk_clk_set_rate,
};

static struct clk * __init
mclk_clk_register(struct device *dev, const char *name,
		  const char *parent_name, bool is_mspro)
{
	struct clk *clk;
	struct clk_mclk *mclk;
	struct clk_init_data init;

	mclk = kzalloc(sizeof(struct clk_mclk), GFP_KERNEL);
	if (!mclk) {
		pr_err("could not allocate MMC/SD clock %s\n",
		       name);
		return ERR_PTR(-ENOMEM);
	}
	init.name = "mclk";
	init.ops = &mclk_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	mclk->hw.init = &init;
	mclk->is_mspro = is_mspro;

	clk = clk_register(dev, &mclk->hw);
	if (IS_ERR(clk))
		kfree(mclk);

	return clk;
}

void __init u300_clk_init(void __iomem *base)
{
	u16 val;
	struct clk *clk;

	syscon_vbase = base;

	/* Set system to run at PLL208, max performance, a known state. */
	val = readw(syscon_vbase + U300_SYSCON_CCR);
	val &= ~U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK;
	writew(val, syscon_vbase + U300_SYSCON_CCR);
	/* Wait for the PLL208 to lock if not locked in yet */
	while (!(readw(syscon_vbase + U300_SYSCON_CSR) &
		 U300_SYSCON_CSR_PLL208_LOCK_IND));

	/* Power management enable */
	val = readw(syscon_vbase + U300_SYSCON_PMCR);
	val |= U300_SYSCON_PMCR_PWR_MGNT_ENABLE;
	writew(val, syscon_vbase + U300_SYSCON_PMCR);

	/* These are always available (RTC and PLL13) */
	clk = clk_register_fixed_rate(NULL, "app_32_clk", NULL,
				      CLK_IS_ROOT, 32768);
	/* The watchdog sits directly on the 32 kHz clock */
	clk_register_clkdev(clk, NULL, "coh901327_wdog");
	clk = clk_register_fixed_rate(NULL, "pll13", NULL,
				      CLK_IS_ROOT, 13000000);

	/* These derive from PLL208 */
	clk = clk_register_fixed_rate(NULL, "pll208", NULL,
				      CLK_IS_ROOT, 208000000);
	clk = clk_register_fixed_factor(NULL, "app_208_clk", "pll208",
					0, 1, 1);
	clk = clk_register_fixed_factor(NULL, "app_104_clk", "pll208",
					0, 1, 2);
	clk = clk_register_fixed_factor(NULL, "app_52_clk", "pll208",
					0, 1, 4);
	/* The 52 MHz is divided down to 26 MHz */
	clk = clk_register_fixed_factor(NULL, "app_26_clk", "app_52_clk",
					0, 1, 2);

	/* Directly on the AMBA interconnect */
	clk = syscon_clk_register(NULL, "cpu_clk", "app_208_clk", 0, true,
				  syscon_vbase + U300_SYSCON_RRR, 3,
				  syscon_vbase + U300_SYSCON_CERR, 3,
				  U300_SYSCON_SBCER_CPU_CLK_EN);
	clk = syscon_clk_register(NULL, "dmac_clk", "app_52_clk", 0, true,
				  syscon_vbase + U300_SYSCON_RRR, 4,
				  syscon_vbase + U300_SYSCON_CERR, 4,
				  U300_SYSCON_SBCER_DMAC_CLK_EN);
	clk_register_clkdev(clk, NULL, "dma");
	clk = syscon_clk_register(NULL, "fsmc_clk", "app_52_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RRR, 6,
				  syscon_vbase + U300_SYSCON_CERR, 6,
				  U300_SYSCON_SBCER_NANDIF_CLK_EN);
	clk_register_clkdev(clk, NULL, "fsmc-nand");
	clk = syscon_clk_register(NULL, "xgam_clk", "app_52_clk", 0, true,
				  syscon_vbase + U300_SYSCON_RRR, 8,
				  syscon_vbase + U300_SYSCON_CERR, 8,
				  U300_SYSCON_SBCER_XGAM_CLK_EN);
	clk_register_clkdev(clk, NULL, "xgam");
	clk = syscon_clk_register(NULL, "semi_clk", "app_104_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RRR, 9,
				  syscon_vbase + U300_SYSCON_CERR, 9,
				  U300_SYSCON_SBCER_SEMI_CLK_EN);
	clk_register_clkdev(clk, NULL, "semi");

	/* AHB bridge clocks */
	clk = syscon_clk_register(NULL, "ahb_subsys_clk", "app_52_clk", 0, true,
				  syscon_vbase + U300_SYSCON_RRR, 10,
				  syscon_vbase + U300_SYSCON_CERR, 10,
				  U300_SYSCON_SBCER_AHB_SUBSYS_BRIDGE_CLK_EN);
	clk = syscon_clk_register(NULL, "intcon_clk", "ahb_subsys_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RRR, 12,
				  syscon_vbase + U300_SYSCON_CERR, 12,
				  /* Cannot be enabled, just taken out of reset */
				  0xFFFFU);
	clk_register_clkdev(clk, NULL, "intcon");
	clk = syscon_clk_register(NULL, "emif_clk", "ahb_subsys_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RRR, 5,
				  syscon_vbase + U300_SYSCON_CERR, 5,
				  U300_SYSCON_SBCER_EMIF_CLK_EN);
	clk_register_clkdev(clk, NULL, "pl172");

	/* FAST bridge clocks */
	clk = syscon_clk_register(NULL, "fast_clk", "app_26_clk", 0, true,
				  syscon_vbase + U300_SYSCON_RFR, 0,
				  syscon_vbase + U300_SYSCON_CEFR, 0,
				  U300_SYSCON_SBCER_FAST_BRIDGE_CLK_EN);
	clk = syscon_clk_register(NULL, "i2c0_p_clk", "fast_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RFR, 1,
				  syscon_vbase + U300_SYSCON_CEFR, 1,
				  U300_SYSCON_SBCER_I2C0_CLK_EN);
	clk_register_clkdev(clk, NULL, "stu300.0");
	clk = syscon_clk_register(NULL, "i2c1_p_clk", "fast_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RFR, 2,
				  syscon_vbase + U300_SYSCON_CEFR, 2,
				  U300_SYSCON_SBCER_I2C1_CLK_EN);
	clk_register_clkdev(clk, NULL, "stu300.1");
	clk = syscon_clk_register(NULL, "mmc_p_clk", "fast_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RFR, 5,
				  syscon_vbase + U300_SYSCON_CEFR, 5,
				  U300_SYSCON_SBCER_MMC_CLK_EN);
	clk_register_clkdev(clk, "apb_pclk", "mmci");
	clk = syscon_clk_register(NULL, "spi_p_clk", "fast_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RFR, 6,
				  syscon_vbase + U300_SYSCON_CEFR, 6,
				  U300_SYSCON_SBCER_SPI_CLK_EN);
	/* The SPI has no external clock for the outward bus, uses the pclk */
	clk_register_clkdev(clk, NULL, "pl022");
	clk_register_clkdev(clk, "apb_pclk", "pl022");

	/* SLOW bridge clocks */
	clk = syscon_clk_register(NULL, "slow_clk", "pll13", 0, true,
				  syscon_vbase + U300_SYSCON_RSR, 0,
				  syscon_vbase + U300_SYSCON_CESR, 0,
				  U300_SYSCON_SBCER_SLOW_BRIDGE_CLK_EN);
	clk = syscon_clk_register(NULL, "uart0_clk", "slow_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RSR, 1,
				  syscon_vbase + U300_SYSCON_CESR, 1,
				  U300_SYSCON_SBCER_UART_CLK_EN);
	/* Same clock is used for APB and outward bus */
	clk_register_clkdev(clk, NULL, "uart0");
	clk_register_clkdev(clk, "apb_pclk", "uart0");
	clk = syscon_clk_register(NULL, "gpio_clk", "slow_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RSR, 4,
				  syscon_vbase + U300_SYSCON_CESR, 4,
				  U300_SYSCON_SBCER_GPIO_CLK_EN);
	clk_register_clkdev(clk, NULL, "u300-gpio");
	clk = syscon_clk_register(NULL, "keypad_clk", "slow_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RSR, 5,
				  syscon_vbase + U300_SYSCON_CESR, 6,
				  U300_SYSCON_SBCER_KEYPAD_CLK_EN);
	clk_register_clkdev(clk, NULL, "coh901461-keypad");
	clk = syscon_clk_register(NULL, "rtc_clk", "slow_clk", 0, true,
				  syscon_vbase + U300_SYSCON_RSR, 6,
				  /* No clock enable register bit */
				  NULL, 0, 0xFFFFU);
	clk_register_clkdev(clk, NULL, "rtc-coh901331");
	clk = syscon_clk_register(NULL, "app_tmr_clk", "slow_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RSR, 7,
				  syscon_vbase + U300_SYSCON_CESR, 7,
				  U300_SYSCON_SBCER_APP_TMR_CLK_EN);
	clk_register_clkdev(clk, NULL, "apptimer");
	clk = syscon_clk_register(NULL, "acc_tmr_clk", "slow_clk", 0, false,
				  syscon_vbase + U300_SYSCON_RSR, 8,
				  syscon_vbase + U300_SYSCON_CESR, 8,
				  U300_SYSCON_SBCER_ACC_TMR_CLK_EN);
	clk_register_clkdev(clk, NULL, "timer");

	/* Then this special MMC/SD clock */
	clk = mclk_clk_register(NULL, "mmc_clk", "mmc_p_clk", false);
	clk_register_clkdev(clk, NULL, "mmci");
}
