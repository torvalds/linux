/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SP7021 Pin Controller Driver.
 * Copyright (C) Sunplus Tech / Tibbo Tech.
 */

#ifndef __SPPCTL_H__
#define __SPPCTL_H__

#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define SPPCTL_MODULE_NAME		"sppctl_sp7021"

#define SPPCTL_GPIO_OFF_FIRST		0x00
#define SPPCTL_GPIO_OFF_MASTER		0x00
#define SPPCTL_GPIO_OFF_OE		0x20
#define SPPCTL_GPIO_OFF_OUT		0x40
#define SPPCTL_GPIO_OFF_IN		0x60
#define SPPCTL_GPIO_OFF_IINV		0x80
#define SPPCTL_GPIO_OFF_OINV		0xa0
#define SPPCTL_GPIO_OFF_OD		0xc0

#define SPPCTL_FULLY_PINMUX_MASK_MASK	GENMASK(22, 16)
#define SPPCTL_FULLY_PINMUX_SEL_MASK	GENMASK(6, 0)
#define SPPCTL_FULLY_PINMUX_UPPER_SHIFT	8

/*
 * Mask-fields and control-fields of MOON registers of SP7021 are
 * arranged as shown below:
 *
 *  register |  mask-fields | control-fields
 * ----------+--------------+----------------
 *  base[0]  |  (31 : 16)   |   (15 : 0)
 *  base[1]  |  (31 : 24)   |   (15 : 0)
 *  base[2]  |  (31 : 24)   |   (15 : 0)
 *     :     |      :       |       :
 *
 * where mask-fields are used to protect control-fields from write-in
 * accidentally. Set the corresponding bits in the mask-field before
 * you write a value into a control-field.
 */
#define SPPCTL_MOON_REG_MASK_SHIFT	16
#define SPPCTL_SET_MOON_REG_BIT(bit)	(BIT((bit) + SPPCTL_MOON_REG_MASK_SHIFT) | BIT(bit))
#define SPPCTL_CLR_MOON_REG_BIT(bit)	BIT((bit) + SPPCTL_MOON_REG_MASK_SHIFT)

#define SPPCTL_IOP_CONFIGS		0xff

#define FNCE(n, r, o, bo, bl, g) { \
	.name = n, \
	.type = r, \
	.roff = o, \
	.boff = bo, \
	.blen = bl, \
	.grps = (g), \
	.gnum = ARRAY_SIZE(g), \
}

#define FNCN(n, r, o, bo, bl) { \
	.name = n, \
	.type = r, \
	.roff = o, \
	.boff = bo, \
	.blen = bl, \
	.grps = NULL, \
	.gnum = 0, \
}

#define EGRP(n, v, p) { \
	.name = n, \
	.gval = (v), \
	.pins = (p), \
	.pnum = ARRAY_SIZE(p), \
}

/**
 * enum mux_first_reg - Define modes of access of FIRST register
 * @mux_f_mux:  Set the corresponding pin to a fully-pinmux pin
 * @mux_f_gpio: Set the corresponding pin to a GPIO or IOP pin
 * @mux_f_keep: Don't change (keep intact)
 */
enum mux_first_reg {
	mux_f_mux = 0,
	mux_f_gpio = 1,
	mux_f_keep = 2,
};

/**
 * enum mux_master_reg - Define modes of access of MASTER register
 * @mux_m_iop:  Set the corresponding pin to an IO processor (IOP) pin
 * @mux_m_gpio: Set the corresponding pin to a digital GPIO pin
 * @mux_m_keep: Don't change (keep intact)
 */
enum mux_master_reg {
	mux_m_iop = 0,
	mux_m_gpio = 1,
	mux_m_keep = 2,
};

/**
 * enum pinmux_type - Define types of pinmux pins
 * @pinmux_type_fpmx: A fully-pinmux pin
 * @pinmux_type_grp:  A group-pinmux pin
 */
enum pinmux_type {
	pinmux_type_fpmx,
	pinmux_type_grp,
};

/**
 * struct grp2fp_map - A map storing indexes
 * @f_idx: an index to function table
 * @g_idx: an index to group table
 */
struct grp2fp_map {
	u16 f_idx;
	u16 g_idx;
};

struct sppctl_gpio_chip;

struct sppctl_pdata {
	void __iomem *moon2_base;	/* MOON2                                 */
	void __iomem *gpioxt_base;	/* MASTER, OE, OUT, IN, I_INV, O_INV, OD */
	void __iomem *first_base;	/* FIRST                                 */
	void __iomem *moon1_base;	/* MOON1               */

	struct pinctrl_desc pctl_desc;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_gpio_range pctl_grange;
	struct sppctl_gpio_chip *spp_gchip;

	char const **unq_grps;
	size_t unq_grps_sz;
	struct grp2fp_map *g2fp_maps;
};

struct sppctl_grp {
	const char * const name;
	const u8 gval;                  /* group number   */
	const unsigned * const pins;    /* list of pins   */
	const unsigned int pnum;        /* number of pins */
};

struct sppctl_func {
	const char * const name;
	const enum pinmux_type type;    /* function type          */
	const u8 roff;                  /* register offset        */
	const u8 boff;                  /* bit offset             */
	const u8 blen;                  /* bit length             */
	const struct sppctl_grp * const grps; /* list of groups   */
	const unsigned int gnum;        /* number of groups       */
};

extern const struct sppctl_func sppctl_list_funcs[];
extern const char * const sppctl_pmux_list_s[];
extern const char * const sppctl_gpio_list_s[];
extern const struct pinctrl_pin_desc sppctl_pins_all[];
extern const unsigned int sppctl_pins_gpio[];

extern const size_t sppctl_list_funcs_sz;
extern const size_t sppctl_pmux_list_sz;
extern const size_t sppctl_gpio_list_sz;
extern const size_t sppctl_pins_all_sz;

#endif
