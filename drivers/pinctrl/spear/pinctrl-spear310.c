/*
 * Driver for the ST Microelectronics SPEAr310 pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "pinctrl-spear3xx.h"

#define DRIVER_NAME "spear310-pinmux"

/* addresses */
#define PMX_CONFIG_REG			0x08

/* emi_cs_0_to_5_pins */
static const unsigned emi_cs_0_to_5_pins[] = { 45, 46, 47, 48, 49, 50 };
static struct spear_muxreg emi_cs_0_to_5_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	},
};

static struct spear_modemux emi_cs_0_to_5_modemux[] = {
	{
		.muxregs = emi_cs_0_to_5_muxreg,
		.nmuxregs = ARRAY_SIZE(emi_cs_0_to_5_muxreg),
	},
};

static struct spear_pingroup emi_cs_0_to_5_pingroup = {
	.name = "emi_cs_0_to_5_grp",
	.pins = emi_cs_0_to_5_pins,
	.npins = ARRAY_SIZE(emi_cs_0_to_5_pins),
	.modemuxs = emi_cs_0_to_5_modemux,
	.nmodemuxs = ARRAY_SIZE(emi_cs_0_to_5_modemux),
};

static const char *const emi_cs_0_to_5_grps[] = { "emi_cs_0_to_5_grp" };
static struct spear_function emi_cs_0_to_5_function = {
	.name = "emi",
	.groups = emi_cs_0_to_5_grps,
	.ngroups = ARRAY_SIZE(emi_cs_0_to_5_grps),
};

/* uart1_pins */
static const unsigned uart1_pins[] = { 0, 1 };
static struct spear_muxreg uart1_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_FIRDA_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart1_modemux[] = {
	{
		.muxregs = uart1_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_muxreg),
	},
};

static struct spear_pingroup uart1_pingroup = {
	.name = "uart1_grp",
	.pins = uart1_pins,
	.npins = ARRAY_SIZE(uart1_pins),
	.modemuxs = uart1_modemux,
	.nmodemuxs = ARRAY_SIZE(uart1_modemux),
};

static const char *const uart1_grps[] = { "uart1_grp" };
static struct spear_function uart1_function = {
	.name = "uart1",
	.groups = uart1_grps,
	.ngroups = ARRAY_SIZE(uart1_grps),
};

/* uart2_pins */
static const unsigned uart2_pins[] = { 43, 44 };
static struct spear_muxreg uart2_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart2_modemux[] = {
	{
		.muxregs = uart2_muxreg,
		.nmuxregs = ARRAY_SIZE(uart2_muxreg),
	},
};

static struct spear_pingroup uart2_pingroup = {
	.name = "uart2_grp",
	.pins = uart2_pins,
	.npins = ARRAY_SIZE(uart2_pins),
	.modemuxs = uart2_modemux,
	.nmodemuxs = ARRAY_SIZE(uart2_modemux),
};

static const char *const uart2_grps[] = { "uart2_grp" };
static struct spear_function uart2_function = {
	.name = "uart2",
	.groups = uart2_grps,
	.ngroups = ARRAY_SIZE(uart2_grps),
};

/* uart3_pins */
static const unsigned uart3_pins[] = { 37, 38 };
static struct spear_muxreg uart3_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart3_modemux[] = {
	{
		.muxregs = uart3_muxreg,
		.nmuxregs = ARRAY_SIZE(uart3_muxreg),
	},
};

static struct spear_pingroup uart3_pingroup = {
	.name = "uart3_grp",
	.pins = uart3_pins,
	.npins = ARRAY_SIZE(uart3_pins),
	.modemuxs = uart3_modemux,
	.nmodemuxs = ARRAY_SIZE(uart3_modemux),
};

static const char *const uart3_grps[] = { "uart3_grp" };
static struct spear_function uart3_function = {
	.name = "uart3",
	.groups = uart3_grps,
	.ngroups = ARRAY_SIZE(uart3_grps),
};

