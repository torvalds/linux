/*
 * arch/sh/boards/dreamcast/irq.c
 *
 * Holly IRQ support for the Sega Dreamcast.
 *
 * Copyright (c) 2001, 2002 M. R. Brown <mrbrown@0xd6.org>
 *
 * This file is part of the LinuxDC project (www.linuxdc.org)
 * Released under the terms of the GNU GPL v2.0
 */

#include <linux/irq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dreamcast/sysasic.h>

/* Dreamcast System ASIC Hardware Events -

   The Dreamcast's System ASIC (a.k.a. Holly) is responsible for receiving
   hardware events from system peripherals and triggering an SH7750 IRQ.
   Hardware events can trigger IRQs 13, 11, or 9 depending on which bits are
   set in the Event Mask Registers (EMRs).  When a hardware event is
   triggered, it's corresponding bit in the Event Status Registers (ESRs)
   is set, and that bit should be rewritten to the ESR to acknowledge that
   event.

   There are three 32-bit ESRs located at 0xa05f8900 - 0xa05f6908.  Event
   types can be found in include/asm-sh/dreamcast/sysasic.h. There are three
   groups of EMRs that parallel the ESRs.  Each EMR group corresponds to an
   IRQ, so 0xa05f6910 - 0xa05f6918 triggers IRQ 13, 0xa05f6920 - 0xa05f6928
   triggers IRQ 11, and 0xa05f6930 - 0xa05f6938 triggers IRQ 9.

   In the kernel, these events are mapped to virtual IRQs so that drivers can
   respond to them as they would a normal interrupt.  In order to keep this
   mapping simple, the events are mapped as:

   6900/6910 - Events  0-31, IRQ 13
   6904/6924 - Events 32-63, IRQ 11
   6908/6938 - Events 64-95, IRQ  9

*/

#define ESR_BASE 0x005f6900    /* Base event status register */
#define EMR_BASE 0x005f6910    /* Base event mask register */

/* Helps us determine the EMR group that this event belongs to: 0 = 0x6910,
   1 = 0x6920, 2 = 0x6930; also determine the event offset */
#define LEVEL(event) (((event) - HW_EVENT_IRQ_BASE) / 32)

/* Return the hardware event's bit positon within the EMR/ESR */
#define EVENT_BIT(event) (((event) - HW_EVENT_IRQ_BASE) & 31)

/* For each of these *_irq routines, the IRQ passed in is the virtual IRQ
   (logically mapped to the corresponding bit for the hardware event). */

/* Disable the hardware event by masking its bit in its EMR */
static inline void disable_systemasic_irq(unsigned int irq)
{
        __u32 emr = EMR_BASE + (LEVEL(irq) << 4) + (LEVEL(irq) << 2);
        __u32 mask;

        mask = inl(emr);
        mask &= ~(1 << EVENT_BIT(irq));
        outl(mask, emr);
}

/* Enable the hardware event by setting its bit in its EMR */
static inline void enable_systemasic_irq(unsigned int irq)
{
        __u32 emr = EMR_BASE + (LEVEL(irq) << 4) + (LEVEL(irq) << 2);
        __u32 mask;

        mask = inl(emr);
        mask |= (1 << EVENT_BIT(irq));
        outl(mask, emr);
}

/* Acknowledge a hardware event by writing its bit back to its ESR */
static void ack_systemasic_irq(unsigned int irq)
{
        __u32 esr = ESR_BASE + (LEVEL(irq) << 2);
        disable_systemasic_irq(irq);
        outl((1 << EVENT_BIT(irq)), esr);
}

/* After a IRQ has been ack'd and responded to, it needs to be renabled */
static void end_systemasic_irq(unsigned int irq)
{
        if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
                enable_systemasic_irq(irq);
}

static unsigned int startup_systemasic_irq(unsigned int irq)
{
        enable_systemasic_irq(irq);

        return 0;
}

static void shutdown_systemasic_irq(unsigned int irq)
{
        disable_systemasic_irq(irq);
}

struct hw_interrupt_type systemasic_int = {
        .typename       = "System ASIC",
        .startup        = startup_systemasic_irq,
        .shutdown       = shutdown_systemasic_irq,
        .enable         = enable_systemasic_irq,
        .disable        = disable_systemasic_irq,
        .ack            = ack_systemasic_irq,
        .end            = end_systemasic_irq,
};

/*
 * Map the hardware event indicated by the processor IRQ to a virtual IRQ.
 */
int systemasic_irq_demux(int irq)
{
        __u32 emr, esr, status, level;
        __u32 j, bit;

        switch (irq) {
                case 13:
                        level = 0;
                        break;
                case 11:
                        level = 1;
                        break;
                case  9:
                        level = 2;
                        break;
                default:
                        return irq;
        }
        emr = EMR_BASE + (level << 4) + (level << 2);
        esr = ESR_BASE + (level << 2);

        /* Mask the ESR to filter any spurious, unwanted interrupts */
        status = inl(esr);
        status &= inl(emr);

        /* Now scan and find the first set bit as the event to map */
        for (bit = 1, j = 0; j < 32; bit <<= 1, j++) {
                if (status & bit) {
                        irq = HW_EVENT_IRQ_BASE + j + (level << 5);
                        return irq;
                }
        }

        /* Not reached */
        return irq;
}
