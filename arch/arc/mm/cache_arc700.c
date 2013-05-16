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
#include <linux/pagemap.h>
#include <asm/cacheflush.h>
#include <asm/cachectl.h>
#include <asm/setup.h>

/* Instruction cache related Auxiliary registers */
#define ARC_REG_IC_BCR		0x77	/* Build Config reg */
#define ARC_REG_IC_IVIC		0x10
#define ARC_REG_IC_CTRL		0x11
#define ARC_REG_IC_IVIL		0x19
#if (CONFIG_ARC_MMU_VER > 2)
#define ARC_REG_IC_PTAG		0x1E
#endif

/* Bit val in IC_CTRL */
#define IC_CTRL_CACHE_DISABLE   0x1

/* Data cache related Auxiliary registers */
#define ARC_REG_DC_BCR		0x72	/* Build Config reg */
#define ARC_REG_DC_IVDC		0x47
#define ARC_REG_DC_CTRL		0x48
#define ARC_REG_DC_IVDL		0x4A
#define ARC_REG_DC_FLSH		0x4B
#define ARC_REG_DC_FLDL		0x4C
#if (CONFIG_ARC_MMU_VER > 2)
#define ARC_REG_DC_PTAG		0x5C
#endif

/* Bit val in DC_CTRL */
#define DC_CTRL_INV_MODE_FLUSH  0x40
#define DC_CTRL_FLUSH_STATUS    0x100

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

	PR_CACHE(&cpuinfo_arc700[c].icache, IS_ENABLED(CONFIG_ARC_HAS_ICACHE),
			"I-Cache");
	PR_CACHE(&cpuinfo_arc700[c].dcache, IS_ENABLED(CONFIG_ARC_HAS_DCACHE),
			"D-Cache");

	return buf;
}

/*
 * Read the Cache Build Confuration Registers, Decode them and save into
 * the cpuinfo structure for later use.
 * No Validation done here, simply read/convert the BCRs
 */
