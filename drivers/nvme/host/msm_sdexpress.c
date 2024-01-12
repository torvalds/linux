// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "msm_sdexpress.h"

static void msm_sdexpress_kobj_release(struct kobject *kobj)
{
	kobject_put(kobj);
}

static struct kobj_type msm_sdexpress_ktype = {
	.release = msm_sdexpress_kobj_release,
};

static int msm_sdexpress_vreg_set_voltage(struct msm_sdexpress_reg_data *vreg,
					int min_uV, int max_uV)
{
	int ret = 0;

	if (!vreg->set_voltage_sup)
		return ret;

	ret = regulator_set_voltage(vreg->reg, min_uV, max_uV);
	if (ret)
		pr_err("%s: regulator_set_voltage(%s)failed. min_uV=%d,max_uV=%d,ret=%d\n",
			__func__, vreg->name, min_uV, max_uV, ret);

	return ret;
}

static int msm_sdexpress_vreg_set_optimum_mode(struct msm_sdexpress_reg_data *vreg,
						int uA_load)
{
	int ret = 0;

	if (!vreg->set_voltage_sup)
		return ret;

	ret = regulator_set_load(vreg->reg, uA_load);
	if (ret < 0) {
		pr_err("%s: regulator_set_load(reg=%s,uA_load=%d) failed ret=%d\n",
				__func__, vreg->name, uA_load, ret);
		return ret;
	}

	return 0;
}

static int msm_sdexpress_vreg_enable(struct msm_sdexpress_reg_data *vreg)
{
	int ret = 0;

	/* Put regulator in HPM (high power mode) */
	ret = msm_sdexpress_vreg_set_optimum_mode(vreg, vreg->hpm_uA);
	if (ret < 0)
		return ret;

	if (!vreg->is_enabled) {
		/* Set voltage level */
		ret = msm_sdexpress_vreg_set_voltage(vreg, vreg->low_vol_level,
				vreg->high_vol_level);
		if (ret)
			return ret;
	}

	ret = regulator_enable(vreg->reg);
	if (ret) {
		pr_err("%s: regulator_enable(%s) failed. ret=%d\n",
				__func__, vreg->name, ret);
		return ret;
	}

	vreg->is_enabled = true;
	return ret;
}

static int msm_sdexpress_vreg_disable(struct msm_sdexpress_reg_data *vreg)
{
	int ret = 0;

	if (!vreg->is_enabled)
		return ret;

	/* Never disable regulator marked as always_on */
	if (!vreg->is_always_on) {
		ret = regulator_disable(vreg->reg);
		if (ret) {
			pr_err("%s: regulator_disable(%s) failed. ret=%d\n",
					__func__, vreg->name, ret);
			return ret;
		}
		vreg->is_enabled = false;

		/* Set min. voltage level to 0 */
		return msm_sdexpress_vreg_set_voltage(vreg, 0, vreg->high_vol_level);
	}

	if (!vreg->lpm_sup)
		return ret;

	/* Put always_on regulator in LPM (low power mode) */
	return msm_sdexpress_vreg_set_optimum_mode(vreg, vreg->lpm_uA);
}

static int msm_sdexpress_setup_vreg(struct msm_sdexpress_info *info,
		struct msm_sdexpress_reg_data *vreg,
		bool enable)
{
	if (enable)
		return msm_sdexpress_vreg_enable(vreg);

	return msm_sdexpress_vreg_disable(vreg);
}

static void msm_sdexpress_deenumerate_card(struct msm_sdexpress_info *info)
{
	int rc = 0;
	struct msm_sdexpress_reg_data *vreg;

	mutex_lock(&info->detect_lock);

	/* Check if card is already deenumerated */
	if (!info->card_enumerated) {
		mutex_unlock(&info->detect_lock);
		return;
	}

	rc = msm_pcie_deenumerate(info->pci_nvme_instance);
	if (rc) {
		pr_err("%s: pcie deenumeration fails err:%d\n",
				__func__, rc);
		mutex_unlock(&info->detect_lock);
		return;
	}

	vreg = info->vreg_data->vdd1_data;
	rc = msm_sdexpress_vreg_disable(vreg);
	vreg = info->vreg_data->vdd2_data;
	rc = msm_sdexpress_vreg_disable(vreg);
	info->card_enumerated = false;
	mutex_unlock(&info->detect_lock);
	pr_debug("sdexpress deenumeration successful\n");
}

