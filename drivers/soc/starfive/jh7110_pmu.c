// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * JH7110 Power Domain Controller Driver
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <dt-bindings/power/jh7110-power.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/pm_domain.h>
#include <linux/of.h>
#include <linux/of_device.h>

/* register define */
#define HW_EVENT_TURN_ON_MASK		0x04
#define HW_EVENT_TURN_OFF_MASK		0x08
#define SW_TURN_ON_POWER_MODE		0x0C
#define SW_TURN_OFF_POWER_MODE		0x10
#define SW_ENCOURAGE			0x44
#define PMU_INT_MASK			0x48
#define PCH_BYPASS			0x4C
#define PCH_PSTATE			0x50
#define PCH_TIMEOUT			0x54
#define LP_TIMEOUT			0x58
#define HW_TURN_ON_MODE			0x5C
#define CURR_POWER_MODE			0x80
#define PMU_EVENT_STATUS		0x88
#define PMU_INT_STATUS			0x8C

/* sw encourage cfg */
#define SW_MODE_ENCOURAGE_EN_LO		0x05
#define SW_MODE_ENCOURAGE_EN_HI		0x50
#define SW_MODE_ENCOURAGE_DIS_LO	0x0A
#define SW_MODE_ENCOURAGE_DIS_HI	0xA0
#define SW_MODE_ENCOURAGE_ON		0xFF

/* pmu int status */
#define PMU_INT_SEQ_DONE		BIT(0)
#define PMU_INT_HW_REQ			BIT(1)
#define PMU_INT_SW_FAIL			GENMASK(3, 2)
#define PMU_INT_HW_FAIL			GENMASK(5, 4)
#define PMU_INT_PCH_FAIL		GENMASK(8, 6)
#define PMU_INT_FAIL_MASK		(PMU_INT_SW_FAIL | \
					PMU_INT_HW_FAIL | \
					PMU_INT_PCH_FAIL)
#define PMU_INT_ALL_MASK		(PMU_INT_SEQ_DONE | \
					PMU_INT_HW_REQ | \
					PMU_INT_FAIL_MASK)

struct jh7110_power_dev {
	struct generic_pm_domain genpd;
	struct jh7110_pmu *power;
	uint32_t mask;
};

struct jh7110_pmu {
	void __iomem *base;
	spinlock_t lock;
	int irq;
	struct device *pdev;
	struct jh7110_power_dev *dev;
	struct genpd_onecell_data genpd_data;
	struct generic_pm_domain **genpd;
};

struct jh7110_pmu_data {
	const char * const name;
	uint8_t bit;
	unsigned int flags;
};

static void __iomem *pmu_base;

static inline void pmu_writel(u32 val, u32 offset)
{
	writel(val, pmu_base + offset);
}

void starfive_pmu_hw_event_turn_off_mask(u32 mask)
{
	pmu_writel(mask, HW_EVENT_TURN_OFF_MASK);
}
EXPORT_SYMBOL(starfive_pmu_hw_event_turn_off_mask);

static int jh7110_pmu_get_state(struct jh7110_power_dev *pmd, bool *is_on)
{
	struct jh7110_pmu *pmu = pmd->power;

	if (!pmd->mask) {
		*is_on = false;
		return -EINVAL;
	}

	*is_on = __raw_readl(pmu->base + CURR_POWER_MODE) & pmd->mask;

	return 0;
}

