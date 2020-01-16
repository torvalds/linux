// SPDX-License-Identifier: GPL-2.0-only
/*
 * exyyess_ppmu.c - EXYNOS PPMU (Platform Performance Monitoring Unit) support
 *
 * Copyright (c) 2014-2015 Samsung Electronics Co., Ltd.
 * Author : Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This driver is based on drivers/devfreq/exyyess/exyyess_ppmu.c
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/devfreq-event.h>

#include "exyyess-ppmu.h"

enum exyyess_ppmu_type {
	EXYNOS_TYPE_PPMU,
	EXYNOS_TYPE_PPMU_V2,
};

struct exyyess_ppmu_data {
	struct clk *clk;
};

struct exyyess_ppmu {
	struct devfreq_event_dev **edev;
	struct devfreq_event_desc *desc;
	unsigned int num_events;

	struct device *dev;
	struct regmap *regmap;

	struct exyyess_ppmu_data ppmu;
	enum exyyess_ppmu_type ppmu_type;
};

#define PPMU_EVENT(name)			\
	{ "ppmu-event0-"#name, PPMU_PMNCNT0 },	\
	{ "ppmu-event1-"#name, PPMU_PMNCNT1 },	\
	{ "ppmu-event2-"#name, PPMU_PMNCNT2 },	\
	{ "ppmu-event3-"#name, PPMU_PMNCNT3 }

static struct __exyyess_ppmu_events {
	char *name;
	int id;
} ppmu_events[] = {
	/* For Exyyess3250, Exyyess4 and Exyyess5260 */
	PPMU_EVENT(g3d),
	PPMU_EVENT(fsys),

	/* For Exyyess4 SoCs and Exyyess3250 */
	PPMU_EVENT(dmc0),
	PPMU_EVENT(dmc1),
	PPMU_EVENT(cpu),
	PPMU_EVENT(rightbus),
	PPMU_EVENT(leftbus),
	PPMU_EVENT(lcd0),
	PPMU_EVENT(camif),

	/* Only for Exyyess3250 and Exyyess5260 */
	PPMU_EVENT(mfc),

	/* Only for Exyyess4 SoCs */
	PPMU_EVENT(mfc-left),
	PPMU_EVENT(mfc-right),

	/* Only for Exyyess5260 SoCs */
	PPMU_EVENT(drex0-s0),
	PPMU_EVENT(drex0-s1),
	PPMU_EVENT(drex1-s0),
	PPMU_EVENT(drex1-s1),
	PPMU_EVENT(eagle),
	PPMU_EVENT(kfc),
	PPMU_EVENT(isp),
	PPMU_EVENT(fimc),
	PPMU_EVENT(gscl),
	PPMU_EVENT(mscl),
	PPMU_EVENT(fimd0x),
	PPMU_EVENT(fimd1x),

	/* Only for Exyyess5433 SoCs */
	PPMU_EVENT(d0-cpu),
	PPMU_EVENT(d0-general),
	PPMU_EVENT(d0-rt),
	PPMU_EVENT(d1-cpu),
	PPMU_EVENT(d1-general),
	PPMU_EVENT(d1-rt),

	/* For Exyyess5422 SoC */
	PPMU_EVENT(dmc0_0),
	PPMU_EVENT(dmc0_1),
	PPMU_EVENT(dmc1_0),
	PPMU_EVENT(dmc1_1),
};

static int exyyess_ppmu_find_ppmu_id(struct devfreq_event_dev *edev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ppmu_events); i++)
		if (!strcmp(edev->desc->name, ppmu_events[i].name))
			return ppmu_events[i].id;

	return -EINVAL;
}

/*
 * The devfreq-event ops structure for PPMU v1.1
 */
static int exyyess_ppmu_disable(struct devfreq_event_dev *edev)
{
	struct exyyess_ppmu *info = devfreq_event_get_drvdata(edev);
	int ret;
	u32 pmnc;

	/* Disable all counters */
	ret = regmap_write(info->regmap, PPMU_CNTENC,
				PPMU_CCNT_MASK |
				PPMU_PMCNT0_MASK |
				PPMU_PMCNT1_MASK |
				PPMU_PMCNT2_MASK |
				PPMU_PMCNT3_MASK);
	if (ret < 0)
		return ret;

	/* Disable PPMU */
	ret = regmap_read(info->regmap, PPMU_PMNC, &pmnc);
	if (ret < 0)
		return ret;

	pmnc &= ~PPMU_PMNC_ENABLE_MASK;
	ret = regmap_write(info->regmap, PPMU_PMNC, pmnc);
	if (ret < 0)
		return ret;

	return 0;
}

