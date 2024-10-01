/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#if !defined(__OSC_UNSOL_H__)
#define __OSC_UNSOL_H__

int
efct_unsolicited_cb(void *arg, struct efc_hw_sequence *seq);
int
efct_dispatch_fcp_cmd(struct efct_node *node, struct efc_hw_sequence *seq);
int
efct_node_recv_abts_frame(struct efct_node *node, struct efc_hw_sequence *seq);

#endif /* __OSC_UNSOL_H__ */