static int jh7110_pmu_set_state(struct jh7110_power_dev *pmd, bool on)
{
	struct jh7110_pmu *pmu = pmd->power;
	unsigned long flags;
	uint32_t val;
	uint32_t mode;
	uint32_t encourage_lo;
	uint32_t encourage_hi;
	bool is_on;
	int ret;

	if (!pmd->mask)
		return -EINVAL;
	ret = jh7110_pmu_get_state(pmd, &is_on);
	if (ret)
		dev_info(pmu->pdev, "unable to get current state for %s\n",
				pmd->genpd.name);
	if (is_on == on) {
		dev_info(pmu->pdev, "pm domain is already %sable status.\n",
				on ? "en" : "dis");
		return 0;
	}

	spin_lock_irqsave(&pmu->lock, flags);

	if (on) {
		mode = SW_TURN_ON_POWER_MODE;
		encourage_lo = SW_MODE_ENCOURAGE_EN_LO;
		encourage_hi = SW_MODE_ENCOURAGE_EN_HI;
	} else {
		mode = SW_TURN_OFF_POWER_MODE;
		encourage_lo = SW_MODE_ENCOURAGE_DIS_LO;
		encourage_hi = SW_MODE_ENCOURAGE_DIS_HI;
	}

	val = __raw_readl(pmu->base + mode);
	val |= pmd->mask;
	__raw_writel(val, pmu->base + mode);

	/* write SW_ENCOURAGE to make the configuration take effect */
	__raw_writel(SW_MODE_ENCOURAGE_ON, pmu->base + SW_ENCOURAGE);
	__raw_writel(encourage_lo, pmu->base + SW_ENCOURAGE);
	__raw_writel(encourage_hi, pmu->base + SW_ENCOURAGE);

	spin_unlock_irqrestore(&pmu->lock, flags);

	return 0;
}

static int jh7110_pmu_on(struct generic_pm_domain *genpd)
{
	struct jh7110_power_dev *pmd = container_of(genpd,
		struct jh7110_power_dev, genpd);

	return jh7110_pmu_set_state(pmd, true);
}

static int jh7110_pmu_off(struct generic_pm_domain *genpd)
{
	struct jh7110_power_dev *pmd = container_of(genpd,
		struct jh7110_power_dev, genpd);

	return jh7110_pmu_set_state(pmd, false);
}

static void starfive_pmu_int_enable(struct jh7110_pmu *pmu, u32 mask, bool enable)
{
	u32 val = __raw_readl(pmu->base + PMU_INT_MASK);

	if (enable)
		val &= ~mask;
	else
		val |= mask;

	__raw_writel(val, pmu->base + PMU_INT_MASK);
}

static irqreturn_t starfive_pmu_interrupt(int irq, void *data)
{
	struct jh7110_pmu *pmu = data;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&pmu->lock, flags);
	val = __raw_readl(pmu->base + PMU_INT_STATUS);

	if (val & PMU_INT_SEQ_DONE)
		dev_dbg(pmu->pdev, "sequence done.\n");
	if (val & PMU_INT_HW_REQ)
		dev_dbg(pmu->pdev, "hardware encourage requestion.\n");
	if (val & PMU_INT_SW_FAIL)
		dev_err(pmu->pdev, "software encourage fail.\n");
	if (val & PMU_INT_HW_FAIL)
		dev_err(pmu->pdev, "hardware encourage fail.\n");
	if (val & PMU_INT_PCH_FAIL)
		dev_err(pmu->pdev, "p-channel fail event.\n");

	/* clear interrupts */
	__raw_writel(val, pmu->base + PMU_INT_STATUS);
	__raw_writel(val, pmu->base + PMU_EVENT_STATUS);

	spin_unlock_irqrestore(&pmu->lock, flags);

	return IRQ_HANDLED;
}

