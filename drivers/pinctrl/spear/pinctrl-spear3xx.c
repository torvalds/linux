/*
 * Driver for the ST Microelectronics SPEAr3xx pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-spear3xx.h"

/* pins */
static const struct pinctrl_pin_desc spear3xx_pins[] = {
	SPEAR_PIN_0_TO_101,
};

/* firda_pins */
static const unsigned firda_pins[] = { 0, 1 };
static struct spear_muxreg firda_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_FIRDA_MASK,
		.val = PMX_FIRDA_MASK,
	},
};

static struct spear_modemux firda_modemux[] = {
	{
		.modes = ~0,
		.muxregs = firda_muxreg,
		.nmuxregs = ARRAY_SIZE(firda_muxreg),
	},
};

struct spear_pingroup spear3xx_firda_pingroup = {
	.name = "firda_grp",
	.pins = firda_pins,
	.npins = ARRAY_SIZE(firda_pins),
	.modemuxs = firda_modemux,
	.nmodemuxs = ARRAY_SIZE(firda_modemux),
};

static const char *const firda_grps[] = { "firda_grp" };
struct spear_function spear3xx_firda_function = {
	.name = "firda",
	.groups = firda_grps,
	.ngroups = ARRAY_SIZE(firda_grps),
};

/* i2c_pins */
static const unsigned i2c_pins[] = { 4, 5 };
static struct spear_muxreg i2c_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_I2C_MASK,
		.val = PMX_I2C_MASK,
	},
};

static struct spear_modemux i2c_modemux[] = {
	{
		.modes = ~0,
		.muxregs = i2c_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c_muxreg),
	},
};

struct spear_pingroup spear3xx_i2c_pingroup = {
	.name = "i2c0_grp",
	.pins = i2c_pins,
	.npins = ARRAY_SIZE(i2c_pins),
	.modemuxs = i2c_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c_modemux),
};

static const char *const i2c_grps[] = { "i2c0_grp" };
struct spear_function spear3xx_i2c_function = {
	.name = "i2c0",
	.groups = i2c_grps,
	.ngroups = ARRAY_SIZE(i2c_grps),
};

/* ssp_cs_pins */
static const unsigned ssp_cs_pins[] = { 34, 35, 36 };
static struct spear_muxreg ssp_cs_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_SSP_CS_MASK,
		.val = PMX_SSP_CS_MASK,
	},
};

static struct spear_modemux ssp_cs_modemux[] = {
	{
		.modes = ~0,
		.muxregs = ssp_cs_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp_cs_muxreg),
	},
};

struct spear_pingroup spear3xx_ssp_cs_pingroup = {
	.name = "ssp_cs_grp",
	.pins = ssp_cs_pins,
	.npins = ARRAY_SIZE(ssp_cs_pins),
	.modemuxs = ssp_cs_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp_cs_modemux),
};

static const char *const ssp_cs_grps[] = { "ssp_cs_grp" };
struct spear_function spear3xx_ssp_cs_function = {
	.name = "ssp_cs",
	.groups = ssp_cs_grps,
	.ngroups = ARRAY_SIZE(ssp_cs_grps),
};

/* ssp_pins */
static const unsigned ssp_pins[] = { 6, 7, 8, 9 };
static struct spear_muxreg ssp_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_SSP_MASK,
		.val = PMX_SSP_MASK,
	},
};

static struct spear_modemux ssp_modemux[] = {
	{
		.modes = ~0,
		.muxregs = ssp_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp_muxreg),
	},
};

struct spear_pingroup spear3xx_ssp_pingroup = {
	.name = "ssp0_grp",
	.pins = ssp_pins,
	.npins = ARRAY_SIZE(ssp_pins),
	.modemuxs = ssp_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp_modemux),
};

static const char *const ssp_grps[] = { "ssp0_grp" };
struct spear_function spear3xx_ssp_function = {
	.name = "ssp0",
	.groups = ssp_grps,
	.ngroups = ARRAY_SIZE(ssp_grps),
};

/* mii_pins */
static const unsigned mii_pins[] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27 };
static struct spear_muxreg mii_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_MII_MASK,
		.val = PMX_MII_MASK,
	},
};

static struct spear_modemux mii_modemux[] = {
	{
		.modes = ~0,
		.muxregs = mii_muxreg,
		.nmuxregs = ARRAY_SIZE(mii_muxreg),
	},
};

struct spear_pingroup spear3xx_mii_pingroup = {
	.name = "mii0_grp",
	.pins = mii_pins,
	.npins = ARRAY_SIZE(mii_pins),
	.modemuxs = mii_modemux,
	.nmodemuxs = ARRAY_SIZE(mii_modemux),
};

static const char *const mii_grps[] = { "mii0_grp" };
struct spear_function spear3xx_mii_function = {
	.name = "mii0",
	.groups = mii_grps,
	.ngroups = ARRAY_SIZE(mii_grps),
};

/* gpio0_pin0_pins */
static const unsigned gpio0_pin0_pins[] = { 28 };
static struct spear_muxreg gpio0_pin0_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_GPIO_PIN0_MASK,
		.val = PMX_GPIO_PIN0_MASK,
	},
};