/* uart4_pins */
static const unsigned uart4_pins[] = { 39, 40 };
static struct spear_muxreg uart4_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart4_modemux[] = {
	{
		.muxregs = uart4_muxreg,
		.nmuxregs = ARRAY_SIZE(uart4_muxreg),
	},
};

static struct spear_pingroup uart4_pingroup = {
	.name = "uart4_grp",
	.pins = uart4_pins,
	.npins = ARRAY_SIZE(uart4_pins),
	.modemuxs = uart4_modemux,
	.nmodemuxs = ARRAY_SIZE(uart4_modemux),
};

static const char *const uart4_grps[] = { "uart4_grp" };
static struct spear_function uart4_function = {
	.name = "uart4",
	.groups = uart4_grps,
	.ngroups = ARRAY_SIZE(uart4_grps),
};

/* uart5_pins */
static const unsigned uart5_pins[] = { 41, 42 };
static struct spear_muxreg uart5_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart5_modemux[] = {
	{
		.muxregs = uart5_muxreg,
		.nmuxregs = ARRAY_SIZE(uart5_muxreg),
	},
};

static struct spear_pingroup uart5_pingroup = {
	.name = "uart5_grp",
	.pins = uart5_pins,
	.npins = ARRAY_SIZE(uart5_pins),
	.modemuxs = uart5_modemux,
	.nmodemuxs = ARRAY_SIZE(uart5_modemux),
};

static const char *const uart5_grps[] = { "uart5_grp" };
static struct spear_function uart5_function = {
	.name = "uart5",
	.groups = uart5_grps,
	.ngroups = ARRAY_SIZE(uart5_grps),
};

/* fsmc_pins */
static const unsigned fsmc_pins[] = { 34, 35, 36 };
static struct spear_muxreg fsmc_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_CS_MASK,
		.val = 0,
	},
};

static struct spear_modemux fsmc_modemux[] = {
	{
		.muxregs = fsmc_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_muxreg),
	},
};

static struct spear_pingroup fsmc_pingroup = {
	.name = "fsmc_grp",
	.pins = fsmc_pins,
	.npins = ARRAY_SIZE(fsmc_pins),
	.modemuxs = fsmc_modemux,
	.nmodemuxs = ARRAY_SIZE(fsmc_modemux),
};

static const char *const fsmc_grps[] = { "fsmc_grp" };
static struct spear_function fsmc_function = {
	.name = "fsmc",
	.groups = fsmc_grps,
	.ngroups = ARRAY_SIZE(fsmc_grps),
};

/* rs485_0_pins */
static const unsigned rs485_0_pins[] = { 19, 20, 21, 22, 23 };
static struct spear_muxreg rs485_0_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_modemux rs485_0_modemux[] = {
	{
		.muxregs = rs485_0_muxreg,
		.nmuxregs = ARRAY_SIZE(rs485_0_muxreg),
	},
};

static struct spear_pingroup rs485_0_pingroup = {
	.name = "rs485_0_grp",
	.pins = rs485_0_pins,
	.npins = ARRAY_SIZE(rs485_0_pins),
	.modemuxs = rs485_0_modemux,
	.nmodemuxs = ARRAY_SIZE(rs485_0_modemux),
};

static const char *const rs485_0_grps[] = { "rs485_0" };
static struct spear_function rs485_0_function = {
	.name = "rs485_0",
	.groups = rs485_0_grps,
	.ngroups = ARRAY_SIZE(rs485_0_grps),
};

/* rs485_1_pins */
static const unsigned rs485_1_pins[] = { 14, 15, 16, 17, 18 };
static struct spear_muxreg rs485_1_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_modemux rs485_1_modemux[] = {
	{
		.muxregs = rs485_1_muxreg,
		.nmuxregs = ARRAY_SIZE(rs485_1_muxreg),
	},
};

