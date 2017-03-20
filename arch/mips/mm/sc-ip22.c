/*
 * sc-ip22.c: Indy cache management functions.
 *
 * Copyright (C) 1997, 2001 Ralf Baechle (ralf@gnu.org),
 * derived from r4xx0.c by David S. Miller (davem@davemloft.net).
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/bcache.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/sgi/ip22.h>
#include <asm/sgi/mc.h>

/* Secondary cache size in bytes, if present.  */
static unsigned long scache_size;

#undef DEBUG_CACHE

#define SC_SIZE 0x00080000
#define SC_LINE 32
#define CI_MASK (SC_SIZE - SC_LINE)
#define SC_INDEX(n) ((n) & CI_MASK)

static inline void indy_sc_wipe(unsigned long first, unsigned long last)
{
	unsigned long tmp;

	__asm__ __volatile__(
	"	.set	push			# indy_sc_wipe		\n"
	"	.set	noreorder					\n"
	"	.set	mips3						\n"
	"	.set	noat						\n"
	"	mfc0	%2, $12						\n"
	"	li	$1, 0x80		# Go 64 bit		\n"
	"	mtc0	$1, $12						\n"
	"								\n"
	"	#							\n"
	"	# Open code a dli $1, 0x9000000080000000		\n"
	"	#							\n"
	"	# Required because binutils 2.25 will happily accept	\n"
	"	# 64 bit instructions in .set mips3 mode but puke on	\n"
	"	# 64 bit constants when generating 32 bit ELF		\n"
	"	#							\n"
	"	lui	$1,0x9000					\n"
	"	dsll	$1,$1,0x10					\n"
	"	ori	$1,$1,0x8000					\n"
	"	dsll	$1,$1,0x10					\n"
	"								\n"
	"	or	%0, $1			# first line to flush	\n"
	"	or	%1, $1			# last line to flush	\n"
	"	.set	at						\n"
	"								\n"
	"1:	sw	$0, 0(%0)					\n"
	"	bne	%0, %1, 1b					\n"
	"	 daddu	%0, 32						\n"
	"								\n"
	"	mtc0	%2, $12			# Back to 32 bit	\n"
	"	nop				# pipeline hazard	\n"
	"	nop							\n"
	"	nop							\n"
	"	nop							\n"
	"	.set	pop						\n"
	: "=r" (first), "=r" (last), "=&r" (tmp)
	: "0" (first), "1" (last));
}

static void indy_sc_wback_invalidate(unsigned long addr, unsigned long size)
{
	unsigned long first_line, last_line;
	unsigned long flags;

#ifdef DEBUG_CACHE
	printk("indy_sc_wback_invalidate[%08lx,%08lx]", addr, size);
#endif

	/* Catch bad driver code */
	BUG_ON(size == 0);

	/* Which lines to flush?  */
	first_line = SC_INDEX(addr);
	last_line = SC_INDEX(addr + size - 1);

	local_irq_save(flags);
	if (first_line <= last_line) {
		indy_sc_wipe(first_line, last_line);
		goto out;
	}

	indy_sc_wipe(first_line, SC_SIZE - SC_LINE);
	indy_sc_wipe(0, last_line);
out:
	local_irq_restore(flags);
}

static void indy_sc_enable(void)
{
	unsigned long addr, tmp1, tmp2;

	/* This is really cool... */
#ifdef DEBUG_CACHE
	printk("Enabling R4600 SCACHE\n");
#endif
	__asm__ __volatile__(
	".set\tpush\n\t"
	".set\tnoreorder\n\t"
	".set\tmips3\n\t"
	"mfc0\t%2, $12\n\t"
	"nop; nop; nop; nop;\n\t"
	"li\t%1, 0x80\n\t"
	"mtc0\t%1, $12\n\t"
	"nop; nop; nop; nop;\n\t"
	"li\t%0, 0x1\n\t"
	"dsll\t%0, 31\n\t"
	"lui\t%1, 0x9000\n\t"
	"dsll32\t%1, 0\n\t"
	"or\t%0, %1, %0\n\t"
	"sb\t$0, 0(%0)\n\t"
	"mtc0\t$0, $12\n\t"
	"nop; nop; nop; nop;\n\t"
	"mtc0\t%2, $12\n\t"
	"nop; nop; nop; nop;\n\t"
	".set\tpop"
	: "=r" (tmp1), "=r" (tmp2), "=r" (addr));
}

static void indy_sc_disable(void)
{
	unsigned long tmp1, tmp2, tmp3;

#ifdef DEBUG_CACHE
	printk("Disabling R4600 SCACHE\n");
#endif
	__asm__ __volatile__(
	".set\tpush\n\t"
	".set\tnoreorder\n\t"
	".set\tmips3\n\t"
	"li\t%0, 0x1\n\t"
	"dsll\t%0, 31\n\t"
	"lui\t%1, 0x9000\n\t"
	"dsll32\t%1, 0\n\t"
	"or\t%0, %1, %0\n\t"
	"mfc0\t%2, $12\n\t"
	"nop; nop; nop; nop\n\t"
	"li\t%1, 0x80\n\t"
	"mtc0\t%1, $12\n\t"
	"nop; nop; nop; nop\n\t"
	"sh\t$0, 0(%0)\n\t"
	"mtc0\t$0, $12\n\t"
	"nop; nop; nop; nop\n\t"
	"mtc0\t%2, $12\n\t"
	"nop; nop; nop; nop\n\t"
	".set\tpop"
	: "=r" (tmp1), "=r" (tmp2), "=r" (tmp3));
}

static inline int __init indy_sc_probe(void)
{
	unsigned int size = ip22_eeprom_read(&sgimc->eeprom, 17);
	if (size == 0)
		return 0;

	size <<= PAGE_SHIFT;
	printk(KERN_INFO "R4600/R5000 SCACHE size %dK, linesize 32 bytes.\n",
	       size >> 10);
	scache_size = size;

	return 1;
}

/* XXX Check with wje if the Indy caches can differentiate between
   writeback + invalidate and just invalidate.	*/
static struct bcache_ops indy_sc_ops = {
	.bc_enable = indy_sc_enable,
	.bc_disable = indy_sc_disable,
	.bc_wback_inv = indy_sc_wback_invalidate,
	.bc_inv = indy_sc_wback_invalidate
};

void indy_sc_init(void)
{
	if (indy_sc_probe()) {
		indy_sc_enable();
		bcops = &indy_sc_ops;
	}
}
