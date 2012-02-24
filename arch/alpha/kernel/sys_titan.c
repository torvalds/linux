/*
 *	linux/arch/alpha/kernel/sys_titan.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996, 1999 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *      Copyright (C) 1999, 2000 Jeff Wiedemeier
 *
 * Code supporting TITAN systems (EV6+TITAN), currently:
 *      Privateer
 *	Falcon
 *	Granite
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_titan.h>
#include <asm/hwrpb.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"
#include "err_impl.h"


/*
 * Titan generic
 */

/*
 * Titan supports up to 4 CPUs
 */
static unsigned long titan_cpu_irq_affinity[4] = { ~0UL, ~0UL, ~0UL, ~0UL };

/*
 * Mask is set (1) if enabled
 */
static unsigned long titan_cached_irq_mask;

/*
 * Need SMP-safe access to interrupt CSRs
 */
DEFINE_SPINLOCK(titan_irq_lock);

static void
titan_update_irq_hw(unsigned long mask)
{
	register titan_cchip *cchip = TITAN_cchip;
	unsigned long isa_enable = 1UL << 55;
	register int bcpu = boot_cpuid;

#ifdef CONFIG_SMP
	cpumask_t cpm;
	volatile unsigned long *dim0, *dim1, *dim2, *dim3;
	unsigned long mask0, mask1, mask2, mask3, dummy;

	cpumask_copy(&cpm, cpu_present_mask);
	mask &= ~isa_enable;
	mask0 = mask & titan_cpu_irq_affinity[0];
	mask1 = mask & titan_cpu_irq_affinity[1];
	mask2 = mask & titan_cpu_irq_affinity[2];
	mask3 = mask & titan_cpu_irq_affinity[3];

	if (bcpu == 0) mask0 |= isa_enable;
	else if (bcpu == 1) mask1 |= isa_enable;
	else if (bcpu == 2) mask2 |= isa_enable;
	else mask3 |= isa_enable;

	dim0 = &cchip->dim0.csr;
	dim1 = &cchip->dim1.csr;
	dim2 = &cchip->dim2.csr;
	dim3 = &cchip->dim3.csr;
	if (!cpumask_test_cpu(0, &cpm)) dim0 = &dummy;
	if (!cpumask_test_cpu(1, &cpm)) dim1 = &dummy;
	if (!cpumask_test_cpu(2, &cpm)) dim2 = &dummy;
	if (!cpumask_test_cpu(3, &cpm)) dim3 = &dummy;

	*dim0 = mask0;
	*dim1 = mask1;
	*dim2 = mask2;
	*dim3 = mask3;
	mb();
	*dim0;
	*dim1;
	*dim2;
	*dim3;
#else
	volatile unsigned long *dimB;
	dimB = &cchip->dim0.csr;
	if (bcpu == 1) dimB = &cchip->dim1.csr;
	else if (bcpu == 2) dimB = &cchip->dim2.csr;
	else if (bcpu == 3) dimB = &cchip->dim3.csr;

	*dimB = mask | isa_enable;
	mb();
	*dimB;
#endif
}

static inline void
titan_enable_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
	spin_lock(&titan_irq_lock);
	titan_cached_irq_mask |= 1UL << (irq - 16);
	titan_update_irq_hw(titan_cached_irq_mask);
	spin_unlock(&titan_irq_lock);
}

static inline void
titan_disable_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
	spin_lock(&titan_irq_lock);
	titan_cached_irq_mask &= ~(1UL << (irq - 16));
	titan_update_irq_hw(titan_cached_irq_mask);
	spin_unlock(&titan_irq_lock);
}

static void
titan_cpu_set_irq_affinity(unsigned int irq, cpumask_t affinity)
{
	int cpu;

	for (cpu = 0; cpu < 4; cpu++) {
		if (cpumask_test_cpu(cpu, &affinity))
			titan_cpu_irq_affinity[cpu] |= 1UL << irq;
		else
			titan_cpu_irq_affinity[cpu] &= ~(1UL << irq);
	}

}

static int
titan_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity,
		       bool force)
{ 
	unsigned int irq = d->irq;
	spin_lock(&titan_irq_lock);
	titan_cpu_set_irq_affinity(irq - 16, *affinity);
	titan_update_irq_hw(titan_cached_irq_mask);
	spin_unlock(&titan_irq_lock);

	return 0;
}

static void
titan_device_interrupt(unsigned long vector)
{
	printk("titan_device_interrupt: NOT IMPLEMENTED YET!!\n");
}

static void 
titan_srm_device_interrupt(unsigned long vector)
{
	int irq;

	irq = (vector - 0x800) >> 4;
	handle_irq(irq);
}


static void __init
init_titan_irqs(struct irq_chip * ops, int imin, int imax)
{
	long i;
	for (i = imin; i <= imax; ++i) {
		irq_set_chip_and_handler(i, ops, handle_level_irq);
		irq_set_status_flags(i, IRQ_LEVEL);
	}
}

static struct irq_chip titan_irq_type = {
       .name			= "TITAN",
       .irq_unmask		= titan_enable_irq,
       .irq_mask		= titan_disable_irq,
       .irq_mask_ack		= titan_disable_irq,
       .irq_set_affinity	= titan_set_irq_affinity,
};

static irqreturn_t
titan_intr_nop(int irq, void *dev_id)
{
      /*
       * This is a NOP interrupt handler for the purposes of
       * event counting -- just return.
       */                                                                     
       return IRQ_HANDLED;
}

