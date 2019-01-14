/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 * Copyright (C) 2018 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/module.h>

/* sparse doesn't like tracepoint macros */
#ifndef __CHECKER__
#include "iwl-trans.h"

#define CREATE_TRACE_POINTS
#include "iwl-devtrace.h"

EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ucode_event);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ucode_cont_event);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ucode_wrap_event);
#endif
