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
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>

#include <asm/bugs.h>
#include <asm/cacheops.h>
#include <asm/cpu-type.h>
#include <asm/inst.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/prefetch.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/mmu_context.h>
#include <asm/regdef.h>
#include <asm/cpu.h>

#ifdef CONFIG_SIBYTE_DMA_PAGEOPS
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_dma.h>
#endif

#include <asm/uasm.h>

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
static struct uasm_label labels[5];
static struct uasm_reloc relocs[5];

#define cpu_is_r4600_v1_x()	((read_c0_prid() & 0xfffffff0) == 0x00002010)
#define cpu_is_r4600_v2_x()	((read_c0_prid() & 0xfffffff0) == 0x00002020)

/*
 * R6 has a limited offset of the pref instruction.
 * Skip it if the offset is more than 9 bits.
 */
#define _uasm_i_pref(a, b, c, d)		\
do {						\
	if (cpu_has_mips_r6) {			\
		if (c <= 0xff && c >= -0x100)	\
			uasm_i_pref(a, b, c, d);\
	} else {				\
		uasm_i_pref(a, b, c, d);	\
	}					\
} while(0)

static int pref_bias_clear_store;
static int pref_bias_copy_load;
static int pref_bias_copy_store;

static u32 pref_src_mode;
static u32 pref_dst_mode;

static int clear_word_size;
static int copy_word_size;

static int half_clear_loop_size;
static int half_copy_loop_size;

static int cache_line_size;
#define cache_line_mask() (cache_line_size - 1)

static inline void
pg_addiu(u32 **buf, unsigned int reg1, unsigned int reg2, unsigned int off)
{
	if (cpu_has_64bit_gp_regs &&
	    IS_ENABLED(CONFIG_CPU_DADDI_WORKAROUNDS) &&
	    r4k_daddiu_bug()) {
		if (off > 0x7fff) {
			uasm_i_lui(buf, GPR_T9, uasm_rel_hi(off));
			uasm_i_addiu(buf, GPR_T9, GPR_T9, uasm_rel_lo(off));
		} else
			uasm_i_addiu(buf, GPR_T9, GPR_ZERO, off);
		uasm_i_daddu(buf, reg1, reg2, GPR_T9);
	} else {
		if (off > 0x7fff) {
			uasm_i_lui(buf, GPR_T9, uasm_rel_hi(off));
			uasm_i_addiu(buf, GPR_T9, GPR_T9, uasm_rel_lo(off));
			UASM_i_ADDU(buf, reg1, reg2, GPR_T9);
		} else
			UASM_i_ADDIU(buf, reg1, reg2, off);
	}
}

static void set_prefetch_parameters(void)
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
		case CPU_R16000:
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

		case CPU_LOONGSON64:
			/* Loongson-3 only support the Pref_Load/Pref_Store. */
			pref_bias_clear_store = 128;
			pref_bias_copy_load = 128;
			pref_bias_copy_store = 128;
			pref_src_mode = Pref_Load;
			pref_dst_mode = Pref_Store;
			break;

		default:
			pref_bias_clear_store = 128;
			pref_bias_copy_load = 256;
			pref_bias_copy_store = 128;
			pref_src_mode = Pref_LoadStreamed;
			if (cpu_has_mips_r6)
				/*
				 * Bit 30 (Pref_PrepareForStore) has been
				 * removed from MIPS R6. Use bit 5
				 * (Pref_StoreStreamed).
				 */
				pref_dst_mode = Pref_StoreStreamed;
			else
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

static void build_clear_store(u32 **buf, int off)
{
	if (cpu_has_64bit_gp_regs || cpu_has_64bit_zero_reg) {
		uasm_i_sd(buf, GPR_ZERO, off, GPR_A0);
	} else {
		uasm_i_sw(buf, GPR_ZERO, off, GPR_A0);
	}
}