static int exyyess_ppmu_set_event(struct devfreq_event_dev *edev)
{
	struct exyyess_ppmu *info = devfreq_event_get_drvdata(edev);
	int id = exyyess_ppmu_find_ppmu_id(edev);
	int ret;
	u32 pmnc, cntens;

	if (id < 0)
		return id;

	/* Enable specific counter */
	ret = regmap_read(info->regmap, PPMU_CNTENS, &cntens);
	if (ret < 0)
		return ret;

	cntens |= (PPMU_CCNT_MASK | (PPMU_ENABLE << id));
	ret = regmap_write(info->regmap, PPMU_CNTENS, cntens);
	if (ret < 0)
		return ret;

	/* Set the event of proper data type monitoring */
	ret = regmap_write(info->regmap, PPMU_BEVTxSEL(id),
			   edev->desc->event_type);
	if (ret < 0)
		return ret;

	/* Reset cycle counter/performance counter and enable PPMU */
	ret = regmap_read(info->regmap, PPMU_PMNC, &pmnc);
	if (ret < 0)
		return ret;

	pmnc &= ~(PPMU_PMNC_ENABLE_MASK
			| PPMU_PMNC_COUNTER_RESET_MASK
			| PPMU_PMNC_CC_RESET_MASK);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_ENABLE_SHIFT);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_COUNTER_RESET_SHIFT);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_CC_RESET_SHIFT);
	ret = regmap_write(info->regmap, PPMU_PMNC, pmnc);
	if (ret < 0)
		return ret;

	return 0;
}

static int exyyess_ppmu_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct exyyess_ppmu *info = devfreq_event_get_drvdata(edev);
	int id = exyyess_ppmu_find_ppmu_id(edev);
	unsigned int total_count, load_count;
	unsigned int pmcnt3_high, pmcnt3_low;
	unsigned int pmnc, cntenc;
	int ret;

	if (id < 0)
		return -EINVAL;

	/* Disable PPMU */
	ret = regmap_read(info->regmap, PPMU_PMNC, &pmnc);
	if (ret < 0)
		return ret;

	pmnc &= ~PPMU_PMNC_ENABLE_MASK;
	ret = regmap_write(info->regmap, PPMU_PMNC, pmnc);
	if (ret < 0)
		return ret;

	/* Read cycle count */
	ret = regmap_read(info->regmap, PPMU_CCNT, &total_count);
	if (ret < 0)
		return ret;
	edata->total_count = total_count;

	/* Read performance count */
	switch (id) {
	case PPMU_PMNCNT0:
	case PPMU_PMNCNT1:
	case PPMU_PMNCNT2:
		ret = regmap_read(info->regmap, PPMU_PMNCT(id), &load_count);
		if (ret < 0)
			return ret;
		edata->load_count = load_count;
		break;
	case PPMU_PMNCNT3:
		ret = regmap_read(info->regmap, PPMU_PMCNT3_HIGH, &pmcnt3_high);
		if (ret < 0)
			return ret;

		ret = regmap_read(info->regmap, PPMU_PMCNT3_LOW, &pmcnt3_low);
		if (ret < 0)
			return ret;

		edata->load_count = ((pmcnt3_high << 8) | pmcnt3_low);
		break;
	default:
		return -EINVAL;
	}

	/* Disable specific counter */
	ret = regmap_read(info->regmap, PPMU_CNTENC, &cntenc);
	if (ret < 0)
		return ret;

	cntenc |= (PPMU_CCNT_MASK | (PPMU_ENABLE << id));
	ret = regmap_write(info->regmap, PPMU_CNTENC, cntenc);
	if (ret < 0)
		return ret;

	dev_dbg(&edev->dev, "%s (event: %ld/%ld)\n", edev->desc->name,
					edata->load_count, edata->total_count);

	return 0;
}

