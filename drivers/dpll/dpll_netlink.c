// SPDX-License-Identifier: GPL-2.0
/*
 * Generic netlink for DPLL management framework
 *
 *  Copyright (c) 2023 Meta Platforms, Inc. and affiliates
 *  Copyright (c) 2023 Intel and affiliates
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <net/genetlink.h>
#include "dpll_core.h"
#include "dpll_netlink.h"
#include "dpll_nl.h"
#include <uapi/linux/dpll.h>

#define ASSERT_NOT_NULL(ptr)	(WARN_ON(!ptr))

#define xa_for_each_marked_start(xa, index, entry, filter, start) \
	for (index = start, entry = xa_find(xa, &index, ULONG_MAX, filter); \
	     entry; entry = xa_find_after(xa, &index, ULONG_MAX, filter))

struct dpll_dump_ctx {
	unsigned long idx;
};

static struct dpll_dump_ctx *dpll_dump_context(struct netlink_callback *cb)
{
	return (struct dpll_dump_ctx *)cb->ctx;
}

static int
dpll_msg_add_dev_handle(struct sk_buff *msg, struct dpll_device *dpll)
{
	if (nla_put_u32(msg, DPLL_A_ID, dpll->id))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_dev_parent_handle(struct sk_buff *msg, u32 id)
{
	if (nla_put_u32(msg, DPLL_A_PIN_PARENT_ID, id))
		return -EMSGSIZE;

	return 0;
}

/**
 * dpll_msg_pin_handle_size - get size of pin handle attribute for given pin
 * @pin: pin pointer
 *
 * Return: byte size of pin handle attribute for given pin.
 */
size_t dpll_msg_pin_handle_size(struct dpll_pin *pin)
{
	return pin ? nla_total_size(4) : 0; /* DPLL_A_PIN_ID */
}
EXPORT_SYMBOL_GPL(dpll_msg_pin_handle_size);

/**
 * dpll_msg_add_pin_handle - attach pin handle attribute to a given message
 * @msg: pointer to sk_buff message to attach a pin handle
 * @pin: pin pointer
 *
 * Return:
 * * 0 - success
 * * -EMSGSIZE - no space in message to attach pin handle
 */
int dpll_msg_add_pin_handle(struct sk_buff *msg, struct dpll_pin *pin)
{
	if (!pin)
		return 0;
	if (nla_put_u32(msg, DPLL_A_PIN_ID, pin->id))
		return -EMSGSIZE;
	return 0;
}
EXPORT_SYMBOL_GPL(dpll_msg_add_pin_handle);

static int
dpll_msg_add_mode(struct sk_buff *msg, struct dpll_device *dpll,
		  struct netlink_ext_ack *extack)
{
	const struct dpll_device_ops *ops = dpll_device_ops(dpll);
	enum dpll_mode mode;
	int ret;

	ret = ops->mode_get(dpll, dpll_priv(dpll), &mode, extack);
	if (ret)
		return ret;
	if (nla_put_u32(msg, DPLL_A_MODE, mode))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_mode_supported(struct sk_buff *msg, struct dpll_device *dpll,
			    struct netlink_ext_ack *extack)
{
	const struct dpll_device_ops *ops = dpll_device_ops(dpll);
	enum dpll_mode mode;
	int ret;

	/* No mode change is supported now, so the only supported mode is the
	 * one obtained by mode_get().
	 */

	ret = ops->mode_get(dpll, dpll_priv(dpll), &mode, extack);
	if (ret)
		return ret;
	if (nla_put_u32(msg, DPLL_A_MODE_SUPPORTED, mode))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_lock_status(struct sk_buff *msg, struct dpll_device *dpll,
			 struct netlink_ext_ack *extack)
{
	const struct dpll_device_ops *ops = dpll_device_ops(dpll);
	enum dpll_lock_status status;
	int ret;

	ret = ops->lock_status_get(dpll, dpll_priv(dpll), &status, extack);
	if (ret)
		return ret;
	if (nla_put_u32(msg, DPLL_A_LOCK_STATUS, status))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_temp(struct sk_buff *msg, struct dpll_device *dpll,
		  struct netlink_ext_ack *extack)
{
	const struct dpll_device_ops *ops = dpll_device_ops(dpll);
	s32 temp;
	int ret;