static void __init
titan_init_irq(void)
{
	if (alpha_using_srm && !alpha_mv.device_interrupt)
		alpha_mv.device_interrupt = titan_srm_device_interrupt;
	if (!alpha_mv.device_interrupt)
		alpha_mv.device_interrupt = titan_device_interrupt;

	titan_update_irq_hw(0);

	init_titan_irqs(&titan_irq_type, 16, 63 + 16);
}
  
static void __init
titan_legacy_init_irq(void)
{
	/* init the legacy dma controller */
	outb(0, DMA1_RESET_REG);
	outb(0, DMA2_RESET_REG);
	outb(DMA_MODE_CASCADE, DMA2_MODE_REG);
	outb(0, DMA2_MASK_REG);

	/* init the legacy irq controller */
	init_i8259a_irqs();

	/* init the titan irqs */
	titan_init_irq();
}

void
titan_dispatch_irqs(u64 mask)
{
	unsigned long vector;

	/*
	 * Mask down to those interrupts which are enable on this processor
	 */
	mask &= titan_cpu_irq_affinity[smp_processor_id()];

	/*
	 * Dispatch all requested interrupts 
	 */
	while (mask) {
		/* convert to SRM vector... priority is <63> -> <0> */
		vector = 63 - __kernel_ctlz(mask);
		mask &= ~(1UL << vector);	/* clear it out 	 */
		vector = 0x900 + (vector << 4);	/* convert to SRM vector */
		
		/* dispatch it */
		alpha_mv.device_interrupt(vector);
	}
}
  

/*
 * Titan Family
 */
static void __init
titan_request_irq(unsigned int irq, irq_handler_t handler,
		  unsigned long irqflags, const char *devname,
		  void *dev_id)
{
	int err;
	err = request_irq(irq, handler, irqflags, devname, dev_id);
	if (err) {
		printk("titan_request_irq for IRQ %d returned %d; ignoring\n",
		       irq, err);
	}
}

static void __init
titan_late_init(void)
{
	/*
	 * Enable the system error interrupts. These interrupts are 
	 * all reported to the kernel as machine checks, so the handler
	 * is a nop so it can be called to count the individual events.
	 */
	titan_request_irq(63+16, titan_intr_nop, IRQF_DISABLED,
		    "CChip Error", NULL);
	titan_request_irq(62+16, titan_intr_nop, IRQF_DISABLED,
		    "PChip 0 H_Error", NULL);
	titan_request_irq(61+16, titan_intr_nop, IRQF_DISABLED,
		    "PChip 1 H_Error", NULL);
	titan_request_irq(60+16, titan_intr_nop, IRQF_DISABLED,
		    "PChip 0 C_Error", NULL);
	titan_request_irq(59+16, titan_intr_nop, IRQF_DISABLED,
		    "PChip 1 C_Error", NULL);

	/* 
	 * Register our error handlers.
	 */
	titan_register_error_handlers();

	/*
	 * Check if the console left us any error logs.
	 */
	cdl_check_console_data_log();

}

static int __devinit
titan_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	u8 intline;
	int irq;

 	/* Get the current intline.  */
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &intline);
	irq = intline;

 	/* Is it explicitly routed through ISA?  */
 	if ((irq & 0xF0) == 0xE0)
 		return irq;
 
 	/* Offset by 16 to make room for ISA interrupts 0 - 15.  */
 	return irq + 16;
}

static void __init
titan_init_pci(void)
{
 	/*
 	 * This isn't really the right place, but there's some init
 	 * that needs to be done after everything is basically up.
 	 */
 	titan_late_init();
 
	/* Indicate that we trust the console to configure things properly */
	pci_set_flags(PCI_PROBE_ONLY);
	common_init_pci();
	SMC669_Init(0);
	locate_and_init_vga(NULL);
}


/*
 * Privateer
 */
static void __init
privateer_init_pci(void)
{
	/*
	 * Hook a couple of extra err interrupts that the
	 * common titan code won't.
	 */
	titan_request_irq(53+16, titan_intr_nop, IRQF_DISABLED,
		    "NMI", NULL);
	titan_request_irq(50+16, titan_intr_nop, IRQF_DISABLED,
		    "Temperature Warning", NULL);

	/*
	 * Finish with the common version.
	 */
	return titan_init_pci();
}


/*
 * The System Vectors.
 */
struct alpha_machine_vector titan_mv __initmv = {
	.vector_name		= "TITAN",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_TITAN_IO,
	.machine_check		= titan_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= DEFAULT_MEM_BASE,
	.pci_dac_offset		= TITAN_DAC_OFFSET,

	.nr_irqs		= 80,	/* 64 + 16 */
	/* device_interrupt will be filled in by titan_init_irq */

	.agp_info		= titan_agp_info,

	.init_arch		= titan_init_arch,
	.init_irq		= titan_legacy_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= titan_init_pci,

	.kill_arch		= titan_kill_arch,
	.pci_map_irq		= titan_map_irq,
	.pci_swizzle		= common_swizzle,
};
ALIAS_MV(titan)

struct alpha_machine_vector privateer_mv __initmv = {
	.vector_name		= "PRIVATEER",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_TITAN_IO,
	.machine_check		= privateer_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= DEFAULT_MEM_BASE,
	.pci_dac_offset		= TITAN_DAC_OFFSET,

	.nr_irqs		= 80,	/* 64 + 16 */
	/* device_interrupt will be filled in by titan_init_irq */

	.agp_info		= titan_agp_info,

	.init_arch		= titan_init_arch,
	.init_irq		= titan_legacy_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= privateer_init_pci,

	.kill_arch		= titan_kill_arch,
	.pci_map_irq		= titan_map_irq,
	.pci_swizzle		= common_swizzle,
};
/* No alpha_mv alias for privateer since we compile it 
   in unconditionally with titan; setup_arch knows how to cope. */
