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
#include <net/netevent.h>

#include "en.h"
#include "eswitch.h"
#include "ipsec.h"
#include "ipsec_rxtx.h"
#include "en_rep.h"

#define MLX5_IPSEC_RESCHED msecs_to_jiffies(1000)
#define MLX5E_IPSEC_TUNNEL_SA XA_MARK_1

static struct mlx5e_ipsec_sa_entry *to_ipsec_sa_entry(struct xfrm_state *x)
{
	return (struct mlx5e_ipsec_sa_entry *)x->xso.offload_handle;
}

static struct mlx5e_ipsec_pol_entry *to_ipsec_pol_entry(struct xfrm_policy *x)
{
	return (struct mlx5e_ipsec_pol_entry *)x->xdo.offload_handle;
}

static void mlx5e_ipsec_handle_sw_limits(struct work_struct *_work)
{
	struct mlx5e_ipsec_dwork *dwork =
		container_of(_work, struct mlx5e_ipsec_dwork, dwork.work);
	struct mlx5e_ipsec_sa_entry *sa_entry = dwork->sa_entry;
	struct xfrm_state *x = sa_entry->x;

	if (sa_entry->attrs.drop)
		return;

	spin_lock_bh(&x->lock);
	if (x->km.state == XFRM_STATE_EXPIRED) {
		sa_entry->attrs.drop = true;
		spin_unlock_bh(&x->lock);

		mlx5e_accel_ipsec_fs_modify(sa_entry);
		return;
	}

	if (x->km.state != XFRM_STATE_VALID) {
		spin_unlock_bh(&x->lock);
		return;
	}

	xfrm_state_check_expire(x);
	spin_unlock_bh(&x->lock);

	queue_delayed_work(sa_entry->ipsec->wq, &dwork->dwork,
			   MLX5_IPSEC_RESCHED);
}

static bool mlx5e_ipsec_update_esn_state(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct xfrm_state *x = sa_entry->x;
	u32 seq_bottom = 0;
	u32 esn, esn_msb;
	u8 overlap;

	switch (x->xso.dir) {
	case XFRM_DEV_OFFLOAD_IN:
		esn = x->replay_esn->seq;
		esn_msb = x->replay_esn->seq_hi;
		break;
	case XFRM_DEV_OFFLOAD_OUT:
		esn = x->replay_esn->oseq;
		esn_msb = x->replay_esn->oseq_hi;
		break;
	default:
		WARN_ON(true);
		return false;
	}

	overlap = sa_entry->esn_state.overlap;

	if (!x->replay_esn->replay_window) {
		seq_bottom = esn;
	} else {
		if (esn >= x->replay_esn->replay_window)
			seq_bottom = esn - x->replay_esn->replay_window + 1;

		if (x->xso.type == XFRM_DEV_OFFLOAD_CRYPTO)
			esn_msb = xfrm_replay_seqhi(x, htonl(seq_bottom));
	}

	if (sa_entry->esn_state.esn_msb)
		sa_entry->esn_state.esn = esn;
	else
		/* According to RFC4303, section "3.3.3. Sequence Number Generation",
		 * the first packet sent using a given SA will contain a sequence
		 * number of 1.
		 */
		sa_entry->esn_state.esn = max_t(u32, esn, 1);
	sa_entry->esn_state.esn_msb = esn_msb;

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
	s64 start_value, n;

	attrs->lft.hard_packet_limit = x->lft.hard_packet_limit;
	attrs->lft.soft_packet_limit = x->lft.soft_packet_limit;
	if (x->lft.soft_packet_limit == XFRM_INF)
		return;

	/* Compute hard limit initial value and number of rounds.
	 *
	 * The counting pattern of hardware counter goes:
	 *                value  -> 2^31-1
	 *      2^31  | (2^31-1) -> 2^31-1
	 *      2^31  | (2^31-1) -> 2^31-1
	 *      [..]
	 *      2^31  | (2^31-1) -> 0
	 *
	 * The pattern is created by using an ASO operation to atomically set
	 * bit 31 after the down counter clears bit 31. This is effectively an
	 * atomic addition of 2**31 to the counter.
	 *
	 * We wish to configure the counter, within the above pattern, so that
	 * when it reaches 0, it has hit the hard limit. This is defined by this
	 * system of equations:
	 *
	 *      hard_limit == start_value + n * 2^31
	 *      n >= 0
	 *      start_value < 2^32, start_value >= 0
	 *
	 * These equations are not single-solution, there are often two choices:
	 *      hard_limit == start_value + n * 2^31
	 *      hard_limit == (start_value+2^31) + (n-1) * 2^31
	 *
	 * The algorithm selects the solution that keeps the counter value
	 * above 2^31 until the final iteration.
	 */

	/* Start by estimating n and compute start_value */
	n = attrs->lft.hard_packet_limit / BIT_ULL(31);
	start_value = attrs->lft.hard_packet_limit - n * BIT_ULL(31);

	/* Choose the best of the two solutions: */
	if (n >= 1)
		n -= 1;

	/* Computed values solve the system of equations: */
	start_value = attrs->lft.hard_packet_limit - n * BIT_ULL(31);

	/* The best solution means: when there are multiple iterations we must
	 * start above 2^31 and count down to 2**31 to get the interrupt.
	 */
	attrs->lft.hard_packet_limit = lower_32_bits(start_value);
	attrs->lft.numb_rounds_hard = (u64)n;

	/* Compute soft limit initial value and number of rounds.
	 *
	 * The soft_limit is achieved by adjusting the counter's
	 * interrupt_value. This is embedded in the counting pattern created by
	 * hard packet calculations above.
	 *
	 * We wish to compute the interrupt_value for the soft_limit. This is
	 * defined by this system of equations:
	 *
	 *      soft_limit == start_value - soft_value + n * 2^31
	 *      n >= 0
	 *      soft_value < 2^32, soft_value >= 0
	 *      for n == 0 start_value > soft_value
	 *
	 * As with compute_hard_n_value() the equations are not single-solution.
	 * The algorithm selects the solution that has:
	 *      2^30 <= soft_limit < 2^31 + 2^30
	 * for the interior iterations, which guarantees a large guard band
	 * around the counter hard limit and next interrupt.
	 */

	/* Start by estimating n and compute soft_value */
	n = (x->lft.soft_packet_limit - attrs->lft.hard_packet_limit) / BIT_ULL(31);
	start_value = attrs->lft.hard_packet_limit + n * BIT_ULL(31) -
		      x->lft.soft_packet_limit;

	/* Compare against constraints and adjust n */
	if (n < 0)
		n = 0;
	else if (start_value >= BIT_ULL(32))
		n -= 1;
	else if (start_value < 0)
		n += 1;

	/* Choose the best of the two solutions: */
	start_value = attrs->lft.hard_packet_limit + n * BIT_ULL(31) - start_value;
	if (n != attrs->lft.numb_rounds_hard && start_value < BIT_ULL(30))
		n += 1;

	/* Note that the upper limit of soft_value happens naturally because we
	 * always select the lowest soft_value.
	 */

	/* Computed values solve the system of equations: */
	start_value = attrs->lft.hard_packet_limit + n * BIT_ULL(31) - start_value;

	/* The best solution means: when there are multiple iterations we must
	 * not fall below 2^30 as that would get too close to the false
	 * hard_limit and when we reach an interior iteration for soft_limit it
	 * has to be far away from 2**32-1 which is the counter reset point
	 * after the +2^31 to accommodate latency.
	 */
	attrs->lft.soft_packet_limit = lower_32_bits(start_value);
	attrs->lft.numb_rounds_soft = (u64)n;
}

