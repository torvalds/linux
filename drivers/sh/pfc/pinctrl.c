/*
 * SuperH Pin Function Controller pinmux support.
 *
 * Copyright (C) 2012  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sh_pfc.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>

struct sh_pfc_pinctrl {
	struct pinctrl_dev *pctl;
	struct sh_pfc *pfc;

	struct pinctrl_pin_desc *pads;
	unsigned int nr_pads;
};

static struct sh_pfc_pinctrl *sh_pfc_pmx;

/*
 * No group support yet
 */
static int sh_pfc_get_noop_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *sh_pfc_get_noop_name(struct pinctrl_dev *pctldev,
					 unsigned selector)
{
	return NULL;
}

static int sh_pfc_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
				 const unsigned **pins, unsigned *num_pins)
{
	return -ENOTSUPP;
}

static struct pinctrl_ops sh_pfc_pinctrl_ops = {
	.get_groups_count	= sh_pfc_get_noop_count,
	.get_group_name		= sh_pfc_get_noop_name,
	.get_group_pins		= sh_pfc_get_group_pins,
};


/*
 * No function support yet
 */
static int sh_pfc_get_function_groups(struct pinctrl_dev *pctldev, unsigned func,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	return 0;
}

static int sh_pfc_noop_enable(struct pinctrl_dev *pctldev, unsigned func,
			      unsigned group)
{
	return 0;
}

static void sh_pfc_noop_disable(struct pinctrl_dev *pctldev, unsigned func,
				unsigned group)
{
}

static int sh_pfc_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned offset)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	struct pinmux_data_reg *dummy;
	unsigned long flags;
	int i, ret, pinmux_type;

	ret = -EINVAL;

	spin_lock_irqsave(&pfc->lock, flags);

	if ((pfc->gpios[offset].flags & PINMUX_FLAG_TYPE) != PINMUX_TYPE_NONE)
		goto err;

	/* setup pin function here if no data is associated with pin */
	if (sh_pfc_get_data_reg(pfc, offset, &dummy, &i) != 0) {
		pinmux_type = PINMUX_TYPE_FUNCTION;

		if (sh_pfc_config_gpio(pfc, offset,
				       pinmux_type,
				       GPIO_CFG_DRYRUN) != 0)
			goto err;

		if (sh_pfc_config_gpio(pfc, offset,
				       pinmux_type,
				       GPIO_CFG_REQ) != 0)
			goto err;
	} else
		pinmux_type = PINMUX_TYPE_GPIO;

	pfc->gpios[offset].flags &= ~PINMUX_FLAG_TYPE;
	pfc->gpios[offset].flags |= pinmux_type;

	ret = 0;

err:
	spin_unlock_irqrestore(&pfc->lock, flags);

	return ret;
}

static void sh_pfc_gpio_disable_free(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	unsigned long flags;
	int pinmux_type;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pfc->gpios[offset].flags & PINMUX_FLAG_TYPE;

	sh_pfc_config_gpio(pfc, offset, pinmux_type, GPIO_CFG_FREE);

	pfc->gpios[offset].flags &= ~PINMUX_FLAG_TYPE;
	pfc->gpios[offset].flags |= PINMUX_TYPE_NONE;

	spin_unlock_irqrestore(&pfc->lock, flags);
}

static int sh_pfc_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset, bool input)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	unsigned long flags;
	int pinmux_type, new_pinmux_type;
	int ret = -EINVAL;

	new_pinmux_type = input ? PINMUX_TYPE_INPUT : PINMUX_TYPE_OUTPUT;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pfc->gpios[offset].flags & PINMUX_FLAG_TYPE;

	switch (pinmux_type) {
	case PINMUX_TYPE_GPIO:
		break;
	case PINMUX_TYPE_OUTPUT:
	case PINMUX_TYPE_INPUT:
	case PINMUX_TYPE_INPUT_PULLUP:
	case PINMUX_TYPE_INPUT_PULLDOWN:
		sh_pfc_config_gpio(pfc, offset, pinmux_type, GPIO_CFG_FREE);
		break;
	default:
		goto err;
	}

	if (sh_pfc_config_gpio(pfc, offset,
			       new_pinmux_type,
			       GPIO_CFG_DRYRUN) != 0)
		goto err;

	if (sh_pfc_config_gpio(pfc, offset,
			       new_pinmux_type,
			       GPIO_CFG_REQ) != 0)
		BUG();

	pfc->gpios[offset].flags &= ~PINMUX_FLAG_TYPE;
	pfc->gpios[offset].flags |= new_pinmux_type;

	ret = 0;

err:
	spin_unlock_irqrestore(&pfc->lock, flags);

	return ret;
}

static struct pinmux_ops sh_pfc_pinmux_ops = {
	.get_functions_count	= sh_pfc_get_noop_count,
	.get_function_name	= sh_pfc_get_noop_name,
	.get_function_groups	= sh_pfc_get_function_groups,
	.enable			= sh_pfc_noop_enable,
	.disable		= sh_pfc_noop_disable,
	.gpio_request_enable	= sh_pfc_gpio_request_enable,
	.gpio_disable_free	= sh_pfc_gpio_disable_free,
	.gpio_set_direction	= sh_pfc_gpio_set_direction,
};

