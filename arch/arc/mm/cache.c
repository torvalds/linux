// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARC Cache Management
 *
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
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

#ifdef CONFIG_ISA_ARCV2
#define USE_RGN_FLSH	1
#endif

static int l2_line_sz;
static int ioc_exists;
int slc_enable = 1, ioc_enable = 1;
unsigned long perip_base = ARC_UNCACHED_ADDR_SPACE; /* legacy value for boot */
unsigned long perip_end = 0xFFFFFFFF; /* legacy value */

static struct cpuinfo_arc_cache {
	unsigned int sz_k, line_len, colors;
} ic_info, dc_info, slc_info;

void (*_cache_line_loop_ic_fn)(phys_addr_t paddr, unsigned long vaddr,
			       unsigned long sz, const int op, const int full_page);

void (*__dma_cache_wback_inv)(phys_addr_t start, unsigned long sz);
void (*__dma_cache_inv)(phys_addr_t start, unsigned long sz);
void (*__dma_cache_wback)(phys_addr_t start, unsigned long sz);

static int read_decode_cache_bcr_arcv2(int c, char *buf, int len)
{
	struct cpuinfo_arc_cache *p_slc = &slc_info;
	struct bcr_identity ident;
	struct bcr_generic sbcr;
	struct bcr_clust_cfg cbcr;
	struct bcr_volatile vol;
	int n = 0;

	READ_BCR(ARC_REG_SLC_BCR, sbcr);
	if (sbcr.ver) {
		struct bcr_slc_cfg  slc_cfg;
		READ_BCR(ARC_REG_SLC_CFG, slc_cfg);
		p_slc->sz_k = 128 << slc_cfg.sz;
		l2_line_sz = p_slc->line_len = (slc_cfg.lsz == 0) ? 128 : 64;
		n += scnprintf(buf + n, len - n,
			       "SLC\t\t: %uK, %uB Line%s\n",
			       p_slc->sz_k, p_slc->line_len, IS_USED_RUN(slc_enable));
	}

	READ_BCR(ARC_REG_CLUSTER_BCR, cbcr);
	if (cbcr.c) {
		ioc_exists = 1;

		/*
		 * As for today we don't support both IOC and ZONE_HIGHMEM enabled
		 * simultaneously. This happens because as of today IOC aperture covers
		 * only ZONE_NORMAL (low mem) and any dma transactions outside this
		 * region won't be HW coherent.
		 * If we want to use both IOC and ZONE_HIGHMEM we can use
		 * bounce_buffer to handle dma transactions to HIGHMEM.
		 * Also it is possible to modify dma_direct cache ops or increase IOC
		 * aperture size if we are planning to use HIGHMEM without PAE.
		 */
		if (IS_ENABLED(CONFIG_HIGHMEM) || is_pae40_enabled())
			ioc_enable = 0;
	} else {
		ioc_enable = 0;
	}

	READ_BCR(AUX_IDENTITY, ident);

	/* HS 2.0 didn't have AUX_VOL */
	if (ident.family > 0x51) {
		READ_BCR(AUX_VOL, vol);
		perip_base = vol.start << 28;
		/* HS 3.0 has limit and strict-ordering fields */
		if (ident.family > 0x52)
			perip_end = (vol.limit << 28) - 1;
	}

	n += scnprintf(buf + n, len - n, "Peripherals\t: %#lx%s%s\n",
		       perip_base,
		       IS_AVAIL3(ioc_exists, ioc_enable, ", IO-Coherency (per-device) "));

	return n;
}

