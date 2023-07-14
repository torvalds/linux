/*
 * Board setup routines for the Emerson KSI8560
 *
 * Author: Alexandr Smirnov <asmirnov@ru.mvista.com>
 *
 * Based on mpc85xx_ads.c maintained by Kumar Gala
 *
 * 2008 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
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

#include <asm/cpm2.h>
#include <sysdev/cpm2_pic.h>

#include "mpc85xx.h"

#define KSI8560_CPLD_HVR		0x04 /* Hardware Version Register */
#define KSI8560_CPLD_PVR		0x08 /* PLD Version Register */
#define KSI8560_CPLD_RCR1		0x30 /* Reset Command Register 1 */

#define KSI8560_CPLD_RCR1_CPUHR		0x80 /* CPU Hard Reset */

static void __iomem *cpld_base = NULL;

static void __noreturn machine_restart(char *cmd)
{
	if (cpld_base)
		out_8(cpld_base + KSI8560_CPLD_RCR1, KSI8560_CPLD_RCR1_CPUHR);
	else
		printk(KERN_ERR "Can't find CPLD base, hang forever\n");

	for (;;);
}

static void __init ksi8560_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);

	mpc85xx_cpm2_pic_init();
}

#ifdef CONFIG_CPM2
/*
 * Setup I/O ports
 */
struct cpm_pin {
	int port, pin, flags;
};

static struct cpm_pin __initdata ksi8560_pins[] = {
	/* SCC1 */
	{3, 29, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 30, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{3, 31, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* SCC2 */
	{3, 26, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 27, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 28, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* FCC1 */
	{0, 14, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 15, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 16, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 17, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 18, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 19, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 20, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 21, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 26, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 27, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 28, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{0, 29, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{0, 30, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 31, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{2, 23, CPM_PIN_INPUT | CPM_PIN_PRIMARY}, /* CLK9 */
	{2, 22, CPM_PIN_INPUT | CPM_PIN_PRIMARY}, /* CLK10 */

};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ksi8560_pins); i++) {
		struct cpm_pin *pin = &ksi8560_pins[i];
		cpm2_set_pin(pin->port, pin->pin, pin->flags);
	}

	cpm2_clk_setup(CPM_CLK_SCC1, CPM_BRG1, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC1, CPM_BRG1, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_SCC2, CPM_BRG2, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC2, CPM_BRG2, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_FCC1, CPM_CLK9, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC1, CPM_CLK10, CPM_CLK_TX);
}
#endif

/*
 * Setup the architecture
 */
static void __init ksi8560_setup_arch(void)
{
	struct device_node *cpld;

	cpld = of_find_compatible_node(NULL, NULL, "emerson,KSI8560-cpld");
	if (cpld)
		cpld_base = of_iomap(cpld, 0);
	else
		printk(KERN_ERR "Can't find CPLD in device tree\n");

	of_node_put(cpld);

	if (ppc_md.progress)
		ppc_md.progress("ksi8560_setup_arch()", 0);

#ifdef CONFIG_CPM2
	cpm2_reset();
	init_ioports();
#endif
}

static void ksi8560_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Emerson Network Power\n");
	seq_printf(m, "Board\t\t: KSI8560\n");

	if (cpld_base) {
		seq_printf(m, "Hardware rev\t: %d\n",
					in_8(cpld_base + KSI8560_CPLD_HVR));
		seq_printf(m, "CPLD rev\t: %d\n",
					in_8(cpld_base + KSI8560_CPLD_PVR));
	} else
		seq_printf(m, "Unknown Hardware and CPLD revs\n");

	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));
}

machine_device_initcall(ksi8560, mpc85xx_common_publish_devices);

define_machine(ksi8560) {
	.name			= "KSI8560",
	.compatible		= "emerson,KSI8560",
	.setup_arch		= ksi8560_setup_arch,
	.init_IRQ		= ksi8560_pic_init,
	.show_cpuinfo		= ksi8560_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.restart		= machine_restart,
};
