/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IXD_VIRTCHNL_H_
#define _IXD_VIRTCHNL_H_

int ixd_vc_dev_init(struct ixd_adapter *adapter);
bool ixd_vc_can_handle_msg(struct libie_ctlq_msg *ctlq_msg);
void ixd_vc_recv_event_msg(struct ixd_adapter *adapter,
			   struct libie_ctlq_msg *ctlq_msg);

#endif /* _IXD_VIRTCHNL_H_ */
