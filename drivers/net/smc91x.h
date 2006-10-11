/*------------------------------------------------------------------------
 . smc91x.h - macros for SMSC's 91C9x/91C1xx single-chip Ethernet device.
 .
 . Copyright (C) 1996 by Erik Stahlman
 . Copyright (C) 2001 Standard Microsystems Corporation
 .	Developed by Simple Network Magic Corporation
 . Copyright (C) 2003 Monta Vista Software, Inc.
 .	Unified SMC91x driver by Nicolas Pitre
 .
 . This program is free software; you can redistribute it and/or modify
 . it under the terms of the GNU General Public License as published by
 . the Free Software Foundation; either version 2 of the License, or
 . (at your option) any later version.
 .
 . This program is distributed in the hope that it will be useful,
 . but WITHOUT ANY WARRANTY; without even the implied warranty of
 . MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 . GNU General Public License for more details.
 .
 . You should have received a copy of the GNU General Public License
 . along with this program; if not, write to the Free Software
 . Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 .
 . Information contained in this file was obtained from the LAN91C111
 . manual from SMC.  To get a copy, if you really want one, you can find
 . information under www.smsc.com.
 .
 . Authors
 .	Erik Stahlman		<erik@vt.edu>
 .	Daris A Nevil		<dnevil@snmc.com>
 .	Nicolas Pitre 		<nico@cam.org>
 .
 ---------------------------------------------------------------------------*/
#ifndef _SMC91X_H_
#define _SMC91X_H_


/*
 * Define your architecture specific bus configuration parameters here.
 */

#if	defined(CONFIG_ARCH_LUBBOCK)

/* We can only do 16-bit reads and writes in the static memory space. */
#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0
#define SMC_NOWAIT		1

/* The first two address lines aren't connected... */
#define SMC_IO_SHIFT		2

#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_insw(a, r, p, l)	readsw((a) + (r), p, l)
#define SMC_outsw(a, r, p, l)	writesw((a) + (r), p, l)

#elif defined(CONFIG_REDWOOD_5) || defined(CONFIG_REDWOOD_6)

/* We can only do 16-bit reads and writes in the static memory space. */
#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0
#define SMC_NOWAIT		1

#define SMC_IO_SHIFT		0

#define SMC_inw(a, r)		in_be16((volatile u16 *)((a) + (r)))
#define SMC_outw(v, a, r)	out_be16((volatile u16 *)((a) + (r)), v)
#define SMC_insw(a, r, p, l) 						\
	do {								\
		unsigned long __port = (a) + (r);			\
		u16 *__p = (u16 *)(p);					\
		int __l = (l);						\
		insw(__port, __p, __l);					\
		while (__l > 0) {					\
			*__p = swab16(*__p);				\
			__p++;						\
			__l--;						\
		}							\
	} while (0)
#define SMC_outsw(a, r, p, l) 						\
	do {								\
		unsigned long __port = (a) + (r);			\
		u16 *__p = (u16 *)(p);					\
		int __l = (l);						\
		while (__l > 0) {					\
			/* Believe it or not, the swab isn't needed. */	\
			outw( /* swab16 */ (*__p++), __port);		\
			__l--;						\
		}							\
	} while (0)
#define SMC_IRQ_FLAGS		(0)

#elif defined(CONFIG_SA1100_PLEB)
/* We can only do 16-bit reads and writes in the static memory space. */
#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0
#define SMC_IO_SHIFT		0
#define SMC_NOWAIT		1

#define SMC_inb(a, r)		readb((a) + (r))
#define SMC_insb(a, r, p, l)	readsb((a) + (r), p, (l))
#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_insw(a, r, p, l)	readsw((a) + (r), p, l)
#define SMC_outb(v, a, r)	writeb(v, (a) + (r))
#define SMC_outsb(a, r, p, l)	writesb((a) + (r), p, (l))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_outsw(a, r, p, l)	writesw((a) + (r), p, l)

#define SMC_IRQ_FLAGS		(0)

#elif defined(CONFIG_SA1100_ASSABET)

#include <asm/arch/neponset.h>

/* We can only do 8-bit reads and writes in the static memory space. */
#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	0
#define SMC_CAN_USE_32BIT	0
#define SMC_NOWAIT		1

/* The first two address lines aren't connected... */
#define SMC_IO_SHIFT		2

#define SMC_inb(a, r)		readb((a) + (r))
#define SMC_outb(v, a, r)	writeb(v, (a) + (r))
#define SMC_insb(a, r, p, l)	readsb((a) + (r), p, (l))
#define SMC_outsb(a, r, p, l)	writesb((a) + (r), p, (l))

#elif	defined(CONFIG_MACH_LOGICPD_PXA270)

#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0
#define SMC_IO_SHIFT		0
#define SMC_NOWAIT		1

#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_insw(a, r, p, l)	readsw((a) + (r), p, l)
#define SMC_outsw(a, r, p, l)	writesw((a) + (r), p, l)

#elif	defined(CONFIG_ARCH_INNOKOM) || \
	defined(CONFIG_MACH_MAINSTONE) || \
	defined(CONFIG_ARCH_PXA_IDP) || \
	defined(CONFIG_ARCH_RAMSES)

#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	1
#define SMC_IO_SHIFT		0
#define SMC_NOWAIT		1
#define SMC_USE_PXA_DMA		1

#define SMC_inb(a, r)		readb((a) + (r))
#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_inl(a, r)		readl((a) + (r))
#define SMC_outb(v, a, r)	writeb(v, (a) + (r))
#define SMC_outl(v, a, r)	writel(v, (a) + (r))
#define SMC_insl(a, r, p, l)	readsl((a) + (r), p, l)
#define SMC_outsl(a, r, p, l)	writesl((a) + (r), p, l)

/* We actually can't write halfwords properly if not word aligned */
static inline void
SMC_outw(u16 val, void __iomem *ioaddr, int reg)
{
	if (reg & 2) {
		unsigned int v = val << 16;
		v |= readl(ioaddr + (reg & ~2)) & 0xffff;
		writel(v, ioaddr + (reg & ~2));
	} else {
		writew(val, ioaddr + reg);
	}
}

#elif	defined(CONFIG_ARCH_OMAP)

/* We can only do 16-bit reads and writes in the static memory space. */
#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0
#define SMC_IO_SHIFT		0
#define SMC_NOWAIT		1

#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_insw(a, r, p, l)	readsw((a) + (r), p, l)
#define SMC_outsw(a, r, p, l)	writesw((a) + (r), p, l)