int arc_cache_mumbojumbo(int c, char *buf, int len)
{
	struct cpuinfo_arc_cache *p_ic = &ic_info, *p_dc = &dc_info;
	struct bcr_cache ibcr, dbcr;
	int vipt, assoc;
	int n = 0;

	READ_BCR(ARC_REG_IC_BCR, ibcr);
	if (!ibcr.ver)
		goto dc_chk;

	if (is_isa_arcompact() && (ibcr.ver <= 3)) {
		BUG_ON(ibcr.config != 3);
		assoc = 2;		/* Fixed to 2w set assoc */
	} else if (is_isa_arcv2() && (ibcr.ver >= 4)) {
		assoc = 1 << ibcr.config;	/* 1,2,4,8 */
	}

	p_ic->line_len = 8 << ibcr.line_len;
	p_ic->sz_k = 1 << (ibcr.sz - 1);
	p_ic->colors = p_ic->sz_k/assoc/TO_KB(PAGE_SIZE);

	n += scnprintf(buf + n, len - n,
			"I-Cache\t\t: %uK, %dway/set, %uB Line, VIPT%s%s\n",
			p_ic->sz_k, assoc, p_ic->line_len,
			p_ic->colors > 1 ? " aliasing" : "",
			IS_USED_CFG(CONFIG_ARC_HAS_ICACHE));

dc_chk:
	READ_BCR(ARC_REG_DC_BCR, dbcr);
	if (!dbcr.ver)
		goto slc_chk;

	if (is_isa_arcompact() && (dbcr.ver <= 3)) {
		BUG_ON(dbcr.config != 2);
		vipt = 1;
		assoc = 4;		/* Fixed to 4w set assoc */
		p_dc->colors = p_dc->sz_k/assoc/TO_KB(PAGE_SIZE);
	} else if (is_isa_arcv2() && (dbcr.ver >= 4)) {
		vipt = 0;
		assoc = 1 << dbcr.config;	/* 1,2,4,8 */
		p_dc->colors = 1;		/* PIPT so can't VIPT alias */
	}

	p_dc->line_len = 16 << dbcr.line_len;
	p_dc->sz_k = 1 << (dbcr.sz - 1);

	n += scnprintf(buf + n, len - n,
			"D-Cache\t\t: %uK, %dway/set, %uB Line, %s%s\n",
			p_dc->sz_k, assoc, p_dc->line_len,
			vipt ? "VIPT" : "PIPT",
			IS_USED_CFG(CONFIG_ARC_HAS_DCACHE));

slc_chk:
	if (is_isa_arcv2())
		n += read_decode_cache_bcr_arcv2(c, buf + n, len - n);

	return n;
}

/*
 * Line Operation on {I,D}-Cache
 */

#define OP_INV		0x1
#define OP_FLUSH	0x2
#define OP_FLUSH_N_INV	0x3
#define OP_INV_IC	0x4

/*
 * Cache Flush programming model
 *
 * ARC700 MMUv3 I$ and D$ are both VIPT and can potentially alias.
 * Programming model requires both paddr and vaddr irrespecive of aliasing
 * considerations:
 *  - vaddr in {I,D}C_IV?L
 *  - paddr in {I,D}C_PTAG
 *
 * In HS38x (MMUv4), D$ is PIPT, I$ is VIPT and can still alias.
 * Programming model is different for aliasing vs. non-aliasing I$
 *  - D$ / Non-aliasing I$: only paddr in {I,D}C_IV?L
 *  - Aliasing I$: same as ARC700 above (so MMUv3 routine used for MMUv4 I$)
 *
 *  - If PAE40 is enabled, independent of aliasing considerations, the higher
 *    bits needs to be written into PTAG_HI
 */

static inline
void __cache_line_loop_v3(phys_addr_t paddr, unsigned long vaddr,
			  unsigned long sz, const int op, const int full_page)
{
	unsigned int aux_cmd, aux_tag;
	int num_lines;

	if (op == OP_INV_IC) {
		aux_cmd = ARC_REG_IC_IVIL;
		aux_tag = ARC_REG_IC_PTAG;
	} else {
		aux_cmd = op & OP_INV ? ARC_REG_DC_IVDL : ARC_REG_DC_FLDL;
		aux_tag = ARC_REG_DC_PTAG;
	}

	/* Ensure we properly floor/ceil the non-line aligned/sized requests
	 * and have @paddr - aligned to cache line and integral @num_lines.
	 * This however can be avoided for page sized since:
	 *  -@paddr will be cache-line aligned already (being page aligned)
	 *  -@sz will be integral multiple of line size (being page sized).
	 */
	if (!full_page) {
		sz += paddr & ~CACHE_LINE_MASK;
		paddr &= CACHE_LINE_MASK;
		vaddr &= CACHE_LINE_MASK;
	}
	num_lines = DIV_ROUND_UP(sz, L1_CACHE_BYTES);

	/*
	 * MMUv3, cache ops require paddr in PTAG reg
	 * if V-P const for loop, PTAG can be written once outside loop
	 */
	if (full_page)
		write_aux_reg(aux_tag, paddr);

	/*
	 * This is technically for MMU v4, using the MMU v3 programming model
	 * Special work for HS38 aliasing I-cache configuration with PAE40
	 *   - upper 8 bits of paddr need to be written into PTAG_HI
	 *   - (and needs to be written before the lower 32 bits)
	 * Note that PTAG_HI is hoisted outside the line loop
	 */
	if (is_pae40_enabled() && op == OP_INV_IC)
		write_aux_reg(ARC_REG_IC_PTAG_HI, (u64)paddr >> 32);

	while (num_lines-- > 0) {
		if (!full_page) {
			write_aux_reg(aux_tag, paddr);
			paddr += L1_CACHE_BYTES;
		}

		write_aux_reg(aux_cmd, vaddr);
		vaddr += L1_CACHE_BYTES;
	}
}

#ifndef USE_RGN_FLSH

