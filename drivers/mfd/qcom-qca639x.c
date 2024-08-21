#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/devinfo.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define MAX_NUM_REGULATORS	8

static struct vreg {
	const char *name;
	unsigned int load_uA;
} vregs [MAX_NUM_REGULATORS] = {
	/* 2.0 V */
	{ "vddpcie2", 15000 },
	{ "vddrfa3", 400000 },

	/* 0.95 V */
	{ "vddaon", 100000 },
	{ "vddpmu", 1250000 },
	{ "vddrfa1", 200000 },

	/* 1.35 V */
	{ "vddrfa2", 400000 },
	{ "vddpcie1", 35000 },

	/* 1.8 V */
	{ "vddio", 20000 },
};

struct qca639x_data {
	struct regulator_bulk_data regulators[MAX_NUM_REGULATORS];
	size_t num_vregs;
	struct device *dev;
	struct pinctrl_state *active_state;
	struct generic_pm_domain pd;
};

#define domain_to_data(domain) container_of(domain, struct qca639x_data, pd)

static int qca639x_power_on(struct generic_pm_domain *domain)
{
	struct qca639x_data *data = domain_to_data(domain);
	int ret;

	dev_warn(&domain->dev, "DUMMY POWER ON\n");

	ret = regulator_bulk_enable(data->num_vregs, data->regulators);
	if (ret) {
		dev_err(data->dev, "Failed to enable regulators");
		return ret;
	}

	/* Wait for 1ms before toggling enable pins. */
	msleep(1);

	ret = pinctrl_select_state(data->dev->pins->p, data->active_state);
	if (ret) {
		dev_err(data->dev, "Failed to select active state");
		return ret;
	}

	/* Wait for all power levels to stabilize */
	msleep(6);

	return 0;
}

static int qca639x_power_off(struct generic_pm_domain *domain)
{
	struct qca639x_data *data = domain_to_data(domain);

	dev_warn(&domain->dev, "DUMMY POWER OFF\n");

	pinctrl_select_default_state(data->dev);
	regulator_bulk_disable(data->num_vregs, data->regulators);

	return 0;
}

static int qca639x_probe(struct platform_device *pdev)
{
	struct qca639x_data *data;
	struct device *dev = &pdev->dev;
	int i, ret;

	if (!dev->pins || IS_ERR_OR_NULL(dev->pins->default_state))
		return -EINVAL;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->num_vregs = ARRAY_SIZE(vregs);

	data->active_state = pinctrl_lookup_state(dev->pins->p, "active");
	if (IS_ERR(data->active_state)) {
		ret = PTR_ERR(data->active_state);
		dev_err(dev, "Failed to get active_state: %d\n", ret);
		return ret;
	}

	for (i = 0; i < data->num_vregs; i++)
		data->regulators[i].supply = vregs[i].name;
	ret = devm_regulator_bulk_get(dev, data->num_vregs, data->regulators);
	if (ret < 0)
		return ret;

	for (i = 0; i < data->num_vregs; i++) {
		ret = regulator_set_load(data->regulators[i].consumer, vregs[i].load_uA);
		if (ret)
			return ret;
	}

	data->pd.name = dev_name(dev);
	data->pd.power_on = qca639x_power_on;
	data->pd.power_off = qca639x_power_off;

	ret = pm_genpd_init(&data->pd, NULL, true);
	if (ret < 0)
		return ret;

	ret = of_genpd_add_provider_simple(dev->of_node, &data->pd);
	if (ret < 0) {
		pm_genpd_remove(&data->pd);
		return ret;
	}

	platform_set_drvdata(pdev, data);

	return 0;
}

static int qca639x_remove(struct platform_device *pdev)
{
	struct qca639x_data *data = platform_get_drvdata(pdev);

	pm_genpd_remove(&data->pd);

	return 0;
}

static const struct of_device_id qca639x_of_match[] = {
	{ .compatible = "qcom,qca639x" },
};

static struct platform_driver qca639x_driver = {
	.probe = qca639x_probe,
	.remove = qca639x_remove,
	.driver = {
		.name = "qca639x",
		.of_match_table = qca639x_of_match,
	},
};

module_platform_driver(qca639x_driver);
MODULE_LICENSE("GPL v2");
