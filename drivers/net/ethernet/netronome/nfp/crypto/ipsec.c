// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc */
/* Copyright (C) 2021 Corigine, Inc */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/unaligned.h>
#include <linux/ktime.h>
#include <net/xfrm.h>

#include "../nfpcore/nfp_dev.h"
#include "../nfp_net_ctrl.h"
#include "../nfp_net.h"
#include "crypto.h"

#define NFP_NET_IPSEC_MAX_SA_CNT  (16 * 1024) /* Firmware support a maximum of 16K SA offload */

/* IPsec config message cmd codes */
enum nfp_ipsec_cfg_mssg_cmd_codes {
	NFP_IPSEC_CFG_MSSG_ADD_SA,	 /* Add a new SA */
	NFP_IPSEC_CFG_MSSG_INV_SA	 /* Invalidate an existing SA */
};

/* IPsec config message response codes */
enum nfp_ipsec_cfg_mssg_rsp_codes {
	NFP_IPSEC_CFG_MSSG_OK,
	NFP_IPSEC_CFG_MSSG_FAILED,
	NFP_IPSEC_CFG_MSSG_SA_VALID,
	NFP_IPSEC_CFG_MSSG_SA_HASH_ADD_FAILED,
	NFP_IPSEC_CFG_MSSG_SA_HASH_DEL_FAILED,
	NFP_IPSEC_CFG_MSSG_SA_INVALID_CMD
};

/* Protocol */
enum nfp_ipsec_sa_prot {
	NFP_IPSEC_PROTOCOL_AH = 0,
	NFP_IPSEC_PROTOCOL_ESP = 1
};

/* Mode */
enum nfp_ipsec_sa_mode {
	NFP_IPSEC_PROTMODE_TRANSPORT = 0,
	NFP_IPSEC_PROTMODE_TUNNEL = 1
};

/* Cipher types */
enum nfp_ipsec_sa_cipher {
	NFP_IPSEC_CIPHER_NULL,
	NFP_IPSEC_CIPHER_3DES,
	NFP_IPSEC_CIPHER_AES128,
	NFP_IPSEC_CIPHER_AES192,
	NFP_IPSEC_CIPHER_AES256,
	NFP_IPSEC_CIPHER_AES128_NULL,
	NFP_IPSEC_CIPHER_AES192_NULL,
	NFP_IPSEC_CIPHER_AES256_NULL,
	NFP_IPSEC_CIPHER_CHACHA20
};

/* Cipher modes */
enum nfp_ipsec_sa_cipher_mode {
	NFP_IPSEC_CIMODE_ECB,
	NFP_IPSEC_CIMODE_CBC,
	NFP_IPSEC_CIMODE_CFB,
	NFP_IPSEC_CIMODE_OFB,
	NFP_IPSEC_CIMODE_CTR
};

/* Hash types */
enum nfp_ipsec_sa_hash_type {
	NFP_IPSEC_HASH_NONE,
	NFP_IPSEC_HASH_MD5_96,
	NFP_IPSEC_HASH_SHA1_96,
	NFP_IPSEC_HASH_SHA256_96,
	NFP_IPSEC_HASH_SHA384_96,
	NFP_IPSEC_HASH_SHA512_96,
	NFP_IPSEC_HASH_MD5_128,
	NFP_IPSEC_HASH_SHA1_80,
	NFP_IPSEC_HASH_SHA256_128,
	NFP_IPSEC_HASH_SHA384_192,
	NFP_IPSEC_HASH_SHA512_256,
	NFP_IPSEC_HASH_GF128_128,
	NFP_IPSEC_HASH_POLY1305_128
};

