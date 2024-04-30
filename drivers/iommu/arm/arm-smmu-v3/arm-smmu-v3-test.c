// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 Google LLC.
 */
#include <kunit/test.h>
#include <linux/io-pgtable.h>

#include "arm-smmu-v3.h"

struct arm_smmu_test_writer {
	struct arm_smmu_entry_writer writer;
	struct kunit *test;
	const __le64 *init_entry;
	const __le64 *target_entry;
	__le64 *entry;

	bool invalid_entry_written;
	unsigned int num_syncs;
};

#define NUM_ENTRY_QWORDS 8
#define NUM_EXPECTED_SYNCS(x) x

static struct arm_smmu_ste bypass_ste;
static struct arm_smmu_ste abort_ste;
static struct arm_smmu_device smmu = {
	.features = ARM_SMMU_FEAT_STALLS | ARM_SMMU_FEAT_ATTR_TYPES_OVR
};
static struct mm_struct sva_mm = {
	.pgd = (void *)0xdaedbeefdeadbeefULL,
};

static bool arm_smmu_entry_differs_in_used_bits(const __le64 *entry,
						const __le64 *used_bits,
						const __le64 *target,
						unsigned int length)
{
	bool differs = false;
	unsigned int i;

	for (i = 0; i < length; i++) {
		if ((entry[i] & used_bits[i]) != target[i])
			differs = true;
	}
	return differs;
}

static void
arm_smmu_test_writer_record_syncs(struct arm_smmu_entry_writer *writer)
{
	struct arm_smmu_test_writer *test_writer =
		container_of(writer, struct arm_smmu_test_writer, writer);
	__le64 *entry_used_bits;

	entry_used_bits = kunit_kzalloc(
		test_writer->test, sizeof(*entry_used_bits) * NUM_ENTRY_QWORDS,
		GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test_writer->test, entry_used_bits);

	pr_debug("STE value is now set to: ");
	print_hex_dump_debug("    ", DUMP_PREFIX_NONE, 16, 8,
			     test_writer->entry,
			     NUM_ENTRY_QWORDS * sizeof(*test_writer->entry),
			     false);

	test_writer->num_syncs += 1;
	if (!test_writer->entry[0]) {
		test_writer->invalid_entry_written = true;
	} else {
		/*
		 * At any stage in a hitless transition, the entry must be
		 * equivalent to either the initial entry or the target entry
		 * when only considering the bits used by the current
		 * configuration.
		 */
		writer->ops->get_used(test_writer->entry, entry_used_bits);
		KUNIT_EXPECT_FALSE(
			test_writer->test,
			arm_smmu_entry_differs_in_used_bits(
				test_writer->entry, entry_used_bits,
				test_writer->init_entry, NUM_ENTRY_QWORDS) &&
				arm_smmu_entry_differs_in_used_bits(
					test_writer->entry, entry_used_bits,
					test_writer->target_entry,
					NUM_ENTRY_QWORDS));
	}
}

static void
arm_smmu_v3_test_debug_print_used_bits(struct arm_smmu_entry_writer *writer,
				       const __le64 *ste)
{
	__le64 used_bits[NUM_ENTRY_QWORDS] = {};

	arm_smmu_get_ste_used(ste, used_bits);
	pr_debug("STE used bits: ");
	print_hex_dump_debug("    ", DUMP_PREFIX_NONE, 16, 8, used_bits,
			     sizeof(used_bits), false);
}

static const struct arm_smmu_entry_writer_ops test_ste_ops = {
	.sync = arm_smmu_test_writer_record_syncs,
	.get_used = arm_smmu_get_ste_used,
};

static const struct arm_smmu_entry_writer_ops test_cd_ops = {
	.sync = arm_smmu_test_writer_record_syncs,
	.get_used = arm_smmu_get_cd_used,
};

