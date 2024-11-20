/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __EVENT_FIFO_PUBLIC_H
#define __EVENT_FIFO_PUBLIC_H

#include <type_support.h>
#include "system_local.h"

/*! Blocking read from an event source EVENT[ID]

 \param	ID[in]				EVENT identifier

 \return none, dequeue(event_queue[ID])
 */
STORAGE_CLASS_EVENT_H void event_wait_for(
    const event_ID_t		ID);

/*! Conditional blocking wait for an event source EVENT[ID]

 \param	ID[in]				EVENT identifier
 \param	cnd[in]				predicate

 \return none, if(cnd) dequeue(event_queue[ID])
 */
STORAGE_CLASS_EVENT_H void cnd_event_wait_for(
    const event_ID_t		ID,
    const bool				cnd);

/*! Blocking read from an event source EVENT[ID]

 \param	ID[in]				EVENT identifier

 \return dequeue(event_queue[ID])
 */
STORAGE_CLASS_EVENT_H hrt_data event_receive_token(
    const event_ID_t		ID);

/*! Blocking write to an event sink EVENT[ID]

 \param	ID[in]				EVENT identifier
 \param	token[in]			token to be written on the event

 \return none, enqueue(event_queue[ID])
 */
STORAGE_CLASS_EVENT_H void event_send_token(
    const event_ID_t		ID,
    const hrt_data			token);

/*! Query an event source EVENT[ID]

 \param	ID[in]				EVENT identifier

 \return !isempty(event_queue[ID])
 */
STORAGE_CLASS_EVENT_H bool is_event_pending(
    const event_ID_t		ID);

/*! Query an event sink EVENT[ID]

 \param	ID[in]				EVENT identifier

 \return !isfull(event_queue[ID])
 */
STORAGE_CLASS_EVENT_H bool can_event_send_token(
    const event_ID_t		ID);

#endif /* __EVENT_FIFO_PUBLIC_H */
