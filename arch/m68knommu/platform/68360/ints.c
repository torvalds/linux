/*
 * linux/arch/$(ARCH)/platform/$(PLATFORM)/ints.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000  Michael Leslie <mleslie@lineo.com>
 * Copyright (c) 1996 Roman Zippel
 * Copyright (c) 1999 D. Jeff Dionne <jeff@uclinux.org>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/m68360.h>

/* from quicc/commproc.c: */
extern QUICC *pquicc;
extern void cpm_interrupt_init(void);

#define INTERNAL_IRQS (96)

/* assembler routines */
asmlinkage void system_call(void);
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void bad_interrupt(void);
asmlinkage void inthandler(void);

extern void *_ramvec[];

/* The number of spurious interrupts */
volatile unsigned int num_spurious;

static void intc_irq_unmask(unsigned int irq)
{
	pquicc->intr_cimr |= (1 << irq);
}

static void intc_irq_mask(unsigned int irq)
{
	pquicc->intr_cimr &= ~(1 << irq);
}

static void intc_irq_ack(unsigned int irq)
{
	pquicc->intr_cisr = (1 << irq);
}

static struct irq_chip intc_irq_chip = {
	.name		= "M68K-INTC",
	.mask		= intc_irq_mask,
	.unmask		= intc_irq_unmask,
	.ack		= intc_irq_ack,
};

/*
 * This function should be called during kernel startup to initialize
 * the vector table.
 */
void init_IRQ(void)
{
	int i;
	int vba = (CPM_VECTOR_BASE<<4);

	/* set up the vectors */
	_ramvec[2] = buserr;
	_ramvec[3] = trap;
	_ramvec[4] = trap;
	_ramvec[5] = trap;
	_ramvec[6] = trap;
	_ramvec[7] = trap;
	_ramvec[8] = trap;
	_ramvec[9] = trap;
	_ramvec[10] = trap;
	_ramvec[11] = trap;
	_ramvec[12] = trap;
	_ramvec[13] = trap;
	_ramvec[14] = trap;
	_ramvec[15] = trap;

	_ramvec[32] = system_call;
	_ramvec[33] = trap;

	cpm_interrupt_init();

	/* set up CICR for vector base address and irq level */
	/* irl = 4, hp = 1f - see MC68360UM p 7-377 */
	pquicc->intr_cicr = 0x00e49f00 | vba;

	/* CPM interrupt vectors: (p 7-376) */
	_ramvec[vba+CPMVEC_ERROR]       = bad_interrupt; /* Error */
	_ramvec[vba+CPMVEC_PIO_PC11]    = inthandler;   /* pio - pc11 */
	_ramvec[vba+CPMVEC_PIO_PC10]    = inthandler;   /* pio - pc10 */
	_ramvec[vba+CPMVEC_SMC2]        = inthandler;   /* smc2/pip */
	_ramvec[vba+CPMVEC_SMC1]        = inthandler;   /* smc1 */
	_ramvec[vba+CPMVEC_SPI]         = inthandler;   /* spi */
	_ramvec[vba+CPMVEC_PIO_PC9]     = inthandler;   /* pio - pc9 */
	_ramvec[vba+CPMVEC_TIMER4]      = inthandler;   /* timer 4 */
	_ramvec[vba+CPMVEC_RESERVED1]   = inthandler;   /* reserved */
	_ramvec[vba+CPMVEC_PIO_PC8]     = inthandler;   /* pio - pc8 */
	_ramvec[vba+CPMVEC_PIO_PC7]     = inthandler;  /* pio - pc7 */
	_ramvec[vba+CPMVEC_PIO_PC6]     = inthandler;  /* pio - pc6 */
	_ramvec[vba+CPMVEC_TIMER3]      = inthandler;  /* timer 3 */
	_ramvec[vba+CPMVEC_PIO_PC5]     = inthandler;  /* pio - pc5 */
	_ramvec[vba+CPMVEC_PIO_PC4]     = inthandler;  /* pio - pc4 */
	_ramvec[vba+CPMVEC_RESERVED2]   = inthandler;  /* reserved */
	_ramvec[vba+CPMVEC_RISCTIMER]   = inthandler;  /* timer table */
	_ramvec[vba+CPMVEC_TIMER2]      = inthandler;  /* timer 2 */
	_ramvec[vba+CPMVEC_RESERVED3]   = inthandler;  /* reserved */
	_ramvec[vba+CPMVEC_IDMA2]       = inthandler;  /* idma 2 */
	_ramvec[vba+CPMVEC_IDMA1]       = inthandler;  /* idma 1 */
	_ramvec[vba+CPMVEC_SDMA_CB_ERR] = inthandler;  /* sdma channel bus error */
	_ramvec[vba+CPMVEC_PIO_PC3]     = inthandler;  /* pio - pc3 */
	_ramvec[vba+CPMVEC_PIO_PC2]     = inthandler;  /* pio - pc2 */
	/* _ramvec[vba+CPMVEC_TIMER1]      = cpm_isr_timer1; */  /* timer 1 */
	_ramvec[vba+CPMVEC_TIMER1]      = inthandler;  /* timer 1 */
	_ramvec[vba+CPMVEC_PIO_PC1]     = inthandler;  /* pio - pc1 */
	_ramvec[vba+CPMVEC_SCC4]        = inthandler;  /* scc 4 */
	_ramvec[vba+CPMVEC_SCC3]        = inthandler;  /* scc 3 */
	_ramvec[vba+CPMVEC_SCC2]        = inthandler;  /* scc 2 */
	_ramvec[vba+CPMVEC_SCC1]        = inthandler;  /* scc 1 */
	_ramvec[vba+CPMVEC_PIO_PC0]     = inthandler;  /* pio - pc0 */


	/* turn off all CPM interrupts */
	pquicc->intr_cimr = 0x00000000;

	for (i = 0; (i < NR_IRQS); i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].chip = &intc_irq_chip;
	}
}

