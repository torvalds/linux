/*
 * Platform setup for the MPC8xx based boards from TQM.
 *
 * Heiko Schocher <hs@denx.de>
 * Copyright 2010 DENX Software Engineering GmbH
 *
 * based on:
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * Copyright 2005 MontaVista Software Inc.
 *
 * Heavily modified by Scott Wood <scottwood@freescale.com>
 * Copyright 2007 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/delay.h>

#include <linux/fs_enet_pd.h>
#include <linux/fs_uart_pd.h>
#include <linux/fsl_devices.h>
#include <linux/mii.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/8xx_immap.h>
#include <asm/cpm1.h>
#include <asm/fs_pd.h>
#include <asm/udbg.h>

#include "mpc8xx.h"

struct cpm_pin {
	int port, pin, flags;
};

static struct cpm_pin tqm8xx_pins[] __initdata = {
	/* SMC1 */
	{CPM_PORTB, 24, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTB, 25, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TX */

	/* SCC1 */
	{CPM_PORTA, 5, CPM_PIN_INPUT}, /* CLK1 */
	{CPM_PORTA, 7, CPM_PIN_INPUT}, /* CLK2 */
	{CPM_PORTA, 14, CPM_PIN_INPUT}, /* TX */
	{CPM_PORTA, 15, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTC, 15, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TENA */
	{CPM_PORTC, 10, CPM_PIN_INPUT | CPM_PIN_SECONDARY | CPM_PIN_GPIO},
	{CPM_PORTC, 11, CPM_PIN_INPUT | CPM_PIN_SECONDARY | CPM_PIN_GPIO},
};

static struct cpm_pin tqm8xx_fec_pins[] __initdata = {
	/* MII */
	{CPM_PORTD, 3, CPM_PIN_OUTPUT},
	{CPM_PORTD, 4, CPM_PIN_OUTPUT},
	{CPM_PORTD, 5, CPM_PIN_OUTPUT},
	{CPM_PORTD, 6, CPM_PIN_OUTPUT},
	{CPM_PORTD, 7, CPM_PIN_OUTPUT},
	{CPM_PORTD, 8, CPM_PIN_OUTPUT},
	{CPM_PORTD, 9, CPM_PIN_OUTPUT},
	{CPM_PORTD, 10, CPM_PIN_OUTPUT},
	{CPM_PORTD, 11, CPM_PIN_OUTPUT},
	{CPM_PORTD, 12, CPM_PIN_OUTPUT},
	{CPM_PORTD, 13, CPM_PIN_OUTPUT},
	{CPM_PORTD, 14, CPM_PIN_OUTPUT},
	{CPM_PORTD, 15, CPM_PIN_OUTPUT},
};

static void __init init_pins(int n, struct cpm_pin *pin)
{
	int i;

	for (i = 0; i < n; i++) {
		cpm1_set_pin(pin->port, pin->pin, pin->flags);
		pin++;
	}
}

static void __init init_ioports(void)
{
	struct device_node *dnode;
	struct property *prop;
	int	len;

	init_pins(ARRAY_SIZE(tqm8xx_pins), &tqm8xx_pins[0]);

	cpm1_clk_setup(CPM_CLK_SMC1, CPM_BRG1, CPM_CLK_RTX);

	dnode = of_find_node_by_name(NULL, "aliases");
	if (dnode == NULL)
		return;
	prop = of_find_property(dnode, "ethernet1", &len);
	if (prop == NULL)
		return;

	/* init FEC pins */
	init_pins(ARRAY_SIZE(tqm8xx_fec_pins), &tqm8xx_fec_pins[0]);
}

static void __init tqm8xx_setup_arch(void)
{
	cpm_reset();
	init_ioports();
}

static int __init tqm8xx_probe(void)
{
	return of_machine_is_compatible("tqc,tqm8xx");
}

static const struct of_device_id of_bus_ids[] __initconst = {
	{ .name = "soc", },
	{ .name = "cpm", },
	{ .name = "localbus", },
	{ .compatible = "simple-bus" },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(tqm8xx, declare_of_platform_devices);

define_machine(tqm8xx) {
	.name			= "TQM8xx",
	.probe			= tqm8xx_probe,
	.setup_arch		= tqm8xx_setup_arch,
	.init_IRQ		= mpc8xx_pics_init,
	.get_irq		= mpc8xx_get_irq,
	.restart		= mpc8xx_restart,
	.calibrate_decr		= mpc8xx_calibrate_decr,
	.set_rtc_time		= mpc8xx_set_rtc_time,
	.get_rtc_time		= mpc8xx_get_rtc_time,
	.progress		= udbg_progress,
};