	if (!ops->temp_get)
		return 0;
	ret = ops->temp_get(dpll, dpll_priv(dpll), &temp, extack);
	if (ret)
		return ret;
	if (nla_put_s32(msg, DPLL_A_TEMP, temp))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_pin_prio(struct sk_buff *msg, struct dpll_pin *pin,
		      struct dpll_pin_ref *ref,
		      struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops = dpll_pin_ops(ref);
	struct dpll_device *dpll = ref->dpll;
	u32 prio;
	int ret;

	if (!ops->prio_get)
		return 0;
	ret = ops->prio_get(pin, dpll_pin_on_dpll_priv(dpll, pin), dpll,
			    dpll_priv(dpll), &prio, extack);
	if (ret)
		return ret;
	if (nla_put_u32(msg, DPLL_A_PIN_PRIO, prio))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_pin_on_dpll_state(struct sk_buff *msg, struct dpll_pin *pin,
			       struct dpll_pin_ref *ref,
			       struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops = dpll_pin_ops(ref);
	struct dpll_device *dpll = ref->dpll;
	enum dpll_pin_state state;
	int ret;

	if (!ops->state_on_dpll_get)
		return 0;
	ret = ops->state_on_dpll_get(pin, dpll_pin_on_dpll_priv(dpll, pin),
				     dpll, dpll_priv(dpll), &state, extack);
	if (ret)
		return ret;
	if (nla_put_u32(msg, DPLL_A_PIN_STATE, state))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_pin_direction(struct sk_buff *msg, struct dpll_pin *pin,
			   struct dpll_pin_ref *ref,
			   struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops = dpll_pin_ops(ref);
	struct dpll_device *dpll = ref->dpll;
	enum dpll_pin_direction direction;
	int ret;

	ret = ops->direction_get(pin, dpll_pin_on_dpll_priv(dpll, pin), dpll,
				 dpll_priv(dpll), &direction, extack);
	if (ret)
		return ret;
	if (nla_put_u32(msg, DPLL_A_PIN_DIRECTION, direction))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_pin_phase_adjust(struct sk_buff *msg, struct dpll_pin *pin,
			      struct dpll_pin_ref *ref,
			      struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops = dpll_pin_ops(ref);
	struct dpll_device *dpll = ref->dpll;
	s32 phase_adjust;
	int ret;

	if (!ops->phase_adjust_get)
		return 0;
	ret = ops->phase_adjust_get(pin, dpll_pin_on_dpll_priv(dpll, pin),
				    dpll, dpll_priv(dpll),
				    &phase_adjust, extack);
	if (ret)
		return ret;
	if (nla_put_s32(msg, DPLL_A_PIN_PHASE_ADJUST, phase_adjust))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_msg_add_phase_offset(struct sk_buff *msg, struct dpll_pin *pin,
			  struct dpll_pin_ref *ref,
			  struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops = dpll_pin_ops(ref);
	struct dpll_device *dpll = ref->dpll;
	s64 phase_offset;
	int ret;

	if (!ops->phase_offset_get)
		return 0;
	ret = ops->phase_offset_get(pin, dpll_pin_on_dpll_priv(dpll, pin),
				    dpll, dpll_priv(dpll), &phase_offset,
				    extack);
	if (ret)
		return ret;
	if (nla_put_64bit(msg, DPLL_A_PIN_PHASE_OFFSET, sizeof(phase_offset),
			  &phase_offset, DPLL_A_PIN_PAD))
		return -EMSGSIZE;

	return 0;
}

static int dpll_msg_add_ffo(struct sk_buff *msg, struct dpll_pin *pin,
			    struct dpll_pin_ref *ref,
			    struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops = dpll_pin_ops(ref);
	struct dpll_device *dpll = ref->dpll;
	s64 ffo;
	int ret;

	if (!ops->ffo_get)
		return 0;
	ret = ops->ffo_get(pin, dpll_pin_on_dpll_priv(dpll, pin),
			   dpll, dpll_priv(dpll), &ffo, extack);
	if (ret) {
		if (ret == -ENODATA)
			return 0;
		return ret;
	}
	return nla_put_sint(msg, DPLL_A_PIN_FRACTIONAL_FREQUENCY_OFFSET, ffo);
}

static int
dpll_msg_add_pin_freq(struct sk_buff *msg, struct dpll_pin *pin,
		      struct dpll_pin_ref *ref, struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops = dpll_pin_ops(ref);
	struct dpll_device *dpll = ref->dpll;
	struct nlattr *nest;
	int fs, ret;
	u64 freq;

	if (!ops->frequency_get)
		return 0;
	ret = ops->frequency_get(pin, dpll_pin_on_dpll_priv(dpll, pin), dpll,
				 dpll_priv(dpll), &freq, extack);
	if (ret)
		return ret;
	if (nla_put_64bit(msg, DPLL_A_PIN_FREQUENCY, sizeof(freq), &freq,
			  DPLL_A_PIN_PAD))
		return -EMSGSIZE;
	for (fs = 0; fs < pin->prop.freq_supported_num; fs++) {
		nest = nla_nest_start(msg, DPLL_A_PIN_FREQUENCY_SUPPORTED);
		if (!nest)
			return -EMSGSIZE;
		freq = pin->prop.freq_supported[fs].min;
		if (nla_put_64bit(msg, DPLL_A_PIN_FREQUENCY_MIN, sizeof(freq),
				  &freq, DPLL_A_PIN_PAD)) {
			nla_nest_cancel(msg, nest);
			return -EMSGSIZE;
		}
		freq = pin->prop.freq_supported[fs].max;
		if (nla_put_64bit(msg, DPLL_A_PIN_FREQUENCY_MAX, sizeof(freq),
				  &freq, DPLL_A_PIN_PAD)) {
			nla_nest_cancel(msg, nest);
			return -EMSGSIZE;
		}
		nla_nest_end(msg, nest);
	}

	return 0;
}

static bool dpll_pin_is_freq_supported(struct dpll_pin *pin, u32 freq)
{
	int fs;

	for (fs = 0; fs < pin->prop.freq_supported_num; fs++)
		if (freq >= pin->prop.freq_supported[fs].min &&
		    freq <= pin->prop.freq_supported[fs].max)
			return true;
	return false;
}

static int
dpll_msg_add_pin_parents(struct sk_buff *msg, struct dpll_pin *pin,
			 struct dpll_pin_ref *dpll_ref,
			 struct netlink_ext_ack *extack)
{
	enum dpll_pin_state state;
	struct dpll_pin_ref *ref;
	struct dpll_pin *ppin;
	struct nlattr *nest;
	unsigned long index;
	int ret;

	xa_for_each(&pin->parent_refs, index, ref) {
		const struct dpll_pin_ops *ops = dpll_pin_ops(ref);
		void *parent_priv;

		ppin = ref->pin;
		parent_priv = dpll_pin_on_dpll_priv(dpll_ref->dpll, ppin);
		ret = ops->state_on_pin_get(pin,
					    dpll_pin_on_pin_priv(ppin, pin),
					    ppin, parent_priv, &state, extack);
		if (ret)
			return ret;
		nest = nla_nest_start(msg, DPLL_A_PIN_PARENT_PIN);
		if (!nest)
			return -EMSGSIZE;
		ret = dpll_msg_add_dev_parent_handle(msg, ppin->id);
		if (ret)
			goto nest_cancel;
		if (nla_put_u32(msg, DPLL_A_PIN_STATE, state)) {
			ret = -EMSGSIZE;
			goto nest_cancel;
		}
		nla_nest_end(msg, nest);
	}

	return 0;

nest_cancel:
	nla_nest_cancel(msg, nest);
	return ret;
}

static int
dpll_msg_add_pin_dplls(struct sk_buff *msg, struct dpll_pin *pin,
		       struct netlink_ext_ack *extack)
{
	struct dpll_pin_ref *ref;
	struct nlattr *attr;
	unsigned long index;
	int ret;

	xa_for_each(&pin->dpll_refs, index, ref) {
		attr = nla_nest_start(msg, DPLL_A_PIN_PARENT_DEVICE);
		if (!attr)
			return -EMSGSIZE;
		ret = dpll_msg_add_dev_parent_handle(msg, ref->dpll->id);
		if (ret)
			goto nest_cancel;
		ret = dpll_msg_add_pin_on_dpll_state(msg, pin, ref, extack);
		if (ret)
			goto nest_cancel;
		ret = dpll_msg_add_pin_prio(msg, pin, ref, extack);
		if (ret)
			goto nest_cancel;
		ret = dpll_msg_add_pin_direction(msg, pin, ref, extack);
		if (ret)
			goto nest_cancel;
		ret = dpll_msg_add_phase_offset(msg, pin, ref, extack);
		if (ret)
			goto nest_cancel;
		nla_nest_end(msg, attr);
	}

	return 0;

nest_cancel:
	nla_nest_end(msg, attr);
	return ret;
}

static int
dpll_cmd_pin_get_one(struct sk_buff *msg, struct dpll_pin *pin,
		     struct netlink_ext_ack *extack)
{
	const struct dpll_pin_properties *prop = &pin->prop;
	struct dpll_pin_ref *ref;
	int ret;

	ref = dpll_xa_ref_dpll_first(&pin->dpll_refs);
	ASSERT_NOT_NULL(ref);

	ret = dpll_msg_add_pin_handle(msg, pin);
	if (ret)
		return ret;
	if (nla_put_string(msg, DPLL_A_PIN_MODULE_NAME,
			   module_name(pin->module)))
		return -EMSGSIZE;
	if (nla_put_64bit(msg, DPLL_A_PIN_CLOCK_ID, sizeof(pin->clock_id),
			  &pin->clock_id, DPLL_A_PIN_PAD))
		return -EMSGSIZE;
	if (prop->board_label &&
	    nla_put_string(msg, DPLL_A_PIN_BOARD_LABEL, prop->board_label))
		return -EMSGSIZE;
	if (prop->panel_label &&
	    nla_put_string(msg, DPLL_A_PIN_PANEL_LABEL, prop->panel_label))
		return -EMSGSIZE;
	if (prop->package_label &&
	    nla_put_string(msg, DPLL_A_PIN_PACKAGE_LABEL,
			   prop->package_label))
		return -EMSGSIZE;
	if (nla_put_u32(msg, DPLL_A_PIN_TYPE, prop->type))
		return -EMSGSIZE;
	if (nla_put_u32(msg, DPLL_A_PIN_CAPABILITIES, prop->capabilities))
		return -EMSGSIZE;
	ret = dpll_msg_add_pin_freq(msg, pin, ref, extack);
	if (ret)
		return ret;
	if (nla_put_s32(msg, DPLL_A_PIN_PHASE_ADJUST_MIN,
			prop->phase_range.min))
		return -EMSGSIZE;
	if (nla_put_s32(msg, DPLL_A_PIN_PHASE_ADJUST_MAX,
			prop->phase_range.max))
		return -EMSGSIZE;
	ret = dpll_msg_add_pin_phase_adjust(msg, pin, ref, extack);
	if (ret)
		return ret;
	ret = dpll_msg_add_ffo(msg, pin, ref, extack);
	if (ret)
		return ret;
	if (xa_empty(&pin->parent_refs))
		ret = dpll_msg_add_pin_dplls(msg, pin, extack);
	else
		ret = dpll_msg_add_pin_parents(msg, pin, ref, extack);

	return ret;
}

static int
dpll_device_get_one(struct dpll_device *dpll, struct sk_buff *msg,
		    struct netlink_ext_ack *extack)
{
	int ret;

	ret = dpll_msg_add_dev_handle(msg, dpll);
	if (ret)
		return ret;
	if (nla_put_string(msg, DPLL_A_MODULE_NAME, module_name(dpll->module)))
		return -EMSGSIZE;
	if (nla_put_64bit(msg, DPLL_A_CLOCK_ID, sizeof(dpll->clock_id),
			  &dpll->clock_id, DPLL_A_PAD))
		return -EMSGSIZE;
	ret = dpll_msg_add_temp(msg, dpll, extack);
	if (ret)
		return ret;
	ret = dpll_msg_add_lock_status(msg, dpll, extack);
	if (ret)
		return ret;
	ret = dpll_msg_add_mode(msg, dpll, extack);
	if (ret)
		return ret;
	ret = dpll_msg_add_mode_supported(msg, dpll, extack);
	if (ret)
		return ret;
	if (nla_put_u32(msg, DPLL_A_TYPE, dpll->type))
		return -EMSGSIZE;

	return 0;
}

static int
dpll_device_event_send(enum dpll_cmd event, struct dpll_device *dpll)
{
	struct sk_buff *msg;
	int ret = -ENOMEM;
	void *hdr;

	if (WARN_ON(!xa_get_mark(&dpll_device_xa, dpll->id, DPLL_REGISTERED)))
		return -ENODEV;
	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	hdr = genlmsg_put(msg, 0, 0, &dpll_nl_family, 0, event);
	if (!hdr)
		goto err_free_msg;
	ret = dpll_device_get_one(dpll, msg, NULL);
	if (ret)
		goto err_cancel_msg;
	genlmsg_end(msg, hdr);
	genlmsg_multicast(&dpll_nl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

err_cancel_msg:
	genlmsg_cancel(msg, hdr);
err_free_msg:
	nlmsg_free(msg);

	return ret;
}

int dpll_device_create_ntf(struct dpll_device *dpll)
{
	return dpll_device_event_send(DPLL_CMD_DEVICE_CREATE_NTF, dpll);
}

int dpll_device_delete_ntf(struct dpll_device *dpll)
{
	return dpll_device_event_send(DPLL_CMD_DEVICE_DELETE_NTF, dpll);
}

static int
__dpll_device_change_ntf(struct dpll_device *dpll)
{
	return dpll_device_event_send(DPLL_CMD_DEVICE_CHANGE_NTF, dpll);
}

static bool dpll_pin_available(struct dpll_pin *pin)
{
	struct dpll_pin_ref *par_ref;
	unsigned long i;

	if (!xa_get_mark(&dpll_pin_xa, pin->id, DPLL_REGISTERED))
		return false;
	xa_for_each(&pin->parent_refs, i, par_ref)
		if (xa_get_mark(&dpll_pin_xa, par_ref->pin->id,
				DPLL_REGISTERED))
			return true;
	xa_for_each(&pin->dpll_refs, i, par_ref)
		if (xa_get_mark(&dpll_device_xa, par_ref->dpll->id,
				DPLL_REGISTERED))
			return true;
	return false;
}

/**
 * dpll_device_change_ntf - notify that the dpll device has been changed
 * @dpll: registered dpll pointer
 *
 * Context: acquires and holds a dpll_lock.
 * Return: 0 if succeeds, error code otherwise.
 */
int dpll_device_change_ntf(struct dpll_device *dpll)
{
	int ret;

	mutex_lock(&dpll_lock);
	ret = __dpll_device_change_ntf(dpll);
	mutex_unlock(&dpll_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dpll_device_change_ntf);

static int
dpll_pin_event_send(enum dpll_cmd event, struct dpll_pin *pin)
{
	struct sk_buff *msg;
	int ret = -ENOMEM;
	void *hdr;

	if (!dpll_pin_available(pin))
		return -ENODEV;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &dpll_nl_family, 0, event);
	if (!hdr)
		goto err_free_msg;
	ret = dpll_cmd_pin_get_one(msg, pin, NULL);
	if (ret)
		goto err_cancel_msg;
	genlmsg_end(msg, hdr);
	genlmsg_multicast(&dpll_nl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

err_cancel_msg:
	genlmsg_cancel(msg, hdr);
err_free_msg:
	nlmsg_free(msg);

	return ret;
}

int dpll_pin_create_ntf(struct dpll_pin *pin)
{
	return dpll_pin_event_send(DPLL_CMD_PIN_CREATE_NTF, pin);
}

int dpll_pin_delete_ntf(struct dpll_pin *pin)
{
	return dpll_pin_event_send(DPLL_CMD_PIN_DELETE_NTF, pin);
}

static int __dpll_pin_change_ntf(struct dpll_pin *pin)
{
	return dpll_pin_event_send(DPLL_CMD_PIN_CHANGE_NTF, pin);
}

/**
 * dpll_pin_change_ntf - notify that the pin has been changed
 * @pin: registered pin pointer
 *
 * Context: acquires and holds a dpll_lock.
 * Return: 0 if succeeds, error code otherwise.
 */
int dpll_pin_change_ntf(struct dpll_pin *pin)
{
	int ret;

	mutex_lock(&dpll_lock);
	ret = __dpll_pin_change_ntf(pin);
	mutex_unlock(&dpll_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dpll_pin_change_ntf);

static int
dpll_pin_freq_set(struct dpll_pin *pin, struct nlattr *a,
		  struct netlink_ext_ack *extack)
{
	u64 freq = nla_get_u64(a), old_freq;
	struct dpll_pin_ref *ref, *failed;
	const struct dpll_pin_ops *ops;
	struct dpll_device *dpll;
	unsigned long i;
	int ret;

	if (!dpll_pin_is_freq_supported(pin, freq)) {
		NL_SET_ERR_MSG_ATTR(extack, a, "frequency is not supported by the device");
		return -EINVAL;
	}

	xa_for_each(&pin->dpll_refs, i, ref) {
		ops = dpll_pin_ops(ref);
		if (!ops->frequency_set || !ops->frequency_get) {
			NL_SET_ERR_MSG(extack, "frequency set not supported by the device");
			return -EOPNOTSUPP;
		}
	}
	ref = dpll_xa_ref_dpll_first(&pin->dpll_refs);
	ops = dpll_pin_ops(ref);
	dpll = ref->dpll;
	ret = ops->frequency_get(pin, dpll_pin_on_dpll_priv(dpll, pin), dpll,
				 dpll_priv(dpll), &old_freq, extack);
	if (ret) {
		NL_SET_ERR_MSG(extack, "unable to get old frequency value");
		return ret;
	}
	if (freq == old_freq)
		return 0;

	xa_for_each(&pin->dpll_refs, i, ref) {
		ops = dpll_pin_ops(ref);
		dpll = ref->dpll;
		ret = ops->frequency_set(pin, dpll_pin_on_dpll_priv(dpll, pin),
					 dpll, dpll_priv(dpll), freq, extack);
		if (ret) {
			failed = ref;
			NL_SET_ERR_MSG_FMT(extack, "frequency set failed for dpll_id:%u",
					   dpll->id);
			goto rollback;
		}
	}
	__dpll_pin_change_ntf(pin);

	return 0;

rollback:
	xa_for_each(&pin->dpll_refs, i, ref) {
		if (ref == failed)
			break;
		ops = dpll_pin_ops(ref);
		dpll = ref->dpll;
		if (ops->frequency_set(pin, dpll_pin_on_dpll_priv(dpll, pin),
				       dpll, dpll_priv(dpll), old_freq, extack))
			NL_SET_ERR_MSG(extack, "set frequency rollback failed");
	}
	return ret;
}

static int
dpll_pin_on_pin_state_set(struct dpll_pin *pin, u32 parent_idx,
			  enum dpll_pin_state state,
			  struct netlink_ext_ack *extack)
{
	struct dpll_pin_ref *parent_ref;
	const struct dpll_pin_ops *ops;
	struct dpll_pin_ref *dpll_ref;
	void *pin_priv, *parent_priv;
	struct dpll_pin *parent;
	unsigned long i;
	int ret;

	if (!(DPLL_PIN_CAPABILITIES_STATE_CAN_CHANGE &
	      pin->prop.capabilities)) {
		NL_SET_ERR_MSG(extack, "state changing is not allowed");
		return -EOPNOTSUPP;
	}
	parent = xa_load(&dpll_pin_xa, parent_idx);
	if (!parent)
		return -EINVAL;
	parent_ref = xa_load(&pin->parent_refs, parent->pin_idx);
	if (!parent_ref)
		return -EINVAL;
	xa_for_each(&parent->dpll_refs, i, dpll_ref) {
		ops = dpll_pin_ops(parent_ref);
		if (!ops->state_on_pin_set)
			return -EOPNOTSUPP;
		pin_priv = dpll_pin_on_pin_priv(parent, pin);
		parent_priv = dpll_pin_on_dpll_priv(dpll_ref->dpll, parent);
		ret = ops->state_on_pin_set(pin, pin_priv, parent, parent_priv,
					    state, extack);
		if (ret)
			return ret;
	}
	__dpll_pin_change_ntf(pin);

	return 0;
}

static int
dpll_pin_state_set(struct dpll_device *dpll, struct dpll_pin *pin,
		   enum dpll_pin_state state,
		   struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops;
	struct dpll_pin_ref *ref;
	int ret;

	if (!(DPLL_PIN_CAPABILITIES_STATE_CAN_CHANGE &
	      pin->prop.capabilities)) {
		NL_SET_ERR_MSG(extack, "state changing is not allowed");
		return -EOPNOTSUPP;
	}
	ref = xa_load(&pin->dpll_refs, dpll->id);
	ASSERT_NOT_NULL(ref);
	ops = dpll_pin_ops(ref);
	if (!ops->state_on_dpll_set)
		return -EOPNOTSUPP;
	ret = ops->state_on_dpll_set(pin, dpll_pin_on_dpll_priv(dpll, pin),
				     dpll, dpll_priv(dpll), state, extack);
	if (ret)
		return ret;
	__dpll_pin_change_ntf(pin);

	return 0;
}

static int
dpll_pin_prio_set(struct dpll_device *dpll, struct dpll_pin *pin,
		  u32 prio, struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops;
	struct dpll_pin_ref *ref;
	int ret;

	if (!(DPLL_PIN_CAPABILITIES_PRIORITY_CAN_CHANGE &
	      pin->prop.capabilities)) {
		NL_SET_ERR_MSG(extack, "prio changing is not allowed");
		return -EOPNOTSUPP;
	}
	ref = xa_load(&pin->dpll_refs, dpll->id);
	ASSERT_NOT_NULL(ref);
	ops = dpll_pin_ops(ref);
	if (!ops->prio_set)
		return -EOPNOTSUPP;
	ret = ops->prio_set(pin, dpll_pin_on_dpll_priv(dpll, pin), dpll,
			    dpll_priv(dpll), prio, extack);
	if (ret)
		return ret;
	__dpll_pin_change_ntf(pin);

	return 0;
}

static int
dpll_pin_direction_set(struct dpll_pin *pin, struct dpll_device *dpll,
		       enum dpll_pin_direction direction,
		       struct netlink_ext_ack *extack)
{
	const struct dpll_pin_ops *ops;
	struct dpll_pin_ref *ref;
	int ret;

	if (!(DPLL_PIN_CAPABILITIES_DIRECTION_CAN_CHANGE &
	      pin->prop.capabilities)) {
		NL_SET_ERR_MSG(extack, "direction changing is not allowed");
		return -EOPNOTSUPP;
	}
	ref = xa_load(&pin->dpll_refs, dpll->id);
	ASSERT_NOT_NULL(ref);
	ops = dpll_pin_ops(ref);
	if (!ops->direction_set)
		return -EOPNOTSUPP;
	ret = ops->direction_set(pin, dpll_pin_on_dpll_priv(dpll, pin),
				 dpll, dpll_priv(dpll), direction, extack);
	if (ret)
		return ret;
	__dpll_pin_change_ntf(pin);

	return 0;
}

static int
dpll_pin_phase_adj_set(struct dpll_pin *pin, struct nlattr *phase_adj_attr,
		       struct netlink_ext_ack *extack)
{
	struct dpll_pin_ref *ref, *failed;
	const struct dpll_pin_ops *ops;
	s32 phase_adj, old_phase_adj;
	struct dpll_device *dpll;
	unsigned long i;
	int ret;

	phase_adj = nla_get_s32(phase_adj_attr);
	if (phase_adj > pin->prop.phase_range.max ||
	    phase_adj < pin->prop.phase_range.min) {
		NL_SET_ERR_MSG_ATTR(extack, phase_adj_attr,
				    "phase adjust value not supported");
		return -EINVAL;
	}

	xa_for_each(&pin->dpll_refs, i, ref) {
		ops = dpll_pin_ops(ref);
		if (!ops->phase_adjust_set || !ops->phase_adjust_get) {
			NL_SET_ERR_MSG(extack, "phase adjust not supported");
			return -EOPNOTSUPP;
		}
	}
	ref = dpll_xa_ref_dpll_first(&pin->dpll_refs);
	ops = dpll_pin_ops(ref);
	dpll = ref->dpll;
	ret = ops->phase_adjust_get(pin, dpll_pin_on_dpll_priv(dpll, pin),
				    dpll, dpll_priv(dpll), &old_phase_adj,
				    extack);
	if (ret) {
		NL_SET_ERR_MSG(extack, "unable to get old phase adjust value");
		return ret;
	}
	if (phase_adj == old_phase_adj)
		return 0;

	xa_for_each(&pin->dpll_refs, i, ref) {
		ops = dpll_pin_ops(ref);
		dpll = ref->dpll;
		ret = ops->phase_adjust_set(pin,
					    dpll_pin_on_dpll_priv(dpll, pin),
					    dpll, dpll_priv(dpll), phase_adj,
					    extack);
		if (ret) {
			failed = ref;
			NL_SET_ERR_MSG_FMT(extack,
					   "phase adjust set failed for dpll_id:%u",
					   dpll->id);
			goto rollback;
		}
	}
	__dpll_pin_change_ntf(pin);

	return 0;

rollback:
	xa_for_each(&pin->dpll_refs, i, ref) {
		if (ref == failed)
			break;
		ops = dpll_pin_ops(ref);
		dpll = ref->dpll;
		if (ops->phase_adjust_set(pin, dpll_pin_on_dpll_priv(dpll, pin),
					  dpll, dpll_priv(dpll), old_phase_adj,
					  extack))
			NL_SET_ERR_MSG(extack, "set phase adjust rollback failed");
	}
	return ret;
}

static int
dpll_pin_parent_device_set(struct dpll_pin *pin, struct nlattr *parent_nest,
			   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[DPLL_A_PIN_MAX + 1];
	enum dpll_pin_direction direction;
	enum dpll_pin_state state;
	struct dpll_pin_ref *ref;
	struct dpll_device *dpll;
	u32 pdpll_idx, prio;
	int ret;

	nla_parse_nested(tb, DPLL_A_PIN_MAX, parent_nest,
			 dpll_pin_parent_device_nl_policy, extack);
	if (!tb[DPLL_A_PIN_PARENT_ID]) {
		NL_SET_ERR_MSG(extack, "device parent id expected");
		return -EINVAL;
	}
	pdpll_idx = nla_get_u32(tb[DPLL_A_PIN_PARENT_ID]);
	dpll = xa_load(&dpll_device_xa, pdpll_idx);
	if (!dpll) {
		NL_SET_ERR_MSG(extack, "parent device not found");
		return -EINVAL;
	}
	ref = xa_load(&pin->dpll_refs, dpll->id);
	if (!ref) {
		NL_SET_ERR_MSG(extack, "pin not connected to given parent device");
		return -EINVAL;
	}
	if (tb[DPLL_A_PIN_STATE]) {
		state = nla_get_u32(tb[DPLL_A_PIN_STATE]);
		ret = dpll_pin_state_set(dpll, pin, state, extack);
		if (ret)
			return ret;
	}
	if (tb[DPLL_A_PIN_PRIO]) {
		prio = nla_get_u32(tb[DPLL_A_PIN_PRIO]);
		ret = dpll_pin_prio_set(dpll, pin, prio, extack);
		if (ret)
			return ret;
	}
	if (tb[DPLL_A_PIN_DIRECTION]) {
		direction = nla_get_u32(tb[DPLL_A_PIN_DIRECTION]);
		ret = dpll_pin_direction_set(pin, dpll, direction, extack);
		if (ret)
			return ret;
	}
	return 0;
}

static int
dpll_pin_parent_pin_set(struct dpll_pin *pin, struct nlattr *parent_nest,
			struct netlink_ext_ack *extack)
{
	struct nlattr *tb[DPLL_A_PIN_MAX + 1];
	u32 ppin_idx;
	int ret;

	nla_parse_nested(tb, DPLL_A_PIN_MAX, parent_nest,
			 dpll_pin_parent_pin_nl_policy, extack);
	if (!tb[DPLL_A_PIN_PARENT_ID]) {
		NL_SET_ERR_MSG(extack, "device parent id expected");
		return -EINVAL;
	}
	ppin_idx = nla_get_u32(tb[DPLL_A_PIN_PARENT_ID]);

	if (tb[DPLL_A_PIN_STATE]) {
		enum dpll_pin_state state = nla_get_u32(tb[DPLL_A_PIN_STATE]);

		ret = dpll_pin_on_pin_state_set(pin, ppin_idx, state, extack);
		if (ret)
			return ret;
	}

	return 0;
}

static int
dpll_pin_set_from_nlattr(struct dpll_pin *pin, struct genl_info *info)
{
	struct nlattr *a;
	int rem, ret;

	nla_for_each_attr(a, genlmsg_data(info->genlhdr),
			  genlmsg_len(info->genlhdr), rem) {
		switch (nla_type(a)) {
		case DPLL_A_PIN_FREQUENCY:
			ret = dpll_pin_freq_set(pin, a, info->extack);
			if (ret)
				return ret;
			break;
		case DPLL_A_PIN_PHASE_ADJUST:
			ret = dpll_pin_phase_adj_set(pin, a, info->extack);
			if (ret)
				return ret;
			break;
		case DPLL_A_PIN_PARENT_DEVICE:
			ret = dpll_pin_parent_device_set(pin, a, info->extack);
			if (ret)
				return ret;
			break;
		case DPLL_A_PIN_PARENT_PIN:
			ret = dpll_pin_parent_pin_set(pin, a, info->extack);
			if (ret)
				return ret;
			break;
		}
	}

	return 0;
}

static struct dpll_pin *
dpll_pin_find(u64 clock_id, struct nlattr *mod_name_attr,
	      enum dpll_pin_type type, struct nlattr *board_label,
	      struct nlattr *panel_label, struct nlattr *package_label,
	      struct netlink_ext_ack *extack)
{
	bool board_match, panel_match, package_match;
	struct dpll_pin *pin_match = NULL, *pin;
	const struct dpll_pin_properties *prop;
	bool cid_match, mod_match, type_match;
	unsigned long i;

	xa_for_each_marked(&dpll_pin_xa, i, pin, DPLL_REGISTERED) {
		prop = &pin->prop;
		cid_match = clock_id ? pin->clock_id == clock_id : true;
		mod_match = mod_name_attr && module_name(pin->module) ?
			!nla_strcmp(mod_name_attr,
				    module_name(pin->module)) : true;
		type_match = type ? prop->type == type : true;
		board_match = board_label ? (prop->board_label ?
			!nla_strcmp(board_label, prop->board_label) : false) :
			true;
		panel_match = panel_label ? (prop->panel_label ?
			!nla_strcmp(panel_label, prop->panel_label) : false) :
			true;
		package_match = package_label ? (prop->package_label ?
			!nla_strcmp(package_label, prop->package_label) :
			false) : true;
		if (cid_match && mod_match && type_match && board_match &&
		    panel_match && package_match) {
			if (pin_match) {
				NL_SET_ERR_MSG(extack, "multiple matches");
				return ERR_PTR(-EINVAL);
			}
			pin_match = pin;
		}
	}
	if (!pin_match) {
		NL_SET_ERR_MSG(extack, "not found");
		return ERR_PTR(-ENODEV);
	}
	return pin_match;
}

static struct dpll_pin *dpll_pin_find_from_nlattr(struct genl_info *info)
{
	struct nlattr *attr, *mod_name_attr = NULL, *board_label_attr = NULL,
		*panel_label_attr = NULL, *package_label_attr = NULL;
	enum dpll_pin_type type = 0;
	u64 clock_id = 0;
	int rem = 0;

	nla_for_each_attr(attr, genlmsg_data(info->genlhdr),
			  genlmsg_len(info->genlhdr), rem) {
		switch (nla_type(attr)) {
		case DPLL_A_PIN_CLOCK_ID:
			if (clock_id)
				goto duplicated_attr;
			clock_id = nla_get_u64(attr);
			break;
		case DPLL_A_PIN_MODULE_NAME:
			if (mod_name_attr)
				goto duplicated_attr;
			mod_name_attr = attr;
			break;
		case DPLL_A_PIN_TYPE:
			if (type)
				goto duplicated_attr;
			type = nla_get_u32(attr);
		break;
		case DPLL_A_PIN_BOARD_LABEL:
			if (board_label_attr)
				goto duplicated_attr;
			board_label_attr = attr;
		break;
		case DPLL_A_PIN_PANEL_LABEL:
			if (panel_label_attr)
				goto duplicated_attr;
			panel_label_attr = attr;
		break;
		case DPLL_A_PIN_PACKAGE_LABEL:
			if (package_label_attr)
				goto duplicated_attr;
			package_label_attr = attr;
		break;
		default:
			break;
		}
	}
	if (!(clock_id  || mod_name_attr || board_label_attr ||
	      panel_label_attr || package_label_attr)) {
		NL_SET_ERR_MSG(info->extack, "missing attributes");
		return ERR_PTR(-EINVAL);
	}
	return dpll_pin_find(clock_id, mod_name_attr, type, board_label_attr,
			     panel_label_attr, package_label_attr,
			     info->extack);
duplicated_attr:
	NL_SET_ERR_MSG(info->extack, "duplicated attribute");
	return ERR_PTR(-EINVAL);
}

int dpll_nl_pin_id_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct dpll_pin *pin;
	struct sk_buff *msg;
	struct nlattr *hdr;
	int ret;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	hdr = genlmsg_put_reply(msg, info, &dpll_nl_family, 0,
				DPLL_CMD_PIN_ID_GET);
	if (!hdr) {
		nlmsg_free(msg);
		return -EMSGSIZE;
	}
	pin = dpll_pin_find_from_nlattr(info);
	if (!IS_ERR(pin)) {
		if (!dpll_pin_available(pin)) {
			nlmsg_free(msg);
			return -ENODEV;
		}
		ret = dpll_msg_add_pin_handle(msg, pin);
		if (ret) {
			nlmsg_free(msg);
			return ret;
		}
	}
	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);
}

int dpll_nl_pin_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct dpll_pin *pin = info->user_ptr[0];
	struct sk_buff *msg;
	struct nlattr *hdr;
	int ret;

	if (!pin)
		return -ENODEV;
	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	hdr = genlmsg_put_reply(msg, info, &dpll_nl_family, 0,
				DPLL_CMD_PIN_GET);
	if (!hdr) {
		nlmsg_free(msg);
		return -EMSGSIZE;
	}
	ret = dpll_cmd_pin_get_one(msg, pin, info->extack);
	if (ret) {
		nlmsg_free(msg);
		return ret;
	}
	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);
}

int dpll_nl_pin_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct dpll_dump_ctx *ctx = dpll_dump_context(cb);
	struct dpll_pin *pin;
	struct nlattr *hdr;
	unsigned long i;
	int ret = 0;

	mutex_lock(&dpll_lock);
	xa_for_each_marked_start(&dpll_pin_xa, i, pin, DPLL_REGISTERED,
				 ctx->idx) {
		if (!dpll_pin_available(pin))
			continue;
		hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid,
				  cb->nlh->nlmsg_seq,
				  &dpll_nl_family, NLM_F_MULTI,
				  DPLL_CMD_PIN_GET);
		if (!hdr) {
			ret = -EMSGSIZE;
			break;
		}
		ret = dpll_cmd_pin_get_one(skb, pin, cb->extack);
		if (ret) {
			genlmsg_cancel(skb, hdr);
			break;
		}
		genlmsg_end(skb, hdr);
	}
	mutex_unlock(&dpll_lock);

	if (ret == -EMSGSIZE) {
		ctx->idx = i;
		return skb->len;
	}
	return ret;
}

int dpll_nl_pin_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct dpll_pin *pin = info->user_ptr[0];

	return dpll_pin_set_from_nlattr(pin, info);
}

static struct dpll_device *
dpll_device_find(u64 clock_id, struct nlattr *mod_name_attr,
		 enum dpll_type type, struct netlink_ext_ack *extack)
{
	struct dpll_device *dpll_match = NULL, *dpll;
	bool cid_match, mod_match, type_match;
	unsigned long i;