static void mlx5e_ipsec_init_macs(struct mlx5e_ipsec_sa_entry *sa_entry,
				  struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	struct net_device *netdev = sa_entry->dev;
	struct neighbour *n;
	u8 addr[ETH_ALEN];
	const void *pkey;
	u8 *dst, *src;

	if (attrs->mode != XFRM_MODE_TUNNEL ||
	    attrs->type != XFRM_DEV_OFFLOAD_PACKET)
		return;

	mlx5_query_mac_address(mdev, addr);
	switch (attrs->dir) {
	case XFRM_DEV_OFFLOAD_IN:
		src = attrs->dmac;
		dst = attrs->smac;
		pkey = &attrs->addrs.saddr.a4;
		break;
	case XFRM_DEV_OFFLOAD_OUT:
		src = attrs->smac;
		dst = attrs->dmac;
		pkey = &attrs->addrs.daddr.a4;
		break;
	default:
		return;
	}

	ether_addr_copy(src, addr);
	n = neigh_lookup(&arp_tbl, pkey, netdev);
	if (!n) {
		n = neigh_create(&arp_tbl, pkey, netdev);
		if (IS_ERR(n))
			return;
		neigh_event_send(n, NULL);
		attrs->drop = true;
	} else {
		neigh_ha_snapshot(addr, n, netdev);
		ether_addr_copy(dst, addr);
	}
	neigh_release(n);
}

static void mlx5e_ipsec_state_mask(struct mlx5e_ipsec_addr *addrs)
{
	/*
	 * State doesn't have subnet prefixes in outer headers.
	 * The match is performed for exaxt source/destination addresses.
	 */
	memset(addrs->smask.m6, 0xFF, sizeof(__be32) * 4);
	memset(addrs->dmask.m6, 0xFF, sizeof(__be32) * 4);
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

	attrs->dir = x->xso.dir;

	/* esn */
	if (x->props.flags & XFRM_STATE_ESN) {
		attrs->replay_esn.trigger = true;
		attrs->replay_esn.esn = sa_entry->esn_state.esn;
		attrs->replay_esn.esn_msb = sa_entry->esn_state.esn_msb;
		attrs->replay_esn.overlap = sa_entry->esn_state.overlap;
		if (attrs->dir == XFRM_DEV_OFFLOAD_OUT)
			goto skip_replay_window;

		switch (x->replay_esn->replay_window) {
		case 32:
			attrs->replay_esn.replay_window =
				MLX5_IPSEC_ASO_REPLAY_WIN_32BIT;
			break;
		case 64:
			attrs->replay_esn.replay_window =
				MLX5_IPSEC_ASO_REPLAY_WIN_64BIT;
			break;
		case 128:
			attrs->replay_esn.replay_window =
				MLX5_IPSEC_ASO_REPLAY_WIN_128BIT;
			break;
		case 256:
			attrs->replay_esn.replay_window =
				MLX5_IPSEC_ASO_REPLAY_WIN_256BIT;
			break;
		default:
			WARN_ON(true);
			return;
		}
	}

skip_replay_window:
	/* spi */
	attrs->spi = be32_to_cpu(x->id.spi);

	/* source , destination ips */
	memcpy(&attrs->addrs.saddr, x->props.saddr.a6,
	       sizeof(attrs->addrs.saddr));
	memcpy(&attrs->addrs.daddr, x->id.daddr.a6, sizeof(attrs->addrs.daddr));
	attrs->addrs.family = x->props.family;
	mlx5e_ipsec_state_mask(&attrs->addrs);
	attrs->type = x->xso.type;
	attrs->reqid = x->props.reqid;
	attrs->upspec.dport = ntohs(x->sel.dport);
	attrs->upspec.dport_mask = ntohs(x->sel.dport_mask);
	attrs->upspec.sport = ntohs(x->sel.sport);
	attrs->upspec.sport_mask = ntohs(x->sel.sport_mask);
	attrs->upspec.proto = x->sel.proto;
	attrs->mode = x->props.mode;

	mlx5e_ipsec_init_limits(sa_entry, attrs);
	mlx5e_ipsec_init_macs(sa_entry, attrs);

	if (x->encap) {
		attrs->encap = true;
		attrs->sport = x->encap->encap_sport;
		attrs->dport = x->encap->encap_dport;
	}
}

