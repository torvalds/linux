/*
 * ARC700 VIPT Cache Management
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  vineetg: May 2011: for Non-aliasing VIPT D-cache following can be NOPs
 *   -flush_cache_dup_mm (fork)
 *   -likewise for flush_cache_mm (exit/execve)
 *   -likewise for flush_cache_range,flush_cache_page (munmap, exit, COW-break)
 *
 * vineetg: Apr 2011
 *  -Now that MMU can support larger pg sz (16K), the determiniation of
 *   aliasing shd not be based on assumption of 8k pg
 *
 * vineetg: Mar 2011
 *  -optimised version of flush_icache_range( ) for making I/D coherent
 *   when vaddr is available (agnostic of num of aliases)
 *
 * vineetg: Mar 2011
 *  -Added documentation about I-cache aliasing on ARC700 and the way it
 *   was handled up until MMU V2.
 *  -Spotted a three year old bug when killing the 4 aliases, which needs
 *   bottom 2 bits, so we need to do paddr | {0x00, 0x01, 0x02, 0x03}
 *                        instead of paddr | {0x00, 0x01, 0x10, 0x11}
 *   (Rajesh you owe me one now)
 *
 * vineetg: Dec 2010
 *  -Off-by-one error when computing num_of_lines to flush
 *   This broke signal handling with bionic which uses synthetic sigret stub
 *
 * vineetg: Mar 2010
 *  -GCC can't generate ZOL for core cache flush loops.
 *   Conv them into iterations based as opposed to while (start < end) types
 *
 * Vineetg: July 2009
 *  -In I-cache flush routine we used to chk for aliasing for every line INV.
 *   Instead now we setup routines per cache geometry and invoke them
 *   via function pointers.
 *
 * Vineetg: Jan 2009
 *  -Cache Line flush routines used to flush an extra line beyond end addr
 *   because check was while (end >= start) instead of (end > start)
 *     =Some call sites had to work around by doing -1, -4 etc to end param
 *     =Some callers didnt care. This was spec bad in case of INV routines
 *      which would discard valid data (cause of the horrible ext2 bug
 *      in ARC IDE driver)
 *
 * vineetg: June 11th 2008: Fixed flush_icache_range( )
 *  -Since ARC700 caches are not coherent (I$ doesnt snoop D$) both need
 *   to be flushed, which it was not doing.
 *  -load_module( ) passes vmalloc addr (Kernel Virtual Addr) to the API,
 *   however ARC cache maintenance OPs require PHY addr. Thus need to do
 *   vmalloc_to_phy.
 *  -Also added optimisation there, that for range > PAGE SIZE we flush the
 *   entire cache in one shot rather than line by line. For e.g. a module
 *   with Code sz 600k, old code flushed 600k worth of cache (line-by-line),
 *   while cache is only 16 or 32k.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/cache.h>
#include <linux/mmu_context.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/cachectl.h>
#include <asm/setup.h>

char *arc_cache_mumbojumbo(int cpu_id, char *buf, int len)
{
	int n = 0;
	unsigned int c = smp_processor_id();

#define PR_CACHE(p, enb, str)						\
{									\
	if (!(p)->ver)							\
		n += scnprintf(buf + n, len - n, str"\t\t: N/A\n");	\
	else								\
		n += scnprintf(buf + n, len - n,			\
			str"\t\t: (%uK) VIPT, %dway set-asc, %ub Line %s\n", \
			TO_KB((p)->sz), (p)->assoc, (p)->line_len,	\
			enb ?  "" : "DISABLED (kernel-build)");		\
}

	PR_CACHE(&cpuinfo_arc700[c].icache, __CONFIG_ARC_HAS_ICACHE, "I-Cache");
	PR_CACHE(&cpuinfo_arc700[c].dcache, __CONFIG_ARC_HAS_DCACHE, "D-Cache");

	return buf;
}

/*
 * Read the Cache Build Confuration Registers, Decode them and save into
 * the cpuinfo structure for later use.
 * No Validation done here, simply read/convert the BCRs
 */
