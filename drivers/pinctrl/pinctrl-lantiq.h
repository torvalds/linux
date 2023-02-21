/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/drivers/pinctrl/pinctrl-lantiq.h
 *  based on linux/drivers/pinctrl/pinctrl-pxa3xx.h
 *
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 */

#ifndef __PINCTRL_LANTIQ_H
#define __PINCTRL_LANTIQ_H

#include <linux/clkdev.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#define LTQ_MAX_MUX		4
#define MFPR_FUNC_MASK		0x3

#define LTQ_PINCONF_PACK(param, arg)		((param) << 16 | (arg))
#define LTQ_PINCONF_UNPACK_PARAM(conf)		((conf) >> 16)
#define LTQ_PINCONF_UNPACK_ARG(conf)		((conf) & 0xffff)

enum ltq_pinconf_param {
	LTQ_PINCONF_PARAM_PULL,
	LTQ_PINCONF_PARAM_OPEN_DRAIN,
	LTQ_PINCONF_PARAM_DRIVE_CURRENT,
	LTQ_PINCONF_PARAM_SLEW_RATE,
	LTQ_PINCONF_PARAM_OUTPUT,
};

struct ltq_cfg_param {
	const char *property;
	enum ltq_pinconf_param param;
};

struct ltq_mfp_pin {
	const char *name;
	const unsigned int pin;
	const unsigned short func[LTQ_MAX_MUX];
};

struct ltq_pin_group {
	const char *name;
	const unsigned mux;
	const unsigned *pins;
	const unsigned npins;
};

struct ltq_pmx_func {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;
};

struct ltq_pinmux_info {
	struct device *dev;
	struct pinctrl_dev *pctrl;

	/* we need to manage up to 5 pad controllers */
	void __iomem *membase[5];

	/* the descriptor for the subsystem */
	struct pinctrl_desc *desc;

	/* we expose our pads to the subsystem */
	struct pinctrl_pin_desc *pads;

	/* the number of pads. this varies between socs */
	unsigned int num_pads;

	/* these are our multifunction pins */
	const struct ltq_mfp_pin *mfp;
	unsigned int num_mfp;

	/* a number of multifunction pins can be grouped together */
	const struct ltq_pin_group *grps;
	unsigned int num_grps;

	/* a mapping between function string and id */
	const struct ltq_pmx_func *funcs;
	unsigned int num_funcs;

	/* the pinconf options that we are able to read from the DT */
	const struct ltq_cfg_param *params;
	unsigned int num_params;

	/* the pad controller can have a irq mapping  */
	const unsigned *exin;
	unsigned int num_exin;

	/* we need 5 clocks max */
	struct clk *clk[5];

	/* soc specific callback used to apply muxing */
	int (*apply_mux)(struct pinctrl_dev *pctrldev, int pin, int mux);
};

enum ltq_pin {
	GPIO0 = 0,
	GPIO1,
	GPIO2,
	GPIO3,
	GPIO4,
	GPIO5,
	GPIO6,
	GPIO7,
	GPIO8,
	GPIO9,
	GPIO10, /* 10 */
	GPIO11,
	GPIO12,
	GPIO13,
	GPIO14,
	GPIO15,
	GPIO16,
	GPIO17,
	GPIO18,
	GPIO19,
	GPIO20, /* 20 */
	GPIO21,
	GPIO22,
	GPIO23,
	GPIO24,
	GPIO25,
	GPIO26,
	GPIO27,
	GPIO28,
	GPIO29,
	GPIO30, /* 30 */
	GPIO31,
	GPIO32,
	GPIO33,
	GPIO34,
	GPIO35,
	GPIO36,
	GPIO37,
	GPIO38,
	GPIO39,
	GPIO40, /* 40 */
	GPIO41,
	GPIO42,
	GPIO43,
	GPIO44,
	GPIO45,
	GPIO46,
	GPIO47,
	GPIO48,
	GPIO49,
	GPIO50, /* 50 */
	GPIO51,
	GPIO52,
	GPIO53,
	GPIO54,
	GPIO55,
	GPIO56,
	GPIO57,
	GPIO58,
	GPIO59,
	GPIO60, /* 60 */
	GPIO61,
	GPIO62,
	GPIO63,

	GPIO64,
	GPIO65,
	GPIO66,
	GPIO67,
	GPIO68,
	GPIO69,
	GPIO70,
	GPIO71,
	GPIO72,
	GPIO73,
	GPIO74,
	GPIO75,
	GPIO76,
	GPIO77,
	GPIO78,
	GPIO79,
	GPIO80,
	GPIO81,
	GPIO82,
	GPIO83,
	GPIO84,
	GPIO85,
	GPIO86,
	GPIO87,
	GPIO88,
};

extern int ltq_pinctrl_register(struct platform_device *pdev,
				   struct ltq_pinmux_info *info);
extern int ltq_pinctrl_unregister(struct platform_device *pdev);
#endif	/* __PINCTRL_LANTIQ_H */
