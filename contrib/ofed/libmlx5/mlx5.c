/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
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
#define _GNU_SOURCE
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/cpuset.h>

#include "mlx5.h"
#include "mlx5-abi.h"

#ifndef PCI_VENDOR_ID_MELLANOX
#define PCI_VENDOR_ID_MELLANOX			0x15b3
#endif

#ifndef CPU_OR
#define CPU_OR(x, y, z) do {} while (0)
#endif

#ifndef CPU_EQUAL
#define CPU_EQUAL(x, y) 1
#endif


#define HCA(v, d) \
	{ .vendor = PCI_VENDOR_ID_##v,			\
	  .device = d }

static struct {
	unsigned		vendor;
	unsigned		device;
} hca_table[] = {
	HCA(MELLANOX, 4113),	/* MT4113 Connect-IB */
	HCA(MELLANOX, 4114),	/* Connect-IB Virtual Function */
	HCA(MELLANOX, 4115),	/* ConnectX-4 */
	HCA(MELLANOX, 4116),	/* ConnectX-4 Virtual Function */
	HCA(MELLANOX, 4117),	/* ConnectX-4LX */
	HCA(MELLANOX, 4118),	/* ConnectX-4LX Virtual Function */
	HCA(MELLANOX, 4119),	/* ConnectX-5, PCIe 3.0 */
	HCA(MELLANOX, 4120),	/* ConnectX-5 Virtual Function */
	HCA(MELLANOX, 4121),    /* ConnectX-5 Ex */
	HCA(MELLANOX, 4122),	/* ConnectX-5 Ex VF */
	HCA(MELLANOX, 4123),    /* ConnectX-6 */
	HCA(MELLANOX, 4124),	/* ConnectX-6 VF */
	HCA(MELLANOX, 41682),	/* BlueField integrated ConnectX-5 network controller */
	HCA(MELLANOX, 41683),	/* BlueField integrated ConnectX-5 network controller VF */
};

uint32_t mlx5_debug_mask = 0;
int mlx5_freeze_on_error_cqe;

static struct ibv_context_ops mlx5_ctx_ops = {
	.query_device  = mlx5_query_device,
	.query_port    = mlx5_query_port,
	.alloc_pd      = mlx5_alloc_pd,
	.dealloc_pd    = mlx5_free_pd,
	.reg_mr	       = mlx5_reg_mr,
	.rereg_mr      = mlx5_rereg_mr,
	.dereg_mr      = mlx5_dereg_mr,
	.alloc_mw      = mlx5_alloc_mw,
	.dealloc_mw    = mlx5_dealloc_mw,
	.bind_mw       = mlx5_bind_mw,
	.create_cq     = mlx5_create_cq,
	.poll_cq       = mlx5_poll_cq,
	.req_notify_cq = mlx5_arm_cq,
	.cq_event      = mlx5_cq_event,
	.resize_cq     = mlx5_resize_cq,
	.destroy_cq    = mlx5_destroy_cq,
	.create_srq    = mlx5_create_srq,
	.modify_srq    = mlx5_modify_srq,
	.query_srq     = mlx5_query_srq,
	.destroy_srq   = mlx5_destroy_srq,
	.post_srq_recv = mlx5_post_srq_recv,
	.create_qp     = mlx5_create_qp,
	.query_qp      = mlx5_query_qp,
	.modify_qp     = mlx5_modify_qp,
	.destroy_qp    = mlx5_destroy_qp,
	.post_send     = mlx5_post_send,
	.post_recv     = mlx5_post_recv,
	.create_ah     = mlx5_create_ah,
	.destroy_ah    = mlx5_destroy_ah,
	.attach_mcast  = mlx5_attach_mcast,
	.detach_mcast  = mlx5_detach_mcast
};

static int read_number_from_line(const char *line, int *value)
{
	const char *ptr;

	ptr = strchr(line, ':');
	if (!ptr)
		return 1;

	++ptr;

	*value = atoi(ptr);
	return 0;
}
/**
 * The function looks for the first free user-index in all the
 * user-index tables. If all are used, returns -1, otherwise
 * a valid user-index.
 * In case the reference count of the table is zero, it means the
 * table is not in use and wasn't allocated yet, therefore the
 * mlx5_store_uidx allocates the table, and increment the reference
 * count on the table.
 */
