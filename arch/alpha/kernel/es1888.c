/*
 *	linux/arch/alpha/kernel/es1888.c
 *
 * Init the built-in ES1888 sound chip (SB16 compatible)
 */

#include <linux/init.h>
#include <asm/io.h>
#include "proto.h"

void __init
es1888_init(void)
{
	/* Sequence of IO reads to init the audio controller */
	inb(0x0229);
	inb(0x0229);
	inb(0x0229);
	inb(0x022b);
	inb(0x0229);
	inb(0x022b);
	inb(0x0229);
	inb(0x0229);
	inb(0x022b);
	inb(0x0229);
	inb(0x0220); /* This sets the base address to 0x220 */

	/* Sequence to set DMA channels */
	outb(0x01, 0x0226);		/* reset */
	inb(0x0226);			/* pause */
	outb(0x00, 0x0226);		/* release reset */
	while (!(inb(0x022e) & 0x80))	/* wait for bit 7 to assert*/
		continue;
	inb(0x022a);			/* pause */
	outb(0xc6, 0x022c);		/* enable extended mode */
	inb(0x022a);			/* pause, also forces the write */
	while (inb(0x022c) & 0x80)	/* wait for bit 7 to deassert */
		continue;
	outb(0xb1, 0x022c);		/* setup for write to Interrupt CR */
	while (inb(0x022c) & 0x80)	/* wait for bit 7 to deassert */
		continue;
	outb(0x14, 0x022c);		/* set IRQ 5 */
	while (inb(0x022c) & 0x80)	/* wait for bit 7 to deassert */
		continue;
	outb(0xb2, 0x022c);		/* setup for write to DMA CR */
	while (inb(0x022c) & 0x80)	/* wait for bit 7 to deassert */
		continue;
	outb(0x18, 0x022c);		/* set DMA channel 1 */
	inb(0x022c);			/* force the write */
}
