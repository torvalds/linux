// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Power Interface (SCMI) Protocol based i.MX pinctrl driver
 *
 * Copyright 2024 NXP
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/scmi_protocol.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../pinctrl-utils.h"
#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define DRV_NAME "scmi-pinctrl-imx"

struct scmi_pinctrl_imx {
	struct device *dev;
	struct scmi_protocol_handle *ph;
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc pctl_desc;
	const struct scmi_pinctrl_proto_ops *ops;
};

/* SCMI pin control types, aligned with SCMI firmware */
#define IMX_SCMI_NUM_CFG	4
#define IMX_SCMI_PIN_MUX	192
#define IMX_SCMI_PIN_CONFIG	193
#define IMX_SCMI_PIN_DAISY_ID	194
#define IMX_SCMI_PIN_DAISY_CFG	195

#define IMX_SCMI_NO_PAD_CTL		BIT(31)
#define IMX_SCMI_PAD_SION		BIT(30)
#define IMX_SCMI_IOMUXC_CONFIG_SION	BIT(4)

#define IMX_SCMI_PIN_SIZE	24

#define IMX95_DAISY_OFF		0x408

static int pinctrl_scmi_imx_dt_node_to_map(struct pinctrl_dev *pctldev,
					   struct device_node *np,
					   struct pinctrl_map **map,
					   unsigned int *num_maps)
{
	struct pinctrl_map *new_map;
	const __be32 *list;
	unsigned long *configs = NULL;
	unsigned long cfg[IMX_SCMI_NUM_CFG];
	int map_num, size, pin_size, pin_id, num_pins;
	int mux_reg, conf_reg, input_reg, mux_val, conf_val, input_val;
	int i, j;
	uint32_t ncfg;
	static uint32_t daisy_off;

	if (!daisy_off) {
		if (of_machine_is_compatible("fsl,imx95")) {
			daisy_off = IMX95_DAISY_OFF;
		} else {
			dev_err(pctldev->dev, "platform not support scmi pinctrl\n");
			return -EINVAL;
		}
	}

	list = of_get_property(np, "fsl,pins", &size);
	if (!list) {
		dev_err(pctldev->dev, "no fsl,pins property in node %pOF\n", np);
		return -EINVAL;
	}

	pin_size = IMX_SCMI_PIN_SIZE;

	if (!size || size % pin_size) {
		dev_err(pctldev->dev, "Invalid fsl,pins or pins property in node %pOF\n", np);
		return -EINVAL;
	}

	num_pins = size / pin_size;
	map_num = num_pins;

	new_map = kmalloc_array(map_num, sizeof(struct pinctrl_map),
				GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;

	/* create config map */
	for (i = 0; i < num_pins; i++) {
		j = 0;
		ncfg = IMX_SCMI_NUM_CFG;
		mux_reg = be32_to_cpu(*list++);
		conf_reg = be32_to_cpu(*list++);
		input_reg = be32_to_cpu(*list++);
		mux_val = be32_to_cpu(*list++);
		input_val = be32_to_cpu(*list++);
		conf_val = be32_to_cpu(*list++);
		if (conf_val & IMX_SCMI_PAD_SION)
			mux_val |= IMX_SCMI_IOMUXC_CONFIG_SION;

		pin_id = mux_reg / 4;

		cfg[j++] = pinconf_to_config_packed(IMX_SCMI_PIN_MUX, mux_val);

		if (!conf_reg || (conf_val & IMX_SCMI_NO_PAD_CTL))
			ncfg--;
		else
			cfg[j++] = pinconf_to_config_packed(IMX_SCMI_PIN_CONFIG, conf_val);

		if (!input_reg) {
			ncfg -= 2;
		} else {
			cfg[j++] = pinconf_to_config_packed(IMX_SCMI_PIN_DAISY_ID,
							    (input_reg - daisy_off) / 4);
			cfg[j++] = pinconf_to_config_packed(IMX_SCMI_PIN_DAISY_CFG, input_val);
		}

		configs = kmemdup_array(cfg, ncfg, sizeof(unsigned long), GFP_KERNEL);

		new_map[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[i].data.configs.group_or_pin = pin_get_name(pctldev, pin_id);
		new_map[i].data.configs.configs = configs;
		new_map[i].data.configs.num_configs = ncfg;
	}

	return 0;
}

static void pinctrl_scmi_imx_dt_free_map(struct pinctrl_dev *pctldev,
					 struct pinctrl_map *map, unsigned int num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops pinctrl_scmi_imx_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinctrl_scmi_imx_dt_node_to_map,
	.dt_free_map = pinctrl_scmi_imx_dt_free_map,
};

static int pinctrl_scmi_imx_func_set_mux(struct pinctrl_dev *pctldev,
					 unsigned int selector, unsigned int group)
{
	/*
	 * For i.MX SCMI PINCTRL , postpone the mux setting
	 * until config is set as they can be set together
	 * in one IPC call
	 */
	return 0;
}

static const struct pinmux_ops pinctrl_scmi_imx_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = pinctrl_scmi_imx_func_set_mux,
};

static int pinctrl_scmi_imx_pinconf_get(struct pinctrl_dev *pctldev,
					unsigned int pin, unsigned long *config)
{
	int ret;
	struct scmi_pinctrl_imx *pmx = pinctrl_dev_get_drvdata(pctldev);
	u32 config_type, val;

	if (!config)
		return -EINVAL;

	config_type = pinconf_to_config_param(*config);

	ret = pmx->ops->settings_get_one(pmx->ph, pin, PIN_TYPE, config_type, &val);
	/* Convert SCMI error code to PINCTRL expected error code */
	if (ret == -EOPNOTSUPP)
		return -ENOTSUPP;
	if (ret)
		return ret;

	*config = pinconf_to_config_packed(config_type, val);

	dev_dbg(pmx->dev, "pin:%s, conf:0x%x", pin_get_name(pctldev, pin), val);

	return 0;
}

static int pinctrl_scmi_imx_pinconf_set(struct pinctrl_dev *pctldev,
					unsigned int pin,
					unsigned long *configs,
					unsigned int num_configs)
{
	struct scmi_pinctrl_imx *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum scmi_pinctrl_conf_type config_type[IMX_SCMI_NUM_CFG];
	u32 config_value[IMX_SCMI_NUM_CFG];
	enum scmi_pinctrl_conf_type *p_config_type = config_type;
	u32 *p_config_value = config_value;
	int ret;
	int i;

	if (!configs || !num_configs)
		return -EINVAL;

	if (num_configs > IMX_SCMI_NUM_CFG) {
		dev_err(pmx->dev, "num_configs(%d) too large\n", num_configs);
		return -EINVAL;
	}

	for (i = 0; i < num_configs; i++) {
		/* cast to avoid build warning */
		p_config_type[i] =
			(enum scmi_pinctrl_conf_type)pinconf_to_config_param(configs[i]);
		p_config_value[i] = pinconf_to_config_argument(configs[i]);

		dev_dbg(pmx->dev, "pin: %u, type: %u, val: 0x%x\n",
			pin, p_config_type[i], p_config_value[i]);
	}

	ret = pmx->ops->settings_conf(pmx->ph, pin, PIN_TYPE, num_configs,
				      p_config_type,  p_config_value);
	if (ret)
		dev_err(pmx->dev, "Error set config %d\n", ret);

	return ret;
}

static void pinctrl_scmi_imx_pinconf_dbg_show(struct pinctrl_dev *pctldev,
					      struct seq_file *s, unsigned int pin_id)
{
	unsigned long config = pinconf_to_config_packed(IMX_SCMI_PIN_CONFIG, 0);
	int ret;

	ret = pinctrl_scmi_imx_pinconf_get(pctldev, pin_id, &config);
	if (ret)
		config = 0;
	else
		config = pinconf_to_config_argument(config);

	seq_printf(s, "0x%lx", config);
}

static const struct pinconf_ops pinctrl_scmi_imx_pinconf_ops = {
	.pin_config_get = pinctrl_scmi_imx_pinconf_get,
	.pin_config_set = pinctrl_scmi_imx_pinconf_set,
	.pin_config_dbg_show = pinctrl_scmi_imx_pinconf_dbg_show,
};

static int
scmi_pinctrl_imx_get_pins(struct scmi_pinctrl_imx *pmx, struct pinctrl_desc *desc)
{
	struct pinctrl_pin_desc *pins;
	unsigned int npins;
	int ret, i;

	npins = pmx->ops->count_get(pmx->ph, PIN_TYPE);
	pins = devm_kmalloc_array(pmx->dev, npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < npins; i++) {
		pins[i].number = i;
		/* no need free name, firmware driver handles it */
		ret = pmx->ops->name_get(pmx->ph, i, PIN_TYPE, &pins[i].name);
		if (ret)
			return dev_err_probe(pmx->dev, ret,
					     "Can't get name for pin %d", i);
	}

	desc->npins = npins;
	desc->pins = pins;
	dev_dbg(pmx->dev, "got pins %u", npins);

	return 0;
}

static const char * const scmi_pinctrl_imx_allowlist[] = {
	"fsl,imx95",
	NULL
};

static int scmi_pinctrl_imx_probe(struct scmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	const struct scmi_handle *handle = sdev->handle;
	struct scmi_pinctrl_imx *pmx;
	struct scmi_protocol_handle *ph;
	const struct scmi_pinctrl_proto_ops *pinctrl_ops;
	int ret;

	if (!handle)
		return -EINVAL;

	if (!of_machine_compatible_match(scmi_pinctrl_imx_allowlist))
		return -ENODEV;

	pinctrl_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_PINCTRL, &ph);
	if (IS_ERR(pinctrl_ops))
		return PTR_ERR(pinctrl_ops);

	pmx = devm_kzalloc(dev, sizeof(*pmx), GFP_KERNEL);
	if (!pmx)
		return -ENOMEM;

	pmx->ph = ph;
	pmx->ops = pinctrl_ops;

	pmx->dev = dev;
	pmx->pctl_desc.name = DRV_NAME;
	pmx->pctl_desc.owner = THIS_MODULE;
	pmx->pctl_desc.pctlops = &pinctrl_scmi_imx_pinctrl_ops;
	pmx->pctl_desc.pmxops = &pinctrl_scmi_imx_pinmux_ops;
	pmx->pctl_desc.confops = &pinctrl_scmi_imx_pinconf_ops;

	ret = scmi_pinctrl_imx_get_pins(pmx, &pmx->pctl_desc);
	if (ret)
		return ret;

	pmx->dev = &sdev->dev;

	ret = devm_pinctrl_register_and_init(dev, &pmx->pctl_desc, pmx,
					     &pmx->pctldev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register pinctrl\n");

	return pinctrl_enable(pmx->pctldev);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_PINCTRL, "pinctrl-imx" },
	{ }
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_pinctrl_imx_driver = {
	.name = DRV_NAME,
	.probe = scmi_pinctrl_imx_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_pinctrl_imx_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("i.MX SCMI pin controller driver");
MODULE_LICENSE("GPL");
