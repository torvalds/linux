// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Maple (970 eval board) setup code
 *
 *  (c) Copyright 2004 Benjamin Herrenschmidt (benh@kernel.crashing.org),
 *                     IBM Corp. 
 */

#undef DEBUG

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/smp.h>
#include <linux/bitops.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/memblock.h>

#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/dma.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/mpic.h>
#include <asm/rtas.h>
#include <asm/udbg.h>
#include <asm/nvram.h>

#include "maple.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static unsigned long maple_find_nvram_base(void)
{
	struct device_node *rtcs;
	unsigned long result = 0;

	/* find NVRAM device */
	rtcs = of_find_compatible_node(NULL, "nvram", "AMD8111");
	if (rtcs) {
		struct resource r;
		if (of_address_to_resource(rtcs, 0, &r)) {
			printk(KERN_EMERG "Maple: Unable to translate NVRAM"
			       " address\n");
			goto bail;
		}
		if (!(r.flags & IORESOURCE_IO)) {
			printk(KERN_EMERG "Maple: NVRAM address isn't PIO!\n");
			goto bail;
		}
		result = r.start;
	} else
		printk(KERN_EMERG "Maple: Unable to find NVRAM\n");
 bail:
	of_node_put(rtcs);
	return result;
}

static void __noreturn maple_restart(char *cmd)
{
	unsigned int maple_nvram_base;
	const unsigned int *maple_nvram_offset, *maple_nvram_command;
	struct device_node *sp;

	maple_nvram_base = maple_find_nvram_base();
	if (maple_nvram_base == 0)
		goto fail;

	/* find service processor device */
	sp = of_find_node_by_name(NULL, "service-processor");
	if (!sp) {
		printk(KERN_EMERG "Maple: Unable to find Service Processor\n");
		goto fail;
	}
	maple_nvram_offset = of_get_property(sp, "restart-addr", NULL);
	maple_nvram_command = of_get_property(sp, "restart-value", NULL);
	of_node_put(sp);

	/* send command */
	outb_p(*maple_nvram_command, maple_nvram_base + *maple_nvram_offset);
	for (;;) ;
 fail:
	printk(KERN_EMERG "Maple: Manual Restart Required\n");
	for (;;) ;
}

static void __noreturn maple_power_off(void)
{
	unsigned int maple_nvram_base;
	const unsigned int *maple_nvram_offset, *maple_nvram_command;
	struct device_node *sp;

	maple_nvram_base = maple_find_nvram_base();
	if (maple_nvram_base == 0)
		goto fail;

	/* find service processor device */
	sp = of_find_node_by_name(NULL, "service-processor");
	if (!sp) {
		printk(KERN_EMERG "Maple: Unable to find Service Processor\n");
		goto fail;
	}
	maple_nvram_offset = of_get_property(sp, "power-off-addr", NULL);
	maple_nvram_command = of_get_property(sp, "power-off-value", NULL);
	of_node_put(sp);

	/* send command */
	outb_p(*maple_nvram_command, maple_nvram_base + *maple_nvram_offset);
	for (;;) ;
 fail:
	printk(KERN_EMERG "Maple: Manual Power-Down Required\n");
	for (;;) ;
}

static void __noreturn maple_halt(void)
{
	maple_power_off();
}

#ifdef CONFIG_SMP
static struct smp_ops_t maple_smp_ops = {
	.probe		= smp_mpic_probe,
	.message_pass	= smp_mpic_message_pass,
	.kick_cpu	= smp_generic_kick_cpu,
	.setup_cpu	= smp_mpic_setup_cpu,
	.give_timebase	= smp_generic_give_timebase,
	.take_timebase	= smp_generic_take_timebase,
};
#endif /* CONFIG_SMP */

static void __init maple_use_rtas_reboot_and_halt_if_present(void)
{
	if (rtas_function_implemented(RTAS_FN_SYSTEM_REBOOT) &&
	    rtas_function_implemented(RTAS_FN_POWER_OFF)) {
		ppc_md.restart = rtas_restart;
		pm_power_off = rtas_power_off;
		ppc_md.halt = rtas_halt;
	}
}

static void __init maple_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	/* Setup SMP callback */
#ifdef CONFIG_SMP
	smp_ops = &maple_smp_ops;
#endif
	maple_use_rtas_reboot_and_halt_if_present();

	printk(KERN_DEBUG "Using native/NAP idle loop\n");

	mmio_nvram_init();
}

/*
 * This is almost identical to pSeries and CHRP. We need to make that
 * code generic at one point, with appropriate bits in the device-tree to
 * identify the presence of an HT APIC
 */