static void msm_sdexpress_enumerate_card(struct msm_sdexpress_info *info)
{
	int rc, retry_count = 0;
	unsigned long timeout;
	struct msm_sdexpress_reg_data *vreg;

	mutex_lock(&info->detect_lock);

	/* Check if card is already enumerated */
	if (info->card_enumerated) {
		mutex_unlock(&info->detect_lock);
		return;
	}
	/*
	 * Make sure the following are low and high before powering the VDD's.
	 * During probe, all these lines are in their respective low/high states.
	 * So, take care during hotplug, system suspend & resumes sceniarios.
	 * LOW:
	 * SDCLK (pull down to be provided on PCB),
	 * PCIE RESET
	 * REFCLK+
	 * REFCLK-
	 *
	 * HIGH:
	 * CMD (pull up to be provided on PCB)
	 * CLKREQ
	 */
retry:
	/* Enable vdd1 regulator */
	vreg = info->vreg_data->vdd1_data;
	rc = msm_sdexpress_setup_vreg(info, vreg, true);
	if (rc) {
		pr_err("%s: Unable to enable VDD1 regulator:%d\n", __func__, rc);
		goto out;
	}

	/*
	 * After VDD1 enable, if the host detects RESET low and CLKREQ high,
	 * enable VDD2 LDO.
	 *
	 * Setting RESET to low and CLKREQ to high would be taken care of by
	 * board-level hardware. So go ahead and enable VDD2
	 */

	udelay(1000);
	vreg = info->vreg_data->vdd2_data;
	rc = msm_sdexpress_setup_vreg(info, vreg, true);
	if (rc) {
		pr_err("%s: Unable to enable VDD2 regulator:%d\n", __func__, rc);
		goto disable_vdd1;
	}

	/*
	 * When the card detects VDD2 ON, the card drives CLKREQ low
	 * within a time period from VDD2 stabilization.
	 * wait for 5 sec. ?
	 */
	timeout = (5 * HZ) + jiffies;
	while ((gpiod_get_value(info->sdexpress_clkreq_gpio->gpio)) == 0) {
		usleep_range(500, 600);
		if (time_after(jiffies, timeout)) {
			pr_err("%s: CLKREQ is not going low. Wrong card may be inserted ?\n",
					__func__);
			/* notify userspace that an incompatible card is inserted */
			kobject_uevent(&info->kobj, KOBJ_CHANGE);
			rc = -ENODEV;
			goto disable_vdd2;
		}
	}

	if (!rc)
		rc = msm_pcie_enumerate(info->pci_nvme_instance);

	/*
	 * sometimes on few platforms, pcie enumerate may fail on first call.
	 * As there is no harm for a retry, go for it.
	 */
	if (rc) {
		while (retry_count++ < PCIE_ENUMERATE_RETRY) {
			vreg = info->vreg_data->vdd1_data;
			msm_sdexpress_vreg_disable(vreg);

			vreg = info->vreg_data->vdd2_data;
			msm_sdexpress_vreg_disable(vreg);
			usleep_range(5000, 6000);
			goto retry;
		}
	}

	if (!rc) {
		info->card_enumerated = true;
		mutex_unlock(&info->detect_lock);
		pr_info("%s: Card enumerated successfully\n", __func__);
		return;
	}

disable_vdd2:
	vreg = info->vreg_data->vdd2_data;
	msm_sdexpress_vreg_disable(vreg);
disable_vdd1:
	vreg = info->vreg_data->vdd1_data;
	msm_sdexpress_vreg_disable(vreg);
out:
	mutex_unlock(&info->detect_lock);
	pr_err("%s: failed to call pcie enumeration. err:%d\n", __func__, rc);
}

