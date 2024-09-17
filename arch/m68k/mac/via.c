// SPDX-License-Identifier: GPL-2.0
/*
 *	6522 Versatile Interface Adapter (VIA)
 *
 *	There are two of these on the Mac II. Some IRQs are vectored
 *	via them as are assorted bits and bobs - eg RTC, ADB.
 *
 * CSA: Motorola seems to have removed documentation on the 6522 from
 * their web site; try
 *     http://nerini.drf.com/vectrex/other/text/chips/6522/
 *     http://www.zymurgy.net/classic/vic20/vicdet1.htm
 * and
 *     http://193.23.168.87/mikro_laborversuche/via_iobaustein/via6522_1.html
 * for info.  A full-text web search on 6522 AND VIA will probably also
 * net some usefulness. <cananian@alumni.princeton.edu> 20apr1999
 *
 * Additional data is here (the SY6522 was used in the Mac II etc):
 *     http://www.6502.org/documents/datasheets/synertek/synertek_sy6522.pdf
 *     http://www.6502.org/documents/datasheets/synertek/synertek_sy6522_programming_reference.pdf
 *
 * PRAM/RTC access algorithms are from the NetBSD RTC toolkit version 1.08b
 * by Erik Vogan and adapted to Linux by Joshua M. Thompson (funaho@jurai.org)
 *
 */

#include <linux/clocksource.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/irq.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>
#include <asm/mac_psc.h>
#include <asm/mac_oss.h>

volatile __u8 *via1, *via2;
int rbv_present;
int via_alt_mapping;
EXPORT_SYMBOL(via_alt_mapping);
static __u8 rbv_clear;

/*
 * Globals for accessing the VIA chip registers without having to
 * check if we're hitting a real VIA or an RBV. Normally you could
 * just hit the combined register (ie, vIER|rIER) but that seems to
 * break on AV Macs...probably because they actually decode more than
 * eight address bits. Why can't Apple engineers at least be
 * _consistently_ lazy?                          - 1999-05-21 (jmt)
 */

static int gIER,gIFR,gBufA,gBufB;

/*
 * On Macs with a genuine VIA chip there is no way to mask an individual slot
 * interrupt. This limitation also seems to apply to VIA clone logic cores in
 * Quadra-like ASICs. (RBV and OSS machines don't have this limitation.)
 *
 * We used to fake it by configuring the relevant VIA pin as an output
 * (to mask the interrupt) or input (to unmask). That scheme did not work on
 * (at least) the Quadra 700. A NuBus card's /NMRQ signal is an open-collector
 * circuit (see Designing Cards and Drivers for Macintosh II and Macintosh SE,
 * p. 10-11 etc) but VIA outputs are not (see datasheet).
 *
 * Driving these outputs high must cause the VIA to source current and the
 * card to sink current when it asserts /NMRQ. Current will flow but the pin
 * voltage is uncertain and so the /NMRQ condition may still cause a transition
 * at the VIA2 CA1 input (which explains the lost interrupts). A side effect
 * is that a disabled slot IRQ can never be tested as pending or not.
 *
 * Driving these outputs low doesn't work either. All the slot /NMRQ lines are
 * (active low) OR'd together to generate the CA1 (aka "SLOTS") interrupt (see
 * The Guide To Macintosh Family Hardware, 2nd edition p. 167). If we drive a
 * disabled /NMRQ line low, the falling edge immediately triggers a CA1
 * interrupt and all slot interrupts after that will generate no transition
 * and therefore no interrupt, even after being re-enabled.
 *
 * So we make the VIA port A I/O lines inputs and use nubus_disabled to keep
 * track of their states. When any slot IRQ becomes disabled we mask the CA1
 * umbrella interrupt. Only when all slot IRQs become enabled do we unmask
 * the CA1 interrupt. It must remain enabled even when cards have no interrupt
 * handler registered. Drivers must therefore disable a slot interrupt at the
 * device before they call free_irq (like shared and autovector interrupts).
 *
 * There is also a related problem when MacOS is used to boot Linux. A network
 * card brought up by a MacOS driver may raise an interrupt while Linux boots.
 * This can be fatal since it can't be handled until the right driver loads
 * (if such a driver exists at all). Apparently related to this hardware
 * limitation, "Designing Cards and Drivers", p. 9-8, says that a slot
 * interrupt with no driver would crash MacOS (the book was written before
 * the appearance of Macs with RBV or OSS).
 */

