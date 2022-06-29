// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/soc/tegra/pmc.c
 *
 * Copyright (c) 2010 Google, Inc
 * Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 */

#define pr_fmt(fmt) "tegra-pmc: " fmt

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/clk-conf.h>
#include <linux/clk/tegra.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/pmc.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>
#include <dt-bindings/pinctrl/pinctrl-tegra-io-pad.h>
#include <dt-bindings/gpio/tegra186-gpio.h>
#include <dt-bindings/gpio/tegra194-gpio.h>
#include <dt-bindings/gpio/tegra234-gpio.h>
#include <dt-bindings/soc/tegra-pmc.h>

#define PMC_CNTRL			0x0
#define  PMC_CNTRL_INTR_POLARITY	BIT(17) /* inverts INTR polarity */
#define  PMC_CNTRL_CPU_PWRREQ_OE	BIT(16) /* CPU pwr req enable */
#define  PMC_CNTRL_CPU_PWRREQ_POLARITY	BIT(15) /* CPU pwr req polarity */
#define  PMC_CNTRL_SIDE_EFFECT_LP0	BIT(14) /* LP0 when CPU pwr gated */
#define  PMC_CNTRL_SYSCLK_OE		BIT(11) /* system clock enable */
#define  PMC_CNTRL_SYSCLK_POLARITY	BIT(10) /* sys clk polarity */
#define  PMC_CNTRL_PWRREQ_POLARITY	BIT(8)
#define  PMC_CNTRL_BLINK_EN		7
#define  PMC_CNTRL_MAIN_RST		BIT(4)

#define PMC_WAKE_MASK			0x0c
#define PMC_WAKE_LEVEL			0x10
#define PMC_WAKE_STATUS			0x14
#define PMC_SW_WAKE_STATUS		0x18
#define PMC_DPD_PADS_ORIDE		0x1c
#define  PMC_DPD_PADS_ORIDE_BLINK	20

#define DPD_SAMPLE			0x020
#define  DPD_SAMPLE_ENABLE		BIT(0)
#define  DPD_SAMPLE_DISABLE		(0 << 0)

#define PWRGATE_TOGGLE			0x30
#define  PWRGATE_TOGGLE_START		BIT(8)

#define REMOVE_CLAMPING			0x34

#define PWRGATE_STATUS			0x38

#define PMC_BLINK_TIMER			0x40
#define PMC_IMPL_E_33V_PWR		0x40

#define PMC_PWR_DET			0x48

#define PMC_SCRATCH0_MODE_RECOVERY	BIT(31)
#define PMC_SCRATCH0_MODE_BOOTLOADER	BIT(30)
#define PMC_SCRATCH0_MODE_RCM		BIT(1)
#define PMC_SCRATCH0_MODE_MASK		(PMC_SCRATCH0_MODE_RECOVERY | \
					 PMC_SCRATCH0_MODE_BOOTLOADER | \
					 PMC_SCRATCH0_MODE_RCM)

#define PMC_CPUPWRGOOD_TIMER		0xc8
#define PMC_CPUPWROFF_TIMER		0xcc
#define PMC_COREPWRGOOD_TIMER		0x3c
#define PMC_COREPWROFF_TIMER		0xe0

#define PMC_PWR_DET_VALUE		0xe4

#define PMC_USB_DEBOUNCE_DEL		0xec
#define PMC_USB_AO			0xf0

#define PMC_SCRATCH37			0x130
#define PMC_SCRATCH41			0x140

#define PMC_WAKE2_MASK			0x160
#define PMC_WAKE2_LEVEL			0x164
#define PMC_WAKE2_STATUS		0x168
#define PMC_SW_WAKE2_STATUS		0x16c

#define PMC_CLK_OUT_CNTRL		0x1a8
#define  PMC_CLK_OUT_MUX_MASK		GENMASK(1, 0)
#define PMC_SENSOR_CTRL			0x1b0
#define  PMC_SENSOR_CTRL_SCRATCH_WRITE	BIT(2)
#define  PMC_SENSOR_CTRL_ENABLE_RST	BIT(1)

#define  PMC_RST_STATUS_POR		0
#define  PMC_RST_STATUS_WATCHDOG	1
#define  PMC_RST_STATUS_SENSOR		2
#define  PMC_RST_STATUS_SW_MAIN		3
#define  PMC_RST_STATUS_LP0		4
#define  PMC_RST_STATUS_AOTAG		5

#define IO_DPD_REQ			0x1b8
#define  IO_DPD_REQ_CODE_IDLE		(0U << 30)
#define  IO_DPD_REQ_CODE_OFF		(1U << 30)
#define  IO_DPD_REQ_CODE_ON		(2U << 30)
#define  IO_DPD_REQ_CODE_MASK		(3U << 30)

#define IO_DPD_STATUS			0x1bc
#define IO_DPD2_REQ			0x1c0
#define IO_DPD2_STATUS			0x1c4
#define SEL_DPD_TIM			0x1c8

#define PMC_UTMIP_UHSIC_TRIGGERS	0x1ec
#define PMC_UTMIP_UHSIC_SAVED_STATE	0x1f0

#define PMC_UTMIP_TERM_PAD_CFG		0x1f8
#define PMC_UTMIP_UHSIC_SLEEP_CFG	0x1fc
#define PMC_UTMIP_UHSIC_FAKE		0x218

#define PMC_SCRATCH54			0x258
#define  PMC_SCRATCH54_DATA_SHIFT	8
#define  PMC_SCRATCH54_ADDR_SHIFT	0

#define PMC_SCRATCH55			0x25c
#define  PMC_SCRATCH55_RESET_TEGRA	BIT(31)
#define  PMC_SCRATCH55_CNTRL_ID_SHIFT	27
#define  PMC_SCRATCH55_PINMUX_SHIFT	24
#define  PMC_SCRATCH55_16BITOP		BIT(15)
#define  PMC_SCRATCH55_CHECKSUM_SHIFT	16
#define  PMC_SCRATCH55_I2CSLV1_SHIFT	0

#define  PMC_UTMIP_UHSIC_LINE_WAKEUP	0x26c

#define PMC_UTMIP_BIAS_MASTER_CNTRL	0x270
#define PMC_UTMIP_MASTER_CONFIG		0x274
#define PMC_UTMIP_UHSIC2_TRIGGERS	0x27c
#define PMC_UTMIP_MASTER2_CONFIG	0x29c

#define GPU_RG_CNTRL			0x2d4

#define PMC_UTMIP_PAD_CFG0		0x4c0
#define PMC_UTMIP_UHSIC_SLEEP_CFG1	0x4d0
#define PMC_UTMIP_SLEEPWALK_P3		0x4e0
/* Tegra186 and later */
#define WAKE_AOWAKE_CNTRL(x) (0x000 + ((x) << 2))
#define WAKE_AOWAKE_CNTRL_LEVEL (1 << 3)
#define WAKE_AOWAKE_MASK_W(x) (0x180 + ((x) << 2))
#define WAKE_AOWAKE_MASK_R(x) (0x300 + ((x) << 2))
#define WAKE_AOWAKE_STATUS_W(x) (0x30c + ((x) << 2))
#define WAKE_AOWAKE_STATUS_R(x) (0x48c + ((x) << 2))
#define WAKE_AOWAKE_TIER0_ROUTING(x) (0x4b4 + ((x) << 2))
#define WAKE_AOWAKE_TIER1_ROUTING(x) (0x4c0 + ((x) << 2))
#define WAKE_AOWAKE_TIER2_ROUTING(x) (0x4cc + ((x) << 2))

#define WAKE_AOWAKE_CTRL 0x4f4
#define  WAKE_AOWAKE_CTRL_INTR_POLARITY BIT(0)

/* for secure PMC */
#define TEGRA_SMC_PMC		0xc2fffe00
#define  TEGRA_SMC_PMC_READ	0xaa
#define  TEGRA_SMC_PMC_WRITE	0xbb

struct pmc_clk {
	struct clk_hw	hw;
	unsigned long	offs;
	u32		mux_shift;
	u32		force_en_shift;
};

#define to_pmc_clk(_hw) container_of(_hw, struct pmc_clk, hw)

struct pmc_clk_gate {
	struct clk_hw	hw;
	unsigned long	offs;
	u32		shift;
};

#define to_pmc_clk_gate(_hw) container_of(_hw, struct pmc_clk_gate, hw)

struct pmc_clk_init_data {
	char *name;
	const char *const *parents;
	int num_parents;
	int clk_id;
	u8 mux_shift;
	u8 force_en_shift;
};

static const char * const clk_out1_parents[] = { "osc", "osc_div2",
	"osc_div4", "extern1",
};

static const char * const clk_out2_parents[] = { "osc", "osc_div2",
	"osc_div4", "extern2",
};

static const char * const clk_out3_parents[] = { "osc", "osc_div2",
	"osc_div4", "extern3",
};

static const struct pmc_clk_init_data tegra_pmc_clks_data[] = {
	{
		.name = "pmc_clk_out_1",
		.parents = clk_out1_parents,
		.num_parents = ARRAY_SIZE(clk_out1_parents),
		.clk_id = TEGRA_PMC_CLK_OUT_1,
		.mux_shift = 6,
		.force_en_shift = 2,
	},
	{
		.name = "pmc_clk_out_2",
		.parents = clk_out2_parents,
		.num_parents = ARRAY_SIZE(clk_out2_parents),
		.clk_id = TEGRA_PMC_CLK_OUT_2,
		.mux_shift = 14,
		.force_en_shift = 10,
	},
	{
		.name = "pmc_clk_out_3",
		.parents = clk_out3_parents,
		.num_parents = ARRAY_SIZE(clk_out3_parents),
		.clk_id = TEGRA_PMC_CLK_OUT_3,
		.mux_shift = 22,
		.force_en_shift = 18,
	},
};

struct tegra_powergate {
	struct generic_pm_domain genpd;
	struct tegra_pmc *pmc;
	unsigned int id;
	struct clk **clks;
	unsigned int num_clks;
	unsigned long *clk_rates;
	struct reset_control *reset;
};

struct tegra_io_pad_soc {
	enum tegra_io_pad id;
	unsigned int dpd;
	unsigned int voltage;
	const char *name;
};

struct tegra_pmc_regs {
	unsigned int scratch0;
	unsigned int dpd_req;
	unsigned int dpd_status;
	unsigned int dpd2_req;
	unsigned int dpd2_status;
	unsigned int rst_status;
	unsigned int rst_source_shift;
	unsigned int rst_source_mask;
	unsigned int rst_level_shift;
	unsigned int rst_level_mask;
};

struct tegra_wake_event {
	const char *name;
	unsigned int id;
	unsigned int irq;
	struct {
		unsigned int instance;
		unsigned int pin;
	} gpio;
};

#define TEGRA_WAKE_IRQ(_name, _id, _irq)		\
	{						\
		.name = _name,				\
		.id = _id,				\
		.irq = _irq,				\
		.gpio = {				\
			.instance = UINT_MAX,		\
			.pin = UINT_MAX,		\
		},					\
	}

#define TEGRA_WAKE_GPIO(_name, _id, _instance, _pin)	\
	{						\
		.name = _name,				\
		.id = _id,				\
		.irq = 0,				\
		.gpio = {				\
			.instance = _instance,		\
			.pin = _pin,			\
		},					\
	}

struct tegra_pmc_soc {
	unsigned int num_powergates;
	const char *const *powergates;
	unsigned int num_cpu_powergates;
	const u8 *cpu_powergates;

	bool has_tsense_reset;
	bool has_gpu_clamps;
	bool needs_mbist_war;
	bool has_impl_33v_pwr;
	bool maybe_tz_only;

	const struct tegra_io_pad_soc *io_pads;
	unsigned int num_io_pads;

	const struct pinctrl_pin_desc *pin_descs;
	unsigned int num_pin_descs;

	const struct tegra_pmc_regs *regs;
	void (*init)(struct tegra_pmc *pmc);
	void (*setup_irq_polarity)(struct tegra_pmc *pmc,
				   struct device_node *np,
				   bool invert);
	int (*irq_set_wake)(struct irq_data *data, unsigned int on);
	int (*irq_set_type)(struct irq_data *data, unsigned int type);
	int (*powergate_set)(struct tegra_pmc *pmc, unsigned int id,
			     bool new_state);

	const char * const *reset_sources;
	unsigned int num_reset_sources;
	const char * const *reset_levels;
	unsigned int num_reset_levels;

	/*
	 * These describe events that can wake the system from sleep (i.e.
	 * LP0 or SC7). Wakeup from other sleep states (such as LP1 or LP2)
	 * are dealt with in the LIC.
	 */
	const struct tegra_wake_event *wake_events;
	unsigned int num_wake_events;

	const struct pmc_clk_init_data *pmc_clks_data;
	unsigned int num_pmc_clks;
	bool has_blink_output;
	bool has_usb_sleepwalk;
	bool supports_core_domain;
};

/**
 * struct tegra_pmc - NVIDIA Tegra PMC
 * @dev: pointer to PMC device structure
 * @base: pointer to I/O remapped register region
 * @wake: pointer to I/O remapped region for WAKE registers
 * @aotag: pointer to I/O remapped region for AOTAG registers
 * @scratch: pointer to I/O remapped region for scratch registers
 * @clk: pointer to pclk clock
 * @soc: pointer to SoC data structure
 * @tz_only: flag specifying if the PMC can only be accessed via TrustZone
 * @debugfs: pointer to debugfs entry
 * @rate: currently configured rate of pclk
 * @suspend_mode: lowest suspend mode available
 * @cpu_good_time: CPU power good time (in microseconds)
 * @cpu_off_time: CPU power off time (in microsecends)
 * @core_osc_time: core power good OSC time (in microseconds)
 * @core_pmu_time: core power good PMU time (in microseconds)
 * @core_off_time: core power off time (in microseconds)
 * @corereq_high: core power request is active-high
 * @sysclkreq_high: system clock request is active-high
 * @combined_req: combined power request for CPU & core
 * @cpu_pwr_good_en: CPU power good signal is enabled
 * @lp0_vec_phys: physical base address of the LP0 warm boot code
 * @lp0_vec_size: size of the LP0 warm boot code
 * @powergates_available: Bitmap of available power gates
 * @powergates_lock: mutex for power gate register access
 * @pctl_dev: pin controller exposed by the PMC
 * @domain: IRQ domain provided by the PMC
 * @irq: chip implementation for the IRQ domain
 * @clk_nb: pclk clock changes handler
 * @core_domain_state_synced: flag marking the core domain's state as synced
 * @core_domain_registered: flag marking the core domain as registered
 */
struct tegra_pmc {
	struct device *dev;
	void __iomem *base;
	void __iomem *wake;
	void __iomem *aotag;
	void __iomem *scratch;
	struct clk *clk;
	struct dentry *debugfs;

	const struct tegra_pmc_soc *soc;
	bool tz_only;

	unsigned long rate;

	enum tegra_suspend_mode suspend_mode;
	u32 cpu_good_time;
	u32 cpu_off_time;
	u32 core_osc_time;
	u32 core_pmu_time;
	u32 core_off_time;
	bool corereq_high;
	bool sysclkreq_high;
	bool combined_req;
	bool cpu_pwr_good_en;
	u32 lp0_vec_phys;
	u32 lp0_vec_size;
	DECLARE_BITMAP(powergates_available, TEGRA_POWERGATE_MAX);

	struct mutex powergates_lock;

	struct pinctrl_dev *pctl_dev;

	struct irq_domain *domain;
	struct irq_chip irq;

	struct notifier_block clk_nb;

	bool core_domain_state_synced;
	bool core_domain_registered;
};

static struct tegra_pmc *pmc = &(struct tegra_pmc) {
	.base = NULL,
	.suspend_mode = TEGRA_SUSPEND_NOT_READY,
};

static inline struct tegra_powergate *
to_powergate(struct generic_pm_domain *domain)
{
	return container_of(domain, struct tegra_powergate, genpd);
}

static u32 tegra_pmc_readl(struct tegra_pmc *pmc, unsigned long offset)
{
	struct arm_smccc_res res;

	if (pmc->tz_only) {
		arm_smccc_smc(TEGRA_SMC_PMC, TEGRA_SMC_PMC_READ, offset, 0, 0,
			      0, 0, 0, &res);
		if (res.a0) {
			if (pmc->dev)
				dev_warn(pmc->dev, "%s(): SMC failed: %lu\n",
					 __func__, res.a0);
			else
				pr_warn("%s(): SMC failed: %lu\n", __func__,
					res.a0);
		}

		return res.a1;
	}

	return readl(pmc->base + offset);
}

static void tegra_pmc_writel(struct tegra_pmc *pmc, u32 value,
			     unsigned long offset)
{
	struct arm_smccc_res res;

	if (pmc->tz_only) {
		arm_smccc_smc(TEGRA_SMC_PMC, TEGRA_SMC_PMC_WRITE, offset,
			      value, 0, 0, 0, 0, &res);
		if (res.a0) {
			if (pmc->dev)
				dev_warn(pmc->dev, "%s(): SMC failed: %lu\n",
					 __func__, res.a0);
			else
				pr_warn("%s(): SMC failed: %lu\n", __func__,
					res.a0);
		}
	} else {
		writel(value, pmc->base + offset);
	}
}

static u32 tegra_pmc_scratch_readl(struct tegra_pmc *pmc, unsigned long offset)
{
	if (pmc->tz_only)
		return tegra_pmc_readl(pmc, offset);

	return readl(pmc->scratch + offset);
}

