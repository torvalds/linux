// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPC85xx setup and early boot code plus other random bits.
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpic.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#ifdef CONFIG_CPM2
#include <asm/cpm2.h>
#include <sysdev/cpm2_pic.h>
#endif

#include "mpc85xx.h"

static void __init mpc85xx_ads_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);

	mpc85xx_cpm2_pic_init();
}

/*
 * Setup the architecture
 */
#ifdef CONFIG_CPM2
struct cpm_pin {
	int port, pin, flags;
};

static const struct cpm_pin mpc8560_ads_pins[] = {
	/* SCC1 */
	{3, 29, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 30, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{3, 31, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* SCC2 */
	{2, 12, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 13, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{3, 26, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 27, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 28, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* FCC2 */
	{1, 18, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 19, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 20, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 21, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 22, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 23, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 24, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 25, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 26, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 27, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 28, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 29, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{1, 30, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 31, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{2, 18, CPM_PIN_INPUT | CPM_PIN_PRIMARY}, /* CLK14 */
	{2, 19, CPM_PIN_INPUT | CPM_PIN_PRIMARY}, /* CLK13 */

	/* FCC3 */
	{1, 4, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 5, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 6, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 8, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 9, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 10, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 11, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 12, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 13, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 14, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 15, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 16, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 17, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 16, CPM_PIN_INPUT | CPM_PIN_PRIMARY}, /* CLK16 */
	{2, 17, CPM_PIN_INPUT | CPM_PIN_PRIMARY}, /* CLK15 */
	{2, 27, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mpc8560_ads_pins); i++) {
		const struct cpm_pin *pin = &mpc8560_ads_pins[i];
		cpm2_set_pin(pin->port, pin->pin, pin->flags);
	}

	cpm2_clk_setup(CPM_CLK_SCC1, CPM_BRG1, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC1, CPM_BRG1, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_SCC2, CPM_BRG2, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC2, CPM_BRG2, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_FCC2, CPM_CLK13, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC2, CPM_CLK14, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_FCC3, CPM_CLK15, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC3, CPM_CLK16, CPM_CLK_TX);
}
#endif

static void __init mpc85xx_ads_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_ads_setup_arch()", 0);

#ifdef CONFIG_CPM2
	cpm2_reset();
	init_ioports();
#endif

	fsl_pci_assign_primary();
}

static void mpc85xx_ads_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));
}

machine_arch_initcall(mpc85xx_ads, mpc85xx_common_publish_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc85xx_ads_probe(void)
{
	return of_machine_is_compatible("MPC85xxADS");
}

define_machine(mpc85xx_ads) {
	.name			= "MPC85xx ADS",
	.probe			= mpc85xx_ads_probe,
	.setup_arch		= mpc85xx_ads_setup_arch,
	.init_IRQ		= mpc85xx_ads_pic_init,
	.show_cpuinfo		= mpc85xx_ads_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
