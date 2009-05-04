/* $Id: shmem.c,v 1.2.10.1 2001/09/23 22:24:59 kai Exp $
 *
 * Copyright (C) 1996  SpellCaster Telecommunications Inc.
 *
 * Card functions implementing ISDN4Linux functionality
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For more information, please contact gpl-info@spellcast.com or write:
 *
 *     SpellCaster Telecommunications Inc.
 *     5621 Finch Avenue East, Unit #3
 *     Scarborough, Ontario  Canada
 *     M1B 2T9
 *     +1 (416) 297-8565
 *     +1 (416) 297-6433 Facsimile
 */

#include "includes.h"		/* This must be first */
#include "hardware.h"
#include "card.h"

/*
 *
 */
void memcpy_toshmem(int card, void *dest, const void *src, size_t n)
{
	unsigned long flags;
	unsigned char ch;
	unsigned long dest_rem = ((unsigned long) dest) % 0x4000;

	if (!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return;
	}

	if (n > SRAM_PAGESIZE)
		return;

	/*
	 * determine the page to load from the address
	 */
	ch = (unsigned long) dest / SRAM_PAGESIZE;
	pr_debug("%s: loaded page %d\n", sc_adapter[card]->devicename,ch);
	/*
	 * Block interrupts and load the page
	 */
	spin_lock_irqsave(&sc_adapter[card]->lock, flags);

	outb(((sc_adapter[card]->shmem_magic + ch * SRAM_PAGESIZE) >> 14) | 0x80,
		sc_adapter[card]->ioport[sc_adapter[card]->shmem_pgport]);
	memcpy_toio((void __iomem *)(sc_adapter[card]->rambase + dest_rem), src, n);
	spin_unlock_irqrestore(&sc_adapter[card]->lock, flags);
	pr_debug("%s: set page to %#x\n",sc_adapter[card]->devicename,
		((sc_adapter[card]->shmem_magic + ch * SRAM_PAGESIZE)>>14)|0x80);
	pr_debug("%s: copying %zu bytes from %#lx to %#lx\n",
		sc_adapter[card]->devicename, n,
		(unsigned long) src,
		sc_adapter[card]->rambase + ((unsigned long) dest %0x4000));
}

/*
 * Reverse of above
 */
void memcpy_fromshmem(int card, void *dest, const void *src, size_t n)
{
	unsigned long flags;
	unsigned char ch;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return;
	}

	if(n > SRAM_PAGESIZE) {
		return;
	}

	/*
	 * determine the page to load from the address
	 */
	ch = (unsigned long) src / SRAM_PAGESIZE;
	pr_debug("%s: loaded page %d\n", sc_adapter[card]->devicename,ch);
	
	
	/*
	 * Block interrupts and load the page
	 */
	spin_lock_irqsave(&sc_adapter[card]->lock, flags);

	outb(((sc_adapter[card]->shmem_magic + ch * SRAM_PAGESIZE) >> 14) | 0x80,
		sc_adapter[card]->ioport[sc_adapter[card]->shmem_pgport]);
	memcpy_fromio(dest,(void *)(sc_adapter[card]->rambase +
		((unsigned long) src % 0x4000)), n);
	spin_unlock_irqrestore(&sc_adapter[card]->lock, flags);
	pr_debug("%s: set page to %#x\n",sc_adapter[card]->devicename,
		((sc_adapter[card]->shmem_magic + ch * SRAM_PAGESIZE)>>14)|0x80);
/*	pr_debug("%s: copying %d bytes from %#x to %#x\n",
		sc_adapter[card]->devicename, n,
		sc_adapter[card]->rambase + ((unsigned long) src %0x4000), (unsigned long) dest); */
}

#if 0
void memset_shmem(int card, void *dest, int c, size_t n)
{
	unsigned long flags;
	unsigned char ch;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return;
	}

	if(n > SRAM_PAGESIZE) {
		return;
	}

	/*
	 * determine the page to load from the address
	 */
	ch = (unsigned long) dest / SRAM_PAGESIZE;
	pr_debug("%s: loaded page %d\n",sc_adapter[card]->devicename,ch);

	/*
	 * Block interrupts and load the page
	 */
	spin_lock_irqsave(&sc_adapter[card]->lock, flags);

	outb(((sc_adapter[card]->shmem_magic + ch * SRAM_PAGESIZE) >> 14) | 0x80,
		sc_adapter[card]->ioport[sc_adapter[card]->shmem_pgport]);
	memset_io(sc_adapter[card]->rambase +
		((unsigned long) dest % 0x4000), c, n);
	pr_debug("%s: set page to %#x\n",sc_adapter[card]->devicename,
		((sc_adapter[card]->shmem_magic + ch * SRAM_PAGESIZE)>>14)|0x80);
	spin_unlock_irqrestore(&sc_adapter[card]->lock, flags);
}
#endif  /*  0  */