/* IPSEC_CFG_MSSG_ADD_SA */
struct nfp_ipsec_cfg_add_sa {
	u32 ciph_key[8];		  /* Cipher Key */
	union {
		u32 auth_key[16];	  /* Authentication Key */
		struct nfp_ipsec_aesgcm { /* AES-GCM-ESP fields */
			u32 salt;	  /* Initialized with SA */
			u32 resv[15];
		} aesgcm_fields;
	};
	struct sa_ctrl_word {
		uint32_t hash   :4;	  /* From nfp_ipsec_sa_hash_type */
		uint32_t cimode :4;	  /* From nfp_ipsec_sa_cipher_mode */
		uint32_t cipher :4;	  /* From nfp_ipsec_sa_cipher */
		uint32_t mode   :2;	  /* From nfp_ipsec_sa_mode */
		uint32_t proto  :2;	  /* From nfp_ipsec_sa_prot */
		uint32_t dir :1;	  /* SA direction */
		uint32_t resv0 :12;
		uint32_t encap_dsbl:1;	  /* Encap/Decap disable */
		uint32_t resv1 :2;	  /* Must be set to 0 */
	} ctrl_word;
	u32 spi;			  /* SPI Value */
	uint32_t pmtu_limit :16;          /* PMTU Limit */
	uint32_t resv0 :5;
	uint32_t ipv6       :1;		  /* Outbound IPv6 addr format */
	uint32_t resv1	 :10;
	u32 resv2[2];
	u32 src_ip[4];			  /* Src IP addr */
	u32 dst_ip[4];			  /* Dst IP addr */
	u32 resv3[6];
};

/* IPSEC_CFG_MSSG */
struct nfp_ipsec_cfg_mssg {
	union {
		struct{
			uint32_t cmd:16;     /* One of nfp_ipsec_cfg_mssg_cmd_codes */
			uint32_t rsp:16;     /* One of nfp_ipsec_cfg_mssg_rsp_codes */
			uint32_t sa_idx:16;  /* SA table index */
			uint32_t spare0:16;
			struct nfp_ipsec_cfg_add_sa cfg_add_sa;
		};
		u32 raw[64];
	};
};

static int nfp_net_ipsec_cfg(struct nfp_net *nn, struct nfp_mbox_amsg_entry *entry)
{
	unsigned int offset = nn->tlv_caps.mbox_off + NFP_NET_CFG_MBOX_SIMPLE_VAL;
	struct nfp_ipsec_cfg_mssg *msg = (struct nfp_ipsec_cfg_mssg *)entry->msg;
	int i, msg_size, ret;

	ret = nfp_net_mbox_lock(nn, sizeof(*msg));
	if (ret)
		return ret;

	msg_size = ARRAY_SIZE(msg->raw);
	for (i = 0; i < msg_size; i++)
		nn_writel(nn, offset + 4 * i, msg->raw[i]);

	ret = nfp_net_mbox_reconfig(nn, entry->cmd);
	if (ret < 0) {
		nn_ctrl_bar_unlock(nn);
		return ret;
	}

	/* For now we always read the whole message response back */
	for (i = 0; i < msg_size; i++)
		msg->raw[i] = nn_readl(nn, offset + 4 * i);

	nn_ctrl_bar_unlock(nn);

	switch (msg->rsp) {
	case NFP_IPSEC_CFG_MSSG_OK:
		return 0;
	case NFP_IPSEC_CFG_MSSG_SA_INVALID_CMD:
		return -EINVAL;
	case NFP_IPSEC_CFG_MSSG_SA_VALID:
		return -EEXIST;
	case NFP_IPSEC_CFG_MSSG_FAILED:
	case NFP_IPSEC_CFG_MSSG_SA_HASH_ADD_FAILED:
	case NFP_IPSEC_CFG_MSSG_SA_HASH_DEL_FAILED:
		return -EIO;
	default:
		return -EINVAL;
	}
}