/*
 */
static inline
void __cache_line_loop_v4(phys_addr_t paddr, unsigned long vaddr,
			  unsigned long sz, const int op, const int full_page)
{
	unsigned int aux_cmd;
	int num_lines;

	if (op == OP_INV_IC) {
		aux_cmd = ARC_REG_IC_IVIL;
	} else {
		/* d$ cmd: INV (discard or wback-n-discard) OR FLUSH (wback) */
		aux_cmd = op & OP_INV ? ARC_REG_DC_IVDL : ARC_REG_DC_FLDL;
	}

	/* Ensure we properly floor/ceil the non-line aligned/sized requests
	 * and have @paddr - aligned to cache line and integral @num_lines.
	 * This however can be avoided for page sized since:
	 *  -@paddr will be cache-line aligned already (being page aligned)
	 *  -@sz will be integral multiple of line size (being page sized).
	 */
	if (!full_page) {
		sz += paddr & ~CACHE_LINE_MASK;
		paddr &= CACHE_LINE_MASK;
	}

	num_lines = DIV_ROUND_UP(sz, L1_CACHE_BYTES);

	/*
	 * For HS38 PAE40 configuration
	 *   - upper 8 bits of paddr need to be written into PTAG_HI
	 *   - (and needs to be written before the lower 32 bits)
	 */
	if (is_pae40_enabled()) {
		if (op == OP_INV_IC)
			/*
			 * Non aliasing I-cache in HS38,
			 * aliasing I-cache handled in __cache_line_loop_v3()
			 */
			write_aux_reg(ARC_REG_IC_PTAG_HI, (u64)paddr >> 32);
		else
			write_aux_reg(ARC_REG_DC_PTAG_HI, (u64)paddr >> 32);
	}

	while (num_lines-- > 0) {
		write_aux_reg(aux_cmd, paddr);
		paddr += L1_CACHE_BYTES;
	}
}

#else

/*
 * optimized flush operation which takes a region as opposed to iterating per line
 */
static inline
void __cache_line_loop_v4(phys_addr_t paddr, unsigned long vaddr,
			  unsigned long sz, const int op, const int full_page)
{
	unsigned int s, e;

	/* Only for Non aliasing I-cache in HS38 */
	if (op == OP_INV_IC) {
		s = ARC_REG_IC_IVIR;
		e = ARC_REG_IC_ENDR;
	} else {
		s = ARC_REG_DC_STARTR;
		e = ARC_REG_DC_ENDR;
	}

	if (!full_page) {
		/* for any leading gap between @paddr and start of cache line */
		sz += paddr & ~CACHE_LINE_MASK;
		paddr &= CACHE_LINE_MASK;

		/*
		 *  account for any trailing gap to end of cache line
		 *  this is equivalent to DIV_ROUND_UP() in line ops above
		 */
		sz += L1_CACHE_BYTES - 1;
	}

	if (is_pae40_enabled()) {
		/* TBD: check if crossing 4TB boundary */
		if (op == OP_INV_IC)
			write_aux_reg(ARC_REG_IC_PTAG_HI, (u64)paddr >> 32);
		else
			write_aux_reg(ARC_REG_DC_PTAG_HI, (u64)paddr >> 32);
	}

	/* ENDR needs to be set ahead of START */
	write_aux_reg(e, paddr + sz);	/* ENDR is exclusive */
	write_aux_reg(s, paddr);

	/* caller waits on DC_CTRL.FS */
}

#endif

#ifdef CONFIG_ARC_MMU_V3
#define __cache_line_loop	__cache_line_loop_v3
#else
#define __cache_line_loop	__cache_line_loop_v4
#endif

#ifdef CONFIG_ARC_HAS_DCACHE

/***************************************************************
 * Machine specific helpers for Entire D-Cache or Per Line ops
 */

#ifndef USE_RGN_FLSH
/*
 * this version avoids extra read/write of DC_CTRL for flush or invalid ops
 * in the non region flush regime (such as for ARCompact)
 */
static inline void __before_dc_op(const int op)
{
	if (op == OP_FLUSH_N_INV) {
		/* Dcache provides 2 cmd: FLUSH or INV
		 * INV in turn has sub-modes: DISCARD or FLUSH-BEFORE
		 * flush-n-inv is achieved by INV cmd but with IM=1
		 * So toggle INV sub-mode depending on op request and default
		 */
		const unsigned int ctl = ARC_REG_DC_CTRL;
		write_aux_reg(ctl, read_aux_reg(ctl) | DC_CTRL_INV_MODE_FLUSH);
	}
}

#else

