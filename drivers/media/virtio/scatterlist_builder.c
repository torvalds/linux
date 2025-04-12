// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0+

/*
 * Scatterlist builder helpers for virtio-media.
 *
 * Copyright (c) 2024-2025 Google LLC.
 */

#include <linux/moduleparam.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>
#include <media/videobuf2-memops.h>

#include "protocol.h"
#include "scatterlist_builder.h"
#include "session.h"

/*
 * If set to ``true``, then the driver will always copy the data passed to the
 * host into the shadow buffer (instead of trying to map the source memory into
 * the SG table directly when possible).
 */
static bool always_use_shadow_buffer;
module_param(always_use_shadow_buffer, bool, 0660);

/* Convert a V4L2 IOCTL into the IOCTL code we can give to the host */
#define VIRTIO_MEDIA_IOCTL_CODE(IOCTL) ((IOCTL >> _IOC_NRSHIFT) & _IOC_NRMASK)

/**
 * scatterlist_builder_add_descriptor() - Add a descriptor to the chain.
 * @builder: builder to use.
 * @desc_index: index of the descriptor to add.
 *
 * Returns ``-ENOSPC`` if ``sgs`` is already full.
 */
int scatterlist_builder_add_descriptor(struct scatterlist_builder *builder,
				       size_t desc_index)
{
	if (builder->cur_sg >= builder->num_sgs)
		return -ENOSPC;
	builder->sgs[builder->cur_sg++] = &builder->descs[desc_index];

	return 0;
}

/**
 * scatterlist_builder_add_data() - Append arbitrary data to the descriptor chain.
 * @builder: builder to use.
 * @data: pointer to the data to add to the descriptor chain.
 * @len: length of the data to add.
 *
 * @data will either be directly referenced, or copied into the shadow buffer
 * to be referenced from there.
 */
int scatterlist_builder_add_data(struct scatterlist_builder *builder,
				 void *data, size_t len)
{
	const size_t cur_desc = builder->cur_desc;

	if (len == 0)
		return 0;

	if (builder->cur_desc >= builder->num_descs)
		return -ENOSPC;

	if (!always_use_shadow_buffer && virt_addr_valid(data + len)) {
		/*
		 * If "data" is in the 1:1 physical memory mapping then we can
		 * use a single SG entry and avoid copying.
		 */
		struct page *page = virt_to_page(data);
		size_t offset = (((size_t)data) & ~PAGE_MASK);
		struct scatterlist *next_desc =
			&builder->descs[builder->cur_desc];

		memset(next_desc, 0, sizeof(*next_desc));
		sg_set_page(next_desc, page, len, offset);
		builder->cur_desc++;
	} else if (!always_use_shadow_buffer && is_vmalloc_addr(data)) {
		int prev_pfn = -2;

		/*
		 * If "data" has been vmalloc'ed, we need at most one entry per
		 * memory page but can avoid copying.
		 */
		while (len > 0) {
			struct page *page = vmalloc_to_page(data);
			int cur_pfn = page_to_pfn(page);
			/* All pages but the first will start at offset 0. */
			unsigned long offset =
				(((unsigned long)data) & ~PAGE_MASK);
			size_t len_in_page = min(PAGE_SIZE - offset, len);
			struct scatterlist *next_desc =
				&builder->descs[builder->cur_desc];

			if (builder->cur_desc >= builder->num_descs)
				return -ENOSPC;

			/* Optimize contiguous pages */
			if (cur_pfn == prev_pfn + 1) {
				(next_desc - 1)->length += len_in_page;
			} else {
				memset(next_desc, 0, sizeof(*next_desc));
				sg_set_page(next_desc, page, len_in_page,
					    offset);
				builder->cur_desc++;
			}
			data += len_in_page;
			len -= len_in_page;
			prev_pfn = cur_pfn;
		}
	} else {
		/*
		 * As a last resort, copy into the shadow buffer and reference
		 * it with a single SG entry. Calling
		 * `scatterlist_builder_retrieve_data` will be necessary to copy
		 * the data written by the device back into @data.
		 */
		void *shadow_buffer =
			builder->shadow_buffer + builder->shadow_buffer_pos;
		struct page *page = virt_to_page(shadow_buffer);
		unsigned long offset =
			(((unsigned long)shadow_buffer) & ~PAGE_MASK);
		struct scatterlist *next_desc =
			&builder->descs[builder->cur_desc];

		if (len >
		    builder->shadow_buffer_size - builder->shadow_buffer_pos)
			return -ENOSPC;

		memcpy(shadow_buffer, data, len);
		memset(next_desc, 0, sizeof(*next_desc));
		sg_set_page(next_desc, page, len, offset);
		builder->cur_desc++;
		builder->shadow_buffer_pos += len;
	}

	sg_mark_end(&builder->descs[builder->cur_desc - 1]);
	return scatterlist_builder_add_descriptor(builder, cur_desc);
}

