/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HALMAC_API_8822B_H_
#define _HALMAC_API_8822B_H_

#include "../../halmac_2_platform.h"
#include "../../halmac_type.h"

HALMAC_RET_STATUS
halmac_mount_api_8822b(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_init_trx_cfg_8822b(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_TRX_MODE halmac_trx_mode
);

HALMAC_RET_STATUS
halmac_init_h2c_8822b(
	IN PHALMAC_ADAPTER pHalmac_adapter
);


#endif/* _HALMAC_API_8822B_H_ */
