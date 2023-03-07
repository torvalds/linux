/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DWC3_DEBUG_IPC_H
#define __DWC3_DEBUG_IPC_H

#include "core.h"
#include "debug.h"
#include <linux/ipc_logging.h>

/*
 * NOTE: Make sure to have mdwc as local variable in function before using
 * below macros.
 */
#define dbg_event(ep_num, name, status) \
	dwc3_dbg_print(mdwc->dwc_ipc_log_ctxt, ep_num, name, status, "")

#define dbg_print(ep_num, name, status, extra) \
	dwc3_dbg_print(mdwc->dwc_ipc_log_ctxt, ep_num, name, status, extra)

#define dbg_print_reg(name, reg) \
	dwc3_dbg_print_reg(mdwc->dwc_ipc_log_ctxt, name, reg)

#define dbg_done(ep_num, count, status) \
	dwc3_dbg_done(mdwc->dwc_ipc_log_ctxt, ep_num, count, status)

#define dbg_queue(ep_num, req, status) \
	dwc3_dbg_queue(mdwc->dwc_ipc_log_ctxt, ep_num, req, status)

#define dbg_setup(ep_num, req) \
	dwc3_dbg_setup(mdwc->dwc_ipc_log_ctxt, ep_num, req)

#define dbg_ep_queue(ep_num, req) \
	dwc3_dbg_dma_queue(mdwc->dwc_ipc_log_ctxt, ep_num, req)

#define dbg_ep_dequeue(ep_num, req) \
	dwc3_dbg_dma_dequeue(mdwc->dwc_ipc_log_ctxt, ep_num, req)

#define dbg_ep_unmap(ep_num, req) \
	dwc3_dbg_dma_unmap(mdwc->dwc_dma_ipc_log_ctxt, ep_num, req)

#define dbg_ep_map(ep_num, req) \
	dwc3_dbg_dma_map(mdwc->dwc_dma_ipc_log_ctxt, ep_num, req)

#define dbg_log_string(fmt, ...) \
	ipc_log_string(mdwc->dwc_ipc_log_ctxt,\
			"%s: " fmt, __func__, ##__VA_ARGS__)

#define dbg_trace_ctrl_req(ctrl) \
	dwc3_dbg_trace_log_ctrl(dwc_trace_ipc_log_ctxt, ctrl)

#define dbg_trace_ep_queue(req) \
	dwc3_dbg_trace_log_request(dwc_trace_ipc_log_ctxt, req, "dbg_ep_queue")

#define dbg_trace_ep_dequeue(req) \
	dwc3_dbg_trace_log_request(dwc_trace_ipc_log_ctxt, req, "dbg_ep_dequeue")

#define dbg_trace_gadget_giveback(req) \
	dwc3_dbg_trace_log_request(dwc_trace_ipc_log_ctxt, req, "dbg_gadget_giveback")

#define dbg_trace_gadget_ep_cmd(dep, cmd, params, cmd_status) \
	dwc3_dbg_trace_ep_cmd(dwc_trace_ipc_log_ctxt, dep, cmd, params, cmd_status)

#define dbg_trace_trb_prepare(dep, event) \
	dwc3_dbg_trace_trb_complete(dwc_trace_ipc_log_ctxt, dep, trb, "dbg_prepare")

#define dbg_trace_trb_complete(dep, event) \
	dwc3_dbg_trace_trb_complete(dwc_trace_ipc_log_ctxt, dep, trb, "dbg_complete")

#define dbg_trace_event(event, dwc) \
	dwc3_dbg_trace_event(dwc_trace_ipc_log_ctxt, event, dwc)

#define dbg_trace_ep(dep) \
	dwc3_dbg_trace_ep(dwc_trace_ipc_log_ctxt, dep)

void dwc3_dbg_trace_ep(void *log_ctxt, struct dwc3_ep *dep);
void dwc3_dbg_trace_log_ctrl(void *log_ctxt, struct usb_ctrlrequest *ctrl);
void dwc3_dbg_trace_log_request(void *log_ctxt, struct dwc3_request *req,
				char *tag);
void dwc3_dbg_trace_ep_cmd(void *log_ctxt, struct dwc3_ep *dep,
				unsigned int cmd,
				struct dwc3_gadget_ep_cmd_params *params,
				int cmd_status);
void dwc3_dbg_trace_trb_complete(void *log_ctxt, struct dwc3_ep *dep,
					struct dwc3_trb *trb, char *tag);
void dwc3_dbg_trace_event(void *log_ctxt, u32 event, struct dwc3 *dwc);
void dwc3_dbg_print(void *log_ctxt, u8 ep_num,
		const char *name, int status, const char *extra);
void dwc3_dbg_done(void *log_ctxt, u8 ep_num,
		const u32 count, int status);
void dwc3_dbg_event(void *log_ctxt, u8 ep_num,
		const char *name, int status);
void dwc3_dbg_queue(void *log_ctxt, u8 ep_num,
		const struct usb_request *req, int status);
void dwc3_dbg_setup(void *log_ctxt, u8 ep_num,
		const struct usb_ctrlrequest *req);
void dwc3_dbg_print_reg(void *log_ctxt,
		const char *name, int reg);
void dwc3_dbg_dma_queue(void *log_ctxt, u8 ep_num,
			struct dwc3_request *req);
void dwc3_dbg_dma_dequeue(void *log_ctxt, u8 ep_num,
			struct dwc3_request *req);
void dwc3_dbg_dma_map(void *log_ctxt, u8 ep_num,
			struct dwc3_request *req);
void dwc3_dbg_dma_unmap(void *log_ctxt, u8 ep_num,
			struct dwc3_request *req);

#endif /* __DWC3_DEBUG_IPC_H */
