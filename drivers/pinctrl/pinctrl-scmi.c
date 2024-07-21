// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Power Interface (SCMI) Protocol based pinctrl driver
 *
 * Copyright (C) 2024 EPAM
 * Copyright 2024 NXP
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "pinctrl-utils.h"
#include "core.h"
#include "pinconf.h"

#define DRV_NAME "scmi-pinctrl"

/* Define num configs, if not large than 4 use stack, else use kcalloc() */
#define SCMI_NUM_CONFIGS	4

static const struct scmi_pinctrl_proto_ops *pinctrl_ops;

struct scmi_pinctrl {
	struct device *dev;
	struct scmi_protocol_handle *ph;
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc pctl_desc;
	struct pinfunction *functions;
	unsigned int nr_functions;
	struct pinctrl_pin_desc *pins;
	unsigned int nr_pins;
};

static int pinctrl_scmi_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl_ops->count_get(pmx->ph, GROUP_TYPE);
}

static const char *pinctrl_scmi_get_group_name(struct pinctrl_dev *pctldev,
					       unsigned int selector)
{
	int ret;
	const char *name;
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	ret = pinctrl_ops->name_get(pmx->ph, selector, GROUP_TYPE, &name);
	if (ret) {
		dev_err(pmx->dev, "get name failed with err %d", ret);
		return NULL;
	}

	return name;
}

static int pinctrl_scmi_get_group_pins(struct pinctrl_dev *pctldev,
				       unsigned int selector,
				       const unsigned int **pins,
				       unsigned int *num_pins)
{
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl_ops->group_pins_get(pmx->ph, selector, pins, num_pins);
}

static const struct pinctrl_ops pinctrl_scmi_pinctrl_ops = {
	.get_groups_count = pinctrl_scmi_get_groups_count,
	.get_group_name = pinctrl_scmi_get_group_name,
	.get_group_pins = pinctrl_scmi_get_group_pins,
#ifdef CONFIG_OF
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
#endif
};

static int pinctrl_scmi_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl_ops->count_get(pmx->ph, FUNCTION_TYPE);
}

static const char *pinctrl_scmi_get_function_name(struct pinctrl_dev *pctldev,
						  unsigned int selector)
{
	int ret;
	const char *name;
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	ret = pinctrl_ops->name_get(pmx->ph, selector, FUNCTION_TYPE, &name);
	if (ret) {
		dev_err(pmx->dev, "get name failed with err %d", ret);
		return NULL;
	}

	return name;
}

