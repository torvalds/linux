/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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

#include <linux/rhashtable.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/fs_helpers.h>
#include <linux/mlx5/fs.h>
#include <linux/rbtree.h>

#include "mlx5_core.h"
#include "fs_cmd.h"
#include "fpga/ipsec.h"
#include "fpga/sdk.h"
#include "fpga/core.h"

enum mlx5_fpga_ipsec_cmd_status {
	MLX5_FPGA_IPSEC_CMD_PENDING,
	MLX5_FPGA_IPSEC_CMD_SEND_FAIL,
	MLX5_FPGA_IPSEC_CMD_COMPLETE,
};

struct mlx5_fpga_ipsec_cmd_context {
	struct mlx5_fpga_dma_buf buf;
	enum mlx5_fpga_ipsec_cmd_status status;
	struct mlx5_ifc_fpga_ipsec_cmd_resp resp;
	int status_code;
	struct completion complete;
	struct mlx5_fpga_device *dev;
	struct list_head list; /* Item in pending_cmds */
	u8 command[0];
};

struct mlx5_fpga_esp_xfrm;

struct mlx5_fpga_ipsec_sa_ctx {
	struct rhash_head		hash;
	struct mlx5_ifc_fpga_ipsec_sa	hw_sa;
	struct mlx5_core_dev		*dev;
	struct mlx5_fpga_esp_xfrm	*fpga_xfrm;
};

struct mlx5_fpga_esp_xfrm {
	unsigned int			num_rules;
	struct mlx5_fpga_ipsec_sa_ctx	*sa_ctx;
	struct mutex			lock; /* xfrm lock */
	struct mlx5_accel_esp_xfrm	accel_xfrm;
};

struct mlx5_fpga_ipsec_rule {
	struct rb_node			node;
	struct fs_fte			*fte;
	struct mlx5_fpga_ipsec_sa_ctx	*ctx;
};

static const struct rhashtable_params rhash_sa = {
	/* Keep out "cmd" field from the key as it's
	 * value is not constant during the lifetime
	 * of the key object.
	 */
	.key_len = FIELD_SIZEOF(struct mlx5_fpga_ipsec_sa_ctx, hw_sa) -
		   FIELD_SIZEOF(struct mlx5_ifc_fpga_ipsec_sa_v1, cmd),
	.key_offset = offsetof(struct mlx5_fpga_ipsec_sa_ctx, hw_sa) +
		      FIELD_SIZEOF(struct mlx5_ifc_fpga_ipsec_sa_v1, cmd),
	.head_offset = offsetof(struct mlx5_fpga_ipsec_sa_ctx, hash),
	.automatic_shrinking = true,
	.min_size = 1,
};

struct mlx5_fpga_ipsec {
	struct mlx5_fpga_device *fdev;
	struct list_head pending_cmds;
	spinlock_t pending_cmds_lock; /* Protects pending_cmds */
	u32 caps[MLX5_ST_SZ_DW(ipsec_extended_cap)];
	struct mlx5_fpga_conn *conn;

	struct notifier_block	fs_notifier_ingress_bypass;
	struct notifier_block	fs_notifier_egress;

	/* Map hardware SA           -->  SA context
	 *     (mlx5_fpga_ipsec_sa)       (mlx5_fpga_ipsec_sa_ctx)
	 * We will use this hash to avoid SAs duplication in fpga which
	 * aren't allowed
	 */
	struct rhashtable sa_hash;	/* hw_sa -> mlx5_fpga_ipsec_sa_ctx */
	struct mutex sa_hash_lock;

	/* Tree holding all rules for this fpga device
	 * Key for searching a rule (mlx5_fpga_ipsec_rule) is (ft, id)
	 */
	struct rb_root rules_rb;
	struct mutex rules_rb_lock; /* rules lock */
};

static bool mlx5_fpga_is_ipsec_device(struct mlx5_core_dev *mdev)
{
	if (!mdev->fpga || !MLX5_CAP_GEN(mdev, fpga))
		return false;

	if (MLX5_CAP_FPGA(mdev, ieee_vendor_id) !=
	    MLX5_FPGA_CAP_SANDBOX_VENDOR_ID_MLNX)
		return false;

	if (MLX5_CAP_FPGA(mdev, sandbox_product_id) !=
	    MLX5_FPGA_CAP_SANDBOX_PRODUCT_ID_IPSEC)
		return false;

	return true;
}

static void mlx5_fpga_ipsec_send_complete(struct mlx5_fpga_conn *conn,
					  struct mlx5_fpga_device *fdev,
					  struct mlx5_fpga_dma_buf *buf,
					  u8 status)
{
	struct mlx5_fpga_ipsec_cmd_context *context;

	if (status) {
		context = container_of(buf, struct mlx5_fpga_ipsec_cmd_context,
				       buf);
		mlx5_fpga_warn(fdev, "IPSec command send failed with status %u\n",
			       status);
		context->status = MLX5_FPGA_IPSEC_CMD_SEND_FAIL;
		complete(&context->complete);
	}
}

static inline
int syndrome_to_errno(enum mlx5_ifc_fpga_ipsec_response_syndrome syndrome)
{
	switch (syndrome) {
	case MLX5_FPGA_IPSEC_RESPONSE_SUCCESS:
		return 0;
	case MLX5_FPGA_IPSEC_RESPONSE_SADB_ISSUE:
		return -EEXIST;
	case MLX5_FPGA_IPSEC_RESPONSE_ILLEGAL_REQUEST:
		return -EINVAL;
	case MLX5_FPGA_IPSEC_RESPONSE_WRITE_RESPONSE_ISSUE:
		return -EIO;
	}
	return -EIO;
}

static void mlx5_fpga_ipsec_recv(void *cb_arg, struct mlx5_fpga_dma_buf *buf)
{
	struct mlx5_ifc_fpga_ipsec_cmd_resp *resp = buf->sg[0].data;
	struct mlx5_fpga_ipsec_cmd_context *context;
	enum mlx5_ifc_fpga_ipsec_response_syndrome syndrome;
	struct mlx5_fpga_device *fdev = cb_arg;
	unsigned long flags;

	if (buf->sg[0].size < sizeof(*resp)) {
		mlx5_fpga_warn(fdev, "Short receive from FPGA IPSec: %u < %zu bytes\n",
			       buf->sg[0].size, sizeof(*resp));
		return;
	}

	mlx5_fpga_dbg(fdev, "mlx5_ipsec recv_cb syndrome %08x\n",
		      ntohl(resp->syndrome));

	spin_lock_irqsave(&fdev->ipsec->pending_cmds_lock, flags);
	context = list_first_entry_or_null(&fdev->ipsec->pending_cmds,
					   struct mlx5_fpga_ipsec_cmd_context,
					   list);
	if (context)
		list_del(&context->list);
	spin_unlock_irqrestore(&fdev->ipsec->pending_cmds_lock, flags);

	if (!context) {
		mlx5_fpga_warn(fdev, "Received IPSec offload response without pending command request\n");
		return;
	}
	mlx5_fpga_dbg(fdev, "Handling response for %p\n", context);

	syndrome = ntohl(resp->syndrome);
	context->status_code = syndrome_to_errno(syndrome);
	context->status = MLX5_FPGA_IPSEC_CMD_COMPLETE;
	memcpy(&context->resp, resp, sizeof(*resp));

	if (context->status_code)
		mlx5_fpga_warn(fdev, "IPSec command failed with syndrome %08x\n",
			       syndrome);

	complete(&context->complete);
}

