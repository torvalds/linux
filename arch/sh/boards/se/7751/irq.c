/*
 * linux/arch/sh/boards/se/7751/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine Support.
 *
 * Modified for 7751 Solution Engine by
 * Ian da Silva and Jeremy Siegel, 2001.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/se7751/se7751.h>

/*
 * Initialize IRQ setting
 */
void __init init_7751se_IRQ(void)
{

  /* Leave old Solution Engine code in for reference. */
#if defined(CONFIG_SH_SOLUTION_ENGINE)
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

#elif defined(CONFIG_SH_7751_SOLUTION_ENGINE)

        make_ipr_irq(13, BCR_ILCRD, 3, 2);

        /* Add additional calls to make_ipr_irq() as drivers are added
         * and tested.
         */
#endif

}
