/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Intel Corporation.
 * Intel Visual Sensing Controller Transport Layer Linux driver
 */

#ifndef _VSC_TP_H_
#define _VSC_TP_H_

#include <linux/types.h>

#define VSC_TP_CMD_WRITE	0x01
#define VSC_TP_CMD_READ		0x02

#define VSC_TP_CMD_ACK		0x10
#define VSC_TP_CMD_NACK		0x11
#define VSC_TP_CMD_BUSY		0x12

struct vsc_tp;

/**
 * typedef vsc_event_cb_t - event callback function signature
 * @context: the execution context of who registered this callback
 *
 * The callback function is called in interrupt context and the data
 * payload is only valid during the call. If the user needs access
 * the data payload later, it must copy the payload.
 */
typedef void (*vsc_tp_event_cb_t)(void *context);

int vsc_tp_rom_xfer(struct vsc_tp *tp, const void *obuf, void *ibuf,
		    size_t len);

int vsc_tp_xfer(struct vsc_tp *tp, u8 cmd, const void *obuf, size_t olen,
		void *ibuf, size_t ilen);

int vsc_tp_register_event_cb(struct vsc_tp *tp, vsc_tp_event_cb_t event_cb,
			     void *context);

int vsc_tp_request_irq(struct vsc_tp *tp);
void vsc_tp_free_irq(struct vsc_tp *tp);

void vsc_tp_intr_enable(struct vsc_tp *tp);
void vsc_tp_intr_disable(struct vsc_tp *tp);
void vsc_tp_intr_synchronize(struct vsc_tp *tp);

void vsc_tp_reset(struct vsc_tp *tp);

bool vsc_tp_need_read(struct vsc_tp *tp);

int vsc_tp_init(struct vsc_tp *tp, struct device *dev);

#endif
