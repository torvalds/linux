// SPDX-License-Identifier: GPL-2.0+
/*
 * OWL SoC's Pinctrl definitions
 *
 * Copyright (c) 2014 Actions Semi Inc.
 * Author: David Liu <liuwei@actions-semi.com>
 *
 * Copyright (c) 2018 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#ifndef __PINCTRL_OWL_H__
#define __PINCTRL_OWL_H__

#define OWL_PINCONF_SLEW_SLOW 0
#define OWL_PINCONF_SLEW_FAST 1

enum owl_pinconf_pull {
	OWL_PINCONF_PULL_HIZ,
	OWL_PINCONF_PULL_DOWN,
	OWL_PINCONF_PULL_UP,
	OWL_PINCONF_PULL_HOLD,
};

enum owl_pinconf_drv {
	OWL_PINCONF_DRV_2MA,
	OWL_PINCONF_DRV_4MA,
	OWL_PINCONF_DRV_8MA,
	OWL_PINCONF_DRV_12MA,
};

/* GPIO CTRL Bit Definition */
#define OWL_GPIO_CTLR_PENDING		0
#define OWL_GPIO_CTLR_ENABLE		1
#define OWL_GPIO_CTLR_SAMPLE_CLK_24M	2

/* GPIO TYPE Bit Definition */
#define OWL_GPIO_INT_LEVEL_HIGH		0
#define OWL_GPIO_INT_LEVEL_LOW		1
#define OWL_GPIO_INT_EDGE_RISING	2
#define OWL_GPIO_INT_EDGE_FALLING	3
#define OWL_GPIO_INT_MASK		3

/**
 * struct owl_pullctl - Actions pad pull control register
 * @reg: offset to the pull control register
 * @shift: shift value of the register
 * @width: width of the register
 */
struct owl_pullctl {
	int reg;
	unsigned int shift;
	unsigned int width;
};

/**
 * struct owl_st - Actions pad schmitt trigger enable register
 * @reg: offset to the schmitt trigger enable register
 * @shift: shift value of the register
 * @width: width of the register
 */
struct owl_st {
	int reg;
	unsigned int shift;
	unsigned int width;
};

/**
 * struct owl_pingroup - Actions pingroup definition
 * @name: name of the  pin group
 * @pads: list of pins assigned to this pingroup
 * @npads: size of @pads array
 * @funcs: list of pinmux functions for this pingroup
 * @nfuncs: size of @funcs array
 * @mfpctl_reg: multiplexing control register offset
 * @mfpctl_shift: multiplexing control register bit mask
 * @mfpctl_width: multiplexing control register width
 * @drv_reg: drive control register offset
 * @drv_shift: drive control register bit mask
 * @drv_width: driver control register width
 * @sr_reg: slew rate control register offset
 * @sr_shift: slew rate control register bit mask
 * @sr_width: slew rate control register width
 */
struct owl_pingroup {
	const char *name;
	unsigned int *pads;
	unsigned int npads;
	unsigned int *funcs;
	unsigned int nfuncs;

	int mfpctl_reg;
	unsigned int mfpctl_shift;
	unsigned int mfpctl_width;

	int drv_reg;
	unsigned int drv_shift;
	unsigned int drv_width;

	int sr_reg;
	unsigned int sr_shift;
	unsigned int sr_width;
};

/**
 * struct owl_padinfo - Actions pinctrl pad info
 * @pad: pad name of the SoC
 * @pullctl: pull control register info
 * @st: schmitt trigger register info
 */
struct owl_padinfo {
	int pad;
	struct owl_pullctl *pullctl;
	struct owl_st *st;
};

/**
 * struct owl_pinmux_func - Actions pinctrl mux functions
 * @name: name of the pinmux function.
 * @groups: array of pin groups that may select this function.
 * @ngroups: number of entries in @groups.
 */
struct owl_pinmux_func {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

/**
 * struct owl_gpio_port - Actions GPIO port info
 * @offset: offset of the GPIO port.
 * @pins: number of pins belongs to the GPIO port.
 * @outen: offset of the output enable register.
 * @inen: offset of the input enable register.
 * @dat: offset of the data register.
 * @intc_ctl: offset of the interrupt control register.
 * @intc_pd: offset of the interrupt pending register.
 * @intc_msk: offset of the interrupt mask register.
 * @intc_type: offset of the interrupt type register.
 */
struct owl_gpio_port {
	unsigned int offset;
	unsigned int pins;
	unsigned int outen;
	unsigned int inen;
	unsigned int dat;
	unsigned int intc_ctl;
	unsigned int intc_pd;
	unsigned int intc_msk;
	unsigned int intc_type;
};

/**
 * struct owl_pinctrl_soc_data - Actions pin controller driver configuration
 * @pins: array describing all pins of the pin controller.
 * @npins: number of entries in @pins.
 * @functions: array describing all mux functions of this SoC.
 * @nfunction: number of entries in @functions.
 * @groups: array describing all pin groups of this SoC.
 * @ngroups: number of entries in @groups.
 * @padinfo: array describing the pad info of this SoC.
 * @ngpios: number of pingroups the driver should expose as GPIOs.
 * @ports: array describing all GPIO ports of this SoC.
 * @nports: number of GPIO ports in this SoC.
 */
struct owl_pinctrl_soc_data {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct owl_pinmux_func *functions;
	unsigned int nfunctions;
	const struct owl_pingroup *groups;
	unsigned int ngroups;
	const struct owl_padinfo *padinfo;
	unsigned int ngpios;
	const struct owl_gpio_port *ports;
	unsigned int nports;
};

int owl_pinctrl_probe(struct platform_device *pdev,
		struct owl_pinctrl_soc_data *soc_data);

#endif /* __PINCTRL_OWL_H__ */