static void msm_sdexpress_detect_change(struct work_struct *work)
{
	struct msm_sdexpress_info *info;

	info = container_of(to_delayed_work(work), struct msm_sdexpress_info, sdex_work);
	pr_debug("%s Enter. trigger event:%d cd gpio:%d clkreq gpio:%d\n", __func__,
			atomic_read(&info->trigger_card_event),
			gpiod_get_value(info->sdexpress_gpio->gpio),
			gpiod_get_value(info->sdexpress_clkreq_gpio->gpio));

	if (atomic_read(&info->trigger_card_event) &&
			gpiod_get_value(info->sdexpress_gpio->gpio))
		msm_sdexpress_deenumerate_card(info);
	else
		msm_sdexpress_enumerate_card(info);
}

static irqreturn_t msm_sdexpress_gpio_cd_irqt(int irq, void *dev_id)
{
	struct msm_sdexpress_info *info = dev_id;

	atomic_set(&info->trigger_card_event, 1);

	queue_delayed_work(info->sdexpress_wq, &info->sdex_work,
		msecs_to_jiffies(info->sdexpress_gpio->cd_debounce_delay_ms));
	return IRQ_HANDLED;
}

static int msm_sdexpress_dt_parse_vreg_info(struct device *dev,
		struct msm_sdexpress_reg_data **vreg_data, const char *vreg_name)
{
	int len, ret = 0;
	const __be32 *prop;
	char prop_name[MAX_PROP_SIZE];
	struct msm_sdexpress_reg_data *vreg;
	struct device_node *np = dev->of_node;

	snprintf(prop_name, MAX_PROP_SIZE, "%s-supply", vreg_name);
	if (!of_parse_phandle(np, prop_name, 0)) {
		dev_info(dev, "No vreg data found for %s\n", vreg_name);
		return ret;
	}

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	vreg->name = vreg_name;

	snprintf(prop_name, MAX_PROP_SIZE,
		"qcom,%s-always-on", vreg_name);
	if (of_get_property(np, prop_name, NULL))
		vreg->is_always_on = true;

	snprintf(prop_name, MAX_PROP_SIZE,
		"qcom,%s-lpm-sup", vreg_name);
	if (of_get_property(np, prop_name, NULL))
		vreg->lpm_sup = true;

	snprintf(prop_name, MAX_PROP_SIZE,
		"qcom,%s-voltage-level", vreg_name);
	prop = of_get_property(np, prop_name, &len);
	if (!prop || (len != (2 * sizeof(__be32)))) {
		dev_warn(dev, "%s %s property. setting default values\n",
			prop ? "invalid format" : "no", prop_name);

		if (!strcmp(vreg_name, "vdd1")) {
			vreg->low_vol_level = SDEXPRESS_VREG_VDD1_DEFAULT_UV;
			vreg->high_vol_level = SDEXPRESS_VREG_VDD1_DEFAULT_UV;
		} else {
			vreg->low_vol_level = SDEXPRESS_VREG_VDD2_DEFAULT_UV;
			vreg->high_vol_level = SDEXPRESS_VREG_VDD2_DEFAULT_UV;
		}

	} else {
		vreg->low_vol_level = be32_to_cpup(&prop[0]);
		vreg->high_vol_level = be32_to_cpup(&prop[1]);
	}

	snprintf(prop_name, MAX_PROP_SIZE,
		"qcom,%s-current-level", vreg_name);
	prop = of_get_property(np, prop_name, &len);
	if (!prop || (len != (2 * sizeof(__be32)))) {
		dev_warn(dev, "%s %s property\n",
			prop ? "invalid format" : "no", prop_name);
		vreg->lpm_uA = SDEXPRESS_VREG_DEFAULT_MIN_LOAD_UA;
		vreg->hpm_uA = SDEXPRESS_VREG_DEFAULT_MAX_LODA_UA;
	} else {
		vreg->lpm_uA = be32_to_cpup(&prop[0]);
		vreg->hpm_uA = be32_to_cpup(&prop[1]);
	}

	*vreg_data = vreg;
	dev_dbg(dev, "%s: %s %s vol=[%d %d]uV, curr=[%d %d]uA\n",
		vreg->name, vreg->is_always_on ? "always_on," : "",
		vreg->lpm_sup ? "lpm_sup," : "", vreg->low_vol_level,
		vreg->high_vol_level, vreg->lpm_uA, vreg->hpm_uA);

	return ret;
}