#include <asm/mach-types.h>
#include <asm/arch/cpu.h>

#define	SMC_IRQ_FLAGS (( \
		   machine_is_omap_h2() \
		|| machine_is_omap_h3() \
		|| machine_is_omap_h4() \
		|| (machine_is_omap_innovator() && !cpu_is_omap1510()) \
	) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING)


#elif	defined(CONFIG_SH_SH4202_MICRODEV)

#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0

#define SMC_inb(a, r)		inb((a) + (r) - 0xa0000000)
#define SMC_inw(a, r)		inw((a) + (r) - 0xa0000000)
#define SMC_inl(a, r)		inl((a) + (r) - 0xa0000000)
#define SMC_outb(v, a, r)	outb(v, (a) + (r) - 0xa0000000)
#define SMC_outw(v, a, r)	outw(v, (a) + (r) - 0xa0000000)
#define SMC_outl(v, a, r)	outl(v, (a) + (r) - 0xa0000000)
#define SMC_insl(a, r, p, l)	insl((a) + (r) - 0xa0000000, p, l)
#define SMC_outsl(a, r, p, l)	outsl((a) + (r) - 0xa0000000, p, l)
#define SMC_insw(a, r, p, l)	insw((a) + (r) - 0xa0000000, p, l)
#define SMC_outsw(a, r, p, l)	outsw((a) + (r) - 0xa0000000, p, l)

#define SMC_IRQ_FLAGS		(0)

#elif	defined(CONFIG_ISA)

#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0

#define SMC_inb(a, r)		inb((a) + (r))
#define SMC_inw(a, r)		inw((a) + (r))
#define SMC_outb(v, a, r)	outb(v, (a) + (r))
#define SMC_outw(v, a, r)	outw(v, (a) + (r))
#define SMC_insw(a, r, p, l)	insw((a) + (r), p, l)
#define SMC_outsw(a, r, p, l)	outsw((a) + (r), p, l)

#elif   defined(CONFIG_M32R)

#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0

#define SMC_inb(a, r)		inb((u32)a) + (r))
#define SMC_inw(a, r)		inw(((u32)a) + (r))
#define SMC_outb(v, a, r)	outb(v, ((u32)a) + (r))
#define SMC_outw(v, a, r)	outw(v, ((u32)a) + (r))
#define SMC_insw(a, r, p, l)	insw(((u32)a) + (r), p, l)
#define SMC_outsw(a, r, p, l)	outsw(((u32)a) + (r), p, l)

#define SMC_IRQ_FLAGS		(0)

#define RPC_LSA_DEFAULT		RPC_LED_TX_RX
#define RPC_LSB_DEFAULT		RPC_LED_100_10

#elif   defined(CONFIG_MACH_LPD79520) \
     || defined(CONFIG_MACH_LPD7A400) \
     || defined(CONFIG_MACH_LPD7A404)

/* The LPD7X_IOBARRIER is necessary to overcome a mismatch between the
 * way that the CPU handles chip selects and the way that the SMC chip
 * expects the chip select to operate.  Refer to
 * Documentation/arm/Sharp-LH/IOBarrier for details.  The read from
 * IOBARRIER is a byte, in order that we read the least-common
 * denominator.  It would be wasteful to read 32 bits from an 8-bit
 * accessible region.
 *
 * There is no explicit protection against interrupts intervening
 * between the writew and the IOBARRIER.  In SMC ISR there is a
 * preamble that performs an IOBARRIER in the extremely unlikely event
 * that the driver interrupts itself between a writew to the chip an
 * the IOBARRIER that follows *and* the cache is large enough that the
 * first off-chip access while handing the interrupt is to the SMC
 * chip.  Other devices in the same address space as the SMC chip must
 * be aware of the potential for trouble and perform a similar
 * IOBARRIER on entry to their ISR.
 */

#include <asm/arch/constants.h>	/* IOBARRIER_VIRT */

#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0
#define SMC_NOWAIT		0
#define LPD7X_IOBARRIER		readb (IOBARRIER_VIRT)

#define SMC_inw(a,r)\
   ({ unsigned short v = readw ((void*) ((a) + (r))); LPD7X_IOBARRIER; v; })
#define SMC_outw(v,a,r)	  ({ writew ((v), (a) + (r)); LPD7X_IOBARRIER; })

#define SMC_insw		LPD7_SMC_insw
static inline void LPD7_SMC_insw (unsigned char* a, int r,
				  unsigned char* p, int l)
{
	unsigned short* ps = (unsigned short*) p;
	while (l-- > 0) {
		*ps++ = readw (a + r);
		LPD7X_IOBARRIER;
	}
}

#define SMC_outsw		LPD7_SMC_outsw
static inline void LPD7_SMC_outsw (unsigned char* a, int r,
				   unsigned char* p, int l)
{
	unsigned short* ps = (unsigned short*) p;
	while (l-- > 0) {
		writew (*ps++, a + r);
		LPD7X_IOBARRIER;
	}
}

#define SMC_INTERRUPT_PREAMBLE	LPD7X_IOBARRIER

#define RPC_LSA_DEFAULT		RPC_LED_TX_RX
#define RPC_LSB_DEFAULT		RPC_LED_100_10

#elif defined(CONFIG_SOC_AU1X00)

#include <au1xxx.h>

/* We can only do 16-bit reads and writes in the static memory space. */
#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0
#define SMC_IO_SHIFT		0
#define SMC_NOWAIT		1

#define SMC_inw(a, r)		au_readw((unsigned long)((a) + (r)))
#define SMC_insw(a, r, p, l)	\
	do {	\
		unsigned long _a = (unsigned long)((a) + (r)); \
		int _l = (l); \
		u16 *_p = (u16 *)(p); \
		while (_l-- > 0) \
			*_p++ = au_readw(_a); \
	} while(0)
#define SMC_outw(v, a, r)	au_writew(v, (unsigned long)((a) + (r)))
#define SMC_outsw(a, r, p, l)	\
	do {	\
		unsigned long _a = (unsigned long)((a) + (r)); \
		int _l = (l); \
		const u16 *_p = (const u16 *)(p); \
		while (_l-- > 0) \
			au_writew(*_p++ , _a); \
	} while(0)

#define SMC_IRQ_FLAGS		(0)

#elif	defined(CONFIG_ARCH_VERSATILE)

#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	1
#define SMC_NOWAIT		1

