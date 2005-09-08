/*
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 2001 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 2000 SiByte, Inc.
 * Copyright (C) 2005 Thiemo Seufer
 *
 * Written by Justin Carlson of SiByte, Inc.
 *         and Kip Walker of Broadcom Corp.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include <asm/io.h>
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_dma.h>

#ifdef CONFIG_SB1_PASS_1_WORKAROUNDS
#define SB1_PREF_LOAD_STREAMED_HINT "0"
#define SB1_PREF_STORE_STREAMED_HINT "1"
#else
#define SB1_PREF_LOAD_STREAMED_HINT "4"
#define SB1_PREF_STORE_STREAMED_HINT "5"
#endif

static inline void clear_page_cpu(void *page)
{
	unsigned char *addr = (unsigned char *) page;
	unsigned char *end = addr + PAGE_SIZE;

	/*
	 * JDCXXX - This should be bottlenecked by the write buffer, but these
	 * things tend to be mildly unpredictable...should check this on the
	 * performance model
	 *
	 * We prefetch 4 lines ahead.  We're also "cheating" slightly here...
	 * since we know we're on an SB1, we force the assembler to take
	 * 64-bit operands to speed things up
	 */
	__asm__ __volatile__(
	"	.set	push		\n"
	"	.set	mips4		\n"
	"	.set	noreorder	\n"
#ifdef CONFIG_CPU_HAS_PREFETCH
	"	daddiu	%0, %0, 128	\n"
	"	pref	" SB1_PREF_STORE_STREAMED_HINT ", -128(%0)  \n"  /* Prefetch the first 4 lines */
	"	pref	" SB1_PREF_STORE_STREAMED_HINT ",  -96(%0)  \n"
	"	pref	" SB1_PREF_STORE_STREAMED_HINT ",  -64(%0)  \n"
	"	pref	" SB1_PREF_STORE_STREAMED_HINT ",  -32(%0)  \n"
	"1:	sd	$0, -128(%0)	\n"  /* Throw out a cacheline of 0's */
	"	sd	$0, -120(%0)	\n"
	"	sd	$0, -112(%0)	\n"
	"	sd	$0, -104(%0)	\n"
	"	daddiu	%0, %0, 32	\n"
	"	bnel	%0, %1, 1b	\n"
	"	 pref	" SB1_PREF_STORE_STREAMED_HINT ",  -32(%0)  \n"
	"	daddiu	%0, %0, -128	\n"
#endif
	"	sd	$0, 0(%0)	\n"  /* Throw out a cacheline of 0's */
	"1:	sd	$0, 8(%0)	\n"
	"	sd	$0, 16(%0)	\n"
	"	sd	$0, 24(%0)	\n"
	"	daddiu	%0, %0, 32	\n"
	"	bnel	%0, %1, 1b	\n"
	"	 sd	$0, 0(%0)	\n"
	"	.set	pop		\n"
	: "+r" (addr)
	: "r" (end)
	: "memory");
}