static int32_t get_free_uidx(struct mlx5_context *ctx)
{
	int32_t tind;
	int32_t i;

	for (tind = 0; tind < MLX5_UIDX_TABLE_SIZE; tind++) {
		if (ctx->uidx_table[tind].refcnt < MLX5_UIDX_TABLE_MASK)
			break;
	}

	if (tind == MLX5_UIDX_TABLE_SIZE)
		return -1;

	if (!ctx->uidx_table[tind].refcnt)
		return tind << MLX5_UIDX_TABLE_SHIFT;

	for (i = 0; i < MLX5_UIDX_TABLE_MASK + 1; i++) {
		if (!ctx->uidx_table[tind].table[i])
			break;
	}

	return (tind << MLX5_UIDX_TABLE_SHIFT) | i;
}

int32_t mlx5_store_uidx(struct mlx5_context *ctx, void *rsc)
{
	int32_t tind;
	int32_t ret = -1;
	int32_t uidx;

	pthread_mutex_lock(&ctx->uidx_table_mutex);
	uidx = get_free_uidx(ctx);
	if (uidx < 0)
		goto out;

	tind = uidx >> MLX5_UIDX_TABLE_SHIFT;

	if (!ctx->uidx_table[tind].refcnt) {
		ctx->uidx_table[tind].table = calloc(MLX5_UIDX_TABLE_MASK + 1,
						     sizeof(struct mlx5_resource *));
		if (!ctx->uidx_table[tind].table)
			goto out;
	}

	++ctx->uidx_table[tind].refcnt;
	ctx->uidx_table[tind].table[uidx & MLX5_UIDX_TABLE_MASK] = rsc;
	ret = uidx;

out:
	pthread_mutex_unlock(&ctx->uidx_table_mutex);
	return ret;
}

void mlx5_clear_uidx(struct mlx5_context *ctx, uint32_t uidx)
{
	int tind = uidx >> MLX5_UIDX_TABLE_SHIFT;

	pthread_mutex_lock(&ctx->uidx_table_mutex);

	if (!--ctx->uidx_table[tind].refcnt)
		free(ctx->uidx_table[tind].table);
	else
		ctx->uidx_table[tind].table[uidx & MLX5_UIDX_TABLE_MASK] = NULL;

	pthread_mutex_unlock(&ctx->uidx_table_mutex);
}

static int mlx5_is_sandy_bridge(int *num_cores)
{
	char line[128];
	FILE *fd;
	int rc = 0;
	int cur_cpu_family = -1;
	int cur_cpu_model = -1;

	fd = fopen("/proc/cpuinfo", "r");
	if (!fd)
		return 0;

	*num_cores = 0;

	while (fgets(line, 128, fd)) {
		int value;

		/* if this is information on new processor */
		if (!strncmp(line, "processor", 9)) {
			++*num_cores;

			cur_cpu_family = -1;
			cur_cpu_model  = -1;
		} else if (!strncmp(line, "cpu family", 10)) {
			if ((cur_cpu_family < 0) && (!read_number_from_line(line, &value)))
				cur_cpu_family = value;
		} else if (!strncmp(line, "model", 5)) {
			if ((cur_cpu_model < 0) && (!read_number_from_line(line, &value)))
				cur_cpu_model = value;
		}

		/* if this is a Sandy Bridge CPU */
		if ((cur_cpu_family == 6) &&
		    (cur_cpu_model == 0x2A || (cur_cpu_model == 0x2D) ))
			rc = 1;
	}

	fclose(fd);
	return rc;
}

/*
man cpuset

  This format displays each 32-bit word in hexadecimal (using ASCII characters "0" - "9" and "a" - "f"); words
  are filled with leading zeros, if required. For masks longer than one word, a comma separator is used between
  words. Words are displayed in big-endian order, which has the most significant bit first. The hex digits
  within a word are also in big-endian order.

  The number of 32-bit words displayed is the minimum number needed to display all bits of the bitmask, based on
  the size of the bitmask.

  Examples of the Mask Format:

     00000001                        # just bit 0 set
     40000000,00000000,00000000      # just bit 94 set
     000000ff,00000000               # bits 32-39 set
     00000000,000E3862               # 1,5,6,11-13,17-19 set

  A mask with bits 0, 1, 2, 4, 8, 16, 32, and 64 set displays as:

     00000001,00000001,00010117

  The first "1" is for bit 64, the second for bit 32, the third for bit 16, the fourth for bit 8, the fifth for
  bit 4, and the "7" is for bits 2, 1, and 0.
*/
static void mlx5_local_cpu_set(struct ibv_device *ibdev, cpuset_t *cpu_set)
{
	char *p, buf[1024];
	char *env_value;
	uint32_t word;
	int i, k;

	env_value = getenv("MLX5_LOCAL_CPUS");
	if (env_value)
		strncpy(buf, env_value, sizeof(buf));
	else {
		char fname[MAXPATHLEN];

		snprintf(fname, MAXPATHLEN, "/sys/class/infiniband/%s",
			 ibv_get_device_name(ibdev));

		if (ibv_read_sysfs_file(fname, "device/local_cpus", buf, sizeof(buf))) {
			fprintf(stderr, PFX "Warning: can not get local cpu set: failed to open %s\n", fname);
			return;
		}
	}

	p = strrchr(buf, ',');
	if (!p)
		p = buf;

	i = 0;
	do {
		if (*p == ',') {
			*p = 0;
			p ++;
		}

		word = strtoul(p, NULL, 16);

		for (k = 0; word; ++k, word >>= 1)
			if (word & 1)
				CPU_SET(k+i, cpu_set);

		if (p == buf)
			break;

		p = strrchr(buf, ',');
		if (!p)
			p = buf;

		i += 32;
	} while (i < CPU_SETSIZE);
}

