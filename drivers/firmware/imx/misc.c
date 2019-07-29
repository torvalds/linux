// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017~2018 NXP
 *  Author: Dong Aisheng <aisheng.dong@nxp.com>
 *
 * File containing client-side RPC functions for the MISC service. These
 * function are ported to clients that communicate to the SC.
 *
 */

#include <linux/firmware/imx/svc/misc.h>

struct imx_sc_msg_req_misc_set_ctrl {
	struct imx_sc_rpc_msg hdr;
	u32 ctrl;
	u32 val;
	u16 resource;
} __packed;

struct imx_sc_msg_req_misc_get_ctrl {
	struct imx_sc_rpc_msg hdr;
	u32 ctrl;
	u16 resource;
} __packed;

struct imx_sc_msg_resp_misc_get_ctrl {
	struct imx_sc_rpc_msg hdr;
	u32 val;
} __packed;

/*
 * This function sets a miscellaneous control value.
 *
 * @param[in]     ipc         IPC handle
 * @param[in]     resource    resource the control is associated with
 * @param[in]     ctrl        control to change
 * @param[in]     val         value to apply to the control
 *
 * @return Returns 0 for success and < 0 for errors.
 */

int imx_sc_misc_set_control(struct imx_sc_ipc *ipc, u32 resource,
			    u8 ctrl, u32 val)
{
	struct imx_sc_msg_req_misc_set_ctrl msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = (uint8_t)IMX_SC_RPC_SVC_MISC;
	hdr->func = (uint8_t)IMX_SC_MISC_FUNC_SET_CONTROL;
	hdr->size = 4;

	msg.ctrl = ctrl;
	msg.val = val;
	msg.resource = resource;

	return imx_scu_call_rpc(ipc, &msg, true);
}
EXPORT_SYMBOL(imx_sc_misc_set_control);

/*
 * This function gets a miscellaneous control value.
 *
 * @param[in]     ipc         IPC handle
 * @param[in]     resource    resource the control is associated with
 * @param[in]     ctrl        control to get
 * @param[out]    val         pointer to return the control value
 *
 * @return Returns 0 for success and < 0 for errors.
 */

int imx_sc_misc_get_control(struct imx_sc_ipc *ipc, u32 resource,
			    u8 ctrl, u32 *val)
{
	struct imx_sc_msg_req_misc_get_ctrl msg;
	struct imx_sc_msg_resp_misc_get_ctrl *resp;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = (uint8_t)IMX_SC_RPC_SVC_MISC;
	hdr->func = (uint8_t)IMX_SC_MISC_FUNC_GET_CONTROL;
	hdr->size = 3;

	msg.ctrl = ctrl;
	msg.resource = resource;

	ret = imx_scu_call_rpc(ipc, &msg, true);
	if (ret)
		return ret;

	resp = (struct imx_sc_msg_resp_misc_get_ctrl *)&msg;
	if (val != NULL)
		*val = resp->val;

	return 0;
}
EXPORT_SYMBOL(imx_sc_misc_get_control);