#define SMC_inb(a, r)		readb((a) + (r))
#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_inl(a, r)		readl((a) + (r))
#define SMC_outb(v, a, r)	writeb(v, (a) + (r))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_outl(v, a, r)	writel(v, (a) + (r))
#define SMC_insl(a, r, p, l)	readsl((a) + (r), p, l)
#define SMC_outsl(a, r, p, l)	writesl((a) + (r), p, l)

#define SMC_IRQ_FLAGS		(0)

#elif	defined(CONFIG_ARCH_VERSATILE)

#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	1
#define SMC_NOWAIT		1

#define SMC_inb(a, r)		readb((a) + (r))
#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_inl(a, r)		readl((a) + (r))
#define SMC_outb(v, a, r)	writeb(v, (a) + (r))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_outl(v, a, r)	writel(v, (a) + (r))
#define SMC_insl(a, r, p, l)	readsl((a) + (r), p, l)
#define SMC_outsl(a, r, p, l)	writesl((a) + (r), p, l)

#define SMC_IRQ_FLAGS		(0)

#elif	defined(CONFIG_ARCH_VERSATILE)

#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	1
#define SMC_NOWAIT		1

#define SMC_inb(a, r)		readb((a) + (r))
#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_inl(a, r)		readl((a) + (r))
#define SMC_outb(v, a, r)	writeb(v, (a) + (r))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_outl(v, a, r)	writel(v, (a) + (r))
#define SMC_insl(a, r, p, l)	readsl((a) + (r), p, l)
#define SMC_outsl(a, r, p, l)	writesl((a) + (r), p, l)

#define SMC_IRQ_FLAGS		(0)

#elif	defined(CONFIG_ARCH_VERSATILE)

#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	1
#define SMC_NOWAIT		1

#define SMC_inb(a, r)		readb((a) + (r))
#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_inl(a, r)		readl((a) + (r))
#define SMC_outb(v, a, r)	writeb(v, (a) + (r))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_outl(v, a, r)	writel(v, (a) + (r))
#define SMC_insl(a, r, p, l)	readsl((a) + (r), p, l)
#define SMC_outsl(a, r, p, l)	writesl((a) + (r), p, l)

#define SMC_IRQ_FLAGS		(0)

#else

#define SMC_CAN_USE_8BIT	1
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	1
#define SMC_NOWAIT		1

#define SMC_inb(a, r)		readb((a) + (r))
#define SMC_inw(a, r)		readw((a) + (r))
#define SMC_inl(a, r)		readl((a) + (r))
#define SMC_outb(v, a, r)	writeb(v, (a) + (r))
#define SMC_outw(v, a, r)	writew(v, (a) + (r))
#define SMC_outl(v, a, r)	writel(v, (a) + (r))
#define SMC_insl(a, r, p, l)	readsl((a) + (r), p, l)
#define SMC_outsl(a, r, p, l)	writesl((a) + (r), p, l)

#define RPC_LSA_DEFAULT		RPC_LED_100_10
#define RPC_LSB_DEFAULT		RPC_LED_TX_RX

#endif

#ifdef SMC_USE_PXA_DMA
/*
 * Let's use the DMA engine on the XScale PXA2xx for RX packets. This is
 * always happening in irq context so no need to worry about races.  TX is
 * different and probably not worth it for that reason, and not as critical
 * as RX which can overrun memory and lose packets.
 */
#include <linux/dma-mapping.h>
#include <asm/dma.h>
#include <asm/arch/pxa-regs.h>

#ifdef SMC_insl
#undef SMC_insl
#define SMC_insl(a, r, p, l) \
	smc_pxa_dma_insl(a, lp->physaddr, r, dev->dma, p, l)
static inline void
smc_pxa_dma_insl(void __iomem *ioaddr, u_long physaddr, int reg, int dma,
		 u_char *buf, int len)
{
	dma_addr_t dmabuf;

	/* fallback if no DMA available */
	if (dma == (unsigned char)-1) {
		readsl(ioaddr + reg, buf, len);
		return;
	}

	/* 64 bit alignment is required for memory to memory DMA */
	if ((long)buf & 4) {
		*((u32 *)buf) = SMC_inl(ioaddr, reg);
		buf += 4;
		len--;
	}

	len *= 4;
	dmabuf = dma_map_single(NULL, buf, len, DMA_FROM_DEVICE);
	DCSR(dma) = DCSR_NODESC;
	DTADR(dma) = dmabuf;
	DSADR(dma) = physaddr + reg;
	DCMD(dma) = (DCMD_INCTRGADDR | DCMD_BURST32 |
		     DCMD_WIDTH4 | (DCMD_LENGTH & len));
	DCSR(dma) = DCSR_NODESC | DCSR_RUN;
	while (!(DCSR(dma) & DCSR_STOPSTATE))
		cpu_relax();
	DCSR(dma) = 0;
	dma_unmap_single(NULL, dmabuf, len, DMA_FROM_DEVICE);
}
#endif

#ifdef SMC_insw
#undef SMC_insw
#define SMC_insw(a, r, p, l) \
	smc_pxa_dma_insw(a, lp->physaddr, r, dev->dma, p, l)
static inline void
smc_pxa_dma_insw(void __iomem *ioaddr, u_long physaddr, int reg, int dma,
		 u_char *buf, int len)
{
	dma_addr_t dmabuf;

	/* fallback if no DMA available */
	if (dma == (unsigned char)-1) {
		readsw(ioaddr + reg, buf, len);
		return;
	}

	/* 64 bit alignment is required for memory to memory DMA */
	while ((long)buf & 6) {
		*((u16 *)buf) = SMC_inw(ioaddr, reg);
		buf += 2;
		len--;
	}

	len *= 2;
	dmabuf = dma_map_single(NULL, buf, len, DMA_FROM_DEVICE);
	DCSR(dma) = DCSR_NODESC;
	DTADR(dma) = dmabuf;
	DSADR(dma) = physaddr + reg;
	DCMD(dma) = (DCMD_INCTRGADDR | DCMD_BURST32 |
		     DCMD_WIDTH2 | (DCMD_LENGTH & len));
	DCSR(dma) = DCSR_NODESC | DCSR_RUN;
	while (!(DCSR(dma) & DCSR_STOPSTATE))
		cpu_relax();
	DCSR(dma) = 0;
	dma_unmap_single(NULL, dmabuf, len, DMA_FROM_DEVICE);
}
#endif

