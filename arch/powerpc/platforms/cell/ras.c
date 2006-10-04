#define DEBUG

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/smp.h>

#include <asm/reg.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>

#include "ras.h"
#include "cbe_regs.h"


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
