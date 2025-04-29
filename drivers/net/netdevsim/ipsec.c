// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Oracle and/or its affiliates. All rights reserved. */

#include <crypto/aead.h>
#include <linux/debugfs.h>
#include <net/xfrm.h>

#include "netdevsim.h"

#define NSIM_IPSEC_AUTH_BITS	128

static ssize_t nsim_dbg_netdev_ops_read(struct file *filp,
					char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct netdevsim *ns = filp->private_data;
	struct nsim_ipsec *ipsec = &ns->ipsec;
	size_t bufsize;
	char *buf, *p;
	int len;
	int i;

	/* the buffer needed is
	 * (num SAs * 3 lines each * ~60 bytes per line) + one more line
	 */
	bufsize = (ipsec->count * 4 * 60) + 60;
	buf = kzalloc(bufsize, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	p = buf;
	p += scnprintf(p, bufsize - (p - buf),
		       "SA count=%u tx=%u\n",
		       ipsec->count, ipsec->tx);

	for (i = 0; i < NSIM_IPSEC_MAX_SA_COUNT; i++) {
		struct nsim_sa *sap = &ipsec->sa[i];

		if (!sap->used)
			continue;

		if (sap->xs->props.family == AF_INET6)
			p += scnprintf(p, bufsize - (p - buf),
				       "sa[%i] %cx ipaddr=%pI6c\n",
				       i, (sap->rx ? 'r' : 't'), &sap->ipaddr);
		else
			p += scnprintf(p, bufsize - (p - buf),
				       "sa[%i] %cx ipaddr=%pI4\n",
				       i, (sap->rx ? 'r' : 't'), &sap->ipaddr[3]);
		p += scnprintf(p, bufsize - (p - buf),
			       "sa[%i]    spi=0x%08x proto=0x%x salt=0x%08x crypt=%d\n",
			       i, be32_to_cpu(sap->xs->id.spi),
			       sap->xs->id.proto, sap->salt, sap->crypt);
		p += scnprintf(p, bufsize - (p - buf),
			       "sa[%i]    key=0x%08x %08x %08x %08x\n",
			       i, sap->key[0], sap->key[1],
			       sap->key[2], sap->key[3]);
	}

	len = simple_read_from_buffer(buffer, count, ppos, buf, p - buf);

	kfree(buf);
	return len;
}

static const struct file_operations ipsec_dbg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = nsim_dbg_netdev_ops_read,
};

static int nsim_ipsec_find_empty_idx(struct nsim_ipsec *ipsec)
{
	u32 i;

	if (ipsec->count == NSIM_IPSEC_MAX_SA_COUNT)
		return -ENOSPC;

	/* search sa table */
	for (i = 0; i < NSIM_IPSEC_MAX_SA_COUNT; i++) {
		if (!ipsec->sa[i].used)
			return i;
	}

	return -ENOSPC;
}

static int nsim_ipsec_parse_proto_keys(struct xfrm_state *xs,
				       u32 *mykey, u32 *mysalt)
{
	const char aes_gcm_name[] = "rfc4106(gcm(aes))";
	struct net_device *dev = xs->xso.real_dev;
	unsigned char *key_data;
	char *alg_name = NULL;
	int key_len;

	if (!xs->aead) {
		netdev_err(dev, "Unsupported IPsec algorithm\n");
		return -EINVAL;
	}

	if (xs->aead->alg_icv_len != NSIM_IPSEC_AUTH_BITS) {
		netdev_err(dev, "IPsec offload requires %d bit authentication\n",
			   NSIM_IPSEC_AUTH_BITS);
		return -EINVAL;
	}

	key_data = &xs->aead->alg_key[0];
	key_len = xs->aead->alg_key_len;
	alg_name = xs->aead->alg_name;

	if (strcmp(alg_name, aes_gcm_name)) {
		netdev_err(dev, "Unsupported IPsec algorithm - please use %s\n",
			   aes_gcm_name);
		return -EINVAL;
	}

	/* 160 accounts for 16 byte key and 4 byte salt */
	if (key_len > NSIM_IPSEC_AUTH_BITS) {
		*mysalt = ((u32 *)key_data)[4];
	} else if (key_len == NSIM_IPSEC_AUTH_BITS) {
		*mysalt = 0;
	} else {
		netdev_err(dev, "IPsec hw offload only supports 128 bit keys with optional 32 bit salt\n");
		return -EINVAL;
	}
	memcpy(mykey, key_data, 16);

	return 0;
}

static int nsim_ipsec_add_sa(struct xfrm_state *xs,
			     struct netlink_ext_ack *extack)
{
	struct nsim_ipsec *ipsec;
	struct net_device *dev;
	struct netdevsim *ns;
	struct nsim_sa sa;
	u16 sa_idx;
	int ret;

	dev = xs->xso.real_dev;
	ns = netdev_priv(dev);
	ipsec = &ns->ipsec;

