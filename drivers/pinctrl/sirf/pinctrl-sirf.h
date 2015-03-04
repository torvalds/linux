/*
 * pinmux driver shared headfile for CSR SiRFsoc
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __PINMUX_SIRF_H__
#define __PINMUX_SIRF_H__

#define SIRFSOC_NUM_PADS		622
#define SIRFSOC_RSC_USB_UART_SHARE	0
#define SIRFSOC_RSC_PIN_MUX		0x4

#define SIRFSOC_GPIO_PAD_EN(g)		((g)*0x100 + 0x84)
#define SIRFSOC_GPIO_PAD_EN_CLR(g)	((g)*0x100 + 0x90)
#define SIRFSOC_GPIO_CTRL(g, i)			((g)*0x100 + (i)*4)
#define SIRFSOC_GPIO_DSP_EN0			(0x80)
#define SIRFSOC_GPIO_INT_STATUS(g)		((g)*0x100 + 0x8C)

#define SIRFSOC_GPIO_CTL_INTR_LOW_MASK		0x1
#define SIRFSOC_GPIO_CTL_INTR_HIGH_MASK		0x2
#define SIRFSOC_GPIO_CTL_INTR_TYPE_MASK		0x4
#define SIRFSOC_GPIO_CTL_INTR_EN_MASK		0x8
#define SIRFSOC_GPIO_CTL_INTR_STS_MASK		0x10
#define SIRFSOC_GPIO_CTL_OUT_EN_MASK		0x20
#define SIRFSOC_GPIO_CTL_DATAOUT_MASK		0x40
#define SIRFSOC_GPIO_CTL_DATAIN_MASK		0x80
#define SIRFSOC_GPIO_CTL_PULL_MASK		0x100
#define SIRFSOC_GPIO_CTL_PULL_HIGH		0x200
#define SIRFSOC_GPIO_CTL_DSP_INT		0x400

#define SIRFSOC_GPIO_NO_OF_BANKS        5
#define SIRFSOC_GPIO_BANK_SIZE          32
#define SIRFSOC_GPIO_NUM(bank, index)	(((bank)*(32)) + (index))

/**
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 */
struct sirfsoc_pmx {
	struct device *dev;
	struct pinctrl_dev *pmx;
	void __iomem *gpio_virtbase;
	void __iomem *rsc_virtbase;
	u32 gpio_regs[SIRFSOC_GPIO_NO_OF_BANKS][SIRFSOC_GPIO_BANK_SIZE];
	u32 ints_regs[SIRFSOC_GPIO_NO_OF_BANKS];
	u32 paden_regs[SIRFSOC_GPIO_NO_OF_BANKS];
	u32 dspen_regs;
	u32 rsc_regs[3];
};

/* SIRFSOC_GPIO_PAD_EN set */
struct sirfsoc_muxmask {
	unsigned long group;
	unsigned long mask;
};

struct sirfsoc_padmux {
	unsigned long muxmask_counts;
	const struct sirfsoc_muxmask *muxmask;
	/* RSC_PIN_MUX set */
	unsigned long ctrlreg;
	unsigned long funcmask;
	unsigned long funcval;
};

 /**
 * struct sirfsoc_pin_group - describes a SiRFprimaII pin group
 * @name: the name of this specific pin group
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @num_pins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 */
struct sirfsoc_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned num_pins;
};

#define SIRFSOC_PIN_GROUP(n, p)  \
	{			\
		.name = n,	\
		.pins = p,	\
		.num_pins = ARRAY_SIZE(p),	\
	}

struct sirfsoc_pmx_func {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;
	const struct sirfsoc_padmux *padmux;
};

#define SIRFSOC_PMX_FUNCTION(n, g, m)		\
	{					\
		.name = n,			\
		.groups = g,			\
		.num_groups = ARRAY_SIZE(g),	\
		.padmux = &m,			\
	}

struct sirfsoc_pinctrl_data {
	struct pinctrl_pin_desc *pads;
	int pads_cnt;
	struct sirfsoc_pin_group *grps;
	int grps_cnt;
	struct sirfsoc_pmx_func *funcs;
	int funcs_cnt;
};

extern struct sirfsoc_pinctrl_data prima2_pinctrl_data;
extern struct sirfsoc_pinctrl_data atlas6_pinctrl_data;

#endif
