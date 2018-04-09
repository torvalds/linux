/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
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

#include "uverbs.h"
#include <rdma/uverbs_std_types.h>

static int uverbs_free_flow_action(struct ib_uobject *uobject,
				   enum rdma_remove_reason why)
{
	struct ib_flow_action *action = uobject->object;

	if (why == RDMA_REMOVE_DESTROY &&
	    atomic_read(&action->usecnt))
		return -EBUSY;

	return action->device->destroy_flow_action(action);
}

static u64 esp_flags_uverbs_to_verbs(struct uverbs_attr_bundle *attrs,
				     u32 flags, bool is_modify)
{
	u64 verbs_flags = flags;

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_FLOW_ACTION_ESP_ESN))
		verbs_flags |= IB_FLOW_ACTION_ESP_FLAGS_ESN_TRIGGERED;

	if (is_modify && uverbs_attr_is_valid(attrs,
					      UVERBS_ATTR_FLOW_ACTION_ESP_ATTRS))
		verbs_flags |= IB_FLOW_ACTION_ESP_FLAGS_MOD_ESP_ATTRS;

	return verbs_flags;
};

static int validate_flow_action_esp_keymat_aes_gcm(struct ib_flow_action_attrs_esp_keymats *keymat)
{
	struct ib_uverbs_flow_action_esp_keymat_aes_gcm *aes_gcm =
		&keymat->keymat.aes_gcm;

	if (aes_gcm->iv_algo > IB_UVERBS_FLOW_ACTION_IV_ALGO_SEQ)
		return -EOPNOTSUPP;

	if (aes_gcm->key_len != 32 &&
	    aes_gcm->key_len != 24 &&
	    aes_gcm->key_len != 16)
		return -EINVAL;

	if (aes_gcm->icv_len != 16 &&
	    aes_gcm->icv_len != 8 &&
	    aes_gcm->icv_len != 12)
		return -EINVAL;

	return 0;
}

static int (* const flow_action_esp_keymat_validate[])(struct ib_flow_action_attrs_esp_keymats *keymat) = {
	[IB_UVERBS_FLOW_ACTION_ESP_KEYMAT_AES_GCM] = validate_flow_action_esp_keymat_aes_gcm,
};

static int flow_action_esp_replay_none(struct ib_flow_action_attrs_esp_replays *replay,
				       bool is_modify)
{
	/* This is used in order to modify an esp flow action with an enabled
	 * replay protection to a disabled one. This is only supported via
	 * modify, as in create verb we can simply drop the REPLAY attribute and
	 * achieve the same thing.
	 */
	return is_modify ? 0 : -EINVAL;
}

static int flow_action_esp_replay_def_ok(struct ib_flow_action_attrs_esp_replays *replay,
					 bool is_modify)
{
	/* Some replay protections could always be enabled without validating
	 * anything.
	 */
	return 0;
}

static int (* const flow_action_esp_replay_validate[])(struct ib_flow_action_attrs_esp_replays *replay,
						       bool is_modify) = {
	[IB_UVERBS_FLOW_ACTION_ESP_REPLAY_NONE] = flow_action_esp_replay_none,
	[IB_UVERBS_FLOW_ACTION_ESP_REPLAY_BMP] = flow_action_esp_replay_def_ok,
};

