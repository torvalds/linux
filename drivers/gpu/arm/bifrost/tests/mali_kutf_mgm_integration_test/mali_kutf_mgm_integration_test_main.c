// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */
#include <linux/module.h>
#include "mali_kbase.h"
#include <kutf/kutf_suite.h>
#include <kutf/kutf_utils.h>
#include <kutf/kutf_helpers.h>
#include <kutf/kutf_helpers_user.h>

#define MINOR_FOR_FIRST_KBASE_DEV (-1)

#define BASE_MEM_GROUP_COUNT (16)
#define PA_MAX ((1ULL << 48) - 1)
#define PA_START_BIT 12
#define ENTRY_ACCESS_BIT (1ULL << 10)

#define ENTRY_IS_ATE_L3 3ULL
#define ENTRY_IS_ATE_L02 1ULL

#define MGM_INTEGRATION_SUITE_NAME "mgm_integration"
#define MGM_INTEGRATION_PTE_TRANSLATION "pte_translation"

static char msg_buf[KUTF_MAX_LINE_LENGTH];

/* KUTF test application pointer for this test */
struct kutf_application *mgm_app;

/**
 * struct kutf_mgm_fixture_data - test fixture used by test functions
 * @kbdev: kbase device for the GPU.
 * @group_id: Memory group ID to test based on fixture index.
 */
struct kutf_mgm_fixture_data {
	struct kbase_device *kbdev;
	int group_id;
};

/**
 * mali_kutf_mgm_pte_translation_test() -  Tests forward and reverse translation
 * of PTE by the MGM module
 * @context: KUTF context within which to perform the test.
 *
 * This test creates PTEs with physical addresses in the range
 * 0x0000-0xFFFFFFFFF000 and tests that mgm_update_gpu_pte() returns a different
 * PTE and mgm_pte_to_original_pte() returns the original PTE. This is tested
 * at MMU level 2 and 3 as mgm_update_gpu_pte() is called for ATEs only.
 *
 * This test is run for a specific group_id depending on the fixture_id.
 */
static void mali_kutf_mgm_pte_translation_test(struct kutf_context *context)
{
	struct kutf_mgm_fixture_data *data = context->fixture;
	struct kbase_device *kbdev = data->kbdev;
	struct memory_group_manager_device *mgm_dev = kbdev->mgm_dev;
	u64 addr;

	for (addr = 1 << (PA_START_BIT - 1); addr <= PA_MAX; addr <<= 1) {
		/* Mask 1 << 11 by ~0xFFF to get 0x0000 at first iteration */
		phys_addr_t pa = addr;
		u8 mmu_level;

		/* Test MMU level 3 and 2 (2MB pages) only */
		for (mmu_level = MIDGARD_MMU_LEVEL(2); mmu_level <= MIDGARD_MMU_LEVEL(3);
		     mmu_level++) {
			u64 translated_pte;
			u64 returned_pte;
			u64 original_pte;

			if (mmu_level == MIDGARD_MMU_LEVEL(3))
				original_pte =
					(pa & PAGE_MASK) | ENTRY_ACCESS_BIT | ENTRY_IS_ATE_L3;
			else
				original_pte =
					(pa & PAGE_MASK) | ENTRY_ACCESS_BIT | ENTRY_IS_ATE_L02;

			dev_dbg(kbdev->dev, "Testing group_id=%u, mmu_level=%u, pte=0x%llx\n",
				data->group_id, mmu_level, original_pte);

			translated_pte = mgm_dev->ops.mgm_update_gpu_pte(mgm_dev, data->group_id,
									 mmu_level, original_pte);
			if (translated_pte == original_pte) {
				snprintf(
					msg_buf, sizeof(msg_buf),
					"PTE unchanged. translated_pte (0x%llx) == original_pte (0x%llx) for mmu_level=%u, group_id=%d",
					translated_pte, original_pte, mmu_level, data->group_id);
				kutf_test_fail(context, msg_buf);
				return;
			}

			returned_pte = mgm_dev->ops.mgm_pte_to_original_pte(
				mgm_dev, data->group_id, mmu_level, translated_pte);
			dev_dbg(kbdev->dev, "\treturned_pte=%llx\n", returned_pte);

			if (returned_pte != original_pte) {
				snprintf(
					msg_buf, sizeof(msg_buf),
					"Original PTE not returned. returned_pte (0x%llx) != origin al_pte (0x%llx) for mmu_level=%u, group_id=%d",
					returned_pte, original_pte, mmu_level, data->group_id);
				kutf_test_fail(context, msg_buf);
				return;
			}
		}
	}
	snprintf(msg_buf, sizeof(msg_buf), "Translation passed for group_id=%d", data->group_id);
	kutf_test_pass(context, msg_buf);
}

/**
 * mali_kutf_mgm_integration_create_fixture() - Creates the fixture data
 *                   required for all tests in the mgm integration suite.
 * @context: KUTF context.
 *
 * Return: Fixture data created on success or NULL on failure
 */
static void *mali_kutf_mgm_integration_create_fixture(struct kutf_context *context)
{
	struct kutf_mgm_fixture_data *data;
	struct kbase_device *kbdev;

	pr_debug("Finding kbase device\n");
	kbdev = kbase_find_device(MINOR_FOR_FIRST_KBASE_DEV);
	if (kbdev == NULL) {
		kutf_test_fail(context, "Failed to find kbase device");
		return NULL;
	}
	pr_debug("Creating fixture\n");

	data = kutf_mempool_alloc(&context->fixture_pool, sizeof(struct kutf_mgm_fixture_data));
	if (!data)
		return NULL;
	data->kbdev = kbdev;
	data->group_id = context->fixture_index;

	pr_debug("Fixture created\n");
	return data;
}

/**
 * mali_kutf_mgm_integration_remove_fixture() - Destroy fixture data previously
 *                          created by mali_kutf_mgm_integration_create_fixture.
 * @context: KUTF context.
 */
static void mali_kutf_mgm_integration_remove_fixture(struct kutf_context *context)
{
	struct kutf_mgm_fixture_data *data = context->fixture;
	struct kbase_device *kbdev = data->kbdev;

	kbase_release_device(kbdev);
}

/**
 * mali_kutf_mgm_integration_test_main_init() - Module entry point for this test.
 *
 * Return: 0 on success, error code on failure.
 */
static int __init mali_kutf_mgm_integration_test_main_init(void)
{
	struct kutf_suite *suite;

	mgm_app = kutf_create_application("mgm");

	if (mgm_app == NULL) {
		pr_warn("Creation of mgm KUTF app failed!\n");
		return -ENOMEM;
	}
	suite = kutf_create_suite(mgm_app, MGM_INTEGRATION_SUITE_NAME, BASE_MEM_GROUP_COUNT,
				  mali_kutf_mgm_integration_create_fixture,
				  mali_kutf_mgm_integration_remove_fixture);
	if (suite == NULL) {
		pr_warn("Creation of %s suite failed!\n", MGM_INTEGRATION_SUITE_NAME);
		kutf_destroy_application(mgm_app);
		return -ENOMEM;
	}
	kutf_add_test(suite, 0x0, MGM_INTEGRATION_PTE_TRANSLATION,
		      mali_kutf_mgm_pte_translation_test);
	return 0;
}

/**
 * mali_kutf_mgm_integration_test_main_exit() - Module exit point for this test.
 */
static void __exit mali_kutf_mgm_integration_test_main_exit(void)
{
	kutf_destroy_application(mgm_app);
}

module_init(mali_kutf_mgm_integration_test_main_init);
module_exit(mali_kutf_mgm_integration_test_main_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION("1.0");