static void
smc_pxa_dma_irq(int dma, void *dummy)
{
	DCSR(dma) = 0;
}
#endif  /* SMC_USE_PXA_DMA */


/*
 * Everything a particular hardware setup needs should have been defined
 * at this point.  Add stubs for the undefined cases, mainly to avoid
 * compilation warnings since they'll be optimized away, or to prevent buggy
 * use of them.
 */

#if ! SMC_CAN_USE_32BIT
#define SMC_inl(ioaddr, reg)		({ BUG(); 0; })
#define SMC_outl(x, ioaddr, reg)	BUG()
#define SMC_insl(a, r, p, l)		BUG()
#define SMC_outsl(a, r, p, l)		BUG()
#endif

#if !defined(SMC_insl) || !defined(SMC_outsl)
#define SMC_insl(a, r, p, l)		BUG()
#define SMC_outsl(a, r, p, l)		BUG()
#endif

#if ! SMC_CAN_USE_16BIT

/*
 * Any 16-bit access is performed with two 8-bit accesses if the hardware
 * can't do it directly. Most registers are 16-bit so those are mandatory.
 */
#define SMC_outw(x, ioaddr, reg)					\
	do {								\
		unsigned int __val16 = (x);				\
		SMC_outb( __val16, ioaddr, reg );			\
		SMC_outb( __val16 >> 8, ioaddr, reg + (1 << SMC_IO_SHIFT));\
	} while (0)
#define SMC_inw(ioaddr, reg)						\
	({								\
		unsigned int __val16;					\
		__val16 =  SMC_inb( ioaddr, reg );			\
		__val16 |= SMC_inb( ioaddr, reg + (1 << SMC_IO_SHIFT)) << 8; \
		__val16;						\
	})

#define SMC_insw(a, r, p, l)		BUG()
#define SMC_outsw(a, r, p, l)		BUG()

#endif

#if !defined(SMC_insw) || !defined(SMC_outsw)
#define SMC_insw(a, r, p, l)		BUG()
#define SMC_outsw(a, r, p, l)		BUG()
#endif

#if ! SMC_CAN_USE_8BIT
#define SMC_inb(ioaddr, reg)		({ BUG(); 0; })
#define SMC_outb(x, ioaddr, reg)	BUG()
#define SMC_insb(a, r, p, l)		BUG()
#define SMC_outsb(a, r, p, l)		BUG()
#endif

#if !defined(SMC_insb) || !defined(SMC_outsb)
#define SMC_insb(a, r, p, l)		BUG()
#define SMC_outsb(a, r, p, l)		BUG()
#endif

#ifndef SMC_CAN_USE_DATACS
#define SMC_CAN_USE_DATACS	0
#endif

#ifndef SMC_IO_SHIFT
#define SMC_IO_SHIFT	0
#endif

#ifndef	SMC_IRQ_FLAGS
#define	SMC_IRQ_FLAGS		IRQF_TRIGGER_RISING
#endif

#ifndef SMC_INTERRUPT_PREAMBLE
#define SMC_INTERRUPT_PREAMBLE
#endif


/* Because of bank switching, the LAN91x uses only 16 I/O ports */
#define SMC_IO_EXTENT	(16 << SMC_IO_SHIFT)
#define SMC_DATA_EXTENT (4)

/*
 . Bank Select Register:
 .
 .		yyyy yyyy 0000 00xx
 .		xx 		= bank number
 .		yyyy yyyy	= 0x33, for identification purposes.
*/
#define BANK_SELECT		(14 << SMC_IO_SHIFT)


// Transmit Control Register
/* BANK 0  */
#define TCR_REG 	SMC_REG(0x0000, 0)
#define TCR_ENABLE	0x0001	// When 1 we can transmit
#define TCR_LOOP	0x0002	// Controls output pin LBK
#define TCR_FORCOL	0x0004	// When 1 will force a collision
#define TCR_PAD_EN	0x0080	// When 1 will pad tx frames < 64 bytes w/0
#define TCR_NOCRC	0x0100	// When 1 will not append CRC to tx frames
#define TCR_MON_CSN	0x0400	// When 1 tx monitors carrier
#define TCR_FDUPLX    	0x0800  // When 1 enables full duplex operation
#define TCR_STP_SQET	0x1000	// When 1 stops tx if Signal Quality Error
#define TCR_EPH_LOOP	0x2000	// When 1 enables EPH block loopback
#define TCR_SWFDUP	0x8000	// When 1 enables Switched Full Duplex mode

#define TCR_CLEAR	0	/* do NOTHING */
/* the default settings for the TCR register : */
#define TCR_DEFAULT	(TCR_ENABLE | TCR_PAD_EN)


// EPH Status Register
/* BANK 0  */
#define EPH_STATUS_REG	SMC_REG(0x0002, 0)
#define ES_TX_SUC	0x0001	// Last TX was successful
#define ES_SNGL_COL	0x0002	// Single collision detected for last tx
#define ES_MUL_COL	0x0004	// Multiple collisions detected for last tx
#define ES_LTX_MULT	0x0008	// Last tx was a multicast
#define ES_16COL	0x0010	// 16 Collisions Reached
#define ES_SQET		0x0020	// Signal Quality Error Test
#define ES_LTXBRD	0x0040	// Last tx was a broadcast
#define ES_TXDEFR	0x0080	// Transmit Deferred
#define ES_LATCOL	0x0200	// Late collision detected on last tx
#define ES_LOSTCARR	0x0400	// Lost Carrier Sense
#define ES_EXC_DEF	0x0800	// Excessive Deferral
#define ES_CTR_ROL	0x1000	// Counter Roll Over indication
#define ES_LINK_OK	0x4000	// Driven by inverted value of nLNK pin
#define ES_TXUNRN	0x8000	// Tx Underrun


// Receive Control Register
/* BANK 0  */
#define RCR_REG		SMC_REG(0x0004, 0)
#define RCR_RX_ABORT	0x0001	// Set if a rx frame was aborted
#define RCR_PRMS	0x0002	// Enable promiscuous mode
#define RCR_ALMUL	0x0004	// When set accepts all multicast frames
#define RCR_RXEN	0x0100	// IFF this is set, we can receive packets
#define RCR_STRIP_CRC	0x0200	// When set strips CRC from rx packets
#define RCR_ABORT_ENB	0x0200	// When set will abort rx on collision
#define RCR_FILT_CAR	0x0400	// When set filters leading 12 bit s of carrier
#define RCR_SOFTRST	0x8000 	// resets the chip

