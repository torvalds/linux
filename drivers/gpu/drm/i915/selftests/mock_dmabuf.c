/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "mock_dmabuf.h"

static struct sg_table *mock_map_dma_buf(struct dma_buf_attachment *attachment,
					 enum dma_data_direction dir)
{
	struct mock_dmabuf *mock = to_mock(attachment->dmabuf);
	struct sg_table *st;
	struct scatterlist *sg;
	int i, err;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return ERR_PTR(-ENOMEM);

	err = sg_alloc_table(st, mock->npages, GFP_KERNEL);
	if (err)
		goto err_free;

	sg = st->sgl;
	for (i = 0; i < mock->npages; i++) {
		sg_set_page(sg, mock->pages[i], PAGE_SIZE, 0);
		sg = sg_next(sg);
	}

	if (!dma_map_sg(attachment->dev, st->sgl, st->nents, dir)) {
		err = -ENOMEM;
		goto err_st;
	}

	return st;

err_st:
	sg_free_table(st);
err_free:
	kfree(st);
	return ERR_PTR(err);
}

static void mock_unmap_dma_buf(struct dma_buf_attachment *attachment,
			       struct sg_table *st,
			       enum dma_data_direction dir)
{
	dma_unmap_sg(attachment->dev, st->sgl, st->nents, dir);
	sg_free_table(st);
	kfree(st);
}

static void mock_dmabuf_release(struct dma_buf *dma_buf)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);
	int i;

	for (i = 0; i < mock->npages; i++)
		put_page(mock->pages[i]);

	kfree(mock);
}

static void *mock_dmabuf_vmap(struct dma_buf *dma_buf)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);

	return vm_map_ram(mock->pages, mock->npages, 0, PAGE_KERNEL);
}

static void mock_dmabuf_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);

	vm_unmap_ram(vaddr, mock->npages);
}

static void *mock_dmabuf_kmap_atomic(struct dma_buf *dma_buf, unsigned long page_num)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);

	return kmap_atomic(mock->pages[page_num]);
}

static void mock_dmabuf_kunmap_atomic(struct dma_buf *dma_buf, unsigned long page_num, void *addr)
{
	kunmap_atomic(addr);
}

static void *mock_dmabuf_kmap(struct dma_buf *dma_buf, unsigned long page_num)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);

	return kmap(mock->pages[page_num]);
}

static void mock_dmabuf_kunmap(struct dma_buf *dma_buf, unsigned long page_num, void *addr)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);

	return kunmap(mock->pages[page_num]);
}

static int mock_dmabuf_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	return -ENODEV;
}

static const struct dma_buf_ops mock_dmabuf_ops =  {
	.map_dma_buf = mock_map_dma_buf,
	.unmap_dma_buf = mock_unmap_dma_buf,
	.release = mock_dmabuf_release,
	.kmap = mock_dmabuf_kmap,
	.kmap_atomic = mock_dmabuf_kmap_atomic,
	.kunmap = mock_dmabuf_kunmap,
	.kunmap_atomic = mock_dmabuf_kunmap_atomic,
	.mmap = mock_dmabuf_mmap,
	.vmap = mock_dmabuf_vmap,
	.vunmap = mock_dmabuf_vunmap,
};

static struct dma_buf *mock_dmabuf(int npages)
{
	struct mock_dmabuf *mock;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int i;

	mock = kmalloc(sizeof(*mock) + npages * sizeof(struct page *),
		       GFP_KERNEL);
	if (!mock)
		return ERR_PTR(-ENOMEM);

	mock->npages = npages;
	for (i = 0; i < npages; i++) {
		mock->pages[i] = alloc_page(GFP_KERNEL);
		if (!mock->pages[i])
			goto err;
	}

	exp_info.ops = &mock_dmabuf_ops;
	exp_info.size = npages * PAGE_SIZE;
	exp_info.flags = O_CLOEXEC;
	exp_info.priv = mock;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf))
		goto err;

	return dmabuf;

err:
	while (i--)
		put_page(mock->pages[i]);
	kfree(mock);
	return ERR_PTR(-ENOMEM);
}
