// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * CPU frequency scaling for S5PC110/S5PV210
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>

static void __iomem *clk_base;
static void __iomem *dmc_base[2];

#define S5P_CLKREG(x)		(clk_base + (x))

#define S5P_APLL_LOCK		S5P_CLKREG(0x00)
#define S5P_APLL_CON		S5P_CLKREG(0x100)
#define S5P_CLK_SRC0		S5P_CLKREG(0x200)
#define S5P_CLK_SRC2		S5P_CLKREG(0x208)
#define S5P_CLK_DIV0		S5P_CLKREG(0x300)
#define S5P_CLK_DIV2		S5P_CLKREG(0x308)
#define S5P_CLK_DIV6		S5P_CLKREG(0x318)
#define S5P_CLKDIV_STAT0	S5P_CLKREG(0x1000)
#define S5P_CLKDIV_STAT1	S5P_CLKREG(0x1004)
#define S5P_CLKMUX_STAT0	S5P_CLKREG(0x1100)
#define S5P_CLKMUX_STAT1	S5P_CLKREG(0x1104)

#define S5P_ARM_MCS_CON		S5P_CLKREG(0x6100)

/* CLKSRC0 */
#define S5P_CLKSRC0_MUX200_SHIFT	(16)
#define S5P_CLKSRC0_MUX200_MASK		(0x1 << S5P_CLKSRC0_MUX200_SHIFT)
#define S5P_CLKSRC0_MUX166_MASK		(0x1<<20)
#define S5P_CLKSRC0_MUX133_MASK		(0x1<<24)

/* CLKSRC2 */
#define S5P_CLKSRC2_G3D_SHIFT           (0)
#define S5P_CLKSRC2_G3D_MASK            (0x3 << S5P_CLKSRC2_G3D_SHIFT)
#define S5P_CLKSRC2_MFC_SHIFT           (4)
#define S5P_CLKSRC2_MFC_MASK            (0x3 << S5P_CLKSRC2_MFC_SHIFT)

/* CLKDIV0 */
#define S5P_CLKDIV0_APLL_SHIFT		(0)
#define S5P_CLKDIV0_APLL_MASK		(0x7 << S5P_CLKDIV0_APLL_SHIFT)
#define S5P_CLKDIV0_A2M_SHIFT		(4)
#define S5P_CLKDIV0_A2M_MASK		(0x7 << S5P_CLKDIV0_A2M_SHIFT)
#define S5P_CLKDIV0_HCLK200_SHIFT	(8)
#define S5P_CLKDIV0_HCLK200_MASK	(0x7 << S5P_CLKDIV0_HCLK200_SHIFT)
#define S5P_CLKDIV0_PCLK100_SHIFT	(12)
#define S5P_CLKDIV0_PCLK100_MASK	(0x7 << S5P_CLKDIV0_PCLK100_SHIFT)
#define S5P_CLKDIV0_HCLK166_SHIFT	(16)
#define S5P_CLKDIV0_HCLK166_MASK	(0xF << S5P_CLKDIV0_HCLK166_SHIFT)
#define S5P_CLKDIV0_PCLK83_SHIFT	(20)
#define S5P_CLKDIV0_PCLK83_MASK		(0x7 << S5P_CLKDIV0_PCLK83_SHIFT)
#define S5P_CLKDIV0_HCLK133_SHIFT	(24)
#define S5P_CLKDIV0_HCLK133_MASK	(0xF << S5P_CLKDIV0_HCLK133_SHIFT)
#define S5P_CLKDIV0_PCLK66_SHIFT	(28)
#define S5P_CLKDIV0_PCLK66_MASK		(0x7 << S5P_CLKDIV0_PCLK66_SHIFT)

/* CLKDIV2 */
#define S5P_CLKDIV2_G3D_SHIFT           (0)
#define S5P_CLKDIV2_G3D_MASK            (0xF << S5P_CLKDIV2_G3D_SHIFT)
#define S5P_CLKDIV2_MFC_SHIFT           (4)
#define S5P_CLKDIV2_MFC_MASK            (0xF << S5P_CLKDIV2_MFC_SHIFT)

/* CLKDIV6 */
#define S5P_CLKDIV6_ONEDRAM_SHIFT       (28)
#define S5P_CLKDIV6_ONEDRAM_MASK        (0xF << S5P_CLKDIV6_ONEDRAM_SHIFT)

