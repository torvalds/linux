#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/mm.h>

#include <asm/memory.h>
#include <asm/ptrace.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>

extern struct cpu_cache_fns blk_cache_fns;

#define HARVARD_CACHE

/*
 *	blk_flush_kern_dcache_page(kaddr)
 *
 *	Ensure that the data held in the page kaddr is written back
 *	to the page in question.
 *
 *	- kaddr   - kernel address (guaranteed to be page aligned)
 */
static void __attribute__((naked))
blk_flush_kern_dcache_page(void *kaddr)
{
	asm(
	"add	r1, r0, %0							\n\
1:	.word	0xec401f0e	@ mcrr	p15, 0, r0, r1, c14, 0	@ blocking	\n\
	mov	r0, #0								\n\
	mcr	p15, 0, r0, c7, c5, 0						\n\
	mcr	p15, 0, r0, c7, c10, 4						\n\
	mov	pc, lr"
	:
	: "I" (PAGE_SIZE));
}

/*
 *	blk_dma_inv_range(start,end)
 *
 *	Invalidate the data cache within the specified region; we will
 *	be performing a DMA operation in this region and we want to
 *	purge old data in the cache.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
static void __attribute__((naked))
blk_dma_inv_range_unified(unsigned long start, unsigned long end)
{
	asm(
	"tst	r0, %0								\n\
	mcrne	p15, 0, r0, c7, c11, 1		@ clean unified line		\n\
	tst	r1, %0								\n\
	mcrne	p15, 0, r1, c7, c15, 1		@ clean & invalidate unified line\n\
	.word	0xec401f06	@ mcrr	p15, 0, r1, r0, c6, 0	@ blocking	\n\
	mov	r0, #0								\n\
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer		\n\
	mov	pc, lr"
	:
	: "I" (L1_CACHE_BYTES - 1));
}

static void __attribute__((naked))
blk_dma_inv_range_harvard(unsigned long start, unsigned long end)
{
	asm(
	"tst	r0, %0								\n\
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D line			\n\
	tst	r1, %0								\n\
	mcrne	p15, 0, r1, c7, c14, 1		@ clean & invalidate D line	\n\
	.word	0xec401f06	@ mcrr	p15, 0, r1, r0, c6, 0	@ blocking	\n\
	mov	r0, #0								\n\
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer		\n\
	mov	pc, lr"
	:
	: "I" (L1_CACHE_BYTES - 1));
}

/*
 *	blk_dma_clean_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
static void __attribute__((naked))
blk_dma_clean_range(unsigned long start, unsigned long end)
{
	asm(
	".word	0xec401f0c	@ mcrr	p15, 0, r1, r0, c12, 0	@ blocking	\n\
	mov	r0, #0								\n\
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer		\n\
	mov	pc, lr");
}

/*
 *	blk_dma_flush_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
static void __attribute__((naked))
blk_dma_flush_range(unsigned long start, unsigned long end)
{
	asm(
	".word	0xec401f0e	@ mcrr	p15, 0, r1, r0, c14, 0	@ blocking	\n\
	mov	pc, lr");
}

static int blockops_trap(struct pt_regs *regs, unsigned int instr)
{
	regs->ARM_r4 |= regs->ARM_r2;
	regs->ARM_pc += 4;
	return 0;
}

static char *func[] = {
	"Prefetch data range",
	"Clean+Invalidate data range",
	"Clean data range",
	"Invalidate data range",
	"Invalidate instr range"
};

static struct undef_hook blockops_hook __initdata = {
	.instr_mask	= 0x0fffffd0,
	.instr_val	= 0x0c401f00,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= 0,
	.fn		= blockops_trap,
};

static int __init blockops_check(void)
{
	register unsigned int err asm("r4") = 0;
	unsigned int err_pos = 1;
	unsigned int cache_type;
	int i;

	asm("mrc p15, 0, %0, c0, c0, 1" : "=r" (cache_type));

	printk("Checking V6 block cache operations:\n");
	register_undef_hook(&blockops_hook);

	__asm__ ("mov	r0, %0\n\t"
		"mov	r1, %1\n\t"
		"mov	r2, #1\n\t"
		".word	0xec401f2c @ mcrr p15, 0, r1, r0, c12, 2\n\t"
		"mov	r2, #2\n\t"
		".word	0xec401f0e @ mcrr p15, 0, r1, r0, c14, 0\n\t"
		"mov	r2, #4\n\t"
		".word	0xec401f0c @ mcrr p15, 0, r1, r0, c12, 0\n\t"
		"mov	r2, #8\n\t"
		".word	0xec401f06 @ mcrr p15, 0, r1, r0, c6, 0\n\t"
		"mov	r2, #16\n\t"
		".word	0xec401f05 @ mcrr p15, 0, r1, r0, c5, 0\n\t"
		:
		: "r" (PAGE_OFFSET), "r" (PAGE_OFFSET + 128)
		: "r0", "r1", "r2");

	unregister_undef_hook(&blockops_hook);

	for (i = 0; i < ARRAY_SIZE(func); i++, err_pos <<= 1)
		printk("%30s: %ssupported\n", func[i], err & err_pos ? "not " : "");

	if ((err & 8) == 0) {
		printk(" --> Using %s block cache invalidate\n",
			cache_type & (1 << 24) ? "harvard" : "unified");
		if (cache_type & (1 << 24))
			cpu_cache.dma_inv_range = blk_dma_inv_range_harvard;
		else
			cpu_cache.dma_inv_range = blk_dma_inv_range_unified;
	}
	if ((err & 4) == 0) {
		printk(" --> Using block cache clean\n");
		cpu_cache.dma_clean_range        = blk_dma_clean_range;
	}
	if ((err & 2) == 0) {
		printk(" --> Using block cache clean+invalidate\n");
		cpu_cache.dma_flush_range        = blk_dma_flush_range;
		cpu_cache.flush_kern_dcache_page = blk_flush_kern_dcache_page;
	}

	return 0;
}

__initcall(blockops_check);