static int msm_sdexpress_parse_clkreq_gpio(struct device *dev,
			struct msm_sdexpress_info *info)
{
	int rc = 0;
	struct gpio_desc *desc;
	struct msm_sdexpress_gpio *ctx;

	ctx = devm_kzalloc(dev, sizeof(struct msm_sdexpress_gpio),
			GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->label = devm_kasprintf(dev, GFP_KERNEL,
			"%s sdexpress-clkreq", dev_name(dev));
	if (!ctx->label) {
		dev_err(dev, "%s no memory for clkreq_label (%d)\n",
				__func__, -ENOMEM);
		rc = -ENOMEM;
		goto out;
	}

	info->sdexpress_clkreq_gpio = ctx;
	desc = devm_gpiod_get_index(dev, "clkreq", 0, GPIOD_IN);
	if (IS_ERR(desc)) {
		dev_err(dev, "%s unable to get clkreq gpio desc (%d)\n",
				__func__, PTR_ERR(desc));
		rc = PTR_ERR(desc);
		goto out;
	}

	/*
	 * clkreq pin function should be set to function-1 so that PCIe
	 * controller can drive this pin for PCIe link low power states.
	 * But devm_gpiod_get_index is inadvertently overriding pin
	 * configuration from function-1 to function-0. Due to this PCIe
	 * controller is unable to drive this pin. So PCIe l1ss is not
	 * working as expected.
	 *
	 * To configure this clkreq gpio function to function-1, apply
	 * pinctrl configuration which can set function to function-1.
	 * Since we can't directly write active config as its gets
	 * applied by driver framework, first apply sleep config and
	 * then apply active config.
	 */
	rc = pinctrl_pm_select_sleep_state(dev);
	if (rc) {
		dev_err(dev, "%s failed to set sleep state (%d)\n",
				__func__, rc);
		goto out;
	}

	msleep(200);
	rc = pinctrl_pm_select_default_state(dev);
	if (rc) {
		dev_err(dev, "%s failed to set default state (%d)\n",
				__func__, rc);
		goto out;
	}

	info->sdexpress_clkreq_gpio->gpio = desc;
out:
	return rc;
}

static int msm_sdexpress_parse_cd_gpio(struct device *dev,
		struct msm_sdexpress_info *info)
{
	int rc;
	u32 cd_debounce_delay_ms;
	struct gpio_desc *desc;
	struct msm_sdexpress_gpio *ctx;

	ctx = devm_kzalloc(dev, sizeof(struct msm_sdexpress_gpio),
					GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->label = devm_kasprintf(dev, GFP_KERNEL,
			"%s sdexpress-cd", dev_name(dev));
	if (!ctx->label) {
		dev_err(dev, "%s no memory for label (%d)\n",
				 __func__, -ENOMEM);
		rc = -ENOMEM;
		goto out;
	}

	info->sdexpress_gpio =  ctx;
	info->cd_irq = -EINVAL;
	desc = devm_gpiod_get_index(dev, "sdexpress", 0, GPIOD_IN);
	if (IS_ERR(desc)) {
		dev_err(dev, "%s unable to get gpio desc (%d)\n",
				__func__, PTR_ERR(desc));
		rc = PTR_ERR(desc);
		goto out;
	}

	if (device_property_read_u32(dev, "cd-debounce-delay-ms",
				&cd_debounce_delay_ms))
		cd_debounce_delay_ms = 200;

	rc = gpiod_set_debounce(desc, cd_debounce_delay_ms * 1000);
	if (rc < 0) {
		dev_warn(dev, "%s unable to set debounce for cd gpio desc (%d)\n",
				__func__, rc);
		rc = 0;
	}

	info->sdexpress_gpio->cd_debounce_delay_ms = cd_debounce_delay_ms;
	info->sdexpress_gpio->gpio = desc;

out:
	return rc;
}

static int msm_sdexpress_populate_pdata(struct device *dev,
				struct msm_sdexpress_info *info)
{
	int rc;
	struct device_node *of_node = dev->of_node;

	rc = of_property_read_u32(of_node, "qcom,pcie-nvme-instance",
					&info->pci_nvme_instance);
	if (rc) {
		dev_err(dev, "pcie instance is missing\n");
		goto out;
	}

