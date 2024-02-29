/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GUC_RELAY_H_
#define _XE_GUC_RELAY_H_

#include <linux/types.h>
#include <linux/errno.h>

struct xe_guc_relay;

int xe_guc_relay_init(struct xe_guc_relay *relay);

int xe_guc_relay_send_to_pf(struct xe_guc_relay *relay,
			    const u32 *msg, u32 len, u32 *buf, u32 buf_size);

int xe_guc_relay_process_guc2vf(struct xe_guc_relay *relay, const u32 *msg, u32 len);

#ifdef CONFIG_PCI_IOV
int xe_guc_relay_send_to_vf(struct xe_guc_relay *relay, u32 target,
			    const u32 *msg, u32 len, u32 *buf, u32 buf_size);
int xe_guc_relay_process_guc2pf(struct xe_guc_relay *relay, const u32 *msg, u32 len);
#else
static inline int xe_guc_relay_send_to_vf(struct xe_guc_relay *relay, u32 target,
					  const u32 *msg, u32 len, u32 *buf, u32 buf_size)
{
	return -ENODEV;
}
static inline int xe_guc_relay_process_guc2pf(struct xe_guc_relay *relay, const u32 *msg, u32 len)
{
	return -ENODEV;
}
#endif

#endif
