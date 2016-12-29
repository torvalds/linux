/*
 *
 * (C) COPYRIGHT 2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#define NR_BYTES_PER_CNT  4
#define NR_CNT_PER_BLOCK 64

#define JM_BASE    (0 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT)
#define TILER_BASE (1 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT)
#define MMU_BASE   (2 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT)
#define SC0_BASE   (3 * NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT)

#define GPU_ACTIVE       (JM_BASE    + NR_BYTES_PER_CNT *  6)
#define TILER_ACTIVE     (TILER_BASE + NR_BYTES_PER_CNT * 45)
#define L2_ANY_LOOKUP    (MMU_BASE   + NR_BYTES_PER_CNT * 25)
#define FRAG_ACTIVE      (SC0_BASE   + NR_BYTES_PER_CNT *  4)
#define EXEC_CORE_ACTIVE (SC0_BASE   + NR_BYTES_PER_CNT * 26)
#define EXEC_INSTR_COUNT (SC0_BASE   + NR_BYTES_PER_CNT * 28)
#define TEX_COORD_ISSUE  (SC0_BASE   + NR_BYTES_PER_CNT * 40)
#define VARY_SLOT_32     (SC0_BASE   + NR_BYTES_PER_CNT * 50)
#define VARY_SLOT_16     (SC0_BASE   + NR_BYTES_PER_CNT * 51)
#define BEATS_RD_LSC     (SC0_BASE   + NR_BYTES_PER_CNT * 56)
#define BEATS_WR_LSC     (SC0_BASE   + NR_BYTES_PER_CNT * 61)

static u32 calc_power_group0(struct kbase_ipa_context *ctx,
		struct ipa_group *group);
static u32 calc_power_group1(struct kbase_ipa_context *ctx,
		struct ipa_group *group);
static u32 calc_power_group2(struct kbase_ipa_context *ctx,
		struct ipa_group *group);
static u32 calc_power_group3(struct kbase_ipa_context *ctx,
		struct ipa_group *group);
static u32 calc_power_group4(struct kbase_ipa_context *ctx,
		struct ipa_group *group);
static u32 calc_power_group5(struct kbase_ipa_context *ctx,
		struct ipa_group *group);
static u32 calc_power_group6(struct kbase_ipa_context *ctx,
		struct ipa_group *group);
static u32 calc_power_group7(struct kbase_ipa_context *ctx,
		struct ipa_group *group);

static struct ipa_group ipa_groups_def[] = {
	/* L2 */
	{
		.name = "group0",
		.capacitance = 687,
		.calc_power = calc_power_group0,
	},
	/* TILER */
	{
		.name = "group1",
		.capacitance = 0,
		.calc_power = calc_power_group1,
	},
	/* FRAG */
	{
		.name = "group2",
		.capacitance = 23,
		.calc_power = calc_power_group2,
	},
	/* VARY */
	{
		.name = "group3",
		.capacitance = 108,
		.calc_power = calc_power_group3,
	},
	/* TEX */
	{
		.name = "group4",
		.capacitance = 128,
		.calc_power = calc_power_group4,
	},
	/* EXEC INSTR */
	{
		.name = "group5",
		.capacitance = 249,
		.calc_power = calc_power_group5,
	},
	/* LSC */
	{
		.name = "group6",
		.capacitance = 0,
		.calc_power = calc_power_group6,
	},
	/* EXEC OVERHEAD */
	{
		.name = "group7",
		.capacitance = 29,
		.calc_power = calc_power_group7,
	},
};
