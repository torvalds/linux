/*
 * Alpha specific irq code.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>

#include <asm/machvec.h>
#include <asm/dma.h>
#include <asm/perf_event.h>
#include <asm/mce.h>

#include "proto.h"
#include "irq_impl.h"

/* Hack minimum IPL during interrupt processing for broken hardware.  */
#ifdef CONFIG_ALPHA_BROKEN_IRQ_MASK
int __min_ipl;
EXPORT_SYMBOL(__min_ipl);
#endif

/*
 * Performance counter hook.  A module can override this to
 * do something useful.
 */
static void
dummy_perf(unsigned long vector, struct pt_regs *regs)
{
	irq_err_count++;
	printk(KERN_CRIT "Performance counter interrupt!\n");
}

void (*perf_irq)(unsigned long, struct pt_regs *) = dummy_perf;
EXPORT_SYMBOL(perf_irq);

/*
 * The main interrupt entry point.
 */

asmlinkage void 
do_entInt(unsigned long type, unsigned long vector,
	  unsigned long la_ptr, struct pt_regs *regs)
{
	struct pt_regs *old_regs;

	/*
	 * Disable interrupts during IRQ handling.
	 * Note that there is no matching local_irq_enable() due to
	 * severe problems with RTI at IPL0 and some MILO PALcode
	 * (namely LX164).
	 */
	local_irq_disable();
	switch (type) {
	case 0:
#ifdef CONFIG_SMP
		handle_ipi(regs);
		return;
#else
		irq_err_count++;
		printk(KERN_CRIT "Interprocessor interrupt? "
		       "You must be kidding!\n");
#endif
		break;
	case 1:
		old_regs = set_irq_regs(regs);
		handle_irq(RTC_IRQ);
		set_irq_regs(old_regs);
		return;
	case 2:
		old_regs = set_irq_regs(regs);
		alpha_mv.machine_check(vector, la_ptr);
		set_irq_regs(old_regs);
		return;
	case 3:
		old_regs = set_irq_regs(regs);
		alpha_mv.device_interrupt(vector);
		set_irq_regs(old_regs);
		return;
	case 4:
		perf_irq(la_ptr, regs);
		return;
	default:
		printk(KERN_CRIT "Hardware intr %ld %lx? Huh?\n",
		       type, vector);
	}
	printk(KERN_CRIT "PC = %016lx PS=%04lx\n", regs->pc, regs->ps);
}

void __init
common_init_isa_dma(void)
{
	outb(0, DMA1_RESET_REG);
	outb(0, DMA2_RESET_REG);
	outb(0, DMA1_CLR_MASK_REG);
	outb(0, DMA2_CLR_MASK_REG);
}

void __init
init_IRQ(void)
{
	/* Just in case the platform init_irq() causes interrupts/mchecks
	   (as is the case with RAWHIDE, at least).  */
	wrent(entInt, 0);

	alpha_mv.init_irq();
}

/*
 * machine error checks
 */
#define MCHK_K_TPERR           0x0080
#define MCHK_K_TCPERR          0x0082
#define MCHK_K_HERR            0x0084
#define MCHK_K_ECC_C           0x0086
#define MCHK_K_ECC_NC          0x0088
#define MCHK_K_OS_BUGCHECK     0x008A
#define MCHK_K_PAL_BUGCHECK    0x0090

#ifndef CONFIG_SMP
struct mcheck_info __mcheck_info;
#endif

