/*
 * Copyright 2006-2008, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>

#include <asm/kexec.h>
#include <asm/reg.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/cell-regs.h>

#include "ras.h"


static void dump_fir(int cpu)
{
	struct cbe_pmd_regs __iomem *pregs = cbe_get_cpu_pmd_regs(cpu);
	struct cbe_iic_regs __iomem *iregs = cbe_get_cpu_iic_regs(cpu);

	if (pregs == NULL)
		return;

	/* Todo: do some nicer parsing of bits and based on them go down
	 * to other sub-units FIRs and not only IIC
	 */
	printk(KERN_ERR "Global Checkstop FIR    : 0x%016llx\n",
	       in_be64(&pregs->checkstop_fir));
	printk(KERN_ERR "Global Recoverable FIR  : 0x%016llx\n",
	       in_be64(&pregs->checkstop_fir));
	printk(KERN_ERR "Global MachineCheck FIR : 0x%016llx\n",
	       in_be64(&pregs->spec_att_mchk_fir));

	if (iregs == NULL)
		return;
	printk(KERN_ERR "IOC FIR                 : 0x%016llx\n",
	       in_be64(&iregs->ioc_fir));

}

void cbe_system_error_exception(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	printk(KERN_ERR "System Error Interrupt on CPU %d !\n", cpu);
	dump_fir(cpu);
	dump_stack();
}

void cbe_maintenance_exception(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	/*
	 * Nothing implemented for the maintenance interrupt at this point
	 */

	printk(KERN_ERR "Unhandled Maintenance interrupt on CPU %d !\n", cpu);
	dump_stack();
}

void cbe_thermal_exception(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	/*
	 * Nothing implemented for the thermal interrupt at this point
	 */

	printk(KERN_ERR "Unhandled Thermal interrupt on CPU %d !\n", cpu);
	dump_stack();
}

static int cbe_machine_check_handler(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	printk(KERN_ERR "Machine Check Interrupt on CPU %d !\n", cpu);
	dump_fir(cpu);

	/* No recovery from this code now, lets continue */
	return 0;
}

struct ptcal_area {
	struct list_head list;
	int nid;
	int order;
	struct page *pages;
};

static LIST_HEAD(ptcal_list);

static int ptcal_start_tok, ptcal_stop_tok;

static int __init cbe_ptcal_enable_on_node(int nid, int order)
{
	struct ptcal_area *area;
	int ret = -ENOMEM;
	unsigned long addr;

	if (is_kdump_kernel())
		rtas_call(ptcal_stop_tok, 1, 1, NULL, nid);

	area = kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		goto out_err;

	area->nid = nid;
	area->order = order;
	area->pages = __alloc_pages_node(area->nid,
						GFP_KERNEL|__GFP_THISNODE,
						area->order);

	if (!area->pages) {
		printk(KERN_WARNING "%s: no page on node %d\n",
			__func__, area->nid);
		goto out_free_area;
	}

	/*
	 * We move the ptcal area to the middle of the allocated
	 * page, in order to avoid prefetches in memcpy and similar
	 * functions stepping on it.
	 */
	addr = __pa(page_address(area->pages)) + (PAGE_SIZE >> 1);
	printk(KERN_DEBUG "%s: enabling PTCAL on node %d address=0x%016lx\n",
			__func__, area->nid, addr);

	ret = -EIO;
	if (rtas_call(ptcal_start_tok, 3, 1, NULL, area->nid,
				(unsigned int)(addr >> 32),
				(unsigned int)(addr & 0xffffffff))) {
		printk(KERN_ERR "%s: error enabling PTCAL on node %d!\n",
				__func__, nid);
		goto out_free_pages;
	}

	list_add(&area->list, &ptcal_list);

	return 0;

out_free_pages:
	__free_pages(area->pages, area->order);
out_free_area:
	kfree(area);
out_err:
	return ret;
}

static int __init cbe_ptcal_enable(void)
{
	const u32 *size;
	struct device_node *np;
	int order, found_mic = 0;

	np = of_find_node_by_path("/rtas");
	if (!np)
		return -ENODEV;

	size = of_get_property(np, "ibm,cbe-ptcal-size", NULL);
	if (!size) {
		of_node_put(np);
		return -ENODEV;
	}

	pr_debug("%s: enabling PTCAL, size = 0x%x\n", __func__, *size);
	order = get_order(*size);
	of_node_put(np);

	/* support for malta device trees, with be@/mic@ nodes */
	for_each_node_by_type(np, "mic-tm") {
		cbe_ptcal_enable_on_node(of_node_to_nid(np), order);
		found_mic = 1;
	}

	if (found_mic)
		return 0;

	/* support for older device tree - use cpu nodes */
	for_each_node_by_type(np, "cpu") {
		const u32 *nid = of_get_property(np, "node-id", NULL);
		if (!nid) {
			printk(KERN_ERR "%s: node %pOF is missing node-id?\n",
					__func__, np);
			continue;
		}
		cbe_ptcal_enable_on_node(*nid, order);
		found_mic = 1;
	}

	return found_mic ? 0 : -ENODEV;
}

