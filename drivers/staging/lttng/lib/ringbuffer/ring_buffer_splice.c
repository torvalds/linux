/*
 * ring_buffer_splice.c
 *
 * Copyright (C) 2002-2005 - Tom Zanussi <zanussi@us.ibm.com>, IBM Corp
 * Copyright (C) 1999-2005 - Karim Yaghmour <karim@opersys.com>
 * Copyright (C) 2008-2012 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Re-using code from kernel/relay.c, which is why it is licensed under
 * the GPLv2.
 */

#include <linux/module.h>
#include <linux/fs.h>

#include "../../wrapper/splice.h"
#include "../../wrapper/ringbuffer/backend.h"
#include "../../wrapper/ringbuffer/frontend.h"
#include "../../wrapper/ringbuffer/vfs.h"

#if 0
#define printk_dbg(fmt, args...) printk(fmt, args)
#else
#define printk_dbg(fmt, args...)
#endif

loff_t vfs_lib_ring_buffer_no_llseek(struct file *file, loff_t offset,
		int origin)
{
	return -ESPIPE;
}
EXPORT_SYMBOL_GPL(vfs_lib_ring_buffer_no_llseek);

/*
 * Release pages from the buffer so splice pipe_to_file can move them.
 * Called after the pipe has been populated with buffer pages.
 */
static void lib_ring_buffer_pipe_buf_release(struct pipe_inode_info *pipe,
					     struct pipe_buffer *pbuf)
{
	__free_page(pbuf->page);
}

static const struct pipe_buf_operations ring_buffer_pipe_buf_ops = {
	.can_merge = 0,
	.map = generic_pipe_buf_map,
	.unmap = generic_pipe_buf_unmap,
	.confirm = generic_pipe_buf_confirm,
	.release = lib_ring_buffer_pipe_buf_release,
	.steal = generic_pipe_buf_steal,
	.get = generic_pipe_buf_get,
};

/*
 * Page release operation after splice pipe_to_file ends.
 */
static void lib_ring_buffer_page_release(struct splice_pipe_desc *spd,
					 unsigned int i)
{
	__free_page(spd->pages[i]);
}

/*
 *	subbuf_splice_actor - splice up to one subbuf's worth of data
 */
static int subbuf_splice_actor(struct file *in,
			       loff_t *ppos,
			       struct pipe_inode_info *pipe,
			       size_t len,
			       unsigned int flags,
			       struct lib_ring_buffer *buf)
{
	struct channel *chan = buf->backend.chan;
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	unsigned int poff, subbuf_pages, nr_pages;
	struct page *pages[PIPE_DEF_BUFFERS];
	struct partial_page partial[PIPE_DEF_BUFFERS];
	struct splice_pipe_desc spd = {
		.pages = pages,
		.nr_pages = 0,
		.partial = partial,
		.flags = flags,
		.ops = &ring_buffer_pipe_buf_ops,
		.spd_release = lib_ring_buffer_page_release,
	};
	unsigned long consumed_old, roffset;
	unsigned long bytes_avail;

	/*
	 * Check that a GET_SUBBUF ioctl has been done before.
	 */
	WARN_ON(atomic_long_read(&buf->active_readers) != 1);
	consumed_old = lib_ring_buffer_get_consumed(config, buf);
	consumed_old += *ppos;

	/*
	 * Adjust read len, if longer than what is available.
	 * Max read size is 1 subbuffer due to get_subbuf/put_subbuf for
	 * protection.
	 */
	bytes_avail = chan->backend.subbuf_size;
	WARN_ON(bytes_avail > chan->backend.buf_size);
	len = min_t(size_t, len, bytes_avail);
	subbuf_pages = bytes_avail >> PAGE_SHIFT;
	nr_pages = min_t(unsigned int, subbuf_pages, PIPE_DEF_BUFFERS);
	roffset = consumed_old & PAGE_MASK;
	poff = consumed_old & ~PAGE_MASK;
	printk_dbg(KERN_DEBUG "SPLICE actor len %zu pos %zd write_pos %ld\n",
		   len, (ssize_t)*ppos, lib_ring_buffer_get_offset(config, buf));

	for (; spd.nr_pages < nr_pages; spd.nr_pages++) {
		unsigned int this_len;
		struct page **page, *new_page;
		void **virt;

		if (!len)
			break;
		printk_dbg(KERN_DEBUG "SPLICE actor loop len %zu roffset %ld\n",
			   len, roffset);

		/*
		 * We have to replace the page we are moving into the splice
		 * pipe.
		 */
		new_page = alloc_pages_node(cpu_to_node(max(buf->backend.cpu,
							    0)),
					    GFP_KERNEL | __GFP_ZERO, 0);
		if (!new_page)
			break;

		this_len = PAGE_SIZE - poff;
		page = lib_ring_buffer_read_get_page(&buf->backend, roffset, &virt);
		spd.pages[spd.nr_pages] = *page;
		*page = new_page;
		*virt = page_address(new_page);
		spd.partial[spd.nr_pages].offset = poff;
		spd.partial[spd.nr_pages].len = this_len;

		poff = 0;
		roffset += PAGE_SIZE;
		len -= this_len;
	}

	if (!spd.nr_pages)
		return 0;

	return wrapper_splice_to_pipe(pipe, &spd);
}

ssize_t lib_ring_buffer_splice_read(struct file *in, loff_t *ppos,
				    struct pipe_inode_info *pipe, size_t len,
				    unsigned int flags,
				    struct lib_ring_buffer *buf)
{
	struct channel *chan = buf->backend.chan;
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	ssize_t spliced;
	int ret;

	if (config->output != RING_BUFFER_SPLICE)
		return -EINVAL;

	/*
	 * We require ppos and length to be page-aligned for performance reasons
	 * (no page copy). Size is known using the ioctl
	 * RING_BUFFER_GET_PADDED_SUBBUF_SIZE, which is page-size padded.
	 * We fail when the ppos or len passed is not page-sized, because splice
	 * is not allowed to copy more than the length passed as parameter (so
	 * the ABI does not let us silently copy more than requested to include
	 * padding).
	 */
	if (*ppos != PAGE_ALIGN(*ppos) || len != PAGE_ALIGN(len))
		return -EINVAL;

	ret = 0;
	spliced = 0;

	printk_dbg(KERN_DEBUG "SPLICE read len %zu pos %zd\n", len,
		   (ssize_t)*ppos);
	while (len && !spliced) {
		ret = subbuf_splice_actor(in, ppos, pipe, len, flags, buf);
		printk_dbg(KERN_DEBUG "SPLICE read loop ret %d\n", ret);
		if (ret < 0)
			break;
		else if (!ret) {
			if (flags & SPLICE_F_NONBLOCK)
				ret = -EAGAIN;
			break;
		}

		*ppos += ret;
		if (ret > len)
			len = 0;
		else
			len -= ret;
		spliced += ret;
	}

	if (spliced)
		return spliced;

	return ret;
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_splice_read);

ssize_t vfs_lib_ring_buffer_splice_read(struct file *in, loff_t *ppos,
				    struct pipe_inode_info *pipe, size_t len,
				    unsigned int flags)
{
	struct lib_ring_buffer *buf = in->private_data;

	return lib_ring_buffer_splice_read(in, ppos, pipe, len, flags, buf);
}
EXPORT_SYMBOL_GPL(vfs_lib_ring_buffer_splice_read);
