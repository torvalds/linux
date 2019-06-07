// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/bpf.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>

#include "../ccm.h"
#include "../nfp_app.h"
#include "../nfp_net.h"
#include "fw.h"
#include "main.h"

static struct sk_buff *
nfp_bpf_cmsg_alloc(struct nfp_app_bpf *bpf, unsigned int size)
{
	struct sk_buff *skb;

	skb = nfp_app_ctrl_msg_alloc(bpf->app, size, GFP_KERNEL);
	skb_put(skb, size);

	return skb;
}

static unsigned int
nfp_bpf_cmsg_map_req_size(struct nfp_app_bpf *bpf, unsigned int n)
{
	unsigned int size;

	size = sizeof(struct cmsg_req_map_op);
	size += (bpf->cmsg_key_sz + bpf->cmsg_val_sz) * n;

	return size;
}

static struct sk_buff *
nfp_bpf_cmsg_map_req_alloc(struct nfp_app_bpf *bpf, unsigned int n)
{
	return nfp_bpf_cmsg_alloc(bpf, nfp_bpf_cmsg_map_req_size(bpf, n));
}

static unsigned int
nfp_bpf_cmsg_map_reply_size(struct nfp_app_bpf *bpf, unsigned int n)
{
	unsigned int size;

	size = sizeof(struct cmsg_reply_map_op);
	size += (bpf->cmsg_key_sz + bpf->cmsg_val_sz) * n;

	return size;
}

static int
nfp_bpf_ctrl_rc_to_errno(struct nfp_app_bpf *bpf,
			 struct cmsg_reply_map_simple *reply)
{
	static const int res_table[] = {
		[CMSG_RC_SUCCESS]	= 0,
		[CMSG_RC_ERR_MAP_FD]	= -EBADFD,
		[CMSG_RC_ERR_MAP_NOENT]	= -ENOENT,
		[CMSG_RC_ERR_MAP_ERR]	= -EINVAL,
		[CMSG_RC_ERR_MAP_PARSE]	= -EIO,
		[CMSG_RC_ERR_MAP_EXIST]	= -EEXIST,
		[CMSG_RC_ERR_MAP_NOMEM]	= -ENOMEM,
		[CMSG_RC_ERR_MAP_E2BIG]	= -E2BIG,
	};
	u32 rc;

	rc = be32_to_cpu(reply->rc);
	if (rc >= ARRAY_SIZE(res_table)) {
		cmsg_warn(bpf, "FW responded with invalid status: %u\n", rc);
		return -EIO;
	}

	return res_table[rc];
}

long long int
nfp_bpf_ctrl_alloc_map(struct nfp_app_bpf *bpf, struct bpf_map *map)
{
	struct cmsg_reply_map_alloc_tbl *reply;
	struct cmsg_req_map_alloc_tbl *req;
	struct sk_buff *skb;
	u32 tid;
	int err;

	skb = nfp_bpf_cmsg_alloc(bpf, sizeof(*req));
	if (!skb)
		return -ENOMEM;

	req = (void *)skb->data;
	req->key_size = cpu_to_be32(map->key_size);
	req->value_size = cpu_to_be32(map->value_size);
	req->max_entries = cpu_to_be32(map->max_entries);
	req->map_type = cpu_to_be32(map->map_type);
	req->map_flags = 0;

	skb = nfp_ccm_communicate(&bpf->ccm, skb, NFP_CCM_TYPE_BPF_MAP_ALLOC,
				  sizeof(*reply));
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	reply = (void *)skb->data;
	err = nfp_bpf_ctrl_rc_to_errno(bpf, &reply->reply_hdr);
	if (err)
		goto err_free;

	tid = be32_to_cpu(reply->tid);
	dev_consume_skb_any(skb);

	return tid;
err_free:
	dev_kfree_skb_any(skb);
	return err;
}

void nfp_bpf_ctrl_free_map(struct nfp_app_bpf *bpf, struct nfp_bpf_map *nfp_map)
{
	struct cmsg_reply_map_free_tbl *reply;
	struct cmsg_req_map_free_tbl *req;
	struct sk_buff *skb;
	int err;

	skb = nfp_bpf_cmsg_alloc(bpf, sizeof(*req));
	if (!skb) {
		cmsg_warn(bpf, "leaking map - failed to allocate msg\n");
		return;
	}

	req = (void *)skb->data;
	req->tid = cpu_to_be32(nfp_map->tid);

	skb = nfp_ccm_communicate(&bpf->ccm, skb, NFP_CCM_TYPE_BPF_MAP_FREE,
				  sizeof(*reply));
	if (IS_ERR(skb)) {
		cmsg_warn(bpf, "leaking map - I/O error\n");
		return;
	}

	reply = (void *)skb->data;
	err = nfp_bpf_ctrl_rc_to_errno(bpf, &reply->reply_hdr);
	if (err)
		cmsg_warn(bpf, "leaking map - FW responded with: %d\n", err);

	dev_consume_skb_any(skb);
}

static void *
nfp_bpf_ctrl_req_key(struct nfp_app_bpf *bpf, struct cmsg_req_map_op *req,
		     unsigned int n)
{
	return &req->data[bpf->cmsg_key_sz * n + bpf->cmsg_val_sz * n];
}

static void *
nfp_bpf_ctrl_req_val(struct nfp_app_bpf *bpf, struct cmsg_req_map_op *req,
		     unsigned int n)
{
	return &req->data[bpf->cmsg_key_sz * (n + 1) + bpf->cmsg_val_sz * n];
}

static void *
nfp_bpf_ctrl_reply_key(struct nfp_app_bpf *bpf, struct cmsg_reply_map_op *reply,
		       unsigned int n)
{
	return &reply->data[bpf->cmsg_key_sz * n + bpf->cmsg_val_sz * n];
}

