/*
 *
 * By Dustin McIntire (dustin@sensoria.com) (c)2001
 *
 * Setup and IRQ handling code for the HD64465 companion chip.
 * by Greg Banks <gbanks@pocketpenguins.com>
 * Copyright (c) 2000 PocketPenguins Inc
 *
 * Derived from setup_hd64465.c which bore the message:
 * Greg Banks <gbanks@pocketpenguins.com>
 * Copyright (c) 2000 PocketPenguins Inc and
 * Copyright (C) 2000 YAEGASHI Takeshi
 * and setup_cqreek.c which bore message:
 * Copyright (C) 2000  Niibe Yutaka
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IRQ functions for a Hitachi Big Sur Evaluation Board.
 *
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/bigsur/io.h>
#include <asm/hd64465/hd64465.h>
#include <asm/bigsur/bigsur.h>

//#define BIGSUR_DEBUG 3
#undef BIGSUR_DEBUG

#ifdef BIGSUR_DEBUG
#define DPRINTK(args...)        printk(args)
#define DIPRINTK(n, args...)    if (BIGSUR_DEBUG>(n)) printk(args)
#else
#define DPRINTK(args...)
#define DIPRINTK(n, args...)
#endif /* BIGSUR_DEBUG */

#ifdef CONFIG_HD64465
extern int hd64465_irq_demux(int irq);
#endif /* CONFIG_HD64465 */


/*===========================================================*/
//              Big Sur CPLD IRQ Routines
/*===========================================================*/

/* Level 1 IRQ routines */
static void disable_bigsur_l1irq(unsigned int irq)
{
        unsigned long flags;
        unsigned char mask;
        unsigned int mask_port = ((irq - BIGSUR_IRQ_LOW)/8) ? BIGSUR_IRLMR1 : BIGSUR_IRLMR0;
        unsigned char bit =  (1 << ((irq - MGATE_IRQ_LOW)%8) );

        if(irq >= BIGSUR_IRQ_LOW && irq < BIGSUR_IRQ_HIGH) {
                DPRINTK("Disable L1 IRQ %d\n", irq);
                DIPRINTK(2,"disable_bigsur_l1irq: IMR=0x%08x mask=0x%x\n",
                        mask_port, bit);
                local_irq_save(flags);

                /* Disable IRQ - set mask bit */
                mask = inb(mask_port) | bit;
                outb(mask, mask_port);
                local_irq_restore(flags);
                return;
        }
        DPRINTK("disable_bigsur_l1irq: Invalid IRQ %d\n", irq);
}

static void enable_bigsur_l1irq(unsigned int irq)
{
        unsigned long flags;
        unsigned char mask;
        unsigned int mask_port = ((irq - BIGSUR_IRQ_LOW)/8) ? BIGSUR_IRLMR1 : BIGSUR_IRLMR0;
        unsigned char bit =  (1 << ((irq - MGATE_IRQ_LOW)%8) );

        if(irq >= BIGSUR_IRQ_LOW && irq < BIGSUR_IRQ_HIGH) {
                DPRINTK("Enable L1 IRQ %d\n", irq);
                DIPRINTK(2,"enable_bigsur_l1irq: IMR=0x%08x mask=0x%x\n",
                        mask_port, bit);
                local_irq_save(flags);
                /* Enable L1 IRQ - clear mask bit */
                mask = inb(mask_port) & ~bit;
                outb(mask, mask_port);
                local_irq_restore(flags);
                return;
        }
        DPRINTK("enable_bigsur_l1irq: Invalid IRQ %d\n", irq);
}


/* Level 2 irq masks and registers for L2 decoding */
/* Level2 bitmasks for each level 1 IRQ */
const u32 bigsur_l2irq_mask[] =
    {0x40,0x80,0x08,0x01,0x01,0x3C,0x3E,0xFF,0x40,0x80,0x06,0x03};
/* Level2 to ISR[n] map for each level 1 IRQ */
const u32 bigsur_l2irq_reg[]  =
    {   2,   2,   3,   3,   1,   2,   1,   0,   1,   1,   3,   2};
/* Level2 to Level 1 IRQ map */
const u32 bigsur_l2_l1_map[]  =
    {7,7,7,7,7,7,7,7, 4,6,6,6,6,6,8,9, 11,11,5,5,5,5,0,1, 3,10,10,2,-1,-1,-1,-1};
/* IRQ inactive level (high or low) */
const u32 bigsur_l2_inactv_state[]  =   {0x00, 0xBE, 0xFC, 0xF7};

/* CPLD external status and mask registers base and offsets */
static const u32 isr_base = BIGSUR_IRQ0;
static const u32 isr_offset = BIGSUR_IRQ0 - BIGSUR_IRQ1;
static const u32 imr_base = BIGSUR_IMR0;
static const u32 imr_offset = BIGSUR_IMR0 - BIGSUR_IMR1;

