/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

#ifndef __NFP_FLOWER_H__
#define __NFP_FLOWER_H__ 1

#include <linux/types.h>

struct tc_to_netdev;
struct net_device;
struct nfp_app;

struct nfp_fl_key_ls {
	u32 key_layer_two;
	u8 key_layer;
	int key_size;
};

struct nfp_fl_rule_metadata {
	u8 key_len;
	u8 mask_len;
	u8 act_len;
	u8 flags;
	__be32 host_ctx_id;
	__be64 host_cookie __packed;
	__be64 flow_version __packed;
	__be32 shortcut;
};

struct nfp_fl_payload {
	struct nfp_fl_rule_metadata meta;
	char *unmasked_data;
	char *mask_data;
	char *action_data;
};

int nfp_flower_setup_tc(struct nfp_app *app, struct net_device *netdev,
			u32 handle, __be16 proto, struct tc_to_netdev *tc);
int nfp_flower_compile_flow_match(struct tc_cls_flower_offload *flow,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow);
int nfp_flower_compile_action(struct tc_cls_flower_offload *flow,
			      struct net_device *netdev,
			      struct nfp_fl_payload *nfp_flow);

#endif
