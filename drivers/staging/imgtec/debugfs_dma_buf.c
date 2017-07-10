/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File           debugfs_dma_buf.c
@Codingstyle    LinuxKernel
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include "debugfs_dma_buf.h"

#if defined(DEBUGFS_DMA_BUF) && defined(CONFIG_DEBUG_FS)

#include <linux/kernel.h>
#include <linux/debugfs.h>

#include "kernel_compatibility.h"

static struct dentry *g_debugfs_dentry;
static struct dma_buf *g_dma_buf;

static ssize_t read_file_dma_buf(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	size_t istart = *ppos / PAGE_SIZE, iend, i = istart;
	ssize_t wb = 0, res = 0;
	struct dma_buf *dma_buf = g_dma_buf;
	int err;

	if (!dma_buf)
		goto err_out;

	/* Inc the ref count for the time we use it in this function. */
	get_dma_buf(dma_buf);

	/* End of buffer? */
	if (*ppos >= dma_buf->size)
		goto err_put;

	/* Calculate the number of pages we need to process based on the
	 * remaining dma buffer size or the available um buffer size. */
	iend = istart + min((size_t)(dma_buf->size - *ppos), count) / PAGE_SIZE;

	res = dma_buf_begin_cpu_access(dma_buf, DMA_FROM_DEVICE);
	if (res)
		goto err_put;

	/* dma_buf_kmap only allows mapping one page, so we have to loop until
	 * the um buffer is full. */
	while (i < iend) {
		loff_t dummy = 0; /* We ignore that */
		void *map = dma_buf_kmap(dma_buf, i);

		if (!map) {
			res = -EFAULT;
			goto err_access;
		}
		/* Read PAGE_SIZE or the remaining buffer size worth of
		 * data. Whichever is smaller. */
		res = simple_read_from_buffer(&user_buf[wb], count - wb,
			&dummy, map,
			min((size_t)PAGE_SIZE,
			    (size_t)(dma_buf->size - *ppos)));
		dma_buf_kunmap(dma_buf, i, map);
		if (res < 0)
			goto err_access;
		wb    += res;
		*ppos += res;
		++i;
	}
	res = wb;

err_access:
	do {
		err = dma_buf_end_cpu_access(dma_buf, DMA_FROM_DEVICE);
	} while (err == -EAGAIN || err == -EINTR);
err_put:
	dma_buf_put(dma_buf);
err_out:
	return res;
}

static const struct file_operations fops_dma_buf = {
	.open =   simple_open,
	.read =	  read_file_dma_buf,
	.llseek = default_llseek,
};

int debugfs_dma_buf_init(const char *name)
{
	int err = 0;

	g_debugfs_dentry = debugfs_create_file(name, S_IRUSR, NULL,
					       NULL, &fops_dma_buf);
	if (IS_ERR(g_debugfs_dentry)) {
		err = PTR_ERR(g_debugfs_dentry);
		g_debugfs_dentry = NULL;
		goto err_out;
	}

err_out:
	return err;
}

void debugfs_dma_buf_deinit(void)
{
	debugfs_remove(g_debugfs_dentry);
}

void debugfs_dma_buf_set(struct dma_buf *dma_buf)
{
	struct dma_buf *old_dma_buf = g_dma_buf;

	if (dma_buf)
		get_dma_buf(dma_buf);

	g_dma_buf = dma_buf;

	if (old_dma_buf)
		dma_buf_put(old_dma_buf);
}

#else /* defined(DEBUGFS_DMA_BUF) && defined(CONFIG_DEBUG_FS) */

int debugfs_dma_buf_init(const char *name)
{
	return 0;
}

void debugfs_dma_buf_deinit(void)
{
}

void debugfs_dma_buf_set(struct dma_buf *dma_buf)
{
}

#endif /* defined(DEBUGFS_DMA_BUF) && defined(CONFIG_DEBUG_FS) */
