/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2022 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_context_h__
#define __iwl_fw_api_context_h__

/**
 * enum iwl_ctxt_id_and_color - ID and color fields in context dword
 * @FW_CTXT_ID_POS: position of the ID
 * @FW_CTXT_ID_MSK: mask of the ID
 * @FW_CTXT_COLOR_POS: position of the color
 * @FW_CTXT_COLOR_MSK: mask of the color
 * @FW_CTXT_INVALID: value used to indicate unused/invalid
 */
enum iwl_ctxt_id_and_color {
	FW_CTXT_ID_POS		= 0,
	FW_CTXT_ID_MSK		= 0xff << FW_CTXT_ID_POS,
	FW_CTXT_COLOR_POS	= 8,
	FW_CTXT_COLOR_MSK	= 0xff << FW_CTXT_COLOR_POS,
	FW_CTXT_INVALID		= 0xffffffff,
};

#define FW_CMD_ID_AND_COLOR(_id, _color) (((_id) << FW_CTXT_ID_POS) |\
					  ((_color) << FW_CTXT_COLOR_POS))

/**
 * enum iwl_ctxt_action - Posssible actions on PHYs, MACs, Bindings and other
 * @FW_CTXT_ACTION_INVALID: unused, invalid action
 * @FW_CTXT_ACTION_ADD: add the context
 * @FW_CTXT_ACTION_MODIFY: modify the context
 * @FW_CTXT_ACTION_REMOVE: remove the context
 */
enum iwl_ctxt_action {
	FW_CTXT_ACTION_INVALID = 0,
	FW_CTXT_ACTION_ADD,
	FW_CTXT_ACTION_MODIFY,
	FW_CTXT_ACTION_REMOVE,
}; /* COMMON_CONTEXT_ACTION_API_E_VER_1 */

#endif /* __iwl_fw_api_context_h__ */
