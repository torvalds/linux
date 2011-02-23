/******************************************************************************
 *
 * Copyright(c) 2009 - 2011 Intel Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/module.h>

/* sparse doesn't like tracepoint macros */
#ifndef __CHECKER__
#include "iwl-dev.h"

#define CREATE_TRACE_POINTS
#include "iwl-devtrace.h"

EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_iowrite8);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_ioread32);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_iowrite32);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_rx);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_tx);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_ucode_event);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_ucode_error);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_ucode_cont_event);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_legacy_dev_ucode_wrap_event);
#endif