static void *mlx5_fpga_ipsec_cmd_exec(struct mlx5_core_dev *mdev,
				      const void *cmd, int cmd_size)
{
	struct mlx5_fpga_ipsec_cmd_context *context;
	struct mlx5_fpga_device *fdev = mdev->fpga;
	unsigned long flags;
	int res;

	if (!fdev || !fdev->ipsec)
		return ERR_PTR(-EOPNOTSUPP);

	if (cmd_size & 3)
		return ERR_PTR(-EINVAL);

	context = kzalloc(sizeof(*context) + cmd_size, GFP_ATOMIC);
	if (!context)
		return ERR_PTR(-ENOMEM);

	context->status = MLX5_FPGA_IPSEC_CMD_PENDING;
	context->dev = fdev;
	context->buf.complete = mlx5_fpga_ipsec_send_complete;
	init_completion(&context->complete);
	memcpy(&context->command, cmd, cmd_size);
	context->buf.sg[0].size = cmd_size;
	context->buf.sg[0].data = &context->command;

	spin_lock_irqsave(&fdev->ipsec->pending_cmds_lock, flags);
	res = mlx5_fpga_sbu_conn_sendmsg(fdev->ipsec->conn, &context->buf);
	if (!res)
		list_add_tail(&context->list, &fdev->ipsec->pending_cmds);
	spin_unlock_irqrestore(&fdev->ipsec->pending_cmds_lock, flags);

	if (res) {
		mlx5_fpga_warn(fdev, "Failed to send IPSec command: %d\n", res);
		kfree(context);
		return ERR_PTR(res);
	}

	/* Context should be freed by the caller after completion. */
	return context;
}

static int mlx5_fpga_ipsec_cmd_wait(void *ctx)
{
	struct mlx5_fpga_ipsec_cmd_context *context = ctx;
	unsigned long timeout =
		msecs_to_jiffies(MLX5_FPGA_CMD_TIMEOUT_MSEC);
	int res;

	res = wait_for_completion_timeout(&context->complete, timeout);
	if (!res) {
		mlx5_fpga_warn(context->dev, "Failure waiting for IPSec command response\n");
		return -ETIMEDOUT;
	}

	if (context->status == MLX5_FPGA_IPSEC_CMD_COMPLETE)
		res = context->status_code;
	else
		res = -EIO;

	return res;
}

static inline bool is_v2_sadb_supported(struct mlx5_fpga_ipsec *fipsec)
{
	if (MLX5_GET(ipsec_extended_cap, fipsec->caps, v2_command))
		return true;
	return false;
}

static int mlx5_fpga_ipsec_update_hw_sa(struct mlx5_fpga_device *fdev,
					struct mlx5_ifc_fpga_ipsec_sa *hw_sa,
					int opcode)
{
	struct mlx5_core_dev *dev = fdev->mdev;
	struct mlx5_ifc_fpga_ipsec_sa *sa;
	struct mlx5_fpga_ipsec_cmd_context *cmd_context;
	size_t sa_cmd_size;
	int err;

	hw_sa->ipsec_sa_v1.cmd = htonl(opcode);
	if (is_v2_sadb_supported(fdev->ipsec))
		sa_cmd_size = sizeof(*hw_sa);
	else
		sa_cmd_size = sizeof(hw_sa->ipsec_sa_v1);

	cmd_context = (struct mlx5_fpga_ipsec_cmd_context *)
			mlx5_fpga_ipsec_cmd_exec(dev, hw_sa, sa_cmd_size);
	if (IS_ERR(cmd_context))
		return PTR_ERR(cmd_context);

	err = mlx5_fpga_ipsec_cmd_wait(cmd_context);
	if (err)
		goto out;

	sa = (struct mlx5_ifc_fpga_ipsec_sa *)&cmd_context->command;
	if (sa->ipsec_sa_v1.sw_sa_handle != cmd_context->resp.sw_sa_handle) {
		mlx5_fpga_err(fdev, "mismatch SA handle. cmd 0x%08x vs resp 0x%08x\n",
			      ntohl(sa->ipsec_sa_v1.sw_sa_handle),
			      ntohl(cmd_context->resp.sw_sa_handle));
		err = -EIO;
	}

out:
	kfree(cmd_context);
	return err;
}

u32 mlx5_fpga_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;
	u32 ret = 0;

	if (mlx5_fpga_is_ipsec_device(mdev)) {
		ret |= MLX5_ACCEL_IPSEC_CAP_DEVICE;
		ret |= MLX5_ACCEL_IPSEC_CAP_REQUIRED_METADATA;
	} else {
		return ret;
	}

	if (!fdev->ipsec)
		return ret;

	if (MLX5_GET(ipsec_extended_cap, fdev->ipsec->caps, esp))
		ret |= MLX5_ACCEL_IPSEC_CAP_ESP;

	if (MLX5_GET(ipsec_extended_cap, fdev->ipsec->caps, ipv6))
		ret |= MLX5_ACCEL_IPSEC_CAP_IPV6;

	if (MLX5_GET(ipsec_extended_cap, fdev->ipsec->caps, lso))
		ret |= MLX5_ACCEL_IPSEC_CAP_LSO;

	if (MLX5_GET(ipsec_extended_cap, fdev->ipsec->caps, rx_no_trailer))
		ret |= MLX5_ACCEL_IPSEC_CAP_RX_NO_TRAILER;

	if (MLX5_GET(ipsec_extended_cap, fdev->ipsec->caps, esn)) {
		ret |= MLX5_ACCEL_IPSEC_CAP_ESN;
		ret |= MLX5_ACCEL_IPSEC_CAP_TX_IV_IS_ESN;
	}

	return ret;
}

unsigned int mlx5_fpga_ipsec_counters_count(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;

	if (!fdev || !fdev->ipsec)
		return 0;

	return MLX5_GET(ipsec_extended_cap, fdev->ipsec->caps,
			number_of_ipsec_counters);
}

int mlx5_fpga_ipsec_counters_read(struct mlx5_core_dev *mdev, u64 *counters,
				  unsigned int counters_count)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;
	unsigned int i;
	__be32 *data;
	u32 count;
	u64 addr;
	int ret;

	if (!fdev || !fdev->ipsec)
		return 0;

	addr = (u64)MLX5_GET(ipsec_extended_cap, fdev->ipsec->caps,
			     ipsec_counters_addr_low) +
	       ((u64)MLX5_GET(ipsec_extended_cap, fdev->ipsec->caps,
			     ipsec_counters_addr_high) << 32);

	count = mlx5_fpga_ipsec_counters_count(mdev);

	data = kzalloc(array3_size(sizeof(*data), count, 2), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mlx5_fpga_mem_read(fdev, count * sizeof(u64), addr, data,
				 MLX5_FPGA_ACCESS_TYPE_DONTCARE);
	if (ret < 0) {
		mlx5_fpga_err(fdev, "Failed to read IPSec counters from HW: %d\n",
			      ret);
		goto out;
	}
	ret = 0;

	if (count > counters_count)
		count = counters_count;

	/* Each counter is low word, then high. But each word is big-endian */
	for (i = 0; i < count; i++)
		counters[i] = (u64)ntohl(data[i * 2]) |
			      ((u64)ntohl(data[i * 2 + 1]) << 32);

out:
	kfree(data);
	return ret;
}

