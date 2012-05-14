/*
 * Driver for the ST Microelectronics SPEAr3xx pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-spear3xx.h"

/* pins */
static const struct pinctrl_pin_desc spear3xx_pins[] = {
	PINCTRL_PIN(0, "PLGPIO0"),
	PINCTRL_PIN(1, "PLGPIO1"),
	PINCTRL_PIN(2, "PLGPIO2"),
	PINCTRL_PIN(3, "PLGPIO3"),
	PINCTRL_PIN(4, "PLGPIO4"),
	PINCTRL_PIN(5, "PLGPIO5"),
	PINCTRL_PIN(6, "PLGPIO6"),
	PINCTRL_PIN(7, "PLGPIO7"),
	PINCTRL_PIN(8, "PLGPIO8"),
	PINCTRL_PIN(9, "PLGPIO9"),
	PINCTRL_PIN(10, "PLGPIO10"),
	PINCTRL_PIN(11, "PLGPIO11"),
	PINCTRL_PIN(12, "PLGPIO12"),
	PINCTRL_PIN(13, "PLGPIO13"),
	PINCTRL_PIN(14, "PLGPIO14"),
	PINCTRL_PIN(15, "PLGPIO15"),
	PINCTRL_PIN(16, "PLGPIO16"),
	PINCTRL_PIN(17, "PLGPIO17"),
	PINCTRL_PIN(18, "PLGPIO18"),
	PINCTRL_PIN(19, "PLGPIO19"),
	PINCTRL_PIN(20, "PLGPIO20"),
	PINCTRL_PIN(21, "PLGPIO21"),
	PINCTRL_PIN(22, "PLGPIO22"),
	PINCTRL_PIN(23, "PLGPIO23"),
	PINCTRL_PIN(24, "PLGPIO24"),
	PINCTRL_PIN(25, "PLGPIO25"),
	PINCTRL_PIN(26, "PLGPIO26"),
	PINCTRL_PIN(27, "PLGPIO27"),
	PINCTRL_PIN(28, "PLGPIO28"),
	PINCTRL_PIN(29, "PLGPIO29"),
	PINCTRL_PIN(30, "PLGPIO30"),
	PINCTRL_PIN(31, "PLGPIO31"),
	PINCTRL_PIN(32, "PLGPIO32"),
	PINCTRL_PIN(33, "PLGPIO33"),
	PINCTRL_PIN(34, "PLGPIO34"),
	PINCTRL_PIN(35, "PLGPIO35"),
	PINCTRL_PIN(36, "PLGPIO36"),
	PINCTRL_PIN(37, "PLGPIO37"),
	PINCTRL_PIN(38, "PLGPIO38"),
	PINCTRL_PIN(39, "PLGPIO39"),
	PINCTRL_PIN(40, "PLGPIO40"),
	PINCTRL_PIN(41, "PLGPIO41"),
	PINCTRL_PIN(42, "PLGPIO42"),
	PINCTRL_PIN(43, "PLGPIO43"),
	PINCTRL_PIN(44, "PLGPIO44"),
	PINCTRL_PIN(45, "PLGPIO45"),
	PINCTRL_PIN(46, "PLGPIO46"),
	PINCTRL_PIN(47, "PLGPIO47"),
	PINCTRL_PIN(48, "PLGPIO48"),
	PINCTRL_PIN(49, "PLGPIO49"),
	PINCTRL_PIN(50, "PLGPIO50"),
	PINCTRL_PIN(51, "PLGPIO51"),
	PINCTRL_PIN(52, "PLGPIO52"),
	PINCTRL_PIN(53, "PLGPIO53"),
	PINCTRL_PIN(54, "PLGPIO54"),
	PINCTRL_PIN(55, "PLGPIO55"),
	PINCTRL_PIN(56, "PLGPIO56"),
	PINCTRL_PIN(57, "PLGPIO57"),
	PINCTRL_PIN(58, "PLGPIO58"),
	PINCTRL_PIN(59, "PLGPIO59"),
	PINCTRL_PIN(60, "PLGPIO60"),
	PINCTRL_PIN(61, "PLGPIO61"),
	PINCTRL_PIN(62, "PLGPIO62"),
	PINCTRL_PIN(63, "PLGPIO63"),
	PINCTRL_PIN(64, "PLGPIO64"),
	PINCTRL_PIN(65, "PLGPIO65"),
	PINCTRL_PIN(66, "PLGPIO66"),
	PINCTRL_PIN(67, "PLGPIO67"),
	PINCTRL_PIN(68, "PLGPIO68"),
	PINCTRL_PIN(69, "PLGPIO69"),
	PINCTRL_PIN(70, "PLGPIO70"),
	PINCTRL_PIN(71, "PLGPIO71"),
	PINCTRL_PIN(72, "PLGPIO72"),
	PINCTRL_PIN(73, "PLGPIO73"),
	PINCTRL_PIN(74, "PLGPIO74"),
	PINCTRL_PIN(75, "PLGPIO75"),
	PINCTRL_PIN(76, "PLGPIO76"),
	PINCTRL_PIN(77, "PLGPIO77"),
	PINCTRL_PIN(78, "PLGPIO78"),
	PINCTRL_PIN(79, "PLGPIO79"),
	PINCTRL_PIN(80, "PLGPIO80"),
	PINCTRL_PIN(81, "PLGPIO81"),
	PINCTRL_PIN(82, "PLGPIO82"),
	PINCTRL_PIN(83, "PLGPIO83"),
	PINCTRL_PIN(84, "PLGPIO84"),
	PINCTRL_PIN(85, "PLGPIO85"),
	PINCTRL_PIN(86, "PLGPIO86"),
	PINCTRL_PIN(87, "PLGPIO87"),
	PINCTRL_PIN(88, "PLGPIO88"),
	PINCTRL_PIN(89, "PLGPIO89"),
	PINCTRL_PIN(90, "PLGPIO90"),
	PINCTRL_PIN(91, "PLGPIO91"),
	PINCTRL_PIN(92, "PLGPIO92"),
	PINCTRL_PIN(93, "PLGPIO93"),
	PINCTRL_PIN(94, "PLGPIO94"),
	PINCTRL_PIN(95, "PLGPIO95"),
	PINCTRL_PIN(96, "PLGPIO96"),
	PINCTRL_PIN(97, "PLGPIO97"),
	PINCTRL_PIN(98, "PLGPIO98"),
	PINCTRL_PIN(99, "PLGPIO99"),
	PINCTRL_PIN(100, "PLGPIO100"),
	PINCTRL_PIN(101, "PLGPIO101"),
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
