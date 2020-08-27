// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2012 ARM Limited
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/cache.h>
#include <linux/clocksource.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/timekeeper_internal.h>
#include <linux/vmalloc.h>
#include <linux/random.h>

#include <asm/cacheflush.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>
#include <asm/vdso_timer_info.h>
#include <asm/cache_info.h>
extern struct cache_info L1_cache_info[2];
extern char vdso_start[], vdso_end[];
static unsigned long vdso_pages __ro_after_init;
static unsigned long timer_mapping_base;

struct timer_info_t timer_info = {
	.cycle_count_down = true,
	.mapping_base = EMPTY_TIMER_MAPPING,
	.cycle_count_reg_offset = EMPTY_REG_OFFSET
};
/*
 * The vDSO data page.
 */
static struct page *no_pages[] = { NULL };

static union {
	struct vdso_data data;
	u8 page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;
struct vdso_data *vdso_data = &vdso_data_store.data;
static struct vm_special_mapping vdso_spec[2] __ro_after_init = {
	{
	 .name = "[vvar]",
	 .pages = no_pages,
	 },
	{
	 .name = "[vdso]",
	 },
};

static void get_timer_node_info(void)
{
	timer_mapping_base = timer_info.mapping_base;
	vdso_data->cycle_count_offset =
		timer_info.cycle_count_reg_offset;
	vdso_data->cycle_count_down =
		timer_info.cycle_count_down;
}

static int __init vdso_init(void)
{
	int i;
	struct page **vdso_pagelist;

	if (memcmp(vdso_start, "\177ELF", 4)) {
		pr_err("vDSO is not a valid ELF object!\n");
		return -EINVAL;
	}
	/* Creat a timer io mapping to get clock cycles counter */
	get_timer_node_info();

	vdso_pages = (vdso_end - vdso_start) >> PAGE_SHIFT;
	pr_info("vdso: %ld pages (%ld code @ %p, %ld data @ %p)\n",
		vdso_pages + 1, vdso_pages, vdso_start, 1L, vdso_data);

	/* Allocate the vDSO pagelist */
	vdso_pagelist = kcalloc(vdso_pages, sizeof(struct page *), GFP_KERNEL);
	if (vdso_pagelist == NULL)
		return -ENOMEM;

	for (i = 0; i < vdso_pages; i++)
		vdso_pagelist[i] = virt_to_page(vdso_start + i * PAGE_SIZE);
	vdso_spec[1].pages = &vdso_pagelist[0];

	return 0;
}

arch_initcall(vdso_init);

unsigned long inline vdso_random_addr(unsigned long vdso_mapping_len)
{
	unsigned long start = current->mm->mmap_base, end, offset, addr;
	start = PAGE_ALIGN(start);

	/* Round the lowest possible end address up to a PMD boundary. */
	end = (start + vdso_mapping_len + PMD_SIZE - 1) & PMD_MASK;
	if (end >= TASK_SIZE)
		end = TASK_SIZE;
	end -= vdso_mapping_len;

	if (end > start) {
		offset = get_random_int() % (((end - start) >> PAGE_SHIFT) + 1);
		addr = start + (offset << PAGE_SHIFT);
	} else {
		addr = start;
	}
	return addr;
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	unsigned long vdso_base, vdso_text_len, vdso_mapping_len;
	struct vm_area_struct *vma;
	unsigned long addr = 0;
	pgprot_t prot;
	int ret, vvar_page_num = 2;

	vdso_text_len = vdso_pages << PAGE_SHIFT;

	if(timer_mapping_base == EMPTY_VALUE)
		vvar_page_num = 1;
	/* Be sure to map the data page */
	vdso_mapping_len = vdso_text_len + vvar_page_num * PAGE_SIZE;
#ifdef CONFIG_CPU_CACHE_ALIASING
	vdso_mapping_len += L1_cache_info[DCACHE].aliasing_num - 1;
#endif

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	addr = vdso_random_addr(vdso_mapping_len);
	vdso_base = get_unmapped_area(NULL, addr, vdso_mapping_len, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		ret = vdso_base;
		goto up_fail;
	}

#ifdef CONFIG_CPU_CACHE_ALIASING
	{
		unsigned int aliasing_mask =
		    L1_cache_info[DCACHE].aliasing_mask;
		unsigned int page_colour_ofs;
		page_colour_ofs = ((unsigned int)vdso_data & aliasing_mask) -
		    (vdso_base & aliasing_mask);
		vdso_base += page_colour_ofs & aliasing_mask;
	}
#endif

	vma = _install_special_mapping(mm, vdso_base, vvar_page_num * PAGE_SIZE,
				       VM_READ | VM_MAYREAD, &vdso_spec[0]);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	/*Map vdata to user space */
	ret = io_remap_pfn_range(vma, vdso_base,
				 virt_to_phys(vdso_data) >> PAGE_SHIFT,
				 PAGE_SIZE, vma->vm_page_prot);
	if (ret)
		goto up_fail;

	/*Map timer to user space */
	vdso_base += PAGE_SIZE;
	prot = __pgprot(_PAGE_V | _PAGE_M_UR_KR | _PAGE_D |  _PAGE_C_DEV);
	ret = io_remap_pfn_range(vma, vdso_base, timer_mapping_base >> PAGE_SHIFT,
				 PAGE_SIZE, prot);
	if (ret)
		goto up_fail;

	/*Map vdso to user space */
	vdso_base += PAGE_SIZE;
	mm->context.vdso = (void *)vdso_base;
	vma = _install_special_mapping(mm, vdso_base, vdso_text_len,
				       VM_READ | VM_EXEC |
				       VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
				       &vdso_spec[1]);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	mmap_write_unlock(mm);
	return 0;

up_fail:
	mm->context.vdso = NULL;
	mmap_write_unlock(mm);
	return ret;
}

static void vdso_write_begin(struct vdso_data *vdata)
{
	++vdso_data->seq_count;
	smp_wmb();		/* Pairs with smp_rmb in vdso_read_retry */
}

static void vdso_write_end(struct vdso_data *vdata)
{
	smp_wmb();		/* Pairs with smp_rmb in vdso_read_begin */
	++vdso_data->seq_count;
}

void update_vsyscall(struct timekeeper *tk)
{
	vdso_write_begin(vdso_data);
	vdso_data->cs_mask = tk->tkr_mono.mask;
	vdso_data->cs_mult = tk->tkr_mono.mult;
	vdso_data->cs_shift = tk->tkr_mono.shift;
	vdso_data->cs_cycle_last = tk->tkr_mono.cycle_last;
	vdso_data->wtm_clock_sec = tk->wall_to_monotonic.tv_sec;
	vdso_data->wtm_clock_nsec = tk->wall_to_monotonic.tv_nsec;
	vdso_data->xtime_clock_sec = tk->xtime_sec;
	vdso_data->xtime_clock_nsec = tk->tkr_mono.xtime_nsec;
	vdso_data->xtime_coarse_sec = tk->xtime_sec;
	vdso_data->xtime_coarse_nsec = tk->tkr_mono.xtime_nsec >>
	    tk->tkr_mono.shift;
	vdso_data->hrtimer_res = hrtimer_resolution;
	vdso_write_end(vdso_data);
}

void update_vsyscall_tz(void)
{
	vdso_data->tz_minuteswest = sys_tz.tz_minuteswest;
	vdso_data->tz_dsttime = sys_tz.tz_dsttime;
}
