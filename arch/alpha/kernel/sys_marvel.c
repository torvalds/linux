/*
 * linux/arch/alpha/kernel/sys_marvel.c
 *
 * Marvel / IO7 support
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
#include <asm/core_marvel.h>
#include <asm/hwrpb.h>
#include <asm/tlbflush.h>
#include <asm/vga.h>
#include <asm/rtc.h>

#include "proto.h"
#include "err_impl.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"

#if NR_IRQS < MARVEL_NR_IRQS
# error NR_IRQS < MARVEL_NR_IRQS !!!
#endif


/*
 * Interrupt handling.
 */
static void 
io7_device_interrupt(unsigned long vector)
{
	unsigned int pid;
	unsigned int irq;

	/*
	 * Vector is 0x800 + (interrupt)
	 *
	 * where (interrupt) is:
	 *
	 *	...16|15 14|13     4|3 0
	 *	-----+-----+--------+---
	 *	  PE |  0  |   irq  | 0
	 *
	 * where (irq) is 
	 *
	 *       0x0800 - 0x0ff0	 - 0x0800 + (LSI id << 4)
	 *	 0x1000 - 0x2ff0	 - 0x1000 + (MSI_DAT<8:0> << 4)
	 */
	pid = vector >> 16;
	irq = ((vector & 0xffff) - 0x800) >> 4;

	irq += 16;				/* offset for legacy */
	irq &= MARVEL_IRQ_VEC_IRQ_MASK;		/* not too many bits */
	irq |= pid << MARVEL_IRQ_VEC_PE_SHIFT;	/* merge the pid     */

	handle_irq(irq);
}

static volatile unsigned long *
io7_get_irq_ctl(unsigned int irq, struct io7 **pio7)
{
	volatile unsigned long *ctl;
	unsigned int pid;
	struct io7 *io7;

	pid = irq >> MARVEL_IRQ_VEC_PE_SHIFT;

	if (!(io7 = marvel_find_io7(pid))) {
		printk(KERN_ERR 
		       "%s for nonexistent io7 -- vec %x, pid %d\n",
		       __func__, irq, pid);
		return NULL;
	}

	irq &= MARVEL_IRQ_VEC_IRQ_MASK;	/* isolate the vector    */
	irq -= 16;			/* subtract legacy bias  */

	if (irq >= 0x180) {
		printk(KERN_ERR 
		       "%s for invalid irq -- pid %d adjusted irq %x\n",
		       __func__, pid, irq);
		return NULL;
	}

	ctl = &io7->csrs->PO7_LSI_CTL[irq & 0xff].csr; /* assume LSI */
	if (irq >= 0x80)	     	/* MSI */
		ctl = &io7->csrs->PO7_MSI_CTL[((irq - 0x80) >> 5) & 0x0f].csr;

	if (pio7) *pio7 = io7;
	return ctl;
}

static void
io7_enable_irq(struct irq_data *d)
{
	volatile unsigned long *ctl;
	unsigned int irq = d->irq;
	struct io7 *io7;

	ctl = io7_get_irq_ctl(irq, &io7);
	if (!ctl || !io7) {
		printk(KERN_ERR "%s: get_ctl failed for irq %x\n",
		       __func__, irq);
		return;
	}

	spin_lock(&io7->irq_lock);
	*ctl |= 1UL << 24;
	mb();
	*ctl;
	spin_unlock(&io7->irq_lock);
}

static void
io7_disable_irq(struct irq_data *d)
{
	volatile unsigned long *ctl;
	unsigned int irq = d->irq;
	struct io7 *io7;

	ctl = io7_get_irq_ctl(irq, &io7);
	if (!ctl || !io7) {
		printk(KERN_ERR "%s: get_ctl failed for irq %x\n",
		       __func__, irq);
		return;
	}

	spin_lock(&io7->irq_lock);
	*ctl &= ~(1UL << 24);
	mb();
	*ctl;
	spin_unlock(&io7->irq_lock);
}

static void
marvel_irq_noop(struct irq_data *d)
{
	return;
}

static struct irq_chip marvel_legacy_irq_type = {
	.name		= "LEGACY",
	.irq_mask	= marvel_irq_noop,
	.irq_unmask	= marvel_irq_noop,
};