	xa_for_each_marked(&dpll_device_xa, i, dpll, DPLL_REGISTERED) {
		cid_match = clock_id ? dpll->clock_id == clock_id : true;
		mod_match = mod_name_attr ? (module_name(dpll->module) ?
			!nla_strcmp(mod_name_attr,
				    module_name(dpll->module)) : false) : true;
		type_match = type ? dpll->type == type : true;
		if (cid_match && mod_match && type_match) {
			if (dpll_match) {
				NL_SET_ERR_MSG(extack, "multiple matches");
				return ERR_PTR(-EINVAL);
			}
			dpll_match = dpll;
		}
	}
	if (!dpll_match) {
		NL_SET_ERR_MSG(extack, "not found");
		return ERR_PTR(-ENODEV);
	}

	return dpll_match;
}

static struct dpll_device *
dpll_device_find_from_nlattr(struct genl_info *info)
{
	struct nlattr *attr, *mod_name_attr = NULL;
	enum dpll_type type = 0;
	u64 clock_id = 0;
	int rem = 0;

	nla_for_each_attr(attr, genlmsg_data(info->genlhdr),
			  genlmsg_len(info->genlhdr), rem) {
		switch (nla_type(attr)) {
		case DPLL_A_CLOCK_ID:
			if (clock_id)
				goto duplicated_attr;
			clock_id = nla_get_u64(attr);
			break;
		case DPLL_A_MODULE_NAME:
			if (mod_name_attr)
				goto duplicated_attr;
			mod_name_attr = attr;
			break;
		case DPLL_A_TYPE:
			if (type)
				goto duplicated_attr;
			type = nla_get_u32(attr);
			break;
		default:
			break;
		}
	}
	if (!clock_id && !mod_name_attr && !type) {
		NL_SET_ERR_MSG(info->extack, "missing attributes");
		return ERR_PTR(-EINVAL);
	}
	return dpll_device_find(clock_id, mod_name_attr, type, info->extack);
duplicated_attr:
	NL_SET_ERR_MSG(info->extack, "duplicated attribute");
	return ERR_PTR(-EINVAL);
}

int dpll_nl_device_id_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct dpll_device *dpll;
	struct sk_buff *msg;
	struct nlattr *hdr;
	int ret;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	hdr = genlmsg_put_reply(msg, info, &dpll_nl_family, 0,
				DPLL_CMD_DEVICE_ID_GET);
	if (!hdr) {
		nlmsg_free(msg);
		return -EMSGSIZE;
	}

