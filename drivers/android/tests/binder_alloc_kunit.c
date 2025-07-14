// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for binder allocator code
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/anon_inodes.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sizes.h>

#include "../binder_alloc.h"
#include "../binder_internal.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

#define BINDER_MMAP_SIZE SZ_128K

#define BUFFER_NUM 5
#define BUFFER_MIN_SIZE (PAGE_SIZE / 8)

static int binder_alloc_test_failures;

/**
 * enum buf_end_align_type - Page alignment of a buffer
 * end with regard to the end of the previous buffer.
 *
 * In the pictures below, buf2 refers to the buffer we
 * are aligning. buf1 refers to previous buffer by addr.
 * Symbol [ means the start of a buffer, ] means the end
 * of a buffer, and | means page boundaries.
 */
enum buf_end_align_type {
	/**
	 * @SAME_PAGE_UNALIGNED: The end of this buffer is on
	 * the same page as the end of the previous buffer and
	 * is not page aligned. Examples:
	 * buf1 ][ buf2 ][ ...
	 * buf1 ]|[ buf2 ][ ...
	 */
	SAME_PAGE_UNALIGNED = 0,
	/**
	 * @SAME_PAGE_ALIGNED: When the end of the previous buffer
	 * is not page aligned, the end of this buffer is on the
	 * same page as the end of the previous buffer and is page
	 * aligned. When the previous buffer is page aligned, the
	 * end of this buffer is aligned to the next page boundary.
	 * Examples:
	 * buf1 ][ buf2 ]| ...
	 * buf1 ]|[ buf2 ]| ...
	 */
	SAME_PAGE_ALIGNED,
	/**
	 * @NEXT_PAGE_UNALIGNED: The end of this buffer is on
	 * the page next to the end of the previous buffer and
	 * is not page aligned. Examples:
	 * buf1 ][ buf2 | buf2 ][ ...
	 * buf1 ]|[ buf2 | buf2 ][ ...
	 */
	NEXT_PAGE_UNALIGNED,
	/**
	 * @NEXT_PAGE_ALIGNED: The end of this buffer is on
	 * the page next to the end of the previous buffer and
	 * is page aligned. Examples:
	 * buf1 ][ buf2 | buf2 ]| ...
	 * buf1 ]|[ buf2 | buf2 ]| ...
	 */
	NEXT_PAGE_ALIGNED,
	/**
	 * @NEXT_NEXT_UNALIGNED: The end of this buffer is on
	 * the page that follows the page after the end of the
	 * previous buffer and is not page aligned. Examples:
	 * buf1 ][ buf2 | buf2 | buf2 ][ ...
	 * buf1 ]|[ buf2 | buf2 | buf2 ][ ...
	 */
	NEXT_NEXT_UNALIGNED,
	/**
	 * @LOOP_END: The number of enum values in &buf_end_align_type.
	 * It is used for controlling loop termination.
	 */
	LOOP_END,
};

static void pr_err_size_seq(struct kunit *test, size_t *sizes, int *seq)
{
	int i;

	kunit_err(test, "alloc sizes: ");
	for (i = 0; i < BUFFER_NUM; i++)
		pr_cont("[%zu]", sizes[i]);
	pr_cont("\n");
	kunit_err(test, "free seq: ");
	for (i = 0; i < BUFFER_NUM; i++)
		pr_cont("[%d]", seq[i]);
	pr_cont("\n");
}

static bool check_buffer_pages_allocated(struct kunit *test,
					 struct binder_alloc *alloc,
					 struct binder_buffer *buffer,
					 size_t size)
{
	unsigned long page_addr;
	unsigned long end;
	int page_index;

	end = PAGE_ALIGN(buffer->user_data + size);
	page_addr = buffer->user_data;
	for (; page_addr < end; page_addr += PAGE_SIZE) {
		page_index = (page_addr - alloc->vm_start) / PAGE_SIZE;
		if (!alloc->pages[page_index] ||
		    !list_empty(page_to_lru(alloc->pages[page_index]))) {
			kunit_err(test, "expect alloc but is %s at page index %d\n",
				  alloc->pages[page_index] ?
				  "lru" : "free", page_index);
			return false;
		}
	}
	return true;
}

static void binder_alloc_test_alloc_buf(struct kunit *test,
					struct binder_alloc *alloc,
					struct binder_buffer *buffers[],
					size_t *sizes, int *seq)
{
	int i;

	for (i = 0; i < BUFFER_NUM; i++) {
		buffers[i] = binder_alloc_new_buf(alloc, sizes[i], 0, 0, 0);
		if (IS_ERR(buffers[i]) ||
		    !check_buffer_pages_allocated(test, alloc, buffers[i], sizes[i])) {
			pr_err_size_seq(test, sizes, seq);
			binder_alloc_test_failures++;
		}
	}
}

