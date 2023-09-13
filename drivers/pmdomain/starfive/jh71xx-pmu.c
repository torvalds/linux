// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * StarFive JH71XX PMU (Power Management Unit) Controller Driver
 *
 * Copyright (C) 2022-2023 StarFive Technology Co., Ltd.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <dt-bindings/power/starfive,jh7110-pmu.h>

/* register offset */
#define JH71XX_PMU_SW_TURN_ON_POWER	0x0C
#define JH71XX_PMU_SW_TURN_OFF_POWER	0x10
#define JH71XX_PMU_SW_ENCOURAGE		0x44
#define JH71XX_PMU_TIMER_INT_MASK	0x48
#define JH71XX_PMU_CURR_POWER_MODE	0x80
#define JH71XX_PMU_EVENT_STATUS		0x88
#define JH71XX_PMU_INT_STATUS		0x8C

/* aon pmu register offset */
#define JH71XX_AON_PMU_SWITCH		0x00

/* sw encourage cfg */
#define JH71XX_PMU_SW_ENCOURAGE_EN_LO	0x05
#define JH71XX_PMU_SW_ENCOURAGE_EN_HI	0x50
#define JH71XX_PMU_SW_ENCOURAGE_DIS_LO	0x0A
#define JH71XX_PMU_SW_ENCOURAGE_DIS_HI	0xA0
#define JH71XX_PMU_SW_ENCOURAGE_ON	0xFF

/* pmu int status */
#define JH71XX_PMU_INT_SEQ_DONE		BIT(0)
#define JH71XX_PMU_INT_HW_REQ		BIT(1)
#define JH71XX_PMU_INT_SW_FAIL		GENMASK(3, 2)
#define JH71XX_PMU_INT_HW_FAIL		GENMASK(5, 4)
#define JH71XX_PMU_INT_PCH_FAIL		GENMASK(8, 6)
#define JH71XX_PMU_INT_ALL_MASK		GENMASK(8, 0)

/*
 * The time required for switching power status is based on the time
 * to turn on the largest domain's power, which is at microsecond level
 */
#define JH71XX_PMU_TIMEOUT_US		100

struct jh71xx_domain_info {
	const char * const name;
	unsigned int flags;
	u8 bit;
};

struct jh71xx_pmu;
struct jh71xx_pmu_dev;

struct jh71xx_pmu_match_data {
	const struct jh71xx_domain_info *domain_info;
	int num_domains;
	unsigned int pmu_status;
	int (*pmu_parse_irq)(struct platform_device *pdev,
			     struct jh71xx_pmu *pmu);
	int (*pmu_set_state)(struct jh71xx_pmu_dev *pmd,
			     u32 mask, bool on);
};

struct jh71xx_pmu {
	struct device *dev;
	const struct jh71xx_pmu_match_data *match_data;
	void __iomem *base;
	struct generic_pm_domain **genpd;
	struct genpd_onecell_data genpd_data;
	int irq;
	spinlock_t lock;	/* protects pmu reg */
};

struct jh71xx_pmu_dev {
	const struct jh71xx_domain_info *domain_info;
	struct jh71xx_pmu *pmu;
	struct generic_pm_domain genpd;
};

static int jh71xx_pmu_get_state(struct jh71xx_pmu_dev *pmd, u32 mask, bool *is_on)
{
	struct jh71xx_pmu *pmu = pmd->pmu;

	if (!mask)
		return -EINVAL;

	*is_on = readl(pmu->base + pmu->match_data->pmu_status) & mask;

	return 0;
}