static struct spear_modemux gpio0_pin0_modemux[] = {
	{
		.modes = ~0,
		.muxregs = gpio0_pin0_muxreg,
		.nmuxregs = ARRAY_SIZE(gpio0_pin0_muxreg),
	},
};

struct spear_pingroup spear3xx_gpio0_pin0_pingroup = {
	.name = "gpio0_pin0_grp",
	.pins = gpio0_pin0_pins,
	.npins = ARRAY_SIZE(gpio0_pin0_pins),
	.modemuxs = gpio0_pin0_modemux,
	.nmodemuxs = ARRAY_SIZE(gpio0_pin0_modemux),
};

/* gpio0_pin1_pins */
static const unsigned gpio0_pin1_pins[] = { 29 };
static struct spear_muxreg gpio0_pin1_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_GPIO_PIN1_MASK,
		.val = PMX_GPIO_PIN1_MASK,
	},
};

static struct spear_modemux gpio0_pin1_modemux[] = {
	{
		.modes = ~0,
		.muxregs = gpio0_pin1_muxreg,
		.nmuxregs = ARRAY_SIZE(gpio0_pin1_muxreg),
	},
};

struct spear_pingroup spear3xx_gpio0_pin1_pingroup = {
	.name = "gpio0_pin1_grp",
	.pins = gpio0_pin1_pins,
	.npins = ARRAY_SIZE(gpio0_pin1_pins),
	.modemuxs = gpio0_pin1_modemux,
	.nmodemuxs = ARRAY_SIZE(gpio0_pin1_modemux),
};

/* gpio0_pin2_pins */
static const unsigned gpio0_pin2_pins[] = { 30 };
static struct spear_muxreg gpio0_pin2_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_GPIO_PIN2_MASK,
		.val = PMX_GPIO_PIN2_MASK,
	},
};

static struct spear_modemux gpio0_pin2_modemux[] = {
	{
		.modes = ~0,
		.muxregs = gpio0_pin2_muxreg,
		.nmuxregs = ARRAY_SIZE(gpio0_pin2_muxreg),
	},
};

struct spear_pingroup spear3xx_gpio0_pin2_pingroup = {
	.name = "gpio0_pin2_grp",
	.pins = gpio0_pin2_pins,
	.npins = ARRAY_SIZE(gpio0_pin2_pins),
	.modemuxs = gpio0_pin2_modemux,
	.nmodemuxs = ARRAY_SIZE(gpio0_pin2_modemux),
};

/* gpio0_pin3_pins */
static const unsigned gpio0_pin3_pins[] = { 31 };
static struct spear_muxreg gpio0_pin3_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_GPIO_PIN3_MASK,
		.val = PMX_GPIO_PIN3_MASK,
	},
};

static struct spear_modemux gpio0_pin3_modemux[] = {
	{
		.modes = ~0,
		.muxregs = gpio0_pin3_muxreg,
		.nmuxregs = ARRAY_SIZE(gpio0_pin3_muxreg),
	},
};

struct spear_pingroup spear3xx_gpio0_pin3_pingroup = {
	.name = "gpio0_pin3_grp",
	.pins = gpio0_pin3_pins,
	.npins = ARRAY_SIZE(gpio0_pin3_pins),
	.modemuxs = gpio0_pin3_modemux,
	.nmodemuxs = ARRAY_SIZE(gpio0_pin3_modemux),
};

/* gpio0_pin4_pins */
static const unsigned gpio0_pin4_pins[] = { 32 };
static struct spear_muxreg gpio0_pin4_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_GPIO_PIN4_MASK,
		.val = PMX_GPIO_PIN4_MASK,
	},
};

static struct spear_modemux gpio0_pin4_modemux[] = {
	{
		.modes = ~0,
		.muxregs = gpio0_pin4_muxreg,
		.nmuxregs = ARRAY_SIZE(gpio0_pin4_muxreg),
	},
};

struct spear_pingroup spear3xx_gpio0_pin4_pingroup = {
	.name = "gpio0_pin4_grp",
	.pins = gpio0_pin4_pins,
	.npins = ARRAY_SIZE(gpio0_pin4_pins),
	.modemuxs = gpio0_pin4_modemux,
	.nmodemuxs = ARRAY_SIZE(gpio0_pin4_modemux),
};

/* gpio0_pin5_pins */
static const unsigned gpio0_pin5_pins[] = { 33 };
static struct spear_muxreg gpio0_pin5_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_GPIO_PIN5_MASK,
		.val = PMX_GPIO_PIN5_MASK,
	},
};

static struct spear_modemux gpio0_pin5_modemux[] = {
	{
		.modes = ~0,
		.muxregs = gpio0_pin5_muxreg,
		.nmuxregs = ARRAY_SIZE(gpio0_pin5_muxreg),
	},
};

struct spear_pingroup spear3xx_gpio0_pin5_pingroup = {
	.name = "gpio0_pin5_grp",
	.pins = gpio0_pin5_pins,
	.npins = ARRAY_SIZE(gpio0_pin5_pins),
	.modemuxs = gpio0_pin5_modemux,
	.nmodemuxs = ARRAY_SIZE(gpio0_pin5_modemux),
};

