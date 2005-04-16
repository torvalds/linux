/*
 * linux/arch/sh/boards/se/770x/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine Support.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/se/se.h>

/*
 * Initialize IRQ setting
 */
void __init init_se_IRQ(void)
{
        /*
         * Super I/O (Just mimic PC):
         *  1: keyboard
         *  3: serial 0
         *  4: serial 1
         *  5: printer
         *  6: floppy
         *  8: rtc
         * 12: mouse
         * 14: ide0
         */
#if defined(CONFIG_CPU_SUBTYPE_SH7705)
	/* Disable all interrupts */
	ctrl_outw(0, BCR_ILCRA);
	ctrl_outw(0, BCR_ILCRB);
	ctrl_outw(0, BCR_ILCRC);
	ctrl_outw(0, BCR_ILCRD);
	ctrl_outw(0, BCR_ILCRE);
	ctrl_outw(0, BCR_ILCRF);
	ctrl_outw(0, BCR_ILCRG);
	/* This is default value */
	make_ipr_irq(0xf-0x2, BCR_ILCRA, 2, 0x2);
	make_ipr_irq(0xf-0xa, BCR_ILCRA, 1, 0xa);
	make_ipr_irq(0xf-0x5, BCR_ILCRB, 0, 0x5);
	make_ipr_irq(0xf-0x8, BCR_ILCRC, 1, 0x8);
	make_ipr_irq(0xf-0xc, BCR_ILCRC, 0, 0xc);
	make_ipr_irq(0xf-0xe, BCR_ILCRD, 3, 0xe);
	make_ipr_irq(0xf-0x3, BCR_ILCRD, 1, 0x3); /* LAN */
	make_ipr_irq(0xf-0xd, BCR_ILCRE, 2, 0xd);
	make_ipr_irq(0xf-0x9, BCR_ILCRE, 1, 0x9);
	make_ipr_irq(0xf-0x1, BCR_ILCRE, 0, 0x1);
	make_ipr_irq(0xf-0xf, BCR_ILCRF, 3, 0xf);
	make_ipr_irq(0xf-0xb, BCR_ILCRF, 1, 0xb);
	make_ipr_irq(0xf-0x7, BCR_ILCRG, 3, 0x7);
	make_ipr_irq(0xf-0x6, BCR_ILCRG, 2, 0x6);
	make_ipr_irq(0xf-0x4, BCR_ILCRG, 1, 0x4);
#else
        make_ipr_irq(14, BCR_ILCRA, 2, 0x0f-14);
        make_ipr_irq(12, BCR_ILCRA, 1, 0x0f-12);
        make_ipr_irq( 8, BCR_ILCRB, 1, 0x0f- 8);
        make_ipr_irq( 6, BCR_ILCRC, 3, 0x0f- 6);
        make_ipr_irq( 5, BCR_ILCRC, 2, 0x0f- 5);
        make_ipr_irq( 4, BCR_ILCRC, 1, 0x0f- 4);
        make_ipr_irq( 3, BCR_ILCRC, 0, 0x0f- 3);
        make_ipr_irq( 1, BCR_ILCRD, 3, 0x0f- 1);

        make_ipr_irq(10, BCR_ILCRD, 1, 0x0f-10); /* LAN */

        make_ipr_irq( 0, BCR_ILCRE, 3, 0x0f- 0); /* PCIRQ3 */
        make_ipr_irq(11, BCR_ILCRE, 2, 0x0f-11); /* PCIRQ2 */
        make_ipr_irq( 9, BCR_ILCRE, 1, 0x0f- 9); /* PCIRQ1 */
        make_ipr_irq( 7, BCR_ILCRE, 0, 0x0f- 7); /* PCIRQ0 */

        /* #2, #13 are allocated for SLOT IRQ #1 and #2 (for now) */
        /* NOTE: #2 and #13 are not used on PC */
        make_ipr_irq(13, BCR_ILCRG, 1, 0x0f-13); /* SLOTIRQ2 */
        make_ipr_irq( 2, BCR_ILCRG, 0, 0x0f- 2); /* SLOTIRQ1 */
#endif
}