void __cpuinit read_decode_cache_bcr(void)
{
	struct bcr_cache ibcr, dbcr;
	struct cpuinfo_arc_cache *p_ic, *p_dc;
	unsigned int cpu = smp_processor_id();

	p_ic = &cpuinfo_arc700[cpu].icache;
	READ_BCR(ARC_REG_IC_BCR, ibcr);

	if (ibcr.config == 0x3)
		p_ic->assoc = 2;
	p_ic->line_len = 8 << ibcr.line_len;
	p_ic->sz = 0x200 << ibcr.sz;
	p_ic->ver = ibcr.ver;

	p_dc = &cpuinfo_arc700[cpu].dcache;
	READ_BCR(ARC_REG_DC_BCR, dbcr);

	if (dbcr.config == 0x2)
		p_dc->assoc = 4;
	p_dc->line_len = 16 << dbcr.line_len;
	p_dc->sz = 0x200 << dbcr.sz;
	p_dc->ver = dbcr.ver;
}

/*
 * 1. Validate the Cache Geomtery (compile time config matches hardware)
 * 2. If I-cache suffers from aliasing, setup work arounds (difft flush rtn)
 *    (aliasing D-cache configurations are not supported YET)
 * 3. Enable the Caches, setup default flush mode for D-Cache
 * 3. Calculate the SHMLBA used by user space
 */
void __cpuinit arc_cache_init(void)
{
	unsigned int temp;
	unsigned int cpu = smp_processor_id();
	struct cpuinfo_arc_cache *ic = &cpuinfo_arc700[cpu].icache;
	struct cpuinfo_arc_cache *dc = &cpuinfo_arc700[cpu].dcache;
	int way_pg_ratio = way_pg_ratio;
	char str[256];

	printk(arc_cache_mumbojumbo(0, str, sizeof(str)));

	if (!ic->ver)
		goto chk_dc;

#ifdef CONFIG_ARC_HAS_ICACHE
	/* 1. Confirm some of I-cache params which Linux assumes */
	if ((ic->assoc != ARC_ICACHE_WAYS) ||
	    (ic->line_len != ARC_ICACHE_LINE_LEN)) {
		panic("Cache H/W doesn't match kernel Config");
	}
#if (CONFIG_ARC_MMU_VER > 2)
	if (ic->ver != 3) {
		if (running_on_hw)
			panic("Cache ver doesn't match MMU ver\n");

		/* For ISS - suggest the toggles to use */
		pr_err("Use -prop=icache_version=3,-prop=dcache_version=3\n");

	}
#endif
#endif

	/* Enable/disable I-Cache */
	temp = read_aux_reg(ARC_REG_IC_CTRL);

#ifdef CONFIG_ARC_HAS_ICACHE
	temp &= ~IC_CTRL_CACHE_DISABLE;
#else
	temp |= IC_CTRL_CACHE_DISABLE;
#endif

	write_aux_reg(ARC_REG_IC_CTRL, temp);

chk_dc:
	if (!dc->ver)
		return;

#ifdef CONFIG_ARC_HAS_DCACHE
	if ((dc->assoc != ARC_DCACHE_WAYS) ||
	    (dc->line_len != ARC_DCACHE_LINE_LEN)) {
		panic("Cache H/W doesn't match kernel Config");
	}

	/* check for D-Cache aliasing */
	if ((dc->sz / ARC_DCACHE_WAYS) > PAGE_SIZE)
		panic("D$ aliasing not handled right now\n");
#endif

	/* Set the default Invalidate Mode to "simpy discard dirty lines"
	 *  as this is more frequent then flush before invalidate
	 * Ofcourse we toggle this default behviour when desired
	 */
	temp = read_aux_reg(ARC_REG_DC_CTRL);
	temp &= ~DC_CTRL_INV_MODE_FLUSH;

#ifdef CONFIG_ARC_HAS_DCACHE
	/* Enable D-Cache: Clear Bit 0 */
	write_aux_reg(ARC_REG_DC_CTRL, temp & ~IC_CTRL_CACHE_DISABLE);
#else
	/* Flush D cache */
	write_aux_reg(ARC_REG_DC_FLSH, 0x1);
	/* Disable D cache */
	write_aux_reg(ARC_REG_DC_CTRL, temp | IC_CTRL_CACHE_DISABLE);
#endif

	return;
}

