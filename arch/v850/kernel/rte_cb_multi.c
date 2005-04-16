/*
 * include/asm-v850/rte_multi.c -- Support for Multi debugger monitor ROM
 * 	on Midas lab RTE-CB series of evaluation boards
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/init.h>

#include <asm/machdep.h>

#define IRQ_ADDR(irq) (0x80 + (irq) * 0x10)

/* A table of which interrupt vectors to install, since blindly
   installing all of them makes the debugger stop working.  This is a
   list of offsets in the interrupt vector area; each entry means to
   copy that particular 16-byte vector.  An entry less than zero ends
   the table.  */
static long multi_intv_install_table[] = {
	/* Trap vectors */
	0x40, 0x50,		

#ifdef CONFIG_RTE_CB_MULTI_DBTRAP
	/* Illegal insn / dbtrap.  These are used by multi, so only handle
	   them if configured to do so.  */
	0x60,
#endif

	/* GINT1 - GINT3 (note, not GINT0!) */
	IRQ_ADDR (IRQ_GINT(1)),
	IRQ_ADDR (IRQ_GINT(2)),
	IRQ_ADDR (IRQ_GINT(3)),

	/* Timer D interrupts (up to 4 timers) */
	IRQ_ADDR (IRQ_INTCMD(0)),
#if IRQ_INTCMD_NUM > 1
	IRQ_ADDR (IRQ_INTCMD(1)),
#if IRQ_INTCMD_NUM > 2
	IRQ_ADDR (IRQ_INTCMD(2)),
#if IRQ_INTCMD_NUM > 3
	IRQ_ADDR (IRQ_INTCMD(3)),
#endif
#endif
#endif
	
	/* UART interrupts (up to 3 channels) */
	IRQ_ADDR (IRQ_INTSER (0)), /* err */
	IRQ_ADDR (IRQ_INTSR  (0)), /* rx */
	IRQ_ADDR (IRQ_INTST  (0)), /* tx */
#if IRQ_INTSR_NUM > 1
	IRQ_ADDR (IRQ_INTSER (1)), /* err */
	IRQ_ADDR (IRQ_INTSR  (1)), /* rx */
	IRQ_ADDR (IRQ_INTST  (1)), /* tx */
#if IRQ_INTSR_NUM > 2
	IRQ_ADDR (IRQ_INTSER (2)), /* err */
	IRQ_ADDR (IRQ_INTSR  (2)), /* rx */
	IRQ_ADDR (IRQ_INTST  (2)), /* tx */
#endif
#endif

	-1
};

/* Early initialization for kernel using Multi debugger ROM monitor.  */
void __init multi_init (void)
{
	/* We're using the Multi debugger monitor, so we have to install
	   the interrupt vectors.  The monitor doesn't allow them to be
	   initially downloaded into their final destination because
	   it's in the monitor's scratch-RAM area.  Unfortunately, Multi
	   also doesn't deal correctly with ELF sections where the LMA
	   and VMA differ -- it just ignores the LMA -- so we can't use
	   that feature to work around the problem.  What we do instead
	   is just put the interrupt vectors into a normal section, and
	   do the necessary copying and relocation here.  Since the
	   interrupt vector basically only contains `jr' instructions
	   and no-ops, it's not that hard.  */
	extern unsigned long _intv_load_start, _intv_start;
	register unsigned long *src = &_intv_load_start;
	register unsigned long *dst = (unsigned long *)INTV_BASE;
	register unsigned long jr_fixup = (char *)&_intv_start - (char *)dst;
	register long *ii;

	/* Copy interrupt vectors as instructed by multi_intv_install_table. */
	for (ii = multi_intv_install_table; *ii >= 0; ii++) {
		/* Copy 16-byte interrupt vector at offset *ii.  */
		int boffs;
		for (boffs = 0; boffs < 0x10; boffs += sizeof *src) {
			/* Copy a single word, fixing up the jump offs
			   if it's a `jr' instruction.  */
			int woffs = (*ii + boffs) / sizeof *src;
			unsigned long word = src[woffs];

			if ((word & 0xFC0) == 0x780) {
				/* A `jr' insn, fix up its offset (and yes, the
				   weird half-word swapping is intentional). */
				unsigned short hi = word & 0xFFFF;
				unsigned short lo = word >> 16;
				unsigned long udisp22
					= lo + ((hi & 0x3F) << 16);
				long disp22 = (long)(udisp22 << 10) >> 10;

				disp22 += jr_fixup;

				hi = ((disp22 >> 16) & 0x3F) | 0x780;
				lo = disp22 & 0xFFFF;

				word = hi + (lo << 16);
			}

			dst[woffs] = word;
		}
	}
}
