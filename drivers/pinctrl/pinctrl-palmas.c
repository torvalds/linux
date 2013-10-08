/*
 * pinctrl-palmas.c -- TI PALMAS series pin control driver.
 *
 * Copyright (c) 2013, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mfd/palmas.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#define PALMAS_PIN_GPIO0_ID				0
#define PALMAS_PIN_GPIO1_VBUS_LED1_PWM1			1
#define PALMAS_PIN_GPIO2_REGEN_LED2_PWM2		2
#define PALMAS_PIN_GPIO3_CHRG_DET			3
#define PALMAS_PIN_GPIO4_SYSEN1				4
#define PALMAS_PIN_GPIO5_CLK32KGAUDIO_USB_PSEL		5
#define PALMAS_PIN_GPIO6_SYSEN2				6
#define PALMAS_PIN_GPIO7_MSECURE_PWRHOLD		7
#define PALMAS_PIN_GPIO8_SIM1RSTI			8
#define PALMAS_PIN_GPIO9_LOW_VBAT			9
#define PALMAS_PIN_GPIO10_WIRELESS_CHRG1		10
#define PALMAS_PIN_GPIO11_RCM				11
#define PALMAS_PIN_GPIO12_SIM2RSTO			12
#define PALMAS_PIN_GPIO13				13
#define PALMAS_PIN_GPIO14				14
#define PALMAS_PIN_GPIO15_SIM2RSTI			15
#define PALMAS_PIN_VAC					16
#define PALMAS_PIN_POWERGOOD_USB_PSEL			17
#define PALMAS_PIN_NRESWARM				18
#define PALMAS_PIN_PWRDOWN				19
#define PALMAS_PIN_GPADC_START				20
#define PALMAS_PIN_RESET_IN				21
#define PALMAS_PIN_NSLEEP				22
#define PALMAS_PIN_ENABLE1				23
#define PALMAS_PIN_ENABLE2				24
#define PALMAS_PIN_INT					25
#define PALMAS_PIN_NUM					(PALMAS_PIN_INT + 1)

struct palmas_pin_function {
	const char *name;
	const char * const *groups;
	unsigned ngroups;
};

struct palmas_pctrl_chip_info {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct palmas *palmas;
	int pins_current_opt[PALMAS_PIN_NUM];
	const struct palmas_pin_function *functions;
	unsigned num_functions;
	const struct palmas_pingroup *pin_groups;
	int num_pin_groups;
	const struct pinctrl_pin_desc *pins;
	unsigned num_pins;
};

static const struct pinctrl_pin_desc palmas_pins_desc[] = {
	PINCTRL_PIN(PALMAS_PIN_GPIO0_ID, "gpio0"),
	PINCTRL_PIN(PALMAS_PIN_GPIO1_VBUS_LED1_PWM1, "gpio1"),
	PINCTRL_PIN(PALMAS_PIN_GPIO2_REGEN_LED2_PWM2, "gpio2"),
	PINCTRL_PIN(PALMAS_PIN_GPIO3_CHRG_DET, "gpio3"),
	PINCTRL_PIN(PALMAS_PIN_GPIO4_SYSEN1, "gpio4"),
	PINCTRL_PIN(PALMAS_PIN_GPIO5_CLK32KGAUDIO_USB_PSEL, "gpio5"),
	PINCTRL_PIN(PALMAS_PIN_GPIO6_SYSEN2, "gpio6"),
	PINCTRL_PIN(PALMAS_PIN_GPIO7_MSECURE_PWRHOLD, "gpio7"),
	PINCTRL_PIN(PALMAS_PIN_GPIO8_SIM1RSTI, "gpio8"),
	PINCTRL_PIN(PALMAS_PIN_GPIO9_LOW_VBAT, "gpio9"),
	PINCTRL_PIN(PALMAS_PIN_GPIO10_WIRELESS_CHRG1, "gpio10"),
	PINCTRL_PIN(PALMAS_PIN_GPIO11_RCM, "gpio11"),
	PINCTRL_PIN(PALMAS_PIN_GPIO12_SIM2RSTO, "gpio12"),
	PINCTRL_PIN(PALMAS_PIN_GPIO13, "gpio13"),
	PINCTRL_PIN(PALMAS_PIN_GPIO14, "gpio14"),
	PINCTRL_PIN(PALMAS_PIN_GPIO15_SIM2RSTI, "gpio15"),
	PINCTRL_PIN(PALMAS_PIN_VAC, "vac"),
	PINCTRL_PIN(PALMAS_PIN_POWERGOOD_USB_PSEL, "powergood"),
	PINCTRL_PIN(PALMAS_PIN_NRESWARM, "nreswarm"),
	PINCTRL_PIN(PALMAS_PIN_PWRDOWN, "pwrdown"),
	PINCTRL_PIN(PALMAS_PIN_GPADC_START, "gpadc_start"),
	PINCTRL_PIN(PALMAS_PIN_RESET_IN, "reset_in"),
	PINCTRL_PIN(PALMAS_PIN_NSLEEP, "nsleep"),
	PINCTRL_PIN(PALMAS_PIN_ENABLE1, "enable1"),
	PINCTRL_PIN(PALMAS_PIN_ENABLE2, "enable2"),
	PINCTRL_PIN(PALMAS_PIN_INT, "int"),
};

static const char * const opt0_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
	"gpio8",
	"gpio9",
	"gpio10",
	"gpio11",
	"gpio12",
	"gpio13",
	"gpio14",
	"gpio15",
	"vac",
	"powergood",
	"nreswarm",
	"pwrdown",
	"gpadc_start",
	"reset_in",
	"nsleep",
	"enable1",
	"enable2",
	"int",
};

static const char * const opt1_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
	"gpio8",
	"gpio9",
	"gpio10",
	"gpio11",
	"gpio12",
	"gpio15",
	"vac",
	"powergood",
};

static const char * const opt2_groups[] = {
	"gpio1",
	"gpio2",
	"gpio5",
	"gpio7",
};

static const char * const opt3_groups[] = {
	"gpio1",
	"gpio2",
};

static const char * const gpio_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
	"gpio8",
	"gpio9",
	"gpio10",
	"gpio11",
	"gpio12",
	"gpio13",
	"gpio14",
	"gpio15",
};

static const char * const led_groups[] = {
	"gpio1",
	"gpio2",
};

static const char * const pwm_groups[] = {
	"gpio1",
	"gpio2",
};

static const char * const regen_groups[] = {
	"gpio2",
};

static const char * const sysen_groups[] = {
	"gpio4",
	"gpio6",
};

static const char * const clk32kgaudio_groups[] = {
	"gpio5",
};

static const char * const id_groups[] = {
	"gpio0",
};

static const char * const vbus_det_groups[] = {
	"gpio1",
};

static const char * const chrg_det_groups[] = {
	"gpio3",
};

static const char * const vac_groups[] = {
	"vac",
};

static const char * const vacok_groups[] = {
	"vac",
};

static const char * const powergood_groups[] = {
	"powergood",
};

static const char * const usb_psel_groups[] = {
	"gpio5",
	"powergood",
};

static const char * const msecure_groups[] = {
	"gpio7",
};

static const char * const pwrhold_groups[] = {
	"gpio7",
};

static const char * const int_groups[] = {
	"int",
};

static const char * const nreswarm_groups[] = {
	"nreswarm",
};

static const char * const simrsto_groups[] = {
	"gpio12",
};

static const char * const simrsti_groups[] = {
	"gpio8",
	"gpio15",
};

static const char * const low_vbat_groups[] = {
	"gpio9",
};

static const char * const wireless_chrg1_groups[] = {
	"gpio10",
};

static const char * const rcm_groups[] = {
	"gpio11",
};

static const char * const pwrdown_groups[] = {
	"pwrdown",
};

static const char * const gpadc_start_groups[] = {
	"gpadc_start",
};

static const char * const reset_in_groups[] = {
	"reset_in",
};

static const char * const nsleep_groups[] = {
	"nsleep",
};

static const char * const enable_groups[] = {
	"enable1",
	"enable2",
};

#define FUNCTION_GROUPS					\
	FUNCTION_GROUP(opt0, OPTION0),			\
	FUNCTION_GROUP(opt1, OPTION1),			\
	FUNCTION_GROUP(opt2, OPTION2),			\
	FUNCTION_GROUP(opt3, OPTION3),			\
	FUNCTION_GROUP(gpio, GPIO),			\
	FUNCTION_GROUP(led, LED),			\
	FUNCTION_GROUP(pwm, PWM),			\
	FUNCTION_GROUP(regen, REGEN),			\
	FUNCTION_GROUP(sysen, SYSEN),			\
	FUNCTION_GROUP(clk32kgaudio, CLK32KGAUDIO),	\
	FUNCTION_GROUP(id, ID),				\
	FUNCTION_GROUP(vbus_det, VBUS_DET),		\
	FUNCTION_GROUP(chrg_det, CHRG_DET),		\
	FUNCTION_GROUP(vac, VAC),			\
	FUNCTION_GROUP(vacok, VACOK),			\
	FUNCTION_GROUP(powergood, POWERGOOD),		\
	FUNCTION_GROUP(usb_psel, USB_PSEL),		\
	FUNCTION_GROUP(msecure, MSECURE),		\
	FUNCTION_GROUP(pwrhold, PWRHOLD),		\
	FUNCTION_GROUP(int, INT),			\
	FUNCTION_GROUP(nreswarm, NRESWARM),		\
	FUNCTION_GROUP(simrsto, SIMRSTO),		\
	FUNCTION_GROUP(simrsti, SIMRSTI),		\
	FUNCTION_GROUP(low_vbat, LOW_VBAT),		\
	FUNCTION_GROUP(wireless_chrg1, WIRELESS_CHRG1),	\
	FUNCTION_GROUP(rcm, RCM),			\
	FUNCTION_GROUP(pwrdown, PWRDOWN),		\
	FUNCTION_GROUP(gpadc_start, GPADC_START),	\
	FUNCTION_GROUP(reset_in, RESET_IN),		\
	FUNCTION_GROUP(nsleep, NSLEEP),			\
	FUNCTION_GROUP(enable, ENABLE)

static const struct palmas_pin_function palmas_pin_function[] = {
#undef FUNCTION_GROUP
#define FUNCTION_GROUP(fname, mux)			\
	{						\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

	FUNCTION_GROUPS,
};

enum palmas_pinmux {
#undef FUNCTION_GROUP
#define FUNCTION_GROUP(fname, mux)	PALMAS_PINMUX_##mux
	FUNCTION_GROUPS,
	PALMAS_PINMUX_NA = 0xFFFF,
};

struct palmas_pins_pullup_dn_info {
	int pullup_dn_reg_base;
	int pullup_dn_reg_add;
	int pullup_dn_mask;
	int normal_val;
	int pull_up_val;
	int pull_dn_val;
};

struct palmas_pins_od_info {
	int od_reg_base;
	int od_reg_add;
	int od_mask;
	int od_enable;
	int od_disable;
};

struct palmas_pin_info {
	enum palmas_pinmux mux_opt;
	const struct palmas_pins_pullup_dn_info *pud_info;
	const struct palmas_pins_od_info *od_info;
};

struct palmas_pingroup {
	const char *name;
	const unsigned pins[1];
	unsigned npins;
	unsigned mux_reg_base;
	unsigned mux_reg_add;
	unsigned mux_reg_mask;
	unsigned mux_bit_shift;
	const struct palmas_pin_info *opt[4];
};

#define PULL_UP_DN(_name, _rbase, _add, _mask, _nv, _uv, _dv)		\
static const struct palmas_pins_pullup_dn_info pud_##_name##_info = {	\
	.pullup_dn_reg_base = PALMAS_##_rbase##_BASE,			\
	.pullup_dn_reg_add = _add,					\
	.pullup_dn_mask = _mask,					\
	.normal_val = _nv,						\
	.pull_up_val = _uv,						\
	.pull_dn_val = _dv,						\
}

PULL_UP_DN(nreswarm,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL1,	0x2,	0x0,	0x2,	-1);
PULL_UP_DN(pwrdown,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL1,	0x4,	0x0,	-1,	0x4);
PULL_UP_DN(gpadc_start,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL1,	0x30,	0x0,	0x20,	0x10);
PULL_UP_DN(reset_in,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL1,	0x40,	0x0,	-1,	0x40);
PULL_UP_DN(nsleep,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL2,	0x3,	0x0,	0x2,	0x1);
PULL_UP_DN(enable1,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL2,	0xC,	0x0,	0x8,	0x4);
PULL_UP_DN(enable2,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL2,	0x30,	0x0,	0x20,	0x10);
PULL_UP_DN(vacok,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL3,	0x40,	0x0,	-1,	0x40);
PULL_UP_DN(chrg_det,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL3,	0x10,	0x0,	-1,	0x10);
PULL_UP_DN(pwrhold,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL3,	0x4,	0x0,	-1,	0x4);
PULL_UP_DN(msecure,	PU_PD_OD,	PALMAS_PU_PD_INPUT_CTRL3,	0x1,	0x0,	-1,	0x1);
PULL_UP_DN(id,		USB_OTG,	PALMAS_USB_ID_CTRL_SET,		0x40,	0x0,	0x40,	-1);
PULL_UP_DN(gpio0,	GPIO,		PALMAS_PU_PD_GPIO_CTRL1,	0x04,	0,	-1,	1);
PULL_UP_DN(gpio1,	GPIO,		PALMAS_PU_PD_GPIO_CTRL1,	0x0C,	0,	0x8,	0x4);
PULL_UP_DN(gpio2,	GPIO,		PALMAS_PU_PD_GPIO_CTRL1,	0x30,	0x0,	0x20,	0x10);
PULL_UP_DN(gpio3,	GPIO,		PALMAS_PU_PD_GPIO_CTRL1,	0x40,	0x0,	-1,	0x40);
PULL_UP_DN(gpio4,	GPIO,		PALMAS_PU_PD_GPIO_CTRL2,	0x03,	0x0,	0x2,	0x1);
PULL_UP_DN(gpio5,	GPIO,		PALMAS_PU_PD_GPIO_CTRL2,	0x0c,	0x0,	0x8,	0x4);
PULL_UP_DN(gpio6,	GPIO,		PALMAS_PU_PD_GPIO_CTRL2,	0x30,	0x0,	0x20,	0x10);
PULL_UP_DN(gpio7,	GPIO,		PALMAS_PU_PD_GPIO_CTRL2,	0x40,	0x0,	-1,	0x40);
PULL_UP_DN(gpio9,	GPIO,		PALMAS_PU_PD_GPIO_CTRL3,	0x0C,	0x0,	0x8,	0x4);
PULL_UP_DN(gpio10,	GPIO,		PALMAS_PU_PD_GPIO_CTRL3,	0x30,	0x0,	0x20,	0x10);
PULL_UP_DN(gpio11,	GPIO,		PALMAS_PU_PD_GPIO_CTRL3,	0xC0,	0x0,	0x80,	0x40);
PULL_UP_DN(gpio13,	GPIO,		PALMAS_PU_PD_GPIO_CTRL4,	0x04,	0x0,	-1,	0x04);
PULL_UP_DN(gpio14,	GPIO,		PALMAS_PU_PD_GPIO_CTRL4,	0x30,	0x0,	0x20,	0x10);

#define OD_INFO(_name, _rbase, _add, _mask, _ev, _dv)		\
static const struct palmas_pins_od_info od_##_name##_info = {	\
	.od_reg_base = PALMAS_##_rbase##_BASE,			\
	.od_reg_add = _add,					\
	.od_mask = _mask,					\
	.od_enable = _ev,					\
	.od_disable = _dv,					\
}

OD_INFO(gpio1,	GPIO,	PALMAS_OD_OUTPUT_GPIO_CTRL,	0x1,	0x1,	0x0);
OD_INFO(gpio2,	GPIO,	PALMAS_OD_OUTPUT_GPIO_CTRL,	0x2,	0x2,	0x0);
OD_INFO(gpio5,	GPIO,	PALMAS_OD_OUTPUT_GPIO_CTRL,	0x20,	0x20,	0x0);
OD_INFO(gpio10,	GPIO,	PALMAS_OD_OUTPUT_GPIO_CTRL2,	0x04,	0x04,	0x0);
OD_INFO(gpio13,	GPIO,	PALMAS_OD_OUTPUT_GPIO_CTRL2,	0x20,	0x20,	0x0);
OD_INFO(int,		PU_PD_OD,	PALMAS_OD_OUTPUT_CTRL,	0x8,	0x8,	0x0);
OD_INFO(pwm1,		PU_PD_OD,	PALMAS_OD_OUTPUT_CTRL,	0x20,	0x20,	0x0);
OD_INFO(pwm2,		PU_PD_OD,	PALMAS_OD_OUTPUT_CTRL,	0x80,	0x80,	0x0);
OD_INFO(vbus_det,	PU_PD_OD,	PALMAS_OD_OUTPUT_CTRL,	0x40,	0x40,	0x0);

#define PIN_INFO(_name, _id, _pud_info, _od_info)		\
static const struct palmas_pin_info pin_##_name##_info = {	\
	.mux_opt = PALMAS_PINMUX_##_id,				\
	.pud_info = _pud_info,					\
	.od_info = _od_info					\
}

PIN_INFO(gpio0,		GPIO,		&pud_gpio0_info,	NULL);
PIN_INFO(gpio1,		GPIO,		&pud_gpio1_info,	&od_gpio1_info);
PIN_INFO(gpio2,		GPIO,		&pud_gpio2_info,	&od_gpio2_info);
PIN_INFO(gpio3,		GPIO,		&pud_gpio3_info,	NULL);
PIN_INFO(gpio4,		GPIO,		&pud_gpio4_info,	NULL);
PIN_INFO(gpio5,		GPIO,		&pud_gpio5_info,	&od_gpio5_info);
PIN_INFO(gpio6,		GPIO,		&pud_gpio6_info,	NULL);
PIN_INFO(gpio7,		GPIO,		&pud_gpio7_info,	NULL);
PIN_INFO(gpio8,		GPIO,		NULL,			NULL);
PIN_INFO(gpio9,		GPIO,		&pud_gpio9_info,	NULL);
PIN_INFO(gpio10,	GPIO,		&pud_gpio10_info,	&od_gpio10_info);
PIN_INFO(gpio11,	GPIO,		&pud_gpio11_info,	NULL);
PIN_INFO(gpio12,	GPIO,		NULL,			NULL);
PIN_INFO(gpio13,	GPIO,		&pud_gpio13_info,	&od_gpio13_info);
PIN_INFO(gpio14,	GPIO,		&pud_gpio14_info,	NULL);
PIN_INFO(gpio15,	GPIO,		NULL,			NULL);
PIN_INFO(id,		ID,		&pud_id_info,		NULL);
PIN_INFO(led1,		LED,		NULL,			NULL);
PIN_INFO(led2,		LED,		NULL,			NULL);
PIN_INFO(regen,		REGEN,		NULL,			NULL);
PIN_INFO(sysen1,	SYSEN,		NULL,			NULL);
PIN_INFO(sysen2,	SYSEN,		NULL,			NULL);
PIN_INFO(int,		INT,		NULL,			&od_int_info);
PIN_INFO(pwm1,		PWM,		NULL,			&od_pwm1_info);
PIN_INFO(pwm2,		PWM,		NULL,			&od_pwm2_info);
PIN_INFO(vacok,		VACOK,		&pud_vacok_info,	NULL);
PIN_INFO(chrg_det,	CHRG_DET,	&pud_chrg_det_info,	NULL);
PIN_INFO(pwrhold,	PWRHOLD,	&pud_pwrhold_info,	NULL);
PIN_INFO(msecure,	MSECURE,	&pud_msecure_info,	NULL);
PIN_INFO(nreswarm,	NA,		&pud_nreswarm_info,	NULL);
PIN_INFO(pwrdown,	NA,		&pud_pwrdown_info,	NULL);
PIN_INFO(gpadc_start,	NA,		&pud_gpadc_start_info,	NULL);
PIN_INFO(reset_in,	NA,		&pud_reset_in_info,	NULL);
PIN_INFO(nsleep,	NA,		&pud_nsleep_info,	NULL);
PIN_INFO(enable1,	NA,		&pud_enable1_info,	NULL);
PIN_INFO(enable2,	NA,		&pud_enable2_info,	NULL);
PIN_INFO(clk32kgaudio,	CLK32KGAUDIO,	NULL,			NULL);
PIN_INFO(usb_psel,	USB_PSEL,	NULL,			NULL);
PIN_INFO(vac,		VAC,		NULL,			NULL);
PIN_INFO(powergood,	POWERGOOD,	NULL,			NULL);
PIN_INFO(vbus_det,	VBUS_DET,	NULL,			&od_vbus_det_info);
PIN_INFO(sim1rsti,	SIMRSTI,	NULL,			NULL);
PIN_INFO(low_vbat,	LOW_VBAT,	NULL,			NULL);
PIN_INFO(rcm,		RCM,		NULL,			NULL);
PIN_INFO(sim2rsto,	SIMRSTO,	NULL,			NULL);
PIN_INFO(sim2rsti,	SIMRSTI,	NULL,			NULL);
PIN_INFO(wireless_chrg1,	WIRELESS_CHRG1,	NULL,		NULL);

#define PALMAS_PRIMARY_SECONDARY_NONE	0
#define PALMAS_NONE_BASE		0
#define PALMAS_PRIMARY_SECONDARY_INPUT3 PALMAS_PU_PD_INPUT_CTRL3

#define PALMAS_PINGROUP(pg_name, pin_id, base, reg, _mask, _bshift, o0, o1, o2, o3)  \
	{								\
		.name = #pg_name,					\
		.pins = {PALMAS_PIN_##pin_id},				\
		.npins = 1,						\
		.mux_reg_base = PALMAS_##base##_BASE,			\
		.mux_reg_add = PALMAS_PRIMARY_SECONDARY_##reg,		\
		.mux_reg_mask = _mask,					\
		.mux_bit_shift = _bshift,				\
		.opt = {						\
			o0,						\
			o1,						\
			o2,						\
			o3,						\
		},							\
	}

static const struct palmas_pingroup tps65913_pingroups[] = {
	PALMAS_PINGROUP(gpio0,	GPIO0_ID,			PU_PD_OD,	PAD1,	0x4,	0x2,	&pin_gpio0_info,	&pin_id_info,		NULL,		NULL),
	PALMAS_PINGROUP(gpio1,	GPIO1_VBUS_LED1_PWM1,		PU_PD_OD,	PAD1,	0x18,	0x3,	&pin_gpio1_info,	&pin_vbus_det_info,	&pin_led1_info,	&pin_pwm1_info),
	PALMAS_PINGROUP(gpio2,	GPIO2_REGEN_LED2_PWM2,		PU_PD_OD,	PAD1,	0x60,	0x5,	&pin_gpio2_info,	&pin_regen_info,	&pin_led2_info,	&pin_pwm2_info),
	PALMAS_PINGROUP(gpio3,	GPIO3_CHRG_DET,			PU_PD_OD,	PAD1,	0x80,	0x7,	&pin_gpio3_info,	&pin_chrg_det_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio4,	GPIO4_SYSEN1,			PU_PD_OD,	PAD1,	0x01,	0x0,	&pin_gpio4_info,	&pin_sysen1_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio5,	GPIO5_CLK32KGAUDIO_USB_PSEL,	PU_PD_OD,	PAD2,	0x6,	0x1,	&pin_gpio5_info,	&pin_clk32kgaudio_info,	&pin_usb_psel_info,	NULL),
	PALMAS_PINGROUP(gpio6,	GPIO6_SYSEN2,			PU_PD_OD,	PAD2,	0x08,	0x3,	&pin_gpio6_info,	&pin_sysen2_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio7,	GPIO7_MSECURE_PWRHOLD,		PU_PD_OD,	PAD2,	0x30,	0x4,	&pin_gpio7_info,	&pin_msecure_info,	&pin_pwrhold_info,	NULL),
	PALMAS_PINGROUP(vac,	VAC,				PU_PD_OD,	PAD1,	0x02,	0x1,	&pin_vac_info,		&pin_vacok_info,	NULL,		NULL),
	PALMAS_PINGROUP(powergood,	POWERGOOD_USB_PSEL,	PU_PD_OD,	PAD1,	0x01,	0x0,	&pin_powergood_info,	&pin_usb_psel_info,	NULL,	NULL),
	PALMAS_PINGROUP(nreswarm,	NRESWARM,		NONE,		NONE,	0x0,	0x0,	&pin_nreswarm_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(pwrdown,	PWRDOWN,		NONE,		NONE,	0x0,	0x0,	&pin_pwrdown_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(gpadc_start,	GPADC_START,		NONE,		NONE,	0x0,	0x0,	&pin_gpadc_start_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(reset_in,	RESET_IN,		NONE,		NONE,	0x0,	0x0,	&pin_reset_in_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(nsleep,		NSLEEP,			NONE,		NONE,	0x0,	0x0,	&pin_nsleep_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(enable1,	ENABLE1,		NONE,		NONE,	0x0,	0x0,	&pin_enable1_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(enable2,	ENABLE2,		NONE,		NONE,	0x0,	0x0,	&pin_enable2_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(int,		INT,			NONE,		NONE,	0x0,	0x0,	&pin_int_info,		NULL,			NULL,		NULL),
};

static const struct palmas_pingroup tps80036_pingroups[] = {
	PALMAS_PINGROUP(gpio0,	GPIO0_ID,			PU_PD_OD,	PAD1,	0x4,	0x2,	&pin_gpio0_info,	&pin_id_info,		NULL,		NULL),
	PALMAS_PINGROUP(gpio1,	GPIO1_VBUS_LED1_PWM1,		PU_PD_OD,	PAD1,	0x18,	0x3,	&pin_gpio1_info,	&pin_vbus_det_info,	&pin_led1_info,	&pin_pwm1_info),
	PALMAS_PINGROUP(gpio2,	GPIO2_REGEN_LED2_PWM2,		PU_PD_OD,	PAD1,	0x60,	0x5,	&pin_gpio2_info,	&pin_regen_info,	&pin_led2_info,	&pin_pwm2_info),
	PALMAS_PINGROUP(gpio3,	GPIO3_CHRG_DET,			PU_PD_OD,	PAD1,	0x80,	0x7,	&pin_gpio3_info,	&pin_chrg_det_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio4,	GPIO4_SYSEN1,			PU_PD_OD,	PAD1,	0x01,	0x0,	&pin_gpio4_info,	&pin_sysen1_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio5,	GPIO5_CLK32KGAUDIO_USB_PSEL,	PU_PD_OD,	PAD2,	0x6,	0x1,	&pin_gpio5_info,	&pin_clk32kgaudio_info,	&pin_usb_psel_info,	NULL),
	PALMAS_PINGROUP(gpio6,	GPIO6_SYSEN2,			PU_PD_OD,	PAD2,	0x08,	0x3,	&pin_gpio6_info,	&pin_sysen2_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio7,	GPIO7_MSECURE_PWRHOLD,		PU_PD_OD,	PAD2,	0x30,	0x4,	&pin_gpio7_info,	&pin_msecure_info,	&pin_pwrhold_info,	NULL),
	PALMAS_PINGROUP(gpio8,	GPIO8_SIM1RSTI,			PU_PD_OD,	PAD4,	0x01,	0x0,	&pin_gpio8_info,	&pin_sim1rsti_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio9,	GPIO9_LOW_VBAT,			PU_PD_OD,	PAD4,	0x02,	0x1,	&pin_gpio9_info,	&pin_low_vbat_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio10,	GPIO10_WIRELESS_CHRG1,		PU_PD_OD,	PAD4,	0x04,	0x2,	&pin_gpio10_info,	&pin_wireless_chrg1_info,	NULL,	NULL),
	PALMAS_PINGROUP(gpio11,	GPIO11_RCM,			PU_PD_OD,	PAD4,	0x08,	0x3,	&pin_gpio11_info,	&pin_rcm_info,		NULL,		NULL),
	PALMAS_PINGROUP(gpio12,	GPIO12_SIM2RSTO,		PU_PD_OD,	PAD4,	0x10,	0x4,	&pin_gpio12_info,	&pin_sim2rsto_info,	NULL,		NULL),
	PALMAS_PINGROUP(gpio13,	GPIO13,				NONE,		NONE,	0x00,	0x0,	&pin_gpio13_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(gpio14,	GPIO14,				NONE,		NONE,	0x00,	0x0,	&pin_gpio14_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(gpio15,	GPIO15_SIM2RSTI,		PU_PD_OD,	PAD4,	0x80,	0x7,	&pin_gpio15_info,	&pin_sim2rsti_info,	NULL,		NULL),
	PALMAS_PINGROUP(vac,	VAC,				PU_PD_OD,	PAD1,	0x02,	0x1,	&pin_vac_info,		&pin_vacok_info,	NULL,		NULL),
	PALMAS_PINGROUP(powergood,	POWERGOOD_USB_PSEL,	PU_PD_OD,	PAD1,	0x01,	0x0,	&pin_powergood_info,	&pin_usb_psel_info,	NULL,	NULL),
	PALMAS_PINGROUP(nreswarm,	NRESWARM,		NONE,		NONE,	0x0,	0x0,	&pin_nreswarm_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(pwrdown,	PWRDOWN,		NONE,		NONE,	0x0,	0x0,	&pin_pwrdown_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(gpadc_start,	GPADC_START,		NONE,		NONE,	0x0,	0x0,	&pin_gpadc_start_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(reset_in,	RESET_IN,		NONE,		NONE,	0x0,	0x0,	&pin_reset_in_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(nsleep,		NSLEEP,			NONE,		NONE,	0x0,	0x0,	&pin_nsleep_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(enable1,	ENABLE1,		NONE,		NONE,	0x0,	0x0,	&pin_enable1_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(enable2,	ENABLE2,		NONE,		NONE,	0x0,	0x0,	&pin_enable2_info,	NULL,			NULL,		NULL),
	PALMAS_PINGROUP(int,		INT,			NONE,		NONE,	0x0,	0x0,	&pin_int_info,		NULL,			NULL,		NULL),
};

static int palmas_pinctrl_get_pin_mux(struct palmas_pctrl_chip_info *pci)
{
	const struct palmas_pingroup *g;
	unsigned int val;
	int ret;
	int i;

	for (i = 0; i < pci->num_pin_groups; ++i) {
		g = &pci->pin_groups[i];
		if (g->mux_reg_base == PALMAS_NONE_BASE) {
			pci->pins_current_opt[i] = 0;
			continue;
		}
		ret = palmas_read(pci->palmas, g->mux_reg_base,
				g->mux_reg_add, &val);
		if (ret < 0) {
			dev_err(pci->dev, "mux_reg 0x%02x read failed: %d\n",
					g->mux_reg_add, ret);
			return ret;
		}
		val &= g->mux_reg_mask;
		pci->pins_current_opt[i] = val >> g->mux_bit_shift;
	}
	return 0;
}

static int palmas_pinctrl_set_dvfs1(struct palmas_pctrl_chip_info *pci,
		bool enable)
{
	int ret;
	int val;

	val = enable ? PALMAS_PRIMARY_SECONDARY_PAD3_DVFS1 : 0;
	ret = palmas_update_bits(pci->palmas, PALMAS_PU_PD_OD_BASE,
			PALMAS_PRIMARY_SECONDARY_PAD3,
			PALMAS_PRIMARY_SECONDARY_PAD3_DVFS1, val);
	if (ret < 0)
		dev_err(pci->dev, "SECONDARY_PAD3 update failed %d\n", ret);
	return ret;
}

static int palmas_pinctrl_set_dvfs2(struct palmas_pctrl_chip_info *pci,
		bool enable)
{
	int ret;
	int val;

	val = enable ? PALMAS_PRIMARY_SECONDARY_PAD3_DVFS2 : 0;
	ret = palmas_update_bits(pci->palmas, PALMAS_PU_PD_OD_BASE,
			PALMAS_PRIMARY_SECONDARY_PAD3,
			PALMAS_PRIMARY_SECONDARY_PAD3_DVFS2, val);
	if (ret < 0)
		dev_err(pci->dev, "SECONDARY_PAD3 update failed %d\n", ret);
	return ret;
}

static int palmas_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->num_pin_groups;
}

static const char *palmas_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
		unsigned group)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->pin_groups[group].name;
}

static int palmas_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned group, const unsigned **pins, unsigned *num_pins)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);

	*pins = pci->pin_groups[group].pins;
	*num_pins = pci->pin_groups[group].npins;
	return 0;
}

static const struct pinctrl_ops palmas_pinctrl_ops = {
	.get_groups_count = palmas_pinctrl_get_groups_count,
	.get_group_name = palmas_pinctrl_get_group_name,
	.get_group_pins = palmas_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_dt_free_map,
};

static int palmas_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->num_functions;
}

static const char *palmas_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
			unsigned function)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->functions[function].name;
}

static int palmas_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
		unsigned function, const char * const **groups,
		unsigned * const num_groups)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);

	*groups = pci->functions[function].groups;
	*num_groups = pci->functions[function].ngroups;
	return 0;
}

static int palmas_pinctrl_enable(struct pinctrl_dev *pctldev, unsigned function,
		unsigned group)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);
	const struct palmas_pingroup *g;
	int i;
	int ret;

	g = &pci->pin_groups[group];

	/* If direct option is provided here */
	if (function <= PALMAS_PINMUX_OPTION3) {
		if (!g->opt[function]) {
			dev_err(pci->dev, "Pin %s does not support option %d\n",
				g->name, function);
			return -EINVAL;
		}
		i = function;
	} else {
		for (i = 0; i < ARRAY_SIZE(g->opt); i++) {
			if (!g->opt[i])
				continue;
			if (g->opt[i]->mux_opt == function)
				break;
		}
		if (WARN_ON(i == ARRAY_SIZE(g->opt))) {
			dev_err(pci->dev, "Pin %s does not support option %d\n",
				g->name, function);
			return -EINVAL;
		}
	}

	if (g->mux_reg_base == PALMAS_NONE_BASE) {
		if (WARN_ON(i != 0))
			return -EINVAL;
		return 0;
	}

	dev_dbg(pci->dev, "%s(): Base0x%02x:0x%02x:0x%02x:0x%02x\n",
			__func__, g->mux_reg_base, g->mux_reg_add,
			g->mux_reg_mask, i << g->mux_bit_shift);

	ret = palmas_update_bits(pci->palmas, g->mux_reg_base, g->mux_reg_add,
			g->mux_reg_mask, i << g->mux_bit_shift);
	if (ret < 0) {
		dev_err(pci->dev, "Reg 0x%02x update failed: %d\n",
				g->mux_reg_add, ret);
		return ret;
	}
	pci->pins_current_opt[group] = i;
	return 0;
}