static void binder_alloc_test_free_buf(struct kunit *test,
				       struct binder_alloc *alloc,
				       struct binder_buffer *buffers[],
				       size_t *sizes, int *seq, size_t end)
{
	int i;

	for (i = 0; i < BUFFER_NUM; i++)
		binder_alloc_free_buf(alloc, buffers[seq[i]]);

	for (i = 0; i <= (end - 1) / PAGE_SIZE; i++) {
		if (list_empty(page_to_lru(alloc->pages[i]))) {
			pr_err_size_seq(test, sizes, seq);
			kunit_err(test, "expect lru but is %s at page index %d\n",
				  alloc->pages[i] ? "alloc" : "free", i);
			binder_alloc_test_failures++;
		}
	}
}

static void binder_alloc_test_free_page(struct kunit *test,
					struct binder_alloc *alloc)
{
	unsigned long count;
	int i;

	while ((count = list_lru_count(alloc->freelist))) {
		list_lru_walk(alloc->freelist, binder_alloc_free_page,
			      NULL, count);
	}

	for (i = 0; i < (alloc->buffer_size / PAGE_SIZE); i++) {
		if (alloc->pages[i]) {
			kunit_err(test, "expect free but is %s at page index %d\n",
				  list_empty(page_to_lru(alloc->pages[i])) ?
				  "alloc" : "lru", i);
			binder_alloc_test_failures++;
		}
	}
}

static void binder_alloc_test_alloc_free(struct kunit *test,
					 struct binder_alloc *alloc,
					 size_t *sizes, int *seq, size_t end)
{
	struct binder_buffer *buffers[BUFFER_NUM];

	binder_alloc_test_alloc_buf(test, alloc, buffers, sizes, seq);
	binder_alloc_test_free_buf(test, alloc, buffers, sizes, seq, end);

	/* Allocate from lru. */
	binder_alloc_test_alloc_buf(test, alloc, buffers, sizes, seq);
	if (list_lru_count(alloc->freelist))
		kunit_err(test, "lru list should be empty but is not\n");

	binder_alloc_test_free_buf(test, alloc, buffers, sizes, seq, end);
	binder_alloc_test_free_page(test, alloc);
}

static bool is_dup(int *seq, int index, int val)
{
	int i;

	for (i = 0; i < index; i++) {
		if (seq[i] == val)
			return true;
	}
	return false;
}

/* Generate BUFFER_NUM factorial free orders. */
static void permute_frees(struct kunit *test, struct binder_alloc *alloc,
			  size_t *sizes, int *seq, int index, size_t end)
{
	int i;

	if (index == BUFFER_NUM) {
		binder_alloc_test_alloc_free(test, alloc, sizes, seq, end);
		return;
	}
	for (i = 0; i < BUFFER_NUM; i++) {
		if (is_dup(seq, index, i))
			continue;
		seq[index] = i;
		permute_frees(test, alloc, sizes, seq, index + 1, end);
	}
}

static void gen_buf_sizes(struct kunit *test, struct binder_alloc *alloc,
			  size_t *end_offset)
{
	size_t last_offset, offset = 0;
	size_t front_sizes[BUFFER_NUM];
	size_t back_sizes[BUFFER_NUM];
	int seq[BUFFER_NUM] = {0};
	int i;

	for (i = 0; i < BUFFER_NUM; i++) {
		last_offset = offset;
		offset = end_offset[i];
		front_sizes[i] = offset - last_offset;
		back_sizes[BUFFER_NUM - i - 1] = front_sizes[i];
	}
	/*
	 * Buffers share the first or last few pages.
	 * Only BUFFER_NUM - 1 buffer sizes are adjustable since
	 * we need one giant buffer before getting to the last page.
	 */
	back_sizes[0] += alloc->buffer_size - end_offset[BUFFER_NUM - 1];
	permute_frees(test, alloc, front_sizes, seq, 0,
		      end_offset[BUFFER_NUM - 1]);
	permute_frees(test, alloc, back_sizes, seq, 0, alloc->buffer_size);
}

static void gen_buf_offsets(struct kunit *test, struct binder_alloc *alloc,
			    size_t *end_offset, int index)
{
	size_t end, prev;
	int align;

	if (index == BUFFER_NUM) {
		gen_buf_sizes(test, alloc, end_offset);
		return;
	}
	prev = index == 0 ? 0 : end_offset[index - 1];
	end = prev;

	BUILD_BUG_ON(BUFFER_MIN_SIZE * BUFFER_NUM >= PAGE_SIZE);

	for (align = SAME_PAGE_UNALIGNED; align < LOOP_END; align++) {
		if (align % 2)
			end = ALIGN(end, PAGE_SIZE);
		else
			end += BUFFER_MIN_SIZE;
		end_offset[index] = end;
		gen_buf_offsets(test, alloc, end_offset, index + 1);
	}
}

struct binder_alloc_test {
	struct binder_alloc alloc;
	struct list_lru binder_test_freelist;
	struct file *filp;
	unsigned long mmap_uaddr;
};

static void binder_alloc_test_init_freelist(struct kunit *test)
{
	struct binder_alloc_test *priv = test->priv;

	KUNIT_EXPECT_PTR_EQ(test, priv->alloc.freelist,
			    &priv->binder_test_freelist);
}