static int parse_esp_ip(enum ib_flow_spec_type proto,
			const void __user *val_ptr,
			size_t len, union ib_flow_spec *out)
{
	int ret;
	const struct ib_uverbs_flow_ipv4_filter ipv4 = {
		.src_ip = cpu_to_be32(0xffffffffUL),
		.dst_ip = cpu_to_be32(0xffffffffUL),
		.proto = 0xff,
		.tos = 0xff,
		.ttl = 0xff,
		.flags = 0xff,
	};
	const struct ib_uverbs_flow_ipv6_filter ipv6 = {
		.src_ip = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		.dst_ip = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		.flow_label = cpu_to_be32(0xffffffffUL),
		.next_hdr = 0xff,
		.traffic_class = 0xff,
		.hop_limit = 0xff,
	};
	union {
		struct ib_uverbs_flow_ipv4_filter ipv4;
		struct ib_uverbs_flow_ipv6_filter ipv6;
	} user_val = {};
	const void *user_pmask;
	size_t val_len;

	/* If the flow IPv4/IPv6 flow specifications are extended, the mask
	 * should be changed as well.
	 */
	BUILD_BUG_ON(offsetof(struct ib_uverbs_flow_ipv4_filter, flags) +
		     sizeof(ipv4.flags) != sizeof(ipv4));
	BUILD_BUG_ON(offsetof(struct ib_uverbs_flow_ipv6_filter, reserved) +
		     sizeof(ipv6.reserved) != sizeof(ipv6));

	switch (proto) {
	case IB_FLOW_SPEC_IPV4:
		if (len > sizeof(user_val.ipv4) &&
		    !ib_is_buffer_cleared(val_ptr + sizeof(user_val.ipv4),
					  len - sizeof(user_val.ipv4)))
			return -EOPNOTSUPP;

		val_len = min_t(size_t, len, sizeof(user_val.ipv4));
		ret = copy_from_user(&user_val.ipv4, val_ptr,
				     val_len);
		if (ret)
			return -EFAULT;

		user_pmask = &ipv4;
		break;
	case IB_FLOW_SPEC_IPV6:
		if (len > sizeof(user_val.ipv6) &&
		    !ib_is_buffer_cleared(val_ptr + sizeof(user_val.ipv6),
					  len - sizeof(user_val.ipv6)))
			return -EOPNOTSUPP;

		val_len = min_t(size_t, len, sizeof(user_val.ipv6));
		ret = copy_from_user(&user_val.ipv6, val_ptr,
				     val_len);
		if (ret)
			return -EFAULT;

		user_pmask = &ipv6;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ib_uverbs_kern_spec_to_ib_spec_filter(proto, user_pmask,
						     &user_val,
						     val_len, out);
}

static int flow_action_esp_get_encap(struct ib_flow_spec_list *out,
				     struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_flow_action_esp_encap uverbs_encap;
	int ret;

	ret = uverbs_copy_from(&uverbs_encap, attrs,
			       UVERBS_ATTR_FLOW_ACTION_ESP_ENCAP);
	if (ret)
		return ret;

	/* We currently support only one encap */
	if (uverbs_encap.next_ptr)
		return -EOPNOTSUPP;

	if (uverbs_encap.type != IB_FLOW_SPEC_IPV4 &&
	    uverbs_encap.type != IB_FLOW_SPEC_IPV6)
		return -EOPNOTSUPP;

	return parse_esp_ip(uverbs_encap.type,
			    u64_to_user_ptr(uverbs_encap.val_ptr),
			    uverbs_encap.len,
			    &out->spec);
}

struct ib_flow_action_esp_attr {
	struct	ib_flow_action_attrs_esp		hdr;
	struct	ib_flow_action_attrs_esp_keymats	keymat;
	struct	ib_flow_action_attrs_esp_replays	replay;
	/* We currently support only one spec */
	struct	ib_flow_spec_list			encap;
};

#define ESP_LAST_SUPPORTED_FLAG		IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW
static int parse_flow_action_esp(struct ib_device *ib_dev,
				 struct ib_uverbs_file *file,
				 struct uverbs_attr_bundle *attrs,
				 struct ib_flow_action_esp_attr *esp_attr,
				 bool is_modify)
{
	struct ib_uverbs_flow_action_esp uverbs_esp = {};
	int ret;

	/* Optional param, if it doesn't exist, we get -ENOENT and skip it */
	ret = uverbs_copy_from(&esp_attr->hdr.esn, attrs,
			       UVERBS_ATTR_FLOW_ACTION_ESP_ESN);
	if (IS_UVERBS_COPY_ERR(ret))
		return ret;

	/* This can be called from FLOW_ACTION_ESP_MODIFY where
	 * UVERBS_ATTR_FLOW_ACTION_ESP_ATTRS is optional
	 */
	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_FLOW_ACTION_ESP_ATTRS)) {
		ret = uverbs_copy_from_or_zero(&uverbs_esp, attrs,
					       UVERBS_ATTR_FLOW_ACTION_ESP_ATTRS);
		if (ret)
			return ret;

		if (uverbs_esp.flags & ~((ESP_LAST_SUPPORTED_FLAG << 1) - 1))
			return -EOPNOTSUPP;

		esp_attr->hdr.spi = uverbs_esp.spi;
		esp_attr->hdr.seq = uverbs_esp.seq;
		esp_attr->hdr.tfc_pad = uverbs_esp.tfc_pad;
		esp_attr->hdr.hard_limit_pkts = uverbs_esp.hard_limit_pkts;
	}
	esp_attr->hdr.flags = esp_flags_uverbs_to_verbs(attrs, uverbs_esp.flags,
							is_modify);

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_FLOW_ACTION_ESP_KEYMAT)) {
		esp_attr->keymat.protocol =
			uverbs_attr_get_enum_id(attrs,
						UVERBS_ATTR_FLOW_ACTION_ESP_KEYMAT);
		ret = uverbs_copy_from_or_zero(&esp_attr->keymat.keymat,
					       attrs,
					       UVERBS_ATTR_FLOW_ACTION_ESP_KEYMAT);
		if (ret)
			return ret;

		ret = flow_action_esp_keymat_validate[esp_attr->keymat.protocol](&esp_attr->keymat);
		if (ret)
			return ret;

		esp_attr->hdr.keymat = &esp_attr->keymat;
	}

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_FLOW_ACTION_ESP_REPLAY)) {
		esp_attr->replay.protocol =
			uverbs_attr_get_enum_id(attrs,
						UVERBS_ATTR_FLOW_ACTION_ESP_REPLAY);

		ret = uverbs_copy_from_or_zero(&esp_attr->replay.replay,
					       attrs,
					       UVERBS_ATTR_FLOW_ACTION_ESP_REPLAY);
		if (ret)
			return ret;

		ret = flow_action_esp_replay_validate[esp_attr->replay.protocol](&esp_attr->replay,
										 is_modify);
		if (ret)
			return ret;

		esp_attr->hdr.replay = &esp_attr->replay;
	}

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_FLOW_ACTION_ESP_ENCAP)) {
		ret = flow_action_esp_get_encap(&esp_attr->encap, attrs);
		if (ret)
			return ret;

		esp_attr->hdr.encap = &esp_attr->encap;
	}

	return 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_FLOW_ACTION_ESP_CREATE)(struct ib_device *ib_dev,
								struct ib_uverbs_file *file,
								struct uverbs_attr_bundle *attrs)
{
	int				  ret;
	struct ib_uobject		  *uobj;
	struct ib_flow_action		  *action;
	struct ib_flow_action_esp_attr	  esp_attr = {};