	dpll = dpll_device_find_from_nlattr(info);
	if (!IS_ERR(dpll)) {
		ret = dpll_msg_add_dev_handle(msg, dpll);
		if (ret) {
			nlmsg_free(msg);
			return ret;
		}
	}
	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);
}

int dpll_nl_device_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct dpll_device *dpll = info->user_ptr[0];
	struct sk_buff *msg;
	struct nlattr *hdr;
	int ret;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	hdr = genlmsg_put_reply(msg, info, &dpll_nl_family, 0,
				DPLL_CMD_DEVICE_GET);
	if (!hdr) {
		nlmsg_free(msg);
		return -EMSGSIZE;
	}

	ret = dpll_device_get_one(dpll, msg, info->extack);
	if (ret) {
		nlmsg_free(msg);
		return ret;
	}
	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);
}

int dpll_nl_device_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	/* placeholder for set command */
	return 0;
}

int dpll_nl_device_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct dpll_dump_ctx *ctx = dpll_dump_context(cb);
	struct dpll_device *dpll;
	struct nlattr *hdr;
	unsigned long i;
	int ret = 0;

	mutex_lock(&dpll_lock);
	xa_for_each_marked_start(&dpll_device_xa, i, dpll, DPLL_REGISTERED,
				 ctx->idx) {
		hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid,
				  cb->nlh->nlmsg_seq, &dpll_nl_family,
				  NLM_F_MULTI, DPLL_CMD_DEVICE_GET);
		if (!hdr) {
			ret = -EMSGSIZE;
			break;
		}
		ret = dpll_device_get_one(dpll, skb, cb->extack);
		if (ret) {
			genlmsg_cancel(skb, hdr);
			break;
		}
		genlmsg_end(skb, hdr);
	}
	mutex_unlock(&dpll_lock);

	if (ret == -EMSGSIZE) {
		ctx->idx = i;
		return skb->len;
	}
	return ret;
}