static int jh7110_pmu_set_state(struct jh71xx_pmu_dev *pmd, u32 mask, bool on)
{
	struct jh71xx_pmu *pmu = pmd->pmu;
	unsigned long flags;
	u32 val;
	u32 mode;
	u32 encourage_lo;
	u32 encourage_hi;
	int ret;

	spin_lock_irqsave(&pmu->lock, flags);

	/*
	 * The PMU accepts software encourage to switch power mode in the following 2 steps:
	 *
	 * 1.Configure the register SW_TURN_ON_POWER (offset 0x0c) by writing 1 to
	 *   the bit corresponding to the power domain that will be turned on
	 *   and writing 0 to the others.
	 *   Likewise, configure the register SW_TURN_OFF_POWER (offset 0x10) by
	 *   writing 1 to the bit corresponding to the power domain that will be
	 *   turned off and writing 0 to the others.
	 */
	if (on) {
		mode = JH71XX_PMU_SW_TURN_ON_POWER;
		encourage_lo = JH71XX_PMU_SW_ENCOURAGE_EN_LO;
		encourage_hi = JH71XX_PMU_SW_ENCOURAGE_EN_HI;
	} else {
		mode = JH71XX_PMU_SW_TURN_OFF_POWER;
		encourage_lo = JH71XX_PMU_SW_ENCOURAGE_DIS_LO;
		encourage_hi = JH71XX_PMU_SW_ENCOURAGE_DIS_HI;
	}

	writel(mask, pmu->base + mode);

	/*
	 * 2.Write SW encourage command sequence to the Software Encourage Reg (offset 0x44)
	 *   First write SW_MODE_ENCOURAGE_ON to JH71XX_PMU_SW_ENCOURAGE. This will reset
	 *   the state machine which parses the command sequence. This register must be
	 *   written every time software wants to power on/off a domain.
	 *   Then write the lower bits of the command sequence, followed by the upper
	 *   bits. The sequence differs between powering on & off a domain.
	 */
	writel(JH71XX_PMU_SW_ENCOURAGE_ON, pmu->base + JH71XX_PMU_SW_ENCOURAGE);
	writel(encourage_lo, pmu->base + JH71XX_PMU_SW_ENCOURAGE);
	writel(encourage_hi, pmu->base + JH71XX_PMU_SW_ENCOURAGE);

	spin_unlock_irqrestore(&pmu->lock, flags);

	/* Wait for the power domain bit to be enabled / disabled */
	if (on) {
		ret = readl_poll_timeout_atomic(pmu->base + JH71XX_PMU_CURR_POWER_MODE,
						val, val & mask,
						1, JH71XX_PMU_TIMEOUT_US);
	} else {
		ret = readl_poll_timeout_atomic(pmu->base + JH71XX_PMU_CURR_POWER_MODE,
						val, !(val & mask),
						1, JH71XX_PMU_TIMEOUT_US);
	}

	if (ret) {
		dev_err(pmu->dev, "%s: failed to power %s\n",
			pmd->genpd.name, on ? "on" : "off");
		return -ETIMEDOUT;
	}

	return 0;
}

static int jh7110_aon_pmu_set_state(struct jh71xx_pmu_dev *pmd, u32 mask, bool on)
{
	struct jh71xx_pmu *pmu = pmd->pmu;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&pmu->lock, flags);
	val = readl(pmu->base + JH71XX_AON_PMU_SWITCH);

	if (on)
		val |= mask;
	else
		val &= ~mask;

	writel(val, pmu->base + JH71XX_AON_PMU_SWITCH);
	spin_unlock_irqrestore(&pmu->lock, flags);

	return 0;
}

static int jh71xx_pmu_set_state(struct jh71xx_pmu_dev *pmd, u32 mask, bool on)
{
	struct jh71xx_pmu *pmu = pmd->pmu;
	const struct jh71xx_pmu_match_data *match_data = pmu->match_data;
	bool is_on;
	int ret;

	ret = jh71xx_pmu_get_state(pmd, mask, &is_on);
	if (ret) {
		dev_dbg(pmu->dev, "unable to get current state for %s\n",
			pmd->genpd.name);
		return ret;
	}

	if (is_on == on) {
		dev_dbg(pmu->dev, "pm domain [%s] is already %sable status.\n",
			pmd->genpd.name, on ? "en" : "dis");
		return 0;
	}

	return match_data->pmu_set_state(pmd, mask, on);
}

static int jh71xx_pmu_on(struct generic_pm_domain *genpd)
{
	struct jh71xx_pmu_dev *pmd = container_of(genpd,
						  struct jh71xx_pmu_dev, genpd);
	u32 pwr_mask = BIT(pmd->domain_info->bit);

	return jh71xx_pmu_set_state(pmd, pwr_mask, true);
}