	if (!ib_dev->create_flow_action_esp)
		return -EOPNOTSUPP;

	ret = parse_flow_action_esp(ib_dev, file, attrs, &esp_attr, false);
	if (ret)
		return ret;

	/* No need to check as this attribute is marked as MANDATORY */
	uobj = uverbs_attr_get(attrs, UVERBS_ATTR_FLOW_ACTION_ESP_HANDLE)->obj_attr.uobject;
	action = ib_dev->create_flow_action_esp(ib_dev, &esp_attr.hdr, attrs);
	if (IS_ERR(action))
		return PTR_ERR(action);

	atomic_set(&action->usecnt, 0);
	action->device = ib_dev;
	action->type = IB_FLOW_ACTION_ESP;
	action->uobject = uobj;
	uobj->object = action;

	return 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_FLOW_ACTION_ESP_MODIFY)(struct ib_device *ib_dev,
								struct ib_uverbs_file *file,
								struct uverbs_attr_bundle *attrs)
{
	int				  ret;
	struct ib_uobject		  *uobj;
	struct ib_flow_action		  *action;
	struct ib_flow_action_esp_attr	  esp_attr = {};

	if (!ib_dev->modify_flow_action_esp)
		return -EOPNOTSUPP;

	ret = parse_flow_action_esp(ib_dev, file, attrs, &esp_attr, true);
	if (ret)
		return ret;

	uobj = uverbs_attr_get(attrs, UVERBS_ATTR_FLOW_ACTION_ESP_HANDLE)->obj_attr.uobject;
	action = uobj->object;

	if (action->type != IB_FLOW_ACTION_ESP)
		return -EINVAL;

	return ib_dev->modify_flow_action_esp(action,
					      &esp_attr.hdr,
					      attrs);
}

static const struct uverbs_attr_spec uverbs_flow_action_esp_keymat[] = {
	[IB_UVERBS_FLOW_ACTION_ESP_KEYMAT_AES_GCM] = {
		{ .ptr = {
			.type = UVERBS_ATTR_TYPE_PTR_IN,
			UVERBS_ATTR_TYPE(struct ib_uverbs_flow_action_esp_keymat_aes_gcm),
			.flags = UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO,
		} },
	},
};