static int mlx5_enable_sandy_bridge_fix(struct ibv_device *ibdev)
{
	cpuset_t my_cpus, dev_local_cpus, result_set;
	int stall_enable;
	int ret;
	int num_cores;

	if (!mlx5_is_sandy_bridge(&num_cores))
		return 0;

	/* by default enable stall on sandy bridge arch */
	stall_enable = 1;

	/*
	 * check if app is bound to cpu set that is inside
	 * of device local cpu set. Disable stalling if true
	 */

	/* use static cpu set - up to CPU_SETSIZE (1024) cpus/node */
	CPU_ZERO(&my_cpus);
	CPU_ZERO(&dev_local_cpus);
	CPU_ZERO(&result_set);
	ret = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
	    sizeof(my_cpus), &my_cpus);
	if (ret == -1) {
		if (errno == EINVAL)
			fprintf(stderr, PFX "Warning: my cpu set is too small\n");
		else
			fprintf(stderr, PFX "Warning: failed to get my cpu set\n");
		goto out;
	}

	/* get device local cpu set */
	mlx5_local_cpu_set(ibdev, &dev_local_cpus);

	/* check if my cpu set is in dev cpu */
	CPU_OR(&result_set, &my_cpus);
	CPU_OR(&result_set, &dev_local_cpus);
	stall_enable = CPU_EQUAL(&result_set, &dev_local_cpus) ? 0 : 1;

out:
	return stall_enable;
}

static void mlx5_read_env(struct ibv_device *ibdev, struct mlx5_context *ctx)
{
	char *env_value;

	env_value = getenv("MLX5_STALL_CQ_POLL");
	if (env_value)
		/* check if cq stall is enforced by user */
		ctx->stall_enable = (strcmp(env_value, "0")) ? 1 : 0;
	else
		/* autodetect if we need to do cq polling */
		ctx->stall_enable = mlx5_enable_sandy_bridge_fix(ibdev);

	env_value = getenv("MLX5_STALL_NUM_LOOP");
	if (env_value)
		mlx5_stall_num_loop = atoi(env_value);

	env_value = getenv("MLX5_STALL_CQ_POLL_MIN");
	if (env_value)
		mlx5_stall_cq_poll_min = atoi(env_value);

	env_value = getenv("MLX5_STALL_CQ_POLL_MAX");
	if (env_value)
		mlx5_stall_cq_poll_max = atoi(env_value);

	env_value = getenv("MLX5_STALL_CQ_INC_STEP");
	if (env_value)
		mlx5_stall_cq_inc_step = atoi(env_value);

	env_value = getenv("MLX5_STALL_CQ_DEC_STEP");
	if (env_value)
		mlx5_stall_cq_dec_step = atoi(env_value);

	ctx->stall_adaptive_enable = 0;
	ctx->stall_cycles = 0;

	if (mlx5_stall_num_loop < 0) {
		ctx->stall_adaptive_enable = 1;
		ctx->stall_cycles = mlx5_stall_cq_poll_min;
	}

}

static int get_total_uuars(int page_size)
{
	int size = MLX5_DEF_TOT_UUARS;
	int uuars_in_page;
	char *env;

	env = getenv("MLX5_TOTAL_UUARS");
	if (env)
		size = atoi(env);

	if (size < 1)
		return -EINVAL;

	uuars_in_page = page_size / MLX5_ADAPTER_PAGE_SIZE * MLX5_NUM_NON_FP_BFREGS_PER_UAR;
	size = max(uuars_in_page, size);
	size = align(size, MLX5_NUM_NON_FP_BFREGS_PER_UAR);
	if (size > MLX5_MAX_BFREGS)
		return -ENOMEM;

	return size;
}

