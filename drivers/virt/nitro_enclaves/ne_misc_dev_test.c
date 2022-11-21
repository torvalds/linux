// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#define MAX_PHYS_REGIONS	16
#define INVALID_VALUE		(~0ull)

struct ne_phys_regions_test {
	u64           paddr;
	u64           size;
	int           expect_rc;
	unsigned long expect_num;
	u64           expect_last_paddr;
	u64           expect_last_size;
} phys_regions_test_cases[] = {
	/*
	 * Add the region from 0x1000 to (0x1000 + 0x200000 - 1):
	 *   Expected result:
	 *       Failed, start address is not 2M-aligned
	 *
	 * Now the instance of struct ne_phys_contig_mem_regions is:
	 *   num = 0
	 *   regions = {}
	 */
	{0x1000, 0x200000, -EINVAL, 0, INVALID_VALUE, INVALID_VALUE},

	/*
	 * Add the region from 0x200000 to (0x200000 + 0x1000 - 1):
	 *   Expected result:
	 *       Failed, size is not 2M-aligned
	 *
	 * Now the instance of struct ne_phys_contig_mem_regions is:
	 *   num = 0
	 *   regions = {}
	 */
	{0x200000, 0x1000, -EINVAL, 0, INVALID_VALUE, INVALID_VALUE},

	/*
	 * Add the region from 0x200000 to (0x200000 + 0x200000 - 1):
	 *   Expected result:
	 *       Successful
	 *
	 * Now the instance of struct ne_phys_contig_mem_regions is:
	 *   num = 1
	 *   regions = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *   }
	 */
	{0x200000, 0x200000, 0, 1, 0x200000, 0x200000},

	/*
	 * Add the region from 0x0 to (0x0 + 0x200000 - 1):
	 *   Expected result:
	 *       Successful
	 *
	 * Now the instance of struct ne_phys_contig_mem_regions is:
	 *   num = 2
	 *   regions = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *       {start=0x0,      end=0x1fffff}, // len=0x200000
	 *   }
	 */
	{0x0, 0x200000, 0, 2, 0x0, 0x200000},

	/*
	 * Add the region from 0x600000 to (0x600000 + 0x400000 - 1):
	 *   Expected result:
	 *       Successful
	 *
	 * Now the instance of struct ne_phys_contig_mem_regions is:
	 *   num = 3
	 *   regions = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *       {start=0x0,      end=0x1fffff}, // len=0x200000
	 *       {start=0x600000, end=0x9fffff}, // len=0x400000
	 *   }
	 */
	{0x600000, 0x400000, 0, 3, 0x600000, 0x400000},

	/*
	 * Add the region from 0xa00000 to (0xa00000 + 0x400000 - 1):
	 *   Expected result:
	 *       Successful, merging case!
	 *
	 * Now the instance of struct ne_phys_contig_mem_regions is:
	 *   num = 3
	 *   regions = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *       {start=0x0,      end=0x1fffff}, // len=0x200000
	 *       {start=0x600000, end=0xdfffff}, // len=0x800000
	 *   }
	 */
	{0xa00000, 0x400000, 0, 3, 0x600000, 0x800000},

	/*
	 * Add the region from 0x1000 to (0x1000 + 0x200000 - 1):
	 *   Expected result:
	 *       Failed, start address is not 2M-aligned
	 *
	 * Now the instance of struct ne_phys_contig_mem_regions is:
	 *   num = 3
	 *   regions = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *       {start=0x0,      end=0x1fffff}, // len=0x200000
	 *       {start=0x600000, end=0xdfffff}, // len=0x800000
	 *   }
	 */
	{0x1000, 0x200000, -EINVAL, 3, 0x600000, 0x800000},
};

static void ne_misc_dev_test_merge_phys_contig_memory_regions(struct kunit *test)
{
	struct ne_phys_contig_mem_regions phys_contig_mem_regions = {};
	int rc = 0;
	int i = 0;

	phys_contig_mem_regions.regions = kunit_kcalloc(test, MAX_PHYS_REGIONS,
							sizeof(*phys_contig_mem_regions.regions),
							GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, phys_contig_mem_regions.regions);

	for (i = 0; i < ARRAY_SIZE(phys_regions_test_cases); i++) {
		struct ne_phys_regions_test *test_case = &phys_regions_test_cases[i];
		unsigned long num = 0;

		rc = ne_merge_phys_contig_memory_regions(&phys_contig_mem_regions,
							 test_case->paddr, test_case->size);
		KUNIT_EXPECT_EQ(test, rc, test_case->expect_rc);
		KUNIT_EXPECT_EQ(test, phys_contig_mem_regions.num, test_case->expect_num);

		if (test_case->expect_last_paddr == INVALID_VALUE)
			continue;

		num = phys_contig_mem_regions.num;
		KUNIT_EXPECT_EQ(test, phys_contig_mem_regions.regions[num - 1].start,
				test_case->expect_last_paddr);
		KUNIT_EXPECT_EQ(test, range_len(&phys_contig_mem_regions.regions[num - 1]),
				test_case->expect_last_size);
	}

	kunit_kfree(test, phys_contig_mem_regions.regions);
}

static struct kunit_case ne_misc_dev_test_cases[] = {
	KUNIT_CASE(ne_misc_dev_test_merge_phys_contig_memory_regions),
	{}
};

static struct kunit_suite ne_misc_dev_test_suite = {
	.name = "ne_misc_dev_test",
	.test_cases = ne_misc_dev_test_cases,
};

static struct kunit_suite *ne_misc_dev_test_suites[] = {
	&ne_misc_dev_test_suite,
	NULL
};