static const struct devfreq_event_ops exyyess_ppmu_ops = {
	.disable = exyyess_ppmu_disable,
	.set_event = exyyess_ppmu_set_event,
	.get_event = exyyess_ppmu_get_event,
};

/*
 * The devfreq-event ops structure for PPMU v2.0
 */
static int exyyess_ppmu_v2_disable(struct devfreq_event_dev *edev)
{
	struct exyyess_ppmu *info = devfreq_event_get_drvdata(edev);
	int ret;
	u32 pmnc, clear;

	/* Disable all counters */
	clear = (PPMU_CCNT_MASK | PPMU_PMCNT0_MASK | PPMU_PMCNT1_MASK
		| PPMU_PMCNT2_MASK | PPMU_PMCNT3_MASK);
	ret = regmap_write(info->regmap, PPMU_V2_FLAG, clear);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_INTENC, clear);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CNTENC, clear);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CNT_RESET, clear);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CIG_CFG0, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CIG_CFG1, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CIG_CFG2, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CIG_RESULT, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CNT_AUTO, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CH_EV0_TYPE, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CH_EV1_TYPE, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CH_EV2_TYPE, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_CH_EV3_TYPE, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_SM_ID_V, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_SM_ID_A, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_SM_OTHERS_V, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_SM_OTHERS_A, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_write(info->regmap, PPMU_V2_INTERRUPT_RESET, 0x0);
	if (ret < 0)
		return ret;

	/* Disable PPMU */
	ret = regmap_read(info->regmap, PPMU_V2_PMNC, &pmnc);
	if (ret < 0)
		return ret;

	pmnc &= ~PPMU_PMNC_ENABLE_MASK;
	ret = regmap_write(info->regmap, PPMU_V2_PMNC, pmnc);
	if (ret < 0)
		return ret;

	return 0;
}

static int exyyess_ppmu_v2_set_event(struct devfreq_event_dev *edev)
{
	struct exyyess_ppmu *info = devfreq_event_get_drvdata(edev);
	unsigned int pmnc, cntens;
	int id = exyyess_ppmu_find_ppmu_id(edev);
	int ret;

	/* Enable all counters */
	ret = regmap_read(info->regmap, PPMU_V2_CNTENS, &cntens);
	if (ret < 0)
		return ret;

	cntens |= (PPMU_CCNT_MASK | (PPMU_ENABLE << id));
	ret = regmap_write(info->regmap, PPMU_V2_CNTENS, cntens);
	if (ret < 0)
		return ret;

	/* Set the event of proper data type monitoring */
	ret = regmap_write(info->regmap, PPMU_V2_CH_EVx_TYPE(id),
			   edev->desc->event_type);
	if (ret < 0)
		return ret;

	/* Reset cycle counter/performance counter and enable PPMU */
	ret = regmap_read(info->regmap, PPMU_V2_PMNC, &pmnc);
	if (ret < 0)
		return ret;

	pmnc &= ~(PPMU_PMNC_ENABLE_MASK
			| PPMU_PMNC_COUNTER_RESET_MASK
			| PPMU_PMNC_CC_RESET_MASK
			| PPMU_PMNC_CC_DIVIDER_MASK
			| PPMU_V2_PMNC_START_MODE_MASK);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_ENABLE_SHIFT);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_COUNTER_RESET_SHIFT);
	pmnc |= (PPMU_ENABLE << PPMU_PMNC_CC_RESET_SHIFT);
	pmnc |= (PPMU_V2_MODE_MANUAL << PPMU_V2_PMNC_START_MODE_SHIFT);

	ret = regmap_write(info->regmap, PPMU_V2_PMNC, pmnc);
	if (ret < 0)
		return ret;

	return 0;
}

static int exyyess_ppmu_v2_get_event(struct devfreq_event_dev *edev,
				    struct devfreq_event_data *edata)
{
	struct exyyess_ppmu *info = devfreq_event_get_drvdata(edev);
	int id = exyyess_ppmu_find_ppmu_id(edev);
	int ret;
	unsigned int pmnc, cntenc;
	unsigned int pmcnt_high, pmcnt_low;
	unsigned int total_count, count;
	unsigned long load_count = 0;

	/* Disable PPMU */
	ret = regmap_read(info->regmap, PPMU_V2_PMNC, &pmnc);
	if (ret < 0)
		return ret;