/* the normal settings for the RCR register : */
#define RCR_DEFAULT	(RCR_STRIP_CRC | RCR_RXEN)
#define RCR_CLEAR	0x0	// set it to a base state


// Counter Register
/* BANK 0  */
#define COUNTER_REG	SMC_REG(0x0006, 0)


// Memory Information Register
/* BANK 0  */
#define MIR_REG		SMC_REG(0x0008, 0)


// Receive/Phy Control Register
/* BANK 0  */
#define RPC_REG		SMC_REG(0x000A, 0)
#define RPC_SPEED	0x2000	// When 1 PHY is in 100Mbps mode.
#define RPC_DPLX	0x1000	// When 1 PHY is in Full-Duplex Mode
#define RPC_ANEG	0x0800	// When 1 PHY is in Auto-Negotiate Mode
#define RPC_LSXA_SHFT	5	// Bits to shift LS2A,LS1A,LS0A to lsb
#define RPC_LSXB_SHFT	2	// Bits to get LS2B,LS1B,LS0B to lsb
#define RPC_LED_100_10	(0x00)	// LED = 100Mbps OR's with 10Mbps link detect
#define RPC_LED_RES	(0x01)	// LED = Reserved
#define RPC_LED_10	(0x02)	// LED = 10Mbps link detect
#define RPC_LED_FD	(0x03)	// LED = Full Duplex Mode
#define RPC_LED_TX_RX	(0x04)	// LED = TX or RX packet occurred
#define RPC_LED_100	(0x05)	// LED = 100Mbps link dectect
#define RPC_LED_TX	(0x06)	// LED = TX packet occurred
#define RPC_LED_RX	(0x07)	// LED = RX packet occurred

#ifndef RPC_LSA_DEFAULT
#define RPC_LSA_DEFAULT	RPC_LED_100
#endif
#ifndef RPC_LSB_DEFAULT
#define RPC_LSB_DEFAULT RPC_LED_FD
#endif

#define RPC_DEFAULT (RPC_ANEG | (RPC_LSA_DEFAULT << RPC_LSXA_SHFT) | (RPC_LSB_DEFAULT << RPC_LSXB_SHFT) | RPC_SPEED | RPC_DPLX)


/* Bank 0 0x0C is reserved */

// Bank Select Register
/* All Banks */
#define BSR_REG		0x000E


// Configuration Reg
/* BANK 1 */
#define CONFIG_REG	SMC_REG(0x0000,	1)
#define CONFIG_EXT_PHY	0x0200	// 1=external MII, 0=internal Phy
#define CONFIG_GPCNTRL	0x0400	// Inverse value drives pin nCNTRL
#define CONFIG_NO_WAIT	0x1000	// When 1 no extra wait states on ISA bus
#define CONFIG_EPH_POWER_EN 0x8000 // When 0 EPH is placed into low power mode.

// Default is powered-up, Internal Phy, Wait States, and pin nCNTRL=low
#define CONFIG_DEFAULT	(CONFIG_EPH_POWER_EN)


// Base Address Register
/* BANK 1 */
#define BASE_REG	SMC_REG(0x0002, 1)


// Individual Address Registers
/* BANK 1 */
#define ADDR0_REG	SMC_REG(0x0004, 1)
#define ADDR1_REG	SMC_REG(0x0006, 1)
#define ADDR2_REG	SMC_REG(0x0008, 1)


// General Purpose Register
/* BANK 1 */
#define GP_REG		SMC_REG(0x000A, 1)


// Control Register
/* BANK 1 */
#define CTL_REG		SMC_REG(0x000C, 1)
#define CTL_RCV_BAD	0x4000 // When 1 bad CRC packets are received
#define CTL_AUTO_RELEASE 0x0800 // When 1 tx pages are released automatically
#define CTL_LE_ENABLE	0x0080 // When 1 enables Link Error interrupt
#define CTL_CR_ENABLE	0x0040 // When 1 enables Counter Rollover interrupt
#define CTL_TE_ENABLE	0x0020 // When 1 enables Transmit Error interrupt
#define CTL_EEPROM_SELECT 0x0004 // Controls EEPROM reload & store
#define CTL_RELOAD	0x0002 // When set reads EEPROM into registers
#define CTL_STORE	0x0001 // When set stores registers into EEPROM


// MMU Command Register
/* BANK 2 */
#define MMU_CMD_REG	SMC_REG(0x0000, 2)
#define MC_BUSY		1	// When 1 the last release has not completed
#define MC_NOP		(0<<5)	// No Op
#define MC_ALLOC	(1<<5) 	// OR with number of 256 byte packets
#define MC_RESET	(2<<5)	// Reset MMU to initial state
#define MC_REMOVE	(3<<5) 	// Remove the current rx packet
#define MC_RELEASE  	(4<<5) 	// Remove and release the current rx packet
#define MC_FREEPKT  	(5<<5) 	// Release packet in PNR register
#define MC_ENQUEUE	(6<<5)	// Enqueue the packet for transmit
#define MC_RSTTXFIFO	(7<<5)	// Reset the TX FIFOs


// Packet Number Register
/* BANK 2 */
#define PN_REG		SMC_REG(0x0002, 2)


// Allocation Result Register
/* BANK 2 */
#define AR_REG		SMC_REG(0x0003, 2)
#define AR_FAILED	0x80	// Alocation Failed


// TX FIFO Ports Register
/* BANK 2 */
#define TXFIFO_REG	SMC_REG(0x0004, 2)
#define TXFIFO_TEMPTY	0x80	// TX FIFO Empty

// RX FIFO Ports Register
/* BANK 2 */
#define RXFIFO_REG	SMC_REG(0x0005, 2)
#define RXFIFO_REMPTY	0x80	// RX FIFO Empty

#define FIFO_REG	SMC_REG(0x0004, 2)

// Pointer Register
/* BANK 2 */
#define PTR_REG		SMC_REG(0x0006, 2)
#define PTR_RCV		0x8000 // 1=Receive area, 0=Transmit area
#define PTR_AUTOINC 	0x4000 // Auto increment the pointer on each access
#define PTR_READ	0x2000 // When 1 the operation is a read


// Data Register
/* BANK 2 */
#define DATA_REG	SMC_REG(0x0008, 2)


// Interrupt Status/Acknowledge Register
/* BANK 2 */
#define INT_REG		SMC_REG(0x000C, 2)


