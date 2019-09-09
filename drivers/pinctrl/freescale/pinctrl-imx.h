/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * IMX pinmux core definitions
 *
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 *
 * Author: Dong Aisheng <dong.aisheng@linaro.org>
 */

#ifndef __DRIVERS_PINCTRL_IMX_H
#define __DRIVERS_PINCTRL_IMX_H

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>

struct platform_device;

extern struct pinmux_ops imx_pmx_ops;
extern const struct dev_pm_ops imx_pinctrl_pm_ops;

/**
 * struct imx_pin_mmio - MMIO pin configurations
 * @mux_mode: the mux mode for this pin.
 * @input_reg: the select input register offset for this pin if any
 *	0 if no select input setting needed.
 * @input_val: the select input value for this pin.
 * @configs: the config for this pin.
 */
struct imx_pin_mmio {
	unsigned int mux_mode;
	u16 input_reg;
	unsigned int input_val;
	unsigned long config;
};

/**
 * struct imx_pin_scu - SCU pin configurations
 * @mux: the mux mode for this pin.
 * @configs: the config for this pin.
 */
struct imx_pin_scu {
	unsigned int mux_mode;
	unsigned long config;
};

/**
 * struct imx_pin - describes a single i.MX pin
 * @pin: the pin_id of this pin
 * @conf: config type of this pin, either mmio or scu
 */
struct imx_pin {
	unsigned int pin;
	union {
		struct imx_pin_mmio mmio;
		struct imx_pin_scu scu;
	} conf;
};

/**
 * struct imx_pin_reg - describe a pin reg map
 * @mux_reg: mux register offset
 * @conf_reg: config register offset
 */
struct imx_pin_reg {
	s16 mux_reg;
	s16 conf_reg;
};

/* decode a generic config into raw register value */
struct imx_cfg_params_decode {
	enum pin_config_param param;
	u32 mask;
	u8 shift;
	bool invert;
};

struct imx_pinctrl_soc_info {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	unsigned int flags;
	const char *gpr_compatible;

	/* MUX_MODE shift and mask in case SHARE_MUX_CONF_REG */
	unsigned int mux_mask;
	u8 mux_shift;

	/* generic pinconf */
	bool generic_pinconf;
	const struct pinconf_generic_params *custom_params;
	unsigned int num_custom_params;
	const struct imx_cfg_params_decode *decodes;
	unsigned int num_decodes;
	void (*fixup)(unsigned long *configs, unsigned int num_configs,
		      u32 *raw_config);

	int (*gpio_set_direction)(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset,
				  bool input);
};

/**
 * @dev: a pointer back to containing device
 * @base: the offset to the controller in virtual memory
 */
struct imx_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *base;
	void __iomem *input_sel_base;
	const struct imx_pinctrl_soc_info *info;
	struct imx_pin_reg *pin_regs;
	unsigned int group_index;
	struct mutex mutex;
};

#define IMX_CFG_PARAMS_DECODE(p, m, o) \
	{ .param = p, .mask = m, .shift = o, .invert = false, }

#define IMX_CFG_PARAMS_DECODE_INVERT(p, m, o) \
	{ .param = p, .mask = m, .shift = o, .invert = true, }

#define SHARE_MUX_CONF_REG	BIT(0)
#define ZERO_OFFSET_VALID	BIT(1)
#define IMX_USE_SCU		BIT(2)

#define NO_MUX		0x0
#define NO_PAD		0x0

#define IMX_PINCTRL_PIN(pin) PINCTRL_PIN(pin, #pin)

#define PAD_CTL_MASK(len)	((1 << len) - 1)
#define IMX_MUX_MASK	0x7
#define IOMUXC_CONFIG_SION	(0x1 << 4)

int imx_pinctrl_probe(struct platform_device *pdev,
			const struct imx_pinctrl_soc_info *info);

#ifdef CONFIG_PINCTRL_IMX_SCU
#define BM_PAD_CTL_GP_ENABLE		BIT(30)
#define BM_PAD_CTL_IFMUX_ENABLE		BIT(31)
#define BP_PAD_CTL_IFMUX		27

int imx_pinctrl_sc_ipc_init(struct platform_device *pdev);
int imx_pinconf_get_scu(struct pinctrl_dev *pctldev, unsigned pin_id,
			unsigned long *config);
int imx_pinconf_set_scu(struct pinctrl_dev *pctldev, unsigned pin_id,
			unsigned long *configs, unsigned num_configs);
void imx_pinctrl_parse_pin_scu(struct imx_pinctrl *ipctl,
			       unsigned int *pin_id, struct imx_pin *pin,
			       const __be32 **list_p);
#else
static inline int imx_pinconf_get_scu(struct pinctrl_dev *pctldev,
				      unsigned pin_id, unsigned long *config)
{
	return -EINVAL;
}
static inline int imx_pinconf_set_scu(struct pinctrl_dev *pctldev,
				      unsigned pin_id, unsigned long *configs,
				      unsigned num_configs)
{
	return -EINVAL;
}
static inline void imx_pinctrl_parse_pin_scu(struct imx_pinctrl *ipctl,
					    unsigned int *pin_id,
					    struct imx_pin *pin,
					    const __be32 **list_p)
{
}
#endif
#endif /* __DRIVERS_PINCTRL_IMX_H */