#define OP_INV		0x1
#define OP_FLUSH	0x2
#define OP_FLUSH_N_INV	0x3

#ifdef CONFIG_ARC_HAS_DCACHE

/***************************************************************
 * Machine specific helpers for Entire D-Cache or Per Line ops
 */

static inline void wait_for_flush(void)
{
	while (read_aux_reg(ARC_REG_DC_CTRL) & DC_CTRL_FLUSH_STATUS)
		;
}

/*
 * Operation on Entire D-Cache
 * @cacheop = {OP_INV, OP_FLUSH, OP_FLUSH_N_INV}
 * Note that constant propagation ensures all the checks are gone
 * in generated code
 */
static inline void __dc_entire_op(const int cacheop)
{
	unsigned long flags, tmp = tmp;
	int aux;

	local_irq_save(flags);

	if (cacheop == OP_FLUSH_N_INV) {
		/* Dcache provides 2 cmd: FLUSH or INV
		 * INV inturn has sub-modes: DISCARD or FLUSH-BEFORE
		 * flush-n-inv is achieved by INV cmd but with IM=1
		 * Default INV sub-mode is DISCARD, which needs to be toggled
		 */
		tmp = read_aux_reg(ARC_REG_DC_CTRL);
		write_aux_reg(ARC_REG_DC_CTRL, tmp | DC_CTRL_INV_MODE_FLUSH);
	}

	if (cacheop & OP_INV)	/* Inv or flush-n-inv use same cmd reg */
		aux = ARC_REG_DC_IVDC;
	else
		aux = ARC_REG_DC_FLSH;

	write_aux_reg(aux, 0x1);

	if (cacheop & OP_FLUSH)	/* flush / flush-n-inv both wait */
		wait_for_flush();

	/* Switch back the DISCARD ONLY Invalidate mode */
	if (cacheop == OP_FLUSH_N_INV)
		write_aux_reg(ARC_REG_DC_CTRL, tmp & ~DC_CTRL_INV_MODE_FLUSH);

	local_irq_restore(flags);
}

/*
 * Per Line Operation on D-Cache
 * Doesn't deal with type-of-op/IRQ-disabling/waiting-for-flush-to-complete
 * It's sole purpose is to help gcc generate ZOL
 * (aliasing VIPT dcache flushing needs both vaddr and paddr)
 */
static inline void __dc_line_loop(unsigned long paddr, unsigned long vaddr,
				  unsigned long sz, const int aux_reg)
{
	int num_lines;

	/* Ensure we properly floor/ceil the non-line aligned/sized requests
	 * and have @paddr - aligned to cache line and integral @num_lines.
	 * This however can be avoided for page sized since:
	 *  -@paddr will be cache-line aligned already (being page aligned)
	 *  -@sz will be integral multiple of line size (being page sized).
	 */
	if (!(__builtin_constant_p(sz) && sz == PAGE_SIZE)) {
		sz += paddr & ~DCACHE_LINE_MASK;
		paddr &= DCACHE_LINE_MASK;
		vaddr &= DCACHE_LINE_MASK;
	}

	num_lines = DIV_ROUND_UP(sz, ARC_DCACHE_LINE_LEN);

#if (CONFIG_ARC_MMU_VER <= 2)
	paddr |= (vaddr >> PAGE_SHIFT) & 0x1F;
#endif

	while (num_lines-- > 0) {
#if (CONFIG_ARC_MMU_VER > 2)
		/*
		 * Just as for I$, in MMU v3, D$ ops also require
		 * "tag" bits in DC_PTAG, "index" bits in FLDL,IVDL ops
		 */
		write_aux_reg(ARC_REG_DC_PTAG, paddr);

		write_aux_reg(aux_reg, vaddr);
		vaddr += ARC_DCACHE_LINE_LEN;
#else
		/* paddr contains stuffed vaddrs bits */
		write_aux_reg(aux_reg, paddr);
#endif
		paddr += ARC_DCACHE_LINE_LEN;
	}
}