#define REG_NUM(irq)  ((irq-BIGSUR_2NDLVL_IRQ_LOW)/8 )

/* Level 2 IRQ routines */
static void disable_bigsur_l2irq(unsigned int irq)
{
        unsigned long flags;
        unsigned char mask;
        unsigned char bit = 1 << ((irq-BIGSUR_2NDLVL_IRQ_LOW)%8);
        unsigned int mask_port = imr_base - REG_NUM(irq)*imr_offset;

    if(irq >= BIGSUR_2NDLVL_IRQ_LOW && irq < BIGSUR_2NDLVL_IRQ_HIGH) {
                DPRINTK("Disable L2 IRQ %d\n", irq);
                DIPRINTK(2,"disable_bigsur_l2irq: IMR=0x%08x mask=0x%x\n",
                        mask_port, bit);
                local_irq_save(flags);

                /* Disable L2 IRQ - set mask bit */
                mask = inb(mask_port) | bit;
                outb(mask, mask_port);
                local_irq_restore(flags);
                return;
        }
        DPRINTK("disable_bigsur_l2irq: Invalid IRQ %d\n", irq);
}

static void enable_bigsur_l2irq(unsigned int irq)
{
        unsigned long flags;
        unsigned char mask;
        unsigned char bit = 1 << ((irq-BIGSUR_2NDLVL_IRQ_LOW)%8);
        unsigned int mask_port = imr_base - REG_NUM(irq)*imr_offset;

    if(irq >= BIGSUR_2NDLVL_IRQ_LOW && irq < BIGSUR_2NDLVL_IRQ_HIGH) {
                DPRINTK("Enable L2 IRQ %d\n", irq);
                DIPRINTK(2,"enable_bigsur_l2irq: IMR=0x%08x mask=0x%x\n",
                        mask_port, bit);
                local_irq_save(flags);

                /* Enable L2 IRQ - clear mask bit */
                mask = inb(mask_port) & ~bit;
                outb(mask, mask_port);
                local_irq_restore(flags);
                return;
        }
        DPRINTK("enable_bigsur_l2irq: Invalid IRQ %d\n", irq);
}

static void mask_and_ack_bigsur(unsigned int irq)
{
        DPRINTK("mask_and_ack_bigsur IRQ %d\n", irq);
        if(irq >= BIGSUR_IRQ_LOW && irq < BIGSUR_IRQ_HIGH)
                disable_bigsur_l1irq(irq);
        else
                disable_bigsur_l2irq(irq);
}

static void end_bigsur_irq(unsigned int irq)
{
        DPRINTK("end_bigsur_irq IRQ %d\n", irq);
        if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS))) {
                if(irq >= BIGSUR_IRQ_LOW && irq < BIGSUR_IRQ_HIGH)
                        enable_bigsur_l1irq(irq);
                else
                        enable_bigsur_l2irq(irq);
        }
}

static unsigned int startup_bigsur_irq(unsigned int irq)
{
        u8 mask;
        u32 reg;

        DPRINTK("startup_bigsur_irq IRQ %d\n", irq);

        if(irq >= BIGSUR_IRQ_LOW && irq < BIGSUR_IRQ_HIGH) {
                /* Enable the L1 IRQ */
                enable_bigsur_l1irq(irq);
                /* Enable all L2 IRQs in this L1 IRQ */
                mask = ~(bigsur_l2irq_mask[irq-BIGSUR_IRQ_LOW]);
                reg = imr_base - bigsur_l2irq_reg[irq-BIGSUR_IRQ_LOW] * imr_offset;
                mask &= inb(reg);
                outb(mask,reg);
                DIPRINTK(2,"startup_bigsur_irq: IMR=0x%08x mask=0x%x\n",reg,inb(reg));
        }
        else {
                /* Enable the L2 IRQ - clear mask bit */
                enable_bigsur_l2irq(irq);
                /* Enable the L1 bit masking this L2 IRQ */
                enable_bigsur_l1irq(bigsur_l2_l1_map[irq-BIGSUR_2NDLVL_IRQ_LOW]);
                DIPRINTK(2,"startup_bigsur_irq: L1=%d L2=%d\n",
                        bigsur_l2_l1_map[irq-BIGSUR_2NDLVL_IRQ_LOW],irq);
        }
        return 0;
}

static void shutdown_bigsur_irq(unsigned int irq)
{
        DPRINTK("shutdown_bigsur_irq IRQ %d\n", irq);
        if(irq >= BIGSUR_IRQ_LOW && irq < BIGSUR_IRQ_HIGH)
                disable_bigsur_l1irq(irq);
        else
                disable_bigsur_l2irq(irq);
}

/* Define the IRQ structures for the L1 and L2 IRQ types */
static struct hw_interrupt_type bigsur_l1irq_type = {
        "BigSur-CPLD-Level1-IRQ",
        startup_bigsur_irq,
        shutdown_bigsur_irq,
        enable_bigsur_l1irq,
        disable_bigsur_l1irq,
        mask_and_ack_bigsur,
        end_bigsur_irq
};