static int mlx5e_xfrm_validate_state(struct mlx5_core_dev *mdev,
				     struct xfrm_state *x,
				     struct netlink_ext_ack *extack)
{
	if (x->props.aalgo != SADB_AALG_NONE) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload authenticated xfrm states");
		return -EINVAL;
	}
	if (x->props.ealgo != SADB_X_EALG_AES_GCM_ICV16) {
		NL_SET_ERR_MSG_MOD(extack, "Only AES-GCM-ICV16 xfrm state may be offloaded");
		return -EINVAL;
	}
	if (x->props.calgo != SADB_X_CALG_NONE) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload compressed xfrm states");
		return -EINVAL;
	}
	if (x->props.flags & XFRM_STATE_ESN &&
	    !(mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_ESN)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload ESN xfrm states");
		return -EINVAL;
	}
	if (x->props.family != AF_INET &&
	    x->props.family != AF_INET6) {
		NL_SET_ERR_MSG_MOD(extack, "Only IPv4/6 xfrm states may be offloaded");
		return -EINVAL;
	}
	if (x->id.proto != IPPROTO_ESP) {
		NL_SET_ERR_MSG_MOD(extack, "Only ESP xfrm state may be offloaded");
		return -EINVAL;
	}
	if (x->encap) {
		if (!(mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_ESPINUDP)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Encapsulation is not supported");
			return -EINVAL;
		}

		if (x->encap->encap_type != UDP_ENCAP_ESPINUDP) {
			NL_SET_ERR_MSG_MOD(extack, "Encapsulation other than UDP is not supported");
			return -EINVAL;
		}

		if (x->xso.type != XFRM_DEV_OFFLOAD_PACKET) {
			NL_SET_ERR_MSG_MOD(extack, "Encapsulation is supported in packet offload mode only");
			return -EINVAL;
		}

		if (x->props.mode != XFRM_MODE_TRANSPORT) {
			NL_SET_ERR_MSG_MOD(extack, "Encapsulation is supported in transport mode only");
			return -EINVAL;
		}
	}
	if (!x->aead) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload xfrm states without aead");
		return -EINVAL;
	}
	if (x->aead->alg_icv_len != 128) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload xfrm states with AEAD ICV length other than 128bit");
		return -EINVAL;
	}
	if ((x->aead->alg_key_len != 128 + 32) &&
	    (x->aead->alg_key_len != 256 + 32)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload xfrm states with AEAD key length other than 128/256 bit");
		return -EINVAL;
	}
	if (x->tfcpad) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload xfrm states with tfc padding");
		return -EINVAL;
	}
	if (!x->geniv) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload xfrm states without geniv");
		return -EINVAL;
	}
	if (strcmp(x->geniv, "seqiv")) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload xfrm states with geniv other than seqiv");
		return -EINVAL;
	}

	if (x->sel.proto != IPPROTO_IP && x->sel.proto != IPPROTO_UDP &&
	    x->sel.proto != IPPROTO_TCP) {
		NL_SET_ERR_MSG_MOD(extack, "Device does not support upper protocol other than TCP/UDP");
		return -EINVAL;
	}

	if (x->props.mode != XFRM_MODE_TRANSPORT && x->props.mode != XFRM_MODE_TUNNEL) {
		NL_SET_ERR_MSG_MOD(extack, "Only transport and tunnel xfrm states may be offloaded");
		return -EINVAL;
	}

	switch (x->xso.type) {
	case XFRM_DEV_OFFLOAD_CRYPTO:
		if (!(mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_CRYPTO)) {
			NL_SET_ERR_MSG_MOD(extack, "Crypto offload is not supported");
			return -EINVAL;
		}

		break;
	case XFRM_DEV_OFFLOAD_PACKET:
		if (!(mlx5_ipsec_device_caps(mdev) &
		      MLX5_IPSEC_CAP_PACKET_OFFLOAD)) {
			NL_SET_ERR_MSG_MOD(extack, "Packet offload is not supported");
			return -EINVAL;
		}

		if (x->props.mode == XFRM_MODE_TUNNEL &&
		    !(mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_TUNNEL)) {
			NL_SET_ERR_MSG_MOD(extack, "Packet offload is not supported for tunnel mode");
			return -EINVAL;
		}

		if (x->replay_esn && x->xso.dir == XFRM_DEV_OFFLOAD_IN &&
		    x->replay_esn->replay_window != 32 &&
		    x->replay_esn->replay_window != 64 &&
		    x->replay_esn->replay_window != 128 &&
		    x->replay_esn->replay_window != 256) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported replay window size");
			return -EINVAL;
		}

		if (!x->props.reqid) {
			NL_SET_ERR_MSG_MOD(extack, "Cannot offload without reqid");
			return -EINVAL;
		}

		if (x->lft.soft_byte_limit >= x->lft.hard_byte_limit &&
		    x->lft.hard_byte_limit != XFRM_INF) {
			/* XFRM stack doesn't prevent such configuration :(. */
			NL_SET_ERR_MSG_MOD(extack, "Hard byte limit must be greater than soft one");
			return -EINVAL;
		}

		if (!x->lft.soft_byte_limit || !x->lft.hard_byte_limit) {
			NL_SET_ERR_MSG_MOD(extack, "Soft/hard byte limits can't be 0");
			return -EINVAL;
		}

		if (x->lft.soft_packet_limit >= x->lft.hard_packet_limit &&
		    x->lft.hard_packet_limit != XFRM_INF) {
			/* XFRM stack doesn't prevent such configuration :(. */
			NL_SET_ERR_MSG_MOD(extack, "Hard packet limit must be greater than soft one");
			return -EINVAL;
		}

		if (!x->lft.soft_packet_limit || !x->lft.hard_packet_limit) {
			NL_SET_ERR_MSG_MOD(extack, "Soft/hard packet limits can't be 0");
			return -EINVAL;
		}
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unsupported xfrm offload type");
		return -EINVAL;
	}
	return 0;
}