/* For kernel mappings cache op index is same as paddr */
#define __dc_line_op_k(p, sz, op)	__dc_line_op(p, p, sz, op)

/*
 * D-Cache : Per Line INV (discard or wback+discard) or FLUSH (wback)
 */
static inline void __dc_line_op(unsigned long paddr, unsigned long vaddr,
				unsigned long sz, const int cacheop)
{
	unsigned long flags, tmp = tmp;
	int aux;

	local_irq_save(flags);

	if (cacheop == OP_FLUSH_N_INV) {
		/*
		 * Dcache provides 2 cmd: FLUSH or INV
		 * INV inturn has sub-modes: DISCARD or FLUSH-BEFORE
		 * flush-n-inv is achieved by INV cmd but with IM=1
		 * Default INV sub-mode is DISCARD, which needs to be toggled
		 */
		tmp = read_aux_reg(ARC_REG_DC_CTRL);
		write_aux_reg(ARC_REG_DC_CTRL, tmp | DC_CTRL_INV_MODE_FLUSH);
	}

	if (cacheop & OP_INV)	/* Inv / flush-n-inv use same cmd reg */
		aux = ARC_REG_DC_IVDL;
	else
		aux = ARC_REG_DC_FLDL;

	__dc_line_loop(paddr, vaddr, sz, aux);

	if (cacheop & OP_FLUSH)	/* flush / flush-n-inv both wait */
		wait_for_flush();

	/* Switch back the DISCARD ONLY Invalidate mode */
	if (cacheop == OP_FLUSH_N_INV)
		write_aux_reg(ARC_REG_DC_CTRL, tmp & ~DC_CTRL_INV_MODE_FLUSH);

	local_irq_restore(flags);
}

#else

#define __dc_entire_op(cacheop)
#define __dc_line_op(paddr, vaddr, sz, cacheop)
#define __dc_line_op_k(paddr, sz, cacheop)

#endif /* CONFIG_ARC_HAS_DCACHE */


#ifdef CONFIG_ARC_HAS_ICACHE

/*
 *		I-Cache Aliasing in ARC700 VIPT caches
 *
 * ARC VIPT I-cache uses vaddr to index into cache and paddr to match the tag.
 * The orig Cache Management Module "CDU" only required paddr to invalidate a
 * certain line since it sufficed as index in Non-Aliasing VIPT cache-geometry.
 * Infact for distinct V1,V2,P: all of {V1-P},{V2-P},{P-P} would end up fetching
 * the exact same line.
 *
 * However for larger Caches (way-size > page-size) - i.e. in Aliasing config,
 * paddr alone could not be used to correctly index the cache.
 *
 * ------------------
 * MMU v1/v2 (Fixed Page Size 8k)
 * ------------------
 * The solution was to provide CDU with these additonal vaddr bits. These
 * would be bits [x:13], x would depend on cache-geometry, 13 comes from
 * standard page size of 8k.
 * H/w folks chose [17:13] to be a future safe range, and moreso these 5 bits
 * of vaddr could easily be "stuffed" in the paddr as bits [4:0] since the
 * orig 5 bits of paddr were anyways ignored by CDU line ops, as they
 * represent the offset within cache-line. The adv of using this "clumsy"
 * interface for additional info was no new reg was needed in CDU programming
 * model.
 *
 * 17:13 represented the max num of bits passable, actual bits needed were
 * fewer, based on the num-of-aliases possible.
 * -for 2 alias possibility, only bit 13 needed (32K cache)
 * -for 4 alias possibility, bits 14:13 needed (64K cache)
 *
 * ------------------
 * MMU v3
 * ------------------
 * This ver of MMU supports variable page sizes (1k-16k): although Linux will
 * only support 8k (default), 16k and 4k.
 * However from hardware perspective, smaller page sizes aggrevate aliasing
 * meaning more vaddr bits needed to disambiguate the cache-line-op ;
 * the existing scheme of piggybacking won't work for certain configurations.
 * Two new registers IC_PTAG and DC_PTAG inttoduced.
 * "tag" bits are provided in PTAG, index bits in existing IVIL/IVDL/FLDL regs
 */

