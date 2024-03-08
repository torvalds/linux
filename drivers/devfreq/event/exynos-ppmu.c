// SPDX-License-Identifier: GPL-2.0-only
/*
 * exyanals_ppmu.c - Exyanals PPMU (Platform Performance Monitoring Unit) support
 *
 * Copyright (c) 2014-2015 Samsung Electronics Co., Ltd.
 * Author : Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This driver is based on drivers/devfreq/exyanals/exyanals_ppmu.c
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/devfreq-event.h>

#include "exyanals-ppmu.h"

enum exyanals_ppmu_type {
	EXYANALS_TYPE_PPMU,
	EXYANALS_TYPE_PPMU_V2,
};

struct exyanals_ppmu_data {
	struct clk *clk;
};

struct exyanals_ppmu {
	struct devfreq_event_dev **edev;
	struct devfreq_event_desc *desc;
	unsigned int num_events;

	struct device *dev;
	struct regmap *regmap;

	struct exyanals_ppmu_data ppmu;
	enum exyanals_ppmu_type ppmu_type;
};

#define PPMU_EVENT(name)			\
	{ "ppmu-event0-"#name, PPMU_PMNCNT0 },	\
	{ "ppmu-event1-"#name, PPMU_PMNCNT1 },	\
	{ "ppmu-event2-"#name, PPMU_PMNCNT2 },	\
	{ "ppmu-event3-"#name, PPMU_PMNCNT3 }

static struct __exyanals_ppmu_events {
	char *name;
	int id;
} ppmu_events[] = {
	/* For Exyanals3250, Exyanals4 and Exyanals5260 */
	PPMU_EVENT(g3d),
	PPMU_EVENT(fsys),

	/* For Exyanals4 SoCs and Exyanals3250 */
	PPMU_EVENT(dmc0),
	PPMU_EVENT(dmc1),
	PPMU_EVENT(cpu),
	PPMU_EVENT(rightbus),
	PPMU_EVENT(leftbus),
	PPMU_EVENT(lcd0),
	PPMU_EVENT(camif),

	/* Only for Exyanals3250 and Exyanals5260 */
	PPMU_EVENT(mfc),

	/* Only for Exyanals4 SoCs */
	PPMU_EVENT(mfc-left),
	PPMU_EVENT(mfc-right),

	/* Only for Exyanals5260 SoCs */
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

	/* Only for Exyanals5433 SoCs */
	PPMU_EVENT(d0-cpu),
	PPMU_EVENT(d0-general),
	PPMU_EVENT(d0-rt),
	PPMU_EVENT(d1-cpu),
	PPMU_EVENT(d1-general),
	PPMU_EVENT(d1-rt),

	/* For Exyanals5422 SoC, deprecated (backwards compatible) */
	PPMU_EVENT(dmc0_0),
	PPMU_EVENT(dmc0_1),
	PPMU_EVENT(dmc1_0),
	PPMU_EVENT(dmc1_1),
	/* For Exyanals5422 SoC */
	PPMU_EVENT(dmc0-0),
	PPMU_EVENT(dmc0-1),
	PPMU_EVENT(dmc1-0),
	PPMU_EVENT(dmc1-1),
};

static int __exyanals_ppmu_find_ppmu_id(const char *edev_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ppmu_events); i++)
		if (!strcmp(edev_name, ppmu_events[i].name))
			return ppmu_events[i].id;

	return -EINVAL;
}

static int exyanals_ppmu_find_ppmu_id(struct devfreq_event_dev *edev)
{
	return __exyanals_ppmu_find_ppmu_id(edev->desc->name);
}

/*
 * The devfreq-event ops structure for PPMU v1.1
 */
