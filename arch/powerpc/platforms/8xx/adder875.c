// SPDX-License-Identifier: GPL-2.0-only
/* Analogue & Micro Adder MPC875 board support
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 */

#include <linux/init.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/cpm1.h>
#include <asm/8xx_immap.h>
#include <asm/udbg.h>

#include "mpc8xx.h"
#include "pic.h"

struct cpm_pin {
	int port, pin, flags;
};

static __initdata struct cpm_pin adder875_pins[] = {
	/* SMC1 */
	{CPM_PORTB, 24, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTB, 25, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TX */

	/* MII1 */
	{CPM_PORTA, 0, CPM_PIN_INPUT},
	{CPM_PORTA, 1, CPM_PIN_INPUT},
	{CPM_PORTA, 2, CPM_PIN_INPUT},
	{CPM_PORTA, 3, CPM_PIN_INPUT},
	{CPM_PORTA, 4, CPM_PIN_OUTPUT},
	{CPM_PORTA, 10, CPM_PIN_OUTPUT},
	{CPM_PORTA, 11, CPM_PIN_OUTPUT},
	{CPM_PORTB, 19, CPM_PIN_INPUT},
	{CPM_PORTB, 31, CPM_PIN_INPUT},
	{CPM_PORTC, 12, CPM_PIN_INPUT},
	{CPM_PORTC, 13, CPM_PIN_INPUT},
	{CPM_PORTE, 30, CPM_PIN_OUTPUT},
	{CPM_PORTE, 31, CPM_PIN_OUTPUT},

	/* MII2 */
	{CPM_PORTE, 14, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{CPM_PORTE, 15, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{CPM_PORTE, 16, CPM_PIN_OUTPUT},
	{CPM_PORTE, 17, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{CPM_PORTE, 18, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{CPM_PORTE, 19, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{CPM_PORTE, 20, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{CPM_PORTE, 21, CPM_PIN_OUTPUT},
	{CPM_PORTE, 22, CPM_PIN_OUTPUT},
	{CPM_PORTE, 23, CPM_PIN_OUTPUT},
	{CPM_PORTE, 24, CPM_PIN_OUTPUT},
	{CPM_PORTE, 25, CPM_PIN_OUTPUT},
	{CPM_PORTE, 26, CPM_PIN_OUTPUT},
	{CPM_PORTE, 27, CPM_PIN_OUTPUT},
	{CPM_PORTE, 28, CPM_PIN_OUTPUT},
	{CPM_PORTE, 29, CPM_PIN_OUTPUT},
};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adder875_pins); i++) {
		const struct cpm_pin *pin = &adder875_pins[i];
		cpm1_set_pin(pin->port, pin->pin, pin->flags);
	}

	cpm1_clk_setup(CPM_CLK_SMC1, CPM_BRG1, CPM_CLK_RTX);

	/* Set FEC1 and FEC2 to MII mode */
	clrbits32(&mpc8xx_immr->im_cpm.cp_cptr, 0x00000180);
}

static void __init adder875_setup(void)
{
	cpm_reset();
	init_ioports();
}

static const struct of_device_id of_bus_ids[] __initconst = {
	{ .compatible = "simple-bus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);
	return 0;
}
machine_device_initcall(adder875, declare_of_platform_devices);

define_machine(adder875) {
	.name = "Adder MPC875",
	.compatible = "analogue-and-micro,adder875",
	.setup_arch = adder875_setup,
	.init_IRQ = mpc8xx_pic_init,
	.get_irq = mpc8xx_get_irq,
	.restart = mpc8xx_restart,
	.progress = udbg_progress,
};
