/*
 * linux/arch/sh/boards/se/770x/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine Support.
 *
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/se.h>

static struct ipr_data se770x_ipr_map[] = {
#if defined(CONFIG_CPU_SUBTYPE_SH7705)
	/* This is default value */
	{ 0xf-0x2, BCR_ILCRA, 2, 0x2 },
	{ 0xf-0xa, BCR_ILCRA, 1, 0xa },
	{ 0xf-0x5, BCR_ILCRB, 0, 0x5 },
	{ 0xf-0x8, BCR_ILCRC, 1, 0x8 },
	{ 0xf-0xc, BCR_ILCRC, 0, 0xc },
	{ 0xf-0xe, BCR_ILCRD, 3, 0xe },
	{ 0xf-0x3, BCR_ILCRD, 1, 0x3 }, /* LAN */
	{ 0xf-0xd, BCR_ILCRE, 2, 0xd },
	{ 0xf-0x9, BCR_ILCRE, 1, 0x9 },
	{ 0xf-0x1, BCR_ILCRE, 0, 0x1 },
	{ 0xf-0xf, BCR_ILCRF, 3, 0xf },
	{ 0xf-0xb, BCR_ILCRF, 1, 0xb },
	{ 0xf-0x7, BCR_ILCRG, 3, 0x7 },
	{ 0xf-0x6, BCR_ILCRG, 2, 0x6 },
	{ 0xf-0x4, BCR_ILCRG, 1, 0x4 },
#else
	{ 14, BCR_ILCRA, 2, 0x0f-14 },
	{ 12, BCR_ILCRA, 1, 0x0f-12 },
	{  8, BCR_ILCRB, 1, 0x0f- 8 },
	{  6, BCR_ILCRC, 3, 0x0f- 6 },
	{  5, BCR_ILCRC, 2, 0x0f- 5 },
	{  4, BCR_ILCRC, 1, 0x0f- 4 },
	{  3, BCR_ILCRC, 0, 0x0f- 3 },
	{  1, BCR_ILCRD, 3, 0x0f- 1 },

	{ 10, BCR_ILCRD, 1, 0x0f-10 }, /* LAN */

	{  0, BCR_ILCRE, 3, 0x0f- 0 }, /* PCIRQ3 */
	{ 11, BCR_ILCRE, 2, 0x0f-11 }, /* PCIRQ2 */
	{  9, BCR_ILCRE, 1, 0x0f- 9 }, /* PCIRQ1 */
	{  7, BCR_ILCRE, 0, 0x0f- 7 }, /* PCIRQ0 */

	/* #2, #13 are allocated for SLOT IRQ #1 and #2 (for now) */
	/* NOTE: #2 and #13 are not used on PC */
	{ 13, BCR_ILCRG, 1, 0x0f-13 }, /* SLOTIRQ2 */
	{  2, BCR_ILCRG, 0, 0x0f- 2 }, /* SLOTIRQ1 */
#endif
};

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
#endif
	make_ipr_irq(se770x_ipr_map, ARRAY_SIZE(se770x_ipr_map));
}