static void mlx5e_ipsec_modify_state(struct work_struct *_work)
{
	struct mlx5e_ipsec_work *work =
		container_of(_work, struct mlx5e_ipsec_work, work);
	struct mlx5e_ipsec_sa_entry *sa_entry = work->sa_entry;
	struct mlx5_accel_esp_xfrm_attrs *attrs;

	attrs = &((struct mlx5e_ipsec_sa_entry *)work->data)->attrs;

	mlx5_accel_esp_modify_xfrm(sa_entry, attrs);
}

static void mlx5e_ipsec_set_esn_ops(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct xfrm_state *x = sa_entry->x;

	if (x->xso.type != XFRM_DEV_OFFLOAD_CRYPTO ||
	    x->xso.dir != XFRM_DEV_OFFLOAD_OUT)
		return;

	if (x->props.flags & XFRM_STATE_ESN) {
		sa_entry->set_iv_op = mlx5e_ipsec_set_iv_esn;
		return;
	}

	sa_entry->set_iv_op = mlx5e_ipsec_set_iv;
}

static void mlx5e_ipsec_handle_netdev_event(struct work_struct *_work)
{
	struct mlx5e_ipsec_work *work =
		container_of(_work, struct mlx5e_ipsec_work, work);
	struct mlx5e_ipsec_sa_entry *sa_entry = work->sa_entry;
	struct mlx5e_ipsec_netevent_data *data = work->data;
	struct mlx5_accel_esp_xfrm_attrs *attrs;

	attrs = &sa_entry->attrs;

	switch (attrs->dir) {
	case XFRM_DEV_OFFLOAD_IN:
		ether_addr_copy(attrs->smac, data->addr);
		break;
	case XFRM_DEV_OFFLOAD_OUT:
		ether_addr_copy(attrs->dmac, data->addr);
		break;
	default:
		WARN_ON_ONCE(true);
	}
	attrs->drop = false;
	mlx5e_accel_ipsec_fs_modify(sa_entry);
}

static int mlx5_ipsec_create_work(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct xfrm_state *x = sa_entry->x;
	struct mlx5e_ipsec_work *work;
	void *data = NULL;

	switch (x->xso.type) {
	case XFRM_DEV_OFFLOAD_CRYPTO:
		if (!(x->props.flags & XFRM_STATE_ESN))
			return 0;
		break;
	case XFRM_DEV_OFFLOAD_PACKET:
		if (x->props.mode != XFRM_MODE_TUNNEL)
			return 0;
		break;
	default:
		break;
	}

	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	switch (x->xso.type) {
	case XFRM_DEV_OFFLOAD_CRYPTO:
		data = kzalloc(sizeof(*sa_entry), GFP_KERNEL);
		if (!data)
			goto free_work;

		INIT_WORK(&work->work, mlx5e_ipsec_modify_state);
		break;
	case XFRM_DEV_OFFLOAD_PACKET:
		data = kzalloc(sizeof(struct mlx5e_ipsec_netevent_data),
			       GFP_KERNEL);
		if (!data)
			goto free_work;

		INIT_WORK(&work->work, mlx5e_ipsec_handle_netdev_event);
		break;
	default:
		break;
	}

	work->data = data;
	work->sa_entry = sa_entry;
	sa_entry->work = work;
	return 0;

free_work:
	kfree(work);
	return -ENOMEM;
}

