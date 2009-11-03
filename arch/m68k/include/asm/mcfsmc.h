/****************************************************************************/

/*
 *	mcfsmc.h -- SMC ethernet support for ColdFire environments.
 *
 *	(C) Copyright 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2000, Lineo Inc. (www.lineo.com) 
 */

/****************************************************************************/
#ifndef	mcfsmc_h
#define	mcfsmc_h
/****************************************************************************/

/*
 *	None of the current ColdFire targets that use the SMC91x111
 *	allow 8 bit accesses. So this code is 16bit access only.
 */


#undef	outb
#undef	inb
#undef	outw
#undef	outwd
#undef	inw	
#undef	outl
#undef	inl

#undef	outsb
#undef	outsw
#undef	outsl
#undef	insb
#undef	insw
#undef	insl

/*
 *	Re-defines for ColdFire environment... The SMC part is
 *	mapped into memory space, so remap the PC-style in/out
 *	routines to handle that.
 */
#define	outb	smc_outb
#define	inb	smc_inb
#define	outw	smc_outw
#define	outwd	smc_outwd
#define	inw	smc_inw
#define	outl	smc_outl
#define	inl	smc_inl

#define	outsb	smc_outsb
#define	outsw	smc_outsw
#define	outsl	smc_outsl
#define	insb	smc_insb
#define	insw	smc_insw
#define	insl	smc_insl


static inline int smc_inb(unsigned int addr)
{
	register unsigned short	w;
	w = *((volatile unsigned short *) (addr & ~0x1));
	return(((addr & 0x1) ? w : (w >> 8)) & 0xff);
}

static inline void smc_outw(unsigned int val, unsigned int addr)
{
	*((volatile unsigned short *) addr) = (val << 8) | (val >> 8);
}

static inline int smc_inw(unsigned int addr)
{
	register unsigned short	w;
	w = *((volatile unsigned short *) addr);
	return(((w << 8) | (w >> 8)) & 0xffff);
}

static inline void smc_outl(unsigned long val, unsigned int addr)
{
	*((volatile unsigned long *) addr) = 
		((val << 8) & 0xff000000) | ((val >> 8) & 0x00ff0000) |
		((val << 8) & 0x0000ff00) | ((val >> 8) & 0x000000ff);
}

static inline void smc_outwd(unsigned int val, unsigned int addr)
{
	*((volatile unsigned short *) addr) = val;
}


/*
 *	The rep* functions are used to feed the data port with
 *	raw data. So we do not byte swap them when copying.
 */

static inline void smc_insb(unsigned int addr, void *vbuf, int unsigned long len)
{
	volatile unsigned short	*rp;
	unsigned short		*buf, *ebuf;

	buf = (unsigned short *) vbuf;
	rp = (volatile unsigned short *) addr;

	/* Copy as words for as long as possible */
	for (ebuf = buf + (len >> 1); (buf < ebuf); )
		*buf++ = *rp;

	/* Lastly, handle left over byte */
	if (len & 0x1)
		*((unsigned char *) buf) = (*rp >> 8) & 0xff;
}

static inline void smc_insw(unsigned int addr, void *vbuf, unsigned long len)
{
	volatile unsigned short	*rp;
	unsigned short		*buf, *ebuf;

	buf = (unsigned short *) vbuf;
	rp = (volatile unsigned short *) addr;
	for (ebuf = buf + len; (buf < ebuf); )
		*buf++ = *rp;
}

static inline void smc_insl(unsigned int addr, void *vbuf, unsigned long len)
{
	volatile unsigned long	*rp;
	unsigned long		*buf, *ebuf;

	buf = (unsigned long *) vbuf;
	rp = (volatile unsigned long *) addr;
	for (ebuf = buf + len; (buf < ebuf); )
		*buf++ = *rp;
}

static inline void smc_outsw(unsigned int addr, const void *vbuf, unsigned long len)
{
	volatile unsigned short	*rp;
	unsigned short		*buf, *ebuf;

	buf = (unsigned short *) vbuf;
	rp = (volatile unsigned short *) addr;
	for (ebuf = buf + len; (buf < ebuf); )
		*rp = *buf++;
}

static inline void smc_outsl(unsigned int addr, void *vbuf, unsigned long len)
{
	volatile unsigned long	*rp;
	unsigned long		*buf, *ebuf;

	buf = (unsigned long *) vbuf;
	rp = (volatile unsigned long *) addr;
	for (ebuf = buf + len; (buf < ebuf); )
		*rp = *buf++;
}


#ifdef CONFIG_NETtel
/*
 *	Re-map the address space of at least one of the SMC ethernet
 *	parts. Both parts power up decoding the same address, so we
 *	need to move one of them first, before doing enything else.
 *
 *	We also increase the number of wait states for this part by one.
 */

void smc_remap(unsigned int ioaddr)
{
	static int		once = 0;
	extern unsigned short	ppdata;
	if (once++ == 0) {
		*((volatile unsigned short *)MCFSIM_PADDR) = 0x00ec;
		ppdata |= 0x0080;
		*((volatile unsigned short *)MCFSIM_PADAT) = ppdata;
		outw(0x0001, ioaddr + BANK_SELECT);
		outw(0x0001, ioaddr + BANK_SELECT);
		outw(0x0067, ioaddr + BASE);

		ppdata &= ~0x0080;
		*((volatile unsigned short *)MCFSIM_PADAT) = ppdata;
	}
	
	*((volatile unsigned short *)(MCF_MBAR+MCFSIM_CSCR3)) = 0x1180;
}

#endif

/****************************************************************************/
#endif	/* mcfsmc_h */