/***********************************************************
 * Machine specific helper for per line I-Cache invalidate.
 */
static void __ic_line_inv_vaddr(unsigned long paddr, unsigned long vaddr,
				unsigned long sz)
{
	unsigned long flags;
	int num_lines;

	/*
	 * Ensure we properly floor/ceil the non-line aligned/sized requests:
	 * However page sized flushes can be compile time optimised.
	 *  -@paddr will be cache-line aligned already (being page aligned)
	 *  -@sz will be integral multiple of line size (being page sized).
	 */
	if (!(__builtin_constant_p(sz) && sz == PAGE_SIZE)) {
		sz += paddr & ~ICACHE_LINE_MASK;
		paddr &= ICACHE_LINE_MASK;
		vaddr &= ICACHE_LINE_MASK;
	}

	num_lines = DIV_ROUND_UP(sz, ARC_ICACHE_LINE_LEN);

#if (CONFIG_ARC_MMU_VER <= 2)
	/* bits 17:13 of vaddr go as bits 4:0 of paddr */
	paddr |= (vaddr >> PAGE_SHIFT) & 0x1F;
#endif

	local_irq_save(flags);
	while (num_lines-- > 0) {
#if (CONFIG_ARC_MMU_VER > 2)
		/* tag comes from phy addr */
		write_aux_reg(ARC_REG_IC_PTAG, paddr);

		/* index bits come from vaddr */
		write_aux_reg(ARC_REG_IC_IVIL, vaddr);
		vaddr += ARC_ICACHE_LINE_LEN;
#else
		/* paddr contains stuffed vaddrs bits */
		write_aux_reg(ARC_REG_IC_IVIL, paddr);
#endif
		paddr += ARC_ICACHE_LINE_LEN;
	}
	local_irq_restore(flags);
}

#else

#define __ic_line_inv_vaddr(pstart, vstart, sz)

#endif /* CONFIG_ARC_HAS_ICACHE */


/***********************************************************
 * Exported APIs
 */

void flush_dcache_page(struct page *page)
{
	/* Make a note that dcache is not yet flushed for this page */
	set_bit(PG_arch_1, &page->flags);
}
EXPORT_SYMBOL(flush_dcache_page);


void dma_cache_wback_inv(unsigned long start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_FLUSH_N_INV);
}
EXPORT_SYMBOL(dma_cache_wback_inv);

void dma_cache_inv(unsigned long start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_INV);
}
EXPORT_SYMBOL(dma_cache_inv);

void dma_cache_wback(unsigned long start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_FLUSH);
}
EXPORT_SYMBOL(dma_cache_wback);

/*
 * This is API for making I/D Caches consistent when modifying
 * kernel code (loadable modules, kprobes, kgdb...)
 * This is called on insmod, with kernel virtual address for CODE of
 * the module. ARC cache maintenance ops require PHY address thus we
 * need to convert vmalloc addr to PHY addr
 */
