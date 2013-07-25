/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.htm
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2013, Intel Corporation.
 */
/*
 * lustre/include/lustre_update.h
 *
 * Author: Di Wang <di.wang@intel.com>
 */

#ifndef _LUSTRE_UPDATE_H
#define _LUSTRE_UPDATE_H

#define UPDATE_BUFFER_SIZE	8192
struct update_request {
	struct dt_device	*ur_dt;
	struct list_head		ur_list;    /* attached itself to thandle */
	int			ur_flags;
	int			ur_rc;	    /* request result */
	int			ur_batchid; /* Current batch(trans) id */
	struct update_buf	*ur_buf;   /* Holding the update req */
};

static inline unsigned long update_size(struct update *update)
{
	unsigned long size;
	int	   i;

	size = cfs_size_round(offsetof(struct update, u_bufs[0]));
	for (i = 0; i < UPDATE_BUF_COUNT; i++)
		size += cfs_size_round(update->u_lens[i]);

	return size;
}

static inline void *update_param_buf(struct update *update, int index,
				     int *size)
{
	int	i;
	void	*ptr;

	if (index >= UPDATE_BUF_COUNT)
		return NULL;

	ptr = (char *)update + cfs_size_round(offsetof(struct update,
						       u_bufs[0]));
	for (i = 0; i < index; i++) {
		LASSERT(update->u_lens[i] > 0);
		ptr += cfs_size_round(update->u_lens[i]);
	}

	if (size != NULL)
		*size = update->u_lens[index];

	return ptr;
}

static inline unsigned long update_buf_size(struct update_buf *buf)
{
	unsigned long size;
	int	   i = 0;

	size = cfs_size_round(offsetof(struct update_buf, ub_bufs[0]));
	for (i = 0; i < buf->ub_count; i++) {
		struct update *update;

		update = (struct update *)((char *)buf + size);
		size += update_size(update);
	}
	LASSERT(size <= UPDATE_BUFFER_SIZE);
	return size;
}

static inline void *update_buf_get(struct update_buf *buf, int index, int *size)
{
	int	count = buf->ub_count;
	void	*ptr;
	int	i = 0;

	if (index >= count)
		return NULL;

	ptr = (char *)buf + cfs_size_round(offsetof(struct update_buf,
						    ub_bufs[0]));
	for (i = 0; i < index; i++)
		ptr += update_size((struct update *)ptr);

	if (size != NULL)
		*size = update_size((struct update *)ptr);

	return ptr;
}

static inline void update_init_reply_buf(struct update_reply *reply, int count)
{
	reply->ur_version = UPDATE_REPLY_V1;
	reply->ur_count = count;
}

static inline void *update_get_buf_internal(struct update_reply *reply,
					    int index, int *size)
{
	char *ptr;
	int count = reply->ur_count;
	int i;

	if (index >= count)
		return NULL;

	ptr = (char *)reply + cfs_size_round(offsetof(struct update_reply,
					     ur_lens[count]));
	for (i = 0; i < index; i++) {
		LASSERT(reply->ur_lens[i] > 0);
		ptr += cfs_size_round(reply->ur_lens[i]);
	}

	if (size != NULL)
		*size = reply->ur_lens[index];

	return ptr;
}

static inline void update_insert_reply(struct update_reply *reply, void *data,
				       int data_len, int index, int rc)
{
	char *ptr;

	ptr = update_get_buf_internal(reply, index, NULL);
	LASSERT(ptr != NULL);

	*(int *)ptr = cpu_to_le32(rc);
	ptr += sizeof(int);
	if (data_len > 0) {
		LASSERT(data != NULL);
		memcpy(ptr, data, data_len);
	}
	reply->ur_lens[index] = data_len + sizeof(int);
}

static inline int update_get_reply_buf(struct update_reply *reply, void **buf,
				       int index)
{
	char *ptr;
	int  size = 0;
	int  result;

	ptr = update_get_buf_internal(reply, index, &size);
	result = *(int *)ptr;

	if (result < 0)
		return result;

	LASSERT((ptr != NULL && size >= sizeof(int)));
	*buf = ptr + sizeof(int);
	return size - sizeof(int);
}

static inline int update_get_reply_result(struct update_reply *reply,
					  void **buf, int index)
{
	void *ptr;
	int  size;

	ptr = update_get_buf_internal(reply, index, &size);
	LASSERT(ptr != NULL && size > sizeof(int));
	return *(int *)ptr;
}

#endif
