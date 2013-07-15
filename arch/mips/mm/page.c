/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 04, 05 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2007  Maciej W. Rozycki
 * Copyright (C) 2008  Thiemo Seufer
 * Copyright (C) 2012  MIPS Technologies, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#include <asm/bugs.h>
#include <asm/cacheops.h>
#include <asm/inst.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/prefetch.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/mmu_context.h>
#include <asm/cpu.h>
#include <asm/war.h>

#ifdef CONFIG_SIBYTE_DMA_PAGEOPS
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_dma.h>
#endif

#include <asm/uasm.h>

/* Registers used in the assembled routines. */
#define ZERO 0
#define AT 2
#define A0 4
#define A1 5
#define A2 6
#define T0 8
#define T1 9
#define T2 10
#define T3 11
#define T9 25
#define RA 31

/* Handle labels (which must be positive integers). */
enum label_id {
	label_clear_nopref = 1,
	label_clear_pref,
	label_copy_nopref,
	label_copy_pref_both,
	label_copy_pref_store,
};

UASM_L_LA(_clear_nopref)
UASM_L_LA(_clear_pref)
UASM_L_LA(_copy_nopref)
UASM_L_LA(_copy_pref_both)
UASM_L_LA(_copy_pref_store)

/* We need one branch and therefore one relocation per target label. */
static struct uasm_label __cpuinitdata labels[5];
static struct uasm_reloc __cpuinitdata relocs[5];

#define cpu_is_r4600_v1_x()	((read_c0_prid() & 0xfffffff0) == 0x00002010)
#define cpu_is_r4600_v2_x()	((read_c0_prid() & 0xfffffff0) == 0x00002020)

static int pref_bias_clear_store __cpuinitdata;
static int pref_bias_copy_load __cpuinitdata;
static int pref_bias_copy_store __cpuinitdata;

static u32 pref_src_mode __cpuinitdata;
static u32 pref_dst_mode __cpuinitdata;

static int clear_word_size __cpuinitdata;
static int copy_word_size __cpuinitdata;

static int half_clear_loop_size __cpuinitdata;
static int half_copy_loop_size __cpuinitdata;

static int cache_line_size __cpuinitdata;
#define cache_line_mask() (cache_line_size - 1)

static inline void __cpuinit
pg_addiu(u32 **buf, unsigned int reg1, unsigned int reg2, unsigned int off)
{
	if (cpu_has_64bit_gp_regs && DADDI_WAR && r4k_daddiu_bug()) {
		if (off > 0x7fff) {
			uasm_i_lui(buf, T9, uasm_rel_hi(off));
			uasm_i_addiu(buf, T9, T9, uasm_rel_lo(off));
		} else
			uasm_i_addiu(buf, T9, ZERO, off);
		uasm_i_daddu(buf, reg1, reg2, T9);
	} else {
		if (off > 0x7fff) {
			uasm_i_lui(buf, T9, uasm_rel_hi(off));
			uasm_i_addiu(buf, T9, T9, uasm_rel_lo(off));
			UASM_i_ADDU(buf, reg1, reg2, T9);
		} else
			UASM_i_ADDIU(buf, reg1, reg2, off);
	}
}