static void tegra_pmc_scratch_writel(struct tegra_pmc *pmc, u32 value,
				     unsigned long offset)
{
	if (pmc->tz_only)
		tegra_pmc_writel(pmc, value, offset);
	else
		writel(value, pmc->scratch + offset);
}

/*
 * TODO Figure out a way to call this with the struct tegra_pmc * passed in.
 * This currently doesn't work because readx_poll_timeout() can only operate
 * on functions that take a single argument.
 */
static inline bool tegra_powergate_state(int id)
{
	if (id == TEGRA_POWERGATE_3D && pmc->soc->has_gpu_clamps)
		return (tegra_pmc_readl(pmc, GPU_RG_CNTRL) & 0x1) == 0;
	else
		return (tegra_pmc_readl(pmc, PWRGATE_STATUS) & BIT(id)) != 0;
}

static inline bool tegra_powergate_is_valid(struct tegra_pmc *pmc, int id)
{
	return (pmc->soc && pmc->soc->powergates[id]);
}

static inline bool tegra_powergate_is_available(struct tegra_pmc *pmc, int id)
{
	return test_bit(id, pmc->powergates_available);
}

static int tegra_powergate_lookup(struct tegra_pmc *pmc, const char *name)
{
	unsigned int i;

	if (!pmc || !pmc->soc || !name)
		return -EINVAL;

	for (i = 0; i < pmc->soc->num_powergates; i++) {
		if (!tegra_powergate_is_valid(pmc, i))
			continue;

		if (!strcmp(name, pmc->soc->powergates[i]))
			return i;
	}

	return -ENODEV;
}

static int tegra20_powergate_set(struct tegra_pmc *pmc, unsigned int id,
				 bool new_state)
{
	unsigned int retries = 100;
	bool status;
	int ret;

	/*
	 * As per TRM documentation, the toggle command will be dropped by PMC
	 * if there is contention with a HW-initiated toggling (i.e. CPU core
	 * power-gated), the command should be retried in that case.
	 */
	do {
		tegra_pmc_writel(pmc, PWRGATE_TOGGLE_START | id, PWRGATE_TOGGLE);

		/* wait for PMC to execute the command */
		ret = readx_poll_timeout(tegra_powergate_state, id, status,
					 status == new_state, 1, 10);
	} while (ret == -ETIMEDOUT && retries--);

	return ret;
}

static inline bool tegra_powergate_toggle_ready(struct tegra_pmc *pmc)
{
	return !(tegra_pmc_readl(pmc, PWRGATE_TOGGLE) & PWRGATE_TOGGLE_START);
}

static int tegra114_powergate_set(struct tegra_pmc *pmc, unsigned int id,
				  bool new_state)
{
	bool status;
	int err;

	/* wait while PMC power gating is contended */
	err = readx_poll_timeout(tegra_powergate_toggle_ready, pmc, status,
				 status == true, 1, 100);
	if (err)
		return err;

	tegra_pmc_writel(pmc, PWRGATE_TOGGLE_START | id, PWRGATE_TOGGLE);

	/* wait for PMC to accept the command */
	err = readx_poll_timeout(tegra_powergate_toggle_ready, pmc, status,
				 status == true, 1, 100);
	if (err)
		return err;

	/* wait for PMC to execute the command */
	err = readx_poll_timeout(tegra_powergate_state, id, status,
				 status == new_state, 10, 100000);
	if (err)
		return err;

	return 0;
}

/**
 * tegra_powergate_set() - set the state of a partition
 * @pmc: power management controller
 * @id: partition ID
 * @new_state: new state of the partition
 */
static int tegra_powergate_set(struct tegra_pmc *pmc, unsigned int id,
			       bool new_state)
{
	int err;

	if (id == TEGRA_POWERGATE_3D && pmc->soc->has_gpu_clamps)
		return -EINVAL;

	mutex_lock(&pmc->powergates_lock);

	if (tegra_powergate_state(id) == new_state) {
		mutex_unlock(&pmc->powergates_lock);
		return 0;
	}

	err = pmc->soc->powergate_set(pmc, id, new_state);

	mutex_unlock(&pmc->powergates_lock);

	return err;
}

static int __tegra_powergate_remove_clamping(struct tegra_pmc *pmc,
					     unsigned int id)
{
	u32 mask;

	mutex_lock(&pmc->powergates_lock);

	/*
	 * On Tegra124 and later, the clamps for the GPU are controlled by a
	 * separate register (with different semantics).
	 */
	if (id == TEGRA_POWERGATE_3D) {
		if (pmc->soc->has_gpu_clamps) {
			tegra_pmc_writel(pmc, 0, GPU_RG_CNTRL);
			goto out;
		}
	}

	/*
	 * Tegra 2 has a bug where PCIE and VDE clamping masks are
	 * swapped relatively to the partition ids
	 */
	if (id == TEGRA_POWERGATE_VDEC)
		mask = (1 << TEGRA_POWERGATE_PCIE);
	else if (id == TEGRA_POWERGATE_PCIE)
		mask = (1 << TEGRA_POWERGATE_VDEC);
	else
		mask = (1 << id);

	tegra_pmc_writel(pmc, mask, REMOVE_CLAMPING);

out:
	mutex_unlock(&pmc->powergates_lock);

	return 0;
}

static int tegra_powergate_prepare_clocks(struct tegra_powergate *pg)
{
	unsigned long safe_rate = 100 * 1000 * 1000;
	unsigned int i;
	int err;

	for (i = 0; i < pg->num_clks; i++) {
		pg->clk_rates[i] = clk_get_rate(pg->clks[i]);

		if (!pg->clk_rates[i]) {
			err = -EINVAL;
			goto out;
		}

		if (pg->clk_rates[i] <= safe_rate)
			continue;

		/*
		 * We don't know whether voltage state is okay for the
		 * current clock rate, hence it's better to temporally
		 * switch clock to a safe rate which is suitable for
		 * all voltages, before enabling the clock.
		 */
		err = clk_set_rate(pg->clks[i], safe_rate);
		if (err)
			goto out;
	}

	return 0;

out:
	while (i--)
		clk_set_rate(pg->clks[i], pg->clk_rates[i]);

	return err;
}

static int tegra_powergate_unprepare_clocks(struct tegra_powergate *pg)
{
	unsigned int i;
	int err;

	for (i = 0; i < pg->num_clks; i++) {
		err = clk_set_rate(pg->clks[i], pg->clk_rates[i]);
		if (err)
			return err;
	}

	return 0;
}

static void tegra_powergate_disable_clocks(struct tegra_powergate *pg)
{
	unsigned int i;

	for (i = 0; i < pg->num_clks; i++)
		clk_disable_unprepare(pg->clks[i]);
}

static int tegra_powergate_enable_clocks(struct tegra_powergate *pg)
{
	unsigned int i;
	int err;

	for (i = 0; i < pg->num_clks; i++) {
		err = clk_prepare_enable(pg->clks[i]);
		if (err)
			goto out;
	}

	return 0;

out:
	while (i--)
		clk_disable_unprepare(pg->clks[i]);

	return err;
}

static int tegra_powergate_power_up(struct tegra_powergate *pg,
				    bool disable_clocks)
{
	int err;

	err = reset_control_assert(pg->reset);
	if (err)
		return err;

	usleep_range(10, 20);

	err = tegra_powergate_set(pg->pmc, pg->id, true);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	err = tegra_powergate_prepare_clocks(pg);
	if (err)
		goto powergate_off;

	err = tegra_powergate_enable_clocks(pg);
	if (err)
		goto unprepare_clks;

	usleep_range(10, 20);

	err = __tegra_powergate_remove_clamping(pg->pmc, pg->id);
	if (err)
		goto disable_clks;

	usleep_range(10, 20);

	err = reset_control_deassert(pg->reset);
	if (err)
		goto disable_clks;

	usleep_range(10, 20);

	if (pg->pmc->soc->needs_mbist_war)
		err = tegra210_clk_handle_mbist_war(pg->id);
	if (err)
		goto disable_clks;

	if (disable_clocks)
		tegra_powergate_disable_clocks(pg);

	err = tegra_powergate_unprepare_clocks(pg);
	if (err)
		return err;

	return 0;

disable_clks:
	tegra_powergate_disable_clocks(pg);
	usleep_range(10, 20);

unprepare_clks:
	tegra_powergate_unprepare_clocks(pg);

powergate_off:
	tegra_powergate_set(pg->pmc, pg->id, false);

	return err;
}

static int tegra_powergate_power_down(struct tegra_powergate *pg)
{
	int err;

	err = tegra_powergate_prepare_clocks(pg);
	if (err)
		return err;

	err = tegra_powergate_enable_clocks(pg);
	if (err)
		goto unprepare_clks;

	usleep_range(10, 20);

	err = reset_control_assert(pg->reset);
	if (err)
		goto disable_clks;

	usleep_range(10, 20);

	tegra_powergate_disable_clocks(pg);

	usleep_range(10, 20);

	err = tegra_powergate_set(pg->pmc, pg->id, false);
	if (err)
		goto assert_resets;

	err = tegra_powergate_unprepare_clocks(pg);
	if (err)
		return err;

	return 0;

assert_resets:
	tegra_powergate_enable_clocks(pg);
	usleep_range(10, 20);
	reset_control_deassert(pg->reset);
	usleep_range(10, 20);

disable_clks:
	tegra_powergate_disable_clocks(pg);

unprepare_clks:
	tegra_powergate_unprepare_clocks(pg);

	return err;
}

static int tegra_genpd_power_on(struct generic_pm_domain *domain)
{
	struct tegra_powergate *pg = to_powergate(domain);
	struct device *dev = pg->pmc->dev;
	int err;

	err = tegra_powergate_power_up(pg, true);
	if (err) {
		dev_err(dev, "failed to turn on PM domain %s: %d\n",
			pg->genpd.name, err);
		goto out;
	}

	reset_control_release(pg->reset);

out:
	return err;
}

static int tegra_genpd_power_off(struct generic_pm_domain *domain)
{
	struct tegra_powergate *pg = to_powergate(domain);
	struct device *dev = pg->pmc->dev;
	int err;

	err = reset_control_acquire(pg->reset);
	if (err < 0) {
		dev_err(dev, "failed to acquire resets for PM domain %s: %d\n",
			pg->genpd.name, err);
		return err;
	}

	err = tegra_powergate_power_down(pg);
	if (err) {
		dev_err(dev, "failed to turn off PM domain %s: %d\n",
			pg->genpd.name, err);
		reset_control_release(pg->reset);
	}

	return err;
}

/**
 * tegra_powergate_power_on() - power on partition
 * @id: partition ID
 */
int tegra_powergate_power_on(unsigned int id)
{
	if (!tegra_powergate_is_available(pmc, id))
		return -EINVAL;

	return tegra_powergate_set(pmc, id, true);
}
EXPORT_SYMBOL(tegra_powergate_power_on);

/**
 * tegra_powergate_power_off() - power off partition
 * @id: partition ID
 */
int tegra_powergate_power_off(unsigned int id)
{
	if (!tegra_powergate_is_available(pmc, id))
		return -EINVAL;

	return tegra_powergate_set(pmc, id, false);
}
EXPORT_SYMBOL(tegra_powergate_power_off);

/**
 * tegra_powergate_is_powered() - check if partition is powered
 * @pmc: power management controller
 * @id: partition ID
 */
static int tegra_powergate_is_powered(struct tegra_pmc *pmc, unsigned int id)
{
	if (!tegra_powergate_is_valid(pmc, id))
		return -EINVAL;

	return tegra_powergate_state(id);
}

/**
 * tegra_powergate_remove_clamping() - remove power clamps for partition
 * @id: partition ID
 */
int tegra_powergate_remove_clamping(unsigned int id)
{
	if (!tegra_powergate_is_available(pmc, id))
		return -EINVAL;

	return __tegra_powergate_remove_clamping(pmc, id);
}
EXPORT_SYMBOL(tegra_powergate_remove_clamping);

/**
 * tegra_powergate_sequence_power_up() - power up partition
 * @id: partition ID
 * @clk: clock for partition
 * @rst: reset for partition
 *
 * Must be called with clk disabled, and returns with clk enabled.
 */
int tegra_powergate_sequence_power_up(unsigned int id, struct clk *clk,
				      struct reset_control *rst)
{
	struct tegra_powergate *pg;
	int err;

	if (!tegra_powergate_is_available(pmc, id))
		return -EINVAL;