static inline void __before_dc_op(const int op)
{
	const unsigned int ctl = ARC_REG_DC_CTRL;
	unsigned int val = read_aux_reg(ctl);

	if (op == OP_FLUSH_N_INV) {
		val |= DC_CTRL_INV_MODE_FLUSH;
	}

	if (op != OP_INV_IC) {
		/*
		 * Flush / Invalidate is provided by DC_CTRL.RNG_OP 0 or 1
		 * combined Flush-n-invalidate uses DC_CTRL.IM = 1 set above
		 */
		val &= ~DC_CTRL_RGN_OP_MSK;
		if (op & OP_INV)
			val |= DC_CTRL_RGN_OP_INV;
	}
	write_aux_reg(ctl, val);
}

#endif


static inline void __after_dc_op(const int op)
{
	if (op & OP_FLUSH) {
		const unsigned int ctl = ARC_REG_DC_CTRL;
		unsigned int reg;

		/* flush / flush-n-inv both wait */
		while ((reg = read_aux_reg(ctl)) & DC_CTRL_FLUSH_STATUS)
			;

		/* Switch back to default Invalidate mode */
		if (op == OP_FLUSH_N_INV)
			write_aux_reg(ctl, reg & ~DC_CTRL_INV_MODE_FLUSH);
	}
}

/*
 * Operation on Entire D-Cache
 * @op = {OP_INV, OP_FLUSH, OP_FLUSH_N_INV}
 * Note that constant propagation ensures all the checks are gone
 * in generated code
 */
static inline void __dc_entire_op(const int op)
{
	int aux;

	__before_dc_op(op);

	if (op & OP_INV)	/* Inv or flush-n-inv use same cmd reg */
		aux = ARC_REG_DC_IVDC;
	else
		aux = ARC_REG_DC_FLSH;

	write_aux_reg(aux, 0x1);

	__after_dc_op(op);
}

static inline void __dc_disable(void)
{
	const int r = ARC_REG_DC_CTRL;

	__dc_entire_op(OP_FLUSH_N_INV);
	write_aux_reg(r, read_aux_reg(r) | DC_CTRL_DIS);
}

static void __dc_enable(void)
{
	const int r = ARC_REG_DC_CTRL;

	write_aux_reg(r, read_aux_reg(r) & ~DC_CTRL_DIS);
}

/* For kernel mappings cache operation: index is same as paddr */
#define __dc_line_op_k(p, sz, op)	__dc_line_op(p, p, sz, op)

/*
 * D-Cache Line ops: Per Line INV (discard or wback+discard) or FLUSH (wback)
 */
static inline void __dc_line_op(phys_addr_t paddr, unsigned long vaddr,
				unsigned long sz, const int op)
{
	const int full_page = __builtin_constant_p(sz) && sz == PAGE_SIZE;
	unsigned long flags;

	local_irq_save(flags);

	__before_dc_op(op);

	__cache_line_loop(paddr, vaddr, sz, op, full_page);

	__after_dc_op(op);

	local_irq_restore(flags);
}

#else

#define __dc_entire_op(op)
#define __dc_disable()
#define __dc_enable()
#define __dc_line_op(paddr, vaddr, sz, op)
#define __dc_line_op_k(paddr, sz, op)

#endif /* CONFIG_ARC_HAS_DCACHE */

#ifdef CONFIG_ARC_HAS_ICACHE

static inline void __ic_entire_inv(void)
{
	write_aux_reg(ARC_REG_IC_IVIC, 1);
	read_aux_reg(ARC_REG_IC_CTRL);	/* blocks */
}

static inline void
__ic_line_inv_vaddr_local(phys_addr_t paddr, unsigned long vaddr,
			  unsigned long sz)
{
	const int full_page = __builtin_constant_p(sz) && sz == PAGE_SIZE;
	unsigned long flags;

	local_irq_save(flags);
	(*_cache_line_loop_ic_fn)(paddr, vaddr, sz, OP_INV_IC, full_page);
	local_irq_restore(flags);
}

#ifndef CONFIG_SMP

#define __ic_line_inv_vaddr(p, v, s)	__ic_line_inv_vaddr_local(p, v, s)

#else

struct ic_inv_args {
	phys_addr_t paddr, vaddr;
	int sz;
};

static void __ic_line_inv_vaddr_helper(void *info)
{
        struct ic_inv_args *ic_inv = info;

        __ic_line_inv_vaddr_local(ic_inv->paddr, ic_inv->vaddr, ic_inv->sz);
}

static void __ic_line_inv_vaddr(phys_addr_t paddr, unsigned long vaddr,
				unsigned long sz)
{
	struct ic_inv_args ic_inv = {
		.paddr = paddr,
		.vaddr = vaddr,
		.sz    = sz
	};

	on_each_cpu(__ic_line_inv_vaddr_helper, &ic_inv, 1);
}

#endif	/* CONFIG_SMP */