static int mlx5_fpga_ipsec_set_caps(struct mlx5_core_dev *mdev, u32 flags)
{
	struct mlx5_fpga_ipsec_cmd_context *context;
	struct mlx5_ifc_fpga_ipsec_cmd_cap cmd = {0};
	int err;

	cmd.cmd = htonl(MLX5_FPGA_IPSEC_CMD_OP_SET_CAP);
	cmd.flags = htonl(flags);
	context = mlx5_fpga_ipsec_cmd_exec(mdev, &cmd, sizeof(cmd));
	if (IS_ERR(context))
		return PTR_ERR(context);

	err = mlx5_fpga_ipsec_cmd_wait(context);
	if (err)
		goto out;

	if ((context->resp.flags & cmd.flags) != cmd.flags) {
		mlx5_fpga_err(context->dev, "Failed to set capabilities. cmd 0x%08x vs resp 0x%08x\n",
			      cmd.flags,
			      context->resp.flags);
		err = -EIO;
	}

out:
	kfree(context);
	return err;
}

static int mlx5_fpga_ipsec_enable_supported_caps(struct mlx5_core_dev *mdev)
{
	u32 dev_caps = mlx5_fpga_ipsec_device_caps(mdev);
	u32 flags = 0;

	if (dev_caps & MLX5_ACCEL_IPSEC_CAP_RX_NO_TRAILER)
		flags |= MLX5_FPGA_IPSEC_CAP_NO_TRAILER;

	return mlx5_fpga_ipsec_set_caps(mdev, flags);
}

static void
mlx5_fpga_ipsec_build_hw_xfrm(struct mlx5_core_dev *mdev,
			      const struct mlx5_accel_esp_xfrm_attrs *xfrm_attrs,
			      struct mlx5_ifc_fpga_ipsec_sa *hw_sa)
{
	const struct aes_gcm_keymat *aes_gcm = &xfrm_attrs->keymat.aes_gcm;

	/* key */
	memcpy(&hw_sa->ipsec_sa_v1.key_enc, aes_gcm->aes_key,
	       aes_gcm->key_len / 8);
	/* Duplicate 128 bit key twice according to HW layout */
	if (aes_gcm->key_len == 128)
		memcpy(&hw_sa->ipsec_sa_v1.key_enc[16],
		       aes_gcm->aes_key, aes_gcm->key_len / 8);

	/* salt and seq_iv */
	memcpy(&hw_sa->ipsec_sa_v1.gcm.salt_iv, &aes_gcm->seq_iv,
	       sizeof(aes_gcm->seq_iv));
	memcpy(&hw_sa->ipsec_sa_v1.gcm.salt, &aes_gcm->salt,
	       sizeof(aes_gcm->salt));

	/* esn */
	if (xfrm_attrs->flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED) {
		hw_sa->ipsec_sa_v1.flags |= MLX5_FPGA_IPSEC_SA_ESN_EN;
		hw_sa->ipsec_sa_v1.flags |=
				(xfrm_attrs->flags &
				 MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP) ?
					MLX5_FPGA_IPSEC_SA_ESN_OVERLAP : 0;
		hw_sa->esn = htonl(xfrm_attrs->esn);
	} else {
		hw_sa->ipsec_sa_v1.flags &= ~MLX5_FPGA_IPSEC_SA_ESN_EN;
		hw_sa->ipsec_sa_v1.flags &=
				~(xfrm_attrs->flags &
				  MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP) ?
					MLX5_FPGA_IPSEC_SA_ESN_OVERLAP : 0;
		hw_sa->esn = 0;
	}

	/* rx handle */
	hw_sa->ipsec_sa_v1.sw_sa_handle = htonl(xfrm_attrs->sa_handle);

	/* enc mode */
	switch (aes_gcm->key_len) {
	case 128:
		hw_sa->ipsec_sa_v1.enc_mode =
			MLX5_FPGA_IPSEC_SA_ENC_MODE_AES_GCM_128_AUTH_128;
		break;
	case 256:
		hw_sa->ipsec_sa_v1.enc_mode =
			MLX5_FPGA_IPSEC_SA_ENC_MODE_AES_GCM_256_AUTH_128;
		break;
	}

	/* flags */
	hw_sa->ipsec_sa_v1.flags |= MLX5_FPGA_IPSEC_SA_SA_VALID |
			MLX5_FPGA_IPSEC_SA_SPI_EN |
			MLX5_FPGA_IPSEC_SA_IP_ESP;

	if (xfrm_attrs->action & MLX5_ACCEL_ESP_ACTION_ENCRYPT)
		hw_sa->ipsec_sa_v1.flags |= MLX5_FPGA_IPSEC_SA_DIR_SX;
	else
		hw_sa->ipsec_sa_v1.flags &= ~MLX5_FPGA_IPSEC_SA_DIR_SX;
}

static void
mlx5_fpga_ipsec_build_hw_sa(struct mlx5_core_dev *mdev,
			    struct mlx5_accel_esp_xfrm_attrs *xfrm_attrs,
			    const __be32 saddr[4],
			    const __be32 daddr[4],
			    const __be32 spi, bool is_ipv6,
			    struct mlx5_ifc_fpga_ipsec_sa *hw_sa)
{
	mlx5_fpga_ipsec_build_hw_xfrm(mdev, xfrm_attrs, hw_sa);

	/* IPs */
	memcpy(hw_sa->ipsec_sa_v1.sip, saddr, sizeof(hw_sa->ipsec_sa_v1.sip));
	memcpy(hw_sa->ipsec_sa_v1.dip, daddr, sizeof(hw_sa->ipsec_sa_v1.dip));

	/* SPI */
	hw_sa->ipsec_sa_v1.spi = spi;

	/* flags */
	if (is_ipv6)
		hw_sa->ipsec_sa_v1.flags |= MLX5_FPGA_IPSEC_SA_IPV6;
}

static bool is_full_mask(const void *p, size_t len)
{
	WARN_ON(len % 4);

	return !memchr_inv(p, 0xff, len);
}

static bool validate_fpga_full_mask(struct mlx5_core_dev *dev,
				    const u32 *match_c,
				    const u32 *match_v)
{
	const void *misc_params_c = MLX5_ADDR_OF(fte_match_param,
						 match_c,
						 misc_parameters);
	const void *headers_c = MLX5_ADDR_OF(fte_match_param,
					     match_c,
					     outer_headers);
	const void *headers_v = MLX5_ADDR_OF(fte_match_param,
					     match_v,
					     outer_headers);

	if (mlx5_fs_is_outer_ipv4_flow(dev, headers_c, headers_v)) {
		const void *s_ipv4_c = MLX5_ADDR_OF(fte_match_set_lyr_2_4,
						    headers_c,
						    src_ipv4_src_ipv6.ipv4_layout.ipv4);
		const void *d_ipv4_c = MLX5_ADDR_OF(fte_match_set_lyr_2_4,
						    headers_c,
						    dst_ipv4_dst_ipv6.ipv4_layout.ipv4);

		if (!is_full_mask(s_ipv4_c, MLX5_FLD_SZ_BYTES(ipv4_layout,
							      ipv4)) ||
		    !is_full_mask(d_ipv4_c, MLX5_FLD_SZ_BYTES(ipv4_layout,
							      ipv4)))
			return false;
	} else {
		const void *s_ipv6_c = MLX5_ADDR_OF(fte_match_set_lyr_2_4,
						    headers_c,
						    src_ipv4_src_ipv6.ipv6_layout.ipv6);
		const void *d_ipv6_c = MLX5_ADDR_OF(fte_match_set_lyr_2_4,
						    headers_c,
						    dst_ipv4_dst_ipv6.ipv6_layout.ipv6);

		if (!is_full_mask(s_ipv6_c, MLX5_FLD_SZ_BYTES(ipv6_layout,
							      ipv6)) ||
		    !is_full_mask(d_ipv6_c, MLX5_FLD_SZ_BYTES(ipv6_layout,
							      ipv6)))
			return false;
	}

	if (!is_full_mask(MLX5_ADDR_OF(fte_match_set_misc, misc_params_c,
				       outer_esp_spi),
			  MLX5_FLD_SZ_BYTES(fte_match_set_misc, outer_esp_spi)))
		return false;

	return true;
}

