// SPDX-License-Identifier: GPL-2.0
/*
 * arch/parisc/lib/io.c
 *
 * Copyright (c) Matthew Wilcox 2001 for Hewlett-Packard
 * Copyright (c) Randolph Chung 2001 <tausq@debian.org>
 *
 * IO accessing functions which shouldn't be inlined because they're too big
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/io.h>

/*
** Copies a block of memory from a device in an efficient manner.
** Assumes the device can cope with 32-bit transfers.  If it can't,
** don't use this function.
**
** CR16 counts on C3000 reading 256 bytes from Symbios 896 RAM:
**	27341/64    = 427 cyc per int
**	61311/128   = 478 cyc per short
**	122637/256  = 479 cyc per byte
** Ergo bus latencies dominant (not transfer size).
**      Minimize total number of transfers at cost of CPU cycles.
**	TODO: only look at src alignment and adjust the stores to dest.
*/
void memcpy_fromio(void *dst, const volatile void __iomem *src, int count)
{
	/* first compare alignment of src/dst */ 
	if ( (((unsigned long)dst ^ (unsigned long)src) & 1) || (count < 2) )
		goto bytecopy;

	if ( (((unsigned long)dst ^ (unsigned long)src) & 2) || (count < 4) )
		goto shortcopy;

	/* Then check for misaligned start address */
	if ((unsigned long)src & 1) {
		*(u8 *)dst = readb(src);
		src++;
		dst++;
		count--;
		if (count < 2) goto bytecopy;
	}

	if ((unsigned long)src & 2) {
		*(u16 *)dst = __raw_readw(src);
		src += 2;
		dst += 2;
		count -= 2;
	}

	while (count > 3) {
		*(u32 *)dst = __raw_readl(src);
		dst += 4;
		src += 4;
		count -= 4;
	}

 shortcopy:
	while (count > 1) {
		*(u16 *)dst = __raw_readw(src);
		src += 2;
		dst += 2;
		count -= 2;
	}

 bytecopy:
	while (count--) {
		*(char *)dst = readb(src);
		src++;
		dst++;
	}
}

/*
 * Read COUNT 8-bit bytes from port PORT into memory starting at
 * SRC.
 */
void insb (unsigned long port, void *dst, unsigned long count)
{
	unsigned char *p;

	p = (unsigned char *)dst;

	while (((unsigned long)p) & 0x3) {
		if (!count)
			return;
		count--;
		*p = inb(port);
		p++;
	}

	while (count >= 4) {
		unsigned int w;
		count -= 4;
		w = inb(port) << 24;
		w |= inb(port) << 16;
		w |= inb(port) << 8;
		w |= inb(port);
		*(unsigned int *) p = w;
		p += 4;
	}

	while (count) {
		--count;
		*p = inb(port);
		p++;
	}
}


/*
 * Read COUNT 16-bit words from port PORT into memory starting at
 * SRC.  SRC must be at least short aligned.  This is used by the
 * IDE driver to read disk sectors.  Performance is important, but
 * the interfaces seems to be slow: just using the inlined version
 * of the inw() breaks things.
 */
void insw (unsigned long port, void *dst, unsigned long count)
{
	unsigned int l = 0, l2;
	unsigned char *p;

	p = (unsigned char *)dst;
	
	if (!count)
		return;
	
	switch (((unsigned long)p) & 0x3)
	{
	 case 0x00:			/* Buffer 32-bit aligned */
		while (count>=2) {
			
			count -= 2;
			l = cpu_to_le16(inw(port)) << 16;
			l |= cpu_to_le16(inw(port));
			*(unsigned int *)p = l;
			p += 4;
		}
		if (count) {
			*(unsigned short *)p = cpu_to_le16(inw(port));
		}
		break;
	
	 case 0x02:			/* Buffer 16-bit aligned */
		*(unsigned short *)p = cpu_to_le16(inw(port));
		p += 2;
		count--;
		while (count>=2) {
			
			count -= 2;
			l = cpu_to_le16(inw(port)) << 16;
			l |= cpu_to_le16(inw(port));
			*(unsigned int *)p = l;
			p += 4;
		}
		if (count) {
			*(unsigned short *)p = cpu_to_le16(inw(port));
		}
		break;
		
	 case 0x01:			/* Buffer 8-bit aligned */
	 case 0x03:
		/* I don't bother with 32bit transfers
		 * in this case, 16bit will have to do -- DE */
		--count;
		
		l = cpu_to_le16(inw(port));
		*p = l >> 8;
		p++;
		while (count--)
		{
			l2 = cpu_to_le16(inw(port));
			*(unsigned short *)p = (l & 0xff) << 8 | (l2 >> 8);
			p += 2;
			l = l2;
		}
		*p = l & 0xff;
		break;
	}
}



/*
 * Read COUNT 32-bit words from port PORT into memory starting at
 * SRC. Now works with any alignment in SRC. Performance is important,
 * but the interfaces seems to be slow: just using the inlined version
 * of the inl() breaks things.
 */