static void open_debug_file(struct mlx5_context *ctx)
{
	char *env;

	env = getenv("MLX5_DEBUG_FILE");
	if (!env) {
		ctx->dbg_fp = stderr;
		return;
	}

	ctx->dbg_fp = fopen(env, "aw+");
	if (!ctx->dbg_fp) {
		fprintf(stderr, "Failed opening debug file %s, using stderr\n", env);
		ctx->dbg_fp = stderr;
		return;
	}
}

static void close_debug_file(struct mlx5_context *ctx)
{
	if (ctx->dbg_fp && ctx->dbg_fp != stderr)
		fclose(ctx->dbg_fp);
}

static void set_debug_mask(void)
{
	char *env;

	env = getenv("MLX5_DEBUG_MASK");
	if (env)
		mlx5_debug_mask = strtol(env, NULL, 0);
}

static void set_freeze_on_error(void)
{
	char *env;

	env = getenv("MLX5_FREEZE_ON_ERROR_CQE");
	if (env)
		mlx5_freeze_on_error_cqe = strtol(env, NULL, 0);
}

static int get_always_bf(void)
{
	char *env;

	env = getenv("MLX5_POST_SEND_PREFER_BF");
	if (!env)
		return 1;

	return strcmp(env, "0") ? 1 : 0;
}

static int get_shut_up_bf(void)
{
	char *env;

	env = getenv("MLX5_SHUT_UP_BF");
	if (!env)
		return 0;

	return strcmp(env, "0") ? 1 : 0;
}

static int get_num_low_lat_uuars(int tot_uuars)
{
	char *env;
	int num = 4;

	env = getenv("MLX5_NUM_LOW_LAT_UUARS");
	if (env)
		num = atoi(env);

	if (num < 0)
		return -EINVAL;

	num = max(num, tot_uuars - MLX5_MED_BFREGS_TSHOLD);
	return num;
}

/* The library allocates an array of uuar contexts. The one in index zero does
 * not to execersize odd/even policy so it can avoid a lock but it may not use
 * blue flame. The upper ones, low_lat_uuars can use blue flame with no lock
 * since they are assigned to one QP only. The rest can use blue flame but since
 * they are shared they need a lock
 */
static int need_uuar_lock(struct mlx5_context *ctx, int uuarn)
{
	if (uuarn == 0 || mlx5_single_threaded)
		return 0;

	if (uuarn >= (ctx->tot_uuars - ctx->low_lat_uuars) * 2)
		return 0;

	return 1;
}

static int single_threaded_app(void)
{

	char *env;

	env = getenv("MLX5_SINGLE_THREADED");
	if (env)
		return strcmp(env, "1") ? 0 : 1;

	return 0;
}

static int mlx5_cmd_get_context(struct mlx5_context *context,
				struct mlx5_alloc_ucontext *req,
				size_t req_len,
				struct mlx5_alloc_ucontext_resp *resp,
				size_t resp_len)
{
	if (!ibv_cmd_get_context(&context->ibv_ctx, &req->ibv_req,
				 req_len, &resp->ibv_resp, resp_len))
		return 0;

	/* The ibv_cmd_get_context fails in older kernels when passing
	 * a request length that the kernel doesn't know.
	 * To avoid breaking compatibility of new libmlx5 and older
	 * kernels, when ibv_cmd_get_context fails with the full
	 * request length, we try once again with the legacy length.
	 * We repeat this process while reducing requested size based
	 * on the feature input size. To avoid this in the future, we
	 * will remove the check in kernel that requires fields unknown
	 * to the kernel to be cleared. This will require that any new
	 * feature that involves extending struct mlx5_alloc_ucontext
	 * will be accompanied by an indication in the form of one or
	 * more fields in struct mlx5_alloc_ucontext_resp. If the
	 * response value can be interpreted as feature not supported
	 * when the returned value is zero, this will suffice to
	 * indicate to the library that the request was ignored by the
	 * kernel, either because it is unaware or because it decided
	 * to do so. If zero is a valid response, we will add a new
	 * field that indicates whether the request was handled.
	 */
	if (!ibv_cmd_get_context(&context->ibv_ctx, &req->ibv_req,
				 offsetof(struct mlx5_alloc_ucontext, lib_caps),
				 &resp->ibv_resp, resp_len))
		return 0;

	return ibv_cmd_get_context(&context->ibv_ctx, &req->ibv_req,
				   offsetof(struct mlx5_alloc_ucontext,
					    cqe_version),
				   &resp->ibv_resp, resp_len);
}

