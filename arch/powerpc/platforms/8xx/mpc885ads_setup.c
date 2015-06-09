/*
 * Platform setup for the Freescale mpc885ads board
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
#include <linux/module.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/delay.h>

#include <linux/fs_enet_pd.h>
#include <linux/fs_uart_pd.h>
#include <linux/fsl_devices.h>
#include <linux/mii.h>
#include <linux/of_address.h>
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

#include "mpc885ads.h"
#include "mpc8xx.h"

static u32 __iomem *bcsr, *bcsr5;

struct cpm_pin {
	int port, pin, flags;
};

static struct cpm_pin mpc885ads_pins[] = {
	/* SMC1 */
	{CPM_PORTB, 24, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTB, 25, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TX */

	/* SMC2 */
#ifndef CONFIG_MPC8xx_SECOND_ETH_FEC2
	{CPM_PORTE, 21, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTE, 20, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TX */
#endif

	/* SCC3 */
	{CPM_PORTA, 9, CPM_PIN_INPUT}, /* RX */
	{CPM_PORTA, 8, CPM_PIN_INPUT}, /* TX */
	{CPM_PORTC, 4, CPM_PIN_INPUT | CPM_PIN_SECONDARY | CPM_PIN_GPIO}, /* RENA */
	{CPM_PORTC, 5, CPM_PIN_INPUT | CPM_PIN_SECONDARY | CPM_PIN_GPIO}, /* CLSN */
	{CPM_PORTE, 27, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* TENA */
	{CPM_PORTE, 17, CPM_PIN_INPUT}, /* CLK5 */
	{CPM_PORTE, 16, CPM_PIN_INPUT}, /* CLK6 */

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
#ifdef CONFIG_MPC8xx_SECOND_ETH_FEC2
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
#endif
	/* I2C */
	{CPM_PORTB, 26, CPM_PIN_INPUT | CPM_PIN_OPENDRAIN},
	{CPM_PORTB, 27, CPM_PIN_INPUT | CPM_PIN_OPENDRAIN},
};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mpc885ads_pins); i++) {
		struct cpm_pin *pin = &mpc885ads_pins[i];
		cpm1_set_pin(pin->port, pin->pin, pin->flags);
	}

	cpm1_clk_setup(CPM_CLK_SMC1, CPM_BRG1, CPM_CLK_RTX);
	cpm1_clk_setup(CPM_CLK_SMC2, CPM_BRG2, CPM_CLK_RTX);
	cpm1_clk_setup(CPM_CLK_SCC3, CPM_CLK5, CPM_CLK_TX);
	cpm1_clk_setup(CPM_CLK_SCC3, CPM_CLK6, CPM_CLK_RX);

	/* Set FEC1 and FEC2 to MII mode */
	clrbits32(&mpc8xx_immr->im_cpm.cp_cptr, 0x00000180);
}

static void __init mpc885ads_setup_arch(void)
{
	struct device_node *np;

	cpm_reset();
	init_ioports();

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc885ads-bcsr");
	if (!np) {
		printk(KERN_CRIT "Could not find fsl,mpc885ads-bcsr node\n");
		return;
	}

	bcsr = of_iomap(np, 0);
	bcsr5 = of_iomap(np, 1);
	of_node_put(np);

	if (!bcsr || !bcsr5) {
		printk(KERN_CRIT "Could not remap BCSR\n");
		return;
	}

	clrbits32(&bcsr[1], BCSR1_RS232EN_1);
#ifdef CONFIG_MPC8xx_SECOND_ETH_FEC2
	setbits32(&bcsr[1], BCSR1_RS232EN_2);
#else
	clrbits32(&bcsr[1], BCSR1_RS232EN_2);
#endif

	clrbits32(bcsr5, BCSR5_MII1_EN);
	setbits32(bcsr5, BCSR5_MII1_RST);
	udelay(1000);
	clrbits32(bcsr5, BCSR5_MII1_RST);

#ifdef CONFIG_MPC8xx_SECOND_ETH_FEC2
	clrbits32(bcsr5, BCSR5_MII2_EN);
	setbits32(bcsr5, BCSR5_MII2_RST);
	udelay(1000);
	clrbits32(bcsr5, BCSR5_MII2_RST);
#else
	setbits32(bcsr5, BCSR5_MII2_EN);
#endif

#ifdef CONFIG_MPC8xx_SECOND_ETH_SCC3
	clrbits32(&bcsr[4], BCSR4_ETH10_RST);
	udelay(1000);
	setbits32(&bcsr[4], BCSR4_ETH10_RST);

	setbits32(&bcsr[1], BCSR1_ETHEN);

	np = of_find_node_by_path("/soc@ff000000/cpm@9c0/serial@a80");
#else
	np = of_find_node_by_path("/soc@ff000000/cpm@9c0/ethernet@a40");
#endif

	/* The SCC3 enet registers overlap the SMC1 registers, so
	 * one of the two must be removed from the device tree.
	 */

	if (np) {
		of_detach_node(np);
		of_node_put(np);
	}
}

static int __init mpc885ads_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
	return of_flat_dt_is_compatible(root, "fsl,mpc885ads");
}

static const struct of_device_id of_bus_ids[] __initconst = {
	{ .name = "soc", },
	{ .name = "cpm", },
	{ .name = "localbus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	/* Publish the QE devices */
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(mpc885_ads, declare_of_platform_devices);

define_machine(mpc885_ads) {
	.name			= "Freescale MPC885 ADS",
	.probe			= mpc885ads_probe,
	.setup_arch		= mpc885ads_setup_arch,
	.init_IRQ		= mpc8xx_pics_init,
	.get_irq		= mpc8xx_get_irq,
	.restart		= mpc8xx_restart,
	.calibrate_decr		= mpc8xx_calibrate_decr,
	.set_rtc_time		= mpc8xx_set_rtc_time,
	.get_rtc_time		= mpc8xx_get_rtc_time,
	.progress		= udbg_progress,
};