static int mlx5e_ipsec_create_dwork(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct xfrm_state *x = sa_entry->x;
	struct mlx5e_ipsec_dwork *dwork;

	if (x->xso.type != XFRM_DEV_OFFLOAD_PACKET)
		return 0;

	if (x->lft.soft_packet_limit == XFRM_INF &&
	    x->lft.hard_packet_limit == XFRM_INF &&
	    x->lft.soft_byte_limit == XFRM_INF &&
	    x->lft.hard_byte_limit == XFRM_INF)
		return 0;

	dwork = kzalloc(sizeof(*dwork), GFP_KERNEL);
	if (!dwork)
		return -ENOMEM;

	dwork->sa_entry = sa_entry;
	INIT_DELAYED_WORK(&dwork->dwork, mlx5e_ipsec_handle_sw_limits);
	sa_entry->dwork = dwork;
	return 0;
}

static int mlx5e_xfrm_add_state(struct net_device *dev,
				struct xfrm_state *x,
				struct netlink_ext_ack *extack)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = NULL;
	struct mlx5e_ipsec *ipsec;
	struct mlx5e_priv *priv;
	gfp_t gfp;
	int err;

	priv = netdev_priv(dev);
	if (!priv->ipsec)
		return -EOPNOTSUPP;

	ipsec = priv->ipsec;
	gfp = (x->xso.flags & XFRM_DEV_OFFLOAD_FLAG_ACQ) ? GFP_ATOMIC : GFP_KERNEL;
	sa_entry = kzalloc(sizeof(*sa_entry), gfp);
	if (!sa_entry)
		return -ENOMEM;

	sa_entry->x = x;
	sa_entry->dev = dev;
	sa_entry->ipsec = ipsec;
	/* Check if this SA is originated from acquire flow temporary SA */
	if (x->xso.flags & XFRM_DEV_OFFLOAD_FLAG_ACQ)
		goto out;

	err = mlx5e_xfrm_validate_state(priv->mdev, x, extack);
	if (err)
		goto err_xfrm;

	if (!mlx5_eswitch_block_ipsec(priv->mdev)) {
		err = -EBUSY;
		goto err_xfrm;
	}

	/* check esn */
	if (x->props.flags & XFRM_STATE_ESN)
		mlx5e_ipsec_update_esn_state(sa_entry);
	else
		/* According to RFC4303, section "3.3.3. Sequence Number Generation",
		 * the first packet sent using a given SA will contain a sequence
		 * number of 1.
		 */
		sa_entry->esn_state.esn = 1;

	mlx5e_ipsec_build_accel_xfrm_attrs(sa_entry, &sa_entry->attrs);

	err = mlx5_ipsec_create_work(sa_entry);
	if (err)
		goto unblock_ipsec;

	err = mlx5e_ipsec_create_dwork(sa_entry);
	if (err)
		goto release_work;

	/* create hw context */
	err = mlx5_ipsec_create_sa_ctx(sa_entry);
	if (err)
		goto release_dwork;

	err = mlx5e_accel_ipsec_fs_add_rule(sa_entry);
	if (err)
		goto err_hw_ctx;

	if (x->props.mode == XFRM_MODE_TUNNEL &&
	    x->xso.type == XFRM_DEV_OFFLOAD_PACKET &&
	    !mlx5e_ipsec_fs_tunnel_enabled(sa_entry)) {
		NL_SET_ERR_MSG_MOD(extack, "Packet offload tunnel mode is disabled due to encap settings");
		err = -EINVAL;
		goto err_add_rule;
	}

	/* We use *_bh() variant because xfrm_timer_handler(), which runs
	 * in softirq context, can reach our state delete logic and we need
	 * xa_erase_bh() there.
	 */
	err = xa_insert_bh(&ipsec->sadb, sa_entry->ipsec_obj_id, sa_entry,
			   GFP_KERNEL);
	if (err)
		goto err_add_rule;

	mlx5e_ipsec_set_esn_ops(sa_entry);

	if (sa_entry->dwork)
		queue_delayed_work(ipsec->wq, &sa_entry->dwork->dwork,
				   MLX5_IPSEC_RESCHED);

	if (x->xso.type == XFRM_DEV_OFFLOAD_PACKET &&
	    x->props.mode == XFRM_MODE_TUNNEL) {
		xa_lock_bh(&ipsec->sadb);
		__xa_set_mark(&ipsec->sadb, sa_entry->ipsec_obj_id,
			      MLX5E_IPSEC_TUNNEL_SA);
		xa_unlock_bh(&ipsec->sadb);
	}

out:
	x->xso.offload_handle = (unsigned long)sa_entry;
	return 0;

err_add_rule:
	mlx5e_accel_ipsec_fs_del_rule(sa_entry);
err_hw_ctx:
	mlx5_ipsec_free_sa_ctx(sa_entry);
release_dwork:
	kfree(sa_entry->dwork);
release_work:
	if (sa_entry->work)
		kfree(sa_entry->work->data);
	kfree(sa_entry->work);
unblock_ipsec:
	mlx5_eswitch_unblock_ipsec(priv->mdev);
err_xfrm:
	kfree(sa_entry);
	NL_SET_ERR_MSG_WEAK_MOD(extack, "Device failed to offload this state");
	return err;
}

