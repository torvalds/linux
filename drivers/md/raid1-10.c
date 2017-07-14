/* generally called after bio_reset() for reseting bvec */
static void md_bio_reset_resync_pages(struct bio *bio, struct resync_pages *rp,
			       int size)
{
	int idx = 0;

	/* initialize bvec table again */
	do {
		struct page *page = resync_fetch_page(rp, idx);
		int len = min_t(int, size, PAGE_SIZE);

		/*
		 * won't fail because the vec table is big
		 * enough to hold all these pages
		 */
		bio_add_page(bio, page, len, 0);
		size -= len;
	} while (idx++ < RESYNC_PAGES && size > 0);
}
