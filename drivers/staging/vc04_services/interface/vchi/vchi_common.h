/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHI_COMMON_H_
#define VCHI_COMMON_H_

//Callback used by all services / bulk transfers
typedef void (*vchi_callback)(void *callback_param, //my service local param
			      enum vchiq_reason reason,
			      void *handle); //for transmitting msg's only

#endif // VCHI_COMMON_H_