static const struct uverbs_attr_spec uverbs_flow_action_esp_replay[] = {
	[IB_UVERBS_FLOW_ACTION_ESP_REPLAY_NONE] = {
		{ .ptr = {
			.type = UVERBS_ATTR_TYPE_PTR_IN,
			/* No need to specify any data */
			.len = 0,
		} }
	},
	[IB_UVERBS_FLOW_ACTION_ESP_REPLAY_BMP] = {
		{ .ptr = {
			.type = UVERBS_ATTR_TYPE_PTR_IN,
			UVERBS_ATTR_STRUCT(struct ib_uverbs_flow_action_esp_replay_bmp, size),
			.flags = UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO,
		} }
	},
};

static DECLARE_UVERBS_NAMED_METHOD(UVERBS_METHOD_FLOW_ACTION_ESP_CREATE,
	&UVERBS_ATTR_IDR(UVERBS_ATTR_FLOW_ACTION_ESP_HANDLE, UVERBS_OBJECT_FLOW_ACTION,
			 UVERBS_ACCESS_NEW,
			 UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY)),
	&UVERBS_ATTR_PTR_IN(UVERBS_ATTR_FLOW_ACTION_ESP_ATTRS,
			    UVERBS_ATTR_STRUCT(struct ib_uverbs_flow_action_esp, hard_limit_pkts),
			    UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY |
				     UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO)),
	&UVERBS_ATTR_PTR_IN(UVERBS_ATTR_FLOW_ACTION_ESP_ESN, UVERBS_ATTR_TYPE(__u32)),
	&UVERBS_ATTR_ENUM_IN(UVERBS_ATTR_FLOW_ACTION_ESP_KEYMAT,
			     uverbs_flow_action_esp_keymat,
			     UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY)),
	&UVERBS_ATTR_ENUM_IN(UVERBS_ATTR_FLOW_ACTION_ESP_REPLAY,
			     uverbs_flow_action_esp_replay),
	&UVERBS_ATTR_PTR_IN(UVERBS_ATTR_FLOW_ACTION_ESP_ENCAP,
			    UVERBS_ATTR_STRUCT(struct ib_uverbs_flow_action_esp_encap, type)));

static DECLARE_UVERBS_NAMED_METHOD(UVERBS_METHOD_FLOW_ACTION_ESP_MODIFY,
	&UVERBS_ATTR_IDR(UVERBS_ATTR_FLOW_ACTION_ESP_HANDLE, UVERBS_OBJECT_FLOW_ACTION,
			 UVERBS_ACCESS_WRITE,
			 UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY)),
	&UVERBS_ATTR_PTR_IN(UVERBS_ATTR_FLOW_ACTION_ESP_ATTRS,
			    UVERBS_ATTR_STRUCT(struct ib_uverbs_flow_action_esp, hard_limit_pkts),
			    UA_FLAGS(UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO)),
	&UVERBS_ATTR_PTR_IN(UVERBS_ATTR_FLOW_ACTION_ESP_ESN, UVERBS_ATTR_TYPE(__u32)),
	&UVERBS_ATTR_ENUM_IN(UVERBS_ATTR_FLOW_ACTION_ESP_KEYMAT,
			     uverbs_flow_action_esp_keymat),
	&UVERBS_ATTR_ENUM_IN(UVERBS_ATTR_FLOW_ACTION_ESP_REPLAY,
			     uverbs_flow_action_esp_replay),
	&UVERBS_ATTR_PTR_IN(UVERBS_ATTR_FLOW_ACTION_ESP_ENCAP,
			    UVERBS_ATTR_STRUCT(struct ib_uverbs_flow_action_esp_encap, type)));

static DECLARE_UVERBS_NAMED_METHOD_WITH_HANDLER(UVERBS_METHOD_FLOW_ACTION_DESTROY,
	uverbs_destroy_def_handler,
	&UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_FLOW_ACTION_HANDLE,
			 UVERBS_OBJECT_FLOW_ACTION,
			 UVERBS_ACCESS_DESTROY,
			 UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY)));

DECLARE_UVERBS_NAMED_OBJECT(UVERBS_OBJECT_FLOW_ACTION,
			    &UVERBS_TYPE_ALLOC_IDR(0, uverbs_free_flow_action),
			    &UVERBS_METHOD(UVERBS_METHOD_FLOW_ACTION_ESP_CREATE),
			    &UVERBS_METHOD(UVERBS_METHOD_FLOW_ACTION_DESTROY),
			    &UVERBS_METHOD(UVERBS_METHOD_FLOW_ACTION_ESP_MODIFY));