// Interrupt Mask Register
/* BANK 2 */
#define IM_REG		SMC_REG(0x000D, 2)
#define IM_MDINT	0x80 // PHY MI Register 18 Interrupt
#define IM_ERCV_INT	0x40 // Early Receive Interrupt
#define IM_EPH_INT	0x20 // Set by Ethernet Protocol Handler section
#define IM_RX_OVRN_INT	0x10 // Set by Receiver Overruns
#define IM_ALLOC_INT	0x08 // Set when allocation request is completed
#define IM_TX_EMPTY_INT	0x04 // Set if the TX FIFO goes empty
#define IM_TX_INT	0x02 // Transmit Interrupt
#define IM_RCV_INT	0x01 // Receive Interrupt


// Multicast Table Registers
/* BANK 3 */
#define MCAST_REG1	SMC_REG(0x0000, 3)
#define MCAST_REG2	SMC_REG(0x0002, 3)
#define MCAST_REG3	SMC_REG(0x0004, 3)
#define MCAST_REG4	SMC_REG(0x0006, 3)


// Management Interface Register (MII)
/* BANK 3 */
#define MII_REG		SMC_REG(0x0008, 3)
#define MII_MSK_CRS100	0x4000 // Disables CRS100 detection during tx half dup
#define MII_MDOE	0x0008 // MII Output Enable
#define MII_MCLK	0x0004 // MII Clock, pin MDCLK
#define MII_MDI		0x0002 // MII Input, pin MDI
#define MII_MDO		0x0001 // MII Output, pin MDO


// Revision Register
/* BANK 3 */
/* ( hi: chip id   low: rev # ) */
#define REV_REG		SMC_REG(0x000A, 3)


// Early RCV Register
/* BANK 3 */
/* this is NOT on SMC9192 */
#define ERCV_REG	SMC_REG(0x000C, 3)
#define ERCV_RCV_DISCRD	0x0080 // When 1 discards a packet being received
#define ERCV_THRESHOLD	0x001F // ERCV Threshold Mask


// External Register
/* BANK 7 */
#define EXT_REG		SMC_REG(0x0000, 7)


#define CHIP_9192	3
#define CHIP_9194	4
#define CHIP_9195	5
#define CHIP_9196	6
#define CHIP_91100	7
#define CHIP_91100FD	8
#define CHIP_91111FD	9

static const char * chip_ids[ 16 ] =  {
	NULL, NULL, NULL,
	/* 3 */ "SMC91C90/91C92",
	/* 4 */ "SMC91C94",
	/* 5 */ "SMC91C95",
	/* 6 */ "SMC91C96",
	/* 7 */ "SMC91C100",
	/* 8 */ "SMC91C100FD",
	/* 9 */ "SMC91C11xFD",
	NULL, NULL, NULL,
	NULL, NULL, NULL};


/*
 . Receive status bits
*/
#define RS_ALGNERR	0x8000
#define RS_BRODCAST	0x4000
#define RS_BADCRC	0x2000
#define RS_ODDFRAME	0x1000
#define RS_TOOLONG	0x0800
#define RS_TOOSHORT	0x0400
#define RS_MULTICAST	0x0001
#define RS_ERRORS	(RS_ALGNERR | RS_BADCRC | RS_TOOLONG | RS_TOOSHORT)


/*
 * PHY IDs
 *  LAN83C183 == LAN91C111 Internal PHY
 */
#define PHY_LAN83C183	0x0016f840
#define PHY_LAN83C180	0x02821c50

/*
 * PHY Register Addresses (LAN91C111 Internal PHY)
 *
 * Generic PHY registers can be found in <linux/mii.h>
 *
 * These phy registers are specific to our on-board phy.
 */

// PHY Configuration Register 1
#define PHY_CFG1_REG		0x10
#define PHY_CFG1_LNKDIS		0x8000	// 1=Rx Link Detect Function disabled
#define PHY_CFG1_XMTDIS		0x4000	// 1=TP Transmitter Disabled
#define PHY_CFG1_XMTPDN		0x2000	// 1=TP Transmitter Powered Down
#define PHY_CFG1_BYPSCR		0x0400	// 1=Bypass scrambler/descrambler
#define PHY_CFG1_UNSCDS		0x0200	// 1=Unscramble Idle Reception Disable
#define PHY_CFG1_EQLZR		0x0100	// 1=Rx Equalizer Disabled
#define PHY_CFG1_CABLE		0x0080	// 1=STP(150ohm), 0=UTP(100ohm)
#define PHY_CFG1_RLVL0		0x0040	// 1=Rx Squelch level reduced by 4.5db
#define PHY_CFG1_TLVL_SHIFT	2	// Transmit Output Level Adjust
#define PHY_CFG1_TLVL_MASK	0x003C
#define PHY_CFG1_TRF_MASK	0x0003	// Transmitter Rise/Fall time


// PHY Configuration Register 2
#define PHY_CFG2_REG		0x11
#define PHY_CFG2_APOLDIS	0x0020	// 1=Auto Polarity Correction disabled
#define PHY_CFG2_JABDIS		0x0010	// 1=Jabber disabled
#define PHY_CFG2_MREG		0x0008	// 1=Multiple register access (MII mgt)
#define PHY_CFG2_INTMDIO	0x0004	// 1=Interrupt signaled with MDIO pulseo

// PHY Status Output (and Interrupt status) Register
#define PHY_INT_REG		0x12	// Status Output (Interrupt Status)
#define PHY_INT_INT		0x8000	// 1=bits have changed since last read
#define PHY_INT_LNKFAIL		0x4000	// 1=Link Not detected
#define PHY_INT_LOSSSYNC	0x2000	// 1=Descrambler has lost sync
#define PHY_INT_CWRD		0x1000	// 1=Invalid 4B5B code detected on rx
#define PHY_INT_SSD		0x0800	// 1=No Start Of Stream detected on rx
#define PHY_INT_ESD		0x0400	// 1=No End Of Stream detected on rx
#define PHY_INT_RPOL		0x0200	// 1=Reverse Polarity detected
#define PHY_INT_JAB		0x0100	// 1=Jabber detected
#define PHY_INT_SPDDET		0x0080	// 1=100Base-TX mode, 0=10Base-T mode
#define PHY_INT_DPLXDET		0x0040	// 1=Device in Full Duplex

// PHY Interrupt/Status Mask Register
#define PHY_MASK_REG		0x13	// Interrupt Mask
// Uses the same bit definitions as PHY_INT_REG


/*
 * SMC91C96 ethernet config and status registers.
 * These are in the "attribute" space.
 */
