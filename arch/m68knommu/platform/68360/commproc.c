/*
 * General Purpose functions for the global management of the
 * Communication Processor Module.
 *
 * Copyright (c) 2000 Michael Leslie <mleslie@lineo.com>
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * In addition to the individual control of the communication
 * channels, there are a few functions that globally affect the
 * communication processor.
 *
 * Buffer descriptors must be allocated from the dual ported memory
 * space.  The allocator for that is here.  When the communication
 * process is reset, we reclaim the memory available.  There is
 * currently no deallocator for this memory.
 * The amount of space available is platform dependent.  On the
 * MBX, the EPPC software loads additional microcode into the
 * communication processor, and uses some of the DP ram for this
 * purpose.  Current, the first 512 bytes and the last 256 bytes of
 * memory are used.  Right now I am conservative and only use the
 * memory that can never be used for microcode.  If there are
 * applications that require more DP ram, we can expand the boundaries
 * but then we have to be careful of any downloaded microcode.
 *
 */

/*
 * Michael Leslie <mleslie@lineo.com>
 * adapted Dan Malek's ppc8xx drivers to M68360
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/m68360.h>
#include <asm/commproc.h>

/* #include <asm/page.h> */
/* #include <asm/pgtable.h> */
extern void *_quicc_base;
extern unsigned int system_clock;


static uint dp_alloc_base;	/* Starting offset in DP ram */
static uint dp_alloc_top;	/* Max offset + 1 */

#if 0
static	void	*host_buffer;	/* One page of host buffer */
static	void	*host_end;	    /* end + 1 */
#endif

/* struct  cpm360_t *cpmp; */         /* Pointer to comm processor space */

QUICC  *pquicc;
/* QUICC  *quicc_dpram; */ /* mleslie - temporary; use extern pquicc elsewhere instead */


/* CPM interrupt vector functions. */
struct	cpm_action {
	void	(*handler)(void *);
	void	*dev_id;
};
static	struct	cpm_action cpm_vecs[CPMVEC_NR];
static	void	cpm_interrupt(int irq, void * dev, struct pt_regs * regs);
static	void	cpm_error_interrupt(void *);

/* prototypes: */
void cpm_install_handler(int vec, void (*handler)(), void *dev_id);
void m360_cpm_reset(void);




void m360_cpm_reset()
{
/* 	pte_t		   *pte; */

	pquicc = (struct quicc *)(_quicc_base); /* initialized in crt0_rXm.S */

	/* Perform a CPM reset. */
	pquicc->cp_cr = (SOFTWARE_RESET | CMD_FLAG);

	/* Wait for CPM to become ready (should be 2 clocks). */
	while (pquicc->cp_cr & CMD_FLAG);

	/* On the recommendation of the 68360 manual, p. 7-60
	 * - Set sdma interrupt service mask to 7
	 * - Set sdma arbitration ID to 4
	 */
	pquicc->sdma_sdcr = 0x0740;


	/* Claim the DP memory for our use.
	 */
	dp_alloc_base = CPM_DATAONLY_BASE;
	dp_alloc_top = dp_alloc_base + CPM_DATAONLY_SIZE;


	/* Set the host page for allocation.
	 */
	/* 	host_buffer = host_page_addr; */
	/* 	host_end = host_page_addr + PAGE_SIZE; */

	/* 	pte = find_pte(&init_mm, host_page_addr); */
	/* 	pte_val(*pte) |= _PAGE_NO_CACHE; */
	/* 	flush_tlb_page(current->mm->mmap, host_buffer); */

	/* Tell everyone where the comm processor resides.
	*/
/* 	cpmp = (cpm360_t *)commproc; */
}


/* This is called during init_IRQ.  We used to do it above, but this
 * was too early since init_IRQ was not yet called.
 */
void
cpm_interrupt_init(void)
{
	/* Initialize the CPM interrupt controller.
	 * NOTE THAT pquicc had better have been initialized!
	 * reference: MC68360UM p. 7-377
	 */
	pquicc->intr_cicr =
		(CICR_SCD_SCC4 | CICR_SCC_SCC3 | CICR_SCB_SCC2 | CICR_SCA_SCC1) |
		(CPM_INTERRUPT << 13) |
		CICR_HP_MASK |
		(CPM_VECTOR_BASE << 5) |
		CICR_SPS;

	/* mask all CPM interrupts from reaching the cpu32 core: */
	pquicc->intr_cimr = 0;


	/* mles - If I understand correctly, the 360 just pops over to the CPM
	 * specific vector, obviating the necessity to vector through the IRQ
	 * whose priority the CPM is set to. This needs a closer look, though.
	 */

	/* Set our interrupt handler with the core CPU. */
/* 	if (request_irq(CPM_INTERRUPT, cpm_interrupt, 0, "cpm", NULL) != 0) */
/* 		panic("Could not allocate CPM IRQ!"); */

	/* Install our own error handler.
	 */
	/* I think we want to hold off on this one for the moment - mles */
	/* cpm_install_handler(CPMVEC_ERROR, cpm_error_interrupt, NULL); */

	/* master CPM interrupt enable */
	/* pquicc->intr_cicr |= CICR_IEN; */ /* no such animal for 360 */
}



