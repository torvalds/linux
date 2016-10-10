/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __QEDR_VERBS_H__
#define __QEDR_VERBS_H__

int qedr_query_device(struct ib_device *ibdev,
		      struct ib_device_attr *attr, struct ib_udata *udata);
int qedr_query_port(struct ib_device *, u8 port, struct ib_port_attr *props);
int qedr_modify_port(struct ib_device *, u8 port, int mask,
		     struct ib_port_modify *props);

int qedr_query_gid(struct ib_device *, u8 port, int index, union ib_gid *gid);

int qedr_query_pkey(struct ib_device *, u8 port, u16 index, u16 *pkey);

struct ib_ucontext *qedr_alloc_ucontext(struct ib_device *, struct ib_udata *);
int qedr_dealloc_ucontext(struct ib_ucontext *);

int qedr_mmap(struct ib_ucontext *, struct vm_area_struct *vma);
int qedr_del_gid(struct ib_device *device, u8 port_num,
		 unsigned int index, void **context);
int qedr_add_gid(struct ib_device *device, u8 port_num,
		 unsigned int index, const union ib_gid *gid,
		 const struct ib_gid_attr *attr, void **context);
struct ib_pd *qedr_alloc_pd(struct ib_device *,
			    struct ib_ucontext *, struct ib_udata *);
int qedr_dealloc_pd(struct ib_pd *pd);

struct ib_cq *qedr_create_cq(struct ib_device *ibdev,
			     const struct ib_cq_init_attr *attr,
			     struct ib_ucontext *ib_ctx,
			     struct ib_udata *udata);
int qedr_resize_cq(struct ib_cq *, int cqe, struct ib_udata *);
int qedr_destroy_cq(struct ib_cq *);
int qedr_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags);

#endif
