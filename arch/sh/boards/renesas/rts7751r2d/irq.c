/*
 * linux/arch/sh/boards/renesas/rts7751r2d/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Technology Sales RTS7751R2D Support.
 *
 * Modified for RTS7751R2D by
 * Atom Create Engineering Co., Ltd. 2002.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/rts7751r2d/rts7751r2d.h>

#if defined(CONFIG_RTS7751R2D_REV11)
static int mask_pos[] = {11, 9, 8, 12, 10, 6, 5, 4, 7, 14, 13, 0, 0, 0, 0};
#else
static int mask_pos[] = {6, 11, 9, 8, 12, 10, 5, 4, 7, 14, 13, 0, 0, 0, 0};
#endif

extern int voyagergx_irq_demux(int irq);
extern void setup_voyagergx_irq(void);

static void enable_rts7751r2d_irq(unsigned int irq);
static void disable_rts7751r2d_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_rts7751r2d_irq disable_rts7751r2d_irq

static void ack_rts7751r2d_irq(unsigned int irq);
static void end_rts7751r2d_irq(unsigned int irq);

static unsigned int startup_rts7751r2d_irq(unsigned int irq)
{
	enable_rts7751r2d_irq(irq);
	return 0; /* never anything pending */
}

static void disable_rts7751r2d_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned short val;
	unsigned short mask = 0xffff ^ (0x0001 << mask_pos[irq]);

	/* Set the priority in IPR to 0 */
	local_irq_save(flags);
	val = ctrl_inw(IRLCNTR1);
	val &= mask;
	ctrl_outw(val, IRLCNTR1);
	local_irq_restore(flags);
}

static void enable_rts7751r2d_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned short val;
	unsigned short value = (0x0001 << mask_pos[irq]);

	/* Set priority in IPR back to original value */
	local_irq_save(flags);
	val = ctrl_inw(IRLCNTR1);
	val |= value;
	ctrl_outw(val, IRLCNTR1);
	local_irq_restore(flags);
}

int rts7751r2d_irq_demux(int irq)
{
	int demux_irq;

	demux_irq = voyagergx_irq_demux(irq);
	return demux_irq;
}

static void ack_rts7751r2d_irq(unsigned int irq)
{
	disable_rts7751r2d_irq(irq);
}

static void end_rts7751r2d_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_rts7751r2d_irq(irq);
}

static struct hw_interrupt_type rts7751r2d_irq_type = {
	"RTS7751R2D IRQ",
	startup_rts7751r2d_irq,
	shutdown_rts7751r2d_irq,
	enable_rts7751r2d_irq,
	disable_rts7751r2d_irq,
	ack_rts7751r2d_irq,
	end_rts7751r2d_irq,
};

static void make_rts7751r2d_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &rts7751r2d_irq_type;
	disable_rts7751r2d_irq(irq);
}

/*
 * Initialize IRQ setting
 */
void __init init_rts7751r2d_IRQ(void)
{
	int i;

	/* IRL0=KEY Input
	 * IRL1=Ethernet
	 * IRL2=CF Card
	 * IRL3=CF Card Insert
	 * IRL4=PCMCIA
	 * IRL5=VOYAGER
	 * IRL6=RTC Alarm
	 * IRL7=RTC Timer
	 * IRL8=SD Card
	 * IRL9=PCI Slot #1
	 * IRL10=PCI Slot #2
	 * IRL11=Extention #0
	 * IRL12=Extention #1
	 * IRL13=Extention #2
	 * IRL14=Extention #3
	 */

	for (i=0; i<15; i++)
		make_rts7751r2d_irq(i);

	setup_voyagergx_irq();
}