static struct clk *dmc0_clk;
static struct clk *dmc1_clk;
static DEFINE_MUTEX(set_freq_lock);

/* APLL M,P,S values for 1G/800Mhz */
#define APLL_VAL_1000	((1 << 31) | (125 << 16) | (3 << 8) | 1)
#define APLL_VAL_800	((1 << 31) | (100 << 16) | (3 << 8) | 1)

/* Use 800MHz when entering sleep mode */
#define SLEEP_FREQ	(800 * 1000)

/* Tracks if cpu freqency can be updated anymore */
static bool no_cpufreq_access;

/*
 * DRAM configurations to calculate refresh counter for changing
 * frequency of memory.
 */
struct dram_conf {
	unsigned long freq;	/* HZ */
	unsigned long refresh;	/* DRAM refresh counter * 1000 */
};

/* DRAM configuration (DMC0 and DMC1) */
static struct dram_conf s5pv210_dram_conf[2];

enum perf_level {
	L0, L1, L2, L3, L4,
};

enum s5pv210_mem_type {
	LPDDR	= 0x1,
	LPDDR2	= 0x2,
	DDR2	= 0x4,
};

enum s5pv210_dmc_port {
	DMC0 = 0,
	DMC1,
};

static struct cpufreq_frequency_table s5pv210_freq_table[] = {
	{0, L0, 1000*1000},
	{0, L1, 800*1000},
	{0, L2, 400*1000},
	{0, L3, 200*1000},
	{0, L4, 100*1000},
	{0, 0, CPUFREQ_TABLE_END},
};

static struct regulator *arm_regulator;
static struct regulator *int_regulator;

struct s5pv210_dvs_conf {
	int arm_volt;	/* uV */
	int int_volt;	/* uV */
};

static const int arm_volt_max = 1350000;
static const int int_volt_max = 1250000;

static struct s5pv210_dvs_conf dvs_conf[] = {
	[L0] = {
		.arm_volt	= 1250000,
		.int_volt	= 1100000,
	},
	[L1] = {
		.arm_volt	= 1200000,
		.int_volt	= 1100000,
	},
	[L2] = {
		.arm_volt	= 1050000,
		.int_volt	= 1100000,
	},
	[L3] = {
		.arm_volt	= 950000,
		.int_volt	= 1100000,
	},
	[L4] = {
		.arm_volt	= 950000,
		.int_volt	= 1000000,
	},
};

static u32 clkdiv_val[5][11] = {
	/*
	 * Clock divider value for following
	 * { APLL, A2M, HCLK_MSYS, PCLK_MSYS,
	 *   HCLK_DSYS, PCLK_DSYS, HCLK_PSYS, PCLK_PSYS,
	 *   ONEDRAM, MFC, G3D }
	 */

	/* L0 : [1000/200/100][166/83][133/66][200/200] */
	{0, 4, 4, 1, 3, 1, 4, 1, 3, 0, 0},

	/* L1 : [800/200/100][166/83][133/66][200/200] */
	{0, 3, 3, 1, 3, 1, 4, 1, 3, 0, 0},

	/* L2 : [400/200/100][166/83][133/66][200/200] */
	{1, 3, 1, 1, 3, 1, 4, 1, 3, 0, 0},

	/* L3 : [200/200/100][166/83][133/66][200/200] */
	{3, 3, 1, 1, 3, 1, 4, 1, 3, 0, 0},

	/* L4 : [100/100/100][83/83][66/66][100/100] */
	{7, 7, 0, 0, 7, 0, 9, 0, 7, 0, 0},
};

/*
 * This function set DRAM refresh counter
 * accoriding to operating frequency of DRAM
 * ch: DMC port number 0 or 1
 * freq: Operating frequency of DRAM(KHz)
 */
static void s5pv210_set_refresh(enum s5pv210_dmc_port ch, unsigned long freq)
{
	unsigned long tmp, tmp1;
	void __iomem *reg = NULL;

	if (ch == DMC0) {
		reg = (dmc_base[0] + 0x30);
	} else if (ch == DMC1) {
		reg = (dmc_base[1] + 0x30);
	} else {
		pr_err("Cannot find DMC port\n");
		return;
	}

	/* Find current DRAM frequency */
	tmp = s5pv210_dram_conf[ch].freq;

	tmp /= freq;

	tmp1 = s5pv210_dram_conf[ch].refresh;

	tmp1 /= tmp;

	writel_relaxed(tmp1, reg);
}