static void mlx5e_xfrm_del_state(struct net_device *dev, struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = to_ipsec_sa_entry(x);
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5e_ipsec_sa_entry *old;

	if (x->xso.flags & XFRM_DEV_OFFLOAD_FLAG_ACQ)
		return;

	old = xa_erase_bh(&ipsec->sadb, sa_entry->ipsec_obj_id);
	WARN_ON(old != sa_entry);
}

static void mlx5e_xfrm_free_state(struct net_device *dev, struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = to_ipsec_sa_entry(x);
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;

	if (x->xso.flags & XFRM_DEV_OFFLOAD_FLAG_ACQ)
		goto sa_entry_free;

	if (sa_entry->work)
		cancel_work_sync(&sa_entry->work->work);

	if (sa_entry->dwork)
		cancel_delayed_work_sync(&sa_entry->dwork->dwork);

	mlx5e_accel_ipsec_fs_del_rule(sa_entry);
	mlx5_ipsec_free_sa_ctx(sa_entry);
	kfree(sa_entry->dwork);
	if (sa_entry->work)
		kfree(sa_entry->work->data);
	kfree(sa_entry->work);
	mlx5_eswitch_unblock_ipsec(ipsec->mdev);
sa_entry_free:
	kfree(sa_entry);
}

static int mlx5e_ipsec_netevent_event(struct notifier_block *nb,
				      unsigned long event, void *ptr)
{
	struct mlx5_accel_esp_xfrm_attrs *attrs;
	struct mlx5e_ipsec_netevent_data *data;
	struct mlx5e_ipsec_sa_entry *sa_entry;
	struct mlx5e_ipsec *ipsec;
	struct neighbour *n = ptr;
	unsigned long idx;

	if (event != NETEVENT_NEIGH_UPDATE || !(n->nud_state & NUD_VALID))
		return NOTIFY_DONE;

	ipsec = container_of(nb, struct mlx5e_ipsec, netevent_nb);
	xa_for_each_marked(&ipsec->sadb, idx, sa_entry, MLX5E_IPSEC_TUNNEL_SA) {
		attrs = &sa_entry->attrs;

		if (attrs->addrs.family == AF_INET) {
			if (!neigh_key_eq32(n, &attrs->addrs.saddr.a4) &&
			    !neigh_key_eq32(n, &attrs->addrs.daddr.a4))
				continue;
		} else {
			if (!neigh_key_eq128(n, &attrs->addrs.saddr.a4) &&
			    !neigh_key_eq128(n, &attrs->addrs.daddr.a4))
				continue;
		}

		data = sa_entry->work->data;

		neigh_ha_snapshot(data->addr, n, sa_entry->dev);
		queue_work(ipsec->wq, &sa_entry->work->work);
	}

	return NOTIFY_DONE;
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
	init_completion(&ipsec->comp);
	ipsec->wq = alloc_workqueue("mlx5e_ipsec: %s", WQ_UNBOUND, 0,
				    priv->netdev->name);
	if (!ipsec->wq)
		goto err_wq;

	if (mlx5_ipsec_device_caps(priv->mdev) &
	    MLX5_IPSEC_CAP_PACKET_OFFLOAD) {
		ret = mlx5e_ipsec_aso_init(ipsec);
		if (ret)
			goto err_aso;
	}

	if (mlx5_ipsec_device_caps(priv->mdev) & MLX5_IPSEC_CAP_TUNNEL) {
		ipsec->netevent_nb.notifier_call = mlx5e_ipsec_netevent_event;
		ret = register_netevent_notifier(&ipsec->netevent_nb);
		if (ret)
			goto clear_aso;
	}

	ipsec->is_uplink_rep = mlx5e_is_uplink_rep(priv);
	ret = mlx5e_accel_ipsec_fs_init(ipsec, &priv->devcom);
	if (ret)
		goto err_fs_init;

	ipsec->fs = priv->fs;
	priv->ipsec = ipsec;
	netdev_dbg(priv->netdev, "IPSec attached to netdevice\n");
	return;

err_fs_init:
	if (mlx5_ipsec_device_caps(priv->mdev) & MLX5_IPSEC_CAP_TUNNEL)
		unregister_netevent_notifier(&ipsec->netevent_nb);
clear_aso:
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
	if (ipsec->netevent_nb.notifier_call) {
		unregister_netevent_notifier(&ipsec->netevent_nb);
		ipsec->netevent_nb.notifier_call = NULL;
	}
	if (ipsec->aso)
		mlx5e_ipsec_aso_cleanup(ipsec);
	destroy_workqueue(ipsec->wq);
	kfree(ipsec);
	priv->ipsec = NULL;
}

static void mlx5e_xfrm_advance_esn_state(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = to_ipsec_sa_entry(x);
	struct mlx5e_ipsec_work *work = sa_entry->work;
	struct mlx5e_ipsec_sa_entry *sa_entry_shadow;
	bool need_update;

	need_update = mlx5e_ipsec_update_esn_state(sa_entry);
	if (!need_update)
		return;

	sa_entry_shadow = work->data;
	memset(sa_entry_shadow, 0x00, sizeof(*sa_entry_shadow));
	mlx5e_ipsec_build_accel_xfrm_attrs(sa_entry, &sa_entry_shadow->attrs);
	queue_work(sa_entry->ipsec->wq, &work->work);
}