static int mlx5_map_internal_clock(struct mlx5_device *mdev,
				   struct ibv_context *ibv_ctx)
{
	struct mlx5_context *context = to_mctx(ibv_ctx);
	void *hca_clock_page;
	off_t offset = 0;

	set_command(MLX5_MMAP_GET_CORE_CLOCK_CMD, &offset);
	hca_clock_page = mmap(NULL, mdev->page_size,
			      PROT_READ, MAP_SHARED, ibv_ctx->cmd_fd,
			      mdev->page_size * offset);

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

int mlx5dv_query_device(struct ibv_context *ctx_in,
			 struct mlx5dv_context *attrs_out)
{
	struct mlx5_context *mctx = to_mctx(ctx_in);
	uint64_t comp_mask_out = 0;

	attrs_out->version   = 0;
	attrs_out->flags     = 0;

	if (mctx->cqe_version == MLX5_CQE_VERSION_V1)
		attrs_out->flags |= MLX5DV_CONTEXT_FLAGS_CQE_V1;

	if (mctx->vendor_cap_flags & MLX5_VENDOR_CAP_FLAGS_MPW)
		attrs_out->flags |= MLX5DV_CONTEXT_FLAGS_MPW;

	if (attrs_out->comp_mask & MLX5DV_CONTEXT_MASK_CQE_COMPRESION) {
		attrs_out->cqe_comp_caps = mctx->cqe_comp_caps;
		comp_mask_out |= MLX5DV_CONTEXT_MASK_CQE_COMPRESION;
	}

	attrs_out->comp_mask = comp_mask_out;

	return 0;
}

static int mlx5dv_get_qp(struct ibv_qp *qp_in,
			 struct mlx5dv_qp *qp_out)
{
	struct mlx5_qp *mqp = to_mqp(qp_in);

	qp_out->comp_mask = 0;
	qp_out->dbrec     = mqp->db;

	if (mqp->sq_buf_size)
		/* IBV_QPT_RAW_PACKET */
		qp_out->sq.buf = (void *)((uintptr_t)mqp->sq_buf.buf);
	else
		qp_out->sq.buf = (void *)((uintptr_t)mqp->buf.buf + mqp->sq.offset);
	qp_out->sq.wqe_cnt = mqp->sq.wqe_cnt;
	qp_out->sq.stride  = 1 << mqp->sq.wqe_shift;

	qp_out->rq.buf     = (void *)((uintptr_t)mqp->buf.buf + mqp->rq.offset);
	qp_out->rq.wqe_cnt = mqp->rq.wqe_cnt;
	qp_out->rq.stride  = 1 << mqp->rq.wqe_shift;

	qp_out->bf.reg    = mqp->bf->reg;

	if (mqp->bf->uuarn > 0)
		qp_out->bf.size = mqp->bf->buf_size;
	else
		qp_out->bf.size = 0;

	return 0;
}

static int mlx5dv_get_cq(struct ibv_cq *cq_in,
			 struct mlx5dv_cq *cq_out)
{
	struct mlx5_cq *mcq = to_mcq(cq_in);
	struct mlx5_context *mctx = to_mctx(cq_in->context);

	cq_out->comp_mask = 0;
	cq_out->cqn       = mcq->cqn;
	cq_out->cqe_cnt   = mcq->ibv_cq.cqe + 1;
	cq_out->cqe_size  = mcq->cqe_sz;
	cq_out->buf       = mcq->active_buf->buf;
	cq_out->dbrec     = mcq->dbrec;
	cq_out->uar	  = mctx->uar;

	mcq->flags	 |= MLX5_CQ_FLAGS_DV_OWNED;

	return 0;
}

static int mlx5dv_get_rwq(struct ibv_wq *wq_in,
			  struct mlx5dv_rwq *rwq_out)
{
	struct mlx5_rwq *mrwq = to_mrwq(wq_in);

	rwq_out->comp_mask = 0;
	rwq_out->buf       = mrwq->pbuff;
	rwq_out->dbrec     = mrwq->recv_db;
	rwq_out->wqe_cnt   = mrwq->rq.wqe_cnt;
	rwq_out->stride    = 1 << mrwq->rq.wqe_shift;