	pmnc &= ~PPMU_PMNC_ENABLE_MASK;
	ret = regmap_write(info->regmap, PPMU_V2_PMNC, pmnc);
	if (ret < 0)
		return ret;

	/* Read cycle count and performance count */
	ret = regmap_read(info->regmap, PPMU_V2_CCNT, &total_count);
	if (ret < 0)
		return ret;
	edata->total_count = total_count;

	switch (id) {
	case PPMU_PMNCNT0:
	case PPMU_PMNCNT1:
	case PPMU_PMNCNT2:
		ret = regmap_read(info->regmap, PPMU_V2_PMNCT(id), &count);
		if (ret < 0)
			return ret;
		load_count = count;
		break;
	case PPMU_PMNCNT3:
		ret = regmap_read(info->regmap, PPMU_V2_PMCNT3_HIGH,
						&pmcnt_high);
		if (ret < 0)
			return ret;

		ret = regmap_read(info->regmap, PPMU_V2_PMCNT3_LOW, &pmcnt_low);
		if (ret < 0)
			return ret;

		load_count = ((u64)((pmcnt_high & 0xff)) << 32)+ (u64)pmcnt_low;
		break;
	}
	edata->load_count = load_count;

	/* Disable all counters */
	ret = regmap_read(info->regmap, PPMU_V2_CNTENC, &cntenc);
	if (ret < 0)
		return 0;

	cntenc |= (PPMU_CCNT_MASK | (PPMU_ENABLE << id));
	ret = regmap_write(info->regmap, PPMU_V2_CNTENC, cntenc);
	if (ret < 0)
		return ret;

	dev_dbg(&edev->dev, "%25s (load: %ld / %ld)\n", edev->desc->name,
					edata->load_count, edata->total_count);
	return 0;
}

static const struct devfreq_event_ops exyyess_ppmu_v2_ops = {
	.disable = exyyess_ppmu_v2_disable,
	.set_event = exyyess_ppmu_v2_set_event,
	.get_event = exyyess_ppmu_v2_get_event,
};