static inline void build_clear_pref(u32 **buf, int off)
{
	if (off & cache_line_mask())
		return;

	if (pref_bias_clear_store) {
		_uasm_i_pref(buf, pref_dst_mode, pref_bias_clear_store + off,
			    GPR_A0);
	} else if (cache_line_size == (half_clear_loop_size << 1)) {
		if (cpu_has_cache_cdex_s) {
			uasm_i_cache(buf, Create_Dirty_Excl_SD, off, GPR_A0);
		} else if (cpu_has_cache_cdex_p) {
			if (IS_ENABLED(CONFIG_WAR_R4600_V1_HIT_CACHEOP) &&
			    cpu_is_r4600_v1_x()) {
				uasm_i_nop(buf);
				uasm_i_nop(buf);
				uasm_i_nop(buf);
				uasm_i_nop(buf);
			}

			if (IS_ENABLED(CONFIG_WAR_R4600_V2_HIT_CACHEOP) &&
			    cpu_is_r4600_v2_x())
				uasm_i_lw(buf, GPR_ZERO, GPR_ZERO, GPR_AT);

			uasm_i_cache(buf, Create_Dirty_Excl_D, off, GPR_A0);
		}
	}
}

extern u32 __clear_page_start;
extern u32 __clear_page_end;
extern u32 __copy_page_start;
extern u32 __copy_page_end;

void build_clear_page(void)
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
		pg_addiu(&buf, GPR_A2, GPR_A0, off);
	else
		uasm_i_ori(&buf, GPR_A2, GPR_A0, off);

	if (IS_ENABLED(CONFIG_WAR_R4600_V2_HIT_CACHEOP) && cpu_is_r4600_v2_x())
		uasm_i_lui(&buf, GPR_AT, uasm_rel_hi(0xa0000000));

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
	pg_addiu(&buf, GPR_A0, GPR_A0, 2 * off);
	off = -off;
	do {
		build_clear_pref(&buf, off);
		if (off == -clear_word_size)
			uasm_il_bne(&buf, &r, GPR_A0, GPR_A2, label_clear_pref);
		build_clear_store(&buf, off);
		off += clear_word_size;
	} while (off < 0);

	if (pref_bias_clear_store) {
		pg_addiu(&buf, GPR_A2, GPR_A0, pref_bias_clear_store);
		uasm_l_clear_nopref(&l, buf);
		off = 0;
		do {
			build_clear_store(&buf, off);
			off += clear_word_size;
		} while (off < half_clear_loop_size);
		pg_addiu(&buf, GPR_A0, GPR_A0, 2 * off);
		off = -off;
		do {
			if (off == -clear_word_size)
				uasm_il_bne(&buf, &r, GPR_A0, GPR_A2,
					    label_clear_nopref);
			build_clear_store(&buf, off);
			off += clear_word_size;
		} while (off < 0);
	}

	uasm_i_jr(&buf, GPR_RA);
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

static void build_copy_load(u32 **buf, int reg, int off)
{
	if (cpu_has_64bit_gp_regs) {
		uasm_i_ld(buf, reg, off, GPR_A1);
	} else {
		uasm_i_lw(buf, reg, off, GPR_A1);
	}
}

static void build_copy_store(u32 **buf, int reg, int off)
{
	if (cpu_has_64bit_gp_regs) {
		uasm_i_sd(buf, reg, off, GPR_A0);
	} else {
		uasm_i_sw(buf, reg, off, GPR_A0);
	}
}

static inline void build_copy_load_pref(u32 **buf, int off)
{
	if (off & cache_line_mask())
		return;

	if (pref_bias_copy_load)
		_uasm_i_pref(buf, pref_src_mode, pref_bias_copy_load + off, GPR_A1);
}

static inline void build_copy_store_pref(u32 **buf, int off)
{
	if (off & cache_line_mask())
		return;

	if (pref_bias_copy_store) {
		_uasm_i_pref(buf, pref_dst_mode, pref_bias_copy_store + off,
			    GPR_A0);
	} else if (cache_line_size == (half_copy_loop_size << 1)) {
		if (cpu_has_cache_cdex_s) {
			uasm_i_cache(buf, Create_Dirty_Excl_SD, off, GPR_A0);
		} else if (cpu_has_cache_cdex_p) {
			if (IS_ENABLED(CONFIG_WAR_R4600_V1_HIT_CACHEOP) &&
			    cpu_is_r4600_v1_x()) {
				uasm_i_nop(buf);
				uasm_i_nop(buf);
				uasm_i_nop(buf);
				uasm_i_nop(buf);
			}

			if (IS_ENABLED(CONFIG_WAR_R4600_V2_HIT_CACHEOP) &&
			    cpu_is_r4600_v2_x())
				uasm_i_lw(buf, GPR_ZERO, GPR_ZERO, GPR_AT);

			uasm_i_cache(buf, Create_Dirty_Excl_D, off, GPR_A0);
		}
	}
}

