/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 Sanechips Technology Co., Ltd.
 * Copyright 2017 Linaro Ltd.
 */

#ifndef __PINCTRL_ZX_H
#define __PINCTRL_ZX_H

/**
 * struct zx_mux_desc - hardware mux descriptor
 * @name: mux function name
 * @muxval: mux register bit value
 */
struct zx_mux_desc {
	const char *name;
	u8 muxval;
};

/**
 * struct zx_pin_data - hardware per-pin data
 * @aon_pin: whether it's an AON pin
 * @offset: register offset within TOP pinmux controller
 * @bitpos: bit position within TOP pinmux register
 * @width: bit width within TOP pinmux register
 * @coffset: pinconf register offset within AON controller
 * @cbitpos: pinconf bit position within AON register
 * @muxes: available mux function names and corresponding register values
 *
 * Unlike TOP pinmux and AON pinconf registers which are arranged pretty
 * arbitrarily, AON pinmux register bits are well organized per pin id, and
 * each pin occupies two bits, so that we can calculate the AON register offset
 * and bit position from pin id.  Thus, we only need to define TOP pinmux and
 * AON pinconf register data for the pin.
 */
struct zx_pin_data {
	bool aon_pin;
	u16 offset;
	u16 bitpos;
	u16 width;
	u16 coffset;
	u16 cbitpos;
	struct zx_mux_desc *muxes;
};

struct zx_pinctrl_soc_info {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
};

#define TOP_PIN(pin, off, bp, wd, coff, cbp, ...) {		\
	.number = pin,						\
	.name = #pin,						\
	.drv_data = &(struct zx_pin_data) {			\
		.aon_pin = false,				\
		.offset = off,					\
		.bitpos = bp,					\
		.width = wd,					\
		.coffset = coff,				\
		.cbitpos = cbp,					\
		.muxes = (struct zx_mux_desc[]) {		\
			 __VA_ARGS__, { } },			\
	},							\
}

#define AON_PIN(pin, off, bp, wd, coff, cbp, ...) {		\
	.number = pin,						\
	.name = #pin,						\
	.drv_data = &(struct zx_pin_data) {			\
		.aon_pin = true,				\
		.offset = off,					\
		.bitpos = bp,					\
		.width = wd,					\
		.coffset = coff,				\
		.cbitpos = cbp,					\
		.muxes = (struct zx_mux_desc[]) {		\
			 __VA_ARGS__, { } },			\
	},							\
}

#define ZX_RESERVED(pin) PINCTRL_PIN(pin, #pin)

#define TOP_MUX(_val, _name) {					\
	.name = _name,						\
	.muxval = _val,						\
}

/*
 * When the flag is set, it's a mux configuration for an AON pin that sits in
 * AON register.  Otherwise, it's one for AON pin but sitting in TOP register.
 */
#define AON_MUX_FLAG BIT(7)

#define AON_MUX(_val, _name) {					\
	.name = _name,						\
	.muxval = _val | AON_MUX_FLAG,				\
}

int zx_pinctrl_init(struct platform_device *pdev,
		    struct zx_pinctrl_soc_info *info);

#endif /* __PINCTRL_ZX_H */
