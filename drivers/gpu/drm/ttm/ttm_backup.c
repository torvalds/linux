// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/ttm/ttm_backup.h>

#include <linux/export.h>
#include <linux/page-flags.h>
#include <linux/swap.h>

/*
 * Need to map shmem indices to handle since a handle value
 * of 0 means error, following the swp_entry_t convention.
 */
static unsigned long ttm_backup_shmem_idx_to_handle(pgoff_t idx)
{
	return (unsigned long)idx + 1;
}

static pgoff_t ttm_backup_handle_to_shmem_idx(pgoff_t handle)
{
	return handle - 1;
}

/**
 * ttm_backup_drop() - release memory associated with a handle
 * @backup: The struct backup pointer used to obtain the handle
 * @handle: The handle obtained from the @backup_page function.
 */
void ttm_backup_drop(struct file *backup, pgoff_t handle)
{
	loff_t start = ttm_backup_handle_to_shmem_idx(handle);

	start <<= PAGE_SHIFT;
	shmem_truncate_range(file_inode(backup), start,
			     start + PAGE_SIZE - 1);
}

/**
 * ttm_backup_copy_page() - Copy the contents of a previously backed
 * up page
 * @backup: The struct backup pointer used to back up the page.
 * @dst: The struct page to copy into.
 * @handle: The handle returned when the page was backed up.
 * @intr: Try to perform waits interruptible or at least killable.
 *
 * Return: 0 on success, Negative error code on failure, notably
 * -EINTR if @intr was set to true and a signal is pending.
 */
int ttm_backup_copy_page(struct file *backup, struct page *dst,
			 pgoff_t handle, bool intr)
{
	struct address_space *mapping = backup->f_mapping;
	struct folio *from_folio;
	pgoff_t idx = ttm_backup_handle_to_shmem_idx(handle);

	from_folio = shmem_read_folio(mapping, idx);
	if (IS_ERR(from_folio))
		return PTR_ERR(from_folio);

	copy_highpage(dst, folio_file_page(from_folio, idx));
	folio_put(from_folio);

	return 0;
}

/**
 * ttm_backup_backup_page() - Backup a page
 * @backup: The struct backup pointer to use.
 * @page: The page to back up.
 * @writeback: Whether to perform immediate writeback of the page.
 * This may have performance implications.
 * @idx: A unique integer for each page and each struct backup.
 * This allows the backup implementation to avoid managing
 * its address space separately.
 * @page_gfp: The gfp value used when the page was allocated.
 * This is used for accounting purposes.
 * @alloc_gfp: The gfp to be used when allocating memory.
 *
 * Context: If called from reclaim context, the caller needs to
 * assert that the shrinker gfp has __GFP_FS set, to avoid
 * deadlocking on lock_page(). If @writeback is set to true and
 * called from reclaim context, the caller also needs to assert
 * that the shrinker gfp has __GFP_IO set, since without it,
 * we're not allowed to start backup IO.
 *
 * Return: A handle on success. Negative error code on failure.
 *
 * Note: This function could be extended to back up a folio and
 * implementations would then split the folio internally if needed.
 * Drawback is that the caller would then have to keep track of
 * the folio size- and usage.
 */
s64
ttm_backup_backup_page(struct file *backup, struct page *page,
		       bool writeback, pgoff_t idx, gfp_t page_gfp,
		       gfp_t alloc_gfp)
{
	struct address_space *mapping = backup->f_mapping;
	unsigned long handle = 0;
	struct folio *to_folio;
	int ret;

	to_folio = shmem_read_folio_gfp(mapping, idx, alloc_gfp);
	if (IS_ERR(to_folio))
		return PTR_ERR(to_folio);

	folio_mark_accessed(to_folio);
	folio_lock(to_folio);
	folio_mark_dirty(to_folio);
	copy_highpage(folio_file_page(to_folio, idx), page);
	handle = ttm_backup_shmem_idx_to_handle(idx);

	if (writeback && !folio_mapped(to_folio) &&
	    folio_clear_dirty_for_io(to_folio)) {
		folio_set_reclaim(to_folio);
		ret = shmem_writeout(to_folio, NULL, NULL);
		if (!folio_test_writeback(to_folio))
			folio_clear_reclaim(to_folio);
		/*
		 * If writeout succeeds, it unlocks the folio.	errors
		 * are otherwise dropped, since writeout is only best
		 * effort here.
		 */
		if (ret)
			folio_unlock(to_folio);
	} else {
		folio_unlock(to_folio);
	}

	folio_put(to_folio);

	return handle;
}

/**
 * ttm_backup_fini() - Free the struct backup resources after last use.
 * @backup: Pointer to the struct backup whose resources to free.
 *
 * After a call to this function, it's illegal to use the @backup pointer.
 */
void ttm_backup_fini(struct file *backup)
{
	fput(backup);
}

/**
 * ttm_backup_bytes_avail() - Report the approximate number of bytes of backup space
 * left for backup.
 *
 * This function is intended also for driver use to indicate whether a
 * backup attempt is meaningful.
 *
 * Return: An approximate size of backup space available.
 */
u64 ttm_backup_bytes_avail(void)
{
	/*
	 * The idea behind backing up to shmem is that shmem objects may
	 * eventually be swapped out. So no point swapping out if there
	 * is no or low swap-space available. But the accuracy of this
	 * number also depends on shmem actually swapping out backed-up
	 * shmem objects without too much buffering.
	 */
	return (u64)get_nr_swap_pages() << PAGE_SHIFT;
}
EXPORT_SYMBOL_GPL(ttm_backup_bytes_avail);

/**
 * ttm_backup_shmem_create() - Create a shmem-based struct backup.
 * @size: The maximum size (in bytes) to back up.
 *
 * Create a backup utilizing shmem objects.
 *
 * Return: A pointer to a struct file on success,
 * an error pointer on error.
 */
struct file *ttm_backup_shmem_create(loff_t size)
{
	return shmem_file_setup("ttm shmem backup", size, 0);
}