static void mlx5e_xfrm_update_stats(struct xfrm_state *x)
{
	struct mlx5e_ipsec_sa_entry *sa_entry = to_ipsec_sa_entry(x);
	struct mlx5e_ipsec_rule *ipsec_rule = &sa_entry->ipsec_rule;
	struct net *net = dev_net(x->xso.dev);
	u64 trailer_packets = 0, trailer_bytes = 0;
	u64 replay_packets = 0, replay_bytes = 0;
	u64 auth_packets = 0, auth_bytes = 0;
	u64 success_packets, success_bytes;
	u64 packets, bytes, lastuse;
	size_t headers;

	lockdep_assert(lockdep_is_held(&x->lock) ||
		       lockdep_is_held(&net->xfrm.xfrm_cfg_mutex) ||
		       lockdep_is_held(&net->xfrm.xfrm_state_lock));

	if (x->xso.flags & XFRM_DEV_OFFLOAD_FLAG_ACQ)
		return;

	if (sa_entry->attrs.dir == XFRM_DEV_OFFLOAD_IN) {
		mlx5_fc_query_cached(ipsec_rule->auth.fc, &auth_bytes,
				     &auth_packets, &lastuse);
		x->stats.integrity_failed += auth_packets;
		XFRM_ADD_STATS(net, LINUX_MIB_XFRMINSTATEPROTOERROR, auth_packets);

		mlx5_fc_query_cached(ipsec_rule->trailer.fc, &trailer_bytes,
				     &trailer_packets, &lastuse);
		XFRM_ADD_STATS(net, LINUX_MIB_XFRMINHDRERROR, trailer_packets);
	}

	if (x->xso.type != XFRM_DEV_OFFLOAD_PACKET)
		return;

	if (sa_entry->attrs.dir == XFRM_DEV_OFFLOAD_IN) {
		mlx5_fc_query_cached(ipsec_rule->replay.fc, &replay_bytes,
				     &replay_packets, &lastuse);
		x->stats.replay += replay_packets;
		XFRM_ADD_STATS(net, LINUX_MIB_XFRMINSTATESEQERROR, replay_packets);
	}

	mlx5_fc_query_cached(ipsec_rule->fc, &bytes, &packets, &lastuse);
	success_packets = packets - auth_packets - trailer_packets - replay_packets;
	x->curlft.packets += success_packets;
	/* NIC counts all bytes passed through flow steering and doesn't have
	 * an ability to count payload data size which is needed for SA.
	 *
	 * To overcome HW limitestion, let's approximate the payload size
	 * by removing always available headers.
	 */
	headers = sizeof(struct ethhdr);
	if (sa_entry->attrs.addrs.family == AF_INET)
		headers += sizeof(struct iphdr);
	else
		headers += sizeof(struct ipv6hdr);

	success_bytes = bytes - auth_bytes - trailer_bytes - replay_bytes;
	x->curlft.bytes += success_bytes - headers * success_packets;
}

static __be32 word_to_mask(int prefix)
{
	if (prefix < 0)
		return 0;

	if (!prefix || prefix > 31)
		return cpu_to_be32(0xFFFFFFFF);

	return cpu_to_be32(((1U << prefix) - 1) << (32 - prefix));
}

static void mlx5e_ipsec_policy_mask(struct mlx5e_ipsec_addr *addrs,
				    struct xfrm_selector *sel)
{
	int i;

	if (addrs->family == AF_INET) {
		addrs->smask.m4 = word_to_mask(sel->prefixlen_s);
		addrs->saddr.a4 &= addrs->smask.m4;
		addrs->dmask.m4 = word_to_mask(sel->prefixlen_d);
		addrs->daddr.a4 &= addrs->dmask.m4;
		return;
	}

	for (i = 0; i < 4; i++) {
		if (sel->prefixlen_s != 32 * i)
			addrs->smask.m6[i] =
				word_to_mask(sel->prefixlen_s - 32 * i);
		addrs->saddr.a6[i] &= addrs->smask.m6[i];

		if (sel->prefixlen_d != 32 * i)
			addrs->dmask.m6[i] =
				word_to_mask(sel->prefixlen_d - 32 * i);
		addrs->daddr.a6[i] &= addrs->dmask.m6[i];
	}
}

static int mlx5e_xfrm_validate_policy(struct mlx5_core_dev *mdev,
				      struct xfrm_policy *x,
				      struct netlink_ext_ack *extack)
{
	struct xfrm_selector *sel = &x->selector;