static void arm_smmu_v3_test_ste_expect_transition(
	struct kunit *test, const struct arm_smmu_ste *cur,
	const struct arm_smmu_ste *target, unsigned int num_syncs_expected,
	bool hitless)
{
	struct arm_smmu_ste cur_copy = *cur;
	struct arm_smmu_test_writer test_writer = {
		.writer = {
			.ops = &test_ste_ops,
		},
		.test = test,
		.init_entry = cur->data,
		.target_entry = target->data,
		.entry = cur_copy.data,
		.num_syncs = 0,
		.invalid_entry_written = false,

	};

	pr_debug("STE initial value: ");
	print_hex_dump_debug("    ", DUMP_PREFIX_NONE, 16, 8, cur_copy.data,
			     sizeof(cur_copy), false);
	arm_smmu_v3_test_debug_print_used_bits(&test_writer.writer, cur->data);
	pr_debug("STE target value: ");
	print_hex_dump_debug("    ", DUMP_PREFIX_NONE, 16, 8, target->data,
			     sizeof(cur_copy), false);
	arm_smmu_v3_test_debug_print_used_bits(&test_writer.writer,
					       target->data);

	arm_smmu_write_entry(&test_writer.writer, cur_copy.data, target->data);

	KUNIT_EXPECT_EQ(test, test_writer.invalid_entry_written, !hitless);
	KUNIT_EXPECT_EQ(test, test_writer.num_syncs, num_syncs_expected);
	KUNIT_EXPECT_MEMEQ(test, target->data, cur_copy.data, sizeof(cur_copy));
}

static void arm_smmu_v3_test_ste_expect_hitless_transition(
	struct kunit *test, const struct arm_smmu_ste *cur,
	const struct arm_smmu_ste *target, unsigned int num_syncs_expected)
{
	arm_smmu_v3_test_ste_expect_transition(test, cur, target,
					       num_syncs_expected, true);
}

static const dma_addr_t fake_cdtab_dma_addr = 0xF0F0F0F0F0F0;

static void arm_smmu_test_make_cdtable_ste(struct arm_smmu_ste *ste,
					   const dma_addr_t dma_addr)
{
	struct arm_smmu_master master = {
		.cd_table.cdtab_dma = dma_addr,
		.cd_table.s1cdmax = 0xFF,
		.cd_table.s1fmt = STRTAB_STE_0_S1FMT_64K_L2,
		.smmu = &smmu,
	};

	arm_smmu_make_cdtable_ste(ste, &master);
}