int dpll_pre_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		  struct genl_info *info)
{
	u32 id;

	if (GENL_REQ_ATTR_CHECK(info, DPLL_A_ID))
		return -EINVAL;

	mutex_lock(&dpll_lock);
	id = nla_get_u32(info->attrs[DPLL_A_ID]);
	info->user_ptr[0] = dpll_device_get_by_id(id);
	if (!info->user_ptr[0]) {
		NL_SET_ERR_MSG(info->extack, "device not found");
		goto unlock;
	}
	return 0;
unlock:
	mutex_unlock(&dpll_lock);
	return -ENODEV;
}

void dpll_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		    struct genl_info *info)
{
	mutex_unlock(&dpll_lock);
}

int
dpll_lock_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
	       struct genl_info *info)
{
	mutex_lock(&dpll_lock);

	return 0;
}

void
dpll_unlock_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		 struct genl_info *info)
{
	mutex_unlock(&dpll_lock);
}

int dpll_pin_pre_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		      struct genl_info *info)
{
	int ret;

	mutex_lock(&dpll_lock);
	if (GENL_REQ_ATTR_CHECK(info, DPLL_A_PIN_ID)) {
		ret = -EINVAL;
		goto unlock_dev;
	}
	info->user_ptr[0] = xa_load(&dpll_pin_xa,
				    nla_get_u32(info->attrs[DPLL_A_PIN_ID]));
	if (!info->user_ptr[0] ||
	    !dpll_pin_available(info->user_ptr[0])) {
		NL_SET_ERR_MSG(info->extack, "pin not found");
		ret = -ENODEV;
		goto unlock_dev;
	}

	return 0;

unlock_dev:
	mutex_unlock(&dpll_lock);
	return ret;
}

void dpll_pin_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
			struct genl_info *info)
{
	mutex_unlock(&dpll_lock);
}
