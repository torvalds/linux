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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/bootinfo.h>
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
 * Timer defs.
 */

#define TICK_SIZE		10000
#define MAC_CLOCK_TICK		(783300/HZ)		/* ticks per HZ */
#define MAC_CLOCK_LOW		(MAC_CLOCK_TICK&0xFF)
#define MAC_CLOCK_HIGH		(MAC_CLOCK_TICK>>8)

/* To disable a NuBus slot on Quadras we make that slot IRQ line an output set
 * high. On RBV we just use the slot interrupt enable register. On Macs with
 * genuine VIA chips we must use nubus_disabled to keep track of disabled slot
 * interrupts. When any slot IRQ is disabled we mask the (edge triggered) CA1
 * or "SLOTS" interrupt. When no slot is disabled, we unmask the CA1 interrupt.
 * So, on genuine VIAs, having more than one NuBus IRQ can mean trouble,
 * because closing one of those drivers can mask all of the NuBus interrupts.
 * Also, since we can't mask the unregistered slot IRQs on genuine VIAs, it's
 * possible to get interrupts from cards that MacOS or the ROM has configured
 * but we have not. FWIW, "Designing Cards and Drivers for Macintosh II and
 * Macintosh SE", page 9-8, says, a slot IRQ with no driver would crash MacOS.
 */
static u8 nubus_disabled;

void via_debug_dump(void);
irqreturn_t via1_irq(int, void *);
irqreturn_t via2_irq(int, void *);
irqreturn_t via_nubus_irq(int, void *);
void via_irq_enable(int irq);
void via_irq_disable(int irq);
void via_irq_clear(int irq);

extern irqreturn_t mac_scc_dispatch(int, void *);

/*
 * Initialize the VIAs
 *
 * First we figure out where they actually _are_ as well as what type of
 * VIA we have for VIA2 (it could be a real VIA or an RBV or even an OSS.)
 * Then we pretty much clear them out and disable all IRQ sources.
 *
 * Note: the OSS is actually "detected" here and not in oss_init(). It just
 *	 seems more logical to do it here since via_init() needs to know
 *	 these things anyways.
 */

