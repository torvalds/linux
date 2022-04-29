// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PMU driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 samin <samin.guo@starfivetech.com>
 */
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <soc/starfive/jh7110_pmu.h>

/* register define */
#define HW_EVENT_TURN_ON_MASK		0x04
#define HW_EVENT_TURN_OFF_MASK		0x08
#define SW_TURN_ON_POWER_MODE		0x0C
#define SW_TURN_OFF_POWER_MODE		0x10
#define THE_SHOLD_SEQ_TIMEOUT		0x14
#define POWER_DOMAIN_CASCADE_0		0x18
#define POWER_DOMAIN_CASCADE_1		0x1C
#define POWER_DOMAIN_CASCADE_2		0x20
#define POWER_DOMAIN_CASCADE_3		0x24
#define POWER_DOMAIN_CASCADE_4		0x28
#define POWER_DOMAIN_CASCADE_5		0x2C
#define POWER_DOMAIN_CASCADE_6		0x30
#define POWER_DOMAIN_CASCADE_7		0x34
#define POWER_DOMAIN_CASCADE_8		0x38
#define POWER_DOMAIN_CASCADE_9		0x3C
#define POWER_DOMAIN_CASCADE_10		0x40
#define SW_ENCOURAGE			0x44
#define PMU_INT_MASK			0x48
#define PCH_BYPASS			0x4C
#define PCH_PSTATE			0x50
#define PCH_TIMEOUT			0x54
#define LP_TIMEOUT			0x58
#define HW_TURN_ON_MODE			0x5C
#define CURR_POWER_MODE			0x80
#define CURR_SEQ_STATE			0x84
#define PMU_EVENT_STATUS		0x88
#define PMU_INT_STATUS			0x8C
#define HW_EVENT_RECORD			0x90
#define HW_EVENT_TYPE_RECORD		0x94
#define PCH_PACTIVE_STATUS		0x98

/* pmu int status */
#define PMU_INT_SEQ_DONE		BIT(0)
#define PMU_INT_HW_REQ			BIT(1)
#define PMU_INT_SW_FAIL			GENMASK(3, 2)
#define PMU_INT_HW_FAIL			GENMASK(5, 4)
#define PMU_INT_PCH_FAIL		GENMASK(8, 6)
#define PMU_INT_FAIL_MASK		(PMU_INT_SW_FAIL|\
					PMU_INT_HW_FAIL	|\
					PMU_INT_PCH_FAIL)
#define PMU_INT_ALL_MASK		(PMU_INT_SEQ_DONE|\
					PMU_INT_HW_REQ	|\
					PMU_INT_FAIL_MASK)

/* sw encourage cfg */
#define SW_MODE_ENCOURAGE_EN_LO		0x05
#define SW_MODE_ENCOURAGE_EN_HI		0x50
#define SW_MODE_ENCOURAGE_DIS_LO	0x0A
#define SW_MODE_ENCOURAGE_DIS_HI	0xA0
#define SW_MODE_ENCOURAGE_ON		0xFF

u32 power_domain_cascade[] = {
	GENMASK(4, 0),
	GENMASK(9, 5),
	GENMASK(14, 10),
	GENMASK(19, 15),
	GENMASK(24, 20),
	GENMASK(29, 25)
};

static void __iomem *pmu_base;

struct starfive_pmu {
	struct device	*dev;
	spinlock_t lock;
	int irq;
};

static inline u32 pmu_readl(u32 offset)
{
	return readl(pmu_base + offset);
}

static inline void pmu_writel(u32 val, u32 offset)
{
	writel(val, pmu_base + offset);
}

static bool pmu_get_current_power_mode(u32 domain)
{
	return pmu_readl(CURR_POWER_MODE) & domain;
}

static void starfive_pmu_int_enable(u32 mask, bool enable)
{
	u32 val = pmu_readl(PMU_INT_MASK);

	if (enable)
		val &= ~mask;
	else
		val |= mask;

	pmu_writel(val, PMU_INT_MASK);
}
/*
 * mask the hw_evnet
 */
static void starfive_pmu_hw_event_turn_on_mask(u32 hw_event, bool mask)
{
	u32 val = pmu_readl(HW_EVENT_TURN_ON_MASK);

	if (mask)
		val |= hw_event;
	else
		val &= ~hw_event;

	pmu_writel(val, HW_EVENT_TURN_ON_MASK);
}

void starfive_pmu_hw_event_turn_off_mask(u32 mask)
{
	pmu_writel(mask, HW_EVENT_TURN_OFF_MASK);
}
EXPORT_SYMBOL(starfive_pmu_hw_event_turn_off_mask);

void starfive_power_domain_set(u32 domain, bool enable)
{
	u32 val, mode;
	u32 encourage_lo, encourage_hi;

	if (!(pmu_get_current_power_mode(domain) ^ enable))
		return;

	if (enable) {
		mode = SW_TURN_ON_POWER_MODE;
		encourage_lo = SW_MODE_ENCOURAGE_EN_LO;
		encourage_hi = SW_MODE_ENCOURAGE_EN_HI;
	} else {
		mode = SW_TURN_OFF_POWER_MODE;
		encourage_lo = SW_MODE_ENCOURAGE_DIS_LO;
		encourage_hi = SW_MODE_ENCOURAGE_DIS_HI;
	}

	pr_debug("[pmu]domain: %#x %sable\n", domain, enable ? "en" : "dis");
	val = pmu_readl(mode);
	val |= domain;
	pmu_writel(val, mode);

	/* write SW_ENCOURAGE to make the configuration take effect */
	pmu_writel(SW_MODE_ENCOURAGE_ON, SW_ENCOURAGE);
	pmu_writel(encourage_lo, SW_ENCOURAGE);
	pmu_writel(encourage_hi, SW_ENCOURAGE);
}
EXPORT_SYMBOL(starfive_power_domain_set);

