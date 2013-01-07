/*
 * SuperH Pin Function Controller pinmux support.
 *
 * Copyright (C) 2012  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define DRV_NAME "pinctrl-sh_pfc"

#define pr_fmt(fmt) DRV_NAME " " KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sh_pfc.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>

struct sh_pfc_pinctrl {
	struct pinctrl_dev *pctl;
	struct sh_pfc *pfc;

	struct pinmux_gpio **functions;
	unsigned int nr_functions;

	struct pinctrl_pin_desc *pads;
	unsigned int nr_pads;

	spinlock_t lock;
};

static struct sh_pfc_pinctrl *sh_pfc_pmx;

static int sh_pfc_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->nr_pads;
}

static const char *sh_pfc_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned selector)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pads[selector].name;
}

static int sh_pfc_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
				 const unsigned **pins, unsigned *num_pins)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pmx->pads[group].number;
	*num_pins = 1;

	return 0;
}

static void sh_pfc_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				unsigned offset)
{
	seq_printf(s, "%s", DRV_NAME);
}

static struct pinctrl_ops sh_pfc_pinctrl_ops = {
	.get_groups_count	= sh_pfc_get_groups_count,
	.get_group_name		= sh_pfc_get_group_name,
	.get_group_pins		= sh_pfc_get_group_pins,
	.pin_dbg_show		= sh_pfc_pin_dbg_show,
};

static int sh_pfc_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->nr_functions;
}

static const char *sh_pfc_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned selector)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->functions[selector]->name;
}

static int sh_pfc_get_function_groups(struct pinctrl_dev *pctldev, unsigned func,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	*groups = &pmx->functions[func]->name;
	*num_groups = 1;

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

static inline int sh_pfc_config_function(struct sh_pfc *pfc, unsigned offset)
{
	if (sh_pfc_config_gpio(pfc, offset,
			       PINMUX_TYPE_FUNCTION,
			       GPIO_CFG_DRYRUN) != 0)
		return -EINVAL;

	if (sh_pfc_config_gpio(pfc, offset,
			       PINMUX_TYPE_FUNCTION,
			       GPIO_CFG_REQ) != 0)
		return -EINVAL;

	return 0;
}

static int sh_pfc_reconfig_pin(struct sh_pfc *pfc, unsigned offset,
			       int new_type)
{
	unsigned long flags;
	int pinmux_type;
	int ret = -EINVAL;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pfc->gpios[offset].flags & PINMUX_FLAG_TYPE;

	/*
	 * See if the present config needs to first be de-configured.
	 */
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

	/*
	 * Dry run
	 */
	if (sh_pfc_config_gpio(pfc, offset, new_type,
			       GPIO_CFG_DRYRUN) != 0)
		goto err;

	/*
	 * Request
	 */
	if (sh_pfc_config_gpio(pfc, offset, new_type,
			       GPIO_CFG_REQ) != 0)
		goto err;

	pfc->gpios[offset].flags &= ~PINMUX_FLAG_TYPE;
	pfc->gpios[offset].flags |= new_type;

	ret = 0;

err:
	spin_unlock_irqrestore(&pfc->lock, flags);

	return ret;
}


static int sh_pfc_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned offset)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	unsigned long flags;
	int ret, pinmux_type;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pfc->gpios[offset].flags & PINMUX_FLAG_TYPE;

	switch (pinmux_type) {
	case PINMUX_TYPE_FUNCTION:
		pr_notice_once("Use of GPIO API for function requests is "
			       "deprecated, convert to pinctrl\n");
		/* handle for now */
		ret = sh_pfc_config_function(pfc, offset);
		if (unlikely(ret < 0))
			goto err;

		break;
	case PINMUX_TYPE_GPIO:
	case PINMUX_TYPE_INPUT:
	case PINMUX_TYPE_OUTPUT:
		break;
	default:
		pr_err("Unsupported mux type (%d), bailing...\n", pinmux_type);
		ret = -ENOTSUPP;
		goto err;
	}

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

	spin_unlock_irqrestore(&pfc->lock, flags);
}

