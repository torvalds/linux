/*
 * $Id: io.c,v 1.4 2003/08/03 03:05:10 lethal Exp $
 * by Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc
 *
 * Derived from io_hd64461.c, which bore the message:
 * Copyright (C) 2000 YAEGASHI Takeshi
 *
 * Typical I/O routines for HD64465 system.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/hd64465/hd64465.h>


#define HD64465_DEBUG 0

#if HD64465_DEBUG
#define DPRINTK(args...)	printk(args)
#define DIPRINTK(n, args...)	if (hd64465_io_debug>(n)) printk(args)
#else
#define DPRINTK(args...)
#define DIPRINTK(n, args...)
#endif



/* This is a hack suitable only for debugging IO port problems */
int hd64465_io_debug;
EXPORT_SYMBOL(hd64465_io_debug);

/* Low iomap maps port 0-1K to addresses in 8byte chunks */
#define HD64465_IOMAP_LO_THRESH 0x400
#define HD64465_IOMAP_LO_SHIFT	3
#define HD64465_IOMAP_LO_MASK	((1<<HD64465_IOMAP_LO_SHIFT)-1)
#define HD64465_IOMAP_LO_NMAP	(HD64465_IOMAP_LO_THRESH>>HD64465_IOMAP_LO_SHIFT)
static unsigned long	hd64465_iomap_lo[HD64465_IOMAP_LO_NMAP];
static unsigned char	hd64465_iomap_lo_shift[HD64465_IOMAP_LO_NMAP];

/* High iomap maps port 1K-64K to addresses in 1K chunks */
#define HD64465_IOMAP_HI_THRESH 0x10000
#define HD64465_IOMAP_HI_SHIFT	10
#define HD64465_IOMAP_HI_MASK	((1<<HD64465_IOMAP_HI_SHIFT)-1)
#define HD64465_IOMAP_HI_NMAP	(HD64465_IOMAP_HI_THRESH>>HD64465_IOMAP_HI_SHIFT)
static unsigned long	hd64465_iomap_hi[HD64465_IOMAP_HI_NMAP];
static unsigned char	hd64465_iomap_hi_shift[HD64465_IOMAP_HI_NMAP];

#define PORT2ADDR(x) (sh_mv.mv_isa_port2addr(x))

void hd64465_port_map(unsigned short baseport, unsigned int nports,
		      unsigned long addr, unsigned char shift)
{
    	unsigned int port, endport = baseport + nports;

    	DPRINTK("hd64465_port_map(base=0x%04hx, n=0x%04hx, addr=0x%08lx,endport=0x%04x)\n",
	    baseport, nports, addr,endport);
	    
	for (port = baseport ;
	     port < endport && port < HD64465_IOMAP_LO_THRESH ;
	     port += (1<<HD64465_IOMAP_LO_SHIFT)) {
	    DPRINTK("    maplo[0x%x] = 0x%08lx\n", port, addr);
    	    hd64465_iomap_lo[port>>HD64465_IOMAP_LO_SHIFT] = addr;
    	    hd64465_iomap_lo_shift[port>>HD64465_IOMAP_LO_SHIFT] = shift;
	    addr += (1<<(HD64465_IOMAP_LO_SHIFT));
	}

	for (port = max_t(unsigned int, baseport, HD64465_IOMAP_LO_THRESH);
	     port < endport && port < HD64465_IOMAP_HI_THRESH ;
	     port += (1<<HD64465_IOMAP_HI_SHIFT)) {
	    DPRINTK("    maphi[0x%x] = 0x%08lx\n", port, addr);
    	    hd64465_iomap_hi[port>>HD64465_IOMAP_HI_SHIFT] = addr;
    	    hd64465_iomap_hi_shift[port>>HD64465_IOMAP_HI_SHIFT] = shift;
	    addr += (1<<(HD64465_IOMAP_HI_SHIFT));
	}
}
EXPORT_SYMBOL(hd64465_port_map);

void hd64465_port_unmap(unsigned short baseport, unsigned int nports)
{
    	unsigned int port, endport = baseport + nports;
	
    	DPRINTK("hd64465_port_unmap(base=0x%04hx, n=0x%04hx)\n",
	    baseport, nports);

	for (port = baseport ;
	     port < endport && port < HD64465_IOMAP_LO_THRESH ;
	     port += (1<<HD64465_IOMAP_LO_SHIFT)) {
    	    hd64465_iomap_lo[port>>HD64465_IOMAP_LO_SHIFT] = 0;
	}

	for (port = max_t(unsigned int, baseport, HD64465_IOMAP_LO_THRESH);
	     port < endport && port < HD64465_IOMAP_HI_THRESH ;
	     port += (1<<HD64465_IOMAP_HI_SHIFT)) {
    	    hd64465_iomap_hi[port>>HD64465_IOMAP_HI_SHIFT] = 0;
	}
}
EXPORT_SYMBOL(hd64465_port_unmap);