static u8 nubus_disabled;

void via_debug_dump(void);
static void via_nubus_init(void);

/*
 * Initialize the VIAs
 *
 * First we figure out where they actually _are_ as well as what type of
 * VIA we have for VIA2 (it could be a real VIA or an RBV or even an OSS.)
 * Then we pretty much clear them out and disable all IRQ sources.
 */

void __init via_init(void)
{
	via1 = (void *)VIA1_BASE;
	pr_debug("VIA1 detected at %p\n", via1);

	if (oss_present) {
		via2 = NULL;
		rbv_present = 0;
	} else {
		switch (macintosh_config->via_type) {

		/* IIci, IIsi, IIvx, IIvi (P6xx), LC series */

		case MAC_VIA_IICI:
			via2 = (void *)RBV_BASE;
			pr_debug("VIA2 (RBV) detected at %p\n", via2);
			rbv_present = 1;
			if (macintosh_config->ident == MAC_MODEL_LCIII) {
				rbv_clear = 0x00;
			} else {
				/* on most RBVs (& unlike the VIAs), you   */
				/* need to set bit 7 when you write to IFR */
				/* in order for your clear to occur.       */
				rbv_clear = 0x80;
			}
			gIER = rIER;
			gIFR = rIFR;
			gBufA = rSIFR;
			gBufB = rBufB;
			break;

		/* Quadra and early MacIIs agree on the VIA locations */

		case MAC_VIA_QUADRA:
		case MAC_VIA_II:
			via2 = (void *) VIA2_BASE;
			pr_debug("VIA2 detected at %p\n", via2);
			rbv_present = 0;
			rbv_clear = 0x00;
			gIER = vIER;
			gIFR = vIFR;
			gBufA = vBufA;
			gBufB = vBufB;
			break;

		default:
			panic("UNKNOWN VIA TYPE");
		}
	}

#ifdef DEBUG_VIA
	via_debug_dump();
#endif

	/*
	 * Shut down all IRQ sources, reset the timers, and
	 * kill the timer latch on VIA1.
	 */

	via1[vIER] = 0x7F;
	via1[vIFR] = 0x7F;
	via1[vT1CL] = 0;
	via1[vT1CH] = 0;
	via1[vT2CL] = 0;
	via1[vT2CH] = 0;
	via1[vACR] &= ~0xC0; /* setup T1 timer with no PB7 output */
	via1[vACR] &= ~0x03; /* disable port A & B latches */

	/*
	 * SE/30: disable video IRQ
	 */

	if (macintosh_config->ident == MAC_MODEL_SE30) {
		via1[vDirB] |= 0x40;
		via1[vBufB] |= 0x40;
	}

	switch (macintosh_config->adb_type) {
	case MAC_ADB_IOP:
	case MAC_ADB_II:
	case MAC_ADB_PB1:
		/*
		 * Set the RTC bits to a known state: all lines to outputs and
		 * RTC disabled (yes that's 0 to enable and 1 to disable).
		 */
		via1[vDirB] |= VIA1B_vRTCEnb | VIA1B_vRTCClk | VIA1B_vRTCData;
		via1[vBufB] |= VIA1B_vRTCEnb | VIA1B_vRTCClk;
		break;
	}

	/* Everything below this point is VIA2/RBV only... */

	if (oss_present)
		return;

	if ((macintosh_config->via_type == MAC_VIA_QUADRA) &&
	    (macintosh_config->adb_type != MAC_ADB_PB1) &&
	    (macintosh_config->adb_type != MAC_ADB_PB2) &&
	    (macintosh_config->ident    != MAC_MODEL_C660) &&
	    (macintosh_config->ident    != MAC_MODEL_Q840)) {
		via_alt_mapping = 1;
		via1[vDirB] |= 0x40;
		via1[vBufB] &= ~0x40;
	} else {
		via_alt_mapping = 0;
	}

	/*
	 * Now initialize VIA2. For RBV we just kill all interrupts;
	 * for a regular VIA we also reset the timers and stuff.
	 */

	via2[gIER] = 0x7F;
	via2[gIFR] = 0x7F | rbv_clear;
	if (!rbv_present) {
		via2[vT1CL] = 0;
		via2[vT1CH] = 0;
		via2[vT2CL] = 0;
		via2[vT2CH] = 0;
		via2[vACR] &= ~0xC0; /* setup T1 timer with no PB7 output */
		via2[vACR] &= ~0x03; /* disable port A & B latches */
	}

	via_nubus_init();

	/* Everything below this point is VIA2 only... */

	if (rbv_present)
		return;

	/*
	 * Set vPCR for control line interrupts.
	 *
	 * CA1 (SLOTS IRQ), CB1 (ASC IRQ): negative edge trigger.
	 *
	 * Macs with ESP SCSI have a negative edge triggered SCSI interrupt.
	 * Testing reveals that PowerBooks do too. However, the SE/30
	 * schematic diagram shows an active high NCR5380 IRQ line.
	 */

	pr_debug("VIA2 vPCR is 0x%02X\n", via2[vPCR]);
	if (macintosh_config->via_type == MAC_VIA_II) {
		/* CA2 (SCSI DRQ), CB2 (SCSI IRQ): indep. input, pos. edge */
		via2[vPCR] = 0x66;
	} else {
		/* CA2 (SCSI DRQ), CB2 (SCSI IRQ): indep. input, neg. edge */
		via2[vPCR] = 0x22;
	}
}