static int jh71xx_pmu_off(struct generic_pm_domain *genpd)
{
	struct jh71xx_pmu_dev *pmd = container_of(genpd,
						  struct jh71xx_pmu_dev, genpd);
	u32 pwr_mask = BIT(pmd->domain_info->bit);

	return jh71xx_pmu_set_state(pmd, pwr_mask, false);
}

static void jh71xx_pmu_int_enable(struct jh71xx_pmu *pmu, u32 mask, bool enable)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&pmu->lock, flags);
	val = readl(pmu->base + JH71XX_PMU_TIMER_INT_MASK);

	if (enable)
		val &= ~mask;
	else
		val |= mask;

	writel(val, pmu->base + JH71XX_PMU_TIMER_INT_MASK);
	spin_unlock_irqrestore(&pmu->lock, flags);
}

static irqreturn_t jh71xx_pmu_interrupt(int irq, void *data)
{
	struct jh71xx_pmu *pmu = data;
	u32 val;

	val = readl(pmu->base + JH71XX_PMU_INT_STATUS);

	if (val & JH71XX_PMU_INT_SEQ_DONE)
		dev_dbg(pmu->dev, "sequence done.\n");
	if (val & JH71XX_PMU_INT_HW_REQ)
		dev_dbg(pmu->dev, "hardware encourage requestion.\n");
	if (val & JH71XX_PMU_INT_SW_FAIL)
		dev_err(pmu->dev, "software encourage fail.\n");
	if (val & JH71XX_PMU_INT_HW_FAIL)
		dev_err(pmu->dev, "hardware encourage fail.\n");
	if (val & JH71XX_PMU_INT_PCH_FAIL)
		dev_err(pmu->dev, "p-channel fail event.\n");

	/* clear interrupts */
	writel(val, pmu->base + JH71XX_PMU_INT_STATUS);
	writel(val, pmu->base + JH71XX_PMU_EVENT_STATUS);

	return IRQ_HANDLED;
}

static int jh7110_pmu_parse_irq(struct platform_device *pdev, struct jh71xx_pmu *pmu)
{
	struct device *dev = &pdev->dev;
	int ret;

	pmu->irq = platform_get_irq(pdev, 0);
	if (pmu->irq < 0)
		return pmu->irq;

	ret = devm_request_irq(dev, pmu->irq, jh71xx_pmu_interrupt,
			       0, pdev->name, pmu);
	if (ret)
		dev_err(dev, "failed to request irq\n");

	jh71xx_pmu_int_enable(pmu, JH71XX_PMU_INT_ALL_MASK & ~JH71XX_PMU_INT_PCH_FAIL, true);

	return 0;
}

static int jh71xx_pmu_init_domain(struct jh71xx_pmu *pmu, int index)
{
	struct jh71xx_pmu_dev *pmd;
	u32 pwr_mask;
	int ret;
	bool is_on = false;

	pmd = devm_kzalloc(pmu->dev, sizeof(*pmd), GFP_KERNEL);
	if (!pmd)
		return -ENOMEM;

	pmd->domain_info = &pmu->match_data->domain_info[index];
	pmd->pmu = pmu;
	pwr_mask = BIT(pmd->domain_info->bit);

	pmd->genpd.name = pmd->domain_info->name;
	pmd->genpd.flags = pmd->domain_info->flags;

	ret = jh71xx_pmu_get_state(pmd, pwr_mask, &is_on);
	if (ret)
		dev_warn(pmu->dev, "unable to get current state for %s\n",
			 pmd->genpd.name);

	pmd->genpd.power_on = jh71xx_pmu_on;
	pmd->genpd.power_off = jh71xx_pmu_off;
	pm_genpd_init(&pmd->genpd, NULL, !is_on);

	pmu->genpd_data.domains[index] = &pmd->genpd;

	return 0;
}

