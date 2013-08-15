/*
 * Nomadik clock implementation
 * Copyright (C) 2013 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */

#define pr_fmt(fmt) "Nomadik SRC clocks: " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>

/*
 * The Nomadik clock tree is described in the STN8815A12 DB V4.2
 * reference manual for the chip, page 94 ff.
 * Clock IDs are in the STn8815 Reference Manual table 3, page 27.
 */

#define SRC_CR			0x00U
#define SRC_XTALCR		0x0CU
#define SRC_XTALCR_XTALTIMEN	BIT(20)
#define SRC_XTALCR_SXTALDIS	BIT(19)
#define SRC_XTALCR_MXTALSTAT	BIT(2)
#define SRC_XTALCR_MXTALEN	BIT(1)
#define SRC_XTALCR_MXTALOVER	BIT(0)
#define SRC_PLLCR		0x10U
#define SRC_PLLCR_PLLTIMEN	BIT(29)
#define SRC_PLLCR_PLL2EN	BIT(28)
#define SRC_PLLCR_PLL1STAT	BIT(2)
#define SRC_PLLCR_PLL1EN	BIT(1)
#define SRC_PLLCR_PLL1OVER	BIT(0)
#define SRC_PLLFR		0x14U
#define SRC_PCKEN0		0x24U
#define SRC_PCKDIS0		0x28U
#define SRC_PCKENSR0		0x2CU
#define SRC_PCKSR0		0x30U
#define SRC_PCKEN1		0x34U
#define SRC_PCKDIS1		0x38U
#define SRC_PCKENSR1		0x3CU
#define SRC_PCKSR1		0x40U

/* Lock protecting the SRC_CR register */
static DEFINE_SPINLOCK(src_lock);
/* Base address of the SRC */
static void __iomem *src_base;

/**
 * struct clk_pll1 - Nomadik PLL1 clock
 * @hw: corresponding clock hardware entry
 * @id: PLL instance: 1 or 2
 */
struct clk_pll {
	struct clk_hw hw;
	int id;
};

/**
 * struct clk_src - Nomadik src clock
 * @hw: corresponding clock hardware entry
 * @id: the clock ID
 * @group1: true if the clock is in group1, else it is in group0
 * @clkbit: bit 0...31 corresponding to the clock in each clock register
 */
struct clk_src {
	struct clk_hw hw;
	int id;
	bool group1;
	u32 clkbit;
};

#define to_pll(_hw) container_of(_hw, struct clk_pll, hw)
#define to_src(_hw) container_of(_hw, struct clk_src, hw)

static int pll_clk_enable(struct clk_hw *hw)
{
	struct clk_pll *pll = to_pll(hw);
	u32 val;

	spin_lock(&src_lock);
	val = readl(src_base + SRC_PLLCR);
	if (pll->id == 1) {
		if (val & SRC_PLLCR_PLL1OVER) {
			val |= SRC_PLLCR_PLL1EN;
			writel(val, src_base + SRC_PLLCR);
		}
	} else if (pll->id == 2) {
		val |= SRC_PLLCR_PLL2EN;
		writel(val, src_base + SRC_PLLCR);
	}
	spin_unlock(&src_lock);
	return 0;
}

static void pll_clk_disable(struct clk_hw *hw)
{
	struct clk_pll *pll = to_pll(hw);
	u32 val;

	spin_lock(&src_lock);
	val = readl(src_base + SRC_PLLCR);
	if (pll->id == 1) {
		if (val & SRC_PLLCR_PLL1OVER) {
			val &= ~SRC_PLLCR_PLL1EN;
			writel(val, src_base + SRC_PLLCR);
		}
	} else if (pll->id == 2) {
		val &= ~SRC_PLLCR_PLL2EN;
		writel(val, src_base + SRC_PLLCR);
	}
	spin_unlock(&src_lock);
}

static int pll_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_pll *pll = to_pll(hw);
	u32 val;

	val = readl(src_base + SRC_PLLCR);
	if (pll->id == 1) {
		if (val & SRC_PLLCR_PLL1OVER)
			return !!(val & SRC_PLLCR_PLL1EN);
	} else if (pll->id == 2) {
		return !!(val & SRC_PLLCR_PLL2EN);
	}
	return 1;
}