static int jh7110_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;
	const struct jh7110_pmu_data *entry, *table;
	struct jh7110_pmu *pmu;
	unsigned int i;
	uint8_t max_bit = 0;
	int ret;

	pmu = devm_kzalloc(dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pmu_base = pmu->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmu->base))
		return PTR_ERR(pmu->base);

	/* initialize pmu interrupt  */
	pmu->irq = platform_get_irq(pdev, 0);
	if (pmu->irq < 0)
		return pmu->irq;

	ret = devm_request_irq(dev, pmu->irq, starfive_pmu_interrupt,
			       0, pdev->name, pmu);
	if (ret)
		dev_err(dev, "request irq failed.\n");

	table = of_device_get_match_data(dev);
	if (!table)
		return -EINVAL;

	pmu->pdev = dev;
	pmu->genpd_data.num_domains = 0;
	i = 0;
	for (entry = table; entry->name; entry++) {
		max_bit = max(max_bit, entry->bit);
		i++;
	}

	if (!i)
		return -ENODEV;

	pmu->genpd_data.num_domains = max_bit + 1;

	pmu->dev = devm_kcalloc(dev, pmu->genpd_data.num_domains,
				  sizeof(struct jh7110_power_dev),
				  GFP_KERNEL);
	if (!pmu->dev)
		return -ENOMEM;

	pmu->genpd = devm_kcalloc(dev, pmu->genpd_data.num_domains,
				    sizeof(struct generic_pm_domain *),
				    GFP_KERNEL);
	if (!pmu->genpd)
		return -ENOMEM;

	pmu->genpd_data.domains = pmu->genpd;

	i = 0;
	for (entry = table; entry->name; entry++) {
		struct jh7110_power_dev *pmd = &pmu->dev[i];
		bool is_on;

		pmd->power = pmu;
		pmd->mask = BIT(entry->bit);
		pmd->genpd.name = entry->name;
		pmd->genpd.flags = entry->flags;

		ret = jh7110_pmu_get_state(pmd, &is_on);
		if (ret)
			dev_warn(dev, "unable to get current state for %s\n",
				 pmd->genpd.name);

		pmd->genpd.power_on = jh7110_pmu_on;
		pmd->genpd.power_off = jh7110_pmu_off;

		pm_genpd_init(&pmd->genpd, NULL, !is_on);
		pmu->genpd[entry->bit] = &pmd->genpd;

		i++;
	}

	spin_lock_init(&pmu->lock);
	starfive_pmu_int_enable(pmu, PMU_INT_ALL_MASK & ~PMU_INT_PCH_FAIL, true);

	ret = of_genpd_add_provider_onecell(np, &pmu->genpd_data);
	if (ret) {
		dev_err(dev, "failed to register genpd driver: %d\n", ret);
		return ret;
	}

	dev_info(dev, "registered %u power domains\n", i);

	return 0;
}

static const struct jh7110_pmu_data jh7110_power_domains[] = {
	{
		.name = "SYSTOP",
		.bit = JH7110_PD_SYSTOP,
		.flags = GENPD_FLAG_ALWAYS_ON,
	}, {
		.name = "CPU",
		.bit = JH7110_PD_CPU,
		.flags = GENPD_FLAG_ALWAYS_ON,
	}, {
		.name = "GPUA",
		.bit = JH7110_PD_GPUA,
	}, {
		.name = "VDEC",
		.bit = JH7110_PD_VDEC,
	}, {
		.name = "VOUT",
		.bit = JH7110_PD_VOUT,
	}, {
		.name = "ISP",
		.bit = JH7110_PD_ISP,
	}, {
		.name = "VENC",
		.bit = JH7110_PD_VENC,
	}, {
		.name = "GPUB",
		.bit = JH7110_PD_GPUB,
	}, {
		/* sentinel */
	},
};

static const struct of_device_id jh7110_pmu_of_match[] = {
	{
		.compatible = "starfive,jh7110-pmu",
		.data = &jh7110_power_domains,
	}, {
		/* sentinel */
	}
};

static struct platform_driver jh7110_pmu_driver = {
	.driver = {
		.name = "jh7110-pmu",
		.of_match_table = jh7110_pmu_of_match,
	},
	.probe  = jh7110_pmu_probe,
};
builtin_platform_driver(jh7110_pmu_driver);

MODULE_AUTHOR("Walker Chen <walker.chen@starfivetech.com>");
MODULE_DESCRIPTION("Starfive JH7110 Power Domain Driver");
MODULE_LICENSE("GPL");