static int s5pv210_target(struct cpufreq_policy *policy, unsigned int index)
{
	unsigned long reg;
	unsigned int priv_index;
	unsigned int pll_changing = 0;
	unsigned int bus_speed_changing = 0;
	unsigned int old_freq, new_freq;
	int arm_volt, int_volt;
	int ret = 0;

	mutex_lock(&set_freq_lock);

	if (no_cpufreq_access) {
		pr_err("Denied access to %s as it is disabled temporarily\n",
		       __func__);
		ret = -EINVAL;
		goto exit;
	}

	old_freq = policy->cur;
	new_freq = s5pv210_freq_table[index].frequency;

	/* Finding current running level index */
	priv_index = cpufreq_table_find_index_h(policy, old_freq);

	arm_volt = dvs_conf[index].arm_volt;
	int_volt = dvs_conf[index].int_volt;

	if (new_freq > old_freq) {
		ret = regulator_set_voltage(arm_regulator,
				arm_volt, arm_volt_max);
		if (ret)
			goto exit;

		ret = regulator_set_voltage(int_regulator,
				int_volt, int_volt_max);
		if (ret)
			goto exit;
	}

	/* Check if there need to change PLL */
	if ((index == L0) || (priv_index == L0))
		pll_changing = 1;

	/* Check if there need to change System bus clock */
	if ((index == L4) || (priv_index == L4))
		bus_speed_changing = 1;

	if (bus_speed_changing) {
		/*
		 * Reconfigure DRAM refresh counter value for minimum
		 * temporary clock while changing divider.
		 * expected clock is 83Mhz : 7.8usec/(1/83Mhz) = 0x287
		 */
		if (pll_changing)
			s5pv210_set_refresh(DMC1, 83000);
		else
			s5pv210_set_refresh(DMC1, 100000);

		s5pv210_set_refresh(DMC0, 83000);
	}

	/*
	 * APLL should be changed in this level
	 * APLL -> MPLL(for stable transition) -> APLL
	 * Some clock source's clock API are not prepared.
	 * Do not use clock API in below code.
	 */
	if (pll_changing) {
		/*
		 * 1. Temporary Change divider for MFC and G3D
		 * SCLKA2M(200/1=200)->(200/4=50)Mhz
		 */
		reg = readl_relaxed(S5P_CLK_DIV2);
		reg &= ~(S5P_CLKDIV2_G3D_MASK | S5P_CLKDIV2_MFC_MASK);
		reg |= (3 << S5P_CLKDIV2_G3D_SHIFT) |
			(3 << S5P_CLKDIV2_MFC_SHIFT);
		writel_relaxed(reg, S5P_CLK_DIV2);

		/* For MFC, G3D dividing */
		do {
			reg = readl_relaxed(S5P_CLKDIV_STAT0);
		} while (reg & ((1 << 16) | (1 << 17)));

		/*
		 * 2. Change SCLKA2M(200Mhz)to SCLKMPLL in MFC_MUX, G3D MUX
		 * (200/4=50)->(667/4=166)Mhz
		 */
		reg = readl_relaxed(S5P_CLK_SRC2);
		reg &= ~(S5P_CLKSRC2_G3D_MASK | S5P_CLKSRC2_MFC_MASK);
		reg |= (1 << S5P_CLKSRC2_G3D_SHIFT) |
			(1 << S5P_CLKSRC2_MFC_SHIFT);
		writel_relaxed(reg, S5P_CLK_SRC2);

		do {
			reg = readl_relaxed(S5P_CLKMUX_STAT1);
		} while (reg & ((1 << 7) | (1 << 3)));

		/*
		 * 3. DMC1 refresh count for 133Mhz if (index == L4) is
		 * true refresh counter is already programed in upper
		 * code. 0x287@83Mhz
		 */
		if (!bus_speed_changing)
			s5pv210_set_refresh(DMC1, 133000);

		/* 4. SCLKAPLL -> SCLKMPLL */
		reg = readl_relaxed(S5P_CLK_SRC0);
		reg &= ~(S5P_CLKSRC0_MUX200_MASK);
		reg |= (0x1 << S5P_CLKSRC0_MUX200_SHIFT);
		writel_relaxed(reg, S5P_CLK_SRC0);

		do {
			reg = readl_relaxed(S5P_CLKMUX_STAT0);
		} while (reg & (0x1 << 18));

	}

	/* Change divider */
	reg = readl_relaxed(S5P_CLK_DIV0);

	reg &= ~(S5P_CLKDIV0_APLL_MASK | S5P_CLKDIV0_A2M_MASK |
		S5P_CLKDIV0_HCLK200_MASK | S5P_CLKDIV0_PCLK100_MASK |
		S5P_CLKDIV0_HCLK166_MASK | S5P_CLKDIV0_PCLK83_MASK |
		S5P_CLKDIV0_HCLK133_MASK | S5P_CLKDIV0_PCLK66_MASK);

	reg |= ((clkdiv_val[index][0] << S5P_CLKDIV0_APLL_SHIFT) |
		(clkdiv_val[index][1] << S5P_CLKDIV0_A2M_SHIFT) |
		(clkdiv_val[index][2] << S5P_CLKDIV0_HCLK200_SHIFT) |
		(clkdiv_val[index][3] << S5P_CLKDIV0_PCLK100_SHIFT) |
		(clkdiv_val[index][4] << S5P_CLKDIV0_HCLK166_SHIFT) |
		(clkdiv_val[index][5] << S5P_CLKDIV0_PCLK83_SHIFT) |
		(clkdiv_val[index][6] << S5P_CLKDIV0_HCLK133_SHIFT) |
		(clkdiv_val[index][7] << S5P_CLKDIV0_PCLK66_SHIFT));

	writel_relaxed(reg, S5P_CLK_DIV0);

	do {
		reg = readl_relaxed(S5P_CLKDIV_STAT0);
	} while (reg & 0xff);

	/* ARM MCS value changed */
	reg = readl_relaxed(S5P_ARM_MCS_CON);
	reg &= ~0x3;
	if (index >= L3)
		reg |= 0x3;
	else
		reg |= 0x1;

	writel_relaxed(reg, S5P_ARM_MCS_CON);

	if (pll_changing) {
		/* 5. Set Lock time = 30us*24Mhz = 0x2cf */
		writel_relaxed(0x2cf, S5P_APLL_LOCK);

		/*
		 * 6. Turn on APLL
		 * 6-1. Set PMS values
		 * 6-2. Wait untile the PLL is locked
		 */
		if (index == L0)
			writel_relaxed(APLL_VAL_1000, S5P_APLL_CON);
		else
			writel_relaxed(APLL_VAL_800, S5P_APLL_CON);

		do {
			reg = readl_relaxed(S5P_APLL_CON);
		} while (!(reg & (0x1 << 29)));

		/*
		 * 7. Change souce clock from SCLKMPLL(667Mhz)
		 * to SCLKA2M(200Mhz) in MFC_MUX and G3D MUX
		 * (667/4=166)->(200/4=50)Mhz
		 */
		reg = readl_relaxed(S5P_CLK_SRC2);
		reg &= ~(S5P_CLKSRC2_G3D_MASK | S5P_CLKSRC2_MFC_MASK);
		reg |= (0 << S5P_CLKSRC2_G3D_SHIFT) |
			(0 << S5P_CLKSRC2_MFC_SHIFT);
		writel_relaxed(reg, S5P_CLK_SRC2);

		do {
			reg = readl_relaxed(S5P_CLKMUX_STAT1);
		} while (reg & ((1 << 7) | (1 << 3)));

		/*
		 * 8. Change divider for MFC and G3D
		 * (200/4=50)->(200/1=200)Mhz
		 */
		reg = readl_relaxed(S5P_CLK_DIV2);
		reg &= ~(S5P_CLKDIV2_G3D_MASK | S5P_CLKDIV2_MFC_MASK);
		reg |= (clkdiv_val[index][10] << S5P_CLKDIV2_G3D_SHIFT) |
			(clkdiv_val[index][9] << S5P_CLKDIV2_MFC_SHIFT);
		writel_relaxed(reg, S5P_CLK_DIV2);

		/* For MFC, G3D dividing */
		do {
			reg = readl_relaxed(S5P_CLKDIV_STAT0);
		} while (reg & ((1 << 16) | (1 << 17)));

		/* 9. Change MPLL to APLL in MSYS_MUX */
		reg = readl_relaxed(S5P_CLK_SRC0);
		reg &= ~(S5P_CLKSRC0_MUX200_MASK);
		reg |= (0x0 << S5P_CLKSRC0_MUX200_SHIFT);
		writel_relaxed(reg, S5P_CLK_SRC0);

		do {
			reg = readl_relaxed(S5P_CLKMUX_STAT0);
		} while (reg & (0x1 << 18));

		/*
		 * 10. DMC1 refresh counter
		 * L4 : DMC1 = 100Mhz 7.8us/(1/100) = 0x30c
		 * Others : DMC1 = 200Mhz 7.8us/(1/200) = 0x618
		 */
		if (!bus_speed_changing)
			s5pv210_set_refresh(DMC1, 200000);
	}

	/*
	 * L4 level need to change memory bus speed, hence onedram clock divier
	 * and memory refresh parameter should be changed
	 */
	if (bus_speed_changing) {
		reg = readl_relaxed(S5P_CLK_DIV6);
		reg &= ~S5P_CLKDIV6_ONEDRAM_MASK;
		reg |= (clkdiv_val[index][8] << S5P_CLKDIV6_ONEDRAM_SHIFT);
		writel_relaxed(reg, S5P_CLK_DIV6);

		do {
			reg = readl_relaxed(S5P_CLKDIV_STAT1);
		} while (reg & (1 << 15));

		/* Reconfigure DRAM refresh counter value */
		if (index != L4) {
			/*
			 * DMC0 : 166Mhz
			 * DMC1 : 200Mhz
			 */
			s5pv210_set_refresh(DMC0, 166000);
			s5pv210_set_refresh(DMC1, 200000);
		} else {
			/*
			 * DMC0 : 83Mhz
			 * DMC1 : 100Mhz
			 */
			s5pv210_set_refresh(DMC0, 83000);
			s5pv210_set_refresh(DMC1, 100000);
		}
	}

	if (new_freq < old_freq) {
		regulator_set_voltage(int_regulator,
				int_volt, int_volt_max);

		regulator_set_voltage(arm_regulator,
				arm_volt, arm_volt_max);
	}

	pr_debug("Perf changed[L%d]\n", index);

exit:
	mutex_unlock(&set_freq_lock);
	return ret;
}

