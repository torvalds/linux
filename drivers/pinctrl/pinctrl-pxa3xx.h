/*
 *  linux/drivers/pinctrl/pinctrl-pxa3xx.h
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 *
 *  Copyright (C) 2011, Marvell Technology Group Ltd.
 *
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 */

#ifndef __PINCTRL_PXA3XX_H

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#define PXA3xx_MUX_GPIO		0

#define PXA3xx_MAX_MUX		8
#define MFPR_FUNC_MASK		0x7

enum pxa_cpu_type {
	PINCTRL_INVALID = 0,
	PINCTRL_PXA300,
	PINCTRL_PXA310,
	PINCTRL_PXA320,
	PINCTRL_PXA168,
	PINCTRL_PXA910,
	PINCTRL_PXA930,
	PINCTRL_PXA955,
	PINCTRL_MMP2,
	PINCTRL_MAX,
};

struct pxa3xx_mfp_pin {
	const char *name;
	const unsigned int pin;
	const unsigned int mfpr;	/* register offset */
	const unsigned short func[8];
};

struct pxa3xx_pin_group {
	const char *name;
	const unsigned mux;
	const unsigned *pins;
	const unsigned npins;
};

struct pxa3xx_pmx_func {
	const char *name;
	const char * const * groups;
	const unsigned num_groups;
};

struct pxa3xx_pinmux_info {
	struct device *dev;
	struct pinctrl_dev *pctrl;
	enum pxa_cpu_type cputype;
	unsigned int phy_base;
	unsigned int phy_size;
	void __iomem *virt_base;

	struct pxa3xx_mfp_pin *mfp;
	unsigned int num_mfp;
	struct pxa3xx_pin_group *grps;
	unsigned int num_grps;
	struct pxa3xx_pmx_func *funcs;
	unsigned int num_funcs;
	unsigned int num_gpio;
	struct pinctrl_desc *desc;
	struct pinctrl_pin_desc *pads;
	unsigned int num_pads;

	unsigned ds_mask;	/* drive strength mask */
	unsigned ds_shift;	/* drive strength shift */
	unsigned slp_mask;	/* sleep mask */
	unsigned slp_input_low;
	unsigned slp_input_high;
	unsigned slp_output_low;
	unsigned slp_output_high;
	unsigned slp_float;
};

enum pxa3xx_pin_list {
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
	GPIO70, /* 70 */
	GPIO71,
	GPIO72,
	GPIO73,
	GPIO74,
	GPIO75,
	GPIO76,
	GPIO77,
	GPIO78,
	GPIO79,
	GPIO80, /* 80 */
	GPIO81,
	GPIO82,
	GPIO83,
	GPIO84,
	GPIO85,
	GPIO86,
	GPIO87,
	GPIO88,
	GPIO89,
	GPIO90, /* 90 */
	GPIO91,
	GPIO92,
	GPIO93,
	GPIO94,
	GPIO95,
	GPIO96,
	GPIO97,
	GPIO98,
	GPIO99,
	GPIO100, /* 100 */
	GPIO101,
	GPIO102,
	GPIO103,
	GPIO104,
	GPIO105,
	GPIO106,
	GPIO107,
	GPIO108,
	GPIO109,
	GPIO110, /* 110 */
	GPIO111,
	GPIO112,
	GPIO113,
	GPIO114,
	GPIO115,
	GPIO116,
	GPIO117,
	GPIO118,
	GPIO119,
	GPIO120, /* 120 */
	GPIO121,
	GPIO122,
	GPIO123,
	GPIO124,
	GPIO125,
	GPIO126,
	GPIO127,
	GPIO128,
	GPIO129,
	GPIO130, /* 130 */
	GPIO131,
	GPIO132,
	GPIO133,
	GPIO134,
	GPIO135,
	GPIO136,
	GPIO137,
	GPIO138,
	GPIO139,
	GPIO140, /* 140 */
	GPIO141,
	GPIO142,
	GPIO143,
	GPIO144,
	GPIO145,
	GPIO146,
	GPIO147,
	GPIO148,
	GPIO149,
	GPIO150, /* 150 */
	GPIO151,
	GPIO152,
	GPIO153,
	GPIO154,
	GPIO155,
	GPIO156,
	GPIO157,
	GPIO158,
	GPIO159,
	GPIO160, /* 160 */
	GPIO161,
	GPIO162,
	GPIO163,
	GPIO164,
	GPIO165,
	GPIO166,
	GPIO167,
	GPIO168,
	GPIO169,
};

extern int pxa3xx_pinctrl_register(struct platform_device *pdev,
				   struct pxa3xx_pinmux_info *info);
extern int pxa3xx_pinctrl_unregister(struct platform_device *pdev);
#endif	/* __PINCTRL_PXA3XX_H */