static inline void copy_page_cpu(void *to, void *from)
{
	unsigned char *src = (unsigned char *)from;
	unsigned char *dst = (unsigned char *)to;
	unsigned char *end = src + PAGE_SIZE;

	/*
	 * The pref's used here are using "streaming" hints, which cause the
	 * copied data to be kicked out of the cache sooner.  A page copy often
	 * ends up copying a lot more data than is commonly used, so this seems
	 * to make sense in terms of reducing cache pollution, but I've no real
	 * performance data to back this up
	 */
	__asm__ __volatile__(
	"	.set	push		\n"
	"	.set	mips4		\n"
	"	.set	noreorder	\n"
#ifdef CONFIG_CPU_HAS_PREFETCH
	"	daddiu	%0, %0, 128	\n"
	"	daddiu	%1, %1, 128	\n"
	"	pref	" SB1_PREF_LOAD_STREAMED_HINT  ", -128(%0)\n"  /* Prefetch the first 4 lines */
	"	pref	" SB1_PREF_STORE_STREAMED_HINT ", -128(%1)\n"
	"	pref	" SB1_PREF_LOAD_STREAMED_HINT  ",  -96(%0)\n"
	"	pref	" SB1_PREF_STORE_STREAMED_HINT ",  -96(%1)\n"
	"	pref	" SB1_PREF_LOAD_STREAMED_HINT  ",  -64(%0)\n"
	"	pref	" SB1_PREF_STORE_STREAMED_HINT ",  -64(%1)\n"
	"	pref	" SB1_PREF_LOAD_STREAMED_HINT  ",  -32(%0)\n"
	"1:	pref	" SB1_PREF_STORE_STREAMED_HINT ",  -32(%1)\n"
# ifdef CONFIG_64BIT
	"	ld	$8, -128(%0)	\n"  /* Block copy a cacheline */
	"	ld	$9, -120(%0)	\n"
	"	ld	$10, -112(%0)	\n"
	"	ld	$11, -104(%0)	\n"
	"	sd	$8, -128(%1)	\n"
	"	sd	$9, -120(%1)	\n"
	"	sd	$10, -112(%1)	\n"
	"	sd	$11, -104(%1)	\n"
# else
	"	lw	$2, -128(%0)	\n"  /* Block copy a cacheline */
	"	lw	$3, -124(%0)	\n"
	"	lw	$6, -120(%0)	\n"
	"	lw	$7, -116(%0)	\n"
	"	lw	$8, -112(%0)	\n"
	"	lw	$9, -108(%0)	\n"
	"	lw	$10, -104(%0)	\n"
	"	lw	$11, -100(%0)	\n"
	"	sw	$2, -128(%1)	\n"
	"	sw	$3, -124(%1)	\n"
	"	sw	$6, -120(%1)	\n"
	"	sw	$7, -116(%1)	\n"
	"	sw	$8, -112(%1)	\n"
	"	sw	$9, -108(%1)	\n"
	"	sw	$10, -104(%1)	\n"
	"	sw	$11, -100(%1)	\n"
# endif
	"	daddiu	%0, %0, 32	\n"
	"	daddiu	%1, %1, 32	\n"
	"	bnel	%0, %2, 1b	\n"
	"	 pref	" SB1_PREF_LOAD_STREAMED_HINT  ",  -32(%0)\n"
	"	daddiu	%0, %0, -128	\n"
	"	daddiu	%1, %1, -128	\n"
#endif
#ifdef CONFIG_64BIT
	"	ld	$8, 0(%0)	\n"  /* Block copy a cacheline */
	"1:	ld	$9, 8(%0)	\n"
	"	ld	$10, 16(%0)	\n"
	"	ld	$11, 24(%0)	\n"
	"	sd	$8, 0(%1)	\n"
	"	sd	$9, 8(%1)	\n"
	"	sd	$10, 16(%1)	\n"
	"	sd	$11, 24(%1)	\n"
#else
	"	lw	$2, 0(%0)	\n"  /* Block copy a cacheline */
	"1:	lw	$3, 4(%0)	\n"
	"	lw	$6, 8(%0)	\n"
	"	lw	$7, 12(%0)	\n"
	"	lw	$8, 16(%0)	\n"
	"	lw	$9, 20(%0)	\n"
	"	lw	$10, 24(%0)	\n"
	"	lw	$11, 28(%0)	\n"
	"	sw	$2, 0(%1)	\n"
	"	sw	$3, 4(%1)	\n"
	"	sw	$6, 8(%1)	\n"
	"	sw	$7, 12(%1)	\n"
	"	sw	$8, 16(%1)	\n"
	"	sw	$9, 20(%1)	\n"
	"	sw	$10, 24(%1)	\n"
	"	sw	$11, 28(%1)	\n"
#endif
	"	daddiu	%0, %0, 32	\n"
	"	daddiu	%1, %1, 32	\n"
	"	bnel	%0, %2, 1b	\n"
#ifdef CONFIG_64BIT
	"	 ld	$8, 0(%0)	\n"
#else
	"	 lw	$2, 0(%0)	\n"
#endif
	"	.set	pop		\n"
	: "+r" (src), "+r" (dst)
	: "r" (end)
#ifdef CONFIG_64BIT
	: "$8","$9","$10","$11","memory");
#else
	: "$2","$3","$6","$7","$8","$9","$10","$11","memory");
