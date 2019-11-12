/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
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

#ifndef _HALMAC_INIT_88XX_H_
#define _HALMAC_INIT_88XX_H_

#include "../halmac_api.h"

#if HALMAC_88XX_SUPPORT

enum halmac_ret_status
register_api_88xx(struct halmac_adapter *adapter,
		  struct halmac_api_registry *registry);

void
init_adapter_param_88xx(struct halmac_adapter *adapter);

void
init_adapter_dynamic_param_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
mount_api_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
init_mac_cfg_88xx(struct halmac_adapter *adapter, enum halmac_trx_mode mode);

enum halmac_ret_status
reset_ofld_feature_88xx(struct halmac_adapter *adapter,
			enum halmac_feature_id feature_id);

enum halmac_ret_status
verify_platform_api_88xx(struct halmac_adapter *adapter);

void
tx_desc_chksum_88xx(struct halmac_adapter *adapter, u8 enable);

enum halmac_ret_status
pg_num_parser_88xx(struct halmac_adapter *adapter, enum halmac_trx_mode mode,
		   struct halmac_pg_num *tbl);

enum halmac_ret_status
rqpn_parser_88xx(struct halmac_adapter *adapter, enum halmac_trx_mode mode,
		 struct halmac_rqpn *tbl);

void
init_ofld_feature_state_machine_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
fwff_is_empty_88xx(struct halmac_adapter *adapter);

#endif /* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_INIT_88XX_H_ */
