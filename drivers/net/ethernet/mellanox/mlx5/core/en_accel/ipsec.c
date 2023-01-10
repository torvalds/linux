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

#include <crypto/internal/geniv.h>
#include <crypto/aead.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>

#include "en.h"
#include "ipsec.h"
#include "ipsec_rxtx.h"

static struct mlx5e_ipsec_sa_entry *to_ipsec_sa_entry(struct xfrm_state *x)
{
	return (struct mlx5e_ipsec_sa_entry *)x->xso.offload_handle;
}

static struct mlx5e_ipsec_pol_entry *to_ipsec_pol_entry(struct xfrm_policy *x)
{
	return (struct mlx5e_ipsec_pol_entry *)x->xdo.offload_handle;
}

static bool mlx5e_ipsec_update_esn_state(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct xfrm_replay_state_esn *replay_esn;
	u32 seq_bottom = 0;
	u8 overlap;

	if (!(sa_entry->x->props.flags & XFRM_STATE_ESN)) {
		sa_entry->esn_state.trigger = 0;
		return false;
	}

	replay_esn = sa_entry->x->replay_esn;
	if (replay_esn->seq >= replay_esn->replay_window)
		seq_bottom = replay_esn->seq - replay_esn->replay_window + 1;

	overlap = sa_entry->esn_state.overlap;

	sa_entry->esn_state.esn = xfrm_replay_seqhi(sa_entry->x,
						    htonl(seq_bottom));

	sa_entry->esn_state.trigger = 1;
	if (unlikely(overlap && seq_bottom < MLX5E_IPSEC_ESN_SCOPE_MID)) {
		sa_entry->esn_state.overlap = 0;
		return true;
	} else if (unlikely(!overlap &&
			    (seq_bottom >= MLX5E_IPSEC_ESN_SCOPE_MID))) {
		sa_entry->esn_state.overlap = 1;
		return true;
	}

	return false;
}

static void mlx5e_ipsec_init_limits(struct mlx5e_ipsec_sa_entry *sa_entry,
				    struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct xfrm_state *x = sa_entry->x;

	attrs->hard_packet_limit = x->lft.hard_packet_limit;
	if (x->lft.soft_packet_limit == XFRM_INF)
		return;

	/* Hardware decrements hard_packet_limit counter through
	 * the operation. While fires an event when soft_packet_limit
	 * is reached. It emans that we need substitute the numbers
	 * in order to properly count soft limit.
	 *
	 * As an example:
	 * XFRM user sets soft limit is 2 and hard limit is 9 and
	 * expects to see soft event after 2 packets and hard event
	 * after 9 packets. In our case, the hard limit will be set
	 * to 9 and soft limit is comparator to 7 so user gets the
	 * soft event after 2 packeta
	 */
	attrs->soft_packet_limit =
		x->lft.hard_packet_limit - x->lft.soft_packet_limit;
}

