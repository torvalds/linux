/*
 * arch/m68k/atari/ataints.c -- Atari Linux interrupt handling code
 *
 * 5/2/94 Roman Hodek:
 *  Added support for TT interrupts; setup for TT SCU (may someone has
 *  twiddled there and we won't get the right interrupts :-()
 *
 *  Major change: The device-independent code in m68k/ints.c didn't know
 *  about non-autovec ints yet. It hardcoded the number of possible ints to
 *  7 (IRQ1...IRQ7). But the Atari has lots of non-autovec ints! I made the
 *  number of possible ints a constant defined in interrupt.h, which is
 *  47 for the Atari. So we can call request_irq() for all Atari interrupts
 *  just the normal way. Additionally, all vectors >= 48 are initialized to
 *  call trap() instead of inthandler(). This must be changed here, too.
 *
 * 1995-07-16 Lars Brinkhoff <f93labr@dd.chalmers.se>:
 *  Corrected a bug in atari_add_isr() which rejected all SCC
 *  interrupt sources if there were no TT MFP!
 *
 * 12/13/95: New interface functions atari_level_triggered_int() and
 *  atari_register_vme_int() as support for level triggered VME interrupts.
 *
 * 02/12/96: (Roman)
 *  Total rewrite of Atari interrupt handling, for new scheme see comments
 *  below.
 *
 * 1996-09-03 lars brinkhoff <f93labr@dd.chalmers.se>:
 *  Added new function atari_unregister_vme_int(), and
 *  modified atari_register_vme_int() as well as IS_VALID_INTNO()
 *  to work with it.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/irq.h>

#include <asm/traps.h>

#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#include <asm/irq.h>
#include <asm/entry.h>
#include <asm/io.h>

#include "atari.h"

/*
 * Atari interrupt handling scheme:
 * --------------------------------
 *
 * All interrupt source have an internal number (defined in
 * <asm/atariints.h>): Autovector interrupts are 1..7, then follow ST-MFP,
 * TT-MFP, SCC, and finally VME interrupts. Vector numbers for the latter can
 * be allocated by atari_register_vme_int().
 */

/*
 * Bitmap for free interrupt vector numbers
 * (new vectors starting from 0x70 can be allocated by
 * atari_register_vme_int())
 */
static int free_vme_vec_bitmap;

/* GK:
 * HBL IRQ handler for Falcon. Nobody needs it :-)
 * ++andreas: raise ipl to disable further HBLANK interrupts.
 */
asmlinkage void falcon_hblhandler(void);
asm(".text\n"
__ALIGN_STR "\n\t"
"falcon_hblhandler:\n\t"
	"orw	#0x200,%sp@\n\t"	/* set saved ipl to 2 */
	"rte");

static unsigned int atari_irq_startup(struct irq_data *data)
{
	unsigned int irq = data->irq;

	m68k_irq_startup(data);
	atari_turnon_irq(irq);
	atari_enable_irq(irq);
	return 0;
}

static void atari_irq_shutdown(struct irq_data *data)
{
	unsigned int irq = data->irq;

	atari_disable_irq(irq);
	atari_turnoff_irq(irq);
	m68k_irq_shutdown(data);

	if (irq == IRQ_AUTO_4)
	    vectors[VEC_INT4] = falcon_hblhandler;
}

static void atari_irq_enable(struct irq_data *data)
{
	atari_enable_irq(data->irq);
}

static void atari_irq_disable(struct irq_data *data)
{
	atari_disable_irq(data->irq);
}

static struct irq_chip atari_irq_chip = {
	.name		= "atari",
	.irq_startup	= atari_irq_startup,
	.irq_shutdown	= atari_irq_shutdown,
	.irq_enable	= atari_irq_enable,
	.irq_disable	= atari_irq_disable,
};

/*
 * ST-MFP timer D chained interrupts - each driver gets its own timer
 * interrupt instance.
 */

struct mfptimerbase {
	volatile struct MFP *mfp;
	unsigned char mfp_mask, mfp_data;
	unsigned short int_mask;
	int handler_irq, mfptimer_irq, server_irq;
	char *name;
} stmfp_base = {
	.mfp		= &st_mfp,
	.int_mask	= 0x0,
	.handler_irq	= IRQ_MFP_TIMD,
	.mfptimer_irq	= IRQ_MFP_TIMER1,
	.name		= "MFP Timer D"
};