static struct irq_chip io7_lsi_irq_type = {
	.name		= "LSI",
	.irq_unmask	= io7_enable_irq,
	.irq_mask	= io7_disable_irq,
	.irq_mask_ack	= io7_disable_irq,
};

static struct irq_chip io7_msi_irq_type = {
	.name		= "MSI",
	.irq_unmask	= io7_enable_irq,
	.irq_mask	= io7_disable_irq,
	.irq_ack	= marvel_irq_noop,
};

static void
io7_redirect_irq(struct io7 *io7, 
		 volatile unsigned long *csr, 
		 unsigned int where)
{
	unsigned long val;
	
	val = *csr;
	val &= ~(0x1ffUL << 24);		/* clear the target pid   */
	val |= ((unsigned long)where << 24);	/* set the new target pid */
	
	*csr = val;
	mb();
	*csr;
}

static void 
io7_redirect_one_lsi(struct io7 *io7, unsigned int which, unsigned int where)
{
	unsigned long val;

	/*
	 * LSI_CTL has target PID @ 14
	 */
	val = io7->csrs->PO7_LSI_CTL[which].csr;
	val &= ~(0x1ffUL << 14);		/* clear the target pid */
	val |= ((unsigned long)where << 14);	/* set the new target pid */

	io7->csrs->PO7_LSI_CTL[which].csr = val;
	mb();
	io7->csrs->PO7_LSI_CTL[which].csr;
}

static void 
io7_redirect_one_msi(struct io7 *io7, unsigned int which, unsigned int where)
{
	unsigned long val;

	/*
	 * MSI_CTL has target PID @ 14
	 */
	val = io7->csrs->PO7_MSI_CTL[which].csr;
	val &= ~(0x1ffUL << 14);		/* clear the target pid */
	val |= ((unsigned long)where << 14);	/* set the new target pid */

	io7->csrs->PO7_MSI_CTL[which].csr = val;
	mb();
	io7->csrs->PO7_MSI_CTL[which].csr;
}

static void __init
init_one_io7_lsi(struct io7 *io7, unsigned int which, unsigned int where)
{
	/*
	 * LSI_CTL has target PID @ 14
	 */
	io7->csrs->PO7_LSI_CTL[which].csr = ((unsigned long)where << 14);
	mb();
	io7->csrs->PO7_LSI_CTL[which].csr;
}

static void __init
init_one_io7_msi(struct io7 *io7, unsigned int which, unsigned int where)
{
	/*
	 * MSI_CTL has target PID @ 14
	 */
	io7->csrs->PO7_MSI_CTL[which].csr = ((unsigned long)where << 14);
	mb();
	io7->csrs->PO7_MSI_CTL[which].csr;
}

static void __init
init_io7_irqs(struct io7 *io7, 
	      struct irq_chip *lsi_ops,
	      struct irq_chip *msi_ops)
{
	long base = (io7->pe << MARVEL_IRQ_VEC_PE_SHIFT) + 16;
	long i;

	printk("Initializing interrupts for IO7 at PE %u - base %lx\n",
		io7->pe, base);

	/*
	 * Where should interrupts from this IO7 go?
	 *
	 * They really should be sent to the local CPU to avoid having to
	 * traverse the mesh, but if it's not an SMP kernel, they have to
	 * go to the boot CPU. Send them all to the boot CPU for now,
	 * as each secondary starts, it can redirect it's local device 
	 * interrupts.
	 */
	printk("  Interrupts reported to CPU at PE %u\n", boot_cpuid);

	spin_lock(&io7->irq_lock);

	/* set up the error irqs */
	io7_redirect_irq(io7, &io7->csrs->HLT_CTL.csr, boot_cpuid);
	io7_redirect_irq(io7, &io7->csrs->HPI_CTL.csr, boot_cpuid);
	io7_redirect_irq(io7, &io7->csrs->CRD_CTL.csr, boot_cpuid);
	io7_redirect_irq(io7, &io7->csrs->STV_CTL.csr, boot_cpuid);
	io7_redirect_irq(io7, &io7->csrs->HEI_CTL.csr, boot_cpuid);