void mlx5e_ipsec_build_accel_xfrm_attrs(struct mlx5e_ipsec_sa_entry *sa_entry,
					struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct xfrm_state *x = sa_entry->x;
	struct aes_gcm_keymat *aes_gcm = &attrs->aes_gcm;
	struct aead_geniv_ctx *geniv_ctx;
	struct crypto_aead *aead;
	unsigned int crypto_data_len, key_len;
	int ivsize;

	memset(attrs, 0, sizeof(*attrs));

	/* key */
	crypto_data_len = (x->aead->alg_key_len + 7) / 8;
	key_len = crypto_data_len - 4; /* 4 bytes salt at end */

	memcpy(aes_gcm->aes_key, x->aead->alg_key, key_len);
	aes_gcm->key_len = key_len * 8;

	/* salt and seq_iv */
	aead = x->data;
	geniv_ctx = crypto_aead_ctx(aead);
	ivsize = crypto_aead_ivsize(aead);
	memcpy(&aes_gcm->seq_iv, &geniv_ctx->salt, ivsize);
	memcpy(&aes_gcm->salt, x->aead->alg_key + key_len,
	       sizeof(aes_gcm->salt));

	attrs->authsize = crypto_aead_authsize(aead) / 4; /* in dwords */

	/* iv len */
	aes_gcm->icv_len = x->aead->alg_icv_len;

	/* esn */
	if (sa_entry->esn_state.trigger) {
		attrs->esn_trigger = true;
		attrs->esn = sa_entry->esn_state.esn;
		attrs->esn_overlap = sa_entry->esn_state.overlap;
		attrs->replay_window = x->replay_esn->replay_window;
	}

	attrs->dir = x->xso.dir;
	/* spi */
	attrs->spi = be32_to_cpu(x->id.spi);

	/* source , destination ips */
	memcpy(&attrs->saddr, x->props.saddr.a6, sizeof(attrs->saddr));
	memcpy(&attrs->daddr, x->id.daddr.a6, sizeof(attrs->daddr));
	attrs->family = x->props.family;
	attrs->type = x->xso.type;
	attrs->reqid = x->props.reqid;

	mlx5e_ipsec_init_limits(sa_entry, attrs);
}

static inline int mlx5e_xfrm_validate_state(struct xfrm_state *x)
{
	struct net_device *netdev = x->xso.real_dev;
	struct mlx5e_priv *priv;

	priv = netdev_priv(netdev);

	if (x->props.aalgo != SADB_AALG_NONE) {
		netdev_info(netdev, "Cannot offload authenticated xfrm states\n");
		return -EINVAL;
	}
	if (x->props.ealgo != SADB_X_EALG_AES_GCM_ICV16) {
		netdev_info(netdev, "Only AES-GCM-ICV16 xfrm state may be offloaded\n");
		return -EINVAL;
	}
	if (x->props.calgo != SADB_X_CALG_NONE) {
		netdev_info(netdev, "Cannot offload compressed xfrm states\n");
		return -EINVAL;
	}
	if (x->props.flags & XFRM_STATE_ESN &&
	    !(mlx5_ipsec_device_caps(priv->mdev) & MLX5_IPSEC_CAP_ESN)) {
		netdev_info(netdev, "Cannot offload ESN xfrm states\n");
		return -EINVAL;
	}
	if (x->props.family != AF_INET &&
	    x->props.family != AF_INET6) {
		netdev_info(netdev, "Only IPv4/6 xfrm states may be offloaded\n");
		return -EINVAL;
	}
	if (x->id.proto != IPPROTO_ESP) {
		netdev_info(netdev, "Only ESP xfrm state may be offloaded\n");
		return -EINVAL;
	}
	if (x->encap) {
		netdev_info(netdev, "Encapsulated xfrm state may not be offloaded\n");
		return -EINVAL;
	}
	if (!x->aead) {
		netdev_info(netdev, "Cannot offload xfrm states without aead\n");
		return -EINVAL;
	}
	if (x->aead->alg_icv_len != 128) {
		netdev_info(netdev, "Cannot offload xfrm states with AEAD ICV length other than 128bit\n");
		return -EINVAL;
	}
	if ((x->aead->alg_key_len != 128 + 32) &&
	    (x->aead->alg_key_len != 256 + 32)) {
		netdev_info(netdev, "Cannot offload xfrm states with AEAD key length other than 128/256 bit\n");
		return -EINVAL;
	}
	if (x->tfcpad) {
		netdev_info(netdev, "Cannot offload xfrm states with tfc padding\n");
		return -EINVAL;
	}
	if (!x->geniv) {
		netdev_info(netdev, "Cannot offload xfrm states without geniv\n");
		return -EINVAL;
	}
	if (strcmp(x->geniv, "seqiv")) {
		netdev_info(netdev, "Cannot offload xfrm states with geniv other than seqiv\n");
		return -EINVAL;
	}
	switch (x->xso.type) {
	case XFRM_DEV_OFFLOAD_CRYPTO:
		if (!(mlx5_ipsec_device_caps(priv->mdev) &
		      MLX5_IPSEC_CAP_CRYPTO)) {
			netdev_info(netdev, "Crypto offload is not supported\n");
			return -EINVAL;
		}

		if (x->props.mode != XFRM_MODE_TRANSPORT &&
		    x->props.mode != XFRM_MODE_TUNNEL) {
			netdev_info(netdev, "Only transport and tunnel xfrm states may be offloaded\n");
			return -EINVAL;
		}
		break;
	case XFRM_DEV_OFFLOAD_PACKET:
		if (!(mlx5_ipsec_device_caps(priv->mdev) &
		      MLX5_IPSEC_CAP_PACKET_OFFLOAD)) {
			netdev_info(netdev, "Packet offload is not supported\n");
			return -EINVAL;
		}

		if (x->props.mode != XFRM_MODE_TRANSPORT) {
			netdev_info(netdev, "Only transport xfrm states may be offloaded in packet mode\n");
			return -EINVAL;
		}

		if (x->replay_esn && x->replay_esn->replay_window != 32 &&
		    x->replay_esn->replay_window != 64 &&
		    x->replay_esn->replay_window != 128 &&
		    x->replay_esn->replay_window != 256) {
			netdev_info(netdev,
				    "Unsupported replay window size %u\n",
				    x->replay_esn->replay_window);
			return -EINVAL;
		}

		if (!x->props.reqid) {
			netdev_info(netdev, "Cannot offload without reqid\n");
			return -EINVAL;
		}

		if (x->lft.hard_byte_limit != XFRM_INF ||
		    x->lft.soft_byte_limit != XFRM_INF) {
			netdev_info(netdev,
				    "Device doesn't support limits in bytes\n");
			return -EINVAL;
		}

		if (x->lft.soft_packet_limit >= x->lft.hard_packet_limit &&
		    x->lft.hard_packet_limit != XFRM_INF) {
			/* XFRM stack doesn't prevent such configuration :(. */
			netdev_info(netdev,
				    "Hard packet limit must be greater than soft one\n");
			return -EINVAL;
		}
		break;
	default:
		netdev_info(netdev, "Unsupported xfrm offload type %d\n",
			    x->xso.type);
		return -EINVAL;
	}
	return 0;
}

