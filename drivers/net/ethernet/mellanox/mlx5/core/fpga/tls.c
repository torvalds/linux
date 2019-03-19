/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <linux/mlx5/device.h>
#include "fpga/tls.h"
#include "fpga/cmd.h"
#include "fpga/sdk.h"
#include "fpga/core.h"
#include "accel/tls.h"

struct mlx5_fpga_tls_command_context;

typedef void (*mlx5_fpga_tls_command_complete)
	(struct mlx5_fpga_conn *conn, struct mlx5_fpga_device *fdev,
	 struct mlx5_fpga_tls_command_context *ctx,
	 struct mlx5_fpga_dma_buf *resp);

struct mlx5_fpga_tls_command_context {
	struct list_head list;
	/* There is no guarantee on the order between the TX completion
	 * and the command response.
	 * The TX completion is going to touch cmd->buf even in
	 * the case of successful transmission.
	 * So instead of requiring separate allocations for cmd
	 * and cmd->buf we've decided to use a reference counter
	 */
	refcount_t ref;
	struct mlx5_fpga_dma_buf buf;
	mlx5_fpga_tls_command_complete complete;
};

static void
mlx5_fpga_tls_put_command_ctx(struct mlx5_fpga_tls_command_context *ctx)
{
	if (refcount_dec_and_test(&ctx->ref))
		kfree(ctx);
}

static void mlx5_fpga_tls_cmd_complete(struct mlx5_fpga_device *fdev,
				       struct mlx5_fpga_dma_buf *resp)
{
	struct mlx5_fpga_conn *conn = fdev->tls->conn;
	struct mlx5_fpga_tls_command_context *ctx;
	struct mlx5_fpga_tls *tls = fdev->tls;
	unsigned long flags;

	spin_lock_irqsave(&tls->pending_cmds_lock, flags);
	ctx = list_first_entry(&tls->pending_cmds,
			       struct mlx5_fpga_tls_command_context, list);
	list_del(&ctx->list);
	spin_unlock_irqrestore(&tls->pending_cmds_lock, flags);
	ctx->complete(conn, fdev, ctx, resp);
}

static void mlx5_fpga_cmd_send_complete(struct mlx5_fpga_conn *conn,
					struct mlx5_fpga_device *fdev,
					struct mlx5_fpga_dma_buf *buf,
					u8 status)
{
	struct mlx5_fpga_tls_command_context *ctx =
	    container_of(buf, struct mlx5_fpga_tls_command_context, buf);

	mlx5_fpga_tls_put_command_ctx(ctx);

	if (unlikely(status))
		mlx5_fpga_tls_cmd_complete(fdev, NULL);
}

static void mlx5_fpga_tls_cmd_send(struct mlx5_fpga_device *fdev,
				   struct mlx5_fpga_tls_command_context *cmd,
				   mlx5_fpga_tls_command_complete complete)
{
	struct mlx5_fpga_tls *tls = fdev->tls;
	unsigned long flags;
	int ret;

	refcount_set(&cmd->ref, 2);
	cmd->complete = complete;
	cmd->buf.complete = mlx5_fpga_cmd_send_complete;

	spin_lock_irqsave(&tls->pending_cmds_lock, flags);
	/* mlx5_fpga_sbu_conn_sendmsg is called under pending_cmds_lock
	 * to make sure commands are inserted to the tls->pending_cmds list
	 * and the command QP in the same order.
	 */
	ret = mlx5_fpga_sbu_conn_sendmsg(tls->conn, &cmd->buf);
	if (likely(!ret))
		list_add_tail(&cmd->list, &tls->pending_cmds);
	else
		complete(tls->conn, fdev, cmd, NULL);
	spin_unlock_irqrestore(&tls->pending_cmds_lock, flags);
}

/* Start of context identifiers range (inclusive) */
#define SWID_START	0
/* End of context identifiers range (exclusive) */
#define SWID_END	BIT(24)

static int mlx5_fpga_tls_alloc_swid(struct idr *idr, spinlock_t *idr_spinlock,
				    void *ptr)
{
	unsigned long flags;
	int ret;

	/* TLS metadata format is 1 byte for syndrome followed
	 * by 3 bytes of swid (software ID)
	 * swid must not exceed 3 bytes.
	 * See tls_rxtx.c:insert_pet() for details
	 */
	BUILD_BUG_ON((SWID_END - 1) & 0xFF000000);

	idr_preload(GFP_KERNEL);
	spin_lock_irqsave(idr_spinlock, flags);
	ret = idr_alloc(idr, ptr, SWID_START, SWID_END, GFP_ATOMIC);
	spin_unlock_irqrestore(idr_spinlock, flags);
	idr_preload_end();

	return ret;
}