/**
 * scatterlist_builder_retrieve_data() - Retrieve a response written by the
 * device on the shadow buffer.
 * @builder: builder to use.
 * @sg_index: index of the descriptor to read from.
 * @data: destination for the shadowed data.
 *
 * If the shadow buffer is pointed to by the descriptor at index @sg_index of
 * the chain, then ``sg->length`` bytes are copied back from it into @data.
 * Otherwise nothing is done since the device has written into @data directly.
 *
 * @data must have originally been added by ``scatterlist_builder_add_data`` as
 * the same size as passed to ``scatterlist_builder_add_data`` will be copied
 * back.
 */
int scatterlist_builder_retrieve_data(struct scatterlist_builder *builder,
				      size_t sg_index, void *data)
{
	void *shadow_buf = builder->shadow_buffer;
	struct scatterlist *sg;
	void *kaddr;

	/* We can only retrieve from the range of sgs currently set. */
	if (sg_index >= builder->cur_sg)
		return -ERANGE;

	sg = builder->sgs[sg_index];
	kaddr = pfn_to_kaddr(page_to_pfn(sg_page(sg))) + sg->offset;

	if (kaddr >= shadow_buf &&
	    kaddr < shadow_buf + VIRTIO_SHADOW_BUF_SIZE) {
		if (kaddr + sg->length >= shadow_buf + VIRTIO_SHADOW_BUF_SIZE)
			return -EINVAL;

		memcpy(data, kaddr, sg->length);
	}

	return 0;
}

/**
 * scatterlist_builder_add_ioctl_cmd() - Add an ioctl command to the descriptor
 * chain.
 * @builder: builder to use.
 * @session: session on behalf of which the ioctl command is added.
 * @ioctl_code: code of the ioctl to add (i.e. ``VIDIOC_*``).
 */
int scatterlist_builder_add_ioctl_cmd(struct scatterlist_builder *builder,
				      struct virtio_media_session *session,
				      u32 ioctl_code)
{
	struct virtio_media_cmd_ioctl *cmd_ioctl = &session->cmd.ioctl;

	cmd_ioctl->hdr.cmd = VIRTIO_MEDIA_CMD_IOCTL;
	cmd_ioctl->session_id = session->id;
	cmd_ioctl->code = VIRTIO_MEDIA_IOCTL_CODE(ioctl_code);

	return scatterlist_builder_add_data(builder, cmd_ioctl,
					    sizeof(*cmd_ioctl));
}

/**
 * scatterlist_builder_add_ioctl_resp() - Add storage to receive an ioctl
 * response to the descriptor chain.
 * @builder: builder to use.
 * @session: session on behalf of which the ioctl response is added.
 */
int scatterlist_builder_add_ioctl_resp(struct scatterlist_builder *builder,
				       struct virtio_media_session *session)
{
	struct virtio_media_resp_ioctl *resp_ioctl = &session->resp.ioctl;

	return scatterlist_builder_add_data(builder, resp_ioctl,
					    sizeof(*resp_ioctl));
}

/**
 * __scatterlist_builder_add_userptr() - Add user pages to @builder.
 * @builder: builder to use.
 * @userptr: pointer to userspace memory that we want to add.
 * @length: length of the data to add.
 * @sg_list: output parameter. Upon success, points to the area of the shadow
 * buffer containing the array of SG entries to be added to the descriptor
 * chain.
 * @nents: output parameter. Upon success, contains the number of entries
 * pointed to by @sg_list.
 *
 * Data referenced by userspace pointers can be potentially large and very
 * scattered, which could overwhelm the descriptor chain if added as-is. For
 * these, we instead build an array of ``struct virtio_media_sg_entry`` in the
 * shadow buffer and reference it using a single descriptor.
 *
 * This function is a helper to perform that. Callers should then add the
 * descriptor to the chain properly.
 *
 * Returns -EFAULT if @userptr is not a valid user address, which is a case the
 * driver should consider as "normal" operation. All other failures signal a
 * problem with the driver.
 */
