/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GUC_HXG_HELPERS_H_
#define _XE_GUC_HXG_HELPERS_H_

#include <linux/bitfield.h>
#include <linux/types.h>

#include "abi/guc_messages_abi.h"

/**
 * hxg_sizeof - Queries size of the object or type (in HXG units).
 * @T: the object or type
 *
 * Force a compilation error if actual size is not aligned to HXG unit (u32).
 *
 * Return: size in dwords (u32).
 */
#define hxg_sizeof(T)	(sizeof(T) / sizeof(u32) + BUILD_BUG_ON_ZERO(sizeof(T) % sizeof(u32)))

static inline const char *guc_hxg_type_to_string(unsigned int type)
{
	switch (type) {
	case GUC_HXG_TYPE_REQUEST:
		return "request";
	case GUC_HXG_TYPE_FAST_REQUEST:
		return "fast-request";
	case GUC_HXG_TYPE_EVENT:
		return "event";
	case GUC_HXG_TYPE_NO_RESPONSE_BUSY:
		return "busy";
	case GUC_HXG_TYPE_NO_RESPONSE_RETRY:
		return "retry";
	case GUC_HXG_TYPE_RESPONSE_FAILURE:
		return "failure";
	case GUC_HXG_TYPE_RESPONSE_SUCCESS:
		return "response";
	default:
		return "<invalid>";
	}
}

static inline bool guc_hxg_type_is_action(unsigned int type)
{
	switch (type) {
	case GUC_HXG_TYPE_REQUEST:
	case GUC_HXG_TYPE_FAST_REQUEST:
	case GUC_HXG_TYPE_EVENT:
		return true;
	default:
		return false;
	}
}

static inline bool guc_hxg_type_is_reply(unsigned int type)
{
	switch (type) {
	case GUC_HXG_TYPE_NO_RESPONSE_BUSY:
	case GUC_HXG_TYPE_NO_RESPONSE_RETRY:
	case GUC_HXG_TYPE_RESPONSE_FAILURE:
	case GUC_HXG_TYPE_RESPONSE_SUCCESS:
		return true;
	default:
		return false;
	}
}

static inline u32 guc_hxg_msg_encode_success(u32 *msg, u32 data0)
{
	msg[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		 FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		 FIELD_PREP(GUC_HXG_RESPONSE_MSG_0_DATA0, data0);

	return GUC_HXG_RESPONSE_MSG_MIN_LEN;
}

static inline u32 guc_hxg_msg_encode_failure(u32 *msg, u32 error, u32 hint)
{
	msg[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		 FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_FAILURE) |
		 FIELD_PREP(GUC_HXG_FAILURE_MSG_0_HINT, hint) |
		 FIELD_PREP(GUC_HXG_FAILURE_MSG_0_ERROR, error);

	return GUC_HXG_FAILURE_MSG_LEN;
}

static inline u32 guc_hxg_msg_encode_busy(u32 *msg, u32 counter)
{
	msg[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		 FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_NO_RESPONSE_BUSY) |
		 FIELD_PREP(GUC_HXG_BUSY_MSG_0_COUNTER, counter);

	return GUC_HXG_BUSY_MSG_LEN;
}

static inline u32 guc_hxg_msg_encode_retry(u32 *msg, u32 reason)
{
	msg[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		 FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_NO_RESPONSE_RETRY) |
		 FIELD_PREP(GUC_HXG_RETRY_MSG_0_REASON, reason);

	return GUC_HXG_RETRY_MSG_LEN;
}

#endif