static int set_aes_keylen(struct nfp_ipsec_cfg_add_sa *cfg, int alg, int keylen)
{
	bool aes_gmac = (alg == SADB_X_EALG_NULL_AES_GMAC);

	switch (keylen) {
	case 128:
		cfg->ctrl_word.cipher = aes_gmac ? NFP_IPSEC_CIPHER_AES128_NULL :
						   NFP_IPSEC_CIPHER_AES128;
		break;
	case 192:
		cfg->ctrl_word.cipher = aes_gmac ? NFP_IPSEC_CIPHER_AES192_NULL :
						   NFP_IPSEC_CIPHER_AES192;
		break;
	case 256:
		cfg->ctrl_word.cipher = aes_gmac ? NFP_IPSEC_CIPHER_AES256_NULL :
						   NFP_IPSEC_CIPHER_AES256;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void set_md5hmac(struct nfp_ipsec_cfg_add_sa *cfg, int *trunc_len)
{
	switch (*trunc_len) {
	case 96:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_MD5_96;
		break;
	case 128:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_MD5_128;
		break;
	default:
		*trunc_len = 0;
	}
}

static void set_sha1hmac(struct nfp_ipsec_cfg_add_sa *cfg, int *trunc_len)
{
	switch (*trunc_len) {
	case 96:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_SHA1_96;
		break;
	case 80:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_SHA1_80;
		break;
	default:
		*trunc_len = 0;
	}
}

static void set_sha2_256hmac(struct nfp_ipsec_cfg_add_sa *cfg, int *trunc_len)
{
	switch (*trunc_len) {
	case 96:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_SHA256_96;
		break;
	case 128:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_SHA256_128;
		break;
	default:
		*trunc_len = 0;
	}
}

static void set_sha2_384hmac(struct nfp_ipsec_cfg_add_sa *cfg, int *trunc_len)
{
	switch (*trunc_len) {
	case 96:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_SHA384_96;
		break;
	case 192:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_SHA384_192;
		break;
	default:
		*trunc_len = 0;
	}
}

static void set_sha2_512hmac(struct nfp_ipsec_cfg_add_sa *cfg, int *trunc_len)
{
	switch (*trunc_len) {
	case 96:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_SHA512_96;
		break;
	case 256:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_SHA512_256;
		break;
	default:
		*trunc_len = 0;
	}
}

static int nfp_net_xfrm_add_state(struct net_device *dev,
				  struct xfrm_state *x,
				  struct netlink_ext_ack *extack)
{
	struct nfp_ipsec_cfg_mssg msg = {};
	int i, key_len, trunc_len, err = 0;
	struct nfp_ipsec_cfg_add_sa *cfg;
	struct nfp_net *nn;
	unsigned int saidx;

	nn = netdev_priv(dev);
	cfg = &msg.cfg_add_sa;

	/* General */
	switch (x->props.mode) {
	case XFRM_MODE_TUNNEL:
		cfg->ctrl_word.mode = NFP_IPSEC_PROTMODE_TUNNEL;
		break;
	case XFRM_MODE_TRANSPORT:
		cfg->ctrl_word.mode = NFP_IPSEC_PROTMODE_TRANSPORT;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unsupported mode for xfrm offload");
		return -EINVAL;
	}

	switch (x->id.proto) {
	case IPPROTO_ESP:
		cfg->ctrl_word.proto = NFP_IPSEC_PROTOCOL_ESP;
		break;
	case IPPROTO_AH:
		cfg->ctrl_word.proto = NFP_IPSEC_PROTOCOL_AH;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unsupported protocol for xfrm offload");
		return -EINVAL;
	}

	if (x->props.flags & XFRM_STATE_ESN) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported XFRM_REPLAY_MODE_ESN for xfrm offload");
		return -EINVAL;
	}

	if (x->xso.type != XFRM_DEV_OFFLOAD_CRYPTO) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported xfrm offload type");
		return -EINVAL;
	}

	cfg->spi = ntohl(x->id.spi);

	/* Hash/Authentication */
	if (x->aalg)
		trunc_len = x->aalg->alg_trunc_len;
	else
		trunc_len = 0;

	switch (x->props.aalgo) {
	case SADB_AALG_NONE:
		if (x->aead) {
			trunc_len = -1;
		} else {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported authentication algorithm");
			return -EINVAL;
		}
		break;
	case SADB_X_AALG_NULL:
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_NONE;
		trunc_len = -1;
		break;
	case SADB_AALG_MD5HMAC:
		if (nn->pdev->device == PCI_DEVICE_ID_NFP3800) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported authentication algorithm");
			return -EINVAL;
		}
		set_md5hmac(cfg, &trunc_len);
		break;
	case SADB_AALG_SHA1HMAC:
		set_sha1hmac(cfg, &trunc_len);
		break;
	case SADB_X_AALG_SHA2_256HMAC:
		set_sha2_256hmac(cfg, &trunc_len);
		break;
	case SADB_X_AALG_SHA2_384HMAC:
		set_sha2_384hmac(cfg, &trunc_len);
		break;
	case SADB_X_AALG_SHA2_512HMAC:
		set_sha2_512hmac(cfg, &trunc_len);
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unsupported authentication algorithm");
		return -EINVAL;
	}

	if (!trunc_len) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported authentication algorithm trunc length");
		return -EINVAL;
	}

	if (x->aalg) {
		key_len = DIV_ROUND_UP(x->aalg->alg_key_len, BITS_PER_BYTE);
		if (key_len > sizeof(cfg->auth_key)) {
			NL_SET_ERR_MSG_MOD(extack, "Insufficient space for offloaded auth key");
			return -EINVAL;
		}
		for (i = 0; i < key_len / sizeof(cfg->auth_key[0]) ; i++)
			cfg->auth_key[i] = get_unaligned_be32(x->aalg->alg_key +
							      sizeof(cfg->auth_key[0]) * i);
	}

	/* Encryption */
	switch (x->props.ealgo) {
	case SADB_EALG_NONE:
		/* The xfrm descriptor for CHACAH20_POLY1305 does not set the algorithm id, which
		 * is the default value SADB_EALG_NONE. In the branch of SADB_EALG_NONE, driver
		 * uses algorithm name to identify CHACAH20_POLY1305's algorithm.
		 */
		if (x->aead && !strcmp(x->aead->alg_name, "rfc7539esp(chacha20,poly1305)")) {
			if (nn->pdev->device != PCI_DEVICE_ID_NFP3800) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Unsupported encryption algorithm for offload");
				return -EINVAL;
			}
			if (x->aead->alg_icv_len != 128) {
				NL_SET_ERR_MSG_MOD(extack,
						   "ICV must be 128bit with CHACHA20_POLY1305");
				return -EINVAL;
			}

			/* Aead->alg_key_len includes 32-bit salt */
			if (x->aead->alg_key_len - 32 != 256) {
				NL_SET_ERR_MSG_MOD(extack, "Unsupported CHACHA20 key length");
				return -EINVAL;
			}

			/* The CHACHA20's mode is not configured */
			cfg->ctrl_word.hash = NFP_IPSEC_HASH_POLY1305_128;
			cfg->ctrl_word.cipher = NFP_IPSEC_CIPHER_CHACHA20;
			break;
		}
		fallthrough;
	case SADB_EALG_NULL:
		cfg->ctrl_word.cimode = NFP_IPSEC_CIMODE_CBC;
		cfg->ctrl_word.cipher = NFP_IPSEC_CIPHER_NULL;
		break;
	case SADB_EALG_3DESCBC:
		if (nn->pdev->device == PCI_DEVICE_ID_NFP3800) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported encryption algorithm for offload");
			return -EINVAL;
		}
		cfg->ctrl_word.cimode = NFP_IPSEC_CIMODE_CBC;
		cfg->ctrl_word.cipher = NFP_IPSEC_CIPHER_3DES;
		break;
	case SADB_X_EALG_AES_GCM_ICV16:
	case SADB_X_EALG_NULL_AES_GMAC:
		if (!x->aead) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid AES key data");
			return -EINVAL;
		}

		if (x->aead->alg_icv_len != 128) {
			NL_SET_ERR_MSG_MOD(extack, "ICV must be 128bit with SADB_X_EALG_AES_GCM_ICV16");
			return -EINVAL;
		}
		cfg->ctrl_word.cimode = NFP_IPSEC_CIMODE_CTR;
		cfg->ctrl_word.hash = NFP_IPSEC_HASH_GF128_128;

		/* Aead->alg_key_len includes 32-bit salt */
		if (set_aes_keylen(cfg, x->props.ealgo, x->aead->alg_key_len - 32)) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported AES key length");
			return -EINVAL;
		}
		break;
	case SADB_X_EALG_AESCBC:
		cfg->ctrl_word.cimode = NFP_IPSEC_CIMODE_CBC;
		if (!x->ealg) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid AES key data");
			return -EINVAL;
		}
		if (set_aes_keylen(cfg, x->props.ealgo, x->ealg->alg_key_len) < 0) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported AES key length");
			return -EINVAL;
		}
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unsupported encryption algorithm for offload");
		return -EINVAL;
	}

	if (x->aead) {
		int key_offset = 0;
		int salt_len = 4;

		key_len = DIV_ROUND_UP(x->aead->alg_key_len, BITS_PER_BYTE);
		key_len -= salt_len;

		if (key_len > sizeof(cfg->ciph_key)) {
			NL_SET_ERR_MSG_MOD(extack, "aead: Insufficient space for offloaded key");
			return -EINVAL;
		}

		/* The CHACHA20's key order needs to be adjusted based on hardware design.
		 * Other's key order: {K0, K1, K2, K3, K4, K5, K6, K7}
		 * CHACHA20's key order: {K4, K5, K6, K7, K0, K1, K2, K3}
		 */
		if (!strcmp(x->aead->alg_name, "rfc7539esp(chacha20,poly1305)"))
			key_offset = key_len / sizeof(cfg->ciph_key[0]) >> 1;

		for (i = 0; i < key_len / sizeof(cfg->ciph_key[0]); i++) {
			int index = (i + key_offset) % (key_len / sizeof(cfg->ciph_key[0]));

			cfg->ciph_key[index] = get_unaligned_be32(x->aead->alg_key +
								  sizeof(cfg->ciph_key[0]) * i);
		}

		/* Load up the salt */
		cfg->aesgcm_fields.salt = get_unaligned_be32(x->aead->alg_key + key_len);
	}

	if (x->ealg) {
		key_len = DIV_ROUND_UP(x->ealg->alg_key_len, BITS_PER_BYTE);

		if (key_len > sizeof(cfg->ciph_key)) {
			NL_SET_ERR_MSG_MOD(extack, "ealg: Insufficient space for offloaded key");
			return -EINVAL;
		}
		for (i = 0; i < key_len / sizeof(cfg->ciph_key[0]) ; i++)
			cfg->ciph_key[i] = get_unaligned_be32(x->ealg->alg_key +
							      sizeof(cfg->ciph_key[0]) * i);
	}

	/* IP related info */
	switch (x->props.family) {
	case AF_INET:
		cfg->ipv6 = 0;
		cfg->src_ip[0] = ntohl(x->props.saddr.a4);
		cfg->dst_ip[0] = ntohl(x->id.daddr.a4);
		break;
	case AF_INET6:
		cfg->ipv6 = 1;
		for (i = 0; i < 4; i++) {
			cfg->src_ip[i] = ntohl(x->props.saddr.a6[i]);
			cfg->dst_ip[i] = ntohl(x->id.daddr.a6[i]);
		}
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unsupported address family");
		return -EINVAL;
	}

	/* Maximum nic IPsec code could handle. Other limits may apply. */
	cfg->pmtu_limit = 0xffff;
	cfg->ctrl_word.encap_dsbl = 1;

	/* SA direction */
	cfg->ctrl_word.dir = x->xso.dir;

	/* Find unused SA data*/
	err = xa_alloc(&nn->xa_ipsec, &saidx, x,
		       XA_LIMIT(0, NFP_NET_IPSEC_MAX_SA_CNT - 1), GFP_KERNEL);
	if (err < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to get sa_data number for IPsec");
		return err;
	}

	/* Allocate saidx and commit the SA */
	msg.cmd = NFP_IPSEC_CFG_MSSG_ADD_SA;
	msg.sa_idx = saidx;
	err = nfp_net_sched_mbox_amsg_work(nn, NFP_NET_CFG_MBOX_CMD_IPSEC, &msg,
					   sizeof(msg), nfp_net_ipsec_cfg);
	if (err) {
		xa_erase(&nn->xa_ipsec, saidx);
		NL_SET_ERR_MSG_MOD(extack, "Failed to issue IPsec command");
		return err;
	}

	/* 0 is invalid offload_handle for kernel */
	x->xso.offload_handle = saidx + 1;
	return 0;
}