static int check_mem_type(void __iomem *dmc_reg)
{
	unsigned long val;

	val = readl_relaxed(dmc_reg + 0x4);
	val = (val & (0xf << 8));

	return val >> 8;
}

static int s5pv210_cpu_init(struct cpufreq_policy *policy)
{
	unsigned long mem_type;
	int ret;

	policy->clk = clk_get(NULL, "armclk");
	if (IS_ERR(policy->clk))
		return PTR_ERR(policy->clk);

	dmc0_clk = clk_get(NULL, "sclk_dmc0");
	if (IS_ERR(dmc0_clk)) {
		ret = PTR_ERR(dmc0_clk);
		goto out_dmc0;
	}

	dmc1_clk = clk_get(NULL, "hclk_msys");
	if (IS_ERR(dmc1_clk)) {
		ret = PTR_ERR(dmc1_clk);
		goto out_dmc1;
	}

	if (policy->cpu != 0) {
		ret = -EINVAL;
		goto out_dmc1;
	}

	/*
	 * check_mem_type : This driver only support LPDDR & LPDDR2.
	 * other memory type is not supported.
	 */
	mem_type = check_mem_type(dmc_base[0]);

	if ((mem_type != LPDDR) && (mem_type != LPDDR2)) {
		pr_err("CPUFreq doesn't support this memory type\n");
		ret = -EINVAL;
		goto out_dmc1;
	}

	/* Find current refresh counter and frequency each DMC */
	s5pv210_dram_conf[0].refresh = (readl_relaxed(dmc_base[0] + 0x30) * 1000);
	s5pv210_dram_conf[0].freq = clk_get_rate(dmc0_clk);

	s5pv210_dram_conf[1].refresh = (readl_relaxed(dmc_base[1] + 0x30) * 1000);
	s5pv210_dram_conf[1].freq = clk_get_rate(dmc1_clk);

	policy->suspend_freq = SLEEP_FREQ;
	return cpufreq_generic_init(policy, s5pv210_freq_table, 40000);

out_dmc1:
	clk_put(dmc0_clk);
out_dmc0:
	clk_put(policy->clk);
	return ret;
}

