/******************************************************************************
 *
 * Copyright(c) 2018 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef _HALMAC_DBG_H_
#define _HALMAC_DBG_H_

#include "halmac_api.h"

#if HALMAC_DBG_MONITOR_IO
enum halmac_ret_status
mount_api_dbg(struct halmac_adapter *adapter);
#endif

#endif
