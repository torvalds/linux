/*
 *	6522 Versatile Interface Adapter (VIA)
 *
 *	There are two of these on the Mac II. Some IRQ's are vectored
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
 * PRAM/RTC access algorithms are from the NetBSD RTC toolkit version 1.08b
 * by Erik Vogan and adapted to Linux by Joshua M. Thompson (funaho@jurai.org)
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/bootinfo.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/machw.h>
#include <asm/mac_via.h>
#include <asm/mac_psc.h>

volatile __u8 *via1, *via2;
#if 0
/* See note in mac_via.h about how this is possibly not useful */
volatile long *via_memory_bogon=(long *)&via_memory_bogon;
#endif
int  rbv_present,via_alt_mapping;
__u8 rbv_clear;

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

static int  nubus_active;

void via_debug_dump(void);
irqreturn_t via1_irq(int, void *, struct pt_regs *);
irqreturn_t via2_irq(int, void *, struct pt_regs *);
irqreturn_t via_nubus_irq(int, void *, struct pt_regs *);
void via_irq_enable(int irq);
void via_irq_disable(int irq);
void via_irq_clear(int irq);

extern irqreturn_t mac_scc_dispatch(int, void *, struct pt_regs *);
extern int oss_present;

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
		printk(KERN_INFO "an RBV\n");
	} else if (oss_present) {
		printk(KERN_INFO "an OSS\n");
	} else {
		printk(KERN_INFO "a 6522 or clone\n");
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
	via1[vACR] &= 0x3F;

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

	if (oss_present) return;

#if 1
	/* Some machines support an alternate IRQ mapping that spreads  */
	/* Ethernet and Sound out to their own autolevel IRQs and moves */
	/* VIA1 to level 6. A/UX uses this mapping and we do too.  Note */
	/* that the IIfx emulates this alternate mapping using the OSS. */

	switch(macintosh_config->ident) {
		case MAC_MODEL_C610:
		case MAC_MODEL_Q610:
		case MAC_MODEL_C650:
		case MAC_MODEL_Q650:
		case MAC_MODEL_Q700:
		case MAC_MODEL_Q800:
		case MAC_MODEL_Q900:
		case MAC_MODEL_Q950:
			via_alt_mapping = 1;
			via1[vDirB] |= 0x40;
			via1[vBufB] &= ~0x40;
			break;
		default:
			via_alt_mapping = 0;
			break;
	}
#else
	via_alt_mapping = 0;
#endif

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
		via2[vACR] &= 0x3F;
	}
}

/*
 * Start the 100 Hz clock
 */

void __init via_init_clock(irqreturn_t (*func)(int, void *, struct pt_regs *))
{
	via1[vACR] |= 0x40;
	via1[vT1LL] = MAC_CLOCK_LOW;
	via1[vT1LH] = MAC_CLOCK_HIGH;
	via1[vT1CL] = MAC_CLOCK_LOW;
	via1[vT1CH] = MAC_CLOCK_HIGH;

	request_irq(IRQ_MAC_TIMER_1, func, IRQ_FLG_LOCK, "timer", func);
}

/*
 * Register the interrupt dispatchers for VIA or RBV machines only.
 */

void __init via_register_interrupts(void)
{
	if (via_alt_mapping) {
		request_irq(IRQ_AUTO_1, via1_irq,
				IRQ_FLG_LOCK|IRQ_FLG_FAST, "software",
				(void *) via1);
		request_irq(IRQ_AUTO_6, via1_irq,
				IRQ_FLG_LOCK|IRQ_FLG_FAST, "via1",
				(void *) via1);
	} else {
		request_irq(IRQ_AUTO_1, via1_irq,
				IRQ_FLG_LOCK|IRQ_FLG_FAST, "via1",
				(void *) via1);
	}
	request_irq(IRQ_AUTO_2, via2_irq, IRQ_FLG_LOCK|IRQ_FLG_FAST,
			"via2", (void *) via2);
	if (!psc_present) {
		request_irq(IRQ_AUTO_4, mac_scc_dispatch, IRQ_FLG_LOCK,
				"scc", mac_scc_dispatch);
	}
	request_irq(IRQ_MAC_NUBUS, via_nubus_irq, IRQ_FLG_LOCK|IRQ_FLG_FAST,
			"nubus", (void *) via2);
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
	/* don't set nubus_active = 0 here, it kills the Baboon */
	/* interrupt that we've already registered.		*/

	/* unlock nubus transactions */

	if (!rbv_present) {
		/* set the line to be an output on non-RBV machines */
		if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
		   (macintosh_config->adb_type != MAC_ADB_PB2)) {
			via2[vDirB] |= 0x02;
		}
	}

	/* this seems to be an ADB bit on PMU machines */
	/* according to MkLinux.  -- jmt               */

	if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
	    (macintosh_config->adb_type != MAC_ADB_PB2)) {
		via2[gBufB] |= 0x02;
	}

	/* disable nubus slot interrupts. */
	if (rbv_present) {
		via2[rSIER] = 0x7F;
		via2[rSIER] = nubus_active | 0x80;
	} else {
		/* These are ADB bits on PMU */
		if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
		   (macintosh_config->adb_type != MAC_ADB_PB2)) {
			switch(macintosh_config->ident)
			{
				case MAC_MODEL_II:
				case MAC_MODEL_IIX:
				case MAC_MODEL_IICX:
				case MAC_MODEL_SE30:
					via2[vBufA] |= 0x3F;
					via2[vDirA] = ~nubus_active | 0xc0;
					break;
				default:
					via2[vBufA] = 0xFF;
					via2[vDirA] = ~nubus_active;
			}
		}
	}
}

