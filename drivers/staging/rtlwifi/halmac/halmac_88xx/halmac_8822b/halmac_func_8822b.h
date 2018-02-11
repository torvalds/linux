/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
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
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_FUNC_8822B_H_
#define _HALMAC_FUNC_8822B_H_

#include "../../halmac_type.h"

enum halmac_ret_status
halmac_txdma_queue_mapping_8822b(struct halmac_adapter *halmac_adapter,
				 enum halmac_trx_mode halmac_trx_mode);

enum halmac_ret_status
halmac_priority_queue_config_8822b(struct halmac_adapter *halmac_adapter,
				   enum halmac_trx_mode halmac_trx_mode);

#endif /* _HALMAC_FUNC_8822B_H_ */