static struct hw_interrupt_type bigsur_l2irq_type = {
        "BigSur-CPLD-Level2-IRQ",
        startup_bigsur_irq,
        shutdown_bigsur_irq,
        enable_bigsur_l2irq,
        disable_bigsur_l2irq,
        mask_and_ack_bigsur,
        end_bigsur_irq
};


static void make_bigsur_l1isr(unsigned int irq) {

        /* sanity check first */
        if(irq >= BIGSUR_IRQ_LOW && irq < BIGSUR_IRQ_HIGH) {
                /* save the handler in the main description table */
                irq_desc[irq].handler = &bigsur_l1irq_type;
                irq_desc[irq].status = IRQ_DISABLED;
                irq_desc[irq].action = 0;
                irq_desc[irq].depth = 1;

                disable_bigsur_l1irq(irq);
                return;
        }
        DPRINTK("make_bigsur_l1isr: bad irq, %d\n", irq);
        return;
}

static void make_bigsur_l2isr(unsigned int irq) {

        /* sanity check first */
        if(irq >= BIGSUR_2NDLVL_IRQ_LOW && irq < BIGSUR_2NDLVL_IRQ_HIGH) {
                /* save the handler in the main description table */
                irq_desc[irq].handler = &bigsur_l2irq_type;
                irq_desc[irq].status = IRQ_DISABLED;
                irq_desc[irq].action = 0;
                irq_desc[irq].depth = 1;

                disable_bigsur_l2irq(irq);
                return;
        }
        DPRINTK("make_bigsur_l2isr: bad irq, %d\n", irq);
        return;
}

/* The IRQ's will be decoded as follows:
 * If a level 2 handler exists and there is an unmasked active
 * IRQ, the 2nd level handler will be called.
 * If a level 2 handler does not exist for the active IRQ
 * the 1st level handler will be called.
 */

int bigsur_irq_demux(int irq)
{
        int dmux_irq = irq;
        u8 mask, actv_irqs;
        u32 reg_num;

        DIPRINTK(3,"bigsur_irq_demux, irq=%d\n", irq);
        /* decode the 1st level IRQ */
        if(irq >= BIGSUR_IRQ_LOW && irq < BIGSUR_IRQ_HIGH) {
                /* Get corresponding L2 ISR bitmask and ISR number */
                mask = bigsur_l2irq_mask[irq-BIGSUR_IRQ_LOW];
                reg_num = bigsur_l2irq_reg[irq-BIGSUR_IRQ_LOW];
                /* find the active IRQ's (XOR with inactive level)*/
                actv_irqs = inb(isr_base-reg_num*isr_offset) ^
                                        bigsur_l2_inactv_state[reg_num];
                /* decode active IRQ's */
                actv_irqs = actv_irqs & mask & ~(inb(imr_base-reg_num*imr_offset));
                /* if NEZ then we have an active L2 IRQ */
                if(actv_irqs) dmux_irq = ffz(~actv_irqs) + reg_num*8+BIGSUR_2NDLVL_IRQ_LOW;
                /* if no 2nd level IRQ action, but has 1st level, use 1st level handler */
                if(!irq_desc[dmux_irq].action && irq_desc[irq].action)
                        dmux_irq = irq;
                DIPRINTK(1,"bigsur_irq_demux: irq=%d dmux_irq=%d mask=0x%04x reg=%d\n",
                        irq, dmux_irq, mask, reg_num);
        }
#ifdef CONFIG_HD64465
        dmux_irq = hd64465_irq_demux(dmux_irq);
#endif /* CONFIG_HD64465 */
        DIPRINTK(3,"bigsur_irq_demux, demux_irq=%d\n", dmux_irq);

        return dmux_irq;
}

/*===========================================================*/
//              Big Sur Init Routines
/*===========================================================*/
void __init init_bigsur_IRQ(void)
{
        int i;

        if (!MACH_BIGSUR) return;

        /* Create ISR's for Big Sur CPLD IRQ's */
        /*==============================================================*/
        for(i=BIGSUR_IRQ_LOW;i<BIGSUR_IRQ_HIGH;i++)
                make_bigsur_l1isr(i);

        printk(KERN_INFO "Big Sur CPLD L1 interrupts %d to %d.\n",
                BIGSUR_IRQ_LOW,BIGSUR_IRQ_HIGH);

        for(i=BIGSUR_2NDLVL_IRQ_LOW;i<BIGSUR_2NDLVL_IRQ_HIGH;i++)
                make_bigsur_l2isr(i);

        printk(KERN_INFO "Big Sur CPLD L2 interrupts %d to %d.\n",
                BIGSUR_2NDLVL_IRQ_LOW,BIGSUR_2NDLVL_IRQ_HIGH);

}
