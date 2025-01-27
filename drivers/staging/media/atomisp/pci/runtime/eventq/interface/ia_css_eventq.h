/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef _IA_CSS_EVENTQ_H
#define _IA_CSS_EVENTQ_H

#include "ia_css_queue.h"	/* queue APIs */

/**
 * @brief HOST receives event from SP.
 *
 * @param[in]	eventq_handle	eventq_handle.
 * @param[in]	payload		The event payload.
 * @return	0		- Successfully dequeue.
 * @return	-EINVAL		- Invalid argument.
 * @return	-ENODATA		- Queue is empty.
 */
int ia_css_eventq_recv(
    ia_css_queue_t *eventq_handle,
    uint8_t *payload);

/**
 * @brief The Host sends the event to SP.
 * The caller of this API will be blocked until the event
 * is sent.
 *
 * @param[in]	eventq_handle   eventq_handle.
 * @param[in]	evt_id		The event ID.
 * @param[in]	evt_payload_0	The event payload.
 * @param[in]	evt_payload_1	The event payload.
 * @param[in]	evt_payload_2	The event payload.
 * @return	0		- Successfully enqueue.
 * @return	-EINVAL		- Invalid argument.
 * @return	-ENOBUFS		- Queue is full.
 */
int ia_css_eventq_send(
    ia_css_queue_t *eventq_handle,
    u8 evt_id,
    u8 evt_payload_0,
    u8 evt_payload_1,
    uint8_t evt_payload_2);
#endif /* _IA_CSS_EVENTQ_H */