static void nfp_net_xfrm_del_state(struct net_device *dev, struct xfrm_state *x)
{
	struct nfp_ipsec_cfg_mssg msg = {
		.cmd = NFP_IPSEC_CFG_MSSG_INV_SA,
		.sa_idx = x->xso.offload_handle - 1,
	};
	struct nfp_net *nn;
	int err;

	nn = netdev_priv(dev);
	err = nfp_net_sched_mbox_amsg_work(nn, NFP_NET_CFG_MBOX_CMD_IPSEC, &msg,
					   sizeof(msg), nfp_net_ipsec_cfg);
	if (err)
		nn_warn(nn, "Failed to invalidate SA in hardware\n");

	xa_erase(&nn->xa_ipsec, x->xso.offload_handle - 1);
}

static const struct xfrmdev_ops nfp_net_ipsec_xfrmdev_ops = {
	.xdo_dev_state_add = nfp_net_xfrm_add_state,
	.xdo_dev_state_delete = nfp_net_xfrm_del_state,
};

void nfp_net_ipsec_init(struct nfp_net *nn)
{
	if (!(nn->cap_w1 & NFP_NET_CFG_CTRL_IPSEC))
		return;

	xa_init_flags(&nn->xa_ipsec, XA_FLAGS_ALLOC);
	nn->dp.netdev->xfrmdev_ops = &nfp_net_ipsec_xfrmdev_ops;
}