static void __cpuinit set_prefetch_parameters(void)
{
	if (cpu_has_64bit_gp_regs || cpu_has_64bit_zero_reg)
		clear_word_size = 8;
	else
		clear_word_size = 4;

	if (cpu_has_64bit_gp_regs)
		copy_word_size = 8;
	else
		copy_word_size = 4;

	/*
	 * The pref's used here are using "streaming" hints, which cause the
	 * copied data to be kicked out of the cache sooner.  A page copy often
	 * ends up copying a lot more data than is commonly used, so this seems
	 * to make sense in terms of reducing cache pollution, but I've no real
	 * performance data to back this up.
	 */
	if (cpu_has_prefetch) {
		/*
		 * XXX: Most prefetch bias values in here are based on
		 * guesswork.
		 */
		cache_line_size = cpu_dcache_line_size();
		switch (current_cpu_type()) {
		case CPU_R5500:
		case CPU_TX49XX:
			/* These processors only support the Pref_Load. */
			pref_bias_copy_load = 256;
			break;

		case CPU_R10000:
		case CPU_R12000:
		case CPU_R14000:
			/*
			 * Those values have been experimentally tuned for an
			 * Origin 200.
			 */
			pref_bias_clear_store = 512;
			pref_bias_copy_load = 256;
			pref_bias_copy_store = 256;
			pref_src_mode = Pref_LoadStreamed;
			pref_dst_mode = Pref_StoreStreamed;
			break;

		case CPU_SB1:
		case CPU_SB1A:
			pref_bias_clear_store = 128;
			pref_bias_copy_load = 128;
			pref_bias_copy_store = 128;
			/*
			 * SB1 pass1 Pref_LoadStreamed/Pref_StoreStreamed
			 * hints are broken.
			 */
			if (current_cpu_type() == CPU_SB1 &&
			    (current_cpu_data.processor_id & 0xff) < 0x02) {
				pref_src_mode = Pref_Load;
				pref_dst_mode = Pref_Store;
			} else {
				pref_src_mode = Pref_LoadStreamed;
				pref_dst_mode = Pref_StoreStreamed;
			}
			break;

		default:
			pref_bias_clear_store = 128;
			pref_bias_copy_load = 256;
			pref_bias_copy_store = 128;
			pref_src_mode = Pref_LoadStreamed;
			pref_dst_mode = Pref_PrepareForStore;
			break;
		}
	} else {
		if (cpu_has_cache_cdex_s)
			cache_line_size = cpu_scache_line_size();
		else if (cpu_has_cache_cdex_p)
			cache_line_size = cpu_dcache_line_size();
	}
	/*
	 * Too much unrolling will overflow the available space in
	 * clear_space_array / copy_page_array.
	 */
	half_clear_loop_size = min(16 * clear_word_size,
				   max(cache_line_size >> 1,
				       4 * clear_word_size));
	half_copy_loop_size = min(16 * copy_word_size,
				  max(cache_line_size >> 1,
				      4 * copy_word_size));
}

static void __cpuinit build_clear_store(u32 **buf, int off)
{
	if (cpu_has_64bit_gp_regs || cpu_has_64bit_zero_reg) {
		uasm_i_sd(buf, ZERO, off, A0);
	} else {
		uasm_i_sw(buf, ZERO, off, A0);
	}
}

static inline void __cpuinit build_clear_pref(u32 **buf, int off)
{
	if (off & cache_line_mask())
		return;

	if (pref_bias_clear_store) {
		uasm_i_pref(buf, pref_dst_mode, pref_bias_clear_store + off,
			    A0);
	} else if (cache_line_size == (half_clear_loop_size << 1)) {
		if (cpu_has_cache_cdex_s) {
			uasm_i_cache(buf, Create_Dirty_Excl_SD, off, A0);
		} else if (cpu_has_cache_cdex_p) {
			if (R4600_V1_HIT_CACHEOP_WAR && cpu_is_r4600_v1_x()) {
				uasm_i_nop(buf);
				uasm_i_nop(buf);
				uasm_i_nop(buf);
				uasm_i_nop(buf);
			}

			if (R4600_V2_HIT_CACHEOP_WAR && cpu_is_r4600_v2_x())
				uasm_i_lw(buf, ZERO, ZERO, AT);

			uasm_i_cache(buf, Create_Dirty_Excl_D, off, A0);
		}
	}
}

extern u32 __clear_page_start;
extern u32 __clear_page_end;
extern u32 __copy_page_start;
extern u32 __copy_page_end;