void insl (unsigned long port, void *dst, unsigned long count)
{
	unsigned int l = 0, l2;
	unsigned char *p;

	p = (unsigned char *)dst;
	
	if (!count)
		return;
	
	switch (((unsigned long) dst) & 0x3)
	{
	 case 0x00:			/* Buffer 32-bit aligned */
		while (count--)
		{
			*(unsigned int *)p = cpu_to_le32(inl(port));
			p += 4;
		}
		break;
	
	 case 0x02:			/* Buffer 16-bit aligned */
		--count;
		
		l = cpu_to_le32(inl(port));
		*(unsigned short *)p = l >> 16;
		p += 2;
		
		while (count--)
		{
			l2 = cpu_to_le32(inl(port));
			*(unsigned int *)p = (l & 0xffff) << 16 | (l2 >> 16);
			p += 4;
			l = l2;
		}
		*(unsigned short *)p = l & 0xffff;
		break;
	 case 0x01:			/* Buffer 8-bit aligned */
		--count;
		
		l = cpu_to_le32(inl(port));
		*(unsigned char *)p = l >> 24;
		p++;
		*(unsigned short *)p = (l >> 8) & 0xffff;
		p += 2;
		while (count--)
		{
			l2 = cpu_to_le32(inl(port));
			*(unsigned int *)p = (l & 0xff) << 24 | (l2 >> 8);
			p += 4;
			l = l2;
		}
		*p = l & 0xff;
		break;
	 case 0x03:			/* Buffer 8-bit aligned */
		--count;
		
		l = cpu_to_le32(inl(port));
		*p = l >> 24;
		p++;
		while (count--)
		{
			l2 = cpu_to_le32(inl(port));
			*(unsigned int *)p = (l & 0xffffff) << 8 | l2 >> 24;
			p += 4;
			l = l2;
		}
		*(unsigned short *)p = (l >> 8) & 0xffff;
		p += 2;
		*p = l & 0xff;
		break;
	}
}


/*
 * Like insb but in the opposite direction.
 * Don't worry as much about doing aligned memory transfers:
 * doing byte reads the "slow" way isn't nearly as slow as
 * doing byte writes the slow way (no r-m-w cycle).
 */
void outsb(unsigned long port, const void * src, unsigned long count)
{
	const unsigned char *p;

	p = (const unsigned char *)src;
	while (count) {
		count--;
		outb(*p, port);
		p++;
	}
}

/*
 * Like insw but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Performance is important, but the
 * interfaces seems to be slow: just using the inlined version of the
 * outw() breaks things.
 */
void outsw (unsigned long port, const void *src, unsigned long count)
{
	unsigned int l = 0, l2;
	const unsigned char *p;

	p = (const unsigned char *)src;
	
	if (!count)
		return;
	
	switch (((unsigned long)p) & 0x3)
	{
	 case 0x00:			/* Buffer 32-bit aligned */
		while (count>=2) {
			count -= 2;
			l = *(unsigned int *)p;
			p += 4;
			outw(le16_to_cpu(l >> 16), port);
			outw(le16_to_cpu(l & 0xffff), port);
		}
		if (count) {
			outw(le16_to_cpu(*(unsigned short*)p), port);
		}
		break;
	
	 case 0x02:			/* Buffer 16-bit aligned */
		
		outw(le16_to_cpu(*(unsigned short*)p), port);
		p += 2;
		count--;
		
		while (count>=2) {
			count -= 2;
			l = *(unsigned int *)p;
			p += 4;
			outw(le16_to_cpu(l >> 16), port);
			outw(le16_to_cpu(l & 0xffff), port);
		}
		if (count) {
			outw(le16_to_cpu(*(unsigned short *)p), port);
		}
		break;
		
	 case 0x01:			/* Buffer 8-bit aligned */	
		/* I don't bother with 32bit transfers
		 * in this case, 16bit will have to do -- DE */
		
		l  = *p << 8;
		p++;
		count--;
		while (count)
		{
			count--;
			l2 = *(unsigned short *)p;
			p += 2;
			outw(le16_to_cpu(l | l2 >> 8), port);
		        l = l2 << 8;
		}
		l2 = *(unsigned char *)p;
		outw (le16_to_cpu(l | l2>>8), port);
		break;
	
	}
}


/*
 * Like insl but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Works with any alignment in SRC.
 *  Performance is important, but the interfaces seems to be slow:
 * just using the inlined version of the outl() breaks things.
 */
void outsl (unsigned long port, const void *src, unsigned long count)
{
	unsigned int l = 0, l2;
	const unsigned char *p;

	p = (const unsigned char *)src;
	
	if (!count)
		return;
	
	switch (((unsigned long)p) & 0x3)
	{
	 case 0x00:			/* Buffer 32-bit aligned */
		while (count--)
		{
			outl(le32_to_cpu(*(unsigned int *)p), port);
			p += 4;
		}
		break;
	
	 case 0x02:			/* Buffer 16-bit aligned */
		--count;
		
		l = *(unsigned short *)p;
		p += 2;
		
		while (count--)
		{
			l2 = *(unsigned int *)p;
			p += 4;
			outl (le32_to_cpu(l << 16 | l2 >> 16), port);
			l = l2;
		}
		l2 = *(unsigned short *)p;
		outl (le32_to_cpu(l << 16 | l2), port);
		break;
	 case 0x01:			/* Buffer 8-bit aligned */
		--count;

		l = *p << 24;
		p++;
		l |= *(unsigned short *)p << 8;
		p += 2;

		while (count--)
		{
			l2 = *(unsigned int *)p;
			p += 4;
			outl (le32_to_cpu(l | l2 >> 24), port);
			l = l2 << 8;
		}
		l2 = *p;
		outl (le32_to_cpu(l | l2), port);
		break;
	 case 0x03:			/* Buffer 8-bit aligned */
		--count;
		
		l = *p << 24;
		p++;

		while (count--)
		{
			l2 = *(unsigned int *)p;
			p += 4;
			outl (le32_to_cpu(l | l2 >> 8), port);
			l = l2 << 24;
		}
		l2 = *(unsigned short *)p << 16;
		p += 2;
		l2 |= *p;
		outl (le32_to_cpu(l | l2), port);
		break;
	}
}

EXPORT_SYMBOL(insb);
EXPORT_SYMBOL(insw);
EXPORT_SYMBOL(insl);
EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(outsl);