#else	/* !CONFIG_ARC_HAS_ICACHE */

#define __ic_entire_inv()
#define __ic_line_inv_vaddr(pstart, vstart, sz)

#endif /* CONFIG_ARC_HAS_ICACHE */

static noinline void slc_op_rgn(phys_addr_t paddr, unsigned long sz, const int op)
{
#ifdef CONFIG_ISA_ARCV2
	/*
	 * SLC is shared between all cores and concurrent aux operations from
	 * multiple cores need to be serialized using a spinlock
	 * A concurrent operation can be silently ignored and/or the old/new
	 * operation can remain incomplete forever (lockup in SLC_CTRL_BUSY loop
	 * below)
	 */
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	unsigned int ctrl;
	phys_addr_t end;

	spin_lock_irqsave(&lock, flags);

	/*
	 * The Region Flush operation is specified by CTRL.RGN_OP[11..9]
	 *  - b'000 (default) is Flush,
	 *  - b'001 is Invalidate if CTRL.IM == 0
	 *  - b'001 is Flush-n-Invalidate if CTRL.IM == 1
	 */
	ctrl = read_aux_reg(ARC_REG_SLC_CTRL);

	/* Don't rely on default value of IM bit */
	if (!(op & OP_FLUSH))		/* i.e. OP_INV */
		ctrl &= ~SLC_CTRL_IM;	/* clear IM: Disable flush before Inv */
	else
		ctrl |= SLC_CTRL_IM;

	if (op & OP_INV)
		ctrl |= SLC_CTRL_RGN_OP_INV;	/* Inv or flush-n-inv */
	else
		ctrl &= ~SLC_CTRL_RGN_OP_INV;

	write_aux_reg(ARC_REG_SLC_CTRL, ctrl);

	/*
	 * Lower bits are ignored, no need to clip
	 * END needs to be setup before START (latter triggers the operation)
	 * END can't be same as START, so add (l2_line_sz - 1) to sz
	 */
	end = paddr + sz + l2_line_sz - 1;
	if (is_pae40_enabled())
		write_aux_reg(ARC_REG_SLC_RGN_END1, upper_32_bits(end));

	write_aux_reg(ARC_REG_SLC_RGN_END, lower_32_bits(end));

	if (is_pae40_enabled())
		write_aux_reg(ARC_REG_SLC_RGN_START1, upper_32_bits(paddr));

	write_aux_reg(ARC_REG_SLC_RGN_START, lower_32_bits(paddr));

	/* Make sure "busy" bit reports correct stataus, see STAR 9001165532 */
	read_aux_reg(ARC_REG_SLC_CTRL);

	while (read_aux_reg(ARC_REG_SLC_CTRL) & SLC_CTRL_BUSY);

	spin_unlock_irqrestore(&lock, flags);
#endif
}

static __maybe_unused noinline void slc_op_line(phys_addr_t paddr, unsigned long sz, const int op)
{
#ifdef CONFIG_ISA_ARCV2
	/*
	 * SLC is shared between all cores and concurrent aux operations from
	 * multiple cores need to be serialized using a spinlock
	 * A concurrent operation can be silently ignored and/or the old/new
	 * operation can remain incomplete forever (lockup in SLC_CTRL_BUSY loop
	 * below)
	 */
	static DEFINE_SPINLOCK(lock);

	const unsigned long SLC_LINE_MASK = ~(l2_line_sz - 1);
	unsigned int ctrl, cmd;
	unsigned long flags;
	int num_lines;

	spin_lock_irqsave(&lock, flags);

	ctrl = read_aux_reg(ARC_REG_SLC_CTRL);

	/* Don't rely on default value of IM bit */
	if (!(op & OP_FLUSH))		/* i.e. OP_INV */
		ctrl &= ~SLC_CTRL_IM;	/* clear IM: Disable flush before Inv */
	else
		ctrl |= SLC_CTRL_IM;

	write_aux_reg(ARC_REG_SLC_CTRL, ctrl);

	cmd = op & OP_INV ? ARC_AUX_SLC_IVDL : ARC_AUX_SLC_FLDL;

	sz += paddr & ~SLC_LINE_MASK;
	paddr &= SLC_LINE_MASK;

	num_lines = DIV_ROUND_UP(sz, l2_line_sz);

	while (num_lines-- > 0) {
		write_aux_reg(cmd, paddr);
		paddr += l2_line_sz;
	}

	/* Make sure "busy" bit reports correct stataus, see STAR 9001165532 */
	read_aux_reg(ARC_REG_SLC_CTRL);

	while (read_aux_reg(ARC_REG_SLC_CTRL) & SLC_CTRL_BUSY);

	spin_unlock_irqrestore(&lock, flags);
#endif
}