void build_copy_page(void)
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
		pg_addiu(&buf, GPR_A2, GPR_A0, off);
	else
		uasm_i_ori(&buf, GPR_A2, GPR_A0, off);

	if (IS_ENABLED(CONFIG_WAR_R4600_V2_HIT_CACHEOP) && cpu_is_r4600_v2_x())
		uasm_i_lui(&buf, GPR_AT, uasm_rel_hi(0xa0000000));

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
		build_copy_load(&buf, GPR_T0, off);
		build_copy_load_pref(&buf, off + copy_word_size);
		build_copy_load(&buf, GPR_T1, off + copy_word_size);
		build_copy_load_pref(&buf, off + 2 * copy_word_size);
		build_copy_load(&buf, GPR_T2, off + 2 * copy_word_size);
		build_copy_load_pref(&buf, off + 3 * copy_word_size);
		build_copy_load(&buf, GPR_T3, off + 3 * copy_word_size);
		build_copy_store_pref(&buf, off);
		build_copy_store(&buf, GPR_T0, off);
		build_copy_store_pref(&buf, off + copy_word_size);
		build_copy_store(&buf, GPR_T1, off + copy_word_size);
		build_copy_store_pref(&buf, off + 2 * copy_word_size);
		build_copy_store(&buf, GPR_T2, off + 2 * copy_word_size);
		build_copy_store_pref(&buf, off + 3 * copy_word_size);
		build_copy_store(&buf, GPR_T3, off + 3 * copy_word_size);
		off += 4 * copy_word_size;
	} while (off < half_copy_loop_size);
	pg_addiu(&buf, GPR_A1, GPR_A1, 2 * off);
	pg_addiu(&buf, GPR_A0, GPR_A0, 2 * off);
	off = -off;
	do {
		build_copy_load_pref(&buf, off);
		build_copy_load(&buf, GPR_T0, off);
		build_copy_load_pref(&buf, off + copy_word_size);
		build_copy_load(&buf, GPR_T1, off + copy_word_size);
		build_copy_load_pref(&buf, off + 2 * copy_word_size);
		build_copy_load(&buf, GPR_T2, off + 2 * copy_word_size);
		build_copy_load_pref(&buf, off + 3 * copy_word_size);
		build_copy_load(&buf, GPR_T3, off + 3 * copy_word_size);
		build_copy_store_pref(&buf, off);
		build_copy_store(&buf, GPR_T0, off);
		build_copy_store_pref(&buf, off + copy_word_size);
		build_copy_store(&buf, GPR_T1, off + copy_word_size);
		build_copy_store_pref(&buf, off + 2 * copy_word_size);
		build_copy_store(&buf, GPR_T2, off + 2 * copy_word_size);
		build_copy_store_pref(&buf, off + 3 * copy_word_size);
		if (off == -(4 * copy_word_size))
			uasm_il_bne(&buf, &r, GPR_A2, GPR_A0, label_copy_pref_both);
		build_copy_store(&buf, GPR_T3, off + 3 * copy_word_size);
		off += 4 * copy_word_size;
	} while (off < 0);

	if (pref_bias_copy_load - pref_bias_copy_store) {
		pg_addiu(&buf, GPR_A2, GPR_A0,
			 pref_bias_copy_load - pref_bias_copy_store);
		uasm_l_copy_pref_store(&l, buf);
		off = 0;
		do {
			build_copy_load(&buf, GPR_T0, off);
			build_copy_load(&buf, GPR_T1, off + copy_word_size);
			build_copy_load(&buf, GPR_T2, off + 2 * copy_word_size);
			build_copy_load(&buf, GPR_T3, off + 3 * copy_word_size);
			build_copy_store_pref(&buf, off);
			build_copy_store(&buf, GPR_T0, off);
			build_copy_store_pref(&buf, off + copy_word_size);
			build_copy_store(&buf, GPR_T1, off + copy_word_size);
			build_copy_store_pref(&buf, off + 2 * copy_word_size);
			build_copy_store(&buf, GPR_T2, off + 2 * copy_word_size);
			build_copy_store_pref(&buf, off + 3 * copy_word_size);
			build_copy_store(&buf, GPR_T3, off + 3 * copy_word_size);
			off += 4 * copy_word_size;
		} while (off < half_copy_loop_size);
		pg_addiu(&buf, GPR_A1, GPR_A1, 2 * off);
		pg_addiu(&buf, GPR_A0, GPR_A0, 2 * off);
		off = -off;
		do {
			build_copy_load(&buf, GPR_T0, off);
			build_copy_load(&buf, GPR_T1, off + copy_word_size);
			build_copy_load(&buf, GPR_T2, off + 2 * copy_word_size);
			build_copy_load(&buf, GPR_T3, off + 3 * copy_word_size);
			build_copy_store_pref(&buf, off);
			build_copy_store(&buf, GPR_T0, off);
			build_copy_store_pref(&buf, off + copy_word_size);
			build_copy_store(&buf, GPR_T1, off + copy_word_size);
			build_copy_store_pref(&buf, off + 2 * copy_word_size);
			build_copy_store(&buf, GPR_T2, off + 2 * copy_word_size);
			build_copy_store_pref(&buf, off + 3 * copy_word_size);
			if (off == -(4 * copy_word_size))
				uasm_il_bne(&buf, &r, GPR_A2, GPR_A0,
					    label_copy_pref_store);
			build_copy_store(&buf, GPR_T3, off + 3 * copy_word_size);
			off += 4 * copy_word_size;
		} while (off < 0);
	}

	if (pref_bias_copy_store) {
		pg_addiu(&buf, GPR_A2, GPR_A0, pref_bias_copy_store);
		uasm_l_copy_nopref(&l, buf);
		off = 0;
		do {
			build_copy_load(&buf, GPR_T0, off);
			build_copy_load(&buf, GPR_T1, off + copy_word_size);
			build_copy_load(&buf, GPR_T2, off + 2 * copy_word_size);
			build_copy_load(&buf, GPR_T3, off + 3 * copy_word_size);
			build_copy_store(&buf, GPR_T0, off);
			build_copy_store(&buf, GPR_T1, off + copy_word_size);
			build_copy_store(&buf, GPR_T2, off + 2 * copy_word_size);
			build_copy_store(&buf, GPR_T3, off + 3 * copy_word_size);
			off += 4 * copy_word_size;
		} while (off < half_copy_loop_size);
		pg_addiu(&buf, GPR_A1, GPR_A1, 2 * off);
		pg_addiu(&buf, GPR_A0, GPR_A0, 2 * off);
		off = -off;
		do {
			build_copy_load(&buf, GPR_T0, off);
			build_copy_load(&buf, GPR_T1, off + copy_word_size);
			build_copy_load(&buf, GPR_T2, off + 2 * copy_word_size);
			build_copy_load(&buf, GPR_T3, off + 3 * copy_word_size);
			build_copy_store(&buf, GPR_T0, off);
			build_copy_store(&buf, GPR_T1, off + copy_word_size);
			build_copy_store(&buf, GPR_T2, off + 2 * copy_word_size);
			if (off == -(4 * copy_word_size))
				uasm_il_bne(&buf, &r, GPR_A2, GPR_A0,
					    label_copy_nopref);
			build_copy_store(&buf, GPR_T3, off + 3 * copy_word_size);
			off += 4 * copy_word_size;
		} while (off < 0);
	}

	uasm_i_jr(&buf, GPR_RA);
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
EXPORT_SYMBOL(clear_page);

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
EXPORT_SYMBOL(copy_page);

#endif /* CONFIG_SIBYTE_DMA_PAGEOPS */