void
process_mcheck_info(unsigned long vector, unsigned long la_ptr,
		    const char *machine, int expected)
{
	struct el_common *mchk_header;
	const char *reason;

	/*
	 * See if the machine check is due to a badaddr() and if so,
	 * ignore it.
	 */

#ifdef CONFIG_VERBOSE_MCHECK
	if (alpha_verbose_mcheck > 1) {
		printk(KERN_CRIT "%s machine check %s\n", machine,
		       expected ? "expected." : "NOT expected!!!");
	}
#endif

	if (expected) {
		int cpu = smp_processor_id();
		mcheck_expected(cpu) = 0;
		mcheck_taken(cpu) = 1;
		return;
	}

	mchk_header = (struct el_common *)la_ptr;

	printk(KERN_CRIT "%s machine check: vector=0x%lx pc=0x%lx code=0x%x\n",
	       machine, vector, get_irq_regs()->pc, mchk_header->code);

	switch (mchk_header->code) {
	/* Machine check reasons.  Defined according to PALcode sources.  */
	case 0x80: reason = "tag parity error"; break;
	case 0x82: reason = "tag control parity error"; break;
	case 0x84: reason = "generic hard error"; break;
	case 0x86: reason = "correctable ECC error"; break;
	case 0x88: reason = "uncorrectable ECC error"; break;
	case 0x8A: reason = "OS-specific PAL bugcheck"; break;
	case 0x90: reason = "callsys in kernel mode"; break;
	case 0x96: reason = "i-cache read retryable error"; break;
	case 0x98: reason = "processor detected hard error"; break;
	
	/* System specific (these are for Alcor, at least): */
	case 0x202: reason = "system detected hard error"; break;
	case 0x203: reason = "system detected uncorrectable ECC error"; break;
	case 0x204: reason = "SIO SERR occurred on PCI bus"; break;
	case 0x205: reason = "parity error detected by core logic"; break;
	case 0x206: reason = "SIO IOCHK occurred on ISA bus"; break;
	case 0x207: reason = "non-existent memory error"; break;
	case 0x208: reason = "MCHK_K_DCSR"; break;
	case 0x209: reason = "PCI SERR detected"; break;
	case 0x20b: reason = "PCI data parity error detected"; break;
	case 0x20d: reason = "PCI address parity error detected"; break;
	case 0x20f: reason = "PCI master abort error"; break;
	case 0x211: reason = "PCI target abort error"; break;
	case 0x213: reason = "scatter/gather PTE invalid error"; break;
	case 0x215: reason = "flash ROM write error"; break;
	case 0x217: reason = "IOA timeout detected"; break;
	case 0x219: reason = "IOCHK#, EISA add-in board parity or other catastrophic error"; break;
	case 0x21b: reason = "EISA fail-safe timer timeout"; break;
	case 0x21d: reason = "EISA bus time-out"; break;
	case 0x21f: reason = "EISA software generated NMI"; break;
	case 0x221: reason = "unexpected ev5 IRQ[3] interrupt"; break;
	default: reason = "unknown"; break;
	}

	printk(KERN_CRIT "machine check type: %s%s\n",
	       reason, mchk_header->retry ? " (retryable)" : "");

	dik_show_regs(get_irq_regs(), NULL);

#ifdef CONFIG_VERBOSE_MCHECK
	if (alpha_verbose_mcheck > 1) {
		/* Dump the logout area to give all info.  */
		unsigned long *ptr = (unsigned long *)la_ptr;
		long i;
		for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
			printk(KERN_CRIT "   +%8lx %016lx %016lx\n",
			       i*sizeof(long), ptr[i], ptr[i+1]);
		}
	}
#endif /* CONFIG_VERBOSE_MCHECK */
}

/*
 * The special RTC interrupt type.  The interrupt itself was
 * processed by PALcode, and comes in via entInt vector 1.
 */

struct irqaction timer_irqaction = {
	.handler	= rtc_timer_interrupt,
	.name		= "timer",
};

void __init
init_rtc_irq(void)
{
	irq_set_chip_and_handler_name(RTC_IRQ, &dummy_irq_chip,
				      handle_percpu_irq, "RTC");
	setup_irq(RTC_IRQ, &timer_irqaction);
}

/* Dummy irqactions.  */
struct irqaction isa_cascade_irqaction = {
	.handler	= no_action,
	.name		= "isa-cascade"
};

struct irqaction timer_cascade_irqaction = {
	.handler	= no_action,
	.name		= "timer-cascade"
};

struct irqaction halt_switch_irqaction = {
	.handler	= no_action,
	.name		= "halt-switch"
};