void __cpuinit build_clear_page(void)
{
	int off;
	u32 *buf = &__clear_page_start;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	int i;
	static atomic_t run_once = ATOMIC_INIT(0);

	if (atomic_xchg(&run_once, 1)) {
		return;
	}

	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	set_prefetch_parameters();

	/*
	 * This algorithm makes the following assumptions:
	 *   - The prefetch bias is a multiple of 2 words.
	 *   - The prefetch bias is less than one page.
	 */
	BUG_ON(pref_bias_clear_store % (2 * clear_word_size));
	BUG_ON(PAGE_SIZE < pref_bias_clear_store);

	off = PAGE_SIZE - pref_bias_clear_store;
	if (off > 0xffff || !pref_bias_clear_store)
		pg_addiu(&buf, A2, A0, off);
	else
		uasm_i_ori(&buf, A2, A0, off);

	if (R4600_V2_HIT_CACHEOP_WAR && cpu_is_r4600_v2_x())
		uasm_i_lui(&buf, AT, 0xa000);

	off = cache_line_size ? min(8, pref_bias_clear_store / cache_line_size)
				* cache_line_size : 0;
	while (off) {
		build_clear_pref(&buf, -off);
		off -= cache_line_size;
	}
	uasm_l_clear_pref(&l, buf);
	do {
		build_clear_pref(&buf, off);
		build_clear_store(&buf, off);
		off += clear_word_size;
	} while (off < half_clear_loop_size);
	pg_addiu(&buf, A0, A0, 2 * off);
	off = -off;
	do {
		build_clear_pref(&buf, off);
		if (off == -clear_word_size)
			uasm_il_bne(&buf, &r, A0, A2, label_clear_pref);
		build_clear_store(&buf, off);
		off += clear_word_size;
	} while (off < 0);

	if (pref_bias_clear_store) {
		pg_addiu(&buf, A2, A0, pref_bias_clear_store);
		uasm_l_clear_nopref(&l, buf);
		off = 0;
		do {
			build_clear_store(&buf, off);
			off += clear_word_size;
		} while (off < half_clear_loop_size);
		pg_addiu(&buf, A0, A0, 2 * off);
		off = -off;
		do {
			if (off == -clear_word_size)
				uasm_il_bne(&buf, &r, A0, A2,
					    label_clear_nopref);
			build_clear_store(&buf, off);
			off += clear_word_size;
		} while (off < 0);
	}

	uasm_i_jr(&buf, RA);
	uasm_i_nop(&buf);

	BUG_ON(buf > &__clear_page_end);

	uasm_resolve_relocs(relocs, labels);

	pr_debug("Synthesized clear page handler (%u instructions).\n",
		 (u32)(buf - &__clear_page_start));

	pr_debug("\t.set push\n");
	pr_debug("\t.set noreorder\n");
	for (i = 0; i < (buf - &__clear_page_start); i++)
		pr_debug("\t.word 0x%08x\n", (&__clear_page_start)[i]);
	pr_debug("\t.set pop\n");
}

static void __cpuinit build_copy_load(u32 **buf, int reg, int off)
{
	if (cpu_has_64bit_gp_regs) {
		uasm_i_ld(buf, reg, off, A1);
	} else {
		uasm_i_lw(buf, reg, off, A1);
	}
}

static void __cpuinit build_copy_store(u32 **buf, int reg, int off)
{
	if (cpu_has_64bit_gp_regs) {
		uasm_i_sd(buf, reg, off, A0);
	} else {
		uasm_i_sw(buf, reg, off, A0);
	}
}

static inline void build_copy_load_pref(u32 **buf, int off)
{
	if (off & cache_line_mask())
		return;

	if (pref_bias_copy_load)
		uasm_i_pref(buf, pref_src_mode, pref_bias_copy_load + off, A1);
}

static inline void build_copy_store_pref(u32 **buf, int off)
{
	if (off & cache_line_mask())
		return;

	if (pref_bias_copy_store) {
		uasm_i_pref(buf, pref_dst_mode, pref_bias_copy_store + off,
			    A0);
	} else if (cache_line_size == (half_copy_loop_size << 1)) {
		if (cpu_has_cache_cdex_s) {
			uasm_i_cache(buf, Create_Dirty_Excl_SD, off, A0);
		} else if (cpu_has_cache_cdex_p) {
			if (R4600_V1_HIT_CACHEOP_WAR && cpu_is_r4600_v1_x()) {
				uasm_i_nop(buf);
				uasm_i_nop(buf);
				uasm_i_nop(buf);
				uasm_i_nop(buf);
			}

			if (R4600_V2_HIT_CACHEOP_WAR && cpu_is_r4600_v2_x())
				uasm_i_lw(buf, ZERO, ZERO, AT);

			uasm_i_cache(buf, Create_Dirty_Excl_D, off, A0);
		}
	}
}