static bool mlx5_is_fpga_ipsec_rule(struct mlx5_core_dev *dev,
				    u8 match_criteria_enable,
				    const u32 *match_c,
				    const u32 *match_v)
{
	u32 ipsec_dev_caps = mlx5_accel_ipsec_device_caps(dev);
	bool ipv6_flow;

	ipv6_flow = mlx5_fs_is_outer_ipv6_flow(dev, match_c, match_v);

	if (!(match_criteria_enable & MLX5_MATCH_OUTER_HEADERS) ||
	    mlx5_fs_is_outer_udp_flow(match_c, match_v) ||
	    mlx5_fs_is_outer_tcp_flow(match_c, match_v) ||
	    mlx5_fs_is_vxlan_flow(match_c) ||
	    !(mlx5_fs_is_outer_ipv4_flow(dev, match_c, match_v) ||
	      ipv6_flow))
		return false;

	if (!(ipsec_dev_caps & MLX5_ACCEL_IPSEC_CAP_DEVICE))
		return false;

	if (!(ipsec_dev_caps & MLX5_ACCEL_IPSEC_CAP_ESP) &&
	    mlx5_fs_is_outer_ipsec_flow(match_c))
		return false;

	if (!(ipsec_dev_caps & MLX5_ACCEL_IPSEC_CAP_IPV6) &&
	    ipv6_flow)
		return false;

	if (!validate_fpga_full_mask(dev, match_c, match_v))
		return false;

	return true;
}

static bool mlx5_is_fpga_egress_ipsec_rule(struct mlx5_core_dev *dev,
					   u8 match_criteria_enable,
					   const u32 *match_c,
					   const u32 *match_v,
					   struct mlx5_flow_act *flow_act,
					   struct mlx5_flow_context *flow_context)
{
	const void *outer_c = MLX5_ADDR_OF(fte_match_param, match_c,
					   outer_headers);
	bool is_dmac = MLX5_GET(fte_match_set_lyr_2_4, outer_c, dmac_47_16) ||
			MLX5_GET(fte_match_set_lyr_2_4, outer_c, dmac_15_0);
	bool is_smac = MLX5_GET(fte_match_set_lyr_2_4, outer_c, smac_47_16) ||
			MLX5_GET(fte_match_set_lyr_2_4, outer_c, smac_15_0);
	int ret;

	ret = mlx5_is_fpga_ipsec_rule(dev, match_criteria_enable, match_c,
				      match_v);
	if (!ret)
		return ret;

	if (is_dmac || is_smac ||
	    (match_criteria_enable &
	     ~(MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS)) ||
	    (flow_act->action & ~(MLX5_FLOW_CONTEXT_ACTION_ENCRYPT | MLX5_FLOW_CONTEXT_ACTION_ALLOW)) ||
	     (flow_context->flags & FLOW_CONTEXT_HAS_TAG))
		return false;

	return true;
}

void *mlx5_fpga_ipsec_create_sa_ctx(struct mlx5_core_dev *mdev,
				    struct mlx5_accel_esp_xfrm *accel_xfrm,
				    const __be32 saddr[4],
				    const __be32 daddr[4],
				    const __be32 spi, bool is_ipv6)
{
	struct mlx5_fpga_ipsec_sa_ctx *sa_ctx;
	struct mlx5_fpga_esp_xfrm *fpga_xfrm =
			container_of(accel_xfrm, typeof(*fpga_xfrm),
				     accel_xfrm);
	struct mlx5_fpga_device *fdev = mdev->fpga;
	struct mlx5_fpga_ipsec *fipsec = fdev->ipsec;
	int opcode, err;
	void *context;

	/* alloc SA */
	sa_ctx = kzalloc(sizeof(*sa_ctx), GFP_KERNEL);
	if (!sa_ctx)
		return ERR_PTR(-ENOMEM);

	sa_ctx->dev = mdev;

	/* build candidate SA */
	mlx5_fpga_ipsec_build_hw_sa(mdev, &accel_xfrm->attrs,
				    saddr, daddr, spi, is_ipv6,
				    &sa_ctx->hw_sa);

	mutex_lock(&fpga_xfrm->lock);

	if (fpga_xfrm->sa_ctx) {        /* multiple rules for same accel_xfrm */
		/* all rules must be with same IPs and SPI */
		if (memcmp(&sa_ctx->hw_sa, &fpga_xfrm->sa_ctx->hw_sa,
			   sizeof(sa_ctx->hw_sa))) {
			context = ERR_PTR(-EINVAL);
			goto exists;
		}

		++fpga_xfrm->num_rules;
		context = fpga_xfrm->sa_ctx;
		goto exists;
	}

	/* This is unbounded fpga_xfrm, try to add to hash */
	mutex_lock(&fipsec->sa_hash_lock);

	err = rhashtable_lookup_insert_fast(&fipsec->sa_hash, &sa_ctx->hash,
					    rhash_sa);
	if (err) {
		/* Can't bound different accel_xfrm to already existing sa_ctx.
		 * This is because we can't support multiple ketmats for
		 * same IPs and SPI
		 */
		context = ERR_PTR(-EEXIST);
		goto unlock_hash;
	}

	/* Bound accel_xfrm to sa_ctx */
	opcode = is_v2_sadb_supported(fdev->ipsec) ?
			MLX5_FPGA_IPSEC_CMD_OP_ADD_SA_V2 :
			MLX5_FPGA_IPSEC_CMD_OP_ADD_SA;
	err = mlx5_fpga_ipsec_update_hw_sa(fdev, &sa_ctx->hw_sa, opcode);
	sa_ctx->hw_sa.ipsec_sa_v1.cmd = 0;
	if (err) {
		context = ERR_PTR(err);
		goto delete_hash;
	}

	mutex_unlock(&fipsec->sa_hash_lock);

	++fpga_xfrm->num_rules;
	fpga_xfrm->sa_ctx = sa_ctx;
	sa_ctx->fpga_xfrm = fpga_xfrm;

	mutex_unlock(&fpga_xfrm->lock);

	return sa_ctx;

delete_hash:
	WARN_ON(rhashtable_remove_fast(&fipsec->sa_hash, &sa_ctx->hash,
				       rhash_sa));
unlock_hash:
	mutex_unlock(&fipsec->sa_hash_lock);

exists:
	mutex_unlock(&fpga_xfrm->lock);
	kfree(sa_ctx);
	return context;
}