/*
 * The generic VIA interrupt routines (shamelessly stolen from Alan Cox's
 * via6522.c :-), disable/pending masks added.
 *
 * The new interrupt architecture in macints.c takes care of a lot of the
 * gruntwork for us, including tallying the interrupts and calling the
 * handlers on the linked list. All we need to do here is basically generate
 * the machspec interrupt number after clearing the interrupt.
 */

irqreturn_t via1_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int irq_bit, i;
	unsigned char events, mask;

	mask = via1[vIER] & 0x7F;
	if (!(events = via1[vIFR] & mask))
		return IRQ_NONE;

	for (i = 0, irq_bit = 1 ; i < 7 ; i++, irq_bit <<= 1)
		if (events & irq_bit) {
			via1[vIER] = irq_bit;
			m68k_handle_int(VIA1_SOURCE_BASE + i, regs);
			via1[vIFR] = irq_bit;
			via1[vIER] = irq_bit | 0x80;
		}

#if 0 /* freakin' pmu is doing weird stuff */
	if (!oss_present) {
		/* This (still) seems to be necessary to get IDE
		   working.  However, if you enable VBL interrupts,
		   you're screwed... */
		/* FIXME: should we check the SLOTIRQ bit before
                   pulling this stunt? */
		/* No, it won't be set. that's why we're doing this. */
		via_irq_disable(IRQ_MAC_NUBUS);
		via_irq_clear(IRQ_MAC_NUBUS);
		m68k_handle_int(IRQ_MAC_NUBUS, regs);
		via_irq_enable(IRQ_MAC_NUBUS);
	}
#endif
	return IRQ_HANDLED;
}

irqreturn_t via2_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int irq_bit, i;
	unsigned char events, mask;

	mask = via2[gIER] & 0x7F;
	if (!(events = via2[gIFR] & mask))
		return IRQ_NONE;

	for (i = 0, irq_bit = 1 ; i < 7 ; i++, irq_bit <<= 1)
		if (events & irq_bit) {
			via2[gIER] = irq_bit;
			via2[gIFR] = irq_bit | rbv_clear;
			m68k_handle_int(VIA2_SOURCE_BASE + i, regs);
			via2[gIER] = irq_bit | 0x80;
		}
	return IRQ_HANDLED;
}

/*
 * Dispatch Nubus interrupts. We are called as a secondary dispatch by the
 * VIA2 dispatcher as a fast interrupt handler.
 */

irqreturn_t via_nubus_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int irq_bit, i;
	unsigned char events;

	if (!(events = ~via2[gBufA] & nubus_active))
		return IRQ_NONE;

	for (i = 0, irq_bit = 1 ; i < 7 ; i++, irq_bit <<= 1) {
		if (events & irq_bit) {
			via_irq_disable(NUBUS_SOURCE_BASE + i);
			m68k_handle_int(NUBUS_SOURCE_BASE + i, regs);
			via_irq_enable(NUBUS_SOURCE_BASE + i);
		}
	}
	return IRQ_HANDLED;
}

void via_irq_enable(int irq) {
	int irq_src	= IRQ_SRC(irq);
	int irq_idx	= IRQ_IDX(irq);
	int irq_bit	= 1 << irq_idx;

#ifdef DEBUG_IRQUSE
	printk(KERN_DEBUG "via_irq_enable(%d)\n", irq);
#endif

	if (irq_src == 1) {
		via1[vIER] = irq_bit | 0x80;
	} else if (irq_src == 2) {
		/*
		 * Set vPCR for SCSI interrupts (but not on RBV)
		 */
		if ((irq_idx == 0) && !rbv_present) {
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
		via2[gIER] = irq_bit | 0x80;
	} else if (irq_src == 7) {
		nubus_active |= irq_bit;
		if (rbv_present) {
			/* enable the slot interrupt. SIER works like IER. */
			via2[rSIER] = IER_SET_BIT(irq_idx);
		} else {
			/* Make sure the bit is an input, to enable the irq */
			/* But not on PowerBooks, that's ADB... */
			if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
			   (macintosh_config->adb_type != MAC_ADB_PB2)) {
				switch(macintosh_config->ident)
				{
					case MAC_MODEL_II:
					case MAC_MODEL_IIX:
					case MAC_MODEL_IICX:
					case MAC_MODEL_SE30:
						via2[vDirA] &= (~irq_bit | 0xc0);
						break;
					default:
						via2[vDirA] &= ~irq_bit;
				}
			}
		}
	}
}

void via_irq_disable(int irq) {
	int irq_src	= IRQ_SRC(irq);
	int irq_idx	= IRQ_IDX(irq);
	int irq_bit	= 1 << irq_idx;

#ifdef DEBUG_IRQUSE
	printk(KERN_DEBUG "via_irq_disable(%d)\n", irq);
#endif

	if (irq_src == 1) {
		via1[vIER] = irq_bit;
	} else if (irq_src == 2) {
		via2[gIER] = irq_bit;
	} else if (irq_src == 7) {
		if (rbv_present) {
			/* disable the slot interrupt.  SIER works like IER. */
			via2[rSIER] = IER_CLR_BIT(irq_idx);
		} else {
			/* disable the nubus irq by changing dir to output */
			/* except on PMU */
			if ((macintosh_config->adb_type != MAC_ADB_PB1) &&
			   (macintosh_config->adb_type != MAC_ADB_PB2)) {
				via2[vDirA] |= irq_bit;
			}
		}
		nubus_active &= ~irq_bit;
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
		/* FIXME: hmm.. */
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
		return ~via2[gBufA] & irq_bit;
	}
	return 0;
}
