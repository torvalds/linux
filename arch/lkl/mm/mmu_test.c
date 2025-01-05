// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/vmalloc.h>

#include <asm/page.h>

static void vmalloc_test(struct kunit *test)
{
	unsigned long nr_pages = 255;
	void *ptr = vmalloc(nr_pages * PAGE_SIZE);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	for (int i = 0; i < nr_pages; i++)
		memset(ptr + i * PAGE_SIZE, i, PAGE_SIZE);

	for (int i = 0; i < nr_pages; i++) {
		struct page *pg = vmalloc_to_page(ptr + i * PAGE_SIZE);

		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pg);

		unsigned char *va = (unsigned char *)kmap_local_page(pg);

		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, va);
		KUNIT_ASSERT_PTR_NE(test, va, ptr + i * PAGE_SIZE);

		KUNIT_ASSERT_TRUE(test, va[0] == (unsigned char)i);

		kunmap_local(va);
	}

	vfree(ptr);
}

static struct kunit_case mmu_kunit_test_cases[] = {
	KUNIT_CASE(vmalloc_test),
	{}
};

static struct kunit_suite lkl_mmu_kunit_test_suite = {
	.name = "lkl_mmu",
	.test_cases = mmu_kunit_test_cases,
};

kunit_test_suite(lkl_mmu_kunit_test_suite);

MODULE_LICENSE("GPL");