static void *
mlx5_fpga_ipsec_fs_create_sa_ctx(struct mlx5_core_dev *mdev,
				 struct fs_fte *fte,
				 bool is_egress)
{
	struct mlx5_accel_esp_xfrm *accel_xfrm;
	__be32 saddr[4], daddr[4], spi;
	struct mlx5_flow_group *fg;
	bool is_ipv6 = false;

	fs_get_obj(fg, fte->node.parent);
	/* validate */
	if (is_egress &&
	    !mlx5_is_fpga_egress_ipsec_rule(mdev,
					    fg->mask.match_criteria_enable,
					    fg->mask.match_criteria,
					    fte->val,
					    &fte->action,
					    &fte->flow_context))
		return ERR_PTR(-EINVAL);
	else if (!mlx5_is_fpga_ipsec_rule(mdev,
					  fg->mask.match_criteria_enable,
					  fg->mask.match_criteria,
					  fte->val))
		return ERR_PTR(-EINVAL);

	/* get xfrm context */
	accel_xfrm =
		(struct mlx5_accel_esp_xfrm *)fte->action.esp_id;

	/* IPs */
	if (mlx5_fs_is_outer_ipv4_flow(mdev, fg->mask.match_criteria,
				       fte->val)) {
		memcpy(&saddr[3],
		       MLX5_ADDR_OF(fte_match_set_lyr_2_4,
				    fte->val,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
				    sizeof(saddr[3]));
		memcpy(&daddr[3],
		       MLX5_ADDR_OF(fte_match_set_lyr_2_4,
				    fte->val,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
				    sizeof(daddr[3]));
	} else {
		memcpy(saddr,
		       MLX5_ADDR_OF(fte_match_param,
				    fte->val,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
				    sizeof(saddr));
		memcpy(daddr,
		       MLX5_ADDR_OF(fte_match_param,
				    fte->val,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
				    sizeof(daddr));
		is_ipv6 = true;
	}

	/* SPI */
	spi = MLX5_GET_BE(typeof(spi),
			  fte_match_param, fte->val,
			  misc_parameters.outer_esp_spi);

	/* create */
	return mlx5_fpga_ipsec_create_sa_ctx(mdev, accel_xfrm,
					     saddr, daddr,
					     spi, is_ipv6);
}

static void
mlx5_fpga_ipsec_release_sa_ctx(struct mlx5_fpga_ipsec_sa_ctx *sa_ctx)
{
	struct mlx5_fpga_device *fdev = sa_ctx->dev->fpga;
	struct mlx5_fpga_ipsec *fipsec = fdev->ipsec;
	int opcode = is_v2_sadb_supported(fdev->ipsec) ?
			MLX5_FPGA_IPSEC_CMD_OP_DEL_SA_V2 :
			MLX5_FPGA_IPSEC_CMD_OP_DEL_SA;
	int err;

	err = mlx5_fpga_ipsec_update_hw_sa(fdev, &sa_ctx->hw_sa, opcode);
	sa_ctx->hw_sa.ipsec_sa_v1.cmd = 0;
	if (err) {
		WARN_ON(err);
		return;
	}

	mutex_lock(&fipsec->sa_hash_lock);
	WARN_ON(rhashtable_remove_fast(&fipsec->sa_hash, &sa_ctx->hash,
				       rhash_sa));
	mutex_unlock(&fipsec->sa_hash_lock);
}

void mlx5_fpga_ipsec_delete_sa_ctx(void *context)
{
	struct mlx5_fpga_esp_xfrm *fpga_xfrm =
			((struct mlx5_fpga_ipsec_sa_ctx *)context)->fpga_xfrm;

	mutex_lock(&fpga_xfrm->lock);
	if (!--fpga_xfrm->num_rules) {
		mlx5_fpga_ipsec_release_sa_ctx(fpga_xfrm->sa_ctx);
		fpga_xfrm->sa_ctx = NULL;
	}
	mutex_unlock(&fpga_xfrm->lock);
}

static inline struct mlx5_fpga_ipsec_rule *
_rule_search(struct rb_root *root, struct fs_fte *fte)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct mlx5_fpga_ipsec_rule *rule =
				container_of(node, struct mlx5_fpga_ipsec_rule,
					     node);

		if (rule->fte < fte)
			node = node->rb_left;
		else if (rule->fte > fte)
			node = node->rb_right;
		else
			return rule;
	}
	return NULL;
}

static struct mlx5_fpga_ipsec_rule *
rule_search(struct mlx5_fpga_ipsec *ipsec_dev, struct fs_fte *fte)
{
	struct mlx5_fpga_ipsec_rule *rule;

	mutex_lock(&ipsec_dev->rules_rb_lock);
	rule = _rule_search(&ipsec_dev->rules_rb, fte);
	mutex_unlock(&ipsec_dev->rules_rb_lock);

	return rule;
}

static inline int _rule_insert(struct rb_root *root,
			       struct mlx5_fpga_ipsec_rule *rule)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct mlx5_fpga_ipsec_rule *this =
				container_of(*new, struct mlx5_fpga_ipsec_rule,
					     node);

		parent = *new;
		if (rule->fte < this->fte)
			new = &((*new)->rb_left);
		else if (rule->fte > this->fte)
			new = &((*new)->rb_right);
		else
			return -EEXIST;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&rule->node, parent, new);
	rb_insert_color(&rule->node, root);

	return 0;
}

static int rule_insert(struct mlx5_fpga_ipsec *ipsec_dev,
		       struct mlx5_fpga_ipsec_rule *rule)
{
	int ret;

	mutex_lock(&ipsec_dev->rules_rb_lock);
	ret = _rule_insert(&ipsec_dev->rules_rb, rule);
	mutex_unlock(&ipsec_dev->rules_rb_lock);

	return ret;
}

static inline void _rule_delete(struct mlx5_fpga_ipsec *ipsec_dev,
				struct mlx5_fpga_ipsec_rule *rule)
{
	struct rb_root *root = &ipsec_dev->rules_rb;

	mutex_lock(&ipsec_dev->rules_rb_lock);
	rb_erase(&rule->node, root);
	mutex_unlock(&ipsec_dev->rules_rb_lock);
}

static void rule_delete(struct mlx5_fpga_ipsec *ipsec_dev,
			struct mlx5_fpga_ipsec_rule *rule)
{
	_rule_delete(ipsec_dev, rule);
	kfree(rule);
}

struct mailbox_mod {
	uintptr_t			saved_esp_id;
	u32				saved_action;
	u32				saved_outer_esp_spi_value;
};

static void restore_spec_mailbox(struct fs_fte *fte,
				 struct mailbox_mod *mbox_mod)
{
	char *misc_params_v = MLX5_ADDR_OF(fte_match_param,
					   fte->val,
					   misc_parameters);

	MLX5_SET(fte_match_set_misc, misc_params_v, outer_esp_spi,
		 mbox_mod->saved_outer_esp_spi_value);
	fte->action.action |= mbox_mod->saved_action;
	fte->action.esp_id = (uintptr_t)mbox_mod->saved_esp_id;
}

static void modify_spec_mailbox(struct mlx5_core_dev *mdev,
				struct fs_fte *fte,
				struct mailbox_mod *mbox_mod)
{
	char *misc_params_v = MLX5_ADDR_OF(fte_match_param,
					   fte->val,
					   misc_parameters);

	mbox_mod->saved_esp_id = fte->action.esp_id;
	mbox_mod->saved_action = fte->action.action &
			(MLX5_FLOW_CONTEXT_ACTION_ENCRYPT |
			 MLX5_FLOW_CONTEXT_ACTION_DECRYPT);
	mbox_mod->saved_outer_esp_spi_value =
			MLX5_GET(fte_match_set_misc, misc_params_v,
				 outer_esp_spi);