#define slc_op(paddr, sz, op)	slc_op_rgn(paddr, sz, op)

noinline static void slc_entire_op(const int op)
{
	unsigned int ctrl, r = ARC_REG_SLC_CTRL;

	ctrl = read_aux_reg(r);

	if (!(op & OP_FLUSH))		/* i.e. OP_INV */
		ctrl &= ~SLC_CTRL_IM;	/* clear IM: Disable flush before Inv */
	else
		ctrl |= SLC_CTRL_IM;

	write_aux_reg(r, ctrl);

	if (op & OP_INV)	/* Inv or flush-n-inv use same cmd reg */
		write_aux_reg(ARC_REG_SLC_INVALIDATE, 0x1);
	else
		write_aux_reg(ARC_REG_SLC_FLUSH, 0x1);

	/* Make sure "busy" bit reports correct stataus, see STAR 9001165532 */
	read_aux_reg(r);

	/* Important to wait for flush to complete */
	while (read_aux_reg(r) & SLC_CTRL_BUSY);
}

static inline void arc_slc_disable(void)
{
	const int r = ARC_REG_SLC_CTRL;

	slc_entire_op(OP_FLUSH_N_INV);
	write_aux_reg(r, read_aux_reg(r) | SLC_CTRL_DIS);
}

static inline void arc_slc_enable(void)
{
	const int r = ARC_REG_SLC_CTRL;

	write_aux_reg(r, read_aux_reg(r) & ~SLC_CTRL_DIS);
}

/***********************************************************
 * Exported APIs
 */

void flush_dcache_folio(struct folio *folio)
{
	clear_bit(PG_dc_clean, &folio->flags.f);
	return;
}
EXPORT_SYMBOL(flush_dcache_folio);

void flush_dcache_page(struct page *page)
{
	return flush_dcache_folio(page_folio(page));
}
EXPORT_SYMBOL(flush_dcache_page);

/*
 * DMA ops for systems with L1 cache only
 * Make memory coherent with L1 cache by flushing/invalidating L1 lines
 */
static void __dma_cache_wback_inv_l1(phys_addr_t start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_FLUSH_N_INV);
}

static void __dma_cache_inv_l1(phys_addr_t start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_INV);
}

static void __dma_cache_wback_l1(phys_addr_t start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_FLUSH);
}

/*
 * DMA ops for systems with both L1 and L2 caches, but without IOC
 * Both L1 and L2 lines need to be explicitly flushed/invalidated
 */
static void __dma_cache_wback_inv_slc(phys_addr_t start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_FLUSH_N_INV);
	slc_op(start, sz, OP_FLUSH_N_INV);
}

static void __dma_cache_inv_slc(phys_addr_t start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_INV);
	slc_op(start, sz, OP_INV);
}

static void __dma_cache_wback_slc(phys_addr_t start, unsigned long sz)
{
	__dc_line_op_k(start, sz, OP_FLUSH);
	slc_op(start, sz, OP_FLUSH);
}

/*
 * Exported DMA API
 */
void dma_cache_wback_inv(phys_addr_t start, unsigned long sz)
{
	__dma_cache_wback_inv(start, sz);
}
EXPORT_SYMBOL(dma_cache_wback_inv);

void dma_cache_inv(phys_addr_t start, unsigned long sz)
{
	__dma_cache_inv(start, sz);
}
EXPORT_SYMBOL(dma_cache_inv);

void dma_cache_wback(phys_addr_t start, unsigned long sz)
{
	__dma_cache_wback(start, sz);
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
	unsigned int tot_sz;

	WARN(kstart < TASK_SIZE, "%s() can't handle user vaddr", __func__);

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
		unsigned int off, sz;
		unsigned long phy, pfn;

		off = kstart % PAGE_SIZE;
		pfn = vmalloc_to_pfn((void *)kstart);
		phy = (pfn << PAGE_SHIFT) + off;
		sz = min_t(unsigned int, tot_sz, PAGE_SIZE - off);
		__sync_icache_dcache(phy, kstart, sz);
		kstart += sz;
		tot_sz -= sz;
	}
}
EXPORT_SYMBOL(flush_icache_range);

/*
 * General purpose helper to make I and D cache lines consistent.
 * @paddr is phy addr of region
 * @vaddr is typically user vaddr (breakpoint) or kernel vaddr (vmalloc)
 *    However in one instance, when called by kprobe (for a breakpt in
 *    builtin kernel code) @vaddr will be paddr only, meaning CDU operation will
 *    use a paddr to index the cache (despite VIPT). This is fine since a
 *    builtin kernel page will not have any virtual mappings.
 *    kprobe on loadable module will be kernel vaddr.
 */
