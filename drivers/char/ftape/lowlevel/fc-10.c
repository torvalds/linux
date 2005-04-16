/*
 *

   Copyright (C) 1993,1994 Jon Tombs.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The entire guts of this program was written by dosemu, modified to
   record reads and writes to the ports in the 0x180-0x188 address space,
   while running the CMS program TAPE.EXE V2.0.5 supplied with the drive.

   Modified to use an array of addresses and generally cleaned up (made
   much shorter) 4 June 94, dosemu isn't that good at writing short code it
   would seem :-). Made independent of 0x180, but I doubt it will work
   at any other address.

   Modified for distribution with ftape source. 21 June 94, SJL.

   Modifications on 20 October 95, by Daniel Cohen (catman@wpi.edu):
   Modified to support different DMA, IRQ, and IO Ports.  Borland's
   Turbo Debugger in virtual 8086 mode (TD386.EXE with hardware breakpoints
   provided by the TDH386.SYS Device Driver) was used on the CMS program
   TAPE V4.0.5.  I set breakpoints on I/O to ports 0x180-0x187.  Note that
   CMS's program will not successfully configure the tape drive if you set
   breakpoints on IO Reads, but you can set them on IO Writes without problems.
   Known problems:
   - You can not use DMA Channels 5 or 7.

   Modification on 29 January 96, by Daniel Cohen (catman@wpi.edu):
   Modified to only accept IRQs 3 - 7, or 9.  Since we can only send a 3 bit
   number representing the IRQ to the card, special handling is required when
   IRQ 9 is selected.  IRQ 2 and 9 are the same, and we should request IRQ 9
   from the kernel while telling the card to use IRQ 2.  Thanks to Greg
   Crider (gcrider@iclnet.org) for finding and locating this bug, as well as
   testing the patch.

   Modification on 11 December 96, by Claus Heine (claus@momo.math.rwth-aachen.de):
   Modified a little to use variahle ft_fdc_base, ft_fdc_irq, ft_fdc_dma 
   instead of preprocessor symbols. Thus we can compile this into the module
   or kernel and let the user specify the options as command line arguments.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/fc-10.c,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:04 $
 *
 *      This file contains code for the CMS FC-10/FC-20 card.
 */

#include <asm/io.h>
#include <linux/ftape.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/fdc-io.h"
#include "../lowlevel/fc-10.h"

static __u16 inbs_magic[] = {
	0x3, 0x3, 0x0, 0x4, 0x7, 0x2, 0x5, 0x3, 0x1, 0x4,
	0x3, 0x5, 0x2, 0x0, 0x3, 0x7, 0x4, 0x2,
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7
};

static __u16 fc10_ports[] = {
	0x180, 0x210, 0x2A0, 0x300, 0x330, 0x340, 0x370
};

int fc10_enable(void)
{
	int i;
	__u8 cardConfig = 0x00;
	__u8 x;
	TRACE_FUN(ft_t_flow);

/*  This code will only work if the FC-10 (or FC-20) is set to
 *  use DMA channels 1, 2, or 3.  DMA channels 5 and 7 seem to be 
 *  initialized by the same command as channels 1 and 3, respectively.
 */
	if (ft_fdc_dma > 3) {
		TRACE_ABORT(0, ft_t_err,
"Error: The FC-10/20 must be set to use DMA channels 1, 2, or 3!");
	}
/*  Only allow the FC-10/20 to use IRQ 3-7, or 9.  Note that CMS's program
 *  only accepts IRQ's 2-7, but in linux, IRQ 2 is the same as IRQ 9.
 */
	if (ft_fdc_irq < 3 || ft_fdc_irq == 8 || ft_fdc_irq > 9) {
		TRACE_ABORT(0, ft_t_err, 
"Error: The FC-10/20 must be set to use IRQ levels 3 - 7, or 9!\n"
KERN_INFO "Note: IRQ 9 is the same as IRQ 2");
	}
	/*  Clear state machine ???
	 */
	for (i = 0; i < NR_ITEMS(inbs_magic); i++) {
		inb(ft_fdc_base + inbs_magic[i]);
	}
	outb(0x0, ft_fdc_base);

	x = inb(ft_fdc_base);
	if (x == 0x13 || x == 0x93) {
		for (i = 1; i < 8; i++) {
			if (inb(ft_fdc_base + i) != x) {
				TRACE_EXIT 0;
			}
		}
	} else {
		TRACE_EXIT 0;
	}

	outb(0x8, ft_fdc_base);

	for (i = 0; i < 8; i++) {
		if (inb(ft_fdc_base + i) != 0x0) {
			TRACE_EXIT 0;
		}
	}
	outb(0x10, ft_fdc_base);

	for (i = 0; i < 8; i++) {
		if (inb(ft_fdc_base + i) != 0xff) {
			TRACE_EXIT 0;
		}
	}

	/*  Okay, we found a FC-10 card ! ???
	 */
	outb(0x0, fdc.ccr);

	/*  Clear state machine again ???
	 */
	for (i = 0; i < NR_ITEMS(inbs_magic); i++) {
		inb(ft_fdc_base + inbs_magic[i]);
	}
	/* Send io port */
	for (i = 0; i < NR_ITEMS(fc10_ports); i++)
		if (ft_fdc_base == fc10_ports[i])
			cardConfig = i + 1;
	if (cardConfig == 0) {
		TRACE_EXIT 0;	/* Invalid I/O Port */
	}
	/* and IRQ - If using IRQ 9, tell the FC card it is actually IRQ 2 */
	if (ft_fdc_irq != 9)
		cardConfig |= ft_fdc_irq << 3;
	else
		cardConfig |= 2 << 3;

	/* and finally DMA Channel */
	cardConfig |= ft_fdc_dma << 6;
	outb(cardConfig, ft_fdc_base);	/* DMA [2 bits]/IRQ [3 bits]/BASE [3 bits] */

	/*  Enable FC-10 ???
	 */
	outb(0, fdc.ccr);
	outb(0, fdc.dor2);
	outb(FDC_DMA_MODE /* 8 */, fdc.dor);
	outb(FDC_DMA_MODE /* 8 */, fdc.dor);
	outb(1, fdc.dor2);

	/*************************************
	 *
	 * cH: why the hell should this be necessary? This is done 
	 *     by fdc_reset()!!!
	 *
	 *************************************/
	/*  Initialize fdc, select drive B:
	 */
	outb(FDC_DMA_MODE, fdc.dor);	/* assert reset, dma & irq enabled */
	/*       0x08    */
	outb(FDC_DMA_MODE|FDC_RESET_NOT, fdc.dor);	/* release reset */
	/*       0x08    |   0x04   = 0x0c */
	outb(FDC_DMA_MODE|FDC_RESET_NOT|FDC_MOTOR_1|FTAPE_SEL_B, fdc.dor);
	/*       0x08    |   0x04      |  0x20     |  0x01  = 0x2d */    
	/* select drive 1 */ /* why not drive 0 ???? */
	TRACE_EXIT (x == 0x93) ? 2 : 1;
}