	fte->action.esp_id = 0;
	fte->action.action &= ~(MLX5_FLOW_CONTEXT_ACTION_ENCRYPT |
				MLX5_FLOW_CONTEXT_ACTION_DECRYPT);
	if (!MLX5_CAP_FLOWTABLE(mdev,
				flow_table_properties_nic_receive.ft_field_support.outer_esp_spi))
		MLX5_SET(fte_match_set_misc, misc_params_v, outer_esp_spi, 0);
}

static enum fs_flow_table_type egress_to_fs_ft(bool egress)
{
	return egress ? FS_FT_NIC_TX : FS_FT_NIC_RX;
}

static int fpga_ipsec_fs_create_flow_group(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft,
					   u32 *in,
					   struct mlx5_flow_group *fg,
					   bool is_egress)
{
	int (*create_flow_group)(struct mlx5_flow_root_namespace *ns,
				 struct mlx5_flow_table *ft, u32 *in,
				 struct mlx5_flow_group *fg) =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(is_egress))->create_flow_group;
	char *misc_params_c = MLX5_ADDR_OF(create_flow_group_in, in,
					   match_criteria.misc_parameters);
	struct mlx5_core_dev *dev = ns->dev;
	u32 saved_outer_esp_spi_mask;
	u8 match_criteria_enable;
	int ret;

	if (MLX5_CAP_FLOWTABLE(dev,
			       flow_table_properties_nic_receive.ft_field_support.outer_esp_spi))
		return create_flow_group(ns, ft, in, fg);

	match_criteria_enable =
		MLX5_GET(create_flow_group_in, in, match_criteria_enable);
	saved_outer_esp_spi_mask =
		MLX5_GET(fte_match_set_misc, misc_params_c, outer_esp_spi);
	if (!match_criteria_enable || !saved_outer_esp_spi_mask)
		return create_flow_group(ns, ft, in, fg);

	MLX5_SET(fte_match_set_misc, misc_params_c, outer_esp_spi, 0);

	if (!(*misc_params_c) &&
	    !memcmp(misc_params_c, misc_params_c + 1, MLX5_ST_SZ_BYTES(fte_match_set_misc) - 1))
		MLX5_SET(create_flow_group_in, in, match_criteria_enable,
			 match_criteria_enable & ~MLX5_MATCH_MISC_PARAMETERS);

	ret = create_flow_group(ns, ft, in, fg);

	MLX5_SET(fte_match_set_misc, misc_params_c, outer_esp_spi, saved_outer_esp_spi_mask);
	MLX5_SET(create_flow_group_in, in, match_criteria_enable, match_criteria_enable);

	return ret;
}

static int fpga_ipsec_fs_create_fte(struct mlx5_flow_root_namespace *ns,
				    struct mlx5_flow_table *ft,
				    struct mlx5_flow_group *fg,
				    struct fs_fte *fte,
				    bool is_egress)
{
	int (*create_fte)(struct mlx5_flow_root_namespace *ns,
			  struct mlx5_flow_table *ft,
			  struct mlx5_flow_group *fg,
			  struct fs_fte *fte) =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(is_egress))->create_fte;
	struct mlx5_core_dev *dev = ns->dev;
	struct mlx5_fpga_device *fdev = dev->fpga;
	struct mlx5_fpga_ipsec *fipsec = fdev->ipsec;
	struct mlx5_fpga_ipsec_rule *rule;
	bool is_esp = fte->action.esp_id;
	struct mailbox_mod mbox_mod;
	int ret;

	if (!is_esp ||
	    !(fte->action.action &
	      (MLX5_FLOW_CONTEXT_ACTION_ENCRYPT |
	       MLX5_FLOW_CONTEXT_ACTION_DECRYPT)))
		return create_fte(ns, ft, fg, fte);

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return -ENOMEM;

	rule->ctx = mlx5_fpga_ipsec_fs_create_sa_ctx(dev, fte, is_egress);
	if (IS_ERR(rule->ctx)) {
		int err = PTR_ERR(rule->ctx);
		kfree(rule);
		return err;
	}

	rule->fte = fte;
	WARN_ON(rule_insert(fipsec, rule));

	modify_spec_mailbox(dev, fte, &mbox_mod);
	ret = create_fte(ns, ft, fg, fte);
	restore_spec_mailbox(fte, &mbox_mod);
	if (ret) {
		_rule_delete(fipsec, rule);
		mlx5_fpga_ipsec_delete_sa_ctx(rule->ctx);
		kfree(rule);
	}

	return ret;
}

static int fpga_ipsec_fs_update_fte(struct mlx5_flow_root_namespace *ns,
				    struct mlx5_flow_table *ft,
				    struct mlx5_flow_group *fg,
				    int modify_mask,
				    struct fs_fte *fte,
				    bool is_egress)
{
	int (*update_fte)(struct mlx5_flow_root_namespace *ns,
			  struct mlx5_flow_table *ft,
			  struct mlx5_flow_group *fg,
			  int modify_mask,
			  struct fs_fte *fte) =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(is_egress))->update_fte;
	struct mlx5_core_dev *dev = ns->dev;
	bool is_esp = fte->action.esp_id;
	struct mailbox_mod mbox_mod;
	int ret;

	if (!is_esp ||
	    !(fte->action.action &
	      (MLX5_FLOW_CONTEXT_ACTION_ENCRYPT |
	       MLX5_FLOW_CONTEXT_ACTION_DECRYPT)))
		return update_fte(ns, ft, fg, modify_mask, fte);

	modify_spec_mailbox(dev, fte, &mbox_mod);
	ret = update_fte(ns, ft, fg, modify_mask, fte);
	restore_spec_mailbox(fte, &mbox_mod);

	return ret;
}

static int fpga_ipsec_fs_delete_fte(struct mlx5_flow_root_namespace *ns,
				    struct mlx5_flow_table *ft,
				    struct fs_fte *fte,
				    bool is_egress)
{
	int (*delete_fte)(struct mlx5_flow_root_namespace *ns,
			  struct mlx5_flow_table *ft,
			  struct fs_fte *fte) =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(is_egress))->delete_fte;
	struct mlx5_core_dev *dev = ns->dev;
	struct mlx5_fpga_device *fdev = dev->fpga;
	struct mlx5_fpga_ipsec *fipsec = fdev->ipsec;
	struct mlx5_fpga_ipsec_rule *rule;
	bool is_esp = fte->action.esp_id;
	struct mailbox_mod mbox_mod;
	int ret;

	if (!is_esp ||
	    !(fte->action.action &
	      (MLX5_FLOW_CONTEXT_ACTION_ENCRYPT |
	       MLX5_FLOW_CONTEXT_ACTION_DECRYPT)))
		return delete_fte(ns, ft, fte);

	rule = rule_search(fipsec, fte);
	if (!rule)
		return -ENOENT;

	mlx5_fpga_ipsec_delete_sa_ctx(rule->ctx);
	rule_delete(fipsec, rule);

	modify_spec_mailbox(dev, fte, &mbox_mod);
	ret = delete_fte(ns, ft, fte);
	restore_spec_mailbox(fte, &mbox_mod);

	return ret;
}

static int
mlx5_fpga_ipsec_fs_create_flow_group_egress(struct mlx5_flow_root_namespace *ns,
					    struct mlx5_flow_table *ft,
					    u32 *in,
					    struct mlx5_flow_group *fg)
{
	return fpga_ipsec_fs_create_flow_group(ns, ft, in, fg, true);
}