static unsigned long pll_clk_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_pll *pll = to_pll(hw);
	u32 val;

	val = readl(src_base + SRC_PLLFR);

	if (pll->id == 1) {
		u8 mul;
		u8 div;

		mul = (val >> 8) & 0x3FU;
		mul += 2;
		div = val & 0x07U;
		return (parent_rate * mul) >> div;
	}

	if (pll->id == 2) {
		u8 mul;

		mul = (val >> 24) & 0x3FU;
		mul += 2;
		return (parent_rate * mul);
	}

	/* Unknown PLL */
	return 0;
}


static const struct clk_ops pll_clk_ops = {
	.enable = pll_clk_enable,
	.disable = pll_clk_disable,
	.is_enabled = pll_clk_is_enabled,
	.recalc_rate = pll_clk_recalc_rate,
};

static struct clk * __init
pll_clk_register(struct device *dev, const char *name,
		 const char *parent_name, u32 id)
{
	struct clk *clk;
	struct clk_pll *pll;
	struct clk_init_data init;

	if (id != 1 && id != 2) {
		pr_err("%s: the Nomadik has only PLL 1 & 2\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate PLL clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &pll_clk_ops;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	pll->hw.init = &init;
	pll->id = id;

	pr_debug("register PLL1 clock \"%s\"\n", name);

	clk = clk_register(dev, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

/*
 * The Nomadik SRC clocks are gated, but not in the sense that
 * you read-modify-write a register. Instead there are separate
 * clock enable and clock disable registers. Writing a '1' bit in
 * the enable register for a certain clock ungates that clock without
 * affecting the other clocks. The disable register works the opposite
 * way.
 */

static int src_clk_enable(struct clk_hw *hw)
{
	struct clk_src *sclk = to_src(hw);
	u32 enreg = sclk->group1 ? SRC_PCKEN1 : SRC_PCKEN0;
	u32 sreg = sclk->group1 ? SRC_PCKSR1 : SRC_PCKSR0;

	writel(sclk->clkbit, src_base + enreg);
	/* spin until enabled */
	while (!(readl(src_base + sreg) & sclk->clkbit))
		cpu_relax();
	return 0;
}

static void src_clk_disable(struct clk_hw *hw)
{
	struct clk_src *sclk = to_src(hw);
	u32 disreg = sclk->group1 ? SRC_PCKDIS1 : SRC_PCKDIS0;
	u32 sreg = sclk->group1 ? SRC_PCKSR1 : SRC_PCKSR0;

	writel(sclk->clkbit, src_base + disreg);
	/* spin until disabled */
	while (readl(src_base + sreg) & sclk->clkbit)
		cpu_relax();
}

static int src_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_src *sclk = to_src(hw);
	u32 sreg = sclk->group1 ? SRC_PCKSR1 : SRC_PCKSR0;
	u32 val = readl(src_base + sreg);

	return !!(val & sclk->clkbit);
}

static unsigned long
src_clk_recalc_rate(struct clk_hw *hw,
		    unsigned long parent_rate)
{
	return parent_rate;
}

static const struct clk_ops src_clk_ops = {
	.enable = src_clk_enable,
	.disable = src_clk_disable,
	.is_enabled = src_clk_is_enabled,
	.recalc_rate = src_clk_recalc_rate,
};

static struct clk * __init
src_clk_register(struct device *dev, const char *name,
		 const char *parent_name, u8 id)
{
	struct clk *clk;
	struct clk_src *sclk;
	struct clk_init_data init;

	sclk = kzalloc(sizeof(*sclk), GFP_KERNEL);
	if (!sclk) {
		pr_err("could not allocate SRC clock %s\n",
			name);
		return ERR_PTR(-ENOMEM);
	}
	init.name = name;
	init.ops = &src_clk_ops;
	/* Do not force-disable the static SDRAM controller */
	if (id == 2)
		init.flags = CLK_IGNORE_UNUSED;
	else
		init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	sclk->hw.init = &init;
	sclk->id = id;
	sclk->group1 = (id > 31);
	sclk->clkbit = BIT(id & 0x1f);

	pr_debug("register clock \"%s\" ID: %d group: %d bits: %08x\n",
		 name, id, sclk->group1, sclk->clkbit);

	clk = clk_register(dev, &sclk->hw);
	if (IS_ERR(clk))
		kfree(sclk);

	return clk;
}

#ifdef CONFIG_DEBUG_FS

static u32 src_pcksr0_boot;
static u32 src_pcksr1_boot;

static const char * const src_clk_names[] = {
	"HCLKDMA0  ",
	"HCLKSMC   ",
	"HCLKSDRAM ",
	"HCLKDMA1  ",
	"HCLKCLCD  ",
	"PCLKIRDA  ",
	"PCLKSSP   ",
	"PCLKUART0 ",
	"PCLKSDI   ",
	"PCLKI2C0  ",
	"PCLKI2C1  ",
	"PCLKUART1 ",
	"PCLMSP0   ",
	"HCLKUSB   ",
	"HCLKDIF   ",
	"HCLKSAA   ",
	"HCLKSVA   ",
	"PCLKHSI   ",
	"PCLKXTI   ",
	"PCLKUART2 ",
	"PCLKMSP1  ",
	"PCLKMSP2  ",
	"PCLKOWM   ",
	"HCLKHPI   ",
	"PCLKSKE   ",
	"PCLKHSEM  ",
	"HCLK3D    ",
	"HCLKHASH  ",
	"HCLKCRYP  ",
	"PCLKMSHC  ",
	"HCLKUSBM  ",
	"HCLKRNG   ",
	"RESERVED  ",
	"RESERVED  ",
	"RESERVED  ",
	"RESERVED  ",
	"CLDCLK    ",
	"IRDACLK   ",
	"SSPICLK   ",
	"UART0CLK  ",
	"SDICLK    ",
	"I2C0CLK   ",
	"I2C1CLK   ",
	"UART1CLK  ",
	"MSPCLK0   ",
	"USBCLK    ",
	"DIFCLK    ",
	"IPI2CCLK  ",
	"IPBMCCLK  ",
	"HSICLKRX  ",
	"HSICLKTX  ",
	"UART2CLK  ",
	"MSPCLK1   ",
	"MSPCLK2   ",
	"OWMCLK    ",
	"RESERVED  ",
	"SKECLK    ",
	"RESERVED  ",
	"3DCLK     ",
	"PCLKMSP3  ",
	"MSPCLK3   ",
	"MSHCCLK   ",
	"USBMCLK   ",
	"RNGCCLK   ",
};

static int nomadik_src_clk_show(struct seq_file *s, void *what)
{
	int i;
	u32 src_pcksr0 = readl(src_base + SRC_PCKSR0);
	u32 src_pcksr1 = readl(src_base + SRC_PCKSR1);
	u32 src_pckensr0 = readl(src_base + SRC_PCKENSR0);
	u32 src_pckensr1 = readl(src_base + SRC_PCKENSR1);

	seq_printf(s, "Clock:      Boot:   Now:    Request: ASKED:\n");
	for (i = 0; i < ARRAY_SIZE(src_clk_names); i++) {
		u32 pcksrb = (i < 0x20) ? src_pcksr0_boot : src_pcksr1_boot;
		u32 pcksr = (i < 0x20) ? src_pcksr0 : src_pcksr1;
		u32 pckreq = (i < 0x20) ? src_pckensr0 : src_pckensr1;
		u32 mask = BIT(i & 0x1f);

		seq_printf(s, "%s  %s     %s     %s\n",
			   src_clk_names[i],
			   (pcksrb & mask) ? "on " : "off",
			   (pcksr & mask) ? "on " : "off",
			   (pckreq & mask) ? "on " : "off");
	}
	return 0;
}

static int nomadik_src_clk_open(struct inode *inode, struct file *file)
{
	return single_open(file, nomadik_src_clk_show, NULL);
}

static const struct file_operations nomadik_src_clk_debugfs_ops = {
	.open           = nomadik_src_clk_open,
	.read           = seq_read,
        .llseek         = seq_lseek,
	.release        = single_release,
};

static int __init nomadik_src_clk_init_debugfs(void)
{
	src_pcksr0_boot = readl(src_base + SRC_PCKSR0);
	src_pcksr1_boot = readl(src_base + SRC_PCKSR1);
	debugfs_create_file("nomadik-src-clk", S_IFREG | S_IRUGO,
			    NULL, NULL, &nomadik_src_clk_debugfs_ops);
	return 0;
}

module_init(nomadik_src_clk_init_debugfs);

#endif

static void __init of_nomadik_pll_setup(struct device_node *np)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
	const char *parent_name;
	u32 pll_id;

	if (of_property_read_u32(np, "pll-id", &pll_id)) {
		pr_err("%s: PLL \"%s\" missing pll-id property\n",
			__func__, clk_name);
		return;
	}
	parent_name = of_clk_get_parent_name(np, 0);
	clk = pll_clk_register(NULL, clk_name, parent_name, pll_id);
	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static void __init of_nomadik_hclk_setup(struct device_node *np)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
	const char *parent_name;

	parent_name = of_clk_get_parent_name(np, 0);
	/*
	 * The HCLK divides PLL1 with 1 (passthru), 2, 3 or 4.
	 */
	clk = clk_register_divider(NULL, clk_name, parent_name,
			   0, src_base + SRC_CR,
			   13, 2,
			   CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			   &src_lock);
	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static void __init of_nomadik_src_clk_setup(struct device_node *np)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
	const char *parent_name;
	u32 clk_id;

	if (of_property_read_u32(np, "clock-id", &clk_id)) {
		pr_err("%s: SRC clock \"%s\" missing clock-id property\n",
			__func__, clk_name);
		return;
	}
	parent_name = of_clk_get_parent_name(np, 0);
	clk = src_clk_register(NULL, clk_name, parent_name, clk_id);
	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static const __initconst struct of_device_id nomadik_src_match[] = {
	{ .compatible = "stericsson,nomadik-src" },
	{ /* sentinel */ }
};

static const __initconst struct of_device_id nomadik_src_clk_match[] = {
	{
		.compatible = "fixed-clock",
		.data = of_fixed_clk_setup,
	},
	{
		.compatible = "fixed-factor-clock",
		.data = of_fixed_factor_clk_setup,
	},
	{
		.compatible = "st,nomadik-pll-clock",
		.data = of_nomadik_pll_setup,
	},
	{
		.compatible = "st,nomadik-hclk-clock",
		.data = of_nomadik_hclk_setup,
	},
	{
		.compatible = "st,nomadik-src-clock",
		.data = of_nomadik_src_clk_setup,
	},
	{ /* sentinel */ }
};

static int nomadik_clk_reboot_handler(struct notifier_block *this,
				unsigned long code,
				void *unused)
{
	u32 val;

	/* The main chrystal need to be enabled for reboot to work */
	val = readl(src_base + SRC_XTALCR);
	val &= ~SRC_XTALCR_MXTALOVER;
	val |= SRC_XTALCR_MXTALEN;
	pr_crit("force-enabling MXTALO\n");
	writel(val, src_base + SRC_XTALCR);
	return NOTIFY_OK;
}

static struct notifier_block nomadik_clk_reboot_notifier = {
	.notifier_call = nomadik_clk_reboot_handler,
};

void __init nomadik_clk_init(void)
{
	struct device_node *np;
	u32 val;

	np = of_find_matching_node(NULL, nomadik_src_match);
	if (!np) {
		pr_crit("no matching node for SRC, aborting clock init\n");
		return;
	}
	src_base = of_iomap(np, 0);
	if (!src_base) {
		pr_err("%s: must have src parent node with REGS (%s)\n",
		       __func__, np->name);
		return;
	}
	val = readl(src_base + SRC_XTALCR);
	pr_info("SXTALO is %s\n",
		(val & SRC_XTALCR_SXTALDIS) ? "disabled" : "enabled");
	pr_info("MXTAL is %s\n",
		(val & SRC_XTALCR_MXTALSTAT) ? "enabled" : "disabled");
	if (of_property_read_bool(np, "disable-sxtalo")) {
		/* The machine uses an external oscillator circuit */
		val |= SRC_XTALCR_SXTALDIS;
		pr_info("disabling SXTALO\n");
	}
	if (of_property_read_bool(np, "disable-mxtalo")) {
		/* Disable this too: also run by external oscillator */
		val |= SRC_XTALCR_MXTALOVER;
		val &= ~SRC_XTALCR_MXTALEN;
		pr_info("disabling MXTALO\n");
	}
	writel(val, src_base + SRC_XTALCR);
	register_reboot_notifier(&nomadik_clk_reboot_notifier);

	of_clk_init(nomadik_src_clk_match);
}
