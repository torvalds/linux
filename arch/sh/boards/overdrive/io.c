/* 
 * Copyright (C) 2000 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * This file contains the I/O routines for use on the overdrive board
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/addrspace.h>

#include <asm/overdrive/overdrive.h>

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the SuperH architecture, we just read/write the
 * memory location directly.
 */

#define dprintk(x...)

/* Translates an IO address to where it is mapped in memory */

#define io_addr(x) (((unsigned)(x))|PCI_GTIO_BASE)

unsigned char od_inb(unsigned long port)
{
dprintk("od_inb(%x)\n", port);
	return readb(io_addr(port)) & 0xff;
}


unsigned short od_inw(unsigned long port)
{
dprintk("od_inw(%x)\n", port);
	return readw(io_addr(port)) & 0xffff;
}

unsigned int od_inl(unsigned long port)
{
dprintk("od_inl(%x)\n", port);
	return readl(io_addr(port));
}

void od_outb(unsigned char value, unsigned long port)
{
dprintk("od_outb(%x, %x)\n", value, port);
	writeb(value, io_addr(port));
}

void od_outw(unsigned short value, unsigned long port)
{
dprintk("od_outw(%x, %x)\n", value, port);
	writew(value, io_addr(port));
}

void od_outl(unsigned int value, unsigned long port)
{
dprintk("od_outl(%x, %x)\n", value, port);
	writel(value, io_addr(port));
}

/* This is horrible at the moment - needs more work to do something sensible */
#define IO_DELAY() udelay(10)

#define OUT_DELAY(x,type) \
void od_out##x##_p(unsigned type value,unsigned long port){out##x(value,port);IO_DELAY();}

#define IN_DELAY(x,type) \
unsigned type od_in##x##_p(unsigned long port) {unsigned type tmp=in##x(port);IO_DELAY();return tmp;}


OUT_DELAY(b,char)
OUT_DELAY(w,short)
OUT_DELAY(l,int)

IN_DELAY(b,char)
IN_DELAY(w,short)
IN_DELAY(l,int)


/*  Now for the string version of these functions */
void od_outsb(unsigned long port, const void *addr, unsigned long count)
{
	int i;
	unsigned char *p = (unsigned char *) addr;

	for (i = 0; i < count; i++, p++) {
		outb(*p, port);
	}
}


void od_insb(unsigned long port, void *addr, unsigned long count)
{
	int i;
	unsigned char *p = (unsigned char *) addr;

	for (i = 0; i < count; i++, p++) {
		*p = inb(port);
	}
}

/* For the 16 and 32 bit string functions, we have to worry about alignment.
 * The SH does not do unaligned accesses, so we have to read as bytes and
 * then write as a word or dword. 
 * This can be optimised a lot more, especially in the case where the data
 * is aligned
 */

void od_outsw(unsigned long port, const void *addr, unsigned long count)
{
	int i;
	unsigned short tmp;
	unsigned char *p = (unsigned char *) addr;

	for (i = 0; i < count; i++, p += 2) {
		tmp = (*p) | ((*(p + 1)) << 8);
		outw(tmp, port);
	}
}


void od_insw(unsigned long port, void *addr, unsigned long count)
{
	int i;
	unsigned short tmp;
	unsigned char *p = (unsigned char *) addr;

	for (i = 0; i < count; i++, p += 2) {
		tmp = inw(port);
		p[0] = tmp & 0xff;
		p[1] = (tmp >> 8) & 0xff;
	}
}


void od_outsl(unsigned long port, const void *addr, unsigned long count)
{
	int i;
	unsigned tmp;
	unsigned char *p = (unsigned char *) addr;

	for (i = 0; i < count; i++, p += 4) {
		tmp = (*p) | ((*(p + 1)) << 8) | ((*(p + 2)) << 16) |
		      ((*(p + 3)) << 24);
		outl(tmp, port);
	}
}


void od_insl(unsigned long port, void *addr, unsigned long count)
{
	int i;
	unsigned tmp;
	unsigned char *p = (unsigned char *) addr;

	for (i = 0; i < count; i++, p += 4) {
		tmp = inl(port);
		p[0] = tmp & 0xff;
		p[1] = (tmp >> 8) & 0xff;
		p[2] = (tmp >> 16) & 0xff;
		p[3] = (tmp >> 24) & 0xff;

	}
}