static void mlx5_fpga_tls_release_swid(struct idr *idr,
				       spinlock_t *idr_spinlock, u32 swid)
{
	unsigned long flags;

	spin_lock_irqsave(idr_spinlock, flags);
	idr_remove(idr, swid);
	spin_unlock_irqrestore(idr_spinlock, flags);
}

static void mlx_tls_kfree_complete(struct mlx5_fpga_conn *conn,
				   struct mlx5_fpga_device *fdev,
				   struct mlx5_fpga_dma_buf *buf, u8 status)
{
	kfree(buf);
}

struct mlx5_teardown_stream_context {
	struct mlx5_fpga_tls_command_context cmd;
	u32 swid;
};

static void
mlx5_fpga_tls_teardown_completion(struct mlx5_fpga_conn *conn,
				  struct mlx5_fpga_device *fdev,
				  struct mlx5_fpga_tls_command_context *cmd,
				  struct mlx5_fpga_dma_buf *resp)
{
	struct mlx5_teardown_stream_context *ctx =
		    container_of(cmd, struct mlx5_teardown_stream_context, cmd);

	if (resp) {
		u32 syndrome = MLX5_GET(tls_resp, resp->sg[0].data, syndrome);

		if (syndrome)
			mlx5_fpga_err(fdev,
				      "Teardown stream failed with syndrome = %d",
				      syndrome);
		else if (MLX5_GET(tls_cmd, cmd->buf.sg[0].data, direction_sx))
			mlx5_fpga_tls_release_swid(&fdev->tls->tx_idr,
						   &fdev->tls->tx_idr_spinlock,
						   ctx->swid);
		else
			mlx5_fpga_tls_release_swid(&fdev->tls->rx_idr,
						   &fdev->tls->rx_idr_spinlock,
						   ctx->swid);
	}
	mlx5_fpga_tls_put_command_ctx(cmd);
}

static void mlx5_fpga_tls_flow_to_cmd(void *flow, void *cmd)
{
	memcpy(MLX5_ADDR_OF(tls_cmd, cmd, src_port), flow,
	       MLX5_BYTE_OFF(tls_flow, ipv6));

	MLX5_SET(tls_cmd, cmd, ipv6, MLX5_GET(tls_flow, flow, ipv6));
	MLX5_SET(tls_cmd, cmd, direction_sx,
		 MLX5_GET(tls_flow, flow, direction_sx));
}