#define ECOR			0x8000
#define ECOR_RESET		0x80
#define ECOR_LEVEL_IRQ		0x40
#define ECOR_WR_ATTRIB		0x04
#define ECOR_ENABLE		0x01

#define ECSR			0x8002
#define ECSR_IOIS8		0x20
#define ECSR_PWRDWN		0x04
#define ECSR_INT		0x02

#define ATTRIB_SIZE		((64*1024) << SMC_IO_SHIFT)


/*
 * Macros to abstract register access according to the data bus
 * capabilities.  Please use those and not the in/out primitives.
 * Note: the following macros do *not* select the bank -- this must
 * be done separately as needed in the main code.  The SMC_REG() macro
 * only uses the bank argument for debugging purposes (when enabled).
 *
 * Note: despite inline functions being safer, everything leading to this
 * should preferably be macros to let BUG() display the line number in
 * the core source code since we're interested in the top call site
 * not in any inline function location.
 */

#if SMC_DEBUG > 0
#define SMC_REG(reg, bank)						\
	({								\
		int __b = SMC_CURRENT_BANK();				\
		if (unlikely((__b & ~0xf0) != (0x3300 | bank))) {	\
			printk( "%s: bank reg screwed (0x%04x)\n",	\
				CARDNAME, __b );			\
			BUG();						\
		}							\
		reg<<SMC_IO_SHIFT;					\
	})
#else
#define SMC_REG(reg, bank)	(reg<<SMC_IO_SHIFT)
#endif

/*
 * Hack Alert: Some setups just can't write 8 or 16 bits reliably when not
 * aligned to a 32 bit boundary.  I tell you that does exist!
 * Fortunately the affected register accesses can be easily worked around
 * since we can write zeroes to the preceeding 16 bits without adverse
 * effects and use a 32-bit access.
 *
 * Enforce it on any 32-bit capable setup for now.
 */
#define SMC_MUST_ALIGN_WRITE	SMC_CAN_USE_32BIT

#define SMC_GET_PN()							\
	( SMC_CAN_USE_8BIT	? (SMC_inb(ioaddr, PN_REG))		\
				: (SMC_inw(ioaddr, PN_REG) & 0xFF) )

#define SMC_SET_PN(x)							\
	do {								\
		if (SMC_MUST_ALIGN_WRITE)				\
			SMC_outl((x)<<16, ioaddr, SMC_REG(0, 2));	\
		else if (SMC_CAN_USE_8BIT)				\
			SMC_outb(x, ioaddr, PN_REG);			\
		else							\
			SMC_outw(x, ioaddr, PN_REG);			\
	} while (0)

#define SMC_GET_AR()							\
	( SMC_CAN_USE_8BIT	? (SMC_inb(ioaddr, AR_REG))		\
	  			: (SMC_inw(ioaddr, PN_REG) >> 8) )

#define SMC_GET_TXFIFO()						\
	( SMC_CAN_USE_8BIT	? (SMC_inb(ioaddr, TXFIFO_REG))		\
				: (SMC_inw(ioaddr, TXFIFO_REG) & 0xFF) )

#define SMC_GET_RXFIFO()						\
	  ( SMC_CAN_USE_8BIT	? (SMC_inb(ioaddr, RXFIFO_REG))		\
				: (SMC_inw(ioaddr, TXFIFO_REG) >> 8) )

#define SMC_GET_INT()							\
	( SMC_CAN_USE_8BIT	? (SMC_inb(ioaddr, INT_REG))		\
				: (SMC_inw(ioaddr, INT_REG) & 0xFF) )

#define SMC_ACK_INT(x)							\
	do {								\
		if (SMC_CAN_USE_8BIT)					\
			SMC_outb(x, ioaddr, INT_REG);			\
		else {							\
			unsigned long __flags;				\
			int __mask;					\
			local_irq_save(__flags);			\
			__mask = SMC_inw( ioaddr, INT_REG ) & ~0xff;	\
			SMC_outw( __mask | (x), ioaddr, INT_REG );	\
			local_irq_restore(__flags);			\
		}							\
	} while (0)

#define SMC_GET_INT_MASK()						\
	( SMC_CAN_USE_8BIT	? (SMC_inb(ioaddr, IM_REG))		\
				: (SMC_inw( ioaddr, INT_REG ) >> 8) )

#define SMC_SET_INT_MASK(x)						\
	do {								\
		if (SMC_CAN_USE_8BIT)					\
			SMC_outb(x, ioaddr, IM_REG);			\
		else							\
			SMC_outw((x) << 8, ioaddr, INT_REG);		\
	} while (0)

#define SMC_CURRENT_BANK()	SMC_inw(ioaddr, BANK_SELECT)

#define SMC_SELECT_BANK(x)						\
	do {								\
		if (SMC_MUST_ALIGN_WRITE)				\
			SMC_outl((x)<<16, ioaddr, 12<<SMC_IO_SHIFT);	\
		else							\
			SMC_outw(x, ioaddr, BANK_SELECT);		\
	} while (0)

#define SMC_GET_BASE()		SMC_inw(ioaddr, BASE_REG)

#define SMC_SET_BASE(x)		SMC_outw(x, ioaddr, BASE_REG)

#define SMC_GET_CONFIG()	SMC_inw(ioaddr, CONFIG_REG)

#define SMC_SET_CONFIG(x)	SMC_outw(x, ioaddr, CONFIG_REG)

#define SMC_GET_COUNTER()	SMC_inw(ioaddr, COUNTER_REG)

#define SMC_GET_CTL()		SMC_inw(ioaddr, CTL_REG)

#define SMC_SET_CTL(x)		SMC_outw(x, ioaddr, CTL_REG)

#define SMC_GET_MII()		SMC_inw(ioaddr, MII_REG)

#define SMC_SET_MII(x)		SMC_outw(x, ioaddr, MII_REG)

#define SMC_GET_MIR()		SMC_inw(ioaddr, MIR_REG)

#define SMC_SET_MIR(x)		SMC_outw(x, ioaddr, MIR_REG)

#define SMC_GET_MMU_CMD()	SMC_inw(ioaddr, MMU_CMD_REG)

#define SMC_SET_MMU_CMD(x)	SMC_outw(x, ioaddr, MMU_CMD_REG)

#define SMC_GET_FIFO()		SMC_inw(ioaddr, FIFO_REG)

#define SMC_GET_PTR()		SMC_inw(ioaddr, PTR_REG)