static const struct pinmux_ops palmas_pinmux_ops = {
	.get_functions_count = palmas_pinctrl_get_funcs_count,
	.get_function_name = palmas_pinctrl_get_func_name,
	.get_function_groups = palmas_pinctrl_get_func_groups,
	.enable = palmas_pinctrl_enable,
};

static int palmas_pinconf_get(struct pinctrl_dev *pctldev,
			unsigned pin, unsigned long *config)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	const struct palmas_pingroup *g;
	const struct palmas_pin_info *opt;
	unsigned int val;
	int ret;
	int base, add;
	int rval;
	int arg;
	int group_nr;

	for (group_nr = 0; group_nr < pci->num_pin_groups; ++group_nr) {
		if (pci->pin_groups[group_nr].pins[0] == pin)
			break;
	}

	if (group_nr == pci->num_pin_groups) {
		dev_err(pci->dev,
			"Pinconf is not supported for pin-id %d\n", pin);
		return -ENOTSUPP;
	}

	g = &pci->pin_groups[group_nr];
	opt = g->opt[pci->pins_current_opt[group_nr]];
	if (!opt) {
		dev_err(pci->dev,
			"Pinconf is not supported for pin %s\n", g->name);
		return -ENOTSUPP;
	}

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!opt->pud_info) {
			dev_err(pci->dev,
				"PULL control not supported for pin %s\n",
				g->name);
			return -ENOTSUPP;
		}
		base = opt->pud_info->pullup_dn_reg_base;
		add = opt->pud_info->pullup_dn_reg_add;
		ret = palmas_read(pci->palmas, base, add, &val);
		if (ret < 0) {
			dev_err(pci->dev, "Reg 0x%02x read failed: %d\n",
				add, ret);
			return ret;
		}

		rval = val & opt->pud_info->pullup_dn_mask;
		arg = 0;
		if ((opt->pud_info->normal_val >= 0) &&
				(opt->pud_info->normal_val == rval) &&
				(param == PIN_CONFIG_BIAS_DISABLE))
			arg = 1;
		else if ((opt->pud_info->pull_up_val >= 0) &&
				(opt->pud_info->pull_up_val == rval) &&
				(param == PIN_CONFIG_BIAS_PULL_UP))
			arg = 1;
		else if ((opt->pud_info->pull_dn_val >= 0) &&
				(opt->pud_info->pull_dn_val == rval) &&
				(param == PIN_CONFIG_BIAS_PULL_DOWN))
			arg = 1;
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!opt->od_info) {
			dev_err(pci->dev,
				"OD control not supported for pin %s\n",
				g->name);
			return -ENOTSUPP;
		}
		base = opt->od_info->od_reg_base;
		add = opt->od_info->od_reg_add;
		ret = palmas_read(pci->palmas, base, add, &val);
		if (ret < 0) {
			dev_err(pci->dev, "Reg 0x%02x read failed: %d\n",
				add, ret);
			return ret;
		}
		rval = val & opt->od_info->od_mask;
		arg = -1;
		if ((opt->od_info->od_disable >= 0) &&
				(opt->od_info->od_disable == rval))
			arg = 0;
		else if ((opt->od_info->od_enable >= 0) &&
					(opt->od_info->od_enable == rval))
			arg = 1;
		if (arg < 0) {
			dev_err(pci->dev,
				"OD control not supported for pin %s\n",
				g->name);
			return -ENOTSUPP;
		}
		break;

	default:
		dev_err(pci->dev, "Properties not supported\n");
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, (u16)arg);
	return 0;
}