static void arm_smmu_v3_write_ste_test_bypass_to_abort(struct kunit *test)
{
	/*
	 * Bypass STEs has used bits in the first two Qwords, while abort STEs
	 * only have used bits in the first QWord. Transitioning from bypass to
	 * abort requires two syncs: the first to set the first qword and make
	 * the STE into an abort, the second to clean up the second qword.
	 */
	arm_smmu_v3_test_ste_expect_hitless_transition(
		test, &bypass_ste, &abort_ste, NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_abort_to_bypass(struct kunit *test)
{
	/*
	 * Transitioning from abort to bypass also requires two syncs: the first
	 * to set the second qword data required by the bypass STE, and the
	 * second to set the first qword and switch to bypass.
	 */
	arm_smmu_v3_test_ste_expect_hitless_transition(
		test, &abort_ste, &bypass_ste, NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_cdtable_to_abort(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_cdtable_ste(&ste, fake_cdtab_dma_addr);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &ste, &abort_ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_abort_to_cdtable(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_cdtable_ste(&ste, fake_cdtab_dma_addr);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &abort_ste, &ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_cdtable_to_bypass(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_cdtable_ste(&ste, fake_cdtab_dma_addr);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &ste, &bypass_ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void arm_smmu_v3_write_ste_test_bypass_to_cdtable(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_cdtable_ste(&ste, fake_cdtab_dma_addr);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &bypass_ste, &ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void arm_smmu_test_make_s2_ste(struct arm_smmu_ste *ste,
				      bool ats_enabled)
{
	struct arm_smmu_master master = {
		.smmu = &smmu,
		.ats_enabled = ats_enabled,
	};
	struct io_pgtable io_pgtable = {};
	struct arm_smmu_domain smmu_domain = {
		.pgtbl_ops = &io_pgtable.ops,
	};

	io_pgtable.cfg.arm_lpae_s2_cfg.vttbr = 0xdaedbeefdeadbeefULL;
	io_pgtable.cfg.arm_lpae_s2_cfg.vtcr.ps = 1;
	io_pgtable.cfg.arm_lpae_s2_cfg.vtcr.tg = 2;
	io_pgtable.cfg.arm_lpae_s2_cfg.vtcr.sh = 3;
	io_pgtable.cfg.arm_lpae_s2_cfg.vtcr.orgn = 1;
	io_pgtable.cfg.arm_lpae_s2_cfg.vtcr.irgn = 2;
	io_pgtable.cfg.arm_lpae_s2_cfg.vtcr.sl = 3;
	io_pgtable.cfg.arm_lpae_s2_cfg.vtcr.tsz = 4;

	arm_smmu_make_s2_domain_ste(ste, &master, &smmu_domain);
}

static void arm_smmu_v3_write_ste_test_s2_to_abort(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_s2_ste(&ste, true);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &ste, &abort_ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_abort_to_s2(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_s2_ste(&ste, true);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &abort_ste, &ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_s2_to_bypass(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_s2_ste(&ste, true);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &ste, &bypass_ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_bypass_to_s2(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_s2_ste(&ste, true);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &bypass_ste, &ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_test_cd_expect_transition(
	struct kunit *test, const struct arm_smmu_cd *cur,
	const struct arm_smmu_cd *target, unsigned int num_syncs_expected,
	bool hitless)
{
	struct arm_smmu_cd cur_copy = *cur;
	struct arm_smmu_test_writer test_writer = {
		.writer = {
			.ops = &test_cd_ops,
		},
		.test = test,
		.init_entry = cur->data,
		.target_entry = target->data,
		.entry = cur_copy.data,
		.num_syncs = 0,
		.invalid_entry_written = false,

	};

	pr_debug("CD initial value: ");
	print_hex_dump_debug("    ", DUMP_PREFIX_NONE, 16, 8, cur_copy.data,
			     sizeof(cur_copy), false);
	arm_smmu_v3_test_debug_print_used_bits(&test_writer.writer, cur->data);
	pr_debug("CD target value: ");
	print_hex_dump_debug("    ", DUMP_PREFIX_NONE, 16, 8, target->data,
			     sizeof(cur_copy), false);
	arm_smmu_v3_test_debug_print_used_bits(&test_writer.writer,
					       target->data);

	arm_smmu_write_entry(&test_writer.writer, cur_copy.data, target->data);

	KUNIT_EXPECT_EQ(test, test_writer.invalid_entry_written, !hitless);
	KUNIT_EXPECT_EQ(test, test_writer.num_syncs, num_syncs_expected);
	KUNIT_EXPECT_MEMEQ(test, target->data, cur_copy.data, sizeof(cur_copy));
}

static void arm_smmu_v3_test_cd_expect_non_hitless_transition(
	struct kunit *test, const struct arm_smmu_cd *cur,
	const struct arm_smmu_cd *target, unsigned int num_syncs_expected)
{
	arm_smmu_v3_test_cd_expect_transition(test, cur, target,
					      num_syncs_expected, false);
}

static void arm_smmu_v3_test_cd_expect_hitless_transition(
	struct kunit *test, const struct arm_smmu_cd *cur,
	const struct arm_smmu_cd *target, unsigned int num_syncs_expected)
{
	arm_smmu_v3_test_cd_expect_transition(test, cur, target,
					      num_syncs_expected, true);
}

static void arm_smmu_test_make_s1_cd(struct arm_smmu_cd *cd, unsigned int asid)
{
	struct arm_smmu_master master = {
		.smmu = &smmu,
	};
	struct io_pgtable io_pgtable = {};
	struct arm_smmu_domain smmu_domain = {
		.pgtbl_ops = &io_pgtable.ops,
		.cd = {
			.asid = asid,
		},
	};

	io_pgtable.cfg.arm_lpae_s1_cfg.ttbr = 0xdaedbeefdeadbeefULL;
	io_pgtable.cfg.arm_lpae_s1_cfg.tcr.ips = 1;
	io_pgtable.cfg.arm_lpae_s1_cfg.tcr.tg = 2;
	io_pgtable.cfg.arm_lpae_s1_cfg.tcr.sh = 3;
	io_pgtable.cfg.arm_lpae_s1_cfg.tcr.orgn = 1;
	io_pgtable.cfg.arm_lpae_s1_cfg.tcr.irgn = 2;
	io_pgtable.cfg.arm_lpae_s1_cfg.tcr.tsz = 4;
	io_pgtable.cfg.arm_lpae_s1_cfg.mair = 0xabcdef012345678ULL;

	arm_smmu_make_s1_cd(cd, &master, &smmu_domain);
}

static void arm_smmu_v3_write_cd_test_s1_clear(struct kunit *test)
{
	struct arm_smmu_cd cd = {};
	struct arm_smmu_cd cd_2;

	arm_smmu_test_make_s1_cd(&cd_2, 1997);
	arm_smmu_v3_test_cd_expect_non_hitless_transition(
		test, &cd, &cd_2, NUM_EXPECTED_SYNCS(2));
	arm_smmu_v3_test_cd_expect_non_hitless_transition(
		test, &cd_2, &cd, NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_cd_test_s1_change_asid(struct kunit *test)
{
	struct arm_smmu_cd cd = {};
	struct arm_smmu_cd cd_2;

	arm_smmu_test_make_s1_cd(&cd, 778);
	arm_smmu_test_make_s1_cd(&cd_2, 1997);
	arm_smmu_v3_test_cd_expect_hitless_transition(test, &cd, &cd_2,
						      NUM_EXPECTED_SYNCS(1));
	arm_smmu_v3_test_cd_expect_hitless_transition(test, &cd_2, &cd,
						      NUM_EXPECTED_SYNCS(1));
}

static void arm_smmu_test_make_sva_cd(struct arm_smmu_cd *cd, unsigned int asid)
{
	struct arm_smmu_master master = {
		.smmu = &smmu,
	};

	arm_smmu_make_sva_cd(cd, &master, &sva_mm, asid);
}

static void arm_smmu_test_make_sva_release_cd(struct arm_smmu_cd *cd,
					      unsigned int asid)
{
	struct arm_smmu_master master = {
		.smmu = &smmu,
	};

	arm_smmu_make_sva_cd(cd, &master, NULL, asid);
}

static void arm_smmu_v3_write_cd_test_sva_clear(struct kunit *test)
{
	struct arm_smmu_cd cd = {};
	struct arm_smmu_cd cd_2;

	arm_smmu_test_make_sva_cd(&cd_2, 1997);
	arm_smmu_v3_test_cd_expect_non_hitless_transition(
		test, &cd, &cd_2, NUM_EXPECTED_SYNCS(2));
	arm_smmu_v3_test_cd_expect_non_hitless_transition(
		test, &cd_2, &cd, NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_cd_test_sva_release(struct kunit *test)
{
	struct arm_smmu_cd cd;
	struct arm_smmu_cd cd_2;

	arm_smmu_test_make_sva_cd(&cd, 1997);
	arm_smmu_test_make_sva_release_cd(&cd_2, 1997);
	arm_smmu_v3_test_cd_expect_hitless_transition(test, &cd, &cd_2,
						      NUM_EXPECTED_SYNCS(2));
	arm_smmu_v3_test_cd_expect_hitless_transition(test, &cd_2, &cd,
						      NUM_EXPECTED_SYNCS(2));
}

static struct kunit_case arm_smmu_v3_test_cases[] = {
	KUNIT_CASE(arm_smmu_v3_write_ste_test_bypass_to_abort),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_abort_to_bypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_cdtable_to_abort),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_abort_to_cdtable),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_cdtable_to_bypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_bypass_to_cdtable),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s2_to_abort),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_abort_to_s2),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s2_to_bypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_bypass_to_s2),
	KUNIT_CASE(arm_smmu_v3_write_cd_test_s1_clear),
	KUNIT_CASE(arm_smmu_v3_write_cd_test_s1_change_asid),
	KUNIT_CASE(arm_smmu_v3_write_cd_test_sva_clear),
	KUNIT_CASE(arm_smmu_v3_write_cd_test_sva_release),
	{},
};

static int arm_smmu_v3_test_suite_init(struct kunit_suite *test)
{
	arm_smmu_make_bypass_ste(&smmu, &bypass_ste);
	arm_smmu_make_abort_ste(&abort_ste);
	return 0;
}

static struct kunit_suite arm_smmu_v3_test_module = {
	.name = "arm-smmu-v3-kunit-test",
	.suite_init = arm_smmu_v3_test_suite_init,
	.test_cases = arm_smmu_v3_test_cases,
};
kunit_test_suites(&arm_smmu_v3_test_module);