static int s5pv210_cpufreq_reboot_notifier_event(struct notifier_block *this,
						 unsigned long event, void *ptr)
{
	int ret;

	ret = cpufreq_driver_target(cpufreq_cpu_get(0), SLEEP_FREQ, 0);
	if (ret < 0)
		return NOTIFY_BAD;

	no_cpufreq_access = true;
	return NOTIFY_DONE;
}

static struct cpufreq_driver s5pv210_driver = {
	.flags		= CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= s5pv210_target,
	.get		= cpufreq_generic_get,
	.init		= s5pv210_cpu_init,
	.name		= "s5pv210",
	.suspend	= cpufreq_generic_suspend,
	.resume		= cpufreq_generic_suspend, /* We need to set SLEEP FREQ again */
};

static struct notifier_block s5pv210_cpufreq_reboot_notifier = {
	.notifier_call = s5pv210_cpufreq_reboot_notifier_event,
};

static int s5pv210_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;
	int id, result = 0;

	/*
	 * HACK: This is a temporary workaround to get access to clock
	 * and DMC controller registers directly and remove static mappings
	 * and dependencies on platform headers. It is necessary to enable
	 * S5PV210 multi-platform support and will be removed together with
	 * this whole driver as soon as S5PV210 gets migrated to use
	 * cpufreq-dt driver.
	 */
	arm_regulator = regulator_get(NULL, "vddarm");
	if (IS_ERR(arm_regulator)) {
		if (PTR_ERR(arm_regulator) == -EPROBE_DEFER)
			pr_debug("vddarm regulator not ready, defer\n");
		else
			pr_err("failed to get regulator vddarm\n");
		return PTR_ERR(arm_regulator);
	}

	int_regulator = regulator_get(NULL, "vddint");
	if (IS_ERR(int_regulator)) {
		if (PTR_ERR(int_regulator) == -EPROBE_DEFER)
			pr_debug("vddint regulator not ready, defer\n");
		else
			pr_err("failed to get regulator vddint\n");
		result = PTR_ERR(int_regulator);
		goto err_int_regulator;
	}

	np = of_find_compatible_node(NULL, NULL, "samsung,s5pv210-clock");
	if (!np) {
		pr_err("%s: failed to find clock controller DT node\n",
			__func__);
		result = -ENODEV;
		goto err_clock;
	}

	clk_base = of_iomap(np, 0);
	of_node_put(np);
	if (!clk_base) {
		pr_err("%s: failed to map clock registers\n", __func__);
		result = -EFAULT;
		goto err_clock;
	}

	for_each_compatible_node(np, NULL, "samsung,s5pv210-dmc") {
		id = of_alias_get_id(np, "dmc");
		if (id < 0 || id >= ARRAY_SIZE(dmc_base)) {
			pr_err("%s: failed to get alias of dmc node '%pOFn'\n",
				__func__, np);
			of_node_put(np);
			result = id;
			goto err_clk_base;
		}

		dmc_base[id] = of_iomap(np, 0);
		if (!dmc_base[id]) {
			pr_err("%s: failed to map dmc%d registers\n",
				__func__, id);
			of_node_put(np);
			result = -EFAULT;
			goto err_dmc;
		}
	}

	for (id = 0; id < ARRAY_SIZE(dmc_base); ++id) {
		if (!dmc_base[id]) {
			pr_err("%s: failed to find dmc%d node\n", __func__, id);
			result = -ENODEV;
			goto err_dmc;
		}
	}

	register_reboot_notifier(&s5pv210_cpufreq_reboot_notifier);

	return cpufreq_register_driver(&s5pv210_driver);

err_dmc:
	for (id = 0; id < ARRAY_SIZE(dmc_base); ++id)
		if (dmc_base[id]) {
			iounmap(dmc_base[id]);
			dmc_base[id] = NULL;
		}

err_clk_base:
	iounmap(clk_base);

err_clock:
	regulator_put(int_regulator);

err_int_regulator:
	regulator_put(arm_regulator);

	return result;
}

static struct platform_driver s5pv210_cpufreq_platdrv = {
	.driver = {
		.name	= "s5pv210-cpufreq",
	},
	.probe = s5pv210_cpufreq_probe,
};
builtin_platform_driver(s5pv210_cpufreq_platdrv);
