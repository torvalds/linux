/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * This header provides constants for pinctrl bindings for TI's K3 SoC
 * family.
 *
 * Copyright (C) 2018-2024 Texas Instruments Incorporated - https://www.ti.com/
 */
#ifndef DTS_ARM64_TI_K3_PINCTRL_H
#define DTS_ARM64_TI_K3_PINCTRL_H

#define PULLUDEN_SHIFT		(16)
#define PULLTYPESEL_SHIFT	(17)
#define RXACTIVE_SHIFT		(18)
#define DEBOUNCE_SHIFT		(11)
#define FORCE_DS_EN_SHIFT	(15)
#define DS_EN_SHIFT		(24)
#define DS_OUT_DIS_SHIFT	(25)
#define DS_OUT_VAL_SHIFT	(26)
#define DS_PULLUD_EN_SHIFT	(27)
#define DS_PULLTYPE_SEL_SHIFT	(28)

#define PULL_DISABLE		(1 << PULLUDEN_SHIFT)
#define PULL_ENABLE		(0 << PULLUDEN_SHIFT)

#define PULL_UP			(1 << PULLTYPESEL_SHIFT | PULL_ENABLE)
#define PULL_DOWN		(0 << PULLTYPESEL_SHIFT | PULL_ENABLE)

#define INPUT_EN		(1 << RXACTIVE_SHIFT)
#define INPUT_DISABLE		(0 << RXACTIVE_SHIFT)

/* Only these macros are expected be used directly in device tree files */
#define PIN_OUTPUT		(INPUT_DISABLE | PULL_DISABLE)
#define PIN_OUTPUT_PULLUP	(INPUT_DISABLE | PULL_UP)
#define PIN_OUTPUT_PULLDOWN	(INPUT_DISABLE | PULL_DOWN)
#define PIN_INPUT		(INPUT_EN | PULL_DISABLE)
#define PIN_INPUT_PULLUP	(INPUT_EN | PULL_UP)
#define PIN_INPUT_PULLDOWN	(INPUT_EN | PULL_DOWN)

#define PIN_DEBOUNCE_DISABLE	(0 << DEBOUNCE_SHIFT)
#define PIN_DEBOUNCE_CONF1	(1 << DEBOUNCE_SHIFT)
#define PIN_DEBOUNCE_CONF2	(2 << DEBOUNCE_SHIFT)
#define PIN_DEBOUNCE_CONF3	(3 << DEBOUNCE_SHIFT)
#define PIN_DEBOUNCE_CONF4	(4 << DEBOUNCE_SHIFT)
#define PIN_DEBOUNCE_CONF5	(5 << DEBOUNCE_SHIFT)
#define PIN_DEBOUNCE_CONF6	(6 << DEBOUNCE_SHIFT)

#define PIN_DS_FORCE_DISABLE		(0 << FORCE_DS_EN_SHIFT)
#define PIN_DS_FORCE_ENABLE		(1 << FORCE_DS_EN_SHIFT)
#define PIN_DS_IO_OVERRIDE_DISABLE	(0 << DS_IO_OVERRIDE_EN_SHIFT)
#define PIN_DS_IO_OVERRIDE_ENABLE	(1 << DS_IO_OVERRIDE_EN_SHIFT)
#define PIN_DS_OUT_ENABLE		(0 << DS_OUT_DIS_SHIFT)
#define PIN_DS_OUT_DISABLE		(1 << DS_OUT_DIS_SHIFT)
#define PIN_DS_OUT_VALUE_ZERO		(0 << DS_OUT_VAL_SHIFT)
#define PIN_DS_OUT_VALUE_ONE		(1 << DS_OUT_VAL_SHIFT)
#define PIN_DS_PULLUD_ENABLE		(0 << DS_PULLUD_EN_SHIFT)
#define PIN_DS_PULLUD_DISABLE		(1 << DS_PULLUD_EN_SHIFT)
#define PIN_DS_PULL_DOWN		(0 << DS_PULLTYPE_SEL_SHIFT)
#define PIN_DS_PULL_UP			(1 << DS_PULLTYPE_SEL_SHIFT)

/* Default mux configuration for gpio-ranges to use with pinctrl */
#define PIN_GPIO_RANGE_IOPAD	(PIN_INPUT | 7)

#define AM62AX_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define AM62AX_MCU_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#define AM62PX_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define AM62PX_MCU_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#define AM62X_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define AM62X_MCU_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#define AM64X_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define AM64X_MCU_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#define AM65X_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define AM65X_WKUP_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#define J721E_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define J721E_WKUP_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#define J721S2_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define J721S2_WKUP_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#define J722S_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define J722S_MCU_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#define J784S4_IOPAD(pa, val, muxmode)		(((pa) & 0x1fff)) ((val) | (muxmode))
#define J784S4_WKUP_IOPAD(pa, val, muxmode)	(((pa) & 0x1fff)) ((val) | (muxmode))

#endif