unsigned long hd64465_isa_port2addr(unsigned long port)
{
    	unsigned long addr = 0;
	unsigned char shift;

	/* handle remapping of low IO ports */
	if (port < HD64465_IOMAP_LO_THRESH) {
	    addr = hd64465_iomap_lo[port >> HD64465_IOMAP_LO_SHIFT];
	    shift = hd64465_iomap_lo_shift[port >> HD64465_IOMAP_LO_SHIFT];
	    if (addr != 0)
	    	addr += (port & HD64465_IOMAP_LO_MASK) << shift;
	    else
		printk(KERN_NOTICE "io_hd64465: access to un-mapped port %lx\n", port);
	} else if (port < HD64465_IOMAP_HI_THRESH) {
	    addr = hd64465_iomap_hi[port >> HD64465_IOMAP_HI_SHIFT];
	    shift = hd64465_iomap_hi_shift[port >> HD64465_IOMAP_HI_SHIFT];
	    if (addr != 0)
		addr += (port & HD64465_IOMAP_HI_MASK) << shift;
	    else
		printk(KERN_NOTICE "io_hd64465: access to un-mapped port %lx\n", port);
	}
	    	
	/* HD64465 internal devices (0xb0000000) */
	else if (port < 0x20000)
	    addr = CONFIG_HD64465_IOBASE + port - 0x10000;

	/* Whole physical address space (0xa0000000) */
	else
	    addr = P2SEGADDR(port);

    	DIPRINTK(2, "PORT2ADDR(0x%08lx) = 0x%08lx\n", port, addr);

	return addr;
}

static inline void delay(void)
{
	ctrl_inw(0xa0000000);
}

unsigned char hd64465_inb(unsigned long port)
{
	unsigned long addr = PORT2ADDR(port);
	unsigned long b = (addr == 0 ? 0 : *(volatile unsigned char*)addr);

	DIPRINTK(0, "inb(%08lx) = %02x\n", addr, (unsigned)b);
	return b;
}

unsigned char hd64465_inb_p(unsigned long port)
{
    	unsigned long v;
	unsigned long addr = PORT2ADDR(port);

	v = (addr == 0 ? 0 : *(volatile unsigned char*)addr);
	delay();
	DIPRINTK(0, "inb_p(%08lx) = %02x\n", addr, (unsigned)v);
	return v;
}

unsigned short hd64465_inw(unsigned long port)
{
    	unsigned long addr = PORT2ADDR(port);
	unsigned long b = (addr == 0 ? 0 : *(volatile unsigned short*)addr);
	DIPRINTK(0, "inw(%08lx) = %04lx\n", addr, b);
	return b;
}

unsigned int hd64465_inl(unsigned long port)
{
    	unsigned long addr = PORT2ADDR(port);
	unsigned int b = (addr == 0 ? 0 : *(volatile unsigned long*)addr);
	DIPRINTK(0, "inl(%08lx) = %08x\n", addr, b);
	return b;
}

void hd64465_outb(unsigned char b, unsigned long port)
{
	unsigned long addr = PORT2ADDR(port);

	DIPRINTK(0, "outb(%02x, %08lx)\n", (unsigned)b, addr);
	if (addr != 0)
	    *(volatile unsigned char*)addr = b;
}

void hd64465_outb_p(unsigned char b, unsigned long port)
{
	unsigned long addr = PORT2ADDR(port);

	DIPRINTK(0, "outb_p(%02x, %08lx)\n", (unsigned)b, addr);
    	if (addr != 0)
	    *(volatile unsigned char*)addr = b;
	delay();
}

void hd64465_outw(unsigned short b, unsigned long port)
{
	unsigned long addr = PORT2ADDR(port);
	DIPRINTK(0, "outw(%04x, %08lx)\n", (unsigned)b, addr);
	if (addr != 0)
	    *(volatile unsigned short*)addr = b;
}

void hd64465_outl(unsigned int b, unsigned long port)
{
	unsigned long addr = PORT2ADDR(port);
	DIPRINTK(0, "outl(%08x, %08lx)\n", b, addr);
	if (addr != 0)
            *(volatile unsigned long*)addr = b;
}

