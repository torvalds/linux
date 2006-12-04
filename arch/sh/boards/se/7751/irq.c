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

#include <linux/init.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/se7751.h>

static struct ipr_data se7751_ipr_map[] = {
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
#elif defined(CONFIG_SH_7751_SOLUTION_ENGINE)
	{ 13, BCR_ILCRD, 3, 2 },
	/* Add additional entries here as drivers are added and tested. */
#endif
};

/*
 * Initialize IRQ setting
 */
void __init init_7751se_IRQ(void)
{
	make_ipr_irq(se7751_ipr_map, ARRAY_SIZE(se7751_ipr_map));
}