static int
__scatterlist_builder_add_userptr(struct scatterlist_builder *builder,
				  unsigned long userptr, unsigned long length,
				  struct virtio_media_sg_entry **sg_list,
				  int *nents)
{
	struct sg_table sg_table = {};
	struct frame_vector *framevec;
	struct scatterlist *sg_iter;
	struct page **pages;
	const unsigned int offset = userptr & ~PAGE_MASK;
	unsigned int pages_count;
	size_t entries_size;
	int i;
	int ret;

	framevec = vb2_create_framevec(userptr, length, true);
	if (IS_ERR(framevec)) {
		if (PTR_ERR(framevec) != -EFAULT) {
			pr_warn("error %ld creating frame vector for userptr 0x%lx, length 0x%lx\n",
				PTR_ERR(framevec), userptr, length);
		} else {
			/* -EINVAL is expected in case of invalid userptr. */
			framevec = ERR_PTR(-EINVAL);
		}
		return PTR_ERR(framevec);
	}

	pages = frame_vector_pages(framevec);
	if (IS_ERR(pages)) {
		pr_warn("error getting vector pages\n");
		ret = PTR_ERR(pages);
		goto done;
	}
	pages_count = frame_vector_count(framevec);
	ret = sg_alloc_table_from_pages(&sg_table, pages, pages_count, offset,
					length, 0);
	if (ret) {
		pr_warn("error creating sg table\n");
		goto done;
	}

	/* Allocate our actual SG in the shadow buffer. */
	*nents = sg_nents(sg_table.sgl);
	entries_size = sizeof(**sg_list) * *nents;
	if (builder->shadow_buffer_pos + entries_size >
	    builder->shadow_buffer_size) {
		ret = -ENOMEM;
		goto free_sg;
	}

	*sg_list = builder->shadow_buffer + builder->shadow_buffer_pos;
	builder->shadow_buffer_pos += entries_size;

	for_each_sgtable_sg(&sg_table, sg_iter, i) {
		struct virtio_media_sg_entry *sg_entry = &(*sg_list)[i];

		sg_entry->start = sg_phys(sg_iter);
		sg_entry->len = sg_iter->length;
	}

free_sg:
	sg_free_table(&sg_table);

done:
	vb2_destroy_framevec(framevec);
	return ret;
}

/**
 * scatterlist_builder_add_userptr() - Add a user-memory buffer using an array
 * of ``struct virtio_media_sg_entry``.
 * @builder: builder to use.
 * @userptr: pointer to userspace memory that we want to add.
 * @length: length of the data to add.
 *
 * Upon success, an array of ``struct virtio_media_sg_entry`` referencing
 * @userptr has been built into the shadow buffer, and that array added to the
 * descriptor chain.
 */
static int scatterlist_builder_add_userptr(struct scatterlist_builder *builder,
					   unsigned long userptr,
					   unsigned long length)
{
	int ret;
	int nents;
	struct virtio_media_sg_entry *sg_list;

	ret = __scatterlist_builder_add_userptr(builder, userptr, length,
						&sg_list, &nents);
	if (ret)
		return ret;

	ret = scatterlist_builder_add_data(builder, sg_list,
					   sizeof(*sg_list) * nents);
	if (ret)
		return ret;

	return 0;
}

/**
 * scatterlist_builder_add_buffer() - Add a ``v4l2_buffer`` and its planes to
 * the descriptor chain.
 * @builder: builder to use.
 * @b: ``v4l2_buffer`` to add.
 */
int scatterlist_builder_add_buffer(struct scatterlist_builder *builder,
				   struct v4l2_buffer *b)
{
	int i;
	int ret;

	/* Fixup: plane length must be zero if userptr is NULL */
	if (!V4L2_TYPE_IS_MULTIPLANAR(b->type) &&
	    b->memory == V4L2_MEMORY_USERPTR && b->m.userptr == 0)
		b->length = 0;