int mlx5_fpga_tls_resync_rx(struct mlx5_core_dev *mdev, u32 handle, u32 seq,
			    u64 rcd_sn)
{
	struct mlx5_fpga_dma_buf *buf;
	int size = sizeof(*buf) + MLX5_TLS_COMMAND_SIZE;
	void *flow;
	void *cmd;
	int ret;

	rcu_read_lock();
	flow = idr_find(&mdev->fpga->tls->rx_idr, ntohl(handle));
	rcu_read_unlock();

	if (!flow) {
		WARN_ONCE(1, "Received NULL pointer for handle\n");
		return -EINVAL;
	}

	buf = kzalloc(size, GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	cmd = (buf + 1);

	mlx5_fpga_tls_flow_to_cmd(flow, cmd);

	MLX5_SET(tls_cmd, cmd, swid, ntohl(handle));
	MLX5_SET64(tls_cmd, cmd, tls_rcd_sn, be64_to_cpu(rcd_sn));
	MLX5_SET(tls_cmd, cmd, tcp_sn, seq);
	MLX5_SET(tls_cmd, cmd, command_type, CMD_RESYNC_RX);

	buf->sg[0].data = cmd;
	buf->sg[0].size = MLX5_TLS_COMMAND_SIZE;
	buf->complete = mlx_tls_kfree_complete;

	ret = mlx5_fpga_sbu_conn_sendmsg(mdev->fpga->tls->conn, buf);
	if (ret < 0)
		kfree(buf);

	return ret;
}

static void mlx5_fpga_tls_send_teardown_cmd(struct mlx5_core_dev *mdev,
					    void *flow, u32 swid, gfp_t flags)
{
	struct mlx5_teardown_stream_context *ctx;
	struct mlx5_fpga_dma_buf *buf;
	void *cmd;

	ctx = kzalloc(sizeof(*ctx) + MLX5_TLS_COMMAND_SIZE, flags);
	if (!ctx)
		return;

	buf = &ctx->cmd.buf;
	cmd = (ctx + 1);
	MLX5_SET(tls_cmd, cmd, command_type, CMD_TEARDOWN_STREAM);
	MLX5_SET(tls_cmd, cmd, swid, swid);

	mlx5_fpga_tls_flow_to_cmd(flow, cmd);
	kfree(flow);

	buf->sg[0].data = cmd;
	buf->sg[0].size = MLX5_TLS_COMMAND_SIZE;

	ctx->swid = swid;
	mlx5_fpga_tls_cmd_send(mdev->fpga, &ctx->cmd,
			       mlx5_fpga_tls_teardown_completion);
}

void mlx5_fpga_tls_del_flow(struct mlx5_core_dev *mdev, u32 swid,
			    gfp_t flags, bool direction_sx)
{
	struct mlx5_fpga_tls *tls = mdev->fpga->tls;
	void *flow;

	rcu_read_lock();
	if (direction_sx)
		flow = idr_find(&tls->tx_idr, swid);
	else
		flow = idr_find(&tls->rx_idr, swid);

	rcu_read_unlock();

	if (!flow) {
		mlx5_fpga_err(mdev->fpga, "No flow information for swid %u\n",
			      swid);
		return;
	}

	mlx5_fpga_tls_send_teardown_cmd(mdev, flow, swid, flags);
}

enum mlx5_fpga_setup_stream_status {
	MLX5_FPGA_CMD_PENDING,
	MLX5_FPGA_CMD_SEND_FAILED,
	MLX5_FPGA_CMD_RESPONSE_RECEIVED,
	MLX5_FPGA_CMD_ABANDONED,
};

struct mlx5_setup_stream_context {
	struct mlx5_fpga_tls_command_context cmd;
	atomic_t status;
	u32 syndrome;
	struct completion comp;
};

static void
mlx5_fpga_tls_setup_completion(struct mlx5_fpga_conn *conn,
			       struct mlx5_fpga_device *fdev,
			       struct mlx5_fpga_tls_command_context *cmd,
			       struct mlx5_fpga_dma_buf *resp)
{
	struct mlx5_setup_stream_context *ctx =
	    container_of(cmd, struct mlx5_setup_stream_context, cmd);
	int status = MLX5_FPGA_CMD_SEND_FAILED;
	void *tls_cmd = ctx + 1;

	/* If we failed to send to command resp == NULL */
	if (resp) {
		ctx->syndrome = MLX5_GET(tls_resp, resp->sg[0].data, syndrome);
		status = MLX5_FPGA_CMD_RESPONSE_RECEIVED;
	}

	status = atomic_xchg_release(&ctx->status, status);
	if (likely(status != MLX5_FPGA_CMD_ABANDONED)) {
		complete(&ctx->comp);
		return;
	}

	mlx5_fpga_err(fdev, "Command was abandoned, syndrome = %u\n",
		      ctx->syndrome);

	if (!ctx->syndrome) {
		/* The process was killed while waiting for the context to be
		 * added, and the add completed successfully.
		 * We need to destroy the HW context, and we can't can't reuse
		 * the command context because we might not have received
		 * the tx completion yet.
		 */
		mlx5_fpga_tls_del_flow(fdev->mdev,
				       MLX5_GET(tls_cmd, tls_cmd, swid),
				       GFP_ATOMIC,
				       MLX5_GET(tls_cmd, tls_cmd,
						direction_sx));
	}

	mlx5_fpga_tls_put_command_ctx(cmd);
}

static int mlx5_fpga_tls_setup_stream_cmd(struct mlx5_core_dev *mdev,
					  struct mlx5_setup_stream_context *ctx)
{
	struct mlx5_fpga_dma_buf *buf;
	void *cmd = ctx + 1;
	int status, ret = 0;

	buf = &ctx->cmd.buf;
	buf->sg[0].data = cmd;
	buf->sg[0].size = MLX5_TLS_COMMAND_SIZE;
	MLX5_SET(tls_cmd, cmd, command_type, CMD_SETUP_STREAM);

	init_completion(&ctx->comp);
	atomic_set(&ctx->status, MLX5_FPGA_CMD_PENDING);
	ctx->syndrome = -1;

	mlx5_fpga_tls_cmd_send(mdev->fpga, &ctx->cmd,
			       mlx5_fpga_tls_setup_completion);
	wait_for_completion_killable(&ctx->comp);

	status = atomic_xchg_acquire(&ctx->status, MLX5_FPGA_CMD_ABANDONED);
	if (unlikely(status == MLX5_FPGA_CMD_PENDING))
	/* ctx is going to be released in mlx5_fpga_tls_setup_completion */
		return -EINTR;

	if (unlikely(ctx->syndrome))
		ret = -ENOMEM;

	mlx5_fpga_tls_put_command_ctx(&ctx->cmd);
	return ret;
}

static void mlx5_fpga_tls_hw_qp_recv_cb(void *cb_arg,
					struct mlx5_fpga_dma_buf *buf)
{
	struct mlx5_fpga_device *fdev = (struct mlx5_fpga_device *)cb_arg;

	mlx5_fpga_tls_cmd_complete(fdev, buf);
}

bool mlx5_fpga_is_tls_device(struct mlx5_core_dev *mdev)
{
	if (!mdev->fpga || !MLX5_CAP_GEN(mdev, fpga))
		return false;

	if (MLX5_CAP_FPGA(mdev, ieee_vendor_id) !=
	    MLX5_FPGA_CAP_SANDBOX_VENDOR_ID_MLNX)
		return false;

	if (MLX5_CAP_FPGA(mdev, sandbox_product_id) !=
	    MLX5_FPGA_CAP_SANDBOX_PRODUCT_ID_TLS)
		return false;

	if (MLX5_CAP_FPGA(mdev, sandbox_product_version) != 0)
		return false;

	return true;
}

static int mlx5_fpga_tls_get_caps(struct mlx5_fpga_device *fdev,
				  u32 *p_caps)
{
	int err, cap_size = MLX5_ST_SZ_BYTES(tls_extended_cap);
	u32 caps = 0;
	void *buf;

	buf = kzalloc(cap_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	err = mlx5_fpga_get_sbu_caps(fdev, cap_size, buf);
	if (err)
		goto out;

	if (MLX5_GET(tls_extended_cap, buf, tx))
		caps |= MLX5_ACCEL_TLS_TX;
	if (MLX5_GET(tls_extended_cap, buf, rx))
		caps |= MLX5_ACCEL_TLS_RX;
	if (MLX5_GET(tls_extended_cap, buf, tls_v12))
		caps |= MLX5_ACCEL_TLS_V12;
	if (MLX5_GET(tls_extended_cap, buf, tls_v13))
		caps |= MLX5_ACCEL_TLS_V13;
	if (MLX5_GET(tls_extended_cap, buf, lro))
		caps |= MLX5_ACCEL_TLS_LRO;
	if (MLX5_GET(tls_extended_cap, buf, ipv6))
		caps |= MLX5_ACCEL_TLS_IPV6;

	if (MLX5_GET(tls_extended_cap, buf, aes_gcm_128))
		caps |= MLX5_ACCEL_TLS_AES_GCM128;
	if (MLX5_GET(tls_extended_cap, buf, aes_gcm_256))
		caps |= MLX5_ACCEL_TLS_AES_GCM256;

	*p_caps = caps;
	err = 0;
out:
	kfree(buf);
	return err;
}

int mlx5_fpga_tls_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;
	struct mlx5_fpga_conn_attr init_attr = {0};
	struct mlx5_fpga_conn *conn;
	struct mlx5_fpga_tls *tls;
	int err = 0;

	if (!mlx5_fpga_is_tls_device(mdev) || !fdev)
		return 0;

	tls = kzalloc(sizeof(*tls), GFP_KERNEL);
	if (!tls)
		return -ENOMEM;

	err = mlx5_fpga_tls_get_caps(fdev, &tls->caps);
	if (err)
		goto error;

	if (!(tls->caps & (MLX5_ACCEL_TLS_V12 | MLX5_ACCEL_TLS_AES_GCM128))) {
		err = -ENOTSUPP;
		goto error;
	}

	init_attr.rx_size = SBU_QP_QUEUE_SIZE;
	init_attr.tx_size = SBU_QP_QUEUE_SIZE;
	init_attr.recv_cb = mlx5_fpga_tls_hw_qp_recv_cb;
	init_attr.cb_arg = fdev;
	conn = mlx5_fpga_sbu_conn_create(fdev, &init_attr);
	if (IS_ERR(conn)) {
		err = PTR_ERR(conn);
		mlx5_fpga_err(fdev, "Error creating TLS command connection %d\n",
			      err);
		goto error;
	}

	tls->conn = conn;
	spin_lock_init(&tls->pending_cmds_lock);
	INIT_LIST_HEAD(&tls->pending_cmds);

	idr_init(&tls->tx_idr);
	idr_init(&tls->rx_idr);
	spin_lock_init(&tls->tx_idr_spinlock);
	spin_lock_init(&tls->rx_idr_spinlock);
	fdev->tls = tls;
	return 0;

error:
	kfree(tls);
	return err;
}

void mlx5_fpga_tls_cleanup(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;

	if (!fdev || !fdev->tls)
		return;

	mlx5_fpga_sbu_conn_destroy(fdev->tls->conn);
	kfree(fdev->tls);
	fdev->tls = NULL;
}

static void mlx5_fpga_tls_set_aes_gcm128_ctx(void *cmd,
					     struct tls_crypto_info *info,
					     __be64 *rcd_sn)
{
	struct tls12_crypto_info_aes_gcm_128 *crypto_info =
	    (struct tls12_crypto_info_aes_gcm_128 *)info;

	memcpy(MLX5_ADDR_OF(tls_cmd, cmd, tls_rcd_sn), crypto_info->rec_seq,
	       TLS_CIPHER_AES_GCM_128_REC_SEQ_SIZE);

	memcpy(MLX5_ADDR_OF(tls_cmd, cmd, tls_implicit_iv),
	       crypto_info->salt, TLS_CIPHER_AES_GCM_128_SALT_SIZE);
	memcpy(MLX5_ADDR_OF(tls_cmd, cmd, encryption_key),
	       crypto_info->key, TLS_CIPHER_AES_GCM_128_KEY_SIZE);

	/* in AES-GCM 128 we need to write the key twice */
	memcpy(MLX5_ADDR_OF(tls_cmd, cmd, encryption_key) +
		   TLS_CIPHER_AES_GCM_128_KEY_SIZE,
	       crypto_info->key, TLS_CIPHER_AES_GCM_128_KEY_SIZE);

	MLX5_SET(tls_cmd, cmd, alg, MLX5_TLS_ALG_AES_GCM_128);
}

static int mlx5_fpga_tls_set_key_material(void *cmd, u32 caps,
					  struct tls_crypto_info *crypto_info)
{
	__be64 rcd_sn;

	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128:
		if (!(caps & MLX5_ACCEL_TLS_AES_GCM128))
			return -EINVAL;
		mlx5_fpga_tls_set_aes_gcm128_ctx(cmd, crypto_info, &rcd_sn);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int _mlx5_fpga_tls_add_flow(struct mlx5_core_dev *mdev, void *flow,
				   struct tls_crypto_info *crypto_info,
				   u32 swid, u32 tcp_sn)
{
	u32 caps = mlx5_fpga_tls_device_caps(mdev);
	struct mlx5_setup_stream_context *ctx;
	int ret = -ENOMEM;
	size_t cmd_size;
	void *cmd;

	cmd_size = MLX5_TLS_COMMAND_SIZE + sizeof(*ctx);
	ctx = kzalloc(cmd_size, GFP_KERNEL);
	if (!ctx)
		goto out;

	cmd = ctx + 1;
	ret = mlx5_fpga_tls_set_key_material(cmd, caps, crypto_info);
	if (ret)
		goto free_ctx;

	mlx5_fpga_tls_flow_to_cmd(flow, cmd);

	MLX5_SET(tls_cmd, cmd, swid, swid);
	MLX5_SET(tls_cmd, cmd, tcp_sn, tcp_sn);

	return mlx5_fpga_tls_setup_stream_cmd(mdev, ctx);

free_ctx:
	kfree(ctx);
out:
	return ret;
}

int mlx5_fpga_tls_add_flow(struct mlx5_core_dev *mdev, void *flow,
			   struct tls_crypto_info *crypto_info,
			   u32 start_offload_tcp_sn, u32 *p_swid,
			   bool direction_sx)
{
	struct mlx5_fpga_tls *tls = mdev->fpga->tls;
	int ret = -ENOMEM;
	u32 swid;

	if (direction_sx)
		ret = mlx5_fpga_tls_alloc_swid(&tls->tx_idr,
					       &tls->tx_idr_spinlock, flow);
	else
		ret = mlx5_fpga_tls_alloc_swid(&tls->rx_idr,
					       &tls->rx_idr_spinlock, flow);

	if (ret < 0)
		return ret;

	swid = ret;
	MLX5_SET(tls_flow, flow, direction_sx, direction_sx ? 1 : 0);

	ret = _mlx5_fpga_tls_add_flow(mdev, flow, crypto_info, swid,
				      start_offload_tcp_sn);
	if (ret && ret != -EINTR)
		goto free_swid;

	*p_swid = swid;
	return 0;
free_swid:
	if (direction_sx)
		mlx5_fpga_tls_release_swid(&tls->tx_idr,
					   &tls->tx_idr_spinlock, swid);
	else
		mlx5_fpga_tls_release_swid(&tls->rx_idr,
					   &tls->rx_idr_spinlock, swid);

	return ret;
}