static int sh_pfc_pinconf_get(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long *config)
{
	return -ENOTSUPP;
}

static int sh_pfc_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long config)
{
	return -EINVAL;
}

static struct pinconf_ops sh_pfc_pinconf_ops = {
	.is_generic		= true,
	.pin_config_get		= sh_pfc_pinconf_get,
	.pin_config_set		= sh_pfc_pinconf_set,
};

static struct pinctrl_gpio_range sh_pfc_gpio_range = {
	.name		= KBUILD_MODNAME,
	.id		= 0,
};

static struct pinctrl_desc sh_pfc_pinctrl_desc = {
	.name		= KBUILD_MODNAME,
	.owner		= THIS_MODULE,
	.pctlops	= &sh_pfc_pinctrl_ops,
	.pmxops		= &sh_pfc_pinmux_ops,
	.confops	= &sh_pfc_pinconf_ops,
};

int sh_pfc_register_pinctrl(struct sh_pfc *pfc)
{
	sh_pfc_pmx = kmalloc(sizeof(struct sh_pfc_pinctrl), GFP_KERNEL);
	if (unlikely(!sh_pfc_pmx))
		return -ENOMEM;

	sh_pfc_pmx->pfc = pfc;

	return 0;
}

/* pinmux ranges -> pinctrl pin descs */
static int __devinit sh_pfc_map_gpios(struct sh_pfc *pfc,
				      struct sh_pfc_pinctrl *pmx)
{
	int i;

	pmx->nr_pads = pfc->last_gpio - pfc->first_gpio + 1;

	pmx->pads = kmalloc(sizeof(struct pinctrl_pin_desc) * pmx->nr_pads,
			    GFP_KERNEL);
	if (unlikely(!pmx->pads)) {
		pmx->nr_pads = 0;
		return -ENOMEM;
	}

	/*
	 * We don't necessarily have a 1:1 mapping between pin and linux
	 * GPIO number, as the latter maps to the associated enum_id.
	 * Care needs to be taken to translate back to pin space when
	 * dealing with any pin configurations.
	 */
	for (i = 0; i < pmx->nr_pads; i++) {
		struct pinctrl_pin_desc *pin = pmx->pads + i;
		struct pinmux_gpio *gpio = pfc->gpios + i;

		pin->number = pfc->first_gpio + i;
		pin->name = gpio->name;
	}

	sh_pfc_pinctrl_desc.pins = pmx->pads;
	sh_pfc_pinctrl_desc.npins = pmx->nr_pads;

	return 0;
}

static int __devinit sh_pfc_pinctrl_probe(struct platform_device *pdev)
{
	struct sh_pfc *pfc;
	int ret;

	if (unlikely(!sh_pfc_pmx))
		return -ENODEV;

	pfc = sh_pfc_pmx->pfc;

	ret = sh_pfc_map_gpios(pfc, sh_pfc_pmx);
	if (unlikely(ret != 0))
		return ret;

	sh_pfc_pmx->pctl = pinctrl_register(&sh_pfc_pinctrl_desc, &pdev->dev,
					    sh_pfc_pmx);
	if (IS_ERR(sh_pfc_pmx->pctl)) {
		ret = PTR_ERR(sh_pfc_pmx->pctl);
		goto out;
	}

	sh_pfc_gpio_range.npins = pfc->last_gpio - pfc->first_gpio + 1;
	sh_pfc_gpio_range.base = pfc->first_gpio;
	sh_pfc_gpio_range.pin_base = pfc->first_gpio;

	pinctrl_add_gpio_range(sh_pfc_pmx->pctl, &sh_pfc_gpio_range);

	platform_set_drvdata(pdev, sh_pfc_pmx);

	return 0;

out:
	kfree(sh_pfc_pmx->pads);
	kfree(sh_pfc_pmx);
	return ret;
}

static int __devexit sh_pfc_pinctrl_remove(struct platform_device *pdev)
{
	struct sh_pfc_pinctrl *pmx = platform_get_drvdata(pdev);

	pinctrl_remove_gpio_range(pmx->pctl, &sh_pfc_gpio_range);
	pinctrl_unregister(pmx->pctl);

	platform_set_drvdata(pdev, NULL);

	kfree(sh_pfc_pmx->pads);
	kfree(sh_pfc_pmx);

	return 0;
}

static struct platform_driver sh_pfc_pinctrl_driver = {
	.probe		= sh_pfc_pinctrl_probe,
	.remove		= __devexit_p(sh_pfc_pinctrl_remove),
	.driver		= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
};

static struct platform_device sh_pfc_pinctrl_device = {
	.name		= KBUILD_MODNAME,
	.id		= -1,
};

static int __init sh_pfc_pinctrl_init(void)
{
	int rc;

	rc = platform_driver_register(&sh_pfc_pinctrl_driver);
	if (likely(!rc)) {
		rc = platform_device_register(&sh_pfc_pinctrl_device);
		if (unlikely(rc))
			platform_driver_unregister(&sh_pfc_pinctrl_driver);
	}

	return rc;
}

static void __exit sh_pfc_pinctrl_exit(void)
{
	platform_driver_unregister(&sh_pfc_pinctrl_driver);
}

subsys_initcall(sh_pfc_pinctrl_init);
module_exit(sh_pfc_pinctrl_exit);
