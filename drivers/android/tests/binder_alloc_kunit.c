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