static int
mlx5_fpga_ipsec_fs_create_fte_egress(struct mlx5_flow_root_namespace *ns,
				     struct mlx5_flow_table *ft,
				     struct mlx5_flow_group *fg,
				     struct fs_fte *fte)
{
	return fpga_ipsec_fs_create_fte(ns, ft, fg, fte, true);
}

static int
mlx5_fpga_ipsec_fs_update_fte_egress(struct mlx5_flow_root_namespace *ns,
				     struct mlx5_flow_table *ft,
				     struct mlx5_flow_group *fg,
				     int modify_mask,
				     struct fs_fte *fte)
{
	return fpga_ipsec_fs_update_fte(ns, ft, fg, modify_mask, fte,
					true);
}

static int
mlx5_fpga_ipsec_fs_delete_fte_egress(struct mlx5_flow_root_namespace *ns,
				     struct mlx5_flow_table *ft,
				     struct fs_fte *fte)
{
	return fpga_ipsec_fs_delete_fte(ns, ft, fte, true);
}

static int
mlx5_fpga_ipsec_fs_create_flow_group_ingress(struct mlx5_flow_root_namespace *ns,
					     struct mlx5_flow_table *ft,
					     u32 *in,
					     struct mlx5_flow_group *fg)
{
	return fpga_ipsec_fs_create_flow_group(ns, ft, in, fg, false);
}

static int
mlx5_fpga_ipsec_fs_create_fte_ingress(struct mlx5_flow_root_namespace *ns,
				      struct mlx5_flow_table *ft,
				      struct mlx5_flow_group *fg,
				      struct fs_fte *fte)
{
	return fpga_ipsec_fs_create_fte(ns, ft, fg, fte, false);
}

static int
mlx5_fpga_ipsec_fs_update_fte_ingress(struct mlx5_flow_root_namespace *ns,
				      struct mlx5_flow_table *ft,
				      struct mlx5_flow_group *fg,
				      int modify_mask,
				      struct fs_fte *fte)
{
	return fpga_ipsec_fs_update_fte(ns, ft, fg, modify_mask, fte,
					false);
}

static int
mlx5_fpga_ipsec_fs_delete_fte_ingress(struct mlx5_flow_root_namespace *ns,
				      struct mlx5_flow_table *ft,
				      struct fs_fte *fte)
{
	return fpga_ipsec_fs_delete_fte(ns, ft, fte, false);
}

static struct mlx5_flow_cmds fpga_ipsec_ingress;
static struct mlx5_flow_cmds fpga_ipsec_egress;

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_default_ipsec_fpga_cmds(enum fs_flow_table_type type)
{
	switch (type) {
	case FS_FT_NIC_RX:
		return &fpga_ipsec_ingress;
	case FS_FT_NIC_TX:
		return &fpga_ipsec_egress;
	default:
		WARN_ON(true);
		return NULL;
	}
}

int mlx5_fpga_ipsec_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_conn_attr init_attr = {0};
	struct mlx5_fpga_device *fdev = mdev->fpga;
	struct mlx5_fpga_conn *conn;
	int err;

	if (!mlx5_fpga_is_ipsec_device(mdev))
		return 0;

	fdev->ipsec = kzalloc(sizeof(*fdev->ipsec), GFP_KERNEL);
	if (!fdev->ipsec)
		return -ENOMEM;

	fdev->ipsec->fdev = fdev;

	err = mlx5_fpga_get_sbu_caps(fdev, sizeof(fdev->ipsec->caps),
				     fdev->ipsec->caps);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to retrieve IPSec extended capabilities: %d\n",
			      err);
		goto error;
	}

	INIT_LIST_HEAD(&fdev->ipsec->pending_cmds);
	spin_lock_init(&fdev->ipsec->pending_cmds_lock);

	init_attr.rx_size = SBU_QP_QUEUE_SIZE;
	init_attr.tx_size = SBU_QP_QUEUE_SIZE;
	init_attr.recv_cb = mlx5_fpga_ipsec_recv;
	init_attr.cb_arg = fdev;
	conn = mlx5_fpga_sbu_conn_create(fdev, &init_attr);
	if (IS_ERR(conn)) {
		err = PTR_ERR(conn);
		mlx5_fpga_err(fdev, "Error creating IPSec command connection %d\n",
			      err);
		goto error;
	}
	fdev->ipsec->conn = conn;

	err = rhashtable_init(&fdev->ipsec->sa_hash, &rhash_sa);
	if (err)
		goto err_destroy_conn;
	mutex_init(&fdev->ipsec->sa_hash_lock);

	fdev->ipsec->rules_rb = RB_ROOT;
	mutex_init(&fdev->ipsec->rules_rb_lock);

	err = mlx5_fpga_ipsec_enable_supported_caps(mdev);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to enable IPSec extended capabilities: %d\n",
			      err);
		goto err_destroy_hash;
	}

	return 0;

err_destroy_hash:
	rhashtable_destroy(&fdev->ipsec->sa_hash);

err_destroy_conn:
	mlx5_fpga_sbu_conn_destroy(conn);

error:
	kfree(fdev->ipsec);
	fdev->ipsec = NULL;
	return err;
}

static void destroy_rules_rb(struct rb_root *root)
{
	struct mlx5_fpga_ipsec_rule *r, *tmp;

	rbtree_postorder_for_each_entry_safe(r, tmp, root, node) {
		rb_erase(&r->node, root);
		mlx5_fpga_ipsec_delete_sa_ctx(r->ctx);
		kfree(r);
	}
}

void mlx5_fpga_ipsec_cleanup(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;

	if (!mlx5_fpga_is_ipsec_device(mdev))
		return;

	destroy_rules_rb(&fdev->ipsec->rules_rb);
	rhashtable_destroy(&fdev->ipsec->sa_hash);

	mlx5_fpga_sbu_conn_destroy(fdev->ipsec->conn);
	kfree(fdev->ipsec);
	fdev->ipsec = NULL;
}

void mlx5_fpga_ipsec_build_fs_cmds(void)
{
	/* ingress */
	fpga_ipsec_ingress.create_flow_table =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(false))->create_flow_table;
	fpga_ipsec_ingress.destroy_flow_table =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(false))->destroy_flow_table;
	fpga_ipsec_ingress.modify_flow_table =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(false))->modify_flow_table;
	fpga_ipsec_ingress.create_flow_group =
		mlx5_fpga_ipsec_fs_create_flow_group_ingress;
	fpga_ipsec_ingress.destroy_flow_group =
		 mlx5_fs_cmd_get_default(egress_to_fs_ft(false))->destroy_flow_group;
	fpga_ipsec_ingress.create_fte =
		mlx5_fpga_ipsec_fs_create_fte_ingress;
	fpga_ipsec_ingress.update_fte =
		mlx5_fpga_ipsec_fs_update_fte_ingress;
	fpga_ipsec_ingress.delete_fte =
		mlx5_fpga_ipsec_fs_delete_fte_ingress;
	fpga_ipsec_ingress.update_root_ft =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(false))->update_root_ft;

	/* egress */
	fpga_ipsec_egress.create_flow_table =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(true))->create_flow_table;
	fpga_ipsec_egress.destroy_flow_table =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(true))->destroy_flow_table;
	fpga_ipsec_egress.modify_flow_table =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(true))->modify_flow_table;
	fpga_ipsec_egress.create_flow_group =
		mlx5_fpga_ipsec_fs_create_flow_group_egress;
	fpga_ipsec_egress.destroy_flow_group =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(true))->destroy_flow_group;
	fpga_ipsec_egress.create_fte =
		mlx5_fpga_ipsec_fs_create_fte_egress;
	fpga_ipsec_egress.update_fte =
		mlx5_fpga_ipsec_fs_update_fte_egress;
	fpga_ipsec_egress.delete_fte =
		mlx5_fpga_ipsec_fs_delete_fte_egress;
	fpga_ipsec_egress.update_root_ft =
		mlx5_fs_cmd_get_default(egress_to_fs_ft(true))->update_root_ft;
}