static void *
nfp_bpf_ctrl_reply_val(struct nfp_app_bpf *bpf, struct cmsg_reply_map_op *reply,
		       unsigned int n)
{
	return &reply->data[bpf->cmsg_key_sz * (n + 1) + bpf->cmsg_val_sz * n];
}

static int
nfp_bpf_ctrl_entry_op(struct bpf_offloaded_map *offmap, enum nfp_ccm_type op,
		      u8 *key, u8 *value, u64 flags, u8 *out_key, u8 *out_value)
{
	struct nfp_bpf_map *nfp_map = offmap->dev_priv;
	struct nfp_app_bpf *bpf = nfp_map->bpf;
	struct bpf_map *map = &offmap->map;
	struct cmsg_reply_map_op *reply;
	struct cmsg_req_map_op *req;
	struct sk_buff *skb;
	int err;

	/* FW messages have no space for more than 32 bits of flags */
	if (flags >> 32)
		return -EOPNOTSUPP;

	skb = nfp_bpf_cmsg_map_req_alloc(bpf, 1);
	if (!skb)
		return -ENOMEM;

	req = (void *)skb->data;
	req->tid = cpu_to_be32(nfp_map->tid);
	req->count = cpu_to_be32(1);
	req->flags = cpu_to_be32(flags);

	/* Copy inputs */
	if (key)
		memcpy(nfp_bpf_ctrl_req_key(bpf, req, 0), key, map->key_size);
	if (value)
		memcpy(nfp_bpf_ctrl_req_val(bpf, req, 0), value,
		       map->value_size);

	skb = nfp_ccm_communicate(&bpf->ccm, skb, op,
				  nfp_bpf_cmsg_map_reply_size(bpf, 1));
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	reply = (void *)skb->data;
	err = nfp_bpf_ctrl_rc_to_errno(bpf, &reply->reply_hdr);
	if (err)
		goto err_free;

	/* Copy outputs */
	if (out_key)
		memcpy(out_key, nfp_bpf_ctrl_reply_key(bpf, reply, 0),
		       map->key_size);
	if (out_value)
		memcpy(out_value, nfp_bpf_ctrl_reply_val(bpf, reply, 0),
		       map->value_size);

	dev_consume_skb_any(skb);

	return 0;
err_free:
	dev_kfree_skb_any(skb);
	return err;
}

int nfp_bpf_ctrl_update_entry(struct bpf_offloaded_map *offmap,
			      void *key, void *value, u64 flags)
{
	return nfp_bpf_ctrl_entry_op(offmap, NFP_CCM_TYPE_BPF_MAP_UPDATE,
				     key, value, flags, NULL, NULL);
}

int nfp_bpf_ctrl_del_entry(struct bpf_offloaded_map *offmap, void *key)
{
	return nfp_bpf_ctrl_entry_op(offmap, NFP_CCM_TYPE_BPF_MAP_DELETE,
				     key, NULL, 0, NULL, NULL);
}

int nfp_bpf_ctrl_lookup_entry(struct bpf_offloaded_map *offmap,
			      void *key, void *value)
{
	return nfp_bpf_ctrl_entry_op(offmap, NFP_CCM_TYPE_BPF_MAP_LOOKUP,
				     key, NULL, 0, NULL, value);
}

int nfp_bpf_ctrl_getfirst_entry(struct bpf_offloaded_map *offmap,
				void *next_key)
{
	return nfp_bpf_ctrl_entry_op(offmap, NFP_CCM_TYPE_BPF_MAP_GETFIRST,
				     NULL, NULL, 0, next_key, NULL);
}

int nfp_bpf_ctrl_getnext_entry(struct bpf_offloaded_map *offmap,
			       void *key, void *next_key)
{
	return nfp_bpf_ctrl_entry_op(offmap, NFP_CCM_TYPE_BPF_MAP_GETNEXT,
				     key, NULL, 0, next_key, NULL);
}

unsigned int nfp_bpf_ctrl_cmsg_mtu(struct nfp_app_bpf *bpf)
{
	return max3((unsigned int)NFP_NET_DEFAULT_MTU,
		    nfp_bpf_cmsg_map_req_size(bpf, 1),
		    nfp_bpf_cmsg_map_reply_size(bpf, 1));
}

void nfp_bpf_ctrl_msg_rx(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_app_bpf *bpf = app->priv;

	if (unlikely(skb->len < sizeof(struct cmsg_reply_map_simple))) {
		cmsg_warn(bpf, "cmsg drop - too short %d!\n", skb->len);
		dev_kfree_skb_any(skb);
		return;
	}

	if (nfp_ccm_get_type(skb) == NFP_CCM_TYPE_BPF_BPF_EVENT) {
		if (!nfp_bpf_event_output(bpf, skb->data, skb->len))
			dev_consume_skb_any(skb);
		else
			dev_kfree_skb_any(skb);
	}

	nfp_ccm_rx(&bpf->ccm, skb);
}

void
nfp_bpf_ctrl_msg_rx_raw(struct nfp_app *app, const void *data, unsigned int len)
{
	const struct nfp_ccm_hdr *hdr = data;
	struct nfp_app_bpf *bpf = app->priv;

	if (unlikely(len < sizeof(struct cmsg_reply_map_simple))) {
		cmsg_warn(bpf, "cmsg drop - too short %d!\n", len);
		return;
	}

	if (hdr->type == NFP_CCM_TYPE_BPF_BPF_EVENT)
		nfp_bpf_event_output(bpf, data, len);
	else
		cmsg_warn(bpf, "cmsg drop - msg type %d with raw buffer!\n",
			  hdr->type);
}