	pg = kzalloc(sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return -ENOMEM;

	pg->clk_rates = kzalloc(sizeof(*pg->clk_rates), GFP_KERNEL);
	if (!pg->clk_rates) {
		kfree(pg->clks);
		return -ENOMEM;
	}

	pg->id = id;
	pg->clks = &clk;
	pg->num_clks = 1;
	pg->reset = rst;
	pg->pmc = pmc;

	err = tegra_powergate_power_up(pg, false);
	if (err)
		dev_err(pmc->dev, "failed to turn on partition %d: %d\n", id,
			err);

	kfree(pg->clk_rates);
	kfree(pg);

	return err;
}
EXPORT_SYMBOL(tegra_powergate_sequence_power_up);

/**
 * tegra_get_cpu_powergate_id() - convert from CPU ID to partition ID
 * @pmc: power management controller
 * @cpuid: CPU partition ID
 *
 * Returns the partition ID corresponding to the CPU partition ID or a
 * negative error code on failure.
 */
static int tegra_get_cpu_powergate_id(struct tegra_pmc *pmc,
				      unsigned int cpuid)
{
	if (pmc->soc && cpuid < pmc->soc->num_cpu_powergates)
		return pmc->soc->cpu_powergates[cpuid];

	return -EINVAL;
}

/**
 * tegra_pmc_cpu_is_powered() - check if CPU partition is powered
 * @cpuid: CPU partition ID
 */
bool tegra_pmc_cpu_is_powered(unsigned int cpuid)
{
	int id;

	id = tegra_get_cpu_powergate_id(pmc, cpuid);
	if (id < 0)
		return false;

	return tegra_powergate_is_powered(pmc, id);
}

/**
 * tegra_pmc_cpu_power_on() - power on CPU partition
 * @cpuid: CPU partition ID
 */
int tegra_pmc_cpu_power_on(unsigned int cpuid)
{
	int id;

	id = tegra_get_cpu_powergate_id(pmc, cpuid);
	if (id < 0)
		return id;

	return tegra_powergate_set(pmc, id, true);
}

/**
 * tegra_pmc_cpu_remove_clamping() - remove power clamps for CPU partition
 * @cpuid: CPU partition ID
 */
int tegra_pmc_cpu_remove_clamping(unsigned int cpuid)
{
	int id;

	id = tegra_get_cpu_powergate_id(pmc, cpuid);
	if (id < 0)
		return id;

	return tegra_powergate_remove_clamping(id);
}

static void tegra_pmc_program_reboot_reason(const char *cmd)
{
	u32 value;

	value = tegra_pmc_scratch_readl(pmc, pmc->soc->regs->scratch0);
	value &= ~PMC_SCRATCH0_MODE_MASK;

	if (cmd) {
		if (strcmp(cmd, "recovery") == 0)
			value |= PMC_SCRATCH0_MODE_RECOVERY;

		if (strcmp(cmd, "bootloader") == 0)
			value |= PMC_SCRATCH0_MODE_BOOTLOADER;

		if (strcmp(cmd, "forced-recovery") == 0)
			value |= PMC_SCRATCH0_MODE_RCM;
	}

	tegra_pmc_scratch_writel(pmc, value, pmc->soc->regs->scratch0);
}

static int tegra_pmc_reboot_notify(struct notifier_block *this,
				   unsigned long action, void *data)
{
	if (action == SYS_RESTART)
		tegra_pmc_program_reboot_reason(data);

	return NOTIFY_DONE;
}

static struct notifier_block tegra_pmc_reboot_notifier = {
	.notifier_call = tegra_pmc_reboot_notify,
};

static void tegra_pmc_restart(void)
{
	u32 value;

	/* reset everything but PMC_SCRATCH0 and PMC_RST_STATUS */
	value = tegra_pmc_readl(pmc, PMC_CNTRL);
	value |= PMC_CNTRL_MAIN_RST;
	tegra_pmc_writel(pmc, value, PMC_CNTRL);
}

static int tegra_pmc_restart_handler(struct sys_off_data *data)
{
	tegra_pmc_restart();

	return NOTIFY_DONE;
}

static int tegra_pmc_power_off_handler(struct sys_off_data *data)
{
	/*
	 * Reboot Nexus 7 into special bootloader mode if USB cable is
	 * connected in order to display battery status and power off.
	 */
	if (of_machine_is_compatible("asus,grouper") &&
	    power_supply_is_system_supplied()) {
		const u32 go_to_charger_mode = 0xa5a55a5a;

		tegra_pmc_writel(pmc, go_to_charger_mode, PMC_SCRATCH37);
		tegra_pmc_restart();
	}

	return NOTIFY_DONE;
}

static int powergate_show(struct seq_file *s, void *data)
{
	unsigned int i;
	int status;

	seq_printf(s, " powergate powered\n");
	seq_printf(s, "------------------\n");

	for (i = 0; i < pmc->soc->num_powergates; i++) {
		status = tegra_powergate_is_powered(pmc, i);
		if (status < 0)
			continue;

		seq_printf(s, " %9s %7s\n", pmc->soc->powergates[i],
			   status ? "yes" : "no");
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(powergate);

static int tegra_powergate_debugfs_init(void)
{
	pmc->debugfs = debugfs_create_file("powergate", S_IRUGO, NULL, NULL,
					   &powergate_fops);
	if (!pmc->debugfs)
		return -ENOMEM;

	return 0;
}

static int tegra_powergate_of_get_clks(struct tegra_powergate *pg,
				       struct device_node *np)
{
	struct clk *clk;
	unsigned int i, count;
	int err;

	count = of_clk_get_parent_count(np);
	if (count == 0)
		return -ENODEV;

	pg->clks = kcalloc(count, sizeof(clk), GFP_KERNEL);
	if (!pg->clks)
		return -ENOMEM;

	pg->clk_rates = kcalloc(count, sizeof(*pg->clk_rates), GFP_KERNEL);
	if (!pg->clk_rates) {
		kfree(pg->clks);
		return -ENOMEM;
	}

	for (i = 0; i < count; i++) {
		pg->clks[i] = of_clk_get(np, i);
		if (IS_ERR(pg->clks[i])) {
			err = PTR_ERR(pg->clks[i]);
			goto err;
		}
	}

	pg->num_clks = count;

	return 0;

err:
	while (i--)
		clk_put(pg->clks[i]);

	kfree(pg->clk_rates);
	kfree(pg->clks);

	return err;
}

static int tegra_powergate_of_get_resets(struct tegra_powergate *pg,
					 struct device_node *np, bool off)
{
	struct device *dev = pg->pmc->dev;
	int err;

	pg->reset = of_reset_control_array_get_exclusive_released(np);
	if (IS_ERR(pg->reset)) {
		err = PTR_ERR(pg->reset);
		dev_err(dev, "failed to get device resets: %d\n", err);
		return err;
	}

	err = reset_control_acquire(pg->reset);
	if (err < 0) {
		pr_err("failed to acquire resets: %d\n", err);
		goto out;
	}

	if (off) {
		err = reset_control_assert(pg->reset);
	} else {
		err = reset_control_deassert(pg->reset);
		if (err < 0)
			goto out;

		reset_control_release(pg->reset);
	}

out:
	if (err) {
		reset_control_release(pg->reset);
		reset_control_put(pg->reset);
	}

	return err;
}

static int tegra_powergate_add(struct tegra_pmc *pmc, struct device_node *np)
{
	struct device *dev = pmc->dev;
	struct tegra_powergate *pg;
	int id, err = 0;
	bool off;

	pg = kzalloc(sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return -ENOMEM;

	id = tegra_powergate_lookup(pmc, np->name);
	if (id < 0) {
		dev_err(dev, "powergate lookup failed for %pOFn: %d\n", np, id);
		err = -ENODEV;
		goto free_mem;
	}

	/*
	 * Clear the bit for this powergate so it cannot be managed
	 * directly via the legacy APIs for controlling powergates.
	 */
	clear_bit(id, pmc->powergates_available);

	pg->id = id;
	pg->genpd.name = np->name;
	pg->genpd.power_off = tegra_genpd_power_off;
	pg->genpd.power_on = tegra_genpd_power_on;
	pg->pmc = pmc;

	off = !tegra_powergate_is_powered(pmc, pg->id);

	err = tegra_powergate_of_get_clks(pg, np);
	if (err < 0) {
		dev_err(dev, "failed to get clocks for %pOFn: %d\n", np, err);
		goto set_available;
	}

	err = tegra_powergate_of_get_resets(pg, np, off);
	if (err < 0) {
		dev_err(dev, "failed to get resets for %pOFn: %d\n", np, err);
		goto remove_clks;
	}

	if (!IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS)) {
		if (off)
			WARN_ON(tegra_powergate_power_up(pg, true));

		goto remove_resets;
	}

	err = pm_genpd_init(&pg->genpd, NULL, off);
	if (err < 0) {
		dev_err(dev, "failed to initialise PM domain %pOFn: %d\n", np,
		       err);
		goto remove_resets;
	}

	err = of_genpd_add_provider_simple(np, &pg->genpd);
	if (err < 0) {
		dev_err(dev, "failed to add PM domain provider for %pOFn: %d\n",
			np, err);
		goto remove_genpd;
	}

	dev_dbg(dev, "added PM domain %s\n", pg->genpd.name);

	return 0;

remove_genpd:
	pm_genpd_remove(&pg->genpd);

remove_resets:
	reset_control_put(pg->reset);

remove_clks:
	while (pg->num_clks--)
		clk_put(pg->clks[pg->num_clks]);

	kfree(pg->clks);

set_available:
	set_bit(id, pmc->powergates_available);

free_mem:
	kfree(pg);

	return err;
}

bool tegra_pmc_core_domain_state_synced(void)
{
	return pmc->core_domain_state_synced;
}

static int
tegra_pmc_core_pd_set_performance_state(struct generic_pm_domain *genpd,
					unsigned int level)
{
	struct dev_pm_opp *opp;
	int err;

	opp = dev_pm_opp_find_level_ceil(&genpd->dev, &level);
	if (IS_ERR(opp)) {
		dev_err(&genpd->dev, "failed to find OPP for level %u: %pe\n",
			level, opp);
		return PTR_ERR(opp);
	}

	mutex_lock(&pmc->powergates_lock);
	err = dev_pm_opp_set_opp(pmc->dev, opp);
	mutex_unlock(&pmc->powergates_lock);

	dev_pm_opp_put(opp);

	if (err) {
		dev_err(&genpd->dev, "failed to set voltage to %duV: %d\n",
			level, err);
		return err;
	}

	return 0;
}

static unsigned int
tegra_pmc_core_pd_opp_to_performance_state(struct generic_pm_domain *genpd,
					   struct dev_pm_opp *opp)
{
	return dev_pm_opp_get_level(opp);
}

static int tegra_pmc_core_pd_add(struct tegra_pmc *pmc, struct device_node *np)
{
	struct generic_pm_domain *genpd;
	const char *rname = "core";
	int err;

	genpd = devm_kzalloc(pmc->dev, sizeof(*genpd), GFP_KERNEL);
	if (!genpd)
		return -ENOMEM;

	genpd->name = "core";
	genpd->set_performance_state = tegra_pmc_core_pd_set_performance_state;
	genpd->opp_to_performance_state = tegra_pmc_core_pd_opp_to_performance_state;

	err = devm_pm_opp_set_regulators(pmc->dev, &rname, 1);
	if (err)
		return dev_err_probe(pmc->dev, err,
				     "failed to set core OPP regulator\n");

	err = pm_genpd_init(genpd, NULL, false);
	if (err) {
		dev_err(pmc->dev, "failed to init core genpd: %d\n", err);
		return err;
	}

	err = of_genpd_add_provider_simple(np, genpd);
	if (err) {
		dev_err(pmc->dev, "failed to add core genpd: %d\n", err);
		goto remove_genpd;
	}

	pmc->core_domain_registered = true;

	return 0;

remove_genpd:
	pm_genpd_remove(genpd);

	return err;
}

static int tegra_powergate_init(struct tegra_pmc *pmc,
				struct device_node *parent)
{
	struct of_phandle_args child_args, parent_args;
	struct device_node *np, *child;
	int err = 0;

	/*
	 * Core power domain is the parent of powergate domains, hence it
	 * should be registered first.
	 */
	np = of_get_child_by_name(parent, "core-domain");
	if (np) {
		err = tegra_pmc_core_pd_add(pmc, np);
		of_node_put(np);
		if (err)
			return err;
	}

	np = of_get_child_by_name(parent, "powergates");
	if (!np)
		return 0;

	for_each_child_of_node(np, child) {
		err = tegra_powergate_add(pmc, child);
		if (err < 0) {
			of_node_put(child);
			break;
		}

		if (of_parse_phandle_with_args(child, "power-domains",
					       "#power-domain-cells",
					       0, &parent_args))
			continue;

		child_args.np = child;
		child_args.args_count = 0;

		err = of_genpd_add_subdomain(&parent_args, &child_args);
		of_node_put(parent_args.np);
		if (err) {
			of_node_put(child);
			break;
		}
	}

	of_node_put(np);

	return err;
}

static void tegra_powergate_remove(struct generic_pm_domain *genpd)
{
	struct tegra_powergate *pg = to_powergate(genpd);

	reset_control_put(pg->reset);

	while (pg->num_clks--)
		clk_put(pg->clks[pg->num_clks]);

	kfree(pg->clks);

	set_bit(pg->id, pmc->powergates_available);

	kfree(pg);
}

static void tegra_powergate_remove_all(struct device_node *parent)
{
	struct generic_pm_domain *genpd;
	struct device_node *np, *child;

	np = of_get_child_by_name(parent, "powergates");
	if (!np)
		return;

	for_each_child_of_node(np, child) {
		of_genpd_del_provider(child);

		genpd = of_genpd_remove_last(child);
		if (IS_ERR(genpd))
			continue;

		tegra_powergate_remove(genpd);
	}

	of_node_put(np);

	np = of_get_child_by_name(parent, "core-domain");
	if (np) {
		of_genpd_del_provider(np);
		of_genpd_remove_last(np);
	}
}

static const struct tegra_io_pad_soc *
tegra_io_pad_find(struct tegra_pmc *pmc, enum tegra_io_pad id)
{
	unsigned int i;

	for (i = 0; i < pmc->soc->num_io_pads; i++)
		if (pmc->soc->io_pads[i].id == id)
			return &pmc->soc->io_pads[i];

	return NULL;
}

static int tegra_io_pad_get_dpd_register_bit(struct tegra_pmc *pmc,
					     enum tegra_io_pad id,
					     unsigned long *request,
					     unsigned long *status,
					     u32 *mask)
{
	const struct tegra_io_pad_soc *pad;

	pad = tegra_io_pad_find(pmc, id);
	if (!pad) {
		dev_err(pmc->dev, "invalid I/O pad ID %u\n", id);
		return -ENOENT;
	}

	if (pad->dpd == UINT_MAX)
		return -ENOTSUPP;

	*mask = BIT(pad->dpd % 32);

	if (pad->dpd < 32) {
		*status = pmc->soc->regs->dpd_status;
		*request = pmc->soc->regs->dpd_req;
	} else {
		*status = pmc->soc->regs->dpd2_status;
		*request = pmc->soc->regs->dpd2_req;
	}

	return 0;
}

static int tegra_io_pad_prepare(struct tegra_pmc *pmc, enum tegra_io_pad id,
				unsigned long *request, unsigned long *status,
				u32 *mask)
{
	unsigned long rate, value;
	int err;

	err = tegra_io_pad_get_dpd_register_bit(pmc, id, request, status, mask);
	if (err)
		return err;

	if (pmc->clk) {
		rate = pmc->rate;
		if (!rate) {
			dev_err(pmc->dev, "failed to get clock rate\n");
			return -ENODEV;
		}

		tegra_pmc_writel(pmc, DPD_SAMPLE_ENABLE, DPD_SAMPLE);

		/* must be at least 200 ns, in APB (PCLK) clock cycles */
		value = DIV_ROUND_UP(1000000000, rate);
		value = DIV_ROUND_UP(200, value);
		tegra_pmc_writel(pmc, value, SEL_DPD_TIM);
	}

	return 0;
}

static int tegra_io_pad_poll(struct tegra_pmc *pmc, unsigned long offset,
			     u32 mask, u32 val, unsigned long timeout)
{
	u32 value;

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_after(timeout, jiffies)) {
		value = tegra_pmc_readl(pmc, offset);
		if ((value & mask) == val)
			return 0;

		usleep_range(250, 1000);
	}

	return -ETIMEDOUT;
}

static void tegra_io_pad_unprepare(struct tegra_pmc *pmc)
{
	if (pmc->clk)
		tegra_pmc_writel(pmc, DPD_SAMPLE_DISABLE, DPD_SAMPLE);
}

/**
 * tegra_io_pad_power_enable() - enable power to I/O pad
 * @id: Tegra I/O pad ID for which to enable power
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int tegra_io_pad_power_enable(enum tegra_io_pad id)
{
	unsigned long request, status;
	u32 mask;
	int err;

	mutex_lock(&pmc->powergates_lock);

	err = tegra_io_pad_prepare(pmc, id, &request, &status, &mask);
	if (err < 0) {
		dev_err(pmc->dev, "failed to prepare I/O pad: %d\n", err);
		goto unlock;
	}

	tegra_pmc_writel(pmc, IO_DPD_REQ_CODE_OFF | mask, request);

	err = tegra_io_pad_poll(pmc, status, mask, 0, 250);
	if (err < 0) {
		dev_err(pmc->dev, "failed to enable I/O pad: %d\n", err);
		goto unlock;
	}

	tegra_io_pad_unprepare(pmc);

unlock:
	mutex_unlock(&pmc->powergates_lock);
	return err;
}
EXPORT_SYMBOL(tegra_io_pad_power_enable);

/**
 * tegra_io_pad_power_disable() - disable power to I/O pad
 * @id: Tegra I/O pad ID for which to disable power
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int tegra_io_pad_power_disable(enum tegra_io_pad id)
{
	unsigned long request, status;
	u32 mask;
	int err;

	mutex_lock(&pmc->powergates_lock);

	err = tegra_io_pad_prepare(pmc, id, &request, &status, &mask);
	if (err < 0) {
		dev_err(pmc->dev, "failed to prepare I/O pad: %d\n", err);
		goto unlock;
	}

	tegra_pmc_writel(pmc, IO_DPD_REQ_CODE_ON | mask, request);

	err = tegra_io_pad_poll(pmc, status, mask, mask, 250);
	if (err < 0) {
		dev_err(pmc->dev, "failed to disable I/O pad: %d\n", err);
		goto unlock;
	}

	tegra_io_pad_unprepare(pmc);

unlock:
	mutex_unlock(&pmc->powergates_lock);
	return err;
}
EXPORT_SYMBOL(tegra_io_pad_power_disable);

static int tegra_io_pad_is_powered(struct tegra_pmc *pmc, enum tegra_io_pad id)
{
	unsigned long request, status;
	u32 mask, value;
	int err;

	err = tegra_io_pad_get_dpd_register_bit(pmc, id, &request, &status,
						&mask);
	if (err)
		return err;

	value = tegra_pmc_readl(pmc, status);

	return !(value & mask);
}

static int tegra_io_pad_set_voltage(struct tegra_pmc *pmc, enum tegra_io_pad id,
				    int voltage)
{
	const struct tegra_io_pad_soc *pad;
	u32 value;

	pad = tegra_io_pad_find(pmc, id);
	if (!pad)
		return -ENOENT;

	if (pad->voltage == UINT_MAX)
		return -ENOTSUPP;

	mutex_lock(&pmc->powergates_lock);

	if (pmc->soc->has_impl_33v_pwr) {
		value = tegra_pmc_readl(pmc, PMC_IMPL_E_33V_PWR);

		if (voltage == TEGRA_IO_PAD_VOLTAGE_1V8)
			value &= ~BIT(pad->voltage);
		else
			value |= BIT(pad->voltage);

		tegra_pmc_writel(pmc, value, PMC_IMPL_E_33V_PWR);
	} else {
		/* write-enable PMC_PWR_DET_VALUE[pad->voltage] */
		value = tegra_pmc_readl(pmc, PMC_PWR_DET);
		value |= BIT(pad->voltage);
		tegra_pmc_writel(pmc, value, PMC_PWR_DET);

		/* update I/O voltage */
		value = tegra_pmc_readl(pmc, PMC_PWR_DET_VALUE);

		if (voltage == TEGRA_IO_PAD_VOLTAGE_1V8)
			value &= ~BIT(pad->voltage);
		else
			value |= BIT(pad->voltage);

		tegra_pmc_writel(pmc, value, PMC_PWR_DET_VALUE);
	}

	mutex_unlock(&pmc->powergates_lock);

	usleep_range(100, 250);

	return 0;
}

static int tegra_io_pad_get_voltage(struct tegra_pmc *pmc, enum tegra_io_pad id)
{
	const struct tegra_io_pad_soc *pad;
	u32 value;

	pad = tegra_io_pad_find(pmc, id);
	if (!pad)
		return -ENOENT;

	if (pad->voltage == UINT_MAX)
		return -ENOTSUPP;

	if (pmc->soc->has_impl_33v_pwr)
		value = tegra_pmc_readl(pmc, PMC_IMPL_E_33V_PWR);
	else
		value = tegra_pmc_readl(pmc, PMC_PWR_DET_VALUE);

	if ((value & BIT(pad->voltage)) == 0)
		return TEGRA_IO_PAD_VOLTAGE_1V8;

	return TEGRA_IO_PAD_VOLTAGE_3V3;
}

/**
 * tegra_io_rail_power_on() - enable power to I/O rail
 * @id: Tegra I/O pad ID for which to enable power
 *
 * See also: tegra_io_pad_power_enable()
 */
int tegra_io_rail_power_on(unsigned int id)
{
	return tegra_io_pad_power_enable(id);
}
EXPORT_SYMBOL(tegra_io_rail_power_on);

/**
 * tegra_io_rail_power_off() - disable power to I/O rail
 * @id: Tegra I/O pad ID for which to disable power
 *
 * See also: tegra_io_pad_power_disable()
 */
int tegra_io_rail_power_off(unsigned int id)
{
	return tegra_io_pad_power_disable(id);
}
EXPORT_SYMBOL(tegra_io_rail_power_off);

