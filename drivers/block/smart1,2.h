/*
 *    Disk Array driver for Compaq SMART2 Controllers
 *    Copyright 1998 Compaq Computer Corporation
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
 *
 *    If you want to make changes, improve or add functionality to this
 *    driver, you'll probably need the Compaq Array Controller Interface
 *    Specificiation (Document number ECG086/1198)
 */

/*
 * This file contains the controller communication implementation for
 * Compaq SMART-1 and SMART-2 controllers.  To the best of my knowledge,
 * this should support:
 *
 *  PCI:
 *  SMART-2/P, SMART-2DH, SMART-2SL, SMART-221, SMART-3100ES, SMART-3200
 *  Integerated SMART Array Controller, SMART-4200, SMART-4250ES
 *
 *  EISA:
 *  SMART-2/E, SMART, IAES, IDA-2, IDA
 */

/*
 * Memory mapped FIFO interface (SMART 42xx cards)
 */
static void smart4_submit_command(ctlr_info_t *h, cmdlist_t *c)
{
        writel(c->busaddr, h->vaddr + S42XX_REQUEST_PORT_OFFSET);
}

/*  
 *  This card is the opposite of the other cards.  
 *   0 turns interrupts on... 
 *   0x08 turns them off... 
 */
static void smart4_intr_mask(ctlr_info_t *h, unsigned long val)
{
	if (val) 
	{ /* Turn interrupts on */
		writel(0, h->vaddr + S42XX_REPLY_INTR_MASK_OFFSET);
	} else /* Turn them off */
	{
        	writel( S42XX_INTR_OFF, 
			h->vaddr + S42XX_REPLY_INTR_MASK_OFFSET);
	}
}

/*
 *  For older cards FIFO Full = 0. 
 *  On this card 0 means there is room, anything else FIFO Full. 
 * 
 */ 
static unsigned long smart4_fifo_full(ctlr_info_t *h)
{
	
        return (!readl(h->vaddr + S42XX_REQUEST_PORT_OFFSET));
}

/* This type of controller returns -1 if the fifo is empty, 
 *    Not 0 like the others.
 *    And we need to let it know we read a value out 
 */ 
static unsigned long smart4_completed(ctlr_info_t *h)
{
	long register_value 
		= readl(h->vaddr + S42XX_REPLY_PORT_OFFSET);

	/* Fifo is empty */
	if( register_value == 0xffffffff)
		return 0; 	

	/* Need to let it know we got the reply */
	/* We do this by writing a 0 to the port we just read from */
	writel(0, h->vaddr + S42XX_REPLY_PORT_OFFSET);

	return ((unsigned long) register_value); 
}

 /*
 *  This hardware returns interrupt pending at a different place and 
 *  it does not tell us if the fifo is empty, we will have check  
 *  that by getting a 0 back from the command_completed call. 
 */
static unsigned long smart4_intr_pending(ctlr_info_t *h)
{
	unsigned long register_value  = 
		readl(h->vaddr + S42XX_INTR_STATUS);

	if( register_value &  S42XX_INTR_PENDING) 
		return  FIFO_NOT_EMPTY;	
	return 0 ;
}

static struct access_method smart4_access = {
	smart4_submit_command,
	smart4_intr_mask,
	smart4_fifo_full,
	smart4_intr_pending,
	smart4_completed,
};

/*
 * Memory mapped FIFO interface (PCI SMART2 and SMART 3xxx cards)
 */
static void smart2_submit_command(ctlr_info_t *h, cmdlist_t *c)
{
	writel(c->busaddr, h->vaddr + COMMAND_FIFO);
}

static void smart2_intr_mask(ctlr_info_t *h, unsigned long val)
{
	writel(val, h->vaddr + INTR_MASK);
}

static unsigned long smart2_fifo_full(ctlr_info_t *h)
{
	return readl(h->vaddr + COMMAND_FIFO);
}

static unsigned long smart2_completed(ctlr_info_t *h)
{
	return readl(h->vaddr + COMMAND_COMPLETE_FIFO);
}

static unsigned long smart2_intr_pending(ctlr_info_t *h)
{
	return readl(h->vaddr + INTR_PENDING);
}

static struct access_method smart2_access = {
	smart2_submit_command,
	smart2_intr_mask,
	smart2_fifo_full,
	smart2_intr_pending,
	smart2_completed,
};

/*
 *  IO access for SMART-2/E cards
 */
