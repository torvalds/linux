/*
 * Wind River SBC8560 setup and early boot code.
 *
 * Copyright 2007 Wind River Systems Inc.
 *
 * By Paul Gortmaker (see MAINTAINERS for contact information)
 *
 * Based largely on the MPC8560ADS support - Copyright 2005 Freescale Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/of_platform.h>

#include <asm/system.h>
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

#ifdef CONFIG_CPM2

static void cpm2_cascade(unsigned int irq, struct irq_desc *desc)
{
	int cascade_irq;

	while ((cascade_irq = cpm2_get_irq()) >= 0)
		generic_handle_irq(cascade_irq);

	desc->chip->eoi(irq);
}

#endif /* CONFIG_CPM2 */

static void __init sbc8560_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np = NULL;
#ifdef CONFIG_CPM2
	int irq;
#endif

	np = of_find_node_by_type(np, "open-pic");
	if (!np) {
		printk(KERN_ERR "Could not find open-pic node\n");
		return;
	}

	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_ERR "Could not map mpic register space\n");
		of_node_put(np);
		return;
	}

	mpic = mpic_alloc(np, r.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	of_node_put(np);

	mpic_init(mpic);

#ifdef CONFIG_CPM2
	/* Setup CPM2 PIC */
	np = of_find_compatible_node(NULL, NULL, "fsl,cpm2-pic");
	if (np == NULL) {
		printk(KERN_ERR "PIC init: can not find fsl,cpm2-pic node\n");
		return;
	}
	irq = irq_of_parse_and_map(np, 0);

	cpm2_pic_init(np);
	of_node_put(np);
	set_irq_chained_handler(irq, cpm2_cascade);
#endif
}

/*
 * Setup the architecture
 */
#ifdef CONFIG_CPM2
struct cpm_pin {
	int port, pin, flags;
};

static const struct cpm_pin sbc8560_pins[] = {
	/* SCC1 */
	{3, 29, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 30, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{3, 31, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* SCC2 */
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
	{1, 7, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
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
	{2, 16, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* CLK16 */
	{2, 17, CPM_PIN_INPUT | CPM_PIN_SECONDARY}, /* CLK15 */
};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sbc8560_pins); i++) {
		const struct cpm_pin *pin = &sbc8560_pins[i];
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

static void __init sbc8560_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("sbc8560_setup_arch()", 0);

#ifdef CONFIG_CPM2
	cpm2_reset();
	init_ioports();
#endif

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,mpc8540-pci")
		fsl_add_bridge(np, 1);
#endif
}

static void sbc8560_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Wind River\n");
	seq_printf(m, "Machine\t\t: SBC8560\n");
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}

static struct of_device_id __initdata of_bus_ids[] = {
	{ .name = "soc", },
	{ .type = "soc", },
	{ .name = "cpm", },
	{ .name = "localbus", },
	{ .compatible = "simple-bus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(sbc8560, declare_of_platform_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init sbc8560_probe(void)
{
        unsigned long root = of_get_flat_dt_root();

        return of_flat_dt_is_compatible(root, "SBC8560");
}

#ifdef CONFIG_RTC_DRV_M48T59
static int __init sbc8560_rtc_init(void)
{
	struct device_node *np;
	struct resource res;
	struct platform_device *rtc_dev;

	np = of_find_compatible_node(NULL, NULL, "m48t59");
	if (np == NULL) {
		printk("No RTC in DTB. Has it been eaten by wild dogs?\n");
		return -ENODEV;
	}

	of_address_to_resource(np, 0, &res);
	of_node_put(np);

	printk("Found RTC (m48t59) at i/o 0x%x\n", res.start);

	rtc_dev = platform_device_register_simple("rtc-m48t59", 0, &res, 1);

	if (IS_ERR(rtc_dev)) {
		printk("Registering sbc8560 RTC device failed\n");
		return PTR_ERR(rtc_dev);
	}

	return 0;
}

arch_initcall(sbc8560_rtc_init);

#endif	/* M48T59 */

define_machine(sbc8560) {
	.name			= "SBC8560",
	.probe			= sbc8560_probe,
	.setup_arch		= sbc8560_setup_arch,
	.init_IRQ		= sbc8560_pic_init,
	.show_cpuinfo		= sbc8560_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
