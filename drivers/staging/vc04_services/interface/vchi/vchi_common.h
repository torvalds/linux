/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHI_COMMON_H_
#define VCHI_COMMON_H_

//callback reasons when an event occurs on a service
enum vchi_callback_reason {
	/*
	 * This indicates that there is data available handle is the msg id that
	 * was transmitted with the data
	 * When a message is received and there was no FULL message available
	 * previously, send callback
	 * Tasks get kicked by the callback, reset their event and try and read
	 * from the fifo until it fails
	 */
	VCHI_CALLBACK_SERVICE_CLOSED,
	VCHI_CALLBACK_MSG_AVAILABLE,
	VCHI_CALLBACK_BULK_SENT,
	VCHI_CALLBACK_BULK_RECEIVED,
	VCHI_CALLBACK_BULK_TRANSMIT_ABORTED,
	VCHI_CALLBACK_BULK_RECEIVE_ABORTED,
};

//Callback used by all services / bulk transfers
typedef void (*vchi_callback)(void *callback_param, //my service local param
			      enum vchi_callback_reason reason,
			      void *handle); //for transmitting msg's only

#endif // VCHI_COMMON_H_