	return 0;
}

static int mlx5dv_get_srq(struct ibv_srq *srq_in,
			  struct mlx5dv_srq *srq_out)
{
	struct mlx5_srq *msrq;

	msrq = container_of(srq_in, struct mlx5_srq, vsrq.srq);

	srq_out->comp_mask = 0;
	srq_out->buf       = msrq->buf.buf;
	srq_out->dbrec     = msrq->db;
	srq_out->stride    = 1 << msrq->wqe_shift;
	srq_out->head      = msrq->head;
	srq_out->tail      = msrq->tail;

	return 0;
}

int mlx5dv_init_obj(struct mlx5dv_obj *obj, uint64_t obj_type)
{
	int ret = 0;

	if (obj_type & MLX5DV_OBJ_QP)
		ret = mlx5dv_get_qp(obj->qp.in, obj->qp.out);
	if (!ret && (obj_type & MLX5DV_OBJ_CQ))
		ret = mlx5dv_get_cq(obj->cq.in, obj->cq.out);
	if (!ret && (obj_type & MLX5DV_OBJ_SRQ))
		ret = mlx5dv_get_srq(obj->srq.in, obj->srq.out);
	if (!ret && (obj_type & MLX5DV_OBJ_RWQ))
		ret = mlx5dv_get_rwq(obj->rwq.in, obj->rwq.out);

	return ret;
}

static void adjust_uar_info(struct mlx5_device *mdev,
			    struct mlx5_context *context,
			    struct mlx5_alloc_ucontext_resp resp)
{
	if (!resp.log_uar_size && !resp.num_uars_per_page) {
		/* old kernel */
		context->uar_size = mdev->page_size;
		context->num_uars_per_page = 1;
		return;
	}

	context->uar_size = 1 << resp.log_uar_size;
	context->num_uars_per_page = resp.num_uars_per_page;
}

static int mlx5_init_context(struct verbs_device *vdev,
			     struct ibv_context *ctx, int cmd_fd)
{
	struct mlx5_context	       *context;
	struct mlx5_alloc_ucontext	req;
	struct mlx5_alloc_ucontext_resp resp;
	int				i;
	int				page_size;
	int				tot_uuars;
	int				low_lat_uuars;
	int				gross_uuars;
	int				j;
	off_t				offset;
	struct mlx5_device	       *mdev;
	struct verbs_context	       *v_ctx;
	struct ibv_port_attr		port_attr;
	struct ibv_device_attr_ex	device_attr;
	int				k;
	int				bfi;
	int				num_sys_page_map;

	mdev = to_mdev(&vdev->device);
	v_ctx = verbs_get_ctx(ctx);
	page_size = mdev->page_size;
	mlx5_single_threaded = single_threaded_app();

	context = to_mctx(ctx);
	context->ibv_ctx.cmd_fd = cmd_fd;

	open_debug_file(context);
	set_debug_mask();
	set_freeze_on_error();
	if (gethostname(context->hostname, sizeof(context->hostname)))
		strcpy(context->hostname, "host_unknown");

	tot_uuars = get_total_uuars(page_size);
	if (tot_uuars < 0) {
		errno = -tot_uuars;
		goto err_free;
	}

	low_lat_uuars = get_num_low_lat_uuars(tot_uuars);
	if (low_lat_uuars < 0) {
		errno = -low_lat_uuars;
		goto err_free;
	}

	if (low_lat_uuars > tot_uuars - 1) {
		errno = ENOMEM;
		goto err_free;
	}

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	req.total_num_uuars = tot_uuars;
	req.num_low_latency_uuars = low_lat_uuars;
	req.cqe_version = MLX5_CQE_VERSION_V1;
	req.lib_caps |= MLX5_LIB_CAP_4K_UAR;

	if (mlx5_cmd_get_context(context, &req, sizeof(req), &resp,
				 sizeof(resp)))
		goto err_free;

	context->max_num_qps		= resp.qp_tab_size;
	context->bf_reg_size		= resp.bf_reg_size;
	context->tot_uuars		= resp.tot_uuars;
	context->low_lat_uuars		= low_lat_uuars;
	context->cache_line_size	= resp.cache_line_size;
	context->max_sq_desc_sz = resp.max_sq_desc_sz;
	context->max_rq_desc_sz = resp.max_rq_desc_sz;
	context->max_send_wqebb	= resp.max_send_wqebb;
	context->num_ports	= resp.num_ports;
	context->max_recv_wr	= resp.max_recv_wr;
	context->max_srq_recv_wr = resp.max_srq_recv_wr;