void nfp_net_ipsec_clean(struct nfp_net *nn)
{
	if (!(nn->cap_w1 & NFP_NET_CFG_CTRL_IPSEC))
		return;

	WARN_ON(!xa_empty(&nn->xa_ipsec));
	xa_destroy(&nn->xa_ipsec);
}

bool nfp_net_ipsec_tx_prep(struct nfp_net_dp *dp, struct sk_buff *skb,
			   struct nfp_ipsec_offload *offload_info)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct xfrm_state *x;

	x = xfrm_input_state(skb);
	if (!x)
		return false;

	offload_info->seq_hi = xo->seq.hi;
	offload_info->seq_low = xo->seq.low;
	offload_info->handle = x->xso.offload_handle;

	return true;
}

int nfp_net_ipsec_rx(struct nfp_meta_parsed *meta, struct sk_buff *skb)
{
	struct net_device *netdev = skb->dev;
	struct xfrm_offload *xo;
	struct xfrm_state *x;
	struct sec_path *sp;
	struct nfp_net *nn;
	u32 saidx;

	nn = netdev_priv(netdev);

	saidx = meta->ipsec_saidx - 1;
	if (saidx >= NFP_NET_IPSEC_MAX_SA_CNT)
		return -EINVAL;

	sp = secpath_set(skb);
	if (unlikely(!sp))
		return -ENOMEM;

	xa_lock(&nn->xa_ipsec);
	x = xa_load(&nn->xa_ipsec, saidx);
	xa_unlock(&nn->xa_ipsec);
	if (!x)
		return -EINVAL;

	xfrm_state_hold(x);
	sp->xvec[sp->len++] = x;
	sp->olen++;
	xo = xfrm_offload(skb);
	xo->flags = CRYPTO_DONE;
	xo->status = CRYPTO_SUCCESS;

	return 0;
}
