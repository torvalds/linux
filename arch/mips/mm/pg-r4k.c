/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 04, 05 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#include <asm/cacheops.h>
#include <asm/inst.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/prefetch.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/mmu_context.h>
#include <asm/cpu.h>
#include <asm/war.h>

#define half_scache_line_size()	(cpu_scache_line_size() >> 1)
#define cpu_is_r4600_v1_x()	((read_c0_prid() & 0xfffffff0) == 0x00002010)
#define cpu_is_r4600_v2_x()	((read_c0_prid() & 0xfffffff0) == 0x00002020)


/*
 * Maximum sizes:
 *
 * R4000 128 bytes S-cache:		0x58 bytes
 * R4600 v1.7:				0x5c bytes
 * R4600 v2.0:				0x60 bytes
 * With prefetching, 16 byte strides	0xa0 bytes
 */

static unsigned int clear_page_array[0x130 / 4];

void clear_page(void * page) __attribute__((alias("clear_page_array")));

EXPORT_SYMBOL(clear_page);

/*
 * Maximum sizes:
 *
 * R4000 128 bytes S-cache:		0x11c bytes
 * R4600 v1.7:				0x080 bytes
 * R4600 v2.0:				0x07c bytes
 * With prefetching, 16 byte strides	0x0b8 bytes
 */
static unsigned int copy_page_array[0x148 / 4];

void copy_page(void *to, void *from) __attribute__((alias("copy_page_array")));

EXPORT_SYMBOL(copy_page);

/*
 * This is suboptimal for 32-bit kernels; we assume that R10000 is only used
 * with 64-bit kernels.  The prefetch offsets have been experimentally tuned
 * an Origin 200.
 */
static int pref_offset_clear __initdata = 512;
static int pref_offset_copy  __initdata = 256;

static unsigned int pref_src_mode __initdata;
static unsigned int pref_dst_mode __initdata;

static int load_offset __initdata;
static int store_offset __initdata;

static unsigned int __initdata *dest, *epc;

static unsigned int instruction_pending;
static union mips_instruction delayed_mi;

static void __init emit_instruction(union mips_instruction mi)
{
	if (instruction_pending)
		*epc++ = delayed_mi.word;

	instruction_pending = 1;
	delayed_mi = mi;
}

static inline void flush_delay_slot_or_nop(void)
{
	if (instruction_pending) {
		*epc++ = delayed_mi.word;
		instruction_pending = 0;
		return;
	}

	*epc++ = 0;
}

static inline unsigned int *label(void)
{
	if (instruction_pending) {
		*epc++ = delayed_mi.word;
		instruction_pending = 0;
	}

	return epc;
}

static inline void build_insn_word(unsigned int word)
{
	union mips_instruction mi;

	mi.word		 = word;

	emit_instruction(mi);
}

static inline void build_nop(void)
{
	build_insn_word(0);			/* nop */
}

static inline void build_src_pref(int advance)
{
	if (!(load_offset & (cpu_dcache_line_size() - 1)) && advance) {
		union mips_instruction mi;

		mi.i_format.opcode     = pref_op;
		mi.i_format.rs         = 5;		/* $a1 */
		mi.i_format.rt         = pref_src_mode;
		mi.i_format.simmediate = load_offset + advance;

		emit_instruction(mi);
	}
}

static inline void __build_load_reg(int reg)
{
	union mips_instruction mi;
	unsigned int width;

	if (cpu_has_64bit_gp_regs) {
		mi.i_format.opcode     = ld_op;
		width = 8;
	} else {
		mi.i_format.opcode     = lw_op;
		width = 4;
	}
	mi.i_format.rs         = 5;		/* $a1 */
	mi.i_format.rt         = reg;		/* $reg */
	mi.i_format.simmediate = load_offset;

	load_offset += width;
	emit_instruction(mi);
}

static inline void build_load_reg(int reg)
{
	if (cpu_has_prefetch)
		build_src_pref(pref_offset_copy);

	__build_load_reg(reg);
}

static inline void build_dst_pref(int advance)
{
	if (!(store_offset & (cpu_dcache_line_size() - 1)) && advance) {
		union mips_instruction mi;

		mi.i_format.opcode     = pref_op;
		mi.i_format.rs         = 4;		/* $a0 */
		mi.i_format.rt         = pref_dst_mode;
		mi.i_format.simmediate = store_offset + advance;

		emit_instruction(mi);
	}
}

static inline void build_cdex_s(void)
{
	union mips_instruction mi;

	if ((store_offset & (cpu_scache_line_size() - 1)))
		return;

	mi.c_format.opcode     = cache_op;
	mi.c_format.rs         = 4;		/* $a0 */
	mi.c_format.c_op       = 3;		/* Create Dirty Exclusive */
	mi.c_format.cache      = 3;		/* Secondary Data Cache */
	mi.c_format.simmediate = store_offset;

	emit_instruction(mi);
}