static void _update_xfrm_state(struct work_struct *work)
{
	struct mlx5e_ipsec_modify_state_work *modify_work =
		container_of(work, struct mlx5e_ipsec_modify_state_work, work);
	struct mlx5e_ipsec_sa_entry *sa_entry = container_of(
		modify_work, struct mlx5e_ipsec_sa_entry, modify_work);

	mlx5_accel_esp_modify_xfrm(sa_entry, &modify_work->attrs);
}

static int mlx5e_xfrm_add_state(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = NULL;
	struct net_device *netdev = x->xso.real_dev;
	struct mlx5e_ipsec *ipsec;
	struct mlx5e_priv *priv;
	int err;

	priv = netdev_priv(netdev);
	if (!priv->ipsec)
		return -EOPNOTSUPP;

	ipsec = priv->ipsec;
	err = mlx5e_xfrm_validate_state(x);
	if (err)
		return err;

	sa_entry = kzalloc(sizeof(*sa_entry), GFP_KERNEL);
	if (!sa_entry) {
		err = -ENOMEM;
		goto out;
	}

	sa_entry->x = x;
	sa_entry->ipsec = ipsec;

	/* check esn */
	mlx5e_ipsec_update_esn_state(sa_entry);

	mlx5e_ipsec_build_accel_xfrm_attrs(sa_entry, &sa_entry->attrs);
	/* create hw context */
	err = mlx5_ipsec_create_sa_ctx(sa_entry);
	if (err)
		goto err_xfrm;

	err = mlx5e_accel_ipsec_fs_add_rule(sa_entry);
	if (err)
		goto err_hw_ctx;

	/* We use *_bh() variant because xfrm_timer_handler(), which runs
	 * in softirq context, can reach our state delete logic and we need
	 * xa_erase_bh() there.
	 */
	err = xa_insert_bh(&ipsec->sadb, sa_entry->ipsec_obj_id, sa_entry,
			   GFP_KERNEL);
	if (err)
		goto err_add_rule;

	if (x->xso.dir == XFRM_DEV_OFFLOAD_OUT)
		sa_entry->set_iv_op = (x->props.flags & XFRM_STATE_ESN) ?
				mlx5e_ipsec_set_iv_esn : mlx5e_ipsec_set_iv;

	INIT_WORK(&sa_entry->modify_work.work, _update_xfrm_state);
	x->xso.offload_handle = (unsigned long)sa_entry;
	return 0;

err_add_rule:
	mlx5e_accel_ipsec_fs_del_rule(sa_entry);
err_hw_ctx:
	mlx5_ipsec_free_sa_ctx(sa_entry);
err_xfrm:
	kfree(sa_entry);
out:
	return err;
}