#ifdef CONFIG_PM_SLEEP
enum tegra_suspend_mode tegra_pmc_get_suspend_mode(void)
{
	return pmc->suspend_mode;
}

void tegra_pmc_set_suspend_mode(enum tegra_suspend_mode mode)
{
	if (mode < TEGRA_SUSPEND_NONE || mode >= TEGRA_MAX_SUSPEND_MODE)
		return;

	pmc->suspend_mode = mode;
}

void tegra_pmc_enter_suspend_mode(enum tegra_suspend_mode mode)
{
	unsigned long long rate = 0;
	u64 ticks;
	u32 value;

	switch (mode) {
	case TEGRA_SUSPEND_LP1:
		rate = 32768;
		break;

	case TEGRA_SUSPEND_LP2:
		rate = pmc->rate;
		break;

	default:
		break;
	}

	if (WARN_ON_ONCE(rate == 0))
		rate = 100000000;

	ticks = pmc->cpu_good_time * rate + USEC_PER_SEC - 1;
	do_div(ticks, USEC_PER_SEC);
	tegra_pmc_writel(pmc, ticks, PMC_CPUPWRGOOD_TIMER);

	ticks = pmc->cpu_off_time * rate + USEC_PER_SEC - 1;
	do_div(ticks, USEC_PER_SEC);
	tegra_pmc_writel(pmc, ticks, PMC_CPUPWROFF_TIMER);

	value = tegra_pmc_readl(pmc, PMC_CNTRL);
	value &= ~PMC_CNTRL_SIDE_EFFECT_LP0;
	value |= PMC_CNTRL_CPU_PWRREQ_OE;
	tegra_pmc_writel(pmc, value, PMC_CNTRL);
}
#endif

static int tegra_pmc_parse_dt(struct tegra_pmc *pmc, struct device_node *np)
{
	u32 value, values[2];

	if (of_property_read_u32(np, "nvidia,suspend-mode", &value)) {
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;
	} else {
		switch (value) {
		case 0:
			pmc->suspend_mode = TEGRA_SUSPEND_LP0;
			break;

		case 1:
			pmc->suspend_mode = TEGRA_SUSPEND_LP1;
			break;

		case 2:
			pmc->suspend_mode = TEGRA_SUSPEND_LP2;
			break;

		default:
			pmc->suspend_mode = TEGRA_SUSPEND_NONE;
			break;
		}
	}

	pmc->suspend_mode = tegra_pm_validate_suspend_mode(pmc->suspend_mode);

	if (of_property_read_u32(np, "nvidia,cpu-pwr-good-time", &value))
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;

	pmc->cpu_good_time = value;

	if (of_property_read_u32(np, "nvidia,cpu-pwr-off-time", &value))
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;

	pmc->cpu_off_time = value;

	if (of_property_read_u32_array(np, "nvidia,core-pwr-good-time",
				       values, ARRAY_SIZE(values)))
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;

	pmc->core_osc_time = values[0];
	pmc->core_pmu_time = values[1];

	if (of_property_read_u32(np, "nvidia,core-pwr-off-time", &value))
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;

	pmc->core_off_time = value;

	pmc->corereq_high = of_property_read_bool(np,
				"nvidia,core-power-req-active-high");

	pmc->sysclkreq_high = of_property_read_bool(np,
				"nvidia,sys-clock-req-active-high");

	pmc->combined_req = of_property_read_bool(np,
				"nvidia,combined-power-req");

	pmc->cpu_pwr_good_en = of_property_read_bool(np,
				"nvidia,cpu-pwr-good-en");

	if (of_property_read_u32_array(np, "nvidia,lp0-vec", values,
				       ARRAY_SIZE(values)))
		if (pmc->suspend_mode == TEGRA_SUSPEND_LP0)
			pmc->suspend_mode = TEGRA_SUSPEND_LP1;

	pmc->lp0_vec_phys = values[0];
	pmc->lp0_vec_size = values[1];

	return 0;
}

static void tegra_pmc_init(struct tegra_pmc *pmc)
{
	if (pmc->soc->init)
		pmc->soc->init(pmc);
}

static void tegra_pmc_init_tsense_reset(struct tegra_pmc *pmc)
{
	static const char disabled[] = "emergency thermal reset disabled";
	u32 pmu_addr, ctrl_id, reg_addr, reg_data, pinmux;
	struct device *dev = pmc->dev;
	struct device_node *np;
	u32 value, checksum;

	if (!pmc->soc->has_tsense_reset)
		return;

	np = of_get_child_by_name(pmc->dev->of_node, "i2c-thermtrip");
	if (!np) {
		dev_warn(dev, "i2c-thermtrip node not found, %s.\n", disabled);
		return;
	}

	if (of_property_read_u32(np, "nvidia,i2c-controller-id", &ctrl_id)) {
		dev_err(dev, "I2C controller ID missing, %s.\n", disabled);
		goto out;
	}

	if (of_property_read_u32(np, "nvidia,bus-addr", &pmu_addr)) {
		dev_err(dev, "nvidia,bus-addr missing, %s.\n", disabled);
		goto out;
	}

	if (of_property_read_u32(np, "nvidia,reg-addr", &reg_addr)) {
		dev_err(dev, "nvidia,reg-addr missing, %s.\n", disabled);
		goto out;
	}

	if (of_property_read_u32(np, "nvidia,reg-data", &reg_data)) {
		dev_err(dev, "nvidia,reg-data missing, %s.\n", disabled);
		goto out;
	}

	if (of_property_read_u32(np, "nvidia,pinmux-id", &pinmux))
		pinmux = 0;

	value = tegra_pmc_readl(pmc, PMC_SENSOR_CTRL);
	value |= PMC_SENSOR_CTRL_SCRATCH_WRITE;
	tegra_pmc_writel(pmc, value, PMC_SENSOR_CTRL);

	value = (reg_data << PMC_SCRATCH54_DATA_SHIFT) |
		(reg_addr << PMC_SCRATCH54_ADDR_SHIFT);
	tegra_pmc_writel(pmc, value, PMC_SCRATCH54);

	value = PMC_SCRATCH55_RESET_TEGRA;
	value |= ctrl_id << PMC_SCRATCH55_CNTRL_ID_SHIFT;
	value |= pinmux << PMC_SCRATCH55_PINMUX_SHIFT;
	value |= pmu_addr << PMC_SCRATCH55_I2CSLV1_SHIFT;

	/*
	 * Calculate checksum of SCRATCH54, SCRATCH55 fields. Bits 23:16 will
	 * contain the checksum and are currently zero, so they are not added.
	 */
	checksum = reg_addr + reg_data + (value & 0xff) + ((value >> 8) & 0xff)
		+ ((value >> 24) & 0xff);
	checksum &= 0xff;
	checksum = 0x100 - checksum;

	value |= checksum << PMC_SCRATCH55_CHECKSUM_SHIFT;

	tegra_pmc_writel(pmc, value, PMC_SCRATCH55);

	value = tegra_pmc_readl(pmc, PMC_SENSOR_CTRL);
	value |= PMC_SENSOR_CTRL_ENABLE_RST;
	tegra_pmc_writel(pmc, value, PMC_SENSOR_CTRL);

	dev_info(pmc->dev, "emergency thermal reset enabled\n");

out:
	of_node_put(np);
}

static int tegra_io_pad_pinctrl_get_groups_count(struct pinctrl_dev *pctl_dev)
{
	struct tegra_pmc *pmc = pinctrl_dev_get_drvdata(pctl_dev);

	return pmc->soc->num_io_pads;
}

static const char *tegra_io_pad_pinctrl_get_group_name(struct pinctrl_dev *pctl,
						       unsigned int group)
{
	struct tegra_pmc *pmc = pinctrl_dev_get_drvdata(pctl);

	return pmc->soc->io_pads[group].name;
}

static int tegra_io_pad_pinctrl_get_group_pins(struct pinctrl_dev *pctl_dev,
					       unsigned int group,
					       const unsigned int **pins,
					       unsigned int *num_pins)
{
	struct tegra_pmc *pmc = pinctrl_dev_get_drvdata(pctl_dev);

	*pins = &pmc->soc->io_pads[group].id;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops tegra_io_pad_pinctrl_ops = {
	.get_groups_count = tegra_io_pad_pinctrl_get_groups_count,
	.get_group_name = tegra_io_pad_pinctrl_get_group_name,
	.get_group_pins = tegra_io_pad_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int tegra_io_pad_pinconf_get(struct pinctrl_dev *pctl_dev,
				    unsigned int pin, unsigned long *config)
{
	enum pin_config_param param = pinconf_to_config_param(*config);
	struct tegra_pmc *pmc = pinctrl_dev_get_drvdata(pctl_dev);
	const struct tegra_io_pad_soc *pad;
	int ret;
	u32 arg;

	pad = tegra_io_pad_find(pmc, pin);
	if (!pad)
		return -EINVAL;

	switch (param) {
	case PIN_CONFIG_POWER_SOURCE:
		ret = tegra_io_pad_get_voltage(pmc, pad->id);
		if (ret < 0)
			return ret;

		arg = ret;
		break;

	case PIN_CONFIG_MODE_LOW_POWER:
		ret = tegra_io_pad_is_powered(pmc, pad->id);
		if (ret < 0)
			return ret;

		arg = !ret;
		break;

	default:
		return -EINVAL;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int tegra_io_pad_pinconf_set(struct pinctrl_dev *pctl_dev,
				    unsigned int pin, unsigned long *configs,
				    unsigned int num_configs)
{
	struct tegra_pmc *pmc = pinctrl_dev_get_drvdata(pctl_dev);
	const struct tegra_io_pad_soc *pad;
	enum pin_config_param param;
	unsigned int i;
	int err;
	u32 arg;

	pad = tegra_io_pad_find(pmc, pin);
	if (!pad)
		return -EINVAL;

	for (i = 0; i < num_configs; ++i) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_MODE_LOW_POWER:
			if (arg)
				err = tegra_io_pad_power_disable(pad->id);
			else
				err = tegra_io_pad_power_enable(pad->id);
			if (err)
				return err;
			break;
		case PIN_CONFIG_POWER_SOURCE:
			if (arg != TEGRA_IO_PAD_VOLTAGE_1V8 &&
			    arg != TEGRA_IO_PAD_VOLTAGE_3V3)
				return -EINVAL;
			err = tegra_io_pad_set_voltage(pmc, pad->id, arg);
			if (err)
				return err;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static const struct pinconf_ops tegra_io_pad_pinconf_ops = {
	.pin_config_get = tegra_io_pad_pinconf_get,
	.pin_config_set = tegra_io_pad_pinconf_set,
	.is_generic = true,
};

static struct pinctrl_desc tegra_pmc_pctl_desc = {
	.pctlops = &tegra_io_pad_pinctrl_ops,
	.confops = &tegra_io_pad_pinconf_ops,
};

static int tegra_pmc_pinctrl_init(struct tegra_pmc *pmc)
{
	int err;

	if (!pmc->soc->num_pin_descs)
		return 0;

	tegra_pmc_pctl_desc.name = dev_name(pmc->dev);
	tegra_pmc_pctl_desc.pins = pmc->soc->pin_descs;
	tegra_pmc_pctl_desc.npins = pmc->soc->num_pin_descs;

	pmc->pctl_dev = devm_pinctrl_register(pmc->dev, &tegra_pmc_pctl_desc,
					      pmc);
	if (IS_ERR(pmc->pctl_dev)) {
		err = PTR_ERR(pmc->pctl_dev);
		dev_err(pmc->dev, "failed to register pin controller: %d\n",
			err);
		return err;
	}

	return 0;
}

static ssize_t reset_reason_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	u32 value;

	value = tegra_pmc_readl(pmc, pmc->soc->regs->rst_status);
	value &= pmc->soc->regs->rst_source_mask;
	value >>= pmc->soc->regs->rst_source_shift;

	if (WARN_ON(value >= pmc->soc->num_reset_sources))
		return sprintf(buf, "%s\n", "UNKNOWN");

	return sprintf(buf, "%s\n", pmc->soc->reset_sources[value]);
}

static DEVICE_ATTR_RO(reset_reason);

static ssize_t reset_level_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u32 value;

	value = tegra_pmc_readl(pmc, pmc->soc->regs->rst_status);
	value &= pmc->soc->regs->rst_level_mask;
	value >>= pmc->soc->regs->rst_level_shift;

	if (WARN_ON(value >= pmc->soc->num_reset_levels))
		return sprintf(buf, "%s\n", "UNKNOWN");

	return sprintf(buf, "%s\n", pmc->soc->reset_levels[value]);
}

static DEVICE_ATTR_RO(reset_level);

static void tegra_pmc_reset_sysfs_init(struct tegra_pmc *pmc)
{
	struct device *dev = pmc->dev;
	int err = 0;

	if (pmc->soc->reset_sources) {
		err = device_create_file(dev, &dev_attr_reset_reason);
		if (err < 0)
			dev_warn(dev,
				 "failed to create attr \"reset_reason\": %d\n",
				 err);
	}

	if (pmc->soc->reset_levels) {
		err = device_create_file(dev, &dev_attr_reset_level);
		if (err < 0)
			dev_warn(dev,
				 "failed to create attr \"reset_level\": %d\n",
				 err);
	}
}

static int tegra_pmc_irq_translate(struct irq_domain *domain,
				   struct irq_fwspec *fwspec,
				   unsigned long *hwirq,
				   unsigned int *type)
{
	if (WARN_ON(fwspec->param_count < 2))
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];

	return 0;
}

static int tegra_pmc_irq_alloc(struct irq_domain *domain, unsigned int virq,
			       unsigned int num_irqs, void *data)
{
	struct tegra_pmc *pmc = domain->host_data;
	const struct tegra_pmc_soc *soc = pmc->soc;
	struct irq_fwspec *fwspec = data;
	unsigned int i;
	int err = 0;

	if (WARN_ON(num_irqs > 1))
		return -EINVAL;

	for (i = 0; i < soc->num_wake_events; i++) {
		const struct tegra_wake_event *event = &soc->wake_events[i];

		if (fwspec->param_count == 2) {
			struct irq_fwspec spec;

			if (event->id != fwspec->param[0])
				continue;

			err = irq_domain_set_hwirq_and_chip(domain, virq,
							    event->id,
							    &pmc->irq, pmc);
			if (err < 0)
				break;

			spec.fwnode = &pmc->dev->of_node->fwnode;
			spec.param_count = 3;
			spec.param[0] = GIC_SPI;
			spec.param[1] = event->irq;
			spec.param[2] = fwspec->param[1];

			err = irq_domain_alloc_irqs_parent(domain, virq,
							   num_irqs, &spec);

			break;
		}

		if (fwspec->param_count == 3) {
			if (event->gpio.instance != fwspec->param[0] ||
			    event->gpio.pin != fwspec->param[1])
				continue;

			err = irq_domain_set_hwirq_and_chip(domain, virq,
							    event->id,
							    &pmc->irq, pmc);

			/* GPIO hierarchies stop at the PMC level */
			if (!err && domain->parent)
 				err = irq_domain_disconnect_hierarchy(domain->parent,
								      virq);
			break;
		}
	}

	/* If there is no wake-up event, there is no PMC mapping */
	if (i == soc->num_wake_events)
		err = irq_domain_disconnect_hierarchy(domain, virq);

	return err;
}

static const struct irq_domain_ops tegra_pmc_irq_domain_ops = {
	.translate = tegra_pmc_irq_translate,
	.alloc = tegra_pmc_irq_alloc,
};

static int tegra210_pmc_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct tegra_pmc *pmc = irq_data_get_irq_chip_data(data);
	unsigned int offset, bit;
	u32 value;

	offset = data->hwirq / 32;
	bit = data->hwirq % 32;

	/* clear wake status */
	tegra_pmc_writel(pmc, 0, PMC_SW_WAKE_STATUS);
	tegra_pmc_writel(pmc, 0, PMC_SW_WAKE2_STATUS);

	tegra_pmc_writel(pmc, 0, PMC_WAKE_STATUS);
	tegra_pmc_writel(pmc, 0, PMC_WAKE2_STATUS);

	/* enable PMC wake */
	if (data->hwirq >= 32)
		offset = PMC_WAKE2_MASK;
	else
		offset = PMC_WAKE_MASK;

	value = tegra_pmc_readl(pmc, offset);

	if (on)
		value |= BIT(bit);
	else
		value &= ~BIT(bit);

	tegra_pmc_writel(pmc, value, offset);

	return 0;
}

static int tegra210_pmc_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct tegra_pmc *pmc = irq_data_get_irq_chip_data(data);
	unsigned int offset, bit;
	u32 value;

	offset = data->hwirq / 32;
	bit = data->hwirq % 32;

	if (data->hwirq >= 32)
		offset = PMC_WAKE2_LEVEL;
	else
		offset = PMC_WAKE_LEVEL;

	value = tegra_pmc_readl(pmc, offset);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		value |= BIT(bit);
		break;

	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		value &= ~BIT(bit);
		break;

	case IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING:
		value ^= BIT(bit);
		break;

	default:
		return -EINVAL;
	}

	tegra_pmc_writel(pmc, value, offset);

	return 0;
}