static inline void build_cdex_p(void)
{
	union mips_instruction mi;

	if (store_offset & (cpu_dcache_line_size() - 1))
		return;

	if (R4600_V1_HIT_CACHEOP_WAR && cpu_is_r4600_v1_x()) {
		build_nop();
		build_nop();
		build_nop();
		build_nop();
	}

	if (R4600_V2_HIT_CACHEOP_WAR && cpu_is_r4600_v2_x())
		build_insn_word(0x3c01a000);	/* lui     $at, 0xa000  */

	mi.c_format.opcode     = cache_op;
	mi.c_format.rs         = 4;		/* $a0 */
	mi.c_format.c_op       = 3;		/* Create Dirty Exclusive */
	mi.c_format.cache      = 1;		/* Data Cache */
	mi.c_format.simmediate = store_offset;

	emit_instruction(mi);
}

static void __init __build_store_reg(int reg)
{
	union mips_instruction mi;
	unsigned int width;

	if (cpu_has_64bit_gp_regs ||
	    (cpu_has_64bit_zero_reg && reg == 0)) {
		mi.i_format.opcode     = sd_op;
		width = 8;
	} else {
		mi.i_format.opcode     = sw_op;
		width = 4;
	}
	mi.i_format.rs         = 4;		/* $a0 */
	mi.i_format.rt         = reg;		/* $reg */
	mi.i_format.simmediate = store_offset;

	store_offset += width;
	emit_instruction(mi);
}

static inline void build_store_reg(int reg)
{
	int pref_off = cpu_has_prefetch ?
		(reg ? pref_offset_copy : pref_offset_clear) : 0;
	if (pref_off)
		build_dst_pref(pref_off);
	else if (cpu_has_cache_cdex_s)
		build_cdex_s();
	else if (cpu_has_cache_cdex_p)
		build_cdex_p();

	__build_store_reg(reg);
}

static inline void build_addiu_a2_a0(unsigned long offset)
{
	union mips_instruction mi;

	BUG_ON(offset > 0x7fff);

	mi.i_format.opcode     = cpu_has_64bit_gp_regs ? daddiu_op : addiu_op;
	mi.i_format.rs         = 4;		/* $a0 */
	mi.i_format.rt         = 6;		/* $a2 */
	mi.i_format.simmediate = offset;

	emit_instruction(mi);
}

static inline void build_addiu_a2(unsigned long offset)
{
	union mips_instruction mi;

	BUG_ON(offset > 0x7fff);

	mi.i_format.opcode     = cpu_has_64bit_gp_regs ? daddiu_op : addiu_op;
	mi.i_format.rs         = 6;		/* $a2 */
	mi.i_format.rt         = 6;		/* $a2 */
	mi.i_format.simmediate = offset;

	emit_instruction(mi);
}

static inline void build_addiu_a1(unsigned long offset)
{
	union mips_instruction mi;

	BUG_ON(offset > 0x7fff);

	mi.i_format.opcode     = cpu_has_64bit_gp_regs ? daddiu_op : addiu_op;
	mi.i_format.rs         = 5;		/* $a1 */
	mi.i_format.rt         = 5;		/* $a1 */
	mi.i_format.simmediate = offset;

	load_offset -= offset;

	emit_instruction(mi);
}

static inline void build_addiu_a0(unsigned long offset)
{
	union mips_instruction mi;

	BUG_ON(offset > 0x7fff);

	mi.i_format.opcode     = cpu_has_64bit_gp_regs ? daddiu_op : addiu_op;
	mi.i_format.rs         = 4;		/* $a0 */
	mi.i_format.rt         = 4;		/* $a0 */
	mi.i_format.simmediate = offset;

	store_offset -= offset;

	emit_instruction(mi);
}

static inline void build_bne(unsigned int *dest)
{
	union mips_instruction mi;

	mi.i_format.opcode = bne_op;
	mi.i_format.rs     = 6;			/* $a2 */
	mi.i_format.rt     = 4;			/* $a0 */
	mi.i_format.simmediate = dest - epc - 1;

	*epc++ = mi.word;
	flush_delay_slot_or_nop();
}

static inline void build_jr_ra(void)
{
	union mips_instruction mi;

	mi.r_format.opcode = spec_op;
	mi.r_format.rs     = 31;
	mi.r_format.rt     = 0;
	mi.r_format.rd     = 0;
	mi.r_format.re     = 0;
	mi.r_format.func   = jr_op;

	*epc++ = mi.word;
	flush_delay_slot_or_nop();
}