static int palmas_pinconf_set(struct pinctrl_dev *pctldev,
			unsigned pin, unsigned long *configs,
			unsigned num_configs)
{
	struct palmas_pctrl_chip_info *pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u16 param_val;
	const struct palmas_pingroup *g;
	const struct palmas_pin_info *opt;
	int ret;
	int base, add, mask;
	int rval;
	int group_nr;
	int i;

	for (group_nr = 0; group_nr < pci->num_pin_groups; ++group_nr) {
		if (pci->pin_groups[group_nr].pins[0] == pin)
			break;
	}

	if (group_nr == pci->num_pin_groups) {
		dev_err(pci->dev,
			"Pinconf is not supported for pin-id %d\n", pin);
		return -ENOTSUPP;
	}

	g = &pci->pin_groups[group_nr];
	opt = g->opt[pci->pins_current_opt[group_nr]];
	if (!opt) {
		dev_err(pci->dev,
			"Pinconf is not supported for pin %s\n", g->name);
		return -ENOTSUPP;
	}

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		param_val = pinconf_to_config_argument(configs[i]);

		if (param == PIN_CONFIG_BIAS_PULL_PIN_DEFAULT)
			continue;

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (!opt->pud_info) {
				dev_err(pci->dev,
					"PULL control not supported for pin %s\n",
					g->name);
				return -ENOTSUPP;
			}
			base = opt->pud_info->pullup_dn_reg_base;
			add = opt->pud_info->pullup_dn_reg_add;
			mask = opt->pud_info->pullup_dn_mask;

			if (param == PIN_CONFIG_BIAS_DISABLE)
				rval = opt->pud_info->normal_val;
			else if (param == PIN_CONFIG_BIAS_PULL_UP)
				rval = opt->pud_info->pull_up_val;
			else
				rval = opt->pud_info->pull_dn_val;

			if (rval < 0) {
				dev_err(pci->dev,
					"PULL control not supported for pin %s\n",
					g->name);
				return -ENOTSUPP;
			}
			break;

		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			if (!opt->od_info) {
				dev_err(pci->dev,
					"OD control not supported for pin %s\n",
					g->name);
				return -ENOTSUPP;
			}
			base = opt->od_info->od_reg_base;
			add = opt->od_info->od_reg_add;
			mask = opt->od_info->od_mask;
			if (param_val == 0)
				rval = opt->od_info->od_disable;
			else
				rval = opt->od_info->od_enable;
			if (rval < 0) {
				dev_err(pci->dev,
					"OD control not supported for pin %s\n",
					g->name);
				return -ENOTSUPP;
			}
			break;
		default:
			dev_err(pci->dev, "Properties not supported\n");
			return -ENOTSUPP;
		}

		dev_dbg(pci->dev, "%s(): Add0x%02x:0x%02x:0x%02x:0x%02x\n",
				__func__, base, add, mask, rval);
		ret = palmas_update_bits(pci->palmas, base, add, mask, rval);
		if (ret < 0) {
			dev_err(pci->dev, "Reg 0x%02x update failed: %d\n",
				add, ret);
			return ret;
		}
	} /* for each config */

	return 0;
}

