// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/jump_label.h>

#include "asm/cacheflush.h"

#define JUMPLABEL_ERR	"ARC: jump_label: ERROR: "

/* Halt system on fatal error to make debug easier */
#define arc_jl_fatal(format...)						\
({									\
	pr_err(JUMPLABEL_ERR format);					\
	BUG();								\
})

static inline u32 arc_gen_nop(void)
{
	/* 1x 32bit NOP in middle endian */
	return 0x7000264a;
}

/*
 * Atomic update of patched instruction is only available if this
 * instruction doesn't cross L1 cache line boundary. You can read about
 * the way we achieve this in arc/include/asm/jump_label.h
 */
static inline void instruction_align_assert(void *addr, int len)
{
	unsigned long a = (unsigned long)addr;

	if ((a >> L1_CACHE_SHIFT) != ((a + len - 1) >> L1_CACHE_SHIFT))
		arc_jl_fatal("instruction (addr %px) cross L1 cache line border",
			     addr);
}

/*
 * ARCv2 'Branch unconditionally' instruction:
 * 00000ssssssssss1SSSSSSSSSSNRtttt
 * s S[n:0] lower bits signed immediate (number is bitfield size)
 * S S[m:n+1] upper bits signed immediate (number is bitfield size)
 * t S[24:21] upper bits signed immediate (branch unconditionally far)
 * N N <.d> delay slot mode
 * R R Reserved
 */
static inline u32 arc_gen_branch(jump_label_t pc, jump_label_t target)
{
	u32 instruction_l, instruction_r;
	u32 pcl = pc & GENMASK(31, 2);
	u32 u_offset = target - pcl;
	u32 s, S, t;

	/*
	 * Offset in 32-bit branch instruction must to fit into s25.
	 * Something is terribly broken if we get such huge offset within one
	 * function.
	 */
	if ((s32)u_offset < -16777216 || (s32)u_offset > 16777214)
		arc_jl_fatal("gen branch with offset (%d) not fit in s25",
			     (s32)u_offset);

	/*
	 * All instructions are aligned by 2 bytes so we should never get offset
	 * here which is not 2 bytes aligned.
	 */
	if (u_offset & 0x1)
		arc_jl_fatal("gen branch with offset (%d) unaligned to 2 bytes",
			     (s32)u_offset);

	s = (u_offset >> 1)  & GENMASK(9, 0);
	S = (u_offset >> 11) & GENMASK(9, 0);
	t = (u_offset >> 21) & GENMASK(3, 0);

	/* 00000ssssssssss1 */
	instruction_l = (s << 1) | 0x1;
	/* SSSSSSSSSSNRtttt */
	instruction_r = (S << 6) | t;

	return (instruction_r << 16) | (instruction_l & GENMASK(15, 0));
}

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	jump_label_t *instr_addr = (jump_label_t *)entry->code;
	u32 instr;

	instruction_align_assert(instr_addr, JUMP_LABEL_NOP_SIZE);

	if (type == JUMP_LABEL_JMP)
		instr = arc_gen_branch(entry->code, entry->target);
	else
		instr = arc_gen_nop();

	WRITE_ONCE(*instr_addr, instr);
	flush_icache_range(entry->code, entry->code + JUMP_LABEL_NOP_SIZE);
}

void arch_jump_label_transform_static(struct jump_entry *entry,
				      enum jump_label_type type)
{
	/*
	 * We use only one NOP type (1x, 4 byte) in arch_static_branch, so
	 * there's no need to patch an identical NOP over the top of it here.
	 * The generic code calls 'arch_jump_label_transform' if the NOP needs
	 * to be replaced by a branch, so 'arch_jump_label_transform_static' is
	 * never called with type other than JUMP_LABEL_NOP.
	 */
	BUG_ON(type != JUMP_LABEL_NOP);
}

#ifdef CONFIG_ARC_DBG_JUMP_LABEL
#define SELFTEST_MSG	"ARC: instruction generation self-test: "

struct arc_gen_branch_testdata {
	jump_label_t pc;
	jump_label_t target_address;
	u32 expected_instr;
};

static __init int branch_gen_test(const struct arc_gen_branch_testdata *test)
{
	u32 instr_got;

	instr_got = arc_gen_branch(test->pc, test->target_address);
	if (instr_got == test->expected_instr)
		return 0;

	pr_err(SELFTEST_MSG "FAIL:\n arc_gen_branch(0x%08x, 0x%08x) != 0x%08x, got 0x%08x\n",
	       test->pc, test->target_address,
	       test->expected_instr, instr_got);

	return -EFAULT;
}

/*
 * Offset field in branch instruction is not continuous. Test all
 * available offset field and sign combinations. Test data is generated
 * from real working code.
 */
static const struct arc_gen_branch_testdata arcgenbr_test_data[] __initconst = {
	{0x90007548, 0x90007514, 0xffcf07cd}, /* tiny (-52) offs */
	{0x9000c9c0, 0x9000c782, 0xffcf05c3}, /* tiny (-574) offs */
	{0x9000cc1c, 0x9000c782, 0xffcf0367}, /* tiny (-1178) offs */
	{0x9009dce0, 0x9009d106, 0xff8f0427}, /* small (-3034) offs */
	{0x9000f5de, 0x90007d30, 0xfc0f0755}, /* big  (-30892) offs */
	{0x900a2444, 0x90035f64, 0xc9cf0321}, /* huge (-443616) offs */
	{0x90007514, 0x9000752c, 0x00000019}, /* tiny (+24) offs */
	{0x9001a578, 0x9001a77a, 0x00000203}, /* tiny (+514) offs */
	{0x90031ed8, 0x90032634, 0x0000075d}, /* tiny (+1884) offs */
	{0x9008c7f2, 0x9008d3f0, 0x00400401}, /* small (+3072) offs */
	{0x9000bb38, 0x9003b340, 0x17c00009}, /* big  (+194568) offs */
	{0x90008f44, 0x90578d80, 0xb7c2063d}  /* huge (+5701180) offs */
};

static __init int instr_gen_test(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(arcgenbr_test_data); i++)
		if (branch_gen_test(&arcgenbr_test_data[i]))
			return -EFAULT;

	pr_info(SELFTEST_MSG "OK\n");

	return 0;
}
early_initcall(instr_gen_test);

#endif /* CONFIG_ARC_DBG_JUMP_LABEL */