void __init via_init(void)
{
	switch(macintosh_config->via_type) {

		/* IIci, IIsi, IIvx, IIvi (P6xx), LC series */

		case MAC_VIA_IIci:
			via1 = (void *) VIA1_BASE;
			if (macintosh_config->ident == MAC_MODEL_IIFX) {
				via2 = NULL;
				rbv_present = 0;
				oss_present = 1;
			} else {
				via2 = (void *) RBV_BASE;
				rbv_present = 1;
				oss_present = 0;
			}
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
			via1 = (void *) VIA1_BASE;
			via2 = (void *) VIA2_BASE;
			rbv_present = 0;
			oss_present = 0;
			rbv_clear = 0x00;
			gIER = vIER;
			gIFR = vIFR;
			gBufA = vBufA;
			gBufB = vBufB;
			break;
		default:
			panic("UNKNOWN VIA TYPE");
	}

	printk(KERN_INFO "VIA1 at %p is a 6522 or clone\n", via1);

	printk(KERN_INFO "VIA2 at %p is ", via2);
	if (rbv_present) {
		printk("an RBV\n");
	} else if (oss_present) {
		printk("an OSS\n");
	} else {
		printk("a 6522 or clone\n");
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
	via1[vT1LL] = 0;
	via1[vT1LH] = 0;
	via1[vT1CL] = 0;
	via1[vT1CH] = 0;
	via1[vT2CL] = 0;
	via1[vT2CH] = 0;
	via1[vACR] &= ~0xC0; /* setup T1 timer with no PB7 output */
	via1[vACR] &= ~0x03; /* disable port A & B latches */

	/*
	 * SE/30: disable video IRQ
	 * XXX: testing for SE/30 VBL
	 */

	if (macintosh_config->ident == MAC_MODEL_SE30) {
		via1[vDirB] |= 0x40;
		via1[vBufB] |= 0x40;
	}

	/*
	 * Set the RTC bits to a known state: all lines to outputs and
	 * RTC disabled (yes that's 0 to enable and 1 to disable).
	 */

	via1[vDirB] |= (VIA1B_vRTCEnb | VIA1B_vRTCClk | VIA1B_vRTCData);
	via1[vBufB] |= (VIA1B_vRTCEnb | VIA1B_vRTCClk);

	/* Everything below this point is VIA2/RBV only... */

	if (oss_present)
		return;

	/* Some machines support an alternate IRQ mapping that spreads  */
	/* Ethernet and Sound out to their own autolevel IRQs and moves */
	/* VIA1 to level 6. A/UX uses this mapping and we do too.  Note */
	/* that the IIfx emulates this alternate mapping using the OSS. */

	via_alt_mapping = 0;
	if (macintosh_config->via_type == MAC_VIA_QUADRA)
		switch (macintosh_config->ident) {
		case MAC_MODEL_C660:
		case MAC_MODEL_Q840:
			/* not applicable */
			break;
		case MAC_MODEL_P588:
		case MAC_MODEL_TV:
		case MAC_MODEL_PB140:
		case MAC_MODEL_PB145:
		case MAC_MODEL_PB160:
		case MAC_MODEL_PB165:
		case MAC_MODEL_PB165C:
		case MAC_MODEL_PB170:
		case MAC_MODEL_PB180:
		case MAC_MODEL_PB180C:
		case MAC_MODEL_PB190:
		case MAC_MODEL_PB520:
			/* not yet tested */
			break;
		default:
			via_alt_mapping = 1;
			via1[vDirB] |= 0x40;
			via1[vBufB] &= ~0x40;
			break;
		}

	/*
	 * Now initialize VIA2. For RBV we just kill all interrupts;
	 * for a regular VIA we also reset the timers and stuff.
	 */

	via2[gIER] = 0x7F;
	via2[gIFR] = 0x7F | rbv_clear;
	if (!rbv_present) {
		via2[vT1LL] = 0;
		via2[vT1LH] = 0;
		via2[vT1CL] = 0;
		via2[vT1CH] = 0;
		via2[vT2CL] = 0;
		via2[vT2CH] = 0;
		via2[vACR] &= ~0xC0; /* setup T1 timer with no PB7 output */
		via2[vACR] &= ~0x03; /* disable port A & B latches */
	}

	/*
	 * Set vPCR for control line interrupts (but not on RBV)
	 */
	if (!rbv_present) {
		/* For all VIA types, CA1 (SLOTS IRQ) and CB1 (ASC IRQ)
		 * are made negative edge triggered here.
		 */
		if (macintosh_config->scsi_type == MAC_SCSI_OLD) {
			/* CB2 (IRQ) indep. input, positive edge */
			/* CA2 (DRQ) indep. input, positive edge */
			via2[vPCR] = 0x66;
		} else {
			/* CB2 (IRQ) indep. input, negative edge */
			/* CA2 (DRQ) indep. input, negative edge */
			via2[vPCR] = 0x22;
		}
	}
}

/*
 * Start the 100 Hz clock
 */

void __init via_init_clock(irq_handler_t func)
{
	via1[vACR] |= 0x40;
	via1[vT1LL] = MAC_CLOCK_LOW;
	via1[vT1LH] = MAC_CLOCK_HIGH;
	via1[vT1CL] = MAC_CLOCK_LOW;
	via1[vT1CH] = MAC_CLOCK_HIGH;

	if (request_irq(IRQ_MAC_TIMER_1, func, IRQ_FLG_LOCK, "timer", func))
		pr_err("Couldn't register %s interrupt\n", "timer");
}

/*
 * Register the interrupt dispatchers for VIA or RBV machines only.
 */

void __init via_register_interrupts(void)
{
	if (via_alt_mapping) {
		if (request_irq(IRQ_AUTO_1, via1_irq,
				IRQ_FLG_LOCK|IRQ_FLG_FAST, "software",
				(void *) via1))
			pr_err("Couldn't register %s interrupt\n", "software");
		if (request_irq(IRQ_AUTO_6, via1_irq,
				IRQ_FLG_LOCK|IRQ_FLG_FAST, "via1",
				(void *) via1))
			pr_err("Couldn't register %s interrupt\n", "via1");
	} else {
		if (request_irq(IRQ_AUTO_1, via1_irq,
				IRQ_FLG_LOCK|IRQ_FLG_FAST, "via1",
				(void *) via1))
			pr_err("Couldn't register %s interrupt\n", "via1");
	}
	if (request_irq(IRQ_AUTO_2, via2_irq, IRQ_FLG_LOCK|IRQ_FLG_FAST,
			"via2", (void *) via2))
		pr_err("Couldn't register %s interrupt\n", "via2");
	if (!psc_present) {
		if (request_irq(IRQ_AUTO_4, mac_scc_dispatch, IRQ_FLG_LOCK,
				"scc", mac_scc_dispatch))
			pr_err("Couldn't register %s interrupt\n", "scc");
	}
	if (request_irq(IRQ_MAC_NUBUS, via_nubus_irq,
			IRQ_FLG_LOCK|IRQ_FLG_FAST, "nubus", (void *) via2))
		pr_err("Couldn't register %s interrupt\n", "nubus");
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
	if (oss_present) {
		printk(KERN_DEBUG "VIA2: <OSS>\n");
	} else if (rbv_present) {
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
 * This is always executed with interrupts disabled.
 *
 * TBI: get time offset between scheduling timer ticks
 */

unsigned long mac_gettimeoffset (void)
{
	unsigned long ticks, offset = 0;

	/* read VIA1 timer 2 current value */
	ticks = via1[vT1CL] | (via1[vT1CH] << 8);
	/* The probability of underflow is less than 2% */
	if (ticks > MAC_CLOCK_TICK - MAC_CLOCK_TICK / 50)
		/* Check for pending timer interrupt in VIA1 IFR */
		if (via1[vIFR] & 0x40) offset = TICK_SIZE;

	ticks = MAC_CLOCK_TICK - ticks;
	ticks = ticks * 10000L / MAC_CLOCK_TICK;

	return ticks + offset;
}

/*
 * Flush the L2 cache on Macs that have it by flipping
 * the system into 24-bit mode for an instant.
 */

void via_flush_cache(void)
{
	via2[gBufB] &= ~VIA2B_vMode32;
	via2[gBufB] |= VIA2B_vMode32;
}

/*
 * Return the status of the L2 cache on a IIci
 */

int via_get_cache_disable(void)
{
	/* Safeguard against being called accidentally */
	if (!via2) {
		printk(KERN_ERR "via_get_cache_disable called on a non-VIA machine!\n");
		return 1;
	}

	return (int) via2[gBufB] & VIA2B_vCDis;
}

/*
 * Initialize VIA2 for Nubus access
 */

void __init via_nubus_init(void)
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

	/* Disable all the slot interrupts (where possible). */

	switch (macintosh_config->via_type) {
	case MAC_VIA_II:
		/* Just make the port A lines inputs. */
		switch(macintosh_config->ident) {
		case MAC_MODEL_II:
		case MAC_MODEL_IIX:
		case MAC_MODEL_IICX:
		case MAC_MODEL_SE30:
			/* The top two bits are RAM size outputs. */
			via2[vDirA] &= 0xC0;
			break;
		default:
			via2[vDirA] &= 0x80;
		}
		break;
	case MAC_VIA_IIci:
		/* RBV. Disable all the slot interrupts. SIER works like IER. */
		via2[rSIER] = 0x7F;
		break;
	case MAC_VIA_QUADRA:
		/* Disable the inactive slot interrupts by making those lines outputs. */
		if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
		    (macintosh_config->adb_type != MAC_ADB_PB2)) {
			via2[vBufA] |= 0x7F;
			via2[vDirA] |= 0x7F;
		}
		break;
	}
}

/*
 * The generic VIA interrupt routines (shamelessly stolen from Alan Cox's
 * via6522.c :-), disable/pending masks added.
 */

irqreturn_t via1_irq(int irq, void *dev_id)
{
	int irq_num;
	unsigned char irq_bit, events;

	events = via1[vIFR] & via1[vIER] & 0x7F;
	if (!events)
		return IRQ_NONE;

	irq_num = VIA1_SOURCE_BASE;
	irq_bit = 1;
	do {
		if (events & irq_bit) {
			via1[vIFR] = irq_bit;
			m68k_handle_int(irq_num);
		}
		++irq_num;
		irq_bit <<= 1;
	} while (events >= irq_bit);
	return IRQ_HANDLED;
}

irqreturn_t via2_irq(int irq, void *dev_id)
{
	int irq_num;
	unsigned char irq_bit, events;

	events = via2[gIFR] & via2[gIER] & 0x7F;
	if (!events)
		return IRQ_NONE;

	irq_num = VIA2_SOURCE_BASE;
	irq_bit = 1;
	do {
		if (events & irq_bit) {
			via2[gIFR] = irq_bit | rbv_clear;
			m68k_handle_int(irq_num);
		}
		++irq_num;
		irq_bit <<= 1;
	} while (events >= irq_bit);
	return IRQ_HANDLED;
}

/*
 * Dispatch Nubus interrupts. We are called as a secondary dispatch by the
 * VIA2 dispatcher as a fast interrupt handler.
 */

irqreturn_t via_nubus_irq(int irq, void *dev_id)
{
	int slot_irq;
	unsigned char slot_bit, events;

	events = ~via2[gBufA] & 0x7F;
	if (rbv_present)
		events &= via2[rSIER];
	else
		events &= ~via2[vDirA];
	if (!events)
		return IRQ_NONE;

	do {
		slot_irq = IRQ_NUBUS_F;
		slot_bit = 0x40;
		do {
			if (events & slot_bit) {
				events &= ~slot_bit;
				m68k_handle_int(slot_irq);
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
	return IRQ_HANDLED;
}

void via_irq_enable(int irq) {
	int irq_src	= IRQ_SRC(irq);
	int irq_idx	= IRQ_IDX(irq);

#ifdef DEBUG_IRQUSE
	printk(KERN_DEBUG "via_irq_enable(%d)\n", irq);
#endif

	if (irq_src == 1) {
		via1[vIER] = IER_SET_BIT(irq_idx);
	} else if (irq_src == 2) {
		if (irq != IRQ_MAC_NUBUS || nubus_disabled == 0)
			via2[gIER] = IER_SET_BIT(irq_idx);
	} else if (irq_src == 7) {
		switch (macintosh_config->via_type) {
		case MAC_VIA_II:
			nubus_disabled &= ~(1 << irq_idx);
			/* Enable the CA1 interrupt when no slot is disabled. */
			if (!nubus_disabled)
				via2[gIER] = IER_SET_BIT(1);
			break;
		case MAC_VIA_IIci:
			/* On RBV, enable the slot interrupt.
			 * SIER works like IER.
			 */
			via2[rSIER] = IER_SET_BIT(irq_idx);
			break;
		case MAC_VIA_QUADRA:
			/* Make the port A line an input to enable the slot irq.
			 * But not on PowerBooks, that's ADB.
			 */
			if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
			    (macintosh_config->adb_type != MAC_ADB_PB2))
				via2[vDirA] &= ~(1 << irq_idx);
			break;
		}
	}
}

void via_irq_disable(int irq) {
	int irq_src	= IRQ_SRC(irq);
	int irq_idx	= IRQ_IDX(irq);

#ifdef DEBUG_IRQUSE
	printk(KERN_DEBUG "via_irq_disable(%d)\n", irq);
#endif

	if (irq_src == 1) {
		via1[vIER] = IER_CLR_BIT(irq_idx);
	} else if (irq_src == 2) {
		via2[gIER] = IER_CLR_BIT(irq_idx);
	} else if (irq_src == 7) {
		switch (macintosh_config->via_type) {
		case MAC_VIA_II:
			nubus_disabled |= 1 << irq_idx;
			if (nubus_disabled)
				via2[gIER] = IER_CLR_BIT(1);
			break;
		case MAC_VIA_IIci:
			via2[rSIER] = IER_CLR_BIT(irq_idx);
			break;
		case MAC_VIA_QUADRA:
			if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
			    (macintosh_config->adb_type != MAC_ADB_PB2))
				via2[vDirA] |= 1 << irq_idx;
			break;
		}
	}
}

void via_irq_clear(int irq) {
	int irq_src	= IRQ_SRC(irq);
	int irq_idx	= IRQ_IDX(irq);
	int irq_bit	= 1 << irq_idx;

	if (irq_src == 1) {
		via1[vIFR] = irq_bit;
	} else if (irq_src == 2) {
		via2[gIFR] = irq_bit | rbv_clear;
	} else if (irq_src == 7) {
		/* FIXME: There is no way to clear an individual nubus slot
		 * IRQ flag, other than getting the device to do it.
		 */
	}
}

/*
 * Returns nonzero if an interrupt is pending on the given
 * VIA/IRQ combination.
 */

int via_irq_pending(int irq)
{
	int irq_src	= IRQ_SRC(irq);
	int irq_idx	= IRQ_IDX(irq);
	int irq_bit	= 1 << irq_idx;

	if (irq_src == 1) {
		return via1[vIFR] & irq_bit;
	} else if (irq_src == 2) {
		return via2[gIFR] & irq_bit;
	} else if (irq_src == 7) {
		/* Always 0 for MAC_VIA_QUADRA if the slot irq is disabled. */
		return ~via2[gBufA] & irq_bit;
	}
	return 0;
}
