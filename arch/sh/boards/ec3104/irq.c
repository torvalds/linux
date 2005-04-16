/*
 * linux/arch/sh/boards/ec3104/irq.c
 * EC3104 companion chip support
 *
 * Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *
 */

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ec3104/ec3104.h>

/* This is for debugging mostly;  here's the table that I intend to keep
 * in here:
 *
 *   index      function        base addr       power           interrupt bit
 *       0      power           b0ec0000        ---             00000001 (unused)
 *       1      irqs            b0ec1000        ---             00000002 (unused)
 *       2      ??              b0ec2000        b0ec0008        00000004
 *       3      PS2 (1)         b0ec3000        b0ec000c        00000008
 *       4      PS2 (2)         b0ec4000        b0ec0010        00000010
 *       5      ??              b0ec5000        b0ec0014        00000020
 *       6      I2C             b0ec6000        b0ec0018        00000040
 *       7      serial (1)      b0ec7000        b0ec001c        00000080
 *       8      serial (2)      b0ec8000        b0ec0020        00000100
 *       9      serial (3)      b0ec9000        b0ec0024        00000200
 *      10      serial (4)      b0eca000        b0ec0028        00000400
 *      12      GPIO (1)        b0ecc000        b0ec0030
 *      13      GPIO (2)        b0ecc000        b0ec0030
 *      16      pcmcia (1)      b0ed0000        b0ec0040        00010000
 *      17      pcmcia (2)      b0ed1000        b0ec0044        00020000
 */

/* I used the register names from another interrupt controller I worked with,
 * since it seems to be identical to the ec3104 except that all bits are
 * inverted:
 *
 * IRR: Interrupt Request Register (pending and enabled interrupts)
 * IMR: Interrupt Mask Register (which interrupts are enabled)
 * IPR: Interrupt Pending Register (pending interrupts, even disabled ones)
 *
 * 0 bits mean pending or enabled, 1 bits mean not pending or disabled.  all
 * IRQs seem to be level-triggered.
 */

#define EC3104_IRR (EC3104_BASE + 0x1000)
#define EC3104_IMR (EC3104_BASE + 0x1004)
#define EC3104_IPR (EC3104_BASE + 0x1008)

#define ctrl_readl(addr) (*(volatile u32 *)(addr))
#define ctrl_writel(data,addr) (*(volatile u32 *)(addr) = (data))
#define ctrl_readb(addr) (*(volatile u8 *)(addr))

static char *ec3104_name(unsigned index)
{
        switch(index) {
        case 0:
                return "power management";
        case 1:
                return "interrupts";
        case 3:
                return "PS2 (1)";
        case 4:
                return "PS2 (2)";
        case 5:
                return "I2C (1)";
        case 6:
                return "I2C (2)";
        case 7:
                return "serial (1)";
        case 8:
                return "serial (2)";
        case 9:
                return "serial (3)";
        case 10:
                return "serial (4)";
        case 16:
                return "pcmcia (1)";
        case 17:
                return "pcmcia (2)";
        default: {
                static char buf[32];

                sprintf(buf, "unknown (%d)", index);

                return buf;
                }
        }
}

int get_pending_interrupts(char *buf)
{
        u32 ipr;
        u32 bit;
        char *p = buf;

        p += sprintf(p, "pending: (");

        ipr = ctrl_inl(EC3104_IPR);

        for (bit = 1; bit < 32; bit++)
                if (!(ipr & (1<<bit)))
                        p += sprintf(p, "%s ", ec3104_name(bit));

        p += sprintf(p, ")\n");

        return p - buf;
}

static inline u32 ec3104_irq2mask(unsigned int irq)
{
        return (1 << (irq - EC3104_IRQBASE));
}

static inline void mask_ec3104_irq(unsigned int irq)
{
        u32 mask;

        mask = ctrl_readl(EC3104_IMR);

        mask |= ec3104_irq2mask(irq);

        ctrl_writel(mask, EC3104_IMR);
}

static inline void unmask_ec3104_irq(unsigned int irq)
{
        u32 mask;

        mask = ctrl_readl(EC3104_IMR);

        mask &= ~ec3104_irq2mask(irq);

        ctrl_writel(mask, EC3104_IMR);
}

static void disable_ec3104_irq(unsigned int irq)
{
        mask_ec3104_irq(irq);
}

static void enable_ec3104_irq(unsigned int irq)
{
        unmask_ec3104_irq(irq);
}

static void mask_and_ack_ec3104_irq(unsigned int irq)
{
        mask_ec3104_irq(irq);
}

static void end_ec3104_irq(unsigned int irq)
{
        if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
                unmask_ec3104_irq(irq);
}

static unsigned int startup_ec3104_irq(unsigned int irq)
{
        unmask_ec3104_irq(irq);

        return 0;
}

static void shutdown_ec3104_irq(unsigned int irq)
{
        mask_ec3104_irq(irq);

}

static struct hw_interrupt_type ec3104_int = {
        .typename       = "EC3104",
        .enable         = enable_ec3104_irq,
        .disable        = disable_ec3104_irq,
        .ack            = mask_and_ack_ec3104_irq,
        .end            = end_ec3104_irq,
        .startup        = startup_ec3104_irq,
        .shutdown       = shutdown_ec3104_irq,
};

/* Yuck.  the _demux API is ugly */
int ec3104_irq_demux(int irq)
{
        if (irq == EC3104_IRQ) {
                unsigned int mask;

                mask = ctrl_readl(EC3104_IRR);

                if (mask == 0xffffffff)
                        return EC3104_IRQ;
                else
                        return EC3104_IRQBASE + ffz(mask);
        }

        return irq;
}
