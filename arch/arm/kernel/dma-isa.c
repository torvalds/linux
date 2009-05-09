/*
 *  linux/arch/arm/kernel/dma-isa.c
 *
 *  Copyright (C) 1999-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ISA DMA primitives
 *  Taken from various sources, including:
 *   linux/include/asm/dma.h: Defines for using and allocating dma channels.
 *     Written by Hennus Bergman, 1992.
 *     High DMA channel support & info by Hannu Savolainen and John Boyd,
 *     Nov. 1992.
 *   arch/arm/kernel/dma-ebsa285.c
 *   Copyright (C) 1998 Phil Blundell
 */
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <asm/dma.h>
#include <asm/mach/dma.h>

#define ISA_DMA_MASK		0
#define ISA_DMA_MODE		1
#define ISA_DMA_CLRFF		2
#define ISA_DMA_PGHI		3
#define ISA_DMA_PGLO		4
#define ISA_DMA_ADDR		5
#define ISA_DMA_COUNT		6

static unsigned int isa_dma_port[8][7] = {
	/* MASK   MODE   CLRFF  PAGE_HI PAGE_LO ADDR COUNT */
	{  0x0a,  0x0b,  0x0c,  0x487,  0x087,  0x00, 0x01 },
	{  0x0a,  0x0b,  0x0c,  0x483,  0x083,  0x02, 0x03 },
	{  0x0a,  0x0b,  0x0c,  0x481,  0x081,  0x04, 0x05 },
	{  0x0a,  0x0b,  0x0c,  0x482,  0x082,  0x06, 0x07 },
	{  0xd4,  0xd6,  0xd8,  0x000,  0x000,  0xc0, 0xc2 },
	{  0xd4,  0xd6,  0xd8,  0x48b,  0x08b,  0xc4, 0xc6 },
	{  0xd4,  0xd6,  0xd8,  0x489,  0x089,  0xc8, 0xca },
	{  0xd4,  0xd6,  0xd8,  0x48a,  0x08a,  0xcc, 0xce }
};

static int isa_get_dma_residue(unsigned int chan, dma_t *dma)
{
	unsigned int io_port = isa_dma_port[chan][ISA_DMA_COUNT];
	int count;

	count = 1 + inb(io_port);
	count |= inb(io_port) << 8;

	return chan < 4 ? count : (count << 1);
}

static void isa_enable_dma(unsigned int chan, dma_t *dma)
{
	if (dma->invalid) {
		unsigned long address, length;
		unsigned int mode;
		enum dma_data_direction direction;

		mode = (chan & 3) | dma->dma_mode;
		switch (dma->dma_mode & DMA_MODE_MASK) {
		case DMA_MODE_READ:
			direction = DMA_FROM_DEVICE;
			break;

		case DMA_MODE_WRITE:
			direction = DMA_TO_DEVICE;
			break;

		case DMA_MODE_CASCADE:
			direction = DMA_BIDIRECTIONAL;
			break;

		default:
			direction = DMA_NONE;
			break;
		}

		if (!dma->sg) {
			/*
			 * Cope with ISA-style drivers which expect cache
			 * coherence.
			 */
			dma->sg = &dma->buf;
			dma->sgcount = 1;
			dma->buf.length = dma->count;
			dma->buf.dma_address = dma_map_single(NULL,
				dma->addr, dma->count,
				direction);
		}

		address = dma->buf.dma_address;
		length  = dma->buf.length - 1;

		outb(address >> 16, isa_dma_port[chan][ISA_DMA_PGLO]);
		outb(address >> 24, isa_dma_port[chan][ISA_DMA_PGHI]);

		if (chan >= 4) {
			address >>= 1;
			length >>= 1;
		}

		outb(0, isa_dma_port[chan][ISA_DMA_CLRFF]);

		outb(address, isa_dma_port[chan][ISA_DMA_ADDR]);
		outb(address >> 8, isa_dma_port[chan][ISA_DMA_ADDR]);

		outb(length, isa_dma_port[chan][ISA_DMA_COUNT]);
		outb(length >> 8, isa_dma_port[chan][ISA_DMA_COUNT]);

		outb(mode, isa_dma_port[chan][ISA_DMA_MODE]);
		dma->invalid = 0;
	}
	outb(chan & 3, isa_dma_port[chan][ISA_DMA_MASK]);
}

static void isa_disable_dma(unsigned int chan, dma_t *dma)
{
	outb(chan | 4, isa_dma_port[chan][ISA_DMA_MASK]);
}

static struct dma_ops isa_dma_ops = {
	.type		= "ISA",
	.enable		= isa_enable_dma,
	.disable	= isa_disable_dma,
	.residue	= isa_get_dma_residue,
};

static struct resource dma_resources[] = { {
	.name	= "dma1",
	.start	= 0x0000,
	.end	= 0x000f
}, {
	.name	= "dma low page",
	.start	= 0x0080,
	.end 	= 0x008f
}, {
	.name	= "dma2",
	.start	= 0x00c0,
	.end	= 0x00df
}, {
	.name	= "dma high page",
	.start	= 0x0480,
	.end	= 0x048f
} };

static dma_t isa_dma[8];

/*
 * ISA DMA always starts at channel 0
 */
void __init isa_init_dma(void)
{
	/*
	 * Try to autodetect presence of an ISA DMA controller.
	 * We do some minimal initialisation, and check that
	 * channel 0's DMA address registers are writeable.
	 */
	outb(0xff, 0x0d);
	outb(0xff, 0xda);

	/*
	 * Write high and low address, and then read them back
	 * in the same order.
	 */
	outb(0x55, 0x00);
	outb(0xaa, 0x00);

	if (inb(0) == 0x55 && inb(0) == 0xaa) {
		unsigned int chan, i;

		for (chan = 0; chan < 8; chan++) {
			isa_dma[chan].d_ops = &isa_dma_ops;
			isa_disable_dma(chan, NULL);
		}

		outb(0x40, 0x0b);
		outb(0x41, 0x0b);
		outb(0x42, 0x0b);
		outb(0x43, 0x0b);

		outb(0xc0, 0xd6);
		outb(0x41, 0xd6);
		outb(0x42, 0xd6);
		outb(0x43, 0xd6);

		outb(0, 0xd4);

		outb(0x10, 0x08);
		outb(0x10, 0xd0);

		/*
		 * Is this correct?  According to my documentation, it
		 * doesn't appear to be.  It should be:
		 *  outb(0x3f, 0x40b); outb(0x3f, 0x4d6);
		 */
		outb(0x30, 0x40b);
		outb(0x31, 0x40b);
		outb(0x32, 0x40b);
		outb(0x33, 0x40b);
		outb(0x31, 0x4d6);
		outb(0x32, 0x4d6);
		outb(0x33, 0x4d6);

		request_dma(DMA_ISA_CASCADE, "cascade");

		for (i = 0; i < ARRAY_SIZE(dma_resources); i++)
			request_resource(&ioport_resource, dma_resources + i);

		for (chan = 0; chan < 8; chan++) {
			int ret = isa_dma_add(chan, &isa_dma[chan]);
			if (ret)
				printk(KERN_ERR "ISADMA%u: unable to register: %d\n",
					chan, ret);
		}
	}
}