void __sync_icache_dcache(phys_addr_t paddr, unsigned long vaddr, int len)
{
	__dc_line_op(paddr, vaddr, len, OP_FLUSH_N_INV);
	__ic_line_inv_vaddr(paddr, vaddr, len);
}

/* wrapper to compile time eliminate alignment checks in flush loop */
void __inv_icache_pages(phys_addr_t paddr, unsigned long vaddr, unsigned nr)
{
	__ic_line_inv_vaddr(paddr, vaddr, nr * PAGE_SIZE);
}

/*
 * wrapper to clearout kernel or userspace mappings of a page
 * For kernel mappings @vaddr == @paddr
 */
void __flush_dcache_pages(phys_addr_t paddr, unsigned long vaddr, unsigned nr)
{
	__dc_line_op(paddr, vaddr & PAGE_MASK, nr * PAGE_SIZE, OP_FLUSH_N_INV);
}

noinline void flush_cache_all(void)
{
	unsigned long flags;

	local_irq_save(flags);

	__ic_entire_inv();
	__dc_entire_op(OP_FLUSH_N_INV);

	local_irq_restore(flags);

}

void copy_user_highpage(struct page *to, struct page *from,
	unsigned long u_vaddr, struct vm_area_struct *vma)
{
	struct folio *src = page_folio(from);
	struct folio *dst = page_folio(to);
	void *kfrom = kmap_atomic(from);
	void *kto = kmap_atomic(to);

	copy_page(kto, kfrom);

	clear_bit(PG_dc_clean, &dst->flags.f);
	clear_bit(PG_dc_clean, &src->flags.f);

	kunmap_atomic(kto);
	kunmap_atomic(kfrom);
}

void clear_user_page(void *to, unsigned long u_vaddr, struct page *page)
{
	struct folio *folio = page_folio(page);
	clear_page(to);
	clear_bit(PG_dc_clean, &folio->flags.f);
}
EXPORT_SYMBOL(clear_user_page);

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

/*
 * IO-Coherency (IOC) setup rules:
 *
 * 1. Needs to be at system level, so only once by Master core
 *    Non-Masters need not be accessing caches at that time
 *    - They are either HALT_ON_RESET and kick started much later or
 *    - if run on reset, need to ensure that arc_platform_smp_wait_to_boot()
 *      doesn't perturb caches or coherency unit
 *
 * 2. caches (L1 and SLC) need to be purged (flush+inv) before setting up IOC,
 *    otherwise any straggler data might behave strangely post IOC enabling
 *
 * 3. All Caches need to be disabled when setting up IOC to elide any in-flight
 *    Coherency transactions
 */
static noinline void __init arc_ioc_setup(void)
{
	unsigned int ioc_base, mem_sz;

	/*
	 * If IOC was already enabled (due to bootloader) it technically needs to
	 * be reconfigured with aperture base,size corresponding to Linux memory map
	 * which will certainly be different than uboot's. But disabling and
	 * reenabling IOC when DMA might be potentially active is tricky business.
	 * To avoid random memory issues later, just panic here and ask user to
	 * upgrade bootloader to one which doesn't enable IOC
	 */
	if (read_aux_reg(ARC_REG_IO_COH_ENABLE) & ARC_IO_COH_ENABLE_BIT)
		panic("IOC already enabled, please upgrade bootloader!\n");

	if (!ioc_enable)
		return;

	/* Flush + invalidate + disable L1 dcache */
	__dc_disable();

	/* Flush + invalidate SLC */
	if (read_aux_reg(ARC_REG_SLC_BCR))
		slc_entire_op(OP_FLUSH_N_INV);

	/*
	 * currently IOC Aperture covers entire DDR
	 * TBD: fix for PGU + 1GB of low mem
	 * TBD: fix for PAE
	 */
	mem_sz = arc_get_mem_sz();

	if (!is_power_of_2(mem_sz) || mem_sz < 4096)
		panic("IOC Aperture size must be power of 2 larger than 4KB");

	/*
	 * IOC Aperture size decoded as 2 ^ (SIZE + 2) KB,
	 * so setting 0x11 implies 512MB, 0x12 implies 1GB...
	 */
	write_aux_reg(ARC_REG_IO_COH_AP0_SIZE, order_base_2(mem_sz >> 10) - 2);

	/* for now assume kernel base is start of IOC aperture */
	ioc_base = CONFIG_LINUX_RAM_BASE;

	if (ioc_base % mem_sz != 0)
		panic("IOC Aperture start must be aligned to the size of the aperture");

	write_aux_reg(ARC_REG_IO_COH_AP0_BASE, ioc_base >> 12);
	write_aux_reg(ARC_REG_IO_COH_PARTIAL, ARC_IO_COH_PARTIAL_BIT);
	write_aux_reg(ARC_REG_IO_COH_ENABLE, ARC_IO_COH_ENABLE_BIT);

	/* Re-enable L1 dcache */
	__dc_enable();
}

