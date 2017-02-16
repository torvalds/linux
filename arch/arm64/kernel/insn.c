/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * Copyright (C) 2014-2016 Zi Shen Lim <zlim.lnx@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/stop_machine.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>
#include <asm/fixmap.h>
#include <asm/insn.h>

#define AARCH64_INSN_SF_BIT	BIT(31)
#define AARCH64_INSN_N_BIT	BIT(22)

static int aarch64_insn_encoding_class[] = {
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_REG,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_FPSIMD,
	AARCH64_INSN_CLS_DP_IMM,
	AARCH64_INSN_CLS_DP_IMM,
	AARCH64_INSN_CLS_BR_SYS,
	AARCH64_INSN_CLS_BR_SYS,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_REG,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_FPSIMD,
};

enum aarch64_insn_encoding_class __kprobes aarch64_get_insn_class(u32 insn)
{
	return aarch64_insn_encoding_class[(insn >> 25) & 0xf];
}

/* NOP is an alias of HINT */
bool __kprobes aarch64_insn_is_nop(u32 insn)
{
	if (!aarch64_insn_is_hint(insn))
		return false;

	switch (insn & 0xFE0) {
	case AARCH64_INSN_HINT_YIELD:
	case AARCH64_INSN_HINT_WFE:
	case AARCH64_INSN_HINT_WFI:
	case AARCH64_INSN_HINT_SEV:
	case AARCH64_INSN_HINT_SEVL:
		return false;
	default:
		return true;
	}
}

bool aarch64_insn_is_branch_imm(u32 insn)
{
	return (aarch64_insn_is_b(insn) || aarch64_insn_is_bl(insn) ||
		aarch64_insn_is_tbz(insn) || aarch64_insn_is_tbnz(insn) ||
		aarch64_insn_is_cbz(insn) || aarch64_insn_is_cbnz(insn) ||
		aarch64_insn_is_bcond(insn));
}

static DEFINE_RAW_SPINLOCK(patch_lock);

