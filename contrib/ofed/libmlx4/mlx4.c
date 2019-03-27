/*
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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
 *        disclaimer in the documentation and/or other materials
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>

#include "mlx4.h"
#include "mlx4-abi.h"

#ifndef PCI_VENDOR_ID_MELLANOX
#define PCI_VENDOR_ID_MELLANOX			0x15b3
#endif

#define HCA(v, d) \
	{ .vendor = PCI_VENDOR_ID_##v,			\
	  .device = d }

static struct {
	unsigned		vendor;
	unsigned		device;
} hca_table[] = {
	HCA(MELLANOX, 0x6340),	/* MT25408 "Hermon" SDR */
	HCA(MELLANOX, 0x634a),	/* MT25408 "Hermon" DDR */
	HCA(MELLANOX, 0x6354),	/* MT25408 "Hermon" QDR */
	HCA(MELLANOX, 0x6732),	/* MT25408 "Hermon" DDR PCIe gen2 */
	HCA(MELLANOX, 0x673c),	/* MT25408 "Hermon" QDR PCIe gen2 */
	HCA(MELLANOX, 0x6368),	/* MT25408 "Hermon" EN 10GigE */
	HCA(MELLANOX, 0x6750),	/* MT25408 "Hermon" EN 10GigE PCIe gen2 */
	HCA(MELLANOX, 0x6372),	/* MT25458 ConnectX EN 10GBASE-T 10GigE */
	HCA(MELLANOX, 0x675a),	/* MT25458 ConnectX EN 10GBASE-T+Gen2 10GigE */
	HCA(MELLANOX, 0x6764),	/* MT26468 ConnectX EN 10GigE PCIe gen2*/
	HCA(MELLANOX, 0x6746),	/* MT26438 ConnectX EN 40GigE PCIe gen2 5GT/s */
	HCA(MELLANOX, 0x676e),	/* MT26478 ConnectX2 40GigE PCIe gen2 */
	HCA(MELLANOX, 0x1002),	/* MT25400 Family [ConnectX-2 Virtual Function] */
	HCA(MELLANOX, 0x1003),	/* MT27500 Family [ConnectX-3] */
	HCA(MELLANOX, 0x1004),	/* MT27500 Family [ConnectX-3 Virtual Function] */
	HCA(MELLANOX, 0x1005),	/* MT27510 Family */
	HCA(MELLANOX, 0x1006),	/* MT27511 Family */
	HCA(MELLANOX, 0x1007),	/* MT27520 Family */
	HCA(MELLANOX, 0x1008),	/* MT27521 Family */
	HCA(MELLANOX, 0x1009),	/* MT27530 Family */
	HCA(MELLANOX, 0x100a),	/* MT27531 Family */
	HCA(MELLANOX, 0x100b),	/* MT27540 Family */
	HCA(MELLANOX, 0x100c),	/* MT27541 Family */
	HCA(MELLANOX, 0x100d),	/* MT27550 Family */
	HCA(MELLANOX, 0x100e),	/* MT27551 Family */
	HCA(MELLANOX, 0x100f),	/* MT27560 Family */
	HCA(MELLANOX, 0x1010),	/* MT27561 Family */
};

static struct ibv_context_ops mlx4_ctx_ops = {
	.query_device  = mlx4_query_device,
	.query_port    = mlx4_query_port,
	.alloc_pd      = mlx4_alloc_pd,
	.dealloc_pd    = mlx4_free_pd,
	.reg_mr	       = mlx4_reg_mr,
	.rereg_mr      = mlx4_rereg_mr,
	.dereg_mr      = mlx4_dereg_mr,
	.alloc_mw      = mlx4_alloc_mw,
	.dealloc_mw    = mlx4_dealloc_mw,
	.bind_mw       = mlx4_bind_mw,
	.create_cq     = mlx4_create_cq,
	.poll_cq       = mlx4_poll_cq,
	.req_notify_cq = mlx4_arm_cq,
	.cq_event      = mlx4_cq_event,
	.resize_cq     = mlx4_resize_cq,
	.destroy_cq    = mlx4_destroy_cq,
	.create_srq    = mlx4_create_srq,
	.modify_srq    = mlx4_modify_srq,
	.query_srq     = mlx4_query_srq,
	.destroy_srq   = mlx4_destroy_srq,
	.post_srq_recv = mlx4_post_srq_recv,
	.create_qp     = mlx4_create_qp,
	.query_qp      = mlx4_query_qp,
	.modify_qp     = mlx4_modify_qp,
	.destroy_qp    = mlx4_destroy_qp,
	.post_send     = mlx4_post_send,
	.post_recv     = mlx4_post_recv,
	.create_ah     = mlx4_create_ah,
	.destroy_ah    = mlx4_destroy_ah,
	.attach_mcast  = ibv_cmd_attach_mcast,
	.detach_mcast  = ibv_cmd_detach_mcast
};