static void smart2e_submit_command(ctlr_info_t *h, cmdlist_t *c)
{
	outl(c->busaddr, h->io_mem_addr + COMMAND_FIFO);
}

static void smart2e_intr_mask(ctlr_info_t *h, unsigned long val)
{
	outl(val, h->io_mem_addr + INTR_MASK);
}

static unsigned long smart2e_fifo_full(ctlr_info_t *h)
{
	return inl(h->io_mem_addr + COMMAND_FIFO);
}

static unsigned long smart2e_completed(ctlr_info_t *h)
{
	return inl(h->io_mem_addr + COMMAND_COMPLETE_FIFO);
}

static unsigned long smart2e_intr_pending(ctlr_info_t *h)
{
	return inl(h->io_mem_addr + INTR_PENDING);
}

static struct access_method smart2e_access = {
	smart2e_submit_command,
	smart2e_intr_mask,
	smart2e_fifo_full,
	smart2e_intr_pending,
	smart2e_completed,
};

/*
 *  IO access for older SMART-1 type cards
 */
#define SMART1_SYSTEM_MASK		0xC8E
#define SMART1_SYSTEM_DOORBELL		0xC8F
#define SMART1_LOCAL_MASK		0xC8C
#define SMART1_LOCAL_DOORBELL		0xC8D
#define SMART1_INTR_MASK		0xC89
#define SMART1_LISTADDR			0xC90
#define SMART1_LISTLEN			0xC94
#define SMART1_TAG			0xC97
#define SMART1_COMPLETE_ADDR		0xC98
#define SMART1_LISTSTATUS		0xC9E

#define CHANNEL_BUSY			0x01
#define CHANNEL_CLEAR			0x02

static void smart1_submit_command(ctlr_info_t *h, cmdlist_t *c)
{
	/*
	 * This __u16 is actually a bunch of control flags on SMART
	 * and below.  We want them all to be zero.
	 */
	c->hdr.size = 0;

	outb(CHANNEL_CLEAR, h->io_mem_addr + SMART1_SYSTEM_DOORBELL);

	outl(c->busaddr, h->io_mem_addr + SMART1_LISTADDR);
	outw(c->size, h->io_mem_addr + SMART1_LISTLEN);

	outb(CHANNEL_BUSY, h->io_mem_addr + SMART1_LOCAL_DOORBELL);
}

static void smart1_intr_mask(ctlr_info_t *h, unsigned long val)
{
	if (val == 1) {
		outb(0xFD, h->io_mem_addr + SMART1_SYSTEM_DOORBELL);
		outb(CHANNEL_BUSY, h->io_mem_addr + SMART1_LOCAL_DOORBELL);
		outb(0x01, h->io_mem_addr + SMART1_INTR_MASK);
		outb(0x01, h->io_mem_addr + SMART1_SYSTEM_MASK);
	} else {
		outb(0, h->io_mem_addr + 0xC8E);
	}
}

static unsigned long smart1_fifo_full(ctlr_info_t *h)
{
	unsigned char chan;
	chan = inb(h->io_mem_addr + SMART1_SYSTEM_DOORBELL) & CHANNEL_CLEAR;
	return chan;
}

static unsigned long smart1_completed(ctlr_info_t *h)
{
	unsigned char status;
	unsigned long cmd;

	if (inb(h->io_mem_addr + SMART1_SYSTEM_DOORBELL) & CHANNEL_BUSY) {
		outb(CHANNEL_BUSY, h->io_mem_addr + SMART1_SYSTEM_DOORBELL);

		cmd = inl(h->io_mem_addr + SMART1_COMPLETE_ADDR);
		status = inb(h->io_mem_addr + SMART1_LISTSTATUS);

		outb(CHANNEL_CLEAR, h->io_mem_addr + SMART1_LOCAL_DOORBELL);

		/*
		 * this is x86 (actually compaq x86) only, so it's ok
		 */
		if (cmd) ((cmdlist_t*)bus_to_virt(cmd))->req.hdr.rcode = status;
	} else {
		cmd = 0;
	}
	return cmd;
}

static unsigned long smart1_intr_pending(ctlr_info_t *h)
{
	unsigned char chan;
	chan = inb(h->io_mem_addr + SMART1_SYSTEM_DOORBELL) & CHANNEL_BUSY;
	return chan;
}

static struct access_method smart1_access = {
	smart1_submit_command,
	smart1_intr_mask,
	smart1_fifo_full,
	smart1_intr_pending,
	smart1_completed,
};
