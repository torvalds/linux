/*
 *	linux/arch/alpha/kernel/sys_jensen.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *	Copyright (C) 1998, 1999 Richard Henderson
 *
 * Code supporting the Jensen.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/jensen.h>
#undef  __EXTERN_INLINE

#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


/*
 * Jensen is special: the vector is 0x8X0 for EISA interrupt X, and
 * 0x9X0 for the local motherboard interrupts.
 *
 * Note especially that those local interrupts CANNOT be masked,
 * which causes much of the pain below...
 *
 *	0x660 - NMI
 *
 *	0x800 - IRQ0  interval timer (not used, as we use the RTC timer)
 *	0x810 - IRQ1  line printer (duh..)
 *	0x860 - IRQ6  floppy disk
 *
 *	0x900 - COM1
 *	0x920 - COM2
 *	0x980 - keyboard
 *	0x990 - mouse
 *
 * PCI-based systems are more sane: they don't have the local
 * interrupts at all, and have only normal PCI interrupts from
 * devices.  Happily it's easy enough to do a sane mapping from the
 * Jensen.
 * 
 * Note that this means that we may have to do a hardware
 * "local_op" to a different interrupt than we report to the rest of the
 * world.
 */

static unsigned int
jensen_local_startup(unsigned int irq)
{
	/* the parport is really hw IRQ 1, silly Jensen.  */
	if (irq == 7)
		i8259a_startup_irq(1);
	else
		/*
		 * For all true local interrupts, set the flag that prevents
		 * the IPL from being dropped during handler processing.
		 */
		if (irq_desc[irq].action)
			irq_desc[irq].action->flags |= IRQF_DISABLED;
	return 0;
}

static void
jensen_local_shutdown(unsigned int irq)
{
	/* the parport is really hw IRQ 1, silly Jensen.  */
	if (irq == 7)
		i8259a_disable_irq(1);
}

static void
jensen_local_enable(unsigned int irq)
{
	/* the parport is really hw IRQ 1, silly Jensen.  */
	if (irq == 7)
		i8259a_enable_irq(1);
}

static void
jensen_local_disable(unsigned int irq)
{
	/* the parport is really hw IRQ 1, silly Jensen.  */
	if (irq == 7)
		i8259a_disable_irq(1);
}

static void
jensen_local_ack(unsigned int irq)
{
	/* the parport is really hw IRQ 1, silly Jensen.  */
	if (irq == 7)
		i8259a_mask_and_ack_irq(1);
}

static void
jensen_local_end(unsigned int irq)
{
	/* the parport is really hw IRQ 1, silly Jensen.  */
	if (irq == 7)
		i8259a_end_irq(1);
}

static struct hw_interrupt_type jensen_local_irq_type = {
	.typename	= "LOCAL",
	.startup	= jensen_local_startup,
	.shutdown	= jensen_local_shutdown,
	.enable		= jensen_local_enable,
	.disable	= jensen_local_disable,
	.ack		= jensen_local_ack,
	.end		= jensen_local_end,
};

static void 
jensen_device_interrupt(unsigned long vector)
{
	int irq;

	switch (vector) {
	case 0x660:
		printk("Whee.. NMI received. Probable hardware error\n");
		printk("61=%02x, 461=%02x\n", inb(0x61), inb(0x461));
		return;

	/* local device interrupts: */
	case 0x900: irq = 4; break;		/* com1 -> irq 4 */
	case 0x920: irq = 3; break;		/* com2 -> irq 3 */
	case 0x980: irq = 1; break;		/* kbd -> irq 1 */
	case 0x990: irq = 9; break;		/* mouse -> irq 9 */

	default:
		if (vector > 0x900) {
			printk("Unknown local interrupt %lx\n", vector);
			return;
		}

		irq = (vector - 0x800) >> 4;
		if (irq == 1)
			irq = 7;
		break;
	}

	/* If there is no handler yet... */
	if (irq_desc[irq].action == NULL) {
	    /* If it is a local interrupt that cannot be masked... */
	    if (vector >= 0x900)
	    {
	        /* Clear keyboard/mouse state */
	    	inb(0x64);
		inb(0x60);
		/* Reset serial ports */
		inb(0x3fa);
		inb(0x2fa);
		outb(0x0c, 0x3fc);
		outb(0x0c, 0x2fc);
		/* Clear NMI */
		outb(0,0x61);
		outb(0,0x461);
	    }
	}

#if 0
        /* A useful bit of code to find out if an interrupt is going wild.  */
        {
          static unsigned int last_msg = 0, last_cc = 0;
          static int last_irq = -1, count = 0;
          unsigned int cc;

          __asm __volatile("rpcc %0" : "=r"(cc));
          ++count;
#define JENSEN_CYCLES_PER_SEC	(150000000)
          if (cc - last_msg > ((JENSEN_CYCLES_PER_SEC) * 3) ||
	      irq != last_irq) {
                printk(KERN_CRIT " irq %d count %d cc %u @ %lx\n",
                       irq, count, cc-last_cc, get_irq_regs()->pc);
                count = 0;
                last_msg = cc;
                last_irq = irq;
          }
          last_cc = cc;
        }
#endif

	handle_irq(irq);
}

static void __init
jensen_init_irq(void)
{
	init_i8259a_irqs();

	irq_desc[1].chip = &jensen_local_irq_type;
	irq_desc[4].chip = &jensen_local_irq_type;
	irq_desc[3].chip = &jensen_local_irq_type;
	irq_desc[7].chip = &jensen_local_irq_type;
	irq_desc[9].chip = &jensen_local_irq_type;

	common_init_isa_dma();
}

static void __init
jensen_init_arch(void)
{
	struct pci_controller *hose;
#ifdef CONFIG_PCI
	static struct pci_dev fake_isa_bridge = { .dma_mask = 0xffffffffUL, };

	isa_bridge = &fake_isa_bridge;
#endif

	/* Create a hose so that we can report i/o base addresses to
	   userland.  */

	pci_isa_hose = hose = alloc_pci_controller();
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->index = 0;

	hose->sparse_mem_base = EISA_MEM - IDENT_ADDR;
	hose->dense_mem_base = 0;
	hose->sparse_io_base = EISA_IO - IDENT_ADDR;
	hose->dense_io_base = 0;

	hose->sg_isa = hose->sg_pci = NULL;
	__direct_map_base = 0;
	__direct_map_size = 0xffffffff;
}

static void
jensen_machine_check(unsigned long vector, unsigned long la)
{
	printk(KERN_CRIT "Machine check\n");
}

/*
 * The System Vector
 */

struct alpha_machine_vector jensen_mv __initmv = {
	.vector_name		= "Jensen",
	DO_EV4_MMU,
	IO_LITE(JENSEN,jensen),
	.machine_check		= jensen_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.rtc_port		= 0x170,
	.rtc_get_time		= common_get_rtc_time,
	.rtc_set_time		= common_set_rtc_time,

	.nr_irqs		= 16,
	.device_interrupt	= jensen_device_interrupt,

	.init_arch		= jensen_init_arch,
	.init_irq		= jensen_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= NULL,
	.kill_arch		= NULL,
};
ALIAS_MV(jensen)
