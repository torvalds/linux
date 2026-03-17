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

enum arm_smmu_test_master_feat {
	ARM_SMMU_MASTER_TEST_ATS = BIT(0),
	ARM_SMMU_MASTER_TEST_STALL = BIT(1),
	ARM_SMMU_MASTER_TEST_NESTED = BIT(2),
};

static void arm_smmu_test_make_s2_ste(struct arm_smmu_ste *ste,
				      enum arm_smmu_test_master_feat feat);

static bool arm_smmu_entry_differs_in_used_bits(const __le64 *entry,
						const __le64 *used_bits,
						const __le64 *target,
						const __le64 *safe,
						unsigned int length)
{
	bool differs = false;
	unsigned int i;

	for (i = 0; i < length; i++) {
		__le64 used = used_bits[i] & ~safe[i];

		if ((entry[i] & used) != (target[i] & used))
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
	__le64 *safe_target;
	__le64 *safe_init;

	entry_used_bits = kunit_kzalloc(
		test_writer->test, sizeof(*entry_used_bits) * NUM_ENTRY_QWORDS,
		GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test_writer->test, entry_used_bits);

	safe_target = kunit_kzalloc(test_writer->test,
				    sizeof(*safe_target) * NUM_ENTRY_QWORDS,
				    GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test_writer->test, safe_target);

	safe_init = kunit_kzalloc(test_writer->test,
				  sizeof(*safe_init) * NUM_ENTRY_QWORDS,
				  GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test_writer->test, safe_init);

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
		if (writer->ops->get_update_safe)
			writer->ops->get_update_safe(test_writer->entry,
						     test_writer->init_entry,
						     safe_init);
		if (writer->ops->get_update_safe)
			writer->ops->get_update_safe(test_writer->entry,
						     test_writer->target_entry,
						     safe_target);
		KUNIT_EXPECT_FALSE(
			test_writer->test,
			arm_smmu_entry_differs_in_used_bits(
				test_writer->entry, entry_used_bits,
				test_writer->init_entry, safe_init,
				NUM_ENTRY_QWORDS) &&
				arm_smmu_entry_differs_in_used_bits(
					test_writer->entry, entry_used_bits,
					test_writer->target_entry, safe_target,
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
	.get_update_safe = arm_smmu_get_ste_update_safe,
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

static void arm_smmu_v3_test_ste_expect_non_hitless_transition(
	struct kunit *test, const struct arm_smmu_ste *cur,
	const struct arm_smmu_ste *target, unsigned int num_syncs_expected)
{
	arm_smmu_v3_test_ste_expect_transition(test, cur, target,
					       num_syncs_expected, false);
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
					   unsigned int s1dss,
					   const dma_addr_t dma_addr,
					   enum arm_smmu_test_master_feat feat)
{
	bool ats_enabled = feat & ARM_SMMU_MASTER_TEST_ATS;
	bool stall_enabled = feat & ARM_SMMU_MASTER_TEST_STALL;

	struct arm_smmu_master master = {
		.ats_enabled = ats_enabled,
		.cd_table.cdtab_dma = dma_addr,
		.cd_table.s1cdmax = 0xFF,
		.cd_table.s1fmt = STRTAB_STE_0_S1FMT_64K_L2,
		.smmu = &smmu,
		.stall_enabled = stall_enabled,
	};

	arm_smmu_make_cdtable_ste(ste, &master, ats_enabled, s1dss);
	if (feat & ARM_SMMU_MASTER_TEST_NESTED) {
		struct arm_smmu_ste s2ste;
		int i;

		arm_smmu_test_make_s2_ste(&s2ste,
					  feat & ~ARM_SMMU_MASTER_TEST_NESTED);
		ste->data[0] |= cpu_to_le64(
			FIELD_PREP(STRTAB_STE_0_CFG, STRTAB_STE_0_CFG_NESTED));
		ste->data[1] |= cpu_to_le64(STRTAB_STE_1_MEV);
		for (i = 2; i < NUM_ENTRY_QWORDS; i++)
			ste->data[i] = s2ste.data[i];
	}
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

	arm_smmu_test_make_cdtable_ste(&ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &ste, &abort_ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_abort_to_cdtable(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_cdtable_ste(&ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &abort_ste, &ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_cdtable_to_bypass(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_cdtable_ste(&ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &ste, &bypass_ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void arm_smmu_v3_write_ste_test_bypass_to_cdtable(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_cdtable_ste(&ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &bypass_ste, &ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void arm_smmu_v3_write_ste_test_cdtable_s1dss_change(struct kunit *test)
{
	struct arm_smmu_ste ste;
	struct arm_smmu_ste s1dss_bypass;

	arm_smmu_test_make_cdtable_ste(&ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_test_make_cdtable_ste(&s1dss_bypass, STRTAB_STE_1_S1DSS_BYPASS,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);

	/*
	 * Flipping s1dss on a CD table STE only involves changes to the second
	 * qword of an STE and can be done in a single write.
	 */
	arm_smmu_v3_test_ste_expect_hitless_transition(
		test, &ste, &s1dss_bypass, NUM_EXPECTED_SYNCS(1));
	arm_smmu_v3_test_ste_expect_hitless_transition(
		test, &s1dss_bypass, &ste, NUM_EXPECTED_SYNCS(1));
}

static void
arm_smmu_v3_write_ste_test_s1dssbypass_to_stebypass(struct kunit *test)
{
	struct arm_smmu_ste s1dss_bypass;

	arm_smmu_test_make_cdtable_ste(&s1dss_bypass, STRTAB_STE_1_S1DSS_BYPASS,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(
		test, &s1dss_bypass, &bypass_ste, NUM_EXPECTED_SYNCS(2));
}

static void
arm_smmu_v3_write_ste_test_stebypass_to_s1dssbypass(struct kunit *test)
{
	struct arm_smmu_ste s1dss_bypass;

	arm_smmu_test_make_cdtable_ste(&s1dss_bypass, STRTAB_STE_1_S1DSS_BYPASS,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(
		test, &bypass_ste, &s1dss_bypass, NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_test_make_s2_ste(struct arm_smmu_ste *ste,
				      enum arm_smmu_test_master_feat feat)
{
	bool ats_enabled = feat & ARM_SMMU_MASTER_TEST_ATS;
	bool stall_enabled = feat & ARM_SMMU_MASTER_TEST_STALL;
	struct arm_smmu_master master = {
		.ats_enabled = ats_enabled,
		.smmu = &smmu,
		.stall_enabled = stall_enabled,
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

	arm_smmu_make_s2_domain_ste(ste, &master, &smmu_domain, ats_enabled);
}

static void arm_smmu_v3_write_ste_test_s2_to_abort(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_s2_ste(&ste, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &ste, &abort_ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_abort_to_s2(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_s2_ste(&ste, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &abort_ste, &ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_s2_to_bypass(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_s2_ste(&ste, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &ste, &bypass_ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_bypass_to_s2(struct kunit *test)
{
	struct arm_smmu_ste ste;

	arm_smmu_test_make_s2_ste(&ste, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &bypass_ste, &ste,
						       NUM_EXPECTED_SYNCS(2));
}

static void arm_smmu_v3_write_ste_test_s1_to_s2(struct kunit *test)
{
	struct arm_smmu_ste s1_ste;
	struct arm_smmu_ste s2_ste;

	arm_smmu_test_make_cdtable_ste(&s1_ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_test_make_s2_ste(&s2_ste, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &s1_ste, &s2_ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void arm_smmu_v3_write_ste_test_s2_to_s1(struct kunit *test)
{
	struct arm_smmu_ste s1_ste;
	struct arm_smmu_ste s2_ste;

	arm_smmu_test_make_cdtable_ste(&s1_ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_test_make_s2_ste(&s2_ste, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &s2_ste, &s1_ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void arm_smmu_v3_write_ste_test_non_hitless(struct kunit *test)
{
	struct arm_smmu_ste ste;
	struct arm_smmu_ste ste_2;

	/*
	 * Although no flow resembles this in practice, one way to force an STE
	 * update to be non-hitless is to change its CD table pointer as well as
	 * s1 dss field in the same update.
	 */
	arm_smmu_test_make_cdtable_ste(&ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_test_make_cdtable_ste(&ste_2, STRTAB_STE_1_S1DSS_BYPASS,
				       0x4B4B4b4B4B, ARM_SMMU_MASTER_TEST_ATS);
	arm_smmu_v3_test_ste_expect_non_hitless_transition(
		test, &ste, &ste_2, NUM_EXPECTED_SYNCS(3));
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

static void arm_smmu_v3_write_ste_test_s1_to_s2_stall(struct kunit *test)
{
	struct arm_smmu_ste s1_ste;
	struct arm_smmu_ste s2_ste;

	arm_smmu_test_make_cdtable_ste(&s1_ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_STALL);
	arm_smmu_test_make_s2_ste(&s2_ste, ARM_SMMU_MASTER_TEST_STALL);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &s1_ste, &s2_ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void arm_smmu_v3_write_ste_test_s2_to_s1_stall(struct kunit *test)
{
	struct arm_smmu_ste s1_ste;
	struct arm_smmu_ste s2_ste;

	arm_smmu_test_make_cdtable_ste(&s1_ste, STRTAB_STE_1_S1DSS_SSID0,
				       fake_cdtab_dma_addr, ARM_SMMU_MASTER_TEST_STALL);
	arm_smmu_test_make_s2_ste(&s2_ste, ARM_SMMU_MASTER_TEST_STALL);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &s2_ste, &s1_ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void
arm_smmu_v3_write_ste_test_nested_s1dssbypass_to_s1bypass(struct kunit *test)
{
	struct arm_smmu_ste s1_ste;
	struct arm_smmu_ste s2_ste;

	arm_smmu_test_make_cdtable_ste(
		&s1_ste, STRTAB_STE_1_S1DSS_BYPASS, fake_cdtab_dma_addr,
		ARM_SMMU_MASTER_TEST_ATS | ARM_SMMU_MASTER_TEST_NESTED);
	arm_smmu_test_make_s2_ste(&s2_ste, 0);
	/* Expect an additional sync to unset ignored bits: EATS and MEV */
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &s1_ste, &s2_ste,
						       NUM_EXPECTED_SYNCS(3));
}

static void
arm_smmu_v3_write_ste_test_nested_s1bypass_to_s1dssbypass(struct kunit *test)
{
	struct arm_smmu_ste s1_ste;
	struct arm_smmu_ste s2_ste;

	arm_smmu_test_make_cdtable_ste(
		&s1_ste, STRTAB_STE_1_S1DSS_BYPASS, fake_cdtab_dma_addr,
		ARM_SMMU_MASTER_TEST_ATS | ARM_SMMU_MASTER_TEST_NESTED);
	arm_smmu_test_make_s2_ste(&s2_ste, 0);
	arm_smmu_v3_test_ste_expect_hitless_transition(test, &s2_ste, &s1_ste,
						       NUM_EXPECTED_SYNCS(2));
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

static void arm_smmu_v3_invs_test_verify(struct kunit *test,
					 struct arm_smmu_invs *invs,
					 int num_invs, const int num_trashes,
					 const int *ids, const int *users,
					 const int *ssids)
{
	KUNIT_EXPECT_EQ(test, invs->num_invs, num_invs);
	KUNIT_EXPECT_EQ(test, invs->num_trashes, num_trashes);
	while (num_invs--) {
		KUNIT_EXPECT_EQ(test, invs->inv[num_invs].id, ids[num_invs]);
		KUNIT_EXPECT_EQ(test, READ_ONCE(invs->inv[num_invs].users),
				users[num_invs]);
		KUNIT_EXPECT_EQ(test, invs->inv[num_invs].ssid, ssids[num_invs]);
	}
}

static struct arm_smmu_invs invs1 = {
	.num_invs = 3,
	.inv = { { .type = INV_TYPE_S2_VMID, .id = 1, },
		 { .type = INV_TYPE_S2_VMID_S1_CLEAR, .id = 1, },
		 { .type = INV_TYPE_ATS, .id = 3, }, },
};

static struct arm_smmu_invs invs2 = {
	.num_invs = 3,
	.inv = { { .type = INV_TYPE_S2_VMID, .id = 1, }, /* duplicated */
		 { .type = INV_TYPE_ATS, .id = 4, },
		 { .type = INV_TYPE_ATS, .id = 5, }, },
};

static struct arm_smmu_invs invs3 = {
	.num_invs = 3,
	.inv = { { .type = INV_TYPE_S2_VMID, .id = 1, }, /* duplicated */
		 { .type = INV_TYPE_ATS, .id = 5, }, /* recover a trash */
		 { .type = INV_TYPE_ATS, .id = 6, }, },
};

static struct arm_smmu_invs invs4 = {
	.num_invs = 3,
	.inv = { { .type = INV_TYPE_ATS, .id = 10, .ssid = 1 },
		 { .type = INV_TYPE_ATS, .id = 10, .ssid = 3 },
		 { .type = INV_TYPE_ATS, .id = 12, .ssid = 1 }, },
};

static struct arm_smmu_invs invs5 = {
	.num_invs = 3,
	.inv = { { .type = INV_TYPE_ATS, .id = 10, .ssid = 2 },
		 { .type = INV_TYPE_ATS, .id = 10, .ssid = 3 }, /* duplicate */
		 { .type = INV_TYPE_ATS, .id = 12, .ssid = 2 }, },
};

static void arm_smmu_v3_invs_test(struct kunit *test)
{
	const int results1[3][3] = { { 1, 1, 3, }, { 1, 1, 1, }, { 0, 0, 0, } };
	const int results2[3][5] = { { 1, 1, 3, 4, 5, }, { 2, 1, 1, 1, 1, }, { 0, 0, 0, 0, 0, } };
	const int results3[3][3] = { { 1, 1, 3, }, { 1, 1, 1, }, { 0, 0, 0, } };
	const int results4[3][5] = { { 1, 1, 3, 5, 6, }, { 2, 1, 1, 1, 1, }, { 0, 0, 0, 0, 0, } };
	const int results5[3][5] = { { 1, 1, 3, 5, 6, }, { 1, 0, 0, 1, 1, }, { 0, 0, 0, 0, 0, } };
	const int results6[3][3] = { { 1, 5, 6, }, { 1, 1, 1, }, { 0, 0, 0, } };
	const int results7[3][3] = { { 10, 10, 12, }, { 1, 1, 1, }, { 1, 3, 1, } };
	const int results8[3][5] = { { 10, 10, 10, 12, 12, }, { 1, 1, 2, 1, 1, }, { 1, 2, 3, 1, 2, } };
	const int results9[3][4] = { { 10, 10, 10, 12, }, { 1, 0, 1, 1, }, { 1, 2, 3, 1, } };
	const int results10[3][3] = { { 10, 10, 12, }, { 1, 1, 1, }, { 1, 3, 1, } };
	struct arm_smmu_invs *test_a, *test_b;

	/* New array */
	test_a = arm_smmu_invs_alloc(0);
	KUNIT_EXPECT_EQ(test, test_a->num_invs, 0);

	/* Test1: merge invs1 (new array) */
	test_b = arm_smmu_invs_merge(test_a, &invs1);
	kfree(test_a);
	arm_smmu_v3_invs_test_verify(test, test_b, ARRAY_SIZE(results1[0]), 0,
				     results1[0], results1[1], results1[2]);

	/* Test2: merge invs2 (new array) */
	test_a = arm_smmu_invs_merge(test_b, &invs2);
	kfree(test_b);
	arm_smmu_v3_invs_test_verify(test, test_a, ARRAY_SIZE(results2[0]), 0,
				     results2[0], results2[1], results2[2]);

	/* Test3: unref invs2 (same array) */
	arm_smmu_invs_unref(test_a, &invs2);
	arm_smmu_v3_invs_test_verify(test, test_a, ARRAY_SIZE(results3[0]), 0,
				     results3[0], results3[1], results3[2]);

	/* Test4: merge invs3 (new array) */
	test_b = arm_smmu_invs_merge(test_a, &invs3);
	kfree(test_a);
	arm_smmu_v3_invs_test_verify(test, test_b, ARRAY_SIZE(results4[0]), 0,
				     results4[0], results4[1], results4[2]);

	/* Test5: unref invs1 (same array) */
	arm_smmu_invs_unref(test_b, &invs1);
	arm_smmu_v3_invs_test_verify(test, test_b, ARRAY_SIZE(results5[0]), 2,
				     results5[0], results5[1], results5[2]);

	/* Test6: purge test_b (new array) */
	test_a = arm_smmu_invs_purge(test_b);
	kfree(test_b);
	arm_smmu_v3_invs_test_verify(test, test_a, ARRAY_SIZE(results6[0]), 0,
				     results6[0], results6[1], results6[2]);

	/* Test7: unref invs3 (same array) */
	arm_smmu_invs_unref(test_a, &invs3);
	KUNIT_EXPECT_EQ(test, test_a->num_invs, 0);
	KUNIT_EXPECT_EQ(test, test_a->num_trashes, 0);

	/* Test8: merge invs4 (new array) */
	test_b = arm_smmu_invs_merge(test_a, &invs4);
	kfree(test_a);
	arm_smmu_v3_invs_test_verify(test, test_b, ARRAY_SIZE(results7[0]), 0,
				     results7[0], results7[1], results7[2]);

	/* Test9: merge invs5 (new array) */
	test_a = arm_smmu_invs_merge(test_b, &invs5);
	kfree(test_b);
	arm_smmu_v3_invs_test_verify(test, test_a, ARRAY_SIZE(results8[0]), 0,
				     results8[0], results8[1], results8[2]);

	/* Test10: unref invs5 (same array) */
	arm_smmu_invs_unref(test_a, &invs5);
	arm_smmu_v3_invs_test_verify(test, test_a, ARRAY_SIZE(results9[0]), 1,
				     results9[0], results9[1], results9[2]);

	/* Test11: purge test_a (new array) */
	test_b = arm_smmu_invs_purge(test_a);
	kfree(test_a);
	arm_smmu_v3_invs_test_verify(test, test_b, ARRAY_SIZE(results10[0]), 0,
				     results10[0], results10[1], results10[2]);

	kfree(test_b);
}

static struct kunit_case arm_smmu_v3_test_cases[] = {
	KUNIT_CASE(arm_smmu_v3_write_ste_test_bypass_to_abort),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_abort_to_bypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_cdtable_to_abort),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_abort_to_cdtable),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_cdtable_to_bypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_bypass_to_cdtable),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_cdtable_s1dss_change),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s1dssbypass_to_stebypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_stebypass_to_s1dssbypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s2_to_abort),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_abort_to_s2),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s2_to_bypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_bypass_to_s2),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s1_to_s2),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s2_to_s1),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_non_hitless),
	KUNIT_CASE(arm_smmu_v3_write_cd_test_s1_clear),
	KUNIT_CASE(arm_smmu_v3_write_cd_test_s1_change_asid),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s1_to_s2_stall),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_s2_to_s1_stall),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_nested_s1dssbypass_to_s1bypass),
	KUNIT_CASE(arm_smmu_v3_write_ste_test_nested_s1bypass_to_s1dssbypass),
	KUNIT_CASE(arm_smmu_v3_write_cd_test_sva_clear),
	KUNIT_CASE(arm_smmu_v3_write_cd_test_sva_release),
	KUNIT_CASE(arm_smmu_v3_invs_test),
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

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_DESCRIPTION("KUnit tests for arm-smmu-v3 driver");
MODULE_LICENSE("GPL v2");