/*
 * Debugging dump, used in various places to see what's going on.
 */

void via_debug_dump(void)
{
	printk(KERN_DEBUG "VIA1: DDRA = 0x%02X DDRB = 0x%02X ACR = 0x%02X\n",
		(uint) via1[vDirA], (uint) via1[vDirB], (uint) via1[vACR]);
	printk(KERN_DEBUG "         PCR = 0x%02X  IFR = 0x%02X IER = 0x%02X\n",
		(uint) via1[vPCR], (uint) via1[vIFR], (uint) via1[vIER]);
	if (!via2)
		return;
	if (rbv_present) {
		printk(KERN_DEBUG "VIA2:  IFR = 0x%02X  IER = 0x%02X\n",
			(uint) via2[rIFR], (uint) via2[rIER]);
		printk(KERN_DEBUG "      SIFR = 0x%02X SIER = 0x%02X\n",
			(uint) via2[rSIFR], (uint) via2[rSIER]);
	} else {
		printk(KERN_DEBUG "VIA2: DDRA = 0x%02X DDRB = 0x%02X ACR = 0x%02X\n",
			(uint) via2[vDirA], (uint) via2[vDirB],
			(uint) via2[vACR]);
		printk(KERN_DEBUG "         PCR = 0x%02X  IFR = 0x%02X IER = 0x%02X\n",
			(uint) via2[vPCR],
			(uint) via2[vIFR], (uint) via2[vIER]);
	}
}

/*
 * Flush the L2 cache on Macs that have it by flipping
 * the system into 24-bit mode for an instant.
 */

void via_l2_flush(int writeback)
{
	unsigned long flags;

	local_irq_save(flags);
	via2[gBufB] &= ~VIA2B_vMode32;
	via2[gBufB] |= VIA2B_vMode32;
	local_irq_restore(flags);
}

/*
 * Initialize VIA2 for Nubus access
 */

static void __init via_nubus_init(void)
{
	/* unlock nubus transactions */

	if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
	    (macintosh_config->adb_type != MAC_ADB_PB2)) {
		/* set the line to be an output on non-RBV machines */
		if (!rbv_present)
			via2[vDirB] |= 0x02;

		/* this seems to be an ADB bit on PMU machines */
		/* according to MkLinux.  -- jmt               */
		via2[gBufB] |= 0x02;
	}

	/*
	 * Disable the slot interrupts. On some hardware that's not possible.
	 * On some hardware it's unclear what all of these I/O lines do.
	 */

	switch (macintosh_config->via_type) {
	case MAC_VIA_II:
	case MAC_VIA_QUADRA:
		pr_debug("VIA2 vDirA is 0x%02X\n", via2[vDirA]);
		break;
	case MAC_VIA_IICI:
		/* RBV. Disable all the slot interrupts. SIER works like IER. */
		via2[rSIER] = 0x7F;
		break;
	}
}

void via_nubus_irq_startup(int irq)
{
	int irq_idx = IRQ_IDX(irq);

	switch (macintosh_config->via_type) {
	case MAC_VIA_II:
	case MAC_VIA_QUADRA:
		/* Make the port A line an input. Probably redundant. */
		if (macintosh_config->via_type == MAC_VIA_II) {
			/* The top two bits are RAM size outputs. */
			via2[vDirA] &= 0xC0 | ~(1 << irq_idx);
		} else {
			/* Allow NuBus slots 9 through F. */
			via2[vDirA] &= 0x80 | ~(1 << irq_idx);
		}
		fallthrough;
	case MAC_VIA_IICI:
		via_irq_enable(irq);
		break;
	}
}