static void __kprobes *patch_map(void *addr, int fixmap)
{
	unsigned long uintaddr = (uintptr_t) addr;
	bool module = !core_kernel_text(uintaddr);
	struct page *page;

	if (module && IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		page = vmalloc_to_page(addr);
	else if (!module)
		page = phys_to_page(__pa_symbol(addr));
	else
		return addr;

	BUG_ON(!page);
	return (void *)set_fixmap_offset(fixmap, page_to_phys(page) +
			(uintaddr & ~PAGE_MASK));
}

static void __kprobes patch_unmap(int fixmap)
{
	clear_fixmap(fixmap);
}
/*
 * In ARMv8-A, A64 instructions have a fixed length of 32 bits and are always
 * little-endian.
 */
int __kprobes aarch64_insn_read(void *addr, u32 *insnp)
{
	int ret;
	u32 val;

	ret = probe_kernel_read(&val, addr, AARCH64_INSN_SIZE);
	if (!ret)
		*insnp = le32_to_cpu(val);

	return ret;
}

static int __kprobes __aarch64_insn_write(void *addr, u32 insn)
{
	void *waddr = addr;
	unsigned long flags = 0;
	int ret;

	raw_spin_lock_irqsave(&patch_lock, flags);
	waddr = patch_map(addr, FIX_TEXT_POKE0);

	ret = probe_kernel_write(waddr, &insn, AARCH64_INSN_SIZE);

	patch_unmap(FIX_TEXT_POKE0);
	raw_spin_unlock_irqrestore(&patch_lock, flags);

	return ret;
}

int __kprobes aarch64_insn_write(void *addr, u32 insn)
{
	insn = cpu_to_le32(insn);
	return __aarch64_insn_write(addr, insn);
}

static bool __kprobes __aarch64_insn_hotpatch_safe(u32 insn)
{
	if (aarch64_get_insn_class(insn) != AARCH64_INSN_CLS_BR_SYS)
		return false;

	return	aarch64_insn_is_b(insn) ||
		aarch64_insn_is_bl(insn) ||
		aarch64_insn_is_svc(insn) ||
		aarch64_insn_is_hvc(insn) ||
		aarch64_insn_is_smc(insn) ||
		aarch64_insn_is_brk(insn) ||
		aarch64_insn_is_nop(insn);
}

bool __kprobes aarch64_insn_uses_literal(u32 insn)
{
	/* ldr/ldrsw (literal), prfm */

	return aarch64_insn_is_ldr_lit(insn) ||
		aarch64_insn_is_ldrsw_lit(insn) ||
		aarch64_insn_is_adr_adrp(insn) ||
		aarch64_insn_is_prfm_lit(insn);
}

bool __kprobes aarch64_insn_is_branch(u32 insn)
{
	/* b, bl, cb*, tb*, b.cond, br, blr */

	return aarch64_insn_is_b(insn) ||
		aarch64_insn_is_bl(insn) ||
		aarch64_insn_is_cbz(insn) ||
		aarch64_insn_is_cbnz(insn) ||
		aarch64_insn_is_tbz(insn) ||
		aarch64_insn_is_tbnz(insn) ||
		aarch64_insn_is_ret(insn) ||
		aarch64_insn_is_br(insn) ||
		aarch64_insn_is_blr(insn) ||
		aarch64_insn_is_bcond(insn);
}

/*
 * ARM Architecture Reference Manual for ARMv8 Profile-A, Issue A.a
 * Section B2.6.5 "Concurrent modification and execution of instructions":
 * Concurrent modification and execution of instructions can lead to the
 * resulting instruction performing any behavior that can be achieved by
 * executing any sequence of instructions that can be executed from the
 * same Exception level, except where the instruction before modification
 * and the instruction after modification is a B, BL, NOP, BKPT, SVC, HVC,
 * or SMC instruction.
 */
bool __kprobes aarch64_insn_hotpatch_safe(u32 old_insn, u32 new_insn)
{
	return __aarch64_insn_hotpatch_safe(old_insn) &&
	       __aarch64_insn_hotpatch_safe(new_insn);
}

int __kprobes aarch64_insn_patch_text_nosync(void *addr, u32 insn)
{
	u32 *tp = addr;
	int ret;

	/* A64 instructions must be word aligned */
	if ((uintptr_t)tp & 0x3)
		return -EINVAL;

	ret = aarch64_insn_write(tp, insn);
	if (ret == 0)
		flush_icache_range((uintptr_t)tp,
				   (uintptr_t)tp + AARCH64_INSN_SIZE);

	return ret;
}

struct aarch64_insn_patch {
	void		**text_addrs;
	u32		*new_insns;
	int		insn_cnt;
	atomic_t	cpu_count;
};

static int __kprobes aarch64_insn_patch_text_cb(void *arg)
{
	int i, ret = 0;
	struct aarch64_insn_patch *pp = arg;

	/* The first CPU becomes master */
	if (atomic_inc_return(&pp->cpu_count) == 1) {
		for (i = 0; ret == 0 && i < pp->insn_cnt; i++)
			ret = aarch64_insn_patch_text_nosync(pp->text_addrs[i],
							     pp->new_insns[i]);
		/*
		 * aarch64_insn_patch_text_nosync() calls flush_icache_range(),
		 * which ends with "dsb; isb" pair guaranteeing global
		 * visibility.
		 */
		/* Notify other processors with an additional increment. */
		atomic_inc(&pp->cpu_count);
	} else {
		while (atomic_read(&pp->cpu_count) <= num_online_cpus())
			cpu_relax();
		isb();
	}

	return ret;
}

int __kprobes aarch64_insn_patch_text_sync(void *addrs[], u32 insns[], int cnt)
{
	struct aarch64_insn_patch patch = {
		.text_addrs = addrs,
		.new_insns = insns,
		.insn_cnt = cnt,
		.cpu_count = ATOMIC_INIT(0),
	};

	if (cnt <= 0)
		return -EINVAL;

	return stop_machine(aarch64_insn_patch_text_cb, &patch,
			    cpu_online_mask);
}

int __kprobes aarch64_insn_patch_text(void *addrs[], u32 insns[], int cnt)
{
	int ret;
	u32 insn;

	/* Unsafe to patch multiple instructions without synchronizaiton */
	if (cnt == 1) {
		ret = aarch64_insn_read(addrs[0], &insn);
		if (ret)
			return ret;

		if (aarch64_insn_hotpatch_safe(insn, insns[0])) {
			/*
			 * ARMv8 architecture doesn't guarantee all CPUs see
			 * the new instruction after returning from function
			 * aarch64_insn_patch_text_nosync(). So send IPIs to
			 * all other CPUs to achieve instruction
			 * synchronization.
			 */
			ret = aarch64_insn_patch_text_nosync(addrs[0], insns[0]);
			kick_all_cpus_sync();
			return ret;
		}
	}

	return aarch64_insn_patch_text_sync(addrs, insns, cnt);
}

static int __kprobes aarch64_get_imm_shift_mask(enum aarch64_insn_imm_type type,
						u32 *maskp, int *shiftp)
{
	u32 mask;
	int shift;

	switch (type) {
	case AARCH64_INSN_IMM_26:
		mask = BIT(26) - 1;
		shift = 0;
		break;
	case AARCH64_INSN_IMM_19:
		mask = BIT(19) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_16:
		mask = BIT(16) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_14:
		mask = BIT(14) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_12:
		mask = BIT(12) - 1;
		shift = 10;
		break;
	case AARCH64_INSN_IMM_9:
		mask = BIT(9) - 1;
		shift = 12;
		break;
	case AARCH64_INSN_IMM_7:
		mask = BIT(7) - 1;
		shift = 15;
		break;
	case AARCH64_INSN_IMM_6:
	case AARCH64_INSN_IMM_S:
		mask = BIT(6) - 1;
		shift = 10;
		break;
	case AARCH64_INSN_IMM_R:
		mask = BIT(6) - 1;
		shift = 16;
		break;
	default:
		return -EINVAL;
	}

	*maskp = mask;
	*shiftp = shift;

	return 0;
}

#define ADR_IMM_HILOSPLIT	2
#define ADR_IMM_SIZE		SZ_2M
#define ADR_IMM_LOMASK		((1 << ADR_IMM_HILOSPLIT) - 1)
#define ADR_IMM_HIMASK		((ADR_IMM_SIZE >> ADR_IMM_HILOSPLIT) - 1)
#define ADR_IMM_LOSHIFT		29
#define ADR_IMM_HISHIFT		5

u64 aarch64_insn_decode_immediate(enum aarch64_insn_imm_type type, u32 insn)
{
	u32 immlo, immhi, mask;
	int shift;

	switch (type) {
	case AARCH64_INSN_IMM_ADR:
		shift = 0;
		immlo = (insn >> ADR_IMM_LOSHIFT) & ADR_IMM_LOMASK;
		immhi = (insn >> ADR_IMM_HISHIFT) & ADR_IMM_HIMASK;
		insn = (immhi << ADR_IMM_HILOSPLIT) | immlo;
		mask = ADR_IMM_SIZE - 1;
		break;
	default:
		if (aarch64_get_imm_shift_mask(type, &mask, &shift) < 0) {
			pr_err("aarch64_insn_decode_immediate: unknown immediate encoding %d\n",
			       type);
			return 0;
		}
	}

	return (insn >> shift) & mask;
}

u32 __kprobes aarch64_insn_encode_immediate(enum aarch64_insn_imm_type type,
				  u32 insn, u64 imm)
{
	u32 immlo, immhi, mask;
	int shift;

	if (insn == AARCH64_BREAK_FAULT)
		return AARCH64_BREAK_FAULT;

	switch (type) {
	case AARCH64_INSN_IMM_ADR:
		shift = 0;
		immlo = (imm & ADR_IMM_LOMASK) << ADR_IMM_LOSHIFT;
		imm >>= ADR_IMM_HILOSPLIT;
		immhi = (imm & ADR_IMM_HIMASK) << ADR_IMM_HISHIFT;
		imm = immlo | immhi;
		mask = ((ADR_IMM_LOMASK << ADR_IMM_LOSHIFT) |
			(ADR_IMM_HIMASK << ADR_IMM_HISHIFT));
		break;
	default:
		if (aarch64_get_imm_shift_mask(type, &mask, &shift) < 0) {
			pr_err("aarch64_insn_encode_immediate: unknown immediate encoding %d\n",
			       type);
			return AARCH64_BREAK_FAULT;
		}
	}

	/* Update the immediate field. */
	insn &= ~(mask << shift);
	insn |= (imm & mask) << shift;

	return insn;
}

u32 aarch64_insn_decode_register(enum aarch64_insn_register_type type,
					u32 insn)
{
	int shift;

	switch (type) {
	case AARCH64_INSN_REGTYPE_RT:
	case AARCH64_INSN_REGTYPE_RD:
		shift = 0;
		break;
	case AARCH64_INSN_REGTYPE_RN:
		shift = 5;
		break;
	case AARCH64_INSN_REGTYPE_RT2:
	case AARCH64_INSN_REGTYPE_RA:
		shift = 10;
		break;
	case AARCH64_INSN_REGTYPE_RM:
		shift = 16;
		break;
	default:
		pr_err("%s: unknown register type encoding %d\n", __func__,
		       type);
		return 0;
	}

	return (insn >> shift) & GENMASK(4, 0);
}

static u32 aarch64_insn_encode_register(enum aarch64_insn_register_type type,
					u32 insn,
					enum aarch64_insn_register reg)
{
	int shift;

	if (insn == AARCH64_BREAK_FAULT)
		return AARCH64_BREAK_FAULT;

	if (reg < AARCH64_INSN_REG_0 || reg > AARCH64_INSN_REG_SP) {
		pr_err("%s: unknown register encoding %d\n", __func__, reg);
		return AARCH64_BREAK_FAULT;
	}

	switch (type) {
	case AARCH64_INSN_REGTYPE_RT:
	case AARCH64_INSN_REGTYPE_RD:
		shift = 0;
		break;
	case AARCH64_INSN_REGTYPE_RN:
		shift = 5;
		break;
	case AARCH64_INSN_REGTYPE_RT2:
	case AARCH64_INSN_REGTYPE_RA:
		shift = 10;
		break;
	case AARCH64_INSN_REGTYPE_RM:
		shift = 16;
		break;
	default:
		pr_err("%s: unknown register type encoding %d\n", __func__,
		       type);
		return AARCH64_BREAK_FAULT;
	}

	insn &= ~(GENMASK(4, 0) << shift);
	insn |= reg << shift;

	return insn;
}

static u32 aarch64_insn_encode_ldst_size(enum aarch64_insn_size_type type,
					 u32 insn)
{
	u32 size;

	switch (type) {
	case AARCH64_INSN_SIZE_8:
		size = 0;
		break;
	case AARCH64_INSN_SIZE_16:
		size = 1;
		break;
	case AARCH64_INSN_SIZE_32:
		size = 2;
		break;
	case AARCH64_INSN_SIZE_64:
		size = 3;
		break;
	default:
		pr_err("%s: unknown size encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn &= ~GENMASK(31, 30);
	insn |= size << 30;

	return insn;
}

static inline long branch_imm_common(unsigned long pc, unsigned long addr,
				     long range)
{
	long offset;

	if ((pc & 0x3) || (addr & 0x3)) {
		pr_err("%s: A64 instructions must be word aligned\n", __func__);
		return range;
	}

	offset = ((long)addr - (long)pc);

	if (offset < -range || offset >= range) {
		pr_err("%s: offset out of range\n", __func__);
		return range;
	}

	return offset;
}

u32 __kprobes aarch64_insn_gen_branch_imm(unsigned long pc, unsigned long addr,
					  enum aarch64_insn_branch_type type)
{
	u32 insn;
	long offset;

	/*
	 * B/BL support [-128M, 128M) offset
	 * ARM64 virtual address arrangement guarantees all kernel and module
	 * texts are within +/-128M.
	 */
	offset = branch_imm_common(pc, addr, SZ_128M);
	if (offset >= SZ_128M)
		return AARCH64_BREAK_FAULT;

	switch (type) {
	case AARCH64_INSN_BRANCH_LINK:
		insn = aarch64_insn_get_bl_value();
		break;
	case AARCH64_INSN_BRANCH_NOLINK:
		insn = aarch64_insn_get_b_value();
		break;
	default:
		pr_err("%s: unknown branch encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_26, insn,
					     offset >> 2);
}

u32 aarch64_insn_gen_comp_branch_imm(unsigned long pc, unsigned long addr,
				     enum aarch64_insn_register reg,
				     enum aarch64_insn_variant variant,
				     enum aarch64_insn_branch_type type)
{
	u32 insn;
	long offset;

	offset = branch_imm_common(pc, addr, SZ_1M);
	if (offset >= SZ_1M)
		return AARCH64_BREAK_FAULT;

	switch (type) {
	case AARCH64_INSN_BRANCH_COMP_ZERO:
		insn = aarch64_insn_get_cbz_value();
		break;
	case AARCH64_INSN_BRANCH_COMP_NONZERO:
		insn = aarch64_insn_get_cbnz_value();
		break;
	default:
		pr_err("%s: unknown branch encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn, reg);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_19, insn,
					     offset >> 2);
}

u32 aarch64_insn_gen_cond_branch_imm(unsigned long pc, unsigned long addr,
				     enum aarch64_insn_condition cond)
{
	u32 insn;
	long offset;

	offset = branch_imm_common(pc, addr, SZ_1M);

	insn = aarch64_insn_get_bcond_value();

	if (cond < AARCH64_INSN_COND_EQ || cond > AARCH64_INSN_COND_AL) {
		pr_err("%s: unknown condition encoding %d\n", __func__, cond);
		return AARCH64_BREAK_FAULT;
	}
	insn |= cond;

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_19, insn,
					     offset >> 2);
}

u32 __kprobes aarch64_insn_gen_hint(enum aarch64_insn_hint_op op)
{
	return aarch64_insn_get_hint_value() | op;
}

u32 __kprobes aarch64_insn_gen_nop(void)
{
	return aarch64_insn_gen_hint(AARCH64_INSN_HINT_NOP);
}

u32 aarch64_insn_gen_branch_reg(enum aarch64_insn_register reg,
				enum aarch64_insn_branch_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_BRANCH_NOLINK:
		insn = aarch64_insn_get_br_value();
		break;
	case AARCH64_INSN_BRANCH_LINK:
		insn = aarch64_insn_get_blr_value();
		break;
	case AARCH64_INSN_BRANCH_RETURN:
		insn = aarch64_insn_get_ret_value();
		break;
	default:
		pr_err("%s: unknown branch encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, reg);
}

u32 aarch64_insn_gen_load_store_reg(enum aarch64_insn_register reg,
				    enum aarch64_insn_register base,
				    enum aarch64_insn_register offset,
				    enum aarch64_insn_size_type size,
				    enum aarch64_insn_ldst_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_LDST_LOAD_REG_OFFSET:
		insn = aarch64_insn_get_ldr_reg_value();
		break;
	case AARCH64_INSN_LDST_STORE_REG_OFFSET:
		insn = aarch64_insn_get_str_reg_value();
		break;
	default:
		pr_err("%s: unknown load/store encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_ldst_size(size, insn);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn, reg);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    base);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn,
					    offset);
}

u32 aarch64_insn_gen_load_store_pair(enum aarch64_insn_register reg1,
				     enum aarch64_insn_register reg2,
				     enum aarch64_insn_register base,
				     int offset,
				     enum aarch64_insn_variant variant,
				     enum aarch64_insn_ldst_type type)
{
	u32 insn;
	int shift;

	switch (type) {
	case AARCH64_INSN_LDST_LOAD_PAIR_PRE_INDEX:
		insn = aarch64_insn_get_ldp_pre_value();
		break;
	case AARCH64_INSN_LDST_STORE_PAIR_PRE_INDEX:
		insn = aarch64_insn_get_stp_pre_value();
		break;
	case AARCH64_INSN_LDST_LOAD_PAIR_POST_INDEX:
		insn = aarch64_insn_get_ldp_post_value();
		break;
	case AARCH64_INSN_LDST_STORE_PAIR_POST_INDEX:
		insn = aarch64_insn_get_stp_post_value();
		break;
	default:
		pr_err("%s: unknown load/store encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if ((offset & 0x3) || (offset < -256) || (offset > 252)) {
			pr_err("%s: offset must be multiples of 4 in the range of [-256, 252] %d\n",
			       __func__, offset);
			return AARCH64_BREAK_FAULT;
		}
		shift = 2;
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		if ((offset & 0x7) || (offset < -512) || (offset > 504)) {
			pr_err("%s: offset must be multiples of 8 in the range of [-512, 504] %d\n",
			       __func__, offset);
			return AARCH64_BREAK_FAULT;
		}
		shift = 3;
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn,
					    reg1);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT2, insn,
					    reg2);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    base);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_7, insn,
					     offset >> shift);
}

u32 aarch64_insn_gen_add_sub_imm(enum aarch64_insn_register dst,
				 enum aarch64_insn_register src,
				 int imm, enum aarch64_insn_variant variant,
				 enum aarch64_insn_adsb_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_ADSB_ADD:
		insn = aarch64_insn_get_add_imm_value();
		break;
	case AARCH64_INSN_ADSB_SUB:
		insn = aarch64_insn_get_sub_imm_value();
		break;
	case AARCH64_INSN_ADSB_ADD_SETFLAGS:
		insn = aarch64_insn_get_adds_imm_value();
		break;
	case AARCH64_INSN_ADSB_SUB_SETFLAGS:
		insn = aarch64_insn_get_subs_imm_value();
		break;
	default:
		pr_err("%s: unknown add/sub encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	if (imm & ~(SZ_4K - 1)) {
		pr_err("%s: invalid immediate encoding %d\n", __func__, imm);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_12, insn, imm);
}

u32 aarch64_insn_gen_bitfield(enum aarch64_insn_register dst,
			      enum aarch64_insn_register src,
			      int immr, int imms,
			      enum aarch64_insn_variant variant,
			      enum aarch64_insn_bitfield_type type)
{
	u32 insn;
	u32 mask;

	switch (type) {
	case AARCH64_INSN_BITFIELD_MOVE:
		insn = aarch64_insn_get_bfm_value();
		break;
	case AARCH64_INSN_BITFIELD_MOVE_UNSIGNED:
		insn = aarch64_insn_get_ubfm_value();
		break;
	case AARCH64_INSN_BITFIELD_MOVE_SIGNED:
		insn = aarch64_insn_get_sbfm_value();
		break;
	default:
		pr_err("%s: unknown bitfield encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		mask = GENMASK(4, 0);
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT | AARCH64_INSN_N_BIT;
		mask = GENMASK(5, 0);
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	if (immr & ~mask) {
		pr_err("%s: invalid immr encoding %d\n", __func__, immr);
		return AARCH64_BREAK_FAULT;
	}
	if (imms & ~mask) {
		pr_err("%s: invalid imms encoding %d\n", __func__, imms);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	insn = aarch64_insn_encode_immediate(AARCH64_INSN_IMM_R, insn, immr);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_S, insn, imms);
}

u32 aarch64_insn_gen_movewide(enum aarch64_insn_register dst,
			      int imm, int shift,
			      enum aarch64_insn_variant variant,
			      enum aarch64_insn_movewide_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_MOVEWIDE_ZERO:
		insn = aarch64_insn_get_movz_value();
		break;
	case AARCH64_INSN_MOVEWIDE_KEEP:
		insn = aarch64_insn_get_movk_value();
		break;
	case AARCH64_INSN_MOVEWIDE_INVERSE:
		insn = aarch64_insn_get_movn_value();
		break;
	default:
		pr_err("%s: unknown movewide encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	if (imm & ~(SZ_64K - 1)) {
		pr_err("%s: invalid immediate encoding %d\n", __func__, imm);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if (shift != 0 && shift != 16) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		if (shift != 0 && shift != 16 && shift != 32 && shift != 48) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn |= (shift >> 4) << 21;

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_16, insn, imm);
}

u32 aarch64_insn_gen_add_sub_shifted_reg(enum aarch64_insn_register dst,
					 enum aarch64_insn_register src,
					 enum aarch64_insn_register reg,
					 int shift,
					 enum aarch64_insn_variant variant,
					 enum aarch64_insn_adsb_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_ADSB_ADD:
		insn = aarch64_insn_get_add_value();
		break;
	case AARCH64_INSN_ADSB_SUB:
		insn = aarch64_insn_get_sub_value();
		break;
	case AARCH64_INSN_ADSB_ADD_SETFLAGS:
		insn = aarch64_insn_get_adds_value();
		break;
	case AARCH64_INSN_ADSB_SUB_SETFLAGS:
		insn = aarch64_insn_get_subs_value();
		break;
	default:
		pr_err("%s: unknown add/sub encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if (shift & ~(SZ_32 - 1)) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		if (shift & ~(SZ_64 - 1)) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}


	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn, reg);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_6, insn, shift);
}

u32 aarch64_insn_gen_data1(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data1_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_DATA1_REVERSE_16:
		insn = aarch64_insn_get_rev16_value();
		break;
	case AARCH64_INSN_DATA1_REVERSE_32:
		insn = aarch64_insn_get_rev32_value();
		break;
	case AARCH64_INSN_DATA1_REVERSE_64:
		if (variant != AARCH64_INSN_VARIANT_64BIT) {
			pr_err("%s: invalid variant for reverse64 %d\n",
			       __func__, variant);
			return AARCH64_BREAK_FAULT;
		}
		insn = aarch64_insn_get_rev64_value();
		break;
	default:
		pr_err("%s: unknown data1 encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);
}

u32 aarch64_insn_gen_data2(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_register reg,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data2_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_DATA2_UDIV:
		insn = aarch64_insn_get_udiv_value();
		break;
	case AARCH64_INSN_DATA2_SDIV:
		insn = aarch64_insn_get_sdiv_value();
		break;
	case AARCH64_INSN_DATA2_LSLV:
		insn = aarch64_insn_get_lslv_value();
		break;
	case AARCH64_INSN_DATA2_LSRV:
		insn = aarch64_insn_get_lsrv_value();
		break;
	case AARCH64_INSN_DATA2_ASRV:
		insn = aarch64_insn_get_asrv_value();
		break;
	case AARCH64_INSN_DATA2_RORV:
		insn = aarch64_insn_get_rorv_value();
		break;
	default:
		pr_err("%s: unknown data2 encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn, reg);
}

u32 aarch64_insn_gen_data3(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_register reg1,
			   enum aarch64_insn_register reg2,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data3_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_DATA3_MADD:
		insn = aarch64_insn_get_madd_value();
		break;
	case AARCH64_INSN_DATA3_MSUB:
		insn = aarch64_insn_get_msub_value();
		break;
	default:
		pr_err("%s: unknown data3 encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RA, insn, src);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    reg1);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn,
					    reg2);
}

u32 aarch64_insn_gen_logical_shifted_reg(enum aarch64_insn_register dst,
					 enum aarch64_insn_register src,
					 enum aarch64_insn_register reg,
					 int shift,
					 enum aarch64_insn_variant variant,
					 enum aarch64_insn_logic_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_LOGIC_AND:
		insn = aarch64_insn_get_and_value();
		break;
	case AARCH64_INSN_LOGIC_BIC:
		insn = aarch64_insn_get_bic_value();
		break;
	case AARCH64_INSN_LOGIC_ORR:
		insn = aarch64_insn_get_orr_value();
		break;
	case AARCH64_INSN_LOGIC_ORN:
		insn = aarch64_insn_get_orn_value();
		break;
	case AARCH64_INSN_LOGIC_EOR:
		insn = aarch64_insn_get_eor_value();
		break;
	case AARCH64_INSN_LOGIC_EON:
		insn = aarch64_insn_get_eon_value();
		break;
	case AARCH64_INSN_LOGIC_AND_SETFLAGS:
		insn = aarch64_insn_get_ands_value();
		break;
	case AARCH64_INSN_LOGIC_BIC_SETFLAGS:
		insn = aarch64_insn_get_bics_value();
		break;
	default:
		pr_err("%s: unknown logical encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if (shift & ~(SZ_32 - 1)) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		if (shift & ~(SZ_64 - 1)) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}


	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn, reg);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_6, insn, shift);
}

/*
 * Decode the imm field of a branch, and return the byte offset as a
 * signed value (so it can be used when computing a new branch
 * target).
 */
s32 aarch64_get_branch_offset(u32 insn)
{
	s32 imm;

	if (aarch64_insn_is_b(insn) || aarch64_insn_is_bl(insn)) {
		imm = aarch64_insn_decode_immediate(AARCH64_INSN_IMM_26, insn);
		return (imm << 6) >> 4;
	}

	if (aarch64_insn_is_cbz(insn) || aarch64_insn_is_cbnz(insn) ||
	    aarch64_insn_is_bcond(insn)) {
		imm = aarch64_insn_decode_immediate(AARCH64_INSN_IMM_19, insn);
		return (imm << 13) >> 11;
	}

	if (aarch64_insn_is_tbz(insn) || aarch64_insn_is_tbnz(insn)) {
		imm = aarch64_insn_decode_immediate(AARCH64_INSN_IMM_14, insn);
		return (imm << 18) >> 16;
	}

	/* Unhandled instruction */
	BUG();
}

/*
 * Encode the displacement of a branch in the imm field and return the
 * updated instruction.
 */
u32 aarch64_set_branch_offset(u32 insn, s32 offset)
{
	if (aarch64_insn_is_b(insn) || aarch64_insn_is_bl(insn))
		return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_26, insn,
						     offset >> 2);

	if (aarch64_insn_is_cbz(insn) || aarch64_insn_is_cbnz(insn) ||
	    aarch64_insn_is_bcond(insn))
		return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_19, insn,
						     offset >> 2);

	if (aarch64_insn_is_tbz(insn) || aarch64_insn_is_tbnz(insn))
		return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_14, insn,
						     offset >> 2);

	/* Unhandled instruction */
	BUG();
}

s32 aarch64_insn_adrp_get_offset(u32 insn)
{
	BUG_ON(!aarch64_insn_is_adrp(insn));
	return aarch64_insn_decode_immediate(AARCH64_INSN_IMM_ADR, insn) << 12;
}

u32 aarch64_insn_adrp_set_offset(u32 insn, s32 offset)
{
	BUG_ON(!aarch64_insn_is_adrp(insn));
	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_ADR, insn,
						offset >> 12);
}

