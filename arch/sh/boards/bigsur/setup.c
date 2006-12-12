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
 * Setup functions for a Hitachi Big Sur Evaluation Board.
 * 
 */

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
#include <asm/machvec.h>
#include <asm/bigsur/io.h>
#include <asm/hd64465/hd64465.h>
#include <asm/bigsur/bigsur.h>

/*===========================================================*/
//		Big Sur Init Routines	
/*===========================================================*/

static void __init bigsur_setup(char **cmdline_p)
{
	/* Mask all 2nd level IRQ's */
	outb(-1,BIGSUR_IMR0);
	outb(-1,BIGSUR_IMR1);
	outb(-1,BIGSUR_IMR2);
	outb(-1,BIGSUR_IMR3);

	/* Mask 1st level interrupts */
	outb(-1,BIGSUR_IRLMR0);
	outb(-1,BIGSUR_IRLMR1);

#if defined (CONFIG_HD64465) && defined (CONFIG_SERIAL) 
	/* remap IO ports for first ISA serial port to HD64465 UART */
	bigsur_port_map(0x3f8, 8, CONFIG_HD64465_IOBASE + 0x8000, 1);
#endif /* CONFIG_HD64465 && CONFIG_SERIAL */
	/* TODO: setup IDE registers */
	bigsur_port_map(BIGSUR_IDECTL_IOPORT, 2, BIGSUR_ICTL, 8);
	/* Setup the Ethernet port to BIGSUR_ETHER_IOPORT */
	bigsur_port_map(BIGSUR_ETHER_IOPORT, 16, BIGSUR_ETHR+BIGSUR_ETHER_IOPORT, 0);
	/* set page to 1 */
	outw(1, BIGSUR_ETHR+0xe);
	/* set the IO port to BIGSUR_ETHER_IOPORT */
	outw(BIGSUR_ETHER_IOPORT<<3, BIGSUR_ETHR+0x2);
}

/*
 * The Machine Vector
 */
extern void heartbeat_bigsur(void);
extern void init_bigsur_IRQ(void);

struct sh_machine_vector mv_bigsur __initmv = {
	.mv_name		= "Big Sur",
	.mv_setup		= bigsur_setup,

	.mv_isa_port2addr	= bigsur_isa_port2addr,
	.mv_irq_demux       	= bigsur_irq_demux,

	.mv_init_irq		= init_bigsur_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_bigsur,
#endif
};
ALIAS_MV(bigsur)