void via_nubus_irq_shutdown(int irq)
{
	switch (macintosh_config->via_type) {
	case MAC_VIA_II:
	case MAC_VIA_QUADRA:
		/* Ensure that the umbrella CA1 interrupt remains enabled. */
		via_irq_enable(irq);
		break;
	case MAC_VIA_IICI:
		via_irq_disable(irq);
		break;
	}
}

/*
 * The generic VIA interrupt routines (shamelessly stolen from Alan Cox's
 * via6522.c :-), disable/pending masks added.
 */

#define VIA_TIMER_1_INT BIT(6)

void via1_irq(struct irq_desc *desc)
{
	int irq_num;
	unsigned char irq_bit, events;

	events = via1[vIFR] & via1[vIER] & 0x7F;
	if (!events)
		return;

	irq_num = IRQ_MAC_TIMER_1;
	irq_bit = VIA_TIMER_1_INT;
	if (events & irq_bit) {
		unsigned long flags;

		local_irq_save(flags);
		via1[vIFR] = irq_bit;
		generic_handle_irq(irq_num);
		local_irq_restore(flags);

		events &= ~irq_bit;
		if (!events)
			return;
	}

	irq_num = VIA1_SOURCE_BASE;
	irq_bit = 1;
	do {
		if (events & irq_bit) {
			via1[vIFR] = irq_bit;
			generic_handle_irq(irq_num);
		}
		++irq_num;
		irq_bit <<= 1;
	} while (events >= irq_bit);
}

static void via2_irq(struct irq_desc *desc)
{
	int irq_num;
	unsigned char irq_bit, events;

	events = via2[gIFR] & via2[gIER] & 0x7F;
	if (!events)
		return;

	irq_num = VIA2_SOURCE_BASE;
	irq_bit = 1;
	do {
		if (events & irq_bit) {
			via2[gIFR] = irq_bit | rbv_clear;
			generic_handle_irq(irq_num);
		}
		++irq_num;
		irq_bit <<= 1;
	} while (events >= irq_bit);
}

/*
 * Dispatch Nubus interrupts. We are called as a secondary dispatch by the
 * VIA2 dispatcher as a fast interrupt handler.
 */

static void via_nubus_irq(struct irq_desc *desc)
{
	int slot_irq;
	unsigned char slot_bit, events;

	events = ~via2[gBufA] & 0x7F;
	if (rbv_present)
		events &= via2[rSIER];
	else
		events &= ~via2[vDirA];
	if (!events)
		return;

	do {
		slot_irq = IRQ_NUBUS_F;
		slot_bit = 0x40;
		do {
			if (events & slot_bit) {
				events &= ~slot_bit;
				generic_handle_irq(slot_irq);
			}
			--slot_irq;
			slot_bit >>= 1;
		} while (events);

 		/* clear the CA1 interrupt and make certain there's no more. */
		via2[gIFR] = 0x02 | rbv_clear;
		events = ~via2[gBufA] & 0x7F;
		if (rbv_present)
			events &= via2[rSIER];
		else
			events &= ~via2[vDirA];
	} while (events);
}

/*
 * Register the interrupt dispatchers for VIA or RBV machines only.
 */

void __init via_register_interrupts(void)
{
	if (via_alt_mapping) {
		/* software interrupt */
		irq_set_chained_handler(IRQ_AUTO_1, via1_irq);
		/* via1 interrupt */
		irq_set_chained_handler(IRQ_AUTO_6, via1_irq);
	} else {
		irq_set_chained_handler(IRQ_AUTO_1, via1_irq);
	}
	irq_set_chained_handler(IRQ_AUTO_2, via2_irq);
	irq_set_chained_handler(IRQ_MAC_NUBUS, via_nubus_irq);
}

