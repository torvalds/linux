/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
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