	context->cqe_version = resp.cqe_version;
	if (context->cqe_version) {
		if (context->cqe_version == MLX5_CQE_VERSION_V1)
			mlx5_ctx_ops.poll_cq = mlx5_poll_cq_v1;
		else
			goto err_free;
	}

	adjust_uar_info(mdev, context, resp);

	gross_uuars = context->tot_uuars / MLX5_NUM_NON_FP_BFREGS_PER_UAR * NUM_BFREGS_PER_UAR;
	context->bfs = calloc(gross_uuars, sizeof(*context->bfs));
	if (!context->bfs) {
		errno = ENOMEM;
		goto err_free;
	}

	context->cmds_supp_uhw = resp.cmds_supp_uhw;
	context->vendor_cap_flags = 0;

	pthread_mutex_init(&context->qp_table_mutex, NULL);
	pthread_mutex_init(&context->srq_table_mutex, NULL);
	pthread_mutex_init(&context->uidx_table_mutex, NULL);
	for (i = 0; i < MLX5_QP_TABLE_SIZE; ++i)
		context->qp_table[i].refcnt = 0;

	for (i = 0; i < MLX5_QP_TABLE_SIZE; ++i)
		context->uidx_table[i].refcnt = 0;

	context->db_list = NULL;

	pthread_mutex_init(&context->db_list_mutex, NULL);

	num_sys_page_map = context->tot_uuars / (context->num_uars_per_page * MLX5_NUM_NON_FP_BFREGS_PER_UAR);
	for (i = 0; i < num_sys_page_map; ++i) {
		offset = 0;
		set_command(MLX5_MMAP_GET_REGULAR_PAGES_CMD, &offset);
		set_index(i, &offset);
		context->uar[i] = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED,
				       cmd_fd, page_size * offset);
		if (context->uar[i] == MAP_FAILED) {
			context->uar[i] = NULL;
			goto err_free_bf;
		}
	}

	for (i = 0; i < num_sys_page_map; i++) {
		for (j = 0; j < context->num_uars_per_page; j++) {
			for (k = 0; k < NUM_BFREGS_PER_UAR; k++) {
				bfi = (i * context->num_uars_per_page + j) * NUM_BFREGS_PER_UAR + k;
				context->bfs[bfi].reg = context->uar[i] + MLX5_ADAPTER_PAGE_SIZE * j +
							MLX5_BF_OFFSET + k * context->bf_reg_size;
				context->bfs[bfi].need_lock = need_uuar_lock(context, bfi);
				mlx5_spinlock_init(&context->bfs[bfi].lock);
				context->bfs[bfi].offset = 0;
				if (bfi)
					context->bfs[bfi].buf_size = context->bf_reg_size / 2;
				context->bfs[bfi].uuarn = bfi;
			}
		}
	}
	context->hca_core_clock = NULL;
	if (resp.response_length + sizeof(resp.ibv_resp) >=
	    offsetof(struct mlx5_alloc_ucontext_resp, hca_core_clock_offset) +
	    sizeof(resp.hca_core_clock_offset) &&
	    resp.comp_mask & MLX5_IB_ALLOC_UCONTEXT_RESP_MASK_CORE_CLOCK_OFFSET) {
		context->core_clock.offset = resp.hca_core_clock_offset;
		mlx5_map_internal_clock(mdev, ctx);
	}

	mlx5_spinlock_init(&context->lock32);

	context->prefer_bf = get_always_bf();
	context->shut_up_bf = get_shut_up_bf();
	mlx5_read_env(&vdev->device, context);

	mlx5_spinlock_init(&context->hugetlb_lock);
	TAILQ_INIT(&context->hugetlb_list);

	context->ibv_ctx.ops = mlx5_ctx_ops;