/*
 * Extract the Op/CR data from a msr/mrs instruction.
 */
u32 aarch64_insn_extract_system_reg(u32 insn)
{
	return (insn & 0x1FFFE0) >> 5;
}

bool aarch32_insn_is_wide(u32 insn)
{
	return insn >= 0xe800;
}

/*
 * Macros/defines for extracting register numbers from instruction.
 */
u32 aarch32_insn_extract_reg_num(u32 insn, int offset)
{
	return (insn & (0xf << offset)) >> offset;
}

#define OPC2_MASK	0x7
#define OPC2_OFFSET	5
u32 aarch32_insn_mcr_extract_opc2(u32 insn)
{
	return (insn & (OPC2_MASK << OPC2_OFFSET)) >> OPC2_OFFSET;
}

#define CRM_MASK	0xf
u32 aarch32_insn_mcr_extract_crm(u32 insn)
{
	return insn & CRM_MASK;
}

static bool __kprobes __check_eq(unsigned long pstate)
{
	return (pstate & PSR_Z_BIT) != 0;
}

static bool __kprobes __check_ne(unsigned long pstate)
{
	return (pstate & PSR_Z_BIT) == 0;
}

static bool __kprobes __check_cs(unsigned long pstate)
{
	return (pstate & PSR_C_BIT) != 0;
}