static int tegra186_pmc_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct tegra_pmc *pmc = irq_data_get_irq_chip_data(data);
	unsigned int offset, bit;
	u32 value;

	offset = data->hwirq / 32;
	bit = data->hwirq % 32;

	/* clear wake status */
	writel(0x1, pmc->wake + WAKE_AOWAKE_STATUS_W(data->hwirq));

	/* route wake to tier 2 */
	value = readl(pmc->wake + WAKE_AOWAKE_TIER2_ROUTING(offset));

	if (!on)
		value &= ~(1 << bit);
	else
		value |= 1 << bit;

	writel(value, pmc->wake + WAKE_AOWAKE_TIER2_ROUTING(offset));

	/* enable wakeup event */
	writel(!!on, pmc->wake + WAKE_AOWAKE_MASK_W(data->hwirq));

	return 0;
}

static int tegra186_pmc_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct tegra_pmc *pmc = irq_data_get_irq_chip_data(data);
	u32 value;

	value = readl(pmc->wake + WAKE_AOWAKE_CNTRL(data->hwirq));

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		value |= WAKE_AOWAKE_CNTRL_LEVEL;
		break;

	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		value &= ~WAKE_AOWAKE_CNTRL_LEVEL;
		break;

	case IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING:
		value ^= WAKE_AOWAKE_CNTRL_LEVEL;
		break;

	default:
		return -EINVAL;
	}

	writel(value, pmc->wake + WAKE_AOWAKE_CNTRL(data->hwirq));

	return 0;
}

static void tegra_irq_mask_parent(struct irq_data *data)
{
	if (data->parent_data)
		irq_chip_mask_parent(data);
}

static void tegra_irq_unmask_parent(struct irq_data *data)
{
	if (data->parent_data)
		irq_chip_unmask_parent(data);
}

static void tegra_irq_eoi_parent(struct irq_data *data)
{
	if (data->parent_data)
		irq_chip_eoi_parent(data);
}

static int tegra_irq_set_affinity_parent(struct irq_data *data,
					 const struct cpumask *dest,
					 bool force)
{
	if (data->parent_data)
		return irq_chip_set_affinity_parent(data, dest, force);

	return -EINVAL;
}

static int tegra_pmc_irq_init(struct tegra_pmc *pmc)
{
	struct irq_domain *parent = NULL;
	struct device_node *np;

	np = of_irq_find_parent(pmc->dev->of_node);
	if (np) {
		parent = irq_find_host(np);
		of_node_put(np);
	}

	if (!parent)
		return 0;

	pmc->irq.name = dev_name(pmc->dev);
	pmc->irq.irq_mask = tegra_irq_mask_parent;
	pmc->irq.irq_unmask = tegra_irq_unmask_parent;
	pmc->irq.irq_eoi = tegra_irq_eoi_parent;
	pmc->irq.irq_set_affinity = tegra_irq_set_affinity_parent;
	pmc->irq.irq_set_type = pmc->soc->irq_set_type;
	pmc->irq.irq_set_wake = pmc->soc->irq_set_wake;

	pmc->domain = irq_domain_add_hierarchy(parent, 0, 96, pmc->dev->of_node,
					       &tegra_pmc_irq_domain_ops, pmc);
	if (!pmc->domain) {
		dev_err(pmc->dev, "failed to allocate domain\n");
		return -ENOMEM;
	}

	return 0;
}

static int tegra_pmc_clk_notify_cb(struct notifier_block *nb,
				   unsigned long action, void *ptr)
{
	struct tegra_pmc *pmc = container_of(nb, struct tegra_pmc, clk_nb);
	struct clk_notifier_data *data = ptr;

	switch (action) {
	case PRE_RATE_CHANGE:
		mutex_lock(&pmc->powergates_lock);
		break;

	case POST_RATE_CHANGE:
		pmc->rate = data->new_rate;
		fallthrough;

	case ABORT_RATE_CHANGE:
		mutex_unlock(&pmc->powergates_lock);
		break;

	default:
		WARN_ON_ONCE(1);
		return notifier_from_errno(-EINVAL);
	}

	return NOTIFY_OK;
}

static void pmc_clk_fence_udelay(u32 offset)
{
	tegra_pmc_readl(pmc, offset);
	/* pmc clk propagation delay 2 us */
	udelay(2);
}

static u8 pmc_clk_mux_get_parent(struct clk_hw *hw)
{
	struct pmc_clk *clk = to_pmc_clk(hw);
	u32 val;

	val = tegra_pmc_readl(pmc, clk->offs) >> clk->mux_shift;
	val &= PMC_CLK_OUT_MUX_MASK;

	return val;
}

static int pmc_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct pmc_clk *clk = to_pmc_clk(hw);
	u32 val;

	val = tegra_pmc_readl(pmc, clk->offs);
	val &= ~(PMC_CLK_OUT_MUX_MASK << clk->mux_shift);
	val |= index << clk->mux_shift;
	tegra_pmc_writel(pmc, val, clk->offs);
	pmc_clk_fence_udelay(clk->offs);

	return 0;
}

static int pmc_clk_is_enabled(struct clk_hw *hw)
{
	struct pmc_clk *clk = to_pmc_clk(hw);
	u32 val;

	val = tegra_pmc_readl(pmc, clk->offs) & BIT(clk->force_en_shift);

	return val ? 1 : 0;
}

static void pmc_clk_set_state(unsigned long offs, u32 shift, int state)
{
	u32 val;

	val = tegra_pmc_readl(pmc, offs);
	val = state ? (val | BIT(shift)) : (val & ~BIT(shift));
	tegra_pmc_writel(pmc, val, offs);
	pmc_clk_fence_udelay(offs);
}

static int pmc_clk_enable(struct clk_hw *hw)
{
	struct pmc_clk *clk = to_pmc_clk(hw);

	pmc_clk_set_state(clk->offs, clk->force_en_shift, 1);

	return 0;
}

static void pmc_clk_disable(struct clk_hw *hw)
{
	struct pmc_clk *clk = to_pmc_clk(hw);

	pmc_clk_set_state(clk->offs, clk->force_en_shift, 0);
}

static const struct clk_ops pmc_clk_ops = {
	.get_parent = pmc_clk_mux_get_parent,
	.set_parent = pmc_clk_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
	.is_enabled = pmc_clk_is_enabled,
	.enable = pmc_clk_enable,
	.disable = pmc_clk_disable,
};

static struct clk *
tegra_pmc_clk_out_register(struct tegra_pmc *pmc,
			   const struct pmc_clk_init_data *data,
			   unsigned long offset)
{
	struct clk_init_data init;
	struct pmc_clk *pmc_clk;

	pmc_clk = devm_kzalloc(pmc->dev, sizeof(*pmc_clk), GFP_KERNEL);
	if (!pmc_clk)
		return ERR_PTR(-ENOMEM);

	init.name = data->name;
	init.ops = &pmc_clk_ops;
	init.parent_names = data->parents;
	init.num_parents = data->num_parents;
	init.flags = CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT |
		     CLK_SET_PARENT_GATE;

	pmc_clk->hw.init = &init;
	pmc_clk->offs = offset;
	pmc_clk->mux_shift = data->mux_shift;
	pmc_clk->force_en_shift = data->force_en_shift;

	return clk_register(NULL, &pmc_clk->hw);
}

static int pmc_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct pmc_clk_gate *gate = to_pmc_clk_gate(hw);

	return tegra_pmc_readl(pmc, gate->offs) & BIT(gate->shift) ? 1 : 0;
}

static int pmc_clk_gate_enable(struct clk_hw *hw)
{
	struct pmc_clk_gate *gate = to_pmc_clk_gate(hw);

	pmc_clk_set_state(gate->offs, gate->shift, 1);

	return 0;
}

static void pmc_clk_gate_disable(struct clk_hw *hw)
{
	struct pmc_clk_gate *gate = to_pmc_clk_gate(hw);

	pmc_clk_set_state(gate->offs, gate->shift, 0);
}

static const struct clk_ops pmc_clk_gate_ops = {
	.is_enabled = pmc_clk_gate_is_enabled,
	.enable = pmc_clk_gate_enable,
	.disable = pmc_clk_gate_disable,
};

static struct clk *
tegra_pmc_clk_gate_register(struct tegra_pmc *pmc, const char *name,
			    const char *parent_name, unsigned long offset,
			    u32 shift)
{
	struct clk_init_data init;
	struct pmc_clk_gate *gate;

	gate = devm_kzalloc(pmc->dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &pmc_clk_gate_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;

	gate->hw.init = &init;
	gate->offs = offset;
	gate->shift = shift;

	return clk_register(NULL, &gate->hw);
}

static void tegra_pmc_clock_register(struct tegra_pmc *pmc,
				     struct device_node *np)
{
	struct clk *clk;
	struct clk_onecell_data *clk_data;
	unsigned int num_clks;
	int i, err;

	num_clks = pmc->soc->num_pmc_clks;
	if (pmc->soc->has_blink_output)
		num_clks += 1;

	if (!num_clks)
		return;

	clk_data = devm_kmalloc(pmc->dev, sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->clks = devm_kcalloc(pmc->dev, TEGRA_PMC_CLK_MAX,
				      sizeof(*clk_data->clks), GFP_KERNEL);
	if (!clk_data->clks)
		return;

	clk_data->clk_num = TEGRA_PMC_CLK_MAX;

	for (i = 0; i < TEGRA_PMC_CLK_MAX; i++)
		clk_data->clks[i] = ERR_PTR(-ENOENT);

	for (i = 0; i < pmc->soc->num_pmc_clks; i++) {
		const struct pmc_clk_init_data *data;

		data = pmc->soc->pmc_clks_data + i;

		clk = tegra_pmc_clk_out_register(pmc, data, PMC_CLK_OUT_CNTRL);
		if (IS_ERR(clk)) {
			dev_warn(pmc->dev, "unable to register clock %s: %d\n",
				 data->name, PTR_ERR_OR_ZERO(clk));
			return;
		}

		err = clk_register_clkdev(clk, data->name, NULL);
		if (err) {
			dev_warn(pmc->dev,
				 "unable to register %s clock lookup: %d\n",
				 data->name, err);
			return;
		}

		clk_data->clks[data->clk_id] = clk;
	}

	if (pmc->soc->has_blink_output) {
		tegra_pmc_writel(pmc, 0x0, PMC_BLINK_TIMER);
		clk = tegra_pmc_clk_gate_register(pmc,
						  "pmc_blink_override",
						  "clk_32k",
						  PMC_DPD_PADS_ORIDE,
						  PMC_DPD_PADS_ORIDE_BLINK);
		if (IS_ERR(clk)) {
			dev_warn(pmc->dev,
				 "unable to register pmc_blink_override: %d\n",
				 PTR_ERR_OR_ZERO(clk));
			return;
		}

		clk = tegra_pmc_clk_gate_register(pmc, "pmc_blink",
						  "pmc_blink_override",
						  PMC_CNTRL,
						  PMC_CNTRL_BLINK_EN);
		if (IS_ERR(clk)) {
			dev_warn(pmc->dev,
				 "unable to register pmc_blink: %d\n",
				 PTR_ERR_OR_ZERO(clk));
			return;
		}

		err = clk_register_clkdev(clk, "pmc_blink", NULL);
		if (err) {
			dev_warn(pmc->dev,
				 "unable to register pmc_blink lookup: %d\n",
				 err);
			return;
		}

		clk_data->clks[TEGRA_PMC_CLK_BLINK] = clk;
	}

	err = of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
	if (err)
		dev_warn(pmc->dev, "failed to add pmc clock provider: %d\n",
			 err);
}

static const struct regmap_range pmc_usb_sleepwalk_ranges[] = {
	regmap_reg_range(PMC_USB_DEBOUNCE_DEL, PMC_USB_AO),
	regmap_reg_range(PMC_UTMIP_UHSIC_TRIGGERS, PMC_UTMIP_UHSIC_SAVED_STATE),
	regmap_reg_range(PMC_UTMIP_TERM_PAD_CFG, PMC_UTMIP_UHSIC_FAKE),
	regmap_reg_range(PMC_UTMIP_UHSIC_LINE_WAKEUP, PMC_UTMIP_UHSIC_LINE_WAKEUP),
	regmap_reg_range(PMC_UTMIP_BIAS_MASTER_CNTRL, PMC_UTMIP_MASTER_CONFIG),
	regmap_reg_range(PMC_UTMIP_UHSIC2_TRIGGERS, PMC_UTMIP_MASTER2_CONFIG),
	regmap_reg_range(PMC_UTMIP_PAD_CFG0, PMC_UTMIP_UHSIC_SLEEP_CFG1),
	regmap_reg_range(PMC_UTMIP_SLEEPWALK_P3, PMC_UTMIP_SLEEPWALK_P3),
};

static const struct regmap_access_table pmc_usb_sleepwalk_table = {
	.yes_ranges = pmc_usb_sleepwalk_ranges,
	.n_yes_ranges = ARRAY_SIZE(pmc_usb_sleepwalk_ranges),
};

static int tegra_pmc_regmap_readl(void *context, unsigned int offset, unsigned int *value)
{
	struct tegra_pmc *pmc = context;

	*value = tegra_pmc_readl(pmc, offset);
	return 0;
}

static int tegra_pmc_regmap_writel(void *context, unsigned int offset, unsigned int value)
{
	struct tegra_pmc *pmc = context;

	tegra_pmc_writel(pmc, value, offset);
	return 0;
}

static const struct regmap_config usb_sleepwalk_regmap_config = {
	.name = "usb_sleepwalk",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
	.rd_table = &pmc_usb_sleepwalk_table,
	.wr_table = &pmc_usb_sleepwalk_table,
	.reg_read = tegra_pmc_regmap_readl,
	.reg_write = tegra_pmc_regmap_writel,
};

static int tegra_pmc_regmap_init(struct tegra_pmc *pmc)
{
	struct regmap *regmap;
	int err;

	if (pmc->soc->has_usb_sleepwalk) {
		regmap = devm_regmap_init(pmc->dev, NULL, pmc, &usb_sleepwalk_regmap_config);
		if (IS_ERR(regmap)) {
			err = PTR_ERR(regmap);
			dev_err(pmc->dev, "failed to allocate register map (%d)\n", err);
			return err;
		}
	}

	return 0;
}

static void tegra_pmc_reset_suspend_mode(void *data)
{
	pmc->suspend_mode = TEGRA_SUSPEND_NOT_READY;
}

static int tegra_pmc_probe(struct platform_device *pdev)
{
	void __iomem *base;
	struct resource *res;
	int err;

	/*
	 * Early initialisation should have configured an initial
	 * register mapping and setup the soc data pointer. If these
	 * are not valid then something went badly wrong!
	 */
	if (WARN_ON(!pmc->base || !pmc->soc))
		return -ENODEV;

	err = tegra_pmc_parse_dt(pmc, pdev->dev.of_node);
	if (err < 0)
		return err;

	err = devm_add_action_or_reset(&pdev->dev, tegra_pmc_reset_suspend_mode,
				       NULL);
	if (err)
		return err;

	/* take over the memory region from the early initialization */
	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "wake");
	if (res) {
		pmc->wake = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pmc->wake))
			return PTR_ERR(pmc->wake);
	} else {
		pmc->wake = base;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aotag");
	if (res) {
		pmc->aotag = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pmc->aotag))
			return PTR_ERR(pmc->aotag);
	} else {
		pmc->aotag = base;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "scratch");
	if (res) {
		pmc->scratch = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pmc->scratch))
			return PTR_ERR(pmc->scratch);
	} else {
		pmc->scratch = base;
	}

	pmc->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pmc->clk)) {
		err = PTR_ERR(pmc->clk);

		if (err != -ENOENT) {
			dev_err(&pdev->dev, "failed to get pclk: %d\n", err);
			return err;
		}

		pmc->clk = NULL;
	}

	/*
	 * PMC should be last resort for restarting since it soft-resets
	 * CPU without resetting everything else.
	 */
	err = devm_register_reboot_notifier(&pdev->dev,
					    &tegra_pmc_reboot_notifier);
	if (err) {
		dev_err(&pdev->dev, "unable to register reboot notifier, %d\n",
			err);
		return err;
	}

	err = devm_register_sys_off_handler(&pdev->dev,
					    SYS_OFF_MODE_RESTART,
					    SYS_OFF_PRIO_LOW,
					    tegra_pmc_restart_handler, NULL);
	if (err) {
		dev_err(&pdev->dev, "failed to register sys-off handler: %d\n",
			err);
		return err;
	}

	/*
	 * PMC should be primary power-off method if it soft-resets CPU,
	 * asking bootloader to shutdown hardware.
	 */
	err = devm_register_sys_off_handler(&pdev->dev,
					    SYS_OFF_MODE_POWER_OFF,
					    SYS_OFF_PRIO_FIRMWARE,
					    tegra_pmc_power_off_handler, NULL);
	if (err) {
		dev_err(&pdev->dev, "failed to register sys-off handler: %d\n",
			err);
		return err;
	}

	/*
	 * PCLK clock rate can't be retrieved using CLK API because it
	 * causes lockup if CPU enters LP2 idle state from some other
	 * CLK notifier, hence we're caching the rate's value locally.
	 */
	if (pmc->clk) {
		pmc->clk_nb.notifier_call = tegra_pmc_clk_notify_cb;
		err = clk_notifier_register(pmc->clk, &pmc->clk_nb);
		if (err) {
			dev_err(&pdev->dev,
				"failed to register clk notifier\n");
			return err;
		}

		pmc->rate = clk_get_rate(pmc->clk);
	}

	pmc->dev = &pdev->dev;

	tegra_pmc_init(pmc);

	tegra_pmc_init_tsense_reset(pmc);

	tegra_pmc_reset_sysfs_init(pmc);

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_powergate_debugfs_init();
		if (err < 0)
			goto cleanup_sysfs;
	}

	err = tegra_pmc_pinctrl_init(pmc);
	if (err)
		goto cleanup_debugfs;

	err = tegra_pmc_regmap_init(pmc);
	if (err < 0)
		goto cleanup_debugfs;

	err = tegra_powergate_init(pmc, pdev->dev.of_node);
	if (err < 0)
		goto cleanup_powergates;

	err = tegra_pmc_irq_init(pmc);
	if (err < 0)
		goto cleanup_powergates;

	mutex_lock(&pmc->powergates_lock);
	iounmap(pmc->base);
	pmc->base = base;
	mutex_unlock(&pmc->powergates_lock);

	tegra_pmc_clock_register(pmc, pdev->dev.of_node);
	platform_set_drvdata(pdev, pmc);
	tegra_pm_init_suspend();

	return 0;