static const char *const gpio0_grps[] = { "gpio0_pin0_grp", "gpio0_pin1_grp",
	"gpio0_pin2_grp", "gpio0_pin3_grp", "gpio0_pin4_grp", "gpio0_pin5_grp",
};
struct spear_function spear3xx_gpio0_function = {
	.name = "gpio0",
	.groups = gpio0_grps,
	.ngroups = ARRAY_SIZE(gpio0_grps),
};

/* uart0_ext_pins */
static const unsigned uart0_ext_pins[] = { 37, 38, 39, 40, 41, 42 };
static struct spear_muxreg uart0_ext_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_UART0_MODEM_MASK,
		.val = PMX_UART0_MODEM_MASK,
	},
};

static struct spear_modemux uart0_ext_modemux[] = {
	{
		.modes = ~0,
		.muxregs = uart0_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(uart0_ext_muxreg),
	},
};

struct spear_pingroup spear3xx_uart0_ext_pingroup = {
	.name = "uart0_ext_grp",
	.pins = uart0_ext_pins,
	.npins = ARRAY_SIZE(uart0_ext_pins),
	.modemuxs = uart0_ext_modemux,
	.nmodemuxs = ARRAY_SIZE(uart0_ext_modemux),
};

static const char *const uart0_ext_grps[] = { "uart0_ext_grp" };
struct spear_function spear3xx_uart0_ext_function = {
	.name = "uart0_ext",
	.groups = uart0_ext_grps,
	.ngroups = ARRAY_SIZE(uart0_ext_grps),
};

/* uart0_pins */
static const unsigned uart0_pins[] = { 2, 3 };
static struct spear_muxreg uart0_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_UART0_MASK,
		.val = PMX_UART0_MASK,
	},
};

static struct spear_modemux uart0_modemux[] = {
	{
		.modes = ~0,
		.muxregs = uart0_muxreg,
		.nmuxregs = ARRAY_SIZE(uart0_muxreg),
	},
};

struct spear_pingroup spear3xx_uart0_pingroup = {
	.name = "uart0_grp",
	.pins = uart0_pins,
	.npins = ARRAY_SIZE(uart0_pins),
	.modemuxs = uart0_modemux,
	.nmodemuxs = ARRAY_SIZE(uart0_modemux),
};

static const char *const uart0_grps[] = { "uart0_grp" };
struct spear_function spear3xx_uart0_function = {
	.name = "uart0",
	.groups = uart0_grps,
	.ngroups = ARRAY_SIZE(uart0_grps),
};

/* timer_0_1_pins */
static const unsigned timer_0_1_pins[] = { 43, 44, 47, 48 };
static struct spear_muxreg timer_0_1_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_TIMER_0_1_MASK,
		.val = PMX_TIMER_0_1_MASK,
	},
};

static struct spear_modemux timer_0_1_modemux[] = {
	{
		.modes = ~0,
		.muxregs = timer_0_1_muxreg,
		.nmuxregs = ARRAY_SIZE(timer_0_1_muxreg),
	},
};

struct spear_pingroup spear3xx_timer_0_1_pingroup = {
	.name = "timer_0_1_grp",
	.pins = timer_0_1_pins,
	.npins = ARRAY_SIZE(timer_0_1_pins),
	.modemuxs = timer_0_1_modemux,
	.nmodemuxs = ARRAY_SIZE(timer_0_1_modemux),
};

static const char *const timer_0_1_grps[] = { "timer_0_1_grp" };
struct spear_function spear3xx_timer_0_1_function = {
	.name = "timer_0_1",
	.groups = timer_0_1_grps,
	.ngroups = ARRAY_SIZE(timer_0_1_grps),
};

/* timer_2_3_pins */
static const unsigned timer_2_3_pins[] = { 45, 46, 49, 50 };
static struct spear_muxreg timer_2_3_muxreg[] = {
	{
		.reg = -1,
		.mask = PMX_TIMER_2_3_MASK,
		.val = PMX_TIMER_2_3_MASK,
	},
};

static struct spear_modemux timer_2_3_modemux[] = {
	{
		.modes = ~0,
		.muxregs = timer_2_3_muxreg,
		.nmuxregs = ARRAY_SIZE(timer_2_3_muxreg),
	},
};

struct spear_pingroup spear3xx_timer_2_3_pingroup = {
	.name = "timer_2_3_grp",
	.pins = timer_2_3_pins,
	.npins = ARRAY_SIZE(timer_2_3_pins),
	.modemuxs = timer_2_3_modemux,
	.nmodemuxs = ARRAY_SIZE(timer_2_3_modemux),
};

static const char *const timer_2_3_grps[] = { "timer_2_3_grp" };
struct spear_function spear3xx_timer_2_3_function = {
	.name = "timer_2_3",
	.groups = timer_2_3_grps,
	.ngroups = ARRAY_SIZE(timer_2_3_grps),
};

struct spear_pinctrl_machdata spear3xx_machdata = {
	.pins = spear3xx_pins,
	.npins = ARRAY_SIZE(spear3xx_pins),
};