static int mlx4_map_internal_clock(struct mlx4_device *mdev,
				   struct ibv_context *ibv_ctx)
{
	struct mlx4_context *context = to_mctx(ibv_ctx);
	void *hca_clock_page;

	hca_clock_page = mmap(NULL, mdev->page_size,
			      PROT_READ, MAP_SHARED, ibv_ctx->cmd_fd,
			      mdev->page_size * 3);

	if (hca_clock_page == MAP_FAILED) {
		fprintf(stderr, PFX
			"Warning: Timestamp available,\n"
			"but failed to mmap() hca core clock page.\n");
		return -1;
	}

	context->hca_core_clock = hca_clock_page +
		(context->core_clock.offset & (mdev->page_size - 1));
	return 0;
}

static int mlx4_init_context(struct verbs_device *v_device,
				struct ibv_context *ibv_ctx, int cmd_fd)
{
	struct mlx4_context	       *context;
	struct ibv_get_context		cmd;
	struct mlx4_alloc_ucontext_resp resp;
	int				i;
	struct mlx4_alloc_ucontext_resp_v3 resp_v3;
	__u16				bf_reg_size;
	struct mlx4_device              *dev = to_mdev(&v_device->device);
	struct verbs_context *verbs_ctx = verbs_get_ctx(ibv_ctx);
	struct ibv_device_attr_ex	dev_attrs;

	/* memory footprint of mlx4_context and verbs_context share
	* struct ibv_context.
	*/
	context = to_mctx(ibv_ctx);
	ibv_ctx->cmd_fd = cmd_fd;

	if (dev->abi_version <= MLX4_UVERBS_NO_DEV_CAPS_ABI_VERSION) {
		if (ibv_cmd_get_context(ibv_ctx, &cmd, sizeof cmd,
					&resp_v3.ibv_resp, sizeof resp_v3))
			return errno;

		context->num_qps  = resp_v3.qp_tab_size;
		bf_reg_size	  = resp_v3.bf_reg_size;
		context->cqe_size = sizeof (struct mlx4_cqe);
	} else  {
		if (ibv_cmd_get_context(ibv_ctx, &cmd, sizeof cmd,
					&resp.ibv_resp, sizeof resp))
			return errno;

		context->num_qps  = resp.qp_tab_size;
		bf_reg_size	  = resp.bf_reg_size;
		if (resp.dev_caps & MLX4_USER_DEV_CAP_64B_CQE)
			context->cqe_size = resp.cqe_size;
		else
			context->cqe_size = sizeof (struct mlx4_cqe);
	}

	context->qp_table_shift = ffs(context->num_qps) - 1 - MLX4_QP_TABLE_BITS;
	context->qp_table_mask	= (1 << context->qp_table_shift) - 1;
	for (i = 0; i < MLX4_PORTS_NUM; ++i)
		context->port_query_cache[i].valid = 0;

	pthread_mutex_init(&context->qp_table_mutex, NULL);
	for (i = 0; i < MLX4_QP_TABLE_SIZE; ++i)
		context->qp_table[i].refcnt = 0;

	for (i = 0; i < MLX4_NUM_DB_TYPE; ++i)
		context->db_list[i] = NULL;

	mlx4_init_xsrq_table(&context->xsrq_table, context->num_qps);
	pthread_mutex_init(&context->db_list_mutex, NULL);

	context->uar = mmap(NULL, dev->page_size, PROT_WRITE,
			    MAP_SHARED, cmd_fd, 0);
	if (context->uar == MAP_FAILED)
		return errno;

	if (bf_reg_size) {
		context->bf_page = mmap(NULL, dev->page_size,
					PROT_WRITE, MAP_SHARED, cmd_fd,
					dev->page_size);
		if (context->bf_page == MAP_FAILED) {
			fprintf(stderr, PFX "Warning: BlueFlame available, "
				"but failed to mmap() BlueFlame page.\n");
				context->bf_page     = NULL;
				context->bf_buf_size = 0;
		} else {
			context->bf_buf_size = bf_reg_size / 2;
			context->bf_offset   = 0;
			pthread_spin_init(&context->bf_lock, PTHREAD_PROCESS_PRIVATE);
		}
	} else {
		context->bf_page     = NULL;
		context->bf_buf_size = 0;
	}