static bool __kprobes __check_cc(unsigned long pstate)
{
	return (pstate & PSR_C_BIT) == 0;
}

static bool __kprobes __check_mi(unsigned long pstate)
{
	return (pstate & PSR_N_BIT) != 0;
}

static bool __kprobes __check_pl(unsigned long pstate)
{
	return (pstate & PSR_N_BIT) == 0;
}

static bool __kprobes __check_vs(unsigned long pstate)
{
	return (pstate & PSR_V_BIT) != 0;
}

static bool __kprobes __check_vc(unsigned long pstate)
{
	return (pstate & PSR_V_BIT) == 0;
}

static bool __kprobes __check_hi(unsigned long pstate)
{
	pstate &= ~(pstate >> 1);	/* PSR_C_BIT &= ~PSR_Z_BIT */
	return (pstate & PSR_C_BIT) != 0;
}

static bool __kprobes __check_ls(unsigned long pstate)
{
	pstate &= ~(pstate >> 1);	/* PSR_C_BIT &= ~PSR_Z_BIT */
	return (pstate & PSR_C_BIT) == 0;
}

static bool __kprobes __check_ge(unsigned long pstate)
{
	pstate ^= (pstate << 3);	/* PSR_N_BIT ^= PSR_V_BIT */
	return (pstate & PSR_N_BIT) == 0;
}