void __cpuinit build_copy_page(void)
{
	int off;
	u32 *buf = &__copy_page_start;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	int i;
	static atomic_t run_once = ATOMIC_INIT(0);

	if (atomic_xchg(&run_once, 1)) {
		return;
	}

	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	set_prefetch_parameters();

	/*
	 * This algorithm makes the following assumptions:
	 *   - All prefetch biases are multiples of 8 words.
	 *   - The prefetch biases are less than one page.
	 *   - The store prefetch bias isn't greater than the load
	 *     prefetch bias.
	 */
	BUG_ON(pref_bias_copy_load % (8 * copy_word_size));
	BUG_ON(pref_bias_copy_store % (8 * copy_word_size));
	BUG_ON(PAGE_SIZE < pref_bias_copy_load);
	BUG_ON(pref_bias_copy_store > pref_bias_copy_load);

	off = PAGE_SIZE - pref_bias_copy_load;
	if (off > 0xffff || !pref_bias_copy_load)
		pg_addiu(&buf, A2, A0, off);
	else
		uasm_i_ori(&buf, A2, A0, off);

	if (R4600_V2_HIT_CACHEOP_WAR && cpu_is_r4600_v2_x())
		uasm_i_lui(&buf, AT, 0xa000);

	off = cache_line_size ? min(8, pref_bias_copy_load / cache_line_size) *
				cache_line_size : 0;
	while (off) {
		build_copy_load_pref(&buf, -off);
		off -= cache_line_size;
	}
	off = cache_line_size ? min(8, pref_bias_copy_store / cache_line_size) *
				cache_line_size : 0;
	while (off) {
		build_copy_store_pref(&buf, -off);
		off -= cache_line_size;
	}
	uasm_l_copy_pref_both(&l, buf);
	do {
		build_copy_load_pref(&buf, off);
		build_copy_load(&buf, T0, off);
		build_copy_load_pref(&buf, off + copy_word_size);
		build_copy_load(&buf, T1, off + copy_word_size);
		build_copy_load_pref(&buf, off + 2 * copy_word_size);
		build_copy_load(&buf, T2, off + 2 * copy_word_size);
		build_copy_load_pref(&buf, off + 3 * copy_word_size);
		build_copy_load(&buf, T3, off + 3 * copy_word_size);
		build_copy_store_pref(&buf, off);
		build_copy_store(&buf, T0, off);
		build_copy_store_pref(&buf, off + copy_word_size);
		build_copy_store(&buf, T1, off + copy_word_size);
		build_copy_store_pref(&buf, off + 2 * copy_word_size);
		build_copy_store(&buf, T2, off + 2 * copy_word_size);
		build_copy_store_pref(&buf, off + 3 * copy_word_size);
		build_copy_store(&buf, T3, off + 3 * copy_word_size);
		off += 4 * copy_word_size;
	} while (off < half_copy_loop_size);
	pg_addiu(&buf, A1, A1, 2 * off);
	pg_addiu(&buf, A0, A0, 2 * off);
	off = -off;
	do {
		build_copy_load_pref(&buf, off);
		build_copy_load(&buf, T0, off);
		build_copy_load_pref(&buf, off + copy_word_size);
		build_copy_load(&buf, T1, off + copy_word_size);
		build_copy_load_pref(&buf, off + 2 * copy_word_size);
		build_copy_load(&buf, T2, off + 2 * copy_word_size);
		build_copy_load_pref(&buf, off + 3 * copy_word_size);
		build_copy_load(&buf, T3, off + 3 * copy_word_size);
		build_copy_store_pref(&buf, off);
		build_copy_store(&buf, T0, off);
		build_copy_store_pref(&buf, off + copy_word_size);
		build_copy_store(&buf, T1, off + copy_word_size);
		build_copy_store_pref(&buf, off + 2 * copy_word_size);
		build_copy_store(&buf, T2, off + 2 * copy_word_size);
		build_copy_store_pref(&buf, off + 3 * copy_word_size);
		if (off == -(4 * copy_word_size))
			uasm_il_bne(&buf, &r, A2, A0, label_copy_pref_both);
		build_copy_store(&buf, T3, off + 3 * copy_word_size);
		off += 4 * copy_word_size;
	} while (off < 0);

	if (pref_bias_copy_load - pref_bias_copy_store) {
		pg_addiu(&buf, A2, A0,
			 pref_bias_copy_load - pref_bias_copy_store);
		uasm_l_copy_pref_store(&l, buf);
		off = 0;
		do {
			build_copy_load(&buf, T0, off);
			build_copy_load(&buf, T1, off + copy_word_size);
			build_copy_load(&buf, T2, off + 2 * copy_word_size);
			build_copy_load(&buf, T3, off + 3 * copy_word_size);
			build_copy_store_pref(&buf, off);
			build_copy_store(&buf, T0, off);
			build_copy_store_pref(&buf, off + copy_word_size);
			build_copy_store(&buf, T1, off + copy_word_size);
			build_copy_store_pref(&buf, off + 2 * copy_word_size);
			build_copy_store(&buf, T2, off + 2 * copy_word_size);
			build_copy_store_pref(&buf, off + 3 * copy_word_size);
			build_copy_store(&buf, T3, off + 3 * copy_word_size);
			off += 4 * copy_word_size;
		} while (off < half_copy_loop_size);
		pg_addiu(&buf, A1, A1, 2 * off);
		pg_addiu(&buf, A0, A0, 2 * off);
		off = -off;
		do {
			build_copy_load(&buf, T0, off);
			build_copy_load(&buf, T1, off + copy_word_size);
			build_copy_load(&buf, T2, off + 2 * copy_word_size);
			build_copy_load(&buf, T3, off + 3 * copy_word_size);
			build_copy_store_pref(&buf, off);
			build_copy_store(&buf, T0, off);
			build_copy_store_pref(&buf, off + copy_word_size);
			build_copy_store(&buf, T1, off + copy_word_size);
			build_copy_store_pref(&buf, off + 2 * copy_word_size);
			build_copy_store(&buf, T2, off + 2 * copy_word_size);
			build_copy_store_pref(&buf, off + 3 * copy_word_size);
			if (off == -(4 * copy_word_size))
				uasm_il_bne(&buf, &r, A2, A0,
					    label_copy_pref_store);
			build_copy_store(&buf, T3, off + 3 * copy_word_size);
			off += 4 * copy_word_size;
		} while (off < 0);
	}

	if (pref_bias_copy_store) {
		pg_addiu(&buf, A2, A0, pref_bias_copy_store);
		uasm_l_copy_nopref(&l, buf);
		off = 0;
		do {
			build_copy_load(&buf, T0, off);
			build_copy_load(&buf, T1, off + copy_word_size);
			build_copy_load(&buf, T2, off + 2 * copy_word_size);
			build_copy_load(&buf, T3, off + 3 * copy_word_size);
			build_copy_store(&buf, T0, off);
			build_copy_store(&buf, T1, off + copy_word_size);
			build_copy_store(&buf, T2, off + 2 * copy_word_size);
			build_copy_store(&buf, T3, off + 3 * copy_word_size);
			off += 4 * copy_word_size;
		} while (off < half_copy_loop_size);
		pg_addiu(&buf, A1, A1, 2 * off);
		pg_addiu(&buf, A0, A0, 2 * off);
		off = -off;
		do {
			build_copy_load(&buf, T0, off);
			build_copy_load(&buf, T1, off + copy_word_size);
			build_copy_load(&buf, T2, off + 2 * copy_word_size);
			build_copy_load(&buf, T3, off + 3 * copy_word_size);
			build_copy_store(&buf, T0, off);
			build_copy_store(&buf, T1, off + copy_word_size);
			build_copy_store(&buf, T2, off + 2 * copy_word_size);
			if (off == -(4 * copy_word_size))
				uasm_il_bne(&buf, &r, A2, A0,
					    label_copy_nopref);
			build_copy_store(&buf, T3, off + 3 * copy_word_size);
			off += 4 * copy_word_size;
		} while (off < 0);
	}

	uasm_i_jr(&buf, RA);
	uasm_i_nop(&buf);

	BUG_ON(buf > &__copy_page_end);

	uasm_resolve_relocs(relocs, labels);

	pr_debug("Synthesized copy page handler (%u instructions).\n",
		 (u32)(buf - &__copy_page_start));

	pr_debug("\t.set push\n");
	pr_debug("\t.set noreorder\n");
	for (i = 0; i < (buf - &__copy_page_start); i++)
		pr_debug("\t.word 0x%08x\n", (&__copy_page_start)[i]);
	pr_debug("\t.set pop\n");
}