static int pinctrl_scmi_get_function_groups(struct pinctrl_dev *pctldev,
					    unsigned int selector,
					    const char * const **p_groups,
					    unsigned int * const p_num_groups)
{
	struct pinfunction *func;
	const unsigned int *group_ids;
	unsigned int num_groups;
	const char **groups;
	int ret, i;
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	if (!p_groups || !p_num_groups)
		return -EINVAL;

	if (selector >= pmx->nr_functions)
		return -EINVAL;

	func = &pmx->functions[selector];
	if (func->ngroups)
		goto done;

	ret = pinctrl_ops->function_groups_get(pmx->ph, selector, &num_groups,
					       &group_ids);
	if (ret) {
		dev_err(pmx->dev, "Unable to get function groups, err %d", ret);
		return ret;
	}
	if (!num_groups)
		return -EINVAL;

	groups = kcalloc(num_groups, sizeof(*groups), GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	for (i = 0; i < num_groups; i++) {
		groups[i] = pinctrl_scmi_get_group_name(pctldev, group_ids[i]);
		if (!groups[i]) {
			ret = -EINVAL;
			goto err_free;
		}
	}

	func->ngroups = num_groups;
	func->groups = groups;
done:
	*p_groups = func->groups;
	*p_num_groups = func->ngroups;

	return 0;

err_free:
	kfree(groups);

	return ret;
}

static int pinctrl_scmi_func_set_mux(struct pinctrl_dev *pctldev,
				     unsigned int selector, unsigned int group)
{
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl_ops->mux_set(pmx->ph, selector, group);
}

static int pinctrl_scmi_request(struct pinctrl_dev *pctldev,
				unsigned int offset)
{
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl_ops->pin_request(pmx->ph, offset);
}

static int pinctrl_scmi_free(struct pinctrl_dev *pctldev, unsigned int offset)
{
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl_ops->pin_free(pmx->ph, offset);
}

static const struct pinmux_ops pinctrl_scmi_pinmux_ops = {
	.request = pinctrl_scmi_request,
	.free = pinctrl_scmi_free,
	.get_functions_count = pinctrl_scmi_get_functions_count,
	.get_function_name = pinctrl_scmi_get_function_name,
	.get_function_groups = pinctrl_scmi_get_function_groups,
	.set_mux = pinctrl_scmi_func_set_mux,
};

static int pinctrl_scmi_map_pinconf_type(enum pin_config_param param,
					 enum scmi_pinctrl_conf_type *type)
{
	u32 arg = param;

	switch (arg) {
	case PIN_CONFIG_BIAS_BUS_HOLD:
		*type = SCMI_PIN_BIAS_BUS_HOLD;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		*type = SCMI_PIN_BIAS_DISABLE;
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		*type = SCMI_PIN_BIAS_HIGH_IMPEDANCE;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		*type = SCMI_PIN_BIAS_PULL_DOWN;
		break;
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		*type = SCMI_PIN_BIAS_PULL_DEFAULT;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		*type = SCMI_PIN_BIAS_PULL_UP;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		*type = SCMI_PIN_DRIVE_OPEN_DRAIN;
		break;
	case PIN_CONFIG_DRIVE_OPEN_SOURCE:
		*type = SCMI_PIN_DRIVE_OPEN_SOURCE;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		*type = SCMI_PIN_DRIVE_PUSH_PULL;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		*type = SCMI_PIN_DRIVE_STRENGTH;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		*type = SCMI_PIN_DRIVE_STRENGTH;
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		*type = SCMI_PIN_INPUT_DEBOUNCE;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		*type = SCMI_PIN_INPUT_MODE;
		break;
	case PIN_CONFIG_INPUT_SCHMITT:
		*type = SCMI_PIN_INPUT_SCHMITT;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		*type = SCMI_PIN_INPUT_MODE;
		break;
	case PIN_CONFIG_MODE_LOW_POWER:
		*type = SCMI_PIN_LOW_POWER_MODE;
		break;
	case PIN_CONFIG_OUTPUT:
		*type = SCMI_PIN_OUTPUT_VALUE;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		*type = SCMI_PIN_OUTPUT_MODE;
		break;
	case PIN_CONFIG_OUTPUT_IMPEDANCE_OHMS:
		*type = SCMI_PIN_OUTPUT_VALUE;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		*type = SCMI_PIN_POWER_SOURCE;
		break;
	case PIN_CONFIG_SLEW_RATE:
		*type = SCMI_PIN_SLEW_RATE;
		break;
	case SCMI_PIN_OEM_START ... SCMI_PIN_OEM_END:
		*type = arg;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int pinctrl_scmi_pinconf_get(struct pinctrl_dev *pctldev,
				    unsigned int pin, unsigned long *config)
{
	int ret;
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param config_type;
	enum scmi_pinctrl_conf_type type;
	u32 config_value;

	if (!config)
		return -EINVAL;

	config_type = pinconf_to_config_param(*config);

	ret = pinctrl_scmi_map_pinconf_type(config_type, &type);
	if (ret)
		return ret;

	ret = pinctrl_ops->settings_get_one(pmx->ph, pin, PIN_TYPE, type,
					    &config_value);
	/* Convert SCMI error code to PINCTRL expected error code */
	if (ret == -EOPNOTSUPP)
		return -ENOTSUPP;
	if (ret)
		return ret;

	*config = pinconf_to_config_packed(config_type, config_value);

	return 0;
}

static int
pinctrl_scmi_alloc_configs(struct pinctrl_dev *pctldev, u32 num_configs,
			   u32 **p_config_value,
			   enum scmi_pinctrl_conf_type **p_config_type)
{
	if (num_configs <= SCMI_NUM_CONFIGS)
		return 0;

	*p_config_value = kcalloc(num_configs, sizeof(**p_config_value), GFP_KERNEL);
	if (!*p_config_value)
		return -ENOMEM;

	*p_config_type = kcalloc(num_configs, sizeof(**p_config_type), GFP_KERNEL);
	if (!*p_config_type) {
		kfree(*p_config_value);
		return -ENOMEM;
	}

	return 0;
}

static void
pinctrl_scmi_free_configs(struct pinctrl_dev *pctldev, u32 num_configs,
			  u32 **p_config_value,
			  enum scmi_pinctrl_conf_type **p_config_type)
{
	if (num_configs <= SCMI_NUM_CONFIGS)
		return;

	kfree(*p_config_value);
	kfree(*p_config_type);
}

static int pinctrl_scmi_pinconf_set(struct pinctrl_dev *pctldev,
				    unsigned int pin,
				    unsigned long *configs,
				    unsigned int num_configs)
{
	int i, ret;
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum scmi_pinctrl_conf_type config_type[SCMI_NUM_CONFIGS];
	u32 config_value[SCMI_NUM_CONFIGS];
	enum scmi_pinctrl_conf_type *p_config_type = config_type;
	u32 *p_config_value = config_value;
	enum pin_config_param param;

	if (!configs || !num_configs)
		return -EINVAL;

	ret = pinctrl_scmi_alloc_configs(pctldev, num_configs, &p_config_type,
					 &p_config_value);
	if (ret)
		return ret;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		ret = pinctrl_scmi_map_pinconf_type(param, &p_config_type[i]);
		if (ret) {
			dev_err(pmx->dev, "Error map pinconf_type %d\n", ret);
			goto free_config;
		}
		p_config_value[i] = pinconf_to_config_argument(configs[i]);
	}

	ret = pinctrl_ops->settings_conf(pmx->ph, pin, PIN_TYPE, num_configs,
					 p_config_type,  p_config_value);
	if (ret)
		dev_err(pmx->dev, "Error parsing config %d\n", ret);

free_config:
	pinctrl_scmi_free_configs(pctldev, num_configs, &p_config_type,
				  &p_config_value);
	return ret;
}

static int pinctrl_scmi_pinconf_group_set(struct pinctrl_dev *pctldev,
					  unsigned int group,
					  unsigned long *configs,
					  unsigned int num_configs)
{
	int i, ret;
	struct scmi_pinctrl *pmx =  pinctrl_dev_get_drvdata(pctldev);
	enum scmi_pinctrl_conf_type config_type[SCMI_NUM_CONFIGS];
	u32 config_value[SCMI_NUM_CONFIGS];
	enum scmi_pinctrl_conf_type *p_config_type = config_type;
	u32 *p_config_value = config_value;
	enum pin_config_param param;

	if (!configs || !num_configs)
		return -EINVAL;

	ret = pinctrl_scmi_alloc_configs(pctldev, num_configs, &p_config_type,
					 &p_config_value);
	if (ret)
		return ret;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		ret = pinctrl_scmi_map_pinconf_type(param, &p_config_type[i]);
		if (ret) {
			dev_err(pmx->dev, "Error map pinconf_type %d\n", ret);
			goto free_config;
		}

		p_config_value[i] = pinconf_to_config_argument(configs[i]);
	}

	ret = pinctrl_ops->settings_conf(pmx->ph, group, GROUP_TYPE,
					 num_configs, p_config_type,
					 p_config_value);
	if (ret)
		dev_err(pmx->dev, "Error parsing config %d", ret);

free_config:
	pinctrl_scmi_free_configs(pctldev, num_configs, &p_config_type,
				  &p_config_value);
	return ret;
};

static int pinctrl_scmi_pinconf_group_get(struct pinctrl_dev *pctldev,
					  unsigned int group,
					  unsigned long *config)
{
	int ret;
	struct scmi_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param config_type;
	enum scmi_pinctrl_conf_type type;
	u32 config_value;

	if (!config)
		return -EINVAL;

	config_type = pinconf_to_config_param(*config);
	ret = pinctrl_scmi_map_pinconf_type(config_type, &type);
	if (ret) {
		dev_err(pmx->dev, "Error map pinconf_type %d\n", ret);
		return ret;
	}

	ret = pinctrl_ops->settings_get_one(pmx->ph, group, GROUP_TYPE, type,
					    &config_value);
	/* Convert SCMI error code to PINCTRL expected error code */
	if (ret == -EOPNOTSUPP)
		return -ENOTSUPP;
	if (ret)
		return ret;

	*config = pinconf_to_config_packed(config_type, config_value);

	return 0;
}

static const struct pinconf_ops pinctrl_scmi_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = pinctrl_scmi_pinconf_get,
	.pin_config_set = pinctrl_scmi_pinconf_set,
	.pin_config_group_set = pinctrl_scmi_pinconf_group_set,
	.pin_config_group_get = pinctrl_scmi_pinconf_group_get,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static int pinctrl_scmi_get_pins(struct scmi_pinctrl *pmx,
				 struct pinctrl_desc *desc)
{
	struct pinctrl_pin_desc *pins;
	unsigned int npins;
	int ret, i;

	npins = pinctrl_ops->count_get(pmx->ph, PIN_TYPE);
	/*
	 * npins will never be zero, the scmi pinctrl driver has bailed out
	 * if npins is zero.
	 */
	pins = devm_kmalloc_array(pmx->dev, npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < npins; i++) {
		pins[i].number = i;
		/*
		 * The memory for name is handled by the scmi firmware driver,
		 * no need free here
		 */
		ret = pinctrl_ops->name_get(pmx->ph, i, PIN_TYPE, &pins[i].name);
		if (ret)
			return dev_err_probe(pmx->dev, ret,
					     "Can't get name for pin %d", i);
	}

	desc->npins = npins;
	desc->pins = pins;
	dev_dbg(pmx->dev, "got pins %u", npins);

	return 0;
}

static const char * const scmi_pinctrl_blocklist[] = {
	"fsl,imx95",
	NULL
};

static int scmi_pinctrl_probe(struct scmi_device *sdev)
{
	int ret;
	struct device *dev = &sdev->dev;
	struct scmi_pinctrl *pmx;
	const struct scmi_handle *handle;
	struct scmi_protocol_handle *ph;

	if (!sdev->handle)
		return -EINVAL;

	if (of_machine_compatible_match(scmi_pinctrl_blocklist))
		return -ENODEV;

	handle = sdev->handle;

	pinctrl_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_PINCTRL, &ph);
	if (IS_ERR(pinctrl_ops))
		return PTR_ERR(pinctrl_ops);

	pmx = devm_kzalloc(dev, sizeof(*pmx), GFP_KERNEL);
	if (!pmx)
		return -ENOMEM;

	pmx->ph = ph;

	pmx->dev = dev;
	pmx->pctl_desc.name = DRV_NAME;
	pmx->pctl_desc.owner = THIS_MODULE;
	pmx->pctl_desc.pctlops = &pinctrl_scmi_pinctrl_ops;
	pmx->pctl_desc.pmxops = &pinctrl_scmi_pinmux_ops;
	pmx->pctl_desc.confops = &pinctrl_scmi_pinconf_ops;

	ret = pinctrl_scmi_get_pins(pmx, &pmx->pctl_desc);
	if (ret)
		return ret;

	ret = devm_pinctrl_register_and_init(dev, &pmx->pctl_desc, pmx,
					     &pmx->pctldev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register pinctrl\n");

	pmx->nr_functions = pinctrl_scmi_get_functions_count(pmx->pctldev);
	pmx->functions = devm_kcalloc(dev, pmx->nr_functions,
				      sizeof(*pmx->functions), GFP_KERNEL);
	if (!pmx->functions)
		return -ENOMEM;

	return pinctrl_enable(pmx->pctldev);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_PINCTRL, "pinctrl" },
	{ }
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_pinctrl_driver = {
	.name = DRV_NAME,
	.probe = scmi_pinctrl_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_pinctrl_driver);

MODULE_AUTHOR("Oleksii Moisieiev <oleksii_moisieiev@epam.com>");
MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("ARM SCMI pin controller driver");
MODULE_LICENSE("GPL");