static void binder_alloc_test_mmap(struct kunit *test)
{
	struct binder_alloc_test *priv = test->priv;
	struct binder_alloc *alloc = &priv->alloc;
	struct binder_buffer *buf;
	struct rb_node *n;

	KUNIT_EXPECT_EQ(test, alloc->mapped, true);
	KUNIT_EXPECT_EQ(test, alloc->buffer_size, BINDER_MMAP_SIZE);

	n = rb_first(&alloc->allocated_buffers);
	KUNIT_EXPECT_PTR_EQ(test, n, NULL);

	n = rb_first(&alloc->free_buffers);
	buf = rb_entry(n, struct binder_buffer, rb_node);
	KUNIT_EXPECT_EQ(test, binder_alloc_buffer_size(alloc, buf),
			BINDER_MMAP_SIZE);
	KUNIT_EXPECT_TRUE(test, list_is_last(&buf->entry, &alloc->buffers));
}

/**
 * binder_alloc_exhaustive_test() - Exhaustively test alloc and free of buffer pages.
 * @test: The test context object.
 *
 * Allocate BUFFER_NUM buffers to cover all page alignment cases,
 * then free them in all orders possible. Check that pages are
 * correctly allocated, put onto lru when buffers are freed, and
 * are freed when binder_alloc_free_page() is called.
 */
static void binder_alloc_exhaustive_test(struct kunit *test)
{
	struct binder_alloc_test *priv = test->priv;
	size_t end_offset[BUFFER_NUM];

	gen_buf_offsets(test, &priv->alloc, end_offset, 0);

	KUNIT_EXPECT_EQ(test, binder_alloc_test_failures, 0);
}

/* ===== End test cases ===== */

static void binder_alloc_test_vma_close(struct vm_area_struct *vma)
{
	struct binder_alloc *alloc = vma->vm_private_data;

	binder_alloc_vma_close(alloc);
}

static const struct vm_operations_struct binder_alloc_test_vm_ops = {
	.close = binder_alloc_test_vma_close,
	.fault = binder_vm_fault,
};

static int binder_alloc_test_mmap_handler(struct file *filp,
					  struct vm_area_struct *vma)
{
	struct binder_alloc *alloc = filp->private_data;

	vm_flags_mod(vma, VM_DONTCOPY | VM_MIXEDMAP, VM_MAYWRITE);

	vma->vm_ops = &binder_alloc_test_vm_ops;
	vma->vm_private_data = alloc;

	return binder_alloc_mmap_handler(alloc, vma);
}

static const struct file_operations binder_alloc_test_fops = {
	.mmap = binder_alloc_test_mmap_handler,
};

static int binder_alloc_test_init(struct kunit *test)
{
	struct binder_alloc_test *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	test->priv = priv;

	ret = list_lru_init(&priv->binder_test_freelist);
	if (ret) {
		kunit_err(test, "Failed to initialize test freelist\n");
		return ret;
	}

	/* __binder_alloc_init requires mm to be attached */
	ret = kunit_attach_mm();
	if (ret) {
		kunit_err(test, "Failed to attach mm\n");
		return ret;
	}
	__binder_alloc_init(&priv->alloc, &priv->binder_test_freelist);

	priv->filp = anon_inode_getfile("binder_alloc_kunit",
					&binder_alloc_test_fops, &priv->alloc,
					O_RDWR | O_CLOEXEC);
	if (IS_ERR_OR_NULL(priv->filp)) {
		kunit_err(test, "Failed to open binder alloc test driver file\n");
		return priv->filp ? PTR_ERR(priv->filp) : -ENOMEM;
	}

	priv->mmap_uaddr = kunit_vm_mmap(test, priv->filp, 0, BINDER_MMAP_SIZE,
					 PROT_READ, MAP_PRIVATE | MAP_NORESERVE,
					 0);
	if (!priv->mmap_uaddr) {
		kunit_err(test, "Could not map the test's transaction memory\n");
		return -ENOMEM;
	}

	return 0;
}

static void binder_alloc_test_exit(struct kunit *test)
{
	struct binder_alloc_test *priv = test->priv;

	/* Close the backing file to make sure binder_alloc_vma_close runs */
	if (!IS_ERR_OR_NULL(priv->filp))
		fput(priv->filp);

	if (priv->alloc.mm)
		binder_alloc_deferred_release(&priv->alloc);

	/* Make sure freelist is empty */
	KUNIT_EXPECT_EQ(test, list_lru_count(&priv->binder_test_freelist), 0);
	list_lru_destroy(&priv->binder_test_freelist);
}

static struct kunit_case binder_alloc_test_cases[] = {
	KUNIT_CASE(binder_alloc_test_init_freelist),
	KUNIT_CASE(binder_alloc_test_mmap),
	KUNIT_CASE(binder_alloc_exhaustive_test),
	{}
};

static struct kunit_suite binder_alloc_test_suite = {
	.name = "binder_alloc",
	.test_cases = binder_alloc_test_cases,
	.init = binder_alloc_test_init,
	.exit = binder_alloc_test_exit,
};

kunit_test_suite(binder_alloc_test_suite);

MODULE_AUTHOR("Tiffany Yang <ynaffit@google.com>");
MODULE_DESCRIPTION("Binder Alloc KUnit tests");
MODULE_LICENSE("GPL");
