#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>

void show_mem(void)
{
#ifndef CONFIG_NEED_MULTIPLE_NODES  /* XXX(hch): later.. */
	int pfn, total = 0, reserved = 0;
	int shared = 0, cached = 0;
	int highmem = 0;
	struct page *page;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	pfn = max_mapnr;
	while (pfn-- > 0) {
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		total++;
		if (PageHighMem(page))
			highmem++;
		if (PageReserved(page))
			reserved++;
		else if (PageSwapCache(page))
			cached++;
		else if (page_count(page))
			shared += page_count(page) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d pages of HIGHMEM\n", highmem);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
#endif
}