#define SMC_SET_PTR(x)							\
	do {								\
		if (SMC_MUST_ALIGN_WRITE)				\
			SMC_outl((x)<<16, ioaddr, SMC_REG(4, 2));	\
		else							\
			SMC_outw(x, ioaddr, PTR_REG);			\
	} while (0)

#define SMC_GET_EPH_STATUS()	SMC_inw(ioaddr, EPH_STATUS_REG)

#define SMC_GET_RCR()		SMC_inw(ioaddr, RCR_REG)

#define SMC_SET_RCR(x)		SMC_outw(x, ioaddr, RCR_REG)

#define SMC_GET_REV()		SMC_inw(ioaddr, REV_REG)

#define SMC_GET_RPC()		SMC_inw(ioaddr, RPC_REG)

#define SMC_SET_RPC(x)							\
	do {								\
		if (SMC_MUST_ALIGN_WRITE)				\
			SMC_outl((x)<<16, ioaddr, SMC_REG(8, 0));	\
		else							\
			SMC_outw(x, ioaddr, RPC_REG);			\
	} while (0)

#define SMC_GET_TCR()		SMC_inw(ioaddr, TCR_REG)

#define SMC_SET_TCR(x)		SMC_outw(x, ioaddr, TCR_REG)

#ifndef SMC_GET_MAC_ADDR
#define SMC_GET_MAC_ADDR(addr)						\
	do {								\
		unsigned int __v;					\
		__v = SMC_inw( ioaddr, ADDR0_REG );			\
		addr[0] = __v; addr[1] = __v >> 8;			\
		__v = SMC_inw( ioaddr, ADDR1_REG );			\
		addr[2] = __v; addr[3] = __v >> 8;			\
		__v = SMC_inw( ioaddr, ADDR2_REG );			\
		addr[4] = __v; addr[5] = __v >> 8;			\
	} while (0)
#endif

#define SMC_SET_MAC_ADDR(addr)						\
	do {								\
		SMC_outw( addr[0]|(addr[1] << 8), ioaddr, ADDR0_REG );	\
		SMC_outw( addr[2]|(addr[3] << 8), ioaddr, ADDR1_REG );	\
		SMC_outw( addr[4]|(addr[5] << 8), ioaddr, ADDR2_REG );	\
	} while (0)

#define SMC_SET_MCAST(x)						\
	do {								\
		const unsigned char *mt = (x);				\
		SMC_outw( mt[0] | (mt[1] << 8), ioaddr, MCAST_REG1 );	\
		SMC_outw( mt[2] | (mt[3] << 8), ioaddr, MCAST_REG2 );	\
		SMC_outw( mt[4] | (mt[5] << 8), ioaddr, MCAST_REG3 );	\
		SMC_outw( mt[6] | (mt[7] << 8), ioaddr, MCAST_REG4 );	\
	} while (0)

#define SMC_PUT_PKT_HDR(status, length)					\
	do {								\
		if (SMC_CAN_USE_32BIT)					\
			SMC_outl((status) | (length)<<16, ioaddr, DATA_REG); \
		else {							\
			SMC_outw(status, ioaddr, DATA_REG);		\
			SMC_outw(length, ioaddr, DATA_REG);		\
		}							\
	} while (0)

#define SMC_GET_PKT_HDR(status, length)					\
	do {								\
		if (SMC_CAN_USE_32BIT) {				\
			unsigned int __val = SMC_inl(ioaddr, DATA_REG);	\
			(status) = __val & 0xffff;			\
			(length) = __val >> 16;				\
		} else {						\
			(status) = SMC_inw(ioaddr, DATA_REG);		\
			(length) = SMC_inw(ioaddr, DATA_REG);		\
		}							\
	} while (0)

#define SMC_PUSH_DATA(p, l)						\
	do {								\
		if (SMC_CAN_USE_32BIT) {				\
			void *__ptr = (p);				\
			int __len = (l);				\
			void *__ioaddr = ioaddr;			\
			if (__len >= 2 && (unsigned long)__ptr & 2) {	\
				__len -= 2;				\
				SMC_outw(*(u16 *)__ptr, ioaddr, DATA_REG); \
				__ptr += 2;				\
			}						\
			if (SMC_CAN_USE_DATACS && lp->datacs)		\
				__ioaddr = lp->datacs;			\
			SMC_outsl(__ioaddr, DATA_REG, __ptr, __len>>2);	\
			if (__len & 2) {				\
				__ptr += (__len & ~3);			\
				SMC_outw(*((u16 *)__ptr), ioaddr, DATA_REG); \
			}						\
		} else if (SMC_CAN_USE_16BIT)				\
			SMC_outsw(ioaddr, DATA_REG, p, (l) >> 1);	\
		else if (SMC_CAN_USE_8BIT)				\
			SMC_outsb(ioaddr, DATA_REG, p, l);		\
	} while (0)

#define SMC_PULL_DATA(p, l)						\
	do {								\
		if (SMC_CAN_USE_32BIT) {				\
			void *__ptr = (p);				\
			int __len = (l);				\
			void *__ioaddr = ioaddr;			\
			if ((unsigned long)__ptr & 2) {			\
				/*					\
				 * We want 32bit alignment here.	\
				 * Since some buses perform a full	\
				 * 32bit fetch even for 16bit data	\
				 * we can't use SMC_inw() here.		\
				 * Back both source (on-chip) and	\
				 * destination pointers of 2 bytes.	\
				 * This is possible since the call to	\
				 * SMC_GET_PKT_HDR() already advanced	\
				 * the source pointer of 4 bytes, and	\
				 * the skb_reserve(skb, 2) advanced	\
				 * the destination pointer of 2 bytes.	\
				 */					\
				__ptr -= 2;				\
				__len += 2;				\
				SMC_SET_PTR(2|PTR_READ|PTR_RCV|PTR_AUTOINC); \
			}						\
			if (SMC_CAN_USE_DATACS && lp->datacs)		\
				__ioaddr = lp->datacs;			\
			__len += 2;					\
			SMC_insl(__ioaddr, DATA_REG, __ptr, __len>>2);	\
		} else if (SMC_CAN_USE_16BIT)				\
			SMC_insw(ioaddr, DATA_REG, p, (l) >> 1);	\
		else if (SMC_CAN_USE_8BIT)				\
			SMC_insb(ioaddr, DATA_REG, p, l);		\
	} while (0)

#endif  /* _SMC91X_H_ */
