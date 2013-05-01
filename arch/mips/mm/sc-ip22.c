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
	".set\tpush\t\t\t# indy_sc_wipe\n\t"
	".set\tnoreorder\n\t"
	".set\tmips3\n\t"
	".set\tnoat\n\t"
	"mfc0\t%2, $12\n\t"
	"li\t$1, 0x80\t\t\t# Go 64 bit\n\t"
	"mtc0\t$1, $12\n\t"

	"dli\t$1, 0x9000000080000000\n\t"
	"or\t%0, $1\t\t\t# first line to flush\n\t"
	"or\t%1, $1\t\t\t# last line to flush\n\t"
	".set\tat\n\t"

	"1:\tsw\t$0, 0(%0)\n\t"
	"bne\t%0, %1, 1b\n\t"
	" daddu\t%0, 32\n\t"

	"mtc0\t%2, $12\t\t\t# Back to 32 bit\n\t"
	"nop; nop; nop; nop;\n\t"
	".set\tpop"
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

/* XXX Check with wje if the Indy caches can differenciate between
   writeback + invalidate and just invalidate.	*/
static struct bcache_ops indy_sc_ops = {
	.bc_enable = indy_sc_enable,
	.bc_disable = indy_sc_disable,
	.bc_wback_inv = indy_sc_wback_invalidate,
	.bc_inv = indy_sc_wback_invalidate
};

void __cpuinit indy_sc_init(void)
{
	if (indy_sc_probe()) {
		indy_sc_enable();
		bcops = &indy_sc_ops;
	}
}