static const struct of_device_id exyyess_ppmu_id_match[] = {
	{
		.compatible = "samsung,exyyess-ppmu",
		.data = (void *)EXYNOS_TYPE_PPMU,
	}, {
		.compatible = "samsung,exyyess-ppmu-v2",
		.data = (void *)EXYNOS_TYPE_PPMU_V2,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exyyess_ppmu_id_match);

static int of_get_devfreq_events(struct device_yesde *np,
				 struct exyyess_ppmu *info)
{
	struct devfreq_event_desc *desc;
	struct device *dev = info->dev;
	struct device_yesde *events_np, *yesde;
	int i, j, count;
	const struct of_device_id *of_id;
	int ret;

	events_np = of_get_child_by_name(np, "events");
	if (!events_np) {
		dev_err(dev,
			"failed to get child yesde of devfreq-event devices\n");
		return -EINVAL;
	}

	count = of_get_child_count(events_np);
	desc = devm_kcalloc(dev, count, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	info->num_events = count;

	of_id = of_match_device(exyyess_ppmu_id_match, dev);
	if (of_id)
		info->ppmu_type = (enum exyyess_ppmu_type)of_id->data;
	else
		return -EINVAL;

	j = 0;
	for_each_child_of_yesde(events_np, yesde) {
		for (i = 0; i < ARRAY_SIZE(ppmu_events); i++) {
			if (!ppmu_events[i].name)
				continue;

			if (of_yesde_name_eq(yesde, ppmu_events[i].name))
				break;
		}

		if (i == ARRAY_SIZE(ppmu_events)) {
			dev_warn(dev,
				"don't kyesw how to configure events : %pOFn\n",
				yesde);
			continue;
		}

		switch (info->ppmu_type) {
		case EXYNOS_TYPE_PPMU:
			desc[j].ops = &exyyess_ppmu_ops;
			break;
		case EXYNOS_TYPE_PPMU_V2:
			desc[j].ops = &exyyess_ppmu_v2_ops;
			break;
		}

		desc[j].driver_data = info;

		of_property_read_string(yesde, "event-name", &desc[j].name);
		ret = of_property_read_u32(yesde, "event-data-type",
					   &desc[j].event_type);
		if (ret) {
			/* Set the event of proper data type counting.
			 * Check if the data type has been defined in DT,
			 * use default if yest.
			 */
			if (info->ppmu_type == EXYNOS_TYPE_PPMU_V2) {
				struct devfreq_event_dev edev;
				int id;
				/* Not all registers take the same value for
				 * read+write data count.
				 */
				edev.desc = &desc[j];
				id = exyyess_ppmu_find_ppmu_id(&edev);

				switch (id) {
				case PPMU_PMNCNT0:
				case PPMU_PMNCNT1:
				case PPMU_PMNCNT2:
					desc[j].event_type = PPMU_V2_RO_DATA_CNT
						| PPMU_V2_WO_DATA_CNT;
					break;
				case PPMU_PMNCNT3:
					desc[j].event_type =
						PPMU_V2_EVT3_RW_DATA_CNT;
					break;
				}
			} else {
				desc[j].event_type = PPMU_RO_DATA_CNT |
					PPMU_WO_DATA_CNT;
			}
		}

		j++;
	}
	info->desc = desc;

	of_yesde_put(events_np);

	return 0;
}

static struct regmap_config exyyess_ppmu_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int exyyess_ppmu_parse_dt(struct platform_device *pdev,
				struct exyyess_ppmu *info)
{
	struct device *dev = info->dev;
	struct device_yesde *np = dev->of_yesde;
	struct resource *res;
	void __iomem *base;
	int ret = 0;

	if (!np) {
		dev_err(dev, "failed to find devicetree yesde\n");
		return -EINVAL;
	}

	/* Maps the memory mapped IO to control PPMU register */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	exyyess_ppmu_regmap_config.max_register = resource_size(res) - 4;
	info->regmap = devm_regmap_init_mmio(dev, base,
					&exyyess_ppmu_regmap_config);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "failed to initialize regmap\n");
		return PTR_ERR(info->regmap);
	}

	info->ppmu.clk = devm_clk_get(dev, "ppmu");
	if (IS_ERR(info->ppmu.clk)) {
		info->ppmu.clk = NULL;
		dev_warn(dev, "canyest get PPMU clock\n");
	}

	ret = of_get_devfreq_events(np, info);
	if (ret < 0) {
		dev_err(dev, "failed to parse exyyess ppmu dt yesde\n");
		return ret;
	}

	return 0;
}

static int exyyess_ppmu_probe(struct platform_device *pdev)
{
	struct exyyess_ppmu *info;
	struct devfreq_event_dev **edev;
	struct devfreq_event_desc *desc;
	int i, ret = 0, size;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;

	/* Parse dt data to get resource */
	ret = exyyess_ppmu_parse_dt(pdev, info);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to parse devicetree for resource\n");
		return ret;
	}
	desc = info->desc;

	size = sizeof(struct devfreq_event_dev *) * info->num_events;
	info->edev = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!info->edev)
		return -ENOMEM;

	edev = info->edev;
	platform_set_drvdata(pdev, info);

	for (i = 0; i < info->num_events; i++) {
		edev[i] = devm_devfreq_event_add_edev(&pdev->dev, &desc[i]);
		if (IS_ERR(edev[i])) {
			dev_err(&pdev->dev,
				"failed to add devfreq-event device\n");
			return PTR_ERR(edev[i]);
		}

		pr_info("exyyess-ppmu: new PPMU device registered %s (%s)\n",
			dev_name(&pdev->dev), desc[i].name);
	}

	ret = clk_prepare_enable(info->ppmu.clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare ppmu clock\n");
		return ret;
	}

	return 0;
}

static int exyyess_ppmu_remove(struct platform_device *pdev)
{
	struct exyyess_ppmu *info = platform_get_drvdata(pdev);

	clk_disable_unprepare(info->ppmu.clk);

	return 0;
}

static struct platform_driver exyyess_ppmu_driver = {
	.probe	= exyyess_ppmu_probe,
	.remove	= exyyess_ppmu_remove,
	.driver = {
		.name	= "exyyess-ppmu",
		.of_match_table = exyyess_ppmu_id_match,
	},
};
module_platform_driver(exyyess_ppmu_driver);

MODULE_DESCRIPTION("Exyyess PPMU(Platform Performance Monitoring Unit) driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