static int jh71xx_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct jh71xx_pmu_match_data *match_data;
	struct jh71xx_pmu *pmu;
	unsigned int i;
	int ret;

	pmu = devm_kzalloc(dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmu->base))
		return PTR_ERR(pmu->base);

	spin_lock_init(&pmu->lock);

	match_data = of_device_get_match_data(dev);
	if (!match_data)
		return -EINVAL;

	if (match_data->pmu_parse_irq) {
		ret = match_data->pmu_parse_irq(pdev, pmu);
		if (ret) {
			dev_err(dev, "failed to parse irq\n");
			return ret;
		}
	}

	pmu->genpd = devm_kcalloc(dev, match_data->num_domains,
				  sizeof(struct generic_pm_domain *),
				  GFP_KERNEL);
	if (!pmu->genpd)
		return -ENOMEM;

	pmu->dev = dev;
	pmu->match_data = match_data;
	pmu->genpd_data.domains = pmu->genpd;
	pmu->genpd_data.num_domains = match_data->num_domains;

	for (i = 0; i < match_data->num_domains; i++) {
		ret = jh71xx_pmu_init_domain(pmu, i);
		if (ret) {
			dev_err(dev, "failed to initialize power domain\n");
			return ret;
		}
	}

	ret = of_genpd_add_provider_onecell(np, &pmu->genpd_data);
	if (ret) {
		dev_err(dev, "failed to register genpd driver: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "registered %u power domains\n", i);

	return 0;
}

static const struct jh71xx_domain_info jh7110_power_domains[] = {
	[JH7110_PD_SYSTOP] = {
		.name = "SYSTOP",
		.bit = 0,
		.flags = GENPD_FLAG_ALWAYS_ON,
	},
	[JH7110_PD_CPU] = {
		.name = "CPU",
		.bit = 1,
		.flags = GENPD_FLAG_ALWAYS_ON,
	},
	[JH7110_PD_GPUA] = {
		.name = "GPUA",
		.bit = 2,
	},
	[JH7110_PD_VDEC] = {
		.name = "VDEC",
		.bit = 3,
	},
	[JH7110_PD_VOUT] = {
		.name = "VOUT",
		.bit = 4,
	},
	[JH7110_PD_ISP] = {
		.name = "ISP",
		.bit = 5,
	},
	[JH7110_PD_VENC] = {
		.name = "VENC",
		.bit = 6,
	},
};

static const struct jh71xx_pmu_match_data jh7110_pmu = {
	.num_domains = ARRAY_SIZE(jh7110_power_domains),
	.domain_info = jh7110_power_domains,
	.pmu_status = JH71XX_PMU_CURR_POWER_MODE,
	.pmu_parse_irq = jh7110_pmu_parse_irq,
	.pmu_set_state = jh7110_pmu_set_state,
};

static const struct jh71xx_domain_info jh7110_aon_power_domains[] = {
	[JH7110_PD_DPHY_TX] = {
		.name = "DPHY-TX",
		.bit = 30,
	},
	[JH7110_PD_DPHY_RX] = {
		.name = "DPHY-RX",
		.bit = 31,
	},
};

static const struct jh71xx_pmu_match_data jh7110_aon_pmu = {
	.num_domains = ARRAY_SIZE(jh7110_aon_power_domains),
	.domain_info = jh7110_aon_power_domains,
	.pmu_status = JH71XX_AON_PMU_SWITCH,
	.pmu_set_state = jh7110_aon_pmu_set_state,
};

static const struct of_device_id jh71xx_pmu_of_match[] = {
	{
		.compatible = "starfive,jh7110-pmu",
		.data = (void *)&jh7110_pmu,
	}, {
		.compatible = "starfive,jh7110-aon-syscon",
		.data = (void *)&jh7110_aon_pmu,
	}, {
		/* sentinel */
	}
};

static struct platform_driver jh71xx_pmu_driver = {
	.probe = jh71xx_pmu_probe,
	.driver = {
		.name = "jh71xx-pmu",
		.of_match_table = jh71xx_pmu_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(jh71xx_pmu_driver);

MODULE_AUTHOR("Walker Chen <walker.chen@starfivetech.com>");
MODULE_AUTHOR("Changhuang Liang <changhuang.liang@starfivetech.com>");
MODULE_DESCRIPTION("StarFive JH71XX PMU Driver");
MODULE_LICENSE("GPL");