	/* Set up the lsi irqs.  */
	for (i = 0; i < 128; ++i) {
		irq_set_chip_and_handler(base + i, lsi_ops, handle_level_irq);
		irq_set_status_flags(i, IRQ_LEVEL);
	}

	/* Disable the implemented irqs in hardware.  */
	for (i = 0; i < 0x60; ++i) 
		init_one_io7_lsi(io7, i, boot_cpuid);

	init_one_io7_lsi(io7, 0x74, boot_cpuid);
	init_one_io7_lsi(io7, 0x75, boot_cpuid);


	/* Set up the msi irqs.  */
	for (i = 128; i < (128 + 512); ++i) {
		irq_set_chip_and_handler(base + i, msi_ops, handle_level_irq);
		irq_set_status_flags(i, IRQ_LEVEL);
	}

	for (i = 0; i < 16; ++i)
		init_one_io7_msi(io7, i, boot_cpuid);

	spin_unlock(&io7->irq_lock);
}

static void __init
marvel_init_irq(void)
{
	int i;
	struct io7 *io7 = NULL;

	/* Reserve the legacy irqs.  */
	for (i = 0; i < 16; ++i) {
		irq_set_chip_and_handler(i, &marvel_legacy_irq_type,
					 handle_level_irq);
	}

	/* Init the io7 irqs.  */
	for (io7 = NULL; (io7 = marvel_next_io7(io7)) != NULL; )
		init_io7_irqs(io7, &io7_lsi_irq_type, &io7_msi_irq_type);
}

static int 
marvel_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pci_controller *hose = dev->sysdata;
	struct io7_port *io7_port = hose->sysdata;
	struct io7 *io7 = io7_port->io7;
	int msi_loc, msi_data_off;
	u16 msg_ctl;
	u16 msg_dat;
	u8 intline; 
	int irq;

	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &intline);
	irq = intline;

	msi_loc = pci_find_capability(dev, PCI_CAP_ID_MSI);
	msg_ctl = 0;
	if (msi_loc) 
		pci_read_config_word(dev, msi_loc + PCI_MSI_FLAGS, &msg_ctl);

	if (msg_ctl & PCI_MSI_FLAGS_ENABLE) {
 		msi_data_off = PCI_MSI_DATA_32;
		if (msg_ctl & PCI_MSI_FLAGS_64BIT) 
			msi_data_off = PCI_MSI_DATA_64;
		pci_read_config_word(dev, msi_loc + msi_data_off, &msg_dat);

		irq = msg_dat & 0x1ff;		/* we use msg_data<8:0> */
		irq += 0x80;			/* offset for lsi       */

#if 1
		printk("PCI:%d:%d:%d (hose %d) is using MSI\n",
		       dev->bus->number, 
		       PCI_SLOT(dev->devfn), 
		       PCI_FUNC(dev->devfn),
		       hose->index);
		printk("  %d message(s) from 0x%04x\n", 
		       1 << ((msg_ctl & PCI_MSI_FLAGS_QSIZE) >> 4),
		       msg_dat);
		printk("  reporting on %d IRQ(s) from %d (0x%x)\n", 
		       1 << ((msg_ctl & PCI_MSI_FLAGS_QSIZE) >> 4),
		       (irq + 16) | (io7->pe << MARVEL_IRQ_VEC_PE_SHIFT),
		       (irq + 16) | (io7->pe << MARVEL_IRQ_VEC_PE_SHIFT));
#endif

#if 0
		pci_write_config_word(dev, msi_loc + PCI_MSI_FLAGS,
				      msg_ctl & ~PCI_MSI_FLAGS_ENABLE);
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &intline);
		irq = intline;

		printk("  forcing LSI interrupt on irq %d [0x%x]\n", irq, irq);
#endif
	}

	irq += 16;					/* offset for legacy */
	irq |= io7->pe << MARVEL_IRQ_VEC_PE_SHIFT;	/* merge the pid     */

	return irq; 
}

static void __init
marvel_init_pci(void)
{
	struct io7 *io7;

	marvel_register_error_handlers();

	pci_probe_only = 1;
	common_init_pci();
	locate_and_init_vga(NULL);

	/* Clear any io7 errors.  */
	for (io7 = NULL; (io7 = marvel_next_io7(io7)) != NULL; ) 
		io7_clear_errors(io7);
}