void __init build_clear_page(void)
{
	unsigned int loop_start;
	unsigned long off;

	epc = (unsigned int *) &clear_page_array;
	instruction_pending = 0;
	store_offset = 0;

	if (cpu_has_prefetch) {
		switch (current_cpu_data.cputype) {
		case CPU_TX49XX:
			/* TX49 supports only Pref_Load */
			pref_offset_clear = 0;
			pref_offset_copy = 0;
			break;

		case CPU_RM9000:
			/*
			 * As a workaround for erratum G105 which make the
			 * PrepareForStore hint unusable we fall back to
			 * StoreRetained on the RM9000.  Once it is known which
			 * versions of the RM9000 we'll be able to condition-
			 * alize this.
			 */

		case CPU_R10000:
		case CPU_R12000:
		case CPU_R14000:
			pref_src_mode = Pref_LoadStreamed;
			pref_dst_mode = Pref_StoreStreamed;
			break;

		default:
			pref_src_mode = Pref_LoadStreamed;
			pref_dst_mode = Pref_PrepareForStore;
			break;
		}
	}

        off = PAGE_SIZE - (cpu_has_prefetch ? pref_offset_clear : 0);
	if (off > 0x7fff) {
		build_addiu_a2_a0(off >> 1);
		build_addiu_a2(off >> 1);
	} else
		build_addiu_a2_a0(off);

	if (R4600_V2_HIT_CACHEOP_WAR && cpu_is_r4600_v2_x())
		build_insn_word(0x3c01a000);	/* lui     $at, 0xa000  */

dest = label();
	do {
		build_store_reg(0);
		build_store_reg(0);
		build_store_reg(0);
		build_store_reg(0);
	} while (store_offset < half_scache_line_size());
	build_addiu_a0(2 * store_offset);
	loop_start = store_offset;
	do {
		build_store_reg(0);
		build_store_reg(0);
		build_store_reg(0);
		build_store_reg(0);
	} while ((store_offset - loop_start) < half_scache_line_size());
	build_bne(dest);

	if (cpu_has_prefetch && pref_offset_clear) {
		build_addiu_a2_a0(pref_offset_clear);
	dest = label();
		loop_start = store_offset;
		do {
			__build_store_reg(0);
			__build_store_reg(0);
			__build_store_reg(0);
			__build_store_reg(0);
		} while ((store_offset - loop_start) < half_scache_line_size());
		build_addiu_a0(2 * store_offset);
		loop_start = store_offset;
		do {
			__build_store_reg(0);
			__build_store_reg(0);
			__build_store_reg(0);
			__build_store_reg(0);
		} while ((store_offset - loop_start) < half_scache_line_size());
		build_bne(dest);
	}

	build_jr_ra();

	BUG_ON(epc > clear_page_array + ARRAY_SIZE(clear_page_array));
}

void __init build_copy_page(void)
{
	unsigned int loop_start;
	unsigned long off;

	epc = (unsigned int *) &copy_page_array;
	store_offset = load_offset = 0;
	instruction_pending = 0;

	off = PAGE_SIZE - (cpu_has_prefetch ? pref_offset_copy : 0);
	if (off > 0x7fff) {
		build_addiu_a2_a0(off >> 1);
		build_addiu_a2(off >> 1);
	} else
		build_addiu_a2_a0(off);

	if (R4600_V2_HIT_CACHEOP_WAR && cpu_is_r4600_v2_x())
		build_insn_word(0x3c01a000);	/* lui     $at, 0xa000  */

dest = label();
	loop_start = store_offset;
	do {
		build_load_reg( 8);
		build_load_reg( 9);
		build_load_reg(10);
		build_load_reg(11);
		build_store_reg( 8);
		build_store_reg( 9);
		build_store_reg(10);
		build_store_reg(11);
	} while ((store_offset - loop_start) < half_scache_line_size());
	build_addiu_a0(2 * store_offset);
	build_addiu_a1(2 * load_offset);
	loop_start = store_offset;
	do {
		build_load_reg( 8);
		build_load_reg( 9);
		build_load_reg(10);
		build_load_reg(11);
		build_store_reg( 8);
		build_store_reg( 9);
		build_store_reg(10);
		build_store_reg(11);
	} while ((store_offset - loop_start) < half_scache_line_size());
	build_bne(dest);

	if (cpu_has_prefetch && pref_offset_copy) {
		build_addiu_a2_a0(pref_offset_copy);
	dest = label();
		loop_start = store_offset;
		do {
			__build_load_reg( 8);
			__build_load_reg( 9);
			__build_load_reg(10);
			__build_load_reg(11);
			__build_store_reg( 8);
			__build_store_reg( 9);
			__build_store_reg(10);
			__build_store_reg(11);
		} while ((store_offset - loop_start) < half_scache_line_size());
		build_addiu_a0(2 * store_offset);
		build_addiu_a1(2 * load_offset);
		loop_start = store_offset;
		do {
			__build_load_reg( 8);
			__build_load_reg( 9);
			__build_load_reg(10);
			__build_load_reg(11);
			__build_store_reg( 8);
			__build_store_reg( 9);
			__build_store_reg(10);
			__build_store_reg(11);
		} while ((store_offset - loop_start) < half_scache_line_size());
		build_bne(dest);
	}

	build_jr_ra();

	BUG_ON(epc > copy_page_array + ARRAY_SIZE(copy_page_array));
}
