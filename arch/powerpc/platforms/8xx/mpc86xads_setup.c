/*arch/powerpc/platforms/8xx/mpc86xads_setup.c
 *
 * Platform setup for the Freescale mpc86xads board
 *
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
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>

#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/8xx_immap.h>
#include <asm/cpm1.h>
#include <asm/udbg.h>

#include "mpc86xads.h"
#include "mpc8xx.h"
#include "pic.h"

struct cpm_pin {
	int port, pin, flags;
};

static struct cpm_pin mpc866ads_pins[] = {
	/* SMC1 */
	{CPM_PORTB, 24, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTB, 25, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TX */

	/* SMC2 */
	{CPM_PORTB, 21, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTB, 20, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TX */

	/* SCC1 */
	{CPM_PORTA, 6, CPM_PIN_INPUT}, /* CLK1 */
	{CPM_PORTA, 7, CPM_PIN_INPUT}, /* CLK2 */
	{CPM_PORTA, 14, CPM_PIN_INPUT}, /* TX */
	{CPM_PORTA, 15, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTB, 19, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TENA */
	{CPM_PORTC, 10, CPM_PIN_INPUT | CPM_PIN_SECONDARY | CPM_PIN_GPIO}, /* RENA */
	{CPM_PORTC, 11, CPM_PIN_INPUT | CPM_PIN_SECONDARY | CPM_PIN_GPIO}, /* CLSN */

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

	/* I2C */
	{CPM_PORTB, 26, CPM_PIN_INPUT | CPM_PIN_OPENDRAIN},
	{CPM_PORTB, 27, CPM_PIN_INPUT | CPM_PIN_OPENDRAIN},
};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mpc866ads_pins); i++) {
		struct cpm_pin *pin = &mpc866ads_pins[i];
		cpm1_set_pin(pin->port, pin->pin, pin->flags);
	}

	cpm1_clk_setup(CPM_CLK_SMC1, CPM_BRG1, CPM_CLK_RTX);
	cpm1_clk_setup(CPM_CLK_SMC2, CPM_BRG2, CPM_CLK_RTX);
	cpm1_clk_setup(CPM_CLK_SCC1, CPM_CLK1, CPM_CLK_TX);
	cpm1_clk_setup(CPM_CLK_SCC1, CPM_CLK2, CPM_CLK_RX);

	/* Set FEC1 and FEC2 to MII mode */
	clrbits32(&mpc8xx_immr->im_cpm.cp_cptr, 0x00000180);
}

static void __init mpc86xads_setup_arch(void)
{
	struct device_node *np;
	u32 __iomem *bcsr_io;

	cpm_reset();
	init_ioports();

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc866ads-bcsr");
	if (!np) {
		printk(KERN_CRIT "Could not find fsl,mpc866ads-bcsr node\n");
		return;
	}

	bcsr_io = of_iomap(np, 0);
	of_node_put(np);

	if (bcsr_io == NULL) {
		printk(KERN_CRIT "Could not remap BCSR\n");
		return;
	}

	clrbits32(bcsr_io, BCSR1_RS232EN_1 | BCSR1_RS232EN_2 | BCSR1_ETHEN);
	iounmap(bcsr_io);
}

static const struct of_device_id of_bus_ids[] __initconst = {
	{ .name = "soc", },
	{ .name = "cpm", },
	{ .name = "localbus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(mpc86x_ads, declare_of_platform_devices);

define_machine(mpc86x_ads) {
	.name			= "MPC86x ADS",
	.compatible		= "fsl,mpc866ads",
	.setup_arch		= mpc86xads_setup_arch,
	.init_IRQ		= mpc8xx_pic_init,
	.get_irq		= mpc8xx_get_irq,
	.restart		= mpc8xx_restart,
	.calibrate_decr		= mpc8xx_calibrate_decr,
	.set_rtc_time		= mpc8xx_set_rtc_time,
	.get_rtc_time		= mpc8xx_get_rtc_time,
	.progress		= udbg_progress,
};