	info->vreg_data = devm_kzalloc(dev, sizeof(struct msm_sdexpress_vreg_data),
							GFP_KERNEL);
	if (!info->vreg_data) {
		rc = -ENOMEM;
		goto out;
	}

	rc = msm_sdexpress_dt_parse_vreg_info(dev, &info->vreg_data->vdd1_data,
							"vdd1");
	if (rc) {
		dev_err(dev, "failed parsing vdd1 data\n");
		goto out;
	}

	rc = msm_sdexpress_dt_parse_vreg_info(dev, &info->vreg_data->vdd2_data,
							"vdd2");
	if (rc) {
		dev_err(dev, "failed parsing vdd2 data\n");
		goto out;
	}

	rc = msm_sdexpress_parse_cd_gpio(dev, info);
	if (rc) {
		dev_err(dev, "failed to parse cd gpio\n");
		goto out;
	}

	rc = msm_sdexpress_parse_clkreq_gpio(dev, info);
	if (rc)
		dev_err(dev, "failed to parse clkreq gpio\n");

out:
	return rc;
}

/*
 * Regulator utility functions
 */
static int msm_sdexpress_vreg_init_reg(struct device *dev,
				struct msm_sdexpress_reg_data *vreg)
{
	int ret = 0;

	/* check if regulator is already initialized? */
	if (vreg->reg)
		goto out;

	/* Get the regulator handle */
	vreg->reg = devm_regulator_get(dev, vreg->name);
	if (IS_ERR(vreg->reg)) {
		ret = PTR_ERR(vreg->reg);
		pr_err("%s: devm_regulator_get(%s) failed. ret=%d\n",
		__func__, vreg->name, ret);
		goto out;
	}

	if (regulator_count_voltages(vreg->reg) > 0) {
		vreg->set_voltage_sup = true;
		/* sanity check */
		if (!vreg->high_vol_level || !vreg->hpm_uA) {
			pr_err("%s: %s invalid constraints specified\n",
				__func__, vreg->name);
			ret = -EINVAL;
		}
	}

out:
	return ret;
}

static int msm_sdexpress_vreg_init(struct device *dev,
				struct msm_sdexpress_info *info)
{
	int ret;
	struct msm_sdexpress_vreg_data *slot;
	struct msm_sdexpress_reg_data *vdd1_reg, *vdd2_reg;

	slot = info->vreg_data;

	vdd1_reg = slot->vdd1_data;
	vdd2_reg = slot->vdd2_data;

	if (!vdd1_reg || !vdd2_reg)
		return -EINVAL;

	/*
	 * Get the regulator handle from voltage regulator framework
	 * and then try to set the voltage level for the regulator
	 */
	ret = msm_sdexpress_vreg_init_reg(dev, vdd1_reg);
	if (ret)
		goto out;

	ret = msm_sdexpress_vreg_init_reg(dev, vdd2_reg);
out:
	if (ret)
		dev_err(dev, "vreg init failed (%d)\n", ret);