void flush_icache_range(unsigned long kstart, unsigned long kend)
{
	unsigned int tot_sz, off, sz;
	unsigned long phy, pfn;

	/* printk("Kernel Cache Cohenercy: %lx to %lx\n",kstart, kend); */

	/* This is not the right API for user virtual address */
	if (kstart < TASK_SIZE) {
		BUG_ON("Flush icache range for user virtual addr space");
		return;
	}

	/* Shortcut for bigger flush ranges.
	 * Here we don't care if this was kernel virtual or phy addr
	 */
	tot_sz = kend - kstart;
	if (tot_sz > PAGE_SIZE) {
		flush_cache_all();
		return;
	}

	/* Case: Kernel Phy addr (0x8000_0000 onwards) */
	if (likely(kstart > PAGE_OFFSET)) {
		/*
		 * The 2nd arg despite being paddr will be used to index icache
		 * This is OK since no alternate virtual mappings will exist
		 * given the callers for this case: kprobe/kgdb in built-in
		 * kernel code only.
		 */
		__sync_icache_dcache(kstart, kstart, kend - kstart);
		return;
	}

	/*
	 * Case: Kernel Vaddr (0x7000_0000 to 0x7fff_ffff)
	 * (1) ARC Cache Maintenance ops only take Phy addr, hence special
	 *     handling of kernel vaddr.
	 *
	 * (2) Despite @tot_sz being < PAGE_SIZE (bigger cases handled already),
	 *     it still needs to handle  a 2 page scenario, where the range
	 *     straddles across 2 virtual pages and hence need for loop
	 */
	while (tot_sz > 0) {
		off = kstart % PAGE_SIZE;
		pfn = vmalloc_to_pfn((void *)kstart);
		phy = (pfn << PAGE_SHIFT) + off;
		sz = min_t(unsigned int, tot_sz, PAGE_SIZE - off);
		__sync_icache_dcache(phy, kstart, sz);
		kstart += sz;
		tot_sz -= sz;
	}
}

/*
 * General purpose helper to make I and D cache lines consistent.
 * @paddr is phy addr of region
 * @vaddr is typically user or kernel vaddr (vmalloc)
 *    Howver in one instance, flush_icache_range() by kprobe (for a breakpt in
 *    builtin kernel code) @vaddr will be paddr only, meaning CDU operation will
 *    use a paddr to index the cache (despite VIPT). This is fine since since a
 *    built-in kernel page will not have any virtual mappings (not even kernel)
 *    kprobe on loadable module is different as it will have kvaddr.
 */
void __sync_icache_dcache(unsigned long paddr, unsigned long vaddr, int len)
{
	unsigned long flags;

	local_irq_save(flags);
	__ic_line_inv_vaddr(paddr, vaddr, len);
	__dc_line_op(paddr, vaddr, len, OP_FLUSH);
	local_irq_restore(flags);
}

/* wrapper to compile time eliminate alignment checks in flush loop */
void __inv_icache_page(unsigned long paddr, unsigned long vaddr)
{
	__ic_line_inv_vaddr(paddr, vaddr, PAGE_SIZE);
}

/*
 * wrapper to clearout kernel or userspace mappings of a page
 * For kernel mappings @vaddr == @paddr
 */
void __flush_dcache_page(unsigned long paddr, unsigned long vaddr)
{
	__dc_line_op(paddr, vaddr & PAGE_MASK, PAGE_SIZE, OP_FLUSH_N_INV);
}

void flush_icache_all(void)
{
	unsigned long flags;

	local_irq_save(flags);

	write_aux_reg(ARC_REG_IC_IVIC, 1);

	/* lr will not complete till the icache inv operation is not over */
	read_aux_reg(ARC_REG_IC_CTRL);
	local_irq_restore(flags);
}

noinline void flush_cache_all(void)
{
	unsigned long flags;

	local_irq_save(flags);

	flush_icache_all();
	__dc_entire_op(OP_FLUSH_N_INV);

	local_irq_restore(flags);

}

/**********************************************************************
 * Explicit Cache flush request from user space via syscall
 * Needed for JITs which generate code on the fly
 */
SYSCALL_DEFINE3(cacheflush, uint32_t, start, uint32_t, sz, uint32_t, flags)
{
	/* TBD: optimize this */
	flush_cache_all();
	return 0;
}