/*
 * Cache related boot time checks/setups only needed on master CPU:
 *  - Geometry checks (kernel build and hardware agree: e.g. L1_CACHE_BYTES)
 *    Assume SMP only, so all cores will have same cache config. A check on
 *    one core suffices for all
 *  - IOC setup / dma callbacks only need to be done once
 */
static noinline void __init arc_cache_init_master(void)
{
	if (IS_ENABLED(CONFIG_ARC_HAS_ICACHE)) {
		struct cpuinfo_arc_cache *ic = &ic_info;

		if (!ic->line_len)
			panic("cache support enabled but non-existent cache\n");

		if (ic->line_len != L1_CACHE_BYTES)
			panic("ICache line [%d] != kernel Config [%d]",
			      ic->line_len, L1_CACHE_BYTES);

		/*
		 * In MMU v4 (HS38x) the aliasing icache config uses IVIL/PTAG
		 * pair to provide vaddr/paddr respectively, just as in MMU v3
		 */
		if (is_isa_arcv2() && ic->colors > 1)
			_cache_line_loop_ic_fn = __cache_line_loop_v3;
		else
			_cache_line_loop_ic_fn = __cache_line_loop;
	}

	if (IS_ENABLED(CONFIG_ARC_HAS_DCACHE)) {
		struct cpuinfo_arc_cache *dc = &dc_info;

		if (!dc->line_len)
			panic("cache support enabled but non-existent cache\n");

		if (dc->line_len != L1_CACHE_BYTES)
			panic("DCache line [%d] != kernel Config [%d]",
			      dc->line_len, L1_CACHE_BYTES);

		/* check for D-Cache aliasing on ARCompact: ARCv2 has PIPT */
		if (is_isa_arcompact() && dc->colors > 1) {
			panic("Aliasing VIPT cache not supported\n");
		}
	}

	/*
	 * Check that SMP_CACHE_BYTES (and hence ARCH_DMA_MINALIGN) is larger
	 * or equal to any cache line length.
	 */
	BUILD_BUG_ON_MSG(L1_CACHE_BYTES > SMP_CACHE_BYTES,
			 "SMP_CACHE_BYTES must be >= any cache line length");
	if (is_isa_arcv2() && (l2_line_sz > SMP_CACHE_BYTES))
		panic("L2 Cache line [%d] > kernel Config [%d]\n",
		      l2_line_sz, SMP_CACHE_BYTES);

	/* Note that SLC disable not formally supported till HS 3.0 */
	if (is_isa_arcv2() && l2_line_sz && !slc_enable)
		arc_slc_disable();

	if (is_isa_arcv2() && ioc_exists)
		arc_ioc_setup();

	if (is_isa_arcv2() && l2_line_sz && slc_enable) {
		__dma_cache_wback_inv = __dma_cache_wback_inv_slc;
		__dma_cache_inv = __dma_cache_inv_slc;
		__dma_cache_wback = __dma_cache_wback_slc;
	} else {
		__dma_cache_wback_inv = __dma_cache_wback_inv_l1;
		__dma_cache_inv = __dma_cache_inv_l1;
		__dma_cache_wback = __dma_cache_wback_l1;
	}
	/*
	 * In case of IOC (say IOC+SLC case), pointers above could still be set
	 * but end up not being relevant as the first function in chain is not
	 * called at all for devices using coherent DMA.
	 *     arch_sync_dma_for_cpu() -> dma_cache_*() -> __dma_cache_*()
	 */
}

void __ref arc_cache_init(void)
{
	unsigned int __maybe_unused cpu = smp_processor_id();

	if (!cpu)
		arc_cache_init_master();

	/*
	 * In PAE regime, TLB and cache maintenance ops take wider addresses
	 * And even if PAE is not enabled in kernel, the upper 32-bits still need
	 * to be zeroed to keep the ops sane.
	 * As an optimization for more common !PAE enabled case, zero them out
	 * once at init, rather than checking/setting to 0 for every runtime op
	 */
	if (is_isa_arcv2() && pae40_exist_but_not_enab()) {

		if (IS_ENABLED(CONFIG_ARC_HAS_ICACHE))
			write_aux_reg(ARC_REG_IC_PTAG_HI, 0);

		if (IS_ENABLED(CONFIG_ARC_HAS_DCACHE))
			write_aux_reg(ARC_REG_DC_PTAG_HI, 0);

		if (l2_line_sz) {
			write_aux_reg(ARC_REG_SLC_RGN_END1, 0);
			write_aux_reg(ARC_REG_SLC_RGN_START1, 0);
		}
	}
}