	/* v4l2_buffer */
	ret = scatterlist_builder_add_data(builder, b, sizeof(*b));
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type) && b->length > 0) {
		/* Fixup: plane length must be zero if userptr is NULL */
		if (b->memory == V4L2_MEMORY_USERPTR) {
			for (i = 0; i < b->length; i++) {
				struct v4l2_plane *plane = &b->m.planes[i];

				if (plane->m.userptr == 0)
					plane->length = 0;
			}
		}

		/* Array of v4l2_planes */
		ret = scatterlist_builder_add_data(builder, b->m.planes,
						   sizeof(struct v4l2_plane) *
							   b->length);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * scatterlist_builder_add_buffer_userptr() - Add the payload of a ``USERTPR``
 * v4l2_buffer to the descriptor chain.
 * @builder: builder to use.
 * @b: ``v4l2_buffer`` which ``USERPTR`` payload we want to add.
 *
 * Add an array of ``virtio_media_sg_entry`` pointing to a ``USERPTR`` buffer's
 * contents. Does nothing if the buffer is not of type ``USERPTR``. This is
 * split out of :ref:`scatterlist_builder_add_buffer` because we only want to
 * add these to the device-readable part of the descriptor chain.
 */
int scatterlist_builder_add_buffer_userptr(struct scatterlist_builder *builder,
					   struct v4l2_buffer *b)
{
	int i;
	int ret;

	if (b->memory != V4L2_MEMORY_USERPTR)
		return 0;

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		for (i = 0; i < b->length; i++) {
			struct v4l2_plane *plane = &b->m.planes[i];

			if (b->memory == V4L2_MEMORY_USERPTR &&
			    plane->length > 0) {
				ret = scatterlist_builder_add_userptr(
					builder, plane->m.userptr,
					plane->length);
				if (ret)
					return ret;
			}
		}
	} else if (b->length > 0) {
		ret = scatterlist_builder_add_userptr(builder, b->m.userptr,
						      b->length);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * scatterlist_builder_retrieve_buffer() - Retrieve a v4l2_buffer written by
 * the device on the shadow buffer, if needed.
 * @builder: builder to use.
 * @sg_index: index of the first SG entry of the buffer in the builder's
 * descriptor chain.
 * @b: v4l2_buffer to copy shadow buffer data into.
 * @orig_planes: the original ``planes`` pointer, to be restored if the buffer
 * is multi-planar.
 *
 * If the v4l2_buffer pointed by @buffer_sgs was copied into the shadow buffer,
 * then its updated content is copied back into @b. Otherwise nothing is done
 * as the device has written into @b directly.
 *
 * @orig_planes is used to restore the original ``planes`` pointer in case it
 * gets modified by the host. The specification stipulates that the host should
 * not modify it, but we enforce this for additional safety.
 */
int scatterlist_builder_retrieve_buffer(struct scatterlist_builder *builder,
					size_t sg_index, struct v4l2_buffer *b,
					struct v4l2_plane *orig_planes)
{
	int ret;

	ret = scatterlist_builder_retrieve_data(builder, sg_index++, b);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		b->m.planes = orig_planes;

		if (orig_planes != NULL) {
			ret = scatterlist_builder_retrieve_data(
				builder, sg_index++, b->m.planes);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/**
 * scatterlist_builder_add_ext_ctrls() - Add a v4l2_ext_controls and its
 * controls to @builder.
 * @builder: builder to use.
 * @ctrls: ``struct v4l2_ext_controls`` to add.
 *
 * Add @ctrls and its array of `struct v4l2_ext_control` to the descriptor chain.
 */
int scatterlist_builder_add_ext_ctrls(struct scatterlist_builder *builder,
				      struct v4l2_ext_controls *ctrls)
{
	int ret;

	/* v4l2_ext_controls */
	ret = scatterlist_builder_add_data(builder, ctrls, sizeof(*ctrls));
	if (ret)
		return ret;

	if (ctrls->count > 0) {
		/* array of v4l2_controls */
		ret = scatterlist_builder_add_data(builder, ctrls->controls,
						   sizeof(ctrls->controls[0]) *
							   ctrls->count);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * scatterlist_builder_add_ext_ctrls_userptrs() - Add the userspace payloads of
 * a ``struct v4l2_ext_controls`` to the descriptor chain.
 * @builder: builder to use.
 * @ctrls: ``struct v4l2_ext_controls`` from which we want to add the userspace payload of.
 *
 * Add the userspace payloads of @ctrls to the descriptor chain. This is split
 * out of :ref:`scatterlist_builder_add_ext_ctrls` because we only want to add
 * these to the device-readable part of the descriptor chain.
 */
int scatterlist_builder_add_ext_ctrls_userptrs(
	struct scatterlist_builder *builder, struct v4l2_ext_controls *ctrls)
{
	int i;
	int ret;

	/* Pointers to user memory in individual controls */
	for (i = 0; i < ctrls->count; i++) {
		struct v4l2_ext_control *ctrl = &ctrls->controls[i];

		if (ctrl->size > 0) {
			ret = scatterlist_builder_add_userptr(
				builder, (unsigned long)ctrl->ptr, ctrl->size);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/**
 * scatterlist_builder_retrieve_ext_ctrls() - Retrieve controls written by the
 * device on the shadow buffer, if needed.
 * @builder: builder to use.
 * @sg_index: index of the first SG entry of the controls in the builder's
 * descriptor chain.
 * @ctrls: ``struct v4l2_ext_controls`` to copy shadow buffer data into.
 *
 * If the shadow buffer is pointed to by @sg, copy its content back into @ctrls.
 */
int scatterlist_builder_retrieve_ext_ctrls(struct scatterlist_builder *builder,
					   size_t sg_index,
					   struct v4l2_ext_controls *ctrls)
{
	struct v4l2_ext_control *controls_backup = ctrls->controls;
	int ret;

	ret = scatterlist_builder_retrieve_data(builder, sg_index++, ctrls);
	if (ret)
		return ret;

	ctrls->controls = controls_backup;

	if (ctrls->count > 0 && ctrls->controls) {
		ret = scatterlist_builder_retrieve_data(builder, sg_index++,
							ctrls->controls);
		if (ret)
			return ret;
	}

	return 0;
}