static void __init maple_init_IRQ(void)
{
	struct device_node *root, *np, *mpic_node = NULL;
	const unsigned int *opprop;
	unsigned long openpic_addr = 0;
	int naddr, n, i, opplen, has_isus = 0;
	struct mpic *mpic;
	unsigned int flags = 0;

	/* Locate MPIC in the device-tree. Note that there is a bug
	 * in Maple device-tree where the type of the controller is
	 * open-pic and not interrupt-controller
	 */

	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "open-pic")) {
			mpic_node = np;
			break;
		}
	if (mpic_node == NULL)
		for_each_node_by_type(np, "open-pic") {
			mpic_node = np;
			break;
		}
	if (mpic_node == NULL) {
		printk(KERN_ERR
		       "Failed to locate the MPIC interrupt controller\n");
		return;
	}

	/* Find address list in /platform-open-pic */
	root = of_find_node_by_path("/");
	naddr = of_n_addr_cells(root);
	opprop = of_get_property(root, "platform-open-pic", &opplen);
	if (opprop) {
		openpic_addr = of_read_number(opprop, naddr);
		has_isus = (opplen > naddr);
		printk(KERN_DEBUG "OpenPIC addr: %lx, has ISUs: %d\n",
		       openpic_addr, has_isus);
	}

	BUG_ON(openpic_addr == 0);

	/* Check for a big endian MPIC */
	if (of_property_read_bool(np, "big-endian"))
		flags |= MPIC_BIG_ENDIAN;

	/* XXX Maple specific bits */
	flags |= MPIC_U3_HT_IRQS;
	/* All U3/U4 are big-endian, older SLOF firmware doesn't encode this */
	flags |= MPIC_BIG_ENDIAN;

	/* Setup the openpic driver. More device-tree junks, we hard code no
	 * ISUs for now. I'll have to revisit some stuffs with the folks doing
	 * the firmware for those
	 */
	mpic = mpic_alloc(mpic_node, openpic_addr, flags,
			  /*has_isus ? 16 :*/ 0, 0, " MPIC     ");
	BUG_ON(mpic == NULL);

	/* Add ISUs */
	opplen /= sizeof(u32);
	for (n = 0, i = naddr; i < opplen; i += naddr, n++) {
		unsigned long isuaddr = of_read_number(opprop + i, naddr);
		mpic_assign_isu(mpic, n, isuaddr);
	}

	/* All ISUs are setup, complete initialization */
	mpic_init(mpic);
	ppc_md.get_irq = mpic_get_irq;
	of_node_put(mpic_node);
	of_node_put(root);
}

static void __init maple_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}


/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init maple_probe(void)
{
	if (!of_machine_is_compatible("Momentum,Maple") &&
	    !of_machine_is_compatible("Momentum,Apache"))
		return 0;

	pm_power_off = maple_power_off;

	iommu_init_early_dart(&maple_pci_controller_ops);

	return 1;
}

#ifdef CONFIG_EDAC
/*
 * Register a platform device for CPC925 memory controller on
 * all boards with U3H (CPC925) bridge.
 */
static int __init maple_cpc925_edac_setup(void)
{
	struct platform_device *pdev;
	struct device_node *np = NULL;
	struct resource r;
	int ret;
	volatile void __iomem *mem;
	u32 rev;

	np = of_find_node_by_type(NULL, "memory-controller");
	if (!np) {
		printk(KERN_ERR "%s: Unable to find memory-controller node\n",
			__func__);
		return -ENODEV;
	}

	ret = of_address_to_resource(np, 0, &r);
	of_node_put(np);

	if (ret < 0) {
		printk(KERN_ERR "%s: Unable to get memory-controller reg\n",
			__func__);
		return -ENODEV;
	}

	mem = ioremap(r.start, resource_size(&r));
	if (!mem) {
		printk(KERN_ERR "%s: Unable to map memory-controller memory\n",
				__func__);
		return -ENOMEM;
	}

	rev = __raw_readl(mem);
	iounmap(mem);

	if (rev < 0x34 || rev > 0x3f) { /* U3H */
		printk(KERN_ERR "%s: Non-CPC925(U3H) bridge revision: %02x\n",
			__func__, rev);
		return 0;
	}

	pdev = platform_device_register_simple("cpc925_edac", 0, &r, 1);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	printk(KERN_INFO "%s: CPC925 platform device created\n", __func__);

	return 0;
}
machine_device_initcall(maple, maple_cpc925_edac_setup);
#endif

define_machine(maple) {
	.name			= "Maple",
	.probe			= maple_probe,
	.setup_arch		= maple_setup_arch,
	.discover_phbs		= maple_pci_init,
	.init_IRQ		= maple_init_IRQ,
	.pci_irq_fixup		= maple_pci_irq_fixup,
	.pci_get_legacy_ide_irq	= maple_pci_get_legacy_ide_irq,
	.restart		= maple_restart,
	.halt			= maple_halt,
	.get_boot_time		= maple_get_boot_time,
	.set_rtc_time		= maple_set_rtc_time,
	.get_rtc_time		= maple_get_rtc_time,
	.progress		= maple_progress,
	.power_save		= power4_idle,
};