static void mlx5e_xfrm_del_state(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = to_ipsec_sa_entry(x);
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5e_ipsec_sa_entry *old;

	old = xa_erase_bh(&ipsec->sadb, sa_entry->ipsec_obj_id);
	WARN_ON(old != sa_entry);
}

static void mlx5e_xfrm_free_state(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = to_ipsec_sa_entry(x);

	cancel_work_sync(&sa_entry->modify_work.work);
	mlx5e_accel_ipsec_fs_del_rule(sa_entry);
	mlx5_ipsec_free_sa_ctx(sa_entry);
	kfree(sa_entry);
}

void mlx5e_ipsec_init(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec *ipsec;
	int ret = -ENOMEM;

	if (!mlx5_ipsec_device_caps(priv->mdev)) {
		netdev_dbg(priv->netdev, "Not an IPSec offload device\n");
		return;
	}

	ipsec = kzalloc(sizeof(*ipsec), GFP_KERNEL);
	if (!ipsec)
		return;

	xa_init_flags(&ipsec->sadb, XA_FLAGS_ALLOC);
	ipsec->mdev = priv->mdev;
	ipsec->wq = alloc_ordered_workqueue("mlx5e_ipsec: %s", 0,
					    priv->netdev->name);
	if (!ipsec->wq)
		goto err_wq;

	if (mlx5_ipsec_device_caps(priv->mdev) &
	    MLX5_IPSEC_CAP_PACKET_OFFLOAD) {
		ret = mlx5e_ipsec_aso_init(ipsec);
		if (ret)
			goto err_aso;
	}

	ret = mlx5e_accel_ipsec_fs_init(ipsec);
	if (ret)
		goto err_fs_init;

	ipsec->fs = priv->fs;
	priv->ipsec = ipsec;
	netdev_dbg(priv->netdev, "IPSec attached to netdevice\n");
	return;

err_fs_init:
	if (mlx5_ipsec_device_caps(priv->mdev) & MLX5_IPSEC_CAP_PACKET_OFFLOAD)
		mlx5e_ipsec_aso_cleanup(ipsec);
err_aso:
	destroy_workqueue(ipsec->wq);
err_wq:
	kfree(ipsec);
	mlx5_core_err(priv->mdev, "IPSec initialization failed, %d\n", ret);
	return;
}

void mlx5e_ipsec_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec *ipsec = priv->ipsec;

	if (!ipsec)
		return;

	mlx5e_accel_ipsec_fs_cleanup(ipsec);
	if (mlx5_ipsec_device_caps(priv->mdev) & MLX5_IPSEC_CAP_PACKET_OFFLOAD)
		mlx5e_ipsec_aso_cleanup(ipsec);
	destroy_workqueue(ipsec->wq);
	kfree(ipsec);
	priv->ipsec = NULL;
}