void __cpuinit read_decode_cache_bcr(void)
{
	struct cpuinfo_arc_cache *p_ic, *p_dc;
	unsigned int cpu = smp_processor_id();
	struct bcr_cache {
#ifdef CONFIG_CPU_BIG_ENDIAN
		unsigned int pad:12, line_len:4, sz:4, config:4, ver:8;
#else
		unsigned int ver:8, config:4, sz:4, line_len:4, pad:12;
#endif
	} ibcr, dbcr;

	p_ic = &cpuinfo_arc700[cpu].icache;
	READ_BCR(ARC_REG_IC_BCR, ibcr);

	BUG_ON(ibcr.config != 3);
	p_ic->assoc = 2;		/* Fixed to 2w set assoc */
	p_ic->line_len = 8 << ibcr.line_len;
	p_ic->sz = 0x200 << ibcr.sz;
	p_ic->ver = ibcr.ver;

	p_dc = &cpuinfo_arc700[cpu].dcache;
	READ_BCR(ARC_REG_DC_BCR, dbcr);

	BUG_ON(dbcr.config != 2);
	p_dc->assoc = 4;		/* Fixed to 4w set assoc */
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
	unsigned int cpu = smp_processor_id();
	struct cpuinfo_arc_cache *ic = &cpuinfo_arc700[cpu].icache;
	struct cpuinfo_arc_cache *dc = &cpuinfo_arc700[cpu].dcache;
	unsigned int dcache_does_alias, temp;
	char str[256];

	printk(arc_cache_mumbojumbo(0, str, sizeof(str)));

	if (!ic->ver)
		goto chk_dc;

#ifdef CONFIG_ARC_HAS_ICACHE
	/* 1. Confirm some of I-cache params which Linux assumes */
	if (ic->line_len != ARC_ICACHE_LINE_LEN)
		panic("Cache H/W doesn't match kernel Config");

	if (ic->ver != CONFIG_ARC_MMU_VER)
		panic("Cache ver doesn't match MMU ver\n");
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
	if (dc->line_len != ARC_DCACHE_LINE_LEN)
		panic("Cache H/W doesn't match kernel Config");

	/* check for D-Cache aliasing */
	dcache_does_alias = (dc->sz / dc->assoc) > PAGE_SIZE;

	if (dcache_does_alias && !cache_is_vipt_aliasing())
		panic("Enable CONFIG_ARC_CACHE_VIPT_ALIASING\n");
	else if (!dcache_does_alias && cache_is_vipt_aliasing())
		panic("Don't need CONFIG_ARC_CACHE_VIPT_ALIASING\n");
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
	unsigned int tmp = tmp;
	int aux;

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

/* For kernel mappings cache operation: index is same as paddr */
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

static inline void __ic_entire_inv(void)
{
	write_aux_reg(ARC_REG_IC_IVIC, 1);
	read_aux_reg(ARC_REG_IC_CTRL);	/* blocks */
}

#else

#define __ic_entire_inv()
#define __ic_line_inv_vaddr(pstart, vstart, sz)

#endif /* CONFIG_ARC_HAS_ICACHE */


/***********************************************************
 * Exported APIs
 */

/*
 * Handle cache congruency of kernel and userspace mappings of page when kernel
 * writes-to/reads-from
 *
 * The idea is to defer flushing of kernel mapping after a WRITE, possible if:
 *  -dcache is NOT aliasing, hence any U/K-mappings of page are congruent
 *  -U-mapping doesn't exist yet for page (finalised in update_mmu_cache)
 *  -In SMP, if hardware caches are coherent
 *
 * There's a corollary case, where kernel READs from a userspace mapped page.
 * If the U-mapping is not congruent to to K-mapping, former needs flushing.
 */
void flush_dcache_page(struct page *page)
{
	struct address_space *mapping;

	if (!cache_is_vipt_aliasing()) {
		set_bit(PG_arch_1, &page->flags);
		return;
	}

	/* don't handle anon pages here */
	mapping = page_mapping(page);
	if (!mapping)
		return;

	/*
	 * pagecache page, file not yet mapped to userspace
	 * Make a note that K-mapping is dirty
	 */
	if (!mapping_mapped(mapping)) {
		set_bit(PG_arch_1, &page->flags);
	} else if (page_mapped(page)) {

		/* kernel reading from page with U-mapping */
		void *paddr = page_address(page);
		unsigned long vaddr = page->index << PAGE_CACHE_SHIFT;

		if (addr_not_cache_congruent(paddr, vaddr))
			__flush_dcache_page(paddr, vaddr);
	}
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
	__dc_line_op(paddr, vaddr, len, OP_FLUSH_N_INV);
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
void ___flush_dcache_page(unsigned long paddr, unsigned long vaddr)
{
	__dc_line_op(paddr, vaddr & PAGE_MASK, PAGE_SIZE, OP_FLUSH_N_INV);
}

noinline void flush_cache_all(void)
{
	unsigned long flags;

	local_irq_save(flags);

	__ic_entire_inv();
	__dc_entire_op(OP_FLUSH_N_INV);

	local_irq_restore(flags);

}

#ifdef CONFIG_ARC_CACHE_VIPT_ALIASING

void flush_cache_mm(struct mm_struct *mm)
{
	flush_cache_all();
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long u_vaddr,
		      unsigned long pfn)
{
	unsigned int paddr = pfn << PAGE_SHIFT;

	u_vaddr &= PAGE_MASK;

	___flush_dcache_page(paddr, u_vaddr);

	if (vma->vm_flags & VM_EXEC)
		__inv_icache_page(paddr, u_vaddr);
}

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end)
{
	flush_cache_all();
}

void flush_anon_page(struct vm_area_struct *vma, struct page *page,
		     unsigned long u_vaddr)
{
	/* TBD: do we really need to clear the kernel mapping */
	__flush_dcache_page(page_address(page), u_vaddr);
	__flush_dcache_page(page_address(page), page_address(page));

}

#endif

void copy_user_highpage(struct page *to, struct page *from,
	unsigned long u_vaddr, struct vm_area_struct *vma)
{
	void *kfrom = page_address(from);
	void *kto = page_address(to);
	int clean_src_k_mappings = 0;

	/*
	 * If SRC page was already mapped in userspace AND it's U-mapping is
	 * not congruent with K-mapping, sync former to physical page so that
	 * K-mapping in memcpy below, sees the right data
	 *
	 * Note that while @u_vaddr refers to DST page's userspace vaddr, it is
	 * equally valid for SRC page as well
	 */
	if (page_mapped(from) && addr_not_cache_congruent(kfrom, u_vaddr)) {
		__flush_dcache_page(kfrom, u_vaddr);
		clean_src_k_mappings = 1;
	}

	copy_page(kto, kfrom);

	/*
	 * Mark DST page K-mapping as dirty for a later finalization by
	 * update_mmu_cache(). Although the finalization could have been done
	 * here as well (given that both vaddr/paddr are available).
	 * But update_mmu_cache() already has code to do that for other
	 * non copied user pages (e.g. read faults which wire in pagecache page
	 * directly).
	 */
	set_bit(PG_arch_1, &to->flags);

	/*
	 * if SRC was already usermapped and non-congruent to kernel mapping
	 * sync the kernel mapping back to physical page
	 */
	if (clean_src_k_mappings) {
		__flush_dcache_page(kfrom, kfrom);
	} else {
		set_bit(PG_arch_1, &from->flags);
	}
}

void clear_user_page(void *to, unsigned long u_vaddr, struct page *page)
{
	clear_page(to);
	set_bit(PG_arch_1, &page->flags);
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
