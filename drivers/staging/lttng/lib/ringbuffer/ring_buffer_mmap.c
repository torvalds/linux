/*
 * ring_buffer_mmap.c
 *
 * Copyright (C) 2002-2005 - Tom Zanussi <zanussi@us.ibm.com>, IBM Corp
 * Copyright (C) 1999-2005 - Karim Yaghmour <karim@opersys.com>
 * Copyright (C) 2008-2012 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; only version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Re-using code from kernel/relay.c, hence the GPLv2 license for this
 * file.
 */

#include <linux/module.h>
#include <linux/mm.h>

#include "../../wrapper/ringbuffer/backend.h"
#include "../../wrapper/ringbuffer/frontend.h"
#include "../../wrapper/ringbuffer/vfs.h"

/*
 * fault() vm_op implementation for ring buffer file mapping.
 */
static int lib_ring_buffer_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct lib_ring_buffer *buf = vma->vm_private_data;
	struct channel *chan = buf->backend.chan;
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	pgoff_t pgoff = vmf->pgoff;
	struct page **page;
	void **virt;
	unsigned long offset, sb_bindex;

	/*
	 * Verify that faults are only done on the range of pages owned by the
	 * reader.
	 */
	offset = pgoff << PAGE_SHIFT;
	sb_bindex = subbuffer_id_get_index(config, buf->backend.buf_rsb.id);
	if (!(offset >= buf->backend.array[sb_bindex]->mmap_offset
	      && offset < buf->backend.array[sb_bindex]->mmap_offset +
			  buf->backend.chan->backend.subbuf_size))
		return VM_FAULT_SIGBUS;
	/*
	 * ring_buffer_read_get_page() gets the page in the current reader's
	 * pages.
	 */
	page = lib_ring_buffer_read_get_page(&buf->backend, offset, &virt);
	if (!*page)
		return VM_FAULT_SIGBUS;
	get_page(*page);
	vmf->page = *page;

	return 0;
}

/*
 * vm_ops for ring buffer file mappings.
 */
static const struct vm_operations_struct lib_ring_buffer_mmap_ops = {
	.fault = lib_ring_buffer_fault,
};

/**
 *	lib_ring_buffer_mmap_buf: - mmap channel buffer to process address space
 *	@buf: ring buffer to map
 *	@vma: vm_area_struct describing memory to be mapped
 *
 *	Returns 0 if ok, negative on error
 *
 *	Caller should already have grabbed mmap_sem.
 */
static int lib_ring_buffer_mmap_buf(struct lib_ring_buffer *buf,
				    struct vm_area_struct *vma)
{
	unsigned long length = vma->vm_end - vma->vm_start;
	struct channel *chan = buf->backend.chan;
	const struct lib_ring_buffer_config *config = &chan->backend.config;
	unsigned long mmap_buf_len;

	if (config->output != RING_BUFFER_MMAP)
		return -EINVAL;

	mmap_buf_len = chan->backend.buf_size;
	if (chan->backend.extra_reader_sb)
		mmap_buf_len += chan->backend.subbuf_size;

	if (length != mmap_buf_len)
		return -EINVAL;

	vma->vm_ops = &lib_ring_buffer_mmap_ops;
	vma->vm_flags |= VM_DONTEXPAND;
	vma->vm_private_data = buf;

	return 0;
}

int lib_ring_buffer_mmap(struct file *filp, struct vm_area_struct *vma,
		struct lib_ring_buffer *buf)
{
	return lib_ring_buffer_mmap_buf(buf, vma);
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_mmap);

/**
 *	vfs_lib_ring_buffer_mmap - mmap file op
 *	@filp: the file
 *	@vma: the vma describing what to map
 *
 *	Calls upon lib_ring_buffer_mmap_buf() to map the file into user space.
 */
int vfs_lib_ring_buffer_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct lib_ring_buffer *buf = filp->private_data;
	return lib_ring_buffer_mmap(filp, vma, buf);
}
EXPORT_SYMBOL_GPL(vfs_lib_ring_buffer_mmap);