static bool mlx5e_ipsec_offload_ok(struct sk_buff *skb, struct xfrm_state *x)
{
	if (x->props.family == AF_INET) {
		/* Offload with IPv4 options is not supported yet */
		if (ip_hdr(skb)->ihl > 5)
			return false;
	} else {
		/* Offload with IPv6 extension headers is not support yet */
		if (ipv6_ext_hdr(ipv6_hdr(skb)->nexthdr))
			return false;
	}

	return true;
}

static void mlx5e_xfrm_advance_esn_state(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = to_ipsec_sa_entry(x);
	struct mlx5e_ipsec_modify_state_work *modify_work =
		&sa_entry->modify_work;
	bool need_update;

	need_update = mlx5e_ipsec_update_esn_state(sa_entry);
	if (!need_update)
		return;

	mlx5e_ipsec_build_accel_xfrm_attrs(sa_entry, &modify_work->attrs);
	queue_work(sa_entry->ipsec->wq, &modify_work->work);
}

static void mlx5e_xfrm_update_curlft(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = to_ipsec_sa_entry(x);
	int err;

	lockdep_assert_held(&x->lock);

	if (sa_entry->attrs.soft_packet_limit == XFRM_INF)
		/* Limits are not configured, as soft limit
		 * must be lowever than hard limit.
		 */
		return;

	err = mlx5e_ipsec_aso_query(sa_entry, NULL);
	if (err)
		return;

	mlx5e_ipsec_aso_update_curlft(sa_entry, &x->curlft.packets);
}

static int mlx5e_xfrm_validate_policy(struct xfrm_policy *x)
{
	struct net_device *netdev = x->xdo.real_dev;

	if (x->type != XFRM_POLICY_TYPE_MAIN) {
		netdev_info(netdev, "Cannot offload non-main policy types\n");
		return -EINVAL;
	}

	/* Please pay attention that we support only one template */
	if (x->xfrm_nr > 1) {
		netdev_info(netdev, "Cannot offload more than one template\n");
		return -EINVAL;
	}

	if (x->xdo.dir != XFRM_DEV_OFFLOAD_IN &&
	    x->xdo.dir != XFRM_DEV_OFFLOAD_OUT) {
		netdev_info(netdev, "Cannot offload forward policy\n");
		return -EINVAL;
	}

	if (!x->xfrm_vec[0].reqid) {
		netdev_info(netdev, "Cannot offload policy without reqid\n");
		return -EINVAL;
	}

	if (x->xdo.type != XFRM_DEV_OFFLOAD_PACKET) {
		netdev_info(netdev, "Unsupported xfrm offload type\n");
		return -EINVAL;
	}

	return 0;
}

static void
mlx5e_ipsec_build_accel_pol_attrs(struct mlx5e_ipsec_pol_entry *pol_entry,
				  struct mlx5_accel_pol_xfrm_attrs *attrs)
{
	struct xfrm_policy *x = pol_entry->x;
	struct xfrm_selector *sel;

	sel = &x->selector;
	memset(attrs, 0, sizeof(*attrs));

	memcpy(&attrs->saddr, sel->saddr.a6, sizeof(attrs->saddr));
	memcpy(&attrs->daddr, sel->daddr.a6, sizeof(attrs->daddr));
	attrs->family = sel->family;
	attrs->dir = x->xdo.dir;
	attrs->action = x->action;
	attrs->type = XFRM_DEV_OFFLOAD_PACKET;
	attrs->reqid = x->xfrm_vec[0].reqid;
}