	pthread_spin_init(&context->uar_lock, PTHREAD_PROCESS_PRIVATE);
	ibv_ctx->ops = mlx4_ctx_ops;

	context->hca_core_clock = NULL;
	memset(&dev_attrs, 0, sizeof(dev_attrs));
	if (!mlx4_query_device_ex(ibv_ctx, NULL, &dev_attrs,
				  sizeof(struct ibv_device_attr_ex))) {
		context->max_qp_wr = dev_attrs.orig_attr.max_qp_wr;
		context->max_sge = dev_attrs.orig_attr.max_sge;
		if (context->core_clock.offset_valid)
			mlx4_map_internal_clock(dev, ibv_ctx);
	}

	verbs_ctx->has_comp_mask = VERBS_CONTEXT_XRCD | VERBS_CONTEXT_SRQ |
					VERBS_CONTEXT_QP;
	verbs_set_ctx_op(verbs_ctx, close_xrcd, mlx4_close_xrcd);
	verbs_set_ctx_op(verbs_ctx, open_xrcd, mlx4_open_xrcd);
	verbs_set_ctx_op(verbs_ctx, create_srq_ex, mlx4_create_srq_ex);
	verbs_set_ctx_op(verbs_ctx, get_srq_num, verbs_get_srq_num);
	verbs_set_ctx_op(verbs_ctx, create_qp_ex, mlx4_create_qp_ex);
	verbs_set_ctx_op(verbs_ctx, open_qp, mlx4_open_qp);
	verbs_set_ctx_op(verbs_ctx, ibv_create_flow, ibv_cmd_create_flow);
	verbs_set_ctx_op(verbs_ctx, ibv_destroy_flow, ibv_cmd_destroy_flow);
	verbs_set_ctx_op(verbs_ctx, create_cq_ex, mlx4_create_cq_ex);
	verbs_set_ctx_op(verbs_ctx, query_device_ex, mlx4_query_device_ex);
	verbs_set_ctx_op(verbs_ctx, query_rt_values, mlx4_query_rt_values);

	return 0;

}

static void mlx4_uninit_context(struct verbs_device *v_device,
					struct ibv_context *ibv_ctx)
{
	struct mlx4_context *context = to_mctx(ibv_ctx);

	munmap(context->uar, to_mdev(&v_device->device)->page_size);
	if (context->bf_page)
		munmap(context->bf_page, to_mdev(&v_device->device)->page_size);
	if (context->hca_core_clock)
		munmap(context->hca_core_clock - context->core_clock.offset,
		       to_mdev(&v_device->device)->page_size);
}

static struct verbs_device_ops mlx4_dev_ops = {
	.init_context = mlx4_init_context,
	.uninit_context = mlx4_uninit_context,
};

static struct verbs_device *mlx4_driver_init(const char *uverbs_sys_path, int abi_version)
{
	char			value[8];
	struct mlx4_device    *dev;
	unsigned		vendor, device;
	int			i;

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/vendor",
				value, sizeof value) < 0)
		return NULL;
	vendor = strtol(value, NULL, 16);

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/device",
				value, sizeof value) < 0)
		return NULL;
	device = strtol(value, NULL, 16);

	for (i = 0; i < sizeof hca_table / sizeof hca_table[0]; ++i)
		if (vendor == hca_table[i].vendor &&
		    device == hca_table[i].device)
			goto found;

	return NULL;

found:
	if (abi_version < MLX4_UVERBS_MIN_ABI_VERSION ||
	    abi_version > MLX4_UVERBS_MAX_ABI_VERSION) {
		fprintf(stderr, PFX "Fatal: ABI version %d of %s is not supported "
			"(min supported %d, max supported %d)\n",
			abi_version, uverbs_sys_path,
			MLX4_UVERBS_MIN_ABI_VERSION,
			MLX4_UVERBS_MAX_ABI_VERSION);
		return NULL;
	}

	dev = calloc(1, sizeof *dev);
	if (!dev) {
		fprintf(stderr, PFX "Fatal: couldn't allocate device for %s\n",
			uverbs_sys_path);
		return NULL;
	}

	dev->page_size   = sysconf(_SC_PAGESIZE);
	dev->abi_version = abi_version;

	dev->verbs_dev.ops = &mlx4_dev_ops;
	dev->verbs_dev.sz = sizeof(*dev);
	dev->verbs_dev.size_of_context =
		sizeof(struct mlx4_context) - sizeof(struct ibv_context);

	return &dev->verbs_dev;
}

static __attribute__((constructor)) void mlx4_register_driver(void)
{
	verbs_register_driver("mlx4", mlx4_driver_init);
}