static int sh_pfc_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset, bool input)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	int type = input ? PINMUX_TYPE_INPUT : PINMUX_TYPE_OUTPUT;

	return sh_pfc_reconfig_pin(pmx->pfc, offset, type);
}

static struct pinmux_ops sh_pfc_pinmux_ops = {
	.get_functions_count	= sh_pfc_get_functions_count,
	.get_function_name	= sh_pfc_get_function_name,
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
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;

	*config = pfc->gpios[pin].flags & PINMUX_FLAG_TYPE;

	return 0;
}

static int sh_pfc_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long config)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	/* Validate the new type */
	if (config >= PINMUX_FLAG_TYPE)
		return -EINVAL;

	return sh_pfc_reconfig_pin(pmx->pfc, pin, config);
}

static void sh_pfc_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				    struct seq_file *s, unsigned pin)
{
	const char *pinmux_type_str[] = {
		[PINMUX_TYPE_NONE]		= "none",
		[PINMUX_TYPE_FUNCTION]		= "function",
		[PINMUX_TYPE_GPIO]		= "gpio",
		[PINMUX_TYPE_OUTPUT]		= "output",
		[PINMUX_TYPE_INPUT]		= "input",
		[PINMUX_TYPE_INPUT_PULLUP]	= "input bias pull up",
		[PINMUX_TYPE_INPUT_PULLDOWN]	= "input bias pull down",
	};
	unsigned long config;
	int rc;

	rc = sh_pfc_pinconf_get(pctldev, pin, &config);
	if (unlikely(rc != 0))
		return;

	seq_printf(s, " %s", pinmux_type_str[config]);
}

static struct pinconf_ops sh_pfc_pinconf_ops = {
	.pin_config_get		= sh_pfc_pinconf_get,
	.pin_config_set		= sh_pfc_pinconf_set,
	.pin_config_dbg_show	= sh_pfc_pinconf_dbg_show,
};

static struct pinctrl_gpio_range sh_pfc_gpio_range = {
	.name		= DRV_NAME,
	.id		= 0,
};

static struct pinctrl_desc sh_pfc_pinctrl_desc = {
	.name		= DRV_NAME,
	.owner		= THIS_MODULE,
	.pctlops	= &sh_pfc_pinctrl_ops,
	.pmxops		= &sh_pfc_pinmux_ops,
	.confops	= &sh_pfc_pinconf_ops,
};

static inline void sh_pfc_map_one_gpio(struct sh_pfc *pfc,
				       struct sh_pfc_pinctrl *pmx,
				       struct pinmux_gpio *gpio,
				       unsigned offset)
{
	struct pinmux_data_reg *dummy;
	unsigned long flags;
	int bit;

	gpio->flags &= ~PINMUX_FLAG_TYPE;

	if (sh_pfc_get_data_reg(pfc, offset, &dummy, &bit) == 0)
		gpio->flags |= PINMUX_TYPE_GPIO;
	else {
		gpio->flags |= PINMUX_TYPE_FUNCTION;

		spin_lock_irqsave(&pmx->lock, flags);
		pmx->nr_functions++;
		spin_unlock_irqrestore(&pmx->lock, flags);
	}
}

/* pinmux ranges -> pinctrl pin descs */
static int sh_pfc_map_gpios(struct sh_pfc *pfc, struct sh_pfc_pinctrl *pmx)
{
	unsigned long flags;
	int i;

	pmx->nr_pads = pfc->last_gpio - pfc->first_gpio + 1;

	pmx->pads = kmalloc(sizeof(struct pinctrl_pin_desc) * pmx->nr_pads,
			    GFP_KERNEL);
	if (unlikely(!pmx->pads)) {
		pmx->nr_pads = 0;
		return -ENOMEM;
	}

	spin_lock_irqsave(&pfc->lock, flags);

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

		/* XXX */
		if (unlikely(!gpio->enum_id))
			continue;

		sh_pfc_map_one_gpio(pfc, pmx, gpio, i);
	}

	spin_unlock_irqrestore(&pfc->lock, flags);

	sh_pfc_pinctrl_desc.pins = pmx->pads;
	sh_pfc_pinctrl_desc.npins = pmx->nr_pads;

	return 0;
}