static int cbe_ptcal_disable(void)
{
	struct ptcal_area *area, *tmp;
	int ret = 0;

	pr_debug("%s: disabling PTCAL\n", __func__);

	list_for_each_entry_safe(area, tmp, &ptcal_list, list) {
		/* disable ptcal on this node */
		if (rtas_call(ptcal_stop_tok, 1, 1, NULL, area->nid)) {
			printk(KERN_ERR "%s: error disabling PTCAL "
					"on node %d!\n", __func__,
					area->nid);
			ret = -EIO;
			continue;
		}

		/* ensure we can access the PTCAL area */
		memset(page_address(area->pages), 0,
				1 << (area->order + PAGE_SHIFT));

		/* clean up */
		list_del(&area->list);
		__free_pages(area->pages, area->order);
		kfree(area);
	}

	return ret;
}

static int cbe_ptcal_notify_reboot(struct notifier_block *nb,
		unsigned long code, void *data)
{
	return cbe_ptcal_disable();
}

static void cbe_ptcal_crash_shutdown(void)
{
	cbe_ptcal_disable();
}

static struct notifier_block cbe_ptcal_reboot_notifier = {
	.notifier_call = cbe_ptcal_notify_reboot
};

#ifdef CONFIG_PPC_IBM_CELL_RESETBUTTON
static int sysreset_hack;

static int __init cbe_sysreset_init(void)
{
	struct cbe_pmd_regs __iomem *regs;

	sysreset_hack = of_machine_is_compatible("IBM,CBPLUS-1.0");
	if (!sysreset_hack)
		return 0;

	regs = cbe_get_cpu_pmd_regs(0);
	if (!regs)
		return 0;

	/* Enable JTAG system-reset hack */
	out_be32(&regs->fir_mode_reg,
		in_be32(&regs->fir_mode_reg) |
		CBE_PMD_FIR_MODE_M8);

	return 0;
}
device_initcall(cbe_sysreset_init);

int cbe_sysreset_hack(void)
{
	struct cbe_pmd_regs __iomem *regs;

	/*
	 * The BMC can inject user triggered system reset exceptions,
	 * but cannot set the system reset reason in srr1,
	 * so check an extra register here.
	 */
	if (sysreset_hack && (smp_processor_id() == 0)) {
		regs = cbe_get_cpu_pmd_regs(0);
		if (!regs)
			return 0;
		if (in_be64(&regs->ras_esc_0) & 0x0000ffff) {
			out_be64(&regs->ras_esc_0, 0);
			return 0;
		}
	}
	return 1;
}
#endif /* CONFIG_PPC_IBM_CELL_RESETBUTTON */

static int __init cbe_ptcal_init(void)
{
	int ret;
	ptcal_start_tok = rtas_token("ibm,cbe-start-ptcal");
	ptcal_stop_tok = rtas_token("ibm,cbe-stop-ptcal");

	if (ptcal_start_tok == RTAS_UNKNOWN_SERVICE
			|| ptcal_stop_tok == RTAS_UNKNOWN_SERVICE)
		return -ENODEV;

	ret = register_reboot_notifier(&cbe_ptcal_reboot_notifier);
	if (ret)
		goto out1;

	ret = crash_shutdown_register(&cbe_ptcal_crash_shutdown);
	if (ret)
		goto out2;

	return cbe_ptcal_enable();

out2:
	unregister_reboot_notifier(&cbe_ptcal_reboot_notifier);
out1:
	printk(KERN_ERR "Can't disable PTCAL, so not enabling\n");
	return ret;
}

arch_initcall(cbe_ptcal_init);

void __init cbe_ras_init(void)
{
	unsigned long hid0;

	/*
	 * Enable System Error & thermal interrupts and wakeup conditions
	 */

	hid0 = mfspr(SPRN_HID0);
	hid0 |= HID0_CBE_THERM_INT_EN | HID0_CBE_THERM_WAKEUP |
		HID0_CBE_SYSERR_INT_EN | HID0_CBE_SYSERR_WAKEUP;
	mtspr(SPRN_HID0, hid0);
	mb();

	/*
	 * Install machine check handler. Leave setting of precise mode to
	 * what the firmware did for now
	 */
	ppc_md.machine_check_exception = cbe_machine_check_handler;
	mb();

	/*
	 * For now, we assume that IOC_FIR is already set to forward some
	 * error conditions to the System Error handler. If that is not true
	 * then it will have to be fixed up here.
	 */
}