void via_irq_enable(int irq) {
	int irq_src	= IRQ_SRC(irq);
	int irq_idx	= IRQ_IDX(irq);

	if (irq_src == 1) {
		via1[vIER] = IER_SET_BIT(irq_idx);
	} else if (irq_src == 2) {
		if (irq != IRQ_MAC_NUBUS || nubus_disabled == 0)
			via2[gIER] = IER_SET_BIT(irq_idx);
	} else if (irq_src == 7) {
		switch (macintosh_config->via_type) {
		case MAC_VIA_II:
		case MAC_VIA_QUADRA:
			nubus_disabled &= ~(1 << irq_idx);
			/* Enable the CA1 interrupt when no slot is disabled. */
			if (!nubus_disabled)
				via2[gIER] = IER_SET_BIT(1);
			break;
		case MAC_VIA_IICI:
			/* On RBV, enable the slot interrupt.
			 * SIER works like IER.
			 */
			via2[rSIER] = IER_SET_BIT(irq_idx);
			break;
		}
	}
}

void via_irq_disable(int irq) {
	int irq_src	= IRQ_SRC(irq);
	int irq_idx	= IRQ_IDX(irq);

	if (irq_src == 1) {
		via1[vIER] = IER_CLR_BIT(irq_idx);
	} else if (irq_src == 2) {
		via2[gIER] = IER_CLR_BIT(irq_idx);
	} else if (irq_src == 7) {
		switch (macintosh_config->via_type) {
		case MAC_VIA_II:
		case MAC_VIA_QUADRA:
			nubus_disabled |= 1 << irq_idx;
			if (nubus_disabled)
				via2[gIER] = IER_CLR_BIT(1);
			break;
		case MAC_VIA_IICI:
			via2[rSIER] = IER_CLR_BIT(irq_idx);
			break;
		}
	}
}

void via1_set_head(int head)
{
	if (head == 0)
		via1[vBufA] &= ~VIA1A_vHeadSel;
	else
		via1[vBufA] |= VIA1A_vHeadSel;
}
EXPORT_SYMBOL(via1_set_head);

int via2_scsi_drq_pending(void)
{
	return via2[gIFR] & (1 << IRQ_IDX(IRQ_MAC_SCSIDRQ));
}
EXPORT_SYMBOL(via2_scsi_drq_pending);

/* timer and clock source */

#define VIA_CLOCK_FREQ     783360                /* VIA "phase 2" clock in Hz */
#define VIA_TIMER_CYCLES   (VIA_CLOCK_FREQ / HZ) /* clock cycles per jiffy */

#define VIA_TC             (VIA_TIMER_CYCLES - 2) /* including 0 and -1 */
#define VIA_TC_LOW         (VIA_TC & 0xFF)
#define VIA_TC_HIGH        (VIA_TC >> 8)

static u64 mac_read_clk(struct clocksource *cs);

static struct clocksource mac_clk = {
	.name   = "via1",
	.rating = 250,
	.read   = mac_read_clk,
	.mask   = CLOCKSOURCE_MASK(32),
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static u32 clk_total, clk_offset;

static irqreturn_t via_timer_handler(int irq, void *dev_id)
{
	clk_total += VIA_TIMER_CYCLES;
	clk_offset = 0;
	legacy_timer_tick(1);

	return IRQ_HANDLED;
}

void __init via_init_clock(void)
{
	if (request_irq(IRQ_MAC_TIMER_1, via_timer_handler, IRQF_TIMER, "timer",
			NULL)) {
		pr_err("Couldn't register %s interrupt\n", "timer");
		return;
	}

	via1[vT1CL] = VIA_TC_LOW;
	via1[vT1CH] = VIA_TC_HIGH;
	via1[vACR] |= 0x40;

	clocksource_register_hz(&mac_clk, VIA_CLOCK_FREQ);
}

static u64 mac_read_clk(struct clocksource *cs)
{
	unsigned long flags;
	u8 count_high;
	u16 count;
	u32 ticks;

	/*
	 * Timer counter wrap-around is detected with the timer interrupt flag
	 * but reading the counter low byte (vT1CL) would reset the flag.
	 * Also, accessing both counter registers is essentially a data race.
	 * These problems are avoided by ignoring the low byte. Clock accuracy
	 * is 256 times worse (error can reach 0.327 ms) but CPU overhead is
	 * reduced by avoiding slow VIA register accesses.
	 */

	local_irq_save(flags);
	count_high = via1[vT1CH];
	if (count_high == 0xFF)
		count_high = 0;
	if (count_high > 0 && (via1[vIFR] & VIA_TIMER_1_INT))
		clk_offset = VIA_TIMER_CYCLES;
	count = count_high << 8;
	ticks = VIA_TIMER_CYCLES - count;
	ticks += clk_offset + clk_total;
	local_irq_restore(flags);

	return ticks;
}