static int
mlx5_fpga_esp_validate_xfrm_attrs(struct mlx5_core_dev *mdev,
				  const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	if (attrs->tfc_pad) {
		mlx5_core_err(mdev, "Cannot offload xfrm states with tfc padding\n");
		return -EOPNOTSUPP;
	}

	if (attrs->replay_type != MLX5_ACCEL_ESP_REPLAY_NONE) {
		mlx5_core_err(mdev, "Cannot offload xfrm states with anti replay\n");
		return -EOPNOTSUPP;
	}

	if (attrs->keymat_type != MLX5_ACCEL_ESP_KEYMAT_AES_GCM) {
		mlx5_core_err(mdev, "Only aes gcm keymat is supported\n");
		return -EOPNOTSUPP;
	}

	if (attrs->keymat.aes_gcm.iv_algo !=
	    MLX5_ACCEL_ESP_AES_GCM_IV_ALGO_SEQ) {
		mlx5_core_err(mdev, "Only iv sequence algo is supported\n");
		return -EOPNOTSUPP;
	}

	if (attrs->keymat.aes_gcm.icv_len != 128) {
		mlx5_core_err(mdev, "Cannot offload xfrm states with AEAD ICV length other than 128bit\n");
		return -EOPNOTSUPP;
	}

	if (attrs->keymat.aes_gcm.key_len != 128 &&
	    attrs->keymat.aes_gcm.key_len != 256) {
		mlx5_core_err(mdev, "Cannot offload xfrm states with AEAD key length other than 128/256 bit\n");
		return -EOPNOTSUPP;
	}

	if ((attrs->flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED) &&
	    (!MLX5_GET(ipsec_extended_cap, mdev->fpga->ipsec->caps,
		       v2_command))) {
		mlx5_core_err(mdev, "Cannot offload xfrm states with AEAD key length other than 128/256 bit\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

struct mlx5_accel_esp_xfrm *
mlx5_fpga_esp_create_xfrm(struct mlx5_core_dev *mdev,
			  const struct mlx5_accel_esp_xfrm_attrs *attrs,
			  u32 flags)
{
	struct mlx5_fpga_esp_xfrm *fpga_xfrm;

	if (!(flags & MLX5_ACCEL_XFRM_FLAG_REQUIRE_METADATA)) {
		mlx5_core_warn(mdev, "Tried to create an esp action without metadata\n");
		return ERR_PTR(-EINVAL);
	}

	if (mlx5_fpga_esp_validate_xfrm_attrs(mdev, attrs)) {
		mlx5_core_warn(mdev, "Tried to create an esp with unsupported attrs\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	fpga_xfrm = kzalloc(sizeof(*fpga_xfrm), GFP_KERNEL);
	if (!fpga_xfrm)
		return ERR_PTR(-ENOMEM);

	mutex_init(&fpga_xfrm->lock);
	memcpy(&fpga_xfrm->accel_xfrm.attrs, attrs,
	       sizeof(fpga_xfrm->accel_xfrm.attrs));

	return &fpga_xfrm->accel_xfrm;
}

void mlx5_fpga_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm)
{
	struct mlx5_fpga_esp_xfrm *fpga_xfrm =
			container_of(xfrm, struct mlx5_fpga_esp_xfrm,
				     accel_xfrm);
	/* assuming no sa_ctx are connected to this xfrm_ctx */
	kfree(fpga_xfrm);
}

int mlx5_fpga_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
			      const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct mlx5_core_dev *mdev = xfrm->mdev;
	struct mlx5_fpga_device *fdev = mdev->fpga;
	struct mlx5_fpga_ipsec *fipsec = fdev->ipsec;
	struct mlx5_fpga_esp_xfrm *fpga_xfrm;
	struct mlx5_ifc_fpga_ipsec_sa org_hw_sa;

	int err = 0;

	if (!memcmp(&xfrm->attrs, attrs, sizeof(xfrm->attrs)))
		return 0;

	if (!mlx5_fpga_esp_validate_xfrm_attrs(mdev, attrs)) {
		mlx5_core_warn(mdev, "Tried to create an esp with unsupported attrs\n");
		return -EOPNOTSUPP;
	}

	if (is_v2_sadb_supported(fipsec)) {
		mlx5_core_warn(mdev, "Modify esp is not supported\n");
		return -EOPNOTSUPP;
	}

	fpga_xfrm = container_of(xfrm, struct mlx5_fpga_esp_xfrm, accel_xfrm);

	mutex_lock(&fpga_xfrm->lock);

	if (!fpga_xfrm->sa_ctx)
		/* Unbounded xfrm, chane only sw attrs */
		goto change_sw_xfrm_attrs;

	/* copy original hw sa */
	memcpy(&org_hw_sa, &fpga_xfrm->sa_ctx->hw_sa, sizeof(org_hw_sa));
	mutex_lock(&fipsec->sa_hash_lock);
	/* remove original hw sa from hash */
	WARN_ON(rhashtable_remove_fast(&fipsec->sa_hash,
				       &fpga_xfrm->sa_ctx->hash, rhash_sa));
	/* update hw_sa with new xfrm attrs*/
	mlx5_fpga_ipsec_build_hw_xfrm(xfrm->mdev, attrs,
				      &fpga_xfrm->sa_ctx->hw_sa);
	/* try to insert new hw_sa to hash */
	err = rhashtable_insert_fast(&fipsec->sa_hash,
				     &fpga_xfrm->sa_ctx->hash, rhash_sa);
	if (err)
		goto rollback_sa;

	/* modify device with new hw_sa */
	err = mlx5_fpga_ipsec_update_hw_sa(fdev, &fpga_xfrm->sa_ctx->hw_sa,
					   MLX5_FPGA_IPSEC_CMD_OP_MOD_SA_V2);
	fpga_xfrm->sa_ctx->hw_sa.ipsec_sa_v1.cmd = 0;
	if (err)
		WARN_ON(rhashtable_remove_fast(&fipsec->sa_hash,
					       &fpga_xfrm->sa_ctx->hash,
					       rhash_sa));
rollback_sa:
	if (err) {
		/* return original hw_sa to hash */
		memcpy(&fpga_xfrm->sa_ctx->hw_sa, &org_hw_sa,
		       sizeof(org_hw_sa));
		WARN_ON(rhashtable_insert_fast(&fipsec->sa_hash,
					       &fpga_xfrm->sa_ctx->hash,
					       rhash_sa));
	}
	mutex_unlock(&fipsec->sa_hash_lock);

change_sw_xfrm_attrs:
	if (!err)
		memcpy(&xfrm->attrs, attrs, sizeof(xfrm->attrs));
	mutex_unlock(&fpga_xfrm->lock);
	return err;
}