static void __init
marvel_init_rtc(void)
{
	init_rtc_irq();
}

struct marvel_rtc_time {
	struct rtc_time *time;
	int retval;
};

#ifdef CONFIG_SMP
static void
smp_get_rtc_time(void *data)
{
	struct marvel_rtc_time *mrt = data;
	mrt->retval = __get_rtc_time(mrt->time);
}

static void
smp_set_rtc_time(void *data)
{
	struct marvel_rtc_time *mrt = data;
	mrt->retval = __set_rtc_time(mrt->time);
}
#endif

static unsigned int
marvel_get_rtc_time(struct rtc_time *time)
{
#ifdef CONFIG_SMP
	struct marvel_rtc_time mrt;

	if (smp_processor_id() != boot_cpuid) {
		mrt.time = time;
		smp_call_function_single(boot_cpuid, smp_get_rtc_time, &mrt, 1);
		return mrt.retval;
	}
#endif
	return __get_rtc_time(time);
}

static int
marvel_set_rtc_time(struct rtc_time *time)
{
#ifdef CONFIG_SMP
	struct marvel_rtc_time mrt;

	if (smp_processor_id() != boot_cpuid) {
		mrt.time = time;
		smp_call_function_single(boot_cpuid, smp_set_rtc_time, &mrt, 1);
		return mrt.retval;
	}
#endif
	return __set_rtc_time(time);
}

static void
marvel_smp_callin(void)
{
	int cpuid = hard_smp_processor_id();
	struct io7 *io7 = marvel_find_io7(cpuid);
	unsigned int i;

	if (!io7)
		return;

	/* 
	 * There is a local IO7 - redirect all of its interrupts here.
	 */
	printk("Redirecting IO7 interrupts to local CPU at PE %u\n", cpuid);

	/* Redirect the error IRQS here.  */
	io7_redirect_irq(io7, &io7->csrs->HLT_CTL.csr, cpuid);
	io7_redirect_irq(io7, &io7->csrs->HPI_CTL.csr, cpuid);
	io7_redirect_irq(io7, &io7->csrs->CRD_CTL.csr, cpuid);
	io7_redirect_irq(io7, &io7->csrs->STV_CTL.csr, cpuid);
	io7_redirect_irq(io7, &io7->csrs->HEI_CTL.csr, cpuid);

	/* Redirect the implemented LSIs here.  */
	for (i = 0; i < 0x60; ++i) 
		io7_redirect_one_lsi(io7, i, cpuid);

	io7_redirect_one_lsi(io7, 0x74, cpuid);
	io7_redirect_one_lsi(io7, 0x75, cpuid);

	/* Redirect the MSIs here.  */
	for (i = 0; i < 16; ++i)
		io7_redirect_one_msi(io7, i, cpuid);
}

/*
 * System Vectors
 */
struct alpha_machine_vector marvel_ev7_mv __initmv = {
	.vector_name		= "MARVEL/EV7",
	DO_EV7_MMU,
	.rtc_port		= 0x70,
	.rtc_get_time		= marvel_get_rtc_time,
	.rtc_set_time		= marvel_set_rtc_time,
	DO_MARVEL_IO,
	.machine_check		= marvel_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= DEFAULT_MEM_BASE,
	.pci_dac_offset		= IO7_DAC_OFFSET,

	.nr_irqs		= MARVEL_NR_IRQS,
	.device_interrupt	= io7_device_interrupt,

	.agp_info		= marvel_agp_info,

	.smp_callin		= marvel_smp_callin,
	.init_arch		= marvel_init_arch,
	.init_irq		= marvel_init_irq,
	.init_rtc		= marvel_init_rtc,
	.init_pci		= marvel_init_pci,
	.kill_arch		= marvel_kill_arch,
	.pci_map_irq		= marvel_map_irq,
	.pci_swizzle		= common_swizzle,

	.pa_to_nid		= marvel_pa_to_nid,
	.cpuid_to_nid		= marvel_cpuid_to_nid,
	.node_mem_start		= marvel_node_mem_start,
	.node_mem_size		= marvel_node_mem_size,
};
ALIAS_MV(marvel_ev7)
