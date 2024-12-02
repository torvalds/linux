// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pagemap.h>
#include <linux/xarray.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <asm/mte.h>

static DEFINE_XARRAY(mte_pages);

void *mte_allocate_tag_storage(void)
{
	/* tags granule is 16 bytes, 2 tags stored per byte */
	return kmalloc(MTE_PAGE_TAG_STORAGE, GFP_KERNEL);
}

void mte_free_tag_storage(char *storage)
{
	kfree(storage);
}

int mte_save_tags(struct page *page)
{
	void *tag_storage, *ret;

	if (!page_mte_tagged(page))
		return 0;

	tag_storage = mte_allocate_tag_storage();
	if (!tag_storage)
		return -ENOMEM;

	mte_save_page_tags(page_address(page), tag_storage);

	/* page_private contains the swap entry.val set in do_swap_page */
	ret = xa_store(&mte_pages, page_private(page), tag_storage, GFP_KERNEL);
	if (WARN(xa_is_err(ret), "Failed to store MTE tags")) {
		mte_free_tag_storage(tag_storage);
		return xa_err(ret);
	} else if (ret) {
		/* Entry is being replaced, free the old entry */
		mte_free_tag_storage(ret);
	}

	return 0;
}

void mte_restore_tags(swp_entry_t entry, struct page *page)
{
	void *tags = xa_load(&mte_pages, entry.val);

	if (!tags)
		return;

	/*
	 * Test PG_mte_tagged in case the tags were restored before
	 * (e.g. CoW pages).
	 */
	if (!test_and_set_bit(PG_mte_tagged, &page->flags))
		mte_restore_page_tags(page_address(page), tags);
}

void mte_invalidate_tags(int type, pgoff_t offset)
{
	swp_entry_t entry = swp_entry(type, offset);
	void *tags = xa_erase(&mte_pages, entry.val);

	mte_free_tag_storage(tags);
}

void mte_invalidate_tags_area(int type)
{
	swp_entry_t entry = swp_entry(type, 0);
	swp_entry_t last_entry = swp_entry(type + 1, 0);
	void *tags;

	XA_STATE(xa_state, &mte_pages, entry.val);

	xa_lock(&mte_pages);
	xas_for_each(&xa_state, tags, last_entry.val - 1) {
		__xa_erase(&mte_pages, xa_state.xa_index);
		mte_free_tag_storage(tags);
	}
	xa_unlock(&mte_pages);
}
