/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pwm.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/module.h>

/*
 * PWM Out generators
 * Bank: 0x10
 */
#define AB8500_PWM_OUT_CTRL1_REG	0x60
#define AB8500_PWM_OUT_CTRL2_REG	0x61
#define AB8500_PWM_OUT_CTRL7_REG	0x66

/* backlight driver constants */
#define ENABLE_PWM			1
#define DISABLE_PWM			0

struct pwm_device {
	struct device *dev;
	struct list_head node;
	const char *label;
	unsigned int pwm_id;
};

static LIST_HEAD(pwm_list);

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	int ret = 0;
	unsigned int higher_val, lower_val;
	u8 reg;

	/*
	 * get the first 8 bits that are be written to
	 * AB8500_PWM_OUT_CTRL1_REG[0:7]
	 */
	lower_val = duty_ns & 0x00FF;
	/*
	 * get bits [9:10] that are to be written to
	 * AB8500_PWM_OUT_CTRL2_REG[0:1]
	 */
	higher_val = ((duty_ns & 0x0300) >> 8);

	reg = AB8500_PWM_OUT_CTRL1_REG + ((pwm->pwm_id - 1) * 2);

	ret = abx500_set_register_interruptible(pwm->dev, AB8500_MISC,
			reg, (u8)lower_val);
	if (ret < 0)
		return ret;
	ret = abx500_set_register_interruptible(pwm->dev, AB8500_MISC,
			(reg + 1), (u8)higher_val);

	return ret;
}
EXPORT_SYMBOL(pwm_config);

int pwm_enable(struct pwm_device *pwm)
{
	int ret;

	ret = abx500_mask_and_set_register_interruptible(pwm->dev,
				AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
				1 << (pwm->pwm_id-1), ENABLE_PWM);
	if (ret < 0)
		dev_err(pwm->dev, "%s: Failed to disable PWM, Error %d\n",
							pwm->label, ret);
	return ret;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
	int ret;

	ret = abx500_mask_and_set_register_interruptible(pwm->dev,
				AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
				1 << (pwm->pwm_id-1), DISABLE_PWM);
	if (ret < 0)
		dev_err(pwm->dev, "%s: Failed to disable PWM, Error %d\n",
							pwm->label, ret);
	return;
}
EXPORT_SYMBOL(pwm_disable);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;

	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->pwm_id == pwm_id) {
			pwm->label = label;
			pwm->pwm_id = pwm_id;
			return pwm;
		}
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(pwm_request);

void pwm_free(struct pwm_device *pwm)
{
	pwm_disable(pwm);
}
EXPORT_SYMBOL(pwm_free);

static int __devinit ab8500_pwm_probe(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	/*
	 * Nothing to be done in probe, this is required to get the
	 * device which is required for ab8500 read and write
	 */
	pwm = kzalloc(sizeof(struct pwm_device), GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	pwm->dev = &pdev->dev;
	pwm->pwm_id = pdev->id;
	list_add_tail(&pwm->node, &pwm_list);
	platform_set_drvdata(pdev, pwm);
	dev_dbg(pwm->dev, "pwm probe successful\n");
	return 0;
}

static int __devexit ab8500_pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm = platform_get_drvdata(pdev);
	list_del(&pwm->node);
	dev_dbg(&pdev->dev, "pwm driver removed\n");
	kfree(pwm);
	return 0;
}

static struct platform_driver ab8500_pwm_driver = {
	.driver = {
		.name = "ab8500-pwm",
		.owner = THIS_MODULE,
	},
	.probe = ab8500_pwm_probe,
	.remove = __devexit_p(ab8500_pwm_remove),
};

static int __init ab8500_pwm_init(void)
{
	return platform_driver_register(&ab8500_pwm_driver);
}

static void __exit ab8500_pwm_exit(void)
{
	platform_driver_unregister(&ab8500_pwm_driver);
}

subsys_initcall(ab8500_pwm_init);
module_exit(ab8500_pwm_exit);
MODULE_AUTHOR("Arun MURTHY <arun.murthy@stericsson.com>");
MODULE_DESCRIPTION("AB8500 Pulse Width Modulation Driver");
MODULE_ALIAS("platform:ab8500-pwm");
MODULE_LICENSE("GPL v2");