cleanup_powergates:
	tegra_powergate_remove_all(pdev->dev.of_node);
cleanup_debugfs:
	debugfs_remove(pmc->debugfs);
cleanup_sysfs:
	device_remove_file(&pdev->dev, &dev_attr_reset_reason);
	device_remove_file(&pdev->dev, &dev_attr_reset_level);
	clk_notifier_unregister(pmc->clk, &pmc->clk_nb);

	return err;
}

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_ARM)
static int tegra_pmc_suspend(struct device *dev)
{
	struct tegra_pmc *pmc = dev_get_drvdata(dev);

	tegra_pmc_writel(pmc, virt_to_phys(tegra_resume), PMC_SCRATCH41);

	return 0;
}

static int tegra_pmc_resume(struct device *dev)
{
	struct tegra_pmc *pmc = dev_get_drvdata(dev);

	tegra_pmc_writel(pmc, 0x0, PMC_SCRATCH41);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tegra_pmc_pm_ops, tegra_pmc_suspend, tegra_pmc_resume);

#endif

static const char * const tegra20_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "cpu",
	[TEGRA_POWERGATE_3D] = "td",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_VDEC] = "vdec",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_L2] = "l2",
	[TEGRA_POWERGATE_MPE] = "mpe",
};

static const struct tegra_pmc_regs tegra20_pmc_regs = {
	.scratch0 = 0x50,
	.dpd_req = 0x1b8,
	.dpd_status = 0x1bc,
	.dpd2_req = 0x1c0,
	.dpd2_status = 0x1c4,
	.rst_status = 0x1b4,
	.rst_source_shift = 0x0,
	.rst_source_mask = 0x7,
	.rst_level_shift = 0x0,
	.rst_level_mask = 0x0,
};

static void tegra20_pmc_init(struct tegra_pmc *pmc)
{
	u32 value, osc, pmu, off;

	/* Always enable CPU power request */
	value = tegra_pmc_readl(pmc, PMC_CNTRL);
	value |= PMC_CNTRL_CPU_PWRREQ_OE;
	tegra_pmc_writel(pmc, value, PMC_CNTRL);

	value = tegra_pmc_readl(pmc, PMC_CNTRL);

	if (pmc->sysclkreq_high)
		value &= ~PMC_CNTRL_SYSCLK_POLARITY;
	else
		value |= PMC_CNTRL_SYSCLK_POLARITY;

	if (pmc->corereq_high)
		value &= ~PMC_CNTRL_PWRREQ_POLARITY;
	else
		value |= PMC_CNTRL_PWRREQ_POLARITY;

	/* configure the output polarity while the request is tristated */
	tegra_pmc_writel(pmc, value, PMC_CNTRL);

	/* now enable the request */
	value = tegra_pmc_readl(pmc, PMC_CNTRL);
	value |= PMC_CNTRL_SYSCLK_OE;
	tegra_pmc_writel(pmc, value, PMC_CNTRL);

	/* program core timings which are applicable only for suspend state */
	if (pmc->suspend_mode != TEGRA_SUSPEND_NONE) {
		osc = DIV_ROUND_UP(pmc->core_osc_time * 8192, 1000000);
		pmu = DIV_ROUND_UP(pmc->core_pmu_time * 32768, 1000000);
		off = DIV_ROUND_UP(pmc->core_off_time * 32768, 1000000);
		tegra_pmc_writel(pmc, ((osc << 8) & 0xff00) | (pmu & 0xff),
				 PMC_COREPWRGOOD_TIMER);
		tegra_pmc_writel(pmc, off, PMC_COREPWROFF_TIMER);
	}
}

static void tegra20_pmc_setup_irq_polarity(struct tegra_pmc *pmc,
					   struct device_node *np,
					   bool invert)
{
	u32 value;

	value = tegra_pmc_readl(pmc, PMC_CNTRL);

	if (invert)
		value |= PMC_CNTRL_INTR_POLARITY;
	else
		value &= ~PMC_CNTRL_INTR_POLARITY;

	tegra_pmc_writel(pmc, value, PMC_CNTRL);
}

static const struct tegra_pmc_soc tegra20_pmc_soc = {
	.supports_core_domain = true,
	.num_powergates = ARRAY_SIZE(tegra20_powergates),
	.powergates = tegra20_powergates,
	.num_cpu_powergates = 0,
	.cpu_powergates = NULL,
	.has_tsense_reset = false,
	.has_gpu_clamps = false,
	.needs_mbist_war = false,
	.has_impl_33v_pwr = false,
	.maybe_tz_only = false,
	.num_io_pads = 0,
	.io_pads = NULL,
	.num_pin_descs = 0,
	.pin_descs = NULL,
	.regs = &tegra20_pmc_regs,
	.init = tegra20_pmc_init,
	.setup_irq_polarity = tegra20_pmc_setup_irq_polarity,
	.powergate_set = tegra20_powergate_set,
	.reset_sources = NULL,
	.num_reset_sources = 0,
	.reset_levels = NULL,
	.num_reset_levels = 0,
	.pmc_clks_data = NULL,
	.num_pmc_clks = 0,
	.has_blink_output = true,
	.has_usb_sleepwalk = true,
};

static const char * const tegra30_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "cpu0",
	[TEGRA_POWERGATE_3D] = "td",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_VDEC] = "vdec",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_L2] = "l2",
	[TEGRA_POWERGATE_MPE] = "mpe",
	[TEGRA_POWERGATE_HEG] = "heg",
	[TEGRA_POWERGATE_SATA] = "sata",
	[TEGRA_POWERGATE_CPU1] = "cpu1",
	[TEGRA_POWERGATE_CPU2] = "cpu2",
	[TEGRA_POWERGATE_CPU3] = "cpu3",
	[TEGRA_POWERGATE_CELP] = "celp",
	[TEGRA_POWERGATE_3D1] = "td2",
};

static const u8 tegra30_cpu_powergates[] = {
	TEGRA_POWERGATE_CPU,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static const char * const tegra30_reset_sources[] = {
	"POWER_ON_RESET",
	"WATCHDOG",
	"SENSOR",
	"SW_MAIN",
	"LP0"
};

static const struct tegra_pmc_soc tegra30_pmc_soc = {
	.supports_core_domain = true,
	.num_powergates = ARRAY_SIZE(tegra30_powergates),
	.powergates = tegra30_powergates,
	.num_cpu_powergates = ARRAY_SIZE(tegra30_cpu_powergates),
	.cpu_powergates = tegra30_cpu_powergates,
	.has_tsense_reset = true,
	.has_gpu_clamps = false,
	.needs_mbist_war = false,
	.has_impl_33v_pwr = false,
	.maybe_tz_only = false,
	.num_io_pads = 0,
	.io_pads = NULL,
	.num_pin_descs = 0,
	.pin_descs = NULL,
	.regs = &tegra20_pmc_regs,
	.init = tegra20_pmc_init,
	.setup_irq_polarity = tegra20_pmc_setup_irq_polarity,
	.powergate_set = tegra20_powergate_set,
	.reset_sources = tegra30_reset_sources,
	.num_reset_sources = ARRAY_SIZE(tegra30_reset_sources),
	.reset_levels = NULL,
	.num_reset_levels = 0,
	.pmc_clks_data = tegra_pmc_clks_data,
	.num_pmc_clks = ARRAY_SIZE(tegra_pmc_clks_data),
	.has_blink_output = true,
	.has_usb_sleepwalk = true,
};

static const char * const tegra114_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "crail",
	[TEGRA_POWERGATE_3D] = "td",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_VDEC] = "vdec",
	[TEGRA_POWERGATE_MPE] = "mpe",
	[TEGRA_POWERGATE_HEG] = "heg",
	[TEGRA_POWERGATE_CPU1] = "cpu1",
	[TEGRA_POWERGATE_CPU2] = "cpu2",
	[TEGRA_POWERGATE_CPU3] = "cpu3",
	[TEGRA_POWERGATE_CELP] = "celp",
	[TEGRA_POWERGATE_CPU0] = "cpu0",
	[TEGRA_POWERGATE_C0NC] = "c0nc",
	[TEGRA_POWERGATE_C1NC] = "c1nc",
	[TEGRA_POWERGATE_DIS] = "dis",
	[TEGRA_POWERGATE_DISB] = "disb",
	[TEGRA_POWERGATE_XUSBA] = "xusba",
	[TEGRA_POWERGATE_XUSBB] = "xusbb",
	[TEGRA_POWERGATE_XUSBC] = "xusbc",
};

static const u8 tegra114_cpu_powergates[] = {
	TEGRA_POWERGATE_CPU0,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static const struct tegra_pmc_soc tegra114_pmc_soc = {
	.supports_core_domain = false,
	.num_powergates = ARRAY_SIZE(tegra114_powergates),
	.powergates = tegra114_powergates,
	.num_cpu_powergates = ARRAY_SIZE(tegra114_cpu_powergates),
	.cpu_powergates = tegra114_cpu_powergates,
	.has_tsense_reset = true,
	.has_gpu_clamps = false,
	.needs_mbist_war = false,
	.has_impl_33v_pwr = false,
	.maybe_tz_only = false,
	.num_io_pads = 0,
	.io_pads = NULL,
	.num_pin_descs = 0,
	.pin_descs = NULL,
	.regs = &tegra20_pmc_regs,
	.init = tegra20_pmc_init,
	.setup_irq_polarity = tegra20_pmc_setup_irq_polarity,
	.powergate_set = tegra114_powergate_set,
	.reset_sources = tegra30_reset_sources,
	.num_reset_sources = ARRAY_SIZE(tegra30_reset_sources),
	.reset_levels = NULL,
	.num_reset_levels = 0,
	.pmc_clks_data = tegra_pmc_clks_data,
	.num_pmc_clks = ARRAY_SIZE(tegra_pmc_clks_data),
	.has_blink_output = true,
	.has_usb_sleepwalk = true,
};

static const char * const tegra124_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "crail",
	[TEGRA_POWERGATE_3D] = "3d",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_VDEC] = "vdec",
	[TEGRA_POWERGATE_MPE] = "mpe",
	[TEGRA_POWERGATE_HEG] = "heg",
	[TEGRA_POWERGATE_SATA] = "sata",
	[TEGRA_POWERGATE_CPU1] = "cpu1",
	[TEGRA_POWERGATE_CPU2] = "cpu2",
	[TEGRA_POWERGATE_CPU3] = "cpu3",
	[TEGRA_POWERGATE_CELP] = "celp",
	[TEGRA_POWERGATE_CPU0] = "cpu0",
	[TEGRA_POWERGATE_C0NC] = "c0nc",
	[TEGRA_POWERGATE_C1NC] = "c1nc",
	[TEGRA_POWERGATE_SOR] = "sor",
	[TEGRA_POWERGATE_DIS] = "dis",
	[TEGRA_POWERGATE_DISB] = "disb",
	[TEGRA_POWERGATE_XUSBA] = "xusba",
	[TEGRA_POWERGATE_XUSBB] = "xusbb",
	[TEGRA_POWERGATE_XUSBC] = "xusbc",
	[TEGRA_POWERGATE_VIC] = "vic",
	[TEGRA_POWERGATE_IRAM] = "iram",
};

static const u8 tegra124_cpu_powergates[] = {
	TEGRA_POWERGATE_CPU0,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

#define TEGRA_IO_PAD(_id, _dpd, _voltage, _name)	\
	((struct tegra_io_pad_soc) {			\
		.id	= (_id),			\
		.dpd	= (_dpd),			\
		.voltage = (_voltage),			\
		.name	= (_name),			\
	})

#define TEGRA_IO_PIN_DESC(_id, _dpd, _voltage, _name)	\
	((struct pinctrl_pin_desc) {			\
		.number = (_id),			\
		.name	= (_name)			\
	})

#define TEGRA124_IO_PAD_TABLE(_pad)                                   \
	/* .id                          .dpd  .voltage  .name */      \
	_pad(TEGRA_IO_PAD_AUDIO,        17,   UINT_MAX, "audio"),     \
	_pad(TEGRA_IO_PAD_BB,           15,   UINT_MAX, "bb"),        \
	_pad(TEGRA_IO_PAD_CAM,          36,   UINT_MAX, "cam"),       \
	_pad(TEGRA_IO_PAD_COMP,         22,   UINT_MAX, "comp"),      \
	_pad(TEGRA_IO_PAD_CSIA,         0,    UINT_MAX, "csia"),      \
	_pad(TEGRA_IO_PAD_CSIB,         1,    UINT_MAX, "csb"),       \
	_pad(TEGRA_IO_PAD_CSIE,         44,   UINT_MAX, "cse"),       \
	_pad(TEGRA_IO_PAD_DSI,          2,    UINT_MAX, "dsi"),       \
	_pad(TEGRA_IO_PAD_DSIB,         39,   UINT_MAX, "dsib"),      \
	_pad(TEGRA_IO_PAD_DSIC,         40,   UINT_MAX, "dsic"),      \
	_pad(TEGRA_IO_PAD_DSID,         41,   UINT_MAX, "dsid"),      \
	_pad(TEGRA_IO_PAD_HDMI,         28,   UINT_MAX, "hdmi"),      \
	_pad(TEGRA_IO_PAD_HSIC,         19,   UINT_MAX, "hsic"),      \
	_pad(TEGRA_IO_PAD_HV,           38,   UINT_MAX, "hv"),        \
	_pad(TEGRA_IO_PAD_LVDS,         57,   UINT_MAX, "lvds"),      \
	_pad(TEGRA_IO_PAD_MIPI_BIAS,    3,    UINT_MAX, "mipi-bias"), \
	_pad(TEGRA_IO_PAD_NAND,         13,   UINT_MAX, "nand"),      \
	_pad(TEGRA_IO_PAD_PEX_BIAS,     4,    UINT_MAX, "pex-bias"),  \
	_pad(TEGRA_IO_PAD_PEX_CLK1,     5,    UINT_MAX, "pex-clk1"),  \
	_pad(TEGRA_IO_PAD_PEX_CLK2,     6,    UINT_MAX, "pex-clk2"),  \
	_pad(TEGRA_IO_PAD_PEX_CNTRL,    32,   UINT_MAX, "pex-cntrl"), \
	_pad(TEGRA_IO_PAD_SDMMC1,       33,   UINT_MAX, "sdmmc1"),    \
	_pad(TEGRA_IO_PAD_SDMMC3,       34,   UINT_MAX, "sdmmc3"),    \
	_pad(TEGRA_IO_PAD_SDMMC4,       35,   UINT_MAX, "sdmmc4"),    \
	_pad(TEGRA_IO_PAD_SYS_DDC,      58,   UINT_MAX, "sys_ddc"),   \
	_pad(TEGRA_IO_PAD_UART,         14,   UINT_MAX, "uart"),      \
	_pad(TEGRA_IO_PAD_USB0,         9,    UINT_MAX, "usb0"),      \
	_pad(TEGRA_IO_PAD_USB1,         10,   UINT_MAX, "usb1"),      \
	_pad(TEGRA_IO_PAD_USB2,         11,   UINT_MAX, "usb2"),      \
	_pad(TEGRA_IO_PAD_USB_BIAS,     12,   UINT_MAX, "usb_bias")

static const struct tegra_io_pad_soc tegra124_io_pads[] = {
	TEGRA124_IO_PAD_TABLE(TEGRA_IO_PAD)
};

static const struct pinctrl_pin_desc tegra124_pin_descs[] = {
	TEGRA124_IO_PAD_TABLE(TEGRA_IO_PIN_DESC)
};

static const struct tegra_pmc_soc tegra124_pmc_soc = {
	.supports_core_domain = false,
	.num_powergates = ARRAY_SIZE(tegra124_powergates),
	.powergates = tegra124_powergates,
	.num_cpu_powergates = ARRAY_SIZE(tegra124_cpu_powergates),
	.cpu_powergates = tegra124_cpu_powergates,
	.has_tsense_reset = true,
	.has_gpu_clamps = true,
	.needs_mbist_war = false,
	.has_impl_33v_pwr = false,
	.maybe_tz_only = false,
	.num_io_pads = ARRAY_SIZE(tegra124_io_pads),
	.io_pads = tegra124_io_pads,
	.num_pin_descs = ARRAY_SIZE(tegra124_pin_descs),
	.pin_descs = tegra124_pin_descs,
	.regs = &tegra20_pmc_regs,
	.init = tegra20_pmc_init,
	.setup_irq_polarity = tegra20_pmc_setup_irq_polarity,
	.powergate_set = tegra114_powergate_set,
	.reset_sources = tegra30_reset_sources,
	.num_reset_sources = ARRAY_SIZE(tegra30_reset_sources),
	.reset_levels = NULL,
	.num_reset_levels = 0,
	.pmc_clks_data = tegra_pmc_clks_data,
	.num_pmc_clks = ARRAY_SIZE(tegra_pmc_clks_data),
	.has_blink_output = true,
	.has_usb_sleepwalk = true,
};

static const char * const tegra210_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "crail",
	[TEGRA_POWERGATE_3D] = "3d",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_MPE] = "mpe",
	[TEGRA_POWERGATE_SATA] = "sata",
	[TEGRA_POWERGATE_CPU1] = "cpu1",
	[TEGRA_POWERGATE_CPU2] = "cpu2",
	[TEGRA_POWERGATE_CPU3] = "cpu3",
	[TEGRA_POWERGATE_CPU0] = "cpu0",
	[TEGRA_POWERGATE_C0NC] = "c0nc",
	[TEGRA_POWERGATE_SOR] = "sor",
	[TEGRA_POWERGATE_DIS] = "dis",
	[TEGRA_POWERGATE_DISB] = "disb",
	[TEGRA_POWERGATE_XUSBA] = "xusba",
	[TEGRA_POWERGATE_XUSBB] = "xusbb",
	[TEGRA_POWERGATE_XUSBC] = "xusbc",
	[TEGRA_POWERGATE_VIC] = "vic",
	[TEGRA_POWERGATE_IRAM] = "iram",
	[TEGRA_POWERGATE_NVDEC] = "nvdec",
	[TEGRA_POWERGATE_NVJPG] = "nvjpg",
	[TEGRA_POWERGATE_AUD] = "aud",
	[TEGRA_POWERGATE_DFD] = "dfd",
	[TEGRA_POWERGATE_VE2] = "ve2",
};