static int mlx5e_xfrm_add_policy(struct xfrm_policy *x)
{
	struct net_device *netdev = x->xdo.real_dev;
	struct mlx5e_ipsec_pol_entry *pol_entry;
	struct mlx5e_priv *priv;
	int err;

	priv = netdev_priv(netdev);
	if (!priv->ipsec)
		return -EOPNOTSUPP;

	err = mlx5e_xfrm_validate_policy(x);
	if (err)
		return err;

	pol_entry = kzalloc(sizeof(*pol_entry), GFP_KERNEL);
	if (!pol_entry)
		return -ENOMEM;

	pol_entry->x = x;
	pol_entry->ipsec = priv->ipsec;

	mlx5e_ipsec_build_accel_pol_attrs(pol_entry, &pol_entry->attrs);
	err = mlx5e_accel_ipsec_fs_add_pol(pol_entry);
	if (err)
		goto err_fs;

	x->xdo.offload_handle = (unsigned long)pol_entry;
	return 0;

err_fs:
	kfree(pol_entry);
	return err;
}

static void mlx5e_xfrm_free_policy(struct xfrm_policy *x)
{
	struct mlx5e_ipsec_pol_entry *pol_entry = to_ipsec_pol_entry(x);

	mlx5e_accel_ipsec_fs_del_pol(pol_entry);
	kfree(pol_entry);
}

static const struct xfrmdev_ops mlx5e_ipsec_xfrmdev_ops = {
	.xdo_dev_state_add	= mlx5e_xfrm_add_state,
	.xdo_dev_state_delete	= mlx5e_xfrm_del_state,
	.xdo_dev_state_free	= mlx5e_xfrm_free_state,
	.xdo_dev_offload_ok	= mlx5e_ipsec_offload_ok,
	.xdo_dev_state_advance_esn = mlx5e_xfrm_advance_esn_state,
};

static const struct xfrmdev_ops mlx5e_ipsec_packet_xfrmdev_ops = {
	.xdo_dev_state_add	= mlx5e_xfrm_add_state,
	.xdo_dev_state_delete	= mlx5e_xfrm_del_state,
	.xdo_dev_state_free	= mlx5e_xfrm_free_state,
	.xdo_dev_offload_ok	= mlx5e_ipsec_offload_ok,
	.xdo_dev_state_advance_esn = mlx5e_xfrm_advance_esn_state,

	.xdo_dev_state_update_curlft = mlx5e_xfrm_update_curlft,
	.xdo_dev_policy_add = mlx5e_xfrm_add_policy,
	.xdo_dev_policy_free = mlx5e_xfrm_free_policy,
};

void mlx5e_ipsec_build_netdev(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct net_device *netdev = priv->netdev;

	if (!mlx5_ipsec_device_caps(mdev))
		return;

	mlx5_core_info(mdev, "mlx5e: IPSec ESP acceleration enabled\n");

	if (mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_PACKET_OFFLOAD)
		netdev->xfrmdev_ops = &mlx5e_ipsec_packet_xfrmdev_ops;
	else
		netdev->xfrmdev_ops = &mlx5e_ipsec_xfrmdev_ops;

	netdev->features |= NETIF_F_HW_ESP;
	netdev->hw_enc_features |= NETIF_F_HW_ESP;

	if (!MLX5_CAP_ETH(mdev, swp_csum)) {
		mlx5_core_dbg(mdev, "mlx5e: SWP checksum not supported\n");
		return;
	}

	netdev->features |= NETIF_F_HW_ESP_TX_CSUM;
	netdev->hw_enc_features |= NETIF_F_HW_ESP_TX_CSUM;

	if (!MLX5_CAP_ETH(mdev, swp_lso)) {
		mlx5_core_dbg(mdev, "mlx5e: ESP LSO not supported\n");
		return;
	}

	netdev->gso_partial_features |= NETIF_F_GSO_ESP;
	mlx5_core_dbg(mdev, "mlx5e: ESP GSO capability turned on\n");
	netdev->features |= NETIF_F_GSO_ESP;
	netdev->hw_features |= NETIF_F_GSO_ESP;
	netdev->hw_enc_features |= NETIF_F_GSO_ESP;
}
