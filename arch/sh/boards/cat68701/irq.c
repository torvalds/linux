/*
 * linux/arch/sh/boards/cat68701/irq.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *               2001  Yutaro Ebihara
 *
 * Setup routines for A-ONE Corp CAT-68701 SH7708 Board
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <asm/irq.h>

int cat68701_irq_demux(int irq)
{
        if(irq==13) return 14;
        if(irq==7)  return 10;
        return irq;
}

void init_cat68701_IRQ()
{
        make_imask_irq(10);
        make_imask_irq(14);
}