static bool __kprobes __check_lt(unsigned long pstate)
{
	pstate ^= (pstate << 3);	/* PSR_N_BIT ^= PSR_V_BIT */
	return (pstate & PSR_N_BIT) != 0;
}

static bool __kprobes __check_gt(unsigned long pstate)
{
	/*PSR_N_BIT ^= PSR_V_BIT */
	unsigned long temp = pstate ^ (pstate << 3);

	temp |= (pstate << 1);	/*PSR_N_BIT |= PSR_Z_BIT */
	return (temp & PSR_N_BIT) == 0;
}

static bool __kprobes __check_le(unsigned long pstate)
{
	/*PSR_N_BIT ^= PSR_V_BIT */
	unsigned long temp = pstate ^ (pstate << 3);

	temp |= (pstate << 1);	/*PSR_N_BIT |= PSR_Z_BIT */
	return (temp & PSR_N_BIT) != 0;
}

static bool __kprobes __check_al(unsigned long pstate)
{
	return true;
}

/*
 * Note that the ARMv8 ARM calls condition code 0b1111 "nv", but states that
 * it behaves identically to 0b1110 ("al").
 */
pstate_check_t * const aarch32_opcode_cond_checks[16] = {
	__check_eq, __check_ne, __check_cs, __check_cc,
	__check_mi, __check_pl, __check_vs, __check_vc,
	__check_hi, __check_ls, __check_ge, __check_lt,
	__check_gt, __check_le, __check_al, __check_al
};