	verbs_set_ctx_op(v_ctx, create_qp_ex, mlx5_create_qp_ex);
	verbs_set_ctx_op(v_ctx, open_xrcd, mlx5_open_xrcd);
	verbs_set_ctx_op(v_ctx, close_xrcd, mlx5_close_xrcd);
	verbs_set_ctx_op(v_ctx, create_srq_ex, mlx5_create_srq_ex);
	verbs_set_ctx_op(v_ctx, get_srq_num, mlx5_get_srq_num);
	verbs_set_ctx_op(v_ctx, query_device_ex, mlx5_query_device_ex);
	verbs_set_ctx_op(v_ctx, query_rt_values, mlx5_query_rt_values);
	verbs_set_ctx_op(v_ctx, ibv_create_flow, ibv_cmd_create_flow);
	verbs_set_ctx_op(v_ctx, ibv_destroy_flow, ibv_cmd_destroy_flow);
	verbs_set_ctx_op(v_ctx, create_cq_ex, mlx5_create_cq_ex);
	verbs_set_ctx_op(v_ctx, create_wq, mlx5_create_wq);
	verbs_set_ctx_op(v_ctx, modify_wq, mlx5_modify_wq);
	verbs_set_ctx_op(v_ctx, destroy_wq, mlx5_destroy_wq);
	verbs_set_ctx_op(v_ctx, create_rwq_ind_table, mlx5_create_rwq_ind_table);
	verbs_set_ctx_op(v_ctx, destroy_rwq_ind_table, mlx5_destroy_rwq_ind_table);

	memset(&device_attr, 0, sizeof(device_attr));
	if (!mlx5_query_device_ex(ctx, NULL, &device_attr,
				  sizeof(struct ibv_device_attr_ex))) {
		context->cached_device_cap_flags =
			device_attr.orig_attr.device_cap_flags;
		context->atomic_cap = device_attr.orig_attr.atomic_cap;
		context->cached_tso_caps = device_attr.tso_caps;
	}

	for (j = 0; j < min(MLX5_MAX_PORTS_NUM, context->num_ports); ++j) {
		memset(&port_attr, 0, sizeof(port_attr));
		if (!mlx5_query_port(ctx, j + 1, &port_attr))
			context->cached_link_layer[j] = port_attr.link_layer;
	}

	return 0;

err_free_bf:
	free(context->bfs);

err_free:
	for (i = 0; i < MLX5_MAX_UARS; ++i) {
		if (context->uar[i])
			munmap(context->uar[i], page_size);
	}
	close_debug_file(context);
	return errno;
}

static void mlx5_cleanup_context(struct verbs_device *device,
				 struct ibv_context *ibctx)
{
	struct mlx5_context *context = to_mctx(ibctx);
	int page_size = to_mdev(ibctx->device)->page_size;
	int i;

	free(context->bfs);
	for (i = 0; i < MLX5_MAX_UARS; ++i) {
		if (context->uar[i])
			munmap(context->uar[i], page_size);
	}
	if (context->hca_core_clock)
		munmap(context->hca_core_clock - context->core_clock.offset,
		       page_size);
	close_debug_file(context);
}

static struct verbs_device_ops mlx5_dev_ops = {
	.init_context = mlx5_init_context,
	.uninit_context = mlx5_cleanup_context,
};

static struct verbs_device *mlx5_driver_init(const char *uverbs_sys_path,
					     int abi_version)
{
	char			value[8];
	struct mlx5_device     *dev;
	unsigned		vendor, device;
	int			i;

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/vendor",
				value, sizeof value) < 0)
		return NULL;
	sscanf(value, "%i", &vendor);

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/device",
				value, sizeof value) < 0)
		return NULL;
	sscanf(value, "%i", &device);

	for (i = 0; i < sizeof hca_table / sizeof hca_table[0]; ++i)
		if (vendor == hca_table[i].vendor &&
		    device == hca_table[i].device)
			goto found;

	return NULL;

found:
	if (abi_version < MLX5_UVERBS_MIN_ABI_VERSION ||
	    abi_version > MLX5_UVERBS_MAX_ABI_VERSION) {
		fprintf(stderr, PFX "Fatal: ABI version %d of %s is not supported "
			"(min supported %d, max supported %d)\n",
			abi_version, uverbs_sys_path,
			MLX5_UVERBS_MIN_ABI_VERSION,
			MLX5_UVERBS_MAX_ABI_VERSION);
		return NULL;
	}

	dev = calloc(1, sizeof *dev);
	if (!dev) {
		fprintf(stderr, PFX "Fatal: couldn't allocate device for %s\n",
			uverbs_sys_path);
		return NULL;
	}

	dev->page_size   = sysconf(_SC_PAGESIZE);
	dev->driver_abi_ver = abi_version;

	dev->verbs_dev.ops = &mlx5_dev_ops;
	dev->verbs_dev.sz = sizeof(*dev);
	dev->verbs_dev.size_of_context = sizeof(struct mlx5_context) -
		sizeof(struct ibv_context);

	return &dev->verbs_dev;
}

static __attribute__((constructor)) void mlx5_register_driver(void)
{
	verbs_register_driver("mlx5", mlx5_driver_init);
}