static irqreturn_t mfp_timer_d_handler(int irq, void *dev_id)
{
	struct mfptimerbase *base = dev_id;
	int mach_irq;
	unsigned char ints;

	mach_irq = base->mfptimer_irq;
	ints = base->int_mask;
	for (; ints; mach_irq++, ints >>= 1) {
		if (ints & 1)
			generic_handle_irq(mach_irq);
	}
	return IRQ_HANDLED;
}


static void atari_mfptimer_enable(struct irq_data *data)
{
	int mfp_num = data->irq - IRQ_MFP_TIMER1;
	stmfp_base.int_mask |= 1 << mfp_num;
	atari_enable_irq(IRQ_MFP_TIMD);
}

static void atari_mfptimer_disable(struct irq_data *data)
{
	int mfp_num = data->irq - IRQ_MFP_TIMER1;
	stmfp_base.int_mask &= ~(1 << mfp_num);
	if (!stmfp_base.int_mask)
		atari_disable_irq(IRQ_MFP_TIMD);
}

static struct irq_chip atari_mfptimer_chip = {
	.name		= "timer_d",
	.irq_enable	= atari_mfptimer_enable,
	.irq_disable	= atari_mfptimer_disable,
};


/*
 * EtherNAT CPLD interrupt handling
 * CPLD interrupt register is at phys. 0x80000023
 * Need this mapped in at interrupt startup time
 * Possibly need this mapped on demand anyway -
 * EtherNAT USB driver needs to disable IRQ before
 * startup!
 */

static unsigned char *enat_cpld;

static unsigned int atari_ethernat_startup(struct irq_data *data)
{
	int enat_num = 140 - data->irq + 1;

	m68k_irq_startup(data);
	/*
	* map CPLD interrupt register
	*/
	if (!enat_cpld)
		enat_cpld = (unsigned char *)ioremap((ATARI_ETHERNAT_PHYS_ADDR+0x23), 0x2);
	/*
	 * do _not_ enable the USB chip interrupt here - causes interrupt storm
	 * and triggers dead interrupt watchdog
	 * Need to reset the USB chip to a sane state in early startup before
	 * removing this hack
	 */
	if (enat_num == 1)
		*enat_cpld |= 1 << enat_num;

	return 0;
}

static void atari_ethernat_enable(struct irq_data *data)
{
	int enat_num = 140 - data->irq + 1;
	/*
	* map CPLD interrupt register
	*/
	if (!enat_cpld)
		enat_cpld = (unsigned char *)ioremap((ATARI_ETHERNAT_PHYS_ADDR+0x23), 0x2);
	*enat_cpld |= 1 << enat_num;
}

static void atari_ethernat_disable(struct irq_data *data)
{
	int enat_num = 140 - data->irq + 1;
	/*
	* map CPLD interrupt register
	*/
	if (!enat_cpld)
		enat_cpld = (unsigned char *)ioremap((ATARI_ETHERNAT_PHYS_ADDR+0x23), 0x2);
	*enat_cpld &= ~(1 << enat_num);
}

static void atari_ethernat_shutdown(struct irq_data *data)
{
	int enat_num = 140 - data->irq + 1;
	if (enat_cpld) {
		*enat_cpld &= ~(1 << enat_num);
		iounmap(enat_cpld);
		enat_cpld = NULL;
	}
}

static struct irq_chip atari_ethernat_chip = {
	.name		= "ethernat",
	.irq_startup	= atari_ethernat_startup,
	.irq_shutdown	= atari_ethernat_shutdown,
	.irq_enable	= atari_ethernat_enable,
	.irq_disable	= atari_ethernat_disable,
};

/*
 * void atari_init_IRQ (void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the atari IRQ handling routines.
 */