	if (x->type != XFRM_POLICY_TYPE_MAIN) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload non-main policy types");
		return -EINVAL;
	}

	/* Please pay attention that we support only one template */
	if (x->xfrm_nr > 1) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload more than one template");
		return -EINVAL;
	}

	if (x->xdo.dir != XFRM_DEV_OFFLOAD_IN &&
	    x->xdo.dir != XFRM_DEV_OFFLOAD_OUT) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload forward policy");
		return -EINVAL;
	}

	if (!x->xfrm_vec[0].reqid && sel->proto == IPPROTO_IP &&
	    addr6_all_zero(sel->saddr.a6) && addr6_all_zero(sel->daddr.a6)) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported policy with reqid 0 without at least one of upper protocol or ip addr(s) different than 0");
		return -EINVAL;
	}

	if (x->xdo.type != XFRM_DEV_OFFLOAD_PACKET) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported xfrm offload type");
		return -EINVAL;
	}

	if (x->selector.proto != IPPROTO_IP &&
	    x->selector.proto != IPPROTO_UDP &&
	    x->selector.proto != IPPROTO_TCP) {
		NL_SET_ERR_MSG_MOD(extack, "Device does not support upper protocol other than TCP/UDP");
		return -EINVAL;
	}

	if (x->priority) {
		if (!(mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_PRIO)) {
			NL_SET_ERR_MSG_MOD(extack, "Device does not support policy priority");
			return -EINVAL;
		}

		if (x->priority == U32_MAX) {
			NL_SET_ERR_MSG_MOD(extack, "Device does not support requested policy priority");
			return -EINVAL;
		}
	}

	if (x->xdo.type == XFRM_DEV_OFFLOAD_PACKET &&
	    !(mlx5_ipsec_device_caps(mdev) & MLX5_IPSEC_CAP_PACKET_OFFLOAD)) {
		NL_SET_ERR_MSG_MOD(extack, "Packet offload is not supported");
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

	memcpy(&attrs->addrs.saddr, sel->saddr.a6, sizeof(attrs->addrs.saddr));
	memcpy(&attrs->addrs.daddr, sel->daddr.a6, sizeof(attrs->addrs.daddr));
	attrs->addrs.family = sel->family;
	mlx5e_ipsec_policy_mask(&attrs->addrs, sel);
	attrs->dir = x->xdo.dir;
	attrs->action = x->action;
	attrs->type = XFRM_DEV_OFFLOAD_PACKET;
	attrs->reqid = x->xfrm_vec[0].reqid;
	attrs->upspec.dport = ntohs(sel->dport);
	attrs->upspec.dport_mask = ntohs(sel->dport_mask);
	attrs->upspec.sport = ntohs(sel->sport);
	attrs->upspec.sport_mask = ntohs(sel->sport_mask);
	attrs->upspec.proto = sel->proto;
	attrs->prio = x->priority;
}

static int mlx5e_xfrm_add_policy(struct xfrm_policy *x,
				 struct netlink_ext_ack *extack)
{
	struct net_device *netdev = x->xdo.dev;
	struct mlx5e_ipsec_pol_entry *pol_entry;
	struct mlx5e_priv *priv;
	int err;

	priv = netdev_priv(netdev);
	if (!priv->ipsec) {
		NL_SET_ERR_MSG_MOD(extack, "Device doesn't support IPsec packet offload");
		return -EOPNOTSUPP;
	}

	err = mlx5e_xfrm_validate_policy(priv->mdev, x, extack);
	if (err)
		return err;

	pol_entry = kzalloc(sizeof(*pol_entry), GFP_KERNEL);
	if (!pol_entry)
		return -ENOMEM;

	pol_entry->x = x;
	pol_entry->ipsec = priv->ipsec;

	if (!mlx5_eswitch_block_ipsec(priv->mdev)) {
		err = -EBUSY;
		goto ipsec_busy;
	}

	mlx5e_ipsec_build_accel_pol_attrs(pol_entry, &pol_entry->attrs);
	err = mlx5e_accel_ipsec_fs_add_pol(pol_entry);
	if (err)
		goto err_fs;

	x->xdo.offload_handle = (unsigned long)pol_entry;
	return 0;

err_fs:
	mlx5_eswitch_unblock_ipsec(priv->mdev);
ipsec_busy:
	kfree(pol_entry);
	NL_SET_ERR_MSG_MOD(extack, "Device failed to offload this policy");
	return err;
}

static void mlx5e_xfrm_del_policy(struct xfrm_policy *x)
{
	struct mlx5e_ipsec_pol_entry *pol_entry = to_ipsec_pol_entry(x);

	mlx5e_accel_ipsec_fs_del_pol(pol_entry);
	mlx5_eswitch_unblock_ipsec(pol_entry->ipsec->mdev);
}

static void mlx5e_xfrm_free_policy(struct xfrm_policy *x)
{
	struct mlx5e_ipsec_pol_entry *pol_entry = to_ipsec_pol_entry(x);

	kfree(pol_entry);
}

static const struct xfrmdev_ops mlx5e_ipsec_xfrmdev_ops = {
	.xdo_dev_state_add	= mlx5e_xfrm_add_state,
	.xdo_dev_state_delete	= mlx5e_xfrm_del_state,
	.xdo_dev_state_free	= mlx5e_xfrm_free_state,
	.xdo_dev_state_advance_esn = mlx5e_xfrm_advance_esn_state,

	.xdo_dev_state_update_stats = mlx5e_xfrm_update_stats,
	.xdo_dev_policy_add = mlx5e_xfrm_add_policy,
	.xdo_dev_policy_delete = mlx5e_xfrm_del_policy,
	.xdo_dev_policy_free = mlx5e_xfrm_free_policy,
};

void mlx5e_ipsec_build_netdev(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct net_device *netdev = priv->netdev;

	if (!mlx5_ipsec_device_caps(mdev))
		return;

	mlx5_core_info(mdev, "mlx5e: IPSec ESP acceleration enabled\n");

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