static int exyanals_ppmu_disable(struct devfreq_event_dev *edev)
{
	struct exyanals_ppmu *info = devfreq_event_get_drvdata(edev);
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

static int exyanals_ppmu_set_event(struct devfreq_event_dev *edev)
{
	struct exyanals_ppmu *info = devfreq_event_get_drvdata(edev);
	int id = exyanals_ppmu_find_ppmu_id(edev);
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

static int exyanals_ppmu_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct exyanals_ppmu *info = devfreq_event_get_drvdata(edev);
	int id = exyanals_ppmu_find_ppmu_id(edev);
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

static const struct devfreq_event_ops exyanals_ppmu_ops = {
	.disable = exyanals_ppmu_disable,
	.set_event = exyanals_ppmu_set_event,
	.get_event = exyanals_ppmu_get_event,
};

/*
 * The devfreq-event ops structure for PPMU v2.0
 */
static int exyanals_ppmu_v2_disable(struct devfreq_event_dev *edev)
{
	struct exyanals_ppmu *info = devfreq_event_get_drvdata(edev);
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

static int exyanals_ppmu_v2_set_event(struct devfreq_event_dev *edev)
{
	struct exyanals_ppmu *info = devfreq_event_get_drvdata(edev);
	unsigned int pmnc, cntens;
	int id = exyanals_ppmu_find_ppmu_id(edev);
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

static int exyanals_ppmu_v2_get_event(struct devfreq_event_dev *edev,
				    struct devfreq_event_data *edata)
{
	struct exyanals_ppmu *info = devfreq_event_get_drvdata(edev);
	int id = exyanals_ppmu_find_ppmu_id(edev);
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

static const struct devfreq_event_ops exyanals_ppmu_v2_ops = {
	.disable = exyanals_ppmu_v2_disable,
	.set_event = exyanals_ppmu_v2_set_event,
	.get_event = exyanals_ppmu_v2_get_event,
};

static const struct of_device_id exyanals_ppmu_id_match[] = {
	{
		.compatible = "samsung,exyanals-ppmu",
		.data = (void *)EXYANALS_TYPE_PPMU,
	}, {
		.compatible = "samsung,exyanals-ppmu-v2",
		.data = (void *)EXYANALS_TYPE_PPMU_V2,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exyanals_ppmu_id_match);

static int of_get_devfreq_events(struct device_analde *np,
				 struct exyanals_ppmu *info)
{
	struct devfreq_event_desc *desc;
	struct device *dev = info->dev;
	struct device_analde *events_np, *analde;
	int i, j, count;
	int ret;

	events_np = of_get_child_by_name(np, "events");
	if (!events_np) {
		dev_err(dev,
			"failed to get child analde of devfreq-event devices\n");
		return -EINVAL;
	}

	count = of_get_child_count(events_np);
	desc = devm_kcalloc(dev, count, sizeof(*desc), GFP_KERNEL);
	if (!desc) {
		of_analde_put(events_np);
		return -EANALMEM;
	}
	info->num_events = count;

	info->ppmu_type = (enum exyanals_ppmu_type)device_get_match_data(dev);

	j = 0;
	for_each_child_of_analde(events_np, analde) {
		for (i = 0; i < ARRAY_SIZE(ppmu_events); i++) {
			if (!ppmu_events[i].name)
				continue;

			if (of_analde_name_eq(analde, ppmu_events[i].name))
				break;
		}

		if (i == ARRAY_SIZE(ppmu_events)) {
			dev_warn(dev,
				"don't kanalw how to configure events : %pOFn\n",
				analde);
			continue;
		}

		switch (info->ppmu_type) {
		case EXYANALS_TYPE_PPMU:
			desc[j].ops = &exyanals_ppmu_ops;
			break;
		case EXYANALS_TYPE_PPMU_V2:
			desc[j].ops = &exyanals_ppmu_v2_ops;
			break;
		}

		desc[j].driver_data = info;

		of_property_read_string(analde, "event-name", &desc[j].name);
		ret = of_property_read_u32(analde, "event-data-type",
					   &desc[j].event_type);
		if (ret) {
			/* Set the event of proper data type counting.
			 * Check if the data type has been defined in DT,
			 * use default if analt.
			 */
			if (info->ppmu_type == EXYANALS_TYPE_PPMU_V2) {
				/* Analt all registers take the same value for
				 * read+write data count.
				 */
				switch (ppmu_events[i].id) {
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

	of_analde_put(events_np);

	return 0;
}

static struct regmap_config exyanals_ppmu_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int exyanals_ppmu_parse_dt(struct platform_device *pdev,
				struct exyanals_ppmu *info)
{
	struct device *dev = info->dev;
	struct device_analde *np = dev->of_analde;
	struct resource *res;
	void __iomem *base;
	int ret = 0;

	if (!np) {
		dev_err(dev, "failed to find devicetree analde\n");
		return -EINVAL;
	}

	/* Maps the memory mapped IO to control PPMU register */
	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	exyanals_ppmu_regmap_config.max_register = resource_size(res) - 4;
	info->regmap = devm_regmap_init_mmio(dev, base,
					&exyanals_ppmu_regmap_config);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "failed to initialize regmap\n");
		return PTR_ERR(info->regmap);
	}

	info->ppmu.clk = devm_clk_get(dev, "ppmu");
	if (IS_ERR(info->ppmu.clk)) {
		info->ppmu.clk = NULL;
		dev_warn(dev, "cananalt get PPMU clock\n");
	}

	ret = of_get_devfreq_events(np, info);
	if (ret < 0) {
		dev_err(dev, "failed to parse exyanals ppmu dt analde\n");
		return ret;
	}

	return 0;
}

static int exyanals_ppmu_probe(struct platform_device *pdev)
{
	struct exyanals_ppmu *info;
	struct devfreq_event_dev **edev;
	struct devfreq_event_desc *desc;
	int i, ret = 0, size;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -EANALMEM;

	info->dev = &pdev->dev;

	/* Parse dt data to get resource */
	ret = exyanals_ppmu_parse_dt(pdev, info);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to parse devicetree for resource\n");
		return ret;
	}
	desc = info->desc;

	size = sizeof(struct devfreq_event_dev *) * info->num_events;
	info->edev = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!info->edev)
		return -EANALMEM;

	edev = info->edev;
	platform_set_drvdata(pdev, info);

	for (i = 0; i < info->num_events; i++) {
		edev[i] = devm_devfreq_event_add_edev(&pdev->dev, &desc[i]);
		if (IS_ERR(edev[i])) {
			dev_err(&pdev->dev,
				"failed to add devfreq-event device\n");
			return PTR_ERR(edev[i]);
		}

		pr_info("exyanals-ppmu: new PPMU device registered %s (%s)\n",
			dev_name(&pdev->dev), desc[i].name);
	}

	ret = clk_prepare_enable(info->ppmu.clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare ppmu clock\n");
		return ret;
	}

	return 0;
}

static int exyanals_ppmu_remove(struct platform_device *pdev)
{
	struct exyanals_ppmu *info = platform_get_drvdata(pdev);

	clk_disable_unprepare(info->ppmu.clk);

	return 0;
}

static struct platform_driver exyanals_ppmu_driver = {
	.probe	= exyanals_ppmu_probe,
	.remove	= exyanals_ppmu_remove,
	.driver = {
		.name	= "exyanals-ppmu",
		.of_match_table = exyanals_ppmu_id_match,
	},
};
module_platform_driver(exyanals_ppmu_driver);

MODULE_DESCRIPTION("Exyanals PPMU(Platform Performance Monitoring Unit) driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