void __init atari_init_IRQ(void)
{
	m68k_setup_user_interrupt(VEC_USER, NUM_ATARI_SOURCES - IRQ_USER);
	m68k_setup_irq_controller(&atari_irq_chip, handle_simple_irq, 1,
				  NUM_ATARI_SOURCES - 1);

	/* Initialize the MFP(s) */

#ifdef ATARI_USE_SOFTWARE_EOI
	st_mfp.vec_adr  = 0x48;	/* Software EOI-Mode */
#else
	st_mfp.vec_adr  = 0x40;	/* Automatic EOI-Mode */
#endif
	st_mfp.int_en_a = 0x00;	/* turn off MFP-Ints */
	st_mfp.int_en_b = 0x00;
	st_mfp.int_mk_a = 0xff;	/* no Masking */
	st_mfp.int_mk_b = 0xff;

	if (ATARIHW_PRESENT(TT_MFP)) {
#ifdef ATARI_USE_SOFTWARE_EOI
		tt_mfp.vec_adr  = 0x58;		/* Software EOI-Mode */
#else
		tt_mfp.vec_adr  = 0x50;		/* Automatic EOI-Mode */
#endif
		tt_mfp.int_en_a = 0x00;		/* turn off MFP-Ints */
		tt_mfp.int_en_b = 0x00;
		tt_mfp.int_mk_a = 0xff;		/* no Masking */
		tt_mfp.int_mk_b = 0xff;
	}

	if (ATARIHW_PRESENT(SCC) && !atari_SCC_reset_done) {
		atari_scc.cha_a_ctrl = 9;
		MFPDELAY();
		atari_scc.cha_a_ctrl = (char) 0xc0; /* hardware reset */
	}

	if (ATARIHW_PRESENT(SCU)) {
		/* init the SCU if present */
		tt_scu.sys_mask = 0x0;		/* disable all interrupts */
		tt_scu.vme_mask = 0x60;		/* enable MFP and SCC ints */
	} else {
		/* If no SCU and no Hades, the HSYNC interrupt needs to be
		 * disabled this way. (Else _inthandler in kernel/sys_call.S
		 * gets overruns)
		 */

		vectors[VEC_INT2] = falcon_hblhandler;
		vectors[VEC_INT4] = falcon_hblhandler;
	}

	if (ATARIHW_PRESENT(PCM_8BIT) && ATARIHW_PRESENT(MICROWIRE)) {
		/* Initialize the LM1992 Sound Controller to enable
		   the PSG sound.  This is misplaced here, it should
		   be in an atasound_init(), that doesn't exist yet. */
		atari_microwire_cmd(MW_LM1992_PSG_HIGH);
	}

	stdma_init();

	/* Initialize the PSG: all sounds off, both ports output */
	sound_ym.rd_data_reg_sel = 7;
	sound_ym.wd_data = 0xff;

	m68k_setup_irq_controller(&atari_mfptimer_chip, handle_simple_irq,
				  IRQ_MFP_TIMER1, 8);

	irq_set_status_flags(IRQ_MFP_TIMER1, IRQ_IS_POLLED);
	irq_set_status_flags(IRQ_MFP_TIMER2, IRQ_IS_POLLED);

	/* prepare timer D data for use as poll interrupt */
	/* set Timer D data Register - needs to be > 0 */
	st_mfp.tim_dt_d = 254;	/* < 100 Hz */
	/* start timer D, div = 1:100 */
	st_mfp.tim_ct_cd = (st_mfp.tim_ct_cd & 0xf0) | 0x6;

	/* request timer D dispatch handler */
	if (request_irq(IRQ_MFP_TIMD, mfp_timer_d_handler, IRQF_SHARED,
			stmfp_base.name, &stmfp_base))
		pr_err("Couldn't register %s interrupt\n", stmfp_base.name);

	/*
	 * EtherNAT ethernet / USB interrupt handlers
	 */

	m68k_setup_irq_controller(&atari_ethernat_chip, handle_simple_irq,
				  139, 2);
}


/*
 * atari_register_vme_int() returns the number of a free interrupt vector for
 * hardware with a programmable int vector (probably a VME board).
 */

unsigned int atari_register_vme_int(void)
{
	int i;

	for (i = 0; i < 32; i++)
		if ((free_vme_vec_bitmap & (1 << i)) == 0)
			break;

	if (i == 16)
		return 0;

	free_vme_vec_bitmap |= 1 << i;
	return VME_SOURCE_BASE + i;
}
EXPORT_SYMBOL(atari_register_vme_int);


void atari_unregister_vme_int(unsigned int irq)
{
	if (irq >= VME_SOURCE_BASE && irq < VME_SOURCE_BASE + VME_MAX_SOURCES) {
		irq -= VME_SOURCE_BASE;
		free_vme_vec_bitmap &= ~(1 << irq);
	}
}
EXPORT_SYMBOL(atari_unregister_vme_int);