static struct spear_pingroup rs485_1_pingroup = {
	.name = "rs485_1_grp",
	.pins = rs485_1_pins,
	.npins = ARRAY_SIZE(rs485_1_pins),
	.modemuxs = rs485_1_modemux,
	.nmodemuxs = ARRAY_SIZE(rs485_1_modemux),
};

static const char *const rs485_1_grps[] = { "rs485_1" };
static struct spear_function rs485_1_function = {
	.name = "rs485_1",
	.groups = rs485_1_grps,
	.ngroups = ARRAY_SIZE(rs485_1_grps),
};

/* tdm_pins */
static const unsigned tdm_pins[] = { 10, 11, 12, 13 };
static struct spear_muxreg tdm_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_modemux tdm_modemux[] = {
	{
		.muxregs = tdm_muxreg,
		.nmuxregs = ARRAY_SIZE(tdm_muxreg),
	},
};

static struct spear_pingroup tdm_pingroup = {
	.name = "tdm_grp",
	.pins = tdm_pins,
	.npins = ARRAY_SIZE(tdm_pins),
	.modemuxs = tdm_modemux,
	.nmodemuxs = ARRAY_SIZE(tdm_modemux),
};

static const char *const tdm_grps[] = { "tdm_grp" };
static struct spear_function tdm_function = {
	.name = "tdm",
	.groups = tdm_grps,
	.ngroups = ARRAY_SIZE(tdm_grps),
};

/* pingroups */
static struct spear_pingroup *spear310_pingroups[] = {
	SPEAR3XX_COMMON_PINGROUPS,
	&emi_cs_0_to_5_pingroup,
	&uart1_pingroup,
	&uart2_pingroup,
	&uart3_pingroup,
	&uart4_pingroup,
	&uart5_pingroup,
	&fsmc_pingroup,
	&rs485_0_pingroup,
	&rs485_1_pingroup,
	&tdm_pingroup,
};

/* functions */
static struct spear_function *spear310_functions[] = {
	SPEAR3XX_COMMON_FUNCTIONS,
	&emi_cs_0_to_5_function,
	&uart1_function,
	&uart2_function,
	&uart3_function,
	&uart4_function,
	&uart5_function,
	&fsmc_function,
	&rs485_0_function,
	&rs485_1_function,
	&tdm_function,
};

static struct of_device_id spear310_pinctrl_of_match[] __devinitdata = {
	{
		.compatible = "st,spear310-pinmux",
	},
	{},
};

static int __devinit spear310_pinctrl_probe(struct platform_device *pdev)
{
	int ret;

	spear3xx_machdata.groups = spear310_pingroups;
	spear3xx_machdata.ngroups = ARRAY_SIZE(spear310_pingroups);
	spear3xx_machdata.functions = spear310_functions;
	spear3xx_machdata.nfunctions = ARRAY_SIZE(spear310_functions);

	pmx_init_addr(&spear3xx_machdata, PMX_CONFIG_REG);

	spear3xx_machdata.modes_supported = false;

	ret = spear_pinctrl_probe(pdev, &spear3xx_machdata);
	if (ret)
		return ret;

	return 0;
}

static int __devexit spear310_pinctrl_remove(struct platform_device *pdev)
{
	return spear_pinctrl_remove(pdev);
}

static struct platform_driver spear310_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = spear310_pinctrl_of_match,
	},
	.probe = spear310_pinctrl_probe,
	.remove = __devexit_p(spear310_pinctrl_remove),
};

static int __init spear310_pinctrl_init(void)
{
	return platform_driver_register(&spear310_pinctrl_driver);
}
arch_initcall(spear310_pinctrl_init);

static void __exit spear310_pinctrl_exit(void)
{
	platform_driver_unregister(&spear310_pinctrl_driver);
}
module_exit(spear310_pinctrl_exit);

MODULE_AUTHOR("Viresh Kumar <viresh.linux@gmail.com>");
MODULE_DESCRIPTION("ST Microelectronics SPEAr310 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, SPEAr310_pinctrl_of_match);