#ifdef CONFIG_SIBYTE_DMA_PAGEOPS
extern void clear_page_cpu(void *page);
extern void copy_page_cpu(void *to, void *from);

/*
 * Pad descriptors to cacheline, since each is exclusively owned by a
 * particular CPU.
 */
struct dmadscr {
	u64 dscr_a;
	u64 dscr_b;
	u64 pad_a;
	u64 pad_b;
} ____cacheline_aligned_in_smp page_descr[DM_NUM_CHANNELS];

void sb1_dma_init(void)
{
	int i;

	for (i = 0; i < DM_NUM_CHANNELS; i++) {
		const u64 base_val = CPHYSADDR((unsigned long)&page_descr[i]) |
				     V_DM_DSCR_BASE_RINGSZ(1);
		void *base_reg = IOADDR(A_DM_REGISTER(i, R_DM_DSCR_BASE));

		__raw_writeq(base_val, base_reg);
		__raw_writeq(base_val | M_DM_DSCR_BASE_RESET, base_reg);
		__raw_writeq(base_val | M_DM_DSCR_BASE_ENABL, base_reg);
	}
}

void clear_page(void *page)
{
	u64 to_phys = CPHYSADDR((unsigned long)page);
	unsigned int cpu = smp_processor_id();

	/* if the page is not in KSEG0, use old way */
	if ((long)KSEGX((unsigned long)page) != (long)CKSEG0)
		return clear_page_cpu(page);

	page_descr[cpu].dscr_a = to_phys | M_DM_DSCRA_ZERO_MEM |
				 M_DM_DSCRA_L2C_DEST | M_DM_DSCRA_INTERRUPT;
	page_descr[cpu].dscr_b = V_DM_DSCRB_SRC_LENGTH(PAGE_SIZE);
	__raw_writeq(1, IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_COUNT)));

	/*
	 * Don't really want to do it this way, but there's no
	 * reliable way to delay completion detection.
	 */
	while (!(__raw_readq(IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE_DEBUG)))
		 & M_DM_DSCR_BASE_INTERRUPT))
		;
	__raw_readq(IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE)));
}

void copy_page(void *to, void *from)
{
	u64 from_phys = CPHYSADDR((unsigned long)from);
	u64 to_phys = CPHYSADDR((unsigned long)to);
	unsigned int cpu = smp_processor_id();

	/* if any page is not in KSEG0, use old way */
	if ((long)KSEGX((unsigned long)to) != (long)CKSEG0
	    || (long)KSEGX((unsigned long)from) != (long)CKSEG0)
		return copy_page_cpu(to, from);

	page_descr[cpu].dscr_a = to_phys | M_DM_DSCRA_L2C_DEST |
				 M_DM_DSCRA_INTERRUPT;
	page_descr[cpu].dscr_b = from_phys | V_DM_DSCRB_SRC_LENGTH(PAGE_SIZE);
	__raw_writeq(1, IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_COUNT)));

	/*
	 * Don't really want to do it this way, but there's no
	 * reliable way to delay completion detection.
	 */
	while (!(__raw_readq(IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE_DEBUG)))
		 & M_DM_DSCR_BASE_INTERRUPT))
		;
	__raw_readq(IOADDR(A_DM_REGISTER(cpu, R_DM_DSCR_BASE)));
}

#endif /* CONFIG_SIBYTE_DMA_PAGEOPS */