	return ret;
}

static void msm_sdexpress_gpiod_request_cd_irq(struct msm_sdexpress_info *info)
{
	int irq, ret;
	struct device *dev = info->dev;
	struct msm_sdexpress_gpio *ctx = info->sdexpress_gpio;

	if (!ctx || !ctx->gpio)
		return;

	/* Do not use IRQ if the platform prefers to poll */
	irq = gpiod_to_irq(ctx->gpio);
	if (irq < 0) {
		dev_err(dev, "fails to allocate irq for cdgpio(%d)\n", irq);
		return;
	}

	if (!ctx->cd_gpio_isr)
		ctx->cd_gpio_isr = msm_sdexpress_gpio_cd_irqt;

	ret = devm_request_threaded_irq(dev, irq, NULL, ctx->cd_gpio_isr,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						ctx->label, info);
	if (ret < 0) {
		dev_err(dev, "intr allocation failed, continuing w/o hotplug support(%d)\n",
				ret);
		return;
	}

	info->cd_irq = irq;
	/* Enable wake capability for card detect irq */
	ret = enable_irq_wake(info->cd_irq);
	if (ret)
		dev_err(dev, "failed to enable wake capability for cd-gpio(%d)\n",
			ret);
}

/*
 * This function gets called when its device named msm-sdexpress is added to
 * device tree .dts file with all its required resources such as gpio, vdd1,
 * vdd2 etc.
 */
static int msm_sdexpress_probe(struct platform_device *pdev)
{
	struct msm_sdexpress_info *info;
	int ret;
	char sdexpress_wq_name[sizeof("sdexpress_wq")];
	struct device *dev = &pdev->dev;

	info = devm_kzalloc(&pdev->dev, sizeof(struct msm_sdexpress_info),
							 GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto out;
	}

	scnprintf(sdexpress_wq_name, ARRAY_SIZE(sdexpress_wq_name), "%s",
			"sdexpress_wq");
	info->sdexpress_wq = create_singlethread_workqueue(sdexpress_wq_name);
	if (!info->sdexpress_wq) {
		pr_err("%s: failed to create the workqueue\n",
				__func__);
		ret = -ENOMEM;
		goto out;
	}

	INIT_DELAYED_WORK(&info->sdex_work, msm_sdexpress_detect_change);
	mutex_init(&info->detect_lock);

	/* Parse platform data */
	ret = msm_sdexpress_populate_pdata(dev, info);
	if (ret) {
		dev_err(&pdev->dev, "DT parsing error\n");
		goto err;
	}

	info->dev = dev;

	/*
	 * Register an irq for a given cd gpio.
	 * If registration fails, still go ahead this would allow
	 * to detect the card presence on boot up.
	 */
	msm_sdexpress_gpiod_request_cd_irq(info);

	platform_set_drvdata(pdev, info);

	/* Setup regulators */
	ret = msm_sdexpress_vreg_init(&pdev->dev, info);
	if (ret) {
		dev_err(&pdev->dev, "Regulator setup failed (%d)\n", ret);
		goto err;
	}

	/* Queue a work-item for card presence from bootup */
	atomic_set(&info->trigger_card_event, 0);
	if (!gpiod_get_value(info->sdexpress_gpio->gpio))
		queue_delayed_work(info->sdexpress_wq, &info->sdex_work,
			msecs_to_jiffies(SDEXPRESS_PROBE_DELAYED_PERIOD));

	/* Initialize and register a kobject with the kobject core */
	kobject_init(&info->kobj, &msm_sdexpress_ktype);
	ret = kobject_add(&info->kobj, &dev->kobj, "%s", "sdexpress");
	if (ret) {
		dev_err(&pdev->dev, "Failed to add kobject (%d)\n", ret);
		goto kput;
	}

	/* announce that kobject has been created */
	ret = kobject_uevent(&info->kobj, KOBJ_ADD);
	if (ret) {
		dev_err(&pdev->dev, "Failed to sent uevent (%d)\n", ret);
		goto kput;
	}

	pr_info("%s: probe successful\n", __func__);
	return ret;

kput:
	kobject_put(&info->kobj);
err:
	if (info->sdexpress_wq)
		destroy_workqueue(info->sdexpress_wq);
out:
	return ret;
}

/*
 * Remove functionality that gets called when driver/device
 * msm_sdexpress is removed.
 */
static int msm_sdexpress_remove(struct platform_device *pdev)
{
	struct msm_sdexpress_reg_data *vreg;
	struct msm_sdexpress_info *info = dev_get_drvdata(&pdev->dev);

	if (info->sdexpress_wq)
		destroy_workqueue(info->sdexpress_wq);

	vreg = info->vreg_data->vdd1_data;
	msm_sdexpress_vreg_disable(vreg);
	vreg = info->vreg_data->vdd2_data;
	msm_sdexpress_vreg_disable(vreg);
	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}

static const struct of_device_id msm_sdexpress_match_table[] = {
	{ .compatible = "qcom,msm-sdexpress", },
	{},
};

static struct platform_driver msm_sdexpress_driver = {
	.probe          = msm_sdexpress_probe,
	.remove         = msm_sdexpress_remove,
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = msm_sdexpress_match_table,
	},
};

module_platform_driver(msm_sdexpress_driver);

MODULE_ALIAS(DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MSM SDExpress platform driver");