void starfive_power_domain_set_by_hwevent(u32 domain, u32 event, bool enable)
{
	u32 val;

	val = pmu_readl(HW_TURN_ON_MODE);

	if (enable)
		val |= domain;
	else
		val &= ~domain;

	pmu_writel(val, HW_TURN_ON_MODE);

	starfive_pmu_hw_event_turn_on_mask(event, enable);
}
EXPORT_SYMBOL(starfive_power_domain_set_by_hwevent);

static irqreturn_t starfive_pmu_interrupt(int irq, void *data)
{
	struct starfive_pmu *pmu = data;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&pmu->lock, flags);
	val = pmu_readl(PMU_INT_STATUS);

	if (val & PMU_INT_SEQ_DONE)
		dev_dbg(pmu->dev, "sequence done.\n");
	if (val & PMU_INT_HW_REQ)
		dev_dbg(pmu->dev, "hardware encourage requestion.\n");
	if (val & PMU_INT_SW_FAIL)
		dev_err(pmu->dev, "software encourage fail.\n");
	if (val & PMU_INT_HW_FAIL)
		dev_err(pmu->dev, "hardware encourage fail.\n");
	if (val & PMU_INT_PCH_FAIL)
		dev_err(pmu->dev, "p-channel fail event.\n");

	/* clear interrupts */
	pmu_writel(val, PMU_INT_STATUS);
	pmu_writel(val, PMU_EVENT_STATUS);

	spin_unlock_irqrestore(&pmu->lock, flags);

	return IRQ_HANDLED;
}

static int starfive_pmu_pad_order_get(u32 domain, bool on_off)
{
	unsigned int group;
	u32 val, offset;

	group = domain / 3;
	offset = (domain % 3) << 1;

	val = pmu_readl(POWER_DOMAIN_CASCADE_0 + group * 4);
	if (on_off)
		val &= power_domain_cascade[offset + 1];
	else
		val &= power_domain_cascade[offset];

	return val;
}

static void starfive_pmu_pad_order_set(u32 domain, bool on_off, u32 order)
{
	unsigned int group;
	u32 val, offset;

	group = domain / 3;
	offset = (domain % 3) << 1;

	val = pmu_readl(POWER_DOMAIN_CASCADE_0 + group * 4);
	if (on_off)
		val |= (order << __ffs(power_domain_cascade[offset + 1]));
	else
		val |= (order << __ffs(power_domain_cascade[offset]));

	pmu_writel(val, POWER_DOMAIN_CASCADE_0 + group * 4);
}

int starfive_power_domain_order_on_get(u32 domain)
{
	return starfive_pmu_pad_order_get(domain, true);
}
EXPORT_SYMBOL(starfive_power_domain_order_on_get);

int starfive_power_domain_order_off_get(u32 domain)
{
	return starfive_pmu_pad_order_get(domain, false);
}
EXPORT_SYMBOL(starfive_power_domain_order_off_get);

void starfive_power_domain_order_on_set(u32 domain, u32 order)
{
	starfive_pmu_pad_order_set(domain, true, order);
}
EXPORT_SYMBOL(starfive_power_domain_order_on_set);

void starfive_power_domain_order_off_set(u32 domain, u32 order)
{
	starfive_pmu_pad_order_set(domain, false, order);
}
EXPORT_SYMBOL(starfive_power_domain_order_off_set);

static int starfive_pmu_probe(struct platform_device *pdev)
{
	struct starfive_pmu *pmu;
	struct device *dev = &pdev->dev;
	int ret;

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	pmu->dev = dev;
	dev->driver_data = pmu;

	pmu_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmu_base))
		return PTR_ERR(pmu_base);

	pmu->irq = platform_get_irq(pdev, 0);
	if (pmu->irq < 0)
		return pmu->irq;

	ret = devm_request_irq(dev, pmu->irq, starfive_pmu_interrupt,
			       0, pdev->name, pmu);
	if (ret)
		dev_err(dev, "request irq failed.\n");

	spin_lock_init(&pmu->lock);
	starfive_pmu_int_enable(PMU_INT_ALL_MASK, true);

	return ret;
}

static int starfive_pmu_remove(struct platform_device *dev)
{
	starfive_pmu_int_enable(PMU_INT_ALL_MASK, false);

	return 0;
}

static const struct of_device_id starfive_pmu_dt_ids[] = {
	{ .compatible = "starfive,jh7110-pmu" },
	{ /* sentinel */ }
};

static struct platform_driver starfive_pmu_driver = {
	.probe		= starfive_pmu_probe,
	.remove		= starfive_pmu_remove,
	.driver		= {
		.name	= "starfive-pmu",
		.of_match_table = starfive_pmu_dt_ids,
	},
};

static int __init starfive_pmu_init(void)
{
	return platform_driver_register(&starfive_pmu_driver);
}

static void __exit starfive_pmu_exit(void)
{
	platform_driver_unregister(&starfive_pmu_driver);
}
subsys_initcall(starfive_pmu_init);
module_exit(starfive_pmu_exit);

MODULE_AUTHOR("samin <samin.guo@starfivetech.com>");
MODULE_DESCRIPTION("StarFive JH7110 PMU Device Driver");
MODULE_LICENSE("GPL v2");