static const struct pinconf_ops palmas_pinconf_ops = {
	.pin_config_get = palmas_pinconf_get,
	.pin_config_set = palmas_pinconf_set,
};

static struct pinctrl_desc palmas_pinctrl_desc = {
	.pctlops = &palmas_pinctrl_ops,
	.pmxops = &palmas_pinmux_ops,
	.confops = &palmas_pinconf_ops,
	.owner = THIS_MODULE,
};

struct palmas_pinctrl_data {
	const struct palmas_pingroup *pin_groups;
	int num_pin_groups;
};

static struct palmas_pinctrl_data tps65913_pinctrl_data = {
	.pin_groups = tps65913_pingroups,
	.num_pin_groups = ARRAY_SIZE(tps65913_pingroups),
};

static struct palmas_pinctrl_data tps80036_pinctrl_data = {
	.pin_groups = tps80036_pingroups,
	.num_pin_groups = ARRAY_SIZE(tps80036_pingroups),
};

static struct of_device_id palmas_pinctrl_of_match[] = {
	{ .compatible = "ti,palmas-pinctrl", .data = &tps65913_pinctrl_data},
	{ .compatible = "ti,tps65913-pinctrl", .data = &tps65913_pinctrl_data},
	{ .compatible = "ti,tps80036-pinctrl", .data = &tps80036_pinctrl_data},
	{ },
};
MODULE_DEVICE_TABLE(of, palmas_pinctrl_of_match);