static const u8 tegra210_cpu_powergates[] = {
	TEGRA_POWERGATE_CPU0,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

#define TEGRA210_IO_PAD_TABLE(_pad)                                        \
	/*   .id                        .dpd     .voltage  .name */        \
	_pad(TEGRA_IO_PAD_AUDIO,       17,       5,        "audio"),       \
	_pad(TEGRA_IO_PAD_AUDIO_HV,    61,       18,       "audio-hv"),    \
	_pad(TEGRA_IO_PAD_CAM,         36,       10,       "cam"),         \
	_pad(TEGRA_IO_PAD_CSIA,        0,        UINT_MAX, "csia"),        \
	_pad(TEGRA_IO_PAD_CSIB,        1,        UINT_MAX, "csib"),        \
	_pad(TEGRA_IO_PAD_CSIC,        42,       UINT_MAX, "csic"),        \
	_pad(TEGRA_IO_PAD_CSID,        43,       UINT_MAX, "csid"),        \
	_pad(TEGRA_IO_PAD_CSIE,        44,       UINT_MAX, "csie"),        \
	_pad(TEGRA_IO_PAD_CSIF,        45,       UINT_MAX, "csif"),        \
	_pad(TEGRA_IO_PAD_DBG,         25,       19,       "dbg"),         \
	_pad(TEGRA_IO_PAD_DEBUG_NONAO, 26,       UINT_MAX, "debug-nonao"), \
	_pad(TEGRA_IO_PAD_DMIC,        50,       20,       "dmic"),        \
	_pad(TEGRA_IO_PAD_DP,          51,       UINT_MAX, "dp"),          \
	_pad(TEGRA_IO_PAD_DSI,         2,        UINT_MAX, "dsi"),         \
	_pad(TEGRA_IO_PAD_DSIB,        39,       UINT_MAX, "dsib"),        \
	_pad(TEGRA_IO_PAD_DSIC,        40,       UINT_MAX, "dsic"),        \
	_pad(TEGRA_IO_PAD_DSID,        41,       UINT_MAX, "dsid"),        \
	_pad(TEGRA_IO_PAD_EMMC,        35,       UINT_MAX, "emmc"),        \
	_pad(TEGRA_IO_PAD_EMMC2,       37,       UINT_MAX, "emmc2"),       \
	_pad(TEGRA_IO_PAD_GPIO,        27,       21,       "gpio"),        \
	_pad(TEGRA_IO_PAD_HDMI,        28,       UINT_MAX, "hdmi"),        \
	_pad(TEGRA_IO_PAD_HSIC,        19,       UINT_MAX, "hsic"),        \
	_pad(TEGRA_IO_PAD_LVDS,        57,       UINT_MAX, "lvds"),        \
	_pad(TEGRA_IO_PAD_MIPI_BIAS,   3,        UINT_MAX, "mipi-bias"),   \
	_pad(TEGRA_IO_PAD_PEX_BIAS,    4,        UINT_MAX, "pex-bias"),    \
	_pad(TEGRA_IO_PAD_PEX_CLK1,    5,        UINT_MAX, "pex-clk1"),    \
	_pad(TEGRA_IO_PAD_PEX_CLK2,    6,        UINT_MAX, "pex-clk2"),    \
	_pad(TEGRA_IO_PAD_PEX_CNTRL,   UINT_MAX, 11,       "pex-cntrl"),   \
	_pad(TEGRA_IO_PAD_SDMMC1,      33,       12,       "sdmmc1"),      \
	_pad(TEGRA_IO_PAD_SDMMC3,      34,       13,       "sdmmc3"),      \
	_pad(TEGRA_IO_PAD_SPI,         46,       22,       "spi"),         \
	_pad(TEGRA_IO_PAD_SPI_HV,      47,       23,       "spi-hv"),      \
	_pad(TEGRA_IO_PAD_UART,        14,       2,        "uart"),        \
	_pad(TEGRA_IO_PAD_USB0,        9,        UINT_MAX, "usb0"),        \
	_pad(TEGRA_IO_PAD_USB1,        10,       UINT_MAX, "usb1"),        \
	_pad(TEGRA_IO_PAD_USB2,        11,       UINT_MAX, "usb2"),        \
	_pad(TEGRA_IO_PAD_USB3,        18,       UINT_MAX, "usb3"),        \
	_pad(TEGRA_IO_PAD_USB_BIAS,    12,       UINT_MAX, "usb-bias")

static const struct tegra_io_pad_soc tegra210_io_pads[] = {
	TEGRA210_IO_PAD_TABLE(TEGRA_IO_PAD)
};

static const struct pinctrl_pin_desc tegra210_pin_descs[] = {
	TEGRA210_IO_PAD_TABLE(TEGRA_IO_PIN_DESC)
};

static const char * const tegra210_reset_sources[] = {
	"POWER_ON_RESET",
	"WATCHDOG",
	"SENSOR",
	"SW_MAIN",
	"LP0",
	"AOTAG"
};

static const struct tegra_wake_event tegra210_wake_events[] = {
	TEGRA_WAKE_IRQ("rtc", 16, 2),
	TEGRA_WAKE_IRQ("pmu", 51, 86),
};

static const struct tegra_pmc_soc tegra210_pmc_soc = {
	.supports_core_domain = false,
	.num_powergates = ARRAY_SIZE(tegra210_powergates),
	.powergates = tegra210_powergates,
	.num_cpu_powergates = ARRAY_SIZE(tegra210_cpu_powergates),
	.cpu_powergates = tegra210_cpu_powergates,
	.has_tsense_reset = true,
	.has_gpu_clamps = true,
	.needs_mbist_war = true,
	.has_impl_33v_pwr = false,
	.maybe_tz_only = true,
	.num_io_pads = ARRAY_SIZE(tegra210_io_pads),
	.io_pads = tegra210_io_pads,
	.num_pin_descs = ARRAY_SIZE(tegra210_pin_descs),
	.pin_descs = tegra210_pin_descs,
	.regs = &tegra20_pmc_regs,
	.init = tegra20_pmc_init,
	.setup_irq_polarity = tegra20_pmc_setup_irq_polarity,
	.powergate_set = tegra114_powergate_set,
	.irq_set_wake = tegra210_pmc_irq_set_wake,
	.irq_set_type = tegra210_pmc_irq_set_type,
	.reset_sources = tegra210_reset_sources,
	.num_reset_sources = ARRAY_SIZE(tegra210_reset_sources),
	.reset_levels = NULL,
	.num_reset_levels = 0,
	.num_wake_events = ARRAY_SIZE(tegra210_wake_events),
	.wake_events = tegra210_wake_events,
	.pmc_clks_data = tegra_pmc_clks_data,
	.num_pmc_clks = ARRAY_SIZE(tegra_pmc_clks_data),
	.has_blink_output = true,
	.has_usb_sleepwalk = true,
};

#define TEGRA186_IO_PAD_TABLE(_pad)                                          \
	/*   .id                        .dpd      .voltage  .name */         \
	_pad(TEGRA_IO_PAD_CSIA,         0,        UINT_MAX, "csia"),         \
	_pad(TEGRA_IO_PAD_CSIB,         1,        UINT_MAX, "csib"),         \
	_pad(TEGRA_IO_PAD_DSI,          2,        UINT_MAX, "dsi"),          \
	_pad(TEGRA_IO_PAD_MIPI_BIAS,    3,        UINT_MAX, "mipi-bias"),    \
	_pad(TEGRA_IO_PAD_PEX_CLK_BIAS, 4,        UINT_MAX, "pex-clk-bias"), \
	_pad(TEGRA_IO_PAD_PEX_CLK3,     5,        UINT_MAX, "pex-clk3"),     \
	_pad(TEGRA_IO_PAD_PEX_CLK2,     6,        UINT_MAX, "pex-clk2"),     \
	_pad(TEGRA_IO_PAD_PEX_CLK1,     7,        UINT_MAX, "pex-clk1"),     \
	_pad(TEGRA_IO_PAD_USB0,         9,        UINT_MAX, "usb0"),         \
	_pad(TEGRA_IO_PAD_USB1,         10,       UINT_MAX, "usb1"),         \
	_pad(TEGRA_IO_PAD_USB2,         11,       UINT_MAX, "usb2"),         \
	_pad(TEGRA_IO_PAD_USB_BIAS,     12,       UINT_MAX, "usb-bias"),     \
	_pad(TEGRA_IO_PAD_UART,         14,       UINT_MAX, "uart"),         \
	_pad(TEGRA_IO_PAD_AUDIO,        17,       UINT_MAX, "audio"),        \
	_pad(TEGRA_IO_PAD_HSIC,         19,       UINT_MAX, "hsic"),         \
	_pad(TEGRA_IO_PAD_DBG,          25,       UINT_MAX, "dbg"),          \
	_pad(TEGRA_IO_PAD_HDMI_DP0,     28,       UINT_MAX, "hdmi-dp0"),     \
	_pad(TEGRA_IO_PAD_HDMI_DP1,     29,       UINT_MAX, "hdmi-dp1"),     \
	_pad(TEGRA_IO_PAD_PEX_CNTRL,    32,       UINT_MAX, "pex-cntrl"),    \
	_pad(TEGRA_IO_PAD_SDMMC2_HV,    34,       5,        "sdmmc2-hv"),    \
	_pad(TEGRA_IO_PAD_SDMMC4,       36,       UINT_MAX, "sdmmc4"),       \
	_pad(TEGRA_IO_PAD_CAM,          38,       UINT_MAX, "cam"),          \
	_pad(TEGRA_IO_PAD_DSIB,         40,       UINT_MAX, "dsib"),         \
	_pad(TEGRA_IO_PAD_DSIC,         41,       UINT_MAX, "dsic"),         \
	_pad(TEGRA_IO_PAD_DSID,         42,       UINT_MAX, "dsid"),         \
	_pad(TEGRA_IO_PAD_CSIC,         43,       UINT_MAX, "csic"),         \
	_pad(TEGRA_IO_PAD_CSID,         44,       UINT_MAX, "csid"),         \
	_pad(TEGRA_IO_PAD_CSIE,         45,       UINT_MAX, "csie"),         \
	_pad(TEGRA_IO_PAD_CSIF,         46,       UINT_MAX, "csif"),         \
	_pad(TEGRA_IO_PAD_SPI,          47,       UINT_MAX, "spi"),          \
	_pad(TEGRA_IO_PAD_UFS,          49,       UINT_MAX, "ufs"),          \
	_pad(TEGRA_IO_PAD_DMIC_HV,      52,       2,        "dmic-hv"),	     \
	_pad(TEGRA_IO_PAD_EDP,          53,       UINT_MAX, "edp"),          \
	_pad(TEGRA_IO_PAD_SDMMC1_HV,    55,       4,        "sdmmc1-hv"),    \
	_pad(TEGRA_IO_PAD_SDMMC3_HV,    56,       6,        "sdmmc3-hv"),    \
	_pad(TEGRA_IO_PAD_CONN,         60,       UINT_MAX, "conn"),         \
	_pad(TEGRA_IO_PAD_AUDIO_HV,     61,       1,        "audio-hv"),     \
	_pad(TEGRA_IO_PAD_AO_HV,        UINT_MAX, 0,        "ao-hv")

static const struct tegra_io_pad_soc tegra186_io_pads[] = {
	TEGRA186_IO_PAD_TABLE(TEGRA_IO_PAD)
};

static const struct pinctrl_pin_desc tegra186_pin_descs[] = {
	TEGRA186_IO_PAD_TABLE(TEGRA_IO_PIN_DESC)
};

static const struct tegra_pmc_regs tegra186_pmc_regs = {
	.scratch0 = 0x2000,
	.dpd_req = 0x74,
	.dpd_status = 0x78,
	.dpd2_req = 0x7c,
	.dpd2_status = 0x80,
	.rst_status = 0x70,
	.rst_source_shift = 0x2,
	.rst_source_mask = 0x3c,
	.rst_level_shift = 0x0,
	.rst_level_mask = 0x3,
};

static void tegra186_pmc_setup_irq_polarity(struct tegra_pmc *pmc,
					    struct device_node *np,
					    bool invert)
{
	struct resource regs;
	void __iomem *wake;
	u32 value;
	int index;

	index = of_property_match_string(np, "reg-names", "wake");
	if (index < 0) {
		dev_err(pmc->dev, "failed to find PMC wake registers\n");
		return;
	}

	of_address_to_resource(np, index, &regs);

	wake = ioremap(regs.start, resource_size(&regs));
	if (!wake) {
		dev_err(pmc->dev, "failed to map PMC wake registers\n");
		return;
	}

	value = readl(wake + WAKE_AOWAKE_CTRL);

	if (invert)
		value |= WAKE_AOWAKE_CTRL_INTR_POLARITY;
	else
		value &= ~WAKE_AOWAKE_CTRL_INTR_POLARITY;

	writel(value, wake + WAKE_AOWAKE_CTRL);

	iounmap(wake);
}

static const char * const tegra186_reset_sources[] = {
	"SYS_RESET",
	"AOWDT",
	"MCCPLEXWDT",
	"BPMPWDT",
	"SCEWDT",
	"SPEWDT",
	"APEWDT",
	"BCCPLEXWDT",
	"SENSOR",
	"AOTAG",
	"VFSENSOR",
	"SWREST",
	"SC7",
	"HSM",
	"CORESIGHT"
};

static const char * const tegra186_reset_levels[] = {
	"L0", "L1", "L2", "WARM"
};

static const struct tegra_wake_event tegra186_wake_events[] = {
	TEGRA_WAKE_IRQ("pmu", 24, 209),
	TEGRA_WAKE_GPIO("power", 29, 1, TEGRA186_AON_GPIO(FF, 0)),
	TEGRA_WAKE_IRQ("rtc", 73, 10),
};

static const struct tegra_pmc_soc tegra186_pmc_soc = {
	.supports_core_domain = false,
	.num_powergates = 0,
	.powergates = NULL,
	.num_cpu_powergates = 0,
	.cpu_powergates = NULL,
	.has_tsense_reset = false,
	.has_gpu_clamps = false,
	.needs_mbist_war = false,
	.has_impl_33v_pwr = true,
	.maybe_tz_only = false,
	.num_io_pads = ARRAY_SIZE(tegra186_io_pads),
	.io_pads = tegra186_io_pads,
	.num_pin_descs = ARRAY_SIZE(tegra186_pin_descs),
	.pin_descs = tegra186_pin_descs,
	.regs = &tegra186_pmc_regs,
	.init = NULL,
	.setup_irq_polarity = tegra186_pmc_setup_irq_polarity,
	.irq_set_wake = tegra186_pmc_irq_set_wake,
	.irq_set_type = tegra186_pmc_irq_set_type,
	.reset_sources = tegra186_reset_sources,
	.num_reset_sources = ARRAY_SIZE(tegra186_reset_sources),
	.reset_levels = tegra186_reset_levels,
	.num_reset_levels = ARRAY_SIZE(tegra186_reset_levels),
	.num_wake_events = ARRAY_SIZE(tegra186_wake_events),
	.wake_events = tegra186_wake_events,
	.pmc_clks_data = NULL,
	.num_pmc_clks = 0,
	.has_blink_output = false,
	.has_usb_sleepwalk = false,
};

#define TEGRA194_IO_PAD_TABLE(_pad)                                              \
	/*   .id                          .dpd      .voltage  .name */           \
	_pad(TEGRA_IO_PAD_CSIA,           0,        UINT_MAX, "csia"),           \
	_pad(TEGRA_IO_PAD_CSIB,           1,        UINT_MAX, "csib"),           \
	_pad(TEGRA_IO_PAD_MIPI_BIAS,      3,        UINT_MAX, "mipi-bias"),      \
	_pad(TEGRA_IO_PAD_PEX_CLK_BIAS,   4,        UINT_MAX, "pex-clk-bias"),   \
	_pad(TEGRA_IO_PAD_PEX_CLK3,       5,        UINT_MAX, "pex-clk3"),       \
	_pad(TEGRA_IO_PAD_PEX_CLK2,       6,        UINT_MAX, "pex-clk2"),       \
	_pad(TEGRA_IO_PAD_PEX_CLK1,       7,        UINT_MAX, "pex-clk1"),       \
	_pad(TEGRA_IO_PAD_EQOS,           8,        UINT_MAX, "eqos"),           \
	_pad(TEGRA_IO_PAD_PEX_CLK_2_BIAS, 9,        UINT_MAX, "pex-clk-2-bias"), \
	_pad(TEGRA_IO_PAD_PEX_CLK_2,      10,       UINT_MAX, "pex-clk-2"),      \
	_pad(TEGRA_IO_PAD_DAP3,           11,       UINT_MAX, "dap3"),           \
	_pad(TEGRA_IO_PAD_DAP5,           12,       UINT_MAX, "dap5"),           \
	_pad(TEGRA_IO_PAD_UART,           14,       UINT_MAX, "uart"),           \
	_pad(TEGRA_IO_PAD_PWR_CTL,        15,       UINT_MAX, "pwr-ctl"),        \
	_pad(TEGRA_IO_PAD_SOC_GPIO53,     16,       UINT_MAX, "soc-gpio53"),     \
	_pad(TEGRA_IO_PAD_AUDIO,          17,       UINT_MAX, "audio"),          \
	_pad(TEGRA_IO_PAD_GP_PWM2,        18,       UINT_MAX, "gp-pwm2"),        \
	_pad(TEGRA_IO_PAD_GP_PWM3,        19,       UINT_MAX, "gp-pwm3"),        \
	_pad(TEGRA_IO_PAD_SOC_GPIO12,     20,       UINT_MAX, "soc-gpio12"),     \
	_pad(TEGRA_IO_PAD_SOC_GPIO13,     21,       UINT_MAX, "soc-gpio13"),     \
	_pad(TEGRA_IO_PAD_SOC_GPIO10,     22,       UINT_MAX, "soc-gpio10"),     \
	_pad(TEGRA_IO_PAD_UART4,          23,       UINT_MAX, "uart4"),          \
	_pad(TEGRA_IO_PAD_UART5,          24,       UINT_MAX, "uart5"),          \
	_pad(TEGRA_IO_PAD_DBG,            25,       UINT_MAX, "dbg"),            \
	_pad(TEGRA_IO_PAD_HDMI_DP3,       26,       UINT_MAX, "hdmi-dp3"),       \
	_pad(TEGRA_IO_PAD_HDMI_DP2,       27,       UINT_MAX, "hdmi-dp2"),       \
	_pad(TEGRA_IO_PAD_HDMI_DP0,       28,       UINT_MAX, "hdmi-dp0"),       \
	_pad(TEGRA_IO_PAD_HDMI_DP1,       29,       UINT_MAX, "hdmi-dp1"),       \
	_pad(TEGRA_IO_PAD_PEX_CNTRL,      32,       UINT_MAX, "pex-cntrl"),      \
	_pad(TEGRA_IO_PAD_PEX_CTL2,       33,       UINT_MAX, "pex-ctl2"),       \
	_pad(TEGRA_IO_PAD_PEX_L0_RST_N,   34,       UINT_MAX, "pex-l0-rst"),     \
	_pad(TEGRA_IO_PAD_PEX_L1_RST_N,   35,       UINT_MAX, "pex-l1-rst"),     \
	_pad(TEGRA_IO_PAD_SDMMC4,         36,       UINT_MAX, "sdmmc4"),         \
	_pad(TEGRA_IO_PAD_PEX_L5_RST_N,   37,       UINT_MAX, "pex-l5-rst"),     \
	_pad(TEGRA_IO_PAD_CAM,            38,       UINT_MAX, "cam"),            \
	_pad(TEGRA_IO_PAD_CSIC,           43,       UINT_MAX, "csic"),           \
	_pad(TEGRA_IO_PAD_CSID,           44,       UINT_MAX, "csid"),           \
	_pad(TEGRA_IO_PAD_CSIE,           45,       UINT_MAX, "csie"),           \
	_pad(TEGRA_IO_PAD_CSIF,           46,       UINT_MAX, "csif"),           \
	_pad(TEGRA_IO_PAD_SPI,            47,       UINT_MAX, "spi"),            \
	_pad(TEGRA_IO_PAD_UFS,            49,       UINT_MAX, "ufs"),            \
	_pad(TEGRA_IO_PAD_CSIG,           50,       UINT_MAX, "csig"),           \
	_pad(TEGRA_IO_PAD_CSIH,           51,       UINT_MAX, "csih"),           \
	_pad(TEGRA_IO_PAD_EDP,            53,       UINT_MAX, "edp"),            \
	_pad(TEGRA_IO_PAD_SDMMC1_HV,      55,       4,        "sdmmc1-hv"),      \
	_pad(TEGRA_IO_PAD_SDMMC3_HV,      56,       6,        "sdmmc3-hv"),      \
	_pad(TEGRA_IO_PAD_CONN,           60,       UINT_MAX, "conn"),           \
	_pad(TEGRA_IO_PAD_AUDIO_HV,       61,       1,        "audio-hv"),       \
	_pad(TEGRA_IO_PAD_AO_HV,          UINT_MAX, 0,        "ao-hv")

static const struct tegra_io_pad_soc tegra194_io_pads[] = {
	TEGRA194_IO_PAD_TABLE(TEGRA_IO_PAD)
};

static const struct pinctrl_pin_desc tegra194_pin_descs[] = {
	TEGRA194_IO_PAD_TABLE(TEGRA_IO_PIN_DESC)
};

static const struct tegra_pmc_regs tegra194_pmc_regs = {
	.scratch0 = 0x2000,
	.dpd_req = 0x74,
	.dpd_status = 0x78,
	.dpd2_req = 0x7c,
	.dpd2_status = 0x80,
	.rst_status = 0x70,
	.rst_source_shift = 0x2,
	.rst_source_mask = 0x7c,
	.rst_level_shift = 0x0,
	.rst_level_mask = 0x3,
};

static const char * const tegra194_reset_sources[] = {
	"SYS_RESET_N",
	"AOWDT",
	"BCCPLEXWDT",
	"BPMPWDT",
	"SCEWDT",
	"SPEWDT",
	"APEWDT",
	"LCCPLEXWDT",
	"SENSOR",
	"AOTAG",
	"VFSENSOR",
	"MAINSWRST",
	"SC7",
	"HSM",
	"CSITE",
	"RCEWDT",
	"PVA0WDT",
	"PVA1WDT",
	"L1A_ASYNC",
	"BPMPBOOT",
	"FUSECRC",
};

static const struct tegra_wake_event tegra194_wake_events[] = {
	TEGRA_WAKE_IRQ("pmu", 24, 209),
	TEGRA_WAKE_GPIO("power", 29, 1, TEGRA194_AON_GPIO(EE, 4)),
	TEGRA_WAKE_IRQ("rtc", 73, 10),
};

static const struct tegra_pmc_soc tegra194_pmc_soc = {
	.supports_core_domain = false,
	.num_powergates = 0,
	.powergates = NULL,
	.num_cpu_powergates = 0,
	.cpu_powergates = NULL,
	.has_tsense_reset = false,
	.has_gpu_clamps = false,
	.needs_mbist_war = false,
	.has_impl_33v_pwr = true,
	.maybe_tz_only = false,
	.num_io_pads = ARRAY_SIZE(tegra194_io_pads),
	.io_pads = tegra194_io_pads,
	.num_pin_descs = ARRAY_SIZE(tegra194_pin_descs),
	.pin_descs = tegra194_pin_descs,
	.regs = &tegra194_pmc_regs,
	.init = NULL,
	.setup_irq_polarity = tegra186_pmc_setup_irq_polarity,
	.irq_set_wake = tegra186_pmc_irq_set_wake,
	.irq_set_type = tegra186_pmc_irq_set_type,
	.reset_sources = tegra194_reset_sources,
	.num_reset_sources = ARRAY_SIZE(tegra194_reset_sources),
	.reset_levels = tegra186_reset_levels,
	.num_reset_levels = ARRAY_SIZE(tegra186_reset_levels),
	.num_wake_events = ARRAY_SIZE(tegra194_wake_events),
	.wake_events = tegra194_wake_events,
	.pmc_clks_data = NULL,
	.num_pmc_clks = 0,
	.has_blink_output = false,
	.has_usb_sleepwalk = false,
};

static const struct tegra_pmc_regs tegra234_pmc_regs = {
	.scratch0 = 0x2000,
	.dpd_req = 0,
	.dpd_status = 0,
	.dpd2_req = 0,
	.dpd2_status = 0,
	.rst_status = 0x70,
	.rst_source_shift = 0x2,
	.rst_source_mask = 0xfc,
	.rst_level_shift = 0x0,
	.rst_level_mask = 0x3,
};

static const char * const tegra234_reset_sources[] = {
	"SYS_RESET_N",	/* 0x0 */
	"AOWDT",
	"BCCPLEXWDT",
	"BPMPWDT",
	"SCEWDT",
	"SPEWDT",
	"APEWDT",
	"LCCPLEXWDT",
	"SENSOR",	/* 0x8 */
	NULL,
	NULL,
	"MAINSWRST",
	"SC7",
	"HSM",
	NULL,
	"RCEWDT",
	NULL,		/* 0x10 */
	NULL,
	NULL,
	"BPMPBOOT",
	"FUSECRC",
	"DCEWDT",
	"PSCWDT",
	"PSC",
	"CSITE_SW",	/* 0x18 */
	"POD",
	"SCPM",
	"VREFRO_POWERBAD",
	"VMON",
	"FMON",
	"FSI_R5WDT",
	"FSI_THERM",
	"FSI_R52C0WDT",	/* 0x20 */
	"FSI_R52C1WDT",
	"FSI_R52C2WDT",
	"FSI_R52C3WDT",
	"FSI_FMON",
	"FSI_VMON",	/* 0x25 */
};

static const struct tegra_wake_event tegra234_wake_events[] = {
	TEGRA_WAKE_GPIO("power", 29, 1, TEGRA234_AON_GPIO(EE, 4)),
	TEGRA_WAKE_IRQ("rtc", 73, 10),
};

static const struct tegra_pmc_soc tegra234_pmc_soc = {
	.supports_core_domain = false,
	.num_powergates = 0,
	.powergates = NULL,
	.num_cpu_powergates = 0,
	.cpu_powergates = NULL,
	.has_tsense_reset = false,
	.has_gpu_clamps = false,
	.needs_mbist_war = false,
	.has_impl_33v_pwr = true,
	.maybe_tz_only = false,
	.num_io_pads = 0,
	.io_pads = NULL,
	.num_pin_descs = 0,
	.pin_descs = NULL,
	.regs = &tegra234_pmc_regs,
	.init = NULL,
	.setup_irq_polarity = tegra186_pmc_setup_irq_polarity,
	.irq_set_wake = tegra186_pmc_irq_set_wake,
	.irq_set_type = tegra186_pmc_irq_set_type,
	.reset_sources = tegra234_reset_sources,
	.num_reset_sources = ARRAY_SIZE(tegra234_reset_sources),
	.reset_levels = tegra186_reset_levels,
	.num_reset_levels = ARRAY_SIZE(tegra186_reset_levels),
	.num_wake_events = ARRAY_SIZE(tegra234_wake_events),
	.wake_events = tegra234_wake_events,
	.pmc_clks_data = NULL,
	.num_pmc_clks = 0,
	.has_blink_output = false,
};

static const struct of_device_id tegra_pmc_match[] = {
	{ .compatible = "nvidia,tegra234-pmc", .data = &tegra234_pmc_soc },
	{ .compatible = "nvidia,tegra194-pmc", .data = &tegra194_pmc_soc },
	{ .compatible = "nvidia,tegra186-pmc", .data = &tegra186_pmc_soc },
	{ .compatible = "nvidia,tegra210-pmc", .data = &tegra210_pmc_soc },
	{ .compatible = "nvidia,tegra132-pmc", .data = &tegra124_pmc_soc },
	{ .compatible = "nvidia,tegra124-pmc", .data = &tegra124_pmc_soc },
	{ .compatible = "nvidia,tegra114-pmc", .data = &tegra114_pmc_soc },
	{ .compatible = "nvidia,tegra30-pmc", .data = &tegra30_pmc_soc },
	{ .compatible = "nvidia,tegra20-pmc", .data = &tegra20_pmc_soc },
	{ }
};

static void tegra_pmc_sync_state(struct device *dev)
{
	int err;

	/*
	 * Newer device-trees have power domains, but we need to prepare all
	 * device drivers with runtime PM and OPP support first, otherwise
	 * state syncing is unsafe.
	 */
	if (!pmc->soc->supports_core_domain)
		return;

	/*
	 * Older device-trees don't have core PD, and thus, there are
	 * no dependencies that will block the state syncing. We shouldn't
	 * mark the domain as synced in this case.
	 */
	if (!pmc->core_domain_registered)
		return;

	pmc->core_domain_state_synced = true;

	/* this is a no-op if core regulator isn't used */
	mutex_lock(&pmc->powergates_lock);
	err = dev_pm_opp_sync_regulators(dev);
	mutex_unlock(&pmc->powergates_lock);

	if (err)
		dev_err(dev, "failed to sync regulators: %d\n", err);
}

static struct platform_driver tegra_pmc_driver = {
	.driver = {
		.name = "tegra-pmc",
		.suppress_bind_attrs = true,
		.of_match_table = tegra_pmc_match,
#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_ARM)
		.pm = &tegra_pmc_pm_ops,
#endif
		.sync_state = tegra_pmc_sync_state,
	},
	.probe = tegra_pmc_probe,
};
builtin_platform_driver(tegra_pmc_driver);

static bool __init tegra_pmc_detect_tz_only(struct tegra_pmc *pmc)
{
	u32 value, saved;

	saved = readl(pmc->base + pmc->soc->regs->scratch0);
	value = saved ^ 0xffffffff;

	if (value == 0xffffffff)
		value = 0xdeadbeef;

	/* write pattern and read it back */
	writel(value, pmc->base + pmc->soc->regs->scratch0);
	value = readl(pmc->base + pmc->soc->regs->scratch0);

	/* if we read all-zeroes, access is restricted to TZ only */
	if (value == 0) {
		pr_info("access to PMC is restricted to TZ\n");
		return true;
	}

	/* restore original value */
	writel(saved, pmc->base + pmc->soc->regs->scratch0);

	return false;
}

/*
 * Early initialization to allow access to registers in the very early boot
 * process.
 */
static int __init tegra_pmc_early_init(void)
{
	const struct of_device_id *match;
	struct device_node *np;
	struct resource regs;
	unsigned int i;
	bool invert;

	mutex_init(&pmc->powergates_lock);

	np = of_find_matching_node_and_match(NULL, tegra_pmc_match, &match);
	if (!np) {
		/*
		 * Fall back to legacy initialization for 32-bit ARM only. All
		 * 64-bit ARM device tree files for Tegra are required to have
		 * a PMC node.
		 *
		 * This is for backwards-compatibility with old device trees
		 * that didn't contain a PMC node. Note that in this case the
		 * SoC data can't be matched and therefore powergating is
		 * disabled.
		 */
		if (IS_ENABLED(CONFIG_ARM) && soc_is_tegra()) {
			pr_warn("DT node not found, powergating disabled\n");

			regs.start = 0x7000e400;
			regs.end = 0x7000e7ff;
			regs.flags = IORESOURCE_MEM;

			pr_warn("Using memory region %pR\n", &regs);
		} else {
			/*
			 * At this point we're not running on Tegra, so play
			 * nice with multi-platform kernels.
			 */
			return 0;
		}
	} else {
		/*
		 * Extract information from the device tree if we've found a
		 * matching node.
		 */
		if (of_address_to_resource(np, 0, &regs) < 0) {
			pr_err("failed to get PMC registers\n");
			of_node_put(np);
			return -ENXIO;
		}
	}

	pmc->base = ioremap(regs.start, resource_size(&regs));
	if (!pmc->base) {
		pr_err("failed to map PMC registers\n");
		of_node_put(np);
		return -ENXIO;
	}

	if (np) {
		pmc->soc = match->data;

		if (pmc->soc->maybe_tz_only)
			pmc->tz_only = tegra_pmc_detect_tz_only(pmc);

		/* Create a bitmap of the available and valid partitions */
		for (i = 0; i < pmc->soc->num_powergates; i++)
			if (pmc->soc->powergates[i])
				set_bit(i, pmc->powergates_available);

		/*
		 * Invert the interrupt polarity if a PMC device tree node
		 * exists and contains the nvidia,invert-interrupt property.
		 */
		invert = of_property_read_bool(np, "nvidia,invert-interrupt");

		pmc->soc->setup_irq_polarity(pmc, np, invert);

		of_node_put(np);
	}

	return 0;
}
early_initcall(tegra_pmc_early_init);