static int sh_pfc_map_functions(struct sh_pfc *pfc, struct sh_pfc_pinctrl *pmx)
{
	unsigned long flags;
	int i, fn;

	pmx->functions = kzalloc(pmx->nr_functions * sizeof(void *),
				 GFP_KERNEL);
	if (unlikely(!pmx->functions))
		return -ENOMEM;

	spin_lock_irqsave(&pmx->lock, flags);

	for (i = fn = 0; i < pmx->nr_pads; i++) {
		struct pinmux_gpio *gpio = pfc->gpios + i;

		if ((gpio->flags & PINMUX_FLAG_TYPE) == PINMUX_TYPE_FUNCTION)
			pmx->functions[fn++] = gpio;
	}

	spin_unlock_irqrestore(&pmx->lock, flags);

	return 0;
}

static int sh_pfc_pinctrl_probe(struct platform_device *pdev)
{
	struct sh_pfc *pfc;
	int ret;

	if (unlikely(!sh_pfc_pmx))
		return -ENODEV;

	pfc = sh_pfc_pmx->pfc;

	ret = sh_pfc_map_gpios(pfc, sh_pfc_pmx);
	if (unlikely(ret != 0))
		return ret;

	ret = sh_pfc_map_functions(pfc, sh_pfc_pmx);
	if (unlikely(ret != 0))
		goto free_pads;

	sh_pfc_pmx->pctl = pinctrl_register(&sh_pfc_pinctrl_desc, &pdev->dev,
					    sh_pfc_pmx);
	if (IS_ERR(sh_pfc_pmx->pctl)) {
		ret = PTR_ERR(sh_pfc_pmx->pctl);
		goto free_functions;
	}

	sh_pfc_gpio_range.npins = pfc->last_gpio - pfc->first_gpio + 1;
	sh_pfc_gpio_range.base = pfc->first_gpio;
	sh_pfc_gpio_range.pin_base = pfc->first_gpio;

	pinctrl_add_gpio_range(sh_pfc_pmx->pctl, &sh_pfc_gpio_range);

	platform_set_drvdata(pdev, sh_pfc_pmx);

	return 0;

free_functions:
	kfree(sh_pfc_pmx->functions);
free_pads:
	kfree(sh_pfc_pmx->pads);
	kfree(sh_pfc_pmx);

	return ret;
}

static int sh_pfc_pinctrl_remove(struct platform_device *pdev)
{
	struct sh_pfc_pinctrl *pmx = platform_get_drvdata(pdev);

	pinctrl_unregister(pmx->pctl);

	platform_set_drvdata(pdev, NULL);

	kfree(sh_pfc_pmx->functions);
	kfree(sh_pfc_pmx->pads);
	kfree(sh_pfc_pmx);

	return 0;
}

static struct platform_driver sh_pfc_pinctrl_driver = {
	.probe		= sh_pfc_pinctrl_probe,
	.remove		= sh_pfc_pinctrl_remove,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static struct platform_device sh_pfc_pinctrl_device = {
	.name		= DRV_NAME,
	.id		= -1,
};

static int sh_pfc_pinctrl_init(void)
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

int sh_pfc_register_pinctrl(struct sh_pfc *pfc)
{
	sh_pfc_pmx = kzalloc(sizeof(struct sh_pfc_pinctrl), GFP_KERNEL);
	if (unlikely(!sh_pfc_pmx))
		return -ENOMEM;

	spin_lock_init(&sh_pfc_pmx->lock);

	sh_pfc_pmx->pfc = pfc;

	return sh_pfc_pinctrl_init();
}
EXPORT_SYMBOL_GPL(sh_pfc_register_pinctrl);

static void __exit sh_pfc_pinctrl_exit(void)
{
	platform_driver_unregister(&sh_pfc_pinctrl_driver);
}
module_exit(sh_pfc_pinctrl_exit);