#endif
}


#ifdef CONFIG_SIBYTE_DMA_PAGEOPS

/*
 * Pad descriptors to cacheline, since each is exclusively owned by a
 * particular CPU.
 */
typedef struct dmadscr_s {
	u64 dscr_a;
	u64 dscr_b;
	u64 pad_a;
	u64 pad_b;
} dmadscr_t;

static dmadscr_t page_descr[NR_CPUS] __attribute__((aligned(SMP_CACHE_BYTES)));

void sb1_dma_init(void)
{
	int cpu = smp_processor_id();
	u64 base_val = CPHYSADDR(&page_descr[cpu]) | V_DM_DSCR_BASE_RINGSZ(1);

	bus_writeq(base_val,
		   (void *)IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE)));
	bus_writeq(base_val | M_DM_DSCR_BASE_RESET,
		   (void *)IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE)));
	bus_writeq(base_val | M_DM_DSCR_BASE_ENABL,
		   (void *)IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE)));
}

void clear_page(void *page)
{
	int cpu = smp_processor_id();

	/* if the page is above Kseg0, use old way */
	if ((long)KSEGX(page) != (long)CKSEG0)
		return clear_page_cpu(page);

	page_descr[cpu].dscr_a = CPHYSADDR(page) | M_DM_DSCRA_ZERO_MEM | M_DM_DSCRA_L2C_DEST | M_DM_DSCRA_INTERRUPT;
	page_descr[cpu].dscr_b = V_DM_DSCRB_SRC_LENGTH(PAGE_SIZE);
	bus_writeq(1, (void *)IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_COUNT)));

	/*
	 * Don't really want to do it this way, but there's no
	 * reliable way to delay completion detection.
	 */
	while (!(bus_readq((void *)(IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE_DEBUG)) &
			   M_DM_DSCR_BASE_INTERRUPT))))
		;
	bus_readq((void *)IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE)));
}

void copy_page(void *to, void *from)
{
	unsigned long from_phys = CPHYSADDR(from);
	unsigned long to_phys = CPHYSADDR(to);
	int cpu = smp_processor_id();

	/* if either page is above Kseg0, use old way */
	if ((long)KSEGX(to) != (long)CKSEG0
	    || (long)KSEGX(from) != (long)CKSEG0)
		return copy_page_cpu(to, from);

	page_descr[cpu].dscr_a = CPHYSADDR(to_phys) | M_DM_DSCRA_L2C_DEST | M_DM_DSCRA_INTERRUPT;
	page_descr[cpu].dscr_b = CPHYSADDR(from_phys) | V_DM_DSCRB_SRC_LENGTH(PAGE_SIZE);
	bus_writeq(1, (void *)IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_COUNT)));

	/*
	 * Don't really want to do it this way, but there's no
	 * reliable way to delay completion detection.
	 */
	while (!(bus_readq((void *)(IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE_DEBUG)) &
				    M_DM_DSCR_BASE_INTERRUPT))))
		;
	bus_readq((void *)IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE)));
}

#else /* !CONFIG_SIBYTE_DMA_PAGEOPS */

void clear_page(void *page)
{
	return clear_page_cpu(page);
}

void copy_page(void *to, void *from)
{
	return copy_page_cpu(to, from);
}

#endif /* !CONFIG_SIBYTE_DMA_PAGEOPS */

EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(copy_page);