	if (xs->id.proto != IPPROTO_ESP && xs->id.proto != IPPROTO_AH) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported protocol for ipsec offload");
		return -EINVAL;
	}

	if (xs->calg) {
		NL_SET_ERR_MSG_MOD(extack, "Compression offload not supported");
		return -EINVAL;
	}

	if (xs->xso.type != XFRM_DEV_OFFLOAD_CRYPTO) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported ipsec offload type");
		return -EINVAL;
	}

	/* find the first unused index */
	ret = nsim_ipsec_find_empty_idx(ipsec);
	if (ret < 0) {
		NL_SET_ERR_MSG_MOD(extack, "No space for SA in Rx table!");
		return ret;
	}
	sa_idx = (u16)ret;

	memset(&sa, 0, sizeof(sa));
	sa.used = true;
	sa.xs = xs;

	if (sa.xs->id.proto & IPPROTO_ESP)
		sa.crypt = xs->ealg || xs->aead;

	/* get the key and salt */
	ret = nsim_ipsec_parse_proto_keys(xs, sa.key, &sa.salt);
	if (ret) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to get key data for SA table");
		return ret;
	}

	if (xs->xso.dir == XFRM_DEV_OFFLOAD_IN)
		sa.rx = true;

	if (xs->props.family == AF_INET6)
		memcpy(sa.ipaddr, &xs->id.daddr.a6, 16);
	else
		memcpy(&sa.ipaddr[3], &xs->id.daddr.a4, 4);

	/* the preparations worked, so save the info */
	memcpy(&ipsec->sa[sa_idx], &sa, sizeof(sa));

	/* the XFRM stack doesn't like offload_handle == 0,
	 * so add a bitflag in case our array index is 0
	 */
	xs->xso.offload_handle = sa_idx | NSIM_IPSEC_VALID;
	ipsec->count++;

	return 0;
}

static void nsim_ipsec_del_sa(struct xfrm_state *xs)
{
	struct netdevsim *ns = netdev_priv(xs->xso.real_dev);
	struct nsim_ipsec *ipsec = &ns->ipsec;
	u16 sa_idx;

	sa_idx = xs->xso.offload_handle & ~NSIM_IPSEC_VALID;
	if (!ipsec->sa[sa_idx].used) {
		netdev_err(ns->netdev, "Invalid SA for delete sa_idx=%d\n",
			   sa_idx);
		return;
	}

	memset(&ipsec->sa[sa_idx], 0, sizeof(struct nsim_sa));
	ipsec->count--;
}

static const struct xfrmdev_ops nsim_xfrmdev_ops = {
	.xdo_dev_state_add	= nsim_ipsec_add_sa,
	.xdo_dev_state_delete	= nsim_ipsec_del_sa,
};

bool nsim_ipsec_tx(struct netdevsim *ns, struct sk_buff *skb)
{
	struct sec_path *sp = skb_sec_path(skb);
	struct nsim_ipsec *ipsec = &ns->ipsec;
	struct xfrm_state *xs;
	struct nsim_sa *tsa;
	u32 sa_idx;

	/* do we even need to check this packet? */
	if (!sp)
		return true;

	if (unlikely(!sp->len)) {
		netdev_err(ns->netdev, "no xfrm state len = %d\n",
			   sp->len);
		return false;
	}

	xs = xfrm_input_state(skb);
	if (unlikely(!xs)) {
		netdev_err(ns->netdev, "no xfrm_input_state() xs = %p\n", xs);
		return false;
	}

	sa_idx = xs->xso.offload_handle & ~NSIM_IPSEC_VALID;
	if (unlikely(sa_idx >= NSIM_IPSEC_MAX_SA_COUNT)) {
		netdev_err(ns->netdev, "bad sa_idx=%d max=%d\n",
			   sa_idx, NSIM_IPSEC_MAX_SA_COUNT);
		return false;
	}

	tsa = &ipsec->sa[sa_idx];
	if (unlikely(!tsa->used)) {
		netdev_err(ns->netdev, "unused sa_idx=%d\n", sa_idx);
		return false;
	}

	if (xs->id.proto != IPPROTO_ESP && xs->id.proto != IPPROTO_AH) {
		netdev_err(ns->netdev, "unexpected proto=%d\n", xs->id.proto);
		return false;
	}

	ipsec->tx++;

	return true;
}

void nsim_ipsec_init(struct netdevsim *ns)
{
	ns->netdev->xfrmdev_ops = &nsim_xfrmdev_ops;

#define NSIM_ESP_FEATURES	(NETIF_F_HW_ESP | \
				 NETIF_F_HW_ESP_TX_CSUM | \
				 NETIF_F_GSO_ESP)

	ns->netdev->features |= NSIM_ESP_FEATURES;
	ns->netdev->hw_enc_features |= NSIM_ESP_FEATURES;

	ns->ipsec.pfile = debugfs_create_file("ipsec", 0400,
					      ns->nsim_dev_port->ddir, ns,
					      &ipsec_dbg_fops);
}

void nsim_ipsec_teardown(struct netdevsim *ns)
{
	struct nsim_ipsec *ipsec = &ns->ipsec;

	if (ipsec->count)
		netdev_err(ns->netdev, "tearing down IPsec offload with %d SAs left\n",
			   ipsec->count);
	debugfs_remove_recursive(ipsec->pfile);
}
