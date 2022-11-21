// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

#include "funeth.h"
#include "funeth_ktls.h"

static int fun_admin_ktls_create(struct funeth_priv *fp, unsigned int id)
{
	struct fun_admin_ktls_create_req req = {
		.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_KTLS,
						     sizeof(req)),
		.subop = FUN_ADMIN_SUBOP_CREATE,
		.id = cpu_to_be32(id),
	};

	return fun_submit_admin_sync_cmd(fp->fdev, &req.common, NULL, 0, 0);
}

static int fun_ktls_add(struct net_device *netdev, struct sock *sk,
			enum tls_offload_ctx_dir direction,
			struct tls_crypto_info *crypto_info,
			u32 start_offload_tcp_sn)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct fun_admin_ktls_modify_req req = {
		.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_KTLS,
						     sizeof(req)),
		.subop = FUN_ADMIN_SUBOP_MODIFY,
		.id = cpu_to_be32(fp->ktls_id),
		.tcp_seq = cpu_to_be32(start_offload_tcp_sn),
	};
	struct fun_admin_ktls_modify_rsp rsp;
	struct fun_ktls_tx_ctx *tx_ctx;
	int rc;

	if (direction != TLS_OFFLOAD_CTX_DIR_TX)
		return -EOPNOTSUPP;

	if (crypto_info->version == TLS_1_2_VERSION)
		req.version = FUN_KTLS_TLSV2;
	else
		return -EOPNOTSUPP;

	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128: {
		struct tls12_crypto_info_aes_gcm_128 *c = (void *)crypto_info;

		req.cipher = FUN_KTLS_CIPHER_AES_GCM_128;
		memcpy(req.key, c->key, sizeof(c->key));
		memcpy(req.iv, c->iv, sizeof(c->iv));
		memcpy(req.salt, c->salt, sizeof(c->salt));
		memcpy(req.record_seq, c->rec_seq, sizeof(c->rec_seq));
		break;
	}
	default:
		return -EOPNOTSUPP;
	}

	rc = fun_submit_admin_sync_cmd(fp->fdev, &req.common, &rsp,
				       sizeof(rsp), 0);
	memzero_explicit(&req, sizeof(req));
	if (rc)
		return rc;

	tx_ctx = tls_driver_ctx(sk, direction);
	tx_ctx->tlsid = rsp.tlsid;
	tx_ctx->next_seq = start_offload_tcp_sn;
	atomic64_inc(&fp->tx_tls_add);
	return 0;
}

static void fun_ktls_del(struct net_device *netdev,
			 struct tls_context *tls_ctx,
			 enum tls_offload_ctx_dir direction)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct fun_admin_ktls_modify_req req;
	struct fun_ktls_tx_ctx *tx_ctx;

	if (direction != TLS_OFFLOAD_CTX_DIR_TX)
		return;

	tx_ctx = __tls_driver_ctx(tls_ctx, direction);

	req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_KTLS,
			offsetof(struct fun_admin_ktls_modify_req, tcp_seq));
	req.subop = FUN_ADMIN_SUBOP_MODIFY;
	req.flags = cpu_to_be16(FUN_KTLS_MODIFY_REMOVE);
	req.id = cpu_to_be32(fp->ktls_id);
	req.tlsid = tx_ctx->tlsid;

	fun_submit_admin_sync_cmd(fp->fdev, &req.common, NULL, 0, 0);
	atomic64_inc(&fp->tx_tls_del);
}

static int fun_ktls_resync(struct net_device *netdev, struct sock *sk, u32 seq,
			   u8 *rcd_sn, enum tls_offload_ctx_dir direction)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct fun_admin_ktls_modify_req req;
	struct fun_ktls_tx_ctx *tx_ctx;
	int rc;

	if (direction != TLS_OFFLOAD_CTX_DIR_TX)
		return -EOPNOTSUPP;

	tx_ctx = tls_driver_ctx(sk, direction);

	req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_KTLS,
			offsetof(struct fun_admin_ktls_modify_req, key));
	req.subop = FUN_ADMIN_SUBOP_MODIFY;
	req.flags = 0;
	req.id = cpu_to_be32(fp->ktls_id);
	req.tlsid = tx_ctx->tlsid;
	req.tcp_seq = cpu_to_be32(seq);
	req.version = 0;
	req.cipher = 0;
	memcpy(req.record_seq, rcd_sn, sizeof(req.record_seq));

	atomic64_inc(&fp->tx_tls_resync);
	rc = fun_submit_admin_sync_cmd(fp->fdev, &req.common, NULL, 0, 0);
	if (!rc)
		tx_ctx->next_seq = seq;
	return rc;
}

static const struct tlsdev_ops fun_ktls_ops = {
	.tls_dev_add = fun_ktls_add,
	.tls_dev_del = fun_ktls_del,
	.tls_dev_resync = fun_ktls_resync,
};

int fun_ktls_init(struct net_device *netdev)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	int rc;

	rc = fun_admin_ktls_create(fp, netdev->dev_port);
	if (rc)
		return rc;

	fp->ktls_id = netdev->dev_port;
	netdev->tlsdev_ops = &fun_ktls_ops;
	netdev->hw_features |= NETIF_F_HW_TLS_TX;
	netdev->features |= NETIF_F_HW_TLS_TX;
	return 0;
}

void fun_ktls_cleanup(struct funeth_priv *fp)
{
	if (fp->ktls_id == FUN_HCI_ID_INVALID)
		return;

	fun_res_destroy(fp->fdev, FUN_ADMIN_OP_KTLS, 0, fp->ktls_id);
	fp->ktls_id = FUN_HCI_ID_INVALID;
}
