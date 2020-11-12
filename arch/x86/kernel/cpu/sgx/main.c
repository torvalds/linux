// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <linux/freezer.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/ratelimit.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include "driver.h"
#include "encl.h"
#include "encls.h"

struct sgx_epc_section sgx_epc_sections[SGX_MAX_EPC_SECTIONS];
static int sgx_nr_epc_sections;
static struct task_struct *ksgxd_tsk;

/*
 * Reset dirty EPC pages to uninitialized state. Laundry can be left with SECS
 * pages whose child pages blocked EREMOVE.
 */
static void sgx_sanitize_section(struct sgx_epc_section *section)
{
	struct sgx_epc_page *page;
	LIST_HEAD(dirty);
	int ret;

	while (!list_empty(&section->laundry_list)) {
		if (kthread_should_stop())
			return;

		spin_lock(&section->lock);

		page = list_first_entry(&section->laundry_list,
					struct sgx_epc_page, list);

		ret = __eremove(sgx_get_epc_virt_addr(page));
		if (!ret)
			list_move(&page->list, &section->page_list);
		else
			list_move_tail(&page->list, &dirty);

		spin_unlock(&section->lock);

		cond_resched();
	}

	list_splice(&dirty, &section->laundry_list);
}

static int ksgxd(void *p)
{
	int i;

	set_freezable();

	/*
	 * Sanitize pages in order to recover from kexec(). The 2nd pass is
	 * required for SECS pages, whose child pages blocked EREMOVE.
	 */
	for (i = 0; i < sgx_nr_epc_sections; i++)
		sgx_sanitize_section(&sgx_epc_sections[i]);

	for (i = 0; i < sgx_nr_epc_sections; i++) {
		sgx_sanitize_section(&sgx_epc_sections[i]);

		/* Should never happen. */
		if (!list_empty(&sgx_epc_sections[i].laundry_list))
			WARN(1, "EPC section %d has unsanitized pages.\n", i);
	}

	return 0;
}

static bool __init sgx_page_reclaimer_init(void)
{
	struct task_struct *tsk;

	tsk = kthread_run(ksgxd, NULL, "ksgxd");
	if (IS_ERR(tsk))
		return false;

	ksgxd_tsk = tsk;

	return true;
}

static struct sgx_epc_page *__sgx_alloc_epc_page_from_section(struct sgx_epc_section *section)
{
	struct sgx_epc_page *page;

	spin_lock(&section->lock);

	if (list_empty(&section->page_list)) {
		spin_unlock(&section->lock);
		return NULL;
	}

	page = list_first_entry(&section->page_list, struct sgx_epc_page, list);
	list_del_init(&page->list);

	spin_unlock(&section->lock);
	return page;
}

/**
 * __sgx_alloc_epc_page() - Allocate an EPC page
 *
 * Iterate through EPC sections and borrow a free EPC page to the caller. When a
 * page is no longer needed it must be released with sgx_free_epc_page().
 *
 * Return:
 *   an EPC page,
 *   -errno on error
 */
struct sgx_epc_page *__sgx_alloc_epc_page(void)
{
	struct sgx_epc_section *section;
	struct sgx_epc_page *page;
	int i;

	for (i = 0; i < sgx_nr_epc_sections; i++) {
		section = &sgx_epc_sections[i];

		page = __sgx_alloc_epc_page_from_section(section);
		if (page)
			return page;
	}

	return ERR_PTR(-ENOMEM);
}

/**
 * sgx_free_epc_page() - Free an EPC page
 * @page:	an EPC page
 *
 * Call EREMOVE for an EPC page and insert it back to the list of free pages.
 */
void sgx_free_epc_page(struct sgx_epc_page *page)
{
	struct sgx_epc_section *section = &sgx_epc_sections[page->section];
	int ret;

	ret = __eremove(sgx_get_epc_virt_addr(page));
	if (WARN_ONCE(ret, "EREMOVE returned %d (0x%x)", ret, ret))
		return;

	spin_lock(&section->lock);
	list_add_tail(&page->list, &section->page_list);
	spin_unlock(&section->lock);
}

static bool __init sgx_setup_epc_section(u64 phys_addr, u64 size,
					 unsigned long index,
					 struct sgx_epc_section *section)
{
	unsigned long nr_pages = size >> PAGE_SHIFT;
	unsigned long i;

	section->virt_addr = memremap(phys_addr, size, MEMREMAP_WB);
	if (!section->virt_addr)
		return false;

	section->pages = vmalloc(nr_pages * sizeof(struct sgx_epc_page));
	if (!section->pages) {
		memunmap(section->virt_addr);
		return false;
	}

	section->phys_addr = phys_addr;
	spin_lock_init(&section->lock);
	INIT_LIST_HEAD(&section->page_list);
	INIT_LIST_HEAD(&section->laundry_list);

	for (i = 0; i < nr_pages; i++) {
		section->pages[i].section = index;
		list_add_tail(&section->pages[i].list, &section->laundry_list);
	}

	return true;
}

/**
 * A section metric is concatenated in a way that @low bits 12-31 define the
 * bits 12-31 of the metric and @high bits 0-19 define the bits 32-51 of the
 * metric.
 */
static inline u64 __init sgx_calc_section_metric(u64 low, u64 high)
{
	return (low & GENMASK_ULL(31, 12)) +
	       ((high & GENMASK_ULL(19, 0)) << 32);
}

static bool __init sgx_page_cache_init(void)
{
	u32 eax, ebx, ecx, edx, type;
	u64 pa, size;
	int i;

	for (i = 0; i < ARRAY_SIZE(sgx_epc_sections); i++) {
		cpuid_count(SGX_CPUID, i + SGX_CPUID_EPC, &eax, &ebx, &ecx, &edx);

		type = eax & SGX_CPUID_EPC_MASK;
		if (type == SGX_CPUID_EPC_INVALID)
			break;

		if (type != SGX_CPUID_EPC_SECTION) {
			pr_err_once("Unknown EPC section type: %u\n", type);
			break;
		}

		pa   = sgx_calc_section_metric(eax, ebx);
		size = sgx_calc_section_metric(ecx, edx);

		pr_info("EPC section 0x%llx-0x%llx\n", pa, pa + size - 1);

		if (!sgx_setup_epc_section(pa, size, i, &sgx_epc_sections[i])) {
			pr_err("No free memory for an EPC section\n");
			break;
		}

		sgx_nr_epc_sections++;
	}

	if (!sgx_nr_epc_sections) {
		pr_err("There are zero EPC sections.\n");
		return false;
	}

	return true;
}

static void __init sgx_init(void)
{
	int ret;
	int i;

	if (!cpu_feature_enabled(X86_FEATURE_SGX))
		return;

	if (!sgx_page_cache_init())
		return;

	if (!sgx_page_reclaimer_init())
		goto err_page_cache;

	ret = sgx_drv_init();
	if (ret)
		goto err_kthread;

	return;

err_kthread:
	kthread_stop(ksgxd_tsk);

err_page_cache:
	for (i = 0; i < sgx_nr_epc_sections; i++) {
		vfree(sgx_epc_sections[i].pages);
		memunmap(sgx_epc_sections[i].virt_addr);
	}
}

device_initcall(sgx_init);