/* CPM interrupt controller interrupt.
*/
static	void
cpm_interrupt(int irq, void * dev, struct pt_regs * regs)
{
	/* uint	vec; */

	/* mles: Note that this stuff is currently being performed by
	 * M68360_do_irq(int vec, struct pt_regs *fp), in ../ints.c  */

	/* figure out the vector */
	/* call that vector's handler */
	/* clear the irq's bit in the service register */

#if 0 /* old 860 stuff: */
	/* Get the vector by setting the ACK bit and then reading
	 * the register.
	 */
	((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_civr = 1;
	vec = ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_civr;
	vec >>= 11;


	if (cpm_vecs[vec].handler != 0)
		(*cpm_vecs[vec].handler)(cpm_vecs[vec].dev_id);
	else
		((immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr &= ~(1 << vec);

	/* After servicing the interrupt, we have to remove the status
	 * indicator.
	 */
	((immap_t *)IMAP_ADDR)->im_cpic.cpic_cisr |= (1 << vec);
#endif

}

/* The CPM can generate the error interrupt when there is a race condition
 * between generating and masking interrupts.  All we have to do is ACK it
 * and return.  This is a no-op function so we don't need any special
 * tests in the interrupt handler.
 */
static	void
cpm_error_interrupt(void *dev)
{
}

/* Install a CPM interrupt handler.
*/
void
cpm_install_handler(int vec, void (*handler)(), void *dev_id)
{

	request_irq(vec, handler, IRQ_FLG_LOCK, "timer", dev_id);

/* 	if (cpm_vecs[vec].handler != 0) */
/* 		printk(KERN_INFO "CPM interrupt %x replacing %x\n", */
/* 			(uint)handler, (uint)cpm_vecs[vec].handler); */
/* 	cpm_vecs[vec].handler = handler; */
/* 	cpm_vecs[vec].dev_id = dev_id; */

	/*              ((immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr |= (1 << vec); */
/* 	pquicc->intr_cimr |= (1 << vec); */

}

/* Free a CPM interrupt handler.
*/
void
cpm_free_handler(int vec)
{
	cpm_vecs[vec].handler = NULL;
	cpm_vecs[vec].dev_id = NULL;
	/* ((immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr &= ~(1 << vec); */
	pquicc->intr_cimr &= ~(1 << vec);
}




/* Allocate some memory from the dual ported ram.  We may want to
 * enforce alignment restrictions, but right now everyone is a good
 * citizen.
 */
uint
m360_cpm_dpalloc(uint size)
{
        uint    retloc;

        if ((dp_alloc_base + size) >= dp_alloc_top)
                return(CPM_DP_NOSPACE);

        retloc = dp_alloc_base;
        dp_alloc_base += size;

        return(retloc);
}


#if 0 /* mleslie - for now these are simply kmalloc'd */
/* We also own one page of host buffer space for the allocation of
 * UART "fifos" and the like.
 */
uint
m360_cpm_hostalloc(uint size)
{
	uint	retloc;

	if ((host_buffer + size) >= host_end)
		return(0);

	retloc = host_buffer;
	host_buffer += size;

	return(retloc);
}
#endif


/* Set a baud rate generator.  This needs lots of work.  There are
 * four BRGs, any of which can be wired to any channel.
 * The internal baud rate clock is the system clock divided by 16.
 * This assumes the baudrate is 16x oversampled by the uart.
 */
/* #define BRG_INT_CLK	(((bd_t *)__res)->bi_intfreq * 1000000) */
#define BRG_INT_CLK		system_clock
#define BRG_UART_CLK	(BRG_INT_CLK/16)

void
m360_cpm_setbrg(uint brg, uint rate)
{
	volatile uint	*bp;

	/* This is good enough to get SMCs running.....
	 */
	/* bp = (uint *)&cpmp->cp_brgc1; */
	bp = (volatile uint *)(&pquicc->brgc[0].l);
	bp += brg;
	*bp = ((BRG_UART_CLK / rate - 1) << 1) | CPM_BRG_EN;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