static int palmas_pinctrl_probe(struct platform_device *pdev)
{
	struct palmas_pctrl_chip_info *pci;
	const struct palmas_pinctrl_data *pinctrl_data = &tps65913_pinctrl_data;
	int ret;
	bool enable_dvfs1 = false;
	bool enable_dvfs2 = false;

	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_device(palmas_pinctrl_of_match, &pdev->dev);
		pinctrl_data = match->data;
		enable_dvfs1 = of_property_read_bool(pdev->dev.of_node,
					"ti,palmas-enable-dvfs1");
		enable_dvfs2 = of_property_read_bool(pdev->dev.of_node,
					"ti,palmas-enable-dvfs2");
	}

	pci = devm_kzalloc(&pdev->dev, sizeof(*pci), GFP_KERNEL);
	if (!pci) {
		dev_err(&pdev->dev, "Malloc for pci failed\n");
		return -ENOMEM;
	}

	pci->dev = &pdev->dev;
	pci->palmas = dev_get_drvdata(pdev->dev.parent);

	pci->pins = palmas_pins_desc;
	pci->num_pins = ARRAY_SIZE(palmas_pins_desc);
	pci->functions = palmas_pin_function;
	pci->num_functions = ARRAY_SIZE(palmas_pin_function);
	pci->pin_groups = pinctrl_data->pin_groups;
	pci->num_pin_groups = pinctrl_data->num_pin_groups;

	platform_set_drvdata(pdev, pci);

	palmas_pinctrl_set_dvfs1(pci, enable_dvfs1);
	palmas_pinctrl_set_dvfs2(pci, enable_dvfs2);
	ret = palmas_pinctrl_get_pin_mux(pci);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Reading pinctrol option register failed: %d\n", ret);
		return ret;
	}

	palmas_pinctrl_desc.name = dev_name(&pdev->dev);
	palmas_pinctrl_desc.pins = palmas_pins_desc;
	palmas_pinctrl_desc.npins = ARRAY_SIZE(palmas_pins_desc);
	pci->pctl = pinctrl_register(&palmas_pinctrl_desc, &pdev->dev, pci);
	if (!pci->pctl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -ENODEV;
	}
	return 0;
}

static int palmas_pinctrl_remove(struct platform_device *pdev)
{
	struct palmas_pctrl_chip_info *pci = platform_get_drvdata(pdev);

	pinctrl_unregister(pci->pctl);
	return 0;
}

static struct platform_driver palmas_pinctrl_driver = {
	.driver = {
		.name = "palmas-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = palmas_pinctrl_of_match,
	},
	.probe = palmas_pinctrl_probe,
	.remove = palmas_pinctrl_remove,
};

module_platform_driver(palmas_pinctrl_driver);

MODULE_DESCRIPTION("Palmas pin control driver");
MODULE_AUTHOR("Laxman Dewangan<ldewangan@nvidia.com>");
MODULE_ALIAS("platform:palmas-pinctrl");
MODULE_LICENSE("GPL v2");
