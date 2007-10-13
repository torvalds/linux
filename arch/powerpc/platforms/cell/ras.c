#define DEBUG

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/reboot.h>

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
	printk(KERN_ERR "Global Checkstop FIR    : 0x%016lx\n",
	       in_be64(&pregs->checkstop_fir));
	printk(KERN_ERR "Global Recoverable FIR  : 0x%016lx\n",
	       in_be64(&pregs->checkstop_fir));
	printk(KERN_ERR "Global MachineCheck FIR : 0x%016lx\n",
	       in_be64(&pregs->spec_att_mchk_fir));

	if (iregs == NULL)
		return;
	printk(KERN_ERR "IOC FIR                 : 0x%016lx\n",
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

#ifdef CONFIG_CRASH_DUMP
	rtas_call(ptcal_stop_tok, 1, 1, NULL, nid);
#endif

	area = kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		goto out_err;

	area->nid = nid;
	area->order = order;
	area->pages = alloc_pages_node(area->nid, GFP_KERNEL, area->order);

	if (!area->pages)
		goto out_free_area;

	addr = __pa(page_address(area->pages));

	ret = -EIO;
	if (rtas_call(ptcal_start_tok, 3, 1, NULL, area->nid,
				(unsigned int)(addr >> 32),
				(unsigned int)(addr & 0xffffffff))) {
		printk(KERN_ERR "%s: error enabling PTCAL on node %d!\n",
				__FUNCTION__, nid);
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
	if (!size)
		return -ENODEV;

	pr_debug("%s: enabling PTCAL, size = 0x%x\n", __FUNCTION__, *size);
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
			printk(KERN_ERR "%s: node %s is missing node-id?\n",
					__FUNCTION__, np->full_name);
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

	pr_debug("%s: disabling PTCAL\n", __FUNCTION__);

	list_for_each_entry_safe(area, tmp, &ptcal_list, list) {
		/* disable ptcal on this node */
		if (rtas_call(ptcal_stop_tok, 1, 1, NULL, area->nid)) {
			printk(KERN_ERR "%s: error disabling PTCAL "
					"on node %d!\n", __FUNCTION__,
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

static struct notifier_block cbe_ptcal_reboot_notifier = {
	.notifier_call = cbe_ptcal_notify_reboot
};

int __init cbe_ptcal_init(void)
{
	int ret;
	ptcal_start_tok = rtas_token("ibm,cbe-start-ptcal");
	ptcal_stop_tok = rtas_token("ibm,cbe-stop-ptcal");

	if (ptcal_start_tok == RTAS_UNKNOWN_SERVICE
			|| ptcal_stop_tok == RTAS_UNKNOWN_SERVICE)
		return -ENODEV;

	ret = register_reboot_notifier(&cbe_ptcal_reboot_notifier);
	if (ret) {
		printk(KERN_ERR "Can't disable PTCAL, so not enabling\n");
		return ret;
	}

	return cbe_ptcal_enable();
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
